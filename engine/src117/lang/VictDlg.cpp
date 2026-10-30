// VictDlg.cpp -- the victory and defeat dialogs, their activate handlers and
// the two command handlers that run whatever the player chose. Bodies are in
// oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/VictDlg.h"
#include "../h/dialog.h"
#include "../h/dlg_ctrl.h"     // ctrl_find_ndx, ctrl_showhide, ctrl_set_val
#include "../h/dlg_dlg.h"      // dlg_deactivate
#include "../h/game.h"         // net_end_game
#include "../h/game_evt.h"     // game_event_reset
#include "../h/glues.h"        // glues_get_str
#include "../h/menu_dlg.h"     // set_size_enter, big_menu_btn_fcn
#include "../h/minimap.h"      // timer_create, timer_remove_node
#include "../h/net_cmds.h"     // gameloop_init, error_get_str
#include "../h/net_misc.h"     // IsBNetGame, IsLoss
#include "../h/opt_dlgs.h"     // victdlg_quit, dfl_load_bmp
#include "../h/save_dlg.h"     // modeless_dlg_startup
#include "../h/snd.h"          // snd_play_snd
#include "../h/victory.h"      // do_loss_dlg, do_victory_dlg
#include "../gen/globals117.h"
#include "../port/front/port_gamemenu.h"  // Hyperion: PortGameEndKind and the rest
#include "../port/gameplay/port_watchgame.h"  // Hyperion: PortWatchLive

// Callees no header of ours declares.

// 0x4B8E80, `xor eax, eax / ret`. /OPT:ICF folded every nullary predicate that
// always answers false onto it; the ledger's name is the survivor. Nullary,
// because loss_activate sets no register before the call.
int __fastcall nullsub___shared_();

// 0x4BE950 is a one-byte `ret` that /OPT:ICF folded several empty functions
// onto, so one address carries several signatures. loss_dlg_cmd sets neither
// ECX nor EDX, so its sibling is nullary; net_cmds.h's two-argument spelling is
// a different overload of the same ledger name and cannot express this call.
void __fastcall voice_squelch();

// 0x6CA734 sgbContinueGame_g, 0x6CA738 sgbVictQuitGame_g and 0x6CA73C
// sgbVictDlgActivated_g, all named by sc-refs/sheets/data_naming_1.17.csv and
// seated in VictDlg.cpp there too. DWORD: every access is
// `mov dword ptr [x], N` or `cmp dword ptr [x], 0h`.
#ifdef OC_DLL_MODE
  #ifndef sgbContinueGame_g
  #define sgbContinueGame_g (*reinterpret_cast<DWORD *>(OC_VA(0x6CA734u)))
  #endif
#else
extern DWORD sgbContinueGame_g;
#endif

#ifdef OC_DLL_MODE
  #ifndef sgbVictQuitGame_g
  #define sgbVictQuitGame_g (*reinterpret_cast<DWORD *>(OC_VA(0x6CA738u)))
  #endif
#else
extern DWORD sgbVictQuitGame_g;
#endif

#ifdef OC_DLL_MODE
  #ifndef sgbVictDlgActivated_g
  #define sgbVictDlgActivated_g (*reinterpret_cast<DWORD *>(OC_VA(0x6CA73Cu)))
  #endif
#else
extern DWORD sgbVictDlgActivated_g;
#endif

// 0x50E0D4 sVictoryDlgEvtFcnTbl, the sheet's name and its VictDlg.cpp seat.
// Extent 1 from the byte count victory_activate passes ctrl_set_evt_fcns
// (`push 4`, indexed by dword); its one entry is 0x46C7D0.
#ifdef OC_DLL_MODE
  #ifndef sVictoryDlgEvtFcnTbl
  #define sVictoryDlgEvtFcnTbl (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x50E0D4u)))
  #endif
#else
extern PFN_CTRL_EVT sVictoryDlgEvtFcnTbl[1];
#endif

// 0x671A81 gbDlgLevel, registered without a type. BYTE: loss_activate writes it
// `mov byte ptr [0x671A81], 0h` and video_dlg_set_options steps it through AL.
#ifdef OC_DLL_MODE
  #ifndef gbDlgLevel
  #define gbDlgLevel (*reinterpret_cast<BYTE *>(OC_VA(0x671A81u)))
  #endif
#else
extern BYTE gbDlgLevel;
#endif

// 0x6CE6DC gnDlgReturnVal, registered long. `cmp dword ptr [0x6CE6DC], -2h` is
// the width and the sign.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

// 0x672664 gbMultiPlayerMode, registered BYTE.
#ifdef OC_DLL_MODE
  #ifndef gbMultiPlayerMode
  #define gbMultiPlayerMode (*reinterpret_cast<BYTE *>(OC_VA(0x672664u)))
  #endif
#else
extern BYTE gbMultiPlayerMode;
#endif

// 0x67CC90 gPlayback, registered DWORD.
#ifdef OC_DLL_MODE
  #ifndef gPlayback
  #define gPlayback (*reinterpret_cast<DWORD *>(OC_VA(0x67CC90u)))
  #endif
#else
extern DWORD gPlayback;
#endif

// ctrl_t +0x26 read as a node pointer and +0x36 as the callback the scroll bar
// runs through its parent, both inside dialog.h's unnamed windows and spelled
// as snd_dlg.cpp already spells them.
typedef void(__fastcall *PFN_CTRL_NOTIFY)(ctrl_t *pCtrl);

// Globals this TU reaches, dual-spelled, one #ifdef per name. The names are the
// approved sheet's (sc-refs/sheets/data_naming_1.17.csv).



// Clear the two flags that make the victory and defeat dialogs idempotent.
void victdlg_init() {
  sgbContinueGame_g = 0;
  sgbVictQuitGame_g = 0;
}

// Leave a Battle.net game once, whatever brings the player out of it.
void victdlg_quit() {
  if (IsBNetGame() && sgbVictQuitGame_g == 0) {
    sgbVictQuitGame_g = 1;
    net_end_game();
  }
}

// Raise the defeat dialog, unless one is already up or the player has already
// chosen to carry on. The tail jmp is what pins the return type.
void __fastcall do_loss_dlg(void) {
  if (sgbVictDlgActivated_g == 0 && sgbContinueGame_g == 0) {
    victdlg_quit();
    sgbVictDlgActivated_g = 1;
    game_event_reset(1);
    gnDlgReturnVal = 0;
    modeless_dlg_startup((void *)"rez\\lmission.bin", (void *)&loss_dlg_fcn);
  }
}

// The same, for victory. This one calls modeless_dlg_startup rather than
// tail-jumping it because it has a value to return.
int __fastcall do_victory_dlg(void) {
  if (sgbVictDlgActivated_g == 0 && sgbContinueGame_g == 0) {
    victdlg_quit();
    sgbVictDlgActivated_g = 1;
    game_event_reset(1);
    gnDlgReturnVal = 0;
    modeless_dlg_startup((void *)"rez\\wmission.bin", (void *)&victory_dlg_fcn);
  }
  return 0;
}

// Build the defeat dialog. Controls -20 and -21 are the two texts a multiplayer
// loser and everyone else get, so their visibilities are opposites, and the
// 20-second timer is what closes the dialog on its own.
void loss_activate(ctrl_t *pDlg) {
  ctrl_t *pCtrl;

  snd_play_snd(0x16);
  dfl_load_bmp(pDlg, NULL);
  opt_dlg_set_blend(pDlg);
  set_size_enter(pDlg, 4, 0x20000000);
  pCtrl = ctrl_find_ndx(pDlg, 1);
  pCtrl->pfnEvt = &big_menu_btn_fcn;
  ctrl_showhide(pCtrl, nullsub___shared_());
  pCtrl = ctrl_find_ndx(pDlg, -20);
  ctrl_showhide(pCtrl, (gbMultiPlayerMode != 0 && IsLoss()) ? 0 : 1);
  pCtrl = ctrl_find_ndx(pDlg, -21);
  ctrl_showhide(pCtrl, (gbMultiPlayerMode != 0 && IsLoss()) ? 1 : 0);
  timer_create(pDlg, 0, 0x4E20, NULL);
  gbDlgLevel = 0;
}

// Run the choice the player made. The single load of gnDlgReturnVal feeds both
// compares, and the trailing store is duplicated into the middle arm. The call
// to 0x4BE950 is nullary here and net_cmds.h seats that name with two
// arguments, so the body matches and cannot yet link.
void loss_dlg_cmd(ctrl_t *pCtrl) {
  switch (gnDlgReturnVal) {
    case -2:
      gameloop_init(7);
      break;
    case 1:
      voice_squelch();
      break;
  }
  sgbContinueGame_g = 1;
}

// The defeat dialog's proc. Id 8 is the timer expiring.
int loss_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        loss_activate(pCtrl);
        break;
      case 2:
        loss_dlg_cmd(pCtrl);
        break;
      case 8:
        if (pEvent->dwParam == 0) {
          gameloop_init(7);
          dlg_deactivate(pCtrl);
          return 1;
        }
        break;
      case 1:
        timer_remove_node(pCtrl, 0);
        modeless_dlg_shutdown(0);
        modeless_opt_dlg(pCtrl, pEvent);
        sgbVictDlgActivated_g = 0;
        return 1;
    }
  }
  return modeless_opt_dlg(pCtrl, pEvent);
}

// Build the victory dialog. Three visibilities are decided together: -20 is the
// briefing text, -21 the multiplayer-defeat text and -23 the next-mission
// button, and a replay replaces two of the captions outright.
void victory_activate(ctrl_t *pDlg) {
  int anStatus[8];
  int nLocal;
  ctrl_t *pText;
  void *pCaption;
  int bShowNext;
  int bShowText;
  int bShowLoss;

  snd_play_snd(0x15);
  dfl_load_bmp(pDlg, NULL);
  opt_dlg_set_blend(pDlg);
  ctrl_set_evt_fcns(pDlg, sVictoryDlgEvtFcnTbl, 4);
  set_size_enter(pDlg, 4, 0x20000000);
  pText = ctrl_find_ndx(pDlg, 1);
  ctrl_showhide(pText, (gbMultiPlayerMode != 0 && gPlayback == 0) ? 1 : 0);
  sub_477270(anStatus, &nLocal);
  if (gbMultiPlayerMode != 0 && IsLoss()) {
    bShowNext = 0;
    bShowLoss = 1;
    bShowText = 0;
  } else {
    bShowLoss = 0;
    if (nLocal == 3 && gPlayback == 0) {
      bShowNext = 1;
      bShowText = 0;
    } else {
      bShowNext = 0;
      bShowText = 1;
    }
  }
  pText = ctrl_find_ndx(pDlg, -20);
  ctrl_showhide(pText, bShowText);
  if (gPlayback != 0)
    ctrl_set_text(pText, glues_get_str(0xB5));
  ctrl_showhide(ctrl_find_ndx(pDlg, -21), bShowLoss);
  // Both arms are the same call; MSVC merges them after register allocation,
  // and the second bShowNext read ranks it above bShowText for EDI.
  if (gPlayback != 0)
    ctrl_showhide(ctrl_find_ndx(pDlg, -23), bShowNext);
  else
    ctrl_showhide(ctrl_find_ndx(pDlg, -23), bShowNext);
  pCaption = ctrl_find_ndx(pDlg, -10)->pData;
  pText = ctrl_find_ndx(pDlg, -22);
  if (bShowText == 0)
    pCaption = pText->pData;
  if (gPlayback != 0) {
    pText = ctrl_find_ndx(pDlg, -2);
    ctrl_set_text(pText, glues_get_str(0xBC));
  } else if (gbMultiPlayerMode != 0) {
    ctrl_find_ndx(pDlg, -2)->pData = pCaption;
  }
}

// Run the choice the player made. Id 1 announces the mission's completion in
// the chat area before returning to the menu.
void victory_dlg_cmd(ctrl_t *pCtrl) {
  switch (gnDlgReturnVal) {
    case -2:
      gameloop_init(6);
      break;
    case 1:
      map_msg_chat_msg(error_get_str(16), 0);
      break;
  }
  sgbContinueGame_g = 1;
}

// The victory dialog's proc. It carries no timer, so its switch is the three
// ids a dec-chain covers.
int victory_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 2:
        victory_dlg_cmd(pCtrl);
        break;
      case 1:
        modeless_dlg_shutdown(0);
        modeless_opt_dlg(pCtrl, pEvent);
        sgbVictDlgActivated_g = 0;
        return 1;
      case 0:
        victory_activate(pCtrl);
        break;
    }
  }
  return modeless_opt_dlg(pCtrl, pEvent);
}

// Hyperion: victory and defeat in the game menu's style (port_front_gamemenu.cpp):
// what victory_activate and loss_activate show, and the choice made by their own handlers.
int PortGameEndKind(bool bVictory, bool *pbContinue)
{
  if (PortWatchLive()) {
    *pbContinue = true;  // Keep Watching
    return GM_END_WATCHED;
  }
  *pbContinue = bVictory && gbMultiPlayerMode != 0 && gPlayback == 0;
  if (gbMultiPlayerMode != 0 && IsLoss())
    return GM_END_DISCONNECTED;
  if (!bVictory)
    return GM_END_DEFEAT;
  int anStatus[8];
  int nLocal;
  sub_477270(anStatus, &nLocal);
  if (nLocal == 3 && gPlayback == 0)
    return GM_END_DRAW;
  if (gPlayback != 0)
    return GM_END_REPLAY;
  return gbMultiPlayerMode != 0 ? GM_END_VICTORY_MULTI : GM_END_VICTORY;
}

void PortGameEndSound(bool bVictory)
{
  snd_play_snd(bVictory ? 0x15 : 0x16);
}

void PortGameEndChoose(bool bVictory, bool bContinue)
{
  if (PortWatchLive()) {
    // a watched skirmish's end: Leave Game as the menu's Exit Replay leaves, Keep Watching as
    // Continue Playing goes on, without its message
    sgbContinueGame_g = 1;
    if (!bContinue)
      PortQuitToMenu();
    return;
  }
  gnDlgReturnVal = bContinue ? 1 : -2;
  if (bVictory)
    victory_dlg_cmd(NULL);
  else
    loss_dlg_cmd(NULL);
}

void PortGameEndTimeout(void)
{
  gameloop_init(7);
}

void PortGameEndClosed(void)
{
  sgbVictDlgActivated_g = 0;
}
