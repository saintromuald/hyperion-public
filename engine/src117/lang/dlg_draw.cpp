// dlg_draw.cpp -- the dialog draw layer: the per-control renderers
// gCtrlDrawFcnTbl @0x4F1148 dispatches through, the shared text renderer they
// all funnel into, and the pen-based line primitive behind the dialog frames.
//
// Every renderer in the table has one signature,
//     void __fastcall f(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
// -- ECX + EDX + two stack dwords, callee-cleaned (`ret 8h`); dialog.h carries
// the derivation. This TU is a second proof of it: draw_rect and draw_dlg call
// draw_bmp_effect without ever writing EDX and draw_bmp_effect uses EDX, so
// EDX is live across draw_rect, which only works if draw_rect receives it.
// Definition order does not matter here: nothing in the file is inlined into
// anything else in it, and every intra-file call is a real call or tail jump.
#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>

#include "bmp.h"
#include "dialog.h"
#include "rect16.h"
#include "dlg_ctrl.h"      // ctrl_set_hot_flag -- ported
#include "dlg_draw.h"
#include "dlg_effects.h"
#include "gds_grp2bmpx.h"

typedef unsigned char  BYTE;
typedef unsigned short WORD;
#include "platform.h"  // DWORD
#include <string.h>
#include "../port/data/port_language.h"  // Hyperion: PortLanguageGame
#include "../port/render/port_maptext.h"  // Hyperion: MapTextSqueeze

// Two callees declared here rather than in dlg_draw.h; see that header's note.

// font_adjust -- oracle 0x4CDF20. ECX = x, EDX = y, one stack dword = the
// string; both coordinates are built with `movsx` and added to a 32-bit
// offset, so they are signed ints, not shorts. It returns psz, or NULL when the
// string is empty -- the oracle's epilogue is `cmp byte ptr [eax], cl /
// cmovz eax, ecx / ret 4h` -- so do not simplify the return to void: it is
// unobservable from the bodies here, but it is the callee's real ABI and the
// mangled name depends on it.
const char * __fastcall font_adjust(int nX, int nY, const char *psz);

// appcritf -- oracle 0x4CC190, __cdecl: `push 4F1184h / call appcritf /
// add esp, 4h`, the pointer being the literal "draw:1" at 0x4F1184. It is
// printf-shaped, and only the one-argument form is reached here.
void __cdecl appcritf(const char *pszFmt, ...);

// ctrl_bmp -- the bmp_t a ctrl_t carries at +0x0C. dialog.h models +0x0C as
// `_unmapped0C[4]` and +0x10 as `long nResIndex`, and both readings of +0x10
// are real: dlg_load_ctrl tests it `jns` and resolves a negative value against
// the load base, while every draw function here tests the same dword against 0
// to decide whether the control has a bitmap at all. One slot does load-time
// index and run-time pointer duty, so this accessor casts instead of adding a
// field, and it is a macro rather than a static inline because as a function
// MSVC would inline it at both sites and still emit a 4-byte COMDAT.
#define ctrl_bmp(p) (reinterpret_cast<bmp_t *>(&(p)->_unmapped0C[0]))

// The game globals this TU reaches, in both spellings.

// gbFontJust -- BYTE @0x6D300E (gds/font.cpp)
#ifdef OC_DLL_MODE
#define gbFontJust (*reinterpret_cast<BYTE*>(OC_VA(0x6D300Eu)))
#else
extern BYTE gbFontJust;
#endif

// gbFontCharExtra -- BYTE @0x6D3035 (gds/font.cpp)
#ifdef OC_DLL_MODE
#define gbFontCharExtra (*reinterpret_cast<BYTE*>(OC_VA(0x6D3035u)))
#else
extern BYTE gbFontCharExtra;
#endif

// gsFontMargins -- rect16_t @0x6D3038 (gds/font.cpp). gsFontMargins+4 is not a
// global of its own; it is the rect's right/bottom half.
#ifdef OC_DLL_MODE
#define gsFontMargins (*reinterpret_cast<rect16_t*>(OC_VA(0x6D3038u)))
#else
extern rect16_t gsFontMargins;
#endif

// sgpFonts_g -- void*[4] @0x6D3024 (gds/font.cpp); src117/lang/dlg_dlg.cpp
// reads the same object and derives the extent.
#ifdef OC_DLL_MODE
#define sgpFonts_g (reinterpret_cast<void**>(OC_VA(0x6D3024u)))
#else
extern void *sgpFonts_g[4];
#endif

// _bmp_pen -- BYTE @0x6D0C50 (gds/vidblit.cpp), bounded by ghLocalDLL
// @0x6D0C54.
#ifdef OC_DLL_MODE
#define _bmp_pen (*reinterpret_cast<BYTE*>(OC_VA(0x6D0C50u)))
#else
extern BYTE _bmp_pen;
#endif

// sgbPens_g -- BYTE[32] @0x6D2368 (gds/palette.cpp), bounded by _vid_pal
// @0x6D2388. Every folded element here ([2], [5], [12], [16]) is spelled
// base+index rather than as the folded address.
#ifdef OC_DLL_MODE
#define sgbPens_g (reinterpret_cast<BYTE*>(OC_VA(0x6D2368u)))
#else
extern BYTE sgbPens_g[32] OC_UNALIGNED;
#endif

// sgnXPen -- short @0x6CE76C (dlg/draw.cpp). File-static: draw_frame_rect's rc
// loads are hoisted above the pen stores, which MSVC does only when the
// global's address cannot escape the TU.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgnXPen (*reinterpret_cast<short*>(OC_VA(0x6CE76Cu)))
#else
static short sgnXPen;
#endif

// sgnYPen -- short @0x6CE770 (dlg/draw.cpp)
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgnYPen (*reinterpret_cast<short*>(OC_VA(0x6CE770u)))
#else
static short sgnYPen;
#endif

// gwUnk28_g -- WORD @0x6CE75C (dlg/dlg.cpp), one of the 31 theme metrics
// dlg_dlg.cpp's dlg_stats_init publishes.
#ifdef OC_DLL_MODE
#define gwUnk28_g (*reinterpret_cast<WORD*>(OC_VA(0x6CE75Cu)))
#else
extern WORD gwUnk28_g;
#endif

// draw_ctrl_bmp_xy_transparent -- not a gCtrlDrawFcnTbl entry despite `ret 8h`:
// a thin wrapper on draw_ctrl_bmp_xy that forces the 0x2000 flag, taking that
// function's first four parameters. ECX and EDX pass through with no
// instruction at all, which only works if the types match exactly, BYTE in ECX
// and short in EDX.
void __fastcall draw_ctrl_bmp_xy_transparent(BYTE nFrame, short nX, int nY,
                                             rect16_t *prcClip)
{
  draw_ctrl_bmp_xy(nFrame, nX, nY, prcClip, 0x2000);
}

// draw_rtext -- gCtrlDrawFcnTbl[11], right-justified text.
void __fastcall draw_rtext(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  if (pCtrl->pData) {
    gbFontJust = 0x14;
    draw_ctrl_text(pCtrl, 0, 0, 0);
  }
}

// draw_ltext -- gCtrlDrawFcnTbl[9], left-justified text.
void __fastcall draw_ltext(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  if (pCtrl->pData) {
    gbFontJust = 0x11;
    draw_ctrl_text(pCtrl, 0, 0, 0);
  }
}

// draw_ctext -- gCtrlDrawFcnTbl[10], centred text.
void __fastcall draw_ctext(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  if (pCtrl->pData) {
    gbFontJust = 0x12;
    draw_ctrl_text(pCtrl, 0, 0, 0);
  }
}

// draw_rect -- gCtrlDrawFcnTbl[5]. Blits the control's own bitmap, honouring
// its flags. nXOfs never appears in the code, which is half of this TU's proof
// of the four-argument signature: EDX passes through untouched.
void __fastcall draw_rect(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  if (pCtrl->nResIndex)
    draw_bmp_effect(ctrl_bmp(pCtrl), nXOfs, nYOfs, prcClip, pCtrl->dwFlags);
}

// draw_dlg -- gCtrlDrawFcnTbl[0]. A dialog with a background bitmap blits it;
// one without floods the clip rectangle with a pen.
void __fastcall draw_dlg(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  if (pCtrl->nResIndex) {
    draw_bmp_effect(ctrl_bmp(pCtrl), nXOfs, nYOfs, prcClip, 0);
    return;
  }
  _bmp_pen = (BYTE)((pCtrl->dwFlags & 0x2000) ? 0 : sgbPens_g[12]);
  bmp_rect_fill(prcClip->left, prcClip->top,
                prcClip->right - prcClip->left + 1,
                prcClip->bottom - prcClip->top + 1);
}

// draw_pushbutton -- gCtrlDrawFcnTbl[1] and [2], one function in two slots.
// Three tail calls, all of the family signature.
void __fastcall draw_pushbutton(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  if (pCtrl->nResIndex)
    draw_custom_btn(pCtrl, nXOfs, nYOfs, prcClip);
  else if (pCtrl->dwFlags & 0x20000000)
    draw_local_btn(pCtrl, nXOfs, nYOfs, prcClip);
  else
    draw_standard_btn(pCtrl, nXOfs, nYOfs, prcClip);
}

// draw_frame_hot -- not a table entry: `ret 4h`, and EDX is a rect16_t* rather
// than an offset. Outlines a rectangle in the caller's pen when the control
// carries the 0x1000 "hot" flag.
void __fastcall draw_frame_hot(ctrl_t *pCtrl, rect16_t *prc, int nPen)
{
  if (pCtrl->dwFlags & 0x1000) {
    _bmp_pen = (BYTE)nPen;
    bmp_rect(prc->left, prc->top,
             prc->right - prc->left + 1,
             prc->bottom - prc->top + 1);
  }
}

// draw_edit -- gCtrlDrawFcnTbl[8]. Ignores all three of its offset and clip
// arguments, still `ret 8h`, and works entirely off the control's own rect.
void __fastcall draw_edit(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  ctrl_set_hot_flag(pCtrl);
  edit_draw_text(pCtrl);
  if ((pCtrl->dwFlags & 0x80000000) && (pCtrl->dwFlags & 0x1000)) {
    _bmp_pen = sgbPens_g[16];
    bmp_rect(pCtrl->rc.left, pCtrl->rc.top,
             pCtrl->rc.right - pCtrl->rc.left + 1,
             pCtrl->rc.bottom - pCtrl->rc.top + 1);
  }
}

// LineTo -- the pen-based line primitive behind the dialog frame drawing. ECX
// and EDX are shorts (`mov di, cx` / `mov bx, dx`, no widening) and it cleans
// nothing, so both arguments are register-only. It normalises each axis into
// (min, max) as signed 32-bit values, every materialisation a `movsx`, then
// draws whichever degenerate case holds; a diagonal is a hard error.
//
// The pen only advances on the axis that moved -- the horizontal branch writes
// sgnXPen and leaves sgnYPen, the vertical branch the reverse -- and each branch
// is taken only when the other axis is unchanged, so the pen still ends at
// (nX, nY). Surprising, not a bug.
void __fastcall LineTo(short nX, short nY)
{
  int nX0, nX1, nY0, nY1;

  if (sgnXPen < nX) { nX0 = sgnXPen; nX1 = nX; }
  else              { nX0 = nX;      nX1 = sgnXPen; }

  if (sgnYPen < nY) { nY0 = sgnYPen; nY1 = nY; }
  else              { nY0 = nY;      nY1 = sgnYPen; }

  if (nY0 == nY1) {
    bmp_hline(nX0, nY0, nX1 - nX0 + 1);
    sgnXPen = nX;
  } else if (nX0 == nX1) {
    bmp_vline(nX0, nY0, nY1 - nY0 + 1);
    sgnYPen = nY;
  } else {
    // The literal at 0x4F1184 -- .rdata, immediately after gCtrlDrawFcnTbl's
    // fifteenth and last entry, read straight out of the oracle image.
    appcritf("draw:1");
  }
}

// draw_ctrl_text -- the shared text renderer draw_rtext, draw_ltext,
// draw_ctext, draw_local_btn and draw_standard_btn all funnel into. Not a table
// entry: its EDX and +8 are the x and y offsets and +0Ch is a clip rect that may
// be NULL, in which case the control's own rectangle is used. It saves
// gbFontJust at entry and writes it back right after font_set, which resets it,
// and that is what makes the callers' `gbFontJust = 0x11/0x12/0x14/0x22` idiom
// work.
// Hyperion: in another language, a line too wide for a control of one line, which would
// wrap out of sight where the classic's English did not (a console's "label: number"), takes the
// set's next smaller font that fits it, else the smallest with its TrueType letters narrowed
// until it fits; true when they were (the caller sets them back). A font's height is its
// header's byte at +7 (gds_font.cpp's FontHeader).
static bool FitText(ctrl_t *pCtrl, int nFont, BYTE bJust)
{
  static const bool s_bOther = strcmp(PortLanguageGame(), "enUS") != 0;
  const char *psz = static_cast<const char *>(pCtrl->pData);
  if (!s_bOther || psz == NULL || sgpFonts_g[nFont] == NULL)
    return false;
  if (pCtrl->dwFlags & 0x300)
    ++psz;
  const int nWidth = pCtrl->rc.right - pCtrl->rc.left;
  const int nHeight = pCtrl->rc.bottom - pCtrl->rc.top + 1;
  if (nHeight >= 2 * static_cast<const BYTE *>(sgpFonts_g[nFont])[7] || strchr(psz, '\n') != NULL ||
      font_strlen(psz) <= nWidth)
    return false;
  for (int i = nFont - 1; i >= 0; --i) {
    font_set(sgpFonts_g[i]);
    gbFontJust = bJust;
    if (font_strlen(psz) <= nWidth)
      return false;
  }
  for (int nPercent = 90; nPercent >= 60; nPercent -= 10) {
    MapTextSqueeze(nPercent);
    if (font_strlen(psz) <= nWidth)
      break;
  }
  return true;
}

void __fastcall draw_ctrl_text(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  BYTE bJust = gbFontJust;
  int nColor;
  dlg_word_t wType;
  const char *psz;

  // The font is selected by three bits of the flag word, and two things about
  // this block are derived. It is a switch, not an if/else-if chain: the oracle
  // lays the arms out in reverse source order after a fallthrough default, as
  // dlg_set_font does. And the call belongs inside every arm -- assigning pFont
  // and calling font_set once after the switch lets MSVC if-convert the last two
  // arms into a `cmovne` and costs three bytes. A call is not speculatable, so
  // font_set in each arm takes if-conversion off the table, and MSVC tail-merges
  // the four identical calls back into one.
  switch (pCtrl->dwFlags & 0x4C00) {
    case 0x400:  font_set(sgpFonts_g[0]); break;
    case 0x800:  font_set(sgpFonts_g[2]); break;
    case 0x4000: font_set(sgpFonts_g[3]); break;
    default:     font_set(sgpFonts_g[1]); break;
  }

  gbFontJust = bJust;
  const bool bSqueezed = FitText(pCtrl, (pCtrl->dwFlags & 0x4C00) == 0x400 ? 0 : (pCtrl->dwFlags & 0x4C00) == 0x800 ? 2
                                      : (pCtrl->dwFlags & 0x4C00) == 0x4000 ? 3 : 1, bJust);
  gbFontCharExtra = (BYTE)((pCtrl->dwFlags & 0x10000) ? 0 : gbFontCharExtra);

  // `int nColor = 2` comes first because the oracle hoists the constant into EAX
  // before the first test and reuses it both as the mask (flags & 2) and as the
  // comparand (wType == 2). Only a live 2 produces that.
  wType = pCtrl->wType;
  nColor = 2;
  if (pCtrl->dwFlags & 2)
    nColor = 5;
  else if (pCtrl->dwFlags & 0x80)
    nColor = 4;
  else if (wType == 2 || wType == 1 || wType == 0x0D || wType == 3)
    nColor = 3;
  else if (wType == 9 || wType == 0x0A || wType == 0x0B) {
    if (pCtrl->dwFlags & 0x80000000)
      nColor = 4;
    else if (pCtrl->dwFlags & 0x40000000)
      nColor = 6;
    else if (pCtrl->dwFlags & 0x20000000)
      nColor = 3;
  }
  font_set_color_tbl((BYTE)nColor);

  psz = (const char *)pCtrl->pData;
  if (pCtrl->dwFlags & 0x300)
    ++psz;

  if (prcClip)
    gsFontMargins = *prcClip;
  else
    gsFontMargins = pCtrl->rc;

  font_adjust(pCtrl->rc.left + nXOfs, pCtrl->rc.top + nYOfs, psz);
  font_set(0);
  if (bSqueezed)
    MapTextSqueeze(100);
}

// draw_local_btn -- the "local" (skinned) push button, reached from
// draw_pushbutton when the control carries flag 0x20000000. The frame index
// goes through a stack slot only its low byte is written to, so three of the
// four pushed bytes are stale -- MSVC's ordinary way of passing a BYTE-typed
// argument out of a register, since the callee reads only the low byte.
// Declaring draw_local_ctrl's fifth parameter BYTE is what reproduces it; a
// wider parameter emits a `movzx` and a plain `push`.
void __fastcall draw_local_btn(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  BYTE bFrame;

  ctrl_set_hot_flag(pCtrl);

  // The +0x24 load is written into each arm rather than hoisted above the flag
  // test: the oracle loads dwFlags first and only then reads the frame byte,
  // duplicating the load into both sides. Hoisting it drags the whole
  // argument-push schedule with it.
  if (pCtrl->dwFlags & 2)
    bFrame = pCtrl->_unmapped24[0];
  else if (pCtrl->dwFlags & 0x40000000)
    bFrame = pCtrl->_unmapped24[0] + 6;
  else
    bFrame = pCtrl->_unmapped24[0] + 3;

  draw_local_ctrl(pCtrl->rc.left, pCtrl->rc.top,
                  pCtrl->rc.right - pCtrl->rc.left + 1,
                  gwUnk28_g, bFrame, prcClip, 0x2000);

  // draw_frame_hot's block, written out inline: the oracle's plain `mov al`
  // here is not what an inlined draw_frame_hot would emit.
  if (pCtrl->dwFlags & 0x1000) {
    _bmp_pen = sgbPens_g[16];
    bmp_rect(pCtrl->rc.left, pCtrl->rc.top,
             pCtrl->rc.right - pCtrl->rc.left + 1,
             pCtrl->rc.bottom - pCtrl->rc.top + 1);
  }

  if (pCtrl->pData) {
    gbFontJust = 0x22;
    draw_ctrl_text(pCtrl, 0, 0, 0);
  }
}

// draw_standard_btn -- the default push-button renderer. The (WORD) on rc.top
// is derived, not decoration: the oracle loads it `movzx eax, word ptr
// [esi+6h]` for an argument that is otherwise a plain dword, and a signed short
// widened to int would give `movsx`. rc.left needs no cast because the
// parameter it feeds is itself a short.
void __fastcall draw_standard_btn(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  ctrl_set_hot_flag(pCtrl);

  if (!(pCtrl->dwFlags & 0x20000)) {
    BYTE bFrame = pCtrl->_unmapped24[0];
    if (!(pCtrl->dwFlags & 2)) {
      if (pCtrl->dwFlags & 0x40000000)
        bFrame += 2;
      else
        bFrame += 1;
    }
    draw_ctrl_bmp_xy(bFrame, pCtrl->rc.left, (WORD)pCtrl->rc.top,
                     prcClip, pCtrl->dwFlags);
  }

  if (pCtrl->wType == 1) {
    _bmp_pen = sgbPens_g[5];
    bmp_rect(pCtrl->rc.left, pCtrl->rc.top,
             pCtrl->rc.right - pCtrl->rc.left + 1,
             pCtrl->rc.bottom - pCtrl->rc.top + 1);
  }

  if (pCtrl->dwFlags & 0x1000) {
    _bmp_pen = sgbPens_g[16];
    bmp_rect(pCtrl->rc.left, pCtrl->rc.top,
             pCtrl->rc.right - pCtrl->rc.left + 1,
             pCtrl->rc.bottom - pCtrl->rc.top + 1);
  }

  if (pCtrl->pData) {
    int nOfs = (pCtrl->dwFlags >> 29) & 2;
    gbFontJust = 0x22;
    draw_ctrl_text(pCtrl, nOfs, nOfs, 0);
  }
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))

// gwUnk29_g -- WORD @0x6CE760 (dlg/dlg.cpp), the last theme metric.
#ifdef OC_DLL_MODE
#define gwUnk29_g (*reinterpret_cast<WORD*>(OC_VA(0x6CE760u)))
#else
extern WORD gwUnk29_g;
#endif

// draw_bar_graph -- draws a progress bar across prc: the empty track, then
// dwDone/dwTotal of it filled, never less than one pixel once anything is done.
void __fastcall draw_bar_graph(rect16_t *prc, DWORD dwTotal, DWORD dwDone)
{
  DWORD nWidth = prc->right - prc->left + 1;
  DWORD nFill;
  if (dwTotal) {
    nFill = nWidth * dwDone / dwTotal;
    if (nFill > nWidth) {
      nFill = nWidth;
      goto draw;
    }
  } else {
    nFill = 0;
  }
  if (dwDone && !nFill)
    nFill = 1;
draw:
  if (nWidth)
    draw_local_ctrl(prc->left, prc->top, nWidth, gwUnk29_g, 0x85, prc, 0x2000);
  if (nFill)
    draw_local_ctrl(prc->left, prc->top, nFill, gwUnk29_g, 0x88, prc, 0x2000);
}

// draw_frame_rect -- draws a two-pixel frame around a control: an inner
// rectangle in bPen, then the outer one in palette pen 12.
void __fastcall draw_frame_rect(ctrl_t *pCtrl, BYTE bPen)
{
  _bmp_pen = bPen;
  sgnXPen = pCtrl->rc.left + 1;
  sgnYPen = pCtrl->rc.bottom - 1;
  LineTo(pCtrl->rc.left + 1, pCtrl->rc.top + 1);
  LineTo(pCtrl->rc.right - 1, pCtrl->rc.top + 1);
  LineTo(pCtrl->rc.right - 1, pCtrl->rc.bottom - 1);
  LineTo(pCtrl->rc.left + 1, pCtrl->rc.bottom - 1);
  _bmp_pen = sgbPens_g[12];
  sgnXPen = pCtrl->rc.right;
  sgnYPen = pCtrl->rc.top;
  LineTo(pCtrl->rc.right, pCtrl->rc.bottom);
  LineTo(pCtrl->rc.left, pCtrl->rc.bottom);
  LineTo(pCtrl->rc.left, pCtrl->rc.top);
  LineTo(pCtrl->rc.right, pCtrl->rc.top);
}

// draw_bmp_effect -- blits a bitmap at (nXOfs, nYOfs) inside prcClip through
// effects_draw_surface, picking the render mode from dwFlags.
void __fastcall draw_bmp_effect(bmp_t *pBmp, int nXOfs, int nYOfs, rect16_t *prcClip, ULONG dwFlags)
{
  rect32_t rcSrc;
  rect32_t rcDest;
  int size[2];
  int nMode;
  rcDest.left = prcClip->left;
  rcDest.top = prcClip->top;
  rcDest.right = MIN(pBmp->nWidth + prcClip->left, prcClip->right + 1);
  rcDest.bottom = MIN(pBmp->nHeight + prcClip->top, prcClip->bottom + 1);
  if (nXOfs < 0) {
    rcDest.left -= nXOfs;
    nXOfs = 0;
  }
  if (nYOfs < 0) {
    rcDest.top -= nYOfs;
    nYOfs = 0;
  }
  rcSrc.left = nXOfs;
  rcSrc.top = nYOfs;
  rcSrc.right = MIN(rcDest.right - rcDest.left + nXOfs + 1, pBmp->nWidth);
  rcSrc.bottom = MIN(rcDest.bottom - rcDest.top + nYOfs + 1, pBmp->nHeight);
  // The size pair is filled last; filling it first moves two spill slots.
  size[0] = pBmp->nWidth;
  size[1] = pBmp->nHeight;
  nMode = 0;
  if (dwFlags & 0x40000)
    nMode = (dwFlags & 0x2000000) ? 5 : 4;
  else if (dwFlags & 0x2000)
    nMode = 1;
  effects_draw_surface(pBmp->pPixels, (int *)&rcSrc, size, size[0], (int *)&rcDest, nMode, 0);
}

// gpCurrDlgGrp_g -- void * @0x6CE6E0  (dlg/dlg.cpp), the current theme's group.
#ifdef OC_DLL_MODE
#define gpCurrDlgGrp_g (*reinterpret_cast<void **>(OC_VA(0x6CE6E0u)))
#else
extern void *gpCurrDlgGrp_g;
#endif

// draw_ctrl_bmp_xy -- draws frame nFrame of the current theme group at (nX, nY),
// clipped to prcClip.
void __fastcall draw_ctrl_bmp_xy(BYTE nFrame, short nX, int nY, rect16_t *prcClip, ULONG dwFlags)
{
  bmp_t bmp;
  rect16_t rc;
  short x, y, w, h;
  int nXOfs, nYOfs;
  grp_to_bmpx(static_cast<GROUP *>(gpCurrDlgGrp_g), nFrame, &bmp, &x, &y, &w, &h);
  x += nX;
  y += nY;
  rc.left = (WORD)x > prcClip->left ? x : prcClip->left;
  rc.top = (WORD)y > prcClip->top ? y : prcClip->top;
  rc.right = (WORD)x + (WORD)w < prcClip->right ? x + w : prcClip->right;
  rc.bottom = (WORD)y + (WORD)h < prcClip->bottom ? y + h : prcClip->bottom;
  nYOfs = rc.top - y;
  if (nYOfs < 0)
    nYOfs = 0;
  nXOfs = rc.left - x;
  if (nXOfs < 0)
    nXOfs = 0;
  draw_bmp_effect(&bmp, nXOfs, nYOfs, &rc, dwFlags);
}

// gwRadioBtnSize / gwCheckBoxSize -- WORD @0x6CE6EC / 0x6CE6F0  (dlg/dlg.cpp)
#ifdef OC_DLL_MODE
#define gwRadioBtnSize (*reinterpret_cast<WORD*>(OC_VA(0x6CE6ECu)))
#else
extern WORD gwRadioBtnSize;
#endif
#ifdef OC_DLL_MODE
#define gwCheckBoxSize (*reinterpret_cast<WORD*>(OC_VA(0x6CE6F0u)))
#else
extern WORD gwCheckBoxSize;
#endif

// gFontRect -- rect16_t @0x6D3040  (gds/font.cpp), the last text box drawn.
#ifdef OC_DLL_MODE
#define gFontRect (*reinterpret_cast<rect16_t*>(OC_VA(0x6D3040u)))
#else
extern rect16_t gFontRect;
#endif

// The button state a radio button or check box keeps at +0x36.
#define ctrl_state(p) (reinterpret_cast<DWORD *>(&(p)->_unmapped36[0])[0])

// draw_radiobutton -- draws a radio button's glyph, left- or right-aligned by
// flag 0x10000000, then its label beside it and the hot frame around the label.
void __fastcall draw_radiobutton(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  WORD wSize = gwRadioBtnSize;
  int nX = pCtrl->rc.left;
  int nY = pCtrl->rc.top;
  BYTE bFrame;
  if (pCtrl->dwFlags & 0x10000000)
    nX = pCtrl->rc.right - wSize;
  nY += (pCtrl->rc.bottom - wSize - nY) / 2;
  ctrl_set_hot_flag(pCtrl);
  if (pCtrl->dwFlags & 2)
    bFrame = 6;
  else if (pCtrl->dwFlags & 0x40000000)
    bFrame = (BYTE)ctrl_state(pCtrl) * 2 + 8;
  else
    bFrame = (BYTE)ctrl_state(pCtrl) * 2 + 7;
  draw_ctrl_bmp_xy(bFrame, nX, nY, prcClip, 0x2000);
  if (pCtrl->pData) {
    if (pCtrl->dwFlags & 0x10000000) {
      rect16_t rc = pCtrl->rc;
      rc.right += -4 - gwRadioBtnSize;
      gbFontJust = 0x24;
      draw_ctrl_text(pCtrl, 0, 0, &rc);
    } else {
      gbFontJust = 0x21;
      draw_ctrl_text(pCtrl, gwRadioBtnSize + 4, 0, 0);
    }
    if (pCtrl->dwFlags & 0x1000) {
      _bmp_pen = sgbPens_g[16];
      bmp_rect(gFontRect.left, gFontRect.top, gFontRect.right - gFontRect.left + 1,
               gFontRect.bottom - gFontRect.top + 1);
    }
  }
}

// draw_checkbox -- draws a check box's glyph and hot frame, then its label as
// draw_radiobutton does.
void __fastcall draw_checkbox(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  WORD wSize = gwCheckBoxSize;
  int nX = pCtrl->rc.left;
  int nY = pCtrl->rc.top;
  BYTE bFrame;
  rect16_t rc;
  if (pCtrl->dwFlags & 0x10000000)
    nX = pCtrl->rc.right - wSize;
  nY += (pCtrl->rc.bottom - wSize - nY) / 2;
  ctrl_set_hot_flag(pCtrl);
  if (pCtrl->dwFlags & 2) {
    bFrame = pCtrl->_unmapped24[0];
    if (ctrl_state(pCtrl))
      bFrame = 0x8B;
  } else if (pCtrl->dwFlags & 0x40000000) {
    bFrame = pCtrl->_unmapped24[0] + (BYTE)ctrl_state(pCtrl) * 2 + 2;
  } else {
    bFrame = pCtrl->_unmapped24[0] + (BYTE)ctrl_state(pCtrl) * 2 + 1;
  }
  draw_ctrl_bmp_xy(bFrame, nX, nY, prcClip, 0x2000);
  rc.right = gwCheckBoxSize - 1 + nX;
  rc.bottom = gwCheckBoxSize - 1 + nY;
  if (pCtrl->dwFlags & 0x1000) {
    _bmp_pen = sgbPens_g[16];
    bmp_rect(nX, nY, rc.right - nX + 1, rc.bottom - nY + 1);
  }
  if (pCtrl->pData) {
    if (pCtrl->dwFlags & 0x10000000) {
      rc = pCtrl->rc;
      rc.right += -4 - gwCheckBoxSize;
      gbFontJust = 0x24;
      draw_ctrl_text(pCtrl, 0, 0, &rc);
    } else {
      gbFontJust = 0x21;
      draw_ctrl_text(pCtrl, gwCheckBoxSize + 4, 0, 0);
    }
    if (pCtrl->dwFlags & 0x1000) {
      _bmp_pen = sgbPens_g[16];
      bmp_rect(gFontRect.left, gFontRect.top, gFontRect.right - gFontRect.left + 1,
               gFontRect.bottom - gFontRect.top + 1);
    }
  }
}

// draw_local_ctrl -- draws a horizontally stretched skin nWidth wide from three
// frames: a left cap, as many nTile-wide middle pieces as fit, and a right cap.
void __fastcall draw_local_ctrl(int nX, int nY, int nWidth, int nTile, BYTE bFrame, rect16_t *prcClip, ULONG dwFlags)
{
  rect16_t rc = *prcClip;
  int nXPos;
  int n;
  rc.right = rc.left + nWidth;
  // The first call takes a copy of nX, so nX + nTile is recomputed at the end.
  int nXFirst = nX;
  draw_ctrl_bmp_xy(bFrame, nXFirst, nY, &rc, dwFlags);
  rc.right -= nTile;
  bFrame++;
  nXPos = nX + nTile;
  n = (nWidth - nTile - 1) / nTile;
  while (n-- > 0) {
    draw_ctrl_bmp_xy(bFrame, nXPos, nY, &rc, dwFlags);
    nXPos += nTile;
  }
  rc.right += nTile;
  bFrame++;
  if (nWidth >= nTile) {
    if (rc.left <= nX + nTile)
      rc.left = nX + nTile;
    draw_ctrl_bmp_xy(bFrame, nX + nWidth - nTile, nY, &rc, dwFlags);
  }
}

// Draws a bevelled custom button when no clip rectangle is given: the hot or
// plain outline, an inner outline, then two-pixel light and dark edges swapped
// by the pressed flag. The bitmap and the label are drawn either way.
// dwPressed is read before the inset; read after it, the oracle's `test`
// after the `and` is folded away.
void __fastcall draw_custom_btn(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  if (!prcClip) {
    rect16_t rc;
    ULONG dwPressed;

    if (ctrl_is_hot(pCtrl)) {
      _bmp_pen = sgbPens_g[5];
      pCtrl->dwFlags |= 0x1000;
    } else {
      _bmp_pen = sgbPens_g[15];
      pCtrl->dwFlags &= ~0x1000;
    }
    bmp_rect(pCtrl->rc.left, pCtrl->rc.top,
             pCtrl->rc.right - pCtrl->rc.left + 1,
             pCtrl->rc.bottom - pCtrl->rc.top + 1);
    rc.left = pCtrl->rc.left + 1;
    rc.top = pCtrl->rc.top + 1;
    rc.right = pCtrl->rc.right - 1;
    rc.bottom = pCtrl->rc.bottom - 1;
    _bmp_pen = sgbPens_g[15];
    bmp_rect(rc.left, rc.top, rc.right - rc.left + 1, rc.bottom - rc.top + 1);
    dwPressed = pCtrl->dwFlags & 0x40000000;
    rc.left += 2;
    rc.top += 2;
    rc.right -= 1;
    rc.bottom -= 1;
    _bmp_pen = dwPressed ? sgbPens_g[12] : sgbPens_g[2];
    sgnXPen = rc.right;
    sgnYPen = rc.top;
    LineTo(rc.left, rc.top);
    LineTo(rc.left, rc.bottom);
    sgnXPen = rc.right;
    sgnYPen = rc.top + 1;
    LineTo(rc.left + 1, rc.top + 1);
    LineTo(rc.left + 1, rc.bottom);
    _bmp_pen = dwPressed ? sgbPens_g[5] : sgbPens_g[12];
    sgnXPen = rc.left + 2;
    sgnYPen = rc.bottom;
    LineTo(rc.right, rc.bottom);
    LineTo(rc.right, rc.top + 2);
    sgnXPen = rc.left + 2;
    sgnYPen = rc.bottom - 1;
    LineTo(rc.right - 1, rc.bottom - 1);
    LineTo(rc.right - 1, rc.top + 2);
    _bmp_pen = sgbPens_g[5];
    if (dwPressed) {
      sgnXPen = rc.right;
      sgnYPen = rc.top + 1;
      LineTo(rc.left + 1, rc.top + 1);
      LineTo(rc.left + 1, rc.bottom - 1);
    }
  }
  if (pCtrl->nResIndex)
    draw_bmp_effect(ctrl_bmp(pCtrl), 0, 0, prcClip, pCtrl->dwFlags);
  if (pCtrl->pData) {
    int nOfs = (pCtrl->dwFlags >> 29) & 2;
    gbFontJust = 0x22;
    draw_ctrl_text(pCtrl, nOfs, nOfs, 0);
  }
}
