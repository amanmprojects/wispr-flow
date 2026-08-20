#define _GNU_SOURCE
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

static void expand_tilde(char *out, size_t outsz, const char *in) {
    if (in[0] == '~' && in[1] == '/') {
        const char *home = getenv("HOME");
        if (!home || !home[0]) home = "/tmp";
        snprintf(out, outsz, "%s%s", home, in + 1);
        return;
    }
    snprintf(out, outsz, "%s", in);
}

void config_defaults(Config *c) {
    memset(c, 0, sizeof(*c));
    expand_tilde(c->model, sizeof(c->model),
                 "~/.local/share/wispr-flow/ggml-large-v3-turbo-q5_0.bin");
    snprintf(c->language, sizeof(c->language), "en");
    c->source[0] = 0;
    snprintf(c->insert_mode, sizeof(c->insert_mode), "paste");
    snprintf(c->paste_combo, sizeof(c->paste_combo), "ctrl+v");
    c->initial_prompt[0] = 0; // auto
    c->whisper_threads = 4;
    c->no_speech_thold = 0.6f;
    c->logprob_thold = -1.0f;
    c->min_audio_ms = 300;
    c->max_audio_s = 120;
    c->stop_tail_ms = 350;
    c->trim_trailing = false;
    c->notify = true;
    c->beep = true;
    c->capitalize = true;
    c->restore_clipboard = true;
    c->allow_virtual = false;
}

// trim spaces around a token, strip surrounding quotes
static char *trim_val(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') || (s[0] == '\'' && s[n - 1] == '\''))) {
        s[n - 1] = 0;
        s++;
    }
    return s;
}

int config_load(Config *c, const char *path, char *errbuf, size_t errsz) {
    char p[1024];
    if (path) {
        snprintf(p, sizeof(p), "%s", path);
    } else {
        const char *xdg = getenv("XDG_CONFIG_HOME");
        if (xdg && xdg[0]) snprintf(p, sizeof(p), "%s/wispr-flow/config", xdg);
        else {
            const char *home = getenv("HOME");
            if (!home || !home[0]) home = "/tmp";
            snprintf(p, sizeof(p), "%s/.config/wispr-flow/config", home);
        }
    }

    FILE *f = fopen(p, "r");
    if (!f) {
        if (path) { snprintf(errbuf, errsz, "cannot open config %s: %s", p, strerror(errno)); return -1; }
        return 0; // default config file absent -> defaults
    }

    char line[4096];
    int lineno = 0;
    int rc = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *s = line;
        // strip comments: '#' outside single/double quotes
        bool in_squote = false, in_dquote = false;
        for (char *q = s; *q; q++) {
            if (!in_squote && !in_dquote && *q == '#') { *q = 0; break; }
            if (*q == '\'' && !in_dquote) in_squote = !in_squote;
            else if (*q == '"' && !in_squote) in_dquote = !in_dquote;
        }
        // find '='
        char *eq = strchr(s, '=');
        if (!eq) {
            if (strlen(s) > 0 && strspn(s, " \t\r\n") != strlen(s))
                fprintf(stderr, "wispr-flow: %s:%d: ignoring malformed line\n", p, lineno);
            continue;
        }
        *eq = 0;
        char *key = trim_val(s);
        char *val = trim_val(eq + 1);
        if (!*key) continue;
        if (!strcmp(key, "model"))            expand_tilde(c->model, sizeof(c->model), val);
        else if (!strcmp(key, "language"))    snprintf(c->language, sizeof(c->language), "%s", val);
        else if (!strcmp(key, "source"))      snprintf(c->source, sizeof(c->source), "%s", val);
        else if (!strcmp(key, "insert_mode")) snprintf(c->insert_mode, sizeof(c->insert_mode), "%s", val);
        else if (!strcmp(key, "paste_combo")) snprintf(c->paste_combo, sizeof(c->paste_combo), "%s", val);
        else if (!strcmp(key, "initial_prompt")) snprintf(c->initial_prompt, sizeof(c->initial_prompt), "%s", val);
        else if (!strcmp(key, "whisper_threads"))   c->whisper_threads = atoi(val);
        else if (!strcmp(key, "no_speech_thold"))   c->no_speech_thold = atof(val);
        else if (!strcmp(key, "logprob_thold"))     c->logprob_thold = atof(val);
        else if (!strcmp(key, "min_audio_ms"))      c->min_audio_ms = atoi(val);
        else if (!strcmp(key, "max_audio_s"))       c->max_audio_s = atoi(val);
        else if (!strcmp(key, "stop_tail_ms"))      c->stop_tail_ms = atoi(val);
        else if (!strcmp(key, "trim_trailing_silence")) c->trim_trailing = !strcmp(val, "true") || !strcmp(val, "1") || !strcmp(val, "yes");
        else if (!strcmp(key, "notify"))            c->notify = !strcmp(val, "true") || !strcmp(val, "1") || !strcmp(val, "yes");
        else if (!strcmp(key, "beep"))              c->beep = !strcmp(val, "true") || !strcmp(val, "1") || !strcmp(val, "yes");
        else if (!strcmp(key, "capitalize"))        c->capitalize = !strcmp(val, "true") || !strcmp(val, "1") || !strcmp(val, "yes");
        else if (!strcmp(key, "restore_clipboard")) c->restore_clipboard = !strcmp(val, "true") || !strcmp(val, "1") || !strcmp(val, "yes");
        else if (!strcmp(key, "allow_virtual"))     c->allow_virtual = !strcmp(val, "true") || !strcmp(val, "1") || !strcmp(val, "yes");
        else fprintf(stderr, "wispr-flow: %s:%d: unknown key '%s'\n", p, lineno, key);
    }
    fclose(f);
    return rc;
}
