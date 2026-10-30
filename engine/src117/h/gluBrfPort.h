// gluBrfPort.h -- declarations for src117/lang/gluBrfPort.cpp, the mission
// briefing's portrait control: the talking-head SMK panel each briefing slot
// owns.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default. The SVidPlay* imports are in Storm.h;
// briefing_port_free, briefing_port_init, the three brfport_* calls, sub_4502E0
// and sub_4504C0 are declared in gluRdy.h.

#ifndef OC117_GLUBRFPORT_H
#define OC117_GLUBRFPORT_H

#include "platform.h"
#include "gluRdy.h"
#include "dialog.h"

// 0x44FF70 (89 B). ECX only; it ends in a tail jump to timer_remove_node, so
// void.
void gluBrfPort_ctrl_deactivate(intptr_t nArg0);

// 0x44FFD0 (60 B). ECX and EDX; every path returns a value. Reached only by
// address.
BOOL ctrl_brief_portrait_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x450080 (167 B). ECX only; it ends in a tail jump to ctrl_hide, so void.
void gluBrfPort_ctrl_init(intptr_t nArg0);

// 0x4501F0 (82 B). ECX and EDX, the two name arrays; no caller reads EAX.
void sub_4501F0(intptr_t nArg0, intptr_t nArg1);

// 0x450370 (83 B). The control's draw handler, reached only by address: ECX,
// an EDX it ignores, and `ret 8h` for two stack words it never reads.
void sub_450370(intptr_t nArg0, int nArg1, int nArg2, int nArg3);

// 0x4503D0 (233 B). ECX only; no caller reads EAX.
void sub_4503D0(intptr_t nArg0);

// 0x4505A0 (74 B). The control's timer callback, reached only by address: ECX
// and EDX, the timer id.
void sub_4505A0(intptr_t nArg0, int nArg1);

#endif  // OC117_GLUBRFPORT_H
