// gluChat.h -- declarations for src117/lang/gluChat.cpp: the Battle.net chat room's
// dialog, and the lobby entries src117/lang/net_chat_cmds.cpp reaches.
//
// Free functions are spelled without an explicit __fastcall, which /Gr already makes the
// default.

#ifndef OC117_GLUCHAT_H
#define OC117_GLUCHAT_H

// <stddef.h> comes before dialog.h on purpose: dialog.h's static_asserts use offsetof
// and it does not include <stddef.h> itself.
#include <stddef.h>
#include "platform.h"
#include "dialog.h"
#include "rect16.h"

// 0x4506B0 (136 B) -- ECX read as 8 bits, EDX never read.
void on_chrm_kicked(BYTE bReason);

// 0x451770 (11 B) -- nullary; the body tail-jumps to 0x451AD0.
void sub_451770();

// 0x451790 (11 B) -- the lobby's redraw-request entry; nullary. Seven
// net_chat_cmds.cpp receivers call it.
void sub_451790();

// 0x4517A0 (34 B) -- nullary: the body loads ECX itself from 0x667C04, then tail-jumps
// to ctrl_hide.
void sub_4517A0();

// 0x451680 (232 B) -- CL is the seconds left; the body tests it against 1 and 3, then
// tail-jumps to ctrl_set_text. Its one caller, uiMulti.cpp's update_countdown, tail-jumps
// here with CL still holding its own argument.
void updateChrmCountdown(BYTE bSecs);

// 0x451640 (59 B) -- ECX is the player's name, which the four call sites in uiMulti pass
// as `&gPlayerData[i].szName` or a stack copy of it; the return is a zero-extended byte
// all five sites consume.
BOOL ConfirmKick(const char *pszName);

// 0x4505F0 (77 B) -- ECX is only forwarded to a callee, so its width comes from that
// callee.
void sub_4505F0(intptr_t nArg0);

// 0x450640 (100 B) -- Reached only by address, so the argument list is a lower bound;
// the indirect call site is the authority.
void sub_450640(intptr_t nArg0);

// 0x450760 (72 B)
void gluChat_hover_minimap_preview(intptr_t nArg0);

// 0x4507B0 (315 B)
void gluChat_mouse_moved(intptr_t nArg0, int nArg1, int nArg2);

// 0x4508F0 (24 B) -- ECX and EDX are pointers whose class the bytes do not name.
BOOL ctrl_mapinfo_text(void *pArg0, void *pArg1);

// 0x450910 (101 B)
void gluChat_dlg_activate(intptr_t nArg0);

// 0x450980 (73 B)
void sub_450980(int nArg0, int nArg1);

// 0x4509D0 (348 B)
BOOL gluChat_cmd(intptr_t nArg0, int nArg1);

// 0x450B30 (498 B) -- Reached only by address, so the argument list is a lower bound;
// the indirect call site is the authority.
BOOL gluChat_dlg_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x450D50 (40 B) -- ECX and EDX are pointers whose class the bytes do not name.
BOOL ctrl_btn_ok_ctrl(void *pArg0, void *pArg1);

// 0x450D80 (283 B) -- Reached only by address, so the argument list is a lower bound;
// the indirect call site is the authority.
void gluChat_poll(intptr_t nArg0);

// 0x450EA0 (77 B)
void gluChat_start_poll(intptr_t nArg0);

// 0x450EF0 (435 B)
void gluChat_update_game_info(intptr_t nArg0);

// 0x4510B0 (134 B)
void gluChat_update_password(intptr_t nArg0);

// 0x451140 (447 B) -- The body never reads arg0, so only its 4-byte footprint is
// known.
void gluChat_write_msg(intptr_t nArg0, int nArg1, int nArg2);

// 0x451310 (67 B)
BOOL ctrl_edit_msgtosend_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x451360 (100 B)
void ctrl_edit_msgtosend_char(intptr_t nArg0, intptr_t nArg1);

// 0x4513D0 (121 B)
BOOL ctrl_listbox_msgs_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x451470 (233 B)
void do_gluChat_dlg();

// 0x451560 (223 B)
void on_recv_chat_from(int nArg0, intptr_t nArg1);

#endif  // OC117_GLUCHAT_H
