// port_present.cpp -- the screen, an 8-bit buffer of Hyperion's own in place of Storm's SDraw
// surface at the view's size (port_view.cpp), and its presenting through SDL3: the display
// kind, the cover, the frame rate, the mouse in game pixels and screenshots
// (docs/modules/render.md; docs/renderer.md). Presentation only.
//
// The sites that drew onto Storm's surface lock this buffer, and flip_palette hands its entries
// here. PortPresentFrame gives both to SDL's renderer or to Hyperion's presenter on SDL_GPU
// (port_gpu.cpp), as HYPERION_RENDERER picks until the settings do; a GPU driver that cannot
// start falls back to SDL's, and hyperion_render.log names the one in use.
//
// The window is SDL's, made by InitWindow (port_winmsg.cpp). SDL's mouse capture is off: the
// game captures the mouse itself while a button is down, and a capture SDL took or released
// beside it reached the game as WM_CAPTURECHANGED, ending a map drag before its button came up.
// HYPERION_WINDOW and HYPERION_DISPLAY say how and where the window is shown, else Settings'
// saved choice (PortDisplaySaved), borderless full screen until the player chooses.
#include <windows.h>
#include <emmintrin.h>  // SSE2 (PortScreenCoverCopied)
#include <deque>
#include <string>
#include <SDL3/SDL.h>
#include "../tools/port_bench.h"
#include "../loop/port_clock.h"  // PortClockShown
#include "../tools/port_debugview.h"  // PortDebugViewShown
#include "port_overlay.h"  // PortOverlayFrameShown, PortOverlayHas, PortOverlayOutside
#include "../platform/port_paths.h"  // PortLogFile, PortCommandOptions, PortDataFolder
#include "../loop/port_perf.h"  // PortPerfWanted, PortPerfLine, PERF_PARTS
#include "../tools/port_framelog.h"  // a row a frame shown
#include "port_gpu_hd.h"  // PortGpuHdDrawCount
#include "../platform/port_posix.h"  // PortNativePath, PortWebTurn
#include "port_present.h"
#include "port_hwcursor.h"
#include "../data/port_chatfilter.h"  // PortChatFilterSet
#include "port_view.h"
#include "img.h"  // img_invalid
#include "../platform/port_log.h"  // PortLogger
#include "../platform/port_winmsg.h"
#include "../tools/port_control.h"
#include "../lan/port_lanclient.h"  // LanClientPump
#include "../lan/port_lanstream.h"  // LanStreamFrame
#include "../lan/port_lanfeed.h"    // LanWatchFollowPump
#include "../lab/port_labfarm.h"     // PortLabFarmPump
#include "../platform/port_discord.h"  // PortDiscordPump
#include "port_gpu.h"
#include "port_scene.h"
#include "../platform/port_settings.h"
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"
#include "../tools/port_startup.h"  // PortStartupStep, PortStartupFail, PortStartupFrame
#include "../tools/port_sysinfo.h"  // PortSysInfoSet

// SDL's string and environment functions, not the C runtime's: when the engine still
// ran beside 1.17's code, several runtime names were bound to the original's copies,
// and pulling libcmt's in beside them failed the link.

// The screen holds the widest view; its rows are the current view's width apart.
static BYTE g_screen[PORT_VIEW_MAX_WIDTH * PORT_VIEW_MAX_HEIGHT];
// The screen's cover (port_present.h, PortScreenCoverCopied): 1 where the frame shows the GPU
// scene's map, in the view's rows as the screen's; cleared when the view changes size.
static BYTE g_cover[PORT_VIEW_MAX_WIDTH * PORT_VIEW_MAX_HEIGHT];
static int g_nCoverWidth;
static int g_nCoverHeight;
// HYPERION_PERF=1: once a second, hyperion_perf.log in the folder the game runs in
// gets how many frames were shown and passes drawn, and where the time went.
static int g_perf = -1;
static Uint64 g_perfSince;
static unsigned int g_nPerfFrames;
static unsigned int g_nPerfScenes;
static double g_dPerfSceneMs;
static double g_dPerfPresentMs;
// The frame as shown, for a snapshot or a screenshot, when the GPU alone draws the map.
static BYTE g_shown[PORT_VIEW_MAX_WIDTH * PORT_VIEW_MAX_HEIGHT];
static HWND g_hWnd;
static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;
// SDL's renderer zoomed (PresentSoftZoom): the software's render view in colour, and the
// screen in colour, clear where it shows the map
static SDL_Texture *g_zoomMap;
static int g_nZoomMapW, g_nZoomMapH;
static unsigned int g_nZoomPass;
static Uint32 g_zoomColors[256];
static SDL_Texture *g_zoomScreen;
static int g_nZoomScreenW, g_nZoomScreenH;
static int g_nArtFilter;  // Settings, "Classic art filtering": 0 sharp to 3 filtered (PortArtFilterSet)
static int g_nTextureWidth;   // the size the texture and the logical presentation have
static int g_nTextureHeight;
static SDL_Palette *g_palette;
static bool g_gpu;            // the GPU presenter is in use, not SDL's renderer
static bool g_gpuCheck;
static bool g_paletteChanged;  // since the GPU presenter last took it
static bool g_hidden;
static bool g_remoteInput;  // a streamed seat's: the mouse in game pixels (PortPresentRemoteInput)
static bool g_background;
static bool g_testMouse;      // HYPERION_TEST_MOUSE: a test's posted mouse is taken
static bool g_fullscreen;
static int g_kind = PORT_DISPLAY_BORDERLESS;  // the full screen kind while g_fullscreen
static bool g_frameDirty;
static unsigned int g_nScreenWrites;  // PortScreenWrites

// The cursor cage, in game pixels, and whether the game asked for one. While the
// front end is up the cage stands down: the front end has the whole window.
static RECT g_cageGame;
static bool g_caged;
static bool g_frontUp;
static RECT g_lastClient;
// Settings: "Confine the mouse to the window" ("confineMouse") holds the cursor in a game's
// window when 1.17 cages it to the whole view; the cages it sets for a drag (the minimap's)
// hold all the same. "Middle-mouse drag scroll" ("dragScroll").
static bool g_confine = true;
// Ctrl+Alt lets the cursor go in a game, as VMware's does (PortCursorFreeSet): no cage holds it,
// and off the game's picture the game keeps its pointer in from the edges; a click on the game
// takes it back.
static bool g_cursorFree;
// A layer's drag of its own (PortCursorHoldScroll): the map does not scroll under it.
static bool g_holdScroll;
// Off the game's picture the game draws no cursor there, so Windows' arrow shows (PortCursorOffGame).
// 1.17 hides the system cursor with ShowCursor(FALSE) (show_cursor, event_Win.cpp), and no cursor
// shows over the window while that display count is below 0, so the arrow raises it to 0 and
// keeps how many raises that took, to lower it by as many when the pointer is back on the game.
static bool g_arrowShown;
static int g_nArrowRaises;
// What the system shows over the window in a game, NULL for nothing: the arrow, or with Settings'
// "Hardware cursor" the game's own cursor (port_hwcursor.cpp), which one display count serves.
static SDL_Cursor *g_pSystemShown;

static void PresentSystemCursor(SDL_Cursor *pCursor)
{
  if (pCursor == g_pSystemShown)
    return;
  if (pCursor != NULL) {
    SDL_SetCursor(pCursor);
    if (g_pSystemShown == NULL) {
      SDL_ShowCursor();
      while (g_nArrowRaises < 32 && ShowCursor(TRUE) < 0)
        ++g_nArrowRaises;
      ++g_nArrowRaises;
    }
  } else {
    for (; g_nArrowRaises > 0; --g_nArrowRaises)
      ShowCursor(FALSE);
    SDL_HideCursor();
    SDL_SetCursor(SDL_GetDefaultCursor());
  }
  g_pSystemShown = pCursor;
  const bool bGame = pCursor != NULL && pCursor != SDL_GetDefaultCursor();
  if (bGame != PortHwCursorShowing()) {
    PortHwCursorShown(bGame);
    img_invalid(0);  // the game draws its cursor again, or stops
  }
}

static void PresentArrow(bool bShow)
{
  g_arrowShown = bShow;
  PresentSystemCursor(bShow ? SDL_GetDefaultCursor() : NULL);
  if (bShow)
    SetCursor(LoadCursor(NULL, IDC_ARROW));
}
static bool g_dragScroll = true;
static float g_fDragSpeed = PORT_DRAG_SPEED_DEFAULT;  // "dragScrollSpeed", 0 to 1
// Settings' "Mouse sensitivity" ("mouseSensitivity", off unless set on, and
// "mouseSensitivitySpeed", 0 to 1), applied by PortMouseScale while a cage holds the cursor
// (g_held). g_sensAt says where the pointer was last seen is known; anything else that moves the
// pointer clears it.
static bool g_sens;
static float g_fSensScale = 1.0f;
static bool g_held;
static bool g_sensAt;

static void PresentFatal(const char *pszWhat)
{
  char szMsg[512];

  SDL_snprintf(szMsg, sizeof szMsg, "%s failed: %s", pszWhat, SDL_GetError());
  if (PortStartupPending())
    PortStartupFail("presenter", "The window could not be drawn: %s. Updating the graphics driver may help.", szMsg);
  MessageBoxA(NULL, szMsg, "Hyperion", MB_OK | MB_ICONERROR);
  ExitProcess(1);
}

// Windowed: the largest whole multiple of the game's frame that fits the
// display's usable area with the window frame around it, centred. The game's
// frame is --view's (port_view.cpp), so a window for a wide game is wide from
// the start, and the menus' 640x480 sits in its middle.
static void PresentSetWindowed(void)
{
  SDL_Rect usable;
  RECT frame = {0, 0, 0, 0};
  const int nWidth = PortViewGameWidth();
  const int nHeight = g_portView.nHeight;
  int nScale;

  SDL_SetWindowFullscreen(g_window, false);
  SDL_SetWindowBordered(g_window, true);
  SDL_SetWindowResizable(g_window, true);
  if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(g_window), &usable)) {
    usable.w = nWidth * 2;
    usable.h = nHeight * 2;
  }
  AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
  nScale = 1;
  while (nWidth * (nScale + 1) + (frame.right - frame.left) <= usable.w &&
         nHeight * (nScale + 1) + (frame.bottom - frame.top) <= usable.h)
    ++nScale;
  SDL_SetWindowSize(g_window, nWidth * nScale, nHeight * nScale);
  SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  g_fullscreen = false;
}

static void PresentSetFullscreen(void)
{
#if defined(__EMSCRIPTEN__)
  // in a browser, always the window (PresentShow)
  PresentSetWindowed();
  return;
#endif
  SDL_SetWindowFullscreenMode(g_window, NULL);
  SDL_SetWindowFullscreen(g_window, true);
  g_fullscreen = true;
  g_kind = PORT_DISPLAY_BORDERLESS;
}

// ---------------------------------------------------------------------------
// The display the player chose in Settings, kept in the settings file's "hyperion" section
// as "display" (window, fullscreen or borderless), "monitor", "monitorIndex", "width" and
// "height". Without them the window is borderless on the primary display, as before.

static const char *const kKindNames[] = {"window", "fullscreen", "borderless"};

static void PresentApplyCage(void);

static void PickFor(SDL_DisplayID id, int nIndex, PortDisplayPick *pPick)
{
  const char *pszName = SDL_GetDisplayName(id);
  SDL_strlcpy(pPick->szMonitor, pszName != NULL ? pszName : "", sizeof pPick->szMonitor);
  pPick->nMonitor = nIndex;
  pPick->bPrimary = id == SDL_GetPrimaryDisplay();
  pPick->nWidth = 0;
  pPick->nHeight = 0;
}

// The displays, the primary's first.
static int Displays(SDL_DisplayID *pIds, int nMax)
{
  int nCount = 0;
  SDL_DisplayID *pAll = SDL_GetDisplays(&nCount);
  const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
  int n = 0;
  if (pAll == NULL)
    return 0;
  for (int i = 0; i < nCount && n < nMax; ++i)
    if (pAll[i] == primary)
      pIds[n++] = pAll[i];
  for (int i = 0; i < nCount && n < nMax; ++i)
    if (pAll[i] != primary)
      pIds[n++] = pAll[i];
  SDL_free(pAll);
  return n;
}

// Each display, for the start-up log and a crash report: its name, size, refresh and scale.
static void PresentNoteDisplays(void)
{
  SDL_DisplayID aIds[16];
  const int n = Displays(aIds, 16);
  PortStartupStep("displays", "%d, the video driver %s", n, SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?");
  for (int i = 0; i < n; ++i) {
    const SDL_DisplayMode *pMode = SDL_GetDesktopDisplayMode(aIds[i]);
    const char *pszName = SDL_GetDisplayName(aIds[i]);
    char szPart[16], szMode[256];
    SDL_snprintf(szPart, sizeof szPart, "display %d", i + 1);
    SDL_snprintf(szMode, sizeof szMode, "%s%s, %dx%d at %.2f Hz, scale %.2f", pszName != NULL ? pszName : "?",
                 i == 0 ? " (primary)" : "", pMode != NULL ? pMode->w : 0, pMode != NULL ? pMode->h : 0,
                 pMode != NULL ? pMode->refresh_rate : 0.0f, SDL_GetDisplayContentScale(aIds[i]));
    PortSysInfoSet(szPart, "mode", "%s", szMode);
    PortStartupStep(szPart, "%s", szMode);
  }
}

// HYPERION_DISPLAY's screen (above), or 0 for the primary.
static SDL_DisplayID TestDisplay(void)
{
  const char *psz = SDL_getenv("HYPERION_DISPLAY");
  if (psz == NULL || *psz == '\0')
    return 0;
  SDL_DisplayID ids[16];
  const int n = Displays(ids, 16);
  const bool bLeft = SDL_strcasecmp(psz, "left") == 0, bRight = SDL_strcasecmp(psz, "right") == 0;
  if (!bLeft && !bRight) {
    const int k = SDL_atoi(psz);
    return k >= 1 && k <= n ? ids[k - 1] : 0;
  }
  SDL_DisplayID best = 0;
  int nBest = 0;
  for (int i = 0; i < n; ++i) {
    SDL_Rect r;
    if (!SDL_GetDisplayBounds(ids[i], &r))
      continue;
    if (best == 0 || (bLeft ? r.x < nBest : r.x > nBest)) {
      best = ids[i];
      nBest = r.x;
    }
  }
  return best;
}

// The pick's display: the one at its place if the name agrees, else the first of its name,
// else the primary.
static SDL_DisplayID DisplayOf(const PortDisplayPick *pPick)
{
  SDL_DisplayID ids[16];
  const int n = Displays(ids, 16);
  if (pPick->nMonitor >= 0 && pPick->nMonitor < n) {
    const char *pszName = SDL_GetDisplayName(ids[pPick->nMonitor]);
    if (pszName != NULL && SDL_strcmp(pszName, pPick->szMonitor) == 0)
      return ids[pPick->nMonitor];
  }
  for (int i = 0; i < n; ++i) {
    const char *pszName = SDL_GetDisplayName(ids[i]);
    if (pszName != NULL && SDL_strcmp(pszName, pPick->szMonitor) == 0)
      return ids[i];
  }
  return SDL_GetPrimaryDisplay();
}

static int DisplayIndex(SDL_DisplayID id)
{
  SDL_DisplayID ids[16];
  const int n = Displays(ids, 16);
  for (int i = 0; i < n; ++i)
    if (ids[i] == id)
      return i;
  return 0;
}

// The client area a display's usable bounds leave a window with its frame.
static void WindowRoom(SDL_DisplayID id, int *pnWidth, int *pnHeight)
{
  SDL_Rect usable;
  RECT frame = {0, 0, 0, 0};
  if (!SDL_GetDisplayUsableBounds(id, &usable)) {
    *pnWidth = *pnHeight = 0;
    return;
  }
  AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
  *pnWidth = usable.w - (frame.right - frame.left);
  *pnHeight = usable.h - (frame.bottom - frame.top);
}

int PortDisplayChoices(int nKind, PortDisplayPick *pPicks, int nMax)
{
  SDL_DisplayID ids[16];
  const int nDisplays = Displays(ids, 16);
  int n = 0;
  for (int d = 0; d < nDisplays && n < nMax; ++d) {
    PortDisplayPick base;
    PickFor(ids[d], d, &base);
    if (nKind == PORT_DISPLAY_BORDERLESS) {
      const SDL_DisplayMode *pDesk = SDL_GetDesktopDisplayMode(ids[d]);
      if (pDesk == NULL)
        continue;
      pPicks[n] = base;
      pPicks[n].nWidth = pDesk->w;
      pPicks[n].nHeight = pDesk->h;
      ++n;
      continue;
    }
    int nRoomW = 0, nRoomH = 0;
    if (nKind == PORT_DISPLAY_WINDOW)
      WindowRoom(ids[d], &nRoomW, &nRoomH);
    int nModes = 0;
    SDL_DisplayMode **ppModes = SDL_GetFullscreenDisplayModes(ids[d], &nModes);
    if (ppModes == NULL)
      continue;
    // largest first, as SDL lists them; one each size, whatever its refresh rates
    for (int m = 0; m < nModes && n < nMax; ++m) {
      const SDL_DisplayMode *pMode = ppModes[m];
      if (pMode->w < 640 || pMode->h < 480)
        continue;
      if (nKind == PORT_DISPLAY_WINDOW && (pMode->w > nRoomW || pMode->h > nRoomH))
        continue;
      bool bSeen = false;
      for (int k = 0; k < n && !bSeen; ++k)
        bSeen = pPicks[k].nMonitor == d && pPicks[k].nWidth == pMode->w && pPicks[k].nHeight == pMode->h;
      if (bSeen)
        continue;
      pPicks[n] = base;
      pPicks[n].nWidth = pMode->w;
      pPicks[n].nHeight = pMode->h;
      ++n;
    }
    SDL_free(ppModes);
  }
  return n;
}

void PortDisplaySaved(int *pnKind, PortDisplayPick *pPick)
{
  char sz[96];
  double d = 0.0;
  *pnKind = PORT_DISPLAY_BORDERLESS;
  if (PortSettingString("hyperion", "display", sz, sizeof sz))
    for (int k = 0; k < 3; ++k)
      if (SDL_strcmp(sz, kKindNames[k]) == 0)
        *pnKind = k;
  PickFor(SDL_GetPrimaryDisplay(), 0, pPick);
  if (PortSettingString("hyperion", "monitor", sz, sizeof sz)) {
    PortDisplayPick want;
    SDL_strlcpy(want.szMonitor, sz, sizeof want.szMonitor);
    want.nMonitor = PortSettingNumber("hyperion", "monitorIndex", &d) ? static_cast<int>(d) : -1;
    const SDL_DisplayID id = DisplayOf(&want);
    PickFor(id, DisplayIndex(id), pPick);
  }
  if (PortSettingNumber("hyperion", "width", &d))
    pPick->nWidth = static_cast<int>(d);
  if (PortSettingNumber("hyperion", "height", &d))
    pPick->nHeight = static_cast<int>(d);
  // borderless is always the desktop's size, and a pick without a size takes it too
  if (*pnKind == PORT_DISPLAY_BORDERLESS || pPick->nWidth <= 0 || pPick->nHeight <= 0) {
    if (const SDL_DisplayMode *pDesk = SDL_GetDesktopDisplayMode(DisplayOf(pPick))) {
      pPick->nWidth = pDesk->w;
      pPick->nHeight = pDesk->h;
    }
  }
}

void PortDisplayNow(int *pnKind, PortDisplayPick *pPick)
{
  const SDL_DisplayID id = g_window != NULL ? SDL_GetDisplayForWindow(g_window) : SDL_GetPrimaryDisplay();
  PickFor(id, DisplayIndex(id), pPick);
  *pnKind = g_fullscreen ? g_kind : PORT_DISPLAY_WINDOW;
  if (g_fullscreen) {
    const SDL_DisplayMode *pMode = g_kind == PORT_DISPLAY_FULLSCREEN ? SDL_GetWindowFullscreenMode(g_window) : NULL;
    if (pMode == NULL)
      pMode = SDL_GetDesktopDisplayMode(id);
    if (pMode != NULL) {
      pPick->nWidth = pMode->w;
      pPick->nHeight = pMode->h;
    }
  } else {
    PortPresentClientSize(&pPick->nWidth, &pPick->nHeight);
  }
}

// The window to its display first, out of full screen if it must leave one display for another.
static void PresentToDisplay(SDL_DisplayID id)
{
  if (SDL_GetDisplayForWindow(g_window) == id)
    return;
  if (g_fullscreen) {
    SDL_SetWindowFullscreen(g_window, false);
    SDL_SyncWindow(g_window);
  }
  SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(id), SDL_WINDOWPOS_CENTERED_DISPLAY(id));
  SDL_SyncWindow(g_window);
}

static void PresentShow(int nKind, const PortDisplayPick *pPick)
{
#if defined(__EMSCRIPTEN__)
  // In a browser the window is the page's canvas, which the page scales to the tab (web/play).
  // SDL's full screen there waits for a click and leaves the canvas at its size while SDL draws
  // at the screen's, so the frame came out shifted and cropped, past all sense on a large
  // screen; so the browser build is always the window, at the game's size.
  (void)nKind;
  (void)pPick;
  PresentSetWindowed();
  return;
#endif
  const SDL_DisplayID id = DisplayOf(pPick);
  PresentToDisplay(id);
  if (nKind == PORT_DISPLAY_WINDOW) {
    int nRoomW = 0, nRoomH = 0;
    WindowRoom(id, &nRoomW, &nRoomH);
    int nWidth = pPick->nWidth, nHeight = pPick->nHeight;
    if (nWidth <= 0 || nHeight <= 0 || nWidth > nRoomW || nHeight > nRoomH) {
      PresentSetWindowed();  // the largest whole multiple of the game's frame that fits
      return;
    }
    SDL_SetWindowFullscreen(g_window, false);
    SDL_SetWindowBordered(g_window, true);
    SDL_SetWindowResizable(g_window, true);
    SDL_SetWindowSize(g_window, nWidth, nHeight);
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(id), SDL_WINDOWPOS_CENTERED_DISPLAY(id));
    g_fullscreen = false;
    return;
  }
  if (nKind == PORT_DISPLAY_FULLSCREEN) {
    SDL_DisplayMode mode;
    if (SDL_GetClosestFullscreenDisplayMode(id, pPick->nWidth, pPick->nHeight, 0.0f, false, &mode))
      SDL_SetWindowFullscreenMode(g_window, &mode);
    else
      SDL_SetWindowFullscreenMode(g_window, NULL);
  } else {
    SDL_SetWindowFullscreenMode(g_window, NULL);
  }
  SDL_SetWindowFullscreen(g_window, true);
  g_fullscreen = true;
  g_kind = nKind;
}

bool PortDisplayShow(int nKind, const PortDisplayPick *pPick)
{
  if (g_window == NULL || g_hidden || g_background)
    return false;
  PresentShow(nKind, pPick);
  SDL_SyncWindow(g_window);
  SDL_memset(&g_lastClient, 0, sizeof g_lastClient);
  g_frameDirty = true;
  PresentApplyCage();
  return true;
}

void PortDisplaySave(int nKind, const PortDisplayPick *pPick)
{
  PortSettingsHold();
  PortSettingSetString("hyperion", "display", kKindNames[nKind < 0 || nKind > 2 ? 2 : nKind]);
  PortSettingSetString("hyperion", "monitor", pPick->szMonitor);
  PortSettingSetNumber("hyperion", "monitorIndex", pPick->nMonitor);
  PortSettingSetNumber("hyperion", "width", pPick->nWidth);
  PortSettingSetNumber("hyperion", "height", pPick->nHeight);
  PortSettingsRelease();
}

// ---------------------------------------------------------------------------
// The video player's screen (port_stormvid.cpp). Storm's SVid drew through
// SDraw's entry points, and these do what the port's stand-ins for them did.

void PortVidScreenSize(ULONG *pdwWidth, ULONG *pdwHeight)
{
  *pdwWidth = g_portView.nWidth;
  *pdwHeight = g_portView.nHeight;
}

int PortVidLock(void **ppBits, int *pnPitch)
{
  PortScreenCoverClear();
  return PortScreenLock(ppBits, pnPitch);
}

// The player unlocks once per frame, and play_flic runs no img_update pass
// while a cinematic plays, so the frame is presented here.
void PortVidUnlock(void)
{
  PortScreenUnlock();
  PortPresentFrame();
}

// Shown at once, as Storm's palette change was: a cut to a scene with new
// colours would otherwise show one frame in the old ones.
void PortVidPalette(const void *pEntries, int nFirst, int nCount)
{
  PortPresentPalette(pEntries, nFirst, nCount);
  PortPresentFrame();
}

void PortVidClear(void)
{
  SDL_memset(g_screen, 0, g_portView.nWidth * g_portView.nHeight);
  PortScreenCoverClear();
  g_frameDirty = true;
  ++g_nScreenWrites;
}

bool PortVidFullColour(void) { return g_gpu; }

int PortVidWindowRows(void)
{
  int nWidth = 0, nHeight = 0;
  if (g_window != NULL)
    SDL_GetWindowSizeInPixels(g_window, &nWidth, &nHeight);
  return nHeight;
}

void PortVidPicture(const PortVideoPlanes *pPlanes, int nShowW, int nShowH)
{
  if (!g_gpu)
    return;
  PortGpuVideoSet(pPlanes, nShowW, nShowH);
  g_frameDirty = true;
  PortPresentFrame();
}

// The cover's values differ from those the GPU was last given (PortGpuCoverChanged): once a
// game settles, the cover stays the same frame after frame, and it was copied and uploaded
// with every one, a screen's bytes each time.
static bool g_coverChanged = true;

void PortScreenCoverClear(void)
{
  SDL_memset(g_cover, 0, sizeof g_cover);
  g_coverChanged = true;
  g_nCoverWidth = g_portView.nWidth;
  g_nCoverHeight = g_portView.nHeight;
}

// The cover as the view's size has it; a cover of another size says nothing of it.
static void PresentCoverFit(void)
{
  if (g_nCoverWidth != g_portView.nWidth || g_nCoverHeight != g_portView.nHeight)
    PortScreenCoverClear();
}

void PortScreenCoverCopied(const unsigned char *pCopied, int nRows, int bScene)
{
  const int nWidth = g_portView.nWidth;
  const int nHeight = g_portView.nHeight;

  PresentCoverFit();
  // 16 pixels at a time, as PortMaskBlt (port_mask.cpp); a byte that changes says so
  const __m128i zero = _mm_setzero_si128();
  bool bChanged = false;
  for (int y = 0; y < nHeight; ++y) {
    const BYTE bMark = bScene && y < nRows ? 1 : 0;
    const __m128i mark = _mm_set1_epi8(static_cast<char>(bMark));
    const unsigned char *pIn = pCopied + static_cast<size_t>(y) * nWidth;
    BYTE *pOut = g_cover + static_cast<size_t>(y) * nWidth;
    int x = 0;
    for (; x + 16 <= nWidth; x += 16) {
      const __m128i keep = _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i *>(pIn + x)), zero);
      const int nKeep = _mm_movemask_epi8(keep);
      if (nKeep == 0xFFFF)
        continue;
      const __m128i out = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pOut + x));
      const __m128i next = nKeep == 0 ? mark : _mm_or_si128(_mm_and_si128(keep, out), _mm_andnot_si128(keep, mark));
      if (_mm_movemask_epi8(_mm_cmpeq_epi8(next, out)) != 0xFFFF) {
        _mm_storeu_si128(reinterpret_cast<__m128i *>(pOut + x), next);
        bChanged = true;
      }
    }
    for (; x < nWidth; ++x)
      if (pIn[x] != 0 && pOut[x] != bMark) {
        pOut[x] = bMark;
        bChanged = true;
      }
  }
  if (bChanged)
    g_coverChanged = true;
}

void PortScreenCoverRect(int nLeft, int nTop, int nWidth, int nHeight)
{
  PresentCoverFit();
  if (nLeft < 0) {
    nWidth += nLeft;
    nLeft = 0;
  }
  if (nTop < 0) {
    nHeight += nTop;
    nTop = 0;
  }
  if (nLeft + nWidth > g_portView.nWidth)
    nWidth = g_portView.nWidth - nLeft;
  if (nTop + nHeight > g_portView.nHeight)
    nHeight = g_portView.nHeight - nTop;
  for (int y = 0; y < nHeight; ++y)
    if (nWidth > 0) {
      SDL_memset(g_cover + static_cast<size_t>(nTop + y) * g_portView.nWidth + nLeft, 0, nWidth);
      g_coverChanged = true;
    }
}

static void PresentApplyCage(void);

// The texture, the logical presentation and a background window take the view's
// size (port_view.cpp) when it differs from theirs: at start-up, and when the
// view changes between frames.
// A background window's size in a game: the frame's, or HYPERION_GAME_SIZE=WxH's for a check of
// what is drawn round the game (a benchmark run's overlay, port_benchview.cpp).
static void PresentTestGameSize(int *pnW, int *pnH)
{
  *pnW = g_portView.nWidth;
  *pnH = g_portView.nHeight;
  const char *pszSize = SDL_getenv("HYPERION_GAME_SIZE");
  int nW = 0, nH = 0;
  if (g_background && pszSize != NULL && SDL_sscanf(pszSize, "%dx%d", &nW, &nH) == 2 && nW > 0 && nH > 0) {
    *pnW = nW;
    *pnH = nH;
  }
}

static void PresentFitView(void)
{
  if (g_nTextureWidth == g_portView.nWidth && g_nTextureHeight == g_portView.nHeight)
    return;
  if (g_renderer != NULL) {
    if (g_texture != NULL)
      SDL_DestroyTexture(g_texture);
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_INDEX8, SDL_TEXTUREACCESS_STREAMING,
                                  g_portView.nWidth, g_portView.nHeight);
    if (g_texture == NULL)
      PresentFatal("SDL_CreateTexture");
    if (!SDL_SetTexturePalette(g_texture, g_palette))
      PresentFatal("SDL_SetTexturePalette");
    SDL_SetTextureScaleMode(g_texture, g_nArtFilter == 0 ? SDL_SCALEMODE_PIXELART : SDL_SCALEMODE_LINEAR);
    SDL_SetRenderLogicalPresentation(g_renderer, g_portView.nWidth, g_portView.nHeight,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
  }
  g_nTextureWidth = g_portView.nWidth;
  g_nTextureHeight = g_portView.nHeight;
  if ((g_hidden || g_background) && g_window != NULL) {
    int nW, nH;
    PresentTestGameSize(&nW, &nH);
    SDL_SetWindowSize(g_window, nW, nH);
  }
  g_frameDirty = true;
  PresentApplyCage();
}

// The presenter HYPERION_RENDERER names. Without it the GPU presenter on the driver Settings'
// Renderer chose ("renderer": vulkan, the default, or direct3d12), else on the driver SDL_GPU
// picks, Direct3D 12 on Windows; SDL's renderer when it names sdl, or when no GPU driver starts. Vulkan comes first because SDL's Direct3D 12 swapchain held frames 39 to 49 ms
// covering a display, where Vulkan showed them a refresh after. The GPU
// presenter starts the GPU scene too (PortSceneDefault), which the front end's art, HD and the
// lights draw from.
static void PresentCreate(void)
{
  const char *pszRenderer = SDL_getenv("HYPERION_RENDERER");
  const char *pszCheck = SDL_getenv("HYPERION_GPU_CHECK");
  const char *pszDriver = "vulkan";
  const bool bAsked = pszRenderer != NULL && pszRenderer[0] != '\0';
  bool bGpu = !bAsked;
  char szChosen[16];
  if (!bAsked && PortSettingString("hyperion", "renderer", szChosen, sizeof szChosen) &&
      SDL_strcasecmp(szChosen, "direct3d12") == 0)
    pszDriver = "direct3d12";

  if (bAsked) {
    if (SDL_strcasecmp(pszRenderer, "direct3d12") == 0 || SDL_strcasecmp(pszRenderer, "d3d12") == 0) {
      bGpu = true;
      pszDriver = "direct3d12";
    } else if (SDL_strcasecmp(pszRenderer, "vulkan") == 0) {
      bGpu = true;
    } else if (SDL_strcasecmp(pszRenderer, "gpu") == 0) {
      bGpu = true;
      pszDriver = NULL;
    }
  }
#if !defined(_WIN32)
  // There is no Direct3D 12 off Windows: asked for, it means the GPU presenter on the driver
  // SDL_GPU picks, Vulkan on Linux.
  if (pszDriver != NULL && SDL_strcasecmp(pszDriver, "direct3d12") == 0)
    pszDriver = NULL;
#endif
  g_gpuCheck = pszCheck != NULL && pszCheck[0] == '1';
  // A hidden window is never presented, so it needs no swapchain. Each one tried goes in the
  // start-up log, with why it did not start.
  std::string tried;
  auto attempt = [&](const char *pszTry) {
    if (PortGpuInit(g_window, pszTry, !g_hidden))
      return true;
    const std::string line = std::string(pszTry != NULL ? pszTry : "SDL's choice of GPU driver") + ": " + PortGpuWhy();
    PortStartupStep("presenter", "%s", line.c_str());
    tried += (tried.empty() ? "" : "; ") + line;
    return false;
  };
  if (bGpu && (attempt(pszDriver) || (!bAsked && pszDriver != NULL && attempt(NULL)))) {
    g_gpu = true;
    PortSceneDefault();
    return;
  }
  g_renderer = SDL_CreateRenderer(g_window, NULL);
  if (g_renderer == NULL)
    PresentFatal("SDL_CreateRenderer");
  PortStartupStep("presenter", "SDL's renderer, %s%s", SDL_GetRendererName(g_renderer),
                  bGpu ? ", as the GPU presenter did not start" : "");
  PortSysInfoSet("presenter", "renderer", "SDL's %s%s%s", SDL_GetRendererName(g_renderer),
                 tried.empty() ? "" : "; the GPU presenter did not start: ", tried.c_str());
  SDL_SetRenderVSync(g_renderer, 0);
  if (SDL_IOStream *pLog = SDL_IOFromFile(PortNativePath(PortLogFile("hyperion_render.log")), "w")) {
    SDL_IOprintf(pLog, "renderer=sdl %s%s\n", SDL_GetRendererName(g_renderer),
                 bGpu ? " (the GPU presenter did not start)" : "");
    SDL_CloseIO(pLog);
  }
}

void PortPresentInit(void *pWnd)
{
  const char *pszMode = SDL_getenv("HYPERION_WINDOW");

  g_hWnd = static_cast<HWND>(pWnd);
  g_hidden = pszMode != NULL && SDL_strcasecmp(pszMode, "hidden") == 0;
  g_background = pszMode != NULL && SDL_strcasecmp(pszMode, "background") == 0;
  g_testMouse = g_background && SDL_getenv("HYPERION_TEST_MOUSE") != NULL &&
                SDL_strcmp(SDL_getenv("HYPERION_TEST_MOUSE"), "1") == 0;
  SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
  // SDL minimizes an exclusive full screen when it loses focus, so a click on another monitor
  // hid the game; it stays up, as the borderless one does
  SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
  double dSetting = 1.0;
  g_confine = !PortSettingNumber("hyperion", "confineMouse", &dSetting) || dSetting != 0.0;
  dSetting = 1.0;
  g_dragScroll = !PortSettingNumber("hyperion", "dragScroll", &dSetting) || dSetting != 0.0;
  if (PortSettingNumber("hyperion", "dragScrollSpeed", &dSetting) && dSetting >= 0.0 && dSetting <= 1.0)
    g_fDragSpeed = static_cast<float>(dSetting);
  dSetting = 0.0;
  PortSettingNumber("hyperion", "mouseSensitivity", &dSetting);
  double dSpeed = 0.5;
  PortSettingNumber("hyperion", "mouseSensitivitySpeed", &dSpeed);
  PortMouseSensitivitySet(dSetting != 0.0, static_cast<float>(dSpeed));
  dSetting = 0.0;
  PortSettingNumber("hyperion", "hardwareCursor", &dSetting);
  PortHwCursorSet(dSetting != 0.0);
  // the chat filter's list loads on a thread of its own, but not for a test's window
  dSetting = 1.0;
  PortSettingNumber("hyperion", "chatFilter", &dSetting);
  if (!g_hidden && !g_background)
    PortChatFilterSet(dSetting != 0.0);
  dSetting = 0.0;
  PortSettingNumber("hyperion", "artFilter", &dSetting);
  PortArtFilterSet(static_cast<int>(dSetting));
  if (!SDL_Init(SDL_INIT_VIDEO))
    PresentFatal("SDL_Init");
  // the window is SDL's, made by InitWindow (port_winmsg.cpp)
  g_window = WinMsgSdlWindow();
  if (g_window == NULL)
    PresentFatal("SDL_CreateWindow");
  PresentNoteDisplays();
  g_palette = SDL_CreatePalette(256);
  if (g_palette == NULL)
    PresentFatal("SDL_CreatePalette");
  // SDL starts a palette white; the game's first frame, drawn before it sets one (init_video's
  // img_update), would show the window white until the title fades in. Black, as DirectDraw's was.
  SDL_Color black[256];
  for (SDL_Color &c : black)
    c = SDL_Color{0, 0, 0, 255};
  SDL_SetPaletteColors(g_palette, black, 0, 256);
  PresentCreate();
  PresentFitView();
  // the game draws its own cursor into the frame; SDL would set the arrow over the client area
  // on WM_SETCURSOR otherwise
  SDL_HideCursor();

  // hidden or in the background the client area is the frame itself, so a position a test
  // harness posts in game pixels ({CLICKx:y}, rt_observer.cpp) maps to itself
  if (g_hidden || g_background) {
    int nW, nH;
    PresentTestGameSize(&nW, &nH);
    SDL_SetWindowSize(g_window, nW, nH);
    if (g_hidden)
      return;
    SDL_SetWindowBordered(g_window, true);
    const SDL_DisplayID test = TestDisplay();
    if (test != 0)
      SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(test), SDL_WINDOWPOS_CENTERED_DISPLAY(test));
    else
      SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    // not focusable, or Windows hands the window the foreground when the one in front closes,
    // as each of a test run's games does; the harness posts its keys, so it needs no focus
    SDL_SetWindowFocusable(g_window, false);
    // Not focusable is WS_EX_NOACTIVATE, which keeps a window off the taskbar; the
    // taskbar's entry is put back so the player can see a test run and close it.
    SetWindowLongPtrW(g_hWnd, GWL_EXSTYLE, GetWindowLongPtrW(g_hWnd, GWL_EXSTYLE) | WS_EX_APPWINDOW);
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
    SDL_ShowWindow(g_window);
    return;
  }
  if (pszMode != NULL && SDL_strcasecmp(pszMode, "windowed") == 0) {
    PresentSetWindowed();
  } else if (pszMode != NULL && SDL_strcasecmp(pszMode, "fullscreen") == 0) {
    PresentSetFullscreen();
  } else {
    int nKind;
    PortDisplayPick pick;
    PortDisplaySaved(&nKind, &pick);
    PresentShow(nKind, &pick);
  }
  SDL_ShowWindow(g_window);
  SDL_SyncWindow(g_window);
  SetForegroundWindow(g_hWnd);
  SetFocus(g_hWnd);
}

void PortPresentShutdown(void)
{
  if (!g_hidden && !g_background)
    ClipCursor(NULL);
  g_caged = false;
  PresentSystemCursor(NULL);
  PortHwCursorFree();
  if (g_texture != NULL)
    SDL_DestroyTexture(g_texture);
  if (g_zoomMap != NULL)
    SDL_DestroyTexture(g_zoomMap);
  if (g_zoomScreen != NULL)
    SDL_DestroyTexture(g_zoomScreen);
  g_zoomMap = g_zoomScreen = NULL;
  if (g_palette != NULL)
    SDL_DestroyPalette(g_palette);
  if (g_renderer != NULL)
    SDL_DestroyRenderer(g_renderer);
  if (g_gpu)
    PortGpuShutdown();
  g_gpu = false;
  // Destroying a wrapped window leaves the game's window itself alone.
  if (g_window != NULL)
    SDL_DestroyWindow(g_window);
  g_texture = NULL;
  g_nTextureWidth = 0;
  g_nTextureHeight = 0;
  g_palette = NULL;
  g_renderer = NULL;
  g_window = NULL;
  SDL_Quit();
}

void PortPresenterReport(char *pszName, unsigned int nCap, ULONG *pnChecked,
                         ULONG *pnMismatched)
{
  if (g_gpu) {
    SDL_strlcpy(pszName, PortGpuDriver(), nCap);
    PortGpuCheckCounts(pnChecked, pnMismatched);
    return;
  }
  SDL_snprintf(pszName, nCap, "sdl %s", g_renderer != NULL ? SDL_GetRendererName(g_renderer) : "none");
  *pnChecked = 0;
  *pnMismatched = 0;
}

int PortScreenIsUp(void)
{
  return g_renderer != NULL || g_gpu;
}

void *PortPresentWindow(void)
{
  return g_window;
}

int PortPresentGpu(void)
{
  return g_gpu ? 1 : 0;
}

void PortPresentRedraw(void)
{
  g_frameDirty = true;
}

int PortScreenLock(void **ppBits, int *pnPitch)
{
  *ppBits = g_screen;
  if (pnPitch != NULL)
    *pnPitch = g_portView.nWidth;
  return 1;
}

int PortScreenUnlock(void)
{
  g_frameDirty = true;
  ++g_nScreenWrites;
  return 1;
}

unsigned int PortScreenWrites(void)
{
  return g_nScreenWrites;
}

// The frame as it is shown: the screen, or, at the scene's last stage, the screen with
// the GPU's copy where the cover says, read back from the GPU. While the GPU alone
// draws the map (HYPERION_GPU_SCENE=gpu) the screen has only the rest.
static const BYTE *PresentShown(void)
{
  if (!g_gpu || PortSceneStage() != SCENE_SCREEN)
    return g_screen;
  if (const SceneFrame *pScene = PortSceneTake())
    if (!PortGpuScene(pScene, g_palette->colors, false))
      PortScreenCoverClear();
  PresentCoverFit();
  const int n = g_portView.nWidth * g_portView.nHeight;
  if (!PortGpuReadShown(g_shown, static_cast<Uint32>(n)))
    return g_screen;
  for (int i = 0; i < n; ++i)
    if (g_cover[i] == 0)
      g_shown[i] = g_screen[i];
  return g_shown;
}

// For the replay suite's observer (tools/replaytest/rt_observer.cpp): the
// screen, as SDrawLockSurface gives the oracle's, for full-frame snapshots; the frame
// as shown, when the GPU alone draws the map. Exported undecorated by Hyperion.exe's
// .def (hyperion/hyperion.def).
extern "C" int __stdcall oc_screen(void **ppBits, int *pnPitch)
{
  *ppBits = const_cast<BYTE *>(PresentShown());
  if (pnPitch != NULL)
    *pnPitch = g_portView.nWidth;
  return 1;
}

// A small picture of the game as it is shown, w by h, three bytes a pixel: the map above 1.17's
// console at w:h, its middle when the rows above the console are too few for the whole width,
// each pixel the average of those it covers (a LAN room's copy of a match, for the console's
// Wall).
bool PortPresentThumb(unsigned char *pRgb, int w, int h)
{
  if (g_palette == NULL || w <= 0 || h <= 0)
    return false;
  const BYTE *pShown = PresentShown();
  const int nPitch = g_portView.nWidth;
  // the console's sides rise above the map's last row, to about three quarters of it (the
  // minimap's panel), and are left out
  const int nMapRows = g_portView.nViewRows > 0 ? g_portView.nViewRows : g_portView.nHeight;
  const int nRows = SDL_min(g_portView.nHeight, nMapRows * 3 / 4);
  const int nH = SDL_min(nRows, nPitch * h / w);
  const int nW = nH * w / h, nLeft = (nPitch - nW) / 2;
  const SDL_Color *pColors = g_palette->colors;
  for (int y = 0; y < h; ++y) {
    const int y0 = y * nH / h, y1 = SDL_max(y0 + 1, (y + 1) * nH / h);
    for (int x = 0; x < w; ++x) {
      const int x0 = nLeft + x * nW / w, x1 = SDL_max(x0 + 1, nLeft + (x + 1) * nW / w);
      unsigned r = 0, g = 0, b = 0, n = 0;
      for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) {
          const SDL_Color &c = pColors[pShown[yy * nPitch + xx]];
          r += c.r;
          g += c.g;
          b += c.b;
          ++n;
        }
      unsigned char *pOut = pRgb + (static_cast<size_t>(y) * w + x) * 3;
      pOut[0] = static_cast<unsigned char>(r / n);
      pOut[1] = static_cast<unsigned char>(g / n);
      pOut[2] = static_cast<unsigned char>(b / n);
    }
  }
  return true;
}

// The screen's size, the view's (port_view.cpp), for the same snapshots: 640x480
// in 1.17, wider in a wide view.
extern "C" int __stdcall oc_screen_size(int *pnWidth, int *pnHeight)
{
  *pnWidth = g_portView.nWidth;
  *pnHeight = g_portView.nHeight;
  return 1;
}

void PortPresentPalette(const void *pEntries, int nFirst, int nCount)
{
  // palentry_t: red, green, blue, flags, one byte each.
  const BYTE *pSrc = static_cast<const BYTE *>(pEntries);
  SDL_Color colors[256];
  int i;

  if (g_palette == NULL || nFirst < 0 || nCount <= 0 || nFirst + nCount > 256)
    return;
  for (i = 0; i < nCount; ++i) {
    colors[i].r = pSrc[i * 4 + 0];
    colors[i].g = pSrc[i * 4 + 1];
    colors[i].b = pSrc[i * 4 + 2];
    colors[i].a = 255;
  }
  SDL_SetPaletteColors(g_palette, colors, nFirst, nCount);
  g_paletteChanged = true;
  g_frameDirty = true;
}

void PortPresentPaletteRGB(unsigned char *pRGB)
{
  SDL_memset(pRGB, 0, 768);
  if (g_palette == NULL)
    return;
  for (int i = 0; i < 256 && i < g_palette->ncolors; ++i) {
    pRGB[3 * i] = g_palette->colors[i].r;
    pRGB[3 * i + 1] = g_palette->colors[i].g;
    pRGB[3 * i + 2] = g_palette->colors[i].b;
  }
}

static double PresentMs(Uint64 nFrom)
{
  return 1000.0 * static_cast<double>(SDL_GetPerformanceCounter() - nFrom) /
         static_cast<double>(SDL_GetPerformanceFrequency());
}

// bForce: the part of a second since the last, as a benchmark run ends (PortPresentPerfFlush).
// The seconds are kept with HYPERION_PERF=1 and while a benchmark is wanted (PortPerfOn's
// rule); hyperion_perf.log starts afresh the first time they are.
static bool PresentPerfOn(void)
{
  if (g_perf < 0) {
    const char *psz = SDL_getenv("HYPERION_PERF");
    g_perf = psz != NULL && psz[0] == '1';
  }
  const bool bOn = g_perf > 0 || PortBenchWanted() || PortPerfWanted() || PortDebugViewShown();
  static bool s_bStarted;
  if (bOn && !s_bStarted) {
    s_bStarted = true;
    g_perfSince = SDL_GetPerformanceCounter();
    g_nPerfFrames = 0;
    SDL_RemovePath(PortNativePath(PortLogFile("hyperion_perf.log")));
  }
  return bOn;
}

// The seconds kept for Debug Tools' live numbers, the last 600, numbered from the first.
struct LiveSecond {
  size_t nIndex;
  double dAt;  // seconds since the sampler started
  PortBenchSecond s;
  double dPresentMs, dSceneMs, dPassesPerS;
  unsigned int nFrame;
  int nSpeed;
  bool bActive;
  bool bPlayback;
};
static std::deque<LiveSecond> g_live;
static size_t g_nLiveCount;
static Uint64 g_liveStart;

static void PresentPerf(bool bForce = false)
{
  if (!PresentPerfOn())
    return;
  const double dMs = PresentMs(g_perfSince);
  if (dMs < 1000.0 && !(bForce && dMs > 0.0 && g_nPerfFrames > 0))
    return;
  double dPassMs;
  unsigned int nPasses;
  PortSceneTimes(&dPassMs, &nPasses);
  char szParts[512];
  PortBenchSecond second = {};
  PortPerfLine(szParts, sizeof szParts, dMs / 1000.0, &second);
  // a benchmark run's second (port_bench.cpp)
  second.dFramesPerS = g_nPerfFrames * 1000.0 / dMs;
  PortBenchAddSecond(second, dMs / 1000.0);
  if (PortPerfWanted() || PortDebugViewShown()) {
    if (g_liveStart == 0)
      g_liveStart = g_perfSince;
    LiveSecond l;
    l.nIndex = g_nLiveCount++;
    l.dAt = static_cast<double>(SDL_GetPerformanceCounter() - g_liveStart) / SDL_GetPerformanceFrequency();
    l.s = second;
    l.dPresentMs = g_nPerfFrames ? g_dPerfPresentMs / g_nPerfFrames : 0.0;
    l.dSceneMs = g_nPerfScenes ? g_dPerfSceneMs / g_nPerfScenes : 0.0;
    l.dPassesPerS = nPasses * 1000.0 / dMs;
    l.nFrame = oc_dwReplayFrame;
    l.nSpeed = static_cast<int>(gOptions);
    l.bActive = sgbWindowActive_g != 0;
    l.bPlayback = gPlayback != 0;
    g_live.push_back(l);
    if (g_live.size() > 600)
      g_live.pop_front();
  }
  static const PortLogger s_log("hyperion_perf.log", PortLogger::SHARED);
  s_log("frames/s=%.1f passes/s=%.1f pass_ms=%.2f gpu_scene_ms=%.2f present_ms=%.2f view=%dx%d renderer=%s scene=%d%s%s",
        g_nPerfFrames * 1000.0 / dMs, nPasses * 1000.0 / dMs, nPasses ? dPassMs / nPasses : 0.0,
        g_nPerfScenes ? g_dPerfSceneMs / g_nPerfScenes : 0.0, g_nPerfFrames ? g_dPerfPresentMs / g_nPerfFrames : 0.0,
        g_portView.nWidth, g_portView.nHeight, g_gpu ? PortGpuDriver() : "sdl", PortSceneStage(),
        PortSceneGpuOnly() ? " gpu-only" : "", szParts);
  g_perfSince = SDL_GetPerformanceCounter();
  g_nPerfFrames = 0;
  g_nPerfScenes = 0;
  g_dPerfSceneMs = 0.0;
  g_dPerfPresentMs = 0.0;
}

int PortPerfLiveSeconds(PortBenchSecond *pOut, double *pdPresentMs, int nMax)
{
  int n = 0;
  const size_t nFrom = g_live.size() > static_cast<size_t>(nMax) ? g_live.size() - nMax : 0;
  for (size_t i = nFrom; i < g_live.size(); ++i)
    pOut[n++] = g_live[i].s;
  if (pdPresentMs != NULL)
    *pdPresentMs = g_live.empty() ? 0.0 : g_live.back().dPresentMs;
  return n;
}

std::string PortPerfSecondsJson(size_t nSince, size_t *pnCount)
{
  *pnCount = g_nLiveCount;
  std::string s = "[";
  char sz[768];
  bool bFirst = true;
  for (const LiveSecond &l : g_live) {
    if (l.nIndex < nSince)
      continue;
    int k = snprintf(sz, sizeof sz,
                     "%s{\"i\":%u,\"at\":%.1f,\"fps\":%.1f,\"gfps\":%.1f,\"units\":%u,\"frame\":%u,"
                     "\"speed\":%d,\"active\":%s,\"replay\":%s,\"present_ms\":%.2f,\"scene_ms\":%.2f,"
                     "\"passes\":%.1f,\"parts\":[",
                     bFirst ? "" : ",", static_cast<unsigned>(l.nIndex), l.dAt, l.s.dFramesPerS, l.s.dGameFramesPerS,
                     l.s.nUnits, l.nFrame, l.nSpeed, l.bActive ? "true" : "false", l.bPlayback ? "true" : "false",
                     l.dPresentMs, l.dSceneMs, l.dPassesPerS);
    for (int i = 0; i < PERF_PARTS && k > 0 && static_cast<size_t>(k) < sizeof sz; ++i)
      k += snprintf(sz + k, sizeof sz - k, "%s%.3f", i ? "," : "", l.s.adPartMs[i]);
    s += sz;
    s += "]}";
    bFirst = false;
  }
  return s + "]";
}

void PortPresentPerfFlush(bool bKeep)
{
  if (!PresentPerfOn())
    return;
  if (bKeep) {
    PresentPerf(true);
    return;
  }
  char sz[512];
  PortPerfLine(sz, sizeof sz, 1.0, NULL);
  g_perfSince = SDL_GetPerformanceCounter();
  g_nPerfFrames = 0;
  g_nPerfScenes = 0;
  g_dPerfSceneMs = 0.0;
  g_dPerfPresentMs = 0.0;
}

// Settings, "Frame rate" ("frameRate"): 0 as 1.17, a frame drawn only when the game's loop
// has something new (a game frame, a scroll step, the mouse), about 80 a second; 1 sixty a
// second; 2 the display's refresh, the default; 3 144; 4 240; 5 no limit. Past 0 the loop
// draws a frame whenever one is due (PortFrameDue), so what moves between the game's steps,
// the camera's glide and eases, moves at the frame rate.
static int g_nFrameRate = -1;
static Uint64 g_frameDueAt;
// PortPresentHold: a frame asked for now waits for the next one due
static bool g_presentHeld;
// PortPresentFrontKeep: no game's frame shown before this tick, while the front end's last stays up
static Uint64 g_frontKeepUntil;

void PortPresentHold(bool bHold)
{
  g_presentHeld = bHold;
}

int PortFrameRate(void)
{
  if (g_nFrameRate < 0) {
    double d = 2.0;
    PortSettingNumber("hyperion", "frameRate", &d);
    g_nFrameRate = d >= 0.0 && d <= 5.0 ? static_cast<int>(d) : 2;
  }
  return g_nFrameRate;
}

void PortArtFilterSet(int nStep)
{
  g_nArtFilter = SDL_clamp(nStep, 0, 3);
  PortGpuArtFilterSet(g_nArtFilter);
  if (g_texture != NULL)
    SDL_SetTextureScaleMode(g_texture, g_nArtFilter == 0 ? SDL_SCALEMODE_PIXELART : SDL_SCALEMODE_LINEAR);
  g_frameDirty = true;
}

void PortFrameRateSet(int nRate)
{
  g_nFrameRate = nRate >= 0 && nRate <= 5 ? nRate : 2;
  g_frameDueAt = 0;
}

// The display's rate, read again once a second, as the window may move to another: SDL takes
// about 40 us to say which display a window is on, too much to ask on every pass of the loop.
static double DisplayHz(void)
{
  static double s_dHz = 60.0;
  static Uint64 s_nReadAt;
  const Uint64 nNow = SDL_GetTicks() + 1;
  if (s_nReadAt == 0 || nNow - s_nReadAt >= 1000) {
    s_nReadAt = nNow;
    const SDL_DisplayMode *pMode =
        g_window != NULL ? SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(g_window)) : NULL;
    s_dHz = pMode != NULL && pMode->refresh_rate > 20.0f ? pMode->refresh_rate : 60.0;
  }
  return s_dHz;
}

// The frames a second the setting asks for, 0 for 1.17's way or no limit. With Vertical sync
// on, no more than the display shows: a frame past its refresh is never seen, and handing it
// over queued the next behind it, a refresh later to the screen (measured on a 60 Hz
// display: 39 ms from hand-over to the screen at No limit, 22 at the display's rate).
static double FrameRateHz(void)
{
  double dHz;
  switch (PortFrameRate()) {
    case 0: return 0.0;
    case 1: dHz = 60.0; break;
    case 2: return DisplayHz();
    case 3: dHz = 144.0; break;
    case 4: dHz = 240.0; break;
    default: dHz = 0.0; break;
  }
  if (g_gpu && PortGpuVsyncOn() && (dHz == 0.0 || dHz > DisplayHz()))
    return DisplayHz();
  return dHz;
}

bool PortFrameDue(void)
{
  const int nRate = PortFrameRate();
  if (nRate == 0)
    return false;
  const double dHz = FrameRateHz();
  if (dHz == 0.0)
    return true;
  const Uint64 nNow = SDL_GetPerformanceCounter();
  if (g_frameDueAt != 0 && nNow < g_frameDueAt)
    return false;
  const Uint64 nPeriod = static_cast<Uint64>(SDL_GetPerformanceFrequency() / dHz);
  // the next one a period on from this one's due time, so the frames keep an even beat; a
  // frame late by more than a period starts the beat again
  g_frameDueAt = g_frameDueAt != 0 && nNow - g_frameDueAt < nPeriod ? g_frameDueAt + nPeriod : nNow + nPeriod;
  return true;
}

double PortFrameDueIn(void)
{
  const int nRate = PortFrameRate();
  if (nRate == 0)
    return -1.0;
  const Uint64 nNow = SDL_GetPerformanceCounter();
  if (FrameRateHz() == 0.0 || g_frameDueAt == 0 || nNow >= g_frameDueAt)
    return 0.0;
  return static_cast<double>(g_frameDueAt - nNow) / static_cast<double>(SDL_GetPerformanceFrequency());
}

static void PresentGameToClient(float fX, float fY, float *pfX, float *pfY);

// Settings' "Hardware cursor": over a game in this window the system shows the game's cursor,
// and the game stops drawing it. Not for a browser's player, whose stream carries the frame as
// drawn; the arrow off the game's picture goes before it.
static void PresentHwCursor(void)
{
  if (g_arrowShown || g_frontUp)
    return;
  SDL_Cursor *pCursor = NULL;
  if (PortHwCursorOn() && !g_remoteInput && !LanStreamWants()) {
    float fX0, fY0, fX1, fY1;
    PresentGameToClient(0.0f, 0.0f, &fX0, &fY0);
    PresentGameToClient(100.0f, 0.0f, &fX1, &fY1);
    pCursor = PortHwCursorNow((fX1 - fX0) / 100.0f, g_palette->colors);
  }
  PresentSystemCursor(pCursor);
}

// SDL's renderer with the camera zoomed out: the software's render view
// (PortViewSoftShown), scaled to the camera with linear filtering, under the screen, which is
// clear where its cover says it shows the map. False when there is none to show, and the
// screen alone is the frame.
static bool PresentSoftZoom(void)
{
  unsigned int nPass = 0;
  const unsigned char *pMap = PortViewSoftShown(&nPass);
  if (pMap == NULL)
    return false;
  const int nMapW = g_portRender.nWidth;
  const int nMapH = g_portRender.nRows;
  const int nW = g_portView.nWidth;
  const int nH = g_portView.nHeight;
  Uint32 colors[256];
  for (int i = 0; i < 256; ++i) {
    const SDL_Color &c = g_palette->colors[i];
    colors[i] = 0xFF000000u | (static_cast<Uint32>(c.r) << 16) | (static_cast<Uint32>(c.g) << 8) | c.b;
  }
  const bool bColors = SDL_memcmp(colors, g_zoomColors, sizeof colors) != 0;
  if (bColors)
    SDL_memcpy(g_zoomColors, colors, sizeof colors);
  if (g_zoomMap == NULL || nMapW > g_nZoomMapW || nMapH > g_nZoomMapH) {
    if (g_zoomMap != NULL)
      SDL_DestroyTexture(g_zoomMap);
    g_nZoomMapW = SDL_max(nMapW, g_nZoomMapW);
    g_nZoomMapH = SDL_max(nMapH, g_nZoomMapH);
    g_zoomMap = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, g_nZoomMapW,
                                  g_nZoomMapH);
    if (g_zoomMap == NULL) {
      g_nZoomMapW = g_nZoomMapH = 0;
      return false;
    }
    SDL_SetTextureBlendMode(g_zoomMap, SDL_BLENDMODE_NONE);
    g_nZoomPass = nPass + 1;
  }
  // a zoom out shrinks the map, which linear filtering averages; a replay's closer look keeps
  // the screen's filter
  const double z = g_portRender.fZoom;
  SDL_SetTextureScaleMode(g_zoomMap, z >= 1.0 || g_nArtFilter != 0 ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_PIXELART);
  if (nPass != g_nZoomPass || bColors) {
    const SDL_Rect rc = {0, 0, nMapW, nMapH};
    void *pv = NULL;
    int nPitch = 0;
    if (!SDL_LockTexture(g_zoomMap, &rc, &pv, &nPitch))
      return false;
    for (int y = 0; y < nMapH; ++y) {
      Uint32 *pOut = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(pv) + static_cast<size_t>(y) * nPitch);
      const unsigned char *pIn = pMap + static_cast<size_t>(y) * nMapW;
      for (int x = 0; x < nMapW; ++x)
        pOut[x] = colors[pIn[x]];
    }
    SDL_UnlockTexture(g_zoomMap);
    g_nZoomPass = nPass;
  }
  if (g_zoomScreen == NULL || g_nZoomScreenW != nW || g_nZoomScreenH != nH) {
    if (g_zoomScreen != NULL)
      SDL_DestroyTexture(g_zoomScreen);
    g_zoomScreen = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, nW, nH);
    if (g_zoomScreen == NULL) {
      g_nZoomScreenW = g_nZoomScreenH = 0;
      return false;
    }
    g_nZoomScreenW = nW;
    g_nZoomScreenH = nH;
    SDL_SetTextureBlendMode(g_zoomScreen, SDL_BLENDMODE_BLEND);
  }
  SDL_SetTextureScaleMode(g_zoomScreen, g_nArtFilter == 0 ? SDL_SCALEMODE_PIXELART : SDL_SCALEMODE_LINEAR);
  PresentCoverFit();
  void *pv = NULL;
  int nPitch = 0;
  if (!SDL_LockTexture(g_zoomScreen, NULL, &pv, &nPitch))
    return false;
  for (int y = 0; y < nH; ++y) {
    Uint32 *pOut = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(pv) + static_cast<size_t>(y) * nPitch);
    const size_t nRow = static_cast<size_t>(y) * nW;
    for (int x = 0; x < nW; ++x)
      pOut[x] = g_cover[nRow + x] != 0 ? 0u : colors[g_screen[nRow + x]];
  }
  SDL_UnlockTexture(g_zoomScreen);

  // the render view's pixel x shows the map's nX + x, which the screen shows at its pixel
  // (nX + x - fCamX) / z
  const SDL_FRect src = {0.0f, 0.0f, static_cast<float>(nMapW), static_cast<float>(nMapH)};
  const SDL_FRect dst = {static_cast<float>((g_portRender.nX - g_portRender.fCamX) / z),
                         static_cast<float>((g_portRender.nY - g_portRender.fCamY) / z),
                         static_cast<float>(nMapW / z), static_cast<float>(nMapH / z)};
  const SDL_Rect clip = {0, 0, nW, g_portView.nMapRows};
  SDL_SetRenderClipRect(g_renderer, &clip);
  SDL_RenderTexture(g_renderer, g_zoomMap, &src, &dst);
  SDL_SetRenderClipRect(g_renderer, NULL);
  SDL_RenderTexture(g_renderer, g_zoomScreen, NULL, NULL);
  return true;
}

void PortPresentFrame(void)
{
  RECT client;

#if defined(__EMSCRIPTEN__)
  // in a browser, the page shows the last frame and takes the player's input in its turn
  PortWebTurn();
#endif
  // The live channel's messages, between the game's frames (port_control.cpp).
  PortControlPoll();
  // The AI lab's farm, which plays on while the lab shows a game (port_labfarm.cpp).
  PortLabFarmPump();
  // What the player does, to Discord (port_discord.cpp).
  PortDiscordPump();
  // The LAN room this copy is in, on 1.17's own screens too (port_lanclient.cpp).
  LanClientPump();
  // a seat in a browser's tab watching for its page: the match the room says, to its end (W3)
  if (PortCommandOptions()->szLanTab[0])
    LanWatchFollowPump();
  // a browser player's seat on the host: its window is the player's game, active for them (1.17
  // scrolls and cycles colours only in the active window), and the frame as shown goes to them
  if (g_remoteInput)
    sgbWindowActive_g = 1;
  if (g_palette != NULL && LanStreamWants())
    LanStreamFrame(PresentShown(), g_portView.nWidth, g_portView.nHeight, g_portView.nWidth,
                   reinterpret_cast<const unsigned char *>(g_palette->colors));
  if (g_renderer == NULL && !g_gpu)
    return;
  // held (the colour cycle at a frame rate Settings chose): what changed shows with the next
  // frame due, which the game's loop draws within a period
  if (g_presentHeld)
    return;
  // between two of the front end's screens: 1.17's, black, pass unseen
  if (g_frontKeepUntil != 0) {
    if (SDL_GetTicks() < g_frontKeepUntil)
      return;
    g_frontKeepUntil = 0;
  }
  // A snapshot asked for is taken of a frame drawn for it, paused or not.
  const bool bSnap = g_gpu && PortGpuSnapPending();
  if (bSnap)
    g_frameDirty = true;
  PortStartupFrame();
  PresentPerf();
  PresentFitView();
  // the frame description's map, drawn on the GPU (port_scene.cpp), hidden windows too, and at
  // the last stage shown where the cover says; scene and present share one command buffer, so
  // what a frame recorded and did not present (hidden, not due) is submitted however this ends
  struct FrameEnd {
    ~FrameEnd() {
      if (g_gpu)
        PortGpuFrameEnd();
    }
  } frameEnd;
  const Uint8 *pCover = NULL;
  // what the frame log says of this frame, when one is written (port_framelog.cpp)
  PortFrameLogFrame logged = {};
  const bool bLog = PortFrameLogOn() != 0;
  if (g_gpu) {
    if (const SceneFrame *pScene = PortSceneTake()) {
      const Uint64 nFrom = SDL_GetPerformanceCounter();
      if (!PortGpuScene(pScene, g_palette->colors, g_gpuCheck))
        PortScreenCoverClear();
      logged.dSceneMs = PresentMs(nFrom);
      g_dPerfSceneMs += logged.dSceneMs;
      ++g_nPerfScenes;
      if (bLog) {
        logged.nPrims = pScene->nPrims;
        if (pScene->pPrimArt != NULL)
          for (unsigned int i = 0; i < pScene->nPrims; ++i)
            logged.nImagePrims += pScene->pPrimArt[i] != SCENE_NONE;
        for (unsigned int i = 0; i < pScene->nPrims; ++i)
          logged.nDistorts += (pScene->pPrims[i].nKind & 0xFF) == SCENE_PRIM_DISTORT;
        logged.nLayerPx = static_cast<unsigned int>(pScene->nLayerW * pScene->nLayerRows);
        logged.nHdDraws = PortGpuHdDrawCount();
      }
    }
    if (PortSceneStage() == SCENE_SCREEN) {
      PresentCoverFit();
      pCover = g_cover;
      if (g_coverChanged) {
        PortGpuCoverChanged();
        g_coverChanged = false;
      }
    }
  }
  if (g_hidden) {
    // Never shown; with the check on, or a snapshot asked for, the GPU presenter still draws.
    if (g_gpu && (g_gpuCheck || bSnap) && g_frameDirty) {
      g_frameDirty = false;
      PortGpuFrame(g_screen, pCover, g_portView.nWidth, g_portView.nHeight, g_palette->colors,
                   g_paletteChanged, false, g_gpuCheck);
      g_paletteChanged = false;
    }
    return;
  }
  if (!g_background)
    PresentHwCursor();
  // A resize or a switch to full screen moves the frame inside the window, so a
  // cage in force is placed again and the frame drawn at its new size.
  GetClientRect(g_hWnd, &client);
  if (SDL_memcmp(&client, &g_lastClient, sizeof client) != 0) {
    g_lastClient = client;
    g_frameDirty = true;
    PresentApplyCage();
  }
  // and when an overlay round the game comes or goes, which widens the cage to the window
  static bool s_bOverlay;
  const bool bOverlay = PortOverlayHas(PORT_OVERLAY_FREE_CURSOR);
  if (bOverlay != s_bOverlay) {
    s_bOverlay = bOverlay;
    PresentApplyCage();
  }
  if (!g_frameDirty)
    return;
  g_frameDirty = false;
  const Uint64 nPresentFrom = SDL_GetPerformanceCounter();
  ++g_nPerfFrames;
  PortBenchShown();
  PortOverlayFrameShown();
  if (g_gpu) {
    // A frame the swapchain had no image for waits for the next present.
    if (!PortGpuFrame(g_screen, pCover, g_portView.nWidth, g_portView.nHeight, g_palette->colors,
                      g_paletteChanged, true, g_gpuCheck))
      g_frameDirty = true;
    else
      PortClockShown();  // Debug Tools' game clock counts the frames that reached the window
    logged.dPresentMs = PresentMs(nPresentFrom);
    g_dPerfPresentMs += logged.dPresentMs;
    g_paletteChanged = false;
    if (bLog) {
      logged.dPassMs = PortSceneLastPassMs();
      logged.dGpuWaitMs = PortGpuWaitMs();
      PortFrameLogShown(logged);
    }
    return;
  }
  SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
  SDL_RenderClear(g_renderer);
  if (!PresentSoftZoom()) {
    SDL_UpdateTexture(g_texture, NULL, g_screen, g_portView.nWidth);
    SDL_RenderTexture(g_renderer, g_texture, NULL, NULL);
  }
  SDL_RenderPresent(g_renderer);
  PortClockShown();
  logged.dPresentMs = PresentMs(nPresentFrom);
  g_dPerfPresentMs += logged.dPresentMs;
  if (bLog) {
    logged.dPassMs = PortSceneLastPassMs();
    PortFrameLogShown(logged);
  }
}

// The front end (port_front.cpp) presents itself; see port_present.h.
static bool g_frontSized;
static void PresentApplyCage(void);

void PortPresentClientSize(int *pnWidth, int *pnHeight)
{
  RECT client;
  GetClientRect(g_hWnd, &client);
  *pnWidth = client.right - client.left;
  *pnHeight = client.bottom - client.top;
}

void PortPresentFrontBegin(int *pnWidth, int *pnHeight, float *pfRefresh)
{
  g_cursorFree = false;  // a game's Ctrl+Alt ends with it
  if (g_arrowShown)
    PresentArrow(false);
  PresentSystemCursor(NULL);
  const char *pszSize = SDL_getenv("HYPERION_FRONT_SIZE");
  int nW = 0;
  int nH = 0;
  if ((g_hidden || g_background) && pszSize != NULL && SDL_sscanf(pszSize, "%dx%d", &nW, &nH) == 2 && nW > 0 &&
      nH > 0) {
    SDL_SetWindowSize(g_window, nW, nH);
    SDL_SyncWindow(g_window);
    g_frontSized = true;
  }
  if (!g_hidden && !g_background) {
    // the game draws its own cursor into its frame; the front end has the system's
    SDL_ShowCursor();
    if (ShowCursor(TRUE) > 0)
      ShowCursor(FALSE);
  }
  g_frontUp = true;
  g_frontKeepUntil = 0;
  PresentApplyCage();
  const SDL_DisplayMode *pMode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(g_window));
  *pfRefresh = pMode != NULL ? pMode->refresh_rate : 0.0f;
  PortPresentClientSize(pnWidth, pnHeight);
}

void PortPresentFrontEnd(void)
{
  if (g_frontSized) {
    int nW, nH;
    PresentTestGameSize(&nW, &nH);
    SDL_SetWindowSize(g_window, nW, nH);
    SDL_SyncWindow(g_window);
    g_frontSized = false;
  }
  if (!g_hidden && !g_background) {
    SDL_HideCursor();
    while (ShowCursor(FALSE) >= 0) {
    }
  }
  g_frontUp = false;
  PresentApplyCage();
  SDL_memset(&g_lastClient, 0, sizeof g_lastClient);
  g_frameDirty = true;
}

void PortPresentFrontKeep(void)
{
  g_frontKeepUntil = SDL_GetTicks() + 1500;
}

void PortToggleFullscreen(void)
{
  if (g_window == NULL || g_hidden || g_background)
    return;
  // a window as Settings saved one, or the largest whole multiple of the frame; full screen
  // as saved, or borderless; on the display the window is on
  int nSavedKind;
  PortDisplayPick saved, pick;
  PortDisplaySaved(&nSavedKind, &saved);
  const SDL_DisplayID id = SDL_GetDisplayForWindow(g_window);
  PickFor(id, DisplayIndex(id), &pick);
  if (g_fullscreen) {
    pick.nWidth = nSavedKind == PORT_DISPLAY_WINDOW ? saved.nWidth : 0;
    pick.nHeight = nSavedKind == PORT_DISPLAY_WINDOW ? saved.nHeight : 0;
    PresentShow(PORT_DISPLAY_WINDOW, &pick);
  } else {
    pick.nWidth = saved.nWidth;
    pick.nHeight = saved.nHeight;
    PresentShow(nSavedKind == PORT_DISPLAY_FULLSCREEN ? PORT_DISPLAY_FULLSCREEN : PORT_DISPLAY_BORDERLESS, &pick);
  }
  SDL_SyncWindow(g_window);
  g_frameDirty = true;
  PortPresentFrame();
}

int PortDisplayViewWidth(void)
{
  SDL_Rect bounds;

  if (g_window == NULL || !SDL_GetDisplayBounds(SDL_GetDisplayForWindow(g_window), &bounds) ||
      bounds.h <= 0)
    return 0;
  return bounds.w * g_portView.nHeight / bounds.h;
}

int PortScreenshot(const char *pszFile)
{
  SDL_Surface *pSurface;
  bool bSaved;
  char szPath[MAX_PATH];

  if (g_palette == NULL)
    return 0;
  // A bare name goes in the player's data folder, with the rest of their data
  // (port_paths.cpp).
  if (pszFile[0] != '\\' && pszFile[0] != '/' && (pszFile[0] == '\0' || pszFile[1] != ':')) {
    SDL_snprintf(szPath, sizeof szPath, "%s%s", PortDataFolder(), pszFile);
    pszFile = szPath;
  }
  pSurface = SDL_CreateSurfaceFrom(g_portView.nWidth, g_portView.nHeight, SDL_PIXELFORMAT_INDEX8,
                                   const_cast<BYTE *>(PresentShown()), g_portView.nWidth);
  if (pSurface == NULL)
    return 0;
  SDL_SetSurfacePalette(pSurface, g_palette);
  bSaved = SDL_SavePNG(pSurface, PortNativePath(pszFile));
  SDL_DestroySurface(pSurface);
  return bSaved ? 1 : 0;
}

// ---------------------------------------------------------------------------
// The mouse. Window client pixels become game pixels through the presenter's
// letterboxing; before the presenter exists the two are the same.
//
// A player's window reads and moves the system cursor, as the oracle did. A
// background or hidden window leaves the user's mouse alone: the game's
// pointer is then the last position a mouse message or mou_set_pos gave.

// The window's pixels to a point: 2 for a Mac's window on a Retina screen (port_winmsg.cpp), else
// 1. SDL's renderer takes a window position in points, and the client positions here are pixels.
static float PresentDensity(void)
{
  const float fDensity = g_window != NULL ? SDL_GetWindowPixelDensity(g_window) : 1.0f;
  return fDensity > 0.0f ? fDensity : 1.0f;
}

// The GPU presenter's letterbox in client pixels, as port_gpu.cpp places it in the
// swapchain, which is the client area's size.
static bool PresentGpuBox(SDL_FRect *pBox)
{
  RECT client;
  if (g_hWnd == NULL || !GetClientRect(g_hWnd, &client) || client.right <= 0 || client.bottom <= 0)
    return false;
  PortGpuGameBox(static_cast<float>(client.right), static_cast<float>(client.bottom),
                 static_cast<float>(g_portView.nWidth), static_cast<float>(g_portView.nHeight), pBox);
  return true;
}

// The middle of 1.17's screen, where the game starts.
static int g_nPointerX = 320;
static int g_nPointerY = 240;

static void PresentClientToGame(float fX, float fY, int *pnX, int *pnY)
{
  float fGameX = fX;
  float fGameY = fY;
  int nX;
  int nY;

  if (g_remoteInput) {
    // a streamed seat's browser says game pixels
  } else if (g_renderer != NULL) {
    const float fDensity = PresentDensity();
    SDL_RenderCoordinatesFromWindow(g_renderer, fX / fDensity, fY / fDensity, &fGameX, &fGameY);
  } else if (g_gpu) {
    SDL_FRect box;
    if (PresentGpuBox(&box)) {
      fGameX = (fX - box.x) * g_portView.nWidth / box.w;
      fGameY = (fY - box.y) * g_portView.nHeight / box.h;
    }
  }
  nX = static_cast<int>(SDL_floorf(fGameX));
  nY = static_cast<int>(SDL_floorf(fGameY));
  *pnX = nX < 0 ? 0 : nX >= g_portView.nWidth ? g_portView.nWidth - 1 : nX;
  *pnY = nY < 0 ? 0 : nY >= g_portView.nHeight ? g_portView.nHeight - 1 : nY;
}

// A game position as a client position, in client pixels as floats.
static void PresentGameToClient(float fX, float fY, float *pfX, float *pfY)
{
  *pfX = fX;
  *pfY = fY;
  if (g_renderer != NULL) {
    const float fDensity = PresentDensity();
    SDL_RenderCoordinatesToWindow(g_renderer, fX, fY, pfX, pfY);
    *pfX *= fDensity;
    *pfY *= fDensity;
  } else if (g_gpu) {
    SDL_FRect box;
    if (PresentGpuBox(&box)) {
      *pfX = box.x + fX * box.w / g_portView.nWidth;
      *pfY = box.y + fY * box.h / g_portView.nHeight;
    }
  }
}

void PortPresentTextArea(int nLeft, int nTop, int nRight, int nBottom, int nCaret)
{
  static SDL_Rect s_last;
  static int s_nLastCursor = -1;
  if (g_window == NULL)
    return;
  float fLeft, fTop, fRight, fBottom, fCaret, fUnused;
  PresentGameToClient(static_cast<float>(nLeft), static_cast<float>(nTop), &fLeft, &fTop);
  PresentGameToClient(static_cast<float>(nRight + 1), static_cast<float>(nBottom + 1), &fRight, &fBottom);
  PresentGameToClient(static_cast<float>(nLeft + nCaret), static_cast<float>(nTop), &fCaret, &fUnused);
  // client pixels to the window's points, as SDL takes them
  const float fDensity = PresentDensity();
  const SDL_Rect r = {static_cast<int>(fLeft / fDensity), static_cast<int>(fTop / fDensity),
                      static_cast<int>((fRight - fLeft) / fDensity), static_cast<int>((fBottom - fTop) / fDensity)};
  const int nCursor = static_cast<int>((fCaret - fLeft) / fDensity);
  if (SDL_memcmp(&r, &s_last, sizeof r) == 0 && nCursor == s_nLastCursor)
    return;
  s_last = r;
  s_nLastCursor = nCursor;
  SDL_SetTextInputArea(g_window, &r, nCursor);
}

void PortPresentRemoteInput(bool bOn)
{
  g_remoteInput = bOn;
}

bool PortPresentRemote(void)
{
  return g_remoteInput;
}

int PortMouseIgnored(UINT_PTR *pwParam)
{
  if ((*pwParam & PORT_TEST_MOUSE) != 0) {
    *pwParam &= ~static_cast<UINT_PTR>(PORT_TEST_MOUSE);
    if (g_testMouse || g_remoteInput)
      return 0;
  }
  return g_hidden || g_background;
}

// Whether a client point is off the game's picture: over a layer's panels round it or over it
// (Debug Tools', a benchmark run's, the trainer's), or anywhere off the picture while Ctrl+Alt
// has let the cursor go.
static bool PresentOffGame(float fX, float fY)
{
  if (PortOverlayOutside(fX, fY))
    return true;
  if (!g_cursorFree)
    return false;
  float fLeft, fTop, fRight, fBottom;
  PresentGameToClient(0.0f, 0.0f, &fLeft, &fTop);
  PresentGameToClient(static_cast<float>(g_portView.nWidth), static_cast<float>(g_portView.nHeight), &fRight,
                      &fBottom);
  return fX < fLeft || fY < fTop || fX >= fRight || fY >= fBottom;
}

// The pointer off the game's picture: where the game last had it, a few pixels in from the
// edges, so that leaving across an edge does not keep the view scrolling.
static void PresentHeldPointer(int *pnX, int *pnY)
{
  const int nIn = 4;
  g_nPointerX = SDL_clamp(g_nPointerX, nIn, g_portView.nWidth - 1 - nIn);
  g_nPointerY = SDL_clamp(g_nPointerY, nIn, g_portView.nHeight - 1 - nIn);
  *pnX = g_nPointerX;
  *pnY = g_nPointerY;
}

LONG PortMouseToGame(LONG lParam)
{
  // a release off the game's picture ends what the game saw pressed, at the pointer's last
  // place in the game, so it does not move it to the window's edge
  if (PresentOffGame(static_cast<float>(static_cast<short>(LOWORD(lParam))),
                     static_cast<float>(static_cast<short>(HIWORD(lParam))))) {
    int nX, nY;
    PresentHeldPointer(&nX, &nY);
    return MAKELONG(nX, nY);
  }
  PresentClientToGame(static_cast<float>(static_cast<short>(LOWORD(lParam))),
                      static_cast<float>(static_cast<short>(HIWORD(lParam))), &g_nPointerX,
                      &g_nPointerY);
  return MAKELONG(g_nPointerX, g_nPointerY);
}

void PortCursorGet(int *pnX, int *pnY)
{
  POINT pt;

  if (g_hidden || g_background || g_hWnd == NULL || !GetCursorPos(&pt)) {
    *pnX = g_nPointerX;
    *pnY = g_nPointerY;
    return;
  }
#if defined(__EMSCRIPTEN__)
  // in a browser the pointer is the page's while it is off the game's canvas, and before it
  // has moved at all SDL has it at (0,0), an edge that would scroll the map
  static bool s_bMoved;
  s_bMoved = s_bMoved || pt.x != 0 || pt.y != 0;
  if (!s_bMoved || SDL_GetMouseFocus() == NULL) {
    PresentHeldPointer(pnX, pnY);
    return;
  }
#endif
  ScreenToClient(g_hWnd, &pt);
  // off the game's picture (round Debug Tools' overlay's window, or anywhere once Ctrl+Alt has
  // let the cursor go) the game keeps the pointer where it left it, so nothing scrolls the map;
  // and a few pixels in while a layer drags a window of its own
  if (g_holdScroll || PresentOffGame(static_cast<float>(pt.x), static_cast<float>(pt.y))) {
    PresentHeldPointer(pnX, pnY);
    return;
  }
  PresentClientToGame(static_cast<float>(pt.x), static_cast<float>(pt.y), pnX, pnY);
}

// The system cursor goes to the client pixel under the game pixel's centre,
// which maps back to the same game pixel at any scale.
void PortCursorSet(int nX, int nY)
{
  POINT pt;
  float fX;
  float fY;

  g_nPointerX = nX;
  g_nPointerY = nY;
  if (g_hidden || g_background || g_hWnd == NULL)
    return;
  g_sensAt = false;
  PresentGameToClient(nX + 0.5f, nY + 0.5f, &fX, &fY);
  pt.x = static_cast<LONG>(SDL_floorf(fX));
  pt.y = static_cast<LONG>(SDL_floorf(fY));
  ClientToScreen(g_hWnd, &pt);
  SetCursorPos(pt.x, pt.y);
}

// The cage the game asked for, in screen coordinates. The cursor is only held
// while the window is in front; a hidden or background window leaves the
// cursor to whatever else holds it.
static void PresentApplyCage(void)
{
  RECT rc;
  POINT ptTopLeft;
  POINT ptBottomRight;
  float fX;
  float fY;

  if (g_hidden || g_background)
    return;
  g_held = false;
  g_sensAt = false;  // a new cage may move the pointer into it
  if (!g_caged || g_frontUp || g_cursorFree || g_hWnd == NULL || GetForegroundWindow() != g_hWnd) {
    ClipCursor(NULL);
    return;
  }
  if (!g_confine && g_cageGame.left <= 0 && g_cageGame.top <= 0 && g_cageGame.right >= g_portView.nWidth &&
      g_cageGame.bottom >= g_portView.nHeight) {
    ClipCursor(NULL);
    return;
  }
  g_held = true;
  // With an overlay round the game (Debug Tools' window, a benchmark run's), a cage of the whole
  // view is the whole window, so the pointer reaches the overlay; a drag's cage stays the game's.
  if (PortOverlayHas(PORT_OVERLAY_FREE_CURSOR) && g_cageGame.left <= 0 && g_cageGame.top <= 0 &&
      g_cageGame.right >= g_portView.nWidth && g_cageGame.bottom >= g_portView.nHeight) {
    GetClientRect(g_hWnd, &rc);
    ptTopLeft.x = rc.left;
    ptTopLeft.y = rc.top;
    ptBottomRight.x = rc.right;
    ptBottomRight.y = rc.bottom;
    ClientToScreen(g_hWnd, &ptTopLeft);
    ClientToScreen(g_hWnd, &ptBottomRight);
    SetRect(&rc, ptTopLeft.x, ptTopLeft.y, ptBottomRight.x, ptBottomRight.y);
    ClipCursor(&rc);
    return;
  }
  // The cage's edges fall between pixels, so they round to the nearest.
  PresentGameToClient(static_cast<float>(g_cageGame.left), static_cast<float>(g_cageGame.top), &fX,
                      &fY);
  ptTopLeft.x = static_cast<LONG>(SDL_floorf(fX + 0.5f));
  ptTopLeft.y = static_cast<LONG>(SDL_floorf(fY + 0.5f));
  PresentGameToClient(static_cast<float>(g_cageGame.right), static_cast<float>(g_cageGame.bottom),
                      &fX, &fY);
  ptBottomRight.x = static_cast<LONG>(SDL_floorf(fX + 0.5f));
  ptBottomRight.y = static_cast<LONG>(SDL_floorf(fY + 0.5f));
  ClientToScreen(g_hWnd, &ptTopLeft);
  ClientToScreen(g_hWnd, &ptBottomRight);
  SetRect(&rc, ptTopLeft.x, ptTopLeft.y, ptBottomRight.x, ptBottomRight.y);
  ClipCursor(&rc);
}

void PortClipCursor(const struct tagRECT *prcGame)
{
  g_caged = prcGame != NULL;
  if (prcGame != NULL)
    g_cageGame = *prcGame;
  PresentApplyCage();
}

void PortConfineSet(bool bConfine)
{
  g_confine = bConfine;
  PresentApplyCage();
}

void PortCursorFreeSet(bool bFree)
{
  g_cursorFree = bFree;
  PresentApplyCage();
}

bool PortCursorFree(void)
{
  return g_cursorFree;
}

void PortCursorOffGame(LONG lParam)
{
  const bool bOff = !g_frontUp && PresentOffGame(static_cast<float>(static_cast<short>(LOWORD(lParam))),
                                                 static_cast<float>(static_cast<short>(HIWORD(lParam))));
  if (bOff != g_arrowShown)
    PresentArrow(bOff);
}

void PortCursorRecheck(void)
{
  POINT pt;
  if (g_hidden || g_background || g_hWnd == NULL || !GetCursorPos(&pt))
    return;
  ScreenToClient(g_hWnd, &pt);
  PortCursorOffGame(MAKELONG(pt.x, pt.y));
}

bool PortCursorOverLayer(int nX, int nY)
{
  float fX, fY;
  PresentGameToClient(nX + 0.5f, nY + 0.5f, &fX, &fY);
  return PortOverlayConsoleAt(fX, fY);
}

void PortCursorHoldScroll(bool bHold)
{
  g_holdScroll = bHold;
}

bool PortCursorTakeBack(unsigned uMsg, LONG lParam)
{
  static unsigned s_uUp;  // the release of the press that took it back, which the game never saw
  if (s_uUp != 0 && uMsg == s_uUp) {
    s_uUp = 0;
    return true;
  }
  if (!g_cursorFree || (uMsg != WM_LBUTTONDOWN && uMsg != WM_RBUTTONDOWN && uMsg != WM_MBUTTONDOWN))
    return false;
  if (PresentOffGame(static_cast<float>(static_cast<short>(LOWORD(lParam))),
                     static_cast<float>(static_cast<short>(HIWORD(lParam)))))
    return false;
  s_uUp = uMsg + 1;  // WM_LBUTTONUP and the others follow their presses
  PortCursorFreeSet(false);
  return true;
}

void PortDragScrollSet(bool bOn, float fSpeed)
{
  g_dragScroll = bOn;
  g_fDragSpeed = fSpeed;
}

// From half Windows' speed at Slow to twice it at Fast, the same at the middle.
void PortMouseSensitivitySet(bool bOn, float fSpeed)
{
  g_sens = bOn;
  g_fSensScale = SDL_powf(4.0f, SDL_clamp(fSpeed, 0.0f, 1.0f) - 0.5f);
  g_sensAt = false;
}

// The movement is read from the pointer itself, not from the event, so moves queued before the
// last put are not counted twice. Where the cage stops the pointer short, the parts of a pixel
// carried on that axis are dropped.
bool PortMouseScale(int *pnX, int *pnY)
{
  static POINT s_at;
  static float s_fRestX;
  static float s_fRestY;
  POINT pt;
  if (!g_sens || !g_held || g_frontUp || g_cursorFree || g_hWnd == NULL || GetForegroundWindow() != g_hWnd ||
      !GetCursorPos(&pt)) {
    g_sensAt = false;
    return false;
  }
  if (!g_sensAt) {
    g_sensAt = true;
    s_at = pt;
    s_fRestX = s_fRestY = 0.0f;
    return false;
  }
  const float fX = (pt.x - s_at.x) * g_fSensScale + s_fRestX;
  const float fY = (pt.y - s_at.y) * g_fSensScale + s_fRestY;
  POINT want;
  want.x = s_at.x + static_cast<LONG>(SDL_floorf(fX));
  want.y = s_at.y + static_cast<LONG>(SDL_floorf(fY));
  s_fRestX = fX - SDL_floorf(fX);
  s_fRestY = fY - SDL_floorf(fY);
  POINT to = want;
  if (want.x != pt.x || want.y != pt.y) {
    SetCursorPos(want.x, want.y);
    if (!GetCursorPos(&to))
      to = want;
    if (to.x != want.x)
      s_fRestX = 0.0f;
    if (to.y != want.y)
      s_fRestY = 0.0f;
  }
  s_at = to;
  ScreenToClient(g_hWnd, &to);
  *pnX = to.x;
  *pnY = to.y;
  return true;
}

// How many game pixels a client pixel is across, for the drag: the frame's scale in the
// window.
static void PresentGameScale(float *pfX, float *pfY)
{
  *pfX = 1.0f;
  *pfY = 1.0f;
  if (g_renderer != NULL) {
    float fX0, fY0, fX1, fY1;
    const float fDensity = PresentDensity();
    SDL_RenderCoordinatesFromWindow(g_renderer, 0.0f, 0.0f, &fX0, &fY0);
    SDL_RenderCoordinatesFromWindow(g_renderer, 100.0f, 100.0f, &fX1, &fY1);
    *pfX = (fX1 - fX0) / 100.0f / fDensity;
    *pfY = (fY1 - fY0) / 100.0f / fDensity;
  } else if (g_gpu) {
    SDL_FRect box;
    if (PresentGpuBox(&box) && box.w > 0.0f && box.h > 0.0f) {
      *pfX = g_portView.nWidth / box.w;
      *pfY = g_portView.nHeight / box.h;
    }
  }
}

// While the middle button is held in the game's window, how far the view moves this step:
// the way the cursor moved since the last, the other way round, so the map goes with the
// cursor. The speed (Settings' slider) scales it from a tenth of the cursor's movement to
// one and a half times it, a plain grab at 0.64; the movement is taken in the screen's
// pixels and the parts of a game pixel carried to the next step, so a slow drag is smooth.
// Never in a background or hidden window, whose runs the real mouse must not reach.
bool PortDragScroll(int *pnX, int *pnY)
{
  static bool s_bHeld;
  static POINT s_at;
  static float s_fRestX;
  static float s_fRestY;
  POINT pt;
  if (!g_dragScroll || g_hidden || g_background || g_frontUp || g_hWnd == NULL ||
      GetForegroundWindow() != g_hWnd || (GetAsyncKeyState(VK_MBUTTON) & 0x8000) == 0 || !GetCursorPos(&pt)) {
    s_bHeld = false;
    return false;
  }
  *pnX = 0;
  *pnY = 0;
  if (!s_bHeld) {
    s_bHeld = true;
    s_at = pt;
    s_fRestX = s_fRestY = 0.0f;
    return true;
  }
  float fScaleX;
  float fScaleY;
  PresentGameScale(&fScaleX, &fScaleY);
  const float fSpeed = 0.1f + 1.4f * g_fDragSpeed;
  const float fX = (s_at.x - pt.x) * fScaleX * fSpeed + s_fRestX;
  const float fY = (s_at.y - pt.y) * fScaleY * fSpeed + s_fRestY;
  *pnX = static_cast<int>(fX);
  *pnY = static_cast<int>(fY);
  s_fRestX = fX - *pnX;
  s_fRestY = fY - *pnY;
  s_at = pt;
  return true;
}
