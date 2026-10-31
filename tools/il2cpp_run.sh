#!/bin/sh
# Compatibility entry point for the cross-platform symbol-analysis driver.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "${PYTHON:-python3}" "$HERE/analyze_il2cpp.py" "$@"
