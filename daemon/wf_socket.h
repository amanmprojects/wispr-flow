#pragma once
// WisprFlow for Linux - small JSON-over-unix-socket event server
// Events (newline-delimited JSON) let the UI show state, live audio levels,
// transcripts and the path of the recording that was used.

#include <stdbool.h>

typedef struct WfSocket WfSocket;

// status_json must stay valid for the lifetime of the socket (static string)
WfSocket *wf_socket_new(const char *sock_path, const char *status_json);
void wf_socket_free(WfSocket *s);
// start listener thread; returns 0 on success
int wf_socket_start(WfSocket *s);
// broadcast one JSON line to all clients (thread-safe)
void wf_socket_broadcast(WfSocket *s, const char *json_line);
// tell the socket thread whether recording is active (for level events)
void wf_socket_set_recording(WfSocket *s, bool recording);
// pointer to an int (0..1000) the socket thread polls for live levels;
// may be NULL to disable level events
void wf_socket_set_level_source(WfSocket *s, const volatile int *level);
// set when a client requested a retranscription of the last recording
bool wf_socket_take_retranscribe(WfSocket *s);
