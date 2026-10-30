// TrigLB.h -- declarations for src117/lang/TrigLB.cpp, oracle
// 0x4BA880..0x4BAD01: the trigger leaderboard. Two aggregators walk the
// nations through one of TrigEnum's counting callbacks and fill the eight
// {nation, value} pairs the status display reads, and two qsort comparators
// order them.
//
// Free functions are spelled without an explicit __fastcall: /Gr already makes
// it the default.

#ifndef OC117_TRIGLB_H
#define OC117_TRIGLB_H

#include "platform.h"
#include "TrigEnum.h"   // NationEnumCb, the counting callbacks the aggregators walk

// The eight {nation, value} pairs, in place. Declared here rather than at its
// seat in StatLB.cpp, its only consumer, because a hooked free function needs a
// header. `long *`, not void *: all three consuming callers subscript it.
LONG *get_lb_values();
// Hyperion: the records' size, 0x40 in 1.17, every slot's in a Hyperion game.
int lb_values_bytes();

// Walks the nations, calls the counting callback for each and accumulates the
// shortfall against the goal at 0x5349F0 rather than the raw count. ECX is the
// callback and EDX the value handed to it, a WORD: lb_update loads it with
// `mov dx` and the one call forwards the register unwidened.
void agg_lb_values_togoal(NationEnumCb pfnCount, WORD nParam);

// Rebuilds the whole leaderboard and sorts it. Nullary; EAX at the ret is
// qsort's leftover, which no caller reads.
void lb_update();

// The same nation walk as agg_lb_values_togoal, storing the raw count. Its
// second argument to the callback is a WORD read out of 0x5349EE.
void agg_lb_values(NationEnumCb pfnCount);

// Picks the counting callback and the parameter for the current leaderboard
// type and dispatches into agg_lb_values_togoal. Nullary.
void sub_4BABF0();

// The two qsort comparators, over 8-byte {nation, value} pairs. int, not void:
// each body materialises -1, 0 or +1 into EAX, and qsort is their only consumer,
// indirectly.
int __cdecl lb_sort_ascending_cb(const void *pA, const void *pB);
int __cdecl lb_sort_descending_cb(const void *pA, const void *pB);

#endif  // OC117_TRIGLB_H
