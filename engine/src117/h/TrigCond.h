// TrigCond.h -- declarations for src117/lang/TrigCond.cpp: the trigger condition
// handlers, 0x4B8E80 to 0x4B95E0. The free functions are __fastcall through /Gr
// and are declared without the keyword.

#ifndef OC117_TRIGCOND_H
#define OC117_TRIGCOND_H

#include "platform.h"
#include "victory.h"   // TrigCondition, and TRIGCONDPROC, which sets the return type

// The twenty handlers are callbacks with one signature. trigger_cond_parse
// (0x4BDED0) calls them through sgConditionFcns (0x50E058), indexed by the
// condition byte, with the condition in ECX, and tests the int they return
// (victory.h's TRIGCONDPROC). In oracle address order, the definitions' order.

// 0x4B8E90. Countdown Timer: compares the countdown at 0x5349D4 against
// pCond->dwQualifier.
int cond_timer(TrigCondition *pCond);

// 0x4B8ED0. Command: TrigEnum_PrepCounts, then units_count, counting completed
// units only when the comparison byte is not 1.
int cond_control(TrigCondition *pCond);

// 0x4B8F20. Bring: cond_control behind the location guard, with
// owned_count_atloc(bRegion - 1). Returns 0 outright when bRegion is 0.
int cond_bring(TrigCondition *pCond);

// 0x4B8F80. Accumulate: resource_count(dwGroup, bResType, 0).
int cond_accumulate(TrigCondition *pCond);

// 0x4B8FD0. Kill: kill_count(dwGroup, wUnitType, 0).
int cond_kill(TrigCondition *pCond);

// 0x4B9020. Score: score_count(dwGroup, bResType, 0).
int cond_score(TrigCondition *pCond);

// 0x4B9070. Most Control: the nation walk, maximising units_count with the
// completed flag set.
int cond_most_control(TrigCondition *pCond);

// 0x4B90E0. Most Control At Location: cond_most_control behind the location
// guard, with the owned_count_atloc refill.
int cond_most_control_atloc(TrigCondition *pCond);

// 0x4B9160. Most Kills: the nation walk over kill_count.
int cond_most_kills(TrigCondition *pCond);

// 0x4B91C0. Highest Score: the nation walk over score_count.
int cond_highest_score(TrigCondition *pCond);

// 0x4B9220. Most Resources: the nation walk over resource_count.
int cond_most_resource(TrigCondition *pCond);

// 0x4B9280. Switch: bit bResType of the eight-dword switch bitfield at 0x534F20.
// Its comparison byte is a different enum from the other handlers': 2 and 3,
// not 0, 1 and 10.
int cond_switch(TrigCondition *pCond);

// 0x4B92E0. Elapsed Time: cond_timer against g_dwElapsedTime.
int cond_elapsed(TrigCondition *pCond);

// 0x4B9320. Opponents: opponents_count(dwGroup, 0, 0), then the shared
// comparison.
int cond_opponents(TrigCondition *pCond);

// 0x4B9360. Deaths: death_count(dwGroup, wUnitType, 0).
int cond_deaths(TrigCondition *pCond);

// 0x4B93B0. Least Control: cond_most_control minimising instead, with both
// accumulators seeded to -1.
int cond_least_control(TrigCondition *pCond);

// 0x4B9420. Least Control At Location.
int cond_least_control_atloc(TrigCondition *pCond);

// 0x4B94A0. Least Kills.
int cond_least_kills(TrigCondition *pCond);

// 0x4B9510. Lowest Score.
int cond_lowest_score(TrigCondition *pCond);

// 0x4B9580. Least Resources.
int cond_least_resource(TrigCondition *pCond);

// 0x4BA060, defined in TrigEnum.cpp and declared by no other header; the rest
// of this TU's callees are in TrigEnum.h. It returns int because cond_opponents
// narrows the result to a byte itself.
int opponents_count(DWORD nGroup, WORD, void *);

#endif  // OC117_TRIGCOND_H
