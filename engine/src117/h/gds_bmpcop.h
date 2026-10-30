// gds_bmpcop.h -- declarations for src117/lang/gds_bmpcop.cpp, the whole-bitmap
// copy onto the current bitmap.

#ifndef OC117_GDS_BMPCOP_H
#define OC117_GDS_BMPCOP_H

#include "platform.h"

#include "bmp.h"

// 0x4CF4F0. CX and DX pass straight through to bmp_copy_rect as its 16-bit
// position; the stack word is the source bitmap.
void bmp_copy(unsigned short nX, unsigned short nY, bmp_t *pBmp);

#endif  // OC117_GDS_BMPCOP_H
