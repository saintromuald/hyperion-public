// gluMain.h -- declarations for src117/lang/gluMain.cpp: the main menu, with the
// campaign-presence predicates it grays its buttons with, the StarEdit launcher, and the
// screen's activate and event handlers.
//
// Free functions are spelled without an explicit __fastcall, which /Gr already makes the
// default. A parameter spelled `int` or `void *` is a placeholder with a proven 4-byte
// footprint and nothing more; name one when a call site proves it, and say which site did.

#ifndef OC117_GLUMAIN_H
#define OC117_GLUMAIN_H

#include "platform.h"
#include "dialog.h"

// 0x45A690 (30 B)
int HasCampaign();

// 0x45A6B0 (69 B)
BOOL HasEitherCampaign();

// 0x45A700 (39 B)
BOOL HasExpansionCampaign();

// 0x45A730 (29 B) -- Reached only by address, so the argument list is a lower bound.
// ECX is only forwarded to a callee, so its width comes from that callee. EDX is only
// forwarded to a callee, so its width comes from that callee. The body never reads
// arg1 and arg2, so only their 4-byte footprints are known.
// Hyperion: dlg_load consumes the result, so it returns the block.
void *glumain_load_fn(intptr_t nArg0, int nArg1, intptr_t nArg2, int nArg3, int nArg4);

// 0x45A750 (139 B)
int do_gamemode_select_dlg(int nArg0);

// 0x45A7E0 (197 B)
BOOL glumain_file_exists(int nArg0, intptr_t nArg1);

// Hyperion: the game chosen before single player (3) or multiplayer (4) leaves the
// menu, as the game mode dialog's choice sets it; false when its campaign file is missing.
int glumain_choose_product(int nChoice, int bExpansion);

// 0x45A8B0 (296 B)
void do_main_dlg();

// 0x45AA00 (246 B)
BOOL launch_staredit(intptr_t nArg0);

// 0x45AB00 (339 B) -- ECX is only forwarded to a callee, so its width comes from that
// callee.
void gluMain_dlg_activate(intptr_t nArg0);

// 0x45AC60 (343 B) -- reached only by address, so the argument list is a lower bound. The
// return is int: do_main_dlg installs it in dlg_run_modal's PFN_CTRL_EVT slot, which
// returns int, and the two tail jmps to ctrl_dlg agree. The body itself materialises an
// explicit value into EAX on every path -- `mov eax, 1h` six times, and a `movzx eax, al`
// widening glues_transition_out's BYTE result -- which a void body has no reason to emit.
int gluMain_dlg_fcn(intptr_t nArg0, intptr_t nArg1);

// Two callees of this TU declared elsewhere: SetSurveyProduct in SystemSurvey.h, and
// SFileGetInstallPath, a Storm import, in src117/h/Storm.h -- ordinal 295, three
// __stdcall arguments, with the matching `_SFileGetInstallPath@12` line in
// src117/storm_imports.def. launch_staredit is the caller here.

#endif  // OC117_GLUMAIN_H
