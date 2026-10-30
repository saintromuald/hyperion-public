// menu_dlg.cpp -- the menu dialog's button handlers and title placement.
// Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers on some declarations are for, and why moving a
// body between TUs can change the code generated.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/menu_dlg.h"
#include "../h/dlg_ctrl.h"   // ctrl_evt, ctrl_find_ndx
#include "../h/minimap.h"    // btn_ctrl_pushbutton
#include "../gen/globals117.h"
#include "../port/render/port_view.h"  // Hyperion: g_portView

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// 0x508BA0 sgwBtnFrames -- the button frame indices, WORD elements: every read
// is `mov ax, word ptr [<idx>*2+508BA0h]`, and the scale of 2 fixes the width.
// The extent is the gap to the next named global, sgMinimapArea_g at 0x508BB0,
// so 0x10 bytes and 8 entries; the highest index any caller passes is 5.
#ifndef OC_SGW_BTN_FRAMES
#define OC_SGW_BTN_FRAMES
#ifdef OC_DLL_MODE
#define sgwBtnFrames (*reinterpret_cast<WORD (*)[8]>(OC_VA(0x508BA0u)))
#else
extern WORD sgwBtnFrames[8];
#endif
#endif

// 0x671A81 gbDlgLevel -- the nesting level of the dialog stack, a BYTE: the
// only access in this TU is `mov byte ptr [0x671A81], 0h`.
#ifndef OC_GB_DLG_LEVEL
#define OC_GB_DLG_LEVEL
#ifdef OC_DLL_MODE
#define gbDlgLevel (*reinterpret_cast<BYTE *>(OC_VA(0x671A81u)))
#else
extern BYTE gbDlgLevel;
#endif
#endif

// 0x6CE6DC gnDlgReturnVal -- the modal result, a signed long: the store here is
// a full 32-bit one fed by a sign-extending load.
#ifndef OC_GN_DLG_RETURN_VAL
#define OC_GN_DLG_RETURN_VAL
#ifdef OC_DLL_MODE
#define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
#else
extern LONG gnDlgReturnVal;
#endif
#endif

// ctrl_t +0x24, a WORD inside dialog.h's _unmapped24 gap: the button frame
// index each helper here copies out of sgwBtnFrames. Named locally, since it is
// one control type's arm of that per-type payload.
static dlg_word_t &ctrl_frame(ctrl_t *pCtrl)
{
  return *reinterpret_cast<dlg_word_t *>(&pCtrl->_unmapped24[0]);
}

// The bodies, in oracle VA order.

void dlg_level_init()
{
  gbDlgLevel = 0;
}

// An activate event publishes the control's index as the modal result and
// re-raises the event on the owning dialog; everything else falls through to
// the stock push button.
BOOL menu_btn_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 2:
      gnDlgReturnVal = pCtrl->nIndex;
      ctrl_evt(pCtrl->pParent, pEvent);
      return TRUE;
    }
  }
  return btn_ctrl_pushbutton(pCtrl, pEvent);
}

// The create event swaps the control over to the big frame and to
// menu_btn_fcn, which the oracle then inlines rather than calls.
BOOL big_menu_btn_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E && pEvent->dwId == 0) {
    ctrl_frame(pCtrl) = sgwBtnFrames[4];
    pCtrl->dwFlags |= 0x20000000;
    pCtrl->pfnEvt = menu_btn_fcn;
  }
  return menu_btn_fcn(pCtrl, pEvent);
}

void set_btn_size(ctrl_t *pCtrl, WORD wFrame, DWORD dwFlags)
{
  ctrl_frame(pCtrl) = sgwBtnFrames[wFrame];
  pCtrl->dwFlags |= dwFlags;
}

void set_size_enter(ctrl_t *pDlg, WORD wFrame, DWORD dwFlags)
{
  ctrl_t *pCtrl = ctrl_find_ndx(pDlg, -2);
  if (pCtrl != NULL) {
    set_btn_size(pCtrl, wFrame, dwFlags);
  }
}

void set_size_escape(ctrl_t *pDlg, WORD wFrame, DWORD dwFlags)
{
  ctrl_t *pCtrl = ctrl_find_ndx(pDlg, -3);
  if (pCtrl != NULL) {
    set_btn_size(pCtrl, wFrame, dwFlags);
  }
}

// The dialog's rect holds its size, not its place, until this runs: right and
// bottom are read as width and height and the result centres them in 640x480.
// Hyperion: centred in the view's screen (port_view.cpp).
void title_position(ctrl_t *pDlg)
{
  if (gwGameMode == 4) {
    pDlg->rc.left = static_cast<short>((g_portView.nWidth - pDlg->rc.right) / 2);
    pDlg->rc.top = static_cast<short>((g_portView.nHeight - pDlg->rc.bottom) / 2);
  }
}
