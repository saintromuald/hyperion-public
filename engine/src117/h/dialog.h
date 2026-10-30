// dialog.h -- the dialog manager's node type: one ctrl_t for both a dialog and
// a control, the event record its handlers receive, and the two handler
// signatures. Include after <stddef.h>: the static_asserts use offsetof.
//
// `wType == 0` means the node is a dialog (a root); any other value means a
// control owned by the dialog at +0x32, and a dozen functions open by
// normalising whatever node they were handed to that owner.
//
// The struct is `#pragma pack(1)`: the oracle puts 4-byte pointers at +0x2A,
// +0x32, +0x3E, +0x42 and +0x46, each misaligned, so under the default /Zp8
// every field from +0x2A on shifts by two bytes. sizeof is a derived lower
// bound, not an allocation size -- see the note at the bottom of this file.
#pragma once

#include "platform.h"  // OC_X86_LAYOUT
#include "rect16.h"
#include "point16.h"   // point16_t, in its own header like rect16_t

// Local spellings, so this header depends on no platform header. A TU that also
// includes Storm.h gets the same widths there; these are file-scope typedefs,
// not macros, so they cannot collide destructively.
typedef unsigned char  dlg_byte_t;
typedef unsigned short dlg_word_t;
typedef ULONG  dlg_dword_t;

struct ctrl_t;
struct dlgevt_t;

// The control event handler stored at +0x2A. Every call site passes the control
// in ECX and the event in EDX, which under /Gr is a plain two-argument free
// function; ctrl_show and ctrl_find test the truth value it leaves in EAX.
typedef int(__fastcall *PFN_CTRL_EVT)(ctrl_t *pCtrl, dlgevt_t *pEvent);

// The +0x2E slot is not a second PFN_CTRL_EVT: it is a four-argument function,
// ECX + EDX + two stack dwords, callee-cleaned. Only the dispatch site
// (sub_4C51C0) shows the EDX argument, because a callee that ignores a register
// parameter compiles to the same bytes as one that has none. EDX and the first
// stack dword are a signed X and Y offset, not surfaces; the last argument is a
// clip rectangle, which draw_dlg reads as a rect16_t.
typedef void(__fastcall *PFN_CTRL_DRAW)(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                        rect16_t *prcClip);

// dlgevt_t -- the event record every control handler receives in EDX, 0x14
// bytes: the event raisers build one with `sub esp, 14h` and ctrl_find copies
// an incoming one whole as 16 + 4 bytes. They fill it in part -- ctrl_set_val
// writes only +0x00, +0x04 and +0x0C, leaving +0x08 and +0x0E/+0x10 holding
// whatever was on the stack -- which is 1.17's own behaviour, reproduced rather
// than repaired.
struct dlgevt_t {
  // +0x00 the event id, which ctrl_rect dispatches on through a jump table.
  /*0x00*/ dlg_dword_t dwId;
  // +0x04 the event parameter, ctrl_send_msg's stack argument passed through.
  /*0x04*/ ULONG_PTR dwParam;
  // +0x08 a per-event-id payload, left unnamed: ctrl_dlg_char reads it as a
  // virtual key plus modifier bits, but that is one event id's reading of the
  // slot and none of the raisers write it.
  /*0x08*/ dlg_byte_t _unmapped08[4];
  // +0x0C an event discriminant, stored as a word. Every raiser in this family
  // stores 0x0E, and ctrl_rect and ctrl_text refuse to dispatch on anything
  // else, but statcmd_btn_ctrl_fcn switches over the range 3..14 with 0x0E the
  // top arm. The name stays `wTag`: the other values are not derived.
  /*0x0C*/ dlg_word_t wTag;
  // +0x0E / +0x10 the cursor position when the event was raised, filled from
  // the two cursor globals and compared signed 16-bit against a ctrl_t rect.
  /*0x0E*/ short nX;
  /*0x10*/ short nY;
  // +0x12 the rest of the 0x14 bytes ctrl_find copies; nothing reads it.
  /*0x12*/ dlg_byte_t _unmapped12[2];
};

OC_X86_LAYOUT(sizeof(dlgevt_t)           == 0x14, "dlgevt_t size    ctrl_find: movdqu 16B + mov dword 4B");
static_assert(offsetof(dlgevt_t, dwId)   == 0x00, "dlgevt_t::dwId   ctrl_set_val: mov dword ptr [ebp-14h], 0Bh");
OC_X86_LAYOUT(offsetof(dlgevt_t, dwParam) == 0x04, "dlgevt_t::dwParam ctrl_send_msg: mov dword ptr [ebp-10h], eax");
OC_X86_LAYOUT(offsetof(dlgevt_t, wTag)   == 0x0C, "dlgevt_t::wTag   statcmd_btn_ctrl_fcn: movzx eax, word ptr [edi+0Ch] -> jump table over 3..14");
OC_X86_LAYOUT(offsetof(dlgevt_t, nX)     == 0x0E, "dlgevt_t::nX     cursor_in_dlg: mov si, word ptr [edx+0Eh]");
OC_X86_LAYOUT(offsetof(dlgevt_t, nY)     == 0x10, "dlgevt_t::nY     cursor_in_dlg: mov ax, word ptr [edx+10h]");

// Hyperion: the per-type windows of ctrl_t hold dwords or pointers by the
// control's type, so at 64 bits each is as wide as the pointers it holds, and a byte
// index into one is the same on both widths up to its first pointer
//:
//   +0x0C  four bytes, a bmp_t's size; at 64 bits four more, bmp_t's padding
//          before its pPixels, which is nResIndex;
//   +0x24  a word, then a dword or a pointer at [2];
//   +0x36  two dwords or pointers, the second at [CTRL_SLOT2] (a list box's scroll
//          bar and items, a scroll bar's callback and position), a bmp_t or a
//          rectangle;
//   +0x4E  four bytes, then a dword or a pointer at [4] (a list box's painter).
// A dialog file keeps the 32-bit records, which PortDialogNative
// (src117/port/data/port_formats.cpp) turns into these. scroll_t and lbox_t
// (dlg_scrlbar.cpp, dlg_lbox.cpp) lay the same node out their way and assert that
// they agree with this one.
#define CTRL_SLOT2 sizeof(void *)
// A list box's selected item, the byte at +0x48 in the +0x46 union (lbox_t::nSel),
// for the TUs that read it through a ctrl_t.
#define CTRL_LBOX_SEL(p) (reinterpret_cast<BYTE *>(&(p)->rcItems)[2])

#pragma pack(push, 1)
struct ctrl_t {
  // +0x00 next sibling; every walker stops on NULL.
  /*0x00*/ ctrl_t *pNext;
  // +0x04 the node's rectangle -- left, top, right, bottom, read signed -- in
  // the owning dialog's coordinates for a control and in screen coordinates
  // for a dialog.
  /*0x04*/ rect16_t rc;
  // +0x0C four bytes dlg_load_ctrl hands its measurement callback for a
  // wType == 5 node; what the callback writes there is outside this file.
  /*0x0C*/ dlg_byte_t _unmapped0C[sizeof(void *)];
  // +0x10 a signed resource index: dlg_load_ctrl tests its sign, and on the
  // negative branch resolves it against the load base and sets flag 0x20.
  /*0x10*/ LONG_PTR nResIndex;
  // +0x14 the node's text or per-type data pointer, stored by ctrl_set_text.
  // dlg_load_ctrl relocates it by the load base and lowercases its first
  // character when flag 0x200 is set; ctrl_set_mouse_in and
  // ctrl_clear_mouse_in gate a redraw on it being non-NULL.
  /*0x14*/ void *pData;
  // +0x18 the flag word, a DWORD: ctrl_dlg_set_current tests 0x20000000 and
  // ctrl_set_med_text clears 0x4C00. The bits with evidence:
  //   0x00000001  needs redraw, set by ctrl_set_update
  //   0x00000002  disabled, set by ctrl_gray; ctrl_is_hot rejects the node
  //   0x00000008  visible
  //   0x00000010  locatable, and ctrl_query_locate's result
  //   0x00000020  the +0x10 index was negative, set by dlg_load_ctrl
  //   0x00000080  the mouse is inside, set by ctrl_set_mouse_in
  //   0x00000100  the first text character is an accelerator, matched as stored
  //   0x00000200  the same, case-insensitively: load lowercases that character
  //               and ctrl_check_hotkey_upper uppercases it back to match
  //   0x00000400 / 0x800 / 0x4000  text size, cleared together and set singly
  //   0x00001000  hot, set and cleared by ctrl_set_hot_flag
  //   0x10000000  dialog-level, tested by dlg_area_invalid on the owner
  //   0x20000000  dialog-level, suppresses focus, tested on the owner
  /*0x18*/ dlg_dword_t dwFlags;
  /*0x1C*/ dlg_byte_t _unmapped1C[4];
  // +0x20 the control's index, a signed short: ctrl_set_evt_fcns branches on
  // its sign and sign-extends it into an array index, ctrl_find_ndx compares it
  // 16-bit, and ctrl_query_locate treats 0xFFFF as a sentinel.
  /*0x20*/ short nIndex;
  // +0x22 the node kind, a word. 0 means the node is itself a dialog; 1 is the
  // push button ctrl_is_hot, ctrl_set_hot_flag and ctrl_update_defpushbutton
  // single out, the last publishing it to the 0x6CE654 global; 5 and 0x0E get
  // their own handling in dlg_load_ctrl, which also indexes the two per-type
  // handler tables with it.
  /*0x22*/ dlg_word_t wType;
  /*0x24*/ dlg_byte_t _unmapped24[2 + sizeof(void *)];
  // +0x2A the event handler, installed by dlg_load_ctrl from the per-type table
  // gCtrlEvtFcnTbl at 0x4F10F8 and called indirectly by ctrl_evt and by every
  // function that raises an event.
  /*0x2A*/ PFN_CTRL_EVT pfnEvt;
  // +0x2E the draw handler, installed by dlg_load_ctrl from the sibling table
  // gCtrlDrawFcnTbl at 0x4F1148; ctrl_flc_init overwrites it with a fixed
  // address. Its signature is not pfnEvt's -- see PFN_CTRL_DRAW above.
  /*0x2E*/ PFN_CTRL_DRAW pfnDraw;
  // +0x32 the owning dialog, written by dlg_load_ctrl for a non-dialog node. A
  // reader tells a dialog by its wType, not by this being NULL.
  /*0x32*/ ctrl_t *pParent;
  // +0x36 the start of the per-type payload: ctrl_flc_init uses +0x36 to +0x3C
  // as four words it offsets by the node's own left and top, while for a
  // wType == 0x0D node pt_inside_border reads +0x36 as another ctrl_t. Only the
  // dialog arm below is derived; the rest of the region stays unnamed.
  /*0x36*/ dlg_byte_t _unmapped36[2 * sizeof(void *)];
  // +0x3E dialog only -- the focused control, stored by ctrl_dlg_set_current
  // and read off the parent by ctrl_is_hot and ctrl_set_hot_flag.
  /*0x3E*/ ctrl_t *pFocus;
  // +0x42 dialog only -- head of the child list. "First child if I am a dialog,
  // else my next sibling" is how ctrl_chain, ctrl_chainr, ctrl_set_evt_fcns and
  // ctrl_update_defpushbutton walk a node. For a wType == 0x0E node the slot is
  // a list of dlgitem_t instead, so the element type is not common.
  /*0x42*/ ctrl_t *pChildren;

  // +0x46 two readings of one slot, both real and keyed on wType, so the slot
  // is a union. As a pointer it is the control the mouse is inside, read and
  // stored by the ctrl_*_mouse_in family; as a word rect it is a second
  // rectangle dlg_load_ctrl offsets by the node's own left and top, and the
  // `jnz $+38h` that skips that block covers all of it, so the rect belongs to
  // wType == 0x0E alone. The dialog arm runs on at +0x4A, so its two members
  // are grouped.
  union {
    struct {
      // +0x46 the control the mouse is currently inside, or NULL.
      /*0x46*/ ctrl_t *pMouseIn;
      // +0x4A a dialog-level callback: dlg_activate zeroes it, dlgfatal_activate
      // stores a code address and dlg_run_modal loads it back. Not called from
      // this file, so the type stays void * rather than a guessed signature.
      /*0x4A*/ void *pfnDlgProc;
    };
    // +0x46 wType == 0x0E only: a second rectangle, relocated into the
    // dialog's coordinates at load time.
    /*0x46*/ rect16_t rcItems;
  };
  // +0x4E unnamed remainder, and another per-type payload. ctrl_flc_init and
  // ctrl_btnglow_flc_evn write the byte at +0x4E; on a listbox +0x50 is a saved
  // top row and +0x52 a per-item draw callback whose signature is not pfnDraw's
  // (`void __fastcall(ctrl_t*, BYTE, rect16_t*, int, int)`: the default
  // draw_lbox_item closes `ret 0Ch` where PFN_CTRL_DRAW closes `ret 8h`).
  // src117/lang/dlg_lbox.cpp lays the same node out as a listbox.
  /*0x4E*/ dlg_byte_t _unmapped4E[4 + sizeof(void *)];
};
#pragma pack(pop)

// 0x56 is a derived lower bound, not an allocation size: +0x52 holds a 4-byte
// function pointer on a listbox node, so the object reaches +0x55, and nothing
// bounds it from above -- control nodes arrive as a relocated file blob and are
// only ever pointer-chased through the +0x00 and +0x42 chains, so there is no
// allocation site or array stride to measure. It is safe for field access
// through a node the game gave us, where only the individually asserted offsets
// matter, and not for allocating, copying or indexing one. It has gone up once
// already, from 0x4F, when a control kind the first sweep excluded reached
// further than the sweep could see.
OC_X86_LAYOUT(sizeof(ctrl_t)                == 0x56, "ctrl_t size      DERIVED LOWER BOUND -- see caveat above, not an allocation size");
static_assert(offsetof(ctrl_t, pNext)       == 0x00, "ctrl_t::pNext     ctrl_tab_next: mov eax, dword ptr [ecx]");
OC_X86_LAYOUT(offsetof(ctrl_t, rc)          == 0x04, "ctrl_t::rc        cursor_in_dlg: cmp si, word ptr [ecx+4h]");
OC_X86_LAYOUT(offsetof(ctrl_t, nResIndex)   == 0x10, "ctrl_t::nResIndex dlg_load_ctrl: mov eax, dword ptr [esi+10h] / jns");
OC_X86_LAYOUT(offsetof(ctrl_t, pData)       == 0x14, "ctrl_t::pData     ctrl_set_text: mov dword ptr [ecx+14h], edx");
OC_X86_LAYOUT(offsetof(ctrl_t, dwFlags)     == 0x18, "ctrl_t::dwFlags   ctrl_dlg_set_current: test dword ptr [ecx+18h], 20000000h");
OC_X86_LAYOUT(offsetof(ctrl_t, nIndex)      == 0x20, "ctrl_t::nIndex    ctrl_find_ndx: cmp word ptr [eax+20h], dx");
OC_X86_LAYOUT(offsetof(ctrl_t, wType)       == 0x22, "ctrl_t::wType     ctrl_find_ndx: cmp word ptr [ecx+22h], 0h");
OC_X86_LAYOUT(offsetof(ctrl_t, pfnEvt)      == 0x2A, "ctrl_t::pfnEvt    ctrl_evt: mov eax, dword ptr [eax+2Ah] / jmp eax");
OC_X86_LAYOUT(offsetof(ctrl_t, pfnDraw)     == 0x2E, "ctrl_t::pfnDraw   dlg_load_ctrl: mov eax,[ecx*4+4F1148h] (gCtrlDrawFcnTbl)");
OC_X86_LAYOUT(offsetof(ctrl_t, pParent)     == 0x32, "ctrl_t::pParent   ctrl_find_ndx: mov ecx, dword ptr [ecx+32h]");
OC_X86_LAYOUT(offsetof(ctrl_t, pFocus)      == 0x3E, "ctrl_t::pFocus    ctrl_dlg_set_current: mov dword ptr [ecx+3Eh], edx");
OC_X86_LAYOUT(offsetof(ctrl_t, pChildren)   == 0x42, "ctrl_t::pChildren ctrl_find_ndx: mov eax, dword ptr [ecx+42h]");
OC_X86_LAYOUT(offsetof(ctrl_t, pMouseIn)    == 0x46, "ctrl_t::pMouseIn  ctrl_clear_mouse_in: mov ecx, dword ptr [esi+46h]");
OC_X86_LAYOUT(offsetof(ctrl_t, pfnDlgProc)  == 0x4A, "ctrl_t::pfnDlgProc dlg_activate: mov dword ptr [esi+4Ah], 0h");
OC_X86_LAYOUT(offsetof(ctrl_t, rcItems)     == 0x46, "ctrl_t::rcItems   dlg_load_ctrl: add word ptr [esi+46h], ax (wType == 0Eh)");

// Hyperion: moved here from src117/lang/dlg_dlg.cpp for PortDialogNative
// (src117/port/data/port_formats.cpp).
// The wType == 0x0E item list, the FLC control's animation elements, which
// src117/h/dlg_flc.h lays out whole as flc_t. Two fields, both from dlg_load_ctrl:
//   +0x00 next     `mov ecx, dword ptr [ecx]` / `cmp dword ptr [ecx], 0h`
//   +0x0A a relocated pointer   `mov eax, dword ptr [edx+0Ah]` / `add eax, edi`
// Nothing else in the corpus reaches this object through this path.
#pragma pack(push, 1)
struct dlgitem_t {
  /*0x00*/ dlgitem_t *pNext;
  /*0x04*/ dlg_byte_t _unmapped04[6];
  /*0x0A*/ void *pText;
};
#pragma pack(pop)
static_assert(offsetof(dlgitem_t, pNext) == 0x00, "dlgitem_t::pNext  dlg_load_ctrl: cmp dword ptr [ecx], 0h");
OC_X86_LAYOUT(offsetof(dlgitem_t, pText) == 0x0A, "dlgitem_t::pText  dlg_load_ctrl: mov eax, dword ptr [edx+0Ah]");
// 0x0E is a derived extent, not an allocation size, the same caveat ctrl_t
// carries above: +0x0A is the highest offset any oracle instruction reaches on
// this object and it is a dword, and nothing indexes or allocates one. Safe for
// field access through the loaded blob, not safe to allocate.
OC_X86_LAYOUT(sizeof(dlgitem_t) == 0x0E, "dlgitem_t size    DERIVED EXTENT -- see caveat, not an allocation size");
