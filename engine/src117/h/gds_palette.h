// gds_palette.h -- declarations for src117/lang/gds_palette.cpp: the palette's
// colour cycling, its fades and the pen table.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_GDS_PALETTE_H
#define OC117_GDS_PALETTE_H

#include "platform.h"

// The cycle records are at 0x6D2F88 with a stride of 0x10, the pen table is at
// 0x6D2368 and the monitor palette at 0x6D2388; gds_palette.cpp spells them.

// 0x4CCBC0 (146 B). ECX is the first cycle record to walk, EDX its index, the
// one stack word the highest low index to tick. Returns the index it stopped on.
int cycle_palette(void *pArg0, int nArg1, int nArg2);

// 0x4CCC60 (76 B). ECX is a cycle slot's index, not a pointer.
void extra_cycle(int nIndex);

// 0x4CCCB0 (57 B). EDX is a function pointer, called with ECX forwarded and each
// of the 26 colours of the table at 0x50F0F8; the AL it returns goes to the pen
// table. palentry_t is defined further down.
struct palentry_t;

// The finder takes a palentry_t by value, which __fastcall pushes: ECX, one
// stack dword and `ret 4h`. A DWORD there would ride EDX instead.
typedef int(__fastcall *PFN_FIND_PEN)(void *pPalette, palentry_t entry);
void init_pens(void *pPalette, PFN_FIND_PEN pfnFind);

// 0x4CCCF0 (63 B).
void normal_cycle(int nIndex);

// 0x4CCD30 (46 B).
void do_all_cycles();

// 0x4CCF20 (18 B).
void vid_palette_to_monitor();

// One palette entry: red, green and blue, which vid_desaturate weights 77, 151
// and 28, and a fourth byte the game carries but never computes.
struct palentry_t {
  /*0x00*/ BYTE bRed;
  /*0x01*/ BYTE bGreen;
  /*0x02*/ BYTE bBlue;
  /*0x03*/ BYTE bFlags;
};
static_assert(sizeof(palentry_t) == 4,
              "palentry_t size    -- vid_fade `add ecx,4h` stride; 256 * 4 == 400h, the memset size");
static_assert(offsetof(palentry_t, bRed) == 0x00,
              "palentry_t.bRed    -- vid_fade [ecx+6D2788h]");
static_assert(offsetof(palentry_t, bGreen) == 0x01,
              "palentry_t.bGreen  -- vid_fade [ecx+6D2789h]");
static_assert(offsetof(palentry_t, bBlue) == 0x02,
              "palentry_t.bBlue   -- vid_fade [ecx+6D278Ah]");
static_assert(offsetof(palentry_t, bFlags) == 0x03,
              "palentry_t.bFlags  -- vid_desaturate `mov al, byte ptr [ecx-3h]`");

// vid_fade -- 0x4CCD60. ECX is the step count, clamped to 10. palette_to_monitor
// is in gds_vidinimo.h with its body.
void vid_fade(int nSpeed);

// The three maphdr.cpp calls to set up a tileset's colour cycles.

// 0x4CCF00 (21 B)
void vid_init_cycles(void);

// 0x4CD000 (53 B). The fifth parameter is the colour table extra_cycle reads,
// pointer-wide, though maphdr.cpp's record calls it dwRate.
void vid_set_cycle(BYTE bSlot, BYTE bArg1, BYTE bLo, BYTE bHi,
                   DWORD_PTR dwRate, BYTE bArg3);

// 0x4CD070 (14 B)
void vid_start_cycle(BYTE bSlot);

#endif  // OC117_GDS_PALETTE_H
