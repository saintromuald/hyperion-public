// gds_bmpmas.h -- declarations for src117/lang/gds_bmpmas.cpp, the whole-bitmap
// masked copy onto the current bitmap. Free functions are __fastcall through /Gr
// and are declared without the keyword.

#ifndef OC117_GDS_BMPMAS_H
#define OC117_GDS_BMPMAS_H

#include "platform.h"

#include "bmp.h"

// 0x4CF530 bmp_mask (63 B), `ret 4h`. The same shape as bmp_copy: ECX/EDX pass
// straight through to bmp_mask_rect and the stack word is the source bitmap.
void bmp_mask(unsigned short nX, unsigned short nY, bmp_t *pBmp);

#endif  // OC117_GDS_BMPMAS_H
