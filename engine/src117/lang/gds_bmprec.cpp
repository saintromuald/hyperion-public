// gds_bmprec.cpp -- the one-pixel rectangle outline, one body.

#include "../h/gds_bmprec.h"
#include "../gen/globals117.h"

// 0x4CE7D0. Draws the outline of a w by h rectangle at (nX, nY) in the current
// pen: top and bottom edges first, then left and right.
void bmp_rect(unsigned short nX, unsigned short nY, unsigned short nWidth,
              unsigned short nHeight)
{
  bmp_hline(nX, nY, nWidth);
  bmp_hline(nX, nY + nHeight - 1, nWidth);
  bmp_vline(nX, nY, nHeight);
  bmp_vline(nX + nWidth - 1, nY, nHeight);
}
