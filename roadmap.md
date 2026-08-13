# Roadmap

Lower-priority / deferred work, tracked here so it doesn't get lost. The
high-priority ops items (systemd user service with journald logging, crash
restart) are done — see `scripts/wispr-flow.service` and `CHANGELOG.md`.

Roughly in priority order:

## 1. Automated tests
The daemon has zero tests, and regressions fail silently (a missing Qt
signal connection took an hour of debugging to find — a test would have
caught it instantly). Targets, cheapest first:
- socket protocol round-trip (hello/state/done/level/retranscribe)
- config parser (defaults, overrides, bad values)
- UI `WfClient` reconnect logic (connect, disconnect, retry)
- hotkey state machine (combo transitions, multi-device, device removal)

## 2. Hotkey state machine hardening
Observed edge cases during testing: the 4 s rescan re-derives combo state
from `EVIOCGKEY`, and destroying a virtual device synthesizes key-up events;
under multiple simultaneous virtual keyboards the daemon recorded/restarted
in unexpected ways. Add a debounce/grace period and re-evaluate the
multi-device combo semantics (a held combo on *any* device should keep
recording; releases should only count per the device that started it).

## 3. Configurable hotkey combo
Ctrl+Win is hardcoded in `hotkey.c` (modifiers are fixed, the evdev layer
already handles arbitrary key codes). Config keys like `hotkey = ctrl+win`
or `hotkey = capslock hold`.

## 4. Live transcript preview while recording
The flagship WisprFlow feature: text appears while you speak, not after.
Options: partial whisper decoding of the rolling buffer (throttled, e.g.
every 2-3 s), a small streaming model, or an external whisper-stream style
pipeline. Display via the Plasma OSD (repeated `showText` calls) or a small
shell-owned overlay. Expensive — the north star, not the next step.

## 5. KDE Klipper DBus fallback for the clipboard
`wl-copy`/`wl-paste` requires wl-clipboard; Klipper has a DBus API that
could replace it (and remove the dependency). Note `restore_clipboard`
already handles clipboard preservation.

## 6. Auto-language punctuation prompt
Per-language punctuation prompt (e.g. German transcriptions get worse
punctuation than English) — whisper.cpp initial prompt per `language`.

## 7. Multiple saved recordings
`last.wav` is overwritten every dictation; keep a small ring of recent
recordings (or timestamped files) so "Replay" isn't lossy.
