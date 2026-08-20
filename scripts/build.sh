#!/usr/bin/env bash
# WisprFlow for Linux - build whisper.cpp + the daemon
#   uses CUDA when nvcc is available, otherwise falls back to CPU
set -euo pipefail
cd "$(dirname "$0")/.."

# --- whisper.cpp submodule check -------------------------------------------
if [ ! -f whisper.cpp/CMakeLists.txt ]; then
    echo "==> whisper.cpp not found (empty checkout)" >&2
    if [ -f .gitmodules ] && command -v git >/dev/null 2>&1; then
        echo "    running: git submodule update --init --recursive" >&2
        if git submodule update --init --recursive; then
            echo "    submodule fetched" >&2
        else
            echo "error: git submodule update failed" >&2
            exit 1
        fi
    else
        cat >&2 <<'EOF'
error: whisper.cpp/CMakeLists.txt missing and no .gitmodules / git available.
  Fix:
    git clone --recursive https://github.com/ggml-org/whisper.cpp whisper.cpp
  or if this repo should have submodules:
    git submodule update --init --recursive
EOF
        exit 1
    fi
    if [ ! -f whisper.cpp/CMakeLists.txt ]; then
        cat >&2 <<'EOF'
error: whisper.cpp/CMakeLists.txt still missing after submodule update.
  Try:
    git clone https://github.com/ggml-org/whisper.cpp whisper.cpp
EOF
        exit 1
    fi
fi

JOBS="$(nproc)"
CUDA=0

if command -v nvcc >/dev/null 2>&1; then
    CUDA=1
elif [ -x /opt/cuda/bin/nvcc ]; then
    CUDA=1
    export PATH="/opt/cuda/bin:$PATH"
elif [ -x /usr/local/cuda/bin/nvcc ]; then
    CUDA=1
    export PATH="/usr/local/cuda/bin:$PATH"
elif [ -x /usr/lib/cuda/bin/nvcc ]; then
    CUDA=1
    export PATH="/usr/lib/cuda/bin:$PATH"
fi

# GGML_NATIVE: native arch tuning; can break cross-builds. Keep on by default
# but allow opt-out via GGML_NATIVE=OFF env.
GGML_NATIVE="${GGML_NATIVE:-ON}"
if [ "$GGML_NATIVE" = "ON" ]; then
    echo "    note: GGML_NATIVE=ON (set GGML_NATIVE=OFF to disable native tuning)" >&2
fi

echo "==> Configuring whisper.cpp (backend: $([ $CUDA -eq 1 ] && echo CUDA || echo CPU))"
CMAKE_OPTS=(-B build -S whisper.cpp -DCMAKE_BUILD_TYPE=Release "-DGGML_NATIVE=$GGML_NATIVE")
if [ "$CUDA" -eq 1 ]; then
    CMAKE_OPTS+=(-DGGML_CUDA=ON)
    CMAKE_OPTS+=(-DCMAKE_CUDA_COMPILER="$(command -v nvcc)")
    # Auto-detect CUDA arch; fallback to broad coverage if detection fails.
    CUDA_ARCH=""
    if command -v nvidia-smi >/dev/null 2>&1; then
        # nvidia-smi prints like "8.9" or "8.9, 8.9" for multi-GPU
        cap="$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>/dev/null | head -n1 | tr -d '[:space:]' || true)"
        if [ -n "$cap" ]; then
            # normalize "8.9" -> "89"
            arch_nodot="$(echo "$cap" | tr -d '.')"
            # sanity: must be 2-3 digits
            if echo "$arch_nodot" | grep -Eq '^[0-9]{2,3}$'; then
                CUDA_ARCH="${arch_nodot}-real"
                echo "    detected GPU compute capability $cap -> -DCMAKE_CUDA_ARCHITECTURES=$CUDA_ARCH"
            fi
        fi
    fi
    if [ -n "$CUDA_ARCH" ]; then
        CMAKE_OPTS+=("-DCMAKE_CUDA_ARCHITECTURES=$CUDA_ARCH")
    else
        # No GPU queryable (headless / driver not loaded / nvidia-smi missing).
        # Use a broad set covering common consumer cards + PTX fallback.
        # CMake will JIT PTX for newer archs not listed.
        echo "    GPU arch not detected; using broad CUDA arch list (75/80/86/89/90 + PTX)" >&2
        CMAKE_OPTS+=("-DCMAKE_CUDA_ARCHITECTURES=75-real;80-real;86-real;89-real;90-real")
        # Alternative: omit the flag entirely to let whisper.cpp default.
        # Uncomment to prefer default: leave CMAKE_CUDA_ARCHITECTURES unset.
    fi
fi
cmake "${CMAKE_OPTS[@]}"

echo "==> Building whisper.cpp ($JOBS jobs)"
cmake --build build -j"$JOBS" --target whisper whisper-cli

echo "==> Building the daemon"
make -C daemon

echo
echo "Built: bin/wispr-flow  [backend: $([ $CUDA -eq 1 ] && echo CUDA || echo CPU)]"
echo "Next:  ./scripts/install.sh"
