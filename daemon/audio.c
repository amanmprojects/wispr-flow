#define _GNU_SOURCE
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#define SAMPLE_RATE 16000
#define CHUNK_SAMPLES 320 // 20 ms

struct Rec {
    pthread_t  tid;
    volatile int stop_requested;
    volatile long stop_deadline_ms; // monotonic deadline once stop is requested
    pa_simple *pa;
    char      *source;
    int        err;
    int16_t   *data;
    size_t     n, cap;
    volatile int last_rms;         // 0..1000, for the UI waveform
};

static int level_from_rms(int32_t rms) {
    // rms of int16 samples; map ~0..10000 to 0..1000
    int v = rms * 25 / 256;
    if (v < 0) v = 0;
    if (v > 1000) v = 1000;
    return v;
}

int rec_level(const Rec *r) { return r ? r->last_rms : 0; }
volatile int *rec_level_src(Rec *r) { return r ? (volatile int *)&r->last_rms : NULL; }

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

Rec *rec_new(void) {
    Rec *r = calloc(1, sizeof(*r));
    return r;
}

void rec_free(Rec *r) {
    if (!r) return;
    if (r->pa) pa_simple_free(r->pa);
    free(r->data);
    free(r->source);
    free(r);
}

static int append(Rec *r, const int16_t *chunk, size_t n) {
    if (r->n + n > r->cap) {
        size_t ncap = r->cap ? r->cap * 2 : 65536;
        while (ncap < r->n + n) ncap *= 2;
        int16_t *nd = realloc(r->data, ncap * sizeof(int16_t));
        if (!nd) return -1;
        r->data = nd;
        r->cap = ncap;
    }
    memcpy(r->data + r->n, chunk, n * sizeof(int16_t));
    r->n += n;
    return 0;
}

static void *capture_thread(void *arg) {
    Rec *r = arg;
    int16_t chunk[CHUNK_SAMPLES];
    for (;;) {
        // keep reading while recording, plus a short tail after stop is requested
        if (r->stop_requested && now_ms() >= r->stop_deadline_ms) break;
        int err = 0;
        if (pa_simple_read(r->pa, chunk, sizeof(chunk), &err) < 0) {
            r->err = err;
            break;
        }
        if (append(r, chunk, CHUNK_SAMPLES) != 0) {
            r->err = -1;
            break;
        }
        // running RMS for the waveform
        int64_t sum = 0;
        for (int i = 0; i < CHUNK_SAMPLES; i++) sum += (int32_t)chunk[i] * chunk[i];
        r->last_rms = level_from_rms((int32_t)(sum / CHUNK_SAMPLES));
    }
    r->last_rms = 0;
    return NULL;
}

static char *resolve_source(const char *src) {
    if (!src || !src[0]) return strdup("");
    if (strcmp(src, "monitor") == 0) {
        // resolve the default sink monitor via pactl
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "pactl get-default-sink 2>/dev/null");
        FILE *f = popen(cmd, "r");
        if (!f) return strdup("monitor");
        char sink[128] = {0};
        if (fgets(sink, sizeof(sink), f) != NULL) {
            char *nl = strchr(sink, '\n');
            if (nl) *nl = 0;
        }
        pclose(f);
        if (!sink[0] || strcmp(sink, "auto_null") == 0) return strdup("monitor");
        char *out = malloc(strlen(sink) + 16);
        if (!out) return strdup("monitor");
        sprintf(out, "%s.monitor", sink);
        return out;
    }
    return strdup(src);
}

int rec_start(Rec *r, const char *source, char *errbuf, size_t errsz) {
    if (r->stop_requested && r->pa) { snprintf(errbuf, errsz, "already recording"); return -1; }
    free(r->source);
    r->source = resolve_source(source);
    if (!r->source) { snprintf(errbuf, errsz, "out of memory"); return -1; }

    pa_sample_spec ss = { .format = PA_SAMPLE_S16LE, .rate = SAMPLE_RATE, .channels = 1 };
    int err = 0;
    const char *src = r->source[0] ? r->source : NULL;
    r->pa = pa_simple_new(NULL, "wispr-flow", PA_STREAM_RECORD, src,
                          "dictation", &ss, NULL, NULL, &err);
    if (!r->pa) {
        snprintf(errbuf, errsz, "cannot open audio source '%s': %s",
                 src ? src : "(default)", pa_strerror(err));
        return -1;
    }

    r->n = 0;
    r->err = 0;
    r->stop_requested = 0;
    r->stop_deadline_ms = 0;
    if (pthread_create(&r->tid, NULL, capture_thread, r) != 0) {
        r->stop_requested = 1;
        pa_simple_free(r->pa);
        r->pa = NULL;
        snprintf(errbuf, errsz, "cannot start capture thread");
        return -1;
    }
    return 0;
}

int rec_stop(Rec *r, int16_t **samples, size_t *n, char *errbuf, size_t errsz) {
    return rec_stop_tail(r, 0, samples, n, errbuf, errsz);
}

// stop capturing, but keep reading for an extra tail_ms so the last syllable
// spoken just before the hotkey release is never cut off.
int rec_stop_tail(Rec *r, long tail_ms, int16_t **samples, size_t *n, char *errbuf, size_t errsz) {
    if (r->stop_requested && !r->pa) { snprintf(errbuf, errsz, "not recording"); return -1; }
    if (!r->stop_requested) {
        r->stop_requested = 1;
        r->stop_deadline_ms = now_ms() + tail_ms;
        pthread_join(r->tid, NULL);
    }
    if (r->pa) {
        pa_simple_free(r->pa);
        r->pa = NULL;
    }
    if (r->err) {
        snprintf(errbuf, errsz, "capture error: %s", pa_strerror(r->err));
        free(r->data);
        r->data = NULL;
        r->n = 0;
        return -1;
    }
    *samples = r->data;
    *n = r->n;
    r->data = NULL;
    r->n = r->cap = 0;
    return 0;
}

int wf_beep(double freq_hz, double dur_ms, double vol) {
    int rate = 22050;
    pa_sample_spec ss = { .format = PA_SAMPLE_S16LE, .rate = rate, .channels = 1 };
    int err = 0;
    pa_simple *s = pa_simple_new(NULL, "wispr-flow", PA_STREAM_PLAYBACK, NULL,
                                 "beep", &ss, NULL, NULL, &err);
    if (!s) return -1;
    size_t n = (size_t)(rate * dur_ms / 1000.0);
    int16_t *buf = malloc(n * sizeof(int16_t));
    if (!buf) { pa_simple_free(s); return -1; }
    const double pi = acos(-1.0);
    for (size_t i = 0; i < n; i++) {
        double t = (double)i / rate;
        // short fade in/out to avoid clicks
        double env = 1.0;
        double fadelen = 0.005;
        if (t < fadelen) env = t / fadelen;
        if (t > dur_ms / 1000.0 - fadelen) env = (dur_ms / 1000.0 - t) / fadelen;
        if (env < 0) env = 0;
        buf[i] = (int16_t)(vol * 32767.0 * env * sin(2 * pi * freq_hz * t));
    }
    pa_simple_write(s, buf, n * sizeof(int16_t), &err);
    pa_simple_drain(s, &err);
    free(buf);
    pa_simple_free(s);
    return 0;
}
