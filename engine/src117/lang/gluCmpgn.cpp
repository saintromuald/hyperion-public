// gluCmpgn.cpp -- the campaign select screen: the mission list, its per-entry FLC
// animation and the three "you have not finished the previous campaign" gates.
// gluExpCmpgn.cpp is its twin, shape for shape. Bodies are in oracle VA order.
//
// Two of this screen's callbacks live in the other TU: ctrl_cmpgn_flc_draw (0x458D50) and
// ctrl_cmpgn_flc_animate (0x458ED0) are shared by both campaign screens and belong to
// gluExpCmpgn.cpp in the oracle's own TU map, despite carrying no `x` prefix.
// ctrl_cmpgn_flc_conditional reaches them by absolute address, so this TU includes
// gluExpCmpgn.h.
//
// TU membership cuts both ways. Two edges here: do_cmpgn_dlg -> cmpgn_preinit (178 B) and
// glucmpgn_dlg_fcn -> glucmpgn_dlg_cmd (232 B, a tail jmp). The oracle keeps both as real
// transfers, so an /Ob2 splice would cost the callee's whole size.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>   // _stricmp, memset

#include "../h/platform.h"


#include "../h/gluCmpgn.h"
#include "../h/gluExpCmpgn.h"  // ctrl_cmpgn_flc_draw / ctrl_cmpgn_flc_animate
#include "../h/dialog.h"       // ctrl_t, dlgevt_t, PFN_CTRL_EVT, PFN_CTRL_DRAW
#include "../h/dlg_ctrl.h"     // ctrl_find_ndx / ctrl_show / ctrl_hide / ctrl_dlg
#include "../h/dlg_dlg.h"      // dlg_run_modal
#include "../h/glues.h"        // glue_load_cd_dlg / glues_clear_all / glue_init_screen_dlg
#include "../port/front/port_front.h"  // Hyperion: PortFrontCampaign
#include "../h/gluRdy.h"       // glues_transition_out, snd_play_music
#include "../h/minimap.h"      // timer_create / timer_remove_node / PFN_TIMER_FN
#include "../h/net_cmds.h"     // error_get_str
#include "../h/net_error.h"    // err_popup
#include "../h/point16.h"      // point16_t for global_evt_2_local_point
#include "../h/rect16.h"       // rect16_t for pt_in_rect
#include "../h/snd.h"          // snd_music_enabled
#include "../gen/globals117.h"

// This TU's external callees are declared in headers: ctrl_flc, ctrl_btnglow_flc,
// flc_anim_update_fn and draw_flc in src117/h/dlg_flc.h, which gluCmpgn, gluExpCmpgn and
// gluConn all read; ReadCharacterFile, do_err_okcancel_dlg and sub_4BB5B0 in gluCmpgn.h,
// which gluExpCmpgn.h includes.

// The globals this TU reaches, spelled both ways with one guard per name: a `#define`
// over OC_VA under OC_DLL_MODE, a plain `extern` otherwise. glGluesMode @0x66A274,
// gbMultiPlayerMode @0x672664 and gLocalPlayerName @0x672690 are not respelled here; they
// come from ../gen/globals117.h. The four guarded names below are spelled identically in
// the twin TU.

// 0x6CE6DC gnDlgReturnVal, long. The same spelling gluChat.cpp, gluModem.cpp and
// gluPopup.cpp carry.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

// 0x66A278 -- written `= 0` by do_cmpgn_dlg and by do_xcampaign_dlg, read
// nowhere in this slice. `mov dword ptr [0x66A278], 0h`, so the width is proven
// and the role is not; the name is the address, which is also the spelling
// sc-refs/sheets/data_naming_1.17.csv carries (glues.cpp, dword_66A278).
#ifdef OC_DLL_MODE
  #ifndef dword_66A278
  #define dword_66A278 (*reinterpret_cast<DWORD *>(OC_VA(0x66A278u)))
  #endif
#else
extern DWORD dword_66A278;
#endif

// 0x668A2C nCmpgnCompletionMsgId_g -- three DWORDs: cmpgn_preinit clears exactly 12 bytes
// of it, glucmpgn_dlg_cmd reads all three, and the next named .data entry is at 0x668A38.
// Signed, since the values stored are -10, -11 and -12.
#ifdef OC_DLL_MODE
  #ifndef nCmpgnCompletionMsgId_g
  #define nCmpgnCompletionMsgId_g (reinterpret_cast<LONG *>(OC_VA(0x668A2Cu)))
  #endif
#else
extern LONG nCmpgnCompletionMsgId_g[3];
#endif

// 0x503D14 sCmpgnAnims_g and 0x503D1C sCmpgnEvtFcnTbl_g, both `.data`. The event table
// holds 11 entries: glucmpgn_dlg_fcn passes 0x2C as glue_init_screen_dlg's cbTable, and
// the image holds four NULLs then 0x4C83B0, 0x453FE0, 0x4C82F0, 0x453FE0, ... --
// ctrl_cmpgn_flc_conditional at entries 5 and 7, which is what makes that row a
// PFN_CTRL_EVT. The anims record is the 8 bytes glues_transition_in takes in EDX.
#ifdef OC_DLL_MODE
  #ifndef sCmpgnAnims_g
  #define sCmpgnAnims_g (reinterpret_cast<void *>(OC_VA(0x503D14u)))
  #endif
#else
extern BYTE sCmpgnAnims_g[8];
#endif
#ifdef OC_DLL_MODE
  #ifndef sCmpgnEvtFcnTbl_g
  #define sCmpgnEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x503D1Cu)))
  #endif
#else
extern PFN_CTRL_EVT sCmpgnEvtFcnTbl_g[11] OC_UNALIGNED;
#endif

// 0x4ED704 -- the `.rdata` dialog template, the ASCII "rez\gluCmpgn.bin" at that VA. The
// address is the value, so this is an object whose address is taken rather than a pointer
// variable: `extern void *const` would cost a 6-byte load where the oracle has the 5-byte
// `mov ecx, 4ED704h`. The name is local to this TU.
#ifdef OC_DLL_MODE
  #ifndef sgCmpgnDlgTemplate
  #define sgCmpgnDlgTemplate (*reinterpret_cast<char (*)[1]>(OC_VA(0x4ED704u)))
  #endif
#else
extern char sgCmpgnDlgTemplate[1];
#endif

// The bodies, in oracle VA order.

// 0x453D90 glucmpgn_dlg_cmd (232 B) -- the screen's command handler: three "play campaign
// N" buttons, each gated on its slot in nCmpgnCompletionMsgId_g. A non-zero slot means
// the campaign is locked, so the user gets error_get_str(<id>) in an OK/Cancel box and
// the command is abandoned unless they confirm; sub_4BB5B0 then starts the campaign and
// can still refuse.
//
// The case order is 8, 7, 6, read off the arms' addresses -- MSVC's compare chain
// ascends whatever the source order is. Case 8 compares against EAX, still the chain's
// known 0, where cases 7 and 6 are reached by a taken branch and get the immediate form.
BOOL glucmpgn_dlg_cmd(intptr_t nArg0) {
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);

  switch (gnDlgReturnVal) {
    case 8:
      if (nCmpgnCompletionMsgId_g[0] == 0 ||
          do_err_okcancel_dlg(error_get_str(0x8D))) {
        if (!sub_4BB5B0(0))
          return TRUE;
        gnDlgReturnVal = 8;
        break;
      }
      return TRUE;
    case 7:
      if (nCmpgnCompletionMsgId_g[1] == 0 ||
          do_err_okcancel_dlg(error_get_str(0x8D))) {
        if (!sub_4BB5B0(1))
          return TRUE;
        gnDlgReturnVal = 7;
        break;
      }
      return TRUE;
    case 6:
      if (nCmpgnCompletionMsgId_g[2] == 0 ||
          do_err_okcancel_dlg(error_get_str(0x8E))) {
        if (!sub_4BB5B0(2))
          return TRUE;
        gnDlgReturnVal = 6;
        break;
      }
      return TRUE;
  }
  return glues_transition_out(pDlg);
}

// 0x453E80 glucmpgn_dlg_fcn (114 B) -- the screen's dialog handler, installed by
// do_cmpgn_dlg as dlg_run_modal's PFN_CTRL_EVT. Everything it does not handle goes to
// ctrl_dlg, so three of its four exits are a tail `jmp`. The arms are laid out 10, 2, 0,
// which is the source order below; the ascending compare chain is MSVC's and says nothing
// about it. `push eax` for glue_init_screen_dlg's nFlags is the chain's known 0, a byte
// cheaper than the immediate.
int glucmpgn_dlg_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 10:
        glue_init_screen_dlg(pDlg, sCmpgnEvtFcnTbl_g, 0x2C, 0);
        glues_transition_in(nArg0, sCmpgnAnims_g, 2, 2,
                            0);
        break;
      case 2:
        return glucmpgn_dlg_cmd(nArg0);
      case 0:
        glue_screen_activate(pDlg);
        if (!snd_music_enabled())
          snd_play_music(0x16);
        break;
    }
  }
  return ctrl_dlg(pDlg, pEvent);
}

// 0x453F00 do_cmpgn_dlg (181 B) -- run the campaign screen modally and translate its
// return code into the glue state machine's next screen. do_xcampaign_dlg's twin apart
// from three addresses and the jump table's base.
//
// The dense switch over 5..11 keeps its seven-entry table in the COMDAT tail, and the
// case order below is the memory order of the arms rather than the table's: 8, 7, 6, 5,
// 10, 11, 9, default. Each arm ends `jmp glues_clear_all`, MSVC duplicating the void tail
// call into all eight. The glue_load_cd_dlg call is its own statement on purpose: written
// inline as dlg_run_modal's first argument, MSVC hoists the `push` above it and three
// opcodes come out transposed.
void do_cmpgn_dlg() {
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
  cmpgn_preinit();

  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(
      glue_load_cd_dlg(reinterpret_cast<intptr_t>(sgCmpgnDlgTemplate)));

  switch (dlg_run_modal(pDlg, 0,
                        reinterpret_cast<PFN_CTRL_EVT>(glucmpgn_dlg_fcn))) {
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

// 0x453FE0 ctrl_cmpgn_flc_conditional (220 B) -- the FLC control's own event handler,
// entries 5 and 7 of sCmpgnEvtFcnTbl_g. Two switches, the outer on dlgevt_t::wTag and the
// inner on dwId when wTag is 0x0E; anything it does not consume goes to ctrl_flc.
//
// The inner switch is a two-level table, an 11-byte case map over dwId 0..10 feeding five
// targets, both in the COMDAT tail. The four cases below are in the memory order of their
// arms, and dwId 0's arm falls into the shared ctrl_flc tail because it is physically
// next, where dwId 1's gets a duplicated copy.
//
// ctrl_t +0x36 is read as a rect16_t here, through a cast: dialog.h leaves it an 8-byte
// per-type payload because each control type reads it differently, and the FLC arm hands
// `lea ecx, [esi+36h]` to pt_in_rect, whose first argument is a `const rect16_t *`. The
// `movzx eax, al` on the wTag 4/6 arm is ctrl_query_locate's tail, which the BYTE cast
// produces.
int ctrl_cmpgn_flc_conditional(intptr_t nArg0, intptr_t nArg1) {
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
          timer_create(pCtrl, 2, 0xC8,
                       reinterpret_cast<PFN_TIMER_FN *>(ctrl_cmpgn_flc_activate));
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

// 0x4540E0 ctrl_cmpgn_flc_activate (70 B) -- the 200 ms timer this screen arms on the FLC
// control's activate event: show or hide the sibling control whose index is parked in
// nCmpgnCompletionMsgId_g, according to this node's 0x80 bit.
//
// The index mapping is where this row and its expansion twin differ, and it is the whole
// two-byte size difference between them: here nIndex 6 maps to slot 2, there nIndex 7
// maps to slot 1, and both funnel nIndex 8 to slot 0 off the compare chain's known zero.
// ctrl_t::nIndex is a signed short, and the slot is tested as a dword then reloaded as a
// word, because ctrl_find_ndx's second argument is a short.
void ctrl_cmpgn_flc_activate(intptr_t nArg0) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  int nSlot;

  switch (pCtrl->nIndex) {
    case 6:
      nSlot = 2;
      break;
    case 8:
      nSlot = 0;
      break;
    default:
      return;
  }

  if (nCmpgnCompletionMsgId_g[nSlot] == 0)
    return;

  ctrl_t *pTarget = ctrl_find_ndx(
      pCtrl->pParent, static_cast<short>(nCmpgnCompletionMsgId_g[nSlot]));
  if (pCtrl->dwFlags & 0x80)
    ctrl_show(pTarget);
  else
    ctrl_hide(pTarget);
}

// 0x454130 cmpgn_preinit (178 B) -- decide, before the screen is built, which campaigns
// are locked and why. Each of the three slots holds a negative message id, -10, -11 or
// -12, or zero for unlocked, and glucmpgn_dlg_cmd turns a non-zero slot into a prompt.
//
// The 12-byte clear is split on purpose: one `memset(g, 0, 12)` reproduces the oracle in
// the plain form, but under OC_DLL_MODE it lowers to a base register plus three offset
// stores, leaving the oracle's third address with no route. Writing the tail element as
// its own statement puts that literal back; three plain assignments do not merge into
// the movq at all.
//
// The 0x70-byte record is ReadCharacterFile's: a magic at +0x00, a byte at +0x04, a
// _time64 stamp at +0x08, a char[0x19] name at +0x0C and a 0x3C-byte block at +0x34 it
// clears separately. It stays a raw BYTE[0x70] with offset casts because the twin TU
// reads it at different offsets: this screen's counters are at +0x28 and +0x2C, gated
// on +0x30, where the expansion's are in that +0x34 block.
void cmpgn_preinit() {
  BYTE rec[0x70];

  nCmpgnCompletionMsgId_g[2] = 0;
  memset(nCmpgnCompletionMsgId_g, 0, 8);

  if (!ReadCharacterFile(gLocalPlayerName, rec)) {
    if (_stricmp(gLocalPlayerName, error_get_str(0x48)) != 0)
      err_popup(0x58, reinterpret_cast<intptr_t>(gLocalPlayerName), 0, 0, 0);
    return;
  }

  DWORD dwA = *reinterpret_cast<DWORD *>(rec + 0x28);
  DWORD dwB = *reinterpret_cast<DWORD *>(rec + 0x2C);

  if (dwA == 0) {
    if (dwB <= 0x0B)
      nCmpgnCompletionMsgId_g[0] = -10;
  }
  if (*reinterpret_cast<DWORD *>(rec + 0x30) != 0)
    return;
  if (dwB <= 0x0B) {
    nCmpgnCompletionMsgId_g[2] = -11;
    return;
  }
  if (dwA <= 0x15)
    nCmpgnCompletionMsgId_g[2] = -12;
}
