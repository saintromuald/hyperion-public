// statwrite.h -- declarations for src117/lang/statwrite.cpp: the two
// game-creation log writers, split from SystemSurvey. Free functions are
// __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_STATWRITE_H
#define OC117_STATWRITE_H

#include "platform.h"

// 0x4A8B50 (627 B). Tallies the game creation log into Protocol.json. Nullary,
// though its aligned-frame prologue can read as taking ECX: neither call site
// sets it. Both exits set AL only and both callers discard it.
bool write_game_creation_log();

// 0x4A8DD0 (341 B). ECX is the network provider id; appends one line to the
// game creation log. Every exit sets AL, and the one caller discards it.
bool sub_4A8DD0(DWORD dwProvider);

#endif  // OC117_STATWRITE_H
