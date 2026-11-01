#!/bin/sh
# Build fakehost.exe, the 32-bit host that runs pluto.dll against scripted state.
set -e
MINGW=/home/saintbellarmine/Downloads/llvm-mingw-20261020-msvcrt-ubuntu-22.04-x86_64
BWAPI=/home/saintbellarmine/Github/bwapi/bwapi
CXX="$MINGW/bin/i686-w64-mingw32-g++"
SRC="$BWAPI/BWAPIClient/Source"
LIB="$BWAPI/BWAPILIB/Source"
SHARED="$BWAPI/Shared"
mkdir -p dump

# NOTE on patched BWAPI headers (local copy of v4.4.0, for this rig only):
#  - include/BWAPI/Client/{UnitImpl,PlayerImpl,GameImpl}.h: include the full
#    interface class headers (<BWAPI/Unit.h> etc.) before inheriting from them;
#  - include/BWAPI/Client/CommandTemp.h: `class Command;` forward in BWAPI.

"$CXX" -std=c++17 -O1 -g -m32 \
  -I/home/saintbellarmine/Github/pluto/rig/shim \
  -I"$BWAPI/include" -I"$BWAPI/BWAPI/Source" -I"$SHARED" \
  -I"$SRC" -I"$BWAPI/BWAPI/Source/BWAPI" -I"$BWAPI/bwapi" -I"$BWAPI/Util/Source" \
  fakehost.cpp \
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
  -Wl,--image-base,0x20000000 -o fakehost.exe -lwinmm -lws2_32 -static-libgcc -static-libstdc++
echo "OK -> fakehost.exe"
