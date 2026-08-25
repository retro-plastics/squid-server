#!/bin/sh
# Host-side wrapper used by the optional CMake Z80 targets.
set -eu

IMAGE="$1"
PLATFORM="$2"
BUILD_DIR="$3"
LIBRARY_DIR="$4"
RUN_EMULATOR="${5:-0}"
LIBSQUID_SOURCE_DIR="$6"
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)

exec docker run --rm \
    --user "$(id -u):$(id -g)" \
    -v "$ROOT:/work" \
    -v "$LIBSQUID_SOURCE_DIR:/libsquid:ro" \
    -w /work \
    -e Z80_CFLAGS="${Z80_CFLAGS:--std=c11 -Os -Wall -Wextra}" \
    -e LIBSQUID_SOURCE_DIR=/libsquid \
    -e LIBSQUID_INCLUDE_DIR=/libsquid/include \
    -e SQUID_MAX_SOCKETS="${SQUID_MAX_SOCKETS:-4}" \
    "$IMAGE" \
    sh lib/client/scripts/build-z80.sh \
        "$PLATFORM" "$BUILD_DIR" "$LIBRARY_DIR" "$RUN_EMULATOR"
