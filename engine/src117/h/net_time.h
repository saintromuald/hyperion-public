// net_time.h -- declarations for src117/lang/net_time.cpp: elapsed game time,
// the turn clock and the drop-timeout dialog. No explicit __fastcall: /Gr
// already makes it the default.

#ifndef OC117_NET_TIME_H
#define OC117_NET_TIME_H

#include "platform.h"
#include "dialog.h"

// 0x47A7C0 (47 B). Nullary and void; recv_net_cmd_StartGame tail-jumps to it.
void net_time_init();

// 0x47A780 (50 B). Nullary; returns the elapsed seconds, which its one caller,
// sub_476F60, compares at once.
int sub_47A780();

// 0x47A810 (185 B). Nullary and void. ProgressNetwork calls it.
void sub_47A810();

// 0x47A610 (7 B). ECX is the new count.
void net_set_elapsed(int nElapsed);

// 0x47A630 (286 B). Nullary and void.
void sub_47A630();

// 0x47A750 (33 B). Nullary; the one caller reads the full EAX.
int net_elapsed_gametime();

// 0x47A7F0 (31 B). Nullary and void.
void sub_47A7F0();

// 0x47A8D0 (135 B). ECX is the array and EDX its count; no caller reads EAX.
void sub_47A8D0(intptr_t nArg0, int nArg1);

// 0x47A960 (176 B). Nullary and void; it ends in tail jumps to ctrl_showhide and
// ctrl_hide.
void timeout_activate();

// 0x47AA10 (111 B). A PFN_CTRL_EVT, spelled as in opt_dlgs.h: ECX is the dialog
// and EDX the event. It is reached only by address.
BOOL timeout_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x47AAA0 (88 B). Nullary and void; it ends in a tail jump to img_update.
void do_timeout();

// 0x47AB00 (151 B). Nullary and void.
void timeout_update_countdown();

// 0x47ABA0 (268 B). Nullary and void.
void timeout_update_got_msg();

#endif  // OC117_NET_TIME_H
