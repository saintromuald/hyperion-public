// filters.cpp -- the ally, vision and message filter dialogs, oracle
// 0x441980-0x442B1F. Bodies are in oracle VA order.
//
// /Ob2 can splice a callee defined in this file into a caller here, where 1.17,
// built without WPO, keeps every cross-TU call; noinline on the declaration is
// the lever when such a callee has to live here.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stdlib.h>
#include <string.h>

#include "../h/filters.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_btn.h"
#include "../h/dlg_lbox.h"
#include "../h/opt_dlgs.h"     // dfl_load_bmp, opt_dlg_set_blend, modeless_dlg_shutdown
#include "../h/save_dlg.h"     // modeless_dlg_startup, modeless_opt_dlg
#include "../h/menu_dlg.h"     // set_size_enter, set_size_escape
#include "../h/glues.h"        // glues_get_str
#include "../h/bmp.h"          // bmp_rect_fill
#include "../h/dlg_draw.h"     // draw_ctrl_text
#include "../h/gametype.h"     // AlliesAllowed, CanChangeVision, IsTVBGame
#include "../h/Teams.h"        // IsTeamGame, GetTeamName, IsComputerTeam
#include "../h/net_cmds.h"     // gPlayerData, send_net_cmd_SetAllies, IsValidPlayer
#include "../h/gluScore.h"     // btn_ctrl_radiobutton
#include "../h/TrigEnum.h"     // nation_find_first, nation_find_next
#include "../h/net_rand.h"     // net_get_fixed_seed
#include "../h/victory.h"      // IsOutOfGame
#include "../h/maphdr.h"       // sub_46A370
#include "../gen/globals117.h"
#include "../port/front/port_gamemenu.h"  // Hyperion: PortAlliesOpen and the rest

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// 0x529040 gbMsgFilterMode is already derived in net_cmds.cpp; 0x6662AC is the
// sibling byte this TU stores it into and reads back. 0x4EC3C4 is the .rdata
// dialog template, an object rather than a pointer, named from the ASCII path
// at that VA ("rez\msgfltr.bin").
#ifdef OC_DLL_MODE
  #ifndef gbMsgFilterMode
  #define gbMsgFilterMode (*reinterpret_cast<BYTE *>(OC_VA(0x529040u)))
  #endif
#else
extern BYTE gbMsgFilterMode;
#endif

#ifdef OC_DLL_MODE
  #ifndef byte_6662AC
  #define byte_6662AC (*reinterpret_cast<BYTE *>(OC_VA(0x6662ACu)))
  #endif
#else
extern BYTE byte_6662AC;
#endif

#ifdef OC_DLL_MODE
  #define sgMsgFilterDlgTemplate (*reinterpret_cast<BYTE (*)[1]>(OC_VA(0x4EC3C4u)))
#else
extern BYTE sgMsgFilterDlgTemplate[1];
#endif

// The staged copy of sgbAlliances_g[g_LocalNationID] the ally dialog edits.
// Extent 12: do_ally_filter_dlg fills it with one 12-byte row copy and
// allied_victory_fcn walks it with `cmp ecx, 0Ch / jl`. The sheet's autoname
// is kept, as it is for the block's other members.
#ifdef OC_DLL_MODE
  #ifndef byte_6662A0
  #define byte_6662A0 (*reinterpret_cast<BYTE (*)[12]>(OC_VA(0x6662A0u)))
  #endif
#else
extern BYTE byte_6662A0[12];
#endif

// The local black mask paired with gdwLocalMask.
#ifdef OC_DLL_MODE
  #ifndef gdwLocalBlackMask
  #define gdwLocalBlackMask (*reinterpret_cast<DWORD *>(OC_VA(0x67266Cu)))
  #endif
#else
extern DWORD gdwLocalBlackMask;
#endif

// The modal result dlg_run_modal returns. Signed: the sentinels are negative.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

#ifdef OC_DLL_MODE
  #ifndef byte_6662AD
  #define byte_6662AD (*reinterpret_cast<BYTE *>(OC_VA(0x6662ADu)))
  #endif
#else
extern BYTE byte_6662AD;
#endif

// 0x6662B0, the vision mask the ally dialog stages. WORD: every access is
// `mov ax, word ptr [6662B0h]`. The proof arm defines it, as Blizzard's
// filters.cpp did: only a defined object lets teamfilter_dlg_cmd pass it to
// SetFog with the oracle's widened `mov ecx, dword ptr [6662B0h]`.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
  #ifndef word_6662B0
  #define word_6662B0 (*reinterpret_cast<WORD *>(OC_VA(0x6662B0u)))
  #endif
#else
static WORD word_6662B0;
#endif

// The two remaining .rdata dialog templates, objects rather than pointers,
// named from the ASCII path at each VA (rez\teamfltr.bin and
// rez\allyfltr.bin).
#ifdef OC_DLL_MODE
  #define sgTeamFilterDlgTemplate (*reinterpret_cast<BYTE (*)[1]>(OC_VA(0x4EC39Cu)))
#else
extern BYTE sgTeamFilterDlgTemplate[1];
#endif

#ifdef OC_DLL_MODE
  #define sgAllyFilterDlgTemplate (*reinterpret_cast<BYTE (*)[1]>(OC_VA(0x4EC3B0u)))
#else
extern BYTE sgAllyFilterDlgTemplate[1];
#endif

// 0x5032C8 sFilterEvtFcnTbl, named and placed in this TU by the naming sheet.
// Extent 29 from the byte count its only caller passes (`push 74h` to
// ctrl_set_evt_fcns, which indexes it by dword).
#ifdef OC_DLL_MODE
  #define sFilterEvtFcnTbl (*reinterpret_cast<PFN_CTRL_EVT (*)[29]>(OC_VA(0x5032C8u)))
#else
extern PFN_CTRL_EVT sFilterEvtFcnTbl[29];
#endif

// 0x50333C off_50333C, the sheet's autoname for the message-filter dialog's own
// handler table. Extent 4 from the byte count msgfltr_dlg_fcn passes
// (`push 10h` to ctrl_set_evt_fcns).
#ifdef OC_DLL_MODE
  #define off_50333C (*reinterpret_cast<PFN_CTRL_EVT (*)[4]>(OC_VA(0x50333Cu)))
#else
extern PFN_CTRL_EVT off_50333C[4] OC_UNALIGNED;
#endif

// 0x5290B6, the twelve player colour indices initPlayerColorMap loads out of an
// image (`push 0Ch / push 5290B6h / call GameLoadImage`).
#ifdef OC_DLL_MODE
  #ifndef byte_5290B6
  #define byte_5290B6 (*reinterpret_cast<BYTE (*)[12]>(OC_VA(0x5290B6u)))
  #endif
#else
extern BYTE byte_5290B6[12];
#endif

// ctrl_t +0x26, a DWORD the filter check boxes carry as their player index
// (`mov ecx, dword ptr [esi+26h]` then used to subscript byte_6662A0).
// It falls inside dialog.h's unnamed +0x24 window.
static __forceinline dlg_dword_t &ctrl_d26(ctrl_t *p) {
  return *reinterpret_cast<dlg_dword_t *>(&p->_unmapped24[2]);
}

// ctrl_t +0x24, a WORD the ally and vision rows publish as their frame index,
// the same slot dlg_btn.cpp names btn_frame.
static __forceinline dlg_word_t &ctrl_w24(ctrl_t *p) {
  return *reinterpret_cast<dlg_word_t *>(&p->_unmapped24[0]);
}

// ctrl_t +0x36, the check box's own value, the same slot dlg_btn.cpp names
// btn_value.
static __forceinline dlg_dword_t &ctrl_d36(ctrl_t *p) {
  return *reinterpret_cast<dlg_dword_t *>(&p->_unmapped36[0]);
}

// The allied-victory check box. Event 10 relabels it for a replay, event 2
// pushes the new state onto every ally already staged, event 0 seeds it from
// the local player's own row and greys it when the game forbids allies.
BOOL allied_victory_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0: {
        DWORD dwOn;

        if (gPlayback == 0)
          dwOn = (byte_6662A0[g_LocalNationID] != 1);
        else
          dwOn = sgbShowAll_g;
        ctrl_set_val(pCtrl, dwOn != 0);
        if (gPlayback == 0 && !AlliesAllowed())
          ctrl_gray(pCtrl);
        break;
      }
      case 2:
        btn_ctrl_checkbox(pCtrl, pEvent);
        if (gPlayback == 0) {
          // A `for` over the same range is unrolled six deep; the do-while is
          // what keeps the oracle's single `inc ecx / cmp ecx, 0Ch / jl`.
          int i = 0;
          do {
            if (byte_6662A0[i] != 0)
              byte_6662A0[i] = static_cast<BYTE>((ctrl_d36(pCtrl) != 0) + 1);
          } while (++i < 12);
        }
        return 1;
      case 10:
        if (gPlayback != 0)
          ctrl_set_text(pCtrl, glues_get_str(0xB7));
        break;
    }
  }
  return btn_ctrl_checkbox(pCtrl, pEvent);
}

// One player's ally check box. Event 2 toggles that player's staged byte
// between 0 and whatever the local player's own row holds; event 0 seeds the
// box from it, but only once the control is visible.
BOOL filter_checkbox_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        if ((pCtrl->dwFlags & 8) != 0)
          ctrl_set_val(pCtrl, byte_6662A0[ctrl_d26(pCtrl)] != 0);
        break;
      case 2: {
        DWORD dwSlot;
        int nNew;

        btn_ctrl_checkbox(pCtrl, pEvent);
        dwSlot = ctrl_d26(pCtrl);
        // The staged value goes through an int: that gives the clear arm
        // `xor eax, eax`, the seed arm a `movzx`, and one shared store.
        if (byte_6662A0[dwSlot] != 0)
          nNew = 0;
        else
          nNew = byte_6662A0[g_LocalNationID];
        byte_6662A0[dwSlot] = static_cast<BYTE>(nNew);
        return 1;
      }
    }
  }
  return btn_ctrl_checkbox(pCtrl, pEvent);
}

// Collect the active nations and hand them back in a fixed-seed random order,
// returning how many there were. The memmove length is (7 - r) * 4 whatever the
// count is, so it always closes the hole out to the end of the eight-slot
// scratch; that is the oracle's own arithmetic and is reproduced, not repaired.
int sub_441AE0(int *pNations, int nUnused) {
  int aNations[8];
  int nCursor;
  DWORD nCount = 0;
  DWORD n = nation_find_first(&nCursor);

  // Hyperion: the dialogs have 8 rows, the first 8 nations
  while (n < (DWORD)PlayerLobbySlots() && nCount < 8) {
    aNations[nCount] = n;
    ++nCount;
    n = nation_find_next(&nCursor);
  }

  OcSrand(net_get_fixed_seed());

  // Both induction variables are initialised before the guard; that is what
  // keeps EBX and EDI in the prologue instead of sinking their pushes past it.
  DWORD i = 0;
  DWORD nLeft = nCount;

  while (i < nCount) {
    DWORD r = OcRand() % nLeft;

    pNations[i] = aNations[r];
    memmove(&aNations[r], &aNations[r + 1], (7 - r) * 4);
    ++i;
    --nLeft;
  }
  return nCount;
}

// Stage the local player's alliance row and vision mask, then bring the ally
// or team filter dialog up modeless. The twelve-byte row copy is one movq plus
// one dword.
void do_ally_filter_dlg() {
  memcpy(byte_6662A0, sgbAlliances_g.a[g_LocalNationID], 12);
  if (gPlayback != 0)
    word_6662B0 = static_cast<WORD>(gdwReplayVisionMask);
  else
    word_6662B0 = static_cast<WORD>(SharedVision[g_LocalNationID]);
  modeless_dlg_startup(
      IsTeamGame() ? sgTeamFilterDlgTemplate : sgAllyFilterDlgTemplate,
      (void *)&teamfilter_dlg_fcn);
}

// Bank the current filter mode, then bring the message-filter dialog up
// modeless. The tail jmp is what pins the return type.
void do_msg_filter_dlg() {
  byte_6662AC = gbMsgFilterMode;
  modeless_dlg_startup(sgMsgFilterDlgTemplate, (void *)&msgfltr_dlg_fcn);
}

// Paint a 12x12 swatch in the player's colour, vertically centred in the
// control, and then the label to its right.
// nY spells the height out again rather than reading nHeight; that is what
// schedules the pen load between the divide and the shift.
void color_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip) {
  int nTop = pCtrl->rc.top;
  int nHeight = pCtrl->rc.bottom - nTop;
  int nLeft = pCtrl->rc.left;
  int nY = nTop + (pCtrl->rc.bottom - nTop - 11) / 2;

  _bmp_pen = byte_5290B6[ctrl_d26(pCtrl)];
  bmp_rect_fill(nLeft, nY, 12, 12);
  if (pCtrl->pData != 0) {
    gbFontJust = 0x11;
    draw_ctrl_text(pCtrl, nHeight + 9, 0, NULL);
  }
}

// The team/ally filter dialog's activate handler; which of the two body
// builders runs is the only thing that distinguishes the two dialogs.
void teamfilter_dlg_activate(ctrl_t *pDlg) {
  dfl_load_bmp(pDlg, NULL);
  opt_dlg_set_blend(pDlg);
  if (IsTeamGame())
    sub_442490(pDlg);
  else
    sub_442320(pDlg);
  set_size_enter(pDlg, 3, 0x20000000);
  set_size_escape(pDlg, 5, 0x20000000);
}

// Commit the ally/vision dialog: in a replay the staged vision mask becomes the
// local one, otherwise the alliance row and vision go out as net commands.
// gPlayback is re-read at every step, as the oracle does. The shift count is
// cast to BYTE (`mov cl, bl`), which keeps nShift in EBX.
// Hyperion: the body is AlliesCommit, which the game menu's Alliances calls too, with
// Reveal Entire Map's box (control 0x19) as a value, read in a replay alone as before.
static void AlliesCommit(bool bShowAll);

void teamfilter_dlg_cmd(ctrl_t *pDlg) {
  if (gnDlgReturnVal != -2)
    return;
  AlliesCommit(gPlayback != 0 && ctrl_d36(ctrl_find_ndx(pDlg, 0x19)) != 0);
}

static void AlliesCommit(bool bShowAll) {
  if (gPlayback != 0) {
    DWORD dwVision = word_6662B0;

    gdwReplayVisionMask = dwVision;
    gdwLocalMask = dwVision;
    gdwLocalBlackMask = dwVision << 8;
  } else {
    int nShift = 0;
    DWORD dwAllies = 0;
    int i = 0;

    // A `for` here is unrolled six deep; the do-while keeps the oracle's single
    // `inc edx / cmp edx, 0Ch / jl`.
    do {
      dwAllies |= (byte_6662A0[i] & 3) << static_cast<BYTE>(nShift);
      nShift += 2;
    } while (++i < 12);
    send_net_cmd_SetAllies(dwAllies);
    send_net_cmd_SetFog(word_6662B0);
  }

  if (gPlayback != 0) {
    if (bShowAll) {
      sgbShowAll_g = 1;
      gdwLocalMask = 0xFF;
      gdwReplayVisionMask = 0xFF;
      gdwLocalBlackMask = 0xFF00;
    } else {
      sgbShowAll_g = 0;
    }
    if (gPlayback != 0) {
      for (int i = 0; i < 12; ++i) {
        if (IsOutOfGame(static_cast<BYTE>(i))) {
          gdwReplayVisionMask &= ~(1 << i);
          gdwLocalMask &= ~(1 << i);
          gdwLocalBlackMask &= ~(1 << (i + 8));
        }
      }
    }
  }

  if (gPlayback != 0)
    sub_46A370();
}

// The team/ally filter dialog's own event handler. The four ids reach a
// two-level jump table over 0..10; anything else falls to the base handler.
BOOL teamfilter_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 10:
        teamfilter_dlg_init(pCtrl);
        break;
      case 0:
        teamfilter_dlg_activate(pCtrl);
        break;
      case 2:
        teamfilter_dlg_cmd(pCtrl);
        break;
      case 1:
        modeless_dlg_shutdown(0);
        modeless_opt_dlg(pCtrl, pEvent);
        return TRUE;
    }
  }
  return modeless_opt_dlg(pCtrl, pEvent);
}

// Install the per-control handler table, and in a replay relabel control 0x1A
// and hide control 0x1B.
void teamfilter_dlg_init(ctrl_t *pDlg) {
  ctrl_set_evt_fcns(pDlg, sFilterEvtFcnTbl, 0x74);
  if (gPlayback != 0) {
    ctrl_t *pLabel = ctrl_find_ndx(pDlg, 0x1A);

    ctrl_set_text(pLabel, glues_get_str(0xB6));
    ctrl_hide(ctrl_find_ndx(pDlg, 0x1B));
  }
}

// Load the dialog's backdrop and wire Enter and Escape to controls 3 and 5.
void msgfilter_dlg_init(ctrl_t *pDlg) {
  dfl_load_bmp(pDlg, NULL);
  opt_dlg_set_blend(pDlg);
  set_size_enter(pDlg, 3, 0x20000000);
  set_size_escape(pDlg, 5, 0x20000000);
}

// Commit the message-filter dialog's list selection. Modes 0..7 are a player
// slot, 8 is everyone and 9 is the current allies.
//
// One expression for both bits shifts by the slot first, whatever the operand
// order. The else arm's merged pair is what keeps the local bit first.
// Hyperion: the body is MessagesCommit, which the game menu's Messaging calls too, with
// the list's selection and its data as values.
static void MessagesCommit(BYTE nSel, BYTE nData);

void msgfilter_dlg_cmd(ctrl_t *pDlg) {
  if (gnDlgReturnVal == -2) {
    lbox_t *pList = reinterpret_cast<lbox_t *>(ctrl_find_ndx(pDlg, 4));
    BYTE nSel = lbox_get_selection(pList);

    MessagesCommit(nSel, static_cast<BYTE>(reinterpret_cast<DWORD_PTR>(lbox_get_data(pList, nSel))));
  }
}

static void MessagesCommit(BYTE nSel, BYTE nData) {
  BYTE bMode = byte_6662AC;

  byte_6662AD = nSel;
  if (bMode < 8) {
    bMode = nData;
    byte_6662AC = bMode;
  }
  gbMsgFilterMode = bMode;
  gwMsgFilter_g = 0;
  switch (bMode) {
    case 8:
      gwMsgFilter_g = 0xFFFF;
      break;
    case 9:
      update_message_filter();
      break;
    default:
      if (IsTeamGame()) {
        sub_442740(gPlayerData[g_LocalNationID].nTeam);
        sub_442740(gbMsgFilterMode);
      } else {
        gwMsgFilter_g = 1 << g_LocalStormID;
        if (gnDlgReturnVal == -2)
          gwMsgFilter_g |= 1 << gPlayerData[gbMsgFilterMode].dwStormID;
        else
          gwMsgFilter_g |= 1 << gPlayerData[gbMsgFilterMode].dwStormID;
      }
      break;
  }
}

// Fill the ally dialog's rows one per active nation: a colour swatch carrying
// the name, an ally check box and a vision check box, all three walked down
// their own sibling chains.
void sub_442320(ctrl_t *pDlg) {
  int aNations[8];
  ctrl_t *pColor = ctrl_find_ndx(pDlg, -10);
  ctrl_t *pAlly = ctrl_find_ndx(pDlg, 1);
  ctrl_t *pVision = ctrl_find_ndx(pDlg, 13);
  int nCount = sub_441AE0(aNations, 8);

  if (nCount == 0)
    return;
  do {
    int nNation;

    --nCount;
    nNation = aNations[nCount];
    if (gPlayback == 0) {
      // A plain `continue`: the oracle's two rejections jump past the three
      // sibling advances, straight to the loop test.
      if (nNation == g_LocalNationID)
        continue;
      if (!IsTVBGame() && gPlayerData[nNation].nType == 1)
        continue;
    }

    // The store goes in each arm: a shared local for the name lands the phi in
    // ECX and costs a `mov ecx, eax` after GetTeamName.
    if (IsTeamGame())
      pColor->pData = const_cast<char *>(GetTeamName(gPlayerData[nNation].nTeam));
    else
      pColor->pData = gPlayerData[nNation].szName;
    ctrl_d26(pColor) = nNation;
    pColor->pfnDraw = color_draw;
    ctrl_show(pColor);

    if (gPlayback != 0) {
      ctrl_hide(pAlly);
    } else {
      ctrl_show(pAlly);
      ctrl_d26(pAlly) = nNation;
      ctrl_w24(pAlly) = 0x0B;
      if (!AlliesAllowed())
        ctrl_gray(pAlly);
      if (gPlayerData[nNation].nType == 1)
        ctrl_gray(pAlly);
    }

    ctrl_show(pVision);
    ctrl_w24(pVision) = 0x0B;
    ctrl_d26(pVision) = nNation;
    if (gPlayback == 0) {
      if (gPlayerData[nNation].nType == 1)
        ctrl_gray(pVision);
      if (!CanChangeVision(g_LocalNationID, nNation))
        ctrl_gray(pVision);
    }

    pVision = pVision->pNext;
    pAlly = pAlly->pNext;
    pColor = pColor->pNext;
  } while (nCount != 0);
}

// Rebuild the outgoing message filter from the local player's alliance row: in
// a team game every human sharing an allied player's team, otherwise every
// allied slot. Both slot walks are fully unrolled by the compiler. The mask is
// read once after IsTeamGame; the team arm writes through a local, the other
// arm ORs into the global.
void update_message_filter() {
  gwMsgFilter_g = 0;
  BOOL bTeam = IsTeamGame();
  WORD wMask = gwMsgFilter_g;
  if (bTeam) {
    for (int i = 0; i < 8; ++i) {
      int nTeam;

      if (sgbAlliances_g[g_LocalNationID][i] == 0)
        continue;
      nTeam = gPlayerData[i].nTeam;
      for (int j = 0; j < 8; ++j) {
        if (gPlayerData[j].nType == 2 && gPlayerData[j].nTeam == nTeam) {
          wMask |= static_cast<WORD>(1 << gPlayerData[j].dwStormID);
          gwMsgFilter_g = wMask;
        }
      }
    }
  } else {
    for (int i = 0; i < 8; ++i) {
      if (sgbAlliances_g[g_LocalNationID][i] != 0)
        gwMsgFilter_g |= static_cast<WORD>(1 << gPlayerData[i].dwStormID);
    }
  }
}

// The team-game form of the same dialog: one row per nation not on a computer
// team, plus a name row per human slot that shares that nation's team. The
// ally and vision rows are skipped for the local player's own team. Both +0x24
// stores come before their +0x26 sibling, and the inner slot loop's index is
// unsigned so the gPlayerData base stays in the displacement.
void sub_442490(ctrl_t *pDlg) {
  int aNations[8];
  ctrl_t *pColor = ctrl_find_ndx(pDlg, -10);
  ctrl_t *pName = ctrl_find_ndx(pDlg, -30);
  ctrl_t *pAlly = ctrl_find_ndx(pDlg, 1);
  ctrl_t *pVision = ctrl_find_ndx(pDlg, 13);
  int nCount = sub_441AE0(aNations, 8);

  if (nCount == 0)
    return;
  do {
    int nNation;
    int nVision;

    --nCount;
    nNation = aNations[nCount];
    if (IsComputerTeam(gPlayerData[nNation].nTeam))
      continue;

    if (IsTeamGame())
      pColor->pData = const_cast<char *>(GetTeamName(gPlayerData[nNation].nTeam));
    else
      pColor->pData = gPlayerData[nNation].szName;
    ctrl_d26(pColor) = aNations[nCount];
    pColor->pfnDraw = color_draw;
    ctrl_show(pColor);

    if (gPlayerData[nNation].nTeam != gPlayerData[g_LocalNationID].nTeam) {
      if (gPlayback != 0) {
        ctrl_hide(pAlly);
      } else {
        ctrl_show(pAlly);
        ctrl_w24(pAlly) = 0x0B;
        ctrl_d26(pAlly) = aNations[nCount];
        if (!AlliesAllowed())
          ctrl_gray(pAlly);
        if (gPlayerData[nNation].nType == 1)
          ctrl_gray(pAlly);
      }

      ctrl_show(pVision);
      ctrl_w24(pVision) = 0x0B;
      // The only use that needs the value twice, so it is the only one that
      // earns a register; the other two rematerialise out of aNations.
      nVision = aNations[nCount];
      ctrl_d26(pVision) = nVision;
      if (gPlayback == 0) {
        if (gPlayerData[nNation].nType == 1)
          ctrl_gray(pVision);
        if (!CanChangeVision(g_LocalNationID, nVision))
          ctrl_gray(pVision);
      }
    }

    // Unsigned: a signed index folds the array base into the induction variable
    // where the oracle keeps it in the displacement and tests `jb`.
    for (unsigned i = 0; i < 8; ++i) {
      if (gPlayerData[nNation].nTeam == gPlayerData[i].nTeam &&
          !IsOutOfGame(static_cast<BYTE>(i)) && gPlayerData[i].dwStormID < 8 &&
          IsValidPlayer(static_cast<BYTE>(gPlayerData[i].dwStormID))) {
        ctrl_show(pName);
        pName->pData = gPlayerData[i].szName;
        pColor = pColor->pNext;
        pName = pName->pNext;
        pAlly = pAlly->pNext;
        pVision = pVision->pNext;
      }
    }

    if (pColor != 0)
      pColor = pColor->pNext;
    if (pName != 0)
      pName = pName->pNext;
    if (pAlly != 0)
      pAlly = pAlly->pNext;
    if (pVision != 0)
      pVision = pVision->pNext;
  } while (nCount != 0);
}

// The message-filter dialog's own event handler, the same jump-table shape as
// teamfilter_dlg_fcn with the handler table installed inline on id 10.
BOOL msgfltr_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 10:
        ctrl_set_evt_fcns(pCtrl, off_50333C, 0x10);
        break;
      case 0:
        msgfilter_dlg_init(pCtrl);
        break;
      case 2:
        msgfilter_dlg_cmd(pCtrl);
        break;
      case 1:
        modeless_dlg_shutdown(0);
        modeless_opt_dlg(pCtrl, pEvent);
        return TRUE;
    }
  }
  return modeless_opt_dlg(pCtrl, pEvent);
}

// The vision check box for one player. Event 2 flips that player's bit in the
// staged mask; event 0 seeds the box from it, once the control is visible.
BOOL vision_checkbox_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        if ((pCtrl->dwFlags & 8) != 0)
          ctrl_set_val(pCtrl, (word_6662B0 & (1 << ctrl_d26(pCtrl))) != 0);
        break;
      case 2:
        btn_ctrl_checkbox(pCtrl, pEvent);
        word_6662B0 ^= static_cast<WORD>(1 << ctrl_d26(pCtrl));
        return 1;
    }
  }
  return btn_ctrl_checkbox(pCtrl, pEvent);
}

// Set the message filter to every human slot on one team. The eight-slot walk
// is fully unrolled by the compiler.
void sub_442740(int nTeam) {
  for (int i = 0; i < 8; ++i) {
    if (gPlayerData[i].nType == 2 && gPlayerData[i].nTeam == nTeam)
      gwMsgFilter_g |= static_cast<WORD>(1 << gPlayerData[i].dwStormID);
  }
}

// Seed one of the three message-filter radio buttons from the banked mode, and
// grey the allies button when the game forbids allies.
void sub_4428B0(ctrl_t *pDlg) {
  switch (pDlg->nIndex) {
    case 1:
      if (byte_6662AC < 8)
        ctrl_set_val(pDlg, 1);
      break;
    case 2:
      if (byte_6662AC == 8)
        ctrl_set_val(pDlg, 1);
      break;
    case 3:
      if (byte_6662AC == 9)
        ctrl_set_val(pDlg, 1);
      if (!AlliesAllowed())
        ctrl_gray(pDlg);
      break;
  }
}

// Bank the mode a radio button selects; button 1 takes it from the sibling
// list box instead of from a constant.
void sub_442910(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  btn_ctrl_radiobutton(pCtrl, pEvent);
  switch (pCtrl->nIndex) {
    case 1: {
      lbox_t *pList =
          reinterpret_cast<lbox_t *>(ctrl_find_ndx(pCtrl->pParent, 4));
      BYTE nSel = lbox_get_selection(pList);

      byte_6662AC = static_cast<BYTE>(
          reinterpret_cast<DWORD_PTR>(lbox_get_data(pList, nSel)));
      break;
    }
    case 2:
      byte_6662AC = 8;
      break;
    case 3:
      byte_6662AC = 9;
      break;
  }
}

// The radio buttons' control handler.
BOOL sub_442960(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        sub_4428B0(pCtrl);
        break;
      case 2:
        sub_442910(pCtrl, pEvent);
        return TRUE;
    }
  }
  return btn_ctrl_radiobutton(pCtrl, pEvent);
}

// Refill the message-filter combo: one entry per active nation in the shuffled
// order, by team name in a team game and by player name otherwise, then restore
// the banked selection and grey control 1 when nothing was added.
void sub_4429A0(ctrl_t *pDlg) {
  lbox_t *pBox = reinterpret_cast<lbox_t *>(pDlg);
  int aNations[8];
  int nCount;

  pDlg->dwFlags |= 0x2000;
  lbox_set_recalc(pBox, 0);
  nCount = sub_441AE0(aNations, 8);
  while (nCount != 0) {
    int nNation;

    --nCount;
    nNation = aNations[nCount];
    if (gPlayerData[nNation].nType == 1)
      continue;
    // Each arm keeps its own lbox_add_item and data push; a `pData` local
    // common to both arms hoists the push out and loses the match.
    if (!IsTeamGame()) {
      BYTE nItem;

      if (nNation == g_LocalNationID)
        continue;
      nItem = lbox_add_item(pDlg, gPlayerData[nNation].szName, 0);
      lbox_set_data(pBox, static_cast<unsigned char>(nItem),
                    reinterpret_cast<void *>((intptr_t)nNation));
    } else {
      BYTE nItem =
          lbox_add_item(pDlg, GetTeamName(gPlayerData[nNation].nTeam), 0);

      lbox_set_data(pBox, static_cast<unsigned char>(nItem),
                    reinterpret_cast<void *>(
                        static_cast<DWORD_PTR>(gPlayerData[nNation].nTeam)));
    }
  }
  lbox_set_recalc(pBox, 1);

  if (byte_6662AC < 8) {
    int i = 0;

    if (lbox_get_count(pBox) != 0) {
      do {
        if (byte_6662AC == reinterpret_cast<DWORD_PTR>(
                               lbox_get_data(pBox, static_cast<unsigned char>(i)))) {
          lbox_set_selection(pBox, static_cast<unsigned char>(i));
          break;
        }
        ++i;
      } while (static_cast<DWORD>(i) < lbox_get_count(pBox));
    }
    if (static_cast<DWORD>(i) >= lbox_get_count(pBox))
      lbox_set_selection(pBox, 0);
  } else {
    int nSel = byte_6662AD;

    if (lbox_get_count(pBox) - 1 < nSel)
      nSel = lbox_get_count(pBox) - 1;
    ctrl_set_val(pDlg, nSel);
  }

  if (lbox_get_count(pBox) == 0)
    ctrl_gray(ctrl_find_ndx(pDlg->pParent, 1));
}

// The message-filter combo's control handler.
BOOL sub_442AF0(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E && pEvent->dwId == 0)
    sub_4429A0(pCtrl);
  return lbox_ctrl_combo(reinterpret_cast<lbox_t *>(pCtrl), pEvent);
}

// Hyperion: Alliances and Messaging in the game menu's style
// (port_front_gamemenu.cpp). The rows are read by the rules sub_442320, sub_442490 and sub_4429A0 show and grey
// the dialogs' controls by; the boxes change the staged copy as filter_checkbox_fcn,
// vision_checkbox_fcn and allied_victory_fcn do; Accept is AlliesCommit or MessagesCommit.
static bool s_bPortShowAll;

static void PortAlliesBoxes(PortAlliesRow *r, int nNation) {
  r->bVision = true;
  if (gPlayback != 0)
    return;
  r->bAlly = true;
  r->bAllyOff = !AlliesAllowed() || gPlayerData[nNation].nType == 1;
  r->bVisionOff = gPlayerData[nNation].nType == 1 || !CanChangeVision(g_LocalNationID, nNation);
}

void PortAlliesOpen(PortAllies *p) {
  memcpy(byte_6662A0, sgbAlliances_g.a[g_LocalNationID], 12);
  if (gPlayback != 0)
    word_6662B0 = static_cast<WORD>(gdwReplayVisionMask);
  else
    word_6662B0 = static_cast<WORD>(SharedVision[g_LocalNationID]);
  s_bPortShowAll = sgbShowAll_g != 0;
  memset(p, 0, sizeof *p);
  p->bReplay = gPlayback != 0;
  p->bVictoryOff = gPlayback == 0 && !AlliesAllowed();
  int aNations[8];
  int nCount = sub_441AE0(aNations, 8);
  const bool bTeams = IsTeamGame() != 0;
  while (nCount != 0) {
    const int nNation = aNations[--nCount];
    PortAlliesRow *r = &p->aRows[p->nRows];
    if (bTeams) {
      if (IsComputerTeam(gPlayerData[nNation].nTeam))
        continue;
      r->pszName = GetTeamName(gPlayerData[nNation].nTeam);
      if (gPlayerData[nNation].nTeam != gPlayerData[g_LocalNationID].nTeam)
        PortAlliesBoxes(r, nNation);
      for (unsigned i = 0; i < 8; ++i)
        if (gPlayerData[nNation].nTeam == gPlayerData[i].nTeam && !IsOutOfGame(static_cast<BYTE>(i)) &&
            gPlayerData[i].dwStormID < 8 && IsValidPlayer(static_cast<BYTE>(gPlayerData[i].dwStormID)))
          r->apszMembers[r->nMembers++] = gPlayerData[i].szName;
    } else {
      if (gPlayback == 0 &&
          (nNation == g_LocalNationID || (!IsTVBGame() && gPlayerData[nNation].nType == 1)))
        continue;
      r->pszName = gPlayerData[nNation].szName;
      PortAlliesBoxes(r, nNation);
    }
    r->nNation = nNation;
    ++p->nRows;
  }
}

bool PortAlliesIsAlly(int nNation) {
  return byte_6662A0[nNation] != 0;
}

bool PortAlliesHasVision(int nNation) {
  return (word_6662B0 & (1 << nNation)) != 0;
}

bool PortAlliesVictory(void) {
  return gPlayback != 0 ? s_bPortShowAll : byte_6662A0[g_LocalNationID] != 1;
}

void PortAlliesToggleAlly(int nNation) {
  byte_6662A0[nNation] = byte_6662A0[nNation] != 0 ? 0 : byte_6662A0[g_LocalNationID];
}

void PortAlliesToggleVision(int nNation) {
  word_6662B0 ^= static_cast<WORD>(1 << nNation);
}

void PortAlliesToggleVictory(void) {
  if (gPlayback != 0) {
    s_bPortShowAll = !s_bPortShowAll;
    return;
  }
  const BYTE bNew = static_cast<BYTE>(PortAlliesVictory() ? 1 : 2);
  for (int i = 0; i < 12; ++i)
    if (byte_6662A0[i] != 0)
      byte_6662A0[i] = bNew;
}

void PortAlliesAccept(void) {
  AlliesCommit(s_bPortShowAll);
}

void PortReplayVisionToggle(int nNation) {
  if (gPlayback == 0 || nNation < 0 || nNation >= 8)
    return;
  const WORD wStaged = word_6662B0;
  word_6662B0 = static_cast<WORD>((gdwReplayVisionMask ^ (1u << nNation)) & 0xFF);
  AlliesCommit(false);
  word_6662B0 = wStaged;
}

// The list's data, a nation or in a team game a team, as sub_4429A0 sets it.
static BYTE s_abPortMsgData[8];

void PortMessagesOpen(PortMessages *p) {
  byte_6662AC = gbMsgFilterMode;
  memset(p, 0, sizeof *p);
  int aNations[8];
  int nCount = sub_441AE0(aNations, 8);
  const bool bTeams = IsTeamGame() != 0;
  while (nCount != 0) {
    const int nNation = aNations[--nCount];
    if (gPlayerData[nNation].nType == 1)
      continue;
    if (!bTeams && nNation == g_LocalNationID)
      continue;
    p->apszNames[p->nNames] = bTeams ? GetTeamName(gPlayerData[nNation].nTeam) : gPlayerData[nNation].szName;
    s_abPortMsgData[p->nNames++] = static_cast<BYTE>(bTeams ? gPlayerData[nNation].nTeam : nNation);
  }
  p->bAlliesOff = !AlliesAllowed();
  if (byte_6662AC == 8) {
    p->nChoice = PORT_MSG_EVERYONE;
  } else if (byte_6662AC == 9) {
    p->nChoice = PORT_MSG_ALLIES;
  } else {
    p->nChoice = 0;
    for (int i = 0; i < p->nNames; ++i)
      if (s_abPortMsgData[i] == byte_6662AC)
        p->nChoice = i;
    if (p->nNames == 0)
      p->nChoice = PORT_MSG_EVERYONE;
  }
}

void PortMessagesAccept(int nChoice) {
  BYTE nSel = byte_6662AD;
  if (nChoice >= 0) {
    nSel = static_cast<BYTE>(nChoice);
    byte_6662AC = s_abPortMsgData[nChoice];
  } else {
    byte_6662AC = static_cast<BYTE>(nChoice == PORT_MSG_EVERYONE ? 8 : 9);
  }
  MessagesCommit(nSel, s_abPortMsgData[nSel < 8 ? nSel : 0]);
}
