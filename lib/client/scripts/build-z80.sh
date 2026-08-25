#!/bin/sh
# Build the Z80 squid-server client archive inside an xcc Docker image.
# usage: build-z80.sh <platform> <build-dir> <library-dir> [run-emulator]
set -eu

PLATFORM="$1"
BUILD_DIR="$2"
LIBRARY_DIR="$3"
RUN_EMULATOR="${4:-0}"
CFLAGS="${Z80_CFLAGS:--std=c11 -Os -Wall -Wextra}"
LIBSQUID_INCLUDE_DIR="${LIBSQUID_INCLUDE_DIR:-/libsquid/include}"
LIBSQUID_SOURCE_DIR="${LIBSQUID_SOURCE_DIR:-/libsquid}"
MAX_SOCKETS="${SQUID_MAX_SOCKETS:-4}"

PLUGIN_ASM_SOURCES="
lib/client/z80/echo.s
lib/client/z80/filesystem.s
lib/client/z80/retrovault.s
lib/client/z80/system.s
lib/client/z80/tcp_proxy.s
lib/client/z80/time.s
"

mkdir -p "$BUILD_DIR" "$LIBRARY_DIR"

case "$MAX_SOCKETS" in
    *[!0-9]*|'')
        echo "error: SQUID_MAX_SOCKETS must be an integer from 1 to 15" >&2
        exit 1
        ;;
esac
if [ "$MAX_SOCKETS" -lt 1 ] || [ "$MAX_SOCKETS" -gt 15 ]; then
    echo "error: SQUID_MAX_SOCKETS must be from 1 to 15" >&2
    exit 1
fi

echo "  xas[$PLATFORM] lib/client/z80/client.s"
xas --mode=sdcc -o "$BUILD_DIR/client.rel" lib/client/z80/client.s
echo "  xas[$PLATFORM] lib/client/z80/internal.s"
xas --mode=sdcc -o "$BUILD_DIR/internal.rel" lib/client/z80/internal.s
OBJECTS="$BUILD_DIR/client.rel $BUILD_DIR/internal.rel"

for source in $PLUGIN_ASM_SOURCES; do
    base=${source##*/}
    object="$BUILD_DIR/${base%.s}.rel"
    echo "  xas[$PLATFORM] $source"
    xas --mode=sdcc -o "$object" "$source"
    OBJECTS="$OBJECTS $object"
done

# The .rel area size is the actual Z80 code contributed when that archive
# member is selected, unlike the on-disk .rel file size (which is textual).
SIZE_REPORT="$LIBRARY_DIR/code-sizes.txt"
report_code_size()
{
    label="$1"
    object="$2"
    code_hex=$(sed -n 's/^A _CODE size \([0-9A-Fa-f][0-9A-Fa-f]*\) .*/\1/p' \
        "$object")
    if [ -z "$code_hex" ]; then
        echo "error: no _CODE area in $object" >&2
        exit 1
    fi
    code_decimal=$(printf '%d' "0x$code_hex")
    printf '%-12s %5s bytes (0x%s)\n' "$label" "$code_decimal" "$code_hex"
}

{
    echo "Hand-written Z80 client archive member code sizes ($PLATFORM)"
    echo "Only referenced members are linked. libsquid and the application are excluded."
    report_code_size core "$BUILD_DIR/client.rel"
    report_code_size helpers "$BUILD_DIR/internal.rel"
    report_code_size echo "$BUILD_DIR/echo.rel"
    report_code_size system "$BUILD_DIR/system.rel"
    report_code_size time "$BUILD_DIR/time.rel"
    report_code_size filesystem "$BUILD_DIR/filesystem.rel"
    report_code_size retrovault "$BUILD_DIR/retrovault.rel"
    report_code_size tcp_proxy "$BUILD_DIR/tcp_proxy.rel"
} > "$SIZE_REPORT"
cat "$SIZE_REPORT"

case "$PLATFORM" in
    zx-ram|zx-rom)
        echo "  xas[$PLATFORM] lib/client/z80/spectrum_if1.s"
        xas --mode=sdcc -o "$BUILD_DIR/spectrum_if1.rel" \
            lib/client/z80/spectrum_if1.s
        echo "  xcc[$PLATFORM] lib/client/z80/spectrum_if1.c"
        # shellcheck disable=SC2086
        xcc --platform "$PLATFORM" $CFLAGS -Iinclude \
            -I"$LIBSQUID_INCLUDE_DIR" -c \
            -o "$BUILD_DIR/spectrum_if1_c.rel" \
            lib/client/z80/spectrum_if1.c
        OBJECTS="$OBJECTS $BUILD_DIR/spectrum_if1.rel"
        OBJECTS="$OBJECTS $BUILD_DIR/spectrum_if1_c.rel"
        ;;
esac

rm -f "$LIBRARY_DIR/libsquid_client.a" "$LIBRARY_DIR/libsquid_client.lib"
# shellcheck disable=SC2086
xar --mode=gnu rcs "$LIBRARY_DIR/libsquid_client.a" $OBJECTS
cp "$LIBRARY_DIR/libsquid_client.a" "$LIBRARY_DIR/libsquid_client.lib"
echo "  ar  [$PLATFORM] $LIBRARY_DIR/libsquid_client.a (+ .lib)"

# Build the pinned libsquid Z80 core beside the client so the output directory
# is a complete two-archive SDK with a known-compatible wire implementation.
echo "  xas[$PLATFORM] libsquid Z80 core"
xas --mode=sdcc -I"$LIBSQUID_SOURCE_DIR/lib/squid/z80" \
    -DSQUID_MAX_SOCKETS="$MAX_SOCKETS" \
    -o "$BUILD_DIR/libsquid_state.rel" \
    "$LIBSQUID_SOURCE_DIR/lib/squid/z80/state.s"
xas --mode=sdcc -I"$LIBSQUID_SOURCE_DIR/lib/squid/z80" \
    -DSQUID_MAX_SOCKETS="$MAX_SOCKETS" \
    -o "$BUILD_DIR/libsquid_core.rel" \
    "$LIBSQUID_SOURCE_DIR/lib/squid/z80/core.s"
rm -f "$LIBRARY_DIR/libsquid.a" "$LIBRARY_DIR/libsquid.lib"
xar --mode=gnu rcs "$LIBRARY_DIR/libsquid.a" \
    "$BUILD_DIR/libsquid_state.rel" "$BUILD_DIR/libsquid_core.rel"
cp "$LIBRARY_DIR/libsquid.a" "$LIBRARY_DIR/libsquid.lib"
echo "  ar  [$PLATFORM] $LIBRARY_DIR/libsquid.a (+ .lib)"

# Link on the selected target as a check that the archive and all ABI-facing
# declarations are usable by that platform's runtime.
echo "  link[$PLATFORM] lib/client/tests/z80/test_client_z80.c"
# shellcheck disable=SC2086
xcc --platform "$PLATFORM" $CFLAGS -Iinclude \
    -Map="$BUILD_DIR/client-link.map" -o "$BUILD_DIR/client-link.out" \
    lib/client/tests/z80/test_client_z80.c \
    "$LIBRARY_DIR/libsquid_client.a"

if [ "$RUN_EMULATOR" = 1 ]; then
    # The test supplies a one-byte-at-a-time libsquid mock. This validates the
    # assembly ABI, callback ABI, and selected typed C helpers at run time.
    echo "  test[xemu] lib/client/tests/z80/test_client_z80.c"
    # shellcheck disable=SC2086
    xcc --platform emu $CFLAGS -Iinclude --oformat=binary \
        -Map="$BUILD_DIR/client-test.map" -o "$BUILD_DIR/client-test.bin" \
        lib/client/tests/z80/test_client_z80.c \
        "$LIBRARY_DIR/libsquid_client.a"

    OUTPUT=$(xemu --run --emu-stdio --load-bin "$BUILD_DIR/client-test.bin" \
        --origin 0 --pc 0 --max-steps 10000000 2>&1)
    printf '%s\n' "$OUTPUT"
    case "$OUTPUT" in
        *"z80 client scenarios: OK"*) ;;
        *) echo "error: Z80 client scenarios did not complete" >&2; exit 1 ;;
    esac

    # Run the same successful request/response fixtures used by the portable
    # tests, but link them against the hand-written Z80 plugin implementations.
    echo "  test[xemu] every Z80 plugin protocol"
    # shellcheck disable=SC2086
    xcc --platform emu $CFLAGS -Iinclude --oformat=binary \
        -Map="$BUILD_DIR/service-test.map" \
        -o "$BUILD_DIR/service-test.bin" \
        lib/client/tests/test_services.c \
        "$LIBRARY_DIR/libsquid_client.a"

    SERVICE_OUTPUT=$(xemu --run --emu-stdio \
        --load-bin "$BUILD_DIR/service-test.bin" \
        --origin 0 --pc 0 --max-steps 20000000 2>&1)
    printf '%s\n' "$SERVICE_OUTPUT"
    case "$SERVICE_OUTPUT" in
        *"FAIL"*|*"checks failed"*)
            echo "error: a Z80 plugin protocol check failed" >&2
            exit 1
            ;;
        *"squid client service helpers:"*) ;;
        *) echo "error: Z80 plugin protocol checks did not complete" >&2; exit 1 ;;
    esac
fi

case "$PLATFORM" in
    zx-ram|zx-rom)
        echo "  link[$PLATFORM] Interface 1 platform"
        # shellcheck disable=SC2086
        xcc --platform "$PLATFORM" $CFLAGS -Iinclude \
            -I"$LIBSQUID_INCLUDE_DIR" \
            -Map="$BUILD_DIR/spectrum-if1-link.map" \
            -o "$BUILD_DIR/spectrum-if1-link.out" \
            lib/client/tests/z80/link_spectrum_if1.c \
            "$LIBRARY_DIR/libsquid_client.a" "$LIBRARY_DIR/libsquid.a"
        ;;
esac
