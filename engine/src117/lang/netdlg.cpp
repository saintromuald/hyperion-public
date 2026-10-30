// netdlg.cpp -- the network-options dialog. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/netdlg.h"
#include "../h/dialog.h"    // ctrl_t, dlgevt_t, PFN_CTRL_EVT
#include "../h/dlg_ctrl.h"  // ctrl_set_val, ctrl_find_ndx, ctrl_set_evt_fcns
#include "../h/dlg_dlg.h"   // dlg_deactivate
#include "../h/net_mgr.h"   // net_get_latency, netmgr_set_latency
#include "../h/opt_dlgs.h"  // option_dlg_cmd, do_optsmenu_dlg, opt_dlg_generic_close
#include "../h/save_dlg.h"  // modeless_dlg_startup, modeless_opt_dlg
#include "../h/gluScore.h"  // btn_ctrl_radiobutton
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// 0x4EEF94 -- the dialog's .bin path, "rez\netdlg.bin", which do_net_dlg hands
// modeless_dlg_startup as ECX. Extent 15 from the terminating NUL at 0x4EEFA2.
#ifndef OC_SGSZ_NET_DLG_FILE_G
#define OC_SGSZ_NET_DLG_FILE_G
#ifdef OC_DLL_MODE
#define sgszNetDlgFile_g (*reinterpret_cast<char (*)[15]>(OC_VA(0x4EEF94u)))
#else
extern char sgszNetDlgFile_g[15];
#endif
#endif

// 0x508D20. All three slots hold net_dlg_radio_fcn; net_dlg_fcn passes the
// table's size, 0x0C, to ctrl_set_evt_fcns.
#ifndef OC_S_NET_DLG_EVT_FCN_TBL_G
#define OC_S_NET_DLG_EVT_FCN_TBL_G
#ifdef OC_DLL_MODE
#define sNetDlgEvtFcnTbl_g (*reinterpret_cast<PFN_CTRL_EVT (*)[3]>(OC_VA(0x508D20u)))
#else
extern PFN_CTRL_EVT sNetDlgEvtFcnTbl_g[3];
#endif
#endif

// 0x671A81, a BYTE.
#ifndef OC_GB_DLG_LEVEL
#define OC_GB_DLG_LEVEL
#ifdef OC_DLL_MODE
#define gbDlgLevel (*reinterpret_cast<BYTE *>(OC_VA(0x671A81u)))
#else
extern BYTE gbDlgLevel;
#endif
#endif

// 0x6CE6DC, a long.
#ifndef OC_GN_DLG_RETURN_VAL
#define OC_GN_DLG_RETURN_VAL
#ifdef OC_DLL_MODE
#define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
#else
extern LONG gnDlgReturnVal;
#endif
#endif

// The DWORD at ctrl_t +0x36 is a radio button's check state. It is reached
// through the unmapped payload block rather than by naming the field, because
// +0x36 means something different for every control type.
#define radio_value(p) (*reinterpret_cast<dlg_dword_t *>(&(p)->_unmapped36[0]))

// The bodies, in oracle VA order.

// Bring the network-options dialog up modeless.
void do_net_dlg()
{
  modeless_dlg_startup(sgszNetDlgFile_g, (void *)&net_dlg_fcn);
}

// Bank the latency the radio group is showing, then run the shared options
// teardown. Only -2 (OK) commits: the three buttons carry indices 1..3 and the
// checked one names the latency, so the loop counter is the setting itself.
// The counter reaches netmgr_set_latency as a full `mov ecx, esi`; a BYTE
// argument would force it into a byte-addressable register.
void net_dlg_cmd(ctrl_t *pDlg)
{
  if (gnDlgReturnVal == -2) {
    for (unsigned nLatency = 0; nLatency <= 2; ++nLatency) {
      if (radio_value(ctrl_find_ndx(pDlg, (short)(nLatency + 1))) != 0)
        netmgr_set_latency(nLatency);
    }
  }
  option_dlg_cmd(pDlg);
  if (--gbDlgLevel != 0) {
    --gbDlgLevel;
    do_optsmenu_dlg();
  } else {
    dlg_deactivate(pDlg);
  }
}

// The dialog's event handler, the jump-table shape msgfltr_dlg_fcn shares, with
// the radio handler table installed inline on id 10. Cases 10 and 0 break into
// the shared tail jmp; cases 2 and 1 return 1 instead.
BOOL net_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 10:
        ctrl_set_evt_fcns(pCtrl, sNetDlgEvtFcnTbl_g, 0x0C);
        break;
      case 0:
        snd_dlg_activate(pCtrl);
        break;
      case 2:
        net_dlg_cmd(pCtrl);
        return TRUE;
      case 1:
        opt_dlg_generic_close(pCtrl);
        modeless_opt_dlg(pCtrl, pEvent);
        return TRUE;
    }
  }
  return modeless_opt_dlg(pCtrl, pEvent);
}

// The handler all three latency radio buttons share. Id 10 seeds the button
// from the current setting; everything else goes straight to the generic radio
// behaviour, which the tail jmp keeps.
BOOL net_dlg_radio_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E && pEvent->dwId == 0x0A)
    net_dlg_radio_init(pCtrl);
  return btn_ctrl_radiobutton(pCtrl, pEvent);
}

// Three radio buttons share this initialiser and tell themselves apart by their
// control index, so the latency is fetched once and each button is checked only
// if the setting equals its own position. Index 0 is not a latency button and
// falls through without touching the control.
void net_dlg_radio_init(ctrl_t *pCtrl)
{
  int nLatency = net_get_latency();
  if (pCtrl->nIndex == 1)
    ctrl_set_val(pCtrl, nLatency == 0);
  else if (pCtrl->nIndex == 2)
    ctrl_set_val(pCtrl, nLatency == 1);
  else if (pCtrl->nIndex == 3)
    ctrl_set_val(pCtrl, nLatency == 2);
}
