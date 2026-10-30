// _GAMENODE.h -- the join-game screen's advertised-game list node and the list that
// holds them, both used by gluJoin.cpp. Each offset's assert names the instruction
// that fixes it.

#ifndef OC117__GAMENODE_H
#define OC117__GAMENODE_H

#include <stddef.h>

#include "platform.h"
#include "TSList.h"

// The list node; its size is _GAMENODE::Create's own `push 58h` to SMemAlloc.
#pragma pack(push, 1)
struct _GAMENODE {
  /*0x00*/ TSLink<_GAMENODE> link;
  // psnprintf(node + 8, 41h, ...) twice in Create, and sub_4590D0 strcmps it.
  /*0x08*/ char szText[0x41];
  /*0x49*/ BYTE _unmapped_49[0x4C - 0x49];
  // Create copies [pInfo] here and compares it to find an existing node;
  // gluJoin_gamelist_setval matches the remembered selection against it.
  /*0x4C*/ DWORD dwGameId;
  // Set to 1 when the node is refreshed; sub_459640 frees every node still 0.
  /*0x50*/ DWORD dwSeen;
  /*0x54*/ BYTE bListIndex;          // lbox_add_item's return
  /*0x55*/ BYTE _unmapped_55[3];

  // The enumeration callback ListGames runs per advertised game. `ret 8h`
  // with no register input, so __stdcall rather than /Gr's default.
  static void __stdcall Create(intptr_t nArg0, intptr_t nArg1);
};
#pragma pack(pop)
OC_X86_LAYOUT(sizeof(_GAMENODE) == 0x58, "Create's push 58h to SMemAlloc");
static_assert(offsetof(_GAMENODE, link) == 0x00, "TSList threads the node by its first field");
OC_X86_LAYOUT(offsetof(_GAMENODE, szText) == 0x08, "lea ebx, [esi+8]");
OC_X86_LAYOUT(offsetof(_GAMENODE, dwGameId) == 0x4C, "mov [esi+4Ch], eax");
OC_X86_LAYOUT(offsetof(_GAMENODE, dwSeen) == 0x50, "mov [esi+50h], 1");
OC_X86_LAYOUT(offsetof(_GAMENODE, bListIndex) == 0x54, "mov [esi+54h], al");

// The list of advertised games.
#pragma pack(push, 1)
template <> struct TSList<_GAMENODE,TSGetLink<_GAMENODE> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<_GAMENODE> link;
  // 0x459000 (84 B). Unlinks and frees every node.
  void Clear(void);
  // 0x459060 (103 B), `ret 4h`.
  _GAMENODE *DeleteNode(_GAMENODE *pNode);
};
#pragma pack(pop)
typedef TSList<_GAMENODE,TSGetLink<_GAMENODE> > GAMENODELIST;
OC_X86_LAYOUT(sizeof(GAMENODELIST) == 12, "TSList.h layout");
static_assert(offsetof(GAMENODELIST, _unmapped_00) == 0x00, "TSList.h layout");
static_assert(offsetof(GAMENODELIST, link) == 0x04, "every walk reads [0x50811C], +8");

#endif  // OC117__GAMENODE_H
