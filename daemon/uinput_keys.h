#pragma once
// WisprFlow for Linux - kernel-level key injection via /dev/uinput
// (works on Wayland and X11, no external daemon required)

// open a virtual keyboard; returns fd or -1 (errno set)
int uk_open(const char *name);
void uk_close(int fd);

// press/release a sequence: keys in down[] are pressed (in order), then
// keys in up[] are released (in order). returns 0 on success.
int uk_tap(int fd, const int *down, int n_down, const int *up, int n_up);

// type ASCII text (letters/digits/punctuation via XKB-ish mapping);
// returns 0 on success, -2 when the text contains untypable characters.
int uk_type_text(int fd, const char *text);
