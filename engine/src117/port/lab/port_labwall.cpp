// port_labwall.cpp -- the AI lab's wall (port_labwall.h). A tile a game the farm plays:
// its map, small, with its resources, and its units as dots in their players' colours. Each
// worker samples its units by the clock (30 a second), and the wall draws them a little behind
// the last sample, gliding each unit from where the sample before had it to where the last one
// does, so the games move as they play rather than in steps. A unit that is gone flashes a
// moment where it was. When a game ends its tile says how, until the next one starts.
//
// The maps' pictures are kept in the picture atlas's slots 26 and 27, nine maps of at most 128
// pixels a slot. Presentation only: the lab draws what its workers send, nothing of a game.
#include <windows.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>
#include "port_labfarm.h"
#include "port_labwall.h"
#include "../data/port_hxm.h"
#include "../front/port_frontkit.h"
#include "../front/port_frontlobby.h"
#include "../front/port_ui.h"
#include "../platform/port_util.h"
#include "glues.h"   // mem_free
#include "maphdr.h"  // get_map_chk
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

// 0x66FD4C ghScenArchive, spelled as src117/lang/maphdr.cpp spells it: the scenario archive,
// which a running game keeps open as its map's (port_control.cpp's map_info reads one alike).
extern HANDLE ghScenArchive;

using namespace FrontKit;

namespace {

const int kWallSlots[2] = {26, 27};  // the picture atlas's (port_ui.h)
const int kMapSide = 128;            // a map's picture, at most, in pixels
const int kAcross = UI_PICTURE_SIDE / kMapSide;
const int kTilesMost = 64;
const uint64_t kBehindMs = 50;       // how far behind the last sample the wall draws
const uint64_t kGoneMs = 350;        // how long a unit that is gone flashes
const uint64_t kEndMs = 1600;        // how long a game's end is said on its tile

// the slots' colours as 1.17 gives them (as the benchmark's minimap, port_benchview.cpp)
const unsigned kColours[12] = {0xf40404, 0x0c48cc, 0x2cb494, 0x88409c, 0xf88c14, 0x703014,
                               0xcce0d0, 0xfcfc38, 0x10fc18, 0x7cacfc, 0x00e4fc, 0x006400};
const char *const kColourNames[8] = {"Red", "Blue", "Teal", "Purple", "Orange", "Brown", "White", "Yellow"};

unsigned SlotRgb(int nSlot)
{
  return nSlot >= 0 && nSlot < 12 ? kColours[nSlot] : 0xb0b0b0;
}

// A map the wall has read: where its picture is, its size and its resources.
struct WallMap {
  std::string path;
  int nSlot = -1, nCell = 0;  // the wall's slot (0 or 1) and a cell of it, -1 for no picture
  int nPicW = 0, nPicH = 0;
  int w = 0, h = 0;  // tiles
  std::vector<MapResource> resources;
};
std::deque<WallMap> g_maps;
std::vector<unsigned char> g_sheets[2];
int g_nPictures;

const WallMap &MapOf(const std::string &path)
{
  for (const WallMap &m : g_maps)
    if (m.path == path)
      return m;
  g_maps.emplace_back();
  WallMap &m = g_maps.back();
  m.path = path;
  std::string file = path;
  char szCompiled[MAX_PATH] = "";
  if (PortHxmIsMap(path.c_str()) && PortHxmCompiled(path.c_str(), szCompiled, sizeof szCompiled))
    file = szCompiled;
  // read as the live channel's map_info reads one, a game's own archive put by and back
  const HANDLE hGameArchive = ghScenArchive;
  ghScenArchive = NULL;
  DWORD dwSize = 0;
  BYTE *pChk = static_cast<BYTE *>(get_map_chk(file.c_str(), &dwSize));
  ghScenArchive = hGameArchive;
  if (pChk == NULL)
    return m;
  MapPicture pic;
  pic.nSlot = -1;
  MapPictureRead(&pic, path, pChk, dwSize);
  m.w = pic.w;
  m.h = pic.h;
  m.resources = pic.resources;
  std::vector<unsigned char> rgba;
  int nW = 0, nH = 0;
  if (g_nPictures < 2 * kAcross * kAcross && MapMinimapRgba(pChk, dwSize, kMapSide, &rgba, &nW, &nH)) {
    m.nSlot = g_nPictures / (kAcross * kAcross);
    m.nCell = g_nPictures % (kAcross * kAcross);
    m.nPicW = nW;
    m.nPicH = nH;
    ++g_nPictures;
    std::vector<unsigned char> &sheet = g_sheets[m.nSlot];
    sheet.resize(static_cast<size_t>(UI_PICTURE_SIDE) * UI_PICTURE_SIDE * 4, 0);
    const int x0 = (m.nCell % kAcross) * kMapSide, y0 = (m.nCell / kAcross) * kMapSide;
    for (int y = 0; y < nH; ++y)
      memcpy(&sheet[(static_cast<size_t>(y0 + y) * UI_PICTURE_SIDE + x0) * 4], &rgba[static_cast<size_t>(y) * nW * 4],
             static_cast<size_t>(nW) * 4);
    UiPictureSet(kWallSlots[m.nSlot], sheet.data(), UI_PICTURE_SIDE, UI_PICTURE_SIDE);
  }
  mem_free(pChk, __FILE__, __LINE__);
  return m;
}

// What a tile keeps between frames: the sample it last saw, and the units gone since.
struct Gone {
  float x, y;  // map pixels
  int nPlayer;
  uint64_t qAt;
};
struct TileState {
  uint32_t dwGame = 0;
  uint64_t qSample = 0;
  std::vector<Gone> gone;
};
struct TileCtx {
  int nSlot;
  bool bHidden;
};
TileState g_states[kTilesMost];
TileCtx g_ctx[kTilesMost];
UiCustom g_tileCustoms[kTilesMost];
int g_nClicked = -1;

std::string Clock(unsigned dwFrame)
{
  const unsigned n = static_cast<unsigned>(dwFrame * 42ull / 1000);  // Fastest's 42 ms a frame
  char sz[24];
  if (n >= 3600)
    snprintf(sz, sizeof sz, "%u:%02u:%02u", n / 3600, n / 60 % 60, n % 60);
  else
    snprintf(sz, sizeof sz, "%u:%02u", n / 60, n % 60);
  return sz;
}

// The units gone from one sample to the next, by the two lists in the order of their ids.
void NoteGone(TileState &st, const LabSlot &s)
{
  const std::vector<LabDot> &a = s.prev.dots, &b = s.last.dots;
  size_t j = 0;
  for (const LabDot &d : a) {
    while (j < b.size() && b[j].dwId < d.dwId)
      ++j;
    if (j >= b.size() || b[j].dwId != d.dwId)
      st.gone.push_back({static_cast<float>(d.x), static_cast<float>(d.y), d.bPlayer, s.last.qAt});
  }
}

void Centred(float x, float y, float w, float fSize, UiFace face, UiColor c, const std::string &text)
{
  const float fW = UiTextWidth(face, fSize, 0.0f, text.c_str());
  UiText(x + (w - fW) / 2.0f, y, face, fSize, 0.0f, c, text.c_str());
}

void DrawTile(float x, float y, float w, float h, void *pCtx)
{
  const TileCtx &t = *static_cast<const TileCtx *>(pCtx);
  const LabSlot &s = PortLabFarmSlot(t.nSlot);
  TileState &st = g_states[t.nSlot];
  const uint64_t qNow = PortNowMs();
  const float r = Sc(3);
  UiFill(x, y, w, h, KitRgb(0x02070e), r);
  const float fStrip = Sc(22), fText = Sc(13);
  if (s.dwShown == 0) {
    Centred(x, y + h / 2.0f, w, fText, UI_FACE_TEXT, kLbInkDesc, s.state == LAB_STARTING ? "Starting" : "Waiting");
    return;
  }
  if (st.dwGame != s.dwShown) {
    st = TileState();
    st.dwGame = s.dwShown;
  }
  const WallMap &m = MapOf(s.shownMap);
  // the map, fitted below the strip
  const float fAreaY = y + fStrip, fAreaH = h - fStrip - Sc(2);
  float mw = w - Sc(4), mh = fAreaH;
  if (m.w > 0 && m.h > 0) {
    if (mw / mh > static_cast<float>(m.w) / m.h)
      mw = mh * m.w / m.h;
    else
      mh = mw * m.h / m.w;
  }
  const float mx = x + (w - mw) / 2.0f, my = fAreaY + (fAreaH - mh) / 2.0f;
  const bool bEnding = s.qEndAt != 0 && qNow - s.qEndAt < kEndMs;
  const bool bWaiting = s.state == LAB_STARTING || (s.state == LAB_ENDING && !bEnding) ||
                        (s.qEndAt != 0 && !bEnding);
  if (t.bHidden) {
    // how far it has gone, by its length
    const unsigned dwMost = PortLabFarmProject().dwFrames;
    const float f = dwMost ? fminf(1.0f, static_cast<float>(s.dwFrame) / dwMost) : 0.0f;
    UiFill(x + Sc(8), y + h - Sc(20), (w - Sc(16)) * f, Sc(6), KitRgb(0x66d5fe, 0.25f), Sc(2));
  } else {
    if (m.nSlot >= 0) {
      const float u0 = static_cast<float>((m.nCell % kAcross) * kMapSide);
      const float v0 = static_cast<float>((m.nCell / kAcross) * kMapSide);
      UiPicturePart(mx, my, mw, mh, kWallSlots[m.nSlot], u0, v0, u0 + m.nPicW, v0 + m.nPicH,
                    KitRgb(0xffffff, bWaiting ? 0.35f : 1.0f));
    } else {
      UiFill(mx, my, mw, mh, KitRgb(0x1a2a1a, bWaiting ? 0.35f : 1.0f));
    }
    UiFill(mx, my, mw, mh, KitRgb(0x000000, 0.18f));  // the map darker, so the units stand out
    const float k = m.w > 0 ? mw / (m.w * 32.0f) : 0.0f;
    // the resources the map starts with
    const float fRes = fmaxf(Sc(1.6f), 32.0f * k);
    for (const MapResource &res : m.resources)
      UiFill(mx + res.x * k - fRes / 2.0f, my + res.y * k - fRes / 2.0f, fRes, fRes,
             KitRgb(res.bGas ? 0x48c878 : 0x58b8f0, bWaiting ? 0.3f : 0.8f));
    // the units, glided between the last two samples
    if (s.last.qAt != st.qSample) {
      if (st.qSample != 0 && s.prev.qAt != 0)
        NoteGone(st, s);
      st.qSample = s.last.qAt;
    }
    const uint64_t qShow = qNow > kBehindMs ? qNow - kBehindMs : 0;
    float a = 1.0f;
    if (s.prev.qAt != 0 && s.last.qAt > s.prev.qAt)
      a = fminf(1.0f, fmaxf(0.0f, static_cast<float>(static_cast<int64_t>(qShow - s.prev.qAt)) /
                                      static_cast<float>(s.last.qAt - s.prev.qAt)));
    const float fUnit = fmaxf(Sc(2.0f), 40.0f * k), fBuilding = fmaxf(Sc(3.6f), 96.0f * k);
    const float fAlpha = bWaiting ? 0.35f : 1.0f;
    const std::vector<LabDot> &pv = s.prev.dots;
    size_t j = 0;
    for (const LabDot &d : s.last.dots) {
      while (j < pv.size() && pv[j].dwId < d.dwId)
        ++j;
      float ux = d.x, uy = d.y, fA = fAlpha;
      if (j < pv.size() && pv[j].dwId == d.dwId) {
        ux = pv[j].x + (d.x - pv[j].x) * a;
        uy = pv[j].y + (d.y - pv[j].y) * a;
      } else if (s.prev.qAt != 0) {
        fA *= a;  // new since the sample before: it comes in
      }
      const bool bBuilding = (d.bKind & 1) != 0;
      const float f = bBuilding ? fBuilding : fUnit;
      UiFill(mx + ux * k - f / 2.0f, my + uy * k - f / 2.0f, f, f, KitRgb(SlotRgb(d.bPlayer), fA),
             bBuilding ? 0.0f : f / 2.0f);
    }
    // the units gone, a flash fading where each was
    for (size_t i = 0; i < st.gone.size();) {
      const Gone &g = st.gone[i];
      if (qShow > g.qAt + kGoneMs) {
        st.gone.erase(st.gone.begin() + static_cast<std::ptrdiff_t>(i));
        continue;
      }
      const float f = qShow > g.qAt ? static_cast<float>(qShow - g.qAt) / kGoneMs : 0.0f;
      const float fR = fUnit * (1.0f + 1.5f * f);
      UiRing(mx + g.x * k - fR / 2.0f, my + g.y * k - fR / 2.0f, fR, fR, KitRgb(0xffffff, 0.7f * (1.0f - f)), fR / 2.0f,
             Sc(1.0f));
      ++i;
    }
  }
  // the strip: its time, its game, its speed
  UiFill(x, y, w, fStrip, KitRgb(0x020a14, 0.72f), r);
  const float fBase = y + fStrip / 2.0f + fText * 0.35f;
  UiText(x + Sc(7), fBase, UI_FACE_TEXT, fText, 0.0f, KitRgb(0xcce7ff), Clock(s.dwFrame).c_str());
  const std::string right = PortLabFarmPaused() ? std::string("Paused")
                            : s.nFps10 > 0 ? std::to_string(static_cast<int>(s.nFps10 / 10.0 / 23.81 + 0.5)) + "\xc3\x97"
                                           : std::string("");
  const std::string mid = "Game " + std::to_string(s.dwShown);
  Centred(x, fBase, w, fText, UI_FACE_TEXT, KitRgb(0x8dbfea, 0.8f), mid);
  const float fRW = UiTextWidth(UI_FACE_TEXT, fText, 0.0f, right.c_str());
  UiText(x + w - Sc(7) - fRW, fBase, UI_FACE_TEXT, fText, 0.0f, KitRgb(0x8dbfea), right.c_str());
  // its end
  if (bEnding) {
    const bool bWon = !s.winners.empty();
    const int nWinner = bWon ? s.winners[0] : -1;
    const std::string text = bWon ? std::string(nWinner < 8 ? kColourNames[nWinner] : "Player " + std::to_string(nWinner + 1)) +
                                        " wins" + kDot + Clock(s.dwFrame)
                                  : std::string("Length limit") + kDot + Clock(s.dwFrame);
    const float fBand = Sc(28);
    UiFill(x, y + h / 2.0f - fBand / 2.0f, w, fBand, KitRgb(bWon ? SlotRgb(nWinner) : 0x8dbfea, 0.85f));
    Centred(x, y + h / 2.0f + Sc(5), w, Sc(15), UI_FACE_BOLD, KitRgb(0x02070e), text);
  } else if (bWaiting) {
    Centred(x, y + h / 2.0f + Sc(5), w, Sc(14), UI_FACE_TEXT, kScInkHi, "Next game starting");
  }
}

}  // namespace

namespace FrontKit {

void LabWall(float x, float y, float w, float h, bool bHidden)
{
  g_nClicked = -1;
  PortLabFarmWallShown(!bHidden);
  // the tiles: every slot playing, or showing a game it played
  std::vector<int> tiles;
  for (int i = 0; i < PortLabFarmSlots() && static_cast<int>(tiles.size()) < kTilesMost; ++i) {
    const LabSlot &s = PortLabFarmSlot(i);
    if (s.state != LAB_IDLE || s.dwShown != 0)
      tiles.push_back(i);
  }
  Decl wall = At("labwall", x, y, w, h);
  wall.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  wall.d.layout.childGap = Gap(Fs(8));
  Open(wall);
  if (tiles.empty()) {
    Text(PortLabFarmRunning() ? "The workers are starting." : "Start the farm to see its games here.", UI_FACE_TEXT,
         Fs(17), 0.0f, kLbInkDesc);
    Close();
    return;
  }
  // as many across as makes the tiles largest, a little wider than tall for the strip
  const int n = static_cast<int>(tiles.size());
  const float fGap = 8.0f;
  int nAcross = 1;
  float fBest = 0.0f;
  for (int c = 1; c <= n; ++c) {
    const int nDown = (n + c - 1) / c;
    const float tw = (w - fGap * (c - 1)) / c, th = (h - fGap * (nDown - 1)) / nDown;
    const float f = fminf(tw, th * 1.15f);
    if (f > fBest) {
      fBest = f;
      nAcross = c;
    }
  }
  const int nDown = (n + nAcross - 1) / nAcross;
  const float tw = fminf((w - fGap * (nAcross - 1)) / nAcross, ((h - fGap * (nDown - 1)) / nDown) * 1.15f);
  const float th = tw / 1.15f;
  for (int r = 0; r < nDown; ++r) {
    Decl row;
    row.d.layout.childGap = Gap(Fs(fGap));
    Open(row);
    for (int c = 0; c < nAcross && r * nAcross + c < n; ++c) {
      const int nTile = r * nAcross + c, nSlot = tiles[static_cast<size_t>(nTile)];
      g_ctx[nTile] = {nSlot, bHidden};
      g_tileCustoms[nTile] = UiCustom{0.0f, DrawTile, &g_ctx[nTile]};
      Decl t = Box("labtile", nTile);
      t.d.layout.sizing.width = Fixed(Sc(tw));
      t.d.layout.sizing.height = Fixed(Sc(th));
      t.d.custom.customData = &g_tileCustoms[nTile];
      t.d.border = Border(Over(t.d.id) ? kScInkHi : KitRgb(0x3980bc, 0.4f), Fs(1), Fs(1), Fs(1), Fs(1));
      if (Clicked(t.d.id))
        g_nClicked = nSlot;
      Open(t);
      Close();
    }
    Close();
  }
  Close();
}

int LabWallClicked(void)
{
  return g_nClicked;
}

void LabMapPicture(float x, float y, float w, float h, const std::string &path, float fAlpha)
{
  const WallMap &m = MapOf(path);
  float mw = w, mh = h;
  if (m.w > 0 && m.h > 0) {
    if (mw / mh > static_cast<float>(m.w) / m.h)
      mw = mh * m.w / m.h;
    else
      mh = mw * m.h / m.w;
  }
  const float mx = x + (w - mw) / 2.0f, my = y + (h - mh) / 2.0f;
  if (m.nSlot >= 0) {
    const float u0 = static_cast<float>((m.nCell % kAcross) * kMapSide);
    const float v0 = static_cast<float>((m.nCell / kAcross) * kMapSide);
    UiPicturePart(mx, my, mw, mh, kWallSlots[m.nSlot], u0, v0, u0 + m.nPicW, v0 + m.nPicH, KitRgb(0xffffff, fAlpha));
  } else {
    UiFill(mx, my, mw, mh, KitRgb(0x1a2a1a, fAlpha));
  }
  const float k = m.w > 0 ? mw / (m.w * 32.0f) : 0.0f;
  const float fRes = fmaxf(Sc(2.0f), 32.0f * k);
  for (const MapResource &res : m.resources)
    UiFill(mx + res.x * k - fRes / 2.0f, my + res.y * k - fRes / 2.0f, fRes, fRes,
           KitRgb(res.bGas ? 0x48c878 : 0x58b8f0, 0.8f * fAlpha));
}

}  // namespace FrontKit
