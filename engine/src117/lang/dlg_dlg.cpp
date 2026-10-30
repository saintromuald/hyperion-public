// dlg_dlg.cpp -- the dialog manager: the live dialog list, the 19 hook slots
// and 4 timed hooks, the loader that relocates a dialog blob into pointers, the
// modal pump, and the theme-metric publisher. Four flat arrays and one list do
// all the work:
//   sgCtrlHooks[19]     event-slot owners; dlgevt_t::wTag is the slot index
//   sgTimedHooks_g[4]   {tick deadline, control} -- the 30-tick timeout pool
//   gpDlgList           the live dialogs, walked through ctrl_t::pNext
//   gpDlgRemoveList_g   the deferred-destroy list sub_4C36B0 drains
// Each extent is derived at its declaration. The bodies are not in oracle VA
// order and some cannot be reordered: the oracle inlines dlg_deactivate into
// sub_4C36B0 and dlg_focus_free into dlg_run_modal, so each stays above its
// caller, as dlg_contains does.
#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>
#include <string.h>

#include "dialog.h"
#include "rect16.h"
#include "dlg_ctrl.h"      // ctrl_send_msg / ctrl_set_update -- ported
#include "dlg_dlg.h"
#include "dlg_draw.h"   // font_set -- its only seat
#include "gluRdy.h"
#include "minimap.h"
#include "Storm.h"
#include "TSList.h"
#include "rtti.h"
#include "dlg_dlgdraw.h"
#include "gds_grp2bmpx.h"
#include "../port/data/port_formats.h"  // Hyperion: PortDialogNative
#include "../port/render/port_view.h"  // Hyperion: g_portView, PortViewDialogShift
#include "../port/render/port_console.h"  // Hyperion: PortConsoleHudHeld
#include "../h/img.h"  // Hyperion: _img_list

// GetTickCount, imported from KERNEL32: dlg_event and dlg_register_timeout both
// emit `call dword ptr [0x4EB2FC]`, and that IAT slot is the one the oracle's
// import directory binds to KERNEL32.dll!GetTickCount. Declared here rather
// than by including <windows.h>, to keep this TU header-light.
extern "C" __declspec(dllimport) ULONG __stdcall GetTickCount(void);

// The globals, named as the data sheet names them (dlg/dlg.cpp unless noted).

// sgbDlgDeactivating_g @0x6CE660 -- set to 1 by every deactivation path. Four
// bytes despite the `sgb` prefix: every write in the oracle is a 32-bit store,
// so `sgb` reads as "boolean", not "byte".
#ifdef OC_DLL_MODE
#define sgbDlgDeactivating_g (*reinterpret_cast<dlg_dword_t*>(OC_VA(0x6CE660u)))
#else
extern dlg_dword_t sgbDlgDeactivating_g;
#endif

// sgpBtnClickSndFn @0x6CE664 -- the button-click sound hook. Signature
// derivation at PFN_BTNCLICKSND in src117/h/dlg_dlg.h.
#ifdef OC_DLL_MODE
#define sgpBtnClickSndFn (*reinterpret_cast<PFN_BTNCLICKSND*>(OC_VA(0x6CE664u)))
#else
extern PFN_BTNCLICKSND sgpBtnClickSndFn;
#endif

// sgCtrlHooks @0x6CE668 -- nineteen entries, proven twice: sgTimedHooks_g, the
// next name in the sheet, is 0x4C = 19 * 4 bytes on, and dlg_unhook_all opens
// with a 19-iteration counter over this base. The element is a ctrl_t*, since
// dlg_hook and dlg_focus_free hand an entry straight to ctrl_send_msg in ECX
// and dlg_event calls through [entry+2Ah].
#ifdef OC_DLL_MODE
#define sgCtrlHooks (reinterpret_cast<ctrl_t**>(OC_VA(0x6CE668u)))
#else
extern ctrl_t *sgCtrlHooks[19];
#endif

// sgTimedHooks_g @0x6CE6B4 -- four elements of eight bytes, not four globals.
// dlg_unhook_all touches 0x6CE6B8 / 0x6CE6C0 / 0x6CE6C8 / 0x6CE6D0, a stride-8
// progression ending exactly at the next named global, and dlg_register_timeout
// settles the split with one scaled index and two displacements four apart:
// { dword at +0; pointer at +4 }. The dword is a tick deadline --
// dlg_register_timeout stores GetTickCount() + 0x1E and dlg_event fires the
// entry when (deadline - now) goes negative as a signed 32-bit difference, the
// standard wrapping tick comparison.
struct dlgtimedhook_t {
  /*0x00*/ LONG nDeadline;
  /*0x04*/ ctrl_t *pCtrl;
};
OC_X86_LAYOUT(sizeof(dlgtimedhook_t) == 0x08, "dlgtimedhook_t size   dlg_register_timeout: [esi*8+...] stride 8");
static_assert(offsetof(dlgtimedhook_t, nDeadline) == 0x00, "dlgtimedhook_t::nDeadline  dlg_register_timeout: mov [esi*8+6CE6B4h], eax");
OC_X86_LAYOUT(offsetof(dlgtimedhook_t, pCtrl) == 0x04, "dlgtimedhook_t::pCtrl      dlg_register_timeout: mov [esi*8+6CE6B8h], ecx");

#ifdef OC_DLL_MODE
#define sgTimedHooks_g (reinterpret_cast<dlgtimedhook_t*>(OC_VA(0x6CE6B4u)))
#else
extern dlgtimedhook_t sgTimedHooks_g[4] OC_UNALIGNED;
#endif

// gpDlgRemoveList_g @0x6CE6D4 -- head of the deferred-destroy list. The call
// takes the address of the head pointer, which list_remove_node dereferences,
// so the global itself is one ctrl_t*.
#ifdef OC_DLL_MODE
#define gpDlgRemoveList_g (*reinterpret_cast<ctrl_t**>(OC_VA(0x6CE6D4u)))
#else
extern ctrl_t *gpDlgRemoveList_g;
#endif

// gpDlgList @0x6CE6D8 -- head of the live dialog list, walked through +0x00 by
// dlg_contains, dlg_event and dlg_run_modal.
#ifdef OC_DLL_MODE
#define gpDlgList (*reinterpret_cast<ctrl_t**>(OC_VA(0x6CE6D8u)))
#else
extern ctrl_t *gpDlgList;
#endif

// gnDlgReturnVal @0x6CE6DC -- dlg_run_modal seeds it with -3 and returns it, so
// it is signed.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG*>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

// gpfnModalExtraFn_g @0x6CE6E4 -- an optional per-modal-entry callback.
// Signature derivation at PFN_MODALEXTRA in src117/h/dlg_dlg.h.
#ifdef OC_DLL_MODE
#define gpfnModalExtraFn_g (*reinterpret_cast<PFN_MODALEXTRA*>(OC_VA(0x6CE6E4u)))
#else
extern PFN_MODALEXTRA gpfnModalExtraFn_g;
#endif

// sgnDigitalVolume @0x6CE6E8 -- set_digital_volume's only statement.
// DWORD from `mov dword ptr [0x6CE6E8], ecx`; `sgn` = signed int.
#ifdef OC_DLL_MODE
#define sgnDigitalVolume (*reinterpret_cast<LONG*>(OC_VA(0x6CE6E8u)))
#else
extern LONG sgnDigitalVolume;
#endif

// sgpFonts_g @0x6D3024 (gds/font.cpp) -- four font handles, not four globals.
// dlg_set_font reads 0x6D3024 through 0x6D3030 and the next name in the same TU
// is gbFontLineExtra @0x6D3034, so the object is 0x10 bytes. Every read feeds
// font_set's ECX, so the elements are opaque font pointers.
#ifdef OC_DLL_MODE
#define sgpFonts_g (reinterpret_cast<void**>(OC_VA(0x6D3024u)))
#else
extern void *sgpFonts_g[4];
#endif

// _img_list @0x6D0B90 (gds/image.cpp) -- eight screen layers of twenty bytes, of
// which dlg_mgr_init writes element [2]. The extent comes from img_init's
// `memset(0x6D0B90, 0, 0xA0)` and from the next name, sgCursorSaveRect
// @0x6D0C30; the stride from img_invalid's 20*i indexing. The fields, all read
// in img_invalid's one basic block:
//   +0x00 BYTE         in use
//   +0x01 BYTE         flags, bit 0 dirty
//   +0x02 signed WORD  left      +0x04 signed WORD  top
//   +0x06 signed WORD  width     +0x08 signed WORD  height
//   +0x0C pointer      a bmp, built two instructions earlier by bmp_create
//   +0x10 pointer      a code address; dlg_mgr_init stores dlg_update_fcn
// right and bottom are computed there as left + width - 1 and top + height - 1,
// which is what fixes width and height rather than right and bottom. +0x0A is
// not derived: nothing in the corpus touches those two bytes. The element is
// src117/h/img.h's img_t.


// snd_set_btn_click -- installs the button-click sound hook.
void __fastcall snd_set_btn_click(PFN_BTNCLICKSND pfn) {
  sgpBtnClickSndFn = pfn;
}

// set_digital_volume -- stores the digital volume, and nothing else.
void __fastcall set_digital_volume(LONG nVolume) {
  sgnDigitalVolume = nVolume;
}

// snd_play_btn -- plays a button sound through the hook, if one is installed.
void __fastcall snd_play_btn(dlg_dword_t dwSound) {
  PFN_BTNCLICKSND pfn = sgpBtnClickSndFn;
  if (pfn)
    pfn(dwSound);
}

// dlg_contains -- the name and the behaviour pull apart: it returns 1 when the
// dialog is not in gpDlgList. The tail is `xor eax, eax / test edx, edx /
// setz al`, true on a NULL walk pointer, and dlg_run_modal's loop spins while
// this returns 0. Both the name and the sense are the oracle's.
int __fastcall dlg_contains(ctrl_t *pDlg) {
  ctrl_t *p = gpDlgList;
  while (p) {
    if (p == pDlg)
      break;
    p = p->pNext;
  }
  return p == 0;
}

// dlg_hook -- installs pCtrl as hook slot nIndex, telling the displaced
// occupant (event 6) that it is losing the slot.
void __fastcall dlg_hook(ctrl_t *pCtrl, dlg_dword_t nIndex) {
  ctrl_t *pOld = sgCtrlHooks[nIndex];
  if (pOld)
    ctrl_send_msg(pOld, 6, 0);
  sgCtrlHooks[nIndex] = pCtrl;
}

// dlg_focus_free -- clears all 19 hook slots, sending event 6 to each occupant.
void __fastcall dlg_focus_free(void) {
  int i;
  for (i = 0; i < 19; i++) {
    ctrl_t *p = sgCtrlHooks[i];
    if (p) {
      ctrl_send_msg(p, 6, 0);
      sgCtrlHooks[i] = 0;
    }
  }
}

// dlg_mgr_init -- registers the shutdown hook and claims screen layer 2 for the
// dialog system: full 640x480, no bitmap of its own, drawn by dlg_update_fcn.
void __fastcall dlg_mgr_init(void) {
  app_add_exit(dlg_mgr_free);
  // Hyperion: the dialogs' layer covers the view's screen (port_view.cpp).
  _img_list[2].nWidth = static_cast<short>(g_portView.nWidth);
  _img_list[2].nHeight = static_cast<short>(g_portView.nHeight);
  _img_list[2].nX = 0;
  _img_list[2].nY = 0;
  _img_list[2].pBmp = 0;
  _img_list[2].pfnUpdate = dlg_update_fcn;
  _img_list[2].bInUse = 1;
  _img_list[2].bFlags = 0x20;
}

// dlg_load -- runs the caller's loader to pull a dialog blob into memory, marks
// it, then relocates every pointer inside it. ECX is never touched in the body:
// it is passed straight through to the loader in ECX, so it has to be a declared
// parameter rather than a local.
ctrl_t *__fastcall dlg_load(void *pCtx, PFN_DLGLOADER pfnLoad,
                            PFN_DLGMEASURE pfnMeasure, void *pA, dlg_dword_t dwB) {
  ctrl_t *pDlg = pfnLoad(pCtx, 0, 0, pA, dwB);
  // An early return, not `if (pDlg) { ... }`: the oracle's `jnz $+0x5` hops over
  // an inline failure epilogue, where the guarded-block form jumps forward to one
  // shared epilogue and comes out five bytes short.
  if (!pDlg)
    return pDlg;
  // Hyperion: the file's 32-bit records as the engine's ctrl_t, in a new block
  // still linked by offsets, which dlg_load_ctrl relocates as before
  // (PortDialogNative).
  pDlg = PortDialogNative(pDlg);
  pDlg->dwFlags |= 4;
  dlg_load_ctrl(pDlg, (char *)pDlg, pfnMeasure, pA, dwB);
  return pDlg;
}

// dlg_set_font -- selects one of the four fonts from the three text-size bits
// (0x4C00) in the node's flag word and tail-jumps to font_set.
void __fastcall dlg_set_font(ctrl_t *pCtrl) {
  // A switch, not an if/else-if chain: the chain form lays the arm bodies out in
  // source order, where the oracle lays them out default, 0x4000, 0x800, 0x400 --
  // reverse order after a fallthrough default, MSVC's sparse-switch shape.
  switch (pCtrl->dwFlags & 0x4C00) {
  case 0x400:
    font_set(sgpFonts_g[0]);
    break;
  case 0x800:
    font_set(sgpFonts_g[2]);
    break;
  case 0x4000:
    font_set(sgpFonts_g[3]);
    break;
  default:
    font_set(sgpFonts_g[1]);
    break;
  }
}

// dlg_register_timeout -- arms a 30-tick timeout for pCtrl's owning dialog,
// unless one is already armed for that dialog or all four slots are taken.
void __fastcall dlg_register_timeout(ctrl_t *pCtrl) {
  int i;
  for (i = 0; i < 4; i++) {
    ctrl_t *p = sgTimedHooks_g[i].pCtrl;
    if (p && p->pParent == pCtrl->pParent)
      return;
  }
  for (i = 0; i < 4; i++) {
    if (sgTimedHooks_g[i].pCtrl == 0) {
      sgTimedHooks_g[i].pCtrl = pCtrl;
      sgTimedHooks_g[i].nDeadline = (LONG)GetTickCount() + 30;
      return;
    }
  }
}

// dlg_unhook_all -- forgets pCtrl everywhere, in all 19 hook slots and all 4
// timed slots. The four timed slots are fully unrolled in the oracle and each
// uses `cmovz eax, edx` with EDX the 0 left over from the 19-slot loop's own
// down-counter.
void __fastcall dlg_unhook_all(ctrl_t *pCtrl) {
  int i;
  ctrl_t **pp = sgCtrlHooks;
  // A down-counter, not `for (i = 0; i < 19; i++)`: the ascending form strength-
  // reduces the index away, where the oracle keeps a live `mov edx, 13h / dec
  // edx / jnz` counter, and the four cmovs below reuse that register's terminal
  // 0 instead of materialising their own.
  for (i = 19; i != 0; i--, pp++) {
    if (*pp == pCtrl)
      *pp = 0;
  }
  for (i = 0; i < 4; i++) {
    if (sgTimedHooks_g[i].pCtrl == pCtrl)
      sgTimedHooks_g[i].pCtrl = 0;
  }
}

// dlg_deactivate -- defined before sub_4C36B0, because the oracle inlines the
// whole body there.
void __fastcall dlg_deactivate(ctrl_t *pDlg) {
  if (pDlg->dwFlags & 0x4000000) {
    if (list_remove_node(&gpDlgList, pDlg))
      list_add_node_head(&gpDlgRemoveList_g, pDlg);
  } else {
    ctrl_set_update(pDlg);
    list_remove_node(&gpDlgList, pDlg);
    ctrl_send_msg(pDlg, 1, 0);
  }
  sgbDlgDeactivating_g = 1;
}

// sub_4C36B0 -- drains the deferred-destroy list. The second
// `test [esi+18h], 4000000h` is not redundant source: the first test proves the
// flag clear, but the intervening list_remove_node call can alias the node, so
// MSVC has to re-read it inside the inlined dlg_deactivate.
void __fastcall sub_4C36B0(void) {
  ctrl_t *p = gpDlgRemoveList_g;
  while (p) {
    ctrl_t *pNext = p->pNext;
    if (!(p->dwFlags & 0x4000000)) {
      list_remove_node(&gpDlgRemoveList_g, p);
      dlg_deactivate(p);
    }
    p = pNext;
  }
}

// dlg_event -- the dialog manager's event pump: drain the destroy list, offer
// the event to the hook that owns this event's slot, then to every live dialog,
// then expire any timed hooks whose deadline has passed.
int __fastcall dlg_event(dlgevt_t *pEvent) {
  int nRet = 0;
  int i;
  LONG nNow;
  ctrl_t *pHook;
  ctrl_t *p;

  sub_4C36B0();
  pHook = sgCtrlHooks[pEvent->wTag];
  if (pHook)
    nRet = pHook->pfnEvt(pHook, pEvent);
  if (!nRet) {
    for (p = gpDlgList; p; ) {
      ctrl_t *pNext = p->pNext;
      // Hyperion: a dialog the HUD's slots show takes no mouse, its moves and buttons
      // (tags 3 to 12); the HUD's slots take it (port_console.cpp)
      if (pEvent->wTag >= 3 && pEvent->wTag <= 12 && PortConsoleHudHeld(p)) {
        p = pNext;
        continue;
      }
      nRet = p->pfnEvt(p, pEvent);
      if (nRet)
        break;
      p = pNext;
    }
  }
  nNow = (LONG)GetTickCount();
  for (i = 0; i < 4; i++) {
    ctrl_t *pTimed = sgTimedHooks_g[i].pCtrl;
    if (pTimed && sgTimedHooks_g[i].nDeadline - nNow < 0) {
      sgTimedHooks_g[i].pCtrl = 0;
      ctrl_send_msg(pTimed, 3, 0);
    }
  }
  return nRet;
}

// dlg_run_modal -- opens pDlg modally and pumps until it leaves gpDlgList, then
// returns the code the dialog parked in gnDlgReturnVal. dlg_focus_free is
// inlined, so it stays above. EDX is unread and pfnEvt is the stack argument
// (`ret 4`). The modal extra call is inside the loop; outside it, MSVC pads two
// loop heads.
int __fastcall dlg_run_modal(ctrl_t *pDlg, dlg_dword_t dwUnused, PFN_CTRL_EVT pfnEvt) {
  dlg_focus_free();
  pDlg->dwFlags |= 0x80000000;
  gnDlgReturnVal = -3;
  dlg_activate(pDlg, 0, pfnEvt);
  do {
    ctrl_t *p;
    if (gpfnModalExtraFn_g)
      gpfnModalExtraFn_g(pDlg);
    for (p = gpDlgList; p; ) {
      PFN_MODALEXTRA pfn = (PFN_MODALEXTRA)p->pfnDlgProc;
      ctrl_t *pNext = p->pNext;
      if (pfn)
        pfn(p);
      p = pNext;
    }
    RunMsgDispatcher();
    img_update();
  } while (!dlg_contains(pDlg));
  return gnDlgReturnVal;
}


// The wType == 0x0E item list, dlgitem_t, is in src117/h/dialog.h, where the
// dialog loader needs it too.

// gCtrlEvtFcnTbl @0x4F10F8 (.rdata, dlg/ctrl.cpp) -- twenty entries, bounded
// exactly: the next name in the same segment is gCtrlDrawFcnTbl @0x4F1148, so
// the table is 0x50 bytes. Indexed by ctrl_t::wType.
#ifdef OC_DLL_MODE
#define gCtrlEvtFcnTbl (reinterpret_cast<PFN_CTRL_EVT*>(OC_VA(0x4F10F8u)))
#else
extern PFN_CTRL_EVT gCtrlEvtFcnTbl[20];
#endif

// gCtrlDrawFcnTbl @0x4F1148 (.rdata, dlg/draw.cpp) -- the sibling table, fifteen
// entries. The next name is sgwPart2ScrollFlagTbl @0x4F11D0, 0x88 bytes on, but
// slot [15] is the string "draw:1" and the gap holds other TUs' strings, as
// minimap.cpp and dlg_scrlbar.cpp declare it. The extent matters because the
// table's layout changes at 64 bits.
#ifdef OC_DLL_MODE
#define gCtrlDrawFcnTbl (reinterpret_cast<PFN_CTRL_DRAW*>(OC_VA(0x4F1148u)))
#else
extern PFN_CTRL_DRAW gCtrlDrawFcnTbl[15];
#endif

// dlg_load_ctrl -- walks a freshly loaded dialog blob and turns every
// file-relative offset in it into a real pointer, installs the per-kind handler
// pair, and resolves the negative-index resource strings. pBase is also the
// dialog: dlg_load calls it with the same pointer in ECX and EDX and the walk
// never updates the parent register, so every control gets the root node, where
// the blob begins, as its parent. It is a flat walk, not a recursion: descend
// once through +0x42 when the node is a dialog (wType == 0), otherwise follow
// +0x00, and stop on NULL.
void __fastcall dlg_load_ctrl(ctrl_t *pNode, char *pBase, PFN_DLGMEASURE pfnMeasure,
                              void *pA, dlg_dword_t dwB) {
  while (pNode) {
    dlg_word_t wType;
    if (pNode->pNext)
      pNode->pNext = (ctrl_t *)((ULONG_PTR)pNode->pNext + (ULONG_PTR)pBase);
    if (pNode->pData) {
      char *pStr = (char *)pNode->pData + (ULONG_PTR)pBase;
      // Flag 0x200 means do lowercase, not "do not": the oracle's branch is
      // `test dword ptr [esi+18h], 200h ... jz`, and `jz` fires when the bit is
      // clear, so it is the clear case that skips. dialog.h's bit table was
      // corrected to match, and the hotkey family confirms the sense.
      if (pNode->dwFlags & 0x200)
        *pStr = (char)tolower(*pStr);
      pNode->pData = pStr;
    }
    if (pNode->wType == 0x0E) {
      dlgitem_t **pp = (dlgitem_t **)&pNode->pChildren;
      if (*pp) {
        do {
          dlgitem_t *pItem;
          *pp = (dlgitem_t *)((ULONG_PTR)*pp + (ULONG_PTR)pBase);
          pItem = *pp;
          if (pItem->pText)
            pItem->pText = (char *)pItem->pText + (ULONG_PTR)pBase;
          pp = (dlgitem_t **)*pp;
        } while (*pp);
      }
      pNode->rcItems.left = (short)(pNode->rcItems.left + pNode->rc.left);
      pNode->rcItems.right = (short)(pNode->rcItems.right + pNode->rc.left);
      pNode->rcItems.top = (short)(pNode->rcItems.top + pNode->rc.top);
      pNode->rcItems.bottom = (short)(pNode->rcItems.bottom + pNode->rc.top);
    }
    wType = pNode->wType;
    pNode->pfnEvt = gCtrlEvtFcnTbl[wType];
    pNode->pfnDraw = gCtrlDrawFcnTbl[wType];
    if (wType == 5 && pNode->pData)
      pfnMeasure(pNode->pData, pNode->_unmapped0C, 0, pA, dwB);
    if (pNode->nResIndex < 0) {
      pNode->nResIndex = *(LONG *)(pBase + pNode->nResIndex + 0x10);
      pNode->dwFlags |= 0x20;
    }
    if (pNode->wType == 0) {
      if (pNode->pChildren)
        pNode->pChildren = (ctrl_t *)((ULONG_PTR)pNode->pChildren + (ULONG_PTR)pBase);
      pNode = pNode->pChildren;
    } else {
      pNode->pParent = (ctrl_t *)pBase;
      pNode = pNode->pNext;
    }
  }
}

// dlg_stats_init -- publishes one loaded dialog theme's control metrics into the
// 31 loose globals the drawing code reads. A flat copy, with no logic beyond the
// guard. The parameter's size is derived, not padded to fit: dlg_load_theme
// allocates it with `push 14Ch / call SMemAlloc` and touches its last member as
// a WORD at +0x14A, so the object ends at exactly 0x14C. Everything below
// +0x10C is untouched here and left unmapped rather than invented.
struct _DLGGRP {
  /*0x000*/ TSLink<_DLGGRP> link;
  /*0x008*/ char szName[0x104];
  /*0x10C*/ void *pGroup;
  /*0x110*/ dlg_word_t wRadioBtnSize;
  /*0x112*/ dlg_word_t wCheckBoxSize;
  /*0x114*/ dlg_word_t wHArrowWid;
  /*0x116*/ dlg_word_t wHArrowHgt;
  /*0x118*/ dlg_word_t wVArrowWid;
  /*0x11A*/ dlg_word_t wVArrowHgt;
  /*0x11C*/ dlg_word_t wUnk6;
  /*0x11E*/ dlg_word_t wUnk7;
  /*0x120*/ dlg_word_t wThumbWid;
  /*0x122*/ dlg_word_t wThumbHgt;
  /*0x124*/ dlg_word_t wUnk10;
  /*0x126*/ dlg_word_t wUnk11;
  /*0x128*/ dlg_word_t wCombo1OpenWid;
  /*0x12A*/ dlg_word_t wUnk13;
  /*0x12C*/ dlg_word_t wUnk14;
  /*0x12E*/ dlg_word_t wUnk15;
  /*0x130*/ dlg_word_t wCombo1ArrowWid;
  /*0x132*/ dlg_word_t wUnk17;
  /*0x134*/ dlg_word_t wUnk18;
  /*0x136*/ dlg_word_t wCombo2OpenWid;
  /*0x138*/ dlg_word_t wUnk20;
  /*0x13A*/ dlg_word_t wUnk21;
  /*0x13C*/ dlg_word_t wUnk22;
  /*0x13E*/ dlg_word_t wCombo2ArrowWid;
  /*0x140*/ dlg_word_t wUnk24;
  /*0x142*/ dlg_word_t wSliderSlotOnWid;
  /*0x144*/ dlg_word_t wUnk26;
  /*0x146*/ dlg_word_t wSliderSlotWid;
  /*0x148*/ dlg_word_t wUnk28;
  /*0x14A*/ dlg_word_t wUnk29;
};
OC_X86_LAYOUT(sizeof(dlgtheme_t) == 0x14C, "dlgtheme_t size   dlg_load_theme: push 14Ch / call SMemAlloc");
static_assert(offsetof(dlgtheme_t, link) == 0x000, "dlgtheme_t::link   Clear unlinks [esi] / [esi+4h] before SMemFree");
OC_X86_LAYOUT(offsetof(dlgtheme_t, szName) == 0x008, "dlgtheme_t::szName dlg_load_theme: lea ecx, [edi+8h] then a strcpy loop");
// Every metric offset is asserted individually because the whole function is the
// offset list: one wrong displacement publishes the wrong metric and still
// compiles. Each is read once, as
//   movzx eax, word ptr [ecx+NNNh] / mov word ptr [<global>], ax.
OC_X86_LAYOUT(offsetof(dlgtheme_t, pGroup) == 0x10C, "dlgtheme_t::pGroup   dlg_stats_init: [ecx+10Ch]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wRadioBtnSize) == 0x110, "dlgtheme_t::wRadioBtnSize   dlg_stats_init: [ecx+110h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wCheckBoxSize) == 0x112, "dlgtheme_t::wCheckBoxSize   dlg_stats_init: [ecx+112h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wHArrowWid) == 0x114, "dlgtheme_t::wHArrowWid   dlg_stats_init: [ecx+114h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wHArrowHgt) == 0x116, "dlgtheme_t::wHArrowHgt   dlg_stats_init: [ecx+116h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wVArrowWid) == 0x118, "dlgtheme_t::wVArrowWid   dlg_stats_init: [ecx+118h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wVArrowHgt) == 0x11A, "dlgtheme_t::wVArrowHgt   dlg_stats_init: [ecx+11Ah]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk6) == 0x11C, "dlgtheme_t::wUnk6   dlg_stats_init: [ecx+11Ch]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk7) == 0x11E, "dlgtheme_t::wUnk7   dlg_stats_init: [ecx+11Eh]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wThumbWid) == 0x120, "dlgtheme_t::wThumbWid   dlg_stats_init: [ecx+120h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wThumbHgt) == 0x122, "dlgtheme_t::wThumbHgt   dlg_stats_init: [ecx+122h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk10) == 0x124, "dlgtheme_t::wUnk10   dlg_stats_init: [ecx+124h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk11) == 0x126, "dlgtheme_t::wUnk11   dlg_stats_init: [ecx+126h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wCombo1OpenWid) == 0x128, "dlgtheme_t::wCombo1OpenWid   dlg_stats_init: [ecx+128h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk13) == 0x12A, "dlgtheme_t::wUnk13   dlg_stats_init: [ecx+12Ah]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk14) == 0x12C, "dlgtheme_t::wUnk14   dlg_stats_init: [ecx+12Ch]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk15) == 0x12E, "dlgtheme_t::wUnk15   dlg_stats_init: [ecx+12Eh]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wCombo1ArrowWid) == 0x130, "dlgtheme_t::wCombo1ArrowWid   dlg_stats_init: [ecx+130h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk17) == 0x132, "dlgtheme_t::wUnk17   dlg_stats_init: [ecx+132h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk18) == 0x134, "dlgtheme_t::wUnk18   dlg_stats_init: [ecx+134h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wCombo2OpenWid) == 0x136, "dlgtheme_t::wCombo2OpenWid   dlg_stats_init: [ecx+136h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk20) == 0x138, "dlgtheme_t::wUnk20   dlg_stats_init: [ecx+138h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk21) == 0x13A, "dlgtheme_t::wUnk21   dlg_stats_init: [ecx+13Ah]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk22) == 0x13C, "dlgtheme_t::wUnk22   dlg_stats_init: [ecx+13Ch]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wCombo2ArrowWid) == 0x13E, "dlgtheme_t::wCombo2ArrowWid   dlg_stats_init: [ecx+13Eh]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk24) == 0x140, "dlgtheme_t::wUnk24   dlg_stats_init: [ecx+140h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wSliderSlotOnWid) == 0x142, "dlgtheme_t::wSliderSlotOnWid   dlg_stats_init: [ecx+142h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk26) == 0x144, "dlgtheme_t::wUnk26   dlg_stats_init: [ecx+144h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wSliderSlotWid) == 0x146, "dlgtheme_t::wSliderSlotWid   dlg_stats_init: [ecx+146h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk28) == 0x148, "dlgtheme_t::wUnk28   dlg_stats_init: [ecx+148h]");
OC_X86_LAYOUT(offsetof(dlgtheme_t, wUnk29) == 0x14A, "dlgtheme_t::wUnk29   dlg_stats_init: [ecx+14Ah]");

// The 31 destinations. Every one is a WORD sitting in a 4-byte linker slot,
// except gpCurrDlgGrp_g, which is the one dword. The `gwUnkNN_g` spellings are
// the data sheet's own: the code says nothing about what they measure beyond
// "the Nth theme metric".
#ifdef OC_DLL_MODE
#define gpCurrDlgGrp_g (*reinterpret_cast<void**>(OC_VA(0x6CE6E0u)))
#else
extern void *gpCurrDlgGrp_g;
#endif

// dword_6CE764 -- the theme dlg_stats_init compares its argument against. It is
// the only reference to the address in the whole oracle, with no writer anywhere
// in the corpus, so the "already current" early exit can only ever fire for a
// NULL argument and the theme is re-published on every call. Reproduced, not
// repaired.
#ifdef OC_DLL_MODE
#define dword_6CE764 (*reinterpret_cast<dlgtheme_t**>(OC_VA(0x6CE764u)))
#else
extern dlgtheme_t *dword_6CE764;
#endif

// Spelled out rather than macro-generated, one `#define` per global.
#ifdef OC_DLL_MODE
#define gwRadioBtnSize (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE6ECu)))
#define gwCheckBoxSize (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE6F0u)))
#define gwVArrowWid (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE6F4u)))
#define gwVArrowHgt (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE6F8u)))
#define gwHArrowWid (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE6FCu)))
#define gwHArrowHgt (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE700u)))
#define gwUnk6_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE704u)))
#define gwUnk7_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE708u)))
#define gwThumbWid (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE70Cu)))
#define gwThumbHgt (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE710u)))
#define gwUnk10_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE714u)))
#define gwUnk11_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE718u)))
#define gwCombo1OpenWid_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE71Cu)))
#define gwUnk13_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE720u)))
#define gwCombo1ArrowWid (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE724u)))
#define gwUnk14_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE728u)))
#define gwUnk15_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE72Cu)))
#define gwUnk17_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE730u)))
#define gwUnk18_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE734u)))
#define gwCombo2OpenWid_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE738u)))
#define gwUnk20_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE73Cu)))
#define gwCombo2ArrowWid (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE740u)))
#define gwUnk21_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE744u)))
#define gwUnk22_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE748u)))
#define gwUnk24_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE74Cu)))
#define gwSliderSlotOnWid_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE750u)))
#define gwUnk26_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE754u)))
#define gwSliderSlotWid_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE758u)))
#define gwUnk28_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE75Cu)))
#define gwUnk29_g (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE760u)))
#else
extern dlg_word_t gwRadioBtnSize;
extern dlg_word_t gwCheckBoxSize;
extern dlg_word_t gwVArrowWid;
extern dlg_word_t gwVArrowHgt;
extern dlg_word_t gwHArrowWid;
extern dlg_word_t gwHArrowHgt;
extern dlg_word_t gwUnk6_g;
extern dlg_word_t gwUnk7_g;
extern dlg_word_t gwThumbWid;
extern dlg_word_t gwThumbHgt;
extern dlg_word_t gwUnk10_g;
extern dlg_word_t gwUnk11_g;
extern dlg_word_t gwCombo1OpenWid_g;
extern dlg_word_t gwUnk13_g;
extern dlg_word_t gwCombo1ArrowWid;
extern dlg_word_t gwUnk14_g;
extern dlg_word_t gwUnk15_g;
extern dlg_word_t gwUnk17_g;
extern dlg_word_t gwUnk18_g;
extern dlg_word_t gwCombo2OpenWid_g;
extern dlg_word_t gwUnk20_g;
extern dlg_word_t gwCombo2ArrowWid;
extern dlg_word_t gwUnk21_g;
extern dlg_word_t gwUnk22_g;
extern dlg_word_t gwUnk24_g;
extern dlg_word_t gwSliderSlotOnWid_g;
extern dlg_word_t gwUnk26_g;
extern dlg_word_t gwSliderSlotWid_g;
extern dlg_word_t gwUnk28_g;
extern dlg_word_t gwUnk29_g;
#endif

void __fastcall dlg_stats_init(dlgtheme_t *pTheme) {
  if (pTheme == dword_6CE764)
    return;
  gpCurrDlgGrp_g = pTheme->pGroup;
  gwRadioBtnSize = pTheme->wRadioBtnSize;
  gwCheckBoxSize = pTheme->wCheckBoxSize;
  gwHArrowWid = pTheme->wHArrowWid;
  gwHArrowHgt = pTheme->wHArrowHgt;
  gwVArrowWid = pTheme->wVArrowWid;
  gwVArrowHgt = pTheme->wVArrowHgt;
  gwUnk6_g = pTheme->wUnk6;
  gwUnk7_g = pTheme->wUnk7;
  gwThumbWid = pTheme->wThumbWid;
  gwThumbHgt = pTheme->wThumbHgt;
  gwUnk10_g = pTheme->wUnk10;
  gwUnk11_g = pTheme->wUnk11;
  gwCombo1OpenWid_g = pTheme->wCombo1OpenWid;
  gwUnk13_g = pTheme->wUnk13;
  gwUnk14_g = pTheme->wUnk14;
  gwUnk15_g = pTheme->wUnk15;
  gwCombo1ArrowWid = pTheme->wCombo1ArrowWid;
  gwUnk17_g = pTheme->wUnk17;
  gwUnk18_g = pTheme->wUnk18;
  gwCombo2OpenWid_g = pTheme->wCombo2OpenWid;
  gwUnk20_g = pTheme->wUnk20;
  gwUnk21_g = pTheme->wUnk21;
  gwUnk22_g = pTheme->wUnk22;
  gwCombo2ArrowWid = pTheme->wCombo2ArrowWid;
  gwUnk24_g = pTheme->wUnk24;
  gwSliderSlotOnWid_g = pTheme->wSliderSlotOnWid;
  gwUnk26_g = pTheme->wUnk26;
  gwSliderSlotWid_g = pTheme->wSliderSlotWid;
  gwUnk28_g = pTheme->wUnk28;
  gwUnk29_g = pTheme->wUnk29;
}

// dlg_grp_free -- frees a theme's loaded group file.
void __fastcall dlg_grp_free(dlgtheme_t *pTheme) {
  SMemFree(pTheme->pGroup, "dlg.cpp", 0x2CF, 0);
  pTheme->pGroup = 0;
}

// dlg_activate -- brings a loaded dialog up: installs the handler, allocates its
// backing bitmap, converts its rectangle to right/bottom form and raises the
// three activation events, then starts its 100 ms timer.
void __fastcall dlg_activate(ctrl_t *pDlg, dlg_dword_t dwUnused, PFN_CTRL_EVT pfnEvt) {
  // Hyperion: a dialog's place is laid out for 640x480; a wider view puts
  // that layout in its middle, with the console, and the resources and the
  // leaderboard at the screen's edges (port_view.cpp, PortViewDialogAnchor). A
  // dialog that centres itself does so at event 7 below, from the view's width.
  pDlg->rc.left = static_cast<short>(pDlg->rc.left + PortViewDialogShift(pDlg));
  pDlg->dwFlags |= 0x44000000;
  pDlg->pfnDlgProc = 0;
  if (pfnEvt)
    pDlg->pfnEvt = pfnEvt;
  bmp_create(reinterpret_cast<bmp_t *>(&pDlg->_unmapped36[0]), "dlg.cpp", 0x1EF);
  pDlg->nIndex = 0;
  ctrl_send_msg(pDlg, 7, 0);
  pDlg->rc.right += pDlg->rc.left;
  pDlg->rc.bottom += pDlg->rc.top;
  ctrl_send_msg(pDlg, 0x0A, 0);
  ctrl_send_msg(pDlg, 0, 0);
  timer_create(pDlg, 0x0C, 100, 0);
  pDlg->dwFlags &= ~0x04000000;
}


// The RTTI Type Descriptor theme allocations are tagged with.
#ifdef OC_DLL_MODE
#define gTypeInfo__DLGGRP (*reinterpret_cast<type_info *>(OC_VA(0x510A14u)))
#else
extern type_info gTypeInfo__DLGGRP;
#endif

// Every loaded theme.
#ifdef OC_DLL_MODE
#define gDlgGroups_g (*reinterpret_cast<DLGGRPLIST *>(OC_VA(0x50EF38u)))
#else
extern DLGGRPLIST gDlgGroups_g;
#endif

static void UnlinkDlgGrp(_DLGGRP *p) {
  _DLGGRP *pprev = p->link.prev;
  if (pprev != 0) {
    _DLGGRP *pnext = p->link.next;
    TSLink<_DLGGRP> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<_DLGGRP> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0)
        nOff = (intptr_t)&p->link - (intptr_t)pprev->link.next;
      pnl = reinterpret_cast<TSLink<_DLGGRP> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    p->link.prev->link.next = p->link.next;
    p->link.prev = 0;
    p->link.next = 0;
  }
}

// TSList<_DLGGRP>::Clear -- unlinks and frees every theme on the list.
void DLGGRPLIST::Clear(void) {
  for (;;) {
    _DLGGRP *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkDlgGrp(p);
    SMemFree(p, gTypeInfo__DLGGRP.raw_name(), -2, 0);
  }
}

// dlg_mgr_free -- the dialog manager's exit handler: frees every theme's group
// file, then the themes, then the dirty region.
void __fastcall dlg_mgr_free(void) {
  _DLGGRP *p = gDlgGroups_g.link.next;
  if ((intptr_t)p <= 0)
    p = 0;
  while (p) {
    if (p->pGroup) {
      SMemFree(p->pGroup, "dlg.cpp", 0x28C, 0);
      p->pGroup = 0;
    }
    p = p->link.next;
    if ((intptr_t)p <= 0)
      p = 0;
  }
  gDlgGroups_g.Clear();
  dlg_rgn_free();
}

// The loader dlg_load_theme calls in EDX; ReadGroupFile's shape.
typedef GROUP *(__fastcall *PFN_THEMELOADER)(const char *pszFile, intptr_t nFlags,
                                             intptr_t nTag, int nLine);

// sgdwThemesLoaded_g @0x6CE768, counted up once per theme and read nowhere.
#ifndef sgdwThemesLoaded_g
#ifdef OC_DLL_MODE
#define sgdwThemesLoaded_g (*reinterpret_cast<int*>(OC_VA(0x6CE768u)))
#else
extern int sgdwThemesLoaded_g;
#endif
#endif

// Stores one frame dimension unless the destination is null.
static __forceinline void theme_metric(dlg_word_t *pwOut, short nValue) {
  if (pwOut)
    *pwOut = nValue;
}

// TSList::NewNode's placement new: the node's constructor is its link's.
static __forceinline _DLGGRP *InitDlgGrp(_DLGGRP *p) {
  p->link.prev = NULL;
  p->link.next = NULL;
  return p;
}

// Loads a theme's group through the caller's loader, appends a new theme to the
// list, reads its control metrics off twenty frames and publishes them.
intptr_t dlg_load_theme(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, int nArg3) {
  const char *pszName = reinterpret_cast<const char *>(nArg0);
  GROUP *pGroup;
  _DLGGRP *pTheme;
  TSLink<_DLGGRP> *pLink;
  _DLGGRP *pTail;
  dlg_word_t wW, wH;
  bmp_t bmp;
  short nX, nY, nW, nH;

  pGroup = reinterpret_cast<PFN_THEMELOADER>(nArg1)(pszName, 0, nArg2, nArg3);
  void *pv = SMemAlloc(sizeof(_DLGGRP), gTypeInfo__DLGGRP.raw_name(), -2, 8);
  pTheme = pv ? InitDlgGrp(static_cast<_DLGGRP *>(pv)) : NULL;
  pLink = pTheme ? &pTheme->link : &gDlgGroups_g.link;
  if (pLink->prev != NULL) {
    _DLGGRP *pNext = pLink->next;
    TSLink<_DLGGRP> *pSlot;
    if ((intptr_t)pNext <= 0) {
      pSlot = reinterpret_cast<TSLink<_DLGGRP> *>(~(intptr_t)pNext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pNext > 0)
        nOff = (intptr_t)pLink - (intptr_t)pLink->prev->link.next;
      pSlot = reinterpret_cast<TSLink<_DLGGRP> *>((intptr_t)pNext + nOff);
    }
    pSlot->prev = pLink->prev;
    pLink->prev->link.next = pLink->next;
    pLink->prev = NULL;
    pLink->next = NULL;
  }
  pTail = gDlgGroups_g.link.prev;
  ++sgdwThemesLoaded_g;
  pLink->prev = pTail;
  pLink->next = pTail->link.next;
  pTail->link.next = pTheme;
  gDlgGroups_g.link.prev = reinterpret_cast<_DLGGRP *>(pLink);
  strcpy(pTheme->szName, pszName);
  pTheme->pGroup = pGroup;

  grp_to_bmpx(pGroup, 0x0A, &bmp, &nX, &nY, &nW, &nH);
  wW = bmp.nWidth;
  wH = bmp.nHeight;
  pTheme->wRadioBtnSize = wW > wH ? wW : wH;
  grp_to_bmpx(pGroup, 0x0F, &bmp, &nX, &nY, &nW, &nH);
  wW = bmp.nWidth;
  wH = bmp.nHeight;
  pTheme->wCheckBoxSize = wW > wH ? wW : wH;
  grp_to_bmpx(pGroup, 0x16, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wHArrowWid, bmp.nWidth);
  theme_metric(&pTheme->wVArrowWid, bmp.nHeight);
  grp_to_bmpx(pGroup, 0x10, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wHArrowHgt, bmp.nWidth);
  theme_metric(&pTheme->wVArrowHgt, bmp.nHeight);
  grp_to_bmpx(pGroup, 0x1D, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wUnk6, bmp.nHeight);
  grp_to_bmpx(pGroup, 0x20, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wUnk7, bmp.nWidth);
  grp_to_bmpx(pGroup, 0x1C, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wThumbWid, bmp.nWidth);
  theme_metric(&pTheme->wThumbHgt, bmp.nHeight);
  grp_to_bmpx(pGroup, 0x35, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wUnk10, bmp.nWidth);
  theme_metric(&pTheme->wUnk11, bmp.nHeight);
  grp_to_bmpx(pGroup, 0x23, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wCombo1OpenWid, bmp.nWidth);
  theme_metric(&pTheme->wUnk13, bmp.nHeight);
  grp_to_bmpx(pGroup, 0x2F, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wUnk14, bmp.nWidth);
  theme_metric(&pTheme->wUnk15, bmp.nHeight);
  grp_to_bmpx(pGroup, 0x32, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wCombo1ArrowWid, bmp.nWidth);
  grp_to_bmpx(pGroup, 0x4D, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wUnk17, bmp.nWidth);
  theme_metric(&pTheme->wUnk18, bmp.nHeight);
  grp_to_bmpx(pGroup, 0x3B, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wCombo2OpenWid, bmp.nWidth);
  theme_metric(&pTheme->wUnk20, bmp.nHeight);
  grp_to_bmpx(pGroup, 0x47, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wUnk21, bmp.nWidth);
  theme_metric(&pTheme->wUnk22, bmp.nHeight);
  grp_to_bmpx(pGroup, 0x4A, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wCombo2ArrowWid, bmp.nWidth);
  grp_to_bmpx(pGroup, 0x5C, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wUnk24, bmp.nWidth);
  grp_to_bmpx(pGroup, 0x63, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wSliderSlotOnWid, bmp.nWidth);
  theme_metric(&pTheme->wUnk26, bmp.nHeight);
  grp_to_bmpx(pGroup, 0x61, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wSliderSlotWid, bmp.nWidth);
  grp_to_bmpx(pGroup, 0x67, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wUnk28, bmp.nWidth);
  grp_to_bmpx(pGroup, 0x85, &bmp, &nX, &nY, &nW, &nH);
  theme_metric(&pTheme->wUnk29, bmp.nWidth);
  dlg_stats_init(pTheme);
  return reinterpret_cast<intptr_t>(pTheme);
}
