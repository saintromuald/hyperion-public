// clock.h -- declarations for src117/lang/clock.cpp: the time-stamp counter
// reads Blizzard kept in asm\clock.asm. Free functions are __fastcall through
// /Gr and are declared without the keyword.

#ifndef OC117_CLOCK_H
#define OC117_CLOCK_H

#include "platform.h"

// Each takes the 8-byte destination in ECX and stores EDX:EAX from rdtsc there.
// None has a direct caller. EDX:EAX still holds the stamp at `ret`, so an
// `unsigned __int64` return would be the same code; they are declared void.

// 0x405C70 (8 B).
void sub_405C70(unsigned __int64 *pStamp);

// 0x405C78 (9 B). Disables interrupts (`cli`) first and leaves them disabled.
void sub_405C78(unsigned __int64 *pStamp);

// 0x405C81 (9 B). Enables interrupts (`sti`) between the read and the store.
void sub_405C81(unsigned __int64 *pStamp);

#endif  // OC117_CLOCK_H
