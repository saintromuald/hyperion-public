// port_front_mutators.cpp -- the skirmish set-up's Mutators tab (port_front_mutators.h).
//
// The design's take A2 (lobby.js mutBrief, mutators.js STILL): its sizes in SC2's units, its
// colours the kit's, and the resources' and the harder choices' colours of their own, which no
// theme changes. The pictures are drawn in a space 240 high and as wide as the picture's shape,
// as the design draws them, in the console's wireframes (wirefram.grp) lit in one hue: Classic's
// blue, the option blue, and off Classic Hyperion's gold, or the dark red for a harder choice.
// The command card's icons and the console's resource icons come in their own colours. The art
// is the player's own, read from their files as the tab is first shown.
#define NOMINMAX
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <SDL3/SDL.h>
#include "port_front_mutators.h"
#include "port_frontkit.h"
#include "port_frontlobby.h"  // MapPicture, MapWalkable, the set-up's pieces
#include "port_ui.h"
#include "../../h/Storm.h"    // SFileLoadFile
#include "../../h/grp.h"      // FRAME
#include "../../h/mutators.h"
#include "../data/port_stormapi.h"     // SFileUnloadFile, PortBmpDecodePcx
#include "../gameplay/port_mutators.h"
#include "../gameplay/port_richbases.h"  // RichBasesPreview
#include "../render/port_hwcursor.h"     // PortGrpFrameDecode

namespace FrontKit {
namespace {

// the colours no theme changes (mutators.css --mu-res-*, --mu-hard*), and Classic's blue
// (--lb-classic)
constexpr UiColor kClassicInk = KitRgb(0x9eccf3);
constexpr UiColor kAu = KitRgb(0xffd24a);
constexpr UiColor kAuLo = KitRgb(0xa8740c);
constexpr UiColor kVi = KitRgb(0xc58bff);
constexpr UiColor kCry = KitRgb(0x4ec8ff);
constexpr UiColor kCryLo = KitRgb(0x145f9c);
constexpr UiColor kGasInk = KitRgb(0x41d36e);
constexpr UiColor kHard = KitRgb(0xcc4136);
constexpr UiColor kHardLo = KitRgb(0x5e1410);
constexpr UiColor kHardRim = KitRgb(0xcc4136, 0.6f);
constexpr UiColor kBlack = KitRgb(0x000000);
constexpr UiColor kWhite = KitRgb(0xffffff);

UiColor Alpha(UiColor c, float a)
{
  c.a *= a;
  return c;
}

// The list's order and its groups (mutators.js GROUPS), and what each one does in a sentence
// over its picture (gist).
const int kOrder[MUT_COUNT] = {MUT_MINERALS, MUT_GAS,   MUT_RICH, MUT_START, MUT_WORKERS,
                               MUT_SUPPLY,   MUT_BOOST, MUT_FOG,  MUT_BUILD, MUT_TECH};
struct Group {
  const char *pszName;
  int nFirst, nCount;  // in kOrder
};
const Group kGroups[] = {{PT_("Resources"), 0, 3},
                         {PT_("The start"), 3, 2},
                         {PT_("Supply"), 5, 2},
                         {PT_("Fog, time and tech"), 7, 3}};
const char *const kGists[MUT_COUNT] = {
    PT_("How much each mineral field holds, so how long a base can be mined."),
    PT_("How much each geyser holds before it runs dry."),
    PT_("What each player has to spend at the first second."),
    PT_("The biggest army each player can keep."),
    PT_("How many workers each player starts with."),
    PT_("More supply from each depot, pylon, overlord and town hall, so fewer are needed."),
    PT_("How much of the map each player sees."),
    PT_("How fast everything is built, trained and researched."),
    PT_("Start with everything researched, or play without upgrades or research."),
    PT_("Some bases away from the starts get gold minerals and violet gas, which bring more each trip."),
};

int g_nSel = MUT_MINERALS;  // the mutator briefed

// ---------------------------------------------------------------------------
// The art: a sheet of 64-pixel cells in the picture atlas's slot 41 (port_ui.h). The wireframes
// are white, their parts' shading in the brightness, for the picture to light in a hue.

const int kArtSlot = 41, kFogSlot = 42, kCell = 64, kAcross = UI_PICTURE_SIDE / kCell;
enum {
  ART_MARINE, ART_SCV, ART_ULTRA, ART_OVERLORD, ART_ZEALOT, ART_CC, ART_DEPOT, ART_HATCH, ART_NEXUS, ART_PYLON,
  ART_FIELD, ART_GEYSER, ART_WIRES,
  ART_TECH = ART_WIRES,  // six command icons
  ART_RES = ART_TECH + 6,  // the console's minerals, gas and supply
  ART_COUNT = ART_RES + 3
};
const int kWireType[ART_WIRES] = {0, 7, 39, 42, 65, 106, 109, 131, 154, 156, 176, 188};
// three upgrades and three researches (mutators.js TECH): cmdicons.grp's frames
const int kTechIcon[6] = {288, 292, 238, 237, 240, 245};
const int kTechLevels[6] = {3, 3, 1, 0, 0, 0};
// icons.grp: minerals, Terran gas, Terran supply
const int kResIcon[3] = {0, 2, 5};

struct Art {
  bool bTried = false, bAny = false;
  std::vector<unsigned char> rgba;
  int anW[ART_COUNT] = {}, anH[ART_COUNT] = {};  // each cell's picture from its corner; 0 none
};
Art g_art;

// A GRP's frame into the sheet's cell: a wireframe's shades as white (nMode 0), a command icon
// through ticon.pcx's first row and the palette (1), or the palette's colours (2).
void PutFrame(const BYTE *p, DWORD cb, int nFrame, int nCell, int nMode, const unsigned char *pPal,
              const unsigned char *pTicon)
{
  const int nFrames = cb >= 6 ? p[0] | p[1] << 8 : 0;
  const size_t at = 6 + static_cast<size_t>(nFrame) * 8;
  if (nFrame >= nFrames || at + 8 > cb)
    return;
  DWORD dwOff;
  memcpy(&dwOff, p + at + 4, sizeof dwOff);
  dwOff &= 0x7FFFFFFFu;
  FRAME f;
  memset(&f, 0, sizeof f);
  f.bWidth = p[at + 2];
  f.bHeight = p[at + 3];
  f.pData = const_cast<BYTE *>(p + dwOff);
  if (f.bWidth == 0 || f.bHeight == 0 || f.bWidth > kCell || f.bHeight > kCell || dwOff + 2u * f.bHeight > cb)
    return;
  // a frame of runs starts with its first row's offset, past the rows' table; a GRP of whole
  // frames, as game\icons.grp, with its pixels, 0 clear (port_gpu_hud.cpp's asset art)
  const size_t nPx = static_cast<size_t>(f.bWidth) * f.bHeight;
  std::vector<int> px;
  if ((p[dwOff] | p[dwOff + 1] << 8) == 2 * f.bHeight) {
    PortGrpFrameDecode(f, &px);
  } else if (dwOff + nPx <= cb) {
    px.resize(nPx);
    for (size_t i = 0; i < nPx; ++i)
      px[i] = p[dwOff + i] != 0 ? p[dwOff + i] : -1;
  }
  if (px.size() < nPx)
    return;
  const int x0 = nCell % kAcross * kCell, y0 = nCell / kAcross * kCell;
  for (int y = 0; y < f.bHeight; ++y)
    for (int x = 0; x < f.bWidth; ++x) {
      int v = px[static_cast<size_t>(y) * f.bWidth + x];
      if (v < 0)
        continue;
      unsigned char *q = &g_art.rgba[(static_cast<size_t>(y0 + y) * UI_PICTURE_SIDE + x0 + x) * 4];
      if (nMode == 0) {
        // the four shades of a part, lightest first (PortWireframeCell)
        q[0] = q[1] = q[2] = static_cast<unsigned char>(255 - (v & 3) * 40);
      } else {
        if (nMode == 1 && v < 16)
          v = pTicon[v];
        memcpy(q, pPal + v * 4, 3);
      }
      q[3] = 255;
    }
  g_art.anW[nCell] = f.bWidth;
  g_art.anH[nCell] = f.bHeight;
}

bool LoadFile(const char *pszName, void **pp, DWORD *pcb)
{
  *pp = NULL;
  *pcb = 0;
  return SFileLoadFile(pszName, pp, pcb, 0, NULL) && *pp != NULL;
}

void EnsureArt(void)
{
  Art &a = g_art;
  if (a.bTried)
    return;
  a.bTried = true;
  a.rgba.assign(static_cast<size_t>(UI_PICTURE_SIDE) * UI_PICTURE_SIDE * 4, 0);
  // the game's palette, a tileset's (the console's colours are the same in every one), and the
  // command buttons' remap
  unsigned char pal[1024] = {};
  bool bPal = false;
  unsigned char ticon[16];
  for (int i = 0; i < 16; ++i)
    ticon[i] = static_cast<unsigned char>(i);
  void *p = NULL;
  DWORD cb = 0;
  if (LoadFile("tileset\\badlands.wpe", &p, &cb)) {
    if (cb >= sizeof pal) {
      memcpy(pal, p, sizeof pal);
      bPal = true;
    }
    SFileUnloadFile(p);
  }
  if (LoadFile("unit\\cmdbtns\\ticon.pcx", &p, &cb)) {
    unsigned char *pPixels = NULL;
    int nW = 0, nH = 0;
    if (PortBmpDecodePcx(p, cb, &pPixels, &nW, &nH)) {
      if (nW >= 16 && nH >= 1)
        memcpy(ticon, pPixels, 16);
      SMemFree(pPixels, "port_front_mutators.cpp", __LINE__, 0);
    }
    SFileUnloadFile(p);
  }
  if (LoadFile("unit\\wirefram\\wirefram.grp", &p, &cb)) {
    for (int i = 0; i < ART_WIRES; ++i)
      PutFrame(static_cast<const BYTE *>(p), cb, kWireType[i], i, 0, pal, ticon);
    SFileUnloadFile(p);
  }
  if (bPal && LoadFile("unit\\cmdbtns\\cmdicons.grp", &p, &cb)) {
    for (int i = 0; i < 6; ++i)
      PutFrame(static_cast<const BYTE *>(p), cb, kTechIcon[i], ART_TECH + i, 1, pal, ticon);
    SFileUnloadFile(p);
  }
  if (bPal && LoadFile("game\\icons.grp", &p, &cb)) {
    for (int i = 0; i < 3; ++i)
      PutFrame(static_cast<const BYTE *>(p), cb, kResIcon[i], ART_RES + i, 2, pal, ticon);
    SFileUnloadFile(p);
  }
  for (int i = 0; i < ART_COUNT; ++i)
    a.bAny = a.bAny || a.anW[i] != 0;
  if (a.bAny)
    UiPictureSet(kArtSlot, a.rgba.data(), UI_PICTURE_SIDE, UI_PICTURE_SIDE);
}

// The fog's two masks in slot 42, Classic's (the way a scout came, dimmed, and the sight round
// the base and the scout) and Explored's (the land dimmed, the sight clear), each 192 by 148, as
// fogStill lays the fog over the map's picture.
const int kFogW = 192, kFogH = 148;

void EnsureFog(void)
{
  static bool s_bMade;
  if (s_bMade)
    return;
  s_bMade = true;
  std::vector<unsigned char> rgba(static_cast<size_t>(2 * kFogW) * kFogH * 4, 0);
  // in the zone's own shape, its height 1
  const float zw = static_cast<float>(kFogW) / kFogH;
  const float ox = zw * 0.22f, oy = 0.92f - 0.1f, sx = zw * 0.52f, sy = 0.56f;
  for (int nMask = 0; nMask < 2; ++nMask)
    for (int y = 0; y < kFogH; ++y)
      for (int x = 0; x < kFogW; ++x) {
        const float u = (x + 0.5f) / kFogH, v = (y + 0.5f) / kFogH;
        float a = 1.0f;
        if (nMask == 1) {
          a *= 0.45f;
        } else {
          // the way the scout came: a stroke a quarter of the zone wide
          const float dx = sx - ox, dy = sy - oy, t = fmaxf(0.0f, fminf(1.0f, ((u - ox) * dx + (v - oy) * dy) / (dx * dx + dy * dy)));
          const float ex = u - (ox + dx * t), ey = v - (oy + dy * t);
          if (sqrtf(ex * ex + ey * ey) <= zw * 0.12f)
            a *= 0.5f;
        }
        const float sight[2][3] = {{ox, oy, zw * 0.3f}, {sx, sy, zw * 0.22f}};
        for (const auto &c : sight) {
          const float d = sqrtf((u - c[0]) * (u - c[0]) + (v - c[1]) * (v - c[1]));
          const float k = d <= c[2] * 0.6f ? 1.0f : d >= c[2] ? 0.0f : (c[2] - d) / (c[2] * 0.4f);
          a *= 1.0f - k;
        }
        unsigned char *q = &rgba[(static_cast<size_t>(y) * 2 * kFogW + nMask * kFogW + x) * 4];
        q[3] = static_cast<unsigned char>(a * 255.0f + 0.5f);
      }
  UiPictureSet(kFogSlot, rgba.data(), 2 * kFogW, kFogH);
}

// ---------------------------------------------------------------------------
// The briefing's context: the set as the picture shows it (a choice pointed at taken), and what
// it reads from the set-up.

struct Shown {
  MutatorSet m;
  int nMut = 0;
  const MapPicture *pMap = NULL;
  std::string title, speed;
  bool bMapUnits = false;
};
Shown g_shown;

// The map's bases for rich bases as a choice reads them (RichBasesPreview), kept a map at a time:
// NULL when the map is not read, or not yet. A read runs on a thread of its own, since on a large
// map it takes seconds (half of one on a 384-tile map of sixteen starts, more on Big Game Hunters
// 8x8's 64), and the set-up does not wait for it (the user, 2026-10-26: it hung while the pointer
// crossed the list). The browser's build has no threads, and reads in turn.
struct RichRead {
  std::string of;
  bool bWalk = false;
  std::vector<unsigned char> walk;
  std::map<std::string, std::pair<std::string, std::vector<RichBasePreview>>> by;  // by the choice
};
RichRead g_rich;

// The read under way, one at a time: its map and choice, and once done what it found. Never
// freed, as its thread may outlive the screen.
struct RichJob {
  std::mutex lock;
  bool bBusy = false, bDone = false;
  std::string of, key;
  std::pair<std::string, std::vector<RichBasePreview>> read;
};
RichJob &Job()
{
  static RichJob *const s_pJob = new RichJob;
  return *s_pJob;
}

bool g_bRichReading;  // the last RichOf waits on a read
bool g_bMeasuring;    // the effect's lines are measured: a read not made is not started

const std::vector<RichBasePreview> *RichOf(const MapPicture *p, const MutatorSet &m, std::string *pWhy)
{
  pWhy->clear();
  g_bRichReading = false;
  if (p == NULL || p->w <= 0 || p->starts.empty() || m.nRich == MUTATOR_RICH_OFF)
    return NULL;
  if (g_rich.of != p->of) {
    g_rich = RichRead();
    g_rich.of = p->of;
    g_rich.bWalk = MapWalkable(*p, &g_rich.walk);
  }
  if (!g_rich.bWalk)
    return NULL;
  // what the read takes: where, the mineral fields' share (which makes walls), and for a roll its
  // chance, rules and radius
  const bool bRolled = m.nRich != MUTATOR_RICH_EVERY;
  const std::string nKey = std::to_string(m.nRich) + "/" + std::to_string(m.nMinerals) + "/" +
                           (bRolled ? std::to_string(m.nRichChance) + "/" + std::to_string(m.nRichRules) + "/" +
                                          std::to_string(m.nRichRadius)
                                    : std::string());
  auto it = g_rich.by.find(nKey);
  if (it == g_rich.by.end()) {
    RichJob &job = Job();
    std::lock_guard<std::mutex> hold(job.lock);
    if (job.bDone) {
      if (job.of == g_rich.of)
        g_rich.by.emplace(job.key, std::move(job.read));
      job.bDone = false;
    }
    it = g_rich.by.find(nKey);
  }
  if (it == g_rich.by.end()) {
    // measuring: the longest line, as many bases as a map holds
    static const std::vector<RichBasePreview> s_many(99, RichBasePreview{0, 0, 2, 1000});
    if (g_bMeasuring)
      return &s_many;
    std::vector<std::pair<int, int>> starts;
    for (const MapStart &s : p->starts)
      starts.push_back(std::make_pair(s.x, s.y));
    std::vector<RichBaseResource> res;
    for (const MapResource &r : p->resources) {
      const RichBaseResource one = {r.x, r.y, r.bGas, r.wAmount};
      res.push_back(one);
    }
#if defined(__EMSCRIPTEN__)
    std::pair<std::string, std::vector<RichBasePreview>> read;
    read.first = RichBasesPreview(p->w, p->h, g_rich.walk, starts, res, m, &read.second);
    it = g_rich.by.emplace(nKey, read).first;
#else
    RichJob &job = Job();
    std::lock_guard<std::mutex> hold(job.lock);
    if (!job.bBusy) {
      job.bBusy = true;
      job.of = g_rich.of;
      job.key = nKey;
      std::thread([w = p->w, h = p->h, walk = g_rich.walk, starts, res, m]() {
        std::pair<std::string, std::vector<RichBasePreview>> read;
        read.first = RichBasesPreview(w, h, walk, starts, res, m, &read.second);
        RichJob &done = Job();
        std::lock_guard<std::mutex> hold(done.lock);
        done.read = std::move(read);
        done.bDone = true;
        done.bBusy = false;
      }).detach();
    }
    g_bRichReading = true;
    return NULL;
#endif
  }
  *pWhy = it->second.first;
  return &it->second.second;
}

int RichMay(const std::vector<RichBasePreview> &bases)
{
  int n = 0;
  for (const RichBasePreview &b : bases)
    n += b.nChance > 0 ? 1 : 0;
  return n;
}

// ---------------------------------------------------------------------------
// What a choice does, in words (mutators.js effect).

// a share of 1.17's, in percent: how much longer it lasts or takes
std::string Times(int nPercent)
{
  switch (nPercent) {
  case 25: return PortText("a quarter as long");
  case 50: return PortText("half as long");
  case 75: return PortText("three quarters as long");
  case 100: return PortText("as long as Classic");
  case 150: return PortText("half again as long");
  case 200: return PortText("twice as long");
  case 300: return PortText("three times as long");
  case 400: return PortText("four times as long");
  case 800: return PortText("eight times as long");
  }
  char sz[32];
  SDL_snprintf(sz, sizeof sz, "%g", nPercent / 100.0);
  // {0} how many times as long, as a number
  return PortTextF("\xc3\x97{0} as long", {sz});
}

// 1.17's frame at each speed, in milliseconds, and an SCV's training in frames
int FrameMs(const std::string &speed)
{
  static const char *const kNames[] = {"Slowest", "Slower", "Slow", "Normal", "Fast", "Faster", "Fastest"};
  static const int kMs[] = {167, 111, 83, 67, 56, 48, 42};
  for (int i = 0; i < 7; ++i)
    if (speed == kNames[i])
      return kMs[i];
  return 42;
}
const int kScvFrames = 300;

std::string Seconds(int nFrames, const std::string &speed)
{
  char sz[32];
  SDL_snprintf(sz, sizeof sz, "%.1f", nFrames * FrameMs(speed) / 1000.0);
  return sz;
}

int Boosted(int n, int nBoost)
{
  return n * (100 + nBoost) / 100;
}

std::string Effect(const MutatorSet &m, int nMut)
{
  const std::string title = g_shown.title.empty() ? std::string(PortText("this map")) : g_shown.title;
  switch (nMut) {
  case MUT_MINERALS:
    if (m.nMinerals == 0)
      return PortText("Fields never run dry: a base can be mined all game.");
    // {0} a field's amount, {1} a small field's, {2} how long a base lasts
    return PortTextF("A field holds {0}, the small ones {1}: a base lasts {2}.",
                     {Thousands(1500LL * m.nMinerals / 100), Thousands(750LL * m.nMinerals / 100),
                      Times(m.nMinerals)});
  case MUT_GAS:
    if (m.nGas == 0)
      return PortText("Geysers never run dry.");
    return PortTextF("A geyser holds {0} before it runs dry, then gives 2 a trip. It lasts {1}.",
                     {Thousands(5000LL * m.nGas / 100), Times(m.nGas)});
  case MUT_RICH: {
    if (m.nRich == MUTATOR_RICH_OFF)
      return PortText("Every base as the map made it.");
    if (m.nRich == MUTATOR_RICH_EVERY)
      // {0} the map's name
      return PortTextF("Every mineral field and geyser on {0} is rich: a trip brings 11 minerals or 16 gas, Classic 8.",
                       {title});
    // how far a base must be, with the tiles a share of this map makes
    std::string radius = MutatorRichRadiusName(m);
    if (m.nRichRadius < 0 && g_shown.pMap != NULL && g_shown.pMap->w > 0)
      // {0} the distance, {1} how many tiles it is on this map
      radius = PortTextF("{0} ({1} tiles)",
                         {radius, std::to_string(MutatorRichRadiusTiles(m, g_shown.pMap->w, g_shown.pMap->h))});
    // which bases may turn rich, as the sentences below read it
    std::string where = m.nRich == MUTATOR_RICH_NATURALS
                            ? std::string(PortText("that is no start\xe2\x80\x99s main or natural"))
                        : m.nRich == MUTATOR_RICH_RADIUS ? PortTextF("{0} or more from every start", {radius})
                                                         : std::string(PortText("farthest from every start"));
    std::string why;
    const std::vector<RichBasePreview> *pBases = RichOf(g_shown.pMap, m, &why);
    if (pBases == NULL)
      // {0} which bases, {1} the chance in percent
      return g_bRichReading
                 ? PortTextF("Any base {0} may turn rich, at {1}%. Reading this map\xe2\x80\x99s bases\xe2\x80\xa6",
                             {where, std::to_string(m.nRichChance)})
                 : PortTextF("Any base {0} may turn rich, at {1}%. Read from the map as the game starts.",
                             {where, std::to_string(m.nRichChance)});
    if (!why.empty())
      // {0} why none can
      return PortTextF("{0} No base turns rich, so no player starts ahead.", {why});
    const int n = RichMay(*pBases);
    if (n != 1 && m.nRich == MUTATOR_RICH_NATURALS)
      where = PortText("that are no start\xe2\x80\x99s main or natural");
    // {0} how many bases, {1} which ones, {2} the map's name, {3} the chance in percent
    return PortTextNF("Up to {0} base {1} on {2} may turn rich, each at {3}%: a trip brings 11 minerals or 16 gas, "
                      "Classic 8.",
                      "Up to {0} bases {1} on {2} may turn rich, each at {3}%: a trip brings 11 minerals or 16 gas, "
                      "Classic 8.",
                      n, {std::to_string(n), where, title, std::to_string(m.nRichChance)});
  }
  case MUT_START:
    // {0} the minerals, {1} the gas, {2} how many workers they would buy
    return PortTextNF("Each player begins with {0} minerals and {1} gas: {2} worker\xe2\x80\x99s worth.",
                      "Each player begins with {0} minerals and {1} gas: {2} workers\xe2\x80\x99 worth.",
                      m.nStartMinerals / 50,
                      {Thousands(m.nStartMinerals), Thousands(m.nStartGas), Thousands(m.nStartMinerals / 50)});
  case MUT_WORKERS: {
    std::string line = PortTextNF("Each player begins with {0} worker beside the town hall.",
                                  "Each player begins with {0} workers beside the town hall.", m.nWorkers,
                                  {std::to_string(m.nWorkers)});
    if (g_shown.bMapUnits && m.nWorkers != MutatorSet().nWorkers)
      line += std::string(" ") + PortText("Not on this game: Use Map Settings keeps the map\xe2\x80\x99s own start units.");
    return line;
  }
  case MUT_SUPPLY:
    if (m.nSupplyCap == 0)
      return PortText("No cap: an army is held back only by the supply built.");
    return PortTextF("Each player can hold {0} supply.", {Thousands(m.nSupplyCap)});
  case MUT_BOOST: {
    const int b = m.nSupplyBoost;
    // {0} a depot's supply, {1} a command center's, {2} a nexus's, {3} a hatchery's
    const std::string depot = std::to_string(Boosted(8, b)), center = std::to_string(Boosted(10, b)),
                      nexus = std::to_string(Boosted(9, b)), hatchery = std::to_string(Boosted(1, b));
    return b ? PortTextF("A depot, pylon or overlord gives {0}, a command center {1}, a nexus {2}, a hatchery {3}: "
                         "whole supply, rounded down.",
                         {depot, center, nexus, hatchery})
             : PortTextF("A depot, pylon or overlord gives {0}, a command center {1}, a nexus {2}, a hatchery {3}, "
                         "as Classic.",
                         {depot, center, nexus, hatchery});
  }
  case MUT_FOG:
    if (m.nFog == MUTATOR_FOG_EXPLORED)
      return PortText("Every player knows the map\xe2\x80\x99s land from the start. Units still show only in sight.");
    if (m.nFog == MUTATOR_FOG_NONE)
      return PortText("Every player sees the whole map all game.");
    return PortText("The fog of war, as Classic plays it.");
  case MUT_BUILD: {
    const std::string sp = PortText(g_shown.speed.c_str());  // the game's speed, named
    if (m.nBuildTime == 100)
      // {0} the seconds, {1} the game's speed
      return PortTextF("As Classic: an SCV trains in {0} s at {1}.", {Seconds(kScvFrames, g_shown.speed), sp});
    // {0} how much longer, {1} the seconds, {2} the game's speed, {3} Classic's seconds
    return PortTextF("Everything takes {0}: an SCV trains in {1} s at {2}, Classic {3} s.",
                     {Times(m.nBuildTime), Seconds(kScvFrames * m.nBuildTime / 100, g_shown.speed), sp,
                      Seconds(kScvFrames, g_shown.speed)});
  }
  case MUT_TECH:
    if (m.nTech == MUTATOR_TECH_ALL)
      return PortText("Every upgrade at its top level and every ability researched from the start.");
    if (m.nTech == MUTATOR_TECH_NO_UPGRADES)
      return PortText("Upgrades cannot be researched. Abilities still can.");
    if (m.nTech == MUTATOR_TECH_NO_RESEARCH)
      return PortText("Neither upgrades nor the abilities that need research.");
    return PortText("Everything to research, as Classic.");
  }
  return MutatorLine(nMut);
}

// ---------------------------------------------------------------------------
// The pictures (mutators.js STILL), in a space 240 high.

const float kPicH = 240.0f, kPicMinW = 340.0f;

struct Paint {
  float x0, y0, s;  // the picture's corner in pixels, and pixels a unit of its space
  float W, H;       // its space
  bool bHard;       // a harder choice: the dark red where the gold would be
  UiColor gold, au, auLo, goldRim;
  float X(float x) const { return x0 + x * s; }
  float Y(float y) const { return y0 + y * s; }
};

void Fill(const Paint &p, float x, float y, float w, float h, UiColor c, float r = 0.0f)
{
  UiFill(p.X(x), p.Y(y), w * p.s, h * p.s, c, r * p.s);
}

void Ring(const Paint &p, float x, float y, float w, float h, UiColor c, float r, float lw)
{
  UiRing(p.X(x), p.Y(y), w * p.s, h * p.s, c, r * p.s, fmaxf(1.0f, lw * p.s));
}

void Dot(const Paint &p, float cx, float cy, float r, UiColor c)
{
  Fill(p, cx - r, cy - r, 2 * r, 2 * r, c, r);
}

void DotRing(const Paint &p, float cx, float cy, float r, UiColor c, float lw)
{
  Ring(p, cx - r, cy - r, 2 * r, 2 * r, c, r, lw);
}

void Line(const Paint &p, float x0, float y0, float x1, float y1, UiColor c, float lw)
{
  UiLine(p.X(x0), p.Y(y0), p.X(x1), p.Y(y1), c, fmaxf(1.0f, lw * p.s));
}

// text, its middle at y; nAlign 0 left, 1 centred, 2 right; no text under 10 pixels
float TextPx(const Paint &p, float fSize)
{
  return fmaxf(fSize * p.s, UiPx(10.0f));
}

float Width(const Paint &p, const std::string &s, float fSize, UiFace face = UI_FACE_TEXT)
{
  return UiTextWidth(face, TextPx(p, fSize), 0.0f, s.c_str()) / p.s;
}

void Say(const Paint &p, const std::string &s, float x, float y, float fSize, UiColor c, int nAlign = 0,
         UiFace face = UI_FACE_TEXT, float fSpacing = 0.0f)
{
  const float px = TextPx(p, fSize), sp = fSpacing * p.s;
  float tx = p.X(x);
  if (nAlign != 0) {
    const float tw = UiTextWidth(face, px, sp, s.c_str());
    tx -= nAlign == 1 ? tw / 2 : tw;
  }
  UiText(tx, p.Y(y) + px * 0.36f, face, px, sp, c, s.c_str());
}

// a label in the wide face's capitals
void Caps(const Paint &p, const std::string &s, float x, float y, UiColor c, int nAlign = 0)
{
  Say(p, Upper(s.c_str()), x, y, 10.5f, c, nAlign, UI_FACE_WIDE, 1.0f);
}

// a piece of art in a w by h box, the middle of its foot at (x, y), times c; with fCut below 1,
// only that share of it from the left, the rest a ghost. A faint frame where this machine lacks it
bool ArtAt(const Paint &p, int nArt, float x, float y, float w, float h, UiColor c, float fCut = 1.0f)
{
  if (!g_art.bAny || g_art.anW[nArt] == 0) {
    Ring(p, x - w * 0.3f, y - h * 0.6f, w * 0.6f, h * 0.6f, Alpha(kLbDivider, 0.5f), 3, 1);
    return false;
  }
  const float cw = static_cast<float>(g_art.anW[nArt]), ch = static_cast<float>(g_art.anH[nArt]);
  const float k = fminf(w / cw, h / ch), dw = cw * k, dh = ch * k, dx = x - dw / 2, dy = y - dh;
  const float u0 = static_cast<float>(nArt % kAcross * kCell), v0 = static_cast<float>(nArt / kAcross * kCell);
  // 1.17's pixels stay pixels when grown
  const bool bNearest = k * p.s >= 1.2f;
  if (fCut < 1.0f) {
    UiPicturePart(p.X(dx), p.Y(dy), dw * p.s, dh * p.s, kArtSlot, u0, v0, u0 + cw, v0 + ch, Alpha(kClassicInk, 0.2f),
                  bNearest);
    UiPushClip(p.X(dx - 4), p.Y(dy - 4), p.X(dx + dw * fCut), p.Y(dy + dh + 4));
  }
  UiPicturePart(p.X(dx), p.Y(dy), dw * p.s, dh * p.s, kArtSlot, u0, v0, u0 + cw, v0 + ch, c, bNearest);
  if (fCut < 1.0f)
    UiPopClip();
  return true;
}

// a wireframe's hue: Classic's half, this game's (gold when its choice is off Classic), or a
// ghost of what this game does not have
enum { SIDE_CLASSIC, SIDE_GAME, SIDE_GHOST };
UiColor Side(const Paint &p, int nSide, bool bLit)
{
  return nSide == SIDE_CLASSIC ? kClassicInk : nSide == SIDE_GHOST ? Alpha(kClassicInk, 0.22f) : bLit ? p.au : kScInkLabel;
}

// a console icon (n: 0 minerals, 1 gas, 2 supply), size s, its middle at (x, y)
void ResIcon(const Paint &p, int n, float x, float y, float s)
{
  if (ArtAt(p, ART_RES + n, x, y + s / 2, s, s, kWhite))
    return;
  if (n == 0)
    Dot(p, x, y, s * 0.3f, kCry);
  else
    Dot(p, x, y, s * 0.3f, n == 1 ? kGasInk : kLbInkDesc);
}

void Bar(const Paint &p, float x, float y, float w, float h, float f, UiColor hi, UiColor lo)
{
  Fill(p, x, y, w, h, kScWell, h / 2);
  Ring(p, x, y, w, h, kLbDivider, h / 2, 1);
  if (f <= 0.0f)
    return;
  const float fw = fmaxf(h, w * fminf(1.0f, f));
  UiGradient(p.X(x), p.Y(y), fw * p.s, h * p.s, hi, lo, false, h / 2 * p.s);
}

// Classic's half and this game's: two wells, each named at its top, this game's ruled in gold
// when its choice is off Classic, or named Harder
struct Half {
  float x, y, w, h;
};
void Halves(const Paint &p, bool bOn, Half out[2])
{
  const float m = 8, gap = 8, w = (p.W - m * 2 - gap) / 2, h = p.H - m * 2;
  for (int i = 0; i < 2; ++i) {
    Half &b = out[i];
    b = {m + i * (w + gap), m, w, h};
    Fill(p, b.x, b.y, b.w, b.h, i ? kLbItem : UiColor{0.0f, 0.0f, 0.0f, 0.32f}, 5);
    Ring(p, b.x, b.y, b.w, b.h, i && bOn ? p.goldRim : kLbDivider, 5, 1.2f);
    Caps(p, i ? (p.bHard ? PortText("Harder") : PortText("This game")) : PortText("Classic"), b.x + 10, b.y + 15,
         i ? (bOn ? p.gold : kLbInkDesc) : kScInkOff);
  }
}

// a half's figure, low in it, with a line or two of small print under it
void Figure(const Paint &p, const Half &b, const std::string &big, const std::vector<std::string> &notes, int i,
            bool bOn, float fSize = 24)
{
  Say(p, big, b.x + b.w / 2, b.y + b.h - 50, fSize, i ? (bOn ? p.gold : kLbInkInfo) : kLbInkDesc, 1, UI_FACE_BOLD);
  for (size_t k = 0; k < notes.size(); ++k)
    Say(p, notes[k], b.x + b.w / 2, b.y + b.h - 27 + k * 14, 12, k ? kScInkOff : kLbInkDesc, 1);
}

// Mineral fields and geysers: what one holds, as Classic ones. x2 is two fields where Classic has
// one, x1.5 one and a half, x0.5 half of one; Infinite is one. Geysers, being wide, overlap
void Pile(const Paint &p, int nArt, int nPercent, const Half &b, int nSide, bool bLit)
{
  const bool bInf = nPercent == 0;
  const int n = bInf ? 1 : (nPercent + 99) / 100;
  const float fPart = bInf || nPercent % 100 == 0 ? 1.0f : (nPercent % 100) / 100.0f;
  const int back = n > 2 ? n / 2 : 0, front = n - back;
  const float zw = b.w - 16, zy = b.y + 26, zh = b.h - 26 - 66;
  const float rowH = back ? zh / 1.5f : zh, sp = zw / fmaxf(static_cast<float>(front), 1.6f);
  const float over = nArt == ART_GEYSER ? 1.4f : 1.12f, cx = b.x + b.w / 2;
  int drawn = 0;
  const int rows[2][2] = {{back, 0}, {front, 1}};
  for (const auto &r : rows) {
    if (r[0] == 0)
      continue;
    const float y = r[1] ? zy + zh : zy + zh - rowH * 0.5f;
    for (int i = 0; i < r[0]; ++i) {
      ++drawn;
      ArtAt(p, nArt, cx + (i - (r[0] - 1) / 2.0f) * sp, y, sp * over, rowH, Side(p, nSide, bLit),
            drawn == n ? fPart : 1.0f);
    }
  }
}

void StillPile(const Paint &p, const MutatorSet &m, bool bGas)
{
  const int k = bGas ? m.nGas : m.nMinerals;
  const bool bOn = k != 100;
  const int a0 = bGas ? 5000 : 1500;
  const char *pszWhat = bGas ? PortText("a geyser") : PortText("a field");
  Half h[2];
  Halves(p, bOn, h);
  for (int i = 0; i < 2; ++i) {
    const int kk = i ? k : 100;
    const bool bInf = kk == 0;
    Pile(p, bGas ? ART_GEYSER : ART_FIELD, kk, h[i], i ? SIDE_GAME : SIDE_CLASSIC, bOn);
    std::vector<std::string> notes;
    notes.push_back(pszWhat);
    // {0} how long it lasts, or what the small fields hold
    notes.push_back(bInf      ? std::string(PortText("never runs dry"))
                    : i && bOn ? PortTextF("lasts {0}", {Times(kk)})
                    : bGas     ? std::string(PortText("then 2 a trip"))
                               : PortTextF("the small ones {0}", {Thousands(750LL * kk / 100)}));
    Figure(p, h[i], bInf ? std::string(PortText("Infinite")) : Thousands(static_cast<long long>(a0) * kk / 100), notes,
           i, bOn, bInf ? 22 : 24);
  }
}

// the map's picture's size in pixels as MinimapRgba made it
void MinimapSize(const MapPicture &m, int *pnW, int *pnH)
{
  const int nSide = std::max(m.w, m.h), nStep = (nSide + UI_PICTURE_SIDE - 1) / UI_PICTURE_SIDE;
  *pnW = (m.w + nStep - 1) / std::max(nStep, 1);
  *pnH = (m.h + nStep - 1) / std::max(nStep, 1);
}

// Rich bases' map: the bases the choice may turn rich in gold, the starts' mains as squares and
// the others as rings; a sketch of a four-start map when the map is not read. How many may.
int RichMap(const Paint &p, float mx, float my, float sz, const MutatorSet &m)
{
  Fill(p, mx - 2, my - 2, sz + 4, sz + 4, kBlack, 4);
  Ring(p, mx - 2, my - 2, sz + 4, sz + 4, kLbInnerRim, 4, 1.5f);
  const MapPicture *pMap = g_shown.pMap;
  const float ar = pMap != NULL && pMap->w > 0 && pMap->h > 0 ? static_cast<float>(pMap->w) / pMap->h : 1.0f;
  const float pw = ar >= 1 ? sz : sz * ar, ph = ar >= 1 ? sz / ar : sz, px = mx + (sz - pw) / 2, py = my + (sz - ph) / 2;
  if (pMap != NULL && pMap->bMinimap) {
    UiPicture(p.X(px), p.Y(py), pw * p.s, ph * p.s, pMap->nSlot, UiColor{0.85f, 0.85f, 0.85f, 1.0f});
  } else {
    Fill(p, px, py, pw, ph, kLbItem);
    for (int g = 1; g < 8; ++g) {
      Line(p, px + pw * g / 8, py, px + pw * g / 8, py + ph, kLbDivider, 1);
      Line(p, px, py + ph * g / 8, px + pw, py + ph * g / 8, kLbDivider, 1);
    }
  }
  struct Mark {
    float x, y;
    int nKind;
    bool bRich;
  };
  std::vector<Mark> marks;
  std::string why;
  const std::vector<RichBasePreview> *pBases = RichOf(pMap, m, &why);
  if (pBases != NULL) {
    for (const RichBasePreview &b : *pBases)
      marks.push_back({b.x / (pMap->w * 32.0f), b.y / (pMap->h * 32.0f), b.nKind, b.nChance > 0});
  } else {
    // mains, naturals, the middle and the edges' middles
    const float sketch[13][3] = {{0.12f, 0.12f, 0}, {0.88f, 0.12f, 0}, {0.12f, 0.88f, 0}, {0.88f, 0.88f, 0},
                                 {0.3f, 0.16f, 1},  {0.84f, 0.3f, 1},  {0.16f, 0.7f, 1},  {0.7f, 0.84f, 1},
                                 {0.5f, 0.5f, 3},   {0.5f, 0.12f, 2},  {0.88f, 0.5f, 2},  {0.12f, 0.5f, 2},
                                 {0.5f, 0.88f, 2}};
    for (const auto &b : sketch) {
      const int nKind = static_cast<int>(b[2]);
      const bool bRich = m.nRich == MUTATOR_RICH_EVERY || (m.nRich == MUTATOR_RICH_FAR && nKind == 3) ||
                         ((m.nRich == MUTATOR_RICH_NATURALS || m.nRich == MUTATOR_RICH_RADIUS) && nKind >= 2);
      marks.push_back({b[0], b[1], std::min(nKind, 2), m.nRich != MUTATOR_RICH_OFF && bRich});
    }
  }
  int nRich = 0;
  for (const Mark &b : marks) {
    const float x = px + b.x * pw, y = py + b.y * ph;
    if (b.bRich) {
      ++nRich;
      UiSetTurn(0.785398f, p.X(x), p.Y(y));
      Fill(p, x - 4.5f, y - 4.5f, 9, 9, kAu);
      UiSetTurn(0.0f, 0.0f, 0.0f);
      Dot(p, x + 7, y + 6, 2.6f, kVi);
    } else if (b.nKind == 0) {
      Fill(p, x - 4.5f, y - 4.5f, 9, 9, Alpha(kBlack, 0.6f));
      Ring(p, x - 4.5f, y - 4.5f, 9, 9, kLbInkDesc, 0, 1.5f);
    } else {
      const float r = b.nKind == 1 ? 4.0f : 3.5f;
      Dot(p, x, y, r, Alpha(kBlack, 0.55f));
      DotRing(p, x, y, r, b.nKind == 1 ? kLbInkDesc : kLbInkInfo, 1.3f);
    }
  }
  return nRich;
}

// Rich bases: the map with the bases that may turn rich, and what a trip brings from a Classic
// base and from a rich one, the rich minerals gold and the rich gas violet
void StillRich(const Paint &p, const MutatorSet &m)
{
  const bool bOn = m.nRich != MUTATOR_RICH_OFF;
  const float sz = fminf(p.H - 16, p.W * 0.52f);
  const int n = RichMap(p, 8, (p.H - sz) / 2, sz, m);
  const float cx = 8 + sz + 14, cw = p.W - cx - 8, half = cw / 2;
  Caps(p, PortText("A trip brings"), cx, 16, kScInkOff);
  for (int nRow = 0; nRow < 2; ++nRow) {
    const bool bRich = nRow == 1;
    const float y = bRich ? 132.0f : 36.0f;
    // {0} how many bases may turn rich
    const std::string label = !bRich ? std::string(PortText("Classic"))
                              : !bOn ? std::string(PortText("Rich \xc2\xb7 off"))
                              : m.nRich == MUTATOR_RICH_EVERY ? std::string(PortText("Rich \xc2\xb7 every base"))
                              : n ? PortTextNF("Rich \xc2\xb7 up to {0} base", "Rich \xc2\xb7 up to {0} bases", n,
                                               {std::to_string(n)})
                                  : std::string(PortText("Rich \xc2\xb7 none far enough"));
    const bool bLit = !bRich || (bOn && n > 0);
    const float foot = y + 58, a = bLit ? 1.0f : 0.35f;
    Caps(p, label, cx, y, bRich && bLit ? p.gold : kLbInkDesc);
    ArtAt(p, ART_FIELD, cx + half / 2, foot, half * 0.8f, 46, Alpha(bRich ? kAu : kCry, a));
    ArtAt(p, ART_GEYSER, cx + half * 1.5f, foot, half * 0.92f, 40, Alpha(bRich ? kVi : kGasInk, a));
    Say(p, bRich ? "11" : "8", cx + half / 2, foot + 16, 19, bLit ? (bRich ? kAu : kLbInkInfo) : kScInkOff, 1,
        UI_FACE_BOLD);
    Say(p, bRich ? "16" : "8", cx + half * 1.5f, foot + 16, 19, bLit ? (bRich ? kVi : kLbInkInfo) : kScInkOff, 1,
        UI_FACE_BOLD);
  }
  std::string why;
  if (RichOf(g_shown.pMap, m, &why) == NULL && bOn)
    Say(p, g_bRichReading ? PortText("Reading this map\xe2\x80\xa6") : PortText("A sketch, not this map"), cx,
        p.H - 10, 11, kScInkOff);
}

// Start resources: the counter at the first second, its minerals and gas, Classic's and this
// game's (the workers its minerals buy left out; the user, 2026-10-26: "Not needed")
void StillStart(const Paint &p, const MutatorSet &m)
{
  const MutatorSet c;
  const bool bOn = MutatorOn(m, MUT_START);
  Half h[2];
  Halves(p, bOn, h);
  for (int i = 0; i < 2; ++i) {
    const Half &b = h[i];
    const int an[2] = {i ? m.nStartMinerals : c.nStartMinerals, i ? m.nStartGas : c.nStartGas};
    const bool bLit = i && bOn;
    for (int r = 0; r < 2; ++r) {
      const float y = b.y + b.h * (r ? 0.7f : 0.4f);
      ResIcon(p, r, b.x + 30, y, 26);
      Say(p, Thousands(an[r]), b.x + 52, y - 4, 28, bLit ? p.gold : i ? kLbInkInfo : kLbInkDesc, 0, UI_FACE_BOLD);
      Say(p, r ? PortText("gas") : PortText("minerals"), b.x + 53, y + 20, 12, kLbInkDesc);
    }
  }
}

// Start workers: the town hall, its mineral line and the workers between, Classic's four as
// ghosts where this game has fewer
void StillWorkers(const Paint &p, const MutatorSet &m)
{
  const int n = m.nWorkers;
  const bool bOn = n != 4;
  Half h[2];
  Halves(p, bOn, h);
  for (int i = 0; i < 2; ++i) {
    const Half &b = h[i];
    const int k = i ? n : 4, nSide = i ? SIDE_GAME : SIDE_CLASSIC;
    const bool bLit = i && bOn;
    const float cx = b.x + b.w / 2;
    // the count in the corner beside the town hall, clear of the half's name
    Say(p, std::to_string(k), b.x + b.w - 10, b.y + b.h - 20, 24, i ? (bOn ? p.gold : kLbInkInfo) : kLbInkDesc, 2,
        UI_FACE_BOLD);
    for (int f = 0; f < 4; ++f)
      ArtAt(p, ART_FIELD, b.x + 12 + (b.w - 24) * (f + 0.5f) / 4, b.y + 66, (b.w - 24) / 4 * 1.15f, 36,
            Side(p, nSide, bLit));
    ArtAt(p, ART_CC, cx, b.y + b.h - 8, b.w * 0.5f, 66, Side(p, nSide, bLit));
    const int slots = std::max(k, 4);
    const float sp = (b.w - 24) / std::min(6, slots);
    for (int j = 0; j < slots; ++j) {
      const int r = j / 6, inRow = std::min(6, slots - r * 6);
      ArtAt(p, ART_SCV, cx + (j % 6 - (inRow - 1) / 2.0f) * sp, b.y + 100 + r * 28, sp * 1.1f, 28,
            Side(p, j < k ? nSide : SIDE_GHOST, bLit));
    }
  }
}

// Supply cap: the cap on a gauge with Classic's 200 marked, and what it holds in three armies, a
// race each: marines (1 supply), zealots (2) and ultralisks (4)
void StillSupply(const Paint &p, const MutatorSet &m)
{
  const bool bUn = m.nSupplyCap == 0, bOn = m.nSupplyCap != 200;
  const float cap = bUn ? 820.0f : static_cast<float>(m.nSupplyCap);
  const std::string big = bUn ? std::string(PortText("No cap")) : Thousands(m.nSupplyCap);
  // the cap and what it is, the words beside it or, where they do not fit, under it
  ResIcon(p, 2, 26, 26, 26);
  Say(p, big, 46, 26, 30, bOn ? p.gold : kLbInkInfo, 0, UI_FACE_BOLD);
  const std::string what = bUn        ? PortText("supply has no limit")
                           : p.bHard  ? PortText("supply, less than Classic: harder")
                                      : PortText("supply, the most a player holds");
  const float fBeside = 46 + Width(p, big, 30, UI_FACE_BOLD) + 10;
  if (fBeside + Width(p, what, 12) <= p.W - 12)
    Say(p, what, fBeside, 29, 12, p.bHard ? p.gold : kLbInkDesc);
  else
    Say(p, what, 16, 50, 11, p.bHard ? p.gold : kLbInkDesc);
  const float x0 = 16, bw = p.W - 32, y = 72, top = bUn ? 820.0f : fmaxf(220.0f, cap * 1.1f);
  const float fw = bw * fminf(1.0f, cap / top), at200 = x0 + bw * 200 / top;
  Bar(p, x0, y - 10, bw, 20, 0, kLbInkDesc, kLbInkDesc);
  if (!bUn && cap < 200) {
    // what the cap takes away from Classic's, dashed
    for (float dx = x0 + fw; dx < at200; dx += 7)
      Fill(p, dx, y - 10, fminf(4.0f, at200 - dx), 1.2f, Alpha(p.gold, 0.7f));
    Fill(p, x0, y - 10, fmaxf(20.0f, fw), 20, p.gold, 10);
  } else {
    Fill(p, x0, y - 10, fmaxf(20.0f, fw), 20, kClassicInk, 10);
    if (fw > at200 - x0) {
      UiPushClip(p.X(at200), p.Y(y - 10), p.X(x0 + fw), p.Y(y + 10));
      Fill(p, x0, y - 10, fw, 20, p.gold, 10);
      UiPopClip();
    }
  }
  for (float s = 50; s < top; s += 50) {
    const float sx = x0 + bw * s / top;
    if (sx > x0 + fw)
      break;
    Line(p, sx, y - 10, sx, y + 10, Alpha(kBlack, 0.55f), 1.5f);
  }
  Line(p, at200, y - 16, at200, y + 16, kLbInkInfo, 2);
  Caps(p, PortText("Classic 200"), at200, y + 28, kScInkOff, at200 > p.W - 80 ? 2 : at200 < 80 ? 0 : 1);
  struct Army {
    int nArt, nSupply;
    const char *pszName;
  };
  const Army army[3] = {
      {ART_MARINE, 1, PT_("marines")}, {ART_ZEALOT, 2, PT_("zealots")}, {ART_ULTRA, 4, PT_("ultralisks")}};
  const float cw = (p.W - 32) / 3;
  for (int i = 0; i < 3; ++i) {
    const float x = 16 + cw * (i + 0.5f);
    const std::string n0 = Thousands(200 / army[i].nSupply);
    ArtAt(p, army[i].nArt, x, 176, cw * 0.7f, 62, Side(p, SIDE_GAME, bOn));
    // {0} what they are: the count stands before their name
    Say(p,
        bOn && bUn ? PortTextF("Any number of {0}", {PortText(army[i].pszName)})
                   : (bOn ? Thousands(m.nSupplyCap / army[i].nSupply) : n0) + " " + PortText(army[i].pszName),
        x, 196, 13, bOn ? p.gold : kLbInkInfo, 1, UI_FACE_BOLD);
    if (bOn)
      Say(p, PortTextF("Classic {0}", {n0}), x, 212, 11, kScInkOff, 1);
  }
}

// an arrow pointing right, w long, its middle at y
void Arrow(const Paint &p, float x, float y, float w, UiColor c)
{
  Line(p, x, y, x + w, y, c, 1.5f);
  Line(p, x + w - w * 0.35f, y - w * 0.3f, x + w, y, c, 1.5f);
  Line(p, x + w - w * 0.35f, y + w * 0.3f, x + w, y, c, 1.5f);
}

// Classic's number, an arrow and this game's, centred on x; Classic's alone when it is unchanged
void Pair(const Paint &p, int nFrom, int nTo, bool bOn, float x, float y, float fSize)
{
  if (!bOn) {
    Say(p, std::to_string(nFrom), x, y, fSize, kLbInkInfo, 1, UI_FACE_BOLD);
    return;
  }
  const std::string from = std::to_string(nFrom), to = std::to_string(nTo);
  const float a = Width(p, from, fSize * 0.72f, UI_FACE_BOLD), r = fSize * 0.62f, c = Width(p, to, fSize, UI_FACE_BOLD);
  const float sp = fSize * 0.22f, x0 = x - (a + r + c + sp * 2) / 2;
  Say(p, from, x0, y, fSize * 0.72f, kScInkOff, 0, UI_FACE_BOLD);
  Arrow(p, x0 + a + sp, y, r, kScInkOff);
  Say(p, to, x0 + a + r + sp * 2, y, fSize, p.gold, 0, UI_FACE_BOLD);
}

// Supply boost: two groups in wells of their own, each a triangle of a race each, the suppliers'
// (one figure, theirs being the same) and the town halls' (one each: 10, 9 and 1)
void StillBoost(const Paint &p, const MutatorSet &m)
{
  const int b = m.nSupplyBoost;
  const bool bOn = b > 0;
  const float mg = 8, gap = 8, w = (p.W - mg * 2 - gap) / 2, h = p.H - mg * 2;
  const int anArt[2][3] = {{ART_DEPOT, ART_PYLON, ART_OVERLORD}, {ART_CC, ART_NEXUS, ART_HATCH}};
  const int anMade[3] = {10, 9, 1};
  const char *const kRaces[3] = {PT_("Terran"), PT_("Protoss"), PT_("Zerg")};
  for (int g = 0; g < 2; ++g) {
    const float x = mg + g * (w + gap), y = mg;
    const float at[3][2] = {{x + w / 2, y + 40 + 58}, {x + w * 0.26f, y + 40 + 116}, {x + w * 0.74f, y + 40 + 116}};
    Fill(p, x, y, w, h, kLbItem, 5);
    Ring(p, x, y, w, h, bOn ? p.goldRim : kLbDivider, 5, 1.2f);
    Caps(p, g ? PortText("Town halls") : PortText("Supply"), x + 10, y + 15, bOn ? p.gold : kLbInkDesc);
    for (int i = 0; i < 3; ++i)
      ArtAt(p, anArt[g][i], at[i][0], at[i][1], w * (i ? 0.46f : 0.5f), i ? 60.0f : 62.0f, Side(p, SIDE_GAME, bOn));
    const float fy = y + h - 44;
    if (g == 0) {
      Pair(p, 8, Boosted(8, b), bOn, x + w / 2, fy, 22);
      Say(p, PortText("depot, pylon, overlord"), x + w / 2, fy + 22, 11.5f, kLbInkDesc, 1);
    } else {
      for (int i = 0; i < 3; ++i) {
        const float cx = x + w * (i + 0.5f) / 3;
        Pair(p, anMade[i], Boosted(anMade[i], b), bOn, cx, fy, 15);
        Say(p, PortText(kRaces[i]), cx, fy + 22, 11.5f, kLbInkDesc, 1);
      }
    }
  }
}

// Fog of war: the map in each half, a base at the foot, a scout in the middle and the enemy's base
// in the far corner; what is black, dimmed or seen is what each player sees
void FogZone(const Paint &p, const Half &z, int nFog, int nSide, bool bLit)
{
  UiPushClip(p.X(z.x), p.Y(z.y), p.X(z.x + z.w), p.Y(z.y + z.h));
  const MapPicture *pMap = g_shown.pMap;
  if (pMap != NULL && pMap->bMinimap) {
    // the map's picture, cropped to fill
    int iw, ih;
    MinimapSize(*pMap, &iw, &ih);
    const float ar = z.w / z.h, sw = fminf(static_cast<float>(iw), ih * ar), sh = sw / ar;
    UiPicturePart(p.X(z.x), p.Y(z.y), z.w * p.s, z.h * p.s, pMap->nSlot, (iw - sw) / 2, (ih - sh) / 2, (iw + sw) / 2,
                  (ih + sh) / 2, kWhite);
  } else {
    Fill(p, z.x, z.y, z.w, z.h, kLbItem);
  }
  const float own[2] = {z.x + z.w * 0.22f, z.y + z.h * 0.92f}, scout[2] = {z.x + z.w * 0.52f, z.y + z.h * 0.56f},
              foe[2] = {z.x + z.w * 0.8f, z.y + z.h * 0.3f};
  ArtAt(p, ART_CC, own[0], own[1], z.w * 0.34f, z.h * 0.26f, Side(p, nSide, bLit));
  if (nFog == MUTATOR_FOG_NONE)
    ArtAt(p, ART_CC, foe[0], foe[1], z.w * 0.34f, z.h * 0.26f, Side(p, nSide, bLit));
  ArtAt(p, ART_MARINE, scout[0], scout[1] + 8, z.w * 0.16f, z.h * 0.14f, Side(p, nSide, bLit));
  if (nFog != MUTATOR_FOG_NONE) {
    const float u0 = nFog == MUTATOR_FOG_EXPLORED ? static_cast<float>(kFogW) : 0.0f;
    UiPicturePart(p.X(z.x), p.Y(z.y), z.w * p.s, z.h * p.s, kFogSlot, u0, 0, u0 + kFogW, static_cast<float>(kFogH),
                  kWhite);
  }
  UiPopClip();
  Ring(p, z.x, z.y, z.w, z.h, kLbInnerRim, 4, 1.2f);
}

void StillFog(const Paint &p, const MutatorSet &m)
{
  const bool bOn = m.nFog != MUTATOR_FOG_CLASSIC;
  static const char *const kSay[] = {PT_("Black until scouted"), PT_("Land known, units in sight"),
                                     PT_("The whole map, all game")};
  Half h[2];
  Halves(p, bOn, h);
  for (int i = 0; i < 2; ++i) {
    const Half &b = h[i];
    const int nFog = std::min(std::max(i ? m.nFog : MUTATOR_FOG_CLASSIC, 0), 2);
    FogZone(p, {b.x + 8, b.y + 26, b.w - 16, b.h - 26 - 32}, nFog, i ? SIDE_GAME : SIDE_CLASSIC, i && bOn);
    const std::string say = PortText(kSay[nFog]);
    Say(p, say, b.x + b.w / 2, b.y + b.h - 15, Width(p, say, 12) <= b.w - 8 ? 12.0f : 10.5f,
        i && bOn ? p.gold : kLbInkDesc, 1);
  }
}

// Build time: an SCV's training, Classic's beside this game's, each line as long as its time
void StillBuild(const Paint &p, const MutatorSet &m)
{
  const int k = m.nBuildTime;
  const bool bOn = k != 100;
  const std::string sp = g_shown.speed;  // its English name: Seconds reads the frame's length from it
  const float most = fmaxf(1.0f, k / 100.0f), bx = 96, bw = p.W - bx - 14;
  for (int i = 0; i < 2; ++i) {
    const int kk = i ? k : 100;
    const float y = 16.0f + i * 100;
    const bool bLit = i && bOn;
    Fill(p, 14, y, 70, 70, kScWell, 4);
    Ring(p, 14, y, 70, 70, bLit ? p.goldRim : kLbDivider, 4, 1.2f);
    ArtAt(p, ART_SCV, 49, y + 62, 56, 54, Side(p, i ? SIDE_GAME : SIDE_CLASSIC, bLit));
    Caps(p, i ? (p.bHard ? PortText("This game \xc2\xb7 harder") : PortText("This game")) : PortText("Classic"), bx,
         y + 10, i ? (bOn ? p.gold : kLbInkDesc) : kScInkOff);
    // {0} the seconds it takes
    Say(p, PortTextF("{0} s", {Seconds(kScvFrames * kk / 100, sp)}), p.W - 14, y + 10, 17,
        bLit ? p.gold : i ? kLbInkInfo : kLbInkDesc, 2, UI_FACE_BOLD);
    Bar(p, bx, y + 28, bw, 16, kk / 100.0f / most, bLit ? p.au : kClassicInk, bLit ? p.auLo : kCryLo);
    // {0} the game's speed
    Say(p,
        i ? (bOn ? Times(kk) : std::string(PortText("as Classic")))
          : PortTextF("an SCV at {0}", {PortText(sp.c_str())}),
        bx, y + 60, 12, kScInkOff);
  }
  Say(p, PortText("Buildings, units, research and upgrades alike"), 14, 222, 12, kScInkOff);
}

// Tech and upgrades: three upgrades and three researches, Classic's row over this game's, in the
// command card's own icons, greyed and struck where they cannot be researched
enum { TECH_OPEN, TECH_DONE, TECH_LOCKED };
void IconTile(const Paint &p, int j, float x, float y, float s, int nState)
{
  Fill(p, x, y, s, s, Alpha(kBlack, 0.6f), 3);
  if (g_art.anW[ART_TECH + j] != 0) {
    const float h = fminf(s - 4, static_cast<float>(g_art.anH[ART_TECH + j]) * (s - 4) / 36);
    ArtAt(p, ART_TECH + j, x + s / 2, y + (s + h) / 2, s - 4, h,
          nState == TECH_LOCKED ? UiColor{0.45f, 0.45f, 0.45f, 1.0f} : kWhite);
  }
  Ring(p, x, y, s, s, nState == TECH_DONE ? p.gold : nState == TECH_LOCKED ? kScInkOff : kLbDivider, 3,
       nState == TECH_DONE ? 2.0f : 1.2f);
  if (nState == TECH_LOCKED)
    Line(p, x + 5, y + s - 5, x + s - 5, y + 5, kScInkOff, 2);
  const int nLevels = kTechLevels[j];
  for (int l = 0; l < nLevels; ++l) {
    const float cx = x + s / 2 + (l - (nLevels - 1) / 2.0f) * 9;
    Dot(p, cx, y + s + 7, 3, nState == TECH_DONE ? p.gold : Alpha(kBlack, 0.6f));
    DotRing(p, cx, y + s + 7, 3, nState == TECH_LOCKED ? kScInkOff : kLbInkDesc, 1);
  }
  if (nLevels == 0 && nState == TECH_DONE) {
    Line(p, x + s / 2 - 5, y + s + 6, x + s / 2 - 1, y + s + 10, p.gold, 2);
    Line(p, x + s / 2 - 1, y + s + 10, x + s / 2 + 6, y + s + 2, p.gold, 2);
  }
}

void StillTech(const Paint &p, const MutatorSet &m)
{
  // six tiles in the width, 44 at most
  const float gap = 8, sep = 14, x0 = 14, s = fminf(44.0f, (p.W - x0 * 2 - gap * 4 - sep) / 6);
  const bool bOn = m.nTech != MUTATOR_TECH_CLASSIC;
  static const char *const kSay[] = {PT_("all to research"), PT_("all done at the start"), PT_("no upgrades"),
                                     PT_("nothing to research")};
  Caps(p, PortText("Upgrades"), x0, 14, kScInkOff);
  Caps(p, PortText("Research"), x0 + 3 * (s + gap) + sep, 14, kScInkOff);
  for (int i = 0; i < 2; ++i) {
    const int st = std::min(std::max(i ? m.nTech : MUTATOR_TECH_CLASSIC, 0), 3);
    const float y = 38.0f + i * 100;
    // {0} what it does: "all to research" and the like
    Caps(p,
         i ? PortTextF("This game \xc2\xb7 {0}", {!bOn ? PortText("as Classic") : PortText(kSay[st])})
           : PortTextF("Classic \xc2\xb7 {0}", {PortText(kSay[st])}),
         x0, y, i ? (bOn ? p.gold : kLbInkDesc) : kScInkOff);
    const int up = st == MUTATOR_TECH_ALL ? TECH_DONE : st == MUTATOR_TECH_CLASSIC ? TECH_OPEN : TECH_LOCKED;
    const int re = st == MUTATOR_TECH_ALL ? TECH_DONE : st == MUTATOR_TECH_NO_RESEARCH ? TECH_LOCKED : TECH_OPEN;
    for (int j = 0; j < 6; ++j)
      IconTile(p, j, x0 + j * (s + gap) + (j > 2 ? sep : 0), y + 12, s, j > 2 ? re : up);
  }
  const std::string names = PortText("Weapons, Armor, U-238 Shells \xc2\xb7 Stim Packs, Lockdown, Siege Tech");
  Say(p, Width(p, names, 11) <= p.W - x0 * 2 ? names : std::string(PortText("Three upgrades and three researches")),
      x0, 226, 11, kScInkOff);
}

// The briefing's picture: the mutator in n, as g_shown has it.
void DrawPicture(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  UiGradient(x, y, w, h, UiColor{0.0f, 0.0f, 0.0f, 0.5f}, UiColor{0.0f, 0.0f, 0.0f, 0.3f}, false, Sc(3));
  UiRing(x, y, w, h, kLbDivider, Sc(3), fmaxf(1.0f, Sc(1)));
  if (h <= 0 || w <= 0)
    return;
  const MutatorSet &m = g_shown.m;
  // the space at least kPicMinW wide: a tall picture (a 4:3 screen) is drawn smaller, centred
  Paint p;
  p.s = fminf(h / kPicH, w / kPicMinW);
  p.x0 = x;
  p.y0 = y + (h - kPicH * p.s) / 2;
  p.W = w / p.s;
  p.H = kPicH;
  p.bHard = MutatorHard(m, d.n);
  p.gold = p.bHard ? kHard : kScInkAccept;
  p.au = p.bHard ? kHard : kAu;
  p.auLo = p.bHard ? kHardLo : kAuLo;
  p.goldRim = p.bHard ? kHardRim : kLbHyperionRim;
  UiPushClip(x, y, x + w, y + h);
  switch (d.n) {
  case MUT_MINERALS: StillPile(p, m, false); break;
  case MUT_GAS: StillPile(p, m, true); break;
  case MUT_RICH: StillRich(p, m); break;
  case MUT_START: StillStart(p, m); break;
  case MUT_WORKERS: StillWorkers(p, m); break;
  case MUT_SUPPLY: StillSupply(p, m); break;
  case MUT_BOOST: StillBoost(p, m); break;
  case MUT_FOG: StillFog(p, m); break;
  case MUT_BUILD: StillBuild(p, m); break;
  case MUT_TECH: StillTech(p, m); break;
  }
  UiPopClip();
}

// ---------------------------------------------------------------------------
// The controls' looks.

// a row of the list (n: 1 chosen, 2 pointed at, 4 on, 8 harder)
void DrawRow(float x, float y, float w, float h, void *pCtx)
{
  const int n = static_cast<const ScDraw *>(pCtx)->n;
  const bool bSel = (n & 1) != 0, bHot = (n & 2) != 0, bOn = (n & 4) != 0, bHard = (n & 8) != 0;
  UiFill(x, y, w, h, bSel || bHot ? kLbItemHi : kLbItem, Sc(3));
  const UiColor rim = bOn ? (bHard ? kHardRim : kLbHyperionRim) : bSel || bHot ? kScButtonHi.rim : kLbItemRim;
  UiRing(x, y, w, h, rim, Sc(3), fmaxf(1.0f, Sc(1)));
  if (bSel)
    UiFill(x, y, Sc(3), h, kScSelLine2);
}

// a dashed rectangle's sides, t thick
void Dashes(float x, float y, float w, float h, UiColor c, float t, bool bLeftOnly)
{
  const float dash = Sc(6), gap = Sc(4);
  for (float d = 0; d < h; d += dash + gap)
    UiFill(x, y + d, t, fminf(dash, h - d), c);
  if (bLeftOnly)
    return;
  for (float d = 0; d < h; d += dash + gap)
    UiFill(x + w - t, y + d, t, fminf(dash, h - d), c);
  for (float d = 0; d < w; d += dash + gap) {
    UiFill(x + d, y, fminf(dash, w - d), t, c);
    UiFill(x + d, y + h - t, fminf(dash, w - d), t, c);
  }
}

// the briefing's well (n: 1 a row pointed at, not chosen: its rim dashed)
void DrawBrief(float x, float y, float w, float h, void *pCtx)
{
  const int n = static_cast<const ScDraw *>(pCtx)->n;
  UiFill(x, y, w, h, kScWell, Sc(4));
  if (n & 1)
    Dashes(x, y, w, h, kLbInnerRim, fmaxf(1.0f, Sc(2)), false);
  else
    UiRing(x, y, w, h, kLbInnerRim, Sc(4), fmaxf(1.0f, Sc(2)));
}

// a choice (n: 1 chosen, 2 pointed at, 4 Classic's, 8 harder): Classic's has a blue dot in its
// corner, a harder one a red one
void DrawChip(float x, float y, float w, float h, void *pCtx)
{
  const int n = static_cast<const ScDraw *>(pCtx)->n;
  const bool bOn = (n & 1) != 0, bHot = (n & 2) != 0, bBase = (n & 4) != 0, bHard = (n & 8) != 0;
  const ScLook &k = bOn ? kScTabOn : bHot ? kScMiniHi : kScMini;
  UiGradient(x, y, w, h, k.top, k.bot, false, Sc(3));
  const UiColor rim = !bOn ? k.rim : bBase ? kScTabOn.rim : bHard ? kHardRim : kLbHyperionRim;
  UiRing(x, y, w, h, rim, Sc(3), fmaxf(1.0f, Sc(2)));
  if (bOn)
    UiFill(x + Sc(2), y + h - Sc(4), w - Sc(4), Sc(2), bBase ? kScSelLine2 : bHard ? kHard : kScInkAccept);
  if (bBase || bHard) {
    const float r = Sc(2.5f);
    UiFill(x + w - Sc(4) - 2 * r, y + Sc(4), 2 * r, 2 * r, bBase ? kClassicInk : kHard, r);
  }
}

// what the choice does, ruled at its left (n: 1 on, 2 harder, 4 a choice pointed at: dashed)
void DrawEffect(float x, float y, float w, float h, void *pCtx)
{
  const int n = static_cast<const ScDraw *>(pCtx)->n;
  UiFill(x, y, w, h, kLbItem, Sc(3));
  const UiColor c = (n & 2) ? kHard : (n & 4) ? kScInkHi : (n & 1) ? kScInkAccept : kClassicInk;
  if (n & 4)
    Dashes(x, y, Sc(3), h, c, Sc(3), true);
  else
    UiFill(x, y, Sc(3), h, c);
}

// the Game Options tab's word on the mutators (n: 1 some on)
void DrawSummary(float x, float y, float w, float h, void *pCtx)
{
  const int n = static_cast<const ScDraw *>(pCtx)->n;
  UiFill(x, y, w, h, kLbItem, Sc(4));
  UiRing(x, y, w, h, n ? kLbHyperionRim : kLbItemRim, Sc(4), fmaxf(1.0f, Sc(1)));
  if (n)
    UiFill(x, y, Sc(3), h, kScInkAccept);
}

// a tab over the right column (n: 1 up, 2 pointed at)
void DrawViewTab(float x, float y, float w, float h, void *pCtx)
{
  const int n = static_cast<const ScDraw *>(pCtx)->n;
  if (n & 1) {
    UiFill(x, y, w, h, kLbInner, Sc(4));
    UiRing(x, y, w, h, kLbInnerRim, Sc(4), fmaxf(1.0f, Sc(2)));
    UiFill(x + Sc(2), y + Sc(2), w - Sc(4), Sc(2), kScSelLine2);
    return;
  }
  const ScLook &k = (n & 2) ? kScTabHi : kScTab;
  UiGradient(x, y, w, h, k.top, k.bot, false, Sc(4));
  UiRing(x, y, w, h, k.rim, Sc(4), fmaxf(1.0f, Sc(2)));
}

void DrawBadge(float x, float y, float w, float h, void *)
{
  UiFill(x, y, w, h, kScInkAccept, h / 2);
}

// ---------------------------------------------------------------------------
// Laying out.

// A text's width in SC2's units, as Text draws it.
float TextWidth(UiFace face, float fSize, const std::string &s, float fSpacingEm = 0.0f)
{
  return UiTextWidth(face, Sc(fSize), Sc(fSize) * fSpacingEm, s.c_str()) / Sc(1);
}

void Rule(float fHeight)
{
  Decl gap;
  gap.d.layout.sizing.height = Fixed(Sc(fHeight));
  Open(gap);
  Close();
}

void NewTag(void)
{
  Decl tag;
  tag.d.layout.padding = Pad(Fs(6), Fs(6), Fs(2), Fs(2));
  tag.d.backgroundColor = Col(kAu);
  Radius(tag, 3);
  Open(tag);
  Text(PortText("NEW"), UI_FACE_WIDE, Fs(10), 0.06f, kBlack);
  Close();
}

// Items fWidth units wide at most to a row, fGap apart: the first of each row.
std::vector<int> Rows(const std::vector<float> &widths, float fWidth, float fGap)
{
  std::vector<int> first;
  float x = 0;
  for (size_t i = 0; i < widths.size(); ++i) {
    if (first.empty() || x + widths[i] > fWidth) {
      first.push_back(static_cast<int>(i));
      x = 0;
    }
    x += widths[i] + fGap;
  }
  return first;
}

// A row of choices as chips, wrapped to fWidth: the one clicked, or -1. *pnHot the one pointed at.
int Chips(const char *pszId, int nBase, const std::vector<std::string> &labels, int nCur, int nClassic,
          const std::vector<bool> &hard, bool bMine, float fWidth)
{
  std::vector<float> widths;
  for (const std::string &s : labels)
    widths.push_back(fmaxf(48.0f, 22.0f + TextWidth(UI_FACE_TEXT, 15, s)));
  const std::vector<int> first = Rows(widths, fWidth, 6);
  int nClicked = -1;
  Decl rows;
  rows.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  rows.d.layout.childGap = Gap(Fs(6));
  Open(rows);
  for (size_t r = 0; r < first.size(); ++r) {
    const int nEnd = r + 1 < first.size() ? first[r + 1] : static_cast<int>(labels.size());
    Decl row;
    row.d.layout.childGap = Gap(Fs(6));
    Open(row);
    for (int k = first[r]; k < nEnd; ++k) {
      Decl b = Box(pszId, nBase + k);
      const bool bOn = k == nCur, bHot = bMine && Over(b.d.id);
      if (bMine && Clicked(b.d.id))
        nClicked = k;
      b.d.layout.sizing.width = Fixed(Sc(widths[k]));
      b.d.layout.sizing.height = Fixed(Sc(38));
      b.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
      b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      b.d.custom.customData =
          Custom(DrawChip, kScMini, 0.0f, (bOn ? 1 : 0) | (bHot ? 2 : 0) | (k == nClassic ? 4 : 0) | (hard[k] ? 8 : 0));
      Open(b);
      const UiColor ink = bOn ? (k == nClassic ? kScInkHi : hard[k] ? kHard : kScInkAccept)
                          : bHot ? kScInkHi
                          : bMine ? kScInkButton
                                  : kScInkOff;
      Text(Keep(labels[k]), UI_FACE_TEXT, Fs(15), 0.0f, ink);
      Close();
      if (k == nClassic)
        TipOn(b.d.id, Keep(labels[k]), PortText("Classic\xe2\x80\x99s."));
      else if (hard[k])
        TipOn(b.d.id, Keep(labels[k]), PortText("Harder than Classic."));
    }
    Close();
  }
  Close();
  return nClicked;
}

// How many lines Clay wraps a text into, fWidth pixels wide (clay.h: each word with the space
// after it, a word wider than the line on a line of its own).
int WrappedLines(UiFace face, float fSize, const std::string &s, float fWidth)
{
  const float fPx = roundf(UiPx(fSize));
  const float fSpace = UiTextWidth(face, fPx, 0.0f, " ", 1);
  std::vector<float> words;
  size_t start = 0;
  for (size_t i = 0; i < s.size(); ++i)
    if (s[i] == ' ') {
      words.push_back(UiTextWidth(face, fPx, 0.0f, s.c_str() + start, static_cast<int>(i - start)) + fSpace);
      start = i + 1;
    }
  if (start < s.size())
    words.push_back(UiTextWidth(face, fPx, 0.0f, s.c_str() + start, static_cast<int>(s.size() - start)));
  int nLines = 0;
  float fLine = 0.0f;
  bool bOpen = false;
  for (float fWord : words) {
    if (bOpen && fLine + fWord > fWidth) {
      ++nLines;
      fLine = 0.0f;
      bOpen = false;
    }
    if (!bOpen && fWord > fWidth) {
      ++nLines;
      continue;
    }
    fLine += fWord;
    bOpen = true;
  }
  return nLines + (bOpen ? 1 : 0);
}

bool RichRolls(const MutatorSet &m)
{
  return m.nRich != MUTATOR_RICH_OFF && m.nRich != MUTATOR_RICH_EVERY;
}

// The effect's box as tall as the tallest of the effects of the mutator's choices (and of the
// chance's, for rich bases), as the last frame laid the box out, so a chip pointed at, whose
// effect it shows, never moves the chips from under the pointer. It never shrinks while the same
// mutator is briefed at the same width either, in case Clay wraps a line this does not.
float EffectHeight(const MutatorSet &m, int nMut)
{
  static int s_nOf = -1;
  static float s_fWidth = 0.0f, s_fFloor = 0.0f;
  const Clay_ElementData last = Clay_GetElementData(Id("skmuteff"));
  if (!last.found)
    return 0.0f;
  if (s_nOf != nMut || s_fWidth != last.boundingBox.width) {
    s_nOf = nMut;
    s_fWidth = last.boundingBox.width;
    s_fFloor = 0.0f;
  }
  const float fPadX = static_cast<float>(static_cast<uint16_t>(UiPx(Fs(14)) + 0.5f));
  const float fPadY = static_cast<float>(static_cast<uint16_t>(UiPx(Fs(10)) + 0.5f));
  const float fWidth = last.boundingBox.width - 2 * fPadX;
  int nLines = 1;
  // a rich resources line of a choice not read yet is measured as the longest, with no read
  // started: each would read the map, and on a large one eighteen took nine seconds
  g_bMeasuring = true;
  for (int k = 0; k < MutatorChoices(nMut); ++k) {
    MutatorSet t = m;
    MutatorChoose(&t, nMut, k);
    nLines = std::max(nLines, WrappedLines(UI_FACE_TEXT, Fs(15), Effect(t, nMut), fWidth));
    for (int c = 0; nMut == MUT_RICH && RichRolls(t) && c < MutatorChoices(MUT_RICH_CHANCE); ++c) {
      MutatorSet u = t;
      MutatorChoose(&u, MUT_RICH_CHANCE, c);
      nLines = std::max(nLines, WrappedLines(UI_FACE_TEXT, Fs(15), Effect(u, nMut), fWidth));
    }
    for (int c = 0; nMut == MUT_RICH && t.nRich == MUTATOR_RICH_RADIUS && c < MutatorChoices(MUT_RICH_RADIUS); ++c) {
      MutatorSet u = t;
      MutatorChoose(&u, MUT_RICH_RADIUS, c);
      nLines = std::max(nLines, WrappedLines(UI_FACE_TEXT, Fs(15), Effect(u, nMut), fWidth));
    }
  }
  g_bMeasuring = false;
  s_fFloor = std::max(s_fFloor, last.boundingBox.height);
  return std::max(s_fFloor, nLines * roundf(UiPx(Fs(15) * 1.4f)) + 2 * fPadY);
}

// the choice pointed at among a row of chips, as the last frame laid them out, or -1
int ChipPointed(const char *pszId, int nBase, int nCount)
{
  for (int k = 0; k < nCount; ++k)
    if (Over(Id(pszId, nBase + k)))
      return k;
  return -1;
}

// A tag (lobby.css .lb-tag): Hyperion's gold when the set has mutators, else Classic's.
void Tag(const char *psz, bool bOn)
{
  Decl tag;
  tag.d.layout.padding = Pad(Fs(10), Fs(10), Fs(3), Fs(3));
  tag.d.border = Border(bOn ? kLbHyperionRim : kScWellRim, 1, 1, 1, 1);
  Radius(tag, 3);
  Open(tag);
  Text(psz, UI_FACE_WIDE, Fs(12), 0.04f, bOn ? kScInkAccept : kScInkOff);
  Close();
}

int OnCount(const MutatorSet &m)
{
  int n = 0;
  for (int i = 0; i < MUT_COUNT; ++i)
    n += MutatorOn(m, i) ? 1 : 0;
  return n;
}

}  // namespace

int MutatorsViewTabs(float x, float y, float w, int nView, const MutatorSet &m, const char *pszFirst,
                     const char *pszFirstTip)
{
  const int nOn = OnCount(m), nPreset = MutatorPresetOf(m);
  Decl strip = At("skvtabs", x, y, w, 46);
  strip.d.layout.childGap = Gap(Fs(4));
  strip.d.layout.childAlignment.y = CLAY_ALIGN_Y_BOTTOM;
  Open(strip);
  int nClicked = -1;
  // pszFirst and pszFirstTip come translated from the caller
  const char *const kLabels[2] = {pszFirst != NULL ? pszFirst : PortText("GAME OPTIONS"), PortText("MUTATORS")};
  static const char kKeys[2] = {'O', 'U'};
  for (int t = 0; t < 2; ++t) {
    Decl b = Box("skvtab", t);
    const bool bOn = t == nView, bHot = !bOn && Over(b.d.id);
    if (!bOn && Clicked(b.d.id))
      nClicked = t;
    b.d.layout.sizing.height = Fixed(Sc(bOn ? 44 : 40));
    b.d.layout.padding = Pad(Fs(22), Fs(22), 0, 0);
    b.d.layout.childGap = Gap(Fs(10));
    b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    b.d.custom.customData = Custom(DrawViewTab, kScTab, 0.0f, (bOn ? 1 : 0) | (bHot ? 2 : 0));
    Open(b);
    Text(kLabels[t], UI_FACE_WIDE, Fs(15), 0.04f, bOn || bHot ? kScInkHi : kScInkButton, KeyAt(kLabels[t], kKeys[t]));
    if (t == 1 && nOn > 0) {
      Decl badge;
      badge.d.layout.sizing.width = Fixed(Sc(22));
      badge.d.layout.sizing.height = Fixed(Sc(22));
      badge.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
      badge.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      badge.d.custom.customData = Custom(DrawBadge, kScTab);
      Open(badge);
      Text(Keep(std::to_string(nOn)), UI_FACE_BOLD, Fs(13), 0.0f, kBlack);
      Close();
    }
    Close();
    TipOn(b.d.id,
          t ? PortText("Mutators") : pszFirst != NULL ? PortText("Options") : PortText("Game Options"),
          t ? PortText("One set of mutators for every player: what the map holds, what each player starts with, supply, the "
                       "fog, build time and tech. Hotkey: U")
          : pszFirstTip != NULL ? pszFirstTip
                                : PortText("The game type, speed, rules and start positions, and the map. Hotkey: O"));
  }
  Spacer();
  Decl set;
  set.d.layout.sizing.height = Fixed(Sc(40));
  set.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(set);
  // {0} the set's name
  Text(nOn ? Keep(PortTextF("{0} \xc2\xb7 Hyperion rules",
                            {nPreset >= 0 ? MutatorPresetName(nPreset) : PortText("Custom set")}))
           : PortText("No mutators \xc2\xb7 as Classic"),
       UI_FACE_TEXT, Fs(15), 0.0f, nOn ? kScInkAccept : kScInkOff);
  Close();
  Close();
  return nClicked;
}

bool MutatorsTab(float x, float y, float w, float h, const MutatorsSetUp &s)
{
  EnsureArt();
  EnsureFog();
  MutatorSet &m = *s.pSet;
  bool bChanged = false;
  // the row pointed at is briefed until the pointer leaves the list; a chip pointed at shows its
  // choice in the picture and the line before it is taken
  int nHotRow = -1;
  for (int i = 0; i < MUT_COUNT; ++i)
    if (Over(Id("skmutrow", i)))
      nHotRow = i;
  const int nShown = nHotRow >= 0 ? nHotRow : g_nSel;
  const bool bPeekRow = nShown != g_nSel;
  MutatorSet shown = m;
  bool bPeekChip = false;
  if (!bPeekRow && s.bMine) {
    // rich resources' choices are pulldowns, whose items are pointed at as chips are
    const int k = nShown == MUT_RICH ? ChipPointed("skmutwhere_item", 0, MutatorChoices(MUT_RICH))
                                     : ChipPointed("skmutchip", nShown * 16, MutatorChoices(nShown));
    if (k >= 0 && k != MutatorChoiceExact(m, nShown)) {
      MutatorChoose(&shown, nShown, k);
      bPeekChip = true;
    }
    const int c = nShown == MUT_RICH && RichRolls(m) ? ChipPointed("skmutchance_item", 0, MutatorChoices(MUT_RICH_CHANCE)) : -1;
    if (c >= 0 && c != MutatorChoiceExact(m, MUT_RICH_CHANCE)) {
      MutatorChoose(&shown, MUT_RICH_CHANCE, c);
      bPeekChip = true;
    }
    const int d = nShown == MUT_RICH && m.nRich == MUTATOR_RICH_RADIUS
                      ? ChipPointed("skmutradius_item", 0, MutatorChoices(MUT_RICH_RADIUS))
                      : -1;
    if (d >= 0 && d != MutatorChoiceExact(m, MUT_RICH_RADIUS)) {
      MutatorChoose(&shown, MUT_RICH_RADIUS, d);
      bPeekChip = true;
    }
  }
  g_shown.m = shown;
  g_shown.nMut = nShown;
  g_shown.pMap = s.pMap;
  g_shown.title = s.pszMapTitle != NULL ? s.pszMapTitle : "";
  g_shown.speed = s.pszSpeed != NULL ? s.pszSpeed : "Fastest";
  g_shown.bMapUnits = s.bMapUnits;

  Decl box = At("skmutbox", x, y, w, h);
  box.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  box.d.layout.padding = Pad(Fs(22), Fs(22), Fs(18), Fs(20));
  InnerBox(box);
  Open(box);
  Decl head;
  head.d.layout.sizing.width = Grow();
  head.d.layout.childGap = Gap(Fs(18));
  head.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(head);
  Decl title = Box("skmuttitle");
  Open(title);
  Text(PortText("MUTATORS"), UI_FACE_WIDE, Fs(16), 0.04f, kScInkHead);
  Close();
  TipOn(title.d.id, PortText("Mutators"),
        PortText("They change the game past the classic rules, the same for every player, so they play by Hyperion\xe2\x80\x99s, "
                 "and its replay and save keep them."));
  Decl line;
  line.d.layout.sizing.width = Grow();
  Open(line);
  Text(s.bMine
           ? PortText("One set for every player. Any mutator plays by Hyperion rules, and the replay keeps the set.")
           : PortText("The host\xe2\x80\x99s set, the same for every player. Any mutator plays by Hyperion rules."),
       UI_FACE_TEXT, Fs(15), 0.0f, kLbInkDesc);
  Close();
  Close();
  Rule(16);

  // the presets, the engine's five; Custom lights for any other set and is never a choice
  Decl pr;
  pr.d.layout.childGap = Gap(Fs(14));
  pr.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(pr);
  Text(PortText("Presets"), UI_FACE_BOLD, Fs(16), 0.0f, kScInkLabel);
  const int nPresets = MutatorPresets();
  std::vector<const char *> presets;
  for (int i = 0; i < nPresets; ++i)
    presets.push_back(MutatorPresetName(i));
  presets.push_back(PortText("Custom"));
  const int nPreset = MutatorPresetOf(m);
  const int nPick = Segments("skpreset", presets.data(), nPresets + 1, nPreset >= 0 ? nPreset : nPresets,
                             s.bMine ? 1u << nPresets : ~0u,
                             s.bMine ? PortText("Any other set of the mutators below.")
                                     : PortText("The host chooses the mutators."),
                             34);
  if (nPick >= 0 && nPick < nPresets && nPick != nPreset) {
    const int nUnits = m.nUnits;  // Game Options' unit limit, which no preset sets
    m = MutatorPreset(nPick);
    m.nUnits = nUnits;
    bChanged = true;
  }
  Close();
  Rule(18);

  // the list and the briefing
  const float fBodyH = h - 18 - 20 - 26 - 16 - 38 - 18;
  const float fHeads = 22 + 3 * 34.0f;
  const float fRow = fmaxf(30.0f, fminf(42.0f, (fBodyH - fHeads - MUT_COUNT * 5) / MUT_COUNT));
  Decl body;
  body.d.layout.sizing.width = Grow();
  body.d.layout.sizing.height = Grow();
  body.d.layout.childGap = Gap(Fs(18));
  Open(body);
  Decl list = Box("skmutlist");
  list.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  list.d.layout.sizing.width = Fixed(Sc(300));
  list.d.layout.childGap = Gap(Fs(5));
  Open(list);
  for (const Group &g : kGroups) {
    Decl gh;
    gh.d.layout.sizing.width = Grow();
    gh.d.layout.padding = Pad(Fs(4), Fs(4), Fs(&g == kGroups ? 0 : 12), Fs(5));
    gh.d.border = Border(kLbDivider, 0, 0, 0, 1);
    Open(gh);
    Text(Keep(Upper(PortText(g.pszName))), UI_FACE_WIDE, Fs(12), 0.06f, kScInkHead);
    Close();
    for (int j = g.nFirst; j < g.nFirst + g.nCount; ++j) {
      const int i = kOrder[j];
      const bool bOn = MutatorOn(m, i), bHard = bOn && MutatorHard(m, i);
      Decl row = Box("skmutrow", i);
      const bool bHot = Over(row.d.id);
      if (Clicked(row.d.id))
        g_nSel = i;
      row.d.layout.sizing.width = Grow();
      row.d.layout.sizing.height = Fixed(Sc(fRow));
      row.d.layout.padding = Pad(Fs(14), Fs(14), 0, 0);
      row.d.layout.childGap = Gap(Fs(8));
      row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      row.d.custom.customData = Custom(DrawRow, kScTab, 0.0f,
                                       (i == g_nSel ? 1 : 0) | (bHot ? 2 : 0) | (bOn ? 4 : 0) | (bHard ? 8 : 0));
      Open(row);
      Text(MutatorName(i), UI_FACE_BOLD, Fs(16), 0.0f, kScInkLabel);
      // the new mutator's tag until it is chosen: then its choice takes the room ("Rich resources"
      // and "Outside a radius" with the tag wrapped)
      if (i == MUT_RICH && !bOn)
        NewTag();
      Spacer();
      Text(bOn ? Keep(MutatorValueName(m, i)) : PortText("Classic"), bOn ? UI_FACE_BOLD : UI_FACE_TEXT, Fs(15), 0.0f,
           bOn ? (bHard ? kHard : kScInkAccept) : kScInkOff);
      Close();
    }
  }
  Close();

  // the briefing: the name, what it does, the picture, the choices and what the choice does
  const float fBriefW = w - 44 - 300 - 18 - 36;
  Decl brief = Box("skmutbrief");
  brief.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  brief.d.layout.sizing.width = Grow();
  brief.d.layout.sizing.height = Grow();
  brief.d.layout.padding = Pad(Fs(18), Fs(18), Fs(16), Fs(16));
  brief.d.layout.childGap = Gap(Fs(12));
  brief.d.custom.customData = Custom(DrawBrief, kScTab, 0.0f, bPeekRow ? 1 : 0);
  Open(brief);
  Decl top;
  top.d.layout.sizing.width = Grow();
  top.d.layout.childGap = Gap(Fs(12));
  top.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(top);
  Text(MutatorName(nShown), UI_FACE_BOLD, Fs(24), 0.0f, kLbInkInfo);
  if (nShown == MUT_RICH)
    NewTag();
  Spacer();
  if (bPeekRow)
    Text(PortText("CLICK TO CHOOSE"), UI_FACE_WIDE, Fs(11), 0.06f, kScInkOff);
  Close();
  Decl gist;
  gist.d.layout.sizing.width = Grow();
  Open(gist);
  Text(PortText(kGists[nShown]), UI_FACE_TEXT, Fs(16), 0.0f, kLbInkInfo, -1, 1.4f);
  Close();
  Decl pic;
  pic.d.layout.sizing.width = Grow();
  pic.d.layout.sizing.height = Grow();
  pic.d.custom.customData = Custom(DrawPicture, kScTab, 0.0f, nShown);
  Open(pic);
  Close();
  // the choices; a guest's are the host's, shown, not chosen. Rich resources' are pulldowns
  // (below), so its picture keeps its room in a narrow window
  if (nShown != MUT_RICH) {
    std::vector<std::string> labels;
    std::vector<bool> hard;
    for (int k = 0; k < MutatorChoices(nShown); ++k) {
      labels.push_back(MutatorChoiceName(nShown, k));
      hard.push_back(MutatorChoiceHard(nShown, k));
    }
    const int nClicked = Chips("skmutchip", nShown * 16, labels, MutatorChoiceExact(m, nShown),
                               MutatorChoiceOf(MutatorSet(), nShown), hard, s.bMine && !bPeekRow, fBriefW);
    if (nClicked >= 0 && nClicked != MutatorChoiceExact(m, nShown)) {
      MutatorChoose(&m, nShown, nClicked);
      bChanged = true;
    }
  }
  // rich resources' choices as pulldowns, each under its name: where, then the distance, for
  // Outside a radius, and the chance, at which a base far enough turns rich, side by side; each
  // greyed while it plays no part, and there all the same (the user, 2026-10-26: in a narrow
  // window the chips left the picture no room)
  if (nShown == MUT_RICH) {
    const bool bPick = s.bMine && !bPeekRow;
    // a pulldown of nOf's choices under its name, in play or greyed; the choice made, or -1
    const auto Pick = [&](const char *pszId, const char *pszName, const char *pszTip, float fWidth, int nOf,
                          bool bInPlay, bool bFirstWord) {
      std::vector<std::string> labels;
      for (int k = 0; k < MutatorChoices(nOf); ++k) {
        const std::string name = MutatorChoiceName(nOf, k);
        labels.push_back(bFirstWord ? name.substr(0, name.find(' ')) : name);
      }
      Decl col = Box((std::string(pszId) + "l").c_str());
      col.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
      col.d.layout.childGap = Gap(Fs(5));
      Open(col);
      Text(pszName, UI_FACE_BOLD, Fs(15), 0.0f, bInPlay ? kScInkLabel : kScInkOff);
      const int n = Pulldown(pszId, 0, fWidth, labels, MutatorChoiceExact(m, nOf), bPick && bInPlay ? PD_ON : PD_OFF);
      Close();
      TipOn(col.d.id, pszName, pszTip);
      return n;
    };
    const int nWhere =
        Pick("skmutwhere", PortText("Where"),
             PortText("Which bases may turn rich: those past each start\xe2\x80\x99s natural, those at least a distance from "
                      "every start, only the farthest, or every base. Off has none, as Classic."),
             230, MUT_RICH, true, false);
    if (nWhere >= 0 && nWhere != MutatorChoiceExact(m, MUT_RICH)) {
      MutatorChoose(&m, MUT_RICH, nWhere);
      bChanged = true;
    }
    Decl two;
    two.d.layout.childGap = Gap(Fs(16));
    Open(two);
    const int nRadius =
        Pick("skmutradius", PortText("Distance"),
             PortText("How far a base must be from every start, in a straight line, to turn rich: in tiles, or a share of the "
                      "map\xe2\x80\x99s size, its width and height averaged. For Outside a radius."),
             150, MUT_RICH_RADIUS, shown.nRich == MUTATOR_RICH_RADIUS, false);
    if (nRadius >= 0 && nRadius != MutatorChoiceExact(m, MUT_RICH_RADIUS)) {
      MutatorChoose(&m, MUT_RICH_RADIUS, nRadius);
      bChanged = true;
    }
    // no chance is Classic's: Classic has no rich resources
    const int nChance =
        Pick("skmutchance", PortText("Chance"),
             PortText("The chance a base far enough from every start turns rich at. Bases that mirror each other are rolled "
                      "together. Not for Off or Every base, which roll nothing."),
             110, MUT_RICH_CHANCE, RichRolls(shown), true);
    if (nChance >= 0 && nChance != MutatorChoiceExact(m, MUT_RICH_CHANCE)) {
      MutatorChoose(&m, MUT_RICH_CHANCE, nChance);
      bChanged = true;
    }
    Close();
    // whether the game's start says which bases turned rich, the host's to choose
    const bool bSays = (shown.nRichRules & MUTATOR_RICH_QUIET) == 0, bLive = s.bMine && !bPeekRow && shown.nRich != MUTATOR_RICH_OFF;
    const Clay_ElementId says = Id("skmutannounce");
    const bool bHot = bLive && Over(says);
    Decl line;
    line.d.id = says;
    line.d.layout.childGap = Gap(Fs(10));
    line.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(line);
    Decl box;
    box.d.layout.sizing.width = Fixed(Sc(24));
    box.d.layout.sizing.height = Fixed(Sc(23));
    box.d.custom.customData = Custom(DrawScCheck, kScButton, 0.0f, (bSays ? 1 : 0) | (bHot ? 2 : 0) | (bLive ? 0 : 8));
    Open(box);
    Close();
    Text(PortText("Announce at the start"), UI_FACE_BOLD, Fs(15), 0.0f,
         shown.nRich == MUTATOR_RICH_OFF ? kScInkOff : bHot ? kScInkHi : kScInkLabel);
    Close();
    TipOn(says, PortText("Announce at the start"),
          PortText("On, the game\xe2\x80\x99s first message says how many bases turned rich, or why none did. Off, it says "
                   "nothing, and players find the gold and violet bases on the map."));
    if (bLive && Clicked(says)) {
      m.nRichRules ^= MUTATOR_RICH_QUIET;
      bChanged = true;
    }
  }
  const bool bOnShown = MutatorOn(shown, nShown), bHardShown = MutatorHard(shown, nShown);
  Decl eff = Box("skmuteff");
  eff.d.layout.sizing.width = Grow();
  eff.d.layout.sizing.height = Fit();
  eff.d.layout.sizing.height.size.minMax.min = EffectHeight(m, nShown);
  eff.d.layout.padding = Pad(Fs(14), Fs(14), Fs(10), Fs(10));
  eff.d.custom.customData =
      Custom(DrawEffect, kScTab, 0.0f, (bOnShown ? 1 : 0) | (bHardShown ? 2 : 0) | (bPeekChip ? 4 : 0));
  Open(eff);
  Text(Keep(Effect(shown, nShown)), UI_FACE_TEXT, Fs(15), 0.0f, kLbInkInfo, -1, 1.4f);
  Close();
  Close();
  Close();
  Close();
  if (bChanged)
    g_nSel = nShown;
  return bChanged;
}

bool MutatorsSummary(const MutatorsSetUp &s)
{
  const MutatorSet &m = *s.pSet;
  const int nOn = OnCount(m), nPreset = MutatorPresetOf(m);
  Decl sum = Box("skmutsum");
  sum.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  sum.d.layout.sizing.width = Grow();
  sum.d.layout.padding = Pad(Fs(16), Fs(16), Fs(14), Fs(14));
  sum.d.layout.childGap = Gap(Fs(10));
  sum.d.custom.customData = Custom(DrawSummary, kScTab, 0.0f, nOn ? 1 : 0);
  Open(sum);
  Decl top;
  top.d.layout.sizing.width = Grow();
  top.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(top);
  Text(PortText("MUTATORS"), UI_FACE_WIDE, Fs(16), 0.04f, kScInkHead);
  Spacer();
  // {0} how many mutators are on
  Tag(nOn ? Keep(nPreset >= 0 ? Upper(MutatorPresetName(nPreset))
                              : PortTextF("{0} ON", {std::to_string(nOn)}))
          : PortText("NONE ON"),
      nOn > 0);
  Close();
  if (nOn == 0) {
    Decl none;
    none.d.layout.sizing.width = Grow();
    Open(none);
    Text(PortText("The game plays as Classic: the map\xe2\x80\x99s resources, 50 minerals, 4 workers, 200 supply."),
         UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc, -1, 1.4f);
    Close();
  } else {
    // a chip each, wrapped to the box
    std::vector<int> on;
    std::vector<float> widths;
    std::vector<std::string> values;
    for (int j = 0; j < MUT_COUNT; ++j) {
      const int i = kOrder[j];
      if (!MutatorOn(m, i))
        continue;
      on.push_back(i);
      values.push_back(" " + MutatorValueName(m, i));
      widths.push_back(18 + TextWidth(UI_FACE_BOLD, 14, MutatorName(i)) + TextWidth(UI_FACE_TEXT, 14, values.back()) + 2);
    }
    const std::vector<int> first = Rows(widths, 438 - 35 - 32, 6);
    Decl rows;
    rows.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    rows.d.layout.childGap = Gap(Fs(6));
    Open(rows);
    for (size_t r = 0; r < first.size(); ++r) {
      const int nEnd = r + 1 < first.size() ? first[r + 1] : static_cast<int>(on.size());
      Decl row;
      row.d.layout.childGap = Gap(Fs(6));
      Open(row);
      for (int k = first[r]; k < nEnd; ++k) {
        const bool bHard = MutatorHard(m, on[k]);
        Decl chip;
        chip.d.layout.padding = Pad(Fs(9), Fs(9), Fs(3), Fs(3));
        chip.d.border = Border(bHard ? kHardRim : kLbHyperionRim, 1, 1, 1, 1);
        Radius(chip, 3);
        Open(chip);
        Text(MutatorName(on[k]), UI_FACE_BOLD, Fs(14), 0.0f, bHard ? kHard : kScInkAccept);
        Text(Keep(values[k]), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkInfo);
        Close();
      }
      Close();
    }
    Close();
  }
  Decl go = Box("skmutgo");
  go.d.layout.sizing.height = Fixed(Sc(34));
  const bool bGo = ScMini(go,
                          nOn       ? PortText("Change Mutators")
                          : s.bMine ? PortText("Choose Mutators")
                                    : PortText("See Mutators"),
                          'U');
  Close();
  return bGo;
}

void MutatorsTabBrief(int nAt)
{
  g_nSel = kOrder[(nAt % MUT_COUNT + MUT_COUNT) % MUT_COUNT];
}

bool MutatorsTabKeys(const MutatorsSetUp &s, bool *pbChanged)
{
  *pbChanged = false;
  const bool bUp = Key(VK_UP), bDown = Key(VK_DOWN), bLeft = Key(VK_LEFT), bRight = Key(VK_RIGHT);
  if (bUp || bDown) {
    int j = 0;
    while (j < MUT_COUNT && kOrder[j] != g_nSel)
      ++j;
    g_nSel = kOrder[(j + (bDown ? 1 : MUT_COUNT - 1)) % MUT_COUNT];
    return true;
  }
  if (!(bLeft || bRight))
    return false;
  if (!s.bMine)
    return true;
  const int nAt = MutatorChoiceOf(*s.pSet, g_nSel);
  const int nTo = std::min(std::max(nAt + (bRight ? 1 : -1), 0), MutatorChoices(g_nSel) - 1);
  if (nTo != nAt || MutatorChoiceExact(*s.pSet, g_nSel) < 0) {
    MutatorChoose(s.pSet, g_nSel, nTo);
    *pbChanged = true;
  }
  return true;
}

}  // namespace FrontKit
