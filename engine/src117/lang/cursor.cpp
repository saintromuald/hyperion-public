// cursor.cpp -- which cursor shows: the edge-scroll cursors, the minimap's, the
// console mask test, and the target cursors for what is under the pointer. The
// cursor table's 19 images are loaded here and drawn by cur.cpp. Bodies are in
// oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/cursor.h"
#include "../h/cur.h"
#include "../h/glues.h"
#include "../h/usel.h"
#include "../h/statbtn.h"
#include "../h/CUnit.h"
#include "../h/rect32.h"
#include "../h/Storm.h"
#include "../h/dlg_dlg.h"
#include "../h/bmp.h"

#include "../gen/globals117.h"
#include "../port/render/port_console.h"  // Hyperion: PortConsoleHidden, PortConsoleSplit
#include "../port/hud/port_hudinput.h"  // Hyperion: PortHudOver, PortHudConsoleCursor
#include "../port/render/port_mask.h"  // Hyperion: PortMaskHasPixel
#include "../port/render/port_present.h"  // Hyperion: PortCursorOverLayer
#include "../port/render/port_view.h"  // Hyperion: g_portView, PortViewScreenToMap

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// The cursor lock, the current cursor id and the 19 loaded cursor images.
#ifdef OC_DLL_MODE
#define sgbCursorLocked_g  (*reinterpret_cast<BYTE *>(OC_VA(0x665D68u)))
#else
extern BYTE sgbCursorLocked_g;
#endif

#ifdef OC_DLL_MODE
#define sgnCursor_g        (*reinterpret_cast<int *>(OC_VA(0x665D74u)))
#else
extern int sgnCursor_g;
#endif

#ifdef OC_DLL_MODE
#define sgpCursors_g       (reinterpret_cast<intptr_t *>(OC_VA(0x665D78u)))
#else
extern intptr_t sgpCursors_g[19] OC_UNALIGNED;
#endif

// PtInRect is USER32's, declared by <windows.h> (through Storm.h) as the
// dllimport 1.17 calls through its IAT slot; it takes the cursor position as a
// POINT by value. rect32_t and RECT are the same four LONGs, so the casts are
// at the call sites.

// sgMinimapArea_g -- rect32_t @0x508BB0, the minimap panel in the bottom left of
// the console, {6, 348, 133, 475} in 1.17's image.
#ifdef OC_DLL_MODE
  #ifndef sgMinimapArea_g
  #define sgMinimapArea_g    (*reinterpret_cast<rect32_t *>(OC_VA(0x508BB0u)))
  #endif
#else
extern rect32_t sgMinimapArea_g;
#endif

// gGameMapRect -- rect32_t @0x666430, spelled as src117/lang/usel.cpp spells it.
#ifdef OC_DLL_MODE
  #ifndef gGameMapRect
  #define gGameMapRect       (*reinterpret_cast<rect32_t *>(OC_VA(0x666430u)))
  #endif
#else
extern rect32_t gGameMapRect;
#endif

// gMapCursor -- WORD @0x667288, the cursor the minimap shows, spelled as
// src117/lang/placebox.cpp spells it.
#ifdef OC_DLL_MODE
  #ifndef gMapCursor
  #define gMapCursor         (*reinterpret_cast<WORD *>(OC_VA(0x667288u)))
  #endif
#else
extern WORD gMapCursor;
#endif

// gbSelectDragging -- BYTE @0x6CA720, set while a selection box is dragged,
// spelled as src117/lang/usel.cpp spells it.
#ifdef OC_DLL_MODE
  #ifndef gbSelectDragging
  #define gbSelectDragging   (*reinterpret_cast<BYTE *>(OC_VA(0x6CA720u)))
  #endif
#else
extern BYTE gbSelectDragging;
#endif

// DetermineCursor -- oracle 0x43E6D0 (106 B).
//
// The cursor for the pointer's screen position, in priority order: the minimap
// wins outright, then the console swallows the pointer, then the play area
// either drags or asks DetermineTargetCursor what is under the cursor, a tail
// call whose result is this one's.
int DetermineCursor()
{
  // Hyperion: the minimap where a HUD design shows it, the pointer taken through its slot
  //; 1.17's place for it is open map beside a column or above a lower HUD
  // Hyperion: a layer's panels over the game (an add-on's windows) are the console's,
  // with the arrow, whatever is under them
  if (PortCursorOverLayer(gCursorPos_g[0], gCursorPos_g[1]))
    return 0;
  LONG anAt[2];
  PortHudConsoleCursor(anAt);
  if (PtInRect(reinterpret_cast<const RECT *>(&sgMinimapArea_g),
                *reinterpret_cast<const POINT *>(anAt)))
    return gMapCursor;

  if (!console_collision(gCursorPos_g[0], gCursorPos_g[1])
      && PtInRect(reinterpret_cast<const RECT *>(&gGameMapRect),
                     *reinterpret_cast<const POINT *>(gCursorPos_g))) {
    if (gbSelectDragging)
      return 9;

    return DetermineTargetCursor();
  }

  return 0;
}

// is_enemy_unit -- oracle 0x43E740 (40 B). An enemy is a unit whose owner the
// local player has no alliance with, and while a replay plays there is none.
BOOL is_enemy_unit(CUnit *pUnit)
{
  if (sgbAlliances_g[g_LocalNationID][pUnit->bPlayer] == 0 && gPlayback == 0)
    return 1;

  return 0;
}

// cursor_destroy -- oracle 0x43E770 (46 B). The exit callback cursor_init
// registers: frees the 19 cursor images.
void cursor_destroy()
{
  cur_deinit();

  for (int i = 0; i < 19; ++i) {
    if (sgpCursors_g[i] != 0) {
      cur_free(sgpCursors_g[i]);
      sgpCursors_g[i] = 0;
    }
  }
}

// cursor_get -- oracle 0x43E7A0 (6 B).
int cursor_get()
{
  return sgnCursor_g;
}

// cursor_is_locked -- oracle 0x43E980 (6 B). A byte load, so the return is
// BYTE and not BOOL.
BYTE cursor_is_locked()
{
  return sgbCursorLocked_g;
}

// cursor_lock -- oracle 0x43E990 (8 B).
void cursor_lock()
{
  sgbCursorLocked_g = 1;
}

// cursor_unlock -- oracle 0x43EA30 (8 B).
void cursor_unlock()
{
  sgbCursorLocked_g = 0;
}

// cursor_set -- oracle 0x43E9A0 (27 B). The id is stored before the image is
// loaded, the reverse of cursor_update's order; it tail-calls cursor_set_image.
void cursor_set(int nCursorId)
{
  if (sgnCursor_g != nCursorId) {
    sgnCursor_g = nCursorId;
    cursor_set_image(sgpCursors_g[nCursorId]);
  }
}

// sgnConsoleStartY_g -- int @0x665D70. The first scanline of the console mask
// with any opaque pixel, which cursor_set_console_offset finds;
// console_collision compares it signed.
#ifdef OC_DLL_MODE
#define sgnConsoleStartY_g   (*reinterpret_cast<int *>(OC_VA(0x665D70u)))
#else
extern int sgnConsoleStartY_g;
#endif

// sgnConsoleAlwaysY_g -- int @0x665D6C. The first fully opaque scanline: at or
// below it every pixel is console, so no mask probe is needed. Signed.
#ifdef OC_DLL_MODE
#define sgnConsoleAlwaysY_g  (*reinterpret_cast<int *>(OC_VA(0x665D6Cu)))
#else
extern int sgnConsoleAlwaysY_g;
#endif

// sgnLastCollideX / sgnLastCollideY / sgbLastCollideResult_g -- @0x665DC4 /
// 0x665DC8 / 0x665DCC, the one-entry cache in front of the mask probe, all
// three 32-bit. They follow sgpCursors_g's 19 slots directly.
#ifdef OC_DLL_MODE
#define sgnLastCollideX      (*reinterpret_cast<int *>(OC_VA(0x665DC4u)))
#else
extern int sgnLastCollideX;
#endif

#ifdef OC_DLL_MODE
#define sgnLastCollideY      (*reinterpret_cast<int *>(OC_VA(0x665DC8u)))
#else
extern int sgnLastCollideY;
#endif

#ifdef OC_DLL_MODE
#define sgbLastCollideResult_g (*reinterpret_cast<BOOL *>(OC_VA(0x665DCCu)))
#else
extern BOOL sgbLastCollideResult_g;
#endif

// sgConsoleMask_g -- HSTRANS @0x6D0C44, the console's transparency mask, the
// handle STransIsPixelInMask takes. The generated header does not declare it.
#ifdef OC_DLL_MODE
  #ifndef sgConsoleMask_g
  #define sgConsoleMask_g    (*reinterpret_cast<HSTRANS *>(OC_VA(0x6D0C44u)))
  #endif
#else
extern HSTRANS sgConsoleMask_g;
#endif

// sgCursorFiles -- const char *[19] @0x5031C0, the cursor GRP names: arrow,
// Illegal, three Targ, three Mag, Drag, Time, then the eight Scroll* directions.
#ifdef OC_DLL_MODE
#define sgCursorFiles        (reinterpret_cast<const char **>(OC_VA(0x5031C0u)))
#else
extern const char *sgCursorFiles[19] OC_UNALIGNED;
#endif

// STransIsPixelInMask -- Storm.dll ordinal 442, three __stdcall arguments.
// 1.17 calls it through a thunk, so no dllimport; extern "C" gives the name
// _STransIsPixelInMask@12.
extern "C" {
int STORMAPI STransIsPixelInMask(HSTRANS hTrans, int nX, int nY);
}


// console_collision -- oracle 0x43E890 (83 B). Is the point over the console?
// Two band tests bracket a one-entry cache in front of the per-pixel mask probe.
// The result is the negation of the mask call, 1 where the mask has no pixel.
int console_collision(int nX, int nY)
{
  // Hyperion: a benchmark's run has no console, and the map takes its clicks;
  // a HUD design's console is its panels, wherever they stand, a column's upper part and the
  // resources at the top as well (port_hudinput.cpp). Both asked before 1.17's band.
  // A layer's panels over the game, an add-on's windows, are console too.
  if (PortCursorOverLayer(nX, nY))
    return 1;
  if (PortConsoleHidden())
    return 0;
  if (PortConsoleHudOn())
    return PortHudOver(nX, nY) ? 1 : 0;
  if (nY < sgnConsoleStartY_g)
    return 0;

  // Hyperion: across the console's 640 columns (port_view.cpp); beside
  // it a wider view shows the map, which the mask below answers for.
  if (nY >= sgnConsoleAlwaysY_g && nX >= g_portView.nConsoleX
      && nX < g_portView.nConsoleX + 640 && !PortConsoleSplit())
    return 1;

  if (sgnLastCollideX == nX && sgnLastCollideY == nY)
    return sgbLastCollideResult_g;

  sgnLastCollideX = nX;
  sgnLastCollideY = nY;
  // Hyperion: the port's mask (port_mask.cpp); was STransIsPixelInMask.
  sgbLastCollideResult_g = !PortMaskHasPixel(sgConsoleMask_g, nX, nY);

  return sgbLastCollideResult_g;
}


// cursor_set_console_offset -- oracle 0x43E9C0 (105 B). Two scans down the
// console mask bitmap, each row walked backwards: the first stops at the first
// row with any opaque pixel, the console's top edge; the second walks on to the
// first row with no transparent pixel, below which the console is solid. The
// second loop's `x--` leaves -1 when the row ran out, which `x < 0` tests. The
// width is a short local, as 1.17 holds it.
void cursor_set_console_offset(const bmp_t *pMask)
{
  const unsigned char *pRow = pMask->pPixels;
  int nHeight = pMask->nHeight;
  int y = 0;

  while (y < nHeight) {
    short nWidth = pMask->nWidth;
    int x = nWidth;

    while (x-- != 0) {
      if (pRow[x] != 0)
        goto found_start;
    }

    ++y;
    pRow += nWidth;
  }

found_start:
  sgnConsoleStartY_g = y;

  while (y < nHeight) {
    short nWidth = pMask->nWidth;
    int x = nWidth;

    while (x-- != 0) {
      if (pRow[x] == 0)
        break;
    }

    if (x < 0)
      break;

    ++y;
    pRow += nWidth;
  }

  sgnConsoleAlwaysY_g = y;
}

// cursor_init -- oracle 0x43E8F0 (133 B). Loads all 19 cursor GRPs from
// cursor\<name>.grp, then selects cursor 0. The image is read into a local
// before the id is stored, the order 1.17 has.
void cursor_init()
{
  char szPath[0x104];

  app_add_exit(cursor_destroy);

  for (int i = 0; i < 19; ++i) {
    SStrCopy(szPath, "cursor\\", sizeof(szPath));
    SStrPack(szPath, sgCursorFiles[i], sizeof(szPath));
    sgpCursors_g[i] = cur_load(szPath);
  }

  intptr_t nImage = sgpCursors_g[0];
  sgnCursor_g = 0;
  cursor_set_image(nImage);

  cur_display_init();
}

// dword_6A5ECC -- int @0x6A5ECC, set by scroll_update right after it commits a
// scroll and cleared here.
#ifdef OC_DLL_MODE
#define dword_6A5ECC         (*reinterpret_cast<int *>(OC_VA(0x6A5ECCu)))
#else
extern int dword_6A5ECC;
#endif

// sgnCacheCursorX_g / sgnCacheCursorY_g -- int @0x503220 / 0x503224, the last
// mouse position cursor_get_scroll_cursor acted on, -1 / -1 until it sees one.
#ifdef OC_DLL_MODE
#define sgnCacheCursorX_g    (*reinterpret_cast<int *>(OC_VA(0x503220u)))
#else
extern int sgnCacheCursorX_g;
#endif

#ifdef OC_DLL_MODE
#define sgnCacheCursorY_g    (*reinterpret_cast<int *>(OC_VA(0x503224u)))
#else
extern int sgnCacheCursorY_g;
#endif

// sgScrollCursorTbl -- WORD[9] @0x50320C, the scroll cursors as a 3x3 grid of
// directions, row by row from the top left, the plain arrow in the centre:
// {18, 11, 12, 17, 0, 13, 16, 15, 14}, indexed (y + 1) * 3 + (x + 1).
#ifdef OC_DLL_MODE
#define sgScrollCursorTbl    (reinterpret_cast<WORD *>(OC_VA(0x50320Cu)))
#else
extern WORD sgScrollCursorTbl[9] OC_UNALIGNED;
#endif

// glMapPixelRgt / glMapPixelBtm -- DWORD @0x543220 / 0x543224, the furthest the
// view can scroll right and down. Unsigned: the compares against the signed
// scroll origin are unsigned in 1.17.
#ifdef OC_DLL_MODE
#define glMapPixelRgt        (*reinterpret_cast<DWORD *>(OC_VA(0x543220u)))
#else
extern DWORD glMapPixelRgt;
#endif

#ifdef OC_DLL_MODE
#define glMapPixelBtm        (*reinterpret_cast<DWORD *>(OC_VA(0x543224u)))
#else
extern DWORD glMapPixelBtm;
#endif

// mou_get_pos (0x4CE890, gds/mou_win.cpp) fills two int out-parameters, ECX the
// x and EDX the y.

// cursor_get_scroll_cursor -- oracle 0x43E7B0 (222 B). Returns the scroll cursor
// for the pointer's screen position, or 0 when it is at no edge, and reports the
// axis deltas through the out-parameters, which are read back for the lookup
// since the caller's pointers could alias. cursor_update asks DetermineCursor
// only when this answers 0. nX, nY and the two scroll origins are copied once,
// as 1.17 reads them, before the out-parameters are written.
int cursor_get_scroll_cursor(int *pnScrollX, int *pnScrollY)
{
  int nMouseX, nMouseY;

  mou_get_pos(&nMouseX, &nMouseY);

  int nX = nMouseX;
  int nY = nMouseY;

  if (dword_6A5ECC) {
    if (sgnCacheCursorX_g == nX && sgnCacheCursorY_g == nY)
      return 0;

    dword_6A5ECC = 0;
  }

  LONG lScrollX = glScrollPixelX;

  sgnCacheCursorX_g = nX;
  sgnCacheCursorY_g = nY;

  // Hyperion: the edge bands are the view's screen's last two pixels,
  // 0x27E and 0x1DE in 1.17.
  if (nX <= 1 && lScrollX != 0)
    *pnScrollX = -1;
  else if (nX >= g_portView.nWidth - 2 && lScrollX < glMapPixelRgt)
    *pnScrollX = 1;
  else
    *pnScrollX = 0;

  LONG lScrollY = glScrollPixelY;

  if (nY <= 1 && lScrollY != 0)
    *pnScrollY = -1;
  else if (nY >= g_portView.nHeight - 2 && lScrollY < glMapPixelBtm)
    *pnScrollY = 1;
  else
    *pnScrollY = 0;

  if (*pnScrollX == 0 && *pnScrollY == 0)
    return 0;

  return sgScrollCursorTbl[(*pnScrollY + 1) * 3 + (*pnScrollX + 1)];
}


// cursor_update
//
// The per-frame cursor decision. The scroll cursor takes precedence, and only
// when the pointer is at no screen edge is DetermineCursor asked. cur_animate
// runs even while the cursor is locked.
void cursor_update()
{
  int nScrollX, nScrollY;
  int nCursor;

  if (!sgbCursorLocked_g) {
    nCursor = cursor_get_scroll_cursor(&nScrollX, &nScrollY);
    if (nCursor == 0)
      nCursor = DetermineCursor();

    if (sgnCursor_g != nCursor) {
      intptr_t nImage = sgpCursors_g[nCursor];
      sgnCursor_g = nCursor;
      cursor_set_image(nImage);
    }
  }

  cur_animate();
}

// DetermineTargetCursor -- oracle 0x43EA90 (130 B), below. What is under the
// cursor in map space, as a cursor id: while a target is being chosen, 5 for
// none, 2 for a neutral unit, 3 for an enemy and 4 for another; otherwise 8, 7
// and 6 for the same, and 0 for nothing or while a building is being placed.

// Hyperion: the unit under the cursor, kept for the map point, the game frame and the
// vision it was found with. The loop asks every
// pass, many between two frames, and on a large map the search walks a strip of the map's
// whole height; nothing it reads changes between frames. RunGameLoop forgets it at a game's
// start (cursor_forget_target).
static struct {
  bool bOn;
  unsigned int nGame, nFrame;
  int nMapX, nMapY;
  DWORD dwLocal, dwReplay, dwShowAll, dwPlayback;
  CUnit *pTarget;
} s_target;
static unsigned int s_nTargetGame;
extern unsigned int oc_dwReplayFrame;  // 0x52651C, the frame counter
extern DWORD gdwReplayVisionMask;      // 0x67CC84
extern DWORD sgbShowAll_g;             // 0x67CC88

void cursor_forget_target()
{
  ++s_nTargetGame;
}

static CUnit *CursorTarget(int nMapX, int nMapY)
{
  if (s_target.bOn && s_target.nGame == s_nTargetGame && s_target.nFrame == oc_dwReplayFrame &&
      s_target.nMapX == nMapX && s_target.nMapY == nMapY && s_target.dwLocal == gdwLocalMask &&
      s_target.dwReplay == gdwReplayVisionMask && s_target.dwShowAll == sgbShowAll_g &&
      s_target.dwPlayback == gPlayback)
    return s_target.pTarget;
  s_target.pTarget = get_map_target(nMapX, nMapY);
  s_target.bOn = true;
  s_target.nGame = s_nTargetGame;
  s_target.nFrame = oc_dwReplayFrame;
  s_target.nMapX = nMapX;
  s_target.nMapY = nMapY;
  s_target.dwLocal = gdwLocalMask;
  s_target.dwReplay = gdwReplayVisionMask;
  s_target.dwShowAll = sgbShowAll_g;
  s_target.dwPlayback = gPlayback;
  return s_target.pTarget;
}

int DetermineTargetCursor()
{
  if (placebox_is_on())
    return 0;

  // Hyperion: through the camera (port_view.cpp)
  int nMapX, nMapY;
  PortViewScreenToMap(gCursorPos_g[0], gCursorPos_g[1], &nMapX, &nMapY);

  CUnit *pTarget = CursorTarget(nMapX, nMapY);

  if (target_is_on()) {
    if (pTarget == 0)
      return 5;

    if (pTarget->bPlayer == 0x0B)
      return 2;

    return is_enemy_unit(pTarget) ? 3 : 4;
  }

  if (pTarget != 0) {
    if (pTarget->bPlayer == 0x0B)
      return 8;

    return is_enemy_unit(pTarget) ? 7 : 6;
  }

  return 0;
}
