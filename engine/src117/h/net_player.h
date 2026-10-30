// net_player.h -- declarations for src117/lang/net_player.cpp: the per-player
// network record table (gPlayers) and the accessors over it.
//
// IsValidPlayer (0x4780A0), net_player_get_flags and net_player_set_flags are
// declared in net_cmds.h, beside their callers.

#ifndef OC117_NET_PLAYER_H
#define OC117_NET_PLAYER_H

#include "platform.h"

// The per-player network record, 34 bytes, 8 of them at gPlayers (0x673818). Every
// accessor builds its index as x * 17 (`shl eax, 4h / add eax, ecx`) and scales it
// by 2; net_player_init clears 0x110 bytes, 8 records. +0x00 is a byte
// (`cmp byte ptr`), which rules out a WORD[8][17] reading, and holds 0 empty,
// 1 valid or 2 dropped. Packed, since 34 is not a multiple of 4.
#pragma pack(push, 1)
struct netplayer_t
{
  /*0x00*/ BYTE  bState;      // 0 empty / 1 valid / 2 set by net_player_drop
  /*0x01*/ BYTE  bUnk01;      // nothing reaches it
  /*0x02*/ WORD  wFlags;      // loaded and stored as 16 bits
  /*0x04*/ WORD  wUnk04;      // two WORDs, not one DWORD
  /*0x06*/ WORD  wUnk06;
  /*0x08*/ char  szName[26];  // reached as `lea eax,[ecx*2+673820h]` (base + 8)
};

static_assert(offsetof(netplayer_t, bState) == 0x00, "netplayer_t::bState  cmp byte ptr [ecx*2+673818h], 1h");
static_assert(offsetof(netplayer_t, bUnk01) == 0x01, "netplayer_t::bUnk01  UNDERIVED -- asserted for POSITION, not for meaning: nothing in the corpus reaches +0x01, and the field exists only to make wFlags land at +0x02");
static_assert(offsetof(netplayer_t, wFlags) == 0x02, "netplayer_t::wFlags  mov word ptr [eax*2+67381Ah], dx");
static_assert(offsetof(netplayer_t, wUnk04) == 0x04, "netplayer_t::wUnk04  movzx ecx, word ptr [esi*2+67381Ch]");
static_assert(offsetof(netplayer_t, wUnk06) == 0x06, "netplayer_t::wUnk06  movzx ecx, word ptr [esi*2+67381Eh]");
static_assert(offsetof(netplayer_t, szName) == 0x08, "netplayer_t::szName  lea eax, [ecx*2+673820h]");

// +0x04 and +0x06 are two WORDs, not one DWORD: sub_477FD0 loads both and
// net_player_set_data stores both with `word ptr`. sizeof is 34 either way.
static_assert(sizeof(netplayer_t) == 34, "netplayer_t stride -- x17 then addressing scale 2");
#pragma pack(pop)

// Nullary void: `memset(gPlayers, 0, 0x110)` and nothing else.
void net_player_init();

// One register argument; sets the record's state byte to 2. The index parameter
// is BYTE: the body opens `movzx ecx, cl`, reading CL only.
void net_player_drop(BYTE bPlayer);

// Returns &gPlayers[bPlayer].szName, or NULL when the record's state byte is 0.
// The NULL arm is `xor eax, eax` before the lea, so the test precedes the
// address computation in source too.
char * net_player_get_name(BYTE bPlayer);

// The word_673928 accessors. sub_477FB0 takes a WORD out-param -- `mov word ptr
// [ecx], ax` -- rather than returning the value.
void sub_477FB0(WORD *pwOut);
void sub_4780E0();
void sub_478110(WORD wValue);


// 0x477E30, `ret 0Ch`: five parameters, the last two unread. net_player_set_data
// passes (25, "net_player.cpp", 105): a capacity and Storm's file/line owner tag.
void net_get_player_name(DWORD dwPlayerId, char *pszBuffer, int nCapacity,
                         const char *pszFile, int nLine);

// 0x477E50 -- one register argument, the leave reason; 10/10 callers set ECX
// and none reads EAX.
void leave_game(int nReason);

// 0x477E90 -- nullary; its one caller (recv_net_cmd_StartGame) reads EAX
// 32-bit and the body returns the literals 0 and 1.
int sub_477E90();

// 0x477F60 -- nullary void, tail-jumping into net_init_ids.
void net_exit();

// 0x477FD0, `ret 8h`: four parameters. Three WORD out-params; the result is the
// literal 0 or 1.
BOOL sub_477FD0(BYTE bPlayerId, WORD *pwFlags, WORD *pwOut04, WORD *pwOut06);

// 0x478070 -- one BYTE register argument (`movzx eax, cl`), result read 32-bit.
BOOL net_player_exists(BYTE bPlayerId);

// 0x478120, `ret 8h`: four parameters. The second is 16 bits: `mov di, dx` is a
// bare 16-bit move, where a DWORD parameter gives `mov edi, edx`. Both stack
// slots are read 16-bit as well (`mov ax, word ptr [ebp+8h]`).
void net_player_set_data(BYTE bPlayerId, WORD wFlags, WORD wValue04, WORD wValue06);

#endif  // OC117_NET_PLAYER_H
