// sai_LoadSave.cpp -- AI pathing. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller in this file by
// /Ob2, which the oracle (WPO off) never does across TUs; the lever against it
// is __declspec(noinline) on the declaration.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../port/data/port_formats.h"  // Hyperion: PortSave117Size, PortSave117Put, PortSave117Get
#include "../gen/save117.h"
#include <string.h>              // memcpy -- the save image is copied in runs

#include "../h/sai_LoadSave.h"
#include "../h/sai_PathCreate.h" // sai_glob_alloc -- the one declaration of that name
#include "../h/sai_path.h"       // sub_48E600
#include "../h/sai_ContoursCreate.h"  // Hyperion: g_nContourLists
#include "../h/poollimits.h"     // Hyperion: g_pools.paths
#include "../h/net_cmds.h"       // voice_squelch, the ICF-folded 1-byte ret
#include "../h/Storm.h"          // SMemAlloc
#include "../gen/globals117.h"

// Globals this TU reaches, dual-spelled, with one #ifdef per name rather than
// one over the block: a single guard would undeclare every name under it.

// epGlob @0x69B584 -- the pathing and region block's base. Spelled exactly as
// src117/lang/sai.cpp does, since a disagreeing second spelling of a registered
// global is a C2371 in some other TU.
#ifdef OC_DLL_MODE
#define epGlob            (*reinterpret_cast<void **>(OC_VA(0x69B584u)))
#else
extern void *epGlob;                          // 0x69B584
#endif

// sgFreePath @0x69B570 -- head of the path pool's free list. Unregistered.
// Holds a pointer at run time and a 1-based pool index in the save image;
// the two converters below are what switch between them.
#ifdef OC_DLL_MODE
#define sgFreePath        (*reinterpret_cast<BYTE **>(OC_VA(0x69B570u)))
#else
extern BYTE *sgFreePath;                      // 0x69B570
#endif

// sai_PathPoolBuf @0x69B574 -- base of the path pool. Unregistered.
// sai_pathpool_init SMemAllocs exactly 0x20000 and chains 1024 records of 0x80
// (Hyperion: the game's paths, g_pools.paths, revision 15).
#ifdef OC_DLL_MODE
#define sai_PathPoolBuf   (*reinterpret_cast<BYTE **>(OC_VA(0x69B574u)))
#else
extern BYTE *sai_PathPoolBuf;                 // 0x69B574
#endif

// The save image's leading dword: a version word and a contours-present flag.
// Both serializers keep it in one 4-byte local and move it whole.
struct sai_save_hdr_t {
  /*0x00*/ WORD wVersion;
  /*0x02*/ WORD wHasContours;
};

static_assert(sizeof(sai_save_hdr_t) == 4,
              "sai_save_hdr_t size    both bodies keep it in the single `push ecx` slot and move it as one dword");
static_assert(offsetof(sai_save_hdr_t, wVersion) == 0x00,
              "sai_save_hdr_t::wVersion   read_save_paths `cmp ax, 0Eh` on the low half of [ebp-4h]");
static_assert(offsetof(sai_save_hdr_t, wHasContours) == 0x02,
              "sai_save_hdr_t::wHasContours   read_save_paths `cmp word ptr [ebp-2h], 0h`");

// The region block, walked as byte offsets into ep_glob_t (sai_PathCreate.h):
// the converters below step a cursor over each region's pPaths slot
// (`lea eax, [ecx+44A08h]` then `add eax, 40h`), and a -1 there stands for the
// region's own wInlinePaths, 0x20 further on. The contour table is at
// +0x97A1C and the whole block is 0x97A20 (the memcpy size here and
// sai_glob_alloc's own `push 97A20h`). Spelled with offsetof so that on x64,
// where the pointers widen, each slot is still where the struct puts it.
#define OC_GLOB_RECS       (offsetof(ep_glob_t, aRegions) + offsetof(ep_region_t, pPaths))
#define OC_GLOB_REC_SIZE   ((int)sizeof(ep_region_t))
#define OC_GLOB_INLINE     (offsetof(ep_region_t, wInlinePaths) - offsetof(ep_region_t, pPaths))
#define OC_GLOB_POOL       offsetof(ep_glob_t, wPathPool)
#define OC_GLOB_POOLPTR    offsetof(ep_glob_t, pPathPool)
#define OC_GLOB_SCRATCH    offsetof(ep_glob_t, bScratch)
#define OC_GLOB_SCRATCHPTR offsetof(ep_glob_t, pScratch)
#define OC_GLOB_CONTOURS   offsetof(ep_glob_t, pContours)
#define OC_GLOB_SIZE       sizeof(ep_glob_t)

// What sai_PathingGlobWriteSave writes besides the contour lists: the header
// and free-list dwords, the region block and the path pool. Hyperion: the
// region block and the contour table in 1.17's layout.
#define OC_SAVE_FIXED      (8 + GlobSaveBytes() + OC_SAVE_PATHS)
// Hyperion: the path pool, the game's paths of 0x80 bytes, 0x20000 in 1.17 (revision 15)
#define OC_SAVE_PATHS      (g_pools.paths * 0x80)

// Hyperion: a larger map's save follows the contour table with its counts and
// capacities whole, which 1.17's layout keeps in shorts (contour_count_t).
#define OC_SAVE_COUNTS     (GlobWide() ? (int)(8 * sizeof(DWORD)) : 0)


// Hyperion: a map past 256 tiles a side. Its glob has rows of
// MAP_SIDE_MAX tiles and longer split, region and pool tables (h/maplimits.h), which
// 1.17's layout has no room for. It is saved field by field as 1.17's layout saves the
// glob, each table whole, under a version of its own that 1.17 refuses. A map within
// 256 tiles a side saves 1.17's layout and version, as before.
#define OC_PATH_VERSION      0x0E
#define OC_PATH_VERSION_WIDE 0x10E
#define OC_SAVE_REGION       (SAVE117_PathGlob + 1)  // ep_glob_t.aRegions[] (gen/save117.h)

static bool GlobWide()
{
#if defined(_M_IX86)
  return false;
#else
  return EP_STRIDE != MAP_SIDE_117;
#endif
}

static int GlobSaveBytes()
{
  if (!GlobWide())
    return (int)PortSave117Size(SAVE117_PathGlob);
  return (int)(12 + sizeof(((ep_glob_t *)0)->wMap) + sizeof(((ep_glob_t *)0)->bScratch)
               + EP_REGION_RECS * PortSave117Size(OC_SAVE_REGION) + sizeof(((ep_glob_t *)0)->wPathPool));
}

// The glob after sai_EncodeGlobForSave, whose pointers are then indices that fit a dword.
static BYTE *GlobPut(BYTE *pOut)
{
  if (!GlobWide())
    return PortSave117Put(SAVE117_PathGlob, epGlob, pOut);
  const ep_glob_t *pGlob = (const ep_glob_t *)epGlob;
  const DWORD adwHead[3] = {(WORD)pGlob->nRegions | (DWORD)pGlob->wUnk02 << 16,
                            (DWORD)(DWORD_PTR)pGlob->pPathPool, (DWORD)(DWORD_PTR)pGlob->pScratch};
  memcpy(pOut, adwHead, sizeof adwHead);
  pOut += sizeof adwHead;
  memcpy(pOut, pGlob->wMap, sizeof pGlob->wMap);
  pOut += sizeof pGlob->wMap;
  memcpy(pOut, pGlob->bScratch, sizeof pGlob->bScratch);
  pOut += sizeof pGlob->bScratch;
  for (int i = 0; i < EP_REGION_RECS; i++)
    pOut = PortSave117Put(OC_SAVE_REGION, &pGlob->aRegions[i], pOut);
  memcpy(pOut, pGlob->wPathPool, sizeof pGlob->wPathPool);
  return pOut + sizeof pGlob->wPathPool;
}

static const BYTE *GlobGet(const BYTE *pIn)
{
  if (!GlobWide())
    return PortSave117Get(SAVE117_PathGlob, pIn, epGlob);
  ep_glob_t *pGlob = (ep_glob_t *)epGlob;
  DWORD adwHead[3];
  memcpy(adwHead, pIn, sizeof adwHead);
  pIn += sizeof adwHead;
  pGlob->nRegions = (short)(adwHead[0] & 0xFFFF);
  pGlob->wUnk02 = (WORD)(adwHead[0] >> 16);
  pGlob->pPathPool = (WORD *)(DWORD_PTR)adwHead[1];
  pGlob->pScratch = (void *)(DWORD_PTR)adwHead[2];
  memcpy(pGlob->wMap, pIn, sizeof pGlob->wMap);
  pIn += sizeof pGlob->wMap;
  memcpy(pGlob->bScratch, pIn, sizeof pGlob->bScratch);
  pIn += sizeof pGlob->bScratch;
  for (int i = 0; i < EP_REGION_RECS; i++)
    pIn = PortSave117Get(OC_SAVE_REGION, pIn, &pGlob->aRegions[i]);
  memcpy(pGlob->wPathPool, pIn, sizeof pGlob->wPathPool);
  return pIn + sizeof pGlob->wPathPool;
}

static BYTE *ContourCountsPut(const contours_t *p, BYTE *pOut)
{
  if (!GlobWide())
    return pOut;
  DWORD adw[8];
  for (int i = 0; i < 4; i++) {
    adw[i] = (DWORD)p->nCounts[i];
    adw[4 + i] = (DWORD)p->nCapacity[i];
  }
  memcpy(pOut, adw, sizeof adw);
  return pOut + sizeof adw;
}

static const BYTE *ContourCountsGet(contours_t *p, const BYTE *pIn)
{
  if (!GlobWide())
    return pIn;
  DWORD adw[8];
  memcpy(adw, pIn, sizeof adw);
  for (int i = 0; i < 4; i++) {
    p->nCounts[i] = (contour_count_t)adw[i];
    p->nCapacity[i] = (contour_count_t)adw[4 + i];
  }
  return pIn + sizeof adw;
}

// A contour list as the save holds it: 1.17's records of 8 bytes, or on a larger map the
// x64 record whole, whose point and end are 4 bytes each (contour_t).
static int ContourRecordBytes()
{
  return GlobWide() ? (int)sizeof(contour_t) : 8;
}

static BYTE *ContourListPut(const contour_t *p, int n, BYTE *pOut)
{
  if (GlobWide() || sizeof(contour_t) == 8) {
    memcpy(pOut, p, n * sizeof(contour_t));
    return pOut + n * sizeof(contour_t);
  }
  for (int i = 0; i < n; i++, pOut += 8) {
    const short an[3] = {(short)p[i].pt.x, (short)p[i].pt.y, (short)p[i].nEnd};
    memcpy(pOut, an, sizeof an);
    pOut[6] = p[i].bC;
    pOut[7] = p[i].bFlags;
  }
  return pOut;
}

static const BYTE *ContourListGet(contour_t *p, int n, const BYTE *pIn)
{
  if (GlobWide() || sizeof(contour_t) == 8) {
    memcpy(p, pIn, n * sizeof(contour_t));
    return pIn + n * sizeof(contour_t);
  }
  for (int i = 0; i < n; i++, pIn += 8) {
    short an[3];
    memcpy(an, pIn, sizeof an);
    memset(&p[i], 0, sizeof p[i]);
    p[i].pt.x = an[0];
    p[i].pt.y = an[1];
    p[i].nEnd = an[2];
    p[i].bC = pIn[6];
    p[i].bFlags = pIn[7];
  }
  return pIn;
}

// voice_squelch (0x4BE950) is a one-byte `ret` that /OPT:ICF folded many empty
// functions onto; net_cmds.h spells it (DWORD, BOOL). The three sites here pass
// the contour table in ECX alone, so the call goes through a one-argument
// pointer cast and the referenced symbol stays the one gen thunks.
// Hyperion: the empty function not called: through a pointer of another type the
// call traps in WebAssembly
#define OC_CONTOURS_NOTIFY(p) static_cast<void>(p)

// The bodies, in oracle VA order.

// 0x48C940 (125 B). Turn the region block's saved indices back into pointers.
// Every store aliases epGlob, so the oracle re-reads it after each one.
void sai_DecodeGlobFromSave(void)
{
  int n = *(const short *)epGlob;

  if (n > 0)
  {
    DWORD_PTR *pRec = (DWORD_PTR *)((BYTE *)epGlob + OC_GLOB_RECS);
    do
    {
      int nIndex = (int)*pRec;
      if (nIndex == -1)
        *pRec = (DWORD_PTR)pRec + OC_GLOB_INLINE;
      else
        *pRec = (DWORD_PTR)((WORD *)epGlob + nIndex) + OC_GLOB_POOL;
      pRec = (DWORD_PTR *)((BYTE *)pRec + OC_GLOB_REC_SIZE);
    } while (--n > 0);
  }

  *(DWORD_PTR *)((BYTE *)epGlob + OC_GLOB_POOLPTR) =
      (DWORD_PTR)((WORD *)epGlob + (*(DWORD_PTR *)((BYTE *)epGlob + OC_GLOB_POOLPTR) +
                                    OC_GLOB_POOL / sizeof(WORD)));
  *(DWORD_PTR *)((BYTE *)epGlob + OC_GLOB_SCRATCHPTR) =
      (DWORD_PTR)((WORD *)epGlob + (*(DWORD_PTR *)((BYTE *)epGlob + OC_GLOB_SCRATCHPTR) * 3 +
                                    OC_GLOB_SCRATCH / sizeof(WORD)));

  if (sgFreePath)
    sgFreePath = sai_PathPoolBuf + (((intptr_t)sgFreePath - 1) << 7);
}

// 0x48C9C0 (139 B). The inverse of sai_DecodeGlobFromSave.
// The record is addressed as base + byte offset; a cursor advanced by 0x40 lets
// MSVC build a second induction variable for pRec + 0x20.
void sai_EncodeGlobForSave(void)
{
  int n = *(const short *)epGlob;

  if (n > 0)
  {
    BYTE *pBase = (BYTE *)epGlob + OC_GLOB_RECS;
    int nOff = 0;
    do
    {
      DWORD_PTR *pRec = (DWORD_PTR *)(pBase + nOff);
      if (*pRec == (DWORD_PTR)pRec + OC_GLOB_INLINE)
        *pRec = (DWORD)-1;
      else
        *pRec = (DWORD)(((intptr_t)*pRec - (intptr_t)epGlob - (intptr_t)OC_GLOB_POOL) >> 1);
      nOff += OC_GLOB_REC_SIZE;
    } while (--n > 0);
  }

  *(DWORD_PTR *)((BYTE *)epGlob + OC_GLOB_POOLPTR) =
      (DWORD)((*(intptr_t *)((BYTE *)epGlob + OC_GLOB_POOLPTR) - (intptr_t)epGlob -
               (intptr_t)OC_GLOB_POOL) >> 1);
  *(DWORD_PTR *)((BYTE *)epGlob + OC_GLOB_SCRATCHPTR) =
      (DWORD)((*(intptr_t *)((BYTE *)epGlob + OC_GLOB_SCRATCHPTR) - (intptr_t)epGlob -
               (intptr_t)OC_GLOB_SCRATCH) / 6);

  if (sgFreePath)
    sgFreePath = (BYTE *)((((intptr_t)sgFreePath - (intptr_t)sai_PathPoolBuf) >> 7) + 1);
}

// 0x48CA50 (312 B). Serialize the region block, the contour table and the path
// pool into pDest. Always returns 1.
int sai_PathingGlobWriteSave(void *pDest, int nSize)
{
  sai_save_hdr_t hdr;
  BYTE *pOut = (BYTE *)pDest;
  contours_t *pContours;

  hdr.wVersion = GlobWide() ? OC_PATH_VERSION_WIDE : OC_PATH_VERSION;  // Hyperion
  hdr.wHasContours = (WORD)(*(contours_t **)((BYTE *)epGlob + OC_GLOB_CONTOURS) != 0);

  sub_48E600(0);
  sai_EncodeGlobForSave();

  *(DWORD *)pOut = *(DWORD *)&hdr;
  *(DWORD *)(pOut + 4) = (DWORD)(DWORD_PTR)sgFreePath;
  pOut += 8;

  pOut = GlobPut(pOut);  // Hyperion: 1.17's layout, or a larger map's

  if (hdr.wHasContours)
  {
    pContours = *(contours_t **)((BYTE *)epGlob + OC_GLOB_CONTOURS);
    OC_CONTOURS_NOTIFY(pContours);
    pOut = PortSave117Put(SAVE117_Contours, pContours, pOut);
    OC_CONTOURS_NOTIFY(pContours);
    pOut = ContourCountsPut(pContours, pOut);  // Hyperion

    // Hyperion: record by record
    for (int i = 0; i < 4; i++)
      pOut = ContourListPut(pContours->pLists[i], pContours->nCounts[i], pOut);
  }

  memcpy(pOut, sai_PathPoolBuf, OC_SAVE_PATHS);
  sai_DecodeGlobFromSave();
  sub_48E600(1);
  return 1;
}

// 0x48CB90 (51 B). The byte count sai_PathingGlobWriteSave will write.
int sai_get_contour_bytes(void)
{
  const contours_t *p = *(contours_t **)((BYTE *)epGlob + OC_GLOB_CONTOURS);

  if (p)
    return (int)((p->nCounts[0] + p->nCounts[1] + p->nCounts[2] + p->nCounts[3]) * ContourRecordBytes()
                 + OC_SAVE_FIXED + PortSave117Size(SAVE117_Contours) + OC_SAVE_COUNTS);
  return (int)OC_SAVE_FIXED;
}

// 0x48CBD0 (415 B). Read back what sai_PathingGlobWriteSave wrote. Returns 0
// when the version word is not 0x0E (Hyperion: or the larger map's, for such a map).
int read_save_paths(const BYTE *pIn, int nSize)
{
  sai_save_hdr_t hdr;
  contours_t *pContours;
  // Hyperion: every read held to the nSize bytes the save gives, and the free path to the pool
  const BYTE *const pStart = pIn;

  if (nSize < 8)
    return 0;
  hdr = *(const sai_save_hdr_t *)pIn;
  if (hdr.wVersion != OC_PATH_VERSION && hdr.wVersion != OC_PATH_VERSION_WIDE)
    return 0;

  sgFreePath = (BYTE *)(DWORD_PTR)*(const DWORD *)(pIn + 4);
  if ((DWORD_PTR)sgFreePath > (DWORD_PTR)g_pools.paths)
    return 0;
  pIn += 8;

  sai_glob_alloc();
  // Hyperion: the version has to be the one the map's size gives
  if ((hdr.wVersion == OC_PATH_VERSION_WIDE) != GlobWide())
    return 0;
  if (nSize < (int)OC_SAVE_FIXED)
    return 0;
  pIn = GlobGet(pIn);

  if (hdr.wHasContours)
  {
    if (nSize - (pIn - pStart) < (intptr_t)PortSave117Size(SAVE117_Contours) + OC_SAVE_COUNTS + OC_SAVE_PATHS)
      return 0;
    pContours = (contours_t *)SMemAlloc(sizeof(contours_t), "sai_LoadSave.cpp", 0x1BF, 0);
    *(contours_t **)((BYTE *)epGlob + OC_GLOB_CONTOURS) = pContours;
    pIn = PortSave117Get(SAVE117_Contours, pIn, pContours);
    OC_CONTOURS_NOTIFY(pContours);
    pIn = ContourCountsGet(pContours, pIn);  // Hyperion
    long long nLists = 0;
    for (int i = 0; i < 4; i++) {
      if ((long long)pContours->nCounts[i] < 0)
        return 0;
      nLists += (long long)pContours->nCounts[i] * ContourRecordBytes();
    }
    if (nLists > nSize - (pIn - pStart) - OC_SAVE_PATHS)
      return 0;

    pContours->pLists[0] = (contour_t *)SMemAlloc(pContours->nCounts[0] * sizeof(contour_t), "sai_LoadSave.cpp", 0x1C4, 0);
    pContours->pLists[1] = (contour_t *)SMemAlloc(pContours->nCounts[1] * sizeof(contour_t), "sai_LoadSave.cpp", 0x1C5, 0);
    pContours->pLists[2] = (contour_t *)SMemAlloc(pContours->nCounts[2] * sizeof(contour_t), "sai_LoadSave.cpp", 0x1C6, 0);
    pContours->pLists[3] = (contour_t *)SMemAlloc(pContours->nCounts[3] * sizeof(contour_t), "sai_LoadSave.cpp", 0x1C7, 0);

    // Hyperion: record by record
    for (int i = 0; i < 4; i++)
      pIn = ContourListGet(pContours->pLists[i], pContours->nCounts[i], pIn);
    ++g_nContourLists;  // Hyperion
  }

  memcpy(sai_PathPoolBuf, pIn, OC_SAVE_PATHS);
  sai_DecodeGlobFromSave();
  sub_48E600(1);
  return 1;
}
