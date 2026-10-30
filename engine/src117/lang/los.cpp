// los.cpp -- the line-of-sight mask tables: one pattern's lit cells, the ring
// walk that chains them, and the per-map build and release. The four bodies are
// oracle 0x466990 to 0x466F20.
//
// Bodies are in ascending oracle address, 1.17's own order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/los.h"
#include "../gen/globals117.h"
#include "../h/mask.h"      // mask_tbl_t, mask_ent_t
#include "../h/Storm.h"     // SMemAlloc / SMemFree
#include <string.h>         // memset

// The globals this TU reaches, each spelled once for the standalone build and
// once for the payload.

// 0x4EE644 and 0x4EE648 -- sub_466BD0's walk directions, dx {1,0,-1,0} then
// dy {0,1,0,-1}, read with movsx.
#ifdef OC_DLL_MODE
#define dword_4EE644 (reinterpret_cast<char*>(OC_VA(0x4EE644u)))
#else
extern char dword_4EE644[4];
#endif

#ifdef OC_DLL_MODE
#define dword_4EE648 (reinterpret_cast<char*>(OC_VA(0x4EE648u)))
#else
extern char dword_4EE648[4];
#endif

// One scratch cell per tile of a sight pattern, stride 6.
#pragma pack(push, 2)
struct los_cell_t {
  /*0x00*/ mask_ent_t *pEnt;
  /*0x04*/ BYTE bUnk04;
  /*0x05*/ BYTE bLit;
};
#pragma pack(pop)
static_assert(offsetof(los_cell_t, pEnt) == 0x00, "los_cell_t::pEnt");
OC_X86_LAYOUT(offsetof(los_cell_t, bUnk04) == 0x04, "los_cell_t::bUnk04");
OC_X86_LAYOUT(offsetof(los_cell_t, bLit) == 0x05, "los_cell_t::bLit");
OC_X86_LAYOUT(sizeof(los_cell_t) == 6, "los_cell_t size");

// Marks the lit cells of one pattern (the unit's box, then a midpoint circle)
// and returns how many there are. The two width reads for x and cx stay separate;
// folded into one they take the row start with them and lose the oracle's
// induction variables.
int __declspec(noinline) sub_466990(mask_tbl_t *pTbl, void *pScratch)
{
  los_cell_t *pCell;
  los_cell_t *p;
  int nRows, i;

  memset(pScratch, 0, pTbl->nWidth * pTbl->nHeight * sizeof(los_cell_t));
  nRows = pTbl->nUnk0C;
  int nW = pTbl->nWidth;
  pCell = (los_cell_t *)pScratch + (pTbl->nHeight - nRows) * nW / 2 + (nW - pTbl->nUnk08) / 2;
  while (nRows--) {
    for (i = pTbl->nUnk08; i != 0; ) {
      pCell->bLit = 1;
      i--;
      pCell++;
    }
    pCell += pTbl->nWidth - pTbl->nUnk08;
  }

  int y, x, cx, cy;
  DWORD d, dlim;

  cx = pTbl->nWidth / 2;
  cy = pTbl->nHeight / 2;
  x = pTbl->nWidth / 2;
  d = x * x;
  dlim = (x - 1) * x;
  y = 0;

  do {
    i = cx - x;
    p = &((los_cell_t *)pScratch)[(cy - y) * pTbl->nWidth + i];
    for (; i <= cx + x; i++) { p->bLit = 1; p++; }
    i = cx - x;
    p = &((los_cell_t *)pScratch)[(cy + y) * pTbl->nWidth + i];
    for (; i <= cx + x; i++) { p->bLit = 1; p++; }
    i = cx - y;
    p = &((los_cell_t *)pScratch)[(cy - x) * pTbl->nWidth + i];
    for (; i <= cx + y; i++) { p->bLit = 1; p++; }
    i = cx - y;
    p = &((los_cell_t *)pScratch)[(cy + x) * pTbl->nWidth + i];
    for (; i <= cx + y; i++) { p->bLit = 1; p++; }
    y++;
    d -= 2 * y - 1;
    if (d <= dlim) {
      x--;
      dlim -= 2 * x;
    }
  } while (y <= x);

  int nLit = 0;
  int nCells = pTbl->nWidth * pTbl->nHeight;
  if (nCells != 0) {
    p = (los_cell_t *)pScratch;
    do {
      nCells--;
      if (p->bLit)
        nLit++;
      p++;
    } while (nCells != 0);
  }
  return nLit;
}

// Walks the lit cells ring by ring out from the centre, giving each an entry
// that links the one or two cells nearer the centre it depends on.
void __declspec(noinline) sub_466BD0(mask_tbl_t *pTbl, void *pScratch, int nMapWidth)
{
  DWORD nCount = 1;
  int nRing = 2;  // set here: declared at the loop, the entry jump grows 2 B
  DWORD nTotal = pTbl->nShaded + pTbl->nDirect;
  mask_ent_t *pEnt = pTbl->pEnts;
  int w = pTbl->nWidth;
  int h = pTbl->nHeight;
  los_cell_t *pCentre = &((los_cell_t *)pScratch)[h / 2 * w + w / 2];
  pCentre->pEnt = pEnt;
  pEnt++;
  int x = -1;
  int y = -1;
  int anLim[5];
  anLim[1] = anLim[3] = pTbl->nWidth;
  anLim[0] = anLim[2] = anLim[4] = pTbl->nHeight;
  while (nCount < nTotal) {
    for (DWORD k = 0; k < 4; k++) {
      char cdx, cdy;
      int n;
      if (nRing > anLim[k]) {
        cdx = dword_4EE644[k];
        x += cdx * nRing;
        cdy = dword_4EE648[k];
        y += cdy * nRing;
        n = 1;
      } else {
        cdx = dword_4EE644[k];
        x += cdx;
        cdy = dword_4EE648[k];
        y += cdy;
        n = anLim[k + 1] - 1;
        if (nRing < n)
          n = nRing;
      }
      while (n--) {
        if (pCentre[pTbl->nWidth * y + x].bLit) {
          if (x != 0 || y != 0) {
            int ax, ay, nAbsX, nAbsY;
            if (x == 0) {
              ax = 0;
              nAbsX = 0;
            } else if (x < 0) {
              ax = x + 1;
              nAbsX = -ax;
            } else {
              ax = x - 1;
              nAbsX = ax;
            }
            if (y == 0) {
              ay = 0;
              nAbsY = 0;
            } else if (y < 0) {
              ay = y + 1;
              nAbsY = -ay;
            } else {
              ay = y - 1;
              nAbsY = ay;
            }
            if (nAbsX == nAbsY || (x == 0 && cdx != 0) || (y == 0 && cdy != 0)) {
              pEnt->pPrev[0] = pEnt->pPrev[1] = pCentre[pTbl->nWidth * ay + ax].pEnt;
              pEnt->nPrev = 1;
            } else {
              pEnt->pPrev[0] = pCentre[pTbl->nWidth * ay + ax].pEnt;
              if (nAbsX > nAbsY) {
                if (y < 0)
                  ay--;
                else
                  ay++;
              } else {
                if (x < 0)
                  ax--;
                else
                  ax++;
              }
              pEnt->pPrev[1] = pCentre[pTbl->nWidth * ay + ax].pEnt;
              pEnt->nPrev = 2;
            }
          }
          pEnt->cX = (char)x;
          pEnt->cY = (char)y;
          pEnt->dwTile = y * nMapWidth + x;
          pCentre[pTbl->nWidth * y + x].pEnt = pEnt;
          pEnt++;
          nCount++;
        }
        x += cdx;
        y += cdy;
      }
      x -= cdx;
      y -= cdy;
    }
    if (nRing < pTbl->nWidth - 1)
      x--;
    if (nRing < pTbl->nHeight - 1)
      y--;
    nRing += 2;
  }
}

// Builds the sight patterns. The scratch array is one 6-byte cell per tile of
// the widest table, reused by every pattern and released at the end.
void sub_466E40(mask_tbl_t *pTbls, int nCount, int nMapWidth)
{
  DWORD dwMax = 0;
  mask_tbl_t *pScan = pTbls;
  int nScan = nCount;
  while (nScan--) {
    DWORD dwCells = (DWORD)(pScan->nHeight * pScan->nWidth);
    ++pScan;
    if (dwMax > dwCells)
      dwCells = dwMax;
    dwMax = dwCells;
  }

  void *pScratch = SMemAlloc(dwMax * sizeof(los_cell_t), "los.cpp", 0x11E, 0);  // Hyperion: 6 in 1.17

  mask_tbl_t *pTbl = pTbls;
  int i = nCount;
  while (i--) {
    pTbl->nDirect = pTbl->nUnk0C * pTbl->nUnk08;
    int nTotal = sub_466990(pTbl, pScratch);
    pTbl->nShaded = nTotal - pTbl->nDirect;
    DWORD cbEnts = (DWORD)(nTotal * sizeof(mask_ent_t));
    pTbl->pEnts = static_cast<mask_ent_t *>(SMemAlloc(cbEnts, "los.cpp", 0x133, 0));
    memset(pTbl->pEnts, 0, cbEnts);
    sub_466BD0(pTbl, pScratch, nMapWidth);
    ++pTbl;
  }

  SMemFree(pScratch, "los.cpp", 0x141, 0);
}

// Releases each table's cell array. Clearing pEnts sits inside the null test
// here, unlike light_free's.
void los_free(mask_tbl_t *pTbls, int nCount)
{
  int i = nCount;
  while (i--) {
    if (pTbls->pEnts != NULL) {
      SMemFree(pTbls->pEnts, "los.cpp", 0x14D, 0);
      pTbls->pEnts = NULL;
    }
    ++pTbls;
  }
}
