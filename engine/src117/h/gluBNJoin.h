// gluBNJoin.h -- declarations for src117/lang/gluBNJoin.cpp, the Battle.net
// join screen's two Storm UI callbacks.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_GLUBNJOIN_H
#define OC117_GLUBNJOIN_H

#include "platform.h"
#include "dialog.h"

// 0x44E060 (101 B). Storm's category-list callback, reached only through the
// SNet UI table. `ret 8h`: the UI record and, spelled as an integer, the
// callback that emits each category row. No caller reads EAX.
void __stdcall UiCategoryListCallback(void *pArg0, intptr_t nArg1);

// 0x44E0D0 (62 B). The TEMPLATEPROC UiCategoryListCallback hands to
// IterateMainTemplates. `ret 10h`: four stack words, the second and the fourth
// never read, spelled as integers and cast in the body.
void __stdcall sub_44E0D0(intptr_t nArg0, int nArg1, intptr_t nArg2, int nArg3);

#endif  // OC117_GLUBNJOIN_H
