// status.h -- declarations for src117/lang/status.cpp: the status console
// shell. It loads the console art, owns the current selection, and drives the
// per-frame update the rest of the family hangs off. Free functions are
// __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_STATUS_H
#define OC117_STATUS_H

#include "platform.h"

// Three of this TU's functions are declared where their callers live:
// status_get_console_art (0x4A7C40) in minimap.h, status_get_str (0x4A7D70) in
// order.h and status_set_update (0x4A7ED0) in CUnitTSpells.h.

// The six below are nullary and return void; three end in a tail jump.

// 0x4A7BC0 (125 B). Drop everything the console loaded.
void status_free();

// 0x4A7CD0 (156 B). Recompute the selection the panel is showing. Tail jmps
// into sub_4A18D0.
void status_get_new_selection();

// 0x4A7D80 (146 B). Build the console. Tail jmps into request_statdata_update.
void status_init();

// 0x4A7E20 (140 B). Load the console artwork and its mask.
void status_load_console();

// 0x4A7EB0 (31 B). Load the panel's string table.
void status_load_strings();

// 0x4A7EE0 (36 B). The per-frame entry point. Tail jmps into statdata_update.
void status_update();

#endif  // OC117_STATUS_H
