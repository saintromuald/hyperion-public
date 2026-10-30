// net_misc.cpp -- network miscellany: the game-mode queries (IsBNetGame,
// IsGameHost, IsLoss), the player-drop path, the SNet event callbacks and the
// results bookkeeping. The file name is the one 1.17's own net_error_popup
// string at 0x4EF2C4 gives. Functions are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers are for. sub_4771E0 must stay inlinable and
// above IsGameHost, whose body includes it.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>   // memset

// error.h declares error_get_str with a WORD id; sub_477A60 adds to the id in
// 32 bits, so this TU takes an int view and renames the header's away.
#define error_get_str error_get_str_word_view
#include "../h/net_misc.h"
#include "../h/Storm.h"      // the SNet family these bodies import
#include "../h/net_cmds.h"   // IsGameHost, sub_477E00, get_valid_str_len, sub_4771E0
#include "../h/net_error.h"  // net_error_popup
#include "../h/minimap.h"    // get_num_active_players is seated there
#include "../h/net_glue.h"    // NetworkMode, IsGameCreator
#include "../h/net_time.h"    // sub_47A780
#include "../h/net_slot.h"    // CountDefaultHumanSlots
#include "../h/opt_dlgs.h"    // sub_476F60 is seated there
#include "../h/net_ban.h"     // IsPlayerBanned
#include "../h/net_chrm.h"    // packet_kick, packet_forces, packet_lobby_info
#include "../h/gluChat.h"     // on_chrm_kicked
#include "../h/pause.h"       // get_lobby_state
#include "../h/fd_sessn.h"    // set_fd_session_flags
#include "../port/platform/port_paths.h"  // Hyperion: PORT_MAP_FOLDERS, PortMapFolderCount, PortMapFolderPath
#include "../h/gametype.h"    // IsScenarioGame
#include "../h/Teams.h"       // IsTeamGame, GetNumTeamActivePlayers
#include "../h/victory.h"     // victory_disconnect, victory_quit
#include "../h/select.h"      // sub_49C7D0
#include "../h/net_record.h"  // net_record_cmd
#include "../h/net_player.h"  // net_player_get_name, net_player_drop
#include "../h/gds_local.h"   // psnprintf
#include "../h/fd_user.h"     // sub_440F40
#include "../h/net_slot.h"    // ResetSlotFromLobby, CountDefaultHumanSlots
#include "../h/game.h"       // net_end_game, alliance_init
#include "../h/gluBNProfile.h" // league_get_name
#include "../h/League.h"     // league_write_gameresult
#include "../h/fd.h"         // set_fd_callbacks
#include "../h/map_load.h"   // GetCurrentMap
#include "../h/forces.h"     // GetForceAllianceMode, IsForceVisionShared, SetForceAlliance, SetForceVision
#include "../h/gluScore.h"   // sub_461B80
#undef error_get_str
#include "../gen/globals117.h"
#include "../h/playervision.h"  // Hyperion: the slots past 31 (stage D)

// The id as an int, on error.h's C-linkage symbol.
extern "C" const char *error_get_str(int nErrId);

// 0x4BE950, the folded one-byte `ret`, reached with ECX only.
void __fastcall voice_squelch(DWORD dwStormID);

// Spelled exactly as charactr.cpp, gds_log.cpp and gluCreat.cpp spell it,
// extern "C" included: without it the name mangles and the Storm import does
// not resolve.
extern "C" {
// Hyperion: as port_stormfile.cpp defines it
DWORD  __stdcall SFileGetUserDataPath(char *pszBuf, DWORD cchBuf, DWORD dwFlags);
}

// gbVictoryState @0x5349E0, BYTE[8], spelled as victory.cpp spells it.
#ifdef OC_DLL_MODE
#define gbVictoryState (reinterpret_cast<BYTE *>(OC_VA(0x5349E0u)))
#else
extern PlayerVictoryStateTable gbVictoryState;  // Hyperion
#endif

// gdwMaxPacketSize @0x67265C.
#ifdef OC_DLL_MODE
#define gdwMaxPacketSize (*reinterpret_cast<DWORD *>(OC_VA(0x67265Cu)))
#else
extern DWORD gdwMaxPacketSize;
#endif

// Storm ordinal 113 through the thunk at 0x4D010C, spelled exactly as
// src117/lang/net_player.cpp and src117/lang/gluBN.cpp spell it.
extern "C" BOOL __stdcall SNetGetPlayerName(DWORD dwPlayerId, char *pszBuffer,
                                            DWORD dwBufferSize);

// The globals this TU reaches, dual-spelled, each under a bare #ifdef with no
// #ifndef <name> guard: gen/globals117.h spells an array global's dll-mode arm as
// a pointer, and under a guard its spelling would win and collapse sizeof
// (net_misc_init would clear 4 bytes of a 32-byte table). Restating the macro
// after the include keeps the array form.

// The .rdata file string every net_error_popup and IsGameCreator call here
// passes: 0x4EF2C4, "net_misc.cpp".
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgszNetMiscCpp (reinterpret_cast<const char *>(OC_VA(0x4EF2C4u)))
#else
extern const char sgszNetMiscCpp[1];
#endif

// sgdwLeaveReason_g @0x6737F4 -- DWORD[8]. net_misc_init zeroes exactly 32
// bytes here; snet_playerleave_cb and sub_477DA0 both index it at scale 4 over
// slots 0..7, which agrees.
#ifdef OC_DLL_MODE
#define sgdwLeaveReason_g (*reinterpret_cast<DWORD (*)[8]>(OC_VA(0x6737F4u)))
#else
extern DWORD sgdwLeaveReason_g[8] OC_UNALIGNED;
#endif

// byte_5264C1 @0x5264C1 -- a BYTE; get_num_active_players publishes the active
// count here.
#ifdef OC_DLL_MODE
#define byte_5264C1 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C1u)))
#else
extern BYTE byte_5264C1;
#endif

// gbMultiPlayerMode @0x672664 (net_data.cpp), a BYTE.
#ifdef OC_DLL_MODE
#define gbMultiPlayerMode (*reinterpret_cast<BYTE *>(OC_VA(0x672664u)))
#else
extern BYTE gbMultiPlayerMode;
#endif

// gwGameMode @0x6662B8 (game.cpp), a WORD; both readers here are
// `cmp word ptr [0x6662B8], N`.
#ifdef OC_DLL_MODE
#define gwGameMode (*reinterpret_cast<WORD *>(OC_VA(0x6662B8u)))
#else
extern WORD gwGameMode;
#endif

// g_LocalStormID @0x508D58 (net_glue.cpp), a DWORD.
#ifdef OC_DLL_MODE
#define g_LocalStormID (*reinterpret_cast<DWORD *>(OC_VA(0x508D58u)))
#else
extern DWORD g_LocalStormID;
#endif

// g_ActiveNationID @0x508D44 (net_glue.cpp), a DWORD.
#ifdef OC_DLL_MODE
#define g_ActiveNationID (*reinterpret_cast<DWORD *>(OC_VA(0x508D44u)))
#else
extern DWORD g_ActiveNationID;
#endif

// byte_529041 @0x529041 -- a byte at every access: IsLoss compares it against 2,
// clear_game_results clears it and sub_477A60 reads it.
#ifdef OC_DLL_MODE
#define byte_529041 (*reinterpret_cast<BYTE *>(OC_VA(0x529041u)))
#else
extern BYTE byte_529041;
#endif

// byte_529042 @0x529042 -- BYTE[8]: sub_477A60 compares all eight slots
// bytewise and clear_game_results clears exactly those eight with one `movq`. The
// dll-mode arm is the array form, not a pointer, so that the eight-byte memset
// stays one store.
#ifdef OC_DLL_MODE
#define byte_529042 (*reinterpret_cast<BYTE (*)[8]>(OC_VA(0x529042u)))
#else
extern BYTE byte_529042[8];
#endif

// sgbGameResultsWritten_g @0x6737F0 -- clear_results_written stores a full dword
// zero.
#ifdef OC_DLL_MODE
#define sgbGameResultsWritten_g (*reinterpret_cast<DWORD *>(OC_VA(0x6737F0u)))
#else
extern DWORD sgbGameResultsWritten_g;
#endif

// dword_672960 @0x672960 and dword_672964 @0x672964 (net_glue.cpp). The first
// holds the file-dialog session object: sub_474C30 hands its address to
// sub_440B60 and this TU hands its value to set_fd_session_flags (`void *`).
#ifdef OC_DLL_MODE
#define dword_672960 (*reinterpret_cast<TFD_SESSION **>(OC_VA(0x672960u)))
#else
extern TFD_SESSION *dword_672960;
#endif

#ifdef OC_DLL_MODE
#define dword_672964 (*reinterpret_cast<DWORD *>(OC_VA(0x672964u)))
#else
extern DWORD dword_672964;
#endif

// glGluesMode @0x66A274 (glues.cpp), a DWORD; both readers here are
// `cmp dword ptr [0x66A274], 3h`.
#ifdef OC_DLL_MODE
#define glGluesMode (*reinterpret_cast<DWORD *>(OC_VA(0x66A274u)))
#else
extern DWORD glGluesMode;
#endif

// gpLoadGameFile @0x6A5E34, spelled as src117/lang/saveload.cpp spells it.
#ifdef OC_DLL_MODE
#define gpLoadGameFile (*reinterpret_cast<void **>(OC_VA(0x6A5E34u)))
#else
extern void *gpLoadGameFile;
#endif

// The bodies, in oracle VA order.

// 0x476E20 (124 B). Walks at most min(dwLen, dwCap) characters of pszStr and
// returns the number consumed, terminator included, when it finds one, or 0 when
// it runs out of room or meets a character the two flags forbid. The two switches
// list the same 22 control characters, the ones in 1..31 other than 9-13, 18-20
// and 26, so that the case span is [1, 31]; they differ only in which arm returns
// 0. The switch value is a signed char.
int __fastcall get_valid_str_len(const char *pszStr, DWORD dwLen, DWORD dwCap,
                                 int nFlagA, int nFlagB)
{
  const char *p;
  DWORD_PTR dwRoom;
  int nCount;
  char c;

  nCount = 0;
  dwRoom = dwLen < dwCap ? dwLen : dwCap;
  p = pszStr;

  while (dwRoom != 0)
  {
    c = *p;
    --dwRoom;
    ++nCount;
    if (c == 0)
      return nCount;

    if (nFlagB == 0)
    {
      switch (c)
      {
      case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
      case 14: case 15: case 16: case 17:
      case 21: case 22: case 23: case 24: case 25:
      case 27: case 28: case 29: case 30: case 31:
        return 0;
      }
    }

    if (nFlagA == 0 && (BYTE)c < 0x20)
    {
      switch (c)
      {
      case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
      case 14: case 15: case 16: case 17:
      case 21: case 22: case 23: case 24: case 25:
      case 27: case 28: case 29: case 30: case 31:
        break;
      default:
        return 0;
      }
    }

    ++p;
  }

  return 0;
}

// 0x476EF0 (104 B). Reads Storm's game-mode word, flips one bit and writes it
// back. The sense is inverted: a nonzero argument clears bit 1 and a zero
// argument sets it. nBytesRead is declared first so it takes the lower slot.
void sub_476EF0(int nEnable)
{
  int nBytesRead;
  DWORD dwGameMode;

  if (!SNetGetGameInfo(4, &dwGameMode, 4, &nBytesRead))
  {
    net_error_popup(0x51, sgszNetMiscCpp, 0x6E, 1);
    return;
  }

  if (nEnable)
    dwGameMode &= ~2u;
  else
    dwGameMode |= 2;

  if (!SNetSetGameMode(dwGameMode, 0))
    net_error_popup(0x51, sgszNetMiscCpp, 0x78, 1);
}

// 0x476F60 (17 B). `0x78 < x` with the constant on the left, unsigned:
// `cmp ecx, eax / sbb eax, eax / neg eax`.
int sub_476F60()
{
  return (DWORD)sub_47A780() > 0x78;
}

// 0x476F80 (210 B). Reports the game's results to Storm once per game, and to
// the ladder too when a league name is set; a replay reports nothing.
void net_end_game()
{
  int anResults[8];
  char szHeader[0x400];
  char szDetail[0x800];

  if (sgbGameResultsWritten_g != 0 || gPlayback != 0)
    return;

  sub_477270(anResults, NULL);
  if (!sub_461B80(szHeader, sizeof(szHeader), szDetail, sizeof(szDetail))) {
    szDetail[0] = '\0';
    szHeader[0] = '\0';
  }
  SNetReportGameResult(0, 8, anResults, szHeader, szDetail);
  if (league_get_name()) {
    char **ppszNames = static_cast<char **>(SMemAlloc(8 * sizeof(char *), sgszNetMiscCpp, 0x33F, 8));

    SNetGetPlayerNames(ppszNames);
    league_write_gameresult(anResults, const_cast<const char **>(ppszNames), szHeader, szDetail);
    SMemFree(ppszNames, sgszNetMiscCpp, 0x342, 0);
  }
  sgbGameResultsWritten_g = 1;
}

// 0x477060 (47 B). The bound test `cmp ecx, 8h / jnb` is unsigned though the id
// is an int. wrap_map_msg's EDX is the constant -1.
void net_drop_player(int nPlayerId, intptr_t nArg1, DWORD dwFlags)
{
  if ((DWORD)nPlayerId < 8)
  {
    SNetDropPlayer(nPlayerId, dwFlags);
    if (gwGameMode == 3)
      wrap_map_msg((void *)nArg1, -1, 0);
  }
}

// 0x477090 (323 B). Points the file-transfer layer at the download directory
// and the two maps directories; the host also opens a session for its map.
// Fails only when that session cannot be opened. Hyperion: the folders
// given with --maps (port_paths.cpp) follow the two maps directories.
int sub_477090()
{
  char szInstall[0x104];
  char szUser[0x104];
  char szDownload[0x104];
  char szInstallMaps[0x104];
  char szUserMaps[0x104];
  char aszMaps[PORT_MAP_FOLDERS][0x104];
  const char *apszDirs[3 + PORT_MAP_FOLDERS];
  int nDirs;

  SFileGetUserDataPath(szUser, sizeof(szUser), 1);
  SFileGetInstallPath(szInstall, sizeof(szInstall), 1);
  SStrCopy(szDownload, szUser, sizeof(szDownload));
  SStrPack(szDownload, "maps\\download\\", sizeof(szDownload));
  SStrCopy(szUserMaps, szUser, sizeof(szUserMaps));
  SStrPack(szUserMaps, "maps\\", sizeof(szUserMaps));
  SStrCopy(szInstallMaps, szInstall, sizeof(szInstallMaps));
  SStrPack(szInstallMaps, "maps\\", sizeof(szInstallMaps));

  apszDirs[0] = szUserMaps;
  apszDirs[1] = szInstallMaps;
  nDirs = 2;
  for (int i = 0; i < PortMapFolderCount(); ++i)
    if (PortMapFolderPath(i, "maps\\", aszMaps[i], sizeof(aszMaps[i])))
      apszDirs[nDirs++] = aszMaps[i];
  apszDirs[nDirs] = NULL;
  dword_672960 = NULL;
  set_fd_callbacks(g_LocalStormID, FDTransmission, gdwMaxPacketSize - 4, szDownload, apszDirs);
  if (IsGameCreator(sgszNetMiscCpp, 0x158) && gpLoadGameFile == NULL) {
    dword_672960 = reinterpret_cast<TFD_SESSION *>(sub_440AB0(reinterpret_cast<intptr_t>(GetCurrentMap(1))));
    if (dword_672960 == NULL)
      return 0;
  }
  return 1;
}

// 0x4771E0 (51 B). The lowest valid player slot, or 8 when none is valid; in
// single player the local Storm id. It must stay inlinable and above IsGameHost,
// which is this body inlined.
DWORD __fastcall sub_4771E0()
{
  DWORD dwLowest;
  DWORD i;

  if (!gbMultiPlayerMode)
    return g_LocalStormID;

  dwLowest = 8;
  for (i = 0; i < 8; ++i)
  {
    if (IsValidPlayer((BYTE)i))
    {
      if (i < dwLowest)
        dwLowest = i;
    }
  }
  return dwLowest;
}

// 0x477270 (867 B). Fills the eight per-slot game results from the victory
// state and the drop flags, and the local slot's result through pnLocal.
void __fastcall sub_477270(int *pnStatus, int *pnLocal)
{
  DWORD i;
  DWORD nLast;
  BYTE bNation;
  BYTE bValid;
  int nPlayers;

  sub_47A7F0();
  if (gbMultiPlayerMode && NetworkMode() == 0x424E4554 && (DWORD)sub_47A780() <= 0x78) {
    for (i = 8; i-- != 0;)
      pnStatus[i] = 3;
    if (pnLocal)
      *pnLocal = 3;
    return;
  }

  for (i = 8; i-- != 0;)
    pnStatus[i] = 0;
  nPlayers = 0;
  for (i = 0; i < 8; i++) {
    bNation = static_cast<BYTE>(g_StormToNationID[i]);
    if (bNation < 8) {
      nPlayers++;
      switch (gbVictoryState[bNation]) {
      case 3:
        pnStatus[i] = 1;
        break;
      case 2:
        pnStatus[i] = 2;
        break;
      case 5:
        pnStatus[i] = 3;
        break;
      case 1:
        pnStatus[i] = 4;
        break;
      }
    }
  }
  if (pnStatus[g_LocalStormID] == 0)
    pnStatus[g_LocalStormID] = 2;
  if (pnLocal)
    *pnLocal = pnStatus[g_LocalStormID];

  if (gbMultiPlayerMode &&
      (nPlayers == 1 || (IsScenarioGame(NULL) && !IsTournamentGame() && !IsWGTGame()) ||
       gPlayback)) {
    for (i = 0; i < 8; i++) {
      if (g_StormToNationID[i] < 8)
        pnStatus[i] = 3;
    }
    return;
  }

  if (byte_529041 == 2) {
    if (byte_529042[0])
      pnStatus[0] = 0;
    if (byte_529042[1])
      pnStatus[1] = 0;
    if (byte_529042[2])
      pnStatus[2] = 0;
    if (byte_529042[3])
      pnStatus[3] = 0;
    if (byte_529042[4])
      pnStatus[4] = 0;
    if (byte_529042[5])
      pnStatus[5] = 0;
    if (byte_529042[6])
      pnStatus[6] = 0;
    if (byte_529042[7])
      pnStatus[7] = 0;
    pnStatus[g_LocalStormID] = 4;
  } else if (byte_529041 == 1) {
    if (byte_529042[0])
      pnStatus[0] = 4;
    if (byte_529042[1])
      pnStatus[1] = 4;
    if (byte_529042[2])
      pnStatus[2] = 4;
    if (byte_529042[3])
      pnStatus[3] = 4;
    if (byte_529042[4])
      pnStatus[4] = 4;
    if (byte_529042[5])
      pnStatus[5] = 4;
    if (byte_529042[6])
      pnStatus[6] = 4;
    if (byte_529042[7])
      pnStatus[7] = 4;
    pnStatus[g_LocalStormID] = 0;
  } else {
    if (byte_529042[0])
      pnStatus[0] = 4;
    if (byte_529042[1])
      pnStatus[1] = 4;
    if (byte_529042[2])
      pnStatus[2] = 4;
    if (byte_529042[3])
      pnStatus[3] = 4;
    if (byte_529042[4])
      pnStatus[4] = 4;
    if (byte_529042[5])
      pnStatus[5] = 4;
    if (byte_529042[6])
      pnStatus[6] = 4;
    if (byte_529042[7])
      pnStatus[7] = 4;
  }
  bValid = 0;
  nLast = 0;
  for (i = 0; i < 8; i++) {
    if (i != g_LocalStormID && IsValidPlayer(static_cast<BYTE>(i))) {
      bValid++;
      nLast = i;
    }
  }
  if (bValid == 1 && !CountDefaultComputerSlots() &&
      gbVictoryState[g_StormToNationID[nLast]] == 0) {
    pnStatus[nLast] = 1;
    gbVictoryState[g_StormToNationID[nLast]] = 3;
  }
  if (pnLocal)
    *pnLocal = pnStatus[g_LocalStormID];
}

// 0x4775F0 (19 B). Clears the results state: one byte at 0x529041 and the eight
// per-slot bytes at 0x529042, as two objects, not one nine-byte clear.
void clear_game_results()
{
  byte_529041 = 0;
  memset(byte_529042, 0, 8);
}

// 0x477610 (460 B). Builds the starting alliance and shared-vision tables:
// every slot allies itself and the neutral player, humans ally each other when
// allies are allowed, scenarios take their force settings and team games ally
// by team. The team pass covers only slots 0..10.
void alliance_init()
{
  DWORD i;
  DWORD j;
  int nForce;
  int nI;
  int nJ;
  DWORD dwVision;

  PlayerClear(sgbAlliances_g);  // Hyperion: and Hyperion's slots
  for (i = 8; i-- != 0;)
    sgbAlliances_g[i][i] = 1;

  for (i = 12; i-- != 0;) {
    sgbAlliances_g[11][i] = 1;
    sgbAlliances_g[i][11] = 1;
    if (AlliesAllowed()) {
      for (j = 8; j-- != 0;) {
        if (j != i && gPlayerData[i].nType == 1 && gPlayerData[j].nType == 1) {
          sgbAlliances_g[i][j] = 2;
          sgbAlliances_g[j][i] = 2;
        }
      }
    }
  }

  for (i = 0, dwVision = 1; i < 12; i++, dwVision <<= 1) {
    SharedVision[i] = dwVision;
    if (gPlayerData[i].nType == 3 || gPlayerData[i].nType == 7) {
      for (j = 0; j < 12; j++) {
        sgbAlliances_g[i][j] = 1;
        sgbAlliances_g[j][i] = 1;
      }
    }
  }

  if (IsScenarioGame(NULL)) {
    for (nForce = 1; nForce <= 4; nForce++) {
      BYTE bAlly = GetForceAllianceMode(static_cast<BYTE>(nForce));
      BYTE bVision = IsForceVisionShared(static_cast<BYTE>(nForce));

      if (bAlly != 0xFF)
        SetForceAlliance(static_cast<BYTE>(nForce), bAlly);
      if (bVision != 0xFF)
        SetForceVision(static_cast<BYTE>(nForce), bVision);
    }
  }

  if (IsTVBGame()) {
    for (nI = 0; nI < 11; nI++) {
      for (nJ = 0; nJ < 11; nJ++) {
        if (gPlayerData[nI].nType != 0 && gPlayerData[nJ].nType != 0) {
          int bSame = gPlayerData[nI].nTeam == gPlayerData[nJ].nTeam;

          sgbAlliances_g[nI][nJ] = bSame ? 2 : 0;
          if (bSame)
            SharedVision[nI] |= 1 << nJ;
        }
      }
    }
  }

  // Hyperion: in a game with slots past 7, every slot as 1.17 sets up the first
  // eight: allied with itself and neutral and seeing for itself, the computers allied
  // with each other when allies are allowed, a rescuable or neutral slot allied with all,
  // and a team game's teams allied
  if (g_hxPlayers) {
    const int n = PlayerSlots();
    for (nI = 0; nI < n; nI++) {
      if (nI == PLAYER_NEUTRAL)
        continue;
      sgbAlliances_g[nI][nI] = 1;
      sgbAlliances_g[PLAYER_NEUTRAL][nI] = 1;
      sgbAlliances_g[nI][PLAYER_NEUTRAL] = 1;
      PlayerVisionOnly(nI);  // Hyperion: 1ul << nI, the slots past 31 too (stage D)
    }
    for (nI = 0; nI < n; nI++) {
      for (nJ = 0; nJ < n; nJ++) {
        if (nI == nJ || nI == PLAYER_NEUTRAL || nJ == PLAYER_NEUTRAL)
          continue;
        if (AlliesAllowed() && gPlayerData[nI].nType == 1 && gPlayerData[nJ].nType == 1)
          sgbAlliances_g[nI][nJ] = 2;
        if (gPlayerData[nI].nType == 3 || gPlayerData[nI].nType == 7 ||
            gPlayerData[nJ].nType == 3 || gPlayerData[nJ].nType == 7)
          sgbAlliances_g[nI][nJ] = 1;
        if (IsTVBGame() && gPlayerData[nI].nType != 0 && gPlayerData[nJ].nType != 0) {
          const bool bSame = gPlayerData[nI].nTeam == gPlayerData[nJ].nTeam;
          sgbAlliances_g[nI][nJ] = bSame ? 2 : 0;
          if (bSame)
            PlayerVisionAdd(nI, nJ);  // Hyperion: the slots past 31 too (stage D)
        }
      }
    }
  }
}

// 0x4777E0 (30 B). 0x424E4554 is "TENB" as a little-endian dword, Storm's 'BNET'
// provider tag. Both exits materialise an explicit constant.
int IsBNetGame()
{
  if (gbMultiPlayerMode != 0 && NetworkMode() == 0x424E4554)
    return 1;
  return 0;
}

// 0x477800 (73 B). sub_4771E0 == g_LocalStormID, with sub_4771E0 inlined; its
// single-player arm compares the loaded id against itself.
BOOL __fastcall IsGameHost()
{
  return sub_4771E0() == g_LocalStormID;
}

// 0x477850 (13 B).
BOOL IsLoss()
{
  return byte_529041 == 2;
}

// 0x477860 (119 B). The `push 1h` before the `test eax, eax` is shared by two
// calls on two paths: net_error_popup's fourth argument on the failure arm and
// SNetSetGameMode's second on the success arm, which passes 1 where sub_476EF0
// passes 0. The prologue's `mov edx, 80h / mov ecx, 4EF2C4h` is IsGameCreator's
// file and line.
void sub_477860()
{
  int nBytesRead;
  DWORD dwGameMode;

  if (!IsGameCreator(sgszNetMiscCpp, 0x80))
    return;

  if (!SNetGetGameInfo(4, &dwGameMode, 4, &nBytesRead))
  {
    net_error_popup(0x51, sgszNetMiscCpp, 0x87, 1);
    return;
  }

  if (!SNetSetGameMode(dwGameMode, 1))
    net_error_popup(0x51, sgszNetMiscCpp, 0x8B, 1);

  sub_476EF0(CountDefaultHumanSlots());
}

// 0x4778E0 (283 B). Storm event 2. The lobby host vets the joiner: outside the
// chat room the player is dropped, in it the creator kicks anyone the ban list
// names and otherwise publishes the slot state. The two `packet_kick(id, 4)`
// statements are one block in 1.17, which MSVC cross-jumps.
void __stdcall snet_playerjoin_cb(void *pEvent)
{
  const DWORD *pdwEvent = static_cast<const DWORD *>(pEvent);
  char szName[0x19];

  if (pdwEvent[1] >= 8)
    return;

  sgdwLeaveReason_g[pdwEvent[1]] = 0;

  if (gwGameMode != 4) {
    if (pdwEvent[1] >= 8)
      return;

    SNetDropPlayer(pdwEvent[1], 3);
    if (gwGameMode == 3)
      wrap_map_msg(0, -1, 0);
    return;
  }

  if (!IsGameCreator(sgszNetMiscCpp, 0x261))
    return;

  if (get_lobby_state() != 4) {
    packet_kick(pdwEvent[1], 4);
    return;
  }

  if (dword_672964 != 0) {
    packet_kick(pdwEvent[1], 4);
    return;
  }

  if (pdwEvent[1] == 0) {
    on_chrm_kicked(4);
    return;
  }

  // The failure store is if-converted to neg / sbb / and on the buffer's first
  // byte; the source is the plain guard.
  if (!SNetGetPlayerName(pdwEvent[1], szName, 0x19))
    szName[0] = 0;

  if (IsPlayerBanned(szName)) {
    packet_kick(pdwEvent[1], 1);
    return;
  }

  if (dword_672960 != nullptr)
    set_fd_session_flags(dword_672960, 1 << pdwEvent[1]);

  if (IsScenarioGame(nullptr))
    packet_forces(pdwEvent[1]);

  packet_lobby_info(pdwEvent[1]);
}

// 0x477A00 (49 B). Storm's player-leave handler; the event record holds the slot
// at +4, the data at +8 and its length at +0Ch. The reason defaults to 3.
//
// Hyperion: read as Storm's event record, whose data pointer is
// pointer-wide, so the x64 build reads the pointer and the length where they are;
// on x86 the offsets are the dwords'.
// ===========================================================================
struct snet_event_t {
  DWORD dwEventId;
  DWORD dwSlot;
  const void *pData;
  DWORD cbData;
};
OC_X86_LAYOUT(offsetof(snet_event_t, pData) == 0x08 && offsetof(snet_event_t, cbData) == 0x0C,
              "[+8] the data pointer, [+0Ch] the data length");

void __stdcall snet_playerleave_cb(void *pEvent)
{
  const snet_event_t *pRec;
  DWORD dwSlot;
  DWORD dwReason;

  pRec = (const snet_event_t *)pEvent;
  dwSlot = pRec->dwSlot;
  if (dwSlot < 8)
  {
    dwReason = 0;
    if (pRec->pData != 0 && pRec->cbData >= 4)
      dwReason = *(const DWORD *)pRec->pData;
    if (dwReason == 0)
      dwReason = 3;
    sgdwLeaveReason_g[dwSlot] = dwReason;
  }
}

// A player left: announce it, pass the right id to the victory bookkeeping and
// republish the active count. The message id is `0x4B + (reason == 0x40000006)`
// through this TU's int view of error_get_str, for the 32-bit `add ecx, 4Bh`.
// The replay arms assign the name before the team, which merges it in ECX.
void sub_477A60(int nArg0, int nArg1, int nArg2)
{
  char szMsg[0x80];
  int nNation;
  const char *pszName;
  int bLive;
  BYTE aPacket[2];
  int nPlayer;
  int nTeam;

  bLive = 1;
  nPlayer = nArg1;

  if (gPlayback == 0) {
    DWORD dwSavedStormID;

    nPlayer = IsTeamGame() ? g_StormToHumanID[nArg0] : g_StormToNationID[nArg0];
    nNation = g_StormToNationID[nArg0];
    net_player_drop(static_cast<BYTE>(nArg0));
    voice_squelch(nArg0);

    dwSavedStormID = g_ActiveStormID;
    g_ActiveStormID = nArg0;
    aPacket[0] = 0x57;
    aPacket[1] = static_cast<BYTE>(nArg2);
    net_record_cmd(reinterpret_cast<intptr_t>(aPacket), 2);
    g_ActiveStormID = dwSavedStormID;

    pszName = net_player_get_name(static_cast<BYTE>(nArg0));
    nTeam = nNation;
  } else if (nArg0 == -1) {
    pszName = gPlayerData[nArg1].szName;
    nTeam = gPlayerData[nArg1].nTeam;
  } else {
    pszName = gPlayerSlots_g[nArg1].szName;
    nTeam = gPlayerSlots_g[nArg1].nTeam;
    bLive = 0;
  }

  if (pszName != nullptr) {
    const char *pszFmt = error_get_str(0x4B + (nArg2 == 0x40000006));

    psnprintf(szMsg, 0x80, pszFmt, pszName);
    map_msg_chat_msg(szMsg, 0);
  }

  if (bLive == 0)
    return;

  if (IsTeamGame()) {
    if (GetNumTeamActivePlayers(gPlayerData[nPlayer].nTeam) == 0) {
      if (nArg2 == 0x40000006)
        victory_disconnect(nTeam);
      else
        victory_quit(nTeam);
    }
  } else {
    if (nArg2 == 0x40000006)
      victory_disconnect(nPlayer);
    else
      victory_quit(nPlayer);
  }

  if (gPlayback == 0 && nArg2 == 0x40000006 &&
      (!IsTeamGame() || GetNumTeamActivePlayers(gPlayerData[nPlayer].nTeam) == 0))
    byte_529042[nArg0] = 1;

  sub_49C7D0(nPlayer);

  {
    int nFirst;
    int nLast;
    int nActive;

    nActive = 0;
    if (!SNetGetNumPlayers(&nFirst, &nLast, &nActive))
      net_error_popup(0x51, sgszNetMiscCpp, 0xB5, 1);
    byte_5264C1 = static_cast<BYTE>(nActive);
  }
}

// 0x477C20 (380 B). The lobby's own leave handler: drop the player from the
// file-dialog session, announce it in the chat room and reset the slot. The three
// slot fields are saved and restored around ResetSlotFromLobby, which rewrites
// them; only the type is conditional on coming back as 8 or 5. The announcement
// is two whole psnprintf calls, which MSVC merges into one.
void sub_477C20(int nArg0, int nArg1)
{
  char szMsg[0x80];
  DWORD nNation;
  BYTE bType;
  BYTE bRace;
  BYTE bTeam;
  PlayerData *p;

  net_player_drop(static_cast<BYTE>(nArg0));

  if (IsGameCreator(sgszNetMiscCpp, 0x166) && dword_672960 != nullptr) {
    sub_440BD0(dword_672960, 1 << nArg0);
    sub_440BE0(dword_672960, static_cast<BYTE>(nArg0));
  }

  sub_440F40(static_cast<BYTE>(nArg0));

  nNation = g_StormToNationID[nArg0];
  if (nNation >= 8) {
    if (nArg0 != 0)
      return;

    on_chrm_kicked(3);
    return;
  }

  if (glGluesMode == 3) {
    voice_squelch(nArg0);
    if (nArg0 == 0) {
      on_chrm_kicked(3);
      return;
    }

    if (net_player_get_name(static_cast<BYTE>(nArg0)) != nullptr) {
      if (nArg1 == 0x40000006)
        psnprintf(szMsg, 0x80, error_get_str(0x4C),
                  net_player_get_name(static_cast<BYTE>(nArg0)));
      else
        psnprintf(szMsg, 0x80, error_get_str(0x4B),
                  net_player_get_name(static_cast<BYTE>(nArg0)));
      on_recv_chat_from(8, reinterpret_cast<intptr_t>(szMsg));
    }
  }

  p = &gPlayerData[nNation];
  bType = p->nType;
  bRace = p->nRace;
  bTeam = p->nTeam;
  ResetSlotFromLobby(static_cast<BYTE>(nNation));

  if (bType == 8 || bType == 5)
    p->nType = bType;
  p->nRace = bRace;
  p->nTeam = bTeam;
  g_StormToNationID[nArg0] = 8;

  if (glGluesMode != 3)
    return;

  sub_451790();
  if (gpLoadGameFile != nullptr)
    ClearSaveSlot(static_cast<BYTE>(nNation));

  if (IsGameCreator(sgszNetMiscCpp, 0x1A1))
    sub_476EF0(CountDefaultHumanSlots());
}

// 0x477DA0 (87 B). Drains the leave-reason table: for each of the eight slots
// with a pending reason, dispatch it by game mode and clear the slot.
void sub_477DA0()
{
  DWORD i;
  DWORD dwReason;

  if (!gbMultiPlayerMode)
    return;

  for (i = 0; i < 8; ++i)
  {
    dwReason = sgdwLeaveReason_g[i];
    if (dwReason != 0)
    {
      if (gwGameMode == 4)
        sub_477C20(i, dwReason);
      else if (gwGameMode == 3)
        sub_477A60(i, 0, dwReason);
      sgdwLeaveReason_g[i] = 0;
    }
  }
}

// 0x477E00 (19 B). bPlayer is a BYTE: `movzx eax, cl` widens it for the push.
void __fastcall sub_477E00(BYTE bPlayer)
{
  sub_477A60(-1, g_ActiveNationID, bPlayer);
}

// 0x477220 (66 B).
unsigned char __fastcall get_num_active_players(void)
{
  int nFirst;
  int nLast;
  int nActive;

  nActive = 0;
  if (!SNetGetNumPlayers(&nFirst, &nLast, &nActive))
    net_error_popup(0x51, sgszNetMiscCpp, 0xB5, 1);

  byte_5264C1 = (BYTE)nActive;
  return (unsigned char)nActive;
}

void net_misc_init()
{
  memset(sgdwLeaveReason_g, 0, sizeof(sgdwLeaveReason_g));
}

// Clears the "results already written" latch.
void clear_results_written()
{
  sgbGameResultsWritten_g = 0;
}
