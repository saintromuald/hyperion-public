// gds_bmpcle.cpp -- the bitmap fill, one body.

#include <stddef.h>
#include <string.h>
#include "../h/gds_bmpcle.h"
#include "../h/bmp.h"
#include "../gen/globals117.h"

// bmp_clear -- 0x4CE6F0. Fills the whole current bitmap with one colour. The
// length is width times height as signed shorts; it is declared in bmp.h.
void __fastcall bmp_clear(unsigned char bColour)
{
  bmp_t *pBmp = static_cast<bmp_t *>(_bmp_current);
  memset(pBmp->pPixels, bColour, pBmp->nWidth * pBmp->nHeight);
}
