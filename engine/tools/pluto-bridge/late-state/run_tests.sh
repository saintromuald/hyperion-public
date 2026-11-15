#!/bin/sh
# Host-only HXP4 tests. No official DLL, Wine process, game, or inference is launched.
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"

REPO=$(CDPATH= cd -- "$SCRIPT_DIR/../../../.." && pwd)
BRIDGE_ROOT="$REPO/engine/tools/pluto-bridge"
OUT=${OUT:-/tmp/opencode/pluto-late-state-tests}
PLUTO_DLL=${PLUTO_DLL:-$BRIDGE_ROOT/pluto.dll}
VERIFY_PE=${VERIFY_PE:-$BRIDGE_ROOT/verify_pe.py}
PLUTO_BRIDGE_EXE=${PLUTO_BRIDGE_EXE:-$BRIDGE_ROOT/pluto-bridge.exe}
BWAPI=${BWAPI:-$BRIDGE_ROOT/vendor/bwapi}
SHIM=${SHIM:-$BRIDGE_ROOT/vendor/shim}
if [ -n "${MINGW:-}" ]; then
  MINGW_CXX=${MINGW_CXX:-$MINGW/bin/i686-w64-mingw32-g++}
else
  MINGW_CXX=${MINGW_CXX:-i686-w64-mingw32-g++}
fi
mkdir -p "$OUT"

clang++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -I.. \
  late_state_tests.cpp -o "$OUT/late_state_tests"
"$OUT/late_state_tests"

g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Werror -pedantic -I.. late_state_tests.cpp -o "$OUT/late_state_tests_san"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 "$OUT/late_state_tests_san"

"$MINGW_CXX" -std=c++17 -m32 \
  -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-nested-anon-types \
  -I"$SHIM" -I"$BWAPI/include" -I"$BWAPI/BWAPI/Source" -I"$BWAPI/Shared" \
  -I"$BWAPI/BWAPIClient/Source" -I"$BWAPI/BWAPI/Source/BWAPI" \
  -I"$BWAPI/Util/Source" bwapi_layout_checks.cpp -o "$OUT/bwapi_layout_checks.exe"
# Do not execute the PE: all required checks are compile-time static_asserts.
echo "PASS BWAPI 4.4.0 i686 native layout static_asserts"

# Official disassembly remains available to local validation, but is neither
# downloaded nor required by the clean release build.
if [ -f "$PLUTO_DLL" ]; then
  llvm-objdump -d --x86-asm-syntax=intel "$PLUTO_DLL" > "$OUT/official-pluto.disasm.txt"
  python3 audit_official.py --dll "$PLUTO_DLL" --disasm "$OUT/official-pluto.disasm.txt" \
    --out "$OUT/official-audit.json"
else
  echo "SKIP official DLL audit (no proprietary validation input supplied)"
fi
python3 "$VERIFY_PE" "$PLUTO_BRIDGE_EXE" > "$OUT/pe-layout-verification.txt"
echo "PASS built PE .game reservation verification"
echo "late-state artifacts: $OUT"
