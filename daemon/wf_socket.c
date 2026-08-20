#define _GNU_SOURCE
#include "wf_socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#define MAX_CLIENTS 8
#define MAX_LINE 16384

typedef struct {
    int fd;
    char buf[MAX_LINE];
    size_t len;
    pthread_mutex_t lock;
} Client;

struct WfSocket {
    char path[256];
    char status_json[2048];
    int listen_fd;
    pthread_t tid;
    volatile int running;
    pthread_mutex_t broadcast_lock;
    Client clients[MAX_CLIENTS];
    int nclients;
    volatile int recording;
    const volatile int *level_src;
    volatile int retranscribe_pending;
};


WfSocket *wf_socket_new(const char *sock_path, const char *status_json) {
    WfSocket *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    snprintf(s->path, sizeof(s->path), "%s", sock_path);
    snprintf(s->status_json, sizeof(s->status_json), "%s", status_json);
    for (int i = 0; i < MAX_CLIENTS; i++) pthread_mutex_init(&s->clients[i].lock, NULL);
    pthread_mutex_init(&s->broadcast_lock, NULL);
    return s;
}

void wf_socket_free(WfSocket *s) {
    if (!s) return;
    s->running = 0;
    if (s->tid) pthread_join(s->tid, NULL);
    if (s->listen_fd >= 0) close(s->listen_fd);
    unlink(s->path);
    free(s);
}

static void send_all(Client *c, const char *line) {
    pthread_mutex_lock(&c->lock);
    size_t total = strlen(line);
    size_t off = 0;
    while (off < total) {
        ssize_t w = write(c->fd, line + off, total - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            break; // client gone or error - leave the rest; poll() will reap it
        }
        if (w == 0) break;
        off += (size_t)w;
    }
    pthread_mutex_unlock(&c->lock);
}

void wf_socket_broadcast(WfSocket *s, const char *json_line) {
    if (!s) return;
    char line[MAX_LINE];
    snprintf(line, sizeof(line), "%s\n", json_line);
    pthread_mutex_lock(&s->broadcast_lock);
    for (int i = 0; i < s->nclients; i++) send_all(&s->clients[i], line);
    pthread_mutex_unlock(&s->broadcast_lock);
}

void wf_socket_set_status(WfSocket *s, const char *json) {
    if (!s || !json) return;
    pthread_mutex_lock(&s->broadcast_lock);
    snprintf(s->status_json, sizeof(s->status_json), "%s", json);
    pthread_mutex_unlock(&s->broadcast_lock);
}

void wf_socket_set_recording(WfSocket *s, bool recording) {
    if (!s) return;
    pthread_mutex_lock(&s->broadcast_lock);
    s->recording = recording ? 1 : 0;
    pthread_mutex_unlock(&s->broadcast_lock);
}

void wf_socket_set_level_source(WfSocket *s, const volatile int *level) {
    if (!s) return;
    pthread_mutex_lock(&s->broadcast_lock);
    s->level_src = level;
    pthread_mutex_unlock(&s->broadcast_lock);
}

bool wf_socket_take_retranscribe(WfSocket *s) {
    if (!s) return false;
    pthread_mutex_lock(&s->broadcast_lock);
    bool v = s->retranscribe_pending != 0;
    if (v) s->retranscribe_pending = 0;
    pthread_mutex_unlock(&s->broadcast_lock);
    return v;
}

void wf_socket_requeue_retranscribe(WfSocket *s) {
    if (!s) return;
    pthread_mutex_lock(&s->broadcast_lock);
    s->retranscribe_pending = 1;
    pthread_mutex_unlock(&s->broadcast_lock);
}

static void handle_line(WfSocket *s, Client *c, const char *line) {
    if (strstr(line, "\"cmd\"")) {
        if (strstr(line, "hello")) {
            send_all(c, s->status_json);
            send_all(c, "\n");
        } else if (strstr(line, "retranscribe")) {
            s->retranscribe_pending = 1;
            fprintf(stderr, "wispr-flow: socket: retranscribe requested\n");
        }
    } else {
        fprintf(stderr, "wispr-flow: socket: unknown line: %.80s\n", line);
    }
}

static Client *find_client(WfSocket *s, int fd) {
    for (int i = 0; i < s->nclients; i++)
        if (s->clients[i].fd == fd) return &s->clients[i];
    return NULL;
}

static int find_client_index(WfSocket *s, int fd) {
    for (int i = 0; i < s->nclients; i++)
        if (s->clients[i].fd == fd) return i;
    return -1;
}

static void *socket_thread(void *arg) {
    WfSocket *s = arg;
    long last_level_ms = 0;

    struct pollfd pfds[MAX_CLIENTS + 1];
    while (s->running) {
        // snapshot the client list; NEVER hold the broadcast lock across poll,
        // and never call wf_socket_broadcast() while holding it (it is not
        // recursive - doing so deadlocks the daemon).
        pthread_mutex_lock(&s->broadcast_lock);
        pfds[0].fd = s->listen_fd;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        int nfds = 1;
        for (int i = 0; i < s->nclients && nfds <= MAX_CLIENTS; i++) {
            pfds[nfds].fd = s->clients[i].fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }
        pthread_mutex_unlock(&s->broadcast_lock);

        int rc = poll(pfds, nfds, 40);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (rc > 0 && (pfds[0].revents & POLLIN)) {
            int fd = accept4(s->listen_fd, NULL, NULL, SOCK_CLOEXEC);
            if (fd >= 0) {
                pthread_mutex_lock(&s->broadcast_lock);
                if (s->nclients < MAX_CLIENTS) {
                    Client *c = &s->clients[s->nclients++];
                    // reset per-connection fields WITHOUT zeroing the mutex
                    // (it was initialized once in wf_socket_new; zeroing it
                    // corrupts the mutex on reuse / reconnect).
                    c->fd = fd;
                    c->len = 0;
                    c->buf[0] = 0;
                } else {
                    close(fd);
                }
                pthread_mutex_unlock(&s->broadcast_lock);
            }
        }

        // process readable clients: do NOT hold broadcast_lock across read()
        // Snapshot fds are in pfds[1..nfds-1]; read without lock, then
        // re-acquire lock to update per-client buffers.
        for (int i = 1; i < nfds; i++) {
            if (!(pfds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            int fd = pfds[i].fd;
            // peek avail under lock to know how much we can read, then unlock
            size_t avail;
            pthread_mutex_lock(&s->broadcast_lock);
            Client *tmpc = find_client(s, fd);
            if (!tmpc) { pthread_mutex_unlock(&s->broadcast_lock); continue; }
            avail = sizeof(tmpc->buf) - tmpc->len - 1;
            if (avail == 0) {
                tmpc->len = 0;
                avail = sizeof(tmpc->buf) - 1;
            }
            pthread_mutex_unlock(&s->broadcast_lock);
            // read without holding broadcast_lock
            char tbuf[MAX_LINE];
            if (avail > sizeof(tbuf)) avail = sizeof(tbuf);
            ssize_t r = read(fd, tbuf, avail);
            if (r <= 0) {
                pthread_mutex_lock(&s->broadcast_lock);
                int idx = find_client_index(s, fd);
                if (idx >= 0) {
                    close(s->clients[idx].fd);
                    // safe shift: keep mutexes in place, move only fd/len/buf
                    for (int j = idx; j + 1 < s->nclients; j++) {
                        s->clients[j].fd = s->clients[j + 1].fd;
                        s->clients[j].len = s->clients[j + 1].len;
                        memcpy(s->clients[j].buf, s->clients[j + 1].buf, sizeof(s->clients[j].buf));
                    }
                    s->nclients--;
                } else {
                    close(fd);
                }
                pthread_mutex_unlock(&s->broadcast_lock);
                continue;
            }
            pthread_mutex_lock(&s->broadcast_lock);
            Client *c = find_client(s, fd);
            if (!c) { pthread_mutex_unlock(&s->broadcast_lock); continue; }
            // re-check overflow (another thread could have appended, but only this thread does)
            if ((size_t)r > sizeof(c->buf) - c->len - 1) {
                c->len = 0;
            }
            size_t to_copy = (size_t)r;
            if (to_copy > sizeof(c->buf) - c->len - 1) to_copy = sizeof(c->buf) - c->len - 1;
            memcpy(c->buf + c->len, tbuf, to_copy);
            c->len += to_copy;
            c->buf[c->len] = 0;
            char *nl;
            while ((nl = strchr(c->buf, '\n')) != NULL) {
                *nl = 0;
                handle_line(s, c, c->buf);
                size_t rest = c->len - (size_t)(nl - c->buf) - 1;
                memmove(c->buf, nl + 1, rest);
                c->len = rest;
                c->buf[c->len] = 0;
            }
            pthread_mutex_unlock(&s->broadcast_lock);
        }

        // live audio levels while recording (~25 Hz); snapshot recording/level_src under lock
        bool is_rec;
        const volatile int *lvl_src;
        pthread_mutex_lock(&s->broadcast_lock);
        is_rec = s->recording != 0;
        lvl_src = s->level_src;
        pthread_mutex_unlock(&s->broadcast_lock);
        if (is_rec && lvl_src) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            long ms = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
            if (ms - last_level_ms >= 40) {
                last_level_ms = ms;
                int v = *lvl_src;
                if (v < 0) v = 0;
                if (v > 1000) v = 1000;
                char line[512];
                snprintf(line, sizeof(line), "{\"type\":\"level\",\"v\":%d}", v);
                wf_socket_broadcast(s, line);
            }
        }
    }
    return NULL;
}

int wf_socket_start(WfSocket *s) {
    unlink(s->path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%.107s", s->path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    chmod(s->path, 0600);
    if (listen(fd, 4) != 0) {
        close(fd);
        unlink(s->path);
        return -1;
    }
    s->listen_fd = fd;
    s->running = 1;
    if (pthread_create(&s->tid, NULL, socket_thread, s) != 0) {
        s->running = 0;
        close(fd);
        unlink(s->path);
        return -1;
    }
    return 0;
}
