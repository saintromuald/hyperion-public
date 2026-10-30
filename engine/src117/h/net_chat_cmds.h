// net_chat_cmds.h -- declarations for src117/lang/net_chat_cmds.cpp, the pre-game
// lobby command layer. `create_net_cmd_*` builds a lobby packet into a per-command
// singleton buffer and `recv_net_cmd_*` consumes one from another player. What they
// carry (slot, race, team and force edits, the version handshake, map-download
// progress, the start countdown) is chat-room state, not game state; net_cmds.cpp
// is the in-game layer. The file name is the one 1.17's own ownerTag string at
// 0x4EF014 gives.
//
// Free functions are __fastcall by /Gr's default, so their declarations do not
// spell it.

#ifndef OC117_NET_CHAT_CMDS_H
#define OC117_NET_CHAT_CMDS_H

#include "platform.h"

// PlayerData, the 36-byte slot record create_net_cmd_modify_slot takes, is
// net_cmds.h's; this header includes it so that it compiles on its own.
#include "net_cmds.h"

// The lobby packet layouts, byte-packed. Each offset is a store in the create_*
// body or a load in the recv_* body, and each size is the SMemAlloc count the
// create_* body passes.
#pragma pack(push, 1)

// SMemAlloc(1). create_net_cmd_start_game writes only the opcode.
struct net_chat_cmd_op_t {
  /*0x00*/ BYTE bOpcode;
};

// SMemAlloc(2). One opcode + one byte payload: change_force (0x43),
// change_team (0x42), download_progress (0x3D).
struct net_chat_cmd_op1_t {
  /*0x00*/ BYTE bOpcode;
  /*0x01*/ BYTE bArg;
};

// SMemAlloc(3). One opcode + two byte payloads: change_slot (0x44),
// swap_slots (0x45), change_race (0x41).
struct net_chat_cmd_op2_t {
  /*0x00*/ BYTE bOpcode;
  /*0x01*/ BYTE bArg0;
  /*0x02*/ BYTE bArg1;
};

// SMemAlloc(6). create_net_cmd_modify_slot copies five bytes out of a
// PlayerData-shaped source at +0, +4, +8, +9, +0x0A -- which are exactly
// dwPlayerID's low byte, dwStormID's low byte, nType, nRace and nTeam
// (net_cmds.h's PlayerData). recv_net_cmd_modify_slot reads them back in the
// same order.
struct net_chat_cmd_slot_t {
  /*0x00*/ BYTE bOpcode;
  /*0x01*/ BYTE bPlayerID;
  /*0x02*/ BYTE bStormID;
  /*0x03*/ BYTE nType;
  /*0x04*/ BYTE nRace;
  /*0x05*/ BYTE nTeam;
};

// SMemAlloc(0x0D), the seed-init packet, opcode 0x48: the opcode, the seed (the low
// half of _time64) and the eight slot bytes, which create_net_cmd_seed_init writes
// from slot 7 down.
struct net_chat_cmd_seed_t {
  /*0x00*/ BYTE  bOpcode;
  /*0x01*/ DWORD dwSeed;
  /*0x05*/ BYTE  bSlots[8];
};

// SMemAlloc(8), the version-verify packet, opcode 0x3F: a player slot (below 8)
// and the three WORDs sub_477FD0 fills, which recv_net_cmd_version hands to
// net_player_set_data(bPlayerId, wFlags, wValue04, wValue06).
struct net_chat_cmd_verinfo_t {
  /*0x00*/ BYTE bOpcode;
  /*0x01*/ BYTE bPlayerID;
  /*0x02*/ WORD wFlags;
  /*0x04*/ WORD wValue04;
  /*0x06*/ WORD wValue06;
};

// SMemAlloc(0x12), the version-handshake packet, opcode 0x40: a different layout
// from the verify packet above, known only from create_net_cmd_version's stores.
// Each field is one of that function's arguments, except bSpawned, which is
// IsSpawnedCopy's result, and dwArg0E, which is sub_4634E0's.
struct net_chat_cmd_version_t {
  /*0x00*/ BYTE  bOpcode;
  /*0x01*/ BYTE  bArg01;
  /*0x02*/ BYTE  bArg02;
  /*0x03*/ DWORD dwArg03;
  /*0x07*/ WORD  wArg07;
  /*0x09*/ WORD  wArg09;
  /*0x0B*/ WORD  wArg0B;
  /*0x0D*/ BYTE  bSpawned;
  /*0x0E*/ DWORD dwArg0E;
};

// The lobby chat message, 0x52 bytes, from sub_4701F0's stores: opcode 1 at +0 and
// the text at +2, capacity 0x50. The send's length is strlen + 1 + 2, so +0x01 is
// part of the packet, but nothing writes it and its meaning is unknown.
// packet_chat2 (0x471F80) is the same shape with a 0x78 capacity and opcode 0x4D.
struct net_chat_msg_t {
  /*0x00*/ BYTE bOpcode;
  /*0x01*/ BYTE bUnknown;
  /*0x02*/ char szText[0x50];
};

#pragma pack(pop)

// Each sizeof is the SMemAlloc count the game passes for that packet.
static_assert(offsetof(net_chat_cmd_op_t, bOpcode) == 0x00, "op_t.bOpcode");
static_assert(sizeof(net_chat_cmd_op_t) == 1, "op_t: SMemAlloc(1)");

static_assert(offsetof(net_chat_cmd_op1_t, bOpcode) == 0x00, "op1_t.bOpcode");
static_assert(offsetof(net_chat_cmd_op1_t, bArg) == 0x01, "op1_t.bArg");
static_assert(sizeof(net_chat_cmd_op1_t) == 2, "op1_t: SMemAlloc(2)");

static_assert(offsetof(net_chat_cmd_op2_t, bOpcode) == 0x00, "op2_t.bOpcode");
static_assert(offsetof(net_chat_cmd_op2_t, bArg0) == 0x01, "op2_t.bArg0");
static_assert(offsetof(net_chat_cmd_op2_t, bArg1) == 0x02, "op2_t.bArg1");
static_assert(sizeof(net_chat_cmd_op2_t) == 3, "op2_t: SMemAlloc(3)");

static_assert(offsetof(net_chat_cmd_slot_t, bOpcode) == 0x00, "slot_t.bOpcode");
static_assert(offsetof(net_chat_cmd_slot_t, bPlayerID) == 0x01, "slot_t.bPlayerID");
static_assert(offsetof(net_chat_cmd_slot_t, bStormID) == 0x02, "slot_t.bStormID");
static_assert(offsetof(net_chat_cmd_slot_t, nType) == 0x03, "slot_t.nType");
static_assert(offsetof(net_chat_cmd_slot_t, nRace) == 0x04, "slot_t.nRace");
static_assert(offsetof(net_chat_cmd_slot_t, nTeam) == 0x05, "slot_t.nTeam");
static_assert(sizeof(net_chat_cmd_slot_t) == 6, "slot_t: SMemAlloc(6)");

static_assert(offsetof(net_chat_cmd_seed_t, bOpcode) == 0x00, "seed_t.bOpcode");
static_assert(offsetof(net_chat_cmd_seed_t, dwSeed) == 0x01, "seed_t.dwSeed");
static_assert(offsetof(net_chat_cmd_seed_t, bSlots) == 0x05, "seed_t.bSlots");
static_assert(sizeof(net_chat_cmd_seed_t) == 0x0D, "seed_t: SMemAlloc(0x0D)");

static_assert(offsetof(net_chat_cmd_verinfo_t, bOpcode) == 0x00, "verinfo_t.bOpcode");
static_assert(offsetof(net_chat_cmd_verinfo_t, bPlayerID) == 0x01, "verinfo_t.bPlayerID");
static_assert(offsetof(net_chat_cmd_verinfo_t, wFlags) == 0x02, "verinfo_t.wFlags");
static_assert(offsetof(net_chat_cmd_verinfo_t, wValue04) == 0x04, "verinfo_t.wValue04");
static_assert(offsetof(net_chat_cmd_verinfo_t, wValue06) == 0x06, "verinfo_t.wValue06");
static_assert(sizeof(net_chat_cmd_verinfo_t) == 8, "verinfo_t: SMemAlloc(8)");

static_assert(offsetof(net_chat_cmd_version_t, bOpcode) == 0x00, "version_t.bOpcode");
static_assert(offsetof(net_chat_cmd_version_t, bArg01) == 0x01, "version_t.bArg01");
static_assert(offsetof(net_chat_cmd_version_t, bArg02) == 0x02, "version_t.bArg02");
static_assert(offsetof(net_chat_cmd_version_t, dwArg03) == 0x03, "version_t.dwArg03");
static_assert(offsetof(net_chat_cmd_version_t, wArg07) == 0x07, "version_t.wArg07");
static_assert(offsetof(net_chat_cmd_version_t, wArg09) == 0x09, "version_t.wArg09");
static_assert(offsetof(net_chat_cmd_version_t, wArg0B) == 0x0B, "version_t.wArg0B");
static_assert(offsetof(net_chat_cmd_version_t, bSpawned) == 0x0D, "version_t.bSpawned");
static_assert(offsetof(net_chat_cmd_version_t, dwArg0E) == 0x0E, "version_t.dwArg0E");
static_assert(sizeof(net_chat_cmd_version_t) == 0x12, "version_t: SMemAlloc(0x12)");

static_assert(offsetof(net_chat_msg_t, bOpcode) == 0x00, "msg_t.bOpcode");
static_assert(offsetof(net_chat_msg_t, bUnknown) == 0x01, "msg_t.bUnknown");
static_assert(offsetof(net_chat_msg_t, szText) == 0x02, "msg_t.szText");
static_assert(sizeof(net_chat_msg_t) == 0x52, "msg_t: 2-byte header + char[0x50]");

// The create side. Each builds into a file-static buffer allocated on first use
// and never freed, so every body opens by loading a global pointer and testing it.

// 0x471780 (36 B) -- nullary; EAX at the `ret` is an inherited leftover and no
// caller consumes it -> void.
void create_net_cmd_start_game();

// 0x471560 (36 B) -- nullary, same shape.
void create_net_cmd_make_public();

// 0x471480 (43 B) -- ECX read as 8 bits, EDX never -> one register argument.
void create_net_cmd_change_force(BYTE bForce);

// 0x4717B0 (60 B) -- ECX read as 8 bits, EDX never.
void create_net_cmd_change_team(BYTE bTeam);

// 0x471590 (57 B) -- ECX read as 8 bits, EDX never. EAX is written on no path
// to a `ret` -> void.
void create_net_cmd_download_progress(BYTE bPercent);

// 0x471720 (48 B) -- ECX and EDX both read as 8 bits -> two register arguments.
void create_net_cmd_change_slot(BYTE bArg0, BYTE bArg1);

// 0x471750 (48 B) -- same shape as change_slot, opcode 0x45.
void create_net_cmd_swap_slots(BYTE bArg0, BYTE bArg1);

// 0x4716D0 (77 B) -- ECX and EDX both read as 8 bits.
void create_net_cmd_change_race(BYTE bArg0, BYTE bArg1);

// 0x4715D0 (90 B) -- ECX read as 32 bits, EDX never; ECX is dereferenced, so it
// is a pointer. EAX at the `ret` is a zx8 leftover no caller consumes -> void.
void create_net_cmd_modify_slot(const PlayerData *pSlot);

// 0x471410 (110 B) -- nullary. EAX at the `ret` is call:GetSaveSlot's leftover
// and no caller consumes it -> void.
void create_net_cmd_seed_init();

// 0x471630 (146 B) -- ECX read as an input, EDX never. ECX is an int index into
// gPlayerData, not a pointer: `lea eax, [ecx+ecx*8]` is the 36-byte stride, and
// recv_net_cmd_version_verify calls it with a plain descending loop counter.
void create_net_cmd_version_verify(int nIdx);

// 0x4714B0 (172 B) -- `ret 14h`; ECX and EDX both read as 16 bits, plus five stack
// words whose widths the body's reads set: [ebp+08h] 2 bytes, [ebp+0Ch] and
// [ebp+10h] 1 byte each, [ebp+14h] and [ebp+18h] 4 bytes.
void create_net_cmd_version(WORD wArg0, WORD wArg1, WORD wArg2, BYTE bArg3,
                            BYTE bArg4, int nArg5, int nArg6);

// The receive side. ECX carries the packet and EDX the sending player's id, with
// no stack argument and a plain `ret`. recv_net_cmd_make_public and
// recv_net_cmd_StartGame never read ECX; their first parameter is spelled as
// their siblings' is.

// 0x470D80 (15 B) -- ECX never read; EDX read as 32 bits.
void recv_net_cmd_make_public(const void *pCmd, int nPlayer);

// 0x470D90 (26 B) -- ECX dereferenced (`mov al, byte ptr [ecx+1h]`); EDX 32
// bits. The body tail-jumps to sub_4BB310, which is void, so it is void too.
void recv_net_cmd_download_progress(const void *pCmd, int nPlayer);

// 0x4709D0 (93 B)
void recv_net_cmd_seed_init(const void *pCmd, int nPlayer);

// 0x4712D0 (111 B) -- ECX never read.
void recv_net_cmd_StartGame(const void *pCmd, int nPlayer);

// 0x470EE0 (128 B)
void recv_net_cmd_version(const void *pCmd, int nPlayer);

// 0x470F60 (188 B)
void recv_net_cmd_change_race(const void *pCmd, int nPlayer);

// 0x470A30 (189 B)
void recv_net_cmd_change_force(const void *pCmd, int nPlayer);

// 0x471340 (208 B)
void recv_net_cmd_change_team(const void *pCmd, int nPlayer);

// 0x470DB0 (290 B) -- the body touches x87/SSE, so a floating-point return
// would be invisible to the EAX evidence; EAX is written on no path to a `ret`
// regardless.
void recv_net_cmd_modify_slot(const void *pCmd, int nPlayer);

// 0x471150 (375 B)
void recv_net_cmd_swap_slots(const void *pCmd, int nPlayer);

// 0x471020 (292 B) -- ECX and EDX both read as 32 bits; the tail jumps into
// sub_476EF0, whose EAX the single caller discards.
void recv_net_cmd_change_slot(const void *pCmd, int nPlayer);

// 0x470AF0 (656 B). The one receive handler with three parameters: it is
// `ret 4h` and reads `cmp dword ptr [ebp+8h], 0h` twice. create_net_cmd_version
// calls it with the packet, g_LocalStormID and a literal 1.
void recv_net_cmd_version_verify(const void *pCmd, int nPlayer, int nLocal);

// The rest of the TU.

// 0x470720 (8 B) -- nullary, void.
void mark_local_download_completed();

// 0x470320 (175 B). ECX the text after the '/', EDX the command prefix, then
// its length, the slot out-parameter and a flag (`ret 0Ch`). Answers the rest
// of the line past a human player's name, or NULL. sub_46FD90 passes 0 for
// the squelch family and 1 for the whisper family.
char *sub_470320(const char *pszText, const char *pszPrefix, int nPrefixLen, DWORD *pnSlot,
                 int bNeedSpace);

// 0x4703D0 (134 B). As sub_470320 without the flag (`ret 8h`); the player is
// the one named by the /reply name, and the rest of the line comes back even
// when none matches.
char *sub_4703D0(const char *pszText, const char *pszPrefix, int nPrefixLen, DWORD *pnSlot);

// 0x470910 (25 B) -- ECX read as 8 bits, EDX never. EAX at the `ret` is a 32-bit
// leftover no caller consumes -> void.
void sub_470910(BYTE bArg0);

// 0x470930 (146 B) -- nullary, void.
void sub_470930();

// 0x4701F0 (294 B) -- ECX and EDX both read as 32 bits, no stack parameters. ECX
// is the text: the body dereferences it first, copies from it with SStrCopy and
// takes its length twice.
void sub_4701F0(const char *pszText, int nPlayer);

// 0x470730 (467 B) -- nullary. EAX at the `ret` is an inherited leftover and no
// caller consumes it -> void.
void net_chat_close_lobby();

// 0x470460 (687 B) -- nullary; EAX is written on no path to a `ret` -> void.
void sub_470460();

#endif  // OC117_NET_CHAT_CMDS_H
