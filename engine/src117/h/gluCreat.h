// gluCreat.h -- declarations for src117/lang/gluCreat.cpp: the create-game screen, with
// the map listbox, the game-type, sub-type, race and computer-slot dropdowns, the speed
// scroller, and the registry round-trip that persists each of those settings.
//
// Member functions live in their class header; this file owns the TU's free functions.
// They are spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_GLUCREAT_H
#define OC117_GLUCREAT_H

#include "platform.h"

struct ctrl_t;
struct dlgevt_t;
struct rect16_t;
struct TGAMEPARAMS;
struct TTEMPLATEMETA;
struct lbox_t;

// Parameter names and pointees below are placeholders wherever the bytes do not pin
// them: `int nArg0` and `ctrl_t *pCtrl` occupy the same four bytes in the same slot.
// Refine one when a call site proves it, and say there what proved it.
//
// A CRT callee this TU reaches is left out on purpose: declaring one here would collide
// with its real header.

// 0x454CA0 (146 B)
BYTE gluCreate_get_config_game_speed();

// 0x454D40 (101 B)
int gluCreate_get_game_type_setting(intptr_t nArg0);

// 0x454DB0 (88 B) -- ECX is only forwarded to a callee, so its width comes from that
// callee.
void gluCreate_speedScroll_ctrl_deactivate(intptr_t nArg0);

// 0x454E10 (63 B)
void gluCreate_gameTypeDropDown_ctrl_deactivate(intptr_t nArg0);

// 0x454E50 (455 B)
void sub_454E50();

// 0x455020 (189 B) -- ECX is only forwarded to a callee, so its width comes from that
// callee.
void InitSinglePlayerCustom(intptr_t nArg0);

// 0x4550E0 (227 B) -- Reached only by address, so the argument list is a lower bound.
// The screen's dialog handler: a PFN_CTRL_EVT whose default arm tail-jumps
// ctrl_dlg and returns its int, and whose 0x0E/2 arm returns
// `movzx eax, al` of glues_transition_out.
int create_interact_fcn(ctrl_t *pDlg, dlgevt_t *pEvent);

// 0x4551D0 (103 B)
void create_dlg_init(intptr_t nArg0);

// 0x455240 (542 B)
int sub_455240();

// 0x4554A0 (176 B)
BOOL sub_4554A0();

// 0x455550 (433 B)
void do_create_dlg(int nArg0);

// 0x455710 (41 B)
void sub_455710(int nArg0);

// 0x455740 (199 B)
BOOL sub_455740(intptr_t nArg0);

// 0x455810 (54 B) -- Reached only by address, so the argument list is a lower bound.
// The body never reads arg2, so only its 4-byte footprint is known. __stdcall: the
// body reads no register input and pops its arguments, which under /Gr is
// byte-identical to a fastcall that ignores its two register arguments.
// The return is int: the oracle keeps the item index in EBX across the lbox_set_data
// call and materialises it with `mov eax, ebx`, which compiled void is not emitted at
// all (49 bytes against 54).
int __stdcall sub_455810(intptr_t nArg0, intptr_t nArg1, int nArg2);

// 0x455850 (373 B) -- Reached only by address, so the argument list is a lower bound.
// ECX is a pointer whose class the bytes do not name. The bytes do not name what arg0
// points at.
// gluCreate_listbox_ctrl_activate installs it at the listbox's +0x52; EDX is
// the item index as a BYTE (`mov byte ptr [ebp-1h], dl`), the same shape as
// gluConn_gateway_list_item_draw.
void sub_455850(void *pArg0, BYTE bArg1, void *pArg2, int nArg3, int nArg4);

// 0x4559D0 (170 B)
BOOL gluCreate_listbox_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x455AA0 (614 B)
void gluCreate_listbox_ctrl_activate(intptr_t nArg0);

// 0x455D10 (523 B) -- Reached only by address, so the argument list is a lower bound.
// The bytes do not name what arg1 points at. __stdcall: the body reads no register
// input and pops its arguments, which under /Gr is byte-identical to a fastcall that
// ignores its two register arguments.
void __stdcall sub_455D10(int nArg0, void *pArg1, intptr_t nArg2);

// 0x455F30 (18 B) -- Reached only by address, so the argument list is a lower bound.
// The body never reads arg0, so only its 4-byte footprint is known. __stdcall: the
// body reads no register input and pops its arguments, which under /Gr is
// byte-identical to a fastcall that ignores its two register arguments.
void __stdcall sub_455F30(int nArg0, intptr_t nArg1, int nArg2);

// 0x455F50 (144 B)
void sub_455F50();

// 0x455FE0 (234 B) -- Reached only by address, so the argument list is a lower bound.
// EDX carries an argument the body never reads, so `int` is a placeholder. The body
// never reads arg0 and arg1, so only their 4-byte footprints are known.
// create_interact_fcn stores it into a control's +0x2E slot, so it is a
// PFN_CTRL_DRAW (dialog.h).
void draw_glucreate_directory_label(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);

// 0x4560D0 (65 B)
void sub_4560D0(intptr_t nArg0);

// 0x456120 (79 B)
BOOL gluCreate_selfSlotDropDown_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x456170 (84 B)
void gluCreate_raceDropDown_ctrl_activate(intptr_t nArg0);

// 0x4561D0 (54 B)
BOOL gluCreate_raceDropDown_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x456210 (122 B)
void gluCreate_computerSlotDropDown_ctrl_activate(intptr_t nArg0);

// 0x456290 (75 B)
BOOL gluCreate_computerSlotDropDown_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x4562E0 (99 B) -- EDX is only forwarded to a callee, so its width comes from that
// callee.
void gluCreate_computerSlotDropDown_ctrl_select(intptr_t nArg0, intptr_t nArg1);

// 0x456350 (199 B)
void gluCreate_computerSlotDropDown_update(intptr_t nArg0, int nArg1);

// 0x456420 (420 B)
void gluCreate_computerSlots_update(intptr_t nArg0, int nArg1);

// 0x4565D0 (141 B)
BOOL gluCreate_speedScroll_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x456680 (158 B)
void gluCreate_speedScroll_updateText(intptr_t nArg0);

// 0x456720 (92 B) -- Reached only by address, so the argument list is a lower bound.
// The bytes do not name what arg2 points at. The body never reads arg0, arg1 and
// arg3, so only their 4-byte footprints are known. __stdcall: the body reads no
// register input and pops its arguments, which under /Gr is byte-identical to a
// fastcall that ignores its two register arguments.
// A TEMPLATEPROC (gametype.h): IterateLeagueTemplates is the indirect caller.
void __stdcall gluCreate_subTypeDropDown_add_item(const char *pszName, const char *pszParam,
                                                  const TGAMEPARAMS *pParams,
                                                  const TTEMPLATEMETA *pMeta);

// 0x456780 (165 B)
BOOL gluCreate_subTypeDropDown_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x456830 (221 B)
void gluCreate_subTypeDropDown_init_items(intptr_t nArg0);

// 0x456910 (126 B)
void gluCreate_gameTypeDropDown_add_gametypes(intptr_t nArg0);

// 0x456990 (58 B) -- Reached only by address, so the argument list is a lower bound.
// The body never reads arg1 and arg3, so only their 4-byte footprints are known.
// __stdcall: the body reads no register input and pops its arguments, which under /Gr
// is byte-identical to a fastcall that ignores its two register arguments.
// A TEMPLATEPROC (gametype.h): IterateMainTemplates is the indirect caller.
void __stdcall sub_456990(const char *pszName, const char *pszParam,
                          const TGAMEPARAMS *pParams, const TTEMPLATEMETA *pMeta);

// 0x4569D0 (103 B)
BOOL gluCreate_gameTypeDropDown_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x456A40 (263 B)
void gluCreate_gameTypeDropDown_ctrl_select(intptr_t nArg0);

// 0x4C7090 lbox_set_match (405 B) -- `ret`, ECX + EDX in. Both callers pass a
// listbox in ECX and a local text buffer in EDX, and read only AL back.
unsigned char lbox_set_match(lbox_t *pBox, const char *pszText);

#endif  // OC117_GLUCREAT_H
