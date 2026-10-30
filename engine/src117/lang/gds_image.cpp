// gds_image.cpp -- the screen-image layer. Eight overlay images are composited
// onto the draw buffer in falling index order, a byte per 16 by 16 screen cell
// records what changed, and the mouse cursor (image 0) is saved and restored
// around the pass. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gds_image.h"
#include "../h/img.h"          // img_t, img_invalid, img_force
#include "../h/dlg_dlgdraw.h"  // img_area_invalid, img_area_is_invalid
#include "../h/cur.h"          // img_cursor_invalid
#include "../h/gds_vidblit.h"  // vid_copy_rect, vid_draw_mask
#include "../h/Storm.h"        // SMemAlloc, SMemFree, HSTRANS (pulls <windows.h>)
#include "../port/render/port_mask.h"  // Hyperion: PortMaskDelete, PortMaskIntersectDirty
#include "../port/render/port_scene.h"  // Hyperion: PortSceneScreenDone
#include "../port/render/port_present.h"  // Hyperion: PortScreenWrites
#include "../port/render/port_view.h"  // Hyperion: g_portView, PortViewClearGrid, PortViewLayerDrawn
#include <string.h>            // memset

// img_init's two memsets are 0xA0 and 0x4B0 bytes and img_update_real's second
// is 0x4B0, all of which /Oi would expand inline; the oracle calls the CRT.
#pragma function(memset)

// img_invalid and img_force fall off the end in the oracle: EAX at the `ret` is
// whatever img_area_invalid left. img.h declares both `int`, and every caller
// discards the result, so they return 0 here: falling off the end of a function
// that returns a value is undefined in C++, which clang and gcc act on.

#include "../gen/globals117.h"

// The two transparency-mask imports Storm.h does not carry. STransDelete is
// spelled exactly as gds_vidblit.cpp declares it.
extern "C" {
BOOL __stdcall STransDelete(HSTRANS hTrans);  // 0x4D0130 -> [0x4EB4E8] -> ord 434
BOOL __stdcall STransIntersectDirtyArray(HSTRANS hTrans, const BYTE *pbDirty,
                                         int nCellShift,
                                         HSTRANS *phOut);  // 0x4D034C -> [0x4EB49C] -> ord 437
}  // extern "C"

// The globals this TU reaches, dual-spelled, one #ifdef per name.
// _img_draw_buf (0x6D0B88) and _bmp_current (0x6D0C40) come from
// ../gen/globals117.h.

// 0x50F0F0 gbFadedOut, a BYTE of palette.cpp's: when set, the whole buffer is
// cleared and blitted instead of being composited image by image.
#ifndef OC_GB_FADED_OUT
#define OC_GB_FADED_OUT
#ifdef OC_DLL_MODE
#define gbFadedOut (*reinterpret_cast<BYTE *>(OC_VA(0x50F0F0u)))
#else
extern BYTE gbFadedOut;
#endif
#endif

// 0x6D06D0 gbInvalMap: one byte per 16 by 16 screen cell over the 640 by 480
// buffer, 40 by 30. mask.cpp declares the same address [40 * 25], the 640 by
// 400 play area.
// Hyperion: the grid is the view's now,
// g_portView.pbInvalMap with rows g_portView.nGridWidth apart, sized with the
// screen (port_view.cpp, docs/renderer.md R3).

// 0x6D0B80 _img_cursor_save: the pixels under the cursor, a bmp_t. img_init
// allocates its 0x900 bytes and save_behind_cursor fills in the dimensions.
#ifndef OC_IMG_CURSOR_SAVE
#define OC_IMG_CURSOR_SAVE
#ifdef OC_DLL_MODE
#define _img_cursor_save (*reinterpret_cast<bmp_t *>(OC_VA(0x6D0B80u)))
#else
extern bmp_t _img_cursor_save;
#endif
#endif

// 0x6D0B90 _img_list, img_t[8] with a stride of 0x14, is declared in img.h.

// 0x6D0C30 sgCursorSaveRect: the rectangle save_behind_cursor took, a 12-byte
// bmpsrc_t. Only this TU reaches it.
#ifndef OC_SG_CURSOR_SAVE_RECT
#define OC_SG_CURSOR_SAVE_RECT
#ifdef OC_DLL_MODE
#define sgCursorSaveRect (*reinterpret_cast<bmpsrc_t *>(OC_VA(0x6D0C30u)))
#else
extern bmpsrc_t sgCursorSaveRect;
#endif
#endif

// 0x6D0C3C sgbHasSavedCursor_g, a DWORD: set once save_behind_cursor has run.
#ifndef OC_SGB_HAS_SAVED_CURSOR_G
#define OC_SGB_HAS_SAVED_CURSOR_G
#ifdef OC_DLL_MODE
#define sgbHasSavedCursor_g (*reinterpret_cast<int *>(OC_VA(0x6D0C3Cu)))
#else
extern int sgbHasSavedCursor_g;
#endif
#endif

// 0x6D0C44 sgConsoleMask_g: the console transparency mask;
// STransIntersectDirtyArray takes it and the dirty map and writes the
// intersection to the handle below.
#ifndef OC_SG_CONSOLE_MASK_G
#define OC_SG_CONSOLE_MASK_G
#ifdef OC_DLL_MODE
#define sgConsoleMask_g (*reinterpret_cast<HSTRANS *>(OC_VA(0x6D0C44u)))
#else
extern HSTRANS sgConsoleMask_g;
#endif
#endif

// 0x6D0C48 dword_6D0C48: the intersected mask, deleted and rebuilt every pass
// here, and read by vid_draw_mask as STransBltUsingMask's mask argument.
#ifndef OC_DWORD_6D0C48
#define OC_DWORD_6D0C48
#ifdef OC_DLL_MODE
#define dword_6D0C48 (*reinterpret_cast<HSTRANS *>(OC_VA(0x6D0C48u)))
#else
extern HSTRANS dword_6D0C48;
#endif
#endif

// The .rdata owner tag img_init and img_free push, the string at 0x4F1310.
#define OC_IMAGE_FILE "image.cpp"


// The bodies, in oracle VA order.

// 0x4CAC50 (185 B). Marks every 16-pixel cell the inclusive rectangle touches.
// The four coordinates are clamped independently and each low bound returns
// outright when the high one is already off-screen, so the two arms of a pair
// are not symmetrical.
void __fastcall img_area_invalid(int nLeft, int nTop, int nRight, int nBottom)
{
  BYTE *pCell;
  int nCols;
  int y;

  // Hyperion: clamped to the view's screen, 640x480 in 1.17.
  if (nLeft < 0)
    nLeft = 0;
  else if (nLeft >= g_portView.nWidth)
    return;
  if (nRight < 0)
    return;
  if (nRight > g_portView.nWidth - 1)
    nRight = g_portView.nWidth - 1;
  if (nTop < 0)
    nTop = 0;
  else if (nTop >= g_portView.nHeight)
    return;
  if (nBottom < 0)
    return;
  if (nBottom > g_portView.nHeight - 1)
    nBottom = g_portView.nHeight - 1;

  nLeft /= 16;
  nTop /= 16;
  nRight /= 16;
  nBottom /= 16;
  nCols = nRight - nLeft + 1;
  pCell = &g_portView.pbInvalMap[nTop * g_portView.nGridWidth + nLeft];
  for (y = nTop; y <= nBottom; ++y) {
    memset(pCell, 1, nCols);
    pCell += g_portView.nGridWidth;
  }
}

// 0x4CAD10 (151 B). Asks whether any cell the rectangle touches is marked. The
// right and bottom edges are exclusive here, unlike img_area_invalid's: the
// caller passes x + width and y + height.
unsigned char __fastcall img_area_is_invalid(int nLeft, int nTop, int nRight,
                                             int nBottom)
{
  const BYTE *pCell;
  int nCols;
  int nAdvance;
  int nLastRow;
  int y;
  int x;

  nLeft /= 16;
  nTop /= 16;
  pCell = &g_portView.pbInvalMap[nTop * g_portView.nGridWidth + nLeft];
  nLastRow = (nBottom - 1) / 16;
  nCols = (nRight - 1) / 16 - nLeft + 1;
  nAdvance = g_portView.nGridWidth - nCols;
  for (y = nTop; y <= nLastRow;) {
    ++y;
    for (x = nCols; x-- != 0;) {
      if (*pCell++ != 0)
        return 1;
    }
    pCell += nAdvance;
  }
  return 0;
}

// 0x4CADB0 (58 B). img_invalid pinned to the cursor image, so it is nullary and
// reaches _img_list[0] by absolute address rather than by index.
void img_cursor_invalid()
{
  if (_img_list[0].bInUse) {
    _img_list[0].bFlags |= 1;
    img_area_invalid(_img_list[0].nX, _img_list[0].nY,
                     _img_list[0].nX + _img_list[0].nWidth - 1,
                     _img_list[0].nY + _img_list[0].nHeight - 1);
  }
}

// 0x4CADF0 (62 B). img_invalid without the in-use test, so a layer that is
// about to be shown repaints on its first pass.
int img_force(unsigned char nLayer)
{
  _img_list[nLayer].bFlags |= 1;
  img_area_invalid(_img_list[nLayer].nX, _img_list[nLayer].nY,
                   _img_list[nLayer].nX + _img_list[nLayer].nWidth - 1,
                   _img_list[nLayer].nY + _img_list[nLayer].nHeight - 1);
  return 0;
}

// 0x4CAE30 (35 B). The owner tag is line 0x78 of the oracle's image.cpp.
void img_free(void)
{
  if (_img_cursor_save.pPixels != 0) {
    SMemFree(_img_cursor_save.pPixels, OC_IMAGE_FILE, 0x78, 0);
    _img_cursor_save.pPixels = 0;
  }
}

// 0x4CAE60 (85 B). The draw buffer gets its dimensions but no pixels; the
// surface supplies those later.
void img_init(void)
{
  memset(_img_list, 0, sizeof(_img_list));
  // Hyperion: the grid and the draw buffer are the view's (port_view.cpp).
  PortViewClearGrid();
  _img_draw_buf.nWidth = static_cast<short>(g_portView.nWidth);
  _img_draw_buf.nHeight = static_cast<short>(g_portView.nHeight);
  _img_draw_buf.pPixels = 0;
  _img_cursor_save.pPixels =
      static_cast<BYTE *>(SMemAlloc(0x900, OC_IMAGE_FILE, 0x8D, 0));
}

// 0x4CAEC0 (72 B). Marks the layer's whole rectangle dirty and sets bit 0 of
// its flags. Every caller discards the `int` img.h gives it.
int img_invalid(unsigned char nLayer)
{
  if (_img_list[nLayer].bInUse) {
    _img_list[nLayer].bFlags |= 1;
    img_area_invalid(_img_list[nLayer].nX, _img_list[nLayer].nY,
                     _img_list[nLayer].nX + _img_list[nLayer].nWidth - 1,
                     _img_list[nLayer].nY + _img_list[nLayer].nHeight - 1);
  }
  return 0;
}

// 0x4CAF10 (415 B). One compositing pass. The images are walked from index 7
// down to 0 by pointer, so `pImg == &_img_list[0]` is a pointer compare against
// the array base. Flag bits: bit 0 and bit 5 force a
// draw, bit 2 records that the dirty map said so, bit 1 is a pending draw set
// elsewhere, and the three low bits are cleared afterwards.
void img_update_real(void)
{
  if (_img_draw_buf.pPixels == 0)
    return;
  _bmp_current = &_img_draw_buf;
  if (gbFadedOut) {
    memset(_img_draw_buf.pPixels, 0,
           _img_draw_buf.nWidth * _img_draw_buf.nHeight);

    // Hyperion: the black frame is copied when the screen has been written since it last
    // was, or the view has another size, not on every call. Each copy is a frame to present,
    // and a start on StarCraft from Battle.net, which has no title screen, loads the images'
    // files faded out, a call each, 8,000: 40 seconds of black at 120 Hz, longer at 60.
    static unsigned int s_nBlackAt = ~0u;
    static int s_nBlackW, s_nBlackH;
    if (PortScreenWrites() != s_nBlackAt || s_nBlackW != g_portView.nWidth ||
        s_nBlackH != g_portView.nHeight) {
      rect32_t rc = {0, 0, g_portView.nWidth, g_portView.nHeight};
      vid_copy_rect(&rc);
      s_nBlackAt = PortScreenWrites();
      s_nBlackW = g_portView.nWidth;
      s_nBlackH = g_portView.nHeight;
    }
  } else {
    img_t *pImg;
    bmpsrc_t src;
    BYTE bFlags;

    pImg = &_img_list[7];
    do {
      // Hyperion: a layer the zoomed map's pass drew with the map
      if (pImg->bInUse && PortViewLayerDrawn(static_cast<int>(pImg - &_img_list[0]))) {
        pImg->bFlags &= 0xF8;
        continue;
      }
      if (pImg->bInUse) {
        src.rc.left = -pImg->nX;
        src.rc.top = -pImg->nY;
        src.rc.right = src.rc.left + g_portView.nWidth - 1;
        src.rc.bottom = src.rc.top + g_portView.nHeight - 1;
        src.nWidth = g_portView.nWidth;
        src.nHeight = g_portView.nHeight;
        if (pImg == &_img_list[0])
          save_behind_cursor(&_img_draw_buf, 0, 0);
        bFlags = pImg->bFlags;
        if ((bFlags & 0x21) == 0) {
          int nDraw;

          if (img_area_is_invalid(pImg->nX, pImg->nY, pImg->nX + pImg->nWidth,
                                  pImg->nY + pImg->nHeight)) {
            bFlags |= 4;
            pImg->bFlags = bFlags;
            nDraw = 1;
          } else {
            nDraw = bFlags & 2;
          }
          if (nDraw == 0)
            continue;
        }
        pImg->pfnUpdate(0, 0, pImg->pBmp, &src);
        pImg->bFlags &= 0xF8;
      }
    } while (--pImg >= &_img_list[0]);
  }

  if (sgConsoleMask_g != 0 && dword_6D0C48 != 0) {
    // Hyperion: the port's masks (port_mask.cpp) in place of Storm's
    // STransDelete and STransIntersectDirtyArray.
    PortMaskDelete(dword_6D0C48);
    dword_6D0C48 = PortMaskIntersectDirty(sgConsoleMask_g, g_portView.pbInvalMap, 3);
    // Hyperion: the GPU scene's frame ends here, with what the screen takes
    // (port_scene.cpp).
    PortSceneScreenDone(dword_6D0C48);
    vid_draw_mask();
    PortViewClearGrid();
  } else {
    PortSceneScreenDone(NULL);
  }
  if (!gbFadedOut && _img_list[0].bInUse)
    restore_cursor_area(&_img_draw_buf, 0, 0);
  _bmp_current = 0;
}

// 0x4CB0B0 (135 B). Puts the saved pixels back where save_behind_cursor took
// them. Four guards, any of which means there is nothing to restore.
void restore_cursor_area(bmp_t *pBmp, int nDX, int nDY)
{
  bmpsrc_t src;
  bmp_t *pOld;

  if (_img_cursor_save.pPixels == 0)
    return;
  if (_img_cursor_save.nWidth == 0)
    return;
  if (_img_cursor_save.nHeight == 0)
    return;
  if (sgbHasSavedCursor_g == 0)
    return;

  src.rc.left = 0;
  src.rc.top = 0;
  src.nWidth = sgCursorSaveRect.nWidth;
  src.nHeight = sgCursorSaveRect.nHeight;
  src.rc.right = sgCursorSaveRect.nWidth - 1;
  src.rc.bottom = sgCursorSaveRect.nHeight - 1;
  pOld = static_cast<bmp_t *>(_bmp_current);
  _bmp_current = pBmp;
  bmp_copy_rect(sgCursorSaveRect.rc.left, sgCursorSaveRect.rc.top,
                &_img_cursor_save, &src);
  _bmp_current = pOld;
}

// 0x4CB140 (190 B). Copies whatever is under the cursor image into the save
// buffer, sizing both it and the source rectangle from _img_list[0], in 1.17's
// statement order.
void save_behind_cursor(bmp_t *pBmp, int nDX, int nDY)
{
  bmp_t *pOld;

  pOld = static_cast<bmp_t *>(_bmp_current);
  sgCursorSaveRect.rc.left = _img_list[0].nX - nDX;
  sgCursorSaveRect.rc.top = _img_list[0].nY - nDY;
  sgCursorSaveRect.rc.right =
      sgCursorSaveRect.rc.left + _img_list[0].nWidth - 1;
  sgCursorSaveRect.rc.bottom =
      sgCursorSaveRect.rc.top + _img_list[0].nHeight - 1;
  sgCursorSaveRect.nWidth = sgCursorSaveRect.rc.right - sgCursorSaveRect.rc.left + 1;
  sgCursorSaveRect.nHeight = sgCursorSaveRect.rc.bottom - sgCursorSaveRect.rc.top + 1;
  _img_cursor_save.nWidth = _img_list[0].nWidth;
  _img_cursor_save.nHeight = _img_list[0].nHeight;
  _bmp_current = &_img_cursor_save;
  bmp_copy_rect(0, 0, pBmp, &sgCursorSaveRect);
  _bmp_current = pOld;
  sgbHasSavedCursor_g = 1;
}
