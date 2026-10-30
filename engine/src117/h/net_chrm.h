// net_chrm.h -- declarations for src117/lang/net_chrm.cpp, the lobby packet
// layer: the save-slot table, the inbound message dispatch and the outbound
// packet builders.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_NET_CHRM_H
#define OC117_NET_CHRM_H

#include "platform.h"

#pragma pack(push, 1)

// The 5-byte lobby-request packet, opcode 0x49, packed: the DWORD sits at +1.
struct net_chrm_req_lobby_t {
  /*0x00*/ BYTE bOpcode;
  // Unsigned, as on_recv_player_joined's range check against 8 reads it.
  /*0x01*/ DWORD nValue;
};

// The 0x79-byte force-names packet, opcode 0x4B: four fixed 0x1E-byte names.
struct net_chrm_forces_t {
  /*0x00*/ BYTE bOpcode;
  /*0x01*/ char szNames[4][0x1E];
};

#pragma pack(pop)

// 0x471850 (10 B). ECX is a BYTE; both callers read only AL, so the return is a
// BYTE.
BYTE GetSaveSlot(BYTE bPlayer);

// 0x471A40 (65 B). `ret 4h`: two BYTE registers and one stack word; the one
// caller reads the full EAX.
int sub_471A40(BYTE bArg0, BYTE bArg1, int nArg2);

// 0x471F80 (186 B). ECX is the text, EDX the player; no caller reads EAX. The
// whisper packet, opcode 0x4D, sub_4701F0's shape one capacity up.
void packet_chat2(const char *pszText, int nPlayer);

// 0x4717F0 (11 B). ECX is a BYTE, indexing gbSaveSlot_g; no caller reads EAX.
void ClearSaveSlot(BYTE bPlayer);

// 0x471800 (74 B). `ret Ch` with no register input, so __stdcall: fd.h's
// PFN_FD_SEND, which sub_477090 hands set_fd_callbacks. The third word is a
// byte.
void __stdcall FDTransmission(void *pMsg, int nLen, char cPlayer);

// 0x471860 (332 B). Returns the opcode it handled. Its one caller, net_glue's
// sub_474F00, forwards its own ECX, which this body never reads.
int net_process_lobby_msg(intptr_t nArg0);

// 0x4719D0 (16 B). Nullary; no caller reads EAX.
void InitSaveSlots();

// 0x4719E0 (38 B). Nullary; no caller reads EAX.
void sub_4719E0();

// 0x471A10 (39 B). Nullary; no caller reads EAX.
void sub_471A10();

// 0x471A90 (69 B). ECX is the packet, EDX the player; no caller reads EAX.
void on_recv_force_names(const net_chrm_forces_t *pPacket, int nPlayer);

// 0x471AE0 (401 B). ECX is the packet, EDX the player; no caller reads EAX.
void on_recv_player_data(intptr_t nArg0, int nArg1);

// 0x471C80 (164 B). ECX is the packet, EDX the player; EAX is never written.
void on_recv_player_joined(void *pArg0, int nArg1);

// 0x471D30 (148 B). ECX is the packet, EDX the player; no caller reads EAX.
void on_recv_whisper_from(intptr_t nArg0, int nArg1);

// 0x471DD0 (64 B). Sends the 0x4E kick packet three times, then drops the
// player. ECX is a Storm id; EDX is a BYTE, the reason. Every caller discards
// EAX.
void packet_kick(DWORD dwStormID, BYTE bReason);

// 0x471E10 (78 B). ECX is the text, copied with a 0x78 capacity.
void packet_chat(const char *pszText);

// 0x471E60 (62 B). ECX is the player the names go to.
void packet_forces(int nPlayer);

// 0x471EA0 (183 B). ECX is the player the lobby state goes to.
void packet_lobby_info(int nArg0);

// 0x471F60 (30 B). ECX goes into the packet body, not to the destination;
// net_send_message gets -1.
void packet_request_lobby(int nArg0);

#endif  // OC117_NET_CHRM_H
