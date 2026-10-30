// spd_dlg.cpp -- the game-speed options dialog, oracle 0x49F060-0x49F410: the
// speed and scroll-rate bars, the CPU-throttle check box, and the registry
// round trip that persists the throttle across runs.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/spd_dlg.h"
#include "../h/dlg_btn.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/dlg_lbox.h"
#include "../h/minimap.h"
#include "../h/net_cmds.h"
#include "../h/netdlg.h"
#include "../h/opt_dlgs.h"
#include "../h/replay.h"
#include "../h/save_dlg.h"
#include "../gen/globals117.h"

// init_spd_opts, which init.cpp also declares; its body is below.
void init_spd_opts();

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x6AAA64 isCPUThrottleEnabled and 0x6AAA68 sgbCPUThrottleTmp, both DWORDs
// despite the `sgb` prefix.
#ifdef OC_DLL_MODE
  #ifndef isCPUThrottleEnabled
  #define isCPUThrottleEnabled (*reinterpret_cast<DWORD *>(OC_VA(0x6AAA64u)))
  #endif
#else
extern DWORD isCPUThrottleEnabled;
#endif

#ifdef OC_DLL_MODE
  #ifndef sgbCPUThrottleTmp
  #define sgbCPUThrottleTmp (*reinterpret_cast<DWORD *>(OC_VA(0x6AAA68u)))
  #endif
#else
extern DWORD sgbCPUThrottleTmp;
#endif

// 0x671A81 gbDlgLevel, a BYTE.
#ifdef OC_DLL_MODE
  #ifndef gbDlgLevel
  #define gbDlgLevel (*reinterpret_cast<BYTE *>(OC_VA(0x671A81u)))
  #endif
#else
extern BYTE gbDlgLevel;
#endif

// 0x6CE6DC gnDlgReturnVal, a signed long.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

// 0x672664 gbMultiPlayerMode, a BYTE.
#ifdef OC_DLL_MODE
  #ifndef gbMultiPlayerMode
  #define gbMultiPlayerMode (*reinterpret_cast<BYTE *>(OC_VA(0x672664u)))
  #endif
#else
extern BYTE gbMultiPlayerMode;
#endif

// 0x67CC90 gPlayback, a DWORD.
#ifdef OC_DLL_MODE
  #ifndef gPlayback
  #define gPlayback (*reinterpret_cast<DWORD *>(OC_VA(0x67CC90u)))
  #endif
#else
extern DWORD gPlayback;
#endif

// 0x6D3130 gOptions -- the base dword is the game speed, which the index-1 bar
// shows.
#ifdef OC_DLL_MODE
  #ifndef gOptions
  #define gOptions (*reinterpret_cast<DWORD *>(OC_VA(0x6D3130u)))
  #endif
#else
extern DWORD gOptions;
#endif

// gOptions + 0x04 / + 0x08 and + 0x1C / + 0x20, the two scroll rates in their
// single-player and multiplayer copies (gbMultiPlayerMode picks the pair),
// signed, and separate objects as snd.cpp and gamesnd.cpp spell them.
#ifdef OC_DLL_MODE
  #ifndef dword_6D3134
  #define dword_6D3134 (*reinterpret_cast<int *>(OC_VA(0x6D3134u)))
  #endif
#else
extern int dword_6D3134;
#endif

#ifdef OC_DLL_MODE
  #ifndef dword_6D3138
  #define dword_6D3138 (*reinterpret_cast<int *>(OC_VA(0x6D3138u)))
  #endif
#else
extern int dword_6D3138;
#endif

#ifdef OC_DLL_MODE
  #ifndef dword_6D314C
  #define dword_6D314C (*reinterpret_cast<int *>(OC_VA(0x6D314Cu)))
  #endif
#else
extern int dword_6D314C;
#endif

#ifdef OC_DLL_MODE
  #ifndef dword_6D3150
  #define dword_6D3150 (*reinterpret_cast<int *>(OC_VA(0x6D3150u)))
  #endif
#else
extern int dword_6D3150;
#endif

// 0x4EFE58, the dialog's .bin path "rez\spd_dlg.bin", 16 bytes.
#ifdef OC_DLL_MODE
  #ifndef sgszSpdDlgFile_g
  #define sgszSpdDlgFile_g (*reinterpret_cast<char (*)[16]>(OC_VA(0x4EFE58u)))
  #endif
#else
extern char sgszSpdDlgFile_g[16] OC_UNALIGNED;
#endif

// 0x5093F0 sgSpdEvtFcnTbl, the dialog's four event handlers.
#ifdef OC_DLL_MODE
  #ifndef sgSpdEvtFcnTbl
  #define sgSpdEvtFcnTbl (*reinterpret_cast<PFN_CTRL_EVT (*)[4]>(OC_VA(0x5093F0u)))
  #endif
#else
extern PFN_CTRL_EVT sgSpdEvtFcnTbl[4];
#endif

// ctrl_t +0x26 read as a node pointer, and +0x36 as the callback the scroll bar
// runs through its parent. Both fall inside dialog.h's unnamed windows.
typedef void(__fastcall *PFN_CTRL_NOTIFY)(ctrl_t *pCtrl);

static __forceinline ctrl_t *&ctrl_p26(ctrl_t *p) {
  return *reinterpret_cast<ctrl_t **>(&p->_unmapped24[2]);
}

static __forceinline PFN_CTRL_NOTIFY &ctrl_p36(ctrl_t *p) {
  return *reinterpret_cast<PFN_CTRL_NOTIFY *>(&p->_unmapped36[0]);
}

// The bodies, in oracle VA order.

// Bank the throttle flag so Cancel can restore it, then bring the dialog up.
void do_speed_dlg() {
  sgbCPUThrottleTmp = isCPUThrottleEnabled;
  modeless_dlg_startup(sgszSpdDlgFile_g, (void *)&speed_dlg_fcn);
}

// OK broadcasts the new game speed unless the bar is greyed; anything else puts
// the banked throttle flag back. Then commit and drop a dialog level.
void speed_dlg_cmd(ctrl_t *pDlg) {
  if (gnDlgReturnVal == -2) {
    ctrl_t *pScroll = ctrl_find_ndx(pDlg, 1);

    if (!(pScroll->dwFlags & 2))
      send_net_cmd_SetSpeed(scroll_get_pos(pScroll));
  } else {
    isCPUThrottleEnabled = sgbCPUThrottleTmp;
  }
  option_dlg_cmd(pDlg);
  gbDlgLevel--;
  if (gbDlgLevel != 0) {
    gbDlgLevel--;
    do_optsmenu_dlg();
  } else {
    dlg_deactivate(pDlg);
  }
}

// The speed dialog's proc; the ids reach a two-level jump table over 0..10. It
// shares the sound dialog's activation.
BOOL speed_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 10:
        ctrl_set_evt_fcns(pCtrl, sgSpdEvtFcnTbl, 0x10);
        break;
      case 0:
        snd_dlg_activate(pCtrl);
        break;
      case 2:
        speed_dlg_cmd(pCtrl);
        return TRUE;
      case 1:
        opt_dlg_generic_close(pCtrl);
        modeless_opt_dlg(pCtrl, pEvent);
        return TRUE;
    }
  }
  return modeless_opt_dlg(pCtrl, pEvent);
}

// Read the persisted CPU-throttle flag at startup and register the writer.
void init_spd_opts() {
  DWORD dwValue;

  app_add_exit(reinterpret_cast<void(__fastcall *)()>(exit_spd_opts));
  if (SRegLoadValue("Starcraft", "CPUThrottle", 0, &dwValue))
    isCPUThrottleEnabled = (dwValue != 0);
}

// Persist the CPU-throttle flag, but not on the abnormal exit.
void exit_spd_opts(BYTE bArg0) {
  if (bArg0 == 0)
    SRegSaveValue("Starcraft", "CPUThrottle", 0, isCPUThrottleEnabled);
}

// A speed or scroll-rate bar. Event 10 marks the dialog for redraw, event 1
// cancels the game-speed bar's pending timer, and event 0 seeds the bar.
void speed_scrollbar_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        speed_scrollbar_init(pCtrl);
        break;
      case 1:
        if (pCtrl->nIndex == 1)
          timer_remove_node(pCtrl, 1);
        break;
      case 10:
        pCtrl->dwFlags |= 0x2000000;
        break;
    }
  }
  scroll_ctrl_scroll(pCtrl, pEvent);
}

// Seed one of the three bars over the 0..6 range, then give it the parent and
// the callback the commit runs through. The compiler duplicates that tail into
// each arm of the two scroll-rate selections.
void speed_scrollbar_init(ctrl_t *pCtrl) {
  switch (pCtrl->nIndex) {
    case 1:
      scroll_set_range(pCtrl, 0, 6);
      scroll_set_page_size(pCtrl, 1, 1);
      ctrl_set_val(pCtrl, gPlayback != 0 ? replay_get_initial_game_speed()
                                         : gOptions);
      if (gbMultiPlayerMode != 0 || gPlayback != 0)
        ctrl_gray(pCtrl);
      break;
    case 2:
      scroll_set_range(pCtrl, 0, 6);
      scroll_set_page_size(pCtrl, 1, 1);
      if (gbMultiPlayerMode != 0)
        ctrl_set_val(pCtrl, dword_6D314C);
      else
        ctrl_set_val(pCtrl, dword_6D3134);
      break;
    case 3:
      scroll_set_range(pCtrl, 0, 6);
      scroll_set_page_size(pCtrl, 1, 1);
      if (gbMultiPlayerMode != 0)
        ctrl_set_val(pCtrl, dword_6D3150);
      else
        ctrl_set_val(pCtrl, dword_6D3138);
      break;
    default:
      break;
  }
  ctrl_t *pParent = pCtrl->pParent;

  ctrl_p36(pCtrl) = speed_set_options;
  ctrl_p26(pCtrl) = pParent;
}

// Bank both scroll rates into whichever pair the current mode owns.
void speed_set_options(ctrl_t *pDlg) {
  int nRate = scroll_get_pos(ctrl_find_ndx(pDlg, 2));

  if (gbMultiPlayerMode != 0)
    dword_6D314C = nRate;
  else
    dword_6D3134 = nRate;

  nRate = scroll_get_pos(ctrl_find_ndx(pDlg, 3));
  if (gbMultiPlayerMode != 0)
    dword_6D3150 = nRate;
  else
    dword_6D3138 = nRate;
}

// The CPU-throttle check box. Event 2 toggles the flag; event 10 seeds the box.
BOOL speed_checkbox_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 2:
        isCPUThrottleEnabled = (isCPUThrottleEnabled == 0);
        break;
      case 10:
        ctrl_set_val(pCtrl, isCPUThrottleEnabled != 0);
        break;
    }
  }
  return btn_ctrl_checkbox(pCtrl, pEvent);
}
