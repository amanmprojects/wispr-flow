#pragma once
// WisprFlow for Linux - tiny process helpers (fork/exec, no shell)
#include <stddef.h>

// run argv, wait, return exit code
int wf_run(const char **argv);
// run argv, capture stdout (malloc'd, NUL-terminated); returns exit code
int wf_run_capture(const char **argv, char **out, size_t *outlen);
// run argv, feed input on stdin; returns exit code
int wf_run_pipe(const char **argv, const char *in, size_t inlen);
