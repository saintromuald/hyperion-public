// chunk.cpp -- the chunk reader: splits a loaded file image into tagged chunks
// and dispatches each to a handler table. Oracle 0x4C1020..0x4C13A0, bodies in
// oracle VA order.
//
// The node list is a Storm TSList, so every walk ends on a signed `p > 0`
// (src117/h/TSList.h).

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>
#include "../h/chunk.h"
#include "../h/Storm.h"
#include "../h/rtti.h"          // type_info::raw_name
#include "../gen/globals117.h"

extern "C" void __stdcall SErrSetLastError(DWORD dwErrCode);

// The list type UnlinkAll is named after: the oracle's link folded every
// instantiation onto this one.
struct CTRLNODE;
typedef TSList<CTRLNODE, TSGetLink<CTRLNODE> > CTRLNODELIST;

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// The RTTI Type Descriptor ChunkCreate tags its allocation with.
#ifdef OC_DLL_MODE
#define gTypeInfo_CHUNKNODE (*reinterpret_cast<type_info *>(OC_VA(0x5109FCu)))
#else
extern type_info gTypeInfo_CHUNKNODE;
#endif

// One map authentication template: a 20-byte name and its version id.
struct authtemplate_t {
  /*0x00*/ char szName[0x14];
  /*0x14*/ int nId;
};
static_assert(sizeof(authtemplate_t) == 0x18, "authtemplate_t size");
static_assert(offsetof(authtemplate_t, szName) == 0x00, "authtemplate_t::szName");
static_assert(offsetof(authtemplate_t, nId) == 0x14, "authtemplate_t::nId");

// 0x4F1020; nine entries, ending at gCtrlEvtFcnTbl.
#ifdef OC_DLL_MODE
#define sgAuthTemplates_g (reinterpret_cast<authtemplate_t *>(OC_VA(0x4F1020u)))
#else
extern authtemplate_t sgAuthTemplates_g[9];
#endif

// The bodies, in oracle VA order.

// 0x4C1020 (73 B). TRUE when every required tag has a node.
BOOL CheckChunks(CHUNKLIST *pList, const chunkhandler_t *pTbl, int nCount)
{
  for (int i = 0; i < nCount; ++i, ++pTbl) {
    if (pTbl->bRequired) {
      CHUNKNODE *p;
      for (p = pList->link.next; (intptr_t)p > 0; p = p->link.next) {
        if (p->chunk.dwId == pTbl->dwId)
          break;
      }
      if ((intptr_t)p <= 0)
        return FALSE;
    }
  }
  return TRUE;
}

// 0x4C1070 (147 B). Appends a copy of the chunk to the list. The stores after
// the link run even when SMemAlloc returned null; that is the oracle's own null
// dereference. The unlink keeps src117/lang/gametype.cpp's phi shape.
BOOL __stdcall ChunkCreate(chunk_t *pChunk, int nIndex, CHUNKLIST *pList)
{
  CHUNKNODE *node = reinterpret_cast<CHUNKNODE *>(
      SMemAlloc(sizeof(CHUNKNODE), gTypeInfo_CHUNKNODE.raw_name(), -2, 8));
  TSLink<CHUNKNODE> *pl;
  if (node != 0) {
    node->link.prev = 0;
    pl = &node->link;
    node->link.next = 0;
  } else {
    node = 0;
    pl = &pList->link;
  }
  CHUNKNODE *pprev = pl->prev;
  if (pprev != 0) {
    CHUNKNODE *pnext = pl->next;
    TSLink<CHUNKNODE> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<CHUNKNODE> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0) {
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      }
      pnl = reinterpret_cast<TSLink<CHUNKNODE> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = 0;
    pl->next = 0;
  }
  CHUNKNODE *ptail = pList->link.prev;
  pl->prev = ptail;
  pl->next = ptail->link.next;
  ptail->link.next = node;
  pList->link.prev = reinterpret_cast<CHUNKNODE *>(pl);
  node->chunk = *pChunk;
  return TRUE;
}

// Unlink one node from whatever list holds it. Inlined into Clear.
static void UnlinkChunk(CHUNKNODE *node)
{
  TSLink<CHUNKNODE> *pl = &node->link;
  CHUNKNODE *pprev = pl->prev;
  if (pprev != 0) {
    CHUNKNODE *pnext = pl->next;
    TSLink<CHUNKNODE> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<CHUNKNODE> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0) {
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      }
      pnl = reinterpret_cast<TSLink<CHUNKNODE> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = 0;
    pl->next = 0;
  }
}

// 0x4C1110. Unlink and free every node. Normalising the head to NULL before
// the test keeps MSVC from rotating the loop.
void CHUNKLIST::Clear(void)
{
  for (;;) {
    CHUNKNODE *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkChunk(p);
    SMemFree(p, gTypeInfo_CHUNKNODE.raw_name(), -2, 0);
  }
}

// Reads the 8-byte header at pHdr into c and returns the chunk's data, or null
// when the header would pass the end of the image.
static BYTE *ReadHeader(chunk_t *c, BYTE *pHdr)
{
  BYTE *p = pHdr + 8;
  if (p > c->pEnd)
    return 0;
  c->dwId = reinterpret_cast<DWORD *>(pHdr)[0];
  c->dwSize = reinterpret_cast<DWORD *>(pHdr)[1];
  return p;
}

// Calls pfnCreate for each chunk until it fails or the next header would pass
// the end of the image. The redundant `!= 0` in the loop test keeps the loop
// unrotated and without a loop label, so it gets no head pad and p takes ECX.
BOOL InitNodes(void *pBuffer, DWORD dwSize, chunkcreate_fn pfnCreate,
               CHUNKLIST *pList)
{
  chunk_t c;
  SMemZero(&c, sizeof(c));
  c.pEnd = static_cast<BYTE *>(pBuffer) + dwSize;
  BYTE *p = ReadHeader(&c, static_cast<BYTE *>(pBuffer));
  if (p) {
    int i = 0;
    while ((c.pData = p) != 0 && pfnCreate(&c, i, pList)) {
      p = ReadHeader(&c, c.pData + c.dwSize);
      if (!p)
        break;
      i++;
    }
  }
  return TRUE;
}

// A CHUNKLIST with Storm's constructor and destructor: UnlinkAll, then the
// head's own link is unlinked without being zeroed.
struct CHUNKLISTLOCAL : CHUNKLIST {
  CHUNKLISTLOCAL()
  {
    _unmapped_00 = 0;
    link.prev = reinterpret_cast<CHUNKNODE *>(&link);
    link.next = reinterpret_cast<CHUNKNODE *>(~(intptr_t)&link);
  }
  ~CHUNKLISTLOCAL()
  {
    reinterpret_cast<CTRLNODELIST *>(this)->UnlinkAll();
    CHUNKNODE *pprev = link.prev;
    if (pprev != 0) {
      CHUNKNODE *pnext = link.next;
      TSLink<CHUNKNODE> *pnl;
      if ((intptr_t)pnext <= 0) {
        pnl = reinterpret_cast<TSLink<CHUNKNODE> *>(~(intptr_t)pnext);
      } else {
        int nOff = 0;
        if ((intptr_t)pnext > 0) {
          nOff = (int)((intptr_t)&link - (intptr_t)pprev->link.next);
        }
        pnl = reinterpret_cast<TSLink<CHUNKNODE> *>((intptr_t)pnext + nOff);
      }
      pnl->prev = pprev;
      link.prev->link.next = link.next;
    }
  }
};

// 0x4C1200 (171 B). Splits the image into a local list, checks the required
// tags and runs the handlers. The early returns and the real destructor are
// what put the failure arm first.
BOOL __fastcall ReadBufferChunks(void *pBuffer, DWORD dwSize,
                                 const chunkhandler_t *pTbl, int nCount,
                                 void *pDest)
{
  CHUNKLISTLOCAL list;
  InitNodes(pBuffer, dwSize, ChunkCreate, &list);
  if (!CheckChunks(&list, pTbl, nCount)) {
    list.Clear();
    return FALSE;
  }
  if (!LoadChunks(&list, pTbl, nCount, pDest)) {
    list.Clear();
    return FALSE;
  }
  list.Clear();
  return TRUE;
}

// 0x4C12B0 (39 B). Copies the chunk out when it ends inside the image; the
// size test is unsigned.
BOOL ReadChunk(void *pFile, void *pDest)
{
  chunk_t *c = static_cast<chunk_t *>(pFile);
  if (c->pData + c->dwSize > c->pEnd)
    return FALSE;
  memcpy(pDest, c->pData, c->dwSize);
  return TRUE;
}

// 0x4C12E0 (108 B). Runs each handler over every node carrying its tag.
BOOL LoadChunks(CHUNKLIST *pList, const chunkhandler_t *pTbl, int nCount,
                void *pDest)
{
  for (int i = 0; i < nCount; ++i, ++pTbl) {
    for (CHUNKNODE *p = pList->link.next; (intptr_t)p > 0; p = p->link.next) {
      if (p->chunk.dwId == pTbl->dwId && pTbl->pfnHandler) {
        if (!pTbl->pfnHandler(&p->chunk, p->chunk.dwSize, pDest))
          return FALSE;
      }
    }
  }
  return TRUE;
}

// 0x4C1350 (81 B). The table is walked by index, not by pointer: the index is
// what makes MSVC keep its induction pointer on nId.
void __stdcall sub_4C1350(chunkenum_fn pfnEnum, void *pvContext)
{
  if (!pfnEnum) {
    SErrSetLastError(ERROR_INVALID_PARAMETER);
    return;
  }
  int nVer = 7;
  do {
    --nVer;
    int i = 9;
    do {
      --i;
      if (sgAuthTemplates_g[i].nId == nVer &&
          !pfnEnum(sgAuthTemplates_g[i].szName, sgAuthTemplates_g[i].nId,
                   pvContext))
        return;
    } while (i != 0);
  } while (nVer);
}
