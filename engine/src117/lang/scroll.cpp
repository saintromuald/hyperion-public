// scroll.cpp -- the map-scroll input layer, oracle 0x49B7C0-0x49C140: the
// keyboard and auto-scroll shift, the speed ramp, and the parallax star
// layers the scrolled view drags along. The TU's name is 1.17's own: mem_free's
// owner tag is the string "scroll.cpp".

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/scroll.h"
#include "../h/cellscrl.h"
#include "../h/cursor.h"
#include "../h/gameloop.h"
#include "../h/gamemap.h"
#include "../h/glues.h"
#include "../h/minimap.h"
#include "../h/snd.h"
#include "../h/TrigAct.h"
#include "../port/render/port_camera.h"  // Hyperion: PortCameraScrollPixels
#include "../port/data/port_formats.h"  // Hyperion: PortStarsNative
#include "../port/render/port_present.h"  // Hyperion: PortDragScroll
#include "../port/render/port_scene.h"  // Hyperion: PortSceneStar
#include "../port/render/port_view.h"  // Hyperion: g_portView, PortViewScrollStep
#include "../port/tools/port_control.h"  // Hyperion: PortControlKeepsRunning
#include "../h/usel.h"
#include "../gen/globals117.h"

// The layout of parallax\star.spk.

// One star, 8 bytes: a screen position and the image the layer blits there.
struct starent_t {
  /*0x0*/ WORD wX;
  /*0x2*/ WORD wY;
  /*0x4*/ BYTE *pbImage;
};
static_assert(offsetof(starent_t, wX) == 0x0, "DrawStars movzx ecx, word ptr [ecx]");
static_assert(offsetof(starent_t, wY) == 0x2, "DrawStars movzx esi, word ptr [eax+2h]");
OC_X86_LAYOUT(offsetof(starent_t, pbImage) == 0x4, "DrawStars mov edx, dword ptr [ecx+4h]");
OC_X86_LAYOUT(sizeof(starent_t) == 8, "scroll_init_game lea eax, dword ptr [eax+8h]");
// Hyperion: PortStarsNative writes the stars at 64 bits in this layout.
static_assert(offsetof(starent_t, pbImage) == sizeof(void *) && sizeof(starent_t) == 2 * sizeof(void *),
              "starent_t is port_formats.cpp's PortStar");

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x509390 in .rdata -- the scroll ramp, seven rows of seven pixel steps,
// indexed by the speed setting and the ramp position; scroll_get_speed walks
// row 6.
#ifdef OC_DLL_MODE
  #ifndef sgScrollSpeeds_g
  #define sgScrollSpeeds_g (*reinterpret_cast<BYTE (*)[7][7]>(OC_VA(0x509390u)))
  #endif
#else
extern BYTE sgScrollSpeeds_g[7][7];
#endif

// 0x665E58 gKeyTbl, the key-down table event_Win.cpp owns and memsets at
// 0x100 bytes. scroll_get_shift reads sixteen of its virtual-key slots.
#ifdef OC_DLL_MODE
  #ifndef gKeyTbl
  #define gKeyTbl (reinterpret_cast<BYTE *>(OC_VA(0x665E58u)))
  #endif
#else
extern BYTE gKeyTbl[0x100] OC_UNALIGNED;
#endif

// 0x543250 / 0x543254 -- the parallax accumulators' wrap periods, 648 and 488
// in x256 fixed point, spelled as cellscrl.cpp spells them.
#ifndef OC_DWORD_543250
#define OC_DWORD_543250
#ifdef OC_DLL_MODE
#define dword_543250 (*reinterpret_cast<LONG *>(OC_VA(0x543250u)))
#else
extern LONG dword_543250;
#endif
#endif
#ifndef OC_DWORD_543254
#define OC_DWORD_543254
#ifdef OC_DLL_MODE
#define dword_543254 (*reinterpret_cast<LONG *>(OC_VA(0x543254u)))
#else
extern LONG dword_543254;
#endif
#endif

// 0x6A5EA4 -- how many stars each of the five layers owns, and 0x6A5EB8 --
// where each layer's run of them starts.
#ifdef OC_DLL_MODE
  #ifndef sgLayerCounts_g
  #define sgLayerCounts_g (*reinterpret_cast<DWORD (*)[5]>(OC_VA(0x6A5EA4u)))
  #define sgLayerStars_g (*reinterpret_cast<starent_t *(*)[5]>(OC_VA(0x6A5EB8u)))
  #endif
#else
extern DWORD sgLayerCounts_g[5] OC_UNALIGNED;
extern starent_t *sgLayerStars_g[5];
#endif

// 0x6A5ED0 -- the loaded parallax\star.spk image, freed by scroll_free. Only
// this TU reaches it, so the plain arm is static.
#ifdef OC_DLL_MODE
  #ifndef sgpStarData_g
  #define sgpStarData_g (*reinterpret_cast<void **>(OC_VA(0x6A5ED0u)))
  #endif
#else
extern void *sgpStarData_g;
#endif

// 0x6A5ED4 -- the auto-scroll flag, a BYTE.
#ifdef OC_DLL_MODE
  #ifndef sgbAutoScroll_g
  #define sgbAutoScroll_g (*reinterpret_cast<BYTE *>(OC_VA(0x6A5ED4u)))
  #endif
#else
extern BYTE sgbAutoScroll_g;
#endif

// 0x6A5ED8 -- the nullary callback the next auto-scroll request runs first,
// and that scroll_update runs once the view has arrived.
#ifdef OC_DLL_MODE
  #ifndef sgpfnAutoScrollDone_g
  #define sgpfnAutoScrollDone_g (*reinterpret_cast<void (**)()>(OC_VA(0x6A5ED8u)))
  #endif
#else
extern void (*sgpfnAutoScrollDone_g)();
#endif

// 0x6A5EDC / 0x6A5EE0 -- the auto-scroll target, compared unsigned against
// glScrollPixelX/Y.
#ifdef OC_DLL_MODE
  #ifndef sgAutoScrollX_g
  #define sgAutoScrollX_g (*reinterpret_cast<DWORD *>(OC_VA(0x6A5EDCu)))
  #define sgAutoScrollY_g (*reinterpret_cast<DWORD *>(OC_VA(0x6A5EE0u)))
  #endif
#else
extern DWORD sgAutoScrollX_g;
extern DWORD sgAutoScrollY_g;
#endif

// 0x66FD24 sgbWindowActive_g, a DWORD, spelled as gameloop.cpp spells it.
#ifdef OC_DLL_MODE
  #ifndef sgbWindowActive_g
  #define sgbWindowActive_g (*reinterpret_cast<DWORD *>(OC_VA(0x66FD24u)))
  #endif
#else
extern DWORD sgbWindowActive_g;
#endif

// 0x6CA720 gbSelectDragging and 0x672664 gbMultiPlayerMode, both BYTE and both
// spelled as gamemap.cpp and gameloop.cpp spell them.
#ifdef OC_DLL_MODE
  #ifndef gbSelectDragging
  #define gbSelectDragging (*reinterpret_cast<BYTE *>(OC_VA(0x6CA720u)))
  #endif
  #ifndef gbMultiPlayerMode
  #define gbMultiPlayerMode (*reinterpret_cast<BYTE *>(OC_VA(0x672664u)))
  #endif
#else
extern BYTE gbSelectDragging;
extern BYTE gbMultiPlayerMode;
#endif

// 0x6D3134 / 0x6D3138 and 0x6D314C / 0x6D3150 -- the ramp row to use, the first
// pair single-player and the second multiplayer, the first of each pair for a
// cursor-edge scroll and the second for a key scroll. They fall inside the
// gOptions window at 0x6D3130, which has no registered type.
#ifdef OC_DLL_MODE
  #ifndef dword_6D3134
  #define dword_6D3134 (*reinterpret_cast<DWORD *>(OC_VA(0x6D3134u)))
  #define dword_6D3138 (*reinterpret_cast<DWORD *>(OC_VA(0x6D3138u)))
  #define dword_6D314C (*reinterpret_cast<DWORD *>(OC_VA(0x6D314Cu)))
  #define dword_6D3150 (*reinterpret_cast<DWORD *>(OC_VA(0x6D3150u)))
  #endif
#else
extern DWORD dword_6D3134;
extern DWORD dword_6D3138;
extern DWORD dword_6D314C;
extern DWORD dword_6D3150;
#endif

// 0x6A5ECC -- set when a key scroll moved the view, and cleared by
// cursor_get_scroll_cursor once it has seen it. 0x6A5EEC is the ramp position,
// compared unsigned against 6. 0x6A5EE4 / 0x6A5EE8 keep the last shift, both
// tested signed.
#ifdef OC_DLL_MODE
  #ifndef dword_6A5ECC
  #define dword_6A5ECC (*reinterpret_cast<DWORD *>(OC_VA(0x6A5ECCu)))
  #define dword_6A5EE4 (*reinterpret_cast<int *>(OC_VA(0x6A5EE4u)))
  #define dword_6A5EE8 (*reinterpret_cast<int *>(OC_VA(0x6A5EE8u)))
  #define dword_6A5EEC (*reinterpret_cast<DWORD *>(OC_VA(0x6A5EECu)))
  #endif
#else
extern DWORD dword_6A5ECC;
extern int dword_6A5EE4;
extern int dword_6A5EE8;
extern DWORD dword_6A5EEC;
#endif

// 0x543258 and 0x54326C -- five parallax accumulators per axis in x256 fixed
// point, and 0x6D0B8C the back buffer, spelled as cellscrl.cpp and
// gds_vidblit.cpp spell them.
#ifndef OC_DWORD_543258
#define OC_DWORD_543258
#ifdef OC_DLL_MODE
#define dword_543258 (reinterpret_cast<LONG *>(OC_VA(0x543258u)))
#else
extern LONG dword_543258[5] OC_UNALIGNED;
#endif
#endif
#ifndef OC_DWORD_54326C
#define OC_DWORD_54326C
#ifdef OC_DLL_MODE
#define dword_54326C (reinterpret_cast<LONG *>(OC_VA(0x54326Cu)))
#else
extern LONG dword_54326C[5] OC_UNALIGNED;
#endif
#endif
#ifndef OC_DWORD_6D0B8C
#define OC_DWORD_6D0B8C
// It is _img_draw_buf.pPixels, bmp_t's +4, and is spelled as the field, since
// bmp_t's layout changes at 64 bits.
#define dword_6D0B8C (_img_draw_buf.pPixels)
#endif

// 0x6D06D0 gbInvalMap -- one byte per 16 by 16 screen cell, 30 rows of 40.
// Hyperion: the grid is the view's, g_portView.pbInvalMap (port_view.cpp).
static inline BYTE ScrollInval(int nRow, int nCol)
{
  return g_portView.pbInvalMap[nRow * g_portView.nGridWidth + nCol];
}

// The bodies, in oracle VA order.

unsigned char __fastcall IsAutoScroll(void) {
  return sgbAutoScroll_g;
}

void scroll_free() {
  mem_free(sgpStarData_g, "scroll.cpp", 550);
  sgpStarData_g = 0;
}

// Load the parallax star field, split it into its five layers and turn every
// star's stored image offset into a pointer.
// sgpStarData_g is static in the plain arm, so MSVC reloads it after the counts
// loop, as 1.17 does.
void scroll_init_game() {
  starent_t *pStars;
  WORD *pwHdr;
  WORD *pw;
  int nLayers;
  int nTotal;
  int i;

  dword_543250 = 0x28800;
  dword_543254 = 0x1E800;
  // Hyperion: the stars in the engine's layout (port_formats.cpp)
  pwHdr = PortStarsNative(static_cast<WORD *>(
      load_game_data("parallax\\star.spk", 0, 0, "scroll.cpp", 498)));
  sgpStarData_g = pwHdr;
  nLayers = pwHdr[0];
  nTotal = 0;
  pw = pwHdr;
  for (i = 0; i < nLayers; i++) {
    sgLayerCounts_g[i] = pw[1];
    nTotal += pw[1];
    pw++;
  }
  pw = static_cast<WORD *>(sgpStarData_g);
  pStars = reinterpret_cast<starent_t *>(pw + nLayers + 1);
  for (i = 0; i < nTotal; i++)
    pStars[i].pbImage += reinterpret_cast<DWORD_PTR>(pw);
  sgLayerStars_g[0] = pStars;
  sgLayerStars_g[1] = pStars + sgLayerCounts_g[0];
  sgLayerStars_g[2] = sgLayerStars_g[1] + sgLayerCounts_g[1];
  sgLayerStars_g[3] = sgLayerStars_g[2] + sgLayerCounts_g[2];
  sgLayerStars_g[4] = sgLayerStars_g[3] + sgLayerCounts_g[3];
}

// Which way the view wants to move this frame: toward the auto-scroll target
// while one is running, otherwise whichever of the four diagonal, then four
// cardinal, key pairs is down. The keys are numpad 1/2/3/4/6/7/8/9 against
// End / Down / PageDown / Left / Right / Home / Up / PageUp, and the diagonals
// test the numpad first where the cardinals test the arrow first.
void scroll_get_shift(int *pnShiftX, int *pnShiftY) {
  if (sgbAutoScroll_g != 0) {
    *pnShiftY = 0;
    *pnShiftX = 0;
    if (sgAutoScrollX_g < (DWORD)glScrollPixelX)
      *pnShiftX = -1;
    else if (sgAutoScrollX_g > (DWORD)glScrollPixelX)
      *pnShiftX = 1;
    if (sgAutoScrollY_g < (DWORD)glScrollPixelY)
      *pnShiftY = -1;
    else if (sgAutoScrollY_g > (DWORD)glScrollPixelY)
      *pnShiftY = 1;
    return;
  }
  *pnShiftY = 0;
  *pnShiftX = 0;
  if (gKeyTbl[0x63] != 0 || gKeyTbl[0x22] != 0) {
    *pnShiftY = 1;
    *pnShiftX = 1;
  } else if (gKeyTbl[0x61] != 0 || gKeyTbl[0x23] != 0) {
    *pnShiftY = 1;
    *pnShiftX = -1;
  } else if (gKeyTbl[0x67] != 0 || gKeyTbl[0x24] != 0) {
    *pnShiftY = -1;
    *pnShiftX = -1;
  } else if (gKeyTbl[0x69] != 0 || gKeyTbl[0x21] != 0) {
    *pnShiftY = -1;
    *pnShiftX = 1;
  } else {
    if (gKeyTbl[0x28] != 0 || gKeyTbl[0x62] != 0)
      *pnShiftY = 1;
    else if (gKeyTbl[0x26] != 0 || gKeyTbl[0x68] != 0)
      *pnShiftY = -1;
    if (gKeyTbl[0x27] != 0 || gKeyTbl[0x66] != 0)
      *pnShiftX = 1;
    else if (gKeyTbl[0x25] != 0 || gKeyTbl[0x64] != 0)
      *pnShiftX = -1;
  }
}

// Begin an auto-scroll to a tile-aligned position, retiring whatever callback
// the previous request left armed.
void scroll_start_auto(int nX, int nY, void (*pfnDone)()) {
  if (sgpfnAutoScrollDone_g != 0)
    sgpfnAutoScrollDone_g();
  sgAutoScrollX_g = nX & ~7;
  sgAutoScrollY_g = nY & ~7;
  sgbAutoScroll_g = 1;
  sgpfnAutoScrollDone_g = pfnDone;
}

// Drop auto-scroll and put the pointer back in the middle of the view.
// Hyperion: the middle of the view's width, 320 in 1.17.
void scroll_end_auto() {
  if (sgbAutoScroll_g != 0) {
    sgbAutoScroll_g = 0;
    mou_set_pos(g_portView.nWidth / 2, 200);
  }
}

// Back the ramp position down until both axes' remaining distance covers the
// step it selects. Auto-scroll always runs the fastest row, so the row index
// is the return value.
int scroll_get_speed(int *pnRamp) {
  DWORD dwDistX;
  DWORD dwDistY;

  if (sgAutoScrollX_g < (DWORD)glScrollPixelX)
    dwDistX = (DWORD)glScrollPixelX - sgAutoScrollX_g;
  else
    dwDistX = sgAutoScrollX_g - (DWORD)glScrollPixelX;
  if (sgAutoScrollY_g < (DWORD)glScrollPixelY)
    dwDistY = (DWORD)glScrollPixelY - sgAutoScrollY_g;
  else
    dwDistY = sgAutoScrollY_g - (DWORD)glScrollPixelY;

  if (dwDistX != 0) {
    int nRamp = *pnRamp;
    if (sgScrollSpeeds_g[6][nRamp] > dwDistX) {
      do
        nRamp--;
      while (sgScrollSpeeds_g[6][nRamp] > dwDistX);
      *pnRamp = nRamp;
    }
  }
  if (dwDistY != 0) {
    int nRamp = *pnRamp;
    if (sgScrollSpeeds_g[6][nRamp] > dwDistY) {
      do
        nRamp--;
      while (sgScrollSpeeds_g[6][nRamp] > dwDistY);
      *pnRamp = nRamp;
    }
  }
  return 6;
}

void scroll_reset() {
  sgbAutoScroll_g = 0;
  sgpfnAutoScrollDone_g = 0;
}

// Hyperion: for the live channel (scroll.h)
bool ScrollAutoTarget(int *pnX, int *pnY) {
  *pnX = static_cast<int>(sgAutoScrollX_g);
  *pnY = static_cast<int>(sgAutoScrollY_g);
  return sgbAutoScroll_g != 0;
}

bool ScrollWindowActive(void) {
  return sgbWindowActive_g != 0;
}

// One frame of map scrolling: pick a direction from the keys or the screen
// edge, ramp the step up, move the view and retire the auto-scroll callback.
// 1.17 passes cursor_get_scroll_cursor only the X pointer, in ECX; EDX still
// holds &s.y from scroll_get_shift, and the callee writes the Y shift there.
// Hyperion: both pointers are passed, the Y one as 1.17 left it in EDX; the
// one-parameter form left the x64 build's second argument to chance
//.
// The two shifts and bMoved are one struct in the oracle's frame order;
// passing &s.x lets the whole struct escape, which is why the oracle reloads
// the Y shift after that call.
int cursor_get_scroll_cursor(int *pnScrollX, int *pnScrollY);
void scroll_update() {
  struct { int bMoved; int y; int x; } s;
  DWORD dwRow;

  // Hyperion: a centre-view trigger's scroll also moves while the game runs unfocused
  // (Settings' background play, the live channel), as the trigger waiting on it would otherwise
  // hold until the window is active again
  if (sgbWindowActive_g == 0 && !(sgbAutoScroll_g != 0 && PortControlKeepsRunning()))
    return;
  if (IsPausedSingleplayer() != 0)
    return;
  if (sgbAutoScroll_g != 0) {
    usel_cancel_drag_evt();
  } else {
    if (gbSelectDragging != 0)
      return;
    if (cursor_is_locked() != 0)
      return;
    // Hyperion: the middle button held drags the map, in place of the keys and the
    // screen's edge (Settings, "Middle-mouse drag scroll"; PortDragScroll)
    int nDragX;
    int nDragY;
    if (PortDragScroll(&nDragX, &nDragY)) {
      // Hyperion: the map under the cursor follows it at any zoom
      nDragX = PortCameraScrollPixels(nDragX);
      nDragY = PortCameraScrollPixels(nDragY);
      if (nDragY < 0)
        cell_scroll_up(-nDragY);
      else if (nDragY > 0)
        cell_scroll_dn(nDragY);
      if (nDragX < 0)
        cell_scroll_lft(-nDragX);
      else if (nDragX > 0)
        cell_scroll_rgt(nDragX);
      dword_6A5EEC = 0;
      if (nDragX != 0 || nDragY != 0)
        gamemap_force_redraw();
      return;
    }
  }

  scroll_get_shift(&s.x, &s.y);
  if (s.x != 0 || s.y != 0) {
    dwRow = gbMultiPlayerMode != 0 ? dword_6D3150 : dword_6D3138;
    dword_6A5ECC = 1;
    s.bMoved = 1;
    if (dword_6A5EEC < 6)
      dword_6A5EEC = dword_6A5EEC + 1;
  } else {
    cursor_get_scroll_cursor(&s.x, &s.y);
    dwRow = gbMultiPlayerMode != 0 ? dword_6D314C : dword_6D3134;
    if (s.x != 0 || s.y != 0) {
      s.bMoved = 1;
      if (dword_6A5EEC < 6)
        dword_6A5EEC = dword_6A5EEC + 1;
    } else {
      s.bMoved = 0;
      dword_6A5EEC = 0;
    }
  }

  if (sgbAutoScroll_g != 0) {
    // The first test is repeated to give the call two identical arms, as in 1.17.
    if (sgbWindowActive_g != 0)
      dwRow = scroll_get_speed(reinterpret_cast<int *>(&dword_6A5EEC));
    else
      dwRow = scroll_get_speed(reinterpret_cast<int *>(&dword_6A5EEC));
  }
  if (s.bMoved != 0) {
    dword_6A5EE4 = s.x;
    dword_6A5EE8 = s.y;
  }
  const LONG nFromX = glScrollPixelX;  // Hyperion: for the camera's glide, below
  const LONG nFromY = glScrollPixelY;
  if (dword_6A5EEC != 0) {
    // Hyperion: the player's scrolls times the zoom, so the picture moves as fast at any
    // zoom; an auto-scroll's own speed as 1.17's
    int nSpeed = sgScrollSpeeds_g[dwRow][dword_6A5EEC];
    if (sgbAutoScroll_g == 0)
      nSpeed = PortCameraScrollPixels(nSpeed);
    if (dword_6A5EE8 < 0)
      s.y = cell_scroll_up(nSpeed);
    else if (dword_6A5EE8 > 0)
      s.y = cell_scroll_dn(nSpeed);
    if (dword_6A5EE4 < 0)
      s.x = cell_scroll_lft(nSpeed);
    else if (dword_6A5EE4 > 0)
      s.x = cell_scroll_rgt(nSpeed);
  }
  if (s.x != 0 || s.y != 0)
    gamemap_force_redraw();
  // Hyperion: the step and its time, which the camera glides through (port_view.cpp)
  if (glScrollPixelX != nFromX || glScrollPixelY != nFromY)
    PortViewScrollStep(nFromX, nFromY);
  if (sgbAutoScroll_g != 0 && glScrollPixelX == (LONG)sgAutoScrollX_g &&
      glScrollPixelY == (LONG)sgAutoScrollY_g) {
    if (sgpfnAutoScrollDone_g != 0)
      sgpfnAutoScrollDone_g();
    sgpfnAutoScrollDone_g = 0;
  }
}

// The parallax stars: five layers, each shifted by its own accumulator and
// wrapped in a 648 by 488 field that starts 8 pixels above and left of the
// screen. A star pixel only lands where the back buffer is still clear.
//
// Hyperion: classic's two passes (DrawStars and DrawStarsDirty) are rewritten
// around StarBlit so that a view wider than the
// field repeats it every 648 pixels across (port_view.cpp); 480 map rows fit in
// its height. At 640 only the first copy is on screen, so the frame is 1.17's.
enum {
  STAR_FIELD_WIDTH = 0x288,
  STAR_FIELD_HEIGHT = 0x1E8,
  STAR_FIELD_MARGIN = 8
};

// A star's place in the field, from its position and its layer's shift.
static inline int StarWrap(int nPos, int nField)
{
  if (nPos > nField)
    nPos -= nField;
  else if (nPos < 0)
    nPos += nField;
  return nPos - STAR_FIELD_MARGIN;
}

// One star's image (a WORD width and height, then the pixels) at (nX, nY),
// clipped to the view's width and map rows. With bDirty only when one of its
// corners' 16x16 cells is marked for redraw, or its one cell when it is a
// single pixel.
static void StarBlit(const BYTE *pImage, int nX, int nY, bool bDirty)
{
  const int nW = *reinterpret_cast<const WORD *>(pImage);
  const int nH = *reinterpret_cast<const WORD *>(pImage + 2);
  const int nViewW = g_portView.nWidth;
  const int nRows = g_portView.nMapRows;
  const int nRight = nX + nW;
  const int nBottom = nY + nH;

  if (nX >= nViewW || nRight <= 0 || nY >= nRows || nBottom <= 0)
    return;
  if (bDirty) {
    if (nW < 2 && nH < 2) {
      if (ScrollInval(nY / 16, nX / 16) == 0)
        return;
    } else {
      const int nLeftCell = nX / 16;
      const int nTopCell = nY / 16;
      const int nRightCell = (nRight - 1) / 16;
      const int nBotCell = (nBottom - 1) / 16;
      if (ScrollInval(nTopCell, nLeftCell) == 0 &&
          ScrollInval(nTopCell, nRightCell) == 0 &&
          ScrollInval(nBotCell, nLeftCell) == 0 &&
          ScrollInval(nBotCell, nRightCell) == 0)
        return;
    }
  }

  if (PortSceneStar(pImage + 4, nX, nY, nW, nH))  // Hyperion: the GPU scene (port_scene.cpp)
    return;
  const BYTE *pSrc = pImage + 4;
  int nVisW = nW;
  int nVisH = nH;
  if (nX < 0) {
    nVisW = nRight;
    pSrc -= nX;
    nX = 0;
  } else if (nRight >= nViewW) {
    nVisW = nViewW - nX;
  }
  if (nY < 0) {
    nVisH = nBottom;
    pSrc -= nW * nY;
    nY = 0;
  } else if (nBottom >= nRows) {
    nVisH = nRows - nY;
  }

  BYTE *pDst = dword_6D0B8C + nY * nViewW + nX;
  while (nVisH-- != 0) {
    for (int n = nVisW; n != 0; --n) {
      if (*pDst == 0)
        *pDst = *pSrc;
      ++pDst;
      ++pSrc;
    }
    pDst += nViewW - nVisW;
    pSrc += nW - nVisW;
  }
}

// Every star of the five layers, at each copy of the field across the view.
static void StarsDraw(bool bDirty)
{
  const starent_t *pStar = sgLayerStars_g[0];
  int i = 5;

  while (i-- != 0) {
    const int nOfsX = dword_543258[i] >> 8;
    const int nOfsY = dword_54326C[i] >> 8;
    for (int nCount = sgLayerCounts_g[i]; nCount != 0; --nCount, ++pStar) {
      const int nY = StarWrap(pStar->wY + nOfsY, STAR_FIELD_HEIGHT);
      for (int nX = StarWrap(pStar->wX + nOfsX, STAR_FIELD_WIDTH); nX < g_portView.nWidth;
           nX += STAR_FIELD_WIDTH)
        StarBlit(pStar->pbImage, nX, nY, bDirty);
    }
  }
}

// Draw the stars over the whole back buffer.
void DrawStars() {
  StarsDraw(false);
}

// The same, but only the stars whose 16 by 16 screen cells are marked for
// redraw.
void DrawStarsDirty() {
  StarsDraw(true);
}
