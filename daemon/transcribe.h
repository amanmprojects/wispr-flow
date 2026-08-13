#pragma once
// WisprFlow for Linux - whisper.cpp transcription
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"

typedef struct Transcribe {
    struct whisper_context *ctx;
    char err[512];
} Transcribe;

int  tr_init(Transcribe *t, const char *model_path);
void tr_free(Transcribe *t);
// returns static string describing the build backend (CUDA/CPU/...)
const char *tr_backend(void);

// transcribe mono 16 kHz s16 samples; returns malloc'd UTF-8 text or NULL.
// *no_speech is set when the model found no speech. *elapsed_ms = wall time.
char *tr_run(Transcribe *t, const Config *cfg,
             const int16_t *samples, size_t n,
             bool *no_speech, double *elapsed_ms);

// load a .wav (PCM or float, any rate/channels) as mono 16 kHz s16.
// returns malloc'd buffer, *n = number of samples.
int16_t *wf_load_wav(const char *path, size_t *n, char *errbuf, size_t errsz);
