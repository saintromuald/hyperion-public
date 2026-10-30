// net_glue.cpp -- the network glue layer: the create and join entry points the
// menu screens call, the game-creator and network-mode flags, and the session
// teardown. Functions are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers are for. ResetLeagueEvt must stay inlinable:
// NetworkShutdown's body includes it.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>              // memset, inlined by /Oi

#include "../h/net_glue.h"     // this TU's declarations, and Storm.h
#include "../h/net_player.h"   // sub_4780E0, sub_478110, leave_game
#include "../h/net_error.h"    // net_error_popup
#include "../h/net_cmds.h"     // PlayerData, voice_squelch
#include "../h/net_chat_cmds.h"  // sub_470460, net_chat_close_lobby,
                                       // create_net_cmd_change_race
#include "../h/net_chrm.h"     // packet_chat, net_process_lobby_msg
#include "../h/net_strt.h"     // end_countdown
#include "../h/netdesc.h"      // sub_46F3C0
#include "../h/gameloop.h"     // sub_470150
#include "../h/gametype.h"     // GameHeader, TGAMEPARAMS, IsScenarioGame
#include "../h/gluBNCreate.h"  // gametype_find_template
#include "../h/fd.h"           // fd_cleanup
#include "../h/fd_sessn.h"     // sub_440B60
#include "../h/saveload.h"     // close_savefile
#include "../h/glues.h"        // IsSpawnedCopy
#include "../h/grid.h"         // sub_4634E0
#include "../h/gluBN.h"        // SNETUIDATA, InitSNetUIData
#include "../h/gluModem.h"     // enum_devices
#include "../h/net_misc.h"     // net_misc_init, snet_playerleave_cb
#include "../h/net_bnet_msg.h" // snet_servermessage_cb
#include "../h/net_ban.h"       // ClearBans, IsPlayerBanned
#include "../h/rtti.h"          // type_info::raw_name
#include "../h/net_slot.h"      // net_slot_init
#include "../h/net_mgr.h"       // net_mgr_init
#include "../h/gds_local.h"     // psnprintf
#include "../h/gluChat.h"       // on_chrm_kicked
#include "../h/game.h"          // sub_499C50
#include "../gen/globals117.h"

// Callees no header of ours declares.

// 0x4BE950, the folded one-byte `ret`, reached with no register set up: a third
// arity beside net_cmds.h's two-argument spelling.
void __fastcall voice_squelch();
void __stdcall snet_playerjoin_cb(void *pEvent);

// KERNEL32's ResetEvent through the IAT slot 0x4EB0EC: __declspec(dllimport),
// because 1.17 calls it indirectly. The handle is `void *`, as net_player.cpp's
// Sleep and snd.h's SetEvent are spelled; platform.h has no HANDLE.
extern "C" __declspec(dllimport) BOOL __stdcall ResetEvent(void *hEvent);

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

#ifdef OC_DLL_MODE
#define gbIsGameCreator_g (*reinterpret_cast<DWORD *>(OC_VA(0x672970u)))
#else
extern DWORD gbIsGameCreator_g;
#endif

// 0x672968 -- the league event handle: a DWORD null-tested and pushed straight
// into ResetEvent, so `void *`.
#ifdef OC_DLL_MODE
#define ghLeagueEvt_g (*reinterpret_cast<void **>(OC_VA(0x672968u)))
#else
extern void *ghLeagueEvt_g;
#endif

// 0x67296C -- the "event is currently signalled" flag. A DWORD, though its name
// says byte: every access is 4 bytes wide.
#ifdef OC_DLL_MODE
#define sgbLeagueEvtSet_g (*reinterpret_cast<DWORD *>(OC_VA(0x67296Cu)))
#else
extern DWORD sgbLeagueEvtSet_g;
#endif

// 0x672974 -- the network-mode tag, 4 bytes both ways. int: the only value 1.17
// stores here is -1.
#ifdef OC_DLL_MODE
#define sgxNetworkMode_g (*reinterpret_cast<int *>(OC_VA(0x672974u)))
#else
extern int sgxNetworkMode_g;
#endif

// 0x508D64 -- the head link of a Storm TSList, in .data. It is an int because
// LookupGameHdr ends its walk on a signed `jle`: a TSList's last node holds
// `~(&list.link.prev)`, a large negative number, never NULL.
//
// It is gGameHeaders_g.link.next, and is read through the list, whose layout
// changes at 64 bits.
#define dword_508D64 (reinterpret_cast<intptr_t>(gGameHeaders_g.link.next))

// 0x672960 -- the file-dialog session object: these bodies hand its address to
// sub_440B60 and snet_playerjoin_cb hands its value to set_fd_session_flags,
// which takes `void *`.
#ifdef OC_DLL_MODE
#define dword_672960 (*reinterpret_cast<TFD_SESSION **>(OC_VA(0x672960u)))
#else
extern TFD_SESSION *dword_672960;
#endif

// 0x508D5C gGameHeaders_g -- the list of advertised games; its link.next is
// dword_508D64.
#ifdef OC_DLL_MODE
#define gGameHeaders_g (*reinterpret_cast<TGAMEHDRLIST *>(OC_VA(0x508D5Cu)))
#else
extern TGAMEHDRLIST gGameHeaders_g;
#endif

// 0x5349E0 gbVictoryState, BYTE[8], spelled as victory.cpp spells it.
#ifndef OC_OWN_gbVictoryState
#define OC_OWN_gbVictoryState
#ifdef OC_DLL_MODE
#define gbVictoryState (reinterpret_cast<BYTE *>(OC_VA(0x5349E0u)))
#else
extern PlayerVictoryStateTable gbVictoryState;  // Hyperion
#endif
#endif

// 0x51098C, the RTTI Type Descriptor Clear hands to raw_name for SMemFree's tag.
#ifdef OC_DLL_MODE
#define gTypeInfo_TGAME_HDR_NODE (*reinterpret_cast<type_info *>(OC_VA(0x51098Cu)))
#else
extern type_info gTypeInfo_TGAME_HDR_NODE;
#endif

// gGameHeader (0x6728D0), spelled as gametype.cpp and uiMulti.cpp spell it.
#ifndef OC_OWN_gGameHeader
#define OC_OWN_gGameHeader
#ifdef OC_DLL_MODE
#define gGameHeader (*reinterpret_cast<GameHeader *>(OC_VA(0x6728D0u)))
#else
extern GameHeader gGameHeader;
#endif
#endif

// 0x527120 byte_527120 -- a char[0x20], its extent PreloadMap's SStrCopy
// capacity. Spelled as src117/lang/uiMulti.cpp spells it.
#ifndef OC_OWN_byte_527120
#define OC_OWN_byte_527120
#ifdef OC_DLL_MODE
#define byte_527120 (reinterpret_cast<char *>(OC_VA(0x527120u)))
#else
extern char byte_527120[0x20];
#endif
#endif

// 0x6A5E34 gpLoadGameFile, spelled as src117/lang/saveload.cpp spells it.
#ifndef OC_OWN_gpLoadGameFile
#define OC_OWN_gpLoadGameFile
#ifdef OC_DLL_MODE
#define gpLoadGameFile (*reinterpret_cast<void **>(OC_VA(0x6A5E34u)))
#else
extern void *gpLoadGameFile;
#endif
#endif

// 0x66CCA8 gVersion -- the five-dword record open_archives fills (src117/h/init.h);
// only its address is used here, as SNetInitializeProvider's last argument.
#include "../h/init.h"

// 0x67265C gdwMaxPacketSize and 0x672660 gdwDefTurnsInTransit, both read and
// stored as full dwords; gdwDefTurnsInTransit is spelled as
// src117/lang/net_chat_cmds.cpp spells it.
#ifdef OC_DLL_MODE
#define gdwMaxPacketSize (*reinterpret_cast<DWORD *>(OC_VA(0x67265Cu)))
#else
extern DWORD gdwMaxPacketSize;
#endif

#ifndef OC_OWN_gdwDefTurnsInTransit
#define OC_OWN_gdwDefTurnsInTransit
#ifdef OC_DLL_MODE
#define gdwDefTurnsInTransit (*reinterpret_cast<DWORD *>(OC_VA(0x672660u)))
#else
extern DWORD gdwDefTurnsInTransit;
#endif
#endif

// SNetGetProviderCaps' output block. NetInit states the size itself
// (`mov dword ptr [ebp-38h], 24h`) and reads two fields: the packet cap at +8,
// whose .rdata error string is "caps.maxmessagesize", and the turn count at
// +0x20. The rest is never read here, so only the extent is derived.
#pragma pack(push, 1)
struct SNETCAPS {
  /*0x00*/ DWORD dwSize;
  /*0x04*/ DWORD _underived_04;
  /*0x08*/ DWORD dwMaxMessageSize;
  /*0x0C*/ DWORD _underived_0C[5];
  /*0x20*/ DWORD dwTurnsInTransit;
};
#pragma pack(pop)
static_assert(sizeof(SNETCAPS) == 0x24, "mov dword ptr [ebp-38h], 24h");
static_assert(offsetof(SNETCAPS, dwSize) == 0x00, "the caller's own size word");
static_assert(offsetof(SNETCAPS, dwMaxMessageSize) == 0x08, "[ebp-30h]");
static_assert(offsetof(SNETCAPS, dwTurnsInTransit) == 0x20, "[ebp-18h]");

// The bodies, in oracle VA order.

// 0x474BF0 (36 B). `if (handle && flag) { ResetEvent(handle); flag = 0; }`. The
// handle is loaded once into a local and the local is pushed, as 1.17 does.
void ResetLeagueEvt()
{
  void *hEvent = ghLeagueEvt_g;

  if (hEvent != 0 && sgbLeagueEvtSet_g != 0) {
    ResetEvent(hEvent);
    sgbLeagueEvtSet_g = 0;
  }
}

// Records whether the local player created the game.
void SetCreator(int nArg0)
{
  gbIsGameCreator_g = nArg0;
}

// Tears the file-dialog session down and clears the ban list. The guard's
// argument is the flag's own address (`mov ecx, 672960h`), not its value.
void sub_474C30()
{
  if (dword_672960 != nullptr) {
    sub_440B60(reinterpret_cast<intptr_t>(&dword_672960));
    dword_672960 = nullptr;
  }
  fd_cleanup();
  ClearBans();
}

// 0x672964 dword_672964. One 4-byte store of 0.
#ifndef OC_OWN_dword_672964
#define OC_OWN_dword_672964
#ifdef OC_DLL_MODE
#define dword_672964 (*reinterpret_cast<DWORD *>(OC_VA(0x672964u)))
#else
extern DWORD dword_672964;
#endif
#endif

// 0x672978 byte_672978. One byte store of 0.
#ifndef OC_OWN_byte_672978
#define OC_OWN_byte_672978
#ifdef OC_DLL_MODE
#define byte_672978 (*reinterpret_cast<BYTE *>(OC_VA(0x672978u)))
#else
extern BYTE byte_672978;
#endif
#endif

// 0x5264B8 -- the per-player chat-visibility mask, WORD, spelled as
// net_chrm.cpp spells it. One `mov word ptr` store.
#ifndef OC_OWN_word_5264B8
#define OC_OWN_word_5264B8
#ifdef OC_DLL_MODE
#define word_5264B8 (*reinterpret_cast<WORD *>(OC_VA(0x5264B8u)))
#else
extern WORD word_5264B8;
#endif
#endif

// 0x526490 -- a DWORD read here as its low byte only
// (`movzx eax, byte ptr [0x526490]`), spelled as placebox.cpp spells it.
#ifndef OC_OWN_dword_526490
#define OC_OWN_dword_526490
#ifdef OC_DLL_MODE
#define dword_526490 (*reinterpret_cast<DWORD *>(OC_VA(0x526490u)))
#else
extern DWORD dword_526490;
#endif
#endif

// Brings a joined game up: load the save if there is one, reset the slot and
// player tables, then hand the lobby over. gbIsGameCreator_g is read twice
// before SyncSlots because AssignTeamsBySlot can change it. The slot snapshot goes
// through an `int` read through a BYTE pointer and narrows only at the store,
// which keeps the oracle's cmovnz.
int sub_474C60()
{
  char szSaveName[0x1C];
  WORD wVersion;
  BYTE bSlotSnapshot;
  BYTE bLoadFlag;

  dword_672964 = 0;

  if (g_LocalStormID == 0 && gbIsGameCreator_g == 0) {
    on_chrm_kicked(4);
    return 0;
  }

  sub_49A230();
  InitSaveSlots();

  if (gGameHeader.dwSaveId != 0) {
    if (!sub_4995E0(gGameHeader.dwSaveId, szSaveName, 0x1C)
        || !load_game(gGameHeader.dwSaveId, szSaveName)
        || !sub_499C50()) {
      sub_474E60();
      err_popup(0x5F, reinterpret_cast<intptr_t>("."), 0, 0, 0);
      return 0;
    }

    if (gbIsGameCreator_g != 0)
      sub_479840();
  }

  PlayerClear(gPlayerData);  // Hyperion: and Hyperion's slots
  net_slot_init(0xFFFFFFFF);
  net_player_init();
  clear_results_written();
  ClearBans();

  if (gbIsGameCreator_g != 0) {
    if (gpLoadGameFile == 0) {
      if (!IsScenarioGame(0))
        ResetLobbySlotsOpen();

      sub_471A10();
      AssignTeamsBySlot();
    }

    if (gbIsGameCreator_g != 0)
      SyncSlots();
  }

  g_LocalHumanID = -1;
  g_LocalNationID = -1;
  byte_672978 = 0;
  word_5264B8 = 0xFFFF;
  voice_squelch();
  net_strt_init();
  net_misc_init();
  net_mgr_init();

  if (gGameHeader.dwSaveId == 0)
    PlayerClear(gbVictoryState);

  if (!sub_477090()) {
    sub_474E60();
    err_popup(0x64, 0, 0, 0, 0);
    return 0;
  }

  mark_local_download_completed();
  sub_46F460();
  sub_470910(2);

  if (gbIsGameCreator_g != 0) {
    if (gpLoadGameFile != 0)
      bLoadFlag = static_cast<BYTE>(sub_499850());
    else
      bLoadFlag = 0;

    {
      int nSlot = *(reinterpret_cast<const BYTE *>(&dword_526490));
      bSlotSnapshot = static_cast<BYTE>((gpLoadGameFile != 0) ? nSlot : 0);
    }

    sub_477FB0(&wVersion);
    sub_470910(3);
    create_net_cmd_version(wVersion, 1, 5, bLoadFlag, bSlotSnapshot,
                           sub_499860(), 1);
  }

  return 1;
}

// Leaves the lobby and resets every per-game identity. The bare
// `call voice_squelch` is nullary -- 0x4BE950 is the folded one-byte `ret` that
// several names share, so this is another arity at that one address.
void sub_474E60()
{
  if (dword_672960 != nullptr) {
    sub_440B60(reinterpret_cast<intptr_t>(&dword_672960));
    dword_672960 = nullptr;
  }
  fd_cleanup();
  sub_470460();
  net_chat_close_lobby();
  leave_game(0x40000001);
  end_countdown();

  gbIsGameCreator_g = 0;
  g_LocalHumanID = -1;
  g_LocalNationID = -1;
  g_LocalStormID = -1;
  PlayerClear(gPlayerData);  // Hyperion: and Hyperion's slots
  voice_squelch();
  close_savefile();
}

// A pass-through onto the race-change command builder.
void sub_474EF0(BYTE bRaceCur, BYTE bRaceNew)
{
  create_net_cmd_change_race(bRaceCur, bRaceNew);
}

// The lobby message pump's front end. When the descriptor poll says the game
// is gone it picks the glue mode to fall back to from the local player's race
// and closes the lobby; otherwise it hands the message on.
int sub_474F00(intptr_t nArg0)
{
  sub_470150();
  if (sub_46F3C0() != 0) {
    if (IsScenarioGame(nullptr) != 0 && gpLoadGameFile == nullptr &&
        gPlayback == 0) {
      switch (gPlayerData[g_LocalNationID].nRace) {
        default:
          glGluesMode = 7;
          break;
        case 2:
          glGluesMode = 9;
          break;
        case 0:
          glGluesMode = 8;
          break;
      }
      net_chat_close_lobby();
      return 0x53;
    }
    gwGameMode = 1;
    net_chat_close_lobby();
    return 0x53;
  }
  return net_process_lobby_msg(nArg0);
}

// A pass-through onto the chat packet builder.
void sub_474FB0(intptr_t nArg0)
{
  packet_chat(reinterpret_cast<const char *>(nArg0));
}

// TSList's unlink for this node type. The link offset needs two reaching
// definitions or MSVC folds the three-term sum; static so it inlines.
static void UnlinkGameHdr(TGAME_HDR_NODE *node)
{
  TSLink<TGAME_HDR_NODE> *pl = &node->link;
  TGAME_HDR_NODE *pprev = pl->prev;

  if (pprev != NULL) {
    TGAME_HDR_NODE *pnext = pl->next;
    TSLink<TGAME_HDR_NODE> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<TGAME_HDR_NODE> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0)
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      pnl = reinterpret_cast<TSLink<TGAME_HDR_NODE> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = NULL;
    pl->next = NULL;
  }
}

// Unlinks and frees every advertised game. Normalising the head to null before
// the test keeps MSVC from rotating the loop.
void TGAMEHDRLIST::Clear(void)
{
  for (;;) {
    TGAME_HDR_NODE *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkGameHdr(p);
    SMemFree(p, gTypeInfo_TGAME_HDR_NODE.raw_name(), -2, 0);
  }
}

// 0x475030 (11 B).
void ResetGameCreator()
{
  gbIsGameCreator_g = 0;
}

// Starts a single-player game as a local Storm session: tear any session down,
// bring up provider 0, create the game, then seat the local player and adopt the
// header. NetworkShutdown is inlined here.
BOOL CreateGame_SinglePlayer(intptr_t nArg0)
{
  const GameHeader *pHdr = reinterpret_cast<const GameHeader *>(nArg0);

  if (IsSpawnedCopy()) {
    err_popup(0x67, 0, 0, 0, 1);
    return FALSE;
  }
  NetworkShutdown();
  if (!NetInit(0))
    return FALSE;
  if (SNetCreateGame(reinterpret_cast<const char *>(pHdr->_unmapped_04), "", "",
                     0, 0, 0, pHdr->bSlotCount,
                     gPlayerData[g_LocalNationID].szName, "", &g_LocalStormID)) {
    gbIsGameCreator_g = 1;
    gPlayerData[g_LocalNationID].dwStormID = g_LocalStormID;
    g_StormToNationID[g_LocalStormID] = g_LocalNationID;
    g_StormToHumanID[g_LocalStormID] = g_LocalHumanID;
    gGameHeader = *pHdr;
    net_slot_init(g_LocalStormID);
    net_misc_init();
    net_mgr_init();
    if (gGameHeader.dwSaveId == 0)
      PlayerClear(gbVictoryState);
    return TRUE;
  }
  net_error_popup(0x5C, "net_glue.cpp", 0x241, 1);
  return FALSE;
}

// 0x475360 (6 B). `mov eax, dword ptr [0x672970] / ret`, the whole body. The two
// parameters are the assert-style file and line every caller passes; the body
// ignores them.
int IsGameCreator(const char *pszFile, int nLine)
{
  return gbIsGameCreator_g;
}

// Drops the advertised-game list.
void ClearGameHeaders()
{
  gGameHeaders_g.Clear();
}

// Placement construction of one node: both link fields zeroed.
static __forceinline TGAME_HDR_NODE *InitGameHdr(TGAME_HDR_NODE *q)
{
  q->link.prev = NULL;
  q->link.next = NULL;
  return q;
}

// SNetEnumGamesEx's per-game callback: parse the game's status string into a
// new node, then move the node so the list stays sorted by name. NewNode links
// it at the head and the sorted LinkNode unlinks it twice, all inlined as in
// AddBannedPlayer and sub_499250.
BOOL __stdcall sub_475380(const SNETGAMEREC *pGame)
{
  void *pv = SMemAlloc(sizeof(TGAME_HDR_NODE), gTypeInfo_TGAME_HDR_NODE.raw_name(), -2, 8);
  TGAME_HDR_NODE *pNode = pv ? InitGameHdr(static_cast<TGAME_HDR_NODE *>(pv)) : NULL;
  TSLink<TGAME_HDR_NODE> *pLink = pNode ? &pNode->link : &gGameHeaders_g.link;

  if (pLink->prev != NULL) {
    TGAME_HDR_NODE *pSlot = pLink->next;
    if (reinterpret_cast<intptr_t>(pSlot) <= 0) {
      pSlot = reinterpret_cast<TGAME_HDR_NODE *>(~reinterpret_cast<intptr_t>(pSlot));
    } else {
      pSlot = reinterpret_cast<TGAME_HDR_NODE *>(
          reinterpret_cast<char *>(pSlot) +
          (reinterpret_cast<char *>(pLink) -
           reinterpret_cast<char *>(pLink->prev->link.next)));
    }
    *reinterpret_cast<TGAME_HDR_NODE **>(pSlot) = pLink->prev;
    pLink->prev->link.next = pLink->next;
    pLink->prev = NULL;
    pLink->next = NULL;
  }
  pLink->prev = reinterpret_cast<TGAME_HDR_NODE *>(&gGameHeaders_g.link);
  pLink->next = gGameHeaders_g.link.next;
  TGAME_HDR_NODE *pHead = gGameHeaders_g.link.next;
  int nOff = static_cast<int>(gGameHeaders_g._unmapped_00);
  if (reinterpret_cast<intptr_t>(pHead) <= 0) {
    pHead = reinterpret_cast<TGAME_HDR_NODE *>(~reinterpret_cast<intptr_t>(pHead));
  } else {
    if (nOff < 0) {
      nOff = static_cast<int>(
          reinterpret_cast<char *>(&gGameHeaders_g.link) -
          reinterpret_cast<char *>(gGameHeaders_g.link.prev->link.next));
    }
    pHead = reinterpret_cast<TGAME_HDR_NODE *>(reinterpret_cast<char *>(pHead) + nOff);
  }
  *reinterpret_cast<TGAME_HDR_NODE **>(pHead) = reinterpret_cast<TGAME_HDR_NODE *>(pLink);
  gGameHeaders_g.link.next = pNode;

  if (!Txt2GameData(pGame->pszStatString, &pNode->hdr.gd)) {
    if (pNode) {
      UnlinkGameHdr(pNode);
      SMemFree(pNode, "delete", -1, 0);
    }
    return TRUE;
  }
  psnprintf(pNode->hdr.szName, 0x18, "%s", pGame->pszGameName);
  pNode->hdr.dwGameId = pGame->dwGameId;
  pNode->hdr.gd.bActivePlayers = pGame->bPlayers;
  pNode->hdr.gd.bMaxPlayers = pGame->bMaxPlayers;

  UnlinkGameHdr(pNode);
  TGAME_HDR_NODE *p = gGameHeaders_g.link.next;
  if (reinterpret_cast<intptr_t>(p) <= 0)
    p = NULL;
  while (p) {
    if (_stricmp(pNode->hdr.szName, p->hdr.szName) <= 0)
      break;
    p = p->link.next;
    if (reinterpret_cast<intptr_t>(p) <= 0)
      p = NULL;
  }
  UnlinkGameHdr(pNode);
  TSLink<TGAME_HDR_NODE> *pAt = p ? &p->link : &gGameHeaders_g.link;
  TGAME_HDR_NODE *pBefore = pAt->prev;
  pNode->link.prev = pBefore;
  pNode->link.next = pBefore->link.next;
  pBefore->link.next = pNode;
  pAt->prev = pNode;
  return TRUE;
}

// Re-enumerates the advertised games through Storm, then hands each record and
// nArg to the callback.
void ListGames(PFN_GAMEENUM pfnEnum, intptr_t nArg)
{
  DWORD nCount;

  gGameHeaders_g.Clear();
  if (SNetEnumGamesEx(0, 0, reinterpret_cast<void *>(&sub_475380), &nCount)) {
    TGAME_HDR_NODE *p = gGameHeaders_g.link.next;
    if ((intptr_t)p > 0) {
      do {
        pfnEnum(reinterpret_cast<intptr_t>(&p->hdr), nArg);
        p = p->link.next;
      } while ((intptr_t)p > 0 && p);
    }
  }
}

// 0x4755C0 (37 B). A TSList find-by-id returning the payload at +8, not the node.
// The `> 0` guard is an int cast: the terminator is a large negative number.
//
// The port walks the node through its struct: the link's +4 and the payload's +8
// are 32-bit offsets, and at 64 bits the link is 16 bytes, so the walk read the
// wrong words and never found a game (the first time the list held one).
intptr_t LookupGameHdr(int nKey)
{
  TGAME_HDR_NODE *pNode = reinterpret_cast<TGAME_HDR_NODE *>(dword_508D64);

  if (reinterpret_cast<intptr_t>(pNode) > 0) {
    do {
      if (static_cast<int>(pNode->hdr.dwGameId) == nKey)
        return reinterpret_cast<intptr_t>(&pNode->hdr);
      pNode = pNode->link.next;
      if (reinterpret_cast<intptr_t>(pNode) <= 0)
        break;
    } while (pNode != 0);
  }
  return 0;
}

// Drops the advertised-game list and the creator flag.
void sub_4755F0()
{
  gGameHeaders_g.Clear();
  gbIsGameCreator_g = 0;
}

// The join path's init-data handler: check the spawn key, publish three
// statstring fields, join through Storm, then adopt the lobby's header while
// keeping the template block at +0x6D that snet_initdata_cb already wrote.
// The `== 0xFFFF` guard is dead -- bGameType is one byte, so the arm is
// unreachable; uiMulti_OnInitData carries the identical test.
BOOL net_glue_OnInitData(intptr_t nArg0)
{
  const GameHeader *pSrc = reinterpret_cast<const GameHeader *>(nArg0);
  TGAMEPARAMS params;

  // +0x28..+0x32 are inside gametype.h's _unmapped_28 window; the record there
  // is uiMulti.cpp's GameData, which no header of ours seats.
  if (IsSpawnedCopy() != 0 &&
      sub_4634E0() != *reinterpret_cast<const int *>(&pSrc->_unmapped_28[4])) {
    err_popup(0x69, 0, 0, 0, 0);
    return FALSE;
  }

  gGameHeader._unmapped_28[0] = pSrc->_unmapped_28[0];
  gGameHeader._unmapped_28[1] = pSrc->_unmapped_28[1];
  *reinterpret_cast<WORD *>(&gGameHeader._unmapped_28[2]) =
      *reinterpret_cast<const WORD *>(&pSrc->_unmapped_28[2]);

  // The join succeeding is the arm that carries the body, which is what puts
  // the Storm error handling physically last and makes the guard's jump near.
  if (SNetJoinGame(*reinterpret_cast<const DWORD *>(pSrc), "", "",
                   gLocalPlayerName, "", &g_LocalStormID)) {
    if (gGameHeader.bGameType == 0xFFFF) {
      leave_game(3);
      err_popup(0x5D, reinterpret_cast<intptr_t>("OnInitData()"),
                reinterpret_cast<intptr_t>("net_glue.cpp"), 0x1A6, 0);
      return FALSE;
    }

    params = *reinterpret_cast<const TGAMEPARAMS *>(&gGameHeader.bGameType);
    gGameHeader = *pSrc;
    *reinterpret_cast<TGAMEPARAMS *>(&gGameHeader.bGameType) = params;
    gPlayback = pSrc->_unmapped_28[0x0A];
    SStrCopy(byte_527120, reinterpret_cast<const char *>(&gGameHeader) + 0x4D,
             0x20);
    gbIsGameCreator_g = 0;
    return TRUE;
  }

  if (SErrGetLastError() == 0x85100079)
    net_error_popup(0x5A, "net_glue.cpp", 0x1BB, 0);
  else
    net_error_popup(0x59, "net_glue.cpp", 0x1BD, 0);
  return FALSE;
}

// 0x475750 (30 B). The SStrCopy bound is the literal 25: in the payload the
// global is a pointer, and sizeof would give 4.
void SetLocalPlayerName(const char *pszName)
{
  sub_4780E0();
  sub_478110(0);
  SStrCopy(gLocalPlayerName, pszName, 25);
}

// Adopts a game header from a lobby listing and re-derives its template. The
// 141-byte copy is one struct assignment; gametype_find_template's three
// arguments are fields of the copy, not of the source.
BOOL sub_475770(intptr_t nArg0)
{
  const TGAMEPARAMS *pParams;

  gbIsGameCreator_g = 0;
  gGameHeader = *reinterpret_cast<const GameHeader *>(nArg0);

  // The key is the listing's own +0x28..+0x2A, not the template block at +0x6D;
  // only the shipped form sees the difference.
  pParams = gametype_find_template(
      gGameHeader._unmapped_28[0], gGameHeader._unmapped_28[1],
      *reinterpret_cast<const WORD *>(&gGameHeader._unmapped_28[2]));
  if (pParams == nullptr)
    return FALSE;

  *reinterpret_cast<TGAMEPARAMS *>(&gGameHeader.bGameType) = *pParams;
  // +0x4D is GameData::szMapName, the record gametype.h models inside
  // GameHeader::_unmapped_00; the struct is declared in uiMulti.cpp, not a header.
  SStrCopy(byte_527120, reinterpret_cast<const char *>(&gGameHeader) + 0x4D,
           0x20);
  return TRUE;
}

// Brings a network provider up: fill Storm's three descriptor blocks, start
// the provider, read its caps into the packet and turn budgets, enumerate the
// devices, then register the four event handlers. Every failure path falls
// through into one shared teardown.
BOOL NetInit(int nProvider)
{
  SNETUIDATA ui;
  SNETPROGRAMDATA pd;
  SNETCAPS caps;
  SNETPLAYERDATA pl;
  int nUpgrade;
  BOOL bStarted;
  DWORD n;

  InitSNetProgramData(&pd);
  InitSNetPlayerData(&pl);
  // Hyperion: the UI block handed the providers the Battle.net screens'
  // callbacks (InitSNetUIData, gluBN.cpp). The screens are gone and the port's
  // SNet reads none of it, so it goes out empty.
  memset(&ui, 0, sizeof ui);
  ui.dwSize = sizeof ui;
  ui.dwVersion = 1;

  if (nProvider == 0x424E4554 && ghLeagueEvt_g != 0 && sgbLeagueEvtSet_g == 0) {
    SetEvent(ghLeagueEvt_g);
    sgbLeagueEvtSet_g = 1;
  }

  bStarted = SNetInitializeProvider(nProvider, &pd, &pl, &ui, &gVersion);
  DestroySNetProgramData(&pd);

  if (!bStarted) {
    if (SErrGetLastError() != 0x85100077) {
      if (nProvider != 0) {
        if (nProvider == 0x424E4554)
          goto fail;
        net_error_popup(0x53, nullptr, 0, 0);
        goto fail;
      }
      err_popup(0x52, 0, 0, 0, 1);
      goto fail;
    }

    nUpgrade = 0;
    if (!handle_upgrade(&nUpgrade)) {
      if (nUpgrade == 0)
        goto fail;
      if (nProvider == 0x424E4554)
        ResetLeagueEvt();
      gwGameMode = 2;
      return FALSE;
    }
  }

  caps.dwSize = 0x24;
  if (!SNetGetProviderCaps(&caps)) {
    net_error_popup(0x55, "net_glue.cpp", 0xD6, 0);
    goto fail;
  }

  n = caps.dwMaxMessageSize;
  if (n < 0x80) {
    err_popup(0x55, reinterpret_cast<intptr_t>("caps.maxmessagesize"),
              reinterpret_cast<intptr_t>("net_glue.cpp"), 0xDA, 0);
    goto fail;
  }
  if (n > 0x200)
    n = 0x200;
  gdwMaxPacketSize = n;

  if (nProvider != 0) {
    // Both clamps are ternaries: an if-assignment pair here costs the two
    // cmovs and eight bytes.
    DWORD nTurns = caps.dwTurnsInTransit < 8 ? caps.dwTurnsInTransit : 8;

    gdwDefTurnsInTransit = nTurns > 2 ? nTurns : 2;
  } else {
    gdwDefTurnsInTransit = 1;
  }

  sgxNetworkMode_g = nProvider;
  if (!enum_devices(nProvider, reinterpret_cast<intptr_t>(&pd),
                    reinterpret_cast<intptr_t>(&pl), reinterpret_cast<intptr_t>(&ui)))
    goto fail;

  net_misc_init();
  if (SNetRegisterEventHandler(2, reinterpret_cast<void *>(&snet_playerjoin_cb)) &&
      SNetRegisterEventHandler(3, reinterpret_cast<void *>(&snet_playerleave_cb)) &&
      SNetRegisterEventHandler(4, reinterpret_cast<void *>(&snet_servermessage_cb)) &&
      SNetRegisterEventHandler(1, reinterpret_cast<void *>(&snet_initdata_cb)))
    return TRUE;

  net_error_popup(0x56, "net_glue.cpp", 0xFE, 1);

fail:
  SNetDestroy();
  ResetLeagueEvt();
  return FALSE;
}

// 0x475A10 (6 B).
int NetworkMode()
{
  return sgxNetworkMode_g;
}

// 0x475A20 (62 B). It calls ResetLeagueEvt, which /Ob2 splices in: offsets
// +15..+49 are ResetLeagueEvt's 35 code bytes, so ResetLeagueEvt must stay in
// this TU and above this body. The tail `xor ecx, ecx / jmp sub_478110` is
// sub_478110(0) in tail position.
void NetworkShutdown()
{
  sgxNetworkMode_g = -1;
  SNetDestroy();
  ResetLeagueEvt();
  sub_4780E0();
  sub_478110(0);
}

// Runs the Storm upgrade check; 2 asks the caller to restart, -1 reports an
// error. The `<= 2` guard with `!= 2` inside and the unsigned -1 test keep
// 1.17's code shape.
BOOL handle_upgrade(int *pnOut)
{
  int nStatus;

  SNetPerformUpgrade(&nStatus);

  if (static_cast<unsigned int>(nStatus) <= 2) {
    if (nStatus != 2) {
      switch (nStatus) {
      case 0:
        return TRUE;
      case 1:
        return TRUE;
      }
      return FALSE;
    }
    *pnOut = 1;
    return FALSE;
  }
  if (static_cast<unsigned int>(nStatus) == 0xFFFFFFFFu)
    net_error_popup(0x6B, 0, 0, 1);
  return FALSE;
}
