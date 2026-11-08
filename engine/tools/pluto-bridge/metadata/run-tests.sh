#!/bin/sh
# Native codec tests only: no BWAPI, Wine, game, replay, or inference process.
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$SCRIPT_DIR/../../../.." && pwd)
CXX=${CXX:-clang++}  # clang accepts the real ep_region_t anonymous union on x64
BUILD_ROOT=${TMPDIR:-/tmp/opencode}
mkdir -p "$BUILD_ROOT"
BUILD_DIR=$(mktemp -d "$BUILD_ROOT/pluto-hxpm.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -fms-extensions \
  -D__cdecl= "-D__declspec(x)=" \
  -I"$REPO/engine/src117/h" -I"$REPO/engine/tools/pluto-bridge" \
  "$SCRIPT_DIR/hxpm-tests.cpp" -o "$BUILD_DIR/hxpm-tests"
"$BUILD_DIR/hxpm-tests"
