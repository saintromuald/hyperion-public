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
#include "port_sbmanifest.h"
#include "../platform/port_log.h"  // PortLogger
#include "../platform/port_json.h"  // PortJsonQuote
#include "../data/port_stormerr.h"  // NET_*
#include "../data/port_stormapi.h"  // SStrCopy
#include "../platform/port_paths.h"  // PortCommandOptions
#include "../platform/port_util.h"  // PortNowMs
#include "../../h/net_cmds.h"  // PlayerData
#include "../../h/pause.h"  // get_lobby_state
#include "../platform/port_globals117.h"  // oc_dwReplayFrame
#include "../../gen/globals117.h"  // sgbSyncTurnEnable
#include "../../h/Storm.h"  // SErrGetLastError
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
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
  SbManifestClear();
  Log("this build carries no SB netcode (--no-sb); the session \"%s\" is ignored",
      pszSetupFile ? pszSetupFile : "");
}
void SbSetLaunchJson(const char *pszSetupJson, const char *pszMapPath)
{
  (void)pszSetupJson;
  (void)pszMapPath;
  SbManifestClear();
  Log("this build carries no SB netcode (--no-sb); a launch is ignored");
}
}
bool SbLaunchOn(void)
{
  return false;
}
bool SbSessionStarted(void)
{
  return false;
}
void SbEmitLifecycleEvent(const char *pszType)
{
  (void)pszType;
}
int SbRosterCount(void)
{
  return 0;
}
bool SbSessionStart(const char *pszWho)
{
  Log("this build carries no SB netcode (--no-sb); SbSessionStart(\"%s\") cannot",
      pszWho ? pszWho : "");
  return false;
}
bool SbInMatch(void)
{
  return false;
}
bool SbCreator(void)
{
  return false;
}
bool SbMapFromLaunch(void)
{
  return false;
}
bool SbLaunchHeader(BYTE *pbSubCategory, BYTE *pbScenarioFlag, BYTE *pbLadderGame)
{
  (void)pbSubCategory;
  (void)pbScenarioFlag;
  (void)pbLadderGame;
  return false;
}
bool SbLaunchParams(void *pvParams, size_t cbParams)
{
  (void)pvParams;
  (void)cbParams;
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
  // The stub build carries no relay: nothing to ask.
  (void)dwPlayer;
  (void)dwFlags;
  return Fail(NET_BAD_PROVIDER);
}
BOOL SbDisconnectAll(DWORD dwFlags)
{
  return FALSE;
}
BOOL SbGetNumPlayers(int *pnFirst, int *pnLast, int *pnActive)
{
  if (pnFirst)
    *pnFirst = -1;
  if (pnLast)
    *pnLast = -1;
  if (pnActive)
    *pnActive = 0;
  return FALSE;
}
BOOL SbGetPlayerName(DWORD dwPlayer, char *pszBuffer, DWORD cchBuffer)
{
  (void)dwPlayer;
  if (!pszBuffer || !cchBuffer)
    return Fail(NET_INVALID);
  pszBuffer[0] = '\0';
  return Fail(NET_BAD_PROVIDER);
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
DWORD SbTurnsInTransitTarget(void)
{
  return 0;
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
BOOL SbReportGameResult(void)
{
  return Fail(NET_BAD_PROVIDER);
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
// The max message size bounds the map-download chunks the fd layer sends over
// SNet messages, so it stays under the relay's 64 KB control-frame cap (a
// chunk each message rides there).
const DWORD kCaps[9] = {0x24, 0, 0x1F0, 0x10, kPlayers, 0x4000, 0, 0x1E, 2};

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
  // Sidecar-only credential handoff: never serialized into the native manifest
  // or command line, and never logged. Empty for observers and untracked games.
  std::string resultCode;
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
// Canonical lobby commands have their own phase and channel ordering. Keeping
// their local echoes and the host's seed apart from real turns lets a seat that
// enters the game early queue turns without the other seat dispatching them in
// its lobby. Legacy launches keep their original single in-game queue.
std::deque<std::vector<uint8_t>> g_lobbyTurns[kPlayers];
// The players a step waits on: the local player from the start, and each peer
// the moment its first turn arrives — the LAN relay's semantics, where the
// creator's early steps form alone and a joiner joins the steps from its
// arrival. A peer's first turn also raises EVENT_JOIN, which starts 1.17's
// lobby handshake (the creator's state is 4 by then, from its own slot
// commands in its own early steps).
bool g_bInStep[kPlayers] = {false};
// Peers whose join event waits for the step their first turn rode in to be
// read: 1.17's join handler kicks a joiner when the creator's lobby state is
// not 4 yet, and the creator reaches 4 by processing its own early steps, so
// the event must trail the step, as the LAN relay's JOINED frame does.
std::deque<BYTE> g_pendingJoins;
std::deque<std::pair<BYTE, std::vector<uint8_t>>> g_messages;  // chat, in arrival order
std::vector<uint8_t> g_turnsOut[kPlayers];                     // stable pointers handed to the game
std::vector<uint8_t> g_messageOut;
std::vector<uint8_t> g_pollBuf;  // rp_poll's scratch
DWORD g_dwSent, g_dwRead;        // this player's in-game turns queued, and steps read
DWORD g_dwLobbySent, g_dwLobbyRead;  // the canonical lobby's independent pacing
struct PendingTurn {
  enum Route { TURN, LOBBY, LOCAL } route;
  std::vector<uint8_t> bytes;
};
std::deque<PendingTurn> g_pendingTurns;
std::deque<std::vector<uint8_t>> g_pendingLobby;
bool g_bGameStartedAnnounced;
bool g_bSessionStarted;
bool g_bSeedReceived;
bool g_bResultReported;
const size_t kPendingMost = 64;
const size_t kTurnMost = 16 * 1024;
const DWORD kBufferMost = 14;

DWORD ClampBufferDepth(DWORD dwDepth)
{
  return dwDepth < 1 ? 1 : dwDepth > kBufferMost ? kBufferMost : dwDepth;
}

// The LAN provider's per-step checksum trace (HYPERION_SB_TRACE), for lockstep
// forensics: a step's line shows each in-step player's turn as
// slot:bytes:checksum, so two seats' traces diverge at the first step their
// sims read differently.
static uint32_t StepChecksum(const std::vector<uint8_t> &v)
{
  uint32_t h = 0x811C9DC5u;
  for (const uint8_t b : v)
    h = (h ^ b) * 0x01000193u;
  return h;
}

bool CanonicalSeedRecord(const uint8_t *p, size_t cb)
{
  if (!SbManifestActive() || p == NULL || cb != 13 || p[0] != 0x48)
    return false;
  const uint32_t seed = SbManifestSeed();
  for (int i = 0; i < 4; ++i)
    if (p[1 + i] != static_cast<uint8_t>(seed >> (8 * i)))
      return false;
  for (int i = 5; i < 13; ++i)
    if (p[i] != 8)
      return false;
  return true;
}

DWORD g_dwLatency = 2;           // the relay's current buffer depth (BufferDirective events)
// When each peer's link last went down (0 while up), and when this seat last
// asked the relay for its synced drop. The relay stamps every member's link
// state, so a dead seat shows down here without the dead seat's cooperation;
// its drop requests below the hold's unlock floor are refused, so the ask is
// retried (the relay caps request rate, generously).
uint64_t g_qwLinkDown[kPlayers] = {0};
uint64_t g_qwDropAsked[kPlayers] = {0};
// Per-slot turns handed to the game (its reads): reported to the bridge, whose
// synced-leave tracker compares the relay's final_turn_count against these.
DWORD g_dwSlotConsumed[kPlayers] = {0};
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
  rp_end(&g_session);
  g_pendingTurns.clear();
  g_pendingLobby.clear();
  Log("the relay session has ended; no more turns can be sent");
}

// Every player's flags from the launch: none are answering until turns flow.
void Reset()
{
  if (g_session != NULL)
    rp_end(&g_session);
  g_session = NULL;
  g_bCreator = false;
  g_bLocal = NO_PLAYER;
  g_dwMode = g_dwMaxPlayers = 0;
  g_dwSent = g_dwRead = 0;
  g_dwLobbySent = g_dwLobbyRead = 0;
  g_dwLatency = 2;
  g_nNames = 0;
  for (DWORD i = 0; i < kPlayers; ++i) {
    g_players[i] = Player();
    g_slotTurns[i].clear();
    g_lobbyTurns[i].clear();
  }
  g_events.clear();
  g_messages.clear();
  for (DWORD i = 0; i < kPlayers; ++i)
    g_turnsOut[i].clear();
  g_gameData.clear();
  for (DWORD i = 0; i < kPlayers; ++i)
    g_bInStep[i] = false;
  g_pendingJoins.clear();
  g_pendingTurns.clear();
  g_pendingLobby.clear();
  g_bGameStartedAnnounced = false;
  g_bSessionStarted = false;
  g_bSeedReceived = false;
  g_bResultReported = false;
}

// Retry accepted-but-unsent turns in their original order. A full driver queue
// leaves the front pending; every other error fails the whole session instead
// of allowing a gap in lockstep. Canonical pregame traffic is local except for
// the host's authorized seed record, which uses the reliable lobby channel.
void FlushPendingTurns()
{
  while (g_session != NULL && !g_pendingTurns.empty()) {
    PendingTurn &pending = g_pendingTurns.front();
    int rc = RP_OK;
    if (pending.route == PendingTurn::TURN)
      rc = rp_submit_turn(g_session, oc_dwReplayFrame, pending.bytes.data(), pending.bytes.size());
    else if (pending.route == PendingTurn::LOBBY)
      rc = rp_lobby_send(g_session, pending.bytes.data(), pending.bytes.size());
    if (rc == RP_ERR_TURN_FULL)
      return;
    if (rc != RP_OK) {
      Log("submitting a queued turn failed (rc %d)", rc);
      SessionGone();
      return;
    }
    if (pending.route == PendingTurn::TURN) {
      ++g_dwSent;
      g_slotTurns[g_bLocal].push_back(std::move(pending.bytes));
    } else {
      ++g_dwLobbySent;
      g_lobbyTurns[g_bLocal].push_back(std::move(pending.bytes));
    }
    g_pendingTurns.pop_front();
  }
}

void FlushPendingLobby()
{
  while (g_session != NULL && !g_pendingLobby.empty()) {
    const std::vector<uint8_t> &message = g_pendingLobby.front();
    const int rc = rp_lobby_send(g_session, message.data(), message.size());
    if (rc == RP_ERR_TURN_FULL)
      return;
    if (rc != RP_OK) {
      Log("submitting a queued lobby message failed (rc %d)", rc);
      SessionGone();
      return;
    }
    g_pendingLobby.pop_front();
  }
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
  int32_t n = 0;
  for (;;) {
    n = rp_poll(g_session, oc_dwReplayFrame, g_pollBuf.data(), g_pollBuf.size());
    if (n != RP_ERR_TURN_FULL)
      break;
    // An event larger than the current buffer stays queued in the bridge. Grow
    // only until the bridge's maximum turn plus framing fits, then drain it.
    if (g_pollBuf.size() >= 1024 * 1024 + 64) {
      Log("a poll event exceeds the maximum supported size");
      SessionGone();
      return false;
    }
    g_pollBuf.resize((std::min)(g_pollBuf.size() * 2, static_cast<size_t>(1024 * 1024 + 64)));
  }
  if (n < 0) {
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
          if (slot != g_bLocal && !g_bInStep[slot]) {
            g_bInStep[slot] = true;
            if (!SbManifestActive()) {
              Log("player %u is in the game; its join event waits for the step", slot);
              g_pendingJoins.push_back(static_cast<BYTE>(slot));
            }
          }
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
          // The relay applied the drop at this step: the slot stops gating
          // steps from here on, and a join event still pending for it is moot.
          g_bInStep[slot] = false;
          g_slotTurns[slot].clear();
          g_lobbyTurns[slot].clear();
          g_pendingJoins.erase(std::remove(g_pendingJoins.begin(), g_pendingJoins.end(),
                                           static_cast<BYTE>(slot)),
                               g_pendingJoins.end());
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
      case RP_EV_CONNECT: {
        uint8_t up = 0;
        ReadU32(p, end, slot);
        if (p + 1 > end)
          p = end;
        else
          up = *p++;
        if (slot < kPlayers && slot != g_bLocal) {
          if (up) {
            if (g_qwLinkDown[slot] != 0) {
              Log("player %u's link is back", slot);
              g_qwLinkDown[slot] = 0;
            }
          } else if (g_qwLinkDown[slot] == 0) {
            Log("player %u's link went down", slot);
            g_qwLinkDown[slot] = PortNowMs();
          }
        }
        break;
      }
      case RP_EV_SESSION_START:
        ReadU32(p, end, depth);
        if (depth != 0)
          g_dwLatency = ClampBufferDepth(depth);
        if (!g_bSessionStarted)
          Log("every expected relay member is connected; the match may start");
        g_bSessionStarted = true;
        break;
      case RP_EV_LOBBY:
        ReadU32(p, end, slot);
        ReadU32(p, end, len);
        if (p + len <= end) {
          if (SbManifestActive()) {
            // The reliable lobby policy admits one record, from the host, for
            // this exact manifest seed. Everything else is dropped before the
            // native lobby handlers can see it. Replays after reconnect are
            // idempotent, just as they are in ShieldBattery's SC:R seam.
            if (slot == 0 && slot != g_bLocal && !g_bSeedReceived && CanonicalSeedRecord(p, len)) {
              g_bSeedReceived = true;
              g_lobbyTurns[0].push_back(std::vector<uint8_t>(p, p + len));
            }
            p += len;
            break;
          }
          // The creator's framed launch blobs (kind byte + LE length + bytes):
          // 1 the init data (GetGameInfo(5), which 1.17's join reads), 2 the
          // stat string (the game list's entry, which its parser requires).
          // Anything else is a game message (1.17's binary SNet messages ride
          // the same channel), queued for ReceiveMessage.
          if (len >= 5) {
            const uint8_t kind = p[0];
            const uint32_t cb = static_cast<uint32_t>(p[1]) | (static_cast<uint32_t>(p[2]) << 8) |
                                (static_cast<uint32_t>(p[3]) << 16) | (static_cast<uint32_t>(p[4]) << 24);
            if (cb == len - 5 && (kind == 1 || kind == 2)) {
              if (kind == 1 && g_gameData.empty()) {
                g_gameData.assign(p + 5, p + len);
                Log("the game's init data arrived from slot %u (%u bytes)", slot, cb);
              } else if (kind == 2 && g_szStats[0] == '\0') {
                // Bounded at the payload: the creator sends strlen bytes (the
                // string's trailing CR, no NUL), and SStrCopy would run past
                // the payload into the poll buffer's next events, which
                // Txt2GameData then reads as the string's tail (it requires
                // the byte after the map name's CR to be a NUL). The copy is
                // the payload alone, NUL-terminated.
                const size_t cbCopy = cb < TEXT_MAX - 1 ? cb : TEXT_MAX - 1;
                memcpy(g_szStats, p + 5, cbCopy);
                g_szStats[cbCopy] = '\0';
                Log("the game's stat string arrived from slot %u (%u bytes)", slot, cb);
              }
              p += len;
              break;
            }
          }
          g_messages.push_back(std::make_pair(static_cast<BYTE>(slot), std::vector<uint8_t>(p, p + len)));
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
  FlushPendingTurns();
  FlushPendingLobby();
  if (g_session == NULL)
    return false;
  // A peer whose link stays down past the grace period gets a drop request:
  // the relay's authority confirms it as a synced leave (EV_LEAVE) and the
  // game carries on with the remaining players. The grace tolerates a relay
  // blip and a short reconnect without shedding anyone. The relay refuses a
  // request until the target's hold stands past its unlock floor (30 s), so
  // the ask is retried every few seconds until the leave arrives.
  {
    const uint64_t qwNow = PortNowMs();
    for (DWORD i = 0; i < kPlayers; ++i) {
      if (i == g_bLocal || !g_players[i].bIn || g_qwLinkDown[i] == 0)
        continue;
      if (qwNow - g_qwLinkDown[i] >= 10000 && qwNow - g_qwDropAsked[i] >= 5000) {
        g_qwDropAsked[i] = qwNow;
        const int rc = rp_request_drop(g_session, static_cast<uint32_t>(i));
        Log("player %u's link has been down for %llu ms; the relay's synced drop was asked (rc %d)", i,
            static_cast<unsigned long long>(qwNow - g_qwLinkDown[i]), rc);
      }
    }
  }
  return rp_status(g_session) == RP_STAT_RUNNING;
}

// In-game steps wait for every active slot. A canonical pregame step is paced
// only by the local echo: quiet remote slots receive a keep-alive at dispatch,
// and the host's reliable seed record replaces it when one has arrived.
bool StepReady(bool bCanonicalLobby = false)
{
  for (DWORD i = 0; i < kPlayers; ++i) {
    const auto &queue = bCanonicalLobby ? g_lobbyTurns[i] : g_slotTurns[i];
    if (g_bInStep[i] && queue.empty() && (!bCanonicalLobby || i == g_bLocal))
      return false;
  }
  return true;
}

bool ResultReportPossible()
{
  return InGame() && SbManifestActive() && SbManifestLocalSlot() != NULL &&
         !g_launch.resultCode.empty() && !g_bResultReported;
}

void ExpectResultReport()
{
  // No-code seats must not arm the driver's leave hold: they have no report to
  // release it. Canonical players are armed before their first leave can race
  // the end-of-game report.
  if (ResultReportPossible())
    rp_result_expected(g_session);
}

const char *AssignedRaceJson(BYTE bRace)
{
  switch (bRace) {
    case 0: return "z";
    case 1: return "t";
    case 2: return "p";
    default: return NULL;
  }
}

// RawGameResultsReport, version 2. The relay authenticates the reporting slot;
// this document carries only the native evidence and the sidecar-issued code.
std::string RawResultJson()
{
  if (!ResultReportPossible())
    return std::string();
  const SbManifest &manifest = SbManifestGet();
  std::string players;
  for (DWORD i = 0; i < kPlayers; ++i) {
    const SbManifestSlot *slot = NULL;
    for (const SbManifestSlot &candidate : manifest.slots)
      if (candidate.bwPlayerId == i) {
        slot = &candidate;
        break;
      }
    // Humans come from the authenticated roster. Unmapped computer slots are
    // read directly, as ShieldBattery's raw reporter does.
    if (slot == NULL && gPlayerData[i].nType != 1)
      continue;
    const char *race = AssignedRaceJson(slot != NULL ? byte_526494[i + 12] : gPlayerData[i].nRace);
    if (race == NULL) {
      Log("the result report has no valid assigned race for BW slot %u", i);
      return std::string();
    }
    const DWORD nation = g_StormToNationID[i];
    BYTE victory = nation < 8 ? gbVictoryState[nation] : 0;
    if (victory == 5)  // BW's draw has no raw-v2 verdict; the server reconciles it.
      victory = 0;
    if (victory > 3)
      victory = 0;
    if (!players.empty())
      players += ",";
    players += "{\"userId\":";
    players += slot != NULL ? std::to_string(slot->userId) : "null";
    players += ",\"bwPlayerId\":" + std::to_string(i);
    players += ",\"stormId\":";
    players += slot != NULL ? std::to_string(slot->stormId) : "null";
    players += ",\"race\":\"" + std::string(race) + "\"";
    players += ",\"victoryState\":" + std::to_string(victory);
    players += ",\"alliances\":[";
    for (DWORD other = 0; other < kPlayers; ++other) {
      const DWORD otherNation = g_StormToNationID[other];
      const BYTE alliance = nation < 12 && otherNation < 12 ? sgbAlliances_g[nation][otherNation] : 0;
      players += (other != 0 ? "," : "") + std::to_string(alliance <= 2 ? alliance : 0);
    }
    players += "]}";
  }

  std::string netPlayers;
  for (DWORD i = 0; i < kPlayers; ++i) {
    if (!netPlayers.empty())
      netPlayers += ",";
    netPlayers += "{\"stormId\":" + std::to_string(i);
    netPlayers += ",\"wasDropped\":";
    netPlayers += byte_529042[i] != 0 ? "true" : "false";
    netPlayers += ",\"hasQuit\":";
    netPlayers += sgdwPlayerStatus_g[i] == 0 ? "true" : "false";
    netPlayers += "}";
  }

  const char *loseType = byte_529041 == 1 ? "targetedDisconnect"
                         : byte_529041 == 2 ? "massDisconnect" : NULL;
  const uint64_t timeMs = static_cast<uint64_t>(oc_dwReplayFrame) * 42;
  std::string json = "{\"version\":2,\"userId\":" + std::to_string(manifest.localUserId) +
                     ",\"resultCode\":" + PortJsonQuote(g_launch.resultCode) +
                     ",\"time\":" + std::to_string(timeMs) + ",\"players\":[" + players +
                     "],\"netPlayers\":[" + netPlayers + "],\"localPlayerLoseType\":";
  json += loseType != NULL ? PortJsonQuote(loseType) : "null";
  json += "}";
  // The relay's result control frame is deliberately small and bounded.
  if (json.empty() || json.size() > 4096) {
    Log("the raw result report is outside the relay's size limit");
    return std::string();
  }
  return json;
}

}  // namespace

// ---------------------------------------------------------------------------
// The launch and the front end's questions.

void SbSetLaunch(const char *pszSetupFile)
{
  g_launch = Launch();
  const char *pszResultCode = getenv("HYPERION_SB_RESULT_CODE");
  g_launch.resultCode = pszResultCode != NULL ? pszResultCode : "";
  SbManifestClear();
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
}

// The room's launch: the setup JSON arrives in the launch itself, not a file
// (SbSetLaunch's file form serves the command line's --sb-setup).
void SbSetLaunchJson(const char *pszSetupJson, const char *pszMapPath)
{
  g_launch = Launch();
  const char *pszResultCode = getenv("HYPERION_SB_RESULT_CODE");
  g_launch.resultCode = pszResultCode != NULL ? pszResultCode : "";
  SbManifestClear();
  if (pszSetupJson == NULL || pszSetupJson[0] == '\0') {
    Log("the launch's setup JSON is empty");
    g_launch.bFailed = true;
    return;
  }
  g_launch.setup = pszSetupJson;
  g_launch.bOn = true;
  std::string why;
  const bool manifest = SbManifestLoad(g_launch.setup, pszMapPath ? pszMapPath : "", &why);
  if (!why.empty()) {
    Log("the launch manifest was rejected: %s", why.c_str());
    g_launch.bFailed = true;
    return;
  }
  g_launch.nRoster = manifest ? static_cast<int>(SbManifestGet().slots.size())
                              : JsonInt(g_launch.setup, "rosterSize", 0);
  Log("a relay session is set (the launch's JSON, %d slots%s)", g_launch.nRoster,
      manifest ? ", canonical manifest" : "");
}

bool SbLaunchOn(void)
{
  return g_launch.bOn;
}

bool SbSessionStarted(void)
{
  return g_bSessionStarted;
}

void SbEmitLifecycleEvent(const char *pszType)
{
  static const bool bEvents = getenv("HYPERION_SB_EVENTS") != NULL;
  if (!bEvents || pszType == NULL)
    return;
  if (strcmp(pszType, "start") != 0 && strcmp(pszType, "finished") != 0)
    return;
  fprintf(stdout, "{\"type\":\"%s\"}\n", pszType);
  fflush(stdout);
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

bool SbMapFromLaunch(void)
{
  return g_launch.bOn;
}

bool SbLaunchHeader(BYTE *pbSubCategory, BYTE *pbScenarioFlag, BYTE *pbLadderGame)
{
  // the blob is the creator's header bytes from bGameType (+0x6D) on: the
  // subcategory at +8, the scenario flag at +0x0C, the ladder flag at +0x12
  if (g_gameData.size() < 0x13)
    return false;
  if (pbSubCategory)
    *pbSubCategory = g_gameData[8];
  if (pbScenarioFlag)
    *pbScenarioFlag = g_gameData[0x0C];
  if (pbLadderGame)
    *pbLadderGame = g_gameData[0x12];
  return true;
}

bool SbLaunchParams(void *pvParams, size_t cbParams)
{
  if (pvParams == NULL || cbParams == 0)
    return false;
  if (g_gameData.size() < cbParams)
    return false;
  memcpy(pvParams, g_gameData.data(), cbParams);
  return true;
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
  if (pszSetup[0] && !g_launch.bOn)
    SbSetLaunch(pszSetup);
  return TRUE;
}

void SbDestroy(void)
{
  Log("SbDestroy");
  if (g_bLocal != NO_PLAYER && g_session != NULL)
    rp_leave_intent(g_session);  // a clean leave where there is one
  if (g_session != NULL)
    rp_end(&g_session);
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
  g_dwLatency = ClampBufferDepth(rp_initial_buffer(g_session));
  const uint32_t dwSlot = rp_local_slot(g_session);
  const SbManifestSlot *local = SbManifestLocalSlot();
  if (dwSlot >= kPlayers || (SbManifestActive() &&
      (local == NULL || local->rpSlot != dwSlot || local->stormId != dwSlot))) {
    Log("creator: the relay slot %u does not match the launch identity", dwSlot);
    rp_end(&g_session);
    Reset();
    return Fail(NET_CANNOT_JOIN);
  }
  g_bLocal = static_cast<BYTE>(dwSlot);
  g_bCreator = SbManifestActive() ? local->host : g_bLocal == 0;
  if (!SbManifestActive() && !g_bCreator) {
    // A legacy room always names the creator seat first; a setup that is not
    // slot 0 cannot host 1.17's lobby (its slots, forces and kicks are player 0's).
    Log("creator: this seat is slot %u, not the creator's; make the game on slot 0", dwSlot);
    rp_end(&g_session);
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
    if (SbManifestActive()) {
      const SbManifestSlot &slot = SbManifestGet().slots[i];
      SStrCopy(g_players[i].szName, slot.name.c_str(), TEXT_MAX);
      snprintf(g_players[i].szDesc, TEXT_MAX, "player %u", static_cast<unsigned>(i));
      // The authenticated roster is complete before play: every participant
      // must supply a turn from the first in-game step on every seat.
      g_bInStep[i] = true;
    } else {
      snprintf(g_players[i].szName, TEXT_MAX, "Player %u", static_cast<unsigned>(i + 1));
      snprintf(g_players[i].szDesc, TEXT_MAX, "player %u", static_cast<unsigned>(i));
    }
  }
  DWORD dwFullMode = dwMode | MODE_STARTED;
  if (pszPassword && pszPassword[0])
    dwFullMode |= MODE_PASSWORD;
  if (dwMaxPlayers <= 1)
    dwFullMode |= MODE_ONE_PLAYER;
  g_dwMode = dwFullMode;
  const std::string szGameName = SbManifestActive() ? SbManifestGet().name
                                                     : JsonString(g_launch.setup, "gameName", pszGameName);
  SStrCopy(g_szGameName, szGameName.c_str(), TEXT_MAX);
  SStrCopy(g_szPassword, pszPassword ? pszPassword : "", TEXT_MAX);
  SStrCopy(g_szStats, pszStats ? pszStats : "", TEXT_MAX);
  g_gameData.assign(static_cast<const uint8_t *>(pvGameData), static_cast<const uint8_t *>(pvGameData) + cbGameData);
  g_nNames = 0;
  *pdwPlayerId = g_bLocal;
  g_bInStep[g_bLocal] = true;
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
  if (!SbManifestActive()) {
    SendBlob(1, g_gameData.data(), g_gameData.size());
    if (pszStats && pszStats[0])
      SendBlob(2, reinterpret_cast<const uint8_t *>(pszStats), strlen(pszStats));
  }
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
  g_dwLatency = ClampBufferDepth(rp_initial_buffer(g_session));
  if (SbManifestActive()) {
    const SbManifestSlot *local = SbManifestLocalSlot();
    const uint32_t slot = rp_local_slot(g_session);
    if (local == NULL || local->rpSlot != slot || local->stormId != slot) {
      Log("%s: the relay slot does not match the launch identity", pszWho);
      rp_end(&g_session);
      return false;
    }
    Log("%s: the canonical session is running as slot %u", pszWho, slot);
    return true;
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
    rp_end(&g_session);
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
  g_bInStep[g_bLocal] = true;
  Log("joiner: slot %u, in a game of %u players, as %s", static_cast<unsigned>(g_bLocal), g_dwMaxPlayers, pszPlayerName);
  return TRUE;
}

BOOL SbLeaveGame(DWORD dwReason)
{
  Log("SbLeaveGame(reason 0x%X)", static_cast<unsigned>(dwReason));
  if (!g_bUp)
    return Fail(NET_BAD_PROVIDER);
  if (g_session != NULL) {
    rp_leave_intent(g_session);
    rp_end(&g_session);
  }
  Reset();
  return TRUE;
}

BOOL SbDropPlayer(DWORD dwPlayer, DWORD dwFlags)
{
  if (!InGame())
    return FALSE;
  if (!IsPlayer(dwPlayer))
    return Fail(NET_NO_PLAYER);
  // The relay's LeaveDirective is the only truth of a drop; a request only asks
  // its authority. Until that answer arrives, the step continues to stall.
  const int rc = rp_request_drop(g_session, static_cast<uint32_t>(dwPlayer));
  Log("drop asked for player %u (rc %d); waiting on the relay's leave directive", static_cast<unsigned>(dwPlayer), rc);
  return rc == RP_OK ? TRUE : FALSE;
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

DWORD SbTurnsInTransitTarget(void)
{
  return InGame() ? g_dwLatency : 0;
}
BOOL SbGetTurnsInTransit(DWORD *pdwTurns)
{
  if (!pdwTurns)
    return Fail(NET_INVALID);
  *pdwTurns = 0;
  if (!InGame())
    return FALSE;
  const bool bCanonicalLobby = SbManifestActive() && sgbSyncTurnEnable == 0;
  const DWORD dwSent = bCanonicalLobby ? g_dwLobbySent : g_dwSent;
  const DWORD dwRead = bCanonicalLobby ? g_dwLobbyRead : g_dwRead;
  *pdwTurns = dwSent - dwRead;
  return TRUE;
}

BOOL SbSendTurn(const void *pvData, DWORD cbData)
{
  if (!pvData || !cbData || cbData > kTurnMost) {
    TraceCall("SendTurn", FALSE, true);
    return Fail(NET_INVALID);
  }
  if (!InGame()) {
    TraceCall("SendTurn", FALSE, false);
    return FALSE;
  }
  Pump();
  if (g_pendingTurns.size() >= kPendingMost) {
    Log("the outbound turn queue is full; ending the session rather than dropping a turn");
    SessionGone();
    return Fail(NET_INVALID);
  }
  PendingTurn::Route route = PendingTurn::TURN;
  if (SbManifestActive() && sgbSyncTurnEnable == 0) {
    route = g_bLocal == 0 && CanonicalSeedRecord(static_cast<const uint8_t *>(pvData), cbData)
                ? PendingTurn::LOBBY
                : PendingTurn::LOCAL;
  }
  g_pendingTurns.push_back({route, std::vector<uint8_t>(static_cast<const uint8_t *>(pvData),
                                                        static_cast<const uint8_t *>(pvData) + cbData)});
  FlushPendingTurns();
  TraceCall("SendTurn", g_session != NULL, g_session == NULL);
  return g_session != NULL ? TRUE : FALSE;
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
  const bool bCanonicalLobby = SbManifestActive() && sgbSyncTurnEnable == 0;
  if (!StepReady(bCanonicalLobby)) {
    // who the step waits for: present, and answering once a turn of theirs is in
    for (DWORD i = nFirst; i < nFirst + nPlayers && i < kPlayers; ++i)
      if (g_bInStep[i]) {
        DWORD dwFlags = TURN_PRESENT;
        const auto &queue = bCanonicalLobby ? g_lobbyTurns[i] : g_slotTurns[i];
        if (i == g_bLocal || !queue.empty())
          dwFlags |= TURN_ANSWERING;
        pdwFlags[i - nFirst] = dwFlags;
      }
    const BOOL b = Fail(NET_NOTHING_WAITING);
    TraceCall("ReceiveTurns", b, true);
    return b;
  }
  for (DWORD i = 0; i < kPlayers; ++i)
    if (g_bInStep[i]) {
      auto &queue = bCanonicalLobby ? g_lobbyTurns[i] : g_slotTurns[i];
      // Native accepts the host's seed record only in lobby state 8. Keep it queued and
      // send keepalives until then; consuming it earlier loses the only copy the provider
      // delivers and leaves the guest waiting forever.
      const bool bHoldSeed = bCanonicalLobby && i == 0 && i != g_bLocal && !queue.empty() &&
                             get_lobby_state() != 8;
      const bool bKeepAlive = bCanonicalLobby && i != g_bLocal && (queue.empty() || bHoldSeed);
      if (bKeepAlive) {
        g_turnsOut[i].assign(1, 0x05);
      } else {
        g_turnsOut[i] = std::move(queue.front());
        queue.pop_front();
      }
      if (!bCanonicalLobby) {
        ++g_dwSlotConsumed[i];
        rp_turn_consumed(g_session, static_cast<uint32_t>(i),
                         static_cast<uint64_t>(g_dwSlotConsumed[i]));
      }
      if (i == g_bLocal) {
        if (bCanonicalLobby) {
          ++g_dwLobbyRead;
        } else {
          if (g_dwRead == 0)
            Log("the game's first step was read (lockstep is under way)");
          else if (g_dwRead % 600 == 0)
            Log("the game has read %u steps", g_dwRead);
          ++g_dwRead;
        }
      }
      // zeros after the turn: the classic walker reads a cut-off last command whole
      // before it finds it cut off, and every seat reads the same
      g_turnsOut[i].resize(g_turnsOut[i].size() + kTurnSlack, 0);
    }
  {
    std::string line = std::string(bCanonicalLobby ? "lobby step " : "step ") +
                       std::to_string(bCanonicalLobby ? g_dwLobbyRead : g_dwRead) + ":";
    for (DWORD i = 0; i < kPlayers; ++i)
      if (g_bInStep[i]) {
        char sz[48];
        snprintf(sz, sizeof sz, " %u:%u:%08X", i, static_cast<unsigned>(g_turnsOut[i].size() - kTurnSlack), StepChecksum(g_turnsOut[i]));
        line += sz;
      }
    Trace("%s", line.c_str());
  }
  for (DWORD i = nFirst; i < nFirst + nPlayers && i < kPlayers; ++i) {
    if (!g_bInStep[i])
      continue;
    ppvData[i - nFirst] = g_turnsOut[i].data();
    pcbData[i - nFirst] = static_cast<DWORD>(g_turnsOut[i].size() - kTurnSlack);
    pdwFlags[i - nFirst] = TURN_PRESENT | TURN_READY | TURN_ANSWERING;
  }
  // the join events now: the game has processed this step (its own slot
  // commands included), so the creator's lobby state is what the join handler
  // expects
  for (const BYTE slot : g_pendingJoins)
    g_events.push_back({EVENT_JOIN, slot, {}});
  g_pendingJoins.clear();
  if (!g_bGameStartedAnnounced && sgbSyncTurnEnable != 0) {
    g_bGameStartedAnnounced = true;
    ExpectResultReport();
    rp_game_started(g_session);
    SbEmitLifecycleEvent("start");
    Log("the running game loop was reported to the relay");
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
  // A canonical roster is already installed locally. Native setup messages are
  // acknowledged but never sent: the reliable lobby channel carries only the
  // host's exact seed record through SbSendTurn.
  if (SbManifestActive())
    return TRUE;
  // The LAN's map transfer (opcode 0x4F) has no place in the SB flow: the map
  // is the launch's, and a partial transfer would leave a partial file that
  // the game might load. The chunks are dropped; every other message — 1.17's
  // raw binary packets (the lobby handshake's 0x4A state, the chat's own
  // framing) — rides the relay's binary lobby-command channel, not the text
  // chat channel; the relay fans each to every member.
  if (cbData > 0 && static_cast<const uint8_t *>(pvData)[0] == 0x4F)
    return TRUE;
  if (g_pendingLobby.size() >= kPendingMost) {
    Log("the outbound lobby queue is full; ending the session rather than dropping a message");
    SessionGone();
    return Fail(NET_INVALID);
  }
  g_pendingLobby.emplace_back(static_cast<const uint8_t *>(pvData),
                              static_cast<const uint8_t *>(pvData) + cbData);
  FlushPendingLobby();
  TraceCall("SendMessage", g_session != NULL, g_session == NULL);
  return g_session != NULL ? TRUE : FALSE;
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

BOOL SbReportGameResult(void)
{
  if (!g_bUp)
    return Fail(NET_BAD_PROVIDER);
  if (!InGame() || !SbManifestActive() || g_bResultReported)
    return TRUE;
  if (g_launch.resultCode.empty()) {
    // Observers and untracked games have no report to send and no leave hold.
    g_bResultReported = true;
    return TRUE;
  }
  ExpectResultReport();
  const std::string json = RawResultJson();
  if (json.empty())
    return Fail(NET_INVALID);
  rp_send_result(g_session, reinterpret_cast<const uint8_t *>(json.data()), json.size());
  g_bResultReported = true;
  Log("the raw v2 result report was queued (%u bytes)", static_cast<unsigned>(json.size()));
  return TRUE;
}

#endif // SB_NETCODE_OFF
