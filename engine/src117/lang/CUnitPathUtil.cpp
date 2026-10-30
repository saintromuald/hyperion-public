// CUnitPathUtil.cpp -- the terrain and unit-overlap predicates the path state
// machine asks before it commits to a move. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller here by /Ob2, while the
// oracle, built with WPO off, cannot splice across a TU boundary: a callee left in another
// TU reproduces that for free, and __declspec(noinline) is the lever when one must live
// here.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/CUnitPathUtil.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/rect16.h"      // ours: the gUnitBoundBox row type
#include "../h/CImage.h"      // ours: get_image_script, CImage::RunScript
#include "../h/CFlingy.h"     // ours: AngleBetween, TurnDelta, the CFlingy movement fields
#include "../h/TrigEnum.h"    // ours: find_foreach_unit and its FindUnitCb
#include "../h/order.h"       // ours: get_units_in_rect
#include "../h/CUnitFind.h"   // ours: find_units_in_rect, finish_unit_iteration
#include "../h/CUnitBounds.h" // ours: pos_sort_ent_t, the two sort arrays' element
#include "../h/sai_Contours.h"
                              // src117/h/sai_Contours.h does not declare yet
#include "../h/sai_PathCreate.h"  // ours: ep_glob_t -- epGlob's pContours at +0x97A1C
#include "../gen/globals117.h"

// The iscript cursor this TU keeps at 0x665B48. CUnit::sub_42DFF0 copies it
// whole out of a CImage with two dword moves off image +0x10 and +0x14, so the
// layout is CImage's own +0x10..+0x17 -- header, offset, wUnk04, anim, wait. The
// two byte slots are the ones SetPathScript writes individually.
// Hyperion: the struct is CImage.h's, its offsets 32 bits on 64.
OC_X86_LAYOUT(sizeof(iscript_state_t) == 0x08, "iscript_state_t size (the two dword copies)");
OC_X86_LAYOUT(offsetof(iscript_state_t, wIscriptHeader) == 0x00, "iscript_state_t::wIscriptHeader [0x665B48]");
OC_X86_LAYOUT(offsetof(iscript_state_t, wIscriptOffset) == 0x02, "iscript_state_t::wIscriptOffset [0x665B4A]");
OC_X86_LAYOUT(offsetof(iscript_state_t, wUnk04)            == 0x04, "iscript_state_t::wUnk04            [0x665B4C]");
OC_X86_LAYOUT(offsetof(iscript_state_t, bAnim)          == 0x06, "iscript_state_t::bAnim          [0x665B4E]");
OC_X86_LAYOUT(offsetof(iscript_state_t, bWait)          == 0x07, "iscript_state_t::bWait          [0x665B4F]");

// Hyperion: on 64 bits the cursor is wider than 1.17's 8 bytes at 0x665B48, so it is
// a variable of its own; each user fills it from an image before running it.
#if !defined(_M_IX86)
static iscript_state_t g_hxPathScript;
#define sgPathScript_g g_hxPathScript
#endif

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one over the whole
// block, since the generated set grows over time and a single guard would undeclare every
// name under it. gfpCellMap and sg_vf4_map_g carry no type in the registry, so both keep
// the in-TU spelling.

#ifdef OC_DLL_MODE
#ifndef gwMapWidth
#define gwMapWidth (*reinterpret_cast<WORD *>(OC_VA(0x5264B4u)))
#endif
#ifndef gpSquareMap
#define gpSquareMap (*reinterpret_cast<DWORD **>(OC_VA(0x666454u)))
#endif
#ifndef gfpCellMap
#define gfpCellMap (*reinterpret_cast<WORD **>(OC_VA(0x54323Cu)))
#endif
#ifndef sg_vf4_map_g
#define sg_vf4_map_g (*reinterpret_cast<WORD **>(OC_VA(0x66644Cu)))
#endif
#ifndef gUnitBoundBox
#define gUnitBoundBox (reinterpret_cast<rect16_t *>(OC_VA(0x522C38u)))
#endif
#ifndef dword_69B578
#define dword_69B578 (*reinterpret_cast<DWORD *>(OC_VA(0x69B578u)))
#endif
#ifndef sgpResolveCollisionUnit_g
#define sgpResolveCollisionUnit_g (*reinterpret_cast<CUnit **>(OC_VA(0x69B57Cu)))
#endif
#ifndef sgPathScript_g
#define sgPathScript_g (*reinterpret_cast<iscript_state_t *>(OC_VA(0x665B48u)))
#endif
#ifndef g_pos_sort_x
#define g_pos_sort_x (reinterpret_cast<pos_sort_ent_t *>(OC_VA(0x5C99B8u)))
#endif
#ifndef g_pos_sort_y
#define g_pos_sort_y (reinterpret_cast<pos_sort_ent_t *>(OC_VA(0x5D03F8u)))
#endif
#ifndef epGlob
#define epGlob (*reinterpret_cast<void **>(OC_VA(0x69B584u)))
#endif
#ifndef gpImageUnit
#define gpImageUnit (*reinterpret_cast<CUnit **>(OC_VA(0x543300u)))
#endif
#else
extern WORD   gwMapWidth;     // 0x5264B4, registered
extern DWORD *gpSquareMap;    // 0x666454, registered
extern WORD  *gfpCellMap;     // 0x54323C, sheet name, unregistered
extern WORD  *sg_vf4_map_g;   // 0x66644C, registered, type withheld
extern rect16_t gUnitBoundBox[228] OC_UNALIGNED;  // 0x522C38, registered
extern DWORD  dword_69B578;   // 0x69B578, registered
extern CUnit *sgpResolveCollisionUnit_g;  // 0x69B57C, registered
extern iscript_state_t sgPathScript_g;    // 0x665B48, sheet name, unregistered
extern pos_sort_ent_t g_pos_sort_x[POOL_FINDER_STORE];  // 0x5C99B8, registered, type withheld
extern pos_sort_ent_t g_pos_sort_y[POOL_FINDER_STORE];  // 0x5D03F8, registered, type withheld
extern void  *epGlob;         // 0x69B584, sheet name; void * as in sai.cpp
extern CUnit *gpImageUnit;    // 0x543300, sheet name, unregistered
#endif

// CUnit +0x13C/+0x140/+0x144/+0x148, the four indices into those arrays.
// CUnit.h still spells the range _unmapped_138, so they are reached by offset
// exactly as src117/lang/collide.cpp reaches them.
#define UNIT_BOUND_LEFT(u)   (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x04))
#define UNIT_BOUND_RIGHT(u)  (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x08))
#define UNIT_BOUND_TOP(u)    (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x0C))
#define UNIT_BOUND_BOTTOM(u) (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x10))

// CUnit +0x27, the movement type. src117/h/CFlingy.h names it bMovementType at
// this offset and CUnit is that class's layout prefix, but CUnit.h still spells
// the range _unmapped_24, so it is reached by offset here.
#define UNIT_MOVEMENT_TYPE(u) (*reinterpret_cast<BYTE *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_24) + 3))

// What CUnit::sub_42EAB0 builds on its stack and sub_42E130 reads back: the
// search box plus the unit the search must ignore. sub_42E130 reads the four
// bounds with movsx off +0x00..+0x06 and the pointer with a dword at +0x08.
struct unit_scan_ctx_t {
  /*0x00*/ mrect_t  rc;        // Hyperion: a box of map positions
  /*0x08*/ CUnit   *pExclude;
};
OC_X86_LAYOUT(sizeof(unit_scan_ctx_t) == 0x0C, "unit_scan_ctx_t size (rect then the excluded unit)");
static_assert(offsetof(unit_scan_ctx_t, rc)        == 0x00, "unit_scan_ctx_t::rc       movsx word ptr [edi]");
OC_X86_LAYOUT(offsetof(unit_scan_ctx_t, pExclude)  == 0x08, "unit_scan_ctx_t::pExclude mov edx, dword ptr [edi+8h]");

// The bodies, in oracle VA order.

// CUnit::AtMoveTarget's body, repeated because the oracle inlines it and the copy of it
// lives in another TU, which a WPO-off build cannot reach.
//
// Keep the `? 2 : 1` and the WORD cast on the Y compare; both move bytes. The
// `? 2 : 1` is the whole of `neg / sbb / neg / inc eax` at each call site.
static __forceinline BOOL at_move_target(CUnit *pUnit)
{
  CSprite *pSprite = pUnit->pSprite;

  if (pUnit->wMoveTargetX == pSprite->wPosX
      && pUnit->wMoveTargetY == PosU(pSprite->wPosY))
    return (pUnit->dwFlags & 0x80000) ? 2 : 1;

  return 0;
}


// 0x42DFF0, 219 B. Re-measures this unit's top speed by running its iscript's
// animation 0x0B thirty-two times over the shared path cursor and averaging the
// speed each pass reports.
void CUnit::sub_42DFF0()
{
  if (UNIT_MOVEMENT_TYPE(this) != 2)
    return;

  CImage *pImage = pSprite->pMainImage;

  sgPathScript_g = *reinterpret_cast<const iscript_state_t *>(&pImage->wIscriptHeader);
  sgPathScript_g.bAnim = 0x0B;

  WORD *pScript = (WORD *)get_image_script(sgPathScript_g.wIscriptHeader);
  if (0x0B > pScript[0])
    return;

  sgPathScript_g.wIscriptOffset = ImageScriptAnim(sgPathScript_g.wIscriptHeader, 0x0B);  // Hyperion
  if (sgPathScript_g.wIscriptOffset == 0)
    return;

  sgPathScript_g.bWait = 0;
  sgPathScript_g.wUnk04 = 0;

  int nTotal = 0;
  int nSpeed = 0;

  for (int i = 0x0B + 0x15; i > 0; i--)
  {
    CUnit *pSave = gpImageUnit;
    gpImageUnit = this;
    pSprite->pMainImage->RunScript(&sgPathScript_g, 1, (intptr_t)&nSpeed);
    gpImageUnit = pSave;
    nTotal += GetModifiedSpeed(nSpeed);
    nSpeed = 0;
  }

  lTopSpeed = nTotal / 32;
}

// 0x42E0D0, 91 B. Ends the transparent-worker order: either hand back to the
// harvest order, or clear the two collision flags, re-collide and fall back to
// the type's guard order.
void CUnit::DispatchUndoTransparentWorker()
{
  if (IsHarvestOrderQueued())
  {
    SetOrder_interrupt(0x97, 0);
    EndUninterruptableDispatchOrder();
    return;
  }

  dwFlags &= 0xFF5FFFFF;
  UpdateUnitCollision();

  if (pOrderQueueHead == NULL)
    SetOrder_interrupt(gubUnitOrderGuard[wType], 0);

  ResetMovementState();
  EndUninterruptableDispatchOrder();
}

// 0x42E130, 133 B. The per-unit filter of the search CUnit::sub_42EAB0 runs:
// pathable, not being built over, not the excluded unit, and its sorted bound
// box really overlapping the search box. The table entry is on the left of
// every compare, which is the order the oracle spends.
BOOL sub_42E130(intptr_t nArg0, intptr_t nArg1)
{
  CUnit *pUnit = (CUnit *)nArg0;
  unit_scan_ctx_t *pCtx = (unit_scan_ctx_t *)nArg1;
  CUnit *pExclude = pCtx->pExclude;

  if ((pUnit->bPathingFlags & 1)
      && !(pUnit->dwFlags & 0x200000)
      && (pExclude == NULL
          || (pUnit != pExclude
              && sub_42EBF0((intptr_t)pUnit, (intptr_t)pExclude)))
      && g_pos_sort_x[UNIT_BOUND_RIGHT(pUnit)].nCoord >= pCtx->rc.left
      && g_pos_sort_x[UNIT_BOUND_LEFT(pUnit)].nCoord <= pCtx->rc.right
      && g_pos_sort_y[UNIT_BOUND_BOTTOM(pUnit)].nCoord >= pCtx->rc.top)
    return g_pos_sort_y[UNIT_BOUND_TOP(pUnit)].nCoord <= pCtx->rc.bottom;

  return FALSE;
}

// 0x42E1C0, 183 B. Copies the units in a rectangle into the front of the list
// the finder returned, keeping only the ones that are moving, pathable and not
// the excluded unit, and stops at nArg2 of them.
// The increment is written in both arms; MSVC merges them after register
// allocation, and the extra reference gives nCount EBX over the excluded unit.
intptr_t sub_42E1C0(intptr_t nArg0, int nArg1, int nArg2, void *pArg3)
{
  int nCount = 0;

  CUnit **ppList = (CUnit **)((CUnit *)nArg0)->GetUnitsInRectangle(nArg1, nArg1);
  CUnit **ppOut = ppList;
  CUnit **pp = ppList;
  CUnit *pUnit = *pp;

  while (pUnit != NULL)
  {
    if (!at_move_target(pUnit)
        && (pUnit->dwFlags & 0x100000)
        && (pUnit->bPathingFlags & 1)
        && !(pUnit->dwFlags & 0x200000)
        && ((CUnit *)nArg0 == NULL
            || (pUnit != (CUnit *)nArg0 && sub_42EBF0((intptr_t)pUnit, nArg0))))
    {
      *ppOut = *pp;
      if (nCount & 1) nCount++; else nCount++;
      ppOut++;
      if (nCount == nArg2)
        break;
    }

    pUnit = *++pp;
  }

  *ppOut = NULL;
  *(int *)pArg3 = (int)(ppOut - ppList);
  return (intptr_t)ppList;
}

// 0x42E280, 211 B. The same walk over a plain rectangle, compacting the list in
// place and carrying the IsIntersectable predicate textually rather than as a
// call. dword_69B578 is raised for the length of the walk.
intptr_t sub_42E280(intptr_t nArg0, intptr_t nArg1)
{
  CUnit *pExclude = (CUnit *)nArg1;

  CUnit **ppList = get_units_in_rect((rect16_t *)nArg0);
  dword_69B578 = 1;

  CUnit **pp = ppList;
  CUnit **ppOut = ppList;
  CUnit *pUnit = *ppList;

  while (pUnit != NULL)
  {
    if ((at_move_target(pUnit)
         || pUnit == sgpResolveCollisionUnit_g
         || dword_69B578 != 0)
        && !pUnit->IsDoor()
        && (pUnit->bPathingFlags & 1)
        && !(pUnit->dwFlags & 0x200000)
        && (pExclude == NULL
            || (pUnit != pExclude && sub_42EBF0((intptr_t)pUnit, (intptr_t)pExclude))))
    {
      *ppOut = *pp;
      ppOut++;
    }

    pUnit = *++pp;
  }

  dword_69B578 = 0;
  *ppOut = NULL;
  return (intptr_t)ppList;
}

// 0x42E360, 151 B. Whether a point falls inside this unit's box grown by
// another type's box. The grown box mirrors that type: its right extends the
// left edge and its left the right one.
// Read the sprite position into locals first, grow in two phases, and put the failure
// return at the tail; each of the three shows in the code.
BOOL CUnit::IntersectsType(WORD wOtherType, spos_t x, spos_t y)
{
  mrect_t  rc;
  upos_t wPosY = pSprite->wPosY;
  upos_t wPosX = pSprite->wPosX;

  rc.top    = wPosY - gUnitBoundBox[wType].top;
  rc.left   = wPosX - gUnitBoundBox[wType].left;
  rc.bottom = gUnitBoundBox[wType].bottom + wPosY;
  rc.right  = gUnitBoundBox[wType].right + wPosX;

  rc.left   -= gUnitBoundBox[wOtherType].right;
  rc.right  += gUnitBoundBox[wOtherType].left;
  rc.top    -= gUnitBoundBox[wOtherType].bottom;
  rc.bottom += gUnitBoundBox[wOtherType].top;

  if (x >= rc.left && x <= rc.right && y >= rc.top && y <= rc.bottom)
    return TRUE;

  return FALSE;
}

// 0x42E400, 141 B. IntersectsType with the point and the box centre given
// explicitly instead of read off the sprite.
//
// The bound order top, bottom, left, right is the one 1.17 emits: any other keeps wY in
// a register and loses the second `[ebp+0Ch]` read. The self index is an int
// local so the four reads stay `[idx*8 + base]`.
BOOL CUnit::IntersectsTypeAt(upos_t wX, upos_t wY, int nType, upos_t wOtherX, upos_t wOtherY)
{
  mrect_t  rc;
  int nSelf = wType;

  rc.top    = wY - gUnitBoundBox[nSelf].top;
  rc.bottom = gUnitBoundBox[nSelf].bottom + wY;
  rc.left   = wX - gUnitBoundBox[nSelf].left;
  rc.right  = gUnitBoundBox[nSelf].right + wX;

  rc.left   -= gUnitBoundBox[(WORD)nType].right;
  rc.right  += gUnitBoundBox[(WORD)nType].left;
  rc.top    -= gUnitBoundBox[(WORD)nType].bottom;
  rc.bottom += gUnitBoundBox[(WORD)nType].top;

  if (PosS(wOtherX) >= rc.left && PosS(wOtherX) <= rc.right &&
      PosS(wOtherY) >= rc.top && PosS(wOtherY) <= rc.bottom)
    return TRUE;

  return FALSE;
}

// 0x42E490, 142 B. Whether a unit is a candidate for a collision test at all:
// live, pathable, not a door, and not excluded by the caller's own unit.
BOOL IsIntersectable(intptr_t nArg0, intptr_t nArg1)
{
  CUnit *pUnit  = (CUnit *)nArg0;
  CUnit *pOther = (CUnit *)nArg1;

  if ((at_move_target(pUnit)
       || pUnit == sgpResolveCollisionUnit_g
       || dword_69B578 != 0)
      && !pUnit->IsDoor()
      && (pUnit->bPathingFlags & 1)
      && !(pUnit->dwFlags & 0x200000)
      && (pOther == NULL
          || (pUnit != pOther && sub_42EBF0((intptr_t)pUnit, (intptr_t)pOther))))
    return TRUE;

  return FALSE;
}

// 0x42E520, 216 B. Whether the point sits on the border of pTarget's box grown by this
// unit's box. The oracle grows `right` twice and never grows `bottom`; the second term is
// this unit's top and belongs to bottom, and the quirk is kept, not corrected.
// Build top, bottom, left, right, then grow left, right, top, right: that order
// keeps the two `+1` growths apart and gives the two stack spills. The point
// arrives as one packed dword.
BOOL CUnit::IsIntersecting(CUnit *pTarget, int nPackedPos)
{
  mrect_t  rc;
  DWORD dwPos = static_cast<DWORD>(nPackedPos);
  spos_t posY = pTarget->pSprite->wPosY;
  spos_t posX = pTarget->pSprite->wPosX;

  rc.top    = posY - gUnitBoundBox[pTarget->wType].top;
  rc.bottom = gUnitBoundBox[pTarget->wType].bottom + posY;
  rc.left   = posX - gUnitBoundBox[pTarget->wType].left;
  rc.right  = gUnitBoundBox[pTarget->wType].right + posX;

  rc.left  += -1 - gUnitBoundBox[wType].right;
  rc.right += gUnitBoundBox[wType].left + 1;
  rc.top   += -1 - gUnitBoundBox[wType].bottom;
  rc.right += gUnitBoundBox[wType].top + 1;

  if ((short)dwPos != rc.left && (short)dwPos != rc.right)
  {
    if ((short)(dwPos >> 16) != rc.top && (short)(dwPos >> 16) != rc.bottom)
      return FALSE;
    if ((short)dwPos < rc.left || (short)dwPos > rc.right)
      return FALSE;
    return TRUE;
  }

  if ((short)(dwPos >> 16) < rc.top || (short)(dwPos >> 16) > rc.bottom)
    return FALSE;
  return TRUE;
}

// 0x42E600, 103 B. Re-runs the unit-to-unit collision test over everything in
// this unit's own bound box.
void CUnit::UpdateUnitCollision()
{
  mrect_t  rc;
  upos_t wPosY = pSprite->wPosY;
  upos_t wPosX = pSprite->wPosX;

  rc.top    = wPosY - gUnitBoundBox[wType].top;
  rc.bottom = gUnitBoundBox[wType].bottom + wPosY;
  rc.left   = wPosX - gUnitBoundBox[wType].left;
  rc.right  = gUnitBoundBox[wType].right + wPosX;

  find_foreach_unit(&rc, (FindUnitCb)update_unit_to_unit_collision, this);
}

// 0x42E670, 71 B. The find_foreach_unit callback above: ECX is the unit found
// and EDX the unit that started the search. Returning 0 keeps the walk going.
BOOL update_unit_to_unit_collision(intptr_t nArg0, void *pArg1)
{
  CUnit *pUnit = (CUnit *)nArg0;
  CUnit *pSelf = (CUnit *)pArg1;
  DWORD dwFlags = pUnit->dwFlags;

  if (dwFlags & 2)
  {
    pSelf->dwFlags |= 0x40000;
    return 0;
  }

  if (!(dwFlags & 4)
      && (!(dwFlags & 0x800000) || (pSelf->dwFlags & 2))
      && pUnit->OverlapsWith(pSelf))
    pUnit->dwFlags |= 0x40000;

  return 0;
}

// 0x42E6C0, 738 B. Simulates this unit's movement nArg0 samples ahead, nArg1
// steps per sample, and writes each sample's pixel position to pArg2.
// One return after the if/else; separate returns merge the empty-loop exits
// and lose the oracle's `mov eax, edx` epilogue.
int CUnit::sub_42E6C0(int nArg0, int nArg1, mpoint_t *pOut)
{
  CFlingy *pFlingy = reinterpret_cast<CFlingy *>(this);
  BYTE bTarget = AngleBetween(PosS(pSprite->wPosX), pSprite->wPosY,
                              pFlingy->wNextWaypointX, pFlingy->wNextWaypointY);
  BYTE bCur = bDirection;
  int nTurn = FlingyTurnRadiusWithModifiers();
  int nX = pFlingy->lPositionX256;
  int nY = pFlingy->lPositionY256;
  int nSpeed = pFlingy->lTargetSpeed;
  int nTopSpeed = pFlingy->lTopSpeed;
  int nAccel = FlingyAccelWithModifiers();
  BOOL bTurned = TRUE;
  int nDx = 0;
  int nDy = 0;

  if (UNIT_MOVEMENT_TYPE(this) == 2) {
    sgPathScript_g = *reinterpret_cast<const iscript_state_t *>(&pSprite->pMainImage->wIscriptHeader);
    if (sgPathScript_g.bAnim != 0x0B) {
      SetPathScript(0x0B);
      CUnit *pSave = gpImageUnit;
      gpImageUnit = this;
      pSprite->pMainImage->RunScript(&sgPathScript_g, 1, (intptr_t)&nSpeed);
      gpImageUnit = pSave;
    }
    for (int i = 0; i < nArg0; i++) {
      for (int j = nArg1; j > 0; j--) {
        if (bTarget != bCur) {
          bCur += TurnDelta(bCur, bTarget, nTurn);
          bTurned = TRUE;
        }
        if (bCur == bTarget && nSpeed != 0) {
          if (bTurned) {
            bTurned = FALSE;
            nDx = (DIRECTIONS[bCur * 2] * nSpeed) >> 8;
            nDy = (DIRECTIONS[bCur * 2 + 1] * nSpeed) >> 8;
          }
          nX += nDx;
          nY += nDy;
        }
        CUnit *pSave = gpImageUnit;
        gpImageUnit = this;
        nSpeed = 0;
        pSprite->pMainImage->RunScript(&sgPathScript_g, 1, (intptr_t)&nSpeed);
        gpImageUnit = pSave;
      }
      pOut[i].x = PosS(nX >> 8);
      pOut[i].y = PosS(nY >> 8);
    }
  } else {
    for (int i = 0; i < nArg0; i++) {
      for (int j = nArg1; j > 0; j--) {
        if (bTarget != bCur) {
          bCur += TurnDelta(bCur, bTarget, nTurn / 2);
          upos_t wY = pSprite->wPosY;
          upos_t wX = pSprite->wPosX;
          bTarget = AngleBetween(PosS(wX), PosS(wY),
                                 pFlingy->wNextWaypointX, pFlingy->wNextWaypointY);
          bTurned = TRUE;
        }
        if (nSpeed < nTopSpeed) {
          nSpeed += nAccel;
          if (nSpeed > nTopSpeed)
            nSpeed = nTopSpeed;
          bTurned = TRUE;
        }
        if (bTurned) {
          nDx = (DIRECTIONS[bCur * 2] * nSpeed) >> 8;
          nDy = (DIRECTIONS[bCur * 2 + 1] * nSpeed) >> 8;
          bTurned = FALSE;
        }
        nX += nDx;
        nY += nDy;
      }
      pOut[i].x = PosS(nX >> 8);
      pOut[i].y = PosS(nY >> 8);
    }
  }
  return TRUE;
}

// 0x42E9B0, 86 B. Seeks the shared path iscript cursor to one animation.
// ECX is never read, so the bytes cannot tell a member from a free function.
BOOL CUnit::SetPathScript(BYTE bArg0)
{
  sgPathScript_g.bAnim = bArg0;

  WORD *pScript = (WORD *)get_image_script(sgPathScript_g.wIscriptHeader);
  if ((WORD)bArg0 > pScript[0])
    return FALSE;

  sgPathScript_g.wIscriptOffset = ImageScriptAnim(sgPathScript_g.wIscriptHeader, bArg0);  // Hyperion
  if (sgPathScript_g.wIscriptOffset == 0)
    return FALSE;

  sgPathScript_g.bWait = 0;
  sgPathScript_g.wUnk04 = 0;
  return TRUE;
}

// 0x42EA10, 154 B. Whether the pixel at (x, y) is walkable: not blocked, and
// on a partial-cover tile the minitile it falls in must be land too.
// The tile index is spelled twice, not named: a named index costs a third
// callee-saved register. The two `return TRUE` arms are what keep the oracle's
// shared FALSE block.
BOOL is_land_pixel(spos_t x, spos_t y)
{
  DWORD dwFlags = gpSquareMap[x / 32 + (y / 32) * gwMapWidth];

  if (dwFlags & 0x400000)
    return TRUE;
  if (dwFlags & 0x20000000)
  {
    if (sg_vf4_map_g[((gfpCellMap[x / 32 + (y / 32) * gwMapWidth] & 0x7FFF) * 4
                      + ((y / 8) & 3)) * 4 + ((x / 8) & 3)] & 1)
      return TRUE;
  }
  else if (dwFlags & 0x10000)
  {
    return TRUE;
  }
  return FALSE;
}

// 0x42EAB0, 313 B. The largest unit whose box overlaps the one at the point, or
// -1 when the point itself is off the map, or 0/-1 from the type probe when
// nothing was found. The point is a point16_t by value: the guards then take x
// and y out of the packed dword and the last call reads them back from the slot.
intptr_t CUnit::sub_42EAB0(mpoint_t pt)
{
  unit_scan_ctx_t ctx;

  if (pt.x - gUnitBoundBox[wType].left >= 0
      && gUnitBoundBox[wType].right + pt.x < gwMapPixWidth
      && pt.y - gUnitBoundBox[wType].top >= 0
      && gUnitBoundBox[wType].bottom + pt.y < gwMapPixHeight)
  {
    ctx.pExclude = this;
    GetBounds_xy(&ctx.rc, pt.x, pt.y);

    int nBest = 0;
    CUnit *pBest = NULL;
    CUnit **ppList = find_units_in_rect(&ctx.rc);
    CUnit *pUnit = *ppList;

    for (CUnit **pp = ppList; pUnit != NULL; pUnit = *++pp)
    {
      if (sub_42E130((intptr_t)pUnit, (intptr_t)&ctx))
      {
        short nH = gUnitBoundBox[pUnit->wType].bottom + 1 + gUnitBoundBox[pUnit->wType].top;
        short nW = gUnitBoundBox[pUnit->wType].right + 1 + gUnitBoundBox[pUnit->wType].left;
        int nArea = nH * nW;
        if (nArea > nBest)
        {
          nBest = nArea;
          pBest = pUnit;
        }
      }
    }

    finish_unit_iteration(ppList);

    if (pBest != NULL)
      return (intptr_t)pBest;

    return (sub_42ED50(wType, pt.x, pt.y) != 0) - 1;
  }

  return -1;
}

// 0x42EBF0, 119 B. Whether two units actually collide with one another once
// ownership, the 0x23 type and the two order ids are taken into account.
// Both type guards read pOther->wType at the use: a cached local lets MSVC
// thread the second test away, where the oracle keeps it as `cmp eax, eax`.
int sub_42EBF0(intptr_t nArg0, intptr_t nArg1)
{
  CUnit *pUnit  = (CUnit *)nArg0;
  CUnit *pOther = (CUnit *)nArg1;

  if ((pUnit->dwFlags & 0x400000) && pOther->IsEnemyUnit(pUnit))
    return 0;

  if (pOther->wType == 0x23 && !(pUnit->dwFlags & 2))
    return 0;
  if (pOther->wType != 0x23 && pUnit->wType == 0x23)
    return 0;

  if (!(pOther->dwFlags & 0x800000) || (pUnit->dwFlags & 2))
    return 1;
  if (!(pUnit->dwFlags & 0x800000))
    return 0;

  if (pOther->bOrderId == 0x54)
    return 0;

  return pUnit->bOrderId == 0x52;
}

// Whether a box of nWidth x nHeight centred on (x, y) lies on the map with its
// centre on land. The parameter copies are what put the caller's y in a stack
// home at entry and a copy in ESI.
static __forceinline BOOL box_fits_on_land(spos_t x, spos_t y, int nWidth, int nHeight)
{
  int nHalfW = nWidth / 2;
  upos_t wLeft = PosU(x - nHalfW);
  int nHalfH = nHeight / 2;
  upos_t wTop = PosU(y - nHalfH);
  return PosS(wLeft) >= 0
      && PosS(wLeft) + nWidth < gwMapPixWidth
      && PosS(wTop) >= 0
      && PosS(wTop) + nHeight < gwMapPixHeight
      && is_land_pixel(x, y);
}

// 0x42EC70, 211 B. Whether a box of nArg2 x nArg3 centred on (x, y) fits on
// land, and then whether the contour search can reach it. The four search
// parameters are the box halves either side of the centre.
int sub_42EC70(spos_t x, spos_t y, int nArg2, int nArg3)
{
  if (box_fits_on_land(x, y, nArg2, nArg3))
  {
    contours_t *pContours = ((ep_glob_t *)epGlob)->pContours;

    pContours->SetSearchParameters((WORD)((nArg2 - 1) / 2), (WORD)((nArg3 - 1) / 2),
                                   (WORD)(nArg2 - nArg2 / 2 - 1), (WORD)(nArg3 - nArg3 / 2 - 1));
    return pContours->sub_48B780(x, y) != 0;
  }

  return 0;
}

// 0x42ED50, 161 B. The same question for a unit type at (nArg1, nArg2): its
// own bound box must fit on the map and the centre must be land.
int sub_42ED50(int nType, spos_t x, spos_t y)
{
  if (x >= gUnitBoundBox[(WORD)nType].left
      && gUnitBoundBox[(WORD)nType].right + x < gwMapPixWidth
      && y >= gUnitBoundBox[(WORD)nType].top
      && gUnitBoundBox[(WORD)nType].bottom + y < gwMapPixHeight
      && is_land_pixel(x, y))
  {
    contours_t *pContours = ((ep_glob_t *)epGlob)->pContours;

    pContours->SetSearchUnit(nType);
    return pContours->sub_48B780(x, y) != 0;
  }

  return 0;
}
