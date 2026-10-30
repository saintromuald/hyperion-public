// gds_vidinimo_PC.h -- declarations for src117/lang/gds_vidinimo_PC.cpp: the
// DirectDraw objects behind the Storm SDraw layer, created, freed and handed
// palette updates. The free functions are __fastcall through /Gr and are
// declared without the keyword.
//
// No DirectDraw type appears here, so that an includer does not get
// <windows.h> through <ddraw.h>.

#ifndef OC117_GDS_VIDINIMO_PC_H
#define OC117_GDS_VIDINIMO_PC_H

#include "platform.h"
#include "gds_palette.h"  // palentry_t

// init_directx -- 0x4CF960.
void init_directx();

// free_directx -- 0x4CFCE0.
void free_directx();

// flip_palette -- 0x4CFD60. ECX is the entries, EDX the first index, one stack
// word the count, in SDrawUpdatePalette's argument order.
void flip_palette(palentry_t *pEntries, int nFirst, int nCount);

#endif  // OC117_GDS_VIDINIMO_PC_H
