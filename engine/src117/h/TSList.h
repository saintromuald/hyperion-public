// TSList.h -- Storm's intrusive doubly-linked list template, inlined into its
// callers in gametype.cpp, gluBN.cpp, gluJoin.cpp, snd.cpp, dlg_dlg.cpp,
// saveload.cpp and more. A different type from CLink.h's, the game's own list.
//
// The last node's `next` holds ~(&list.link.prev), the bitwise complement of the
// list's head slot, not NULL. Walks end on a signed `p <= 0`, never on
// `p != NULL`, and an empty list starts in that state. To repair the
// predecessor's slot, unlink takes the complement at the terminator, or adds the
// link offset recovered at run time (the link's address minus the predecessor's
// pointer to it). The link sits at offset 0 in every T seen, so that term is
// zero, but 1.17 computes it and so does the source; do not fold it to offsetof.

#ifndef OC117_TSLIST_H
#define OC117_TSLIST_H

#include "platform.h"

#pragma pack(push, 1)

// The intrusive link, at offset 0 of every T seen so far.
template <class T>
struct TSLink {
  /*0x00*/ T *prev;
  /*0x04*/ T *next;   // or ~(&owner.link.prev) on the last node -- see above
};

// The link accessor policy, part of the instantiations' mangled names.
template <class T>
struct TSGetLink {
  static TSLink<T> *GetLink(T *p) { return &p->link; }
};

// The list head. 12 bytes: a route-selecting DWORD at +0, the link at +4.
template <class T, class TGetLink>
struct TSList {
  // LinkNode and sub_4409B0 both `test` this and `jns`: taken uses a zero link
  // offset, fallthrough recovers it from the data. Every list here holds 0.
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<T> link;

  // Defined by the TU that owns each instantiation, not here (Clear for
  // TTEMPLATE is 0x447650, in gametype.cpp).
  void Clear(void);

  // Walks the list and unlinks every node without freeing one. Out of line; the
  // linker folded every instantiation onto the CTRLNODE body at 0x4C4760.
  void UnlinkAll(void);

  // Unlinks pNode from wherever it is, then links it per dwWhere and pBefore.
  // dwWhere is a mode, not a count: gluLogin passes 2, and NewNode forwards its
  // own, which sub_440AB0 sets to 1. ULONG, not DWORD, as the mangled name has it.
  void LinkNode(T *pNode, ULONG dwWhere, T *pBefore);

  // Unlinks pNode, frees it through SMemFree and returns what followed it, or
  // NULL at the terminator. `ret 4h`, so __thiscall with one stack argument.
  T *DeleteNode(T *pNode);

  // Allocates sizeof(T) + nExtra, primes the link and the node's own inner
  // list, then LinkNode(pNew, dwWhere, NULL) when dwWhere is nonzero.
  T *NewNode(ULONG dwWhere, int nExtra, DWORD dwFlags);
};

#pragma pack(pop)

// offsetof needs a concrete type, and a comma in a template argument list splits
// the macro's arguments, so the asserts go through typedefs in a namespace. T
// stays incomplete: it appears only behind pointers, so the layout holds for
// every TSLink<T>. gametype.h asserts the concrete pair for TTEMPLATE.
namespace tslist_layout {
struct T;
typedef ::TSLink<T> TSLink;
typedef ::TSList<T, ::TSGetLink<T> > TSList;

OC_X86_LAYOUT(sizeof(TSLink) == 8, "two pointers, and no third field");
static_assert(offsetof(TSLink, prev) == 0x00, "Clear reads [node] as prev");
OC_X86_LAYOUT(offsetof(TSLink, next) == 0x04, "every walk reads [p+4]");
OC_X86_LAYOUT(sizeof(TSList) == 12, "0x5033C4 .. the next named global 0x5033D0");
static_assert(offsetof(TSList, link) == 0x04, "gametype_exit passes 0x5033C4 as "
                                              "this; Clear reads [this+8]");
}  // namespace tslist_layout

// TSList::LinkNode for the session list (fd_sessn), the user list sub_4409B0
// inlines and gluLogin's ID list; 1.17 keeps one folded copy at 0x440C10.
// Unlinks the node, then splices it in: dwWhere 1 at the head, anything else
// before pBefore. A negative +0 word means the link offset is recovered from the
// data. `pLink->prev` is re-read rather than cached, and the dispatch is a switch
// (its case chain decrements the parameter in place).
template <class L, class T>
static __forceinline void ListLinkNode(L *self, T *pNode, ULONG dwWhere,
                                       T *pBefore) {
  TSLink<T> *pLink = pNode ? &pNode->link : &self->link;
  TSLink<T> *pWhere;

  if (pLink->prev != NULL) {
    T *pSlot = pLink->next;
    if (reinterpret_cast<intptr_t>(pSlot) <= 0) {
      pSlot = reinterpret_cast<T *>(~reinterpret_cast<intptr_t>(pSlot));
    } else {
      pSlot = reinterpret_cast<T *>(
          reinterpret_cast<char *>(pSlot) +
          (reinterpret_cast<char *>(pLink) -
           reinterpret_cast<char *>(pLink->prev->link.next)));
    }
    *reinterpret_cast<T **>(pSlot) = pLink->prev;
    pLink->prev->link.next = pLink->next;
    pLink->prev = NULL;
    pLink->next = NULL;
  }
  pWhere = pBefore ? &pBefore->link : &self->link;
  switch (dwWhere) {
  case 1: {
    T *pNext;
    intptr_t nOff;

    pLink->prev = reinterpret_cast<T *>(pWhere);
    pLink->next = pWhere->next;
    pNext = pWhere->next;
    nOff = self->_unmapped_00;
    if (reinterpret_cast<intptr_t>(pNext) <= 0) {
      pNext = reinterpret_cast<T *>(~reinterpret_cast<intptr_t>(pNext));
    } else {
      if (nOff < 0)
        nOff = reinterpret_cast<char *>(pWhere) -
               reinterpret_cast<char *>(pWhere->prev->link.next);
      pNext = reinterpret_cast<T *>(reinterpret_cast<char *>(pNext) + nOff);
    }
    *reinterpret_cast<TSLink<T> **>(pNext) = pLink;
    pWhere->next = pNode;
    break;
  }
  default: {
    T *pIns = pWhere->prev;

    pLink->prev = pIns;
    pLink->next = pIns->link.next;
    pIns->link.next = pNode;
    pWhere->prev = reinterpret_cast<T *>(pLink);
    break;
  }
  }
}

#endif  // OC117_TSLIST_H
