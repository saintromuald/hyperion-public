// placebox.h -- declarations for src117/lang/placebox.cpp: the building
// placement box the player drags around before committing a build order, its
// per-tile shading matrix, addon and geyser highlighting, and the map's
// starting-unit placement.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default. Some of this TU's names are declared in
// their callers' headers and not restated here: is_placing_addon
// (CUnitBuild.h), placebox_is_on and sub_47D530 (CUnitTerran.h), sub_47D290
// (statbtn.h), placebox_query_place and placebox_validate (net_cmds.h) and
// FindPlaceForUnit (CUnitZSpells.h).

#ifndef OC117_PLACEBOX_H
#define OC117_PLACEBOX_H

#include "platform.h"
#include "rect16.h"        // ours: RectIsOnMap dereferences its ECX as one
#include "pos.h"           // Hyperion: mpoint_t, mrect_t
#include "point16.h"       // ours: the placement command path passes one by value
#include "typelimits.h"    // Hyperion: order_id_t

class CUnit;

// The box itself: lifecycle and state.

// 0x47D000 (100 B), void: no caller reads the zero it leaves in EAX.
void placebox_init();

// 0x47CE20 (49 B). Nullary; `ret`, no register input.
void placebox_free();

// 0x47CEC0 (258 B), two register arguments. The unit type is a WORD, which
// keeps placebox_on's register moves as 1.17 has them; this body's bytes are
// the same either way.
void placebox_create(int nArg0, WORD wUnitType);

// 0x47D580 (110 B). Nullary. Tears the box down; reaches
// CImage::ClearPlaceboxes.
void placebox_off();

// 0x47D7E0 (243 B). Nullary; void, since no caller reads EAX.
void placebox_update();

// 0x47D920 (298 B). Two register arguments; void.
void placebox_update_mtx(int nArg0, intptr_t nArg1);

// 0x47D490 (158 B). The argument is a pointer to something the bytes do not
// name; the int return is read whole.
int placebox_pix_fix(void *pArg0);

// Drawing.

// 0x47CD80 (147 B). The argument is 16 bits wide. Reaches rle_draw_normal.
void placebox_draw_bldg(WORD wArg0);

// 0x47D200 (133 B), a callback whose address is taken twice. 1.17's is
// __stdcall with the bitmap and the bmpsrc_t; the engine spells it as img.h's
// PFN_IMG_UPDATE, the four parameters img_update_real passes
//.
struct bmp_t;
struct bmpsrc_t;
void __fastcall placebox_mask_rect(unsigned short nX, unsigned short nY, bmp_t *pBmp, bmpsrc_t *pSrc);

// 0x405C12 shade_mtx (88 B), declared here for placebox_update_mtx, its one
// caller, though it belongs to 1.17's hand-written assembly block, not to
// placebox.cpp. ECX, EDX, then two stack words.
void shade_mtx(int nArg0, int nArg1, void *pArg2, int nArg3);

// Highlighting: addons and geysers.

// 0x47D080 (266 B) and 0x47D190 (98 B), both nullary. 1.17 inlines
// placebox_show_geyser into both, so it is written out there.
void placebox_highlight_addons();
void placebox_highlight_geysers();

// 0x47D8E0 (59 B), the unit, which it calls CUnit::DisplayLandingOverlay on.
void placebox_show_geyser(CUnit *pUnit);

// The placement command path.

// 0x47CC80 (239 B). Two register arguments plus two stack words: an 8-bit
// order, the unit SetOrder_xy is called on, and a point passed by value.
void sub_47CC80(order_id_t bOrderID, CUnit *pUnit, mpoint_t pt, int nArg3);  // Hyperion: a map point

// 0x47D620 (263 B). The first argument is 8 bits wide, stored and reloaded
// with no extension; `char` is kept because it is byte-neutral here. The point
// is passed by value and forwarded to sub_47CC80.
void placebox_query_place_addon(order_id_t cArg0, int nArg1, point16_t ptIn);

// 0x47D730 (171 B). The first argument is 8 bits wide, with no extension; the
// point is passed by value and forwarded to sub_47CC80. Reaches
// map_msg_target_self.
void placebox_query_place_bldg(order_id_t bArg0, int nArg1, point16_t ptIn);

// 0x47CE60 (90 B), a callback whose address is taken once; the argument is a
// pointer.
void placebox_gamemap_lclick(void *pArg0);

// 0x47DB60 (25 B). Nullary; its callers read the whole int.
int sub_47DB60();

// 0x47DB80 (168 B). Nullary; its caller reads the whole int.
int sub_47DB80();

// Declarations moved here from the .cpp files that called them.

// 0x47D300 (158 B), from CUnitGUI.cpp: ECX, EDX and one stack word, which the
// body reads as a byte.
void set_placebox(intptr_t a1, BYTE *pbOut, BYTE a3);

// 0x47D3A0 (238 B), from order.cpp: puts the cursor into building-placement
// mode.
void placebox_on(BYTE bCursor, WORD wUnitType);

// 0x47E100 (399 B) and 0x47EB00 (280 B), from maphdr.cpp, both reached only
// from PlaceGameStart.
void PlaceStartingUnits(void);
void TeamPlaceStartingUnits(void);

// Starting-unit placement.

// 0x47DF00 (287 B). Two register arguments, both 8 bits wide and neither
// sign-extended; `char` is kept because it is byte-neutral here.
void PlaceCenter(char cArg0, BYTE bArg1);
// Hyperion: PlaceCenter's placing, for a building of any type
void PlaceStartBuilding(int nType, BYTE bArg1);

// 0x47E020 (94 B). One register argument, the hatchery: `mov esi, ecx` at
// entry and `mov ecx, esi` before `call CUnit::sub_43D360`; +0x4C (bPlayer)
// and +0x134 are read off it.
void PlaceLarva(CUnit *pUnit);

// 0x47E080 (122 B). ECX read as 8 bits, no sign extension. Reaches CreateUnitAt.
void PlaceOverlord(BYTE bArg0);

// 0x47E290 (97 B). Nullary; void, since no caller reads EAX.
void PreparePlayerSlots();

// 0x47DEF0 (12 B), a find_foreach_unit callback: self-destructs the unit and
// returns 0. The context is never read.
BOOL KillUnitProc(CUnit *pUnit, void *pCtx);

// Collision and map geometry.

// 0x47E300 (60 B). Whether the box is on the map: its four signed 16-bit
// fields against gwMapPixWidth and gwMapPixHeight - 0x20.
BOOL RectIsOnMap(const mrect_t *prc);  // Hyperion: a box of map positions

// 0x47EA20 (211 B), a callback whose address is taken five times. The context
// is a unit too, spelled as FindUnitCb spells it, and every path returns a
// value.
int SharesCollisionFn(CUnit *pUnitA, void *pCtx);  // FindUnitCb's spelling

// 0x47E340 (1753 B). Two register arguments plus six stack words. x and w are
// 16-bit, y and h ints fed WORD values.
int ResolveUnitCollision(CUnit *pUnit, mpoint_t *pOutput, spos_t x, int y,
                         short w, int h, mrect_t *pBounds, int a5);

// External callees this TU reaches that no other header declares.

// 0x486200 (311 B), declared here for PlaceLarva, which passes the mother in
// ECX and the new larva in EDX, until an SAI_Build.h of ours exists.
void __fastcall town_move_morphed_building(CUnit *pMother, CUnit *pLarva);   // 0x486200  311 B

#endif  // OC117_PLACEBOX_H
