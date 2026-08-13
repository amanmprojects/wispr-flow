# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
(version lives in `daemon/config.h`, `WF_VERSION`).

## [Unreleased]

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
