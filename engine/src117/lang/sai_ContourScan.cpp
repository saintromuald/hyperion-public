// sai_ContourScan.cpp -- the contour scan sweeps and the rectangle normaliser.
// Bodies are in oracle VA order.

#include "../h/sai_ContourScan.h"
#include "../gen/globals117.h"
#include <string.h>  // memset

// Bias one contour's two coordinates and its end by the search limits their
// own flag bytes select. The limit array is read three times because each
// index is different; nothing is hoisted.
__declspec(noinline) void contours_t::sub_489320(contour_t *pEntry)
{
  pEntry->pt.x += nLimits[pEntry->bC];
  pEntry->pt.y += nLimits[pEntry->bFlags & 3];
  pEntry->nEnd += nLimits[(pEntry->bFlags >> 2) & 3];
}

// The leftward sweep, the mirror of sub_489880 on lists 0 and 3: walk the
// nearest contour above the target, then repeat along the left bound, and
// answer 1 as soon as a mark lands inside the search rectangle. The skip test
// comes first and both halves leave through `goto done`.
int contour_workspace_t::sub_489360()
{
  contour_t e;
  memset(&e, 0, sizeof e);  // Hyperion: the whole record, 16 bytes on x64
  point16_t ptT;
  point16_t ptB;
  point16_t ptR;
  contour_t *pA = pFound[0];
  contour_t *pB = pFound[3];
  ptT.x = nTargetX;
  ptT.y = nTargetY;
  ptR.x = nEdge[0];
  ptB.x = ptR.x;
  ptB.y = nEdge[1];
  rect16_t rc;
  contour_t *p;
  spos_t nLo, nHi, nKeep;  // Hyperion: positions
  BYTE bMask;

  if (ptT.x <= ptR.x)
  {
    if (ptT.y <= ptB.y)
      goto done;
  }
  else
  {
    nLo = ptB.y;
    nHi = ptB.y;
    if (pB)
    {
      nHi = PosS(nOrigin[pB->bFlags & 3] - 1 + pB->pt.y);
      if (nHi > ptB.y)
        nLo = nHi;
    }
    nKeep = ptR.x;
    p = sub_48C5E0(ptT.y - 1, ptB.x, ptT.x, nLo - 1);
    if (p)
    {
      e = *p;
      e.pt.x += nLimits[e.bC];
      e.pt.y += nLimits[e.bFlags & 3];
      e.nEnd += nLimits[(e.bFlags >> 2) & 3];
      if (ptT.x > e.nEnd)
        sub_488220(e.nEnd, e.pt.x, 0x11, 0x29);
      else
        sub_488220(ptT.x, e.pt.x, 0x22, 0x29);
      nLo = e.pt.x;
      nKeep = e.nEnd;
      while (ptR.x < e.pt.y)
      {
        sub_488220(e.pt.y, e.pt.x, 0x18, 0x29);
        --p;
        if (p < pLists[0])
          break;
        if (nOrigin[p->bC] + p->pt.x + 1 != nLo)
          break;
        if (nOrigin[(p->bFlags >> 2) & 3] + p->nEnd + 1 <= ptR.x)
          break;
        e = *p;
        sub_489320(&e);
        if (ptR.x <= e.nEnd)
          sub_488220(e.nEnd, e.pt.x, 0x11, 0x29);
      }
    }
    bMask = 0x9D;
    if (p)
    {
      if (ptR.x > e.pt.y)
        bMask = 0x84;
      else if (ptR.x == e.pt.y)
      {
        bMask = 0x9C;
        if (!(e.bFlags & 0x10))
          bMask = 0;
      }
    }
    if (pB)
    {
      bMask &= 0xFB;
      if (nHi < nLo)
        bMask &= 0x77;
    }
    if (nLo < ptT.y)
      sub_488220(ptB.x, ptT.y, 0xEE, 0x29);
    else
      bMask |= 0x44;
    sub_488220(ptB.x, nLo, bMask, 0x29);
    if (nLo == ptB.y)
      sub_488220(ptT.x, nLo, 0x7B, 0x29);

    if (ptR.x <= ptT.x)
    {
      rc.left = ptR.x;
      rc.right = ptT.x;
    }
    else
    {
      rc.left = ptT.x;
      rc.right = ptR.x;
    }
    if (nLo <= ptT.y)
    {
      rc.top = nLo;
      rc.bottom = ptT.y;
    }
    else
    {
      rc.top = ptT.y;
      rc.bottom = nLo;
    }
    if (sub_489070(&rc))
      return 1;
    sub_4890E0(&rc);
    ptT.y = nLo;
    ptR.x = nKeep;
    if (nLo <= ptB.y)
      goto done;
  }

  nLo = ptR.x;
  nHi = ptR.x;
  if (pA)
  {
    nHi = PosS(nOrigin[pA->bFlags & 3] - 1 + pA->pt.y);
    if (nHi > ptR.x)
      nLo = nHi;
  }
  p = sub_48C400(ptT.x - 1, ptB.y, ptT.y, nLo - 1);
  if (p)
  {
    e = *p;
    e.pt.x += nLimits[e.bC];
    e.pt.y += nLimits[e.bFlags & 3];
    e.nEnd += nLimits[(e.bFlags >> 2) & 3];
    if (e.pt.x > ptR.x)
      sub_488220(e.pt.x, ptT.y, 8, 0x2A);
    if (e.nEnd < ptT.y)
      sub_488220(e.pt.x, e.nEnd, 0x84, 0x2A);
    nLo = e.pt.x;
    while (e.pt.y > ptB.y)
    {
      sub_488220(e.pt.x, e.pt.y, 0x88, 0x2A);
      --p;
      if (p < pLists[3])
        break;
      if (nOrigin[p->bC] + p->pt.x + 1 != nLo)
        break;
      if (nOrigin[(p->bFlags >> 2) & 3] + p->nEnd + 1 <= ptB.y)
        break;
      e = *p;
      sub_489320(&e);
      if (ptB.y <= e.nEnd)
        sub_488220(e.pt.x, e.nEnd, 0x84, 0x2A);
    }
  }
  bMask = 0x9D;
  if (p)
  {
    if (ptB.y > e.pt.y)
      bMask = 0x11;
    else if (ptB.y == e.pt.y)
    {
      bMask = 0x99;
      if (!(e.bFlags & 0x10))
        bMask = 0;
    }
  }
  if (pA)
  {
    bMask &= 0xFE;
    if (nHi < nLo)
      bMask &= 0xE7;
  }
  if (nLo < ptT.x)
    sub_488220(ptT.x, ptB.y, 0x7B, 0x2A);
  else
    bMask |= 0x21;
  sub_488220(nLo, ptB.y, bMask, 0x2A);

  NormalizeRect(&rc, nLo, ptB.y, ptT.x, ptT.y);
  if (sub_489070(&rc))
    return 1;
  sub_4890E0(&rc);
done:
  return 0;
}

// The upward sweep. Walk the nearest contour above the target marking every
// cell, then repeat to the right, and answer 1 as soon as a mark lands inside
// the search rectangle. The masks are assigned then cleared, never written as a
// ternary, as in 1.17. The skip test comes first and both halves leave through
// `goto done`.
int contour_workspace_t::sub_489880()
{
  contour_t e;
  memset(&e, 0, sizeof e);  // Hyperion: the whole record, 16 bytes on x64
  point16_t ptT;
  point16_t ptB;
  point16_t ptR;
  contour_t *pUp = pFound[0];
  contour_t *pRight = pFound[1];
  ptT.x = nTargetX;
  ptT.y = nTargetY;
  ptR.x = nEdge[2];
  ptB.x = ptR.x;
  ptB.y = nEdge[1];
  rect16_t rc;
  contour_t *p;
  spos_t nLo, nHi, nKeep;  // Hyperion: positions
  BYTE bMask;

  if (ptT.x >= ptR.x)
  {
    if (ptT.y <= ptB.y)
      goto done;
  }
  else
  {
    nLo = ptB.y;
    nHi = ptB.y;
    if (pRight)
    {
      nHi = PosS(nOrigin[pRight->bFlags & 3] - 1 + pRight->pt.y);
      if (nHi > ptB.y)
        nLo = nHi;
    }
    nKeep = ptR.x;
    p = sub_48C5E0(ptT.y - 1, ptT.x, ptB.x, nLo - 1);
    bMask = 0x3B;
    if (p)
    {
      e = *p;
      e.pt.x += nLimits[e.bC];
      e.pt.y += nLimits[e.bFlags & 3];
      e.nEnd += nLimits[(e.bFlags >> 2) & 3];
      nLo = e.pt.x;
      if (ptR.x < e.nEnd)
        bMask = 0x22;
      else if (ptR.x == e.nEnd)
      {
        bMask = 0x33;
        if (!(e.bFlags & 0x20))
          bMask = 0;
      }
    }
    if (pRight)
    {
      bMask &= 0xFD;
      if (nHi < nLo)
        bMask &= 0xDE;
    }
    if (nLo < ptT.y)
      sub_488220(ptB.x, ptT.y, 0xE7, 0xB);
    else
      bMask |= 0x44;
    sub_488220(ptB.x, nLo, bMask, 0xB);
    if (nLo == ptB.y)
      sub_488220(ptT.x, nLo, 0xDD, 0xB);
    if (!p)
      goto seatUp;
    if (ptR.x > e.nEnd)
      sub_488220(e.nEnd, e.pt.x, 0x11, 0xB);
    while (e.pt.y >= ptT.x)
    {
      nKeep = e.pt.y;
      sub_488220(e.pt.y, e.pt.x, 0x18, 0xB);
      --p;
      if (p < pLists[0])
        goto seatUp;
      e = *p;
      sub_489320(&e);
      if (e.pt.x != nLo)
        goto seatUp;
      if (e.nEnd < ptT.x)
        goto seatUp;
      if (ptR.x >= e.nEnd)
        sub_488220(e.nEnd, e.pt.x, 0x11, 0xB);
    }
    nKeep = ptT.x;

  seatUp:
    rc.left = ptT.x;
    rc.right = ptR.x;
    if (nLo <= ptT.y)
    {
      rc.top = nLo;
      rc.bottom = ptT.y;
    }
    else
    {
      rc.top = ptT.y;
      rc.bottom = nLo;
    }
    if (sub_489070(&rc))
      return 1;
    sub_4890E0(&rc);
    ptT.y = nLo;
    ptR.x = nKeep;
    if (nLo <= ptB.y)
      goto done;
  }

  nLo = ptR.x;
  nHi = ptR.x;
  if (pUp)
  {
    nHi = PosS(nOrigin[(pUp->bFlags >> 2) & 3] + 1 + pUp->nEnd);
    if (nHi < ptR.x)
      nLo = nHi;
  }
  p = sub_48C4E0(ptT.x + 1, ptB.y, ptT.y, nLo + 1);
  bMask = 0x3B;
  if (p)
  {
    e = *p;
    e.pt.x += nLimits[e.bC];
    e.pt.y += nLimits[e.bFlags & 3];
    e.nEnd += nLimits[(e.bFlags >> 2) & 3];
    nLo = e.pt.x;
    if (ptB.y > e.pt.y)
      bMask = 0x18;
    else if (ptB.y == e.pt.y)
    {
      bMask = 0x39;
      if (!(e.bFlags & 0x10))
        bMask = 0;
    }
  }
  if (pUp)
  {
    bMask &= 0xF7;
    if (nHi > nLo)
      bMask &= 0xDE;
  }
  if (nLo > ptT.x)
    sub_488220(ptT.x, ptB.y, 0xDD, 0xC);
  else
    bMask |= 0x84;
  sub_488220(nLo, ptB.y, bMask, 0xC);
  if (nLo < ptR.x)
    sub_488220(nLo, ptT.y, 1, 0xC);
  if (p)
  {
    if (ptB.y < e.pt.y)
      sub_488220(e.pt.x, e.pt.y, 0x21, 0xC);
    while (e.nEnd <= ptT.y)
    {
      sub_488220(e.pt.x, e.nEnd, 0x22, 0xC);
      ++p;
      if (p >= &pLists[1][nCounts[1]])
        break;
      e = *p;
      sub_489320(&e);
      if (e.pt.x != nLo || e.pt.y > ptT.y)
      {
        sub_488220(nLo, ptT.y, 0x21, 0xC);
        break;
      }
      if (ptB.y <= e.pt.y)
        sub_488220(e.pt.x, e.pt.y, 0x21, 0xC);
    }
  }

  NormalizeRect(&rc, ptT.x, ptB.y, nLo, ptT.y);
  if (sub_489070(&rc))
    return 1;
  sub_4890E0(&rc);
done:
  return 0;
}

// The downward sweep, the mirror of sub_489880 on lists 2 and 3, with the same
// skip test first and `goto done` exits.
int contour_workspace_t::sub_489D50()
{
  contour_t e;
  memset(&e, 0, sizeof e);  // Hyperion: the whole record, 16 bytes on x64
  point16_t ptT;
  point16_t ptB;
  point16_t ptR;
  contour_t *pA = pFound[2];
  contour_t *pB = pFound[3];
  ptT.x = nTargetX;
  ptT.y = nTargetY;
  ptR.x = nEdge[0];
  ptB.x = ptR.x;
  ptB.y = nEdge[3];
  rect16_t rc;
  contour_t *p;
  spos_t nLo, nHi, nKeep;  // Hyperion: positions
  BYTE bMask;

  if (ptT.x <= ptR.x)
  {
    if (ptT.y >= ptB.y)
      goto done;
  }
  else
  {
    nLo = ptB.y;
    nHi = ptB.y;
    if (pB)
    {
      nHi = PosS(nOrigin[(pB->bFlags >> 2) & 3] + 1 + pB->nEnd);
      if (nHi < ptB.y)
        nLo = nHi;
    }
    nKeep = ptR.x;
    p = sub_48C300(ptT.y + 1, ptB.x, ptT.x, nLo + 1);
    bMask = 0xCE;
    if (p)
    {
      e = *p;
      e.pt.x += nLimits[e.bC];
      e.pt.y += nLimits[e.bFlags & 3];
      e.nEnd += nLimits[(e.bFlags >> 2) & 3];
      nLo = e.pt.x;
      if (ptR.x > e.pt.y)
        bMask = 0x88;
      else if (ptR.x == e.pt.y)
      {
        bMask = 0xCC;
        if (!(e.bFlags & 0x10))
          bMask = 0;
      }
    }
    if (pB)
    {
      bMask &= 0xF7;
      if (nHi > nLo)
        bMask &= 0x7B;
    }
    if (nLo > ptT.y)
      sub_488220(ptB.x, ptT.y, 0xBD, 0xB);
    else
      bMask |= 0x11;
    sub_488220(ptB.x, nLo, bMask, 0x1F);
    if (nLo == ptB.y)
      sub_488220(ptT.x, nLo, 0x77, 0xB);
    if (!p)
      goto seatDown;
    if (ptR.x < e.pt.y)
      sub_488220(e.pt.y, e.pt.x, 0x44, 0x1F);
    while (e.nEnd <= ptT.x)
    {
      nKeep = e.nEnd;
      sub_488220(e.nEnd, e.pt.x, 0x42, 0x1F);
      ++p;
      if (p >= &pLists[2][nCounts[2]])
        goto seatDown;
      e = *p;
      sub_489320(&e);
      if (e.pt.x != nLo)
        goto seatDown;
      if (e.pt.y > ptT.x)
        goto seatDown;
      sub_488220(e.pt.y, e.pt.x, 0x44, 0x1F);
    }
    sub_488220(ptT.x, e.pt.x, 0x21, 0x1F);
    nKeep = ptT.x;

  seatDown:
    if (ptR.x <= ptT.x)
    {
      rc.left = ptR.x;
      rc.right = ptT.x;
    }
    else
    {
      rc.left = ptT.x;
      rc.right = ptR.x;
    }
    if (ptT.y <= nLo)
    {
      rc.top = ptT.y;
      rc.bottom = nLo;
    }
    else
    {
      rc.top = nLo;
      rc.bottom = ptT.y;
    }
    if (sub_489070(&rc))
      return 1;
    sub_4890E0(&rc);
    ptT.y = nLo;
    ptR.x = nKeep;
    if (nLo >= ptB.y)
      goto done;
  }

  nLo = ptR.x;
  nHi = ptR.x;
  if (pA)
  {
    nHi = PosS(nOrigin[pA->bFlags & 3] - 1 + pA->pt.y);
    if (nHi > ptR.x)
      nLo = nHi;
  }
  p = sub_48C400(ptT.x - 1, ptT.y, ptB.y, nLo - 1);
  bMask = 0xCE;
  if (p)
  {
    e = *p;
    e.pt.x += nLimits[e.bC];
    e.pt.y += nLimits[e.bFlags & 3];
    e.nEnd += nLimits[(e.bFlags >> 2) & 3];
    nLo = e.pt.x;
    if (ptB.y < e.nEnd)
      bMask = 0x42;
    else if (ptB.y == e.nEnd)
    {
      bMask = 0xC6;
      if (!(e.bFlags & 0x20))
        bMask = 0;
    }
  }
  if (pA)
  {
    bMask &= 0xFD;
    if (nHi < nLo)
      bMask &= 0xBB;
  }
  if (nLo < ptT.x)
    sub_488220(ptT.x, ptB.y, 0x77, 0x20);
  else
    bMask |= 0x22;
  sub_488220(nLo, ptB.y, bMask, 0x20);
  if (nLo > ptR.x)
    sub_488220(nLo, ptT.y, 4, 0x20);
  if (p)
  {
    if (ptB.y > e.nEnd)
      sub_488220(e.pt.x, e.nEnd, 0x84, 0x20);
    while (e.pt.y >= ptT.y)
    {
      sub_488220(e.pt.x, e.pt.y, 0x88, 0x20);
      --p;
      if (p < pLists[3])
        break;
      e = *p;
      sub_489320(&e);
      if (e.pt.x != nLo || e.nEnd < ptT.y)
      {
        sub_488220(nLo, ptT.y, 0x84, 0xC);
        break;
      }
      if (ptB.y >= e.nEnd)
        sub_488220(e.pt.x, e.nEnd, 0x84, 0xC);
    }
  }

  NormalizeRect(&rc, nLo, ptT.y, ptT.x, ptB.y);
  if (sub_489070(&rc))
    return 1;
  sub_4890E0(&rc);
done:
  return 0;
}

// The rightward sweep, the mirror of sub_489880 on lists 2 and 1: walk the
// nearest contour below the target, then repeat along the right bound, and
// answer 1 as soon as a mark lands inside the search rectangle. The skip test
// comes first and both halves leave through `goto done`.
int contour_workspace_t::sub_48A240()
{
  contour_t e;
  memset(&e, 0, sizeof e);  // Hyperion: the whole record, 16 bytes on x64
  point16_t ptT;
  point16_t ptB;  // only x is read; a point, as in 1.17's frame
  point16_t ptR;
  contour_t *pA = pFound[2];
  contour_t *pB = pFound[1];
  ptT.x = nTargetX;
  ptT.y = nTargetY;
  ptB.x = nEdge[2];
  ptR.x = ptB.x;
  ptR.y = nEdge[3];
  rect16_t rc;
  contour_t *p;
  spos_t nLo, nHi, nKeep;  // Hyperion: positions
  BYTE bMask;

  if (ptT.x >= ptR.x)
  {
    if (ptT.y >= ptR.y)
      goto done;
  }
  else
  {
    nLo = ptR.y;
    nHi = ptR.y;
    if (pB)
    {
      nHi = PosS(nOrigin[(pB->bFlags >> 2) & 3] + 1 + pB->nEnd);
      if (nHi < ptR.y)
        nLo = nHi;
    }
    nKeep = ptR.x;
    p = sub_48C300(ptT.y + 1, ptT.x, ptB.x, nLo + 1);
    if (p)
    {
      e = *p;
      e.pt.x += nLimits[e.bC];
      e.pt.y += nLimits[e.bFlags & 3];
      e.nEnd += nLimits[(e.bFlags >> 2) & 3];
      if (ptT.x < e.pt.y)
        sub_488220(e.pt.y, e.pt.x, 0x44, 0x15);
      nLo = e.pt.x;
      nKeep = e.pt.y;
      while (e.nEnd < ptR.x)
      {
        sub_488220(e.nEnd, e.pt.x, 0x42, 0x15);
        ++p;
        if (p >= &pLists[2][nCounts[2]])
          break;
        if (nOrigin[p->bC] + p->pt.x - 1 != nLo)
          break;
        if (nOrigin[p->bFlags & 3] + p->pt.y - 1 >= ptR.x)
          break;
        e = *p;
        sub_489320(&e);
        if (ptR.x >= e.pt.y)
          sub_488220(e.pt.y, e.pt.x, 0x44, 0x15);
      }
    }
    bMask = 0x67;
    if (p)
    {
      if (ptR.x < e.nEnd)
        bMask = 0x21;
      else if (ptR.x == e.nEnd)
      {
        bMask = 0x63;
        if (!(e.bFlags & 0x20))
          bMask = 0;
      }
    }
    if (pB)
    {
      bMask &= 0xFE;
      if (nHi > nLo)
        bMask &= 0xDD;
    }
    if (nLo > ptT.y)
      sub_488220(ptB.x, ptT.y, 0xBB, 0x15);
    else
      bMask |= 0x18;
    sub_488220(ptB.x, nLo, bMask, 0x15);
    if (nLo == ptR.y)
      sub_488220(ptT.x, nLo, 0xDE, 0x15);

    rc.left = ptT.x;
    rc.right = ptR.x;
    if (ptT.y <= nLo)
    {
      rc.top = ptT.y;
      rc.bottom = nLo;
    }
    else
    {
      rc.top = nLo;
      rc.bottom = ptT.y;
    }
    if (sub_489070(&rc))
      return 1;
    sub_4890E0(&rc);
    ptT.y = nLo;
    ptR.x = nKeep;
    if (nLo >= ptR.y)
      goto done;
  }

  nLo = ptR.x;
  nHi = ptR.x;
  if (pA)
  {
    nHi = PosS(nOrigin[(pA->bFlags >> 2) & 3] + 1 + pA->nEnd);
    if (nHi < ptR.x)
      nLo = nHi;
  }
  p = sub_48C4E0(ptT.x + 1, ptT.y, ptR.y, nLo + 1);
  if (p)
  {
    e = *p;
    e.pt.x += nLimits[e.bC];
    e.pt.y += nLimits[e.bFlags & 3];
    e.nEnd += nLimits[(e.bFlags >> 2) & 3];
    if (e.pt.x < ptR.x)
      sub_488220(e.pt.x, ptT.y, 2, 0x16);
    if (e.pt.y > ptT.y)
      sub_488220(e.pt.x, e.pt.y, 0x21, 0x16);
    nLo = e.pt.x;
    while (e.nEnd < ptR.y)
    {
      sub_488220(e.pt.x, e.nEnd, 0x22, 0x16);
      ++p;
      if (p >= &pLists[1][nCounts[1]])
        break;
      if (nOrigin[p->bC] + p->pt.x - 1 != nLo)
        break;
      if (nOrigin[p->bFlags & 3] + p->pt.y - 1 >= ptR.y)
        break;
      e = *p;
      sub_489320(&e);
      if (ptT.y <= e.pt.y && e.pt.y <= ptR.y)
        sub_488220(e.pt.x, e.pt.y, 0x21, 0x16);
    }
  }
  bMask = 0x67;
  if (p)
  {
    if (ptR.y < e.nEnd)
      bMask = 0x44;
    else if (ptR.y == e.nEnd)
    {
      bMask = 0x66;
      if (!(e.bFlags & 0x20))
        bMask = 0;
    }
  }
  if (pA)
  {
    bMask &= 0xFB;
    if (nHi > nLo)
      bMask &= 0xBD;
  }
  if (nLo > ptT.x)
    sub_488220(ptT.x, ptR.y, 0xDE, 0x16);
  else
    bMask |= 0x88;
  sub_488220(nLo, ptR.y, bMask, 0x16);

  NormalizeRect(&rc, ptT.x, ptT.y, nLo, ptR.y);
  if (sub_489070(&rc))
    return 1;
  sub_4890E0(&rc);
done:
  return 0;
}

// Run the four directional sweeps the scan mask still asks for and stop on the
// first one that reports a hit. The last sweep is a tail call.
void contour_workspace_t::sub_48A750()
{
  if ((bScanMask & 1) && sub_489880())
    return;
  if ((bScanMask & 2) && sub_48A240())
    return;
  if ((bScanMask & 4) && sub_489D50())
    return;
  if (bScanMask & 8)
    sub_489360();
}

// Order the two coordinate pairs into a rectangle, smallest first on each axis.
// 1.17 loads the x pair, stores it, then loads the y pair, so the two if/else
// statements stay in that order and nothing is hoisted.
__declspec(noinline) void contour_workspace_t::NormalizeRect(rect16_t *pRect, short x0, short y0, short x1, short y1)
{
  if (x0 <= x1)
  {
    pRect->left = x0;
    pRect->right = x1;
  }
  else
  {
    pRect->left = x1;
    pRect->right = x0;
  }

  if (y0 <= y1)
  {
    pRect->top = y0;
    pRect->bottom = y1;
  }
  else
  {
    pRect->top = y1;
    pRect->bottom = y0;
  }
}
