// CUnitFind.h -- the unit-search iteration API (oracle CUnitFind.cpp): the point,
// rect and region searches that return a NULL-terminated candidate list, and the
// call that closes an iteration. Only the entry points the tree calls are
// declared, not the whole TU.

#ifndef OC117_CUNITFIND_H
#define OC117_CUNITFIND_H

#include "platform.h"
#include "rect16.h"   // the rectangle these searches take by pointer
#include "pos.h"      // Hyperion: mrect_t, a box of map positions

class CUnit;

// get_units_at_cursor_point -- oracle 0x422150. Returns the NULL-terminated list
// of units whose sprite is under (nX, nY). The two arguments arrive in registers
// although no call site loads them: get_map_target already holds them in ECX and
// EDX. The body builds a +/-0x20 rect around both, calls the underlying search
// (0x422330) and filters its result with CSprite::IsUnderPoint.
CUnit **__fastcall get_units_at_cursor_point(int nX, int nY);

// finish_unit_iteration -- oracle 0x4222B0. Releases the iteration the call above
// began: it pops a search-context stack and restores the previous top. The body
// never reads its parameter, which is 1.17's own behaviour, reproduced rather than
// repaired; it stays declared unary because every call site loads the list into
// ECX before the call.
void __fastcall finish_unit_iteration(CUnit **ppList);

// get_units_at_point -- oracle 0x4221F0, 182 B. The NULL-terminated candidate
// list of units overlapping one pixel. A different function from
// get_units_at_cursor_point (0x422150) above; both are closed by
// finish_unit_iteration. The parameters are 16 bits wide: every consumer of
// ECX/EDX in the body is a 16-bit store, and collide.cpp's sub_4132B0 hands its
// own `short` coordinates over with no sign extension.
CUnit **__fastcall get_units_at_point(spos_t sX, spos_t sY);  // Hyperion: positions

// get_units_in_region -- oracle 0x4222D0, 83 B. Fills a rect16_t from
// GetRegionBounds, calls get_units_in_rect, then filters that result by region.
// The region id is a WORD: the body compares it 16 bits wide (`cmp ax, bx`), and
// tai_captain_cannot_fight_region loads it with `movzx ecx` straight into the
// argument register.
CUnit **__fastcall get_units_in_region(WORD nRegion);


// find_units_in_rect -- oracle 0x4224C0, 662 B. Reached by find.cpp's
// find_foreach_unit. ECX is read as 32 bits, EDX never.
CUnit **__fastcall find_units_in_rect(mrect_t *pRect);
#if defined(_WIN64) || defined(__LP64__)
// Hyperion: 1.17's box, for the callers that still take it.
inline CUnit **find_units_in_rect(rect16_t *pRect) {
  mrect_t rc = *pRect;
  return find_units_in_rect(&rc);
}
#endif

// get_units_in_rect_filtered -- oracle 0x422460. find_units_in_rect with a
// per-unit predicate: the filter is entered with the unit in ECX and the context
// in EDX. SAI_Build's town_from_area passes unit_belongs_to_player and a player
// index.
typedef BOOL(__fastcall *PFN_UNIT_FILTER)(CUnit *pUnit, int nContext);
CUnit **__fastcall get_units_in_rect_filtered(rect16_t *pRect,
                                              PFN_UNIT_FILTER pFilter,
                                              int nContext);

#endif // OC117_CUNITFIND_H
