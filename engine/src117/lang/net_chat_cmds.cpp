// net_chat_cmds.cpp -- the pre-game lobby command layer. `create_net_cmd_*`
// builds a lobby packet into a per-command singleton buffer and `recv_net_cmd_*`
// consumes one from another player: slot, race, team and force edits, the
// version handshake, map-download progress and the start countdown, chat-room
// state rather than game state.
//
// The SMemAlloc owner tag is 1.17's own "net_chat_cmds.cpp" string at 0x4EF014,
// and the line numbers are literals from 1.17, not __LINE__; each carries its
// decimal value.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>        // offsetof -- the layout static_asserts below
#include <string.h>        // strlen, strcat
#include <time.h>          // _time64 -- create_net_cmd_seed_init

#include "Storm.h"         // SMemAlloc (ord 401), SMemCopy (ord 491)
#include "net_cmds.h"      // PlayerData -- the 36-byte per-player slot record
#include "net_chat_cmds.h" // our own declarations + the packet layouts
#include "net_glue.h"      // IsGameCreator
#include "Teams.h"         // IsTeamGame, GetNumTeams, IsComputerTeam, GetTeamFreeSlot
#include "net_slot.h"      // swap_slots, net_slot_init, IsHumanPlayerType
#include "gluChat.h"       // sub_451790, sub_451770, on_chrm_kicked
#include "gluChatSlot.h"   // sub_451780, sub_4517D0, sub_451820
#include "net_chrm.h"      // packet_chat2, sub_471A40, GetSaveSlot
#include "dlg_lbox.h"      // combo_reset_slider, lbox_t
#include "net_player.h"    // net_player_get_name
#include "victory.h"       // psnprintf
#include "net_misc.h"      // sub_477860
#include "uiMulti.h"       // sub_4BB310
#include "net_rand.h"      // net_set_fixed_seed
#include "net_error.h"     // err_popup
#include "net_strt.h"      // begin_countdown
#include "net_time.h"      // net_time_init
#include "forces.h"        // IsComputerForce, GetForceOpenSlot, GetNextForceOpenSlot
#include "gametype.h"      // CanChangeRace, IsScenarioGame
#include "replay.h"        // sub_4807F0
#include "grid.h"          // sub_4634E0 -- create_net_cmd_version
#include "glues.h"         // IsSpawnedCopy -- create_net_cmd_version
#include "fd_sessn.h"         // sub_4409B0, sub_440BD0 -- recv_net_cmd_version_verify
#include "saveload.h"        // sub_499860 -- recv_net_cmd_version_verify
#include "../gen/globals117.h"

// 0x4EF014 -- "net_chat_cmds.cpp", this TU's allocator owner tag; every
// create_net_cmd_* body passes it to SMemAlloc and recv_net_cmd_version passes
// it to IsGameCreator. Dual-spelled so the payload build bakes the address.
#ifdef OC_DLL_MODE
#define sgszNetChatCmdsCpp (reinterpret_cast<const char *>(OC_VA(0x4EF014u)))
#else
extern const char sgszNetChatCmdsCpp[1];
#endif

// The lobby state bytes, typed from their accesses: 0x672600 a BYTE (the lobby
// state), 0x672601 a BYTE (the local download percentage, -1 when done),
// 0x672604 a DWORD (the countdown).
#ifdef OC_DLL_MODE
#define sgbChatState_g       (*reinterpret_cast<BYTE *>(OC_VA(0x672600u)))
#define sgbDownloadPct_g     (*reinterpret_cast<BYTE *>(OC_VA(0x672601u)))
#define sgdwCountdown_g      (*reinterpret_cast<DWORD *>(OC_VA(0x672604u)))
#else
extern BYTE  sgbChatState_g;
extern BYTE  sgbDownloadPct_g;
extern DWORD sgdwCountdown_g;
#endif

// The singleton command buffers. Each create_net_cmd_* owns one slot, tests it
// for null and allocates on first use; nothing ever frees them. Plain byte
// pointers, since two slots, 0x672598 and 0x6725B8, are 8-entry arrays indexed
// by player.
#ifdef OC_DLL_MODE
#define sgpNetCmdModifySlot_g        (reinterpret_cast<BYTE **>(OC_VA(0x672598u)))
#define sgpNetCmdVersionVerify_g     (reinterpret_cast<BYTE **>(OC_VA(0x6725B8u)))
#define sgpNetCmdStartGame_g         (*reinterpret_cast<BYTE **>(OC_VA(0x6725D8u)))
#define sgpNetCmdDownloadProgress_g  (*reinterpret_cast<BYTE **>(OC_VA(0x6725DCu)))
#define sgpNetCmdVersion_g           (*reinterpret_cast<BYTE **>(OC_VA(0x6725E0u)))
#define sgpNetCmdChangeRace_g        (*reinterpret_cast<BYTE **>(OC_VA(0x6725E4u)))
#define sgpNetCmdChangeTeam_g        (*reinterpret_cast<BYTE **>(OC_VA(0x6725E8u)))
#define sgpNetCmdChangeForce_g       (*reinterpret_cast<BYTE **>(OC_VA(0x6725ECu)))
#define sgpNetCmdChangeSlot_g        (*reinterpret_cast<BYTE **>(OC_VA(0x6725F0u)))
#define sgpNetCmdSwapSlots_g         (*reinterpret_cast<BYTE **>(OC_VA(0x6725F4u)))
#define sgpNetCmdSeedInit_g          (*reinterpret_cast<BYTE **>(OC_VA(0x6725F8u)))
#define sgpNetCmdMakePublic_g        (*reinterpret_cast<BYTE **>(OC_VA(0x6725FCu)))
#else
extern BYTE *sgpNetCmdModifySlot_g[8];
extern BYTE *sgpNetCmdVersionVerify_g[8];
extern BYTE *sgpNetCmdStartGame_g;
extern BYTE *sgpNetCmdDownloadProgress_g;
extern BYTE *sgpNetCmdVersion_g;
extern BYTE *sgpNetCmdChangeRace_g;
extern BYTE *sgpNetCmdChangeTeam_g;
extern BYTE *sgpNetCmdChangeForce_g;
extern BYTE *sgpNetCmdChangeSlot_g;
extern BYTE *sgpNetCmdSwapSlots_g;
extern BYTE *sgpNetCmdSeedInit_g;
extern BYTE *sgpNetCmdMakePublic_g;
#endif

// 0x672660 gdwDefTurnsInTransit (net_data.cpp), a DWORD: recv_net_cmd_StartGame
// and sub_470930 load it whole into address arithmetic.
#ifdef OC_DLL_MODE
#define gdwDefTurnsInTransit (*reinterpret_cast<DWORD *>(OC_VA(0x672660u)))
#else
extern DWORD gdwDefTurnsInTransit;
#endif

// The /reply name, sheet-named gReplyPlayerName_g (net_data.cpp); sub_4703D0
// compares it against each human player's name. Spelled as gluChat.cpp spells
// it.
#ifndef OC_gReplyPlayerName_g
#define OC_gReplyPlayerName_g
#ifdef OC_DLL_MODE
#define gReplyPlayerName_g (reinterpret_cast<char *>(OC_VA(0x6726ACu)))
#else
extern char gReplyPlayerName_g[];
#endif
#endif

// The session recv_net_cmd_version_verify registers a joiner with, dword_672960
// (net_glue.cpp), typed as gluChat.cpp types it.
#ifndef OC_dword_672960
#define OC_dword_672960
#ifdef OC_DLL_MODE
#define dword_672960 (*reinterpret_cast<TFD_SESSION **>(OC_VA(0x672960u)))
#else
extern TFD_SESSION *dword_672960;
#endif
#endif

// A byte per saved-game slot, indexed by the joiner's slot in the save and
// copied into its nRace. No sheet name; the extent is not derived, so it is
// declared unsized.
#ifndef OC_byte_5264A0
#define OC_byte_5264A0
#ifdef OC_DLL_MODE
#define byte_5264A0 (reinterpret_cast<BYTE *>(OC_VA(0x5264A0u)))
#else
extern BYTE byte_5264A0[];
#endif
#endif

// _strnicmp is declared here and not in a header: a header copy gives gen's
// thunk C linkage and it collides with LIBCMT (gluBNProfile.h records this).
extern "C" {
int __cdecl _strnicmp(const char *pszA, const char *pszB, size_t nLen);
}


// `mov al, byte ptr [0x672600] / ret`, the lobby state. Declared in
// src117/h/pause.h.
BYTE get_lobby_state()
{
  return sgbChatState_g;
}

// `mov byte ptr [0x672601], -1h / ret`: -1 marks the local download finished,
// apart from any 0..100 percentage.
void mark_local_download_completed()
{
  sgbDownloadPct_g = 0xFF;
}

// The state write is unconditional; only the countdown reset (`cmovz`) depends
// on the state being 2.
void sub_470910(BYTE bState)
{
  sgbChatState_g = bState;
  sgdwCountdown_g = (bState == 2) ? 0 : sgdwCountdown_g;
}

// The nullary builder: allocate the singleton on
// first use, then write the opcode through the pointer just stored.
void create_net_cmd_start_game()
{
  if (sgpNetCmdStartGame_g == 0)
    sgpNetCmdStartGame_g = static_cast<BYTE *>(
        SMemAlloc(sizeof(net_chat_cmd_op_t), sgszNetChatCmdsCpp, 0xF0, 0));

  reinterpret_cast<net_chat_cmd_op_t *>(sgpNetCmdStartGame_g)->bOpcode = 0x3C;
}

// Same shape, opcode 0x5B, line 0x3F3.
void create_net_cmd_make_public()
{
  if (sgpNetCmdMakePublic_g == 0)
    sgpNetCmdMakePublic_g = static_cast<BYTE *>(
        SMemAlloc(sizeof(net_chat_cmd_op_t), sgszNetChatCmdsCpp, 0x3F3, 0));

  reinterpret_cast<net_chat_cmd_op_t *>(sgpNetCmdMakePublic_g)->bOpcode = 0x5B;
}

// The two-argument builder. The payload bytes are
// stored at +1 and +2 before the opcode at +0, as in 1.17.
void create_net_cmd_change_slot(BYTE bArg0, BYTE bArg1)
{
  if (sgpNetCmdChangeSlot_g == 0)
    sgpNetCmdChangeSlot_g = static_cast<BYTE *>(
        SMemAlloc(sizeof(net_chat_cmd_op2_t), sgszNetChatCmdsCpp, 0x325, 0));

  net_chat_cmd_op2_t *pCmd =
      reinterpret_cast<net_chat_cmd_op2_t *>(sgpNetCmdChangeSlot_g);
  pCmd->bArg0 = bArg0;
  pCmd->bArg1 = bArg1;
  pCmd->bOpcode = 0x44;
}

// Identical shape, opcode 0x45, line 0x369.
void create_net_cmd_swap_slots(BYTE bArg0, BYTE bArg1)
{
  if (sgpNetCmdSwapSlots_g == 0)
    sgpNetCmdSwapSlots_g = static_cast<BYTE *>(
        SMemAlloc(sizeof(net_chat_cmd_op2_t), sgszNetChatCmdsCpp, 0x369, 0));

  net_chat_cmd_op2_t *pCmd =
      reinterpret_cast<net_chat_cmd_op2_t *>(sgpNetCmdSwapSlots_g);
  pCmd->bArg0 = bArg0;
  pCmd->bArg1 = bArg1;
  pCmd->bOpcode = 0x45;
}

// The guarded builder. Nothing is sent when the
// local player is already on that team: gPlayerData[g_LocalNationID].nTeam (0x6726D2) is
// compared first.
void create_net_cmd_change_team(BYTE bTeam)
{
  if (gPlayerData[g_LocalNationID].nTeam == bTeam)
    return;

  if (sgpNetCmdChangeTeam_g == 0)
    sgpNetCmdChangeTeam_g = static_cast<BYTE *>(
        SMemAlloc(sizeof(net_chat_cmd_op1_t), sgszNetChatCmdsCpp, 0x2B5, 0));

  net_chat_cmd_op1_t *pCmd =
      reinterpret_cast<net_chat_cmd_op1_t *>(sgpNetCmdChangeTeam_g);
  pCmd->bOpcode = 0x42;
  pCmd->bArg = bTeam;
}

// The array-indexed builder. The slot pointer is
// re-read after the store, unlike the singleton builders, since the index comes from the
// caller's record. The five payload bytes are the record's dwPlayerID and dwStormID low
// bytes and its three BYTE fields.
void create_net_cmd_modify_slot(const PlayerData *pSlot)
{
  BYTE bIndex = static_cast<BYTE>(pSlot->dwPlayerID);

  if (sgpNetCmdModifySlot_g[bIndex] == 0)
    sgpNetCmdModifySlot_g[bIndex] = static_cast<BYTE *>(
        SMemAlloc(sizeof(net_chat_cmd_slot_t), sgszNetChatCmdsCpp, 0x148, 0));

  net_chat_cmd_slot_t *pCmd =
      reinterpret_cast<net_chat_cmd_slot_t *>(sgpNetCmdModifySlot_g[bIndex]);
  pCmd->bOpcode = 0x3E;
  pCmd->bPlayerID = static_cast<BYTE>(pSlot->dwPlayerID);
  pCmd->bStormID = static_cast<BYTE>(pSlot->dwStormID);
  pCmd->nType = pSlot->nType;
  pCmd->nRace = pSlot->nRace;
  pCmd->nTeam = pSlot->nTeam;
}

// The creator-only team edit. Every guard is a
// separate early return to one epilogue. The sender's slot is g_StormToNationID[nPlayer],
// bounded below 8 before use. The packet byte is read three times rather than cached.
void recv_net_cmd_change_team(const void *pCmd, int nPlayer)
{
  const net_chat_cmd_op1_t *pTeam =
      reinterpret_cast<const net_chat_cmd_op1_t *>(pCmd);

  if (!IsGameCreator(sgszNetChatCmdsCpp, 0x2BD))
    return;
  if (sgbChatState_g < 4)
    return;
  if (!IsTeamGame())
    return;
  if (pTeam->bArg == 0)
    return;
  if (pTeam->bArg > GetNumTeams())
    return;
  if (IsComputerTeam(pTeam->bArg))
    return;

  DWORD dwSlot = g_StormToNationID[nPlayer];
  if (dwSlot >= 8)
    return;
  if (gPlayerData[dwSlot].nTeam == pTeam->bArg)
    return;
  if (gPlayerData[dwSlot].nType != 2)
    return;

  BYTE bOther = static_cast<BYTE>(GetTeamFreeSlot(pTeam->bArg));
  if (bOther >= 8)
    return;
  if (!sub_4517D0(static_cast<BYTE>(dwSlot), bOther))
    return;

  swap_slots(static_cast<BYTE>(dwSlot), bOther);
  create_net_cmd_modify_slot(&gPlayerData[dwSlot]);
  create_net_cmd_modify_slot(&gPlayerData[bOther]);
  sub_451790();
}

// 0x66A274, the current glue-screen index (glGluesMode), a DWORD at every site;
// spelled as src117/lang/glues.cpp spells it.
#ifdef OC_DLL_MODE
#define sgnGlueScreen (*reinterpret_cast<int *>(OC_VA(0x66A274u)))
#else
extern int sgnGlueScreen;
#endif

// net_chat_msg_t, the 0x52-byte lobby chat packet, is declared in
// src117/h/net_chat_cmds.h.

// The lobby chat send path. The text is a
// `char *`: dereferenced first, SStrCopy's source and measured twice. strlen and strcat are
// inlined by /Oi. The length test is unsigned, so nLen is a size_t, and the two strlens
// come in 1.17's order: szMsg first, then pszText.
void sub_4701F0(const char *pszText, int nPlayer)
{
  char szMsg[0x104];
  net_chat_msg_t cmd;
  size_t nLen;
  int nTotal;
  WORD wMode;

  if (*pszText == 0)
    return;
  if (!IsValidPlayer(static_cast<BYTE>(nPlayer)))
    return;

  wMode = gwGameMode;
  if (wMode == 4) {
    if (sgnGlueScreen != 3)
      return;
    packet_chat2(pszText, nPlayer);
    wMode = gwGameMode;
  }
  if (wMode != 3)
    return;

  nLen = strlen(pszText) + 1;
  if (nLen > 0x50)
    return;

  cmd.bOpcode = 1;
  SStrCopy(cmd.szText, pszText, 0x50);
  net_send_message(nPlayer, &cmd, static_cast<int>(nLen + 2));

  psnprintf(szMsg, 0x104, error_get_str(0x4F),
            net_player_get_name(static_cast<BYTE>(nPlayer)));

  nTotal = static_cast<int>(strlen(szMsg)) +
           static_cast<int>(strlen(pszText)) + 2;
  strcat(szMsg, " ");
  SStrPack(szMsg, pszText, nTotal < 0x104 ? nTotal : 0x104);
  map_msg_chat_msg(szMsg, 0);
}

// The slot-record install. IsGameCreator is
// inverted here against its other uses in this TU: this receiver runs only on a
// non-creator. The record is cleared with an inline 36-byte memset. A 0xFF Storm id in the
// packet becomes -1, "no Storm id". The second IsHumanPlayerType reads the record's type,
// not the packet's.
void recv_net_cmd_modify_slot(const void *pCmd, int nPlayer)
{
  const net_chat_cmd_slot_t *pSlot =
      reinterpret_cast<const net_chat_cmd_slot_t *>(pCmd);

  if (nPlayer != 0)
    return;
  if (pSlot->bPlayerID >= 8)
    return;
  if (pSlot->bStormID >= 8 && pSlot->bStormID != 0xFF)
    return;
  if (IsGameCreator(sgszNetChatCmdsCpp, 0x15F))
    return;

  BYTE bSlot = pSlot->bPlayerID;
  int nStormID = (pSlot->bStormID == 0xFF) ? -1 : pSlot->bStormID;

  if (IsHumanPlayerType(pSlot->nType) &&
      !net_player_exists(static_cast<BYTE>(nStormID)))
    return;

  if (sgbChatState_g == 3)
    sgbChatState_g = 4;
  else if (sgbChatState_g < 4)
    return;

  memset(&gPlayerData[bSlot], 0, sizeof(PlayerData));
  gPlayerData[bSlot].dwPlayerID = bSlot;
  gPlayerData[bSlot].dwStormID = nStormID;
  gPlayerData[bSlot].nType = pSlot->nType;
  gPlayerData[bSlot].nRace = pSlot->nRace;
  gPlayerData[bSlot].nTeam = pSlot->nTeam;

  if (IsHumanPlayerType(gPlayerData[bSlot].nType))
    net_get_player_name(nStormID, gPlayerData[bSlot].szName, 0x19,
                        sgszNetChatCmdsCpp, 0x176);

  net_slot_init(g_LocalStormID);
  sub_451790();
}

// The host's handler for a slot-type change: open, computer or closed. The
// new type is checked with the chat-room callback, applied, and every slot the
// change touches is sent back out; in a team game that is the whole team.
void recv_net_cmd_change_slot(const void *pCmd, int nPlayer)
{
  const net_chat_cmd_op2_t *pSlot =
      reinterpret_cast<const net_chat_cmd_op2_t *>(pCmd);
  BYTE bType;

  if (!IsGameCreator(sgszNetChatCmdsCpp, 0x32E))
    return;
  if (sgbChatState_g < 4)
    return;
  if (pSlot->bArg0 >= 8)
    return;
  if (pSlot->bArg1 >= 3)
    return;
  if (nPlayer != 0)
    return;

  DWORD dwSlot = g_StormToNationID[nPlayer];
  if (dwSlot >= 8)
    return;

  switch (pSlot->bArg1) {
    case 0:
      bType = 5;
      break;
    case 1:
      bType = 6;
      break;
    case 2:
      bType = 8;
      break;
    default:
      return;
  }
  if (!sub_451820(pSlot->bArg0, bType))
    return;

  switch (pSlot->bArg1) {
    case 0:
      sub_478D30(pSlot->bArg0, dwSlot);
      break;
    case 1:
      sub_479760(pSlot->bArg0, dwSlot);
      break;
    case 2:
      sub_478C50(pSlot->bArg0, dwSlot);
      break;
  }

  if (IsTeamGame()) {
    PlayerData *pData = &gPlayerData[8];
    do {
      --pData;
      if (pData->nTeam == gPlayerData[pSlot->bArg0].nTeam)
        create_net_cmd_modify_slot(pData);
    } while (pData != gPlayerData.a);
  } else {
    create_net_cmd_modify_slot(&gPlayerData[pSlot->bArg0]);
  }
  sub_451790();

  if (IsGameCreator(sgszNetChatCmdsCpp, 0x35E))
    sub_476EF0(CountDefaultHumanSlots());
}

// The two-slot swap. The combo-box pointer the
// guard tests is also combo_reset_slider's argument. Each slot is copied whole through two
// 36-byte temporaries with SMemCopy, then gets its own dwPlayerID and nTeam back, so the
// swap moves everything else. The indexes are re-read from the packet at each use.
void recv_net_cmd_swap_slots(const void *pCmd, int nPlayer)
{
  const net_chat_cmd_op2_t *pSwap =
      reinterpret_cast<const net_chat_cmd_op2_t *>(pCmd);
  PlayerData recA;
  PlayerData recB;

  if (sgpCurrentCombo_g != 0 &&
      (pSwap->bArg0 == g_LocalHumanID || pSwap->bArg1 == g_LocalHumanID))
    combo_reset_slider(reinterpret_cast<lbox_t *>(sgpCurrentCombo_g));

  if (!IsGameCreator(sgszNetChatCmdsCpp, 0x37D))
    return;
  if (nPlayer != 0)
    return;
  if (sgbChatState_g < 4)
    return;
  if (pSwap->bArg0 == 0)
    return;
  if (pSwap->bArg0 >= 8)
    return;
  if (pSwap->bArg1 == 0)
    return;
  if (pSwap->bArg1 >= 8)
    return;
  if (pSwap->bArg0 == pSwap->bArg1)
    return;

  SMemCopy(&recA, &gPlayerData[pSwap->bArg0], sizeof(PlayerData));
  SMemCopy(&recB, &gPlayerData[pSwap->bArg1], sizeof(PlayerData));

  SMemCopy(&gPlayerData[pSwap->bArg0], &recB, sizeof(PlayerData));
  gPlayerData[pSwap->bArg0].dwPlayerID = recA.dwPlayerID;
  gPlayerData[pSwap->bArg0].nTeam = recA.nTeam;

  SMemCopy(&gPlayerData[pSwap->bArg1], &recA, sizeof(PlayerData));
  gPlayerData[pSwap->bArg1].dwPlayerID = recB.dwPlayerID;
  gPlayerData[pSwap->bArg1].nTeam = recB.nTeam;

  net_slot_init(g_LocalStormID);
  create_net_cmd_modify_slot(&gPlayerData[pSwap->bArg1]);
  create_net_cmd_modify_slot(&gPlayerData[pSwap->bArg0]);
  sub_451790();
}

// The teardown: two array loops, then ten single
// frees, each with its own line literal (8Fh to 97h, then 99h). The loops are
// `while (i--)`, as in 1.17.
void net_chat_close_lobby()
{
  int i;

  i = 8;
  while (i--) {
    if (sgpNetCmdModifySlot_g[i] != 0) {
      SMemFree(sgpNetCmdModifySlot_g[i], sgszNetChatCmdsCpp, 0x8B, 0);
      sgpNetCmdModifySlot_g[i] = 0;
    }
  }

  i = 8;
  while (i--) {
    if (sgpNetCmdVersionVerify_g[i] != 0) {
      SMemFree(sgpNetCmdVersionVerify_g[i], sgszNetChatCmdsCpp, 0x8D, 0);
      sgpNetCmdVersionVerify_g[i] = 0;
    }
  }

  if (sgpNetCmdStartGame_g != 0) {
    SMemFree(sgpNetCmdStartGame_g, sgszNetChatCmdsCpp, 0x8F, 0);
    sgpNetCmdStartGame_g = 0;
  }
  if (sgpNetCmdDownloadProgress_g != 0) {
    SMemFree(sgpNetCmdDownloadProgress_g, sgszNetChatCmdsCpp, 0x90, 0);
    sgpNetCmdDownloadProgress_g = 0;
  }
  if (sgpNetCmdVersion_g != 0) {
    SMemFree(sgpNetCmdVersion_g, sgszNetChatCmdsCpp, 0x91, 0);
    sgpNetCmdVersion_g = 0;
  }
  if (sgpNetCmdChangeRace_g != 0) {
    SMemFree(sgpNetCmdChangeRace_g, sgszNetChatCmdsCpp, 0x92, 0);
    sgpNetCmdChangeRace_g = 0;
  }
  if (sgpNetCmdChangeTeam_g != 0) {
    SMemFree(sgpNetCmdChangeTeam_g, sgszNetChatCmdsCpp, 0x93, 0);
    sgpNetCmdChangeTeam_g = 0;
  }
  if (sgpNetCmdChangeForce_g != 0) {
    SMemFree(sgpNetCmdChangeForce_g, sgszNetChatCmdsCpp, 0x94, 0);
    sgpNetCmdChangeForce_g = 0;
  }
  if (sgpNetCmdChangeSlot_g != 0) {
    SMemFree(sgpNetCmdChangeSlot_g, sgszNetChatCmdsCpp, 0x95, 0);
    sgpNetCmdChangeSlot_g = 0;
  }
  if (sgpNetCmdSwapSlots_g != 0) {
    SMemFree(sgpNetCmdSwapSlots_g, sgszNetChatCmdsCpp, 0x96, 0);
    sgpNetCmdSwapSlots_g = 0;
  }
  if (sgpNetCmdSeedInit_g != 0) {
    SMemFree(sgpNetCmdSeedInit_g, sgszNetChatCmdsCpp, 0x97, 0);
    sgpNetCmdSeedInit_g = 0;
  }
  if (sgpNetCmdMakePublic_g != 0) {
    SMemFree(sgpNetCmdMakePublic_g, sgszNetChatCmdsCpp, 0x99, 0);
    sgpNetCmdMakePublic_g = 0;
  }
}

// The trailing `jmp sub_451780` is a tail call. ECX is never read; the packet
// argument is declared as its siblings' is.
void recv_net_cmd_make_public(const void *pCmd, int nPlayer)
{
  if (nPlayer == 0)
  {
    sub_477860();
    sub_451780();
  }
}

// The clamp is 1.17's bug, kept: the unsigned `cmovnbe` replaces a percentage
// above 100 with 0, not 100, so a peer reporting 101% shows as 0%.
void recv_net_cmd_download_progress(const void *pCmd, int nPlayer)
{
  BYTE bPct = reinterpret_cast<const net_chat_cmd_op1_t *>(pCmd)->bArg;
  DWORD dwPct = bPct;

  if (bPct > 100)
    dwPct = 0;

  sub_4BB310(nPlayer, static_cast<BYTE>(dwPct));
}

// The `push 1h` at the loop's top is shared: sub_471A40's `ret 4h` takes it on
// the normal path, and on the error path it becomes err_popup's last argument.
// sub_4807F0 takes the packet in ECX, still there from entry.
void recv_net_cmd_seed_init(const void *pCmd, int nPlayer)
{
  const BYTE *p = reinterpret_cast<const BYTE *>(pCmd);

  if (nPlayer != 0 || sgbChatState_g != 8)
    return;

  int nSeed = *reinterpret_cast<const int *>(p + 1);

  if (gPlayback == 0)
    sub_4807F0(const_cast<void *>(pCmd));

  net_set_fixed_seed(nSeed);

  for (int i = 8; i != 0; )
  {
    BYTE bTeam = p[i + 4];
    --i;

    if (bTeam > 8)
    {
      err_popup(0x60, 0, 0, 0, 1);
      return;
    }

    sub_471A40(static_cast<BYTE>(i), bTeam, 1);
  }
}

// The 432-byte copy moves twelve PlayerData records from gPlayerData into
// gPlayerSlots_g, reading 4 records past gPlayerData's 8: 1.17's over-read,
// kept, so it is a byte memcpy. begin_countdown is passed the 24
// already in ECX; it ignores it.
void recv_net_cmd_StartGame(const void *pCmd, int nPlayer)
{
  if (sgbChatState_g != 4)
  {
    on_chrm_kicked(4);
    return;
  }

  if (nPlayer != 0)
    return;

  if (!sub_477E90())
  {
    on_chrm_kicked(9);
    return;
  }

  if (gPlayback != 0)
    PlayerCopy(gPlayerSlots_g, gPlayerData);  // Hyperion: and Hyperion's slots

  sgbChatState_g = 5;

  DWORD dwTicks = gdwDefTurnsInTransit * 2 + 4;
  sgdwCountdown_g = (dwTicks < 24) ? 24 : dwTicks;

  begin_countdown(24);
  net_time_init();
}

// The `cmp al, -1h` arm is dead (the byte was already bounded below 8) and is
// kept. The flags slot is 32-bit and net_player_get_flags fills only its low
// word; the else arm binds the masked value to a `const WORD &` so that both
// arms share one 16-bit load, as in 1.17.
void recv_net_cmd_version(const void *pCmd, int nPlayer)
{
  const net_chat_cmd_verinfo_t *p =
      reinterpret_cast<const net_chat_cmd_verinfo_t *>(pCmd);

  if (sgbChatState_g < 4)
    return;

  if (nPlayer != 0)
    return;

  if (p->bPlayerID >= 8)
    return;

  if (IsGameCreator(sgszNetChatCmdsCpp, 0x1B5))
    return;

  int nId = (p->bPlayerID == 0xFF) ? -1 : p->bPlayerID;

  DWORD dwFlags;

  if (net_player_get_flags(static_cast<BYTE>(nId),
                           reinterpret_cast<WORD *>(&dwFlags)))
    *reinterpret_cast<WORD *>(&dwFlags) |=
        static_cast<WORD>(p->wFlags & 0xFF03);
  else
  {
    // The reference keeps the value 16-bit; see the note above.
    const WORD &wNew = static_cast<WORD>(p->wFlags & 0xFF03);
    dwFlags = wNew;
  }

  net_player_set_data(static_cast<BYTE>(nId), static_cast<WORD>(dwFlags), p->wValue04,
             p->wValue06);
}

// The countdown tick. Every read of sgdwCountdown_g and gdwDefTurnsInTransit is
// a fresh load after each call, so the source reads the globals directly.
// bState is a local read after the if.
void sub_470930()
{
  if (sgdwCountdown_g == 0)
    return;

  sgdwCountdown_g = sgdwCountdown_g - 1;

  if (sgdwCountdown_g == gdwDefTurnsInTransit * 2 + 4)
  {
    sub_451770();
    sgbChatState_g = 6;
  }

  BYTE bState = sgbChatState_g;

  if (sgdwCountdown_g == gdwDefTurnsInTransit + 2)
  {
    if (IsGameCreator(sgszNetChatCmdsCpp, 0xDA))
    {
      sgbChatState_g = 7;
      create_net_cmd_seed_init();
      bState = sgbChatState_g;
    }
    else
    {
      bState = 8;
    }
  }

  sgbChatState_g = (sgdwCountdown_g == 0) ? 9 : bState;
}

// Two exits tail-jump to sub_451790 and one is a plain `ret`, so the arms that
// changed something are early returns. The team loop walks gPlayerData's eight
// records by pointer.
void recv_net_cmd_change_race(const void *pCmd, int nPlayer)
{
  const net_chat_cmd_op2_t *p =
      reinterpret_cast<const net_chat_cmd_op2_t *>(pCmd);

  if (!IsGameCreator(sgszNetChatCmdsCpp, 0x288))
    return;

  if (sgbChatState_g < 4)
    return;

  DWORD dwNation = g_StormToNationID[nPlayer];
  BYTE bSlot = (p->bArg0 == 8) ? static_cast<BYTE>(dwNation) : p->bArg0;

  if (!CanChangeRace(bSlot, p->bArg1, dwNation))
    return;

  BYTE bTeam = gPlayerData[bSlot].nTeam;

  if (IsTeamGame() && IsComputerTeam(bTeam))
  {
    for (int i = 0; i < 8; ++i)
    {
      if (gPlayerData[i].nTeam == bTeam)
      {
        gPlayerData[i].nRace = p->bArg1;
        create_net_cmd_modify_slot(&gPlayerData[i]);
      }
    }

    sub_451790();
    return;
  }

  gPlayerData[bSlot].nRace = p->bArg1;
  create_net_cmd_modify_slot(&gPlayerData[bSlot]);
  sub_451790();
}

// GetNextForceOpenSlot and GetForceOpenSlot are the two arms of one ternary
// stored into one byte, so GetNextForceOpenSlot returns a BYTE. The packet field
// is read again after IsComputerForce, as in 1.17.
void recv_net_cmd_change_force(const void *pCmd, int nPlayer)
{
  const net_chat_cmd_op1_t *p =
      reinterpret_cast<const net_chat_cmd_op1_t *>(pCmd);

  if (!IsGameCreator(sgszNetChatCmdsCpp, 0x2F7))
    return;

  if (sgbChatState_g < 4)
    return;

  if (!IsScenarioGame(NULL))
    return;

  if (p->bArg == 0)
    return;

  if (p->bArg > 4)
    return;

  if (IsComputerForce(p->bArg))
    return;

  DWORD dwNation = g_StormToNationID[nPlayer];

  if (dwNation >= 8)
    return;

  if (gPlayerData[dwNation].nType != 2)
    return;

  BYTE bOther;

  if (gPlayerData[dwNation].nTeam == p->bArg)
    bOther = GetNextForceOpenSlot(p->bArg, static_cast<BYTE>(dwNation));
  else
    bOther = GetForceOpenSlot(p->bArg);

  if (bOther >= 8)
    return;

  swap_slots(static_cast<BYTE>(dwNation), bOther);
  create_net_cmd_modify_slot(&gPlayerData[dwNation]);
  create_net_cmd_modify_slot(&gPlayerData[bOther]);
  sub_451790();
}

// Payload first, opcode last -- the
// create_net_cmd_change_slot store order, one argument instead of two.
void create_net_cmd_change_force(BYTE bForce)
{
  if (sgpNetCmdChangeForce_g == 0)
    sgpNetCmdChangeForce_g = static_cast<BYTE *>(
        SMemAlloc(sizeof(net_chat_cmd_op1_t), sgszNetChatCmdsCpp, 0x2EF, 0));

  net_chat_cmd_op1_t *pCmd =
      reinterpret_cast<net_chat_cmd_op1_t *>(sgpNetCmdChangeForce_g);
  pCmd->bArg = bForce;
  pCmd->bOpcode = 0x43;
}

// The early exit tests the same byte the body then writes, so 0xFF, the finished
// sentinel, is never sent as a percentage. The opcode is stored first here.
void create_net_cmd_download_progress(BYTE bPercent)
{
  if (sgbDownloadPct_g == bPercent)
    return;

  sgbDownloadPct_g = bPercent;

  if (sgpNetCmdDownloadProgress_g == 0)
    sgpNetCmdDownloadProgress_g = static_cast<BYTE *>(
        SMemAlloc(sizeof(net_chat_cmd_op1_t), sgszNetChatCmdsCpp, 0x12C, 0));

  net_chat_cmd_op1_t *pCmd =
      reinterpret_cast<net_chat_cmd_op1_t *>(sgpNetCmdDownloadProgress_g);
  pCmd->bOpcode = 0x3D;
  pCmd->bArg = bPercent;
}

// The local player addresses its own slot as 8 rather than by its nation id (a
// cmovz). The stores go +1, +0, +2.
void create_net_cmd_change_race(BYTE bArg0, BYTE bArg1)
{
  BYTE bSlot = bArg0;
  if (bArg0 == g_LocalNationID)
    bSlot = 8;

  if (sgpNetCmdChangeRace_g == 0)
    sgpNetCmdChangeRace_g = static_cast<BYTE *>(
        SMemAlloc(sizeof(net_chat_cmd_op2_t), sgszNetChatCmdsCpp, 0x27D, 0));

  net_chat_cmd_op2_t *pCmd =
      reinterpret_cast<net_chat_cmd_op2_t *>(sgpNetCmdChangeRace_g);
  pCmd->bArg0 = bSlot;
  pCmd->bOpcode = 0x41;
  pCmd->bArg1 = bArg1;
}

// _time64, not time: it is 1.17's callee, and VS2013's time() forwards to it.
// Only the low half of the 64-bit result goes into the packet's DWORD at +1. The
// slot loop runs 7 down to 0 and re-reads the global each time, across the
// GetSaveSlot call.
void create_net_cmd_seed_init()
{
  if (sgpNetCmdSeedInit_g == 0)
    sgpNetCmdSeedInit_g = static_cast<BYTE *>(
        SMemAlloc(sizeof(net_chat_cmd_seed_t), sgszNetChatCmdsCpp, 0x3AA, 0));

  reinterpret_cast<net_chat_cmd_seed_t *>(sgpNetCmdSeedInit_g)->bOpcode = 0x48;

  __time64_t nNow = _time64(NULL);
  net_chat_cmd_seed_t *pCmd =
      reinterpret_cast<net_chat_cmd_seed_t *>(sgpNetCmdSeedInit_g);
  pCmd->dwSeed = static_cast<DWORD>(nNow);

  if (gPlayback != 0)
    sub_480540(pCmd);

  int i = 8;
  do {
    --i;
    reinterpret_cast<net_chat_cmd_seed_t *>(sgpNetCmdSeedInit_g)->bSlots[i] =
        GetSaveSlot(static_cast<BYTE>(i));
  } while (i != 0);
}

// The parameter is an index into gPlayerData, not a pointer:
// recv_net_cmd_version_verify drives it with a plain loop counter. The unsigned
// `cmp ebx, 8h / jnb` bounds sgpNetCmdVersionVerify_g, so dwStormID is a DWORD.
// The three out-param locals are 4 bytes each; sub_477FD0 writes only 16 bits
// through each.
void create_net_cmd_version_verify(int nIdx)
{
  DWORD dwStorm = gPlayerData[nIdx].dwStormID;
  if (dwStorm >= 8)
    return;

  if (sgpNetCmdVersionVerify_g[dwStorm] == 0)
    sgpNetCmdVersionVerify_g[dwStorm] = static_cast<BYTE *>(
        SMemAlloc(sizeof(net_chat_cmd_verinfo_t), sgszNetChatCmdsCpp, 0x18A, 0));

  reinterpret_cast<net_chat_cmd_verinfo_t *>(
      sgpNetCmdVersionVerify_g[dwStorm])->bOpcode = 0x3F;

  DWORD dwFlags = 0;
  DWORD dwUnk04 = 0;
  DWORD dwUnk06 = 0;
  sub_477FD0(static_cast<BYTE>(dwStorm), reinterpret_cast<WORD *>(&dwFlags),
             reinterpret_cast<WORD *>(&dwUnk04),
             reinterpret_cast<WORD *>(&dwUnk06));

  net_chat_cmd_verinfo_t *pCmd = reinterpret_cast<net_chat_cmd_verinfo_t *>(
      sgpNetCmdVersionVerify_g[dwStorm]);
  pCmd->bPlayerID = static_cast<BYTE>(dwStorm);
  pCmd->wFlags = static_cast<WORD>(dwFlags);
  pCmd->wValue04 = static_cast<WORD>(dwUnk04);
  pCmd->wValue06 = static_cast<WORD>(dwUnk06);
}

// The only `ret 14h` in the family.
// The stores go +1, +2, +0, +3, +7, +9, +0x0B, the opcode between the byte
// payloads and the dword, as in 1.17. With nArg6 the packet goes straight into
// the local receive path and is freed, which is how a host-local handshake skips
// the network.
void create_net_cmd_version(WORD wArg0, WORD wArg1, WORD wArg2, BYTE bArg3,
                            BYTE bArg4, int nArg5, int nArg6)
{
  // The local takes SMemAlloc's result and the global is written from it, which
  // keeps 1.17's store schedule.
  net_chat_cmd_version_t *pCmd =
      reinterpret_cast<net_chat_cmd_version_t *>(sgpNetCmdVersion_g);
  if (pCmd == 0) {
    pCmd = static_cast<net_chat_cmd_version_t *>(
        SMemAlloc(sizeof(net_chat_cmd_version_t), sgszNetChatCmdsCpp, 0x1C7, 0));
    sgpNetCmdVersion_g = reinterpret_cast<BYTE *>(pCmd);
  }
  pCmd->bOpcode = 0x40;
  pCmd->bArg01 = bArg4;
  pCmd->bArg02 = bArg3;
  pCmd->dwArg03 = nArg5;
  pCmd->wArg07 = wArg0;
  pCmd->wArg09 = wArg1;
  pCmd->wArg0B = wArg2;

  reinterpret_cast<net_chat_cmd_version_t *>(sgpNetCmdVersion_g)->bSpawned =
      static_cast<BYTE>(IsSpawnedCopy());
  int nSyncHash = sub_4634E0();
  net_chat_cmd_version_t *pVer =
      reinterpret_cast<net_chat_cmd_version_t *>(sgpNetCmdVersion_g);
  pVer->dwArg0E = nSyncHash;

  if (nArg6 != 0) {
    recv_net_cmd_version_verify(pVer, g_LocalStormID, 1);
    SMemFree(sgpNetCmdVersion_g, sgszNetChatCmdsCpp, 0x1DE, 0);
    sgpNetCmdVersion_g = 0;
  }
}

// The lobby teardown. Flushes every pending lobby
// command: each buffer that exists is queued with its own length (1, 2, 3, 6, 8, 0x0D,
// 0x12, the create_* sizes), then freed and nulled. The two array sweeps run 7 down to 0.
void sub_470460()
{
  BYTE bState = sgbChatState_g;
  if (bState == 7)
    sgbChatState_g = 8;
  else if (bState > 6)
    return;

  if (sgpNetCmdStartGame_g != 0) {
    netmgr_queue_cmd(sgpNetCmdStartGame_g, 1);
    SMemFree(sgpNetCmdStartGame_g, sgszNetChatCmdsCpp, 0xAD, 0);
    sgpNetCmdStartGame_g = 0;
  }

  if (sgpNetCmdDownloadProgress_g != 0) {
    netmgr_queue_cmd(sgpNetCmdDownloadProgress_g, 2);
    SMemFree(sgpNetCmdDownloadProgress_g, sgszNetChatCmdsCpp, 0xAE, 0);
    sgpNetCmdDownloadProgress_g = 0;
  }

  int i6 = 8;
  do {
    --i6;
    if (sgpNetCmdModifySlot_g[i6] != 0) {
      netmgr_queue_cmd(sgpNetCmdModifySlot_g[i6], 6);
      SMemFree(sgpNetCmdModifySlot_g[i6], sgszNetChatCmdsCpp, 0xB1, 0);
      sgpNetCmdModifySlot_g[i6] = 0;
    }
  } while (i6 != 0);

  int i8 = 8;
  do {
    --i8;
    if (sgpNetCmdVersionVerify_g[i8] != 0) {
      netmgr_queue_cmd(sgpNetCmdVersionVerify_g[i8], 8);
      SMemFree(sgpNetCmdVersionVerify_g[i8], sgszNetChatCmdsCpp, 0xB3, 0);
      sgpNetCmdVersionVerify_g[i8] = 0;
    }
  } while (i8 != 0);

  if (sgpNetCmdChangeRace_g != 0) {
    netmgr_queue_cmd(sgpNetCmdChangeRace_g, 3);
    SMemFree(sgpNetCmdChangeRace_g, sgszNetChatCmdsCpp, 0xB4, 0);
    sgpNetCmdChangeRace_g = 0;
  }

  if (sgpNetCmdChangeTeam_g != 0) {
    netmgr_queue_cmd(sgpNetCmdChangeTeam_g, 2);
    SMemFree(sgpNetCmdChangeTeam_g, sgszNetChatCmdsCpp, 0xB5, 0);
    sgpNetCmdChangeTeam_g = 0;
  }

  if (sgpNetCmdChangeForce_g != 0) {
    netmgr_queue_cmd(sgpNetCmdChangeForce_g, 2);
    SMemFree(sgpNetCmdChangeForce_g, sgszNetChatCmdsCpp, 0xB6, 0);
    sgpNetCmdChangeForce_g = 0;
  }

  if (sgpNetCmdChangeSlot_g != 0) {
    netmgr_queue_cmd(sgpNetCmdChangeSlot_g, 3);
    SMemFree(sgpNetCmdChangeSlot_g, sgszNetChatCmdsCpp, 0xB7, 0);
    sgpNetCmdChangeSlot_g = 0;
  }

  if (sgpNetCmdSwapSlots_g != 0) {
    netmgr_queue_cmd(sgpNetCmdSwapSlots_g, 3);
    SMemFree(sgpNetCmdSwapSlots_g, sgszNetChatCmdsCpp, 0xB8, 0);
    sgpNetCmdSwapSlots_g = 0;
  }

  if (sgpNetCmdSeedInit_g != 0) {
    netmgr_queue_cmd(sgpNetCmdSeedInit_g, 0x0D);
    SMemFree(sgpNetCmdSeedInit_g, sgszNetChatCmdsCpp, 0xB9, 0);
    sgpNetCmdSeedInit_g = 0;
  }

  if (sgpNetCmdMakePublic_g != 0) {
    netmgr_queue_cmd(sgpNetCmdMakePublic_g, 1);
    SMemFree(sgpNetCmdMakePublic_g, sgszNetChatCmdsCpp, 0xBB, 0);
    sgpNetCmdMakePublic_g = 0;
  }

  if (sgpNetCmdVersion_g != 0) {
    netmgr_queue_cmd(sgpNetCmdVersion_g, 0x12);
    SMemFree(sgpNetCmdVersion_g, sgszNetChatCmdsCpp, 0xBE, 0);
    sgpNetCmdVersion_g = 0;
  }
}

// 0x470320 (175 B). Matches a slash command's prefix, then a human player's
// name right after it. With bNeedSpace the name must be followed by a space,
// which is skipped. Answers the rest of the line and the slot, or NULL.
char *sub_470320(const char *pszText, const char *pszPrefix, int nPrefixLen, DWORD *pnSlot,
                 int bNeedSpace)
{
  DWORD i;
  size_t nLen;

  if (_strnicmp(pszText, pszPrefix, nPrefixLen) != 0)
    return NULL;

  pszText += nPrefixLen;
  for (i = 0; i < 8; i++) {
    if (gPlayerData[i].nType != 2)
      continue;
    nLen = strlen(gPlayerData[i].szName);
    if (nLen == 0)
      continue;
    if (_strnicmp(pszText, gPlayerData[i].szName, nLen) != 0)
      continue;
    if (bNeedSpace && pszText[nLen++] != ' ')
      continue;
    *pnSlot = i;
    return const_cast<char *>(pszText + nLen);
  }
  return NULL;
}

// 0x4703D0 (134 B). Matches a slash command's prefix, then looks for the
// human player whose name is the /reply name. The rest of the line comes back
// whether or not one is found; the slot is written only when one is.
char *sub_4703D0(const char *pszText, const char *pszPrefix, int nPrefixLen, DWORD *pnSlot)
{
  DWORD i;
  size_t nLen;

  if (_strnicmp(pszText, pszPrefix, nPrefixLen) != 0)
    return NULL;

  pszText += nPrefixLen;
  for (i = 0; i < 8; i++) {
    if (gPlayerData[i].nType != 2)
      continue;
    nLen = strlen(gPlayerData[i].szName);
    if (nLen == 0)
      continue;
    if (_strnicmp(gReplyPlayerName_g, gPlayerData[i].szName, nLen) == 0) {
      *pnSlot = i;
      return const_cast<char *>(pszText);
    }
  }
  return const_cast<char *>(pszText);
}


// 0x470AF0 (656 B). The host's handler for a joiner's version packet: kicks on
// a mismatch, otherwise seats the player and sends it the lobby. Every slot
// the player already held is reopened first.
void recv_net_cmd_version_verify(const void *pCmd, int nPlayer, int nLocal)
{
  const net_chat_cmd_version_t *pVer = reinterpret_cast<const net_chat_cmd_version_t *>(pCmd);
  DWORD dwSaveSlot;
  DWORD dwSaveRace;
  DWORD dwSlot;
  WORD wFlags;
  DWORD i;

  sgbDownloadPct_g = 0xFF;
  if (!IsGameCreator(sgszNetChatCmdsCpp, 0x1FA))
    return;

  if (sgbChatState_g != 4 && nLocal == 0) {
    packet_kick(nPlayer, 4);
    return;
  }
  // Constant first: `mov eax, 5h / cmp ax, [edi+0Bh]`.
  if (5 != pVer->wArg0B) {
    packet_kick(nPlayer, 7);
    return;
  }
  if (pVer->bSpawned && sub_4634E0() != pVer->dwArg0E) {
    packet_kick(nPlayer, 8);
    return;
  }

  dwSaveSlot = pVer->bArg01;
  dwSaveRace = pVer->bArg02;
  if (gpLoadGameFile != NULL &&
      (dwSaveSlot >= 8 || dwSaveRace >= 8 || pVer->dwArg03 != sub_499860())) {
    packet_kick(nPlayer, 5);
    return;
  }

  if (IsScenarioGame(NULL) && gpLoadGameFile == NULL)
    dwSlot = GetFreeSlotFromEmptiestForce();
  else if ((IsTeamGame() || IsScenarioGame(NULL)) && gpLoadGameFile != NULL)
    dwSlot = dwSaveSlot;
  else
    dwSlot = FindLocalSeatSlot();

  for (i = 8; i-- != 0;) {
    if (gPlayerData[i].dwStormID == nPlayer) {
      dwSlot = i;
      ClearSaveSlot(static_cast<BYTE>(i));
      gPlayerData[i].nType = 6;
    }
  }

  if (dwSlot >= 8 || gPlayerData[dwSlot].nType != 6 ||
      (gpLoadGameFile != NULL &&
       !sub_471A40(static_cast<BYTE>(dwSlot), static_cast<BYTE>(dwSaveRace), 0))) {
    packet_kick(nPlayer, 2);
    return;
  }

  gPlayerData[dwSlot].dwPlayerID = dwSlot;
  gPlayerData[dwSlot].dwStormID = nPlayer;
  gPlayerData[dwSlot].nType = 2;
  net_get_player_name(nPlayer, gPlayerData[dwSlot].szName, 0x19, sgszNetChatCmdsCpp, 0x249);
  if (gpLoadGameFile != NULL)
    gPlayerData[dwSlot].nRace = byte_5264A0[dwSaveSlot];
  net_slot_init(g_LocalStormID);
  sub_451790();

  wFlags = pVer->wArg07 & 0xFF03;
  if (pVer->bSpawned)
    wFlags |= 0x100;
  net_player_set_data(static_cast<BYTE>(nPlayer), wFlags, pVer->wArg09, pVer->wArg0B);

  if (nLocal == 0) {
    for (i = 8; i-- != 0;)
      create_net_cmd_modify_slot(&gPlayerData[i]);
    for (i = 8; i != 0;) {
      i--;
      create_net_cmd_version_verify(i);
    }
    packet_request_lobby(nPlayer);
  } else {
    sgbChatState_g = 4;
  }

  if (dword_672960 != NULL) {
    sub_4409B0(dword_672960, static_cast<BYTE>(nPlayer));
    sub_440BD0(dword_672960, 1 << nPlayer);
  }
  sub_476EF0(CountDefaultHumanSlots());
}
