// port_stormnet.cpp -- Storm's network functions, the port's own: the SNet functions 1.17
// imports, with Storm's names and shapes (Net; docs/modules/lan.md).
//
// Single player and replays go through SNet: the game starts provider 0, creates a game of one
// player, and each frame sends its turn and reads it back. Storm served provider 0 with
// standard.snp's loopback, and this does what that did, read from storm.dll's code: a local
// session of one player whose turns and messages go straight onto its own queues, in order;
// tools/porttest/net_test.cpp compares the two. 1.17's other providers are not carried over
// (PRD, 2026-09-26): in their place is Hyperion's LAN provider (port_stormlan.cpp)
// under 1.17's LAN id, 'UDPN', which every SNet call goes to once it is started (g_bLan); any
// other fails to start as Storm failed for one it could not find. Under the loopback the calls
// only other providers answered (joining, listing games, Battle.net and league commands) fail
// as they did, and no event is raised. Errors go through SErrSetLastError (port_stormerr.cpp).
#include <windows.h>
#include <string.h>
#include <deque>
#include <vector>
#include "port_stormlan.h"
#include "../data/port_stormerr.h"  // NET_*
#include "port_stormnet.h"
#include "../data/port_stormapi.h"
#include "net_cmds.h"  // GetCommandByteSize

static const BYTE NO_PLAYER = 0xFF;
#undef NAME_MAX  // <limits.h>'s, which Emscripten's headers define
static const DWORD NAME_MAX = 0x80;  // Storm's buffers for names and the game's strings

// ReceiveTurns' flags for each player.
static const DWORD TURN_PRESENT = 0x10000;
static const DWORD TURN_READY = 0x20000;
static const DWORD TURN_LOCAL = 0x40000;

// The loopback's caps as Storm reports them: its message size less Storm's
// 16-byte header, one player, and its flags cleared.
static const DWORD LOOPBACK_CAPS[9] = {0x24, 0, 0x1F0, 0x10, 1, 0x300000, 0, 0x1E, 0};

// The mode bits CreateGame sets.
static const DWORD MODE_PASSWORD = 0x1;
static const DWORD MODE_ONE_PLAYER = 0x2;
static const DWORD MODE_STARTED = 0x4;

struct NetPacket {
  WORD wSeq;
  std::vector<BYTE> data;
};

// The local player. Storm numbers each kind of packet it sends a player, and a
// read takes the oldest only when its number is the next one expected.
struct NetPlayer {
  char szName[NAME_MAX];
  char szDesc[NAME_MAX];
  bool bCreator;
  WORD wTurnsSent, wTurnsRead;
  WORD wMessagesSent, wMessagesRead;
  std::deque<NetPacket> turns, messages;
};

static bool g_bNetUp;                // provider 0 started, or the LAN's
static bool g_bLan;                  // the LAN provider started: the calls are port_stormlan.cpp's
static BYTE g_bLocal = NO_PLAYER;    // the local player's id, or NO_PLAYER outside a game
static NetPlayer g_player;
static char g_szGameName[NAME_MAX];
static char g_szPassword[NAME_MAX];
static char g_szStats[NAME_MAX];
static DWORD g_dwMode;
static DWORD g_dwMaxPlayers;
static std::vector<BYTE> g_gameData;
static char g_aszNames[256][NAME_MAX];
static DWORD g_nNames;               // what GetPlayerNames hands out; a game leaves it, Destroy clears it
// What the last reads handed out, kept until the next read of the same kind.
static std::vector<NetPacket> g_turnsOut, g_messagesOut;

// Storm finds a player by the low byte of the id it is given; 0xFF is nobody.
static bool IsLocal(DWORD dwId)
{
  return g_bLocal != NO_PLAYER && static_cast<BYTE>(dwId) != NO_PLAYER &&
         static_cast<BYTE>(dwId) == g_bLocal;
}

static BOOL Fail(DWORD dwErr)
{
  SErrSetLastError(dwErr);
  return FALSE;
}

// The checks most calls start with.
static bool InGame()
{
  if (!g_bNetUp) {
    SErrSetLastError(NET_BAD_PROVIDER);
    return false;
  }
  if (g_bLocal == NO_PLAYER) {
    SErrSetLastError(NET_NOT_IN_GAME);
    return false;
  }
  return true;
}

// Storm frees its players when a game ends and makes the local one afresh.
static void ResetPlayer()
{
  g_player.szName[0] = '\0';
  g_player.szDesc[0] = '\0';
  g_player.bCreator = false;
  g_player.wTurnsSent = g_player.wTurnsRead = 0;
  g_player.wMessagesSent = g_player.wMessagesRead = 0;
  g_player.turns.clear();
  g_player.messages.clear();
  g_turnsOut.clear();
  g_messagesOut.clear();
}

static void Enqueue(std::deque<NetPacket> &queue, WORD &wSeq, const void *pvData, DWORD cbData)
{
  NetPacket packet;
  packet.wSeq = wSeq++;
  packet.data.assign(static_cast<const BYTE *>(pvData), static_cast<const BYTE *>(pvData) + cbData);
  queue.push_back(packet);
}

// Storm stores a packet's length, header included, in 16 bits.
static DWORD PacketLength(const NetPacket &packet)
{
  return static_cast<WORD>(packet.data.size() + 12) - 12;
}

static void LeaveGame()
{
  g_gameData.clear();
  g_szGameName[0] = g_szStats[0] = g_szPassword[0] = '\0';
  g_dwMode = 0;
  g_bLocal = NO_PLAYER;
  g_dwMaxPlayers = 0;
  ResetPlayer();
}

extern "C" {


// Ordinal 117. Only provider 0 is found. Storm lets go of the provider it had
// before looking, so a provider it cannot find leaves none started, and a game
// under way stays as it was either way.
BOOL __stdcall SNetInitializeProvider(DWORD dwProvider, void *pProgramData, void *pPlayerData,
                                      void *pUiData, void *pVersionData)
{
  if (g_bLan) {
    LanDestroy();
    g_bLan = false;
  }
  if (dwProvider == LAN_PROVIDER) {
    LeaveGame();
    g_bNetUp = g_bLan = LanInitialize() != FALSE;
    return g_bNetUp ? TRUE : FALSE;
  }
  g_bNetUp = dwProvider == 0;
  return g_bNetUp ? TRUE : Fail(NET_BAD_PROVIDER);
}

// Ordinal 102. Ends a game under way and lets the provider go; it cannot fail.
// Storm's leaves a game under way when no provider is started, with its player
// gone; only starting a provider that is not there during a game leads there,
// which the game never does, and this ends the game.
BOOL __stdcall SNetDestroy()
{
  if (g_bLan) {
    LanDestroy();
    g_bLan = false;
  }
  LeaveGame();
  g_nNames = 0;
  g_bNetUp = false;
  return TRUE;
}

// Ordinal 114.
BOOL __stdcall SNetGetProviderCaps(DWORD *pdwCaps)
{
  if (g_bLan)
    return LanGetProviderCaps(pdwCaps);
  if (pdwCaps[0] != sizeof LOOPBACK_CAPS)
    return Fail(NET_INVALID);
  memset(pdwCaps + 1, 0, sizeof LOOPBACK_CAPS - 4);
  if (!g_bNetUp)
    return Fail(NET_BAD_PROVIDER);
  memcpy(pdwCaps, LOOPBACK_CAPS, sizeof LOOPBACK_CAPS);
  return TRUE;
}

// Ordinal 112. The player's caps are the provider's.
BOOL __stdcall SNetGetPlayerCaps(DWORD dwPlayer, DWORD *pdwCaps)
{
  if (g_bLan)
    return LanGetPlayerCaps(dwPlayer, pdwCaps);
  if (pdwCaps[0] != sizeof LOOPBACK_CAPS)
    return Fail(NET_INVALID);
  memset(pdwCaps + 1, 0, sizeof LOOPBACK_CAPS - 4);
  if (!InGame())
    return FALSE;
  if (!IsLocal(dwPlayer))
    return Fail(NET_NO_PLAYER);
  memcpy(pdwCaps, LOOPBACK_CAPS, sizeof LOOPBACK_CAPS);
  return TRUE;
}

// Ordinal 147.
DWORD __stdcall SNetGetCurrentProviderID()
{
  return g_bLan ? LAN_PROVIDER : 0;
}

// Ordinal 105. The LAN provider is listed: provider 0 never was, and 1.17's others
// are gone.
BOOL __stdcall SNetEnumProviders(const DWORD *pdwCapsFilter, void *pfnCallback)
{
  if (!pfnCallback || (pdwCapsFilter && pdwCapsFilter[0] != sizeof LOOPBACK_CAPS))
    return Fail(NET_INVALID);
  LanListProvider(pfnCallback);
  return TRUE;
}

// Ordinal 103. The loopback has no devices.
BOOL __stdcall SNetEnumDevices(void *pfnCallback)
{
  if (!pfnCallback)
    return Fail(NET_INVALID);
  if (!g_bNetUp)
    return Fail(NET_BAD_PROVIDER);
  return TRUE;
}

// Ordinal 116. The loopback has no device to start, and says nothing about why.
BOOL __stdcall SNetInitializeDevice(DWORD dwDevice, DWORD dwCaps, DWORD_PTR dwArg2, void *pData,
                                    void *pOut)
{
  if (!g_bNetUp)
    return Fail(NET_BAD_PROVIDER);
  return FALSE;
}

// Ordinal 123. Kept for nothing: no event is raised in a game of one player.
BOOL __stdcall SNetRegisterEventHandler(DWORD dwEvent, void *pfnHandler)
{
  if (g_bLan)
    return LanRegisterEventHandler(dwEvent, pfnHandler);
  if (!pfnHandler)
    return Fail(NET_INVALID);
  return TRUE;
}

// Ordinal 138, which CreateGame calls with no ladder values. The game is created
// with the local player as id 0, its creator; one already under way ends first.
BOOL __stdcall SNetCreateLadderGame(const char *pszGameName, const char *pszPassword,
                                    const char *pszStats, DWORD dwCategory, DWORD dwGameType,
                                    DWORD dwMode, const void *pvGameData, DWORD cbGameData,
                                    DWORD dwMaxPlayers, const char *pszPlayerName,
                                    const char *pszPlayerDesc, DWORD *pdwPlayerId)
{
  if (pdwPlayerId)
    *pdwPlayerId = static_cast<DWORD>(-1);
  if (!pszGameName[0] || !dwMaxPlayers || !pdwPlayerId)
    return Fail(NET_INVALID);
  if (!g_bNetUp)
    return Fail(NET_BAD_PROVIDER);
  if (!pszPassword)
    pszPassword = "";
  if (!pszStats)
    pszStats = "";
  if (!pszPlayerName)
    pszPlayerName = "";
  if (!pszPlayerDesc)
    pszPlayerDesc = "";
  // Storm asks the provider for a name when it is given none; the loopback has none.
  if (!pszPlayerName[0])
    return Fail(NET_INVALID);
  if (g_bLan)
    return LanCreateGame(pszGameName, pszPassword, pszStats, dwMode, pvGameData, cbGameData, dwMaxPlayers,
                         pszPlayerName, pszPlayerDesc, pdwPlayerId);

  if (g_bLocal != NO_PLAYER)
    LeaveGame();
  ResetPlayer();
  g_dwMaxPlayers = dwMaxPlayers;
  SStrCopy(g_szGameName, pszGameName, NAME_MAX);
  SStrCopy(g_szStats, pszStats, NAME_MAX);
  SStrCopy(g_szPassword, pszPassword, NAME_MAX);
  g_dwMode = dwMode;
  if (pszPassword[0])
    g_dwMode |= MODE_PASSWORD;
  if (dwMaxPlayers <= 1)
    g_dwMode |= MODE_ONE_PLAYER;
  g_gameData.assign(static_cast<const BYTE *>(pvGameData),
                    static_cast<const BYTE *>(pvGameData) + (pvGameData ? cbGameData : 0));
  g_bLocal = 0;
  SStrCopy(g_player.szName, pszPlayerName, NAME_MAX);
  SStrCopy(g_player.szDesc, pszPlayerDesc, NAME_MAX);
  g_player.bCreator = true;
  *pdwPlayerId = g_bLocal;
  g_nNames = g_bLocal + 1;
  SStrCopy(g_aszNames[g_bLocal], pszPlayerName, NAME_MAX);
  g_dwMode |= MODE_STARTED;
  return TRUE;
}

// Ordinal 101.
BOOL __stdcall SNetCreateGame(const char *pszGameName, const char *pszPassword, const char *pszStats,
                              DWORD dwCategory, const void *pvGameData, DWORD cbGameData,
                              DWORD dwMaxPlayers, const char *pszPlayerName,
                              const char *pszPlayerDesc, DWORD *pdwPlayerId)
{
  return SNetCreateLadderGame(pszGameName, pszPassword, pszStats, dwCategory, 0, 0, pvGameData,
                              cbGameData, dwMaxPlayers, pszPlayerName, pszPlayerDesc, pdwPlayerId);
}

// Ordinal 119. Storm then sleeps for the provider's latency, at least 50 ms, for
// the others to hear of it; with nobody to tell, this does not.
BOOL __stdcall SNetLeaveGame(DWORD dwReason)
{
  if (g_bLan)
    return LanLeaveGame(dwReason);
  if (!g_bNetUp)
    return Fail(NET_BAD_PROVIDER);
  LeaveGame();
  return TRUE;
}

// Ordinal 106. Only another player can be dropped, and there is none.
BOOL __stdcall SNetDropPlayer(DWORD dwPlayer, DWORD dwFlags)
{
  if (g_bLan)
    return LanDropPlayer(dwPlayer, dwFlags);
  if (!g_bNetUp)
    return Fail(NET_BAD_PROVIDER);
  if (dwPlayer == NO_PLAYER || dwPlayer == g_bLocal)
    return Fail(NET_INVALID);
  if (g_bLocal == NO_PLAYER)
    return Fail(NET_NOT_IN_GAME);
  if (!IsLocal(dwPlayer))
    return Fail(NET_NO_PLAYER);
  return TRUE;
}

// Ordinal 137. Every other player is let go of; there is none.
BOOL __stdcall SNetDisconnectAll(DWORD dwFlags)
{
  if (g_bLan)
    return LanDisconnectAll(dwFlags);
  return InGame() ? TRUE : FALSE;
}

// Ordinal 109. The first and last ids and the number of players.
BOOL __stdcall SNetGetNumPlayers(int *pnFirst, int *pnLast, int *pnActive)
{
  if (g_bLan)
    return LanGetNumPlayers(pnFirst, pnLast, pnActive);
  if (pnFirst)
    *pnFirst = -1;
  if (pnLast)
    *pnLast = -1;
  if (pnActive)
    *pnActive = 0;
  if (!InGame())
    return FALSE;
  if (pnFirst)
    *pnFirst = g_bLocal;
  if (pnLast)
    *pnLast = g_bLocal;
  if (pnActive)
    *pnActive = 1;
  return TRUE;
}

// Ordinal 113.
BOOL __stdcall SNetGetPlayerName(DWORD dwPlayer, char *pszBuffer, DWORD cchBuffer)
{
  if (g_bLan)
    return LanGetPlayerName(dwPlayer, pszBuffer, cchBuffer);
  if (!pszBuffer || !cchBuffer)
    return Fail(NET_INVALID);
  pszBuffer[0] = '\0';
  if (!InGame())
    return FALSE;
  if (!IsLocal(dwPlayer))
    return Fail(NET_NO_PLAYER);
  SStrCopy(pszBuffer, g_player.szName, cchBuffer);
  return TRUE;
}

// Ordinal 144. Pointers to the names of the last game's players, which stay
// until the next game or SNetDestroy.
BOOL __stdcall SNetGetPlayerNames(char **ppszNames)
{
  if (g_bLan)
    return LanGetPlayerNames(ppszNames);
  for (DWORD i = 0; i < g_nNames && i < 8; ++i)
    ppszNames[i] = g_aszNames[i];
  return TRUE;
}

// Ordinal 107. 1 the game's name, 2 its password, 3 its stat string, 4 its mode,
// 5 the game data it was created with, 6 its number of players. A string cut
// short keeps its terminator in the buffer's last byte.
BOOL __stdcall SNetGetGameInfo(DWORD dwInfo, void *pvBuffer, DWORD cbBuffer, DWORD *pcbRead)
{
  if (g_bLan)
    return LanGetGameInfo(dwInfo, pvBuffer, cbBuffer, pcbRead);
  if (pvBuffer && cbBuffer)
    memset(pvBuffer, 0, cbBuffer);
  if (pcbRead)
    *pcbRead = 0;
  if (!InGame())
    return FALSE;
  const void *pvInfo;
  DWORD cbInfo;
  bool bString = false;
  switch (dwInfo) {
  case 1: pvInfo = g_szGameName; cbInfo = static_cast<DWORD>(strlen(g_szGameName)) + 1; bString = true; break;
  case 2: pvInfo = g_szPassword; cbInfo = static_cast<DWORD>(strlen(g_szPassword)) + 1; bString = true; break;
  case 3: pvInfo = g_szStats; cbInfo = static_cast<DWORD>(strlen(g_szStats)) + 1; bString = true; break;
  case 4: pvInfo = &g_dwMode; cbInfo = 4; break;
  case 5: pvInfo = g_gameData.data(); cbInfo = static_cast<DWORD>(g_gameData.size()); break;
  case 6: pvInfo = &g_dwMaxPlayers; cbInfo = 4; break;
  default: return Fail(NET_INVALID);
  }
  if (pvBuffer && cbBuffer) {
    if (cbInfo > cbBuffer)
      cbInfo = cbBuffer;
    memcpy(pvBuffer, pvInfo, cbInfo);
    if (bString)
      static_cast<char *>(pvBuffer)[cbBuffer - 1] = '\0';
  }
  if (pcbRead)
    *pcbRead = cbInfo;
  return TRUE;
}

// Ordinal 130. Only the game's creator sets its mode; flag 1 makes it public,
// dropping the password.
BOOL __stdcall SNetSetGameMode(DWORD dwMode, DWORD dwMakePublic)
{
  if (g_bLan)
    return LanSetGameMode(dwMode, dwMakePublic);
  // Storm answers this one with 0x57 when no provider is started.
  if (!g_bNetUp)
    return Fail(NET_INVALID);
  if (g_bLocal == NO_PLAYER)
    return Fail(NET_NOT_IN_GAME);
  if (!g_player.bCreator)
    return Fail(NET_NOT_OWNER);
  if (dwMakePublic) {
    dwMode &= ~MODE_PASSWORD;
    g_szPassword[0] = '\0';
  }
  g_dwMode = dwMode;
  return TRUE;
}

// Ordinal 115. The turns sent and not yet read.
BOOL __stdcall SNetGetTurnsInTransit(DWORD *pdwTurns)
{
  if (g_bLan)
    return LanGetTurnsInTransit(pdwTurns);
  if (!pdwTurns)
    return Fail(NET_INVALID);
  *pdwTurns = 0;
  if (!InGame())
    return FALSE;
  *pdwTurns = static_cast<WORD>(g_player.wTurnsSent - g_player.wTurnsRead);
  return TRUE;
}

// Ordinal 128. A turn goes to every player, here only the local one.
BOOL __stdcall SNetSendTurn(const void *pvData, DWORD cbData)
{
  if (g_bLan)
    return LanSendTurn(pvData, cbData);
  if (!pvData || !cbData)
    return Fail(NET_INVALID);
  if (!InGame())
    return FALSE;
  Enqueue(g_player.turns, g_player.wTurnsSent, pvData, cbData);
  return TRUE;
}

// Ordinal 122. The next turn of each player from nFirst on, once every player
// there has sent it. A player's flags say it is there, that its turn is ready
// and that it is the local one. The local player's count of turns read goes up
// by one on every success, whether or not its own turn was among those asked
// for, as Storm's does.
BOOL __stdcall SNetReceiveTurns(DWORD nFirst, DWORD nPlayers, const void **ppvData, DWORD *pcbData,
                                DWORD *pdwFlags)
{
  if (g_bLan)
    return LanReceiveTurns(nFirst, nPlayers, ppvData, pcbData, pdwFlags);
  if (nPlayers) {
    if (ppvData)
      memset(ppvData, 0, nPlayers * sizeof(*ppvData));
    if (pcbData)
      memset(pcbData, 0, nPlayers * 4);
    if (pdwFlags)
      memset(pdwFlags, 0, nPlayers * 4);
  }
  if (!nPlayers || !ppvData || !pcbData || !pdwFlags)
    return Fail(NET_INVALID);
  if (!InGame())
    return FALSE;
  // What the last read handed out goes now, and so do turns older than the next
  // one expected, which a read that left the local player out skipped past.
  g_turnsOut.clear();
  while (!g_player.turns.empty() && g_player.turns.front().wSeq != g_player.wTurnsRead &&
         static_cast<WORD>(g_player.wTurnsRead - g_player.turns.front().wSeq) <= 0x7FFF)
    g_player.turns.pop_front();
  bool bAllReady = true;
  for (DWORD i = nFirst; i < nFirst + nPlayers; ++i) {
    DWORD dwFlags = 0;
    if (IsLocal(i)) {
      dwFlags = TURN_PRESENT;
      if (!g_player.turns.empty() && g_player.turns.front().wSeq == g_player.wTurnsRead)
        dwFlags |= TURN_READY;
      else
        bAllReady = false;
      if (i == g_bLocal)
        dwFlags |= TURN_LOCAL;
    }
    pdwFlags[i - nFirst] = dwFlags;
  }
  if (!bAllReady)
    return Fail(NET_NOTHING_WAITING);
  const WORD wRead = g_player.wTurnsRead;
  for (DWORD i = nFirst; i < nFirst + nPlayers; ++i) {
    if (pdwFlags[i - nFirst] & TURN_READY) {
      g_turnsOut.push_back(g_player.turns.front());
      g_player.turns.pop_front();
      ++g_player.wTurnsRead;
      // zeros after the turn, as port_stormlan.cpp's: the classic walker reads a cut-off last
      // command whole before it finds it cut off
      pcbData[i - nFirst] = PacketLength(g_turnsOut.back());
      g_turnsOut.back().data.resize(g_turnsOut.back().data.size() + 2048, 0);
      ppvData[i - nFirst] = g_turnsOut.back().data.data();
    }
  }
  g_player.wTurnsRead = wRead + 1;
  return TRUE;
}

// Ordinal 127. To one player, to everyone (-1) or to everyone else (-2).
BOOL __stdcall SNetSendMessage(DWORD dwPlayer, const void *pvData, DWORD cbData)
{
  if (g_bLan)
    return LanSendMessage(dwPlayer, pvData, cbData);
  if (!pvData || !cbData)
    return Fail(NET_INVALID);
  if (!InGame())
    return FALSE;
  const int nTo = static_cast<int>(dwPlayer);
  if (nTo == -2)
    return TRUE;
  if (nTo != -1 && !IsLocal(dwPlayer))
    return Fail(NET_NO_PLAYER);
  Enqueue(g_player.messages, g_player.wMessagesSent, pvData, cbData);
  return TRUE;
}

// Ordinal 121. The oldest message waiting and who sent it.
BOOL __stdcall SNetReceiveMessage(int *pnPlayer, const void **ppvData, DWORD *pcbData)
{
  if (g_bLan)
    return LanReceiveMessage(pnPlayer, ppvData, pcbData);
  if (pnPlayer)
    *pnPlayer = -1;
  if (ppvData)
    *ppvData = NULL;
  if (pcbData)
    *pcbData = 0;
  if (!ppvData || !pcbData)
    return Fail(NET_INVALID);
  if (!InGame())
    return FALSE;
  if (g_player.messages.empty() || g_player.messages.front().wSeq != g_player.wMessagesRead)
    return Fail(NET_NOTHING_WAITING);
  g_messagesOut.clear();
  g_messagesOut.push_back(g_player.messages.front());
  g_player.messages.pop_front();
  ++g_player.wMessagesRead;
  if (pnPlayer)
    *pnPlayer = g_bLocal;
  *ppvData = g_messagesOut.back().data.data();
  *pcbData = PacketLength(g_messagesOut.back());
  return TRUE;
}

// Ordinal 139. The loopback reports no results anywhere.
BOOL __stdcall SNetReportGameResult(DWORD nFirst, DWORD nPlayers, const int *pnResults,
                                    const char *pszHeader, const char *pszDetail)
{
  if (!nPlayers || !pnResults)
    return Fail(NET_INVALID);
  if (!g_bNetUp)
    return Fail(NET_BAD_PROVIDER);
  return TRUE;
}

// Ordinal 120. No upgrade is ever waiting. Storm read the registry for one and,
// finding none, answered with no error of its own.
BOOL __stdcall SNetPerformUpgrade(DWORD *pdwStatus)
{
  if (!pdwStatus)
    return Fail(NET_INVALID);
  *pdwStatus = 0;
  return FALSE;
}

// The calls only the other providers answered. Under the loopback each fails,
// or, for GetReplyName, succeeds with nothing, as here.

// Ordinal 133. The loopback lists no games.
BOOL __stdcall SNetEnumGamesEx(DWORD dwCategory, DWORD dwMask, void *pfnCallback, DWORD *pdwHintNext)
{
  if (g_bLan && pfnCallback)
    return LanEnumGames(pfnCallback, pdwHintNext);
  if (!pfnCallback)
    return Fail(NET_INVALID);
  if (pdwHintNext)
    *pdwHintNext = 0;
  if (!g_bNetUp)
    return Fail(NET_BAD_PROVIDER);
  return TRUE;
}

// Ordinal 118. Storm ends the game under way before it asks the provider, and
// the loopback has no game to join.
BOOL __stdcall SNetJoinGame(DWORD dwGameId, const char *pszGameName, const char *pszPassword,
                            const char *pszPlayerName, const char *pszPlayerDesc, DWORD *pdwPlayerId)
{
  if (!pdwPlayerId)
    return Fail(NET_INVALID);
  *pdwPlayerId = static_cast<DWORD>(-1);
  if (!g_bNetUp)
    return Fail(NET_BAD_PROVIDER);
  if (!pszPlayerName || !pszPlayerName[0])
    return Fail(NET_INVALID);
  if (g_bLan)
    return LanJoinGame(dwGameId, pszPassword, pszPlayerName, pszPlayerDesc, pdwPlayerId);
  if (g_bLocal != NO_PLAYER)
    LeaveGame();
  ResetPlayer();
  g_nNames = 0;
  return FALSE;
}

// Ordinal 125, Battle.net's own screens.
BOOL __stdcall SNetSelectGame(DWORD dwFlags, void *pProgramData, void *pPlayerData, void *pUiData,
                              void *pVersionData, DWORD *pdwPlayerId)
{
  if (pdwPlayerId)
    *pdwPlayerId = 0;
  if (!g_bNetUp)
    return Fail(NET_BAD_PROVIDER);
  return FALSE;
}

// Ordinal 134.
BOOL __stdcall SNetSendServerChatCommand(const char *pszCommand)
{
  if (!pszCommand[0])
    return Fail(NET_INVALID);
  if (!InGame())
    return FALSE;
  return FALSE;
}

// Ordinal 141. A league command starts with '/'.
BOOL __stdcall SNetSendLeagueCommand(const char *pszCommand, void *pfnCallback)
{
  if (!pszCommand || pszCommand[0] != '/')
    return Fail(NET_INVALID);
  return Fail(NET_BAD_PROVIDER);
}

// Ordinal 142.
BOOL __stdcall SNetSendReplayPath(const char *pszPath, DWORD dwGameId, int nResult)
{
  if (!pszPath)
    return Fail(NET_INVALID);
  return Fail(NET_BAD_PROVIDER);
}

// Ordinal 143.
BOOL __stdcall SNetGetLeagueName(int *pnLeague)
{
  if (!pnLeague)
    return Fail(NET_INVALID);
  return Fail(NET_BAD_PROVIDER);
}

// Ordinal 145.
BOOL __stdcall SNetLeagueLogout(const char *pszName)
{
  if (!pszName)
    return Fail(NET_INVALID);
  return Fail(NET_BAD_PROVIDER);
}

// Ordinal 146. Succeeds with the error set and the buffer untouched, as Storm's
// does when no provider answers it.
BOOL __stdcall SNetGetReplyName(char *pszBuffer, DWORD cchBuffer)
{
  if (!pszBuffer || !cchBuffer)
    return Fail(NET_INVALID);
  SErrSetLastError(NET_BAD_PROVIDER);
  return TRUE;
}

}  // extern "C"

// Exported by name, __fastcall. Battle.net's downloads were checked with it; kept
// for nothing.
void __fastcall SNetSetCodeSignVerifcationFunction(bool(__fastcall *pfnVerify)(const char *))
{
}

// The trainer's snapshots (port_snap.cpp): the turns and messages sent and not yet read, and
// the numbers that order them, so a game put back at a frame's end has the commands it had
// in transit then and not the ones of the moment it left. The trainer, which gives a line's
// commands itself, keeps only their syncs.
static void PutWord(std::vector<unsigned char> *pOut, DWORD dw)
{
  const unsigned char ab[4] = {static_cast<unsigned char>(dw), static_cast<unsigned char>(dw >> 8),
                               static_cast<unsigned char>(dw >> 16), static_cast<unsigned char>(dw >> 24)};
  pOut->insert(pOut->end(), ab, ab + 4);
}

static DWORD GetWord(const unsigned char *&p)
{
  const DWORD dw = p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<DWORD>(p[3]) << 24);
  p += 4;
  return dw;
}

static void PutQueue(std::vector<unsigned char> *pOut, const std::deque<NetPacket> &queue)
{
  PutWord(pOut, static_cast<DWORD>(queue.size()));
  for (const NetPacket &packet : queue) {
    PutWord(pOut, packet.wSeq);
    PutWord(pOut, static_cast<DWORD>(packet.data.size()));
    pOut->insert(pOut->end(), packet.data.begin(), packet.data.end());
  }
}

// A blob cut short or damaged stops the walk where it ends: the queue keeps the packets read
// whole and nothing is read past the blob.
static void GetQueue(const unsigned char *&p, const unsigned char *pEnd, std::deque<NetPacket> &queue)
{
  queue.clear();
  if (pEnd - p < 4)
    return;
  for (DWORD n = GetWord(p); n > 0; n--) {
    if (pEnd - p < 8)
      return;
    NetPacket packet;
    packet.wSeq = static_cast<WORD>(GetWord(p));
    const DWORD cb = GetWord(p);
    if (static_cast<size_t>(pEnd - p) < cb)
      return;
    packet.data.assign(p, p + cb);
    p += cb;
    queue.push_back(packet);
  }
}

void PortNetSnapSave(std::vector<unsigned char> *pOut)
{
  pOut->clear();
  PutWord(pOut, g_player.wTurnsSent);
  PutWord(pOut, g_player.wTurnsRead);
  PutWord(pOut, g_player.wMessagesSent);
  PutWord(pOut, g_player.wMessagesRead);
  PutQueue(pOut, g_player.turns);
  PutQueue(pOut, g_player.messages);
}

// A turn's commands less all but the syncs (0x05, 0x37), which every turn must hold once: the
// new length. A command of no known size ends the walk, what is after it dropped.
size_t PortNetKeepSyncs(unsigned char *p, size_t cb)
{
  size_t nAt = 0, nKept = 0;
  while (nAt < cb) {
    // a select command's size is its count byte, the one after the opcode: a turn that ends on
    // the opcode ends the walk, since the size cannot be read
    const BYTE bOpcode = p[nAt];
    const bool bCounted = (bOpcode >= 9 && bOpcode <= 11) || (bOpcode >= 0x89 && bOpcode <= 0x8C);
    if (bCounted && nAt + 2 > cb)
      break;
    const int n = GetCommandByteSize(reinterpret_cast<const net_cmd_select_t *>(p + nAt));
    if (n <= 0 || nAt + static_cast<size_t>(n) > cb)
      break;
    if (p[nAt] == 0x05 || p[nAt] == 0x37) {
      memmove(p + nKept, p + nAt, static_cast<size_t>(n));
      nKept += static_cast<size_t>(n);
    }
    nAt += static_cast<size_t>(n);
  }
  return nKept;
}

void PortNetSnapRestore(const std::vector<unsigned char> &saved, bool bKeepSyncs)
{
  if (saved.size() < 24)
    return;
  const unsigned char *p = saved.data();
  const unsigned char *pEnd = saved.data() + saved.size();
  g_player.wTurnsSent = static_cast<WORD>(GetWord(p));
  g_player.wTurnsRead = static_cast<WORD>(GetWord(p));
  g_player.wMessagesSent = static_cast<WORD>(GetWord(p));
  g_player.wMessagesRead = static_cast<WORD>(GetWord(p));
  GetQueue(p, pEnd, g_player.turns);
  GetQueue(p, pEnd, g_player.messages);
  if (bKeepSyncs)
    for (NetPacket &packet : g_player.turns) {
      packet.data.resize(PortNetKeepSyncs(packet.data.data(), packet.data.size()));
      if (packet.data.empty())
        packet.data.push_back(0x05);  // a turn is never empty: SendTurns' own filler
    }
}
