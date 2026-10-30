// statselect.h -- declarations for src117/lang/statselect.cpp, the
// selection-change hooks of the status console. Free functions are __fastcall
// through /Gr and are declared without the keyword.

#ifndef OC117_STATSELECT_H
#define OC117_STATSELECT_H

#include "platform.h"

// 0x4A7B60 (22 B). Nullary; tail-jumps request_statdata_update and
// select_units discards EAX.
void status_change_selection();

#endif  // OC117_STATSELECT_H
