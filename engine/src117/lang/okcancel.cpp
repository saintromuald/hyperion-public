// okcancel.cpp -- the shared OK and OK/Cancel message boxes. Bodies are in
// oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers on some declarations are for, and why moving a
// body between TUs can change the code generated.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>          // strlen and strcpy, both inlined by /Oi here

#include "../h/okcancel.h"
#include "../h/dlg_effects.h"
#include "../h/dlg_draw.h"   // draw_ctrl_text
#include "../h/dlg_ctrl.h"   // ctrl_find_ndx
#include "../h/dlg_dlg.h"    // dlg_deactivate, dlg_load, dlg_run_modal, img_update
#include "../h/menu_dlg.h"   // title_position, set_size_enter, set_size_escape
#include "../h/opt_dlgs.h"   // dfl_load_bmp, opt_dlg_set_blend, opt_get_tile_grp, sub_47C110,
                             // modeless_menu_deactivate, modeless_dlg_shutdown
#include "../h/save_dlg.h"   // modeless_dlg_startup, modeless_opt_dlg, and the
                             // declarations of modal_ok_dlg / modal_okcancel_dlg
#include "../h/glues.h"      // glue_init_screen_dlg, alloc_load_bmp
#include "../h/snd.h"        // load_game_data
#include "../h/cursor.h"     // cursor_set
#include "../h/Storm.h"      // SMemAlloc, SMemFree
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// sub_4CA820 (0x4CA820) is declared in src117/h/dlg_effects.h, included above,
// since that TU defines it.

// dword_508D6C -- a DWORD @0x508D6C, compared against zero as a full dword and
// set to 1 by modal_ok_dlg_aux. The name is positional: it has no other.
#ifdef OC_DLL_MODE
  #ifndef dword_508D6C
  #define dword_508D6C (*reinterpret_cast<DWORD *>(OC_VA(0x508D6Cu)))
  #endif
#else
extern DWORD dword_508D6C;
#endif

// sgpszMsg -- char * @0x675050. The SMemAlloc'd
// copy of the caller's message; okcancel_activate hands it to the text
// control's pData.
#ifdef OC_DLL_MODE
  #ifndef sgpszMsg
  #define sgpszMsg (*reinterpret_cast<char **>(OC_VA(0x675050u)))
  #endif
#else
extern char *sgpszMsg;
#endif

// sgpszWaitMsg -- char * @0x675054. The SMemAlloc'd copy of the wait box's message; compared against zero and stored
// as a full dword.
#ifdef OC_DLL_MODE
  #ifndef sgpszWaitMsg
  #define sgpszWaitMsg (*reinterpret_cast<char **>(OC_VA(0x675054u)))
  #endif
#else
extern char *sgpszWaitMsg;
#endif

// gbDlgLevel -- BYTE @0x671A81, the dialog stack's nesting level.
// sub_47B090 clears it with `mov byte ptr [..], 0h`.
#ifdef OC_DLL_MODE
  #ifndef gbDlgLevel
  #define gbDlgLevel (*reinterpret_cast<BYTE *>(OC_VA(0x671A81u)))
  #endif
#else
extern BYTE gbDlgLevel;
#endif

// sgbSmallBmp -- DWORD @0x675058, tested as a
// full dword and cleared as one.
#ifdef OC_DLL_MODE
  #ifndef sgbSmallBmp
  #define sgbSmallBmp (*reinterpret_cast<DWORD *>(OC_VA(0x675058u)))
  #endif
#else
extern DWORD sgbSmallBmp;
#endif

// sgpszTileGrp_g -- const char * @0x67505C.
// okcancel_activate loads it into EDX for dfl_load_bmp, whose second parameter
// opt_dlgs.h already spells const char *.
#ifdef OC_DLL_MODE
  #ifndef sgpszTileGrp_g
  #define sgpszTileGrp_g (*reinterpret_cast<const char **>(OC_VA(0x67505Cu)))
  #endif
#else
extern const char *sgpszTileGrp_g;
#endif

// sgpCompleteFcn -- PFN_COMPLETE @0x675060, the box's completion callback.
#ifdef OC_DLL_MODE
  #ifndef sgpCompleteFcn
  #define sgpCompleteFcn (*reinterpret_cast<PFN_COMPLETE *>(OC_VA(0x675060u)))
  #endif
#else
extern PFN_COMPLETE sgpCompleteFcn;
#endif

// gnDlgReturnVal -- long @0x6CE6DC, spelled as src117/lang/dlg_dlg.cpp spells
// it.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

// The bodies, in oracle VA order.

// Draws the box's message text, centred, when the control has one. gbFontJust
// 0x28 is this dialog's justification; draw_ctrl_text saves and restores it, so
// setting it immediately before the call is the whole protocol.
void okcancel_draw_fcn(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  if (pCtrl->pData != nullptr) {
    gbFontJust = 0x28;
    draw_ctrl_text(pCtrl, 0, 0, nullptr);
  }
}

// The oracle interleaves the three global stores among the pushes below, where
// these two emit them after.
// The modal one-button box: copy the message, load rez\ok.bin and run it to
// completion. The strlen and strcpy are /Oi expansions off the local, not
// calls; the copy runs off the local for the same reason modal_ok_dlg_aux's
// does.
void modeless_ok_dlg(const char *pszText, int nSmallBmp, const char *pszTileGrp)
{
  char *psz = static_cast<char *>(
      SMemAlloc((DWORD)(strlen(pszText) + 1), "okcancel.cpp", 163, 0));
  sgpszMsg = psz;
  strcpy(psz, pszText);

  sgbSmallBmp = nSmallBmp;
  dword_508D6C = nSmallBmp;
  sgpszTileGrp_g = pszTileGrp;

  ctrl_t *pDlg = dlg_load("rez\\ok.bin",
                          reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                          reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                          "okcancel.cpp", 167);
  glue_init_screen_dlg(pDlg, nullptr, 0, 0);
  dlg_run_modal(pDlg, 0, okcancel_dlg_fcn);
}


// The modal two-button box. Same body, a different .bin, and it returns the
// button. The return is what costs the third callee-saved register; spelled
// void it is one byte short.
int modeless_okcancel_dlg(const char *pszText, int nSmallBmp,
                          const char *pszTileGrp)
{
  char *psz = static_cast<char *>(
      SMemAlloc((DWORD)(strlen(pszText) + 1), "okcancel.cpp", 174, 0));
  sgpszMsg = psz;
  strcpy(psz, pszText);

  sgbSmallBmp = nSmallBmp;
  dword_508D6C = nSmallBmp;
  sgpszTileGrp_g = pszTileGrp;

  ctrl_t *pDlg = dlg_load("rez\\okcancel.bin",
                          reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                          reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                          "okcancel.cpp", 178);
  glue_init_screen_dlg(pDlg, nullptr, 0, 0);
  return dlg_run_modal(pDlg, 0, okcancel_dlg_fcn);
}

// The one-button box.
void modal_ok_dlg(const char *pszText, void *pfnResult)
{
  modal_ok_dlg_aux(pszText, reinterpret_cast<PFN_COMPLETE>(pfnResult),
                   "rez\\ok.bin");
}

// Takes a private copy of the message and brings the box up modeless. It is a
// no-op when a box is already up, which is what the two guards test. The
// strlen and strcpy are both /Oi expansions, not calls.
void modal_ok_dlg_aux(const char *pszText, PFN_COMPLETE pfnComplete,
                      const char *pszDlgFile)
{
  if (sgpszMsg != nullptr)
    return;

  if (sgpCompleteFcn != nullptr)
    return;

  sgpCompleteFcn = pfnComplete;
  // The copy runs off the local, not off the global: that is what picks the
  // oracle's one-pointer strcpy expansion.
  char *psz = static_cast<char *>(
      SMemAlloc((DWORD)(strlen(pszText) + 1), "okcancel.cpp", 137, 0));
  sgpszMsg = psz;
  strcpy(psz, pszText);

  dword_508D6C = 1;
  sgbSmallBmp = 0;
  sgpszTileGrp_g = nullptr;
  modeless_dlg_startup(const_cast<char *>(pszDlgFile),
                       reinterpret_cast<void *>(&okcancel_dlg_fcn));
}

// The two-button box. Same body, a different .bin.
void modal_okcancel_dlg(const char *pszText, void *pfnResult)
{
  modal_ok_dlg_aux(pszText, reinterpret_cast<PFN_COMPLETE>(pfnResult),
                   "rez\\okcancel.bin");
}

// Dresses the dialog once it exists: background, frame, the two size hints,
// then the message text and its draw handler on control 1.
void okcancel_activate(ctrl_t *pDlg)
{
  dfl_load_bmp(pDlg, sgpszTileGrp_g);

  if (dword_508D6C != 0) {
    if (sgbSmallBmp != 0) {
      pDlg->dwFlags |= 0x2040000;
      sub_4CA820(0x50, sub_47C110(opt_get_tile_grp(), 4, 0, 0), 1);
    } else {
      opt_dlg_set_blend(pDlg);
    }
  }

  set_size_enter(pDlg, 3, 0x20000000);
  set_size_escape(pDlg, 5, 0x20000000);

  ctrl_t *pText = ctrl_find_ndx(pDlg, 1);
  pText->pData = sgpszMsg;
  pText->pfnDraw = okcancel_draw_fcn;
}

// Drops the message copy before handing the event on. The text control's pData
// is cleared first because it aliases the block being freed.
BOOL okcancel_deactivate(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  {
    // __restrict: the payload's literal sgpszMsg read must not wait for this store.
    ctrl_t *__restrict pText = ctrl_find_ndx(pCtrl, 1);
    pText->pData = nullptr;
  }
  SMemFree(sgpszMsg, "okcancel.cpp", 83, 0);
  sgpszMsg = nullptr;
  return modeless_menu_deactivate(pCtrl, pEvent);
}

// The box's dialog procedure. Case 0 has no arm of its own -- it falls into the
// shared tail -- and the jump table at 0x47B010 sends ids 3 to 6 straight there.
BOOL okcancel_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        okcancel_activate(pCtrl);
        break;

      case 7:
        title_position(pCtrl);
        break;

      case 2:
        dlg_deactivate(pCtrl);
        if (sgpCompleteFcn != nullptr)
          sgpCompleteFcn(gnDlgReturnVal);
        sgpCompleteFcn = nullptr;
        return TRUE;

      case 1:
        okcancel_deactivate(pCtrl, pEvent);
        return TRUE;
    }
  }

  return modeless_opt_dlg(pCtrl, pEvent);
}

// Brings up the modeless "please wait" box. A box already up is a no-op, which
// is the single guard at the top; img_update is a tail jmp, so it must be the
// last statement.
void do_wait_dlg(const char *pszText)
{
  if (sgpszWaitMsg != nullptr)
    return;

  char *psz = static_cast<char *>(
      SMemAlloc((DWORD)(strlen(pszText) + 1), "okcancel.cpp", 226, 0));
  sgpszWaitMsg = psz;
  strcpy(psz, pszText);

  modeless_dlg_startup(const_cast<char *>("rez\\wait.bin"),
                       reinterpret_cast<void *>(&wait_dlg_fcn));
  img_update();
}

// Tears the wait box down.
void sub_47B090()
{
  modeless_dlg_shutdown(1);
  gbDlgLevel = 0;
}

// The wait box's dialog procedure. Case 1 is the deactivate arm and drops the
// message copy; case 0 dresses the dialog. Both fall into the shared tail jmp.
BOOL wait_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 1:
        {
          // __restrict: as in okcancel_deactivate, for sgpszWaitMsg.
          ctrl_t *__restrict pText = ctrl_find_ndx(pCtrl, 1);
          pText->pData = nullptr;
        }
        SMemFree(sgpszWaitMsg, "okcancel.cpp", 208, 0);
        sgpszWaitMsg = nullptr;
        break;

      case 0: {
        dfl_load_bmp(pCtrl, nullptr);
        opt_dlg_set_blend(pCtrl);

        ctrl_t *pText = ctrl_find_ndx(pCtrl, 1);
        pText->pData = sgpszWaitMsg;
        pText->pfnDraw = okcancel_draw_fcn;
        cursor_set(1);
        break;
      }
    }
  }

  return modeless_opt_dlg(pCtrl, pEvent);
}
