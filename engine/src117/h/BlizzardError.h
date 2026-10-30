// BlizzardError.h -- declarations for src117/lang/BlizzardError.cpp, the crash
// reporter's launch of the error inspector. Free functions are __fastcall
// through /Gr and are declared without the keyword.

#ifndef OC117_BLIZZARDERROR_H
#define OC117_BLIZZARDERROR_H

#include "platform.h"

// 0x402CA0 (75 B). Appends one argument to the command line SubmitReport hands
// the inspector, quoting it when it contains a space. ECX is the argument and
// nothing else is live in; the one caller discards EAX.
void PushArg(const char *pszArg);

#endif  // OC117_BLIZZARDERROR_H
