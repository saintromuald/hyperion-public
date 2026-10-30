// port_view.cpp -- the view: the size of the screen the game draws, in game pixels, and the
// sizes that follow from it; the render view the camera draws over (docs/modules/render.md,
// "The view and the camera"; docs/renderer.md, R3). 1.17 fixed the screen at 640x480 with the
// map in the 400 rows above its console; the places that used those numbers and the ones
// derived from them (the scroll ring's 21x14 megatiles, the dirty grid's 40x30 cells) read them
// here, so the view can change size between frames. The dirty grid, gbInvalMap at 0x6D06D0 in
// 1.17, lives here sized for the widest view, its rows g_portView.nGridWidth apart; every
// function that read 1.17's is C++, so none reads the old address. Wider than 640, the map is
// drawn the full 480 rows and 1.17's 640-wide layout (the console, the dialogs, the map's text)
// sits nConsoleX from the left; PortViewSet changes the view and PortViewApply switches a
// running game over between frames, moving what a game placed from it.
#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bmp.h"
#include "oc_data.h"       // OC_VA, where 1.17's data is
#include "img.h"
#include "dialog.h"
#include "event_Win.h"    // cur_update
#include "cellscrl.h"     // cell_paint
#include "dlg_dlgdraw.h"  // dlg_set_all_update
#include "gamemap.h"      // gamemap_force_redraw
#include "gds_mou_win.h"  // sub_4CE820, the cage
#include "gds_vidblit.h"  // BLTMASK, vid_mask_initialize
#include "mask.h"         // mask_build_grid
#include "minimap.h"      // cell_set_pos, minimap_force_redraw
#include "usel.h"         // usel_cancel_drag_evt
#include "statbtn.h"  // placebox_is_on
#include "port_camera.h"
#include "../loop/port_clock.h"  // PortClockScroll
#include "port_console.h"  // PortConsoleShift, PortConsoleUnsplit, PortConsoleHudOn
#include "../hud/port_hud.h"  // PortHudMapRaise
#include "../platform/port_paths.h"  // PortCommandOptions
#include "port_present.h"
#include "../tools/port_trace.h"  // PortTraceCamera
#include "../replays/port_tv.h"
#include "port_view.h"
#include "../gameplay/port_cutscene.h"  // PortCutsceneAim, PortCutsceneRunning
#include "port_scene.h"     // PortSceneStage, the render view's slots
#include "../platform/port_settings.h"
#include <math.h>
#include <vector>
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

// Game globals, by their 1.17 addresses (re/globals117.json).
// The ones whose layout changes at 64 bits are externs the build binds to
// their storage.
#define sgrScreenValid_g (*reinterpret_cast<RECT *>(OC_VA(0x50EF44u)))       // the screen, inclusive
#define sgrScreenSize_g (*reinterpret_cast<RECT *>(OC_VA(0x50EF54u)))        // the screen, exclusive
#define sgConsoleAreaRect_g (*reinterpret_cast<RECT *>(OC_VA(0x5033D0u)))    // the command card
extern DWORD dword_6D3160;  // 0x6D3160, the cursor is clipped
extern ctrl_t *sgpStatLBDlg;                // 0x6AB270, the leaderboard
#define sgMinimapArea_g (*reinterpret_cast<RECT *>(OC_VA(0x508BB0u)))        // the minimap's clicks
#define gGameMapRect (*reinterpret_cast<RECT *>(OC_VA(0x666430u)))           // the map on screen
extern BYTE gbSelectDragging;  // 0x6CA720
extern DWORD glMapPixelRgt;  // 0x543220, the scroll's limit across
// gwMapPixWidth and gwMapPixHeight as globals117.h binds them: on x64 a position of their own
// (h/pos.h), where 1.17's WORDs at 0x543230 and 0x543234 stay 0
extern BYTE sgbAutoScroll_g;  // 0x6A5ED4, an auto-scroll runs
extern DWORD sgAutoScrollX_g;  // 0x6A5EDC, and where it goes
extern DWORD sgAutoScrollY_g;  // 0x6A5EE0
#define gwScreenTileX_ (*reinterpret_cast<WORD *>(OC_VA(0x5264B0u)))  // the scroll in megatiles
#define gwScreenTileY_ (*reinterpret_cast<WORD *>(OC_VA(0x5264B2u)))

enum {
  VIEW_MIN_WIDTH = 640,
  VIEW_HEIGHT = 480,
  VIEW_STEP = 16      // a view's width is whole dirty-grid cells
};

static unsigned char g_abInvalMap[PORT_VIEW_GRID_MAX];

// 1.17's view until something asks for another; the sizes after nConsoleX are
// what ViewDerive gives for it.
PortView g_portView = {640, 480, 400, 400, 0, 21, 14, 672, 672 * 448, 40, 30, g_abInvalMap};

// The sizes that follow from the screen's and the map view's.
static void ViewDerive(PortView *pView)
{
  // A view that starts part of the way into a megatile (the scroll moves in
  // steps of 8) touches one more than it covers.
  pView->nRingTilesX = (pView->nWidth + 31) / 32 + 1;
  pView->nRingTilesY = (pView->nMapRows + 31) / 32 + 1;
  pView->nRingPitch = pView->nRingTilesX * 32;
  pView->nRingSize = pView->nRingPitch * pView->nRingTilesY * 32;
  pView->nGridWidth = (pView->nWidth + 15) / 16;
  pView->nGridHeight = (pView->nHeight + 15) / 16;
}

void PortViewClearGrid(void)
{
  memset(g_portView.pbInvalMap, 0, g_portView.nGridWidth * g_portView.nGridHeight);
}

// nWidth to the nearest multiple of 16, inside the widths a view can have.
static int ViewRoundWidth(int nWidth)
{
  nWidth = (nWidth + VIEW_STEP / 2) / VIEW_STEP * VIEW_STEP;
  if (nWidth < VIEW_MIN_WIDTH)
    return VIEW_MIN_WIDTH;
  if (nWidth > PORT_VIEW_MAX_WIDTH)
    return PORT_VIEW_MAX_WIDTH;
  return nWidth;
}

// With no console (a benchmark's run, PortViewFullRows) the map has every row at any width.
static bool g_bFullRows;

void PortViewSet(int nWidth)
{
  nWidth = ViewRoundWidth(nWidth);
  const int nMapRows = nWidth > VIEW_MIN_WIDTH || g_bFullRows ? VIEW_HEIGHT : 400;
  if (nWidth == g_portView.nWidth && nMapRows == g_portView.nMapRows)
    return;

  g_portView.nWidth = nWidth;
  g_portView.nHeight = VIEW_HEIGHT;
  g_portView.nMapRows = nMapRows;
  g_portView.nViewRows = 400;
  g_portView.nConsoleX = (nWidth - VIEW_MIN_WIDTH) / 2;
  ViewDerive(&g_portView);

  // What 1.17 set once, in its data section or at start-up, for 640x480.
  SetRect(&sgrScreenValid_g, 0, 0, nWidth - 1, VIEW_HEIGHT - 1);
  SetRect(&sgrScreenSize_g, 0, 0, nWidth, VIEW_HEIGHT);
  SetRect(&sgConsoleAreaRect_g, g_portView.nConsoleX + 496, 354, g_portView.nConsoleX + 639, 479);
  _img_draw_buf.nWidth = static_cast<short>(nWidth);
  _img_draw_buf.nHeight = static_cast<short>(VIEW_HEIGHT);
  _img_list[2].nWidth = static_cast<short>(nWidth);
  _img_list[2].nHeight = static_cast<short>(VIEW_HEIGHT);
  // The map's layer stays in use after a game, as in 1.17, under the menus'
  // dialogs; it keeps the view's size too (gamemap_reinit sets it in a game).
  if (_img_list[5].bInUse) {
    _img_list[5].nWidth = static_cast<short>(nWidth);
    _img_list[5].nHeight = static_cast<short>(g_portView.nMapRows);
  }

  // The cursor stays on the screen, and an uncaged cursor gets the whole of it.
  if (gCursorPos_g[0] > nWidth - 1)
    gCursorPos_g[0] = nWidth - 1;
  sub_4CE820(static_cast<int>(dword_6D3160));

  // The grid's rows are a new length, so all of it is stale: clear it and mark
  // the new screen for redraw. The present fits its texture on the next frame.
  memset(g_portView.pbInvalMap, 0, PORT_VIEW_GRID_MAX);
  memset(g_portView.pbInvalMap, 1, g_portView.nGridWidth * g_portView.nGridHeight);
}

// --view's width, or 0 without it.
static int ViewCommandWidth(void)
{
  const char *pszView = PortCommandOptions()->szView;
  const char *pszColon;

  if (pszView[0] == '\0')
    return 0;
  pszColon = strchr(pszView, ':');
  if (pszColon != NULL) {
    int nW = atoi(pszView);
    int nH = atoi(pszColon + 1);
    if (nW > 0 && nH > 0)
      return ViewRoundWidth(VIEW_HEIGHT * nW / nH);
    return VIEW_MIN_WIDTH;
  }
  return ViewRoundWidth(atoi(pszView));
}

// The display's shape as a width, or 16:9 when the display is no wider than 4:3.
static int ViewDisplayWidth(void)
{
  const int nWidth = ViewRoundWidth(PortDisplayViewWidth());
  return nWidth > VIEW_MIN_WIDTH ? nWidth : ViewRoundWidth(VIEW_HEIGHT * 16 / 9);
}

int PortViewShape(void)
{
  double d = PORT_VIEW_FIT;
  PortSettingNumber("hyperion", "view", &d);
  const int nShape = static_cast<int>(d);
  return nShape == PORT_VIEW_4_3 || nShape == PORT_VIEW_16_9 ? nShape : PORT_VIEW_FIT;
}

// A shape's width: 640, 16:9's 848, or the display's shape.
static int ViewShapeWidth(int nShape)
{
  if (nShape == PORT_VIEW_4_3)
    return VIEW_MIN_WIDTH;
  return nShape == PORT_VIEW_16_9 ? ViewRoundWidth(VIEW_HEIGHT * 16 / 9) : ViewDisplayWidth();
}

// The wide shape the view hotkey goes to: the last one chosen ("viewWide"), the display's
// unless it was 16:9.
static int ViewWideShape(void)
{
  double d = PORT_VIEW_FIT;
  PortSettingNumber("hyperion", "viewWide", &d);
  return static_cast<int>(d) == PORT_VIEW_16_9 ? PORT_VIEW_16_9 : PORT_VIEW_FIT;
}

// --view's width, or without it Settings' View shape.
static int ViewStartWidth(void)
{
  const int nWidth = ViewCommandWidth();
  if (nWidth > 0)
    return nWidth;
  return ViewShapeWidth(PortViewShape());
}

static int g_nGameWidth;  // the width games are played at, 0 until it is first asked for
static int g_nRequest;    // a width asked for and not yet applied, or 0

int PortViewGameWidth(void)
{
  if (g_nGameWidth == 0)
    g_nGameWidth = ViewStartWidth();
  return g_nGameWidth;
}

void PortViewChoose(int nShape)
{
  PortSettingSetNumber("hyperion", "view", nShape);
  if (nShape != PORT_VIEW_4_3)
    PortSettingSetNumber("hyperion", "viewWide", nShape);
  if (ViewCommandWidth() == 0)
    g_nGameWidth = ViewShapeWidth(nShape);
}

void PortViewRequest(int nWidth)
{
  g_nRequest = ViewRoundWidth(nWidth);
}

// The width the view hotkey goes to from 640: --view's when it is wider, else the wide
// shape last chosen.
static int ViewWideWidth(void)
{
  const int nWidth = ViewCommandWidth();
  return nWidth > VIEW_MIN_WIDTH ? nWidth : ViewShapeWidth(ViewWideShape());
}

// 640 or the wide width, whichever the game is not at; true when it goes wide.
static bool ViewToggle(void)
{
  const bool bWide = g_portView.nWidth <= VIEW_MIN_WIDTH;
  PortViewRequest(bWide ? ViewWideWidth() : VIEW_MIN_WIDTH);
  return bWide;
}

// The view hotkey's switch, kept as Settings' View shape.
void PortViewToggle(void)
{
  if (gwGameMode != 3)
    return;
  PortSettingSetNumber("hyperion", "view", ViewToggle() ? ViewWideShape() : PORT_VIEW_4_3);
}

int PortViewDialogShift(const ctrl_t *pDlg)
{
  if (pDlg != NULL && (pDlg == sgpStatResDlg || pDlg == sgpStatLBDlg))
    return PortViewDialogAnchor(pDlg) * g_portView.nConsoleX;
  return PortConsoleShift(pDlg);
}

int PortViewDialogAnchor(const ctrl_t *pDlg)
{
  if (pDlg == NULL)
    return 1;
  if (pDlg == sgpStatResDlg)
    return 2;
  if (pDlg == sgpStatLBDlg)
    return 0;
  return 1;
}

static int g_nAutoMidX;  // the middle the centre-view trigger's scroll aims for
static int g_nAutoMidY;

// act_center_view's target for the kept middle, with 1.17's clamp at the
// view's size. A map no wider or taller than the view would get a target below
// 0, which the scroll never reaches, so the trigger waiting on it would wait
// for good; 0 instead. At 640 that takes a map of 20 megatiles or fewer.
static void ViewCentreTarget(int *pnX, int *pnY)
{
  const int nViewW = g_portView.nWidth;
  const int nViewH = g_portView.nViewRows;
  const int nMapW = gwMapPixWidth;
  const int nMapH = gwMapPixHeight;
  int x = g_nAutoMidX - nViewW / 2;
  int y = g_nAutoMidY - nViewH / 2;

  if (x < 0)
    x = 0;
  else if (x + nViewW >= nMapW)
    x = nMapW - (nViewW + 1);
  if (y < 0)
    y = 0;
  else if (y + nViewH >= nMapH)
    y = nMapH - (nViewH + 1);
  *pnX = x > 0 ? x : 0;
  *pnY = y > 0 ? y : 0;
}

int PortViewDialogs(char *psz, int nMax)
{
  int n = 0;
  psz[0] = '\0';
  for (const ctrl_t *pDlg = gpDlgList; pDlg != NULL; pDlg = pDlg->pNext) {
    const char *pszName = pDlg == sgpMinimapDlg   ? "minimap"
                          : pDlg == sgpStatDataDlg ? "statdata"
                          : pDlg == sgpPortDlg     ? "statport"
                          : pDlg == sgpStatCmdDlg  ? "statcmd"
                          : pDlg == sgpTextBoxDlg  ? "textbox"
                          : pDlg == sgpStatResDlg  ? "statres"
                          : pDlg == sgpStatLBDlg   ? "statlb"
                                                   : "";
    char szText[32] = "";
    const char *pszText = static_cast<const char *>(pDlg->pData);
    for (int i = 0; pszText != NULL && i < 31 && pszText[i] >= ' ' && pszText[i] < 127 && pszText[i] != '"' &&
                    pszText[i] != '\\';
         ++i) {
      szText[i] = pszText[i];
      szText[i + 1] = '\0';
    }
    const int nWrote = _snprintf(psz + n, nMax - n, "%s{\"name\":\"%s\",\"text\":\"%s\",\"rect\":[%d,%d,%d,%d]}",
                                 n != 0 ? "," : "", pszName, szText, pDlg->rc.left, pDlg->rc.top, pDlg->rc.right,
                                 pDlg->rc.bottom);
    if (nWrote < 0 || nWrote >= nMax - n) {
      psz[n] = '\0';  // _snprintf leaves a cut entry unterminated
      break;
    }
    n += nWrote;
  }
  return n;
}

void PortViewCentreTarget(int nMidX, int nMidY, int *pnX, int *pnY)
{
  g_nAutoMidX = nMidX;
  g_nAutoMidY = nMidY;
  ViewCentreTarget(pnX, pnY);
}

// The side columns of a HUD design (port_view.h's PortViewShownWidth): the
// map is drawn beside them. The camera's sight starts past a left column; 1.17's scroll may go as
// far past the map's right edge as the columns are wide, rounded up to its step of 8, so the
// map's far side comes beside them too; 1.17's jumps and the camera's own aims put a place in
// the middle of what shows. The game's view stays the screen's width, so the HUD over it and the
// mouse are as before. A design with columns has no raise. In the screen's pixels, this pass's.
// Down, nothing covers the screen's foot beside a column: the camera puts the game view's
// middle at the screen's and keeps the screen's foot on the map's (PortViewCameraPlace), where
// 1.17's view of 400 rows would go 24 past the map's foot and the screen's last rows 80 more.
// 1.17's own limit down stays, so a centre-view trigger's scroll still ends where 1.17's does.
static double g_fSideL, g_fSideR;

static void CameraSides(const PortView &view);
static bool RenderZoomCan(void);

// The width of the game's view that shows, as a double: the camera's.
static double ViewShown(const PortView &view)
{
  return view.nWidth - g_fSideL - g_fSideR;
}

int PortViewShownWidth(void)
{
  return static_cast<int>(floor(ViewShown(g_portView) + 0.5));
}

static bool ViewSides(void)
{
  return g_fSideL + g_fSideR > 0.0;
}

// The rows of the game's view that show: the screen's map rows beside columns, 1.17's view's
// above a HUD across the foot or 1.17's console.
static double ViewShownRows(const PortView &view)
{
  return ViewSides() ? view.nMapRows : view.nViewRows;
}

// 1.17's limit across (cellscrl.cpp's, for the view as it is): past the map's right edge by the
// side columns.
static int ViewScrollRight(void)
{
  const int nSides = (static_cast<int>(ceil(g_fSideL + g_fSideR)) + 7) & ~7;
  return gwMapWidth * 32 - g_portView.nWidth + nSides;
}

// The limit made the columns' once a pass, and the view brought inside it when it shrank; with
// none, and none the pass before, 1.17's own as cellscrl.cpp and ViewSwitchGame set it.
static void ViewScrollRightApply(void)
{
  static bool s_bSides;
  const bool bSides = g_fSideL + g_fSideR > 0.0;
  if (!bSides && !s_bSides)
    return;
  s_bSides = bSides;
  const int nRight = ViewScrollRight();
  const DWORD dwRight = static_cast<DWORD>(nRight > 0 ? nRight : 0);
  if (glMapPixelRgt == dwRight)
    return;
  glMapPixelRgt = dwRight;
  if (static_cast<DWORD>(glScrollPixelX) > dwRight && !IsAutoScroll())
    cell_set_pos(glScrollPixelX, glScrollPixelY);
}

// The view becomes nWidth wide in a running game. PortViewSet sizes the view;
// the rest is what the game placed from the old one when it started.
static void ViewSwitchGame(int nWidth)
{
  const int nOldConsoleX = g_portView.nConsoleX;
  const LONG nOldCursorX = gCursorPos_g[0];
  int nDelta;
  ctrl_t *pDlg;
  int nRight;
  LONG nCursorX;

  // A band selection in progress is in the old screen's pixels.
  if (gbSelectDragging)
    usel_cancel_drag_evt();

  PortConsoleUnsplit();  // the console as 1.17's, then moved (port_console.cpp)
  PortViewSet(nWidth);
  nDelta = g_portView.nConsoleX - nOldConsoleX;

  // the pointer stays over the same place as the console and the map move under it; in a
  // window of the game's shape that is where the mouse already is
  nCursorX = nOldCursorX + nDelta;
  if (nCursorX < 0)
    nCursorX = 0;
  else if (nCursorX > g_portView.nWidth - 1)
    nCursorX = g_portView.nWidth - 1;
  mou_set_pos(static_cast<short>(nCursorX), static_cast<short>(gCursorPos_g[1]));
  cur_update();  // the cursor's layer to the pointer, which only a move or a new image does

  // the dialogs keep their places against the console, or the screen's edge for the two
  // anchored there; moving each moves the controls in it
  for (pDlg = gpDlgList; pDlg != NULL; pDlg = pDlg->pNext) {
    const int nShift = PortViewDialogAnchor(pDlg) * nDelta;
    pDlg->rc.left = static_cast<short>(pDlg->rc.left + nShift);
    pDlg->rc.right = static_cast<short>(pDlg->rc.right + nShift);
  }
  dlg_set_all_update();
  sgMinimapArea_g.left += nDelta;
  sgMinimapArea_g.right += nDelta;
  minimap_force_redraw();

  // the console's mask moved to its new place, not made again, since the art it came from is
  // freed once the console's dialogs have their pieces (status_init); the screen's mask from it
  if (sgConsoleArtMask_g != 0) {
    BLTMASK *pNode = reinterpret_cast<BLTMASK *>(sgConsoleArtMask_g);
    pNode->nLeft += nDelta;
    pNode->nRight += nDelta;
    vid_mask_initialize();
  }

  // the map: its rectangle, the scroll's limit across and the view's middle kept; the ring's rows
  // are a new length, so all of it is painted again from black, and the fog's grids rebuilt
  SetRect(&gGameMapRect, 0, 0, g_portView.nWidth - 1, g_portView.nMapRows - 1);
  nRight = ViewScrollRight();
  glMapPixelRgt = static_cast<DWORD>(nRight > 0 ? nRight : 0);
  memset(_gfpScrollBuf, 0, PORT_VIEW_RING_MAX);
  cell_set_pos(glScrollPixelX - nDelta, glScrollPixelY);

  // a centre-view trigger's scroll was aimed from the old width: aimed again it still ends, where
  // an old target past the new scroll limit is never reached and the trigger waits for good
  if (sgbAutoScroll_g) {
    int x;
    int y;
    ViewCentreTarget(&x, &y);
    sgAutoScrollX_g = static_cast<DWORD>(x & ~7);
    sgAutoScrollY_g = static_cast<DWORD>(y & ~7);
  }

  cell_paint();
  mask_build_grid();
  gamemap_force_redraw();
}

void PortViewFullRows(bool bFull, bool bInGame)
{
  if (bFull == g_bFullRows)
    return;
  g_bFullRows = bFull;
  if (g_portView.nWidth == 0)
    return;
  if (bInGame) {
    ViewSwitchGame(g_portView.nWidth);
    return;
  }
  // between games: the next game's map takes its rows from the view (gamemap.cpp)
  g_portView.nMapRows = g_portView.nWidth > VIEW_MIN_WIDTH || g_bFullRows ? VIEW_HEIGHT : 400;
  ViewDerive(&g_portView);
}

void PortViewApply(void)
{
  const int nWidth = g_nRequest;

  if (nWidth == 0 || gwGameMode != 3)
    return;
  g_nRequest = 0;
  g_nGameWidth = nWidth;
  if (nWidth != g_portView.nWidth)
    ViewSwitchGame(nWidth);
}

// The replay harness's switch (rt_observer.cpp, view_every): F5's, applied
// between frames the same way.
extern "C" void __stdcall oc_view_toggle(void)
{
  if (gwGameMode == 3)
    ViewToggle();  // not kept: the harness's own switch
}

// ---------------------------------------------------------------------------
// The render view (port_view.h). PortViewRenderBegin, at gamemap_update's start, sets
// the view, the scroll and the screen buffer to a view around the camera's sight, and
// PortViewRenderEnd, before the map's text, sets them back; between the two, all of 1.17's
// drawing of the map (the terrain from the cell map, the sprites from the rows the view
// reaches, the fog, the stars) is reported to the GPU scene in the render view's place.

PortRenderView g_portRender;

struct RenderSaved {
  PortView view;
  LONG nScrollX, nScrollY;
  WORD wTileX, wTileY;
  short nBufWidth, nBufHeight;
  BYTE *pPixels;
  BYTE *pRing;  // the game's scroll ring, while the software's render view has its own
  RECT rcValid, rcSize;
};

enum {
  RENDER_MAX = 8192,       // the render view's largest side, in game pixels, where the software draws it
  RENDER_MAX_GPU = 32752,  // and where the GPU does, its layer at a coarser step past 8,192:
                           // under the screen buffer's 16-bit sizes, whole cells of the dirty grid
  RENDER_STEP = 16         // its size is whole cells of the dirty grid
};

static bool g_bRendering;      // between PortViewRenderBegin and PortViewRenderEnd
static bool g_bRenderedLast;   // the last pass was drawn over the render view
static RenderSaved g_saved;
static std::vector<unsigned char> g_renderGrid;
static BYTE *g_pRenderPixels;  // the buffer the software is given, which it draws nothing into
static size_t g_nRenderPixels;
// SDL's renderer (a browser's, or where no GPU driver starts) has no GPU scene: the software
// draws the render view itself, into that buffer and a scroll ring of its own, and the
// presenter shows it at the camera's scale under what the screen draws over the map
// (PortViewSoftShown). The game's buffer takes it sampled to the screen's pixels, which is
// what a screenshot or a streamed seat shows, and kept (g_softRef) tells the screen's own
// pixels over the map from the map's (PortViewSoftCover).
enum { SOFT_ZOOM_MOST = 4 };  // the software draws the whole render view each pass
static bool g_bSoftPass;      // this pass is the software's render view
static bool g_bSoftShown;     // the last pass was, and the presenter shows it
static unsigned int g_nSoftPasses;
static BYTE *g_pRenderRing;
static size_t g_nRenderRing;
static std::vector<BYTE> g_softRef;

static int RenderFloor8(double f) { return static_cast<int>(floor(f / 8.0)) * 8; }
static int RenderRoundUp(int n) { return (n + RENDER_STEP - 1) / RENDER_STEP * RENDER_STEP; }

// Where the camera looks and the render view round it, from the game's view and the zoom:
// the game view's middle above the console stays where it is on the screen, as 1.17's
// jumps put a place there, and the sight stays inside the map, a map smaller than it in
// the middle.
void PortViewCameraPlace(double fZoom, double fScrollX, double fScrollY, double *pfX, double *pfY)
{
  const PortView &view = g_bRendering ? g_saved.view : g_portView;
  const int nViewRows = view.nViewRows;
  const double fMapW = gwMapWidth * 32.0;
  const double fMapH = gwMapHeight * 32.0;
  // across, the part that shows beside the HUD's side columns: its middle on the map stays at
  // its middle on the screen, and at zoom 1 its left is the scroll's; down, beside columns the
  // game view's middle is the screen's
  const double fShown = ViewShown(view);
  const double fRows = ViewShownRows(view);
  double x = fScrollX + fShown * 0.5 - (g_fSideL + fShown * 0.5) * fZoom;
  double y = fScrollY + nViewRows * 0.5 - fRows * 0.5 * fZoom;
  const double fSeenW = fShown * fZoom;
  const double fSeenH = fRows * fZoom;
  // 1.17 lets the view's map rows end 24 pixels past the map's foot, which a HUD across the
  // foot or 1.17's console covers; beside columns the screen's foot is the map's
  const double fFoot = ViewSides() ? fMapH : fMapH + 24.0;
  if (fZoom != 1.0 || ViewSides()) {
    double xs = x + g_fSideL * fZoom;
    if (fSeenW >= fMapW)
      xs = (fMapW - fSeenW) * 0.5;
    else
      xs = xs < 0.0 ? 0.0 : xs + fSeenW > fMapW ? fMapW - fSeenW : xs;
    x = xs - g_fSideL * fZoom;
    if (fSeenH >= fFoot)
      y = (fFoot - fSeenH) * 0.5;
    else
      y = y < 0.0 ? 0.0 : y + fSeenH > fFoot ? fFoot - fSeenH : y;
  }
  *pfX = x;
  *pfY = y;
}

// The camera's centre (Z5): the map pixel at the game view's middle above the console, as
// the game's view has it unless the port moved the camera itself, and where it rests from
// the game view's middle (an offset, under 8 pixels in a game, any in a replay).
static bool g_bCamOn;
static double g_fCamX, g_fCamY;
static double g_fCamOffX, g_fCamOffY;
static double g_fCamLastX, g_fCamLastY;  // the game view's middle when last seen
static LARGE_INTEGER g_camLast;
// The wheel's zoom about the cursor: the screen point and the map point held under it.
static bool g_bCamAnchor;
static bool g_bCamMayWrite;  // only at a pass's start: the game's view then follows the camera
// The camera as the pass placed it (PortViewRenderBegin): the map's placement, its lights and
// the cursor's lookups in that pass read this, so they agree; placed again at each pass's
// start, the easing moves once a pass.
static bool g_bCamCached;
static double g_fCamCacheX, g_fCamCacheY, g_fCamCacheZ;
static int g_nCamCacheMoving;
// The minimap moved the game's view since the last pass (PortViewMinimapPress, Moved): 1 a
// press, 2 a drag. A press puts the camera there at once and a drag takes its tiles at once,
// as 1.17 does; with the setting "minimapSmoothDrag" the camera follows a drag with a short
// ease instead (the user's choices, 2026-10-15).
static int g_nMinimapMove;
static bool g_bMinimapPressNext;
static bool g_bMinimapEase;
static double g_fAnchorSX, g_fAnchorSY, g_fAnchorMX, g_fAnchorMY;
// The map raised over the HUD (Settings' HUD tab; "Raise the map"): with a HUD
// design's console the camera's sight is drawn that many of the screen's rows higher, so units
// along the screen's foot come out from under a HUD tall at its sides; as many rows of the
// sight's top go off the screen, and the minimap's box loses them (PortViewSeenRect). The game's
// view stays 1.17's and the camera sits below it by the raise, which gives way over the map's top
// half a view, so the map's top rows still come into sight. g_fCamRaise is the last pass's, in the
// screen's rows.
static double g_fCamRaise;
static bool RenderZoomCan(void);

static double CameraRaiseRows(const PortView &view)
{
  if (gwGameMode != 3 || !PortConsoleHudOn() || !RenderZoomCan() || view.nHeight <= 0 || g_fSideL > 0.0 ||
      g_fSideR > 0.0)
    return 0.0;
  return PortHudMapRaise() * view.nHeight / 1080.0;
}

// This pass's side columns, from the HUD as last laid out.
static void CameraSides(const PortView &view)
{
  g_fSideL = g_fSideR = 0.0;
  if (gwGameMode != 3 || !PortConsoleHudOn() || !RenderZoomCan() || view.nHeight <= 0)
    return;
  double l, r;
  PortHudSides(&l, &r);
  g_fSideL = l * view.nHeight / 1080.0;
  g_fSideR = r * view.nHeight / 1080.0;
}
// The scroll's glide (Settings, "Frame rate" past 1.17's): scroll_update stamps each step
// with its time (PortViewScrollStep), and the camera is placed where the game's view was a
// step's length ago, between the stamped steps, so a scroll moves at an even speed at any
// frame rate, a step behind. The game's view, and the camera's own state, keep 1.17's steps.
struct GlideStep {
  double fMs;    // when, on the performance counter, in ms
  double x, y;   // the game view's scroll position after it
};
static GlideStep g_glide[16];
static int g_nGlide;              // the steps kept, oldest first
static double g_fGlideMs = 15.6;  // the time between steps, averaged

static double GlideNowMs(void)
{
  LARGE_INTEGER now, freq;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&freq);
  return static_cast<double>(now.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
}

void PortViewScrollStep(LONG nFromX, LONG nFromY)
{
  PortClockScroll();
  const double fNow = GlideNowMs();
  // a step long after the last starts a scroll: the position before it, a step's length back
  if (g_nGlide == 0 || fNow - g_glide[g_nGlide - 1].fMs > 60.0) {
    g_nGlide = 0;
    g_fGlideMs = 15.6;
    g_glide[g_nGlide++] = {fNow - g_fGlideMs, static_cast<double>(nFromX), static_cast<double>(nFromY)};
  } else {
    const double fBetween = fNow - g_glide[g_nGlide - 1].fMs;
    if (fBetween >= 4.0 && fBetween <= 40.0)
      g_fGlideMs = g_fGlideMs * 0.8 + fBetween * 0.2;
  }
  if (g_nGlide == 16) {
    memmove(&g_glide[0], &g_glide[1], 15 * sizeof g_glide[0]);
    --g_nGlide;
  }
  g_glide[g_nGlide++] = {fNow, static_cast<double>(glScrollPixelX), static_cast<double>(glScrollPixelY)};
}

// How far the game's view was, a step's length ago, short of where it is now: the lag to place
// the camera by; 0 once the steps are past.
static void GlideLag(double *pfX, double *pfY)
{
  *pfX = *pfY = 0.0;
  if (g_nGlide < 2)
    return;
  const double fAt = GlideNowMs() - g_fGlideMs;
  const GlideStep &last = g_glide[g_nGlide - 1];
  if (fAt >= last.fMs) {
    g_nGlide = 0;
    return;
  }
  int i = g_nGlide - 1;
  while (i > 0 && g_glide[i - 1].fMs > fAt)
    --i;
  const GlideStep &a = g_glide[i > 0 ? i - 1 : 0];
  const GlideStep &b = g_glide[i];
  const double f = b.fMs > a.fMs ? (fAt - a.fMs) / (b.fMs - a.fMs) : 1.0;
  const double t = f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f;
  *pfX = last.x - (a.x + (b.x - a.x) * t);
  *pfY = last.y - (a.y + (b.y - a.y) * t);
}

static void CameraGameMiddle(double *pfX, double *pfY)
{
  const PortView &view = g_bRendering ? g_saved.view : g_portView;
  const LONG nScrollX = g_bRendering ? g_saved.nScrollX : glScrollPixelX;
  const LONG nScrollY = g_bRendering ? g_saved.nScrollY : glScrollPixelY;
  *pfX = nScrollX + ViewShown(view) * 0.5;
  *pfY = nScrollY + view.nViewRows * 0.5;
}

static int CameraUpdate(double *pfX, double *pfY, double *pfZoom)
{
  // a game begins (the frame counter starting again tells one that follows another with no
  // menu drawn between): the zoom last chosen for its kind
  static unsigned int s_nFrame;
  if (gwGameMode == 3 && oc_dwReplayFrame < s_nFrame)
    g_bCamOn = false;
  s_nFrame = gwGameMode == 3 ? oc_dwReplayFrame : 0;
  if (!g_bCamOn && gwGameMode == 3)
    PortCameraGameStarts(gPlayback != 0);
  CameraSides(g_bRendering ? g_saved.view : g_portView);
  if (!g_bRendering && g_bCamMayWrite)
    ViewScrollRightApply();
  const double z = PortCameraShown();
  LARGE_INTEGER now, freq;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&freq);
  double dt = g_camLast.QuadPart != 0 ? static_cast<double>(now.QuadPart - g_camLast.QuadPart) / freq.QuadPart : 0.0;
  g_camLast = now;
  if (dt > 0.1)
    dt = 0.1;
  double gx, gy;
  CameraGameMiddle(&gx, &gy);
  bool bMoving = false;
  bool bGlide = false;  // a scroll step this pass, to be glided
  // for Debug Tools' camera trace (port_trace.cpp): what the camera made of this pass
  const char *pszMode = "rest";
  const double fTraceDx = g_bCamOn ? gx - g_fCamLastX : 0.0, fTraceDy = g_bCamOn ? gy - g_fCamLastY : 0.0;
  double fTraceK = 0.0;
  if (!g_bCamOn || gwGameMode != 3) {
    pszMode = "new";
    // a new game, or none: the game's view
    g_bCamOn = gwGameMode == 3;
    g_fCamX = gx;
    g_fCamY = gy;
    g_fCamOffX = g_fCamOffY = 0.0;
    g_bCamAnchor = false;
  } else {
    // the game's view scrolled: by as much; jumped: the camera eases there
    const double dx = gx - g_fCamLastX;
    const double dy = gy - g_fCamLastY;
    const double fJump = 256.0 * (z > 1.0 ? z : 1.0);
    const int nMinimap = g_nMinimapMove;
    g_nMinimapMove = 0;
    if (nMinimap != 0 && (dx != 0.0 || dy != 0.0)) {
      // the minimap moved the view: the camera goes with it, at once or, for a drag with the
      // setting on, by a short ease
      g_fCamOffX = g_fCamOffY = 0.0;
      g_bCamAnchor = false;
      PortFollowStop();
      PortTvInput();
      double fSmooth = 0.0;
      PortSettingNumber("hyperion", "minimapSmoothDrag", &fSmooth);
      g_bMinimapEase = nMinimap == 2 && fSmooth != 0.0 && PortCameraSmooth();
      if (!g_bMinimapEase) {
        g_fCamX = gx;
        g_fCamY = gy;
      }
      pszMode = nMinimap == 1 ? "minimap-press" : "minimap-drag";
    } else if (fabs(dx) > fJump || fabs(dy) > fJump) {
      g_bMinimapEase = false;
      pszMode = "jump";
      g_fCamOffX = g_fCamOffY = 0.0;
      g_bCamAnchor = false;
      PortFollowStop();
      PortTvInput();
    } else {
      g_fCamX += dx;
      g_fCamY += dy;
      if (dx != 0.0 || dy != 0.0) {
        bGlide = true;
        pszMode = "scroll";
        // the viewer scrolled (the camera's own moves of the game's view are not seen here)
        PortFollowStop();
        PortTvInput();
        // In a replay a shot of Replay TV's, or a unit followed, leaves the camera away from the
        // game's view, which nothing there moves to it: the viewer's scroll takes the game's view
        // to the camera first, so the two scroll together, the minimap's box shows the view, and
        // the map's edge stops the view where it is and not short of it.
        if (gPlayback && (fabs(g_fCamOffX) > 8.0 || fabs(g_fCamOffY) > 8.0) && g_bCamMayWrite && !g_bRendering &&
            !IsAutoScroll()) {
          const PortView &view = g_portView;
          cell_set_pos(static_cast<int>(floor(g_fCamX - ViewShown(view) * 0.5 + 0.5)),
                       static_cast<int>(floor(g_fCamY - view.nViewRows * 0.5 + 0.5)));
          CameraGameMiddle(&gx, &gy);
          g_fCamOffX = g_fCamX - gx;
          g_fCamOffY = g_fCamY - gy;
        }
      }
    }
    // a cutscene, following a unit, or replay TV: their aim, as an offset from the game's view;
    // a cutscene's is eased already, so the camera is put on it
    double fAimX, fAimY, fAimZoom;
    bool bAim = false, bDirect = false;
    PortCameraSlow(0);
    if (PortCutsceneAim(&fAimX, &fAimY)) {
      bAim = bDirect = true;
      g_bCamAnchor = false;
    } else if (IsAutoScroll() && (z != 1.0 || PortCameraZoomTarget() != 1.0)) {
      // a centre-view trigger's scroll, zoomed: the place it names in the middle, where the
      // camera's own offset from the game's view would leave it to one side
      fAimX = g_nAutoMidX;
      fAimY = g_nAutoMidY;
      bAim = true;
      g_bCamAnchor = false;
    } else if (!g_bCamAnchor && PortFollowAt(&fAimX, &fAimY)) {
      bAim = true;
    } else if (!g_bCamAnchor && PortTvTarget(&fAimX, &fAimY, &fAimZoom)) {
      bAim = true;
      PortCameraSlow(1);
      if (fabs(PortCameraZoomTarget() - fAimZoom) > 0.05)
        PortCameraZoomTo(fAimZoom);
    }
    if (bAim) {
      pszMode = "aim";
      g_fCamOffX = fAimX - gx;
      g_fCamOffY = fAimY - gy;
    }
    if (g_bCamAnchor) {
      // the map point under the cursor stays there while the zoom eases, the raise with it
      const PortView &view = g_bRendering ? g_saved.view : g_portView;
      g_fCamX = g_fAnchorMX - (g_fAnchorSX - g_fSideL - ViewShown(view) * 0.5) * z;
      g_fCamY = g_fAnchorMY - (g_fAnchorSY + g_fCamRaise - ViewShownRows(view) * 0.5) * z;
      if (PortCameraZoom() == PortCameraZoomTarget())
        g_bCamAnchor = false;
      bMoving = true;
      pszMode = "anchor";
      if (!gPlayback && g_bCamMayWrite && !g_bRendering && !IsAutoScroll()) {
        // in a game, the game's view follows, to 8 pixels
        cell_set_pos(static_cast<int>(floor(g_fCamX - ViewShown(view) * 0.5 + 0.5)),
                     static_cast<int>(floor(g_fCamY - view.nViewRows * 0.5 + 0.5)));
        CameraGameMiddle(&gx, &gy);
      }
      g_fCamOffX = g_fCamX - gx;
      g_fCamOffY = g_fCamY - gy;
    } else {
      // back at the game's own view in a game, nothing kept from it
      if (!gPlayback && !bAim && z == 1.0 && PortCameraZoomTarget() == 1.0)
        g_fCamOffX = g_fCamOffY = 0.0;
      const double tx = gx + g_fCamOffX;
      const double ty = gy + g_fCamOffY;
      if (fabs(tx - g_fCamX) > 0.25 || fabs(ty - g_fCamY) > 0.25) {
        const double fTau = PortCameraIsSlow() ? 0.6 : g_bMinimapEase ? 0.05 : 0.08;
        const double k = bDirect || !PortCameraSmooth() ? 1.0 : 1.0 - exp(-dt / fTau);
        g_fCamX += (tx - g_fCamX) * k;
        g_fCamY += (ty - g_fCamY) * k;
        bMoving = true;
        fTraceK = k;
        if (strcmp(pszMode, "rest") == 0 || strcmp(pszMode, "scroll") == 0)
          pszMode = "ease";
      } else {
        g_fCamX = tx;
        g_fCamY = ty;
        g_bMinimapEase = false;
      }
      if (bAim && !gPlayback && g_bCamMayWrite && !g_bRendering && !IsAutoScroll()) {
        // following in a game: the game's view follows the camera, to 8 pixels
        const PortView &view = g_portView;
        cell_set_pos(static_cast<int>(floor(g_fCamX - ViewShown(view) * 0.5 + 0.5)),
                     static_cast<int>(floor(g_fCamY - view.nViewRows * 0.5 + 0.5)));
        CameraGameMiddle(&gx, &gy);
        g_fCamOffX = g_fCamX - gx;
        g_fCamOffY = g_fCamY - gy;
      }
    }
  }
  g_fCamLastX = gx;
  g_fCamLastY = gy;
  // the glide, while a scroll's steps are being shown; anything but a scroll ends it
  double fLagX = 0.0, fLagY = 0.0;
  if (!bGlide && strcmp(pszMode, "rest") != 0 && strcmp(pszMode, "ease") != 0)
    g_nGlide = 0;
  else if (PortFrameRate() != 0 && PortCameraSmooth())
    GlideLag(&fLagX, &fLagY);
  if (fLagX != 0.0 || fLagY != 0.0)
    bMoving = true;
  const PortView &view = g_bRendering ? g_saved.view : g_portView;
  PortViewCameraPlace(z, g_fCamX - fLagX - ViewShown(view) * 0.5, g_fCamY - fLagY - view.nViewRows * 0.5, pfX, pfY);
  // the raise, given way over the map's top half a view
  const double fRaise = CameraRaiseRows(view);
  const double fBand = view.nViewRows * 0.5 * z;
  g_fCamRaise = fRaise > 0.0 && fBand > 0.0 ? fRaise * (*pfY <= 0.0 ? 0.0 : *pfY >= fBand ? 1.0 : *pfY / fBand) : 0.0;
  *pfY += g_fCamRaise * z;
  *pfZoom = z;
  if (!g_bRendering)
    PortTraceCamera(gx, gy, fTraceDx, fTraceDy, pszMode, g_fCamX - fLagX, g_fCamY - fLagY, g_fCamOffX, g_fCamOffY, z,
                    PortCameraZoomTarget(), fTraceK);
  g_bCamCached = g_bCamOn;
  g_fCamCacheX = *pfX;
  g_fCamCacheY = *pfY;
  g_fCamCacheZ = z;
  g_nCamCacheMoving = bMoving ? 1 : 0;
  return bMoving ? 1 : 0;
}

int PortViewCamera(double *pfX, double *pfY, double *pfZoom)
{
  if (!g_bCamCached || gwGameMode != 3)
    return CameraUpdate(pfX, pfY, pfZoom);
  *pfX = g_fCamCacheX;
  *pfY = g_fCamCacheY;
  *pfZoom = g_fCamCacheZ;
  return g_nCamCacheMoving;
}

void PortViewMinimapPress(void)
{
  g_bMinimapPressNext = true;
}

void PortViewMinimapMoved(void)
{
  g_nMinimapMove = g_bMinimapPressNext || g_nMinimapMove == 1 ? 1 : 2;
  g_bMinimapPressNext = false;
}

void PortViewShape(double *pfWidth, double *pfRows)
{
  const PortView &view = g_bRendering ? g_saved.view : g_portView;
  *pfWidth = view.nWidth;
  *pfRows = view.nViewRows;
}

void PortViewZoomKey(int nNotches)
{
  g_bCamAnchor = false;
  PortTvInput();
  PortCameraWheel(nNotches);
  gamemap_force_redraw();
}

void PortViewZoomReset(void)
{
  g_bCamAnchor = false;
  PortTvInput();
  PortCameraZoomTo(1.0);
  PortCameraRemember();
  gamemap_force_redraw();
}

void PortViewZoomFit(void)
{
  g_bCamAnchor = false;
  PortTvInput();
  PortCameraZoomTo(PortCameraZoomMax());
  PortCameraRemember();
  if (gPlayback)
    PortViewCameraCentre(gwMapWidth * 16.0, gwMapHeight * 16.0 + 12.0);
  gamemap_force_redraw();
}

void PortViewCameraCentre(double fX, double fY)
{
  if (gwGameMode != 3)
    return;
  g_bCamAnchor = false;
  PortFollowStop();
  if (!gPlayback) {
    cell_set_pos(static_cast<int>(floor(fX - ViewShown(g_portView) * 0.5 + 0.5)),
                 static_cast<int>(floor(fY - g_portView.nViewRows * 0.5 + 0.5)));
  }
  double gx, gy;
  CameraGameMiddle(&gx, &gy);
  g_fCamOffX = fX - gx;
  g_fCamOffY = fY - gy;
  g_fCamLastX = gx;
  g_fCamLastY = gy;
  gamemap_force_redraw();
}

void PortViewCameraMiddle(double *pfX, double *pfY)
{
  if (g_bCamOn) {
    *pfX = g_fCamX;
    *pfY = g_fCamY;
  } else {
    CameraGameMiddle(pfX, pfY);
  }
}

// Whether the camera shows other than the game's own view: zoomed, or moved off it.
static bool CameraOff(void)
{
  return g_portRender.bOn != 0;
}

void PortViewScreenToMap(int nX, int nY, int *pnMapX, int *pnMapY)
{
  if (!CameraOff()) {
    *pnMapX = nX + glScrollPixelX;
    *pnMapY = nY + glScrollPixelY;
    return;
  }
  double fCamX, fCamY, z;
  PortViewCamera(&fCamX, &fCamY, &z);
  *pnMapX = static_cast<int>(floor(fCamX + (nX + 0.5) * z));
  *pnMapY = static_cast<int>(floor(fCamY + (nY + 0.5) * z));
}

void PortViewSeenRect(int *pnLeft, int *pnTop, int *pnRight, int *pnBottom)
{
  if (!CameraOff()) {
    // the camera on the game's view: no column at the left, maybe one at the right
    *pnLeft = glScrollPixelX;
    *pnTop = glScrollPixelY;
    *pnRight = glScrollPixelX + g_portView.nWidth - static_cast<int>(floor(g_fSideR + 0.5));
    *pnBottom = glScrollPixelY + static_cast<int>(ViewShownRows(g_portView));
    return;
  }
  double fCamX, fCamY, z;
  PortViewCamera(&fCamX, &fCamY, &z);
  // beside the HUD's side columns, what shows between them
  *pnLeft = static_cast<int>(floor(fCamX + g_fSideL * z));
  *pnTop = static_cast<int>(floor(fCamY));
  *pnRight = static_cast<int>(ceil(fCamX + (g_portView.nWidth - g_fSideR) * z));
  // raised, the sight's top rows are off the screen and its foot where it was; beside columns,
  // the screen's rows
  *pnBottom = static_cast<int>(ceil(fCamY + (ViewShownRows(g_portView) - g_fCamRaise) * z));
}

static bool RenderZoomCan(void);
static bool RenderSoft(void);

// The render view's largest side: the GPU's draws a map of 1,024 tiles whole.
static int RenderMost(void)
{
  return RenderZoomCan() && !RenderSoft() ? RENDER_MAX_GPU : RENDER_MAX;
}

double PortViewZoomWhole(void)
{
  // a cutscene holds the player's input, so its camera may go as far out as a replay's
  if ((!gPlayback && !PortCutsceneRunning()) || gwGameMode != 3 || g_portView.nWidth <= 0 || g_portView.nViewRows <= 0)
    return 0.0;
  // the map across and its rows with 1.17's 24 below, each no more than the render view takes;
  // beside columns, the map in what shows
  const double fMapW = gwMapWidth * 32.0;
  const double fMapH = gwMapHeight * 32.0 + (ViewSides() ? 0.0 : 24.0);
  double fAcross = fMapW / ViewShown(g_portView);
  double fDown = fMapH / ViewShownRows(g_portView);
  const int nMost = RenderMost();
  if (fMapW > nMost)
    fAcross = static_cast<double>(nMost) / g_portView.nWidth;
  if (fMapH > nMost)
    fDown = static_cast<double>(nMost) / g_portView.nMapRows;
  return fAcross > fDown ? fAcross : fDown;
}

static void RenderPlace(double fZoom, double x, double y)
{
  const int nW = g_portView.nWidth;
  const int nRows = g_portView.nMapRows;
  g_portRender.fCamX = x;
  g_portRender.fCamY = y;
  g_portRender.fZoom = fZoom;
  g_portRender.nGameX = glScrollPixelX;
  g_portRender.nGameY = glScrollPixelY;

  // the render view: the sight rounded out, inside the map's top left; zoomed, with a margin
  // for the camera's moving before the next pass (the presents take it as it is then)
  const double fMargin = fZoom != 1.0 ? 32.0 * fZoom : 0.0;
  int nX = RenderFloor8(x - fMargin);
  int nY = RenderFloor8(y - fMargin);
  if (nX < 0)
    nX = 0;
  if (nY < 0)
    nY = 0;
  const int nRight = static_cast<int>(ceil(x + nW * fZoom + fMargin));
  const int nBottom = static_cast<int>(ceil(y + nRows * fZoom + fMargin));
  int nWidth = RenderRoundUp(nRight - nX);
  int nHeight = RenderRoundUp(nBottom - nY);
  // none of it past the map, but for 1.17's 24 rows below it; the game's view at least
  const int nMapRight = RenderRoundUp(gwMapWidth * 32 - nX);
  const int nMapBottom = RenderRoundUp(gwMapHeight * 32 + 24 - nY);
  if (nWidth > nMapRight)
    nWidth = nMapRight;
  if (nHeight > nMapBottom)
    nHeight = nMapBottom;
  if (nWidth < nW)
    nWidth = nW;
  if (nHeight < nRows)
    nHeight = nRows;
  // the screen buffer's rows are the map's and the console's below, 16 bits in all
  const int nMost = RenderMost();
  const int nMostRows = (0x7FFF - (g_portView.nHeight - g_portView.nMapRows)) & ~(RENDER_STEP - 1);
  if (nWidth > nMost)
    nWidth = nMost;
  if (nHeight > nMost)
    nHeight = nMost;
  if (nHeight > nMostRows)
    nHeight = nMostRows;
  g_portRender.nX = nX;
  g_portRender.nY = nY;
  g_portRender.nWidth = nWidth;
  g_portRender.nRows = nHeight;
}

static bool g_abLayerDrawn[8];  // PortViewRenderLayers drew it in this pass's render view

void PortViewRenderLayers(void)
{
  if (!g_bRendering || !g_portRender.bOn)
    return;
  // the game view's place to the render view's
  const int nDx = g_saved.nScrollX - g_portRender.nX;
  const int nDy = g_saved.nScrollY - g_portRender.nY;
  for (int k = 4; k >= 3; --k) {
    img_t *pImg = &_img_list[k];
    if (!pImg->bInUse || pImg->pfnUpdate == NULL)
      continue;
    bmpsrc_t src;
    src.rc.left = static_cast<short>(-(pImg->nX + nDx));
    src.rc.top = static_cast<short>(-(pImg->nY + nDy));
    src.rc.right = static_cast<short>(src.rc.left + g_portView.nWidth - 1);
    src.rc.bottom = static_cast<short>(src.rc.top + g_portView.nHeight - 1);
    src.nWidth = static_cast<unsigned short>(g_portView.nWidth);
    src.nHeight = static_cast<unsigned short>(g_portView.nHeight);
    pImg->pfnUpdate(0, 0, pImg->pBmp, &src);
    g_abLayerDrawn[k] = true;
  }
}

int PortViewLayerDrawn(int nLayer)
{
  if (nLayer < 0 || nLayer >= 8 || !g_abLayerDrawn[nLayer])
    return 0;
  g_abLayerDrawn[nLayer] = false;
  return 1;
}

int PortViewRendering(void)
{
  return g_bRendering ? 1 : 0;
}

// SDL's renderer, the software drawing the map (port_present.cpp)
static bool RenderSoft(void)
{
  return PortPresentGpu() == 0 && PortSceneGpuOnly() == 0;
}

// The camera can zoom: the GPU alone draws the map and has the zoom's pipelines, or the
// software draws the render view for SDL's renderer.
static bool RenderZoomCan(void)
{
  return (PortSceneGpuOnly() != 0 && PortPresentGpu() != 0 && PortGpuZoomCan()) || RenderSoft();
}

double PortViewZoomSoftMost(void)
{
  return RenderSoft() ? static_cast<double>(SOFT_ZOOM_MOST) : 0.0;
}

int PortViewRenderSoft(void)
{
  return g_bRendering && g_bSoftPass ? 1 : 0;
}

// The game's buffer's map rows, from the software's render view as the camera shows it, each
// pixel the render view's under its middle; kept in g_softRef.
static void RenderSoftSample(void)
{
  const int nW = g_portView.nWidth;
  const int nRows = g_portView.nMapRows;
  const int nPitch = static_cast<WORD>(_img_draw_buf.nWidth);
  const int nRenderW = g_portRender.nWidth;
  const int nRenderRows = g_portRender.nRows;
  const double z = g_portRender.fZoom;
  static std::vector<int> s_column;
  s_column.resize(static_cast<size_t>(nW));
  for (int x = 0; x < nW; ++x) {
    const int n = static_cast<int>(floor(g_portRender.fCamX + (x + 0.5) * z)) - g_portRender.nX;
    s_column[x] = n < 0 ? 0 : n >= nRenderW ? nRenderW - 1 : n;
  }
  g_softRef.resize(static_cast<size_t>(nW) * nRows);
  for (int y = 0; y < nRows; ++y) {
    int nRow = static_cast<int>(floor(g_portRender.fCamY + (y + 0.5) * z)) - g_portRender.nY;
    nRow = nRow < 0 ? 0 : nRow >= nRenderRows ? nRenderRows - 1 : nRow;
    const BYTE *pIn = g_pRenderPixels + static_cast<size_t>(nRow) * nRenderW;
    BYTE *pOut = _img_draw_buf.pPixels + static_cast<size_t>(y) * nPitch;
    BYTE *pRef = &g_softRef[static_cast<size_t>(y) * nW];
    for (int x = 0; x < nW; ++x)
      pOut[x] = pRef[x] = pIn[s_column[x]];
  }
}

const unsigned char *PortViewSoftShown(unsigned int *pnPass)
{
  *pnPass = g_nSoftPasses;
  if (!g_bSoftShown || !g_portRender.bOn || g_bRendering || gwGameMode != 3)
    return NULL;
  return g_pRenderPixels;
}

void PortViewSoftCover(const unsigned char *pCopied)
{
  const int nW = g_portView.nWidth;
  const int nRows = g_portView.nMapRows;
  if (pCopied == NULL || !g_bSoftShown || !g_portRender.bOn || gwGameMode != 3 ||
      g_softRef.size() != static_cast<size_t>(nW) * nRows)
    return;
  // what is copied of the map's rows and still the sample shows the render view; what the
  // layers over the map drew (the map's text, the drag box, the cursor) shows itself
  const int nPitch = static_cast<WORD>(_img_draw_buf.nWidth);
  static std::vector<unsigned char> s_map;
  s_map.assign(static_cast<size_t>(nW) * g_portView.nHeight, 0);
  for (int y = 0; y < nRows; ++y) {
    const size_t nRow = static_cast<size_t>(y) * nW;
    const BYTE *pBuf = _img_draw_buf.pPixels + static_cast<size_t>(y) * nPitch;
    for (int x = 0; x < nW; ++x)
      s_map[nRow + x] = pCopied[nRow + x] != 0 && pBuf[x] == g_softRef[nRow + x] ? 1 : 0;
  }
  PortScreenCoverCopied(&s_map[0], nRows, 1);
}

void PortViewRenderBegin(void)
{
  if (g_bRendering)
    return;
  const bool bForced = PortCameraForced() != 0;
  const bool bGpu = PortSceneGpuOnly() != 0 && PortPresentGpu() != 0 && PortGpuZoomCan();
  // under SDL's renderer the software draws the render view (g_bSoftPass); HYPERION_ZOOM_FORCE's
  // check keeps the software's own path at the game's view
  const bool bSoft = !bGpu && !bForced && RenderSoft();
  const bool bZoom = bGpu || bSoft;
  // the zoom holds at 1 where the render view cannot be shown
  if (!bZoom && PortCameraZoom() != 1.0)
    PortCameraZoomTo(1.0);
  // the camera as it is now, with a skirmish's start's own zoom, which only comes in, so the
  // view drawn now holds what the presents until the next pass show (port_gpu.cpp)
  double fCamX, fCamY, fShown;
  g_bCamMayWrite = bZoom;
  const int bMoving = CameraUpdate(&fCamX, &fCamY, &fShown);
  g_bCamMayWrite = false;
  if (PortCameraStep() || bMoving)
    gamemap_force_redraw();  // the next pass too, while the camera eases
  const double fZoom = bZoom ? fShown : 1.0;
  // moved off the game's view at zoom 1 (a replay's camera): the render view too
  const bool bOff = bZoom && (fabs(fCamX - glScrollPixelX) >= 0.5 || fabs(fCamY - glScrollPixelY) >= 0.5);
  const int nStage = bSoft ? SCENE_SCREEN : PortSceneStage();
  if (gwGameMode != 3 || nStage != SCENE_SCREEN || (fZoom == 1.0 && !bForced && !bOff)) {
    g_portRender.bOn = 0;
    g_bSoftShown = false;
    if (g_bRenderedLast) {
      // the game's ring took no updates while the render view's did
      g_bRenderedLast = false;
      cell_paint();
    }
    return;
  }
  if (!bZoom)
    PortViewCameraPlace(1.0, glScrollPixelX, glScrollPixelY, &fCamX, &fCamY);
  RenderPlace(fZoom, fCamX, fCamY);
  // the software draws into the game's buffer only for HYPERION_ZOOM_FORCE's check, at the
  // game's own view
  if (!bZoom && (g_portRender.nX != glScrollPixelX || g_portRender.nY != glScrollPixelY ||
                 g_portRender.nWidth != g_portView.nWidth || g_portRender.nRows != g_portView.nMapRows)) {
    g_portRender.bOn = 0;
    g_bSoftShown = false;
    return;
  }

  g_saved.view = g_portView;
  g_saved.nScrollX = glScrollPixelX;
  g_saved.nScrollY = glScrollPixelY;
  g_saved.wTileX = gwScreenTileX_;
  g_saved.wTileY = gwScreenTileY_;
  g_saved.nBufWidth = _img_draw_buf.nWidth;
  g_saved.nBufHeight = _img_draw_buf.nHeight;
  g_saved.pPixels = _img_draw_buf.pPixels;
  g_saved.rcValid = sgrScreenValid_g;
  g_saved.rcSize = sgrScreenSize_g;

  // the render view, its rows below the map's as many as the game's (the console's)
  const int nBelow = g_portView.nHeight - g_portView.nMapRows;
  g_portView.nWidth = g_portRender.nWidth;
  g_portView.nMapRows = g_portRender.nRows;
  g_portView.nViewRows = g_portRender.nRows;
  g_portView.nHeight = g_portRender.nRows + nBelow;
  g_portView.nConsoleX = 0;
  ViewDerive(&g_portView);
  const size_t nGrid = static_cast<size_t>(g_portView.nGridWidth + 1) * (g_portView.nGridHeight + 1);
  if (g_renderGrid.size() < nGrid)
    g_renderGrid.resize(nGrid);
  memset(&g_renderGrid[0], 0, nGrid);
  g_portView.pbInvalMap = &g_renderGrid[0];
  glScrollPixelX = g_portRender.nX;
  glScrollPixelY = g_portRender.nY;
  gwScreenTileX_ = static_cast<WORD>(g_portRender.nX >> 5);
  gwScreenTileY_ = static_cast<WORD>(g_portRender.nY >> 5);
  if (bZoom) {
    const size_t nPixels = static_cast<size_t>(g_portView.nWidth) * g_portView.nHeight;
    if (nPixels > g_nRenderPixels) {
      // committed as touched: the software writes into it only what no report covers, or,
      // under SDL's renderer, all of it
      if (g_pRenderPixels != NULL)
        VirtualFree(g_pRenderPixels, 0, MEM_RELEASE);
      g_pRenderPixels = static_cast<BYTE *>(VirtualAlloc(NULL, nPixels, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
      g_nRenderPixels = g_pRenderPixels != NULL ? nPixels : 0;
    }
    const size_t nRing = static_cast<size_t>(g_portView.nRingSize);
    if (bSoft && nRing > g_nRenderRing) {
      if (g_pRenderRing != NULL)
        VirtualFree(g_pRenderRing, 0, MEM_RELEASE);
      g_pRenderRing = static_cast<BYTE *>(VirtualAlloc(NULL, nRing, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
      g_nRenderRing = g_pRenderRing != NULL ? nRing : 0;
    }
    if (g_pRenderPixels == NULL || (bSoft && g_pRenderRing == NULL)) {
      g_portView = g_saved.view;
      glScrollPixelX = g_saved.nScrollX;
      glScrollPixelY = g_saved.nScrollY;
      gwScreenTileX_ = g_saved.wTileX;
      gwScreenTileY_ = g_saved.wTileY;
      g_portRender.bOn = 0;
      g_bSoftShown = false;
      return;
    }
    _img_draw_buf.pPixels = g_pRenderPixels;
    // the software's terrain goes into a ring of the render view's size, drawn whole below
    g_saved.pRing = _gfpScrollBuf;
    if (bSoft)
      _gfpScrollBuf = g_pRenderRing;
  }
  _img_draw_buf.nWidth = static_cast<short>(g_portView.nWidth);
  _img_draw_buf.nHeight = static_cast<short>(g_portView.nHeight);
  SetRect(&sgrScreenValid_g, 0, 0, g_portView.nWidth - 1, g_portView.nHeight - 1);
  SetRect(&sgrScreenSize_g, 0, 0, g_portView.nWidth, g_portView.nHeight);
  g_bRendering = true;
  g_bSoftPass = bSoft;
  g_bRenderedLast = true;
  g_portRender.bOn = 1;

  // what the pass draws from, made for the render view: the sprites in the rows it reaches,
  // and every megatile of it into the scene's own slots (port_scene.cpp)
  PortSceneRenderBegin();
  gamemap_priority_update();
  // the GPU's own ring from the map's cells, written where it changed; the software's
  // ring drawn whole
  if (!PortSceneRingFill())
    cell_paint();
}

void PortViewRenderEnd(void)
{
  if (!g_bRendering)
    return;
  PortSceneRenderEnd();
  const int nWidth = g_saved.view.nWidth;
  const int nMapRows = g_saved.view.nMapRows;
  g_portView = g_saved.view;
  glScrollPixelX = g_saved.nScrollX;
  glScrollPixelY = g_saved.nScrollY;
  gwScreenTileX_ = g_saved.wTileX;
  gwScreenTileY_ = g_saved.wTileY;
  _img_draw_buf.nWidth = g_saved.nBufWidth;
  _img_draw_buf.nHeight = g_saved.nBufHeight;
  _img_draw_buf.pPixels = g_saved.pPixels;
  sgrScreenValid_g = g_saved.rcValid;
  sgrScreenSize_g = g_saved.rcSize;
  const bool bSoft = g_bSoftPass;
  if (bSoft)
    _gfpScrollBuf = g_saved.pRing;
  g_bRendering = false;
  g_bSoftPass = false;
  // the screen takes all of the map's layer, as PortSceneMapBegin marks it without the
  // render view
  img_area_invalid(0, 0, nWidth - 1, nMapRows - 1);
  g_bSoftShown = bSoft;
  if (bSoft) {
    RenderSoftSample();
    ++g_nSoftPasses;
  }
}

int PortViewWheel(int nDelta)
{
  if (!PortViewPointOnMap(static_cast<int>(gCursorPos_g[0]), static_cast<int>(gCursorPos_g[1])) ||
      !RenderZoomCan())
    return 0;
  // toward the player zooms out, as in most strategy games, about the map point under the
  // cursor, which stays under it
  double fCamX, fCamY, z;
  PortViewCamera(&fCamX, &fCamY, &z);
  g_fAnchorSX = gCursorPos_g[0] + 0.5;
  g_fAnchorSY = gCursorPos_g[1] + 0.5;
  g_fAnchorMX = fCamX + g_fAnchorSX * z;
  g_fAnchorMY = fCamY + g_fAnchorSY * z;
  g_bCamAnchor = true;
  PortTvInput();
  PortCameraWheel(-nDelta / 120.0);
  gamemap_force_redraw();
  return 1;
}

int PortViewPointOnMap(int nX, int nY)
{
  if (gwGameMode != 3 || nX < 0 || nY < 0 || nX >= g_portView.nWidth || nY >= g_portView.nMapRows)
    return 0;
  // the console's art over the map's rows, and any dialog shown
  if (nY >= g_portView.nViewRows && nX >= g_portView.nConsoleX && nX < g_portView.nConsoleX + VIEW_MIN_WIDTH)
    return 0;
  for (const ctrl_t *pDlg = gpDlgList; pDlg != NULL; pDlg = pDlg->pNext)
    if ((pDlg->dwFlags & 8) != 0 && nX >= pDlg->rc.left && nX <= pDlg->rc.right && nY >= pDlg->rc.top &&
        nY <= pDlg->rc.bottom)
      return 0;
  return 1;
}
