#define _GNU_SOURCE
#include "insert.h"
#include "wf_proc.h"
#include "uinput_keys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <linux/input-event-codes.h>

// ---------------------------------------------------------------------------
// key mapping for paste shortcuts ("ctrl+v", "ctrl+shift+insert", ...)

typedef struct { const char *name; int code; } KeyMap;

static const KeyMap key_map[] = {
    { "a", KEY_A }, { "b", KEY_B }, { "c", KEY_C }, { "d", KEY_D }, { "e", KEY_E },
    { "f", KEY_F }, { "g", KEY_G }, { "h", KEY_H }, { "i", KEY_I }, { "j", KEY_J },
    { "k", KEY_K }, { "l", KEY_L }, { "m", KEY_M }, { "n", KEY_N }, { "o", KEY_O },
    { "p", KEY_P }, { "q", KEY_Q }, { "r", KEY_R }, { "s", KEY_S }, { "t", KEY_T },
    { "u", KEY_U }, { "v", KEY_V }, { "w", KEY_W }, { "x", KEY_X }, { "y", KEY_Y },
    { "z", KEY_Z },
    { "0", KEY_0 }, { "1", KEY_1 }, { "2", KEY_2 }, { "3", KEY_3 }, { "4", KEY_4 },
    { "5", KEY_5 }, { "6", KEY_6 }, { "7", KEY_7 }, { "8", KEY_8 }, { "9", KEY_9 },
    { "space", KEY_SPACE }, { "tab", KEY_TAB }, { "enter", KEY_ENTER },
    { "backspace", KEY_BACKSPACE }, { "insert", KEY_INSERT }, { "home", KEY_HOME },
    { "end", KEY_END }, { "pageup", KEY_PAGEUP }, { "pagedown", KEY_PAGEDOWN },
    { "comma", KEY_COMMA }, { "period", KEY_DOT }, { "slash", KEY_SLASH },
    { "semicolon", KEY_SEMICOLON }, { "apostrophe", KEY_APOSTROPHE },
    { "minus", KEY_MINUS }, { "equal", KEY_EQUAL }, { "backquote", KEY_GRAVE },
    { "backslash", KEY_BACKSLASH }, { "bracketleft", KEY_LEFTBRACE },
    { "bracketright", KEY_RIGHTBRACE },
    { "f1", KEY_F1 }, { "f2", KEY_F2 }, { "f3", KEY_F3 }, { "f4", KEY_F4 },
    { "f5", KEY_F5 }, { "f6", KEY_F6 }, { "f7", KEY_F7 }, { "f8", KEY_F8 },
    { "f9", KEY_F9 }, { "f10", KEY_F10 }, { "f11", KEY_F11 }, { "f12", KEY_F12 },
    { NULL, 0 }
};

static int lookup_key(const char *name, int *code) {
    for (const KeyMap *k = key_map; k->name; k++)
        if (!strcasecmp(k->name, name)) { *code = k->code; return 0; }
    return -1;
}

// parse "ctrl+shift+v" -> modifier codes + key code
static int parse_combo(const char *combo, int mods[4], int *nmods, int *key) {
    *nmods = 0;
    *key = 0;
    bool key_set = false;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", combo);
    char *save = NULL;
    char *tok = strtok_r(buf, "+", &save);
    while (tok) {
        int mod = -1;
        if (!strcasecmp(tok, "ctrl") || !strcasecmp(tok, "control")) mod = KEY_LEFTCTRL;
        else if (!strcasecmp(tok, "shift")) mod = KEY_LEFTSHIFT;
        else if (!strcasecmp(tok, "alt")) mod = KEY_LEFTALT;
        else if (!strcasecmp(tok, "super") || !strcasecmp(tok, "win") ||
                 !strcasecmp(tok, "meta")) mod = KEY_LEFTMETA;
        if (mod >= 0) {
            if (*nmods < 4) mods[(*nmods)++] = mod;
        } else {
            if (lookup_key(tok, key) != 0) return -1;
            key_set = true;
        }
        tok = strtok_r(NULL, "+", &save);
    }
    return key_set ? 0 : -1;
}

// ---------------------------------------------------------------------------

static int set_clipboard(const char *text, char *errbuf, size_t errsz) {
    const char *argv[] = { "wl-copy", NULL };
    int rc = wf_run_pipe(argv, text, strlen(text));
    if (rc != 0) {
        snprintf(errbuf, errsz,
                 "wl-copy failed (exit %d) - is wl-clipboard installed and WAYLAND_DISPLAY set?", rc);
        return -1;
    }
    return 0;
}

static int send_paste_combo(const Config *cfg, char *errbuf, size_t errsz) {
    int mods[4], nmods, key;
    if (parse_combo(cfg->paste_combo, mods, &nmods, &key) != 0) {
        snprintf(errbuf, errsz, "cannot parse paste_combo '%s'", cfg->paste_combo);
        return -1;
    }

    // primary: uinput virtual keyboard (no external daemon needed)
    int fd = uk_open("wispr-flow virtual keyboard");
    if (fd >= 0) {
        // press modifiers + key, release key + modifiers
        int down[5], up[5];
        int nd = 0, nu = 0;
        for (int i = 0; i < nmods; i++) down[nd++] = mods[i];
        down[nd++] = key;
        up[nu++] = key;
        for (int i = nmods - 1; i >= 0; i--) up[nu++] = mods[i];
        int rc = uk_tap(fd, down, nd, up, nu);
        uk_close(fd);
        if (rc == 0) return 0;
    }

    // fallback: ydotool
    char **argv = calloc(3 + 2 * nmods + 2 + 1, sizeof(char *));
    char *args = calloc(1, 64 + 16 * (3 + 2 * nmods + 2 + 1));
    if (!argv || !args) { free(argv); free(args); snprintf(errbuf, errsz, "out of memory"); return -1; }
    size_t off = 0;
    #define PUSH(s) do { argv[nargs++] = args + off; off += snprintf(args + off, 16 + 64, "%s", (s)) + 1; } while (0)
    int nargs = 0;
    PUSH("ydotool");
    PUSH("key");
    char tmp[16];
    for (int i = 0; i < nmods; i++) {
        snprintf(tmp, sizeof(tmp), "%d:1", mods[i]);
        PUSH(tmp);
    }
    snprintf(tmp, sizeof(tmp), "%d:1", key);
    PUSH(tmp);
    snprintf(tmp, sizeof(tmp), "%d:0", key);
    PUSH(tmp);
    for (int i = nmods - 1; i >= 0; i--) {
        snprintf(tmp, sizeof(tmp), "%d:0", mods[i]);
        PUSH(tmp);
    }
    argv[nargs] = NULL;
    #undef PUSH

    int rc = wf_run((const char **)argv);
    free(argv);
    free(args);
    if (rc != 0) {
        snprintf(errbuf, errsz,
                 "cannot send keys: uinput unavailable and ydotool failed (exit %d)", rc);
        return -1;
    }
    return 0;
}

static int insert_paste(const Config *cfg, const char *text, char *errbuf, size_t errsz) {
    char *old = NULL;
    size_t oldlen = 0;

    if (cfg->restore_clipboard) {
        const char *argv[] = { "wl-paste", "-n", "--no-newline", NULL };
        int rc = wf_run_capture(argv, &old, &oldlen);
        if (rc != 0) { free(old); old = NULL; } // nothing to restore
        else if (oldlen > 4 * 1024 * 1024) { free(old); old = NULL; }
    }

    if (set_clipboard(text, errbuf, errsz) != 0) { free(old); return -1; }
    usleep(200 * 1000); // let the clipboard propagate

    if (send_paste_combo(cfg, errbuf, errsz) != 0) {
        free(old);
        return -1;
    }

    usleep(600 * 1000); // let the app handle the paste

    if (old) {
        char restore_err[256];
        if (set_clipboard(old, restore_err, sizeof(restore_err)) != 0) {
            fprintf(stderr, "wispr-flow: warning: could not restore clipboard: %s\n", restore_err);
        }
        free(old);
    }
    return 0;
}

static int insert_type(const char *text, char *errbuf, size_t errsz) {
    // primary: uinput
    int fd = uk_open("wispr-flow virtual keyboard");
    if (fd >= 0) {
        int rc = uk_type_text(fd, text);
        uk_close(fd);
        if (rc == 0) return 0;
        if (rc == -2) {
            snprintf(errbuf, errsz,
                     "text contains characters that cannot be typed - use insert_mode = paste");
            return -1;
        }
    }
    // fallback: ydotool
    const char *argv[] = { "ydotool", "type", "--key-delay=5", text, NULL };
    int rc = wf_run(argv);
    if (rc != 0) {
        snprintf(errbuf, errsz,
                 "ydotool type failed (exit %d) - is ydotool installed and ydotoold running?", rc);
        return -1;
    }
    return 0;
}

int insert_text(const Config *cfg, const char *text, char *errbuf, size_t errsz) {
    if (!strcmp(cfg->insert_mode, "type")) {
        return insert_type(text, errbuf, errsz);
    }
    return insert_paste(cfg, text, errbuf, errsz);
}
