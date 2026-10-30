// statcmd.h -- declarations for src117/lang/statcmd.cpp: the command card, the
// nine-button action panel under the minimap, its tooltips, its hotkey dispatch,
// and the replay progress bar that replaces it during playback.
//
// Class members belong in the class header; this file owns the free functions,
// spelled without an explicit __fastcall, which /Gr already makes the default.
// Parameter names are ours; a pointee is named where a body or its call site
// shows it.

#ifndef OC117_STATCMD_H
#define OC117_STATCMD_H

#include "platform.h"
#include "dialog.h"    // ctrl_t, dlgevt_t, rect16_t

class CUnit;

// Hyperion: moved here from statcmd.cpp, unchanged, for the HUD's data layer
// (port_hudstate.cpp), which reads a card's buttons as statcmd_show_btn_tooltip does.
// statcmd_btn_t -- one command-card button, what ctrl_t +0x26 points at; a
// card's buttons are an array. The availability test takes bConditionArg, the
// local player and the selected unit, and a negative result grays the button,
// zero skips it. The action takes w0E and the shift key's byte. wMinIndex is
// the lowest control index the button may take, compared signed;
// wDisabledStrId can be overwritten with 0x2FA.
struct statcmd_btn_t {
  /*0x00*/ WORD wMinIndex;
  /*0x02*/ WORD wIcon;
  /*0x04*/ int (__fastcall *pfnCondition)(int nArg, int nPlayer, CUnit *pUnit);
  /*0x08*/ void (__fastcall *pfnAction)(WORD wArg, BYTE bArg);
  // Hyperion: 16 bits on x64, a mod's unit, tech or upgrade past 255
#if defined(_M_IX86)
  /*0x0C*/ BYTE bConditionArg;
#else
  WORD bConditionArg;
#endif
  /*0x0D*/ BYTE _unmapped0D;
  /*0x0E*/ WORD w0E;
  /*0x10*/ WORD wStatusStrId;
  /*0x12*/ WORD wDisabledStrId;
};
OC_X86_LAYOUT(sizeof(statcmd_btn_t) == 0x14, "statcmd_btn_t size           add esi, 14h");
static_assert(offsetof(statcmd_btn_t, wMinIndex)      == 0x00, "statcmd_btn_t::wMinIndex      movzx eax, word ptr [esi]");
static_assert(offsetof(statcmd_btn_t, wIcon)          == 0x02, "statcmd_btn_t::wIcon          movzx eax, word ptr [esi+2h]");
OC_X86_LAYOUT(offsetof(statcmd_btn_t, pfnCondition)   == 0x04, "statcmd_btn_t::pfnCondition   mov eax, dword ptr [esi+4h] / call eax");
OC_X86_LAYOUT(offsetof(statcmd_btn_t, pfnAction)      == 0x08, "statcmd_btn_t::pfnAction      mov eax, dword ptr [eax+8h] / call eax");
OC_X86_LAYOUT(offsetof(statcmd_btn_t, bConditionArg)  == 0x0C, "statcmd_btn_t::bConditionArg  movzx ecx, byte ptr [esi+0Ch]");
OC_X86_LAYOUT(offsetof(statcmd_btn_t, w0E)            == 0x0E, "statcmd_btn_t::w0E            mov cx, word ptr [eax+0Eh]");
OC_X86_LAYOUT(offsetof(statcmd_btn_t, wStatusStrId)   == 0x10, "statcmd_btn_t::wStatusStrId   mov cx, word ptr [eax+10h]");
OC_X86_LAYOUT(offsetof(statcmd_btn_t, wDisabledStrId) == 0x12, "statcmd_btn_t::wDisabledStrId mov word ptr [esi+12h], ax");

#define ctrl_p26(p) (*reinterpret_cast<statcmd_btn_t **>(&(p)->_unmapped24[2]))

// 0x4A0620 statcmd_hotkey_cmp_fn (58 B). The predicate ctrl_chainr walks the
// card with, returning 1 when the button's hotkey is the typed character. Its
// one call site passes the address of a one-byte local through ctrl_chainr's
// dlgevt_t * slot and the body reads one byte there, so this takes a
// `const char *` and the cast is at the call site.
int statcmd_hotkey_cmp_fn(ctrl_t *pCtrl, const char *pch);

// 0x4A0660 statcmd_btn_ctrl_fcn (181 B). The command button's event handler, a
// PFN_CTRL_EVT: its tail dispatches through the per-type table with the same
// two arguments.
int statcmd_btn_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4A0740 statcmd_btn_ctrl_mouse_moved (101 B). Passes its control's parent and
// the event to ctrl_find. Void.
void statcmd_btn_ctrl_mouse_moved(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4A07B0 statcmd_dlg_char_evn (85 B). The parameter is the event, whose +0x08
// virtual key and +0x0A modifier bits the body reads. An int, the ctrl_send_msg
// it returns.
int statcmd_dlg_char_evn(dlgevt_t *pEvent);

// 0x4A0810 statcmd_replay_progress_activate (199 B). Installs
// statcmd_replay_progress_draw in its control's +0x2E draw slot.
void statcmd_replay_progress_activate(ctrl_t *pCtrl);

// 0x4A08E0 statcmd_replay_progress_deactivate (59 B). The control is unused;
// statcmd_replay_progress_fcn passes it, which gives that body 1.17's registers.
void statcmd_replay_progress_deactivate(ctrl_t *pCtrl);

// 0x4A0920 statcmd_replay_progress_draw (202 B). A PFN_CTRL_DRAW, installed at
// +0x2E by the activate above; only the control is read.
void statcmd_replay_progress_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                  rect16_t *prcClip);

// 0x4A09F0 statcmd_replay_progress_fcn (54 B). PFN_CTRL_EVT-shaped, returning
// void.
void statcmd_replay_progress_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4A0A30 DrawTooltipText (314 B). Takes the buffer statcmd_show_tooltip
// formatted.
void DrawTooltipText(char *pszText);

// 0x4A0B70 statcmd_dlg_fcn (108 B). The dialog's event handler, registered
// through dlg_activate; every path returns an explicit value.
BOOL statcmd_dlg_fcn(ctrl_t *pDlg, dlgevt_t *pEvent);

// 0x4A0C00 statcmd_btn_ctrl_draw_fn (424 B). The button's PFN_CTRL_DRAW, which
// statcmd_btn_ctrl_fcn installs; only the control is read.
void statcmd_btn_ctrl_draw_fn(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                              rect16_t *prcClip);

// 0x4A0DC0 sub_4A0DC0 (191 B). Draws the tooltip's cost line under the
// rectangle and grows it to fit.
void sub_4A0DC0(rect16_t *prc);

// 0x4A0E80 request_statcmd_update (11 B). Nullary, void.
void request_statcmd_update();

// 0x4A0E90 statcmd_deactivate (88 B). Nullary, void.
void statcmd_deactivate();

// 0x4A0EF0 statcmd_hide_tooltip (34 B). Nullary, void.
void statcmd_hide_tooltip();

// 0x4A0F20 statcmd_init (277 B). Nullary, void.
void statcmd_init();

// 0x4A1040 statcmd_dlg_mouse_moved (54 B). Forwards both its arguments to
// ctrl_find. Void.
void statcmd_dlg_mouse_moved(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4A1080 statcmd_set_action_btns (349 B). Nullary, void.
void statcmd_set_action_btns();

// 0x4A11E0 statcmd_set_ctrl (12 B). Stores the special card and tail-calls
// statcmd_update. The parameter is an int: every caller loads all of ECX.
void statcmd_set_ctrl(int nCard);

// 0x4A11F0 statcmd_set_tooltip_data (75 B). `ret 18h`: two register words and
// six stack words. The stack words are WORDs: statcmd_show_btn_tooltip's case 1
// pushes a zero-extended 16-bit result, which an int parameter does not give.
void statcmd_set_tooltip_data(WORD wArg0, WORD wArg1, WORD wArg2, WORD wArg3, WORD wArg4, WORD wArg5, WORD wArg6, WORD wArg7);

// 0x4A1240 statcmd_show_btn_tooltip (837 B). The command card dialog and the
// control under the cursor. Void.
void statcmd_show_btn_tooltip(ctrl_t *pDlg, ctrl_t *pCtrl);

// 0x4A15A0 sub_4A15A0 (344 B). Picks the buttonset for a selection of several
// units. Nullary, void.
void sub_4A15A0();

// 0x4A1700 statcmd_show_tooltip (92 B). The string is the first %s of the
// tooltip's two-line template.
void statcmd_show_tooltip(const char *psz);

// 0x4A1760 statcmd_update (353 B). Nullary, void.
void statcmd_update();

// 0x4A18D0 sub_4A18D0 (121 B). Nullary, void.
void sub_4A18D0();

// 0x4A1950 sub_4A1950 (139 B). Nullary, void.
void sub_4A1950();

// Hyperion: a card an EUD map points at buttons in its own memory. Each button is
// 20 bytes as 1.16.1 keeps it, its condition and action given as the engine's functions, two to a
// button in ppfn; the card points at the engine's copy until the next call for it, which builds
// it again. StatcmdEudButtonsClear lets go of every copy, once the cards are 1.17's again.
void StatcmdEudButtons(WORD wCard, const BYTE *pButtons, int nButtons, void *const *ppfn);
void StatcmdEudButtonsClear();

#endif  // OC117_STATCMD_H
