// sai_Contours.cpp -- the AI pathing contour workspace and the contour table. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller in this file by
// /Ob2, which the oracle (WPO off) never does across TUs; the lever against it
// is __declspec(noinline) on the declaration.

#include "oc_data.h"  // Hyperion: OC_VA
#include <intrin.h>              // _ReadWriteBarrier -- sub_488600
#include "../h/sai_Contours.h"
#include "../h/sai_ContoursUtil.h"
#include "../h/CUnit.h"
#include "../h/CUnitBounds.h"       // pos_sort_ent_t, SearchFinder
#include "../h/CUnitPathUtil.h"     // IsIntersectable
#include "../gen/globals117.h"
#include <vector>  // Hyperion: ContourEntries
#include "../h/sai_ContoursCreate.h"  // Hyperion: g_nContourLists

// Globals this TU reaches, dual-spelled, with one #ifdef per name rather than
// one over the block: a single guard would undeclare every name under it.

// gwMapPixWidth @0x543230, gwMapPixHeight @0x543234 and gUnitBoundBox
// @0x522C38 are registered, so gen emits all three and this TU declares none.

// CUnit__nSorted is the sheet's name. This TU reads it as a WORD
// (`movsx eax, word ptr [0x5C99B0]`) where CUnitBounds.cpp reads a DWORD.
// Hyperion: read whole, as CUnitFind.cpp and find.cpp read it. The two
// entries a unit has pass 32,767 at 16,384 units, which a stress run's pools hold, and the
// WORD then went negative and the walks took nothing; below, both read the same.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define CUnit__nSorted (*reinterpret_cast<int *>(OC_VA(0x5C99B0u)))
#else
extern int CUnit__nSorted;
#endif
#ifdef OC_DLL_MODE
#define g_pos_sort_x (reinterpret_cast<pos_sort_ent_t *>(OC_VA(0x5C99B8u)))
#else
extern pos_sort_ent_t g_pos_sort_x[POOL_FINDER_STORE];
#endif
#ifdef OC_DLL_MODE
#define g_pos_sort_y (reinterpret_cast<pos_sort_ent_t *>(OC_VA(0x5D03F8u)))
#else
extern pos_sort_ent_t g_pos_sort_y[POOL_FINDER_STORE];
#endif

// CUnit +0x13C/+0x140 index g_pos_sort_x and +0x144/+0x148 index
// g_pos_sort_y; still inside CUnit.h's `_unmapped_138`.
#define BOUND_LEFT(u)   (*reinterpret_cast<int *>(&(u)->_unmapped_138[0x04]))
#define BOUND_RIGHT(u)  (*reinterpret_cast<int *>(&(u)->_unmapped_138[0x08]))
#define BOUND_TOP(u)    (*reinterpret_cast<int *>(&(u)->_unmapped_138[0x0C]))
#define BOUND_BOTTOM(u) (*reinterpret_cast<int *>(&(u)->_unmapped_138[0x10]))

// The bodies, in oracle VA order.

// Record one scanned cell as a mark. A cell on the span's own border, or the
// start point itself, is forced open and sets the hit flag; anything else
// carries the caller's mask, and a repeat of a point already marked is folded
// into that mark instead of appended.
// bMask itself takes the forced 0xFF: a separate local for it rotates the
// four frame slots.
void contour_workspace_t::sub_488220(short nX, short nY, BYTE bMask, int nDir)
{
  if (nX == nTargetX && nY == nTargetY)
    return;
  // Hyperion: a point past the right or bottom edge of a map of 1,024 tiles (32,768
  // pixels) has wrapped to below -16,384 in a short; it is off the map and no path goes
  // there, so it is no mark (revision 13). A smaller map's marks never come near.
  if (nX < -0x4000 || nY < -0x4000)
    return;

  int bSet = 0;

  if ((nReserved44 != 0 &&
       (((nX == rcSpan.left || nX == rcSpan.right) &&
         nY >= rcSpan.top && nY <= rcSpan.bottom) ||
        ((nY == rcSpan.top || nY == rcSpan.bottom) &&
         nX >= rcSpan.left && nX <= rcSpan.right))) ||
      (nX == ptCur.x && nY == ptCur.y))
  {
    bSet = 1;
    bMask = 0xFF;
    bHit = 1;
  }

  if (nMarks >= 32)
    return;

  if (!bSet)
  {
    point16_t pt;
    pt.x = nX;
    pt.y = nY;
    for (int i = 0; i < nMarks; ++i)
    {
      if (*(DWORD *)&pt == *(DWORD *)&marks[i].pt)
      {
        marks[i].bA &= bMask;
        return;
      }
    }
  }

  marks[nMarks].pt.x = nX;
  marks[nMarks].pt.y = nY;
  marks[nMarks].bA = bMask;
  marks[nMarks].bB = (BYTE)bSet;
  ++nMarks;
}

// Open a scan: seat the target, clamp a 64-pixel box around it against the map
// and against the nearest contour in each direction, then mark the four sides
// the caller still wants walked.
void contour_workspace_t::sub_488320(int nX, int nY, BYTE bMask)
{
  // Merged arms under nEdge[1] < 0, which the clamp below tests; they compile
  // to one store and put `lea ebx, [edi+40h]` ahead of the nStack store.
  if (nEdge[1] < 0) { nTargetX = (short)nX; } else { nTargetX = (short)nX; }
  nTargetY = (short)nY;
  nMarks = 0;
  nStack = 0;

  nEdge[1] = (short)(nY - 64);
  nEdge[0] = (short)(nX - 64);
  // Hyperion: the far sides clamped before they are narrowed to a short: on a map of
  // 1,024 tiles (32,768 pixels) within 64 of the bottom or right edge, 1.17's order wrapped
  // the side negative, past the clamp, and the marks went off the map, where the region
  // lookup read outside its table (revision 13). Below 32,704 pixels it is the same.
  const int nFar3 = nY + 64 >= static_cast<int>(gwMapPixHeight) ? gwMapPixHeight - 1 : nY + 64;
  const int nFar2 = nX + 64 >= static_cast<int>(gwMapPixWidth) ? gwMapPixWidth - 1 : nX + 64;
  nEdge[3] = (short)nFar3;
  nEdge[2] = (short)nFar2;

  if (nEdge[1] < 0)
    nEdge[1] = 0;
  if (nEdge[0] < 0)
    nEdge[0] = 0;

  if (!(bMask & 0x19))
    nEdge[1] = (short)nY;
  if (!(bMask & 0x23))
    nEdge[2] = (short)nX;
  if (!(bMask & 0x46))
    nEdge[3] = (short)nY;
  if (!(bMask & 0x8C))
    nEdge[0] = (short)nX;

  sub_488600();
  sub_488910();

  pLists[0] = store0;
  pLists[1] = store1;
  pLists[2] = store2;
  pLists[3] = store3;
  nCounts[0] = nStore[0];
  nCounts[1] = nStore[1];
  nCounts[2] = nStore[2];
  nCounts[3] = nStore[3];

  pFound[0] = sub_48C5E0(nY, (short)nX, (short)nX, (WORD)(nEdge[1] - 1));
  if (pFound[0])
    nEdge[1] = (short)(nOrigin[0] + 1 + pFound[0]->pt.x);

  pFound[1] = sub_48C4E0(nX, (short)nY, (short)nY, (WORD)(nEdge[2] + 1));
  if (pFound[1])
    nEdge[2] = (short)(nOrigin[1] - 1 + pFound[1]->pt.x);

  pFound[2] = sub_48C300(nY, (short)nX, (short)nX, (WORD)(nEdge[3] + 1));
  if (pFound[2])
    nEdge[3] = (short)(nOrigin[2] - 1 + pFound[2]->pt.x);

  pFound[3] = sub_48C400(nX, (short)nY, (short)nY, (WORD)(nEdge[0] - 1));
  if (pFound[3])
    nEdge[0] = (short)(nOrigin[3] + 1 + pFound[3]->pt.x);

  if (nEdge[1] == (short)nY && nEdge[2] == (short)nX)
    bMask &= 0xFE;
  if (nEdge[3] == (short)nY && nEdge[2] == (short)nX)
    bMask &= 0xFD;
  if (nEdge[3] == (short)nY && nEdge[0] == (short)nX)
    bMask &= 0xFB;
  if (nEdge[1] == (short)nY && nEdge[0] == (short)nX)
    bMask &= 0xF7;
  if (nEdge[1] == (short)nY)
    bMask &= 0xEF;
  if (nEdge[2] == (short)nX)
    bMask &= 0xDF;
  if (nEdge[3] == (short)nY)
    bMask &= 0xBF;
  if (nEdge[0] == (short)nX)
    bMask &= 0x7F;

  if (bMask & 0x10)
  {
    bMask |= 9;
    BYTE b = 0xBF;
    if (pFound[0])
      b = 0xA6;
    sub_488220((short)nX, nEdge[1], b, 1);
  }
  if (bMask & 0x20)
  {
    bMask |= 3;
    BYTE b = 0x7F;
    if (pFound[1])
      b = 0x5C;
    sub_488220(nEdge[2], (short)nY, b, 2);
  }
  if (bMask & 0x40)
  {
    bMask |= 6;
    BYTE b = 0xEF;
    if (pFound[2])
      b = 0xA9;
    sub_488220((short)nX, nEdge[3], b, 3);
  }
  if (bMask & 0x80)
  {
    bMask |= 0xC;
    BYTE b = 0xDF;
    if (pFound[3])
      b = 0x53;
    sub_488220(nEdge[0], (short)nY, b, 4);
  }
  bScanMask = bMask;
}

// Hyperion: the rows of the map's contour lists, for sub_488600. Each of its
// four walks goes along a list, sorted by pt.x and then pt.y, from find_contour's place while
// pt.x is within nLim, keeping each contour that reaches across the scan box. A list holds the
// contours of the map's whole width or height, so on a large map a walk passed thousands for
// the few it kept: with the search for its place, a fifth of a battle's game time. The lists
// are the map's, made with it (contours_t::Init) or read from a save, and the game does not
// change them; g_nContourLists counts the times they are made, read or freed, and the rows are
// made again after. A list's rows give the first row at or past each pt.x, which is
// find_contour's answer where none of its doubled keys wraps; for each row, the cells of 32
// pixels its contours' spans cover; and for each contour the furthest end of those up to it
// in its row. A contour the walk keeps has its span meet the one between the least end and
// the most start that can reach the box for any of the unit's origins, so a walk passes a row
// none of whose cells that span meets, enters the others at the first contour whose end, or
// an earlier one's, can reach the box, and leaves them at the first whose start is past it:
// the contours between are tested as the walk tests them and kept in its order.
// HYPERION_FINDER_CHECK=1 walks as well and compares the contours kept.
namespace {
struct contour_rows_t {
  const contour_t *pList = nullptr;
  int nCount = -1;
  unsigned nMade = 0;
  bool bRows = false;         // the list is in order and no doubled pt.x wraps
  int nFirstX = 0, nLastX = 0;
  int nRows = 0;
  std::vector<int> rowAt;     // the first row at or past pt.x nFirstX + k, to nLastX + 1
  std::vector<int> rowFirst;  // each row's first contour, and the count after the last
  int nLowY = 0, nHighY = 0;  // the least and the most of the contours' spans
  int nWords = 0;             // a row's cells, in words of 64
  std::vector<unsigned long long> cells;
  std::vector<int> endMax;    // the furthest nEnd of the contours of its row up to each
};
contour_rows_t s_contourRows[4];
}

static const int CONTOUR_CELL_SHIFT = 5;  // cells of 32 pixels

static const contour_rows_t &ContourRows(int k, const contour_t *pList, int nCount)
{
  contour_rows_t &r = s_contourRows[k];
  if (r.pList == pList && r.nCount == nCount && r.nMade == g_nContourLists)
    return r;
  r.pList = pList;
  r.nCount = nCount;
  r.nMade = g_nContourLists;
  r.bRows = false;
  r.rowAt.clear();
  r.rowFirst.clear();
  r.cells.clear();
  r.endMax.clear();
  if (nCount <= 0)
    return r;
  r.nFirstX = pList[0].pt.x;
  r.nLastX = pList[nCount - 1].pt.x;
  // a doubled position fits an int: positions are kept in 31 bits at most
  if (PosS(r.nFirstX * 2) != r.nFirstX * 2 || PosS(r.nLastX * 2) != r.nLastX * 2 ||
      r.nLastX < r.nFirstX || r.nLastX - r.nFirstX > (1 << 22))
    return r;
  int nRows = 0, nLowY = 0, nHighY = 0;
  for (int i = 0; i < nCount; ++i) {
    const int x = pList[i].pt.x, y = pList[i].pt.y, e = pList[i].nEnd;
    if (i > 0 && (x < pList[i - 1].pt.x || (x == pList[i - 1].pt.x && y < pList[i - 1].pt.y)))
      return r;
    if (x > r.nLastX)
      return r;
    nRows += i == 0 || x != pList[i - 1].pt.x;
    const int nLo = y < e ? y : e, nHi = y < e ? e : y;
    nLowY = i == 0 || nLo < nLowY ? nLo : nLowY;
    nHighY = i == 0 || nHi > nHighY ? nHi : nHighY;
  }
  if (nHighY - nLowY > (1 << 22))
    return r;
  r.nRows = nRows;
  r.nLowY = nLowY;
  r.nHighY = nHighY;
  r.nWords = (((nHighY - nLowY) >> CONTOUR_CELL_SHIFT) >> 6) + 1;
  r.rowAt.resize(static_cast<size_t>(r.nLastX - r.nFirstX) + 2);
  r.rowFirst.resize(nRows + 1);
  r.cells.assign(static_cast<size_t>(nRows) * r.nWords, 0);
  r.endMax.resize(nCount);
  int nRow = -1;
  int nX = r.nFirstX;  // the next pt.x whose first row is to be set
  for (int i = 0; i < nCount; ++i) {
    const int x = pList[i].pt.x, y = pList[i].pt.y, e = pList[i].nEnd;
    const bool bRow = i == 0 || x != pList[i - 1].pt.x;
    if (bRow) {
      r.rowFirst[++nRow] = i;
      for (; nX <= x; ++nX)
        r.rowAt[nX - r.nFirstX] = nRow;
    }
    r.endMax[i] = bRow || e > r.endMax[i - 1] ? e : r.endMax[i - 1];
    unsigned long long *pw = &r.cells[static_cast<size_t>(nRow) * r.nWords];
    const int c1 = ((y < e ? e : y) - nLowY) >> CONTOUR_CELL_SHIFT;
    for (int c = ((y < e ? y : e) - nLowY) >> CONTOUR_CELL_SHIFT; c <= c1; ++c)
      pw[c >> 6] |= 1ull << (c & 63);
  }
  r.rowFirst[nRows] = nCount;
  for (; nX <= r.nLastX + 1; ++nX)
    r.rowAt[nX - r.nFirstX] = nRows;
  r.bRows = true;
  return r;
}

// The first row of the list at or past pt.x n.
static int ContourRowsAt(const contour_rows_t &r, int n)
{
  return n <= r.nFirstX ? 0 : n > r.nLastX ? r.nRows : r.rowAt[n - r.nFirstX];
}

// One of sub_488600's walks over the map's list k: from find_contour's place for nFrom while
// pt.x is within nLim, each contour whose start is within nEdgeHi and whose end reaches nEdgeLo
// by the origins its flags choose goes to sub_488F00, in the list's order. False where the
// rows cannot answer, or in the check, and the walk is then made.
static bool ContourRowsWalk(contour_workspace_t *pWs, int k, int nFrom, int nLim, int nEdgeHi, int nEdgeLo,
                            contour_t **ppPos, contour_t **ppEnd)
{
  const contour_t *pList = pWs->pLists[k];
  const int nCount = pWs->nCounts[k];
  // the map's lists alone, never the workspace's own stores
  const BYTE *pb = reinterpret_cast<const BYTE *>(pList);
  if (nCount <= 0 || pList != pWs->pSavedLists[k] || nCount != pWs->nSavedCounts[k] ||
      (pb >= reinterpret_cast<const BYTE *>(pWs) && pb < reinterpret_cast<const BYTE *>(pWs + 1)))
    return false;
  const contour_rows_t &r = ContourRows(k, pList, nCount);
  if (!r.bRows || PosS(nFrom * 2 - 1) != nFrom * 2 - 1)
    return false;
  const short *o = pWs->nOrigin;
  int oMin = o[0], oMax = o[0];
  for (int j = 1; j < 4; ++j) {
    oMin = o[j] < oMin ? o[j] : oMin;
    oMax = o[j] > oMax ? o[j] : oMax;
  }
  const int nStartMax = nEdgeHi - oMin;  // a contour starting past it is past the box for any origin
  const int nEndMin = nEdgeLo - oMax;    // and one ending before it is short of it
  // the span every kept contour's own meets, in cells
  const int nSpanLo = nEndMin < nStartMax ? nEndMin : nStartMax;
  const int nSpanHi = nEndMin < nStartMax ? nStartMax : nEndMin;
  static std::vector<const contour_t *> s_rows, s_walk;
  s_rows.clear();
  const int r0 = ContourRowsAt(r, nFrom);
  const int nAbove = ContourRowsAt(r, nLim + 1);
  const int r1 = nAbove > r0 ? nAbove : r0;
  if (nSpanHi >= r.nLowY && nSpanLo <= r.nHighY) {
    const int c0 = ((nSpanLo > r.nLowY ? nSpanLo : r.nLowY) - r.nLowY) >> CONTOUR_CELL_SHIFT;
    const int c1 = ((nSpanHi < r.nHighY ? nSpanHi : r.nHighY) - r.nLowY) >> CONTOUR_CELL_SHIFT;
    const int w0 = c0 >> 6, w1 = c1 >> 6;
    const unsigned long long m0 = ~0ull << (c0 & 63), m1 = ~0ull >> (63 - (c1 & 63));
    for (int nRow = r0; nRow < r1; ++nRow) {
      const unsigned long long *pw = &r.cells[static_cast<size_t>(nRow) * r.nWords];
      bool bAny = w0 == w1 ? (pw[w0] & m0 & m1) != 0 : (pw[w0] & m0) != 0 || (pw[w1] & m1) != 0;
      for (int w = w0 + 1; !bAny && w < w1; ++w)
        bAny = pw[w] != 0;
      if (!bAny)
        continue;
      const int nRowEnd = r.rowFirst[nRow + 1];
      int lo = r.rowFirst[nRow], hi = nRowEnd;
      while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (r.endMax[mid] < nEndMin)
          lo = mid + 1;
        else
          hi = mid;
      }
      for (int j = lo; j < nRowEnd && pList[j].pt.y <= nStartMax; ++j) {
        const contour_t *p = pList + j;
        if (o[p->bFlags & 3] + p->pt.y <= nEdgeHi && o[(p->bFlags >> 2) & 3] + p->nEnd >= nEdgeLo) {
          if (g_bFinderSideCheck)
            s_rows.push_back(p);
          else
            pWs->sub_488F00(p, ppPos, ppEnd);
        }
      }
    }
  }
  if (!g_bFinderSideCheck)
    return true;
  s_walk.clear();
  const int i = find_contour(const_cast<contour_t *>(pList), nCount, nFrom, 1);
  for (const contour_t *p = pList + i; p < pList + nCount && p->pt.x <= nLim; ++p)
    if (o[p->bFlags & 3] + p->pt.y <= nEdgeHi && o[(p->bFlags >> 2) & 3] + p->nEnd >= nEdgeLo)
      s_walk.push_back(p);
  if (s_walk != s_rows)
    FinderSideDiffer("a contour list's rows", static_cast<int>(s_walk.size()));
  return false;
}

// Gather the contours each directional list holds inside the scan box into the
// workspace's own four stores, keeping each store sorted and merged.
// Hyperion: the map's lists answered from their rows (ContourRowsWalk).
void contour_workspace_t::sub_488600()
{
  contour_t *p;
  contour_t *pLast;
  contour_t *pPos;
  contour_t *pEnd;
  spos_t nLim;  // Hyperion: a position
  int i;

  nLim = nTargetY - nOrigin[0] - 1;
  pPos = store0;
  pEnd = store0 + nStore[0];
  if (!ContourRowsWalk(this, 0, nEdge[1] - nOrigin[0] - 1, nLim, nEdge[2], nEdge[0], &pPos, &pEnd))
  {
    i = find_contour(pLists[0], nCounts[0], nEdge[1] - nOrigin[0] - 1, 1);
    pLast = pLists[0] + nCounts[0];
    p = pLists[0] + i;
    for (; p < pLast; ++p)
    {
      if (p->pt.x > nLim)
        break;
      if (nOrigin[p->bFlags & 3] + p->pt.y <= nEdge[2] &&
          nOrigin[(p->bFlags >> 2) & 3] + p->nEnd >= nEdge[0])
        sub_488F00(p, &pPos, &pEnd);
    }
  }
  nStore[0] = (short)(pEnd - store0);

  nLim = nEdge[2] - nOrigin[1] + 1;
  pPos = store1;
  pEnd = store1 + nStore[1];
  if (!ContourRowsWalk(this, 1, nTargetX - nOrigin[1] + 1, nLim, nEdge[3], nEdge[1], &pPos, &pEnd))
  {
    i = find_contour(pLists[1], nCounts[1], nTargetX - nOrigin[1] + 1, 1);
    pLast = pLists[1] + nCounts[1];
    p = pLists[1] + i;
    for (; p < pLast; ++p)
    {
      if (p->pt.x > nLim)
        break;
      if (nOrigin[p->bFlags & 3] + p->pt.y <= nEdge[3] &&
          nOrigin[(p->bFlags >> 2) & 3] + p->nEnd >= nEdge[1])
        sub_488F00(p, &pPos, &pEnd);
    }
  }
  nStore[1] = (short)(pEnd - store1);

  nLim = nEdge[3] - nOrigin[2] + 1;
  pPos = store2;
  pEnd = store2 + nStore[2];
  if (!ContourRowsWalk(this, 2, nTargetY - nOrigin[2] + 1, nLim, nEdge[2], nEdge[0], &pPos, &pEnd))
  {
    i = find_contour(pLists[2], nCounts[2], nTargetY - nOrigin[2] + 1, 1);
    pLast = pLists[2] + nCounts[2];
    p = pLists[2] + i;
    for (; p < pLast; ++p)
    {
      if (p->pt.x > nLim)
        break;
      if (nOrigin[p->bFlags & 3] + p->pt.y <= nEdge[2] &&
          nOrigin[(p->bFlags >> 2) & 3] + p->nEnd >= nEdge[0])
        sub_488F00(p, &pPos, &pEnd);
    }
  }
  nStore[2] = (short)(pEnd - store2);

  nLim = nTargetX - nOrigin[3] - 1;
  pPos = store3;
  pEnd = store3 + nStore[3];
  if (!ContourRowsWalk(this, 3, nEdge[0] - nOrigin[3] - 1, nLim, nEdge[3], nEdge[1], &pPos, &pEnd))
  {
    i = find_contour(pLists[3], nCounts[3], nEdge[0] - nOrigin[3] - 1, 1);
    int nc = nCounts[3];
    // The barrier keeps the call's result copy after this load, as in blocks 1..3.
    _ReadWriteBarrier();
    pLast = pLists[3] + nc;
    p = pLists[3] + i;
    for (; p < pLast; ++p)
    {
      if (p->pt.x > nLim)
        break;
      if (nOrigin[p->bFlags & 3] + p->pt.y <= nEdge[3] &&
          nOrigin[(p->bFlags >> 2) & 3] + p->nEnd >= nEdge[1])
        sub_488F00(p, &pPos, &pEnd);
    }
  }
  nStore[3] = (short)(pEnd - store3);
}

// The bound orderings hold one-based unit ids; zero is no unit.
static CUnit *bound_unit(DWORD dwUnitId)
{
  return dwUnitId == 0 ? 0 : CUnit::Head() + dwUnitId - 1;
}

// Hyperion: the entries one of sub_488910's walks takes, in order. The walk
// goes along an ordering from nFrom's place while the entries are before nLim (at it too with
// bThrough), taking the unit's edges of one kind: in a large game about 60 entries for each
// one used, as the ordering holds the units of the map's whole width or height. Where none of
// the sums its checks make can wrap the grid gives them instead (FinderGridEdges): every unit
// whose edge is in that range and whose other extent can pass the checks, nOtherLo and
// nOtherHi, in the ordering's order. HYPERION_FINDER_CHECK=1 walks, makes the grid's too and
// compares the entries of each that pass the checks (the origins and edges oLow, eLow, oHigh
// and eHigh).
static void ContourEntries(const short *pnOrigin, const short *pnEdge, int bY, int bHigh, int nFrom, int nLim,
                           bool bThrough, int nOtherLo, int nOtherHi, int oLow, int eLow, int oHigh, int eHigh,
                           std::vector<int> &out)
{
  int nOrigin = 0;
  for (int k = 0; k < 4; ++k)
    nOrigin = (pnOrigin[k] < 0 ? -pnOrigin[k] : pnOrigin[k]) > nOrigin ? (pnOrigin[k] < 0 ? -pnOrigin[k] : pnOrigin[k]) : nOrigin;
  const bool bGrid = FinderMostAbs() + nOrigin + 2 <= (g_posShift == 16 ? 0x7FFF : 0x3FFFFFFF);
  out.clear();
  // the grid's answer, into a store kept from call to call: a unit has two entries
  static std::vector<int> s_found;
  if (s_found.size() < static_cast<size_t>(CUnit__nSorted))
    s_found.resize(CUnit__nSorted + 1024);
  if (bGrid && !g_bFinderSideCheck) {
    const int n = FinderGridEdges(bY, bHigh, nFrom, bThrough ? nLim : nLim - 1, nOtherLo, nOtherHi, s_found.data());
    out.assign(s_found.begin(), s_found.begin() + n);
    return;
  }
  const pos_sort_ent_t *pArr = bY ? g_pos_sort_y : g_pos_sort_x;
  for (int i = SearchFinder(bY ? g_pos_sort_y : g_pos_sort_x, nFrom, 1); i < (int)CUnit__nSorted; ++i) {
    const int nCoord = pArr[i].nCoord;
    if (bThrough ? nCoord > nLim : nCoord >= nLim)
      break;
    if ((bY ? FinderSideY(i) : FinderSideX(i)).bHigh != bHigh)
      continue;
    out.push_back(i);
  }
  if (!bGrid || !g_bFinderSideCheck)
    return;
  const int nFound = FinderGridEdges(bY, bHigh, nFrom, bThrough ? nLim : nLim - 1, nOtherLo, nOtherHi, s_found.data());
  const std::vector<int> s_grid(s_found.begin(), s_found.begin() + nFound);
  // the entries of a list that pass the walk's checks, as the walk makes them
  auto passed = [&](const std::vector<int> &v) {
    std::vector<int> kept;
    for (int i : v) {
      const finder_side_t &side = bY ? g_finder_side_y[i] : g_finder_side_x[i];
      const spos_t nLow = PosS(side.nLow), nHigh = PosS(side.nHigh);
      spos_t nSpan = pnOrigin[oLow] + nLow;
      if (nSpan > pnEdge[eLow])
        continue;
      nSpan = pnOrigin[oHigh] + nHigh;
      if (nSpan < pnEdge[eHigh])
        continue;
      kept.push_back(i);
    }
    return kept;
  };
  if (passed(out) != passed(s_grid))
    FinderSideDiffer("a contour walk's grid", static_cast<int>(out.size()));
}

// Gather the units whose collision boxes border the scan box into the four
// stores as one-contour edges, walking each bounds ordering outward from the
// target. The unit being pathed is always kept; any other must intersect.
// Hyperion: each walk's entries are found first (ContourEntries).
void contour_workspace_t::sub_488910()
{
  contour_t c;
  contour_t *pPos;
  contour_t *pEnd;
  spos_t nLim;  // Hyperion: positions
  CUnit *pUnit;
  spos_t nSpan;
  static std::vector<int> s_entries;

  nLim = nTargetY - nOrigin[0];
  pEnd = store0 + nStore[0];
  ContourEntries(nOrigin, nEdge, 1, 1, nEdge[1] - nOrigin[0] - 1, nLim, false, nEdge[0] - nOrigin[3],
                 nEdge[2] - nOrigin[1], 1, 2, 3, 0, s_entries);
  for (int i : s_entries)
  {
    const finder_side_t &side = FinderSideY(i);
    pUnit = bound_unit(g_pos_sort_y[i].dwUnitId);
    c.pt.x = PosS(g_pos_sort_y[i].nCoord);
    c.pt.y = PosS(side.nLow);
    c.nEnd = PosS(side.nHigh);
    *reinterpret_cast<WORD *>(&c.bC) = 0x3D00;
    nSpan = nOrigin[1] + c.pt.y;
    if (nSpan > nEdge[2])
      continue;
    nSpan = nOrigin[3] + c.nEnd;
    if (nSpan < nEdge[0])
      continue;
    if (reinterpret_cast<intptr_t>(pUnit) == nReserved44 ||
        IsIntersectable(reinterpret_cast<intptr_t>(pUnit), nReserved3C))
    {
      pPos = store0;
      sub_488F00(&c, &pPos, &pEnd);
    }
  }
  nStore[0] = (short)(pEnd - store0);

  nLim = nEdge[2] - nOrigin[1] + 1;
  pEnd = store1 + nStore[1];
  ContourEntries(nOrigin, nEdge, 0, 0, nTargetX - nOrigin[1], nLim, true, nEdge[1] - nOrigin[0],
                 nEdge[3] - nOrigin[2], 2, 3, 0, 1, s_entries);
  for (int i : s_entries)
  {
    const finder_side_t &side = FinderSideX(i);
    pUnit = bound_unit(g_pos_sort_x[i].dwUnitId);
    c.pt.x = PosS(g_pos_sort_x[i].nCoord);
    c.pt.y = PosS(side.nLow);
    c.nEnd = PosS(side.nHigh);
    *reinterpret_cast<WORD *>(&c.bC) = 0x3201;
    nSpan = nOrigin[2] + c.pt.y;
    if (nSpan > nEdge[3])
      continue;
    nSpan = nOrigin[0] + c.nEnd;
    if (nSpan < nEdge[1])
      continue;
    if (reinterpret_cast<intptr_t>(pUnit) == nReserved44 ||
        IsIntersectable(reinterpret_cast<intptr_t>(pUnit), nReserved3C))
    {
      pPos = store1;
      sub_488F00(&c, &pPos, &pEnd);
    }
  }
  nStore[1] = (short)(pEnd - store1);

  nLim = nEdge[3] - nOrigin[2] + 1;
  pEnd = store2 + nStore[2];
  ContourEntries(nOrigin, nEdge, 1, 0, nTargetY - nOrigin[2], nLim, true, nEdge[0] - nOrigin[3],
                 nEdge[2] - nOrigin[1], 1, 2, 3, 0, s_entries);
  for (int i : s_entries)
  {
    const finder_side_t &side = FinderSideY(i);
    pUnit = bound_unit(g_pos_sort_y[i].dwUnitId);
    c.pt.x = PosS(g_pos_sort_y[i].nCoord);
    c.pt.y = PosS(side.nLow);
    c.nEnd = PosS(side.nHigh);
    *reinterpret_cast<WORD *>(&c.bC) = 0x3D02;
    nSpan = nOrigin[1] + c.pt.y;
    if (nSpan > nEdge[2])
      continue;
    nSpan = nOrigin[3] + c.nEnd;
    if (nSpan < nEdge[0])
      continue;
    if (reinterpret_cast<intptr_t>(pUnit) == nReserved44 ||
        IsIntersectable(reinterpret_cast<intptr_t>(pUnit), nReserved3C))
    {
      pPos = store2;
      sub_488F00(&c, &pPos, &pEnd);
    }
  }
  nStore[2] = (short)(pEnd - store2);

  nLim = nTargetX - nOrigin[3];
  pEnd = store3 + nStore[3];
  ContourEntries(nOrigin, nEdge, 0, 1, nEdge[0] - nOrigin[3] - 1, nLim, false, nEdge[1] - nOrigin[0],
                 nEdge[3] - nOrigin[2], 2, 3, 0, 1, s_entries);
  for (int i : s_entries)
  {
    const finder_side_t &side = FinderSideX(i);
    pUnit = bound_unit(g_pos_sort_x[i].dwUnitId);
    c.pt.x = PosS(g_pos_sort_x[i].nCoord);
    c.pt.y = PosS(side.nLow);
    c.nEnd = PosS(side.nHigh);
    *reinterpret_cast<WORD *>(&c.bC) = 0x3203;
    nSpan = nOrigin[2] + c.pt.y;
    if (nSpan > nEdge[3])
      continue;
    nSpan = nOrigin[0] + c.nEnd;
    if (nSpan < nEdge[1])
      continue;
    if (reinterpret_cast<intptr_t>(pUnit) == nReserved44 ||
        IsIntersectable(reinterpret_cast<intptr_t>(pUnit), nReserved3C))
    {
      pPos = store3;
      sub_488F00(&c, &pPos, &pEnd);
    }
  }
  nStore[3] = (short)(pEnd - store3);
}

// Open the four search bounds to the empty interval: low ends at 1, high ends
// at -1, so the first candidate in each direction always wins.
void contours_t::Reset()
{
  nBound0 = 1;
  nBound1 = -1;
  nBound2 = -1;
  nBound3 = 1;
}

// Take a copy of the source table's header into the saved slots at +0x174,
// adopt its four bounds, then publish the saved copy as the live header.
void contour_workspace_t::sub_488E10(const contours_t *pSrc)
{
  pSavedLists[0] = pSrc->pLists[0];
  pSavedLists[1] = pSrc->pLists[1];
  pSavedLists[2] = pSrc->pLists[2];
  pSavedLists[3] = pSrc->pLists[3];
  nSavedCounts[0] = pSrc->nCounts[0];
  nSavedCounts[1] = pSrc->nCounts[1];
  nSavedCounts[2] = pSrc->nCounts[2];
  nSavedCounts[3] = pSrc->nCounts[3];
  nBound0 = pSrc->nBound0;
  nBound1 = pSrc->nBound1;
  nBound2 = pSrc->nBound2;
  nBound3 = pSrc->nBound3;
  sub_489110();
}

// Empty the four stores and clear the hit flag before a scan.
void contour_workspace_t::sub_488EE0()
{
  nStore[0] = 0;
  nStore[1] = 0;
  nStore[2] = 0;
  nStore[3] = 0;
  nReserved68 = 0;
  nReserved6C = 0;
  bHit = 0;
}

// Insert one contour into a list kept sorted by column, merging it into the
// contour already there when the two spans touch.
void contour_workspace_t::sub_488F00(const contour_t *pNew, contour_t **ppPos, contour_t **ppEnd)
{
  contour_t *p = *ppPos;
  contour_t *pEnd = *ppEnd;

  for (; p < pEnd; ++p)
  {
    if (p->pt.x < pNew->pt.x)
      continue;
    if (p->pt.x == pNew->pt.x &&
        nOrigin[(p->bFlags >> 2) & 3] + p->nEnd <
          nOrigin[pNew->bFlags & 3] + pNew->pt.y)
      continue;

    if (p->pt.x == pNew->pt.x &&
        nOrigin[(p->bFlags >> 2) & 3] + p->nEnd + 1 >=
          nOrigin[pNew->bFlags & 3] + pNew->pt.y &&
        nOrigin[p->bFlags & 3] + p->pt.y - 1 <=
          nOrigin[(pNew->bFlags >> 2) & 3] + pNew->nEnd)
    {
      if (p->pt.y > pNew->pt.y)
        p->pt.y = pNew->pt.y;
      if (p->nEnd < pNew->nEnd)
        p->nEnd = pNew->nEnd;
      *ppPos = p;
      return;
    }

    for (contour_t *q = pEnd; q > p; --q)
      *q = q[-1];
    break;
  }

  *p = *pNew;
  *ppPos = p;
  *ppEnd = pEnd + 1;
}

// Record the current scan point as a mark when it falls inside the rectangle,
// which is inclusive on all four sides, and reject an inverted rectangle.
//
// The negated chain of early returns is what keeps the three pushes in the
// prologue; the positive form shrink-wraps two of them into the guarded path
// and emits a second epilogue, 117 B against 110.
int contour_workspace_t::sub_489070(const rect16_t *pRect)
{
  if (pRect->left > pRect->right || pRect->top > pRect->bottom ||
      pRect->right < ptCur.x || pRect->left > ptCur.x ||
      pRect->bottom < ptCur.y || pRect->top > ptCur.y)
    return 0;
  // Hyperion: a point wrapped past a 1,024-tile map's edge is no mark (sub_488220)
  if (ptCur.x < -0x4000 || ptCur.y < -0x4000)
    return 0;

  marks[nMarks].pt = ptCur;
  marks[nMarks].bA = 0xFF;
  marks[nMarks].bB = 1;
  ++nMarks;
  bHit = 1;
  return 0;
}

// Push one contour onto the pending stack.
void contour_workspace_t::sub_4890E0(const rect16_t *pEntry)
{
  stack[nStack] = *pEntry;
  ++nStack;
}

// Restore the four list pointers and their counts from the saved copy that
// sub_488E10 parked at +0x174. The inherited header is the face every consumer
// sees, so a scan that rewrote it puts it back through here.
void contour_workspace_t::sub_489110()
{
  pLists[0] = pSavedLists[0];
  pLists[1] = pSavedLists[1];
  pLists[2] = pSavedLists[2];
  pLists[3] = pSavedLists[3];
  nCounts[0] = nSavedCounts[0];
  nCounts[1] = nSavedCounts[1];
  nCounts[2] = nSavedCounts[2];
  nCounts[3] = nSavedCounts[3];
}

// Seat the four search origins and their exclusive limits.
void contours_t::SetSearchParameters(WORD nA, WORD nB, WORD nC, WORD nD)
{
  nOrigin[0] = (short)nB;
  nOrigin[1] = (short)-nC;
  nOrigin[2] = (short)-nD;
  nOrigin[3] = (short)nA;
  nLimits[0] = (short)(nB + 1);
  nLimits[1] = (short)(-1 - nC);
  nLimits[2] = (short)(-1 - nD);
  nLimits[3] = (short)(nA + 1);
}

// Seat the search from a unit type's bounding box, or from a single coordinate
// when the high bit marks the argument as a literal distance.
void contours_t::SetSearchUnit(int nId)
{
  if ((WORD)nId < 0x8000)
  {
    SetSearch(&gUnitBoundBox[(WORD)nId]);
  }
  else
  {
    int n = nId & 0x7FFF;
    SetSearchParameters(n, n, n, n);
  }
}

// Seat the search from a rectangle. SetSearchParameters inlines here.
void contours_t::SetSearch(const rect16_t *pRect)
{
  SetSearchParameters(pRect->left, pRect->top, pRect->right, pRect->bottom);
}

// Seat the scan's target, its start point and its span. A null point or span
// is recorded as -32000 on every axis.
void contour_workspace_t::sub_4892D0(intptr_t nA, const point16_t *pPt, intptr_t nB, const rect16_t *pRect)
{
  nReserved3C = nA;
  if (pPt)
    ptCur = *pPt;
  else
  {
    ptCur.x = -32000;
    ptCur.y = -32000;
  }
  nReserved44 = nB;
  if (pRect)
  {
    rcSpan = *pRect;
  }
  else
  {
    rcSpan.left = -32000;
    rcSpan.top = -32000;
    rcSpan.right = -32000;
    rcSpan.bottom = -32000;
  }
}
