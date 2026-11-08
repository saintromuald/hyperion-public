#!/bin/sh
# Native host-logic tests only: no BWAPI, Wine, game, replay, or inference.
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CXX=${CXX:-c++}
BUILD_ROOT=${TMPDIR:-/tmp/opencode}
mkdir -p "$BUILD_ROOT"
BUILD_DIR=$(mktemp -d "$BUILD_ROOT/pluto-bridge-host.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  -I"$SCRIPT_DIR/.." "$SCRIPT_DIR/host-tests.cpp" -o "$BUILD_DIR/host-tests"
"$BUILD_DIR/host-tests"
PYTHONOPTIMIZE=2 python3 "$SCRIPT_DIR/verify-pe-tests.py"
