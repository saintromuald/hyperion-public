// CUnitBounds.h -- declarations for src117/lang/CUnitBounds.cpp: unit collision
// bounds tracking lists. Class members are in CUnit.h; the free functions are
// __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_CUNITBOUNDS_H
#define OC117_CUNITBOUNDS_H

#include "platform.h"

class CUnit;

// An entry of the two position-sort arrays, `g_pos_sort_x` @0x5C99B8 and
// `g_pos_sort_y` @0x5D03F8. The first dword is a one-based unit id, not a
// `CUnit *`: NewTrackingX forms `CUnit::Head() + id - 1` from it. collide.cpp's
// own TU-local copy of this struct names the slot `CUnit *pUnit` and never
// dereferences it.
struct pos_sort_ent_t
{
  /*0x00*/ DWORD dwUnitId;
  /*0x04*/ int   nCoord;
};
static_assert(sizeof(pos_sort_ent_t) == 0x08, "pos_sort_ent_t size (the [idx*8] stride)");
static_assert(offsetof(pos_sort_ent_t, dwUnitId) == 0x00, "pos_sort_ent_t::dwUnitId [base+idx*8]");
static_assert(offsetof(pos_sort_ent_t, nCoord)   == 0x04, "pos_sort_ent_t::nCoord   [base+idx*8+4]");

// 0x419500 (85 B). ECX, EDX and one stack word; all 15 callers read the full
// EAX.
int SearchFinder(pos_sort_ent_t *pArr, int nCoord, int nFlag);

// 0x419560 (52 B). Nullary; it ends in a tail jump to cunitfind_init, so void.
void cunit_bounds_init();

// 0x419D00 (141 B). ECX, EDX and one stack word; no caller reads EAX.
void DeleteTrackingX(pos_sort_ent_t *pArr, int nLo, int nHi);

// 0x419D90 (141 B). ECX, EDX and one stack word; no caller reads EAX.
void DeleteTrackingY(pos_sort_ent_t *pArr, int nLo, int nHi);

// 0x419E20 (140 B). ECX, EDX and one stack word; EAX is never set.
void NewTrackingX(pos_sort_ent_t *pArr, int nLo, int nHi);

// 0x419EB0 (140 B). ECX, EDX and one stack word; EAX is never set.
void NewTrackingY(pos_sort_ent_t *pArr, int nLo, int nHi);

// 0x41A090 (114 B). ECX, EDX and one stack word; no caller reads EAX.
void TrackXShift(pos_sort_ent_t *pArr, int nLo, int nHi);

// 0x41A110 (114 B). ECX, EDX and one stack word; no caller reads EAX.
void TrackYShift(pos_sort_ent_t *pArr, int nLo, int nHi);

// 0x421BE0 (149 B), in CUnitFind.cpp. Nullary. Declared here for
// cunit_bounds_init's tail jump; CUnitFind.h does not declare it.
void cunitfind_init();

// Hyperion: beside each entry of the two orderings, its unit's extent on the other axis
// and whether the entry is the unit's high edge. The walks over the orderings
// (get_units_in_rect, the pathing's contour_workspace_t::sub_488910) read a unit's other
// extent through its bound indices, a load from the unit and one from the other ordering for
// each entry walked, thousands a search in a large game. The sides move with their entries
// (NewTracking, DeleteTracking, TrackShift) and a unit's are written again where its
// coordinates change (BeginTracking, CoordSort, FinderSideUnit); a snapshot's restore makes
// them all again (FinderSideRebuild). HYPERION_FINDER_CHECK=1 compares each one read with
// the unit's own and writes any difference to hyperion_finder_check.log.
struct finder_side_t
{
  int nLow, nHigh;  // beside g_pos_sort_x the unit's top and bottom; beside _y its left and right
  int bHigh;        // the entry is the unit's right (x) or bottom (y) edge
  int nUnused;
};
extern finder_side_t g_finder_side_x[];
extern finder_side_t g_finder_side_y[];
extern bool g_bFinderSideCheck;
void FinderSideUnit(CUnit *pUnit);
void FinderSideRebuild();
void FinderSideVerify(int bY, int nIdx);
void FinderSideDiffer(const char *pszWhat, int nIdx);

// Hyperion: the finder's units in a grid by their top left corner, squares of 128
// pixels, kept where their sides are, so a box search need not walk the x ordering
// over the map's whole height. FinderGridRect gives the units get_units_in_rect's walk keeps
// for a box, each with the index of the entry the walk meets it at first, so that in that
// order they are the walk's answer: a unit is kept when one of its x edges is in the box's x
// range and its y extent meets the box's, and its edges are within the widest and tallest
// unit seen of its corner. Exact while the orderings are in order, which they are between
// CoordSort's calls; HYPERION_FINDER_CHECK=1 compares it with the walk.
struct finder_kept_t
{
  int nKey;  // the x ordering's index the walk meets the unit at first
  int nId;   // its one-based id
};
int FinderGridRect(int nLeft, int nTop, int nRight, int nBottom, finder_kept_t *pOut);
// The units with an x entry at an index from nX0 to before nX1 and a y entry from nY0 to before
// nY1, each with the first of its x entries in that span as its key: find_units_in_rect's
// answer, in its order once sorted by the key (CUnitFind.cpp). The spans must hold an entry each.
int FinderGridEntries(int nX0, int nX1, int nY0, int nY1, finder_kept_t *pOut);
// The entries of one kind of edge (bY: the y ordering's, else the x's; bHigh: bottom or right)
// at nFrom to nTo, both included, of the units whose other extent may reach from nOtherLo to
// nOtherHi (its high end at nOtherLo or past it, its low end at nOtherHi or before it), as
// indices in the ordering, in order. The contour walks' grid (sai_Contours.cpp).
int FinderGridEdges(int bY, int bHigh, int nFrom, int nTo, int nOtherLo, int nOtherHi, int *pOut);
// The largest magnitude of any unit's edge since the map began.
int FinderMostAbs();

inline const finder_side_t &FinderSideX(int nIdx)
{
  if (g_bFinderSideCheck)
    FinderSideVerify(0, nIdx);
  return g_finder_side_x[nIdx];
}

inline const finder_side_t &FinderSideY(int nIdx)
{
  if (g_bFinderSideCheck)
    FinderSideVerify(1, nIdx);
  return g_finder_side_y[nIdx];
}

#endif  // OC117_CUNITBOUNDS_H
