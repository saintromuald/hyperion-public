// gds_bmpcopre.cpp -- the clipped rectangle copy onto the current bitmap.

#include <stddef.h>
#include "../h/gds_bmpcopre.h"
#include "../gen/globals117.h"
#include "../port/render/port_storm.h"  // Hyperion: PortBltRop3

// Storm's SBltROP3, __stdcall, spelled exactly as gds_vidblit.cpp spells it.
extern "C" {
BOOL __stdcall SBltROP3(void *pDest, const void *pSrc, int nWidth, int nHeight,
                        int nDestPitch, int nSrcPitch, DWORD dwArg6,
                        DWORD dwRop);  // 0x4D0316 -> [0x4EB478] -> ord 313
}

// 0x4CE720. Copies the pSrc rectangle of pBmp to (nX, nY) on the current
// bitmap, clipped against both, with SRCCOPY.
void bmp_copy_rect(unsigned short nX, unsigned short nY, bmp_t *pBmp,
                   bmpsrc_t *pSrc)
{
  bmp_t *pCur = static_cast<bmp_t *>(_bmp_current);
  bmpsrc_t src = *pSrc;
  bmpsrc_t dst;
  int nErr;

  src.nWidth = pBmp->nWidth;
  src.nHeight = pBmp->nHeight;
  dst.rc.left = nX;
  dst.rc.top = nY;
  dst.nWidth = pCur->nWidth;
  dst.nHeight = pCur->nHeight;
  nErr = img_clipped(&src, &dst);
  if (nErr == 0)
    // Hyperion: the port's copy (port_storm.cpp), in place of Storm's SBltROP3.
    PortBltRop3(pCur->pPixels + dst.rc.top * pCur->nWidth + dst.rc.left,
                pBmp->pPixels + src.rc.top * pBmp->nWidth + src.rc.left,
                src.nWidth, src.nHeight, pCur->nWidth, pBmp->nWidth, nErr,
                0xCC0020);
}
