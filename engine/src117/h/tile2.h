// tile2.h -- terrain tile queries, the decaying-creep node pool and the per-tile
// thing position lists.
//
// Free functions are spelled without an explicit __fastcall, which /Gr makes the
// default.
//
// Four of this TU's functions are declared in gamemap.h (the creep_hooks_t slots
// gamemap_init fills) and sub_4B47F0 in CUnitTerran.h. Three more are static in
// the .cpp and not declared here.

#ifndef OC117_TILE2_H
#define OC117_TILE2_H

#include "platform.h"
#include "rect32.h"

class CUnit;

// The decaying-creep node, 20 bytes, 1024 of them in the 0x5000-byte pool. A
// node is on one of ten lists, bList (0 to 8 the count of creep neighbours, 9
// the free list), and on its tile's position bucket.
struct decaying_creep_t {
  /*0x00*/ decaying_creep_t *pNext;     // the bList list
  /*0x04*/ decaying_creep_t *pPrev;
  /*0x08*/ decaying_creep_t *pPosNext;  // the position bucket
  /*0x0C*/ decaying_creep_t *pPosPrev;
  /*0x10*/ BYTE bX;                     // the tile
  /*0x11*/ BYTE bY;
  /*0x12*/ BYTE bList;
  /*0x13*/ BYTE bPad;                   // unread in 1.17;
                                        //   Hyperion: the tile's high bits on a
                                        //   map past 256 tiles (tile2.cpp)
};

OC_X86_LAYOUT(sizeof(decaying_creep_t)              == 0x14, "decaying_creep_t: 0x5000 pool / 1024 nodes");
static_assert(offsetof(decaying_creep_t, pNext)     == 0x00, "decaying_creep_t::pNext");
OC_X86_LAYOUT(offsetof(decaying_creep_t, pPrev)     == 0x04, "decaying_creep_t::pPrev");
OC_X86_LAYOUT(offsetof(decaying_creep_t, pPosNext)  == 0x08, "decaying_creep_t::pPosNext");
OC_X86_LAYOUT(offsetof(decaying_creep_t, pPosPrev)  == 0x0C, "decaying_creep_t::pPosPrev");
OC_X86_LAYOUT(offsetof(decaying_creep_t, bX)        == 0x10, "decaying_creep_t::bX");
OC_X86_LAYOUT(offsetof(decaying_creep_t, bY)        == 0x11, "decaying_creep_t::bY");
OC_X86_LAYOUT(offsetof(decaying_creep_t, bList)     == 0x12, "decaying_creep_t::bList");
OC_X86_LAYOUT(offsetof(decaying_creep_t, bPad)      == 0x13, "decaying_creep_t::bPad");

// 0x4B3F30 (220 B). Takes a node off the free list for (nX, nY); 0 when the
// free list is empty.
int sub_4B3F30(int nX, int nY);

// The three node helpers below are the hooked external copies. tile2.cpp's own
// callers use static twins with the same bytes (the *_local names).
void sub_4B4390(decaying_creep_t *pNode);
void sub_4B4D10(decaying_creep_t *pNode);
void removeThingFromPosList(decaying_creep_t *pNode);

// 0x4B4010 (43 B). One decay tick.
void sub_4B4010();

// 0x4B4550 (13 B).
int tile_is_occupied(int nX, int nY);

// 0x4B45B0 (64 B).
int tile_can_have_creep(int nX, int nY);

// 0x4B4620 (189 B). Reads the pool back from the save file pFile.
BOOL tile2_read_from_save(void *pFile);

// 0x4B4920 (10 B). A creep roll on tag 0x1A.
int creep_rand();

// 0x4B4930 (215 B). noinline, as 1.17 has it: sub_4B4DD0 would inline it.
__declspec(noinline) int sub_4B4930(int nX, int nY);

// 0x4B4A40 (313 B). Decays one random node.
void sub_4B4A40();

// 0x4B4EB0 (156 B). Repaints a map rectangle.
void sub_4B4EB0(rect32_t *pRect);

// 0x4B46E0 (271 B). CUnitZBuild.cpp's creep walk calls it too. The type is
// compared in 16 bits, so the first parameter is a WORD.
int sub_4B46E0(WORD wUnitType, int nPosX, int nPosY, int nArg3);

// 0x4B47F0 (302 B) is declared in CUnitTerran.h, not here.

// 0x4B4170 (185 B). __stdcall: sub_4B46E0 and sub_4B47F0 hand its address to
// sub_4C0250, whose call does no stack cleanup.
BOOL __stdcall FindCreepSupport(int nX, int nY, rect32_t *pRect);

// 0x4B4230 (339 B)
void sub_4B4230(int nX, int nY);

// 0x4B4430 (103 B)
void addThingToPosList(decaying_creep_t *pNode);

// 0x4B4B80 (388 B)
int sub_4B4B80(void *pFile);

// 0x4B4DD0 (216 B)
void sub_4B4DD0(CUnit *pUnit);

#endif  // OC117_TILE2_H
