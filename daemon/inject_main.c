// WisprFlow - uinput key injector utility
//   wispr-inject ctrl+win              tap a combo
//   wispr-inject ctrl+win press        press and hold
//   wispr-inject ctrl+win release      release
//   wispr-inject type "hello world"    type text (ASCII)
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include "uinput_keys.h"
#include "insert.h" // reuse parse_combo? no - reimplement minimal

static int lookup_mod(const char *name) {
    if (!strcasecmp(name, "ctrl") || !strcasecmp(name, "control")) return 29;  // KEY_LEFTCTRL
    if (!strcasecmp(name, "shift")) return 42;                                 // KEY_LEFTSHIFT
    if (!strcasecmp(name, "alt")) return 56;                                   // KEY_LEFTALT
    if (!strcasecmp(name, "super") || !strcasecmp(name, "win") || !strcasecmp(name, "meta")) return 125; // KEY_LEFTMETA
    return -1;
}

static int lookup_key(const char *name) {
    if (strlen(name) == 1 && name[0] >= 'a' && name[0] <= 'z') return 30 + (name[0] - 'a');
    if (strlen(name) == 1 && name[0] >= '0' && name[0] <= '9') return 2 + (name[0] - '1');
    if (!strcasecmp(name, "space")) return 57;
    if (!strcasecmp(name, "tab")) return 15;
    if (!strcasecmp(name, "enter")) return 28;
    if (!strcasecmp(name, "backspace")) return 14;
    if (!strcasecmp(name, "insert")) return 110;
    return -1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "usage: %s <combo> [press|release]   (default: tap)\n"
            "       %s <combo> hold               (press and hold until SIGINT)\n"
            "       %s type <text>                (type ASCII text)\n"
            "combos: ctrl+win, ctrl+shift+v, alt+tab, ...\n",
            argv[0], argv[0], argv[0]);
        return 2;
    }

    int fd = uk_open("wispr-inject");
    if (fd < 0) {
        fprintf(stderr, "cannot open /dev/uinput: %s\n", strerror(errno));
        return 1;
    }

    if (!strcmp(argv[1], "type")) {
        if (argc < 3) { fprintf(stderr, "missing text\n"); return 2; }
        int rc = uk_type_text(fd, argv[2]);
        if (rc == -2) fprintf(stderr, "text contains untypable characters (ASCII only)\n");
        uk_close(fd);
        return rc == 0 ? 0 : 1;
    }

    // parse combo: mods + optional key
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", argv[1]);
    int mods[4], nmods = 0, key = -1;
    bool any = false, unknown = false;
    char *save = NULL;
    for (char *tok = strtok_r(buf, "+", &save); tok; tok = strtok_r(NULL, "+", &save)) {
        int m = lookup_mod(tok);
        if (m >= 0) { if (nmods < 4) mods[nmods++] = m; any = true; }
        else {
            key = lookup_key(tok);
            if (key < 0) unknown = true;
            else any = true;
        }
    }
    if (unknown || !any || nmods == 0) {
        fprintf(stderr, "bad combo '%s' (need at least one modifier, e.g. ctrl+win)\n", argv[1]);
        return 2;
    }

    const char *action = argc > 2 ? argv[2] : "tap";
    if (!strcmp(action, "hold")) {
        // press and stay alive until SIGINT/SIGTERM, then release
        for (int i = 0; i < nmods; i++) uk_tap(fd, (int[]){mods[i]}, 1, NULL, 0);
        if (key >= 0) uk_tap(fd, (int[]){key}, 1, NULL, 0);
        fprintf(stderr, "holding %s - send SIGINT to release\n", argv[1]);
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        sigaddset(&set, SIGTERM);
        sigprocmask(SIG_BLOCK, &set, NULL);
        int sig;
        sigwait(&set, &sig);
        if (key >= 0) uk_tap(fd, NULL, 0, (int[]){key}, 1);
        for (int i = nmods - 1; i >= 0; i--) uk_tap(fd, NULL, 0, (int[]){mods[i]}, 1);
        fprintf(stderr, "released\n");
    } else if (!strcmp(action, "press")) {
        for (int i = 0; i < nmods; i++) uk_tap(fd, (int[]){mods[i]}, 1, NULL, 0);
        if (key >= 0) uk_tap(fd, (int[]){key}, 1, NULL, 0);
        fprintf(stderr, "pressed (hold until you run: %s %s release)\n", argv[0], argv[1]);
    } else if (!strcmp(action, "release")) {
        if (key >= 0) uk_tap(fd, NULL, 0, (int[]){key}, 1);
        for (int i = nmods - 1; i >= 0; i--) uk_tap(fd, NULL, 0, (int[]){mods[i]}, 1);
    } else {
        int down[5], up[5], nd = 0, nu = 0;
        for (int i = 0; i < nmods; i++) down[nd++] = mods[i];
        if (key >= 0) down[nd++] = key;
        if (key >= 0) up[nu++] = key;
        for (int i = nmods - 1; i >= 0; i--) up[nu++] = mods[i];
        uk_tap(fd, down, nd, up, nu);
    }
    uk_close(fd);
    return 0;
}
