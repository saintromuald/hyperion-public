// dlg_ctrl.cpp -- the dialog control layer: the node walks (ctrl_chain,
// ctrl_chainr), the flag accessors, the event constructors, and the focus,
// hover and hotkey dispatch on top of them. Bodies are in ascending oracle VA
// order, with one deviation: ctrl_clear_mouse_in (0x4C1C70) is hoisted above
// ctrl_activate and ctrl_check_mouse_in, because the oracle inlines it into
// both and only the hoist reproduces that. A small helper's position is
// otherwise free here -- ctrl_set_update is inlined into ten functions, nine
// of them defined above it.

// One node type does duty for both dialogs and controls: `wType == 0` means the
// node is a dialog, anything else a control owned by the dialog at +0x32. The
// idiom that opens a dozen of these bodies --
//     cmp word ptr [ecx+22h], 0h / jz $+3 / mov ecx, dword ptr [ecx+32h]
// -- is "if I am not a dialog, use my parent", so a caller may hand any node to
// any of them. The layout and the two handler ABIs are in src117/h/dialog.h.

// The event constructor is the other half: a dozen functions build a 0x14-byte
// dlgevt_t on the stack, stamp wTag = 0x0E and the cursor position into it, and
// call the node's own +0x2A handler. Field-assignment order is source order,
// since /Ox does not reschedule stores into a local struct, and it is not
// uniform across the family: ctrl_set_tabstop's second event writes dwId last
// where its first writes it first. Both are transcribed as the oracle has them.
#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>

#include "dialog.h"
#include "rect16.h"
#include "dlg_ctrl.h"
#include "dlg_dlg.h"   // tolower, list_add_node_head, dlg_deactivate
#include "bmp.h"       // bmp_destroy
#include "Storm.h"
#include "dlg_timer.h"   // timer_remove_ctrl

// The game globals.

// gCursorPos_g @0x6D3154 (gds/mou_win.cpp) -- one eight-byte object, not two
// globals: the next name in that segment is gwMouseFlags @0x6D315C, so it spans
// two dwords, [0] x and [1] y. The 32-bit width comes from the writer, WndProc
// storing 639 and 479 into the two halves. Every reader here takes only the low
// word, because the destination is dlgevt_t's 16-bit nX/nY, and that is what
// the (short) casts below are for.
#ifdef OC_DLL_MODE
#define gCursorPos_g (reinterpret_cast<LONG*>(OC_VA(0x6D3154u)))
#else
extern LONG gCursorPos_g[2];
#endif

// gpChainCtrl @0x6CE654 (dlg/ctrl.cpp) -- the control a chain walk settled on:
// ctrl_chainr publishes the node whose callback returned true, and
// ctrl_update_defpushbutton the default push button it picked. The type comes
// from the reader: statcmd_dlg_char_evn loads it straight into ctrl_send_msg's
// ECX, a ctrl_t*. Four dword-wide sites, bounded above by gnDlgCount @0x6CE658.
#ifdef OC_DLL_MODE
#define gpChainCtrl (*reinterpret_cast<ctrl_t**>(OC_VA(0x6CE654u)))
#else
extern ctrl_t *gpChainCtrl;
#endif

// sgwTableSize @0x6CE65C (dlg/ctrl.cpp) -- ctrl_set_evt_fcns stores
// `(WORD)(cb >> 2)` of its third argument here, the entry count of the dword
// handler table it was handed. It is the only reference to the address in the
// whole oracle, so the width rests on the single `mov word ptr` store and on
// gnDlgCount @0x6CE658 and sgbDlgDeactivating_g @0x6CE660 bracketing it into a
// 4-byte slot.
#ifdef OC_DLL_MODE
#define sgwTableSize (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE65Cu)))
#else
extern dlg_word_t sgwTableSize;
#endif

// byte_50EF28 @0x50EF28 (dlg/ctrl.cpp) -- a BYTE table indexed by
// ctrl_t::wType: ctrl_find_key_handler skips any control whose entry is zero,
// so it gates whether a control kind takes keyboard input. A genuine array
// base, not a folded element, since the index is a register. The extent 16 is
// an upper bound, not a count: the next named global in the same segment is
// gDlgGroups_g @0x50EF38, so the table cannot exceed 0x10 bytes, and the
// control kinds seen so far run 0..0x0E.
#ifdef OC_DLL_MODE
#define byte_50EF28 (reinterpret_cast<dlg_byte_t*>(OC_VA(0x50EF28u)))
#else
extern dlg_byte_t byte_50EF28[16] OC_UNALIGNED;
#endif

// 0x6CE658 gnDlgCount (dlg/ctrl.cpp), the number of live dialogs.
#ifdef OC_DLL_MODE
#define gnDlgCount (*reinterpret_cast<LONG *>(OC_VA(0x6CE658u)))
#else
extern LONG gnDlgCount;
#endif

// 0x6CE6D8 gpDlgList (dlg/dlg.cpp), head of the live dialog list.
#ifdef OC_DLL_MODE
#define gpDlgList (*reinterpret_cast<ctrl_t **>(OC_VA(0x6CE6D8u)))
#else
extern ctrl_t *gpDlgList;
#endif

// ctrl_dlg (0x4C1470, 777 B) -- the dialog node's own handler: activation and
// teardown (tag 0x0E), hover, keys and hotkeys, and clicks forwarded to the
// control under the cursor. Source case order sets the block order, and
// ctrl_find_tabstop with its stepper is inlined at all three sites.
int __fastcall ctrl_dlg(ctrl_t *pDlg, dlgevt_t *pEvent) {
  char ch;

  switch (pEvent->wTag) {
  case 0x0E:
    switch (pEvent->dwId) {
    case 0x0A:
      ++gnDlgCount;
      ctrl_chain(pDlg->pChildren, pEvent, ctrl_evt);
      list_add_node_head(&gpDlgList, pDlg);
      return ctrl_evn_dlg(pDlg, pEvent);
    case 0: {
      ctrl_t *pCtrl;

      ctrl_set_update(pDlg);
      if (!pDlg->pFocus)
        ctrl_find_tabstop(pDlg, ctrl_tab_next);
      ctrl_chain(pDlg->pChildren, pEvent, ctrl_evt);
      pCtrl = ctrl_find(pDlg, pEvent);
      if (pCtrl)
        ctrl_set_mouse_in(pDlg, pCtrl);
      break;
    }
    case 2:
      dlg_deactivate(pDlg);
      return 1;
    case 4:
      return 1;
    case 1:
      ctrl_chain(pDlg->pChildren, pEvent, ctrl_evt);
      bmp_destroy((bmp_t *)&pDlg->_unmapped36[0]);
      --gnDlgCount;
      break;
    }
    return ctrl_evn_dlg(pDlg, pEvent);
  case 3:
    if (cursor_in_dlg(pDlg, pEvent))
      ctrl_check_mouse_in(pDlg, pEvent);
    else
      ctrl_clear_mouse_in(pDlg);
    return pDlg->dwFlags & 0x80000000;
  case 0x0F:
    return ctrl_dlg_char(pDlg, pEvent);
  case 0:
    ch = (char)pEvent->_unmapped08[0];
    if (ctrl_chainr(pDlg, (dlgevt_t *)&ch, (PFN_CTRL_EVT)ctrl_check_virt_hotkey))
      return 1;
    if (pEvent->_unmapped08[2] & 8) {
      ch = (char)pEvent->_unmapped08[0];
      if (ctrl_chainr(pDlg, (dlgevt_t *)&ch, (PFN_CTRL_EVT)ctrl_check_hotkey_upper))
        return 1;
    }
  case 1:
  case 2: {
    dlg_dword_t dwFlags;

    if (pDlg->pFocus && pDlg->pFocus->pfnEvt(pDlg->pFocus, pEvent))
      return 1;
    if ((pEvent->wTag == 0 || pEvent->wTag == 1) && ctrl_find_key_handler(pDlg, pEvent))
      return 1;
    dwFlags = pDlg->dwFlags;
    if (!(dwFlags & 0x20000000) && (pEvent->wTag == 0 || pEvent->wTag == 1)) {
      switch (*(dlg_word_t *)&pEvent->_unmapped08[0]) {
      case 0x25:
      case 0x26:
        if (ctrl_find_tabstop(pDlg, ctrl_tab_prev))
          return 1;
        break;
      case 0x27:
      case 0x28:
        if (ctrl_find_tabstop(pDlg, ctrl_tab_next))
          return 1;
        break;
      }
    }
    return dwFlags & 0x80000000;
  }
  case 4:
  case 6:
  case 7:
  case 9: {
    ctrl_t *pCtrl = ctrl_find(pDlg, pEvent);

    if (!pCtrl)
      return pDlg->dwFlags & 0x80000000;
    if (pCtrl == pDlg)
      return 1;
    ctrl_set_tabstop(pCtrl);
    return pCtrl->pfnEvt(pCtrl, pEvent);
  }
  }
  return 0;
}

// ctrl_rect -- frees the control's allocation at +0x10 on event 1, then handles
// the event as ctrl_text does.
int __fastcall ctrl_rect(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    if (pEvent->dwId == 1 && pCtrl->nResIndex)
      SMemFree((void *)pCtrl->nResIndex, "ctrl.cpp", 0x3EF, 0);
    return ctrl_text(pCtrl, pEvent);
  }
  return 0;
}

// ctrl_text -- passes the dialog-level events to ctrl_evn_dlg and swallows the
// rest.
int __fastcall ctrl_text(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 0:
    case 6:
    case 8:
    case 0x0A:
    case 0x0D:
    case 0x0E:
      return ctrl_evn_dlg(pCtrl, pEvent);
    case 4:
      return 0;
    }
  }
  return 0;
}

// ctrl_chain -- walks the node and everything below and after it, calling
// fn(node, param) on each. The "first child if I am a dialog, else my next
// sibling" step is the shape ctrl_chainr, ctrl_set_evt_fcns and
// ctrl_update_defpushbutton share.
void __fastcall ctrl_chain(ctrl_t *pCtrl, dlgevt_t *pEvent, PFN_CTRL_EVT fn) {
  while (pCtrl) {
    ctrl_t *pNext;
    if (pCtrl->wType == 0)
      pNext = pCtrl->pChildren;
    else
      pNext = pCtrl->pNext;
    fn(pCtrl, pEvent);
    pCtrl = pNext;
  }
}

// ctrl_chainr -- as ctrl_chain, but stops at the first callback that returns
// non-zero, publishes that node to gpChainCtrl and returns the callback's own
// result.
int __fastcall ctrl_chainr(ctrl_t *pCtrl, dlgevt_t *pEvent, PFN_CTRL_EVT fn) {
  while (pCtrl) {
    ctrl_t *pNext;
    int nRet;
    if (pCtrl->wType == 0)
      pNext = pCtrl->pChildren;
    else
      pNext = pCtrl->pNext;
    nRet = fn(pCtrl, pEvent);
    if (nRet) {
      gpChainCtrl = pCtrl;
      return nRet;
    }
    pCtrl = pNext;
  }
  return 0;
}

// ctrl_check_hotkey_upper -- does this control's accelerator letter, uppercased,
// match the key the user pressed? If so, fire command event 3 on it. The flags
// reload is the oracle's: the 0x200 test reads the member directly and the
// inlined locatability ladder reads it again, because _toupper sits between
// them and the compiler cannot carry a memory value across an opaque call.
// ctrl_check_hotkey, which has no call there, folds the two reads into one, so
// writing this one through a local would cost the reload.
int __fastcall ctrl_check_hotkey_upper(ctrl_t *pCtrl, const char *pKey) {
  dlgevt_t evt;
  // The locatability ladder is not a call to ctrl_query_locate, and that is
  // derived: that function's tail narrows the flags to a byte and returns bit 4
  // as 0 or 1, which does not fold to a single `test al, 10h` at a branch site,
  // and `test al, 10h / jz` is what the oracle emits here and in the two
  // siblings. So the callers hold a different expression from the one the
  // function returns -- a macro or an inline duplicate in Blizzard's source.
  // ctrl_send_cmd_index, by contrast, really does call it.
  //
  // One `&&` chain, not six `if (...) return 0;` statements, read off the branch
  // targets: every failing branch lands on the same out-of-line `xor eax, eax`
  // tail at the end of the function, where the sequential-if spelling hoists
  // that tail next to the first test and turns the other five branches
  // backwards.
  if ((pCtrl->dwFlags & 0x200) &&
      *pKey == _toupper(*(const char *)pCtrl->pData) &&
      (pCtrl->dwFlags & 8) && !(pCtrl->dwFlags & 2) &&
      pCtrl->nIndex != -1 &&
      ((pCtrl->nIndex & 0x8000) || (pCtrl->dwFlags & 0x10))) {
    evt.dwId = 3;
    evt.wTag = 0x0E;
    evt.nX = (short)gCursorPos_g[0];
    evt.nY = (short)gCursorPos_g[1];
    evt.dwParam = 0;
    return pCtrl->pfnEvt(pCtrl, &evt);
  }
  return 0;
}

// ctrl_check_hotkey -- the case-sensitive sibling of ctrl_check_hotkey_upper: no
// _toupper, so the flags load is hoisted into a register once and reused by the
// inlined ladder. Flag 0x200 means pData[0] is a normalised accelerator letter:
// dlg_load_ctrl runs `tolower` on it when 0x200 is set, so the stored letter is
// always lower case, and the two checkers are the two halves of one
// case-insensitive match against it --
//   ctrl_check_hotkey       *pKey == pData[0]            (lower-case keypress)
//   ctrl_check_hotkey_upper *pKey == _toupper(pData[0])  (upper-case keypress)
// -- with the oracle applying _toupper to pData, not to pKey. The three hotkey
// checkers need three different structures for what is one function modulo case
// handling, and all three are 130 or 145 bytes, so size alone does not tell
// them apart. Do not simplify them into one shape.
int __fastcall ctrl_check_hotkey(ctrl_t *pCtrl, const char *pKey) {
  dlgevt_t evt;
  // The flags go through an explicit local here, and that is not a style
  // difference: with no call in the chain, repeating `pCtrl->dwFlags` keeps the
  // value in ECX and pushes pData into a callee-saved register, where naming it
  // once puts it in EDX and frees ECX for pData, the oracle's own allocation.
  // Same size either way.
  dlg_dword_t dwFlags = pCtrl->dwFlags;
  if (!(dwFlags & 0x200))
    return 0;
  if (*pKey != *(const char *)pCtrl->pData)
    return 0;
  if ((dwFlags & 8) && !(dwFlags & 2) &&
      pCtrl->nIndex != -1 &&
      ((pCtrl->nIndex & 0x8000) || (dwFlags & 0x10))) {
    evt.dwId = 3;
    evt.wTag = 0x0E;
    evt.nX = (short)gCursorPos_g[0];
    evt.nY = (short)gCursorPos_g[1];
    evt.dwParam = 0;
    return pCtrl->pfnEvt(pCtrl, &evt);
  }
  return 0;
}

// ctrl_check_virt_hotkey -- byte for byte ctrl_check_hotkey with 0x100 in place
// of 0x200, a second accelerator flag. "Virtual key" is the name's claim, not
// something derived here; what is derived is that 0x100 gates the same raw byte
// compare against pData[0], with no case handling on either side.
int __fastcall ctrl_check_virt_hotkey(ctrl_t *pCtrl, const char *pKey) {
  dlgevt_t evt;
  dlg_dword_t dwFlags = pCtrl->dwFlags;
  if (!(dwFlags & 0x100))
    return 0;
  if (*pKey != *(const char *)pCtrl->pData)
    return 0;
  if ((dwFlags & 8) && !(dwFlags & 2) &&
      pCtrl->nIndex != -1 &&
      ((pCtrl->nIndex & 0x8000) || (dwFlags & 0x10))) {
    evt.dwId = 3;
    evt.wTag = 0x0E;
    evt.nX = (short)gCursorPos_g[0];
    evt.nY = (short)gCursorPos_g[1];
    evt.dwParam = 0;
    return pCtrl->pfnEvt(pCtrl, &evt);
  }
  return 0;
}

// ctrl_update_cursor -- offers the control under the cursor a hover event (id 4),
// testing in the dialog's local coordinates; the copied event keeps the global
// ones. Three arguments, the third on the stack (`ret 4`). The point is
// re-derived from pEvent, where `pt.x -= ...` costs a `movzx edx, ax`.
void __fastcall ctrl_update_cursor(ctrl_t *pDlg, ctrl_t *pCtrl,
                                   dlgevt_t *pEvent) {
  dlgevt_t evt;
  ctrl_t *pOrigin;
  point16_t pt;
  pt.x = pEvent->nX;
  pt.y = pEvent->nY;
  if (pt.x < pDlg->rc.left || pt.x > pDlg->rc.right ||
      pt.y < pDlg->rc.top || pt.y > pDlg->rc.bottom)
    return;
  pOrigin = pDlg;
  if (pDlg->wType != 0)
    pOrigin = pDlg->pParent;
  pt.x = pEvent->nX - pOrigin->rc.left;
  pt.y = pEvent->nY - pOrigin->rc.top;
  evt = *pEvent;
  evt.wTag = 0x0E;
  evt.dwId = 4;
  if (pt.x < pCtrl->rc.left || pt.x > pCtrl->rc.right ||
      pt.y < pCtrl->rc.top || pt.y > pCtrl->rc.bottom)
    return;
  if (!pCtrl->pfnEvt(pCtrl, &evt))
    return;
  if (pCtrl->dwFlags & 0x80)
    return;
  ctrl_set_mouse_in(pDlg, pCtrl);
}

// ctrl_clear_mouse_in -- out of VA order on purpose, the one deviation in this
// file: the oracle inlines this body byte for byte into ctrl_activate
// (0x4C1B90), ctrl_check_mouse_in (0x4C1C20) and ctrl_set_mouse_in (0x4C2690),
// and the hoist above the first two is what reproduces that.
void __fastcall ctrl_clear_mouse_in(ctrl_t *pDlg) {
  ctrl_t *pCtrl = pDlg->pMouseIn;
  if (pCtrl) {
    pCtrl->dwFlags &= ~0x80u;
    if (pCtrl->pData)
      ctrl_set_update(pCtrl);
    pDlg->pMouseIn = 0;
  }
}

// ctrl_activate -- re-runs mouse containment for a dialog that has just become
// active: find the control under the cursor and make it the hovered one, or, if
// the cursor is over nothing, clear the dialog's hover entirely
// (ctrl_clear_mouse_in, inlined here byte for byte). Event id 0 with parameter
// 0. The `jnz $+3Bh` on the 0x80 test lands on the epilogue, not on the clear
// block: a control that already owns the mouse is left alone without clearing
// pMouseIn, so the two arms cannot be folded.
void __fastcall ctrl_activate(ctrl_t *pDlg) {
  dlgevt_t evt;
  ctrl_t *pCtrl;
  evt.dwId = 0;
  evt.wTag = 0x0E;
  evt.nX = (short)gCursorPos_g[0];
  evt.dwParam = 0;
  evt.nY = (short)gCursorPos_g[1];
  pCtrl = ctrl_find(pDlg, &evt);
  if (pCtrl) {
    if (!(pCtrl->dwFlags & 0x80))
      ctrl_set_mouse_in(pDlg, pCtrl);
    return;
  }
  ctrl_clear_mouse_in(pDlg);
}

void __fastcall ctrl_check_mouse_in(ctrl_t *pDlg, dlgevt_t *pEvent) {
  ctrl_t *pCtrl = ctrl_find(pDlg, pEvent);
  if (pCtrl) {
    // A control that already owns the mouse is left alone, and without clearing
    // pDlg->pMouseIn: the `jnz $+37h` at oracle offset 16 lands on the epilogue,
    // not on the clear block, so folding this branch into the clear path is a
    // behavioural change, not a shape preference.
    if (!(pCtrl->dwFlags & 0x80))
      ctrl_set_mouse_in(pDlg, pCtrl);
    return;
  }
  ctrl_clear_mouse_in(pDlg);
}

// ctrl_dlg_char (0x4C1CB0, 391 B) -- the dialog's key handler: tab steps the
// focus, escape and enter fire the cancel and default commands, space presses
// the focused control, and any other key is offered as a hotkey, then to the
// focus, then to every keyboard-taking child. Case order sets the block order,
// and the handler is read into a local so its load precedes the dwParam store.
int __fastcall ctrl_dlg_char(ctrl_t *pDlg, dlgevt_t *pEvent) {
  ctrl_t *pFocus = pDlg->pFocus;
  dlgevt_t evt;
  char ch;

  switch (*(dlg_word_t *)&pEvent->_unmapped08[0]) {
  case 9:
    ctrl_find_tabstop(pDlg, (pEvent->_unmapped08[2] & 3) ? ctrl_tab_prev : ctrl_tab_next);
    return 1;
  case 0x1B:
    if (ctrl_send_cmd_index(pDlg, -3))
      return 1;
    if (ctrl_send_cmd(pDlg, 0x40))
      return 1;
    break;
  case 0x0D:
    if (pFocus && (pFocus->wType == 2 || pFocus->wType == 1) &&
        ctrl_send_cmd_index(pDlg, pFocus->nIndex))
      return 1;
    if (ctrl_send_cmd_index(pDlg, -2))
      return 1;
    if (ctrl_send_cmd(pDlg, 0x80000))
      return 1;
    break;
  case 0x20:
    if (pFocus && ctrl_query_locate(pFocus)) {
      evt.dwId = 3;
      evt.wTag = 0x0E;
      evt.nX = (short)gCursorPos_g[0];
      evt.nY = (short)gCursorPos_g[1];
      PFN_CTRL_EVT pfnEvt = pFocus->pfnEvt;
      evt.dwParam = 0;
      if (pfnEvt(pFocus, &evt))
        return 1;
    }
    break;
  }
  if (!(*(dlg_word_t *)&pEvent->_unmapped08[2] & 0x100) && !ctrl_find_edit(pDlg)) {
    ch = (char)tolower(*(dlg_word_t *)&pEvent->_unmapped08[0]);
    // The key byte stands in for the event; ctrl_check_hotkey reads it as one.
    if (ctrl_chainr(pDlg, (dlgevt_t *)&ch, (PFN_CTRL_EVT)ctrl_check_hotkey))
      return 1;
  }
  if (pFocus && pFocus->pfnEvt(pFocus, pEvent))
    return 1;
  if (ctrl_find_key_handler(pDlg, pEvent))
    return 1;
  return pDlg->dwFlags & 0x80000000;
}

// ctrl_dlg_set_current -- sets the dialog's focused control, unless the dialog
// carries flag 0x20000000, in which case the focus is forced to NULL. The
// `mov eax, 0h / cmovnz edx, eax` pair is the conditional-move form of that
// clamp.
void __fastcall ctrl_dlg_set_current(ctrl_t *pCtrl, ctrl_t *pCurrent) {
  if (pCtrl->wType != 0)
    pCtrl = pCtrl->pParent;
  if (pCtrl->dwFlags & 0x20000000)
    pCurrent = 0;
  pCtrl->pFocus = pCurrent;
}

// 0x4C1E90. The shared tag-0x0E handler: creation, teardown, focus loss,
// show and hide, and the delayed show counted down in steps of 100.
int ctrl_evn_dlg(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  dlg_word_t *pwDelay = (dlg_word_t *)&pCtrl->_unmapped1C[2];

  switch (pEvent->dwId) {
  case 0x0A:
    if (*pwDelay != 0) {
      ctrl_hide(pCtrl);
      *pwDelay = (dlg_word_t)((*pwDelay + 50) / 100 * 100);
    }
  // The fall-through seats the shared return block right after this case.
  case 5:
    return 1;
  case 1:
    timer_remove_ctrl(pCtrl);
    dlg_unhook_all(pCtrl);
    if (pCtrl->nResIndex && !(pCtrl->dwFlags & 0x20))
      SMemFree((void *)pCtrl->nResIndex, "ctrl.cpp", 0x254, 0);
    if (pCtrl->dwFlags & 4)
      SMemFree(pCtrl, "ctrl.cpp", 0x257, 0);
    break;
  case 6:
    dlg_unhook_all(pCtrl);
    if (pCtrl->wType == 0)
      ctrl_chain(pCtrl->pChildren, pEvent, ctrl_evt);
    else if (pCtrl == pCtrl->pParent->pFocus &&
             !ctrl_find_tabstop(pCtrl->pParent, ctrl_tab_next))
      ctrl_dlg_set_current(pCtrl, 0);
    break;
  case 4:
    return ctrl_query_locate(pCtrl);
  case 8:
    if (pCtrl->wType == 0) {
      ctrl_chain(pCtrl->pChildren, pEvent, ctrl_evt);
    } else if (pEvent->dwParam == 0x0C && !(pCtrl->dwFlags & 8) &&
               *pwDelay != 0) {
      *pwDelay -= 100;
      if (*pwDelay == 0)
        ctrl_show(pCtrl);
    }
    break;
  case 0x0E:
    pCtrl->dwFlags &= ~8u;
    break;
  case 0x0D:
    if (*pwDelay == 0) {
      pCtrl->dwFlags |= 8;
      ctrl_update_cursor(pCtrl->pParent, pCtrl, pEvent);
    }
    break;
  default:
    return 0;
  }
  return 1;
}

// ctrl_evt -- `mov eax, ecx / mov eax, [eax+2Ah] / jmp eax`: a tail call through
// the node's own event handler. The `jmp eax` rather than call/ret is what a
// tail-return of an indirect call compiles to, and it is why the handler's
// signature has to be exactly the enclosing one -- ECX and EDX already carry
// (node, event) on entry under /Gr and pass straight through untouched.
int __fastcall ctrl_evt(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  return pCtrl->pfnEvt(pCtrl, pEvent);
}

// ctrl_find -- offers a hit-test event (id 4) to each child under the cursor,
// then to the dialog. The event is copied once and the local point re-derived
// from pEvent, where `pt.x -= ...` loads x with `mov bx`; pDlg spills to
// [ebp-4].
ctrl_t *__fastcall ctrl_find(ctrl_t *pDlg, dlgevt_t *pEvent) {
  dlgevt_t evt = *pEvent;
  ctrl_t *pOrigin;
  ctrl_t *pChild;
  point16_t pt;
  pt.x = pEvent->nX;
  pt.y = pEvent->nY;
  // One `||` chain; separate returns hoist the shared tail next to the first test.
  if (pt.x < pDlg->rc.left || pt.x > pDlg->rc.right ||
      pt.y < pDlg->rc.top || pt.y > pDlg->rc.bottom)
    return 0;
  pOrigin = pDlg;
  if (pDlg->wType != 0)
    pOrigin = pDlg->pParent;
  pt.x = pEvent->nX - pOrigin->rc.left;
  pt.y = pEvent->nY - pOrigin->rc.top;
  evt.wTag = 0x0E;
  evt.dwId = 4;
  for (pChild = pDlg->pChildren; pChild; pChild = pChild->pNext) {
    if (pt.x < pChild->rc.left)
      continue;
    if (pt.x > pChild->rc.right)
      continue;
    if (pt.y < pChild->rc.top)
      continue;
    if (pt.y > pChild->rc.bottom)
      continue;
    if (pChild->pfnEvt(pChild, &evt))
      return pChild;
  }
  return pDlg->pfnEvt(pDlg, &evt) ? pDlg : 0;
}

// ctrl_find_edit -- finds the dialog's first visible, enabled, locatable wType 8
// control and publishes it to gpChainCtrl.
static int __fastcall ctrl_is_edit(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pCtrl->wType != 8)
    return 0;
  return (pCtrl->dwFlags & 0x1A) == 0x18;
}

ctrl_t *__fastcall ctrl_find_edit(ctrl_t *pDlg) {
  if (ctrl_chainr(pDlg->pChildren, 0, ctrl_is_edit))
    return gpChainCtrl;
  return 0;
}

// ctrl_find_key_handler -- offers the event to every child that is of a
// keyboard-taking kind and visible and enabled: flags & 0x1A == 0x18, so 0x08
// set, 0x02 clear and 0x10 clear.
int __fastcall ctrl_find_key_handler(ctrl_t *pDlg, dlgevt_t *pEvent) {
  ctrl_t *pCtrl;
  for (pCtrl = pDlg->pChildren; pCtrl; pCtrl = pCtrl->pNext) {
    if (byte_50EF28[pCtrl->wType] != 0 &&
        (pCtrl->dwFlags & 0x1A) == 0x18) {
      if (pCtrl->pfnEvt(pCtrl, pEvent)) {
        ctrl_set_tabstop(pCtrl);
        return 1;
      }
    }
  }
  return 0;
}

ctrl_t *__fastcall ctrl_find_ndx(ctrl_t *pCtrl, short nIndex) {
  ctrl_t *pChild;
  if (pCtrl->wType != 0)
    pCtrl = pCtrl->pParent;
  for (pChild = pCtrl->pChildren; pChild; pChild = pChild->pNext) {
    if (pChild->nIndex == nIndex)
      return pChild;
  }
  return 0;
}

// ctrl_find_tabstop -- steps around the dialog's control ring with the caller's
// stepper until it finds an enabled, visible, tab-stopping control or comes back
// to where it started.
int __fastcall ctrl_find_tabstop(ctrl_t *pDlg, PFN_CTRL_STEP fnStep) {
  ctrl_t *pCtrl = pDlg->pFocus;
  ctrl_t *pFirst;
  pCtrl = pCtrl ? fnStep(pCtrl) : pDlg->pChildren;
  pFirst = pCtrl;
  do {
    dlg_dword_t dwFlags = pCtrl->dwFlags;
    if (!(dwFlags & 2) && (dwFlags & 8) && (dwFlags & 0x10)) {
      ctrl_set_tabstop(pCtrl);
      return 1;
    }
    pCtrl = fnStep(pCtrl);
  } while (pCtrl != pFirst);
  return 0;
}

// ctrl_force_update -- `or dword ptr [ecx+18h], 1h / jmp dlg_area_invalid`: sets
// the redraw bit and tail-calls the invalidator unconditionally, which is what
// distinguishes it from ctrl_set_update, whose whole body is the "already dirty"
// test. Flag 0x1 is "this node needs redrawing".
void __fastcall ctrl_force_update(ctrl_t *pCtrl) {
  pCtrl->dwFlags |= 0x1;
  dlg_area_invalid(pCtrl);
}

// ctrl_gray -- disables the control: clear "mouse in", set "disabled", repaint,
// then tell the control itself (event 6).
void __fastcall ctrl_gray(ctrl_t *pCtrl) {
  dlgevt_t evt;
  dlg_dword_t dwFlags = pCtrl->dwFlags;
  if (dwFlags & 2)
    return;
  pCtrl->dwFlags = (dwFlags & ~0x80u) | 2;
  ctrl_set_update(pCtrl);
  evt.dwId = 6;
  evt.wTag = 0x0E;
  evt.nX = (short)gCursorPos_g[0];
  evt.nY = (short)gCursorPos_g[1];
  evt.dwParam = 0;
  pCtrl->pfnEvt(pCtrl, &evt);
}

// ctrl_hide -- two events, not one, and no flag write of its own: the control is
// asked (event 0x0E) whether it may be hidden, and only if it agrees is it told
// (event 6) and repainted. Clearing the visible bit 0x08 is the handler's job,
// so nothing here touches +0x18 except the inlined ctrl_set_update. 0x0E
// appears twice per event, once as the id and once as the tag, and the oracle
// materialises it into EDI for both; that the "hide" event id collides with the
// constant tag is a coincidence of the ids.
void __fastcall ctrl_hide(ctrl_t *pCtrl) {
  dlgevt_t evt;
  if (!(pCtrl->dwFlags & 8))
    return;
  evt.wTag = 0x0E;
  evt.nX = (short)gCursorPos_g[0];
  evt.nY = (short)gCursorPos_g[1];
  evt.dwId = 0x0E;
  evt.dwParam = 0;
  if (!pCtrl->pfnEvt(pCtrl, &evt))
    return;
  evt.wTag = 0x0E;
  evt.nX = (short)gCursorPos_g[0];
  evt.nY = (short)gCursorPos_g[1];
  evt.dwId = 6;
  evt.dwParam = 0;
  pCtrl->pfnEvt(pCtrl, &evt);
  ctrl_set_update(pCtrl);
}

// ctrl_is_hot -- "hot" means drawn highlighted. The focused control is always
// hot; otherwise a kind-1 control is hot when the dialog has no focus at all
// and is not suppressed by flag 0x20000000.
int __fastcall ctrl_is_hot(ctrl_t *pCtrl) {
  dlg_dword_t dwFlags = pCtrl->dwFlags;
  if (!(dwFlags & 2) && (dwFlags & 8)) {
    ctrl_t *pDlg = pCtrl->pParent;
    ctrl_t *pFocus = pDlg->pFocus;
    if (pCtrl == pFocus)
      return 1;
    if (pCtrl->wType == 1 && !pFocus && !(pDlg->dwFlags & 0x20000000))
      return 1;
  }
  return 0;
}

// ctrl_tab_next -- the next node in tab order: the sibling if there is one, else
// wrap to the owning dialog's first child. It reads pParent unconditionally on
// the wrap path, without the usual dialog test, since a node reached here is
// always a control and so always has a parent.
ctrl_t *__fastcall ctrl_tab_next(ctrl_t *pCtrl) {
  ctrl_t *pNext = pCtrl->pNext;
  if (pNext)
    return pNext;
  return pCtrl->pParent->pChildren;
}

// ctrl_tab_prev -- the previous node in tab order: the sibling whose pNext is
// this node, wrapping from the first child to the last.
ctrl_t *__fastcall ctrl_tab_prev(ctrl_t *pCtrl) {
  ctrl_t *pPrev = pCtrl->pParent->pChildren;
  if (pCtrl == pPrev)
    pCtrl = 0;
  while (pPrev->pNext != pCtrl)
    pPrev = pPrev->pNext;
  return pPrev;
}

// ctrl_query_locate -- the `(dlg_byte_t)` cast in the last line is a guess that
// happens to match. The oracle's tail is `movzx eax, al / shr eax, 4h /
// and eax, 1h`, and that narrowing is the one part of this body whose source
// cannot be argued for: a plain `(dwFlags >> 4) & 1` emits shr+and with no
// movzx, and `(dwFlags & 0x10) != 0` emits test/setne/movzx. Casting the dword
// to a byte first reproduces the oracle, but why Blizzard's source narrowed
// there is not derived.
int __fastcall ctrl_query_locate(ctrl_t *pCtrl) {
  dlg_dword_t dwFlags = pCtrl->dwFlags;
  short nIndex;
  if (!(dwFlags & 8))
    return 0;
  if (dwFlags & 2)
    return 0;
  nIndex = pCtrl->nIndex;
  if (nIndex == -1)
    return 0;
  if (nIndex & 0x8000)
    return 1;
  return ((dlg_byte_t)dwFlags >> 4) & 1;
}

// ctrl_send_cmd (0x4C2430, 135 B) fires command event 3 at the cursor on one
// control. This helper is separate because the event written inline there
// stores dwParam before the handler load.
static __forceinline int ctrl_fire_cmd_evt(ctrl_t *pChild) {
  dlgevt_t evt;
  evt.dwId = 3;
  evt.wTag = 0x0E;
  evt.nX = (short)gCursorPos_g[0];
  evt.nY = (short)gCursorPos_g[1];
  evt.dwParam = 0;
  return pChild->pfnEvt(pChild, &evt);
}

// Finds the first child carrying any bit of the caller's mask and fires command
// event 3 on it. The locatability ladder is inlined, and each of its failure
// arms merges onto the same `xor eax, eax` as "no child matched": an
// unlocatable match is a hard zero, not a reason to keep walking. The four
// branch displacements all resolve to the return-0 block, none to the loop
// head.
int __fastcall ctrl_send_cmd(ctrl_t *pCtrl, dlg_dword_t dwMask) {
  ctrl_t *pChild;
  if (pCtrl->wType != 0)
    pCtrl = pCtrl->pParent;
  for (pChild = pCtrl->pChildren; pChild; pChild = pChild->pNext) {
    if (pChild->dwFlags & dwMask) {
      // Locatability ladder, not a call -- see ctrl_check_hotkey_upper's note.
      dlg_dword_t dwFlags = pChild->dwFlags;
      short nIndex;
      if (!(dwFlags & 8))
        return 0;
      if (dwFlags & 2)
        return 0;
      nIndex = pChild->nIndex;
      if (nIndex == -1)
        return 0;
      if (!(nIndex & 0x8000) && !(dwFlags & 0x10))
        return 0;
      return ctrl_fire_cmd_evt(pChild);
    }
  }
  return 0;
}

// ctrl_send_cmd_index -- as ctrl_send_cmd but keyed on the control index, with
// ctrl_find_ndx inlined, and with an extra kind gate: only wType 1 or 2 may be
// commanded. The two bounds are separate unsigned compares against one
// zero-extended load, so the source reads the field once into a local and tests
// the upper bound first -- not a range subtraction, and not two reloads. Here
// ctrl_query_locate stays a real call while ctrl_send_cmd inlines it, which is
// the inliner's own budget decision after ctrl_find_ndx was already inlined.
int __fastcall ctrl_send_cmd_index(ctrl_t *pCtrl, short nIndex) {
  dlgevt_t evt;
  ctrl_t *pChild = ctrl_find_ndx(pCtrl, nIndex);
  dlg_word_t wType;
  PFN_CTRL_EVT fn;
  if (!pChild)
    return 0;
  wType = pChild->wType;
  if (wType > 2)
    return 0;
  if (wType < 1)
    return 0;
  if (!ctrl_query_locate(pChild))
    return 0;
  evt.dwId = 3;
  evt.wTag = 0x0E;
  evt.nX = (short)gCursorPos_g[0];
  evt.nY = (short)gCursorPos_g[1];
  fn = pChild->pfnEvt;   // before the last store -- see ctrl_send_cmd's note
  evt.dwParam = 0;
  return fn(pChild, &evt);
}

// ctrl_send_msg -- dlgevt_t::_unmapped08 is deliberately left uninitialised,
// since the oracle never writes it here.
int __fastcall ctrl_send_msg(ctrl_t *pCtrl, dlg_word_t wId, dlg_dword_t dwParam) {
  dlgevt_t evt;
  evt.wTag = 0x0E;
  evt.dwId = wId;
  evt.dwParam = dwParam;
  evt.nX = (short)gCursorPos_g[0];
  evt.nY = (short)gCursorPos_g[1];
  return pCtrl->pfnEvt(pCtrl, &evt);
}

// ctrl_set_big_text -- `mov eax,[ecx+18h] / and eax,-4401h / or eax,800h /
// mov [ecx+18h],eax / ret`: the contrasting shape to ctrl_set_med_text, and the
// pair is the evidence for both. Here the oracle loads the flags into a
// register, masks, sets 0x800 and stores once, where two compound assignments
// on the member emit two read-modify-writes. The mask clears only
// 0x400 | 0x4000, because 0x800 is about to be set.
void __fastcall ctrl_set_big_text(ctrl_t *pCtrl) {
  dlg_dword_t dwFlags = pCtrl->dwFlags;
  dwFlags &= ~0x4400u;
  dwFlags |= 0x800u;
  pCtrl->dwFlags = dwFlags;
}

// ctrl_set_evt_fcns -- re-binds every control's +0x2A handler from a
// caller-supplied table indexed by (nIndex - 1). Dialogs themselves
// (wType == 0) are skipped, as is any control whose index is not strictly
// positive.
void __fastcall ctrl_set_evt_fcns(ctrl_t *pCtrl, PFN_CTRL_EVT *pTable,
                                  dlg_dword_t cbTable) {
  sgwTableSize = (dlg_word_t)(cbTable >> 2);
  while (pCtrl) {
    dlg_word_t wType = pCtrl->wType;
    ctrl_t *pNext;
    short nIndex;
    if (wType == 0)
      pNext = pCtrl->pChildren;
    else
      pNext = pCtrl->pNext;
    nIndex = pCtrl->nIndex;
    if (nIndex > 0 && wType != 0) {
      // The subscript is computed and truncated in 16 bits before it is
      // widened: `lea eax, [esi-1h] / cwde / mov eax, [edi+eax*4]`, not the
      // 32-bit `movsx` plus folded -4 displacement a plain `pTable[i-1]` gives.
      // Only a short-typed subscript variable comes out that way.
      short nSlot = nIndex;
      PFN_CTRL_EVT fn;
      --nSlot;
      fn = pTable[nSlot];
      if (fn)
        pCtrl->pfnEvt = fn;
    }
    pCtrl = pNext;
  }
}

// ctrl_set_gray -- ctrl_gray stays a tail call; ctrl_ungray is inlined by the
// oracle.
void __fastcall ctrl_set_gray(ctrl_t *pCtrl, int bGray) {
  if (bGray)
    ctrl_gray(pCtrl);
  else
    ctrl_ungray(pCtrl);
}

void __fastcall ctrl_set_hot_flag(ctrl_t *pCtrl) {
  dlg_dword_t dwFlags = pCtrl->dwFlags;
  if (ctrl_is_hot(pCtrl))
    pCtrl->dwFlags = dwFlags | 0x1000;
  else
    pCtrl->dwFlags = dwFlags & ~0x1000u;
}

// ctrl_set_med_text -- `and dword ptr [ecx+18h], -4C01h / ret`: clears the
// three-bit text-size field (0x400 | 0x800 | 0x4000) in place, selecting the
// default face by leaving all three clear. A single read-modify-write with no
// reload, so the source is a compound assignment on the member rather than a
// read into a local and a write back.
void __fastcall ctrl_set_med_text(ctrl_t *pCtrl) {
  pCtrl->dwFlags &= ~0x4C00u;
}

// ctrl_set_mouse_in -- moves the dialog's hover from whatever held it to pCtrl:
// clear the old (ctrl_clear_mouse_in inlined), set 0x80 on the new, tell it
// (event 9), then record and repaint it. The event's dwParam is a reload of
// pDlg->pMouseIn, not the old control: `mov eax, [edi+46h]` sits after the
// inlined clear, which has just stored 0 there, so the handler is always handed
// NULL. That reads like a Blizzard bug -- the intent was surely "here is the
// control you are replacing" -- and it is reproduced by writing the reload,
// since the obvious `evt.dwParam = pOld` would hold the value in a register.
void __fastcall ctrl_set_mouse_in(ctrl_t *pDlg, ctrl_t *pCtrl) {
  dlgevt_t evt;
  ctrl_clear_mouse_in(pDlg);
  if (pCtrl == pDlg)
    return;
  pCtrl->dwFlags |= 0x80;
  evt.dwId = 9;
  evt.wTag = 0x0E;
  evt.dwParam = (ULONG_PTR)pDlg->pMouseIn;
  evt.nX = (short)gCursorPos_g[0];
  evt.nY = (short)gCursorPos_g[1];
  pCtrl->pfnEvt(pCtrl, &evt);
  pDlg->pMouseIn = pCtrl;
  if (pCtrl->pData)
    ctrl_set_update(pCtrl);
}

// Moves the keyboard focus to pCtrl. It asks the control first (event 5); on agreement it
// publishes the focus through the inlined ctrl_dlg_set_current, tells the old focus it lost
// (event 6) and repaints both, then re-picks the dialog's default push button. pParent is
// read without the usual dialog guard, so a node reaching here is always a control. A
// control that declines focus gets no repaint and no defpushbutton update.
void __fastcall ctrl_set_tabstop(ctrl_t *pCtrl) {
  dlgevt_t evt;
  ctrl_t *pDlg = pCtrl->pParent;
  ctrl_t *pOld = pDlg->pFocus;
  PFN_CTRL_EVT fn;
  if (pOld == pCtrl)
    return;
  evt.dwId = 5;
  evt.wTag = 0x0E;
  evt.nX = (short)gCursorPos_g[0];
  evt.nY = (short)gCursorPos_g[1];
  evt.dwParam = 0;
  if (!pCtrl->pfnEvt(pCtrl, &evt))
    return;
  ctrl_dlg_set_current(pDlg, pCtrl);
  if (pOld) {
    // dwId last here, unlike the first event: the oracle stores +04, +0C, +0E,
    // +10 and only then +00, and the id-first spelling transposes those two runs.
    evt.dwParam = 0;
    evt.wTag = 0x0E;
    evt.nX = (short)gCursorPos_g[0];
    evt.nY = (short)gCursorPos_g[1];
    fn = pOld->pfnEvt;   // before the last store -- see ctrl_send_cmd's note
    evt.dwId = 6;
    fn(pOld, &evt);
    ctrl_set_update(pOld);
  }
  ctrl_set_update(pCtrl);
  ctrl_update_defpushbutton(pDlg);
}

void __fastcall ctrl_set_text(ctrl_t *pCtrl, void *pText) {
  pCtrl->pData = pText;
  ctrl_set_update(pCtrl);
}

// ctrl_set_update -- the redraw primitive the whole family inlines, at ten call
// sites, nine of them above this definition.
void __fastcall ctrl_set_update(ctrl_t *pCtrl) {
  dlg_dword_t dwFlags = pCtrl->dwFlags;
  if (!(dwFlags & 1)) {
    pCtrl->dwFlags = dwFlags | 1;
    dlg_area_invalid(pCtrl);
  }
}

// ctrl_set_val -- event 0x0B. Unlike ctrl_send_msg this one does not stamp the
// cursor position, so nX and nY hold stack garbage. Reproduced, not repaired.
void __fastcall ctrl_set_val(ctrl_t *pCtrl, dlg_dword_t dwValue) {
  dlgevt_t evt;
  evt.dwId = 0x0B;
  evt.dwParam = dwValue;
  evt.wTag = 0x0E;
  pCtrl->pfnEvt(pCtrl, &evt);
}

void __fastcall ctrl_show(ctrl_t *pCtrl) {
  dlgevt_t evt;
  if (pCtrl->dwFlags & 8)
    return;
  evt.dwId = 0x0D;
  evt.wTag = 0x0E;
  evt.nX = (short)gCursorPos_g[0];
  evt.nY = (short)gCursorPos_g[1];
  evt.dwParam = 0;
  if (pCtrl->pfnEvt(pCtrl, &evt))
    ctrl_set_update(pCtrl);
}

void __fastcall ctrl_showhide(ctrl_t *pCtrl, int bShow) {
  if (bShow)
    ctrl_show(pCtrl);
  else
    ctrl_hide(pCtrl);
}

void __fastcall ctrl_ungray(ctrl_t *pCtrl) {
  dlg_dword_t dwFlags = pCtrl->dwFlags;
  if (!(dwFlags & 2))
    return;
  pCtrl->dwFlags = dwFlags & ~2u;
  ctrl_activate(pCtrl->pParent);
  ctrl_set_update(pCtrl);
}

void __fastcall ctrl_update_defpushbutton(ctrl_t *pDlg) {
  ctrl_t *pFocus = pDlg->pFocus;
  ctrl_t *pCtrl;
  if (pFocus && pFocus->wType == 1)
    return;
  pCtrl = pDlg->pChildren;
  while (pCtrl) {
    dlg_word_t wType = pCtrl->wType;
    ctrl_t *pNext;
    if (wType == 0)
      pNext = pCtrl->pChildren;
    else
      pNext = pCtrl->pNext;
    if (pCtrl->wType == 1) {
      dlg_dword_t dwFlags = pCtrl->dwFlags;
      if ((dwFlags & 8) && !(dwFlags & 2)) {
        gpChainCtrl = pCtrl;
        ctrl_set_update(pCtrl);
        return;
      }
    }
    pCtrl = pNext;
  }
}

int __fastcall cursor_in_dlg(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  // The local has to be a point16_t, not two shorts, and it has to be filled x
  // before y. Both halves are visible in the oracle: `mov ax, word ptr [edx+10h]`
  // and `mov si, word ptr [edx+0Eh]` are 16-bit register-variable moves, where
  // two plain `short` locals compile to `movzx` pairs of the same length and
  // different bytes, and assigning y first would let MSVC put x back in EDX and
  // lose the `push esi`, giving 41 bytes against the oracle's 44.
  point16_t pt;
  pt.x = pEvent->nX;
  pt.y = pEvent->nY;
  if (pt.x >= pCtrl->rc.left && pt.x <= pCtrl->rc.right &&
      pt.y >= pCtrl->rc.top && pt.y <= pCtrl->rc.bottom)
    return 1;
  return 0;
}

void __fastcall global_evt_2_local_point(ctrl_t *pCtrl, dlgevt_t *pEvent,
                                         point16_t *pPoint) {
  if (pCtrl->wType != 0)
    pCtrl = pCtrl->pParent;
  pPoint->x = (short)(pEvent->nX - pCtrl->rc.left);
  pPoint->y = (short)(pEvent->nY - pCtrl->rc.top);
}

int __fastcall pt_in_rect(const rect16_t *pRect, const point16_t *pPoint) {
  if (pPoint->x >= pRect->left && pPoint->x <= pRect->right &&
      pPoint->y >= pRect->top && pPoint->y <= pRect->bottom)
    return 1;
  return 0;
}
