// port_stormbmp.cpp -- Storm's image loading, the port's own. The five SBmp
// functions 1.17 imports keep Storm's names and shapes,
// as port_stormbase.cpp's do. Storm reads BMP and PCX and one more format; 1.17
// only ever gives it PCX (every image it names, and SBmpDecodeImage's type 2),
// so PCX is what this reads, the way storm.dll's decoder does, and another
// format is refused. tools/porttest/storm_test.cpp compares the two.
//
// A file is read through Storm's SFile functions while the File family is
// still Storm's: whole, and then decoded as SBmpDecodeImage decodes memory,
// which gives what storm.dll's reading in parts gives.
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "port_stormapi.h"

static const DWORD SERR_INVALID = 0x57;
static const DWORD PCX_HEADER = 0x80;
static const DWORD PCX_PALETTE = 0x300;  // 256 RGB triples at the file's end, after a 0x0C byte

enum { IMAGE_NONE, IMAGE_BMP, IMAGE_PCX, IMAGE_OTHER };

// The format by the first two bytes, as Storm tells them apart.
static int ImageKind(const BYTE *p, DWORD cb)
{
  if (cb < 2)
    return IMAGE_NONE;
  const WORD w = static_cast<WORD>(p[0] | p[1] << 8);
  return w == 0x050A ? IMAGE_PCX : w == 0x4D42 ? IMAGE_BMP : IMAGE_OTHER;
}

// PCX's run-length data into cbDst bytes: nPlanes planes of nBytesPerLine bytes
// a line, of which the first nWidth are pixels. A pixel's planes interleave in
// the output, the file's first plane last. Everything stops at the end of the
// output; storm.dll's fill goes on past it, up to a row's length, when a run
// crosses the end, which the game's own images never do.
static void PcxUnpack(BYTE *pDst, DWORD cbDst, const BYTE *pSrc, DWORD cbSrc, DWORD nWidth, DWORD nBytesPerLine,
                      DWORD nPlanes)
{
  const BYTE *pSrcEnd = pSrc + cbSrc;
  BYTE *const pDstEnd = pDst + cbDst;
  const int nRowStride = static_cast<int>(nWidth * nPlanes);
  const int nLastPlane = static_cast<int>(nPlanes) - 1;
  BYTE *pRow = pDst;
  DWORD x = 0;
  int nPlane = nLastPlane;

  // The next line of the plane below, or of the next row after the last plane.
  auto nextLine = [&](int *pnRemain) {
    x -= nBytesPerLine;
    if (--nPlane < 0) {
      pRow += nRowStride;
      if (pnRemain)
        *pnRemain -= nRowStride;
      nPlane = nLastPlane;
    }
  };

  if (pRow >= pDstEnd)
    return;
  do {
    if (pSrc >= pSrcEnd)
      return;
    const BYTE b = *pSrc++;
    if (b < 0xC0) {
      if (x < nWidth) {
        if (nPlanes == 1) {
          pRow[x] = b;
        } else {
          BYTE *p = pRow + x * nPlanes + nPlane;
          if (p < pDstEnd)
            *p = b;
        }
      }
      if (++x >= nBytesPerLine)
        nextLine(NULL);
    } else {
      DWORD nCount = b & 0x3F;
      const DWORD nLeft = static_cast<DWORD>(pDstEnd - pRow);
      if (nCount >= nLeft)
        nCount = nLeft;
      const BYTE v = *pSrc++;
      int nRemain = static_cast<int>(pDstEnd - pRow);
      while (nCount) {
        if (x >= nWidth) {
          // the line's padding past the image
          DWORD n = nBytesPerLine - x;
          if (nCount < n)
            n = nCount;
          x += n;
          if (x >= nBytesPerLine)
            nextLine(&nRemain);
          nCount -= n;
          if (!nCount)
            break;
        }
        DWORD n = nWidth - x;
        if (nCount < n)
          n = nCount;
        if (nPlanes == 1) {
          BYTE *p = pRow + x;
          DWORD nFill = n;
          if (p >= pDstEnd)
            nFill = 0;
          else if (nFill > static_cast<DWORD>(pDstEnd - p))
            nFill = static_cast<DWORD>(pDstEnd - p);
          memset(p, v, nFill);
        } else {
          int i = static_cast<int>(x * nPlanes) + nPlane;
          int nEnd = static_cast<int>(n * nPlanes);
          if (nRemain < nEnd)
            nEnd = nRemain;
          nEnd += i;
          for (; i < nEnd && pRow + i < pDstEnd; i += nPlanes)
            pRow[i] = v;
        }
        x += n;
        if (x >= nBytesPerLine)
          nextLine(&nRemain);
        nCount -= n;
      }
    }
  } while (pRow + x < pDstEnd);
}

// A PCX, as storm.dll's decoders read one: the size from the header, the
// pixels, and the palette, for an 8-bit image, as Windows PALETTEENTRYs. From
// memory the pixels fill at most the image's size of cbBuf; from a file,
// storm.dll unpacks into all of cbBuf, which differs only for data that runs on
// past the image.
static BOOL PcxDecode(const BYTE *pSrc, DWORD cbSrc, void *pPalette, void *pBuf, DWORD cbBuf, DWORD *pdwWidth,
                      DWORD *pdwHeight, DWORD *pdwBpp, bool bFromFile = false)
{
  // the header, and for 8 bits the palette after the pixels: the lengths below take both off
  if (cbSrc < PCX_HEADER || (pSrc[3] * pSrc[0x41] == 8 && cbSrc < PCX_HEADER + PCX_PALETTE + 1))
    return FALSE;
  const DWORD nWidth = static_cast<DWORD>((pSrc[8] | pSrc[9] << 8) - (pSrc[4] | pSrc[5] << 8) + 1);
  const DWORD nHeight = static_cast<DWORD>((pSrc[10] | pSrc[11] << 8) - (pSrc[6] | pSrc[7] << 8) + 1);
  const DWORD nPlanes = pSrc[0x41];
  const DWORD nBpp = pSrc[3] * nPlanes;
  if (pdwWidth)
    *pdwWidth = nWidth;
  if (pdwHeight)
    *pdwHeight = nHeight;
  if (pdwBpp)
    *pdwBpp = nBpp;
  if (pBuf) {
    DWORD cb = ((nBpp + 7) >> 3) * nHeight * nWidth;
    if (cbBuf < cb || bFromFile)
      cb = cbBuf;
    const DWORD cbData = cbSrc - (nBpp == 8 ? PCX_PALETTE + 1 : 0) - PCX_HEADER;
    PcxUnpack(static_cast<BYTE *>(pBuf), cb, pSrc + PCX_HEADER, cbData, nWidth,
              static_cast<DWORD>(pSrc[0x42] | pSrc[0x43] << 8), nPlanes);
  }
  if (pPalette && nBpp == 8) {
    const BYTE *pRgb = pSrc + cbSrc - PCX_PALETTE;
    BYTE *pOut = static_cast<BYTE *>(pPalette);
    for (int i = 0; i < 256; ++i, pRgb += 3, pOut += 4) {
      pOut[0] = pRgb[0];
      pOut[1] = pRgb[1];
      pOut[2] = pRgb[2];
      pOut[3] = 0;
    }
  }
  return TRUE;
}

// A whole file through Storm's SFile, into a block from malloc the caller
// frees; NULL when it cannot be opened or read.
static BYTE *LoadFile(const char *pszFileName, DWORD *pcbRead)
{
  HANDLE hFile;
  *pcbRead = 0;
  if (!SFileOpenFile(pszFileName, &hFile))
    return NULL;
  const DWORD cb = SFileGetFileSize(hFile, NULL);
  BYTE *pData = static_cast<BYTE *>(malloc(cb ? cb : 1));
  if (pData && cb && !SFileReadFile(hFile, pData, cb, pcbRead, NULL)) {
    free(pData);
    pData = NULL;
  }
  SFileCloseFile(hFile);
  return pData;
}

typedef void *(__stdcall *PFN_BMPALLOC)(DWORD);

extern "C" {

// Ordinal 321. Type 0 tells the format from the data; 2 is PCX.
BOOL __stdcall SBmpDecodeImage(DWORD dwType, const void *pSrc, DWORD cbSrc, void *pPalette, void *pBuf,
                               DWORD cbBuf, DWORD *pdwWidth, DWORD *pdwHeight, DWORD *pdwBpp)
{
  if (pdwWidth)
    *pdwWidth = 0;
  if (pdwHeight)
    *pdwHeight = 0;
  if (pdwBpp)
    *pdwBpp = 0;
  if (!pSrc || !cbSrc || (pBuf && !cbBuf)) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  const BYTE *p = static_cast<const BYTE *>(pSrc);
  if (dwType == IMAGE_NONE)
    dwType = ImageKind(p, cbSrc);
  if (dwType != IMAGE_PCX)
    return FALSE;
  return PcxDecode(p, cbSrc, pPalette, pBuf, cbBuf, pdwWidth, pdwHeight, pdwBpp);
}

// Ordinal 323. The same, from a file. A PCX shorter than its header is refused,
// as Storm's reading of the header refuses it.
BOOL __stdcall SBmpLoadImage(const char *pszFileName, void *pPalette, void *pBuf, DWORD cbBuf, DWORD *pdwWidth,
                             DWORD *pdwHeight, DWORD *pdwBpp)
{
  if (pdwWidth)
    *pdwWidth = 0;
  if (pdwHeight)
    *pdwHeight = 0;
  if (pdwBpp)
    *pdwBpp = 0;
  if (!*pszFileName || (pBuf && !cbBuf)) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  DWORD cb;
  BYTE *pData = LoadFile(pszFileName, &cb);
  if (!pData)
    return FALSE;
  BOOL bOk = FALSE;
  if (ImageKind(pData, cb) == IMAGE_PCX && cb >= PCX_HEADER)
    bOk = PcxDecode(pData, cb, pPalette, pBuf, cbBuf, pdwWidth, pdwHeight, pdwBpp, true);
  free(pData);
  return bOk;
}

// Ordinal 325. A file into a buffer of its own size, from pfnAlloc (__stdcall,
// one argument) or else SMemAlloc. With dwBpp not 0, an image of another depth
// is refused.
BOOL __stdcall SBmpAllocLoadImage(const char *pszFileName, void *pPalette, void **ppBuffer, DWORD *pdwWidth,
                                  DWORD *pdwHeight, DWORD *pdwBpp, DWORD dwBpp, PFN_BMPALLOC pfnAlloc)
{
  if (ppBuffer)
    *ppBuffer = NULL;
  if (pdwWidth)
    *pdwWidth = 0;
  if (pdwHeight)
    *pdwHeight = 0;
  if (pdwBpp)
    *pdwBpp = 0;
  if (!pszFileName || !ppBuffer || !*pszFileName) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  DWORD cb;
  BYTE *pData = LoadFile(pszFileName, &cb);
  if (!pData)
    return FALSE;
  if (cb == 0) {
    free(pData);
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  DWORD nWidth, nHeight, nBpp;
  if (ImageKind(pData, cb) != IMAGE_PCX || !PcxDecode(pData, cb, NULL, NULL, 0, &nWidth, &nHeight, &nBpp)) {
    free(pData);
    return FALSE;
  }
  if (dwBpp == 0)
    dwBpp = nBpp;
  if (dwBpp != nBpp) {
    free(pData);
    return FALSE;
  }
  const int nBits = static_cast<int>(nWidth * nHeight * dwBpp);
  const DWORD cbImage = static_cast<DWORD>(nBits / 8);
  void *pImage = pfnAlloc ? pfnAlloc(cbImage) : SMemAlloc(cbImage, pszFileName, -4, 0);
  *ppBuffer = pImage;
  if (pImage) {
    if (!cbImage)
      SErrSetLastError(SERR_INVALID);
    else
      PcxDecode(pData, cb, pPalette, pImage, cbImage, NULL, NULL, NULL);
  }
  free(pData);
  if (!pImage)
    return FALSE;
  if (pdwWidth)
    *pdwWidth = nWidth;
  if (pdwHeight)
    *pdwHeight = nHeight;
  if (pdwBpp)
    *pdwBpp = dwBpp;
  return TRUE;
}

// Ordinal 327. Rows of nWidth bytes made nAlign-aligned in place, the padding
// zeroed; *pdwNewWidth is the new pitch. Rows already aligned are left, and so
// is *pdwNewWidth; a buffer too small for the padded rows is refused.
BOOL __stdcall SBmpPadImage(void *pBits, DWORD cbBits, int nWidth, int nHeight, int nAlign, DWORD *pdwNewWidth)
{
  if (nAlign <= 0)
    return FALSE;
  const int nRem = nWidth % nAlign;
  if (nRem == 0)
    return TRUE;
  const int nPitch = nWidth + nAlign - nRem;
  if (cbBits < static_cast<DWORD>(nPitch * nHeight))
    return FALSE;
  BYTE *pOut = static_cast<BYTE *>(pBits);
  BYTE *pIn = pOut + nPitch * nHeight - nWidth * nHeight;
  memmove(pIn, pOut, static_cast<size_t>(nWidth * nHeight));
  for (int y = 0; y < nHeight; ++y) {
    memmove(pOut, pIn, static_cast<size_t>(nWidth));
    memset(pOut + nWidth, 0, static_cast<size_t>(nPitch - nWidth));
    pIn += nWidth;
    pOut += nPitch;
  }
  if (pdwNewWidth)
    *pdwNewWidth = static_cast<DWORD>(nPitch);
  return TRUE;
}

// Ordinal 328. nWidth rounded up to a multiple of nAlign.
int __stdcall SBmpGetPitchForAlignment(int nWidth, int nAlign)
{
  if (nAlign <= 0)
    return nWidth;
  const int nRem = nWidth % nAlign;
  return nRem ? nWidth - nRem + nAlign : nWidth;
}

}  // extern "C"

bool PortBmpDecodePcx(const void *pData, size_t cb, unsigned char **ppPixels, int *pnWidth, int *pnHeight)
{
  *ppPixels = NULL;
  const BYTE *p = static_cast<const BYTE *>(pData);
  if (p == NULL || cb < PCX_HEADER || cb > 0x7FFFFFFF || ImageKind(p, static_cast<DWORD>(cb)) != IMAGE_PCX)
    return false;
  DWORD nWidth, nHeight, nBpp;
  if (!PcxDecode(p, static_cast<DWORD>(cb), NULL, NULL, 0, &nWidth, &nHeight, &nBpp) || nBpp != 8 || nWidth == 0 ||
      nHeight == 0 || nWidth > 0x1000 || nHeight > 0x1000)
    return false;
  const DWORD cbImage = nWidth * nHeight;
  BYTE *pImage = static_cast<BYTE *>(SMemAlloc(cbImage, "port_stormbmp.cpp", __LINE__, 0));
  if (pImage == NULL)
    return false;
  if (!PcxDecode(p, static_cast<DWORD>(cb), NULL, pImage, cbImage, NULL, NULL, NULL)) {
    SMemFree(pImage, "port_stormbmp.cpp", __LINE__, 0);
    return false;
  }
  *ppPixels = pImage;
  *pnWidth = static_cast<int>(nWidth);
  *pnHeight = static_cast<int>(nHeight);
  return true;
}
