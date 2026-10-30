// TechTree.h -- declarations for src117/lang/TechTree.cpp: whether a unit, tech
// or upgrade is allowed for a player, and the reason when it is not. The free
// functions are __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_TECHTREE_H
#define OC117_TECHTREE_H

#include "platform.h"

// 0x4B3230.
void sub_4B3230();

// The TU's other functions are declared elsewhere: TTBaseUnitType in
// CUnitSearches.h and get_allowed_err and set_allowed_err in net_cmds.h, which
// the TU includes, and TTAllowed (0x4B32A0) in statbtn.h.

#endif  // OC117_TECHTREE_H
