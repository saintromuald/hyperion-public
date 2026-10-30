// sai_ContoursCreate.cpp -- AI pathing. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller in this file by
// /Ob2, which the oracle (WPO off) never does across TUs; the lever against it
// is __declspec(noinline) on the declaration.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>              // memset, memcpy -- the list reallocations
#include <stdlib.h>              // qsort -- contours_t::Init

#include "../h/sai_ContoursCreate.h"
#include "../h/Storm.h"          // SMemAlloc / SMemFree
#include "../h/gds_appcrit.h"    // appcritf
#include "../h/sai_PathCreate.h"  // ep_glob_t
#include "../gen/globals117.h"

unsigned g_nContourLists = 0;  // Hyperion

// Globals this TU reaches, dual-spelled, with one #ifdef per name rather than
// one over the block: a single guard would undeclare every name under it.

// gwCellFlagsEntries @0x666448 -- VF4 megatile count. Registered without a
// type; sub_48AD80 reads it `movzx eax, word ptr [0x666448]` and compares it
// 16-bit, so WORD.
#ifdef OC_DLL_MODE
#define gwCellFlagsEntries  (*reinterpret_cast<WORD *>(OC_VA(0x666448u)))
#else
extern WORD gwCellFlagsEntries;               // 0x666448
#endif

// sg_vf4_map_g @0x66644C -- the VF4 flag array, 16 WORDs per megatile.
// Registered without a type; sub_48AD80 walks it `add ecx, 20h` reading four
// WORDs per group.
#ifdef OC_DLL_MODE
#define sg_vf4_map_g        (*reinterpret_cast<const WORD **>(OC_VA(0x66644Cu)))
#else
extern const WORD *sg_vf4_map_g;              // 0x66644C
#endif

// gwMapWidth (0x5264B4) and gwMapHeight (0x5264B6) are emitted by
// src117/gen/globals117.h, so they are not re-spelled here.

// dword_69B55C @0x69B55C -- the 0x100000-byte passability matrix. Registered
// without a type; sub_48B1C0 stores SMemAlloc's result into it and every read
// of a cell is `mov dl, byte ptr [reg]` / `test dl, dl` / `js`, a signed byte.
#ifdef OC_DLL_MODE
#define dword_69B55C        (*reinterpret_cast<char **>(OC_VA(0x69B55Cu)))
#else
extern char *dword_69B55C;                    // 0x69B55C
#endif

// gfpCellMap @0x54323C -- the tile map, one WORD per tile. Unregistered.
// sub_48B1C0 reads it `movzx ecx, word ptr [edi] / and ecx, 7FFFh / add edi, 2h`.
#ifdef OC_DLL_MODE
#define gfpCellMap          (*reinterpret_cast<WORD **>(OC_VA(0x54323Cu)))
#else
extern WORD *gfpCellMap;                // 0x54323C
#endif

// The contour tracer's tables, read only by contours_t::Init. Registered, so
// gen emits them once Init is marked. The first two are indexed by a cell's
// low nibble, the corner tables by `face + (turn & 1) * 4`, the rest by a face.
#ifndef sgbContourFaces_g
#ifdef OC_DLL_MODE
#define sgbContourFaces_g   (reinterpret_cast<const char *>(OC_VA(0x508EE8u)))
#else
extern char sgbContourFaces_g[16] OC_UNALIGNED;            // 0x508EE8
#endif
#endif
#ifndef sgbContourTurns_g
#ifdef OC_DLL_MODE
#define sgbContourTurns_g   (reinterpret_cast<const BYTE *>(OC_VA(0x508EF8u)))
#else
extern BYTE sgbContourTurns_g[16] OC_UNALIGNED;            // 0x508EF8
#endif
#endif
#ifndef stru_508F08
#ifdef OC_DLL_MODE
#define stru_508F08         (reinterpret_cast<const contour_corner_t *>(OC_VA(0x508F08u)))
#else
extern contour_corner_t stru_508F08[8] OC_UNALIGNED;       // 0x508F08
#endif
#endif
#ifndef stru_508F28
#ifdef OC_DLL_MODE
#define stru_508F28         (reinterpret_cast<const contour_corner_t *>(OC_VA(0x508F28u)))
#else
extern contour_corner_t stru_508F28[8] OC_UNALIGNED;       // 0x508F28
#endif
#endif
#ifndef word_508F48
#ifdef OC_DLL_MODE
#define word_508F48         (reinterpret_cast<const short *>(OC_VA(0x508F48u)))
#else
extern short word_508F48[4];                  // 0x508F48
#endif
#endif
#ifndef stru_508F50
#ifdef OC_DLL_MODE
#define stru_508F50         (reinterpret_cast<const contour_hop_t *>(OC_VA(0x508F50u)))
#else
extern contour_hop_t stru_508F50[4];          // 0x508F50
#endif
#endif
#ifndef MapHops
#ifdef OC_DLL_MODE
#define MapHops             (reinterpret_cast<const contour_hop_t *>(OC_VA(0x508F58u)))
#else
extern contour_hop_t MapHops[4];              // 0x508F58
#endif
#endif

// The byte at contour_t +0x07 as Init writes it: two 2-bit directions and two
// turn flags; the top two bits are kept.
struct contour_bits_t {
  BYTE nLo : 2;
  BYTE nHi : 2;
  BYTE bA : 1;
  BYTE bB : 1;
  BYTE bRest : 2;
};

// The matrix is one byte per 1/8 tile in each axis, padded to a 0x400 stride.
// Both expressions are re-read from the globals at every use, which is what the
// oracle does and what makes this body its size.
#define OC_ROW_BYTES  ((gwMapWidth * 32) / 8)
#define OC_COL_BYTES  ((gwMapHeight * 32) / 8)

// The contour table hangs off the pathing glob, at the offset in
// sai_contourscreate_init.orig.asm `mov dword ptr [esi+97A1Ch], eax`, which is
// ep_glob_t::pContours.
#define OC_GLOB_CONTOURS  offsetof(ep_glob_t, pContours)


// The bodies, in oracle VA order.

// 0x48A7F0 (1353 B). Allocate the four contour lists, trace every contour of the
// passability matrix into them, and sort each list. Both corners are bare
// point16_t. Four statement orders place stores the way the oracle schedules
// them: --nX before ++nY, ptStart before the cell store, bNextMask before the
// loop's cell store, and pt0 before bTurn.
void contours_t::Init()
{
  BYTE *pMatrix = reinterpret_cast<BYTE *>(dword_69B55C);
  int nCol = 0;
  int nRow = 0;

  nCapacity[0] = 5000;
  nCapacity[1] = 5000;
  nCapacity[2] = 5000;
  nCapacity[3] = 5000;
  pLists[0] = (contour_t *)SMemAlloc(nCapacity[0] * sizeof(contour_t), "sai_ContoursCreate.cpp", 0xC6, 0);
  pLists[1] = (contour_t *)SMemAlloc(nCapacity[1] * sizeof(contour_t), "sai_ContoursCreate.cpp", 0xC7, 0);
  pLists[2] = (contour_t *)SMemAlloc(nCapacity[2] * sizeof(contour_t), "sai_ContoursCreate.cpp", 0xC8, 0);
  pLists[3] = (contour_t *)SMemAlloc(nCapacity[3] * sizeof(contour_t), "sai_ContoursCreate.cpp", 0xC9, 0);
  memset(pLists[0], 0, nCapacity[0] * sizeof(contour_t));
  memset(pLists[1], 0, nCapacity[1] * sizeof(contour_t));
  memset(pLists[2], 0, nCapacity[2] * sizeof(contour_t));
  memset(pLists[3], 0, nCapacity[3] * sizeof(contour_t));
  nCounts[0] = 0;
  nCounts[1] = 0;
  nCounts[2] = 0;
  nCounts[3] = 0;

  while (sub_48AF50(pMatrix, &nRow, &nCol))
  {
    int nY = nRow;
    int nX = nCol;
    BYTE *pCell = &pMatrix[(nY * EP_MROW) + nX];
    char nIdx = *pCell & 0xF;
    char bFace = sgbContourFaces_g[nIdx];
    BYTE bTurn = sgbContourTurns_g[nIdx];

    if (bFace && bFace == 2)
    {
      --nX;
      ++nY;
      pCell = &pMatrix[(nY * EP_MROW) + nX];
      nIdx = *pCell & 0xF;
      bFace = sgbContourFaces_g[nIdx];
      bTurn = sgbContourTurns_g[nIdx];
    }

    mpoint_t pt0;  // Hyperion: positions, 32,768 at a 1,024-tile map's far edge
    pt0.x = (nX << 3) + stru_508F08[bFace + (bTurn & 1) * 4].x;
    pt0.y = (nY << 3) + stru_508F08[bFace + (bTurn & 1) * 4].y;
    mpoint_t ptStart = pt0;
    *pCell ^= 1 << bFace;

    for (;;)
    {
      char bNext = (bFace + 1) & 3;
      // Hyperion: 1.17 reads the step from word_508F48, which holds dy * 0x400 + dx
      // for 1.17's row; this is the same step for the map's row
      int nStep = stru_508F50[bNext].dy * EP_MROW + stru_508F50[bNext].dx;
      int nDy = stru_508F50[bNext].dy;
      int nDx = stru_508F50[bNext].dx;
      BYTE bMask = 1 << bFace;
      BYTE bNextMask = 1 << bNext;
      *pCell ^= bMask;
      int nFace = bFace;

      BYTE bTurned;
      for (;;)
      {
        if (*pCell & bNextMask)
        {
          bTurned = 0;
          break;
        }
        if (!(pCell[nStep] & bMask))
        {
          bTurned = 1;
          break;
        }
        nY += nDy;
        pCell += nStep;
        nX += nDx;
        *pCell ^= bMask;
      }

      mpoint_t pt1;
      pt1.x = (nX << 3) + stru_508F28[nFace + (bTurned & 1) * 4].x;
      pt1.y = (nY << 3) + stru_508F28[nFace + (bTurned & 1) * 4].y;

      if (nCounts[0] == nCapacity[0] || nCounts[1] == nCapacity[1] ||
          nCounts[2] == nCapacity[2] || nCounts[3] == nCapacity[3])
        Grow();

      BYTE bA = (~(bFace << 1) & 3) ^ (bTurn << 1) ^ bFace;
      BYTE bB = (((bFace & 1) ^ bTurned) << 1) ^ bFace ^ 1;

      contour_t *p;
      contour_bits_t *pBits;
      switch (nFace)
      {
      case 0:
        p = &pLists[0][nCounts[0]++];
        p->pt.x = pt0.y;
        p->pt.y = pt0.x;
        p->nEnd = pt1.x;
        // Merged arms under the list-full test above.
        if (nCounts[0] == nCapacity[0])
          pBits = reinterpret_cast<contour_bits_t *>(&p->bFlags);
        else
          pBits = reinterpret_cast<contour_bits_t *>(&p->bFlags);
        pBits->nLo = bA;
        pBits->nHi = bB;
        pBits->bA = bTurn;
        pBits->bB = bTurned;
        break;
      case 1:
        p = &pLists[1][nCounts[1]++];
        p->pt.x = pt0.x;
        p->pt.y = pt0.y;
        p->nEnd = pt1.y;
        pBits = reinterpret_cast<contour_bits_t *>(&p->bFlags);
        pBits->nLo = bA;
        pBits->nHi = bB;
        pBits->bA = bTurn;
        pBits->bB = bTurned;
        break;
      case 2:
        p = &pLists[2][nCounts[2]++];
        p->pt.x = pt0.y;
        p->pt.y = pt1.x;
        p->nEnd = pt0.x;
        pBits = reinterpret_cast<contour_bits_t *>(&p->bFlags);
        pBits->nLo = bB;
        pBits->nHi = bA;
        pBits->bA = bTurned;
        pBits->bB = bTurn;
        break;
      default:
        p = &pLists[3][nCounts[3]++];
        p->pt.x = pt0.x;
        p->pt.y = pt1.y;
        p->nEnd = pt0.y;
        pBits = reinterpret_cast<contour_bits_t *>(&p->bFlags);
        pBits->nLo = bB;
        pBits->nHi = bA;
        pBits->bA = bTurned;
        pBits->bB = bTurn;
        break;
      }
      p->bC = bFace;

      if (bTurned == 0)
      {
        bFace = bNext;
      }
      else if (bTurned == 1)
      {
        nX += MapHops[nFace].dx;
        nY += MapHops[nFace].dy;
        pCell = pMatrix + (nY * EP_MROW) + nX;
        bFace = (bFace - 1) & 3;
      }
      else
      {
        appcritf("write me!");
      }

      pt0 = pt1;
      bTurn = bTurned;
      if (pt0.x == ptStart.x && pt0.y == ptStart.y)
        break;
    }

    *pCell ^= 1 << bFace;
  }

  SMemFree(pMatrix, "sai_ContoursCreate.cpp", 0x1B5, 0);
  Shrink();
  qsort(pLists[0], nCounts[0], sizeof(contour_t), contour_sort_cb);
  qsort(pLists[1], nCounts[1], sizeof(contour_t), contour_sort_cb);
  qsort(pLists[2], nCounts[2], sizeof(contour_t), contour_sort_cb);
  qsort(pLists[3], nCounts[3], sizeof(contour_t), contour_sort_cb);
  ++g_nContourLists;  // Hyperion
}

// 0x48AD40 (61 B). Order two contours by pt.x then pt.y. The two `return -1`
// exits are one tail-merged block in the oracle.
int __cdecl contour_sort_cb(const void *p1, const void *p2)
{
  const contour_t *a = (const contour_t *)p1;
  const contour_t *b = (const contour_t *)p2;

  if (a->pt.x < b->pt.x)
    return -1;
  if (a->pt.x > b->pt.x)
    return 1;
  if (a->pt.y < b->pt.y)
    return -1;
  return a->pt.y > b->pt.y;
}

// 0x48AD80 (300 B). Pack the VF4 map into one DWORD per four minitiles: bit 0
// of each flag word, complemented, at bits 7, 15, 23 and 31.
DWORD *sub_48AD80(void)
{
  DWORD *pOut = (DWORD *)SMemAlloc(gwCellFlagsEntries * 16,
                                   "sai_ContoursCreate.cpp", 0x30F, 0);
  const WORD *pFlags = sg_vf4_map_g;
  DWORD *p = pOut;
  int i;

  for (i = 0; i < gwCellFlagsEntries; i++)
  {
    // the post-increment is the lever: subscripting p keeps the cursor anchored
    // eight bytes late and lets MSVC sink its initialisation past the guard
    *p++ = ((((pFlags[3] << 8 | (pFlags[2] & 1)) << 8 | (pFlags[1] & 1)) << 8
           | (pFlags[0] & 1)) ^ 0xFF010101) << 7;
    *p++ = ((((pFlags[7] << 8 | (pFlags[6] & 1)) << 8 | (pFlags[5] & 1)) << 8
           | (pFlags[4] & 1)) ^ 0xFF010101) << 7;
    *p++ = ((((pFlags[11] << 8 | (pFlags[10] & 1)) << 8 | (pFlags[9] & 1)) << 8
           | (pFlags[8] & 1)) ^ 0xFF010101) << 7;
    *p++ = ((((pFlags[15] << 8 | (pFlags[14] & 1)) << 8 | (pFlags[13] & 1)) << 8
           | (pFlags[12] & 1)) ^ 0xFF010101) << 7;
    pFlags += 16;
  }
  return pOut;
}

// 0x48AEB0 (84 B). Release the four contour lists.
void sai_contours_free(contours_t *pContours)
{
  ++g_nContourLists;  // Hyperion
  SMemFree(pContours->pLists[0], "sai_ContoursCreate.cpp", 0x97, 0);
  SMemFree(pContours->pLists[1], "sai_ContoursCreate.cpp", 0x98, 0);
  SMemFree(pContours->pLists[2], "sai_ContoursCreate.cpp", 0x99, 0);
  SMemFree(pContours->pLists[3], "sai_ContoursCreate.cpp", 0x9A, 0);
}

// 0x48AF10 (62 B). Give the glob a fresh contour table.
void sai_contourscreate_init(void *pGlob)
{
  contours_t **ppContours = (contours_t **)((BYTE *)pGlob + OC_GLOB_CONTOURS);
  contours_t *p;

  *ppContours = (contours_t *)SMemAlloc(sizeof(contours_t),
                                        "sai_ContoursCreate.cpp", 0x81, 0);
  memset(*ppContours, 0, sizeof(contours_t));
  p = *ppContours;
  p->Reset();
  p->Init();
}

// 0x48AF50 (229 B). Advance (*pnRow, *pnCol) to the next non-zero cell of the
// packed matrix, wrapping past the last row. Returns 0 when the scan returns to
// its starting cell.
int sub_48AF50(BYTE *pMatrix, int *pnRow, int *pnCol)
{
  int nRowBytes = (gwMapWidth * 32) / 8;
  int nRowStep = (EP_MROW - nRowBytes) / 4;  // Hyperion: 0x400 in 1.17
  int nRow = *pnRow;
  int nCol = *pnCol;
  DWORD *pCell;
  DWORD *pStart;

  if (nCol >= nRowBytes)
  {
    nCol = 0;
    if (++nRow >= (gwMapHeight * 32) / 8)
      nRow = 0;
  }
  nCol &= ~3;
  pCell = (DWORD *)(pMatrix + (nRow * EP_MROW) + nCol);
  pStart = pCell;

  do
  {
    if (*pCell & 0x7F7F7F7F)
    {
      while (*(char *)pCell <= 0)
      {
        pCell = (DWORD *)((BYTE *)pCell + 1);
        nCol++;
      }
      *pnCol = nCol;
      *pnRow = nRow;
      return 1;
    }
    nCol += 4;
    pCell++;
    if (nCol == nRowBytes)
    {
      pCell += nRowStep;
      nCol = 0;
      if (++nRow >= (gwMapHeight * 32) / 8)
      {
        nRow = 0;
        pCell = (DWORD *)pMatrix;
      }
    }
  } while (pCell != pStart);

  return 0;
}

// 0x48B040 (375 B). Grow each of the four lists by 2500 entries.
void contours_t::Grow()
{
  contour_t *pNew;

  this->nCapacity[0] += 2500;
  pNew = (contour_t *)SMemAlloc(this->nCapacity[0] * sizeof(contour_t),
                                "sai_ContoursCreate.cpp", 0x1CD, 0);
  memset(pNew, 0, this->nCapacity[0] * sizeof(contour_t));
  memcpy(pNew, this->pLists[0], this->nCounts[0] * sizeof(contour_t));
  SMemFree(this->pLists[0], "sai_ContoursCreate.cpp", 0x1D0, 0);
  this->pLists[0] = pNew;

  this->nCapacity[1] += 2500;
  pNew = (contour_t *)SMemAlloc(this->nCapacity[1] * sizeof(contour_t),
                                "sai_ContoursCreate.cpp", 0x1D4, 0);
  memset(pNew, 0, this->nCapacity[1] * sizeof(contour_t));
  memcpy(pNew, this->pLists[1], this->nCounts[1] * sizeof(contour_t));
  SMemFree(this->pLists[1], "sai_ContoursCreate.cpp", 0x1D7, 0);
  this->pLists[1] = pNew;

  this->nCapacity[2] += 2500;
  pNew = (contour_t *)SMemAlloc(this->nCapacity[2] * sizeof(contour_t),
                                "sai_ContoursCreate.cpp", 0x1DB, 0);
  memset(pNew, 0, this->nCapacity[2] * sizeof(contour_t));
  memcpy(pNew, this->pLists[2], this->nCounts[2] * sizeof(contour_t));
  SMemFree(this->pLists[2], "sai_ContoursCreate.cpp", 0x1DE, 0);
  this->pLists[2] = pNew;

  this->nCapacity[3] += 2500;
  pNew = (contour_t *)SMemAlloc(this->nCapacity[3] * sizeof(contour_t),
                                "sai_ContoursCreate.cpp", 0x1E2, 0);
  memset(pNew, 0, this->nCapacity[3] * sizeof(contour_t));
  memcpy(pNew, this->pLists[3], this->nCounts[3] * sizeof(contour_t));
  SMemFree(this->pLists[3], "sai_ContoursCreate.cpp", 0x1E5, 0);
  this->pLists[3] = pNew;
}

// 0x48B1C0 (1074 B). Build the passability matrix: expand the tile map through
// sub_48AD80's packed table, then mark the border and interior edge bits.
char *sub_48B1C0()
{
  int nPad;
  char *pMatrix;
  const DWORD *pPacked;
  const WORD *pTiles;
  char *pOut;
  char *p;
  char *q;
  int i, j;

  // Hyperion: rows of EP_MROW bytes, 0x400 on 1.17's maps
  nPad = EP_MROW - OC_ROW_BYTES;
  pMatrix = (char *)SMemAlloc(EP_MROW * EP_MROW, "sai_ContoursCreate.cpp", 0x336, 0);
  dword_69B55C = pMatrix;
  pPacked = sub_48AD80();
  pTiles = gfpCellMap;
  pOut = pMatrix;

  for (i = 0; i < gwMapHeight; )
  {
    // the inner counter must be its own declaration; sharing j with the later
    // phases costs two byte deltas in the outer tail
    for (int n = 0; n < gwMapWidth; )
    {
      const DWORD *pSrc = (const DWORD *)((const BYTE *)pPacked + ((*pTiles & 0x7FFF) << 4));
      pTiles++;
      *(DWORD *)pOut = pSrc[0];
      *(DWORD *)(pOut + EP_MROW) = pSrc[1];
      *(DWORD *)(pOut + 2 * EP_MROW) = pSrc[2];
      *(DWORD *)(pOut + 3 * EP_MROW) = pSrc[3];
      // both steps are merged pairs; they set the outer loop's register shares
      if (n < gwMapWidth) { pOut += 4; } else { pOut += 4; }
      n++;
    }
    pOut += nPad + 3 * EP_MROW;
    if (i > 0) { i++; } else { i++; }
  }

  SMemFree((void *)pPacked, "sai_ContoursCreate.cpp", 0x368, 0);

  p = pMatrix + (OC_COL_BYTES - 8) * EP_MROW;
  for (i = OC_COL_BYTES - 8; i < OC_COL_BYTES; i++)
  {
    memset(p, 0x80, 20);
    for (j = OC_ROW_BYTES - 0x14; j < OC_ROW_BYTES; j++)
      p[j] = (char)0x80;
    if (i >= OC_COL_BYTES - 4)
      for (j = 0; j < OC_ROW_BYTES; j++)
        p[j] = (char)0x80;
    p += EP_MROW;
  }

  p = pMatrix;
  q = pMatrix - EP_MROW + OC_COL_BYTES * EP_MROW;
  for (i = OC_ROW_BYTES; i > 0; i--)
  {
    if (*p >= 0)
    {
      *p |= 1;
      if (p[EP_MROW] < 0)
        *p |= 4;
      if (i == OC_ROW_BYTES || p[-1] < 0)
        *p |= 8;
      if (i == 1 || p[1] < 0)
        *p |= 2;
    }
    if (*q >= 0)
    {
      *q |= 4;
      if (q[-EP_MROW] < 0)
        *q |= 1;
      if (i == OC_ROW_BYTES || q[-1] < 0)
        *q |= 8;
      if (i == 1 || q[1] < 0)
        *q |= 2;
    }
    p++;
    q++;
  }

  p = pMatrix;
  q = pMatrix - 1 + OC_ROW_BYTES;
  for (i = OC_COL_BYTES; i > 0; i--)
  {
    if (*p >= 0)
    {
      *p |= 8;
      if (p[1] < 0)
        *p |= 2;
      if (i == OC_COL_BYTES || p[-EP_MROW] < 0)
        *p |= 1;
      if (i == 1 || p[EP_MROW] < 0)
        *p |= 4;
    }
    if (*q >= 0)
    {
      *q |= 2;
      if (q[-1] < 0)
        *q |= 8;
      if (i == OC_COL_BYTES || q[-EP_MROW] < 0)
        *q |= 1;
      if (i == 1 || q[EP_MROW] < 0)
        *q |= 4;
    }
    p += EP_MROW;
    q += EP_MROW;
  }

  p = pMatrix + EP_MROW + 1;
  i = OC_COL_BYTES - 2;
  if (i > 0)
  {
    int nStep = nPad + 2;
    do
    {
      for (j = OC_ROW_BYTES - 2; j > 0; j--)
      {
        char c = *p;
        if (c >= 0)
        {
          if (p[-EP_MROW] < 0)
            c |= 1;
          if (p[EP_MROW] < 0)
            c |= 4;
          if (p[-1] < 0)
            c |= 8;
          if (p[1] < 0)
            c |= 2;
          *p = c;
        }
        p++;
      }
      p += nStep;
    } while (--i > 0);
  }
  return pMatrix;
}

// 0x48B600 (284 B). Shrink each of the four lists to the entries in use.
void contours_t::Shrink()
{
  contour_t *pNew;

  pNew = (contour_t *)SMemAlloc(this->nCounts[0] * sizeof(contour_t),
                                "sai_ContoursCreate.cpp", 0x1EC, 0);
  memcpy(pNew, this->pLists[0], this->nCounts[0] * sizeof(contour_t));
  SMemFree(this->pLists[0], "sai_ContoursCreate.cpp", 0x1EE, 0);
  this->pLists[0] = pNew;

  pNew = (contour_t *)SMemAlloc(this->nCounts[1] * sizeof(contour_t),
                                "sai_ContoursCreate.cpp", 0x1F1, 0);
  memcpy(pNew, this->pLists[1], this->nCounts[1] * sizeof(contour_t));
  SMemFree(this->pLists[1], "sai_ContoursCreate.cpp", 0x1F3, 0);
  this->pLists[1] = pNew;

  pNew = (contour_t *)SMemAlloc(this->nCounts[2] * sizeof(contour_t),
                                "sai_ContoursCreate.cpp", 0x1F6, 0);
  memcpy(pNew, this->pLists[2], this->nCounts[2] * sizeof(contour_t));
  SMemFree(this->pLists[2], "sai_ContoursCreate.cpp", 0x1F8, 0);
  this->pLists[2] = pNew;

  pNew = (contour_t *)SMemAlloc(this->nCounts[3] * sizeof(contour_t),
                                "sai_ContoursCreate.cpp", 0x1FB, 0);
  memcpy(pNew, this->pLists[3], this->nCounts[3] * sizeof(contour_t));
  SMemFree(this->pLists[3], "sai_ContoursCreate.cpp", 0x1FD, 0);
  this->pLists[3] = pNew;
}
