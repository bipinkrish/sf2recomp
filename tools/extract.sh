#!/bin/sh
# Prepare the supported combined base/update NSP; inputs and keys remain local.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$HERE/extract.py" "$@"
