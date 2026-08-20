#!/usr/bin/env bash
# WisprFlow for Linux - build (if needed) and install for the current user
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

MODEL_NAME="ggml-large-v3-turbo-q5_0.bin"

# XDG base dirs (respect user env, fallback to spec defaults)
XDG_BIN_HOME="${XDG_BIN_HOME:-$HOME/.local/bin}"
XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
XDG_STATE_HOME="${XDG_STATE_HOME:-$HOME/.local/state}"

# 1. build
if [ ! -x bin/wispr-flow ]; then
    ./scripts/build.sh
fi
if [ ! -x build/ui/wispr-flow-ui ]; then
    if command -v cmake >/dev/null 2>&1; then
        if cmake -B build/ui -S ui 2>/dev/null; then
            cmake --build build/ui -j"$(nproc)"
        else
            cat >&2 <<'EOF'
==> Qt6 not found or cmake configure failed - skipping UI build.
    Install Qt6 (Widgets, Network, DBus) and re-run:
      Arch:    pacman -S qt6-base
      Debian/Ubuntu: apt install qt6-base-dev
      Fedora:  dnf install qt6-qtbase-devel
    Then: cmake -B build/ui -S ui && cmake --build build/ui
EOF
        fi
    else
        echo "==> cmake not found - skipping UI build" >&2
    fi
fi

# 2. layout (XDG-aware)
mkdir -p "$XDG_BIN_HOME" "$HOME/.local/lib/wispr-flow" \
         "$XDG_DATA_HOME/wispr-flow" "$XDG_CONFIG_HOME/wispr-flow" "$XDG_CONFIG_HOME/autostart" \
         "$XDG_STATE_HOME/wispr-flow"

# 3. binary + whisper libs
install -m755 bin/wispr-flow "$XDG_BIN_HOME/wispr-flow"
if [ -x build/ui/wispr-flow-ui ]; then
    install -m755 build/ui/wispr-flow-ui "$XDG_BIN_HOME/wispr-flow-ui"
    echo "==> installed $XDG_BIN_HOME/wispr-flow (daemon) and wispr-flow-ui (UI)"
else
    echo "==> installed $XDG_BIN_HOME/wispr-flow (daemon only; UI not built)" >&2
fi
install -m755 build/bin/libwhisper.so* build/bin/libggml.so* "$HOME/.local/lib/wispr-flow/" 2>/dev/null || true
# ensure data-home lib dir exists for symlink consumers that look there
mkdir -p "$XDG_DATA_HOME/wispr-flow"

# 4. model (with disk-space check, curl retry, wget fallback, size verify)
if [ ! -f "$ROOT/models/$MODEL_NAME" ]; then
    echo "Model not found yet - downloading (large-v3-turbo, ~1.6 GB)..."
    mkdir -p "$ROOT/models"
    # require ~2 GB free on the filesystem containing $ROOT/models
    if command -v df >/dev/null 2>&1; then
        avail_kb="$(df -k "$ROOT/models" 2>/dev/null | awk 'NR==2{print $4}')"
        if [ -n "$avail_kb" ] && [ "$avail_kb" -lt 2097152 ]; then
            echo "error: not enough disk space in $ROOT/models (need ~2 GB, have ${avail_kb} KB)" >&2
            exit 1
        fi
    fi
    MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$MODEL_NAME"
    downloaded=0
    if command -v curl >/dev/null 2>&1; then
        if curl -L --retry 3 --retry-delay 2 -o "$ROOT/models/$MODEL_NAME" "$MODEL_URL"; then
            downloaded=1
        else
            echo "    curl failed, trying wget..." >&2
        fi
    fi
    if [ "$downloaded" -eq 0 ] && command -v wget >/dev/null 2>&1; then
        if wget -O "$ROOT/models/$MODEL_NAME" "$MODEL_URL"; then
            downloaded=1
        fi
    fi
    if [ "$downloaded" -eq 0 ] && command -v hf >/dev/null 2>&1; then
        if hf download ggerganov/whisper.cpp "$MODEL_NAME" --local-dir "$ROOT/models" 2>/dev/null; then
            downloaded=1
        fi
    fi
    if [ "$downloaded" -eq 0 ]; then
        echo "error: model download failed (tried curl, wget, hf). Check network and retry." >&2
        exit 1
    fi
    # verify file size >100M
    if command -v stat >/dev/null 2>&1; then
        fsize="$(stat -c%s "$ROOT/models/$MODEL_NAME" 2>/dev/null || stat -f%z "$ROOT/models/$MODEL_NAME" 2>/dev/null || echo 0)"
        if [ "$fsize" -lt 104857600 ]; then
            echo "error: downloaded model too small (${fsize} bytes), likely truncated" >&2
            rm -f "$ROOT/models/$MODEL_NAME"
            exit 1
        fi
    fi
fi
mkdir -p "$XDG_DATA_HOME/wispr-flow"
ln -sfn "$ROOT/models/$MODEL_NAME" "$XDG_DATA_HOME/wispr-flow/$MODEL_NAME"
# also keep legacy symlink for existing configs that point to ~/.local/share
if [ "$XDG_DATA_HOME" != "$HOME/.local/share" ]; then
    mkdir -p "$HOME/.local/share/wispr-flow"
    ln -sfn "$ROOT/models/$MODEL_NAME" "$HOME/.local/share/wispr-flow/$MODEL_NAME" 2>/dev/null || true
fi
echo "==> model: $XDG_DATA_HOME/wispr-flow/$MODEL_NAME"

# 5. config (only if missing)
if [ ! -f "$XDG_CONFIG_HOME/wispr-flow/config" ]; then
    install -m644 daemon/wispr-flow.conf.example "$XDG_CONFIG_HOME/wispr-flow/config"
    echo "==> wrote default config $XDG_CONFIG_HOME/wispr-flow/config"
fi

# 6. KDE autostart (UI starts the daemon itself) — quote Exec, add TryExec
AUTOSTART_FILE="$XDG_CONFIG_HOME/autostart/wispr-flow.desktop"
cat > "$AUTOSTART_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=WisprFlow
Comment=Dictation - hold Ctrl+Win to speak
TryExec=$XDG_BIN_HOME/wispr-flow-ui
Exec="$XDG_BIN_HOME/wispr-flow-ui"
Terminal=false
X-KDE-autostart-after=panel
X-KDE-autostart-phase=2
EOF
echo "==> autostart: $AUTOSTART_FILE (UI; starts the daemon as a fallback)"

# 7. systemd user service: journald logging + auto-restart on crash.
#    The UI's own daemon-spawn fallback stays in place for desktops/sessions
#    without systemd (the daemon's flock guard makes a duplicate harmless).
if command -v systemctl >/dev/null 2>&1 \
   && systemctl --user show-environment >/dev/null 2>&1; then
    mkdir -p "$XDG_CONFIG_HOME/systemd/user"
    install -m644 scripts/wispr-flow.service "$XDG_CONFIG_HOME/systemd/user/wispr-flow.service"
    systemctl --user daemon-reload
    systemctl --user enable wispr-flow.service
    echo "==> systemd user service enabled (starts with the graphical session)"
    if systemctl --user is-active wispr-flow.service >/dev/null 2>&1; then
        echo "==> daemon already running as a service"
    elif pgrep -x wispr-flow >/dev/null 2>&1; then
        echo "==> NOTE: an unsystemd daemon instance is still running."
        echo "    Hand it over to the service:"
        echo "    pkill -x wispr-flow && systemctl --user start wispr-flow"
    else
        systemctl --user start wispr-flow.service || true
    fi
else
    echo "==> no systemd user session detected - the UI will start the daemon itself"
fi

# 8. uninstall stub (idempotent: create if missing)
UNINSTALL_SH="$ROOT/scripts/uninstall.sh"
if [ ! -f "$UNINSTALL_SH" ]; then
    cat > "$UNINSTALL_SH" <<'EOS'
#!/usr/bin/env bash
# WisprFlow - uninstall (removes user-local install)
set -euo pipefail
XDG_BIN_HOME="${XDG_BIN_HOME:-$HOME/.local/bin}"
XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
echo "==> Stopping services..."
systemctl --user disable --now wispr-flow.service 2>/dev/null || true
pkill -x wispr-flow 2>/dev/null || true
pkill -x wispr-flow-ui 2>/dev/null || true
echo "==> Removing binaries..."
rm -f "$XDG_BIN_HOME/wispr-flow" "$XDG_BIN_HOME/wispr-flow-ui"
rm -rf "$HOME/.local/lib/wispr-flow"
echo "==> Removing autostart & service..."
rm -f "$XDG_CONFIG_HOME/autostart/wispr-flow.desktop"
rm -f "$XDG_CONFIG_HOME/systemd/user/wispr-flow.service"
systemctl --user daemon-reload 2>/dev/null || true
echo "==> Data/config kept (remove manually if desired):"
echo "    $XDG_DATA_HOME/wispr-flow/"
echo "    $XDG_CONFIG_HOME/wispr-flow/"
echo "    $HOME/.local/share/wispr-flow/  (legacy)"
echo "    models/  (in repo checkout)"
echo "To fully remove config/data: rm -rf \"\$XDG_CONFIG_HOME/wispr-flow\" \"\$XDG_DATA_HOME/wispr-flow\""
EOS
    chmod +x "$UNINSTALL_SH"
    echo "==> wrote $UNINSTALL_SH"
fi

echo
echo "All done! Next steps:"
echo "  1. make sure the system setup ran:    sudo ./scripts/setup.sh"
echo "  2. log out/in if you just added the uinput/input groups"
echo "  3. start the UI:  \"$XDG_BIN_HOME/wispr-flow-ui\""
echo "     (or log out and back in - it autostarts with KDE)"
echo "  4. hold Ctrl+Win, speak, release - the transcript is pasted into"
echo "     whatever has focus. Right-click the tray icon for debug/settings."
echo "  5. test without a mic: set source = monitor in Settings or the config"
echo "  6. daemon logs (systemd):  journalctl --user -u wispr-flow -f"
