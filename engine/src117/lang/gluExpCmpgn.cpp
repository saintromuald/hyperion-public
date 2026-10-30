// gluExpCmpgn.cpp -- the expansion campaign select screen: the mission list,
// its per-entry FLC animation and the three gates on finishing the previous
// campaign. gluCmpgn.cpp's twin, shape for shape; bodies in oracle VA order.
//
// Two bodies here are shared with the base-game screen: ctrl_cmpgn_flc_draw
// (0x458D50) and ctrl_cmpgn_flc_animate (0x458ED0) have their address taken
// twice and gluCmpgn.cpp's ctrl_cmpgn_flc_conditional reaches both by absolute
// address, so they are defined once, here, and gluCmpgn.cpp sees them through
// gluExpCmpgn.h.
//
// The oracle keeps do_xcampaign_dlg's call to xcampaign_preinit a real call,
// so an /Ob2 splice of the two would show at once.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>   // _stricmp

#include "../h/platform.h"


#include "../h/gluExpCmpgn.h"
#include "../h/dialog.h"     // ctrl_t, dlgevt_t, PFN_CTRL_EVT, PFN_CTRL_DRAW
#include "../h/dlg_ctrl.h"   // ctrl_find_ndx / ctrl_show / ctrl_hide / ctrl_dlg
#include "../h/dlg_dlg.h"    // dlg_run_modal
#include "../h/glues.h"      // glue_load_cd_dlg / glues_clear_all / glue_init_screen_dlg
#include "../port/front/port_front.h"  // Hyperion: PortFrontCampaign
#include "../h/gluRdy.h"     // glues_transition_out, snd_play_music
#include "../h/minimap.h"    // timer_create / timer_remove_node / PFN_TIMER_FN
#include "../h/net_cmds.h"   // error_get_str
#include "../h/net_error.h"  // err_popup
#include "../h/point16.h"     // point16_t for global_evt_2_local_point
#include "../h/rect16.h"      // rect16_t for pt_in_rect
#include "../h/snd.h"        // snd_music_enabled
#include "../gen/globals117.h"

// The six external callees this TU reaches are declared in headers: ctrl_flc,
// ctrl_btnglow_flc, flc_anim_update_fn and draw_flc in src117/h/dlg_flc.h, and
// ReadCharacterFile, do_err_okcancel_dlg and sub_4BB5B0 in gluCmpgn.h, which
// gluExpCmpgn.h includes.

// The globals this TU reaches, dual-spelled, one `#ifndef` per name. Three come
// from ../gen/globals117.h instead: glGluesMode @0x66A274,
// gbMultiPlayerMode @0x672664 and gLocalPlayerName @0x672690.

// 0x6CE6DC gnDlgReturnVal -- the modal result, a signed long.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

// 0x66A278 dword_66A278 -- written `= 0` by do_xcampaign_dlg and do_cmpgn_dlg
// and read nowhere in this slice, so the width is derived and the role is not;
// the name is the address.
#ifdef OC_DLL_MODE
  #ifndef dword_66A278
  #define dword_66A278 (*reinterpret_cast<DWORD *>(OC_VA(0x66A278u)))
  #endif
#else
extern DWORD dword_66A278;
#endif

// 0x66A8A4 nExpCmpgnCompletionMsgId_g -- three signed dwords, holding -10, -11
// and -12. The extent is proven from both sides: xcampaign_preinit clears
// exactly 12 bytes and ctrl_xcmpgn_flc_activate subscripts the run, while the
// next named .data entry is gluJoin.cpp's sgpJoinDlg @0x66A8B0.
#ifdef OC_DLL_MODE
  #ifndef nExpCmpgnCompletionMsgId_g
  #define nExpCmpgnCompletionMsgId_g (reinterpret_cast<LONG *>(OC_VA(0x66A8A4u)))
  #endif
#else
extern LONG nExpCmpgnCompletionMsgId_g[3];
#endif

// 0x5080CC sExpCmpgnAnims_g and 0x5080D4 sExpCmpgnEvtFcnTbl_g, both .data. The
// table's size is not inferred: gluexpcmpgn_dlg_fcn passes 0x2C as
// glue_init_screen_dlg's cbTable, so eleven entries, and entries 5 and 7 are
// this TU's own ctrl_xcampaign_flc_conditional, which is what makes that body a
// PFN_CTRL_EVT. The anims record is the 8 bytes glues_transition_in takes.
#ifdef OC_DLL_MODE
  #ifndef sExpCmpgnAnims_g
  #define sExpCmpgnAnims_g (reinterpret_cast<void *>(OC_VA(0x5080CCu)))
  #endif
#else
extern BYTE sExpCmpgnAnims_g[8];
#endif
#ifdef OC_DLL_MODE
  #ifndef sExpCmpgnEvtFcnTbl_g
  #define sExpCmpgnEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x5080D4u)))
  #endif
#else
extern PFN_CTRL_EVT sExpCmpgnEvtFcnTbl_g[11] OC_UNALIGNED;
#endif

// 0x4EDAC4 -- the `.rdata` dialog template, the ASCII "rez\gluExpCmpgn.bin" at
// that address. The address is the value, so this is an object whose address is
// taken rather than a pointer variable: `extern void *const` would cost a load
// where the oracle has `mov ecx, 4EDAC4h`.
#ifdef OC_DLL_MODE
  #ifndef sgExpCmpgnDlgTemplate
  #define sgExpCmpgnDlgTemplate (*reinterpret_cast<char (*)[1]>(OC_VA(0x4EDAC4u)))
  #endif
#else
extern char sgExpCmpgnDlgTemplate[1];
#endif

// The bodies, in oracle VA order.

// 0x458B00 do_xcampaign_dlg (181 B). Runs the expansion campaign screen modally
// and turns its return code into the glue state machine's next screen. The
// `add eax, -5h / cmp eax, 6h / jnbe` is a dense switch over 5..11 whose table
// sits in this body's tail; the case order below is the memory order of the
// arms -- 8, 7, 6, 5, 10, 11, 9, default -- not the table's. Each arm ends
// `jmp glues_clear_all`, MSVC duplicating one tail call into all eight.
void do_xcampaign_dlg() {
  dword_66A278 = 0;
  gbMultiPlayerMode = 0;
  // Hyperion: Hyperion's campaign screen in this one's place when the front end is on
  // (port_front_campaign.cpp): its episodes are both games', and it names the glue screen next.
  const int nFront = PortFrontCampaign();
  if (nFront >= 0) {
    glGluesMode = nFront;
    glues_clear_all();
    return;
  }
  xcampaign_preinit();

  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(
      glue_load_cd_dlg(reinterpret_cast<intptr_t>(sgExpCmpgnDlgTemplate)));

  switch (dlg_run_modal(pDlg, 0,
                        reinterpret_cast<PFN_CTRL_EVT>(gluexpcmpgn_dlg_fcn))) {
    case 8:
      glGluesMode = 8;
      break;
    case 7:
      glGluesMode = 7;
      break;
    case 6:
      glGluesMode = 9;
      break;
    case 5:
      glGluesMode = 0x0D;
      break;
    case 10:
      glGluesMode = 0x0B;
      break;
    case 11:
      glGluesMode = 0x0C;
      break;
    case 9:
      glGluesMode = 5;
      break;
    default:
      glGluesMode = 0;
      break;
  }
  glues_clear_all();
}

// 0x458BE0 gluExpCmpgn_dlg_cmd (232 B). glucmpgn_dlg_cmd's twin, byte for byte
// apart from the three campaign slots and the three locked-campaign string ids,
// 0x90 / 0x8F / 0x90 here against the base game's 0x8D / 0x8D / 0x8E. The case
// order is 8, 7, 6, read off the arms' addresses rather than off MSVC's compare
// chain. The guard is one `||` with the early exit inside it, not two
// sequential `if (...) return TRUE;` statements: all six failure paths share
// one `mov eax, 1h` block, and only this shape puts it where the oracle has it.
BOOL gluExpCmpgn_dlg_cmd(intptr_t nArg0) {
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);

  switch (gnDlgReturnVal) {
    case 8:
      if (nExpCmpgnCompletionMsgId_g[0] == 0 ||
          do_err_okcancel_dlg(error_get_str(0x90))) {
        if (!sub_4BB5B0(0))
          return TRUE;
        gnDlgReturnVal = 8;
        break;
      }
      return TRUE;
    case 7:
      if (nExpCmpgnCompletionMsgId_g[1] == 0 ||
          do_err_okcancel_dlg(error_get_str(0x8F))) {
        if (!sub_4BB5B0(1))
          return TRUE;
        gnDlgReturnVal = 7;
        break;
      }
      return TRUE;
    case 6:
      if (nExpCmpgnCompletionMsgId_g[2] == 0 ||
          do_err_okcancel_dlg(error_get_str(0x90))) {
        if (!sub_4BB5B0(2))
          return TRUE;
        gnDlgReturnVal = 6;
        break;
      }
      return TRUE;
  }
  return glues_transition_out(pDlg);
}

// 0x458CD0 gluexpcmpgn_dlg_fcn (114 B). glucmpgn_dlg_fcn's twin, differing only
// in the two .data tables and the command handler it tail-jumps to. `sub eax,
// 0h` is how MSVC lowers a dense switch starting at 0 as a compare chain, so
// the `dwId == 0` arm costs a subtract rather than a `test`; the arms are laid
// out 10, 2, 0, which is the source order below.
int gluexpcmpgn_dlg_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 10:
        glue_init_screen_dlg(pDlg, sExpCmpgnEvtFcnTbl_g, 0x2C, 0);
        glues_transition_in(nArg0, sExpCmpgnAnims_g, 2,
                            2, 0);
        break;
      case 2:
        return gluExpCmpgn_dlg_cmd(nArg0);
      case 0:
        glue_screen_activate(pDlg);
        if (!snd_music_enabled())
          snd_play_music(0x16);
        break;
    }
  }
  return ctrl_dlg(pDlg, pEvent);
}

// 0x458D50 ctrl_cmpgn_flc_draw (45 B). The FLC control's draw handler, a
// PFN_CTRL_DRAW both campaign screens install into ctrl_t::pfnDraw. It forwards
// all four arguments to draw_flc untouched, with the node's mouse-is-inside bit
// (0x80) suppressed for the duration whenever the node is disabled (0x02), then
// puts the flags back as they were. ECX and EDX are never rewritten, so only
// the two stack words are re-pushed.
void ctrl_cmpgn_flc_draw(intptr_t nArg0, int nArg1, int nArg2, intptr_t nArg3) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlg_dword_t dwSaved = pCtrl->dwFlags;

  if (dwSaved & 2)
    pCtrl->dwFlags = dwSaved & ~0x80u;
  draw_flc(pCtrl, nArg1, nArg2, reinterpret_cast<rect16_t *>(nArg3));
  pCtrl->dwFlags = dwSaved;
}

// 0x458D80 ctrl_xcampaign_flc_conditional (220 B). ctrl_cmpgn_flc_conditional's
// twin, entries 5 and 7 of sExpCmpgnEvtFcnTbl_g, differing only in the activate
// callback it arms on dwId 0: ctrl_xcmpgn_flc_activate here,
// ctrl_cmpgn_flc_activate there. The draw handler and the animation timer are
// the shared bodies this TU defines, so both screens push the same addresses.
//
// The arm endings are not interchangeable: dwId 0 has to end in an explicit
// `return ctrl_flc(pCtrl, pEvent);` and dwId 1 in a `break`, and the wTag 4/6
// arm's `return 0` has to be its last statement, since that is where the oracle
// keeps each shared tail. ctrl_t +0x36 is read as a rect16_t here, a third
// reading of the unnamed per-type payload dialog.h describes: `lea ecx,
// [esi+36h]` straight into pt_in_rect, whose first argument dlg_ctrl.h declares
// `const rect16_t *`. The field is not renamed on one family's evidence.
int ctrl_xcampaign_flc_conditional(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);
  point16_t pt;
  dlg_dword_t dwFlags;

  switch (pEvent->wTag) {
    case 0x0E:
      switch (pEvent->dwId) {
        case 10:
          ctrl_flc(pCtrl, pEvent);
          pCtrl->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(ctrl_cmpgn_flc_draw);
          timer_create(pCtrl, 0x48, 0x1E,
                       reinterpret_cast<PFN_TIMER_FN *>(ctrl_cmpgn_flc_animate));
          return 1;
        case 0:
          timer_create(
              pCtrl, 2, 0xC8,
              reinterpret_cast<PFN_TIMER_FN *>(ctrl_xcmpgn_flc_activate));
          return ctrl_flc(pCtrl, pEvent);
        case 1:
          timer_remove_node(pCtrl, 2);
          break;
        case 4:
          global_evt_2_local_point(pCtrl, pEvent, &pt);
          if (pt_in_rect(reinterpret_cast<const rect16_t *>(pCtrl->_unmapped36),
                         &pt)) {
            dwFlags = pCtrl->dwFlags;
            if (dwFlags & 8)
              return ((dlg_byte_t)dwFlags >> 4) & 1;
          }
          return 0;
      }
      break;
    case 4:
    case 6:
      if (!(pCtrl->dwFlags & 2))
        break;
      return 0;
  }
  return ctrl_flc(pCtrl, pEvent);
}

// 0x458E80 ctrl_xcmpgn_flc_activate (68 B). The 200 ms timer the expansion
// screen arms on the FLC control's activate event: show or hide the sibling
// control whose index is parked in nExpCmpgnCompletionMsgId_g, by this node's
// 0x80 bit. The index mapping is the one place this body and its base-game twin
// differ -- nIndex 7 to slot 1 here, nIndex 6 to slot 2 there, with nIndex 8
// going to slot 0 in both -- and it is the whole two-byte size difference.
void ctrl_xcmpgn_flc_activate(intptr_t nArg0) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  int nSlot;

  switch (pCtrl->nIndex) {
    case 7:
      nSlot = 1;
      break;
    case 8:
      nSlot = 0;
      break;
    default:
      return;
  }

  if (nExpCmpgnCompletionMsgId_g[nSlot] == 0)
    return;

  ctrl_t *pTarget = ctrl_find_ndx(
      pCtrl->pParent, static_cast<short>(nExpCmpgnCompletionMsgId_g[nSlot]));
  if (pCtrl->dwFlags & 0x80)
    ctrl_show(pTarget);
  else
    ctrl_hide(pTarget);
}

// 0x458ED0 ctrl_cmpgn_flc_animate (33 B). The 30 ms animation timer both
// campaign screens arm: the same save, suppress and restore of the 0x80 bit as
// ctrl_cmpgn_flc_draw, around flc_anim_update_fn instead of draw_flc, and with
// no stack arguments to forward.
void ctrl_cmpgn_flc_animate(intptr_t nArg0) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlg_dword_t dwSaved = pCtrl->dwFlags;

  if (dwSaved & 2)
    pCtrl->dwFlags = dwSaved & ~0x80u;
  flc_anim_update_fn(pCtrl);
  pCtrl->dwFlags = dwSaved;
}

// 0x458F00 xcampaign_preinit (178 B). Decides, before the screen is built, which
// expansion campaigns are locked and why: each of the three slots holds a
// negative message id, -10, -11 or -12, or zero for unlocked, and
// gluExpCmpgn_dlg_cmd turns a non-zero slot into a prompt.
//
// The 0x70-byte record is ReadCharacterFile's: a 0x112233 magic at +0x00, a
// BYTE at +0x04, a _time64 stamp at +0x08, a char[0x19] name at +0x0C and a
// 0x3C-byte block at +0x34 that it clears separately. It is spelled as a raw
// BYTE[0x70] with offset casts rather than a named struct because gluCmpgn.cpp
// reads the same record at different offsets -- it gates on +0x30 and reads
// +0x28 and +0x2C where this screen gates on +0x34 and reads +0x38 and +0x3C --
// so the expansion's progress counters are inside that separately cleared block.
void xcampaign_preinit() {
  BYTE rec[0x70];

  nExpCmpgnCompletionMsgId_g[2] = 0;
  memset(nExpCmpgnCompletionMsgId_g, 0, 8);

  if (!ReadCharacterFile(gLocalPlayerName, rec)) {
    if (_stricmp(gLocalPlayerName, error_get_str(0x48)) != 0)
      err_popup(0x58, reinterpret_cast<intptr_t>(gLocalPlayerName), 0, 0, 0);
    return;
  }

  DWORD dwA = *reinterpret_cast<DWORD *>(rec + 0x38);
  DWORD dwB = *reinterpret_cast<DWORD *>(rec + 0x3C);

  if (dwA == 0) {
    if (dwB <= 0x27)
      nExpCmpgnCompletionMsgId_g[1] = -10;
  }
  if (*reinterpret_cast<DWORD *>(rec + 0x34) != 0)
    return;
  if (dwB <= 0x27) {
    nExpCmpgnCompletionMsgId_g[0] = -11;
    return;
  }
  if (dwA <= 0x30)
    nExpCmpgnCompletionMsgId_g[0] = -12;
}
