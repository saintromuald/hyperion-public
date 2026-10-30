// netdlg.h -- declarations for src117/lang/netdlg.cpp, the network-options
// dialog: the latency radio group and the command that opens it. Free functions
// are __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_NETDLG_H
#define OC117_NETDLG_H

#include "platform.h"

struct ctrl_t;
struct dlgevt_t;

// 0x46F990 (15 B). Nullary; it ends in a tail jump to modeless_dlg_startup, so
// void.
void do_net_dlg();

// 0x46F9A0 (88 B). ECX only. It ends in tail jumps to do_optsmenu_dlg and
// dlg_deactivate, both void.
void net_dlg_cmd(ctrl_t *pDlg);

// 0x46FA00 (108 B). The dialog's own PFN_CTRL_EVT, whose address do_net_dlg
// hands modeless_dlg_startup in EDX. ECX and EDX are the pair every sibling
// handler takes; two arms return an explicit 1.
BOOL net_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x46FA90 (34 B). The PFN_CTRL_EVT the three latency radio buttons share; all
// three slots of the table at 0x508D20 hold this one address. It returns what
// its tail jump to btn_ctrl_radiobutton returns.
BOOL net_dlg_radio_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x46FAC0 (79 B). ECX only. It ends in a tail jump to ctrl_set_val and
// net_dlg_radio_fcn discards EAX, so void, not the int net_get_latency returns.
void net_dlg_radio_init(ctrl_t *pCtrl);

// 0x49EE00 snd_dlg_activate (67 B), in snd_dlg.cpp; snd_dlg.h leaves its
// declaration here. ECX is read 32-bit and net_dlg_fcn discards EAX.
void snd_dlg_activate(ctrl_t *pCtrl);

#endif  // OC117_NETDLG_H
