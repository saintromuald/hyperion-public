#!/bin/sh
# Native deterministic tests only: no BWAPI, Wine, game, replay, or inference.
set -eu
cd "$(dirname "$0")"
CXX=${CXX:-c++}
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/pluto-lifecycle.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  tracker-tests.cpp -o "$BUILD_DIR/tracker-tests"
"$BUILD_DIR/tracker-tests"
