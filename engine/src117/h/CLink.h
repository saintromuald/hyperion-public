// CLink.h -- the engine's intrusive doubly-linked list node and its four entry
// points in CLists.cpp, all __thiscall, each popping two stack arguments (ret 8).
//
// CImage's first two members are a prev/next pair at +0x00/+0x04, and Blizzard's
// CImage probably derives from CLink. Here it does not: deriving would move the
// pair into a base and make CImage non-standard-layout, where offsetof is only
// conditionally supported. Callers reinterpret_cast at the call site instead; with
// the pair at offset 0 the cast emits no instruction.
#pragma once

#include "platform.h"  // OC_X86_LAYOUT
#include <stddef.h>

// A node in one of the engine's intrusive lists. Only CLists.cpp reads the
// pair through a CLink *; everything else walks its own struct's pPrev/pNext.
class CLink {
public:
  /*0x00*/ CLink *pPrev;
  /*0x04*/ CLink *pNext;

  // 0x411970. Inserts pLink just before `this`, updating *ppHead when `this` was
  // the first node.
  void PrependLink(CLink *pLink, CLink **ppHead);

  // 0x411930. Inserts `this` into an empty list, setting both *ppHead and
  // *ppTail; the CSprite::Alloc* bodies call it only when pImageHead is NULL.
  void PrependThis(CLink **ppHead, CLink **ppTail);

  // 0x411900. Inserts pLink just after `this`, updating *ppTail when `this` was
  // the last node: PrependLink's mirror, taking the tail rather than the head.
  void AppendLink(CLink *pLink, CLink **ppTail);

  // 0x4119A0. Removes `this` from the list, fixing the head and tail if it was at
  // either end, and clears its own links. CSprite::Update and CSprite::Kill pass
  // a pair from two parallel global arrays (0x5C51D0, 0x5C55D0), so both
  // parameters are CLink ** rather than a list object.
  void Unlink(CLink **ppHead, CLink **ppTail);
};

static_assert(offsetof(CLink, pPrev) == 0x00, "CLink::pPrev");
OC_X86_LAYOUT(offsetof(CLink, pNext) == 0x04, "CLink::pNext");
OC_X86_LAYOUT(sizeof(CLink) == 0x08, "CLink size");
