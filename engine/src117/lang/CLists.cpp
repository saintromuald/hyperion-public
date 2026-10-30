// CLists.cpp -- the intrusive list primitives, all CLink members. Bodies are in
// oracle VA order.

#include "../h/CLists.h"
#include "../h/CLink.h"
#include "../gen/globals117.h"

// 0x411900. Links pLink in after this node, moving the tail when this was last.
void CLink::AppendLink(CLink *pLink, CLink **ppTail) {
  if (*ppTail == this)
    *ppTail = pLink;
  pLink->pPrev = this;
  pLink->pNext = pNext;
  if (pNext)
    pNext->pPrev = pLink;
  pNext = pLink;
}

// 0x411930. Links this node in after the head, or makes it the whole list when
// empty.
void CLink::PrependThis(CLink **ppHead, CLink **ppTail) {
  if (*ppHead) {
    (*ppHead)->AppendLink(this, ppTail);
  } else {
    *ppTail = this;
    *ppHead = this;
  }
}

// 0x411970. Links pLink in before this node, moving the head when this was
// first.
void CLink::PrependLink(CLink *pLink, CLink **ppHead) {
  if (*ppHead == this)
    *ppHead = pLink;
  pLink->pPrev = pPrev;
  pLink->pNext = this;
  if (pPrev)
    pPrev->pNext = pLink;
  pPrev = pLink;
}

// 0x4119A0. Unlinks this node, fixing either end of the list, and clears its
// links.
void CLink::Unlink(CLink **ppHead, CLink **ppTail) {
  if (*ppHead == this)
    *ppHead = pNext;
  if (*ppTail == this)
    *ppTail = pPrev;
  if (pPrev)
    pPrev->pNext = pNext;
  if (pNext)
    pNext->pPrev = pPrev;
  pPrev = NULL;
  pNext = NULL;
}
