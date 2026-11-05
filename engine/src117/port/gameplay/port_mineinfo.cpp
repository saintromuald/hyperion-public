// port_mineinfo.cpp -- the mining a town hall shows over it, and the idle workers' icon
// (port_mineinfo.h), as StarCraft II's worker count and idle worker icon do. The user,
// 2026-11-02, asked for a clean-room one (their Cosmonarchy has its own): the game's own
// tables and units give every figure, and the mathematics is Hyperion's.
//
// Over each of the local player's town halls one simple number shows, the minerals', as
// StarCraft II's clean worker count: the workers on the base's mineral patches over the
// maximum, three a patch - 1 / 24 for one worker on an eight-patch base - and each vespene
// geyser shows its own over three. At the maximum the number takes the player's own
// colour, the one their race starts in; under it the number is plain white.
//
// Above the bottom console - on top of it, not on it - a small fixed icon shows the idle
// workers when there are any, with the count, as SC2's does; a click takes the view to the
// next idle worker, as the ` key and the HUD's button do.
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
#include "../hud/port_hudstate.h"  // PortHudPressButton, the idle icon's click; PortSelectLarva
#include "../front/port_text.h"   // PortText, the pick's words
#include "../render/port_console.h"  // PortConsoleHudOn, PortConsoleHidden: the card counts are the classic console's
#include "../../h/net_cmds.h"     // PlayerData, the saturated number's colour
#include "../../h/hxrules.h"  // HX_OPT_START_SPLIT, the start workers split's option
#include "../render/port_teamcolor.h"  // PortTeamColorGet, the saturated number's colour
#include "../platform/port_settings.h"  // PortSettingNumber, the energy bars' setting
#include "../../h/playervision.h"  // SpriteSeenBy, an enemy caster's bar only when seen
#include "port_camkeys.h"  // PortCamKeysLocation, the saved cameras' minimap labels
#include "port_onecard.h"  // the classic console's type-card count labels
#include "../../h/rect32.h"   // rect32_t, the minimap's cage and the console's
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"  // oc_dwReplayFrame, the frame the scan runs on

// the minimap's cage rectangle, as port_hudinput.cpp spells it (minimap.cpp, 0x508BB0)
#ifdef OC_DLL_MODE
#define sgMinimapArea_g (*reinterpret_cast<rect32_t *>(OC_VA(0x508BB0u)))
#else
extern rect32_t sgMinimapArea_g;
#endif
// the console's panel rectangle, the command card's (port_view.cpp, 0x5033D0): its top
// is the bottom console's top edge, which the idle workers' icon hangs above
#ifdef OC_DLL_MODE
#define sgConsoleAreaRect_g (*reinterpret_cast<rect32_t *>(OC_VA(0x5033D0u)))
#else
extern rect32_t sgConsoleAreaRect_g;
#endif

// The classic console's visible type cards: statdraw_draw_multi sets their count labels
// (port_onecard.h).
int g_nSelectionCardLabels = 0;
SelectionCardLabel g_aSelectionCardLabels[SELECTION_PAGE];

namespace {

// The unit types the picture needs (arr/units.dat's order).
bool IsWorkerType(int nType) { return nType == 0x07 || nType == 0x29 || nType == 0x40; }
bool IsTownHall(int nType) {
  return nType == 0x6A || nType == 0x83 || nType == 0x84 || nType == 0x85 || nType == 0x9A;
}
bool IsMineralField(int nType) { return nType >= 0xB0 && nType <= 0xB2; }
bool IsGeyser(int nType) { return nType == 0xBC; }
bool IsRefinery(int nType) { return nType == 0x6E || nType == 0x95 || nType == 0x9D; }

// The workers a patch takes to its maximum, Brood War's own figure (the user, 2026-11-03;
// Liquipedia's Mining page): a mineral patch's third worker mines slightly more than the
// first two do, at about half the rate - some 102 a minute against 78 to 90 - so three a
// patch is the patch's maximum, and a geyser takes three. The number over the town hall is
// the workers on the base's mineral patches over that maximum: 1 / 24 for one worker on an
// eight-patch base, and each geyser's over its own three.

// A patch the base mines: its position, the town hall it belongs to, how many workers it
// takes to saturate, and how many are on it now.
struct Patch {
  CUnit *pUnit;
  bool bGas;
  int nBase;
  int nX, nY;   // its centre, in map pixels
  int nSat;     // the workers at its maximum: three a mineral patch, three a geyser
  int nOn;      // the workers on it now
};

// The worker's patch, or -1. Its persistent harvest link comes first: it still names the
// resource while the worker carries gas back, when the immediate order targets the town hall.
// The order target remains a fallback for a worker approaching a patch before linking to it.
// A refinery target finds the geyser it sits on.
int PatchOf(const CUnit *pWorker, const std::vector<Patch> &ap)
{
  const CUnit *apTarget[2] = {pWorker->pUnionD0, pWorker->pOrderTarget};
  for (const CUnit *pT : apTarget) {
    if (pT == NULL)
      continue;
    for (size_t i = 0; i < ap.size(); ++i) {
      const Patch &p = ap[i];
      if (pT == p.pUnit)
        return static_cast<int>(i);
      // the geyser's refinery stands on it: a worker mining gas links to the refinery
      if (p.bGas && IsRefinery(pT->wType) && pT->pSprite != NULL &&
          abs(static_cast<int>(pT->pSprite->wPosX) - p.nX) < 64 &&
          abs(static_cast<int>(pT->pSprite->wPosY) - p.nY) < 64)
        return static_cast<int>(i);
    }
  }
  return -1;
}

bool Shown(void)
{
  return gwGameMode == 3 && gPlayback == 0;
}

// The local player's own colour, the one their race starts in: the slot's full-RGB colour,
// its chosen one or the classic ramp's. The saturated numbers take it (the user, 2026-11-02).
UiColor RaceColour(void)
{
  const int nSlot = static_cast<int>(g_LocalNationID);
  unsigned char abRgb[3] = {255, 255, 255};
  if (nSlot >= 0 && nSlot < 12)
    PortTeamColorGet(nSlot, abRgb);
  return UiColor{abRgb[0] / 255.0f, abRgb[1] / 255.0f, abRgb[2] / 255.0f, 1.0f};
}

// The idle workers' icon's place, fixed above the bottom console, right of the minimap,
// as SC2's; the count beside the little worker drawn in the box. The Mouse hook answers
// clicks there.
bool g_bIdleBadge;
float g_fIdleX, g_fIdleY, g_fIdleW, g_fIdleH;

// The larvae the player asked for, by Ctrl+Space: a small box above the console with the
// count they type; Enter selects that many larvae (all, with none typed), Escape closes.
bool g_bLarvaPick;
std::string g_larvaText;

// The pick's keys: everything waits while it is up. Enter, Backspace and Escape come on the
// key going down (a taken keydown makes no character), the digits as the characters the
// keyboard's input makes.
bool Key(unsigned vk, bool bDown, bool bRepeat)
{
  if (!g_bLarvaPick)
    return false;
  if (bDown) {
    if (!bRepeat && vk == VK_RETURN) {  // Enter: the count typed, or all with none
      g_bLarvaPick = false;
      const char *pszWhy = NULL;
      PortSelectLarva(g_larvaText.empty() ? -1 : atoi(g_larvaText.c_str()), &pszWhy);
    } else if (!bRepeat && vk == VK_BACK && !g_larvaText.empty()) {  // Backspace
      g_larvaText.pop_back();
    } else if (!bRepeat && vk == VK_ESCAPE) {
      g_bLarvaPick = false;
    }
    return true;
  }
  if (vk >= '0' && vk <= '9' && g_larvaText.size() < 3)
    g_larvaText += static_cast<char>(vk);
  return true;
}

// The pick's box: the count so far, and its two keys.
void LarvaPick(float fX, float fY, float fW, float fH)
{
  const float fScale = fH / 480.0f;
  const float bw = 200.0f * fScale, bh = 52.0f * fScale;
  const float bx = fX + (fW - bw) * 0.5f, by = fY + fH - 170.0f * fScale;
  UiFill(bx, by, bw, bh, UiColor{0.0f, 0.0f, 0.0f, 0.8f});
  UiRing(bx, by, bw, bh, RaceColour(), 5.0f, 1.5f);
  const std::string s = PortText("Larvae: ") + (g_larvaText.empty() ? std::string("_") : g_larvaText);
  UiText(bx + 14.0f * fScale, by + 20.0f * fScale, UI_FACE_BOLD, 15.0f * fScale, 0.0f,
         UiColor{1.0f, 1.0f, 1.0f, 1.0f}, s.c_str());
  const std::string hint = PortText("Enter selects Â· Esc cancels");
  UiText(bx + 14.0f * fScale, by + 40.0f * fScale, UI_FACE_TEXT, 11.0f * fScale, 0.0f,
         UiColor{0.75f, 0.75f, 0.75f, 1.0f}, hint.c_str());
}


// The small badge: a box, a worker (a head and a body), and the count; drawn always, the
// worker and the count in SC2's idle colour when there are idle workers, grey at none, so
// "idle 0" is as visible as any count (the user, 2026-11-02).
void IdleBadge(float fX, float fY, float fW, float fH, int nCount, float fScale)
{
  const float fSize = 11.0f * fScale;  // half its first size (the user, 2026-11-02)
  g_fIdleX = fX;
  g_fIdleY = fY - fSize;
  g_fIdleW = fSize + 34.0f * fScale;
  g_fIdleH = fSize;
  g_bIdleBadge = true;
  const UiColor c = nCount > 0 ? UiColor{0.9f, 0.7f, 0.15f, 1.0f} : UiColor{0.5f, 0.5f, 0.5f, 0.75f};
  UiFill(g_fIdleX, g_fIdleY, g_fIdleW, g_fIdleH, UiColor{0.0f, 0.0f, 0.0f, 0.55f});
  UiRing(g_fIdleX, g_fIdleY, g_fIdleW, g_fIdleH, c, 4.0f, 1.5f);
  // the worker: a head above a body
  const float cx = g_fIdleX + fSize / 2;
  UiFill(cx - fSize * 0.12f, g_fIdleY + fSize * 0.28f, fSize * 0.24f, fSize * 0.5f, c);
  UiRing(cx, g_fIdleY + fSize * 0.16f, fSize * 0.3f, fSize * 0.3f, c,
         fSize * 0.15f, fSize * 0.11f);
  char sz[8];
  snprintf(sz, sizeof sz, "%d", nCount);
  UiText(g_fIdleX + fSize + 8.0f * fScale, g_fIdleY + fSize * 0.62f, UI_FACE_BOLD, fSize * 0.62f, 0.0f,
         nCount > 0 ? UiColor{1.0f, 1.0f, 1.0f, 0.95f} : UiColor{0.7f, 0.7f, 0.7f, 0.8f}, sz);
}

bool Draw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  g_bIdleBadge = false;
  if (!Shown() || fW <= 0 || fH <= 0) {
    g_bLarvaPick = false;
    return false;
  }
  const int nPlayer = static_cast<int>(g_LocalNationID);

  // The scan of the unit table is the picture's whole cost - three walks of the used
  // list, and a big no-cap game holds thousands of units - so it runs on a beat, every
  // sixth frame, and the frames between draw what it found; a fresh game starts it
  // again. A counter does not move more between the beats than a worker walks in a
  // quarter of a second, and the player's keys do not wait on the scan (the spell keys'
  // jank, the user, 2026-11-03).
  // a base's number, gathered on the scan's beat: its group, the workers on its mineral
  // patches over the maximum, and the hall nearest the minerals to show it on
  struct BaseInfo {
    int nShow;
    int nOn;
    int nOpt;
  };
  static std::vector<CUnit *> s_abase;
  static std::vector<Patch> s_ap;
  static std::vector<int> s_anBack;
  // a nydus canal of the player's, for its number and the minimap's link: its place and
  // its exit's, in map pixels; the exit is the canal's pUnionD0 (CanTravelThroughNydus)
  struct NydusLink {
    int nX, nY;
    int nExitX, nExitY;
    bool bExit;
  };
  static std::vector<NydusLink> s_aNydus;
  static std::vector<int> s_aGroup;
  static std::vector<BaseInfo> s_aBase;
  // a spellcaster the local player can see, for its energy bar: its sprite's place and
  // size in map pixels and its energy, gathered on the scan's beat (the user, 2026-11-05)
  struct CasterBar {
    int nX, nY;
    int nW, nH;
    int nNow, nMax;
  };
  static std::vector<CasterBar> s_aBars;
  static double s_fEnergyBars = 1.0;  // Settings' Interface toggle, on until it says no
  static int s_nIdle = 0;
  static unsigned int s_nLastFrame = 0;
  static unsigned int s_nNextScan = 0;
  const unsigned int nFrame = oc_dwReplayFrame;
  if ((s_abase.empty() && s_ap.empty()) || nFrame < s_nLastFrame || nFrame >= s_nNextScan) {
    s_abase.clear();
    s_ap.clear();

    // the local player's completed town halls and the idle workers (the ones standing
    // guard, as the HUD's count takes them)
    int nIdle = 0;
    for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
      if (p->bPlayer != nPlayer || p->pSprite == NULL)
        continue;
      if ((p->dwFlags & 1) != 0 && IsTownHall(p->wType))
        s_abase.push_back(p);
      if (IsWorkerType(p->wType) && (p->dwFlags & 1) != 0 && (p->bOrderId == 0x02 || p->bOrderId == 0x03))
        ++nIdle;
    }

    // the patches, each the nearest town hall's. A geyser whose refinery is built is the
    // refinery itself now - ReplaceGeyser turns the geyser's unit into it - so the built
    // geysers take their number too, the refinery's patch in the geyser's place.
    for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
      const bool bRefinery = IsRefinery(p->wType);
      if (!IsMineralField(p->wType) && !IsGeyser(p->wType) && !(bRefinery && p->bPlayer == nPlayer))
        continue;
      if (p->pSprite == NULL)
        continue;
      const int x = static_cast<int>(p->pSprite->wPosX);
      const int y = static_cast<int>(p->pSprite->wPosY);
      // only a base's own patches count: the nearest hall, and only within its reach,
      // so the fields of the whole map never join the first base's number (the user,
      // 2026-11-03). Eight tiles: a macro hatchery at the same mineral line shares it,
      // the next mineral line over (ten tiles and more on the stock maps) does not.
      const int kReach = 8 * 32;
      int nBest = -1;
      long long llBest = -1;
      for (size_t b = 0; b < s_abase.size(); ++b) {
        if (s_abase[b]->pSprite == NULL)
          continue;
        const long long dx = x - static_cast<int>(s_abase[b]->pSprite->wPosX);
        const long long dy = y - static_cast<int>(s_abase[b]->pSprite->wPosY);
        const long long d = dx * dx + dy * dy;
        if (d > static_cast<long long>(kReach) * kReach)
          continue;
        if (llBest < 0 || d < llBest) {
          llBest = d;
          nBest = static_cast<int>(b);
        }
      }
      if (nBest < 0 && !bRefinery)
        continue;  // no base of the player's within reach: not its minerals. The
                   // player's own refineries are collected wherever they stand, so a
                   // built geyser always takes its number even far from a hall
      Patch patch;
      patch.pUnit = p;
      patch.bGas = IsGeyser(p->wType) || bRefinery;
      patch.nBase = nBest;
      patch.nX = x;
      patch.nY = y;
      patch.nSat = 3;  // the maximum: three a mineral patch, three a geyser (the user, 2026-11-03)
      patch.nOn = 0;
      s_ap.push_back(patch);
    }
    // a geyser a refinery stands on is in the list twice, as itself and as its refinery;
    // the refinery's patch stays, the geyser's goes
    std::vector<Patch> apKeep;
    for (const Patch &q : s_ap) {
      if (q.bGas && q.pUnit->wType == 0xBC) {
        bool bCovered = false;
        for (const Patch &r : s_ap)
          if (r.bGas && r.pUnit->wType != 0xBC &&
              abs(r.nX - q.nX) < 64 && abs(r.nY - q.nY) < 64) {
            bCovered = true;
            break;
          }
        if (bCovered)
          continue;
      }
      apKeep.push_back(q);
    }
    s_ap.swap(apKeep);

    // the workers on each patch, and the ones walking back to a town hall (their load is
    // the minerals', most often)
    s_anBack.assign(s_abase.size(), 0);
    for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
      if (p->bPlayer != nPlayer || !IsWorkerType(p->wType))
        continue;
      const int nPatch = PatchOf(p, s_ap);
      if (nPatch >= 0) {
        ++s_ap[nPatch].nOn;
        continue;
      }
      if (p->pOrderTarget != NULL && p->bCarryResource != 1)  // the gas carriers, off
        for (size_t b = 0; b < s_abase.size(); ++b)
          if (p->pOrderTarget == s_abase[b]) {
            ++s_anBack[b];
            break;
          }
    }
    // the bases: halls within eight tiles of one another are one base (a macro hatchery
    // at the same mineral line), each hall tested against any earlier hall of a group
    // so a chain of halls merges however they are ordered
    s_aGroup.assign(s_abase.size(), -1);
    for (size_t k = 0; k < s_abase.size(); ++k) {
      if (s_abase[k]->pSprite == NULL)
        continue;
      for (size_t j = 0; j < k; ++j) {
        if (s_aGroup[j] < 0 || s_abase[j]->pSprite == NULL)
          continue;
        const double dx = static_cast<double>(s_abase[k]->pSprite->wPosX) - s_abase[j]->pSprite->wPosX;
        const double dy = static_cast<double>(s_abase[k]->pSprite->wPosY) - s_abase[j]->pSprite->wPosY;
        if (dx * dx + dy * dy > static_cast<double>(8 * 32) * (8 * 32))
          continue;
        if (s_aGroup[k] < 0) {
          s_aGroup[k] = s_aGroup[j];  // the first base this hall touches
        } else if (s_aGroup[k] != s_aGroup[j]) {
          // it touches a second base too: the two are one, so the chain of halls merges
          // however the halls are ordered (the critic's counterexample)
          const int nOld = s_aGroup[k];
          for (size_t m = 0; m <= k; ++m)
            if (s_aGroup[m] == nOld)
              s_aGroup[m] = s_aGroup[j];
        }
      }
      if (s_aGroup[k] < 0)
        s_aGroup[k] = static_cast<int>(k);
    }
    // each base's number: its workers over three a patch, capped, and the hall nearest
    // its minerals' middle to show it on (the user, 2026-11-03)
    s_aBase.clear();
    for (size_t b = 0; b < s_abase.size(); ++b) {
      if (s_aGroup[b] != static_cast<int>(b) || s_abase[b]->pSprite == NULL)
        continue;
      BaseInfo base;
      base.nShow = static_cast<int>(b);
      base.nOn = 0;
      base.nOpt = 0;
      double fMidX = 0.0, fMidY = 0.0;
      int nPatches = 0;
      for (const Patch &p : s_ap) {
        if (p.nBase < 0 || p.nBase >= static_cast<int>(s_abase.size()) || s_aGroup[p.nBase] != static_cast<int>(b))
          continue;
        if (p.bGas)
          continue;
        base.nOn += p.nOn;
        base.nOpt += p.nSat;
        fMidX += p.nX;
        fMidY += p.nY;
        ++nPatches;
      }
      if (nPatches == 0 || base.nOpt <= 0)
        continue;
      for (size_t k = 0; k < s_abase.size(); ++k)
        if (s_aGroup[k] == static_cast<int>(b))
          base.nOn += s_anBack[k];
      if (base.nOn > base.nOpt)
        base.nOn = base.nOpt;
      fMidX /= nPatches;
      fMidY /= nPatches;
      double fBest = -1.0;
      for (size_t k = 0; k < s_abase.size(); ++k) {
        if (s_aGroup[k] != static_cast<int>(b) || s_abase[k]->pSprite == NULL)
          continue;
        const double dx = static_cast<double>(s_abase[k]->pSprite->wPosX) - fMidX;
        const double dy = static_cast<double>(s_abase[k]->pSprite->wPosY) - fMidY;
        const double d = dx * dx + dy * dy;
        if (fBest < 0.0 || d < fBest) {
          fBest = d;
          base.nShow = static_cast<int>(k);
        }
      }
      s_aBase.push_back(base);
    }

    // the player's completed nydus canals (0x86), numbered in the used list's order: the
    // number over each, and on the minimap the exit's N and the line linking them (the
    // user, 2026-11-04)
    s_aNydus.clear();
    for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
      if (p->bPlayer != nPlayer || p->wType != 0x86 || (p->dwFlags & 1) == 0 || p->pSprite == NULL)
        continue;
      NydusLink link;
      link.nX = static_cast<int>(p->pSprite->wPosX);
      link.nY = static_cast<int>(p->pSprite->wPosY);
      link.bExit = p->pUnionD0 != NULL && p->pUnionD0->pSprite != NULL;
      link.nExitX = link.bExit ? static_cast<int>(p->pUnionD0->pSprite->wPosX) : 0;
      link.nExitY = link.bExit ? static_cast<int>(p->pUnionD0->pSprite->wPosY) : 0;
      s_aNydus.push_back(link);
    }

    // the spellcasters in the local player's sight, their own or another player's alike:
    // the bars' rule is the bars' setting, and the fog keeps an unseen enemy's energy
    // hidden, but a seen enemy caster's bar shows as one's own does (the user, 2026-11-05)
    {
      double d = 1.0;
      PortSettingNumber("hyperion", "energyBars", &d);
      s_fEnergyBars = d;
    }
    s_aBars.clear();
    for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
      if (p->pSprite == NULL || (gulUnitTypeFlags[p->wType] & 0x200000) == 0 ||
          (p->dwFlags & 0x40000000) != 0)
        continue;  // a caster, never a hallucination
      if (!SpriteSeenBy(p->pSprite, nPlayer))
        continue;
      CasterBar bar;
      bar.nX = static_cast<int>(p->pSprite->wPosX);
      bar.nY = static_cast<int>(p->pSprite->wPosY);
      bar.nW = SpriteGrpWidth(p->pSprite);
      bar.nH = SpriteGrpHeight(p->pSprite);
      bar.nMax = p->GetMaxEnergyReal();
      if (bar.nMax <= 0)
        continue;
      bar.nNow = static_cast<int>(p->wEnergy) >> 8;
      if (bar.nNow > bar.nMax)
        bar.nNow = bar.nMax;
      if (bar.nNow < 0)
        bar.nNow = 0;
      s_aBars.push_back(bar);
    }

    s_nIdle = nIdle;
    s_nNextScan = nFrame + 6;
  }
  s_nLastFrame = nFrame;

  // the camera's anchor and zoom put each world point on the window: the game box shows
  // nWidth by 480 game pixels, and a map pixel is a game pixel divided by the zoom, off
  // the anchor. The picture's motion can scale the zoom to nothing for a moment, which
  // would put the numbers where no number is (and the layout cannot take that), so the
  // numbers wait for a finite camera.
  double fCamX, fCamY, fZoom;
  PortViewCamera(&fCamX, &fCamY, &fZoom);
  const bool bCamOk = fZoom > 0.0 && isfinite(fZoom) && isfinite(fCamX) && isfinite(fCamY);
  const double fSx = bCamOk ? fW / g_portView.nWidth / fZoom : 0.0;
  const double fSy = bCamOk ? fH / 480.0 / fZoom : 0.0;
  const float fScale = static_cast<float>(fH) / 480.0f;
  bool bDrew = false;

  // the number's colour: fully saturated, the player's own colour, the one their race
  // starts in; under it, plain white, as StarCraft II's counter sits (the user, 2026-11-02)
  const auto colour = [](int nOn, int nOpt) -> UiColor {
    if (nOn >= nOpt)
      return RaceColour();
    return UiColor{1.0f, 1.0f, 1.0f, 0.95f};
  };

  // over each base's main hall the minerals' number, the scan's finding: the workers on
  // the base's patches over three a patch - 0 / 24 on an eight-patch base - capped at the
  // maximum, so a worker walking back does not push the counter past it (the user,
  // 2026-11-03)
  for (const BaseInfo &base : s_aBase) {
    if (!bCamOk || base.nShow < 0 || base.nShow >= static_cast<int>(s_abase.size()))
      continue;
    CUnit *pHall = s_abase[base.nShow];
    if (pHall->pSprite == NULL)
      continue;
    const double fXw = static_cast<int>(pHall->pSprite->wPosX);
    const double fYw = static_cast<int>(pHall->pSprite->wPosY);
    const double fSx0 = fX + (fXw - fCamX) * fSx;
    const double fSy0 = fY + (fYw - fCamY) * fSy;
    if (fSx0 < fX - 80 || fSx0 > fX + fW + 80 || fSy0 < fY - 60 || fSy0 > fY + fH)
      continue;
    char sz[16];
    const float fYLine = static_cast<float>(fSy0) - 46.0f;
    snprintf(sz, sizeof sz, "%d / %d", base.nOn, base.nOpt);
    const float fWid = UiTextWidth(UI_FACE_BOLD, 13.0f, 0.0f, sz);
    const float fX0 = static_cast<float>(fSx0) - fWid / 2;
    const UiColor c = colour(base.nOn, base.nOpt);
    UiText(fX0 + 1.0f, fYLine + 1.0f, UI_FACE_BOLD, 13.0f, 0.0f, UiColor{0.0f, 0.0f, 0.0f, 0.7f}, sz);
    UiText(fX0, fYLine, UI_FACE_BOLD, 13.0f, 0.0f, c, sz);
    bDrew = true;
  }

  // over each vespene geyser its own number: the workers on it over three, the same
  // principle as the bases' (the user, 2026-11-03). The player's own refineries always
  // show - they are the built geysers, for all three races - and an unbuilt geyser
  // shows only near one of the player's bases, so a scouted enemy's geyser takes no
  // 0 / 3.
  const int kBaseReach = 15 * 32;  // map pixels: a geyser sits a few tiles from its hall
  for (const Patch &p : s_ap) {
    if (!bCamOk)
      break;
    if (!p.bGas)
      continue;
    const bool bBuilt = p.pUnit->wType != 0xBC;  // the refinery's own unit on the geyser
    if (!bBuilt) {
      if (p.nBase < 0 || p.nBase >= static_cast<int>(s_abase.size()) || s_abase[p.nBase]->pSprite == NULL)
        continue;
      const double fDx = p.nX - static_cast<int>(s_abase[p.nBase]->pSprite->wPosX);
      const double fDy = p.nY - static_cast<int>(s_abase[p.nBase]->pSprite->wPosY);
      if (fDx * fDx + fDy * fDy > static_cast<double>(kBaseReach) * kBaseReach)
        continue;
    }
    const double fXw = p.nX;
    const double fYw = p.nY;
    const double fSx0 = fX + (fXw - fCamX) * fSx;
    const double fSy0 = fY + (fYw - fCamY) * fSy;
    if (fSx0 < fX - 60 || fSx0 > fX + fW + 60 || fSy0 < fY - 40 || fSy0 > fY + fH)
      continue;
    // Gas can be over-assigned: show the true count, including workers returning with gas.
    const int nOn = p.nOn;
    char sz[16];
    snprintf(sz, sizeof sz, "%d / %d", nOn, p.nSat);
    const float fWid = UiTextWidth(UI_FACE_BOLD, 11.0f, 0.0f, sz);
    const float fX0 = static_cast<float>(fSx0) - fWid / 2;
    const float fY0 = static_cast<float>(fSy0) - 30.0f;
    const UiColor c = colour(nOn, p.nSat);
    UiText(fX0 + 1.0f, fY0 + 1.0f, UI_FACE_BOLD, 11.0f, 0.0f, UiColor{0.0f, 0.0f, 0.0f, 0.7f}, sz);
    UiText(fX0, fY0, UI_FACE_BOLD, 11.0f, 0.0f, c, sz);
    bDrew = true;
  }

  // the minimap's window rect, and a map point on it: the cage in the console's design
  // pixels, scaled to the window, the map's tiles spanning it
  auto minimapAt = [&](int nMapX, int nMapY, float *pfX, float *pfY) {
    const float fw = static_cast<float>(sgMinimapArea_g.right - sgMinimapArea_g.left + 1);
    const float fh = static_cast<float>(sgMinimapArea_g.bottom - sgMinimapArea_g.top + 1);
    if (fw <= 0.0f || fh <= 0.0f || gwMapWidth <= 0 || gwMapHeight <= 0)
      return;
    const float fx = static_cast<float>(sgMinimapArea_g.left) + nMapX / 32.0f * fw / gwMapWidth;
    const float fy = static_cast<float>(sgMinimapArea_g.top) + nMapY / 32.0f * fh / gwMapHeight;
    *pfX = fX + fx * fScale;
    *pfY = fY + fy * fScale;
  };
  auto minimapText = [&](int nMapX, int nMapY, const char *psz, const UiColor &c,
                         float fSize = 8.0f, float fBelow = 0.0f) {
    float fx = 0.0f, fy = 0.0f;
    minimapAt(nMapX, nMapY, &fx, &fy);
    if (fx == 0.0f && fy == 0.0f)
      return;
    fSize *= fScale;
    fy += fBelow * fScale;
    const float fWid = UiTextWidth(UI_FACE_BOLD, fSize, 0.0f, psz);
    // UiText's y is the baseline: put the em box's middle on the point, so the label sits
    // on the saved camera's centre and not above it (the user, 2026-11-05)
    float fAscent = 0.0f, fDescent = 0.0f, fGap = 0.0f;
    UiFontMetrics(UI_FACE_BOLD, fSize, &fAscent, &fDescent, &fGap);
    const float fBase = fy + (fAscent + fDescent) * 0.5f;
    UiText(fx - fWid / 2 + 0.5f, fBase + 0.5f, UI_FACE_BOLD, fSize, 0.0f,
           UiColor{0.0f, 0.0f, 0.0f, 0.75f}, psz);
    UiText(fx - fWid / 2, fBase, UI_FACE_BOLD, fSize, 0.0f, c, psz);
  };
  auto minimapLine = [&](int nX0, int nY0, int nX1, int nY1, const UiColor &c) {
    float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
    minimapAt(nX0, nY0, &ax, &ay);
    minimapAt(nX1, nY1, &bx, &by);
    if ((ax == 0.0f && ay == 0.0f) || (bx == 0.0f && by == 0.0f))
      return;
    const double dx = bx - ax, dy = by - ay;
    const double len = sqrt(dx * dx + dy * dy);
    const int nSteps = len > 1.0 ? static_cast<int>(len / 1.5) : 1;
    const int nMax = nSteps < 119 ? nSteps : 119;
    for (int k = 0; k <= nMax; ++k) {
      const double t = nMax > 0 ? static_cast<double>(k) / nMax : 0.0;
      UiFill(static_cast<float>(ax + dx * t), static_cast<float>(ay + dy * t),
             0.65f * fScale, 0.65f * fScale, c);
    }
  };

  // the player's nydus canals, numbered: the number over each, and on the minimap the
  // exit's N and the line linking them, for up to nine (the user, 2026-11-04)
  for (size_t k = 0; k < s_aNydus.size() && k < 9; ++k) {
    const NydusLink &link = s_aNydus[k];
    char sz[8];
    snprintf(sz, sizeof sz, "%zu", k + 1);
    if (bCamOk) {
      const double fSx0 = fX + (link.nX - fCamX) * fSx;
      const double fSy0 = fY + (link.nY - fCamY) * fSy;
      if (fSx0 > fX - 40 && fSx0 < fX + fW + 40 && fSy0 > fY - 40 && fSy0 < fY + fH) {
        const float fSize = 6.0f;  // half the first world-label size
        const float fWid = UiTextWidth(UI_FACE_BOLD, fSize, 0.0f, sz);
        const float fX0 = static_cast<float>(fSx0) - fWid / 2;
        const float fY0 = static_cast<float>(fSy0) - 30.0f;
        UiText(fX0 + 1.0f, fY0 + 1.0f, UI_FACE_BOLD, fSize, 0.0f, UiColor{0.0f, 0.0f, 0.0f, 0.75f}, sz);
        UiText(fX0, fY0, UI_FACE_BOLD, fSize, 0.0f, RaceColour(), sz);
      }
    }
    if (link.bExit) {
      char szN[8];
      snprintf(szN, sizeof szN, "N%zu", k + 1);
      // Half the camera labels' size and a little below them; the link is only a subtle guide.
      minimapText(link.nExitX, link.nExitY, szN, RaceColour(), 4.0f, 4.0f);
      minimapLine(link.nX, link.nY, link.nExitX, link.nExitY, UiColor{1.0f, 1.0f, 1.0f, 0.35f});
    }
  }
  // the saved camera locations' small labels on the minimap: c1 for F1, c2 for F2, and so
  // on, so a newcomer sees where each camera key goes (the user, 2026-11-04)
  for (int n = 0; n < 8; ++n) {
    int nx = 0, ny = 0;
    if (!PortCamKeysLocation(n, &nx, &ny))
      continue;
    char sz[8];
    snprintf(sz, sizeof sz, "c%d", n + 1);
    minimapText(nx, ny, sz, UiColor{1.0f, 1.0f, 1.0f, 0.9f});
  }

  // the classic console's type cards: each label is that type's total over the whole
  // selection, not the number of cards or the selection's total (the user, 2026-11-04).
  // Only while the classic console is on the screen: the HUD's grid puts the counts in
  // its own cells, and a hidden console's rects would hang the text over the map.
  if (!PortConsoleHidden() && !PortConsoleHudOn() && StatUnits()[0] != NULL && StatUnits()[1] != NULL) {
    for (int i = 0; i < g_nSelectionCardLabels && i < SELECTION_PAGE; ++i) {
      const SelectionCardLabel &card = g_aSelectionCardLabels[i];
      char sz[16];
      snprintf(sz, sizeof sz, "x%d", card.nUnits);
      const float fSize = 11.0f * fScale;
      const float fWid = UiTextWidth(UI_FACE_BOLD, fSize, 0.0f, sz);
      const float fx = fX + (card.rc.right - 1) * fScale - fWid - 2.0f;
      const float fy = fY + card.rc.top * fScale + 2.0f;
      UiText(fx + 1.0f, fy + 1.0f, UI_FACE_BOLD, fSize, 0.0f, UiColor{0.0f, 0.0f, 0.0f, 0.8f}, sz);
      UiText(fx, fy, UI_FACE_BOLD, fSize, 0.0f, UiColor{1.0f, 1.0f, 1.0f, 0.95f}, sz);
    }
  }

  // the spellcasters' energy bars, over every caster in the local player's sight: over the
  // sprite, its energy now over its maximum, full or empty alike, so which casters hold
  // energy is read at a glance with nothing selected. Selected units' 1.17 energy row
  // (rle_draw_hpfloat) stays as it is; the Interface setting turns the bars off.
  if (s_fEnergyBars > 0.5 && bCamOk) {
    for (const CasterBar &b : s_aBars) {
      const double fSx0 = fX + (b.nX - fCamX) * fSx;
      const double fSy0 = fY + (b.nY - fCamY) * fSy;
      if (fSx0 < fX - 40 || fSx0 > fX + fW + 40 || fSy0 < fY - 60 || fSy0 > fY + fH)
        continue;
      const float fBarW = static_cast<float>(b.nW * fSx * 0.6);
      if (fBarW < 8.0f)
        continue;
      const float fBarH = 2.5f * fScale;
      const float fTop = static_cast<float>(fSy0 - b.nH * fSy) - fBarH - 4.0f;
      const float fX0 = static_cast<float>(fSx0) - fBarW / 2;
      UiFill(fX0, fTop, fBarW, fBarH, UiColor{0.02f, 0.05f, 0.12f, 0.75f});
      if (b.nNow > 0)
        UiFill(fX0, fTop, fBarW * static_cast<float>(b.nNow) / b.nMax, fBarH,
               UiColor{0.25f, 0.6f, 1.0f, 0.95f});
      bDrew = true;
    }
  }

  // the idle workers' icon, fixed above the bottom console's top edge - on top of it,
  // not on it - and right of the minimap's cage, clear of its clicks, always, the count
  // on it, in the window's own pixels, so it stays put and seen at any zoom (the user,
  // 2026-11-03)
  const int nConsoleTop = static_cast<int>(sgConsoleAreaRect_g.top);
  if (nConsoleTop >= 0 && nConsoleTop <= 480) {
    const float fBadgeBottom = fY + (nConsoleTop - 22.0f) * fScale;
    const float fBadgeX = fX + (sgMinimapArea_g.right + 6.0f) * fScale;
    IdleBadge(fBadgeX, fBadgeBottom, fW, fH, s_nIdle, fScale);
  }
  if (g_bLarvaPick)
    LarvaPick(fX, fY, fW, fH);
  bDrew = true;
  return bDrew;
}

// A click on the idle workers' icon selects the next idle worker, as the ` key does; a
// click anywhere waits while the larvae' pick is up.
bool Mouse(unsigned uMsg, unsigned long long wParam, long long lParam)
{
  if (g_bLarvaPick)
    return true;
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
const PortOverlayLayer kLayer = {"mineinfo", PORT_OVERLAY_DEBUG + 20, PORT_OVERLAY_BEFORE_ACCEL, Shown, NULL, Draw,
                                 NULL, Mouse, Key, NULL, NULL};
PORT_OVERLAY_LAYER(kLayer);

}  // namespace

// The larvae' pick, Ctrl+Space's box: the count the player types, Enter to take it.
void PortLarvaPickOpen(void)
{
  g_bLarvaPick = true;
  g_larvaText.clear();
}

// A game begins (port_camkeys.cpp's seed calls it): the pick does not follow into the new game.
void PortLarvaPickClose(void)
{
  g_bLarvaPick = false;
  g_larvaText.clear();
}

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
