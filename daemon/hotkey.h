#pragma once
// WisprFlow for Linux - global hotkey via evdev (compositor-independent)
#include <stdbool.h>
#include <stdio.h>

typedef void (*wf_hotkey_cb)(void *ud, bool active); // combo pressed / released

typedef struct Hotkey Hotkey;

Hotkey *hotkey_new(void);
void    hotkey_free(Hotkey *h);
void    hotkey_set_callback(Hotkey *h, wf_hotkey_cb cb, void *ud);
void    hotkey_set_allow_virtual(Hotkey *h, bool allow);
// returns 0 on ok, -1 on fatal error
int     hotkey_poll(Hotkey *h, int timeout_ms);
bool    hotkey_combo_active(const Hotkey *h);
int     hotkey_device_count(const Hotkey *h);
const char *hotkey_device_name(const Hotkey *h, int i);
