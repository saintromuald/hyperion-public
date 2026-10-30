// statfluf.h -- declarations for src117/lang/statfluf.cpp: the fixed decorative
// panels ("fluff") that fill the gaps around the in-game console. The file name
// is 1.17's own, from statfluf_init's dlg_load tag. The free functions are
// __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_STATFLUF_H
#define OC117_STATFLUF_H

#include "platform.h"
#include "dialog.h"

// One panel's placement record; a first WORD of 0xFFFF ends a table.
// statfluf_init reads the four WORDs as the panel's position and size and caches
// the loaded dialog in pDlg.
struct fluf_t {
  /*0x00*/ WORD wX;
  /*0x02*/ WORD wY;
  /*0x04*/ WORD wWidth;
  /*0x06*/ WORD wHeight;
  /*0x08*/ ctrl_t *pDlg;
};

OC_X86_LAYOUT(sizeof(fluf_t) == 0x0C, "fluf_t stride  statfluf_deactivate: add esi, 0Ch");
static_assert(offsetof(fluf_t, wX)      == 0x00, "fluf_t::wX      statfluf_init: movzx ecx, word ptr [esi]");
static_assert(offsetof(fluf_t, wY)      == 0x02, "fluf_t::wY      statfluf_init: movzx eax, word ptr [esi+2h]");
static_assert(offsetof(fluf_t, wWidth)  == 0x04, "fluf_t::wWidth  statfluf_init: movzx ecx, word ptr [esi+4h]");
static_assert(offsetof(fluf_t, wHeight) == 0x06, "fluf_t::wHeight statfluf_init: movzx ecx, word ptr [esi+6h]");
static_assert(offsetof(fluf_t, pDlg)    == 0x08, "fluf_t::pDlg    statfluf_deactivate: mov ecx, dword ptr [esi+8h]");

// 0x4A5990. The panels' shared dialog procedure, which statfluf_init passes to
// dlg_activate, so a PFN_CTRL_EVT; it returns zero or ctrl_dlg's result.
int statfluf_dlg_fcn(ctrl_t *pDlg, dlgevt_t *pEvent);

// 0x4A5A00 / 0x4A5A40. Both nullary; the one caller of each (status_free,
// status_init) discards EAX.
void statfluf_deactivate();

// Hyperion: the panels laid out again, from pPanels (terminated by a wX of
// 0xFFFF, the coordinates the screen's) or from 1.17's own table again with NULL:
// the console's layout at the screen's edges (port_console.cpp). The panels take
// their art from sgConsoleArt as they open, as statfluf_init's do. With bReload
// false only the table is put back, for a console that is not up.
void statfluf_port_panels(fluf_t *pPanels, bool bReload);
void statfluf_init();

#endif  // OC117_STATFLUF_H
