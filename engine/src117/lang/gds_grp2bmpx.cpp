// gds_grp2bmpx.cpp -- a bitmap view of one GRP frame plus its placement.

#include <stddef.h>
#include "../h/gds_grp2bmpx.h"
#include "../gen/globals117.h"

// 0x4CF570. grp_to_bmp, after also reporting the frame's offset in the group and
// the group's dimensions. GROUP+0 is the WORD frame count and FRAME+1 the Y
// offset, which grp.h does not name.
void grp_to_bmpx(GROUP *pGroup, unsigned char bFrame, bmp_t *pBmp,
                 short *pnX, short *pnY, short *pnWidth, short *pnHeight)
{
  const FRAME *pFrame;
  DWORD_PTR dwOffset;
  int nWidth;

  if (bFrame >= *reinterpret_cast<const WORD *>(pGroup))
    bFrame = 0;
  pFrame = &pGroup->frames[bFrame];
  *pnX = pFrame->bOffsetX;
  *pnY = pFrame->_unmapped_01;
  *pnWidth = pGroup->wMaxWidth;
  *pnHeight = pGroup->wMaxHeight;
  pBmp->nHeight = pFrame->bHeight;
  dwOffset = GrpFrameWord(pFrame);
  nWidth = pFrame->bWidth;
  if (static_cast<int>(dwOffset) < 0) {
    dwOffset &= 0x7FFFFFFF;
    nWidth += 0x100;
  }
  pBmp->nWidth = static_cast<short>(nWidth);
#if defined(_WIN64) || defined(__LP64__)
  // Hyperion: the loader made pData the pointer, the file's dword kept beside
  // it (PortGroupNative).
  pBmp->pPixels = pFrame->pData;
#else
  pBmp->pPixels = reinterpret_cast<BYTE *>(pGroup) + dwOffset;
#endif
}
