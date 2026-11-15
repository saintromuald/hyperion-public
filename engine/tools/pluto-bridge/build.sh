#!/bin/sh
# Build the Hyperion-owned bridge with an image-backed StarCraft reservation.
# All BWAPI sources and the case-insensitive Windows.h shim are vendored below.
# SOURCE.json records their upstream commit, local changes, and the pinned CI toolchain.
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"
export LC_ALL=C
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}

if [ -n "${MINGW:-}" ]; then
  CXX=${CXX:-$MINGW/bin/i686-w64-mingw32-g++}
  OBJCOPY=${OBJCOPY:-$MINGW/bin/llvm-objcopy}
else
  CXX=${CXX:-i686-w64-mingw32-g++}
  OBJCOPY=${OBJCOPY:-llvm-objcopy}
fi
command -v "$CXX" >/dev/null 2>&1 || {
  echo "missing llvm-mingw i686 compiler: $CXX (set MINGW or CXX)" >&2
  exit 1
}
command -v "$OBJCOPY" >/dev/null 2>&1 || {
  echo "missing llvm-objcopy: $OBJCOPY (set MINGW or OBJCOPY)" >&2
  exit 1
}

BWAPI="$SCRIPT_DIR/vendor/bwapi"
PLUTO_SHIM="$SCRIPT_DIR/vendor/shim"
SRC="$BWAPI/BWAPIClient/Source"
LIB="$BWAPI/BWAPILIB/Source"
SHARED="$BWAPI/Shared"
mkdir -p dump
rm -f dump/pluto-bridge-linked.exe dump/pluto-bridge.debug \
  dump/pluto-bridge-stripped.exe pluto-bridge.exe

# Link fixed at 0x800000 with every real function/data item in its own input
# section. Debug data is separated before pluto_pe.py adds .game/.hdr, because
# the full debug section table leaves no room in SizeOfHeaders. The transform
# moves the final image base to 0x400000 without moving any linked virtual
# address, adds the RWX .game reservation at 0x401000..0x800000, and rewrites
# RVAs/imports/TLS/debug metadata. LAA is required for the 0xF0B5D710 stack.
# Prefix maps and no-insert-timestamp make identical inputs produce identical bytes.
"$CXX" -std=c++17 -O1 -g -m32 -ffunction-sections -fdata-sections \
  -ffile-prefix-map="$SCRIPT_DIR"=. -fdebug-prefix-map="$SCRIPT_DIR"=. \
  -I"$PLUTO_SHIM" -I"$BWAPI/include/BWAPI/Client" \
  -I"$BWAPI/include" -I"$BWAPI/BWAPI/Source" -I"$SHARED" \
  -I"$SRC" -I"$BWAPI/BWAPI/Source/BWAPI" -I"$BWAPI/Util/Source" \
  pluto-bridge.cpp \
  "$SRC/GameImpl.cpp" "$SRC/UnitImpl.cpp" "$SRC/PlayerImpl.cpp" \
  "$SRC/BulletImpl.cpp" "$SRC/ForceImpl.cpp" "$SRC/RegionImpl.cpp" "$SRC/Client.cpp" \
  "$SHARED/UnitShared.cpp" "$SHARED/BulletShared.cpp" "$SHARED/ForceShared.cpp" \
  "$SHARED/GameShared.cpp" "$SHARED/PlayerShared.cpp" "$SHARED/RegionShared.cpp" \
  "$LIB/AIModule.cpp" "$LIB/Game.cpp" "$LIB/Player.cpp" "$LIB/Unit.cpp" \
  "$LIB/BulletType.cpp" "$LIB/DamageType.cpp" "$LIB/ExplosionType.cpp" \
  "$LIB/GameType.cpp" "$LIB/Order.cpp" "$LIB/PlayerType.cpp" "$LIB/Position.cpp" \
  "$LIB/Race.cpp" "$LIB/TechType.cpp" "$LIB/UnitCommandType.cpp" "$LIB/Unitset.cpp" \
  "$LIB/UnitSizeType.cpp" "$LIB/UnitType.cpp" "$LIB/UpgradeType.cpp" "$LIB/WeaponType.cpp" \
  "$LIB/Event.cpp" "$BWAPI/BWAPILIB/UnitCommand.cpp" "$LIB/Filters.cpp" \
  "$BWAPI/Util/Source/Util/Sha1.cpp" \
  -Wl,--image-base,0x800000 -Wl,--disable-dynamicbase \
  -Wl,--disable-reloc-section -Wl,--large-address-aware -Wl,--no-insert-timestamp \
  -o dump/pluto-bridge-linked.exe -lwinmm -lws2_32 -static-libgcc -static-libstdc++
"$OBJCOPY" --only-keep-debug dump/pluto-bridge-linked.exe dump/pluto-bridge.debug
"$OBJCOPY" --strip-debug dump/pluto-bridge-linked.exe dump/pluto-bridge-stripped.exe
python3 pluto_pe.py dump/pluto-bridge-stripped.exe pluto-bridge.exe
python3 verify_pe.py pluto-bridge.exe

# A local official DLL remains an optional validation input, never a build input
# copied into an artifact. Runtime verification in official-dll.h is unconditional.
PLUTO_DLL=${PLUTO_DLL:-$SCRIPT_DIR/pluto.dll}
if [ -f "$PLUTO_DLL" ]; then
  printf '%s  %s\n' \
    603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc "$PLUTO_DLL" \
    | sha256sum -c -
elif [ "${REQUIRE_OFFICIAL_PLUTO_DLL:-0}" = 1 ]; then
  echo "official pluto.dll is required but missing: $PLUTO_DLL" >&2
  exit 1
else
  echo "official pluto.dll not supplied; bridge runtime will verify it before loading"
fi
echo "OK -> pluto-bridge.exe (official pluto.dll is not bundled)"
