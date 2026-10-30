// dlg_lbox.cpp -- the dialog manager's listbox and combo controls: the item and
// data accessors, the selection and scroll bookkeeping, the mouse and timer
// handlers, and the two draw entries the control tables dispatch through.
// Bodies are in ascending oracle VA order, with one hoist: combo_close has to
// stay above combo_reset_slider, combo_lmouse_up and combo_restore, because
// combo_lmouse_up (0x4C5F70) and combo_reset_slider (0x4C5EB0) each carry
// combo_restore's (0x4C5FB0) 37 bytes verbatim, and MSVC cannot inline a body
// it has not seen.

// A listbox is a different arm of the dialog manager's node than a dialog, and
// the two collide: on a listbox +0x3E and +0x42 are heap arrays where the
// dialog arm has pFocus and pChildren, +0x46 is a BYTE count where it has
// pMouseIn, and +0x4A is a BYTE where it has pfnDlgProc. So this TU declares
// its own `lbox_t` view of the node and casts to ctrl_t* at every shared-API
// boundary, which is a pointer cast and no code. The head fields in lbox_t are
// restated from dialog.h so the two views can be checked against each other by
// offset, and every payload field below carries its own evidence.
// `sizeof(ctrl_t)` is 0x56: a derived lower bound, not an allocation size, and
// raised from 0x4F once draw_lbox (+0x52) and combo_restore (+0x50) were seen
// to reach past the first sweep's reach.
#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <intrin.h>   // _ReadWriteBarrier -- lbox_recalc

#include "dialog.h"
#include "rect16.h"
#include "dlg_ctrl.h"      // ctrl_set_update / ctrl_set_val / ctrl_gray /
                           // ctrl_ungray / ctrl_force_update / pt_in_rect /
                           // global_evt_2_local_point
#include "dlg_dlg.h"       // dlg_hook / dlg_set_font
#include "dlg_draw.h"      // font_set, declared only there
#include "dlg_lbox.h"
#include "Storm.h"
#include "minimap.h"
#include "dlg_scrlbar.h"
#include "../port/render/port_maptext.h"  // Hyperion: MapTextCharStart

// lbox_set_match is seated in src117/h/gluCreat.h, its only consumer.
unsigned char lbox_set_match(lbox_t *pBox, const char *pszText);

// The game globals.

// gwUnk13_g / gwUnk15_g / gwUnk20_g / gwUnk22_g -- WORD @0x6CE720 / 0x6CE72C /
// 0x6CE73C / 0x6CE748 (dlg/dlg.cpp), four of the theme metrics dlg_dlg.cpp's
// dlg_stats_init publishes out of the 0x14C-byte theme record. The width is
// WORD even though the run is 4-byte spaced: every read here is a `movzx` of a
// word, where an int would load with a plain `mov`. They sit on 4-byte centres
// with a name of their own on every slot, so they are separate globals.
#ifdef OC_DLL_MODE
#define gwUnk13_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE720u)))
#define gwUnk15_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE72Cu)))
#define gwUnk20_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE73Cu)))
#define gwUnk22_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE748u)))
#else
extern dlg_word_t gwUnk13_g;
extern dlg_word_t gwUnk15_g;
extern dlg_word_t gwUnk20_g;
extern dlg_word_t gwUnk22_g;
#endif

// sgpCurrentCombo_g -- ctrl_t * @0x6CF7A8 (dlg/lbox.cpp), the combo whose list
// is currently dropped open. The store names the type: combo_lmouse_down stores
// the combo node itself and combo_close clears it, both dword-wide. It stands
// alone rather than being an array element, since the next named datum is
// 0x6CF7B0 and every writer uses the bare address with no index.
#ifdef OC_DLL_MODE
#define sgpCurrentCombo_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x6CF7A8u)))
#else
extern ctrl_t *sgpCurrentCombo_g;
#endif

// sgbPens_g -- BYTE[32] @0x6D2368 (gds/palette.cpp), the extent fixed by the
// next name, _vid_pal @0x6D2388. gbFontCharExtra -- BYTE @0x6D3035
// (gds/font.cpp), one byte past gbFontLineExtra @0x6D3034, so the two are
// adjacent byte scalars and not a word. draw_lbox reaches 0x6D2377 and
// 0x6D2378, which are sgbPens_g[15] and [16] rather than globals of their own,
// so they are spelled base+index here.
#ifdef OC_DLL_MODE
#define sgbPens_g (reinterpret_cast<dlg_byte_t*>(OC_VA(0x6D2368u)))
#define gbFontCharExtra (*reinterpret_cast<dlg_byte_t*>(OC_VA(0x6D3035u)))
#else
extern dlg_byte_t sgbPens_g[32] OC_UNALIGNED;
extern dlg_byte_t gbFontCharExtra;
#endif

// gbFontMaxHgt -- BYTE @0x6D3037 (gds/font.cpp), the current font's tallest
// glyph; lbox_init reads it `movzx eax, byte ptr [6D3037h]`. gwVArrowHgt /
// gwHArrowHgt / gwUnk6_g / gwUnk11_g -- WORD theme metrics from
// src117/lang/dlg_dlg.cpp, all read `movzx ..., word ptr`.
#ifdef OC_DLL_MODE
#define gbFontMaxHgt (*reinterpret_cast<dlg_byte_t*>(OC_VA(0x6D3037u)))
#define gwVArrowHgt (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE6F8u)))
#define gwHArrowHgt (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE700u)))
#define gwUnk6_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE704u)))
#define gwUnk11_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE718u)))
#else
extern dlg_byte_t gbFontMaxHgt;
extern dlg_word_t gwVArrowHgt;
extern dlg_word_t gwHArrowHgt;
extern dlg_word_t gwUnk6_g;
extern dlg_word_t gwUnk11_g;
#endif

// Callees reached only from draw_lbox, declared here rather than in
// src117/h/dlg_lbox.h.

// draw_frame_rect is seated in dlg_draw.h, included above.

// timer_create -- oracle 0x4C5960, declared identically to src117/h/minimap.h,
// so the two spellings cannot mangle to different symbols. The stack order is
// read off the pushes: the callback address is pushed first and the interval
// second, so the interval is argument 3.
typedef void(__fastcall *PFN_TIMER)(void);
void __fastcall timer_create(ctrl_t *pDlg, dlg_word_t wId, int nInterval, PFN_TIMER pfn);

// lbox_t -- the listbox view of a dialog-manager node.
struct lbox_t;

// The +0x52 per-item draw callback -- see the field comment for the evidence.
typedef void(__fastcall *PFN_LBOX_DRAWITEM)(lbox_t *pBox, dlg_byte_t nItem,
                                            rect16_t *prc, int nA, int nB);

#pragma pack(push, 1)
struct lbox_t {
  /*0x00*/ lbox_t *pNext;
  /*0x04*/ rect16_t rc;
  /*0x0C*/ dlg_byte_t _unmapped0C[2 * sizeof(void *)];  // Hyperion: ctrl_t's +0x0C and nResIndex
  /*0x14*/ void *pData;
  /*0x18*/ dlg_dword_t dwFlags;
  /*0x1C*/ dlg_byte_t _unmapped1C[4];
  /*0x20*/ short nIndex;
  /*0x22*/ dlg_word_t wType;
  /*0x24*/ dlg_byte_t _unmapped24[2 + sizeof(void *)];  // Hyperion: ctrl_t's window
  /*0x2A*/ PFN_CTRL_EVT pfnEvt;
  /*0x2E*/ PFN_CTRL_DRAW pfnDraw;
  /*0x32*/ ctrl_t *pParent;
  // +0x36 the companion scrollbar control, or NULL. lbox_hide loads it and
  // tail-calls lbox_update_scroll with it in EDX (`mov edx, [ecx+36h]` /
  // `test edx, edx` / `jnz lbox_update_scroll`), and lbox_update_scroll then
  // dereferences EDX as an ordinary control (`mov eax, [esi+18h]`,
  // `mov ax, word ptr [esi+4h]`, `call ctrl_gray`). Typed ctrl_t* for that
  // reason: what it points at is a control node, not another listbox.
  /*0x36*/ ctrl_t *pScroll;
  // +0x3A the item array: lbox_get_item does
  //     mov eax, [ecx+3Ah] / movzx edx, dl / mov eax, [eax+edx*4]
  // -- a base pointer indexed by a BYTE with a scale of 4, i.e. an array of
  // dwords. What the dwords are is not derived here, so void*.
  /*0x3A*/ void **ppItems;
  // +0x3E the per-item flag bytes, a third parallel array: lbox_remove_item
  // reads `mov bh, byte ptr [edi+eax*1]` with EAX = [esi+3Eh]. Bit 0x10 marks
  // an item string the box allocated itself and must free.
  /*0x3E*/ dlg_byte_t *pbFlags;
  // +0x42 the parallel per-item data array, same shape (lbox_get_data,
  // lbox_set_data). Two distinct arrays, not one: lbox_get_item reads +0x3A
  // and lbox_get_data reads +0x42 with identical index arithmetic.
  /*0x42*/ void **ppData;
  // +0x46 the item count, and it is a BYTE: lbox_get_count is literally
  // `mov al, byte ptr [ecx+46h] / ret`, and lbox_set_selection compares the
  // selection against it with `cmp bl, byte ptr [esi+46h]` / `jb` -- an
  // unsigned byte compare.
  /*0x46*/ dlg_byte_t nCount;
  // +0x47 the capacity of the three arrays, BYTE: lbox_add_item refuses with
  // `cmp al, byte ptr [esi+47h]` / `jb` against nCount.
  /*0x47*/ dlg_byte_t nMax;
  // +0x48 the selected item index, BYTE, with 0xFF as "nothing selected":
  // lbox_get_selection returns it but substitutes -1 when nCount is 0, and
  // lbox_update_scroll guards its ctrl_set_val with `cmp byte [edi+48h], -1h`.
  /*0x48*/ dlg_byte_t nSel;
  // +0x49 the per-item row height in pixels, BYTE. combo_close rebuilds the
  // collapsed rectangle as `movzx eax, byte ptr [edx+49h]` / `add ax, cx` /
  // `mov word ptr [edx+0Ah], ax` where CX is the saved top, i.e.
  // bottom = top + height. draw_lbox loads the same byte into BL and advances
  // the per-row rectangle by it.
  /*0x49*/ dlg_byte_t nRowHeight;
  // +0x4A the number of rows the box can show, BYTE. lbox_update_scroll
  // compares it against nCount (`mov al, [edi+46h]` / `cmp al, [edi+4Ah]` /
  // `jbe`) and greys the scrollbar when the list fits.
  /*0x4A*/ dlg_byte_t nVisible;
  // +0x4B the scrollbar range, BYTE: lbox_update_scroll does
  // `movzx eax, byte ptr [edi+4Bh]` / `xor edx, edx` / `push eax` /
  // `call scroll_set_range`.
  /*0x4B*/ dlg_byte_t nScrollRange;
  // +0x4C a BYTE flag word; only bit 0 is derived. combo_lmouse_down tests it
  // (`test byte ptr [esi+4Ch], 1h`) to choose which edge of the combo grows
  // when the list drops: set -> rc.top moves up, clear -> rc.bottom moves down.
  /*0x4C*/ dlg_byte_t bDropUp;
  // +0x4D the index of the top visible item, BYTE. lbox_update_pos_callback
  // copies the scrollbar's own position byte into it, and lbox_update_scroll
  // publishes it back to the scrollbar with ctrl_set_val.
  /*0x4D*/ dlg_byte_t nTopItem;
#if defined(_WIN64) || defined(__LP64__)
  // Hyperion: the rest of ctrl_t's +0x46 union, two pointers at 64 bits, so
  // that the fields from here fall in ctrl_t's +0x4E window as on x86
  // (src117/h/dialog.h).
  dlg_byte_t _pad4E[8];
#endif
  // +0x4E the item the pointer is currently over, BYTE -- the value
  // lbox_calc_pos returns. lbox_lmouse_down / lbox_mouse_moved / lbox_scroll_timer
  // all store it and redraw only when it changed (`cmp byte [esi+4Eh], al`).
  /*0x4E*/ dlg_byte_t nHotItem;
  /*0x4F*/ dlg_byte_t _unmapped4F[1];
  // +0x50, on a combo only -- the rectangle top saved while the list is dropped
  // down, WORD (`movzx ecx, word ptr [edx+50h]`). combo_close writes it back
  // to rc.top and derives rc.bottom from it.
  /*0x50*/ dlg_word_t wSaveTop;
  // +0x52 the per-ITEM draw callback. draw_lbox calls it as
  //     push 0h / push 2h / lea eax, [ebp-10h] / mov dl, ch / push eax
  //     mov eax, dword ptr [esi+52h] / mov ecx, esi / call eax
  // -- ECX + a BYTE in DL + three pushed dwords, and the installed default
  // (draw_lbox_item @0x4C5FF0) is `ret 0Ch` with `mov al, dl` on entry, which
  // confirms the arity from the callee side. So this is not PFN_CTRL_DRAW
  // (that one is `ret 8h`); it is its own five-argument shape.
  /*0x52*/ PFN_LBOX_DRAWITEM pfnDrawItem;
};
#pragma pack(pop)

// Hyperion: other TUs reach a list box through ctrl_t's windows (its items at
// +0x36's second slot, its painter at +0x4E's, its selection in the +0x46 union), so
// the two agree on every width.
#define OC_AT(f, g) static_assert(offsetof(lbox_t, f) == (g), "lbox_t::" #f " is ctrl_t's")
OC_AT(pData, offsetof(ctrl_t, pData));
OC_AT(dwFlags, offsetof(ctrl_t, dwFlags));
OC_AT(wType, offsetof(ctrl_t, wType));
OC_AT(pfnEvt, offsetof(ctrl_t, pfnEvt));
OC_AT(pfnDraw, offsetof(ctrl_t, pfnDraw));
OC_AT(pParent, offsetof(ctrl_t, pParent));
OC_AT(pScroll, offsetof(ctrl_t, _unmapped36));
OC_AT(ppItems, offsetof(ctrl_t, _unmapped36) + CTRL_SLOT2);
OC_AT(pbFlags, offsetof(ctrl_t, pFocus));
OC_AT(ppData, offsetof(ctrl_t, pChildren));
OC_AT(nCount, offsetof(ctrl_t, rcItems));
OC_AT(nSel, offsetof(ctrl_t, rcItems) + 2);
OC_AT(nHotItem, offsetof(ctrl_t, _unmapped4E));
OC_AT(wSaveTop, offsetof(ctrl_t, _unmapped4E) + 2);
OC_AT(pfnDrawItem, offsetof(ctrl_t, _unmapped4E) + 4);
#undef OC_AT
static_assert(sizeof(lbox_t) == sizeof(ctrl_t), "lbox_t is a ctrl_t node");

OC_X86_LAYOUT(sizeof(lbox_t)                    == 0x56, "lbox_t size        draw_lbox: mov eax, dword ptr [esi+52h] (a dword AT +0x52)");
static_assert(offsetof(lbox_t, pNext)           == 0x00, "lbox_t::pNext      shared head, src117/h/dialog.h");
OC_X86_LAYOUT(offsetof(lbox_t, rc)              == 0x04, "lbox_t::rc         combo_close: mov word ptr [edx+6h], cx");
OC_X86_LAYOUT(offsetof(lbox_t, pData)           == 0x14, "lbox_t::pData      shared head, src117/h/dialog.h");
OC_X86_LAYOUT(offsetof(lbox_t, dwFlags)         == 0x18, "lbox_t::dwFlags    lbox_hide: and dword ptr [ecx+18h], -89h");
OC_X86_LAYOUT(offsetof(lbox_t, nIndex)          == 0x20, "lbox_t::nIndex     shared head, src117/h/dialog.h");
OC_X86_LAYOUT(offsetof(lbox_t, wType)           == 0x22, "lbox_t::wType      shared head, src117/h/dialog.h");
OC_X86_LAYOUT(offsetof(lbox_t, pfnEvt)          == 0x2A, "lbox_t::pfnEvt     shared head, src117/h/dialog.h");
OC_X86_LAYOUT(offsetof(lbox_t, pfnDraw)         == 0x2E, "lbox_t::pfnDraw    shared head, src117/h/dialog.h");
OC_X86_LAYOUT(offsetof(lbox_t, pParent)         == 0x32, "lbox_t::pParent    combo_reset_slider: mov ecx, dword ptr [esi+32h]");
OC_X86_LAYOUT(offsetof(lbox_t, pScroll)         == 0x36, "lbox_t::pScroll    lbox_hide: mov edx, dword ptr [ecx+36h]");
OC_X86_LAYOUT(offsetof(lbox_t, ppItems)         == 0x3A, "lbox_t::ppItems    lbox_get_item: mov eax, dword ptr [ecx+3Ah]");
OC_X86_LAYOUT(offsetof(lbox_t, ppData)          == 0x42, "lbox_t::ppData     lbox_get_data: mov eax, dword ptr [ecx+42h]");
OC_X86_LAYOUT(offsetof(lbox_t, pbFlags)         == 0x3E, "lbox_t::pbFlags    lbox_remove_item: mov eax, dword ptr [esi+3Eh]");
OC_X86_LAYOUT(offsetof(lbox_t, nCount)          == 0x46, "lbox_t::nCount     lbox_get_count: mov al, byte ptr [ecx+46h]");
OC_X86_LAYOUT(offsetof(lbox_t, nMax)            == 0x47, "lbox_t::nMax       lbox_add_item: cmp al, byte ptr [esi+47h]");
OC_X86_LAYOUT(offsetof(lbox_t, nSel)            == 0x48, "lbox_t::nSel       lbox_remove_current: mov dl, byte ptr [ecx+48h]");
OC_X86_LAYOUT(offsetof(lbox_t, nRowHeight)      == 0x49, "lbox_t::nRowHeight combo_close: movzx eax, byte ptr [edx+49h]");
OC_X86_LAYOUT(offsetof(lbox_t, nVisible)        == 0x4A, "lbox_t::nVisible   lbox_update_scroll: cmp al, byte ptr [edi+4Ah]");
OC_X86_LAYOUT(offsetof(lbox_t, nScrollRange)    == 0x4B, "lbox_t::nScrollRange lbox_update_scroll: movzx eax, byte ptr [edi+4Bh]");
OC_X86_LAYOUT(offsetof(lbox_t, bDropUp)         == 0x4C, "lbox_t::bDropUp    combo_lmouse_down: test byte ptr [esi+4Ch], 1h");
OC_X86_LAYOUT(offsetof(lbox_t, nTopItem)        == 0x4D, "lbox_t::nTopItem   lbox_update_pos_callback: mov byte ptr [ecx+4Dh], al");
OC_X86_LAYOUT(offsetof(lbox_t, nHotItem)        == 0x4E, "lbox_t::nHotItem   lbox_scroll_timer: cmp byte ptr [esi+4Eh], al");
OC_X86_LAYOUT(offsetof(lbox_t, wSaveTop)        == 0x50, "lbox_t::wSaveTop   combo_close: movzx ecx, word ptr [edx+50h]");
OC_X86_LAYOUT(offsetof(lbox_t, pfnDrawItem)     == 0x52, "lbox_t::pfnDrawItem draw_lbox: mov eax, dword ptr [esi+52h] / call eax");

// ------------------------------------------------------- flag bits used here
// 0x00100000  the combo's list is dropped down. combo_close clears it
//             (`and eax, -100001h`) while collapsing the rectangle, and
//             draw_combo draws the list only while it is set.
// 0x40000000  a scroll timer is running (lbox_mouse_moved).
// 0x80000000  recalc suppressed. lbox_set_recalc(0) sets it; lbox_set_recalc(1)
//             clears it and runs the deferred lbox_recalc.

// combo_close -- the shared collapse helper, with no oracle row of its own:
// clear the dropped-down bit, restore the one-row rectangle from the saved top,
// and forget the global "combo currently open". It is a __forceinline helper
// rather than three copies because the oracle shows it inlined three times; the
// header's hoist note says why it sits above all three.
//
// Statement order matters here, and is worth two bytes plus a register
// permutation: reading wSaveTop into the local before the dwFlags store makes
// MSVC keep the node pointer in ECX and put the temp in EDX, where doing the
// flag store first -- as below -- spends `mov edx, ecx` on entry and hands ECX
// to the temp, which is the oracle. Three source shapes reproduce it and four
// do not, so what is pinned is the order, not the local.
static __forceinline void combo_close(lbox_t *pBox) {
  dlg_dword_t dwFlags = pBox->dwFlags;
  if (dwFlags & 0x100000) {
    pBox->dwFlags = dwFlags & ~0x100000u;
    dlg_word_t wTop = pBox->wSaveTop;
    pBox->rc.top = wTop;
    pBox->rc.bottom = (short)(pBox->nRowHeight + wTop);
    sgpCurrentCombo_g = 0;
  }
}

// gwCombo1OpenWid_g / gwCombo2OpenWid_g -- WORD @0x6CE71C / 0x6CE738
// (dlg/dlg.cpp), the tile widths of the two dropped-list skins.
#ifdef OC_DLL_MODE
#define gwCombo1OpenWid_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE71Cu)))
#else
extern dlg_word_t gwCombo1OpenWid_g;
#endif
#ifdef OC_DLL_MODE
#define gwCombo2OpenWid_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE738u)))
#else
extern dlg_word_t gwCombo2OpenWid_g;
#endif

// gwCombo1ArrowWid / gwCombo2ArrowWid -- WORD @0x6CE724 / 0x6CE740 (dlg/dlg.cpp).
#ifdef OC_DLL_MODE
#define gwCombo1ArrowWid (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE724u)))
#else
extern dlg_word_t gwCombo1ArrowWid;
#endif
#ifdef OC_DLL_MODE
#define gwCombo2ArrowWid (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE740u)))
#else
extern dlg_word_t gwCombo2ArrowWid;
#endif

// gbFontAllowWrap -- BYTE @0x50F160 (gds/font.cpp).
#ifdef OC_DLL_MODE
#define gbFontAllowWrap (*reinterpret_cast<dlg_byte_t*>(OC_VA(0x50F160u)))
#else
extern dlg_byte_t gbFontAllowWrap;
#endif

// gbFontJust -- BYTE @0x6D300E (gds/font.cpp).
#ifdef OC_DLL_MODE
#define gbFontJust (*reinterpret_cast<dlg_byte_t*>(OC_VA(0x6D300Eu)))
#else
extern dlg_byte_t gbFontJust;
#endif

// lbox_draw_combo2 -- draws a combo's closed face: its skin and arrow over the
// saved one-row rectangle, then the selected item's text, restoring the
// rectangle after.
void __fastcall lbox_draw_combo2(lbox_t *pBox, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  int bUp = pBox->bDropUp & 1;
  dlg_dword_t dwFlags = pBox->dwFlags;
  int nFrame, nArrow, nTile, nArrowWid;
  int nTop, nBottom;
  if (dwFlags & 0x20000000) {
    if (dwFlags & 0x100000) {
      nArrow = 0x32;
      nFrame = bUp ? 0x2C : 0x2F;
    } else {
      nFrame = 0x35;
      nArrow = 0x32;
      if (dwFlags & 0x80) {
        nFrame = 0x38;
        nArrow = 0x33;
      } else if (dwFlags & 2) {
        nArrow = 0x34;
      }
    }
    nTile = gwCombo1OpenWid_g;
    nArrowWid = gwCombo1ArrowWid + 4;
  } else {
    if (dwFlags & 0x100000) {
      nArrow = 0x4A;
      nFrame = bUp ? 0x44 : 0x47;
    } else {
      nFrame = 0x4D;
      nArrow = 0x4A;
      if (dwFlags & 0x80) {
        nFrame = 0x50;
        nArrow = 0x4B;
      } else if (dwFlags & 2) {
        nArrow = 0x4C;
      }
    }
    nTile = gwCombo2OpenWid_g;
    nArrowWid = gwCombo2ArrowWid + 4;
  }
  dlg_set_font(reinterpret_cast<ctrl_t *>(pBox));
  nTop = (dlg_word_t)pBox->rc.top;
  nBottom = (dlg_word_t)pBox->rc.bottom;
  pBox->rc.top = pBox->wSaveTop;
  pBox->rc.bottom = pBox->wSaveTop + gwUnk15_g;
  draw_local_ctrl(pBox->rc.left, pBox->rc.top, pBox->rc.right - pBox->rc.left + 1,
                  nTile, nFrame, prcClip, 0x2000);
  draw_ctrl_bmp_xy(nArrow, pBox->rc.right - nArrowWid, (dlg_word_t)(pBox->rc.top + 4),
                   prcClip, 0x2000);
  if (pBox->nCount && pBox->nSel != 0xFF) {
    pBox->pData = pBox->ppItems[pBox->nSel];
    dlg_byte_t bWrap = gbFontAllowWrap;
    gbFontJust = 0x11;
    gbFontAllowWrap = 0;
    dlg_dword_t dwSave = pBox->dwFlags;
    pBox->dwFlags = dwSave & ~2;
    rect16_t rc = pBox->rc;
    rc.right -= nArrowWid;
    draw_ctrl_text(reinterpret_cast<ctrl_t *>(pBox), 4, 0, &rc);
    pBox->dwFlags = dwSave;
    gbFontAllowWrap = bWrap;
  }
  pBox->rc.top = nTop;
  pBox->rc.bottom = nBottom;
}

// lbox_draw_list -- draws a combo's dropped list one row at a time, from the
// edge the list grew away from, each row's skin followed by its item. The row
// height is read separately in each arm, as the oracle loads it twice, the frame
// is picked last in each arm, and the bottom edge goes through a short local,
// which gives the oracle's `movzx edx, word ptr [esi+0Ah]`.
void __fastcall lbox_draw_list(lbox_t *pBox, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  rect16_t rc;
  int nFirst, nMid, nLast, nTile;
  int nStep, nDir, n, nItem;
  BYTE bFrame;
  dlg_set_font(reinterpret_cast<ctrl_t *>(pBox));
  gbFontCharExtra = 1;
  if (pBox->dwFlags & 0x20000000) {
    nFirst = 0x23;
    nMid = 0x26;
    nLast = 0x29;
    nTile = gwCombo1OpenWid_g;
  } else {
    nFirst = 0x3B;
    nMid = 0x3E;
    nLast = 0x41;
    nTile = gwCombo2OpenWid_g;
  }
  rc = pBox->rc;
  if (!(pBox->bDropUp & 1)) {
    short sBottom = pBox->rc.bottom;
    rc.bottom = sBottom;
    rc.top = sBottom - pBox->nRowHeight;
    nStep = -pBox->nRowHeight;
    nItem = pBox->nCount - 1;
    nDir = -1;
    bFrame = nLast;
  } else {
    rc.bottom = rc.top + pBox->nRowHeight;
    nStep = pBox->nRowHeight;
    nDir = 1;
    nItem = 0;
    bFrame = nFirst;
  }
  draw_local_ctrl(rc.left, rc.top, rc.right - rc.left + 1, nTile, bFrame, prcClip, 0x2000);
  pBox->pfnDrawItem(pBox, nItem, &rc, 4, 0);
  rc.top += nStep;
  nItem += nDir;
  rc.bottom += nStep;
  for (n = pBox->nCount - 1; n > 0; n--) {
    draw_local_ctrl(rc.left, rc.top, rc.right - rc.left + 1, nTile, nMid, prcClip, 0x2000);
    pBox->pfnDrawItem(pBox, nItem, &rc, 4, 0);
    nItem += nDir;
    rc.top += nStep;
    rc.bottom += nStep;
  }
}

// combo_reset_slider -- redraw the parent (`mov ecx, dword ptr [esi+32h]`, the
// owning dialog rather than the combo), clear the selection, and collapse.
// NULL-tolerant on entry, which none of its siblings are.
void __fastcall combo_reset_slider(lbox_t *pBox) {
  if (pBox) {
    ctrl_set_update(pBox->pParent);
    pBox->nSel = 0;
    combo_close(pBox);
  }
}

// combo_lmouse_down -- drop the list open: grow the combo rectangle by one row
// per item plus the theme's open and arrow delta, publish it as the current
// combo, and set the dropped-down bit. The theme words come in pairs chosen by
// flag 0x20000000, and the oracle selects them with a branch rather than two
// cmovs, which writing the pair as two ternaries would give; each arm is one
// subtraction, written inline in both arms with no named local, and hoisting
// either loses the match. The row arithmetic is 16-bit throughout
// (`imul cx, ax` / `add cx, dx`), so a long enough list wraps at 65536 pixels
// rather than saturating. That is the oracle's behaviour, matched not repaired.
void __fastcall combo_lmouse_down(lbox_t *pBox, dlgevt_t *pEvent) {
  int nExtra;
  if (pBox->dwFlags & 0x20000000)
    nExtra = gwUnk15_g - gwUnk13_g;
  else
    nExtra = gwUnk22_g - gwUnk20_g;
  if (pBox->bDropUp & 1)
    pBox->rc.top -= (short)(pBox->nRowHeight * pBox->nCount + nExtra);
  else
    pBox->rc.bottom += (short)(pBox->nRowHeight * pBox->nCount + nExtra);

  ctrl_force_update(reinterpret_cast<ctrl_t *>(pBox));

  dlg_dword_t dwFlags = pBox->dwFlags;
  dwFlags &= ~0x40000000u;
  dwFlags |= 0x100000;
  pBox->dwFlags = dwFlags;
  sgpCurrentCombo_g = reinterpret_cast<ctrl_t *>(pBox);
}

// combo_lmouse_up -- releasing the button over a combo redraws it and collapses
// it. EDX, the event, is never touched, so nothing here can prove the second
// parameter; it is declared because every other control handler in the family
// has one and because ctrl_evt dispatches through PFN_CTRL_EVT, and an ignored
// register argument emits no code either way.
void __fastcall combo_lmouse_up(lbox_t *pBox, dlgevt_t *pEvent) {
  ctrl_set_update(reinterpret_cast<ctrl_t *>(pBox));
  combo_close(pBox);
}

// combo_restore -- the out-of-line copy of the shared helper: 37 of these 50
// bytes are what combo_lmouse_up and combo_reset_slider carry inline.
void __fastcall combo_restore(lbox_t *pBox) { combo_close(pBox); }

// gwUnk10_g / gwUnk17_g -- WORD @0x6CE714 / 0x6CE730 (dlg/dlg.cpp), the two
// row-skin tile widths, read `movzx ebx, word ptr`.
#ifdef OC_DLL_MODE
#define gwUnk10_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE714u)))
#define gwUnk17_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE730u)))
#else
extern dlg_word_t gwUnk10_g;
extern dlg_word_t gwUnk17_g;
#endif

// gsFontMargins -- rect16_t @0x6D3038 (gds/font.cpp), the text clip.
#ifdef OC_DLL_MODE
#define gsFontMargins (*reinterpret_cast<rect16_t*>(OC_VA(0x6D3038u)))
#else
extern rect16_t gsFontMargins;
#endif

// byte_50EF74 -- char[4] @0x50EF74, the "..." a clipped item ends in. The
// sheet has no name for it; it is copied byte by byte, so an array.
#ifdef OC_DLL_MODE
#define byte_50EF74 (reinterpret_cast<char*>(OC_VA(0x50EF74u)))
#else
extern char byte_50EF74[4];
#endif

const char * __fastcall font_adjust(int nX, int nY, const char *psz);

// draw_lbox_item -- the default per-item painter: the row skin, then the item
// text in the item's colour, clipped with a trailing "..." when it does not
// fit. The flag byte is read twice in the colour ternary; one read through a
// local swaps the load's SIB base and index.
void __fastcall draw_lbox_item(lbox_t *pBox, unsigned char nItem, rect16_t *prc,
                           int nXOfs, int nYOfs) {
  char szBuf[0x100];
  unsigned char bSaved = nItem;
  dlg_dword_t dwFlags = pBox->dwFlags;
  int nTile;
  int nFrame;
  int nFrameHot;
  unsigned char nColor;

  if (dwFlags & 0x20000000) {
    nFrame = 0x35;
    nFrameHot = 0x38;
    nTile = gwUnk10_g;
  } else {
    nFrame = 0x4D;
    nFrameHot = 0x50;
    nTile = gwUnk17_g;
  }
  if (pBox->nCount == 0)
    return;
  if (nItem == pBox->nHotItem && !(dwFlags & 0x10000000)) {
    nColor = 4;
    nFrame = nFrameHot;
  } else {
    nColor = (pBox->pbFlags[nItem] & 0xF) ? (unsigned char)(pBox->pbFlags[nItem] & 0xF) : 3;
  }
  if (!(dwFlags & 0x20000))
    draw_local_ctrl(prc->left, prc->top, prc->right - prc->left + 1, nTile,
                    nFrame, prc, 0x2000);
  if (bSaved >= pBox->nCount)
    return;
  font_set_color_tbl(nColor);
  gbFontJust = 0x11;
  gsFontMargins = *prc;
  gsFontMargins.left += nXOfs;
  gsFontMargins.top += nYOfs;
  const char *pszItem = (const char *)pBox->ppItems[bSaved];  // Hyperion
  SStrCopy(szBuf, pszItem, 0x100);
  int nWidth = gsFontMargins.right - gsFontMargins.left;
  if (font_strlen(szBuf) > nWidth) {
    unsigned char nLen;
    char *p;

    nWidth -= font_strlen(byte_50EF74);
    nLen = (unsigned char)strlen(szBuf);
    bSaved = 0;
    if (font_strlen(szBuf) > nWidth) {
      p = &szBuf[nLen];
      do {
        *p = bSaved;
        --nLen;
        p = &szBuf[nLen];
        bSaved = *p;
        *p = 0;
      } while (font_strlen(szBuf) > nWidth);
    }
    // Hyperion: the "..." follows a whole character
    nLen = static_cast<unsigned char>(MapTextCharStart(pszItem, pszItem + nLen) - pszItem);
    szBuf[nLen] = byte_50EF74[0];
    szBuf[nLen + 1] = byte_50EF74[1];
    szBuf[nLen + 2] = byte_50EF74[2];
    szBuf[nLen + 3] = byte_50EF74[3];
  }
  font_adjust(prc->left + nXOfs, prc->top + nYOfs, szBuf);
}

// lbox_add_item -- appends an item, copying the string when flag 0x10 asks the
// box to own it, and returns its index, or 0xFF when the box is full. A combo
// re-lays its list out on whichever side of the parent it fits.
BYTE lbox_add_item(ctrl_t *pCtrl, const char *pszItem, BYTE nFlags) {
  lbox_t *pBox = reinterpret_cast<lbox_t *>(pCtrl);
  if (pBox->nCount >= pBox->nMax)
    return 0xFF;
  const char *pszText = pszItem;
  if (nFlags & 0x10) {
    char *pszCopy = (char *)SMemAlloc((DWORD)(strlen(pszItem) + 1), "lbox.cpp", 0x193, 0);
    pszText = pszCopy;
    strcpy(pszCopy, pszItem);
  }
  pBox->pbFlags[pBox->nCount] = nFlags;
  pBox->ppItems[pBox->nCount] = (void *)pszText;
  BYTE nItem = pBox->nCount++;
  if (pBox->wType == 0x0D) {
    int nGrow = pBox->nRowHeight * pBox->nCount;
    if (pBox->rc.bottom + nGrow > pBox->pParent->rc.bottom) {
      pBox->rc.top -= nGrow;
      lbox_recalc(pBox);
      pBox->rc.top += nGrow;
      pBox->bDropUp |= 1;
      ctrl_ungray(pCtrl);
      return nItem;
    }
    pBox->rc.bottom += nGrow;
    lbox_recalc(pBox);
    pBox->rc.bottom -= nGrow;
    pBox->bDropUp = 0;
    ctrl_ungray(pCtrl);
    return nItem;
  }
  lbox_recalc(pBox);
  return nItem;
}

// lbox_calc_pos -- the row under the cursor. Above or below the list it steps
// one row from the hot item and stops the auto-scroll; inside it divides by the
// row height and sets the auto-scroll bit while the cursor is between the side
// edges.
dlg_byte_t __fastcall lbox_calc_pos(lbox_t *pBox, dlgevt_t *pEvent) {
  int nTop;
  if (pBox->wType == 0x0D && !(pBox->bDropUp & 1))
    nTop = pBox->nRowHeight + pBox->rc.top;
  else
    nTop = pBox->rc.top;
  point16_t pt;
  global_evt_2_local_point(reinterpret_cast<ctrl_t *>(pBox), pEvent, &pt);

  dlg_byte_t nPos;
  if (pt.y < nTop) {
    dlg_byte_t nHot = pBox->nHotItem;
    dlg_byte_t nFirst = pBox->nTopItem;
    pBox->dwFlags &= ~0x40000000u;
    if (nHot > nFirst)
      nPos = nFirst;
    else if (nHot)
      nPos = nHot - 1;
    else
      nPos = 0;
  } else if (pt.y > pBox->rc.bottom) {
    dlg_byte_t nFirst = pBox->nTopItem;
    dlg_byte_t nVisible = pBox->nVisible;
    dlg_byte_t nHot = pBox->nHotItem;
    pBox->dwFlags &= ~0x40000000u;
    if ((int)nHot < nFirst - 1 + nVisible) {
      nPos = nVisible - 1 + nFirst;
    } else {
      nPos = nHot;
      if ((int)nHot < pBox->nCount - 1)
        ++nPos;
    }
  } else {
    if (pt.x > pBox->rc.left && pt.x < pBox->rc.right)
      pBox->dwFlags |= 0x40000000;
    pt.y = (short)(pt.y - nTop) / pBox->nRowHeight;
    nPos = pBox->nTopItem + pt.y;
  }
  dlg_byte_t nCount = pBox->nCount;
  if (nPos >= nCount)
    nPos = nCount - 1;
  return nPos;
}

// lbox_ctrl_combo -- the combo's event handler: route the mouse events to the
// combo helpers and hand everything on to the listbox handler.
int __fastcall lbox_ctrl_combo(lbox_t *pBox, dlgevt_t *pEvent) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(pBox);
  switch (pEvent->wTag) {
  case 14:
    switch (pEvent->dwId) {
    case 10: {
      lbox_ctrl_list(pCtrl, pEvent);
      pBox->dwFlags |= 0x20000;
      dlg_word_t wTop = pBox->rc.top;
      pBox->wSaveTop = wTop;
      pBox->rc.bottom = (short)(pBox->nRowHeight + wTop);
      ctrl_evn_dlg(pCtrl, pEvent);
      return 1;
    }
    case 6:
      combo_restore(pBox);
      break;
    case 0:
      if (!pBox->nCount)
        ctrl_gray(pCtrl);
      break;
    }
    break;
  case 6:
    return 1;
  case 4:
    combo_lmouse_down(pBox, pEvent);
    break;
  case 5:
    combo_lmouse_up(pBox, pEvent);
    break;
  }
  return lbox_ctrl_list(pCtrl, pEvent);
}

// lbox_ctrl_list -- the listbox's event handler. Mouse events over a visible
// scrollbar go to the scrollbar; the rest are dispatched on the event tag, and
// the 0x0E notifications on the event id.
int lbox_ctrl_list(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  lbox_t *pBox = reinterpret_cast<lbox_t *>(pCtrl);
  ctrl_t *pScroll = pBox->pScroll;
  if (pScroll) {
    point16_t pt;
    global_evt_2_local_point(pScroll, pEvent, &pt);
    if ((pScroll->dwFlags & 8) && pt_in_rect(&pScroll->rc, &pt)) {
      switch (pEvent->wTag) {
      case 3:
      case 4:
      case 5:
      case 6:
        pScroll->pfnEvt(pScroll, pEvent);
        return 1;
      }
    }
  }

  switch (pEvent->wTag) {
  case 14:
    switch (pEvent->dwId) {
    case 10:
      lbox_init(pBox);
      break;
    case 1:
      lbox_deactivate(pBox);
      break;
    case 8:
      if (pEvent->dwParam == 0x2B)
        lbox_scroll_timer(pBox, pEvent);
      break;
    case 13:
      pBox->dwFlags |= 8;
      if (!pBox->pScroll)
        return 1;
      lbox_update_scroll(pBox, pBox->pScroll);
      return 1;
    case 14:
      lbox_hide(pBox);
      return 1;
    case 11: {
      dlg_byte_t nVal = (dlg_byte_t)pEvent->dwParam;
      dlg_byte_t nHot = pBox->nHotItem;
      if (nHot == pBox->nSel || nHot == nVal) {
        pBox->nSel = nVal;
        pBox->nHotItem = nVal;
        ctrl_set_update(pCtrl);
      }
      return 1;
    }
    }
    return ctrl_evn_dlg(pCtrl, pEvent);
  case 0:
  case 1:
    return lbox_key_down(pBox, pEvent);
  case 4:
    lbox_lmouse_down(pBox, pEvent);
    return 1;
  case 6:
    ctrl_send_cmd(pCtrl, 0x80000);
    return 1;
  case 3:
    lbox_mouse_moved(pBox, pEvent);
    return 1;
  case 5: {
    dlg_byte_t nHot = pBox->nHotItem;
    if (nHot != pBox->nSel && nHot < pBox->nCount)
      ctrl_set_val(pCtrl, nHot);
    dlg_unhook_all(pCtrl);
    timer_remove_node(pCtrl, 0x2B);
    return 1;
  }
  default:
    return 0;
  }
}

// lbox_deactivate -- suppress recalc, empty the list, then free the three
// parallel arrays.
void __fastcall lbox_deactivate(lbox_t *pBox) {
  pBox->dwFlags |= 0x80000000;
  lbox_remove_all(pBox);
  if (pBox->ppItems)
    SMemFree(pBox->ppItems, "lbox.cpp", 0x294, 0);
  if (pBox->pbFlags)
    SMemFree(pBox->pbFlags, "lbox.cpp", 0x296, 0);
  if (pBox->ppData)
    SMemFree(pBox->ppData, "lbox.cpp", 0x298, 0);
}

// draw_combo -- the combo's PFN_CTRL_DRAW: always draw the closed box, and draw
// the dropped list on top of it while flag 0x100000 is set. Both callees are
// `ret 8h`, so all four arguments are forwarded unchanged.
void __fastcall draw_combo(lbox_t *pBox, int nXOfs, int nYOfs, rect16_t *prcClip) {
  lbox_draw_combo2(pBox, nXOfs, nYOfs, prcClip);
  if (pBox->dwFlags & 0x100000)
    lbox_draw_list(pBox, nXOfs, nYOfs, prcClip);
}

// draw_lbox -- the listbox's PFN_CTRL_DRAW: font, deferred recalc, frame, then
// one call per visible row through the +0x52 item callback, walking a stack
// rectangle down by nRowHeight each time. Two shapes are worth naming. The
// suppressed-recalc bit is saved in a stack slot and restored at the end, and
// the full and rather than a sign test is what MSVC emits when the masked value
// is needed and not just its truth. The rectangle is a by-value copy of
// pBox->rc with only bottom replaced: the oracle copies it as two dwords and
// recovers rc.top from the high half of the first (`mov ecx, [esi+4h]` /
// `shr ecx, 10h`) rather than reloading the member, a memory CSE that appears
// only if the source copies the whole struct first and edits one field after.
void __fastcall draw_lbox(lbox_t *pBox, int nXOfs, int nYOfs, rect16_t *prcClip) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(pBox);
  dlg_byte_t nRowHeight = pBox->nRowHeight;
  dlg_set_font(pCtrl);

  dlg_dword_t dwFlags = pBox->dwFlags;
  dlg_dword_t dwWasSuppressed = dwFlags & 0x80000000;
  if (dwWasSuppressed) {
    pBox->dwFlags = dwFlags & 0x7FFFFFFF;
    lbox_recalc(pBox);
  }

  ctrl_set_hot_flag(pCtrl);

  dlg_dword_t dwNow = pBox->dwFlags;
  if (!(dwNow & 0x20000))
    draw_frame_rect(pCtrl, (dwNow & 0x1000) ? sgbPens_g[16] : sgbPens_g[15]);

  gbFontCharExtra = 1;

  rect16_t rc = pBox->rc;
  rc.bottom = rc.top + nRowHeight;

  // Three spellings are pinned here, each by a distinct oracle detail:
  //  * nItem is read before the row count, which schedules its load into the
  //    middle of the min computation, so the oracle can pack nCount and nItem
  //    into CL and CH of one register. Read after, MSVC uses AL instead and the
  //    halves never pair up.
  //  * The min is a ternary, not an if-assignment: both spell a cmovb, but the
  //    ternary materialises nVisible before nCount, as the oracle does.
  //  * `while (nRows--)`, not `while (nRows) { ...; --nRows; }`. The
  //    post-decrement form makes the counter the callee-saved register and
  //    spills the item index to a byte slot; the pre-decrement form swaps the
  //    two, spills the counter to a dword slot, and costs six bytes.
  dlg_byte_t nItem = pBox->nTopItem;
  dlg_byte_t nRows =
      (pBox->nVisible < pBox->nCount) ? pBox->nVisible : pBox->nCount;
  while (nRows--) {
    pBox->pfnDrawItem(pBox, nItem, &rc, 2, 0);
    rc.top += nRowHeight;
    ++nItem;
    rc.bottom += nRowHeight;
  }

  if (dwWasSuppressed)
    pBox->dwFlags |= 0x80000000;
  font_set(0);
}

// lbox_get_count -- returns the item count, which is a byte, not a word read
// narrowly.
// Hyperion: the
// payload has it too, so its callers no longer go to the original's
//.
dlg_byte_t __fastcall lbox_get_count(lbox_t *pBox) { return pBox->nCount; }

void *__fastcall lbox_get_data(lbox_t *pBox, dlg_byte_t nItem) {
  return pBox->ppData[nItem];
}

void *__fastcall lbox_get_item(lbox_t *pBox, dlg_byte_t nItem) {
  return pBox->ppItems[nItem];
}

// lbox_get_scroll -- the width the list has available: its own rectangle, less
// the scrollbar's when the bar is showing. The `movsx` loads are what make rc
// signed here, and the subtraction is spelled left-minus-right because the
// oracle does `sub ecx, eax` with EAX holding the right edge. The accumulation
// is a compound `+=`; `return expr + w` reorders the adds.
int __fastcall lbox_get_scroll(lbox_t *pBox) {
  int nWidth = pBox->rc.right - pBox->rc.left + 1;
  ctrl_t *pScroll = pBox->pScroll;
  if (pScroll && !(pScroll->dwFlags & 8))
    nWidth += pScroll->rc.left - pScroll->rc.right - 3;
  return nWidth;
}

// lbox_get_selection -- `cmp byte [ecx+46h], 0 / jz -> or al,-1 /
// mov al, [ecx+48h] / ret`. The sentinel comes from `or al, -1h`, MSVC's
// 2-byte spelling of "all ones in AL", so the return is a single byte carrying
// 0xFF for "no selection".
dlg_byte_t __fastcall lbox_get_selection(lbox_t *pBox) {
  if (pBox->nCount != 0)
    return pBox->nSel;
  return 0xFF;
}

// lbox_hide -- clears the visible (0x8) and mouse-inside (0x80) bits together:
// the single `and dword ptr [ecx+18h], -89h` is one read-modify-write on the
// member, so the source is a compound assignment and not a load, modify and
// store through a local. Then hand the scrollbar its new state, if there is one.
void __fastcall lbox_hide(lbox_t *pBox) {
  pBox->dwFlags &= ~0x88u;
  ctrl_t *pScroll = pBox->pScroll;
  if (pScroll)
    lbox_update_scroll(pBox, pScroll);
}

// lbox_init -- allocates the three item arrays (254 entries), sizes the rows
// from the font, fits the rectangle to whole rows, and creates the companion
// scrollbar when the box is taller than the two arrows and the thumb.
void __fastcall lbox_init(lbox_t *pBox) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(pBox);
  pBox->ppItems = (void **)SMemAlloc(0xFE * sizeof(void *), "lbox.cpp", 0x24D, 0);
  pBox->pbFlags = (dlg_byte_t *)SMemAlloc(0xFE, "lbox.cpp", 0x24E, 0);
  pBox->ppData = (void **)SMemAlloc(0xFE * sizeof(void *), "lbox.cpp", 0x24F, 0);
  pBox->nCount = 0;
  pBox->nMax = 0xFE;
  pBox->nSel = 0;
  pBox->nTopItem = 0;
  pBox->nHotItem = 0;
  pBox->pfnDrawItem = draw_lbox_item;
  memset(pBox->ppItems, 0, 0xFE * sizeof(void *));
  memset(pBox->pbFlags, 0, 0xFE);
  memset(pBox->ppData, 0, 0xFE * sizeof(void *));

  dlg_set_font(pCtrl);
  if (gbFontMaxHgt >= gwUnk11_g) {
    pBox->dwFlags &= ~0x20000000u;
    pBox->nRowHeight = gbFontMaxHgt;
  } else {
    pBox->dwFlags |= 0x20000000;
    pBox->nRowHeight = (dlg_byte_t)gwUnk11_g;
  }
  font_set(0);

  // nTop, read first, keeps pBox in EBX. The fit reads the field, because a
  // short local added to an int is never the base of the lea.
  short nTop = pBox->rc.top;
  dlg_byte_t nRowHeight = pBox->nRowHeight;
  dlg_word_t nRows = (short)(pBox->rc.bottom - pBox->rc.top + 1) / nRowHeight;
  pBox->nVisible = (dlg_byte_t)nRows;
  short nFit = (short)(nRowHeight * nRows);
  pBox->nScrollRange = 0;
  int nHeight = nRowHeight;
  if (nFit > nHeight)
    nHeight = nFit;
  pBox->rc.bottom = pBox->rc.top + nHeight;
  int nSpan = (short)(nTop + nHeight) - pBox->rc.top;
  if (nSpan > gwUnk6_g + 2 * gwVArrowHgt) {
    ctrl_t *pScroll = scroll_create(pBox->pParent, lbox_update_pos_callback, 7, nSpan);
    *reinterpret_cast<lbox_t **>(&pScroll->_unmapped24[2]) = pBox;
    pBox->rc.bottom = pScroll->rc.bottom + pBox->rc.top;
    pScroll->rc = pBox->rc;
    pScroll->rc.left = pBox->rc.right - gwHArrowHgt + 1;
    pScroll->rc.right = pBox->rc.right;
    pScroll->nIndex = 0;
    ctrl_hide(pScroll);
    pScroll->nIndex = pBox->nIndex - 0x8000;
    pScroll->pNext = reinterpret_cast<ctrl_t *>(pBox->pNext);
    pBox->pNext = reinterpret_cast<lbox_t *>(pScroll);
    pBox->pScroll = pScroll;
  }
}

// lbox_insert_item -- inserts an item at nIndex (clamped to the end), copying
// the string first when flag 0x10 asks the box to own it. A combo re-derives
// which way its list drops from whether the grown list still fits inside the
// parent.
void lbox_insert_item(ctrl_t *pCtrl, BYTE nIndex, const char *pszItem,
                      BYTE nFlags) {
  lbox_t *pBox = reinterpret_cast<lbox_t *>(pCtrl);
  if (pBox->nCount >= pBox->nMax)
    return;
  const char *pszText = pszItem;
  if (nFlags & 0x10) {
    char *pszCopy = (char *)SMemAlloc((DWORD)(strlen(pszItem) + 1), "lbox.cpp", 0x1C9, 0);
    pszText = pszCopy;
    strcpy(pszCopy, pszItem);
  }

  dlg_byte_t nCount = pBox->nCount;
  if (!nCount) {
    nIndex = 0;
  } else if (nIndex >= nCount) {
    nIndex = nCount;
  } else {
    memmove(&pBox->ppItems[nIndex + 1], &pBox->ppItems[nIndex], (nCount - nIndex) * sizeof(pBox->ppItems[0]));  // Hyperion: 4 in 1.17
    memmove(&pBox->pbFlags[nIndex + 1], &pBox->pbFlags[nIndex], pBox->nCount - nIndex);
    memmove(&pBox->ppData[nIndex + 1], &pBox->ppData[nIndex], (pBox->nCount - nIndex) * sizeof(pBox->ppData[0]));  // Hyperion: 4 in 1.17
  }
  pBox->ppItems[nIndex] = (void *)pszText;
  pBox->pbFlags[nIndex] = nFlags;
  pBox->ppData[nIndex] = 0;
  ++pBox->nCount;

  if (pBox->wType == 0x0D) {
    int nGrow = pBox->nRowHeight * pBox->nCount;
    if (pBox->rc.bottom + nGrow > pBox->pParent->rc.bottom)
      pBox->bDropUp |= 1;
    else
      pBox->bDropUp = 0;
    ctrl_ungray(pCtrl);
    pBox->rc.bottom += nGrow;
    lbox_recalc(pBox);
    pBox->rc.bottom -= nGrow;
  } else {
    lbox_recalc(pBox);
  }
}

// lbox_key_down -- keyboard navigation: arrows, Home and End, PgUp and PgDn move
// the selection. A box with flag 0x10000000 hands its keys to the scrollbar
// instead. It returns 0 only for a key the list does not handle.
int __fastcall lbox_key_down(lbox_t *pBox, dlgevt_t *pEvent) {
  if (pBox->dwFlags & 0x10000000) {
    ctrl_t *pScroll = pBox->pScroll;
    if (pScroll)
      return pScroll->pfnEvt(pScroll, pEvent);
  }
  dlg_byte_t nSel = pBox->nSel;
  dlg_byte_t nNew;
  switch (*reinterpret_cast<dlg_word_t *>(&pEvent->_unmapped08[0])) {
  case 0x26:
    if (!nSel || nSel > pBox->nCount - 1)
      return 1;
    nNew = nSel - 1;
    break;
  case 0x28:
    if (nSel >= pBox->nCount - 1)
      return 1;
    nNew = nSel + 1;
    break;
  case 0x24:
    if (!nSel)
      return 1;
    nNew = 0;
    break;
  case 0x23:
    if (nSel == pBox->nCount - 1)
      return 1;
    nNew = pBox->nCount - 1;
    break;
  case 0x21:
    if (!nSel || nSel > pBox->nCount - 1)
      return 1;
    if (nSel <= pBox->nVisible)
      nNew = 0;
    else
      nNew = nSel - pBox->nVisible + 1;
    break;
  case 0x22:
    if (nSel >= pBox->nCount - 1)
      return 1;
    if (nSel + pBox->nVisible < pBox->nCount - 1)
      nNew = nSel - 1 + pBox->nVisible;
    else
      nNew = pBox->nCount - 1;
    break;
  default:
    return 0;
  }
  ctrl_set_val(reinterpret_cast<ctrl_t *>(pBox), nNew);
  lbox_update_pos(pBox, pBox->nSel);
  return 1;
}

// lbox_lmouse_down -- claims three dialog hook slots, then, only inside the
// border, latches the row under the cursor exactly as lbox_scroll_timer does,
// minus the change test.
void __fastcall lbox_lmouse_down(lbox_t *pBox, dlgevt_t *pEvent) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(pBox);
  dlg_hook(pCtrl, 5);
  dlg_hook(pCtrl, 3);
  dlg_hook(pCtrl, 0x0D);
  if (pt_inside_border(pBox, pEvent)) {
    pBox->nHotItem = lbox_calc_pos(pBox, pEvent);
    ctrl_set_update(pCtrl);
    lbox_update_pos(pBox, pBox->nHotItem);
  }
}

// lbox_mouse_moved -- starts the auto-scroll timer the first time the pointer
// moves inside the border, and latches the row under it; on any other move it
// stops the timer. An oracle oddity is reproduced rather than repaired: the `jz`
// from the border test and the `jnz` from the timer-already-running test land on
// the same address, so "pointer inside and the timer already running" falls into
// the block that clears the timer flag, and a second move inside the box tears
// down the timer the first one built. Whether Blizzard meant `else if` is not
// answerable from the bytes.
void __fastcall lbox_mouse_moved(lbox_t *pBox, dlgevt_t *pEvent) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(pBox);
  if (pt_inside_border(pBox, pEvent)) {
    dlg_dword_t dwFlags = pBox->dwFlags;
    if (!(dwFlags & 0x40000000)) {
      pBox->dwFlags = dwFlags | 0x40000000;
      timer_create(pCtrl, 0x2B, 1, 0);
      dlg_byte_t nPos = lbox_calc_pos(pBox, pEvent);
      if (pBox->nHotItem != nPos) {
        pBox->nHotItem = nPos;
        ctrl_set_update(pCtrl);
        lbox_update_pos(pBox, pBox->nHotItem);
      }
      return;
    }
  }
  dlg_dword_t dwNow = pBox->dwFlags;
  if (dwNow & 0x40000000)
    pBox->dwFlags = dwNow & ~0x40000000u;
}

// lbox_recalc -- the layout pass: fit the rectangle to whole rows, clamp the
// selection, top item and scroll range, and push the result to the scrollbar. A
// combo is measured with its list temporarily grown to full length. The barrier
// after the row fit is the only shape that gives the oracle's pBox spill around
// the fit.
void __fastcall lbox_recalc(lbox_t *pBox) {
  int nGrow = 0;
  if (pBox->dwFlags & 0x80000000)
    return;
  if (pBox->wType == 0x0D) {
    nGrow = pBox->nRowHeight * pBox->nCount;
    if (pBox->rc.bottom + nGrow > pBox->pParent->rc.bottom) {
      pBox->rc.top -= nGrow;
      pBox->bDropUp |= 1;
    } else {
      pBox->bDropUp = 0;
      pBox->rc.bottom += nGrow;
    }
    ctrl_ungray(reinterpret_cast<ctrl_t *>(pBox));
  }

  dlg_byte_t nRowHeight = pBox->nRowHeight;
  dlg_word_t nRows = (short)(pBox->rc.bottom - pBox->rc.top + 1) / nRowHeight;
  pBox->nVisible = (dlg_byte_t)nRows;
  short nFit = (short)(nRowHeight * nRows);
  dlg_byte_t nCount = pBox->nCount;
  pBox->nScrollRange = 0;
  int nHeight = nRowHeight;
  if (nFit > nHeight)
    nHeight = nFit;
  pBox->rc.bottom = pBox->rc.top + nHeight;
  _ReadWriteBarrier();

  if (!nCount)
    pBox->nSel = 0xFF;
  else if (pBox->nSel >= nCount && pBox->nSel != 0xFF)
    pBox->nSel = nCount - 1;
  pBox->nHotItem = pBox->nSel;

  int nRange = nCount - pBox->nVisible;
  if (nRange < 0)
    nRange = 0;
  pBox->nScrollRange = (dlg_byte_t)nRange;
  if (pBox->nTopItem > (dlg_byte_t)nRange)
    pBox->nTopItem = (dlg_byte_t)nRange;

  pBox->dwFlags |= 0x80000000;
  if (pBox->pScroll)
    lbox_update_scroll(pBox, pBox->pScroll);
  if (pBox->wType == 0x0D) {
    if (pBox->bDropUp & 1)
      pBox->rc.top += nGrow;
    else
      pBox->rc.bottom -= nGrow;
  }
  pBox->dwFlags &= 0x7FFFFFFF;
  ctrl_set_update(reinterpret_cast<ctrl_t *>(pBox));
}

// lbox_remove_all -- suppress recalc for the duration, drain the list from the
// back, where removing the last item never shifts the others, then restore
// recalc, but only if this call is the one that suppressed it.
// `and edi, -80000000h` saves the bit's prior state before `or eax, -80000000h`
// sets it, which is what makes the function re-entrant against an outer
// lbox_set_recalc(0).
void __fastcall lbox_remove_all(lbox_t *pBox) {
  dlg_dword_t dwFlags = pBox->dwFlags;
  dlg_dword_t dwWasSuppressed = dwFlags & 0x80000000;
  pBox->dwFlags = dwFlags | 0x80000000;

  while (pBox->nCount)
    lbox_remove_item(pBox, (dlg_byte_t)(pBox->nCount - 1));

  if (!dwWasSuppressed) {
    dlg_dword_t dwNow = pBox->dwFlags;
    if (dwNow & 0x80000000) {
      pBox->dwFlags = dwNow & 0x7FFFFFFF;
      lbox_recalc(pBox);
    }
  }
}

void __fastcall lbox_remove_current(lbox_t *pBox) {
  lbox_remove_item(pBox, pBox->nSel);
}

// lbox_remove_item -- tells the box's own handler the item is going (event
// 0x0C), closes the gap in all three arrays, and frees the string if the box
// owns it. It returns the item pointer, or NULL once freed. noinline keeps it a
// call in lbox_remove_all once the payload guards its other callers out.
__declspec(noinline) void *__fastcall lbox_remove_item(lbox_t *pBox, dlg_byte_t nItem) {
  dlgevt_t evt;
  evt.dwId = 0x0C;
  evt.wTag = 0x0E;
  evt.dwParam = nItem;
  pBox->pfnEvt(reinterpret_cast<ctrl_t *>(pBox), &evt);

  dlg_byte_t bOwned = pBox->pbFlags[nItem] & 0x10;
  pBox->ppData[nItem] = 0;
  void *pItem = pBox->ppItems[nItem];
  dlg_byte_t nAfter = pBox->nCount - nItem - 1;
  if (nAfter) {
    memmove(&pBox->ppItems[nItem], &pBox->ppItems[nItem + 1], nAfter * sizeof(pBox->ppItems[0]));  // Hyperion: 4 in 1.17
    memmove(&pBox->pbFlags[nItem], &pBox->pbFlags[nItem + 1], nAfter);
    memmove(&pBox->ppData[nItem], &pBox->ppData[nItem + 1], nAfter * sizeof(pBox->ppData[0]));  // Hyperion: 4 in 1.17
  }
  if (bOwned) {
    SMemFree(pItem, "lbox.cpp", 0x409, 0);
    pItem = 0;
  }
  --pBox->nCount;
  lbox_recalc(pBox);
  return pItem;
}

// lbox_scroll_timer -- the auto-scroll tick: recompute the hot row and, only on
// a change, redraw and republish. lbox_calc_pos is called with EDX untouched,
// this function's own second parameter passing straight through, so the oracle
// emits no register setup for it. The `mov dl, byte ptr [esi+4Eh]` after the
// store is a genuine re-read of the member: ctrl_set_update clobbers memory, so
// the source has to name pBox->nHotItem again rather than reuse the local.
void __fastcall lbox_scroll_timer(lbox_t *pBox, dlgevt_t *pEvent) {
  dlg_byte_t nPos = lbox_calc_pos(pBox, pEvent);
  if (pBox->nHotItem != nPos) {
    pBox->nHotItem = nPos;
    ctrl_set_update(reinterpret_cast<ctrl_t *>(pBox));
    lbox_update_pos(pBox, pBox->nHotItem);
  }
}

// lbox_set_data -- swaps the per-item data word and returns the old one.
// `ret 4h` puts the new value on the stack: under /Gr the first two arguments
// take ECX and EDX and the third spills.
void *__fastcall lbox_set_data(lbox_t *pBox, dlg_byte_t nItem, void *pNew) {
  void *pOld = pBox->ppData[nItem];
  pBox->ppData[nItem] = pNew;
  return pOld;
}

// lbox_set_match -- selects the item whose text matches pszText
// case-insensitively and returns its index. The best-partial fallback never
// fires, since the best starts at 0x7FFFFFFF, so without an exact match the
// selection is cleared to 0xFF, as the oracle does. The two pointers walk in
// lockstep and c is read before either tolower call; an index loop or
// `q[p - pszText]` changes the register assignment.
unsigned char lbox_set_match(lbox_t *pBox, const char *pszText) {
  dlg_byte_t nBest = 0xFF;
  int nBestLen = 0x7FFFFFFF;
  for (dlg_byte_t i = 0; i < pBox->nCount; ++i) {
    const char *p = pszText;
    const char *q = (const char *)pBox->ppItems[i];
    if (!*q)
      continue;
    int c = *q;
    int nLen = 0;
    if (tolower(c) == tolower(*p)) {
      for (;;) {
        ++p;
        ++q;
        if (!*p) {
          lbox_set_selection(pBox, i);
          return i;
        }
        ++nLen;
        c = *q;
        if (tolower(c) != tolower(*p))
          break;
      }
    }
    if (nLen > nBestLen) {
      nBestLen = nLen;
      nBest = i;
    }
  }
  lbox_set_selection(pBox, nBest);
  return nBest;
}

// lbox_set_recalc -- enables or disables the deferred layout pass. Disabling
// just sets the bit; enabling clears it and runs the pending recalc, but only if
// it was actually set -- `test eax, eax / jns` is a sign test on the loaded flag
// word, which is what MSVC emits for a mask of exactly the top bit.
void __fastcall lbox_set_recalc(lbox_t *pBox, dlg_byte_t bEnable) {
  if (bEnable) {
    dlg_dword_t dwFlags = pBox->dwFlags;
    if (dwFlags & 0x80000000) {
      pBox->dwFlags = dwFlags & 0x7FFFFFFF;
      lbox_recalc(pBox);
    }
  } else {
    pBox->dwFlags |= 0x80000000;
  }
}

// lbox_set_selection -- accepts an in-range row or the 0xFF "clear selection"
// sentinel and rejects everything else. The oracle's `cmp bl, [esi+46h] / jb`
// puts the argument on the left, so the source comparison is
// `nItem < pBox->nCount` and not the transpose.
void __fastcall lbox_set_selection(lbox_t *pBox, dlg_byte_t nItem) {
  if (nItem < pBox->nCount || nItem == 0xFF) {
    ctrl_set_val(reinterpret_cast<ctrl_t *>(pBox), nItem);
    lbox_update_pos(pBox, nItem);
  }
}

// lbox_update_pos -- scrolls the view so nItem is inside it, and tells the
// scrollbar. Three exits, two of them tail calls into ctrl_set_val. Every
// pBox->pScroll is a fresh load, and that is the oracle rather than a missed
// CSE: `mov ecx, dword ptr [esi+36h]` appears four separate times, each after a
// call that could have changed it, and caching it in a local collapses them.
// The window test is done in int, not in bytes -- the oracle widens all three
// operands and branches with a signed `jl`, so nTop + nVisible is computed at
// 32 bits and cannot wrap the way the byte fields would -- while the new top is
// byte arithmetic (`mov al, bl` / `sub al, dl` / `inc al`).
void __fastcall lbox_update_pos(lbox_t *pBox, dlg_byte_t nItem) {
  ctrl_t *pScroll = pBox->pScroll;
  if (pScroll) {
    dlg_dword_t dwFlags = pScroll->dwFlags;
    if (dwFlags & 0x60000000) {
      pScroll->dwFlags = dwFlags & ~0x60000000u;
      ctrl_set_update(pBox->pScroll);
    }
  }

  dlg_byte_t nTop = pBox->nTopItem;
  if (nItem < nTop) {
    pBox->nTopItem = nItem;
    if (pBox->pScroll)
      ctrl_set_val(pBox->pScroll, nItem);
    return;
  }

  dlg_byte_t nVisible = pBox->nVisible;
  if ((int)nItem >= (int)nVisible + (int)nTop) {
    dlg_byte_t nNewTop = (dlg_byte_t)(nItem - nVisible + 1);
    pBox->nTopItem = nNewTop;
    if (pBox->pScroll && nItem != 0xFF)
      ctrl_set_val(pBox->pScroll, nNewTop);
  }
}

// lbox_update_pos_callback -- the scrollbar's notification back into the
// listbox: copy the scrollbar's own position byte (+0x3A on the scroll node, a
// different per-type payload from this one, reached through dialog.h's
// _unmapped36 window) into nTopItem and invalidate. Suppressed entirely while
// recalc is off.
void __fastcall lbox_update_pos_callback(lbox_t *pBox) {
  if (!(pBox->dwFlags & 0x80000000)) {
    pBox->nTopItem = pBox->pScroll->_unmapped36[CTRL_SLOT2];  // Hyperion: scroll_t::nPos
    ctrl_set_update(reinterpret_cast<ctrl_t *>(pBox));
  }
}

// lbox_update_scroll -- show or hide the companion scrollbar to match the list,
// and give the box back, or take away, the width the bar occupies. The
// duplicated widen expression is the source, not a goto: the oracle's
// `jmp $+11h` at 0x4B lands on the same four instructions the ctrl_gray arm
// falls into at 0x5E, which is MSVC tail-merging two identical statement
// sequences, so writing them twice is what reproduces it. The two flag updates
// are spelled differently because the oracle spells them differently: the arm
// that already holds the loaded flag word writes back through it, while the arm
// that has just returned from ctrl_gray does a single read-modify-write on the
// member, because the call invalidated the load.
void __fastcall lbox_update_scroll(lbox_t *pBox, ctrl_t *pScroll) {
  if (pBox->nCount > pBox->nVisible) {
    dlg_dword_t dwScroll = pScroll->dwFlags;
    if (!(dwScroll & 8) && (pBox->dwFlags & 8)) {
      pScroll->dwFlags = dwScroll | 8;
      ctrl_ungray(pScroll);
      pBox->rc.right += pScroll->rc.left - pScroll->rc.right - 3;
    }
    dwScroll = pScroll->dwFlags;
    if ((dwScroll & 8) && !(pBox->dwFlags & 8)) {
      pScroll->dwFlags = dwScroll & ~8u;
      pBox->rc.right += pScroll->rc.right - pScroll->rc.left + 3;
    }
  } else if (pScroll->dwFlags & 8) {
    ctrl_gray(pScroll);
    pScroll->dwFlags &= ~8u;
    pBox->rc.right += pScroll->rc.right - pScroll->rc.left + 3;
  }

  scroll_set_page_size(pScroll, pBox->nVisible, 1);
  scroll_set_range(pScroll, 0, pBox->nScrollRange);
  if (pBox->nSel != 0xFF)
    ctrl_set_val(pScroll, pBox->nTopItem);
}

// pt_inside_border -- is the event's cursor inside the list area, the control
// rectangle less the scrollbar's width and, for a wType 0x0D node, less one row
// at whichever edge the drop-down grows from. Two source shapes are pinned:
// `int nBar = 0;` is declared before the global_evt_2_local_point call so it
// lives across it in a callee-saved register, and rc.right is an assignment
// that re-reads the member rather than `rc.right -= nBar`. The rectangle is
// again a whole-struct copy edited afterwards, proven as in draw_lbox by the
// `else` arm recovering rc.top from the high half of the dword it already
// loaded. The scrollbar width is counted when the bar's visible bit is set here
// and when it is clear in lbox_get_scroll: the two senses are genuinely
// opposite.
int __fastcall pt_inside_border(lbox_t *pBox, dlgevt_t *pEvent) {
  int nBar = 0;
  point16_t pt;
  global_evt_2_local_point(reinterpret_cast<ctrl_t *>(pBox), pEvent, &pt);

  ctrl_t *pScroll = pBox->pScroll;
  if (pScroll && (pScroll->dwFlags & 8))
    nBar = pScroll->rc.right - pScroll->rc.left;

  rect16_t rc = pBox->rc;
  rc.right = (short)(pBox->rc.right - nBar);
  if (pBox->wType == 0x0D) {
    if (pBox->bDropUp & 1)
      rc.bottom -= pBox->nRowHeight;
    else
      rc.top += pBox->nRowHeight;
  }
  return pt_in_rect(&rc, &pt);
}
