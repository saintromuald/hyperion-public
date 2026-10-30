// fd_sessn.cpp -- the download session list, oracle 0x440950-0x440DAF.
// Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers on some declarations are for.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>

#include "../h/fd_sessn.h"
#include "../h/fd_user.h"
#include "../h/Storm.h"
#include "../h/rtti.h"
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// The RTTI type descriptor SMemAlloc's owner tag comes from. Its bytes at
// 0x51076C are the type_info vftable, a zero dword, then ".?AUTUSER_INFO@@".
#ifdef OC_DLL_MODE
  #ifndef gTypeInfo_TUSER_INFO
  #define gTypeInfo_TUSER_INFO (*reinterpret_cast<type_info *>(OC_VA(0x51076Cu)))
  #endif
#else
extern type_info gTypeInfo_TUSER_INFO;
#endif

// The TFD_SESSION RTTI descriptor, ".?AUTFD_SESSION@@" at 0x510788.
#ifdef OC_DLL_MODE
  #ifndef gTypeInfo_TFD_SESSION
  #define gTypeInfo_TFD_SESSION (*reinterpret_cast<type_info *>(OC_VA(0x510788u)))
  #endif
#else
extern type_info gTypeInfo_TFD_SESSION;
#endif

// 0x665F81 is the rolling session id set_fd_callbacks zeroes; 0xFF means the
// table is full.
#ifdef OC_DLL_MODE
  #ifndef byte_665F81
  #define byte_665F81 (*reinterpret_cast<BYTE *>(OC_VA(0x665F81u)))
  #endif
#else
extern BYTE byte_665F81;
#endif

// sub_440BE0 tail-jumps to sub_440950 rather than inlining it; the declaration
// this TU compiles against has to say so.
__declspec(noinline) void sub_440950(TUSER_INFO *pNode);

// The bodies, in oracle VA order.

// Unlink one node and free it. The terminator is a complemented slot address,
// so the predecessor repair is branchy and the link offset is recovered at
// runtime rather than folded; see TSList.h.
void sub_440950(TUSER_INFO *pNode) {
  if (pNode) {
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
}

// Set flags on the session's +0x0C word.
// Hyperion: the
// payload has it too, so its callers no longer go to the original's
//.
void set_fd_session_flags(void *pArg0, int nArg1) {
  reinterpret_cast<TFD_SESSION *>(pArg0)->dwUnmapped_00C |= nArg1;
}


// TSLink's constructor: both pointers zeroed. A separate forced-inline helper,
// because the stores it makes are often dead and survive only across that
// boundary (NewNode below).
template <class T> static __forceinline void ZeroLink(TSLink<T> *l) {
  l->prev = NULL;
  l->next = NULL;
}

// ListLinkNode, TSList::LinkNode's body, is in src117/h/TSList.h: the session
// list and gluLogin's ID list share it.


typedef TSList<TUSER_INFO, TSGetLink<TUSER_INFO> > TUSERLIST;

// TSList<TUSER_INFO,...>::NewNode, inlined into its one caller: placement new
// (TUSER_INFO's constructor is its link's), then LinkNode when dwWhere is set.
static __forceinline TUSER_INFO *InitUser(TUSER_INFO *q) {
  ZeroLink(&q->link);
  return q;
}
static __forceinline TUSER_INFO *NewUser(TUSERLIST *self, ULONG dwWhere,
                                         int nExtra, DWORD dwFlags) {
  void *pv = SMemAlloc(nExtra + sizeof(TUSER_INFO), gTypeInfo_TUSER_INFO.raw_name(), -2,  // Hyperion: 0x10 in 1.17
                       dwFlags | 8);
  TUSER_INFO *p = pv ? InitUser(reinterpret_cast<TUSER_INFO *>(pv)) : NULL;
  if (dwWhere != 0)
    ListLinkNode(self, p, dwWhere, static_cast<TUSER_INFO *>(NULL));
  return p;
}

// Add a node for this id unless one already exists.
void sub_4409B0(TFD_SESSION *pSession, BYTE bArg1) {
  for (TUSER_INFO *p = pSession->nodes.link.next; reinterpret_cast<intptr_t>(p) > 0;
       p = p->link.next)
    if (p->bId == bArg1)
      return;

  TUSER_INFO *pNode = NewUser(&pSession->nodes, 1, 0, 0);
  pNode->bId = bArg1;
  pNode->bState = 0x10;
  pNode->dwUnmapped_0C = 0;
  sub_4416A0(bArg1, reinterpret_cast<intptr_t>(pSession));
}

// Open a session for a map path: reject a basename of 0x20 or more, a path with
// no map extension, a full id table, and a header read that yields nothing.
intptr_t sub_440AB0(intptr_t nArg0) {
  const char *pszPath = reinterpret_cast<const char *>(nArg0);
  const char *pszName;
  TFD_SESSION *pSession;
  int nA;
  int nB;

  pszName = strrchr(pszPath, '\\');
  if (pszName == NULL)
    pszName = pszPath;
  else
    pszName++;
  if (strlen(pszName) >= 0x20)
    goto fail;
  if (!fd_has_map_extension(nArg0))
    goto fail;
  if (byte_665F81 == 0xFF)
    goto fail;
  if (!sub_440E70(nArg0, reinterpret_cast<intptr_t>(&nA), reinterpret_cast<intptr_t>(&nB)))
    goto fail;
  if (nA == 0)
    goto fail;
  pSession = gFDSessions.NewNode(1, 0, 0);
  pSession->bId = byte_665F81;
  ++byte_665F81;
  pSession->dwUnmapped_00C = 0;
  pSession->dwSize = nA;
  pSession->dwCrc = nB;
  SStrCopy(pSession->szPath, pszPath, 0x104);
  return reinterpret_cast<intptr_t>(pSession);
fail:
  return 0;
}

// Notify every node of the session's teardown, delete the session and clear the
// caller's slot.
void sub_440B60(intptr_t nArg0) {
  TFD_SESSION **ppSession = reinterpret_cast<TFD_SESSION **>(nArg0);
  TFD_SESSION *pSession = *ppSession;

  for (TUSER_INFO *p = pSession->nodes.link.next; reinterpret_cast<intptr_t>(p) > 0;
       p = p->link.next)
    sub_441890(p->bId, pSession->bId);
  delete_fd_session(reinterpret_cast<intptr_t>(pSession));
  *ppSession = NULL;
}

// True when the session carries flags and every node is in state 1 against the
// session's +0x10 word. Both failing arms share the entry guard's exit.
BOOL IsSessionComplete(const TFD_SESSION *pSession) {
  if (pSession->dwUnmapped_00C != 0)
    return 0;
  for (TUSER_INFO *p = pSession->nodes.link.next; reinterpret_cast<intptr_t>(p) > 0;
       p = p->link.next) {
    if (p->bState != 1 ||
        static_cast<int>(p->dwUnmapped_0C) != pSession->dwSize)
      return 0;
  }
  return 1;
}

// Clear flags on the session's +0x0C word.
void sub_440BD0(void *pArg0, int nArg1) {
  reinterpret_cast<TFD_SESSION *>(pArg0)->dwUnmapped_00C &= ~nArg1;
}

// Drop the session's node with this id; the free is a tail jump.
void sub_440BE0(void *pArg0, BYTE bArg1) {
  TFD_SESSION *pSession = reinterpret_cast<TFD_SESSION *>(pArg0);

  for (TUSER_INFO *p = pSession->nodes.link.next; reinterpret_cast<intptr_t>(p) > 0;
       p = p->link.next) {
    if (p->bId == bArg1) {
      sub_440950(p);
      return;
    }
  }
}


// Unlinks pNode and links it at the head or before pBefore.
void TFD_SESSION_LIST::LinkNode(TFD_SESSION *pNode, ULONG dwWhere,
                                TFD_SESSION *pBefore) {
  ListLinkNode(this, pNode, dwWhere, pBefore);
}

// The constructors NewNode's placement new runs: each TSLink is zeroed, then
// the inner list's constructor primes its terminator.
static __forceinline void InitUserList(TUSERLIST *l) {
  ZeroLink(&l->link);
  l->link.prev = reinterpret_cast<TUSER_INFO *>(&l->link);
  l->_unmapped_00 = 0;
  l->link.next = reinterpret_cast<TUSER_INFO *>(~reinterpret_cast<intptr_t>(&l->link));
}
static __forceinline TFD_SESSION *InitSession(TFD_SESSION *q) {
  ZeroLink(&q->link);
  InitUserList(&q->nodes);
  return q;
}
// Allocate a session plus the caller's tail, run its constructor and link it
// when dwWhere is nonzero. The allocation tag is the type's own RTTI raw name.
TFD_SESSION *TFD_SESSION_LIST::NewNode(ULONG dwWhere, int nExtra,
                                    DWORD dwFlags) {
  void *pv = SMemAlloc(nExtra + sizeof(TFD_SESSION), gTypeInfo_TFD_SESSION.raw_name(), -2,  // Hyperion: 0x128 in 1.17
                       dwFlags | 8);
  TFD_SESSION *p = pv ? InitSession(reinterpret_cast<TFD_SESSION *>(pv)) : NULL;
  if (dwWhere != 0)
    LinkNode(p, dwWhere, NULL);
  return p;
}

// Pair the first node that disagrees with the session's +0x10 word against the
// first that agrees, hand them to sub_441850 and rescan from the head. The head
// is normalised to NULL before its test, which stops MSVC copying the rescan's
// head block onto the back edge.
void sub_440D40(intptr_t nArg0) {
  TFD_SESSION *pSession = reinterpret_cast<TFD_SESSION *>(nArg0);

  for (;;) {
    TUSER_INFO *pOdd = NULL;
    TUSER_INFO *pEven = NULL;
    TUSER_INFO *p = pSession->nodes.link.next;
    if (reinterpret_cast<intptr_t>(p) <= 0)
      p = NULL;
    if (p == NULL)
      return;
    do {
      if (pOdd == NULL && p->bState == 1 &&
          static_cast<int>(p->dwUnmapped_0C) != pSession->dwSize)
        pOdd = p;
      if (pEven == NULL && p->bState == 1 &&
          static_cast<int>(p->dwUnmapped_0C) == pSession->dwSize)
        pEven = p;
      if (pOdd != NULL && pEven != NULL)
        goto found;
      p = p->link.next;
    } while (reinterpret_cast<intptr_t>(p) > 0 && p != NULL);
    return;
  found:
    sub_441850(pEven->bId, pSession->bId, pOdd);
    pOdd->bState = 4;
    pEven->bState = 2;
  }
}
