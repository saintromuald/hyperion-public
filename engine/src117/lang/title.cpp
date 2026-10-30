// title.cpp -- the title screen and its loading dialog. Bodies are in oracle
// VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/palette.h"
#include "../h/title.h"
#include "../h/dialog.h"     // ctrl_t, dlgevt_t, rect16_t
#include "../h/bmp.h"        // bmp_t, bmp_destroy
#include "../h/dlg_ctrl.h"   // ctrl_find_ndx, ctrl_show, ctrl_set_update, ctrl_dlg
#include "../h/dlg_dlg.h"    // dlg_load, dlg_activate, dlg_deactivate
#include "../h/dlg_draw.h"   // font_set, font_set_color_tbl
#include "../h/gluRdy.h"     // bmp_create, snd_play_music
#include "../h/glues.h"      // alloc_load_bmp, vid_fade_in/out, close_title_dlg
#include "../h/minimap.h"    // timer_create, timer_remove_node, PFN_TIMER_FN
#include "../h/net_cmds.h"   // vid_restore_palette
#include "../h/snd.h"        // load_game_data
#include "../h/TrigAct.h"    // cur_hide
#include "../h/gds_bmpcop.h"
#include "../gen/globals117.h"
#include "../port/render/port_storm.h"  // Hyperion: PortBltRop3
#include "../port/data/port_remastered.h"  // Hyperion: PortGameSource

// Callees no header of ours declares.

// Storm's SBltROP3, __stdcall, spelled exactly as dlg_effects.cpp spells it.
extern "C" int __stdcall SBltROP3(void *pDest, void *pSrc, int nWidth,
                                  int nHeight, int nDestPitch, int nSrcPitch,
                                  int nUnused, DWORD dwRop);

// 0x4CDF20, spelled exactly as dlg_draw.cpp spells it.
const char * __fastcall font_adjust(int nX, int nY, const char *psz);


// The bmp_t a ctrl_t carries at +0x0C, as dlg_draw.cpp reaches it; +0x10 is one
// slot doing load-time resource index and run-time pixel-pointer duty.
#define ctrl_bmp(p) (reinterpret_cast<bmp_t *>(&(p)->_unmapped0C[0]))

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x66FD30 sgbLowMem_g, main.cpp's, a DWORD.
#ifndef OC_SGBLOWMEM_G
#define OC_SGBLOWMEM_G
#ifdef OC_DLL_MODE
#define sgbLowMem_g (*reinterpret_cast<DWORD *>(OC_VA(0x66FD30u)))
#else
extern DWORD sgbLowMem_g;
#endif
#endif

// 0x66FD24 sgbWindowActive_g, main.cpp's, a DWORD.
#ifndef OC_SGBWINDOWACTIVE_G
#define OC_SGBWINDOWACTIVE_G
#ifdef OC_DLL_MODE
#define sgbWindowActive_g (*reinterpret_cast<DWORD *>(OC_VA(0x66FD24u)))
#else
extern DWORD sgbWindowActive_g;
#endif
#endif

// 0x6C46E4 sgTitleBmpLoading_g and 0x6C46EC sgTitleBmpBlank_g -- the loading
// bar's two bitmaps, each a bmp_t.
#ifndef OC_SGTITLEBMPLOADING_G
#define OC_SGTITLEBMPLOADING_G
#ifdef OC_DLL_MODE
#define sgTitleBmpLoading_g (*reinterpret_cast<bmp_t *>(OC_VA(0x6C46E4u)))
#else
extern bmp_t sgTitleBmpLoading_g;
#endif
#endif

#ifndef OC_SGTITLEBMPBLANK_G
#define OC_SGTITLEBMPBLANK_G
#ifdef OC_DLL_MODE
#define sgTitleBmpBlank_g (*reinterpret_cast<bmp_t *>(OC_VA(0x6C46ECu)))
#else
extern bmp_t sgTitleBmpBlank_g;
#endif
#endif

// 0x6C46F4 gTitleDlg_g -- the dialog dlg_load returns and close_title_dlg
// takes back down.
#ifndef OC_GTITLEDLG_G
#define OC_GTITLEDLG_G
#ifdef OC_DLL_MODE
#define gTitleDlg_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x6C46F4u)))
#else
extern ctrl_t *gTitleDlg_g;
#endif
#endif

// 0x6C46F8 sgbTitleTimerState_g -- which of the two bitmaps the loading bar
// shows next, a DWORD.
#ifndef OC_SGBTITLETIMERSTATE_G
#define OC_SGBTITLETIMERSTATE_G
#ifdef OC_DLL_MODE
#define sgbTitleTimerState_g (*reinterpret_cast<DWORD *>(OC_VA(0x6C46F8u)))
#else
extern DWORD sgbTitleTimerState_g;
#endif
#endif

// The bodies, in oracle VA order.

// Build the two offscreen bitmaps the loading bar is composited from, stamp the
// control's caption into the first, and start the 500 ms timer that flips
// between them. The placeholder control is one index lower when memory is
// tight, picked by a ternary as in 1.17.
void init_title_loading(ctrl_t *pDlg)
{
  ctrl_t *pCtrl = ctrl_find_ndx(pDlg, sgbLowMem_g ? -11 : -10);
  void *pOldBmp;

  sgTitleBmpLoading_g.nWidth = pCtrl->rc.right - pCtrl->rc.left + 1;
  sgTitleBmpLoading_g.nHeight = pCtrl->rc.bottom - pCtrl->rc.top + 1;
  bmp_create(&sgTitleBmpLoading_g, "title.cpp", 0x44);
  sgTitleBmpBlank_g.nWidth = sgTitleBmpLoading_g.nWidth;
  sgTitleBmpBlank_g.nHeight = sgTitleBmpLoading_g.nHeight;
  bmp_create(&sgTitleBmpBlank_g, "title.cpp", 0x49);

  // Hyperion: the port's copy (port_storm.cpp), in place of Storm's SBltROP3.
  PortBltRop3(sgTitleBmpBlank_g.pPixels,
              ctrl_bmp(pDlg)->pPixels + pCtrl->rc.top * ctrl_bmp(pDlg)->nWidth +
                  pCtrl->rc.left,
              sgTitleBmpBlank_g.nWidth, sgTitleBmpBlank_g.nHeight,
              sgTitleBmpBlank_g.nWidth, ctrl_bmp(pDlg)->nWidth, 0, 0xCC0020);
  PortBltRop3(sgTitleBmpLoading_g.pPixels,
              ctrl_bmp(pDlg)->pPixels + pCtrl->rc.top * ctrl_bmp(pDlg)->nWidth +
                  pCtrl->rc.left,
              sgTitleBmpLoading_g.nWidth, sgTitleBmpLoading_g.nHeight,
              sgTitleBmpLoading_g.nWidth, ctrl_bmp(pDlg)->nWidth, 0, 0xCC0020);

  pOldBmp = _bmp_current;
  _bmp_current = &sgTitleBmpLoading_g;
  font_set(sgpFonts_g[2]);
  gbFontJust = 0x22;
  font_set_color_tbl(2);
  font_adjust(0, 0, reinterpret_cast<const char *>(pCtrl->pData));
  font_set(0);
  _bmp_current = pOldBmp;
  ctrl_show(pCtrl);
  timer_create(pCtrl, 1, 0x1F4,
               reinterpret_cast<PFN_TIMER_FN *>(&title_loading_timer_fcn));
}

// Load the background and the font palette, start the theme unless memory is
// tight, then fade in and tail into the loading bar.
void title_activate(ctrl_t *pDlg)
{
  alloc_load_bmp(const_cast<char *>("glue\\title\\title.pcx"), ctrl_bmp(pDlg),
                 reinterpret_cast<DWORD_PTR>(sgGamePalette_g), "title.cpp", 0x8B);
  load_font_palette("glue\\title\\tFont.pcx");
  if (!sgbLowMem_g)
    snd_play_music(0x16);
  ctrl_set_update(pDlg);
  cur_hide();
  vid_restore_palette(0, 0x100, sgGamePalette_g);
  vid_fade_in(3);
  init_title_loading(pDlg);
}

// Stop the timer, drop both bitmaps and fade back out. ECX is only forwarded to
// ctrl_find_ndx, so this body shows no width for it.
void title_deactivate(ctrl_t *pDlg)
{
  timer_remove_node(ctrl_find_ndx(pDlg, sgbLowMem_g ? -11 : -10), 1);
  bmp_destroy(&sgTitleBmpLoading_g);
  bmp_destroy(&sgTitleBmpBlank_g);
  vid_fade_out(3);
}

// The dialog's event handler: bring the screen up on init, tear it down on
// destroy, and hand everything to the stock dialog handler either way.
int title_interact(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        title_activate(pCtrl);
        break;
      case 1:
        title_deactivate(pCtrl);
        break;
    }
  }
  return ctrl_dlg(pCtrl, pEvent);
}

// True while the title dialog is up.
BOOL is_title_active()
{
  return gTitleDlg_g != 0;
}

// The control's +0x2E draw handler. Both offsets and the clip rect are ignored:
// the bitmap goes at the control's own top-left.
void title_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  bmp_copy(pCtrl->rc.left, pCtrl->rc.top, ctrl_bmp(pCtrl));
}

// Load the title dialog, keep it in this module's own slot, and activate it.
void do_title_dlg()
{
  // Hyperion: a Remastered install has no rez\titledlg.bin and no 8-bit title.pcx, so
  // Hyperion starts there without the splash: the screen stays black while LoadGraphics runs
  // and the menus fade in after it (D). close_title_dlg finds no dialog to close.
  if (PortGameSource() == PORT_SOURCE_REMASTERED)
    return;
  gTitleDlg_g = dlg_load(const_cast<char *>("rez\\titledlg.bin"),
                         reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                         reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                         const_cast<char *>("title.cpp"), 0xBE);
  dlg_activate(gTitleDlg_g, 0, title_interact);
}

// Take the title dialog down if it is up. The store is inside the guard.
void close_title_dlg()
{
  if (gTitleDlg_g) {
    dlg_deactivate(gTitleDlg_g);
    gTitleDlg_g = 0;
  }
}

// The loading bar's timer: flip the toggle, copy whichever bitmap the new state
// names into the control and ask for a redraw. A nonzero new state shows the
// loading bitmap.
void title_loading_timer_fcn(ctrl_t *pCtrl)
{
  if (sgbWindowActive_g) {
    sgbTitleTimerState_g = (sgbTitleTimerState_g == 0);
    *ctrl_bmp(pCtrl) =
        sgbTitleTimerState_g ? sgTitleBmpLoading_g : sgTitleBmpBlank_g;
    pCtrl->pfnDraw = &title_draw;
    ctrl_set_update(pCtrl);
  }
}
