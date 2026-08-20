# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
(version lives in `daemon/config.h`, `WF_VERSION`).

## [Unreleased]

### Fixed

- **SIGPIPE could kill the daemon** mid-transcription: a write to a
  disconnected UI socket or a crashed `wl-copy` pipe terminated the process.
  The daemon now ignores SIGPIPE (`signal(SIGPIPE, SIG_IGN)`).
- **Division by zero (SIGFPE crash)** in `wf_load_wav` on a malformed WAV
  with `bits == 0`; the parser now rejects `bits == 0` and non-byte-aligned
  bit depths.
- **Partial writes to UI clients** truncated large JSON events (up to 16 KB
  transcripts). `send_all` now loops until the full line is written.
- **Zombie processes** from `notify()`: the forked `notify-send` child was
  never reaped, accumulating zombies over a long session.
- **Config file corruption** in Settings → Save: `QFile::resize(0)` was not
  followed by `seek(0)`, so the rewritten config landed at the old EOF,
  producing a sparse file full of NUL bytes.
- **Paste silently failed** when uinput `write()` errors were ignored:
  `ev_key`/`uk_tap`/`uk_type_text` always returned 0 even when key events
  never reached the kernel. Write failures now propagate as errors so the
  ydotool fallback is tried.
- **JSON injection via `path`** in `done` events: the recording path was
  not JSON-escaped, breaking the protocol if `XDG_STATE_HOME` contains a
  `"` or `\`.
- **Duplicate concurrent recording** possible because the "already
  recording" guard checked `stop_requested && pa` (always false while
  recording); now checks `pa` alone.
- **`pthread_mutex_t` corruption on UI client reconnect**: `memset`-zeroing
  a reused `Client` slot destroyed its already-initialized mutex. Only the
  per-connection fields (`fd`, `len`, `buf`) are reset now.
- **Stuck Ctrl+Win combo after keyboard unplug**: `rescan` started from the
  old modifier state and only ever OR-ed `true`, so a modifier held when a
  keyboard was unplugged stuck forever. Modifier state is now rebuilt from
  scratch on each rescan.
- **UI ↔ daemon socket path mismatch** when `XDG_RUNTIME_DIR` is unset: the
  daemon used `getuid()` but the UI used `applicationPid()`, so the two never
  connected. The UI now uses `getuid()` to match.
- **`wispr-inject` mapped the `0` key incorrectly**: `2 + ('0'-'1')` = `1`
  instead of `KEY_0` (11).
- **Clipboard restore clobbered the caller's error buffer** on a successful
  paste: restoring the old clipboard content overwrote `errbuf` even though
  `insert_paste` returned success.
- **Socket client disconnected on overlong command line**: a read into a full
  buffer (no newline within `MAX_LINE`) made `read(fd, …, 0)` return 0,
  mistaken for EOF. The buffer is now reset instead of disconnecting.
- **Transcription leak**: `tr_run` never freed the raw `out` buffer after `clean_text` (every dictation leaked ~256 B+text). Now frees `out` and sets `t->err` on malloc failure.
- **`pthread_mutex_t` move UB on client disconnect**: `memmove` of `Client` copied the mutex (POSIX UB, deadlock). Now shifts `fd`/`len`/`buf` only, mutex stays in place.
- **Deadlock via `broadcast_lock` held across `read()`**: socket thread now snapshots fds, reads without lock, re-locks to update buffers; shared flags protected by lock and re-queued retranscribe.
- **Retranscribe lost when busy**: request during `PROCESSING` was consumed and dropped. Now re-queued via `wf_socket_requeue_retranscribe` and retried when idle.
- **JSON escaping incomplete**: `json_escape` now handles `\b \f \n \r \t` and `\u00XX` for `<0x20`; buffers enlarged (text 16k, line 32k) with truncation warnings. `status_json` rebuilt on every state transition.
- **`level_from_rms` overflow and `append` cap overflow**: use `int64` and `SIZE_MAX` guard.
- **`hotkey` Ctrl/Meta handling too strict + stale poll**: accepts any left/right Ctrl/Meta, forces rescan on `POLLERR|HUP|NVAL`.
- **`config` HOME handling**: `expand_tilde` and `load` fall back to `/tmp` when `HOME` unset (consistent with `make_state_dir`); comment-strip respects quoted `#`.
- **Clipboard X11 fallback**: `wl-copy`/`wl-paste` tried first, then `xclip`/`xsel`; paste and restore both handle Wayland and X11.
- **UI JSON whitespace brittleness**: `jsonGet*` now skips whitespace around `:` and handles `\b \f \uXXXX`.
- **UI offline command loss**: `WfClient` now queues commands while disconnected and flushes on connect; hello has 3 s timeout warning.
- **OSD fallback too narrow**: now checks `sessionBus.isConnected()`, timeout, any non-Reply, tries `notify-send` then `QSystemTrayIcon::showMessage`.
- **Tray single-sample stale**: `isSystemTrayAvailable()` now polled every 2 s; pill ↔ tray switches dynamically.
- **Tray/pill HiDPI blur and fixed size**: HiDPI `devicePixelRatio` scaling, pill uses `sizeHint()` (not 184×42), stops animation when idle/offline.
- **Pill not keyboard-accessible**: now `StrongFocus`, Enter/Space/Shift+F10, AccessibleName/Description, `QAccessible::StateChanged`.
- **Settings corruption**: uses `QSaveFile` atomic write, buddy-linked labels, AccessibleDescriptions.
- **Debug replay paplay-only**: now `paplay` → `pw-play` → `aplay` fallback; retranscribe button disabled when offline.
- **Build: hardcoded CUDA arch 89-real**: now auto-detects via `nvidia-smi` or builds multi-arch 75/80/86/89/90 with PTX fallback; submodule check added.
- **Install: Arch-only**: `setup.sh` now distro-aware (pacman/apt/dnf/zypper), correct package names, creates `/etc/udev/rules.d/60-uinput.rules`, adds `input`+`uinput` groups, handles multi CUDA homes (`/opt/cuda` `/usr/local/cuda` `/usr/lib/cuda`), `install.sh` respects `XDG_*_HOME`, quoted `Exec`, `TryExec`, disk-space check, `curl --retry 3`/`wget`/`hf` fallbacks with size verify, `Makefile`/`CMakeLists` use `PREFIX/DESTDIR` and `install()` targets.

### Added

- `systemd --user` service for the daemon (`scripts/wispr-flow.service`):
  starts with the graphical session, restarts on crash (`Restart=on-failure`),
  and logs to the journal (`journalctl --user -u wispr-flow -f`) instead of
  losing output to /dev/null. `install.sh` installs and enables it; the
  UI's own daemon-spawn fallback remains for sessions without systemd.
- `roadmap.md` tracking deferred work (automated tests, hotkey state-machine
  hardening, configurable hotkey, live transcript preview, …).

## [0.1.0] - 2026-08-14

First tracked release. History before this date was not changelogged.

### Added

- System tray indicator (StatusNotifierItem) replacing the floating pill as
  the primary state display: gray mic = idle, pulsing red = recording,
  purple spinner = transcribing, slashed mic = daemon offline. Tooltip shows
  backend and last transcript; left-click opens the last-dictation window.
- Transient feedback through Plasma's native OSD overlay (the same popup the
  system uses for volume/brightness): "Listening…" on hold, "Transcribing…"
  while the model runs, then the inserted text or the error. Falls back to
  `notify-send` on non-Plasma desktops.
- Automatic daemon reconnection: the UI retries the socket every 1.5 s after
  a failed connect or a disconnect and re-syncs state via `hello` once the
  daemon is back.
- `AGENTS.md` with build/test/architecture guidance and a maintenance rule.

### Changed

- The floating pill window (and its automatic KWin window-rule hack) is now
  only a fallback for desktops without a system tray.
- The daemon no longer sends a "Listening…" desktop notification when
  recording starts — the OSD overlay covers it (error notifications kept).

### Fixed

- The UI could sit on "daemon offline" forever after the daemon restarted:
  `WfClient` never connected `QLocalSocket::errorOccurred`, so failed
  connect attempts were never retried.
- The indicator now flips to "offline" immediately when the daemon dies,
  instead of keeping the last stale state.
