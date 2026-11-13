#!/usr/bin/env sh
# Linux / MinGW / macOS: core lib + tool + tests (the DX11 demo is Windows-only)
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix dist
echo "OK -> dist/"
