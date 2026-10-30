// gluJoin.h -- declarations for src117/lang/gluJoin.cpp, the join-game screen:
// the game list box, its per-row formatting and selection, and the screen's
// dialog handler.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default. The intptr_t parameters stand in for
// pointers the bodies cast.

#ifndef OC117_GLUJOIN_H
#define OC117_GLUJOIN_H

#include "platform.h"
#include "dialog.h"

// 0x4590D0 (82 B). ECX only; both callers read the full EAX.
int sub_4590D0(intptr_t nArg0);

// 0x459130 (154 B). Nullary; it ends in a tail jump to glues_clear_all, so
// void.
void do_join_dlg();

// 0x459310 (572 B). ECX only; no caller reads EAX.
void gluJoin_gamelist_select(intptr_t nArg0);

// 0x459550 (134 B). ECX and EDX; every path returns a value. Reached only by
// address.
BOOL gluJoin_gamelist_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x459600 (52 B). ECX only; no caller reads EAX.
void gluJoin_gamelist_activate(intptr_t nArg0);

// 0x459640 (152 B). Nullary; no caller reads EAX.
void sub_459640();

// 0x4596E0 (267 B). ECX only; it ends in a tail jump to
// gluJoin_gamelist_select, so void.
void gluJoin_gamelist_setval(intptr_t nArg0);

// 0x4597F0 (252 B). The screen's dialog handler, installed by do_join_dlg as a
// PFN_CTRL_EVT: ECX and EDX, and it returns int, as that type and its tail
// jumps to ctrl_dlg do.
int gluJoin_dlg_fcn(intptr_t nArg0, intptr_t nArg1);

#endif  // OC117_GLUJOIN_H
