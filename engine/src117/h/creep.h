// creep.h -- declarations for src117/lang/creep.cpp: the creep layer, the
// per-tile creep matrix Zerg structures spread and the terrain renderer
// consults, and the edge bookkeeping that picks each cell's creep border. The
// TU's name is 1.17's own: creep_free and creep_init tag their Storm
// allocations with the .rdata string "creep.cpp".
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default. maphdr.cpp includes it for
// unit_type_spreads_creep and spread_creep_completely.

#ifndef OC117_CREEP_H
#define OC117_CREEP_H

#include "platform.h"

// The two callback shapes, both __stdcall with five stack words, for the two
// appliers push different things. apply_tile_change_edges passes the neighbour
// index (0 to 8), &sg_pTileData[cell], the neighbour's x and y and the caller's
// parameter. apply_tile_change_direct passes the cell's creep, edge and tile
// pointers and its parameter both by address and by value: the address is how
// a callback reports back.
typedef BOOL(__stdcall *creep_edge_cb_t)(int nIndex, WORD *pTile, int nX,
                                         int nY, intptr_t nParam);
typedef BOOL(__stdcall *creep_cell_cb_t)(WORD *pCreep, BYTE *pEdge,
                                         int *pParamOut, WORD *pTile,
                                         int nParam);

// One cell of creep_update_targets_g @0x6CAA50: spread_creep's candidate lists,
// eight buckets by neighbour count, 240 cells each, an (x, y) pair per cell.
struct creep_cell_t {
  /*0x00*/ int x;
  /*0x04*/ int y;
};
static_assert(sizeof(creep_cell_t) == 0x08, "creep_cell_t  [eax*8+6CAA50h] / [eax*8+6CAA54h]");
static_assert(offsetof(creep_cell_t, x) == 0x00, "creep_cell_t::x  mov [eax*8+6CAA50h], esi");
static_assert(offsetof(creep_cell_t, y) == 0x04, "creep_cell_t::y  mov [eax*8+6CAA54h], edi");

// The 16-byte hook block creep_init copies into sg_callback, four slots. Every
// slot is __stdcall: each call site pushes its arguments and does not clean
// up. The third is consulted by uncover_creep_at and cover_creep_at, the fourth
// by apply_tile_change_direct.
struct creep_hooks_t {
  // Called with the cell's 32 by 32 pixel rectangle as four ints, left, top,
  // right and bottom: a redraw notification.
  /*0x00*/ void(__stdcall *pfnRedrawCell)(int nX, int nY, int nParam,
                                          WORD nTile, int *pBox);
  // A creep-coverage override: when it is NULL, update_creep_edges_cb and
  // creep_set_flag_if_covered_cb ask tile_is_creep the same question.
  /*0x04*/ BOOL(__stdcall *pfnIsCovered)(int nX, int nY);
  /*0x08*/ BOOL(__stdcall *pfnCellChanged)(int nX, int nY);
  /*0x0C*/ void(__stdcall *pfnCellRecord)(void *pRecord);
};

// 0x4C02F0 getEdgeMap (37 B) / 0x4C0320 getCreepMap (39 B). Each returns its
// map and, when pcbSize is not NULL, writes the map's size in bytes: height *
// width for the BYTE edge map, twice that for the WORD creep map.
BYTE *getEdgeMap(int *pcbSize);
WORD *getCreepMap(int *pcbSize);

// 0x4C0350 creep_free (82 B). Nullary; creep_init registers it with
// app_add_exit.
void creep_free();

// 0x4C03B0 cell_draw_creep (52 B) -- fastcall ECX+EDX + `ret 4h`.
void cell_draw_creep(int nX, int nY, int nParam);

// 0x4C03F0 apply_tile_change_edges (125 B) -- fastcall ECX+EDX + `ret 8h`.
// Walks the nine cells of the 3x3 neighbourhood from the {dx, dy} table at
// 0x4F0FD8, bounds-checks each against gdwCreepMtxWdt_g / gdwCreepMtxHgt_g, and
// calls the callback until it returns FALSE. Returns the index it stopped at.
int apply_tile_change_edges(int nX, int nY, creep_edge_cb_t pfnCb, intptr_t nParam);

// 0x4C0CA0 apply_tile_change_direct (235 B) -- fastcall ECX+EDX + `ret 8h`.
void apply_tile_change_direct(int nX, int nY, creep_cell_cb_t pfnCb,
                              int nParam);

// 0x4C0470 update_creep_edges_cb (118 B) -- an edge-shape callback.
BOOL __stdcall update_creep_edges_cb(int nIndex, WORD *pTile, int nX, int nY,
                                     intptr_t nParam);

// 0x4C0BC0 uncover_creep_tile_cb (49 B) / 0x4C0C40 cover_creep_tile_cb (96 B)
// / 0x4C0D90 sub_4C0D90 (45 B) -- cell-shape callbacks.
BOOL __stdcall uncover_creep_tile_cb(WORD *pCreep, BYTE *pEdge, int *pParamOut,
                                     WORD *pTile, int nParam);
BOOL __stdcall cover_creep_tile_cb(WORD *pCreep, BYTE *pEdge, int *pParamOut,
                                   WORD *pTile, int nParam);
BOOL __stdcall sub_4C0D90(WORD *pCreep, BYTE *pEdge, int *pParamOut,
                          WORD *pTile, int nParam);

// 0x4C0E00 creep_set_flag_if_covered_cb (88 B) -- an edge-shape callback, which
// update_creep_edges_cb hands to apply_tile_change_edges. It turns the
// neighbour index into a bit and ORs it into the BYTE nParam points at; nParam
// stays an integer so the function is a creep_edge_cb_t with no cast.
BOOL __stdcall creep_set_flag_if_covered_cb(int nIndex, WORD *pTile, int nX,
                                            int nY, intptr_t nParam);

// 0x4C0BA0 update_creep_edges_at (18 B) -- forwards both arguments to
// apply_tile_change_edges.
BOOL update_creep_edges_at(int nX, int nY);

// 0x4C0C00 uncover_creep_at (59 B) / 0x4C0DC0 cover_creep_at (59 B) -- the same
// body with a different callback, 0x4C0BC0 against 0x4C0C40. Both consult the
// hook block's third slot before touching the edges.
BOOL uncover_creep_at(int nX, int nY);
BOOL cover_creep_at(int nX, int nY);

// 0x4C07E0 tile_is_creep (40 B) / 0x4C0810 tile_is_creep_ptr (21 B). 1.17
// inlines tile_is_creep_ptr into tile_is_creep.
int tile_is_creep_ptr(const WORD *pTile);
int tile_is_creep(int nX, int nY);

// 0x4C0A80 creep_within_ellipse (26 B) -- 100*dx*dx + 256*dy*dy > 0x9C4000.
// Note the sense: it returns TRUE when the offset is outside the ellipse, the
// opposite of what Blizzard's name reads as.
BOOL creep_within_ellipse(int nDx, int nDy);

// 0x4C0790 unit_type_spreads_creep (39 B) -- a jump table over the sixteen unit
// types 0x83 to 0x92. The parameter is `int` and the body narrows it to 16
// bits: unit_create widens the type with movzx, which a WORD parameter would
// not.
BOOL unit_type_spreads_creep(int nUnitType, int nOther);

// 0x4C0AA0 spread_creep_completely (247 B) -- fastcall ECX+EDX + `ret 4h`. The
// type is a WORD: maphdr.cpp's call writes only CX, leaving the high half.
void spread_creep_completely(WORD wType, int nX, int nY);

// 0x4C0E60 spread_creep (442 B) -- `ret 4h`. Returns a BYTE: both consuming
// callers use only AL. The type is a WORD, as spread_creep_completely passes
// it.
BYTE spread_creep(WORD wType, int nX, int nY);

// 0x4C0960 remove_creep_completely (280 B) -- fastcall + `ret 8h`. The last
// argument is the support callback it hands to sub_4C0250.
struct rect32_t;
typedef BOOL(__stdcall *creep_box_cb_t)(int nX, int nY, rect32_t *pBox);
BOOL remove_creep_completely(int nType, int nX, int nY, creep_box_cb_t pfnSupport);

// 0x4C0060 count_neighboring_creep_tiles (484 B) -- fastcall ECX+EDX, `ret`.
int count_neighboring_creep_tiles(int nX, int nY);

// 0x4C04F0 get_max_creep_bounding_box (381 B) -- fastcall ECX+EDX + `ret Ch`.
// The type in ECX is switched on as 16 bits.
BOOL get_max_creep_bounding_box(short sArg0, int nArg1, int nArg2, void *pArg3,
                                int nArg4);

// 0x4C0250 sub_4C0250 (156 B): the callback is __stdcall (x, y, box); tile2.cpp
// passes FindCreepSupport. 0x4C0830 sub_4C0830 (136 B).
int sub_4C0250(int nX, int nY, creep_box_cb_t pfnCb);
BOOL sub_4C0830(int nArg0, int nArg1, int nArg2, int nArg3, int nArg4,
                int nArg5);

// 0x4C08C0 creep_load_edge_group (153 B). Opens the named MPQ file, which
// gamemap_init builds, and returns the allocated edge-group block, which
// creep_init stores in sg_pEdgeGroup_g.
BYTE *creep_load_edge_group(const char *pszFileName);

// 0x4C0690 creep_init (250 B) -- fastcall ECX+EDX + `ret Ch`. EDX points at the
// 16-byte hook block, which the body copies into sg_callback whole.
void creep_init(WORD *pTileData, const creep_hooks_t *pHooks, int nWidth,
                int nHeight, const char *pszEdgeGroupFile);

// 0x4BFF60 sub_4BFF60 (175 B) / 0x4C0010 sub_4C0010 (77 B) -- nullary, `ret`.
void sub_4BFF60();
void sub_4C0010();

#endif  // OC117_CREEP_H
