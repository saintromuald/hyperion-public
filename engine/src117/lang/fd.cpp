// fd.cpp -- the file-download layer's teardown and callback registration,
// oracle 0x440640-0x44093F. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/fd.h"
#include "../h/fd_user.h"
#include "../h/Storm.h"
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// The rest of the registration block set_fd_callbacks writes in one run;
// gbFdLocalId and gpfnFdSend are in fd.h. 0x665F90 and the two slots at
// 0x666098 are SStrCopy destinations with capacity 0x104.
#ifdef OC_DLL_MODE
  #ifndef byte_665F81
  #define byte_665F81 (*reinterpret_cast<BYTE *>(OC_VA(0x665F81u)))
  #endif
#else
extern BYTE byte_665F81;
#endif

#ifdef OC_DLL_MODE
  #ifndef dword_665F84
  #define dword_665F84 (*reinterpret_cast<int *>(OC_VA(0x665F84u)))
  #endif
#else
extern int dword_665F84;
#endif

#ifdef OC_DLL_MODE
  #ifndef dword_665F8C
  #define dword_665F8C (*reinterpret_cast<int *>(OC_VA(0x665F8Cu)))
  #endif
#else
extern int dword_665F8C;
#endif

#ifdef OC_DLL_MODE
  #ifndef sz_665F90
  #define sz_665F90 (reinterpret_cast<char *>(OC_VA(0x665F90u)))
  #endif
#else
extern char sz_665F90[0x104];
#endif

#ifdef OC_DLL_MODE
  #ifndef sz_666098
  #define sz_666098 (reinterpret_cast<char (*)[0x104]>(OC_VA(0x666098u)))
  #endif
#else
extern char sz_666098[2][0x104] OC_UNALIGNED;
#endif

// The list type UnlinkAll is named after: the oracle's link folded every
// instantiation onto this one.
struct CTRLNODE;
typedef TSList<CTRLNODE, TSGetLink<CTRLNODE> > CTRLNODELIST;

// The bodies, in oracle VA order.

// Unlink one TSLink from whatever chain holds it. The zero-initialised offset
// and its redundant `> 0` test keep 1.17's shape, `lea eax, [esi+4h]` in the
// else arm.
template <class T>
static void fd_unlink(TSLink<T> *pl)
{
  T *pprev = pl->prev;
  if (pprev != 0) {
    T *pnext = pl->next;
    TSLink<T> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<T> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0)
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      pnl = reinterpret_cast<TSLink<T> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = 0;
    pl->next = 0;
  }
}

// Drain the inner node list, then unlink the list's own sentinel and the
// session's link. UnlinkAll is called through CTRLNODELIST because the oracle's
// link folded every instantiation onto that one; the pList local keeps the
// field offsets short.
TFD_SESSION::~TFD_SESSION() {
  TSList<TUSER_INFO, TSGetLink<TUSER_INFO> > *pList = &nodes;
  reinterpret_cast<CTRLNODELIST *>(pList)->UnlinkAll();
  fd_unlink(&pList->link);
  fd_unlink(&link);
}

// Transfer-packet dispatch. The seven arms tail-jump into fd_user.cpp through a
// jump table at 0x44070C.
BOOL on_fd_transfer(void *pState, intptr_t nArg1, int nArg2) {
  if (pState != NULL && nArg1 != 0) {
    switch (*reinterpret_cast<BYTE *>(pState)) {
      case 0: return sub_440DB0(reinterpret_cast<intptr_t>(pState), nArg1, static_cast<BYTE>(nArg2));
      case 1: return sub_441150(reinterpret_cast<intptr_t>(pState), reinterpret_cast<void *>(nArg1), nArg2);
      case 2: return sub_4415E0(reinterpret_cast<intptr_t>(pState), nArg1, static_cast<BYTE>(nArg2));
      case 3: return sub_441660(pState, nArg1, static_cast<BYTE>(nArg2));
      case 4: return sub_4413B0(reinterpret_cast<intptr_t>(pState), nArg1, nArg2);
      case 5: return sub_441520(reinterpret_cast<intptr_t>(pState), nArg1, nArg2);
      case 6: return sub_4415A0(pState, nArg1, nArg2);
    }
  }
  return 0;
}

// Drain every session, then every user. The user half is a tail jump. The head
// is normalised to NULL and tested on its own, which keeps MSVC from rotating
// the loop.
void fd_cleanup() {
  for (;;) {
    TFD_SESSION *pSession = gFDSessions.link.next;
    if (reinterpret_cast<intptr_t>(pSession) <= 0)
      pSession = NULL;
    if (!pSession)
      break;
    delete_fd_session(reinterpret_cast<intptr_t>(pSession));
  }
  clear_fd_users();
}

// Register the download callbacks and the two name strings after a full
// teardown. The copy loop tests the caller's array with the loop counter but
// indexes it with the global count; both stay live.
void set_fd_callbacks(BYTE bArg0, PFN_FD_SEND pfnSend, int nArg2, const char *pszDownloadDir,
                      const char **apszDirs) {
  const char **papszNames;

  fd_cleanup();
  gbFdLocalId = bArg0;
  byte_665F81 = 0;
  dword_665F84 = nArg2 - 9;
  gpfnFdSend = pfnSend;
  SStrCopy(sz_665F90, pszDownloadDir, 0x104);
  dword_665F8C = 0;
  papszNames = apszDirs;
  for (int i = 0; i < 2; ++i) {
    if (papszNames[i] == NULL)
      break;
    SStrCopy(sz_666098[dword_665F8C], papszNames[dword_665F8C], 0x104);
    ++dword_665F8C;
  }
}

// Free every node of the session's inner list, run the destructor, free the
// session.
void delete_fd_session(intptr_t nArg0) {
  TFD_SESSION *pSession = reinterpret_cast<TFD_SESSION *>(nArg0);

  if (pSession) {
    for (;;) {
      TUSER_INFO *pNode = pSession->nodes.link.next;
      if (reinterpret_cast<intptr_t>(pNode) <= 0)
        pNode = NULL;
      if (!pNode)
        break;
      TUSER_INFO *pPrev = pNode->link.prev;

      if (pPrev) {
        TUSER_INFO *pSlot = pNode->link.next;
        if (reinterpret_cast<intptr_t>(pSlot) <= 0) {
          pSlot = reinterpret_cast<TUSER_INFO *>(~reinterpret_cast<intptr_t>(pSlot));
        } else {
          pSlot = reinterpret_cast<TUSER_INFO *>(
              reinterpret_cast<char *>(pSlot) +
              (reinterpret_cast<char *>(pNode) -
               reinterpret_cast<char *>(pPrev->link.next)));
        }
        *reinterpret_cast<TUSER_INFO **>(pSlot) = pPrev;
        pNode->link.prev->link.next = pNode->link.next;
        pNode->link.prev = NULL;
        pNode->link.next = NULL;
      }
      SMemFree(pNode, "delete", -1, 0);
    }
    pSession->~TFD_SESSION();
    SMemFree(pSession, "delete", -1, 0);
  }
}

// Free the user's owned block, unlink the record and free it. The terminator is
// a complemented slot address, so the predecessor repair is branchy and the
// link offset is recovered at runtime rather than folded; see TSList.h.
void delete_fd_user(intptr_t nArg0) {
  TFD_USER *pUser = reinterpret_cast<TFD_USER *>(nArg0);

  if (pUser) {
    if (pUser->pOwned) {
      SMemFree(pUser->pOwned, "fd.cpp", 0x42, 0);
      pUser->pOwned = NULL;
    }
    TFD_USER *pPrev = pUser->link.prev;
    if (pPrev) {
      TFD_USER *pSlot = pUser->link.next;
      if (reinterpret_cast<intptr_t>(pSlot) <= 0) {
        pSlot = reinterpret_cast<TFD_USER *>(~reinterpret_cast<intptr_t>(pSlot));
      } else {
        pSlot = reinterpret_cast<TFD_USER *>(
            reinterpret_cast<char *>(pSlot) +
            (reinterpret_cast<char *>(pUser) -
             reinterpret_cast<char *>(pPrev->link.next)));
      }
      *reinterpret_cast<TFD_USER **>(pSlot) = pPrev;
      pUser->link.prev->link.next = pUser->link.next;
      pUser->link.prev = NULL;
      pUser->link.next = NULL;
    }
    SMemFree(pUser, "delete", -1, 0);
  }
}

// delete_fd_user over the whole user list; the oracle inlines it and re-reads
// the head each pass.
void clear_fd_users() {
  for (;;) {
    TFD_USER *pUser = gFDUsers.link.next;
    if (reinterpret_cast<intptr_t>(pUser) <= 0)
      pUser = NULL;
    if (!pUser)
      break;
    delete_fd_user(reinterpret_cast<intptr_t>(pUser));
  }
}
