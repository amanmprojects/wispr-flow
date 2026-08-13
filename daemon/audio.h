#pragma once
// WisprFlow for Linux - audio capture (PulseAudio) + feedback beeps
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Rec Rec;

Rec *rec_new(void);
void rec_free(Rec *r);
// opens the given pulse source ("" = default, "monitor" = default sink monitor)
// and starts a capture thread; 16 kHz mono s16.
int  rec_start(Rec *r, const char *source, char *errbuf, size_t errsz);
// stop capturing, but keep reading for an extra tail_ms so the last syllable
// spoken just before the hotkey release is never cut off.
// *samples/*n receive the audio (malloc'd, caller frees). returns 0 on success.
int  rec_stop_tail(Rec *r, long tail_ms, int16_t **samples, size_t *n, char *errbuf, size_t errsz);
int  rec_stop(Rec *r, int16_t **samples, size_t *n, char *errbuf, size_t errsz);
// current input level, 0..1000 (for the UI waveform)
int  rec_level(const Rec *r);
// pointer to the live level value (for the UI socket thread)
volatile int *rec_level_src(Rec *r);

// short sine beep to the default sink
int  wf_beep(double freq_hz, double dur_ms, double vol);
