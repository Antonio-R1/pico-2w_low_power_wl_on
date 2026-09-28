#!/usr/bin/env bash
# Builds all 8 modes for both host sleep styles:
#   builds/12MHz_LPO/<mode>/<mode>.uf2        RP2350 sleeps at 12 MHz (clock-gated WFI)
#   builds/pstate_ram_off/<mode>/<mode>.uf2   RP2350 sleeps in Pstate, all SRAM off
# (CMake's own working files go to builds/<host>/_cmake.)
# Usage: ./build.sh
set -eu
cd "$(dirname "$0")"

# Needs PICO_SDK_PATH = a pico-sdk with the cyw43-driver patch applied (README.md).
[ -n "${PICO_SDK_PATH:-}" ] || { echo "PICO_SDK_PATH is not set (see README.md)"; exit 1; }

# picotool: an installed one is found by CMake. Otherwise the first build
# downloads and builds it, and the second build reuses that one.
PT=()

build() {   # build <folder> <WLP_PSTATE>
    cmake -S . -B "builds/$1/_cmake" -DCMAKE_BUILD_TYPE=Release -DWLP_PSTATE="$2" \
          -DWLP_OUT_DIR="$PWD/builds/$1" "${PT[@]}" > /dev/null
    cmake --build "builds/$1/_cmake" -j"$(nproc)" > "builds/$1/_cmake/build.log" 2>&1 \
        || { echo "FAIL builds/$1 (see builds/$1/_cmake/build.log)"; exit 1; }
    echo "OK   builds/$1:"; ls "builds/$1"/*/*.uf2 | sed 's/^/       /'
}

build 12MHz_LPO      OFF
[ -d builds/12MHz_LPO/_cmake/_deps/picotool ] && \
    PT=(-Dpicotool_DIR="$PWD/builds/12MHz_LPO/_cmake/_deps/picotool")
build pstate_ram_off ON
