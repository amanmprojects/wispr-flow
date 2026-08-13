# WisprFlow for Linux

A [WisprFlow](https://wisprflow.ai)-style dictation daemon for Linux, built on
[whisper.cpp](https://github.com/ggml-org/whisper.cpp) with full NVIDIA GPU
support (CUDA). Hold **Ctrl + Win** (either side works), speak, release — the
transcript is inserted into whatever application has focus.

```
Hold Ctrl+Win ──▶ record mic ──▶ whisper.cpp (CUDA on RTX 4060)
      └───── release ──▶ transcribe ──▶ clipboard ──▶ paste into focused app
```

## Why it works on Wayland

X11's `XGrabKey` does not work globally under Wayland. This daemon instead:

* **reads raw evdev keyboard events** (`/dev/input/event*`) to detect the
  hold/release gesture — compositor-independent, works with native Wayland
  apps, doesn't consume the keys. It rescans for hotplugged keyboards and
  initializes modifier state from the kernel (`EVIOCGKEY`) so no press is
  ever missed;
* **injects the paste through its own virtual keyboard** (`/dev/uinput`,
  built into the daemon — no external daemon needed) and uses `wl-copy` /
  `wl-paste` for the clipboard. `ydotool` is only used as an automatic
  fallback for key injection if uinput is unavailable.

## Requirements

* Arch Linux (pacman instructions; adapt for other distros)
* NVIDIA GPU + driver (tested on RTX 4060), CUDA toolkit for the GPU build
* PipeWire/PulseAudio with a microphone
* `wl-clipboard` (clipboard access for the paste step)
* KDE Plasma or any Wayland/X11 session

## Install

```sh
# 1. system packages (asks for sudo): cuda, wl-clipboard (+ ydotool fallback)
sudo ./scripts/setup.sh

# 2. log out and back in (uinput group), then:
./scripts/install.sh
```

`install.sh` builds whisper.cpp + the daemon, downloads the
`large-v3-turbo` model (~0.6 GB) into `models/`, installs the binary to
`~/.local/bin`, writes a default config and a KDE autostart entry.

Start it once to test:

```sh
~/.local/bin/wispr-flow
```

You should see `ready — hold Ctrl+Win to dictate`. Hold **Ctrl+Win**, speak,
release — the text appears at the cursor. The system tray icon shows the
state (gray mic = idle, pulsing red = recording, purple spinner =
transcribing, slashed = daemon offline) and each step pops up in Plasma's
native OSD overlay; a beep confirms each gesture and errors still arrive
as desktop notifications.

## Testing without a microphone

Set `source = monitor` in `~/.config/wispr-flow/config` (records what you
hear), restart the daemon, and play any audio while holding the hotkey:

```sh
paplay test/jfk.wav        # while holding Ctrl+Win
```

The bundled `wispr-inject` tool can simulate the hotkey gesture headlessly
(also handy for scripting):

```sh
bin/wispr-inject ctrl+win hold   # press and hold
# ... play audio ...
kill -INT %1                     # release -> transcribe + insert
bin/wispr-inject ctrl+v          # tap any combo, e.g. paste
bin/wispr-inject type "hello"    # type ASCII text
```

Verify the transcription pipeline directly:

```sh
~/.local/bin/wispr-flow --file test/jfk.wav
# "And so my fellow Americans ask not what your country can do for you..."
```

## The UI

`wispr-flow-ui` (Qt6) is a WisprFlow-style companion. It uses **no floating
windows** — the state lives in the shell itself, so it can never be hidden
behind other windows and always stays in sync:

* **system tray icon** (StatusNotifierItem, drawn by the panel) — gray mic =
  idle, **pulsing red** = recording, **purple spinner** = transcribing,
  gray with a red slash = daemon offline; tooltip shows the last transcript;
  left-click opens the last dictation, right-click for menus
* **Plasma OSD popups** — the same overlay the system uses for
  volume/brightness changes: “Listening…” on hold, “Transcribing…” while
  the model runs, then the inserted text (or the error) when done
* **Last dictation…** — shows the transcript, **replays the exact recording
  used** (to debug cut endings: if the replay ends mid-word, the cut is in the
  capture; if it ends cleanly, it's the model), and **retranscribes** it
* **Settings…** — language, audio source, insert mode, capture tail, etc.
  (edits the config file; "Restart daemon" applies changes)

On desktops without a system tray (e.g. GNOME without an extension) the UI
falls back to a small floating pill with the same states and menus.

The UI starts the daemon automatically when launched; the KDE autostart
entry runs the UI. The daemon also guards against double instances (flock)
and saves every recording to `~/.local/state/wispr-flow/last.wav`.

## Configuration

`~/.config/wispr-flow/config` — see `daemon/wispr-flow.conf.example` for all
options. Highlights:

| key | default | meaning |
|---|---|---|
| `language` | `en` | whisper language (`auto`, `de`, `zh`, …) |
| `source` | (default mic) | pulse source; `monitor` = what you hear |
| `insert_mode` | `paste` | `paste` (unicode-safe) or `type` (ASCII) |
| `paste_combo` | `ctrl+v` | shortcut used to insert the transcript |
| `model` | turbo q5_0 | any whisper.cpp ggml model |
| `min_audio_ms` / `max_audio_s` | 300 / 120 | guard rails |
| `notify` / `beep` | true / true | feedback |

## How the hotkey works

* The daemon watches every keyboard device that reports Ctrl + Meta keys.
* Pressing **both** starts recording; releasing either one stops it and
  triggers transcription. Pressing the combo again while the previous
  dictation is still being processed re-queues it.
* Virtual keyboards (ydotool, wtype, keyd…) are ignored so the injected
  paste shortcut can't retrigger the daemon.
* Keys are *not* consumed — normal Ctrl/Win shortcuts keep working.

## Troubleshooting

* **`wl-copy failed`** — wl-clipboard missing (`sudo pacman -S wl-clipboard`)
  or `WAYLAND_DISPLAY` unset (run the daemon from your desktop session, not
  over SSH).
* **`cannot send keys: uinput unavailable`** — the daemon needs write access
  to `/dev/uinput` (built-in virtual keyboard). On most systems it is
  already writable; if not, run `sudo ./scripts/setup.sh` once and
  re-login (uinput group + udev rule). `ydotool` is tried automatically as
  a fallback.
* **CUDA not used** — the daemon prints its backend at startup
  (`AVX=1 … CUDA=1`). Missing `nvcc` → CPU fallback; run
  `sudo ./scripts/setup.sh` then `./scripts/build.sh`.
* **No audio captured** — `pactl list sources short` to find your mic;
  set `source` accordingly.
* **`no speech detected`** — whisper may reject very short or very noisy
  clips; speak a little longer or check the mic level.
* **Debugging** — `WISPR_SAVE_REC=/tmp/rec.wav wispr-flow` saves every
  raw recording, and `bin/wispr-inject ctrl+win hold` simulates the hotkey
  gesture for scripted tests.
* **Transcript appears in the wrong app** — the paste goes to whichever
  window has focus; make sure it accepts Ctrl+V (a few terminals don't —
  set `insert_mode = type` or `paste_combo = ctrl+shift+v`).

## Project layout

```
daemon/     C daemon: config, evdev hotkey, pulse capture, whisper, insertion
scripts/    setup.sh (root) / build.sh / install.sh
whisper.cpp/ whisper.cpp submodule checkout
models/     downloaded ggml models
test/       sample audio
```

## Roadmap

- [x] live transcription state overlay (tray + Plasma OSD)
- [ ] live transcript preview while recording (beyond the OSD state popup)
- [ ] configurable hotkey combo (e.g. CapsLock hold)
- [ ] auto-language punctuation prompt per language
- [ ] systemd user service instead of autostart entry
- [ ] KDE Klipper DBus fallback for the clipboard (no wl-clipboard needed)
