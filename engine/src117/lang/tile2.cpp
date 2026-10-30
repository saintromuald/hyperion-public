// tile2.cpp -- terrain tile queries, the decaying-creep node pool and the
// per-tile thing position lists. Bodies are in oracle VA order.
//
// Some pool bodies keep a node pointer in ECX across a call (tile2_read_from_save
// around sub_4B4D10, sub_4B4930 around removeThingFromPosList), which holds only
// while the callee is in this TU: hence the static copies.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../port/data/port_formats.h"  // Hyperion
#include "../gen/save117.h"
#include <string.h>              // memset -- sub_4B44A0 zero-fills the pool

#include "platform.h"
#include "rect16.h"
#include "rect32.h"
#include "CUnit.h"
#include "CSprite.h"
#include "debug.h"          // invalidate_rect
#include "CUnitFind.h"      // finish_unit_iteration
#include "CUnitZBuild.h"    // ClipToGameScreen
#include "CUnitTSpells.h"   // net_randbetween
#include "TrigAct.h"        // net_rand
#include "cellscrl.h"       // cell_draw_rect
#include "collide.h"        // mtx_has_flags
#include "creep.h"
#include "minimap.h"        // minimap_force_redraw
#include "tile2.h"          // this TU's own rows and decaying_creep_t
#include "gamemap.h"        // the four creep_hooks_t slots gamemap_init fills
#include "CUnitTerran.h"    // sub_4B47F0
#include "order.h"          // get_units_in_rect
#include "saveload.h"       // compress / decompress
#include "../gen/globals117.h"
#include "../h/img.h"  // Hyperion: _img_list
#include "../h/playervision.h"  // Hyperion: the replay viewer past slot 7 (stage D)


// The globals this TU reaches, dual-spelled, one #ifndef per name.
#ifdef OC_DLL_MODE
  #ifndef byte_50DECC
  #define byte_50DECC (reinterpret_cast<BYTE *>(OC_VA(0x50DECCu)))
  #endif
  #ifndef gfpCellMap
  #define gfpCellMap (*reinterpret_cast<WORD **>(OC_VA(0x54323Cu)))
  #endif
  #ifndef gpMtxFlagsBuf
  #define gpMtxFlagsBuf (*reinterpret_cast<DWORD **>(OC_VA(0x666450u)))
  #endif
  #ifndef sgDecayingCreep
  #define sgDecayingCreep (reinterpret_cast<decaying_creep_t *>(OC_VA(0x6BE698u)))
  #endif
  #ifndef dword_6C3698
  #define dword_6C3698 (reinterpret_cast<decaying_creep_t **>(OC_VA(0x6C3698u)))
  #endif
  #ifndef dword_6C4698
  #define dword_6C4698 (reinterpret_cast<decaying_creep_t **>(OC_VA(0x6C4698u)))
  #endif
  #ifndef word_6C46C0
  #define word_6C46C0 (reinterpret_cast<WORD *>(OC_VA(0x6C46C0u)))
  #endif
  #ifndef dword_6C46D4
  #define dword_6C46D4 (*reinterpret_cast<DWORD *>(OC_VA(0x6C46D4u)))
  #endif
  #ifndef dword_6C46D8
  #define dword_6C46D8 (*reinterpret_cast<DWORD *>(OC_VA(0x6C46D8u)))
  #endif
  #ifndef gpMtxInfo
  #define gpMtxInfo (*reinterpret_cast<BYTE **>(OC_VA(0x6CE650u)))
  #endif
#else
extern BYTE              byte_50DECC[12];          // 0x50DECC
extern WORD             *gfpCellMap;               // 0x54323C
extern DWORD            *gpMtxFlagsBuf;            // 0x666450
extern decaying_creep_t  sgDecayingCreep[1024] OC_UNALIGNED;    // 0x6BE698
extern decaying_creep_t *dword_6C3698[1024] OC_UNALIGNED;       // 0x6C3698
extern decaying_creep_t *dword_6C4698[10] OC_UNALIGNED;         // 0x6C4698
extern WORD              word_6C46C0[10];          // 0x6C46C0
extern DWORD             dword_6C46D4;             // 0x6C46D4
extern DWORD             dword_6C46D8;             // 0x6C46D8
extern BYTE             *gpMtxInfo;                // 0x6CE650
#endif

// The other functions are declared in tile2.h, gamemap.h (the four
// creep_hooks_t slots gamemap_init fills) and CUnitTerran.h (sub_4B47F0). The
// static copies are declared here, for the calls that run out of VA order.
static void sub_4B4390_local(decaying_creep_t *pNode);
static void sub_4B4D10_local(decaying_creep_t *pNode);
static void removeThingFromPosList_local(decaying_creep_t *pNode);

// The three list primitives. Every pool body inlines these, so they are static.

// Unlink a node from whichever of the ten lists its bList names. The int list
// index and the forced inline keep sub_4B4230 as 1.17 has it.
static __forceinline void creep_list_remove(decaying_creep_t *pNode) {
  int bList = pNode->bList;

  --word_6C46C0[bList];

  if (pNode == dword_6C4698[bList])
    dword_6C4698[bList] = pNode->pNext;

  if (pNode->pNext != 0)
    pNode->pNext->pPrev = pNode->pPrev;

  if (pNode->pPrev != 0)
    pNode->pPrev->pNext = pNode->pNext;

  pNode->pNext = 0;
  pNode->pPrev = 0;
}

// Push a node onto list bList. The head is re-read after the pNext store, which
// may alias it.
static void creep_list_insert(decaying_creep_t *pNode, BYTE bList) {
  pNode->bList = bList;
  ++word_6C46C0[bList];

  pNode->pNext = dword_6C4698[bList];
  pNode->pPrev = 0;

  if (dword_6C4698[bList] != 0)
    dword_6C4698[bList]->pPrev = pNode;

  dword_6C4698[bList] = pNode;
}

// Hyperion: a node's tile. On a map past 256 tiles a side the coordinates' high
// bits are kept in bPad, x in its low nibble and y in its high one; on 1.17's
// maps bPad is left as 1.17 left it.
static __forceinline int creep_x(const decaying_creep_t *pNode) {
  return pNode->bX | (gwMapWidth > 256 ? (pNode->bPad & 0x0F) << 8 : 0);
}

static __forceinline int creep_y(const decaying_creep_t *pNode) {
  return pNode->bY | (gwMapHeight > 256 ? (pNode->bPad >> 4) << 8 : 0);
}

// The node on (nX, nY), or the tail NULL when the bucket runs out. Callers
// that can reach a NULL here test it themselves; sub_4B4230 does not.
static __forceinline decaying_creep_t *creep_find_at(int nX, int nY) {
  decaying_creep_t *pNode = dword_6C3698[((nY << 4) + nX + nY) & 0x3FF];

  while (pNode != 0) {
    if (creep_x(pNode) == nX && creep_y(pNode) == nY)
      break;

    pNode = pNode->pPosNext;
  }

  return pNode;
}

// The saved form of a node link is a one-based pool index; these two convert
// between it and a pointer in place.
static decaying_creep_t *creep_index_to_ptr(decaying_creep_t *pIndex) {
  if (pIndex == 0)
    return 0;

  return &sgDecayingCreep[reinterpret_cast<intptr_t>(pIndex) - 1];
}

static decaying_creep_t *creep_ptr_to_index(decaying_creep_t *pNode) {
  if (pNode == 0)
    return 0;

  return reinterpret_cast<decaying_creep_t *>((pNode - sgDecayingCreep) + 1);
}

// The bodies, in oracle VA order.

// sub_4B3F30, oracle 0x4B3F30 (220 B). Take a node off the free list
// and start decaying creep on (nX, nY). Fails when list 9 is empty; the new list
// is the neighbour count. noinline keeps it a call in sub_4B46E0 under OC_PAYLOAD.
__declspec(noinline) int sub_4B3F30(int nX, int nY) {
  decaying_creep_t *pNode;

  if (word_6C46C0[9] == 0)
    return 0;

  pNode = dword_6C4698[9];
  creep_list_remove(pNode);

  pNode->bX = (BYTE)nX;
  pNode->bY = (BYTE)nY;
  if (gwMapWidth > 256 || gwMapHeight > 256)  // Hyperion
    pNode->bPad = (BYTE)(((nX >> 8) & 0x0F) | ((nY >> 8) << 4));

  creep_list_insert(pNode, (BYTE)count_neighboring_creep_tiles(nX, nY));
  addThingToPosList(pNode);

  gpSquareMap[gwMapWidth * nY + nX] |= 0x10000000;
  return 1;
}

// sub_4B4010, oracle 0x4B4010 (43 B). One decay tick. The countdown
// reloads from a table indexed by how full the free list is, so a full pool
// decays immediately.
void sub_4B4010() {
  DWORD dwTimer = dword_6C46D4;

  if (dwTimer != 0) {
    dword_6C46D4 = dwTimer - 1;
    return;
  }

  dword_6C46D4 = byte_50DECC[word_6C46C0[9] >> 7];
  sub_4B4A40();
}

// Cell and flag-word pointers and the tile_is_fogged test, inlined into sub_4B4040.
static __forceinline WORD *cell_at(int x, int y) { return &gfpCellMap[gwMapWidth * y + x]; }
static __forceinline DWORD *square_at(int x, int y) { return &gpSquareMap[gwMapWidth * y + x]; }
static __forceinline BOOL fog_at(DWORD *pSq) {
  if (gPlayback != 0) {
    if ((~*pSq & gdwReplayVisionMask & 0xFF) || sgbShowAll_g != 0 ||
        ViewHxTileSeen(static_cast<int>(pSq - gpSquareMap)))  // Hyperion: the viewer's slots past 7 (stage D)
      return 0;
    return 1;
  }
  return *pSq & gdwLocalMask;
}

// The creep_hooks_t redraw slot. nParam 2 only marks the cell dirty; otherwise
// the megatile is rewritten when the tile is not fogged, and the edge flags are
// recomputed either way. The megatile group and slot are written out at each
// use, as in 1.17.
void __stdcall sub_4B4040(int nX, int nY, int nParam, WORD nTile, int *pBox) {
  WORD *pCell = cell_at(nX, nY);
  DWORD *pFlags = square_at(nX, nY);
  DWORD dwEdge;

  if (nParam == 2) {
    *pCell |= 0x8000;
    sub_4B4EB0(reinterpret_cast<rect32_t *>(pBox));
    return;
  }

  if (!fog_at(pFlags)) {
    *pCell = *reinterpret_cast<WORD *>(gpMtxInfo + ((nTile >> 4) & 0x7FF) * 0x34 + 0x14 + (nTile & 0xF) * 2);

    DWORD dwFlags = *pFlags;
    if (nParam == 0)
      dwFlags |= 0x40000000;
    else
      dwFlags &= ~0x40000000;
    *pFlags = dwFlags;

    sub_4B4EB0(reinterpret_cast<rect32_t *>(pBox));
  }

  dwEdge = ((*reinterpret_cast<WORD *>(gpMtxInfo + ((nTile >> 4) & 0x7FF) * 0x34 + 2) & ~5) << 16)
         | gpMtxFlagsBuf[*reinterpret_cast<WORD *>(gpMtxInfo + ((nTile >> 4) & 0x7FF) * 0x34 + 0x14 + (nTile & 0xF) * 2) & 0x7FFF];
  *pFlags ^= (dwEdge ^ *pFlags) & 0x450000;

  if (*pFlags & 0x10000)
    *pFlags &= ~0x40000;

  if (!(*pFlags & 0x450000))
    *pFlags |= 0x40000;

  sub_4B4230(nX, nY);
}

// True when a unit inside the rectangle keeps creep alive under it. The walk
// stops at the first supporter and always closes the iteration. The identical
// arms on dwBit keep 1.17's separate test of the bit.
BOOL __stdcall FindCreepSupport(int nX, int nY, rect32_t *pRect) {
  rect16_t rc;
  rc.left = (WORD)pRect->left;
  rc.right = (WORD)pRect->right;
  rc.top = (WORD)pRect->top;
  rc.bottom = (WORD)pRect->bottom;
  BOOL bFound = 0;
  CUnit **ppList = get_units_in_rect(&rc);
  CUnit **ppUnit;
  CUnit *pUnit;
  for (ppUnit = ppList; (pUnit = *ppUnit) != 0; ++ppUnit) {
    DWORD dwFlags = pUnit->dwFlags;
    if (((BYTE)dwFlags & 2) == 0)
      continue;
    DWORD dwBit = dwFlags & 1;
    BOOL bMorph = (dwBit != 0);
    CSprite *pSprite = pUnit->pSprite;
    int nType = pUnit->wType;
    upos_t wPosY = pSprite->wPosY;
    upos_t wPosX = pSprite->wPosX;
    if (sub_4C0830(nX, nY, nType, PosS(wPosX), PosS(wPosY), bMorph)) {
      if (dwBit)
        bFound = 1;
      else
        bFound = 1;
      break;
    }
  }
  finish_unit_iteration(ppList);
  return bFound;
}

// sub_4B4230, oracle 0x4B4230 (339 B). Re-sort the eight
// neighbours of (nX, nY) into the list their new creep neighbour count names.
void sub_4B4230(int nX, int nY) {
  signed char aOffsets[16] = {
    -1, -1,  0, -1,  1, -1, -1,  0,
     1,  0, -1,  1,  0,  1,  1,  1,
  };
  int i;

  for (i = 0; i < 8; ++i) {
    int x = aOffsets[i * 2] + nX;
    int y = aOffsets[i * 2 + 1] + nY;
    decaying_creep_t *pNode;
    int nCount;

    if ((DWORD)x >= gwMapWidth)
      continue;

    if ((DWORD)y >= gwMapHeight)
      continue;

    if (!tile_is_creep(x, y))
      continue;

    if (!(gpSquareMap[gwMapWidth * y + x] & 0x10000000))
      continue;

    pNode = creep_find_at(x, y);
    nCount = count_neighboring_creep_tiles(x, y);

    if (nCount != pNode->bList) {
      creep_list_remove(pNode);
      creep_list_insert(pNode, (BYTE)nCount);
    }
  }
}

// sub_4B4390, oracle 0x4B4390 (151 B). Convert a node's four links
// from pointers to saved one-based indices.
void sub_4B4390(decaying_creep_t *pNode) {
  pNode->pNext = creep_ptr_to_index(pNode->pNext);
  pNode->pPrev = creep_ptr_to_index(pNode->pPrev);
  pNode->pPosNext = creep_ptr_to_index(pNode->pPosNext);
  pNode->pPosPrev = creep_ptr_to_index(pNode->pPosPrev);
}

// Internal copy of sub_4B4390, same bytes. Its callers in this TU keep a value in a
// register across the call, which only an internal callee allows.
static void sub_4B4390_local(decaying_creep_t *pNode) {
  pNode->pNext = creep_ptr_to_index(pNode->pNext);
  pNode->pPrev = creep_ptr_to_index(pNode->pPrev);
  pNode->pPosNext = creep_ptr_to_index(pNode->pPosNext);
  pNode->pPosPrev = creep_ptr_to_index(pNode->pPosPrev);
}

// Push a node onto the position bucket for its own tile. The index arithmetic
// is 16-bit here and 32-bit in removeThingFromPosList, as in 1.17. The
// emptiness test is a volatile read, so the arm reloads the head as 1.17 does.
void addThingToPosList(decaying_creep_t *pNode) {
  WORD wSlot = (WORD)creep_y(pNode);  // Hyperion: bY and bX in 1.17

  wSlot <<= 4;
  wSlot = (WORD)(wSlot + creep_y(pNode));
  wSlot = (WORD)(wSlot + creep_x(pNode));
  wSlot &= 0x3FF;

  if (*reinterpret_cast<decaying_creep_t *volatile *>(&dword_6C3698[wSlot]) == 0) {
    dword_6C3698[wSlot] = pNode;
    pNode->pPosNext = 0;
    pNode->pPosPrev = 0;
  } else {
    pNode->pPosNext = dword_6C3698[wSlot];
    pNode->pPosPrev = 0;

    if (dword_6C3698[wSlot] != 0)
      dword_6C3698[wSlot]->pPosPrev = pNode;

    dword_6C3698[wSlot] = pNode;
  }
}

// sub_4B44A0, oracle 0x4B44A0 (164 B). Clear the pool and thread
// every node onto free list 9. The identical arms under a bList test are 1.17's
// shape.
void sub_4B44A0() {
  int i;

  memset(dword_6C4698, 0, 10 * sizeof(dword_6C4698[0]));  // Hyperion: 40 in 1.17
  memset(dword_6C3698, 0, 1024 * sizeof(dword_6C3698[0]));  // Hyperion: 0x1000 in 1.17
  memset(word_6C46C0, 0, 20);
  memset(sgDecayingCreep, 0, 1024 * sizeof(sgDecayingCreep[0]));  // Hyperion: 0x5000 in 1.17

  for (i = 0; i < 1024; ++i) {
    decaying_creep_t *pNode = &sgDecayingCreep[i];
    if (pNode->bList)
      pNode->bList = 9;
    else
      pNode->bList = 9;
    ++word_6C46C0[9];
    pNode->pNext = dword_6C4698[9];
    pNode->pPrev = 0;
    if (dword_6C4698[9] != 0)
      dword_6C4698[9]->pPrev = pNode;
    dword_6C4698[9] = pNode;
  }

  dword_6C46D4 = 0;
  dword_6C46D8 = 0;
}

// tile_is_occupied, oracle 0x4B4550 (13 B). True when anything
// already stands on this build cell.
int tile_is_occupied(int nX, int nY) {
  return mtx_has_flags(PosS(nX), PosS(nY), 0x8000000, 0);
}

// The creep_hooks_t fog slot. In a replay the local vision mask and the
// show-all switch decide instead of the player mask.
// The cell is read in each arm; MSVC hoists the one load and keeps it in EAX.
BOOL __stdcall tile_is_fogged(int nX, int nY) {
  int nIdx = gwMapWidth * nY + nX;

  if (gPlayback != 0) {
    if ((~gpSquareMap[nIdx] & gdwReplayVisionMask & 0xFF) || sgbShowAll_g != 0 ||
        ViewHxTileSeen(nIdx))  // Hyperion: the viewer's slots past 7 (stage D)
      return 0;

    return 1;
  }

  return gpSquareMap[nIdx] & gdwLocalMask;
}

// tile_can_have_creep, oracle 0x4B45B0 (64 B). True when creep may
// grow here: either this cell allows it outright, or the cell below does and is
// not the bottom map edge.
int tile_can_have_creep(int nX, int nY) {
  if (mtx_has_flags(PosS(nX), PosS(nY), 0x20800000, 0))
    return 1;

  ++nY;

  if (PosU(nY) == gwMapHeight)
    return 0;

  return mtx_has_flags(PosS(nX), PosS(nY), 0x800000, 0);
}

// tile_covered_by_creep, oracle 0x4B45F0 (34 B). The creep_hooks_t
// coverage slot.
BOOL __stdcall tile_covered_by_creep(int nX, int nY) {
  return gpSquareMap[gwMapWidth * nY + nX] & 0x40000000;
}

// tile2_read_from_save, oracle 0x4B4620 (189 B). Read the pool back
// from a save and turn every saved index into a pointer.
BOOL tile2_read_from_save(void *pFile) {
  decaying_creep_t *pNode;
  decaying_creep_t **ppSlot;

  // Hyperion: 1.17's layout, 0x5000, 0x1000 and 0x28 bytes.
  if (!PortSave117Decompress(SAVE117_DecayingCreep, sgDecayingCreep, 1024, pFile))
    return FALSE;

  if (!PortSaveDecompressIds(dword_6C3698, 1024, pFile))
    return FALSE;

  if (!PortSaveDecompressIds(dword_6C4698, 10, pFile))
    return FALSE;

  if (!decompress(word_6C46C0, 0x14, pFile))
    return FALSE;

  for (pNode = &sgDecayingCreep[1024]; pNode != &sgDecayingCreep[0]; )
    sub_4B4D10_local(--pNode);

  for (ppSlot = &dword_6C3698[1024]; ppSlot != &dword_6C3698[0]; )
    *--ppSlot = creep_index_to_ptr(*ppSlot);

  for (ppSlot = &dword_6C4698[10]; ppSlot != &dword_6C4698[0]; )
    *--ppSlot = creep_index_to_ptr(*ppSlot);

  return TRUE;
}

// sub_4B46E0, oracle 0x4B46E0 (271 B). The outer step `dy += 32`
// belongs in the for-increment after `++y`, 1.17's order.
//
// Re-seed decaying creep over a footprint that is losing its source. Every
// still-creeped cell in the box either finds another supporter or gets a decay
// node; a cell that can get neither fails the whole call.
int sub_4B46E0(WORD wUnitType, int nPosX, int nPosY, int nArg3) {
  rect32_t box;
  BOOL bClipped;
  DWORD dwMask;
  int dx;
  int dy;
  int x;
  int y;

  bClipped = get_max_creep_bounding_box((short)wUnitType, nPosX, nPosY, &box, nArg3);
  dwMask = (wUnitType == 0x95) ? 0x10000000 : 0x18000000;

  dy = (box.top << 5) - nPosY + 16;

  for (y = box.top; y <= box.bottom; ++y, dy += 32) {
    dx = (box.left << 5) - nPosX + 16;

    for (x = box.left; x <= box.right; ++x) {
      if (tile_is_creep(x, y)) {
        if (!bClipped || !creep_within_ellipse(dx, dy)) {
          if (!(gpSquareMap[gwMapWidth * y + x] & dwMask)) {
            if (!sub_4C0250(x, y, FindCreepSupport)) {
              if (!sub_4B3F30(x, y))
                return 0;
            }
          }
        }
      }

      dx += 32;
    }
  }

  dword_6C46D8 = 0;
  return 1;
}

// sub_4B47F0, oracle 0x4B47F0 (302 B). The volatile view forces the
// 32-bit load the mask needs, and the masked value is named in a local so that
// the `!= 0` stays its own test.
//
// The same sweep taken from a unit rather than from loose coordinates.
BOOL sub_4B47F0(CUnit *pUnit) {
  rect32_t box;
  BOOL bClipped;
  DWORD dwMask;
  int dx;
  int dy;
  int x;
  int y;

  int nType = pUnit->wType;
  upos_t wPosX = pUnit->wPositionX;
  upos_t wPosY = pUnit->wPositionY;

  DWORD dwBit = pUnit->dwFlagsVolatile & 1;
  BOOL bMorph = (dwBit != 0);

  bClipped = get_max_creep_bounding_box((short)nType, PosS(wPosX), PosS(wPosY), &box,
                                        bMorph);
  dwMask = (pUnit->wType == 0x95) ? 0x10000000 : 0x18000000;

  dy = (box.top << 5) - PosS(pUnit->wPositionY) + 16;

  for (y = box.top; y <= box.bottom; ++y, dy += 32) {
    dx = (box.left << 5) - PosS(pUnit->wPositionX) + 16;

    for (x = box.left; x <= box.right; ++x) {
      if (tile_is_creep(x, y)) {
        if (!bClipped || !creep_within_ellipse(dx, dy)) {
          if (!(gpSquareMap[gwMapWidth * y + x] & dwMask)) {
            if (!sub_4C0250(x, y, FindCreepSupport)) {
              if (!sub_4B3F30(x, y))
                return 0;
            }
          }
        }
      }

      dx += 32;
    }
  }

  dword_6C46D8 = 0;
  return 1;
}

// creep_rand, oracle 0x4B4920 (10 B). A creep roll on tag 0x1A
int creep_rand() {
  return net_rand(0x1A);
}

// sub_4B4930, oracle 0x4B4930 (215 B). noinline is required:
// without it sub_4B4DD0 inlines this body.
//
// Stop the decay on (nX, nY): unlink the node, uncover the cell and return it
// to the free list.
int sub_4B4930(int nX, int nY) {
  decaying_creep_t *pNode = creep_find_at(nX, nY);

  if (pNode == 0)
    return 0;

  creep_list_remove(pNode);
  removeThingFromPosList_local(pNode);
  creep_list_insert(pNode, 9);

  gpSquareMap[gwMapWidth * nY + nX] &= ~0x10000000;
  return 1;
}

// sub_4B4A40, oracle 0x4B4A40 (313 B). Decay one random node from
// the lowest non-empty list. Lists 0..8 are the neighbour counts, 9 is the free
// list.
void sub_4B4A40() {
  decaying_creep_t *pNode;
  BYTE bList;
  int i;

  for (bList = 0; bList < 9; ++bList) {
    if (word_6C46C0[bList] != 0)
      break;
  }

  if (bList == 9)
    return;

  i = net_randbetween(0, word_6C46C0[bList] - 1, 0x1B);
  pNode = dword_6C4698[bList];

  while (i != 0) {
    pNode = pNode->pNext;
    --i;
  }

  creep_list_remove(pNode);
  removeThingFromPosList_local(pNode);

  gpSquareMap[gwMapWidth * creep_y(pNode) + creep_x(pNode)] &= ~0x10000000;  // Hyperion
  uncover_creep_at(creep_x(pNode), creep_y(pNode));

  creep_list_insert(pNode, 9);
}

// sub_4B4B80, oracle 0x4B4B80 (388 B). Write the pool to a save
// The links go out as indices and are restored afterwards whether or not the
// write succeeded. The restore is a counted loop over sub_4B4D10, which MSVC
// inlines with its pointer biased to pPosNext.
int sub_4B4B80(void *pFile) {
  decaying_creep_t *pNode;
  decaying_creep_t **ppSlot;
  int nOk;

  for (pNode = &sgDecayingCreep[1024]; pNode != &sgDecayingCreep[0]; )
    sub_4B4390_local(--pNode);

  for (ppSlot = &dword_6C3698[1024]; ppSlot != &dword_6C3698[0]; )
    *--ppSlot = creep_ptr_to_index(*ppSlot);

  for (ppSlot = &dword_6C4698[10]; ppSlot != &dword_6C4698[0]; )
    *--ppSlot = creep_ptr_to_index(*ppSlot);

  if (PortSave117Compress(SAVE117_DecayingCreep, sgDecayingCreep, 1024, pFile)  // Hyperion
      && PortSaveCompressIds(dword_6C3698, 1024, pFile)
      && PortSaveCompressIds(dword_6C4698, 10, pFile)
      && compress(word_6C46C0, 0x14, pFile))
    nOk = 1;
  else
    nOk = 0;

  for (int i = 1024; i-- != 0; )
    sub_4B4D10_local(&sgDecayingCreep[i]);

  for (ppSlot = &dword_6C3698[1024]; ppSlot != &dword_6C3698[0]; )
    *--ppSlot = creep_index_to_ptr(*ppSlot);

  for (ppSlot = &dword_6C4698[10]; ppSlot != &dword_6C4698[0]; )
    *--ppSlot = creep_index_to_ptr(*ppSlot);

  return nOk;
}

// sub_4B4D10, oracle 0x4B4D10 (83 B). Turn a node's four saved
// indices back into pointers.
void sub_4B4D10(decaying_creep_t *pNode) {
  pNode->pNext = creep_index_to_ptr(pNode->pNext);
  pNode->pPrev = creep_index_to_ptr(pNode->pPrev);
  pNode->pPosNext = creep_index_to_ptr(pNode->pPosNext);
  pNode->pPosPrev = creep_index_to_ptr(pNode->pPosPrev);
}

// Internal copy of sub_4B4D10, same bytes. Its callers in this TU keep a value in a
// register across the call, which only an internal callee allows.
static void sub_4B4D10_local(decaying_creep_t *pNode) {
  pNode->pNext = creep_index_to_ptr(pNode->pNext);
  pNode->pPrev = creep_index_to_ptr(pNode->pPrev);
  pNode->pPosNext = creep_index_to_ptr(pNode->pPosNext);
  pNode->pPosPrev = creep_index_to_ptr(pNode->pPosPrev);
}

// removeThingFromPosList, oracle 0x4B4D70 (83 B). Internal linkage
// is required: it lets sub_4B4930 keep the node in ECX across the call.
//
// Take a node out of its position bucket.
void removeThingFromPosList(decaying_creep_t *pNode) {
  int nSlot = creep_y(pNode);  // Hyperion: bY and bX in 1.17

  nSlot <<= 4;
  nSlot += creep_y(pNode);
  nSlot += creep_x(pNode);
  nSlot &= 0x3FF;

  if (pNode == dword_6C3698[nSlot])
    dword_6C3698[nSlot] = pNode->pPosNext;

  if (pNode->pPosNext != 0)
    pNode->pPosNext->pPosPrev = pNode->pPosPrev;

  if (pNode->pPosPrev != 0)
    pNode->pPosPrev->pPosNext = pNode->pPosNext;

  pNode->pPosNext = 0;
  pNode->pPosPrev = 0;
}

// Internal copy of removeThingFromPosList, same bytes. Its callers in this TU keep a value in a
// register across the call, which only an internal callee allows.
static void removeThingFromPosList_local(decaying_creep_t *pNode) {
  int nSlot = creep_y(pNode);  // Hyperion: bY and bX in 1.17

  nSlot <<= 4;
  nSlot += creep_y(pNode);
  nSlot += creep_x(pNode);
  nSlot &= 0x3FF;

  if (pNode == dword_6C3698[nSlot])
    dword_6C3698[nSlot] = pNode->pPosNext;

  if (pNode->pPosNext != 0)
    pNode->pPosNext->pPosPrev = pNode->pPosPrev;

  if (pNode->pPosPrev != 0)
    pNode->pPosPrev->pPosNext = pNode->pPosNext;

  pNode->pPosNext = 0;
  pNode->pPosPrev = 0;
}

// sub_4B4DD0, oracle 0x4B4DD0 (216 B). sub_4B4930 needs noinline or
// it is inlined here. Both offsets step by 32 in the loop increments, as in
// 1.17.
//
// Stop the decay under a unit's creep footprint. The ellipse test only runs
// when get_max_creep_bounding_box says the box was clipped.
void sub_4B4DD0(CUnit *pUnit) {
  rect32_t box;
  BOOL bClipped;
  int x;
  int y;
  int dx;
  int dy;

  int nType = pUnit->wType;
  upos_t wPosX = pUnit->wPositionX;
  upos_t wPosY = pUnit->wPositionY;

  DWORD dwBit = pUnit->dwFlagsVolatile & 1;
  BOOL bMorph = (dwBit != 0);

  bClipped = get_max_creep_bounding_box((short)nType, PosS(wPosX), PosS(wPosY), &box,
                                        bMorph);

  dy = (box.top << 5) - PosS(pUnit->wPositionY) + 16;

  for (y = box.top; y <= box.bottom; ++y, dy += 32) {
    dx = (box.left << 5) - PosS(pUnit->wPositionX) + 16;

    for (x = box.left; x <= box.right; ++x) {
      if (gpSquareMap[gwMapWidth * y + x] & 0x10000000) {
        if (!bClipped || !creep_within_ellipse(dx, dy))
          sub_4B4930(x, y);
      }

      dx += 32;
    }
  }
}

// sub_4B4EB0, oracle 0x4B4EB0 (156 B). Repaint a map rectangle: the
// cell grid always, the screen only when the scrolled rectangle survives the
// clip.
void sub_4B4EB0(rect32_t *pRect) {
  rect32_t rc;

  minimap_force_redraw();
  cell_draw_rect(pRect->left / 32, pRect->top / 32,
                 (pRect->right - pRect->left + 31) / 32,
                 (pRect->bottom - pRect->top + 31) / 32);

  if (byte_6D0BF5 & 1)
    return;

  rc = *pRect;
  rc.left -= glScrollPixelX;
  rc.right -= glScrollPixelX;
  rc.top -= glScrollPixelY;
  rc.bottom -= glScrollPixelY;

  if (!ClipToGameScreen(&rc))
    invalidate_rect(&rc);
}
