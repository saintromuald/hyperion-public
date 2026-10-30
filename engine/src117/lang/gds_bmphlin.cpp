// gds_bmphlin.cpp -- the horizontal-line primitive on the current bitmap, the
// one body of 1.17's TU.

#include "../h/gds_bmphlin.h"
#include <string.h>            // memset

#include "../gen/globals117.h"
#include "../port/render/port_scene.h"  // Hyperion: PortSceneSpan

// 0x4CCB30 (58 B). Fills nLen bytes of the current bitmap's row nY starting at
// column nX with the current pen. 1.17 reads both coordinates signed, where
// bmp.h declares them unsigned, hence the casts.
void __fastcall bmp_hline(unsigned short nX, unsigned short nY, unsigned short nLen)
{
  bmp_t *pBmp;

  pBmp = static_cast<bmp_t *>(_bmp_current);
  if (PortSceneSpan(pBmp->pPixels + pBmp->nWidth * (short)nY + (short)nX, (unsigned short)nLen, 1, _bmp_pen))
    return;
  memset(pBmp->pPixels + pBmp->nWidth * (short)nY + (short)nX, _bmp_pen,
         (unsigned short)nLen);
}
