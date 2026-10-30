// port_frontlobby.cpp -- the pieces of the design's lobby screens (port_frontlobby.h).
// Presentation only.
#include <windows.h>
#include <math.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_frontlobby.h"
#include "../gameplay/port_ai.h"         // AiLevelName
#include "Storm.h"           // SMemFree
#include "snd.h"             // load_game_data, load_array_data
#include "rect16.h"          // gUnitBoundBox's rows
#include "../../gen/globals117.h"  // gUnitBoundBox
#include "../platform/port_globals117.h"
#include "../data/port_formats.h"  // PortMegatilesLoad
#include "../data/port_stormbase.h"  // PortMemSize

namespace FrontKit {

// 1.17's player colours, in tunit.pcx's order (the units' brightest): the eight slots', and
// the three a row may also choose; the twelfth is the neutral player's
const unsigned kSlotColours[11] = {0xf40404, 0x0c48cc, 0x2cb494, 0x88409c, 0xf88c14, 0x703014,
                                   0xcce0d0, 0xfcfc38, 0x088008, 0xfcfc7c, 0xecc4b0};

std::string g_lbOpen;

void Radius(Decl &b, float r)
{
  b.d.cornerRadius.topLeft = b.d.cornerRadius.topRight = b.d.cornerRadius.bottomLeft =
      b.d.cornerRadius.bottomRight = Sc(r);
}

// The set-up's boxes (lobby.css .lb-box): SC2's inner container.
void InnerBox(Decl &b, float fRim)
{
  b.d.backgroundColor = Col(kLbInner);
  b.d.border = Border(kLbInnerRim, Fs(fRim), Fs(fRim), Fs(fRim), Fs(fRim));
  Radius(b, 4);
}

// A floating box at x, y in the panel, w by h in SC2's units (h 0 fits its content).
Decl At(const char *pszId, float x, float y, float w, float h)
{
  Decl b = Floating(pszId, 0, Sc(x), Sc(y), 21, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  b.d.layout.sizing.width = Fixed(Sc(w));
  if (h > 0)
    b.d.layout.sizing.height = Fixed(Sc(h));
  return b;
}

void Label(const char *psz, float fSize, UiColor c, UiFace face, float fSpacing)
{
  Text(psz, face, Fs(fSize), fSpacing, c);
}

Decl InBox(const char *pszId, float x, float y, float w, float h, float fGap)
{
  Decl b = At(pszId, x, y, w, h);
  b.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  b.d.layout.padding = Pad(Fs(18), Fs(18), Fs(15), Fs(15));
  if (fGap > 0)
    b.d.layout.childGap = Gap(Fs(fGap));
  InnerBox(b);
  return b;
}

Decl OptionRow(float fHeight)
{
  Decl r;
  r.d.layout.sizing.width = Grow();
  r.d.layout.sizing.height = Fixed(Sc(fHeight));
  r.d.layout.childGap = Gap(Fs(10));
  r.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  return r;
}

void OptionName(const char *pszName, const char *pszNote, float fTopPad)
{
  Decl l;
  l.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  l.d.layout.sizing.width = Grow();
  l.d.layout.padding = Pad(Fs(fTopPad), 0, 0, 0);
  Open(l);
  Text(pszName, UI_FACE_BOLD, Fs(17), 0.0f, kScInkLabel);
  if (pszNote != NULL && pszNote[0] != '\0')
    Text(pszNote, UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
  Close();
}

void OptionName(const char *pszName, const std::string &note, float fTopPad)
{
  OptionName(pszName, note.empty() ? NULL : Keep(note), fTopPad);
}

void BoxTitle(const char *psz)
{
  Text(Keep(Upper(psz)), UI_FACE_WIDE, Fs(16), 0.04f, kScInkHead);
}

Decl SlotGround(const char *pszId, int i, bool bCompact, bool bSeated)
{
  Decl row = Box(pszId, i);
  const bool bHot = Over(row.d.id);
  row.d.layout.sizing.width = Grow();
  row.d.layout.sizing.height = Fixed(Sc(bCompact ? 40 : 69));
  row.d.layout.padding = Pad(Fs(8), Fs(8), Fs(bCompact ? 2 : 5), Fs(bCompact ? 2 : 5));
  row.d.layout.childGap = Gap(Fs(10));
  row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  row.d.custom.customData = Custom(DrawSlot, kScButton, 0.0f, (bHot ? 1 : 0) | (bSeated ? 0 : 2) | (bCompact ? 4 : 0));
  return row;
}

void SlotNumber(const char *pszId, int i, bool bCompact)
{
  Decl num;
  num.d.id = Id(pszId, i);
  num.d.layout.sizing.width = Fixed(Sc(bCompact ? 26 : 30));
  num.d.layout.sizing.height = Fixed(Sc(bCompact ? 26 : 30));
  num.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  num.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  num.d.custom.customData = Custom(DrawNum, kScButton);
  Open(num);
  Text(Keep(std::to_string(i + 1)), UI_FACE_BOLD, Fs(bCompact ? 13 : 15), 0.0f, kScInkHi);
  Close();
}

void TextFitted(const std::string &s, float fSize, float fMin, float fWidth, UiColor c)
{
  while (fSize > fMin && UiTextWidth(UI_FACE_TEXT, roundf(UiPx(Fs(fSize))), 0.0f, s.c_str()) > fWidth)
    fSize -= 1;
  Text(Keep(Elide(s, UI_FACE_TEXT, Fs(fSize), fWidth)), UI_FACE_TEXT, Fs(fSize), 0.0f, c);
}

void SlotShade(const char *pszId, int i, unsigned rgb, const char *pszName, float fWidth)
{
  Decl tile = Box(pszId, i);
  tile.d.layout.sizing.width = Fixed(Sc(fWidth));
  tile.d.layout.sizing.height = Fixed(Sc(36));
  tile.d.layout.padding = Pad(Fs(8), Fs(8), 0, 0);
  tile.d.layout.childGap = Gap(Fs(8));
  tile.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  tile.d.border = Border(kScWellRim, 1, 1, 1, 1);
  Radius(tile, 5);
  Open(tile);
  Chip(rgb, 20);
  TextFitted(pszName, 14, 12, Sc(fWidth) - 3 * Sc(8) - Sc(20), kScInkMenu);
  Close();
}

void TeamChoices(const std::vector<std::string> &names, std::vector<std::string> *pItems,
                 std::vector<unsigned> *pChips)
{
  *pItems = names;
  pItems->push_back(PortText("New team"));
  pChips->clear();
  for (size_t q = 0; q <= names.size(); ++q)
    pChips->push_back(kTeamBases[q % 16]);
}

void QuickLayoutChoices(const std::vector<TeamLayout> &ls, std::vector<std::string> *pItems,
                        std::vector<int> *pWhich)
{
  pItems->clear();
  pWhich->clear();
  for (size_t k = 0; k < ls.size(); ++k)
    if (!ls[k].bFfa) {
      pItems->push_back(std::string(PortText(ls[k].name.c_str())) + kDot + ls[k].vs);
      pWhich->push_back(static_cast<int>(k));
    }
  if (pItems->empty())
    pItems->push_back(PortText("None for this map"));
}

const std::vector<std::string> &LevelNames(void)
{
  static std::vector<std::string> s_names;
  if (s_names.empty())
    for (int n = 0; n < AI_LEVELS; ++n)
      s_names.push_back(PortText(AiLevelName(n)));
  return s_names;
}

void PulldownCloseOutside(void)
{
  if (g_bReleased && !g_lbOpen.empty() && g_nPressedOn == 0)
    g_lbOpen.clear();
}

bool PulldownKeys(void)
{
  if (g_lbOpen.empty())
    return false;
  if (Key(VK_ESCAPE))
    g_lbOpen.clear();
  return true;
}

// A tip for what is not built yet: asked for by the control under the mouse, drawn last,
// under the control (or above it near the foot), at its left or, on the right half, its right.
namespace {

struct TipAsk {
  bool bOn;
  Clay_ElementId id;
  std::string title, text;
};
TipAsk g_tip;

}  // namespace

void TipOn(Clay_ElementId id, const char *pszTitle, const char *pszText)
{
  if (!g_lbOpen.empty() || !Over(id))
    return;
  g_tip.bOn = true;
  g_tip.id = id;
  g_tip.title = pszTitle;
  g_tip.text = pszText;
}

void DrawTip(void)
{
  if (!g_tip.bOn)
    return;
  g_tip.bOn = false;
  const Clay_ElementData at = Clay_GetElementData(g_tip.id);
  if (!at.found)
    return;
  const bool bRight = at.boundingBox.x + at.boundingBox.width / 2 > UiWidth() / 2;
  const bool bAbove = at.boundingBox.y > UiHeight() * 0.7f;
  Decl t;
  t.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  t.d.layout.sizing.width = Fit(Sc(420));
  t.d.layout.padding = Pad(Fs(14), Fs(14), Fs(12), Fs(12));
  t.d.layout.childGap = Gap(Fs(6));
  t.d.floating.attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID;
  t.d.floating.parentId = g_tip.id.id;
  t.d.floating.attachPoints.element = bAbove ? (bRight ? CLAY_ATTACH_POINT_RIGHT_BOTTOM : CLAY_ATTACH_POINT_LEFT_BOTTOM)
                                             : (bRight ? CLAY_ATTACH_POINT_RIGHT_TOP : CLAY_ATTACH_POINT_LEFT_TOP);
  t.d.floating.attachPoints.parent = bAbove ? (bRight ? CLAY_ATTACH_POINT_RIGHT_TOP : CLAY_ATTACH_POINT_LEFT_TOP)
                                            : (bRight ? CLAY_ATTACH_POINT_RIGHT_BOTTOM : CLAY_ATTACH_POINT_LEFT_BOTTOM);
  t.d.floating.offset.y = bAbove ? -Sc(8) : Sc(8);
  t.d.floating.zIndex = 95;
  t.d.floating.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH;
  const ScLook look = {kScTipFill, kScTipFill, kScTipRim, kScTipFill, kScBlack, 3};
  t.d.custom.customData = Custom(DrawScBox, look);
  Open(t);
  Text(Keep(g_tip.title), UI_FACE_BOLD, Fs(16), 0.0f, kScInkTipHead);
  Text(Keep(g_tip.text), UI_FACE_TEXT, Fs(15), 0.0f, kScInkTip, -1, 1.3f);
  Close();
}

// A colour chip before a pulldown's text.
void Chip(unsigned rgb, float fSide)
{
  Decl chip;
  chip.d.layout.sizing.width = Fixed(Sc(fSide));
  chip.d.layout.sizing.height = Fixed(Sc(fSide));
  chip.d.backgroundColor = Col(UiRgb(rgb));
  Radius(chip, 3);
  chip.d.border = Border(kScBlack, 1, 1, 1, 1);
  Open(chip);
  Close();
}

// A slot's ground (lobby.css .lb-slot::before): n 1 under the mouse, 2 closed, 4 compact.
void DrawSlot(float x, float y, float w, float h, void *pCtx)
{
  const int n = static_cast<const ScDraw *>(pCtx)->n;
  const float inset = Sc(n & 4 ? 2 : 5);
  if (n & 2) {
    UiRing(x, y + inset, w, h - 2 * inset, kScWellRim, Sc(3), fmaxf(1.0f, Sc(1)));
    return;
  }
  UiFill(x, y + inset, w, h - 2 * inset, n & 1 ? kLbItemHi : kLbItem, Sc(3));
  UiRing(x, y + inset, w, h - 2 * inset, n & 1 ? kScButtonHi.rim : kLbItemRim, Sc(3), fmaxf(1.0f, Sc(1)));
}

// A slot's number (lobby.css .lb-num): a ring.
void DrawNum(float x, float y, float w, float h, void *)
{
  UiRing(x - Sc(3), y - Sc(3), w + Sc(6), h + Sc(6), kScWellRim, w / 2 + Sc(3), fmaxf(1.0f, Sc(1)));
  UiRing(x - Sc(2), y - Sc(2), w + Sc(4), h + Sc(4), {0.0f, 0.0f, 0.0f, 0.85f}, w / 2 + Sc(2), Sc(2));
}

// The avatar (lobby.css .lb-ava): n 1 a computer's.
void DrawAva(float x, float y, float w, float h, void *pCtx)
{
  const bool bComputer = static_cast<const ScDraw *>(pCtx)->n == 1;
  UiGradient(x, y, w, h, kScAvaFaceTop, kScAvaFaceBot, false, Sc(6));
  UiRing(x, y, w, h, bComputer ? kScWellRim : kScAvaOuter, Sc(6), Sc(2));
}

// A pulldown (BattlenetSmallPulldown): its face shows the choice, or pszFace, and a click
// opens its list under it, or over it near the screen's foot; the choice made, or -1. Off, it is greyed (not built yet);
// locked, it shows its choice in Hyperion's gold.
int Pulldown(const char *pszId, int nIndex, float fWidth, const std::vector<std::string> &items, int nCur,
             int nState, const unsigned *pChips, const char *pszFace, float fListWidth)
{
  const std::string key = std::string(pszId) + "#" + std::to_string(nIndex);
  const bool bEnabled = nState == PD_ON;
  const bool bOpen = g_lbOpen == key;
  Decl face = Box(pszId, nIndex);
  const bool bHot = bEnabled && Over(face.d.id);
  if (bEnabled && Clicked(face.d.id))
    g_lbOpen = bOpen ? std::string() : key;
  face.d.layout.sizing.width = Fixed(Sc(fWidth));
  face.d.layout.sizing.height = Fixed(Sc(36));
  face.d.layout.padding = Pad(Fs(10), Fs(34), 0, 0);
  face.d.layout.childGap = Gap(Fs(8));
  face.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  ScLook look = nState == PD_OFF ? kScOff : bHot || bOpen ? kScDropHi : kScDrop;
  if (nState == PD_LOCKED)
    look.rim = kLbHyperionRim;
  face.d.custom.customData = Custom(DrawScRaised, look, 0.0f, nState == PD_OFF ? 0 : bOpen ? 2 : 1);
  Open(face);
  if (pChips != NULL && nCur >= 0 && nCur < static_cast<int>(items.size()))
    Chip(pChips[nCur]);
  const char *pszShown = pszFace != NULL ? pszFace : nCur >= 0 && nCur < static_cast<int>(items.size()) ? Keep(items[nCur]) : "";
  Text(pszShown, UI_FACE_TEXT, Fs(15), 0.0f,
       nState == PD_OFF ? kScInkOff : nState == PD_LOCKED ? kScInkAccept : kScInkHi);
  int nChosen = -1;
  if (bOpen) {
    Decl list;
    list.d.id = Id((std::string(pszId) + "_list").c_str(), nIndex);
    list.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    list.d.layout.sizing.width = Fixed(Sc(fmaxf(fListWidth, fWidth > 220 ? fWidth : 220)));
    list.d.layout.padding = Pad(0, 0, Fs(4), Fs(4));
    list.d.backgroundColor = Col(kScMenuFill);
    list.d.border = Border(kScMenuRim, 1, 1, 1, 1);
    list.d.floating.attachTo = CLAY_ATTACH_TO_PARENT;
    // under the face, or over it when the list would pass the screen's foot and there is more
    // room above (the face where the last frame laid it out)
    bool bUp = false;
    const Clay_ElementData at = Clay_GetElementData(face.d.id);
    if (at.found) {
      const float fList = Sc(34) * static_cast<float>(items.size()) + Sc(8) + 2.0f;
      const float fBelow = static_cast<float>(UiHeight()) - (at.boundingBox.y + at.boundingBox.height);
      bUp = fList > fBelow && at.boundingBox.y > fBelow;
    }
    list.d.floating.attachPoints.element = bUp ? CLAY_ATTACH_POINT_LEFT_BOTTOM : CLAY_ATTACH_POINT_LEFT_TOP;
    list.d.floating.attachPoints.parent = bUp ? CLAY_ATTACH_POINT_LEFT_TOP : CLAY_ATTACH_POINT_LEFT_BOTTOM;
    list.d.floating.zIndex = 90;
    Open(list);
    for (size_t i = 0; i < items.size(); ++i) {
      Decl it = Box((std::string(pszId) + "_item").c_str(), nIndex * 64 + static_cast<int>(i));
      const bool bItemHot = Over(it.d.id);
      if (Clicked(it.d.id)) {
        nChosen = static_cast<int>(i);
        g_lbOpen.clear();
      }
      it.d.layout.sizing.width = Grow();
      it.d.layout.sizing.height = Fixed(Sc(34));
      it.d.layout.padding = Pad(Fs(12), Fs(12), 0, 0);
      it.d.layout.childGap = Gap(Fs(8));
      it.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      if (bItemHot || static_cast<int>(i) == nCur)
        it.d.backgroundColor = Col(kScMenuSel);
      Open(it);
      if (pChips != NULL)
        Chip(pChips[i]);
      Text(Keep(items[i]), UI_FACE_TEXT, Fs(15), 0.0f, bItemHot ? kScInkHi : kScInkButton);
      Close();
    }
    Close();
  }
  Close();
  return nChosen;
}

// A greyed pulldown showing what 1.17 does, with a tip on what it waits for.
void PulldownLater(const char *pszId, int nIndex, float fWidth, const char *pszShown, const char *pszTitle,
                   const char *pszWhy)
{
  Pulldown(pszId, nIndex, fWidth, std::vector<std::string>(), -1, PD_OFF, NULL, pszShown);
  TipOn(Id(pszId, nIndex), pszTitle, pszWhy);
}

// The design's segmented switch (lobby.css .lb-seg): the choices joined in one rim, the one
// chosen lit with a line under it.
void DrawSeg(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  const bool bOn = (d.n & 1) != 0, bFirst = (d.n & 4) != 0;
  const ScLook &k = bOn ? kScTabOn : kScTab;
  UiGradient(x, y, w, h, k.top, k.bot, false);
  if (!bFirst)
    UiFill(x, y, fmaxf(1.0f, Sc(1)), h, kScWellRim);
  if (bOn) {
    UiRing(x, y, w, h, {k.halo.r, k.halo.g, k.halo.b, 0.3f}, 0.0f, Sc(4));
    UiFill(x, y + h - Sc(2), w, Sc(2), kScSelLine2);
  }
}

// The one clicked, or -1; the choices in nOff (a bit each) are greyed, with pszWhy as
// their tip.
int Segments(const char *pszId, const char *const *ppItems, int nItems, int nCur, unsigned nOff,
             const char *pszWhy, float fHeight)
{
  Decl row = Box(pszId, 99);
  row.d.border = Border(kScTab.rim, Fs(2), Fs(2), Fs(2), Fs(2));
  row.d.layout.padding = Pad(Fs(2), Fs(2), Fs(2), Fs(2));
  Radius(row, 4);
  Open(row);
  int nChosen = -1;
  for (int i = 0; i < nItems; ++i) {
    Decl b = Box(pszId, i);
    const bool bOff = (nOff >> i & 1) != 0;
    const bool bOn = i == nCur;
    const bool bHot = !bOff && Over(b.d.id);
    if (!bOff && Clicked(b.d.id))
      nChosen = i;
    b.d.layout.sizing.height = Fixed(Sc(fHeight));
    b.d.layout.padding = Pad(Fs(12), Fs(12), 0, 0);
    b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    b.d.custom.customData = Custom(DrawSeg, kScTab, 0.0f, (bOn ? 1 : 0) | (bHot ? 2 : 0) | (i == 0 ? 4 : 0));
    Open(b);
    // the current one reads lit even when it cannot be clicked (a guest's view of the host's)
    Text(ppItems[i], UI_FACE_TEXT, Fs(15), 0.0f, bOn ? kScInkHi : bOff ? kScInkOff : bHot ? kScInkHi : kScInkButton);
    Close();
    if (bOff && pszWhy != NULL)
      TipOn(b.d.id, ppItems[i], pszWhy);
  }
  Close();
  return nChosen;
}

void Well(Decl &w)
{
  w.d.backgroundColor = Col(kScWell);
  w.d.border = Border(kScWellRim, Fs(2), Fs(2), Fs(2), Fs(2));
  Radius(w, 4);
}

// A dialog over the screen (lobby.css .lb-dlg): the blackout, which closes it when
// clicked, and the frame, w by h, centred, from y down; the frame is left open.
bool DialogOpen(const char *pszId, float w, float h, float y, int nZ)
{
  Decl scrim = Box((std::string(pszId) + "_scrim").c_str());
  scrim.d.layout.sizing.width = Fixed(static_cast<float>(UiWidth()));
  scrim.d.layout.sizing.height = Fixed(static_cast<float>(UiHeight()));
  scrim.d.backgroundColor = Col(kScBlackout);
  scrim.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  scrim.d.floating.zIndex = static_cast<int16_t>(nZ);
  const bool bOut = Clicked(scrim.d.id);
  Open(scrim);
  Close();
  Decl dlg = Box(pszId);
  dlg.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  dlg.d.layout.sizing.width = Fixed(Sc(w));
  dlg.d.layout.sizing.height = Fixed(Sc(h));
  dlg.d.layout.padding = Pad(Fs(34), Fs(34), Fs(30), Fs(24));
  dlg.d.layout.childGap = Gap(Fs(12));
  dlg.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  dlg.d.floating.offset.x = (UiWidth() - Sc(w)) / 2;
  dlg.d.floating.offset.y = Sc(y);
  dlg.d.floating.zIndex = static_cast<int16_t>(nZ + 1);
  static UiCustom frame = {0.0f, DrawScFrame, NULL};
  dlg.d.custom.customData = &frame;
  Open(dlg);
  return bOut;
}

// ---------------------------------------------------------------------------
// A map's picture.

namespace {

// The picked map's minimap: each tile the average colour of its megatile, worked out from
// the tileset's own files as the game draws a tile (the .cv5's groups, the .vx4's megatiles,
// the .vr4's 8 by 8 cells, the .wpe's palette); a map wider than the picture averages its
// tiles in squares. The tileset's files stay loaded for the next map of the same tileset.
struct TilesetColours {
  int nTileset = -1;
  std::vector<WORD> cv5;        // 26 words a group, the megatiles from the 11th
  std::vector<DWORD> vx4;       // 16 dwords a megatile: a cell, and whether it is flipped (PortMegatilesLoad)
  std::vector<BYTE> vr4;        // 64 bytes a cell
  std::vector<BYTE> wpe;        // 4 bytes a colour
  std::vector<DWORD> megatile;  // a megatile's colour, 0xRRGGBB, with bit 24 once worked out
};
TilesetColours g_tileset;

template <class T>
bool TilesetFile(int nTileset, const char *pszExt, std::vector<T> *pOut)
{
  char szPath[MAX_PATH];
  wsprintfA(szPath, "Tileset\\%s.%s", gpszTilesetNames_g[nTileset], pszExt);
  DWORD cb = 0;
  void *p = load_game_data(szPath, NULL, &cb, "port_frontlobby.cpp", __LINE__);
  if (p == NULL)
    return false;
  pOut->assign(static_cast<T *>(p), static_cast<T *>(p) + cb / sizeof(T));
  SMemFree(p, "port_frontlobby.cpp", __LINE__, 0);
  return true;
}

bool TilesetMegatiles(int nTileset, std::vector<DWORD> *pOut)
{
  char szPath[MAX_PATH];
  wsprintfA(szPath, "Tileset\\%s.vx4", gpszTilesetNames_g[nTileset]);
  ULONG *p = PortMegatilesLoad(szPath);
  if (p == NULL)
    return false;
  pOut->assign(p, p + PortMemSize(p) / sizeof(*p));
  SMemFree(p, "port_frontlobby.cpp", __LINE__, 0);
  return true;
}

DWORD MegatileColour(WORD wMegatile)
{
  if (wMegatile >= g_tileset.megatile.size())
    return 0;
  DWORD &c = g_tileset.megatile[wMegatile];
  if (c & 0x1000000)
    return c & 0xFFFFFF;
  unsigned r = 0, g = 0, b = 0, n = 0;
  for (int k = 0; k < 16; ++k) {
    const size_t nCell = g_tileset.vx4[wMegatile * 16u + k] >> 1;
    if ((nCell + 1) * 64 > g_tileset.vr4.size())
      continue;
    for (int i = 0; i < 64; ++i) {
      const BYTE *e = &g_tileset.wpe[g_tileset.vr4[nCell * 64 + i] * 4u];
      r += e[0];
      g += e[1];
      b += e[2];
      ++n;
    }
  }
  c = 0x1000000 | (n ? (r / n) << 16 | (g / n) << 8 | (b / n) : 0);
  return c & 0xFFFFFF;
}

}  // namespace

// The minimap of a map whose tiles (the CHK's MTXM) are pTiles, nTiles of them, nWidth by
// nHeight: a pixel for every few tiles, at most nMaxSide pixels a side, as RGBA. False when the
// tileset's files are not there.
namespace {

// The tileset's files read, kept for the next map of the same tileset.
bool TilesetReady(int nTileset)
{
  if (nTileset < 0 || nTileset >= 8)
    return false;
  if (g_tileset.nTileset != nTileset) {
    g_tileset = TilesetColours();
    if (!TilesetFile(nTileset, "cv5", &g_tileset.cv5) || !TilesetMegatiles(nTileset, &g_tileset.vx4) ||
        !TilesetFile(nTileset, "vr4", &g_tileset.vr4) || !TilesetFile(nTileset, "wpe", &g_tileset.wpe) ||
        g_tileset.wpe.size() < 1024)
      return false;
    g_tileset.nTileset = nTileset;
    g_tileset.megatile.assign(g_tileset.vx4.size() / 16, 0);
  }
  return true;
}

// A scenario.chk's tileset, size and tiles (ERA, DIM, MTXM).
bool ChkTiles(const BYTE *pChk, DWORD cb, int *pnTileset, int *pnWidth, int *pnHeight, const WORD **ppTiles,
              size_t *pnTiles)
{
  *pnTileset = -1;
  *pnWidth = *pnHeight = 0;
  *ppTiles = NULL;
  *pnTiles = 0;
  for (DWORD i = 0; i + 8 <= cb;) {
    const DWORD dwId = *reinterpret_cast<const DWORD *>(pChk + i);
    const LONG nLen = *reinterpret_cast<const LONG *>(pChk + i + 4);
    i += 8;
    if (nLen < 0 || static_cast<DWORD>(nLen) > cb - i)
      break;
    if (dwId == ' ARE' && nLen >= 2)
      *pnTileset = *reinterpret_cast<const WORD *>(pChk + i) & 7;
    if (dwId == ' MID' && nLen >= 4) {
      *pnWidth = *reinterpret_cast<const WORD *>(pChk + i);
      *pnHeight = *reinterpret_cast<const WORD *>(pChk + i + 2);
    }
    if (dwId == 'MXTM') {
      *ppTiles = reinterpret_cast<const WORD *>(pChk + i);
      *pnTiles = static_cast<size_t>(nLen) / 2;
    }
    i += static_cast<DWORD>(nLen);
  }
  return *ppTiles != NULL && *pnWidth > 0 && *pnHeight > 0;
}

}  // namespace

bool MapTerrainRgba(const BYTE *pChk, DWORD cb, int x0, int y0, int w, int h, std::vector<unsigned char> *pRgba,
                    int *pnMapW, int *pnMapH)
{
  int nTileset, nWidth, nHeight;
  const WORD *pTiles;
  size_t nTiles;
  if (w <= 0 || h <= 0 || !ChkTiles(pChk, cb, &nTileset, &nWidth, &nHeight, &pTiles, &nTiles) ||
      !TilesetReady(nTileset))
    return false;
  if (pnMapW != NULL)
    *pnMapW = nWidth;
  if (pnMapH != NULL)
    *pnMapH = nHeight;
  std::vector<unsigned char> &rgba = *pRgba;
  rgba.assign(static_cast<size_t>(w) * h * 4, 0);
  const TilesetColours &t = g_tileset;
  for (int py = 0; py < h; ++py) {
    const int my = y0 + py;
    unsigned char *o = &rgba[static_cast<size_t>(py) * w * 4];
    for (int px = 0; px < w; ++px, o += 4) {
      const int mx = x0 + px;
      o[3] = 255;
      if (mx < 0 || my < 0 || mx >= nWidth * 32 || my >= nHeight * 32)
        continue;
      // the tile's megatile, its 8 by 8 cell there, flipped or not, and the cell's pixel
      const size_t nAt = static_cast<size_t>(my >> 5) * nWidth + (mx >> 5);
      const WORD wTile = nAt < nTiles ? pTiles[nAt] : 0;
      const size_t nWord = (wTile >> 4) * 26u + 10 + (wTile & 15);
      if (nWord >= t.cv5.size())
        continue;
      const size_t nEntry = t.cv5[nWord] * 16u + ((my & 31) >> 3) * 4 + ((mx & 31) >> 3);
      if (nEntry >= t.vx4.size())
        continue;
      const size_t nCell = t.vx4[nEntry] >> 1;
      const int cx = (t.vx4[nEntry] & 1) != 0 ? 7 - (mx & 7) : mx & 7;
      if ((nCell + 1) * 64 > t.vr4.size())
        continue;
      const BYTE *e = &t.wpe[t.vr4[nCell * 64 + (my & 7) * 8 + cx] * 4u];
      o[0] = e[0];
      o[1] = e[1];
      o[2] = e[2];
    }
  }
  return true;
}

bool MinimapRgba(int nTileset, int nWidth, int nHeight, const WORD *pTiles, size_t nTiles, int nMaxSide,
                 std::vector<unsigned char> *pRgba, int *pnW, int *pnH)
{
  if (nWidth <= 0 || nHeight <= 0 || nTileset < 0 || nTileset >= 8 || nMaxSide <= 0 || !TilesetReady(nTileset))
    return false;
  const int nSide = nWidth > nHeight ? nWidth : nHeight;
  const int nStep = (nSide + nMaxSide - 1) / nMaxSide;
  const int nW = (nWidth + nStep - 1) / nStep, nH = (nHeight + nStep - 1) / nStep;
  std::vector<unsigned char> &rgba = *pRgba;
  rgba.assign(static_cast<size_t>(nW) * nH * 4, 255);
  for (int py = 0; py < nH; ++py)
    for (int px = 0; px < nW; ++px) {
      unsigned r = 0, g = 0, b = 0, n = 0;
      for (int ty = py * nStep; ty < (py + 1) * nStep && ty < nHeight; ++ty)
        for (int tx = px * nStep; tx < (px + 1) * nStep && tx < nWidth; ++tx) {
          const size_t nAt = static_cast<size_t>(ty) * nWidth + tx;
          const WORD wTile = nAt < nTiles ? pTiles[nAt] : 0;
          const size_t nWord = (wTile >> 4) * 26u + 10 + (wTile & 15);
          const DWORD c = nWord < g_tileset.cv5.size() ? MegatileColour(g_tileset.cv5[nWord]) : 0;
          r += c >> 16 & 0xFF;
          g += c >> 8 & 0xFF;
          b += c & 0xFF;
          ++n;
        }
      unsigned char *o = &rgba[(static_cast<size_t>(py) * nW + px) * 4];
      o[0] = static_cast<unsigned char>(n ? r / n : 0);
      o[1] = static_cast<unsigned char>(n ? g / n : 0);
      o[2] = static_cast<unsigned char>(n ? b / n : 0);
    }
  *pnW = nW;
  *pnH = nH;
  return true;
}

namespace {

// The same into the picture atlas's slot nSlot.
bool MakeMinimap(int nSlot, int nTileset, int nWidth, int nHeight, const WORD *pTiles, size_t nTiles)
{
  std::vector<unsigned char> rgba;
  int nW = 0, nH = 0;
  if (!MinimapRgba(nTileset, nWidth, nHeight, pTiles, nTiles, UI_PICTURE_SIDE, &rgba, &nW, &nH))
    return false;
  UiPictureSet(nSlot, rgba.data(), nW, nH);
  return true;
}

}  // namespace

// A map's picture from its scenario.chk, pChk, cb bytes: its size, tileset, start locations
// (unit 214) by owner, resources and other units, and its minimap in p->nSlot.
void MapPictureRead(MapPicture *p, const std::string &of, const BYTE *pChk, DWORD cb)
{
  const int nSlot = p->nSlot;
  *p = MapPicture();
  p->nSlot = nSlot;
  p->of = of;
  // the units' bounds, from units.dat, which the front end has not read before a game or a
  // briefing (each game reads it again)
  if (gUnitBoundBox[0].right == 0)
    load_array_data("arr\\units.dat", gUnitsArrayDataTbl);
  int nTileset = -1, nWidth = 0, nHeight = 0;
  const WORD *pTiles = NULL;
  size_t nTiles = 0;
  for (DWORD i = 0; i + 8 <= cb;) {
    const DWORD dwId = *reinterpret_cast<const DWORD *>(pChk + i);
    const LONG nLen = *reinterpret_cast<const LONG *>(pChk + i + 4);
    i += 8;
    if (nLen < 0 || static_cast<DWORD>(nLen) > cb - i)
      break;
    if (dwId == ' ARE' && nLen >= 2)  // "ERA ", the tileset
      nTileset = *reinterpret_cast<const WORD *>(pChk + i) & 7;
    if (dwId == ' MID' && nLen >= 4) {  // "DIM ", the size in tiles
      nWidth = *reinterpret_cast<const WORD *>(pChk + i);
      nHeight = *reinterpret_cast<const WORD *>(pChk + i + 2);
    }
    if (dwId == 'MXTM') {  // "MTXM", the tiles the game draws
      pTiles = reinterpret_cast<const WORD *>(pChk + i);
      nTiles = static_cast<size_t>(nLen) / 2;
    }
    if (dwId == 'TINU')  // "UNIT", 36 bytes a unit
      for (LONG k = 0; k + 36 <= nLen; k += 36) {
        const BYTE *u = pChk + i + k;
        const WORD wType = *reinterpret_cast<const WORD *>(u + 8);
        if (wType == 214) {
          MapStart st = {u[16], *reinterpret_cast<const WORD *>(u + 4), *reinterpret_cast<const WORD *>(u + 6)};
          p->starts.push_back(st);
        } else if ((wType >= 176 && wType <= 178) || wType == 188) {  // mineral fields, a geyser
          // the amount at +20 when the properties' word at +12 says it is set (maphdr.cpp)
          const bool bAmount = (*reinterpret_cast<const DWORD *>(u + 12) & 0x100000) != 0;
          MapResource res = {*reinterpret_cast<const WORD *>(u + 4), *reinterpret_cast<const WORD *>(u + 6), wType == 188,
                             bAmount ? *reinterpret_cast<const WORD *>(u + 20) : static_cast<WORD>(wType == 188 ? 5000 : 1500)};
          p->resources.push_back(res);
        } else {
          // a mod's unit (from 256), whose bounds come with its game, a tile
          const rect16_t b = wType < 228 ? gUnitBoundBox[wType] : rect16_t{16, 16, 15, 15};
          MapPlaced pl = {*reinterpret_cast<const WORD *>(u + 4), *reinterpret_cast<const WORD *>(u + 6),
                          static_cast<WORD>(b.left + b.right + 1), static_cast<WORD>(b.top + b.bottom + 1), u[16]};
          p->placed.push_back(pl);
        }
      }
    i += static_cast<DWORD>(nLen);
  }
  p->w = nWidth;
  p->h = nHeight;
  p->tileset = nTileset & 7;
  if (pTiles != NULL)
    p->tiles.assign(pTiles, pTiles + nTiles);
  if (pTiles != NULL && p->nSlot >= 0)
    p->bMinimap = MakeMinimap(p->nSlot, nTileset, nWidth, nHeight, pTiles, nTiles) && g_tileset.nTileset == nTileset;
}

bool MapWalkable(const MapPicture &p, std::vector<unsigned char> *pWalk)
{
  // the tileset's groups and each megatile's minitiles' flags, kept for the next map on it
  static int s_nTileset = -1;
  static std::vector<WORD> s_cv5, s_vf4;
  if (p.w <= 0 || p.h <= 0 || p.tiles.empty())
    return false;
  if (s_nTileset != p.tileset) {
    s_nTileset = -1;
    s_cv5.clear();
    s_vf4.clear();
    if (!TilesetFile(p.tileset, "cv5", &s_cv5) || !TilesetFile(p.tileset, "vf4", &s_vf4))
      return false;
    s_nTileset = p.tileset;
  }
  const int mw = p.w * 4;
  pWalk->assign(static_cast<size_t>(mw) * p.h * 4, 0);
  for (int t = 0; t < p.w * p.h; ++t) {
    const WORD wTile = static_cast<size_t>(t) < p.tiles.size() ? p.tiles[t] : 0;
    const size_t nWord = (wTile >> 4) * 26u + 10 + (wTile & 15);
    const size_t nMega = nWord < s_cv5.size() ? s_cv5[nWord] & 0x7FFF : 0;
    if ((nMega + 1) * 16 > s_vf4.size())
      continue;
    const int x0 = t % p.w * 4, y0 = t / p.w * 4;
    for (int k = 0; k < 16; ++k)
      (*pWalk)[static_cast<size_t>(y0 + k / 4) * mw + x0 + k % 4] = s_vf4[nMega * 16 + k] & 1;
  }
  return true;
}

bool MapMinimapRgba(const BYTE *pChk, DWORD cb, int nMaxSide, std::vector<unsigned char> *pRgba, int *pnW, int *pnH)
{
  int nTileset, nWidth, nHeight;
  const WORD *pTiles;
  size_t nTiles;
  return ChkTiles(pChk, cb, &nTileset, &nWidth, &nHeight, &pTiles, &nTiles) &&
         MinimapRgba(nTileset, nWidth, nHeight, pTiles, nTiles, nMaxSide, pRgba, pnW, pnH);
}

const std::vector<TeamLayout> &MapLayouts(const MapPicture *pPic, int nStarts, bool *pbNew)
{
  static std::vector<TeamLayout> s_layouts;
  static std::string s_of;
  static int s_nStarts = -1;
  const std::string of = pPic != NULL ? pPic->of : std::string();
  const bool bNew = of != s_of || nStarts != s_nStarts;
  if (bNew) {
    s_of = of;
    s_nStarts = nStarts;
    s_layouts.clear();
    if (pPic != NULL && pPic->w > 0 && static_cast<int>(pPic->starts.size()) >= nStarts) {
      std::vector<TeamPoint> pts;
      for (int i = 0; i < nStarts; ++i)
        pts.push_back({pPic->starts[i].x / (32.0 * pPic->w), pPic->starts[i].y / (32.0 * pPic->h)});
      s_layouts = TeamsLayouts(pts);
    }
  }
  if (pbNew != NULL)
    *pbNew = bNew && !s_layouts.empty();
  return s_layouts;
}

namespace {

// The map's picture: its minimap, in the map's shape, and its starts numbered where they
// are; until the minimap is made, its tileset's colour and a grid.

struct PreviewLook {
  UiColor fill;
  bool bMinimap;
  float fx0, fy0, fx1, fy1;  // the minimap's place in the box, 0 to 1
  float fMapW, fMapH;        // the map's size in pixels
  bool bAllUnits;            // Use Map Settings, which keeps the players' units too
  const MapPicture *pPic;
};
std::deque<PreviewLook> g_looks;  // the frame's, alive until its layout is drawn

}  // namespace

void MapPreviewFrame(void)
{
  g_looks.clear();
}

namespace {

void DrawPreview(float x, float y, float w, float h, void *pCtx)
{
  const PreviewLook &k = *static_cast<const PreviewLook *>(pCtx);
  UiFill(x, y, w, h, k.fill);
  if (k.bMinimap) {
    const float mx = x + w * k.fx0, my = y + h * k.fy0, mw = w * (k.fx1 - k.fx0), mh = h * (k.fy1 - k.fy0);
    UiPicture(mx, my, mw, mh, k.pPic->nSlot, UiRgb(0xffffff));
    // the resources, as the game's minimap marks them: a field two tiles by one, a geyser
    // four by two
    const float fTileW = mw * 32.0f / k.fMapW, fTileH = mh * 32.0f / k.fMapH;
    for (const MapResource &res : k.pPic->resources) {
      const float rw = fmaxf(2.0f, fTileW * (res.bGas ? 4 : 2)), rh = fmaxf(2.0f, fTileH * (res.bGas ? 2 : 1));
      UiFill(mx + mw * res.x / k.fMapW - rw / 2, my + mh * res.y / k.fMapH - rh / 2, rw, rh,
             UiRgb(res.bGas ? 0x3ce06a : 0x4cc8ff));
    }
    // the other units, at their size and in their owner's colour, the neutral ones (and an
    // owner past the eight colours) grey; a melee game keeps only the neutral ones
    for (const MapPlaced &pl : k.pPic->placed) {
      if (pl.owner != 11 && !k.bAllUnits)
        continue;
      const float rw = fmaxf(2.0f, mw * pl.w / k.fMapW), rh = fmaxf(2.0f, mh * pl.h / k.fMapH);
      UiFill(mx + mw * pl.x / k.fMapW - rw / 2, my + mh * pl.y / k.fMapH - rh / 2, rw, rh,
             UiRgb(pl.owner < 8 ? kSlotColours[pl.owner] : 0xb4b4b4));
    }
  } else {
    const UiColor line = {1.0f, 1.0f, 1.0f, 0.05f};
    for (int i = 1; i < 8; ++i) {
      UiFill(x + w * i / 8, y, 1.0f, h, line);
      UiFill(x, y + h * i / 8, w, 1.0f, line);
    }
  }
  UiRing(x, y, w, h, kScWellRim, 0.0f, fmaxf(1.0f, Sc(2)));
}

const unsigned kTilesetTint[] = {0x3a2c1c, 0x1c1c2a, 0x262a2e, 0x2e1a12, 0x14301a, 0x3a3018, 0x2c3a46, 0x2a1c30};

// A start's mark (lobby.css .lb-mark): its number in a ring; the starts are drawn at
// random, so no player's colour.
void DrawMark(float x, float y, float w, float h, void *)
{
  UiFill(x, y, w, h, {0.0f, 0.0f, 0.0f, 0.6f}, w / 2);
  UiRing(x, y, w, h, kScInkButton, w / 2, fmaxf(1.0f, Sc(2)));
}

// A start in its team's colour (MapMark): the colour, rimmed in black and then light.
void DrawMarkTeam(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  UiFill(x, y, w, h, d.look.top, w / 2);
  UiRing(x, y, w, h, {0.0f, 0.0f, 0.0f, 0.9f}, w / 2, fmaxf(1.0f, Sc(3)));
  UiRing(x + Sc(1), y + Sc(1), w - Sc(2), h - Sc(2), kScInkHi, w / 2 - Sc(1), fmaxf(1.0f, Sc(1)));
}

// Whether text on rgb reads better dark (the sRGB luminance past the middle).
bool LightFill(unsigned rgb)
{
  return 0.2126f * (rgb >> 16 & 0xFF) + 0.7152f * (rgb >> 8 & 0xFF) + 0.0722f * (rgb & 0xFF) > 150.0f;
}

}  // namespace

void MapPreview(const char *pszId, const MapPicture *pPic, int nTileset, const char *pszTitle, int nMapW, int nMapH,
                float fW, float fH, int nZ, bool bBig, bool bAllUnits, const std::vector<MapMark> *pMarks)
{
  Decl pv = Box(pszId);
  pv.d.layout.sizing.width = Fixed(Sc(fW));
  pv.d.layout.sizing.height = Fixed(Sc(fH));
  pv.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  pv.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  PreviewLook look;
  SDL_zero(look);
  look.fill = pszTitle != NULL ? UiRgb(kTilesetTint[nTileset & 7]) : UiRgb(0x0a1420);
  look.pPic = pPic;
  const bool bRead = pszTitle != NULL && nMapW > 0 && nMapH > 0;
  float fScale = 0.0f, ox = 0.0f, oy = 0.0f;
  if (bRead) {
    const float fMapW = nMapW * 32.0f, fMapH = nMapH * 32.0f;
    fScale = fminf(Sc(fW) / fMapW, Sc(fH) / fMapH);
    ox = (Sc(fW) - fMapW * fScale) / 2;
    oy = (Sc(fH) - fMapH * fScale) / 2;
    if (pPic != NULL && pPic->bMinimap) {
      look.bMinimap = true;
      look.fx0 = ox / Sc(fW);
      look.fy0 = oy / Sc(fH);
      look.fx1 = 1.0f - look.fx0;
      look.fy1 = 1.0f - look.fy0;
      look.fMapW = fMapW;
      look.fMapH = fMapH;
      look.bAllUnits = bAllUnits;
      look.fill = UiRgb(0x000000);
    }
  }
  g_looks.push_back(look);
  g_customs.push_back(UiCustom{0.0f, DrawPreview, &g_looks.back()});
  pv.d.custom.customData = &g_customs.back();
  Open(pv);
  if (pszTitle == NULL) {
    Label(PortText("No map"), 18, kScInkMenu);
  } else if (bRead && pPic != NULL) {
    const float d = Sc(bBig ? 30 : pPic->starts.size() > 16 ? 17 : 26);
    for (size_t i = 0; i < pPic->starts.size(); ++i) {
      Decl dot = Floating(NULL, 0, ox + pPic->starts[i].x * fScale - d / 2, oy + pPic->starts[i].y * fScale - d / 2, nZ,
                          CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
      dot.d.floating.clipTo = CLAY_CLIP_TO_ATTACHED_PARENT;
      dot.d.layout.sizing.width = Fixed(d);
      dot.d.layout.sizing.height = Fixed(d);
      dot.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
      dot.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      const MapMark *pMark = pMarks != NULL && i < pMarks->size() ? &(*pMarks)[i] : NULL;
      if (pMark != NULL && pMark->bRgb) {
        ScLook team = kScButton;
        team.top = UiRgb(pMark->rgb);
        dot.d.custom.customData = Custom(DrawMarkTeam, team);
      } else {
        dot.d.custom.customData = Custom(DrawMark, kScButton);
      }
      Open(dot);
      Text(Keep(std::to_string(pMark != NULL && pMark->nNum > 0 ? pMark->nNum : pPic->starts[i].nSlot + 1)), UI_FACE_BOLD,
           Fs(bBig ? 14 : pPic->starts.size() > 16 ? 9 : 13), 0.0f,
           pMark != NULL && pMark->bRgb && LightFill(pMark->rgb) ? kScBlack : kScInkButton);
      Close();
    }
  } else if (!bRead) {
    Label(pszTitle, 14, kScInkOff, UI_FACE_WIDE);
  }
  Close();
}

}  // namespace FrontKit
