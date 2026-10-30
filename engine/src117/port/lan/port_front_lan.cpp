// port_front_lan.cpp -- the front end in a LAN room (docs/modules/lan.md):
// Multiplayer, the room as a guest sees it, a match's lobby on
// either side, and the pieces the host's console shares (port_front_lan.h). Presentation only.
// The room is a process of its own (Hyperion --room, port_lanroom.cpp); Host a Room starts it
// here and this copy talks to it as its console, over the connection a player's copy uses
// (port_lanclient.cpp). A match's lobby is the skirmish set-up, hosted (SkirmishHost nRoom):
// the host's has seats open to the room's people, a guest's shows the host's set-up and their
// own race. A launched match goes to 1.17's chat room (port_lanlobby.cpp), and the front end
// comes back to the console or the room when it ends. --join <address>[:<port>] joins the room
// as the front end comes up, with --room-password; --race gives this player's race in its
// lobbies, and --lan-ready says Ready in each one it sits in (a test's seat, with nobody at it).
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <deque>
#include <map>
#include <random>
#include <string>
#include <SDL3/SDL.h>
#include "../../../hyperion/version.h"  // HYPERION_VERSION_TEXT
#include "../platform/port_paths.h"
#include "../data/port_chatfilter.h"  // PortChatClean
#include "port_front_lan.h"
#include "../front/port_frontscreens.h"
#include "../platform/port_json.h"
#include "../platform/port_util.h"  // PortSpeedFrameMs
#include "port_lanclient.h"
#include "port_lanfeed.h"
#include "port_lanlobby.h"
#include "port_lannet.h"
#include "port_lanproto.h"
#include "../data/port_mods.h"
#include "../data/port_hxm.h"  // PortHxmIsMap, PortHxmCompiled
#include "../front/port_frontlobby.h"  // MapPictureRead, MapMinimapRgba
#include "../front/port_ui.h"
#include "grid.h"  // IsBroodwar
#include "glues.h"   // mem_free
#include "maphdr.h"  // get_map_chk
#include "../gameplay/port_handicap.h"  // HandicapClamp
#include "../gameplay/port_mutators.h"  // MutatorsJson, MutatorsFromJson

// 0x66FD4C ghScenArchive, spelled as src117/lang/maphdr.cpp spells it: the scenario archive,
// which a running game keeps open as its map's (port_labwall.cpp reads a map alike).
extern HANDLE ghScenArchive;

namespace FrontKit {
namespace Lan {

const UiColor kStream = KitRgb(0x66d5fe);
const UiColor kInstall = KitRgb(0x7bd88f);
const UiColor kGood = KitRgb(0x7bd88f);
const UiColor kFair = KitRgb(0xffd36b);
const UiColor kPoor = KitRgb(0xff8a65);
const UiColor kOn = KitRgb(0x6df08d);
const UiColor kOff = KitRgb(0x4a5d70);
const UiColor kBad = KitRgb(0xff6b5b);

}  // namespace Lan
}  // namespace FrontKit

using namespace FrontKit;
using namespace FrontKit::Lan;

namespace {

const UiColor kInkPlayer = KitRgb(0x64d0f9);  // lobby.css --lb-ink-player
const char *const kRaceIds[] = {"zerg", "terran", "protoss"};
const char *const kSpeedIds[] = {"slowest", "slower", "slow", "normal", "fast", "faster", "fastest"};

enum { VIEW_MULTIPLAYER, VIEW_CONSOLE, VIEW_LOBBY, VIEW_ROOM };
int g_nView;
int g_nResult;
Uint64 g_qwWatchAsked;  // when this machine asked to watch a match
int g_nLobbyMatch;  // the match whose lobby is shown
bool g_bLobbyOpened;
bool g_bCameBack;   // a room's match ended and the front end came back to its screen
bool g_bJoinOption; // --join joined a room; its screen opens once it lets this copy in
bool g_bStopAsked;  // this host stopped its room: its closing is said as the host's own

// Multiplayer's own
std::string g_addr = "192.168.";
bool g_bAddrFocus;
std::string g_joining;         // the room being joined, shown until it answers
Uint64 g_qwJoinAt;
std::string g_pwFor, g_pwAddr;  // the room asking for its password
int g_nPwPort;
std::string g_password;
std::string g_refusal, g_refusalFix;
std::vector<LanRoomFound> g_rooms;

// the lobby's chat line
std::string g_chat;
bool g_bChatFocus;

// ---------------------------------------------------------------------------
// The room's server, a process of its own.

struct Server {
  HANDLE hProcess = NULL;
  std::string key, password;
  Uint64 qwStarted = 0, qwTried = 0;
  bool bReported = false;
} g_srv;

std::string RandomHex(int nBytes)
{
  std::random_device rd;
  std::string s;
  char sz[4];
  for (int i = 0; i < nBytes; ++i) {
    SDL_snprintf(sz, sizeof sz, "%02x", static_cast<unsigned>(rd() & 0xFF));
    s += sz;
  }
  return s;
}

bool ProcessAlive(void)
{
  if (g_srv.hProcess == NULL)
    return false;
  if (WaitForSingleObject(g_srv.hProcess, 0) == WAIT_TIMEOUT)
    return true;
  DWORD dwCode = 0;
  GetExitCodeProcess(g_srv.hProcess, &dwCode);
  CloseHandle(g_srv.hProcess);
  g_srv.hProcess = NULL;
  FrontLog("lan: the room's server ended (%lu)\n", static_cast<unsigned long>(dwCode));
  if (dwCode == 2)
    Toast(PortText("The server could not start"),
          PortText("Another room or game on this machine has its port. Close it, then start the server."));
  return false;
}

// A room this data folder's console started, still open: Hyperion closed or restarted while its
// room waited a minute for its console (port_lanroom.cpp, the room's note lan_room.json). This
// console takes it back, by its process and its key, instead of starting a second room, which
// could not have the port. False when there is no such room.
bool TakeBackRoom(void)
{
  std::string text;
  if (FILE *f = fopen(PortLogFile("lan_room.json"), "r")) {
    char sz[256];
    const size_t n = fread(sz, 1, sizeof sz - 1, f);
    fclose(f);
    text.assign(sz, n);
  }
  PortJson j;
  if (text.empty() || !PortJsonParse(text.c_str(), &j) || j["key"].Str().empty())
    return false;
  const DWORD dwPid = static_cast<DWORD>(j["pid"].Num());
  HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwPid);
  if (h == NULL)
    return false;
  bool bOurs = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
#ifdef _WIN32
  // the process is a Hyperion, not another that has its number since
  char szImage[MAX_PATH] = "", szExe[MAX_PATH] = "";
  DWORD cch = MAX_PATH;
  GetModuleFileNameA(NULL, szExe, MAX_PATH);
  bOurs = bOurs && QueryFullProcessImageNameA(h, 0, szImage, &cch) && _stricmp(szImage, szExe) == 0;
#endif
  if (!bOurs) {
    CloseHandle(h);
    return false;
  }
  g_srv.hProcess = h;
  g_srv.key = j["key"].Str();
  g_srv.qwStarted = SDL_GetTicks();
  g_srv.qwTried = 0;
  g_srv.bReported = false;
  FrontLog("lan: took back the room's server, process %lu\n", static_cast<unsigned long>(dwPid));
  return true;
}

// Keeps the console connected to the server it started.
void ServerPump(void)
{
  // once, as the LAN screens first open: a room of this folder's still open is this console's
  static bool s_bLooked;
  if (!s_bLooked) {
    s_bLooked = true;
    if (!ProcessAlive() && TakeBackRoom())
      Toast(PortText("Your room is still open"), PortText("This console is back in it."));
  }
  const bool bAlive = ProcessAlive();
  if (!bAlive || g_srv.key.empty())
    return;
  const Uint64 qwNow = SDL_GetTicks();
  if (!LanClientIn() && !LanClientWaiting() && qwNow - g_srv.qwTried > 500) {
    g_srv.qwTried = qwNow;
    LanClientJoin("127.0.0.1", 0, "", g_srv.key.c_str());
  }
  if (LanClientAdmin()) {
    const std::string pw = Room()["passwordText"].Str();
    if (!pw.empty())
      g_srv.password = pw;
    if (!g_srv.bReported) {
      g_srv.bReported = true;
      FrontLog("lan: the console is in its room\n");
    }
  }
}

// ---------------------------------------------------------------------------
// Pieces.

// The deque's vectors live until the frame's layout is drawn.
std::deque<std::vector<float>> g_sparks;
std::map<int, std::vector<float>> g_rtts;
Uint64 g_qwRttSampled;

void DrawGlyph(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  const float u = w / 24.0f, t = fmaxf(1.0f, 1.6f * u);
  const UiColor c = d.look.top;
  auto rect = [&](float rx, float ry, float rw, float rh, float r) { UiRing(x + rx * u, y + ry * u, rw * u, rh * u, c, r * u, t); };
  auto line = [&](float x0, float y0, float x1, float y1) { UiLine(x + x0 * u, y + y0 * u, x + x1 * u, y + y1 * u, c, t); };
  switch (d.n) {
  case 1:  // laptop
    rect(5, 5, 14, 10, 1.2f);
    line(2.5f, 18.5f, 21.5f, 18.5f);
    line(21.5f, 18.5f, 20, 15);
    line(2.5f, 18.5f, 4, 15);
    break;
  case 2:  // phone
    rect(7, 2.5f, 10, 19, 2);
    line(11, 18.5f, 13, 18.5f);
    break;
  case 3:  // tablet
    rect(4, 3, 16, 18, 2);
    line(11, 18, 13, 18);
    break;
  case 4:  // board
    rect(4, 4, 16, 16, 1.5f);
    rect(9, 9, 6, 6, 0);
    for (float p : {9.0f, 15.0f}) {
      line(p, 4, p, 2);
      line(p, 22, p, 20);
      line(4, p, 2, p);
      line(22, p, 20, p);
    }
    break;
  case 5:  // a television, or any other browser
    rect(2.5f, 4, 19, 13, 1.2f);
    line(8, 21, 12, 17);
    line(12, 17, 16, 21);
    break;
  case 6:  // the host
    rect(6, 2.5f, 12, 19, 1.5f);
    line(9, 6.5f, 15, 6.5f);
    line(9, 9.5f, 15, 9.5f);
    UiFill(x + 11.3f * u, y + 16.8f * u, 1.4f * u, 1.4f * u, c, 0.7f * u);
    break;
  default:  // a desktop
    rect(3, 4, 18, 12, 1.5f);
    line(9, 20, 15, 20);
    line(12, 16, 12, 20);
    break;
  }
}

int GlyphOf(const PortJson &d)
{
  if (d["admin"].b)
    return 6;
  const std::string k = d["kind"].Str();
  return k == "linux" || k == "mac" ? 1 : k == "phone" ? 2 : k == "tablet" ? 3 : k == "arm" ? 4 : k == "browser" ? 5 : 0;
}

void DrawSpark(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  if (d.n < 0 || d.n >= static_cast<int>(g_sparks.size()))
    return;
  const std::vector<float> &v = g_sparks[d.n];
  UiFill(x, y, w, h, KitRgb(0x3980bc, 0.08f), Sc(2));
  if (v.size() < 2 || d.f <= 0)
    return;
  float px = 0, py = 0;
  for (size_t i = 0; i < v.size(); ++i) {
    const float fx = x + w * static_cast<float>(i) / static_cast<float>(v.size() - 1);
    const float fy = y + h - h * fminf(1.0f, fmaxf(0.0f, v[i] / d.f));
    if (i > 0)
      UiLine(px, py, fx, fy, d.look.top, Sc(1.5f));
    px = fx;
    py = fy;
  }
}

// ---------------------------------------------------------------------------
// The ask, and the toasts.

struct AskState {
  std::string id, title, body;
  std::vector<AskChoice> choices;
  bool bUp = false;
  int nChosen = -1;
} g_ask;

struct ToastLine {
  std::string title, line;
  Uint64 qwAt;
};
std::deque<ToastLine> g_toasts;

}  // namespace

namespace FrontKit {
namespace Lan {

const PortJson &Room(void)
{
  return LanClientRoom();
}

const PortJson *DeviceOf(int nId)
{
  const PortJson &ds = Room()["devices"];
  for (size_t i = 0; i < ds.size(); ++i)
    if (static_cast<int>(ds.At(i)["id"].Num()) == nId)
      return &ds.At(i);
  return NULL;
}

const PortJson *MatchOf(int nId)
{
  const PortJson &ms = Room()["matches"];
  for (size_t i = 0; i < ms.size(); ++i)
    if (static_cast<int>(ms.At(i)["id"].Num()) == nId)
      return &ms.At(i);
  return NULL;
}

int You(void)
{
  return LanClientYou();
}

std::string KindName(const PortJson &d)
{
  if (d["admin"].b)
    return PortText("This machine");
  const std::string k = d["kind"].Str();
  return k == "windows" ? "Windows" : k == "linux" ? "Linux" : k == "arm" ? "Linux ARM" : k == "mac" ? "Mac"
         : k == "phone" ? PortText("Phone") : k == "tablet" ? PortText("Tablet") : PortText("Browser");
}

// A browser's player plays in their tab (W3) or streamed from the host; a PC plays installed.
const char *RouteName(const PortJson &d)
{
  const std::string r = d["route"].Str();
  return r == "stream" ? PortText("Streamed") : r == "tab" ? PortText("Browser") : PortText("Installed");
}

std::string VersionName(const std::string &v)
{
  return v.compare(0, 10, "Hyperion-v") == 0 ? "Hyperion " + v.substr(10) : v;
}

std::string Count(int n, const char *pszOne, const char *pszMany)
{
  return std::to_string(n) + " " + (n == 1 ? pszOne : pszMany != NULL ? pszMany : std::string(pszOne) + "s");
}

std::string Clock(double f)
{
  const int s = f > 0 ? static_cast<int>(f) : 0;
  char sz[32];
  SDL_snprintf(sz, sizeof sz, "%d:%02d", s / 60, s % 60);
  return sz;
}

std::string Ago(double f)
{
  const int s = static_cast<int>(f);
  if (s < 90)
    return PortText("a minute");
  if (s < 3600)
    return PortTextF("{0} min", {std::to_string((s + 30) / 60)});
  char sz[8];
  SDL_snprintf(sz, sizeof sz, "%02d", s / 60 % 60);
  return PortTextF("{0} h {1} min", {std::to_string(s / 3600), sz});
}

// A match's game time, at its speed.
double GameSeconds(const PortJson &m)
{
  return m["frame"].Num() * PortSpeedFrameMs(static_cast<int>(m["speedN"].Num(6))) / 1000.0;
}

std::vector<const PortJson *> People(void)
{
  std::vector<const PortJson *> out;
  const PortJson &ds = Room()["devices"];
  for (size_t i = 0; i < ds.size(); ++i) {
    const PortJson &d = ds.At(i);
    if (d["admin"].b && d["state"].Str() == "joined")
      continue;
    out.push_back(&d);
  }
  return out;
}

void Doing(const PortJson &d, std::string *pLine, std::string *pDetail)
{
  const std::string st = d["state"].Str();
  const PortJson *m = MatchOf(static_cast<int>(d["match"].Num()));
  const int nId = static_cast<int>(d["id"].Num());
  pDetail->clear();
  if (st == "playing" && m != NULL) {
    // {0} the match's name
    *pLine = PortTextF("Playing {0}", {(*m)["name"].Str()});
    std::string them;
    const PortJson &seats = (*m)["seats"];
    for (size_t i = 0; i < seats.size(); ++i) {
      const PortJson &s = seats.At(i);
      const std::string o = s["owner"].Str();
      if ((o != "player" && o != "computer") || static_cast<int>(s["dev"].Num()) == nId)
        continue;
      const std::string who = o == "computer" ? std::string(PortText("a computer")) : s["name"].Str();
      them = them.empty() ? who : PortTextF("{0} and {1}", {them, who});
    }
    *pDetail = (them.empty() ? std::string() : PortTextF("Against {0}", {them}) + kDot) + (*m)["mapName"].Str() + kDot +
               Clock(GameSeconds(*m));
  } else if (st == "watching") {
    *pLine = PortTextF("Watching {0}", {m != NULL ? (*m)["name"].Str() : std::string(PortText("a match"))});
  } else if (st == "lobby") {
    *pLine = m != NULL ? PortTextF("In the lobby, {0}", {(*m)["name"].Str()}) : PortText("In the lobby");
    *pDetail = (m != NULL && !(*m)["mapName"].Str().empty() ? (*m)["mapName"].Str() + kDot : std::string()) +
               (d["admin"].b ? PortText("the host") : d["ready"].b ? PortText("ready") : PortText("not ready"));
  } else if (st == "installing") {
    *pLine = PortText("Getting the game from the host");
  } else if (st == "refused") {
    *pLine = PortText("Couldn\xe2\x80\x99t join");
    *pDetail = d["why"].Str();
  } else if (d["route"].Str() == "stream" || d["route"].Str() == "tab") {
    *pLine = PortText("On the host\xe2\x80\x99s page");
    // {0} the page the browser shows
    *pDetail = PortTextF("Looking at {0}", {d["page"].Str(PortText("the room"))}) + kDot + PortText("not in a match");
  } else {
    *pLine = PortText("In the room");
    *pDetail = PortText("Not in a match");
  }
}

void Glyph(const PortJson &d, float fSide)
{
  Decl g;
  g.d.layout.sizing.width = Fixed(Sc(fSide));
  g.d.layout.sizing.height = Fixed(Sc(fSide));
  ScLook look = kScButton;
  look.top = kLbInkInfo;
  g.d.custom.customData = Custom(DrawGlyph, look, 0.0f, GlyphOf(d));
  Open(g);
  Close();
}

void RouteChip(const PortJson &d, bool bSmall)
{
  const bool bStream = d["route"].Str() == "stream" || d["route"].Str() == "tab";
  const UiColor c = bStream ? kStream : kInstall;
  Decl p;
  p.d.layout.sizing.height = Fixed(Sc(bSmall ? 20 : 24));
  p.d.layout.padding = Pad(Fs(bSmall ? 7 : 9), Fs(bSmall ? 7 : 9), 0, 0);
  p.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  p.d.backgroundColor = Col(UiColor{c.r, c.g, c.b, 0.12f});
  p.d.border = Border(UiColor{c.r, c.g, c.b, 0.45f}, 1, 1, 1, 1);
  Radius(p, bSmall ? 10 : 12);
  Open(p);
  Text(RouteName(d), UI_FACE_BOLD, Fs(bSmall ? 12 : 13), 0.02f, c);
  Close();
}

void Rtt(const PortJson &d, float fSize)
{
  if (d["state"].Str() == "refused" && d["rtt"].Num() == 0) {
    Text("\xe2\x80\x94", UI_FACE_TEXT, Fs(fSize), 0.0f, kLbInkDesc);
    return;
  }
  const double f = d["rtt"].Num();
  char sz[32];
  if (f < 1)
    SDL_snprintf(sz, sizeof sz, "<1 ms");
  else
    SDL_snprintf(sz, sizeof sz, "%.0f ms", f);
  Text(Keep(sz), UI_FACE_TEXT, Fs(fSize), 0.0f, f < 25 ? kGood : f < 60 ? kFair : kPoor);
}

void Bar(float fWidth, float f, bool bHot)
{
  Decl b;
  b.d.layout.sizing.width = fWidth > 0 ? Fixed(Sc(fWidth)) : Grow();
  b.d.layout.sizing.height = Fixed(Sc(8));
  b.d.backgroundColor = Col(KitRgb(0x3980bc, 0.2f));
  Radius(b, 4);
  Open(b);
  Decl in;
  in.d.layout.sizing.width = Clay_SizingAxis{};
  in.d.layout.sizing.width.type = CLAY__SIZING_TYPE_PERCENT;
  in.d.layout.sizing.width.size.percent = fminf(1.0f, fmaxf(0.0f, f));
  in.d.layout.sizing.height = Grow();
  in.d.backgroundColor = Col(bHot ? kPoor : kStream);
  Radius(in, 4);
  Open(in);
  Close();
  Close();
}

void Spark(const std::vector<float> &values, float fMax, float w, float h, UiColor c)
{
  g_sparks.push_back(values);
  Decl s;
  s.d.layout.sizing.width = w > 0 ? Fixed(Sc(w)) : Grow();
  s.d.layout.sizing.height = Fixed(Sc(h));
  ScLook look = kScButton;
  look.top = c;
  s.d.custom.customData = Custom(DrawSpark, look, fMax, static_cast<int>(g_sparks.size()) - 1);
  Open(s);
  Close();
}

const std::vector<float> &RttHistory(int nId)
{
  return g_rtts[nId];
}

void RttSample(void)
{
  const Uint64 qwNow = SDL_GetTicks();
  if (qwNow - g_qwRttSampled < 1000)
    return;
  g_qwRttSampled = qwNow;
  const PortJson &ds = Room()["devices"];
  for (size_t i = 0; i < ds.size(); ++i) {
    std::vector<float> &v = g_rtts[static_cast<int>(ds.At(i)["id"].Num())];
    v.push_back(static_cast<float>(ds.At(i)["rtt"].Num()));
    if (v.size() > 30)
      v.erase(v.begin());
  }
}

void BoxAt(const char *pszId, float x, float y, float w, float h, const char *pszTitle)
{
  Decl b = At(pszId, x, y, w, h);
  b.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  b.d.layout.padding = Pad(Fs(18), Fs(18), Fs(15), Fs(15));
  b.d.layout.childGap = Gap(Fs(10));
  b.d.clip.vertical = true;
  InnerBox(b);
  Open(b);
  if (pszTitle != NULL)
    Title(pszTitle);
}

void Title(const char *psz)
{
  Text(Keep(Upper(psz)), UI_FACE_WIDE, Fs(16), 0.04f, kScInkHead);
}

void Fine(const char *psz, UiColor c, float fSize)
{
  Text(psz, UI_FACE_TEXT, Fs(fSize), 0.0f, c, -1, 1.35f);
}

void Ask(const std::string &id, const std::string &title, const std::string &body, const std::vector<AskChoice> &choices)
{
  g_ask.id = id;
  g_ask.title = title;
  g_ask.body = body;
  g_ask.choices = choices;
  g_ask.bUp = true;
  g_ask.nChosen = -1;
  g_lbOpen.clear();
}

bool AskUp(void)
{
  return g_ask.bUp;
}

int AskShown(std::string *pId)
{
  *pId = g_ask.id;
  if (!g_ask.bUp) {
    const int n = g_ask.nChosen;
    g_ask.nChosen = -1;
    if (n != -1)
      g_ask.id.clear();
    return n;
  }
  float fH = 136;
  for (const AskChoice &c : g_ask.choices)
    fH += c.line.empty() ? 62 : 78;
  const bool bOut = DialogOpen("lnask", 880, fH, 230, 80);
  Text(Keep(Upper(g_ask.title.c_str())), UI_FACE_WIDE, Fs(18), 0.03f, kScInkTipHead);
  Fine(Keep(g_ask.body), kLbInkInfo, 16);
  for (size_t i = 0; i < g_ask.choices.size(); ++i) {
    const AskChoice &c = g_ask.choices[i];
    Decl row;
    row.d.layout.sizing.width = Grow();
    row.d.layout.childGap = Gap(Fs(16));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(row);
    Decl b = Box("lnaskbtn", static_cast<int>(i));
    b.d.layout.sizing.width = Fixed(Sc(380));
    b.d.layout.sizing.height = Fixed(Sc(48));
    if (ScButton(b, Keep(c.label), c.cKey, i == 0))
      g_ask.nChosen = static_cast<int>(i);
    Decl l;
    l.d.layout.sizing.width = Grow();
    Open(l);
    if (!c.line.empty())
      Fine(Keep(c.line));
    Close();
    Close();
  }
  Close();
  if (bOut && g_ask.nChosen < 0)
    g_ask.nChosen = -2;
  if (g_ask.nChosen != -1)
    g_ask.bUp = false;
  return -1;
}

bool AskKeys(void)
{
  if (!g_ask.bUp)
    return false;
  for (size_t i = 0; i < g_ask.choices.size(); ++i)
    if (Key(static_cast<unsigned char>(g_ask.choices[i].cKey))) {
      g_ask.nChosen = static_cast<int>(i);
      g_ask.bUp = false;
      return true;
    }
  if (Key(VK_ESCAPE)) {
    g_ask.nChosen = -2;
    g_ask.bUp = false;
  } else if (Key(VK_RETURN)) {
    g_ask.nChosen = 0;
    g_ask.bUp = false;
  }
  return true;
}

void Toast(const std::string &title, const std::string &line)
{
  g_toasts.push_back(ToastLine{title, line, SDL_GetTicks()});
  if (g_toasts.size() > 3)
    g_toasts.pop_front();
  FrontLog("lan: %s: %s\n", title.c_str(), line.c_str());
}

void ToastsDraw(void)
{
  const Uint64 qwNow = SDL_GetTicks();
  while (!g_toasts.empty() && qwNow - g_toasts.front().qwAt > 6000)
    g_toasts.pop_front();
  if (g_toasts.empty())
    return;
  // at the bottom right, above the task bar, clear of the panel's header buttons
  Decl col = Floating("lntoasts", 0, -Sc(30), -Sc(96), 96, CLAY_ATTACH_POINT_RIGHT_BOTTOM, CLAY_ATTACH_POINT_RIGHT_BOTTOM);
  col.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  col.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  col.d.layout.childGap = Gap(Fs(8));
  Open(col);
  int i = 0;
  for (const ToastLine &t : g_toasts) {
    Decl b = Box("lntoast", i++);
    b.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    b.d.layout.sizing.width = Fixed(Sc(460));
    b.d.layout.padding = Pad(Fs(16), Fs(16), Fs(12), Fs(12));
    b.d.layout.childGap = Gap(Fs(4));
    const ScLook tip = {kScTipFill, kScTipFill, kScTipRim, kScTipFill, kScBlack, 4};
    b.d.custom.customData = Custom(DrawScBox, tip);
    Open(b);
    Text(Keep(t.title), UI_FACE_BOLD, Fs(17), 0.0f, kScInkTipHead);
    if (!t.line.empty())
      Fine(Keep(t.line), kScInkTip, 14);
    Close();
  }
  Close();
}

bool ServerRunning(void)
{
  return ProcessAlive();
}

void ServerStart(void)
{
  if (ProcessAlive())
    return;
  // a guest's room is left first: this copy is in one room at a time
  if (LanClientIn() && !LanClientAdmin())
    LanClientLeave();
  if (TakeBackRoom()) {
    Toast(PortText("Your room is still open"), PortText("This console is back in it."));
    return;
  }
  char szExe[MAX_PATH] = "";
  GetModuleFileNameA(NULL, szExe, MAX_PATH);
  // HYPERION_LAN_KEY=<key>: the room's key known to a check without a person, which reads the
  // room as a second console
  const char *pszKey = SDL_getenv("HYPERION_LAN_KEY");
  g_srv.key = pszKey != NULL && pszKey[0] ? pszKey : RandomHex(16);
  std::string cmd = "\"" + std::string(szExe) + "\" --room";
  const PortOptions *o = PortCommandOptions();
  if (o->szData[0])
    cmd += " --data \"" + std::string(o->szData) + "\"";
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  SDL_zero(si);
  si.cb = sizeof si;
  SDL_zero(pi);
  // the key and the password in the room's environment, for its start alone: a command line is
  // any program's to read (port_paths.cpp, PathsSecrets)
  SetEnvironmentVariableA("HYPERION_ROOM_KEY", g_srv.key.c_str());
  SetEnvironmentVariableA("HYPERION_ROOM_PASSWORD", g_srv.password.empty() ? NULL : g_srv.password.c_str());
  const BOOL bStarted = CreateProcessA(NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  SetEnvironmentVariableA("HYPERION_ROOM_KEY", NULL);
  SetEnvironmentVariableA("HYPERION_ROOM_PASSWORD", NULL);
  if (!bStarted) {
    Toast(PortText("The server could not start"), PortText("Hyperion could not start its room here."));
    FrontLog("lan: the room's server did not start (%lu)\n", static_cast<unsigned long>(GetLastError()));
    return;
  }
  if (pi.hThread != NULL)
    CloseHandle(pi.hThread);
  g_srv.hProcess = pi.hProcess;
  g_srv.qwStarted = SDL_GetTicks();
  g_srv.qwTried = 0;
  g_srv.bReported = false;
  FrontLog("lan: started the room's server, process %lu\n", static_cast<unsigned long>(pi.dwProcessId));
  Toast(PortText("The server is running"), PortText("Listening on this network only."));
}

void ServerStop(bool bAfterMatches)
{
  if (!LanClientAdmin())
    return;
  LanClientSay(bAfterMatches ? "{\"t\":\"stop\",\"when\":\"after\"}" : "{\"t\":\"stop\"}");
  g_bStopAsked = true;
  if (bAfterMatches)
    Toast(PortText("The server stops after the last match"), PortText("Nobody new can join meanwhile."));
}

void NewMatch(void)
{
  if (!LanClientAdmin())
    return;
  LanClientSay("{\"t\":\"newmatch\",\"play\":true}");
}

const char *WatchOff(const PortJson &m)
{
  if (m["state"].Str() != "live")
    return PortText("Only a match being played can be watched.");
  if (!LanClientAdmin() && Room()["watching"].kind == PortJson::BOOL && !Room()["watching"].b)
    return PortText("The host has turned watching off.");
  const PortJson *me = DeviceOf(You());
  if (me != NULL && (*me)["state"].Str() == "playing")
    return PortText("You are playing in a match.");
  if (LanWatchWaiting())
    return PortText("A match is on its way.");
  return NULL;
}

void WatchMatch(int nMatch)
{
  const PortJson *m = MatchOf(nMatch);
  if (m == NULL || WatchOff(*m) != NULL)
    return;
  LanWatchAsk(nMatch, (*m)["name"].Str());
  g_qwWatchAsked = SDL_GetTicks();
  const int nDelay = static_cast<int>(Room()["delay"].Num(10));
  Toast(PortTextF("Watching {0}", {(*m)["name"].Str()}),
        PortTextF("{0} s behind the players. L goes back there if you fall behind.", {std::to_string(nDelay)}));
}

void ToMultiplayer(void)
{
  g_nView = VIEW_MULTIPLAYER;
  g_lbOpen.clear();
}

void ToConsole(void)
{
  g_nView = VIEW_CONSOLE;
  g_lbOpen.clear();
  ConsoleOpen();
}

}  // namespace Lan
}  // namespace FrontKit

namespace {

// ---------------------------------------------------------------------------
// A match's lobby, the skirmish set-up hosted.

std::string RaceId(int nRace)
{
  return nRace >= 0 && nRace <= 2 ? kRaceIds[nRace] : "random";
}

int RaceOf(const std::string &s)
{
  return s == "zerg" ? 0 : s == "terran" ? 1 : s == "protoss" ? 2 : 6;
}

// The seat chip's words for a person: how they play, on what, how far from the host.
std::string ChipText(const PortJson &d)
{
  const double f = d["rtt"].Num();
  char sz[32];
  if (f < 1)
    SDL_snprintf(sz, sizeof sz, "<1 ms");
  else
    SDL_snprintf(sz, sizeof sz, "%.0f ms", f);
  return std::string(RouteName(d)) + kDot + KindName(d) + kDot + sz;
}

// The room's seats of the lobby's match, as the set-up takes them.
std::vector<SkirmishRoomSeat> RoomSeats(const PortJson &m)
{
  std::vector<SkirmishRoomSeat> out;
  const int nPlayers = static_cast<int>(m["mapPlayers"].Num(8));
  const PortJson &seats = m["seats"];
  for (int i = 0; i < nPlayers && i < static_cast<int>(seats.size()); ++i) {
    const PortJson &s = seats.At(i);
    SkirmishRoomSeat r;
    r.owner = s["owner"].Str("open");
    r.nRace = RaceOf(s["race"].Str());
    r.nHandicap = HandicapClamp(static_cast<int>(s["handicap"].Num(100)));
    r.name = s["name"].Str();
    r.bReady = s["ready"].b;
    const int nDev = static_cast<int>(s["dev"].Num());
    r.bYou = r.owner == "player" && nDev == You();
    if (const PortJson *d = r.owner == "player" ? DeviceOf(nDev) : NULL)
      r.chip = ChipText(*d);
    out.push_back(r);
  }
  return out;
}

// What the host last told the room, so only a change is sent again.
struct Sent {
  std::string match;
  std::vector<std::string> seats;
  std::string race;
  int nHandicap = 100;
  bool bSat = true;  // the room seats the host when it makes the match
  std::string pic;   // the match and map whose picture was sent
} g_sent;

// A map's picture for the room's page (join.js): its minimap, at most 256 pixels a side, as a
// PNG, and its starts, each the map's player and its place as a fraction of the picture across
// and down. The host draws it, since the room reads no game data. As the live channel's map_info
// reads a map, the game's own archive put by and back.
bool MapPicturePng(const std::string &path, std::vector<unsigned char> *pPng, std::string *pStarts)
{
  std::string file = path;
  char szCompiled[MAX_PATH] = "";
  if (PortHxmIsMap(path.c_str()) && PortHxmCompiled(path.c_str(), szCompiled, sizeof szCompiled))
    file = szCompiled;
  const HANDLE hGameArchive = ghScenArchive;
  ghScenArchive = NULL;
  DWORD dwSize = 0;
  BYTE *pChk = static_cast<BYTE *>(get_map_chk(file.c_str(), &dwSize));
  ghScenArchive = hGameArchive;
  if (pChk == NULL)
    return false;
  MapPicture pic;
  pic.nSlot = -1;
  MapPictureRead(&pic, path, pChk, dwSize);
  std::vector<unsigned char> rgba;
  int nW = 0, nH = 0;
  const bool bRgba = MapMinimapRgba(pChk, dwSize, 256, &rgba, &nW, &nH);
  mem_free(pChk, __FILE__, __LINE__);
  if (!bRgba || pic.w <= 0 || pic.h <= 0)
    return false;
  pStarts->clear();
  for (const MapStart &st : pic.starts) {
    char sz[64];
    snprintf(sz, sizeof sz, "%s[%d,%.4f,%.4f]", pStarts->empty() ? "" : ",", st.nSlot, st.x / (pic.w * 32.0),
             st.y / (pic.h * 32.0));
    *pStarts += sz;
  }
  pPng->clear();
  SDL_Surface *s = SDL_CreateSurfaceFrom(nW, nH, SDL_PIXELFORMAT_RGBA32, rgba.data(), nW * 4);
  SDL_IOStream *io = s != NULL ? SDL_IOFromDynamicMem() : NULL;
  if (io != NULL && SDL_SavePNG_IO(s, io, false)) {
    const Sint64 n = SDL_TellIO(io);
    const unsigned char *p = static_cast<const unsigned char *>(
        SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL));
    if (p != NULL && n > 0)
      pPng->assign(p, p + n);
  }
  if (io != NULL)
    SDL_CloseIO(io);
  if (s != NULL)
    SDL_DestroySurface(s);
  return !pPng->empty();
}

// The host's set-up into the room: the map and options, each seat that is not a person, and
// the host's own race; the room's people into the set-up's open seats.
void HostSync(void)
{
  const PortJson *m = MatchOf(g_nLobbyMatch);
  if (m == NULL)
    return;
  const std::string match = std::to_string(g_nLobbyMatch);
  // the room's people first, so a seat one of them holds is theirs in what is sent
  SkirmishRoomSeats(RoomSeats(*m));
  SkirmishRoomSetUp r;
  if (SkirmishRoomNow(&r) && (*m)["state"].Str() == "lobby") {
    const int nCategory = r.nKey & 0xFF;
    const std::string msg = "{\"t\":\"match\",\"match\":" + match + ",\"map\":" + PortJsonQuote(r.map) +
                            ",\"mapName\":" + PortJsonQuote(r.title) + ",\"players\":" + std::to_string(r.nPlayers) +
                            ",\"type\":" + PortJsonQuote(nCategory == 3 ? "ffa" : "melee") +
                            ",\"key\":" + std::to_string(r.nKey) + ",\"speed\":" +
                            PortJsonQuote(kSpeedIds[r.nSpeed >= 0 && r.nSpeed <= 6 ? r.nSpeed : 6]) +
                            ",\"speedN\":" + std::to_string(r.nSpeed) + ",\"name\":" + PortJsonQuote(r.title) +
                            ",\"rules\":" + PortJsonQuote(r.bHyperion ? "hyperion" : "classic") +
                            ",\"mutators\":" + MutatorsJson(r.mut) + "}";
    if (msg != g_sent.match) {
      LanClientSay(msg);
      g_sent.match = msg;
      g_sent.seats.clear();  // a new map opens the room's seats again: each is said again
    }
    // the map's picture, once a map
    if (g_sent.pic != match + "|" + r.map) {
      g_sent.pic = match + "|" + r.map;
      std::vector<unsigned char> png;
      std::string starts;
      if (MapPicturePng(r.map, &png, &starts))
        LanClientSay("{\"t\":\"mappic\",\"match\":" + match + ",\"map\":" + PortJsonQuote(r.map) + ",\"png\":\"" +
                     PortBase64(png.data(), png.size()) + "\",\"starts\":[" + starts + "]}");
    }
    g_sent.seats.resize(r.seats.size());
    bool bSeated = false;
    for (const SkirmishRoomSeat &s : r.seats)
      bSeated = bSeated || s.bYou;
    if (!bSeated && g_sent.bSat) {
      // the host has given up its seat: it runs the room and watches
      LanClientSay("{\"t\":\"stand\"}");
      g_sent.bSat = false;
      for (std::string &seat : g_sent.seats)
        if (seat == "you")
          seat.clear();
    }
    for (size_t i = 0; i < r.seats.size(); ++i) {
      const SkirmishRoomSeat &s = r.seats[i];
      if (s.bYou) {
        if (RaceId(s.nRace) != g_sent.race) {
          g_sent.race = RaceId(s.nRace);
          LanClientSay("{\"t\":\"race\",\"race\":" + PortJsonQuote(g_sent.race) + "}");
        }
        if (s.nHandicap != g_sent.nHandicap) {
          g_sent.nHandicap = s.nHandicap;
          LanClientSay("{\"t\":\"handicap\",\"handicap\":" + std::to_string(s.nHandicap) + "}");
        }
        if (g_sent.seats[i] != "you") {
          // the seat opened first, as the host may sit where a computer was
          if (!g_sent.seats[i].empty() || !g_sent.bSat)
            LanClientSay("{\"t\":\"slot\",\"match\":" + match + ",\"slot\":" + std::to_string(i) + ",\"owner\":\"open\"}");
          g_sent.seats[i] = "you";
          g_sent.bSat = true;
          LanClientSay("{\"t\":\"sit\",\"match\":" + match + ",\"slot\":" + std::to_string(i) + "}");
        }
        continue;
      }
      if (s.owner == "player")
        continue;  // a person's seat is theirs
      const std::string seat = "{\"t\":\"slot\",\"match\":" + match + ",\"slot\":" + std::to_string(i) +
                               ",\"owner\":" + PortJsonQuote(s.owner) + ",\"race\":" + PortJsonQuote(RaceId(s.nRace)) +
                               ",\"handicap\":" + std::to_string(s.nHandicap) + "}";
      if (seat != g_sent.seats[i]) {
        LanClientSay(seat);
        g_sent.seats[i] = seat;
      }
    }
  }
}

// A guest's set-up from the room's, and their own race back to it.
void GuestSync(void)
{
  const PortJson *me = DeviceOf(You());
  const PortJson *m = me != NULL ? MatchOf(static_cast<int>((*me)["match"].Num())) : NULL;
  if (m == NULL || (*m)["map"].Str().empty())
    return;
  SkirmishRoomSetUp r;
  r.map = std::string(PortDataFolder()) + "lan\\maps\\" + (*m)["mapFile"].Str();
  r.title = (*m)["mapName"].Str();
  r.nPlayers = static_cast<int>((*m)["mapPlayers"].Num(8));
  r.nKey = static_cast<int>((*m)["key"].Num());
  r.nSpeed = static_cast<int>((*m)["speedN"].Num(6));
  r.type = (r.nKey & 0xFF) == 3 ? "Free For All" : "Melee";
  r.bHyperion = (*m)["rules"].Str() == "hyperion";
  r.mut = MutatorsFromJson((*m)["mutators"]);
  r.seats = RoomSeats(*m);
  SkirmishGuestSetUp(r);
  // the guest's race and handicap, when they change them
  SkirmishRoomSetUp now;
  if (SkirmishRoomNow(&now))
    for (const SkirmishRoomSeat &s : now.seats) {
      if (s.bYou && RaceId(s.nRace) != (*me)["race"].Str() && RaceId(s.nRace) != g_sent.race) {
        g_sent.race = RaceId(s.nRace);
        LanClientSay("{\"t\":\"race\",\"race\":" + PortJsonQuote(g_sent.race) + "}");
      }
      if (s.bYou && s.nHandicap != static_cast<int>((*me)["handicap"].Num(100)) && s.nHandicap != g_sent.nHandicap) {
        g_sent.nHandicap = s.nHandicap;
        LanClientSay("{\"t\":\"handicap\",\"handicap\":" + std::to_string(s.nHandicap) + "}");
      }
    }
}

bool MeReady(void)
{
  const PortJson *me = DeviceOf(You());
  return me != NULL && (*me)["ready"].b;
}

void LobbyHead(void)
{
  Decl b = Box("lnlobbyback");
  b.d.layout.sizing.width = Fit();
  b.d.layout.sizing.width.size.minMax.min = Sc(150);
  b.d.layout.sizing.height = Fixed(Sc(40));
  const bool bHost = LanClientAdmin();
  if (ScMini(b, bHost ? PortText("Console") : PortText("The Room"), 'R'))
    g_nView = bHost ? VIEW_CONSOLE : VIEW_ROOM;
  TipOn(Id("lnlobbyback"), bHost ? PortText("The host\xe2\x80\x99s console") : PortText("The room"),
        bHost ? PortText("Everyone in the room, the matches and this machine\xe2\x80\x99s load. Hotkey: R")
              : PortText("Who is in the room and what they play. Leave goes back to Multiplayer. Hotkey: R"));
}

void HostStart(void)
{
  SkirmishRoomSetUp r;
  if (!SkirmishRoomNow(&r)) {
    Toast(PortText("Choose a map first"), "");
    return;
  }
  int nPlayers = 0;
  std::string waiting;
  for (const SkirmishRoomSeat &s : r.seats) {
    nPlayers += s.owner == "player" || s.owner == "computer";
    if (s.owner == "player" && !s.bYou && !s.bReady && waiting.empty())
      waiting = s.name;
  }
  if (nPlayers < 2) {
    Toast(PortText("A match needs two players or more"),
          PortText("Open a seat for the room, or make one a computer."));
    return;
  }
  if (!waiting.empty()) {
    // {0} the player whose seat is not ready
    Ask("notready", PortTextF("{0} Is Not Ready", {waiting}),
        PortText("Every seat says Ready before the match starts."),
        {{'W', PortTextF("Wait for {0}", {waiting}), PortText("The match starts when every seat is ready.")},
         {'S', PortText("Start Anyway"), PortText("The match starts now, with everyone seated.")},
         {'C', PortText("Cancel"), ""}});
    return;
  }
  LanClientSay("{\"t\":\"start\",\"match\":" + std::to_string(g_nLobbyMatch) + "}");
  FrontLog("lan: Start Match, match %d\n", g_nLobbyMatch);
}

void GuestStart(void)
{
  LanClientSay(MeReady() ? "{\"t\":\"ready\",\"on\":false}" : "{\"t\":\"ready\",\"on\":true}");
}

const char *GuestStartLabel(void)
{
  return MeReady() ? PortText("Not Ready") : PortText("Ready");
}

void HostBack(void)
{
  g_nView = VIEW_CONSOLE;
}

void GuestBack(void)
{
  LanClientLeave();
  g_nView = VIEW_MULTIPLAYER;
  Toast(PortText("You left the room"), "");
}

bool LobbyKeys(void)
{
  if (g_bChatFocus) {
    if (Key(VK_RETURN)) {
      if (!g_chat.empty()) {
        const PortJson *me = DeviceOf(You());
        const int nMatch = LanClientAdmin() ? g_nLobbyMatch : me != NULL ? static_cast<int>((*me)["match"].Num()) : 0;
        LanClientSay("{\"t\":\"chat\",\"match\":" + std::to_string(nMatch) + ",\"text\":" + PortJsonQuote(g_chat) + "}");
      }
      g_chat.clear();
      g_bChatFocus = false;
    } else if (Key(VK_ESCAPE)) {
      g_bChatFocus = false;
    }
    return true;  // the line has the keyboard
  }
  if (Key(VK_RETURN) && !g_bChatFocus) {
    g_bChatFocus = true;
    return true;
  }
  if (Key('R')) {
    g_nView = LanClientAdmin() ? VIEW_CONSOLE : VIEW_ROOM;
    return true;
  }
  return false;
}

// The Room box, in the mutators' place (lan.js roomBox): who is seated and how each plays, the
// refused, and the lobby's chat.
void RoomBox(float x, float y, float w, float h)
{
  const PortJson *me = DeviceOf(You());
  const int nMatch = LanClientAdmin() ? g_nLobbyMatch : me != NULL ? static_cast<int>((*me)["match"].Num()) : 0;
  const PortJson *m = MatchOf(nMatch);
  Decl box = At("lnroombox", x, y, w, h);
  box.d.layout.padding = Pad(Fs(20), Fs(20), Fs(14), Fs(14));
  box.d.layout.childGap = Gap(Fs(18));
  box.d.clip.vertical = true;
  InnerBox(box);
  Open(box);
  // who
  Decl left;
  left.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  left.d.layout.sizing.width = Fixed(Sc((w - 40 - 18) * 0.55f));
  left.d.layout.childGap = Gap(Fs(8));
  Open(left);
  Title(PortText("The Room"));
  int nPeople = 0, nComputers = 0, nStream = 0;
  std::vector<const PortJson *> seated;
  if (m != NULL) {
    const PortJson &seats = (*m)["seats"];
    for (size_t i = 0; i < seats.size(); ++i) {
      const std::string o = seats.At(i)["owner"].Str();
      nComputers += o == "computer";
      if (o == "player")
        if (const PortJson *d = DeviceOf(static_cast<int>(seats.At(i)["dev"].Num()))) {
          seated.push_back(d);
          ++nPeople;
          nStream += (*d)["route"].Str() == "stream";
        }
    }
  }
  const std::string players = PortTextNF("{0} player", "{0} players", nPeople, {std::to_string(nPeople)});
  const std::string computers =
      nComputers ? PortTextNF("{0} computer", "{0} computers", nComputers, {std::to_string(nComputers)}) : std::string();
  Text(Keep((computers.empty() ? players : PortTextF("{0} and {1}", {players, computers})) + kDot +
            PortTextF("{0} in a browser, {1} installed",
                      {std::to_string(nStream), std::to_string(nPeople - nStream)})),
       UI_FACE_BOLD, Fs(16), 0.0f, kScInkLabel);
  Fine(Keep(PortTextF("When it starts: {0} from the host",
                      {PortTextNF("{0} new stream", "{0} new streams", nStream, {std::to_string(nStream)})})));
  for (const PortJson *d : seated) {
    Decl r;
    r.d.layout.sizing.width = Grow();
    r.d.layout.childGap = Gap(Fs(8));
    r.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(r);
    Glyph(*d, 20);
    Text(Keep((*d)["name"].Str()), UI_FACE_BOLD, Fs(16), 0.0f, kLbInkInfo);
    RouteChip(*d, true);
    Fine(Keep((*d)["admin"].b
                  ? PortText("This machine") + std::string(kDot) + VersionName(HYPERION_VERSION_TEXT)
                  : (*d)["machine"].Str() + kDot +
                        PortTextF("{0}, the host\xe2\x80\x99s own", {VersionName((*d)["version"].Str())})));
    Close();
  }
  const PortJson &ds = Room()["devices"];
  for (size_t i = 0; i < ds.size(); ++i) {
    const PortJson &d = ds.At(i);
    if (d["state"].Str() != "refused")
      continue;
    Decl r;
    r.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    r.d.layout.sizing.width = Grow();
    Open(r);
    Decl a;
    a.d.layout.childGap = Gap(Fs(8));
    a.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(a);
    Glyph(d, 20);
    Text(Keep(d["name"].Str()), UI_FACE_BOLD, Fs(16), 0.0f, kLbInkInfo);
    Text(PortText("Couldn\xe2\x80\x99t join"), UI_FACE_BOLD, Fs(13), 0.0f, kBad);
    Close();
    Fine(Keep(d["why"].Str() + " " + d["fix"].Str()), kLbInkDesc, 13);
    Close();
  }
  Close();
  // the chat
  Decl right;
  right.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  right.d.layout.sizing.width = Grow();
  right.d.layout.sizing.height = Grow();
  right.d.layout.childGap = Gap(Fs(6));
  Open(right);
  Title(PortText("Chat"));
  Decl lines;
  lines.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  lines.d.layout.sizing.width = Grow();
  lines.d.layout.sizing.height = Grow();
  lines.d.layout.childGap = Gap(Fs(3));
  lines.d.layout.childAlignment.y = CLAY_ALIGN_Y_BOTTOM;
  lines.d.clip.vertical = true;
  Open(lines);
  if (m != NULL) {
    const PortJson &chat = (*m)["chat"];
    const size_t nFrom = chat.size() > 8 ? chat.size() - 8 : 0;
    for (size_t i = nFrom; i < chat.size(); ++i) {
      char szClean[512];
      const std::string who = chat.At(i).At(0).Str(),
                        what = PortChatClean(chat.At(i).At(1).Str().c_str(), szClean, sizeof szClean);
      Decl l;
      l.d.layout.childGap = Gap(Fs(6));
      Open(l);
      if (!who.empty())
        Text(Keep(who + ":"), UI_FACE_BOLD, Fs(14), 0.0f, kStream);
      Text(Keep(what), UI_FACE_TEXT, Fs(14), 0.0f, who.empty() ? kLbInkDesc : kLbInkInfo);
      Close();
    }
    if (chat.size() == 0)
      Fine(PortText("Nothing said yet. Enter types a line."), kScInkOff);
  }
  Close();
  Decl edit = Box("lnchat");
  edit.d.layout.sizing.width = Grow();
  edit.d.layout.sizing.height = Fixed(Sc(36));
  if (Clicked(edit.d.id))
    g_bChatFocus = true;
  ScEdit(edit, &g_chat, 120, g_bChatFocus ? "" : PortText("Enter to chat"), g_bChatFocus);
  Close();
  Close();
}

const SkirmishHost g_hostLobby = {
    "lan", PT_("A match in the room"), PT_("Start Match"),
    PT_("The seats, the game options and who is in the room come next, then Start Match."),
    LobbyHead, RoomBox, HostStart, LobbyKeys, HostBack, 1, NULL};
const SkirmishHost g_guestLobby = {"languest", PT_("A match in the room"), PT_("Ready"), "", LobbyHead, RoomBox,
                                   GuestStart, LobbyKeys, GuestBack, 2, GuestStartLabel};

// ---------------------------------------------------------------------------
// Multiplayer.

void PanelOpen(const char *pszId, float fW, float fH)
{
  Decl panel = Box(pszId);
  panel.d.layout.sizing.width = Fixed(Sc(fW));
  panel.d.layout.sizing.height = Fixed(Sc(fH));
  panel.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  panel.d.floating.offset.x = Sc(58);
  panel.d.floating.offset.y = Sc(132);
  panel.d.floating.zIndex = 20;
  static UiCustom frame = {0.0f, DrawScFrame, NULL};
  panel.d.custom.customData = &frame;
  Open(panel);
}

void Head(float fW, const std::string &title, const std::string &line, UiColor lineColour = kLbInkInfo)
{
  Decl head = At("lnhead", 40, 22, fW - 40 - 700, 0);
  head.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  head.d.layout.childGap = Gap(Fs(2));
  Open(head);
  Text(Keep(title), UI_FACE_BOLD, Fs(27), 0.0f, kScInkHi);
  Text(Keep(line), UI_FACE_TEXT, Fs(17), 0.0f, lineColour);
  Close();
}

void KeyChip(const char *psz)
{
  Decl k;
  k.d.layout.padding = Pad(Fs(6), Fs(6), Fs(1), Fs(1));
  k.d.layout.sizing.width = Fit();
  k.d.layout.sizing.width.size.minMax.min = Sc(18);
  k.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  k.d.backgroundColor = Col(KitRgb(0x3980bc, 0.18f));
  k.d.border = Border(kLbItemRim, 1, 1, 1, 1);
  Radius(k, 3);
  Open(k);
  Text(psz, UI_FACE_BOLD, Fs(12), 0.0f, kLbInkInfo);
  Close();
}

void Tick(UiColor c)
{
  Decl t;
  t.d.layout.sizing.width = Fixed(Sc(12));
  t.d.layout.sizing.height = Fixed(Sc(12));
  t.d.border = Border(c, Fs(2), 0, 0, Fs(2));
  Open(t);
  Close();
}

void JoinRoom(const LanRoomFound &r, const std::string &password)
{
  ServerStop(false);  // a room this copy hosts closes first
  LanClientLeave();
  g_refusal.clear();
  g_joining = r.info["name"].Str(r.addr.c_str());
  g_qwJoinAt = SDL_GetTicks();
  if (!LanClientJoin(r.addr.c_str(), r.nPort, password.c_str(), "")) {
    Toast(PortTextF("Nothing answers at {0}", {r.addr}),
          PortText("A room is joined on port 6112, and found on 6111."));
    g_joining.clear();
  }
  FrontLog("lan: joining %s at %s:%d\n", g_joining.c_str(), r.addr.c_str(), r.nPort);
}

void JoinAsked(const LanRoomFound &r)
{
  if (!r.bOk)
    return;
  if (r.info["password"].b) {
    g_pwFor = r.info["name"].Str(r.addr.c_str());
    g_pwAddr = r.addr;
    g_nPwPort = r.nPort;
    g_password.clear();
    return;
  }
  JoinRoom(r, "");
}

void JoinAddress(void)
{
  LanRoomFound r;
  std::string a = g_addr;
  const size_t n = a.find(':');
  r.nPort = n != std::string::npos ? atoi(a.c_str() + n + 1) : 0;
  r.addr = n != std::string::npos ? a.substr(0, n) : a;
  r.bOk = true;
  uint32_t dw;
  if (!LanNet::AddrParse(r.addr.c_str(), &dw)) {
    Toast(PortText("That is not an address"),
          PortText("An address on this network is four numbers, like 192.168.1.20."));
    return;
  }
  // the password is asked for when the room says it wants one
  g_pwFor = r.addr;
  g_pwAddr = r.addr;
  g_nPwPort = r.nPort;
  g_password.clear();
  for (const LanRoomFound &f : g_rooms)
    if (f.addr == r.addr) {
      JoinAsked(f);
      return;
    }
}

void MpCard(int i, const char *pszName, const char *pszLine, const char *pszSmall, const char *pszKey, bool bHost, float w)
{
  Decl c = Box("lnmpcard", i);
  c.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  c.d.layout.sizing.width = Fixed(Sc(w));
  c.d.layout.sizing.height = Grow();
  c.d.layout.padding = Pad(Fs(24), Fs(24), Fs(20), Fs(20));
  c.d.layout.childGap = Gap(Fs(10));
  const bool bHot = Over(c.d.id);
  c.d.backgroundColor = Col(bHot ? kLbItemHi : kLbItem);
  c.d.border = Border(kLbItemRim, Fs(2), Fs(2), Fs(2), Fs(2));
  Radius(c, 4);
  if (Clicked(c.d.id)) {
    if (bHost) {
      ServerStart();
      ToConsole();
    } else {
      for (const LanRoomFound &r : g_rooms)
        if (r.bOk) {
          JoinAsked(r);
          break;
        }
    }
  }
  Open(c);
  Decl top;
  top.d.layout.sizing.width = Grow();
  Open(top);
  Decl nm;
  nm.d.layout.sizing.width = Grow();
  Open(nm);
  Text(pszName, UI_FACE_BOLD, Fs(26), 0.0f, kLbInkInfo);
  Close();
  KeyChip(pszKey);
  Close();
  Fine(pszLine, kInkPlayer, 16);
  if (bHost) {
    struct Need {
      std::string a, b;
      bool bOk;
    };
    const std::vector<Need> needs = {
        // {0} the game this copy reads
        {PortText("The game, installed here"),
         PortTextF("{0}, from this PC\xe2\x80\x99s copy", {IsBroodwar() ? PortText("StarCraft and Brood War") : "StarCraft"}), true},
        {PortText("A wired network"), PortText("Wi-Fi works, with more delay"), true},
        {PortText("A graphics card that makes video"),
         PortText("For players in a browser, which is not available yet"), false}};
    for (const Need &n : needs) {
      Decl li;
      li.d.layout.childGap = Gap(Fs(10));
      li.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      Open(li);
      Tick(n.bOk ? kGood : kScInkOff);
      Text(Keep(n.a), UI_FACE_BOLD, Fs(16), 0.0f, n.bOk ? kLbInkInfo : kScInkOff);
      Text(Keep(n.b), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
      Close();
    }
  }
  Spacer();
  Fine(pszSmall, kLbInkDesc, 14);
  Close();
}

void RoomsBox(float x, float y, float w, float h)
{
  BoxAt("lnrooms", x, y, w, h, PortText("Rooms on This Network"));
  Decl list;
  list.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  list.d.layout.sizing.width = Grow();
  list.d.layout.childGap = Gap(Fs(8));
  Open(list);
  if (g_rooms.empty())
    Fine(PortText("No room answers on this network yet. A room appears here a moment after its host starts it."),
         kLbInkDesc, 16);
  for (size_t i = 0; i < g_rooms.size(); ++i) {
    const LanRoomFound &r = g_rooms[i];
    Decl row = Box("lnroom", static_cast<int>(i));
    row.d.layout.sizing.width = Grow();
    row.d.layout.sizing.height = Fit();
    row.d.layout.sizing.height.size.minMax.min = Sc(76);
    row.d.layout.padding = Pad(Fs(16), Fs(16), Fs(10), Fs(10));
    row.d.layout.childGap = Gap(Fs(18));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    row.d.backgroundColor = Col(kLbItem);
    row.d.border = Border(kLbItemRim, Fs(2), Fs(2), Fs(2), Fs(2));
    Radius(row, 4);
    Open(row);
    Decl a;
    a.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    a.d.layout.sizing.width = Grow();
    a.d.layout.childGap = Gap(Fs(3));
    Open(a);
    Text(Keep(r.info["name"].Str(PortText("A room"))), UI_FACE_BOLD, Fs(19), 0.0f, kLbInkInfo);
    Text(Keep(r.info["machine"].Str() + kDot + r.addr + kDot + VersionName(r.info["version"].Str()) + kDot +
              r.info["game"].Str("Brood War")),
         UI_FACE_TEXT, Fs(14), 0.0f, kInkPlayer);
    const int nPeople = static_cast<int>(r.info["people"].Num()), nLive = static_cast<int>(r.info["matches"].Num());
    Text(Keep(PortTextNF("{0} device in the room", "{0} devices in the room", nPeople, {std::to_string(nPeople)}) + kDot +
              PortTextNF("{0} match running", "{0} matches running", nLive, {std::to_string(nLive)}) +
              (r.info["password"].b ? std::string(kDot) + PortText("asks for a password") : std::string())),
         UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
    Close();
    Decl z;
    z.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    z.d.layout.sizing.width = Fit(Sc(520));
    z.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
    z.d.layout.childGap = Gap(Fs(3));
    Open(z);
    Text(Keep(r.mark), UI_FACE_BOLD, Fs(13), 0.0f, r.bOk ? kGood : kBad);
    if (!r.bOk)
      Text(Keep(r.why + " " + r.fix), UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc, -1, 1.3f);
    Close();
    Decl j = Box("lnroomjoin", static_cast<int>(i));
    j.d.layout.sizing.width = Fixed(Sc(120));
    j.d.layout.sizing.height = Fixed(Sc(36));
    if (ScMini(j, PortText("Join"), 0, r.bOk))
      JoinAsked(r);
    if (!r.bOk)
      TipOn(Id("lnroomjoin", static_cast<int>(i)), PortText("Can\xe2\x80\x99t join"), Keep(r.why));
    Close();
  }
  // or an address
  Decl adr;
  adr.d.layout.sizing.width = Grow();
  adr.d.layout.sizing.height = Fixed(Sc(60));
  adr.d.layout.padding = Pad(Fs(16), Fs(16), 0, 0);
  adr.d.layout.childGap = Gap(Fs(14));
  adr.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  adr.d.backgroundColor = Col(kLbItem);
  adr.d.border = Border(kLbItemRim, Fs(2), Fs(2), Fs(2), Fs(2));
  Radius(adr, 4);
  Open(adr);
  Text(PortText("Or an address"), UI_FACE_BOLD, Fs(17), 0.0f, kScInkLabel);
  Decl e = Box("lnaddr");
  e.d.layout.sizing.width = Grow();
  e.d.layout.sizing.height = Fixed(Sc(40));
  if (Clicked(e.d.id))
    g_bAddrFocus = true;
  else if (g_bPressed && !Over(e.d.id))
    g_bAddrFocus = false;
  ScEdit(e, &g_addr, 40, "192.168.1.20", g_bAddrFocus && g_pwFor.empty());  // the password's dialog has the keys
  Decl aj = Box("lnaddrjoin");
  aj.d.layout.sizing.width = Fixed(Sc(120));
  aj.d.layout.sizing.height = Fixed(Sc(36));
  if (ScMini(aj, PortText("Join"), 0))
    JoinAddress();
  Close();
  Close();
  Close();  // the box
}

void HowBox(float x, float y, float w, float h)
{
  BoxAt("lnhow", x, y, w, h, PortText("Two Ways to Play"));
  struct Way {
    const char *pszRoute, *pszTitle, *pszLine;
  };
  const Way ways[] = {
      {"stream", PortText("In a browser, streamed"),
       PortText("Phones, tablets, Macs, Chromebooks, a television: anything with a browser. The host runs that player\xe2\x80\x99s "
                "game and sends it as video; nothing is installed and nothing of the game leaves the host. Not available yet.")},
      {"install", PortText("Installed, from the host"),
       PortText("Windows and Linux PCs, x64 or ARM. Each plays the game itself at its own resolution, and sends its "
                "commands through the host.")}};
  for (const Way &wy : ways) {
    Decl s;
    s.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    s.d.layout.sizing.width = Grow();
    s.d.layout.childGap = Gap(Fs(6));
    s.d.layout.padding = Pad(0, 0, 0, Fs(12));
    s.d.border = Border(kLbDivider, 0, 0, 0, Fs(2));
    Open(s);
    PortJson d;
    PortJsonParse(strcmp(wy.pszRoute, "stream") == 0 ? "{\"route\":\"stream\"}" : "{\"route\":\"install\"}", &d);
    RouteChip(d, false);
    Text(wy.pszTitle, UI_FACE_BOLD, Fs(17), 0.0f, kLbInkInfo);
    Fine(wy.pszLine);
    Close();
  }
  Fine(PortText("Both kinds play in one match: every seat sends its commands through the host, and every seat computes the "
                "same game from them."));
  Text(PortText("THIS COPY"), UI_FACE_WIDE, Fs(14), 0.04f, kScInkLabel);
  const std::string mod = PortModActive() >= 0 && PortModActive() < static_cast<int>(PortMods().size()) &&
                                  !PortMods()[PortModActive()].bBuiltIn
                              ? PortTextF("the mod {0}", {PortMods()[PortModActive()].name})
                              : std::string(PortText("no mod"));
  // {0} the version, {1} the game, {2} the mod
  Text(Keep(PortTextF("{0}, {1}, {2}", {VersionName(HYPERION_VERSION_TEXT),
                                        IsBroodwar() ? PortText("StarCraft and Brood War") : "StarCraft", mod})),
       UI_FACE_TEXT, Fs(15), 0.0f, kLbInkInfo);
  // {0} the game data's fingerprint
  Fine(Keep(PortTextF("A room plays only with copies that match it: the same version, the same game data ({0}), "
                      "the same mod.",
                      {LanFingerprint()})));
  Spacer();
  Fine(PortText("Rooms are for this network only, and everyone playing needs their own copy of StarCraft."), kLbInkInfo);
  Close();
}

void PasswordDialog(void)
{
  if (g_pwFor.empty())
    return;
  const bool bOut = DialogOpen("lnpw", 640, 290, 260, 80);
  // {0} the room being joined
  Text(Keep(Upper(PortTextF("Join {0}", {g_pwFor}).c_str())), UI_FACE_WIDE, Fs(18), 0.03f, kScInkTipHead);
  Fine(PortText("The room asks for its password. The host\xe2\x80\x99s console shows it, under Join This Room."), kLbInkInfo,
       16);
  Decl e = Box("lnpwedit");
  e.d.layout.sizing.width = Grow();
  e.d.layout.sizing.height = Fixed(Sc(40));
  ScEdit(e, &g_password, 60, PortText("the password"), true);
  Decl row;
  row.d.layout.childGap = Gap(Fs(10));
  Open(row);
  Decl j = Box("lnpwjoin");
  j.d.layout.sizing.width = Fixed(Sc(220));
  j.d.layout.sizing.height = Fixed(Sc(48));
  const bool bJoin = ScButton(j, PortText("Join"), 0, true);
  Decl c = Box("lnpwcancel");
  c.d.layout.sizing.width = Fixed(Sc(220));
  c.d.layout.sizing.height = Fixed(Sc(48));
  const bool bCancel = ScButton(c, PortText("Cancel"), 0, false);
  Close();
  Close();
  if (bJoin) {
    LanRoomFound r;
    r.addr = g_pwAddr;
    r.nPort = g_nPwPort;
    r.info = PortJson();
    r.bOk = true;
    const std::string name = g_pwFor;
    g_pwFor.clear();
    JoinRoom(r, g_password);
    g_joining = name;
  } else if (bCancel || bOut) {
    g_pwFor.clear();
  }
}

void MultiplayerScreen(float fW, float fH)
{
  g_rooms = LanClientRooms();
  // joining: let in, refused, or nothing
  if (!g_joining.empty()) {
    std::string fix;
    const std::string &why = LanClientRefused(&fix);
    if (LanClientIn()) {
      // {0} the room this copy is in
      Toast(PortTextF("In {0}", {g_joining}), "");
      g_joining.clear();
      g_nView = VIEW_ROOM;
    } else if (!why.empty()) {
      g_refusal = why;
      g_refusalFix = fix;
      Toast(PortTextF("Couldn\xe2\x80\x99t join {0}", {g_joining}), why);
      g_joining.clear();
      LanClientLeave();
    } else if (!LanClientWaiting() || SDL_GetTicks() - g_qwJoinAt > 8000) {
      Toast(PortTextF("Nothing answers in {0}", {g_joining}), PortText("The room may have stopped."));
      g_joining.clear();
      LanClientLeave();
    }
  }
  const bool bHosting = ServerRunning();
  Head(fW, PortText("Multiplayer"),
       !g_joining.empty()  ? PortTextF("Joining {0}\xe2\x80\xa6", {g_joining})
       : !g_refusal.empty() ? PortTextF("Couldn\xe2\x80\x99t join: {0} {1}", {g_refusal, g_refusalFix})
                            : std::string(PortText("Play with others on this network: host a room here, or join one")),
       !g_refusal.empty() && g_joining.empty() ? kLbWarn : kLbInkInfo);
  Decl tabs = Floating("lntabs", 0, -Sc(34), Sc(32), 22, CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP);
  tabs.d.layout.childGap = Gap(Fs(8));
  Open(tabs);
  Decl a = Box("lnnet");
  a.d.layout.sizing.width = Fit();
  a.d.layout.sizing.width.size.minMax.min = Sc(150);
  a.d.layout.sizing.height = Fixed(Sc(40));
  a.d.border = Border(kLbHyperionRim, Fs(2), Fs(2), Fs(2), Fs(2));
  ScMini(a, PortText("This Network"), '1');
  Decl o = Box("lnonline");
  o.d.layout.sizing.width = Fit();
  o.d.layout.sizing.width.size.minMax.min = Sc(150);
  o.d.layout.sizing.height = Fixed(Sc(40));
  ScMini(o, PortText("Online"), 0, false);
  TipOn(Id("lnonline"), PortText("Online"),
        PortText("Not available yet. Online play comes later, on the same connection the network uses."));
  Decl og = Box("lnoriginal");
  og.d.layout.sizing.width = Fit();
  og.d.layout.sizing.width.size.minMax.min = Sc(150);
  og.d.layout.sizing.height = Fixed(Sc(40));
  if (ScMini(og, PortText("The Original\xe2\x80\x99s Screens"), 'O')) {
    g_nResult = FRONT_LAN_ORIGINAL;
    g_bCameBack = true;  // the original's screens left, Multiplayer again
  }
  TipOn(Id("lnoriginal"), PortText("The original\xe2\x80\x99s screens"),
        PortText("StarCraft\xe2\x80\x99s own multiplayer screens, as the original draws them: Local Area Network, its game list and "
                 "chat room. Hotkey: O"));
  Close();
  const float right = 34, info = 425, gap = 10, leftW = fW - right - info - gap - 13;
  Decl cards = At("lnmpcards", 13, 93, leftW, 320);
  cards.d.layout.childGap = Gap(Fs(10));
  Open(cards);
  const float cw = (leftW - 10) / 2;
  MpCard(0, PortText("Host a Room"),
         PortText("This machine hosts. Everyone else on the network joins with Hyperion installed, from Join a Room."),
         bHosting ? PortText("Your room is running: H opens its console.")
                  : PortText("Installed players cost the host next to nothing; as many as the network holds."),
         "H", true, cw);
  int nOk = 0;
  for (const LanRoomFound &r : g_rooms)
    nOk += r.bOk;
  const int nFound = static_cast<int>(g_rooms.size());
  const std::string found =
      PortTextNF("{0} room found on this network", "{0} rooms found on this network", nFound, {std::to_string(nFound)});
  MpCard(1, PortText("Join a Room"),
         PortText("Play in a room on this network. Pick one below, or type its address. This copy plays there itself, as one of the "
                  "room\xe2\x80\x99s installed players."),
         Keep(nOk != nFound ? PortTextF("{0}, {1} this copy can join", {found, std::to_string(nOk)}) : found),
         "J", false, cw);
  Close();
  RoomsBox(13, 93 + 320 + gap, leftW, fH - 93 - 320 - gap - 20);
  HowBox(fW - right - info, 93, info, fH - 93 - 20);
}

bool MultiplayerKeys(void)
{
  if (!g_pwFor.empty()) {
    if (Key(VK_ESCAPE))
      g_pwFor.clear();
    else if (Key(VK_RETURN)) {
      LanRoomFound r;
      r.addr = g_pwAddr;
      r.nPort = g_nPwPort;
      r.bOk = true;
      const std::string name = g_pwFor;
      g_pwFor.clear();
      JoinRoom(r, g_password);
      g_joining = name;
    }
    return true;
  }
  if (g_bAddrFocus) {
    if (Key(VK_RETURN)) {
      g_bAddrFocus = false;
      JoinAddress();
    } else if (Key(VK_ESCAPE)) {
      g_bAddrFocus = false;
    }
    return true;
  }
  if (Key('H')) {
    ServerStart();
    ToConsole();
  } else if (Key('J')) {
    for (const LanRoomFound &r : g_rooms)
      if (r.bOk) {
        JoinAsked(r);
        break;
      }
  } else if (Key('O')) {
    g_nResult = FRONT_LAN_ORIGINAL;
    g_bCameBack = true;  // the original's screens left, Multiplayer again
  } else if (Key(VK_ESCAPE) || Key('B')) {
    g_nResult = FRONT_LAN_HOME;
  }
  return true;
}

// ---------------------------------------------------------------------------
// The room, as a guest sees it while not in a lobby.

void RoomScreen(float fW, float fH)
{
  const PortJson &room = Room();
  if (!LanClientIn()) {
    // the room closed, or this copy was removed
    g_nView = VIEW_MULTIPLAYER;
    return;
  }
  const int nIn = static_cast<int>(People().size());
  Head(fW, room["name"].Str(PortText("The room")),
       PortTextF("Hosted on {0}", {room["machine"].Str()}) + kDot + LanClientAddress() + kDot +
           PortTextNF("{0} device in the room", "{0} devices in the room", nIn, {std::to_string(nIn)}));
  Decl tabs = Floating("lntabs", 0, -Sc(34), Sc(32), 22, CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP);
  tabs.d.layout.childGap = Gap(Fs(8));
  Open(tabs);
  const PortJson *me = DeviceOf(You());
  const bool bSeated = me != NULL && (*me)["state"].Str() == "lobby";
  Decl lb = Box("lnroomlobby");
  lb.d.layout.sizing.width = Fit();
  lb.d.layout.sizing.width.size.minMax.min = Sc(150);
  lb.d.layout.sizing.height = Fixed(Sc(40));
  if (ScMini(lb, PortText("The Lobby"), 'L', bSeated))
    g_nView = VIEW_LOBBY;
  if (!bSeated)
    TipOn(Id("lnroomlobby"), PortText("The lobby"),
          PortText("The host seats you in a match; its lobby opens then."));
  Decl lv = Box("lnleave");
  lv.d.layout.sizing.width = Fit();
  lv.d.layout.sizing.width.size.minMax.min = Sc(170);
  lv.d.layout.sizing.height = Fixed(Sc(40));
  if (ScMini(lv, PortText("Leave Room"), 'B'))
    GuestBack();
  Close();
  const float right = 34, gap = 10, half = (fW - 13 - right - gap) / 2;
  BoxAt("lnroommatches", 13, 93, half, fH - 93 - 20, PortText("Matches"));
  const PortJson &ms = room["matches"];
  if (ms.size() == 0)
    Fine(PortText("No match yet. The host opens one, and seats the room\xe2\x80\x99s people in it."), kLbInkDesc, 16);
  for (size_t i = 0; i < ms.size(); ++i) {
    const PortJson &m = ms.At(i);
    Decl c;
    c.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    c.d.layout.sizing.width = Grow();
    c.d.layout.padding = Pad(Fs(14), Fs(14), Fs(12), Fs(12));
    c.d.layout.childGap = Gap(Fs(4));
    c.d.backgroundColor = Col(kLbItem);
    c.d.border = Border(kLbItemRim, Fs(2), Fs(2), Fs(2), Fs(2));
    Radius(c, 4);
    Open(c);
    Text(Keep(m["name"].Str()), UI_FACE_BOLD, Fs(19), 0.0f, kLbInkInfo);
    const std::string st = m["state"].Str();
    Text(Keep((m["mapName"].Str().empty() ? std::string(PortText("Choosing a map")) : m["mapName"].Str()) + kDot +
              (st == "lobby"      ? std::string(PortText("in its lobby"))
               : st == "starting" ? std::string(PortText("starting"))
               : st == "live"     ? PortTextF("playing for {0}", {Clock(GameSeconds(m))})
               : m["result"].Str().empty() ? std::string(PortText("ended"))
                                           : PortTextF("ended: {0}", {m["result"].Str()}))),
         UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
    std::string who;
    const PortJson &seats = m["seats"];
    for (size_t k = 0; k < seats.size(); ++k) {
      const std::string o = seats.At(k)["owner"].Str();
      if (o == "player" || o == "computer")
        who += (who.empty() ? "" : ", ") + (o == "computer" ? std::string(PortText("a computer")) : seats.At(k)["name"].Str());
    }
    if (!who.empty())
      Text(Keep(who), UI_FACE_TEXT, Fs(14), 0.0f, kInkPlayer);
    if (st == "live") {
      const char *pszOff = WatchOff(m);
      Decl wb = Box("lnroomwatch", static_cast<int>(i));
      wb.d.layout.sizing.height = Fixed(Sc(34));
      if (ScMini(wb, PortText("Watch"), 0, pszOff == NULL))
        WatchMatch(static_cast<int>(m["id"].Num()));
      TipOn(Id("lnroomwatch", static_cast<int>(i)), PortText("Watch this match"),
            pszOff != NULL
                ? pszOff
                : Keep(PortTextF("From this machine, {0} s behind the players, with the replay player\xe2\x80\x99s camera.",
                                 {std::to_string(static_cast<int>(Room()["delay"].Num(10)))})));
    }
    Close();
  }
  Close();
  BoxAt("lnroompeople", 13 + half + gap, 93, half, fH - 93 - 20, PortText("In the Room"));
  for (const PortJson *d : People()) {
    if ((*d)["state"].Str() == "refused")
      continue;  // the host's console says who could not join; the room's people need not
    Decl r;
    r.d.layout.sizing.width = Grow();
    r.d.layout.childGap = Gap(Fs(10));
    r.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(r);
    Glyph(*d, 22);
    Decl nm;
    nm.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    nm.d.layout.sizing.width = Grow();
    Open(nm);
    std::string line, detail;
    Doing(*d, &line, &detail);
    // {0} the player's name
    Text(Keep(static_cast<int>((*d)["id"].Num()) == You() ? PortTextF("{0} (you)", {(*d)["name"].Str()})
                                                          : (*d)["name"].Str()),
         UI_FACE_BOLD, Fs(16), 0.0f, kLbInkInfo);
    Text(Keep(line + (detail.empty() ? std::string() : kDot + detail)), UI_FACE_TEXT, Fs(13), 0.0f,
         (*d)["state"].Str() == "refused" ? kBad : kLbInkDesc);
    Close();
    RouteChip(*d, true);
    Rtt(*d);
    Close();
  }
  Close();
}

bool RoomKeys(void)
{
  const PortJson *me = DeviceOf(You());
  if (Key('L') && me != NULL && (*me)["state"].Str() == "lobby")
    g_nView = VIEW_LOBBY;
  else if (Key(VK_ESCAPE) || Key('B'))
    GuestBack();
  return true;
}

// The lobby: opened once on entering, then kept in step with the room each frame.
void LobbyEnter(bool bHost)
{
  g_bChatFocus = false;
  g_chat.clear();
  g_sent = Sent();
  SkirmishOpen(6, bHost ? &g_hostLobby : &g_guestLobby);
  g_bLobbyOpened = true;
}

}  // namespace

namespace FrontKit {
namespace Lan {

void ToLobby(int nMatch)
{
  g_nLobbyMatch = nMatch;
  g_nView = VIEW_LOBBY;
  g_bLobbyOpened = false;
}

}  // namespace Lan

void LanOpen(void)
{
  g_nResult = 0;
  g_bAddrFocus = false;
  g_refusal.clear();
  g_lbOpen.clear();
  // back in a room: its console or its room
  if (LanClientAdmin())
    ToConsole();
  else if (LanClientIn())
    g_nView = VIEW_ROOM;
  else
    g_nView = VIEW_MULTIPLAYER;
  FrontLog("lan: open, %s\n", g_nView == VIEW_CONSOLE ? "the console" : g_nView == VIEW_ROOM ? "the room" : "Multiplayer");
}

bool LanJoinedNow(void)
{
  if (!g_bJoinOption || !LanClientIn())
    return false;
  g_bJoinOption = false;
  return true;
}

bool LanCameBack(void)
{
  const bool b = g_bCameBack || LanClientIn();
  g_bCameBack = false;
  return b;
}

int LanResult(void)
{
  const int n = g_nResult;
  g_nResult = 0;
  return n;
}

const char *LanPanelId(void)
{
  return g_nView == VIEW_LOBBY ? "skpanel" : "lnpanel";
}

void LanScreen(void)
{
  g_sparks.clear();
  ServerPump();
  // HYPERION_FRONT_OPEN=lanhost-match: a new match and its lobby, once the console is in
  static bool s_bMatchMade;
  if (!s_bMatchMade && g_nView == VIEW_CONSOLE && LanClientAdmin())
    if (const char *pszOpen = SDL_getenv("HYPERION_FRONT_OPEN"))
      if (strcmp(pszOpen, "lanhost-match") == 0) {
        s_bMatchMade = true;
        NewMatch();
        ToLobby(0);
      }
  RttSample();
  // a match being watched: its replay starts once the room has sent its beginning
  LanWatchPump();
  if (LanWatchReady())
    g_nResult = FRONT_LAN_WATCH;
  if (LanWatchWaiting() && SDL_GetTicks() - g_qwWatchAsked > 15000) {
    LanWatchCancel();
    Toast(PortText("The Match Did Not Come"), PortText("The room sent nothing of it. It may have ended."));
  }
  // a guest seated in a lobby goes to it; one unseated comes back to the room
  if (!LanClientAdmin() && LanClientIn()) {
    const PortJson *me = DeviceOf(You());
    const std::string st = me != NULL ? (*me)["state"].Str() : std::string();
    const PortJson *m = me != NULL ? MatchOf(static_cast<int>((*me)["match"].Num())) : NULL;
    const bool bLobby = st == "lobby" && m != NULL && !(*m)["map"].Str().empty();
    if (bLobby && g_nView == VIEW_ROOM && !g_bLobbyOpened)
      g_nView = VIEW_LOBBY;
    if (!bLobby && g_nView == VIEW_LOBBY) {
      g_nView = VIEW_ROOM;
      g_bLobbyOpened = false;
    }
  }
  if (g_nView == VIEW_CONSOLE && !LanClientAdmin() && !ServerRunning() && LanClientIn())
    g_nView = VIEW_ROOM;
  if (g_nView == VIEW_LOBBY) {
    const bool bHost = LanClientAdmin();
    if (bHost) {
      if (const int nMade = LanClientTakeMade())
        g_nLobbyMatch = nMade;
      const PortJson *m = MatchOf(g_nLobbyMatch);
      if (m == NULL || (*m)["state"].Str() != "lobby") {
        if (m != NULL || g_nLobbyMatch == 0 || Room()["matches"].kind != PortJson::NONE) {
          // the match has started, ended or gone: the console
          if (m != NULL && g_nLobbyMatch != 0) {
            g_nView = VIEW_CONSOLE;
            g_bLobbyOpened = false;
          }
        }
      }
    }
    if (g_nView == VIEW_LOBBY) {
      if (!g_bLobbyOpened)
        LobbyEnter(bHost);
      if (bHost)
        HostSync();
      else
        GuestSync();
      SkirmishScreen();
      // the host's answer to an ask over the lobby
      std::string id;
      const int n = AskShown(&id);
      if (id == "notready" && n == 1)
        LanClientSay("{\"t\":\"start\",\"match\":" + std::to_string(g_nLobbyMatch) + ",\"force\":true}");
      ToastsDraw();
      return;
    }
  }
  g_bLobbyOpened = false;
  MapPreviewFrame();
  const float fW = UiWidth() / Sc(1) - 116;
  const float fH = fminf(990.0f, UiHeight() / Sc(1) - 132 - 74);
  PanelOpen("lnpanel", fW, fH);
  if (g_nView == VIEW_CONSOLE)
    ConsoleScreen(fW, fH);
  else if (g_nView == VIEW_ROOM)
    RoomScreen(fW, fH);
  else
    MultiplayerScreen(fW, fH);
  Close();
  PasswordDialog();
  DrawTip();
  ToastsDraw();
  PulldownCloseOutside();
}

bool LanKeys(void)
{
  if (AskKeys())
    return true;
  if (PulldownKeys())
    return true;
  if (g_nView == VIEW_LOBBY)
    return SkirmishKeys();
  if (g_nView == VIEW_CONSOLE)
    return ConsoleKeys();
  if (g_nView == VIEW_ROOM)
    return RoomKeys();
  return MultiplayerKeys();
}

bool LanFrontFrame(void)
{
  static bool s_bJoined;
  static Uint64 s_qwReadySaid;
  const PortOptions *o = PortCommandOptions();
  if (!s_bJoined && o->szJoin[0]) {
    s_bJoined = true;
    std::string addr = o->szJoin;
    int nPort = 0;
    const size_t n = addr.find(':');
    if (n != std::string::npos) {
      nPort = atoi(addr.c_str() + n + 1);
      addr = addr.substr(0, n);
    }
    const bool bIn = LanClientJoin(addr.c_str(), nPort, o->szRoomPassword, o->szRoomKey);
    g_bJoinOption = bIn;
    FrontLog("lan: joining the room at %s%s\n", o->szJoin, bIn ? "" : ": nothing answers there");
    if (bIn && o->szRace[0])
      LanClientSay(std::string("{\"t\":\"race\",\"race\":") + PortJsonQuote(o->szRace) + "}");
    // and --handicaps' first, this seat's own (port_handicap.h)
    if (bIn && o->szHandicaps[0])
      LanClientSay("{\"t\":\"handicap\",\"handicap\":" + std::to_string(HandicapClamp(atoi(o->szHandicaps))) + "}");
  }
  LanClientPump();
  // a test's seat says Ready in every lobby it is seated in
  if (o->bLanReady && LanClientIn() && SDL_GetTicks() - s_qwReadySaid > 2000) {
    const PortJson &room = LanClientRoom();
    const int nYou = LanClientYou();
    for (size_t i = 0; i < room["devices"].size(); ++i) {
      const PortJson &d = room["devices"].At(i);
      if (static_cast<int>(d["id"].Num()) == nYou && d["state"].Str() == "lobby" && !d["ready"].b) {
        s_qwReadySaid = SDL_GetTicks();
        LanClientSay("{\"t\":\"ready\",\"on\":true}");
      }
    }
  }
  // the room's copy of a match (--lan-copy <match>:<token>): the room has made it a watcher
  static bool s_bCopying;
  if (o->szLanCopy[0] && LanClientIn() && !s_bCopying) {
    s_bCopying = true;
    LanWatchAsCopy(atoi(o->szLanCopy));
  }
  // a watching machine (--lan-watch) watches each match being played, once, as Watch does
  static int s_nWatched;
  if (o->bLanWatch && LanClientIn() && !LanWatchWaiting() && !LanWatchReady()) {
    const PortJson &ms = LanClientRoom()["matches"];
    for (size_t i = 0; i < ms.size(); ++i) {
      const int nId = static_cast<int>(ms.At(i)["id"].Num());
      if (nId > s_nWatched && WatchOff(ms.At(i)) == NULL) {
        s_nWatched = nId;
        WatchMatch(nId);
        break;
      }
    }
  }
  // a browser's watching seat (--lan-stream): the match the room says to show, as Watch does
  if (const int nFollow = LanClientFollow())
    if (LanClientIn() && !LanWatchWaiting() && !LanWatchReady()) {
      LanClientFollowed();
      const PortJson *m = MatchOf(nFollow);
      if (m != NULL && WatchOff(*m) == NULL)
        WatchMatch(nFollow);
    }
  for (const std::string &note : LanClientNotes())
    if (note == "The room has closed." && g_bStopAsked)
      Toast(PortText("The server stopped"), PortText("Nobody can join until it starts again."));
    else
      Toast(note, "");
  if (LanClientIn())
    g_bStopAsked = false;
  // a room's match whose unseen chat room closed before the game: what 1.17 would have said
  if (const char *pszWhy = PortLanTakeWhy())
    Toast(PortText("The match did not start"), pszWhy);
  PortJson launch;
  if (LanClientTakeLaunch(&launch)) {
    FrontLog("lan: the room launched match %d; %s\n", static_cast<int>(launch["match"].Num()),
             launch["role"].Str() == "create" ? "this copy makes the game" : "this copy joins it");
    if (PortLaunchNetGame(launch)) {
      g_bCameBack = true;
      return true;
    }
    FrontLog("lan: the match could not be %s\n", launch["role"].Str() == "create" ? "made" : "joined");
    Toast(PortText("The match could not start here"), "");
    PortLanLobbyEnd();
  }
  return false;
}

}  // namespace FrontKit
