// gds_bmpvlin.cpp -- the vertical-line primitive on the current bitmap, the
// one body of 1.17's TU.

#include "../h/gds_bmpvlin.h"

#include "../gen/globals117.h"
#include "../port/render/port_scene.h"  // Hyperion: PortSceneSpan

// 0x4CCB70 (69 B). Walks nLen rows down the current bitmap from (nX, nY),
// storing the current pen. The pen and the pitch are re-read every iteration,
// since the store may alias either. 1.17 reads nX signed and nY unsigned, hence
// the one cast.
void __fastcall bmp_vline(unsigned short nX, unsigned short nY, unsigned short nLen)
{
  bmp_t *pBmp;
  unsigned char *pDst;
  unsigned short i;

  pBmp = static_cast<bmp_t *>(_bmp_current);
  pDst = pBmp->pPixels + pBmp->nWidth * nY + (short)nX;
  if (PortSceneSpan(pDst, nLen, static_cast<unsigned short>(pBmp->nWidth), _bmp_pen))
    return;
  for (i = 0; i < nLen; ++i) {
    *pDst = _bmp_pen;
    pDst += pBmp->nWidth;
  }
}
