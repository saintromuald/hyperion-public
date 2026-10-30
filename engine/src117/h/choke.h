// choke.h -- declarations for src117/lang/choke.cpp: the area-path fixup list
//
// find_choke_regions is not repeated here; src117/h/sai_TownBuilder.h declares it.
//
// The fixup list is a Storm TSList, so its terminator is a complemented address
// and not NULL -- read src117/h/TSList.h before touching any walk here.

#ifndef OC117_CHOKE_H
#define OC117_CHOKE_H

#include "platform.h"

#include "TSList.h"

struct TAiPathReq;

// One pending edit to a region's path list, held until realize_fixups writes it
// back. 0xC bytes: CreateNodes asks SMemAlloc for `2 * bPathCount + 0Ch` and
// then writes the area at +8 and the paths from +0Ah.
struct TAreaFixup {
  /*0x00*/ TSLink<TAreaFixup> link;
  /*0x08*/ WORD wArea;       // `mov word ptr [esi+8h], ax`
  /*0x0A*/ WORD awPaths[1];  // `lea eax, [esi+0Ah]` as memcpy's source

  // 0x40E2F0. Static: ECX is a WORD area id, not a `this` (`mov ax, cx` indexes
  // a 64-byte table). void -- the one caller discards EAX.
  static void CreateNodes(WORD wArea);
};
OC_X86_LAYOUT(offsetof(TAreaFixup, wArea) == 0x08, "TAreaFixup::wArea");
OC_X86_LAYOUT(offsetof(TAreaFixup, awPaths) == 0x0A, "TAreaFixup::awPaths");
OC_X86_LAYOUT(sizeof(TAreaFixup) == 0x0C, "CreateNodes: lea eax, [esi*2+0Ch]");

// The pending-fixup list.
#pragma pack(push, 1)
template <> struct TSList<TAreaFixup,TSGetLink<TAreaFixup> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<TAreaFixup> link;
  // 0x40E120, `ret 4h`. Unlinks and frees pNode; returns its successor.
  TAreaFixup *DeleteNode(TAreaFixup *pNode);
};
#pragma pack(pop)
typedef TSList<TAreaFixup, TSGetLink<TAreaFixup> > TAREAFIXUPLIST;
OC_X86_LAYOUT(sizeof(TAREAFIXUPLIST) == 12, "TSList.h layout");

// 0x40E480. Nullary; the callers discard EAX.
void realize_fixups();

// 0x40E4E0, `ret 8h`. ECX is the path request, EDX a region index; the two stack
// words are the WORD this writes the best region into and the index it starts
// from. The callers consume the full EAX.
int sub_40E4E0(TAiPathReq *pReq, int nLast, WORD *pwOut, int nFirst);

#endif  // OC117_CHOKE_H
