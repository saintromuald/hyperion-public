// port_lanroom.cpp -- the LAN host's room (docs/modules/lan.md): Hyperion started with
// --room, with no window and no game; the host's console (port_front_lanhost.cpp) starts it,
// talks to it as its admin over the connection the players use, and stops it. Its state,
// devices, matches, lobbies, launches, connections and main loop are here; its protocol is
// port_lanroom_proto.cpp, the host machine's work port_lanroom_host.cpp, the feeds' replay
// port_lanroom_replay.cpp, and port_lanroom_internal.h is what they share. Presentation and
// transport only. The room is polled from one thread, and what is added must stay polled.
//
// It answers a query on the game port less one with its summary and takes connections on the
// game port: a Hyperion or the console says ROOM and its hello, checked against the host's
// version, simulation, data and mod; a match's players reach its relay (port_lanrelay.cpp) by
// the game id and token the room gave them. hyperion_lan.log has the evening and the relays'.
#include "port_lanroom.h"
#include "port_lanroom_internal.h"
#include "port_lannet.h"
#include "port_lanweb.h"
#include "../platform/port_paths.h"
#include "../data/port_stormerr.h"  // NET_CANNOT_JOIN
#include "../platform/port_util.h"  // PortNowMs
#include "../gameplay/port_mutators.h"  // MutatorsJson, MutatorsFromJson

namespace LanRoom {

std::map<int, Device> g_devices;
std::map<int, std::unique_ptr<Match>> g_matches;
std::vector<StreamSeat> g_streams;
std::vector<LogLine> g_log;
std::vector<int> g_pending;  // connections whose first frame has not come
int g_nNextDevice = 1, g_nNextMatch = 1;
std::string g_name, g_machine, g_key, g_password, g_address;
PortJson g_settings;  // the console's Settings, kept in lanroom.json
std::string g_settingsText;
bool g_bRunning = true, g_bStopAfter = false, g_bQuit = false;
bool g_bDirty = true;
uint64_t g_qwStarted, g_qwLastSent, g_qwLastAdvert, g_qwLastPing, g_qwAdminGone;
bool g_bHadAdmin;
Load g_load;

const PortLogger Log("hyperion_lan.log", PortLogger::SHARED, "room: ");

// A line of the evening, as Tonight shows it, and in the log.
void Tonight(const char *pszKind, const std::string &text, std::vector<int> devs)
{
  g_log.push_back(LogLine{PortNowMs(), pszKind, text, devs});
  if (g_log.size() > 400)
    g_log.erase(g_log.begin());
  Log("%s", text.c_str());
  g_bDirty = true;
}

Device *Dev(int nId)
{
  auto it = g_devices.find(nId);
  return it == g_devices.end() || it->second.bGone ? NULL : &it->second;
}

Match *MatchOf(int nId)
{
  auto it = g_matches.find(nId);
  return it == g_matches.end() ? NULL : it->second.get();
}

// The room's own Hyperions, which are nobody: a match's copy, and a browser's player's seat.
bool Nobody(const Device &d)
{
  return d.bCopy || d.bStream;
}

// Whether a browser's seat is there: its process running, or its page's tab on.
bool SeatOn(const StreamSeat &t)
{
  return t.hProcess != NULL || t.bTabOn;
}

StreamSeat *StreamFor(int nBrowser)
{
  for (StreamSeat &t : g_streams)
    if (nBrowser != 0 && t.nFor == nBrowser && SeatOn(t))
      return &t;
  return NULL;
}

StreamSeat *StreamOn(int nDev)
{
  for (StreamSeat &t : g_streams)
    if (nDev != 0 && t.nDev == nDev)
      return &t;
  return NULL;
}

// The device that plays a seat's game: an installed seat's own Hyperion, or a browser's player's
// seat on this machine once it is in. A browser's page may be gone while its seat plays.
Device *SeatGame(int nSeatDev)
{
  auto it = g_devices.find(nSeatDev);
  if (it == g_devices.end())
    return NULL;
  if (!it->second.bWeb)
    return it->second.bGone ? NULL : &it->second;
  const StreamSeat *t = StreamFor(nSeatDev);
  return t != NULL ? Dev(t->nDev) : NULL;
}

// ---------------------------------------------------------------------------
// Devices.

void Unseat(Device &d)
{
  for (auto &kv : g_matches) {
    Match &m = *kv.second;
    for (Seat &t : m.seats)
      if (t.owner == "player" && t.nDev == d.nId && m.state == "lobby") {
        t.owner = "open";
        t.nDev = 0;
      }
    for (auto it = m.watchers.begin(); it != m.watchers.end();)
      it = *it == d.nId ? m.watchers.erase(it) : it + 1;
  }
  if (d.state == "lobby") {
    d.state = "joined";
    d.nMatch = 0;
  }
  d.bReady = false;
}

// A device into a lobby's open seat: the one asked for, or the first.
bool Sit(Device &d, Match &m, int nSeat)
{
  if (m.state != "lobby" || d.state == "refused" || d.route == "")
    return false;
  if (nSeat < 0)
    for (int i = 0; i < m.nMapPlayers && i < kPlayers; ++i)
      if (m.seats[i].owner == "open") {
        nSeat = i;
        break;
      }
  if (nSeat < 0 || nSeat >= kPlayers || m.seats[nSeat].owner != "open")
    return false;
  Unseat(d);
  m.seats[nSeat].owner = "player";
  m.seats[nSeat].nDev = d.nId;
  d.state = "lobby";
  d.nMatch = m.nId;
  d.bReady = false;
  if (d.bWeb && d.route == "stream")
    StartStream(d);
  else if (d.bWeb && d.route == "tab")
    StartTab(d);
  g_bDirty = true;
  return true;
}

void Gone(Device &d, const char *pszWhy)
{
  if (d.bGone)
    return;
  Unseat(d);
  LanWeb::RevokeFiles(d.filesToken);
  if (StreamSeat *t = d.bWeb ? StreamFor(d.nId) : NULL)
    if (d.state != "playing")
      StopStream(*t);
  if (StreamSeat *t = d.bStream ? StreamOn(d.nId) : NULL) {
    t->nDev = 0;
    t->info.clear();
  }
  if (!d.bAdmin && !Nobody(d) && d.state != "refused" && !d.state.empty())
    Tonight("leave", d.name + " left the room" + (pszWhy[0] ? std::string(": ") + pszWhy : std::string(".")), {d.nId});
  d.bGone = true;
  if (d.bAdmin) {
    // gone when the last of the console's connections has
    bool bOther = false;
    for (const auto &kv : g_devices)
      bOther = bOther || (kv.second.bAdmin && !kv.second.bGone);
    if (!bOther) {
      g_qwAdminGone = PortNowMs();
      Log("the console has gone");
    }
  }
  if (d.nConn >= 0) {
    if (d.bWeb)
      LanWeb::Close(d.nConn);
    else
      LanNet::Drop(d.nConn);
  }
  d.nConn = -1;
  g_bDirty = true;
}

int RaceId(const std::string &race)
{
  return race == "zerg" ? 0 : race == "terran" ? 1 : race == "protoss" ? 2 : 6;
}

// ---------------------------------------------------------------------------
// Matches.

Match &NewMatch(const PortJson &j)
{
  std::unique_ptr<Match> p(new Match);
  Match &m = *p;
  m.nId = g_nNextMatch++;
  m.name = j["name"].Str();
  if (m.name.empty())
    m.name = "Match " + std::to_string(m.nId);
  m.map = j["map"].Str();
  m.mapName = j["mapName"].Str();
  m.type = j["type"].Str("melee");
  m.speed = j["speed"].Str("fastest");
  m.nKey = static_cast<int>(j["key"].Num(m.nKey));
  m.nSpeed = static_cast<int>(j["speedN"].Num(m.nSpeed));
  m.bHyperion = j["rules"].Str() == "hyperion";
  m.mut = MutatorsFromJson(j["mutators"]);
  m.nMapPlayers = static_cast<int>(j["players"].Num(8));
  if (m.nMapPlayers < 2 || m.nMapPlayers > kPlayers)
    m.nMapPlayers = kPlayers;
  for (int i = 0; i < kPlayers; ++i)
    m.seats[i].owner = i < m.nMapPlayers ? "open" : "closed";
  g_matches[m.nId] = std::move(p);
  g_bDirty = true;
  return m;
}

void Back(Match &m)
{
  // those watching go on, the delay behind, until they stop
  for (auto &kv : g_devices) {
    Device &d = kv.second;
    if (d.nMatch == m.nId && d.state == "playing") {
      d.state = "joined";
      d.nMatch = 0;
      d.bReady = false;
      d.dwFrame = 0;
    }
  }
}

// The launch a seat is sent: where the relay is, its token and game, what the game is, and
// which seats are whose.
std::string LaunchJson(const Match &m, const Device &d, bool bCreate)
{
  std::string s = "{\"t\":\"launch\",\"match\":" + std::to_string(m.nId) + ",\"role\":" + PortJsonQuote(bCreate ? "create" : "join") +
                  ",\"port\":" + std::to_string(LanNet::GamePort()) + ",\"token\":" + PortJsonQuote(m.token) +
                  ",\"game\":" + std::to_string(m.dwGameId) + ",\"mapFile\":" + PortJsonQuote(m.map.substr(m.map.find_last_of("\\/") + 1)) +
                  ",\"mapName\":" + PortJsonQuote(m.mapName) + ",\"type\":" + PortJsonQuote(m.type) + ",\"speed\":" + PortJsonQuote(m.speed) +
                  ",\"key\":" + std::to_string(m.nKey) + ",\"speedN\":" + std::to_string(m.nSpeed) +
                  ",\"rules\":" + PortJsonQuote(m.bHyperion ? "hyperion" : "classic") +
                  ",\"mutators\":" + MutatorsJson(m.mut) +
                  ",\"race\":" + std::to_string(RaceId(d.race)) + ",\"seats\":[";
  bool bFirst = true;
  int nHumans = 0;
  for (int i = 0; i < kPlayers; ++i) {
    const Seat &t = m.seats[i];
    if (t.owner != "player" && t.owner != "computer")
      continue;
    const Device *p = t.owner == "player" ? Dev(t.nDev) : NULL;
    nHumans += t.owner == "player";
    s += std::string(bFirst ? "" : ",") + "{\"slot\":" + std::to_string(i) + ",\"kind\":" + PortJsonQuote(t.owner) +
         ",\"race\":" + std::to_string(RaceId(p ? p->race : t.race)) +
         ",\"handicap\":" + std::to_string(p ? p->nHandicap : t.nHandicap) + ",\"name\":" + PortJsonQuote(p ? p->name : "") +
         ",\"colour\":" + std::to_string(t.nColour) + ",\"team\":" + std::to_string(t.nTeam) + "}";
    bFirst = false;
  }
  s += "],\"humans\":" + std::to_string(nHumans);
  if (!bCreate && m.relay)
    s += ",\"name\":" + PortJsonQuote(m.relay->Name()) + ",\"stats\":" + PortJsonQuote(m.relay->Stats());
  return s + "}";
}

// Start Match: the seats' devices are told, the first person's first, who makes the game.
std::string Launch(Match &m, bool bForce)
{
  if (m.state != "lobby")
    return "The match is not in its lobby.";
  int nPeople = 0, nAll = 0, nCreator = 0;
  std::vector<Device *> seating;  // browsers whose seats on this machine are not in yet
  for (int i = 0; i < kPlayers; ++i) {
    const Seat &t = m.seats[i];
    if (t.owner == "player") {
      Device *d = Dev(t.nDev);
      if (d == NULL)
        return "A seat's player has gone.";
      if (d->bWeb && d->route != "stream" && d->route != "tab")
        return "Not available yet: " + d->name + " installs Hyperion.";
      if (d->bWeb && SeatGame(d->nId) == NULL)
        seating.push_back(d);
      if (!d->bReady && !bForce && !d->bAdmin)
        return d->name + " is not ready.";
      if (nCreator == 0)
        nCreator = d->nId;
      ++nPeople;
    }
    nAll += t.owner == "player" || t.owner == "computer";
  }
  if (nPeople == 0)
    return "Nobody is seated.";
  if (nAll < 2)
    return "A match needs two players or more.";
  if (!seating.empty()) {
    for (Device *d : seating)
      if (d->route == "tab")
        StartTab(*d);
      else
        StartStream(*d);
    if (!m.qwSeating) {
      m.qwSeating = PortNowMs();
      m.bSeatingForce = bForce;
      Log("match %d starts once its browsers' seats on this machine are in", m.nId);
    }
    return "";
  }
  m.qwSeating = 0;
  std::vector<uint8_t> map;
  PortReadFile(m.map, &map);
  if (map.empty())
    return "The map cannot be read: " + m.map;
  do
    m.dwGameId = std::uniform_int_distribution<uint32_t>()(Rng());
  while (m.dwGameId == 0);
  m.token = Token();
  m.relay.reset(new LanRelay(m.dwGameId, m.token, "match " + std::to_string(m.nId)));
  m.nCreator = nCreator;
  m.bJoinsSent = false;
  m.state = "starting";
  m.qwLaunched = PortNowMs();
  m.result.clear();
  Device &w = *Dev(nCreator);
  Device &c = *SeatGame(nCreator);
  Writer f;
  f.S(m.map.substr(m.map.find_last_of("\\/") + 1));
  f.Bytes(map.data(), map.size());
  LanNet::Send(c.nConn, ROOM_FILE, f.v.data(), f.v.size());
  SendJson(c, LaunchJson(m, w, true));
  for (int i = 0; i < kPlayers; ++i)
    if (m.seats[i].owner == "player")
      if (Device *d = Dev(m.seats[i].nDev)) {
        d->state = "playing";
        if (Device *g = SeatGame(d->nId))
          g->nMatch = m.nId;
      }
  Log("match %d (%s) launched on %s; %s makes the game", m.nId, m.name.c_str(), m.mapName.c_str(), w.name.c_str());
  g_bDirty = true;
  return "";
}

void CloseRelay(Match &m)
{
  if (m.relay)
    for (const LanRelay::Seat &t : m.relay->Seats())
      m.relay->Remove(t.nId, 0x40000001);
  m.relay.reset();
  m.qwLeaveAsked = 0;
}

void EndMatch(Match &m, const std::string &why)
{
  // a game under way or in its lobby: its seats leave it as Quit Mission or Cancel leaves, while
  // the relay still steps, and the relay goes when they have (PumpMatches)
  if (m.relay && (m.state == "live" || m.state == "starting") && !m.relay->Done()) {
    if (!m.qwLeaveAsked) {
      m.qwLeaveAsked = PortNowMs();
      for (const Seat &t : m.seats)
        if (t.owner == "player")
          if (Device *d = SeatGame(t.nDev))
            SendJson(*d, "{\"t\":\"end\",\"match\":" + std::to_string(m.nId) + "}");
    }
  } else {
    CloseRelay(m);
  }
  if (m.state == "live" || m.state == "starting") {
    m.state = "ended";
    m.qwEnded = PortNowMs();
    if (m.result.empty())
      m.result = why;
    Tonight("end", m.name + (why.empty() ? " ended." : ": " + why), {});
  }
  Back(m);
  g_bDirty = true;
}

// A lobby's map to each installed seat that has not had it, so its set-up can show the map
// and the launch finds it there.
void SendLobbyMaps(Match &m)
{
  if (m.state != "lobby" || m.map.empty())
    return;
  std::vector<uint8_t> map;
  for (const Seat &t : m.seats) {
    Device *d = t.owner == "player" ? SeatGame(t.nDev) : NULL;
    if (d == NULL || d->bAdmin || d->mapSent == m.map)
      continue;
    if (map.empty())
      PortReadFile(m.map, &map);
    if (map.empty())
      return;
    Writer f;
    f.S(m.map.substr(m.map.find_last_of("\\/") + 1));
    f.Bytes(map.data(), map.size());
    LanNet::Send(d->nConn, ROOM_FILE, f.v.data(), f.v.size());
    d->mapSent = m.map;
  }
}

void PumpMatches(void)
{
  const uint64_t qwNow = PortNowMs();
  for (auto &kv : g_matches) {
    Match &m = *kv.second;
    SendLobbyMaps(m);
    PumpFeed(m);
    if (m.qwSeating && m.state == "lobby") {
      bool bAll = true;
      for (const Seat &t : m.seats)
        if (t.owner == "player")
          if (const Device *d = Dev(t.nDev))
            bAll = bAll && (!d->bWeb || SeatGame(d->nId) != NULL);
      if (bAll || qwNow - m.qwSeating > 30000) {
        m.qwSeating = 0;
        const std::string err = bAll ? Launch(m, m.bSeatingForce)
                                     : std::string("A browser's seat on this machine did not start.");
        if (!err.empty())
          if (Device *p = Dev(m.nStarter))
            SendJson(*p, "{\"t\":\"error\",\"text\":" + PortJsonQuote(err) + "}");
        g_bDirty = true;
      }
    }
    if (!m.relay)
      continue;
    m.relay->Pump();
    for (auto &h : m.relay->TakeHashes())
      m.hashes[h.first].swap(h.second);
    // A player the relay lost whose game is still in the room: the match goes on without it, and
    // its game, which plays on alone as 1.17's does when the others stop answering, would see
    // the others leave and win. It leaves the match instead, as Quit Mission leaves it, so the
    // two sides do not both win; the page is told why.
    for (const std::string &name : m.relay->TakeLost()) {
      if (m.state != "live")
        continue;
      for (const Seat &t : m.seats) {
        Device *d = t.owner == "player" ? Dev(t.nDev) : NULL;
        Device *g = d != NULL ? SeatGame(d->nId) : NULL;
        if (g == NULL || g->bGone || (d->name != name && g->name != name))
          continue;
        Log("match %d: %s's game lost its connection to the match but not to the room; it leaves the match", m.nId,
            name.c_str());
        SendJson(*g, "{\"t\":\"end\",\"match\":" + std::to_string(m.nId) + ",\"why\":\"lost\"}");
        if (d->bWeb && d != g && !d->bGone)
          SendJson(*d, "{\"t\":\"error\",\"text\":" +
                           PortJsonQuote("Your game lost its connection to the match, so it has left it. The others play on.") + "}");
        break;
      }
    }
    if (m.state == "starting" && m.relay->Created() && !m.bJoinsSent) {
      m.bJoinsSent = true;
      for (int i = 0; i < kPlayers; ++i)
        if (m.seats[i].owner == "player" && m.seats[i].nDev != m.nCreator)
          if (Device *d = Dev(m.seats[i].nDev))
            if (Device *g = SeatGame(d->nId))
              SendJson(*g, LaunchJson(m, *d, false));
    }
    if (m.state == "starting" && m.relay->UnderWay()) {
      m.state = "live";
      m.qwStarted = qwNow;
      std::string who;
      std::vector<int> devs;
      for (const Seat &t : m.seats)
        if (t.owner == "player")
          if (const Device *d = Dev(t.nDev)) {
            who += std::string(who.empty() ? "" : ", ") + d->name;
            devs.push_back(d->nId);
          }
      Tonight("start", (m.name == m.mapName ? m.name + " started: " : m.name + " started on " + m.mapName + ": ") + who + ".",
              devs);
      StartCopy(m);
    }
    // a player the relay refused for another version never comes, so the match ends now and says
    // why, rather than at the 90 s below
    if (m.state == "starting" && m.relay && !m.relay->Mismatch().empty()) {
      Log("match %d: %s", m.nId, m.relay->Mismatch().c_str());
      EndMatch(m, m.relay->Mismatch());
      m.state = "lobby";
      continue;
    }
    if (m.state == "starting" && qwNow - m.qwLaunched > 90000) {
      Log("match %d did not start within 90 s", m.nId);
      EndMatch(m, "it did not start");
      m.state = "lobby";
      continue;
    }
    if (m.relay && m.relay->Done() && (m.state == "live" || m.state == "starting"))
      EndMatch(m, "");
    if (m.relay && m.qwLeaveAsked && (m.relay->Done() || qwNow - m.qwLeaveAsked > 8000))
      CloseRelay(m);
    if (m.relay)
      g_bDirty = true;  // its latencies and hashes
  }
}

// ---------------------------------------------------------------------------
// Connections.

void Accept(void)
{
  for (int n; (n = LanNet::Accept()) >= 0;)
    g_pending.push_back(n);
  for (size_t k = 0; k < g_pending.size();) {
    const int nConn = g_pending[k];
    uint8_t bKind;
    std::vector<uint8_t> data;
    if (LanNet::Receive(nConn, &bKind, &data)) {
      g_pending.erase(g_pending.begin() + k);
      if (bKind == ROOM) {
        Device d;
        d.nId = g_nNextDevice++;
        d.nConn = nConn;
        d.ip = LanNet::AddrText(LanNet::PeerAddr(nConn));
        g_devices[d.nId] = d;
        Heard(g_devices[d.nId], std::string(data.begin(), data.end()));
      } else if (bKind == HELLO || bKind == CREATE) {
        Reader r(data);
        r.W();
        r.W();
        r.S();
        r.S();
        r.S();
        r.S();
        const uint32_t dwGame = r.D();
        LanRelay *pRelay = NULL;
        for (auto &kv : g_matches)
          if (kv.second->relay && kv.second->dwGameId == dwGame)
            pRelay = kv.second->relay.get();
        if (pRelay != NULL) {
          pRelay->Admit(nConn, bKind, data);
        } else {
          Writer w;
          w.D(NET_CANNOT_JOIN);
          w.S("no such match");
          LanNet::Send(nConn, REFUSE, w.v.data(), w.v.size());
          LanNet::Drop(nConn);
        }
      } else {
        LanNet::Drop(nConn);
      }
    } else if (!LanNet::Greeted(nConn, kGreetingMs)) {
      g_pending.erase(g_pending.begin() + k);
      LanNet::Drop(nConn);
    } else {
      ++k;
    }
  }
}

void PumpDevices(void)
{
  const uint64_t qwNow = PortNowMs();
  const bool bPing = qwNow - g_qwLastPing >= 2000;
  if (bPing)
    g_qwLastPing = qwNow;
  for (auto &kv : g_devices) {
    Device &d = kv.second;
    if (d.bGone)
      continue;
    if (d.bWeb) {
      // a page's latency, once it has joined (its WebSocket's frames are read below)
      if (bPing && !d.state.empty() && d.state != "refused") {
        d.qwPingSent = qwNow;
        SendJson(d, "{\"t\":\"ping\"}");
      }
      continue;
    }
    uint8_t bKind;
    std::vector<uint8_t> data;
    while (!d.bGone && LanNet::Receive(d.nConn, &bKind, &data))
      if (bKind == ROOM)
        Heard(d, std::string(data.begin(), data.end()));
      else if (bKind == ROOM_FEED)
        FeedHeard(d, data);
    if (!d.bGone && !LanNet::Alive(d.nConn))
      Gone(d, "");
    if (!d.bGone && bPing) {
      d.qwPingSent = qwNow;
      SendJson(d, "{\"t\":\"ping\"}");
    }
  }
  // the page's devices (port_lanweb.cpp)
  LanWeb::Pump();
  for (const LanWeb::Event &e : LanWeb::Events()) {
    if (e.kind == LanWeb::NOTE) {
      Log("%s", e.text.c_str());
      continue;
    }
    if (e.kind == LanWeb::OPENED) {
      Device d;
      d.nId = g_nNextDevice++;
      d.nConn = e.nConn;
      d.bWeb = true;
      d.ip = LanNet::AddrText(LanNet::PeerAddr(e.nConn));
      g_devices[d.nId] = d;
    } else {
      for (auto &kv : g_devices)
        if (kv.second.bWeb && kv.second.nConn == e.nConn && !kv.second.bGone) {
          if (e.kind == LanWeb::TEXT)
            Heard(kv.second, e.text);
          else
            Gone(kv.second, "");
          break;
        }
    }
  }
  PumpStreams();
  // the gone are forgotten once the console has seen them go; a browser's, while its seat on this
  // machine plays, is kept for the page to come back to
  for (auto it = g_devices.begin(); it != g_devices.end();)
    it = it->second.bGone && it->second.state != "refused" && StreamFor(it->first) == NULL ? g_devices.erase(it)
                                                                                         : std::next(it);
}

void Broadcast(void)
{
  const uint64_t qwNow = PortNowMs();
  if (!(g_bDirty && qwNow - g_qwLastSent >= 150) && qwNow - g_qwLastSent < 1000)
    return;
  g_qwLastSent = qwNow;
  g_bDirty = false;
  for (auto &kv : g_devices) {
    Device &d = kv.second;
    // each its own "you", and the console its settings and the password; the refused only
    // what they were told
    if (!d.bGone && d.state != "refused")
      SendJson(d, RoomJson(&d));
  }
}

}  // namespace LanRoom

using namespace LanRoom;

int PortLanRoomMain(void)
{
  const PortOptions *o = PortCommandOptions();
  g_qwStarted = PortNowMs();
  if (!LanNet::Startup())
    return 1;
  SettingsLoad();
  g_machine = MachineName();
  g_key = o->szRoomKey;
  g_name = o->szRoomName[0] ? o->szRoomName : Setting("name", "");
  if (g_name.empty())
    g_name = g_machine + "'s room";
  g_password = o->szRoomPassword[0] ? o->szRoomPassword : NewPassword();
  g_address = LanNet::AddrText(LanNet::LocalAddr());
  if (!LanNet::Listen()) {
    Log("cannot listen on port %d: another room or game has it", LanNet::GamePort());
    return 2;
  }
  RoomNoteWrite();
  LanWeb::Start();
  ServeFonts();
  ServeGameFiles();
  Log("\"%s\" on %s, %s:%d, the page on %d%s", g_name.c_str(), g_machine.c_str(), g_address.c_str(), LanNet::GamePort(),
      LanWeb::Port(), g_key.empty() ? ", with no console" : "");
  Tonight("info", "The server started: " + g_name + ", on " + g_address + ".");
  while (!g_bQuit) {
    LanNet::Pump();
    Accept();
    PumpDevices();
    PumpMatches();
    const uint64_t qwNow = PortNowMs();
    if (qwNow - g_qwLastAdvert >= 1000) {
      g_qwLastAdvert = qwNow;
      Advertise();
      ReadLoad();
    }
    Broadcast();
    // the console gone a minute: its Hyperion has closed
    if (g_bHadAdmin && g_qwAdminGone && qwNow - g_qwAdminGone > 60000) {
      Log("the console has been gone a minute; the room stops");
      break;
    }
    if (g_bStopAfter) {
      bool bAny = false;
      for (const auto &kv : g_matches)
        bAny = bAny || kv.second->state == "live" || kv.second->state == "starting";
      if (!bAny)
        break;
    }
    LanNet::Wait(5);
  }
  for (auto &kv : g_matches)
    if (kv.second->relay)
      EndMatch(*kv.second, "the server stopped");
  for (auto &kv : g_matches)
    StopCopy(*kv.second);
  for (StreamSeat &t : g_streams)
    StopStream(t);
  for (auto &kv : g_devices)
    if (!kv.second.bGone)
      SendJson(kv.second, "{\"t\":\"closed\"}");
  LanNet::Pump();
  RoomNoteRemove();
  Log("stopped");
  LanWeb::Stop();
  LanNet::CloseAll();
  return 0;
}
