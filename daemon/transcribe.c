#define _GNU_SOURCE
#include "transcribe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <whisper.h>

int tr_init(Transcribe *t, const char *model_path) {
    memset(t, 0, sizeof(*t));
    struct whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = true;   // GPU when built with CUDA, CPU otherwise
    t->ctx = whisper_init_from_file_with_params(model_path, cparams);
    if (!t->ctx) {
        snprintf(t->err, sizeof(t->err), "failed to load model: %s", model_path);
        return -1;
    }
    return 0;
}

void tr_free(Transcribe *t) {
    if (t->ctx) whisper_free(t->ctx);
    t->ctx = NULL;
}

const char *tr_backend(void) { return whisper_print_system_info(); }

// --- text post-processing ------------------------------------------------

static char *clean_text(const char *in, bool capitalize) {
    // collapse whitespace, strip leading/trailing spaces
    size_t len = strlen(in);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    size_t o = 0;
    bool space = false, first = true;
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = in[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            space = o > 0;
            continue;
        }
        if (space) out[o++] = ' ';
        space = false;
        if (first && capitalize && ch >= 'a' && ch <= 'z') ch -= 32;
        out[o++] = ch;
        first = false;
    }
    out[o] = 0;
    return out;
}

// --- silence trimming ------------------------------------------------------
// trim leading/trailing silence to reduce end-of-utterance hallucinations
// (whisper tends to add "you", "thank you" ... on trailing silence)

static void trim_silence(const int16_t *in, size_t n, size_t *start, size_t *end,
                         bool trim_trailing) {
    const size_t win = 480;            // 30 ms @ 16 kHz
    const int32_t thr = 64;            // ~ -58 dBFS
    const size_t pad = 4800;           // 300 ms of padding kept around speech
    *start = 0;
    *end = n;
    if (n < win * 2) return;
    // leading: cut only leading silence
    size_t i = 0;
    while (i + win <= n) {
        int64_t sum = 0;
        for (size_t j = i; j < i + win; j++) sum += (int32_t)in[j] * in[j];
        if (sum / (int64_t)win > (int64_t)thr * thr) break;
        i += win / 2;
    }
    *start = i > pad ? i - pad : 0;
    if (!trim_trailing) return; // keep the ending exactly as recorded
    // trailing: only cut when there is a LONG run of quiet (>= 600 ms) so that
    // soft word endings (plosives, fricatives, fade-outs) are never clipped
    const size_t min_silence = 600 * 16; // 600 ms @ 16 kHz
    i = n;
    while (i >= win) {
        int64_t sum = 0;
        for (size_t j = i - win; j < i; j++) sum += (int32_t)in[j] * in[j];
        if (sum / (int64_t)win > (int64_t)thr * thr) break;
        i -= win / 2;
    }
    if (n - i >= min_silence) {
        *end = i + pad < n ? i + pad : n;
    } else {
        *end = n; // not enough silence - keep everything
    }
}

char *tr_run(Transcribe *t, const Config *cfg,
             const int16_t *samples, size_t n,
             bool *no_speech, double *elapsed_ms) {
    *no_speech = false;
    *elapsed_ms = 0.0;

    if (n == 0) return NULL;

    // cut leading silence before decoding (trailing trim is optional - see config)
    size_t s0, s1;
    trim_silence(samples, n, &s0, &s1, cfg->trim_trailing);
    if (s1 <= s0) { *no_speech = true; return NULL; }
    samples += s0;
    n = s1 - s0;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    // int16 -> float in [-1, 1]
    float *f = malloc(n * sizeof(float));
    if (!f) return NULL;
    for (size_t i = 0; i < n; i++) f[i] = (float)samples[i] / 32768.0f;

    struct whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    p.n_threads = cfg->whisper_threads > 0 ? cfg->whisper_threads : 4;
    p.no_context = true;
    p.no_timestamps = true;
    p.print_special = false;
    p.print_progress = false;
    p.print_realtime = false;
    p.print_timestamps = false;
    p.suppress_blank = true;
    p.suppress_nst = true;
    p.max_initial_ts = 1.0f;
    p.length_penalty = -1.0f;
    p.temperature_inc = 0.2f; // keep the fallback: auto-detect + no fallback can false-positive "no speech"
    p.no_speech_thold = cfg->no_speech_thold;
    p.logprob_thold = cfg->logprob_thold;

    if (cfg->language[0] && strcmp(cfg->language, "auto") != 0) {
        p.language = cfg->language;
        p.detect_language = false;
    } else {
        // NB: pass the literal "auto" string, NOT NULL - with language=NULL the
        // decode can return 0 segments on some whisper.cpp builds.
        p.language = "auto";
        p.detect_language = false;
    }

    // punctuation prompt: only defaulted for a fixed English language
    const char *prompt = cfg->initial_prompt[0] ? cfg->initial_prompt : NULL;
    bool autodetect = strcmp(p.language, "auto") == 0;
    if (!prompt && !autodetect && strcmp(p.language, "en") == 0) {
        prompt = "The following is a dictation transcript. "
                 "Add punctuation: periods, commas, question marks, "
                 "exclamation points, and proper capitalization.";
    }
    p.initial_prompt = prompt;
    p.carry_initial_prompt = true;

    int rc = whisper_full(t->ctx, p, f, (int)n);
    free(f);
    if (rc != 0) {
        snprintf(t->err, sizeof(t->err), "whisper_full failed: %d", rc);
        return NULL;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    *elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;

    int nseg = whisper_full_n_segments(t->ctx);
    if (nseg == 0) {
        *no_speech = true;
        return NULL;
    }

    size_t cap = 256;
    for (int i = 0; i < nseg; i++)
        cap += strlen(whisper_full_get_segment_text(t->ctx, i)) + 2;
    char *out = malloc(cap);
    if (!out) return NULL;
    out[0] = 0;
    for (int i = 0; i < nseg; i++) {
        const char *seg = whisper_full_get_segment_text(t->ctx, i);
        // skip pure-timestamp/empty segments
        while (*seg == ' ') seg++;
        if (!*seg) continue;
        strcat(out, seg);
        if (i + 1 < nseg) strcat(out, " ");
    }
    return clean_text(out, cfg->capitalize);
}

// --- WAV loading ----------------------------------------------------------

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

int16_t *wf_load_wav(const char *path, size_t *n, char *errbuf, size_t errsz) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(errbuf, errsz, "cannot open %s", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz < 44) { fclose(f); snprintf(errbuf, errsz, "file too small to be a wav"); return NULL; }

    uint8_t *raw = malloc(fsz);
    if (!raw) { fclose(f); snprintf(errbuf, errsz, "out of memory"); return NULL; }
    if (fread(raw, 1, fsz, f) != (size_t)fsz) { free(raw); fclose(f); snprintf(errbuf, errsz, "read error"); return NULL; }
    fclose(f);

    if (memcmp(raw, "RIFF", 4) || memcmp(raw + 8, "WAVE", 4)) {
        free(raw); snprintf(errbuf, errsz, "not a RIFF/WAVE file"); return NULL;
    }

    int fmt_ok = 0;
    uint16_t fmt_code = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t *data = NULL;
    size_t data_len = 0;

    size_t off = 12;
    while (off + 8 <= (size_t)fsz) {
        uint32_t cid = rd32(raw + off);
        uint32_t csz = rd32(raw + off + 4);
        const uint8_t *body = raw + off + 8;
        if (cid == 0x20746d66) { // "fmt "
            if (csz >= 16) {
                fmt_code = rd16(body);
                channels = rd16(body + 2);
                rate = rd32(body + 4);
                bits = rd16(body + 14);
                fmt_ok = 1;
            }
        } else if (cid == 0x61746164) { // "data"
            data = body;
            data_len = csz;
            if (off + 8 + csz > (size_t)fsz) data_len = fsz - off - 8;
        }
        off += 8 + csz + (csz & 1);
    }
    if (!fmt_ok || !data || !data_len || channels == 0 || rate == 0 || bits == 0 || (bits % 8) != 0) {
        free(raw); snprintf(errbuf, errsz, "incomplete or invalid wav file"); return NULL;
    }
    if (fmt_code != 1 && fmt_code != 3) {
        free(raw); snprintf(errbuf, errsz, "unsupported wav encoding %u", fmt_code);
        return NULL;
    }

    // decode all samples as float64
    size_t nframes = data_len / (channels * (bits / 8));
    double *flt = malloc((nframes ? nframes : 1) * sizeof(double));
    if (!flt) { free(raw); snprintf(errbuf, errsz, "out of memory"); return NULL; }
    for (size_t i = 0; i < nframes; i++) {
        double sum = 0;
        for (uint16_t c = 0; c < channels; c++) {
            size_t b = (i * channels + c) * (bits / 8);
            double v = 0;
            if (bits == 32 && fmt_code == 3) {
                float tmp; memcpy(&tmp, (const uint8_t *)data + b, 4); v = tmp;
            } else if (bits == 16) {
                v = (int16_t)rd16((const uint8_t *)data + b) / 32768.0;
            } else if (bits == 8) {
                v = ((int8_t)(((const uint8_t *)data)[b] - 128)) / 128.0;
            } else if (bits == 24) {
                int32_t s = ((const uint8_t *)data)[b] | ((const uint8_t *)data)[b + 1] << 8 | ((const uint8_t *)data)[b + 2] << 16;
                if (s & 0x800000) s |= ~0xFFFFFF;
                v = s / 8388608.0;
            } else if (bits == 32) {
                int32_t s; memcpy(&s, (const uint8_t *)data + b, 4); v = s / 2147483648.0;
            }
            sum += v;
        }
        flt[i] = sum / channels;
    }

    // resample to 16000 Hz (linear)
    const uint32_t out_rate = 16000;
    size_t nout = (size_t)((double)nframes * out_rate / rate);
    int16_t *out = malloc((nout ? nout : 1) * sizeof(int16_t));
    if (!out) { free(flt); free(raw); snprintf(errbuf, errsz, "out of memory"); return NULL; }
    for (size_t i = 0; i < nout; i++) {
        double pos = (double)i * rate / out_rate;
        size_t i0 = (size_t)pos;
        size_t i1 = i0 + 1 < nframes ? i0 + 1 : i0;
        double frac = pos - i0;
        double v = flt[i0] * (1 - frac) + flt[i1] * frac;
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        out[i] = (int16_t)(v * 32767.0);
    }
    free(flt);
    free(raw);
    *n = nout;
    return out;
}
