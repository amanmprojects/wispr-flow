#define _GNU_SOURCE
#include "uinput_keys.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/uinput.h>
#include <linux/input-event-codes.h>

#define UK_MSLEEP(ms) usleep((ms) * 1000)

static int ev_key(int fd, unsigned int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_KEY;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof(ev)) < 0) return -1;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    if (write(fd, &ev, sizeof(ev)) < 0) return -1;
    return 0;
}

int uk_open(const char *name) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;

    // enable the key classes we may need
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    for (int c = KEY_ESC; c <= KEY_MICMUTE; c++) ioctl(fd, UI_SET_KEYBIT, c);

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    snprintf(usetup.name, sizeof(usetup.name), "%s", name);
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x1209; // generic
    usetup.id.product = 0x5746; // "WF"
    usetup.id.version = 1;
    if (ioctl(fd, UI_DEV_SETUP, &usetup) != 0) {
        close(fd);
        return -1;
    }
    if (ioctl(fd, UI_DEV_CREATE) != 0) {
        close(fd);
        return -1;
    }
    UK_MSLEEP(120); // let the compositor pick the device up
    return fd;
}

void uk_close(int fd) {
    if (fd >= 0) {
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
    }
}

int uk_tap(int fd, const int *down, int n_down, const int *up, int n_up) {
    if (fd < 0) { errno = EBADF; return -1; }
    for (int i = 0; i < n_down; i++) {
        if (ev_key(fd, down[i], 1) < 0) return -1;
        UK_MSLEEP(15);
    }
    UK_MSLEEP(30);
    for (int i = 0; i < n_up; i++) {
        if (ev_key(fd, up[i], 0) < 0) return -1;
        UK_MSLEEP(15);
    }
    return 0;
}

// --- ASCII typing ----------------------------------------------------------

typedef struct { char ch; int code; int shift; } CharMap;

static const CharMap cmap[] = {
    { 'a', KEY_A, 0 }, { 'b', KEY_B, 0 }, { 'c', KEY_C, 0 }, { 'd', KEY_D, 0 },
    { 'e', KEY_E, 0 }, { 'f', KEY_F, 0 }, { 'g', KEY_G, 0 }, { 'h', KEY_H, 0 },
    { 'i', KEY_I, 0 }, { 'j', KEY_J, 0 }, { 'k', KEY_K, 0 }, { 'l', KEY_L, 0 },
    { 'm', KEY_M, 0 }, { 'n', KEY_N, 0 }, { 'o', KEY_O, 0 }, { 'p', KEY_P, 0 },
    { 'q', KEY_Q, 0 }, { 'r', KEY_R, 0 }, { 's', KEY_S, 0 }, { 't', KEY_T, 0 },
    { 'u', KEY_U, 0 }, { 'v', KEY_V, 0 }, { 'w', KEY_W, 0 }, { 'x', KEY_X, 0 },
    { 'y', KEY_Y, 0 }, { 'z', KEY_Z, 0 },
    { 'A', KEY_A, 1 }, { 'B', KEY_B, 1 }, { 'C', KEY_C, 1 }, { 'D', KEY_D, 1 },
    { 'E', KEY_E, 1 }, { 'F', KEY_F, 1 }, { 'G', KEY_G, 1 }, { 'H', KEY_H, 1 },
    { 'I', KEY_I, 1 }, { 'J', KEY_J, 1 }, { 'K', KEY_K, 1 }, { 'L', KEY_L, 1 },
    { 'M', KEY_M, 1 }, { 'N', KEY_N, 1 }, { 'O', KEY_O, 1 }, { 'P', KEY_P, 1 },
    { 'Q', KEY_Q, 1 }, { 'R', KEY_R, 1 }, { 'S', KEY_S, 1 }, { 'T', KEY_T, 1 },
    { 'U', KEY_U, 1 }, { 'V', KEY_V, 1 }, { 'W', KEY_W, 1 }, { 'X', KEY_X, 1 },
    { 'Y', KEY_Y, 1 }, { 'Z', KEY_Z, 1 },
    { '0', KEY_0, 0 }, { '1', KEY_1, 0 }, { '2', KEY_2, 0 }, { '3', KEY_3, 0 },
    { '4', KEY_4, 0 }, { '5', KEY_5, 0 }, { '6', KEY_6, 0 }, { '7', KEY_7, 0 },
    { '8', KEY_8, 0 }, { '9', KEY_9, 0 },
    { ' ', KEY_SPACE, 0 }, { '\t', KEY_TAB, 0 }, { '\n', KEY_ENTER, 0 },
    { ',', KEY_COMMA, 0 }, { '.', KEY_DOT, 0 }, { '/', KEY_SLASH, 0 },
    { ';', KEY_SEMICOLON, 0 }, { '\'', KEY_APOSTROPHE, 0 },
    { '[', KEY_LEFTBRACE, 0 }, { ']', KEY_RIGHTBRACE, 0 },
    { '\\', KEY_BACKSLASH, 0 }, { '-', KEY_MINUS, 0 }, { '=', KEY_EQUAL, 0 },
    { '`', KEY_GRAVE, 0 },
    { '!', KEY_1, 1 }, { '@', KEY_2, 1 }, { '#', KEY_3, 1 }, { '$', KEY_4, 1 },
    { '%', KEY_5, 1 }, { '^', KEY_6, 1 }, { '&', KEY_7, 1 }, { '*', KEY_8, 1 },
    { '(', KEY_9, 1 }, { ')', KEY_0, 1 },
    { '_', KEY_MINUS, 1 }, { '+', KEY_EQUAL, 1 },
    { '{', KEY_LEFTBRACE, 1 }, { '}', KEY_RIGHTBRACE, 1 }, { '|', KEY_BACKSLASH, 1 },
    { ':', KEY_SEMICOLON, 1 }, { '"', KEY_APOSTROPHE, 1 },
    { '<', KEY_COMMA, 1 }, { '>', KEY_DOT, 1 }, { '?', KEY_SLASH, 1 },
    { '~', KEY_GRAVE, 1 },
    { 0, 0, 0 }
};

static const CharMap *char_lookup(char c) {
    for (const CharMap *m = cmap; m->ch; m++)
        if (m->ch == c) return m;
    return NULL;
}

int uk_type_text(int fd, const char *text) {
    if (fd < 0) { errno = EBADF; return -1; }
    for (const char *p = text; *p; p++) {
        const CharMap *m = char_lookup(*p);
        if (!m) return -2; // untypable character
        if (m->shift) {
            if (ev_key(fd, KEY_LEFTSHIFT, 1) < 0) return -1;
            UK_MSLEEP(8);
        }
        if (ev_key(fd, m->code, 1) < 0) return -1;
        UK_MSLEEP(8);
        if (ev_key(fd, m->code, 0) < 0) return -1;
        if (m->shift) {
            UK_MSLEEP(8);
            if (ev_key(fd, KEY_LEFTSHIFT, 0) < 0) return -1;
        }
        UK_MSLEEP(6);
    }
    return 0;
}
