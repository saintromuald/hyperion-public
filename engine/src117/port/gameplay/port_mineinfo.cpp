// port_mineinfo.cpp -- the mining efficiency a town hall shows over it (port_mineinfo.h), as
// StarCraft II's worker count does: the number of workers mining the base over the number
// that would saturate it, and the efficiency that comes of it. The user, 2026-11-02, asked
// for a clean-room one (their Cosmonarchy has its own): the game's own tables and units give
// every figure, and the mathematics is Hyperion's.
//
// Each of the local player's town halls gets its mineral fields and geysers, the resources
// nearest to it, and each patch a saturation count from its distance: a worker's trip is
// the walk there and back plus its harvest, and the patch is saturated when enough workers'
// harvests fill the whole trip, at most three a patch (the fourth takes what a busy patch
// cannot give). The number over the building is the workers on the base's patches now (the
// walk to, the harvest at, the walk back and the return are all its) over the saturation
// count, and, while the patches' work falls short of it, the efficiency it comes to, in
// SC2's green, yellow and red.
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
#include "../render/port_camera.h"
#include "../front/port_ui.h"
#include "../../h/hxrules.h"  // HxRules: the start workers split in a game by Hyperion's rules
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
  int nBase;
  int nX, nY;   // its centre, in map pixels
  int nSat;     // the workers that saturate it, 1 to 3
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

// The workers a patch takes to saturate: the trip, the walk there and back plus one dock,
// over the dock; at least one, at most three.
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
    if (IsGeyser(p.pUnit->wType) && IsRefinery(pT->wType) && pT->pSprite != NULL &&
        abs(static_cast<int>(pT->pSprite->wPosX) - p.nX) < 64 &&
        abs(static_cast<int>(pT->pSprite->wPosY) - p.nY) < 64)
      return static_cast<int>(i);
  }
  return -1;
}

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

namespace {

bool Shown(void)
{
  return gwGameMode == 3 && gPlayback == 0;
}

bool Draw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  if (!Shown() || fW <= 0 || fH <= 0)
    return false;
  const int nPlayer = static_cast<int>(g_LocalNationID);

  // the local player's completed town halls, and one worker for the walk's speed
  std::vector<CUnit *> abase;
  const CUnit *pWorker = NULL;
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
    if (p->bPlayer != nPlayer || (p->dwFlags & 1) == 0 || p->pSprite == NULL)
      continue;
    if (IsTownHall(p->wType))
      abase.push_back(p);
    if (pWorker == NULL && IsWorkerType(p->wType))
      pWorker = p;
  }
  if (abase.empty())
    return false;
  const int nPxFrame = pWorker != NULL ? WorkerPxPerFrame(pWorker) : 0;

  // the patches, each the nearest town hall's; a geyser with no refinery on it yet is
  // still its base's (its workers come when the refinery is up)
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
    if (llBest > 0) {
      // the square root, whole pixels, as the game's own distance wants no better
      nDist = static_cast<long long>(sqrt(static_cast<double>(llBest)));
    }
    Patch patch;
    patch.pUnit = p;
    patch.nBase = nBest;
    patch.nX = x;
    patch.nY = y;
    patch.nSat = Saturate(static_cast<int>(nDist), nPxFrame);
    patch.nOn = 0;
    ap.push_back(patch);
  }

  // the workers on each patch, and the ones walking back to a town hall
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

  // the words over each town hall: the workers over the saturation count, and, while the
  // patches' work falls short, the efficiency it comes to. The camera's anchor and zoom put
  // each world point on the window: the game box shows nWidth by nViewRows game pixels, and
  // a map pixel is a game pixel divided by the zoom, off the anchor.
  double fCamX, fCamY, fZoom;
  PortViewCamera(&fCamX, &fCamY, &fZoom);
  const double fSx = fW / g_portView.nWidth / fZoom;
  const double fSy = fH / 480.0 / fZoom;
  bool bDrew = false;
  for (size_t b = 0; b < abase.size(); ++b) {
    if (abase[b]->pSprite == NULL)
      continue;
    int nOn = anBack[b];
    int nOpt = 0;
    int nWork = 0;
    for (const Patch &p : ap)
      if (p.nBase == static_cast<int>(b)) {
        nOpt += p.nSat;
        nOn += p.nOn;
        nWork += p.nOn < p.nSat ? p.nOn : p.nSat;
      }
    if (nOpt <= 0)
      continue;
    const int nPercent = nWork * 100 / nOpt;
    const UiColor c = nPercent >= 90 ? UiColor{0.24f, 0.83f, 0.24f, 1.0f}
                      : nPercent >= 50 ? UiColor{0.95f, 0.75f, 0.1f, 1.0f}
                                       : UiColor{0.9f, 0.3f, 0.2f, 1.0f};

    const double fXw = static_cast<int>(abase[b]->pSprite->wPosX);
    const double fYw = static_cast<int>(abase[b]->pSprite->wPosY);
    const double fSx0 = fX + (fXw - fCamX) * fSx;
    const double fSy0 = fY + (fYw - fCamY) * fSy;
    if (fSx0 < fX - 80 || fSx0 > fX + fW + 80 || fSy0 < fY - 60 || fSy0 > fY + fH)
      continue;

    char sz[32];
    snprintf(sz, sizeof sz, "%d / %d", nOn, nOpt);
    const float fSize = 14.0f;
    const float fWid = UiTextWidth(UI_FACE_BOLD, fSize, 0.0f, sz);
    // the shadow first, then the number, above the building's top
    const float fX0 = static_cast<float>(fSx0 - fWid / 2);
    const float fY0 = static_cast<float>(fSy0 - 44);
    UiText(fX0 + 1.0f, fY0 + 1.0f, UI_FACE_BOLD, fSize, 0.0f, UiColor{0.0f, 0.0f, 0.0f, 0.7f}, sz);
    UiText(fX0, fY0, UI_FACE_BOLD, fSize, 0.0f, c, sz);
    if (nPercent < 100) {
      snprintf(sz, sizeof sz, "%d%%", nPercent);
      const float fWid2 = UiTextWidth(UI_FACE_BOLD, 11.0f, 0.0f, sz);
      UiText(static_cast<float>(fSx0 - fWid2 / 2) + 1.0f, fY0 - 14.0f + 1.0f, UI_FACE_BOLD, 11.0f, 0.0f,
             UiColor{0.0f, 0.0f, 0.0f, 0.7f}, sz);
      UiText(static_cast<float>(fSx0 - fWid2 / 2), fY0 - 14.0f, UI_FACE_BOLD, 11.0f, 0.0f, c, sz);
    }
    bDrew = true;
  }
  return bDrew;
}

// The layer over the game (port_overlay.h): nothing holds the player's input, and the
// picture is just the numbers over the buildings.
const PortOverlayLayer kLayer = {"mineinfo", PORT_OVERLAY_DEBUG + 20, 0, Shown, NULL, Draw,
                                 NULL, NULL, NULL, NULL, NULL};
PORT_OVERLAY_LAYER(kLayer);

}  // namespace
