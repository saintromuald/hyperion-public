// sai_ContoursCreate.h -- declarations for src117/lang/sai_ContoursCreate.cpp,
// where the AI pathing builds its contour table. The free functions are
// __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_SAI_CONTOURSCREATE_H
#define OC117_SAI_CONTOURSCREATE_H

#include "platform.h"
#include "sai_Contours.h"

// Entries of the contour tracer's tables that contours_t::Init reads: a corner
// offset, and a one-cell step along a face.
struct contour_corner_t { short x, y; };
struct contour_hop_t { char dx, dy; };

// 0x48AD40. The qsort comparator over contour_t, ordering by pt.x then pt.y,
// both compared as signed 16-bit values.
int __cdecl contour_sort_cb(const void *p1, const void *p2);

// 0x48AD80. Builds the packed passability matrix from the VF4 map: one DWORD per
// four minitiles, four DWORDs per tile.
DWORD *sub_48AD80(void);

// 0x48AEB0. A free function although it takes the table in ECX: 1.17's name for
// it has no class prefix.
void sai_contours_free(contours_t *pContours);

// Hyperion: the times a game's contour lists are made, read from a save or freed, so the
// rows sub_488600 keeps of them are made again (sai_Contours.cpp, ContourRows).
extern unsigned g_nContourLists;

// 0x48B1C0.
char *sub_48B1C0();

// 0x48AF10. Allocates the glob's contour table, zeroes it, resets and sizes it,
// and tail-jumps contours_t::Init. ECX is the epGlob block, and the new table
// goes at its +0x97A1C.
void sai_contourscreate_init(void *pGlob);

// 0x48AF50. Scans the packed matrix for the next non-zero cell, wrapping at the
// end of the map. ECX is the matrix, EDX the row cursor and the stack word the
// column cursor, both read and written. Returns 1 when a cell was found and 0
// when the scan came back to where it started.
int sub_48AF50(BYTE *pMatrix, int *pnRow, int *pnCol);

#endif  // OC117_SAI_CONTOURSCREATE_H
