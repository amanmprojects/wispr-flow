#pragma once
// WisprFlow for Linux - configuration
#include <stdbool.h>
#include <stddef.h>

#define WF_VERSION "0.1.0"

typedef struct Config {
    char model[1024];          // path to ggml whisper model
    char language[32];         // "auto" or e.g. "en", "de", "zh"
    char source[256];          // pulse source name; "" = default; "monitor" = default monitor
    char insert_mode[16];      // "paste" | "type"
    char paste_combo[64];      // e.g. "ctrl+v"
    char initial_prompt[2048]; // "" = auto default
    int  whisper_threads;      // decoder threads
    float no_speech_thold;     // drop recording if prob no-speech above this
    float logprob_thold;       // drop recording if avg logprob below this
    int  min_audio_ms;         // recordings shorter than this are discarded
    int  max_audio_s;          // hard recording limit
    int  stop_tail_ms;         // keep capturing this long after hotkey release
    bool trim_trailing;        // cut long trailing silence (may clip soft endings; default off)
    bool notify;               // send desktop notifications
    bool beep;                 // play feedback beeps
    bool capitalize;           // uppercase the first letter of the output
    bool restore_clipboard;    // restore previous clipboard content after paste
    bool allow_virtual;        // also watch virtual keyboards (testing only)
} Config;

void config_defaults(Config *c);
// path == NULL -> $XDG_CONFIG_HOME/wispr-flow/config (missing file is not an error)
int  config_load(Config *c, const char *path, char *errbuf, size_t errsz);
