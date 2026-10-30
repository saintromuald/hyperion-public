// port_mask.cpp -- the screen's masks, in place of Storm's STrans functions, at any size
// (docs/modules/render.md; docs/renderer.md, R2a).
//
// A mask marks the pixels a copy takes, over its own width and height from the top left. The
// game keeps one, the console art's inverted, which is where the map shows through the console
// (vid_mask_initialize). Each frame it is cut to the 16x16 cells the frame redrew (gbInvalMap)
// and the back buffer is copied to the screen through the cut (img_update); the cursor code asks
// it whether a point is on the map (cursor.cpp). Storm's handle type names Hyperion's: a struct
// _HSTRANS here is one of these masks, not Storm's, so the game's globals keep their type. Only
// Storm's behaviour is kept, the pixels each mask copies for every operation, which
// tools/porttest/mask_test.cpp checks against the player's storm.dll.
#include <windows.h>
#include <emmintrin.h>  // SSE2, which every x64 processor has
#include <stdlib.h>
#include <string.h>
#include "port_mask.h"

struct _HSTRANS {
  int nWidth;
  int nHeight;
  BYTE *pbIn;   // one byte a pixel, row by row: 1 where the mask copies
};

// The dirty grid IntersectDirty reads: cells of 1 << shift pixels, and the
// grid's width in cells. Unset until PortMaskSetDirtyGrid.
static bool g_bGrid;
static int g_nCellShiftX;
static int g_nCellShiftY;
static int g_nGridWidth;

static struct _HSTRANS *MaskAlloc(int nWidth, int nHeight)
{
  const size_t cb = static_cast<size_t>(nWidth) * static_cast<size_t>(nHeight);
  struct _HSTRANS *pMask = static_cast<struct _HSTRANS *>(malloc(sizeof(struct _HSTRANS) + cb));

  if (pMask == NULL)
    return NULL;
  pMask->nWidth = nWidth;
  pMask->nHeight = nHeight;
  pMask->pbIn = reinterpret_cast<BYTE *>(pMask + 1);
  return pMask;
}

static size_t MaskPixels(const struct _HSTRANS *pMask)
{
  return static_cast<size_t>(pMask->nWidth) * static_cast<size_t>(pMask->nHeight);
}

// STransCreateE with no rectangle and flags 0x1000000: every pixel that is not
// 0 is in the mask.
struct _HSTRANS *PortMaskCreate(const unsigned char *pBits, int nWidth, int nHeight)
{
  struct _HSTRANS *pMask;
  size_t i;

  if (pBits == NULL || nWidth <= 0 || nHeight <= 0)
    return NULL;
  pMask = MaskAlloc(nWidth, nHeight);
  if (pMask == NULL)
    return NULL;
  for (i = 0; i < MaskPixels(pMask); ++i)
    pMask->pbIn[i] = pBits[i] != 0;
  return pMask;
}

// Every pixel of nWidth x nHeight. Not one of Storm's: the screen's mask starts
// from it when the screen is wider than the console (vid_mask_initialize).
struct _HSTRANS *PortMaskFull(int nWidth, int nHeight)
{
  struct _HSTRANS *pMask;

  if (nWidth <= 0 || nHeight <= 0)
    return NULL;
  pMask = MaskAlloc(nWidth, nHeight);
  if (pMask != NULL)
    memset(pMask->pbIn, 1, MaskPixels(pMask));
  return pMask;
}

// STransInvertMask: the pixels the mask leaves, over the same size.
struct _HSTRANS *PortMaskInvert(const struct _HSTRANS *pMask)
{
  struct _HSTRANS *pOut;
  size_t i;

  if (pMask == NULL)
    return NULL;
  pOut = MaskAlloc(pMask->nWidth, pMask->nHeight);
  if (pOut == NULL)
    return NULL;
  for (i = 0; i < MaskPixels(pMask); ++i)
    pOut->pbIn[i] = !pMask->pbIn[i];
  return pOut;
}

// STransCombineMasks with operation 3: pMask less pCut placed at (nX, nY), over
// pMask's size. The part of pCut outside pMask is ignored.
struct _HSTRANS *PortMaskSubtract(const struct _HSTRANS *pMask, const struct _HSTRANS *pCut, int nX, int nY)
{
  struct _HSTRANS *pOut;
  int x;
  int y;

  if (pMask == NULL || pCut == NULL)
    return NULL;
  pOut = PortMaskDuplicate(pMask);
  if (pOut == NULL)
    return NULL;
  for (y = 0; y < pCut->nHeight; ++y) {
    const int nOutY = nY + y;
    if (nOutY < 0 || nOutY >= pOut->nHeight)
      continue;
    for (x = 0; x < pCut->nWidth; ++x) {
      const int nOutX = nX + x;
      if (nOutX >= 0 && nOutX < pOut->nWidth && pCut->pbIn[y * pCut->nWidth + x])
        pOut->pbIn[nOutY * pOut->nWidth + nOutX] = 0;
    }
  }
  return pOut;
}

// STransDuplicate.
struct _HSTRANS *PortMaskDuplicate(const struct _HSTRANS *pMask)
{
  struct _HSTRANS *pOut;

  if (pMask == NULL)
    return NULL;
  pOut = MaskAlloc(pMask->nWidth, pMask->nHeight);
  if (pOut != NULL)
    memcpy(pOut->pbIn, pMask->pbIn, MaskPixels(pMask));
  return pOut;
}

// STransSetDirtyArrayInfo: the dirty grid covers nWidth x nHeight pixels in
// cells of nCellWidth x nCellHeight, both powers of two; a row of the grid is
// as many cells as cover nWidth. Storm refuses other cell sizes and leaves no
// grid; so does this.
void PortMaskSetDirtyGrid(int nWidth, int nHeight, int nCellWidth, int nCellHeight)
{
  int nShiftX = 0;
  int nShiftY = 0;

  (void)nHeight;
  g_bGrid = false;
  while ((1 << nShiftX) < nCellWidth)
    ++nShiftX;
  while ((1 << nShiftY) < nCellHeight)
    ++nShiftY;
  if ((1 << nShiftX) != nCellWidth || (1 << nShiftY) != nCellHeight)
    return;
  g_nCellShiftX = nShiftX;
  g_nCellShiftY = nShiftY;
  g_nGridWidth = (nWidth - 1 + nCellWidth) >> nShiftX;
  g_bGrid = true;
}

// STransIntersectDirtyArray: the mask's pixels whose cell of the dirty grid
// has one of bDirty's bits set. Needs the grid.
struct _HSTRANS *PortMaskIntersectDirty(const struct _HSTRANS *pMask, const unsigned char *pDirty,
                                        unsigned char bDirty)
{
  struct _HSTRANS *pOut;
  int x;
  int y;

  if (pMask == NULL || pDirty == NULL || bDirty == 0 || !g_bGrid)
    return NULL;
  pOut = MaskAlloc(pMask->nWidth, pMask->nHeight);
  if (pOut == NULL)
    return NULL;
  // a cell at a time: a clean one is 0s, a dirty one 1 where the mask has a pixel, 16 at a
  // time (it runs at every present)
  const int nCell = 1 << g_nCellShiftX;
  const __m128i zero = _mm_setzero_si128();
  const __m128i one = _mm_set1_epi8(1);
  for (y = 0; y < pMask->nHeight; ++y) {
    const unsigned char *pCells = pDirty + (y >> g_nCellShiftY) * g_nGridWidth;
    const BYTE *pIn = pMask->pbIn + y * pMask->nWidth;
    BYTE *pOutRow = pOut->pbIn + y * pMask->nWidth;
    for (x = 0; x < pMask->nWidth;) {
      const int nEnd = (x | (nCell - 1)) + 1 < pMask->nWidth ? (x | (nCell - 1)) + 1 : pMask->nWidth;
      if ((pCells[x >> g_nCellShiftX] & bDirty) == 0) {
        memset(pOutRow + x, 0, nEnd - x);
        x = nEnd;
        continue;
      }
      for (; x + 16 <= nEnd; x += 16) {
        const __m128i in = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pIn + x));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(pOutRow + x), _mm_andnot_si128(_mm_cmpeq_epi8(in, zero), one));
      }
      for (; x < nEnd; ++x)
        pOutRow[x] = pIn[x] != 0;
    }
  }
  return pOut;
}

// STransBltUsingMask: copies the mask's pixels from pSrc to pDst, both from
// their first pixel. A source pitch of 0 is the mask's width.
void PortMaskBlt(unsigned char *pDst, const unsigned char *pSrc, int nDstPitch, int nSrcPitch,
                 const struct _HSTRANS *pMask)
{
  const int nPitch = nSrcPitch != 0 ? nSrcPitch : pMask->nWidth;
  int x;
  int y;

  // 16 pixels at a time: none in the mask, nothing; all, a copy; some, a blend
  const __m128i zero = _mm_setzero_si128();
  for (y = 0; y < pMask->nHeight; ++y) {
    const BYTE *pIn = pMask->pbIn + y * pMask->nWidth;
    for (x = 0; x + 16 <= pMask->nWidth; x += 16) {
      const __m128i keep = _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i *>(pIn + x)), zero);
      const int nKeep = _mm_movemask_epi8(keep);
      if (nKeep == 0xFFFF)
        continue;
      const __m128i src = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pSrc + x));
      if (nKeep == 0) {
        _mm_storeu_si128(reinterpret_cast<__m128i *>(pDst + x), src);
        continue;
      }
      const __m128i dst = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pDst + x));
      _mm_storeu_si128(reinterpret_cast<__m128i *>(pDst + x),
                       _mm_or_si128(_mm_and_si128(keep, dst), _mm_andnot_si128(keep, src)));
    }
    for (; x < pMask->nWidth; ++x) {
      if (pIn[x])
        pDst[x] = pSrc[x];
    }
    pDst += nDstPitch;
    pSrc += nPitch;
  }
}

// A mask's pixels, one byte each, 1 where it copies, and its size (the GPU scene's
// frame, port_scene.cpp).
const unsigned char *PortMaskBits(const struct _HSTRANS *pMask, int *pnWidth, int *pnHeight)
{
  if (pMask == NULL)
    return NULL;
  *pnWidth = pMask->nWidth;
  *pnHeight = pMask->nHeight;
  return pMask->pbIn;
}

// STransIsPixelInMask: 0 outside the mask's size.
int PortMaskHasPixel(const struct _HSTRANS *pMask, int nX, int nY)
{
  if (pMask == NULL || nX < 0 || nY < 0 || nX >= pMask->nWidth || nY >= pMask->nHeight)
    return 0;
  return pMask->pbIn[nY * pMask->nWidth + nX];
}

// STransDelete.
void PortMaskDelete(struct _HSTRANS *pMask)
{
  free(pMask);
}
