// opt_dlgs.h -- declarations for src117/lang/opt_dlgs.cpp, the in-game menu
// stack: the F10 game menu and its abort, options, help, quit and quit-to-menu
// children, the mission-objectives dialog, and the option-dialog alloc, free
// and close plumbing they all share.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_OPT_DLGS_H
#define OC117_OPT_DLGS_H

#include "platform.h"
#include "dialog.h"      // ctrl_t, dlgevt_t

// Every `*_respond` takes the dialog in ECX, though three never read it: they
// are callbacks stored in one slot, menu_dlg_fcn calls that slot with the
// dialog still in ECX, and so all of them share one type.

// The deferred-respond slot's type. sgpRespondFcn at 0x675068 holds the action
// the menu leaves pending, and menu_dlg_fcn fires it on event id 2.
typedef void (PFN_RESPOND)(ctrl_t *pDlg);

// The menu stubs, 25 B each: they arm the deferred action, then end in a tail
// jump to modeless_dlg_startup with menu_dlg_fcn and the .rdata template, an
// object. do_quit_to_menu_dlg is 54 B because it guards first.
void do_abrtmenu_dlg();
void do_gamemenu_dlg();
void do_helpmenu_dlg();
void do_optsmenu_dlg();
void do_quit_dlg();
void do_quit_to_menu_dlg();

// The dialog event dispatch: a dense switch on the event id that returns 1 on
// the handled arms and otherwise ends in a tail jump to modeless_opt_dlg.
BOOL menu_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
BOOL objective_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
BOOL modeless_menu_deactivate(ctrl_t *pCtrl, dlgevt_t *pEvent);

// The activates. ECX is the dialog, still live from menu_dlg_fcn's case 0.
void abortmenu_activate(ctrl_t *pDlg);
void gamemenu_activate(ctrl_t *pDlg);
void menu_activate(ctrl_t *pDlg);
void objective_activate(ctrl_t *pDlg);

// The responds. Each clears the slot and branches on gnDlgReturnVal; several
// arm it again before a tail jump back into modeless_dlg_startup.
void abortmenu_respond(ctrl_t *pDlg);
void abortmenu_restart_respond(ctrl_t *pDlg);
void gamemenu_respond(ctrl_t *pDlg);
void helpmenu_respond(ctrl_t *pDlg);
void optsmenu_respond(ctrl_t *pDlg);
void quit_respond(ctrl_t *pDlg);
void quit_replay_respond(ctrl_t *pDlg);
void quit_to_menu_respond(ctrl_t *pDlg);

// The option-dialog plumbing. option_dlg_free is a callback, nullary as far as
// its body shows; option_dlg_cmd's ECX is a pointer the bytes do not name.
intptr_t  option_dlg_alloc();
void option_dlg_free();
void option_dlg_cmd(void *pArg0);
void opt_dlg_generic_close(ctrl_t *pDlg);   // tail jmp -> modeless_dlg_shutdown
void opts_init();
void opts_free();

// The objectives dialog.
void objective_set_greed_str(ctrl_t *pDlg);

// The abort-menu countdown, a PFN_TIMER_FN for timer_create: EDX is the timer
// id, which the body does not read.
void abortmenu_update_timer(ctrl_t *pDlg, int nId);

// The members still unnamed. sub_47C110's EDX is a short, as replay.cpp's call
// site spells it.
void sub_47B3C0(intptr_t nArg0, int nArg1, int nArg2, void *pArg3);   // ret 8h
void gamemenu_timer_fn(ctrl_t *pDlg);                  // tail jmp -> ctrl_set_gray
// sub_47B760 takes a file name: it is compared with the name cache at 0x675070,
// handed to ReadGroupFile and copied into the cache.
void sub_47B760(const char *pszFile);
// objective_cmd takes the dialog, though its body never reads it; the parameter
// gives objective_dlg_fcn 1.17's register allocation.
void objective_cmd(ctrl_t *pDlg);
void opt_dlg_set_blend(ctrl_t *pDlg);
intptr_t  opt_get_tile_grp();
BYTE sub_47C110(intptr_t nArg0, short nArg1, int nArg2, int nArg3);    // ret 8h
void dfl_load_bmp(ctrl_t *pDlg, const char *pszFile);

// Callees in other TUs that no other header of ours declares.
// modeless_dlg_shutdown's parameter is one byte: menu_dlg_fcn passes it as
// `xor cl, cl`.
void modeless_dlg_shutdown(BYTE bDeactivate);

// effects_free is declared in dlg_effects.h, and IsBNetGame in net_misc.h.
void victdlg_quit();
// Hyperion: Quit Mission's leaving with no dialog, as quit_to_menu_respond leaves
void PortQuitToMenu(void);
// do_tips_dlg takes one register argument, stored first to a DWORD global;
// whether it is a pointer the bytes cannot say, so int is a placeholder.
void do_tips_dlg(int nArg0);
void do_help_dlg();

// The four option-dialog children optsmenu_respond dispatches to, nullary
// stubs of the menu-stub shape.
void do_speed_dlg();
void do_sound_dlg();
void do_video_dlg();
void do_net_dlg();

// The CRT's _stricmp at 0x4D1105, __cdecl, in an extern "C" block.
extern "C" {
int __cdecl _stricmp(const char *psz1, const char *psz2);
}

// 0x476F60 (17 B), abortmenu_update_timer's second test. Nullary; its one
// caller only tests EAX, so int is a placeholder.
int sub_476F60();

#endif  // OC117_OPT_DLGS_H
