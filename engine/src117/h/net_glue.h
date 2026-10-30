// net_glue.h -- declarations for src117/lang/net_glue.cpp, the session glue between
// the game and Storm's SNet: the network mode, the game creator, the advertised-game
// list, and starting, creating and closing a network session. The g_Active* and
// g_Local* id globals belong to this TU.

#ifndef OC117_NET_GLUE_H
#define OC117_NET_GLUE_H

#include "platform.h"

// 0x475A10 (6 B) -- nullary, returns the network-mode tag. sub_477E90 compares
// it against 0x424E4554 ("TENB"); net_exit reaches it through the same path.
int NetworkMode();

// 0x474BF0 (36 B) -- nullary, result discarded by both callers. net_exit calls
// it on the way out.
void ResetLeagueEvt();

// 0x475360 (6 B) -- `mov eax, dword ptr [0x672970] / ret`, the creator id. The
// body reads neither parameter; every caller passes an assert-style file and line
// (recv_net_cmd_version passes "net_chat_cmds.cpp" and 437) and uses the full EAX.
int IsGameCreator(const char *pszFile, int nLine);

#include "Storm.h"   // SNetDestroy, SNetPerformUpgrade, SStrCopy
#include "TSList.h"
#include "netdesc.h"  // gamedesc_t, inside TGAME_HDR

// The advertised-game record the list carries after its link. sub_475380 fills
// it from the Storm record: the id, the name, then the parsed status string
// with its two player counts overwritten from Storm's own. The last 0x20 bytes
// are never touched in this TU.
struct TGAME_HDR {
  /*0x00*/ DWORD dwGameId;
  /*0x04*/ char szName[0x18];
  /*0x1C*/ gamedesc_t gd;
  /*0x70*/ BYTE _unmapped_70[0x20];
};
static_assert(offsetof(TGAME_HDR, dwGameId) == 0x00, "sub_475380: mov dword ptr [esi+8h], eax");
static_assert(offsetof(TGAME_HDR, szName) == 0x04, "sub_475380: lea ebx, [esi+0Ch] to psnprintf with 18h");
static_assert(offsetof(TGAME_HDR, gd) == 0x1C, "sub_475380: lea edx, [esi+24h] to Txt2GameData");
static_assert(offsetof(TGAME_HDR, _unmapped_70) == 0x70, "gamedesc_t is 0x54 bytes");
static_assert(sizeof(TGAME_HDR) == 0x90, "the node is 98h with the 8-byte link");

// One advertised game, as sub_475380 allocates it (`push 98h` to SMemAlloc).
// The link is first; the game record the enumeration callback receives starts
// at +8 (`lea eax, [esi+8h]` in ListGames, `cmp [eax+8], ecx` in LookupGameHdr).
#pragma pack(push, 1)
struct TGAME_HDR_NODE {
  /*0x00*/ TSLink<TGAME_HDR_NODE> link;
  /*0x08*/ TGAME_HDR hdr;
};
#pragma pack(pop)
OC_X86_LAYOUT(sizeof(TGAME_HDR_NODE) == 0x98, "push 98h / call SMemAlloc");
static_assert(offsetof(TGAME_HDR_NODE, link) == 0x00, "Clear frees the node pointer itself");
OC_X86_LAYOUT(offsetof(TGAME_HDR_NODE, hdr) == 0x08, "lea eax, [esi+8h]");

// The game record Storm hands SNetEnumGamesEx's callback. Only the fields
// sub_475380 reads are named; the two counts are read a byte at a time.
struct SNETGAMEREC {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ DWORD dwGameId;             // mov eax, dword ptr [edi+4h]
  /*0x08*/ const char *pszGameName;    // push dword ptr [edi+8h] to psnprintf
  /*0x0C*/ const char *pszStatString;  // mov ecx, dword ptr [edi+0Ch] to Txt2GameData
  /*0x10*/ DWORD _unmapped_10;
  /*0x14*/ BYTE bPlayers;              // movzx eax, byte ptr [edi+14h]
  /*0x15*/ BYTE _unmapped_15[3];
  /*0x18*/ BYTE bMaxPlayers;           // movzx eax, byte ptr [edi+18h]
  /*0x19*/ BYTE _unmapped_19[3];
};
static_assert(offsetof(SNETGAMEREC, _unmapped_00) == 0x00, "SNETGAMEREC");
static_assert(offsetof(SNETGAMEREC, dwGameId) == 0x04, "SNETGAMEREC");
static_assert(offsetof(SNETGAMEREC, pszGameName) == 0x08, "SNETGAMEREC");
OC_X86_LAYOUT(offsetof(SNETGAMEREC, pszStatString) == 0x0C, "SNETGAMEREC");
OC_X86_LAYOUT(offsetof(SNETGAMEREC, _unmapped_10) == 0x10, "SNETGAMEREC");
OC_X86_LAYOUT(offsetof(SNETGAMEREC, bPlayers) == 0x14, "SNETGAMEREC");
OC_X86_LAYOUT(offsetof(SNETGAMEREC, _unmapped_15) == 0x15, "SNETGAMEREC");
OC_X86_LAYOUT(offsetof(SNETGAMEREC, bMaxPlayers) == 0x18, "SNETGAMEREC");
OC_X86_LAYOUT(offsetof(SNETGAMEREC, _unmapped_19) == 0x19, "SNETGAMEREC");
OC_X86_LAYOUT(sizeof(SNETGAMEREC) == 0x1C, "the last byte read is +18h; the tail is unobserved");

// The advertised-game list, an explicit specialization.
#pragma pack(push, 1)
template <> struct TSList<TGAME_HDR_NODE,TSGetLink<TGAME_HDR_NODE> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<TGAME_HDR_NODE> link;
  // 0x474FD0 (84 B). Unlinks and frees every node.
  void Clear(void);
};
#pragma pack(pop)
typedef TSList<TGAME_HDR_NODE,TSGetLink<TGAME_HDR_NODE> > TGAMEHDRLIST;
OC_X86_LAYOUT(sizeof(TGAMEHDRLIST) == 12, "TSList.h layout");
static_assert(offsetof(TGAMEHDRLIST, _unmapped_00) == 0x00, "TSList.h layout");
static_assert(offsetof(TGAMEHDRLIST, link) == 0x04, "Clear reads [this+8] as link.next");

// The per-game callback ListGames runs: the game record, then its own second
// argument passed through. `push edi / push eax / call ebx` with no stack
// cleanup after, so __stdcall.
typedef void (__stdcall *PFN_GAMEENUM)(intptr_t nGame, intptr_t nArg);

// 0x475030 (11 B) -- nullary; one store of 0 to the creator id at 0x672970,
// which is the same global IsGameCreator above reads.  EAX never written -> void.
void ResetGameCreator();

// 0x4755C0 (37 B) -- ECX is the 32-bit id a list walk compares against `[eax+8]`.
// The result is a pointer, NULL or a found node plus 8, to a type with no name
// here, so it is an intptr_t.
intptr_t LookupGameHdr(int nKey);

// 0x475750 (30 B) -- ECX is SStrCopy's source, copied, at most 0x19 bytes, into
// the local-player name buffer at 0x672690. EAX at the `ret` is SStrCopy's
// leftover and no caller reads it.
void SetLocalPlayerName(const char *pszName);

// 0x475A20 (62 B) -- nullary. Tears the network session down: SNetDestroy, then
// ResetEvent through the KERNEL32 IAT slot at 0x4EB0EC, then a tail jump to
// sub_478110, which net_player.h declares void.
void NetworkShutdown();

// 0x475A60 (83 B) -- ECX is an out-pointer: the body stores 1 through it, and
// NetInit passes the address of a zeroed local. Every `ret` gives an explicit 0
// or 1.
BOOL handle_upgrade(int *pnOut);

// 0x474C20 (7 B) -- ECX read as 32 bits and stored; EAX is written on no path
// to the ret.
void SetCreator(int nArg0);

// 0x474C30 (39 B) -- nullary, no ret of its own: it tail-jumps to ClearBans and
// to TSList<TBAN,0>::Clear, both void.
void sub_474C30();

// 0x474C60 (507 B) -- nullary; one consuming caller uses the full EAX and does
// not narrow it. The body touches x87.
int sub_474C60();

// 0x474E60 (129 B) -- nullary, tail-jumps to close_savefile, which is void.
void sub_474E60();

// 0x474F00 (173 B) -- ECX read as 32 bits; one consuming caller uses the full
// EAX and does not narrow it.
int sub_474F00(intptr_t nArg0);

// 0x474FB0 (5 B) -- a jump thunk onto packet_chat; the contract is that body's.
void sub_474FB0(intptr_t nArg0);

// 0x475040 (459 B) -- ECX and EDX both read as 32 bits; two consuming callers
// use the full EAX. The body touches x87.
int CreateGame_MultiPlayer(intptr_t nArg0, intptr_t nArg1);

// 0x475220 (307 B) -- ECX read as 32 bits; seven consuming callers use the full
// EAX and every ret materialises 0 or 1.
BOOL CreateGame_SinglePlayer(intptr_t nArg0);

// 0x475370 (10 B) -- nullary, tail-jumps to the TGAME_HDR_NODE list's Clear,
// which is void.
void ClearGameHeaders();

// 0x475380 (482 B) -- `ret 4h` with no register input, so __stdcall. ListGames
// calls it through a pointer; every exit returns 1.
BOOL __stdcall sub_475380(const SNETGAMEREC *pGame);

// 0x475570 (76 B) -- ECX is called (`mov ebx, ecx` ... `call ebx`) and EDX is
// forwarded to it; EAX at the ret is inherited and no caller reads it.
void ListGames(PFN_GAMEENUM pfnEnum, intptr_t nArg);

// 0x4755F0 (21 B) -- nullary; EAX at the ret is a leftover from the list Clear.
void sub_4755F0();

// 0x475610 (316 B) -- one register argument and a 32-bit return:
// uiMulti's sub_4BAF80 is a bare `jmp` to it, which requires this signature.
BOOL net_glue_OnInitData(intptr_t nArg0);

// 0x475770 (107 B) -- ECX read as 32 bits. The callee decides the return here:
// one exit is an explicit `mov eax, 1h` the body never consumes and the other
// returns gametype_find_template's zero, so it is a BOOL even though no caller
// reads it. The body touches x87.
BOOL sub_475770(intptr_t nArg0);


// 0x474EF0 (5 B). Two BYTE register arguments.
void sub_474EF0(BYTE bRaceCur, BYTE bRaceNew);

// 0x4757E0 (559 B). ECX is the provider; every path to the ret materialises a
// 0 or 1, so the callee settles its own width.
BOOL NetInit(int nProvider);

#endif  // OC117_NET_GLUE_H
