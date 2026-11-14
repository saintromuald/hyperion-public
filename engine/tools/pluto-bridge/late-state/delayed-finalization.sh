#!/bin/sh
# Official-DLL integration regression for the first-join-timeout retry.
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export HX_TEST_FINALIZE_DELAY_MS=3500
export WINEPREFIX=${WINEPREFIX:-/home/saintbellarmine/Games/pluto-reference/prefix}
python3 "$SCRIPT_DIR/replay_official_hxp4.py"
grep -q '\[bridge\] done (clean EOF)' "$SCRIPT_DIR/hxp4-long.err"
echo "PASS delayed finalization: first join times out, cancellation retry ends clean"
