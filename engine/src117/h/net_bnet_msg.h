// net_bnet_msg.h -- declarations for src117/lang/net_bnet_msg.cpp, the
// Battle.net server and league message queue.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_NET_BNET_MSG_H
#define OC117_NET_BNET_MSG_H

#include "platform.h"
#include "TSList.h"

// One queued Battle.net message, 0x88 bytes: the link at offset 0 and up to
// 0x80 bytes of text.
struct TBNETMESSAGE {
  /*0x00*/ TSLink<TBNETMESSAGE> link;
  /*0x08*/ char szText[0x80];
};

OC_X86_LAYOUT(sizeof(TBNETMESSAGE) == 0x88, "CreateBNetMessage's `push 88h`");
static_assert(offsetof(TBNETMESSAGE, link) == 0x00, "the node pointer IS the link");
OC_X86_LAYOUT(offsetof(TBNETMESSAGE, szText) == 0x08, "lea eax, [edi+8h] before memcpy");

// The message list, spelled as the ledger spells its two members.
template <> struct TSList<TBNETMESSAGE,TSGetLink<TBNETMESSAGE> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<TBNETMESSAGE> link;
  // 0x46FC60 (84 B). Unlinks and frees every message.
  void Clear(void);
  // 0x46FCC0 (103 B), `ret 4h`. Unlinks and frees pNode; returns its successor.
  TBNETMESSAGE *DeleteNode(TBNETMESSAGE *pNode);
};
typedef TSList<TBNETMESSAGE, TSGetLink<TBNETMESSAGE> > TBNETMESSAGELIST;

static_assert(offsetof(TBNETMESSAGELIST, _unmapped_00) == 0x00, "TSList.h layout");
static_assert(offsetof(TBNETMESSAGELIST, link) == 0x04, "Clear reads [this+8] as link.next");
OC_X86_LAYOUT(sizeof(TBNETMESSAGELIST) == 12, "0x508D38 .. 0x508D44");

// 0x46FD30 (10 B). Nullary; it ends in a tail jump to the list's Clear.
void ClearBNetMessages();

// 0x470070 (209 B). ECX is the text to queue.
void CreateBNetMessage(const char *pszText);

// One Storm event record, as an SNetRegisterEventHandler callback receives it.
// Only pszData at +8 is read here; the other three slots are named for their
// shape.
struct SNETEVENT {
  /*0x00*/ DWORD dwType;
  /*0x04*/ DWORD dwPlayerId;
  /*0x08*/ char *pszData;
  /*0x0C*/ DWORD cbData;
};

// 0x46FD40 (69 B). `ret 4h`, reached only through the handler slot it is
// registered into, so __stdcall.
void __stdcall snet_servermessage_cb(SNETEVENT *pEvent);

#endif  // OC117_NET_BNET_MSG_H
