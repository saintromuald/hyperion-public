// net_sync.h -- declarations for src117/lang/net_sync.cpp: the turn-sync and
// version-hash traffic that keeps every machine's game state agreeing.
// SaveMinimapCounts is declared in gameloop.h. No explicit __fastcall: /Gr
// already makes it the default.

#ifndef OC117_NET_SYNC_H
#define OC117_NET_SYNC_H

#include "platform.h"

#pragma pack(push, 1)

// One per-slot sync record, sixteen of them at 0x673F28. Only the fields these
// bodies touch are named.
struct NETSYNCREC {
  /*0x000*/ WORD wTurn;
  /*0x002*/ BYTE bSeq;
  /*0x003*/ BYTE b03;
  /*0x004*/ BYTE b04;
  /*0x005*/ BYTE b05;
  /*0x006*/ BYTE _pad_06[2];
  /*0x008*/ DWORD dw08;
  /*0x00C*/ BYTE abRun[0x100];
};

// The seven bytes packet_sync queues and net_recv_sync reads back.
struct NETSYNCPKT {
  /*0x00*/ BYTE bCmd;
  /*0x01*/ BYTE bSeq;
  /*0x02*/ WORD wTurn;
  /*0x04*/ BYTE b04;
  /*0x05*/ BYTE b05;
  /*0x06*/ BYTE b06;
};

#pragma pack(pop)

static_assert(sizeof(NETSYNCREC) == 0x10C, "imul edx, eax, 10Ch in packet_sync");
static_assert(offsetof(NETSYNCREC, wTurn) == 0x000, "mov ax, word ptr [edx]");
static_assert(offsetof(NETSYNCREC, bSeq) == 0x002, "cmp byte ptr [ecx+673F2Ah], bl");
static_assert(offsetof(NETSYNCREC, b03) == 0x003, "movzx eax, byte ptr [edx+3h]");
static_assert(offsetof(NETSYNCREC, b04) == 0x004, "mov byte ptr [esi+4h], al in UpdateGameHash");
static_assert(offsetof(NETSYNCREC, b05) == 0x005, "movzx eax, byte ptr [edx+5h]");
static_assert(offsetof(NETSYNCREC, dw08) == 0x008, "SnapshotSightHash stores a whole dword through it");
static_assert(offsetof(NETSYNCREC, abRun) == 0x00C, "lea esi, [esi+673F34h] then a byte walk");
static_assert(sizeof(NETSYNCPKT) == 7, "netmgr_queue_cmd's literal length");
static_assert(offsetof(NETSYNCPKT, bCmd) == 0x00, "mov byte ptr [ebp-8h], 37h");
static_assert(offsetof(NETSYNCPKT, b04) == 0x04, "mov byte ptr [ebp-4h], al");
static_assert(offsetof(NETSYNCPKT, b05) == 0x05, "mov byte ptr [ebp-3h], al");
static_assert(offsetof(NETSYNCPKT, b06) == 0x06, "mov byte ptr [ebp-2h], al");
static_assert(offsetof(NETSYNCPKT, bSeq) == 0x01, "mov bl, byte ptr [esi+1h]");
static_assert(offsetof(NETSYNCPKT, wTurn) == 0x02, "cmp ax, word ptr [esi+2h]");

// 0x47A250 (61 B). Nullary; the one caller reads the full EAX.
int sub_47A250();

// 0x47A290 (90 B). Nullary, the same shape as the row above.
int sub_47A290();

// 0x47A300 (95 B). Nullary and void. netmgr_set_sync and netmgr_send_turn both
// end in a tail jump to it, so their returns follow this one.
void packet_sync();

// 0x47A360 (147 B). ECX is a BYTE; the one caller narrows EAX to 16 bits.
WORD GetVersionSyncHash(BYTE bArg0);

// 0x47A400 (139 B). Nullary and void. netmgr_set_sync calls it on the enabling
// arm before the tail jump.
void sync_init();

// 0x47A490 (127 B). Nullary and void. ProgressNetwork calls it.
void UpdateGameHash();

// 0x47A510 (83 B). ECX is the packet; returns 0 or 1.
BOOL net_recv_sync(const NETSYNCPKT *pPkt);

// 0x47A570 (145 B). ECX is the slot and EDX the packet net_recv_sync forwards.
int sub_47A570(int nSlot, const NETSYNCPKT *pPkt);

#endif  // OC117_NET_SYNC_H
