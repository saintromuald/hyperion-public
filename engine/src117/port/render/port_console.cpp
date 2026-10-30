// port_console.cpp -- the console laid out as StarCraft: Remastered lays out its HD one, and
// what its controls show in HD (docs/modules/render.md, "The console and the overlay";
// docs/renderer.md, R4f). Presentation only.
//
// 1.17's 640-wide console is cut into three regions at columns 138 and 408 (the minimap; the
// selection's panel and the message entry; the portrait, the menu button and the command card),
// which in a wider view follow Remastered's three HD pieces toward the screen's edges, their
// dialogs with them. What read 1.17's console image reads a screen-wide one (the regions in
// place, cut to the pieces' shape, dark where only the pieces are): the mask, console_collision,
// the fluff panels cut in strips around the region dialogs, and hd.hlsl's PSHdConsole. The
// layout goes to the edges once the frame is wholly HD and back as the art goes to SD or before
// the view changes size (PortConsoleUpdate); a replay's needs none (Remastered's neutral console
// is 1.17's layout with more art at each side). A benchmark's run shows none:
// its dialogs and panels hidden, the mask without it (vid_mask_initialize), the map
// the view's whole height (PortViewFullRows), and nothing faded, since a fade took the cursor.
#include <windows.h>
#include <SDL3/SDL.h>  // SDL_free, for what PortCascRead gives
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include "bmp.h"
#include "oc_data.h"
#include "dialog.h"
#include "cursor.h"        // cursor_set_console_offset
#include "dlg_dlgdraw.h"   // dlg_set_all_update
#include "gamemap.h"       // gamemap_force_redraw
#include "gds_vidblit.h"   // BLTMASK, vid_mask_initialize
#include "minimap.h"       // minimap_force_redraw
#include "statfluf.h"      // statfluf_port_panels
#include "status.h"        // status_free, status_init, status_load_strings, status_get_new_selection
#include "statcmd.h"       // statcmd_btn_ctrl_draw_fn
#include "statwire.h"      // the wireframe's remap
#include "typelimits.h"    // UnitTypeLooks, a mod's unit drawn as the one it looks like
#include "CUnit.h"
#include "grp.h"
#include "group.h"         // grh_get_header
#include "statdata.h"      // grhsize_t
#include "statres.h"       // statres_resource_ctrl_draw
#include "selectlimits.h"  // SelectionWide, SelectionSubgroups
#include "port_console.h"
#include "port_present.h"   // PortPresentPaletteRGB
#include "port_hwcursor.h"  // PortGrpFrameDecode
#include "port_gpu_hd.h"  // PortGpuConsoleArt, PortGpuConsolePieces, PortGpuHdConsoleShown
#include "port_mask.h"  // PortMaskDelete, PortMaskCreate
#include "port_minimap.h"  // PortMinimapHud
#include "port_overlay.h"  // PortOverlayHas
#include "port_selection.h"  // PortSelectionHud
#include "../hud/port_hud.h"  // PortHudPlays
#include "../hud/port_hudstate.h"  // PORT_HUD_BTN_*
#include "../data/port_mods.h"  // PortMods, PortModActive: the active mod's data folder
#include "../data/port_moddata.h"  // ModDataFilePath, a mod's portraits
#include "../data/port_stormapi.h"  // PortBmpDecodePcx
#include "../platform/port_json.h"
#include "../platform/port_log.h"
#include "../platform/port_settings.h"  // PortSettingNumber
#include "../platform/port_util.h"  // PortReadFile
#include "port_view.h"  // g_portView, PortViewFullRows
#include "port_hd.h"
#include "port_modart.h"  // PortModUiHas, a mod's own icons and wireframes at the HD scale
#include "../data/port_stormvid.h"  // PortVidPlaying
#include "../data/port_video.h"
#include "../front/port_ui.h"  // PortCascRead
#include "dlg_ctrl.h"      // ctrl_hide, ctrl_show
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

extern ctrl_t *sgpStatMiscDlg;              // 0x6AB380, the menu button (Stat_F10)
extern HANDLE sgpPortSmk_g;                 // 0x6AB390, the portrait's video (statport.cpp)
extern ctrl_t *sgpStatPortCtrl;             // 0x6AB39C, the portrait's control
extern bmp_t sgConsoleArt;                  // 0x6AB504, 1.17's console image while it is loaded
#define sgMinimapArea_g (*reinterpret_cast<RECT *>(OC_VA(0x508BB0u)))        // the minimap's clicks
extern ctrl_t *sgpStatWireCtrl;             // 0x6AB528, the selection's wireframe
extern GROUP *sgpWireFrames;                // 0x6AB524, wirefram.grp
extern GROUP *sgpMultSelWires;              // 0x6AAB2C, grpwire.grp, the selection panel's (statdata.cpp)
extern bmp_t sgBmpStatProgressbarEmpty_g;   // 0x6AAB18, the progress bar's empty picture (statdata.cpp)
extern bmp_t sgBmpStatProgressbarFull_g;    // 0x6AAB20, and its full one
extern ctrl_t *sgpStatResDlg;               // 0x6AB3F4, the resource counter (statres.cpp)

namespace {

int Min(int a, int b) { return a < b ? a : b; }
int Max(int a, int b) { return a > b ? a : b; }

enum { REGIONS = 3, SKINS = 4, PANELS_MAX = 160, STRIP = 64 };
const int kanCut[REGIONS + 1] = {0, 138, 408, 640};  // the regions' columns in 1.17's console
const int kanPieceRegion[3] = {0, 2, 1};             // port_hd.cpp's pieces: left, right, centre

const PortLogger Log("hyperion_mods.log");

// The layout (port_console.h): 1.17's, or the "console" of the active mod's hud.json.
const char *const kapszSkin[SKINS] = {"zerg", "terran", "protoss", "replay"};
struct Layout {
  bool bToEdges = true;      // the regions move toward the screen's edges with the HD console
  bool bFullMap = false;     // the map drawn to the screen's foot at 640 wide too, under the image
  std::string art[SKINS];    // an image of the mod's own, by kapszSkin
};
Layout g_layout;
bool g_bModArt;  // the console loaded is the mod's own image, which has no HD pieces

std::vector<unsigned char> g_art;   // 1.17's console image, as status_load_console loaded it
int g_nArtW, g_nArtH;
int g_nSkin = -1;                   // 0 zerg, 1 terran, 2 protoss, 3 a replay's
int g_nSkinRace = -1;               // Console skin's race for this game's console; -1 the race played

bool g_bSplit;                      // laid out at the edges
bool g_bEdges = true;               // Settings' Console layout: To the edges
int g_nSplitWidth;                  // for a view this wide
int g_anShift[REGIONS];             // each region's console origin, in game pixels, while split
std::vector<unsigned char> g_wide;  // the screen-wide image
fluf_t g_aPanels[PANELS_MAX + 1];
bool g_bPanels;                     // g_aPanels is the fluff table in use
bool g_bWideArt;                    // sgConsoleArt is the screen-wide image (status_get_console_art)
bool g_bHudConsole;                 // this game's console is the HUD's (PortConsoleHudOn)
bool g_bModUi;                      // the GPU draws a mod's own icons and wireframes (PortConsoleModUi)
bool g_bHudRows;                    // the map has the view's whole height for it
bool g_bRebuilding;                 // the console built again in a game (Rebuild)
const ctrl_t *g_pHudDelivering;     // the held dialog the HUD gives a mouse event to now

// The region a console dialog moves with, or -1 for any other dialog.
int DialogRegion(const ctrl_t *pDlg)
{
  if (pDlg == NULL)
    return -1;
  if (pDlg == sgpMinimapDlg)
    return 0;
  if (pDlg == sgpStatDataDlg || pDlg == sgpTextBoxDlg)
    return 1;
  if (pDlg == sgpPortDlg || pDlg == sgpStatCmdDlg || pDlg == sgpStatMiscDlg)
    return 2;
  return -1;
}

// The active mod's hud.json, in its data folder or else its own, into *pHud: left empty when
// there is none (a built-in game has neither folder) or it does not read.
void ReadHudFile(PortJson *pHud, std::string *pFolder)
{
  const std::vector<PortMod> &mods = PortMods();
  const int nActive = PortModActive();
  if (nActive < 0 || nActive >= static_cast<int>(mods.size()))
    return;
  *pFolder = mods[nActive].data.empty() ? mods[nActive].folder : mods[nActive].data;
  std::string text;
  if (pFolder->empty() || !PortReadFile(*pFolder + "hud.json", &text))
    return;
  std::string why;
  if (!PortJsonParse(text.c_str(), pHud, &why)) {
    Log("hud: hud.json does not read: %s", why.c_str());
    *pHud = PortJson();
  }
}

// hud.json's "console" over 1.17's layout; what it gets wrong is left as 1.17's and named in
// hyperion_mods.log.
void ReadLayout(const PortJson &j, const std::string &folder)
{
  g_layout = Layout();
  if (j["toEdges"].kind == PortJson::BOOL)
    g_layout.bToEdges = j["toEdges"].b;
  if (j["fullMap"].kind == PortJson::BOOL)
    g_layout.bFullMap = j["fullMap"].b;
  const PortJson &art = j["art"];
  for (int s = 0; s < SKINS; ++s)
    if (!art[kapszSkin[s]].Str("").empty())
      g_layout.art[s] = folder + art[kapszSkin[s]].Str("");
}

bool IsPanel(const ctrl_t *pDlg)
{
  for (int i = 0; g_bPanels && i < PANELS_MAX && g_aPanels[i].wX != 0xFFFF; ++i)
    if (g_aPanels[i].pDlg == pDlg)
      return true;
  return false;
}

// How far each region can move out from 1.17's place before its piece no longer
// reaches under the centre one: the spread, in game pixels. 0 for the neutral console.
int SpreadMax(const PortHdConsole *pConsole)
{
  if (pConsole->nPieces != 3)
    return 0;
  const int nScale = PortHdScale();
  const PortHdConsolePiece &left = pConsole->pieces[0], &right = pConsole->pieces[1], &centre = pConsole->pieces[2];
  const int nLeft = (left.nX + left.art.nWidth - centre.nX) / nScale;
  const int nRight = (centre.nX + centre.art.nWidth - right.nX) / nScale;
  return Min(nLeft, nRight) - 1;
}

// Whether any texel of game pixel (x, y) is solid in a piece placed at the shifts.
bool PieceSolid(const PortHdConsole *pConsole, const int anPieceShift[3], int x, int y, int nScreenH)
{
  const int nScale = PortHdScale();
  for (int i = 0; i < pConsole->nPieces; ++i) {
    const PortHdConsolePiece &piece = pConsole->pieces[i];
    const int px0 = x * nScale - (anPieceShift[i] * nScale + piece.nX);
    const int py0 = y * nScale - (nScreenH * nScale - piece.art.nHeight);
    for (int ty = 0; ty < nScale; ++ty)
      for (int tx = 0; tx < nScale; ++tx)
        if (PortHdLayerSolid(piece.art, px0 + tx, py0 + ty))
          return true;
  }
  return false;
}

// The screen-wide image for the shifts: 1.17's regions at their places, cut to the
// pieces' shape, nFill where only a piece is.
void MakeWide(const PortHdConsole *pConsole, int nWidth)
{
  const int nHeight = g_nArtH;
  g_wide.assign(static_cast<size_t>(nWidth) * nHeight, 0);
  for (int r = 0; r < REGIONS; ++r)
    for (int y = 0; y < nHeight; ++y)
      for (int x = kanCut[r]; x < kanCut[r + 1] && x < g_nArtW; ++x) {
        const int sx = x + g_anShift[r];
        if (sx >= 0 && sx < nWidth)
          g_wide[static_cast<size_t>(y) * nWidth + sx] = g_art[static_cast<size_t>(y) * g_nArtW + x];
      }
  // the fill: the darkest colour of 1.17's image that is not 0
  unsigned char nFill = 0;
  {
    unsigned int anCount[256] = {0};
    for (size_t i = static_cast<size_t>(g_nArtH - 8) * g_nArtW; i < g_art.size(); ++i)
      ++anCount[g_art[i]];
    unsigned int nBest = 0;
    for (int v = 1; v < 256; ++v)
      if (anCount[v] > nBest) {
        nBest = anCount[v];
        nFill = static_cast<unsigned char>(v);
      }
  }
  int anPieceShift[3];
  for (int i = 0; i < 3; ++i)
    anPieceShift[i] = g_anShift[kanPieceRegion[i]];
  const int nTop = nHeight - 200;  // no piece is taller than 200 game pixels
  for (int y = 0; y < nHeight; ++y)
    for (int x = 0; x < nWidth; ++x) {
      unsigned char &b = g_wide[static_cast<size_t>(y) * nWidth + x];
      const bool bSolid = y >= nTop && PieceSolid(pConsole, anPieceShift, x, y, nHeight);
      if (!bSolid)
        b = 0;
      else if (b == 0)
        b = nFill;
    }
}

// Rectangle a less rectangle b, into up to four, appended to out.
void Subtract(const RECT &a, const RECT &b, std::vector<RECT> *pOut)
{
  if (b.left >= a.right || b.right <= a.left || b.top >= a.bottom || b.bottom <= a.top) {
    pOut->push_back(a);
    return;
  }
  if (b.top > a.top)
    pOut->push_back({a.left, a.top, a.right, b.top});
  if (b.bottom < a.bottom)
    pOut->push_back({a.left, b.bottom, a.right, a.bottom});
  const LONG nTop = Max(a.top, b.top), nBottom = Min(a.bottom, b.bottom);
  if (b.left > a.left)
    pOut->push_back({a.left, nTop, b.left, nBottom});
  if (b.right < a.right)
    pOut->push_back({b.right, nTop, a.right, nBottom});
}

// The fluff panels for the screen-wide image: strips of it from its top in each, less
// the region dialogs (but the message entry, which 1.17's panels run under too).
void MakePanels(int nWidth)
{
  std::vector<RECT> rects;
  for (int x0 = 0; x0 < nWidth; x0 += STRIP) {
    const int x1 = Min(x0 + STRIP, nWidth);
    int nTop = g_nArtH;
    for (int y = 0; y < g_nArtH && nTop == g_nArtH; ++y)
      for (int x = x0; x < x1; ++x)
        if (g_wide[static_cast<size_t>(y) * nWidth + x] != 0) {
          nTop = y;
          break;
        }
    if (nTop < g_nArtH)
      rects.push_back({x0, nTop, x1, g_nArtH});
  }
  for (const ctrl_t *pDlg = gpDlgList; pDlg != NULL; pDlg = pDlg->pNext) {
    if (DialogRegion(pDlg) < 0 || pDlg == sgpTextBoxDlg)
      continue;
    const RECT cut = {pDlg->rc.left, pDlg->rc.top, pDlg->rc.right + 1, pDlg->rc.bottom + 1};
    std::vector<RECT> next;
    for (const RECT &r : rects)
      Subtract(r, cut, &next);
    rects.swap(next);
  }
  int n = 0;
  for (const RECT &r : rects) {
    if (n == PANELS_MAX)
      break;
    g_aPanels[n].wX = static_cast<WORD>(r.left);
    g_aPanels[n].wY = static_cast<WORD>(r.top);
    g_aPanels[n].wWidth = static_cast<WORD>(r.right - r.left);
    g_aPanels[n].wHeight = static_cast<WORD>(r.bottom - r.top);
    g_aPanels[n].pDlg = NULL;
    ++n;
  }
  g_aPanels[n].wX = 0xFFFF;
}

// The console's mask from an image placed at nX, and the rows console_collision takes
// from it.
void SetMask(const unsigned char *pPixels, int nWidth, int nX)
{
  bmp_t bmp;
  bmp.nWidth = static_cast<short>(nWidth);
  bmp.nHeight = static_cast<short>(g_nArtH);
  bmp.pPixels = const_cast<unsigned char *>(pPixels);
  cursor_set_console_offset(&bmp);
  if (sgConsoleArtMask_g == 0)
    return;
  BLTMASK *pNode = reinterpret_cast<BLTMASK *>(sgConsoleArtMask_g);
  PortMaskDelete(pNode->hMask);
  pNode->hMask = PortMaskCreate(pPixels, nWidth, g_nArtH);
  pNode->nLeft = nX;
  pNode->nTop = 0;
  pNode->nRight = nX + nWidth;
  pNode->nBottom = g_nArtH;
  vid_mask_initialize();
}

// The fluff panels opened again from pPanels (NULL: 1.17's), with sgConsoleArt the image
// they copy their art from while they open.
void ReloadPanels(fluf_t *pPanels, unsigned char *pPixels, int nWidth, bool bWide)
{
  const bmp_t saved = sgConsoleArt;
  sgConsoleArt.nWidth = static_cast<short>(nWidth);
  sgConsoleArt.nHeight = static_cast<short>(g_nArtH);
  sgConsoleArt.pPixels = pPixels;
  g_bWideArt = bWide;
  g_bPanels = pPanels != NULL;
  statfluf_port_panels(pPanels, true);
  g_bWideArt = false;
  sgConsoleArt = saved;
}

// Each dialog moved from its place under the old layout to its place under the new.
void MoveDialogs(const int anOld[REGIONS], const int anNew[REGIONS])
{
  for (ctrl_t *pDlg = gpDlgList; pDlg != NULL; pDlg = pDlg->pNext) {
    const int r = DialogRegion(pDlg);
    if (r < 0)
      continue;
    const int nShift = anNew[r] - anOld[r];
    pDlg->rc.left = static_cast<short>(pDlg->rc.left + nShift);
    pDlg->rc.right = static_cast<short>(pDlg->rc.right + nShift);
    if (pDlg == sgpMinimapDlg) {
      sgMinimapArea_g.left += nShift;
      sgMinimapArea_g.right += nShift;
    }
  }
}

void Split(const PortHdConsole *pConsole)
{
  const int cx = g_portView.nConsoleX;
  const int nSpread = Min(cx, SpreadMax(pConsole));
  const int anOld[REGIONS] = {cx, cx, cx};
  const int anNew[REGIONS] = {cx - nSpread, cx, cx + nSpread};
  const int nWidth = g_portView.nWidth;
  for (int r = 0; r < REGIONS; ++r)
    g_anShift[r] = anNew[r];
  g_bSplit = true;
  g_nSplitWidth = nWidth;
  MoveDialogs(anOld, anNew);
  MakeWide(pConsole, nWidth);
  MakePanels(nWidth);
  ReloadPanels(g_aPanels, g_wide.data(), nWidth, true);
  SetMask(g_wide.data(), nWidth, 0);
  int anPieceShift[3];
  for (int i = 0; i < 3; ++i)
    anPieceShift[i] = g_anShift[kanPieceRegion[i]];
  PortGpuConsoleArt(g_wide.data(), nWidth, g_nArtH, 0, g_nSkin);
  PortGpuConsolePieces(anPieceShift);
  dlg_set_all_update();
  minimap_force_redraw();
  gamemap_force_redraw();
}

void Unsplit(void)
{
  const int cx = g_portView.nConsoleX;
  const int anNew[REGIONS] = {cx, cx, cx};
  g_bSplit = false;
  MoveDialogs(g_anShift, anNew);
  for (int &n : g_anShift)
    n = cx;
  ReloadPanels(NULL, g_art.data(), g_nArtW, false);
  SetMask(g_art.data(), g_nArtW, cx);
  PortGpuConsoleArt(g_art.data(), g_nArtW, g_nArtH, cx, g_nSkin);
  dlg_set_all_update();
  minimap_force_redraw();
  gamemap_force_redraw();
}

}  // namespace

void PortConsoleSkinTake(void)
{
  static const int kanRace[4] = {-1, 1, 2, 0};  // by race, Terran, Protoss, Zerg
  double d = 0.0;
  PortSettingNumber("hyperion", "consoleSkin", &d);
  const int n = static_cast<int>(d);
  g_nSkinRace = gPlayback == 0 && n >= 1 && n <= 3 ? kanRace[n] : -1;
  double dEdges = 1.0;
  PortSettingNumber("hyperion", "consoleLayout", &dEdges);
  g_bEdges = dEdges != 0.0;
}

int PortConsoleRace(void)
{
  return g_nSkinRace >= 0 ? g_nSkinRace : byte_5264C2;
}

void PortConsoleEdgesSet(bool bOn)
{
  g_bEdges = bOn;
}

bool PortConsoleModArt(int nSkin, bmp_t *pArt)
{
  // the active mod's hud.json, read again for each console: its console, and the minimap's and
  // the selection's looks
  PortJson hud;
  std::string folder;
  ReadHudFile(&hud, &folder);
  ReadLayout(hud["console"], folder);
  PortMinimapHud(hud["minimap"]);
  PortSelectionHud(hud["selection"]);
  g_bModArt = false;
  if (nSkin < 0 || nSkin >= SKINS || g_layout.art[nSkin].empty())
    return false;
  std::string bytes;
  unsigned char *pPixels = NULL;
  int nWidth = 0, nHeight = 0;
  if (!PortReadFile(g_layout.art[nSkin], &bytes) ||
      !PortBmpDecodePcx(bytes.data(), bytes.size(), &pPixels, &nWidth, &nHeight)) {
    Log("console: %s is not a PCX of 8 bits a pixel; 1.17's console shows", g_layout.art[nSkin].c_str());
    return false;
  }
  if (nWidth != 640 || nHeight != 480) {
    Log("console: %s is %d by %d, not 640 by 480; 1.17's console shows", g_layout.art[nSkin].c_str(), nWidth,
        nHeight);
    SMemFree(pPixels, "port_console.cpp", __LINE__, 0);
    return false;
  }
  pArt->nWidth = static_cast<short>(nWidth);
  pArt->nHeight = static_cast<short>(nHeight);
  pArt->pPixels = pPixels;
  g_bModArt = true;
  return true;
}

void PortConsoleArtLoaded(const unsigned char *pPixels, int nWidth, int nHeight, int nSkin)
{
  // a new console comes up laid out as 1.17's, its fluff panels 1.17's
  g_bSplit = false;
  if (g_bPanels) {
    statfluf_port_panels(NULL, false);
    g_bPanels = false;
  }
  // a mod's own image shows whole: Remastered's pieces would cover it with 1.17's art; the HUD's
  // console shows none
  g_bHudConsole = PortHudPlays();
  if (g_bModArt || g_bHudConsole)
    nSkin = -1;
  g_nSkin = nSkin;
  g_nArtW = nWidth;
  g_nArtH = nHeight;
  if (pPixels != NULL)
    g_art.assign(pPixels, pPixels + static_cast<size_t>(nWidth) * nHeight);
  else
    g_art.clear();
  PortGpuConsoleArt(pPixels, nWidth, nHeight, g_portView.nConsoleX, nSkin);
}

int PortConsoleShift(const ctrl_t *pDlg)
{
  const int r = g_bSplit ? DialogRegion(pDlg) : -1;
  if (r >= 0)
    return g_anShift[r];
  // the screen-wide panels are placed on the screen already
  return g_bSplit && IsPanel(pDlg) ? 0 : g_portView.nConsoleX;
}

int PortConsoleArtX(void)
{
  return g_bWideArt ? 0 : g_portView.nConsoleX;
}

int PortConsoleSplit(void)
{
  return g_bSplit ? 1 : 0;
}

static void ConsoleFreedHidden(void);  // below: a hidden console's record cleared

void PortConsoleFreed(void)
{
  ConsoleFreedHidden();
  if (g_bHudRows)
    PortViewFullRows(false, false);
  g_bHudConsole = g_bHudRows = false;
  if (g_layout.bFullMap)
    PortViewFullRows(false, false);  // the next game's rows are 1.17's until its own layout says
  g_bSplit = false;
  if (g_bPanels) {
    statfluf_port_panels(NULL, false);
    g_bPanels = false;
  }
}

void PortConsoleUnsplit(void)
{
  if (g_bSplit)
    Unsplit();
}

namespace {

bool g_bConsoleHidden;            // a benchmark's run: no console
std::vector<ctrl_t *> g_hiddenDlgs;  // the dialogs it hid, to show again

void HideDlg(ctrl_t *pDlg)
{
  if (pDlg == NULL || (pDlg->dwFlags & 8) == 0)
    return;
  ctrl_hide(pDlg);
  if (std::find(g_hiddenDlgs.begin(), g_hiddenDlgs.end(), pDlg) == g_hiddenDlgs.end())
    g_hiddenDlgs.push_back(pDlg);
}

// The console's art panels hidden: the four races' lists, 26 records in all, each list ending in
// a wX of 0xFFFF.
void PanelsHide(void)
{
  fluf_t *pAll = StatFlufPanels();
  for (fluf_t *p = pAll; p != NULL && p < pAll + 26; ++p)
    if (p->wX != 0xFFFF)
      HideDlg(p->pDlg);
}

// The console's dialogs and its art panels hidden. The game shows some of them again as the
// selection changes, so a hidden console is hidden again each frame.
void ConsoleHide(void)
{
  ctrl_t *const apDlg[] = {sgpMinimapDlg, sgpStatDataDlg, sgpPortDlg, sgpStatCmdDlg, sgpTextBoxDlg, sgpStatMiscDlg,
                           sgpStatResDlg};
  for (ctrl_t *pDlg : apDlg)
    HideDlg(pDlg);
  PanelsHide();
}

// No console any more: in a game the dialogs it hid shown again; as the console is freed,
// only the record cleared, the dialogs going with it.
void ConsoleBack(bool bInGame)
{
  if (bInGame)
    for (ctrl_t *pDlg : g_hiddenDlgs)
      ctrl_show(pDlg);
  g_hiddenDlgs.clear();
  g_bConsoleHidden = false;
  PortViewFullRows(false, bInGame);
}

}  // namespace

static void ConsoleFreedHidden(void)
{
  if (g_bConsoleHidden)
    ConsoleBack(false);
  else
    g_hiddenDlgs.clear();  // the HUD's hidden panels, freed with the console
}

bool PortConsoleHidden(void)
{
  return g_bConsoleHidden;
}

bool PortConsoleMaskLeftOut(intptr_t nMask)
{
  return (g_bConsoleHidden || g_bHudConsole) && nMask != 0 && nMask == sgConsoleArtMask_g;
}

bool PortConsoleHudOn(void)
{
  return g_bHudConsole;
}

bool PortConsoleRebuilding(void)
{
  return g_bRebuilding;
}

bool PortConsoleModUi(int nKind, unsigned nFrame)
{
  return g_bModUi && PortModUiHas(nKind, nFrame);
}

bool PortConsoleHudHeld(const ctrl_t *pDlg)
{
  return g_bHudConsole && pDlg != NULL && pDlg != g_pHudDelivering &&
         (pDlg == sgpMinimapDlg || pDlg == sgpStatDataDlg || pDlg == sgpPortDlg || pDlg == sgpStatCmdDlg ||
          pDlg == sgpStatMiscDlg || pDlg == sgpStatResDlg);
}

bool PortConsoleHudBlank(bmp_t *pBmp)
{
  if (!g_bHudConsole)
    return false;
  if (pBmp->pPixels != NULL)
    memset(pBmp->pPixels, 0, static_cast<size_t>(pBmp->nWidth) * pBmp->nHeight);
  return true;
}

namespace {

// A control's rectangle of its dialog's bitmap, inside the bitmap.
bool CtrlPixels(const ctrl_t *pDlg, const rect16_t &rc, PortHudPixels *p)
{
  if (pDlg == NULL || (pDlg->dwFlags & 8) == 0)
    return false;
  const bmp_t *pBmp = reinterpret_cast<const bmp_t *>(&pDlg->_unmapped36[0]);
  const int x0 = Max(0, rc.left), y0 = Max(0, rc.top);
  const int x1 = Min(pBmp->nWidth, rc.right + 1), y1 = Min(pBmp->nHeight, rc.bottom + 1);
  if (pBmp->pPixels == NULL || x1 <= x0 || y1 <= y0)
    return false;
  p->pPixels = pBmp->pPixels + static_cast<size_t>(y0) * pBmp->nWidth + x0;
  p->nPitch = pBmp->nWidth;
  p->nWidth = x1 - x0;
  p->nHeight = y1 - y0;
  return true;
}

// A shown control and its rectangle in its dialog.
bool CtrlPart(ctrl_t *pDlg, const ctrl_t *pCtrl, ctrl_t **ppDlg, rect16_t *pRect)
{
  if (pDlg == NULL || pCtrl == NULL || (pDlg->dwFlags & 8) == 0 || (pCtrl->dwFlags & 8) == 0)
    return false;
  *ppDlg = pDlg;
  *pRect = pCtrl->rc;
  return true;
}

// Sets pMask's pixels where the picture still shows pixels (palette indices, -1 clear, w by h of
// a row of nStride) as placed at x0, y0; whether any was.
bool MaskWhereShown(const std::vector<int> &pixels, int nStride, int x0, int y0, int w, int h, const PortHudPixels &px,
                    unsigned char *pMask, int nWidth, int nHeight)
{
  bool bAny = false;
  const int nW = Min(nWidth, px.nWidth), nH = Min(nHeight, px.nHeight);
  for (int y = 0; y < h; ++y) {
    const int py = y0 + y;
    if (py < 0 || py >= nH)
      continue;
    for (int x = 0; x < w; ++x) {
      const int pxX = x0 + x, v = pixels[static_cast<size_t>(y) * nStride + x];
      if (v >= 0 && pxX >= 0 && pxX < nW && px.pPixels[static_cast<size_t>(py) * px.nPitch + pxX] == v) {
        pMask[static_cast<size_t>(py) * nWidth + pxX] = 1;
        bAny = true;
      }
    }
  }
  return bAny;
}

// A box of cmdbtns.grp that takes the player's colour, in its unlit frame: a selected unit's in
// its normal state (statdata_select_btn_draw); one unit's slots: a plain one (0), a unit, add-on,
// research or upgrade being made and the queue's ends (2), the queue's middle (4), its armour,
// weapons, upgrades and kills (0x0C); a passenger's in its normal state, small, medium or large
// (statdraw_cargo_frame). The other states' boxes keep their colours, which say something.
bool AccentBox(bool bSelect, bool bCargo, WORD wFrame)
{
  if (bSelect)
    return wFrame == 0x0D;
  if (bCargo)
    return wFrame == 0x0D || wFrame == 0x0F || wFrame == 0x11;
  return wFrame == 0 || wFrame == 2 || wFrame == 4 || wFrame == 0x0C;
}

}  // namespace

bool PortConsoleHudPart(int nSource, int nIndex, ctrl_t **ppDlg, rect16_t *pRect)
{
  if (!g_bHudConsole || gwGameMode != 3)
    return false;
  switch (nSource) {
  case PORT_HUD_SRC_MINIMAP:
    return sgpMinimapDlg != NULL && CtrlPart(sgpMinimapDlg, ctrl_find_ndx(sgpMinimapDlg, 1), ppDlg, pRect);
  case PORT_HUD_SRC_SELECTION:
    if (sgpStatDataDlg == NULL || (sgpStatDataDlg->dwFlags & 8) == 0)
      return false;
    *ppDlg = sgpStatDataDlg;
    pRect->left = pRect->top = 0;
    pRect->right = static_cast<short>(sgpStatDataDlg->rc.right - sgpStatDataDlg->rc.left);
    pRect->bottom = static_cast<short>(sgpStatDataDlg->rc.bottom - sgpStatDataDlg->rc.top);
    return true;
  case PORT_HUD_SRC_PORTRAIT:
    return sgpStatPortCtrl != NULL && sgpStatPortCtrl->pParent == sgpPortDlg &&
           CtrlPart(sgpPortDlg, sgpStatPortCtrl, ppDlg, pRect);
  case PORT_HUD_SRC_CARD:
    // place n is the control numbered n + 1, as the HUD's presses have it (port_hudstate.cpp)
    return sgpStatCmdDlg != NULL && nIndex >= 0 && nIndex < 9 &&
           CtrlPart(sgpStatCmdDlg, ctrl_find_ndx(sgpStatCmdDlg, static_cast<short>(nIndex + 1)), ppDlg, pRect);
  case PORT_HUD_SRC_RESOURCE: {
    // the counter's icons, 14 by 14 at a control's left and its middle across, minerals first
    const ctrl_t *apIcons[8];
    int n = 0;
    for (const ctrl_t *pCtrl = sgpStatResDlg != NULL ? sgpStatResDlg->pChildren : NULL; pCtrl != NULL && n < 8;
         pCtrl = pCtrl->pNext) {
      const int nIcon = *reinterpret_cast<const WORD *>(&pCtrl->_unmapped24[0]) & 0xFF;
      if ((pCtrl->dwFlags & 8) != 0 && pCtrl->nIndex != 6 && nIcon < 11 &&
          pCtrl->pfnDraw == reinterpret_cast<PFN_CTRL_DRAW>(statres_resource_ctrl_draw))
        apIcons[n++] = pCtrl;
    }
    std::sort(apIcons, apIcons + n, [](const ctrl_t *a, const ctrl_t *b) { return a->rc.left < b->rc.left; });
    if (nIndex < 0 || nIndex >= n || !CtrlPart(sgpStatResDlg, apIcons[nIndex], ppDlg, pRect))
      return false;
    const int kIcon = 14;
    pRect->top = static_cast<short>(pRect->top + (pRect->bottom - pRect->top + 1 - kIcon) / 2);
    pRect->right = static_cast<short>(pRect->left + kIcon - 1);
    pRect->bottom = static_cast<short>(pRect->top + kIcon - 1);
    return true;
  }
  case PORT_HUD_SRC_BUTTON:
    if (nIndex == PORT_HUD_BTN_MENU)
      return sgpStatMiscDlg != NULL && CtrlPart(sgpStatMiscDlg, sgpStatMiscDlg->pChildren, ppDlg, pRect);
    if (sgpMinimapDlg == NULL || nIndex < PORT_HUD_BTN_TERRAIN || nIndex > PORT_HUD_BTN_MESSAGING)
      return false;
    return CtrlPart(sgpMinimapDlg,
                    ctrl_find_ndx(sgpMinimapDlg, nIndex == PORT_HUD_BTN_TERRAIN     ? 2
                                                 : nIndex == PORT_HUD_BTN_DIPLOMACY ? 4
                                                                                    : 3),
                    ppDlg, pRect);
  }
  return false;
}

bool PortConsoleHudSource(int nSource, int nIndex, PortHudPixels *p)
{
  ctrl_t *pDlg = NULL;
  rect16_t rc;
  return PortConsoleHudPart(nSource, nIndex, &pDlg, &rc) && CtrlPixels(pDlg, rc, p);
}

bool PortConsoleHudSelectionBoxes(unsigned char *pMask, int nWidth, int nHeight)
{
  PortHudPixels px;
  if (pMask == NULL || gpCmdBtns == NULL || !PortConsoleHudSource(PORT_HUD_SRC_SELECTION, 0, &px))
    return false;
  memset(pMask, 0, static_cast<size_t>(nWidth) * nHeight);
  bool bAny = false;
  std::vector<int> pixels;
  for (const ctrl_t *pCtrl = sgpStatDataDlg->pChildren; pCtrl != NULL; pCtrl = pCtrl->pNext) {
    if ((pCtrl->dwFlags & 8) == 0)
      continue;
    const int nCw = pCtrl->rc.right - pCtrl->rc.left + 1, nCh = pCtrl->rc.bottom - pCtrl->rc.top + 1;
    // the progress bar's empty part, its frame (statdata_progressbar_draw); the full part, the
    // progress itself, keeps its colours, but where it repeats the empty picture's frame (as
    // Remastered's does, port_remastered.cpp) the frame takes the colour as the bar fills (the
    // user, 2026-10-26: the frame turned blue behind the fill)
    if (pCtrl->pfnDraw == reinterpret_cast<PFN_CTRL_DRAW>(statdata_progressbar_draw)) {
      const bmp_t &empty = sgBmpStatProgressbarEmpty_g;
      if (empty.pPixels == NULL)
        continue;
      const int w = Min(nCw, empty.nWidth), h = Min(nCh, empty.nHeight);
      const int nFull = nCw * *reinterpret_cast<const WORD *>(&pCtrl->_unmapped24[0]) / 100;
      pixels.assign(static_cast<size_t>(w) * h, -1);
      for (int y = 0; y < h; ++y)
        for (int x = Max(0, nFull); x < w; ++x)
          if (const BYTE b = empty.pPixels[static_cast<size_t>(y) * empty.nWidth + x])
            pixels[static_cast<size_t>(y) * w + x] = b;
      bAny |= MaskWhereShown(pixels, w, pCtrl->rc.left, pCtrl->rc.top, w, h, px, pMask, nWidth, nHeight);
      const bmp_t &full = sgBmpStatProgressbarFull_g;
      const int nW = Min(nWidth, px.nWidth), nH = Min(nHeight, px.nHeight);
      for (int y = 0; full.pPixels != NULL && y < Min(nCh, full.nHeight); ++y) {
        const int py = pCtrl->rc.top + y;
        for (int x = 0; x < Min(nFull, full.nWidth); ++x) {
          const int pxX = pCtrl->rc.left + x;
          const BYTE b = full.pPixels[static_cast<size_t>(y) * full.nWidth + x];
          if (b == 0 || py < 0 || py >= nH || pxX < 0 || pxX >= nW || px.pPixels[static_cast<size_t>(py) * px.nPitch + pxX] != b ||
              x >= empty.nWidth || y >= empty.nHeight || empty.pPixels[static_cast<size_t>(y) * empty.nWidth + x] != b)
            continue;
          pMask[static_cast<size_t>(py) * nWidth + pxX] = 1;
          bAny = true;
        }
      }
      // and its frame over the fill, which the HUD draws (PortConsoleHudBarEdge)
      int x0 = 0, y0 = 0, ew = 0, eh = 0;
      if (PortConsoleHudBarEdge(&pixels, &x0, &y0, &ew, &eh))
        for (int y = 0; y < eh; ++y)
          for (int x = 0; x < ew; ++x)
            if (pixels[static_cast<size_t>(y) * ew + x] >= 0 && x0 + x < nWidth && y0 + y < nHeight) {
              pMask[static_cast<size_t>(y0 + y) * nWidth + x0 + x] = 1;
              bAny = true;
            }
      continue;
    }
    // the arrow from the unit being made to the queue (statdata_queue_arrow_draw: frame 0x0B,
    // centred across without the +1, at the top)
    if (pCtrl->pfnDraw == reinterpret_cast<PFN_CTRL_DRAW>(statdata_queue_arrow_draw)) {
      FRAME frame;
      grhsize_t size;
      grh_get_header(static_cast<GROUP *>(gpCmdBtns), 0x0B, &frame, &size);
      PortGrpFrameDecode(frame, &pixels);
      bAny |= MaskWhereShown(pixels, frame.bWidth, pCtrl->rc.left + (nCw - 1 - frame.bWidth) / 2, pCtrl->rc.top,
                             Min(nCw, frame.bWidth), Min(nCh, frame.bHeight), px, pMask, nWidth, nHeight);
      continue;
    }
    const bool bSelect = pCtrl->pfnDraw == reinterpret_cast<PFN_CTRL_DRAW>(statdata_select_btn_draw);
    const bool bCargo = pCtrl->pfnDraw == reinterpret_cast<PFN_CTRL_DRAW>(statdata_transport_btn_draw);
    const bool bSlot = pCtrl->pfnDraw == reinterpret_cast<PFN_CTRL_DRAW>(statdata_icon_info_draw) ||
                       pCtrl->pfnDraw == reinterpret_cast<PFN_CTRL_DRAW>(statdata_btn_queue_draw);
    if (!bSelect && !bSlot && !bCargo)
      continue;
    // an empty place in the queue: its number, an icon of cmdbtns.grp through the icon palette
    // (statdraw_draw_training_unit, statdata_slot_icon_draw)
    const statdata_icon_t *pIcon = bSlot ? *reinterpret_cast<statdata_icon_t *const *>(&pCtrl->_unmapped24[2]) : NULL;
    if (pIcon != NULL && pIcon->pGrp == gpCmdBtns && pIcon->wKind == 6) {
      FRAME frame;
      grhsize_t size;
      grh_get_header(static_cast<GROUP *>(gpCmdBtns), pIcon->wFrame, &frame, &size);
      PortGrpFrameDecode(frame, &pixels);
      const int nPal = (pCtrl->dwFlags & 2) || pCtrl->wType != 2 ? 4 : 3;
      for (int &v : pixels)
        if (v >= 0 && v < 16)
          v = sgBmpTIcon_g[nPal * 16 + v];
      bAny |= MaskWhereShown(pixels, frame.bWidth, pCtrl->rc.left + (nCw - frame.bWidth) / 2,
                             pCtrl->rc.top + (nCh - frame.bHeight) / 2, frame.bWidth, frame.bHeight, px, pMask, nWidth,
                             nHeight);
    }
    // the box's frame of cmdbtns.grp, the next for a lit button, placed as 1.17 places it
    WORD wFrame = *reinterpret_cast<const WORD *>(&pCtrl->_unmapped24[0]);
    if (!AccentBox(bSelect, bCargo, wFrame))
      continue;
    if (pCtrl->dwFlags & 0x40000000)
      ++wFrame;
    FRAME frame;
    grhsize_t size;
    grh_get_header(static_cast<GROUP *>(gpCmdBtns), wFrame, &frame, &size);
    PortGrpFrameDecode(frame, &pixels);
    // the console's subgroup's box, drawn lighter (statdata_select_btn_draw)
    const statdata_select_t *pSel = bSelect ? *reinterpret_cast<statdata_select_t *const *>(&pCtrl->_unmapped24[2]) : NULL;
    if (SelectionWide() && SelectionSubgroups() > 1 && gpStatUnit != NULL && pSel != NULL && pSel->pUnit != NULL &&
        pSel->pUnit->wType == gpStatUnit->wType) {
      const BYTE *pLight = PortConsoleLightRemap();
      for (int &v : pixels)
        if (v >= 0)
          v = pLight[v];
    }
    // the box's pixels, where the wireframe or the icon drawn over it left them
    const int w = Min(static_cast<int>(frame.bWidth), nCw), h = Min(static_cast<int>(frame.bHeight), nCh);
    bAny |= MaskWhereShown(pixels, frame.bWidth, pCtrl->rc.left + (nCw - w) / 2, pCtrl->rc.top + (nCh - h) / 2, w, h,
                           px, pMask, nWidth, nHeight);
  }
  return bAny;
}

bool PortConsoleHudBarEdge(std::vector<int> *pPixels, int *pnX, int *pnY, int *pnWidth, int *pnHeight)
{
  PortHudPixels px;
  const bmp_t &empty = sgBmpStatProgressbarEmpty_g;
  if (pPixels == NULL || empty.pPixels == NULL || !PortConsoleHudSource(PORT_HUD_SRC_SELECTION, 0, &px))
    return false;
  for (const ctrl_t *pCtrl = sgpStatDataDlg->pChildren; pCtrl != NULL; pCtrl = pCtrl->pNext) {
    if ((pCtrl->dwFlags & 8) == 0 || pCtrl->pfnDraw != reinterpret_cast<PFN_CTRL_DRAW>(statdata_progressbar_draw))
      continue;
    const int nCw = pCtrl->rc.right - pCtrl->rc.left + 1, nCh = pCtrl->rc.bottom - pCtrl->rc.top + 1;
    const int w = Min(nCw, empty.nWidth), h = Min(nCh, empty.nHeight);
    const int nFull = Min(w, nCw * *reinterpret_cast<const WORD *>(&pCtrl->_unmapped24[0]) / 100);
    if (nFull <= 0)
      return false;
    auto at = [&](int x, int y) {
      return x >= 0 && y >= 0 && x < empty.nWidth && y < empty.nHeight ? empty.pPixels[static_cast<size_t>(y) * empty.nWidth + x]
                                                                        : 0;
    };
    // a pixel of the empty picture within two of its clear outside, straight across or down
    auto edge = [&](int x, int y) {
      for (int d = 1; d <= 2; ++d)
        if (at(x - d, y) == 0 || at(x + d, y) == 0 || at(x, y - d) == 0 || at(x, y + d) == 0)
          return true;
      return false;
    };
    pPixels->assign(static_cast<size_t>(nFull) * h, -1);
    bool bAny = false;
    for (int y = 0; y < h; ++y) {
      const int py = pCtrl->rc.top + y;
      for (int x = 0; x < nFull; ++x) {
        const int pxX = pCtrl->rc.left + x;
        const BYTE b = at(x, y);
        if (b == 0 || !edge(x, y) || py < 0 || py >= px.nHeight || pxX < 0 || pxX >= px.nWidth ||
            px.pPixels[static_cast<size_t>(py) * px.nPitch + pxX] != 0)
          continue;
        (*pPixels)[static_cast<size_t>(y) * nFull + x] = b;
        bAny = true;
      }
    }
    *pnX = pCtrl->rc.left;
    *pnY = pCtrl->rc.top;
    *pnWidth = nFull;
    *pnHeight = h;
    return bAny;
  }
  return false;
}

void PortConsoleHudDeliver(const ctrl_t *pDlg)
{
  g_pHudDelivering = pDlg;
}

int PortConsoleHudLabel(int nButton, char *psz, int nMax)
{
  const ctrl_t *pCtrl = NULL;
  if (nButton == PORT_HUD_BTN_MENU)
    pCtrl = sgpStatMiscDlg != NULL ? sgpStatMiscDlg->pChildren : NULL;
  else if (sgpMinimapDlg != NULL)
    pCtrl = ctrl_find_ndx(sgpMinimapDlg, nButton == PORT_HUD_BTN_TERRAIN     ? 2
                                         : nButton == PORT_HUD_BTN_DIPLOMACY ? 4
                                                                            : 3);
  int n = 0;
  const unsigned char *pText = pCtrl != NULL ? static_cast<const unsigned char *>(pCtrl->pData) : NULL;
  // a first character that is the hotkey is not shown (draw_ctrl_text)
  if (pText != NULL && *pText != 0 && (pCtrl->dwFlags & 0x300) != 0)
    ++pText;
  for (; pText != NULL && *pText != 0 && n < nMax - 1; ++pText)
    if (*pText >= 0x20)  // its colour bytes left out
      psz[n++] = static_cast<char>(*pText);
  if (nMax > 0)
    psz[n] = 0;
  return n;
}

// The console built again for a design chosen or left during a game: its dialogs take their art,
// or none, as they are made. The selection is picked again from the game's.
static void Rebuild(void)
{
  g_bRebuilding = true;
  status_free();
  status_init();
  status_load_strings();
  g_bRebuilding = false;
  status_get_new_selection();
  dlg_set_all_update();
  gamemap_force_redraw();
  minimap_force_redraw();
}

void PortConsoleUpdate(void)
{
  if (gwGameMode == 3 && !g_bConsoleHidden && PortHudPlays() != g_bHudConsole)
    Rebuild();
  // a benchmark's run: no console (above)
  const bool bHide = gwGameMode == 3 && PortOverlayHas(PORT_OVERLAY_HIDES_CONSOLE);
  if (bHide) {
    ConsoleHide();
    if (!g_bConsoleHidden) {
      g_bConsoleHidden = true;
      PortViewFullRows(true, true);
    }
  } else if (g_bConsoleHidden) {
    ConsoleBack(gwGameMode == 3);
    gamemap_force_redraw();
    minimap_force_redraw();
  }
  // the HUD's console: 1.17's art panels hidden, the map the view's whole height
  if (gwGameMode == 3 && g_bHudConsole && !g_bConsoleHidden) {
    PanelsHide();
    if (!g_bHudRows) {
      g_bHudRows = true;
      PortViewFullRows(true, true);
    }
  }
  // a mod's image that leaves the map room below 1.17's rows (hud.json's fullMap)
  if (gwGameMode == 3 && !g_bConsoleHidden && !g_bHudConsole)
    PortViewFullRows(g_layout.bFullMap, true);
  // a mod's own icons and wireframe drawn by the GPU at the HD scale, and not by the console
  const bool bModUi = gwGameMode == 3 && !g_bHudConsole && PortGpuModUiShown() != 0;
  if (bModUi != g_bModUi) {
    g_bModUi = bModUi;
    dlg_set_all_update();
  }
  const PortHdConsole *pConsole = NULL;
  bool bWant = gwGameMode == 3 && !g_bHudConsole && !g_art.empty() && g_nArtW == 640 && g_nSkin >= 0 && g_nSkin < 3 &&
               g_layout.bToEdges && g_bEdges && sgConsoleArtMask_g != 0 && PortGpuHdConsoleShown() != 0;
  if (bWant) {
    pConsole = PortHdConsoleArt(g_nSkin);
    bWant = pConsole != NULL && pConsole->nPieces == 3 && SpreadMax(pConsole) > 0 && g_portView.nConsoleX > 0;
  }
  if (g_bSplit && (!bWant || g_nSplitWidth != g_portView.nWidth))
    Unsplit();
  if (!g_bSplit && bWant)
    Split(pConsole);
}

// ---------------------------------------------------------------------------
// The console's contents in HD: what its controls show, read each frame.

namespace {

// Whether wireframe frame nType of wirefram.grp is drawn in the damage shades (remap
// 0xD8 to 0xDB) rather than in parts (0xD0 to 0xD3): read from the frame's codes once.
bool WireShaded(int nType)
{
  static signed char s_abShaded[256];  // 0 not read, 1 parts, 2 shades
  if (nType < 0 || nType >= 256 || sgpWireFrames == NULL)
    return false;
  if (s_abShaded[nType] == 0) {
    FRAME frame;
    grhsize_t size;
    grh_get_header(sgpWireFrames, static_cast<WORD>(nType), &frame, &size);
    bool bShaded = false;
    const BYTE *pData = frame.pData;
    for (int y = 0; pData != NULL && y < frame.bHeight && !bShaded; ++y) {
      const BYTE *p = pData + reinterpret_cast<const WORD *>(pData)[y];
      for (int x = 0; x < frame.bWidth && !bShaded;) {
        const int c = *p++;
        if (c & 0x80) {
          x += c & 0x7F;
        } else if (c & 0x40) {
          bShaded = *p >= 0xD8 && *p <= 0xDB;
          ++p;
          x += c & 0x3F;
        } else {
          for (int i = 0; i < c && !bShaded; ++i)
            bShaded = p[i] >= 0xD8 && p[i] <= 0xDB;
          p += c;
          x += c;
        }
      }
    }
    s_abShaded[nType] = bShaded ? 2 : 1;
  }
  return s_abShaded[nType] == 2;
}

}  // namespace

void PortConsoleLighten(unsigned char *pRgb)
{
  for (int i = 0; i < 3; ++i)
    pRgb[i] = static_cast<unsigned char>(Min(255, pRgb[i] * 17 / 10 + 30));
}

const BYTE *PortConsoleLightRemap(void)
{
  static BYTE s_ab[256];
  static unsigned char s_rgb[768];
  static bool s_bMade;
  unsigned char rgb[768];
  PortPresentPaletteRGB(rgb);
  if (s_bMade && memcmp(rgb, s_rgb, sizeof rgb) == 0)
    return s_ab;
  memcpy(s_rgb, rgb, sizeof rgb);
  s_bMade = true;
  // each colour lightened, as the nearest of the palette's; 0 stays, which a frame never draws
  s_ab[0] = 0;
  for (int i = 1; i < 256; ++i) {
    unsigned char c[3] = {rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]};
    PortConsoleLighten(c);
    int nBest = i, nBestD = 1 << 30;
    for (int j = 1; j < 256; ++j) {
      const int dr = rgb[j * 3] - c[0], dg = rgb[j * 3 + 1] - c[1], db = rgb[j * 3 + 2] - c[2];
      const int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
      if (d < nBestD)
        nBestD = d, nBest = j;
    }
    s_ab[i] = static_cast<BYTE>(nBest);
  }
  return s_ab;
}

namespace {

// A frame of a GRP the console holds as palette indices, -1 clear, its own box.
bool GrpPixels(GROUP *pGrp, int nFrame, std::vector<int> *pPixels, int *pnWidth, int *pnHeight)
{
  if (pGrp == NULL || nFrame < 0)
    return false;
  FRAME frame;
  grhsize_t size;
  grh_get_header(pGrp, static_cast<WORD>(nFrame), &frame, &size);
  if (frame.pData == NULL || frame.bWidth == 0 || frame.bHeight == 0)
    return false;
  PortGrpFrameDecode(frame, pPixels);
  *pnWidth = frame.bWidth;
  *pnHeight = frame.bHeight;
  return true;
}

}  // namespace

bool PortConsoleHudBox(int nFrame, std::vector<int> *pPixels, int *pnWidth, int *pnHeight)
{
  return GrpPixels(static_cast<GROUP *>(gpCmdBtns), nFrame, pPixels, pnWidth, pnHeight);
}

bool PortConsoleSmallWire(unsigned nLooks, FRAME *pFrame)
{
  if (sgpMultSelWires == NULL || nLooks < *reinterpret_cast<const WORD *>(sgpMultSelWires))
    return false;
  memset(pFrame, 0, sizeof *pFrame);
  struct Small {
    std::vector<BYTE> data;  // the rows' offsets, then the rows, as a GRP frame's
    int w = 0, h = 0;
  };
  static std::vector<Small> s_small;
  static const GROUP *s_pFrom;
  if (s_pFrom != sgpWireFrames) {
    s_small.clear();
    s_pFrom = sgpWireFrames;
  }
  if (sgpWireFrames == NULL || nLooks >= *reinterpret_cast<const WORD *>(sgpWireFrames))
    return true;
  if (s_small.size() <= nLooks)
    s_small.resize(nLooks + 1);
  Small &s = s_small[nLooks];
  if (s.data.empty()) {
    std::vector<int> big;
    int bw = 0, bh = 0;
    if (!GrpPixels(sgpWireFrames, static_cast<int>(nLooks), &big, &bw, &bh))
      return true;
    // each pixel the first drawn of its square of four, so that a line one pixel wide stays
    s.w = (bw + 1) / 2;
    s.h = (bh + 1) / 2;
    std::vector<int> px(static_cast<size_t>(s.w) * s.h, -1);
    for (int y = 0; y < s.h; ++y)
      for (int x = 0; x < s.w; ++x)
        for (int k = 0; k < 4 && px[static_cast<size_t>(y) * s.w + x] < 0; ++k) {
          const int sx = 2 * x + (k & 1), sy = 2 * y + (k >> 1);
          if (sx < bw && sy < bh)
            px[static_cast<size_t>(y) * s.w + x] = big[static_cast<size_t>(sy) * bw + sx];
        }
    // encoded as 1.17's frames are: 0x80 | n clear, or n of the bytes that follow
    s.data.assign(static_cast<size_t>(s.h) * 2, 0);
    for (int y = 0; y < s.h; ++y) {
      const WORD wAt = static_cast<WORD>(s.data.size());
      memcpy(&s.data[static_cast<size_t>(y) * 2], &wAt, 2);
      const int *pRow = &px[static_cast<size_t>(y) * s.w];
      for (int x = 0; x < s.w;) {
        int n = 0;
        if (pRow[x] < 0) {
          while (x + n < s.w && pRow[x + n] < 0 && n < 0x7F)
            ++n;
          s.data.push_back(static_cast<BYTE>(0x80 | n));
        } else {
          while (x + n < s.w && pRow[x + n] >= 0 && n < 0x3F)
            ++n;
          s.data.push_back(static_cast<BYTE>(n));
          for (int i = 0; i < n; ++i)
            s.data.push_back(static_cast<BYTE>(pRow[x + i]));
        }
        x += n;
      }
    }
  }
  pFrame->bWidth = static_cast<BYTE>(s.w);
  pFrame->bHeight = static_cast<BYTE>(s.h);
  pFrame->pData = s.data.data();
  return true;
}

bool PortConsoleHudWire(CUnit *pUnit, std::vector<int> *pPixels, int *pnWidth, int *pnHeight)
{
  if (pUnit == NULL)
    return false;
  FRAME half;
  const unsigned nGroup = ModUnitArtFrame(MOD_ART_GRPWIRE, pUnit->wType);
  if (PortConsoleSmallWire(nGroup, &half)) {
    if (half.bWidth == 0 || half.bHeight == 0)
      return false;
    PortGrpFrameDecode(half, pPixels);
    *pnWidth = half.bWidth;
    *pnHeight = half.bHeight;
  } else if (!GrpPixels(sgpMultSelWires, static_cast<int>(nGroup), pPixels, pnWidth, pnHeight)) {
    return false;
  }
  // its parts' and its shields' colours, as statdata_select_btn_draw sets them before it draws
  sub_4A7F50(pUnit);
  sub_4A80C0(pUnit);
  sub_4A8030(pUnit);
  for (int &v : *pPixels)
    if (v >= 0)
      v = _gbRemapColorTbl[v];
  sub_4A7F10();
  return true;
}

// A unit type's wireframe (wirefram.grp, as the console's selection shows it) into a cell of
// nCell pixels square, as white with its parts' shading in the brightness, for the benchmark's
// overlay to tint by health (port_benchview.cpp). False when the wireframes are not loaded or the
// type has none.
bool PortWireframeCell(int nType, unsigned char *pRgba, int nPitchPx, int nCell)
{
  if (nType < 0 || sgpWireFrames == NULL)
    return false;
  FRAME frame;
  grhsize_t size;
  grh_get_header(sgpWireFrames, static_cast<WORD>(ModUnitArtFrame(MOD_ART_WIRE, static_cast<unsigned>(nType))), &frame,
                 &size);
  const BYTE *pData = frame.pData;
  if (pData == NULL || frame.bWidth == 0 || frame.bHeight == 0)
    return false;
  const int x0 = frame.bOffsetX, y0 = reinterpret_cast<const BYTE *>(&frame)[1];
  auto Put = [&](int x, int y, BYTE c) {
    const int px = x0 + x, py = y0 + y;
    if (px < 0 || py < 0 || px >= nCell || py >= nCell)
      return;
    // the four shades of a part (0xD0-0xD3) or of the damage (0xD8-0xDB), lightest first
    const unsigned char v = static_cast<unsigned char>(255 - (c & 3) * 40);
    unsigned char *q = pRgba + (static_cast<size_t>(py) * nPitchPx + px) * 4;
    q[0] = q[1] = q[2] = v;
    q[3] = 255;
  };
  for (int y = 0; y < frame.bHeight; ++y) {
    const BYTE *p = pData + reinterpret_cast<const WORD *>(pData)[y];
    for (int x = 0; x < frame.bWidth;) {
      const int c = *p++;
      if (c & 0x80) {
        x += c & 0x7F;
      } else if (c & 0x40) {
        for (int i = 0; i < (c & 0x3F); ++i)
          Put(x + i, y, *p);
        ++p;
        x += c & 0x3F;
      } else {
        for (int i = 0; i < c; ++i)
          Put(x + i, y, p[i]);
        p += c;
        x += c;
      }
    }
  }
  return true;
}

namespace {

void AddItem(PortHdUiItem *pItems, int nMax, int *pn, int nSet, int nFrame, int nCell, const ctrl_t *pDlg,
             const ctrl_t *pCtrl, int nDX, int nDY, const BYTE *pTints, int nTints)
{
  if (*pn >= nMax)
    return;
  PortHdUiItem &item = pItems[(*pn)++];
  item.nSet = nSet;
  item.nFrame = nFrame;
  item.nCell = nCell;
  item.nX = pDlg->rc.left + pCtrl->rc.left;
  item.nY = pDlg->rc.top + pCtrl->rc.top;
  item.nW = pCtrl->rc.right - pCtrl->rc.left + 1;
  item.nH = pCtrl->rc.bottom - pCtrl->rc.top + 1;
  item.nDX = nDX;
  item.nDY = nDY;
  item.nTints = nTints;
  for (int i = 0; i < nTints; ++i)
    item.abTint[i] = pTints[i];
}

}  // namespace

namespace {

// The numbers 1.17 writes over the selection panel's icons (a weapon's level, a queue's
// places), which the HD UI pass keeps over their HD icons as 1.17 drew them: for each control
// the pixels its last text changed (PortConsoleSlotTextEnd), and this frame's (PortConsoleHdTexts).
struct SlotText {
  const ctrl_t *pCtrl;
  int nX, nY, nW, nH;  // the box of the changed pixels, from the control's top left
  unsigned int adwBits[PORT_HD_TEXT_WORDS];
};
SlotText g_aSlotTexts[24];
int g_nSlotTextNext;
std::vector<unsigned char> g_slotBefore;
PortHdUiText g_ahdTexts[16];
int g_nHdTexts;
int g_aanMapBoxes[8][4];  // where the map shows through 1.17's drawing (PortConsoleHdMapBoxes)
int g_nMapBoxes;

// The control's box in the bitmap the dialogs draw into, cut to it; false when nothing is left.
bool SlotTextRect(const ctrl_t *pCtrl, const bmp_t *pBmp, RECT *pRect)
{
  if (pBmp == NULL || pBmp->pPixels == NULL)
    return false;
  pRect->left = SDL_max(0, static_cast<int>(pCtrl->rc.left));
  pRect->top = SDL_max(0, static_cast<int>(pCtrl->rc.top));
  pRect->right = SDL_min(static_cast<int>(pBmp->nWidth), pCtrl->rc.right + 1);
  pRect->bottom = SDL_min(static_cast<int>(pBmp->nHeight), pCtrl->rc.bottom + 1);
  return pRect->right > pRect->left && pRect->bottom > pRect->top;
}

// The HD portrait: the video of the SD one playing, and its picture on the frame last asked.
struct HdPortraitState {
  std::string sdName;  // the SD video it stands for
  PortVideo *pWebm;
  unsigned nFrame;
  std::vector<unsigned char> rgba;
  PortHdPortrait picture;
};
HdPortraitState g_hdPortrait;

}  // namespace

void PortConsoleSlotTextBegin(const ctrl_t *pCtrl)
{
  const bmp_t *pBmp = static_cast<const bmp_t *>(_bmp_current);
  RECT rc;
  g_slotBefore.clear();
  if (!SlotTextRect(pCtrl, pBmp, &rc))
    return;
  for (int y = rc.top; y < rc.bottom; ++y) {
    const unsigned char *pRow = pBmp->pPixels + static_cast<size_t>(y) * pBmp->nWidth;
    g_slotBefore.insert(g_slotBefore.end(), pRow + rc.left, pRow + rc.right);
  }
}

void PortConsoleSlotTextEnd(const ctrl_t *pCtrl)
{
  const bmp_t *pBmp = static_cast<const bmp_t *>(_bmp_current);
  RECT rc;
  if (!SlotTextRect(pCtrl, pBmp, &rc) ||
      g_slotBefore.size() != static_cast<size_t>(rc.right - rc.left) * (rc.bottom - rc.top))
    return;
  const int w = rc.right - rc.left, h = rc.bottom - rc.top;
  auto Changed = [&](int x, int y) {
    return pBmp->pPixels[static_cast<size_t>(rc.top + y) * pBmp->nWidth + rc.left + x] !=
           g_slotBefore[static_cast<size_t>(y) * w + x];
  };
  int x0 = w, y0 = h, x1 = -1, y1 = -1;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      if (Changed(x, y)) {
        x0 = SDL_min(x0, x);
        y0 = SDL_min(y0, y);
        x1 = SDL_max(x1, x);
        y1 = SDL_max(y1, y);
      }
  SlotText *pText = NULL;
  for (SlotText &t : g_aSlotTexts)
    if (t.pCtrl == pCtrl)
      pText = &t;
  if (pText == NULL) {
    pText = &g_aSlotTexts[g_nSlotTextNext];
    g_nSlotTextNext = (g_nSlotTextNext + 1) % static_cast<int>(SDL_arraysize(g_aSlotTexts));
  }
  pText->pCtrl = pCtrl;
  memset(pText->adwBits, 0, sizeof pText->adwBits);
  if (x1 < 0) {  // drawn in the colours already there
    pText->nW = pText->nH = 0;
    return;
  }
  // its right and bottom kept where the box holds fewer bits than it has
  pText->nW = x1 - x0 + 1;
  pText->nH = SDL_min(y1 - y0 + 1, PORT_HD_TEXT_WORDS * 32 / pText->nW);
  pText->nX = x0 + rc.left - pCtrl->rc.left;
  pText->nY = y1 + 1 - pText->nH + rc.top - pCtrl->rc.top;
  for (int y = 0; y < pText->nH; ++y)
    for (int x = 0; x < pText->nW; ++x)
      if (Changed(x0 + x, y1 + 1 - pText->nH + y)) {
        const int i = y * pText->nW + x;
        pText->adwBits[i >> 5] |= 1u << (i & 31);
      }
}

int PortConsoleHdMapBoxes(int (*panBoxes)[4], int nMax)
{
  const int n = SDL_min(nMax, g_nMapBoxes);
  memcpy(panBoxes, g_aanMapBoxes, n * sizeof *panBoxes);
  return n;
}

int PortConsoleHdTexts(PortHdUiText *pTexts, int nMax)
{
  const int n = SDL_min(nMax, g_nHdTexts);
  memcpy(pTexts, g_ahdTexts, n * sizeof *pTexts);
  return n;
}

const PortHdPortrait *PortHdPortraitFrame(void)
{
  HdPortraitState &s = g_hdPortrait;
  const char *pszFile = NULL;
  unsigned nFrame = 0, nInterval = 0;
  if (gwGameMode != 3 || !PortVidPlaying(sgpPortSmk_g, &pszFile, &nFrame, &nInterval) || pszFile[0] == 0)
    return NULL;
  if (s.sdName != pszFile) {
    // a new portrait: portrait\\<unit>\\<name>.smk is Remastered's <name>.webm at the scale's folder
    PortVideoClose(s.pWebm);
    s.pWebm = NULL;
    s.sdName = pszFile;
    s.nFrame = ~0u;
    std::string hd = (PortHdScale() == 4 ? "" : "HD2\\") + s.sdName;
    const size_t nDot = hd.rfind('.');
    if (nDot != std::string::npos)
      hd.replace(nDot, std::string::npos, ".webm");
    // a mod's portrait: its own video, in full colour at the size it is, with or without
    // Remastered's art
    char szMod[MAX_PATH];
    std::vector<BYTE> mod;
    std::string modName = hd.substr(PortHdScale() == 4 ? 0 : 4);
    s.picture.bMod = ModDataFilePath(modName.c_str(), szMod, sizeof szMod);
    if (s.picture.bMod) {
      // the first of the containers' extensions the mod's folder has
      const size_t nModDot = modName.rfind('.');
      for (const char *const *ppszExt = PortVideoExtensions(); *ppszExt && s.pWebm == NULL; ++ppszExt) {
        modName.replace(nModDot, std::string::npos, *ppszExt);
        if (ModDataFilePath(modName.c_str(), szMod, sizeof szMod) && PortReadFile(szMod, &mod))
          s.pWebm = PortVideoOpen(mod.data(), mod.size());
      }
    } else if (PortHdAvailable()) {
      size_t cb = 0;
      void *p = PortCascRead(hd.c_str(), &cb);
      if (p != NULL) {
        s.pWebm = PortVideoOpen(p, cb);
        SDL_free(p);
      }
    }
    if (s.pWebm != NULL) {
      s.picture.nWidth = PortVideoWidth(s.pWebm);
      s.picture.nHeight = PortVideoHeight(s.pWebm);
      s.rgba.assign(static_cast<size_t>(s.picture.nWidth) * s.picture.nHeight * 4, 0);
      s.picture.pRgba = s.rgba.data();
    }
  }
  if (s.pWebm == NULL || PortVideoFrames(s.pWebm) == 0)
    return NULL;
  // the HD frame at the SD one's time, the two at their own rates
  const unsigned nHdInterval = PortVideoInterval(s.pWebm);
  unsigned nHd = nHdInterval ? static_cast<unsigned>(static_cast<unsigned long long>(nFrame) * nInterval / nHdInterval)
                             : nFrame;
  nHd = SDL_min(nHd, PortVideoFrames(s.pWebm) - 1);
  if (nHd != s.nFrame) {
    if (!PortVideoFrame(s.pWebm, nHd, s.rgba.data(), s.picture.nWidth * 4))
      return s.nFrame == ~0u ? NULL : &s.picture;
    s.nFrame = nHd;
    ++s.picture.nSerial;
  }
  return &s.picture;
}

int PortConsoleHdItems(PortHdUiItem *pItems, int nMax, bool bConsole)
{
  int n = 0;
  g_nHdTexts = 0;
  g_nMapBoxes = 0;
  // a mod's own icons and wireframe: over Remastered's console, or over the classic one once
  // the console leaves them out (PortConsoleModUi)
  const bool bMod = bConsole || g_bModUi;
  if (gwGameMode != 3 || (!bConsole && !bMod))
    return 0;
  // the portrait, Remastered's HD video or a mod's own; Remastered's console's window for it
  // reaches 6 rows below the classic 60 x 56 box (measured on the Terran console)
  const ctrl_t *pPort = sgpStatPortCtrl;
  const ctrl_t *pPortDlg = pPort != NULL ? pPort->pParent : NULL;
  const PortHdPortrait *pPortrait = pPortDlg != NULL ? PortHdPortraitFrame() : NULL;
  if (pPortrait != NULL && (pPort->dwFlags & 8) != 0 && (pPortDlg->dwFlags & 8) != 0 && (bConsole || pPortrait->bMod) &&
      n < nMax) {
    AddItem(pItems, nMax, &n, PORT_HD_UI_PORTRAIT, 0, -1, pPortDlg, pPort, 0, 0, NULL, 0);
    if (bConsole)
      pItems[n - 1].nH += 6;
  }
  // the command card: each button's frame (not in a replay) and its icon, coloured by the
  // remap statcmd_btn_ctrl_draw_fn takes from ticon.pcx
  const ctrl_t *pCmd = sgpStatCmdDlg;
  if (pCmd != NULL && (pCmd->dwFlags & 8) != 0) {
    const int nRace = byte_5264C2 < 3 ? byte_5264C2 : 1;
    for (const ctrl_t *pCtrl = pCmd->pChildren; pCtrl != NULL; pCtrl = pCtrl->pNext) {
      if ((pCtrl->dwFlags & 8) == 0 ||
          pCtrl->pfnDraw != reinterpret_cast<PFN_CTRL_DRAW>(statcmd_btn_ctrl_draw_fn))
        continue;
      const WORD wIcon = *reinterpret_cast<const WORD *>(&pCtrl->_unmapped24[0]);
      const int nPressed = static_cast<int>((pCtrl->dwFlags >> 30) & 1);
      if (!gPlayback && bConsole)
        AddItem(pItems, nMax, &n, PORT_HD_UI_BUTTONS + nRace, nPressed, -1, pCmd, pCtrl, 0, 0, NULL, 0);
      const int i = (pCtrl->dwFlags & 2) != 0 ? 1 : (pCtrl->dwFlags & 0x40000000) != 0 || wIcon == word_6AAB00 ? 2 : 0;
      int nDX = 0, nDY = 0;
      if (gPlayback) {
        if (wIcon == 0x182 || wIcon == 0x183) {
          nDX = -2;
          nDY = 3;
        } else if (wIcon == 0x185) {
          nDX = -3;
          nDY = 3;
        } else if (wIcon == 0x184) {
          nDX = -4;
          nDY = 3;
        }
      }
      // a mod's own icon: tinted as Remastered's over its console, else by its remap's shade 1,
      // the brightest the icons use (the remap's other entries may be other colours)
      if (ModArtOwnFrame(MOD_ART_ICON, wIcon)) {
        if (bMod && PortModUiHas(MOD_ART_ICON, wIcon))
          AddItem(pItems, nMax, &n, PORT_HD_UI_MOD, MOD_ART_ICON << 16 | wIcon, 0, pCmd, pCtrl, nDX + nPressed,
                  nDY + nPressed, sgBmpTIcon_g + i * 16 + (bConsole ? 0 : 1), bConsole ? 16 : 1);
      } else if (bConsole) {
        AddItem(pItems, nMax, &n, PORT_HD_UI_ICONS, wIcon, -1, pCmd, pCtrl, nDX + nPressed, nDY + nPressed,
                sgBmpTIcon_g + i * 16, 16);
      }
    }
  }
  // the selection panel: each icon slot's button frame and icon as
  // statdata_icon_info_draw and statdata_btn_queue_draw draw them, in their order, with the
  // numbers 1.17 writes over them kept for the layer above; and the progress bar, as
  // statdata_progressbar_draw. Once the HD sets are in, so that a box is never left empty.
  const ctrl_t *pPanel = sgpStatDataDlg;
  if (pPanel != NULL && (pPanel->dwFlags & 8) != 0) {
    const int nRace = byte_5264C2 < 3 ? byte_5264C2 : 1;
    const bool bSlots =
        bConsole && PortHdUiSet(PORT_HD_UI_ICONS) != NULL && PortHdUiSet(PORT_HD_UI_BUTTONS + nRace) != NULL;
    const int nBarSet = PORT_HD_UI_BARS + SDL_clamp(PortConsoleRace(), 0, 2);
    const bool bBars = bConsole && PortHdUiSet(nBarSet) != NULL;
    for (const ctrl_t *pCtrl = pPanel->pChildren; pCtrl != NULL; pCtrl = pCtrl->pNext) {
      if ((pCtrl->dwFlags & 8) == 0)
        continue;
      const WORD wSlot = *reinterpret_cast<const WORD *>(&pCtrl->_unmapped24[0]);
      const bool bInfo = pCtrl->pfnDraw == reinterpret_cast<PFN_CTRL_DRAW>(statdata_icon_info_draw);
      if (bInfo || pCtrl->pfnDraw == reinterpret_cast<PFN_CTRL_DRAW>(statdata_btn_queue_draw)) {
        const statdata_icon_t *pIcon = *reinterpret_cast<statdata_icon_t *const *>(&pCtrl->_unmapped24[2]);
        if (pIcon == NULL || pIcon->pGrp != gpCmdIcons || n + 2 > nMax)
          continue;
        const bool bOwn = ModArtOwnFrame(MOD_ART_ICON, pIcon->wFrame);
        const bool bOwnHd = bOwn && bMod && PortModUiHas(MOD_ART_ICON, pIcon->wFrame);
        if (!bSlots && !bOwnHd)  // over the classic console, a mod's own icons alone
          continue;
        const int nFrame = wSlot + ((pCtrl->dwFlags & 0x40000000) != 0 ? 1 : 0);
        const int nPal = (pCtrl->dwFlags & 2) != 0 || pCtrl->wType != 2 ? 4 : 3;
        if (bInfo && bSlots)
          AddItem(pItems, nMax, &n, PORT_HD_UI_BUTTONS + nRace, nFrame, -1, pPanel, pCtrl, 0, 0, NULL, 0);
        if (bOwnHd)
          AddItem(pItems, nMax, &n, PORT_HD_UI_MOD, MOD_ART_ICON << 16 | pIcon->wFrame, 0, pPanel, pCtrl, 0, 0,
                  sgBmpTIcon_g + nPal * 16 + (bConsole ? 0 : 1), bConsole ? 16 : 1);
        else if (!bOwn)
          AddItem(pItems, nMax, &n, PORT_HD_UI_ICONS, pIcon->wFrame, -1, pPanel, pCtrl, 0, 0, sgBmpTIcon_g + nPal * 16,
                  16);
        if (!bInfo && bSlots)
          AddItem(pItems, nMax, &n, PORT_HD_UI_BUTTONS + nRace, nFrame, -1, pPanel, pCtrl, 0, 0, NULL, 0);
        // the pixels its text changed when 1.17 last wrote it (PortConsoleSlotTextEnd)
        const SlotText *pText = NULL;
        for (const SlotText &s : g_aSlotTexts)
          if (s.pCtrl == pCtrl && s.nW > 0)
            pText = &s;
        if (pCtrl->pData != NULL && pText != NULL && g_nHdTexts < static_cast<int>(SDL_arraysize(g_ahdTexts))) {
          const PortHdUiItem &box = pItems[n - 1];
          PortHdUiText &t = g_ahdTexts[g_nHdTexts++];
          t.nX = box.nX + pText->nX;
          t.nY = box.nY + pText->nY;
          t.nW = pText->nW;
          t.nH = pText->nH;
          memcpy(t.adwBits, pText->adwBits, sizeof t.adwBits);
        }
      } else if (bBars && pCtrl->pfnDraw == reinterpret_cast<PFN_CTRL_DRAW>(statdata_progressbar_draw) &&
                 n + 2 <= nMax) {
        AddItem(pItems, nMax, &n, nBarSet, 0, -1, pPanel, pCtrl, 0, 0, NULL, 0);
        AddItem(pItems, nMax, &n, nBarSet, 1, wSlot, pPanel, pCtrl, 0, 0, NULL, 0);
      }
    }
  }
  // the resource counter's icons over the map (B8), each where statres_resource_ctrl_draw puts
  // 1.17's: at the control's left, its middle across; the map shows through 1.17's in their
  // boxes (PortConsoleHdMapBoxes), as Remastered's are drawn over it. Its numbers stay 1.17's.
  const ctrl_t *pRes = sgpStatResDlg;
  if (bConsole && pRes != NULL && (pRes->dwFlags & 8) != 0 && PortHdUiSet(PORT_HD_UI_RESOURCES) != NULL) {
    for (const ctrl_t *pCtrl = pRes->pChildren; pCtrl != NULL; pCtrl = pCtrl->pNext) {
      if ((pCtrl->dwFlags & 8) == 0 || pCtrl->nIndex == 6 || n >= nMax ||
          g_nMapBoxes >= static_cast<int>(SDL_arraysize(g_aanMapBoxes)) ||
          pCtrl->pfnDraw != reinterpret_cast<PFN_CTRL_DRAW>(statres_resource_ctrl_draw))
        continue;
      const int nIcon = *reinterpret_cast<const WORD *>(&pCtrl->_unmapped24[0]) & 0xFF;
      if (nIcon >= 11)  // the supply bar's frame, not an icon
        continue;
      AddItem(pItems, nMax, &n, PORT_HD_UI_RESOURCES, nIcon, -1, pRes, pCtrl, 0, 0, NULL, 0);
      PortHdUiItem &item = pItems[n - 1];
      const int kIcon = 14;  // 1.17's icons are 14 by 14
      item.nY += (item.nH - kIcon) / 2;
      item.nW = item.nH = kIcon;
      int *pBox = g_aanMapBoxes[g_nMapBoxes++];
      pBox[0] = item.nX;
      pBox[1] = item.nY;
      pBox[2] = kIcon;
      pBox[3] = kIcon;
    }
  }
  // the selection's wireframe, each part in the colour statwire_draw_dlg's remap gives it
  const ctrl_t *pWire = sgpStatWireCtrl;
  const ctrl_t *pData = pWire != NULL ? pWire->pParent : NULL;
  CUnit *pUnit = pWire != NULL ? *reinterpret_cast<CUnit *const *>(&pWire->_unmapped24[2]) : NULL;
  if (pUnit != NULL && pData != NULL && (pWire->dwFlags & 8) != 0 && (pData->dwFlags & 8) != 0) {
    // the wireframe statwire.cpp and PortWireframeCell draw: a mod's unit's own, which Remastered's
    // HD wireframes do not have, or the type's it looks like
    const int nType = static_cast<int>(ModUnitArtFrame(MOD_ART_WIRE, pUnit->GetTypeEx()));
    sub_4A7F50(pUnit);
    sub_4A80C0(pUnit);
    sub_4A8030(pUnit);
    BYTE abSlots[6];
    const bool bOwn = ModArtOwnFrame(MOD_ART_WIRE, static_cast<unsigned>(nType));
    const bool bOwnHd = bOwn && bMod && PortModUiHas(MOD_ART_WIRE, static_cast<unsigned>(nType));
    const int nParts = !bOwn && WireShaded(nType) ? 0xD8 : 0xD0;
    for (int k = 0; k < 4; ++k)
      abSlots[k] = _gbRemapColorTbl[nParts + k];
    abSlots[4] = _gbRemapColorTbl[0xC0];
    abSlots[5] = _gbRemapColorTbl[0xC1];
    sub_4A7F10();
    for (int k = 0; k < 6 && !bOwn && bConsole; ++k)
      AddItem(pItems, nMax, &n, PORT_HD_UI_WIRES, nType, k, pData, pWire, 0, 0, &abSlots[k], 1);
    // a mod's own: its parts 1 to 6 in the same colours
    for (int k = 0; k < 6 && bOwnHd; ++k)
      AddItem(pItems, nMax, &n, PORT_HD_UI_MOD, MOD_ART_WIRE << 16 | nType, k + 1, pData, pWire, 0, 0, &abSlots[k],
              1);
  }
  return n;
}
