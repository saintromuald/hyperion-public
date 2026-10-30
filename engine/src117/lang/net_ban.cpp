// net_ban.cpp -- the multiplayer ban list: names typed into the chat room's ban
// command are kept in a Storm intrusive list and every joining player is
// checked against it. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/net_ban.h"
#include "../h/TSList.h"
#include "../h/Storm.h"   // SMemAlloc, SStrCopy
#include "../h/rtti.h"    // type_info::raw_name, not <typeinfo>'s
#include "../gen/globals117.h"

#include <string.h>   // _stricmp, which the name compare goes through

// One banned name, 0x24 bytes with the link at offset 0. The tail padding is
// spelled out because TSList.h packs TSLink to 1, so alignment alone does not
// round the node up to 0x24.
struct TBAN {
  /*0x00*/ TSLink<TBAN> link;
  /*0x08*/ char szName[0x19];
  /*0x21*/ char _pad_21[3];
};

OC_X86_LAYOUT(sizeof(TBAN) == 0x24, "AddBannedPlayer's `push 24h` to SMemAlloc");
static_assert(offsetof(TBAN, link) == 0x00, "the node pointer IS the link");
OC_X86_LAYOUT(offsetof(TBAN, szName) == 0x08, "lea eax, [esi+8h] before SStrCopy");
OC_X86_LAYOUT(offsetof(TBAN, _pad_21) == 0x21, "the name field ends at 0x21");

typedef TSList<TBAN, TSGetLink<TBAN> > TBANLIST;

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x508D2C -- the ban list, 12 bytes, with link.prev at 0x508D30 and link.next
// at 0x508D34. The static constructor stores only next, the complement of the
// link's address; prev is initialised data.
#ifdef OC_DLL_MODE
#define gBanList_g (*reinterpret_cast<TBANLIST *>(OC_VA(0x508D2Cu)))
#else
extern TBANLIST gBanList_g;
#endif

// 0x51095C -- the RTTI descriptor for TBAN, whose raw name is the allocator tag.
#ifdef OC_DLL_MODE
#define gTypeInfo_TBAN (*reinterpret_cast<type_info *>(OC_VA(0x51095Cu)))
#else
extern type_info gTypeInfo_TBAN;
#endif

// The step accessor reads the list's negative terminator as NULL. The first
// node has none, because 1.17 tests it without normalizing.

static TBAN *ban_next(TBAN *pBan)
{
  return reinterpret_cast<intptr_t>(pBan->link.next) > 0 ? pBan->link.next : NULL;
}

// The bodies, in oracle VA order.

// Placement construction of one node: both link fields zeroed.
static __forceinline TBAN *InitBan(TBAN *q)
{
  q->link.prev = NULL;
  q->link.next = NULL;
  return q;
}

// 0x46FB10 (148 B). Appends a name to the ban list: the TSList NewNode and
// LinkNode inlined, including the arm that unlinks the list's own link when the
// allocation failed.
BOOL AddBannedPlayer(const char *pszName)
{
  void *pv = SMemAlloc(sizeof(TBAN), gTypeInfo_TBAN.raw_name(), -2, 8);
  TBAN *pNode = pv ? InitBan(static_cast<TBAN *>(pv)) : NULL;
  TSLink<TBAN> *pLink = pNode ? &pNode->link : &gBanList_g.link;

  if (pLink->prev != NULL) {
    TBAN *pSlot = pLink->next;
    if (reinterpret_cast<intptr_t>(pSlot) <= 0) {
      pSlot = reinterpret_cast<TBAN *>(~reinterpret_cast<intptr_t>(pSlot));
    } else {
      pSlot = reinterpret_cast<TBAN *>(
          reinterpret_cast<char *>(pSlot) +
          (reinterpret_cast<char *>(pLink) -
           reinterpret_cast<char *>(pLink->prev->link.next)));
    }
    *reinterpret_cast<TBAN **>(pSlot) = pLink->prev;
    pLink->prev->link.next = pLink->next;
    pLink->prev = NULL;
    pLink->next = NULL;
  }
  {
    TBAN *pIns = gBanList_g.link.prev;

    pLink->prev = pIns;
    pLink->next = pIns->link.next;
    pIns->link.next = pNode;
    gBanList_g.link.prev = reinterpret_cast<TBAN *>(pLink);
  }
  SStrCopy(pNode->szName, pszName, 0x19);
  return TRUE;
}

// The unlink half of the list's DeleteNode, spliced into Clear. The `nOff = 0`
// pre-assignment is the shape the other lists in the tree use.
static void UnlinkBan(TBAN *p)
{
  TBAN *pprev = p->link.prev;
  if (pprev != 0) {
    TBAN *pnext = p->link.next;
    TSLink<TBAN> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<TBAN> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0)
        nOff = (intptr_t)&p->link - (intptr_t)pprev->link.next;
      pnl = reinterpret_cast<TSLink<TBAN> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    p->link.prev->link.next = p->link.next;
    p->link.prev = 0;
    p->link.next = 0;
  }
}

// 0x46FBB0 (84 B). Unlinks and frees every ban. The head is normalised to null
// before the test, as in 1.17.
void TBANLIST::Clear(void)
{
  for (;;) {
    TBAN *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkBan(p);
    SMemFree(p, gTypeInfo_TBAN.raw_name(), -2, 0);
  }
}

// 0x46FC10 (55 B). Walks the list and answers whether the name is on it. The
// loop test is signed: the last node's `next` holds the complement of the
// list's own head-slot address, a large negative number. An empty list holds
// that terminator too, so the first test is the same comparison.
BOOL IsPlayerBanned(const char *pszName)
{
  TBAN *pBan = gBanList_g.link.next;

  if (reinterpret_cast<intptr_t>(pBan) > 0) {
    do {
      if (_stricmp(pszName, pBan->szName) == 0)
        return TRUE;

      pBan = ban_next(pBan);
    } while (pBan != NULL);
  }

  return FALSE;
}

// 0x46FC50 (10 B). Drops every ban; a tail jump into the list's Clear.
void ClearBans()
{
  gBanList_g.Clear();
}
