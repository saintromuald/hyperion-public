// playflic.h -- declarations for src117/lang/playflic.cpp, the full-screen
// cinematic player. Free functions are __fastcall through /Gr and are declared
// without the keyword.

#ifndef OC117_PLAYFLIC_H
#define OC117_PLAYFLIC_H

#include "platform.h"

struct dlgevt_t;

// 0x47EC30 (37 B). The event hook play_flic installs; ECX the event.
void flicEventCB(dlgevt_t *pEvent);

// 0x47EC60 (226 B). ECX the flic index, compared unsigned against the table
// size. Returns the close flag; every caller discards it.
BOOL play_flic(int nFlic);

// CloseFlic (0x47EC20) is declared in event_Win.h.

#endif  // OC117_PLAYFLIC_H
