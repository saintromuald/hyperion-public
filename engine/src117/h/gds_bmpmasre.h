// gds_bmpmasre.h -- declarations for src117/lang/gds_bmpmasre.cpp, the clipped
// transparent-colour blit onto the current bitmap.

#ifndef OC117_GDS_BMPMASRE_H
#define OC117_GDS_BMPMASRE_H

#include "platform.h"

#include "bmp.h"

// 0x4CF5E0. The same frame as bmp_copy_rect: CX and DX are the 16-bit
// destination position, the stack pair the source bitmap and its 12-byte
// source block.
void bmp_mask_rect(unsigned short nX, unsigned short nY, bmp_t *pBmp,
                   bmpsrc_t *pSrc);

// 0x4CFFD7. An assembly body that saves every register and reads only the six
// glBlt*/gpBlt* globals bmp_mask_rect fills.
void bmp_mask_rect_asm(void);

#endif  // OC117_GDS_BMPMASRE_H
