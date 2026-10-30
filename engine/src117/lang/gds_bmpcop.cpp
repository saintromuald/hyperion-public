// gds_bmpcop.cpp -- the whole-bitmap copy, one body.

#include <stddef.h>
#include "../h/gds_bmpcop.h"
#include "../gen/globals117.h"

// 0x4CF4F0. Copies all of pBmp to (nX, nY) on the current bitmap through
// bmp_copy_rect. The source block is written width first, then right from it.
void bmp_copy(unsigned short nX, unsigned short nY, bmp_t *pBmp)
{
  bmpsrc_t src;

  src.rc.left = 0;
  src.rc.top = 0;
  src.nWidth = pBmp->nWidth;
  src.rc.right = src.nWidth - 1;
  src.nHeight = pBmp->nHeight;
  src.rc.bottom = src.nHeight - 1;
  bmp_copy_rect(nX, nY, pBmp, &src);
}
