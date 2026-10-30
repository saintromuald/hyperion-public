// CUnitFind.cpp -- finding units by position, region and range: the point, rect
// and region searches over the two bound orderings, the nested-iteration stack
// they run on, and the box-to-box distance bodies. Bodies are in oracle address
// order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>   // memset, for the finder array clears

#include "../h/CUnitFind.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/CUnitBounds.h"
#include "../h/CUnitSearches.h"
#include "../h/sai.h"
#include "../h/order.h"
#include "../h/CTarget.h"
#include "../h/rect16.h"
#include "../gen/globals117.h"
#include <algorithm>  // Hyperion: std::sort
#include <vector>

// The units.dat flag the distance bodies test, from
// `test byte ptr [edx*4+521660h], 10h`.
#define UNIT_FLAG_SUBUNIT 0x10

// The globals this TU reaches, dual-spelled, one #ifdef per name.

#ifdef OC_DLL_MODE
#define sgdwMaxSizeX_g   (*reinterpret_cast<DWORD *>(OC_VA(0x5D6E40u)))
#define sgdwMaxSizeY_g   (*reinterpret_cast<DWORD *>(OC_VA(0x5D6E44u)))
#define sgOpenLists      (*reinterpret_cast<int *>(OC_VA(0x5D6E48u)))
#define dword_5D6E4C     (reinterpret_cast<int *>(OC_VA(0x5D6E4Cu)))
#define g_find_idx_begin (*reinterpret_cast<int *>(OC_VA(0x5D6E5Cu)))
#define g_find_arr_flag  (reinterpret_cast<int *>(OC_VA(0x5D6E60u)))
#define g_find_arr_unit  (reinterpret_cast<CUnit **>(OC_VA(0x5D88F8u)))
#define g_find_flag      (*reinterpret_cast<int *>(OC_VA(0x5DA38Cu)))
#define g_pos_sort_x     (reinterpret_cast<pos_sort_ent_t *>(OC_VA(0x5C99B8u)))
#define g_pos_sort_y     (reinterpret_cast<pos_sort_ent_t *>(OC_VA(0x5D03F8u)))
#define sgUnitsMem       (reinterpret_cast<CUnit *>(OC_VA(0x5DA3A0u)))
#else
extern DWORD sgdwMaxSizeX_g;
extern DWORD sgdwMaxSizeY_g;
extern int sgOpenLists;
extern int dword_5D6E4C[4] OC_UNALIGNED;
extern int g_find_idx_begin;
extern int g_find_arr_flag[POOL_UNITS_STORE + 1];
extern CUnit *g_find_arr_unit[POOL_UNITS_STORE + 1] OC_UNALIGNED;
extern int g_find_flag;
extern pos_sort_ent_t g_pos_sort_x[POOL_FINDER_STORE];
extern pos_sort_ent_t g_pos_sort_y[POOL_FINDER_STORE];
extern CUnit sgUnitsMem[POOL_UNITS_STORE];
#endif
#ifdef OC_DLL_MODE
#define CUnit__nSorted   (*reinterpret_cast<int *>(OC_VA(0x5C99B0u)))
#else
extern int CUnit__nSorted;
#endif

// CUnit +0x13C/+0x140/+0x144/+0x148 are the four unit-finder bound indices,
// still inside CUnit.h's `_unmapped_138[0x18]` and reached through it here.
// +0x13C/+0x140 index g_pos_sort_x, +0x144/+0x148 index g_pos_sort_y.
#define BOUND_LEFT(u)   (*reinterpret_cast<int *>(&(u)->_unmapped_138[0x04]))
#define BOUND_RIGHT(u)  (*reinterpret_cast<int *>(&(u)->_unmapped_138[0x08]))
#define BOUND_TOP(u)    (*reinterpret_cast<int *>(&(u)->_unmapped_138[0x0C]))
#define BOUND_BOTTOM(u) (*reinterpret_cast<int *>(&(u)->_unmapped_138[0x10]))

// The same four indices read unsigned. Kept separate from BOUND_* above: a
// comparison's signedness is a one-byte lever, so the two spellings are not
// interchangeable.
#define UNIT_BOUND_LEFT(u)   (*reinterpret_cast<DWORD *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x04))
#define UNIT_BOUND_RIGHT(u)  (*reinterpret_cast<DWORD *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x08))
#define UNIT_BOUND_TOP(u)    (*reinterpret_cast<DWORD *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x0C))
#define UNIT_BOUND_BOTTOM(u) (*reinterpret_cast<DWORD *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x10))

// The unit's collision box: its sprite position widened by the per-type
// extents. Same four statements as collide.cpp's w73a_bounds, which is what
// keeps the arithmetic 16-bit; wPosY is read before wPosX and pDim after both,
// or the two coordinates swap registers and the caller's arms tail-merge.
static void unit_bounds(CUnit *pUnit, mrect_t *pBox)
{
  CSprite *pSprite = pUnit->pSprite;
  upos_t wPosY = pSprite->wPosY;
  upos_t wPosX = pSprite->wPosX;
  const rect16_t *pDim = &gUnitBoundBox[pUnit->wType];

  pBox->top    = wPosY - pDim->top;
  pBox->bottom = wPosY + pDim->bottom;
  pBox->left   = wPosX - pDim->left;
  pBox->right  = wPosX + pDim->right;
}

// The unit ids in the two bound orderings are one-based, and zero means no
// unit; same spelling as CUnitBounds.cpp so both TUs read the slot alike.
static CUnit *unit_from_bound_id(DWORD dwUnitId)
{
  return dwUnitId == 0 ? 0 : CUnit::Head() + dwUnitId - 1;
}

// oracle 0x4217F0 (1002 B). The units a move by (nArg0, nArg1) would sweep into:
// one outward scan per axis, on the side the delta's sign picks, each first
// backing up over the entries that share the starting coordinate.
// The bases share one short[4] (see GetUnitsInRectangle); the first scan's limit
// is aEdge[2] itself. The deltas are if/else locals, not the parameters.
intptr_t CUnit::sub_4217F0(int nArg0, int nArg1)
{
  CUnit **ppHead;
  CUnit **ppWrite;
  short aEdge[4];
  int nDx;
  int nDy;
  int i;
  point16_t lim;
  point16_t lh;

  if ((this->pSprite->flags.b & 0x20) != 0) {
    nDy = 0;
    nDx = 0;
  } else {
    nDx = nArg0;
    nDy = nArg1;
  }

  dword_5D6E4C[sgOpenLists] = g_find_idx_begin;
  ++sgOpenLists;
  ppHead = &g_find_arr_unit[g_find_idx_begin];
  ppWrite = ppHead;

  if ((short)nDx > 0) {
    aEdge[0] = g_pos_sort_x[UNIT_BOUND_RIGHT(this)].nCoord;
    aEdge[2] = aEdge[0] + nDx;
    lh.x = g_pos_sort_y[UNIT_BOUND_TOP(this)].nCoord + nDy;
    lh.y = g_pos_sort_y[UNIT_BOUND_BOTTOM(this)].nCoord + nDy;
    i = UNIT_BOUND_RIGHT(this);
    do {
      --i;
    } while (g_pos_sort_x[i].nCoord == aEdge[0]);
    for (++i; i < CUnit__nSorted; i++) {
      CUnit *pUnit;

      if (g_pos_sort_x[i].nCoord > aEdge[2]) {
        break;
      }
      pUnit = unit_from_bound_id(g_pos_sort_x[i].dwUnitId);
      if (pUnit != this
          && g_pos_sort_y[UNIT_BOUND_BOTTOM(pUnit)].nCoord >= lh.x
          && g_pos_sort_y[UNIT_BOUND_TOP(pUnit)].nCoord <= lh.y) {
        *ppWrite = pUnit;
        ++ppWrite;
      }
    }
  } else if ((short)nDx < 0) {
    aEdge[2] = g_pos_sort_x[UNIT_BOUND_LEFT(this)].nCoord;
    lim.x = aEdge[2] + nDx;
    lh.x = g_pos_sort_y[UNIT_BOUND_TOP(this)].nCoord + nDy;
    lh.y = g_pos_sort_y[UNIT_BOUND_BOTTOM(this)].nCoord + nDy;
    i = UNIT_BOUND_LEFT(this);
    do {
      ++i;
    } while (g_pos_sort_x[i].nCoord == aEdge[2]);
    for (--i; i >= 0; i--) {
      CUnit *pUnit;

      if (g_pos_sort_x[i].nCoord < lim.x) {
        break;
      }
      pUnit = unit_from_bound_id(g_pos_sort_x[i].dwUnitId);
      if (pUnit != this
          && g_pos_sort_y[UNIT_BOUND_BOTTOM(pUnit)].nCoord >= lh.x
          && g_pos_sort_y[UNIT_BOUND_TOP(pUnit)].nCoord <= lh.y) {
        *ppWrite = pUnit;
        ++ppWrite;
      }
    }
  }

  if ((short)nDy > 0) {
    aEdge[1] = g_pos_sort_y[UNIT_BOUND_BOTTOM(this)].nCoord;
    lim.x = aEdge[1] + nDy;
    lh.x = g_pos_sort_x[UNIT_BOUND_LEFT(this)].nCoord + nDx;
    lh.y = g_pos_sort_x[UNIT_BOUND_RIGHT(this)].nCoord + nDx;
    i = UNIT_BOUND_BOTTOM(this);
    do {
      --i;
    } while (g_pos_sort_y[i].nCoord == aEdge[1]);
    for (++i; i < CUnit__nSorted; i++) {
      CUnit *pUnit;

      if (g_pos_sort_y[i].nCoord > lim.x) {
        break;
      }
      pUnit = unit_from_bound_id(g_pos_sort_y[i].dwUnitId);
      if (pUnit != this
          && g_pos_sort_x[UNIT_BOUND_RIGHT(pUnit)].nCoord >= lh.x
          && g_pos_sort_x[UNIT_BOUND_LEFT(pUnit)].nCoord <= lh.y) {
        *ppWrite = pUnit;
        ++ppWrite;
      }
    }
  } else if ((short)nDy < 0) {
    aEdge[3] = g_pos_sort_y[UNIT_BOUND_TOP(this)].nCoord;
    lim.x = aEdge[3] + nDy;
    lh.x = g_pos_sort_x[UNIT_BOUND_LEFT(this)].nCoord + nDx;
    lh.y = g_pos_sort_x[UNIT_BOUND_RIGHT(this)].nCoord + nDx;
    i = UNIT_BOUND_TOP(this);
    do {
      ++i;
    } while (g_pos_sort_y[i].nCoord == aEdge[3]);
    for (--i; i >= 0; i--) {
      CUnit *pUnit;

      if (g_pos_sort_y[i].nCoord < lim.x) {
        break;
      }
      pUnit = unit_from_bound_id(g_pos_sort_y[i].dwUnitId);
      if (pUnit != this
          && g_pos_sort_x[UNIT_BOUND_RIGHT(pUnit)].nCoord >= lh.x
          && g_pos_sort_x[UNIT_BOUND_LEFT(pUnit)].nCoord <= lh.y) {
        *ppWrite = pUnit;
        ++ppWrite;
      }
    }
  }

  *ppWrite = NULL;
  g_find_idx_begin = (int)(ppWrite + 1 - g_find_arr_unit);
  return (intptr_t)ppHead;
}

// oracle 0x421BE0 (149 B). Clear the finder's arrays and the nested-iteration
// stack, then cache the widest and tallest unit bounding box.
//
// The box pointer stays an unnamed subexpression: a local for it moves the
// induction anchor off the record's first field. The two closing stores are
// 16 bits wide although every reader of the globals loads four.
void cunitfind_init()
{
  int i;
  int nW;
  int nH;
  WORD wMaxX;
  WORD wMaxY;

  g_find_idx_begin = 0;
  memset(g_find_arr_flag, 0, (g_pools.units + 1) * sizeof(g_find_arr_flag[0]));
  memset(g_find_arr_unit, 0, (g_pools.units + 1) * sizeof(g_find_arr_unit[0]));  // Hyperion: 0x1A94 in 1.17
  sgOpenLists = 0;
  memset(dword_5D6E4C, 0, 16);

  wMaxX = 0;
  wMaxY = 0;
  i = 228;
  do {
    --i;
    nW = gUnitBoundBox[i].left + gUnitBoundBox[i].right + 1;
    nH = gUnitBoundBox[i].top + gUnitBoundBox[i].bottom + 1;
    if (nW > wMaxX) {
      wMaxX = (WORD)nW;
    }
    if (nH > wMaxY) {
      wMaxY = (WORD)nH;
    }
  } while (i != 0);

  *reinterpret_cast<WORD *>(&sgdwMaxSizeX_g) = wMaxX;
  *reinterpret_cast<WORD *>(&sgdwMaxSizeY_g) = wMaxY;
}

// oracle 0x421C80 (309 B). Of the units near (nArg0, nArg1), keep the ones
// whose bounding box would touch this unit's box placed at that point, and
// compact the list in place.
//
// get_units_in_rect (oracle 0x422330) has to be defined in this TU: only then
// does MSVC see that the callee does not capture &rc and keep the frame at 0x18,
// against 0x1C with an external declaration. The unit's four edges go through a
// rect16_t local, since four loose shorts let MSVC sink each edge into the arm of
// the test that reads it, twenty bytes more; the two half-extents stay unnamed
// subexpressions, where a WORD local re-widens each one with a movzx.
//
intptr_t CUnit::sub_421C80(int nArg0, int nArg1)
{
  rect16_t rc;
  CUnit **ppList;
  CUnit **ppWrite;
  CUnit **ppRead;
  CUnit *pUnit;

  rc.left = (short)(nArg0 - ((WORD)sgdwMaxSizeX_g >> 1) - 1);
  rc.top = (short)(nArg1 - ((WORD)sgdwMaxSizeY_g >> 1) - 1);
  rc.right = (short)(nArg0 + 1 + ((WORD)sgdwMaxSizeX_g >> 1));
  rc.bottom = (short)(nArg1 + 1 + ((WORD)sgdwMaxSizeY_g >> 1));

  ppList = get_units_in_rect(&rc);

  ppWrite = ppList;
  ppRead = ppList;
  pUnit = *ppList;
  while (pUnit != NULL) {
    CSprite *pSprite = pUnit->pSprite;
    spos_t sPosY = pSprite->wPosY;
    spos_t sPosX = pSprite->wPosX;
    rect16_t *pBox = &gUnitBoundBox[pUnit->wType];
    mrect_t  rcU;

    rcU.top = PosS(sPosY - pBox->top);
    rcU.bottom = PosS(pBox->bottom + sPosY);
    rcU.left = PosS(sPosX - pBox->left);
    rcU.right = PosS(pBox->right + sPosX);

    rcU.left = PosS(rcU.left - gUnitBoundBox[this->wType].right - 1);
    rcU.right = PosS(rcU.right + gUnitBoundBox[this->wType].left + 1);
    rcU.top = PosS(rcU.top - gUnitBoundBox[this->wType].bottom - 1);
    rcU.bottom = PosS(rcU.bottom + gUnitBoundBox[this->wType].top + 1);

    if (rcU.left <= PosS(nArg0) && rcU.right >= PosS(nArg0)
        && rcU.top <= PosS(nArg1) && rcU.bottom >= PosS(nArg1)) {
      *ppWrite = pUnit;
      ++ppWrite;
    }

    ++ppRead;
    pUnit = *ppRead;
  }

  *ppWrite = NULL;
  return (intptr_t)ppList;
}

// oracle 0x421DC0 (910 B). The units whose bound entries overlap this unit's,
// walking out from its four bound entries until the limit grown by (nArg0, nArg1).
// The four bases share one short[4] so each stays in memory for the dword-read
// limit add; the first scan's upper bound is aEdge[3] itself. Lo, hi and the
// limit are point16_t fields so they keep two-byte homes.
intptr_t CUnit::GetUnitsInRectangle(int nArg0, int nArg1)
{
  CUnit **ppHead;
  CUnit **ppWrite;
  int i;
  point16_t lim;
  short aEdge[4];

  dword_5D6E4C[sgOpenLists] = g_find_idx_begin;
  ++sgOpenLists;
  ppHead = &g_find_arr_unit[g_find_idx_begin];
  ppWrite = ppHead;

  if ((this->pSprite->flags.b & 0x20) != 0) {
    *ppWrite = NULL;
    g_find_idx_begin = (int)(ppWrite + 1 - g_find_arr_unit);
    return (intptr_t)ppHead;
  }

  {
    point16_t lh;

    aEdge[0] = g_pos_sort_x[UNIT_BOUND_RIGHT(this)].nCoord;
    lim.x = nArg0 + aEdge[0];
    lh.x = g_pos_sort_y[UNIT_BOUND_TOP(this)].nCoord - nArg1;
    aEdge[3] = g_pos_sort_y[UNIT_BOUND_BOTTOM(this)].nCoord + nArg1;
    for (i = UNIT_BOUND_RIGHT(this) + 1; i < CUnit__nSorted; i++) {
      CUnit *pUnit;

      if (g_pos_sort_x[i].nCoord >= lim.x) {
        break;
      }
      pUnit = unit_from_bound_id(g_pos_sort_x[i].dwUnitId);
      if (pUnit != this
          && g_pos_sort_y[UNIT_BOUND_BOTTOM(pUnit)].nCoord >= lh.x
          && g_pos_sort_y[UNIT_BOUND_TOP(pUnit)].nCoord < aEdge[3]) {
        *ppWrite = pUnit;
        ++ppWrite;
      }
    }
  }

  {
    point16_t lh;

    aEdge[2] = g_pos_sort_x[UNIT_BOUND_LEFT(this)].nCoord;
    lim.x = aEdge[2] - nArg0;
    lh.x = g_pos_sort_y[UNIT_BOUND_TOP(this)].nCoord - nArg1;
    lh.y = g_pos_sort_y[UNIT_BOUND_BOTTOM(this)].nCoord + nArg1;
    for (i = UNIT_BOUND_LEFT(this) - 1; i >= 0; i--) {
      CUnit *pUnit;

      if (g_pos_sort_x[i].nCoord < lim.x) {
        break;
      }
      pUnit = unit_from_bound_id(g_pos_sort_x[i].dwUnitId);
      if (pUnit != this
          && g_pos_sort_y[UNIT_BOUND_BOTTOM(pUnit)].nCoord >= lh.x
          && g_pos_sort_y[UNIT_BOUND_TOP(pUnit)].nCoord < lh.y) {
        *ppWrite = pUnit;
        ++ppWrite;
      }
    }
  }

  {
    point16_t lh;

    aEdge[1] = g_pos_sort_y[UNIT_BOUND_BOTTOM(this)].nCoord;
    lim.x = nArg1 + aEdge[1];
    lh.x = g_pos_sort_x[UNIT_BOUND_LEFT(this)].nCoord;
    lh.y = g_pos_sort_x[UNIT_BOUND_RIGHT(this)].nCoord;
    for (i = UNIT_BOUND_BOTTOM(this) + 1; i < CUnit__nSorted; i++) {
      CUnit *pUnit;

      if (g_pos_sort_y[i].nCoord >= lim.x) {
        break;
      }
      pUnit = unit_from_bound_id(g_pos_sort_y[i].dwUnitId);
      if (pUnit != this
          && g_pos_sort_x[UNIT_BOUND_RIGHT(pUnit)].nCoord >= lh.x
          && g_pos_sort_x[UNIT_BOUND_LEFT(pUnit)].nCoord < lh.y) {
        *ppWrite = pUnit;
        ++ppWrite;
      }
    }
  }

  {
    point16_t lh;

    aEdge[3] = g_pos_sort_y[UNIT_BOUND_TOP(this)].nCoord;
    lim.x = aEdge[3] - nArg1;
    lh.x = g_pos_sort_x[UNIT_BOUND_LEFT(this)].nCoord;
    lh.y = g_pos_sort_x[UNIT_BOUND_RIGHT(this)].nCoord;
    for (i = UNIT_BOUND_TOP(this) - 1; i >= 0; i--) {
      CUnit *pUnit;

      if (g_pos_sort_y[i].nCoord < lim.x) {
        break;
      }
      pUnit = unit_from_bound_id(g_pos_sort_y[i].dwUnitId);
      if (pUnit != this
          && g_pos_sort_x[UNIT_BOUND_RIGHT(pUnit)].nCoord >= lh.x
          && g_pos_sort_x[UNIT_BOUND_LEFT(pUnit)].nCoord < lh.y) {
        *ppWrite = pUnit;
        ++ppWrite;
      }
    }
  }

  *ppWrite = NULL;
  g_find_idx_begin = (int)(ppWrite + 1 - g_find_arr_unit);
  return (intptr_t)ppHead;
}

// oracle 0x422150 (147 B). Units whose sprite covers the point, searched over a
// +/-32 pixel box around it.
//
// The narrowed x is a local set after the entry test, so it takes EAX and nX's
// home [ebp-4]; with both conversions in the call the y one gets EAX instead.
CUnit **__fastcall get_units_at_cursor_point(int nX, int nY)
{
  mrect_t  rc;
  CUnit **ppList;
  CUnit **ppRead;
  CUnit **ppWrite;
  CUnit *pUnit;

  rc.left = PosS(nX - 32);
  rc.top = PosS(nY - 32);
  rc.right = PosS(nX + 32 + sgdwMaxSizeX_g);
  rc.bottom = PosS(nY + 32 + sgdwMaxSizeY_g);

  ppList = get_units_in_rect(&rc);
  ppRead = ppList;
  ppWrite = ppList;
  if ((pUnit = *ppRead) != NULL) {
    int x = PosS(nX);

    do {
      if (pUnit->pSprite->IsUnderPoint(x, PosS(nY)))
        *ppWrite++ = pUnit;
      ++ppRead;
    } while ((pUnit = *ppRead) != NULL);
  }

  *ppWrite = NULL;
  return ppList;
}

// oracle 0x4221F0 (182 B). Units whose collision box covers one pixel.
CUnit **__fastcall get_units_at_point(spos_t sX, spos_t sY)
{
  mrect_t  rc;
  CUnit **ppList;
  CUnit **ppRead;
  CUnit **ppWrite;
  CUnit *pUnit;

  rc.left = sX;
  rc.right = PosS(sX + sgdwMaxSizeX_g);
  rc.top = sY;
  rc.bottom = PosS(sY + sgdwMaxSizeY_g);

  ppList = get_units_in_rect(&rc);
  ppRead = ppList;
  ppWrite = ppList;
  while ((pUnit = *ppRead) != NULL) {
    if (g_pos_sort_x[BOUND_LEFT(pUnit)].nCoord <= sX
        && sX < g_pos_sort_x[BOUND_RIGHT(pUnit)].nCoord
        && g_pos_sort_y[BOUND_TOP(pUnit)].nCoord <= sY
        && sY < g_pos_sort_y[BOUND_BOTTOM(pUnit)].nCoord)
      *ppWrite++ = pUnit;
    ++ppRead;
  }

  *ppWrite = NULL;
  return ppList;
}

// oracle 0x4222B0 (24 B). Pop one level of the nested-iteration stack and
// restore the begin index the enclosing walk was using.
//
// The depth has to reach a named local: the oracle reuses the decremented
// value as the index. The list parameter is never read here, but 46 of the 47
// call sites set ECX, so it is real and the declaration keeps it.
void __fastcall finish_unit_iteration(CUnit ** /*ppList*/)
{
  int nOpen = --sgOpenLists;

  g_find_idx_begin = dword_5D6E4C[nOpen];
}

// oracle 0x4222D0 (83 B). Keep only the units whose own region is this one:
// search the region's bounding rectangle, then compact the list in place.
//
// The region compare is 16-bit because GetRegion_CUnit returns a WORD. Reading
// the walked unit out of the loop condition binds the read cursor to ESI and
// the write cursor to EDI.
CUnit **__fastcall get_units_in_region(WORD nRegion)
{
  rect16_t bounds;
  CUnit **ppList;
  CUnit **ppRead;
  CUnit **ppWrite;
  CUnit *pUnit;

  GetRegionBounds(nRegion, &bounds);
  ppList = get_units_in_rect(&bounds);

  ppRead = ppList;
  ppWrite = ppList;
  while ((pUnit = *ppRead) != NULL) {
    if (GetRegion_CUnit(pUnit) == nRegion)
      *ppWrite++ = *ppRead;
    ++ppRead;
  }

  *ppWrite = NULL;
  return ppList;
}

// oracle 0x422330 (292 B). Walk the x-ordered bounds list between the
// rectangle's edges, stamp each unit id once, and keep the units whose y bounds
// straddle the rectangle too.
//
// The loop reads g_find_flag itself, not a local copy. The rectangle's top is
// read between computing the stamp and storing it; either side costs two bytes.
// Hyperion: the walk's length past which the grid answers
enum { FIND_GRID_WALK = 96 };

CUnit **__fastcall get_units_in_rect(mrect_t *pRect)
{
  CUnit **ppList;
  CUnit **ppWrite;
  int nYBegin;
  int nYEnd;
  int nXIdx;
  int nRight;
  int nBottom;
  int nFlag;
  int nTop;

  dword_5D6E4C[sgOpenLists] = g_find_idx_begin;
  nBottom = pRect->bottom;
  ++sgOpenLists;

  ppList = &g_find_arr_unit[g_find_idx_begin];
  ppWrite = ppList;

  nFlag = g_find_flag + 1;
  if (nFlag == 0)
    nFlag = 1;
  nTop = pRect->top;
  g_find_flag = nFlag;
  nRight = pRect->right;

  nYBegin = SearchFinder(g_pos_sort_y, nTop, 1);
  nYEnd = nYBegin;
  while (nYEnd < CUnit__nSorted && g_pos_sort_y[nYEnd].nCoord < nBottom)
    ++nYEnd;

  nXIdx = SearchFinder(g_pos_sort_x, pRect->left, 1);

  // Hyperion: a long walk is answered from the grid (FinderGridRect): the units
  // it keeps, in the order of the entry it meets each at first, stamped as it stamps them. The
  // units it walks past without keeping are not stamped; a stamp is read only by the walk that
  // made it.
  if (nYEnd > nYBegin && !g_bFinderSideCheck && SearchFinder(g_pos_sort_x, nRight, 1) - nXIdx > FIND_GRID_WALK) {
    static std::vector<finder_kept_t> s_kept;
    if (s_kept.size() < static_cast<size_t>(g_pools.units))
      s_kept.resize(g_pools.units);
    const int n = FinderGridRect(pRect->left, nTop, nRight, nBottom, s_kept.data());
    std::sort(s_kept.begin(), s_kept.begin() + n,
              [](const finder_kept_t &a, const finder_kept_t &b) { return a.nKey < b.nKey; });
    for (int k = 0; k < n; ++k) {
      g_find_arr_flag[s_kept[k].nId] = g_find_flag;
      *ppWrite++ = &sgUnitsMem[s_kept[k].nId - 1];
    }
    *ppWrite = NULL;
    g_find_idx_begin = (int)(ppWrite + 1 - g_find_arr_unit);
    return ppList;
  }
  // HYPERION_FINDER_CHECK: the grid's answer, to compare with the walk's below
  static std::vector<finder_kept_t> s_check;
  int nCheck = -1;
  if (g_bFinderSideCheck && nYEnd > nYBegin) {
    if (s_check.size() < static_cast<size_t>(g_pools.units))
      s_check.resize(g_pools.units);
    nCheck = FinderGridRect(pRect->left, nTop, nRight, nBottom, s_check.data());
    std::sort(s_check.begin(), s_check.begin() + nCheck,
              [](const finder_kept_t &a, const finder_kept_t &b) { return a.nKey < b.nKey; });
  }

  if (nYEnd > nYBegin) {
    while (nXIdx < CUnit__nSorted) {
      int nId;
      CUnit *pUnit;

      if (g_pos_sort_x[nXIdx].nCoord >= nRight)
        break;
      nId = (int)g_pos_sort_x[nXIdx].dwUnitId;
      if (g_find_arr_flag[nId] != g_find_flag) {
        g_find_arr_flag[nId] = g_find_flag;
        pUnit = &sgUnitsMem[nId - 1];
        // Hyperion: the unit's top and bottom from beside the entry, not through its
        // indices (finder_side_t). Its bottom's index is at nYBegin or past it when
        // its bottom is at nTop or below it, and its top's is before nYEnd when its top is
        // above the box's bottom, the orderings being in order.
        const finder_side_t &side = FinderSideX(nXIdx);
        const bool bIn = side.nHigh >= nTop && side.nLow < nBottom;
        if (g_bFinderSideCheck && bIn != (BOUND_BOTTOM(pUnit) >= nYBegin && BOUND_TOP(pUnit) < nYEnd))
          FinderSideDiffer("get_units_in_rect", nXIdx);
        if (bIn)
          *ppWrite++ = pUnit;
      }
      ++nXIdx;
    }
  }

  if (nCheck >= 0) {
    bool bSame = nCheck == ppWrite - ppList;
    for (int k = 0; bSame && k < nCheck; ++k)
      bSame = ppList[k] == &sgUnitsMem[s_check[k].nId - 1];
    if (!bSame)
      FinderSideDiffer("get_units_in_rect's grid", static_cast<int>(ppWrite - ppList));
  }
  *ppWrite = NULL;
  g_find_idx_begin = (int)(ppWrite + 1 - g_find_arr_unit);
  return ppList;
}

// oracle 0x422460 (96 B). The rectangle search with a per-unit predicate: the
// list is compacted in place and the terminator rewritten.
//
// The NULL filter leaves through an early return, not a wrapping if. That block
// shape puts the walked unit in EAX and copies it to ECX at the call, and it
// also pushes EDI inside the guard rather than in the prologue.
CUnit **__fastcall get_units_in_rect_filtered(rect16_t *pRect,
                                              PFN_UNIT_FILTER pFilter,
                                              int nContext)
{
  CUnit **ppList;
  CUnit **ppRead;
  CUnit **ppWrite;
  CUnit *pUnit;

  ppList = get_units_in_rect(pRect);
  if (pFilter == NULL)
    return ppList;

  ppRead = ppList;
  ppWrite = ppList;
  while ((pUnit = *ppRead) != NULL) {
    if (pFilter(pUnit, nContext))
      *ppWrite++ = *ppRead;
    ++ppRead;
  }

  *ppWrite = NULL;
  return ppList;
}

// oracle 0x4224C0 (662 B). Collect the units overlapping pRect by intersecting
// the two bound orderings: mark every unit in the X span, raise the mark on the
// ones also in the Y span, then walk X again and emit the doubly marked ones.
//
// A span wider than the widest unit cannot be pruned by the far edge, and the
// two flags record which of the two spans was narrow enough to test.
CUnit **__fastcall find_units_in_rect(mrect_t *pRect)
{
  CUnit **ppHead;
  CUnit **ppWrite;
  upos_t wX2;  // Hyperion: positions, 4 bytes on x64
  upos_t wY2;
  int bNarrowX;
  int bNarrowY;
  int nX0;
  int nX1;
  int nY0;
  int nY1;
  int i;

  dword_5D6E4C[sgOpenLists] = g_find_idx_begin;
  ++sgOpenLists;
  ppHead = &g_find_arr_unit[g_find_idx_begin];
  ppWrite = ppHead;

  wX2 = PosU(pRect->right);
  wY2 = PosU(pRect->bottom);
  bNarrowX = 0;
  bNarrowY = 0;

  if (pRect->right - pRect->left + 1 < (WORD)sgdwMaxSizeX_g) {
    bNarrowX = 1;
    wX2 = PosU(PosU(pRect->left) + sgdwMaxSizeX_g - 1);
  }
  if (pRect->bottom - pRect->top + 1 < (WORD)sgdwMaxSizeY_g) {
    bNarrowY = 1;
    wY2 = PosU(PosU(pRect->top) + sgdwMaxSizeY_g - 1);
  }

  nX0 = SearchFinder(g_pos_sort_x, pRect->left, 1);
  nX1 = SearchFinder(g_pos_sort_x, wX2, 1);
  nY0 = SearchFinder(g_pos_sort_y, pRect->top, 1);
  nY1 = SearchFinder(g_pos_sort_y, wY2, 1);

  // Hyperion: long walks answered from the finder's grid (FinderGridEntries):
  // the units with an x entry in the first walk's span and a y entry in the second's, each of
  // them kept when the far edge it is tested by allows, in the order of its first entry in the
  // span, as the third walk puts them out. The walks also leave every unit of the x span marked
  // 0, which the grid does not. The marks are read by these walks, which read only the ones
  // their first walk has just set, and by get_units_in_rect, against a count past every mark
  // left here once g_find_flag is 3 or more (it is from its third call, until it wraps past
  // 2^31 calls), so from then on the marks left make no difference. HYPERION_FINDER_CHECK=1
  // makes both and compares them.
  static std::vector<finder_kept_t> s_entries;
  int nGrid = -1;
  if (nX1 - nX0 > 0 && nY1 - nY0 > 0 && g_find_flag >= 3 && (nX1 - nX0) + (nY1 - nY0) > 2 * FIND_GRID_WALK) {
    if (s_entries.size() < static_cast<size_t>(g_pools.units))
      s_entries.resize(g_pools.units);
    const int nFound = FinderGridEntries(nX0, nX1, nY0, nY1, s_entries.data());
    nGrid = 0;
    for (int k = 0; k < nFound; ++k) {
      CUnit *pUnit = CUnit::Head() + (s_entries[k].nId - 1);
      if (bNarrowX != 0 && PosS(pUnit->pSprite->wPosX) - gUnitBoundBox[pUnit->wType].left > pRect->right)
        continue;
      if (bNarrowY != 0 && pUnit->pSprite->wPosY - gUnitBoundBox[pUnit->wType].top > pRect->bottom)
        continue;
      s_entries[nGrid++] = s_entries[k];
    }
    std::sort(s_entries.begin(), s_entries.begin() + nGrid,
              [](const finder_kept_t &a, const finder_kept_t &b) { return a.nKey < b.nKey; });
    if (!g_bFinderSideCheck) {
      for (int k = 0; k < nGrid; ++k)
        *ppWrite++ = CUnit::Head() + (s_entries[k].nId - 1);
      *ppWrite = NULL;
      g_find_idx_begin = (int)(ppWrite + 1 - g_find_arr_unit);
      return ppHead;
    }
  }

  if (nX1 - nX0 > 0 && nY1 - nY0 > 0) {
    for (i = nX0; i < nX1; i++) {
      DWORD dwId = g_pos_sort_x[i].dwUnitId;
      CUnit *pUnit;

      if (bNarrowX != 0) {
        pUnit = unit_from_bound_id(dwId);
        if (PosS(pUnit->pSprite->wPosX) - gUnitBoundBox[pUnit->wType].left > pRect->right) {
          g_find_arr_flag[dwId] = 0;
          continue;
        }
      }
      g_find_arr_flag[dwId] = 1;
    }

    for (i = nY0; i < nY1; i++) {
      DWORD dwId = g_pos_sort_y[i].dwUnitId;
      CUnit *pUnit;

      if (bNarrowY != 0) {
        pUnit = unit_from_bound_id(dwId);
        if (pUnit->pSprite->wPosY - gUnitBoundBox[pUnit->wType].top > pRect->bottom) {
          continue;
        }
      }
      if (g_find_arr_flag[dwId] == 1) {
        g_find_arr_flag[dwId] = 3;
      }
    }

    for (i = nX0; i < nX1; i++) {
      DWORD dwId = g_pos_sort_x[i].dwUnitId;

      if (g_find_arr_flag[dwId] == 3) {
        *ppWrite = unit_from_bound_id(dwId);
        ++ppWrite;
      }
      g_find_arr_flag[dwId] = 0;
    }
  }
  if (nGrid >= 0) {
    bool bSame = nGrid == ppWrite - ppHead;
    for (int k = 0; bSame && k < nGrid; ++k)
      bSame = ppHead[k] == CUnit::Head() + (s_entries[k].nId - 1);
    if (!bSame)
      FinderSideDiffer("find_units_in_rect's grid", static_cast<int>(ppWrite - ppHead));
  }

  *ppWrite = NULL;
  g_find_idx_begin = (int)(ppWrite + 1 - g_find_arr_unit);
  return ppHead;
}

// oracle 0x422760 (229 B). The gap between this unit's collision box and one
// map point, zero when the point falls inside the box.
//
// The two edges are stepped past the box here rather than inside unit_bounds:
// in the helper each `inc` lands in both arms, at the join each lands once.
DWORD CUnit::GetDistanceTo_xy(int nX, int nY)
{
  mrect_t box;  // Hyperion: a box of map positions
  int nDx;
  int nDy;

  if (gulUnitTypeFlags[wType] & UNIT_FLAG_SUBUNIT)
    unit_bounds(subunit, &box);
  else
    unit_bounds(this, &box);

  ++box.right;
  ++box.bottom;

  nDx = box.left - PosS(nX);
  if (nDx < 0) {
    nDx = PosS(nX) - box.right;
    if (nDx < 0)
      nDx = 0;
  }

  nDy = box.top - PosS(nY);
  if (nDy < 0) {
    nDy = PosS(nY) - box.bottom;
    if (nDy < 0)
      nDy = 0;
  }

  return veclen(0, nDx, 0, nDy);
}

// oracle 0x422850 (329 B). The gap between this unit's collision box and
// another unit's, zero when the two boxes touch. The target's box is built
// first but stepped second.
DWORD CUnit::GetDistanceTo_CUnit(CUnit *pTarget)
{
  mrect_t tbox;
  mrect_t box;  // Hyperion: a box of map positions
  int nDx;
  int nDy;

  unit_bounds(pTarget, &tbox);

  if (gulUnitTypeFlags[wType] & UNIT_FLAG_SUBUNIT)
    unit_bounds(subunit, &box);
  else
    unit_bounds(this, &box);

  ++box.right;
  ++box.bottom;
  ++tbox.right;
  ++tbox.bottom;

  nDx = box.left - tbox.right;
  if (nDx < 0) {
    nDx = tbox.left - box.right;
    if (nDx < 0)
      nDx = 0;
  }

  nDy = box.top - tbox.bottom;
  if (nDy < 0) {
    nDy = tbox.top - box.bottom;
    if (nDy < 0)
      nDy = 0;
  }

  return veclen(0, nDx, 0, nDy);
}

// oracle 0x4229A0 (317 B). The same box-to-box gap as GetDistanceTo_CUnit,
// answered against a bound. The compare is unsigned.
BOOL CUnit::IsWithinTargetRange(int nRange, CUnit *pTarget)
{
  mrect_t box;  // Hyperion: a box of map positions
  mrect_t tbox;
  int nDx;
  int nDy;

  if (gulUnitTypeFlags[wType] & UNIT_FLAG_SUBUNIT)
    unit_bounds(subunit, &box);
  else
    unit_bounds(this, &box);

  ++box.right;
  ++box.bottom;

  unit_bounds(pTarget, &tbox);

  ++tbox.right;
  ++tbox.bottom;

  nDx = box.left - tbox.right;
  if (nDx < 0) {
    nDx = tbox.left - box.right;
    if (nDx < 0)
      nDx = 0;
  }

  nDy = box.top - tbox.bottom;
  if (nDy < 0) {
    nDy = tbox.top - box.bottom;
    if (nDy < 0)
      nDy = 0;
  }

  return (DWORD)nRange >= (DWORD)veclen(0, nDx, 0, nDy);
}
