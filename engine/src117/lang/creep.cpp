// creep.cpp -- the creep layer: the per-tile creep matrix, the parallel edge map
// that picks each border cell's sprite, and the appliers that walk a cell or its
// 3x3 neighbourhood and report changes back through a hook block. The TU's name
// is 1.17's own: creep_free and creep_init tag their Storm allocations with the
// .rdata string "creep.cpp". Bodies are in oracle VA order.
//
// tile_is_creep depends on tile_is_creep_ptr being inlined into it, as in 1.17,
// so the two stay in this file.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>

#include "../h/creep.h"
#include "../h/Storm.h"
#include "../h/dlg_dlg.h"
#include "../h/rect32.h"
#include "../h/tile2.h"      // tile_is_occupied, tile_can_have_creep, creep_rand
#include "../h/grp.h"        // Hyperion: GROUP, FRAME
#include "../port/data/port_formats.h"  // Hyperion: PortGroupNative
#include "../port/render/port_scene.h"  // Hyperion: PortSceneCreep
#include "../h/hxrules.h"              // Hyperion: HxRules
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled, one #ifdef per name. creep_init's
// allocations make the creep map's cells WORDs and the edge map's BYTEs.

#ifndef sg_pTileData
#ifdef OC_DLL_MODE
#define sg_pTileData (*reinterpret_cast<WORD **>(OC_VA(0x6CA7A0u)))
#else
extern WORD *sg_pTileData;
#endif
#endif

#ifndef sg_pCreepMap
#ifdef OC_DLL_MODE
#define sg_pCreepMap (*reinterpret_cast<WORD **>(OC_VA(0x6CA7A4u)))
#else
extern WORD *sg_pCreepMap;
#endif
#endif

#ifndef sg_pEdgeMap
#ifdef OC_DLL_MODE
#define sg_pEdgeMap (*reinterpret_cast<BYTE **>(OC_VA(0x6CA7A8u)))
#else
extern BYTE *sg_pEdgeMap;
#endif
#endif

#ifndef gdwCreepMtxWdt_g
#ifdef OC_DLL_MODE
#define gdwCreepMtxWdt_g (*reinterpret_cast<int *>(OC_VA(0x6CA7ACu)))
#else
extern int gdwCreepMtxWdt_g;
#endif
#endif

#ifndef gdwCreepMtxHgt_g
#ifdef OC_DLL_MODE
#define gdwCreepMtxHgt_g (*reinterpret_cast<int *>(OC_VA(0x6CA7B0u)))
#else
extern int gdwCreepMtxHgt_g;
#endif
#endif

// sg_pEdgeGroup_g is a plain BYTE *: 1.17's cell_draw_creep reaches its records
// with an 8-byte stride from a base displaced by -2.
#ifndef sg_pEdgeGroup_g
#ifdef OC_DLL_MODE
#define sg_pEdgeGroup_g (*reinterpret_cast<BYTE **>(OC_VA(0x6CA7B4u)))
#else
extern BYTE *sg_pEdgeGroup_g;
#endif
#endif

// sg_callback is one 16-byte block of hooks, not four globals: creep_init copies
// it whole.
#ifndef sg_callback
#ifdef OC_DLL_MODE
#define sg_callback (*reinterpret_cast<creep_hooks_t *>(OC_VA(0x6CA7B8u)))
#else
extern creep_hooks_t sg_callback;
#endif
#endif

#ifndef sgbCreepInitCalled_g
#ifdef OC_DLL_MODE
#define sgbCreepInitCalled_g (*reinterpret_cast<int *>(OC_VA(0x6CAA48u)))
#else
extern int sgbCreepInitCalled_g;
#endif
#endif

// byte_6CA7C8 and byte_6CA9C8, the 512-byte and 128-byte tables sub_4BFF60 and
// sub_4C0010 build at runtime, under their placeholder names.
#ifndef byte_6CA7C8
#ifdef OC_DLL_MODE
#define byte_6CA7C8 (reinterpret_cast<BYTE *>(OC_VA(0x6CA7C8u)))
#else
extern BYTE byte_6CA7C8[0x200] OC_UNALIGNED;
#endif
#endif

#ifndef byte_6CA9C8
#ifdef OC_DLL_MODE
#define byte_6CA9C8 (reinterpret_cast<BYTE *>(OC_VA(0x6CA9C8u)))
#else
extern BYTE byte_6CA9C8[0x80] OC_UNALIGNED;
#endif
#endif

// 0x6CAA50 creep_update_targets_g; creep_cell_t is in creep.h.
#ifndef creep_update_targets_g
#ifdef OC_DLL_MODE
#define creep_update_targets_g (reinterpret_cast<creep_cell_t (*)[240]>(OC_VA(0x6CAA50u)))
#else
extern creep_cell_t creep_update_targets_g[8][240];
#endif
#endif

extern "C" {
// rand and srand: OcRand and OcSrand (h/platform.h)
}

// The same two tables, declared a second time.
#ifndef byte_6CA7C8
#ifdef OC_DLL_MODE
#define byte_6CA7C8 (reinterpret_cast<BYTE *>(OC_VA(0x6CA7C8u)))
#else
extern BYTE byte_6CA7C8[0x200] OC_UNALIGNED;
#endif
#endif

#ifndef byte_6CA9C8
#ifdef OC_DLL_MODE
#define byte_6CA9C8 (reinterpret_cast<BYTE *>(OC_VA(0x6CA9C8u)))
#else
extern BYTE byte_6CA9C8[0x80] OC_UNALIGNED;
#endif
#endif

// The four Storm file imports this TU calls, all __stdcall: ordinals 267, 265,
// 269 and 253, in the order below.
extern "C" {
BOOL __stdcall SFileOpenFile(const char *pszFileName, HANDLE *phFile);
DWORD __stdcall SFileGetFileSize(HANDLE hFile, DWORD *pdwSizeHigh);
BOOL __stdcall SFileReadFile(HANDLE hFile, void *pBuffer, DWORD dwToRead,
                             DWORD *pdwRead, void *pOverlapped);
BOOL __stdcall SFileCloseFile(HANDLE hFile);
}

// The 3x3 neighbour table, 1.17's .rdata 0x4F0FD8: the eight neighbours in this
// rotation with the centre last, so an early break in the walk can skip it.
struct creep_offset_t
{
  int dx;
  int dy;
};

static const creep_offset_t s_creepNeighbours[9] = {
    {1, 1}, {0, 1}, {-1, 1}, {1, 0}, {-1, 0}, {1, -1}, {0, -1}, {-1, -1}, {0, 0},
};

// 0x405D40 rle_creep, another TU's, which no header declares.
int __fastcall rle_creep(BYTE *pEdgeRecord, int nParam);

// The bodies, in oracle VA order.

// 0x4BFF60  sub_4BFF60  (175 B)
// Builds the 512-entry creep edge-shape table: the index is a 9-bit neighbour
// mask, the entry a sprite-shape code. The 0x100 arm stores 0x7F and skips the
// rest, including the 0x40 fixup, so it is an else.
void sub_4BFF60()
{
  memset(byte_6CA7C8, 0, 0x200);

  BYTE *p = byte_6CA7C8;
  for (unsigned int i = 0; i < 0x200; i++, p++)
  {
    if (i & 0x100)
    {
      *p = 0x7F;
    }
    else
    {
      if (i & 0x10)
        *p |= 0x09;
      if (i & 0x08)
        *p |= 0x24;
      if (i & 0x40)
        *p |= 0x02;
      if (i & 0x02)
        *p |= 0x10;
      if ((i & 0xC0) == 0xC0)
        *p |= 0x01;
      if ((i & 0x60) == 0x60)
        *p |= 0x04;
      if ((i & 0x06) == 0x06)
        *p |= 0x08;
      if ((i & 0x03) == 0x03)
        *p |= 0x20;

      BYTE bShape = *p;
      if ((bShape & 0x21) == 0x21 || (bShape & 0x0C) == 0x0C)
        *p = bShape | 0x40;
    }
  }
}

// 0x4C0010  sub_4C0010  (77 B)
// Builds the 128-entry inverse of that table: each shape code that occurs in it
// gets the next index, and codes that never occur keep 0. One table is indexed
// and the other walked by pointer, as in 1.17.
void sub_4C0010()
{
  memset(byte_6CA9C8, 0, 0x80);

  BYTE bNext = 0;
  for (unsigned int i = 0; i < 0x80; i++)
  {
    const BYTE *p = byte_6CA7C8;
    for (unsigned int j = 0; j < 0x200; j++, p++)
    {
      if (*p == i)
      {
        byte_6CA9C8[i] = bNext;
        bNext++;
        break;
      }
    }
  }
}

// 0x4C0060  count_neighboring_creep_tiles  (484 B)
// Counts the creep tiles among the eight neighbours of (nX, nY). Each probe is
// the bounds-checked tile_is_creep, inlined; the checks are unsigned.
static inline int creep_neighbour_at(int nX, int nY)
{
  if ((DWORD)nX < (DWORD)gdwCreepMtxWdt_g && (DWORD)nY < (DWORD)gdwCreepMtxHgt_g)
    return tile_is_creep(nX, nY);
  return 0;
}

int count_neighboring_creep_tiles(int nX, int nY)
{
  // A running sum from zero; one expression spills all eight results.
  int nCount = 0;
  nCount += creep_neighbour_at(nX - 1, nY - 1);
  nCount += creep_neighbour_at(nX, nY - 1);
  nCount += creep_neighbour_at(nX + 1, nY - 1);
  nCount += creep_neighbour_at(nX - 1, nY);
  nCount += creep_neighbour_at(nX + 1, nY);
  nCount += creep_neighbour_at(nX - 1, nY + 1);
  nCount += creep_neighbour_at(nX, nY + 1);
  nCount += creep_neighbour_at(nX + 1, nY + 1);
  return nCount;
}

// 0x4C0250  sub_4C0250  (156 B)
// Builds the creep search box, 640 by 400 pixels either side of the centre of
// tile (nX, nY), clipped to the map, and hands it to the callback.
int sub_4C0250(int nX, int nY, creep_box_cb_t pfnCb)
{
  rect32_t box;
  int nPx = nX * 32 + 16;
  int nPy = nY * 32 + 16;
  box.left = nPx - 640;
  box.right = nPx + 640;
  box.top = nPy - 400;
  box.bottom = nPy + 400;
  if (box.left < 0)
    box.left = 0;
  else if (box.right >= gdwCreepMtxWdt_g * 32)
    box.right = gdwCreepMtxWdt_g * 32 - 1;
  if (box.top < 0)
    box.top = 0;
  else if (box.bottom >= gdwCreepMtxHgt_g * 32)
    box.bottom = gdwCreepMtxHgt_g * 32 - 1;
  return pfnCb(nX, nY, &box);
}

// 0x4C02F0  getEdgeMap  (37 B)
BYTE *getEdgeMap(int *pcbSize)
{
  BYTE *pMap = sg_pEdgeMap;
  if (pMap == NULL)
    return NULL;
  if (pcbSize)
    *pcbSize = gdwCreepMtxWdt_g * gdwCreepMtxHgt_g;
  return pMap;
}

// 0x4C0320  getCreepMap  (39 B)
// getEdgeMap with the size doubled, for the creep map's WORD cells.
WORD *getCreepMap(int *pcbSize)
{
  WORD *pMap = sg_pCreepMap;
  if (pMap == NULL)
    return NULL;
  if (pcbSize)
    *pcbSize = gdwCreepMtxWdt_g * gdwCreepMtxHgt_g * 2;
  return pMap;
}

// 0x4C0350  creep_free  (82 B)
// The line numbers 368 to 370 are 1.17's, from its own creep.cpp.
void creep_free()
{
  if (sg_pCreepMap)
    SMemFree(sg_pCreepMap, "creep.cpp", 368, 0);
  if (sg_pEdgeMap)
    SMemFree(sg_pEdgeMap, "creep.cpp", 369, 0);
  if (sg_pEdgeGroup_g)
    SMemFree(sg_pEdgeGroup_g, "creep.cpp", 370, 0);
}

// 0x4C03B0  cell_draw_creep  (52 B)
// Draws the creep border of cell (nX, nY), if it has one, from the edge group.
void cell_draw_creep(int nX, int nY, int nParam)
{
  BYTE bEdge = sg_pEdgeMap[gdwCreepMtxWdt_g * nY + nX];
  if (bEdge) {
    // Hyperion: frame bEdge - 1 of the edge group, by its type, which was
    // `sg_pEdgeGroup_g + bEdge * 8 - 2`.
    const FRAME *pFrame = &reinterpret_cast<GROUP *>(sg_pEdgeGroup_g)->frames[bEdge - 1];
    if (!PortSceneCreep(nX, nY, pFrame, bEdge - 1))  // Hyperion: the GPU scene (port_scene.cpp)
      rle_creep(reinterpret_cast<BYTE *>(const_cast<FRAME *>(pFrame)), nParam);
  }
}

// 0x4C03F0  apply_tile_change_edges  (125 B)
// Runs the edge callback on each in-map cell of the 3x3 around (nX, nY) until
// one returns FALSE, and returns the index it stopped at.
int apply_tile_change_edges(int nX, int nY, creep_edge_cb_t pfnCb, intptr_t nParam)
{
  unsigned int i;

  for (i = 0; i < 9; i++)
  {
    int x = nX + s_creepNeighbours[i].dx;
    int y = nY + s_creepNeighbours[i].dy;

    if (x >= 0 && x < gdwCreepMtxWdt_g && y >= 0 && y < gdwCreepMtxHgt_g)
    {
      if (!pfnCb(i, &sg_pTileData[gdwCreepMtxWdt_g * y + x], x, y, nParam))
        break;
    }
  }

  return i;
}

// 0x4C0470  update_creep_edges_cb  (118 B)
// The creep predicate is written out rather than called. The flag local is an
// int whose low byte alone is set, as in 1.17: creep_set_flag_if_covered_cb ORs
// bits into that byte, and sub_4C0D90 reads only AL of the dword passed on.
BOOL __stdcall update_creep_edges_cb(int nIndex, WORD *pTile, int nX, int nY,
                                     intptr_t nParam)
{
  int nIsCreep;
  int nFlags;

  if (sg_callback.pfnIsCovered == NULL)
    nIsCreep = (sg_pTileData[gdwCreepMtxWdt_g * nY + nX] & 0x7FF0) == 0x10;
  else
    nIsCreep = sg_callback.pfnIsCovered(nX, nY);

  if (!nIsCreep)
  {
    *(BYTE *)&nFlags = 0;
    // The cast is redundant now that creep.h gives the callback the edge shape.
    apply_tile_change_edges(nX, nY,
                            (creep_edge_cb_t)creep_set_flag_if_covered_cb,
                            (intptr_t)(BYTE *)&nFlags);
    apply_tile_change_direct(nX, nY, sub_4C0D90, nFlags);
  }

  return TRUE;
}

// 0x4C04F0  get_max_creep_bounding_box  (381 B)
//
// Fills the box with the tile rectangle a creep source of this type covers at
// pixel (nX, nY): a fixed 640 x 400 box for the types that take one, else the
// placebox. Returns TRUE for the fixed box. The fixed arm stores left, right,
// top, bottom; the placebox arm stores in field order, each value just before
// its store. Any other order moves the type out of ECX.
BOOL get_max_creep_bounding_box(short sType, int nX, int nY, void *pArg3, int bUseFixed)
{
  rect32_t *pBox = static_cast<rect32_t *>(pArg3);
  switch ((WORD)sType)
  {
    case 0x83:
    case 0x8F:
      if (!bUseFixed)
        break;
    case 0x84:
    case 0x85:
    case 0x90:
    case 0x92:
    {
      int nL = (nX - 320) / 32;
      pBox->left = nL;
      int nR = (nX + 320) / 32;
      pBox->right = nR;
      int nT = (nY - 200) / 32;
      pBox->top = nT;
      int nB = (nY + 200) / 32;
      pBox->bottom = nB;
      if (nL < 0)
        pBox->left = 0;
      else if (nR >= gdwCreepMtxWdt_g)
        pBox->right = gdwCreepMtxWdt_g - 1;
      if (nT < 0)
        pBox->top = 0;
      else if (nB >= gdwCreepMtxHgt_g)
        pBox->bottom = gdwCreepMtxHgt_g - 1;
      return TRUE;
    }
  }
  int nWidth = gUnitPlaceboxSize[(WORD)sType].x / 32;
  int nHeight = gUnitPlaceboxSize[(WORD)sType].y / 32;
  int nL = PosS(nX / 32) - nWidth / 2;
  pBox->left = nL;
  int nT = PosS(nY / 32) - nHeight / 2;
  pBox->top = nT;
  int nR = nWidth - 1 + nL;
  pBox->right = nR;
  int nB = nHeight - 1 + nT;
  pBox->bottom = nB;
  if (nL < 0)
    pBox->left = 0;
  else if (nR >= gdwCreepMtxWdt_g)
    pBox->right = gdwCreepMtxWdt_g - 1;
  if (nT < 0)
    pBox->top = 0;
  else if (nB >= gdwCreepMtxHgt_g)
    pBox->bottom = gdwCreepMtxHgt_g - 1;
  return FALSE;
}

// 0x4C0790  unit_type_spreads_creep  (39 B)
// A two-level jump table over the sixteen unit types 0x83 to 0x92, as in 1.17.
BOOL unit_type_spreads_creep(int nUnitType, int nOther)
{
  // 1.17's index array at 0x4C07C4 is 0 1 1 2 2 2 2 2 2 2 2 2 0 1 2 1, and its
  // three arms return the second argument, TRUE, and FALSE, the default.
  switch ((WORD)nUnitType)
  {
    case 0x83:
    case 0x8F:
      return nOther;
    case 0x84:
    case 0x85:
    case 0x90:
    case 0x92:
      return TRUE;
  }
  return FALSE;
}

// 0x4C07E0  tile_is_creep  (40 B)
// tile_is_creep_ptr is inlined here, as in 1.17, so the two stay in one TU.
int tile_is_creep(int nX, int nY)
{
  return tile_is_creep_ptr(&sg_pTileData[gdwCreepMtxWdt_g * nY + nX]);
}

// 0x4C0810  tile_is_creep_ptr  (21 B)
int tile_is_creep_ptr(const WORD *pTile)
{
  return (*pTile & 0x7FF0) == 0x10;
}

// 0x4C0830  sub_4C0830  (136 B)
// Whether tile (nX, nY) lies inside the creep ellipse of a creep-spreading
// building of type nType standing at pixel (nPosX, nPosY).
BOOL sub_4C0830(int nX, int nY, int nType, int nPosX, int nPosY, int nArg5)
{
  // Separate early returns, one per term, place the shared zero exit first.
  WORD wType = (WORD)nType;
  if (!(gulUnitTypeFlags[wType] & 1))
    return FALSE;
  if (!(gubUnitCountInfo[wType] & 1))
    return FALSE;
  rect32_t box;
  get_max_creep_bounding_box((short)nType, nPosX, nPosY, &box, nArg5);
  if (nX < box.left)
    return FALSE;
  if (nX > box.right)
    return FALSE;
  if (nY < box.top)
    return FALSE;
  if (nY > box.bottom)
    return FALSE;
  int nDx = nX * 32 - nPosX + 16;
  int nDy = nY * 32 - nPosY + 16;
  return nDx * nDx * 100 + nDy * nDy * 256 <= 0x9C4000;
}

// 0x4C0A80  creep_within_ellipse  (26 B)
// TRUE outside the ellipse, despite the name; see the declaration.
BOOL creep_within_ellipse(int nDx, int nDy)
{
  return nDx * nDx * 100 + nDy * nDy * 256 > 0x9C4000;
}

// 0x4C0BA0  update_creep_edges_at  (18 B)
BOOL update_creep_edges_at(int nX, int nY)
{
  apply_tile_change_edges(nX, nY, update_creep_edges_cb, 0);
  return TRUE;
}

// 0x4C0BC0  uncover_creep_tile_cb  (49 B)
// Uncovers one cell: the tile gets back the value the creep map saved for it,
// and the saved value is cleared.
BOOL __stdcall uncover_creep_tile_cb(WORD *pCreep, BYTE *pEdge, int *pParamOut,
                                     WORD *pTile, int nParam)
{
  WORD wCreep = *pCreep;
  if (wCreep == 0)
    return FALSE;
  *pTile = wCreep;
  *pCreep = 0;
  *pParamOut = 1;
  return TRUE;
}

// 0x4C0C00  uncover_creep_at  (59 B)
// Uncovers cell (nX, nY) and, unless the changed hook takes over, updates the
// edges around it. cover_creep_at is the same body with the other callback.
BOOL uncover_creep_at(int nX, int nY)
{
  apply_tile_change_direct(nX, nY, uncover_creep_tile_cb, 0);
  // The hook is read once into a local, as in 1.17.
  BOOL(__stdcall * pfnChanged)(int, int) = sg_callback.pfnCellChanged;
  if (pfnChanged == NULL || !pfnChanged(nX, nY))
    apply_tile_change_edges(nX, nY, update_creep_edges_cb, 0);
  return TRUE;
}

// Hyperion: the cell cover_creep_at covers and the frame counter, for the tile's
// picture by Hyperion's rules (revision 19). 1.17 draws it from the C runtime's
// random, which the live game also draws for its units' sounds and its replay does not, so
// a replay drew other creep tiles than its game (the tracker's "Some replays draw other
// creep tiles"). The cell and the frame are the same in the game, its replay and its save.
static int s_nCoverX, s_nCoverY;
extern unsigned int oc_dwReplayFrame;  // 0x52651C, the frame counter

static DWORD HxCreepRoll(int nX, int nY)
{
  DWORD h = static_cast<DWORD>(nX) * 0x9E3779B1u ^ static_cast<DWORD>(nY) * 0x85EBCA77u ^
            oc_dwReplayFrame * 0xC2B2AE3Du;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return h;
}

// 0x4C0C40  cover_creep_tile_cb  (96 B)
// Covers one cell: saves its tile in the creep map unless one is saved, and
// gives it a random creep tile, 4% of the time one of seven rarer ones. The test
// is `>= 4` with the `% 6` arm first, the order 1.17's branch takes.
BOOL __stdcall cover_creep_tile_cb(WORD *pCreep, BYTE *pEdge, int *pParamOut,
                                   WORD *pTile, int nParam)
{
  int nEdge;
  if (*pCreep == 0)
    *pCreep = *pTile;
  if (HxRules()) {  // Hyperion: from the cell and the frame (revision 19)
    const DWORD h = HxCreepRoll(s_nCoverX, s_nCoverY);
    nEdge = h % 100 >= 4 ? (h >> 8) % 6 : (h >> 8) % 7 + 6;
  } else if (OcRand() % 100 >= 4)
    nEdge = OcRand() % 6;
  else
    nEdge = OcRand() % 7 + 6;
  *pTile = (WORD)(nEdge + 0x10);
  *pParamOut = 0;
  return TRUE;
}

// 0x4C0D90  sub_4C0D90  (45 B)
// Sets the cell's edge shape from the neighbour mask. nParam is an int cast to
// BYTE: the caller writes only the low byte of the dword it passes.
BOOL __stdcall sub_4C0D90(WORD *pCreep, BYTE *pEdge, int *pParamOut,
                          WORD *pTile, int nParam)
{
  *pEdge = byte_6CA9C8[byte_6CA7C8[(BYTE)nParam]];
  *pParamOut = 2;
  return TRUE;
}

// 0x4C0E00  creep_set_flag_if_covered_cb  (88 B)
// An edge callback: update_creep_edges_cb passes it to apply_tile_change_edges,
// and it sets bit nIndex of the caller's flags byte when that neighbour is
// creep. The predicate is written out, as 1.17 inlines tile_is_creep here.

BOOL __stdcall creep_set_flag_if_covered_cb(int nIndex, WORD *pTile, int nX,
                                            int nY, intptr_t nParam)
{
  // nParam points at the flags byte in update_creep_edges_cb's frame; taking it
  // as an intptr_t keeps the function a creep_edge_cb_t.
  BYTE *pFlags = reinterpret_cast<BYTE *>(nParam);
  BOOL(__stdcall * pfnIsCovered)(int, int) = sg_callback.pfnIsCovered;
  int nCovered;

  if (pfnIsCovered == NULL)
    nCovered = (sg_pTileData[gdwCreepMtxWdt_g * nY + nX] & 0x7FF0) == 0x10;
  else
    nCovered = pfnIsCovered(nX, nY);

  if (nCovered)
    *pFlags |= (BYTE)(1 << nIndex);
  return TRUE;
}

// 0x4C0DC0  cover_creep_at  (59 B)
// uncover_creep_at with the covering callback; the hook is read once into a
// local, as there.
BOOL cover_creep_at(int nX, int nY)
{
  s_nCoverX = nX;  // Hyperion: for HxCreepRoll
  s_nCoverY = nY;
  apply_tile_change_direct(nX, nY, cover_creep_tile_cb, 0);
  BOOL(__stdcall * pfnChanged)(int, int) = sg_callback.pfnCellChanged;
  if (pfnChanged == NULL || !pfnChanged(nX, nY))
    apply_tile_change_edges(nX, nY, update_creep_edges_cb, 0);
  return TRUE;
}

// 0x4BE950, the one-byte `ret` 1.17 folds every empty function onto. Its two
// calls in creep_load_edge_group push two words and clean them up, so this
// spelling is __cdecl, apart from net_cmds.h's.
extern "C" void __cdecl voice_squelch(int nErrCode, const char *pszFileName);

// 0x4C08C0  creep_load_edge_group  (153 B)
// Reads the edge-group file whole and rebases each record's file-relative
// offset onto the loaded block. Neither voice_squelch call returns early: a
// missing file runs on with a NULL handle, as in 1.17.
BYTE *creep_load_edge_group(const char *pszFileName)
{
  HANDLE hFile;

  if (!SFileOpenFile(pszFileName, &hFile))
    voice_squelch(2, pszFileName);

  DWORD dwSize = SFileGetFileSize(hFile, NULL);
  if (!dwSize)
    voice_squelch(0x28, pszFileName);

  BYTE *pData = (BYTE *)SMemAlloc(dwSize, "creep.cpp", 215, 0);
  SFileReadFile(hFile, pData, dwSize, NULL, NULL);
  SFileCloseFile(hFile);

#if defined(_WIN64) || defined(__LP64__)
  // Hyperion: the pointers do not fit the file's frames, so the group gets a
  // frame table of its own (PortGroupNative).
  pData = reinterpret_cast<BYTE *>(PortGroupNative(reinterpret_cast<GROUP *>(pData)));
#else
  unsigned int nCount = *(const WORD *)pData & 0x7FFF;
  if (nCount)
  {
    DWORD *pEntry = (DWORD *)(pData + nCount * 8 + 0x0A);
    do
    {
      pEntry = (DWORD *)((BYTE *)pEntry - 8);
      *pEntry = (*pEntry & 0x7FFFFFFF) + (DWORD_PTR)pData;
    } while (--nCount);
  }
#endif

  return pData;
}

// 0x4C0690  creep_init  (250 B)
// Takes the hooks, the tile data and the map size, reallocates the two maps,
// loads the edge group and builds the shape tables once. sg_callback is assigned
// first and whole, as 1.17's movdqu pair does, and both sizes are read back from
// the globals. The line numbers 419 to 427 are 1.17's.
void creep_init(WORD *pTileData, const creep_hooks_t *pHooks, int nWidth,
                int nHeight, const char *pszEdgeGroupFile)
{
  sg_callback = *pHooks;
  sg_pTileData = pTileData;
  gdwCreepMtxWdt_g = nWidth;
  gdwCreepMtxHgt_g = nHeight;

  if (sg_pCreepMap)
    SMemFree(sg_pCreepMap, "creep.cpp", 419, 0);
  sg_pCreepMap = (WORD *)SMemAlloc(gdwCreepMtxHgt_g * gdwCreepMtxWdt_g * 2,
                                   "creep.cpp", 420, 8);

  if (sg_pEdgeMap)
    SMemFree(sg_pEdgeMap, "creep.cpp", 423, 0);
  sg_pEdgeMap = (BYTE *)SMemAlloc(gdwCreepMtxHgt_g * gdwCreepMtxWdt_g,
                                  "creep.cpp", 424, 8);

  if (sg_pEdgeGroup_g)
    SMemFree(sg_pEdgeGroup_g, "creep.cpp", 427, 0);
  sg_pEdgeGroup_g = creep_load_edge_group(pszEdgeGroupFile);

  if (!sgbCreepInitCalled_g)
  {
    sgbCreepInitCalled_g = 1;
    sub_4BFF60();
    sub_4C0010();
    app_add_exit(creep_free);
  }
}

// 0x4C0CA0  apply_tile_change_direct  (235 B)
// The record the pfnCellRecord hook receives, 24 bytes of 1.17's frame.
struct creep_change_t
{
  /*0x00*/ WORD *pCreep;
  /*0x04*/ WORD wNewCreep;
  /*0x06*/ WORD wOldCreep;
  /*0x08*/ BYTE *pEdge;
  /*0x0C*/ BYTE bNewEdge;
  /*0x0D*/ BYTE bOldEdge;
  /*0x10*/ WORD *pTile;
  /*0x14*/ WORD wNewTile;
  /*0x16*/ WORD wOldTile;
};

// Runs the cell callback on one creep cell, then reports the change to the record hook
// and the cell's pixel box to the redraw hook. The box's right and bottom are set to the
// next tile first and scaled last, so both stay live in ECX and EDX: the redraw hook goes
// to ESI and nY is read from its slot.
void apply_tile_change_direct(int nX, int nY, creep_cell_cb_t pfnCb, int nParam)
{
  int rcCell[4];
  WORD *pCreep = &sg_pCreepMap[gdwCreepMtxWdt_g * nY + nX];
  WORD *pTile = &sg_pTileData[gdwCreepMtxWdt_g * nY + nX];
  BYTE *pEdge = &sg_pEdgeMap[gdwCreepMtxWdt_g * nY + nX];
  int nOldTile = *pTile;
  int nOldCreep = *pCreep;
  BYTE bOldEdge = *pEdge;

  if (!pfnCb(pCreep, pEdge, &nParam, pTile, nParam))
    return;

  if (sg_callback.pfnCellRecord)
  {
    creep_change_t rec;
    rec.pCreep = pCreep;
    rec.wNewCreep = *pCreep;
    rec.wOldCreep = (WORD)nOldCreep;
    rec.pEdge = pEdge;
    rec.bNewEdge = *pEdge;
    rec.bOldEdge = bOldEdge;
    rec.pTile = pTile;
    rec.wNewTile = *pTile;
    rec.wOldTile = (WORD)nOldTile;
    sg_callback.pfnCellRecord(&rec);
  }

  if (sg_callback.pfnRedrawCell)
  {
    rcCell[2] = nX + 1;
    rcCell[3] = nY + 1;
    rcCell[0] = nX << 5;
    rcCell[1] = nY << 5;
    rcCell[2] <<= 5;
    rcCell[3] <<= 5;
    sg_callback.pfnRedrawCell(nX, nY, nParam, *pTile, rcCell);
  }
}

// 0x4C0960  remove_creep_completely  (280 B)
//
// Uncovers every creep tile in the type's box that no other creep source still
// supports, as pfnSupport reports through sub_4C0250.
BOOL remove_creep_completely(int nType, int nX, int nY, creep_box_cb_t pfnSupport)
{
  int aBox[4];  // left, top, right, bottom -- tile coordinates
  int bEllipse = get_max_creep_bounding_box((short)nType, nX, nY, aBox, 1);

  int nLeft = aBox[0];
  int dy = (aBox[1] << 5) - nY + 16;
  for (int y = aBox[1]; y <= aBox[3]; y++, dy += 32)
  {
    int dx = (nLeft << 5) - nX + 16;
    for (int x = aBox[0]; x <= aBox[2]; x++, dx += 32)
    {
      if (!tile_is_creep(x, y))
        continue;
      if (bEllipse && creep_within_ellipse(dx, dy))
        continue;
      if (sub_4C0250(x, y, pfnSupport))
        continue;
      uncover_creep_at(x, y);
    }
  }
  return TRUE;
}

// 0x4C0E60  spread_creep  (442 B)
//
// Grows creep by one tile around a creep source of type nType at pixel
// (nX, nY): among the uncovered, placeable cells in its box it picks one at
// random from the best-connected bucket and covers it. Returns 1 on growth,
// else 2 if a cell was blocked by a unit, else 0.
BYTE spread_creep(WORD nType, int nX, int nY)
{
  DWORD aCount[8];
  int aBox[4];
  BYTE bResult = 0;
  memset(aCount, 0, sizeof(aCount));
  int bEllipse = get_max_creep_bounding_box((short)nType, nX, nY, aBox, 1);

  int nLeft = aBox[0];
  int dy = (aBox[1] << 5) - nY + 16;
  for (int y = aBox[1]; y <= aBox[3]; y++, dy += 32)
  {
    int dx = (nLeft << 5) - nX + 16;
    for (int x = aBox[0]; x <= aBox[2]; x++, dx += 32)
    {
      if (tile_is_creep(x, y))
        continue;
      if (tile_can_have_creep((WORD)x, (WORD)y))
        continue;
      if (tile_is_occupied((WORD)x, (WORD)y))
      {
        bResult = 2;
        continue;
      }
      int nIdx = count_neighboring_creep_tiles(x, y) - 1;
      if ((unsigned int)nIdx > 7)
        continue;
      if (bEllipse && creep_within_ellipse(dx, dy))
        continue;
      creep_update_targets_g[nIdx][aCount[nIdx]].x = x;
      creep_update_targets_g[nIdx][aCount[nIdx]].y = y;
      aCount[nIdx]++;
    }
  }

  int i = 8;
  while (i--)
  {
    if (aCount[i] == 0)
      continue;
    unsigned int k = ((DWORD)creep_rand() >> 4) % aCount[i];
    cover_creep_at(creep_update_targets_g[i][k].x,
                   creep_update_targets_g[i][k].y);
    return 1;
  }
  return bResult;
}

// 0x4C0AA0  spread_creep_completely  (247 B)
//
// Covers the type's placebox with creep, then grows it until spread_creep
// stops reporting growth. nTop is written self-contained, not through nHeight,
// for the oracle's `lea eax, [esi+edi]` operand order.
void spread_creep_completely(WORD wType, int nX, int nY)
{
  point16_t placebox = gUnitPlaceboxSize[wType];
  int nWidth = placebox.x / 32;
  int nLeft = nX / 32 - nWidth / 2;
  int nHeight = placebox.y / 32;
  unsigned int nTop = nY / 32 - placebox.y / 32 / 2;
  unsigned int nBottom = nHeight + nTop;
  for (unsigned int y = nTop; y < nBottom; y++)
  {
    for (unsigned int x = nLeft; x < nLeft + (unsigned int)nWidth; x++)
    {
      if (!tile_can_have_creep(x, y))
        cover_creep_at(x, y);
    }
  }

  while (spread_creep(wType, nX, nY) == 1)
    ;
}
