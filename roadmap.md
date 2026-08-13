# Roadmap

Deferred work, tracked here so it doesn't get lost. Done so far: tray + OSD
UI, daemon reconnect logic, systemd user service with journald logging and
crash restart (see `CHANGELOG.md`).

In priority order:

## 1. One-command install
Goal: `curl -sSf https://… | sh` (or `./scripts/install.sh` on a clone) that
detects everything and installs without manual steps:

- detect OS/distro (Arch first; Debian/Ubuntu/Fedora later), package
  manager, and whether a systemd user session exists
- detect GPU: `nvidia-smi`/`lspci` → NVIDIA present? compute capability?
  CUDA toolkit available? else CPU-only build (whisper.cpp falls back to
  AVX2/OpenMP — still fast enough for the small models)
- check requirements: wl-clipboard, PulseAudio/PipeWire mic (list sources),
  uinput device + group/udev perms, model not yet downloaded
- install deps, build daemon + UI, download a model (auto-picked, see #2),
  write config, install + enable the systemd unit — with clear per-step
  output and a final "hold Ctrl+Win and speak" smoke test

## 2. Model selection
`model` in the config already accepts any whisper.cpp ggml model — what's
missing is convenience:

- `wispr-flow --model list` (available sizes: tiny/base/small/medium/
  large-v3-turbo with size + expected quality) and `--model download <name>`
  (reuse the hf-download logic from install.sh)
- auto-default by hardware: GPU with ≥6 GB VRAM → large-v3-turbo; weaker
  GPU → medium/small; CPU-only → base/small
- settings window gets a model dropdown (restart daemon to apply)

## 3. GPU power / on-demand model loading
Measured 2026-08-14 (RTX 4060 Laptop, nvidia driver, KDE Wayland):
- daemon running (model resident, ~900 MiB VRAM): **3.94 W**, P8 idle
- daemon stopped (GPU empty): **3.85 W**, P8 idle
- the dGPU never runtime-suspends on this machine anyway — plasmashell
  itself holds `/dev/nvidia0` open, so the ~3.9 W idle floor is drawn
  regardless of wispr-flow

Conclusion: the resident model costs ~0.1 W — **not worth changing on this
machine**. On laptops where the dGPU *can* enter runtime D3 (RTD3), any
resident model blocks full power-off, and loading the model on demand
(first dictation) + unloading after an idle timeout would save ~4 W.
Implement only if/ when such a machine appears; config knob like
`model_idle_unload_s = 0` (0 = always resident).

## 4. Automated tests
The daemon has zero tests, and regressions fail silently (a missing Qt
signal connection took an hour of debugging to find — a test would have
caught it instantly). Targets, cheapest first:
- socket protocol round-trip (hello/state/done/level/retranscribe)
- config parser (defaults, overrides, bad values)
- UI `WfClient` reconnect logic (connect, disconnect, retry)
- hotkey state machine (combo transitions, multi-device, device removal)

## 5. Hotkey state machine hardening
Observed edge cases during testing: the 4 s rescan re-derives combo state
from `EVIOCGKEY`, and destroying a virtual device synthesizes key-up events;
under multiple simultaneous virtual keyboards the daemon recorded/restarted
in unexpected ways. Add a debounce/grace period and re-evaluate the
multi-device combo semantics (a held combo on *any* device should keep
recording; releases should only count per the device that started it).

## 6. Configurable hotkey combo
Ctrl+Win is hardcoded in `hotkey.c` (modifiers are fixed, the evdev layer
already handles arbitrary key codes). Config keys like `hotkey = ctrl+win`
or `hotkey = capslock hold`.

## 7. Live transcript preview while recording — parked
The flagship WisprFlow feature (text appears while you speak). **Decision
(2026-08-14): not pursued for now.** Streaming/partial decoding trades
accuracy for latency, and the requirement is *reliable* speech-to-text —
no shortcuts, no approximate drafts. Revisit only if a streaming whisper
approach demonstrably matches batch quality; otherwise the OSD state
feedback (done) is sufficient.

## 8. KDE Klipper DBus fallback for the clipboard
`wl-copy`/`wl-paste` requires wl-clipboard; Klipper has a DBus API that
could replace it (and remove the dependency). Note `restore_clipboard`
already handles clipboard preservation.

## 9. Auto-language punctuation prompt
Per-language punctuation prompt (e.g. German transcriptions get worse
punctuation than English) — whisper.cpp initial prompt per `language`.

## 10. Multiple saved recordings
`last.wav` is overwritten every dictation; keep a small ring of recent
recordings (or timestamped files) so "Replay" isn't lossy.
