#define _GNU_SOURCE
#include "hotkey.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>

#define MAX_DEVICES 64
#define RESCAN_INTERVAL_MS 4000

typedef struct {
    char name[128];
    int  fd;
} Dev;

struct Hotkey {
    Dev      devs[MAX_DEVICES];
    int      ndevs;
    bool     ctrl_down;
    bool     meta_down;
    bool     combo;
    bool     allow_virtual;
    wf_hotkey_cb cb;
    void    *ud;
    long     last_rescan_ms;
};

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static bool name_is_virtual(const char *name) {
    if (!name) return false;
    const char *bad[] = {
        "ydotool", "uinput", "wtype", "virtual", "keyd", "xremap",
        "deej", "input-remapper", "waydroid", "wtype", "evdev", "ydotoold", NULL
    };
    for (int i = 0; bad[i]; i++)
        if (strcasestr(name, bad[i])) return true;
    return false;
}

static bool dev_ok(const char *name, int fd, bool allow_virtual) {
    if (!allow_virtual && name_is_virtual(name)) return false;

    unsigned long evbits[EV_MAX / 64 + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0) return false;
    if (!(evbits[EV_KEY / 64] & (1UL << (EV_KEY % 64)))) return false;

    unsigned long keybits[KEY_MAX / 64 + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0) return false;
#define KEY_TEST(k) ((keybits[(k) / 64] & (1UL << ((k) % 64))) != 0)
    // must be a real keyboard: has both Ctrl and Meta keys
    if (!KEY_TEST(KEY_LEFTCTRL) || !KEY_TEST(KEY_LEFTMETA)) return false;
    return true;
}

static int open_dev(const char *path, Dev *out, bool allow_virtual) {
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    char name[128] = {0};
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0) {
        close(fd);
        return -1;
    }
    if (!dev_ok(name, fd, allow_virtual)) {
        close(fd);
        return -1;
    }
    snprintf(out->name, sizeof(out->name), "%s", name);
    out->fd = fd;
    return 0;
}

static int rescan(Hotkey *h) {
    for (int i = 0; i < h->ndevs; i++)
        if (h->devs[i].fd >= 0) close(h->devs[i].fd);
    h->ndevs = 0;

    bool ctrl = h->ctrl_down, meta = h->meta_down;
    int added = 0;
    for (int i = 0; i < 64; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        if (access(path, R_OK) != 0) continue;
        Dev d;
        if (open_dev(path, &d, h->allow_virtual) == 0) {
            if (h->ndevs < MAX_DEVICES) {
                h->devs[h->ndevs++] = d;
                added++;
                // initialize modifier state from the kernel's current key state
                // (evdev does not replay events to late subscribers)
                unsigned long keys[KEY_MAX / 64 + 1] = {0};
                if (ioctl(d.fd, EVIOCGKEY(sizeof(keys)), keys) >= 0) {
#define KEY_IS_DOWN(k) ((keys[(k) / 64] & (1UL << ((k) % 64))) != 0)
                    if (KEY_IS_DOWN(KEY_LEFTCTRL) || KEY_IS_DOWN(KEY_RIGHTCTRL)) ctrl = true;
                    if (KEY_IS_DOWN(KEY_LEFTMETA) || KEY_IS_DOWN(KEY_RIGHTMETA)) meta = true;
#undef KEY_IS_DOWN
                }
            } else {
                close(d.fd);
            }
        }
    }
    h->ctrl_down = ctrl;
    h->meta_down = meta;
    bool combo = h->ctrl_down && h->meta_down;
    if (combo != h->combo) {
        h->combo = combo;
        if (h->cb) h->cb(h->ud, combo);
    }
    return added;
}

Hotkey *hotkey_new(void) {
    Hotkey *h = calloc(1, sizeof(*h));
    if (!h) return NULL;
    for (int i = 0; i < MAX_DEVICES; i++) h->devs[i].fd = -1;
    h->last_rescan_ms = now_ms();
    int n = rescan(h);
    fprintf(stderr, "wispr-flow: hotkey: watching %d input device(s)\n", n);
    for (int i = 0; i < h->ndevs; i++)
        fprintf(stderr, "wispr-flow: hotkey:   - %s\n", h->devs[i].name);
    return h;
}

void hotkey_free(Hotkey *h) {
    if (!h) return;
    for (int i = 0; i < h->ndevs; i++)
        if (h->devs[i].fd >= 0) close(h->devs[i].fd);
    free(h);
}

void hotkey_set_callback(Hotkey *h, wf_hotkey_cb cb, void *ud) {
    h->cb = cb;
    h->ud = ud;
    // the combo may already be held (e.g. hotplugged keyboard with keys down)
    if (h->combo && h->cb) h->cb(h->ud, true);
}

void hotkey_set_allow_virtual(Hotkey *h, bool allow) {
    h->allow_virtual = allow;
}

bool hotkey_combo_active(const Hotkey *h) { return h->combo; }

int hotkey_device_count(const Hotkey *h) { return h->ndevs; }
const char *hotkey_device_name(const Hotkey *h, int i) {
    return (i >= 0 && i < h->ndevs) ? h->devs[i].name : NULL;
}

static void handle_event(Hotkey *h, const struct input_event *ev) {
    if (ev->type != EV_KEY || (ev->value != 0 && ev->value != 1)) return;
    bool pressed = ev->value == 1;
    switch (ev->code) {
        case KEY_LEFTCTRL:
        case KEY_RIGHTCTRL:
            h->ctrl_down = pressed;
            break;
        case KEY_LEFTMETA:
        case KEY_RIGHTMETA:
            h->meta_down = pressed;
            break;
        default:
            return;
    }
    bool combo = h->ctrl_down && h->meta_down;
    if (combo != h->combo) {
        h->combo = combo;
        if (h->cb) h->cb(h->ud, combo);
    }
}

int hotkey_poll(Hotkey *h, int timeout_ms) {
    // periodic rescan for hotplugged keyboards
    long t = now_ms();
    if (t - h->last_rescan_ms > RESCAN_INTERVAL_MS) {
        h->last_rescan_ms = t;
        rescan(h);
    }

    struct pollfd pfds[MAX_DEVICES];
    for (int i = 0; i < h->ndevs; i++) {
        pfds[i].fd = h->devs[i].fd;
        pfds[i].events = POLLIN;
        pfds[i].revents = 0;
    }

    int r = poll(pfds, h->ndevs, timeout_ms);
    if (r < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }
    if (r == 0) return 0;

    for (int i = 0; i < h->ndevs; i++) {
        if (!(pfds[i].revents & POLLIN)) continue;
        struct input_event ev;
        for (;;) {
            ssize_t n = read(h->devs[i].fd, &ev, sizeof(ev));
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                break;
            }
            if (n != (ssize_t)sizeof(ev)) break;
            handle_event(h, &ev);
        }
    }
    return 0;
}
