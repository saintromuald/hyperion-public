// port_lanroom_proto.cpp -- the room's words with its devices: the JSON each is
// told (the room, a match, a device, the summary the rooms list shows), the hello that lets
// a Hyperion in or says why not, and what a device and the console say. Presentation and
// transport only.
#include "port_lanroom_internal.h"
#include <stdio.h>
#include "../../../hyperion/version.h"  // HYPERION_VERSION_TEXT
#include "../../h/hxsim.h"              // HX_SIM_REVISION
#include "port_lannet.h"
#include "port_lanweb.h"
#include "../front/port_text.h"  // PT_: the room's sentences go English on the wire
#include "../platform/port_util.h"  // PortNowMs
#include "../gameplay/port_handicap.h"  // HandicapClamp
#include "../gameplay/port_mutators.h"  // MutatorsJson, MutatorsFromJson

namespace LanRoom {

// ---------------------------------------------------------------------------
// Saying things to devices.

void SendJson(Device &d, const std::string &json)
{
  if (d.bWeb)
    LanWeb::SendText(d.nConn, json);
  else
    LanNet::Send(d.nConn, ROOM, json.data(), json.size());
}

void SendTo(int nDev, const std::string &json)
{
  if (Device *d = Dev(nDev))
    SendJson(*d, json);
}

std::string Num(double d)
{
  char sz[32];
  snprintf(sz, sizeof sz, "%.0f", d);
  return sz;
}

std::string Bool(bool b)
{
  return b ? "true" : "false";
}

std::string DeviceJson(const Device &d, uint64_t qwNow)
{
  std::string s = "{\"id\":" + std::to_string(d.nId) + ",\"name\":" + PortJsonQuote(d.name) + ",\"machine\":" + PortJsonQuote(d.machine) +
                  ",\"os\":" + PortJsonQuote(d.os) + ",\"arch\":" + PortJsonQuote(d.arch) + ",\"kind\":" + PortJsonQuote(d.kind) +
                  ",\"route\":" + PortJsonQuote(d.route) + ",\"state\":" + PortJsonQuote(d.state) + ",\"match\":" + std::to_string(d.nMatch) +
                  ",\"version\":" + PortJsonQuote(d.version) + ",\"sim\":" + std::to_string(d.nSim) + ",\"mod\":" + PortJsonQuote(d.mod) +
                  ",\"fp\":" + PortJsonQuote(d.fp) + ",\"ip\":" + PortJsonQuote(d.ip) + ",\"browser\":" + PortJsonQuote(d.browser) +
                  ",\"page\":" + PortJsonQuote(d.page) + ",\"rtt\":" + std::to_string(d.dwRtt) +
                  ",\"in\":" + Num((qwNow - d.qwJoined) / 1000.0) + ",\"ready\":" + Bool(d.bReady) +
                  ",\"race\":" + PortJsonQuote(d.race) + ",\"handicap\":" + std::to_string(d.nHandicap) +
                  ",\"admin\":" + Bool(d.bAdmin) + ",\"frame\":" + std::to_string(d.dwFrame);
  if (!d.why.empty())
    s += ",\"why\":" + PortJsonQuote(d.why) + ",\"fix\":" + PortJsonQuote(d.fix);
  if (const StreamSeat *t = d.bWeb ? StreamFor(d.nId) : NULL)
    s += ",\"seat\":" + Bool(t->nDev != 0) + (t->info.empty() ? std::string() : ",\"stream\":" + t->info);
  return s + "}";
}

std::string MatchJson(const Match &m, uint64_t qwNow)
{
  std::string s = "{\"id\":" + std::to_string(m.nId) + ",\"name\":" + PortJsonQuote(m.name) + ",\"state\":" + PortJsonQuote(m.state) +
                  ",\"map\":" + PortJsonQuote(m.map) + ",\"mapFile\":" + PortJsonQuote(m.map.substr(m.map.find_last_of("\\/") + 1)) +
                  ",\"mapName\":" + PortJsonQuote(m.mapName) + ",\"type\":" + PortJsonQuote(m.type) + ",\"key\":" + std::to_string(m.nKey) +
                  ",\"speed\":" + PortJsonQuote(m.speed) + ",\"speedN\":" + std::to_string(m.nSpeed) +
                  ",\"rules\":" + PortJsonQuote(m.bHyperion ? "hyperion" : "classic") +
                  ",\"mutators\":" + MutatorsJson(m.mut) +
                  ",\"mapPlayers\":" + std::to_string(m.nMapPlayers) +
                  (m.pic.empty() ? std::string() : ",\"pic\":" + PortJsonQuote(m.pic) + ",\"picStarts\":" + m.picStarts) +
                  ",\"seats\":[";
  for (int i = 0; i < kPlayers; ++i) {
    const Seat &t = m.seats[i];
    const Device *d = t.owner == "player" ? Dev(t.nDev) : NULL;
    s += std::string(i ? "," : "") + "{\"owner\":" + PortJsonQuote(t.owner) + ",\"dev\":" + std::to_string(t.nDev) +
         ",\"race\":" + PortJsonQuote(d ? d->race : t.race) +
         ",\"handicap\":" + std::to_string(d ? d->nHandicap : t.nHandicap) + ",\"colour\":" + std::to_string(t.nColour) +
         ",\"team\":" + std::to_string(t.nTeam) + ",\"ready\":" + Bool(d && d->bReady) +
         ",\"name\":" + PortJsonQuote(d ? d->name : "") + "}";
  }
  s += "],\"watchers\":[";
  bool bAny = false;
  for (int nDev : m.watchers) {
    // the room's copy watches too, but is nobody; a browser's seat watches for its page
    const Device *w = Dev(nDev);
    if (w != NULL && w->bCopy)
      continue;
    const StreamSeat *t = w != NULL && w->bStream ? StreamOn(nDev) : NULL;
    s += std::string(bAny ? "," : "") + std::to_string(t != NULL ? t->nFor : nDev);
    bAny = true;
  }
  s += "],\"chat\":[";
  const size_t nFrom = m.chat.size() > 40 ? m.chat.size() - 40 : 0;
  for (size_t i = nFrom; i < m.chat.size(); ++i)
    s += std::string(i > nFrom ? "," : "") + "[" + PortJsonQuote(m.chat[i].first) + "," + PortJsonQuote(m.chat[i].second) + "]";
  s += "]";
  // how long it has played, from the seats' frames: 1.17's fastest is 24 frames a second
  uint32_t dwFrame = 0;
  for (const Seat &t : m.seats)
    if (const Device *d = t.owner == "player" ? Dev(t.nDev) : NULL)
      if (d->nMatch == m.nId && d->dwFrame > dwFrame)
        dwFrame = d->dwFrame;
  s += ",\"frame\":" + std::to_string(dwFrame);
  if (m.qwStarted)
    s += ",\"since\":" + Num(((m.qwEnded ? m.qwEnded : qwNow) - m.qwStarted) / 1000.0);
  if (m.relay) {
    s += ",\"hashes\":{\"agreed\":" + std::to_string(m.relay->HashesAgreed()) +
         ",\"differed\":" + std::to_string(m.relay->HashesDiffered()) + "},\"rtt\":[";
    bool bFirst = true;
    for (const LanRelay::Seat &t : m.relay->Seats()) {
      s += std::string(bFirst ? "" : ",") + "[" + PortJsonQuote(t.name) + "," + std::to_string(t.dwLatency) + "," +
           Bool(t.bWaitedFor) + "]";
      bFirst = false;
    }
    s += "]";
  }
  if (!m.result.empty())
    s += ",\"result\":" + PortJsonQuote(m.result);
  if (m.hCopy != NULL || m.dwCopyAgreed || m.dwCopyDiffered)
    s += ",\"copy\":{\"in\":" + Bool(m.nCopyDev != 0 && Dev(m.nCopyDev) != NULL && !Dev(m.nCopyDev)->bGone) +
         ",\"agreed\":" + std::to_string(m.dwCopyAgreed) + ",\"differed\":" + std::to_string(m.dwCopyDiffered) + "}";
  if (m.dwWatchAgreed || m.dwWatchDiffered)
    s += ",\"watchHashes\":{\"agreed\":" + std::to_string(m.dwWatchAgreed) +
         ",\"differed\":" + std::to_string(m.dwWatchDiffered) + "}";
  if (!m.replay.empty())
    s += ",\"replay\":" + PortJsonQuote(m.replay.substr(m.replay.find_last_of("\\/") + 1));
  return s + "}";
}

// What a page that has not joined yet is told: the room, its version and who is in it (their
// names and devices, so a player sees it is the right room), nothing of its matches, chat or log.
std::string PublicJson(void)
{
  std::string s = "{\"t\":\"room\",\"public\":true,\"name\":" + PortJsonQuote(g_name) + ",\"machine\":" + PortJsonQuote(g_machine) +
                  ",\"address\":" + PortJsonQuote(g_address) + ",\"web\":" + std::to_string(LanWeb::Port()) +
                  ",\"version\":" + PortJsonQuote(HYPERION_VERSION_TEXT) + ",\"game\":" + PortJsonQuote(Setting("game", "Brood War")) +
                  ",\"running\":" + Bool(g_bRunning && !g_bStopAfter) + ",\"password\":" + Bool(PasswordOn()) +
                  ",\"play\":" + Bool(LanWeb::PlayOn()) + ",\"files\":" + Bool(BrowserFilesOn()) + ",\"devices\":[";
  bool bFirst = true;
  for (const auto &kv : g_devices) {
    const Device &d = kv.second;
    if (d.bGone || Nobody(d) || d.bAdmin || d.state.empty() || d.state == "refused" || d.state == "answered")
      continue;
    s += std::string(bFirst ? "" : ",") + "{\"name\":" + PortJsonQuote(d.name) + ",\"kind\":" + PortJsonQuote(d.kind) +
         ",\"route\":" + PortJsonQuote(d.route) + "}";
    bFirst = false;
  }
  return s + "]}";
}

std::string RoomJson(const Device *pFor)
{
  if (pFor != NULL && pFor->state.empty())  // before its hello, framed or a page
    return PublicJson();
  const uint64_t qwNow = PortNowMs();
  int nPeople = 0, nLive = 0;
  for (const auto &kv : g_devices)
    nPeople += !kv.second.bGone && kv.second.state != "refused" && !kv.second.state.empty() && !kv.second.bAdmin &&
               !Nobody(kv.second);
  for (const auto &kv : g_matches)
    nLive += kv.second->state == "live";
  std::string s = "{\"t\":\"room\",\"name\":" + PortJsonQuote(g_name) + ",\"machine\":" + PortJsonQuote(g_machine) +
                  ",\"address\":" + PortJsonQuote(g_address) + ",\"port\":" + std::to_string(LanNet::GamePort()) +
                  ",\"web\":" + std::to_string(LanWeb::Port()) + ",\"version\":" + PortJsonQuote(HYPERION_VERSION_TEXT) +
                  ",\"sim\":" + std::to_string(HX_SIM_REVISION) + ",\"game\":" + PortJsonQuote(Setting("game", "Brood War")) +
                  ",\"rules\":" + PortJsonQuote(Setting("rules", "classic")) + ",\"running\":" + Bool(g_bRunning) +
                  ",\"stopAfter\":" + Bool(g_bStopAfter) + ",\"up\":" + Num((qwNow - g_qwStarted) / 1000.0) +
                  ",\"password\":" + Bool(PasswordOn()) + ",\"people\":" + std::to_string(nPeople) +
                  ",\"live\":" + std::to_string(nLive) + ",\"play\":" + Bool(LanWeb::PlayOn()) +
                  ",\"files\":" + Bool(BrowserFilesOn());
  if (pFor != NULL) {
    s += ",\"you\":" + std::to_string(pFor->nId);
    s += ",\"delay\":" + std::to_string(DelaySeconds()) + ",\"watching\":" + Bool(WatchingOn());
    if (pFor->bAdmin)
      s += ",\"passwordText\":" + PortJsonQuote(g_password) + ",\"settings\":" + g_settingsText + ",\"load\":{\"cpu\":" +
           Num(g_load.fCpu * 10) + ",\"mem\":" + Num(g_load.fMemUsed) + ",\"memTotal\":" + Num(g_load.fMemTotal) + "}";
  }
  s += ",\"devices\":[";
  bool bFirst = true;
  for (const auto &kv : g_devices) {
    // the refused stay listed with their reason, as the console shows them; the host's own
    // Hyperion is listed too, as it plays from a seat ("admin")
    if ((kv.second.bGone && kv.second.state != "refused") || Nobody(kv.second) || kv.second.state.empty())
      continue;
    s += std::string(bFirst ? "" : ",") + DeviceJson(kv.second, qwNow);
    bFirst = false;
  }
  s += "],\"matches\":[";
  bFirst = true;
  for (const auto &kv : g_matches) {
    s += std::string(bFirst ? "" : ",") + MatchJson(*kv.second, qwNow);
    bFirst = false;
  }
  s += "],\"log\":[";
  const size_t nFrom = g_log.size() > 60 ? g_log.size() - 60 : 0;
  for (size_t i = g_log.size(); i-- > nFrom;) {
    const LogLine &l = g_log[i];
    s += std::string(i + 1 < g_log.size() ? "," : "") + "{\"ago\":" + Num((qwNow - l.qwAt) / 1000.0) +
         ",\"kind\":" + PortJsonQuote(l.kind) + ",\"text\":" + PortJsonQuote(l.text) + ",\"devs\":[";
    for (size_t k = 0; k < l.devs.size(); ++k)
      s += std::string(k ? "," : "") + std::to_string(l.devs[k]);
    s += "]}";
  }
  return s + "]}";
}

// The summary a query is answered with, and Multiplayer lists.
void Advertise(void)
{
  int nPeople = 0, nLive = 0;
  for (const auto &kv : g_devices)
    nPeople += !kv.second.bGone && kv.second.state != "refused" && !kv.second.state.empty() && !kv.second.bAdmin &&
               !Nobody(kv.second);
  for (const auto &kv : g_matches)
    nLive += kv.second->state == "live";
  const std::string json = "{\"name\":" + PortJsonQuote(g_name) + ",\"machine\":" + PortJsonQuote(g_machine) +
                           ",\"port\":" + std::to_string(LanNet::GamePort()) + ",\"web\":" + std::to_string(LanWeb::Port()) +
                           ",\"version\":" + PortJsonQuote(HYPERION_VERSION_TEXT) + ",\"sim\":" + std::to_string(HX_SIM_REVISION) +
                           ",\"fp\":" + PortJsonQuote(Setting("fp", "")) + ",\"mod\":" + PortJsonQuote(Setting("mod", "")) +
                           ",\"game\":" + PortJsonQuote(Setting("game", "Brood War")) + ",\"rules\":" + PortJsonQuote(Setting("rules", "classic")) +
                           ",\"people\":" + std::to_string(nPeople) + ",\"matches\":" + std::to_string(nLive) +
                           ",\"password\":" + Bool(PasswordOn()) + ",\"cup\":false}";
  Writer w;
  w.W(kProtocol);
  w.B(ADVERT_ROOM);
  w.S(json);
  LanNet::SetAdvert(g_bRunning ? w.v : std::vector<uint8_t>());
}

// A device's hello: who it is, and whether its copy plays here. A refused device stays in the
// list with the reason, as the console shows it, until it goes.
void Hello(Device &d, const PortJson &j)
{
  // in already: a page's second press of Join, or a second hello on a framed connection, which
  // would otherwise name itself again past the checks it passed
  if (!d.state.empty() && d.state != "refused")
    return;
  // a name as the game holds one: 24 bytes at most, no control characters, and none of the
  // quote and backslash a seat's command line carries it between
  d.name.clear();
  for (char c : j["name"].Str())
    if (static_cast<unsigned char>(c) >= 0x20 && c != '"' && c != '\\' && c != 0x7F && d.name.size() < 24)
      d.name += c;
  if (!d.name.empty()) {  // no character cut in two
    size_t at = d.name.size() - 1;
    while (at > 0 && (static_cast<unsigned char>(d.name[at]) & 0xC0) == 0x80)
      --at;
    const unsigned char bLead = static_cast<unsigned char>(d.name[at]);
    const size_t nWhole = bLead >= 0xF0 ? 4 : bLead >= 0xE0 ? 3 : bLead >= 0xC0 ? 2 : 1;
    if (d.name.size() - at < nWhole)
      d.name.resize(at);
  }
  if (d.name.empty())
    d.name = "Player";
  d.machine = j["machine"].Str();
  d.os = j["os"].Str();
  d.arch = j["arch"].Str();
  d.version = j["version"].Str();
  d.nSim = static_cast<int>(j["sim"].Num());
  d.fp = j["fp"].Str();
  d.mod = j["mod"].Str();
  d.rules = j["rules"].Str();
  d.browser = j["browser"].Str();
  d.route = j["route"].Str(d.bWeb ? "stream" : "install");
  d.kind = j["kind"].Str(d.bWeb ? "browser" : "windows");
  d.qwJoined = PortNowMs();
  if (!g_key.empty() && j["key"].Str() == g_key && !d.bWeb && LanNet::IsLoopback(LanNet::PeerAddr(d.nConn))) {
    d.bAdmin = true;
    d.state = "joined";
    g_bHadAdmin = true;
    g_qwAdminGone = 0;
    Log("the console is in");
    SendJson(d, "{\"t\":\"welcome\",\"you\":" + std::to_string(d.nId) + ",\"admin\":true}");
    g_bDirty = true;
    return;
  }
  // the room's copy of a match, by the token the room started it with
  if (!j["copy"].Str().empty() && !d.bWeb && LanNet::IsLoopback(LanNet::PeerAddr(d.nConn)))
    for (auto &kv : g_matches) {
      Match &m = *kv.second;
      if (m.copyToken.empty() || j["copy"].Str() != m.copyToken)
        continue;
      d.bCopy = true;
      d.state = "copy";
      d.nMatch = m.nId;
      m.nCopyDev = d.nId;
      m.watchers.push_back(d.nId);
      m.sent.erase(d.nId);
      Log("match %d: its copy is in", m.nId);
      SendJson(d, "{\"t\":\"welcome\",\"you\":" + std::to_string(d.nId) + "}");
      return;
    }
  // a browser's player's seat on this machine, by the token the room started it with
  if (!j["stream"].Str().empty() && !d.bWeb && LanNet::IsLoopback(LanNet::PeerAddr(d.nConn)))
    for (StreamSeat &t : g_streams) {
      if (t.token != j["stream"].Str())
        continue;
      d.bStream = true;
      d.state = "stream";
      d.route = "stream";
      t.nDev = d.nId;
      Log("%s's seat on this machine is in", t.name.c_str());
      SendJson(d, "{\"t\":\"welcome\",\"you\":" + std::to_string(d.nId) + "}");
      return;
    }
  // a browser's player's seat in their own tab, by the token the room gave the page; it comes
  // through the room's /tcp bridge from the browser's address
  if (!j["tab"].Str().empty() && !d.bWeb)
    for (StreamSeat &t : g_streams) {
      if (!t.bTab || !t.bTabOn || t.token != j["tab"].Str())
        continue;
      // the room serves the page its Hyperion from web/play, which can be another build than the
      // room's own when the folder was not updated with it: refused here, with the reason on the
      // page over the game's frame, rather than refused by the match's relay at its start
      if (j["version"].Str() != HYPERION_VERSION_TEXT || static_cast<int>(j["sim"].Num()) != HX_SIM_REVISION) {
        const std::string why = "The game this room gives the browser is " + j["version"].Str() + ", and the room plays " +
                                HYPERION_VERSION_TEXT + ".";
        const std::string fix = PT_("The host's web\\play folder is from another build of Hyperion; play streamed meanwhile.");
        Log("%s's seat in the browser refused: it is %s", t.name.c_str(), j["version"].Str().c_str());
        Tonight("bad", t.name + "'s game in the browser couldn't join: " + why, {t.nFor});
        SendJson(d, "{\"t\":\"refused\",\"why\":" + PortJsonQuote(why) + ",\"fix\":" + PortJsonQuote(fix) + "}");
        if (Device *page = Dev(t.nFor))
          SendJson(*page, "{\"t\":\"tabrefused\",\"why\":" + PortJsonQuote(why) + ",\"fix\":" + PortJsonQuote(fix) + "}");
        StopStream(t);
        return;
      }
      if (Device *old = Dev(t.nDev))
        if (old != &d && !old->bGone)
          Gone(*old, "");
      d.bStream = true;
      d.state = "stream";
      d.route = "tab";
      t.nDev = d.nId;
      Log("%s's seat in the browser is in", t.name.c_str());
      SendJson(d, "{\"t\":\"welcome\",\"you\":" + std::to_string(d.nId) + "}");
      g_bDirty = true;
      return;
    }
  std::string why, fix;
  const std::string who = d.machine.empty() ? d.name : d.machine;
  const Device *pSame = NULL;
  for (const auto &kv : g_devices) {
    const Device &o = kv.second;
    if (&o != &d && !o.bGone && !o.bAdmin && !Nobody(o) && (o.state == "joined" || o.state == "lobby" ||
        o.state == "playing" || o.state == "watching" || o.state == "installing") && PortLower(o.name) == PortLower(d.name))
      pSame = &o;
  }
  if (!g_bRunning || g_bStopAfter) {
    why = PT_("The room is not taking anyone now.");
    fix = PT_("Ask the host to start the server.");
  } else if (pSame != NULL) {
    why = pSame->name + " is already here.";
    fix = PT_("Choose another name.");
  } else if (PasswordOn() && j["password"].Str() != g_password) {
    why = j["password"].Str().empty() ? PT_("The room asks for its password.") : PT_("That is not the room's password.");
    fix = PT_("The host's console shows it, under Join This Room.");
  } else if (!d.bWeb && (d.version != HYPERION_VERSION_TEXT || d.nSim != HX_SIM_REVISION)) {
    why = who + " has " + d.version + ", and the room plays " + HYPERION_VERSION_TEXT +
          ": the two compute the game differently.";
    fix = PT_("The host's page offers the room's version to install.");
  } else if (!d.bWeb && d.mod != Setting("mod", "")) {
    why = d.mod.empty() ? "The room plays the mod " + Setting("mod", "") + ", which " + who + " does not have on."
                        : "The mod " + d.mod + " is active on " + who + ".";
    fix = d.mod.empty() ? PT_("Activate it under Mods, then join.") : PT_("Turn it off under Mods, then join.");
  } else if (!d.bWeb && !Setting("fp", "").empty() && !d.fp.empty() && d.fp != Setting("fp", "")) {
    why = who + "'s game data is not the host's (fingerprint " + d.fp + ").";
    fix = PT_("The host's page has the game data to install.");
  }
  if (!why.empty()) {
    d.state = "refused";
    d.why = why;
    d.fix = fix;
    Tonight("bad", d.name + " couldn't join: " + why, {d.nId});
    SendJson(d, "{\"t\":\"refused\",\"why\":" + PortJsonQuote(why) + ",\"fix\":" + PortJsonQuote(fix) + "}");
    return;
  }
  d.state = "joined";
  // an earlier refusal of the same player on the same machine is answered: it leaves the list
  for (auto &kv : g_devices) {
    Device &o = kv.second;
    if (&o != &d && o.state == "refused" && o.name == d.name && o.ip == d.ip) {
      Gone(o, "");
      o.state = "answered";
    }
  }
  // the device as the page says it, in words: "a Windows PC", "a phone"
  const std::string kind = d.kind == "windows" ? "a Windows PC" : d.kind == "linux" ? "a Linux PC"
                           : d.kind == "arm"   ? "an ARM computer" : d.kind == "mac" ? "a Mac"
                           : d.kind == "phone" ? "a phone" : d.kind == "tablet" ? "a tablet" : "";
  const std::string how = d.bWeb ? "from a browser" + (kind.empty() ? std::string() : " on " + kind)
                                 : "with Hyperion installed, on " + (d.machine.empty() ? std::string("a PC") : d.machine);
  Tonight("join", d.name + " joined " + how + ".", {d.nId});
  // a page in the room may fetch the host's game files, by a token of its own (finding the
  // address is not enough: the password is passed first)
  std::string files;
  if (d.bWeb) {
    d.filesToken = Token();
    LanWeb::AllowFiles(d.filesToken);
    files = ",\"files\":" + PortJsonQuote("/files/" + d.filesToken + "/");
    // and the token the page shows to take its seat back after a reload
    d.backToken = Token();
    files += ",\"back\":" + PortJsonQuote(d.backToken);
  }
  SendJson(d, "{\"t\":\"welcome\",\"you\":" + std::to_string(d.nId) + files + "}");
  // a page opened again by a player whose seat on this machine plays on: the seat is theirs again
  if (d.bWeb && d.route == "tab")
    StartTab(d);
  if (d.bWeb)
    for (StreamSeat &t : g_streams) {
      if (t.bTab)
        continue;
      auto it = g_devices.find(t.nFor);
      // the page the room gave the seat's token: a name alone is anyone's
      if (it == g_devices.end() || !it->second.bGone || PortLower(it->second.name) != PortLower(d.name) ||
          it->second.backToken.empty() || j["back"].Str() != it->second.backToken)
        continue;
      Device &o = it->second;
      t.nFor = d.nId;
      d.state = o.state;
      d.nMatch = o.nMatch;
      d.race = o.race;
      d.nHandicap = o.nHandicap;
      for (auto &kv : g_matches)
        for (Seat &seat : kv.second->seats)
          if (seat.owner == "player" && seat.nDev == o.nId)
            seat.nDev = d.nId;
      Log("%s's page is back, to the seat on this machine", d.name.c_str());
      Tonight("info", d.name + " is back.", {d.nId});
      g_bDirty = true;
      return;
    }
  // into the lobby being set up, as Join a Room opens it
  for (auto &kv : g_matches)
    if (Sit(d, *kv.second, -1))
      break;
  g_bDirty = true;
}

// ---------------------------------------------------------------------------
// What a device says.

void Admin(Device &d, const std::string &t, const PortJson &j)
{
  std::string err;
  Match *m = MatchOf(static_cast<int>(j["match"].Num()));
  if (t == "newmatch") {
    Match &n = NewMatch(j);
    // the host plays from the first seat when it says so; the room's people who are in no match
    // take the others, as the design's New Match seats them
    if (j["play"].b)
      Sit(d, n, 0);
    for (auto &kv : g_devices)
      if (!kv.second.bGone && !kv.second.bAdmin && kv.second.state == "joined")
        Sit(kv.second, n, -1);
    SendJson(d, "{\"t\":\"made\",\"match\":" + std::to_string(n.nId) + "}");
  } else if (t == "match" && m) {
    if (j["map"].kind == PortJson::STRING) {
      if (j["map"].Str() != m->map)
        m->pic.clear();  // its picture comes after
      m->map = j["map"].Str();
      m->mapName = j["mapName"].Str(m->mapName.c_str());
      m->nMapPlayers = static_cast<int>(j["players"].Num(m->nMapPlayers));
      for (int i = 0; i < kPlayers; ++i)
        if (i >= m->nMapPlayers && m->seats[i].owner != "closed") {
          if (Device *p = m->seats[i].owner == "player" ? Dev(m->seats[i].nDev) : NULL)
            Unseat(*p);
          m->seats[i].owner = "closed";
        } else if (i < m->nMapPlayers && m->seats[i].owner == "closed") {
          m->seats[i].owner = "open";
        }
    }
    if (j["type"].kind == PortJson::STRING)
      m->type = j["type"].Str();
    if (j["speed"].kind == PortJson::STRING)
      m->speed = j["speed"].Str();
    if (j["key"].kind == PortJson::NUMBER)
      m->nKey = static_cast<int>(j["key"].Num());
    if (j["speedN"].kind == PortJson::NUMBER)
      m->nSpeed = static_cast<int>(j["speedN"].Num());
    if (j["rules"].kind == PortJson::STRING)
      m->bHyperion = j["rules"].Str() == "hyperion";
    if (j["mutators"].kind == PortJson::OBJECT)
      m->mut = MutatorsFromJson(j["mutators"]);
    if (j["name"].kind == PortJson::STRING)
      m->name = j["name"].Str();
  } else if (t == "mappic" && m) {
    // the map's picture, from the host: served at /maps/<match>.png, its address changed with
    // each so a page fetches the new one
    const std::vector<unsigned char> png = PortBase64Bytes(j["png"].Str());
    if (j["map"].Str() == m->map && !png.empty() && j["starts"].kind == PortJson::ARRAY) {
      const std::string path = "/maps/" + std::to_string(m->nId) + ".png";
      LanWeb::Serve(path, "image/png", std::vector<uint8_t>(png.begin(), png.end()));
      m->pic = path + "?v=" + std::to_string(++m->nPic);
      m->picStarts = PortJsonWrite(j["starts"]);
    }
  } else if (t == "slot" && m && m->state == "lobby") {
    const int i = static_cast<int>(j["slot"].Num(-1));
    if (i >= 0 && i < kPlayers) {
      Seat &s = m->seats[i];
      const std::string owner = j["owner"].Str(s.owner.c_str());
      if (owner == "player") {
        if (Device *p = Dev(static_cast<int>(j["dev"].Num())))
          Sit(*p, *m, i);
      } else {
        if (s.owner == "player")
          if (Device *p = Dev(s.nDev))
            Unseat(*p);
        s.owner = owner;
        s.nDev = 0;
      }
      if (j["race"].kind == PortJson::STRING)
        s.race = j["race"].Str();
      if (j["handicap"].kind == PortJson::NUMBER)
        s.nHandicap = HandicapClamp(static_cast<int>(j["handicap"].Num()));
      if (j["team"].kind == PortJson::NUMBER)
        s.nTeam = static_cast<int>(j["team"].Num());
      if (j["colour"].kind == PortJson::NUMBER)
        s.nColour = static_cast<int>(j["colour"].Num());
    }
  } else if (t == "start" && m) {
    m->nStarter = d.nId;
    err = Launch(*m, j["force"].b);
  } else if (t == "end" && m) {
    EndMatch(*m, "the host ended it");
  } else if (t == "drop" && m) {
    if (m->state != "live" && m->state != "starting") {
      Back(*m);
      g_matches.erase(m->nId);
    }
  } else if (t == "remove") {
    if (Device *p = Dev(static_cast<int>(j["dev"].Num()))) {
      SendJson(*p, "{\"t\":\"removed\"}");
      Gone(*p, "the host removed them");
    }
  } else if (t == "password") {
    g_password = NewPassword();
    Tonight("info", PT_("The room has a new password."));
  } else if (t == "settings") {
    // the console's Settings, as it keeps them
    std::string text = j["set"].kind == PortJson::OBJECT ? "" : "{}";
    PortJson parsed;
    const std::string raw = j["raw"].Str();
    if (!raw.empty() && PortJsonParse(raw.c_str(), &parsed)) {
      g_settings = parsed;
      g_settingsText = raw;
      SettingsSave();
    }
    if (g_settings["name"].kind == PortJson::STRING && !g_settings["name"].s.empty())
      g_name = g_settings["name"].s;
    ServeGameFiles();
  } else if (t == "stop") {
    if (j["when"].Str() == "after") {
      g_bStopAfter = true;
      Tonight("info", PT_("The server stops after the last match; nobody new can join."));
    } else {
      g_bQuit = true;
    }
  } else if (t == "run") {
    g_bRunning = true;
    g_bStopAfter = false;
  }
  if (!err.empty())
    SendJson(d, "{\"t\":\"error\",\"text\":" + PortJsonQuote(err) + "}");
  g_bDirty = true;
}

void Said(Device &d, const PortJson &j)
{
  const std::string t = j["t"].Str();
  if (t == "pong") {
    d.dwRtt = static_cast<uint32_t>(PortNowMs() - d.qwPingSent);
    return;
  }
  if (d.state.empty() || d.state == "refused" || d.state == "answered")
    return;
  if (t == "rtc") {
    // the WebRTC handshake between a browser and its seat on this machine, passed on
    StreamSeat *st = d.bWeb ? StreamFor(d.nId) : d.bStream ? StreamOn(d.nId) : NULL;
    Device *p = st == NULL ? NULL : d.bWeb ? Dev(st->nDev) : Dev(st->nFor);
    if (p != NULL) {
      std::string out = "{\"t\":\"rtc\"";
      if (j["type"].kind == PortJson::STRING)
        out += ",\"type\":" + PortJsonQuote(j["type"].Str());
      if (j["sdp"].kind == PortJson::STRING)
        out += ",\"sdp\":" + PortJsonQuote(j["sdp"].Str());
      if (j["want"].b)
        out += ",\"want\":true";
      SendJson(*p, out + "}");
    }
    return;
  }
  if (t == "streaming") {
    // a browser's seat's readings: its encoder, picture, frames and bits a second
    if (StreamSeat *st = d.bStream ? StreamOn(d.nId) : NULL)
      st->info = "{\"encoder\":" + PortJsonQuote(j["encoder"].Str()) + ",\"w\":" + Num(j["w"].Num()) + ",\"h\":" +
                 Num(j["h"].Num()) + ",\"fps\":" + Num(j["fps"].Num()) + ",\"kbps\":" + Num(j["kbps"].Num()) +
                 ",\"encMs\":" + Num(j["encMs"].Num()) + ",\"peer\":" + PortJsonQuote(j["peer"].Str()) +
                 ",\"score\":" + Bool(j["score"].b) + "}";
    g_bDirty = true;
    return;
  }
  if (d.bStream && t != "status" && t != "result" && t != "leave" && t != "watch" && t != "unwatch")
    return;
  if (t == "chat") {
    Match *m = MatchOf(d.nMatch ? d.nMatch : static_cast<int>(j["match"].Num()));
    if (m != NULL && !j["text"].Str().empty())
      m->chat.push_back(std::make_pair(d.bAdmin ? std::string("Host") : d.name, j["text"].Str()));
  } else if (t == "race") {
    d.race = PortLower(j["race"].Str("random"));
  } else if (t == "handicap") {
    d.nHandicap = HandicapClamp(static_cast<int>(j["handicap"].Num(100)));
  } else if (t == "ready") {
    d.bReady = j["on"].b;
  } else if (t == "sit") {
    if (Match *m = MatchOf(static_cast<int>(j["match"].Num())))
      Sit(d, *m, static_cast<int>(j["slot"].Num(-1)));
  } else if (t == "stand") {
    Unseat(d);
  } else if (t == "status") {
    // a browser's seat's frame is its browser's
    if (d.bStream) {
      const StreamSeat *st = StreamOn(d.nId);
      auto it = g_devices.find(st != NULL ? st->nFor : 0);
      if (it != g_devices.end())
        it->second.dwFrame = static_cast<uint32_t>(j["frame"].Num());
      g_bDirty = true;
      return;
    }
    d.dwFrame = static_cast<uint32_t>(j["frame"].Num());
    if (j["page"].kind == PortJson::STRING)
      d.page = j["page"].Str();
    // a page's player saying what the device is, where the page guessed wrong
    if (d.bWeb && j["kind"].kind == PortJson::STRING && !j["kind"].Str().empty())
      d.kind = j["kind"].Str();
    // a page's player choosing to play in the tab or streamed: its seat goes the new way
    const std::string route = j["route"].Str();
    if (d.bWeb && (route == "stream" || route == "tab") && route != d.route && d.state != "playing") {
      d.route = route;
      if (StreamSeat *w = StreamFor(d.nId))
        StopStream(*w);
      if (route == "tab")
        StartTab(d);
      else if (d.state == "lobby")
        StartStream(d);
      g_bDirty = true;
    }
    if (j["state"].kind == PortJson::STRING && (j["state"].s == "joined" || j["state"].s == "installing"))
      d.state = j["state"].s;
  } else if (t == "note" && d.bWeb) {
    // what a page saw of its game's connections (play.js), for this log: a browser keeps its own
    Log("%s's page: %s", d.name.c_str(), j["text"].Str().substr(0, 300).c_str());
  } else if (t == "result") {
    // a seat's game ended: its outcome, the first one in deciding the match's line
    if (Match *m = MatchOf(d.nMatch)) {
      const std::string res = j["text"].Str();
      if (!res.empty() && m->result.empty())
        m->result = res;
    }
    // a browser's seat's game: its browser is back to the room, and the seat goes on waiting
    // for the next, unless the page has gone
    if (StreamSeat *st = d.bStream ? StreamOn(d.nId) : NULL) {
      d.nMatch = 0;
      auto it = g_devices.find(st->nFor);
      if (it != g_devices.end()) {
        it->second.state = it->second.bGone ? it->second.state : "joined";
        it->second.nMatch = 0;
        it->second.dwFrame = 0;
        it->second.bReady = false;
        if (it->second.bGone)
          StopStream(*st);
      }
      g_bDirty = true;
      return;
    }
    d.state = "joined";
    d.nMatch = 0;
    d.dwFrame = 0;
  } else if (t == "watch" && d.bWeb) {
    WatchInBrowser(d, j);
  } else if (t == "watch") {
    // a live match, from a device not playing in it, while the room lets people watch
    Match *m = MatchOf(static_cast<int>(j["match"].Num()));
    if (m == NULL || (m->state != "live" && m->state != "starting") || d.state == "playing") {
      SendJson(d, "{\"t\":\"error\",\"text\":" + PortJsonQuote(PT_("That match is not being played now.")) + "}");
    } else if (!WatchingOn() && !d.bAdmin) {
      SendJson(d, "{\"t\":\"error\",\"text\":" + PortJsonQuote(PT_("The host has turned watching off.")) + "}");
    } else {
      Unseat(d);
      m->watchers.push_back(d.nId);
      m->sent.erase(d.nId);
      d.state = "watching";
      d.nMatch = m->nId;
      Tonight("watch", (d.bAdmin ? std::string("The host") : d.name) + " is watching " + m->name + ".", {d.nId});
    }
  } else if (t == "unwatch") {
    Unseat(d);
    // a page's: the seat that watched for it goes
    if (StreamSeat *w = d.bWeb ? StreamFor(d.nId) : NULL)
      if (w->nWatch != 0) {
        // a tab's seat leaves the match it shows, as Quit Mission leaves
        if (Device *seat = w->bTab ? Dev(w->nDev) : NULL)
          if (seat->state == "watching")
            SendJson(*seat, "{\"t\":\"end\",\"match\":" + std::to_string(seat->nMatch) + "}");
        EndWatch(*w);
      }
    if (d.state == "watching") {
      d.state = d.bStream ? "stream" : "joined";
      d.nMatch = 0;
    }
  } else if (t == "leave") {
    Gone(d, "");
    return;
  } else if (d.bAdmin) {
    Admin(d, t, j);
    return;
  }
  g_bDirty = true;
}

void Heard(Device &d, const std::string &text)
{
  PortJson j;
  if (!PortJsonParse(text.c_str(), &j))
    return;
  if (j["t"].Str() == "hello") {
    Hello(d, j);
    // a device let in may send larger frames from now on: its match's feed (LanNet::Trust)
    if (!d.state.empty() && d.state != "refused")
      LanNet::Trust(d.nConn);
  } else {
    Said(d, j);
  }
}

}  // namespace LanRoom
