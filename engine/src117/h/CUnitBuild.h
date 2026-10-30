// CUnitBuild.h -- the free functions of src117/lang/CUnitBuild.cpp and the free callees in
// other TUs that its bodies reach. The CUnit methods of the build and rally block are
// declared in src117/h/CUnit.h with the rest of the class.

#ifndef OC117_CUNITBUILD_H
#define OC117_CUNITBUILD_H

#include "platform.h"
#include "point16.h"     // ours: point16_t, ClipToMap's first parameter
#include "collide.h"      // sub_4132B0, which collide.cpp owns

class CUnit;

// free functions defined in src117/lang/CUnitBuild.cpp

// 0x41A4B0 (141 B). Clamps a position to the playable map bounds, allowing for the unit
// type's own extents. A free function, not a member, so it is declared here and not in
// CUnit.h. __fastcall with both parameters in registers and a bare ret: the second is a
// WORD, since `movzx edi, dx` reads only DX, and the first points at two 16-bit members
// four bytes apart. The extents are a stride-8 units.dat table at 0x522C38 indexed by the
// type ({left@+0, up@+2, right@+4, down@+6}) against the map pixel size at 0x543230.
#include "pos.h"  // Hyperion: mpoint_t, a point of map positions
void ClipToMap(mpoint_t *pPos, WORD wType);
#if defined(_WIN64) || defined(__LP64__)
// Hyperion: 1.17's point, for the callers that still take it.
inline void ClipToMap(point16_t *pPos, WORD wType) {
  mpoint_t pt = *pPos;
  ClipToMap(&pt, wType);
  *pPos = pt;
}
#endif

// the free callees in other TUs that CUnit::Build, ReplaceGeyser and DispatchBuildSelf reach

// 0x47CD70 (8 B), TU placebox.cpp. The whole body is
// `movzx eax, byte ptr [0x6751E8] / ret`: a BOOL over a byte global, not a BYTE-returning
// accessor, since a BYTE return would emit `mov al` and leave the high half dirty.
BOOL is_placing_addon();

// 0x414580 (12 B), TU count.cpp. Both parameters are __fastcall registers (the body ends on
// a bare ret); it forwards to a three-argument helper with -1 wired in.
void count_remove_completion(CUnit *pUnit, int nDelta);

// 0x446650 (128 B), TU gamesnd.cpp. `mov edi, ecx` on entry, bare `ret`, one
// register argument, result never read.
void gamesnd_create_man(CUnit *pUnit);

// 0x41BA50 (95 B): ECX player, EDX ore, one stack slot of gas; 0 after the
// error sound when either balance is short.
BOOL build_check_money(int nPlayer, int nOre, int nGas);

// 0x41B910 (312 B), `ret 8h`: the ore, gas and step length of one repair step.
void sub_41B910(CUnit *pUnit, int *pnOre, int *pnGas, WORD *pwTime);

#endif  // OC117_CUNITBUILD_H
