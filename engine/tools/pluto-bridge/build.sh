#!/bin/sh
# Build the official-DLL prototype with an image-backed StarCraft reservation.
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"
MINGW=${MINGW:-/home/saintbellarmine/Downloads/llvm-mingw-20261020-msvcrt-ubuntu-22.04-x86_64}
BWAPI=${BWAPI:-/home/saintbellarmine/Github/bwapi/bwapi}
PLUTO_SHIM=${PLUTO_SHIM:-/home/saintbellarmine/Github/pluto/rig/shim}
CXX="$MINGW/bin/i686-w64-mingw32-g++"
OBJCOPY="$MINGW/bin/llvm-objcopy"
SRC="$BWAPI/BWAPIClient/Source"
LIB="$BWAPI/BWAPILIB/Source"
SHARED="$BWAPI/Shared"
mkdir -p dump

# NOTE on patched BWAPI headers (local copy of v4.4.0, for this rig only):
#  - include/BWAPI/Client/{UnitImpl,PlayerImpl,GameImpl}.h: include the full
#    interface class headers (<BWAPI/Unit.h> etc.) before inheriting from them;
#  - include/BWAPI/Client/CommandTemp.h: `class Command;` forward in BWAPI.
#
# Host-provenance limitation: PLUTO_SHIM is an external include overlay and the
# BWAPI checkout carries the local header edits above.  They affect the host ABI
# and are NOT covered by the official pluto.dll digest or vendored by this build.
# Review/pin those dependencies separately when reproducing the host binary.

# Link fixed at 0x800000 with every real function/data item in its own input
# section.  Debug data is separated before pluto_pe.py adds .game/.hdr, because
# the full debug section table leaves no room in SizeOfHeaders.  The transform
# moves the final image base to 0x400000 without moving any linked virtual
# address, adds the RWX .game reservation at 0x401000..0x800000, and rewrites
# RVAs/imports/TLS/debug metadata.  LAA is required for the 0xF0B5D710 stack.
"$CXX" -std=c++17 -O1 -g -m32 -ffunction-sections -fdata-sections \
  -I"$PLUTO_SHIM" \
  -I"$BWAPI/include" -I"$BWAPI/BWAPI/Source" -I"$SHARED" \
  -I"$SRC" -I"$BWAPI/BWAPI/Source/BWAPI" -I"$BWAPI/bwapi" -I"$BWAPI/Util/Source" \
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
  -Wl,--disable-reloc-section -Wl,--large-address-aware \
  -o pluto-bridge-linked.exe -lwinmm -lws2_32 -static-libgcc -static-libstdc++
"$OBJCOPY" --only-keep-debug pluto-bridge-linked.exe dump/pluto-bridge.debug
"$OBJCOPY" --strip-debug pluto-bridge-linked.exe pluto-bridge-stripped.exe
python3 pluto_pe.py pluto-bridge-stripped.exe pluto-bridge.exe
python3 verify_pe.py pluto-bridge.exe
printf '%s  %s\n' \
  603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc pluto.dll \
  | sha256sum -c -
echo "OK -> pluto-bridge.exe (official pluto.dll only)"
