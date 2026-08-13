#!/usr/bin/env bash
# WisprFlow for Linux - build (if needed) and install for the current user
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

MODEL_NAME="ggml-large-v3-turbo-q5_0.bin"

# 1. build
if [ ! -x bin/wispr-flow ]; then
    ./scripts/build.sh
fi
if [ ! -x build/ui/wispr-flow-ui ]; then
    cmake -B build/ui -S ui
    cmake --build build/ui -j"$(nproc)"
fi

# 2. layout
mkdir -p "$HOME/.local/bin" "$HOME/.local/lib/wispr-flow" \
         "$HOME/.local/share/wispr-flow" "$HOME/.config/wispr-flow" "$HOME/.config/autostart"

# 3. binary + whisper libs
install -m755 bin/wispr-flow "$HOME/.local/bin/wispr-flow"
install -m755 build/ui/wispr-flow-ui "$HOME/.local/bin/wispr-flow-ui"
install -m755 build/bin/libwhisper.so* build/bin/libggml.so* "$HOME/.local/lib/wispr-flow/" 2>/dev/null || true
echo "==> installed $HOME/.local/bin/wispr-flow (daemon) and wispr-flow-ui (UI)"

# 4. model
if [ ! -f "$ROOT/models/$MODEL_NAME" ]; then
    echo "Model not found yet - downloading (large-v3-turbo, ~1.6 GB)..."
    hf download ggerganov/whisper.cpp "$MODEL_NAME" --local-dir "$ROOT/models" 2>/dev/null \
        || curl -L -o "$ROOT/models/$MODEL_NAME" \
           "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$MODEL_NAME"
fi
ln -sfn "$ROOT/models/$MODEL_NAME" "$HOME/.local/share/wispr-flow/$MODEL_NAME"
echo "==> model: $HOME/.local/share/wispr-flow/$MODEL_NAME"

# 5. config (only if missing)
if [ ! -f "$HOME/.config/wispr-flow/config" ]; then
    install -m644 daemon/wispr-flow.conf.example "$HOME/.config/wispr-flow/config"
    echo "==> wrote default config $HOME/.config/wispr-flow/config"
fi

# 6. KDE autostart (UI starts the daemon itself)
cat > "$HOME/.config/autostart/wispr-flow.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=WisprFlow
Comment=Dictation - hold Ctrl+Win to speak
Exec=$HOME/.local/bin/wispr-flow-ui
Terminal=false
X-KDE-autostart-after=panel
X-KDE-autostart-phase=2
EOF
echo "==> autostart: $HOME/.config/autostart/wispr-flow.desktop (UI; starts the daemon as a fallback)"

# 7. systemd user service: journald logging + auto-restart on crash.
#    The UI's own daemon-spawn fallback stays in place for desktops/sessions
#    without systemd (the daemon's flock guard makes a duplicate harmless).
if command -v systemctl >/dev/null 2>&1 \
   && systemctl --user show-environment >/dev/null 2>&1; then
    mkdir -p "$HOME/.config/systemd/user"
    install -m644 scripts/wispr-flow.service "$HOME/.config/systemd/user/wispr-flow.service"
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

echo
echo "All done! Next steps:"
echo "  1. make sure the system setup ran:    sudo ./scripts/setup.sh"
echo "  2. log out/in if you just added the uinput group"
echo "  3. start the UI:  $HOME/.local/bin/wispr-flow-ui"
echo "     (or log out and back in - it autostarts with KDE)"
echo "  4. hold Ctrl+Win, speak, release - the transcript is pasted into"
echo "     whatever has focus. Right-click the tray icon for debug/settings."
echo "  5. test without a mic: set source = monitor in Settings or the config"
echo "  6. daemon logs (systemd):  journalctl --user -u wispr-flow -f"
