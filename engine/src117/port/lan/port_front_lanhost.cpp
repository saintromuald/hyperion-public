// port_front_lanhost.cpp -- the host's console: the room this machine hosts, as
// its own Hyperion sees it while the room runs as a
// process of its own (port_lanroom.cpp). The header says how the server stands, with the tabs
// (Room, Devices, Tournament, Settings) and Stop Server or Start Server. The Room tab shows the
// matches as tables on a floor, as a rack of rows, or as a wall of pictures (the room's own
// setting, the floor first), beside the join card (the address, its code for a phone's camera,
// the password) and this machine's load. Devices is port_front_landevices.cpp's. Presentation
// only: what it changes, it asks the room to change.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "../platform/port_json.h"  // PortJsonQuote
#include "port_front_lan.h"
#include "../front/port_frontscreens.h"
#include "port_lanclient.h"
#include "port_lanproto.h"
#include "port_qr.h"
#include "../front/port_ui.h"

using namespace FrontKit;
using namespace FrontKit::Lan;

namespace {

const UiColor kInkPlayer = KitRgb(0x64d0f9);  // lobby.css --lb-ink-player

enum { TAB_ROOM, TAB_DEVICES, TAB_CUP, TAB_SETTINGS };
const float kJoinH = 430;  // the join card's height
int g_nTab;
bool g_bShowPassword;
std::vector<float> g_cpu;  // the processor's load, the last half minute
Uint64 g_qwCpuAt;

// The console's settings, the room's lanroom.json: what this screen keeps, written back whole.
struct Settings {
  bool bRead = false;
  std::string name;
  bool bPassword = true;
  int nDir = 0;  // the Room tab: 0 floor, 1 rack, 2 wall
  std::string delay = "10 s", watch = "Anyone in the room";
  std::string browserFiles = "Offer";  // the host's 1.17 files to a browser that plays in its tab
} g_set;

// the settings' own words, which the room keeps and the page reads: kept in English, each
// translated where a pulldown shows it
const std::vector<std::string> kDelays = {PT_("5 s"), PT_("10 s"), PT_("30 s"), PT_("1 min")};
const std::vector<std::string> kWatchers = {PT_("Anyone in the room"), PT_("Nobody")};

std::string SettingsJson(void)
{
  static const char *const kDirs[] = {"floor", "rack", "wall"};
  return std::string("{\"name\":") + PortJsonQuote(g_set.name) + ",\"password\":" + (g_set.bPassword ? "true" : "false") +
         ",\"dir\":\"" + kDirs[g_set.nDir] + "\",\"delay\":" + PortJsonQuote(g_set.delay) +
         ",\"watch\":" + PortJsonQuote(g_set.watch) + ",\"browserFiles\":" + PortJsonQuote(g_set.browserFiles) +
         ",\"fp\":" + PortJsonQuote(LanFingerprint()) + "}";
}

void SettingsSend(void)
{
  LanClientSay("{\"t\":\"settings\",\"raw\":" + PortJsonQuote(SettingsJson()) + "}");
}

void SettingsRead(void)
{
  if (g_set.bRead || Room()["settings"].kind != PortJson::OBJECT)
    return;
  const PortJson &s = Room()["settings"];
  g_set.bRead = true;
  g_set.name = s["name"].Str(Room()["name"].Str().c_str());
  g_set.bPassword = s["password"].kind != PortJson::BOOL || s["password"].b;
  const std::string dir = s["dir"].Str();
  g_set.nDir = dir == "rack" ? 1 : dir == "wall" ? 2 : 0;
  g_set.delay = s["delay"].Str("10 s");
  g_set.watch = s["watch"].Str("Anyone in the room");
  g_set.browserFiles = s["browserFiles"].Str("Offer");
  // the room checks each copy's game data against the host's: its fingerprint is said once
  if (s["fp"].Str() != LanFingerprint())
    SettingsSend();
}

// ---------------------------------------------------------------------------
// The join card's code.

struct QrCache {
  std::string text;
  std::vector<std::vector<bool>> m;
} g_qr;

void DrawQr(float x, float y, float w, float h, void *)
{
  const int n = static_cast<int>(g_qr.m.size());
  UiFill(x, y, w, h, KitRgb(0xffffff), Sc(4));
  if (n == 0)
    return;
  const float fModule = fminf(w, h) / static_cast<float>(n + 8);
  const float ox = x + (w - fModule * n) / 2, oy = y + (h - fModule * n) / 2;
  for (int r = 0; r < n; ++r)
    for (int c = 0; c < n; ++c)
      if (g_qr.m[r][c])
        UiFill(ox + c * fModule, oy + r * fModule, ceilf(fModule), ceilf(fModule), KitRgb(0x000000));
}

std::string PageUrl(void)
{
  return "http://" + Room()["address"].Str() + ":" + std::to_string(static_cast<int>(Room()["web"].Num(7117)));
}

// ---------------------------------------------------------------------------
// The header.

bool Running(void)
{
  return ServerRunning() && LanClientAdmin();
}

void Status(void)
{
  const PortJson &room = Room();
  Decl st;
  st.d.layout.childGap = Gap(Fs(8));
  st.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(st);
  Decl light;
  light.d.layout.sizing.width = Fixed(Sc(10));
  light.d.layout.sizing.height = Fixed(Sc(10));
  light.d.backgroundColor = Col(Running() ? kOn : kOff);
  Radius(light, 5);
  Open(light);
  Close();
  if (Running()) {
    int nPlaying = 0, nWatching = 0;
    for (const PortJson *d : People()) {
      nPlaying += (*d)["state"].Str() == "playing";
      nWatching += (*d)["state"].Str() == "watching";
    }
    const int nLive = static_cast<int>(room["live"].Num());
    const int nIn = static_cast<int>(People().size());
    Text(Keep(std::string(room["stopAfter"].b ? PortText("Stopping after the last match") : PortText("Running")) + kDot +
              // {0} how long the room has been up
              PortTextF("up {0}", {Ago(room["up"].Num())}) + kDot +
              PortTextNF("{0} device", "{0} devices", nIn, {std::to_string(nIn)}) + kDot +
              PortTextNF("{0} match", "{0} matches", nLive, {std::to_string(nLive)}) + kDot +
              PortTextF("{0} playing, {1} watching", {std::to_string(nPlaying), std::to_string(nWatching)})),
         UI_FACE_TEXT, Fs(17), 0.0f, kLbInkInfo);
  } else {
    Text(ServerRunning() ? PortText("Starting\xe2\x80\xa6") : PortText("Stopped \xc2\xb7 nobody can join"), UI_FACE_TEXT,
         Fs(17), 0.0f, kLbInkInfo);
  }
  Close();
}

void AskStop(void);

void Header(float fW)
{
  const PortJson &room = Room();
  Decl head = At("lnhhead", 40, 22, fW - 40 - 900, 0);
  head.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  head.d.layout.childGap = Gap(Fs(2));
  Open(head);
  Text(Keep(room["name"].Str(PortText("Your room"))), UI_FACE_BOLD, Fs(27), 0.0f, kScInkHi);
  Status();
  if (Running())
    // {0} the port games are joined on, {1} the port the room is found on, {2} the page's port
    Text(Keep(room["machine"].Str() + kDot + room["address"].Str() + kDot +
              PortTextF("games on {0}, found on {1}, the page on {2}",
                        {std::to_string(static_cast<int>(room["port"].Num())),
                         std::to_string(static_cast<int>(room["port"].Num()) - 1),
                         std::to_string(static_cast<int>(room["web"].Num()))})),
         UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
  Close();
  Decl hb = Floating("lnhtabs", 0, -Sc(34), Sc(32), 22, CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP);
  hb.d.layout.childGap = Gap(Fs(8));
  hb.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(hb);
  // {0} how many devices are in the room
  const std::string names[] = {PortText("Room"), PortTextF("Devices \xc2\xb7 {0}", {std::to_string(People().size())}),
                               PortText("Tournament"), PortText("Settings")};
  for (int i = 0; i < 4; ++i) {
    Decl b = Box("lnhtab", i);
    b.d.layout.sizing.width = Fit();
    b.d.layout.sizing.width.size.minMax.min = Sc(150);
    b.d.layout.sizing.height = Fixed(Sc(40));
    if (i == g_nTab)
      b.d.border = Border(kLbHyperionRim, Fs(2), Fs(2), Fs(2), Fs(2));
    if (ScMini(b, Keep(names[i]), static_cast<char>('1' + i), i != TAB_CUP))
      g_nTab = i;
  }
  TipOn(Id("lnhtab", TAB_CUP), PortText("Tournament"),
        PortText("Single and double elimination, round robin and best-of series, played in this room. Not available yet."));
  Decl sep;
  sep.d.layout.sizing.width = Fixed(Sc(2));
  sep.d.layout.sizing.height = Fixed(Sc(36));
  sep.d.backgroundColor = Col(kLbDivider);
  Open(sep);
  Close();
  Decl sv = Box("lnhserver");
  sv.d.layout.sizing.width = Fit();
  sv.d.layout.sizing.width.size.minMax.min = Sc(170);
  sv.d.layout.sizing.height = Fixed(Sc(40));
  const bool bRunning = ServerRunning();
  if (ScMini(sv, bRunning ? PortText("Stop Server") : PortText("Start Server"), 'S')) {
    if (bRunning)
      AskStop();
    else
      ServerStart();
  }
  TipOn(Id("lnhserver"), bRunning ? PortText("Stop the server") : PortText("Start the server"),
        bRunning ? PortText("The matches running end, and nobody can join until it starts again. Hotkey: S")
                 : PortText("Listens on this network, at the same address and password. Hotkey: S"));
  Close();
}

void AskStop(void)
{
  int nLive = 0, nPlaying = 0, nWatching = 0;
  const PortJson &ms = Room()["matches"];
  for (size_t i = 0; i < ms.size(); ++i)
    nLive += ms.At(i)["state"].Str() == "live";
  for (const PortJson *d : People()) {
    nPlaying += (*d)["state"].Str() == "playing";
    nWatching += (*d)["state"].Str() == "watching";
  }
  if (nLive == 0) {
    Ask("stopidle", PortText("Stop the Server?"),
        PortText("No match is running. Everyone in the room goes back to Multiplayer, and nobody can join until it "
                 "starts again."),
        {{'S', PortText("Stop Server"), ""}, {'C', PortText("Cancel"), PortText("The server keeps running.")}});
    return;
  }
  // {0} how many matches are running, {1} how many players are in them, {2} how many watch
  Ask("stop", PortText("Stop the Server?"),
      PortTextF("{0} running, with {1} and {2} watching.",
                {PortTextNF("{0} match", "{0} matches", nLive, {std::to_string(nLive)}),
                 PortTextNF("{0} player", "{0} players", nPlaying, {std::to_string(nPlaying)}),
                 std::to_string(nWatching)}),
      {{'S', PortText("Stop Server"),
        PortText("The matches end now, each one\xe2\x80\x99s replay kept on its players\xe2\x80\x99 machines as it stands; installed copies "
                 "go back to Multiplayer.")},
       {'W', PortText("Stop When the Matches End"),
        PortText("Nobody new can join; the matches running play on, and the server stops after the last.")},
       {'C', PortText("Cancel"), PortText("The server keeps running.")}});
}

// ---------------------------------------------------------------------------
// The right column: the join card, and this machine.

void JoinCard(float x, float y, float w)
{
  BoxAt("lnjoincard", x, y, w, kJoinH, PortText("Join This Room"));
  const std::string url = PageUrl();
  if (g_qr.text != url) {
    g_qr.text = url;
    g_qr.m = QrMatrix(url);
  }
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.childGap = Gap(Fs(16));
  row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(row);
  Decl qr;
  qr.d.layout.sizing.width = Fixed(Sc(170));
  qr.d.layout.sizing.height = Fixed(Sc(170));
  static UiCustom s_qr = {0.0f, DrawQr, NULL};
  qr.d.custom.customData = &s_qr;
  Open(qr);
  Close();
  Decl t;
  t.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  t.d.layout.sizing.width = Grow();
  t.d.layout.childGap = Gap(Fs(6));
  Open(t);
  Fine(PortText("Point a phone\xe2\x80\x99s camera at the code, or open the address below in any browser on this network."),
       kLbInkDesc, 15);
  Fine(PortText("A browser plays in its tab, or streamed from this machine; installing on a PC is not available yet."),
       kLbInkDesc, 15);
  Close();
  Close();
  Decl big;
  big.d.layout.sizing.width = Grow();
  big.d.layout.padding = Pad(0, 0, Fs(6), Fs(6));
  big.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  big.d.backgroundColor = Col(KitRgb(0x3980bc, 0.14f));
  Radius(big, 4);
  Open(big);
  Text(Keep(url.substr(7)), UI_FACE_BOLD, Fs(30), 0.01f, kScInkHi);
  Close();
  Decl pw;
  pw.d.layout.sizing.width = Grow();
  pw.d.layout.padding = Pad(0, 0, Fs(10), Fs(10));
  pw.d.layout.childGap = Gap(Fs(10));
  pw.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  pw.d.border = Border(kLbDivider, 0, 0, Fs(2), Fs(2));
  Open(pw);
  Text(PortText("Password"), UI_FACE_BOLD, Fs(16), 0.0f, kScInkLabel);
  Decl val;
  val.d.layout.sizing.width = Grow();
  Open(val);
  const bool bOn = Room()["password"].b;
  Text(!bOn ? PortText("Off: anyone on this network can join") : g_bShowPassword ? Keep(Room()["passwordText"].Str())
                                                                       : "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2",
       UI_FACE_TEXT, Fs(16), 0.0f, kLbInkInfo);
  Close();
  Decl sh = Box("lnpwshow");
  sh.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(sh, g_bShowPassword ? PortText("Hide") : PortText("Show"), 0, bOn))
    g_bShowPassword = !g_bShowPassword;
  Decl nw = Box("lnpwnew");
  nw.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(nw, PortText("New"), 0, bOn && Running()))
    Ask("password", PortText("A New Password?"),
        PortText("Everyone already in stays in. Anyone joining from now on needs the new one, and the code above does "
                 "not carry it."),
        {{'N', PortText("Make a New Password"),
          PortText("Three words and a number, easy to read out across a room.")},
         {'C', PortText("Cancel"), ""}});
  Close();
  Fine(Running() ? PortText("This network only. Everyone playing needs their own copy of StarCraft.")
                 : PortText("The server is stopped: this address answers again once it starts."),
       kLbInkInfo);
  Close();
}

void Meter(const char *pszName, float f, const std::string &value, bool bOff)
{
  Decl r;
  r.d.layout.sizing.width = Grow();
  r.d.layout.childGap = Gap(Fs(10));
  r.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(r);
  Decl n;
  n.d.layout.sizing.width = Fixed(Sc(84));
  Open(n);
  Text(pszName, UI_FACE_BOLD, Fs(14), 0.0f, bOff ? kScInkOff : kScInkLabel);
  Close();
  Bar(0, f, f > 0.85f);
  Decl v;
  v.d.layout.sizing.width = Fixed(Sc(130));
  v.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
  Open(v);
  Text(Keep(value), UI_FACE_TEXT, Fs(13), 0.0f, bOff ? kScInkOff : kLbInkInfo);
  Close();
  Close();
}

void LoadBox(float x, float y, float w, float h)
{
  BoxAt("lnload", x, y, w, h, PortText("This Machine"));
  const PortJson &load = Room()["load"];
  const float fCpu = static_cast<float>(load["cpu"].Num() / 10.0);
  const float fMem = static_cast<float>(load["mem"].Num()), fTotal = static_cast<float>(load["memTotal"].Num());
  const Uint64 qwNow = SDL_GetTicks();
  if (Running() && qwNow - g_qwCpuAt >= 1000) {
    g_qwCpuAt = qwNow;
    g_cpu.push_back(fCpu);
    if (g_cpu.size() > 30)
      g_cpu.erase(g_cpu.begin());
  }
  Fine(Keep(Room()["machine"].Str(PortText("This machine")) + kDot + VersionName(Room()["version"].Str())));
  char sz[64];
  SDL_snprintf(sz, sizeof sz, "%.0f%%", fCpu);
  Meter(PortText("CPU"), fCpu / 100.0f, sz, !Running());
  Meter(PortText("Graphics"), 0, "\xe2\x80\x94", true);
  TipOn(Id("lnload"), PortText("This machine"),
        PortText("The processor and memory as the system counts them. The graphics card\xe2\x80\x99s load and the upload come with "
                 "players in a browser, which are not available yet."));
  Meter(PortText("Upload"), 0, "0.0 Mbit/s", !Running());
  char szTotal[32];
  SDL_snprintf(sz, sizeof sz, "%.1f", fMem / 1024.0f);
  SDL_snprintf(szTotal, sizeof szTotal, "%.0f", fTotal / 1024.0f);
  // {0} the memory in use, {1} the memory the machine has, both in gigabytes
  Meter(PortText("Memory"), fTotal > 0 ? fMem / fTotal : 0, PortTextF("{0} of {1} GB", {sz, szTotal}), !Running());
  Decl enc;
  enc.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  enc.d.layout.sizing.width = Grow();
  enc.d.layout.childGap = Gap(Fs(4));
  enc.d.layout.padding = Pad(0, 0, Fs(6), 0);
  enc.d.border = Border(kLbDivider, 0, 0, Fs(2), 0);
  Open(enc);
  Text(PortText("Streams"), UI_FACE_BOLD, Fs(14), 0.0f, kScInkLabel);
  Decl cells;
  cells.d.layout.sizing.width = Grow();
  cells.d.layout.childGap = Gap(Fs(5));
  Open(cells);
  for (int i = 0; i < 8; ++i) {
    Decl c;
    c.d.layout.sizing.width = Grow();
    c.d.layout.sizing.height = Fixed(Sc(22));
    c.d.border = Border(kLbItemRim, Fs(2), Fs(2), Fs(2), Fs(2));
    Radius(c, 3);
    Open(c);
    Close();
  }
  Close();
  Fine(PortText("Nothing is being sent: players in a browser are not available yet."));
  Close();
  Spacer();
  Text(PortText("CPU, THE LAST HALF MINUTE"), UI_FACE_WIDE, Fs(12), 0.04f, kScInkLabel);
  Spark(g_cpu, 100.0f, 0, 30, kStream);
  Close();
}

// ---------------------------------------------------------------------------
// The room, as a floor of tables.

int SeatsOf(const PortJson &m, std::vector<int> *pSlots)
{
  const PortJson &seats = m["seats"];
  for (size_t i = 0; i < seats.size(); ++i) {
    const std::string o = seats.At(i)["owner"].Str();
    if (o == "player" || o == "computer")
      pSlots->push_back(static_cast<int>(i));
  }
  return static_cast<int>(pSlots->size());
}

// The seat that holds a match back: lockstep goes at the pace of the slowest.
const PortJson *Slowest(const PortJson &m, double *pfRtt)
{
  const PortJson *worst = NULL;
  *pfRtt = -1;
  const PortJson &seats = m["seats"];
  for (size_t i = 0; i < seats.size(); ++i)
    if (seats.At(i)["owner"].Str() == "player")
      if (const PortJson *d = DeviceOf(static_cast<int>(seats.At(i)["dev"].Num())))
        if ((*d)["rtt"].Num() > *pfRtt) {
          *pfRtt = (*d)["rtt"].Num();
          worst = d;
        }
  return worst;
}

void SeatTile(const PortJson &m, int nSlot, int nIndex)
{
  const PortJson &s = m["seats"].At(nSlot);
  const bool bPerson = s["owner"].Str() == "player";
  const PortJson *d = bPerson ? DeviceOf(static_cast<int>(s["dev"].Num())) : NULL;
  Decl t = Box("lnseat", nIndex);
  t.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  t.d.layout.sizing.width = Grow();
  t.d.layout.padding = Pad(Fs(12), Fs(12), Fs(12), Fs(10));
  t.d.layout.childGap = Gap(Fs(6));
  const bool bHot = d != NULL && Over(t.d.id);
  t.d.backgroundColor = Col(bHot ? kLbItemHi : kLbItem);
  t.d.border = Border(kLbItemRim, Fs(2), Fs(2), Fs(2), Fs(2));
  Radius(t, 4);
  if (d != NULL && Clicked(t.d.id))
    OpenDevice(static_cast<int>((*d)["id"].Num()));
  Open(t);
  Decl top;
  top.d.layout.sizing.width = Grow();
  top.d.layout.childGap = Gap(Fs(6));
  Open(top);
  Decl nm;
  nm.d.layout.sizing.width = Grow();
  Open(nm);
  Text(Keep(bPerson ? s["name"].Str() : std::string(PortText("Computer"))), UI_FACE_BOLD, Fs(18), 0.0f, kLbInkInfo);
  Close();
  std::string race = s["race"].Str("random");
  race[0] = static_cast<char>(toupper(static_cast<unsigned char>(race[0])));
  Text(Keep(race), UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
  Close();
  if (d == NULL) {
    Fine(bPerson ? PortText("Gone from the room") : PortText("Runs in every seat\xe2\x80\x99s game"), kLbInkDesc, 13);
  } else {
    Decl dv;
    dv.d.layout.sizing.width = Grow();
    dv.d.layout.childGap = Gap(Fs(6));
    dv.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(dv);
    Glyph(*d, 18);
    Text(Keep(KindName(*d)), UI_FACE_TEXT, Fs(14), 0.0f, kInkPlayer);
    Close();
    Decl rt;
    rt.d.layout.sizing.width = Grow();
    rt.d.layout.childGap = Gap(Fs(6));
    rt.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(rt);
    RouteChip(*d, true);
    Spacer();
    Rtt(*d);
    Close();
  }
  Close();
}

void MatchButtons(const PortJson &m, int i)
{
  const int nId = static_cast<int>(m["id"].Num());
  Decl w = Box("lnwatch", i);
  w.d.layout.sizing.height = Fixed(Sc(34));
  const char *pszOff = WatchOff(m);
  if (ScMini(w, PortText("Watch"), 0, pszOff == NULL))
    WatchMatch(nId);
  // {0} the delay behind the players
  TipOn(Id("lnwatch", i), PortText("Watch this match"),
        pszOff != NULL
            ? pszOff
            : Keep(PortTextF("From this machine, {0} behind the players, with the replay player\xe2\x80\x99s camera.",
                             {PortText(g_set.delay.c_str())})));
  Decl e = Box("lnend", i);
  e.d.layout.sizing.height = Fixed(Sc(34));
  if (ScMini(e, PortText("End"), 0))
    // {0} the match's name
    Ask("end:" + std::to_string(nId), PortTextF("End {0}?", {m["name"].Str()}),
        PortText("The players go back to the room; each keeps the replay as it stands, and nobody wins."),
        {{'E', PortText("End the Match"), ""}, {'C', PortText("Cancel"), PortText("It plays on.")}});
}

void Table(const PortJson &m, int i, float w)
{
  Decl c = Box("lntable", i);
  c.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  c.d.layout.sizing.width = Fixed(Sc(w));
  c.d.layout.padding = Pad(Fs(14), Fs(14), Fs(12), Fs(12));
  c.d.layout.childGap = Gap(Fs(10));
  c.d.backgroundColor = Col(kLbInner);
  c.d.border = Border(kLbInnerRim, Fs(2), Fs(2), Fs(2), Fs(2));
  Radius(c, 4);
  Open(c);
  const std::string st = m["state"].Str();
  const bool bLobby = st == "lobby";
  Decl h;
  h.d.layout.sizing.width = Grow();
  h.d.layout.childGap = Gap(Fs(10));
  Open(h);
  Decl nm;
  nm.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  nm.d.layout.sizing.width = Grow();
  nm.d.layout.childGap = Gap(Fs(2));
  Open(nm);
  Text(Keep(m["name"].Str()), UI_FACE_BOLD, Fs(20), 0.0f, kScInkHi);
  double fRtt;
  const PortJson *slow = Slowest(m, &fRtt);
  const bool bWaits = !bLobby && slow != NULL && fRtt > 60;
  Text(Keep((m["mapName"].Str().empty() ? std::string(PortText("Choosing a map")) : m["mapName"].Str()) + kDot +
            (bLobby             ? std::string(PortText("in the lobby"))
             : st == "starting" ? std::string(PortText("starting"))
             : st == "ended"    ? (m["result"].Str().empty() ? std::string(PortText("ended"))
                                                             : PortTextF("ended: {0}", {m["result"].Str()}))
                                : Clock(GameSeconds(m)) +
                                      (bWaits ? kDot + PortTextF("waits on {0}", {(*slow)["name"].Str()})
                                              : kDot + std::string(PortText("nobody holds it back"))))),
       UI_FACE_TEXT, Fs(14), 0.0f, bWaits ? kFair : kLbInkDesc);
  Close();
  Decl btns;
  btns.d.layout.childGap = Gap(Fs(6));
  Open(btns);
  if (bLobby) {
    Decl o = Box("lnopenlobby", i);
    o.d.layout.sizing.height = Fixed(Sc(34));
    if (ScMini(o, PortText("Open Lobby"), 'O'))
      ToLobby(static_cast<int>(m["id"].Num()));
  } else if (st == "ended") {
    Decl o = Box("lnclear", i);
    o.d.layout.sizing.height = Fixed(Sc(34));
    if (ScMini(o, PortText("Clear"), 0))
      LanClientSay("{\"t\":\"drop\",\"match\":" + std::to_string(static_cast<int>(m["id"].Num())) + "}");
  } else {
    MatchButtons(m, i);
  }
  Close();
  Close();
  std::vector<int> slots;
  SeatsOf(m, &slots);
  if (st == "ended") {
    std::string who;
    for (int k : slots) {
      const PortJson &s = m["seats"].At(k);
      who += (who.empty() ? "" : ", ") +
             (s["owner"].Str() == "player" ? s["name"].Str() : std::string(PortText("a computer")));
    }
    Fine(Keep(who), kInkPlayer);
    Close();
    return;
  }
  Decl seats;
  seats.d.layout.sizing.width = Grow();
  seats.d.layout.childGap = Gap(Fs(8));
  Open(seats);
  if (slots.empty())
    Fine(PortText("No seat taken yet."), kLbInkDesc);
  for (size_t k = 0; k < slots.size() && k < 4; ++k) {
    if (k > 0 && !bLobby && slots.size() == 2) {
      Decl vs;
      vs.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      vs.d.layout.sizing.height = Grow();
      Open(vs);
      Text(PortText("VS"), UI_FACE_WIDE, Fs(13), 0.0f, kLbInkDesc);
      Close();
    }
    SeatTile(m, slots[k], i * 16 + static_cast<int>(k));
  }
  Close();
  if (slots.size() > 4)
    Fine(Keep(PortTextF("and {0} more", {std::to_string(slots.size() - 4)})), kLbInkDesc);
  if (!bLobby) {
    Decl f;
    f.d.layout.sizing.width = Grow();
    f.d.layout.padding = Pad(0, 0, Fs(8), 0);
    f.d.border = Border(kLbDivider, 0, 0, Fs(2), 0);
    Open(f);
    const int nWatch = static_cast<int>(m["watchers"].size());
    Text(nWatch ? Keep(PortTextNF("{0} person watching", "{0} people watching", nWatch, {std::to_string(nWatch)}))
                : PortText("Nobody watching"),
         UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
    Spacer();
    const PortJson &hashes = m["hashes"];
    if (hashes.kind == PortJson::OBJECT)
      // {0} how many times the seats' games were compared
      Text(Keep(hashes["differed"].Num() > 0
                    ? std::string(PortText("The seats\xe2\x80\x99 games differ"))
                    : PortTextF("{0} checks, every seat agreed",
                                {std::to_string(static_cast<int>(hashes["agreed"].Num()))})),
           UI_FACE_TEXT, Fs(14), 0.0f, hashes["differed"].Num() > 0 ? kBad : kInkPlayer);
    Close();
  }
  Close();
}

void DevChip(const PortJson &d, int i, float w)
{
  Decl c = Box("lnchip", i);
  c.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  c.d.layout.sizing.width = Fixed(Sc(w));
  c.d.layout.padding = Pad(Fs(12), Fs(12), Fs(10), Fs(10));
  c.d.layout.childGap = Gap(Fs(5));
  const bool bHot = Over(c.d.id);
  c.d.backgroundColor = Col(bHot ? kLbItemHi : kLbItem);
  c.d.border = Border(kLbItemRim, Fs(2), Fs(2), Fs(2), Fs(2));
  Radius(c, 4);
  if (Clicked(c.d.id))
    OpenDevice(static_cast<int>(d["id"].Num()));
  Open(c);
  Decl a;
  a.d.layout.sizing.width = Grow();
  a.d.layout.childGap = Gap(Fs(7));
  a.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(a);
  Glyph(d, 20);
  Text(Keep(d["name"].Str()), UI_FACE_BOLD, Fs(17), 0.0f, kLbInkInfo);
  Spacer();
  RouteChip(d, true);
  Close();
  Decl dev;
  dev.d.layout.sizing.width = Grow();
  dev.d.layout.childGap = Gap(Fs(6));
  Open(dev);
  Text(Keep(d["machine"].Str() + kDot + KindName(d)), UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
  Spacer();
  if (d["state"].Str() != "refused")
    Rtt(d);
  Close();
  std::string line, detail;
  Doing(d, &line, &detail);
  if (d["state"].Str() == "refused")
    Text(Keep(d["why"].Str()), UI_FACE_TEXT, Fs(13), 0.0f, kBad, -1, 1.3f);
  else
    Text(Keep(line), UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
  Close();
}

void Tonight(int nLines)
{
  Title(PortText("Tonight"));
  const PortJson &log = Room()["log"];
  for (size_t i = 0; i < log.size() && static_cast<int>(i) < nLines; ++i) {
    const PortJson &l = log.At(i);
    Decl r;
    r.d.layout.sizing.width = Grow();
    r.d.layout.childGap = Gap(Fs(12));
    Open(r);
    Decl t;
    t.d.layout.sizing.width = Fixed(Sc(90));
    Open(t);
    const double fAgo = l["ago"].Num();
    Text(Keep(fAgo < 60 ? std::string(PortText("just now")) : PortTextF("{0} ago", {Ago(fAgo)})), UI_FACE_TEXT, Fs(13),
         0.0f, kLbInkDesc);
    Close();
    const std::string k = l["kind"].Str();
    Text(Keep(l["text"].Str()), UI_FACE_TEXT, Fs(14), 0.0f, k == "bad" ? kBad : k == "start" ? kGood : kLbInkInfo);
    Close();
  }
  if (log.size() == 0)
    Fine(PortText("Nothing yet tonight."));
}

bool LobbyOpen(void)
{
  const PortJson &ms = Room()["matches"];
  for (size_t i = 0; i < ms.size(); ++i)
    if (ms.At(i)["state"].Str() == "lobby")
      return true;
  return false;
}

void Floor(float x, float y, float w, float h)
{
  const PortJson &ms = Room()["matches"];
  Decl wrap = At("lnfloor", x, y, w, h);
  wrap.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  wrap.d.layout.childGap = Gap(Fs(10));
  wrap.d.clip.vertical = true;
  Open(wrap);
  const float tw = (w - 20) / 3;
  std::vector<int> order;
  for (int pass = 0; pass < 3; ++pass)
    for (size_t i = 0; i < ms.size(); ++i) {
      const std::string st = ms.At(i)["state"].Str();
      if ((pass == 0 && (st == "live" || st == "starting")) || (pass == 1 && st == "lobby") || (pass == 2 && st == "ended"))
        order.push_back(static_cast<int>(i));
    }
  const int nCells = static_cast<int>(order.size()) + 1;
  for (int r = 0; r * 3 < nCells; ++r) {
    Decl line;
    line.d.layout.childGap = Gap(Fs(10));
    Open(line);
    for (int c = 0; c < 3 && r * 3 + c < nCells; ++c) {
      const int k = r * 3 + c;
      if (k < static_cast<int>(order.size())) {
        Table(ms.At(order[k]), k, tw);
        continue;
      }
      // New Match
      const bool bCan = Running() && !LobbyOpen();
      Decl nm = Box("lnnewmatch");
      nm.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
      nm.d.layout.sizing.width = Fixed(Sc(tw));
      nm.d.layout.sizing.height = Fit();
      nm.d.layout.sizing.height.size.minMax.min = Sc(150);
      nm.d.layout.padding = Pad(Fs(16), Fs(16), Fs(14), Fs(14));
      nm.d.layout.childGap = Gap(Fs(6));
      nm.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      const bool bHot = bCan && Over(nm.d.id);
      nm.d.backgroundColor = Col(bHot ? kLbItem : kNone);
      nm.d.border = Border(kLbInnerRim, Fs(2), Fs(2), Fs(2), Fs(2));
      Radius(nm, 4);
      if (bCan && Clicked(nm.d.id)) {
        NewMatch();
        ToLobby(0);
      }
      Open(nm);
      Text(PortText("+ New Match"), UI_FACE_BOLD, Fs(20), 0.0f, bCan ? kLbInkInfo : kScInkOff);
      Fine(bCan ? PortText("The map, then the seats: Skirmish\xe2\x80\x99s set-up, with the room\xe2\x80\x99s players. Hotkey: N")
           : LobbyOpen() ? PortText("A match is being set up: open its lobby to start it.")
                         : PortText("The server is stopped."),
           kLbInkDesc, 15);
      Close();
    }
    Close();
  }
  // the bench
  Decl bench;
  bench.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  bench.d.layout.sizing.width = Grow();
  bench.d.layout.padding = Pad(Fs(14), Fs(14), Fs(12), Fs(12));
  bench.d.layout.childGap = Gap(Fs(10));
  bench.d.backgroundColor = Col(kLbInner);
  bench.d.border = Border(kLbInnerRim, Fs(2), Fs(2), Fs(2), Fs(2));
  Radius(bench, 4);
  Open(bench);
  Title(PortText("In the Room, Not Playing"));
  std::vector<const PortJson *> off;
  for (const PortJson *d : People()) {
    const std::string st = (*d)["state"].Str();
    if (st != "playing" && st != "lobby" && !(*d)["admin"].b)
      off.push_back(d);
  }
  if (off.empty())
    Fine(PortText("Everyone in the room is in a match."));
  const float cw = (w - 28 - 5 * 8) / 6;
  for (size_t r = 0; r * 6 < off.size(); ++r) {
    Decl line;
    line.d.layout.childGap = Gap(Fs(8));
    Open(line);
    for (size_t c = 0; c < 6 && r * 6 + c < off.size(); ++c)
      DevChip(*off[r * 6 + c], static_cast<int>(r * 6 + c), cw);
    Close();
  }
  Close();
  Decl log;
  log.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  log.d.layout.sizing.width = Grow();
  log.d.layout.padding = Pad(Fs(14), Fs(14), Fs(12), Fs(12));
  log.d.layout.childGap = Gap(Fs(6));
  Open(log);
  Tonight(6);
  Close();
  Close();
}

// ---------------------------------------------------------------------------
// The room, as a rack of rows.

const float kRackCols[] = {250, 260, 120, 300, 160, 270};

void RackCell(float w)
{
  Decl c;
  c.d.layout.sizing.width = w > 0 ? Fixed(Sc(w)) : Grow();
  c.d.layout.childGap = Gap(Fs(6));
  c.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(c);
}

void RackRow(const PortJson &d, int i)
{
  Decl r = Box("lnrack", i);
  r.d.layout.sizing.width = Grow();
  r.d.layout.sizing.height = Fixed(Sc(50));
  r.d.layout.padding = Pad(Fs(12), Fs(12), 0, 0);
  r.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  const bool bHot = Over(r.d.id);
  r.d.backgroundColor = Col(bHot ? kLbItemHi : kLbItem);
  Radius(r, 3);
  if (Clicked(r.d.id))
    OpenDevice(static_cast<int>(d["id"].Num()));
  Open(r);
  RackCell(kRackCols[0]);
  Text(Keep(d["name"].Str()), UI_FACE_BOLD, Fs(16), 0.0f, kLbInkInfo);
  Close();
  RackCell(kRackCols[1]);
  Glyph(d, 20);
  Decl dn;
  dn.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  Open(dn);
  Text(Keep(d["machine"].Str().empty() ? KindName(d) : d["machine"].Str()), UI_FACE_BOLD, Fs(14), 0.0f, kLbInkInfo);
  Text(Keep(d["os"].Str() + " " + d["arch"].Str()), UI_FACE_TEXT, Fs(12), 0.0f, kLbInkDesc);
  Close();
  Close();
  RackCell(kRackCols[2]);
  RouteChip(d, true);
  Close();
  RackCell(kRackCols[3]);
  std::string line, detail;
  Doing(d, &line, &detail);
  Text(Keep(d["state"].Str() == "refused" ? d["why"].Str() : line), UI_FACE_TEXT, Fs(14), 0.0f,
       d["state"].Str() == "refused" ? kBad : kLbInkInfo);
  Close();
  RackCell(kRackCols[4]);
  Rtt(d);
  Spark(RttHistory(static_cast<int>(d["id"].Num())), 60.0f, 80, 22, kStream);
  Close();
  RackCell(0);
  Text(d["route"].Str() == "stream" ? PortText("One stream") : PortText("Next to nothing"), UI_FACE_TEXT, Fs(13), 0.0f,
       kLbInkDesc);
  Close();
  Close();
}

void RackGroup(const std::string &name, const std::string &line, const std::vector<const PortJson *> &devs, int *pnRow)
{
  if (devs.empty())
    return;
  Decl gh;
  gh.d.layout.sizing.width = Grow();
  gh.d.layout.padding = Pad(Fs(4), Fs(4), Fs(10), Fs(4));
  gh.d.layout.childGap = Gap(Fs(12));
  gh.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(gh);
  Text(Keep(name), UI_FACE_BOLD, Fs(17), 0.0f, kScInkHi);
  Text(Keep(line), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
  Close();
  for (const PortJson *d : devs)
    RackRow(*d, (*pnRow)++);
}

void Rack(float x, float y, float w, float h)
{
  Decl wrap = At("lnrackwrap", x, y, w, h);
  wrap.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  wrap.d.layout.childGap = Gap(Fs(4));
  wrap.d.clip.vertical = true;
  InnerBox(wrap);
  wrap.d.layout.padding = Pad(Fs(14), Fs(14), Fs(12), Fs(12));
  Open(wrap);
  Decl hd;
  hd.d.layout.sizing.width = Grow();
  hd.d.layout.padding = Pad(Fs(12), Fs(12), 0, Fs(6));
  hd.d.border = Border(kLbDivider, 0, 0, 0, Fs(2));
  Open(hd);
  const char *const kNames[] = {PortText("Player"),  PortText("Device"), PortText("Plays"),
                                PortText("Doing"),   PortText("Latency"), PortText("Cost to the host")};
  for (int i = 0; i < 6; ++i) {
    RackCell(i == 5 ? 0 : kRackCols[i]);
    Text(kNames[i], UI_FACE_WIDE, Fs(12), 0.04f, kScInkLabel);
    Close();
  }
  Close();
  int nRow = 0;
  const PortJson &ms = Room()["matches"];
  std::vector<bool> placed;
  const std::vector<const PortJson *> people = People();
  placed.assign(people.size(), false);
  for (size_t i = 0; i < ms.size(); ++i) {
    const PortJson &m = ms.At(i);
    std::vector<const PortJson *> devs;
    for (size_t k = 0; k < people.size(); ++k)
      if (static_cast<int>((*people[k])["match"].Num()) == static_cast<int>(m["id"].Num()) && (*people[k])["state"].Str() != "joined") {
        devs.push_back(people[k]);
        placed[k] = true;
      }
    const std::string st = m["state"].Str();
    RackGroup(m["name"].Str(),
              (m["mapName"].Str().empty() ? std::string(PortText("choosing a map")) : m["mapName"].Str()) + kDot +
                  (st == "lobby" ? std::string(PortText("in the lobby")) : st == "live" ? Clock(GameSeconds(m)) : st),
              devs, &nRow);
  }
  std::vector<const PortJson *> room, refused;
  for (size_t k = 0; k < people.size(); ++k)
    if (!placed[k])
      ((*people[k])["state"].Str() == "refused" ? refused : room).push_back(people[k]);
  RackGroup(PortText("In the room"), PortText("not in a match"), room, &nRow);
  RackGroup(PortText("Couldn\xe2\x80\x99t join"), PortText("each with why, on its card"), refused, &nRow);
  if (nRow == 0)
    Fine(PortText("Nobody has joined yet. The join card on the right says how."), kLbInkDesc, 16);
  Close();
}

// ---------------------------------------------------------------------------
// The room, as a wall of pictures.

// The Wall's pictures: the room's copy of each live match sends a picture of each player's army
// in turn (port_lanfeed.cpp), and they are kept on sheets of six in the picture atlas's slots 28
// to 31, each tile drawing its seat's.
const int kWallSlot = 28, kWallSheets = 4, kWallAcross = 2, kWallDown = 3;
const int kWallW = LanProto::kPictureW, kWallH = LanProto::kPictureH;
struct WallSheet {
  std::vector<unsigned char> rgba = std::vector<unsigned char>(kWallAcross * kWallW * kWallDown * kWallH * 4, 0);
  bool bDirty = false;
} g_sheets[kWallSheets];
std::map<std::pair<int, int>, std::pair<int, uint32_t>> g_wallCells;  // a match's player: its cell, its picture's count
int g_nWallNext;
struct WallPic {
  int nSlot;
  float u0, v0, u1, v1;
};
std::deque<WallPic> g_wallPics;  // the frame's, alive until its layout is drawn
std::deque<UiCustom> g_wallCustoms;

void DrawWallPic(float x, float y, float w, float h, void *pCtx)
{
  const WallPic &k = *static_cast<const WallPic *>(pCtx);
  UiFill(x, y, w, h, UiRgb(0x000000));
  UiPicturePart(x, y, w, h, k.nSlot, k.u0, k.v0, k.u1, k.v1, UiRgb(0xffffff));
}

// A seat's picture: a person's by their name; a computer's, the nComputer'th of those named for
// nobody seated in the match.
const LanPicture *PictureFor(const PortJson &m, const PortJson *d, int nComputer)
{
  const int nId = static_cast<int>(m["id"].Num());
  std::vector<const LanPicture *> others;
  for (const LanPicture &p : LanClientPictures()) {
    if (p.nMatch != nId)
      continue;
    if (d != NULL && p.name == (*d)["name"].Str())
      return &p;
    bool bPerson = false;
    const PortJson &seats = m["seats"];
    for (size_t k = 0; k < seats.size(); ++k)
      bPerson = bPerson || (seats.At(k)["owner"].Str() == "player" && seats.At(k)["name"].Str() == p.name);
    if (!bPerson)
      others.push_back(&p);
  }
  if (d != NULL || nComputer >= static_cast<int>(others.size()))
    return NULL;
  std::sort(others.begin(), others.end(), [](const LanPicture *a, const LanPicture *b) { return a->nPlayer < b->nPlayer; });
  return others[nComputer];
}

// Where a picture is on the sheets, copied there when it is new.
WallPic WallPicOf(const LanPicture &p)
{
  auto key = std::make_pair(p.nMatch, p.nPlayer);
  auto it = g_wallCells.find(key);
  if (it == g_wallCells.end()) {
    // a cell of its own, the oldest's once all are taken
    const int nCell = g_nWallNext++ % (kWallSheets * kWallAcross * kWallDown);
    for (auto jt = g_wallCells.begin(); jt != g_wallCells.end(); ++jt)
      if (jt->second.first == nCell) {
        g_wallCells.erase(jt);
        break;
      }
    it = g_wallCells.insert(std::make_pair(key, std::make_pair(nCell, 0u))).first;
  }
  const int nCell = it->second.first;
  const int nSheet = nCell / (kWallAcross * kWallDown), nIn = nCell % (kWallAcross * kWallDown);
  const int x0 = (nIn % kWallAcross) * kWallW, y0 = (nIn / kWallAcross) * kWallH;
  if (it->second.second != p.nSeq && p.w == kWallW && p.h == kWallH) {
    it->second.second = p.nSeq;
    WallSheet &s = g_sheets[nSheet];
    const int nPitch = kWallAcross * kWallW * 4;
    for (int y = 0; y < kWallH; ++y)
      for (int x = 0; x < kWallW; ++x) {
        unsigned char *pOut = &s.rgba[(y0 + y) * nPitch + (x0 + x) * 4];
        const unsigned char *pIn = &p.rgb[(y * kWallW + x) * 3];
        pOut[0] = pIn[0];
        pOut[1] = pIn[1];
        pOut[2] = pIn[2];
        pOut[3] = 255;
      }
    s.bDirty = true;
  }
  return WallPic{kWallSlot + nSheet, static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x0 + kWallW),
                 static_cast<float>(y0 + kWallH)};
}

void WallSheetsSet(void)
{
  for (int i = 0; i < kWallSheets; ++i)
    if (g_sheets[i].bDirty) {
      g_sheets[i].bDirty = false;
      UiPictureSet(kWallSlot + i, g_sheets[i].rgba.data(), kWallAcross * kWallW, kWallDown * kWallH);
    }
}

void Wall(float x, float y, float w, float h)
{
  g_wallPics.clear();
  g_wallCustoms.clear();
  Decl wrap = At("lnwall", x, y, w, h);
  wrap.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  wrap.d.layout.childGap = Gap(Fs(12));
  wrap.d.clip.vertical = true;
  Open(wrap);
  const PortJson &ms = Room()["matches"];
  int nTile = 0, nLive = 0;
  for (size_t i = 0; i < ms.size(); ++i) {
    const PortJson &m = ms.At(i);
    if (m["state"].Str() != "live")
      continue;
    ++nLive;
    Decl g;
    g.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    g.d.layout.sizing.width = Grow();
    g.d.layout.padding = Pad(Fs(14), Fs(14), Fs(12), Fs(12));
    g.d.layout.childGap = Gap(Fs(10));
    g.d.backgroundColor = Col(kLbInner);
    g.d.border = Border(kLbInnerRim, Fs(2), Fs(2), Fs(2), Fs(2));
    Radius(g, 4);
    Open(g);
    Decl h2;
    h2.d.layout.sizing.width = Grow();
    h2.d.layout.childGap = Gap(Fs(12));
    h2.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(h2);
    Text(Keep(m["name"].Str()), UI_FACE_BOLD, Fs(20), 0.0f, kScInkHi);
    Text(Keep(m["mapName"].Str() + kDot + Clock(GameSeconds(m))), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
    Spacer();
    MatchButtons(m, 100 + static_cast<int>(i));
    Close();
    Decl row;
    row.d.layout.childGap = Gap(Fs(10));
    Open(row);
    std::vector<int> slots;
    SeatsOf(m, &slots);
    int nComputer = 0;
    for (int s : slots) {
      const PortJson &seat = m["seats"].At(s);
      const PortJson *d = seat["owner"].Str() == "player" ? DeviceOf(static_cast<int>(seat["dev"].Num())) : NULL;
      const LanPicture *pPic = PictureFor(m, d, d == NULL ? nComputer++ : 0);
      Decl t = Box("lnwtile", nTile++);
      t.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
      t.d.layout.sizing.width = Fixed(Sc(300));
      t.d.layout.childGap = Gap(Fs(6));
      t.d.layout.padding = Pad(Fs(8), Fs(8), Fs(8), Fs(8));
      t.d.backgroundColor = Col(kLbItem);
      t.d.border = Border(kLbItemRim, Fs(2), Fs(2), Fs(2), Fs(2));
      Radius(t, 4);
      if (d != NULL && Clicked(t.d.id))
        OpenDevice(static_cast<int>((*d)["id"].Num()));
      Open(t);
      Decl pic;
      pic.d.layout.sizing.width = Grow();
      pic.d.layout.sizing.height = Fixed(Sc(160));
      pic.d.layout.padding = Pad(Fs(10), Fs(10), Fs(10), Fs(10));
      pic.d.layout.childAlignment.y = CLAY_ALIGN_Y_BOTTOM;
      pic.d.backgroundColor = Col(KitRgb(0x02070e));
      Radius(pic, 3);
      if (pPic != NULL) {
        g_wallPics.push_back(WallPicOf(*pPic));
        g_wallCustoms.push_back(UiCustom{0.0f, DrawWallPic, &g_wallPics.back()});
        pic.d.custom.customData = &g_wallCustoms.back();
      }
      Open(pic);
      if (pPic == NULL)
        Fine(m["copy"]["in"].b ? PortText("The host\xe2\x80\x99s copy of the match finds their army.")
                               : PortText("The host\xe2\x80\x99s copy of the match is starting."),
             kScInkOff, 13);
      Close();
      Decl foot;
      foot.d.layout.sizing.width = Grow();
      foot.d.layout.childGap = Gap(Fs(6));
      foot.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      Open(foot);
      Text(Keep(d != NULL ? (*d)["name"].Str() : std::string(PortText("Computer"))), UI_FACE_BOLD, Fs(16), 0.0f,
           kLbInkInfo);
      Spacer();
      if (d != NULL) {
        Glyph(*d, 18);
        RouteChip(*d, true);
        Rtt(*d);
      }
      Close();
      Close();
    }
    Close();
    Close();
  }
  if (nLive == 0) {
    Decl e;
    e.d.layout.sizing.width = Grow();
    e.d.layout.padding = Pad(Fs(14), Fs(14), Fs(14), Fs(14));
    Open(e);
    Fine(PortText("No match is being played. The wall shows each live match\xe2\x80\x99s seats."), kLbInkDesc, 16);
    Close();
  }
  // the rest of the room
  Decl rest;
  rest.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  rest.d.layout.sizing.width = Grow();
  rest.d.layout.padding = Pad(Fs(14), Fs(14), Fs(4), 0);
  rest.d.layout.childGap = Gap(Fs(8));
  Open(rest);
  Title(PortText("The Rest of the Room"));
  std::vector<const PortJson *> off;
  for (const PortJson *d : People())
    if ((*d)["state"].Str() != "playing" && !(*d)["admin"].b)
      off.push_back(d);
  const float cw = (w - 28 - 5 * 8) / 6;
  if (off.empty())
    Fine(PortText("Everyone in the room is in a match."));
  for (size_t r = 0; r * 6 < off.size(); ++r) {
    Decl line;
    line.d.layout.childGap = Gap(Fs(8));
    Open(line);
    for (size_t c = 0; c < 6 && r * 6 + c < off.size(); ++c)
      DevChip(*off[r * 6 + c], 200 + static_cast<int>(r * 6 + c), cw);
    Close();
  }
  Close();
  Close();
  WallSheetsSet();
}

void Stopped(float x, float y, float w, float h)
{
  BoxAt("lnstopped", x, y, w, h, PortText("The Server Is Stopped"));
  Fine(PortText("Nobody can join, and the host\xe2\x80\x99s page does not answer. Starting it again listens at the same address with "
                "the same password, so the code on the right stays good."),
       kLbInkDesc, 16);
  Decl go = Box("lnstartbig");
  go.d.layout.sizing.width = Fixed(Sc(260));
  go.d.layout.sizing.height = Fixed(Sc(50));
  if (ScButton(go, PortText("Start Server"), 'S', true))
    ServerStart();
  Fine(PortText("Each player\xe2\x80\x99s copy keeps the replays of the matches it played, up to where they stopped."));
  Fine(PortText("Installed copies come back to this room from Multiplayer, Join a Room."));
  Close();
}

// ---------------------------------------------------------------------------
// Settings.

void SetRow(const char *pszName, const char *pszNote)
{
  Decl l;
  l.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  l.d.layout.sizing.width = Grow();
  Open(l);
  Text(pszName, UI_FACE_BOLD, Fs(16), 0.0f, kScInkLabel);
  if (pszNote != NULL)
    Text(pszNote, UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
  Close();
}

Decl RowDecl(void)
{
  Decl r;
  r.d.layout.sizing.width = Grow();
  r.d.layout.sizing.height = Fixed(Sc(58));
  r.d.layout.childGap = Gap(Fs(12));
  r.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  return r;
}

std::string g_nameEdit;
bool g_bNameFocus;

void SettingsTab(float x, float y, float w, float h)
{
  const float cw = (w - 10) / 2, half = (h - 10) / 2;
  const PortJson &room = Room();
  BoxAt("lnsetroom", x, y, cw, half, PortText("The Room"));
  Open(RowDecl());
  SetRow(PortText("Name"), PortText("What Join a Room shows"));
  Decl e = Box("lnsetname");
  e.d.layout.sizing.width = Fixed(Sc(300));
  e.d.layout.sizing.height = Fixed(Sc(38));
  if (Clicked(e.d.id)) {
    g_bNameFocus = true;
    g_nameEdit = g_set.name;
  }
  if (g_bNameFocus) {
    ScEdit(e, &g_nameEdit, 30, PortText("the room's name"), true);
  } else {
    std::string shown = g_set.name;
    ScEdit(e, &shown, 30, PortText("the room's name"), false);
  }
  Close();
  Open(RowDecl());
  SetRow(PortText("Password"),
         g_set.bPassword ? PortText("Asked in Join a Room") : PortText("Anyone on this network can join"));
  const char *const kOnOff[] = {PortText("On"), PortText("Off")};
  const int nPw = Segments("lnsetpw", kOnOff, 2, g_set.bPassword ? 0 : 1);
  if (nPw >= 0 && (nPw == 0) != g_set.bPassword) {
    g_set.bPassword = nPw == 0;
    SettingsSend();
  }
  Close();
  Open(RowDecl());
  // {0} the port games are joined on, {1} the port the room is found on
  SetRow(PortText("Listens on"),
         Keep(room["address"].Str() + ": " +
              PortTextF("games on {0}, found on {1}", {std::to_string(static_cast<int>(room["port"].Num())),
                                                       std::to_string(static_cast<int>(room["port"].Num()) - 1)})));
  const char *const kWhere[] = {PortText("This network"), PortText("The internet")};
  Segments("lnsetwhere", kWhere, 2, 0, 1u << 1,
           PortText("Not available yet. The room is for one network; online play comes later."));
  Close();
  Open(RowDecl());
  SetRow(PortText("Checks every seat"),
         PortText("Each seat\xe2\x80\x99s game state is compared, so one that drifts is named"));
  PulldownLater("lnsetcheck", 0, 220, PortText("Every 5 seconds"), PortText("Checks every seat"),
                PortText("Every 120 frames, about 5 seconds at Fastest. Choosing how often is not available yet."));
  Close();
  Open(RowDecl());
  SetRow(PortText("A player drops"), PortText("Wait 2 minutes, then it counts as lost"));
  PulldownLater("lnsetdrop", 0, 220, PortText("Wait 2 minutes"), PortText("A player drops"),
                PortText("Waiting for a dropped player, or pausing for everyone, is not available yet: a dropped player\xe2\x80\x99s "
                         "seat leaves the game."));
  Close();
  Close();

  BoxAt("lnsetbrowser", x + cw + 10, y, cw, half, PortText("Players in a Browser"));
  // the one a browser's game in its tab reads; the stream's below are not available yet
  const std::vector<std::string> kFiles = {PortText("Offer them"), PortText("Never")};
  Open(RowDecl());
  SetRow(PortText("StarCraft\xe2\x80\x99s files"),
         PortText("This machine\xe2\x80\x99s copy, to a browser that plays in its tab; a player may use their own"));
  const int nFiles = Pulldown("lnsetbf", 0, 220, kFiles, g_set.browserFiles == "Never" ? 1 : 0);
  if (nFiles >= 0 && (nFiles == 1) != (g_set.browserFiles == "Never")) {
    g_set.browserFiles = nFiles == 1 ? "Never" : "Offer";
    SettingsSend();
  }
  Close();
  const char *const kLater[][3] = {
      {PortText("Picture"), PortText("As the game draws it"),
       PortText("As drawn: 1136 \xc3\x97 480, or 854 \xc3\x97 480 on a phone")},
      {PortText("Quality"), PortText("High, 12 Mbit/s"),
       PortText("High keeps the pixel art and small text sharp on a LAN")},
      {PortText("Frame rate"), "60", PortText("Of the video; the game itself runs at its own speed")},
      {PortText("Encoding"), PortText("The card, then the CPU"),
       PortText("The graphics card first, the processor after it")},
      {PortText("Sound"), PortText("On"), PortText("Sent with the picture")}};
  int n = 0;
  for (const auto &l : kLater) {
    Open(RowDecl());
    SetRow(l[0], l[2]);
    PulldownLater("lnsetb", n++, 220, l[1], l[0], PortText("Players in a browser are not available yet."));
    Close();
  }
  Close();

  BoxAt("lnsetinstall", x, y + half + 10, cw, h - half - 10, PortText("Installing from This Host"));
  const char *const kInstall[][3] = {
      {PortText("Offer the game for"), PortText("Windows and Linux"),
       PortText("Windows x64, Linux x64 and Linux ARM64: the host\xe2\x80\x99s own version")},
      {PortText("Game data to a PC"), PortText("When the player chooses it"),
       PortText("About 1.2 GB, to this network\xe2\x80\x99s devices only")},
      {PortText("Updates"), PortText("Offer the host\xe2\x80\x99s version"),
       PortText("A PC with an older copy is offered this one")}};
  for (const auto &l : kInstall) {
    Open(RowDecl());
    SetRow(l[0], l[2]);
    PulldownLater("lnseti", n++, 300, l[1], l[0], PortText("Installing from this host is not available yet."));
    Close();
  }
  Close();

  BoxAt("lnsetwatch", x + cw + 10, y + half + 10, cw, h - half - 10, PortText("Watching"));
  Open(RowDecl());
  SetRow(PortText("Delay"),
         PortText("So nobody watching can tell a player what is coming; a tournament may ask for more"));
  int nCur = 1;
  for (size_t k = 0; k < kDelays.size(); ++k)
    if (kDelays[k] == g_set.delay)
      nCur = static_cast<int>(k);
  std::vector<std::string> delays;
  for (const std::string &s : kDelays)
    delays.push_back(PortText(s.c_str()));
  int nPick = Pulldown("lnsetw", 0, 220, delays, nCur);
  if (nPick >= 0 && kDelays[nPick] != g_set.delay) {
    g_set.delay = kDelays[nPick];
    SettingsSend();
  }
  Close();
  Open(RowDecl());
  SetRow(PortText("Who may watch"), PortText("The room\xe2\x80\x99s people who are not seated"));
  nCur = g_set.watch == "Nobody" ? 1 : 0;
  std::vector<std::string> watchers;
  for (const std::string &s : kWatchers)
    watchers.push_back(PortText(s.c_str()));
  nPick = Pulldown("lnsetw", 1, 220, watchers, nCur);
  if (nPick >= 0 && kWatchers[nPick] != g_set.watch) {
    g_set.watch = kWatchers[nPick];
    SettingsSend();
  }
  Close();
  Open(RowDecl());
  SetRow(PortText("The big screen"), PortText("A feed that switches between the live matches"));
  PulldownLater("lnsetw", 2, 220, PortText("On"), PortText("The big screen"),
                PortText("The big screen is not available yet."));
  Close();
  Close();
}

bool SettingsKeys(void)
{
  if (!g_bNameFocus)
    return false;
  if (Key(VK_RETURN)) {
    g_bNameFocus = false;
    if (!g_nameEdit.empty() && g_nameEdit != g_set.name) {
      g_set.name = g_nameEdit;
      SettingsSend();
    }
  } else if (Key(VK_ESCAPE)) {
    g_bNameFocus = false;
  }
  return true;
}

void AskAnswered(void)
{
  std::string id;
  const int n = AskShown(&id);
  if (n < 0)
    return;
  if (id == "stop" || id == "stopidle") {
    if (n == 0)
      ServerStop(false);
    else if (n == 1 && id == "stop")
      ServerStop(true);
  } else if (id == "password" && n == 0) {
    LanClientSay("{\"t\":\"password\"}");
    g_bShowPassword = true;
  } else if (id.compare(0, 4, "end:") == 0 && n == 0) {
    LanClientSay("{\"t\":\"end\",\"match\":" + id.substr(4) + "}");
  }
}

}  // namespace

namespace FrontKit {
namespace Lan {

void ConsoleOpen(void)
{
  g_bShowPassword = false;
  g_bNameFocus = false;
  g_set.bRead = false;
}

void ConsoleScreen(float fW, float fH)
{
  SettingsRead();
  Header(fW);
  const float right = 34, info = 425, gap = 10, top = 114;
  const float aw = fW - right - info - gap - 13, ah = fH - top - 20;
  if (g_nTab == TAB_DEVICES) {
    const float pw = 560;
    DevicesTab(13, top, fW - right - pw - gap - 13, ah, fW - right - pw, pw);
  } else {
    if (g_nTab == TAB_SETTINGS) {
      SettingsTab(13, top, aw, ah);
    } else if (!Running()) {
      Stopped(13, top, aw, ah);
    } else {
      // the room's direction, the floor first
      Decl seg = Floating("lnhdir", 0, -Sc(right + info + gap), Sc(top - 44), 22, CLAY_ATTACH_POINT_RIGHT_TOP,
                          CLAY_ATTACH_POINT_RIGHT_TOP);
      Open(seg);
      const char *const kDirs[] = {PortText("Floor"), PortText("Rack"), PortText("Wall")};
      const int nDir = Segments("lnhdirs", kDirs, 3, g_set.nDir, 0, NULL, 32);
      if (nDir >= 0 && nDir != g_set.nDir) {
        g_set.nDir = nDir;
        SettingsSend();
      }
      Close();
      if (g_set.nDir == 1)
        Rack(13, top, aw, ah);
      else if (g_set.nDir == 2)
        Wall(13, top, aw, ah);
      else
        Floor(13, top, aw, ah);
    }
    JoinCard(fW - right - info, top, info);
    LoadBox(fW - right - info, top + kJoinH + gap, info, ah - kJoinH - gap);
  }
  if (g_nDeviceOpen != 0) {
    if (const PortJson *d = DeviceOf(g_nDeviceOpen))
      DeviceCard(*d, false);
    else
      g_nDeviceOpen = 0;
  }
  AskAnswered();
}

bool ConsoleKeys(void)
{
  if (g_nDeviceOpen != 0) {
    if (const PortJson *d = DeviceOf(g_nDeviceOpen))
      return DeviceCardKeys(*d);
    g_nDeviceOpen = 0;
  }
  if (g_nTab == TAB_SETTINGS && SettingsKeys())
    return true;
  if (g_nTab == TAB_DEVICES && DevicesKeys())
    return true;
  for (int i = 0; i < 4; ++i)
    if (i != TAB_CUP && Key('1' + i)) {
      g_nTab = i;
      return true;
    }
  if (Key('S')) {
    if (ServerRunning())
      AskStop();
    else
      ServerStart();
  } else if (Key('N') && Running() && !LobbyOpen()) {
    NewMatch();
    ToLobby(0);
  } else if (Key('O') && LobbyOpen()) {
    const PortJson &ms = Room()["matches"];
    for (size_t i = 0; i < ms.size(); ++i)
      if (ms.At(i)["state"].Str() == "lobby")
        ToLobby(static_cast<int>(ms.At(i)["id"].Num()));
  } else if (Key(VK_ESCAPE) || Key('B')) {
    ToMultiplayer();
  }
  return true;
}

}  // namespace Lan
}  // namespace FrontKit
