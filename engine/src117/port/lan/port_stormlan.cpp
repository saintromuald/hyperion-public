// port_stormlan.cpp -- Storm's LAN provider (docs/modules/lan.md): 1.17's multiplayer,
// its own lobby and lockstep, between Hyperions on one network, through the SNet calls the game
// already makes (port_stormnet.cpp hands them here once the provider is started). Presentation
// and transport only: what the game computes comes from its turns, as from Storm's.
//
// Every player is a connection to a match's relay (port_lanrelay.cpp): the room's when the room
// launched the game (LanSetLaunch, from port_lanlobby.cpp, says where it is and its token),
// else the creator's Hyperion's, which listens, answers the joiners' queries with an advert and
// connects to its relay like everyone else. The creator is Storm player 0, which 1.17's lobby
// needs for slots, forces and kicks; a joiner gets the lowest free id. When the relay goes, the
// turns sent it and in no step, and every turn after, become steps of this player alone, so the
// game plays on. Every 120 frames the game's state hash goes to the relay (port_synchash.cpp).
#include "port_stormlan.h"
#include "../platform/port_log.h"  // PortLogger
#include "../data/port_stormerr.h"  // NET_*
#include "../platform/port_util.h"  // PortNowMs
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include "../../../hyperion/version.h"  // HYPERION_VERSION_TEXT
#include "../../h/hxsim.h"              // HX_SIM_REVISION
#include "../../h/port_synchash.h"      // SyncHashCompute
#include "port_lannet.h"
#include "port_lanfeed.h"   // PortLanWatchHash
#include "port_lanproto.h"
#include "port_lanrelay.h"
#include "port_lanclient.h"  // LanClientPump
#include "port_lanlobby.h"  // PortLanGameFrame
#include "../data/port_stormapi.h"

using namespace LanProto;

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

// The provider's caps: Storm's message size less its header, 8 players, and 2 turns in
// transit, the least NetInit takes for a provider of the network.
const DWORD kCaps[9] = {0x24, 0, 0x1F0, 0x10, kPlayers, 0x300000, 0, 0x1E, 2};

struct Player {
  bool bIn = false;
  char szName[TEXT_MAX] = "";
  char szDesc[TEXT_MAX] = "";
  DWORD dwLatency = 0;
};

struct Step {
  uint32_t dwNumber = 0;
  std::vector<std::pair<BYTE, std::vector<uint8_t>>> turns;
  std::vector<std::pair<BYTE, DWORD>> leaves;
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

struct Found {
  uint32_t dwAddr;
  int nPort;
  std::string name, stats, version;
  BYTE bPlayers, bMax;
  DWORD dwMode;
  uint16_t wSimRevision;
  uint64_t qwSeen;
};

// The room's launch: the match's relay, its token and its game (LanSetLaunch).
struct Launch {
  bool bOn = false;
  uint32_t dwAddr = 0;
  int nPort = 0;
  std::string token, name, stats;
  uint32_t dwGameId = 0;
} g_launch;

bool g_bUp;
bool g_bCreator;
bool g_bAlone;  // the relay has gone: this player plays on alone
BYTE g_bLocal = NO_PLAYER;
int g_nRelayConn = -1;
Player g_players[kPlayers];
char g_szGameName[TEXT_MAX], g_szPassword[TEXT_MAX], g_szStats[TEXT_MAX];
DWORD g_dwMode, g_dwMaxPlayers, g_dwGameId;
std::vector<uint8_t> g_gameData;
char g_aszNames[kPlayers][TEXT_MAX];
DWORD g_nNames;
void(__stdcall *g_apfnEvent[8])(EventRecord *);
std::deque<Event> g_events;
std::deque<Step> g_steps;  // to be read here, in order
std::deque<std::pair<BYTE, std::vector<uint8_t>>> g_messages;
std::deque<std::vector<uint8_t>> g_mine;  // this player's turns sent and in no step yet
DWORD g_dwSent, g_dwRead;  // this player's turns sent, and steps read with one in it
BYTE g_bWaiting;           // the players the next step waits for, as the relay last said
std::vector<std::vector<uint8_t>> g_turnsOut;
const size_t kTurnSlack = 2048;  // zeros after a turn handed to the game (LanReceiveTurns)
std::vector<uint8_t> g_messageOut;
uint64_t g_qwLastQuery, g_qwLastAdvert;
std::map<DWORD, Found> g_found;
std::vector<GameRecord> g_records;
// A game made on 1.17's screens: its relay, here, and the connections not yet let in.
std::unique_ptr<LanRelay> g_relay;
std::vector<int> g_greeting;

// hyperion_lan.log: who joined, left and was refused, and how a game went.
const PortLogger Log("hyperion_lan.log", PortLogger::SHARED);

// HYPERION_LAN_TRACE=1: every step read, with its number and each turn's length and checksum,
// in hyperion_lan_steps.log, to compare what two machines read.
template <class... Args>
void Trace(const char *pszFormat, const Args &...args)
{
  static const PortLogger s_steps("hyperion_lan_steps.log", PortLogger::SHARED);
  static const bool s_bOn = getenv("HYPERION_LAN_TRACE") != NULL && getenv("HYPERION_LAN_TRACE")[0] == '1';
  if (s_bOn)
    s_steps(pszFormat, args...);
}

uint32_t Checksum(const std::vector<uint8_t> &v)
{
  uint32_t h = 2166136261u;
  for (uint8_t b : v)
    h = (h ^ b) * 16777619u;
  return h;
}

BOOL Fail(DWORD dwErr)
{
  SErrSetLastError(dwErr);
  return FALSE;
}

bool InGame(void)
{
  if (!g_bUp) {
    SErrSetLastError(NET_BAD_PROVIDER);
    return false;
  }
  if (g_bLocal == NO_PLAYER) {
    SErrSetLastError(NET_NOT_IN_GAME);
    return false;
  }
  return true;
}

bool IsPlayer(DWORD dwId)
{
  const BYTE b = static_cast<BYTE>(dwId);
  return b < kPlayers && g_players[b].bIn;
}

void QueueEvent(DWORD dwType, DWORD dwPlayer, const void *pv, size_t cb)
{
  Event e;
  e.dwType = dwType;
  e.dwPlayer = dwPlayer;
  e.data.assign(static_cast<const uint8_t *>(pv), static_cast<const uint8_t *>(pv) + cb);
  g_events.push_back(e);
}

void Fire(const Event &e)
{
  if (e.dwType >= 8 || g_apfnEvent[e.dwType] == NULL)
    return;
  std::vector<uint8_t> data = e.data;
  EventRecord r = {e.dwType, e.dwPlayer, data.empty() ? NULL : data.data(), static_cast<DWORD>(data.size())};
  g_apfnEvent[e.dwType](&r);
}

void FireEvents(void)
{
  while (!g_events.empty()) {
    const Event e = g_events.front();
    g_events.pop_front();
    Fire(e);
  }
}

void SetPlayer(BYTE bId, const char *pszName, const char *pszDesc)
{
  Player &p = g_players[bId];
  p.bIn = true;
  SStrCopy(p.szName, pszName, TEXT_MAX);
  SStrCopy(p.szDesc, pszDesc, TEXT_MAX);
  SStrCopy(g_aszNames[bId], pszName, TEXT_MAX);
  if (bId + 1u > g_nNames)
    g_nNames = bId + 1u;
}

int Count(void)
{
  int n = 0;
  for (const Player &p : g_players)
    n += p.bIn;
  return n;
}

void Reset(void)
{
  for (Player &p : g_players)
    p = Player();
  g_bCreator = g_bAlone = false;
  g_bLocal = NO_PLAYER;
  if (g_nRelayConn >= 0)
    LanNet::Drop(g_nRelayConn);
  g_nRelayConn = -1;
  g_szGameName[0] = g_szPassword[0] = g_szStats[0] = '\0';
  g_dwMode = g_dwMaxPlayers = g_dwGameId = 0;
  g_gameData.clear();
  g_events.clear();
  g_steps.clear();
  g_messages.clear();
  g_mine.clear();
  g_dwSent = g_dwRead = 0;
  g_bWaiting = 0;
  g_turnsOut.clear();
  g_messageOut.clear();
  if (g_relay) {
    g_relay.reset();
    for (int n : g_greeting)
      LanNet::Drop(n);
    g_greeting.clear();
    LanNet::CloseGameListener();
  }
}

// A player's hash at a frame: the frame, the random number's state and count, and each part's.
std::vector<uint8_t> HashRecord(void)
{
  SyncHash h;
  SyncHashCompute(&h);
  Writer w;
  w.D(h.dwFrame);
  w.D(h.dwRandState);
  w.D(h.dwRandCount);
  for (int d = 0; d < SYNC_DOMAINS; d++) {
    w.D(static_cast<uint32_t>(h.aqDomain[d]));
    w.D(static_cast<uint32_t>(h.aqDomain[d] >> 32));
  }
  return w.v;
}

// ---------------------------------------------------------------------------
// A game made on 1.17's screens: its relay, in this Hyperion.

void Advertise(void)
{
  if (!g_relay)
    return;
  Writer w;
  w.W(kProtocol);
  w.B(ADVERT_GAME);
  w.D(g_relay->GameId());
  w.W(static_cast<uint16_t>(LanNet::GamePort()));
  w.B(static_cast<uint8_t>(g_relay->Count()));
  w.B(static_cast<uint8_t>(g_relay->MaxPlayers()));
  w.D(g_relay->Mode());
  w.S(g_relay->Name());
  w.S(g_relay->Stats());
  w.S(HYPERION_VERSION_TEXT);
  w.W(HX_SIM_REVISION);
  // a game under way, or full, is not offered
  if (g_relay->UnderWay() || g_relay->Count() >= g_relay->MaxPlayers())
    LanNet::SetAdvert(std::vector<uint8_t>());
  else
    LanNet::SetAdvert(w.v);
}

void RelayPump(void)
{
  if (!g_relay)
    return;
  for (int n; (n = LanNet::Accept()) >= 0;)
    g_greeting.push_back(n);
  for (size_t k = 0; k < g_greeting.size();) {
    const int nConn = g_greeting[k];
    uint8_t bKind;
    std::vector<uint8_t> data;
    if (LanNet::Receive(nConn, &bKind, &data)) {
      g_greeting.erase(g_greeting.begin() + k);
      g_relay->Admit(nConn, bKind, data);
    } else if (!LanNet::Greeted(nConn, kGreetingMs)) {
      g_greeting.erase(g_greeting.begin() + k);
      LanNet::Drop(nConn);
    } else {
      ++k;
    }
  }
  g_relay->Pump();
  const uint64_t qwNow = PortNowMs();
  if (qwNow - g_qwLastAdvert >= 500) {
    g_qwLastAdvert = qwNow;
    Advertise();
  }
}

// ---------------------------------------------------------------------------
// This player, a connection to the relay.

void ReadStep(const std::vector<uint8_t> &data)
{
  Reader r(data);
  Step s;
  s.dwNumber = r.D();
  for (int n = r.B(); n > 0 && !r.bBad; --n) {
    const BYTE bId = r.B();
    s.turns.push_back(std::make_pair(bId, r.Bytes()));
    if (bId == g_bLocal && !g_mine.empty())
      g_mine.pop_front();
  }
  for (int n = r.B(); n > 0 && !r.bBad; --n) {
    const BYTE bId = r.B();
    s.leaves.push_back(std::make_pair(bId, r.D()));
  }
  if (!r.bBad)
    g_steps.push_back(s);
}

// The relay gone: the others with it. The turns sent it and in no step are made steps of this
// player alone, and so is every turn from now on.
void RelayLost(void)
{
  Log("player: the relay is gone; playing on alone");
  g_bAlone = true;
  Step first;
  for (int i = 0; i < kPlayers; ++i)
    if (i != g_bLocal && g_players[i].bIn)
      first.leaves.push_back(std::make_pair(static_cast<BYTE>(i), LEAVE_LOST));
  bool bFirst = true;
  for (std::vector<uint8_t> &t : g_mine) {
    Step s = bFirst ? first : Step();
    bFirst = false;
    s.turns.push_back(std::make_pair(g_bLocal, t));
    g_steps.push_back(s);
  }
  g_mine.clear();
  if (bFirst && !first.leaves.empty())
    g_steps.push_back(first);  // no turn in transit: the leaves come with the next one read
  if (g_nRelayConn >= 0)
    LanNet::Drop(g_nRelayConn);
  g_nRelayConn = -1;
}

void PlayerPump(void)
{
  if (g_nRelayConn < 0)
    return;
  uint8_t bKind;
  std::vector<uint8_t> data;
  while (g_nRelayConn >= 0 && LanNet::Receive(g_nRelayConn, &bKind, &data)) {
    Reader r(data);
    switch (bKind) {
    case JOINED: {
      const BYTE bId = r.B();
      const std::string name = r.S(), desc = r.S();
      if (!r.bBad && bId < kPlayers) {
        SetPlayer(bId, name.c_str(), desc.c_str());
        QueueEvent(EVENT_JOIN, bId, NULL, 0);
      }
      break;
    }
    case STEP: ReadStep(data); break;
    case MESSAGE: {
      const BYTE bFrom = r.B();
      if (!r.bBad)
        g_messages.push_back(std::make_pair(bFrom, std::vector<uint8_t>(data.begin() + 1, data.end())));
      break;
    }
    case STATUS:
      g_bWaiting = r.B();
      for (int i = 0; i < kPlayers; ++i)
        g_players[i].dwLatency = r.W();
      break;
    case PING: LanNet::Send(g_nRelayConn, PONG, data.data(), data.size()); break;
    case MODE: g_dwMode = r.D(); break;
    default: break;
    }
  }
  if (g_nRelayConn >= 0 && !LanNet::Alive(g_nRelayConn))
    RelayLost();
}

void Pump(void)
{
  LanNet::Pump();
  RelayPump();
  if (!g_bAlone)
    PlayerPump();
  LanClientPump();
}

// Connects to a relay with HELLO or CREATE and waits for its answer, pumping this Hyperion's
// own relay meanwhile; the WELCOME read in, or false with Storm's error set.
bool Enter(uint32_t dwAddr, int nPort, const Writer &hello, uint8_t bKind, DWORD dwGameId, const char *pszWho)
{
  const int nConn = LanNet::Connect(dwAddr, nPort, 3000);
  if (nConn < 0) {
    Log("%s: cannot connect to %s:%d", pszWho, LanNet::AddrText(dwAddr).c_str(), nPort);
    return Fail(NET_CANNOT_JOIN) != FALSE;
  }
  LanNet::Send(nConn, bKind, hello.v.data(), hello.v.size());
  // the relay's answer, waited for here: 1.17 reads the game's data as the join returns
  const uint64_t qwUntil = PortNowMs() + 5000;
  uint8_t bAnswer = 0;
  std::vector<uint8_t> data;
  bool bAnswered = false;
  while (PortNowMs() < qwUntil && LanNet::Alive(nConn)) {
    LanNet::Pump();
    RelayPump();
    if (LanNet::Receive(nConn, &bAnswer, &data)) {
      bAnswered = true;
      break;
    }
#if defined(__EMSCRIPTEN__)
    Sleep(2);  // the page's turn, which brings the answer
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
#endif
  }
  if (!bAnswered || bAnswer != WELCOME) {
    LanNet::Drop(nConn);
    Reader r(data);
    const DWORD dwErr = bAnswered && bAnswer == REFUSE ? r.D() : NET_CANNOT_JOIN;
    const std::string why = bAnswered && bAnswer == REFUSE ? r.S() : std::string();
    Log("%s: %s%s", pszWho, !bAnswered ? "no answer from the relay" : "refused: ", why.c_str());
    return Fail(dwErr) != FALSE;
  }
  Reader r(data);
  g_bLocal = r.B();
  g_dwMaxPlayers = r.D();
  g_dwMode = r.D();
  SStrCopy(g_szGameName, r.S().c_str(), TEXT_MAX);
  SStrCopy(g_szStats, r.S().c_str(), TEXT_MAX);
  SStrCopy(g_szPassword, r.S().c_str(), TEXT_MAX);
  g_gameData = r.Bytes();
  r.D();  // the step it starts at
  for (int n = r.B(); n > 0 && !r.bBad; --n) {
    const BYTE bId = r.B();
    const std::string name = r.S(), desc = r.S();
    if (bId < kPlayers)
      SetPlayer(bId, name.c_str(), desc.c_str());
  }
  if (r.bBad || g_bLocal >= kPlayers) {
    LanNet::Drop(nConn);
    g_bLocal = NO_PLAYER;
    return Fail(NET_CANNOT_JOIN) != FALSE;
  }
  g_nRelayConn = nConn;
  g_dwGameId = dwGameId;
  return true;
}

// HELLO's fields, which CREATE begins with.
Writer Hello(const char *pszName, const char *pszDesc, const char *pszPassword, DWORD dwGameId)
{
  Writer w;
  w.W(kProtocol);
  w.W(HX_SIM_REVISION);
  w.S(HYPERION_VERSION_TEXT);
  w.S(pszName);
  w.S(pszDesc ? pszDesc : "");
  w.S(pszPassword ? pszPassword : "");
  w.D(dwGameId);
  return w;
}

}  // namespace

// ---------------------------------------------------------------------------
// The room's launch, and the game's frame.

void LanSetLaunch(const char *pszAddr, int nPort, const char *pszToken, unsigned long dwGameId, const char *pszName,
                  const char *pszStats)
{
  g_launch = Launch();
  if (pszAddr == NULL)
    return;
  g_launch.bOn = LanNet::AddrParse(pszAddr, &g_launch.dwAddr);
  g_launch.nPort = nPort;
  g_launch.token = pszToken ? pszToken : "";
  g_launch.dwGameId = static_cast<uint32_t>(dwGameId);
  g_launch.name = pszName ? pszName : "";
  g_launch.stats = pszStats ? pszStats : "";
}

bool LanLaunchOn(void)
{
  return g_launch.bOn;
}

bool LanInMatch(void)
{
  return g_bUp && g_bLocal != NO_PLAYER;
}

bool LanCreator(void)
{
  return g_bCreator;
}

// The game's frame (gameloop.cpp), in a game over the network: the state hash, every
// kHashEvery frames.
void LanGameFrame(unsigned long dwFrame)
{
  // a LAN match watched: its hash to the room, held against the players' (port_lanfeed.cpp)
  if (dwFrame % kHashEvery == 0 && PortLanWatchShown())
    PortLanWatchHash(HashRecord());
  if (!g_bUp || g_bLocal == NO_PLAYER)
    return;
  // a room's match: the room told how far the game has got, and its end if the host ends it
  PortLanGameFrame(static_cast<uint32_t>(dwFrame));
  if (g_bAlone || g_nRelayConn < 0 || dwFrame % kHashEvery != 0)
    return;
  const std::vector<uint8_t> record = HashRecord();
  LanNet::Send(g_nRelayConn, HASH, record.data(), record.size());
}

// ---------------------------------------------------------------------------
// The SNet calls.

void LanListProvider(void *pfnCallback)
{
  reinterpret_cast<BOOL(__stdcall *)(DWORD, const char *, const char *, const void *)>(pfnCallback)(
      LAN_PROVIDER, "Local Area Network", "Play with others on this network: one hosts, the others join.", kCaps);
}

BOOL LanInitialize(void)
{
  if (!LanNet::Startup())
    return Fail(NET_BAD_PROVIDER);
  Reset();
  g_found.clear();
  g_bUp = true;
  return TRUE;
}

void LanDestroy(void)
{
  if (g_bLocal != NO_PLAYER && g_nRelayConn >= 0) {
    Writer w;
    w.D(LEAVE_LOST);
    LanNet::Send(g_nRelayConn, LEAVE, w.v.data(), w.v.size());
  }
  Reset();
  g_nNames = 0;
  g_found.clear();
  for (auto &pfn : g_apfnEvent)
    pfn = NULL;
  g_bUp = false;
}

BOOL LanGetProviderCaps(DWORD *pdwCaps)
{
  if (pdwCaps[0] != sizeof kCaps)
    return Fail(NET_INVALID);
  memcpy(pdwCaps, kCaps, sizeof kCaps);
  return TRUE;
}

BOOL LanGetPlayerCaps(DWORD dwPlayer, DWORD *pdwCaps)
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

BOOL LanRegisterEventHandler(DWORD dwEvent, void *pfnHandler)
{
  if (!pfnHandler || dwEvent >= 8)
    return Fail(NET_INVALID);
  g_apfnEvent[dwEvent] = reinterpret_cast<void(__stdcall *)(EventRecord *)>(pfnHandler);
  return TRUE;
}

BOOL LanCreateGame(const char *pszGameName, const char *pszPassword, const char *pszStats, DWORD dwMode,
                   const void *pvGameData, DWORD cbGameData, DWORD dwMaxPlayers, const char *pszPlayerName,
                   const char *pszPlayerDesc, DWORD *pdwPlayerId)
{
  if (!g_bUp)
    return Fail(NET_BAD_PROVIDER);
  Reset();
  uint32_t dwAddr;
  int nPort;
  std::string relayPassword;
  DWORD dwGameId;
  if (g_launch.bOn) {
    dwAddr = g_launch.dwAddr;
    nPort = g_launch.nPort;
    relayPassword = g_launch.token;
    dwGameId = g_launch.dwGameId;
  } else {
    // 1.17's own screens: the relay here, joined over the loopback
    if (!LanNet::Listen()) {
      Log("creator: cannot listen");
      return Fail(NET_CANNOT_JOIN);
    }
    std::random_device rd;
    do
      dwGameId = rd();
    while (dwGameId == 0);
    g_relay.reset(new LanRelay(dwGameId, pszPassword ? pszPassword : "", "relay"));
    LanNet::AddrParse("127.0.0.1", &dwAddr);
    nPort = LanNet::GamePort();
    relayPassword = pszPassword ? pszPassword : "";
  }
  DWORD dwFullMode = dwMode | MODE_STARTED;
  if (pszPassword && pszPassword[0])
    dwFullMode |= MODE_PASSWORD;
  if (dwMaxPlayers <= 1)
    dwFullMode |= MODE_ONE_PLAYER;
  Writer w = Hello(pszPlayerName, pszPlayerDesc, relayPassword.c_str(), dwGameId);
  w.S(pszGameName);
  w.S(pszStats);
  w.S(pszPassword ? pszPassword : "");
  w.D(dwFullMode);
  w.D(dwMaxPlayers > static_cast<DWORD>(kPlayers) ? kPlayers : dwMaxPlayers);
  w.Bytes(pvGameData, pvGameData ? cbGameData : 0);
  g_nNames = 0;
  if (!Enter(dwAddr, nPort, w, CREATE, dwGameId, "creator")) {
    Reset();
    return FALSE;
  }
  g_bCreator = true;
  *pdwPlayerId = g_bLocal;
  Advertise();
  Log("creator: game \"%s\" for %u players, as %s; mode 0x%X%s%s", g_szGameName, g_dwMaxPlayers, pszPlayerName,
      g_dwMode, g_szPassword[0] ? ", with a password" : "", g_launch.bOn ? ", the room's" : "");
  return TRUE;
}

BOOL LanEnumGames(void *pfnCallback, DWORD *pdwHintNext)
{
  if (!g_bUp)
    return Fail(NET_BAD_PROVIDER);
  g_records.clear();
  if (g_launch.bOn) {
    // the room's match is the one game there is
    GameRecord g;
    memset(&g, 0, sizeof g);
    g.dwGameId = g_launch.dwGameId;
    g.pszGameName = g_launch.name.c_str();
    g.pszStatString = g_launch.stats.c_str();
    g.bPlayers = 1;
    g.bMaxPlayers = kPlayers;
    g_records.push_back(g);
  } else {
    const uint64_t qwNow = PortNowMs();
    LanNet::Pump();
    for (const LanNet::Answer &a : LanNet::Answers()) {
      Reader r(a.advert);
      if (r.W() != kProtocol || r.B() != ADVERT_GAME)
        continue;
      Found f;
      const DWORD dwId = r.D();
      f.dwAddr = a.dwAddr;
      f.nPort = r.W();
      f.bPlayers = r.B();
      f.bMax = r.B();
      f.dwMode = r.D();
      f.name = r.S();
      f.stats = r.S();
      f.version = r.S();
      f.wSimRevision = r.W();
      f.qwSeen = qwNow;
      if (!r.bBad && dwId != 0)
        g_found[dwId] = f;
    }
    if (qwNow - g_qwLastQuery >= 500) {
      g_qwLastQuery = qwNow;
      LanNet::Query();
    }
    // the games heard from lately; one of another version is not offered
    g_records.reserve(g_found.size());
    for (auto it = g_found.begin(); it != g_found.end();) {
      if (qwNow - it->second.qwSeen > 4000) {
        it = g_found.erase(it);
        continue;
      }
      const Found &f = it->second;
      if (f.version == HYPERION_VERSION_TEXT && f.wSimRevision == HX_SIM_REVISION) {
        GameRecord g;
        memset(&g, 0, sizeof g);
        g.dwGameId = it->first;
        g.pszGameName = f.name.c_str();
        g.pszStatString = f.stats.c_str();
        g.bPlayers = f.bPlayers;
        g.bMaxPlayers = f.bMax;
        g_records.push_back(g);
      }
      ++it;
    }
  }
  for (GameRecord &g : g_records)
    reinterpret_cast<BOOL(__stdcall *)(const GameRecord *)>(pfnCallback)(&g);
  if (pdwHintNext)
    *pdwHintNext = 0;
  return TRUE;
}

BOOL LanJoinGame(DWORD dwGameId, const char *pszPassword, const char *pszPlayerName, const char *pszPlayerDesc,
                 DWORD *pdwPlayerId)
{
  *pdwPlayerId = static_cast<DWORD>(-1);
  if (!g_bUp)
    return Fail(NET_BAD_PROVIDER);
  uint32_t dwAddr;
  int nPort;
  std::string password = pszPassword ? pszPassword : "";
  if (g_launch.bOn && dwGameId == g_launch.dwGameId) {
    dwAddr = g_launch.dwAddr;
    nPort = g_launch.nPort;
    password = g_launch.token;
  } else {
    auto it = g_found.find(dwGameId);
    if (it == g_found.end()) {
      Log("joiner: no game 0x%X heard of", dwGameId);
      return Fail(NET_CANNOT_JOIN);
    }
    dwAddr = it->second.dwAddr;
    nPort = it->second.nPort;
  }
  Reset();
  g_nNames = 0;
  if (!Enter(dwAddr, nPort, Hello(pszPlayerName, pszPlayerDesc, password.c_str(), dwGameId), HELLO, dwGameId,
             "joiner")) {
    Reset();
    return FALSE;
  }
  *pdwPlayerId = g_bLocal;
  Log("joiner: joined \"%s\" as player %d, %u bytes of game data", g_szGameName, g_bLocal,
      static_cast<unsigned>(g_gameData.size()));
  // the game's data, as 1.17 takes it inside the join
  Event e = {EVENT_INITDATA, 0, g_gameData};
  Fire(e);
  return TRUE;
}

BOOL LanLeaveGame(DWORD dwReason)
{
  if (!g_bUp)
    return Fail(NET_BAD_PROVIDER);
  if (g_bLocal != NO_PLAYER && g_nRelayConn >= 0) {
    Writer w;
    w.D(dwReason);
    LanNet::Send(g_nRelayConn, LEAVE, w.v.data(), w.v.size());
    LanNet::Pump();
  }
  Reset();
  return TRUE;
}

BOOL LanDropPlayer(DWORD dwPlayer, DWORD dwFlags)
{
  if (!g_bUp)
    return Fail(NET_BAD_PROVIDER);
  if (static_cast<BYTE>(dwPlayer) == NO_PLAYER || static_cast<BYTE>(dwPlayer) == g_bLocal)
    return Fail(NET_INVALID);
  if (g_bLocal == NO_PLAYER)
    return Fail(NET_NOT_IN_GAME);
  if (!IsPlayer(dwPlayer))
    return Fail(NET_NO_PLAYER);
  if (g_nRelayConn >= 0) {
    Writer w;
    w.B(static_cast<BYTE>(dwPlayer));
    w.D(dwFlags);
    LanNet::Send(g_nRelayConn, DROP, w.v.data(), w.v.size());
  }
  return TRUE;
}

BOOL LanDisconnectAll(DWORD dwFlags)
{
  if (!InGame())
    return FALSE;
  if (g_bAlone)
    return TRUE;
  if (g_bCreator && g_nRelayConn >= 0) {
    for (int i = 0; i < kPlayers; ++i)
      if (i != g_bLocal && g_players[i].bIn) {
        Writer w;
        w.B(static_cast<BYTE>(i));
        w.D(dwFlags);
        LanNet::Send(g_nRelayConn, DROP, w.v.data(), w.v.size());
      }
  } else {
    RelayLost();
  }
  return TRUE;
}

BOOL LanGetNumPlayers(int *pnFirst, int *pnLast, int *pnActive)
{
  if (pnFirst)
    *pnFirst = -1;
  if (pnLast)
    *pnLast = -1;
  if (pnActive)
    *pnActive = 0;
  if (!InGame())
    return FALSE;
  int nFirst = -1, nLast = -1;
  for (int i = 0; i < kPlayers; ++i)
    if (g_players[i].bIn) {
      if (nFirst < 0)
        nFirst = i;
      nLast = i;
    }
  if (pnFirst)
    *pnFirst = nFirst;
  if (pnLast)
    *pnLast = nLast;
  if (pnActive)
    *pnActive = Count();
  return TRUE;
}

BOOL LanGetPlayerName(DWORD dwPlayer, char *pszBuffer, DWORD cchBuffer)
{
  if (!pszBuffer || !cchBuffer)
    return Fail(NET_INVALID);
  pszBuffer[0] = '\0';
  if (!InGame())
    return FALSE;
  if (!IsPlayer(dwPlayer))
    return Fail(NET_NO_PLAYER);
  SStrCopy(pszBuffer, g_players[static_cast<BYTE>(dwPlayer)].szName, cchBuffer);
  return TRUE;
}

BOOL LanGetPlayerNames(char **ppszNames)
{
  for (DWORD i = 0; i < g_nNames && i < static_cast<DWORD>(kPlayers); ++i)
    ppszNames[i] = g_aszNames[i];
  return TRUE;
}

BOOL LanGetGameInfo(DWORD dwInfo, void *pvBuffer, DWORD cbBuffer, DWORD *pcbRead)
{
  if (pvBuffer && cbBuffer)
    memset(pvBuffer, 0, cbBuffer);
  if (pcbRead)
    *pcbRead = 0;
  if (!InGame())
    return FALSE;
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
  return TRUE;
}

BOOL LanSetGameMode(DWORD dwMode, DWORD dwMakePublic)
{
  if (!g_bUp)
    return Fail(NET_INVALID);
  if (g_bLocal == NO_PLAYER)
    return Fail(NET_NOT_IN_GAME);
  if (!g_bCreator)
    return Fail(NET_NOT_OWNER);
  if (dwMakePublic) {
    dwMode &= ~MODE_PASSWORD;
    g_szPassword[0] = '\0';
  }
  g_dwMode = dwMode;
  if (g_nRelayConn >= 0) {
    Writer w;
    w.D(g_dwMode);
    LanNet::Send(g_nRelayConn, MODE, w.v.data(), w.v.size());
  }
  return TRUE;
}

BOOL LanGetTurnsInTransit(DWORD *pdwTurns)
{
  if (!pdwTurns)
    return Fail(NET_INVALID);
  *pdwTurns = 0;
  if (!InGame())
    return FALSE;
  *pdwTurns = g_dwSent - g_dwRead;
  return TRUE;
}

BOOL LanSendTurn(const void *pvData, DWORD cbData)
{
  if (!pvData || !cbData)
    return Fail(NET_INVALID);
  if (!InGame())
    return FALSE;
  std::vector<uint8_t> t(static_cast<const uint8_t *>(pvData), static_cast<const uint8_t *>(pvData) + cbData);
  Trace("send %u: %u:%08X", g_dwSent, static_cast<unsigned>(t.size()), Checksum(t));
  ++g_dwSent;
  if (g_bAlone) {
    Step s;
    s.turns.push_back(std::make_pair(g_bLocal, t));
    g_steps.push_back(s);
  } else {
    g_mine.push_back(t);
    LanNet::Send(g_nRelayConn, TURN, t.data(), t.size());
    // this Hyperion's own relay puts the step together at once
    if (g_relay) {
      LanNet::Pump();
      RelayPump();
    }
  }
  return TRUE;
}

BOOL LanReceiveTurns(DWORD nFirst, DWORD nPlayers, const void **ppvData, DWORD *pcbData, DWORD *pdwFlags)
{
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
  Pump();
  FireEvents();
  if (g_steps.empty()) {
    // who the step waits for: present, and answering unless the relay says it waits for them
    for (DWORD i = nFirst; i < nFirst + nPlayers && i < static_cast<DWORD>(kPlayers); ++i)
      if (g_players[i].bIn) {
        DWORD dwFlags = TURN_PRESENT;
        if (i == g_bLocal || !(g_bWaiting & (1 << i)))
          dwFlags |= TURN_ANSWERING;
        pdwFlags[i - nFirst] = dwFlags;
      }
    return Fail(NET_NOTHING_WAITING);
  }
  g_turnsOut.clear();
  Step s = g_steps.front();
  g_steps.pop_front();
  // the players gone before this step: 1.17 acts on the reason after this read
  for (const auto &l : s.leaves)
    if (l.first < kPlayers && g_players[l.first].bIn) {
      const DWORD dwReason = l.second;
      Event e = {EVENT_LEAVE, l.first, std::vector<uint8_t>(reinterpret_cast<const uint8_t *>(&dwReason),
                                                             reinterpret_cast<const uint8_t *>(&dwReason) + 4)};
      Fire(e);
      g_players[l.first].bIn = false;
    }
  {
    std::string line = "step " + std::to_string(s.dwNumber) + ":";
    for (const auto &l : s.leaves)
      line += " leave " + std::to_string(l.first);
    for (const auto &t : s.turns) {
      char sz[48];
      snprintf(sz, sizeof sz, " %u:%u:%08X", t.first, static_cast<unsigned>(t.second.size()), Checksum(t.second));
      line += sz;
    }
    Trace("%s", line.c_str());
  }
  g_turnsOut.reserve(s.turns.size());
  for (auto &t : s.turns) {
    if (t.first == g_bLocal)
      ++g_dwRead;
    if (t.first < nFirst || t.first >= nFirst + nPlayers)
      continue;
    // zeros after the turn: the classic walker reads a cut-off last command whole before it
    // finds it cut off (a selection chunk's 1023 bytes at most), and every seat reads the same
    g_turnsOut.push_back(t.second);
    g_turnsOut.back().resize(t.second.size() + kTurnSlack, 0);
    ppvData[t.first - nFirst] = g_turnsOut.back().data();
    pcbData[t.first - nFirst] = static_cast<DWORD>(t.second.size());
    pdwFlags[t.first - nFirst] = TURN_PRESENT | TURN_READY | TURN_ANSWERING;
  }
  return TRUE;
}

BOOL LanSendMessage(DWORD dwPlayer, const void *pvData, DWORD cbData)
{
  if (!pvData || !cbData)
    return Fail(NET_INVALID);
  if (!InGame())
    return FALSE;
  const int nTo = static_cast<int>(dwPlayer);
  if (nTo != -1 && nTo != -2 && !IsPlayer(dwPlayer))
    return Fail(NET_NO_PLAYER);
  std::vector<uint8_t> data(static_cast<const uint8_t *>(pvData), static_cast<const uint8_t *>(pvData) + cbData);
  if (g_bAlone) {
    if (nTo == -1 || nTo == g_bLocal)
      g_messages.push_back(std::make_pair(g_bLocal, data));
  } else {
    Writer w;
    w.D(static_cast<uint32_t>(nTo));
    w.v.insert(w.v.end(), data.begin(), data.end());
    LanNet::Send(g_nRelayConn, MESSAGE, w.v.data(), w.v.size());
  }
  return TRUE;
}

BOOL LanReceiveMessage(int *pnPlayer, const void **ppvData, DWORD *pcbData)
{
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
  Pump();
  FireEvents();
  if (g_messages.empty())
    return Fail(NET_NOTHING_WAITING);
  g_messageOut = g_messages.front().second;
  if (pnPlayer)
    *pnPlayer = g_messages.front().first;
  g_messages.pop_front();
  *ppvData = g_messageOut.data();
  *pcbData = static_cast<DWORD>(g_messageOut.size());
  return TRUE;
}
