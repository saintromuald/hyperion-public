// repulse.h -- declarations for src117/lang/repulse.cpp, the air-unit repulsion
// field. Free functions are __fastcall through /Gr and are declared without the
// keyword.

#ifndef OC117_REPULSE_H
#define OC117_REPULSE_H

#include "platform.h"

// 0x4816C0 (38 B) / 0x481740 (28 B). Nullary; gamemap's callers discard EAX.
void repulse_free();
void repulse_init();

// 0x4816F0 (77 B). Nullary; CUnit::InitUnits discards EAX.
void repulse_init_directions();

// The TU's five CUnit members are declared in CUnit.h.

#endif  // OC117_REPULSE_H
