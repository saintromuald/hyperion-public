// port_lanclient.cpp -- a Hyperion in a LAN room; port_lanclient.h says what it is.
// The connection is one of port_lannet.cpp's, framed, with the room's JSON in ROOM frames and
// a launch's map in a ROOM_FILE. Presentation and transport only.
#include "port_lanclient.h"
#include "../platform/port_json.h"  // PortJsonQuote
#include "../platform/port_util.h"  // PortNowMs
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include "../../../hyperion/version.h"  // HYPERION_VERSION_TEXT
#include "../../h/hxsim.h"              // HX_SIM_REVISION
#include "../platform/port_paths.h"  // PortDataFolder, PortCommandOptions, PortOptions
#include "../front/port_frontscreens.h"       // FrontKit::ProfileCurrent
#include "../front/port_text.h"
#include "port_lannet.h"
#include "port_lanproto.h"
#include "port_lanstream.h"
#include "../data/port_mods.h"
#include "../data/port_stormapi.h"  // SFileOpenFileEx, SFileReadFile
#include "../../gen/globals117.h"

using namespace LanProto;

namespace {

int g_nConn = -1;
bool g_bIn, g_bAdmin, g_bWaiting, g_bLost, g_bEndAsked;
std::string g_address, g_refused, g_fix, g_mapPath;
PortJson g_room;
int g_nYou;
std::vector<PortJson> g_launches;
int g_nFollow;  // the room's "follow": the match a browser's watching seat is to show
std::vector<std::string> g_notes;
std::vector<std::vector<uint8_t>> g_feed;  // a watched match's feed, as the room sent it
std::vector<LanPicture> g_pictures;        // the copies' pictures, the latest of each army
uint32_t g_nPictureSeq;

void KeepPicture(const std::vector<uint8_t> &data)
{
  Reader r(data);
  r.B();
  LanPicture p;
  p.nMatch = static_cast<int>(r.D());
  p.nPlayer = r.B();
  p.name = r.S();
  p.w = r.W();
  p.h = r.W();
  p.rgb = r.Bytes();
  if (r.bBad || p.rgb.size() != static_cast<size_t>(p.w) * p.h * 3)
    return;
  p.nSeq = ++g_nPictureSeq;
  for (LanPicture &q : g_pictures)
    if (q.nMatch == p.nMatch && q.nPlayer == p.nPlayer) {
      q = p;
      return;
    }
  if (g_pictures.size() >= 64)
    g_pictures.erase(g_pictures.begin());
  g_pictures.push_back(p);
}
int g_nMade;  // the match the room made for the console, once
uint32_t g_dwFrame, g_dwFrameSent;
uint64_t g_qwFrameSent, g_qwLastQuery;
struct Heard {
  PortJson info;
  uint64_t qwSeen;
  int nPort;
};
std::map<std::string, Heard> g_heard;

const char *OsName(void)
{
#if defined(_WIN32)
  return "Windows";
#elif defined(__APPLE__)
  return "macOS";
#else
  return "Linux";
#endif
}

const char *ArchName(void)
{
#if defined(_M_ARM64) || defined(__aarch64__)
  return "arm64";
#else
  return "x64";
#endif
}

// The design's kinds of device: a Windows PC, a Linux PC, a Linux ARM board.
const char *KindName(void)
{
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "mac";
#elif defined(__aarch64__)
  return "arm";
#else
  return "linux";
#endif
}

std::string ModText(void)
{
  const std::vector<PortMod> &mods = PortMods();
  const int i = PortModActive();
  if (i < 0 || i >= static_cast<int>(mods.size()) || mods[i].bBuiltIn)
    return std::string();
  return mods[i].name + (mods[i].version.empty() ? "" : " " + mods[i].version);
}

void SaveMap(const std::vector<uint8_t> &data)
{
  Reader r(data);
  std::string name = r.S();
  const std::vector<uint8_t> bytes = r.Bytes();
  if (r.bBad || name.empty() || name.find("..") != std::string::npos || name.find_first_of("\\/:") != std::string::npos)
    return;
  std::string folder = std::string(PortDataFolder()) + "lan";
  CreateDirectoryA(folder.c_str(), NULL);
  folder += "\\maps";
  CreateDirectoryA(folder.c_str(), NULL);
  const std::string path = folder + "\\" + name;
  FILE *f = fopen(path.c_str(), "wb");
  if (f == NULL)
    return;
  fwrite(bytes.data(), 1, bytes.size(), f);
  fclose(f);
  g_mapPath = path;
}

}  // namespace

// The room's copy of a match says the token the room started it with (--lan-copy <match>:<token>),
// and so does a browser's player's seat on the host (--lan-stream <device>:<token>), and one in
// the player's browser the token the room gave its page (--lan-tab <device>:<token>).
static std::string CopyToken(void)
{
  const char *psz = strchr(PortCommandOptions()->szLanCopy, ':');
  const char *pszSeat = strchr(PortCommandOptions()->szLanStream, ':');
  const char *pszTab = strchr(PortCommandOptions()->szLanTab, ':');
  return psz != NULL       ? ",\"copy\":" + PortJsonQuote(psz + 1)
         : pszSeat != NULL ? ",\"stream\":" + PortJsonQuote(pszSeat + 1)
         : pszTab != NULL  ? ",\"tab\":" + PortJsonQuote(pszTab + 1)
                           : std::string();
}

std::string LanPlayerName(void)
{
  const PortOptions *o = PortCommandOptions();
  if (o->szLanName[0])
    return o->szLanName;
  const std::string profile = FrontKit::ProfileCurrent();
  if (!profile.empty())
    return profile;
  return gLocalPlayerName[0] ? gLocalPlayerName : "Player";
}

const std::string &LanFingerprint(void)
{
  static std::string s_fp;
  if (!s_fp.empty())
    return s_fp;
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const void *pv, size_t cb) {
    const uint8_t *p = static_cast<const uint8_t *>(pv);
    for (size_t i = 0; i < cb; ++i)
      h = (h ^ p[i]) * 1099511628211ull;
  };
  // the files the game's rules are read from, as Storm gives them (the active mod's, 1.17's
  // MPQs or a Remastered install's), so copies agree whenever their rules do whatever their
  // data comes from: the two sources give these byte for byte. The tilesets are left
  // out: Remastered's differ from 1.17's only in groups 1.17 left empty, which only a map
  // made with Remastered's newer tiles names, and a copy on 1.17's MPQs refuses such a map.
  static const char *const kRules[] = {
      "arr\\units.dat",    "arr\\weapons.dat",  "arr\\flingy.dat",
      "arr\\sprites.dat",  "arr\\images.dat",   "arr\\upgrades.dat",
      "arr\\techdata.dat", "arr\\orders.dat",   "arr\\sfxdata.dat",
      "arr\\portdata.dat", "arr\\mapdata.dat",  "scripts\\iscript.bin",
      "scripts\\aiscript.bin", "scripts\\bwscript.bin"};
  std::vector<uint8_t> ab;
  for (const char *pszName : kRules) {
    void *hFile = NULL;
    if (!SFileOpenFileEx(NULL, pszName, 0, &hFile)) {
      mix(pszName, strlen(pszName));
      continue;
    }
    const DWORD cb = SFileGetFileSize(hFile, NULL);
    ab.resize(cb);
    DWORD dwRead = 0;
    if (cb == 0 || !SFileReadFile(hFile, ab.data(), cb, &dwRead, NULL))
      dwRead = 0;
    const uint64_t qwSize = dwRead;
    mix(&qwSize, sizeof qwSize);
    mix(ab.data(), dwRead);
    SFileCloseFile(hFile);
  }
  char sz[16];
  snprintf(sz, sizeof sz, "%08X", static_cast<unsigned>(h ^ (h >> 32)));
  s_fp = sz;
  return s_fp;
}

std::vector<LanRoomFound> LanClientRooms(void)
{
  const uint64_t qwNow = PortNowMs();
  LanNet::Pump();
  for (const LanNet::Answer &a : LanNet::Answers()) {
    Reader r(a.advert);
    if (r.W() != kProtocol || r.B() != ADVERT_ROOM)
      continue;
    PortJson info;
    if (!PortJsonParse(r.S().c_str(), &info))
      continue;
    Heard &h = g_heard[LanNet::AddrText(a.dwAddr) + ":" + std::to_string(static_cast<int>(info["port"].Num()))];
    h.info = info;
    h.qwSeen = qwNow;
    h.nPort = static_cast<int>(info["port"].Num(LanNet::GamePort()));
  }
  if (qwNow - g_qwLastQuery >= 500) {
    g_qwLastQuery = qwNow;
    LanNet::Query();
  }
  std::vector<LanRoomFound> out;
  for (auto it = g_heard.begin(); it != g_heard.end();) {
    if (qwNow - it->second.qwSeen > 5000) {
      it = g_heard.erase(it);
      continue;
    }
    LanRoomFound f;
    f.addr = it->first.substr(0, it->first.find(':'));
    f.nPort = it->second.nPort;
    f.info = it->second.info;
    const PortJson &i = f.info;
    const std::string version = i["version"].Str(), mod = i["mod"].Str(), fp = i["fp"].Str();
    if (version != HYPERION_VERSION_TEXT || static_cast<int>(i["sim"].Num()) != HX_SIM_REVISION) {
      f.mark = PortText("Another version");
      // {0} the room's version, {1} this copy's
      f.why = PortTextF("Its host plays {0}, and this copy is {1}: the two compute the game differently.",
                        {version, HYPERION_VERSION_TEXT});
      f.fix = PortText("Its host's page offers its version to install beside this one.");
    } else if (mod != ModText()) {
      f.mark = PortText("Needs a mod");
      // {0} the mod's name
      f.why = mod.empty() ? PortTextF("This copy has the mod {0} active, and the room plays without one.", {ModText()})
                          : PortTextF("Its host plays the mod {0}, which this copy does not have active.", {mod});
      f.fix = mod.empty() ? std::string(PortText("Turn it off under Mods, then join."))
                          : PortTextF("Activate {0} under Mods, then join.", {mod});
    } else if (!fp.empty() && fp != LanFingerprint()) {
      f.mark = PortText("Other game data");
      // {0} the room's fingerprint of its game data
      f.why = PortTextF("Its game data is not this copy's (fingerprint {0}).", {fp});
      f.fix = PortText("Its host's page has the game data to install.");
    } else {
      f.bOk = true;
      f.mark = PortText("Plays here");
    }
    out.push_back(f);
    ++it;
  }
  return out;
}

bool LanClientJoin(const char *pszAddr, int nPort, const char *pszPassword, const char *pszKey)
{
  LanClientLeave();
  uint32_t dwAddr;
  if (!LanNet::AddrParse(pszAddr, &dwAddr))
    return false;
  g_nConn = LanNet::Connect(dwAddr, nPort > 0 ? nPort : LanNet::GamePort(), 2000);
  if (g_nConn < 0)
    return false;
  g_address = pszAddr;
  g_bWaiting = true;
  const std::string hello = "{\"t\":\"hello\",\"name\":" + PortJsonQuote(LanPlayerName()) + ",\"machine\":" + PortJsonQuote(LanNet::HostName()) +
                            ",\"os\":" + PortJsonQuote(OsName()) + ",\"arch\":" + PortJsonQuote(ArchName()) + ",\"kind\":" + PortJsonQuote(KindName()) +
                            ",\"route\":\"install\",\"version\":" + PortJsonQuote(HYPERION_VERSION_TEXT) +
                            ",\"sim\":" + std::to_string(HX_SIM_REVISION) + ",\"fp\":" + PortJsonQuote(LanFingerprint()) +
                            ",\"mod\":" + PortJsonQuote(ModText()) + ",\"password\":" + PortJsonQuote(pszPassword ? pszPassword : "") +
                            ",\"key\":" + PortJsonQuote(pszKey ? pszKey : "") + CopyToken() + "}";
  LanNet::Send(g_nConn, ROOM, hello.data(), hello.size());
  return true;
}

void LanClientLeave(void)
{
  if (g_nConn >= 0) {
    const std::string bye = "{\"t\":\"leave\"}";
    LanNet::Send(g_nConn, ROOM, bye.data(), bye.size());
    LanNet::Drop(g_nConn);
  }
  g_nConn = -1;
  g_bIn = g_bAdmin = g_bWaiting = g_bLost = false;
  g_refused.clear();
  g_fix.clear();
  g_room = PortJson();
  g_nYou = 0;
  g_launches.clear();
}

bool LanClientIn(void)
{
  return g_nConn >= 0 && g_bIn;
}

bool LanClientAdmin(void)
{
  return g_bIn && g_bAdmin;
}

bool LanClientWaiting(void)
{
  return g_nConn >= 0 && g_bWaiting;
}

const std::string &LanClientRefused(std::string *pFix)
{
  if (pFix)
    *pFix = g_fix;
  return g_refused;
}

bool LanClientLost(void)
{
  return g_bLost;
}

const char *LanClientAddress(void)
{
  return g_address.c_str();
}

void LanClientPump(void)
{
  if (g_nConn < 0)
    return;
  LanNet::Pump();
  uint8_t bKind;
  std::vector<uint8_t> data;
  while (g_nConn >= 0 && LanNet::Receive(g_nConn, &bKind, &data)) {
    if (bKind == ROOM_FILE) {
      SaveMap(data);
      continue;
    }
    if (bKind == ROOM_FEED) {
      if (!data.empty() && data[0] == FEED_PICTURE)
        KeepPicture(data);
      else
        g_feed.push_back(data);
      continue;
    }
    if (bKind != ROOM)
      continue;
    PortJson j;
    if (!PortJsonParse(std::string(data.begin(), data.end()).c_str(), &j))
      continue;
    const std::string t = j["t"].Str();
    if (t == "room") {
      g_room = j;
      g_nYou = static_cast<int>(j["you"].Num(g_nYou));
    } else if (t == "ping") {
      const std::string pong = "{\"t\":\"pong\"}";
      LanNet::Send(g_nConn, ROOM, pong.data(), pong.size());
    } else if (t == "welcome") {
      g_bIn = true;
      g_bWaiting = false;
      g_bAdmin = j["admin"].b;
      g_nYou = static_cast<int>(j["you"].Num());
    } else if (t == "refused") {
      g_bWaiting = false;
      const std::string why = j["why"].Str(), fix = j["fix"].Str();
      g_refused = PortText(why.c_str());
      g_fix = PortText(fix.c_str());
    } else if (t == "launch") {
      g_launches.push_back(j);
    } else if (t == "follow") {
      g_nFollow = static_cast<int>(j["match"].Num());
    } else if (t == "end") {
      g_bEndAsked = true;
    } else if (t == "made") {
      g_nMade = static_cast<int>(j["match"].Num());
    } else if (t == "error") {
      const std::string text = j["text"].Str();
      g_notes.push_back(PortText(text.c_str()));
    } else if (t == "removed") {
      g_notes.push_back(PortText("The host removed you from the room."));
    } else if (t == "closed") {
      g_notes.push_back(PortText("The room has closed."));
    } else if (t == "rtc") {
      LanStreamSignal(j);
    }
  }
  if (g_nConn >= 0 && !LanNet::Alive(g_nConn)) {
    LanNet::Drop(g_nConn);
    g_nConn = -1;
    g_bLost = g_bIn;
    g_bIn = g_bAdmin = g_bWaiting = false;
  }
  // this player's game's frame, every few seconds while it runs
  const uint64_t qwNow = PortNowMs();
  if (g_nConn >= 0 && g_dwFrame != g_dwFrameSent && qwNow - g_qwFrameSent >= 2000) {
    g_qwFrameSent = qwNow;
    g_dwFrameSent = g_dwFrame;
    const std::string s = "{\"t\":\"status\",\"frame\":" + std::to_string(g_dwFrame) + "}";
    LanNet::Send(g_nConn, ROOM, s.data(), s.size());
  }
  // a browser's player's seat on the host: its peer's events, and the browser's input
  LanStreamPump(LanClientIn());
}

const PortJson &LanClientRoom(void)
{
  return g_room;
}

int LanClientYou(void)
{
  return g_nYou;
}

void LanClientSay(const std::string &json)
{
  if (g_nConn >= 0)
    LanNet::Send(g_nConn, ROOM, json.data(), json.size());
}

void LanClientSend(uint8_t bKind, const std::vector<uint8_t> &data)
{
  if (g_nConn >= 0)
    LanNet::Send(g_nConn, bKind, data.data(), data.size());
}

const std::vector<LanPicture> &LanClientPictures(void)
{
  return g_pictures;
}

bool LanClientTakeFeed(std::vector<uint8_t> *pOut)
{
  if (g_feed.empty())
    return false;
  pOut->swap(g_feed.front());
  g_feed.erase(g_feed.begin());
  return true;
}

std::vector<std::string> LanClientNotes(void)
{
  std::vector<std::string> out;
  out.swap(g_notes);
  return out;
}

bool LanClientTakeLaunch(PortJson *pOut)
{
  if (g_launches.empty())
    return false;
  *pOut = g_launches.front();
  g_launches.erase(g_launches.begin());
  return true;
}

int LanClientFollow(void)
{
  return g_nFollow;
}

void LanClientFollowed(void)
{
  g_nFollow = 0;
}

const std::string &LanClientMapPath(void)
{
  return g_mapPath;
}

void LanClientFrame(uint32_t dwFrame)
{
  g_dwFrame = dwFrame;
}

int LanClientTakeMade(void)
{
  const int n = g_nMade;
  g_nMade = 0;
  return n;
}

bool LanClientTakeEnd(void)
{
  const bool b = g_bEndAsked;
  g_bEndAsked = false;
  return b;
}

void LanClientResult(const char *pszText)
{
  g_dwFrame = g_dwFrameSent = 0;
  LanClientSay("{\"t\":\"result\",\"text\":" + PortJsonQuote(pszText ? pszText : "") + "}");
}
