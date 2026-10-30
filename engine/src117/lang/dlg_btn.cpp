// dlg_btn.cpp -- the check box, radio button and push button control handlers.
// Bodies are in oracle VA order.
//
// All three entry points share one shape: refuse anything whose wTag is not
// 0x0E, switch on the event id, and tail-jump to btn_generic for everything
// they do not handle themselves. btn_generic carries the common button
// behaviour and its own two-level jump table over wTag 3..14. The pressed bit
// is dwFlags & 0x40000000 and the check value is the DWORD at +0x36, this
// control type's arm of the per-type payload dialog.h leaves unnamed.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle never does across a TU; the __declspec(noinline) markers prevent it.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>

#include "../h/dlg_btn.h"
#include "../h/dialog.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/minimap.h"
#include "../h/gluScore.h"
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// Two of the 30 theme metrics dlg_stats_init publishes, words even though the
// run is spaced four bytes apart.
#ifdef OC_DLL_MODE
#define gwRadioBtnSize (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE6ECu)))
#define gwCheckBoxSize (*reinterpret_cast<dlg_word_t*>(OC_VA(0x6CE6F0u)))
#else
extern dlg_word_t gwRadioBtnSize;
extern dlg_word_t gwCheckBoxSize;
#endif

// The modal result dlg_run_modal returns, signed: the sentinels are negative.
#ifdef OC_DLL_MODE
#define gnDlgReturnVal (*reinterpret_cast<LONG*>(OC_VA(0x6CE6DCu)))
#else
extern LONG gnDlgReturnVal;
#endif

// The DWORD at +0x36 is the check state, read and written by every body here
// (`mov dword ptr [esi+36h], eax`). Reached through the unnamed payload window
// rather than by naming the field, because +0x36 means something different for
// every control type.
#define btn_value(p) (*reinterpret_cast<dlg_dword_t *>(&(p)->_unmapped36[0]))

// The WORD at +0x24 the check box publishes as its own frame index.
#define btn_frame(p) (*reinterpret_cast<dlg_word_t *>(&(p)->_unmapped24[0]))

#define BTN_PRESSED 0x40000000u

// The bodies, in oracle VA order.

// 0x4C7440 (96 B). Tracks the pressed bit against where the mouse actually is:
// entering the rect presses and plays the click, leaving it releases silently.
// The flag is read into a local before the locate call, which is what keeps it
// in a callee-saved register across both.
void btn_check_state(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  point16_t pt;
  dlg_dword_t dwPressed = pCtrl->dwFlags & BTN_PRESSED;

  global_evt_2_local_point(pCtrl, pEvent, &pt);
  if (pt_in_rect(&pCtrl->rc, &pt) != 0) {
    if (dwPressed == 0) {
      snd_play_btn(0);
      pCtrl->dwFlags ^= BTN_PRESSED;
      ctrl_set_update(pCtrl);
    }
  } else if (dwPressed != 0) {
    pCtrl->dwFlags ^= BTN_PRESSED;
    ctrl_set_update(pCtrl);
  }
}

// 0x4C74A0 (95 B). Event 2 toggles the check value and event 11 sets it; event
// 10 is the measure pass: widen the node's bottom to fit the theme's check box,
// publish frame 0x0B, then let btn_generic finish. Case order sets block order.
int btn_ctrl_checkbox(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 11:
      btn_value(pCtrl) = (pEvent->dwParam != 0);
      return 1;
    case 10: {
      int nBottom;

      btn_frame(pCtrl) = 0x0B;
      nBottom = pCtrl->rc.top + gwCheckBoxSize;
      if (pCtrl->rc.bottom > nBottom)
        nBottom = pCtrl->rc.bottom;
      pCtrl->rc.bottom = static_cast<short>(nBottom);
      break;
    }
    case 2:
      btn_value(pCtrl) ^= 1;
      return 1;
    }
  }
  return btn_generic(pCtrl, pEvent);
}

// 0x4C7500 (64 B). A push button's only own behaviour is to publish its index
// as the modal result when that index is one of the two negative sentinels.
// An index that is not a sentinel falls through to btn_generic rather than
// reporting the event handled.
int btn_ctrl_pushbutton(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 2:
      if (pCtrl->nIndex == -3 || pCtrl->nIndex == -2) {
        gnDlgReturnVal = pCtrl->nIndex;
        ctrl_evt(pCtrl->pParent, pEvent);
        return 1;
      }
      break;
    }
  }
  return btn_generic(pCtrl, pEvent);
}

// 0x4C7540 (76 B). Events 2 and 11 (the latter only when set) hand off to
// set_radios, which clears the siblings; event 10 is the measure pass.
int btn_ctrl_radiobutton(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 11:
      if (pEvent->dwParam != 0)
        set_radios(pCtrl);
      return 1;
    case 2:
      set_radios(pCtrl);
      return 1;
    case 10: {
      int nBottom = gwRadioBtnSize + pCtrl->rc.top;

      if (pCtrl->rc.bottom > nBottom)
        nBottom = pCtrl->rc.bottom;
      pCtrl->rc.bottom = static_cast<short>(nBottom);
      break;
    }
    }
  }
  return btn_generic(pCtrl, pEvent);
}

// 0x4C7590 (172 B). The behaviour every button type shares, dispatched through
// a two-level jump table over wTag 3..14 (read off 0x4C763C / 0x4C7650): event
// 0x0E id 3 is the press toggle, and wTag 3 shares the tail of 4 and 6.
int btn_generic(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  switch (pEvent->wTag) {
  case 0x0E:
    switch (pEvent->dwId) {
    case 3:
      ctrl_set_update(pCtrl);
      if (pCtrl->dwFlags & BTN_PRESSED) {
        btn_unpress(pCtrl);
        return 1;
      }
      dlg_register_timeout(pCtrl);
      pCtrl->dwFlags ^= BTN_PRESSED;
      return 1;
    case 6:
      pCtrl->dwFlags &= ~BTN_PRESSED;
      break;
    }
    return ctrl_evn_dlg(pCtrl, pEvent);

  case 4:
  case 6:
    dlg_hook(pCtrl, 5);
    dlg_hook(pCtrl, 3);
    btn_check_state(pCtrl, pEvent);
    return 1;
  case 3:
    btn_check_state(pCtrl, pEvent);
    return 1;
  case 5:
    btn_check_state(pCtrl, pEvent);
    ctrl_check_mouse_in(pCtrl->pParent, pEvent);
    btn_unpress(pCtrl);
    return 1;
  }
  return 0;
}

// 0x4C7660 (60 B). Release: drop the pressed bit, tell the dialog, and play
// the up click as a tail call.
void btn_unpress(ctrl_t *pCtrl)
{
  dlg_unhook_all(pCtrl);
  if ((pCtrl->dwFlags & BTN_PRESSED) != 0) {
    pCtrl->dwFlags &= ~BTN_PRESSED;
    ctrl_set_update(pCtrl);
    ctrl_send_msg(pCtrl, 2, 0);
    snd_play_btn(2);
  }
}

// 0x4C76A0 (130 B). Clear the check value on every adjacent node of the same
// type in both index directions, then set our own. The walk stops at the first
// neighbour that is not a radio button.
void set_radios(ctrl_t *pCtrl)
{
  ctrl_t *pNode = pCtrl;
  ctrl_t *pFound;

  pFound = ctrl_find_ndx(pCtrl, static_cast<short>(pNode->nIndex - 1));
  while (pFound != 0 && pFound->wType == 3) {
    pNode = pFound;
    pFound = ctrl_find_ndx(pCtrl, static_cast<short>(pFound->nIndex - 1));
  }
  btn_value(pNode) = 0;
  ctrl_set_update(pNode);

  pNode = ctrl_find_ndx(pCtrl, static_cast<short>(pNode->nIndex + 1));
  while (pNode != 0 && pNode->wType == 3) {
    btn_value(pNode) = 0;
    ctrl_set_update(pNode);
    pNode = ctrl_find_ndx(pCtrl, static_cast<short>(pNode->nIndex + 1));
  }
  btn_value(pCtrl) = 1;
}
