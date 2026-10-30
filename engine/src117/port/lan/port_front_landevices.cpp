// port_front_landevices.cpp -- the console's Devices tab: everyone in the room in one list,
// each row saying in words what that device is doing, filtered by what that is and how it plays,
// in an order, with a search; the selected device's card down the right with its part of
// tonight's log. The card also opens over the Room tab from a seat or a chip. Presentation
// only: Remove from Room asks the room.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "../../../hyperion/version.h"  // HYPERION_VERSION_TEXT
#include "../platform/port_util.h"  // PortLower
#include "port_front_lan.h"
#include "../front/port_frontscreens.h"
#include "port_lanclient.h"
#include "../front/port_ui.h"

using namespace FrontKit;
using namespace FrontKit::Lan;

namespace {

const UiColor kInkPlayer = KitRgb(0x64d0f9);  // lobby.css --lb-ink-player

// the states, in the list's order, and their names
const char *const kStates[][2] = {{"playing", PT_("Playing")},       {"lobby", PT_("In a lobby")},
                                  {"installing", PT_("Getting the game")}, {"watching", PT_("Watching")},
                                  {"joined", PT_("In the room")},     {"refused", PT_("Couldn\xe2\x80\x99t join")}};
const int kStateCount = 6;
const char *const kSorts[] = {PT_("By what they\xe2\x80\x99re doing"), PT_("By name"), PT_("By latency"),
                              PT_("By time in the room")};

int g_nFilter = -1;  // a state, -1 all
int g_nRoute;        // 0 both, 1 streamed, 2 installed
int g_nSort;
std::string g_search;
bool g_bSearchFocus;
int g_nSel;
std::vector<int> g_shown;
int g_nFirst;

int StateIndex(const std::string &s)
{
  for (int i = 0; i < kStateCount; ++i)
    if (s == kStates[i][0])
      return i;
  return kStateCount;
}

UiColor StateColour(const std::string &s)
{
  return s == "playing" ? kOn : s == "lobby" ? KitRgb(0xffd36b) : s == "installing" || s == "watching" ? kStream
         : s == "refused" ? kBad : kOff;
}

std::vector<const PortJson *> Listed(void)
{
  std::vector<const PortJson *> out;
  const std::string q = PortLower(g_search);
  for (const PortJson *d : People()) {
    const std::string st = (*d)["state"].Str();
    if (g_nFilter >= 0 && st != kStates[g_nFilter][0])
      continue;
    if (g_nRoute == 1 && (*d)["route"].Str() == "install")
      continue;
    if (g_nRoute == 2 && (*d)["route"].Str() != "install")
      continue;
    if (!q.empty() && PortLower((*d)["name"].Str() + " " + (*d)["machine"].Str() + " " + (*d)["os"].Str() + " " + KindName(*d)).find(q) ==
                          std::string::npos)
      continue;
    out.push_back(d);
  }
  std::stable_sort(out.begin(), out.end(), [](const PortJson *a, const PortJson *b) {
    switch (g_nSort) {
    case 1: return PortLower((*a)["name"].Str()) < PortLower((*b)["name"].Str());
    case 2: return (*a)["rtt"].Num() > (*b)["rtt"].Num();
    case 3: return (*a)["in"].Num() > (*b)["in"].Num();
    default: {
      const int x = StateIndex((*a)["state"].Str()), y = StateIndex((*b)["state"].Str());
      if (x != y)
        return x < y;
      if ((*a)["match"].Num() != (*b)["match"].Num())
        return (*a)["match"].Num() < (*b)["match"].Num();
      return PortLower((*a)["name"].Str()) < PortLower((*b)["name"].Str());
    }
    }
  });
  return out;
}

void Light(const std::string &state)
{
  Decl l;
  l.d.layout.sizing.width = Fixed(Sc(9));
  l.d.layout.sizing.height = Fixed(Sc(9));
  l.d.backgroundColor = Col(StateColour(state));
  Radius(l, 4.5f);
  Open(l);
  Close();
}

const float kCols[] = {330, 120, 0, 200, 230, 120};

void Cell(float w)
{
  Decl c;
  c.d.layout.sizing.width = w > 0 ? Fixed(Sc(w)) : Grow();
  c.d.layout.childGap = Gap(Fs(8));
  c.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(c);
}

void Row(const PortJson &d, int i)
{
  const int nId = static_cast<int>(d["id"].Num());
  Decl r = Box("lndrow", i);
  r.d.layout.sizing.width = Grow();
  r.d.layout.sizing.height = Fixed(Sc(56));
  r.d.layout.padding = Pad(Fs(12), Fs(12), 0, 0);
  r.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  const bool bOn = nId == g_nSel;
  const bool bHot = Over(r.d.id);
  r.d.backgroundColor = Col(bOn || bHot ? kLbItemHi : kLbItem);
  r.d.border = Border(bOn ? kLbHyperionRim : kNone, Fs(2), Fs(2), Fs(2), Fs(2));
  Radius(r, 3);
  if (Clicked(r.d.id)) {
    static Uint64 s_qwLast;
    static int s_nLast;
    const Uint64 qwNow = SDL_GetTicks();
    if (s_nLast == nId && qwNow - s_qwLast < 450)
      OpenDevice(nId);
    s_qwLast = qwNow;
    s_nLast = nId;
    g_nSel = nId;
  }
  Open(r);
  Cell(kCols[0]);
  Light(d["state"].Str());
  Glyph(d, 22);
  Decl nm;
  nm.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  Open(nm);
  // {0} the player's name
  Text(Keep(d["admin"].b ? PortTextF("{0} (you, the host)", {d["name"].Str()}) : d["name"].Str()), UI_FACE_BOLD, Fs(16),
       0.0f, kLbInkInfo);
  Text(Keep((d["machine"].Str().empty() ? std::string() : d["machine"].Str() + kDot) + KindName(d)), UI_FACE_TEXT, Fs(12),
       0.0f, kLbInkDesc);
  Close();
  Close();
  Cell(kCols[1]);
  RouteChip(d, true);
  Close();
  Cell(kCols[2]);
  std::string line, detail;
  Doing(d, &line, &detail);
  Decl dl;
  dl.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  dl.d.layout.sizing.width = Grow();
  Open(dl);
  Text(Keep(line), UI_FACE_BOLD, Fs(15), 0.0f, d["state"].Str() == "refused" ? kBad : kLbInkInfo);
  if (!detail.empty())
    Text(Keep(detail), UI_FACE_TEXT, Fs(12), 0.0f, kLbInkDesc);
  Close();
  Close();
  Cell(kCols[3]);
  Rtt(d);
  Spark(RttHistory(nId), 60.0f, 96, 22, kStream);
  Close();
  Cell(kCols[4]);
  Text(d["route"].Str() == "stream" ? PortText("One stream") : PortText("Next to nothing"), UI_FACE_TEXT, Fs(13), 0.0f,
       kLbInkDesc);
  Close();
  Cell(kCols[5]);
  Text(Keep(Ago(d["in"].Num())), UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
  Close();
  Close();
}

void Fact(const char *pszName, const std::string &value, UiColor c = kLbInkInfo)
{
  Decl r;
  r.d.layout.sizing.width = Grow();
  r.d.layout.childGap = Gap(Fs(14));
  Open(r);
  Decl k;
  k.d.layout.sizing.width = Fixed(Sc(150));
  Open(k);
  Text(pszName, UI_FACE_BOLD, Fs(14), 0.0f, kScInkLabel);
  Close();
  Decl v;
  v.d.layout.sizing.width = Grow();
  Open(v);
  Text(Keep(value), UI_FACE_TEXT, Fs(14), 0.0f, c, -1, 1.3f);
  Close();
  Close();
}

}  // namespace

namespace FrontKit {
namespace Lan {

int g_nDeviceOpen;

void OpenDevice(int nId)
{
  g_nDeviceOpen = nId;
  g_lbOpen.clear();
}

// A device, close to (lan.js devBody): who and what it is, what it is doing, its latency, how
// it plays, why it was refused, and what the host can do about it.
void DeviceCard(const PortJson &d, bool bPane)
{
  const int nId = static_cast<int>(d["id"].Num());
  bool bOut = false;
  if (!bPane)
    bOut = DialogOpen("lndev", 940, 640, 170, 70);
  Decl h;
  h.d.layout.sizing.width = Grow();
  h.d.layout.childGap = Gap(Fs(12));
  h.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(h);
  Glyph(d, 30);
  Decl nm;
  nm.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  nm.d.layout.sizing.width = Grow();
  Open(nm);
  Text(Keep(d["name"].Str()), UI_FACE_BOLD, Fs(24), 0.0f, kScInkHi);
  Text(Keep((d["machine"].Str().empty() ? KindName(d) : d["machine"].Str()) + kDot + d["os"].Str() + " " + d["arch"].Str() +
            (d["browser"].Str().empty() ? std::string() : kDot + d["browser"].Str())),
       UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
  Close();
  RouteChip(d, false);
  Close();
  Fine(d["route"].Str() == "stream"
           ? PortText("Plays in a browser. The host runs this player\xe2\x80\x99s game and sends it as video; the browser sends back the "
                      "mouse and keys.")
           : PortText("Hyperion is installed on this PC. It plays the game itself and sends its commands to the host."));
  std::string line, detail;
  Doing(d, &line, &detail);
  const std::string st = d["state"].Str();
  Fact(PortText("Doing"),
       st == "refused" ? line : line + (detail.empty() ? std::string() : ". " + detail));
  // {0} how long they have been in the room
  Fact(PortText("In the room"),
       PortTextF("For {0}", {Ago(d["in"].Num())}) + kDot + d["ip"].Str() +
           (d["route"].Str() == "stream" ? kDot + std::string(PortText("came in on the host\xe2\x80\x99s page"))
                                         : kDot + std::string(PortText("came in from Join a Room"))));
  if (const PortJson *m = MatchOf(static_cast<int>(d["match"].Num()))) {
    const PortJson &seats = (*m)["seats"];
    for (size_t i = 0; i < seats.size(); ++i)
      if (seats.At(i)["owner"].Str() == "player" && static_cast<int>(seats.At(i)["dev"].Num()) == nId) {
        std::string race = seats.At(i)["race"].Str("random");
        race[0] = static_cast<char>(toupper(static_cast<unsigned char>(race[0])));
        // {0} the seat's race, {1} its number in the lobby, {2} the match's name
        Fact(PortText("Seat"), PortTextF("{0}, player {1} of {2}", {race, std::to_string(i + 1), (*m)["name"].Str()}));
      }
  }
  const std::vector<float> &h30 = RttHistory(nId);
  float fWorst = 0, fSum = 0;
  for (float f : h30) {
    fWorst = f > fWorst ? f : fWorst;
    fSum += f;
  }
  char szNow[16], szMean[16], szWorst[16];
  SDL_snprintf(szNow, sizeof szNow, "%.0f", d["rtt"].Num());
  SDL_snprintf(szMean, sizeof szMean, "%.0f", h30.empty() ? d["rtt"].Num() : fSum / h30.size());
  SDL_snprintf(szWorst, sizeof szWorst, "%.0f", h30.empty() ? d["rtt"].Num() : fWorst);
  Fact(PortText("Latency"),
       st == "refused" ? std::string("\xe2\x80\x94")
                       : PortTextF("{0} ms now \xc2\xb7 {1} ms on average \xc2\xb7 {2} ms at worst, the last half minute",
                                   {szNow, szMean, szWorst}));
  if (d["route"].Str() != "stream") {
    const bool bSame = d["version"].Str() == HYPERION_VERSION_TEXT;
    // {0} the copy's version
    Fact(PortText("Version"), bSame ? PortTextF("{0}, the host\xe2\x80\x99s own", {VersionName(d["version"].Str())})
                                    : PortTextF("{0}, which computes the game differently",
                                                {VersionName(d["version"].Str())}));
    if (!d["mod"].Str().empty())
      Fact(PortText("Mod"), PortTextF("{0} active", {d["mod"].Str()}));
    if (st == "playing")
      Fact(PortText("On their PC"),
           PortText("At its own resolution. It costs the host next to nothing: the host passes its commands on."));
  } else {
    Fact(PortText("On the host"),
         PortText("Nothing yet: the page is the host\xe2\x80\x99s own, and no game is sent until a match starts."));
  }
  if (st == "refused") {
    Decl why;
    why.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    why.d.layout.sizing.width = Grow();
    why.d.layout.padding = Pad(Fs(14), Fs(14), Fs(10), Fs(10));
    why.d.layout.childGap = Gap(Fs(4));
    why.d.border = Border(kBad, Fs(2), Fs(2), Fs(2), Fs(2));
    Radius(why, 4);
    Open(why);
    Text(PortText("Couldn\xe2\x80\x99t join"), UI_FACE_BOLD, Fs(16), 0.0f, kBad);
    Fine(Keep(d["why"].Str()), kLbInkInfo);
    Fine(Keep(d["fix"].Str()), kInkPlayer);
    Close();
  }
  // what the host can do
  Decl row;
  row.d.layout.childGap = Gap(Fs(8));
  row.d.layout.padding = Pad(0, 0, Fs(6), 0);
  Open(row);
  const bool bHere = !d["admin"].b;
  struct Btn {
    const char *pszLabel;
    char cKey;
    const char *pszOff;
  };
  const std::vector<Btn> btns = {
      {st == "watching" ? PortText("Watch the Match") : PortText("Watch Their Game"), 'W', NULL},
      {PortText("Send the Update"), 'U', PortText("Updating a copy from this host is not available yet.")},
      {PortText("Move to Watching"), 'M',
       PortText("Not available yet: a seat handed to a computer mid-game waits on the mid-game saves.")},
      {PortText("Message"), 'E', PortText("Not available yet.")},
      {PortText("Remove from Room"), 'R', bHere ? NULL : PortText("The host stays in its own room.")}};
  int k = 0;
  for (const Btn &b : btns) {
    if (b.cKey == 'U' && st != "refused")
      continue;
    if (b.cKey == 'W' && st != "playing" && st != "watching")
      continue;
    if (b.cKey == 'M' && st != "playing")
      continue;
    Decl bd = Box(bPane ? "lnpanebtn" : "lncardbtn", k);
    bd.d.layout.sizing.height = Fixed(Sc(36));
    const PortJson *pm = b.cKey == 'W' ? MatchOf(static_cast<int>(d["match"].Num())) : NULL;
    const char *pszOff =
        b.cKey == 'W' ? (pm != NULL ? WatchOff(*pm) : PortText("That match is not being played now.")) : b.pszOff;
    const bool bClicked = ScMini(bd, b.pszLabel, b.cKey, pszOff == NULL);
    if (pszOff != NULL && b.cKey == 'W')
      TipOn(Id(bPane ? "lnpanebtn" : "lncardbtn", k), b.pszLabel, pszOff);
    if (bClicked && b.cKey == 'W' && pm != NULL)
      WatchMatch(static_cast<int>((*pm)["id"].Num()));
    if (bClicked && b.cKey == 'R') {
      LanClientSay("{\"t\":\"remove\",\"dev\":" + std::to_string(nId) + "}");
      // {0} the player who was removed
      Toast(PortTextF("{0} left the room", {d["name"].Str()}),
            st == "playing" ? PortText("Their seat leaves the game.")
                            : PortText("They can join again with the password."));
      g_nDeviceOpen = 0;
    }
    if (b.pszOff != NULL)
      TipOn(Id(bPane ? "lnpanebtn" : "lncardbtn", k), b.pszLabel, b.pszOff);
    ++k;
  }
  if (!bPane) {
    Spacer();
    Decl c = Box("lncardclose");
    c.d.layout.sizing.height = Fixed(Sc(36));
    c.d.layout.sizing.width = Fixed(Sc(120));
    if (ScMini(c, PortText("Close"), 'C') || bOut)
      g_nDeviceOpen = 0;
  }
  Close();
  // its part of tonight
  Decl lg;
  lg.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  lg.d.layout.sizing.width = Grow();
  lg.d.layout.childGap = Gap(Fs(5));
  lg.d.layout.padding = Pad(0, 0, Fs(8), 0);
  lg.d.border = Border(kLbDivider, 0, 0, Fs(2), 0);
  Open(lg);
  Title(PortText("Tonight"));
  const PortJson &log = Room()["log"];
  int nLines = 0;
  for (size_t i = 0; i < log.size() && nLines < 8; ++i) {
    const PortJson &l = log.At(i);
    bool bMine = false;
    for (size_t j = 0; j < l["devs"].size(); ++j)
      bMine = bMine || static_cast<int>(l["devs"].At(j).Num()) == nId;
    if (!bMine)
      continue;
    ++nLines;
    const double fAgo = l["ago"].Num();
    Decl r;
    r.d.layout.childGap = Gap(Fs(12));
    Open(r);
    Decl t;
    t.d.layout.sizing.width = Fixed(Sc(90));
    Open(t);
    Text(Keep(fAgo < 60 ? std::string(PortText("just now")) : PortTextF("{0} ago", {Ago(fAgo)})), UI_FACE_TEXT, Fs(13),
         0.0f, kLbInkDesc);
    Close();
    Text(Keep(l["text"].Str()), UI_FACE_TEXT, Fs(14), 0.0f, l["kind"].Str() == "bad" ? kBad : kLbInkInfo);
    Close();
  }
  if (nLines == 0)
    Fine(PortText("Nothing of theirs in tonight\xe2\x80\x99s log yet."));
  Close();
  if (!bPane)
    Close();  // the dialog
}

bool DeviceCardKeys(const PortJson &d)
{
  if (Key(VK_ESCAPE) || Key('C')) {
    g_nDeviceOpen = 0;
  } else if (Key('W') && (d["state"].Str() == "playing" || d["state"].Str() == "watching")) {
    if (const PortJson *m = MatchOf(static_cast<int>(d["match"].Num())))
      WatchMatch(static_cast<int>((*m)["id"].Num()));
  } else if (Key('R') && !d["admin"].b) {
    LanClientSay("{\"t\":\"remove\",\"dev\":" + std::to_string(static_cast<int>(d["id"].Num())) + "}");
    Toast(PortTextF("{0} left the room", {d["name"].Str()}), PortText("They can join again with the password."));
    g_nDeviceOpen = 0;
  }
  return true;
}

void DevicesTab(float x, float y, float w, float h, float px, float pw)
{
  Decl wrap = At("lndevs", x, y, w, h);
  wrap.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  wrap.d.clip.vertical = true;
  InnerBox(wrap);
  Open(wrap);
  // the tools: what they're doing, how they play, the order, a name
  Decl tools;
  tools.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  tools.d.layout.sizing.width = Grow();
  tools.d.layout.padding = Pad(Fs(16), Fs(16), Fs(14), Fs(10));
  tools.d.layout.childGap = Gap(Fs(10));
  Open(tools);
  const std::vector<const PortJson *> all = People();
  Decl chips;
  chips.d.layout.childGap = Gap(Fs(6));
  Open(chips);
  for (int i = -1; i < kStateCount; ++i) {
    int n = 0;
    for (const PortJson *d : all)
      n += i < 0 || (*d)["state"].Str() == kStates[i][0];
    Decl c = Box("lndchip", i + 1);
    c.d.layout.sizing.height = Fixed(Sc(34));
    c.d.layout.padding = Pad(Fs(12), Fs(12), 0, 0);
    c.d.layout.childGap = Gap(Fs(8));
    c.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    const bool bOn = g_nFilter == i;
    const bool bHot = n > 0 && Over(c.d.id);
    c.d.backgroundColor = Col(bHot ? kLbItemHi : kLbItem);
    c.d.border = Border(bOn ? kLbHyperionRim : kLbItemRim, Fs(2), Fs(2), Fs(2), Fs(2));
    Radius(c, 17);
    if (n > 0 && Clicked(c.d.id))
      g_nFilter = i;
    Open(c);
    Text(i < 0 ? PortText("All") : PortText(kStates[i][1]), UI_FACE_TEXT, Fs(15), 0.0f,
         n == 0 ? kScInkOff : bOn ? kScInkHi : kInkPlayer);
    Text(Keep(std::to_string(n)), UI_FACE_BOLD, Fs(13), 0.0f, bOn ? kScInkAccept : kLbInkDesc);
    Close();
  }
  Close();
  Decl t2;
  t2.d.layout.sizing.width = Grow();
  t2.d.layout.childGap = Gap(Fs(10));
  t2.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(t2);
  const char *const kRoutes[] = {PortText("Both"), PortText("In a Browser"), PortText("Installed")};
  const int nR = Segments("lndroute", kRoutes, 3, g_nRoute);
  if (nR >= 0)
    g_nRoute = nR;
  std::vector<std::string> sorts;
  for (const char *pszSort : kSorts)
    sorts.push_back(PortText(pszSort));
  const int nS = Pulldown("lndsort", 0, 250, sorts, g_nSort);
  if (nS >= 0)
    g_nSort = nS;
  Decl e = Box("lndsearch");
  e.d.layout.sizing.width = Grow();
  e.d.layout.sizing.height = Fixed(Sc(36));
  if (Clicked(e.d.id))
    g_bSearchFocus = true;
  else if (g_bPressed && !Over(e.d.id))
    g_bSearchFocus = false;
  ScEdit(e, &g_search, 40, PortText("Find a name or a device"), g_bSearchFocus);
  Close();
  Close();
  // the head of the list
  Decl hd;
  hd.d.layout.sizing.width = Grow();
  hd.d.layout.padding = Pad(Fs(28), Fs(28), Fs(4), Fs(6));
  hd.d.border = Border(kLbDivider, 0, 0, 0, Fs(2));
  Open(hd);
  const char *const kNames[] = {PortText("Device"),  PortText("Plays"),            PortText("Doing"),
                                PortText("Latency"), PortText("Cost to the host"), PortText("In the room")};
  for (int i = 0; i < 6; ++i) {
    Cell(kCols[i]);
    Text(kNames[i], UI_FACE_WIDE, Fs(12), 0.04f, kScInkLabel);
    Close();
  }
  Close();
  // the rows, grouped by what they're doing when that is the order
  const std::vector<const PortJson *> list = Listed();
  g_shown.clear();
  for (const PortJson *d : list)
    g_shown.push_back(static_cast<int>((*d)["id"].Num()));
  if (std::find(g_shown.begin(), g_shown.end(), g_nSel) == g_shown.end())
    g_nSel = g_shown.empty() ? 0 : g_shown[0];
  Decl body = Box("lndbody");
  body.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  body.d.layout.sizing.width = Grow();
  body.d.layout.sizing.height = Grow();
  body.d.layout.padding = Pad(Fs(16), Fs(16), Fs(8), Fs(8));
  body.d.layout.childGap = Gap(Fs(4));
  body.d.clip.vertical = true;
  const bool bOverBody = Over(body.d.id);
  Open(body);
  if (list.empty())
    Fine(all.empty()          ? PortText("Nobody has joined yet. The Room tab\xe2\x80\x99s join card says how.")
         : g_search.empty()   ? PortText("Nobody matches. Choose All to see everyone.")
                              : PortText("Nobody matches. Clear the search to see everyone."),
         kLbInkDesc, 16);
  const int nFit = SDL_max(1, static_cast<int>((h - 230) / 62));
  g_nFirst += WheelRows(bOverBody, 1);  // its rows are tall
  // the selection stays in view
  const int nSelAt = static_cast<int>(std::find(g_shown.begin(), g_shown.end(), g_nSel) - g_shown.begin());
  if (nSelAt < static_cast<int>(g_shown.size())) {
    if (nSelAt < g_nFirst)
      g_nFirst = nSelAt;
    else if (nSelAt >= g_nFirst + nFit)
      g_nFirst = nSelAt - nFit + 1;
  }
  g_nFirst = SDL_clamp(g_nFirst, 0, SDL_max(0, static_cast<int>(list.size()) - nFit));
  std::string last;
  for (int i = g_nFirst; i < static_cast<int>(list.size()) && i < g_nFirst + nFit; ++i) {
    const PortJson &d = *list[i];
    const std::string st = d["state"].Str();
    if (g_nSort == 0 && st != last) {
      last = st;
      int n = 0;
      for (const PortJson *x : list)
        n += (*x)["state"].Str() == st;
      Decl gh;
      gh.d.layout.sizing.width = Grow();
      gh.d.layout.padding = Pad(Fs(4), Fs(4), Fs(8), Fs(2));
      gh.d.layout.childGap = Gap(Fs(8));
      gh.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      Open(gh);
      Light(st);
      Text(PortText(kStates[StateIndex(st) < kStateCount ? StateIndex(st) : 4][1]), UI_FACE_BOLD, Fs(15), 0.0f,
           kScInkHi);
      Text(Keep(std::to_string(n)), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
      Close();
    }
    Row(d, i);
  }
  Close();
  // the summary
  int nStream = 0;
  const PortJson *slow = NULL;
  double fSum = 0;
  int nLive = 0;
  for (const PortJson *d : all) {
    nStream += (*d)["route"].Str() == "stream";
    if ((*d)["state"].Str() == "refused")
      continue;
    ++nLive;
    fSum += (*d)["rtt"].Num();
    if (slow == NULL || (*d)["rtt"].Num() > (*slow)["rtt"].Num())
      slow = d;
  }
  Decl sum;
  sum.d.layout.sizing.width = Grow();
  sum.d.layout.padding = Pad(Fs(16), Fs(16), Fs(8), Fs(10));
  sum.d.border = Border(kLbDivider, 0, 0, Fs(2), 0);
  Open(sum);
  const int nAll = static_cast<int>(all.size());
  // {0} how many devices are in the room, {1} how many play in a browser, {2} how many installed
  std::string line = PortTextNF("{0} device: {1} streamed, {2} installed", "{0} devices: {1} streamed, {2} installed",
                               nAll, {std::to_string(nAll), std::to_string(nStream), std::to_string(nAll - nStream)});
  if (slow != NULL) {
    char szMean[16];
    SDL_snprintf(szMean, sizeof szMean, "%.0f", nLive ? fSum / nLive : 0.0);
    // {0} the average latency, {1} the slowest device, {2} its latency
    line += kDot + PortTextF("latency {0} ms on average, {1} the slowest at {2} ms",
                            {szMean, (*slow)["name"].Str(), std::to_string(static_cast<int>((*slow)["rtt"].Num()))});
  }
  Text(Keep(line), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
  Close();
  Close();
  // the selected device's card
  BoxAt("lndpane", px, y, pw, h, NULL);
  if (const PortJson *d = DeviceOf(g_nSel))
    DeviceCard(*d, true);
  else
    Fine(PortText("Select a device to see it here."), kLbInkDesc, 16);
  Close();
}

bool DevicesKeys(void)
{
  if (g_bSearchFocus) {
    if (Key(VK_RETURN) || Key(VK_ESCAPE))
      g_bSearchFocus = false;
    return true;
  }
  const int nAt = static_cast<int>(std::find(g_shown.begin(), g_shown.end(), g_nSel) - g_shown.begin());
  if (Key(VK_DOWN) && nAt + 1 < static_cast<int>(g_shown.size())) {
    g_nSel = g_shown[nAt + 1];
    return true;
  }
  if (Key(VK_UP) && nAt > 0 && nAt < static_cast<int>(g_shown.size())) {
    g_nSel = g_shown[nAt - 1];
    return true;
  }
  if (Key(VK_RETURN) && g_nSel != 0) {
    OpenDevice(g_nSel);
    return true;
  }
  if (Key('R') && g_nSel != 0) {
    if (const PortJson *d = DeviceOf(g_nSel))
      if (!(*d)["admin"].b) {
        LanClientSay("{\"t\":\"remove\",\"dev\":" + std::to_string(g_nSel) + "}");
        Toast(PortTextF("{0} left the room", {(*d)["name"].Str()}), PortText("They can join again with the password."));
      }
    return true;
  }
  return false;
}

}  // namespace Lan
}  // namespace FrontKit
