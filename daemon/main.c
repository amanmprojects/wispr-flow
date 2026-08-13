#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>

#include "config.h"
#include "hotkey.h"
#include "audio.h"
#include "transcribe.h"
#include "insert.h"
#include "wf_socket.h"

typedef enum { S_IDLE, S_RECORDING, S_PROCESSING } State;

typedef struct {
    Config      cfg;
    Hotkey     *hk;
    Rec        *rec;
    Transcribe  tr;
    WfSocket   *sock;
    State       st;
    long        rec_started_ms;
    char        state_dir[512];   // ~/.local/state/wispr-flow
    char        lock_path[600];   // flock file for single-instance protection
    char        last_wav[600];    // last_wav path (the exact recording used)
    char        status_json[2048];// sent to UI clients on connect
    char        err[512];
} App;

static volatile sig_atomic_t g_quit = 0;

static void on_signal(int sig) { (void)sig; g_quit = 1; }

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void log_msg(const char *fmt, ...) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);
    fprintf(stderr, "[%s] wispr-flow: ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void notify(const App *a, const char *summary, const char *body) {
    if (!a->cfg.notify) return;
    if (fork() == 0) {
        setsid();
        execlp("notify-send", "notify-send", "-a", "WisprFlow",
               "-t", "2000", summary, body, (char *)NULL);
        _exit(127);
    }
}

// --- state dir / recording persistence ------------------------------------

static void make_state_dir(App *a) {
    const char *xdg = getenv("XDG_STATE_HOME");
    if (xdg && xdg[0]) snprintf(a->state_dir, sizeof(a->state_dir), "%s/wispr-flow", xdg);
    else snprintf(a->state_dir, sizeof(a->state_dir), "%s/.local/state/wispr-flow", getenv("HOME") ?: "/tmp");
    mkdir(a->state_dir, 0700);
    snprintf(a->last_wav, sizeof(a->last_wav), "%s/last.wav", a->state_dir);
    snprintf(a->lock_path, sizeof(a->lock_path), "%s/daemon.lock", a->state_dir);
}

// save the exact recording that will be transcribed (for the replay/debug UI)
static void save_wav(const char *path, const int16_t *samples, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t rate = 16000;
    uint16_t ch = 1, bits = 16;
    uint32_t dsz = (uint32_t)(n * sizeof(int16_t));
    fwrite("RIFF", 1, 4, f);
    uint32_t riffsz = 36 + dsz;
    fwrite(&riffsz, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fmtz = 16;
    fwrite(&fmtz, 4, 1, f);
    uint16_t pcm = 1;
    fwrite(&pcm, 2, 1, f);
    fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    uint32_t brate = rate * ch * bits / 8;
    fwrite(&brate, 4, 1, f);
    uint16_t align = ch * bits / 8;
    fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dsz, 4, 1, f);
    fwrite(samples, 1, dsz, f);
    fclose(f);
}

// --- UI events --------------------------------------------------------------

static void ui_state(App *a, const char *state) {
    char line[128];
    snprintf(line, sizeof(line), "{\"type\":\"state\",\"state\":\"%s\"}", state);
    wf_socket_broadcast(a->sock, line);
}

static void ui_done(App *a, bool ok, const char *text, long rec_ms,
                    long whisper_ms, bool no_speech, const char *error) {
    char text_esc[8192], err_esc[1024];
    // json escaping helper lives in wf_socket.c; do a minimal local escape here
    char *te = text_esc, *ee = err_esc;
    for (const char *p = text ? text : ""; *p && te < text_esc + sizeof(text_esc) - 8; p++) {
        if (*p == '"' || *p == '\\') *te++ = '\\';
        *te++ = *p;
    }
    *te = 0;
    for (const char *p = error ? error : ""; *p && ee < err_esc + sizeof(err_esc) - 8; p++) {
        if (*p == '"' || *p == '\\') *ee++ = '\\';
        *ee++ = *p;
    }
    *ee = 0;
    char line[16384];
    snprintf(line, sizeof(line),
             "{\"type\":\"done\",\"ok\":%s,\"text\":\"%s\",\"rec_ms\":%ld,"
             "\"whisper_ms\":%ld,\"no_speech\":%s,\"error\":\"%s\",\"path\":\"%s\"}",
             ok ? "true" : "false", text_esc, rec_ms, whisper_ms,
             no_speech ? "true" : "false", err_esc, a->last_wav);
    wf_socket_broadcast(a->sock, line);
}

// --- recording / transcription ----------------------------------------------

static void start_recording(App *a) {
    if (a->st != S_IDLE) return;
    if (rec_start(a->rec, a->cfg.source, a->err, sizeof(a->err)) != 0) {
        log_msg("error: %s", a->err);
        notify(a, "WisprFlow", "Cannot record — check your microphone");
        if (a->cfg.beep) wf_beep(220, 200, 0.4);
        ui_done(a, false, "", 0, 0, false, a->err);
        return;
    }
    a->st = S_RECORDING;
    a->rec_started_ms = now_ms();
    log_msg("recording started (source '%s')", a->cfg.source[0] ? a->cfg.source : "default");
    if (a->cfg.beep) wf_beep(880, 60, 0.3);
    // no notify here: the UI shows a Plasma OSD popup for this state
    wf_socket_set_recording(a->sock, true);
    ui_state(a, "recording");
}

// transcribe the given samples; broadcast result; returns malloc'd text or NULL
static char *process_audio(App *a, const int16_t *samples, size_t n,
                           long rec_ms, bool *no_speech, double *whisper_ms) {
    double elapsed = 0.0;
    char *text = tr_run(&a->tr, &a->cfg, samples, n, no_speech, &elapsed);
    if (whisper_ms) *whisper_ms = elapsed;
    if (text) {
        log_msg("transcribed (%.1f s audio, whisper %.0f ms): %s",
                rec_ms / 1000.0, elapsed, text);
    }
    return text;
}

static void finish_recording(App *a, bool cancelled) {
    if (a->st != S_RECORDING) return;

    int16_t *samples = NULL;
    size_t n = 0;
    if (rec_stop_tail(a->rec, a->cfg.stop_tail_ms, &samples, &n,
                      a->err, sizeof(a->err)) != 0) {
        log_msg("error: %s", a->err);
        notify(a, "WisprFlow", "Recording failed");
        a->st = S_IDLE;
        wf_socket_set_recording(a->sock, false);
        ui_state(a, "idle");
        ui_done(a, false, "", 0, 0, false, a->err);
        return;
    }
    long dur_ms = now_ms() - a->rec_started_ms;
    log_msg("recording stopped (%.1f s)", dur_ms / 1000.0);
    a->st = S_PROCESSING;
    wf_socket_set_recording(a->sock, false);
    ui_state(a, "processing");

    // persist the exact recording used (replay/debug)
    if (samples && n > 0) save_wav(a->last_wav, samples, n);

    if (!cancelled && n >= (size_t)(a->cfg.min_audio_ms * 16)) {
        bool no_speech = false;
        double whisper_ms = 0;
        char *text = process_audio(a, samples, n, dur_ms, &no_speech, &whisper_ms);
        if (text) {
            if (a->cfg.beep) wf_beep(1320, 60, 0.25);
            char ins_err[512] = {0};
            int rc = insert_text(&a->cfg, text, ins_err, sizeof(ins_err));
            if (rc != 0) {
                log_msg("insertion failed: %s", ins_err);
                notify(a, "WisprFlow", "Could not insert text");
                if (a->cfg.beep) wf_beep(220, 200, 0.4);
                ui_done(a, false, text, dur_ms, (long)whisper_ms, false, ins_err);
            } else {
                log_msg("inserted into focused window");
                ui_done(a, true, text, dur_ms, (long)whisper_ms, false, "");
            }
            free(text);
        } else if (no_speech) {
            log_msg("no speech detected");
            notify(a, "Nothing heard", "No speech detected — try again");
            if (a->cfg.beep) wf_beep(220, 200, 0.3);
            ui_done(a, false, "", dur_ms, 0, true, "");
        } else {
            log_msg("transcription error: %s", a->tr.err);
            notify(a, "WisprFlow", "Transcription failed");
            if (a->cfg.beep) wf_beep(220, 200, 0.4);
            ui_done(a, false, "", dur_ms, 0, false, a->tr.err);
        }
    } else {
        log_msg("recording discarded (%.0f ms)", (double)dur_ms);
        ui_done(a, false, "", dur_ms, 0, false, "recording too short");
    }
    free(samples);

    a->st = S_IDLE;
    ui_state(a, "idle");
    // user re-held the combo while we were busy
    if (hotkey_combo_active(a->hk)) start_recording(a);
}

// retranscribe the last saved recording (requested from the UI)
static void retranscribe_last(App *a) {
    fprintf(stderr, "wispr-flow: retranscribe_last: st=%d last=%s\n", a->st, a->last_wav);
    if (a->st != S_IDLE) return;
    if (access(a->last_wav, R_OK) != 0) {
        ui_done(a, false, "", 0, 0, false, "no recording saved yet");
        return;
    }
    size_t n = 0;
    char err[512];
    int16_t *wav = wf_load_wav(a->last_wav, &n, err, sizeof(err));
    if (!wav) {
        ui_done(a, false, "", 0, 0, false, err);
        return;
    }
    a->st = S_PROCESSING;
    ui_state(a, "processing");
    bool no_speech = false;
    double whisper_ms = 0;
    long dur_ms = (long)(n / 16);
    char *text = process_audio(a, wav, n, dur_ms, &no_speech, &whisper_ms);
    free(wav);
    if (text) {
        char ins_err[512] = {0};
        int rc = insert_text(&a->cfg, text, ins_err, sizeof(ins_err));
        log_msg("retranscribed: %s", text);
        ui_done(a, rc == 0, text, dur_ms, (long)whisper_ms, false, rc == 0 ? "" : ins_err);
        free(text);
    } else if (no_speech) {
        ui_done(a, false, "", dur_ms, 0, true, "");
    } else {
        ui_done(a, false, "", dur_ms, 0, false, a->tr.err);
    }
    a->st = S_IDLE;
    ui_state(a, "idle");
}

static void on_combo(void *ud, bool active) {
    App *a = ud;
    if (active) {
        if (a->st == S_IDLE) start_recording(a);
        // (if processing: the re-hold is detected after processing finishes)
    } else {
        if (a->st == S_RECORDING) finish_recording(a, false);
    }
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "WisprFlow for Linux v%s\n"
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --config PATH   use a specific config file\n"
        "  --file WAV      transcribe a wav file once and print the text, then exit\n"
        "  --version       print version and backend info\n"
        "  -h, --help      show this help\n"
        "\n"
        "Default config: ~/.config/wispr-flow/config\n",
        WF_VERSION, argv0);
}

int main(int argc, char **argv) {
    const char *config_path = NULL;
    const char *file_mode = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) config_path = argv[++i];
        else if (!strcmp(argv[i], "--file") && i + 1 < argc) file_mode = argv[++i];
        else if (!strcmp(argv[i], "--version")) {
            printf("wispr-flow %s\n", WF_VERSION);
            exit(0);
        }
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(argv[0]); exit(0); }
        else { usage(argv[0]); exit(2); }
    }

    App app;
    memset(&app, 0, sizeof(app));
    config_defaults(&app.cfg);
    if (config_load(&app.cfg, config_path, app.err, sizeof(app.err)) != 0) {
        fprintf(stderr, "wispr-flow: %s\n", app.err);
        return 1;
    }

    // single instance: flock a lock file in the state dir.
    // NB: O_CLOEXEC is essential - forked children (wl-copy, notify-send) must
    // not inherit the lock fd, or the flock would outlive this process.
    make_state_dir(&app);
    int lock_fd = open(app.lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr, "wispr-flow: another instance is already running - exiting\n");
        return 1;
    }

    Transcribe tr;
    if (tr_init(&tr, app.cfg.model) != 0) {
        fprintf(stderr, "wispr-flow: %s\n", tr.err);
        fprintf(stderr, "wispr-flow: run scripts/install.sh or set 'model' in the config\n");
        return 1;
    }

    if (file_mode) {
        size_t n = 0;
        char err[512];
        int16_t *wav = wf_load_wav(file_mode, &n, err, sizeof(err));
        if (!wav) {
            fprintf(stderr, "wispr-flow: %s\n", err);
            tr_free(&tr);
            return 1;
        }
        bool no_speech = false;
        double elapsed = 0.0;
        char *text = tr_run(&tr, &app.cfg, wav, n, &no_speech, &elapsed);
        if (text) {
            printf("%s\n", text);
            free(text);
        } else if (no_speech) {
            fprintf(stderr, "wispr-flow: no speech detected\n");
            tr_free(&tr);
            free(wav);
            return 2;
        } else {
            fprintf(stderr, "wispr-flow: %s\n", tr.err);
            tr_free(&tr);
            free(wav);
            return 1;
        }
        free(wav);
        tr_free(&tr);
        return 0;
    }

    app.tr = tr;
    app.st = S_IDLE;

    app.rec = rec_new();
    if (!app.rec) {
        fprintf(stderr, "wispr-flow: out of memory\n");
        tr_free(&app.tr);
        return 1;
    }

    app.hk = hotkey_new();
    if (!app.hk) {
        fprintf(stderr, "wispr-flow: cannot open input devices\n");
        rec_free(app.rec);
        tr_free(&app.tr);
        return 1;
    }
    hotkey_set_allow_virtual(app.hk, app.cfg.allow_virtual);
    hotkey_set_callback(app.hk, on_combo, &app);
    // NB: hotkey_set_callback may fire immediately if the combo is already held

    // UI socket
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    char sock_path[256];
    if (runtime && runtime[0]) snprintf(sock_path, sizeof(sock_path), "%s/wispr-flow.sock", runtime);
    else snprintf(sock_path, sizeof(sock_path), "/tmp/wispr-flow-%d.sock", (int)getuid());
    const char *backend = tr_backend();
    snprintf(app.status_json, sizeof(app.status_json),
             "{\"type\":\"hello\",\"version\":\"%s\",\"backend\":\"%s\","
             "\"lang\":\"%s\",\"model\":\"%s\",\"state\":\"%s\"}",
             WF_VERSION, backend, app.cfg.language, app.cfg.model,
             app.st == S_RECORDING ? "recording" : (app.st == S_PROCESSING ? "processing" : "idle"));
    app.sock = wf_socket_new(sock_path, app.status_json);
    if (!app.sock || wf_socket_start(app.sock) != 0) {
        fprintf(stderr, "wispr-flow: warning: cannot start UI socket (%s)\n", sock_path);
    } else {
        fprintf(stderr, "wispr-flow: UI socket: %s\n", sock_path);
    }
    wf_socket_set_level_source(app.sock, rec_level_src(app.rec));

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    fprintf(stderr, "wispr-flow: backend: %s\n", backend);
    log_msg("ready — hold Ctrl+Win to dictate (model %s)", app.cfg.model);

    while (!g_quit) {
        if (hotkey_poll(app.hk, 100) != 0) {
            if (!g_quit) log_msg("hotkey poll error, retrying");
        }
        if (app.st == S_RECORDING &&
            now_ms() - app.rec_started_ms > (long)app.cfg.max_audio_s * 1000) {
            log_msg("max recording duration reached");
            finish_recording(&app, false);
        }
        if (wf_socket_take_retranscribe(app.sock)) {
            retranscribe_last(&app);
        }
    }

    if (app.st == S_RECORDING) finish_recording(&app, true);
    log_msg("shutting down");
    wf_socket_free(app.sock);
    rec_free(app.rec);
    hotkey_free(app.hk);
    tr_free(&app.tr);
    return 0;
}
