#!/usr/bin/env bash
# WisprFlow for Linux - system setup (requires root, run once)
#   sudo ./scripts/setup.sh
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Please run as root: sudo $0" >&2
    exit 1
fi

echo "==> Installing packages: cuda (GPU toolkit), wl-clipboard (paste), ydotool (optional fallback)"
pacman -S --needed --noconfirm cuda wl-clipboard ydotool || \
    pacman -S --needed --noconfirm cuda wl-clipboard

# --- uinput access (used by the daemon's built-in virtual keyboard) --------
# /dev/uinput is usually already writable; the udev rule makes it explicit.
echo "==> Setting up uinput access"
getent group uinput >/dev/null 2>&1 || groupadd -r uinput

USER_NAME="${SUDO_USER:-$USER}"
if [ -n "$USER_NAME" ] && [ "$USER_NAME" != "root" ]; then
    usermod -aG uinput "$USER_NAME"
    echo "    added '$USER_NAME' to group 'uinput'"
fi

udevadm control --reload-rules 2>/dev/null || true
udevadm trigger 2>/dev/null || true

# --- ydotool daemon (optional fallback for key injection) ---
echo "==> Enabling ydotool daemon (fallback)"
if systemctl enable --now ydotool 2>/dev/null; then
    echo "    ydotool.service started (system)"
elif systemctl --user enable --now ydotool 2>/dev/null; then
    echo "    ydotool.service started (user)"
else
    echo "    note: ydotool.service not started (optional - the daemon injects keys itself)"
fi

# --- CUDA environment ---
if [ -x /opt/cuda/bin/nvcc ]; then
    if [ ! -f /etc/profile.d/wispr-flow-cuda.sh ]; then
        echo 'export PATH="/opt/cuda/bin:$PATH"' > /etc/profile.d/wispr-flow-cuda.sh
        echo 'export CUDA_HOME="/opt/cuda"' >> /etc/profile.d/wispr-flow-cuda.sh
        echo "    wrote /etc/profile.d/wispr-flow-cuda.sh (PATH for nvcc)"
    fi
    # make ldconfig aware of CUDA libs if not already
    ldconfig -p | grep -q "/opt/cuda/lib64" || {
        echo "/opt/cuda/lib64" > /etc/ld.so.conf.d/cuda.conf
        ldconfig
        echo "    added /opt/cuda/lib64 to ld.so.conf.d"
    }
fi

echo
echo "Done. IMPORTANT:"
echo "  * log out and back in (or run: newgrp uinput) so the uinput group applies"
echo "  * then build & install:  ./scripts/install.sh"
echo "  * verify CUDA:           nvcc --version"
