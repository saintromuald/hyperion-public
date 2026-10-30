// textbox.cpp -- the in-game chat textbox: its dialog and edit callbacks, the
// send-target mode switch, and the outgoing chat packet. Bodies in VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers on some declarations are for, and why moving a
// body between TUs can change the code generated.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>
#include <string.h>          // strlen, inlined by /Oi in textbox_dlg_char

#include "../h/textbox.h"
#include "../h/dialog.h"
#include "../h/dlg_dlg.h"       // dlg_deactivate, dlg_load, dlg_activate, dlg_set_font
#include "../h/dlg_ctrl.h"      // ctrl_find_ndx / show / hide / showhide / set_update / ctrl_dlg
#include "../h/dlg_draw.h"      // font_strlen, font_set
#include "../h/save_dlg.h"      // edit_ctrl_edit, edit_get_text, edit_set_text
#include "../h/statbtn.h"       // targeting_msg -- declared THERE, not in textbox.h
#include "../h/minimap.h"       // IsPausedSingleplayer
#include "../h/victory.h"       // TryAbortCurrentTrigger
#include "../h/event_Win.h"     // init_lang_imm, accel_set_single, accel_set_multi
#include "../h/cheat.h"         // parse_command
#include "../h/net_cmds.h"      // send_net_cmd_Cheat, net_send_string, PlayerData
#include "../h/replay.h"        // parse_replay_command
#include "../h/gametype.h"      // AlliesAllowed
#include "../h/filters.h"       // update_message_filter
#include "../h/Teams.h"         // IsTeamGame, GetTeamName
#include "../h/gds_local.h"     // psnprintf
#include "../h/snd.h"           // load_game_data
#include "../h/glues.h"         // alloc_load_bmp
#include "../port/render/port_console.h"  // Hyperion: PortConsoleRace

// After net_cmds.h, which defines PlayerData: the generated header declares
// `extern PlayerData gPlayerData[8];`, so the type must be complete by here.
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// 0x6BE658 and 0x6BE65C, which sit just above target.cpp's state block and
// allowed_errcode_g.
#ifdef OC_DLL_MODE
  #ifndef gbInMsgMode
  #define gbInMsgMode (*reinterpret_cast<BYTE *>(OC_VA(0x6BE658u)))
  #endif
#else
extern BYTE gbInMsgMode;
#endif

#ifdef OC_DLL_MODE
  #ifndef sgpTextBoxDlg
  #define sgpTextBoxDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x6BE65Cu)))
  #endif
#else
extern ctrl_t *sgpTextBoxDlg;
#endif

// 0x6BE660 sgszToPlayerPrompt. Its extent, 0x32, comes from sub_4B3EB0's own
// `push 32h` into psnprintf, which is an upper bound only, so nothing may take
// sizeof() of it.
#ifdef OC_DLL_MODE
  #ifndef sgszToPlayerPrompt
  #define sgszToPlayerPrompt (reinterpret_cast<char *>(OC_VA(0x6BE660u)))
  #endif
#else
extern char sgszToPlayerPrompt[];
#endif

// 0x6726AC gReplyPlayerName_g, which belongs to net_data.cpp. Its extent,
// 0x19, comes from textbox_mode_switch's `push 19h` into SNetGetReplyName.
#ifdef OC_DLL_MODE
  #ifndef gReplyPlayerName_g
  #define gReplyPlayerName_g (reinterpret_cast<char *>(OC_VA(0x6726ACu)))
  #endif
#else
extern char gReplyPlayerName_g[];
#endif

// 0x529040, a BYTE net_cmds.cpp also reaches: the chat and message filter mode
// selector, values 8 and 9, written and read byte-wide there.
#ifdef OC_DLL_MODE
  #ifndef gbMsgFilterMode
  #define gbMsgFilterMode (*reinterpret_cast<BYTE *>(OC_VA(0x529040u)))
  #endif
#else
extern BYTE gbMsgFilterMode;
#endif

// 0x503358 gszRaceChars_g, which belongs to gamedata.cpp. Signed chars:
// textbox_init reads it `movsx eax, byte ptr [eax+503358h]` and passes the
// value as psnprintf's %c. Its extent is not derived, so it stays incomplete.
#ifdef OC_DLL_MODE
  #ifndef gszRaceChars_g
  #define gszRaceChars_g (reinterpret_cast<char *>(OC_VA(0x503358u)))
  #endif
#else
extern char gszRaceChars_g[];
#endif

// 0x5264C2 byte_5264C2 -- a positional name, since it has no other. A BYTE:
// textbox_init reads it movzx and indexes gszRaceChars_g with it.
#ifdef OC_DLL_MODE
  #ifndef byte_5264C2
  #define byte_5264C2 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C2u)))
  #endif
#else
extern BYTE byte_5264C2;
#endif

// 0x665F58 / 0x665F5C -- event_Win.cpp's pair, both whole dwords.
// textbox_dlg_char compares the first against 1 and only then stores 1 into the
// second, the same shape gluChat.cpp and gluPopup.cpp have. What they mean is
// not derived.
#ifdef OC_DLL_MODE
  #ifndef dword_665F58
  #define dword_665F58 (*reinterpret_cast<DWORD *>(OC_VA(0x665F58u)))
  #endif
#else
extern DWORD dword_665F58;
#endif

#ifdef OC_DLL_MODE
  #ifndef dword_665F5C
  #define dword_665F5C (*reinterpret_cast<DWORD *>(OC_VA(0x665F5Cu)))
  #endif
#else
extern DWORD dword_665F5C;
#endif

// dlgevt_t +0x08 is a per-event-id payload the header leaves unmapped. For the
// keyboard ids this TU handles it is the virtual key at +0x08 and the modifier
// bits at +0x0A, the same split dlg_edit.cpp uses.
#define evt_key(p)  (*reinterpret_cast<dlg_word_t *>(&(p)->_unmapped08[0]))
#define evt_mods(p) (*reinterpret_cast<dlg_word_t *>(&(p)->_unmapped08[2]))

// The bodies, in oracle VA order.

//
// The message control is index 5 of the textbox dialog. Both arms end in the
// same tail jmp to ctrl_set_update, which MSVC duplicates from one call. The
// store sits in each arm and is hoisted back out: written once above the
// branch it schedules after `mov esi, eax` instead of before the compare.
void __fastcall targeting_msg(const char *pszMsg) {
  ctrl_t *pCtrl;

  pCtrl = ctrl_find_ndx(sgpTextBoxDlg, 5);
  if (gbInMsgMode != 0) {
    pCtrl->pData = const_cast<char *>(pszMsg);
    ctrl_showhide(pCtrl, 0);
  } else {
    pCtrl->pData = const_cast<char *>(pszMsg);
    if (pszMsg != NULL)
      ctrl_showhide(pCtrl, 1);
    else
      ctrl_showhide(pCtrl, 0);
  }
  ctrl_set_update(pCtrl);
}

//
// strlen is the /Oi intrinsic, so the scan loop is inline. The full line is
// 0x4E characters; a backspace (key 8) is let through regardless.
void textbox_dlg_char(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  // Hyperion: a UTF-8 character's first byte counts its whole length, so that it is
  // typed whole or not at all
  const unsigned nKey = evt_key(pEvent);
  const size_t nMore = nKey >= 0xF0 && nKey <= 0xF4 ? 3 : nKey >= 0xE0 && nKey < 0xF0 ? 2 : nKey >= 0xC2 && nKey < 0xE0 ? 1 : 0;
  if (strlen(static_cast<const char *>(pCtrl->pData)) + nMore >= 0x4E &&
      evt_key(pEvent) != 8) {
    if (dword_665F58 == 1)
      dword_665F5C = 1;
    return;
  }
  edit_ctrl_edit(pCtrl, pEvent);
}

//
// A two-level jump table over tags 0..15: an index byte at 0x4B3A34 selecting
// one of four blocks at 0x4B3A24. There is no `default:` label -- the
// out-of-range tags and the unlisted ones fall past the switch to the shared
// tail, which is what puts that tail between case 14 and the case 0 block.
int textbox_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  int nResult;

  switch (pEvent->wTag) {
    case 15:
      if (IsPausedSingleplayer()) {
        if (evt_key(pEvent) == 0x1B)
          TryAbortCurrentTrigger();
        return 1;
      }
      if (evt_key(pEvent) == 0x0A || evt_key(pEvent) == 0x0D) {
        textbox_mode_switch(pCtrl, pEvent, 1);
        return 1;
      }
      if (evt_key(pEvent) == 0x1B && gbInMsgMode != 0) {
        textbox_mode_switch(pCtrl, pEvent, 0);
        return 1;
      }
      break;

    case 14:
      // Inverted so the init call is the block that falls into the tail; the
      // `if (dwId == 0) ... else if` spelling swaps the two arms.
      if (pEvent->dwId != 0) {
        if (pEvent->dwId != 4)
          break;
        return 0;
      }
      textbox_dlg_init(pCtrl);
      break;

    case 0:
    case 1:
    case 3:
    case 4:
    case 6:
    case 7:
    case 9:
      return IsPausedSingleplayer() != 0;
  }

  nResult = ctrl_dlg(pCtrl, pEvent);
  return IsPausedSingleplayer() ? 1 : nResult;
}

//
// Widens every control of index 3 or below to fit its own text, then hands the
// edit control (index 6) its handler. In a replay the dialog sits 0x18 higher.
void textbox_dlg_init(ctrl_t *pDlg) {
  ctrl_t *pCtrl;

  gbInMsgMode = 0;
  pDlg->dwFlags |= 0x30002000;
  dlg_set_font(pDlg);

  // `jg` leaves the loop rather than skipping the body, and the length has to
  // reach the add as a plain int: narrowing it first costs a `movzx eax, ax`.
  for (pCtrl = ctrl_find_ndx(pDlg, 1); pCtrl != NULL; pCtrl = pCtrl->pNext) {
    int nLen;

    if (pCtrl->nIndex > 3)
      break;
    nLen = font_strlen(static_cast<const char *>(pCtrl->pData));
    pCtrl->rc.right = static_cast<short>(pCtrl->rc.left + nLen);
  }
  font_set(NULL);

  ctrl_find_ndx(pDlg, 6)->pfnEvt = textbox_edit_fcn;
  if (gPlayback != 0) {
    pDlg->rc.top -= 0x18;
    pDlg->rc.bottom -= 0x18;
  }
}

//
// The edit control's own handler, same two-level table over tags 0..15 and the
// same no-default shape: everything unhandled tails into edit_ctrl_edit.
int textbox_edit_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  switch (pEvent->wTag) {
    case 14:
      if (pEvent->dwId == 6 && gbInMsgMode != 0)
        textbox_mode_switch(pCtrl->pParent, pEvent, 0);
      break;

    case 0:
    case 1:
      // A range over one int load: `(unsigned)(k - 0x21) <= 7` gives `sub`
      // where the oracle has `add eax, -21h`, and a second read stops the fold.
      {
        unsigned int nKey = evt_key(pEvent);
        if (nKey >= 0x21 && nKey <= 0x28)
          return 0;
      }
      break;

    case 15:
      textbox_dlg_char(pCtrl, pEvent);
      return 1;
  }

  return edit_ctrl_edit(pCtrl, pEvent);
}

//
// Nullary: `mov ecx, [0x6BE65C]` loads the dialog into the register the
// function would otherwise have received, so there is no incoming argument.
// The clear runs unconditionally, after the guarded call.
void textbox_free() {
  if (sgpTextBoxDlg != NULL)
    dlg_deactivate(sgpTextBoxDlg);
  sgpTextBoxDlg = NULL;
}

//
// The .bin name carries the race letter, so the dialog is per-race. 0x1E3 is
// the oracle's own __LINE__ for the dlg_load call and is spelled literally.
void textbox_init() {
  char szPath[0x50];

  // Hyperion: the console's race, Console skin's (PortConsoleRace)
  psnprintf(szPath, 0x50, "rez\\%ctextbox.bin", gszRaceChars_g[PortConsoleRace()]);
  sgpTextBoxDlg = dlg_load(szPath,
                           reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                           reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                           "textbox.cpp", 0x1E3);
  dlg_activate(sgpTextBoxDlg, 0, textbox_dlg_fcn);
}

//
// Leaving message mode sends what was typed; entering it picks the send target
// from the modifier bits, then the filter mode. nArg1 is really the dlgevt_t
// whose +0x0A modifier byte selects allied versus all.
void textbox_mode_switch(ctrl_t *pDlg, dlgevt_t *pEvent, BYTE bArg2) {
  ctrl_t *pEdit;
  DWORD dwCheatBits;
  char szText[0x100];

  if (gbInMsgMode != 0) {
    init_lang_imm(0);
    pEdit = ctrl_find_ndx(pDlg, 6);
    if (bArg2 != 0) {
      edit_get_text(pEdit, szText);
      dwCheatBits = glCheatBits;
      if (parse_command(szText, &dwCheatBits)) {
        if (dwCheatBits != glCheatBits)
          send_net_cmd_Cheat(dwCheatBits);
      } else if (gPlayback == 0 || !parse_replay_command(szText)) {
        if (gbMultiPlayerMode != 0)
          packet_send_string(szText);
      }
    }
    edit_set_text(pEdit, "");
    accel_set_single();
    gbInMsgMode = 0;
  } else {
    init_lang_imm(1);
    SNetGetReplyName(gReplyPlayerName_g, 0x19);
    accel_set_multi();
    if (gbMultiPlayerMode == 0)
      gbInMsgMode = 1;
    else if ((evt_mods(pEvent) & 3) != 0)
      gbInMsgMode = 2;
    else if (AlliesAllowed() && (evt_mods(pEvent) & 4) != 0)
      gbInMsgMode = 3;
    else if (gbMsgFilterMode == 8)
      gbInMsgMode = 2;
    else if (AlliesAllowed()) {
      // Store 3 and overwrite, as the oracle does; the `&&` spelling matches
      // the proof form too but costs two bytes of branch in the shipped one.
      gbInMsgMode = 3;
      if (gbMsgFilterMode != 9)
        gbInMsgMode = 4;
    } else
      gbInMsgMode = 4;
  }

  textbox_ctrl_update(pDlg);

  pEdit = ctrl_find_ndx(pDlg, 5);
  if (gbInMsgMode == 0 && pEdit->pData != NULL)
    ctrl_showhide(pEdit, 1);
  else
    ctrl_showhide(pEdit, 0);
}

//
// The filter word is saved in DI and restored on every path. Mode 2 is the
// broadcast case, so it opens the mask wide first.
void packet_send_string(const char *pszText) {
  WORD wSaved = gwMsgFilter_g;

  switch (gbInMsgMode) {
    case 3:
      update_message_filter();
      net_send_string(pszText);
      gwMsgFilter_g = wSaved;
      return;
    case 2:
      gwMsgFilter_g = 0xFFFF;
      break;
  }
  net_send_string(pszText);
  gwMsgFilter_g = wSaved;
}

//
// Controls 1..4 are the four send-target labels; exactly one is shown and kept
// in EDI, and the edit control (6) is then moved right past its text.
void textbox_ctrl_update(ctrl_t *pCtrl) {
  ctrl_t *pShown = NULL;
  ctrl_t *pLabel;
  int nWidth;

  pLabel = ctrl_find_ndx(pCtrl, 1);
  if (gbInMsgMode == 1) {
    ctrl_show(pLabel);
    pShown = pLabel;
  } else {
    ctrl_hide(pLabel);
  }

  pLabel = ctrl_find_ndx(pCtrl, 2);
  if (gbInMsgMode == 3) {
    ctrl_show(pLabel);
    pShown = pLabel;
  } else {
    ctrl_hide(pLabel);
  }

  pLabel = ctrl_find_ndx(pCtrl, 3);
  if (gbInMsgMode == 2) {
    ctrl_show(pLabel);
    pShown = pLabel;
  } else {
    ctrl_hide(pLabel);
  }

  pLabel = ctrl_find_ndx(pCtrl, 4);
  if (gbInMsgMode == 4) {
    sub_4B3EB0(pLabel);
    ctrl_show(pLabel);
    pShown = pLabel;
  } else {
    ctrl_hide(pLabel);
  }

  pLabel = ctrl_find_ndx(pCtrl, 6);
  if (gbInMsgMode != 0) {
    dlg_set_font(pCtrl);
    nWidth = font_strlen(static_cast<const char *>(pShown->pData));
    font_set(NULL);
    pLabel->rc.left = static_cast<short>(gbFontCharExtra + nWidth);
    ctrl_show(pLabel);
  } else {
    ctrl_hide(pLabel);
  }
}

//
// Builds the "to <name>" prompt for the private-message label. Control -10 of
// the parent dialog holds the format string; 0x6726D3 is &gPlayerData[i].szName
// (base 0x6726C8 + 11), reached through the base and never as a global.
void sub_4B3EB0(ctrl_t *pCtrl) {
  ctrl_t *pFormat;
  const char *pszName;
  int nWidth;

  pFormat = ctrl_find_ndx(pCtrl->pParent, -10);
  if (IsTeamGame())
    pszName = GetTeamName(gbMsgFilterMode);
  else
    pszName = gPlayerData[gbMsgFilterMode].szName;

  psnprintf(sgszToPlayerPrompt, 0x32,
            static_cast<const char *>(pFormat->pData), pszName);
  pCtrl->pData = sgszToPlayerPrompt;
  dlg_set_font(pCtrl->pParent);
  nWidth = font_strlen(static_cast<const char *>(pCtrl->pData));
  font_set(NULL);
  pCtrl->rc.right = static_cast<short>(nWidth);
}
