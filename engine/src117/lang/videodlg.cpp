// videodlg.cpp -- the video options dialog, the three control handlers it
// seats, and the pair that reads the settings out of the registry at startup
// and writes them back on the way out. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/videodlg.h"
#include "../h/dialog.h"
#include "../h/dlg_btn.h"      // btn_ctrl_checkbox
#include "../h/dlg_ctrl.h"     // ctrl_find_ndx, ctrl_set_val
#include "../h/dlg_dlg.h"      // dlg_deactivate
#include "../h/dlg_lbox.h"     // scroll_ctrl_scroll
#include "../h/gds_palette.h"  // vid_palette_to_monitor
#include "../h/gluScore.h"     // btn_ctrl_radiobutton
#include "../h/netdlg.h"      // snd_dlg_activate
#include "../h/opt_dlgs.h"     // do_video_dlg, option_dlg_cmd
#include "../h/save_dlg.h"     // modeless_dlg_startup, modeless_opt_dlg
#include "../h/snd_dlg.h"      // set_update_timer
#include "../h/spd_dlg.h"      // SRegLoadValue, SRegSaveValue
#include "../h/statport.h"     // get_playback_option, set_playback_option
#include "../h/gds_vidinimo.h"
#include "../gen/globals117.h"

// Callees no header of ours declares.

// 0x4B8E80, a nullary function that returns 0: the linker folded every
// predicate that always answers false onto it.
int __fastcall nullsub___shared_();

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x50E138 gbColorCycle_g, a DWORD.
#ifdef OC_DLL_MODE
  #ifndef gbColorCycle_g
  #define gbColorCycle_g (*reinterpret_cast<DWORD *>(OC_VA(0x50E138u)))
  #endif
#else
extern DWORD gbColorCycle_g;
#endif

// 0x50E13C sVideoDlgEvtFcnTbl_g, the dialog's five event handlers.
#ifdef OC_DLL_MODE
  #ifndef sVideoDlgEvtFcnTbl_g
  #define sVideoDlgEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x50E13Cu)))
  #endif
#else
extern PFN_CTRL_EVT sVideoDlgEvtFcnTbl_g[5];
#endif

// 0x6CA794 / 0x6CA798 / 0x6CA79C, the three settings do_video_dlg banks so
// Cancel can put them back, each a DWORD.
#ifdef OC_DLL_MODE
  #ifndef vidDlg_tmp_colorcycle
  #define vidDlg_tmp_colorcycle (*reinterpret_cast<DWORD *>(OC_VA(0x6CA794u)))
  #endif
#else
extern DWORD vidDlg_tmp_colorcycle;
#endif

#ifdef OC_DLL_MODE
  #ifndef vidDlg_tmp_gamma
  #define vidDlg_tmp_gamma (*reinterpret_cast<DWORD *>(OC_VA(0x6CA798u)))
  #endif
#else
extern DWORD vidDlg_tmp_gamma;
#endif

#ifdef OC_DLL_MODE
  #ifndef vidDlg_tmp_portraits
  #define vidDlg_tmp_portraits (*reinterpret_cast<DWORD *>(OC_VA(0x6CA79Cu)))
  #endif
#else
extern DWORD vidDlg_tmp_portraits;
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

// ctrl_t +0x26 read as a node pointer and +0x36 as the callback the scroll bar
// runs through its parent, both inside dialog.h's unnamed windows and spelled
// as snd_dlg.cpp spells them.
typedef void(__fastcall *PFN_CTRL_NOTIFY)(ctrl_t *pCtrl);

static __forceinline ctrl_t *&ctrl_p26(ctrl_t *p) {
  return *reinterpret_cast<ctrl_t **>(&p->_unmapped24[2]);
}

static __forceinline PFN_CTRL_NOTIFY &ctrl_p36(ctrl_t *p) {
  return *reinterpret_cast<PFN_CTRL_NOTIFY *>(&p->_unmapped36[0]);
}

// Hyperion: the dialog's +0x26 slot holds option_dlg_alloc's block, an
// optdlg_save_t (opt_dlgs.cpp) with its countdown byte, bKind, at +0x26 on either
// width, where ctrl_t's +0x24 window is not at 64 bits.
static __forceinline BYTE &optdlg_kind(void *pSave) {
  return static_cast<BYTE *>(pSave)[0x26];
}

// The bodies, in oracle VA order.

// Bank the three settings Cancel has to restore, then bring the dialog up.
void do_video_dlg() {
  vidDlg_tmp_colorcycle = gbColorCycle_g;
  vidDlg_tmp_gamma = get_gamma();
  vidDlg_tmp_portraits = get_playback_option();
  modeless_dlg_startup((void *)"rez\\video.bin", (void *)&video_dlg_fcn);
}

// The colour-cycling check box.
int video_dlg_colorcycle_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 2:
        gbColorCycle_g = (gbColorCycle_g == 0);
        break;
      case 10:
        ctrl_set_val(pCtrl, gbColorCycle_g != 0);
        break;
    }
  }
  return btn_ctrl_checkbox(pCtrl, pEvent);
}

// Leave the dialog. Anything but OK puts the banked settings back first, then
// the dialog level drops by one, and by a second one when that did not reach
// zero, which sends the user back to the options menu.
void video_dlg_set_options(ctrl_t *pDlg) {
  if (gnDlgReturnVal != -2) {
    gbColorCycle_g = vidDlg_tmp_colorcycle;
    set_gamma(vidDlg_tmp_gamma);
    vid_palette_to_monitor();
    set_playback_option(vidDlg_tmp_portraits);
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

// The dialog's proc. Tag 0x0D is the redraw tick the gamma bar arms, and it
// pushes the new gamma into the palette on the tick it reaches zero; under tag
// 0x0E the ids reach a two-level jump table over 0..10.
int video_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  switch (pEvent->wTag) {
    case 0x0E:
      switch (pEvent->dwId) {
        case 10:
          ctrl_set_evt_fcns(pCtrl, sVideoDlgEvtFcnTbl_g, 0x14);
          break;
        case 0:
          snd_dlg_activate(pCtrl);
          break;
        case 2:
          video_dlg_set_options(pCtrl);
          return 1;
        case 1:
          opt_dlg_generic_close(pCtrl);
          modeless_opt_dlg(pCtrl, pEvent);
          return 1;
      }
      break;
    case 0x0D: {
      ctrl_t *pNode = ctrl_p26(pCtrl);

      if (optdlg_kind(pNode) != 0 && --optdlg_kind(pNode) == 0) {
        int nPos = scroll_get_pos(ctrl_find_ndx(pCtrl, 1));

        if (nPos != get_gamma()) {
          set_gamma(nPos);
          vid_palette_to_monitor();
        }
      }
      break;
    }
  }
  return modeless_opt_dlg(pCtrl, pEvent);
}

// Read the three settings out of the registry and register the writer. A value
// the registry does not hold takes its default, and the store happens either
// way.
void init_video_opts() {
  DWORD dwColorCycle;
  DWORD dwPortraits;
  DWORD dwGamma;

  app_add_exit(reinterpret_cast<void(__fastcall *)()>(exit_video_opts));
  if (SRegLoadValue("Starcraft", "Gamma", 0, &dwGamma))
    set_gamma(dwGamma);
  if (!SRegLoadValue("Starcraft", "ColorCycle", 0, &dwColorCycle))
    dwColorCycle = 1;
  gbColorCycle_g = (dwColorCycle != 0);
  if (!SRegLoadValue("Starcraft", "UnitPortraits", 0, &dwPortraits))
    dwPortraits = 2;
  set_playback_option(dwPortraits);
}

// The portraits radio group: three buttons whose control indices are the
// playback option biased by three.
int video_dlg_portraits_radio_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 2:
        set_playback_option(pCtrl->nIndex - 3);
        break;
      case 10:
        ctrl_set_val(pCtrl, pCtrl->nIndex == get_playback_option() + 3);
        break;
    }
  }
  return btn_ctrl_radiobutton(pCtrl, pEvent);
}

// Persist the three settings, but not on the abnormal exit.
void exit_video_opts(BYTE bSkipSave) {
  if (bSkipSave == 0) {
    SRegSaveValue("Starcraft", "Gamma", 0, get_gamma());
    SRegSaveValue("Starcraft", "ColorCycle", 0, gbColorCycle_g);
    SRegSaveValue("Starcraft", "UnitPortraits", 0, get_playback_option());
  }
}

// The gamma slider, seeded over 60..140 and given the parent and the callback
// the redraw tick runs through.
int video_dlg_gamma_slider_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0: {
        scroll_set_range(pCtrl, 0x3C, 0x8C);
        scroll_set_page_size(pCtrl, 10, 1);
        ctrl_set_val(pCtrl, get_gamma());
        ctrl_t *pParent = pCtrl->pParent;

        ctrl_p36(pCtrl) = set_update_timer;
        ctrl_p26(pCtrl) = pParent;
        break;
      }
      case 10:
        pCtrl->dwFlags |= 0x2000000;
        break;
    }
  }
  return scroll_ctrl_scroll(pCtrl, pEvent);
}
