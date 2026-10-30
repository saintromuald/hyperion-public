// playflic.cpp -- the full-screen cinematic player. Bodies are in oracle VA
// order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playflic.h"
#include "../h/dialog.h"         // dlgevt_t
#include "../h/event_Win.h"      // init_events, CloseFlic
#include "../h/save_dlg.h"       // RunEvtDispatcher
#include "../h/gds_appfat.h"     // FileErrorDlg
#include "../h/TrigAct.h"        // cur_hide
#include "../h/Storm.h"          // SVidPlay*, Sleep
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled, one #ifdef per name.

typedef void(__fastcall *PFN_EVT_HOOK)(dlgevt_t *pEvent);

// The event hook table init_events clears.
#ifdef OC_DLL_MODE
#define gEventHooks (reinterpret_cast<PFN_EVT_HOOK *>(OC_VA(0x665DF0u)))
#else
extern PFN_EVT_HOOK gEventHooks[19];
#endif
// Nonzero while the main window has focus.
#ifdef OC_DLL_MODE
#define sgbWindowActive_g (*reinterpret_cast<BOOL *>(OC_VA(0x66FD24u)))
#else
extern BOOL sgbWindowActive_g;
#endif
// The flic file names, indexed by flic id. Hyperion: 28 of them in 1.17's data,
// Brood War's three campaign films (xProtoss, xTerran, xZerg) after the 25 before them,
// so the storage the x64 work gives it holds all of them.
#ifdef OC_DLL_MODE
#define sgszFlics (reinterpret_cast<const char **>(OC_VA(0x508DA8u)))
#else
extern const char *sgszFlics[28];
#endif
// Set to stop the playing flic.
#ifdef OC_DLL_MODE
#define sgbWantCloseFlic_g (*reinterpret_cast<BOOL *>(OC_VA(0x675270u)))
#else
extern BOOL sgbWantCloseFlic_g;
#endif

// The bodies, in oracle VA order.

// Asks the playing flic to stop.
void CloseFlic() {
  sgbWantCloseFlic_g = TRUE;
}

// Stops the flic on any non-key event, or on Escape, Enter or Space.
void flicEventCB(dlgevt_t *pEvent) {
  if (pEvent->wTag != 0x0F) {
    sgbWantCloseFlic_g = TRUE;
    return;
  }
  dlg_word_t wKey = *reinterpret_cast<dlg_word_t *>(&pEvent->_unmapped08[0]);
  if (wKey == 0x1B || wKey == 0x0D || wKey == 0x20)
    sgbWantCloseFlic_g = TRUE;
}

// Plays flic nFlic to the end or until closed, pumping events between frames.
// The unsigned compare against 25 picks the playback flags.
BOOL play_flic(int nFlic) {
  HANDLE hVid;
  cur_hide();
  init_events();
  gEventHooks[15] = flicEventCB;
  gEventHooks[5] = flicEventCB;
  gEventHooks[8] = flicEventCB;
  sgbWantCloseFlic_g = FALSE;
  SVidPlayBegin(sgszFlics[nFlic], 0, 0, 0, 0,
                (unsigned int)nFlic < 25 ? 0x10280808 : 0x10A80808, &hVid);
  if (hVid) {
    while (!sgbWantCloseFlic_g) {
      if (sgbWindowActive_g && !SVidPlayContinueSingle(hVid, 0, 0))
        break;
      RunEvtDispatcher(3);
      Sleep(0);
    }
    SVidPlayEnd(hVid);
  } else if (nFlic != 1 && nFlic != 0x18) {
    FileErrorDlg(sgszFlics[nFlic], 2);
  }
  return sgbWantCloseFlic_g;
}
