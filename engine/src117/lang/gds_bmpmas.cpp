// gds_bmpmas.cpp -- the whole-bitmap masked copy.

#include <stddef.h>
#include "../h/gds_bmpmas.h"
#include "../h/gds_bmpmasre.h"
#include "../gen/globals117.h"

// 0x4CF530. Copies all of pBmp to (nX, nY) on the current bitmap through
// bmp_mask_rect, skipping colour 0. Same body as bmp_copy.
void bmp_mask(unsigned short nX, unsigned short nY, bmp_t *pBmp)
{
  bmpsrc_t src;

  src.rc.left = 0;
  src.rc.top = 0;
  src.nWidth = pBmp->nWidth;
  src.rc.right = src.nWidth - 1;
  src.nHeight = pBmp->nHeight;
  src.rc.bottom = src.nHeight - 1;
  bmp_mask_rect(nX, nY, pBmp, &src);
}
