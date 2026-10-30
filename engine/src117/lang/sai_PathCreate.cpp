// sai_PathCreate.cpp -- AI pathing. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller in this file by
// /Ob2, which the oracle (WPO off) never does across TUs; the lever against it
// is __declspec(noinline) on the declaration.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>
#include "../h/hxrules.h"  // Hyperion: HxRules, the game's options

#include <intrin.h>
#include "../h/port_pathstats.h"  // Hyperion: the build's time
#include "../h/sai_PathCreate.h"
#include "../h/sai_ContoursCreate.h"
#include "../h/Storm.h"
#include "../h/sai.h"
#include "../h/net_error.h"       // err_popup
#include "../gen/globals117.h"

// Globals this TU reaches, dual-spelled, with one #ifdef per name rather than
// one over the block: a single guard would undeclare every name under it.

// epGlob -- void * @0x69B584 (SAI_PathCreate.cpp). Already registered in
// re/globals117.json, so gen emits it as soon as a marked row lists it; the
// outer #ifndef is what lets both spellings coexist until then.
#ifndef epGlob
#ifdef OC_DLL_MODE
#define epGlob (*reinterpret_cast<void **>(OC_VA(0x69B584u)))
#else
extern void *epGlob;
#endif
#endif

// gMapSize -- WORD[2] @0x5264B4: tile width at +0, height at +2. One object,
// not the two WORDs re/globals117.json registers: sub_490F50 reads the width
// with `mov esi, dword ptr [5264B4h]`, which MSVC emits only for a WORD inside
// an aggregate (a standalone `extern WORD` gives `mov ax, word ptr [g]`).
// TU-local because retyping the registry entry moves bytes in landed TUs.
#ifdef OC_DLL_MODE
#define gMapSize (reinterpret_cast<WORD *>(OC_VA(0x5264B4u)))
#else
extern WORD gMapSize[2];
#endif

// dword_69B55C @0x69B55C -- the passability matrix, four minitile bytes per
// tile across and 0x400 per minitile row. Spelled as sai_ContoursCreate.cpp
// spells it; a cell is blocked when its signed byte is negative.
#ifdef OC_DLL_MODE
#define dword_69B55C (*reinterpret_cast<char **>(OC_VA(0x69B55Cu)))
#else
extern char *dword_69B55C;
#endif

// gfpCellMap @0x54323C -- the tile map, one WORD per tile. Unregistered;
// spelled as sai_ContoursCreate.cpp spells it.
#ifdef OC_DLL_MODE
#define gfpCellMap (*reinterpret_cast<WORD **>(OC_VA(0x54323Cu)))
#else
extern WORD *gfpCellMap;
#endif

// sg_vf4_map_g @0x66644C -- the VF4 flag array, 16 WORDs per megatile.
#ifdef OC_DLL_MODE
#define sg_vf4_map_g (*reinterpret_cast<const WORD **>(OC_VA(0x66644Cu)))
#else
extern const WORD *sg_vf4_map_g;
#endif

int PathJoinWide(ep_glob_t *pGlob, int nTarget);
int PathCarveWide(ep_glob_t *pGlob, const ep_area_t *pArea, int nRegions);

// Hyperion: the pathing map's row and region cap for the map being played
// (h/maplimits.h): 1.17's for a map within 256 tiles a side.
int g_epStride = MAP_SIDE_117;
int g_epRegionCap = EP_REGIONS_117;
int g_epBoxScale = 1;
int g_epAreaNodes = EP_AREA_NODES_117;
int g_epAreaQueue = EP_AREA_QUEUE_117;
int g_epLocalNodes = EP_LOCAL_NODES_117;
int g_epLocalQueue = EP_LOCAL_QUEUE_117;

void PathSetMapLimits() {
  bool bWide = gMapSize[0] > MAP_SIDE_117 || gMapSize[1] > MAP_SIDE_117;
  g_epStride = bWide ? MAP_SIDE_MAX : MAP_SIDE_117;
  g_epRegionCap = bWide ? EP_REGIONS_MAX : EP_REGIONS_117;
  g_epBoxScale = bWide ? 2 : 1;
  // a game whose options keep 1.17's budgets on a larger map too, for pathbench's
  // comparison (HYPERION_PATH_BUDGET=117 as it starts; recorded in its replay)
  bool bLarge = bWide && HxRules() && !(g_hxRuleOptions & HX_OPT_PATH_BUDGET_117);
  g_epAreaNodes = bLarge ? EP_AREA_NODES_MAX : EP_AREA_NODES_117;
  g_epAreaQueue = bLarge ? EP_AREA_QUEUE_MAX : EP_AREA_QUEUE_117;
  g_epLocalNodes = bLarge ? EP_LOCAL_NODES_MAX : EP_LOCAL_NODES_117;
  g_epLocalQueue = bLarge ? EP_LOCAL_QUEUE_MAX : EP_LOCAL_QUEUE_117;
}

// The bodies, in oracle VA order.

// sgbErrObstructions_g @0x69B580 -- set when the region table overflows.
#ifndef sgbErrObstructions_g
#ifdef OC_DLL_MODE
#define sgbErrObstructions_g (*reinterpret_cast<DWORD *>(OC_VA(0x69B580u)))
#else
extern DWORD sgbErrObstructions_g;
#endif
#endif

// 0x48F060. Builds the region graph over one map area.
int sub_48F060(ep_glob_t *pGlob, const ep_area_t *pArea) {
  int nRegions = pGlob->nRegions;

  // Hyperion: a map past 256 tiles a side is carved and merged by PathCarveWide
  //
  if (g_epBoxScale > 1) {
    if (!PathCarveWide(pGlob, pArea, nRegions))
      return 0;
  } else {
    if (!sub_48F8E0(pGlob, *pArea))
      return 0;
    sub_48F870(pGlob, nRegions);
    EPCalc_JoinAreas(pGlob, pArea);
  }
  sub_48FEB0(pGlob);
  sub_48F870(pGlob, nRegions);
  pGlob->pScratch = pGlob->bScratch;
  sub_48F1B0(pGlob);
  memset(&pGlob->aRegions[pGlob->nRegions], 0,
         (EP_REGION_RECS - pGlob->nRegions) * sizeof(ep_region_t));  // Hyperion: 0x1388
  return 1;
}

// 0x48F0E0. Counts each region's distinct neighbouring areas; the scan stops
// at the fifth.
// The fifth area jumps past the `< 2` clamp, which sits inside the scan block.
void sub_48F0E0(ep_glob_t *pGlob) {
  ep_region_t *pRegion, *pNeighbour;
  WORD *pPath, *pEnd, *pScan;
  WORD aSeen[5];
  WORD wArea;
  unsigned int nKept;
  int i, nFirst, nLeft;

  i = pGlob->nRegions;
  if (i == 0)
    return;

  for (int j = 0; i--; j++) {
    pRegion = &pGlob->aRegions[j];
    nKept = 0;
    nFirst = pRegion->bPathFirst;
    nLeft = pRegion->bPathCount - nFirst;
    if (nLeft != 0) {
      pPath = pRegion->pPaths + nFirst;
      pEnd = aSeen;
      do {
        nLeft--;
        pNeighbour = &((ep_glob_t *)epGlob)->aRegions[*pPath];
        if (pNeighbour->wTiles >= 4) {
          wArea = pNeighbour->wArea;
          for (pScan = aSeen; pScan < pEnd; pScan++) {
            if (wArea == *pScan)
              break;
          }
          if (pScan >= pEnd) {
            nKept++;
            if (nKept >= 5)
              goto done;
            *pEnd = wArea;
            pEnd++;
          }
        }
        pPath++;
      } while (nLeft != 0);
      if (nKept < 2)
        nKept = 0;
    }
done:
    pRegion->bAreaLinks = (BYTE)nKept;
  }
}

// One tile split, six bytes: the bit mask of the tile's open minitiles and the
// two regions it lies between.
struct ep_split_t {
  /*0x00*/ WORD wMask;
  /*0x02*/ WORD wA;
  /*0x04*/ WORD wB;
};

static_assert(sizeof(ep_split_t) == 6, "sub_48F1B0: add esi, 6h");
static_assert(offsetof(ep_split_t, wMask) == 0, "sub_48F1B0: mov word ptr [esi], di");
static_assert(offsetof(ep_split_t, wA) == 2, "sub_48F1B0: mov word ptr [esi+2h], cx");
static_assert(offsetof(ep_split_t, wB) == 4, "sub_48F1B0: mov word ptr [esi+4h], bx");

// 0x48F1B0. Splits every mixed tile between its own region and the neighbour
// its border minitiles lean towards, and records the split. The mask stays a
// WORD and the equal-regions arm is an if/else, which is what lets MSVC share
// the cmove tail with the else-if arm. Each corner count is a char local
// defined where its first sum needs it, and each take stores the index before
// the count; both orders are the oracle's.
void sub_48F1B0(ep_glob_t *pGlob) {
  DWORD *pSquare = gpSquareMap;
  const WORD *pCell = gfpCellMap;
  ep_split_t *pBase = (ep_split_t *)((ep_glob_t *)epGlob)->bScratch;
  ep_split_t *pOut = pBase;
  WORD *pMap = ((ep_glob_t *)epGlob)->wMap;
  int x, y, i, n, nBest, iBest;
  WORD aNear[8];
  char anCount[8];
  const WORD *pVf4;
  const char *m;
  WORD wA, wB, w;
  WORD wMask;

  for (y = 0; y < gwMapHeight; y++) {
    for (x = 0; x < gwMapWidth; x++, pMap++, pSquare++, pCell++) {
      if (*pSquare & 0x20000000) {
        wMask = 0;
        pVf4 = &sg_vf4_map_g[(*pCell & 0x7FFF) * 16];
        i = 16;
        do {
          i--;
          wMask = (WORD)((wMask << 1) | (~pVf4[i] & 1));
        } while (i != 0);
        wA = *pMap;
        wB = wA;
        sub_490A60((ep_glob_t *)epGlob, x, y, aNear);
        m = &dword_69B55C[(y * EP_MROW + x) * 4];  // Hyperion: (y << 10) in 1.17
        *(DWORD *)anCount = 0;
        *(DWORD *)&anCount[4] = 0;
        if (!IsInaccessibleRegion(wA)) {
          char cb = m[3] < 0;
          char ca = m[0] < 0;
          anCount[0] = ca + (m[1] < 0) + (m[2] < 0) + cb;
          char cc = m[3 * EP_MROW] < 0;
          anCount[1] = ca + (m[EP_MROW] < 0) + (m[2 * EP_MROW] < 0) + cc;
          char cd = m[3 * EP_MROW + 3] < 0;
          anCount[2] = cb + (m[EP_MROW + 3] < 0) + (m[2 * EP_MROW + 3] < 0) + cd;
          anCount[3] = cc + (m[3 * EP_MROW + 1] < 0) + (m[3 * EP_MROW + 2] < 0) + cd;
          anCount[4] = ca;
          anCount[5] = cb;
          anCount[6] = cc;
          anCount[7] = cd;
          nBest = 0;
          iBest = 0;
          for (i = 8; i != 0; ) {
            i--;
            if (anCount[i] <= nBest)
              continue;
            w = aNear[i];
            if (w == wA)
              continue;
            if (w >= 0x2000) {
              switch (i) {
              case 0: n = (m[-EP_MROW] < 0) + (m[-EP_MROW + 1] < 0) + (m[-EP_MROW + 2] < 0) + (m[-EP_MROW + 3] < 0); break;
              case 1: n = (m[-1] < 0) + (m[EP_MROW - 1] < 0) + (m[2 * EP_MROW - 1] < 0) + (m[3 * EP_MROW - 1] < 0); break;
              case 2: n = (m[4] < 0) + (m[EP_MROW + 4] < 0) + (m[2 * EP_MROW + 4] < 0) + (m[3 * EP_MROW + 4] < 0); break;
              case 3: n = (m[4 * EP_MROW] < 0) + (m[4 * EP_MROW + 1] < 0) + (m[4 * EP_MROW + 2] < 0) + (m[4 * EP_MROW + 3] < 0); break;
              case 4: n = m[-EP_MROW - 1] < 0; break;
              case 5: n = m[-EP_MROW + 4] < 0; break;
              case 6: n = m[4 * EP_MROW - 1] < 0; break;
              case 7: n = m[4 * EP_MROW + 4] < 0; break;
              default: continue;
              }
            } else {
              n = w < g_epRegionCap && ((ep_glob_t *)epGlob)->aRegions[w].wGroup == 0x1FFD;
            }
            if (n) {
              iBest = i;
              nBest = anCount[i];
            }
          }
          if (nBest) {
            wB = aNear[iBest];
            if (wB >= 0x2000)
              wB = pBase[wB - 0x2000].wB;
          }
        } else {
          anCount[0] = (m[1] >= 0) + (m[2] >= 0) + (m[0] >= 0) + (m[3] >= 0);
          anCount[1] = (m[EP_MROW] >= 0) + (m[2 * EP_MROW] >= 0) + (m[0] >= 0) + (m[3 * EP_MROW] >= 0);
          anCount[2] = (m[EP_MROW + 3] >= 0) + (m[2 * EP_MROW + 3] >= 0) + (m[3] >= 0) + (m[3 * EP_MROW + 3] >= 0);
          anCount[3] = (m[3 * EP_MROW + 1] >= 0) + (m[3 * EP_MROW + 2] >= 0) + (m[3 * EP_MROW] >= 0) + (m[3 * EP_MROW + 3] >= 0);
          nBest = 0;
          iBest = 0;
          for (i = 4; i != 0; ) {
            i--;
            if (anCount[i] <= nBest)
              continue;
            w = aNear[i];
            if (w == wA)
              continue;
            if (w >= 0x2000) {
              switch (i) {
              case 0: n = (m[-EP_MROW] >= 0) + (m[-EP_MROW + 1] >= 0) + (m[-EP_MROW + 2] >= 0) + (m[-EP_MROW + 3] >= 0); break;
              case 1: n = (m[-1] >= 0) + (m[EP_MROW - 1] >= 0) + (m[2 * EP_MROW - 1] >= 0) + (m[3 * EP_MROW - 1] >= 0); break;
              case 2: n = (m[4] >= 0) + (m[EP_MROW + 4] >= 0) + (m[2 * EP_MROW + 4] >= 0) + (m[3 * EP_MROW + 4] >= 0); break;
              case 3: n = (m[4 * EP_MROW] >= 0) + (m[4 * EP_MROW + 1] >= 0) + (m[4 * EP_MROW + 2] >= 0) + (m[4 * EP_MROW + 3] >= 0); break;
              default: continue;
              }
              if (!n)
                continue;
            } else if (w >= g_epRegionCap || ((ep_glob_t *)epGlob)->aRegions[w].wGroup == 0x1FFD) {
              continue;
            }
            iBest = i;
            nBest = anCount[i];
          }
          if (nBest) {
            wA = aNear[iBest];
            if (wA >= 0x2000)
              wA = pBase[wA - 0x2000].wA;
          }
        }
        if (wA == wB) {
          if (((ep_glob_t *)epGlob)->aRegions[wA].wGroup == 0x1FFD)
            wMask = 0xFFFF;
          else
            wMask = 0;
        } else if (((ep_glob_t *)epGlob)->aRegions[wA].wGroup == 0x1FFD
                 && ((ep_glob_t *)epGlob)->aRegions[wB].wGroup == 0x1FFD)
          wMask = 0xFFFF;
        // Hyperion: past the split table a tile stays whole, on its own region;
        // 1.17 had no bound
        if (pOut - pBase >= EP_SPLIT_RECS) {
          *pMap = wA;
          continue;
        }
        pOut->wMask = wMask;
        pOut->wA = wA;
        pOut->wB = wB;
        *pMap = (WORD)(pOut - pBase + 0x2000);
        pOut++;
      }
    }
    pMap += EP_STRIDE - gwMapWidth;
  }
  ((ep_glob_t *)epGlob)->pScratch = pOut;
}

// 0x48F870. Re-runs the tile pass, then lifts every degenerate centre.
void sub_48F870(ep_glob_t *pGlob, int nRegions) {
  ep_region_t *pRegion;
  int i;

  pGlob->pPathPool = pGlob->wPathPool;
  sub_490330(pGlob);
  sub_4902E0(pGlob);

  pRegion = &pGlob->aRegions[nRegions];
  // Hyperion: a centre still in tiles is under 0x100 on 1.17's maps; a lifted one
  // is at least 0x1000, which is the test on a map past 256 tiles
  int nTiled = g_epBoxScale > 1 ? 0x1000 : 0x100;
  for (i = nRegions; i < pGlob->nRegions; i++) {
    if (pRegion->nX < nTiled)
      pRegion->nX = (pRegion->nX << 13) + 0x1000;
    if (pRegion->nY < nTiled)
      pRegion->nY = (pRegion->nY << 13) + 0x1000;
    pRegion++;
  }

  sub_4901F0(pGlob);
}

// 0x48F8E0. Carves the area into regions: repeatedly scans from the last
// cut for the largest box of like tiles, stamps it as a region and grows it by
// one tile each way. Fails when the region table fills.
int sub_48F8E0(ep_glob_t *pGlob, ep_area_t area) {
  sgbErrObstructions_g = 0;
  rect16_t rcBox;
  rect16_t rcBest;
  int nStartY = area.ptMin.y;
  int nFirst = pGlob->nRegions;
  int nStartX = area.ptMin.x;
  WORD wFloor = (WORD)g_epRegionCap;  // Hyperion: 0x1388 in 1.17
  int bGrown = 0;
  WORD wBest = 0;
  int nThreshold = 0x38 * g_epBoxScale * g_epBoxScale;  // Hyperion: 0x38 in 1.17
  int nSkip = area.ptMin.x - area.ptMax.x + EP_STRIDE;  // Hyperion: + 0x100 in 1.17

  for (;;) {
    WORD *pStart;
    WORD *p;
    *(DWORD *)&rcBest.left = 0;
    *(DWORD *)&rcBest.right = 0;
    int nBestArea = 0;
    int nTried = 0;
    int x = nStartX;
    int y = nStartY;
    if (x >= area.ptMax.x) {
      x = area.ptMin.x;
      y++;
      if (y >= area.ptMax.y)
        y = area.ptMin.y;
    }
    p = &pGlob->wMap[y * EP_STRIDE + x];
    pStart = p;
    do {
      if (*p >= wFloor) {
        int nArea;

        sub_490B80(pGlob, x, y, &rcBox, *p);
        nTried++;
        nArea = (rcBox.bottom - rcBox.top) * (rcBox.right - rcBox.left);
        if (nBestArea < nArea) {
          rcBest = rcBox;
          nBestArea = nArea;
          wBest = *p;
        }
        if (nBestArea >= nThreshold || nTried > 25) {
          nStartY = rcBest.top;
          nStartX = rcBest.right;
          goto stamp;
        }
        p += rcBox.right - x - 1;
        x = rcBox.right - 1;
      }
      x++;
      p++;
      if (x >= area.ptMax.x) {
        y++;
        x = area.ptMin.x;
        p += nSkip;
        if (y >= area.ptMax.y) {
          y = area.ptMin.y;
          p = &pGlob->wMap[y * EP_STRIDE + x];
        }
      }
    } while (p != pStart);
stamp:
    if (pGlob->nRegions >= g_epRegionCap) {  // Hyperion: 0x1388 in 1.17
      if (g_epBoxScale > 1)
        return 2;  // Hyperion: PathCarveWide merges and carves on
      sgbErrObstructions_g = 1;
      if (dword_69B55C != NULL) {
        SMemFree(dword_69B55C, "sai_PathCreate.cpp", 0x372, 0);
        dword_69B55C = NULL;
      }
      err_popup(0x61, (intptr_t)"The map could not be loaded because it had too many obstructions. Try widening corridors and reducing the number of small nooks and crannies to correct the problem.\n\n", 0, 0, 1);
      return 0;
    }
    int nWidth = rcBest.right - rcBest.left;
    nThreshold = (rcBest.bottom - rcBest.top) * nWidth;
    if (nThreshold == 0)
      return 1;
    sub_490180(pGlob, &rcBest, pGlob->nRegions);
    pGlob->aRegions[pGlob->nRegions].nX = nWidth / 2 + rcBest.left;
    pGlob->aRegions[pGlob->nRegions].nY = (rcBest.bottom - rcBest.top) / 2 + rcBest.top;
    if (rcBest.left > 0)
      rcBest.left--;
    if (rcBest.right < gMapSize[0]) {
      // Merged arms under the test on the line above.
      if (rcBest.left > 0)
        rcBest.right++;
      else
        rcBest.right++;
    }
    if (rcBest.top > 0)
      rcBest.top--;
    if (rcBest.bottom < gMapSize[1])
      rcBest.bottom++;
    sub_48FFF0(pGlob, &rcBest, pGlob->nRegions, wBest);
    pGlob->aRegions[pGlob->nRegions].rcBounds = rcBest;  // Hyperion: the box by field (stage E)
    pGlob->aRegions[pGlob->nRegions].wGroup = wBest;
    pGlob->nRegions++;
    if (nThreshold <= 6 && !bGrown) {
      bGrown = 1;
      for (int i = nFirst; i < pGlob->nRegions; i++) {
        ep_region_t *pRegion = &pGlob->aRegions[i];

        if (pRegion->nMinX > 0)
          pRegion->nMinX--;
        if (pRegion->nMaxX < gMapSize[0])
          pRegion->nMaxX++;
        if (pRegion->nMinY > 0)
          pRegion->nMinY--;
        if (pRegion->nMaxY < gMapSize[1])
          pRegion->nMaxY++;
        // Hyperion: the box by field (stage E); while regions are made it holds tiles
        const rect16_t rcBounds = pRegion->rcBounds;
        sub_48FFF0(pGlob, &rcBounds, i, pRegion->wGroup);
      }
    }
  }
}

// 0x48FC80. Merges every small region into its smallest matching neighbour,
// repeating while 2500 or more regions are still in use.
void EPCalc_JoinAreas(ep_glob_t *pGlob, const ep_area_t *pArea) {
  ep_glob_t *g = pGlob;
  ep_region_t *pRegion, *pNext, *pBest;
  WORD *pPath;
  WORD wTiles;
  int nLimit, nUsed, iBest;
  int i, j, k, n, x, y;

  nLimit = 4;
  do {
    nLimit += 2;
    n = g->nRegions;
    pRegion = g->aRegions + n - 1;
    i = n - 1;
    while (i >= 0) {
      if (pRegion->wTiles != 0 && pRegion->wArea < 0x4000
          && pRegion->wTiles < nLimit && pRegion->bPathCount != 0) {
        pBest = 0;
        k = pRegion->bPathCount;
        if (k > 0) {
          pPath = pRegion->pPaths;
          do {
            pNext = &g->aRegions[*pPath];
            wTiles = pNext->wTiles;
            if (wTiles != 0 && pNext->wArea < 0x4000
                && pRegion->wGroup == pNext->wGroup) {
              if (pBest == 0 || wTiles < pBest->wTiles)
                pBest = pNext;
            }
            pPath++;
          } while (--k != 0);
        }

        if (pBest != 0) {
          iBest = (int)(pBest - g->aRegions);
          for (y = pRegion->nMinY / 32; y < pRegion->nMaxY / 32; y++) {
            for (x = pRegion->nMinX / 32; x < pRegion->nMaxX / 32; x++) {
              if (g->wMap[y * EP_STRIDE + x] == i)
                g->wMap[y * EP_STRIDE + x] = (WORD)iBest;
            }
          }
          pBest->wTiles += pRegion->wTiles;
          pRegion->wTiles = 0;
          pRegion->wGroup = 0x1FFF;
          if (pRegion->nMinY < pBest->nMinY)
            pBest->nMinY = pRegion->nMinY;
          if (pRegion->nMaxY > pBest->nMaxY)
            pBest->nMaxY = pRegion->nMaxY;
          if (pRegion->nMinX < pBest->nMinX)
            pBest->nMinX = pRegion->nMinX;
          if (pRegion->nMaxX > pBest->nMaxX)
            pBest->nMaxX = pRegion->nMaxX;
        }
      }
      pRegion--;
      i--;
    }

    nUsed = 0;
    pRegion = g->aRegions;
    j = g->nRegions;
    if (j > 0) {
      do {
        if (pRegion->wTiles > 0)
          nUsed++;
        pRegion++;
      } while (--j != 0);
    }
  } while (nUsed >= AI_AREAS_117);
}

// Hyperion: EPCalc_JoinAreas for a map past 256 tiles a side. Such a map
// carves thousands more regions than 1.17's, and the merge must still end under 2,500,
// the AI's areas (TAI_Captain.cpp). 1.17's passes keep each region's neighbours as they
// were carved, so once a region's neighbours of its own group have merged away it has
// none left, and on a large map the loop can stop making progress and never end. This
// one makes the same passes, rebuilds the neighbours whenever a pass merges nothing,
// keeps a region's tile count within its WORD, and returns 0 when the regions cannot
// come under nTarget.
int PathJoinWide(ep_glob_t *pGlob, int nTarget) {
  ep_glob_t *g = pGlob;
  int nLimit = 4;
  bool bRebuilt = false;

  for (;;) {
    int nMerged = 0;
    nLimit += 2;
    for (int i = g->nRegions - 1; i >= 0; i--) {
      ep_region_t *pRegion = &g->aRegions[i];
      if (pRegion->wTiles == 0 || pRegion->wArea >= 0x4000 || pRegion->wTiles >= nLimit
          || pRegion->bPathCount == 0)
        continue;
      ep_region_t *pBest = 0;
      WORD *pPath = pRegion->pPaths;
      for (int k = pRegion->bPathCount; k > 0; k--, pPath++) {
        if (*pPath >= g->nRegions)
          continue;
        ep_region_t *pNext = &g->aRegions[*pPath];
        if (pNext->wTiles != 0 && pNext->wArea < 0x4000 && pRegion->wGroup == pNext->wGroup
            && pNext->wTiles + pRegion->wTiles <= 0xFFFF
            && (pBest == 0 || pNext->wTiles < pBest->wTiles))
          pBest = pNext;
      }
      if (pBest == 0)
        continue;
      int iBest = (int)(pBest - g->aRegions);
      for (int y = pRegion->nMinY / 32; y < pRegion->nMaxY / 32; y++) {
        for (int x = pRegion->nMinX / 32; x < pRegion->nMaxX / 32; x++) {
          if (g->wMap[y * EP_STRIDE + x] == i)
            g->wMap[y * EP_STRIDE + x] = (WORD)iBest;
        }
      }
      pBest->wTiles += pRegion->wTiles;
      pRegion->wTiles = 0;
      pRegion->wGroup = 0x1FFF;
      if (pRegion->nMinY < pBest->nMinY)
        pBest->nMinY = pRegion->nMinY;
      if (pRegion->nMaxY > pBest->nMaxY)
        pBest->nMaxY = pRegion->nMaxY;
      if (pRegion->nMinX < pBest->nMinX)
        pBest->nMinX = pRegion->nMinX;
      if (pRegion->nMaxX > pBest->nMaxX)
        pBest->nMaxX = pRegion->nMaxX;
      nMerged++;
    }

    int nUsed = 0;
    for (int j = 0; j < g->nRegions; j++) {
      if (g->aRegions[j].wTiles > 0)
        nUsed++;
    }
    if (nUsed < nTarget)
      return 1;
    if (nMerged != 0) {
      bRebuilt = false;
      continue;
    }
    // nothing merged under this limit: on to the next region size that could
    int nNext = 0x10000;
    for (int j = 0; j < g->nRegions; j++) {
      const ep_region_t *p = &g->aRegions[j];
      if (p->wTiles >= nLimit && p->wTiles < nNext && p->wArea < 0x4000 && p->bPathCount != 0)
        nNext = p->wTiles;
    }
    if (nNext <= 0xFFFF) {
      nLimit = nNext - 1;
      continue;
    }
    if (bRebuilt)
      return 0;
    // the neighbours as the map now stands, and the limit from the start again
    sub_490330(g);
    sub_4902E0(g);
    bRebuilt = true;
    nLimit = 4;
  }
}

// Hyperion: the carve for a map past 256 tiles a side. Such a map carves
// more raw regions than a WORD region id holds below the group codes (BGH tiled 4 x 4
// needs some 9,500 against 8,176), so the carve stops when the table fills, the regions
// so far merge to half of it and are renumbered, and the carve goes on over the tiles
// left. The last merge brings them under 2,500, the AI's areas.
int PathCarveWide(ep_glob_t *pGlob, const ep_area_t *pArea, int nRegions) {
  int r;
  while ((r = sub_48F8E0(pGlob, *pArea)) == 2) {
    sub_48F870(pGlob, nRegions);
    PathJoinWide(pGlob, g_epRegionCap / 2);
    sub_48FEB0(pGlob);
    if (pGlob->nRegions >= g_epRegionCap - 64)
      break;  // the merge freed too little to go on
  }
  if (r == 1) {
    sub_48F870(pGlob, nRegions);
    if (PathJoinWide(pGlob, AI_AREAS_117))
      return 1;
  }
  if (r != 0) {
    sgbErrObstructions_g = 1;
    if (dword_69B55C != NULL) {
      SMemFree(dword_69B55C, "sai_PathCreate.cpp", 0x372, 0);
      dword_69B55C = NULL;
    }
    err_popup(0x61, (intptr_t)"The map could not be loaded because it had too many obstructions. Try widening corridors and reducing the number of small nooks and crannies to correct the problem.\n\n", 0, 0, 1);
  }
  return 0;
}

// 0x48FEB0. Drops the empty regions, renumbers the survivors and the map.
// The map size is held in a two-WORD struct local: MSVC keeps a struct field
// 16-bit (`mov si, word ptr [g]` / `movzx`), where a WORD local folds to `movzx esi`.
void sub_48FEB0(ep_glob_t *pGlob) {
  WORD aRemap[EP_REGION_RECS];  // Hyperion: [0x1388] in 1.17
  ep_region_t *pSrc, *pDst;
  WORD *pMap, *pPath;
  int i, j, k, x, y, nKept;
  struct { WORD w, h; } sz;

  // Hyperion: on a map past 256 tiles a side this also runs between the stages of
  // the carve, while tiles are still uncarved; their group codes, and paths to them,
  // are past the regions and stay as they are
  const int nOld = pGlob->nRegions;
  const bool bWide = g_epBoxScale > 1;
  nKept = 0;
  pDst = pGlob->aRegions;
  for (i = 0; i < pGlob->nRegions; i++) {
    aRemap[i] = (WORD)nKept;
    if (pDst[i].wTiles > 0)
      nKept++;
  }

  pSrc = pGlob->aRegions;
  for (j = 0; j < pGlob->nRegions; j++) {
    if (pSrc->wTiles != 0) {
      pPath = pSrc->pPaths;
      for (k = pSrc->bPathCount; k > 0; k--) {
        if (!bWide || *pPath < nOld)
          *pPath = aRemap[*pPath];
        pPath++;
      }
      *pDst = *pSrc;
      pDst++;
    }
    pSrc++;
  }

  pGlob->nRegions = (short)nKept;

  pMap = pGlob->wMap;
  for (y = 0; y < gMapSize[1]; y++) {
    sz.w = gMapSize[0];
    for (x = 0; x < sz.w; x++) {
      if (!bWide || *pMap < nOld)
        *pMap = aRemap[*pMap];
      pMap++;
      sz.w = gMapSize[0];
    }
    sz.h = gMapSize[1];
    pMap += EP_STRIDE - sz.w;
  }
}

// 0x48FFF0. Grows nFill by one tile into the wMatch cells, twice.
void sub_48FFF0(ep_glob_t *pGlob, const rect16_t *pBox, int nFill, WORD wMatch) {
  WORD *p;
  int x, y;

  for (y = pBox->top; y < pBox->bottom; y++) {
    for (x = pBox->left; x < pBox->right; x++) {
      p = &pGlob->wMap[y * EP_STRIDE + x];
      if (*p == wMatch
          && ((y > 0 && nFill == p[-EP_STRIDE])
              || (x > 0 && nFill == p[-1])
              || (x + 1 < gMapSize[0] && nFill == p[1])
              || (y + 1 < gMapSize[1] && nFill == p[EP_STRIDE])))
        *p = (WORD)nFill;
    }
  }

  for (y = pBox->top; y < pBox->bottom; y++) {
    for (x = pBox->left; x < pBox->right; x++) {
      p = &pGlob->wMap[y * EP_STRIDE + x];
      if (*p == wMatch
          && ((y > 0 && nFill == p[-EP_STRIDE])
              || (x > 0 && nFill == p[-1])
              || (x + 1 < gMapSize[0] && nFill == p[1])
              || (y + 1 < gMapSize[1] && nFill == p[EP_STRIDE])))
        *p = (WORD)nFill;
    }
  }
}

// 0x490180. Paints nValue over the box's tiles.
void sub_490180(ep_glob_t *pGlob, const rect16_t *pBox, int nValue) {
  int x, y;

  for (y = pBox->top; y < pBox->bottom; y++) {
    for (x = pBox->left; x < pBox->right; x++)
      pGlob->wMap[y * EP_STRIDE + x] = (WORD)nValue;
  }
}

// 0x4901F0. Numbers the connected areas; sub_490E20 inlines into the seed call.
void sub_4901F0(ep_glob_t *pGlob) {
  ep_region_t *pRegion;
  int nArea, n;

  n = pGlob->nRegions;
  pRegion = pGlob->aRegions;
  while (n > 0) {
    if (pRegion->wArea < 0x4000)
      pRegion->wArea = 0;
    n--;
    pRegion++;
  }

  pRegion = pGlob->aRegions;
  nArea = 1;
  n = pGlob->nRegions;
  while (n > 0) {
    if (pRegion->wArea == 0 && pRegion->wTiles > 0) {
      sub_490E20(pGlob, pRegion, nArea);
      nArea++;
    }
    n--;
    pRegion++;
  }
}

// 0x4902E0. Compacts every non-empty region's path list.
void sub_4902E0(ep_glob_t *pGlob) {
  ep_region_t *pRegion;
  int i;

  pGlob->pPathPool = pGlob->wPathPool;
  pRegion = pGlob->aRegions;
  for (i = 0; i < pGlob->nRegions; i++) {
    if (pRegion->wTiles != 0) {
      sub_490500(pGlob, i, pRegion);
      sub_4910C0(pGlob, pRegion);
    }
    pRegion++;
  }
}

// 0x490E20. Floods nArea through the region's path neighbours.
void sub_490E20(ep_glob_t *pGlob, ep_region_t *pRegion, int nArea) {
  ep_region_t *pNext;
  WORD *pPath;
  int nLeft;

  pRegion->wArea = (WORD)nArea;
  if (pRegion->wGroup == 0x1FFD) {
    nLeft = pRegion->bPathCount - pRegion->bPathFirst;
    pPath = pRegion->pPaths + pRegion->bPathFirst;
  } else {
    nLeft = pRegion->bPathFirst;
    pPath = pRegion->pPaths;
  }

  while (nLeft > 0) {
    pNext = &pGlob->aRegions[*pPath];
    if (pNext->wArea == 0)
      sub_490E20(pGlob, pNext, nArea);
    nLeft--;
    pPath++;
  }
}

// 0x490330. Recounts every region's tiles and bounding box from the map.
// The pixel bounds are x * 32 and y * 32, so their counters start after each
// loop's entry test.
// Hyperion: 1.17 casts each bound to short; PosS does as much in a 1.17 game, and
// in a Hyperion game keeps the right and bottom edges of a 1,024-tile map, 32,768, which
// the short made -32,768 and which left the edge regions' neighbours one-way.
void sub_490330(ep_glob_t *pGlob) {
  ep_region_t *pRegion;
  WORD *pMap;
  WORD r;
  int i, x, y;

  pRegion = pGlob->aRegions;
  for (i = 0; i < pGlob->nRegions; i++) {
    pRegion[i].nMinX = pRegion[i].nMinY = 0x7FFF;
    pRegion[i].wTiles = 0;
    pRegion[i].nMaxX = pRegion[i].nMaxY = -1;
  }

  pMap = pGlob->wMap;
  for (y = 0; y < gMapSize[1]; y++, pMap += EP_STRIDE - gMapSize[0]) {
    for (x = 0; x < gMapSize[0]; x++, pMap++) {
      r = *pMap;
      if (r < g_epRegionCap) {  // Hyperion: 0x1388 in 1.17
        ep_region_t *pR = &pGlob->aRegions[r];
        pGlob->aRegions[r].wTiles++;
        if (pR->nMinY > y * 32)
          pR->nMinY = PosS(y * 32);
        if (pR->nMinX > x * 32)
          pR->nMinX = PosS(x * 32);
        if (pR->nMaxY < y * 32 + 32)
          pR->nMaxY = PosS((y + 1) * 32);
        if (pR->nMaxX < x * 32 + 32)
          pR->nMaxX = PosS((x + 1) * 32);
      }
    }
  }

  for (i = 0; i < pGlob->nRegions; i++) {
    if (pRegion->wTiles == 0 && pRegion->wGroup != 0x1FFF)
      pRegion->wGroup = 0x1FFF;
    pRegion++;
  }
}

// 0x490500. Collects the distinct regions next to every tile of one region.
// A diagonal neighbour counts only when both corner blocks and one of the two
// side blocks are walkable, unless either region is a 0x1FFD group.
// The barrier and the pair under nCount > 10 keep the matrix pointer in a register
// where MSVC would fold it into every compare.
#define mini_block_open(p) ((p)[0] >= 0 && (p)[1] >= 0 && (p)[EP_MROW] >= 0 && (p)[EP_MROW + 1] >= 0)

void sub_490500(ep_glob_t *pGlob, int nRegion, ep_region_t *pRegion) {
  WORD *pPool = pGlob->pPathPool;
  int nCount = 0;
  int nDirect = (pRegion->wGroup == 0x1FFD) * 4 + 4;
  mrect_t  rc = pRegion->rcBounds;  // Hyperion: the box by field (stage E)
  WORD aNb[8];
  int x, y, i, j;

  rc.left /= 32;
  rc.right /= 32;
  rc.top /= 32;
  rc.bottom /= 32;
  for (y = rc.top; y < rc.bottom; y++) {
    for (x = rc.left; x < rc.right; x++) {
      if (pGlob->wMap[y * EP_STRIDE + x] != nRegion)
        continue;
      sub_490A60(pGlob, x, y, aNb);
      for (i = 0; i < 8; i++) {
        int nNb = aNb[i];
        if (nNb == 0x1FFF || nNb == nRegion)
          continue;
        if (i >= nDirect && pGlob->aRegions[nNb].wGroup != 0x1FFD) {
          int nIdx = y * EP_MROW + x;  // Hyperion: y * 1024 in 1.17
          // Decision 16: the only shape found that matches in both forms.
          _ReadWriteBarrier();
          const char *m;
          if (nCount > 10) { m = dword_69B55C + nIdx * 4; } else { m = dword_69B55C + nIdx * 4; }
          switch (i) {
          case 4:
            if (!(mini_block_open(m) && mini_block_open(m - (2 * EP_MROW + 2)) && (mini_block_open(m - 2) || mini_block_open(m - (2 * EP_MROW)))))
              continue;
            break;
          case 5:
            if (!(mini_block_open(m + 2) && mini_block_open(m - (2 * EP_MROW - 4)) && (mini_block_open(m + 4) || mini_block_open(m - (2 * EP_MROW - 2)))))
              continue;
            break;
          case 6:
            if (!(mini_block_open(m + (2 * EP_MROW)) && mini_block_open(m + (4 * EP_MROW - 2)) && (mini_block_open(m + (2 * EP_MROW - 2)) || mini_block_open(m + (4 * EP_MROW)))))
              continue;
            break;
          case 7:
            if (!(mini_block_open(m + (2 * EP_MROW + 2)) && mini_block_open(m + (4 * EP_MROW + 4)) && (mini_block_open(m + (2 * EP_MROW + 4)) || mini_block_open(m + (4 * EP_MROW + 2)))))
              continue;
            break;
          default:
            continue;
          }
        }
        BOOL bNew = TRUE;
        for (j = 0; j < nCount; j++) {
          if (pPool[j] == nNb)
            bNew = FALSE;
        }
        if (bNew)
          pPool[nCount++] = (WORD)nNb;
      }
    }
  }

  if (nCount > 10) {
    pRegion->pPaths = pPool;
    pGlob->pPathPool += nCount;
  } else {
    pRegion->pPaths = pRegion->wInlinePaths;
    for (j = 0; j < nCount; j++)
      pRegion->wInlinePaths[j] = pPool[j];
    for (; j < 10; j++)
      pRegion->wInlinePaths[j] = 0x1FFF;
  }
  pRegion->bPathCount = (BYTE)nCount;
  pRegion->nExtraPaths = 0;
}

// 0x490A60. Reads the eight map neighbours of (x, y), 0x1FFF off the edge.
void sub_490A60(ep_glob_t *pGlob, int x, int y, WORD *pOut) {
  WORD *p = &pGlob->wMap[y * EP_STRIDE + x];

  pOut[0] = (y > 0)                ? p[-EP_STRIDE] : 0x1FFF;
  pOut[1] = (x > 0)                ? p[-1]   : 0x1FFF;
  pOut[2] = (x + 1 < gMapSize[0])   ? p[1]    : 0x1FFF;
  pOut[3] = (y + 1 < gMapSize[1])  ? p[EP_STRIDE]  : 0x1FFF;
  pOut[4] = (y > 0 && x > 0)                       ? p[-EP_STRIDE - 1] : 0x1FFF;
  pOut[5] = (y > 0 && x + 1 < gMapSize[0])          ? p[-EP_STRIDE + 1] : 0x1FFF;
  pOut[6] = (y + 1 < gMapSize[1] && x > 0)         ? p[EP_STRIDE - 1]  : 0x1FFF;
  pOut[7] = (y + 1 < gMapSize[1] && x + 1 < gMapSize[0]) ? p[EP_STRIDE + 1] : 0x1FFF;
}

// 0x490B80. Grows a box of like-valued tiles right and down from (x, y), at
// most 8 wide and 7 tall, then trims the longer side to three times the other.
void sub_490B80(ep_glob_t *pGlob, int x, int y, rect16_t *pBox, WORD wMatch) {
  int x0, y0, xEnd, yEnd, n, nW, nH;
  int bCol = 1, bRow = 1, bCorner = 1;
  WORD *p;

  pBox->top = (short)y;
  pBox->left = (short)x;
  x0 = (short)x;
  y0 = (short)y;

  xEnd = x0 + 8 * g_epBoxScale;  // Hyperion: 8 and 7 in 1.17
  if (xEnd > gMapSize[0])
    xEnd = gMapSize[0];
  yEnd = y0 + 7 * g_epBoxScale;
  if (yEnd > gMapSize[1])
    yEnd = gMapSize[1];

  x++;
  y++;
  while (x < xEnd) {
    if (y >= yEnd)
      break;
    if (!bCol && !bRow)
      break;

    if (bCol) {
      p = &pGlob->wMap[y0 * EP_STRIDE + x];
      n = y - y0;
      while (n > 0) {
        if (*p != wMatch) {
          bCol = 0;
          break;
        }
        n--;
        p += EP_STRIDE;
      }
    }
    if (bRow) {
      p = &pGlob->wMap[y * EP_STRIDE + x0];
      n = x - x0;
      while (n > 0) {
        if (*p != wMatch) {
          bRow = 0;
          break;
        }
        n--;
        p++;
      }
    }
    if (pGlob->wMap[y * EP_STRIDE + x] != wMatch)
      bCorner = 0;
    else if (bCorner) {
      if (bRow)
        y++;
      if (bCol)
        x++;
      continue;
    }
    if (bRow) {
      y++;
      continue;
    }
    if (bCol)
      x++;
  }

  pBox->right = (short)x;
  pBox->bottom = (short)y;
  nW = pBox->right - pBox->left;
  nH = pBox->bottom - pBox->top;
  if (nW > nH) {
    if (nW > nH * 0x30000 / 0x10000)
      nW = nH * 0x30000 / 0x10000;
  } else {
    if (nH > nW * 0x30000 / 0x10000)
      nH = nW * 0x30000 / 0x10000;
  }
  pBox->right = (short)(pBox->left + nW);
  pBox->bottom = (short)(pBox->top + nH);
}

// 0x490D80. Allocates and clears the glob, seeds it from the tile flags and
// builds the region graph over the whole map. ECX and EDX are set by the one
// caller and never read.
int sub_490D80(int nArg0, int nArg1, DWORD *pTiles) {
  ep_glob_t *pGlob;
  ep_area_t area;

  PathStatsBuildBegin();  // Hyperion
  PathSetMapLimits();  // Hyperion
  epGlob = SMemAlloc(sizeof(ep_glob_t), "sai_PathCreate.cpp", 0xD2, 0);
  memset(epGlob, 0, sizeof(ep_glob_t));
  pGlob = (ep_glob_t *)epGlob;
  area.ptMin.x = 0;
  area.ptMin.y = 0;
  pGlob->nRegions = 0;
  area.ptMax.y = gMapSize[1];
  area.ptMax.x = gMapSize[0];
  sub_490E90(pTiles);
  sub_490F50(pGlob);
  if (!sub_48F060(pGlob, &area))
    return 0;
  sai_contourscreate_init(epGlob);
  sub_48F0E0((ep_glob_t *)epGlob);
  PathStatsBuildEnd();  // Hyperion
  return 1;
}

// 0x490E90. Seeds the map from the tile flags. The width is read before the
// height and epGlob: that order alone decides which of EAX and ECX holds the
// row counter.
void sub_490E90(const DWORD *pTiles) {
  WORD *pMap;
  int w, x, y, n;
  DWORD dw;

  sub_48B1C0();
  w = gMapSize[0];
  y = gMapSize[1];
  pMap = ((ep_glob_t *)epGlob)->wMap;
  if (y > 0) {
   do {
    for (x = 0; x < w; x++) {
      dw = *pTiles;
      if (dw & 0x410000) {
        dw &= 0x6010000;
        if (dw) {
          if (dw != 0x2010000)
            n = 0x1FFA + (dw != 0x4010000);
          else
            n = 0x1FF9;
        } else {
          n = 0x1FFD;
        }
        *pMap = (WORD)n;
      } else {
        *pMap = 0x1FFD;
      }
      pTiles++;
      pMap++;
    }
    pMap += EP_STRIDE - w;
   } while (--y != 0);
  }
}

// 0x490F50. Appends the bottom-edge region and stamps it over the last row.
// The width is read as the DWORD holding both dimensions and narrowed at each
// use; a WORD read gives MSVC's movzx and colours the loop webs the other way.
void sub_490F50(ep_glob_t *pGlob) {
  ep_region_t *pRegion;
  int nIndex;
  int x, y;
  DWORD dwDims;

  nIndex = (WORD)pGlob->nRegions++;
  pRegion = &pGlob->aRegions[nIndex];
  pRegion->wGroup = 0x1FFD;
  pRegion->nX = (gMapSize[0] * 32 / 2 + 16) * 256;
  pRegion->nY = (gMapSize[1] * 32 - 16) * 256;
  pRegion->nMinX = 0;
  pRegion->nMaxX = gMapSize[0] * 32;
  pRegion->nMinY = gMapSize[1] * 32 - 32;
  pRegion->nMaxY = gMapSize[1] * 32;

  dwDims = *(DWORD *)gMapSize;
  for (y = gMapSize[1] - 1; y < gMapSize[1]; y++) {
    for (x = 0; x < (WORD)(dwDims = *(DWORD *)gMapSize); x++)
      pGlob->wMap[y * EP_STRIDE + x] = (WORD)nIndex;
  }

  pRegion->wTiles = (WORD)dwDims;
  pRegion->wArea = 0x4000;
}

// 0x4910C0. Partitions a region's path list into three runs, in place.
void sub_4910C0(ep_glob_t *pGlob, ep_region_t *pRegion) {
  WORD aKeep[256], aZero[256];
  WORD *pOut, *pKeep, *pZero, *pPath, *q;
  WORD r;
  int n;

  pOut = pRegion->pPaths;
  pKeep = aKeep;
  pZero = aZero;
  n = pRegion->bPathCount;
  pPath = pRegion->pPaths;
  while (n > 0) {
    r = *pPath;
    if (r == 0) {
      *pZero = 0;
      pZero++;
    } else if (pGlob->aRegions[r].wGroup == 0x1FFD) {
      *pKeep = r;
      pKeep++;
    } else {
      *pOut = r;
      pOut++;
    }
    n--;
    pPath++;
  }

  pRegion->bPathFirst = (BYTE)(pOut - pRegion->pPaths);
  for (q = aKeep; q < pKeep; q++) {
    *pOut = *q;
    pOut++;
  }

  if (pZero > aZero) {
    pRegion->nExtraPaths = (char)(pZero - aZero);
    for (q = aZero; q < pZero; q++) {
      *pOut = *q;
      pOut++;
    }
  }
}

// 0x491060. Frees the pathing glob and its contour table. The shipped form is
// 96 B against 95: a baked epGlob lands in a register the oracle spells with
// the 5-byte A1 encoding. Proof form matches.
void sai_glob_free() {
  contours_t *pContours;

  if (epGlob == 0)
    return;

  pContours = (contours_t *)((ep_glob_t *)epGlob)->pContours;
  if (pContours != 0) {
    sai_contours_free(pContours);
    SMemFree(pContours, "sai_PathCreate.cpp", 0x14D, 0);
    ((ep_glob_t *)epGlob)->pContours = 0;
  }
  SMemFree(epGlob, "sai_PathCreate.cpp", 0xE2, 0);
  epGlob = 0;
}

// 0x4911E0. Allocates the pathing glob and zeroes it.
void sai_glob_alloc() {
  PathSetMapLimits();  // Hyperion
  epGlob = SMemAlloc(sizeof(ep_glob_t), "sai_PathCreate.cpp", 0xD2, 0);
  memset(epGlob, 0, sizeof(ep_glob_t));
}
