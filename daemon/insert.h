#pragma once
// WisprFlow for Linux - insert text into the focused application
#include <stddef.h>
#include "config.h"

// mode "paste": put text on the clipboard and send the paste shortcut (ydotool)
// mode "type" : type the text with ydotool (ASCII only, useful in terminals)
// returns 0 on success
int insert_text(const Config *cfg, const char *text, char *errbuf, size_t errsz);
