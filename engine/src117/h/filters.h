// filters.h -- the ally, vision and message filter dialogs, 0x441980 to
// 0x442B1F. An event handler takes (ctrl_t *, dlgevt_t *) in ECX and EDX; an
// activate, init or command handler takes the owning ctrl_t.

#ifndef OC117_FILTERS_H
#define OC117_FILTERS_H

#include "platform.h"
#include "dialog.h"

// 0x441C00. A PFN_CTRL_DRAW: it fills the control's rect with the palette entry
// its +0x26 slot indexes, then draws the label. EDX (nXOfs) is never read.
// net_time.cpp uses it too.
void color_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);

// 0x441980.
BOOL allied_victory_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x441A60. Reads the per-player filter bytes at 0x6662A0, indexed by the
// control's +0x26 slot.
BOOL filter_checkbox_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x441AE0. Fills the output array in ECX. nCount is never read; every caller
// passes 8, the nation-slot count.
int sub_441AE0(int *pNations, int nCount);

// 0x441B70.
void do_ally_filter_dlg();

// 0x441BE0.
void do_msg_filter_dlg();

// 0x441C60.
void teamfilter_dlg_activate(ctrl_t *pDlg);

// 0x441CB0.
void teamfilter_dlg_cmd(ctrl_t *pDlg);

// 0x441DD0.
BOOL teamfilter_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x441E60.
void teamfilter_dlg_init(ctrl_t *pDlg);

// 0x441EC0.
void msgfilter_dlg_init(ctrl_t *pDlg);

// 0x441F00.
void msgfilter_dlg_cmd(ctrl_t *pDlg);

// 0x441FD0.
BOOL msgfltr_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x442070. net_cmds.cpp also declares it, as `void __fastcall
// update_message_filter();`; the two must stay one mangled name.
void update_message_filter();

// 0x442320.
void sub_442320(ctrl_t *pDlg);

// 0x442490.
void sub_442490(ctrl_t *pDlg);

// 0x4426D0.
BOOL vision_checkbox_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x442740. ECX is a team id, not a control, compared at 32 bits against each
// player's zero-extended team byte. int, not BYTE: the caller widens it with
// movzx before the call.
void sub_442740(int nTeam);

// 0x4428B0.
void sub_4428B0(ctrl_t *pDlg);

// 0x442910. The control and event pair sub_442960 forwards to it untouched;
// both hand it on to btn_ctrl_radiobutton.
void sub_442910(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x442960.
BOOL sub_442960(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4429A0.
void sub_4429A0(ctrl_t *pDlg);

// 0x442AF0.
BOOL sub_442AF0(ctrl_t *pCtrl, dlgevt_t *pEvent);

#endif  // OC117_FILTERS_H
