#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
if [[ $(uname -s) != Darwin || $(uname -m) != arm64 ]]; then
    echo 'Native macOS Apple Silicon is required.' >&2; exit 1
fi
if [[ -z ${GOW3_NATIVE_AOT_DIR:-} ]]; then
    echo 'Use the macOS application for automatic preparation and launch.' >&2
    echo 'Maintainer launches require GOW3_NATIVE_AOT_DIR; see packaging/macos/native/README.md.' >&2
    exit 1
fi
exec "${GOW3_PYTHON:-python3}" -B run.py "$@"
