// port_storm.cpp -- the drawing Storm did for the game, in the port's own code
// (docs/renderer.md, R2b). The callers in src117/lang call these in place of
// Storm's functions: the blit with Storm's arguments, the region through four
// functions of its own. tools/porttest/mask_test.cpp runs the player's
// storm.dll and these on the same inputs and compares them.
#include <windows.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "../../h/rect32.h"
#include "port_scene.h"  // PortSceneBlit
#include "port_storm.h"

// SBltROP3 (Storm ordinal 313) with SRCCOPY, the only raster operation 1.17
// passes: nHeight rows of nWidth bytes from pSrc to pDest, the rows nSrcPitch
// and nDestPitch apart. Another operation draws nothing and answers FALSE.
// The same arguments as Storm's, so a caller only changes the name.
BOOL __stdcall PortBltRop3(void *pDest, const void *pSrc, int nWidth, int nHeight, int nDestPitch, int nSrcPitch,
                           DWORD dwUnused, DWORD dwRop)
{
  BYTE *pD = static_cast<BYTE *>(pDest);
  const BYTE *pS = static_cast<const BYTE *>(pSrc);

  (void)dwUnused;
  if (dwRop != SRCCOPY)
    return FALSE;
  if (PortSceneBlit(pDest, nDestPitch, pSrc, nSrcPitch, nWidth, nHeight, NULL, 0))  // the GPU scene
    return TRUE;
  while (nHeight-- > 0) {
    if (nWidth > 0)
      memmove(pD, pS, static_cast<size_t>(nWidth));
    pD += nDestPitch;
    pS += nSrcPitch;
  }
  return TRUE;
}

// ------------------------------------------------------------------ regions
// Storm's region (SRgnCreate, SRgnCombineRect, SRgnGetRects, SRgnDelete) for the one way 1.17
// uses it, the union of the dialogs' dirty rectangles, each added with operation 2. The dialogs
// redraw the pieces one by one in the order they come, so the pieces and their order are
// Storm's, read off the player's storm.dll: the rectangles are kept as added and cut into pieces
// when asked for, each adding what the pieces so far do not cover; one pass then joins touching
// pieces (RGN_GONE marks those used up), and the pieces go by bottom edge, then left. Storm's
// coordinates are floats, but every step is a compare, copy, min or max, so ints match.
struct PortRgn {
  rect32_t *pAdded;
  int nAdded;
  int nAddedMax;
  rect32_t *pPiece;
  int nPiece;
  int nPieceMax;
};

// Storm's mark for a piece that has been merged away, FLT_MAX on every side:
// larger than any coordinate, and empty.
static const int RGN_GONE = 0x7FFFFFFF;

static bool RgnRectHasArea(const rect32_t &rc) { return rc.left < rc.right && rc.top < rc.bottom; }

static void RgnPush(rect32_t *&pList, int &nCount, int &nMax, const rect32_t &rc)
{
  if (nCount == nMax) {
    nMax = nMax ? nMax * 2 : 16;
    pList = static_cast<rect32_t *>(realloc(pList, nMax * sizeof(rect32_t)));
  }
  pList[nCount++] = rc;
}

// Adds what of rc the pieces from nFirst up to nEnd do not cover.
static void RgnAddUncovered(PortRgn *pRgn, int nFirst, int nEnd, const rect32_t &rc)
{
  for (int i = nFirst; i < nEnd; ++i) {
    const rect32_t e = pRgn->pPiece[i];
    if (e.right > rc.left && e.bottom > rc.top && rc.right > e.left && rc.bottom > e.top) {
      const int nTop = rc.top > e.top ? rc.top : e.top;
      const int nBottom = e.bottom > rc.bottom ? rc.bottom : e.bottom;
      const rect32_t part[4] = {
          {rc.left, rc.top, rc.right, e.top},       // above it
          {rc.left, e.bottom, rc.right, rc.bottom}, // below it
          {rc.left, nTop, e.left, nBottom},         // left of it
          {e.right, nTop, rc.right, nBottom},       // right of it
      };
      for (int k = 0; k < 4; ++k) {
        if (RgnRectHasArea(part[k]))
          RgnAddUncovered(pRgn, i + 1, nEnd, part[k]);
      }
      return;
    }
  }
  RgnPush(pRgn->pPiece, pRgn->nPiece, pRgn->nPieceMax, rc);
}

static void RgnMarkGone(rect32_t &rc) { rc.left = rc.top = rc.right = rc.bottom = RGN_GONE; }

// The pass that joins the pieces.
static void RgnJoin(PortRgn *pRgn)
{
  for (int i = 1; i < pRgn->nPiece; ++i) {
    for (int j = 0; j < i; ++j) {
      rect32_t &a = pRgn->pPiece[i];
      rect32_t &b = pRgn->pPiece[j];
      if (a.left == b.left && a.right == b.right && (a.top == b.bottom || b.top == a.bottom)) {
        a.top = b.top > a.top ? a.top : b.top;
        a.bottom = a.bottom > b.bottom ? a.bottom : b.bottom;
        RgnMarkGone(b);
        break;
      }
      if (a.left != b.right && b.left != a.right)
        continue;
      if (a.top == b.top && a.bottom == b.bottom) {
        a.left = b.left > a.left ? a.left : b.left;
        a.right = a.right > b.right ? a.right : b.right;
        RgnMarkGone(b);
        break;
      }
      if (b.bottom > a.top && a.bottom > b.top) {
        const rect32_t part[5] = {
            {a.left, a.top, a.right, b.top},
            {b.left, b.top, b.right, a.top},
            {a.left, b.bottom, a.right, a.bottom},
            {b.left, a.bottom, b.right, b.bottom},
            {b.left > a.left ? a.left : b.left, a.top > b.top ? a.top : b.top,
             a.right > b.right ? a.right : b.right, b.bottom > a.bottom ? a.bottom : b.bottom},
        };
        for (int k = 0; k < 5; ++k) {
          if (RgnRectHasArea(part[k]))
            RgnPush(pRgn->pPiece, pRgn->nPiece, pRgn->nPieceMax, part[k]);
        }
        RgnMarkGone(pRgn->pPiece[i]);
        RgnMarkGone(pRgn->pPiece[j]);
        break;
      }
    }
  }
}

static int __cdecl RgnCompare(const void *p1, const void *p2)
{
  const rect32_t *a = static_cast<const rect32_t *>(p1);
  const rect32_t *b = static_cast<const rect32_t *>(p2);
  if (a->bottom != b->bottom)
    return a->bottom > b->bottom ? 1 : -1;
  return a->left > b->left ? 1 : a->left < b->left ? -1 : 0;
}

void *PortRgnCreate(void) { return calloc(1, sizeof(PortRgn)); }

void PortRgnDelete(void *hRgn)
{
  PortRgn *pRgn = static_cast<PortRgn *>(hRgn);
  if (pRgn == NULL)
    return;
  free(pRgn->pAdded);
  free(pRgn->pPiece);
  free(pRgn);
}

// SRgnClear.
void PortRgnClear(void *hRgn) { static_cast<PortRgn *>(hRgn)->nAdded = 0; }

// SRgnDuplicate: a new region of the same rectangles.
void *PortRgnDuplicate(void *hRgn)
{
  const PortRgn *pRgn = static_cast<const PortRgn *>(hRgn);
  PortRgn *pCopy = static_cast<PortRgn *>(PortRgnCreate());
  for (int i = 0; i < pRgn->nAdded; ++i)
    RgnPush(pCopy->pAdded, pCopy->nAdded, pCopy->nAddedMax, pRgn->pAdded[i]);
  return pCopy;
}

// SRgnCombineRect with operation 2: an empty rectangle adds nothing.
void PortRgnAddRect(void *hRgn, const rect32_t *prc)
{
  PortRgn *pRgn = static_cast<PortRgn *>(hRgn);
  if (RgnRectHasArea(*prc))
    RgnPush(pRgn->pAdded, pRgn->nAdded, pRgn->nAddedMax, *prc);
}

// SRgnGetRects: at most *pdwCount pieces into pRects, and *pdwCount set to the
// number written; with no pRects, *pdwCount is set to the number of pieces.
void PortRgnGetRects(void *hRgn, ULONG *pdwCount, rect32_t *pRects)
{
  PortRgn *pRgn = static_cast<PortRgn *>(hRgn);
  pRgn->nPiece = 0;
  for (int i = 0; i < pRgn->nAdded; ++i)
    RgnAddUncovered(pRgn, 0, pRgn->nPiece, pRgn->pAdded[i]);
  RgnJoin(pRgn);
  qsort(pRgn->pPiece, pRgn->nPiece, sizeof(rect32_t), RgnCompare);
  // The pieces used up sort last.
  while (pRgn->nPiece > 0 && !RgnRectHasArea(pRgn->pPiece[pRgn->nPiece - 1]))
    --pRgn->nPiece;
  ULONG dwCount = static_cast<ULONG>(pRgn->nPiece);
  if (pRects != NULL) {
    if (*pdwCount < dwCount)
      dwCount = *pdwCount;
    memcpy(pRects, pRgn->pPiece, dwCount * sizeof(rect32_t));
  }
  *pdwCount = dwCount;
}

// ------------------------------------------------------------ table blits
// SCodeExecute with the two programs effects_init compiled with SCodeCompile for the dialogs'
// translucency and shading: each destination pixel becomes pTable[index], the index a source
// and a destination byte, the destination high for ghSCodeOther_g and the source for
// ghSCodeTransparent_g (bSourceHigh). Rows are the pitches apart, where Storm's block held the
// pitch less the width.
void PortTableBlit(unsigned char *pDest, const unsigned char *pSrc, int nWidth, int nHeight, int nDestPitch,
                   int nSrcPitch, const unsigned char *pTable, int bSourceHigh)
{
  const int nSrcShift = bSourceHigh ? 8 : 0;
  const int nDestShift = bSourceHigh ? 0 : 8;
  if (PortSceneBlit(pDest, nDestPitch, pSrc, nSrcPitch, nWidth, nHeight, pTable, bSourceHigh ? 2 : 3))
    return;
  while (nHeight-- > 0) {
    for (int x = 0; x < nWidth; ++x)
      pDest[x] = pTable[pSrc[x] << nSrcShift | pDest[x] << nDestShift];
    pDest += nDestPitch;
    pSrc += nSrcPitch;
  }
}
