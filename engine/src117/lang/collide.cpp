// collide.cpp -- the unit collision matrix: the per-cell flag words in
// gpSquareMap, the rect walks that set and clear them, and the placement and
// adjacency queries built on them. Bodies are in ascending oracle address,
// 1.17's own order, from 0x4122D0 to 0x413610.
//
// Which bodies live in this file is itself part of what the compiler sees. A
// callee defined here can be spliced into a caller that the oracle calls, and a
// body whose size is exactly caller plus callee needs its callee here. Where a
// callee has to live here anyway, `__declspec(noinline)` on the declaration is
// the lever. sub_412C90 is held behind `#ifndef OC_PAYLOAD`, so the payload calls
// the oracle's copy of it.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/collide.h"
#include "../h/CUnitPathUtil.h"   // sub_42ED50 -- mtx_passable tail-jumps to it
#include "../h/CSprite.h"           // CSprite::wPosX/wPosY (sub_4132B0)
#include "../h/CUnitFind.h"         // get_units_at_point / finish_unit_iteration
#include "../h/point16.h"           // point16_t, passed BY VALUE to the placement callees
#include "../h/CUnitTerran.h"        // sub_442B60 (mtx_check_near)
#include "../h/rect16.h"            // rect16_t -- gUnitBoundBox (the HELD sub_412C90)
#include "../h/CThingy.h"           // sub_418000, CThingy (the HELD sub_413610)
#include "../h/CUnitBuild.h"        // sub_4132B0 (the HELD sub_413610)
#include "../h/CUnitMisc.h"         // IsBuiltRefineryType (mtx_query_place_bldg)
#include "../h/CUnitSearches.h"     // sai_path_can_reach_target (mtx_query_place_bldg)
#include "../h/net_cmds.h"          // PlayerData -- gPlayerData's element type
#include "../h/CUnitProtoss.h"      // IsCellPowered (mtx_query_powered)
#include "../h/order.h"             // get_units_in_rect (sub_412360)
#include "../gen/globals117.h"
#include <string.h>                 // memset -- mtx_query_place's off-map tail

// ---------------------------------------------------------------------------
// The globals this TU reaches, each spelled once for the standalone build and
// once for the payload, with one guard per name.

// gpSquareMap, gwMapWidth and gulUnitTypeFlags come from ../gen/globals117.h, so
// this file no longer spells them itself.


// ---------------------------------------------------------------------------
// Still spelled here, because the generator emits a global only for a landed
// body and gUnitBoundBox is reached only by the held sub_412C90.

// gUnitBoundBox (0x522C38) has a stride of 8: four signed 16-bit members, left,
// top, right and bottom, reached in the held sub_412C90 and independently in
// CUnit::GetBounds.
#ifndef OC_DLL_MODE
extern rect16_t gUnitBoundBox[228] OC_UNALIGNED;
#endif
#ifdef OC_DLL_MODE
#define gUnitBoundBox (reinterpret_cast<rect16_t *>(OC_VA(0x522C38u)))
#endif

// ---------------------------------------------------------------------------
// Addresses with no registered name, spelled locally here.

// 0x675188 is a BYTE table with a 48-byte row: mtx_query_place's failure tail
// indexes it at that stride and memsets 0x30 bytes, and elsewhere a single byte
// is stored into it through a cell index.
#if !defined(OC_DLL_MODE) && !defined(OC_PAYLOAD)
extern BYTE gTbl675188[][0x30];
#endif
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define gTbl675188 (reinterpret_cast<BYTE (*)[0x30]>(OC_VA(0x675188u)))
#endif

// ---------------------------------------------------------------------------
// Callee prototypes, spelled without an explicit __fastcall: /Gr already makes
// it the default, and a redundant one makes the generator read the name as
// ambiguous.

// mtx_check_near, sub_412360 and sub_412560 are declared in src117/h/collide.h:
// a generated hook row resolves a callee through src117/h alone, so a prototype
// here would compile but leave the row unresolvable.

// 0x4221F0, in CUnitFind.cpp. Returns the NULL-terminated candidate list.
// ECX/EDX are only ever consumed 16-bit (`mov word ptr [ebp-0Ch], bx`,
// `mov word ptr [ebp-8h], ax` after a dword add whose result is truncated), and
// sub_4132B0 hands them over with a plain `mov ecx, esi` -- no sign extension --
// which is only correct if both sides are 16-bit.
CUnit **get_units_at_point(spos_t sX, spos_t sY);

// 0x4222B0. Declared in src117/h/CUnitFind.h and re-spelled here character for
// character, so this TU does not depend on include order.
void __fastcall finish_unit_iteration(CUnit **ppList);

// gPlayerData (0x6726C8) comes from the generated header. The 0x6726D0 that
// appears in the instruction stream is not a second global: it is
// `&gPlayerData[0].nType`, an element folded into the displacement.
//
// byte_675188 is the per-slot placement-status scratch grid, and its shape comes
// from two directions: mtx_query_place_bldg memsets 0x30 bytes at a 48-byte slot
// stride, and sub_412720 and mtx_check_on_creep store one byte per cell at
// `slot*48 + row*6 + col`. So [.][8][6], with the outer extent not derivable
// from any body here.
#ifndef OC_DLL_MODE
extern BYTE byte_675188[][8][6] OC_UNALIGNED;
#endif
#ifdef OC_DLL_MODE
#define byte_675188 (reinterpret_cast<BYTE (*)[8][6]>(OC_VA(0x675188u)))
#endif

// dword_6A2E80 is never dereferenced here, only tested and `cmovnz`'d over the
// CUnit * that mtx_query_place_bldg forwards as sub_412360's ECX. The 4-byte
// slot is certain; the pointee is inferred from that substitution.
#ifndef dword_6A2E80
#ifdef OC_DLL_MODE
#define dword_6A2E80 (*reinterpret_cast<CUnit **>(OC_VA(0x6A2E80u)))
#else
extern CUnit *dword_6A2E80;
#endif
#endif

// ---------------------------------------------------------------------------
// The two position sort arrays, `g_pos_sort_x` (0x5C99B8) and `g_pos_sort_y`
// (0x5D03F8), spelled here rather than coming from the generated header because
// their element type is deliberately not registered.
//
// The stride is 8 and the layout is read off the oracle from four bodies:
// sub_488910 and cunit_bounds_init reach `[idx*8 + base]` and use it as a unit,
// while every bounds query reaches `[idx*8 + base+4]` and compares it against a
// pixel coordinate. The extent of 3400 has two derivations that agree: the byte
// gap to the next name over 8, and 2 x 1700, the unit cap times a low and a high
// bound per unit per axis.
//
// The element is src117/h/CUnitBounds.h's pos_sort_ent_t. This file's copy named
// the first dword `CUnit *pUnit`; it is a one-based unit id, and at 64 bits a
// pointer there would have doubled the entry.
#include "../h/CUnitBounds.h"
#include "../h/playervision.h"  // Hyperion: the slots past 31 (stage D)

#ifndef OC_DLL_MODE
extern pos_sort_ent_t g_pos_sort_x[POOL_FINDER_STORE];
#endif
#ifndef OC_DLL_MODE
extern pos_sort_ent_t g_pos_sort_y[POOL_FINDER_STORE];
#endif
#ifdef OC_DLL_MODE
#define g_pos_sort_x (reinterpret_cast<pos_sort_ent_t *>(OC_VA(0x5C99B8u)))
#endif
#ifdef OC_DLL_MODE
#define g_pos_sort_y (reinterpret_cast<pos_sort_ent_t *>(OC_VA(0x5D03F8u)))
#endif

// CUnit +0x13C through +0x148 are the four dword indices into those two arrays,
// reached as offsets into CUnit.h's `_unmapped_138[0x18]`, which still spells the
// range; the derivation for naming them is recorded on that placeholder. The axis
// of each is fixed by which array it indexes, and the polarity within a pair is
// read off the compare senses, which is the one inferred part.
#define UNIT_BOUND_LEFT(u)   (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x04))
#define UNIT_BOUND_RIGHT(u)  (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x08))
#define UNIT_BOUND_TOP(u)    (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x0C))
#define UNIT_BOUND_BOTTOM(u) (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x10))

// ---------------------------------------------------------------------------
// the bodies

// ---------------------------------------------------------------------------
// 0x412720. The own-building footprint scan: it scores every cell of the
// footprint into byte_675188[nSlot] and returns the worst code found -- 10 for a
// cell the placing player's own something occupies, 9 for an ally's, 5 for free
// and 0 for blocked.
//
// Three shapes here are worth bytes. The max is a ternary and not an `if`, which
// is what cross-jumps the two arms into one merge block. The bounds must not be
// hoisted into named locals: written inline, the compiler sinks the width and the
// row advance below the height guard, which is where the oracle computes them.
// And both counters are WORD, widened for the compare, which an int counter does
// not emit. The 10-arm assigns where the 9- and 5-arms take the max, and the
// blocked arm skips the merge; that asymmetry is in the branch targets.
int sub_412720(short sX, short sY, BYTE bPlayer, point16_t size, int nSlot,
               int nFlag)
{
  DWORD dwMaskOwn = (gPlayerData[bPlayer].nType == 2) ? (1 << (bPlayer + 8)) : 0;
  DWORD dwMaskAlly = (gPlayerData[bPlayer].nType == 2) ? (1 << bPlayer) : 0;
  BYTE bWorst = 0;
  DWORD *pCell = &gpSquareMap[gwMapWidth * sY + sX];
  DWORD dwMaskBlock = nFlag ? 0x40000000 : 0x00400000;

  for (WORD wRow = 0; wRow < size.y; wRow++)
  {
    for (WORD wCol = 0; wCol < size.x; wCol++)
    {
      DWORD dwCell = *pCell;
      BYTE bCode = 0;

      if (dwMaskOwn & dwCell)
      {
        bCode = 10;
        bWorst = bCode;
      }
      else if (nFlag && (dwMaskAlly & dwCell))
      {
        bCode = 9;
        bWorst = (bWorst > bCode) ? bWorst : bCode;
      }
      else if (!(dwMaskBlock & dwCell))
      {
        bCode = 5;
        bWorst = (bWorst > bCode) ? bWorst : bCode;
      }

      byte_675188[nSlot][wRow][wCol] = bCode;
      pCell++;
    }

    pCell += gwMapWidth - size.x;
  }

  return bWorst;
}

// ---------------------------------------------------------------------------
// 0x412870  mtx_check_near  (581 B)
//
// The rect is built only on the path that calls sub_442B60, so the two
// `bVal = 2` sites share one block and EBX carries bVal and bBest.
static BOOL resource_container_near(upos_t wPixX, upos_t wPixY)
{
  mrect_t  rc;

  rc.left = PosS(wPixX - 0xE0);
  rc.top = PosS(wPixY - 0xE0);
  rc.right = PosS(wPixX);
  rc.bottom = PosS(wPixY);

  return sub_442B60(&rc, is_resource_container_cb, NULL) != NULL;
}

// Scores each footprint cell for a unit type near resources and returns the
// highest score. Same levers as mtx_check_near: bVal cleared before the cell
// read and the max in the for-increment.
int sub_412560(int nUnitType, short sX, short sY, BYTE bPlayer,
               point16_t ptSize, int nArg5)
{
  WORD wType = (WORD)nUnitType;
  WORD wRes = wType - 0xC2;
  int bSpecial = wRes <= 5;
  DWORD dwNear = gulUnitTypeFlags[wType] & 0x1000;
  DWORD *pCell = &gpSquareMap[gwMapWidth * sY + sX];
  DWORD dwMask = 0x28800000;
  if (!(gubUnitCountInfo[wType] & 1))
    dwMask = 0x28C00000;
  BYTE bBest = 0;
  BYTE bVal;

  for (WORD wRow = 0; wRow < ptSize.y; wRow++, pCell += gwMapWidth - ptSize.x)
  {
    upos_t wPixY = PosU((sY + wRow + 4) * 32);

    for (WORD wCol = 0; wCol < ptSize.x; wCol++, bBest = (bBest > bVal) ? bBest : bVal)
    {
      upos_t wPixX = PosU((sX + wCol + 4) * 32);
      bVal = 0;
      DWORD dwFlags = *pCell;

      if ((dwFlags & dwMask) && (!bSpecial || (int)dwFlags >= 0))
        bVal = 4;
      else if (dwNear)
      {
        if (resource_container_near(wPixX, wPixY))
          bVal = 2;
      }

      byte_675188[nArg5][wRow][wCol] = bVal;
      pCell++;
    }
  }

  return bBest;
}

// Scores each footprint cell for a unit type near resources and allied
// buildings, and returns the highest score.
// bVal is cleared before the cell read, the max sits in the for-increment and
// pCell steps after the store; together they keep pCell out of a register.
int mtx_check_near(WORD wType, short sX, short sY, BYTE bPlayer, point16_t size, int nSlot,
                   int nArg6)
{
  WORD wRes = wType - 0xC2;
  int bIsResourceType = wRes <= 5;
  int nCountInfoBit = gubUnitCountInfo[wType] & 1;
  DWORD dwHasFlag1000 = gulUnitTypeFlags[wType] & 0x1000;

  DWORD dwMaskA = (gPlayerData[bPlayer].nType == 2) ? (1 << (bPlayer + 8)) : 0;
  DWORD dwMaskB = (gPlayerData[bPlayer].nType == 2) ? (1 << bPlayer) : 0;

  DWORD *pCell = &gpSquareMap[gwMapWidth * sY + sX];

  DWORD dwMaskC = 0x20800000;
  if (!nCountInfoBit)
    dwMaskC = (nArg6 ? 0x40000000 : 0x400000) | 0x20800000;

  BYTE bBest = 0;
  BYTE bVal;

  for (WORD wRow = 0; wRow < size.y; wRow++, pCell += gwMapWidth - size.x)
  {
    upos_t wPixY = PosU((sY + wRow + 4) * 32);

    for (WORD wCol = 0; wCol < size.x; wCol++, bBest = (bBest > bVal) ? bBest : bVal)
    {
      upos_t wPixX = PosU((sX + wCol + 4) * 32);
      bVal = 0;
      DWORD dwCell = *pCell;

      if (dwMaskA & dwCell)
      {
        bVal = 10;
      }
      else if ((nArg6 || !(dwMaskB & dwCell)) && (dwMaskC & dwCell) &&
               (!bIsResourceType || (int)dwCell >= 0))
      {
        bVal = 4;
      }
      else if (dwHasFlag1000)
      {
        BOOL bNear;
        if (nArg6 && sub_4180A0(PosS(sX + wCol), PosS(sY + wRow), 3))
          bNear = TRUE;
        else
          bNear = resource_container_near(wPixX, wPixY);
        if (bNear)
          bVal = 2;
      }

      byte_675188[nSlot][wRow][wCol] = bVal;
      pCell++;
    }
  }

  return bBest;
}

// ---------------------------------------------------------------------------
// 0x412AC0  mtx_check_on_creep  (332 B)  -- the same walk, one argument wider.
// Two independent ifs per cell, so a cell scored 10 can be overwritten with 5,
// as the oracle does. The row advance sits in the outer for's increment after
// wRow++; at the end of the body the height takes EDX instead of the advance.
int mtx_check_on_creep(short sX, short sY, BYTE bPlayer, point16_t size, int nSlot,
               int nFlag, int nFlag2)
{
  DWORD dwMaskOwn = (gPlayerData[bPlayer].nType == 2) ? (1 << (bPlayer + 8)) : 0;
  DWORD dwMaskAlly = (gPlayerData[bPlayer].nType == 2) ? (1 << bPlayer) : 0;
  BYTE bWorst = 0;
  DWORD *pCell = &gpSquareMap[gwMapWidth * sY + sX];
  DWORD dwMaskBlock = nFlag ? 0x40000000 : 0x00400000;

  for (WORD wRow = 0; wRow < size.y; wRow++, pCell += gwMapWidth - size.x)
  {
    for (WORD wCol = 0; wCol < size.x; wCol++)
    {
      DWORD dwCell = *pCell;
      BYTE bCode = 0;

      if (dwMaskOwn & dwCell)
      {
        bCode = 10;
        bWorst = bCode;
      }

      if ((nFlag2 || nFlag || !(dwMaskAlly & dwCell)) && !(dwMaskBlock & dwCell))
      {
        bCode = 5;
        bWorst = (bWorst > bCode) ? bWorst : bCode;
      }

      byte_675188[nSlot][wRow][wCol] = bCode;
      pCell++;
    }
  }

  return bWorst;
}

// ---------------------------------------------------------------------------
// 0x4122D0. Does this building block this build cell? It returns 0 only when
// pUnit is a live building whose sorted bounding box really overlaps the 32x32
// pixel cell, and 1 for every reason to ignore the unit. The four indices at
// CUnit +0x13C through +0x148 are slots in the position-sort arrays, paired
// x with x and y with y as CUnit::OverlapsWith pairs them.
//
// The body is eight conjuncts and one shared `return 1` rather than eight
// guards: written as guards the compiler cross-jumps them and hoists the shared
// block to the first one, where the oracle puts `xor eax, eax / ret` first and
// the shared exit last. The compare operand order survives into the `cmp`, so
// the table entry goes on the left of each relational operator.
BOOL sub_4122D0(CUnit *pUnit, int nCellX, int nCellY)
{
  if (pUnit != NULL
      && (pUnit->dwFlags & CUnit::UF_Building)
      && pUnit->wType != 0x86
      && !(pUnit->pSprite->flags.b & 0x20)
      && g_pos_sort_x[UNIT_BOUND_LEFT(pUnit)].nCoord < nCellX * 32 + 32
      && g_pos_sort_x[UNIT_BOUND_RIGHT(pUnit)].nCoord > nCellX * 32
      && g_pos_sort_y[UNIT_BOUND_TOP(pUnit)].nCoord < nCellY * 32 + 32
      && g_pos_sort_y[UNIT_BOUND_BOTTOM(pUnit)].nCoord > nCellY * 32)
  {
    return 0;
  }

  return 1;
}

// Scores each cell of the footprint against the units overlapping the placebox:
// 4 for a blocking building, otherwise mtx_unit_there's verdict.
// The coordinates are ints: the pixel centre shifts the raw parameter dword.
int sub_412360(CUnit *pUnit, int nX, int nY, BYTE bPlayer, WORD nUnitType,
               point16_t ptSize, int nArg6, int nArg7, int nArg8, int nArg9)
{
  DWORD *pCell = &gpSquareMap[gwMapWidth * PosS(nY) + PosS(nX)];
  BYTE bBest = 0;

  DWORD dwMask = (gPlayerData[bPlayer].nType == 2) ? (1 << bPlayer) : 0;

  int nCX = nX * 32 + gUnitPlaceboxSize[nUnitType].x / 2;
  int nCY = nY * 32 + gUnitPlaceboxSize[nUnitType].y / 2;

  rect16_t rc;
  rc.left = (short)(nCX - gUnitBoundBox[nUnitType].left);
  rc.top = (short)(nCY - gUnitBoundBox[nUnitType].top);
  rc.right = (short)(nCX + gUnitBoundBox[nUnitType].right);
  rc.bottom = (short)(nCY + gUnitBoundBox[nUnitType].bottom);

  CUnit **ppList = get_units_in_rect(&rc);

  int nCurY = PosU(nY);
  for (WORD wRow = 0; wRow < ptSize.y; wRow++)
  {
    upos_t nCurX = PosU(nX);

    for (WORD wCol = 0; wCol < ptSize.x; wCol++)
    {
      BYTE *pStatus = &byte_675188[nArg6][wRow][wCol];
      BYTE bVal = 0;

      if (*pStatus == 0)
      {
        if (nArg9 || !(*pCell & dwMask))
        {
          if ((*pCell & 0x8000000) && sub_4122D0(pUnit, PosS(nCurX), (short)nCurY))
          {
            bVal = 4;
          }
          else
          {
            BYTE bThere = (BYTE)mtx_unit_there(pUnit, ppList, PosS(nCurX), (short)nCurY,
                                               (BYTE)nArg7, (BYTE)nArg8);

            if (bThere)
              bVal = bThere;
          }

          bBest = (bBest > bVal) ? bBest : bVal;
          *pStatus = bVal;
        }
      }

      nCurX++;
      pCell++;
    }

    pCell += gwMapWidth - ptSize.x;
    nCurY++;
  }

  finish_unit_iteration(ppList);

  return bBest;
}

// ---------------------------------------------------------------------------
// 0x412C10. The cell accessor, the shape every other body in this file is built
// out of. gwMapWidth is a WORD so it promotes unsigned, and the coordinates are
// short so they promote signed; the whole family reads exactly this way.
int mtx_flags(short sX, short sY)
{
  return gpSquareMap[gwMapWidth * sY + sX];
}

// ---------------------------------------------------------------------------
// 0x412C30. The forwarder: a frame built and torn down around nothing at all,
// then a tail jump. That is what the compiler emits for a body that only
// forwards, since ECX and EDX already hold the first two arguments and the one
// stack word is reused in place.
BOOL mtx_passable(int nUnitType, spos_t x, spos_t y)
{
  return sub_42ED50(nUnitType, x, y);
}

// ---------------------------------------------------------------------------
// 0x412C40. The forward rect walk: row-major from (nX, nY), ORing dwFlags into
// every cell of the rect. The row advance is computed once as
// `gwMapWidth - nWidth` and scaled, which the oracle keeps as a byte stride on a
// DWORD *, so the natural pointer spelling is what reproduces it. Both loops are
// a guard plus a do-while there, which is what a down-counting `for` gives.
void mtx_place_bldg(int nX, int nY, int nWidth, int nHeight, DWORD dwFlags)
{
  DWORD *pCell = &gpSquareMap[gwMapWidth * nY + nX];
  int nAdvance = gwMapWidth - nWidth;

  while (nHeight)
  {
    nHeight--;

    for (int c = nWidth; c; c--)
      *pCell++ |= dwFlags;

    pCell += nAdvance;
  }
}

// ---------------------------------------------------------------------------
// 0x412E20  mtx_query_place  (240 B)
//
// The placement query for a unit type at a build cell, with a per-index result
// row that is filled with 3 when the rect falls off the map.
//
// gUnitPlaceboxSize is in pixels and reaches build cells through the signed
// divide by 32 that rounds toward zero. The two halves are written into a
// point16_t and passed to both callees by value, as one dword.
int mtx_query_place(BYTE bPlayer, int nX, int nY, int nUnitType, int nIndex)
{
  point16_t ptSize;
  int nFlag;
  int nNear;
  int nFar;

  spos_t sX = PosS(nX);
  spos_t sY = PosS(nY);

  nFlag = ~(gulUnitTypeFlags[(WORD)nUnitType] >> 1) & 1;

  ptSize.x = gUnitPlaceboxSize[(WORD)nUnitType].x / 32;
  ptSize.y = gUnitPlaceboxSize[(WORD)nUnitType].y / 32;

  if (sX < 0 || sY < 0
      || sX + ptSize.x > gwMapWidth
      || sY + ptSize.y > gwMapHeight)
  {
    memset(gTbl675188[nIndex], 3, 0x30);
    return 3;
  }

  nNear = sub_412560(nUnitType, sX, sY, bPlayer, ptSize, nIndex);
  nFar = sub_412360(NULL, nX, nY, bPlayer, nUnitType, ptSize, nIndex, nFlag, 1, 0);

  return (nNear > nFar) ? nNear : nFar;
}

// ---------------------------------------------------------------------------
// 0x412F10  mtx_query_place_bldg  (470 B)
//
// The placement verdict for a building of type wType at a build cell. It scores
// the footprint into byte_675188[nSlot] and returns the code the caller shows:
// 3 off the map, 7 unreachable, and otherwise whatever the per-cell walk reports.
//
// The memset and return tail is one block seven predecessors reach, among them
// the refinery arm, which falls into it where the non-refinery arm is laid out
// after it and returns on its own.
//
// Three shapes hold the body up. The array index must not be the same value as
// the call argument: spelled WORD, the index and the value handed to the four
// callees are one value, so the compiler folds the scale into every addressing
// mode and each access grows a byte. The shared tail needs a `goto`, since two
// textual copies are not tail-merged. And `nResult = 3` has to be assigned after
// the flags read, because the oracle spills the index before it.
int mtx_query_place_bldg(CUnit *pUnit, BYTE bPlayer, int nX, int nY,
                         int nType, int nSlot, int nArg6, int nArg7,
                         int nArg8)
{
  // `shr eax, 1h / not eax / and eax, 1h` -- the value form of a bit test,
  // computed at the top of the function and used only by the last call.
  spos_t sX = PosS(nX);
  spos_t sY = PosS(nY);
  int nNotBit1 = !(gulUnitTypeFlags[(WORD)nType] & 2);
  int nCellW = gUnitPlaceboxSize[(WORD)nType].x / 32;
  int nResult = 3;
  int nCellH = gUnitPlaceboxSize[(WORD)nType].y / 32;
  point16_t size;

  size.x = (short)nCellW;
  size.y = (short)nCellH;

  if (sX >= 0 && sY >= 0 && size.x + sX <= gwMapWidth &&
      size.y + sY <= gwMapHeight)
  {
    nResult = 7;

    if (!pUnit || (pUnit->dwFlags & 6) ||
        sai_path_can_reach_target(pUnit, sX * 32, sY * 32))
    {
      if (IsBuiltRefineryType(nType))
      {
        nResult = sub_413610(sX, sY, bPlayer, nArg6);
      }
      else
      {
        int nNear;
        CUnit *pOwner;
        int nSpread;

        if (gulUnitTypeFlags[(WORD)nType] & 0x80000)
        {
          nResult = mtx_query_powered(nType, bPlayer, sX, sY);

          if (nResult)
            goto done;
        }

        if (pUnit && pUnit->wType == 0x86)
          nNear = sub_412720(sX, sY, bPlayer, size, nSlot, nArg6);
        else if (gulUnitTypeFlags[(WORD)nType] & 0x20000)
          nNear = mtx_check_on_creep(sX, sY, bPlayer, size, nSlot, nArg6, nArg8);
        else
          nNear = mtx_check_near(nType, sX, sY, bPlayer, size, nSlot, nArg6);

        pOwner = pUnit;

        if (dword_6A2E80)
          pOwner = dword_6A2E80;

        nSpread = sub_412360(pOwner, nX, nY, bPlayer, nType, size, nSlot,
                             nNotBit1, nArg7, nArg8);

        if (nNear > nSpread)
          nSpread = nNear;

        return nSpread;
      }
    }
  }

done:
  memset(byte_675188[nSlot], nResult, 48);
  return nResult;
}

// ---------------------------------------------------------------------------
// 0x413150  sub_413150  (195 B)
//
// The same shape as mtx_query_place with a real builder unit, no result row and no
// unit-type flag: the off-map tail is a bare `return 3`.
int sub_413150(CUnit *pUnit, BYTE bPlayer, int nX, int nY, WORD nUnitType)
{
  point16_t ptSize;
  int nNear;
  int nFar;
  spos_t sX = PosS(nX);
  spos_t sY = PosS(nY);

  ptSize.x = gUnitPlaceboxSize[(WORD)nUnitType].x / 32;
  ptSize.y = gUnitPlaceboxSize[(WORD)nUnitType].y / 32;

  if (sX < 0 || sY < 0
      || sX + ptSize.x > gwMapWidth
      || sY + ptSize.y > gwMapHeight)
    return 3;

  nNear = mtx_check_near(nUnitType, sX, sY, bPlayer, ptSize, 0, 1);
  nFar = sub_412360(pUnit, nX, nY, bPlayer, nUnitType, ptSize, 0, 0, 0, 0);

  return (nNear > nFar) ? nNear : nFar;
}

// ---------------------------------------------------------------------------
// 0x4130F0. The psi-power query: it converts a build-cell pair to the pixel
// centre of the placement box for wType and asks IsCellPowered, returning 0 on
// success and 6 on failure. The tail is the compiler's branchless form of that
// select, not a bit trick of the source.
//
// The type and both coordinates are 16-bit by measurement -- either spelled int
// compiles several bytes short -- while the player's width comes from
// IsCellPowered, which declares it a byte; here the value is only forwarded, so
// any width matches. The two placebox members have to be named locals, declared
// y before x: the oracle loads both up front and narrows each at its use, which
// reading them inline does not reproduce.
int mtx_query_powered(WORD wType, BYTE bPlayer, short sCellX, short sCellY)
{
  short sBoxY = gUnitPlaceboxSize[wType].y;
  short sBoxX = gUnitPlaceboxSize[wType].x;

  return IsCellPowered(wType,
                       sCellX * 32 + sBoxX / 2,
                       sCellY * 32 + sBoxY / 2,
                       bPlayer) ? 0 : 6;
}

// ---------------------------------------------------------------------------
// 0x413220. The masked predicate in pixel coordinates: mtx_has_flags with the
// coordinates converted to build cells by a signed divide by 32, which rounds
// toward zero where a shift would not. The y division is emitted first because
// it feeds the multiply. The operand order survives into the `test` here too,
// the stack slot first, so each parameter is written on the left of its `&`.
BOOL mtx_has_flags_px(spos_t sPixelX, spos_t sPixelY, DWORD dwRequired, DWORD dwForbidden)
{
  DWORD dwCell = gpSquareMap[gwMapWidth * (sPixelY / 32) + sPixelX / 32];

  if ((dwRequired & dwCell) && !(dwForbidden & dwCell))
    return 1;

  return 0;
}

// ---------------------------------------------------------------------------
// 0x413270. The masked predicate: true when the cell carries at least one
// required flag and no forbidden one. The source's operand order survives into
// the `test`, the stack slot first, so each parameter goes on the left of its `&`.
BOOL mtx_has_flags(short sX, short sY, DWORD dwRequired, DWORD dwForbidden)
{
  DWORD dwCell = gpSquareMap[gwMapWidth * sY + sX];

  if ((dwRequired & dwCell) && !(dwForbidden & dwCell))
    return 1;

  return 0;
}

// ---------------------------------------------------------------------------
// 0x4132B0. Finds the unit of type wType whose sprite occupies the build cell
// containing the pixel (sX, sY). The list comes from get_units_at_point and has
// to be released with finish_unit_iteration on both exits.
//
// The cell locals are 16-bit and the oracle proves both halves of that: the store
// truncates into a 4-byte slot and every read sign-extends, which is short and
// not WORD. CSprite::wPosX is declared WORD in our header and the oracle reads it
// signed, so that one site needs an explicit cast; wPosY is already short.
//
// The cursor copy has to be declared before the cell locals, and that one line is
// the whole delta: declared after them, the compiler enregisters the WORD
// parameter for the life of the function and the oracle's home slot and
// loop-preheader reload both vanish. Twelve other shapes give the identical wrong
// body, so this is not a matter of how the loop is spelled.
CUnit *sub_4132B0(WORD wType, spos_t sX, spos_t sY)
{
  CUnit *pFound = NULL;
  CUnit **ppList = get_units_at_point(sX, sY);
  CUnit **pp = ppList;
  short sCellX = sX / 32;
  short sCellY = sY / 32;

  while (*pp)
  {
    CUnit *pU = *pp;

    if (pU->wType == wType)
    {
      CSprite *pSprite = pU->pSprite;

      if (PosS(pSprite->wPosX) / 32 == sCellX
          && pSprite->wPosY / 32 == sCellY)
      {
        pFound = pU;
        break;
      }
    }

    pp++;
  }

  finish_unit_iteration(ppList);
  return pFound;
}

// ---------------------------------------------------------------------------
// 0x413370. The unit-type callback. `shr eax, 0Dh / and eax, 1h` is the plain
// spelling of `(flags >> 13) & 1` rather than a mask and compare against 0x2000.
BOOL is_resource_container_cb(CUnit *pUnit, CUnit *pContext)
{
  return (gulUnitTypeFlags[pUnit->wType] >> 13) & 1;
}

// ---------------------------------------------------------------------------
// 0x413390. Walks a candidate list and reports whether any of them blocks the
// build cell: 0 for nothing there, 1 for an enemy of pUnit's player, 4 for a
// neutral or own unit. The occupancy test is against the unit-finder orderings,
// in both axes.
//
// Two shapes carry the delta. bEnemy is an if/else and not a ternary, which
// otherwise produces the compiler's carry-form boolean and hoists the constant
// out of the loop, evicting pUnit from its register. And the flags test is a
// continue rather than an early return: only its branch target says so, landing
// on the advance block and not on the exit two instructions later.
//
// CUnit +0x13C through +0x148 are four dword unit-finder indices, inside
// CUnit.h's `_unmapped_138[0x18]` placeholder and reached through it here. Each
// indexes the x or y ordering at a stride of 8, which fixes both the width and
// the meaning: the first pair is x, the second y.
int mtx_unit_there(CUnit *pUnit, CUnit **ppList, int nX, int nY,
                   BYTE bArg4, BYTE bArg5)
{
  CUnit *pU;

  while ((pU = *ppList) != NULL)
  {
    int bEnemy;

    if (pUnit != NULL
        && (pU->dwFlags & 0x300)
        && !UnitDetectedBy(pU, pUnit->bPlayer))  // Hyperion: every slot (stage D)
      bEnemy = 1;
    else
      bEnemy = 0;

    if ((bArg4 != 0 || !(pU->dwFlags & 0x20000))
        && pU != pUnit
        && !(pU->dwFlags & 6)
        && pU->wType != 0xCA
        && pU->wType != 0x69
        && (!bEnemy || bArg5 != 0)
        && !(pU->pSprite->flags.b & 0x20)
        && g_pos_sort_x[*(DWORD *)&pU->_unmapped_138[0x04]].nCoord < nX * 32 + 32
        && g_pos_sort_x[*(DWORD *)&pU->_unmapped_138[0x08]].nCoord > nX * 32
        && g_pos_sort_y[*(DWORD *)&pU->_unmapped_138[0x0C]].nCoord < nY * 32 + 32
        && g_pos_sort_y[*(DWORD *)&pU->_unmapped_138[0x10]].nCoord > nY * 32)
      return bEnemy ? 1 : 4;

    ppList++;
  }

  return 0;
}

// ---------------------------------------------------------------------------
// 0x4134A0. The clearing twin of mtx_place_bldg: the complement taken once at the
// top, then ANDed into every cell. It is an explicit local rather than a hoisted
// `~dwFlags`, since the oracle materialises it before the height guard where a
// hoist would sink it into the loop preheader.
//
// The mask has to be declared before pCell: the value materialised first takes
// the one free volatile register, and every other order leaves the mask there and
// rotates the two bounds through the saved registers, for extra loads and a pad.
void mtx_unplace_bldg(int nX, int nY, int nWidth, int nHeight, DWORD dwFlags)
{
  DWORD dwMask = ~dwFlags;
  DWORD *pCell = &gpSquareMap[gwMapWidth * nY + nX];
  int nAdvance = gwMapWidth - nWidth;

  while (nHeight)
  {
    nHeight--;

    for (int c = nWidth; c; c--)
      *pCell++ &= dwMask;

    pCell += nAdvance;
  }
}

// ---------------------------------------------------------------------------
// 0x4134F0. The reverse rect scan with the polarity inverted: true as soon as any
// cell of the rect carries bit (bBit + 8), false if the scan completes, so an
// empty or inverted rect returns false, since the loop-entry tests are the
// guards. The `+ 8` is in the bytes.
//
// Both of the twin's shapes hold: the bounds go inline in the for-inits, which
// is what sinks the x bound past the outer guard and reloads it each row, and the
// array is indexed in the inner condition rather than walked by a pointer.
// Hoisting the shift into a local costs 6 bytes, since the oracle rebuilds it at
// the top of each row from the slot it spilled the sum to.
BOOL sub_4134F0(short sX, short sY, short sWidth, short sHeight, BYTE bBit)
{
  for (int y = sY + sHeight - 1; y >= sY; y--)
  {
    for (int x = sX + sWidth - 1; x >= sX; x--)
    {
      if (gpSquareMap[gwMapWidth * y + x] & (1 << (bBit + 8)))
        return 1;
    }
  }

  return 0;
}

// ---------------------------------------------------------------------------
// 0x413580. The reverse rect scan: true when every cell of the rect carries bit
// bBit, scanning backwards from the far corner down to (sX, sY).
//
// An empty or inverted rect returns true, and reading that backwards is how this
// body first came out as a wrong program at nearly the right size. The two `jl`
// tests look like early `return 0` guards and are not guards at all: they are the
// loops' own entry tests, and both land on the `mov eax, 1h` tail.
//
// The mask `1 << bBit` is rebuilt at the top of every row rather than hoisted,
// and the row base is recomputed there too; only the inner walk is
// strength-reduced.
BOOL sub_413580(short sX, short sY, short sWidth, short sHeight, BYTE bBit)
{
  for (int y = sY + sHeight - 1; y >= sY; y--)
  {
    for (int x = sX + sWidth - 1; x >= sX; x--)
    {
      if (!(gpSquareMap[gwMapWidth * y + x] & (1 << bBit)))
        return 0;
    }
  }

  return 1;
}

// ---------------------------------------------------------------------------
// 0x412C90, held: our body is the oracle's program instruction for instruction
// from the clamps to the two epilogues, differing only in which register holds
// which value and in a pad the compiler inserts ahead of the first loop.
//
// It returns true only when every cell of the ring around the unit's footprint
// carries square-map bit 0x8000000: a walk along the row above and the row below,
// then down the column left and right. The two clamp differently, and that is in
// the bytes: the horizontal walk clamps its rows and starts at the unclamped left
// cell, the vertical one the reverse.
//
// The difficulty is that the oracle reads gUnitBoundBox eight times and does the
// four edge computations in 16-bit registers. Written as four short locals here,
// the compiler hoists all four into 32-bit registers and narrows at the divide;
// the 16-bit form appears only behind a function boundary doing its own sprite
// and type reads, which CUnit::GetBounds (0x409CC0) is and the stand-in mimics.
#if !defined(OC_PAYLOAD) || defined(OC_WITH_HELD)
static void w73a_bounds(CUnit *pUnit, mrect_t *pRect)
{
  CSprite *pSprite = pUnit->pSprite;
  WORD wType = pUnit->wType;
  upos_t nPosY = pSprite->wPosY;
  upos_t nPosX = pSprite->wPosX;

  pRect->top    = nPosY - gUnitBoundBox[wType].top;
  pRect->bottom = nPosY + gUnitBoundBox[wType].bottom;
  pRect->left   = nPosX - gUnitBoundBox[wType].left;
  pRect->right  = nPosX + gUnitBoundBox[wType].right;
}

BOOL sub_412C90(CUnit *pUnit)
{
  mrect_t box;  // Hyperion: a box of map positions

  w73a_bounds(pUnit, &box);

  WORD wType = pUnit->wType;

  WORD wW = gUnitBoundBox[wType].right;
  wW++;
  wW += gUnitBoundBox[wType].left;
  int nCellsWide = (short)wW / 32;

  WORD wH = gUnitBoundBox[wType].top;
  wH++;
  wH += gUnitBoundBox[wType].bottom;
  int nCellsHigh = (short)wH / 32;

  int nTopCell  = box.top / 32;
  int nTop      = nTopCell - 1;
  int nBottom   = box.bottom / 32 + 1;
  int nLeftCell = box.left / 32;
  int nLeft     = nLeftCell - 1;
  int nRight    = box.right / 32 + 1;

  if (nTop < 0)
    nTop = 0;
  if (nBottom >= gwMapHeight)
    nBottom = gwMapHeight - 1;
  if (nLeft < 0)
    nLeft = 0;
  if (nRight >= gwMapWidth)
    nRight = gwMapWidth - 1;

  DWORD *pTop = &gpSquareMap[gwMapWidth * nTop + nLeftCell];
  DWORD *pBottom = &gpSquareMap[gwMapWidth * nBottom + nLeftCell];

  while (nCellsWide)
  {
    nCellsWide--;

    if (!(*pTop & 0x8000000))
      return 0;

    if (!(*pBottom & 0x8000000))
      return 0;

    pTop++;
    pBottom++;
  }

  DWORD *pLeft = &gpSquareMap[gwMapWidth * nTopCell + nLeft];
  DWORD *pRight = &gpSquareMap[gwMapWidth * nTopCell + nRight];

  while (nCellsHigh)
  {
    nCellsHigh--;

    if (!(*pLeft & 0x8000000))
      return 0;

    if (!(*pRight & 0x8000000))
      return 0;

    pLeft += gwMapWidth;
    pRight += gwMapWidth;
  }

  return 1;
}
#endif  // OC_PAYLOAD

// 0x413610. The refinery placement query: is the geyser exactly where the
// refinery would go? The placebox entry for the type is read as a whole 4-byte
// struct, because sub_418000 takes one by value; the compiler then promotes it
// into a register and shifts the two halves out, as it does for sub_412720's and
// mtx_check_on_creep's own size parameter.
//
// The two `if (... == 2)` are separate on purpose: the oracle re-compares the
// slot-type byte after the first call rather than threading the test away. And
// `if (nFlag) { ... } return 0;` is what puts the shared return-0 block after the
// return-8 block, where an early return on the negation reverses them.
//
// Caching that byte in a local folds the second compare away, and reading it
// twice shares the index instead of the value; the oracle loads it once, spills
// it, and reloads the spill after the call. The body also depends on sub_413580
// and sub_4134F0 being defined here, though not through inlining.
int sub_413610(int nCellX, int nCellY, BYTE bPlayer, int nFlag)
{
  point16_t size = gUnitPlaceboxSize[0xBC];
  int nPX = nCellX * 32 + size.x / 2;
  int nPY = nCellY * 32 + size.y / 2;

  if (gPlayerData[bPlayer].nType == 2)
  {
    if (sub_4134F0(nCellX, nCellY, size.x / 32, size.y / 32, bPlayer))
      return 10;
  }

  if (gPlayerData[bPlayer].nType == 2)
  {
    if (sub_413580(nCellX, nCellY, size.x / 32, size.y / 32, bPlayer))
    {
      if (nFlag)
      {
        CThingy *pThingy = sub_418000(0x113, nPX, nPY, size);

        // One `return 0` and textually last: with the inner one kept as well,
        // the two exit blocks come out in the opposite order to the oracle's.
        if (pThingy == NULL || PosS(pThingy->pSprite->wPosX) != nPX ||
            pThingy->pSprite->wPosY != nPY)
          return 8;
      }

      return 0;
    }
  }

  return sub_4132B0(0xBC, nPX, nPY) ? 0 : 8;
}

