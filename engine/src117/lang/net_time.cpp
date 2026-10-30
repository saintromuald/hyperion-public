// net_time.cpp -- network timing: elapsed game time, the turn clock, and the
// drop-timeout dialog. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stdlib.h>
#include <string.h>             // memcpy, memmove

#include "../h/net_time.h"
#include "../h/pause.h"      // sub_47A620
#include "../h/dlg_ctrl.h"   // ctrl_find_ndx, ctrl_set_update, ctrl_hide, ...
#include "../h/dlg_dlg.h"    // img_update, dlg_deactivate
#include "../h/opt_dlgs.h"   // dfl_load_bmp, opt_dlg_set_blend, modeless_dlg_shutdown
#include "../h/save_dlg.h"   // modeless_dlg_startup, modeless_opt_dlg
#include "../h/victory.h"    // psnprintf
#include "../h/net_misc.h"   // net_drop_player
#include "../h/net_cmds.h"   // PlayerData, voice_squelch
#include "../h/net_mgr.h"    // netmgr_process_messages, GetTurnPackets
#include "../h/Teams.h"      // IsTeamGame
#include "../h/gametype.h"   // team_get_slot_captain
#include "../h/Storm.h"       // SNetDisconnectAll
#include "../h/filters.h"     // color_draw
#include "../h/menu_dlg.h"    // big_menu_btn_fcn
#include "../h/net_rand.h"    // net_get_fixed_seed
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// GetTickCount, imported from KERNEL32 at IAT slot 0x4EB2FC, declared here so
// the call goes through the import pointer as 1.17's does.
extern "C" __declspec(dllimport) DWORD __stdcall GetTickCount(void);

// 0x675014 sgbGameTicksStarted_g -- whether the game clock is running.
#ifndef OC_SGBGAMETICKSSTARTED_G
#define OC_SGBGAMETICKSSTARTED_G
#ifdef OC_DLL_MODE
#define sgbGameTicksStarted_g (*reinterpret_cast<BOOL *>(OC_VA(0x675014u)))
#else
extern BOOL sgbGameTicksStarted_g;
#endif
#endif

// 0x675018 sgdwElapsed_g -- game seconds banked before the current run of the
// clock.
#ifdef OC_DLL_MODE
#define sgdwElapsed_g (*reinterpret_cast<DWORD *>(OC_VA(0x675018u)))
#else
extern DWORD sgdwElapsed_g;
#endif

// 0x675024 sgdwStartTime_g -- GetTickCount when the clock last started.
#ifdef OC_DLL_MODE
#define sgdwStartTime_g (*reinterpret_cast<DWORD *>(OC_VA(0x675024u)))
#else
extern DWORD sgdwStartTime_g;
#endif

// 0x675028 sgdwEndTime_g -- GetTickCount when the clock last stopped.
#ifdef OC_DLL_MODE
#define sgdwEndTime_g (*reinterpret_cast<DWORD *>(OC_VA(0x675028u)))
#else
extern DWORD sgdwEndTime_g;
#endif

// 0x67502C sgdwTimeoutTime_g -- a tick deadline seeded 0x1D4C0 ms behind the
// start time, so the first drop check is already due.
#ifndef OC_SGDWTIMEOUTTIME_G
#define OC_SGDWTIMEOUTTIME_G
#ifdef OC_DLL_MODE
#define sgdwTimeoutTime_g (*reinterpret_cast<DWORD *>(OC_VA(0x67502Cu)))
#else
extern DWORD sgdwTimeoutTime_g;
#endif
#endif

// 0x67500C gbTimeOutDialogActive -- the reentry guard on the drop-wait loop, a
// BYTE.
#ifdef OC_DLL_MODE
#define gbTimeOutDialogActive (*reinterpret_cast<BYTE *>(OC_VA(0x67500Cu)))
#else
extern BYTE gbTimeOutDialogActive;
#endif

// 0x675010 sgpTimeoutDlg -- the modeless drop dialog while it is up, NULL
// otherwise.
#ifdef OC_DLL_MODE
#define sgpTimeoutDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x675010u)))
#else
extern ctrl_t *sgpTimeoutDlg;
#endif

// 0x67501C -- set to 0 before the drop-wait loop and polled as its exit test.
#ifdef OC_DLL_MODE
#define dword_67501C (*reinterpret_cast<DWORD *>(OC_VA(0x67501Cu)))
#else
extern DWORD dword_67501C;
#endif

// 0x675020 -- the countdown in seconds, unsigned as 1.17's divides by 60 are.
#ifdef OC_DLL_MODE
#define dword_675020 (*reinterpret_cast<DWORD *>(OC_VA(0x675020u)))
#else
extern DWORD dword_675020;
#endif

// 0x675030 sgszTimeoutMsg_g -- the "m:ss" label text, 0x20 bytes.
#ifdef OC_DLL_MODE
#define sgszTimeoutMsg_g (*reinterpret_cast<char (*)[0x20]>(OC_VA(0x675030u)))
#else
extern char sgszTimeoutMsg_g[0x20];
#endif

// 0x671A81 gbDlgLevel, a BYTE, spelled as opt_dlgs.cpp spells it.
#ifndef OC117_gbDlgLevel
#define OC117_gbDlgLevel
#ifdef OC_DLL_MODE
#define gbDlgLevel (*reinterpret_cast<BYTE *>(OC_VA(0x671A81u)))
#else
extern BYTE gbDlgLevel;
#endif
#endif

// 0x6CE6DC gnDlgReturnVal, spelled as dlg_dlg.cpp spells it.
#ifndef OC117_gnDlgReturnVal
#define OC117_gnDlgReturnVal
#ifdef OC_DLL_MODE
#define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
#else
extern LONG gnDlgReturnVal;
#endif
#endif

// 0x4EF310 -- the .rdata dialog template for "rez\timeout.bin", an object
// rather than a pointer.
#ifdef OC_DLL_MODE
#define sgTimeoutDlgTemplate (*reinterpret_cast<BYTE (*)[1]>(OC_VA(0x4EF310u)))
#else
extern BYTE sgTimeoutDlgTemplate[1];
#endif

// The bodies, in oracle VA order.

// Sets the banked game-seconds counter outright.
void net_set_elapsed(int nElapsed)
{
  sgdwElapsed_g = nElapsed;
}

// Takes a paused duration back out of the banked counter, but only while the
// clock is running.
void sub_47A620(int nSeconds)
{
  if (sgbGameTicksStarted_g) {
    DWORD *pElapsed = &sgdwElapsed_g;
    *pElapsed -= nSeconds;
  }
}

// Resolves a stalled turn: if the players still waiting outnumber the ones that
// acked, gives up the whole session; otherwise drops the stragglers. The equal
// case falls into the drop loop.
void sub_47A630()
{
  DWORD *pStatus;
  int i;
  int nAcked = 0;
  int nWaiting = 0;

  for (i = 0; i < 8; ++i) {
    DWORD dwStatus = sgdwPlayerStatus_g[i];

    if (dwStatus & 0x10000) {
      ++nAcked;
      if (!(dwStatus & 0x40000))
        ++nWaiting;
    }
  }

  nAcked -= nWaiting;
  if (nAcked == nWaiting) {
    byte_529041 = 1;
  } else if (nAcked < nWaiting) {
    byte_529041 = 2;
    SNetDisconnectAll(0x40000006);
    return;
  }

  pStatus = sgdwPlayerStatus_g;
  for (i = 0; i < 8; ++i) {
    DWORD dwStatus = *pStatus;

    if ((dwStatus & 0x10000) && !(dwStatus & 0x40000))
      net_drop_player(i, 0, 0x40000006);
    ++pStatus;
  }
}

// Game seconds so far: the banked count plus the current run, unsigned-divided
// by 1000.
int net_elapsed_gametime()
{
  return sgdwElapsed_g + (GetTickCount() - sgdwStartTime_g) / 1000;
}

// The same reading, but frozen at the stop time once the clock is stopped.
int sub_47A780()
{
  DWORD dwNow;

  if (sgbGameTicksStarted_g)
    dwNow = GetTickCount();
  else
    dwNow = sgdwEndTime_g;

  return sgdwElapsed_g + (dwNow - sgdwStartTime_g) / 1000;
}

// Starts the game clock. The timeout deadline is seeded behind the start time.
void net_time_init()
{
  DWORD dwNow = GetTickCount();

  sgdwStartTime_g = dwNow;
  sgdwEndTime_g = dwNow;
  sgbGameTicksStarted_g = 1;
  sgdwElapsed_g = 0;
  sgdwTimeoutTime_g = dwNow - 0x1D4C0;
}

// Stops the game clock, remembering when.
void sub_47A7F0()
{
  if (sgbGameTicksStarted_g) {
    sgdwEndTime_g = GetTickCount();
    sgbGameTicksStarted_g = 0;
  }
}

// Pumps the message loop while the drop dialog is up, ticking the countdown
// once a second, and parks the timeout deadline on the way out. voice_squelch
// is 0x4BE950, a one-byte `ret` several empty functions share; the empty
// function called here takes no arguments, hence the nullary pointer cast.
void sub_47A810()
{
  DWORD dwLast;

  if (gbTimeOutDialogActive != 0)
    return;

  gbTimeOutDialogActive = 1;
  do_timeout();
  reinterpret_cast<void (__fastcall *)()>(&voice_squelch)();
  dword_67501C = 0;
  dwLast = GetTickCount();

  while (byte_672665 == 0 && dword_67501C == 0) {
    netmgr_process_messages();
    reinterpret_cast<void (__fastcall *)()>(&voice_squelch)();
    GetTurnPackets();
    RunEvtDispatcher(3);
    timeout_update_got_msg();

    if (GetTickCount() > dwLast + 0x3E8) {
      timeout_update_countdown();
      dwLast = GetTickCount();
    }
    img_update();
  }

  if (sgpTimeoutDlg != NULL) {
    dlg_deactivate(sgpTimeoutDlg);
    sgpTimeoutDlg = NULL;
  }
  img_update();

  gbTimeOutDialogActive = 0;
  sgdwTimeoutTime_g = GetTickCount();
}

// Shuffles nArg1 dwords at nArg0 in place through a scratch pool, off the
// game's fixed seed so every machine draws the same order. The removal span is
// measured from the original count, not the shrinking one, as in 1.17.
void sub_47A8D0(intptr_t nArg0, int nArg1)
{
  int *pOut = reinterpret_cast<int *>(nArg0);
  int aPool[8];
  DWORD nCount = static_cast<DWORD>(nArg1);
  DWORD nLeft;
  DWORD i;

  memcpy(aPool, pOut, nCount * 4);
  OcSrand(net_get_fixed_seed());

  nLeft = nCount;
  for (i = 0; i < nCount; ++i) {
    DWORD k = static_cast<DWORD>(OcRand()) % nLeft;

    pOut[i] = aPool[k];
    memmove(&aPool[k], &aPool[k + 1], (nCount - k) * 4 - 4);
    --nLeft;
  }
}

// Dresses the drop dialog once it is loaded: background, layout, the player
// rows, then the OK button and the countdown label.
void timeout_activate()
{
  ctrl_t *pBtn;
  ctrl_t *pLabel;
  DWORD nGray;

  dfl_load_bmp(sgpTimeoutDlg, NULL);
  opt_dlg_set_blend(sgpTimeoutDlg);
  sgpTimeoutDlg->dwFlags |= 0x20000000;
  timeout_update_got_msg();

  pBtn = ctrl_find_ndx(sgpTimeoutDlg, 2);
  nGray = dword_675020;
  pBtn->pfnEvt = reinterpret_cast<PFN_CTRL_EVT>(&big_menu_btn_fcn);
  ctrl_set_gray(pBtn, nGray);

  pLabel = ctrl_find_ndx(sgpTimeoutDlg, -30);
  psnprintf(sgszTimeoutMsg_g, 0x20, "%d:%02d",
            dword_675020 / 60, dword_675020 % 60);
  pLabel->pData = sgszTimeoutMsg_g;
  ctrl_set_update(pLabel);
  ctrl_showhide(pLabel, dword_675020);
}

// The drop dialog's event handler, a switch over dlgevt_t::dwId behind a wTag
// == 0x0E gate; ids 3 to 9 take the shared tail. Case 10 comes first, as its
// block does in 1.17.
BOOL timeout_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  BOOL bRet;

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 10:
        sgpTimeoutDlg = pCtrl;
        break;
      case 0:
        timeout_activate();
        break;
      case 1:
        modeless_dlg_shutdown(0);
        sgpTimeoutDlg = NULL;
        break;
      case 2:
        switch (gnDlgReturnVal) {
          case 2:
            sub_47A630();
            break;
        }
        return TRUE;
    }
  }

  bRet = modeless_opt_dlg(pCtrl, pEvent);
  if (bRet == 0)
    bRet = TRUE;
  return bRet;
}

// Puts the drop dialog up, seeded with the seconds left. 0x1D4C0 is 120000 ms,
// the span net_time_init seeds the deadline behind the start time.
void do_timeout()
{
  DWORD nSeconds;

  if (GetTickCount() > sgdwTimeoutTime_g + 0x1D4C0) {
    nSeconds = 45;
  } else {
    nSeconds = dword_675020;
    if (nSeconds >= 2)
      nSeconds -= 2;
    else
      nSeconds = 0;
    nSeconds = nSeconds > 45 ? 45 : nSeconds;
  }

  dword_675020 = nSeconds;
  modeless_dlg_startup(sgTimeoutDlgTemplate, (void *)&timeout_dlg_fcn);
  gbDlgLevel = 0;
  img_update();
}

// Ticks the countdown label down one second, and frees the OK button once it
// reaches zero. The label text is minutes and seconds.
void timeout_update_countdown()
{
  ctrl_t *pLabel;

  if (sgpTimeoutDlg == NULL)
    return;

  if (dword_675020 != 0)
    --dword_675020;

  pLabel = ctrl_find_ndx(sgpTimeoutDlg, -30);
  psnprintf(sgszTimeoutMsg_g, 0x20, "%d:%02d",
            dword_675020 / 60, dword_675020 % 60);
  pLabel->pData = sgszTimeoutMsg_g;
  ctrl_set_update(pLabel);

  if (dword_675020 == 0) {
    ctrl_hide(pLabel);
    ctrl_ungray(ctrl_find_ndx(sgpTimeoutDlg, 2));
  }
}

// Refills the drop dialog's player rows from the slots still waiting, in a
// seed-shuffled order, and hides the rows left over. The name pointer is two
// arms because 1.17 re-tests gPlayback inside the row loop.
void timeout_update_got_msg()
{
  DWORD aSlots[8];
  PlayerData *pSlots;
  PlayerData *p;
  ctrl_t *pRow;
  DWORD nCount;
  DWORD i;
  DWORD j;

  if (sgpTimeoutDlg == NULL)
    return;

  pSlots = gPlayback ? gPlayerSlots_g.a : gPlayerData.a;
  nCount = 0;
  pRow = ctrl_find_ndx(sgpTimeoutDlg, -10);
  p = pSlots;

  for (i = 0; i < 8; ++i) {
    if (pRow == NULL)
      break;

    if (p->nType == 2) {
      DWORD dwStatus = sgdwPlayerStatus_g[p->dwStormID];

      if (!(dwStatus & 0x20000) && (dwStatus & 0x10000))
        aSlots[nCount++] = i;
    }
    ++p;
  }

  sub_47A8D0(reinterpret_cast<intptr_t>(aSlots), nCount);

  for (j = 0; j < nCount; ++j) {
    DWORD nSlot = aSlots[j];
    DWORD nOwner;

    pRow->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(&color_draw);
    if (gPlayback)
      pRow->pData = gPlayerSlots_g[nSlot].szName;
    else
      pRow->pData = pSlots[nSlot].szName;

    nOwner = nSlot;
    if (IsTeamGame())
      nOwner = team_get_slot_captain(pSlots[nSlot].nTeam);

    // ctrl_t +0x26, a DWORD inside dialog.h's _unmapped24[6] window.
    *reinterpret_cast<DWORD *>(&pRow->_unmapped24[2]) = nOwner;
    ctrl_show(pRow);
    ctrl_set_update(pRow);
    pRow = pRow->pNext;
  }

  while (pRow != NULL) {
    ctrl_hide(pRow);
    pRow = pRow->pNext;
  }
}
