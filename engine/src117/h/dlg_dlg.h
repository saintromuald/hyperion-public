// dlg_dlg.h -- the interface of src117/lang/dlg_dlg.cpp: the dialog manager --
// the live dialog list, the hook slots, the timed hooks, the loader and
// relocator, the modal pump and the theme-metric publisher
//
// The layout-bearing structs (dlgtimedhook_t, imglayer_t, dlgitem_t,
// dlgtheme_t) stay in the .cpp beside the function that derives each, so only a
// forward declaration is needed here. Include after <stddef.h>.
#pragma once

#include "platform.h"  // OC_X86_LAYOUT
#include "dialog.h"
#include "TSList.h"

// A loaded dialog theme; the oracle's list type names it _DLGGRP.
struct _DLGGRP;
typedef _DLGGRP dlgtheme_t;

// The theme list, with the one member the game's own code reaches.
#pragma pack(push, 1)
template <> struct TSList<_DLGGRP,TSGetLink<_DLGGRP> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<_DLGGRP> link;
  void Clear(void);
};
#pragma pack(pop)
typedef TSList<_DLGGRP,TSGetLink<_DLGGRP> > DLGGRPLIST;
OC_X86_LAYOUT(sizeof(DLGGRPLIST) == 12, "DLGGRPLIST size");
static_assert(offsetof(DLGGRPLIST, link) == 0x04, "DLGGRPLIST::link");

// The button-click sound hook parked in sgpBtnClickSndFn @0x6CE664.
// snd_play_btn pushes one dword and never cleans up, so the callee pops it:
// __stdcall with one parameter, whose result is discarded.
typedef void(__stdcall *PFN_BTNCLICKSND)(dlg_dword_t dwSound);

// The optional per-modal-entry callback in gpfnModalExtraFn_g @0x6CE6E4.
// dlg_run_modal calls it with the dialog in ECX and nothing pushed.
typedef void(__fastcall *PFN_MODALEXTRA)(ctrl_t *pDlg);

// dlg_load's two caller-supplied callbacks. PFN_DLGMEASURE's arity comes from
// dlg_load_ctrl, the only site that calls it -- two register arguments and
// three stack dwords -- since dlg_load itself only forwards the pointer.
typedef ctrl_t *(__fastcall *PFN_DLGLOADER)(void *pCtx, dlg_dword_t dwZero,
                                            dlg_dword_t dwZero2, void *pA,
                                            dlg_dword_t dwB);
typedef void(__fastcall *PFN_DLGMEASURE)(void *pData, void *pOut,
                                         dlg_dword_t dwZero, void *pA,
                                         dlg_dword_t dwB);

// The names below live in other TUs; each call site fixes the signature.

// app_add_exit -- oracle 0x4CE940, declared by its own TU's header.
#include "gds_appaddex.h"

// dlg_activate -- oracle 0x4C2A90. dlg_run_modal calls it with ECX = the
// dialog and one stack dword; the `xor edx, edx` before the push is a second
// register argument the body never reads, the shape dlg_run_modal has too.
void __fastcall dlg_activate(ctrl_t *pDlg, dlg_dword_t dwUnused, PFN_CTRL_EVT pfnEvt);

// font_set -- oracle 0x4CE160 -- is declared in src117/h/dlg_draw.h, beside
// font_set_color_tbl in the same source file, not here. dlg_set_font tail-jumps
// it with ECX = the font handle and nothing else, so it takes one opaque
// pointer; a TU that needs it includes dlg_draw.h.

// img_update -- oracle 0x4C5360. No arguments.
void __fastcall img_update(void);

// list_add_node_head / list_remove_node -- oracle 0x4CAB80 / 0x4CAC30.
// ECX = &head, EDX = node: list_remove_node dereferences ECX, so the first
// argument is the head pointer's address, not the head. It returns the node it
// unlinked, or 0.
ctrl_t *__fastcall list_add_node_head(ctrl_t **ppHead, ctrl_t *pNode);
ctrl_t *__fastcall list_remove_node(ctrl_t **ppHead, ctrl_t *pNode);

// The predicate the two walking list helpers call: one fastcall argument and an
// int result, tested for truth in list_find_node and compared signed in
// list_insert_node -- a match flag in the one, a sort key in the other.
typedef int(__fastcall *PFN_NODE_TEST)(ctrl_t *pNode);

// list_find_node / list_has_node / list_insert_node -- oracle 0x4CAB90 /
// 0x4CABC0 / 0x4CABE0. All three walk `pNext` at +0x00. list_find_node takes
// the head and returns the first node the predicate accepts, or 0;
// list_has_node walks from ECX and returns 1 on identity with EDX;
// list_insert_node takes `&head` and keeps the list ordered by the key.
ctrl_t *__fastcall list_find_node(ctrl_t *pHead, PFN_NODE_TEST pfn);
BOOL __fastcall list_has_node(ctrl_t *pHead, ctrl_t *pFind);
void __fastcall list_insert_node(ctrl_t **ppHead, ctrl_t *pNode, PFN_NODE_TEST pfn);

// RunMsgDispatcher -- oracle 0x440050. No arguments.
void __fastcall RunMsgDispatcher(void);

// dlg_mgr_free -- oracle 0x4C34E0, the exit handler dlg_mgr_init registers.
void __fastcall dlg_mgr_free(void);

// dlg_update_fcn -- oracle 0x4C4D90, the per-frame draw callback dlg_mgr_init
// parks in _img_list[2].pfnUpdate. img_update_real calls it with ECX and EDX
// zeroed and two stack words, the layer's bitmap and a rectangle, and it reads
// none of the four; typed as img.h's PFN_IMG_UPDATE.
struct bmp_t;
struct bmpsrc_t;
void __fastcall dlg_update_fcn(unsigned short, unsigned short, bmp_t *pBmp, bmpsrc_t *pSrc);

// The CRT's non-locale lowercase primitive -- dlg_load_ctrl emits
// `call tolower` with a hand-rolled `push` and `add esp, 4h`, so __cdecl with
// one int parameter. Declared here rather than by including <ctype.h>, with the
// same #undef guard src117/h/dlg_ctrl.h explains for `_toupper`.
#if defined(_MSC_VER)
#ifdef tolower
#undef tolower
#endif
extern "C" {
int __cdecl tolower(int c);
}
#else
#include <ctype.h>  // Hyperion: the C library's own declaration
#endif

// The TU's own functions; each body's address and size are in its .cpp banner.
int __fastcall dlg_contains(ctrl_t *pDlg);
void __fastcall dlg_deactivate(ctrl_t *pDlg);
int __fastcall dlg_event(dlgevt_t *pEvent);
void __fastcall dlg_focus_free(void);
void __fastcall dlg_stats_init(dlgtheme_t *pTheme);
// 0x4C2C70. Frees a theme's group file; both callers discard the EAX it
// inherits from SMemFree.
void __fastcall dlg_grp_free(dlgtheme_t *pTheme);
void __fastcall dlg_hook(ctrl_t *pCtrl, dlg_dword_t nIndex);
ctrl_t *__fastcall dlg_load(void *pCtx, PFN_DLGLOADER pfnLoad, PFN_DLGMEASURE pfnMeasure, void *pA, dlg_dword_t dwB);
#if !defined(_MSC_VER)
// clang and gcc: a string literal or a function as the void * (h/compiler.h, OcVoidPtr)
template <typename T, typename U>
inline ctrl_t *dlg_load(T *pCtx, PFN_DLGLOADER pfnLoad, PFN_DLGMEASURE pfnMeasure, U *pA, dlg_dword_t dwB)
{
  return dlg_load(OcVoidPtr(pCtx), pfnLoad, pfnMeasure, OcVoidPtr(pA), dwB);
}
#endif
void __fastcall dlg_load_ctrl(ctrl_t *pNode, char *pBase, PFN_DLGMEASURE pfnMeasure, void *pA, dlg_dword_t dwB);
void __fastcall dlg_mgr_init(void);
void __fastcall dlg_register_timeout(ctrl_t *pCtrl);
int __fastcall dlg_run_modal(ctrl_t *pDlg, dlg_dword_t dwUnused, PFN_CTRL_EVT pfnEvt);
void __fastcall dlg_set_font(ctrl_t *pCtrl);
void __fastcall sub_4C36B0(void);
void __fastcall snd_play_btn(dlg_dword_t dwSound);
void __fastcall snd_set_btn_click(PFN_BTNCLICKSND pfn);
void __fastcall set_digital_volume(LONG nVolume);
void __fastcall dlg_unhook_all(ctrl_t *pCtrl);
