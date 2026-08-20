#!/usr/bin/env bash
# WisprFlow for Linux - system setup (requires root, run once)
#   sudo ./scripts/setup.sh
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Please run as root: sudo $0" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Distro-aware package install
# ---------------------------------------------------------------------------
PKGS_COMMON="wl-clipboard ydotool"
# cuda package name varies by distro
if command -v pacman >/dev/null 2>&1; then
    PKGS="cuda $PKGS_COMMON"
    echo "==> Installing packages via pacman: $PKGS"
    pacman -S --needed --noconfirm $PKGS || \
        pacman -S --needed --noconfirm cuda wl-clipboard || true
elif command -v apt-get >/dev/null 2>&1; then
    PKGS="nvidia-cuda-toolkit $PKGS_COMMON"
    echo "==> Installing packages via apt: $PKGS"
    apt-get update -qq || true
    # shellcheck disable=SC2086
    if ! apt-get install -y $PKGS; then
        echo "    (retry without ydotool)"
        apt-get install -y nvidia-cuda-toolkit wl-clipboard || true
    fi
elif command -v dnf >/dev/null 2>&1; then
    # Fedora/RHEL: cuda-toolkit from rpmfusion/non-free or NVIDIA repo
    PKGS="cuda-toolkit $PKGS_COMMON"
    echo "==> Installing packages via dnf: $PKGS"
    # shellcheck disable=SC2086
    dnf install -y $PKGS || dnf install -y cuda-toolkit wl-clipboard || true
elif command -v zypper >/dev/null 2>&1; then
    PKGS="cuda $PKGS_COMMON"
    echo "==> Installing packages via zypper: $PKGS"
    # shellcheck disable=SC2086
    zypper --non-interactive install $PKGS || zypper --non-interactive install cuda wl-clipboard || true
else
    cat >&2 <<'EOF'
==> Unknown distribution: no supported package manager found (pacman/apt/dnf/zypper).
    Please install manually:
      Arch:    pacman -S cuda wl-clipboard ydotool
      Debian/Ubuntu: apt install nvidia-cuda-toolkit wl-clipboard ydotool
      Fedora:  dnf install cuda-toolkit wl-clipboard ydotool
      openSUSE: zypper install cuda wl-clipboard ydotool
    Then re-run this script.
EOF
fi

# --- uinput access (used by the daemon's built-in virtual keyboard) --------
echo "==> Setting up uinput access"
getent group uinput >/dev/null 2>&1 || groupadd -r uinput
getent group input  >/dev/null 2>&1 || groupadd -r input 2>/dev/null || true

# udev rule: make /dev/uinput owned by group uinput, mode 0660
UDEV_RULE="/etc/udev/rules.d/60-uinput.rules"
if [ ! -f "$UDEV_RULE" ]; then
    cat > "$UDEV_RULE" <<'RULE'
KERNEL=="uinput", MODE="0660", GROUP="uinput"
RULE
    echo "    wrote $UDEV_RULE"
else
    # ensure it contains the expected line (idempotent)
    if ! grep -q 'KERNEL=="uinput"' "$UDEV_RULE"; then
        echo 'KERNEL=="uinput", MODE="0660", GROUP="uinput"' >> "$UDEV_RULE"
        echo "    appended uinput rule to $UDEV_RULE"
    else
        echo "    $UDEV_RULE already exists"
    fi
fi

USER_NAME="${SUDO_USER:-$USER}"
if [ -n "$USER_NAME" ] && [ "$USER_NAME" != "root" ]; then
    usermod -aG uinput "$USER_NAME" || true
    echo "    added '$USER_NAME' to group 'uinput'"
    # also add to input group so evdev hotkey works without extra setup
    usermod -aG input "$USER_NAME" || true
    echo "    added '$USER_NAME' to group 'input'"
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
# Detect cuda install across common locations
CUDA_HOME_CANDIDATES=("/opt/cuda" "/usr/local/cuda" "/usr/lib/cuda")
CUDA_HOME=""
for cand in "${CUDA_HOME_CANDIDATES[@]}"; do
    if [ -x "$cand/bin/nvcc" ]; then
        CUDA_HOME="$cand"
        break
    fi
done

if [ -n "$CUDA_HOME" ]; then
    if [ ! -f /etc/profile.d/wispr-flow-cuda.sh ]; then
        {
            echo 'for _wf_cuda in /opt/cuda /usr/local/cuda /usr/lib/cuda; do'
            echo '    if [ -x "$_wf_cuda/bin/nvcc" ]; then'
            echo '        export PATH="$_wf_cuda/bin:$PATH"'
            echo '        export CUDA_HOME="$_wf_cuda"'
            echo '        break'
            echo '    fi'
            echo 'done'
            echo 'unset _wf_cuda'
        } > /etc/profile.d/wispr-flow-cuda.sh
        echo "    wrote /etc/profile.d/wispr-flow-cuda.sh (PATH for nvcc)"
    fi
    # make ldconfig aware of CUDA libs if not already
    for libdir in "$CUDA_HOME/lib64" "$CUDA_HOME/lib" "/usr/lib/x86_64-linux-gnu"; do
        if [ -d "$libdir" ] && ldconfig -p 2>/dev/null | grep -q "$libdir"; then
            break
        fi
        if [ -d "$libdir" ] && [ -f "$libdir/libcudart.so" ] 2>/dev/null; then
            echo "$libdir" > /etc/ld.so.conf.d/cuda.conf
            ldconfig 2>/dev/null || true
            echo "    added $libdir to ld.so.conf.d"
            break
        fi
    done
    # fallback: ensure at least $CUDA_HOME/lib64 is registered
    if [ -d "$CUDA_HOME/lib64" ] && ! ldconfig -p 2>/dev/null | grep -q "$CUDA_HOME/lib64"; then
        echo "$CUDA_HOME/lib64" > /etc/ld.so.conf.d/cuda.conf
        ldconfig 2>/dev/null || true
        echo "    added $CUDA_HOME/lib64 to ld.so.conf.d"
    fi
fi

echo
echo "Done. IMPORTANT:"
echo "  * log out and back in (or run: newgrp uinput) so the uinput/input groups apply"
echo "  * then build & install:  ./scripts/install.sh"
echo "  * verify CUDA:           nvcc --version"
