#!/usr/bin/env bash
# WisprFlow for Linux - build whisper.cpp + the daemon
#   uses CUDA when nvcc is available, otherwise falls back to CPU
set -euo pipefail
cd "$(dirname "$0")/.."

JOBS="$(nproc)"
CUDA=0

if command -v nvcc >/dev/null 2>&1; then
    CUDA=1
elif [ -x /opt/cuda/bin/nvcc ]; then
    CUDA=1
    export PATH="/opt/cuda/bin:$PATH"
fi

echo "==> Configuring whisper.cpp (backend: $([ $CUDA -eq 1 ] && echo CUDA || echo CPU))"
CMAKE_OPTS=(-B build -S whisper.cpp -DCMAKE_BUILD_TYPE=Release -DGGML_NATIVE=ON)
if [ "$CUDA" -eq 1 ]; then
    CMAKE_OPTS+=(-DGGML_CUDA=ON)
    CMAKE_OPTS+=(-DCMAKE_CUDA_COMPILER="$(command -v nvcc)")
    # RTX 4060 = Ada sm_89; pin it for a fast targeted build
    CMAKE_OPTS+=(-DCMAKE_CUDA_ARCHITECTURES=89-real)
fi
cmake "${CMAKE_OPTS[@]}"

echo "==> Building whisper.cpp ($JOBS jobs)"
cmake --build build -j"$JOBS" --target whisper whisper-cli

echo "==> Building the daemon"
make -C daemon

echo
echo "Built: bin/wispr-flow  [backend: $([ $CUDA -eq 1 ] && echo CUDA || echo CPU)]"
echo "Next:  ./scripts/install.sh"
