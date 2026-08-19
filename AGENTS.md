# AGENTS.md

Guidance for AI coding agents (and humans) working on this repository.
Read this file before making changes.

## What this is

WisprFlow for Linux: a WisprFlow-style dictation daemon. Hold **Ctrl+Win**,
speak, release — the transcript is inserted into the focused application.
Built on whisper.cpp with NVIDIA CUDA support. Target desktop: KDE Plasma on
Wayland (X11 also works).

Pipeline: `evdev hotkey → PulseAudio capture → whisper.cpp (CUDA) → clipboard
(wl-copy) → paste via uinput virtual keyboard`.

## Repository layout

```
daemon/        C daemon (C11, GNU style) + wispr-inject test tool
ui/            Qt6 C++ UI (tray indicator + OSD feedback + settings/debug windows)
scripts/       setup.sh (root) / build.sh / install.sh
whisper.cpp/   whisper.cpp submodule checkout (do not edit)
models/        downloaded ggml models (gitignored)
test/          sample audio (jfk.wav)
bin/           built daemon binary (gitignored)
build/         cmake build dir (gitignored)
```

## Architecture

### Daemon (`daemon/`)

Single-threaded event loop (main.c) + one capture thread (audio.c) + one
socket thread (wf_socket.c).

- `main.c` — state machine `S_IDLE → S_RECORDING → S_PROCESSING`; config;
  notifications; retranscribe; CLI (`--file`, `--version`).
- `hotkey.c` — reads raw evdev (`/dev/input/event*`), **ignores virtual
  keyboards by device name** ("ydotool", "uinput", "wtype", "virtual", "keyd",
  "xremap", …). `wispr-inject`'s device name ("wispr-inject") is deliberately
  allowed. Rescans every 4 s; initializes modifier state from `EVIOCGKEY` so
  a combo held before startup is not missed. Keys are never consumed.
- `audio.c` — PulseAudio capture loop, RMS level for the UI, stop-tail.
- `transcribe.c` — whisper.cpp glue.
- `insert.c` — `wl-copy`/`wl-paste` + uinput key injection (ydotool fallback).
- `wf_socket.c` — Unix socket at `$XDG_RUNTIME_DIR/wispr-flow.sock`;
  newline-delimited JSON; max 8 clients. Do not call `wf_socket_broadcast`
  while holding the broadcast lock (deadlock).
- `inject_main.c` — `wispr-inject` CLI: simulate hotkey/keys headlessly.
- `config.h` — `WF_VERSION` (bump for releases).

### UI (`ui/`)

Qt6 (C++17, AUTOMOC, Widgets+Network+DBus). **No floating windows on
Plasma**: state lives in the system tray, transient feedback uses Plasma's
native OSD overlay.

- `main.cpp` — wiring: tray is primary; the pill is used only on desktops
  without a tray (`QSystemTrayIcon::isSystemTrayAvailable()`).
- `tray.cpp` — StatusNotifierItem icon per state (idle mic / pulsing red /
  spinner / offline slash), tooltip with backend + last transcript.
- `osd.cpp` — Plasma 6 OSD via DBus
  `org.kde.plasmashell /org/kde/osdService org.kde.osdService.showText`
  (the volume/brightness overlay). Falls back to `notify-send` on other
  desktops. The old Plasma 5 path `/PlasmaOSD` no longer exists.
- `wfclient.cpp` — QLocalSocket client; **retries every 1.5 s on both
  `errorOccurred` and `disconnected`**, re-syncs via `hello` on reconnect.
- `debugwin.cpp` / `settingswin.cpp` — on-demand windows (fine as windows).

### Socket protocol

Client → daemon: `{"cmd":"hello"}`, `{"cmd":"retranscribe"}` (one per line).
Daemon → client: `{"type":"hello"|"state"|"level"|"done", ...}`.
States: `idle | recording | processing` (daemon), `offline` is UI-only.

## Build & install

```sh
sudo ./scripts/setup.sh        # system deps (cuda, wl-clipboard, uinput udev)
./scripts/build.sh             # builds daemon (make -C daemon) + UI (cmake)
./scripts/install.sh           # installs to ~/.local/bin, model, autostart
```

Incremental builds:

```sh
make -C daemon                          # → bin/wispr-flow
cmake -B build/ui -S ui && cmake --build build/ui -j"$(nproc)"
# → build/ui/wispr-flow-ui; install to ~/.local/bin when testing:
install -m755 build/ui/wispr-flow-ui ~/.local/bin/wispr-flow-ui
```

Running: the daemon runs as a **systemd user service**
(`scripts/wispr-flow.service`, installed to `~/.config/systemd/user/`,
enabled via `WantedBy=graphical-session.target`): it starts with the
graphical session, `Restart=on-failure` (RestartSec=2), and logs to the
journal (`journalctl --user -u wispr-flow -f`). The UI (KDE autostart
entry) connects to the daemon socket; if no systemd user session is
available the UI falls back to spawning the daemon itself (800 ms check)
— the daemon's flock guard (`daemon.lock`) makes a duplicate instance
exit harmlessly. The daemon can also run standalone.

## Testing

```sh
bin/wispr-inject ctrl+win hold   # press and hold (then: kill -INT <pid>)
bin/wispr-inject type "hello"    # type ASCII text
# set source = monitor in ~/.config/wispr-flow/config, then:
paplay test/jfk.wav              # while holding the hotkey
~/.local/bin/wispr-flow --file test/jfk.wav   # transcribe a file directly
```

To verify the UI end-to-end without dictating: query the tray item's ToolTip
via DBus (`org.kde.StatusNotifierWatcher` → `RegisteredStatusNotifierItems`),
or check `ss -xanp | grep wispr-flow.sock` for the ESTAB connection.

## Gotchas (learned the hard way)

- **Qt 6.11+ on Arch silently drops Qt logging (`qWarning`/`qDebug`) when
  stderr is not a TTY** (scripts, autostart, nohup). If your debug prints
  "vanish", run with `QT_FORCE_STDERR_LOGGING=1` — they were never a no-op.
  `fprintf(stderr, ...)` is unaffected.
- **`kill %1` job specs do not work in non-interactive shells** — the kill
  fails silently and `wispr-inject hold` keeps the keys held, which makes the
  daemon record for minutes. Always kill by explicit PID in scripts.
- The UI **does not restart a daemon that dies mid-session** — with the
  systemd service, `Restart=on-failure` handles crashes; the UI shows
  "offline" and reconnects when the daemon returns. Without systemd, use
  tray menu → "Restart daemon" or pkill + relaunch.
- **Daemon logs**: as a systemd service they go to the journal
  (`journalctl --user -u wispr-flow -f`). When the UI spawns the daemon
  (fallback), logs inherit the UI's stderr — which in an autostart session
  is typically /dev/null, i.e. lost. Debug via the journal or run the
  daemon in a terminal.
- **`After=graphical-session.target` in the service unit is deliberate**:
  the daemon needs `WAYLAND_DISPLAY` etc., which Plasma imports into the
  user manager only when the graphical session starts.
- Restarting the daemon is required for config changes to take effect.
- KWin on Wayland ignores Qt's always-on-top flag; the old pill needed a
  hand-written kwinrulesrc rule (removed — do not reintroduce).
- `ui/wfclient.cpp` must connect `QLocalSocket::errorOccurred` — without it
  the UI sits on "daemon offline" forever (fixed in 67e0242; keep it that way).
- **The daemon ignores SIGPIPE** (`signal(SIGPIPE, SIG_IGN)` in `main.c`):
  without it, a write to a dead UI socket or a crashed `wl-copy` pipe would
  terminate the daemon mid-transcription. Do not remove this.
- **The UI and daemon must agree on the socket fallback path** when
  `XDG_RUNTIME_DIR` is unset: both use `/tmp/wispr-flow-<uid>.sock`
  (via `getuid()`). The UI previously used `applicationPid()`, which never
  matched the daemon.
- **`wf_socket.c` Client slots reuse a single `pthread_mutex_t`** initialized
  once in `wf_socket_new`. Do not `memset` a `Client` struct on accept — only
  reset `fd`/`len`/`buf`, or the mutex is corrupted.
- **`hotkey.c rescan` rebuilds modifier state from scratch** (starts `false`,
  ORs in `EVIOCGKEY` from open devices). Do not seed it from the previous
  state — a keyboard unplugged while a modifier is held never sends key-up,
  so the combo would stick forever.
- **`ev_key`/`uk_tap`/`uk_type_text` propagate `write()` errors**. Do not
  revert them to ignoring returns — silently-swallowed write failures make
  paste appear to succeed when no key event reached the kernel.
- `models/`, `bin/`, `build/`, `*.o` are gitignored; the model is symlinked
  into `~/.local/share/wispr-flow/`.
- Recordings are saved to `~/.local/state/wispr-flow/last.wav` (overwritten
  on every dictation — "Replay" in the debug window plays it).

## Maintenance rule (IMPORTANT)

**Keep this file updated.** Whenever you:

- change the architecture, build process, or install layout,
- add/remove a tool or script,
- discover a new gotcha, environment quirk, or non-obvious behavior,
- change the socket protocol, config keys, or state names,

you MUST update AGENTS.md (and the README if user-facing) in the same commit.
A stale AGENTS.md is a bug. Also add a CHANGELOG.md entry for any
user-visible change (Added/Changed/Fixed/Removed).
