#!/bin/sh
# Fetch verified pinned sources, then build local extraction executables.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BIN="$HERE/bin"
MB="$HERE/third_party/.mbedtls-build"
JOBS=${JOBS:-8}
python3 "$HERE/setup_extraction.py"
mkdir -p "$BIN"
cmake -S "$HERE/third_party/hactool/mbedtls" -B "$MB" \
    -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build "$MB" --parallel "$JOBS" --target mbedtls mbedcrypto mbedx509
# Always regenerate local paths; a previous machine's config must not leak in.
cat > "$HERE/third_party/hactool/config.mk" <<EOF
CC = ${CC:-cc}
CFLAGS = -O2 -Wall -Wextra -pedantic -std=gnu11 -fPIC
LDFLAGS = $MB/library/libmbedtls.a $MB/library/libmbedx509.a $MB/library/libmbedcrypto.a -lm
EOF
make -C "$HERE/third_party/hactool" -j"$JOBS" hactool
cp "$HERE/third_party/hactool/hactool" "$BIN/hactool"
# NSTool and all child projects share this root's five dependencies. Nested
# recursive copies are unnecessary; see tools/README.md.
make -C "$HERE/third_party/nstool" deps -j"$JOBS"
make -C "$HERE/third_party/nstool" -j"$JOBS"
cp "$HERE/third_party/nstool/bin/nstool" "$BIN/nstool"
printf 'Extraction tools ready in %s\n' "$BIN"
