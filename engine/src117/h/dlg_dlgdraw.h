// dlg_dlgdraw.h -- the interface of src117/lang/dlg_dlgdraw.cpp: the dialog
// invalidation and region layer -- rectangle clipping, the cursor and context
// overlap tests, and the accumulation of dirty rectangles into the Storm region
// the frame's final blit consumes
//
// Two names this TU reaches are declared by the header of the TU they belong
// to, not here: dlg_area_invalid in src117/h/dlg_ctrl.h, which is dlg_ctrl.cpp's
// callee although this TU defines it, and img_update in src117/h/dlg_dlg.h.
// Scalars are spelled in plain C types rather than dialog.h's dlg_byte_t and
// dlg_dword_t.
//
// Include after <stddef.h>: the static_asserts use offsetof.
#pragma once

#include "platform.h"  // OC_X86_LAYOUT
#include "dialog.h"
#include "bmp.h"
#include "rect16.h"
#include "rect32.h"
#include "TSList.h"

// rendertype_t -- the 6-byte object at 0x6CE77C (sgwRenderTypePercentage_g).
// sub_4C4D50 copies all six bytes in with MSVC's small-struct split, a dword
// then a word, but the members are three words: sub_4C51C0 and sub_4C5630 read
// +0x00 and +0x04 back with `movzx`, so the dword store is the compiler fusing
// the first two. What the three mean is not derived, so they are named by
// offset. The type lives here because it has one owner, where rect32_t and
// img_t earned their own headers by having two.
struct rendertype_t {
  /*0x00*/ short w0;
  /*0x02*/ short w2;
  /*0x04*/ short w4;
};

static_assert(sizeof(rendertype_t)      == 0x06, "rendertype_t size  sub_4C4D50 copies dword+word; next name is 0x6CE784");
static_assert(offsetof(rendertype_t, w0) == 0x00, "rendertype_t::w0   sub_4C51C0: movzx ecx, word ptr [0x6CE77C]");
static_assert(offsetof(rendertype_t, w2) == 0x02, "rendertype_t::w2   the upper half of sub_4C4D50's fused dword store");
static_assert(offsetof(rendertype_t, w4) == 0x04, "rendertype_t::w4   sub_4C5630: movzx ecx, word ptr [0x6CE780]");

// img_area_invalid -- oracle 0x4CAC50 (185 B). Four __fastcall arguments, two
// in ECX and EDX and two on the stack, closing `ret 8h`. The bounds it clamps
// against -- 640 for the first and third, 480 for the second and fourth -- give
// the order: left, top, right, bottom.
void __fastcall img_area_invalid(int nLeft, int nTop, int nRight, int nBottom);

// img_area_is_invalid -- oracle 0x4CAD10 (151 B). The same four-argument shape,
// read off the DlgCombineAllRects call site. The return is one byte wide: the
// caller tests AL, not EAX. Nothing distinguishes `bool` from `unsigned char`
// there, and `unsigned char` is the spelling that agrees with the other
// declarations of it in the tree.
unsigned char __fastcall img_area_is_invalid(int nLeft, int nTop, int nRight, int nBottom);

// CombineRectInRgn -- oracle 0x4C5490 (157 B), defined in this TU. One register
// argument, plain `ret`.
void __fastcall CombineRectInRgn(const rect32_t *prc);

// The TU's own functions, in the oracle's own .text order, which under /Ox is
// Blizzard's declaration order; each body's address and size are in its .cpp
// banner.
int __fastcall ClipRect16(rect16_t *prc, const rect16_t *prcClip);
int __fastcall ClipRect32(rect32_t *prc, const rect32_t *prcClip);
int __fastcall context_in_rect(const rect16_t *prc);
int __fastcall cursor_in_rect(const rect16_t *prc);

// UpdateCursorRect -- oracle 0x4C4BC0 (379 B). No arguments and no return:
// neither ECX nor EDX is read before being written, and both epilogues close on
// a bare `ret`, so nothing arrives on the stack either. The two epilogues are
// the early-out the .cpp describes, not two argument shapes. For `void(void)`
// the conventions emit the same bytes, so the __fastcall is the TU's own, and
// the declaration must spell it as the definition does.
void __fastcall UpdateCursorRect(void);

void __fastcall set_dlg_target_bitmap(bmp_t *pBmp);
void __fastcall dlg_set_all_update(void);
void __fastcall dlg_rgn_free(void);
void __fastcall dlg_draw_in_rect(ctrl_t *pDlg, const rect16_t *prc);
void __fastcall DlgCombineAllRects(void);
void __fastcall CombineDlgInRgn(ctrl_t *pDlg);

// 0x4C4D50 (18 B). One `const rendertype_t *` in ECX, copied whole into
// sgwRenderTypePercentage_g by the fused dword and word store the
// static_asserts above pin.
void __fastcall sub_4C4D50(const rendertype_t *pSrc);

// CTRLNODE -- a dialog-tree node queued for redraw. create_ctrlnode allocates
// it with `push 1Ch / call SMemAlloc`, zeroes the link at +0 and +4 and stores
// the control at +8; the remaining 0x10 bytes are not touched there.
#pragma pack(push, 1)
struct CTRLNODE {
  /*0x00*/ TSLink<CTRLNODE> link;
  /*0x08*/ ctrl_t *pCtrl;
  // The rectangle a deferred redraw is clipped to; dlg_update_fcn copies it
  // whole with one movdqu.
  /*0x0C*/ rect32_t rcDirty;

  // Inline, defined in dlg_dlgdraw.cpp: unlinks the node.
  ~CTRLNODE();
  // Storm's inlined operator delete, under its "delete" tag.
  void operator delete(void *p);
};
#pragma pack(pop)
OC_X86_LAYOUT(sizeof(CTRLNODE) == 0x1C, "CTRLNODE size     create_ctrlnode: push 1Ch / call SMemAlloc");
static_assert(offsetof(CTRLNODE, link) == 0x00, "CTRLNODE::link    DeleteNode: mov edx, dword ptr [esi] / mov ecx, dword ptr [esi+4h]");
OC_X86_LAYOUT(offsetof(CTRLNODE, pCtrl) == 0x08, "CTRLNODE::pCtrl   create_ctrlnode: stores the control at [esi+8h]");
OC_X86_LAYOUT(offsetof(CTRLNODE, rcDirty) == 0x0C, "CTRLNODE::rcDirty dlg_update_fcn: movdqu xmmword ptr [esi+0Ch], xmm0");

// The list of CTRLNODEs, with the two members the game's own code reaches.
#pragma pack(push, 1)
template <> struct TSList<CTRLNODE,TSGetLink<CTRLNODE> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<CTRLNODE> link;

  // Inline, defined in dlg_dlgdraw.cpp: a local list starts empty, and on the
  // way out unlinks every node and then its own terminator.
  TSList();
  ~TSList();

  // 0x4C46F0 (103 B), `ret 4h`. Unlinks and frees pNode, returns its successor.
  CTRLNODE *DeleteNode(CTRLNODE *pNode);

  // 0x4C4760 (63 B). Unlinks every node without freeing any.
  void UnlinkAll(void);
};
#pragma pack(pop)
typedef TSList<CTRLNODE,TSGetLink<CTRLNODE> > CTRLNODELIST;
OC_X86_LAYOUT(sizeof(CTRLNODELIST) == 12, "CTRLNODELIST size  src117/h/TSList.h layout");
static_assert(offsetof(CTRLNODELIST, _unmapped_00) == 0x00, "CTRLNODELIST::_unmapped_00 TSList.h layout");
static_assert(offsetof(CTRLNODELIST, link) == 0x04, "CTRLNODELIST::link  UnlinkAll: mov eax, dword ptr [edi+8h] is link.next");

// create_ctrlnode -- oracle 0x4C4910 (460 B). Queues pDlg and each visible
// child overlapping prc for redraw; ECX and EDX, returns 1 or 0 to
// dlg_update_fcn, which reads the full 32-bit result.
int __fastcall create_ctrlnode(ctrl_t *pDlg, const rect32_t *prc);

// sub_4C51C0 -- oracle 0x4C51C0 (356 B). Redraws one node into its dialog's
// bitmap through its pfnDraw, clipped to prcDirty; called by dlg_update_fcn.
void __fastcall sub_4C51C0(ctrl_t *pCtrl, const rect32_t *prcDirty);

// sub_4C5630 -- oracle 0x4C5630 (524 B). Repaints a dialog's bitmap inside
// prcDirty; ECX and EDX, no stack words, and dlg_update_fcn discards EAX.
void __fastcall sub_4C5630(ctrl_t *pDlg, const rect32_t *prcDirty);
