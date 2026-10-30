// help.cpp -- the in-game help dialog. Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers on some declarations are for, and why moving a
// body between TUs can change the code generated.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/help.h"
#include "../h/dlg_ctrl.h"    // ctrl_set_evt_fcns
#include "../h/dlg_dlg.h"     // dlg_deactivate
#include "../h/dlg_lbox.h"    // lbox_set_recalc
#include "../h/glues.h"       // str_load_table, str_get_string, mem_free
#include "../h/menu_dlg.h"    // set_size_enter
#include "../h/opt_dlgs.h"    // dfl_load_bmp, opt_dlg_set_blend, do_helpmenu_dlg
#include "../h/save_dlg.h"    // modeless_dlg_startup, lbox_ctrl_list
#include "../gen/globals117.h"
#include "../port/front/port_gamemenu.h"  // Hyperion: PortHelpOpen and the rest

// 0x444890 -- the loader callback str_load_table takes in EDX, spelled as
// src117/lang/glues.cpp spells it; only its address is taken here.
void *load_game_data(const char *pszFile, void *pBuffer, DWORD *pdwSize,
                     const char *pszTag, int nLine);

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// 0x5085CC sgHelpEvtFcnTbl -- the help listbox's one-entry event table. `push
// 4h` is the size in bytes and the dword there is help_listbox_ctrl_fcn's
// address, so the extent is exact.
#ifndef OC_SGHELPEVTFCNTBL
#define OC_SGHELPEVTFCNTBL
#ifdef OC_DLL_MODE
#define sgHelpEvtFcnTbl (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x5085CCu)))
#else
extern PFN_CTRL_EVT sgHelpEvtFcnTbl[1];
#endif
#endif

// 0x66CB58 ghelpTxt_strings -- the help text table: str_load_table's result
// goes here and mem_free takes it back.
#ifndef OC_GHELPTXT_STRINGS
#define OC_GHELPTXT_STRINGS
#ifdef OC_DLL_MODE
#define ghelpTxt_strings (*reinterpret_cast<void **>(OC_VA(0x66CB58u)))
#else
extern void *ghelpTxt_strings;
#endif
#endif

// 0x671A81 gbDlgLevel -- the nesting level of the dialog stack, a BYTE: every
// access here is `inc byte ptr` or `mov al, byte ptr`.
#ifndef OC_GBDLGLEVEL
#define OC_GBDLGLEVEL
#ifdef OC_DLL_MODE
#define gbDlgLevel (*reinterpret_cast<BYTE *>(OC_VA(0x671A81u)))
#else
extern BYTE gbDlgLevel;
#endif
#endif

// The bodies, in oracle VA order.

// Bring the help dialog up modeless. The tail jmp is what pins the return type.
void do_help_dlg()
{
  modeless_dlg_startup((void *)"rez\\help.bin", (void *)&help_dlg_fcn);
}

// Load the dialog's bitmap and frames, then read the help text table. The
// dialog level rises here and help_dlg_fcn's teardown arm frees the table.
void help_activate(ctrl_t *pDlg)
{
  dfl_load_bmp(pDlg, NULL);
  opt_dlg_set_blend(pDlg);
  ctrl_set_evt_fcns(pDlg, sgHelpEvtFcnTbl, 4);
  set_size_enter(pDlg, 4, 0x20000000);
  ghelpTxt_strings = reinterpret_cast<void *>(
      str_load_table(reinterpret_cast<intptr_t>("rez\\help_txt.tbl"),
                     reinterpret_cast<intptr_t>(load_game_data),
                     reinterpret_cast<intptr_t>("help.cpp"), 0x46));
  gbDlgLevel++;
}

// The help dialog's proc. Closing it drops the dialog level by one, and by a
// second one when that did not reach zero, which is what sends the user back
// to the help menu rather than out of the dialog stack.
BOOL help_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 0:
      help_activate(pCtrl);
      break;
    case 1:
      modeless_menu_deactivate(pCtrl, pEvent);
      mem_free(ghelpTxt_strings, "help.cpp", 0x5B);
      return 1;
    case 2:
      gbDlgLevel--;
      if (gbDlgLevel != 0) {
        gbDlgLevel--;
        do_helpmenu_dlg();
      } else {
        dlg_deactivate(pCtrl);
      }
      return 1;
    }
  }
  return modeless_opt_dlg(pCtrl, pEvent);
}

// The help listbox. Event 6 is swallowed; the activation fills the list once.
BOOL help_listbox_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  switch (pEvent->wTag) {
  case 6:
    return 1;
  case 0x0E:
    if (pEvent->dwId == 0)
      help_listbox_ctrl_activate(pCtrl);
    break;
  }
  return lbox_ctrl_list(pCtrl, pEvent);
}

// Fill the listbox with help topics 1..0x33 from the table, with the recalc
// suppressed until every item is in.
void help_listbox_ctrl_activate(ctrl_t *pCtrl)
{
  unsigned short i;

  lbox_set_recalc(reinterpret_cast<lbox_t *>(pCtrl), 0);
  for (i = 1; i < 0x34; i++)
    lbox_add_item(pCtrl,
                  reinterpret_cast<const char *>(str_get_string(ghelpTxt_strings, i)),
                  0);
  lbox_set_recalc(reinterpret_cast<lbox_t *>(pCtrl), 1);
  pCtrl->dwFlags |= 0x10020000;
}

// Hyperion: the keystroke help of Hyperion's game menu (port_front_gamemenu.cpp):
// rez\help_txt.tbl, read as help_activate reads it and held while the menu's page shows, and the
// lines help_listbox_ctrl_activate lists, 1 to 0x33.
static void *s_pPortHelp;

// Storm's file imports, HANDLE as void *, which port_stormapi.h declares them with.
extern "C" BOOL __stdcall SFileOpenFileEx(void *hMpq, const char *pszFileName,
                                          DWORD dwSearchScope, void **phFile);
extern "C" BOOL __stdcall SFileCloseFile(void *hFile);

int PortHelpOpen(void)
{
  // load_game_data takes a missing file for a broken install and ends the game
  // (FileErrorDlg), so the table is looked for first
  void *hFile;
  if (s_pPortHelp == NULL && SFileOpenFileEx(NULL, "rez\\help_txt.tbl", 0, &hFile)) {
    SFileCloseFile(hFile);
    s_pPortHelp = reinterpret_cast<void *>(
        str_load_table(reinterpret_cast<intptr_t>("rez\\help_txt.tbl"),
                       reinterpret_cast<intptr_t>(load_game_data),
                       reinterpret_cast<intptr_t>("help.cpp"), 0x46));
  }
  return s_pPortHelp != NULL ? 0x33 : 0;
}

const char *PortHelpLine(int n)
{
  if (s_pPortHelp == NULL)
    return "";
  return reinterpret_cast<const char *>(str_get_string(s_pPortHelp, static_cast<WORD>(n)));
}

void PortHelpClose(void)
{
  if (s_pPortHelp != NULL) {
    mem_free(s_pPortHelp, "help.cpp", 0x5B);
    s_pPortHelp = NULL;
  }
}
