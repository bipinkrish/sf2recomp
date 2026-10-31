#!/bin/sh
# Compatibility entry point; the native build driver also runs directly on Windows.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "${PYTHON:-python3}" "$HERE/build_extraction_tools.py" "$@"
