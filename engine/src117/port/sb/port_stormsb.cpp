// port_stormsb.cpp -- Storm's online provider: 1.17's multiplayer, its own lobby and
// lockstep, between Hyperions anywhere, over ShieldBattery's rally-point2 netcode
// (docs/sb-netcode-port.md). Presentation and transport only: the game's SNet calls
// (port_stormnet.cpp hands them here once the provider is started) are bridged to the
// rallypoint C ABI (tools/rallypoint2/bridge/), whose one Tokio thread drives the QUIC
// link to the relay. The game computes what it computes from its turns, as from Storm's.
//
// The division of labour mirrors the LAN provider's (port_stormlan.cpp), with the
// relay's own machinery replacing the LAN relay's:
//
// - OUT: SNetSendTurn hands the assembled turn to rp_submit_turn; a copy is also queued
//   locally (the relay never echoes a client's own turns back).
// - IN: SNetReceiveTurns drains rp_poll (peers' turns, buffer directives, synced
//   leaves, chat) and gates on every in-game slot having its turn, as the LAN
//   relay's steps did. Turns handed to the game carry 2048 zero bytes after them, as
//   the LAN provider's, for the classic walker that reads a cut-off last command whole.
// - PIPE: the relay paces the buffer itself (BufferDirective events update the
//   reported depth); the game's own stall logic on ReceiveTurns keeps the seats in
//   step exactly as Storm's did.
// - Leaves: a LeaveDirective arrives as an event the game applies at the relay-chosen
//   frame; the slot stops gating steps and EVENT_LEAVE fires with the relay's reason.
//
// The creator is slot 0 (SB's convention, which 1.17's lobby needs for slots, forces
// and kicks); the local slot comes from the signed token, never from the game.
#include "port_stormsb.h"
#include "../platform/port_log.h"  // PortLogger
#include "../data/port_stormerr.h"  // NET_*
#include "../data/port_stormapi.h"  // SStrCopy
#include "../platform/port_paths.h"  // PortCommandOptions
#include "../platform/port_util.h"  // PortNowMs
#include "../../h/Storm.h"  // SErrGetLastError
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#ifdef SB_NETCODE_OFF
// ---------------------------------------------------------------------------
// The stub provider (--no-sb): the bridge is not linked, so the SNet calls
// answer as if no relay session were set, and the Multiplayer screen lists the
// provider with a name that says the build carries no online play.
// ---------------------------------------------------------------------------

struct rp_session;

namespace {
const DWORD kCaps[9] = {0x24, 0, 0x1F0, 0x10, 8, 0x300000, 0, 0x1E, 2};
const PortLogger Log("hyperion_sb.log", PortLogger::SHARED);

BOOL Fail(DWORD dwErr)
{
  SErrSetLastError(dwErr);
  return FALSE;
}
}  // namespace

void SbSetLaunch(const char *pszSetupFile)
{
  Log("this build carries no SB netcode (--no-sb); the session \"%s\" is ignored",
      pszSetupFile ? pszSetupFile : "");
}
bool SbLaunchOn(void)
{
  return false;
}
int SbRosterCount(void)
{
  return 0;
}
bool SbInMatch(void)
{
  return false;
}
bool SbCreator(void)
{
  return false;
}

void SbListProvider(void *pfnCallback)
{
  reinterpret_cast<BOOL(__stdcall *)(DWORD, const char *, const char *, const void *)>(pfnCallback)(
      SB_PROVIDER, "Online (rally point)", "Not in this build: it was made with --no-sb.", kCaps);
}
BOOL SbInitialize(void)
{
  return TRUE;
}
void SbDestroy(void) {}
BOOL SbGetProviderCaps(DWORD *pdwCaps)
{
  if (pdwCaps[0] != sizeof kCaps)
    return Fail(NET_INVALID);
  memcpy(pdwCaps, kCaps, sizeof kCaps);
  return TRUE;
}
BOOL SbGetPlayerCaps(DWORD dwPlayer, DWORD *pdwCaps)
{
  return Fail(NET_BAD_PROVIDER);
}
BOOL SbRegisterEventHandler(DWORD dwEvent, void *pfnHandler)
{
  return TRUE;
}
BOOL SbCreateGame(const char *pszGameName, const char *pszPassword, const char *pszStats, DWORD dwMode,
                  const void *pvGameData, DWORD cbGameData, DWORD dwMaxPlayers, const char *pszPlayerName,
                  const char *pszPlayerDesc, DWORD *pdwPlayerId)
{
  return Fail(NET_CANNOT_JOIN);
}
BOOL SbEnumGames(void *pfnCallback, DWORD *pdwHintNext)
{
  if (pdwHintNext)
    *pdwHintNext = 0;
  return pfnCallback ? TRUE : Fail(NET_INVALID);
}
BOOL SbJoinGame(DWORD dwGameId, const char *pszPassword, const char *pszPlayerName, const char *pszPlayerDesc,
                DWORD *pdwPlayerId)
{
  return Fail(NET_CANNOT_JOIN);
}
BOOL SbLeaveGame(DWORD dwReason)
{
  return TRUE;
}
BOOL SbDropPlayer(DWORD dwPlayer, DWORD dwFlags)
{
  return Fail(NET_BAD_PROVIDER);
}
BOOL SbDisconnectAll(DWORD dwFlags)
{
  return FALSE;
}
BOOL SbGetNumPlayers(int *pnFirst, int *pnLast, int *pnActive)
{
  return FALSE;
}
BOOL SbGetPlayerName(DWORD dwPlayer, char *pszBuffer, DWORD cchBuffer)
{
  return FALSE;
}
BOOL SbGetPlayerNames(char **ppszNames)
{
  return TRUE;
}
BOOL SbGetGameInfo(DWORD dwInfo, void *pvBuffer, DWORD cbBuffer, DWORD *pcbRead)
{
  return FALSE;
}
BOOL SbSetGameMode(DWORD dwMode, DWORD dwMakePublic)
{
  return FALSE;
}
BOOL SbGetTurnsInTransit(DWORD *pdwTurns)
{
  return FALSE;
}
BOOL SbSendTurn(const void *pvData, DWORD cbData)
{
  return FALSE;
}
BOOL SbReceiveTurns(DWORD nFirst, DWORD nPlayers, const void **ppvData, DWORD *pcbData, DWORD *pdwFlags)
{
  return FALSE;
}
BOOL SbSendMessage(DWORD dwPlayer, const void *pvData, DWORD cbData)
{
  return FALSE;
}
BOOL SbReceiveMessage(int *pnPlayer, const void **ppvData, DWORD *pcbData)
{
  return FALSE;
}

#else

#include "../../../tools/rallypoint2/bridge/ctest/rp_bridge.h"

namespace {

const BYTE NO_PLAYER = 0xFF;
const DWORD TEXT_MAX = 0x80;

const DWORD TURN_PRESENT = 0x10000;
const DWORD TURN_READY = 0x20000;
const DWORD TURN_ANSWERING = 0x40000;  // 1.17's waiting dialog counts a player without it as not answering

const DWORD MODE_PASSWORD = 0x1;
const DWORD MODE_ONE_PLAYER = 0x2;
const DWORD MODE_STARTED = 0x4;
const DWORD MODE_UNDER_WAY = 0x8;  // 1.17 sets it as its countdown starts: no longer listed

const DWORD EVENT_INITDATA = 1, EVENT_JOIN = 2, EVENT_LEAVE = 3;
const DWORD LEAVE_LOST = 0x40000006;  // a connection lost, as 1.17 drops a player that stopped answering

const DWORD kPlayers = 8;

// A trace of every SNet call's outcome, for the first seconds of a session:
// HYPERION_SB_TRACE=1 turns it on (the game's net error paths leave on any
// unexpected return, and a stale Storm error reads as one).
const bool g_bTrace = getenv("HYPERION_SB_TRACE") != NULL;
const PortLogger Trace("hyperion_sb_trace.log", PortLogger::SHARED);

void TraceCall(const char *pszCall, bool bResult, bool bFailed)
{
  if (!g_bTrace)
    return;
  const DWORD dwErr = SErrGetLastError();
  Trace("%s -> %s%s%s", pszCall, bResult ? "TRUE" : "FALSE", bFailed ? ", error " : "",
        bFailed ? std::to_string(dwErr).c_str() : "");
}

// The provider's caps: Storm's message size less its header, 8 players, and the
// relay's buffer depth as the turns in transit, which the game's pacing reads.
const DWORD kCaps[9] = {0x24, 0, 0x1F0, 0x10, kPlayers, 0x300000, 0, 0x1E, 2};

struct Player {
  bool bIn = false;
  char szName[TEXT_MAX] = "";
  char szDesc[TEXT_MAX] = "";
  DWORD dwLatency = 0;
};

struct Event {
  DWORD dwType, dwPlayer;
  std::vector<uint8_t> data;
};

// One event as a handler takes it (h/net_bnet_msg.h's SNETEVENT).
struct EventRecord {
  DWORD dwType;
  DWORD dwPlayerId;
  void *pData;
  DWORD cbData;
};

// One game as SNetEnumGamesEx's callback takes it (h/net_glue.h's SNETGAMEREC).
struct GameRecord {
  DWORD dwUnused00;
  DWORD dwGameId;
  const char *pszGameName;
  const char *pszStatString;
  DWORD dwUnused10;
  BYTE bPlayers;
  BYTE abUnused15[3];
  BYTE bMaxPlayers;
  BYTE abUnused19[3];
};

// The room's launch: the setup JSON rp_start consumes (SbSetLaunch).
struct Launch {
  bool bOn = false;
  bool bStarted = false;
  bool bFailed = false;
  int nRoster = 0;  // the session's slots, from "rosterSize" in the JSON
  std::string setup;
} g_launch;

const PortLogger Log("hyperion_sb.log", PortLogger::SHARED);

bool g_bUp;
bool g_bCreator;
rp_session *g_session = NULL;
BYTE g_bLocal = NO_PLAYER;
Player g_players[kPlayers];
char g_szGameName[TEXT_MAX], g_szPassword[TEXT_MAX], g_szStats[TEXT_MAX];
DWORD g_dwMode, g_dwMaxPlayers;
std::vector<uint8_t> g_gameData;
char g_aszNames[kPlayers][TEXT_MAX];
DWORD g_nNames;
void(__stdcall *g_apfnEvent[8])(EventRecord *);
std::deque<Event> g_events;
// A turn of every in-game slot, in the relay's order: one front of each queue is
// a step, once every slot has one (the local turn is queued at SendTurn, the
// relay never echoes it back).
std::deque<std::vector<uint8_t>> g_slotTurns[kPlayers];
std::deque<std::pair<BYTE, std::vector<uint8_t>>> g_messages;  // chat, in arrival order
std::vector<uint8_t> g_turnsOut[kPlayers];                     // stable pointers handed to the game
std::vector<uint8_t> g_messageOut;
std::vector<uint8_t> g_pollBuf;  // rp_poll's scratch
DWORD g_dwSent, g_dwRead;        // this player's turns sent, and steps read
DWORD g_dwLatency = 2;           // the relay's current buffer depth (BufferDirective events)
const size_t kTurnSlack = 2048;  // zeros after a turn handed to the game (SbReceiveTurns)

BOOL Fail(DWORD dwErr)
{
  SErrSetLastError(dwErr);
  return FALSE;
}

bool InGame()
{
  return g_bUp && g_session != NULL && g_bLocal != NO_PLAYER;
}

bool IsPlayer(DWORD dwPlayer)
{
  return static_cast<BYTE>(dwPlayer) != NO_PLAYER && static_cast<BYTE>(dwPlayer) < kPlayers &&
         g_players[static_cast<BYTE>(dwPlayer)].bIn;
}

// A bare int for one JSON field: enough for the launch's "rosterSize", which the
// room (or the dev harness) writes beside the fields rp_start reads.
int JsonInt(const std::string &json, const char *pszKey, int nDefault)
{
  const std::string key = std::string("\"") + pszKey + "\"";
  const size_t at = json.find(key);
  if (at == std::string::npos)
    return nDefault;
  size_t p = json.find(':', at + key.size());
  if (p == std::string::npos)
    return nDefault;
  while (++p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n'))
    ;
  if (p >= json.size() || json[p] < '0' || json[p] > '9')
    return nDefault;
  int n = 0;
  while (p < json.size() && json[p] >= '0' && json[p] <= '9') {
    n = n * 10 + (json[p] - '0');
    ++p;
  }
  return n;
}

// A bare string for one JSON field, for the launch's "gameName" (the game's
// name as the chat room shows it, the same on every seat).
std::string JsonString(const std::string &json, const char *pszKey, const std::string &sDefault)
{
  const std::string key = std::string("\"") + pszKey + "\"";
  const size_t at = json.find(key);
  if (at == std::string::npos)
    return sDefault;
  size_t p = json.find(':', at + key.size());
  if (p == std::string::npos)
    return sDefault;
  while (++p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n'))
    ;
  if (p >= json.size() || json[p] != '"')
    return sDefault;
  size_t q = json.find('"', p + 1);
  if (q == std::string::npos)
    return sDefault;
  return json.substr(p + 1, q - p - 1);
}

// The session is gone: the relay is unreachable and the driver cannot resume, the
// slot was departed, or the game ended the session. The game plays on alone, as
// the LAN provider does when its relay goes.
void SessionGone()
{
  if (g_session == NULL)
    return;
  rp_end(g_session);
  g_session = NULL;
  Log("the relay session has ended; playing on alone");
}

// Every player's flags from the launch: none are answering until turns flow.
void Reset()
{
  g_session = NULL;
  g_bCreator = false;
  g_bLocal = NO_PLAYER;
  g_dwMode = g_dwMaxPlayers = 0;
  g_dwSent = g_dwRead = 0;
  g_dwLatency = 2;
  g_nNames = 0;
  for (DWORD i = 0; i < kPlayers; ++i) {
    g_players[i] = Player();
    g_slotTurns[i].clear();
  }
  g_events.clear();
  g_messages.clear();
  for (DWORD i = 0; i < kPlayers; ++i)
    g_turnsOut[i].clear();
  g_gameData.clear();
}

void Fire(Event e)
{
  EventRecord record = {e.dwType, e.dwPlayer, e.data.data(), static_cast<DWORD>(e.data.size())};
  if (g_apfnEvent[e.dwType])
    g_apfnEvent[e.dwType](&record);
}

void FireEvents()
{
  while (!g_events.empty()) {
    Fire(g_events.front());
    g_events.pop_front();
  }
}

void ReadU32(const uint8_t *&p, const uint8_t *end, uint32_t &out)
{
  if (p + 4 > end) {
    out = 0;
    p = end;
    return;
  }
  memcpy(&out, p, 4);
  p += 4;
}

// Drains the bridge into the provider's queues: peers' turns into their slots',
// leaves into events, buffer directives into the reported depth, chat into the
// message queue. Returns false when the session has ended.
bool Pump()
{
  if (g_session == NULL)
    return false;
  if (g_pollBuf.size() < 256 * 1024)
    g_pollBuf.resize(256 * 1024);
  const int32_t n = rp_poll(g_session, g_dwRead + 1, g_pollBuf.data(), g_pollBuf.size());
  if (n < 0) {
    // RP_ERR_TURN_ENDED (the driver's channels closed) or a poll buffer overflow.
    const int32_t st = rp_status(g_session);
    if (st != RP_STAT_RUNNING)
      SessionGone();
    return false;
  }
  const uint8_t *p = g_pollBuf.data();
  const uint8_t *end = p + n;
  while (p < end) {
    const uint8_t kind = *p++;
    uint32_t slot = 0, len = 0, depth = 0, reason = 0;
    uint64_t seq = 0;
    switch (kind) {
      case RP_EV_TURN:
        ReadU32(p, end, slot);
        memcpy(&seq, p, 8);
        p += 8;
        ReadU32(p, end, len);
        if (p + len > end)
          p = end;
        else if (slot < kPlayers && g_players[slot].bIn) {
          g_slotTurns[slot].push_back(std::vector<uint8_t>(p, p + len));
          p += len;
        } else
          p += len;
        break;
      case RP_EV_BUFFER:
        ReadU32(p, end, depth);
        if (depth != g_dwLatency) {
          Log("the relay set the latency buffer to %u turns", depth);
          g_dwLatency = depth;
        }
        break;
      case RP_EV_LEAVE:
        ReadU32(p, end, slot);
        ReadU32(p, end, reason);
        if (slot < kPlayers && g_players[slot].bIn) {
          g_players[slot].bIn = false;
          g_events.push_back({EVENT_LEAVE, slot,
                              std::vector<uint8_t>(reinterpret_cast<const uint8_t *>(&reason),
                                                   reinterpret_cast<const uint8_t *>(&reason) + 4)});
          Log("player %u left (reason 0x%X)", slot, reason);
        }
        break;
      case RP_EV_CHAT:
        ReadU32(p, end, slot);
        ReadU32(p, end, len);
        if (p + len > end)
          p = end;
        else {
          g_messages.push_back(std::make_pair(static_cast<BYTE>(slot), std::vector<uint8_t>(p, p + len)));
          p += len;
        }
        break;
      case RP_EV_LOBBY:
        ReadU32(p, end, slot);
        ReadU32(p, end, len);
        if (p + len <= end) {
          // The creator's framed launch blobs (kind byte + LE length + bytes):
          // 1 the init data (GetGameInfo(5), which 1.17's join reads), 2 the
          // stat string (the game list's entry, which its parser requires).
          // Kept only while none has arrived; the relay replays each exactly
          // once, so a repeat would only mean the framing is not ours.
          if (len >= 5) {
            const uint8_t kind = p[0];
            const uint32_t cb = static_cast<uint32_t>(p[1]) | (static_cast<uint32_t>(p[2]) << 8) |
                                (static_cast<uint32_t>(p[3]) << 16) | (static_cast<uint32_t>(p[4]) << 24);
            if (cb == len - 5) {
              if (kind == 1 && g_gameData.empty()) {
                g_gameData.assign(p + 5, p + len);
                Log("the game's init data arrived from slot %u (%u bytes)", slot, cb);
              } else if (kind == 2 && g_szStats[0] == '\0') {
                SStrCopy(g_szStats, reinterpret_cast<const char *>(p + 5), TEXT_MAX);
                Log("the game's stat string arrived from slot %u (%u bytes)", slot, cb);
              } else
                Log("lobby command from slot %u (%u bytes)", slot, len);
              p += len;
              break;
            }
          }
          Log("lobby command from slot %u (%u bytes)", slot, len);
          p += len;
        } else
          p = end;
        break;
      default:
        p = end;  // never happens; fail safe rather than loop
        break;
    }
  }
  FireEvents();
  return rp_status(g_session) == RP_STAT_RUNNING;
}

// A step is ready once every in-game slot has a turn queued.
bool StepReady()
{
  for (DWORD i = 0; i < g_dwMaxPlayers && i < kPlayers; ++i)
    if (g_players[i].bIn && g_slotTurns[i].empty())
      return false;
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// The launch and the front end's questions.

void SbSetLaunch(const char *pszSetupFile)
{
  g_launch = Launch();
  if (pszSetupFile == NULL)
    return;
  FILE *f = fopen(pszSetupFile, "rb");
  if (f == NULL) {
    Log("setup file \"%s\" cannot be opened", pszSetupFile);
    g_launch.bFailed = true;
    return;
  }
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  rewind(f);
  g_launch.setup.resize(static_cast<size_t>(n) + 1);
  if (n > 0 && fread(&g_launch.setup[0], 1, static_cast<size_t>(n), f) != static_cast<size_t>(n)) {
    Log("setup file \"%s\" cannot be read", pszSetupFile);
    fclose(f);
    g_launch.bFailed = true;
    return;
  }
  fclose(f);
  g_launch.setup[static_cast<size_t>(n)] = '\0';
  g_launch.bOn = true;
  g_launch.bStarted = false;
  g_launch.bFailed = false;
  g_launch.nRoster = JsonInt(g_launch.setup, "rosterSize", 0);
  Log("a relay session is set (\"%s\", %d slots)", pszSetupFile, g_launch.nRoster);
}

bool SbLaunchOn(void)
{
  return g_launch.bOn;
}

int SbRosterCount(void)
{
  return g_launch.nRoster;
}

bool SbInMatch(void)
{
  return g_bUp && g_bLocal != NO_PLAYER;
}

bool SbCreator(void)
{
  return g_bCreator;
}

// ---------------------------------------------------------------------------
// The SNet calls.

void SbListProvider(void *pfnCallback)
{
  reinterpret_cast<BOOL(__stdcall *)(DWORD, const char *, const char *, const void *)>(pfnCallback)(
      SB_PROVIDER, "Online (rally point)", "Play with others over the internet, through a relay.", kCaps);
}

BOOL SbInitialize(void)
{
  Reset();
  g_bUp = true;
  const char *pszSetup = PortCommandOptions()->szSbSetup;
  if (pszSetup[0])
    SbSetLaunch(pszSetup);
  return TRUE;
}

void SbDestroy(void)
{
  Log("SbDestroy");
  if (g_bLocal != NO_PLAYER && g_session != NULL)
    rp_leave_intent(g_session);  // a clean leave where there is one
  if (g_session != NULL) {
    rp_end(g_session);
    g_session = NULL;
  }
  Reset();
  g_nNames = 0;
  for (auto &pfn : g_apfnEvent)
    pfn = NULL;
  g_bUp = false;
}

BOOL SbGetProviderCaps(DWORD *pdwCaps)
{
  if (pdwCaps[0] != sizeof kCaps)
    return Fail(NET_INVALID);
  memcpy(pdwCaps, kCaps, sizeof kCaps);
  return TRUE;
}

BOOL SbGetPlayerCaps(DWORD dwPlayer, DWORD *pdwCaps)
{
  if (pdwCaps[0] != sizeof kCaps)
    return Fail(NET_INVALID);
  memset(pdwCaps + 1, 0, sizeof kCaps - 4);
  if (!InGame())
    return FALSE;
  if (!IsPlayer(dwPlayer))
    return Fail(NET_NO_PLAYER);
  memcpy(pdwCaps, kCaps, sizeof kCaps);
  pdwCaps[6] = static_cast<BYTE>(dwPlayer) == g_bLocal ? 0 : g_players[static_cast<BYTE>(dwPlayer)].dwLatency;
  return TRUE;
}

BOOL SbRegisterEventHandler(DWORD dwEvent, void *pfnHandler)
{
  if (!pfnHandler || dwEvent >= 8)
    return Fail(NET_INVALID);
  g_apfnEvent[dwEvent] = reinterpret_cast<void(__stdcall *)(EventRecord *)>(pfnHandler);
  return TRUE;
}

// Starts the session the launch names (rp_start dials the relay; bounded), then
// makes the local player and the roster the game reports. The creator is slot 0.
BOOL SbCreateGame(const char *pszGameName, const char *pszPassword, const char *pszStats, DWORD dwMode,
                  const void *pvGameData, DWORD cbGameData, DWORD dwMaxPlayers, const char *pszPlayerName,
                  const char *pszPlayerDesc, DWORD *pdwPlayerId)
{
  if (!g_bUp)
    return Fail(NET_BAD_PROVIDER);
  if (!g_launch.bOn || g_launch.bFailed) {
    Log("creator: no relay session is set (--sb-setup)");
    return Fail(NET_CANNOT_JOIN);
  }
  Reset();
  g_session = NULL;
  if (rp_start(g_launch.setup.c_str(), &g_session) != RP_OK || g_session == NULL) {
    Log("creator: the relay session could not start");
    return Fail(NET_CANNOT_JOIN);
  }
  const uint32_t dwSlot = rp_local_slot(g_session);
  g_bLocal = static_cast<BYTE>(dwSlot);
  g_bCreator = g_bLocal == 0;
  if (!g_bCreator) {
    // The room always names the creator seat first; a setup that is not slot 0
    // cannot host 1.17's lobby (its slots, forces and kicks are player 0's).
    Log("creator: this seat is slot %u, not the creator's; make the game on slot 0", dwSlot);
    rp_end(g_session);
    g_session = NULL;
    Reset();
    return Fail(NET_CANNOT_JOIN);
  }
  // The roster is the launch's (the room wrote it beside the setup JSON); every
  // slot the session names is in the game from the first step. A slot whose
  // client never connects simply stalls the step, as a missing player stalls
  // Storm's.
  g_dwMaxPlayers = g_launch.nRoster > 0 ? static_cast<DWORD>(g_launch.nRoster)
                                        : (dwMaxPlayers > kPlayers ? kPlayers : dwMaxPlayers);
  if (g_dwMaxPlayers < 1)
    g_dwMaxPlayers = 1;
  if (g_dwMaxPlayers > kPlayers)
    g_dwMaxPlayers = kPlayers;
  for (DWORD i = 0; i < g_dwMaxPlayers; ++i) {
    g_players[i].bIn = true;
    snprintf(g_players[i].szName, TEXT_MAX, "Player %u", static_cast<unsigned>(i + 1));
    snprintf(g_players[i].szDesc, TEXT_MAX, "player %u", static_cast<unsigned>(i));
  }
  DWORD dwFullMode = dwMode | MODE_STARTED;
  if (pszPassword && pszPassword[0])
    dwFullMode |= MODE_PASSWORD;
  if (dwMaxPlayers <= 1)
    dwFullMode |= MODE_ONE_PLAYER;
  g_dwMode = dwFullMode;
  const std::string szGameName = JsonString(g_launch.setup, "gameName", pszGameName);
  SStrCopy(g_szGameName, szGameName.c_str(), TEXT_MAX);
  SStrCopy(g_szPassword, pszPassword ? pszPassword : "", TEXT_MAX);
  SStrCopy(g_szStats, pszStats ? pszStats : "", TEXT_MAX);
  g_gameData.assign(static_cast<const uint8_t *>(pvGameData), static_cast<const uint8_t *>(pvGameData) + cbGameData);
  g_nNames = 0;
  *pdwPlayerId = g_bLocal;
  rp_game_started(g_session);
  // The init data and the stat string ride lobby commands: the relay replays
  // them to every member, including a joiner that was not connected when the
  // game was made. Framing: one kind byte, a little-endian length, the bytes.
  // Kind 1 is the game data (1.17's 0x20-byte game-type block, GetGameInfo(5));
  // kind 2 the stat string (the game list's entry, which the joiner's list
  // parser requires, net_glue.cpp's sub_475380).
  const auto SendBlob = [&](uint8_t kind, const uint8_t *pData, size_t cb) {
    if (cb == 0 || cb > 0xFFFF || g_session == NULL)
      return;
    std::vector<uint8_t> framed;
    framed.reserve(cb + 5);
    framed.push_back(kind);
    const uint32_t dwLen = static_cast<uint32_t>(cb);
    framed.push_back(static_cast<uint8_t>(dwLen));
    framed.push_back(static_cast<uint8_t>(dwLen >> 8));
    framed.push_back(static_cast<uint8_t>(dwLen >> 16));
    framed.push_back(static_cast<uint8_t>(dwLen >> 24));
    framed.insert(framed.end(), pData, pData + cb);
    const int rc = rp_lobby_send(g_session, framed.data(), framed.size());
    Log("the %s went to the relay (%u bytes, rc %d)", kind == 1 ? "init data" : "stat string",
        static_cast<unsigned>(framed.size()), rc);
  };
  SendBlob(1, g_gameData.data(), g_gameData.size());
  if (pszStats && pszStats[0])
    SendBlob(2, reinterpret_cast<const uint8_t *>(pszStats), strlen(pszStats));
  Log("creator: game \"%s\" for %u players, as %s, slot %u; the relay's buffer starts at %u turns", g_szGameName,
      g_dwMaxPlayers, pszPlayerName, static_cast<unsigned>(g_bLocal), rp_initial_buffer(g_session));
  return TRUE;
}

BOOL SbEnumGames(void *pfnCallback, DWORD *pdwHintNext)
{
  if (pdwHintNext)
    *pdwHintNext = 0;
  if (!pfnCallback)
    return Fail(NET_INVALID);
  // The session's game is listed from the launch, before the join: 1.17's Join
  // path finds it here, then SNetJoinGame starts the session. Its data is the
  // creator's init data, which the relay replays to this seat (Pump); the stat
  // string is the creator's, which the list's parser requires (sub_475380).
  if (g_bUp && g_launch.bOn) {
    const int nRoster = g_launch.nRoster > 0 ? g_launch.nRoster : 2;
    const std::string szName = JsonString(g_launch.setup, "gameName", "Relay");
    const char *pszStats = g_szStats[0] != '\0' ? g_szStats : "";
    GameRecord g = {0, 1, szName.c_str(), pszStats, 0, 1, {0, 0, 0}, static_cast<BYTE>(nRoster), {0, 0, 0}};
    reinterpret_cast<BOOL(__stdcall *)(const GameRecord *)>(pfnCallback)(&g);
  }
  return TRUE;
}

// Starts the session the launch names and waits for the creator's launch blobs
// (the init data and the stat string the relay replays on register). Shared by
// the join path, whose game list must show the game BEFORE SNetJoinGame runs
// (net_glue.cpp's list parser needs the stat string).
bool SbSessionStart(const char *pszWho)
{
  if (g_session != NULL)
    return true;  // already started (a join's second pass)
  if (!g_launch.bOn || g_launch.bFailed)
    return false;
  if (rp_start(g_launch.setup.c_str(), &g_session) != RP_OK || g_session == NULL) {
    Log("%s: the relay session could not start", pszWho);
    return false;
  }
  const uint64_t qwStart = PortNowMs();
  while ((g_gameData.empty() || g_szStats[0] == '\0') && PortNowMs() - qwStart < 5000) {
    Pump();
    if (rp_status(g_session) != RP_STAT_RUNNING)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (g_gameData.empty() || g_szStats[0] == '\0') {
    Log("%s: the launch blobs did not arrive in time", pszWho);
    rp_end(g_session);
    g_session = NULL;
    return false;
  }
  Log("%s: the session is running as slot %u", pszWho, rp_local_slot(g_session));
  return true;
}

BOOL SbJoinGame(DWORD dwGameId, const char *pszPassword, const char *pszPlayerName, const char *pszPlayerDesc,
                DWORD *pdwPlayerId)
{
  if (!g_bUp)
    return Fail(NET_BAD_PROVIDER);
  if (!SbSessionStart("joiner")) {
    Log("joiner: the relay session could not start");
    return Fail(NET_CANNOT_JOIN);
  }
  const uint32_t dwSlot = rp_local_slot(g_session);
  g_bLocal = static_cast<BYTE>(dwSlot);
  g_bCreator = g_bLocal == 0;
  // The roster mirrors the creator's: the launch's rosterSize, as the game's
  // lobby reports its max players on every seat.
  g_dwMaxPlayers = g_launch.nRoster > 0 && g_launch.nRoster <= static_cast<int>(kPlayers)
                       ? static_cast<DWORD>(g_launch.nRoster)
                       : kPlayers;
  if (g_dwMaxPlayers < 1)
    g_dwMaxPlayers = 1;
  for (DWORD i = 0; i < g_dwMaxPlayers; ++i) {
    g_players[i].bIn = true;
    snprintf(g_players[i].szName, TEXT_MAX, "Player %u", static_cast<unsigned>(i + 1));
    snprintf(g_players[i].szDesc, TEXT_MAX, "player %u", static_cast<unsigned>(i));
  }
  SStrCopy(g_szGameName, JsonString(g_launch.setup, "gameName", "").c_str(), TEXT_MAX);
  g_nNames = 0;
  *pdwPlayerId = g_bLocal;
  rp_game_started(g_session);
  Log("joiner: slot %u, in a game of %u players, as %s", static_cast<unsigned>(g_bLocal), g_dwMaxPlayers, pszPlayerName);
  return TRUE;
}

BOOL SbLeaveGame(DWORD dwReason)
{
  Log("SbLeaveGame(reason 0x%X)", static_cast<unsigned>(dwReason));
  if (!g_bUp)
    return Fail(NET_BAD_PROVIDER);
  if (g_session != NULL)
    rp_leave_intent(g_session);
  Reset();
  return TRUE;
}

BOOL SbDropPlayer(DWORD dwPlayer, DWORD dwFlags)
{
  if (!InGame())
    return FALSE;
  if (!IsPlayer(dwPlayer))
    return Fail(NET_NO_PLAYER);
  // The relay's LeaveDirective for the slot is the only truth of a drop; there is
  // no provider-side drop to send (a survivor's drop request is a platform UI
  // concern, wired with the room). Until then the step stalls on the slot.
  Log("drop asked for player %u; waiting on the relay's leave directive", static_cast<unsigned>(dwPlayer));
  return TRUE;
}

BOOL SbDisconnectAll(DWORD dwFlags)
{
  if (!InGame())
    return FALSE;
  return TRUE;
}

BOOL SbGetNumPlayers(int *pnFirst, int *pnLast, int *pnActive)
{
  if (!InGame())
    return FALSE;
  DWORD nFirst = kPlayers, nLast = 0, nActive = 0;
  for (DWORD i = 0; i < kPlayers; ++i)
    if (g_players[i].bIn) {
      if (i < nFirst)
        nFirst = i;
      if (i > nLast)
        nLast = i;
      ++nActive;
    }
  if (nActive == 0) {
    if (pnFirst)
      *pnFirst = 0;
    if (pnLast)
      *pnLast = 0;
    if (pnActive)
      *pnActive = 0;
    return TRUE;
  }
  if (pnFirst)
    *pnFirst = static_cast<int>(nFirst);
  if (pnLast)
    *pnLast = static_cast<int>(nLast);
  if (pnActive)
    *pnActive = static_cast<int>(nActive);
  return TRUE;
}

BOOL SbGetPlayerName(DWORD dwPlayer, char *pszBuffer, DWORD cchBuffer)
{
  if (!InGame())
    return FALSE;
  if (!IsPlayer(dwPlayer))
    return Fail(NET_NO_PLAYER);
  if (cchBuffer > 0) {
    SStrCopy(pszBuffer, g_players[static_cast<BYTE>(dwPlayer)].szName, cchBuffer);
  }
  return TRUE;
}

BOOL SbGetPlayerNames(char **ppszNames)
{
  if (!InGame())
    return FALSE;
  if (g_nNames == 0) {
    g_nNames = 0;
    for (DWORD i = 0; i < kPlayers && g_nNames < kPlayers; ++i)
      if (g_players[i].bIn)
        SStrCopy(g_aszNames[g_nNames++], g_players[i].szName, TEXT_MAX);
  }
  for (DWORD i = 0; i < g_nNames; ++i)
    ppszNames[i] = g_aszNames[i];
  return TRUE;
}

BOOL SbGetGameInfo(DWORD dwInfo, void *pvBuffer, DWORD cbBuffer, DWORD *pcbRead)
{
  if (pvBuffer && cbBuffer)
    memset(pvBuffer, 0, cbBuffer);
  if (pcbRead)
    *pcbRead = 0;
  if (!g_bUp)
    return Fail(NET_BAD_PROVIDER);
  const void *pvInfo;
  DWORD cbInfo;
  bool bString = false;
  DWORD dwPlayers = g_dwMaxPlayers;
  switch (dwInfo) {
  case 1: pvInfo = g_szGameName; cbInfo = static_cast<DWORD>(strlen(g_szGameName)) + 1; bString = true; break;
  case 2: pvInfo = g_szPassword; cbInfo = static_cast<DWORD>(strlen(g_szPassword)) + 1; bString = true; break;
  case 3: pvInfo = g_szStats; cbInfo = static_cast<DWORD>(strlen(g_szStats)) + 1; bString = true; break;
  case 4: pvInfo = &g_dwMode; cbInfo = 4; break;
  case 5: pvInfo = g_gameData.data(); cbInfo = static_cast<DWORD>(g_gameData.size()); break;
  case 6: pvInfo = &dwPlayers; cbInfo = 4; break;
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
  TraceCall("GetGameInfo", TRUE, false);
  return TRUE;
}

BOOL SbSetGameMode(DWORD dwMode, DWORD dwMakePublic)
{
  if (!InGame())
    return FALSE;
  g_dwMode = dwMode;
  return TRUE;
}

BOOL SbGetTurnsInTransit(DWORD *pdwTurns)
{
  if (!pdwTurns)
    return Fail(NET_INVALID);
  *pdwTurns = 0;
  if (!InGame())
    return FALSE;
  *pdwTurns = g_dwSent - g_dwRead;
  return TRUE;
}

BOOL SbSendTurn(const void *pvData, DWORD cbData)
{
  if (!pvData || !cbData) {
    TraceCall("SendTurn", FALSE, true);
    return Fail(NET_INVALID);
  }
  if (!InGame()) {
    TraceCall("SendTurn", FALSE, false);
    return FALSE;
  }
  Pump();
  std::vector<uint8_t> t(static_cast<const uint8_t *>(pvData), static_cast<const uint8_t *>(pvData) + cbData);
  const int rc = rp_submit_turn(g_session, g_dwSent + 1, t.data(), t.size());
  if (rc == RP_ERR_TURN_FULL) {
    TraceCall("SendTurn(full)", TRUE, false);
    // The driver's queue is full: the step stalls and the game retries, as
    // Storm's backpressure stalls it.
    return TRUE;
  }
  if (rc != RP_OK) {
    TraceCall("SendTurn", FALSE, true);
    SessionGone();
    return FALSE;
  }
  ++g_dwSent;
  g_slotTurns[g_bLocal].push_back(std::move(t));  // the relay never echoes our own turn
  TraceCall("SendTurn", TRUE, false);
  return TRUE;
}

BOOL SbReceiveTurns(DWORD nFirst, DWORD nPlayers, const void **ppvData, DWORD *pcbData, DWORD *pdwFlags)
{
  if (nPlayers) {
    if (ppvData)
      memset(ppvData, 0, nPlayers * sizeof(*ppvData));
    if (pcbData)
      memset(pcbData, 0, nPlayers * 4);
    if (pdwFlags)
      memset(pdwFlags, 0, nPlayers * 4);
  }
  if (!nPlayers || !ppvData || !pcbData || !pdwFlags) {
    TraceCall("ReceiveTurns", FALSE, true);
    return Fail(NET_INVALID);
  }
  if (!InGame()) {
    TraceCall("ReceiveTurns", FALSE, false);
    return FALSE;
  }
  Pump();
  if (!StepReady()) {
    // who the step waits for: present, and answering once a turn of theirs is in
    for (DWORD i = nFirst; i < nFirst + nPlayers && i < kPlayers; ++i)
      if (g_players[i].bIn) {
        DWORD dwFlags = TURN_PRESENT;
        if (i == g_bLocal || !g_slotTurns[i].empty())
          dwFlags |= TURN_ANSWERING;
        pdwFlags[i - nFirst] = dwFlags;
      }
    const BOOL b = Fail(NET_NOTHING_WAITING);
    TraceCall("ReceiveTurns", b, true);
    return b;
  }
  for (DWORD i = 0; i < kPlayers; ++i)
    if (g_players[i].bIn) {
      g_turnsOut[i] = std::move(g_slotTurns[i].front());
      g_slotTurns[i].pop_front();
      if (i == g_bLocal) {
        if (g_dwRead == 0)
          Log("the game's first step was read (lockstep is under way)");
        ++g_dwRead;
      }
      // zeros after the turn: the classic walker reads a cut-off last command whole
      // before it finds it cut off, and every seat reads the same
      g_turnsOut[i].resize(g_turnsOut[i].size() + kTurnSlack, 0);
    }
  for (DWORD i = nFirst; i < nFirst + nPlayers && i < kPlayers; ++i) {
    if (!g_players[i].bIn)
      continue;
    ppvData[i - nFirst] = g_turnsOut[i].data();
    pcbData[i - nFirst] = static_cast<DWORD>(g_turnsOut[i].size() - kTurnSlack);
    pdwFlags[i - nFirst] = TURN_PRESENT | TURN_READY | TURN_ANSWERING;
  }
  return TRUE;
}

BOOL SbSendMessage(DWORD dwPlayer, const void *pvData, DWORD cbData)
{
  if (!pvData || !cbData) {
    TraceCall("SendMessage", FALSE, true);
    return Fail(NET_INVALID);
  }
  if (!InGame()) {
    TraceCall("SendMessage", FALSE, false);
    return FALSE;
  }
  const int nTo = static_cast<int>(dwPlayer);
  if (nTo != -1 && nTo != -2 && !IsPlayer(dwPlayer)) {
    TraceCall("SendMessage", FALSE, true);
    return Fail(NET_NO_PLAYER);
  }
  if (g_session == NULL)
    return TRUE;
  // The relay fans chat to every member; the game's -1/-2 (all / allies) targets
  // are honoured by the receiver's own chat routing, so every line is sent as-is.
  rp_chat_send(g_session, static_cast<const uint8_t *>(pvData), cbData);
  TraceCall("SendMessage", TRUE, false);
  return TRUE;
}

BOOL SbReceiveMessage(int *pnPlayer, const void **ppvData, DWORD *pcbData)
{
  if (pnPlayer)
    *pnPlayer = -1;
  if (ppvData)
    *ppvData = NULL;
  if (pcbData)
    *pcbData = 0;
  if (!ppvData || !pcbData) {
    TraceCall("ReceiveMessage", FALSE, true);
    return Fail(NET_INVALID);
  }
  if (!InGame()) {
    TraceCall("ReceiveMessage", FALSE, false);
    return FALSE;
  }
  Pump();
  if (g_messages.empty()) {
    const BOOL b = Fail(NET_NOTHING_WAITING);
    TraceCall("ReceiveMessage", b, true);
    return b;
  }
  g_messageOut = std::move(g_messages.front().second);
  if (pnPlayer)
    *pnPlayer = g_messages.front().first;
  g_messages.pop_front();
  *ppvData = g_messageOut.data();
  *pcbData = static_cast<DWORD>(g_messageOut.size());
  return TRUE;
}

#endif // SB_NETCODE_OFF
