#define _GNU_SOURCE
#include "wf_proc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>

// ---------------------------------------------------------------------------
// run argv, wait, return exit code
int wf_run(const char **argv) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        // child
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

// ---------------------------------------------------------------------------
// run argv, capture stdout (malloc'd); returns exit code
int wf_run_capture(const char **argv, char **out, size_t *outlen) {
    int fds[2];
    if (pipe(fds) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], 1);
        close(fds[1]);
        // silence child stderr
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, 2); close(devnull); }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    close(fds[1]);

    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) { close(fds[0]); waitpid(pid, NULL, 0); return -1; }
    for (;;) {
        if (len + 4096 > cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); close(fds[0]); waitpid(pid, NULL, 0); return -1; }
            buf = nb;
        }
        ssize_t r = read(fds[0], buf + len, cap - len - 1);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) break;
        len += (size_t)r;
        if (len > 4 * 1024 * 1024) break; // safety cap
    }
    close(fds[0]);
    buf[len] = 0;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (out) *out = buf; else free(buf);
    if (outlen) *outlen = len;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

// ---------------------------------------------------------------------------
// run argv, feed input on stdin; returns exit code
int wf_run_pipe(const char **argv, const char *in, size_t inlen) {
    int fds[2];
    if (pipe(fds) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
        close(fds[1]);
        dup2(fds[0], 0);
        close(fds[0]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    close(fds[0]);

    size_t off = 0;
    while (off < inlen) {
        ssize_t w = write(fds[1], in + off, inlen - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += (size_t)w;
    }
    close(fds[1]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}
