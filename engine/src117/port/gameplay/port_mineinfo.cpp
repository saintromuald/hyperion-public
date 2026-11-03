// port_mineinfo.cpp -- the mining a town hall shows over it, and the idle workers' icon
// (port_mineinfo.h), as StarCraft II's worker count and idle worker icon do. The user,
// 2026-11-02, asked for a clean-room one (their Cosmonarchy has its own): the game's own
// tables and units give every figure, and the mathematics is Hyperion's.
//
// Over each of the local player's town halls two simple numbers show, the minerals' and
// the gas': the workers on the base's patches over the number that would saturate them,
// 20 / 20 for a base fully mining and 3 / 3 for its geysers, in SC2's green, amber and
// red. A mineral patch's saturation count comes from its distance: a worker's trip is the
// walk there and back - at the worker's speed, from the game's own direction table - plus
// its harvest, and the patch is saturated when enough harvests fill the trip, at most
// three a patch; a geyser takes three. The number is the indicator: green at the optimum,
// amber from half of it, red below.
//
// Above the minimap, as SC2's, a small fixed icon shows the idle workers when there are
// any, with the count; a click takes the view to the next idle worker, as the ` key and
// the HUD's button do.
//
// Presentation only: no game value is read or written that a replay does not already keep,
// and nothing here changes what the workers do.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include "CUnit.h"
#include "CSprite.h"
#include "oc_data.h"
#include "port_mineinfo.h"
#include "../render/port_overlay.h"
#include "../render/port_view.h"
#include "../front/port_ui.h"
#include "../hud/port_hudstate.h"  // PortHudPressButton, the idle icon's click
#include "../../h/hxrules.h"  // HX_OPT_START_SPLIT, the start workers split's option
#include "../../gen/globals117.h"

namespace {

// The unit types the picture needs (arr/units.dat's order).
bool IsWorkerType(int nType) { return nType == 0x07 || nType == 0x29 || nType == 0x40; }
bool IsTownHall(int nType) {
  return nType == 0x6A || nType == 0x83 || nType == 0x84 || nType == 0x85 || nType == 0x9A;
}
bool IsMineralField(int nType) { return nType >= 0xB0 && nType <= 0xB2; }
bool IsGeyser(int nType) { return nType == 0xBC; }
bool IsRefinery(int nType) { return nType == 0x6E || nType == 0x95 || nType == 0x9D; }

// The harvest the saturation count shares: how many frames a worker is on the patch taking
// its load. The game's own harvest is longer; this is the steady share of a trip the patch
// is busy for, which is what another worker would queue behind.
const int kDockFrames = 64;

// A patch the base mines: its position, the town hall it belongs to, how many workers it
// takes to saturate, and how many are on it now.
struct Patch {
  CUnit *pUnit;
  bool bGas;
  int nBase;
  int nX, nY;   // its centre, in map pixels
  int nSat;     // the workers that saturate it
  int nOn;      // the workers on it now
};

// The distance a worker walks in a frame: the game's own velocity, the direction table's
// cardinal entry times the worker's top speed shifted to whole pixels (the flingy keeps its
// position in 24.8 and moves a 256th of this a frame, so two shifts of 8); 0 for none.
int WorkerPxPerFrame(const CUnit *pWorker)
{
  if (pWorker->lTopSpeed <= 0)
    return 0;
  return static_cast<int>((DIRECTIONS[0] * static_cast<LONGLONG>(pWorker->lTopSpeed)) >> 16);
}

// The workers a mineral patch takes to saturate: the trip, the walk there and back plus one
// dock, over the dock; at least one, at most three. A geyser takes three.
int Saturate(int nDistPx, int nPxPerFrame)
{
  if (nDistPx <= 0 || nPxPerFrame <= 0)
    return 2;
  const int nFrames = (2 * nDistPx + nPxPerFrame / 2) / nPxPerFrame + kDockFrames;
  const int nSat = (nFrames + kDockFrames - 1) / kDockFrames;
  return nSat < 1 ? 1 : nSat > 3 ? 3 : nSat;
}

// The worker's target as one of the patches, or -1; a refinery target finds the geyser it
// sits on.
int PatchOf(const CUnit *pWorker, const std::vector<Patch> &ap)
{
  const CUnit *pT = pWorker->pOrderTarget;
  if (pT == NULL)
    return -1;
  for (size_t i = 0; i < ap.size(); ++i) {
    const Patch &p = ap[i];
    if (pT == p.pUnit)
      return static_cast<int>(i);
    // the geyser's refinery stands on it: a worker mining gas has the refinery as its target
    if (p.bGas && IsRefinery(pT->wType) && pT->pSprite != NULL &&
        abs(static_cast<int>(pT->pSprite->wPosX) - p.nX) < 64 &&
        abs(static_cast<int>(pT->pSprite->wPosY) - p.nY) < 64)
      return static_cast<int>(i);
  }
  return -1;
}

bool Shown(void)
{
  return gwGameMode == 3 && gPlayback == 0;
}

// The idle workers' icon's place, fixed above the minimap's corner, as SC2's; the count
// beside the little worker drawn in the box. The Mouse hook answers clicks there.
bool g_bIdleBadge;
float g_fIdleX, g_fIdleY, g_fIdleW, g_fIdleH;

// The small badge: a box, a worker (a head and a body), and the count.
void IdleBadge(float fX, float fY, float fW, float fH, int nCount, float fScale)
{
  const float fSize = 22.0f * fScale;
  g_fIdleX = fX;
  g_fIdleY = fY - fSize;
  g_fIdleW = fSize + 34.0f * fScale;
  g_fIdleH = fSize;
  g_bIdleBadge = true;
  UiFill(g_fIdleX, g_fIdleY, g_fIdleW, g_fIdleH, UiColor{0.0f, 0.0f, 0.0f, 0.55f});
  UiRing(g_fIdleX, g_fIdleY, g_fIdleW, g_fIdleH, UiColor{0.9f, 0.7f, 0.15f, 0.9f}, 4.0f, 1.5f);
  // the worker: a head above a body, in SC2's idle colour
  const float cx = g_fIdleX + fSize / 2;
  UiFill(cx - fSize * 0.12f, g_fIdleY + fSize * 0.28f, fSize * 0.24f, fSize * 0.5f,
         UiColor{0.9f, 0.7f, 0.15f, 1.0f});
  UiRing(cx, g_fIdleY + fSize * 0.16f, fSize * 0.3f, fSize * 0.3f, UiColor{0.9f, 0.7f, 0.15f, 1.0f},
         fSize * 0.15f, fSize * 0.11f);
  char sz[8];
  snprintf(sz, sizeof sz, "%d", nCount);
  UiText(g_fIdleX + fSize + 8.0f * fScale, g_fIdleY + fSize * 0.62f, UI_FACE_BOLD, fSize * 0.62f, 0.0f,
         UiColor{1.0f, 1.0f, 1.0f, 0.95f}, sz);
}

bool Draw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  g_bIdleBadge = false;
  if (!Shown() || fW <= 0 || fH <= 0)
    return false;
  const int nPlayer = static_cast<int>(g_LocalNationID);

  // the local player's completed town halls, one worker for the walk's speed, and the
  // idle workers (the ones standing guard, as the HUD's count takes them)
  std::vector<CUnit *> abase;
  const CUnit *pWorker = NULL;
  int nIdle = 0;
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
    if (p->bPlayer != nPlayer || p->pSprite == NULL)
      continue;
    if ((p->dwFlags & 1) != 0 && IsTownHall(p->wType))
      abase.push_back(p);
    if (IsWorkerType(p->wType)) {
      if (pWorker == NULL)
        pWorker = p;
      if ((p->dwFlags & 1) != 0 && (p->bOrderId == 0x02 || p->bOrderId == 0x03))
        ++nIdle;
    }
  }
  const int nPxFrame = pWorker != NULL ? WorkerPxPerFrame(pWorker) : 0;

  // the patches, each the nearest town hall's; a geyser's three workers are its own count
  std::vector<Patch> ap;
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
    if (!IsMineralField(p->wType) && !IsGeyser(p->wType))
      continue;
    if (p->pSprite == NULL)
      continue;
    const int x = static_cast<int>(p->pSprite->wPosX);
    const int y = static_cast<int>(p->pSprite->wPosY);
    int nBest = 0;
    long long llBest = -1;
    for (size_t b = 0; b < abase.size(); ++b) {
      if (abase[b]->pSprite == NULL)
        continue;
      const long long dx = x - static_cast<int>(abase[b]->pSprite->wPosX);
      const long long dy = y - static_cast<int>(abase[b]->pSprite->wPosY);
      const long long d = dx * dx + dy * dy;
      if (llBest < 0 || d < llBest) {
        llBest = d;
        nBest = static_cast<int>(b);
      }
    }
    long long nDist = 0;
    if (llBest > 0)
      nDist = static_cast<long long>(sqrt(static_cast<double>(llBest)));
    Patch patch;
    patch.pUnit = p;
    patch.bGas = IsGeyser(p->wType);
    patch.nBase = nBest;
    patch.nX = x;
    patch.nY = y;
    patch.nSat = patch.bGas ? 3 : Saturate(static_cast<int>(nDist), nPxFrame);
    patch.nOn = 0;
    ap.push_back(patch);
  }

  // the workers on each patch, and the ones walking back to a town hall (their load is
  // the minerals', most often)
  std::vector<int> anBack(abase.size(), 0);
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
    if (p->bPlayer != nPlayer || !IsWorkerType(p->wType))
      continue;
    const int nPatch = PatchOf(p, ap);
    if (nPatch >= 0) {
      ++ap[nPatch].nOn;
      continue;
    }
    if (p->pOrderTarget != NULL)
      for (size_t b = 0; b < abase.size(); ++b)
        if (p->pOrderTarget == abase[b]) {
          ++anBack[b];
          break;
        }
  }

  // the camera's anchor and zoom put each world point on the window: the game box shows
  // nWidth by 480 game pixels, and a map pixel is a game pixel divided by the zoom, off
  // the anchor.
  double fCamX, fCamY, fZoom;
  PortViewCamera(&fCamX, &fCamY, &fZoom);
  const double fSx = fW / g_portView.nWidth / fZoom;
  const double fSy = fH / 480.0 / fZoom;
  bool bDrew = false;

  // over each town hall the two numbers: the minerals', then the gas'
  for (size_t b = 0; b < abase.size(); ++b) {
    if (abase[b]->pSprite == NULL)
      continue;
    int nMinOn = anBack[b], nMinOpt = 0;
    int nGasOn = 0, nGasOpt = 0;
    for (const Patch &p : ap) {
      if (p.nBase != static_cast<int>(b))
        continue;
      if (p.bGas) {
        nGasOn += p.nOn;
        nGasOpt += p.nSat;
      } else {
        nMinOn += p.nOn;
        nMinOpt += p.nSat;
      }
    }
    if (nMinOpt <= 0 && nGasOpt <= 0)
      continue;
    const auto colour = [](int nOn, int nOpt) -> UiColor {
      if (nOpt <= 0)
        return UiColor{0.24f, 0.83f, 0.24f, 1.0f};
      if (nOn >= nOpt)
        return UiColor{0.24f, 0.83f, 0.24f, 1.0f};
      return nOn * 2 >= nOpt ? UiColor{0.95f, 0.75f, 0.1f, 1.0f} : UiColor{0.9f, 0.3f, 0.2f, 1.0f};
    };
    const double fXw = static_cast<int>(abase[b]->pSprite->wPosX);
    const double fYw = static_cast<int>(abase[b]->pSprite->wPosY);
    const double fSx0 = fX + (fXw - fCamX) * fSx;
    const double fSy0 = fY + (fYw - fCamY) * fSy;
    if (fSx0 < fX - 80 || fSx0 > fX + fW + 80 || fSy0 < fY - 60 || fSy0 > fY + fH)
      continue;
    char sz[16];
    float fYLine = static_cast<float>(fSy0) - 46.0f;
    if (nGasOpt > 0)
      fYLine -= 12.0f;
    if (nMinOpt > 0) {
      snprintf(sz, sizeof sz, "%d / %d", nMinOn, nMinOpt);
      const float fWid = UiTextWidth(UI_FACE_BOLD, 13.0f, 0.0f, sz);
      const float fX0 = static_cast<float>(fSx0) - fWid / 2;
      const UiColor c = colour(nMinOn, nMinOpt);
      UiText(fX0 + 1.0f, fYLine + 1.0f, UI_FACE_BOLD, 13.0f, 0.0f, UiColor{0.0f, 0.0f, 0.0f, 0.7f}, sz);
      UiText(fX0, fYLine, UI_FACE_BOLD, 13.0f, 0.0f, c, sz);
    }
    if (nGasOpt > 0) {
      snprintf(sz, sizeof sz, "%d / %d", nGasOn, nGasOpt);
      const float fWid = UiTextWidth(UI_FACE_BOLD, 11.0f, 0.0f, sz);
      const float fX0 = static_cast<float>(fSx0) - fWid / 2;
      const UiColor c = colour(nGasOn, nGasOpt);
      UiText(fX0 + 1.0f, fYLine + 12.0f + 1.0f, UI_FACE_BOLD, 11.0f, 0.0f, UiColor{0.0f, 0.0f, 0.0f, 0.7f}, sz);
      UiText(fX0, fYLine + 12.0f, UI_FACE_BOLD, 11.0f, 0.0f, c, sz);
    }
    bDrew = true;
  }

  // the idle workers' icon, fixed above the minimap's corner, only while there are some
  if (nIdle > 0) {
    IdleBadge(fX + 6.0f, fY + fH - fH * 90.0f / 480.0f - 4.0f, fW, fH, nIdle, static_cast<float>(fH) / 480.0f);
    bDrew = true;
  }
  return bDrew;
}

// A click on the idle workers' icon selects the next idle worker, as the ` key does.
bool Mouse(unsigned uMsg, unsigned long long wParam, long long lParam)
{
  if (!g_bIdleBadge || uMsg != 0x201)  // WM_LBUTTONDOWN
    return false;
  const float mx = static_cast<float>(lParam & 0xFFFF);
  const float my = static_cast<float>((lParam >> 16) & 0xFFFF);
  if (mx < g_fIdleX || mx > g_fIdleX + g_fIdleW || my < g_fIdleY || my > g_fIdleY + g_fIdleH)
    return false;
  const char *pszWhy = NULL;
  PortHudPressButton(PORT_HUD_BTN_IDLE, &pszWhy);
  return true;
}

// The layer over the game (port_overlay.h): nothing holds the player's input, and the
// picture is just the numbers over the buildings and the idle workers' icon.
const PortOverlayLayer kLayer = {"mineinfo", PORT_OVERLAY_DEBUG + 20, 0, Shown, NULL, Draw,
                                 NULL, Mouse, NULL, NULL, NULL};
PORT_OVERLAY_LAYER(kLayer);

}  // namespace

// A start worker's first order, SC2's split (the user, 2026-11-02): each worker of the
// start goes to its own mineral field, the base's nearest ones in the round, where 1.17's
// start lets them all crowd the nearest. The game records the option (HX_OPT_START_SPLIT)
// in its rule record, so a replay of a game that split starts its workers the same way, and
// one recorded before it keeps 1.17's, to the frame; a loaded game never places its workers
// again, and a classic game does not carry the option.
void PortStartWorkerMine(CUnit *pWorker, int nWorker)
{
  if (gpLoadGameFile != NULL || (g_hxRuleOptions & HX_OPT_START_SPLIT) == 0)
    return;
  // the worker's town hall, the nearest completed one it belongs to
  CUnit *pHall = NULL;
  long long llBest = -1;
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
    if (p->bPlayer != pWorker->bPlayer || (p->dwFlags & 1) == 0 || !IsTownHall(p->wType) ||
        p->pSprite == NULL || pWorker->pSprite == NULL)
      continue;
    const long long dx = static_cast<long long>(p->pSprite->wPosX) - pWorker->pSprite->wPosX;
    const long long dy = static_cast<long long>(p->pSprite->wPosY) - pWorker->pSprite->wPosY;
    const long long d = dx * dx + dy * dy;
    if (llBest < 0 || d < llBest) {
      llBest = d;
      pHall = p;
    }
  }
  if (pHall == NULL || pHall->pSprite == NULL)
    return;
  // its mineral fields, nearest first, and the worker's one in the round
  std::vector<CUnit *> ap;
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext)
    if (IsMineralField(p->wType) && p->pSprite != NULL)
      ap.push_back(p);
  std::sort(ap.begin(), ap.end(), [pHall](CUnit *a, CUnit *b) {
    const long long ax = static_cast<long long>(a->pSprite->wPosX) - pHall->pSprite->wPosX;
    const long long ay = static_cast<long long>(a->pSprite->wPosY) - pHall->pSprite->wPosY;
    const long long bx = static_cast<long long>(b->pSprite->wPosX) - pHall->pSprite->wPosX;
    const long long by = static_cast<long long>(b->pSprite->wPosY) - pHall->pSprite->wPosY;
    return ax * ax + ay * ay < bx * bx + by * by;
  });
  if (ap.empty())
    return;
  pWorker->SetOrder_CUnit(0x4F, ap[nWorker % ap.size()]);
}
