// port_front_hud.cpp -- Settings' HUD tab: the designs the player can choose, a
// card each with a plan of its panels at the screen's shape, Classic, Mech and R9 first and the
// player's own after them (port_hud.h, PortHudDesigns); a picture of the chosen one over a map at
// the screen's shape, as its picks stand; and its settings: Mech's layout, size and
// material, the options its file offers, its light, its sounds and which of its buttons it shows;
// and, for every design without side columns, how far the map is raised over it. Classic's are
// the console rows (Console skin, Console layout), drawn by Settings itself. As on every tab,
// nothing applies until Accept; the picks are Settings' "hud" section (port_hud.h), written only
// when they differ from what was saved.
#include <windows.h>
#include <math.h>
#include <string.h>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_frontkit.h"
#include "port_frontscreens.h"
#include "port_front_internal.h"
#include "port_ui.h"
#include "../hud/port_hud.h"
#include "../hud/port_hudbind.h"
#include "../hud/port_hudfile.h"
#include "../hud/port_hudplay.h"
#include "../hud/port_hudstate.h"    // PortHudState, the picture's
#include "../platform/port_paths.h"  // PortDataFolder
#include "glues.h"                   // mem_free
#include "gluMap.h"                  // FindMapFile
#include "maphdr.h"                  // get_map_chk
#include "port_frontlobby.h"         // MapTerrainRgba
#include "../platform/port_settings.h"
#include "../platform/port_util.h"
#include "../render/port_present.h"  // PortPresentGpu

using namespace FrontKit;

namespace Front {

namespace {

// The tab's picks: the keys changed since it opened, by key, "" for the design's own.
std::map<std::string, std::string> g_edits;
std::vector<PortHudDesignInfo> g_designs;
int g_nFirst;          // the first card the list shows
float g_fPaneScroll;   // the pane's scroll, and how far it goes
float g_fPaneMax;

std::string Saved(const std::string &key) {
  char sz[512];
  return PortSettingString("hud", key.c_str(), sz, sizeof sz) ? std::string(sz) : std::string();
}

std::string Value(const std::string &key, const char *pszDefault = "") {
  auto it = g_edits.find(key);
  const std::string v = it != g_edits.end() ? it->second : Saved(key);
  return v.empty() ? std::string(pszDefault) : v;
}

void Pick(const std::string &key, const std::string &value) { g_edits[key] = value; }

// A design read for the tab: its options, its lights and its panels laid out at the screen's
// shape, kept while its files and picks are the same.
struct Loaded {
  std::string key;
  bool bOk = false;
  std::unique_ptr<HudDesign> pDesign;
  std::unique_ptr<HudDesign> pReplay;  // the replay layout its layout names, read, or none
  std::vector<std::string> errors;
  std::vector<std::array<float, 4>> plan;  // the panels, 0 to 1 of the screen
  double sideL = 0, sideR = 0;             // its side columns, units (HudPlayer::Sides)
  unsigned int nRead = 0;                  // the reads so far of all designs when this was read
  std::unique_ptr<HudPlayer> pShown;       // the picture's player, laid out at its box
  int nShownW = 0, nShownH = 0;
};
std::map<std::string, Loaded> g_loaded;  // by design id
unsigned int g_nReads;

float Aspect(void) { return UiHeight() > 0 ? static_cast<float>(UiWidth()) / UiHeight() : 16.0f / 9.0f; }

Loaded &Load(const std::string &id) {
  Loaded &l = g_loaded[id];
  // read again when a pick of its changes, and not otherwise: the files are not looked at each frame
  std::string key = Value("layout") + "|" + Value("size") + "|" + Value("material") + "|" + Value(id + ".light") + "|" +
                    std::to_string(static_cast<int>(Aspect() * 100));
  const std::string options = id + ".option.", replayOptions = id + ".replay.option.";
  for (const auto &e : g_edits)
    if (e.first.compare(0, options.size(), options) == 0 || e.first.compare(0, replayOptions.size(), replayOptions) == 0)
      key += "|" + e.first + "=" + e.second;
  if (key == l.key)
    return l;
  l.key = key;
  l.bOk = false;
  l.pShown.reset();  // it points at the design
  l.pDesign.reset(new HudDesign);
  l.pReplay.reset();
  l.errors.clear();
  l.plan.clear();
  l.sideL = l.sideR = 0;
  l.nRead = ++g_nReads;
  std::string layout, style;
  std::vector<std::string> warnings;
  if (!PortHudDesignFiles(id, Value("layout", "span"), Value("size", "full"), Value("material", "gunmetal"), &layout,
                          &style)) {
    if (id != "classic")
      l.errors.push_back(PortText("Its files are not there."));
    return l;
  }
  l.bOk = PortHudRead(layout, style, Value(id + ".light"), [&](const std::string &o) { return Value(options + o); },
                      l.pDesign.get(), &l.errors, &warnings);
  if (!l.bOk)
    return l;
  // the replay layout it names, for its own options
  if (!l.pDesign->replay.empty()) {
    std::unique_ptr<HudDesign> pReplay(new HudDesign);
    std::vector<std::string> errors, replayWarnings;
    if (PortHudRead(layout.substr(0, layout.find_last_of("/\\") + 1) + l.pDesign->replay, style, Value(id + ".light"),
                    [&](const std::string &o) {
                      const std::string v = Value(replayOptions + o);
                      return v.empty() ? Value(options + o) : v;
                    },
                    pReplay.get(), &errors,
                    &replayWarnings))
      l.pReplay = std::move(pReplay);
  }
  HudPlayer player;
  player.Load(l.pDesign.get());
  const int nH = 1080, nW = static_cast<int>(lroundf(nH * Aspect()));
  player.Layout(nW, nH);
  player.Sides(&l.sideL, &l.sideR);
  HudEnv env;
  player.Frame(env);
  const std::vector<HudPanelFrame> &frames = player.PanelFrames();
  for (size_t p = 0; p < l.pDesign->panels.size(); ++p) {
    double r[4];
    if (p < frames.size() && !frames[p].bVisible)
      continue;
    if (player.PanelRect(static_cast<int>(p), r) && r[2] > 0 && r[3] > 0)
      l.plan.push_back({static_cast<float>(r[0] / player.LW()), static_cast<float>(r[1] / 1080.0),
                        static_cast<float>(r[2] / player.LW()), static_cast<float>(r[3] / 1080.0)});
  }
  return l;
}

// A card's plan: the screen at its shape, dark, and the design's panels on it; Classic's is
// the original's console across the foot.
void DrawPlan(float x, float y, float w, float h, void *pCtx) {
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  UiFill(x, y, w, h, UiColor{0.02f, 0.03f, 0.05f, 1.0f}, Sc(4));
  UiRing(x, y, w, h, kScWellRim, Sc(4), Sc(1.5f));
  if (d.n < 0 || d.n >= static_cast<int>(g_designs.size()))
    return;
  const UiColor panel = d.f > 0.5f ? kScInkHi : kScInkButton;
  const UiColor fill = UiColor{panel.r, panel.g, panel.b, 0.35f};
  if (g_designs[d.n].id == "classic") {
    UiFill(x + 1, y + h * 0.78f, w - 2, h * 0.22f - 1, fill);
    return;
  }
  auto it = g_loaded.find(g_designs[d.n].id);
  if (it == g_loaded.end())
    return;
  for (const auto &r : it->second.plan) {
    const float rx = x + r[0] * w, ry = y + r[1] * h, rw = r[2] * w, rh = r[3] * h;
    UiFill(rx, ry, rw, rh, fill, Sc(1));
    UiRing(rx, ry, rw, rh, panel, Sc(1), Sc(1));
  }
}

// A row of round choices; returns the one clicked, or -1.
int Radios(const char *pszId, int nRow, const std::vector<std::string> &words, int nAt, bool bLive) {
  int nClicked = -1;
  Decl row;
  row.d.layout.childGap = Gap(Fs(24));
  row.d.layout.padding = Pad(0, 0, Fs(4), 0);
  Open(row);
  for (size_t k = 0; k < words.size(); ++k) {
    const Clay_ElementId id = Id(pszId, nRow * 32 + static_cast<int>(k));
    const bool bOn = static_cast<int>(k) == nAt;
    const bool bHot = bLive && Over(id);
    if (bLive && Clicked(id))
      nClicked = static_cast<int>(k);
    Decl r;
    r.d.id = id;
    r.d.layout.childGap = Gap(Fs(10));
    r.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(r);
    Decl dot;
    dot.d.layout.sizing.width = Fixed(Sc(28));
    dot.d.layout.sizing.height = Fixed(Sc(28));
    dot.d.custom.customData = Custom(DrawScCheck, kScButton, 0.0f, (bOn ? 1 : 0) | (bHot ? 2 : 0) | 4 | (bLive ? 0 : 8));
    Open(dot);
    Close();
    Text(Keep(words[k]), UI_FACE_BOLD, Fs(17), 0.0f, !bLive ? kScInkOff : bOn || bHot ? kScInkHi : kScInkButton);
    Close();
  }
  Close();
  return nClicked;
}

// A check and its label; true when clicked.
bool Check(const char *pszId, int nIndex, const char *pszLabel, bool bOn, bool bLive) {
  const Clay_ElementId id = Id(pszId, nIndex);
  const bool bHot = bLive && Over(id);
  Decl line;
  line.d.id = id;
  line.d.layout.childGap = Gap(Fs(10));
  line.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(line);
  Decl box;
  box.d.layout.sizing.width = Fixed(Sc(29));
  box.d.layout.sizing.height = Fixed(Sc(28));
  box.d.custom.customData = Custom(DrawScCheck, kScButton, 0.0f, (bOn ? 1 : 0) | (bHot ? 2 : 0) | (bLive ? 0 : 8));
  Open(box);
  Close();
  Text(pszLabel, UI_FACE_BOLD, Fs(17), 0.0f, !bLive ? kScInkOff : bHot ? kScInkHi : kScInkLabel);
  Close();
  return bLive && Clicked(id);
}

void Label(const char *psz) {
  Decl l;
  l.d.layout.padding = Pad(0, 0, Fs(10), Fs(2));
  Open(l);
  Text(psz, UI_FACE_BOLD, Fs(18), 0.0f, kScInkLabel);
  Close();
}

void Note(const std::string &s, UiColor c) {
  Decl n;
  n.d.layout.sizing.width = Grow();
  n.d.layout.padding = Pad(0, 0, Fs(2), Fs(2));
  Open(n);
  Text(Keep(s), UI_FACE_TEXT, Fs(15), 0.0f, c, -1, 0.0f);
  Close();
}

// A row of colours to pick: the light (the design's own, six of Hyperion's) and the selection's
// boxes (1.17's blue, the design's light, the same six). A value of "" or "light" is drawn as a
// shaded tile, a colour as itself.
struct Swatch {
  const char *pszValue, *pszLabel;
};
const Swatch kLights[] = {{"", PT_("Its own")},      {"#3fa9ff", PT_("Blue")}, {"#39e0c8", PT_("Teal")},
                          {"#5be35b", PT_("Green")}, {"#ffb030", PT_("Amber")}, {"#ff4a3d", PT_("Red")},
                          {"#b07cff", PT_("Violet")}};
const Swatch kBoxes[] = {{"", PT_("Original")},     {"light", PT_("Light")},   {"#3fa9ff", PT_("Blue")},
                         {"#39e0c8", PT_("Teal")},  {"#5be35b", PT_("Green")}, {"#ffb030", PT_("Amber")},
                         {"#ff4a3d", PT_("Red")},   {"#b07cff", PT_("Violet")}};

void DrawSwatch(float x, float y, float w, float h, void *pCtx) {
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  const char *pszColour = ((d.n & 64) != 0 ? kBoxes : kLights)[d.n & 15].pszValue;
  float c[4] = {0.55f, 0.6f, 0.68f, 1.0f};
  const bool bOn = (d.n & 16) != 0, bHot = (d.n & 32) != 0;
  UiFill(x, y, w, h, UiColor{0.04f, 0.05f, 0.07f, 1.0f}, Sc(5));
  if (pszColour[0] == '#' && HudParseColour(pszColour, c)) {
    UiFill(x + Sc(4), y + Sc(4), w - Sc(8), h - Sc(8), UiColor{c[0], c[1], c[2], 1.0f}, Sc(3));
  } else if ((d.n & 64) != 0 && pszColour[0] == 0) {
    // 1.17's own boxes: their blue
    UiFill(x + Sc(4), y + Sc(4), w - Sc(8), h - Sc(8), UiColor{0.13f, 0.29f, 0.85f, 1.0f}, Sc(3));
  } else {
    // the design's own: its colour is not known here; a shaded tile
    UiGradient(x + Sc(4), y + Sc(4), w - Sc(8), h - Sc(8), UiColor{0.6f, 0.7f, 0.8f, 1.0f}, UiColor{0.2f, 0.25f, 0.3f, 1.0f},
               true, Sc(3));
  }
  UiRing(x, y, w, h, bOn ? kScInkHi : bHot ? kScInkButton : kScWellRim, Sc(5), Sc(bOn ? 2.5f : 1.5f));
}

template <size_t N>
void Swatches(const char *pszLabel, const char *pszId, const std::string &key, const Swatch (&list)[N]) {
  Label(pszLabel);
  const std::string now = Value(key);
  Decl row;
  row.d.layout.childGap = Gap(Fs(12));
  Open(row);
  for (int k = 0; k < static_cast<int>(N); ++k) {
    const Clay_ElementId cid = Id(pszId, k);
    const bool bOn = SDL_strcasecmp(now.c_str(), list[k].pszValue) == 0;
    const bool bHot = Over(cid);
    if (Clicked(cid))
      Pick(key, list[k].pszValue);
    Decl col;
    col.d.id = cid;
    col.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    col.d.layout.childGap = Gap(Fs(5));
    col.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    Open(col);
    Decl sw;
    sw.d.layout.sizing.width = Fixed(Sc(52));
    sw.d.layout.sizing.height = Fixed(Sc(36));
    sw.d.custom.customData =
        Custom(DrawSwatch, kScButton, 0.0f, k | (bOn ? 16 : 0) | (bHot ? 32 : 0) | (list == kBoxes ? 64 : 0));
    Open(sw);
    Close();
    Text(PortText(list[k].pszLabel), UI_FACE_TEXT, Fs(13), 0.0f, bOn || bHot ? kScInkHi : kScInkOff);
    Close();
  }
  Close();
}

// The map raised over the HUD, for every design (port_view.cpp): 0 to 200 px
// at 1080p. The slider is dragged, or stepped while the pointer is on it: the arrows by 1, with
// Shift or Page Up and Page Down by 10, Home and End to its ends. Reset puts it back to 0.
const int kRaiseMost = 200;
bool g_bRaiseDrag;

int Raised(void) { return SDL_clamp(atoi(Value("mapRaise", "0").c_str()), 0, kRaiseMost); }

void PickRaise(int n) {
  n = SDL_clamp(n, 0, kRaiseMost);
  Pick("mapRaise", n > 0 ? std::to_string(n) : std::string());
}

void RaiseRow(bool bLive) {
  Decl gap;
  gap.d.layout.sizing.height = Fixed(Sc(10));
  Open(gap);
  Close();
  Decl rule;
  rule.d.layout.sizing.width = Grow();
  rule.d.layout.sizing.height = Fixed(Sc(1.5f));
  rule.d.backgroundColor = Col(kScWellRim);
  Open(rule);
  Close();
  int n = Raised();
  Decl head;
  head.d.layout.sizing.width = Grow();
  head.d.layout.childAlignment.y = CLAY_ALIGN_Y_BOTTOM;
  Open(head);
  Label(PortText("Raise the map"));
  Decl grow;
  grow.d.layout.sizing.width = Grow();
  Open(grow);
  Close();
  Text(Keep(PortTextF("{0} px", {std::to_string(n)})), UI_FACE_BOLD, Fs(17), 0.0f,
       !bLive ? kScInkOff : n > 0 ? kScInkHi : kScInkButton);
  Close();
  Decl line;
  line.d.layout.sizing.width = Grow();
  line.d.layout.childGap = Gap(Fs(16));
  line.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(line);
  const Clay_ElementId id = Id("hudraise");
  const bool bOver = bLive && Over(id);
  if (g_bPressed && bOver)
    g_bRaiseDrag = true;
  if (!g_bDown || !bLive)
    g_bRaiseDrag = false;
  if (g_bRaiseDrag) {
    const Clay_ElementData e = Clay_GetElementData(id);
    if (e.found && e.boundingBox.width > Sc(18)) {
      const float f = SDL_clamp((g_mouseX - e.boundingBox.x - Sc(9)) / (e.boundingBox.width - Sc(18)), 0.0f, 1.0f);
      n = static_cast<int>(lroundf(f * kRaiseMost));
      PickRaise(n);
    }
  } else if (bOver) {
    const int nStep = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 ? 10 : 1;
    int nTo = n;
    if (Key(VK_LEFT) || Key(VK_DOWN))
      nTo -= nStep;
    if (Key(VK_RIGHT) || Key(VK_UP))
      nTo += nStep;
    if (Key(VK_NEXT))
      nTo -= 10;
    if (Key(VK_PRIOR))
      nTo += 10;
    if (Key(VK_HOME))
      nTo = 0;
    if (Key(VK_END))
      nTo = kRaiseMost;
    if (nTo != n) {
      PickRaise(nTo);
      n = Raised();
    }
  }
  Decl sl;
  sl.d.id = id;
  sl.d.layout.sizing.width = Grow();
  sl.d.layout.sizing.height = Fixed(Sc(32));
  sl.d.custom.customData = Custom(DrawScSlider, kScButton, static_cast<float>(n) / kRaiseMost,
                                  (bOver || g_bRaiseDrag ? 1 : 0) | (bLive ? 0 : 2));
  Open(sl);
  Close();
  Decl reset;
  reset.d.id = Id("hudraisereset");
  reset.d.layout.sizing.height = Fixed(Sc(42));
  if (ScMini(reset, PortText("Reset"), 0, bLive && n > 0))
    PickRaise(0);
  Close();
  if (!bLive)
    Note(PortText("Not used with a column layout: the battlefield sits beside the column, nothing under it."),
         kScInkMenu);
  Note(PortText("Draws the battlefield higher, so units along the bottom of the screen come out from under a HUD that "
                "stands tall at its sides. You lose as much at the top of the view, and the minimap\xE2\x80\x99s box "
                "shows what is left. Counted at 1080p, so one setting suits any resolution, and the same for every "
                "HUD design."),
       kScInkOff);
}

// ---------------------------------------------------------------------------
// The picture: the chosen design over a part of a melee map at the screen's shape, as
// the game would show it with the tab's picks: the map from its left edge, low on it, beside a
// side column or raised. Drawn on the GPU into its box between the frame's other parts
// (port_hud.h, PortHudPreview); the box itself is black until then.

// The map: the first of a few melee maps every install has, read once.
struct PreviewMap {
  bool bTried = false;
  std::string title;
  std::vector<BYTE> chk;
  int nTilesW = 0, nTilesH = 0;
  std::vector<unsigned char> rgba;  // the picture, in the game's pixels
  int w = 0, h = 0;
  unsigned long long key = 0;
};
PreviewMap g_previewMap;
PortHudState g_previewState;
HudEnv g_previewEnv;
std::vector<bool> g_previewHidden;
Uint64 g_nPreviewMs;

void PreviewMapRead(void) {
  PreviewMap &m = g_previewMap;
  if (m.bTried)
    return;
  m.bTried = true;
  // in the install's Maps, or a map folder's own (--maps)
  static const char *const kMaps[][2] = {{"Maps\\(2)Bottleneck.scm", "Bottleneck"},
                                         {"(2)Bottleneck.scm", "Bottleneck"},
                                         {"Maps\\(4)Lost Civilization.scm", "Lost Civilization"},
                                         {"(4)Lost Civilization.scm", "Lost Civilization"}};
  for (const auto &map : kMaps) {
    char szPath[MAX_PATH];
    if (!FindMapFile(map[0], szPath, sizeof szPath))
      continue;
    DWORD cb = 0;
    BYTE *p = static_cast<BYTE *>(get_map_chk(szPath, &cb));
    if (p == NULL)
      continue;
    std::vector<unsigned char> one;
    if (MapTerrainRgba(p, cb, 0, 0, 1, 1, &one, &m.nTilesW, &m.nTilesH)) {
      m.chk.assign(p, p + cb);
      m.title = map[1];
    }
    mem_free(p, __FILE__, __LINE__);
    if (!m.chk.empty())
      return;
  }
}

// The picture for the screen's shape, nW game pixels across, past a left column of fLeft pixels
// and raised nRaise: made again when one of them changes.
void PreviewMapPicture(int nW, int nLeft, int nRaise) {
  PreviewMap &m = g_previewMap;
  if (m.chk.empty() || nW <= 0)
    return;
  const int nH = 480;
  const int x0 = -nLeft;
  const int y0 = SDL_max(0, m.nTilesH * 32 - nH - 96) + nRaise;
  const unsigned long long key = (static_cast<unsigned long long>(nW) << 40) ^ (static_cast<unsigned long long>(x0 & 0xFFFFF) << 20) ^
                                 static_cast<unsigned long long>(y0);
  if (key == m.key && !m.rgba.empty())
    return;
  if (MapTerrainRgba(m.chk.data(), static_cast<DWORD>(m.chk.size()), x0, y0, nW, nH, &m.rgba)) {
    m.w = nW;
    m.h = nH;
    m.key = key;
  }
}

// What the picture's readouts show: a game a few minutes in.
void PreviewStateFill(void) {
  PortHudState &s = g_previewState;
  s = PortHudState();
  s.bInGame = true;
  s.nSeconds = 420;
  s.nRace = 1;
  s.nColour = 0xF40404;
  s.nMinerals = 1250;
  s.nGas = 640;
  s.nSupplyUsed = s.anSupplyUsed[1] = 74;
  s.nSupplyMax = s.anSupplyMax[1] = 134;
  s.nApm = 186;
  s.nIdleWorkers = 2;
  s.nArmy = 38;
  s.anGroups[1] = 12;
  s.anGroups[2] = 8;
  s.anGroups[3] = 1;
  s.nGroupLast = -1;
  s.bTerrain = true;
  for (int i = 0; i < PORT_HUD_BTN_KINDS; ++i)
    s.abButton[i] = i != PORT_HUD_BTN_MESSAGING;
  g_previewEnv.Fill(s);
}

// The box's draw: the picture at the screen's shape at its left, black, and its place in the
// frame's list. The box is as wide as the pane, so its width never comes from the picture's.
void DrawPreview(float x, float y, float w, float h, void *pCtx) {
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  w = SDL_min(w, h * Aspect());
  h = w / Aspect();
  UiFill(x, y, w, h, UiColor{0.0f, 0.0f, 0.0f, 1.0f});
  auto it = g_loaded.find(Value("design", "classic"));
  if (d.n == 1 && it != g_loaded.end() && it->second.pShown) {
    PortHudPreview p;
    p.x = x;
    p.y = y;
    p.w = w;
    p.h = h;
    UiPrims(&p.nAtPrim);
    p.pPlayer = it->second.pShown.get();
    p.pEnv = &g_previewEnv;
    p.pState = &g_previewState;
    p.nKey = it->second.nRead;
    if (!g_previewMap.rgba.empty()) {
      p.pMap = g_previewMap.rgba.data();
      p.nMapW = g_previewMap.w;
      p.nMapH = g_previewMap.h;
      p.nMapKey = g_previewMap.key;
    }
    p.pHidden = &g_previewHidden;
    PortHudPreviewSet(p);
  }
  UiRing(x, y, w, h, kScWellRim, 0.0f, Sc(1.5f));
}

void Preview(void) {
  const std::string id = Value("design", "classic");
  if (id == "classic" || !PortPresentGpu())
    return;
  // over a game the picture is the game's own console, which takes the design on Accept: the
  // picture's drawing is the menus' (PortGpuUiEnd), and it shares the console's
  if (SettingsInGame()) {
    Note(PortText("In a game, your console takes the design once you accept it."), kScInkOff);
    return;
  }
  Loaded &l = Load(id);
  if (!l.bOk)
    return;
  // its size: the pane's width, as the box had it, at the screen's shape, no taller than a third
  // of the screen
  const Clay_ElementData row = Clay_GetElementData(Id("hudpreviewbox"));
  const float fAspect = Aspect();
  float w = row.found && row.boundingBox.width > 0 ? row.boundingBox.width : Sc(600);
  float h = w / fAspect;
  if (h > UiHeight() * 0.32f) {
    h = UiHeight() * 0.32f;
    w = h * fAspect;
  }
  const int nW = SDL_max(1, static_cast<int>(lroundf(w))), nH = SDL_max(1, static_cast<int>(lroundf(h)));
  // its player, laid out at the box, its clock the window's
  if (!l.pShown) {
    l.pShown.reset(new HudPlayer);
    l.pShown->Load(l.pDesign.get());
    l.nShownW = l.nShownH = 0;
  }
  if (nW != l.nShownW || nH != l.nShownH) {
    l.pShown->Layout(nW, nH);
    l.nShownW = nW;
    l.nShownH = nH;
  }
  const Uint64 nNow = SDL_GetTicks();
  const double dt = g_nPreviewMs != 0 ? static_cast<double>(nNow - g_nPreviewMs) : 0.0;
  g_nPreviewMs = nNow;
  l.pShown->Advance(dt < 100.0 ? dt : 100.0);
  PreviewStateFill();
  // the buttons left out, as the tab has them
  g_previewHidden.assign(HUD_SLOTS, false);
  for (int k = 0; k < HUD_SLOTS; ++k)
    g_previewHidden[k] = Value(id + ".hide." + HudSlotName(k)) == "1";
  // the map, past a left column or raised, as the game places it (port_view.cpp)
  PreviewMapRead();
  const double fRows = 480.0 / 1080.0;
  const bool bSides = l.sideL > 0 || l.sideR > 0;
  PreviewMapPicture(static_cast<int>(lround(480.0 * fAspect)), static_cast<int>(lround(l.sideL * fRows)),
                    bSides ? 0 : static_cast<int>(lround(Raised() * fRows)));
  Decl rowDecl;
  rowDecl.d.id = Id("hudpreviewrow");
  rowDecl.d.layout.sizing.width = Grow();
  rowDecl.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  rowDecl.d.layout.childGap = Gap(Fs(6));
  Open(rowDecl);
  Decl box;
  box.d.id = Id("hudpreviewbox");
  box.d.layout.sizing.width = Grow();
  box.d.layout.sizing.height = Fixed(h);
  box.d.custom.customData = Custom(DrawPreview, kScButton, 0.0f, 1);
  Open(box);
  Close();
  // {0} the map it is shown over
  Note(g_previewMap.chk.empty() ? std::string(PortText("As it shows on your screen."))
                                : PortTextF("As it shows on your screen, over {0}.", {g_previewMap.title}),
       kScInkOff);
  Close();
}

// The buttons a player may leave out of a design; the menu, diplomacy and messaging always show.
const struct {
  int nSlot;
  const char *pszLabel;
} kHideable[] = {{HUD_SLOT_APM, PT_("Actions per minute")},
                 {HUD_SLOT_IDLE, PT_("Idle workers")},
                 {HUD_SLOT_ARMY, PT_("Army")},
                 {HUD_SLOT_GROUPS, PT_("Control groups")},
                 {HUD_SLOT_TERRAIN, PT_("Terrain")},
                 {HUD_SLOT_TV, PT_("Replay TV")},
                 {HUD_SLOT_FOLLOW, PT_("Follow")}};

bool HasSlot(const HudDesign &d, int nSlot) {
  for (const HudPanelDef &p : d.panels)
    for (const HudLayerDef &l : p.layers)
      if ((l.kind == HUD_SLOT && l.nSlot == nSlot) || (l.nAction >= 0 && HudActionSlot(l.nAction) == nSlot))
        return true;
  return false;
}

// The chosen design's settings.
void Pane(const std::vector<int> &rows, void (*pfnRow)(int)) {
  const std::string id = Value("design", "classic");
  const PortHudDesignInfo *pInfo = NULL;
  for (const PortHudDesignInfo &d : g_designs)
    if (d.id == id)
      pInfo = &d;
  Decl clip;
  clip.d.id = Id("hudpane");
  clip.d.layout.sizing.width = Grow();
  clip.d.layout.sizing.height = Grow();
  clip.d.clip.vertical = true;
  g_fPaneScroll = SDL_clamp(g_fPaneScroll + WheelPixels(Over(clip.d.id), Sc(90)), 0.0f, g_fPaneMax);
  clip.d.clip.childOffset.y = -g_fPaneScroll;
  Open(clip);
  Decl pane;
  pane.d.id = Id("hudpanerows");
  pane.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  pane.d.layout.sizing.width = Grow();
  pane.d.layout.childGap = Gap(Fs(6));
  pane.d.layout.padding = Pad(0, Fs(8), 0, Fs(16));
  Open(pane);
  Text(Keep(pInfo != NULL ? Upper(pInfo->name.c_str()) : Upper(id.c_str())), UI_FACE_WIDE, Fs(20), 0.02f, kScInkHi);
  if (pInfo != NULL && !pInfo->note.empty())
    Note(pInfo->note, kScInkMenu);
  if (id == "classic") {
    // the original's console: the rows Settings keeps for it
    Decl gap;
    gap.d.layout.sizing.height = Fixed(Sc(14));
    Open(gap);
    Close();
    for (int i : rows)
      pfnRow(i);
    Close();
    Close();
    return;
  }
  const Loaded &l = Load(id);
  if (!l.bOk) {
    Note(PortText("This design does not load:"), kScInkMsg);
    for (size_t k = 0; k < l.errors.size() && k < 4; ++k)
      Note(l.errors[k], kScInkOff);
    Close();
    Close();
    return;
  }
  if (id == "mech") {
    static const char *const kLayouts[] = {"span", "flank", "flank-right"};
    static const char *const kMaterials[] = {"gunmetal", "graphite", "blackout"};
    const std::string layout = Value("layout", "span"), size = Value("size", "full"), material = Value("material", "gunmetal");
    int nLayout = 0, nMaterial = 0;
    for (int k = 0; k < 3; ++k) {
      nLayout = layout == kLayouts[k] ? k : nLayout;
      nMaterial = material == kMaterials[k] ? k : nMaterial;
    }
    Label(PortText("Layout"));
    int n = Radios("hudradio", 0, {PortText("Full width"), PortText("Left column"), PortText("Right column")}, nLayout,
                   true);
    if (n >= 0)
      Pick("layout", kLayouts[n]);
    Label(PortText("Size"));
    n = Radios("hudradio", 1, {PortText("Full"), PortText("Minimized")}, size == "min" ? 1 : 0, nLayout == 0);
    if (n >= 0)
      Pick("size", n == 1 ? "min" : "full");
    Label(PortText("Material"));
    n = Radios("hudradio", 2, {PortText("Gunmetal"), PortText("Graphite"), PortText("Blackout")}, nMaterial, true);
    if (n >= 0)
      Pick("material", kMaterials[n]);
  }
  // the options the design's file offers
  int nRow = 3;
  for (const HudOption &o : l.pDesign->options) {
    std::vector<std::string> words;
    int nAt = 0;
    const std::string v = Value(id + ".option." + o.id, o.def.c_str());
    for (size_t k = 0; k < o.values.size(); ++k) {
      words.push_back(o.values[k].label.empty() ? o.values[k].id : o.values[k].label);
      if (o.values[k].id == v)
        nAt = static_cast<int>(k);
    }
    Label(Keep(o.label.empty() ? o.id : o.label));
    const int n = Radios("hudradio", nRow++, words, nAt, true);
    if (n >= 0)
      Pick(id + ".option." + o.id, o.values[n].id);
  }
  // the replay layout's own options, picked apart from the game layout's; one the game layout has
  // too (Width) follows the game layout's pick
  if (l.pReplay)
    for (const HudOption &o : l.pReplay->options) {
      bool bShared = false;
      for (const HudOption &g : l.pDesign->options)
        bShared = bShared || g.id == o.id;
      if (bShared)
        continue;
      std::vector<std::string> words;
      int nAt = 0;
      const std::string key = id + ".replay.option." + o.id, v = Value(key, o.def.c_str());
      for (size_t k = 0; k < o.values.size(); ++k) {
        words.push_back(o.values[k].label.empty() ? o.values[k].id : o.values[k].label);
        if (o.values[k].id == v)
          nAt = static_cast<int>(k);
      }
      std::string label = o.label.empty() ? o.id : o.label;
      if (label.find("Replay") == std::string::npos && label.find("replay") == std::string::npos)
        // {0} the option's own name, as its design gives it
        label = PortTextF("{0} in replays", {label});
      Label(Keep(label));
      const int n = Radios("hudradio", nRow++, words, nAt, true);
      if (n >= 0)
        Pick(key, o.values[n].id);
    }
  Swatches(PortText("Light"), "hudlight", id + ".light", kLights);
  Swatches(PortText("Selection boxes and tooltips"), "hudboxes", id + ".selection", kBoxes);
  Label(PortText("Sounds and tooltips"));
  const bool bSound = Value("sound", "off") == "on";  // off unless turned on
  if (Check("hudsound", 0, PortText("The design\xE2\x80\x99s sounds"), bSound, true))
    Pick("sound", bSound ? "off" : "on");
  // the style's tooltips, set in its fonts and lit in the colour above; off, the original's
  const bool bTips = Value("tooltips", "on") != "off";
  if (Check("hudtips", 0, PortText("The design\xE2\x80\x99s tooltips"), bTips, true))
    Pick("tooltips", bTips ? "off" : "on");
  // the buttons it has, in two columns
  Label(PortText("Buttons"));
  std::vector<int> has;
  for (size_t k = 0; k < sizeof kHideable / sizeof kHideable[0]; ++k)
    if (HasSlot(*l.pDesign, kHideable[k].nSlot) || (l.pReplay && HasSlot(*l.pReplay, kHideable[k].nSlot)))
      has.push_back(static_cast<int>(k));
  if (has.empty())
    Note(PortText("This design has no buttons to leave out."), kScInkOff);
  Decl cols;
  cols.d.layout.childGap = Gap(Fs(40));
  Open(cols);
  for (int c = 0; c < 2; ++c) {
    Decl col;
    col.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    col.d.layout.childGap = Gap(Fs(10));
    Open(col);
    const size_t nHalf = (has.size() + 1) / 2;
    for (size_t j = c == 0 ? 0 : nHalf; j < (c == 0 ? nHalf : has.size()); ++j) {
      const int k = has[j];
      const std::string key = id + ".hide." + HudSlotName(kHideable[k].nSlot);
      const bool bOn = Value(key) != "1";
      if (Check("hudshow", k, PortText(kHideable[k].pszLabel), bOn, true))
        Pick(key, bOn ? "1" : "");
    }
    Close();
  }
  Close();
  RaiseRow(!(l.sideL > 0 || l.sideR > 0));
  Close();
  Close();
}

}  // namespace

void HudTabOpen(void) {
  g_edits.clear();
  g_bRaiseDrag = false;
  g_loaded.clear();
  g_nPreviewMs = 0;
  g_designs = PortHudDesigns();
  // a design held back from players, chosen before it was, is Classic again
  if (PortHudDesignHeld(Saved("design")))
    Pick("design", "");
  g_nFirst = 0;
  g_fPaneScroll = g_fPaneMax = 0.0f;
}

void HudTabAccept(void) {
  bool bAny = false;
  for (const auto &e : g_edits)
    if (e.second != Saved(e.first)) {
      PortSettingSetString("hud", e.first.c_str(), e.second.c_str());
      bAny = true;
    }
  g_edits.clear();
  if (bAny)
    PortHudReload();
}

void HudTabRestore(void) {
  // Classic, and every pick of the designs back to their own
  Pick("design", "");
  Pick("layout", "");
  Pick("size", "");
  Pick("material", "");
  Pick("sound", "");
  Pick("tooltips", "");
  Pick("mapRaise", "");
  for (const PortHudDesignInfo &d : g_designs) {
    Pick(d.id + ".light", "");
    Pick(d.id + ".selection", "");
    auto it = g_loaded.find(d.id);
    if (it != g_loaded.end() && it->second.pDesign)
      for (const HudOption &o : it->second.pDesign->options)
        Pick(d.id + ".option." + o.id, "");
    if (it != g_loaded.end() && it->second.pReplay)
      for (const HudOption &o : it->second.pReplay->options)
        Pick(d.id + ".replay.option." + o.id, "");
    for (const auto &h : kHideable)
      Pick(d.id + ".hide." + HudSlotName(h.nSlot), "");
  }
}

void HudTab(const std::vector<int> &rows, void (*pfnRow)(int)) {
  const bool bGpu = PortPresentGpu();
  Decl body;
  body.d.layout.sizing.width = Grow();
  body.d.layout.sizing.height = Grow();
  body.d.layout.childGap = Gap(Fs(34));
  Open(body);

  // the designs, a card each; the wheel scrolls them
  const int kShown = 5;
  Decl list;
  list.d.id = Id("hudlist");
  list.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  list.d.layout.sizing.width = Fixed(Sc(370));
  list.d.layout.childGap = Gap(Fs(12));
  Open(list);
  const int nDesigns = static_cast<int>(g_designs.size());
  g_nFirst = SDL_max(0, SDL_min(g_nFirst + WheelRows(Over(Id("hudlist")), 1), nDesigns - kShown));
  const std::string chosen = Value("design", "classic");
  for (int k = g_nFirst; k < nDesigns && k < g_nFirst + kShown; ++k) {
    const PortHudDesignInfo &d = g_designs[k];
    const bool bLive = (bGpu || d.id == "classic") && !d.bHeld;
    if (d.id != "classic")
      Load(d.id);  // its plan
    const Clay_ElementId cid = Id("hudcard", k);
    const bool bOn = d.id == chosen, bHot = bLive && Over(cid);
    if (bLive && Clicked(cid) && !bOn)
      Pick("design", d.id == "classic" ? "" : d.id);
    Decl card;
    card.d.id = cid;
    card.d.layout.sizing.width = Grow();
    card.d.layout.sizing.height = Fixed(Sc(132));
    card.d.layout.padding = Pad(Fs(12), Fs(12), Fs(12), Fs(12));
    card.d.layout.childGap = Gap(Fs(14));
    card.d.custom.customData = Custom(DrawScRaised, bOn ? kScTabOn : bHot ? kScTabHi : kScTab);
    Open(card);
    Decl plan;
    plan.d.layout.sizing.height = Grow();
    plan.d.layout.sizing.width = Fixed(Sc(104) * SDL_min(Aspect(), 2.4f));
    plan.d.custom.customData = Custom(DrawPlan, kScButton, bOn ? 1.0f : 0.0f, k);
    Open(plan);
    Close();
    Decl words;
    words.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    words.d.layout.sizing.width = Grow();
    words.d.layout.childGap = Gap(Fs(4));
    Open(words);
    Text(Keep(Elide(d.name, UI_FACE_WIDE, Fs(16), Sc(150))), UI_FACE_WIDE, Fs(16), 0.02f,
         !bLive ? kScInkOff : bOn || bHot ? kScInkHi : kScInkButton);
    Text(d.bHeld ? PortText("Not available yet") : d.bBuiltIn ? "Hyperion" : PortText("Yours"), UI_FACE_TEXT, Fs(13),
         0.0f, kScInkOff);
    Close();
    Close();
  }
  if (nDesigns > kShown)
    // {0} and {1} the designs shown, {2} how many there are
    Note(PortTextF("{0}\xE2\x80\x93{1} of {2}. The wheel shows the rest.",
                   {std::to_string(g_nFirst + 1), std::to_string(SDL_min(nDesigns, g_nFirst + kShown)),
                    std::to_string(nDesigns)}),
         kScInkOff);
  // {0} the folder they go in
  Note(PortTextF("Your own designs go in {0}, a folder or a .hud package each.",
                 {std::string(PortDataFolder()) + "huds\\"}),
       kScInkOff);
  if (!bGpu)
    Note(PortText("The designs need the GPU renderer: Graphics, Renderer."), kScInkMsg);
  Close();

  // the picture over the chosen design's settings
  Decl right;
  right.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  right.d.layout.sizing.width = Grow();
  right.d.layout.sizing.height = Grow();
  right.d.layout.childGap = Gap(Fs(12));
  Open(right);
  Preview();
  Pane(rows, pfnRow);
  Close();
  Close();
  const Clay_ElementData in = Clay_GetElementData(Id("hudpanerows")), out = Clay_GetElementData(Id("hudpane"));
  g_fPaneMax = in.found && out.found && in.boundingBox.height > out.boundingBox.height
                   ? in.boundingBox.height - out.boundingBox.height
                   : 0.0f;
}

}  // namespace Front
