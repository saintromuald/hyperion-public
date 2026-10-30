// port_view.h -- the view: the size of the screen the game draws, the map's rows, the dialogs'
// places, and the camera's picture of the map as the game reads it (port_view.cpp;
// docs/renderer.md, R3).
#ifndef PORT_VIEW_H
#define PORT_VIEW_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// The map's rows the view's whole height at any width (no console), or 1.17's 400 at 640 again:
// in a running game at once, or between games for the next.
void PortViewFullRows(bool bFull, bool bInGame);

// The view (port_view.cpp, docs/renderer.md R3): the size of the screen the game
// draws, in game pixels, and the sizes that follow from it. 1.17's is 640x480
// with the map in the 400 rows above the console; the places that used those
// numbers read them here. A view's width is a multiple of 16, so the dirty
// grid's cells are whole; the widest is 32:9 at 480 rows, rounded to 1712.
#define PORT_VIEW_MAX_WIDTH 1712
#define PORT_VIEW_MAX_HEIGHT 480
// The scroll ring's megatiles for the widest view, and its bytes (gamemap_init
// allocates it once).
#define PORT_VIEW_RING_TILES_X_MAX ((PORT_VIEW_MAX_WIDTH + 31) / 32 + 1)
#define PORT_VIEW_RING_TILES_Y_MAX ((PORT_VIEW_MAX_HEIGHT + 31) / 32 + 1)
#define PORT_VIEW_RING_MAX (PORT_VIEW_RING_TILES_X_MAX * 32 * PORT_VIEW_RING_TILES_Y_MAX * 32)
// The dirty grid for the widest view, with a row and a column to spare:
// invalidate_rect (debug.cpp) marks through the cell below and right of a
// rectangle that ends on the screen's edge, as 1.17's did.
#define PORT_VIEW_GRID_MAX (((PORT_VIEW_MAX_WIDTH + 15) / 16 + 1) * ((PORT_VIEW_MAX_HEIGHT + 15) / 16 + 1))
struct PortView {
  int nWidth;       // the screen's width: 640 in 1.17
  int nHeight;      // its height: 480
  int nMapRows;     // the rows the map is drawn in, from the top: 400 in 1.17
  int nViewRows;    // the rows of map that count as on screen, above the console:
                    // 400, since a wider view shows more only to the sides
  int nConsoleX;    // the left edge of the 640-wide console: 0 in 1.17
  int nRingTilesX;  // megatiles in a row of the scroll ring (_gfpScrollBuf): 21
  int nRingTilesY;  // rows of megatiles in it: 14
  int nRingPitch;   // bytes from one row of the ring to the next: 672
  int nRingSize;    // bytes in the ring: 0x49800
  int nGridWidth;   // 16x16 cells in a row of the dirty grid: 40
  int nGridHeight;  // rows of cells: 30
  unsigned char *pbInvalMap;  // the dirty grid (1.17's gbInvalMap), nGridWidth cells a row
};
extern PortView g_portView;
void PortViewClearGrid(void);
// Sets the view to nWidth game pixels across, rounded to a multiple of 16 and
// held between 640 and the widest, and resizes what was sized from it: the draw
// buffer's and the dialog layer's extents, the screen rectangles, the mouse's
// cage and the dirty grid, all marked for redraw. The buffers behind them are
// the widest view's already, so nothing is reallocated. main_game_run sets the
// game's width before a game and 640 after it, so the menus stay 640x480.
void PortViewSet(int nWidth);
// The width a game is played at: --view, as a width or a shape (16:9 is 848),
// or without it Settings' View shape, until a game switches it (PortViewApply).
int PortViewGameWidth(void);
// Settings' View shape, the "view" setting: the original's 4:3 at 640, the display's
// shape, or widescreen 16:9. The display's shape when none is kept (the user, 2026-10-19;
// 4:3 before). Choosing one keeps it, and it is the next game's width unless --view gives one.
enum { PORT_VIEW_4_3 = 0, PORT_VIEW_FIT = 1, PORT_VIEW_16_9 = 2 };
int PortViewShape(void);
void PortViewChoose(int nShape);
// Switching the view while a game runs. PortViewRequest asks for a width; PortViewToggle for
// 640 or the wide width (--view's, else the wide shape last chosen), whichever the game is not
// at, kept as Settings' View shape (the view hotkey, Ctrl+F5 unless changed). RunGameLoop calls
// PortViewApply between frames, which sets the view and all a game placed from it (dialogs,
// the console's mask, the minimap's area, the scroll limits and ring, the fog's grids). The
// width is the next game's too; outside a game a request waits.
void PortViewRequest(int nWidth);
void PortViewToggle(void);
void PortViewApply(void);
// Where a dialog sits across a wider view, as a multiple of nConsoleX: 1, with
// the console in the middle, for most; 2, at the screen's right, for the
// resources (statres); 0, at its left, for the leaderboard (statlb).
struct ctrl_t;
int PortViewDialogAnchor(const struct ctrl_t *pDlg);
// A dialog's shift from its 640x480 place, in pixels: its anchor's, or for the console's
// own dialogs PortConsoleShift's.
int PortViewDialogShift(const struct ctrl_t *pDlg);
// The open dialogs as JSON objects, comma-separated, into psz: each one's name when it is
// one of the console's or the edges' ("minimap", "statdata", ...), its text, and its
// rectangle on the screen (the live channel's "dialogs"). Returns the length.
int PortViewDialogs(char *psz, int nMax);
// The centre-view trigger's auto-scroll target: the view's top left with
// (nMidX, nMidY) in its middle, inside the map at the view's size. The middle
// is kept, so a view switch during the scroll aims it again.
void PortViewCentreTarget(int nMidX, int nMidY, int *pnX, int *pnY);

// The render view (port_view.cpp): with the camera zoomed out, the map's part of a pass
// (gamemap_update up to the map's text) is drawn over a larger view than the game's, the view,
// the scroll and the screen buffer set to it and back: the GPU alone drawing the map, its
// primitives in the render view's place and the software drawing nothing, or under SDL's
// renderer the software drawing it (below). The camera's top left in map pixels, its zoom, and the render view's rectangle in map pixels, which the GPU
// scene draws and the presenter shows through the camera.
struct PortRenderView {
  int bOn;              // the last pass was drawn over the render view
  double fCamX, fCamY;  // the map pixel at the screen's top left
  double fZoom;
  int nX, nY;           // the render view's top left in map pixels, a multiple of 8
  int nWidth, nRows;    // its size, in multiples of 16
  int nGameX, nGameY;   // the game's own scroll
};
extern PortRenderView g_portRender;
void PortViewRenderBegin(void);
void PortViewRenderEnd(void);
int PortViewRendering(void);
// Under SDL's renderer, which has no GPU scene, the software draws the render view itself, into
// a buffer and a scroll ring of its own (for a browser's tab). PortViewRenderSoft: the
// pass now is one; gamemap_update draws it whole. PortViewSoftShown: the last pass's render
// view, g_portRender.nWidth a row and placed by g_portRender, while a game shows it, else NULL;
// *pnPass counts the passes. PortViewSoftCover: img_update_real's copy onto the screen marks
// the screen's cover (port_present.cpp) 1 where the map shows, and 0 where a layer over it drew.
// PortViewZoomSoftMost: the most the camera zooms out under SDL's renderer, 0 under the GPU's.
int PortViewRenderSoft(void);
const unsigned char *PortViewSoftShown(unsigned int *pnPass);
void PortViewSoftCover(const unsigned char *pCopied);
double PortViewZoomSoftMost(void);
// Whether the cursor at game pixel x, y is over the map and nothing on it (the console, a
// dialog), where the mouse wheel zooms.
int PortViewPointOnMap(int nX, int nY);
// Where the camera looks at zoom fZoom with the game's view at nScrollX, nScrollY: the map
// pixel at the screen's top left. The game view's middle above the console stays where it
// is on the screen, as 1.17's jumps put a place there, and the camera's sight stays inside
// the map, a map smaller than it in the middle. Beside a HUD design's side columns, the
// middle and the sight are those of the part that shows (PortViewShownWidth).
void PortViewCameraPlace(double fZoom, double fScrollX, double fScrollY, double *pfX, double *pfY);
// The width of the game's view that shows beside a HUD design's side columns,
// in the screen's pixels: the view's own width when there are none. The camera's sight
// starts past a left column, 1.17's scroll may go as far past the map's right edge as the
// columns are wide, so the map's far side comes beside them, and 1.17's jumps put a place in
// the middle of this width (gamemap_center_on_mtx, a group's, the portrait's, the minimap's).
int PortViewShownWidth(void);
// The camera as it is shown now: the map pixel at the screen's top left and
// the zoom. Its centre follows the game's view, by as much when the view scrolls and easing
// there when it jumps (the minimap, a hotkey, an alert); the wheel zooms about the cursor.
// In a game the game's view is then moved to follow it (cell_set_pos, which keeps it to 8
// pixels); in a replay it is not, so a replay's game view is only ever the viewer's own
// scrolling, as in 1.17. Returns 1 while it moves on its own.
int PortViewCamera(double *pfX, double *pfY, double *pfZoom);
// The game view's size the camera's zoom multiplies: its width and its rows above the console.
void PortViewShape(double *pfWidth, double *pfRows);
// The camera's own moves from the keys and the live channel: zoom by notches about the
// screen's middle, back to 1, the whole map (a replay; the limit in a game), and a replay's
// camera centred on a map point (in a game the game's view goes there).
void PortViewZoomKey(int nNotches);
void PortViewZoomReset(void);
void PortViewZoomFit(void);
void PortViewCameraCentre(double fX, double fY);
// The middle of what the camera shows, in map pixels (the game's view's when it has none).
void PortViewCameraMiddle(double *pfX, double *pfY);

// The minimap's handlers tell the camera that a press (PortViewMinimapPress, before the
// press's first move) or a drag moved the game's view (PortViewMinimapMoved, port_view.cpp).
void PortViewMinimapPress(void);
void PortViewMinimapMoved(void);
// scroll_update moved the game's view by a scroll step, from nFromX, nFromY to where it is
// now: the camera's glide takes the step's time (port_view.cpp).
void PortViewScrollStep(LONG nFromX, LONG nFromY);

// The map pixel under the screen's pixel nX, nY, as the camera shows it: the scroll plus the
// point without the zoom, as 1.17 has it (the clicks, the cursor over a unit, a drag's box).
void PortViewScreenToMap(int nX, int nY, int *pnMapX, int *pnMapY);
// The map pixels the camera shows above the console, right and bottom exclusive, beside a HUD
// design's side columns: the game's view at zoom 1. For what only the local player hears, which 1.17 already measures from
// each player's own view (a sound's volume, the under-attack call).
void PortViewSeenRect(int *pnLeft, int *pnTop, int *pnRight, int *pnBottom);
// In a replay, or while a cutscene plays, the zoom at which the camera shows the whole map (the
// most the render view's largest side allows); 0 otherwise in a game, where the zoomMax setting
// is the limit.
double PortViewZoomWhole(void);
// Zoomed, the layers 1.17 draws over the map in the game view's place (the placement box's
// two, 3 and 4) drawn in the map's pass at their place in the render view, so they are the
// map's and scale with it; gamemap_update calls it before PortViewRenderEnd. img_update_real
// then leaves out each layer PortViewLayerDrawn says was drawn there.
void PortViewRenderLayers(void);
int PortViewLayerDrawn(int nLayer);
// The mouse wheel in a game (WndProc's WM_MOUSEWHEEL, nDelta its 120ths of a notch, away
// from the player positive): with the cursor over the map, the camera zooms in or out and
// the wheel goes no further (1). Elsewhere, or where the camera cannot zoom, 0.
int PortViewWheel(int nDelta);

#endif
