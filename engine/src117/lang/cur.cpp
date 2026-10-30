// cur.cpp -- the cursor's graphic and its animation, drawn as image layer 0: the
// animation tick, the layer's draw callback, loading and relocating a cursor
// group, and the show/hide pair. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/cur.h"
#include "../h/glues.h"        // ours: cur_animate, cur_load
#include "../h/event_Win.h"    // ours: cur_update
#include "../h/TrigAct.h"      // ours: cur_hide, cur_show
#include "../h/img.h"
#include "../h/bmp.h"
#include "../h/CImage.h"       // rle_draw_normal
#include "../h/dlg_dlgdraw.h"  // UpdateCursorRect
#include "../h/Storm.h"        // SMemFree
#include "../h/grp.h"          // Hyperion: FRAME, which curframe_t is
#include "../port/render/port_hwcursor.h"  // Hyperion: PortHwCursorShowing

#include "../gen/globals117.h"

// ReadGroupFile, 0x444A60, spelled exactly as src117/lang/glues.cpp and
// opt_dlgs.cpp spell it: a declaration that differs mangles differently.
struct GROUP;
GROUP *__fastcall ReadGroupFile(const char *pszFile, intptr_t nFlags,
                                const char *pszTag, int nLine);

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block. _bmp_current @0x6D0C40 and gCursorPos_g @0x6D3154 come from
// src117/gen/globals117.h and are not repeated here.

// sgdwCurFrame_g @0x665D58 -- the free-running animation frame counter, divided
// unsigned by the frame count.
#ifdef OC_DLL_MODE
#define sgdwCurFrame_g     (*reinterpret_cast<DWORD *>(OC_VA(0x665D58u)))
#else
extern DWORD sgdwCurFrame_g;
#endif

// sgdwCurNextTick_g @0x665D5C -- the next animation deadline, in GetTickCount
// milliseconds; cur_animate compares it wrap-safely.
#ifdef OC_DLL_MODE
#define sgdwCurNextTick_g  (*reinterpret_cast<DWORD *>(OC_VA(0x665D5Cu)))
#else
extern DWORD sgdwCurNextTick_g;
#endif

// sgpCurAnim_g @0x665D60 -- the loaded cursor animation record. Its leading
// WORD is the frame count, so the pointer is a `WORD *`.
#ifdef OC_DLL_MODE
#define sgpCurAnim_g       (*reinterpret_cast<WORD **>(OC_VA(0x665D60u)))
#else
extern WORD *sgpCurAnim_g;
#endif



// One frame of the cursor animation record sgpCurAnim_g points at. The record is
// a 6-byte header, the frame count in the low 15 bits of its first WORD (one
// frame means the cursor does not animate) and four bytes nothing reads, then
// the 8-byte frames from +0x06.
#pragma pack(push, 1)  // Hyperion: as FRAME is (grp.h)
struct curframe_t {
  // Hyperion: FRAME's layout at 64 bits, its place and size in WORDs, the file's dword,
  // then the pointer the loader made and the line table a cursor has none of
  // (src117/h/grp.h).
  /*0x00*/ frame_dim_t nX;
  /*0x01*/ frame_dim_t nY;
  /*0x02*/ frame_dim_t nWidth;
  /*0x03*/ frame_dim_t nHeight;
#if defined(_WIN64) || defined(__LP64__)
  DWORD dwData;
#endif
  /*0x04*/ unsigned char *pData;   // cur_load relocates it: (off & 0x7FFFFFFF) + base
#if defined(_WIN64) || defined(__LP64__)
  const DWORD *pLines;
#endif
};
#pragma pack(pop)
static_assert(sizeof(curframe_t) == sizeof(FRAME) &&
              offsetof(curframe_t, pData) == offsetof(FRAME, pData),
              "curframe_t is a FRAME, which rle_draw_normal is handed");
OC_X86_LAYOUT(sizeof(curframe_t) == 0x08, "curframe_t size   cur_update: lea ecx, dword ptr [ecx+edx*8h] -- stride 8");
static_assert(offsetof(curframe_t, nX) == 0x00, "curframe_t::nX      cur_update: movzx eax, byte ptr [ecx]");
OC_X86_LAYOUT(offsetof(curframe_t, nY) == 0x01, "curframe_t::nY      cur_update: movzx eax, byte ptr [ecx+1h]");
OC_X86_LAYOUT(offsetof(curframe_t, nWidth) == 0x02, "curframe_t::nWidth  cur_update: movzx eax, byte ptr [ecx+2h]");
OC_X86_LAYOUT(offsetof(curframe_t, nHeight) == 0x03, "curframe_t::nHeight cur_update: movzx eax, byte ptr [ecx+3h]");
static_assert(offsetof(curframe_t, nWidth) == offsetof(FRAME, bWidth) &&
                  offsetof(curframe_t, nHeight) == offsetof(FRAME, bHeight),
              "curframe_t's size is FRAME's");
OC_X86_LAYOUT(offsetof(curframe_t, pData) == 0x04, "curframe_t::pData   cur_load: mov eax, dword ptr [ecx-8h] at frame+4");

// rle_draw_normal's fourth argument, a local of cur_display_update: the source x
// and y, 0 unless the destination clipped negative, and the width and height
// clamped to the destination bitmap.
struct curdraw_t {
  /*0x00*/ int nSrcX;
  /*0x04*/ int nSrcY;
  /*0x08*/ int nWidth;
  /*0x0C*/ int nHeight;
};
static_assert(sizeof(curdraw_t) == 0x10, "curdraw_t size     lea eax, dword ptr [ebp-14h] over four dwords");
static_assert(offsetof(curdraw_t, nSrcX) == 0x00, "curdraw_t::nSrcX   [ebp-14h]");
static_assert(offsetof(curdraw_t, nSrcY) == 0x04, "curdraw_t::nSrcY   [ebp-10h]");
static_assert(offsetof(curdraw_t, nWidth) == 0x08, "curdraw_t::nWidth  [ebp-0Ch]");
static_assert(offsetof(curdraw_t, nHeight) == 0x0C, "curdraw_t::nHeight [ebp-08h]");

// The bodies, in oracle VA order.

// GetTickCount, KERNEL32's, called through its IAT slot as dlg_dlg.cpp and
// event_Win.cpp call it.
extern "C" __declspec(dllimport) DWORD __stdcall GetTickCount(void);            // 0x4EB2FC

// cur_animate
//
// The cursor's animation tick, skipped when there is no animation record or it
// has one frame. The deadline test reads `now - deadline` signed, which is safe
// across GetTickCount's wrap; comparing the two values directly is not. It
// tail-calls cur_update.
void cur_animate()
{
  DWORD dwNow;

  if (sgpCurAnim_g == NULL || *sgpCurAnim_g == 1)
    return;

  dwNow = GetTickCount();
  if ((LONG)(dwNow - sgdwCurNextTick_g) < 0)
    return;

  sgdwCurNextTick_g = dwNow + 100;
  ++sgdwCurFrame_g;
  cur_update();
}

// 0x43E450 cur_deinit (18 B). Takes the cursor layer out of service and forgets
// the animation record; it does not free it, cur_free does.
void cur_deinit()
{
  _img_list[0].bInUse = 0;
  sgpCurAnim_g = NULL;
}

// 0x43E470 cur_display_init (29 B). Installs cur_display_update as layer 0's
// draw callback and starts the animation clock. The layer stays out of service
// until cur_show turns it on.
void cur_display_init()
{
  _img_list[0].bInUse = 0;
  _img_list[0].pfnUpdate = cur_display_update;
  sgdwCurNextTick_g = GetTickCount();
}

// 0x43E490 cur_display_update (203 B). Layer 0's draw callback: blits the
// current animation frame at the cursor's position minus the caller's origin,
// clipped to the destination bitmap. The WORD parameters are cast to short at
// their use, which reloads them sign-extended as 1.17 does. The `pLayer` local
// keeps the layer's loads after those reloads in the DLL build, where spelling
// `_img_list[0].nX` directly transposes them; do not fold it back.
void __fastcall cur_display_update(WORD wArg0, WORD wArg1, bmp_t *nArg2, bmpsrc_t *nArg3)
{
  curdraw_t draw;
  WORD *pAnim;
  curframe_t *pFrame;
  int nX;
  int nY;
  img_t *pLayer;

  (void)nArg2;
  (void)nArg3;

  // Hyperion: with Settings' "Hardware cursor" the system draws it, and over a HUD design
  // the overlay (port_hwcursor.cpp).
  if (PortHwCursorShowing() || PortCursorAbove())
    return;

  pAnim = sgpCurAnim_g;
  if (pAnim == NULL)
    return;

  pFrame = reinterpret_cast<curframe_t *>(reinterpret_cast<char *>(pAnim) + 6)
           + sgdwCurFrame_g % *pAnim;
  if (pFrame == NULL)
    return;

  pLayer = &_img_list[0];
  nX = pLayer->nX - static_cast<short>(wArg0);
  nY = pLayer->nY - static_cast<short>(wArg1);

  draw.nSrcX = 0;
  draw.nSrcY = 0;
  draw.nWidth = pFrame->nWidth;
  draw.nHeight = pFrame->nHeight;

  if (nX < 0) {
    draw.nWidth += nX;
    draw.nSrcX = -nX;
    nX = 0;
  }
  if (draw.nWidth >= reinterpret_cast<bmp_t *>(_bmp_current)->nWidth - nX)
    draw.nWidth = reinterpret_cast<bmp_t *>(_bmp_current)->nWidth - nX;

  if (nY < 0) {
    draw.nHeight += nY;
    draw.nSrcY = -nY;
    nY = 0;
  }
  if (draw.nHeight >= reinterpret_cast<bmp_t *>(_bmp_current)->nHeight - nY)
    draw.nHeight = reinterpret_cast<bmp_t *>(_bmp_current)->nHeight - nY;

  rle_draw_normal(nX, nY, pFrame, &draw, NULL);
}

// 0x43E560 cur_free (35 B). Frees one cursor animation record, clearing
// sgpCurAnim_g if it was the live one. "cur.cpp" and 254 are 1.17's own file
// name and __LINE__, kept as literals.
void cur_free(intptr_t nArg0)
{
  WORD *pAnim = sgpCurAnim_g;

  if (pAnim == reinterpret_cast<WORD *>(nArg0))
    pAnim = NULL;
  sgpCurAnim_g = pAnim;

  SMemFree(reinterpret_cast<void *>(nArg0), "cur.cpp", 254, 0);
}

// 0x43E590 cur_hide (15 B). Dirties layer 0, then takes it out of service;
// cur_show does the same two things the other way round.
void __fastcall cur_hide(void)
{
  img_invalid(0);
  _img_list[0].bInUse = 0;
}

// 0x43E5A0 cur_load (59 B). Loads a cursor group and relocates it in place:
// every frame's +4 dword arrives as a file offset with bit 31 set and leaves as
// a real pointer. The walk runs backwards from the last frame to the first.
intptr_t cur_load(const char *pszPath)
{
  unsigned char *pBase;
  unsigned int nFrames;
  curframe_t *pFrame;

  pBase = reinterpret_cast<unsigned char *>(
      ReadGroupFile(pszPath, 0, "cur.cpp", 261));

#if defined(_WIN64) || defined(__LP64__)
  // Hyperion: ReadGroupFile has made each pData the pointer already
  // (PortGroupNative).
  (void)nFrames;
  (void)pFrame;
#else
  nFrames = *reinterpret_cast<WORD *>(pBase) & 0x7FFF;
  if (nFrames != 0) {
    pFrame = reinterpret_cast<curframe_t *>(pBase + 6) + nFrames;
    do {
      --pFrame;
      pFrame->pData = pBase
          + (reinterpret_cast<DWORD_PTR>(pFrame->pData) & 0x7FFFFFFF);
    } while (--nFrames != 0);
  }
#endif

  return reinterpret_cast<intptr_t>(pBase);
}

// 0x43E5E0 cursor_set_image (20 B). Points the cursor at a different animation
// record, redrawing only on a real change. cursor.cpp's cursor_set tail-jumps
// here with a slot of the 19-entry cursor pointer table.
void cursor_set_image(intptr_t nArg0)
{
  if (sgpCurAnim_g != reinterpret_cast<WORD *>(nArg0)) {
    sgpCurAnim_g = reinterpret_cast<WORD *>(nArg0);
    cur_update();
  }
}

// 0x43E600 cur_show (14 B). Puts layer 0 in service and dirties it, cur_hide's
// mirror.
void __fastcall cur_show(void)
{
  _img_list[0].bInUse = 1;
  img_invalid(0);
}

// 0x43E610 cur_update (125 B). Recomputes layer 0's rectangle from the current
// animation frame and the mouse position, invalidating the old rectangle and the
// new one. sgpCurAnim_g is read again after the two calls, as 1.17 does, and its
// second null test is 1.17's too.
void cur_update()
{
  WORD *pAnim;
  curframe_t *pFrame;

  if (sgpCurAnim_g == NULL)
    return;

  img_cursor_invalid();
  UpdateCursorRect();

  pAnim = sgpCurAnim_g;
  pFrame = pAnim == NULL
      ? NULL
      : reinterpret_cast<curframe_t *>(reinterpret_cast<char *>(pAnim) + 6)
            + sgdwCurFrame_g % *pAnim;

  _img_list[0].nWidth = pFrame->nWidth;
  _img_list[0].nHeight = pFrame->nHeight;
  _img_list[0].nX = pFrame->nX - 0x3F + static_cast<short>(gCursorPos_g[0]);
  _img_list[0].nY = pFrame->nY - 0x3F + static_cast<short>(gCursorPos_g[1]);

  img_cursor_invalid();
  UpdateCursorRect();
}

// 0x43E690 draw_cursor (61 B). Composites the cursor onto an arbitrary
// destination bitmap: swaps _bmp_current, runs layer 0's draw, puts it back.
void draw_cursor(intptr_t nArg0, int nArg1, int nArg2)
{
  void *pSaved;

  if (sgpCurAnim_g != NULL && _img_list[0].bInUse != 0) {
    pSaved = _bmp_current;
    _bmp_current = reinterpret_cast<void *>(nArg0);
    cur_display_update(static_cast<WORD>(nArg1), static_cast<WORD>(nArg2), 0, 0);
    _bmp_current = pSaved;
  }
}
