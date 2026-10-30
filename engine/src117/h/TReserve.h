// TReserve.h -- the AI town builder's placement scratch grid: a 64 x 64 map of
// bytes that `sai_TownBuilder.cpp` fills with candidate build cells and then
// queries. It is never a global -- every instance is a caller's buffer.
//
// The grid is row-major and 0x1000 bytes: `HasValidSpace` walks it as a nest of
// two 0x40 loops with the row pointer advanced by 0x40 a turn, and both callers
// that make one size it at 0x1000 (`ReservePlacement` zero-fills that many
// bytes, `sai_reserve_addon_space` reserves 0x100C on its stack for the grid
// plus a frame slot).

#ifndef OC117_TRESERVE_H
#define OC117_TRESERVE_H

#include "platform.h"
#include "pos.h"       // Hyperion: RemoveDisconnected takes a map point by value

// A 64x64 grid of cells. Only `HasValidSpace`'s test (`cmp byte ptr [...], 0h`)
// says what a cell value means: 0 is free. The non-zero values stay unnamed
// until a body writes one.
#define RESERVE_DIM 0x40

struct TReserve {
    /*0x0000*/ BYTE aGrid[RESERVE_DIM][RESERVE_DIM];

    // 0x496210 TReserve::HasValidSpace (34 B). `this` in ECX, no stack
    // parameters; every return path materialises 0 or 1.
    BOOL HasValidSpace();

    // 0x497630 TReserve::RemoveDisconnected (149 B). `this` in ECX. The one
    // stack word is an X:Y pair, not a pointer: the body splits it
    // (`shr edx,10h` / `movsx dx` / `movsx ax`) for GetRegion_xy, and each use
    // reloads its half from the slot, so the point passes by value.
    void RemoveDisconnected(mpoint_t pos);

    // 0x498430 TReserve::RemoveWrongHeight (229 B). `this` in ECX and a point by
    // value, as RemoveDisconnected takes one. Void because EAX at the ret is
    // inherited and no caller consumes it; a consuming caller elsewhere would
    // make it an int, and no byte test can tell the two apart.
    void RemoveWrongHeight(mpoint_t pos);
};

static_assert(sizeof(TReserve) == 0x1000, "TReserve");
static_assert(offsetof(TReserve, aGrid) == 0x0000, "TReserve.aGrid");

#endif  // OC117_TRESERVE_H
