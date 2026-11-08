#!/bin/sh
# Host-only HXP4 tests. No official DLL, Wine process, game, or inference is launched.
set -eu
cd "$(dirname "$0")"

REPO=$(CDPATH= cd -- "$(dirname "$0")/../../../.." && pwd)
OUT=${OUT:-/tmp/opencode/pluto-late-state-tests}
PLUTO_DLL=${PLUTO_DLL:-$REPO/engine/tools/pluto-bridge/pluto.dll}
VERIFY_PE=${VERIFY_PE:-$REPO/engine/tools/pluto-bridge/verify_pe.py}
PLUTO_BRIDGE_EXE=${PLUTO_BRIDGE_EXE:-/tmp/opencode/pluto-official-image-work/pluto-bridge.exe}
MINGW=${MINGW:-/home/saintbellarmine/Downloads/llvm-mingw-20261020-msvcrt-ubuntu-22.04-x86_64}
BWAPI=${BWAPI:-/home/saintbellarmine/Github/bwapi/bwapi}
SHIM=${SHIM:-/home/saintbellarmine/Github/pluto/rig/shim}
mkdir -p "$OUT"

clang++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -I.. \
  late_state_tests.cpp -o "$OUT/late_state_tests"
"$OUT/late_state_tests"

g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Werror -pedantic -I.. late_state_tests.cpp -o "$OUT/late_state_tests_san"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 "$OUT/late_state_tests_san"

"$MINGW/bin/i686-w64-mingw32-g++" -std=c++17 -m32 \
  -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-nested-anon-types \
  -I"$SHIM" -I"$BWAPI/include" -I"$BWAPI/BWAPI/Source" -I"$BWAPI/Shared" \
  -I"$BWAPI/BWAPIClient/Source" -I"$BWAPI/BWAPI/Source/BWAPI" -I"$BWAPI/bwapi" \
  -I"$BWAPI/Util/Source" bwapi_layout_checks.cpp -o "$OUT/bwapi_layout_checks.exe"
# Do not execute the PE: all required checks are compile-time static_asserts.
echo "PASS BWAPI 4.4.0 i686 native layout static_asserts"

llvm-objdump -d --x86-asm-syntax=intel "$PLUTO_DLL" > "$OUT/official-pluto.disasm.txt"
python3 audit_official.py --dll "$PLUTO_DLL" --disasm "$OUT/official-pluto.disasm.txt" \
  --out "$OUT/official-audit.json"
python3 "$VERIFY_PE" "$PLUTO_BRIDGE_EXE" > "$OUT/pe-layout-verification.txt"
echo "PASS existing PE .game reservation verification"
echo "late-state artifacts: $OUT"
