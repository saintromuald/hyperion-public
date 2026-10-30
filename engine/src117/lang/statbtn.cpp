// statbtn.cpp -- the command card's button filters (oracle 0x49F410 to
// 0x4A0610). Each command-card slot pairs a filter with an action in a .data
// table near 0x509414 (stride 0x14): the filter returns 1 for enabled, 0 for
// hidden and -1 for shown but unavailable, and the action (order.cpp's order_*
// handlers) runs on click. This TU holds the filters, in 1.17's VA order, the
// in-progress bitset accessors beside them, and two small event handlers in the
// same block; statbtn.h has the ABI.
//
// Four spellings here show only as a size change, never a compile error, and
// each is noted at its body: noinline on bf_can_attack, the goto in
// bf_can_attack_unburrowed, bf_trans's three subscripts, and the cargo
// filters' two bit tests.
#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include <stddef.h>            // offsetof -- CUnit.h's static_asserts need it

typedef unsigned char  BYTE;
typedef unsigned short WORD;
#include "platform.h"  // DWORD
typedef int            BOOL;

#include "statbtn.h"
#include "target.h"            // target_off
#include "../h/typelimits.h"  // Hyperion: a mod's types
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX

// The globals this TU reaches, dual-spelled.

// gTechInProgress -- BYTE[72] @0x536510, the tech-in-progress bitset: a 6-byte
// row per player (48 bits for 44 techs) and 12 rows, as maphdr_data_init's
// memset of 0x48 bytes confirms. The name is ours; the naming sheet has none.
#ifdef OC_DLL_MODE
#define gTechInProgress (reinterpret_cast<BYTE*>(OC_VA(0x536510u)))
#else
extern PlayerTechInProgressTable gTechInProgress;
#endif

// gUpgradeInProgress -- BYTE[96] @0x5366C0, the upgrade-in-progress bitset: an
// 8-byte row per player (64 bits for 61 upgrades) and 12 rows, as its memset of
// 0x60 bytes confirms. The name is ours too.
#ifdef OC_DLL_MODE
#define gUpgradeInProgress (reinterpret_cast<BYTE*>(OC_VA(0x5366C0u)))
#else
extern PlayerUpgradeInProgressTable gUpgradeInProgress;
#endif

// byte_6AB4C0 -- BYTE @0x6AB4C0 (statres.cpp), the sheet's name. It is only
// read as a byte and compared with 1 (bf_building_ok's test is an unsigned
// > 1); the slot up to gpStatUnit at 0x6AB4C4 is the BYTE and slack.
#ifdef OC_DLL_MODE
#define byte_6AB4C0 (*reinterpret_cast<BYTE*>(OC_VA(0x6AB4C0u)))
#else
extern BYTE byte_6AB4C0;
#endif

// gpStatUnits -- CUnit *[12] @0x6AB4C8, the selected units the command card
// reflects; every scan's bound is 0x6AB4F8, 12 pointers on.
#ifdef OC_DLL_MODE
#define gpStatUnits (reinterpret_cast<CUnit **>(OC_VA(0x6AB4C8u)))
#else
extern CUnit *gpStatUnits[SELECTION_MAX];
#endif

// gpStatUnit -- CUnit * @0x6AB4C4, the primary selected unit, read only by
// bf_can_move_stop_unburrowed.
#ifdef OC_DLL_MODE
#define gpStatUnit (*reinterpret_cast<CUnit **>(OC_VA(0x6AB4C4u)))
#else
extern CUnit *gpStatUnit;
#endif

// gulUnitTypeFlags -- DWORD[228] @0x521660, the units.dat flags column; the
// 0x100000 bit bf_can_burrow and bf_burrowed test is left unnamed.
#ifdef OC_DLL_MODE
#define gulUnitTypeFlags (reinterpret_cast<DWORD*>(OC_VA(0x521660u)))
#else
extern DWORD gulUnitTypeFlags[228] OC_UNALIGNED;
#endif

// The gpStatUnits scan the filters share: all 12 slots, null slots skipped.
// The bound test is a signed `jl`, the mark of an int induction variable MSVC
// turned into a pointer; a pointer walk gives `jb`, so each scan is an indexed
// `for (int i = 0; i < 12; ++i)`. The cargo test is two bit tests on the
// bCarryResource byte (0 none, 1 gas, 2 minerals), never one `& 3` mask, which
// fuses the load into the test and costs 2 bytes.

// A constant return.
int __fastcall bf_always(BYTE, int, CUnit *)
{
  return 1;
}

// A guard, a three-way unit-type reject, then a
// pass-through call to TTAllowed. The first parameter is a WORD, as bf_empty_silo's:
// it is forwarded unchanged to TTAllowed, whose first parameter is a WORD.
// Hyperion: in a game of Hyperion's selection several buildings train together, the
// receiver picking one (net_cmds.cpp).
int __fastcall bf_building_ok(WORD wUnitType, int nPlayer, CUnit *pUnit)
{
  if (byte_6AB4C0 > 1 && pUnit->wType != 0x23 && pUnit->wType != 0x2B &&
      pUnit->wType != 0x26 && !SelectionWide())
    return 0;
  return TTAllowed(wUnitType, nPlayer, pUnit);
}

// The "all selected units are the same type"
// scan in front of a memoised TTAllowed call: TTAllowed runs at most once, its
// result the return value, and a type mismatch returns 0. The counter is unsigned.
int __fastcall bf_build_fighter_ok(int nUnitType, int nPlayer, CUnit *pUnit)
{
  WORD wType = pUnit->wType;
  int nResult = 0;
  for (unsigned i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pStatUnit = StatUnits()[i];
    if (pStatUnit)
    {
      if (pStatUnit->wType != wType)
        return 0;
      if (!nResult)
        nResult = TTAllowed(nUnitType, nPlayer, pStatUnit);
    }
  }
  return nResult;
}

// The scan, with a two-step test on the
// one-byte right-click action: an action of 0 continues the scan, and any
// action but 3 returns 1. 3 is the "cannot move" action (CUnit::CanMove).
int __fastcall bf_can_move_patrol(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit)
    {
      BYTE bAction = pUnit->GetRClickAction();
      if (bAction && bAction != 3)
        return 1;
    }
  }
  return 0;
}

// The bare scan: any selected unit with a
// nonzero right-click action enables the slot.
int __fastcall bf_can_move_stop(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && pUnit->GetRClickAction())
      return 1;
  }
  return 0;
}

// The scan with two guarded calls on the same
// unit, one `&&` chain over a local (bf_trans below has the other shape). noinline
// because 1.17 calls this from bf_can_attack_unburrowed rather than inlining it;
// statbtn.h repeats the attribute.
__declspec(noinline) int __fastcall bf_can_attack(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && pUnit->GetRClickAction() && pUnit->CanAttack())
      return 1;
  }
  return 0;
}

// The 12-slot scan with the "unit is not
// disabled" guard chain in front of a member call, as CUnit::DispatchTowerTarget
// has it.
int __fastcall bf_tower(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit &&
        !(pUnit->dwFlags & 0x400) &&
        !pUnit->bLockdownTimer &&
        !pUnit->bStasisTimer &&
        !pUnit->bParalyzeTimer &&
        pUnit->CanAttack())
      return 1;
  }
  return 0;
}

// True when the build queue's head is a unit, not a building: MSVC's unsigned
// `x <= 0x69` as a 16-bit compare, since GetBuildUnit returns a WORD. 0x69 is
// 105; units.dat 106, the Terran Command Center, is the first building.
int __fastcall bf_stop_unit(BYTE, int, CUnit *pUnit)
{
  return UnitTypeQueueable(pUnit->GetBuildUnit(0));  // Hyperion: <= 0x69
}

// A straight delegate to a __thiscall member,
// whose result is the filter's.
int __fastcall bf_stop_addon(BYTE, int, CUnit *pUnit)
{
  return pUnit->IsConstructingAddon();
}

// A 16-bit compare chain through a WORD local.
// 0x83, 0x84 and 0x85 are the Hatchery, Lair and Hive.
int __fastcall bf_larva_sel(BYTE, int, CUnit *pUnit)
{
  WORD wType = pUnit->wType;
  if (wType == 0x83 || wType == 0x84 || wType == 0x85)
    return 1;
  return 0;
}

// A tail-jump delegate with a constant first
// argument; the player and the unit pass through.
int __fastcall bf_rally(BYTE, int nPlayer, CUnit *pUnit)
{
  return OrderAllowed(0x28, nPlayer, pUnit);
}

// bf_larva_sel's 16-bit chain, then a tail
// jump to OrderAllowed, whose third parameter is on the stack (statbtn.h).
int __fastcall bf_hatch_rally(BYTE, int nPlayer, CUnit *pUnit)
{
  WORD wType = pUnit->wType;
  if (wType != 0x83 && wType != 0x84 && wType != 0x85)
    return 0;
  return OrderAllowed(0x28, nPlayer, pUnit);
}

// The complement of the low bit, not a negated
// predicate.
int __fastcall bf_not_comp(BYTE, int, CUnit *pUnit)
{
  return ~pUnit->dwFlags & 1;
}

// 0x3D = 61 = the upgrades.dat entry count,
// the "no upgrade" sentinel (CUnit.h, bUpgradeType).
int __fastcall bf_stop_upgrade(BYTE, int, CUnit *pUnit)
{
  return pUnit->bUpgradeType != 61;
}

// 0x2C = 44 = the techdata.dat entry count.
int __fastcall bf_stop_research(BYTE, int, CUnit *pUnit)
{
  return pUnit->bTechType != 44;
}

// "no load": true as soon as something
// selected is carrying nothing. The two bit tests are the scan note's.
int __fastcall bf_no_load(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit)
    {
      BYTE bCarry = pUnit->bCarryResource;
      if (!(bCarry & 1) && !(bCarry & 2))
        return 1;
    }
  }
  return 0;
}

// bf_no_load with the test inverted.
int __fastcall bf_load(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit)
    {
      BYTE bCarry = pUnit->bCarryResource;
      if ((bCarry & 1) || (bCarry & 2))
        return 1;
    }
  }
  return 0;
}

// Not a uniform scan: a unit that is not a
// transport ends the whole scan with 0; only a null slot continues. So the answer
// is "every selected unit is a transport and one has room". The slot is
// re-subscripted rather than held in a local, as in 1.17.
int __fastcall bf_trans(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    if (StatUnits()[i])
    {
      if (!StatUnits()[i]->IsATransport())
        return 0;
      if (StatUnits()[i]->HasEmptyCargoSpace())
        return 1;
    }
  }
  return 0;
}

// bf_can_move_stop's shape with a full-width
// result: IsUnitInside returns 0 or 1 in the whole of EAX (CUnit.h).
int __fastcall bf_trans_has_passenger(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && pUnit->IsUnitInside())
      return 1;
  }
  return 0;
}

// The first parameter is a BYTE, not the WORD of
// the button table's action slot, widened for the callee's int.
int __fastcall bf_research(tech_id_t bResearch, int nPlayer, CUnit *pUnit)
{
  return ResearchAllowed(bResearch, nPlayer, pUnit);
}

int __fastcall bf_has_spell(tech_id_t bTech, int nPlayer, CUnit *pUnit)
{
  return TechUseAllowed(bTech, nPlayer, pUnit);
}

// The scan that keeps the worst answer: a
// running minimum of TechUseAllowed's three-state result, seeded at 0. 1 as soon
// as any selected unit may use the tech, else -1 if any answered -1, else 0. The
// compare is signed. The third parameter is unused, each call passing the slot's
// unit.
int __fastcall bf_has_mines(BYTE bTech, int nPlayer, CUnit *)
{
  int nResult = 0;
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit)
    {
      int nAllowed = TechUseAllowed(bTech, nPlayer, pUnit);
      if (nAllowed == 1)
        return 1;
      if (nAllowed < nResult)
        nResult = nAllowed;
    }
  }
  return nResult;
}

int __fastcall bf_upgrade(upgrade_id_t bUpgrade, int nPlayer, CUnit *pUnit)
{
  return UpgradeAllowed(bUpgrade, nPlayer, pUnit);
}

// The two cloak predicates the oracle inlined into the four bodies below. Each
// must be a helper that reads dwFlags at every use: the inline reserves the
// unused [ebp-4] frame slot and keeps the returned bit as `and eax, 1h`.
static __forceinline int unit_can_cloak(CUnit *pUnit)
{
  if ((pUnit->dwFlags & 0x800) && !(pUnit->dwFlags & 0x10))
    return 0;
  return ~(pUnit->dwFlags >> 8) & 1;
}

static __forceinline int unit_is_cloaked(CUnit *pUnit)
{
  if ((pUnit->dwFlags & 0x800) && !(pUnit->dwFlags & 0x10))
    return 0;
  return (pUnit->dwFlags >> 8) & 1;
}

// Whether any selected unit may cloak now.
// The third parameter is unused and the slot is re-subscripted after the call.
int __fastcall bf_can_cloak(BYTE bTech, int nPlayer, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit)
    {
      int nAllowed = TechUseAllowed(bTech, nPlayer, pUnit);
      if (nAllowed != 1)
        return nAllowed;

      if (unit_can_cloak(StatUnits()[i]))
        return 1;
    }
  }
  return 0;
}

// Whether every selected unit is cloaked.
int __fastcall bf_is_cloaked(BYTE bTech, int nPlayer, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit)
    {
      if (TechUseAllowed(bTech, nPlayer, pUnit) != 1)
        return 0;

      if (!unit_is_cloaked(StatUnits()[i]))
        return 0;
    }
  }
  return 1;
}

// bf_can_cloak for heroes, whose tech comes
// from the unit; a TechUseAllowed answer other than 1 continues the scan.
int __fastcall bf_can_cloak_hero(BYTE, int nPlayer, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && TechUseAllowed(pUnit->GetCloakingTech(), nPlayer, pUnit) == 1)
    {
      if (unit_can_cloak(StatUnits()[i]))
        return 1;
    }
  }
  return 0;
}

// bf_is_cloaked for heroes.
int __fastcall bf_is_hero_cloaked(BYTE, int nPlayer, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit)
    {
      if (TechUseAllowed(pUnit->GetCloakingTech(), nPlayer, pUnit) != 1)
        return 0;

      if (!unit_is_cloaked(StatUnits()[i]))
        return 0;
    }
  }
  return 1;
}

// Gate on TechUseAllowed, then scan the 12
// selected units.
// A gate failure returns TechUseAllowed's answer unchanged, where
// bf_is_sieged's returns 0.
int __fastcall bf_can_siege(BYTE bTech, int nPlayer, CUnit *pUnit)
{
  int nAllowed = TechUseAllowed(bTech, nPlayer, pUnit);
  if (nAllowed != 1)
    return nAllowed;

  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit2 = StatUnits()[i];
    if (pUnit2 && (pUnit2->wType == 0x1E || pUnit2->wType == 0x19))
      return 0;
  }
  return 1;
}

// The mirror of bf_can_siege: the same prologue
// and scan, but a gate failure returns 0.
int __fastcall bf_is_sieged(BYTE bTech, int nPlayer, CUnit *pUnit)
{
  if (TechUseAllowed(bTech, nPlayer, pUnit) != 1)
    return 0;

  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit2 = StatUnits()[i];
    if (pUnit2 && (pUnit2->wType == 0x1E || pUnit2->wType == 0x19))
      return 1;
  }
  return 0;
}

// One `||` whose true arm is `return 0`. 0x1E
// and 0x19, like 0x53 and 0x51 in bf_has_scarab and 0x48 and 0x52 in
// bf_has_interceptor, are read off the immediates and left unnamed.
int __fastcall bf_can_tank_move(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && (pUnit->wType == 0x1E || pUnit->wType == 0x19))
      return 0;
  }
  return 1;
}

// `neg / sbb / and 2 / dec` maps FindArmedSilo's result onto 1 or -1, a
// ternary between the enabled and shown-but-unavailable states, never 0.
int __fastcall bf_has_nuke(BYTE, int, CUnit *pUnit)
{
  return pUnit->FindArmedSilo(0, 0) ? 1 : -1;
}

// No scan: this one reads the stack parameter,
// one flag bit and three status timers. `return 1` on the fall-through and
// `return 0` last is 1.17's layout.
int __fastcall bf_battery(BYTE, int, CUnit *pUnit)
{
  if (!(pUnit->dwFlags & 0x400)
      && !pUnit->bLockdownTimer
      && !pUnit->bStasisTimer
      && !pUnit->bParalyzeTimer)
    return 1;
  return 0;
}

// dwFlags is read unsigned and shifted before
// the complement.
int __fastcall bf_is_ship(BYTE, int, CUnit *pUnit)
{
  return ~(pUnit->dwFlags >> 1) & 1;
}

// A global guard and a flag test on the stack
// parameter, one `&&` chain with `return 1` on the fall-through, as in 1.17.
int __fastcall bf_lifted(BYTE, int, CUnit *pUnit)
{
  if (byte_6AB4C0 == 1 && !(pUnit->dwFlags & CUnit::UF_Building))
    return 1;
  return 0;
}

// No scan: a six-term guard chain on the one
// unit the third parameter names. 0xE4 (228, the units.dat count) is an empty
// build queue, compared as CUnit::GetBuildUnit's WORD; 0x2C and 0x3D are the
// no-tech and no-upgrade sentinels of bf_stop_research and bf_stop_upgrade.
int __fastcall bf_liftoff_ok(BYTE, int, CUnit *pUnit)
{
  if ((pUnit->dwFlags & CUnit::UF_Building) &&
      pUnit->GetBuildUnit(0) == 0xE4 &&
      !pUnit->IsConstructingAddon() &&
      !pUnit->IsBuildingProtoss() &&
      pUnit->bTechType == 0x2C &&
      pUnit->bUpgradeType == 0x3D)
    return 1;
  return 0;
}

// The positive half of the burrow pair, and
// its gate failure passes TechUseAllowed's answer through, as bf_can_siege's
// does; bf_burrowed's returns 0. The units.dat "can burrow" bit (0x100000) is
// tested first.
int __fastcall bf_can_burrow(BYTE bTech, int nPlayer, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && (gulUnitTypeFlags[pUnit->wType] & 0x100000))
    {
      int nAllowed = TechUseAllowed(bTech, nPlayer, pUnit);
      if (nAllowed != 1)
        return nAllowed;
      if (!(pUnit->dwFlags & CUnit::UF_Burrowed) &&
          pUnit->bOrderId != 0x74 && pUnit->bOrderId != 0x76)
        return 1;
    }
  }
  return 0;
}

// The negative half, and not bf_can_burrow
// with one predicate flipped: all four failures return 0, and the answer is
// "every burrowable selected unit is burrowed, idle and permitted". 0x74 and
// 0x76 are the two transitional burrow orders, unnamed.
int __fastcall bf_burrowed(BYTE bTech, int nPlayer, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && (gulUnitTypeFlags[pUnit->wType] & 0x100000))
    {
      if (!(pUnit->dwFlags & CUnit::UF_Burrowed))
        return 0;
      if (pUnit->bOrderId == 0x74 || pUnit->bOrderId == 0x76)
        return 0;
      if (TechUseAllowed(bTech, nPlayer, pUnit) != 1)
        return 0;
    }
  }
  return 1;
}

// 0x20 = 32 = the Lurker Aspect tech id.
int __fastcall bf_make_lurker(BYTE, int nPlayer, CUnit *pUnit)
{
  return TechUseAllowed(0x20, nPlayer, pUnit);
}

// The burrow bit is a DWORD test, not a BYTE
// field (CUnit.h's dwFlags note).
int __fastcall bf_can_move_patrol_unburrowed(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && (pUnit->dwFlags & CUnit::UF_Burrowed))
      return 0;
  }
  return 1;
}

// A scan that vetoes, then a forwarded call to
// bf_can_attack: any burrowed selected unit returns 0, else the call gets the
// three parameters unchanged. The goto keeps 1.17's branch to the shared
// `return 0`.
int __fastcall bf_can_attack_unburrowed(BYTE bId, int nPlayer, CUnit *pUnit)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pStatUnit = StatUnits()[i];
    if (pStatUnit && (pStatUnit->dwFlags & CUnit::UF_Burrowed))
      goto no;
  }
  if (bf_can_attack(bId, nPlayer, pUnit))
    return 1;
no:
  return 0;
}

// Two consecutive scans, not one loop with two
// tests: the burrow scan's early exit and the cargo scan's fall-through share
// one `return 0`.
int __fastcall bf_no_load_drone(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && (pUnit->dwFlags & CUnit::UF_Burrowed))
      return 0;
  }
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit)
    {
      BYTE bCarry = pUnit->bCarryResource;
      if (!(bCarry & 1) && !(bCarry & 2))
        return 1;
    }
  }
  return 0;
}

// bf_no_load_drone with the cargo test
// inverted.
int __fastcall bf_load_drone(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && (pUnit->dwFlags & CUnit::UF_Burrowed))
      return 0;
  }
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit)
    {
      BYTE bCarry = pUnit->bCarryResource;
      if ((bCarry & 1) || (bCarry & 2))
        return 1;
    }
  }
  return 0;
}

// The scan that compares every slot against
// the primary selected unit rather than against a constant. The primary's
// burrow bit is reduced to 0 or 1 outside the loop, and a slot whose burrow
// state differs from it returns 0.
int __fastcall bf_can_move_stop_unburrowed(BYTE, int, CUnit *)
{
  int bRefBurrowed = (gpStatUnit->dwFlags & CUnit::UF_Burrowed) != 0;
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit)
    {
      if (bRefBurrowed)
      {
        if (!(pUnit->dwFlags & CUnit::UF_Burrowed))
          return 0;
      }
      else if (pUnit->dwFlags & CUnit::UF_Burrowed)
        return 0;
    }
  }
  return 1;
}

int __fastcall bf_no_exit(BYTE, int, CUnit *pUnit)
{
  return pUnit->pUnionD0 == 0;
}

// Shape A: a byte_6AB4C0 gate, a dwFlags
// veto, then a chain of TTAllowed calls, all one `&&` / `||` expression so that
// the early returns share a tail.
// Hyperion: the build menus of the workers take several in a game of Hyperion's
// selection, where the receiver gives the building to the one nearest; so the three
// races' two menus below.
int __fastcall bf_zerg_build_basic(BYTE, int nPlayer, CUnit *pUnit)
{
  if ((byte_6AB4C0 == 1 || SelectionWide()) &&
      !(pUnit->dwFlags & CUnit::UF_Burrowed) &&
      (TTAllowed(0x83, nPlayer, pUnit) ||
       TTAllowed(0x8F, nPlayer, pUnit) ||
       TTAllowed(0x95, nPlayer, pUnit) ||
       TTAllowed(0x8E, nPlayer, pUnit) ||
       TTAllowed(0x8B, nPlayer, pUnit) ||
       TTAllowed(0x87, nPlayer, pUnit)))
    return 1;
  return 0;
}

// Shape A with five ids instead of six.
// The ids are in 1.17's order, with the same gate and burrowed veto.
int __fastcall bf_zerg_build_advanced(BYTE, int nPlayer, CUnit *pUnit)
{
  if ((byte_6AB4C0 == 1 || SelectionWide()) &&
      !(pUnit->dwFlags & CUnit::UF_Burrowed) &&
      (TTAllowed(0x86, nPlayer, pUnit) ||
       TTAllowed(0x8D, nPlayer, pUnit) ||
       TTAllowed(0x8A, nPlayer, pUnit) ||
       TTAllowed(0x8C, nPlayer, pUnit) ||
       TTAllowed(0x88, nPlayer, pUnit)))
    return 1;
  return 0;
}

// A scan with no calls. The upgrade and tech
// test is one byte-width add, bUpgradeType plus bTechType, so the truncation is
// written out; real ids keep the sum well under 0x80. The no answer is -1. 0x48
// and 0x52 are the carrier types only by inference from the name.
int __fastcall bf_has_interceptor(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit
        && (pUnit->wType == 0x48 || pUnit->wType == 0x52)
        && (BYTE)(pUnit->bUpgradeType + pUnit->bTechType) != 0)
      return 1;
  }
  return -1;
}

// A reaver type (0x53 or 0x51) with a nonzero byte at +0xC8, which CUnit.h
// names bTechType, the building's reading of a union; for a reaver it is the
// scarab count. Otherwise -1, the family's third state. Hyperion: read as the
// count, bHangarCount, since bTechType is 16 bits on x64.
int __fastcall bf_has_scarab(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && (pUnit->wType == 0x53 || pUnit->wType == 0x51) &&
        pUnit->bHangarCount != 0)
      return 1;
  }
  return -1;
}

// 0 when TechUseAllowed returns anything but -1, and -1 when it does: a
// ternary whose true arm is 0. Spelled `== -1 ? -1 : 0` it is the same length
// and the wrong bytes.
int __fastcall bf_can_make_archon_need2(BYTE bTech, int nPlayer, CUnit *pUnit)
{
  return TechUseAllowed(bTech, nPlayer, pUnit) != -1 ? 0 : -1;
}

// The third gate shape: on failure `return 0`,
// on success the guard mapped to -1 or 1, `2 * (byte_6AB4C0 != 1) - 1`. 0x17 is
// Archon Warp.
int __fastcall bf_can_make_archon(BYTE, int nPlayer, CUnit *pUnit)
{
  if (TechUseAllowed(0x17, nPlayer, pUnit) != 1)
    return 0;
  return 2 * (byte_6AB4C0 != 1) - 1;
}

// Identical to bf_can_make_archon but for tech
// 0x1C (techdata.dat 28 = Dark Archon Meld). The two bodies differ in one
// constant.
int __fastcall bf_can_make_darchon(BYTE, int nPlayer, CUnit *pUnit)
{
  if (TechUseAllowed(0x1C, nPlayer, pUnit) != 1)
    return 0;
  return 2 * (byte_6AB4C0 != 1) - 1;
}

// Shape A with no burrowed veto and eight
// ids, as 1.17 has it.
int __fastcall bf_protoss_build_basic(BYTE, int nPlayer, CUnit *pUnit)
{
  if ((byte_6AB4C0 == 1 || SelectionWide()) &&
      (TTAllowed(0x9A, nPlayer, pUnit) ||
       TTAllowed(0x9C, nPlayer, pUnit) ||
       TTAllowed(0x9D, nPlayer, pUnit) ||
       TTAllowed(0xA0, nPlayer, pUnit) ||
       TTAllowed(0xA6, nPlayer, pUnit) ||
       TTAllowed(0xA2, nPlayer, pUnit) ||
       TTAllowed(0xA4, nPlayer, pUnit) ||
       TTAllowed(0xAC, nPlayer, pUnit)))
    return 1;
  return 0;
}

// Protoss basic's twin, eight other ids.
int __fastcall bf_protoss_build_advanced(BYTE, int nPlayer, CUnit *pUnit)
{
  if ((byte_6AB4C0 == 1 || SelectionWide()) &&
      (TTAllowed(0x9B, nPlayer, pUnit) ||
       TTAllowed(0xA7, nPlayer, pUnit) ||
       TTAllowed(0xA3, nPlayer, pUnit) ||
       TTAllowed(0xAB, nPlayer, pUnit) ||
       TTAllowed(0xA9, nPlayer, pUnit) ||
       TTAllowed(0xA5, nPlayer, pUnit) ||
       TTAllowed(0x9F, nPlayer, pUnit) ||
       TTAllowed(0xAA, nPlayer, pUnit)))
    return 1;
  return 0;
}

// Order 0x21 == 33. True only when every
// selected unit is on that order.
int __fastcall bf_stop_build(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && pUnit->bOrderId != 0x21)
      return 0;
  }
  return 1;
}

// Same test as bf_stop_build with the two
// results swapped.
int __fastcall bf_scv_attack_orders(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && pUnit->bOrderId != 0x21)
      return 1;
  }
  return 0;
}

int __fastcall bf_no_load_scv(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *p = StatUnits()[i];
    if (p)
    {
      BYTE b = p->bCarryResource;
      if (!(b & 1) && !(b & 2))
        goto second;
    }
  }
  return 0;

second:
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *p = StatUnits()[i];
    if (p && p->bOrderId != 0x21)
      return 1;
  }
  return 0;
}

// The mirror of bf_no_load_scv: the first
// scan's cargo test alone is inverted; the second scan is the same in both.
int __fastcall bf_load_scv(BYTE, int, CUnit *)
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *p = StatUnits()[i];
    if (p)
    {
      BYTE b = p->bCarryResource;
      if ((b & 1) || (b & 2))
        goto second;
    }
  }
  return 0;

second:
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *p = StatUnits()[i];
    if (p && p->bOrderId != 0x21)
      return 1;
  }
  return 0;
}

// The order-0x21 pre-scan of the terran pair, inlined into both in 1.17, so a
// static helper here; bf_scv_attack_orders does the same test but would be a
// real call.
static int any_selected_not_order21()
{
  for (int i = 0; i < SelectionMax(); ++i)
  {
    CUnit *p = StatUnits()[i];
    if (p && p->bOrderId != 0x21)
      return 1;
  }
  return 0;
}

// Shape A plus the pre-scan. The predicate
// is a three-term `&&`: byte_6AB4C0 != 1 returns 0, and the gate, the scan's end
// and the chain's last test share one `return 0`.
int __fastcall bf_terran_build_basic(BYTE, int nPlayer, CUnit *pUnit)
{
  if ((byte_6AB4C0 == 1 || SelectionWide()) &&
      any_selected_not_order21() &&
      (TTAllowed(0x6A, nPlayer, pUnit) ||
       TTAllowed(0x6D, nPlayer, pUnit) ||
       TTAllowed(0x6E, nPlayer, pUnit) ||
       TTAllowed(0x6F, nPlayer, pUnit) ||
       TTAllowed(0x7A, nPlayer, pUnit) ||
       TTAllowed(0x7C, nPlayer, pUnit) ||
       TTAllowed(0x70, nPlayer, pUnit) ||
       TTAllowed(0x7D, nPlayer, pUnit)))
    return 1;
  return 0;
}

// Same three-term predicate and same
// pre-scan, four ids.
int __fastcall bf_terran_build_advanced(BYTE, int nPlayer, CUnit *pUnit)
{
  if ((byte_6AB4C0 == 1 || SelectionWide()) &&
      any_selected_not_order21() &&
      (TTAllowed(0x71, nPlayer, pUnit) ||
       TTAllowed(0x72, nPlayer, pUnit) ||
       TTAllowed(0x74, nPlayer, pUnit) ||
       TTAllowed(0x7B, nPlayer, pUnit)))
    return 1;
  return 0;
}

// The equality reaches the tail jump, so the
// early out is the inequality. statbtn.h has why the first parameter is a WORD.
int __fastcall bf_empty_silo(WORD wUnitType, int nPlayer, CUnit *pUnit)
{
  if (byte_6AB4C0 != 1 && !SelectionWide())  // Hyperion: several silos
    return 0;
  return TTAllowed(wUnitType, nPlayer, pUnit);
}

// No parameters; a conditional tail jump to
// target_off.
void __fastcall evt_target_off()
{
  if (target_is_on())
    target_off();
}

// The (WORD, BYTE) parameters are the button
// table's action-slot signature; both are ignored.
void __fastcall order_cancel_land(WORD, BYTE)
{
  if (placebox_is_on())
    sub_47D290(1, 0);
}

// One register parameter. dwFlags is loaded
// once and tested twice.
int __fastcall unit_is_cloaked_by_arbiter(CUnit *pUnit)
{
  return (pUnit->dwFlags & 0x800) && !(pUnit->dwFlags & 0x10);
}

// The in-progress bitset accessors: two register parameters, no stack
// parameter, a plain ret. The byte index is an unsigned shift of the BYTE
// parameter and the bit position MSVC's signed % 8, so the id is widened into
// a signed int first: `bId % 8` on the BYTE gives `and ecx, 7`, and a signed
// index gives `sar` for the shift. Hyperion: the port
// declares them as player tables, a row a slot, so the index is [nPlayer][byte].

void __fastcall clear_tech_in_progress(tech_id_t bTech, int nPlayer)
{
  // Hyperion: a mod's, past 1.17's rows (playertables.h)
  if (bTech >= TECH_TYPE_MOD_FIRST) {
    PlayerModTech(nPlayer, bTech, PLAYER_MOD_IN_PROGRESS) = 0;
    return;
  }
  int nId = bTech;
  gTechInProgress[nPlayer][bTech >> 3] &= ~(1 << (nId % 8));
}

// The mask is built and applied at byte width.
void __fastcall clear_upgrade_in_progress(upgrade_id_t bUpgrade, int nPlayer)
{
  // Hyperion: a mod's, past 1.17's rows (playertables.h)
  if (bUpgrade >= UPGRADE_TYPE_MOD_FIRST) {
    PlayerModUpgrade(nPlayer, bUpgrade, PLAYER_MOD_IN_PROGRESS) = 0;
    return;
  }
  int nId = bUpgrade;
  gUpgradeInProgress[nPlayer][bUpgrade >> 3] &= ~(1 << (nId % 8));
}

int __fastcall is_tech_in_progress(tech_id_t bTech, int nPlayer)
{
  // Hyperion: a mod's, past 1.17's rows (playertables.h)
  if (bTech >= TECH_TYPE_MOD_FIRST)
    return PlayerModTech(nPlayer, bTech, PLAYER_MOD_IN_PROGRESS);
  int nId = bTech;
  return gTechInProgress[nPlayer][bTech >> 3] & (1 << (nId % 8));
}

int __fastcall is_upgrade_in_progress(upgrade_id_t bUpgrade, int nPlayer)
{
  // Hyperion: a mod's, past 1.17's rows (playertables.h)
  if (bUpgrade >= UPGRADE_TYPE_MOD_FIRST)
    return PlayerModUpgrade(nPlayer, bUpgrade, PLAYER_MOD_IN_PROGRESS);
  int nId = bUpgrade;
  return gUpgradeInProgress[nPlayer][bUpgrade >> 3] & (1 << (nId % 8));
}

void __fastcall set_tech_in_progress(tech_id_t bTech, int nPlayer)
{
  // Hyperion: a mod's, past 1.17's rows (playertables.h)
  if (bTech >= TECH_TYPE_MOD_FIRST) {
    PlayerModTech(nPlayer, bTech, PLAYER_MOD_IN_PROGRESS) = 1;
    return;
  }
  int nId = bTech;
  gTechInProgress[nPlayer][bTech >> 3] |= 1 << (nId % 8);
}

void __fastcall set_upgrade_in_progress(upgrade_id_t bUpgrade, int nPlayer)
{
  // Hyperion: a mod's, past 1.17's rows (playertables.h)
  if (bUpgrade >= UPGRADE_TYPE_MOD_FIRST) {
    PlayerModUpgrade(nPlayer, bUpgrade, PLAYER_MOD_IN_PROGRESS) = 1;
    return;
  }
  int nId = bUpgrade;
  gUpgradeInProgress[nPlayer][bUpgrade >> 3] |= 1 << (nId % 8);
}
