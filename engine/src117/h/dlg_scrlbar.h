// dlg_scrlbar.h -- declarations for src117/lang/dlg_scrlbar.cpp, the horizontal
// and vertical SCROLLBAR controls: hit testing, the auto-repeat drag, the
// position and range accessors, and the two draw entries. The free functions are
// __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_DLG_SCRLBAR_H
#define OC117_DLG_SCRLBAR_H

#include "platform.h"
#include "dialog.h"
#include "rect16.h"

// scroll_ctrl_scroll (0x4C9630), scroll_get_pos (0x4C9920),
// scroll_set_page_size (0x4C9AD0) and scroll_set_range (0x4C9B20) are declared
// in dlg_lbox.h, not here; a second spelling would be a second mangled name.

// The scrollbar node, defined in dlg_scrlbar.cpp beside the code that reads it,
// as lbox_t is: the scrollbar arm of the dialog manager's one node type, four of
// whose fields sit where dialog.h has the dialog arm's (+0x3E to +0x4A).
struct scroll_t;

// draw_hscroll (0x4C8D10) / draw_vscroll (0x4C9060) and their `_native`
// siblings (0x4C8ED0 / 0x4C9210) are all PFN_CTRL_DRAW: ECX, EDX and two stack
// dwords. Each parent tail-calls its `_native` entry with both stack dwords.
void draw_hscroll(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void draw_hscroll_native(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void draw_vscroll(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void draw_vscroll_native(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);

// 0x4C9290. One register argument. All five consuming callers narrow EAX to 16
// bits and none sign-extends it.
unsigned short scroll_calc_thumb_offset(scroll_t *pScroll);

// 0x4C9320. One register argument. Signed: scroll_check_state and draw_vscroll
// sign-extend the result.
int scroll_calc_thumb_run(scroll_t *pScroll);

// 0x4C93B0. ECX + EDX, both 32-bit inputs; EAX at the ret is ctrl_set_update's
// leftover and no caller reads it.
void scroll_check_state(scroll_t *pScroll, dlgevt_t *pEvent);

// 0x4C9580. ECX, EDX and two stack dwords; the first stack slot is read 2 bytes
// wide. Returns the node SMemAlloc handed back. pfnNotify is the
// `void __fastcall (void *)` the position setters call back through; the .cpp
// names its real type.
ctrl_t *scroll_create(ctrl_t *pParent, void *pfnNotify, unsigned short wType, int nSpan);
#if !defined(_MSC_VER)
// clang and gcc: a string literal or a function as the void * (h/compiler.h, OcVoidPtr)
template <typename T> inline ctrl_t *scroll_create(ctrl_t *pParent, T *pfnNotify, unsigned short wType, int nSpan)
{
  return scroll_create(pParent, OcVoidPtr(pfnNotify), wType, nSpan);
}
#endif

// 0x4C9790. ECX + EDX; EDX is only forwarded, so its pointee is the callees'.
// Returns the part under the cursor, 0 for none.
int scroll_find_part(scroll_t *pScroll, dlgevt_t *pEvent);

// 0x4C9930. One register argument; EAX at the ret is a leftover.
void scroll_init(scroll_t *pScroll);

// 0x4C99B0. ECX + EDX. No caller; every path returns an explicit 0 or 1.
BOOL scroll_key_down(scroll_t *pScroll, dlgevt_t *pEvent);

// 0x4C9A70. ECX + EDX. Its one consuming caller uses only AL.
unsigned char scroll_lmouse_down(scroll_t *pScroll, dlgevt_t *pEvent);

// 0x4C9AE0. ECX + EDX, EDX read 16-bit and compared signed. It ends in a tail
// jump to ctrl_set_update.
void scroll_set_pos(scroll_t *pScroll, short nPos);

// 0x4C9B90. ECX + EDX; EDX is only forwarded to scroll_check_state. EAX is
// written on no path to a ret.
void scroll_timer(scroll_t *pScroll, dlgevt_t *pEvent);

// 0x4C9BE0. ECX, EDX and two stack dwords; the first is read 2 bytes wide and
// lands in a signed field. EAX at the ret is a leftover.
void start_scroll(scroll_t *pScroll, dlgevt_t *pEvent, short nDelta, ULONG dwPart);

#endif  // OC117_DLG_SCRLBAR_H
