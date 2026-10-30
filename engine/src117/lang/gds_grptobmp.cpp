// gds_grptobmp.cpp -- a bitmap view of one GRP frame.

#include <stddef.h>
#include "../h/gds_grptobmp.h"
#include "../gen/globals117.h"

// 0x4CFDA0. Points pBmp at frame bFrame of the group, frame 0 when out of range.
// The frame's +4 dword is an offset from the group; its top bit adds 256 to the
// width. GROUP+0 is the WORD frame count, which grp.h does not name.
void grp_to_bmp(GROUP *pGroup, unsigned char bFrame, bmp_t *pBmp)
{
  const FRAME *pFrame;
  DWORD_PTR dwOffset;
  int nWidth;

  if (bFrame >= *reinterpret_cast<const WORD *>(pGroup))
    bFrame = 0;
  pFrame = &pGroup->frames[bFrame];
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
