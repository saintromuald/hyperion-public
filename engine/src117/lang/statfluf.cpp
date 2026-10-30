// statfluf.cpp -- the fixed decorative panels that fill the gaps around the
// in-game console. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/statfluf.h"
#include "../gen/globals117.h"

#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/minimap.h"    // status_get_console_art
#include "../h/snd.h"        // load_game_data
#include "../h/glues.h"      // alloc_load_bmp
#include "../port/render/port_console.h"  // Hyperion: StatFlufPanels, PortConsoleRace

// 0x50D7B0, the four panel tables, and 0x6AB26C, the index into them. The
// tables are 0x50D678, 0x50D708, 0x50D6CC and 0x50D750; the fifth dword,
// 0x00110026, is not an address.
#ifdef OC_DLL_MODE
#define sgConsoleFluf_g (reinterpret_cast<fluf_t **>(OC_VA(0x50D7B0u)))
#define sgbStatFlufConsoleIdx_g (*reinterpret_cast<BYTE *>(OC_VA(0x6AB26Cu)))
#else
extern fluf_t *sgConsoleFluf_g[4];
extern BYTE sgbStatFlufConsoleIdx_g;
#endif

// Hyperion: 0x50D678, the four lists sgConsoleFluf_g points at, 26 records.
// fluf_t holds a pointer, so the x64 build keeps them in storage of its own, in its
// layout, and needs a TU that names them with their type to know it;
// StatFlufPanels is that name's one use.
#ifndef OC_DLL_MODE
extern fluf_t sgConsoleFlufPanels_g[26];
fluf_t *StatFlufPanels() { return sgConsoleFlufPanels_g; }
#endif

// 0x5264C2, the console layout. cheat.cpp and banker.cpp spell it the same way,
// under the same guard.
#ifndef OC_BYTE_5264C2
#define OC_BYTE_5264C2
#ifdef OC_DLL_MODE
#define byte_5264C2 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C2u)))
#else
extern BYTE byte_5264C2;
#endif
#endif

// The dialog's own width and height, +0x36 and +0x38 on a dialog node, reached
// through the _unmapped36 window as dlg_edit.cpp does for the edit-control arm.
#define dlg_width(p) (*reinterpret_cast<dlg_word_t *>(&(p)->_unmapped36[0]))
#define dlg_height(p) (*reinterpret_cast<dlg_word_t *>(&(p)->_unmapped36[2]))

// 0x4A5990. The panels' dialog procedure. Event 0 re-reads the console art and
// asks for a redraw; event 4 and the eight other tags in the table are swallowed
// with a zero, and everything else falls through to ctrl_dlg.
int statfluf_dlg_fcn(ctrl_t *pDlg, dlgevt_t *pEvent)
{
  switch (pEvent->wTag) {
  case 14:
    if (pEvent->dwId != 0) {
      if (pEvent->dwId != 4)
        break;
      return 0;
    }
    pDlg->dwFlags |= 0x2000;
    status_get_console_art(pDlg);
    ctrl_set_update(pDlg);
    break;
  case 0:
  case 1:
  case 3:
  case 4:
  case 6:
  case 7:
  case 9:
  case 15:
    return 0;
  }
  return ctrl_dlg(pDlg, pEvent);
}

// 0x4A5A00. Drops every panel dialog of the console layout currently selected.
void statfluf_deactivate()
{
  fluf_t *p;

  for (p = sgConsoleFluf_g[sgbStatFlufConsoleIdx_g]; p->wX != 0xFFFF; p++) {
    if (p->pDlg != NULL)
      dlg_deactivate(p->pDlg);
    p->pDlg = NULL;
  }
}

// 0x4A5A40. Loads one dialog per panel of the selected console layout and
// stretches it over the panel's rectangle. Replay playback always takes layout
// 3; otherwise the layout is byte_5264C2's (Hyperion: Console skin's, PortConsoleRace).
// The source line 0x7B is 1.17's.
void statfluf_init()
{
  fluf_t *p;

  sgbStatFlufConsoleIdx_g = gPlayback ? 3 : PortConsoleRace();
  for (p = sgConsoleFluf_g[sgbStatFlufConsoleIdx_g]; p->wX != 0xFFFF; p++) {
    p->pDlg = dlg_load("rez\\statfluf.bin",
                       reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                       reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                       "statfluf.cpp", 0x7B);
    p->pDlg->rc.left = p->wX;
    p->pDlg->rc.top = p->wY;
    p->pDlg->rc.right = p->wWidth - 1;
    p->pDlg->rc.bottom = p->wHeight - 1;
    dlg_width(p->pDlg) = p->wWidth;
    dlg_height(p->pDlg) = p->wHeight;
    dlg_activate(p->pDlg, 0, statfluf_dlg_fcn);
  }
}

// Hyperion: 1.17's four tables, kept the first time another is put in place.
void statfluf_port_panels(fluf_t *pPanels, bool bReload)
{
  static fluf_t *s_apOriginal[4];
  static bool s_bSaved;
  if (!s_bSaved) {
    for (int i = 0; i < 4; ++i)
      s_apOriginal[i] = sgConsoleFluf_g[i];
    s_bSaved = true;
  }
  if (bReload)
    statfluf_deactivate();
  for (int i = 0; i < 4; ++i)
    sgConsoleFluf_g[i] = s_apOriginal[i];
  if (pPanels != NULL)
    sgConsoleFluf_g[gPlayback ? 3 : PortConsoleRace()] = pPanels;
  if (bReload)
    statfluf_init();
}
