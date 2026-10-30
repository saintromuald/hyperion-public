// gds_bmpcopre.h -- declarations for src117/lang/gds_bmpcopre.cpp, the clipping
// test the rectangle blits share. bmp_copy_rect is declared in bmp.h, with the
// bmpsrc_t block.

#ifndef OC117_GDS_BMPCOPRE_H
#define OC117_GDS_BMPCOPRE_H

#include "platform.h"

#include "bmp.h"

// 0x4CFE60. ECX is the source block and EDX the destination block, both
// clipped in place; returns 1 when nothing is left to draw and 0 otherwise.
int img_clipped(bmpsrc_t *pSrc, bmpsrc_t *pDst);

#endif  // OC117_GDS_BMPCOPRE_H
