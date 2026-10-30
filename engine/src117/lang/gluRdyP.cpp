// gluRdyP.cpp -- the Protoss ready room: the modal screen, its event handler,
// and the portrait frame table. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gluRdyP.h"
#include "../h/gluRdy.h"      // sub_460A70, glurdy_dlg_*
#include "../h/gluBrfPort.h"  // sub_4501F0
#include "../h/glues.h"       // glue_screen_activate, glues_transition_in
#include "../h/dlg_ctrl.h"    // ctrl_dlg
#include "../h/dlg_dlg.h"     // dlg_load, dlg_run_modal
#include "../h/credits.h"     // show_campaign_credits
#include "../h/snd.h"         // load_game_data
#include "../h/uiSingle.h"    // sub_4BB790
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x536720 -- the expansion gate, only ever compared against zero as a byte;
// spelled as gametype.cpp spells it.
#ifndef gbIsExpansion
#ifdef OC_DLL_MODE
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbIsExpansion;
#endif
#endif

// 0x508304 -- this screen's transition-animation descriptor, handed to
// glues_transition_in in EDX, as gluJoin and gluLoad hand theirs. Its extent
// is not known: 0x1C is the span to the control table below.
#ifdef OC_DLL_MODE
#define sgGluRdyPAnims_g (reinterpret_cast<BYTE *>(OC_VA(0x508304u)))
#else
extern BYTE sgGluRdyPAnims_g[0x1C] OC_UNALIGNED;
#endif

// 0x508320 -- the screen's control event table, 20 PFN_CTRL_EVT slots: the one
// call site passes 0x50 as glue_init_screen_dlg's cbTable.
#ifdef OC_DLL_MODE
#define sReadyPEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x508320u)))
#else
extern PFN_CTRL_EVT sReadyPEvtFcnTbl_g[20];
#endif

// The bodies, in oracle VA order.

// 0x4611E0 (211 B). The Protoss ready room. The switch's default arm clears
// glGluesMode and falls through into case 0x64's tail; a result out of the
// table's range lands there too, not on the early return.
void do_readyp_dlg()
{
  glurdy_set_music(0x0C);
  show_campaign_credits();

  if (gwGameMode != 4)
    return;

  glues_reset_prev_pal();

  ctrl_t *const pDlg = dlg_load("rez\\glurdyp.bin",
                                reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                                reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                                "gluRdyP.cpp", 0x9D);

  switch (dlg_run_modal(pDlg, 0, glurdyp_dlg_fcn)) {
    case 0x65:
      gwGameMode = 1;
      break;
    case 0x0E:
      if (gbMultiPlayerMode != 0) {
        set_netmode_return_menu();
        glurdy_set_menu_music();
        break;
      }
      glGluesMode = (gbIsExpansion != 0) ? 0x16 : 6;
      glurdy_set_menu_music();
      break;
    case 0x13:
      sub_4BB790(1);
      break;
    default:
      glGluesMode = 0;
      // falls through
    case 0x64:
      glurdy_set_menu_music();
      break;
  }

  glues_clear_all();
}

// 0x461320 (147 B). The screen's dialog handler. Every arm but case 2 goes on to
// ctrl_dlg; case 2 returns glurdy_dlg_command's result instead.
int glurdyp_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        glue_screen_activate(pCtrl);
        break;
      case 1:
        glurdy_dlg_deactivate(pCtrl);
        break;
      case 2:
        return glurdy_dlg_command(pCtrl);
      case 0x0A:
        glurdy_dlg_init(pCtrl);
        glues_transition_in(reinterpret_cast<intptr_t>(pCtrl), sgGluRdyPAnims_g,
                            7, 1, 0x50);
        glue_init_screen_dlg(pCtrl, sReadyPEvtFcnTbl_g, 0x50, 0);
        break;
      case 0x405:
        glurdyp_init_portrait_frames(pCtrl);
        break;
    }
  }
  return ctrl_dlg(pCtrl, pEvent);
}

// 0x4613C0 (82 B). Builds the eight frame paths on the stack and hands sub_4501F0
// the two halves. The dialog in ECX goes on to sub_460A70 unchanged.
void glurdyp_init_portrait_frames(ctrl_t *pDlg)
{
  const char *rgszFrames[8];

  rgszFrames[0] = "glue\\ReadyP\\PFrame1.pcx";
  rgszFrames[1] = "glue\\ReadyP\\PFrame2.pcx";
  rgszFrames[2] = "glue\\ReadyP\\PFrame3.pcx";
  rgszFrames[3] = "glue\\ReadyP\\PFrame4.pcx";
  rgszFrames[4] = "glue\\ReadyP\\PFrameH1.pcx";
  rgszFrames[5] = "glue\\ReadyP\\PFrameH2.pcx";
  rgszFrames[6] = "glue\\ReadyP\\PFrameH3.pcx";
  rgszFrames[7] = "glue\\ReadyP\\PFrameH4.pcx";

  sub_460A70(pDlg);
  sub_4501F0(reinterpret_cast<intptr_t>(&rgszFrames[0]),
             reinterpret_cast<intptr_t>(&rgszFrames[4]));
}
