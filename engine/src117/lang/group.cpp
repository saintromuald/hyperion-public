// group.cpp -- the .grp group table and its lo-file overlays.
//
// Bodies are in ascending oracle address, 1.17's own order.

#include "../h/group.h"
#include "../h/grp.h"         // GROUP / FRAME
#include "../h/statdata.h"    // grhsize_t
#include "../h/Storm.h"
#include "../h/gamedata.h"    // LoadGroupFile / LoadOverlayFile / wait_object_loading
#include "../h/gds_local.h"   // psnprintf
#include "../h/glues.h"       // str_get_string
#include "../h/CImage.h"      // GetNumImages
#include "../h/dlg_dlg.h"     // img_update
#include "../h/save_dlg.h"    // RunEvtDispatcher
#include "../port/data/port_formats.h"  // Hyperion: PortGroupNative
#include "../gen/globals117.h"

#include <windows.h>
#include <string.h>

void group_free_tbl(void **ppTbl, int nCount)
{
  if (nCount <= 0)
    return;

  int i = 1;
  void **p = ppTbl;
  int n = nCount;
  do {
    if (*p != NULL) {
      for (int j = i; j < nCount; j++) {
        if (*p == ppTbl[j])
          ppTbl[j] = NULL;
      }
      SMemFree(*p, "group.cpp", 0x158, 0);
      *p = NULL;
    }
    i++;
    p++;
  } while (--n != 0);
}

// 0x463BD0. Copies one frame header out of a group and rebases its data pointer.
void grh_get_header(GROUP *pGroup, WORD wFrame, FRAME *pFrame,
                    grhsize_t *pSize)
{
  WORD nFrames = 0x7FFF & *(WORD *)pGroup;

  wFrame = (wFrame < nFrames) ? wFrame : 0;
  pSize->wWidth = pGroup->wMaxWidth;
  pSize->wHeight = pGroup->wMaxHeight;
  *pFrame = pGroup->frames[wFrame];
#if !defined(_WIN64) && !defined(__LP64__)
  pFrame->pData += (DWORD_PTR)pGroup;
#endif
  // Hyperion: at 64 bits the loader has made pData the pointer already
  // (PortGroupNative).
}

// 0x463A30. Loads group nIndex, or hands back the slot an equal id already
// filled. The id array's slot is reused as LoadGroupFile's out-parameter: the
// frame has room for the 0x104 name buffer and one spill, nothing more.
intptr_t load_grh(void **ppTbl, void *pStr, int nIndex, DWORD *pIds,
             grhreq_t *pReq, intptr_t *pnOut)
{
  char szName[0x104];
  DWORD *pEntry = &pIds[nIndex];
  DWORD dwId = *pEntry;

  if (dwId == 0)
    return 0;

  for (int j = nIndex - 1; j >= 0; j--) {
    if (pIds[j] == dwId)
      return (intptr_t)ppTbl[j];
  }

  pReq->hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
  psnprintf(szName, 0x104, "unit\\%s",
            (const char *)str_get_string(pStr, (WORD)*pEntry));
  pIds = NULL;
  *pnOut = LoadGroupFile((intptr_t)szName, (intptr_t)&pIds, 0, (intptr_t)pReq,
                         (intptr_t)"group.cpp", 0xED);
  return (intptr_t)pIds;
}

// 0x463AE0. One pass loads every group; a second walks the table backwards and
// rebases the frame data pointers of each group whose slot is its first.
void group_load_tbl(void **ppTbl, DWORD *pIds, void *pStr, int nUnused,
                    grhreq_t *pReq, intptr_t *pnSizes)
{
  int n = GetNumImages();
#if defined(_WIN64) || defined(__LP64__)
  const int nImages = n;
#endif

  memset(pReq, 0, n * sizeof(grhreq_t));
  memset(pnSizes, 0, n * sizeof(*pnSizes));

  for (int i = 0; i < n; i++) {
    ppTbl[i] = (void *)load_grh(ppTbl, pStr, i, pIds, &pReq[i], &pnSizes[i]);
    RunEvtDispatcher(1);
    img_update();
  }

  wait_object_loading(pReq, (intptr_t)pnSizes, n);

  while (n != 0) {
    int k = n - 1;
    GROUP *pGroup = (GROUP *)ppTbl[k];
    DWORD nFrames;

    n = k;
    for (int j = 0; j < n; j++) {
      if (ppTbl[j] == pGroup)
        goto next;
    }
#if defined(_WIN64) || defined(__LP64__)
    // Hyperion: a pointer does not fit the file's frame, so the group gets a
    // frame table of its own in a new block, which every slot that shares it takes
    // (PortGroupNative).
    (void)nFrames;
    if (pGroup) {
      GROUP *pNative = PortGroupNative(pGroup);
      for (int j = k; j < nImages; j++) {
        if (ppTbl[j] == pGroup)
          ppTbl[j] = pNative;
      }
    }
#else
    nFrames = *(WORD *)pGroup & 0x7FFF;
    if (nFrames != 0) {
      FRAME *pFrame = &pGroup->frames[nFrames];

      do {
        pFrame--;
        pFrame->pData = (BYTE *)(((DWORD_PTR)pFrame->pData & 0x7FFFFFFF) +
                                 (DWORD_PTR)pGroup);
      } while (--nFrames != 0);
    }
#endif
  next:;
  }
}

// 0x463C20. The same pass over the lo-file overlays. load_grh's body is inline
// here rather than called, and there is no rebase pass.
void group_load_lo(void **ppTbl, DWORD *pIds, void *pStr, int nUnused,
                   grhreq_t *pReq, intptr_t *pnSizes)
{
  char szName[0x104];
  int n = GetNumImages();

  memset(pReq, 0, n * sizeof(grhreq_t));
  memset(pnSizes, 0, n * sizeof(*pnSizes));

  for (int i = 0; i < n; i++) {
    DWORD dwId = pIds[i];
    DWORD_PTR dwOut;
    DWORD_PTR dwGrp;
    int j;

    if (dwId == 0) {
      dwGrp = 0;
      goto store;
    }
    for (j = i - 1; j >= 0; j--) {
      if (pIds[j] == dwId) {
        dwGrp = (DWORD_PTR)ppTbl[j];
        goto store;
      }
    }
    grhreq_t *pThis;  // Hyperion: set apart from its declaration, which a goto jumps past
    pThis = &pReq[i];

    pThis->hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    psnprintf(szName, 0x104, "unit\\%s",
              (const char *)str_get_string(pStr, (WORD)pIds[i]));
    dwOut = 0;
    pnSizes[i] = LoadOverlayFile((intptr_t)szName, (intptr_t)&dwOut, 0, (intptr_t)pThis,
                                 (intptr_t)"group.cpp", 0x8C);
    dwGrp = dwOut;
  store:
    ppTbl[i] = (void *)dwGrp;
    RunEvtDispatcher(1);
    img_update();
  }

  wait_object_loading(pReq, (intptr_t)pnSizes, n);
}
