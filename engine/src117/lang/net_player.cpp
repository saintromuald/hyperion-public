// net_player.cpp -- the per-player network record table and its accessors.
// Functions are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers are for.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>        // memset

#include "../h/net_mgr.h"      // net_init_sync
#include "../h/net_player.h"
#include "../h/map_load.h"   // GetCurrentMap
#include "../h/maphdr.h"    // authenticate_map -- sub_477E90
#include "../h/net_bnet_msg.h" // ClearBNetMessages
#include "../h/net_glue.h"  // NetworkMode
#include "../h/net_cmds.h"  // IsValidPlayer, net_player_{get,set}_flags,
                            // net_init_ids, and gametype.h's Is*Map predicates
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it. gPlayers
// is spelled here rather than in gen/globals117.h because its type, netplayer_t,
// is net_player.h's.
#ifndef OC_DLL_MODE
extern netplayer_t gPlayers[8] OC_UNALIGNED;
#endif
#ifdef OC_DLL_MODE
#define gPlayers (*reinterpret_cast<netplayer_t (*)[8]>(OC_VA(0x673818u)))
#endif

// 0x6728EC, unnamed: sub_477E90 reads it as a full dword and nothing in this TU
// writes it.
#if !defined(OC_DLL_MODE) && !defined(OC_PAYLOAD)
extern DWORD g_6728EC;
#endif
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define g_6728EC (*reinterpret_cast<DWORD *>(OC_VA(0x6728ECu)))
#endif

// Callees no header of ours declares.

// GetCurrentMap (0x46B0B0) is map_load.h's, spelled `void *` as
// src117/lang/maphdr.cpp declares it.

// 0x4BE950, the one-byte `ret` stub, under a third name: leave_game and net_exit
// set up no arguments before calling it, unlike the two (DWORD, BOOL) spellings
// in net_cmds.h.
void nullsub_1();

// KERNEL32!Sleep through the IAT slot 0x4EB2F8. __declspec(dllimport), because
// 1.17 calls it indirectly (`call dword ptr [0x4EB2F8]`).
extern "C" __declspec(dllimport) void __stdcall Sleep(DWORD dwMilliseconds);

// Storm.dll ordinals 113 SNetGetPlayerName, 119 SNetLeaveGame and 102
// SNetDestroy: __stdcall with three, one and no arguments.
extern "C" {
BOOL __stdcall SNetGetPlayerName(DWORD dwPlayerId, char *pszBuffer,
                                 DWORD dwBufferSize);
BOOL __stdcall SNetLeaveGame(int nReason);
BOOL __stdcall SNetDestroy();
}

// The bodies, in oracle VA order.

// Fetches a player's name from Storm, or empties the buffer on failure.
// noinline keeps net_player_set_data's call; /Ob2 splices the body there otherwise.
__declspec(noinline) void net_get_player_name(DWORD dwPlayerId, char *pszBuffer,
                                              int nCapacity, const char *pszFile,
                                              int nLine)
{
  if (!SNetGetPlayerName(dwPlayerId, pszBuffer, nCapacity))
    *pszBuffer = 0;
}

// 0x477E50 (56 B). The `call nullsub_1` is nullary; see the declaration above.
// `if (gPlayback) nReason = 3;` is if-converted to a cmovnz.
void leave_game(int nReason)
{
  net_init_sync();
  nullsub_1();

  if (gbMultiPlayerMode)
    Sleep(1000);

  if (gPlayback != 0)
    nReason = 3;

  SNetLeaveGame(nReason);
}

// 0x477E90 (196 B). "Is this an authenticated ladder-class game?" -- five gates,
// then a map authentication whose returned kind must agree with the map's own
// class. The six Is*Map tests are a flat chain of independent
// `if (P() && kind == K) return 1;` statements: a failing predicate falls through
// to the next one, not to the exit.
int sub_477E90()
{
  int nMapId;

  if (gbMultiPlayerMode
   && NetworkMode() == 0x424E4554
   && g_6728EC == 0
   && !IsUnauthenticatedMap()
   && gPlayback == 0)
  {
    if (!authenticate_map(static_cast<const char *>(GetCurrentMap(1)), &nMapId, 0, 0))
      return 0;

    if (!(IsBlizzardMap() && nMapId == 1)
     && !(IsLadderMap() && nMapId == 2)
     && !(IsTournamentMap() && nMapId == 5)
     && !(IsPGTMap() && nMapId == 3)
     && !(IsKBKMap() && nMapId == 4)
     && !(IsWGTAuthenticatedMap() && nMapId == 6))
      return 0;
  }

  return 1;
}

// 0x477F60 (77 B). The full teardown. The two `Sleep(1000)` guards are separate
// statements: 1.17 tests gbMultiPlayerMode before SNetLeaveGame and again after.
void net_exit()
{
  net_init_sync();
  nullsub_1();

  if (gbMultiPlayerMode)
    Sleep(1000);

  SNetLeaveGame(3);

  if (gbMultiPlayerMode)
    Sleep(1000);

  SNetDestroy();
  ResetLeagueEvt();
  ClearBNetMessages();
  net_init_ids();
}

// `mov ax, word ptr [0x673928] / mov word ptr [ecx], ax / ret`: the value goes
// back through a WORD out-param, with nothing in EAX.
void sub_477FB0(WORD *pwOut) {
  *pwOut = word_673928;
}

// 0x477FD0 (80 B). Read three of a player record's WORDs out through pointers,
// or report the slot empty. Same guarded shape as net_player_get_flags, three
// fields wide.
BOOL sub_477FD0(BYTE bPlayerId, WORD *pwFlags, WORD *pwOut04, WORD *pwOut06)
{
  if (gPlayers[bPlayerId].bState != 1)
    return 0;

    // +0x04 and +0x06 are two WORDs: this body loads both and net_player_set_data
    // stores both with `word ptr`.
  *pwFlags  = gPlayers[bPlayerId].wFlags;
  *pwOut04  = gPlayers[bPlayerId].wUnk04;
  *pwOut06  = gPlayers[bPlayerId].wUnk06;
  return 1;
}

// 0x478020 (45 B), declared in net_cmds.h. The out-param is zeroed before the
// index is computed, so the failure path leaves 0, not the caller's value.
BOOL __fastcall net_player_get_flags(BYTE bPlayerId, WORD *pwFlags)
{
  *pwFlags = 0;

  if (gPlayers[bPlayerId].bState != 1)
    return 0;

  *pwFlags = gPlayers[bPlayerId].wFlags;
  return 1;
}

// Returns a pointer into the record, gPlayers[i] + 8, or NULL when the state
// byte is 0. The NULL arm comes before the address computation.
char * net_player_get_name(BYTE bPlayer) {
  if (gPlayers[bPlayer].bState == 0)
    return 0;
  return gPlayers[bPlayer].szName;
}

// 0x478070 (40 B). Storm is the authority, not the local table: this asks
// SNetGetPlayerName for the slot and throws the name away.
BOOL net_player_exists(BYTE bPlayerId)
{
  char szName[0x104];

  return SNetGetPlayerName(bPlayerId, szName, sizeof(szName)) != 0;
}

// 0x4780A0 (32 B), declared in net_cmds.h. The range test is unsigned and on
// the BYTE (`cmp cl, 8h / jb`).
BOOL __fastcall IsValidPlayer(BYTE bPlayer)
{
  if (bPlayer >= 8)
    return 0;

  return gPlayers[bPlayer].bState == 1;
}

// `mov byte ptr [eax*2+673818h], 2h`: the state byte set to 2, dropped.
void net_player_drop(BYTE bPlayer) {
  gPlayers[bPlayer].bState = 2;
}

// Zeroes word_673928.
void sub_4780E0() {
  word_673928 = 0;
}

// Clears the whole table, 0x110 bytes, 8 records of 34, with one memset call.
void net_player_init() {
  memset(gPlayers, 0, sizeof(gPlayers));
}

// `mov word ptr [0x673928], cx / ret`.
void sub_478110(WORD wValue) {
  word_673928 = wValue;
}

// 0x478120 (117 B). Seat a player into the table, but only if Storm still knows
// the slot; the name is fetched again by net_get_player_name, so Storm is asked
// twice. The fields are stored in 1.17's order, +0x04, +0x00, +0x02, +0x06, not
// declaration order.
void net_player_set_data(BYTE bPlayerId, WORD wFlags, WORD wValue04, WORD wValue06)
{
  char szName[0x104];

  if (SNetGetPlayerName(bPlayerId, szName, sizeof(szName)))
  {
    // Two WORDs, not one DWORD; see sub_477FD0.
    gPlayers[bPlayerId].wUnk04 = wValue04;
    gPlayers[bPlayerId].bState = 1;
    gPlayers[bPlayerId].wFlags = wFlags;
    gPlayers[bPlayerId].wUnk06 = wValue06;

    net_get_player_name(bPlayerId, gPlayers[bPlayerId].szName, 25,
                        "net_player.cpp", 105);
  }
}

// 0x4781A0 (19 B), declared in net_cmds.h.
void __fastcall net_player_set_flags(BYTE bPlayerId, WORD wFlags)
{
  gPlayers[bPlayerId].wFlags = wFlags;
}
