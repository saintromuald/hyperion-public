// TrigCond.cpp -- the trigger condition handlers (oracle 0x4B8E80..0x4B95E0),
// in oracle VA order.
//
// Every handler ends in one shared switch over pCond->bComparison:
//     case 0:  return nCount >= nQual;    // at least
//     case 1:  return nQual  >= nCount;   // at most
//     case 10: return nCount == nQual;    // exactly
//     default: return 0;
// Both operands are DWORD: each exit lowers to `cmp` + `sbb eax,eax` +
// `inc eax`, a carry test, where a signed compare emits `setge`. The
// bodies come in three shapes: A is the bare tail with no call (cond_timer), B
// one accessor and the tail (cond_score), C the nation walk, maximising or
// minimising (cond_highest_score).
#include "oc_data.h"  // Hyperion: OC_VA
#include "../port/gameplay/port_eud.h"  // Hyperion: EUD maps
#include "../h/TrigEnum.h"        // the accessor family, the nation walk,
                                  // TriggerPlayer and TrigEnum_PrepCounts
#include "../h/victory.h"         // TrigCondition + TRIGCONDPROC
#include "../h/TrigCond.h"        // this TU's own interface
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled, one `#ifndef` per name.
//
// 0x5349D4 is unnamed in the registry, so the spelling below is TU-local. What
// is derived: it is a DWORD (`cmp dword ptr [5349D4h], edx` at all three of
// cond_timer's exits) and it is the countdown timer rather than another
// counter, because cond_timer is the "Countdown Timer" condition and its twin
// cond_elapsed -- same 55 bytes, same three exits -- reads 0x5349D8, which the
// registry names g_dwElapsedTime.
#ifdef OC_DLL_MODE
#define g_dwCountdownTimer (*reinterpret_cast<DWORD *>(OC_VA(0x5349D4u)))
#else
extern DWORD g_dwCountdownTimer;
#endif

// 0x534F20 is `dword_534F20` in the registry, DWORD[8]: the 256 trigger
// switches as a bitfield. cond_switch reads it with the `[edi*4+534F20h]` shape
// that fixes the element width. It is spelled locally until the row lands.
#ifdef OC_DLL_MODE
#define gSwitches_g (reinterpret_cast<DWORD *>(OC_VA(0x534F20u)))
#else
extern DWORD gSwitches_g[8];
#endif

// Answers false. /OPT:ICF folded every always-false predicate onto these three
// bytes; the spelling is the one declaration's, in VictDlg.cpp.
// Hyperion: the
// payload has it too, so its callers no longer go to the original's
//.
int __fastcall nullsub___shared_()
{
  return 0;
}

// "Countdown Timer". Shape A.
//
// No call at all: one load, the shared three-way dispatch, three exits. The two operands
// appear in one order at case 0 and the other at case 1; writing both arms the same way
// round and negating one gives different code.
int cond_timer(TrigCondition *pCond)
{
  DWORD nQual = pCond->dwQualifier;

  switch (pCond->bComparison) {
  case 0:  return g_dwCountdownTimer >= nQual;
  case 1:  return nQual >= g_dwCountdownTimer;
  case 10: return g_dwCountdownTimer == nQual;
  }
  return 0;
}
// "Command".
//
// Shape B with two things in front: a bare TrigEnum_PrepCounts(), and units_count's third
// argument computed rather than literal, (pCond->bComparison != 1). The comparison byte is
// read twice, here and for the tail's dispatch, so it is not hoisted into a local.
int cond_control(TrigCondition *pCond)
{
  TrigEnum_PrepCounts();

  DWORD nCount = (DWORD)units_count(
      pCond->dwGroup, pCond->wUnitType, (void *)(DWORD_PTR)(pCond->bComparison != 1));
  DWORD nQual = pCond->dwQualifier;

  switch (pCond->bComparison) {
  case 0:  return nCount >= nQual;
  case 1:  return nQual >= nCount;
  case 10: return nCount == nQual;
  }
  return 0;
}
// "Bring".
//
// cond_control behind the location guard, which replaces the TrigEnum_PrepCounts() call:
// the only call before units_count is owned_count_atloc. The region id is decremented in
// the byte domain, so it is held in a BYTE local.
int cond_bring(TrigCondition *pCond)
{
  BYTE bRegion = pCond->bRegion;
  if (!bRegion)
    return 0;

  owned_count_atloc((BYTE)(bRegion - 1));

  DWORD nCount = (DWORD)units_count(
      pCond->dwGroup, pCond->wUnitType, (void *)(DWORD_PTR)(pCond->bComparison != 1));
  DWORD nQual = pCond->dwQualifier;

  switch (pCond->bComparison) {
  case 0:  return nCount >= nQual;
  case 1:  return nQual >= nCount;
  case 10: return nCount == nQual;
  }
  return 0;
}
// "Accumulate". Shape B.
//
// cond_score with resource_count in its place: the same literal 0 third argument, bResType
// (a BYTE widened into the accessor's 32-bit slot) and dwGroup, then the shared three-way
// dispatch.
int cond_accumulate(TrigCondition *pCond)
{
  DWORD nCount = (DWORD)resource_count(pCond->dwGroup, pCond->bResType, 0);
  DWORD nQual = pCond->dwQualifier;

  switch (pCond->bComparison) {
  case 0:  return nCount >= nQual;
  case 1:  return nQual >= nCount;
  case 10: return nCount == nQual;
  }
  return 0;
}
// "Kill". Shape B: one accessor, then the shared
// three-way dispatch. cond_score except that the accessor is kill_count and the second
// argument is the WORD wUnitType at +0x0C rather than the BYTE bResType at +0x10.
int cond_kill(TrigCondition *pCond)
{
  DWORD nCount = (DWORD)kill_count(pCond->dwGroup, pCond->wUnitType, 0);
  DWORD nQual = pCond->dwQualifier;

  switch (pCond->bComparison) {
  case 0:  return nCount >= nQual;
  case 1:  return nQual >= nCount;
  case 10: return nCount == nQual;
  }
  return 0;
}
// "Score". Shape B.
//
// score_count's third argument is 0 here and (void *)1 in the nation-walk bodies, a real
// argument pushed as a literal. The second argument is pCond->bResType, a BYTE into the
// accessor's 32-bit slot, which is what separates this body from cond_kill.
int cond_score(TrigCondition *pCond)
{
  DWORD nCount = (DWORD)score_count(pCond->dwGroup, pCond->bResType, 0);
  DWORD nQual = pCond->dwQualifier;

  switch (pCond->bComparison) {
  case 0:  return nCount >= nQual;
  case 1:  return nQual >= nCount;
  case 10: return nCount == nQual;
  }
  return 0;
}
// "Most Control".
//
// Shape C, the nation walk, over units_count, with TrigEnum_PrepCounts() before the walk
// and the completed-only flag hard-set to (void *)1. Both `if`s are branchless and the
// greater test unsigned, so both accumulators are DWORD. nBest and nMine are declared below
// TrigEnum_PrepCounts(): above it, their zeroing moves in front of the call.
int cond_most_control(TrigCondition *pCond)
{
  int nCursor;

  TrigEnum_PrepCounts();

  DWORD nBest = 0;
  DWORD nMine = 0;
  DWORD nNation = nation_find_first(&nCursor);
  while (nNation < (DWORD)PlayerLobbySlots()) {
    DWORD nCount = (DWORD)units_count(TrigSlotTag(nNation), pCond->wUnitType, (void *)1);
    if (nNation == (DWORD)TriggerPlayer())
      nMine = nCount;
    if (nCount > nBest)
      nBest = nCount;
    nNation = nation_find_next(&nCursor);
  }
  return nMine >= nBest;
}
// "Most Control At Location".
//
// cond_most_control behind cond_bring's location guard, with
// owned_count_atloc(bRegion - 1) in place of TrigEnum_PrepCounts(). The accumulators are
// declared below the guard, so its early exit comes before EBX and EDI are saved.
int cond_most_control_atloc(TrigCondition *pCond)
{
  int nCursor;

  BYTE bRegion = pCond->bRegion;
  if (!bRegion)
    return 0;

  owned_count_atloc((BYTE)(bRegion - 1));

  DWORD nBest = 0;
  DWORD nMine = 0;
  DWORD nNation = nation_find_first(&nCursor);
  while (nNation < (DWORD)PlayerLobbySlots()) {
    DWORD nCount = (DWORD)units_count(TrigSlotTag(nNation), pCond->wUnitType, (void *)1);
    if (nNation == (DWORD)TriggerPlayer())
      nMine = nCount;
    if (nCount > nBest)
      nBest = nCount;
    nNation = nation_find_next(&nCursor);
  }
  return nMine >= nBest;
}
// "Most Kills". Shape C: the nation walk, maximising.
//
// The third argument is 0 here where the score walk pushes 1. Both accumulators seed to 0
// and both `if`s are branchless; the greater test is unsigned, so the accumulators are
// DWORD.
int cond_most_kills(TrigCondition *pCond)
{
  int nCursor;
  DWORD nBest = 0;
  DWORD nMine = 0;

  DWORD nNation = nation_find_first(&nCursor);
  while (nNation < (DWORD)PlayerLobbySlots()) {
    DWORD nCount = (DWORD)kill_count(TrigSlotTag(nNation), pCond->wUnitType, 0);
    if (nNation == (DWORD)TriggerPlayer())
      nMine = nCount;
    if (nCount > nBest)
      nBest = nCount;
    nNation = nation_find_next(&nCursor);
  }
  return nMine >= nBest;
}
// "Highest Score". Shape C.
//
// The nation walk. Three stack slots: the cursor (its address is taken at both
// nation_find_* calls), the current nation, and pCond, reloaded every iteration across the
// opaque TriggerPlayer(). Both accumulators are plain DWORD locals, both `if`s branchless,
// and the return is `nMine >= nBest`, not `==`.
int cond_highest_score(TrigCondition *pCond)
{
  int nCursor;
  DWORD nBest = 0;
  DWORD nMine = 0;

  DWORD nNation = nation_find_first(&nCursor);
  while (nNation < (DWORD)PlayerLobbySlots()) {
    DWORD nScore = (DWORD)score_count(TrigSlotTag(nNation), pCond->bResType, (void *)1);
    if (nNation == (DWORD)TriggerPlayer())
      nMine = nScore;
    if (nScore > nBest)
      nBest = nScore;
    nNation = nation_find_next(&nCursor);
  }
  return nMine >= nBest;
}
// "Most Resources". Shape C.
//
// The nation walk, maximising: cond_highest_score except for the accessor and its third
// argument, 0 where cond_highest_score passes 1. The accumulators are DWORD.
int cond_most_resource(TrigCondition *pCond)
{
  int nCursor;
  DWORD nBest = 0;
  DWORD nMine = 0;

  DWORD nNation = nation_find_first(&nCursor);
  while (nNation < (DWORD)PlayerLobbySlots()) {
    DWORD nCount = (DWORD)resource_count(TrigSlotTag(nNation), pCond->bResType, 0);
    if (nNation == (DWORD)TriggerPlayer())
      nMine = nCount;
    if (nCount > nBest)
      nBest = nCount;
    nNation = nation_find_next(&nCursor);
  }
  return nMine >= nBest;
}
// oracle 0x4B9280 (88 B). "Switch".
//
// Two signednesses over one value. The oracle loads pCond->bResType once
// (`movzx esi, byte ptr [ecx+10h]`) and derives both halves of the bit address
// from that one register: `shr edi, 5h`, a logical shift, and MSVC's signed
// `% 32` chain (`and esi, -7FFFFFE1h / jns / dec / or / inc`). One expression
// cannot emit both, so the byte is held in a signed int for the `%` and cast to
// DWORD for the shift.
//
// The comparison enum is a different one from every other body here:
// `sub eax,2h / jz` then `dec eax / jz`, cases 2 and 3 rather than 0, 1 and 10.
// The two arms are asymmetric on purpose: case 2 ends `and eax, edx` with no
// normalisation and returns the masked value, while case 3 ends
// `and eax, edx / neg eax / sbb eax,eax / inc eax`, MSVC's `== 0`, and returns
// a bool.
int cond_switch(TrigCondition *pCond)
{
  int nType = pCond->bResType;
  DWORD nIndex = (DWORD)nType >> 5;
  int nBit = nType % 32;

  switch (pCond->bComparison) {
  case 2: return gSwitches_g[nIndex] & (1 << nBit);
  case 3: return (gSwitches_g[nIndex] & (1 << nBit)) == 0;
  }
  return 0;
}
// oracle 0x4B92E0 (55 B). "Elapsed Time". Shape A -- cond_timer's twin, reading
// g_dwElapsedTime (0x5349D8) where cond_timer reads the countdown at 0x5349D4.
// Same 55 bytes, same three exits, same operand order per arm.
int cond_elapsed(TrigCondition *pCond)
{
  DWORD nQual = pCond->dwQualifier;

  switch (pCond->bComparison) {
  case 0:  return g_dwElapsedTime >= nQual;
  case 1:  return nQual >= g_dwElapsedTime;
  case 10: return g_dwElapsedTime == nQual;
  }
  return 0;
}
// oracle 0x4B9320 (62 B). "Opponents". Shape B, leaf variant.
//
// The `movzx edx, al` after the call is ours, not the compiler's:
// opponents_count is declared to return `int`, so the narrowing is written out,
// and into a DWORD-wide local -- a BYTE one leaves the value in AL and MSVC
// re-widens it in each of the three arms.
//
// The three locals are a register-allocation lever: MSVC hands the two values
// ECX and EDX in the order their temporaries are created, so naming the call's
// raw result first (`int nRaw = ...; DWORD nQual = ...; DWORD nCount =
// (BYTE)nRaw;`) creates nQual's temporary first and leaves the count in EDX, as
// the oracle has it. Declaring nQual first without the raw local costs eight
// bytes: the qualifier load is then live across the call.
int cond_opponents(TrigCondition *pCond)
{
  int nRaw = opponents_count(pCond->dwGroup, 0, 0);
  DWORD nQual = pCond->dwQualifier;
  DWORD nCount = (BYTE)nRaw;

  switch (pCond->bComparison) {
  case 0:  return nCount >= nQual;
  case 1:  return nQual >= nCount;
  case 10: return nCount == nQual;
  }
  return 0;
}
// "Deaths". cond_kill with death_count.
int cond_deaths(TrigCondition *pCond)
{
  EudMaskCondition(pCond);  // Hyperion: an EUD map's mask
  DWORD nCount = (DWORD)death_count(pCond->dwGroup, pCond->wUnitType, 0);
  EudMaskClear();  // Hyperion
  DWORD nQual = pCond->dwQualifier;

  switch (pCond->bComparison) {
  case 0:  return nCount >= nQual;
  case 1:  return nQual >= nCount;
  case 10: return nCount == nQual;
  }
  return 0;
}
// "Least Control".
//
// cond_most_control minimising: both accumulators seed to -1, the extreme update becomes an
// unsigned less-than, and the final compare flips, so the return is nLeast >= nMine.
int cond_least_control(TrigCondition *pCond)
{
  int nCursor;

  TrigEnum_PrepCounts();

  DWORD nLeast = (DWORD)-1;
  DWORD nMine = (DWORD)-1;
  DWORD nNation = nation_find_first(&nCursor);
  while (nNation < (DWORD)PlayerLobbySlots()) {
    DWORD nCount = (DWORD)units_count(TrigSlotTag(nNation), pCond->wUnitType, (void *)1);
    if (nNation == (DWORD)TriggerPlayer())
      nMine = nCount;
    if (nCount < nLeast)
      nLeast = nCount;
    nNation = nation_find_next(&nCursor);
  }
  return nLeast >= nMine;
}
// "Least Control At Location".
//
// cond_least_control behind the location guard.
int cond_least_control_atloc(TrigCondition *pCond)
{
  int nCursor;

  BYTE bRegion = pCond->bRegion;
  if (!bRegion)
    return 0;

  owned_count_atloc((BYTE)(bRegion - 1));

  DWORD nLeast = (DWORD)-1;
  DWORD nMine = (DWORD)-1;
  DWORD nNation = nation_find_first(&nCursor);
  while (nNation < (DWORD)PlayerLobbySlots()) {
    DWORD nCount = (DWORD)units_count(TrigSlotTag(nNation), pCond->wUnitType, (void *)1);
    if (nNation == (DWORD)TriggerPlayer())
      nMine = nCount;
    if (nCount < nLeast)
      nLeast = nCount;
    nNation = nation_find_next(&nCursor);
  }
  return nLeast >= nMine;
}
// "Least Kills". cond_most_kills minimising: both
// accumulators seed to -1, the update is an unsigned less-than, so the locals stay DWORD,
// and the final compare flips, so the return is `nBest >= nMine`.
int cond_least_kills(TrigCondition *pCond)
{
  int nCursor;
  DWORD nBest = (DWORD)-1;
  DWORD nMine = (DWORD)-1;

  DWORD nNation = nation_find_first(&nCursor);
  while (nNation < (DWORD)PlayerLobbySlots()) {
    DWORD nCount = (DWORD)kill_count(TrigSlotTag(nNation), pCond->wUnitType, 0);
    if (nNation == (DWORD)TriggerPlayer())
      nMine = nCount;
    if (nCount < nBest)
      nBest = nCount;
    nNation = nation_find_next(&nCursor);
  }
  return nBest >= nMine;
}
// "Lowest Score". cond_highest_score
// minimising, with the kills pair's three differences above (the -1 seeds, the unsigned
// less-than, the flipped final compare), over score_count with the BYTE bResType at +0x10.
int cond_lowest_score(TrigCondition *pCond)
{
  int nCursor;
  DWORD nBest = (DWORD)-1;
  DWORD nMine = (DWORD)-1;

  DWORD nNation = nation_find_first(&nCursor);
  while (nNation < (DWORD)PlayerLobbySlots()) {
    DWORD nScore = (DWORD)score_count(TrigSlotTag(nNation), pCond->bResType, (void *)1);
    if (nNation == (DWORD)TriggerPlayer())
      nMine = nScore;
    if (nScore < nBest)
      nBest = nScore;
    nNation = nation_find_next(&nCursor);
  }
  return nBest >= nMine;
}
// "Least Resources". Shape C, minimising.
//
// The mirror of cond_most_resource. The seeds are (DWORD)-1, the unsigned maximum, and the
// update and the final compare flip, so the return is `nBest >= nMine` with nBest the
// running minimum.
int cond_least_resource(TrigCondition *pCond)
{
  int nCursor;
  DWORD nBest = (DWORD)-1;
  DWORD nMine = (DWORD)-1;

  DWORD nNation = nation_find_first(&nCursor);
  while (nNation < (DWORD)PlayerLobbySlots()) {
    DWORD nCount = (DWORD)resource_count(TrigSlotTag(nNation), pCond->bResType, 0);
    if (nNation == (DWORD)TriggerPlayer())
      nMine = nCount;
    if (nCount < nBest)
      nBest = nCount;
    nNation = nation_find_next(&nCursor);
  }
  return nBest >= nMine;
}
