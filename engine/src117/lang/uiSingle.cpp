// uiSingle.cpp -- the single-player campaign and custom-game setup screens.
// Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/uiSingle.h"
#include "../h/map_load.h"   // PreloadCampaignMap
#include "../h/gluMap.h"       // CreateScenarioGame
#include "../h/credits.h"    // show_epilog_credits
#include "../h/gametype.h"   // gametype_load_ums, TGAMEPARAMS
#include "../h/gluCmpgn.h"   // ReadCharacterFile, sub_4BB5B0
#include "../h/charactr.h"   // WriteCharacterFile, CreateNewCharacterFile
#include "../h/gluGameMode.h" // play_cinematic_flic
#include "../h/gluPopup.h"   // do_hist_popup
#include "../h/net_cmds.h"   // PlayerData, error_get_str, voice_squelch
#include "../h/net_error.h"  // err_popup
#include "../h/net_glue.h"   // ResetGameCreator, CreateGame_SinglePlayer
#include "../h/net_slot.h"   // FindLocalSeatSlot
#include "../h/playertables.h" // Hyperion: PlayersSeatNote
#include "../h/opt_dlgs.h"   // _stricmp
#include "../h/saveload.h"   // sub_499850
#include "../h/Storm.h"      // SStrCopy, SStrPack, SMemFree
#include "../gen/globals117.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// Callees no header of ours declares.

// 0x46B0B0. Spelled as in maphdr.cpp and net_player.cpp.
void *__fastcall GetCurrentMap(int nWhich);

// Storm string helpers, extern "C" so the names do not mangle.
extern "C" {
char *__stdcall SStrChrR(const char *pszStr, char chFind);
int   __stdcall SStrCmpI(const char *pszA, const char *pszB, size_t cchMax);
}

// 0x4BE950 is a one-byte ret several empty functions share, and sub_4BB6C0
// calls the nullary one. net_cmds.h declares the two-argument spelling of the
// same address.
void __fastcall voice_squelch();

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x50E0B8 sCampaigns_g and 0x50E0C4 sExpCampaigns_g -- three races each, each
// element pointing into a .rdata run of rows that ends on wMission == 0.
#ifndef OC_SCAMPAIGNS_G
#define OC_SCAMPAIGNS_G
#ifdef OC_DLL_MODE
#define sCampaigns_g (reinterpret_cast<campaign_t **>(OC_VA(0x50E0B8u)))
#else
extern campaign_t *sCampaigns_g[3] OC_UNALIGNED;
#endif
#endif

#ifndef OC_SEXPCAMPAIGNS_G
#define OC_SEXPCAMPAIGNS_G
#ifdef OC_DLL_MODE
#define sExpCampaigns_g (reinterpret_cast<campaign_t **>(OC_VA(0x50E0C4u)))
#else
extern campaign_t *sExpCampaigns_g[3];
#endif
#endif

// 0x50E0D0 sgSavedFlic -- the flic to play next, a DWORD. Its initial value is
// 0x1C, the default play_next_flic restores.
#ifndef OC_SGSAVEDFLIC
#define OC_SGSAVEDFLIC
#ifdef OC_DLL_MODE
#define sgSavedFlic (*reinterpret_cast<DWORD *>(OC_VA(0x50E0D0u)))
#else
extern DWORD sgSavedFlic;
#endif
#endif

// 0x6CA718 sgpSelectedCmpgn_g -- the selected campaign row.
#ifndef OC_SGPSELECTEDCMPGN_G
#define OC_SGPSELECTEDCMPGN_G
#ifdef OC_DLL_MODE
#define sgpSelectedCmpgn_g (*reinterpret_cast<campaign_t **>(OC_VA(0x6CA718u)))
#else
extern campaign_t *sgpSelectedCmpgn_g;
#endif
#endif

// 0x6CA71C -- a DWORD flag seat_local_player sets and sub_4BB5B0 clears.
#ifndef OC_DWORD_6CA71C
#define OC_DWORD_6CA71C
#ifdef OC_DLL_MODE
#define dword_6CA71C (*reinterpret_cast<DWORD *>(OC_VA(0x6CA71Cu)))
#else
extern DWORD dword_6CA71C;
#endif
#endif

// 0x5264C3 -- a BYTE flag, the copy of dword_6CA71C that sub_4BBB40 restores.
#ifndef OC_BYTE_5264C3_G
#define OC_BYTE_5264C3_G
#ifdef OC_DLL_MODE
#define byte_5264C3_g (*reinterpret_cast<BYTE *>(OC_VA(0x5264C3u)))
#else
extern BYTE byte_5264C3_g;
#endif
#endif

// 0x526524 -- the mission number, spelled as maphdr.cpp and gluBrfAct.cpp
// spell it.
#ifndef OC_WORD_526524_G
#define OC_WORD_526524_G
#ifdef OC_DLL_MODE
#define word_526524_g (*reinterpret_cast<WORD *>(OC_VA(0x526524u)))
#else
extern WORD word_526524_g;
#endif
#endif

// The same object read four bytes wide, as 1.17 reads it wherever the value
// goes to sub_4BBAE0's EDX argument.
#define dword_526524_g (*reinterpret_cast<const DWORD *>(&word_526524_g))

// 0x526526 -- the map file name of the mission to start, char[32].
#ifndef OC_BYTE_526526
#define OC_BYTE_526526
#ifdef OC_DLL_MODE
#define byte_526526 (reinterpret_cast<char *>(OC_VA(0x526526u)))
#else
extern char byte_526526[32] OC_UNALIGNED;
#endif
#endif

// 0x527120 -- 0x20 bytes, spelled as uiMulti.cpp spells it.
#ifndef OC_OWN_byte_527120
#define OC_OWN_byte_527120
#ifdef OC_DLL_MODE
#define byte_527120 (reinterpret_cast<char *>(OC_VA(0x527120u)))
#else
extern char byte_527120[0x20];
#endif
#endif

// 0x536720 -- the expansion gate, a BYTE, spelled as gametype.cpp and
// CUnitUpgrade.cpp spell it.
#ifndef OC_GBISEXPANSION
#define OC_GBISEXPANSION
#ifdef OC_DLL_MODE
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbIsExpansion;
#endif
#endif

// 0x51ABB8 -- the map file table, spelled as gluBrfAct.cpp spells it.
#ifndef OC_GSZMAPFILETBL
#define OC_GSZMAPFILETBL
#ifdef OC_DLL_MODE
#define gszMapFileTbl (reinterpret_cast<char **>(OC_VA(0x51ABB8u)))
#else
extern char *gszMapFileTbl[65];  // Hyperion: mapdata.dat's 65 rows
#endif
#endif

// The bodies, in oracle VA order.

// The static twin of sub_4BBAE0, defined below.
static campaign_t *sub_4BBAE0_local(campaign_t **ppTbl, WORD wMission);

// Starts the mission the campaign screen left in byte_526526, or parks
// glGluesMode on the screen that explains why it will not. The race switch
// breaks to one `return 1` and sends its default to the failure tail.
BOOL sub_4BB360()
{
  char szPath[0x104];
  TGAMEPARAMS *pParams;
  const char *pszLeaf;
  int nTemplate;
  BYTE nRace;

  if (byte_526526[0] == 0) {
    glGluesMode = gbIsExpansion ? 0x16 : 6;
    return 1;
  }

  pParams = gametype_load_ums();
  if (pParams != NULL) {
    nTemplate = ((pParams->wType << 8 | pParams->bSub) << 8) | pParams->bCategory;
    SMemFree(pParams, "uiSingle.cpp", 0x13F, 0);

    SStrCopy(szPath, static_cast<const char *>(GetCurrentMap(1)), 0x104);
    // The const overload calls the CRT's strrchr directly, as 1.17 does.
    pszLeaf = strrchr(const_cast<const char *>(szPath), '\\');
    if (pszLeaf != NULL) {
      const_cast<char *>(pszLeaf)[1] = 0;
      SStrPack(szPath, byte_526526, 0x104);
      if (!CreateScenarioGame(szPath, gLocalPlayerName, 0, nTemplate,
                      static_cast<BYTE>(gOptions))) {
        nRace = gPlayerData[g_LocalNationID].nRace;
        switch (nRace) {
        case 0:
          glGluesMode = 8;
          break;
        case 1:
          glGluesMode = 7;
          break;
        case 2:
          glGluesMode = 9;
          break;
        default:
          goto fail;
        }
        return 1;
      }
    }
  }

fail:
  err_popup(0x6A, 0, 0, 0, 0);
  glGluesMode = gbIsExpansion ? 0x16 : 6;
  return 0;
}

// Demotes every slot still marked 5 (a computer) to 6 (open) before a
// single-player melee seats its computers (SinglePlayerMeleeInitGame).
void reopen_computer_slots()
{
  BYTE *pType = &gPlayerData[8].nType;

  do {
    pType -= sizeof(PlayerData);
    if (*pType == 5)
      *pType = 6;
  } while (pType != &gPlayerData[0].nType);
  // Hyperion: and on a Hyperion map the lobby slots past 7
  for (int i = PLAYER_LOBBY_117; i < PlayerLobbySlots(); i++)
    if (i != PLAYER_NEUTRAL && gPlayerData[i].nType == 5)
      gPlayerData[i].nType = 6;
}

// Claims the highest slot still marked 6 for a computer at bRace, once per
// computer in SinglePlayerMeleeInitGame; the local player takes the lowest
// (FindLocalSeatSlot).
BOOL seat_computer_player(BYTE bRace)
{
  int i = PlayerLobbySlots();  // Hyperion: 8 in 1.17; neutral's is never 6

  do {
    if (i-- == 0)
      return 0;
  } while (gPlayerData[i].nType != 6);
  gPlayerData[i].nType = 5;
  gPlayerData[i].nRace = bRace;
  PlayersSeatNote(i);  // Hyperion: where this computer sits
  return 1;
}

// Sets the local player's race for a single-player game.
BOOL set_single_race(BYTE bRace)
{
  gPlayerData[g_LocalNationID].nRace = bRace;
  return 1;
}

// Seats the local player for a campaign game. The tail is seat_custom_player's
// body, written out because 1.17 has no call.
BOOL seat_local_player()
{
  DWORD nSlot;

  dword_6CA71C = 1;
  byte_5264C3_g = 1;
  nSlot = gpLoadGameFile ? sub_499850() : FindLocalSeatSlot();
  g_LocalNationID = nSlot;
  g_LocalHumanID = nSlot;
  if (nSlot >= 8)
    return 0;
  gPlayerData[nSlot].nType = 2;
  SStrCopy(gPlayerData[nSlot].szName, gLocalPlayerName, 25);
  return 1;
}

// Opens the campaign screen for nRace, records any progress the player just
// made, and either queues the row's flic or starts the mission.
int sub_4BB5B0(int nRace)
{
  BYTE rec[0x70];
  campaign_t *pTbl;
  DWORD *pnBest;

  byte_5264C3_g = 0;
  dword_6CA71C = 0;

  if (!ReadCharacterFile(gLocalPlayerName, rec)
      && (_stricmp(gLocalPlayerName, error_get_str(0x48)) != 0
          || !CreateNewCharacterFile(gLocalPlayerName, rec)))
    err_popup(0x58, reinterpret_cast<intptr_t>(gLocalPlayerName), 0, 0, 0);

  pTbl = sCampaigns_g[nRace];
  pnBest = reinterpret_cast<DWORD *>(rec + 0x28) + nRace;
  if (gbIsExpansion) {
    pTbl = sExpCampaigns_g[nRace];
    pnBest = reinterpret_cast<DWORD *>(rec + 0x34) + nRace;
  }

  sgpSelectedCmpgn_g = reinterpret_cast<campaign_t *>(
      do_hist_popup(reinterpret_cast<intptr_t>(pTbl), static_cast<int>(*pnBest)));
  if (sgpSelectedCmpgn_g == NULL)
    return 0;

  if (*pnBest < sgpSelectedCmpgn_g->wMission) {
    *pnBest = sgpSelectedCmpgn_g->wMission;
    WriteCharacterFile(rec);
  }
  if (sgpSelectedCmpgn_g->wFlic != 0) {
    sgSavedFlic = sgpSelectedCmpgn_g->wFlic;
    word_526524_g = sgpSelectedCmpgn_g->wMission;
    gwGameMode = 10;
    byte_526526[0] = 0;
    return 1;
  }
  return sub_4BB6C0(sgpSelectedCmpgn_g->wMission);
}

// Builds the single-player game header for wMission and hands it to the game
// creator. PreloadCampaignMap fills the 0x20-byte buffer with the map path.
BOOL sub_4BB6C0(WORD wMission)
{
  GameHeader hdr;
  char szMap[0x20];
  BYTE *pHdr = reinterpret_cast<BYTE *>(&hdr);
  TGAMEPARAMS *pParams;
  BYTE bOpt;
  int nRes;

  word_526524_g = wMission;
  if (!PreloadCampaignMap(wMission, szMap))
    return 0;

  memset(&hdr, 0, sizeof(hdr));
  SStrCopy(reinterpret_cast<char *>(pHdr + 4), gLocalPlayerName, 0x18);
  SStrCopy(reinterpret_cast<char *>(pHdr + 0x4D), byte_527120, 0x20);
  bOpt = static_cast<BYTE>(gOptions);
  pHdr[0x24] = 1;
  pHdr[0x25] = 1;
  pHdr[0x26] = bOpt;

  pParams = gametype_load_ums();
  if (pParams == NULL)
    return 0;
  *reinterpret_cast<TGAMEPARAMS *>(pHdr + 0x6D) = *pParams;
  SMemFree(pParams, "uiSingle.cpp", 0x10E, 0);

  if (!seat_custom_player())
    return 0;
  ResetGameCreator();
  nRes = CreateGame_SinglePlayer(reinterpret_cast<intptr_t>(&hdr));
  voice_squelch();
  return nRes != 0;
}

// The campaign screen's step function: advances to the next row, plays its flic
// or starts its mission, and sets the glue the caller should show next.
int sub_4BB790(int bAdvance)
{
  campaign_t *pRow;
  WORD wMission;
  BYTE nRace;

  gwGameMode = 4;
  if (dword_6CA71C) {
    sub_4BB360();
    return 1;
  }
  if (bAdvance == 0) {
    gwGameMode = 5;
    return 1;
  }

  show_epilog_credits();
  pRow = sgpSelectedCmpgn_g;
  if (pRow == NULL || pRow->wMission != word_526524_g) {
    pRow = sub_4BBAE0_local(sCampaigns_g, static_cast<WORD>(dword_526524_g));
    sgpSelectedCmpgn_g = pRow;
    if (pRow == NULL) {
      pRow = sub_4BBAE0_local(sExpCampaigns_g, static_cast<WORD>(dword_526524_g));
      sgpSelectedCmpgn_g = pRow;
      if (pRow == NULL)
        return 0;
    }
  }
  if (pRow->wMission == 0)
    return 0;

  if (byte_526526[0] != 0) {
    pRow = sub_4BBA20(byte_526526);
    byte_526526[0] = 0;
  } else {
    pRow = pRow + 1;
  }
  sgpSelectedCmpgn_g = pRow;
  sub_4BBC20(pRow);

  pRow = sgpSelectedCmpgn_g;
  wMission = pRow->wMission;
  if (wMission == 0) {
    if (gbIsExpansion) {
      if (pRow->nRace == 0)
        goto ep;
      glGluesMode = 0x16;
      sgpSelectedCmpgn_g = NULL;
      return 1;
    }
    if (pRow->nRace == 2) {
ep:
      sgpSelectedCmpgn_g = NULL;
      gwGameMode = 9;
      return 1;
    }
    glGluesMode = 6;
    sgpSelectedCmpgn_g = NULL;
    return 1;
  }

  if (pRow->wFlic != 0) {
    sgSavedFlic = pRow->wFlic;
    gwGameMode = 10;
    word_526524_g = wMission;
    return 1;
  }
  if (!sub_4BB6C0(wMission))
    return 0;
  nRace = sgpSelectedCmpgn_g->nRace;
  if (nRace == 0)
    glGluesMode = 8;
  else
    glGluesMode = nRace == 1 ? 7 : 9;
  return 1;
}

// Seats the local player for a custom game.
BOOL seat_custom_player()
{
  DWORD nSlot = gpLoadGameFile ? sub_499850() : FindLocalSeatSlot();

  g_LocalNationID = nSlot;
  g_LocalHumanID = nSlot;
  if (nSlot >= 8)
    return 0;
  gPlayerData[nSlot].nType = 2;
  SStrCopy(gPlayerData[nSlot].szName, gLocalPlayerName, 25);
  return 1;
}

// Records the score screen's mission in the character file, unless this is a
// multiplayer or loaded game.
void sub_4BB970()
{
  campaign_t *pRow;

  if (gbMultiPlayerMode || dword_6CA71C)
    return;

  pRow = sgpSelectedCmpgn_g;
  if (pRow == NULL || pRow->wMission != word_526524_g) {
    pRow = sub_4BBAE0_local(sCampaigns_g, static_cast<WORD>(dword_526524_g));
    sgpSelectedCmpgn_g = pRow;
    if (pRow == NULL) {
      pRow = sub_4BBAE0_local(sExpCampaigns_g, static_cast<WORD>(dword_526524_g));
      sgpSelectedCmpgn_g = pRow;
      if (pRow == NULL)
        return;
    }
  }
  if (pRow->wMission == 0)
    return;
  sub_4BBC20(pRow + 1);
}

// Advances *pnIndex to the next map file whose leaf name matches pszName over
// cchName characters. noinline: 1.17 calls it from sub_4BBA20, its only caller.
__declspec(noinline) BOOL sub_4BB9D0(const char *pszName, int cchName,
                                     DWORD *pnIndex)
{
  while (*pnIndex < 0x41) {
    const char *pszLeaf = SStrChrR(gszMapFileTbl[*pnIndex], '\\');
    if (pszLeaf != NULL && SStrCmpI(pszLeaf + 1, pszName, cchName) == 0)
      return 1;
    ++*pnIndex;
  }
  return 0;
}

// Resolves a map file name to its campaign row, trying successive mission
// numbers until one of the three tables carries it.
campaign_t *sub_4BBA20(const char *pszFile)
{
  const char *pszDot;
  campaign_t **ppTbl;
  campaign_t *pRow;
  DWORD nMission;
  int cchName;

  pszDot = SStrChrR(pszFile, '.');
  if (pszDot == NULL)
    return sgpSelectedCmpgn_g;

  cchName = static_cast<int>(pszDot - pszFile);
  ppTbl = SStrCmpI(pszDot, ".SCM", 0x7FFFFFFF) == 0 ? sCampaigns_g
                                                    : sExpCampaigns_g;
  nMission = 0;
  while (sub_4BB9D0(pszFile, cchName, &nMission)) {
    pRow = sub_4BBAE0_local(ppTbl, static_cast<WORD>(nMission));
    if (pRow != NULL)
      return pRow;
    nMission++;
  }
  return sgpSelectedCmpgn_g;
}

// Finds the row for wMission in one of the three tables.
campaign_t *sub_4BBAE0(campaign_t **ppTbl, WORD wMission)
{
  for (DWORD i = 0; i < 3; i++) {
    campaign_t *pRow = ppTbl[i];
    while (pRow->wMission != 0) {
      if (pRow->wFlic == 0 && pRow->wMission == wMission)
        return pRow;
      pRow++;
    }
  }
  return NULL;
}

// The internal copy of sub_4BBAE0, the same bytes. Its callers in this TU keep a
// value in a register across the call, which only an internal callee allows.
static campaign_t *sub_4BBAE0_local(campaign_t **ppTbl, WORD wMission)
{
  for (DWORD i = 0; i < 3; i++) {
    campaign_t *pRow = ppTbl[i];
    while (pRow->wMission != 0) {
      if (pRow->wFlic == 0 && pRow->wMission == wMission)
        return pRow;
      pRow++;
    }
  }
  return NULL;
}

// Plays the flic the campaign row parked in sgSavedFlic and restores the
// default.
void play_next_flic()
{
  play_cinematic_flic(sgSavedFlic);
  sgSavedFlic = 0x1C;
}

// Outside a multiplayer game, copies the saved single-player flag into
// dword_6CA71C.
void sub_4BBB40()
{
  if (!gbMultiPlayerMode)
    dword_6CA71C = byte_5264C3_g;
}

// Resolves a mission number, optionally followed by a branch letter, against
// the campaign table for nRace; writes the row's mission to *pdwMission.
int find_campaign(const char *pszText, int nRace, int nFirst, int bExpansion,
                  DWORD *pdwMission)
{
  char *pszEnd;
  campaign_t *pRow;
  int nSkip;
  int nBranch;

  nSkip = static_cast<int>(strtoul(pszText, &pszEnd, 10)) - nFirst;
  if (nSkip < 0)
    return 0;

  pRow = (bExpansion != 0 ? sExpCampaigns_g : sCampaigns_g)[nRace];
  while (pRow->wMission != 0) {
    if (pRow->wFlic == 0 && pRow->bBranch == 0 && nSkip-- == 0) {
      if (pszEnd != NULL && *pszEnd != 0) {
        nBranch = tolower(*pszEnd) - 'a';
        while (nBranch != 0) {
          --nBranch;
          if (pRow->wMission == 0)
            return 0;
          pRow++;
          if (pRow->bBranch == 0)
            return 0;
        }
        if (pRow->wMission == 0)
          return 0;
      }
      *pdwMission = pRow->wMission;
      return 1;
    }
    pRow++;
  }
  return 0;
}

// Merges the row's mission number into the character file's per-race progress.
void sub_4BBC20(campaign_t *pRow)
{
  BYTE rec[0x70];
  DWORD *pnBest;

  if (!ReadCharacterFile(gLocalPlayerName, rec)
      && (_stricmp(gLocalPlayerName, error_get_str(0x48)) != 0
          || !CreateNewCharacterFile(gLocalPlayerName, rec)))
    err_popup(0x58, reinterpret_cast<intptr_t>(gLocalPlayerName), 0, 0, 0);

  pnBest = reinterpret_cast<DWORD *>(rec + 0x28) + pRow->nRace;
  if (gbIsExpansion)
    pnBest = reinterpret_cast<DWORD *>(rec + 0x34) + pRow->nRace;
  if (*pnBest < pRow->wMission) {
    *pnBest = pRow->wMission;
    WriteCharacterFile(rec);
  }
}
