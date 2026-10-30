// sai_ContoursUtil.cpp -- the contour table searches and point tests. Bodies are
// in oracle VA order.

#include "../h/sai_ContoursUtil.h"
#include "../gen/globals117.h"

// Lower bound over one sorted contour list. The key is the coordinate doubled
// and biased by one so an edge sorts before or after its own column.
// __forceinline: 1.17 inlines it four times in sub_48B780, one more than MSVC's
// budget allows; the out-of-line copy is still emitted.
OC_EMIT __forceinline int find_contour(contour_t *pList, int nCount, int nCoord, int bFlag)
{
  if (nCount == 0)
    return 0;

  // Hyperion: PosS for 1.17's short, which the doubled far edge of a map of 512
  // tiles or more passes: 16,384 doubled wrapped to -32,768 and the search started in
  // the wrong place
  const int nKey = PosS(nCoord * 2 + (bFlag ? -1 : 1));
  unsigned int nLo = 0;
  unsigned int nHi = (unsigned int)nCount;
  while (nLo < nHi)
  {
    unsigned int nMid = (nLo + nHi) / 2;
    if (nKey < PosS(pList[nMid].pt.x * 2))
      nHi = nMid;
    else
      nLo = nMid + 1;
  }
  return (int)nHi;
}

// Whether the point (nA, nB) is clear of every contour: each of the four lists
// is entered at its own lower bound and walked outward, and the first contour
// whose span straddles the point answers no.
int contours_t::sub_48B780(short nA, short nB)
{
  contour_t *p = &pLists[0][find_contour(pLists[0], nCounts[0], nB, 1)];
  while (p > pLists[0])
  {
    --p;
    if (PosS(nOrigin[p->bC] + p->pt.x) < nB)
      break;
    if (PosS(nOrigin[1] + p->pt.y) <= nA &&
        PosS(nOrigin[3] + p->nEnd) >= nA)
      return 0;
  }

  p = &pLists[1][find_contour(pLists[1], nCounts[1], nA, 0)];
  while (p < &pLists[1][nCounts[1]])
  {
    if (PosS(nOrigin[p->bC] + p->pt.x) > nA)
      break;
    if (PosS(nOrigin[2] + p->pt.y) <= nB &&
        PosS(nOrigin[0] + p->nEnd) >= nB)
      return 0;
    ++p;
  }

  p = &pLists[2][find_contour(pLists[2], nCounts[2], nB, 0)];
  while (p < &pLists[2][nCounts[2]])
  {
    if (PosS(nOrigin[p->bC] + p->pt.x) > nB)
      break;
    if (PosS(nOrigin[1] + p->pt.y) <= nA &&
        PosS(nOrigin[3] + p->nEnd) >= nA)
      return 0;
    ++p;
  }

  p = &pLists[3][find_contour(pLists[3], nCounts[3], nA, 1)];
  while (p > pLists[3])
  {
    --p;
    if (PosS(nOrigin[p->bC] + p->pt.x) < nA)
      break;
    if (PosS(nOrigin[2] + p->pt.y) <= nB &&
        PosS(nOrigin[0] + p->nEnd) >= nB)
      return 0;
  }

  return 1;
}

// Shrink the rectangle until no contour reaches into it. One pass takes at most
// one contour from each of the four lists and clips whichever side is cheapest
// against the target point; the passes repeat while any list still had one.
// Lists 1 and 2's counts are read through references, as 1.17 loads each once.
void contours_t::sub_48B9E0(short nA, short nB, rect16_t *pRect)
{
  rect16_t rc = *pRect;
  contour_t *p0 = &pLists[0][find_contour(pLists[0], nCounts[0], nB - nOrigin[0], 0)];
  const contour_count_t &n1 = nCounts[1];
  const contour_count_t &n2 = nCounts[2];
  contour_t *p1 = &pLists[1][find_contour(pLists[1], n1, nA - nOrigin[1], 1)];
  contour_t *p2 = &pLists[2][find_contour(pLists[2], n2, nB - nOrigin[2], 1)];
  contour_t *p3 = &pLists[3][find_contour(pLists[3], nCounts[3], nA - nOrigin[3], 0)];
  int bChanged;
  int nX, nLo, nHi, d;

  do
  {
    bChanged = 0;

    if (p0 > pLists[0])
    {
      --p0;
      bChanged = 1;
      nX = PosS(nOrigin[p0->bC] + p0->pt.x);
      nLo = PosS(nOrigin[p0->bFlags & 3] + p0->pt.y);
      nHi = PosS(nOrigin[(p0->bFlags >> 2) & 3] + p0->nEnd);
      if (nX < rc.top)
        p0 = pLists[0];
      else if (nHi >= rc.left && nLo <= rc.right)
      {
        d = nB - nX;
        if (nA < nLo && d < nLo - nA)
        {
          rc.right = (short)(nLo - 1);
        }
        else if (nA > nHi && d < nA - nHi)
        {
          rc.left = (short)(nHi + 1);
        }
        else
        {
          rc.top = (short)(nX + 1);
        }
      }
    }

    if (p1 < &pLists[1][n1])
    {
      bChanged = 1;
      nX = PosS(nOrigin[p1->bC] + p1->pt.x);
      nLo = PosS(nOrigin[p1->bFlags & 3] + p1->pt.y);
      nHi = PosS(nOrigin[(p1->bFlags >> 2) & 3] + p1->nEnd);
      if (nX > rc.right)
        p1 = &pLists[1][n1];
      else if (nHi >= rc.top && nLo <= rc.bottom)
      {
        d = nX - nA;
        if (nB < nLo && d < nLo - nB)
        {
          rc.bottom = (short)(nLo - 1);
        }
        else if (nB > nHi && d < nB - nHi)
        {
          rc.top = (short)(nHi + 1);
        }
        else
        {
          rc.right = (short)(nX - 1);
        }
      }
      ++p1;
    }

    if (p2 < &pLists[2][n2])
    {
      bChanged = 1;
      nX = PosS(nOrigin[p2->bC] + p2->pt.x);
      nLo = PosS(nOrigin[p2->bFlags & 3] + p2->pt.y);
      nHi = PosS(nOrigin[(p2->bFlags >> 2) & 3] + p2->nEnd);
      if (nX > rc.bottom)
        p2 = &pLists[2][n2];
      else if (nHi >= rc.left && nLo <= rc.right)
      {
        d = nX - nB;
        if (nA < nLo && d < nLo - nA)
        {
          rc.right = (short)(nLo - 1);
        }
        else if (nA > nHi && d < nA - nHi)
        {
          rc.left = (short)(nHi + 1);
        }
        else
        {
          rc.bottom = (short)(nX - 1);
        }
      }
      ++p2;
    }

    if (p3 > pLists[3])
    {
      --p3;
      bChanged = 1;
      nX = PosS(nOrigin[p3->bC] + p3->pt.x);
      nLo = PosS(nOrigin[p3->bFlags & 3] + p3->pt.y);
      nHi = PosS(nOrigin[(p3->bFlags >> 2) & 3] + p3->nEnd);
      if (nX < rc.left)
        p3 = pLists[3];
      else if (nHi >= rc.top && nLo <= rc.bottom)
      {
        d = nA - nX;
        if (nB < nLo && d < nLo - nB)
        {
          rc.bottom = (short)(nLo - 1);
        }
        else if (nB > nHi && d < nB - nHi)
        {
          rc.top = (short)(nHi + 1);
        }
        else
        {
          rc.left = (short)(nX + 1);
        }
      }
    }

  } while (bChanged);

  *pRect = rc;
}

// Whether the rectangle (nX0, nY0)-(nX1, nY1) is clear of every contour facing
// it. Only the two lists the rectangle points at are searched, one per axis.
//
// The axes do not test the same thing: the x lists ask nLo < nMinX && nHi >
// nMaxX, the y lists nLo < nMaxY && nHi > nMinY. That asymmetry is 1.17's.
// Each list count is read twice, not held in a local.
int contours_t::sub_48BEC0(short nX0, short nY0, short nX1, short nY1)
{
  short nMinX, nMaxX, nMinY, nMaxY;
  BYTE bFlags;
  int nLo, nHi;  // Hyperion: a contour's span, PosS for 1.17's short

  if (nX0 < nX1)
  {
    nMinX = nX0;
    nMaxX = nX1;
  }
  else
  {
    nMinX = nX1;
    nMaxX = nX0;
  }

  if (nY0 < nY1)
  {
    nMinY = nY0;
    nMaxY = nY1;
  }
  else
  {
    nMinY = nY1;
    nMaxY = nY0;
  }

  if (nY1 < nY0)
  {
    contour_t *p = &pLists[0][find_contour(pLists[0], nCounts[0], nY0 - nOrigin[0], 0)];
    while (p > pLists[0])
    {
      --p;
      if (PosS(nOrigin[p->bC] + p->pt.x) < nY1)
        break;
      bFlags = p->bFlags;
      nLo = PosS(nOrigin[bFlags & 3] + p->pt.y);
      nHi = PosS(nOrigin[(bFlags >> 2) & 3] + p->nEnd);
      if (nLo <= nMaxX && nHi >= nMinX)
      {
        if (nLo < nMinX && nHi > nMaxX)
          return 0;
        if (!(bFlags & 0x10) || nLo <= nMinX || !(bFlags & 0x20) || nHi >= nMaxX)
          return 0;
      }
    }
  }
  else if (nY1 > nY0)
  {
    contour_t *p = &pLists[2][find_contour(pLists[2], nCounts[2], nY0 - nOrigin[2], 1)];
    contour_t *pEnd = &pLists[2][nCounts[2]];
    while (p < pEnd)
    {
      if (PosS(nOrigin[p->bC] + p->pt.x) > nY1)
        break;
      bFlags = p->bFlags;
      nLo = PosS(nOrigin[bFlags & 3] + p->pt.y);
      nHi = PosS(nOrigin[(bFlags >> 2) & 3] + p->nEnd);
      if (nLo <= nMaxX && nHi >= nMinX)
      {
        if (nLo < nMinX && nHi > nMaxX)
          return 0;
        if (!(bFlags & 0x10) || nLo <= nMinX || !(bFlags & 0x20) || nHi >= nMaxX)
          return 0;
      }
      ++p;
    }
  }

  if (nX1 < nX0)
  {
    contour_t *p = &pLists[3][find_contour(pLists[3], nCounts[3], nX0 - nOrigin[3], 0)];
    while (p > pLists[3])
    {
      --p;
      if (PosS(nOrigin[p->bC] + p->pt.x) < nX1)
        break;
      bFlags = p->bFlags;
      nLo = PosS(nOrigin[bFlags & 3] + p->pt.y);
      nHi = PosS(nOrigin[(bFlags >> 2) & 3] + p->nEnd);
      if (nLo <= nMaxY && nHi >= nMinY)
      {
        if (nLo < nMaxY && nHi > nMinY)
          return 0;
        if (!(bFlags & 0x10) || nLo <= nMinY || !(bFlags & 0x20) || nHi >= nMaxY)
          return 0;
      }
    }
  }
  else if (nX1 > nX0)
  {
    contour_t *p = &pLists[1][find_contour(pLists[1], nCounts[1], nX0 - nOrigin[1], 1)];
    contour_t *pEnd = &pLists[1][nCounts[1]];
    while (p < pEnd)
    {
      if (PosS(nOrigin[p->bC] + p->pt.x) > nX1)
        break;
      bFlags = p->bFlags;
      nLo = PosS(nOrigin[bFlags & 3] + p->pt.y);
      nHi = PosS(nOrigin[(bFlags >> 2) & 3] + p->nEnd);
      if (nLo <= nMaxY && nHi >= nMinY)
      {
        if (nLo < nMaxY && nHi > nMinY)
          return 0;
        if (!(bFlags & 0x10) || nLo <= nMinY || !(bFlags & 0x20) || nHi >= nMaxY)
          return 0;
      }
      ++p;
    }
  }

  return 1;
}

// Walk list 2 forward from the coordinate and return the first contour whose
// clamped span still overlaps [nB, nC]. find_contour inlines here.
contour_t *contours_t::sub_48C300(short nA, short nB, short nC, short nD)
{
  int n = nCounts[2];  // Hyperion: ints, as the counts are
  int nIdx = find_contour(pLists[2], n, nA - nOrigin[2], 1);
  if (nIdx == nCounts[2])
    return NULL;

  contour_t *p = &pLists[2][nIdx];
  contour_t *pEnd = &pLists[2][n];
  short nLimit = (short)(nD - nOrigin[2]);
  do
  {
    if (p->pt.x > nLimit)
      return NULL;

    int nLo = PosS(nOrigin[p->bFlags & 3] + p->pt.y);
    int nHi = PosS(nOrigin[(p->bFlags >> 2) & 3] + p->nEnd);
    if (nLo < nB)
      nLo = nB;
    if (nHi > nC)
      nHi = nC;
    if (nLo <= nHi)
      return p;
    ++p;
  } while (p < pEnd);
  return NULL;
}

// Walk list 3 backwards from the coordinate and return the first contour whose
// clamped span still overlaps [nB, nC]. find_contour inlines here. The list
// pointer is read into a local before the row limit, as 1.17 orders the loads.
contour_t *contours_t::sub_48C400(short nA, short nB, short nC, short nD)
{
  int nIdx = find_contour(pLists[3], nCounts[3], nA - nOrigin[3], 0);  // Hyperion: int
  if (nIdx == 0)
    return NULL;

  contour_t *pList = pLists[3];
  short nLimit = (short)(nD - nOrigin[3]);
  contour_t *p = &pList[nIdx - 1];
  while (p >= pList)
  {
    if (p->pt.x < nLimit)
      return NULL;

    int nLo = PosS(nOrigin[p->bFlags & 3] + p->pt.y);
    int nHi = PosS(nOrigin[(p->bFlags >> 2) & 3] + p->nEnd);
    if (nLo < nB)
      nLo = nB;
    if (nHi > nC)
      nHi = nC;
    if (nLo <= nHi)
      return p;
    --p;
  }
  return NULL;
}

// Walk list 1 forward from the coordinate and return the first contour whose
// clamped span still overlaps [nB, nC]. find_contour inlines here.
contour_t *contours_t::sub_48C4E0(short nA, short nB, short nC, short nD)
{
  int n = nCounts[1];  // Hyperion: ints, as the counts are
  int nIdx = find_contour(pLists[1], n, nA - nOrigin[1], 1);
  if (nIdx == nCounts[1])
    return NULL;

  contour_t *p = &pLists[1][nIdx];
  contour_t *pEnd = &pLists[1][n];
  short nLimit = (short)(nD - nOrigin[1]);
  do
  {
    if (p->pt.x > nLimit)
      return NULL;

    int nLo = PosS(nOrigin[p->bFlags & 3] + p->pt.y);
    int nHi = PosS(nOrigin[(p->bFlags >> 2) & 3] + p->nEnd);
    if (nLo < nB)
      nLo = nB;
    if (nHi > nC)
      nHi = nC;
    if (nLo <= nHi)
      return p;
    ++p;
  } while (p < pEnd);
  return NULL;
}

// Walk list 0 backwards from the coordinate and return the first contour whose
// clamped span still overlaps [nB, nC]. find_contour inlines here. The list
// pointer is read first, as in sub_48C400.
contour_t *contours_t::sub_48C5E0(short nA, short nB, short nC, short nD)
{
  int nIdx = find_contour(pLists[0], nCounts[0], nA - nOrigin[0], 0);  // Hyperion: int
  if (nIdx == 0)
    return NULL;

  contour_t *pList = pLists[0];
  short nLimit = (short)(nD - nOrigin[0]);
  contour_t *p = &pList[nIdx - 1];
  while (p >= pList)
  {
    if (p->pt.x < nLimit)
      return NULL;

    int nLo = PosS(nOrigin[p->bFlags & 3] + p->pt.y);
    int nHi = PosS(nOrigin[(p->bFlags >> 2) & 3] + p->nEnd);
    if (nLo < nB)
      nLo = nB;
    if (nHi > nC)
      nHi = nC;
    if (nLo <= nHi)
      return p;
    --p;
  }
  return NULL;
}
