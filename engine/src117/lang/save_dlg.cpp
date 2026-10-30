// save_dlg.cpp -- the save and load game dialogs: the modeless dialogs, their
// listbox and edit-control handlers, the overwrite and delete prompts, the
// save-game name list and the "waiting for other players" pump. Bodies are in
// oracle VA order.
//
// A callee defined here can be inlined into a caller here, which the oracle
// (WPO off) never does across a TU. CanSaveGame stays here because
// do_multi_save_dlg's body includes it.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/save_dlg.h"
#include "../h/opt_dlgs.h"   // do_gamemenu_dlg, dfl_load_bmp, opt_dlg_set_blend
#include "../gen/globals117.h"
#include "../h/dlg_dlg.h"      // img_update, dlg_deactivate
#include "../h/dlg_lbox.h"
#include "../h/dlg_ctrl.h"
#include "../h/minimap.h"
#include "../h/net_cmds.h"     // IsGameHost, error_get_str, send_net_cmd_*
#include "../h/gametype.h"     // IsLadderGame
#include "../h/saveload.h"     // sub_498B30, sub_4991B0
#include "../h/victory.h"      // psnprintf
#include "../h/TSList.h"
#include "../h/save_dlg_list.h"   // TSAVEGAME, its list specialization
#include "../h/rtti.h"         // type_info::raw_name
#include "../h/Storm.h"        // SMemAlloc
#include "../port/gameplay/port_eud.h"  // Hyperion: EUD maps
#include "../port/front/port_gamemenu.h"  // Hyperion: Hyperion's Save Game and Load Game

#include <string.h>

// The oracle expands strlen and strcpy inline (/Oi).
#pragma intrinsic(strlen, strcpy)

// _mbsicmp is declared in ../h/save_dlg.h -- see the note there.

// KERNEL32's GetTickCount, dllimport so the call goes through the import slot
// (0x4EB2FC) as 1.17's does. Declared here, as dlg_dlg.cpp and pause.cpp do.
extern "C" __declspec(dllimport) ULONG __stdcall GetTickCount(void);

// Callees no header declares.
void __fastcall set_size_escape(ctrl_t *pDlg, WORD wFrame, DWORD dwFlags);
void __fastcall set_size_enter(ctrl_t *pDlg, WORD wFrame, DWORD dwFlags);
void __fastcall set_btn_size(ctrl_t *pCtrl, WORD wFrame, DWORD dwFlags);

// Globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x671A81 gbDlgLevel, BYTE.
#ifdef OC_DLL_MODE
#define gbDlgLevel (*reinterpret_cast<BYTE *>(OC_VA(0x671A81u)))
#else
extern BYTE gbDlgLevel;
#endif

// 0x6CE658 gnDlgCount, long.
#ifdef OC_DLL_MODE
#define gnDlgCount (*reinterpret_cast<LONG *>(OC_VA(0x6CE658u)))
#else
extern LONG gnDlgCount;
#endif

// 0x6CE6DC gnDlgReturnVal, long.
#ifdef OC_DLL_MODE
#define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
#else
extern LONG gnDlgReturnVal;
#endif

// 0x6A5EA0 sgdwLastSaveTime, DWORD -- GetTickCount at the last save command,
// for CanSaveGame's two-minute wait.
#ifdef OC_DLL_MODE
#define sgdwLastSaveTime (*reinterpret_cast<DWORD *>(OC_VA(0x6A5EA0u)))
#else
extern DWORD sgdwLastSaveTime;
#endif

// 0x6A5E7C dword_6A5E7C, DWORD -- the timestamp of the save the delete or
// replace prompt is about, parked for do_delete and do_replace.
#ifdef OC_DLL_MODE
#define dword_6A5E7C (*reinterpret_cast<DWORD *>(OC_VA(0x6A5E7Cu)))
#else
extern DWORD dword_6A5E7C;
#endif

// 0x6A5E80 byte_6A5E80, char[0x1C] -- the save-name buffer. 0x1C is the length
// do_save_game passes is_bad_filename, and word_6A5E9C follows it.
#ifdef OC_DLL_MODE
#define byte_6A5E80 (*reinterpret_cast<char (*)[0x1C]>(OC_VA(0x6A5E80u)))
#else
extern char byte_6A5E80[0x1C];
#endif

// 0x6A5E9C word_6A5E9C, unsigned WORD (the compare is unsigned) -- the count of
// saves listed; do_save_game refuses a new name at 0x80. Bumped by sub_49ACE0,
// zeroed by saveload_dlg_activate and saveload_dlg_deactivate.
#ifdef OC_DLL_MODE
#define word_6A5E9C (*reinterpret_cast<unsigned short *>(OC_VA(0x6A5E9Cu)))
#else
extern unsigned short word_6A5E9C;
#endif

// 0x6A5E9E byte_6A5E9E, BYTE -- the remembered listbox selection.
#ifdef OC_DLL_MODE
#define byte_6A5E9E (*reinterpret_cast<BYTE *>(OC_VA(0x6A5E9Eu)))
#else
extern BYTE byte_6A5E9E;
#endif

// 0x4EF8B0 and 0x4EFBC4 -- the save and load dialog templates in .rdata, each
// an object whose address is passed, not a pointer variable, as 1.17 has it.
#ifdef OC_DLL_MODE
#define sgSaveDlgTemplate (*reinterpret_cast<BYTE (*)[1]>(OC_VA(0x4EF8B0u)))
#else
extern BYTE sgSaveDlgTemplate[1];
#endif
#ifdef OC_DLL_MODE
#define sgMultiLoadDlgTemplate (*reinterpret_cast<BYTE (*)[1]>(OC_VA(0x4EFBC4u)))
#else
extern BYTE sgMultiLoadDlgTemplate[1];
#endif

// The save-game list at 0x509374 is a 12-byte TSList with its link at 0x509378.
// saveload_dlg_deactivate and sub_49ACE0 use it as a TSList; the four bodies
// that walk it read the link as an oc_savenode_t and step with
// `(p ? p : &link)->pNext`, as 1.17's walk does, so both views are kept.
// Node fields: +0x04 the next node, +0x08 the save's timestamp, +0x0C the name.
// The walks do not read +0x00.
struct oc_savenode_t {
  /*0x00*/ void          *_unmapped00;
  /*0x04*/ oc_savenode_t *pNext;
  /*0x08*/ ULONG  dwTimestamp;
  /*0x0C*/ char           szName[1];
};
OC_X86_LAYOUT(offsetof(oc_savenode_t, pNext)       == 0x04, "oc_savenode_t::pNext       mov esi, dword ptr [eax+4h]");
OC_X86_LAYOUT(offsetof(oc_savenode_t, dwTimestamp) == 0x08, "oc_savenode_t::dwTimestamp mov ecx, dword ptr [esi+8h]");
OC_X86_LAYOUT(offsetof(oc_savenode_t, szName)      == 0x0C, "oc_savenode_t::szName      lea eax, dword ptr [esi+0Ch]");
OC_X86_LAYOUT(sizeof(oc_savenode_t)                == 0x10, "oc_savenode_t is 16 B as DECLARED -- the oracle proves no upper bound");

// 0x509378, the list's link. Reached through sgSaveGameList because the list's
// layout changes at 64 bits; the node view keeps
// TSAVEGAME's layout at both widths.
#define oc_gSaveGameList (*reinterpret_cast<oc_savenode_t *>(&sgSaveGameList.link))

// 0x509374 sgSaveGameList. TSAVEGAME and its list specialization are in
// save_dlg_list.h.
typedef TSList<TSAVEGAME, TSGetLink<TSAVEGAME> > SAVEGAMELIST;
#ifdef OC_DLL_MODE
#define sgSaveGameList (*reinterpret_cast<SAVEGAMELIST *>(OC_VA(0x509374u)))
#else
extern SAVEGAMELIST sgSaveGameList;
#endif

// 0x509380 -- the four control-event handlers (0x10 bytes) saveload_dlg_activate
// hands ctrl_set_evt_fcns.
#ifdef OC_DLL_MODE
#define sgSaveDlgEvtFcns (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x509380u)))
#else
extern PFN_CTRL_EVT sgSaveDlgEvtFcns[4] OC_UNALIGNED;
#endif

// 0x5109CC -- the MSVC RTTI type descriptor for TSAVEGAME, a global rather than
// typeid so the dll-mode build can use the oracle's own (rtti.h).
#ifdef OC_DLL_MODE
#define gTypeInfo_TSAVEGAME (*reinterpret_cast<type_info *>(OC_VA(0x5109CCu)))
#else
extern type_info gTypeInfo_TSAVEGAME;
#endif

// The walk's loop test is signed, as 1.17's is: Storm's list terminator is
// ~(&head.link.prev), a negative value (TSList.h).
#define OC_NODE_LIVE(p) ((LONG_PTR)(p) > 0)

// The walk's step, shared by the four walking bodies (see the list above).
static oc_savenode_t *oc_save_list_next(oc_savenode_t *p)
{
  return (p != NULL ? p : &oc_gSaveGameList)->pNext;
}

// The name lookup shared by do_load_game, do_delete_game and do_save_game. One
// variable holds the walk and the result, as 1.17 keeps both in one register.
static oc_savenode_t *oc_save_list_find(const char *pszName)
{
  for (oc_savenode_t *p = oc_gSaveGameList.pNext; OC_NODE_LIVE(p); p = oc_save_list_next(p))
  {
    if (_mbsicmp(pszName, p->szName) == 0)
      return p;
  }
  return NULL;
}

// The TSList unlink as sub_49ACE0 spells it, the split gametype.cpp also has
// (TSList<CTRLNODE,...>::DeleteNode and UnlinkAll).
static void UnlinkSaveGame(TSAVEGAME *node)
{
  TSLink<TSAVEGAME> *pl = &node->link;
  TSAVEGAME *pprev = pl->prev;
  if (pprev != NULL)
  {
    TSAVEGAME *pnext = pl->next;
    TSLink<TSAVEGAME> *pnl;
    if ((intptr_t)pnext <= 0)
    {
      pnl = reinterpret_cast<TSLink<TSAVEGAME> *>(~(intptr_t)pnext);
    }
    else
    {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0)
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      pnl = reinterpret_cast<TSLink<TSAVEGAME> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = NULL;
    pl->next = NULL;
  }
}

// The bodies, in oracle VA order.

// SAVEGAMELIST::Clear -- oracle 0x49AC80. Unlink and free every save-game node.
// The head is normalised to NULL before the test, as 1.17 has it.
void SAVEGAMELIST::Clear(void)
{
  for (;;) {
    TSAVEGAME *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkSaveGame(p);
    SMemFree(p, gTypeInfo_TSAVEGAME.raw_name(), -2, 0);
  }
}

// sub_49ACE0 -- oracle 0x49ACE0 (233 B), __stdcall. The callback sub_4991B0
// enumerates saves with: allocate a TSAVEGAME, splice it onto the front of the
// list and copy the timestamp and name out of the header it is handed. 1.17's
// bug is kept: when SMemAlloc fails the body still writes through the null node.
void __stdcall sub_49ACE0(void *pArg0)
{
  const BYTE *pHdr;
  void *pv;
  TSAVEGAME *node;
  TSAVEGAME *pnext;
  TSLink<TSAVEGAME> *nl;
  TSLink<TSAVEGAME> *pnl;
  intptr_t nOff;

  if (word_6A5E9C >= 0x80)
    return;

  pv = SMemAlloc(sizeof(TSAVEGAME), gTypeInfo_TSAVEGAME.raw_name(), -2, 8);
  if (pv != NULL)
  {
    node = reinterpret_cast<TSAVEGAME *>(pv);
    node->link.prev = NULL;
    nl = &node->link;
    node->link.next = NULL;
  }
  else
  {
    node = NULL;
    nl = &sgSaveGameList.link;
  }

  UnlinkSaveGame(reinterpret_cast<TSAVEGAME *>(nl));

  // Splice nl in at the front; the link offset comes from the list's own +0x00
  // field, as 1.17 has it.
  nl->prev = reinterpret_cast<TSAVEGAME *>(&sgSaveGameList.link);
  nl->next = sgSaveGameList.link.next;
  pnext = sgSaveGameList.link.next;
  nOff = static_cast<int>(sgSaveGameList._unmapped_00);
  if ((intptr_t)pnext <= 0)
  {
    pnl = reinterpret_cast<TSLink<TSAVEGAME> *>(~(intptr_t)pnext);
  }
  else
  {
    if (nOff < 0)
      nOff = (intptr_t)&sgSaveGameList.link - (intptr_t)sgSaveGameList.link.prev->link.next;
    pnl = reinterpret_cast<TSLink<TSAVEGAME> *>((intptr_t)pnext + nOff);
  }
  pnl->prev = reinterpret_cast<TSAVEGAME *>(nl);
  sgSaveGameList.link.next = node;

  pHdr = static_cast<const BYTE *>(pArg0);
  node->dwValue = *reinterpret_cast<const DWORD *>(pHdr + 0x28);
  strcpy(node->szName, reinterpret_cast<const char *>(pHdr + 0x99));
  ++word_6A5E9C;
}

// CanSaveGame -- oracle 0x49ADD0 (40 B). Always outside multiplayer; in
// multiplayer, two minutes (0x1D4C0 ms) after the last save command.
// do_multi_save_dlg inlines it, so it is defined here.
BOOL CanSaveGame() {
  if (EudSavesBlocked())  // Hyperion: not in a game whose map writes memory
    return FALSE;
  return gbMultiPlayerMode == 0 || GetTickCount() > sgdwLastSaveTime + 0x1D4C0;
}

// do_delete -- oracle 0x49AE00 (144 B). The delete prompt's result callback,
// reading ECX as 16 bits (save_dlg.h, shape 2). On OK (0xFFFE) delete the save
// through sub_498B30, or report the failure and stop; then reopen the save
// dialog.
int do_delete(WORD wResult) {
  char szMsg[0x100];

  if (wResult == 0xFFFE) {
    if (sub_498B30(byte_6A5E80) == 0) {
      psnprintf(szMsg, 0x100, error_get_str(1), byte_6A5E80);
      modal_ok_dlg(szMsg, (void *)&redo_save);
      return 0;
    }
    byte_6A5E9E = 0;
    dword_6A5E7C = 0;
    byte_6A5E80[0] = 0;
  }
  gbDlgLevel = 0;
  modeless_dlg_startup(sgSaveDlgTemplate, (void *)&save_dlg_fcn);
  return 1;
}

// do_delete_game -- oracle 0x49AE90 (179 B).
// Find the selected save, park its timestamp and name for do_delete and raise
// the "really delete?" prompt. No null check, as in do_load_game.
void do_delete_game(ctrl_t *pDlg)
{
  char szBuf[0x100];
  lbox_t *pBox = reinterpret_cast<lbox_t *>(ctrl_find_ndx(pDlg, 1));
  const char *pszName = (const char *)lbox_get_item(pBox, lbox_get_selection(pBox));
  oc_savenode_t *pFound = oc_save_list_find(pszName);

  dword_6A5E7C = pFound->dwTimestamp;
  strcpy(byte_6A5E80, pFound->szName);
  psnprintf(szBuf, 0x100, error_get_str(4), pszName);
  modal_okcancel_dlg(szBuf, (void *)&do_delete);
}

// do_multi_load_dlg -- oracle 0x49AF50 (72 B). Open the load dialog when
// gbMultiPlayerMode and gPlayback are both clear, after clearing the remembered
// selection and name; in game mode 4, wait for it to close.
void do_multi_load_dlg() {
  if (gbMultiPlayerMode == 0 && gPlayback == 0) {
    byte_6A5E9E = 0;
    dword_6A5E7C = 0;
    byte_6A5E80[0] = 0;
    modeless_dlg_startup(sgMultiLoadDlgTemplate, (void *)&load_dlg_fcn);
    if (gwGameMode == 4)
      wait_dlg_done();
  }
}

// do_load_game -- oracle 0x49AFA0 (104 B).
// Send the load command for the selected save. 1.17 has no null check here: a
// name not in the list dereferences NULL, kept as written.
void do_load_game(ctrl_t *pDlg)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(ctrl_find_ndx(pDlg, 1));
  const char *pszName = (const char *)lbox_get_item(pBox, lbox_get_selection(pBox));
  oc_savenode_t *pFound = oc_save_list_find(pszName);

  send_net_cmd_LoadGame(pFound->dwTimestamp, pszName);
  dlg_deactivate(pDlg);
}

// do_replace -- oracle 0x49B010 (147 B). The overwrite prompt's result
// callback, do_delete's twin: on OK delete the old save and send the save
// command, stamping sgdwLastSaveTime; otherwise reopen the save dialog.
int do_replace(WORD wResult) {
  char szMsg[0x100];

  if (wResult != 0xFFFE) {
    gbDlgLevel = 0;
    modeless_dlg_startup(sgSaveDlgTemplate, (void *)&save_dlg_fcn);
    return 1;
  }
  if (sub_498B30(byte_6A5E80) == 0) {
    psnprintf(szMsg, 0x100, error_get_str(2), byte_6A5E80);
    modal_ok_dlg(szMsg, (void *)&redo_save);
    return 0;
  }
  send_net_cmd_SaveGame(byte_6A5E80);
  sgdwLastSaveTime = GetTickCount();
  return 0;
}

// do_multi_save_dlg -- oracle 0x49B0B0 (97 B). Open the save dialog for the
// host of a non-ladder game once CanSaveGame allows it and gPlayback is clear.
// CanSaveGame is inlined here, as in 1.17.
void do_multi_save_dlg() {
  if (IsGameHost() && !IsLadderGame() && CanSaveGame() && gPlayback == 0) {
    byte_6A5E9E = 0;
    dword_6A5E7C = 0;
    byte_6A5E80[0] = 0;
    modeless_dlg_startup(sgSaveDlgTemplate, (void *)&save_dlg_fcn);
  }
}

// do_save_game -- oracle 0x49B120 (306 B). Read the edit control, strip
// trailing spaces, then: the name exists (overwrite prompt), the list is full,
// the name is not a legal file name, or the save goes out. The trim is a
// `while (--i >= 0)`, the loop shape 1.17 has.
void do_save_game(ctrl_t *pDlg)
{
  char szBuf[0x100];
  oc_savenode_t *pFound;
  int i;

  edit_get_text(ctrl_find_ndx(pDlg, 2), byte_6A5E80);

  i = (int)strlen(byte_6A5E80);
  while (--i >= 0)
  {
    if (byte_6A5E80[i] != ' ')
      break;
    byte_6A5E80[i] = '\0';
  }

  pFound = oc_save_list_find(byte_6A5E80);
  if (pFound != NULL)
  {
    dword_6A5E7C = pFound->dwTimestamp;
    psnprintf(szBuf, 0x100, error_get_str(3), byte_6A5E80);
    modal_okcancel_dlg(szBuf, (void *)&do_replace);
  }
  else if (word_6A5E9C >= 0x80)
  {
    modal_ok_dlg(error_get_str(5), (void *)&redo_save);
  }
  else if (is_bad_filename(byte_6A5E80, 0x1C, 0))
  {
    modal_ok_dlg(error_get_str(6), (void *)&redo_save);
  }
  else
  {
    send_net_cmd_SaveGame(byte_6A5E80);
    sgdwLastSaveTime = GetTickCount();
    dlg_deactivate(pDlg);
  }
}

// edit_key_evt -- oracle 0x49B260 (243 B). The save dialog's edit-control key
// filter: replay.cpp's replaydlg_edit_evn_char (0x480AC0) without its
// event-window store. error_get_str is its own statement so it is called before
// the key is loaded, as in 1.17. gray_delete_btn is written out at the end.
void edit_key_evt(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  ctrl_t *pDel;
  ctrl_t *pLbox;
  ctrl_t *pBtn;
  char *pszText;
  const char *pszReject;

  pszReject = error_get_str(0x11);
  if (strchr(pszReject, *reinterpret_cast<WORD *>(pEvent->_unmapped08)) != NULL)
    return;

  if (*reinterpret_cast<WORD *>(pEvent->_unmapped08) != 8)
  {
    pszText = static_cast<char *>(pCtrl->pData);
    if (strlen(pszText) < 0x1B &&
        (*reinterpret_cast<WORD *>(pEvent->_unmapped08) != 0x20 ||
         *pszText != '\0'))
    {
      edit_ctrl_edit(pCtrl, pEvent);
    }
  }
  else
  {
    edit_ctrl_edit(pCtrl, pEvent);
  }

  pDel = ctrl_find_ndx(pCtrl, -2);
  if (*static_cast<char *>(pCtrl->pData) != '\0')
    ctrl_ungray(pDel);
  else
    ctrl_gray(pDel);

  pLbox = ctrl_find_ndx(pCtrl, 1);
  if (lbox_get_selection(reinterpret_cast<lbox_t *>(pLbox)) != 0xFF)
  {
    ctrl_set_val(pLbox, 0xFF);
    ctrl_set_update(pLbox);
    // gray_delete_btn(pLbox), written out.
    pBtn = ctrl_find_ndx(pLbox, 3);
    if (pBtn != NULL)
    {
      if (lbox_get_count(reinterpret_cast<lbox_t *>(pLbox)) != 0 &&
          lbox_get_selection(reinterpret_cast<lbox_t *>(pLbox)) != 0xFF)
        ctrl_ungray(pBtn);
      else
        ctrl_gray(pBtn);
    }
  }
}

// gray_delete_btn -- oracle 0x49B360 (63 B). Gray or ungray the delete button
// (index 3) from the listbox's count and selection; the argument is the listbox.
void gray_delete_btn(ctrl_t *pBox) {
  ctrl_t *pBtn = ctrl_find_ndx(pBox, 3);

  if (pBtn != NULL) {
    lbox_t *pList = reinterpret_cast<lbox_t *>(pBox);

    if (lbox_get_count(pList) != 0 && lbox_get_selection(pList) != 0xFF)
      ctrl_ungray(pBtn);
    else
      ctrl_gray(pBtn);
  }
}

// load_dlg_cmd -- oracle 0x49B3A0 (51 B). The load dialog's command handler:
// load (-2) or back out (-3). The dialog comes in ECX and the tail jumps pass
// it on there.
void load_dlg_cmd(ctrl_t *pDlg) {
  if (gnDlgReturnVal == -2) {
    do_load_game(pDlg);
  } else if (gnDlgReturnVal == -3) {
    if (--gbDlgLevel != 0) {
      --gbDlgLevel;
      do_gamemenu_dlg();
    } else {
      dlg_deactivate(pDlg);
    }
  }
}

// load_dlg_fcn -- oracle 0x49B3E0 (58 B). The load dialog's event function;
// case 2 is load_dlg_cmd.
BOOL load_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        saveload_dlg_activate(pCtrl, pEvent);
        return TRUE;
      case 1:
        saveload_dlg_deactivate(pCtrl, pEvent);
        return TRUE;
      case 2:
        load_dlg_cmd(pCtrl);
        return TRUE;
    }
  }
  return modeless_opt_dlg(pCtrl, pEvent);
}

// redo_save -- oracle 0x49B420 (28 B). Reopen the save dialog; the modal
// prompts continue with it.
BOOL redo_save() {
  gbDlgLevel = 0;
  modeless_dlg_startup(sgSaveDlgTemplate, (void *)&save_dlg_fcn);
  return TRUE;
}

// save_dlg_cmd -- oracle 0x49B440 (68 B). load_dlg_cmd's twin, with save (-2)
// and delete (3).
void save_dlg_cmd(ctrl_t *pDlg) {
  if (gnDlgReturnVal == 3) {
    do_delete_game(pDlg);
  } else if (gnDlgReturnVal == -2) {
    gbDlgLevel = 0;
    do_save_game(pDlg);
  } else if (gnDlgReturnVal == -3) {
    if (--gbDlgLevel != 0) {
      --gbDlgLevel;
      do_gamemenu_dlg();
    } else {
      dlg_deactivate(pDlg);
    }
  }
}

// save_dlg_fcn -- oracle 0x49B490 (58 B). The save dialog's event function, a
// switch on dwId as 1.17 has it.
BOOL save_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        saveload_dlg_activate(pCtrl, pEvent);
        return TRUE;
      case 1:
        saveload_dlg_deactivate(pCtrl, pEvent);
        return TRUE;
      case 2:
        save_dlg_cmd(pCtrl);
        return TRUE;
    }
  }
  return modeless_opt_dlg(pCtrl, pEvent);
}

// save_edit_fcn -- oracle 0x49B4D0 (44 B). The save dialog's edit control: tag
// 0x0F goes to edit_key_evt; tag 0x0E with dwId 0 sets the control up first.
BOOL save_edit_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  switch (pEvent->wTag) {
    case 0x0E:
      if (pEvent->dwId == 0) {
        pCtrl->dwFlags |= 0x80000000;
        // +0x38 is in dialog.h's _unmapped36; nothing else reads it, so it has
        // no field name.
        pCtrl->_unmapped36[0x38 - 0x36] = 3;
      }
      break;
    case 0x0F:
      edit_key_evt(pCtrl, pEvent);
      return TRUE;
  }
  return edit_ctrl_edit(pCtrl, pEvent);
}

// save_listbox_fcn -- oracle 0x49B500 (107 B). The save listbox: dwId 0 refills
// it; 0x0B copies the selected name to the edit control and regrays the delete
// button. An if-chain on dwId, as 1.17 has it.
BOOL save_listbox_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    if (pEvent->dwId != 0) {
      if (pEvent->dwId == 0x0B) {
        lbox_ctrl_list(pCtrl, pEvent);
        ctrl_t *pEdit = ctrl_find_ndx(pCtrl, 2);
        if (pEdit != 0) {
          unsigned char nSel = lbox_get_selection(reinterpret_cast<lbox_t *>(pCtrl));
          if (nSel != 0xFF) {
            edit_set_text(pEdit, reinterpret_cast<const char *>(
                lbox_get_item(reinterpret_cast<lbox_t *>(pCtrl), nSel)));
          }
        }
        gray_delete_btn(pCtrl);
        return TRUE;
      }
    } else {
      save_listbox_init(pCtrl);
    }
  }
  return lbox_ctrl_list(pCtrl, pEvent);
}

// save_listbox_init -- oracle 0x49B570 (150 B). Refill the save listbox from
// the list, then gray or ungray the OK and delete buttons. pDlg is the listbox.
void save_listbox_init(ctrl_t *pDlg)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(pDlg);

  lbox_set_recalc(pBox, 0);
  for (oc_savenode_t *p = oc_gSaveGameList.pNext; OC_NODE_LIVE(p); p = oc_save_list_next(p))
    lbox_add_item(pDlg, p->szName, 0);
  lbox_set_recalc(pBox, 1);

  if (!lbox_get_count(pBox))
    ctrl_gray(ctrl_find_ndx(pDlg, -2));

  ctrl_t *pDelete = ctrl_find_ndx(pDlg, 3);
  if (pDelete != NULL)
  {
    if (lbox_get_count(pBox) && lbox_get_selection(pBox) != 0xFF)
      ctrl_ungray(pDelete);
    else
      ctrl_gray(pDelete);
  }
}

// saveload_dlg_activate -- oracle 0x49B610 (248 B). Rebuild the save list
// through sub_4991B0 with sub_49ACE0 as the callback, set up the dialog, then
// seed the listbox selection and the edit control from the remembered globals.
void saveload_dlg_activate(ctrl_t *pDlg, dlgevt_t *pEvent)
{
  ctrl_t *pCtrl;
  ctrl_t *pLbox;
  ctrl_t *pEdit;

  word_6A5E9C = 0;
  sub_4991B0(sub_49ACE0);
  dfl_load_bmp(pDlg, NULL);
  opt_dlg_set_blend(pDlg);
  ctrl_set_evt_fcns(pDlg, sgSaveDlgEvtFcns, 0x10);
  set_size_escape(pDlg, 5, 0x20000000);
  set_size_enter(pDlg, 3, 0x20000000);
  pCtrl = ctrl_find_ndx(pDlg, 3);
  if (pCtrl != NULL)
    set_btn_size(pCtrl, 4, 0x20000000);
  modeless_opt_dlg(pDlg, pEvent);
  pLbox = ctrl_find_ndx(pDlg, 1);
  ctrl_set_val(pLbox, byte_6A5E9E);
  pEdit = ctrl_find_ndx(pDlg, 2);
  if (pEdit != NULL)
  {
    if (byte_6A5E80[0] != '\0')
      edit_set_text(pEdit, byte_6A5E80);
    else if (byte_6A5E9E < lbox_get_count(reinterpret_cast<lbox_t *>(pLbox)))
      edit_set_text(pEdit,
                    reinterpret_cast<const char *>(
                        lbox_get_item(reinterpret_cast<lbox_t *>(pLbox),
                                      byte_6A5E9E)));
  }
  ++gbDlgLevel;
}

// saveload_dlg_deactivate -- oracle 0x49B710 (128 B). Remember the selection
// and the trimmed name, empty the save list and zero its count. `--nAt` is its
// own statement, as 1.17 has it.
BOOL saveload_dlg_deactivate(ctrl_t *pDlg, dlgevt_t *pEvent)
{
  ctrl_t *pCtrl;
  int nAt;

  byte_6A5E9E =
      lbox_get_selection(reinterpret_cast<lbox_t *>(ctrl_find_ndx(pDlg, 1)));
  pCtrl = ctrl_find_ndx(pDlg, 2);
  if (pCtrl != NULL)
  {
    edit_get_text(pCtrl, byte_6A5E80);
    nAt = static_cast<int>(strlen(byte_6A5E80));
    --nAt;
    while (nAt >= 0)
    {
      if (byte_6A5E80[nAt] != ' ')
        break;
      byte_6A5E80[nAt] = '\0';
      --nAt;
    }
  }
  sgSaveGameList.Clear();
  word_6A5E9C = 0;
  return modeless_menu_deactivate(pDlg, pEvent);
}

// wait_dlg_done -- oracle 0x49B790 (41 B). Pump events until the dialog count
// falls below its value after the first img_update (a signed compare).
void wait_dlg_done() {
  img_update();
  LONG nCount = gnDlgCount;
  do {
    RunEvtDispatcher(3);
    img_update();
  } while (gnDlgCount >= nCount);
}

// Hyperion: Hyperion's Save Game and Load Game over a game (port_front_load.cpp),
// made as these dialogs make them: the list saveload_dlg_activate reads and its deactivate lets
// go of, do_save_game's checks and send, do_replace's, and do_load_game's.
static void PortSavesRead(void)
{
  word_6A5E9C = 0;
  sub_4991B0(sub_49ACE0);
}

static void PortSavesDrop(void)
{
  sgSaveGameList.Clear();
  word_6A5E9C = 0;
}

int PortGameSave(const char *pszName, bool bReplace)
{
  char szName[sizeof byte_6A5E80];
  SStrCopy(szName, pszName, sizeof szName);
  int i = static_cast<int>(strlen(szName));
  while (--i >= 0 && szName[i] == ' ')
    szName[i] = '\0';
  PortSavesRead();
  const bool bThere = oc_save_list_find(szName) != NULL;
  const bool bFull = word_6A5E9C >= 0x80;
  PortSavesDrop();
  if (bThere && !bReplace)
    return PORT_SAVE_THERE;
  if (!bThere && bFull)
    return PORT_SAVE_FULL;
  if (!bThere && is_bad_filename(szName, sizeof szName, 0))
    return PORT_SAVE_BAD_NAME;
  if (bThere && sub_498B30(szName) == 0)
    return PORT_SAVE_NOT_REPLACED;
  send_net_cmd_SaveGame(szName);
  sgdwLastSaveTime = GetTickCount();
  return PORT_SAVE_DONE;
}

bool PortGameLoad(const char *pszName)
{
  PortSavesRead();
  oc_savenode_t *pFound = oc_save_list_find(pszName);
  if (pFound != NULL)
    send_net_cmd_LoadGame(pFound->dwTimestamp, pszName);
  PortSavesDrop();
  return pFound != NULL;
}
