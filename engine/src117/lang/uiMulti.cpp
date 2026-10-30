// uiMulti.cpp -- the multiplayer lobby glue: the host-side force, team and race
// change emitters, the ban and kick paths, the game-start handshake, and the
// lobby's init-data handler. Bodies are in oracle VA order. Every callee lives
// in another TU, so none is inlined.

#include "oc_data.h"  // Hyperion: OC_VA
#include "uiMulti.h"          // the 18 rows' declarations
#include "charactr.h"         // DeleteCharacterFile
#include "net_cmds.h"         // PlayerData, the 36-byte slot record
#include "Storm.h"            // SStrCopy, SNetGetGameInfo, SNetSetGameMode
#include "net_strt.h"       // end_countdown
#include "net_ban.h"        // AddBannedPlayer
#include "net_error.h"        // err_popup, net_error_popup
#include "net_chat_cmds.h"    // the create_net_cmd_* emitters, sub_470910
#include "Teams.h"            // IsComputerTeam, IsTeamGame
#include "forces.h"           // IsComputerForce
#include "glues.h"            // glue_fade_out, glues_reset_prev_pal
#include "gluBN.h"            // glubn_init, glubn_free
#include "saveload.h"         // DeleteSave
#include "net_player.h"       // leave_game
#include "gametype.h"        // GameHeader, the 141-byte lobby object
#include "gluChatSlot.h"     // sub_4523A0
#include "gluChat.h"         // ConfirmKick, sub_4517A0
#include "net_chrm.h"        // packet_kick
#include "fd_sessn.h"       // IsSessionComplete
#include "netdesc.h"         // Txt2GameData, gamedesc_t

// After the headers above: its plain form declares each global with its type,
// so every such type must already be declared.
#include "../gen/globals117.h"

// Callees in other TUs that no header of ours declares.

// 0x475610 (316 B), net_glue.cpp. sub_4BAF80 is a bare tail jump to it, so the
// two share a signature.
BOOL __fastcall net_glue_OnInitData(intptr_t nArg0);

// 0x474EF0 (5 B), net_glue.cpp, a tail jump to create_net_cmd_change_race. CL
// and DL are the current and the new race, passed through from sub_4BAEA0's
// caller; sub_4BAEA0 drops EAX.
void __fastcall sub_474EF0(BYTE bRaceCur, BYTE bRaceNew);

// sub_4523A0 is declared in gluChatSlot.h, and sub_4517A0, ConfirmKick and
// updateChrmCountdown in gluChat.h.

// 0x4757E0 (559 B), net_glue.cpp. ECX is the provider 4CC; both callers read
// the full EAX.
BOOL __fastcall NetInit(int nProvider);

// strcat, declared here as CImage.cpp and CSprite.cpp declare memset. /Oi
// expands it inline in sub_4BB0E0.
extern "C" char *__cdecl strcat(char *pszDest, const char *pszSrc);

// The globals this TU reaches, dual-spelled, one #ifndef per name.
// gPlayerData, g_LocalNationID, gwGameMode, glGluesMode and gPlayback come from
// gen/globals117.h.

// 0x672960 -- the session StartNetGame hands IsSessionComplete.
#ifndef OC_OWN_dword_672960
#define OC_OWN_dword_672960
#ifdef OC_DLL_MODE
#define dword_672960 (*reinterpret_cast<TFD_SESSION **>(OC_VA(0x672960u)))
#else
extern TFD_SESSION *dword_672960;
#endif
#endif

// 0x672964 -- a DWORD StartNetGame sets to 1.
#ifndef OC_OWN_dword_672964
#define OC_OWN_dword_672964
#ifdef OC_DLL_MODE
#define dword_672964 (*reinterpret_cast<DWORD *>(OC_VA(0x672964u)))
#else
extern DWORD dword_672964;
#endif
#endif

// 0x6728D0 gGameHeader, declared in the TU and not registered, as gametype.cpp
// declares it (gametype.h says why). Its 141 bytes are a DWORD, the game name
// (char[0x18]) at +0x04, the GameData below at +0x1C and snet_initdata_cb's
// 32-byte block at +0x6D.
#ifndef OC_OWN_gGameHeader
#define OC_OWN_gGameHeader
#ifdef OC_DLL_MODE
#define gGameHeader (*reinterpret_cast<GameHeader *>(OC_VA(0x6728D0u)))
#else
extern GameHeader gGameHeader;
#endif
#endif

// GameData, Txt2GameData's output record, embedded at gGameHeader+0x1C: 0x51
// bytes, packed, as uiMulti_OnInitData copies it. +0x0B is gametype.h's
// bMapAuthentication at GameHeader+0x27, which pins the base; +0x16 is the
// playback kind both init-data handlers copy to gPlayback. Nothing reads or
// writes +0x17.
#pragma pack(push, 1)
struct GameData {
  /*0x00*/ DWORD dwSeed;
  /*0x04*/ WORD  wUnk04;
  /*0x06*/ WORD  wUnk06;
  /*0x08*/ BYTE  bUnk08;
  /*0x09*/ BYTE  bUnk09;
  /*0x0A*/ BYTE  bUnk0A;
  /*0x0B*/ BYTE  bMapAuthentication;
  /*0x0C*/ BYTE  bUnk0C;
  /*0x0D*/ BYTE  bUnk0D;
  /*0x0E*/ WORD  wUnk0E;
  /*0x10*/ DWORD dwUnk10;
  /*0x14*/ WORD  wUnk14;
  /*0x16*/ BYTE  bPlaybackKind;
  /*0x17*/ BYTE  _underived_17;
  /*0x18*/ char  szCreatorName[0x19];
  /*0x31*/ char  szMapName[0x20];
};
#pragma pack(pop)

static_assert(sizeof(GameData) == 0x51, "five movdqu (80) + one byte store");
static_assert(offsetof(GameData, dwSeed) == 0x00, "0x6728EC");
static_assert(offsetof(GameData, wUnk04) == 0x04, "0x6728F0");
static_assert(offsetof(GameData, wUnk06) == 0x06, "0x6728F2");
static_assert(offsetof(GameData, bUnk08) == 0x08, "Txt2GameData [edi+8]");
static_assert(offsetof(GameData, bUnk09) == 0x09, "Txt2GameData [edi+9]");
static_assert(offsetof(GameData, bUnk0A) == 0x0A, "Txt2GameData [edi+0Ah]");
static_assert(offsetof(GameData, bMapAuthentication) == 0x0B, "0x6728F7");
static_assert(offsetof(GameData, bUnk0C) == 0x0C, "0x6728F8");
static_assert(offsetof(GameData, bUnk0D) == 0x0D, "0x6728F9");
static_assert(offsetof(GameData, wUnk0E) == 0x0E, "0x6728FA");
static_assert(offsetof(GameData, dwUnk10) == 0x10, "0x6728FC");
static_assert(offsetof(GameData, wUnk14) == 0x14, "0x672900");
static_assert(offsetof(GameData, bPlaybackKind) == 0x16, "0x672902");
static_assert(offsetof(GameData, szCreatorName) == 0x18, "0x672904");
static_assert(offsetof(GameData, szMapName) == 0x31, "0x67291D");

// The two members gametype.h's GameHeader models inside `_unmapped_00`, reached
// through the object rather than as globals of their own.
#define gGameHeaderName \
  (reinterpret_cast<char *>(&gGameHeader) + 0x04)
#define gGameData \
  (*reinterpret_cast<GameData *>(reinterpret_cast<BYTE *>(&gGameHeader) + 0x1C))

// 0x527120 -- the map name, a char[0x20].
#ifndef OC_OWN_byte_527120
#define OC_OWN_byte_527120
#ifdef OC_DLL_MODE
#define byte_527120 (reinterpret_cast<char *>(OC_VA(0x527120u)))
#else
extern char byte_527120[0x20];
#endif
#endif

// The bodies, in oracle VA order.

// 0x4BAD10, 356 B
// The lobby's init-data handler: pulls the game name and the statstring out of
// Storm, decodes the statstring into the 81-byte record in gGameHeader, and
// decides whether the lobby may proceed. The 0xFFFF guard can never fire, since
// the field read is one byte (snet_initdata_cb stores 0xFF); 1.17 has it, and
// net_glue_OnInitData the same. 0x188, 0x18F and 0x19A are 1.17's line numbers.
BOOL uiMulti_OnInitData() {
  GameData gd;
  char szStatString[0x80];
  DWORD dwGameMode;
  int nBytesRead;

  if (!SNetGetGameInfo(1, gGameHeaderName, 0x18, &nBytesRead)) {
    net_error_popup(0x51, "uiMulti.cpp", 0x188, 0);
    return FALSE;
  }

  if (!SNetGetGameInfo(3, szStatString, 0x80, &nBytesRead)) {
    net_error_popup(0x51, "uiMulti.cpp", 0x18F, 0);
    return FALSE;
  }

  if (gGameHeader.bGameType == 0xFFFF) {
    leave_game(3);
    err_popup(0x5D, (intptr_t)"OnInitData()", (intptr_t)"uiMulti.cpp", 0x19A, 0);
    return FALSE;
  }

  if (!Txt2GameData(szStatString, reinterpret_cast<gamedesc_t *>(&gd)))
    return FALSE;

  gGameData = gd;
  gPlayback = gd.bPlaybackKind;
  SStrCopy(byte_527120, gGameData.szMapName, 0x20);

  if (gPlayback == 0) {
    if (!SNetGetGameInfo(4, &dwGameMode, 4, &nBytesRead))
      return FALSE;

    if (dwGameMode & 0x80)
      return FALSE;
  }

  return TRUE;
}

// 0x4BAE80, 30 B
// The force change, refused for a computer force. The parameter is a BYTE, as
// both callees take it.
BOOL sub_4BAE80(BYTE bForce) {
  if (IsComputerForce(bForce))
    return FALSE;

  create_net_cmd_change_force(bForce);
  return TRUE;
}

// 0x4BAEA0, 11 B
// Passes the race change on, then returns 1.
BOOL sub_4BAEA0(BYTE bRaceCur, BYTE bRaceNew) {
  sub_474EF0(bRaceCur, bRaceNew);
  return TRUE;
}

// 0x4BAEB0, 30 B
// sub_4BAE80's shape for the team: refused for a computer team.
BOOL sub_4BAEB0(BYTE bTeam) {
  if (IsComputerTeam(bTeam))
    return FALSE;

  create_net_cmd_change_team(bTeam);
  return TRUE;
}

// 0x4BAED0, 137 B
// The host's start-game path. A failed SNetSetGameMode shows its popup and the
// game still starts, as in 1.17. 0x157 and 0x15D are 1.17's line numbers.
BOOL StartNetGame() {
  DWORD dwMode;
  DWORD cbBytes;

  if (dword_672960 != 0 &&
      !IsSessionComplete(dword_672960))
    return FALSE;

  if (!SNetGetGameInfo(4, &dwMode, 4, (int *)&cbBytes)) {
    net_error_popup(0x51, "uiMulti.cpp", 0x157, 1);
    return FALSE;
  }

  dwMode |= 8;
  if (!SNetSetGameMode(dwMode, 0))
    net_error_popup(0x51, "uiMulti.cpp", 0x15D, 1);

  dword_672964 = 1;
  create_net_cmd_start_game();
  return TRUE;
}

// 0x4BAF60, 12 B
// Sends sub_470910 the constant 1, then ends in a tail jump to end_countdown.
BOOL sub_4BAF60() {
  sub_470910(1);
  return end_countdown();
}

// 0x4BAF70, 16 B
// Deletes the save and then the character file of that name; nArg0 is the
// name, a const char *.
int remove_character(intptr_t nArg0) {
  DeleteSave((const char *)nArg0);
  return DeleteCharacterFile((const char *)nArg0);
}

// 0x4BAF80, 5 B
// A bare tail jump to net_glue_OnInitData.
BOOL sub_4BAF80(intptr_t nArg0) {
  return net_glue_OnInitData(nArg0);
}

// 0x4BAF90, 11 B
// sub_4BAEA0's shape with create_net_cmd_make_public.
BOOL sub_4BAF90() {
  create_net_cmd_make_public();
  return TRUE;
}

// 0x4BAFA0, 56 B
// Starts network provider nArg0. 1.17 gives Battle.net's, 0x424E4554, a path of
// its own.
//
// Hyperion: Battle.net's screens loaded their art around its NetInit
// (glubn_init, glubn_free). They are gone, and the
// port starts no provider but its own, so every provider takes the plain path.
BOOL Begin_BNET(int nArg0) {
  return NetInit(nArg0);
}

// 0x4BB000, 119 B
// Bans the player in slot nArg0 once the host confirms: the name goes on the
// ban list, the player is kicked and the slot opened. A human only, and never
// the local player.
int BanPlayer(int nArg0) {
  char szName[25];
  DWORD dwStormID;

  if (gPlayerData[nArg0].nType != 2)
    return FALSE;

  if (g_LocalNationID == nArg0)
    return FALSE;

  dwStormID = gPlayerData[nArg0].dwStormID;
  SStrCopy(szName, gPlayerData[nArg0].szName, sizeof(szName));

  if (!ConfirmKick(szName))
    return FALSE;

  AddBannedPlayer(szName);
  packet_kick(dwStormID, 1);
  create_net_cmd_change_slot((BYTE)nArg0, 1);
  return TRUE;
}

// 0x4BB080, 88 B
// Closes slot nArg0, kicking a human in it once the host confirms. Never the
// local player's slot.
int sub_4BB080(int nArg0) {
  if (gPlayerData[nArg0].nType == 8)
    return FALSE;

  if (g_LocalNationID == nArg0)
    return FALSE;

  if (gPlayerData[nArg0].nType == 2) {
    DWORD dwStormID = gPlayerData[nArg0].dwStormID;

    if (!ConfirmKick(gPlayerData[nArg0].szName))
      return FALSE;

    packet_kick(dwStormID, 0);
  }

  create_net_cmd_change_slot((BYTE)nArg0, 2);
  return TRUE;
}

// 0x4BB0E0, 421 B
// Kicks every human on the slot's team (in a team game) or the one in the slot,
// once the host confirms, and gives the slot to a computer. IsTeamGame is
// called three times, as in 1.17; both strcats are /Oi expansions.
BOOL sub_4BB0E0(int nArg0) {
  BYTE bTeam;

  if (gPlayerData[nArg0].nType == 5)
    return FALSE;

  bTeam = gPlayerData[nArg0].nTeam;

  if (IsTeamGame() && gPlayerData[g_LocalNationID].nTeam == bTeam)
    return FALSE;

  if (!IsTeamGame() && g_LocalNationID == nArg0)
    return FALSE;

  if (IsTeamGame()) {
    // The declarations sit in this block so that 1.17's initialiser stays after
    // the first guard.
    char szBuf[224] = "";
    DWORD aStormIDs[8];
    int i;

    for (i = 0; i < 8; i++) {
      aStormIDs[i] = 8;

      if (gPlayerData[i].nTeam == bTeam && gPlayerData[i].nType == 2) {
        if (szBuf[0] != 0)
          strcat(szBuf, "\n\t");

        strcat(szBuf, gPlayerData[i].szName);
        aStormIDs[i] = gPlayerData[i].dwStormID;
      }
    }

    if (szBuf[0] != 0 && !ConfirmKick(szBuf))
      return FALSE;

    for (i = 0; i < 8; i++) {
      if (aStormIDs[i] != 8)
        packet_kick(aStormIDs[i], 0);
    }
  } else if (gPlayerData[nArg0].nType == 2) {
    DWORD dwStormID = gPlayerData[nArg0].dwStormID;

    if (!ConfirmKick(gPlayerData[nArg0].szName))
      return FALSE;

    packet_kick(dwStormID, 0);
  }

  create_net_cmd_change_slot((BYTE)nArg0, 0);
  return TRUE;
}

// 0x4BB290, 88 B
// sub_4BB080's shape: opens slot nArg0, which nType 6 means is already open.
int sub_4BB290(int nArg0) {
  if (gPlayerData[nArg0].nType == 6)
    return FALSE;

  if (g_LocalNationID == nArg0)
    return FALSE;

  if (gPlayerData[nArg0].nType == 2) {
    DWORD dwStormID = gPlayerData[nArg0].dwStormID;

    if (!ConfirmKick(gPlayerData[nArg0].szName))
      return FALSE;

    packet_kick(dwStormID, 0);
  }

  create_net_cmd_change_slot((BYTE)nArg0, 1);
  return TRUE;
}

// 0x4BB2F0, 28 B
// Swaps two slots, refused for an index of 8 or more (a signed test) or for one
// slot twice.
BOOL sub_4BB2F0(int nArg0, intptr_t nArg1) {
  if (nArg0 >= 8 || nArg1 >= 8 || nArg0 == nArg1)
    return FALSE;

  create_net_cmd_swap_slots((BYTE)nArg0, (BYTE)nArg1);
  return TRUE;
}

// 0x4BB310, 5 B
// A bare tail jump to sub_4523A0.
void sub_4BB310(int nSlot, BYTE bProgress) {
  sub_4523A0(nSlot, bProgress);
}

// 0x4BB320, 24 B
// Calls sub_4517A0 by a conditional tail jump, in the chat room only
// (gwGameMode 4, glGluesMode 3).
void sub_4BB320() {
  if (gwGameMode == 4 && glGluesMode == 3)
    sub_4517A0();
}

// 0x4BB340, 24 B
// sub_4BB320's shape with gluChat.cpp's countdown updater.
void update_countdown(BYTE bSecs) {
  if (gwGameMode == 4 && glGluesMode == 3)
    updateChrmCountdown(bSecs);
}
