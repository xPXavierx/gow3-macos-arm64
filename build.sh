#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
if [[ $(uname -s) != Darwin || $(uname -m) != arm64 ]]; then
    echo 'This source tree targets native macOS Apple Silicon (ARM64).' >&2
    exit 1
fi
usage() {
    echo 'Maintainer build: ./build.sh --host-tests | --renderer | --native [CMake options]'
    echo 'Players should use the app from GitHub Releases; no developer tools are needed.'
    echo 'For --native, set GOW3_RECOMP_SOURCE_DIRECTORY to the patched pinned recompiler.'
    echo 'See packaging/macos/native/README.md and packaging/macos/aot/README.md.'
}
mode=${1:---help}
if [[ $mode == --help || $mode == -h ]]; then usage; exit 0; fi
shift
prefix=${GOW3_DEPENDENCY_PREFIX:-/opt/homebrew}
cmake_common=(-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=RelWithDebInfo "-DCMAKE_PREFIX_PATH=$prefix")
case $mode in
--host-tests)
    cmake -S packaging/macos -B out/macos-host "${cmake_common[@]}" "$@"
    cmake --build out/macos-host --parallel 6
    ctest --test-dir out/macos-host --output-on-failure
    ;;
--renderer|--native)
    cmake -S gpu -B out/gpu-arm64-release "${cmake_common[@]}" "$@"
    cmake --build out/gpu-arm64-release --target gow3gpu --parallel 6
    if [[ $mode == --native ]]; then
        : "${GOW3_RECOMP_SOURCE_DIRECTORY:?Set the patched ARM64 recompiler source directory}"
        cmake -S packaging/macos/native -B out/macos-native-release "${cmake_common[@]}"             "-DGOW3_RECOMP_SOURCE_DIRECTORY=$GOW3_RECOMP_SOURCE_DIRECTORY"             "-DGOW3_GPU_LIBRARY=$PWD/out/gpu-arm64-release/libgow3gpu.dylib" "$@"
        cmake --build out/macos-native-release --parallel 6
        ctest --test-dir out/macos-native-release --output-on-failure
    fi
    ;;
*) usage >&2; exit 2 ;;
esac
