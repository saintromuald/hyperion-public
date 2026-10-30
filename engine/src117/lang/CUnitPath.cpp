// CUnitPath.cpp -- the unit path family: PathCalcMove's walk state machine, one handler per
// path state, the movement finishers they share, and the small path primitives.
// Bodies are in oracle VA order.
//
// The handlers reach each other as CUnit members declared in CUnit.h. PathFailedPath and
// PathAtMoveTarget carry __declspec(noinline) so PathCalcMove's dispatch keeps calling
// them instead of splicing them in, and MakeImmovable (56 B) is the other splice risk.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>          // offsetof for rect16.h's static_asserts
#include <intrin.h>           // _ReadWriteBarrier -- PathInit

#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/CFlingy.h"      // the CFlingy:: members these bodies call
#include "../h/rect16.h"       // ours: rect16_t -- the gUnitBoundBox row type
#include "../h/TrigAct.h"      // ours: int __fastcall net_rand(int)
#include "../h/CUnitTSpells.h" // ours: net_randbetween (PathStartPath)
#include "../h/sai_path.h"      // ours: sai_path_kill (gen must resolve it from OUR tree)
#include "../h/CUnitPathUtil.h" // ours: sub_42EBF0 (PathFixCollision)
#include "../h/CUnitFind.h"     // finish_unit_iteration (PathScoutPath)
#include "../gen/globals117.h"

// The globals this TU reaches, spelled in-TU with the dual form, one #ifndef per name: a
// plain extern here would leave the shipped form's baked slots unchecked.
#ifdef OC_DLL_MODE
#ifndef sgpResolveCollisionUnit_g
#define sgpResolveCollisionUnit_g (*reinterpret_cast<CUnit **>(OC_VA(0x69B57Cu)))
#endif
#ifndef dword_69B578
#define dword_69B578 (*reinterpret_cast<DWORD *>(OC_VA(0x69B578u)))
#endif
#ifndef sgbUpdateLos_g
#define sgbUpdateLos_g (*reinterpret_cast<DWORD *>(OC_VA(0x665B44u)))
#endif
#ifndef sgwStagedX_g
#define sgwStagedX_g (*reinterpret_cast<WORD *>(OC_VA(0x543280u)))
#endif
#ifndef sgwStagedY_g
#define sgwStagedY_g (*reinterpret_cast<WORD *>(OC_VA(0x543284u)))
#endif
#ifndef sgwOldX_g
#define sgwOldX_g (*reinterpret_cast<WORD *>(OC_VA(0x543288u)))
#endif
#ifndef sgwOldY_g
#define sgwOldY_g (*reinterpret_cast<WORD *>(OC_VA(0x54328Cu)))
#endif
#ifndef gwMapPixWidth
#define gwMapPixWidth (*reinterpret_cast<WORD *>(OC_VA(0x543230u)))
#endif
#ifndef gwMapPixHeight
#define gwMapPixHeight (*reinterpret_cast<WORD *>(OC_VA(0x543234u)))
#endif
#ifndef gUnitBoundBox
#define gUnitBoundBox (reinterpret_cast<rect16_t *>(OC_VA(0x522C38u)))
#endif
#ifndef gulUnitTypeFlags
#define gulUnitTypeFlags (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
#endif
#ifndef byte_6663EC
#define byte_6663EC (*reinterpret_cast<BYTE *>(OC_VA(0x6663ECu)))
#endif
#ifndef oc_dwReplayFrame
#define oc_dwReplayFrame (*reinterpret_cast<unsigned int *>(OC_VA(0x52651Cu)))
#endif
#ifndef sgnStagedDX_g
#define sgnStagedDX_g (*reinterpret_cast<LONG *>(OC_VA(0x543290u)))
#endif
#ifndef sgnStagedDY_g
#define sgnStagedDY_g (*reinterpret_cast<LONG *>(OC_VA(0x543294u)))
#endif
#ifndef sgnStagedSpeed_g
#define sgnStagedSpeed_g (*reinterpret_cast<LONG *>(OC_VA(0x543298u)))
#endif
#else
extern CUnit *sgpResolveCollisionUnit_g;   // 0x69B57C
extern DWORD dword_69B578;                 // 0x69B578
extern DWORD sgbUpdateLos_g;               // 0x665B44
extern upos_t sgwStagedX_g;                 // 0x543280
extern upos_t sgwStagedY_g;                 // 0x543284
extern upos_t sgwOldX_g;                    // 0x543288
extern upos_t sgwOldY_g;                    // 0x54328C
extern upos_t gwMapPixWidth;                // 0x543230
extern upos_t gwMapPixHeight;               // 0x543234
extern rect16_t gUnitBoundBox[228] OC_UNALIGNED;        // 0x522C38
extern DWORD gulUnitTypeFlags[228] OC_UNALIGNED;        // 0x521660
extern BYTE byte_6663EC;                   // 0x6663EC, registered with a null type
extern unsigned int oc_dwReplayFrame;      // 0x52651C, spelled as in game.cpp
extern LONG sgnStagedDX_g;                 // 0x543290
extern LONG sgnStagedDY_g;                 // 0x543294
extern LONG sgnStagedSpeed_g;              // 0x543298
#endif

// The unit's path object is sai_path (src117/h/sai_path.h): the TU that allocates it owns
// its layout. Its speed at +0x14 is a signed int -- PathSlidePrep halves it with
// cdq/sub/sar behind signed compares -- and bit 0 of the flag byte at +0x18 is the
// "path active" bit sub_42C790 sets and FixupPathState re-tests.

// Free-with-object spellings for callees whose oracle symbol is a plain free function
// (sai_path_kill) or a sub_*: under /Gr the object argument rides ECX and the call site is
// byte-identical. A callee with a named member symbol is called as a member instead.
void sai_path_kill(CUnit *pUnit);           // 0x48E880, sai_path.cpp
// CUnit::AtMoveTarget, inlined by the oracle into eight bodies here; the copy of it lives
// in CUnitBuild.cpp, which a WPO-off build cannot reach. Inlining it is what puts the
// unused [ebp-4] home slot in those frames. Keep the `? 2 : 1` and the WORD cast on the Y
// compare.
static __forceinline BOOL path_at_move_target(CUnit *pUnit)
{
    CSprite *pSprite = pUnit->pSprite;

    if (pUnit->wMoveTargetX == pSprite->wPosX
        && pUnit->wMoveTargetY == PosU(pSprite->wPosY))
        return (pUnit->dwFlags & 0x80000) ? 2 : 1;

    return 0;
}

// PathFaceTarget feeds the sprite X to AngleBetween after the test, so it has to
// stay in a register; taking the pixel by value makes the compare use it.
static __forceinline BOOL path_at_move_target_xy(CUnit *pUnit, upos_t wSpX, upos_t wSpY)
{
    if (pUnit->wMoveTargetX == wSpX && pUnit->wMoveTargetY == wSpY)
        return (pUnit->dwFlags & 0x80000) ? 2 : 1;

    return 0;
}

// PathAtRest needs only the sprite X kept, so the Y stays a load inside.
static __forceinline BOOL path_at_move_target_x(CUnit *pUnit, upos_t wSpX,
                                                CSprite *pSprite)
{
    if (pUnit->wMoveTargetX == wSpX
        && pUnit->wMoveTargetY == PosU(pSprite->wPosY))
        return (pUnit->dwFlags & 0x80000) ? 2 : 1;

    return 0;
}

// 0x42AD30 CUnit::PathCalcMove (530 B). The unit's walk state machine: run the handler for
// bPathState and keep stepping while it reports that the state changed. The goto keeps the
// dispatch at the loop head; a for(;;) rotates a second copy of it to the bottom.
void CUnit::PathCalcMove() {
    sgbUpdateLos_g = byte_6663EC != 0;
    BOOL bAgain;
top:
    switch (bPathState) {
    case 0x00: bAgain = PathInit(); break;
    case 0x01:
        if (dwFlags & 0x2000)
            return;
        bPathState = 0;
        goto top;
    case 0x03: bAgain = PathTurret(); break;
    case 0x04:
        sgbUpdateLos_g = 0;
    case 0x05:
        reinterpret_cast<CFlingy *>(this)->UpdateAcceleration();
        reinterpret_cast<CFlingy *>(this)->UpdateMovementFlags();
        reinterpret_cast<CFlingy *>(this)->BreakTurn();
        return;
    case 0x06:
        sgbUpdateLos_g = 0;
        return;
    case 0x07: bAgain = PathFlyer(); break;
    case 0x09: bAgain = PathAtRest(); break;
    case 0x0A: bAgain = PathDormant(); break;
    case 0x0B: bAgain = PathAtMoveTarget(); break;
    case 0x0C: bAgain = PathCheckIllegal(); break;
    case 0x0D: bAgain = PathMoveToLegal(); break;
    case 0x0E: bAgain = PathLumpWannabe(); break;
    case 0x0F: bAgain = PathFailedPath(); break;
    case 0x10: bAgain = PathRetryPath(); break;
    case 0x11: bAgain = PathStartPath(); break;
    case 0x12:
        if (FixupPathState(1))
            goto top;
        if (pPath->b19) {
            --pPath->b19;
            return;
        }
    case 0x1B:
        bPathState = 0x19;
        goto top;
    case 0x13: bAgain = PathTurnAndStart(); break;
    case 0x14: bAgain = PathFaceTarget(); break;
    case 0x15:
        pPath->bFlags &= ~1;
        if (FixupPathState(1))
            goto top;
        sai_path_kill(this);
        bPathState = 0x17;
        goto top;
    case 0x16:
        bPathState = PathFollow(moveTarget)
            ? 0x19 : 0x0F;
        return;
    case 0x17: bAgain = PathRepath(); break;
    case 0x18: bAgain = PathRepathMovers(); break;
    case 0x19: bAgain = PathFollowPath(); break;
    case 0x1A: bAgain = PathScoutPath(); break;
    case 0x1C: bAgain = PathFixCollision(); break;
    case 0x1D: bAgain = PathWaitFree(); break;
    case 0x1E: bAgain = PathGetFree(); break;
    case 0x1F: bAgain = PathSlidePrep(); break;
    case 0x20: bAgain = PathSlideFree(); break;
    case 0x21: bAgain = PathForceMoveFree(); break;
    case 0x22: bAgain = PathFixTerrain(); break;
    case 0x23: bAgain = PathTerrainSlide(); break;
    default:
        return;
    }
    if (bAgain)
        goto top;
}

// 0x42C790 (15 B). Raises the path's own active bit, if the unit has a path.
void CUnit::sub_42C790() {
    if (pPath)
        pPath->bFlags |= 1;
}

// 0x42AD10 (21 B). Sets bit 2 of the flag byte at +0x105 when bit 1 is set, and returns 0
// either way -- the jz lands on the shared `xor eax,eax`. The local is typed BYTE, which
// is what makes the load `mov al,[mem]` instead of a movzx; the upper half of EAX is left
// dirty on one exit.
int CUnit::sub_42AD10() {
    BYTE f = bPathingFlags;
    if (f & 2)
        bPathingFlags = f | 4;
    return 0;
}

// 0x42C7E0 CUnit::ResetMovementState (41 B). `setb al / xor al,[mem] / and al,1 /
// xor [mem],al` is MSVC assigning to a one-bit bitfield. Spelled as arithmetic it caches
// the byte in DL and the two memory operands become a load and a store. CUnit.h types
// bPathingFlags a plain BYTE, so the bit is reached through this overlay.
struct path_flags_bits {
    BYTE bLowGround : 1;
    BYTE _rest : 7;
};

void CUnit::ResetMovementState() {
    KillPath();
    CSprite *pSp = pSprite;
    bPathState = 0;
    reinterpret_cast<path_flags_bits *>(&bPathingFlags)->bLowGround =
        pSp->bElevationLevel < 0x0C;
}

// 0x42C810 CUnit::WalkInterrupt (46 B). Clears the moving bit and puts the sprite on
// animation 0x0C, unless the unit is not moving, its path sub-state is 2 or less, or
// either of the 0x3000 flags is set.
void CUnit::WalkInterrupt() {
    BYTE f = bMoveFlags;
    if ((f & 2) && bPathSubState > 2 && (dwFlags & 0x3000) == 0) {
        bMoveFlags = f & ~2;
        pSprite->SetAnimation(0x0C, 1);
    }
}

// 0x42B370 CUnit::PathFailedPath (57 B). Resets the retry counter to 0x0A, interrupts the
// walk animation on the same three conditions as WalkInterrupt, then leaves bPathState at
// 0x10 and returns FALSE.
__declspec(noinline) BOOL CUnit::PathFailedPath() {  // noinline: PathCalcMove calls it
    BYTE f = bMoveFlags;
    bPathSubState = 0x0A;
    if ((f & 2) && (dwFlags & 0x3000) == 0) {
        CSprite *pSp = pSprite;
        bMoveFlags = f & ~2;
        pSp->SetAnimation(0x0C, 1);
    }
    bPathState = 0x10;
    return FALSE;
}

// 0x42BF10 CUnit::PathRepath (79 B). Publishes the old path's unit in dword_69B57C, kills
// the path, and follows the move target again: bPathState becomes 0x19 when PathFollow
// succeeds and 0x0F when it does not. The return is always 1 (`lea eax,[edx-0Eh]`).
int CUnit::PathRepath() {
    if (pPath) {
        sgpResolveCollisionUnit_g = FromIdAlive(pPath->dwUnitId);
        sai_path_kill(this);
    }
    int ok = PathFollow(moveTarget);
    sgpResolveCollisionUnit_g = NULL;
    int nWalk = 0x0F;
    int st = 0x19;
    if (ok == 0)
        st = nWalk;
    bPathState = static_cast<BYTE>(st);
    return nWalk - 0x0E;
}

// 0x42B1B0 CUnit::PathCheckIllegal (137 B). Decides what an illegal spot means: a collision
// the resolver takes over leaves state 0x0D and 0, otherwise the state is 9 at the move
// target and 0x16 while there is still a way to go.
int CUnit::PathCheckIllegal() {
    dwFlags &= ~0x40000u;
    if (CheckResolveCollision()) {
        bPathingFlags |= 2;
        bPathState = 0x0D;
        return 0;
    }
    bPathingFlags &= ~6u;
    if (path_at_move_target(this) || (bMoveFlags & 4)) {
        bPathState = 0x09;
        return 1;
    }
    bPathState = 0x16;
    return 1;
}


// 0x42AC20 CUnit::FixupPathState (`ret 4h`, one int argument). The entry test every handler
// shares: it sets the next path state and returns whether the handler is done. Arrived or
// mid-collision leaves 0x0B; the 0x40000 and 0x100000 flags together kill the path and
// leave 0x0C; otherwise 0x16 without a path object, or 0x15 with a live one, and a state
// already past 0x1C comes back as 0x13. Two arms share the store, so each materialises its
// state into EAX first rather than storing the immediate.
BOOL CUnit::FixupPathState(int b) {
    int nState;
    BYTE cur;
    int nWalk;

    if (path_at_move_target(this) || (bMoveFlags & 4))
        goto state0B;
    if ((dwFlags & 0x40000) && (dwFlags & 0x100000)) {
        sai_path_kill(this);
        nState = 0x0C;
        goto store;
    }
    if (b && !pPath) {
        nState = 0x16;
        goto walkable;
    }
    if (!pPath)
        goto retfalse;
    if (!(pPath->bFlags & 1))
        goto retfalse;
    nState = 0x15;
walkable:
    cur = bPathState;
    if (cur < 0x1C)
        goto store;
    nWalk = 0x13;
    if (cur <= 0x23)
        nState = nWalk;
    bPathState = static_cast<BYTE>(nState);
    return nWalk - 0x12;
retfalse:
    return FALSE;
state0B:
    nState = 0x0B;
store:
    // Two arms share this store, so each materialises its state into EAX first
    // rather than storing the immediate.
    bPathState = static_cast<BYTE>(nState);
    return TRUE;
}

// 0x42C7A0 CUnit::MakeImmovable. Stops the unit where it stands: speed 0, Stop, the move
// target set to the sprite's own pixel, flag 0x80000 raised and bit 1 of +0x93 cleared.
// ForceSpeed(0) is the first statement, so its push precedes the esi save. The movsx pair
// is the (short) cast on wPosX, declared WORD, plus the short wPosY.
void CUnit::MakeImmovable() {
    reinterpret_cast<CFlingy *>(this)->ForceSpeed(0);
    Stop();
    CSprite *pSp = pSprite;
    SetMoveTarget_xy(PosS(pSp->wPosX), pSp->wPosY);
    dwFlags |= 0x80000u;
    bUnion93 &= ~2;
}

// 0x42AFE0 CUnit::PathAtMoveTarget. The unit has arrived: kill the path, snap the next
// waypoint onto the move target, and leave state 0x0C when the unit is still mid-move
// (flag 0x100000 and no bMoveFlags bit 2), 0x09 otherwise.
__declspec(noinline) BOOL CUnit::PathAtMoveTarget() {  // noinline: PathCalcMove calls it
    sai_path_kill(this);
    if (wNextWaypointX != wMoveTargetX || wNextWaypointY != wMoveTargetY)  // Hyperion: by field (stage E)
        nextWaypoint = moveTarget;
    if ((dwFlags & 0x100000) != 0 && (bMoveFlags & 4) == 0) {
        bPathState = 0x0C;
        return TRUE;
    }
    bPathState = 0x09;
    return TRUE;
}

// 0x42BFD0 CUnit::PathRetryPath. Counts up to 0x14 retries one frame at a time, then
// follows the move target again: state 0x19 when PathFollow succeeds, and MakeImmovable
// with state 0x0B when it does not. Both return-TRUE sites share one tail.
BOOL CUnit::PathRetryPath() {
    if (FixupPathState(0))
        return TRUE;
    if (bPathSubState < 0x14) {
        bPathSubState = bPathSubState + 1;
        return FALSE;
    }
    int ok = PathFollow(moveTarget);
    if (ok) {
        bPathState = 0x19;
        return TRUE;
    }
    MakeImmovable();
    bPathState = 0x0B;
    return FALSE;
}

// The unit box around (x, y), widened by pSelf's box and one pixel on each side.
static __forceinline void psp_box(mrect_t *out, CUnit *pUnit, CUnit *pSelf, spos_t x, spos_t y) {
  const rect16_t *pBox = &gUnitBoundBox[pUnit->wType];
  out->top = y - pBox->top;
  out->bottom = pBox->bottom + y;
  out->left = x - pBox->left;
  out->right = pBox->right + x;
  out->left += -1 - gUnitBoundBox[pSelf->wType].right;
  out->right += gUnitBoundBox[pSelf->wType].left + 1;
  out->top += -1 - gUnitBoundBox[pSelf->wType].bottom;
  out->bottom += gUnitBoundBox[pSelf->wType].top + 1;
}

// Counts the nearby units whose next step would block this unit and sets the path state from it.
// aBlockers is never read; its dead stores keep the oracle's 0x1A4 frame. Both loops walk by pointer.
// The list terminator after loop 2 gives the loop exits their own join, where `this` is reloaded.
BOOL CUnit::PathScoutPath() {
  int nBlocked = 0;
  CUnit *pSelf = this;
  if (FixupPathState(1))
    return TRUE;
  int nFound;
  CUnit **ppUnits = (CUnit **)sub_42E1C0((intptr_t)this, 0x20, 0x20, &nFound);
  if (nFound) {
    mpoint_t ptMine;  // Hyperion: map points
    if (sub_42E6C0(1, 0x1E, &ptMine)) {
      CUnit *aBlockers[32];
      int aOk[32];
      mpoint_t aPt[32];
      int *pOk = aOk;
      mpoint_t *pPt = aPt;
      for (CUnit **pp = ppUnits; *pp; ++pp, ++pOk, ++pPt)
        *pOk = (*pp)->sub_42E6C0(1, 0x1E, pPt);
      CUnit *pUnit;
      pOk = aOk;
      pPt = aPt;
      for (CUnit **pp = ppUnits; (pUnit = *pp) != NULL; ++pp, ++pOk, ++pPt) {
        if (!*pOk)
          continue;
        mrect_t r;
        psp_box(&r, pUnit, pSelf, pPt->x, pPt->y);
        if (ptMine.x >= r.left && ptMine.x <= r.right
            && ptMine.y >= r.top && ptMine.y <= r.bottom)
          aBlockers[nBlocked++] = pUnit;
      }
      aBlockers[nBlocked] = pUnit;
    }
  }
  finish_unit_iteration(ppUnits);
  pSelf->bPathState = nBlocked > 0 ? 0x1B : 0x19;
  return TRUE;
}

// 0x42BE10 CUnit::PathLumpWannabe. Steps the unit on unless it has arrived, in which case
// the state becomes 2. All four arms of the disjunction branch to the same move block, and
// the fall-through is the single `bPathState = 2; return TRUE;` exit.
//
// The flag term is the DispatchAttackMove ternary, negated: the oracle's jz lands on the
// true continuation, and the `? 2 : 1` is what defeats the single-bit shr/and peephole.
BOOL CUnit::PathLumpWannabe() {
    if (!path_at_move_target(this) || (bMoveFlags & 2)) {
        reinterpret_cast<CFlingy *>(this)->ProgressMovement();
        FinishUnitMovementOrStop();
        return FALSE;
    }
    bPathState = 2;
    return TRUE;
}

// 0x42BF60 CUnit::PathRepathMovers. Kills the path and follows the move target again with
// dword_69B578 set, which tells the search that movers may be stepped over; state 0x14 on
// success, or the saved move target and speed slot restored and state 0x13. The call
// re-reads the move-target field rather than the register it was just read into, because
// the dword_69B578 store may alias it.
BOOL CUnit::PathRepathMovers() {
    sai_path_kill(this);
    mpoint_t ptMoveTargetSave = moveTarget;  // Hyperion: the point, not its DWORD (stage E)
    DWORD dwSlot14Save = *reinterpret_cast<DWORD *>(_unmapped_14);
    dword_69B578 = 1;
    int ok = PathFollow(moveTarget);
    dword_69B578 = 0;
    if (ok && (dwFlags & 0x80000) == 0) {
        bPathState = 0x14;
        return TRUE;
    }
    moveTarget = ptMoveTargetSave;
    *reinterpret_cast<DWORD *>(_unmapped_14) = dwSlot14Save;
    bPathState = 0x13;
    return FALSE;
}

// 0x42C610 CUnit::PathTurret. A turret's step: clear the line-of-sight flag, re-aim at the
// sprite's own pixel, then rotate -- the flags alone while flag 0x2000000 is set, and
// acceleration, flags and a turn break otherwise. Both exits return 0, so the header
// declares BOOL.
BOOL CUnit::PathTurret() {
    sgbUpdateLos_g = 0;
    CSprite *pSp = pSprite;
    SetMoveTarget_xy(PosS(pSp->wPosX), pSp->wPosY);
    reinterpret_cast<CFlingy *>(this)->NeedsRotation();
    if (dwFlags & 0x2000000) {
        reinterpret_cast<CFlingy *>(this)->UpdateMovementFlags();
        return FALSE;
    }
    reinterpret_cast<CFlingy *>(this)->UpdateAcceleration();
    reinterpret_cast<CFlingy *>(this)->UpdateMovementFlags();
    reinterpret_cast<CFlingy *>(this)->BreakTurn();
    return FALSE;
}

// 0x42AB10 CUnit::FinishUnitMovement (152 B). Ends the step: sort the staged move's
// coordinates and raise the line-of-sight flag when the staged pixel crossed a 32-pixel
// tile boundary. The divide by 32 is MSVC's signed `(v + ((v>>31)&1Fh)) >> 5`.
BOOL CUnit::FinishUnitMovement() {
    if (reinterpret_cast<CFlingy *>(this)->BreakMovement()) {
        CoordSort(PosS(sgwStagedX_g) - PosS(sgwOldX_g),
                  PosS(sgwStagedY_g) - PosS(sgwOldY_g));
        if (PosS(sgwOldX_g) / 32 != PosS(sgwStagedX_g) / 32 ||
            PosS(sgwOldY_g) / 32 != PosS(sgwStagedY_g) / 32)
            sgbUpdateLos_g = 1;
        return TRUE;
    }
    return FALSE;
}

// 0x42ABB0 CUnit::FinishUnitMovementOrStop (102 B). The same end of step, unless a ground
// unit or the terrain is in the way: then the unit breaks to idle, asks
// IsWithinTargetRange for its side effect and discards the result, and stops on its own
// pixel.
BOOL CUnit::FinishUnitMovementOrStop() {
    if (GetGroundCollisionUnit() || GetCollisionWithTerrain()) {
        reinterpret_cast<CFlingy *>(this)->BreakToIdle();
        CUnit *pMoveTgt = *reinterpret_cast<CUnit *const *>(_unmapped_14);
        if (pMoveTgt)
            IsWithinTargetRange(1, pMoveTgt);
        reinterpret_cast<CFlingy *>(this)->ForceSpeed(0);
        CSprite *pSp = pSprite;
        reinterpret_cast<CFlingy *>(this)->SetTarget_xy(PosS(pSp->wPosX),
                                                        pSp->wPosY);
        Stop();
        return FALSE;
    }
    return FinishUnitMovement() != 0;
}

// 0x42B240 CUnit::PathDormant (109 B). An idle unit stays idle only while it is at its move
// target and on its destination pixel; anything else puts it back in state 9.
BOOL CUnit::PathDormant() {
    DWORD f = dwFlags;
    if ((f & 0x40000) && (f & 0x100000))
        goto fail;
    if (path_at_move_target(this) &&
        PosU(wDestinationX) == wPositionX &&
        PosU(wDestinationY) == wPositionY)
        return FALSE;
fail:
    bPathState = 9;
    return TRUE;
}

// 0x42BB00 CUnit::PathForceMoveFree (121 B). Counts the sub-state up to 0xFF; once the unit
// is at its move target or mid-collision the state becomes 0x0B, otherwise it is stopped,
// its path killed and the state set to 0x13. The ternary is the last arm of an && chain,
// not an || disjunct: the short-circuit join un-tracks its zero, so ForceSpeed(0)
// materialises `push 0h` instead of reusing EAX, and the two position guards jump to the
// test rather than to the fail block.
BOOL CUnit::PathForceMoveFree() {
    if (bPathSubState < 0xFF)
        ++bPathSubState;
    if (path_at_move_target(this) || (bMoveFlags & 4)) {
        bPathState = 0x0B;
        return FALSE;
    }
    reinterpret_cast<CFlingy *>(this)->ForceSpeed(0);
    sai_path_kill(this);
    bPathState = 0x13;
    return TRUE;
}

// 0x42C390 CUnit::PathSlidePrep (101 B). Prepares a slide: halve the path's speed above
// 0x200 and floor it at 0x100, copy it into both flingy speed slots, take the direction
// over, slide forward and leave state 0x20.
BOOL CUnit::PathSlidePrep() {
    BOOL b = FixupPathState(1);
    if (b)
        return TRUE;
    if (static_cast<BYTE>(b) == *(_unmapped_24 + 3)) {
        int v = pPath->nSpeed;
        if (v >= 0x200)
            pPath->nSpeed = v / 2;
        else if (v > 0x100)
            pPath->nSpeed = 0x100;
    }
    // One load feeding both stores, spelled as a local; read twice, MSVC reloads the
    // speed for the second store.
    DWORD dwSlide = pPath->nSpeed;
    *reinterpret_cast<DWORD *>(_unmapped_2C + 0x0C) = dwSlide;
    *reinterpret_cast<DWORD *>(_unmapped_2C + 0x10) = dwSlide;
    *(_unmapped_2C + 0x1E) = bDirection;
    reinterpret_cast<CFlingy *>(this)->SlideForward();
    bPathState = 0x20;
    return TRUE;
}

// 0x42C400 CUnit::PathStartPath (99 B). Starts following the move target: state 0x0F when
// PathFollow refuses, otherwise 0x19, or 0x12 with a fresh net_randbetween jitter in the
// path when bit 1 of +0x93 was set and the sub-state byte is 2.
BOOL CUnit::PathStartPath() {
    BOOL ok = PathFollow(moveTarget);
    if (ok == 0) {
        bPathState = 0x0F;
        return ok;
    }
    BYTE f = bUnion93;
    if (!(f & 2))
        goto state19;
    bUnion93 = f & ~2;
    if (*(_unmapped_24 + 3) != 2)
        goto state19;
    pPath->b19 = static_cast<BYTE>(net_randbetween(0, 2, 0x33));
    bPathState = 0x12;
    return TRUE;
state19:
    bPathState = 0x19;
    return TRUE;
}

// 0x42AA60 CUnit::MoveInBounds (166 B). Pulls the move target back inside the map by the
// unit type's own bound box and re-aims the flingy at it, raising flag 0x80000. The low
// clamps are cmovl and the high ones cmovg, and a move target already on the sprite's own
// pixel returns before any of it.
void CUnit::MoveInBounds() {
    int x = PosS(wMoveTargetX);
    int y = PosS(wMoveTargetY);
    CSprite *pSp = pSprite;
    if (x == PosS(pSp->wPosX) && y == PosS(pSp->wPosY))
        return;
    const rect16_t &box = gUnitBoundBox[wType];
    int nMaxX = gwMapPixWidth - box.right - 1;
    int nMinX = box.left;
    int nMinY = box.top;
    int nMaxY = gwMapPixHeight - box.bottom - 1;
    int nX = PosS(wMoveTargetX);
    nX = nX < nMinX ? nMinX : nX;
    int nY = PosS(wMoveTargetY);
    nY = nY < nMinY ? nMinY : nY;
    if (nX > nMaxX) nX = nMaxX;
    if (nY > nMaxY) nY = nMaxY;
    if (nX != x || nY != y) {
        reinterpret_cast<CFlingy *>(this)->SetTarget_xy(nX, nY);
        dwFlags |= 0x80000u;
    }
}

// 0x42B2B0 CUnit::PathFaceTarget (191 B). Turns the unit toward its move target before it
// steps. The sprite X is bound to a reference and copied to a short for the angle call; a
// WORD local puts it in EDI for the whole body and costs a byte.
BOOL CUnit::PathFaceTarget() {
    if (FixupPathState(1))
        goto rettrue;
    {
        CSprite *pSp = pSprite;
        const upos_t &wSpX = pSp->wPosX;
        if (path_at_move_target_xy(this, wSpX, PosU(pSp->wPosY)) ||
            (bMoveFlags & 4))
            goto state0B;
        BYTE bDir = bDirection;
        upos_t wSpY = pSp->wPosY;  // WORD intermediate: movzx then movsx at the push
        spos_t sSpX = wSpX;
        if (bDir != AngleBetween(sSpX,
                                 PosS(wSpY),
                                 PosS(wNextWaypointX),
                                 PosS(wNextWaypointY))) {
            reinterpret_cast<CFlingy *>(this)->ProgressMovement();
            reinterpret_cast<CFlingy *>(this)->BreakTurn();
            return FALSE;
        }
        if (pPath->bFlags & 1) {
            bPathState = 0x15;
            return TRUE;
        }
        bPathState = 0x19;
        return TRUE;
    }
state0B:
    bPathState = 0x0B;
rettrue:
    return TRUE;
}

// 0x42B700 CUnit::PathFixTerrain (233 B). Both collision tests false -> FinishUnitMovement,
// bMoveFlags |= 1, state 0x19, FALSE. Else the walk-interrupt animation gate (all three
// failures jump to the angle block at 0x42b797), then the angle compare: equal ->
// sub_42DB60() into pPath->bDirection, state 0x23, FALSE; differ -> BreakTurn, FALSE.
BOOL CUnit::PathFixTerrain() {
    if (bPathSubState < 0xFF)
        ++bPathSubState;
    if (FixupPathState(1))
        return TRUE;
    reinterpret_cast<CFlingy *>(this)->ForceSpeed(pPath->nSpeed);
    reinterpret_cast<CFlingy *>(this)->ProgressMovement();
    if (!GetCollisionWithTerrain() && !GetCollisionUnit()) {
        FinishUnitMovement();
        bMoveFlags |= 1;
        bPathState = 0x19;
        return FALSE;
    }
    BYTE f = bMoveFlags;
    if ((f & 2) && bPathSubState > 2 && (dwFlags & 0x3000) == 0) {
        CSprite *pSp = pSprite;
        bMoveFlags = f & ~2;
        pSp->SetAnimation(0x0C, 1);
    }
    CSprite *pSpA = pSprite;
    BYTE bDir = bDirection;
    upos_t wSpY = pSpA->wPosY;   // WORD intermediates: movzx then movsx at the push
    upos_t wSpX = pSpA->wPosX;
    if (bDir != AngleBetween(PosS(wSpX),
                             PosS(wSpY),
                             PosS(wNextWaypointX),
                             PosS(wNextWaypointY))) {
        reinterpret_cast<CFlingy *>(this)->BreakTurn();
        return FALSE;
    }
    pPath->bDirection = sub_42DB60();
    bPathState = 0x23;
    return FALSE;
}

// 0x42C470 CUnit::PathTerrainSlide (247 B). No loop: GetCollisionUnit() or
// GetCollisionWithTerrain() -> state 0x21, FALSE. Else FinishUnitMovement, save the unit's
// +0x3C speed, force the path's own +0x14 speed, face the AngleBetween result, force the
// saved speed back, and reach state 0x19 with bMoveFlags |= 1 only when the last terrain
// test is clear. `this` lives in [ebp-4] across the second half.
BOOL CUnit::PathTerrainSlide() {
    CUnit *pSelf = this;
    if (bPathSubState < 0xFF)
        ++bPathSubState;
    if (FixupPathState(1))
        return TRUE;
    bMoveFlags &= ~1;
    BYTE bSave = bDirection;
    reinterpret_cast<CFlingy *>(this)->ProgressMovement_dir(pPath->bDirection);
    bMoveFlags |= 1;
    bDirection = bSave;
    _unmapped_2C[0x4B - 0x2C] = pPath->bDirection;  // unit +0x4B
    reinterpret_cast<CFlingy *>(this)->UpdateVelocity();
    if (GetCollisionUnit()) {
        bPathState = 0x21;
        return FALSE;
    }
    if (GetCollisionWithTerrain()) {
        bPathState = 0x21;
        return FALSE;
    }
    FinishUnitMovement();
    DWORD dwSavedSpeed = *reinterpret_cast<DWORD *>(&_unmapped_2C[0x3C - 0x2C]);
    reinterpret_cast<CFlingy *>(this)->ForceSpeed(pPath->nSpeed);
    CSprite *pSp = pSprite;
    upos_t wSpY = pSp->wPosY;    // WORD intermediates: movzx then movsx at the push
    upos_t wSpX = pSp->wPosX;
    reinterpret_cast<CFlingy *>(pSelf)->ProgressMovement_dir(
        AngleBetween(PosS(wSpX),
                     PosS(wSpY),
                     PosS(pSelf->wNextWaypointX),
                     PosS(pSelf->wNextWaypointY)));
    reinterpret_cast<CFlingy *>(pSelf)->ForceSpeed(dwSavedSpeed);
    if (!GetCollisionWithTerrain()) {
        bMoveFlags |= 1;
        bPathState = 0x19;
    }
    return FALSE;
}

// 0x42C670 CUnit::PathWaitFree (277 B). No unit and no terrain collision ->
// FinishUnitMovement, state 0x19, FALSE; terrain only -> 0x17, FALSE. A unit collision goes
// through the animation gate, ForceSpeed(0) and the angle compare (BreakTurn when it
// differs), then while bPathSubState >= 0x19: net_rand(0x33) < 0x3FFF (an unsigned jae) ->
// 0x21, else ForceSpeed(0), 0x18 and the shared FALSE tail.
BOOL CUnit::PathWaitFree() {
    if (bPathSubState < 0xFF)
        ++bPathSubState;
    if (FixupPathState(1))
        return TRUE;
    reinterpret_cast<CFlingy *>(this)->ForceSpeed(pPath->nSpeed);
    reinterpret_cast<CFlingy *>(this)->ProgressMovement();
    if (!GetCollisionUnit()) {
        if (GetCollisionWithTerrain()) {
            bPathState = 0x17;
            return FALSE;
        }
        FinishUnitMovement();
        bPathState = 0x19;
        return FALSE;
    }
    BYTE f = bMoveFlags;
    if ((f & 2) && bPathSubState > 2 && (dwFlags & 0x3000) == 0) {
        CSprite *pSp = pSprite;
        bMoveFlags = f & ~2;
        pSp->SetAnimation(0x0C, 1);
    }
    reinterpret_cast<CFlingy *>(this)->ForceSpeed(0);
    CSprite *pSpA = pSprite;
    BYTE bDir = bDirection;
    upos_t wSpY = pSpA->wPosY;   // WORD intermediates: movzx then movsx at the push
    upos_t wSpX = pSpA->wPosX;
    if (bDir != AngleBetween(PosS(wSpX),
                             PosS(wSpY),
                             PosS(wNextWaypointX),
                             PosS(wNextWaypointY)))
        reinterpret_cast<CFlingy *>(this)->BreakTurn();
    if (bPathSubState >= 0x19) {
        if (static_cast<DWORD>(net_rand(0x33)) < 0x3FFFu) {
            bPathState = 0x21;
            return FALSE;
        }
        reinterpret_cast<CFlingy *>(this)->ForceSpeed(0);
        bPathState = 0x18;
    }
    return FALSE;
}

// 0x42BB80 CUnit::PathGetFree (313 B). Walks one step while stuck against another unit,
// then re-seats the path on it. The [ebp-4] slot is the colliding unit, live across three
// calls.
BOOL CUnit::PathGetFree() {
    if (bPathSubState < 0xFF)
        ++bPathSubState;
    if (FixupPathState(1))
        return TRUE;
    reinterpret_cast<CFlingy *>(this)->ForceSpeed(pPath->nSpeed);
    reinterpret_cast<CFlingy *>(this)->ProgressMovement();
    CUnit *pHit = GetCollisionUnit();
    if (!pHit) {
        if (GetCollisionWithTerrain()) {
            bPathState = 0x17;
            return FALSE;
        }
        FinishUnitMovement();
        bPathState = 0x19;
        return FALSE;
    }
    BYTE bFlags = bMoveFlags;
    if ((bFlags & 2) && bPathSubState > 2 && !(dwFlags & 0x3000)) {
        bMoveFlags = bFlags & ~2u;
        pSprite->SetAnimation(0x0C, 1);
    }
    CSprite *pSp = pSprite;
    BYTE bDir = bDirection;
    upos_t wSpY = pSp->wPosY;
    upos_t wSpX = pSp->wPosX;
    if (bDir != AngleBetween(PosS(wSpX),
                             PosS(wSpY),
                             PosS(wNextWaypointX),
                             PosS(wNextWaypointY))) {
        reinterpret_cast<CFlingy *>(this)->BreakTurn();
        return FALSE;
    }
    sai_path_kill(this);
    int nDir = sub_42DC70(pHit);
    if (nDir < 0) {
        bPathState = 0x18;
        return FALSE;
    }
    pPath->dwUnitId = pHit->GetUnitID();
    pPath->bDirection = static_cast<BYTE>(nDir);
    bPathState = 0x1F;
    return FALSE;
}

// 0x42C570 CUnit::PathTurnAndStart (158 B). PathRepath's sibling: hand the old path's unit
// to the collision resolver, then start following the move target again.
BOOL CUnit::PathTurnAndStart() {
    if (pPath) {
        sgpResolveCollisionUnit_g = FromIdAlive(pPath->dwUnitId);
        sai_path_kill(this);
    }
    if (path_at_move_target(this) || (bMoveFlags & 4))
        goto state0B;
    {
        int ok = PathFollow(moveTarget);
        sgpResolveCollisionUnit_g = NULL;
        if (!ok) {
            bPathState = 0x0F;
            return TRUE;
        }
        bPathState = 0x14;
        return FALSE;
    }
state0B:
    bPathState = 0x0B;
    return TRUE;
}

// 0x42BCC0 CUnit::PathInit (333 B).
// Picks the state a unit enters the walk machine in. Keep the high bit spelled
// as an expression: naming it in a local costs the whole EBP frame.
BOOL CUnit::PathInit() {
    DWORD f;

    // Four WORD stores give the oracle's `xor eax, eax` and two dword
    // stores; two DWORD stores give immediates and a threaded `||` join.
    rect16_t *prcSearch = reinterpret_cast<rect16_t *>(_unmapped_106 + 2);
    prcSearch->left = 0;
    prcSearch->top = 0;
    prcSearch->right = 0;
    prcSearch->bottom = 0;
    bPathingFlags = static_cast<BYTE>((bPathingFlags & 0xFC) |
                                      ((pSprite->bElevationLevel < 0x0C) & 1));
    if (!(gulUnitTypeFlags[wType] & 0x10) && (dwFlags & 0x2000)) {
        bPathState = 1;
        return TRUE;
    }
    if (IsDead())
        goto state2;
    f = dwFlags;
    if (f & 0x20) {
        bPathState = 4;
        return TRUE;
    }
    if (pSprite->flags.b & 0x20) {
        if ((bMoveFlags & 2) || !AtMoveTarget()) {
            MakeImmovable();
            reinterpret_cast<CFlingy *>(this)->ProgressMovement();
            FinishUnitMovement();
        }
        bPathState = 6;
        return TRUE;
    }
    if (f & 0x10)
        goto state2;
    if (((f >> 17) & 1) || (f & 0x10000))
        goto highlow;
    if ((bPathingFlags & 1) && ((bMoveFlags & 2) || !AtMoveTarget())) {
        bPathState = 0x0E;
        return TRUE;
    }
    // The barrier keeps the merged state-2 block ahead of the high/low tails.
    _ReadWriteBarrier();
state2:
    bPathState = 2;
    return TRUE;
highlow:
    if (!((f >> 17) & 1))
        bPathState = static_cast<BYTE>((gulUnitTypeFlags[wType] & 0x10) ? 3 : 5);
    else
        bPathState = static_cast<BYTE>((bPathingFlags & 1) ? 9 : 7);
    return TRUE;
}

// 0x42B030 CUnit::PathAtRest (376 B).
// The idle state: age the sub-state counter, decide whether the unit still has to move,
// then settle on the sprite's own pixel or wait. bAt's two dead stores count in the frame
// packer and put the spilled sprite X at [ebp-8]. The pair under bStill, which the tail
// tests again, and the sprite Y read into its own local are what set the registers.
BOOL CUnit::PathAtRest() {
    BOOL bStill;
    BYTE bFlags;
    BYTE bAngle;
    DWORD f;
    int nSpX;
    int wSpX;
    upos_t wTargetX;  // Hyperion: a position
    BOOL bAt;

    bAt = path_at_move_target(this);
    if (bAt)
        bPathSubState = 0;
    else if (bPathSubState > 2)
        bPathSubState = 2;
    else if (bPathSubState != 0)
        --bPathSubState;

    bFlags = bMoveFlags;
    bStill = TRUE;
    if (bFlags & 4)
        goto moving;
    nSpX = pSprite->wPosX;
    bAt = path_at_move_target_x(this, PosU(nSpX), pSprite);
    if (!bAt)
        goto after;
    if (bFlags & 2)
        goto moving;
    wTargetX = PosU(wDestinationX);
    if (wTargetX == wPositionX &&
        PosU(wDestinationY) == wPositionY)
        goto after;
    if (bStill)
        wSpX = static_cast<WORD>(nSpX);
    else
        wSpX = static_cast<WORD>(nSpX);
    upos_t wSpY;  // Hyperion: set apart from its declaration, which a goto jumps past
    wSpY = PosU(pSprite->wPosY);
    bAngle = static_cast<BYTE>(AngleBetween(
        PosS(wSpX),
        PosS(wSpY),
        PosS(wTargetX),
        PosS(PosU(wDestinationY))));
    if (bAngle == bUnion21 && bAngle == bDirection)
        goto after;
moving:
    bStill = FALSE;
    reinterpret_cast<CFlingy *>(this)->ProgressMovement();
    FinishUnitMovementOrStop();
after:
    f = dwFlags;
    if ((f & 0x40000) && (f & 0x100000)) {
        bPathState = 0x0C;
        return FALSE;
    }
    if (!path_at_move_target(this) && !(bMoveFlags & 4)) {
        bPathState = 0x11;
        return TRUE;
    }
    if (bStill) {
        reinterpret_cast<CFlingy *>(this)->ForceSpeed(0);
        CSprite *pSp = pSprite;
        reinterpret_cast<CFlingy *>(this)->SetDestination(
            PosS(pSp->wPosX), PosS(pSp->wPosY));
        bPathState = 0x0A;
    }
    return FALSE;
}

// 0x42BE70 CUnit::PathMoveToLegal (151 B). Re-seats the unit on the path's own waypoint, or
// gives the path up when the collision resolver says the spot is taken.
int CUnit::PathMoveToLegal() {
    sai_path *p = pPath;
    if (!p) {
        bPathState = 0x0C;
        return 1;
    }
    BYTE f = bPathingFlags;
    if (f & 4) {
        dwFlags |= 0x40000u;
        bPathingFlags = f & ~4u;
        sai_path_kill(this);
        bPathState = 9;
        return 1;
    }
    if ((p->bFlags & 1) ||
        p->dwWaypoint != PosPack(wNextWaypointX, wNextWaypointY)) {  // Hyperion: by field (stage E)
        wNextWaypointX = PosPackedX(p->dwWaypoint);
        wNextWaypointY = PosPackedY(p->dwWaypoint);
    }
    int nFree = sub_42D830();
    if (!nFree)
        return nFree;
    if (pPath)
        sai_path_kill(this);
    UpdateUnitCollision();
    dwFlags |= 0x40000u;
    bPathState = 0x0C;
    return 0;
}

// 0x42B7F0 CUnit::PathFlyer (211 B). An air unit's step: move, then shove the staged pixel
// back inside the map by its own bound box. Each clamp is written as the overshoot added
// back to the global itself; a WORD local for the coordinate costs an ebp frame.
// Hyperion: 1.17 takes the overshoot back as a WORD, `-= (WORD)nOver`, which only
// adds -nOver modulo 65,536; it is taken back as itself, the same in 16 bits and right in
// 31.
BOOL CUnit::PathFlyer() {
    MoveInBounds();
    reinterpret_cast<CFlingy *>(this)->ProgressMovement();
    int nRepulse = ProgressRepulse();
    int nOver;

    nOver = PosS(sgwStagedX_g) - gUnitBoundBox[wType].left;
    if (nOver < 0)
        sgwStagedX_g -= nOver;
    nOver = gUnitBoundBox[wType].right + PosS(sgwStagedX_g);
    if (nOver >= gwMapPixWidth)
        sgwStagedX_g += PosU(gwMapPixWidth - nOver - 1);
    nOver = PosS(sgwStagedY_g) - gUnitBoundBox[wType].top;
    if (nOver < 0)
        sgwStagedY_g -= nOver;
    nOver = gUnitBoundBox[wType].bottom + PosS(sgwStagedY_g);
    if (nOver >= gwMapPixHeight)
        sgwStagedY_g += PosU(gwMapPixHeight - nOver - 1);
    FinishUnitMovement();
    FinishRepulse(nRepulse);
    return FALSE;
}

// 0x42C1E0 CUnit::PathSlideFree (428 B). Slides the unit along the path's direction a speed
// step at a time until it either hits something or stops touching the unit its path was
// blocked by.
int CUnit::PathSlideFree() {
    CUnit *pHit = NULL;
    int nState = 0x20;
    if (bPathSubState < 0xFF)
        ++bPathSubState;
    if (FixupPathState(1))
        return 1;
    CUnit *pBlocker = FromIdAlive(pPath->dwUnitId);
    bMoveFlags |= 1;
    _unmapped_4A[1] = pPath->bDirection;
    reinterpret_cast<CFlingy *>(this)->UpdateVelocity();
    bDirection = _unmapped_4A[0];
    reinterpret_cast<CFlingy *>(this)->UpdateRotation();
    pSprite->SetFacing(bUnion21);
    if (_unmapped_24[3] != 2)
        reinterpret_cast<CFlingy *>(this)->UpdateSpeed();
    else
        *reinterpret_cast<LONG *>(_unmapped_38) = lTopSpeed;
    LONG lSpeed = *reinterpret_cast<LONG *>(_unmapped_38);
    LONG lLeft = *reinterpret_cast<LONG *>(_unmapped_38);
    *reinterpret_cast<LONG *>(_unmapped_38 + 4) = lSpeed;
    BYTE bSave = bDirection;
    while (lLeft > 0) {
        LONG lStep = lLeft;
        lLeft -= 0x100;
        if (lStep > 0x100)
            lStep = 0x100;
        bMoveFlags &= ~1;
        reinterpret_cast<CFlingy *>(this)->ProgressMovement_dir_speed(pPath->bDirection, lStep);
        if (GetCollisionUnit() || GetCollisionWithTerrain()) {
            nState = 0x21;
            break;
        }
        FinishUnitMovement();
        bDirection = bSave;
        reinterpret_cast<CFlingy *>(this)->UpdateAcceleration();
        reinterpret_cast<CFlingy *>(this)->ProgressMovement_dir_speed(_unmapped_4A[0], lSpeed);
        pHit = GetCollisionUnit();
        if (pHit != pBlocker)
            break;
    }
    *reinterpret_cast<LONG *>(_unmapped_38) = lSpeed;
    *reinterpret_cast<LONG *>(_unmapped_38 + 4) = lSpeed;
    _unmapped_4A[0] = bSave;
    bDirection = bSave;
    reinterpret_cast<CFlingy *>(this)->SlideForward();
    if (nState != 0x20) {
        bPathState = static_cast<BYTE>(nState);
        return 0;
    }
    if (pHit)
        return 0;
    bMoveFlags |= 1;
    sai_path *p = pPath;
    if (p && oc_dwReplayFrame - p->dwFrame < 0x96) {
        bPathState = 0x19;
        return 0;
    }
    bPathState = 0x13;
    return 0;
}

// 0x42B8D0 CUnit::PathFollowPath (558 B). One step along the path. A ground unit in the way
// is retried at half and then a quarter of the step before the path is handed to the
// collision states.
BOOL CUnit::PathFollowPath() {
    if (FixupPathState(1))
        return TRUE;
    if (!PathFollow(moveTarget)) {
        bPathState = 0x16;
        return TRUE;
    }
    CFlingy *pFl = reinterpret_cast<CFlingy *>(this);
    LONG lSpeed = pFl->lTargetSpeed;
    pFl->ProgressMovement();
    if (pFl->bMovementType == 0)
        lSpeed = pFl->lCurrentSpeed;
    CUnit *pHit = GetGroundCollisionUnit();
    BOOL bTerrain = GetCollisionWithTerrain();
    if (pHit) {
        if (!bTerrain && (pFl->lVelocityX >= 0x20 || pFl->lVelocityY >= 0x20)
            && sgnStagedSpeed_g == pFl->lCurrentSpeed) {
            LONG lDX = sgnStagedDX_g;
            LONG lDY = sgnStagedDY_g;
            upos_t wX = sgwStagedX_g;
            upos_t wY = sgwStagedY_g;
            LONG lX, lY;
            lX = pFl->lVelocityX / 2 + pFl->lPositionX256;
            lY = pFl->lVelocityY / 2 + pFl->lPositionY256;
            sgnStagedDX_g = lX;
            sgnStagedDY_g = lY;
            sgwStagedX_g = static_cast<WORD>(lX >> 8);
            sgwStagedY_g = static_cast<WORD>(lY >> 8);
            if (!GetGroundCollisionUnit())
                goto moved;
            lX = pFl->lVelocityX / 4 + pFl->lPositionX256;
            lY = pFl->lVelocityY / 4 + pFl->lPositionY256;
            sgnStagedDX_g = lX;
            sgnStagedDY_g = lY;
            sgwStagedX_g = static_cast<WORD>(lX >> 8);
            sgwStagedY_g = static_cast<WORD>(lY >> 8);
            if (!GetGroundCollisionUnit())
                goto moved;
            sgnStagedDX_g = lDX;
            sgnStagedDY_g = lDY;
            sgwStagedX_g = wX;
            sgwStagedY_g = wY;
        }
        pPath->dwUnitId = pHit->GetUnitID();
        pPath->nSpeed = lSpeed;
        bPathState = 0x1C;
        return TRUE;
    }
    if (bTerrain) {
        pPath->nSpeed = lSpeed;
        bPathState = 0x22;
        return TRUE;
    }
moved:
    if (sgwStagedX_g != pSprite->wPosX || sgwStagedY_g != PosU(pSprite->wPosY)) {
        if (bPathSubState > 2)
            bPathSubState = 2;
        else if (bPathSubState)
            --bPathSubState;
    }
    FinishUnitMovement();
    if (AtMoveTarget()) {
        bPathState = 0x0B;
        return FALSE;
    }
    if (sai_path_progress_timer(this)) {
        sai_path_set_timer(this, 0x1E);
        bPathState = 0x1A;
    }
    return FALSE;
}

// The type argument of the IntersectsType calls is an inlined call, so MSVC
// evaluates it first into ECX and loads `this` last.
static __forceinline WORD path_unit_type(const CUnit *pUnit) { return pUnit->wType; }

// 0x42B3B0 CUnit::PathFixCollision (796 B).
// Decides what to do about the unit the path last collided with: wait, repath,
// slide past it, or give up and stand still.
// Case 3 jumps to a shared `face:` body written after the else chain, so the
// merged nAction = 6 block lands after case 3 and case 0's jbe reaches it.
int CUnit::PathFixCollision() {
    if (FixupPathState(1))
        return 1;
    if (bPathSubState < 0xFF)
        ++bPathSubState;
    CUnit *pOther = FromIdAlive(pPath->dwUnitId);
    int nAction;
    if (!pOther || (pOther->pSprite->flags.b & 0x20) || !(pOther->bPathingFlags & 1)
        || (pOther->dwFlags & 0x200000) || pOther == this
        || !sub_42EBF0((intptr_t)pOther, (intptr_t)this)) {
        bPathState = 0x19;
        return 0;
    }
    CUnit *pTarget = *reinterpret_cast<CUnit *const *>(_unmapped_14);
    if (pTarget && IsWithinTargetRange(0, pTarget)) {
        nAction = 1;
    } else if (pOther->IntersectsType(path_unit_type(this), wMoveTargetX, wMoveTargetY)) {
        if (pOther->bPathSubState < 0x1E
            && !pOther->IntersectsTypeAt(pOther->wMoveTargetX, pOther->wMoveTargetY,
                                         path_unit_type(this), wMoveTargetX, wMoveTargetY))
            nAction = sub_42CDE0(pOther) ? 4 : 6;
        else
            nAction = (*reinterpret_cast<CUnit *const *>(_unmapped_14) != NULL) + 2;
    } else if (pOther->IntersectsType(path_unit_type(this), wNextWaypointX, wNextWaypointY)) {
        if (pOther->bPathSubState < 0x1E
            && !pOther->IntersectsTypeAt(pOther->wMoveTargetX, pOther->wMoveTargetY,
                                         path_unit_type(this), wNextWaypointX, wNextWaypointY))
            nAction = sub_42CDE0(pOther) ? 4 : 6;
        else
            nAction = 3;
    } else {
        if (pOther->bMoveFlags & 2) {
            switch (sub_42CC70(pOther)) {
            case 3:
                if (pOther->bDirection == 0 || pOther->bDirection >= 0x80)
                    goto face;
                break;
            case 1:
                if (pOther->bDirection <= 0x80) {
                    nAction = 6;
                    goto act;
                }
                break;
            case 0:
                if (pOther->bDirection <= 0x40 || pOther->bDirection >= 0xC0) {
                    nAction = 6;
                    goto act;
                }
                break;
            case 2:
                if (pOther->bDirection >= 0x40 && pOther->bDirection <= 0xC0) {
                    nAction = 6;
                    goto act;
                }
                break;
            }
        }
        int nState;  // Hyperion: set apart from its declaration, which a goto jumps past
        nState = pOther->bPathState;
        if (pOther->bPathSubState > 2)
            nState = 0x1D;
        if (!pOther->AtMoveTarget() && (pOther->bMoveFlags & 2)) {
            switch (nState) {
            case 0x19:
            case 0x1A:
                nAction = 6;
                goto act;
            case 0x1D:
                break;
            default:
                nAction = 4;
                goto act;
            }
        }
        nAction = sub_42DC70(pOther) >= 0 ? 7 : 5;
        goto act;
face:
        nAction = 6;
    }
act:
    if (oc_dwReplayFrame - pPath->dwFrame < 7
        && static_cast<unsigned>(nAction - 3) <= 2) {
        WalkInterrupt();
        return 0;
    }
    switch (nAction) {
    case 1:
        MakeImmovable();
        dwFlags &= ~0x80000u;
        bPathState = 0x0B;
        return 0;
    case 2:
        MakeImmovable();
        bPathState = 0x0B;
        return 0;
    case 3:
        bPathState = 0x17;
        return 0;
    case 4:
        bPathState = 0x18;
        return 0;
    case 5:
        bPathState = 0x13;
        reinterpret_cast<CFlingy *>(this)->ForceSpeed(0);
        return 0;
    case 6:
        bPathState = 0x1D;
        return 0;
    case 7:
        pPath->dwUnitId = pOther->GetUnitID();
        pPath->bDirection = static_cast<BYTE>(sub_42DC70(pOther));
        bPathState = 0x1F;
        return 0;
    case 8:
        bPathState = 0x1E;
        return 0;
    }
    return 0;
}
