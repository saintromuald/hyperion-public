// CUnitMisc.cpp -- the CUnit odds and ends: type predicates over wType, the small accessors
// that read a unit-type row out of arr/units.cpp, the speed modifiers, and the short order
// dispatchers that live between them.
//
// Function order is ascending oracle VA, which is this TU's definition order, and two
// consequences of that are worth knowing before anything moves:
//   * CUnit::GetTypeEx (0x428B20) and get_unit_name_from_type (0x428CE0) are both defined
//     before CUnit::GetName (0x428D10), whose oracle body is the two of them inlined end to
//     end.
//   * CUnit::DisableDoodadState (0x427E90) and CUnit::EnableDoodadState (0x428A80) are a
//     contrasting pair that VA order separates; their shared derivation is carried on the
//     first of the two and referenced from the second.

// Include order matters, and it is not alphabetical: src117/gen/globals117.h is generated
// with no includes of its own -- it names BYTE/DWORD and the struct types its rows are
// declared with, and relies on its includer having them in scope -- so it must come last.
#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playervision.h"  // Hyperion: the slots past 7
#include "../h/handicap.h"  // Hyperion: a player's handicap
#include "../h/mutators.h"  // Hyperion: a rich resource's name
#include <stdlib.h>

#include "../h/CUnitMisc.h"

#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/net_cmds.h"       // PlayerData -- gPlayerData indexes it
#include "../h/SAI_Main.h"       // ours: ai_mark_targets_stale, reseated there w91
#include "../h/CUnitTSpells.h"   // gamesnd_play
#include "../h/CFlingy.h"        // AngleBetween, TurnDelta
#include "../h/CUnitBuild.h"     // ClipToMap
#include "../h/upgrade.h"        // sub_4BBCB0, sub_4BBD40
#include "../h/select.h"         // sub_49C8B0
#include "../h/CUnitSearches.h"  // UnitCanReachGroundTarget

#include "../gen/globals117.h"

// The dual spelling, for the unit-type rows this TU reads that are not recorded in
// re/globals117.json. OC_DLL_MODE only switches spellings inside src117/gen/globals117.h,
// so a plain extern here would stay a symbol in both compiles -- making the shipped-form
// check byte-identical whatever the address is, and so vacuous. Every name is the sheet's
// (sc-refs/sheets/data_naming_1.17.csv, arr/units.cpp and arr/flingy.cpp).
//
// The extent is 228 for every units.cpp row, and that is not a guess: units.dat has 228
// rows, and each of these is indexed directly by wType with no bound check in any body
// here. gxFlingyMaxVel is flingy.dat's 209 rows on the same reasoning. None of the extents
// changes the emitted code -- the index is a runtime value -- so they state an intent the
// compiler cannot check; the address is what the shipped form proves.
#ifdef OC_DLL_MODE
#define gubUnitFlingy (reinterpret_cast<BYTE *>(OC_VA(0x51FAA8u)))
#define gxFlingyMaxVel (reinterpret_cast<int *>(OC_VA(0x510C88u)))
#define gubUnitPriorityPlane (reinterpret_cast<BYTE *>(OC_VA(0x520A98u)))
#define gubUnitOrderGuard (reinterpret_cast<BYTE *>(OC_VA(0x520F20u)))
#define gubUnitOrderAttackUnit (reinterpret_cast<BYTE *>(OC_VA(0x521008u)))
#define guwUnitTimeToBuild (reinterpret_cast<WORD *>(OC_VA(0x5238B0u)))
#define guwUnitCustomName (reinterpret_cast<WORD *>(OC_VA(0x524458u)))
// gxFlingyAccel and gubFlingyMaxTurn are flingy.cpp rows, so 209 entries, reached through
// gubUnitFlingy[wType]. Their element widths come off the loads -- scale 2 for the WORD,
// scale 1 for the BYTE -- and they fix the two callers' return types.
// gxUnitLife is 4 bytes wide and spelled long to match re/globals117.json, where the sign
// decides CUnit::GetLife's sar; this TU's own bodies work on DWORD locals and are
// indifferent, but a second spelling of one global collides with the generated header.
#define gxFlingyAccel (reinterpret_cast<WORD *>(OC_VA(0x510FD0u)))
#define gubFlingyMaxTurn (reinterpret_cast<BYTE *>(OC_VA(0x5114C0u)))
#define gxUnitLife (reinterpret_cast<LONG *>(OC_VA(0x520708u)))
#else
extern flingy_id_t gubUnitFlingy[228] OC_UNALIGNED;
extern int gxFlingyMaxVel[209] OC_UNALIGNED;
extern WORD gxFlingyAccel[209] OC_UNALIGNED;
extern BYTE gubFlingyMaxTurn[209];
extern LONG gxUnitLife[228] OC_UNALIGNED;
extern BYTE gubUnitPriorityPlane[228] OC_UNALIGNED;
extern order_id_t gubUnitOrderGuard[228] OC_UNALIGNED;
extern order_id_t gubUnitOrderAttackUnit[228] OC_UNALIGNED;
extern WORD guwUnitTimeToBuild[228] OC_UNALIGNED;
extern WORD guwUnitCustomName[228] OC_UNALIGNED;
#endif

// guwFirstWhatSnd / guwLastWhatSnd -- 0x522038 / 0x522200, the sheet names
// (arr/units.cpp); gamesnd.cpp carries the same TU-local spelling.
#ifdef OC_DLL_MODE
  #ifndef guwFirstWhatSnd
    #define guwFirstWhatSnd (reinterpret_cast<WORD *>(OC_VA(0x522038u)))
  #endif
#else
extern WORD guwFirstWhatSnd[228] OC_UNALIGNED;
#endif
#ifdef OC_DLL_MODE
  #ifndef guwLastWhatSnd
    #define guwLastWhatSnd (reinterpret_cast<WORD *>(OC_VA(0x522200u)))
  #endif
#else
extern WORD guwLastWhatSnd[228] OC_UNALIGNED;
#endif

// gubUnitOrderAI -- 0x520D50, the sheet name (arr/units.cpp); CUnitInit.cpp
// carries the same TU-local spelling.
#ifdef OC_DLL_MODE
  #ifndef gubUnitOrderAI
    #define gubUnitOrderAI (reinterpret_cast<BYTE *>(OC_VA(0x520D50u)))
  #endif
#else
extern order_id_t gubUnitOrderAI[228] OC_UNALIGNED;
#endif

// Free-function callees, declared here rather than in a header because gen needs a
// declaration only for a callee an emitted body actually reaches, and nothing outside this
// TU calls them.

// 0x48DD40 is declared in src117/h/SAI_Main.h, the header of the TU that defines it: a
// prototype in a .cpp is not a seat gen.py can include. This TU includes that header for
// it.

// The two string getters CUnit::GetName and get_unit_name_from_type tail-jump into: the id
// in ECX and nothing pushed, so __fastcall with one argument. The id is a WORD, since
// str_get_string touches it only as DX. status_get_str is declared in src117/h/order.h and
// not here, because two declarations of one symbol mangle differently and the payload link
// then fails; its return stays char * rather than const char *, because order.cpp passes
// the result to gamesnd_error(CUnit *, char *, DWORD).
#include "../h/order.h"

// The per-player tech and upgrade tables research_tech_cb and increment_upgrade_cb drive.
// They belong to the unported saveload.cpp and are declared in a header of src117 rather than
// in this TU: gen.py cannot emit a payload thunk for a callee whose declaration it cannot
// find, and a prototype inside a .cpp is invisible to it. src117/h/saveload.h also severs a
// reach -- cpp-1.17/lang/saveload.h exists and is on gen's search path, so without src117's
// these four would resolve there.
#include "../h/saveload.h"
#include "../h/TrigAct.h"      // get_map_string -- one seat, gamemap.cpp defines it
#include "../h/typelimits.h"  // Hyperion: a mod's types

// get_unit_name_from_type -- oracle 0x428CE0. A free function, not a member: the type
// arrives in CX with nothing pushed and no `this` is formed, and CUnit::GetName reaches it
// by inlining. __fastcall with one register argument, an unsigned WORD -- the bound check
// is cmp cx, ax and the index movzx eax, cx, where an int would compare and index at 32
// bits. Not static: the oracle keeps a standalone COMDAT at 0x428CE0 even though GetName
// inlines it. Its declaration is in src117/h/CUnit.h, since gen needs a header declaration
// for anything the payload ships.

// CUnit::AssignHPGain
//
// CUnit::AssignShieldGain's twin, directly below: how much health a building regains per
// tick so its bar fills exactly as construction completes. It stores only wHPGain, and
// unlike the shield version it rounds up.
//
// The divides are unsigned, so the life row is held in a DWORD. The build time goes through
// a named DWORD local for both the - 1 and the divisor, which keeps the round-up's two
// addends separate subtrees -- feeding the WORD in directly relinearises the numerator into
// time - life/10 - 1 + life. It is read and tested as a WORD first (test ax, ax), then
// promoted. Both arms clamp the gain to 1, since a gain of zero would stall the bar
// forever.
void CUnit::AssignHPGain()
{
  int nType = wType;
  WORD wTime = guwUnitTimeToBuild[nType];

  if (!wTime) {
    wHPGain = 1;
    return;
  }

  DWORD dwTime = wTime;
  DWORD dwLife = HandicapLife(gxUnitLife[nType], bPlayer);  // Hyperion: at the owner's handicap
  DWORD dwGain = dwLife - dwLife / 10;

  wHPGain = (WORD)((dwTime - 1 + dwGain) / dwTime);
  if (!wHPGain)
    wHPGain = 1;
}

// CUnit::AssignShieldGain
//
// Seeds a building's shields at one tenth of its maximum and works out how much to add per
// tick so the bar fills exactly as construction completes.
//
// Two divides, two signednesses. guwUnitEnergy[nType] << 8 is int, so / 10 is signed; the
// second divide is unsigned without a cast because lShields is a DWORD, and subtracting
// against an int local instead would make it cdq / idiv. wShieldGain = 1 materialises the
// constant in a register rather than store an imm16, whose prefix would cost an LCP stall.
// Both the no-build-time arm and the truncated-quotient arm clamp to 1, since a gain of
// zero would stall the shield bar forever.
void CUnit::AssignShieldGain()
{
  int nType = wType;

  if (!gbUnitHasShields[nType])
    return;
  if (!(dwFlags & UF_Building))
    return;

  lShields = (HandicapShields(guwUnitEnergy[nType], bPlayer) << 8) / 10;  // Hyperion: at the owner's handicap

  WORD wTime = guwUnitTimeToBuild[nType];
  if (wTime) {
    wShieldGain = (WORD)(((HandicapShields(guwUnitEnergy[nType], bPlayer) << 8) - lShields) / wTime);
    if (wShieldGain)
      return;
  }
  wShieldGain = 1;
}

// CUnit::CanMove
//
// GetRClickAction is called twice and MSVC does not CSE the two -- it is an out-of-line
// call with unknown side effects, so both calls are real and the source makes both. The
// first is entered with `this` still in the ECX it arrived in (the entry only copied it to
// ESI); the second has to reload it.
//
// The tail is xor ecx, ecx / cmp al, 3h / setnz cl / mov eax, ecx -- a comparison result
// being returned, not a branch, so the last line is a return <expr> and not
// another if. It needs a second register because AL still holds the callee's result.
BOOL CUnit::CanMove()
{
  if (dwFlags & UF_Building)
    return 0;

  if (!GetRClickAction())
    return 0;

  return GetRClickAction() != 3;
}

// CUnit::DisableDoodadState
//
// Not the same test as CUnit::EnableDoodadState (0x428A80, below) negated: Disable's four
// jumps all target the bare ret, so every term must be zero before it acts (an && chain),
// where Enable acts if any term does (an || chain).
//
// The flag term is spelled as a bit extract, (dwFlags >> 10) & 1, in both bodies. In a
// branch context MSVC folds it back to test ..., 400h, so it is byte-neutral here, but
// CUnit::ToggleDoodadState inlines Enable and CSEs its guard against the written
// subexpression: spelled dwFlags & 0x400 in one and as an extract in the other, Toggle
// costs a second bit test and six immediate-0 compares.
void CUnit::DisableDoodadState()
{
  if (!((dwFlags >> 10) & 1) && !bLockdownTimer && !bStasisTimer &&
      !bParalyzeTimer)
    SetOrder(0xAD);
}

// Order handler that switches a doodad or trap off, with a sound for the
// four door types.
void CUnit::DispatchDisableDoodad()
{
  if ((dwFlags & 0x400) || bLockdownTimer || bStasisTimer || bParalyzeTimer) {
    if (!pUnion74)
      SetOrder(gubUnitOrderGuard[wType]);
    EndUninterruptableDispatchOrder();
    return;
  }
  if (dwFlags & 0x2000)
    return;
  pSprite->SetAnimation(0x18, 1);
  if (wType == 0xCB || wType == 0xD1 || IsDoor())
    pSprite->bElevationLevel = 1;
  if (wType == 0xCD || wType == 0xCE || wType == 0xCF || wType == 0xD0) {
    gamesnd_play(guwFirstWhatSnd[wType], this, 1, 0);
    SetNextOrder(0xA8);
  } else {
    SetCloakStatus(3);
    dwFlags |= 0x400;
    dwUnionE4 = 0x80000000;
    bUnion91 = 0;
    if (!pUnion74)
      SetOrder(gubUnitOrderGuard[wType]);
  }
  EndUninterruptableDispatchOrder();
}

// Order handler for critters: wander to a random nearby point, pause, repeat.
void CUnit::DispatchCritter()
{
  switch (bOrderState) {
  case 0: {
    int nDir = net_randbetween(0, 0xFF, 0x30);
    int nDist;
    if (wType == 0x5E)
      nDist = net_randbetween(0xA0, 0x280, 0x30);
    else
      nDist = 0x20;
    CSprite *pSpr = pSprite;
    wOrderTargetX = pSpr->wPosX + ((DIRECTIONS[nDir * 2] * nDist) >> 8);
    wOrderTargetY = pSpr->wPosY + ((DIRECTIONS[nDir * 2 + 1] * nDist) >> 8);
    ClipToMap(&orderTargetPos, wType);
    SetMoveTarget_xy(wOrderTargetX, wOrderTargetY);
    bOrderState = 1;
    break;
  }

  case 1: {
    // The two-valued state and its always-true test are the oracle's; a
    // named local for it is what reserves the unused frame slot.
    int nState;
    if (wMoveTargetX == pSprite->wPosX && wMoveTargetY == PosU(pSprite->wPosY))
      nState = (dwFlags & 0x80000) ? 2 : 1;
    else
      nState = 0;
    if (nState) {
      if (wType == 0x5E) {
        bOrderState = 0;
      } else if (AtMoveTarget() == 2) {
        bUnion54 = 0x4B;
        bOrderState = 2;
      } else {
        int nWait = net_randbetween(0, 0x4B, 0x2F);
        bOrderState = 2;
        bUnion54 = (BYTE)(nWait > 0x0F ? nWait : 0);
      }
    }
    break;
  }

  case 2:
    if (!bUnion54)
      bOrderState = 0;
    break;
  }
}

// CUnit::DispatchDoorClose
//
// The whole body sits under one bit of bOrderFlags, and the oracle CSEs the byte it tested
// (mov al,[esi+4Fh] / test al,1h / ... / and al,-2h / mov [esi+4Fh],al) -- the
// read-modify-write-inside-the-guard shape, not what a standalone bOrderFlags &= ~1; emits.
//
// The elevation store is BYTE to BYTE, since both the priority-plane row and
// CSprite::bElevationLevel are one byte; the guard-order read two instructions later is
// zero-extended because it becomes SetOrder's int argument.
void CUnit::DispatchDoorClose()
{
  if (bOrderFlags & 1) {
    bOrderFlags &= ~1;
    dwFlags &= ~0x200000;
    pSprite->bElevationLevel = gubUnitPriorityPlane[wType];
    SetOrder(gubUnitOrderGuard[wType]);
    EndUninterruptableDispatchOrder();
    UpdateUnitCollision();
  }
}

// CUnit::DispatchDoorOpen
//
// DispatchDoorClose's mirror, and the differences are all in the data: the 0x200000 flag is
// set rather than cleared, the elevation is the literal 1 rather than the type's priority
// plane, a second flag (0x400) is set after the guard order is issued, and the tail jump
// goes to ai_mark_targets_stale instead of CUnit::UpdateUnitCollision.
void CUnit::DispatchDoorOpen()
{
  if (bOrderFlags & 1) {
    dwFlags |= 0x200000;
    bOrderFlags &= ~1;
    pSprite->bElevationLevel = 1;
    SetOrder(gubUnitOrderGuard[wType]);
    dwFlags |= 0x400;
    EndUninterruptableDispatchOrder();
    ai_mark_targets_stale(this);
  }
}

// Order handler for following another unit; hands off to the Nydus and
// shield-battery orders when the target is one.
void CUnit::DispatchFollow()
{
  CUnit *pTarget = pOrderTarget;
  if (!pTarget) {
    EndDispatchOrder();
    return;
  }
  if (dwUnion134) {
    int nMax = HandicapLife(gxUnitLife[wType], bPlayer) >> 8;  // Hyperion: at the owner's handicap
    if (!nMax) {
      int nCur = (lHitPoints + 0xFF) >> 8;
      nMax = nCur ? nCur : 1;
    }
    if ((lHitPoints + 0xFF) >> 8 == nMax) {
      EndDispatchOrder();
      return;
    }
  }
  if (bOrderState == 0) {
    if (!SetDestination_CUnit(pTarget))
      return;
    switch (pTarget->wType) {
    case 0x86:
      if (CanTravelThroughNydus(pTarget)) {
        SetOrder_CUnit(0x2F, pTarget);
        return;
      }
      break;
    case 0xAC:
      if (CanRechargeShieldsAt(pTarget)) {
        SetOrder_CUnit(0x42, pTarget);
        return;
      }
      break;
    }
    bOrderState = 1;
  }
  if (bUnion54)
    return;
  BYTE bSelf = bPlayer;
  bUnion54 = 7;
  BYTE bOwner = pTarget->bPlayer;
  if (bOwner == 11)
    bOwner = pTarget->pSprite->bCreator;
  if (!sgbAlliances_g[bSelf][bOwner] && CanTarget(pTarget, 1)) {
    SetOrder(2);
    return;
  }
  if (!(dwFlags & 4) && !UnitCanReachGroundTarget(this, pTarget)) {
    StopUnitMovement();
    return;
  }
  BYTE bSlotType = gPlayerData[pTarget->bPlayer].nType;
  if ((bSlotType == 3 || bSlotType == 4) && !IsWithinTargetRange(1, pTarget)) {
    SetDestination_CUnit(pTarget);
    return;
  }
  if (!IsWithinTargetRange(0x60, pTarget)) {
    ApproachMoveTarget_CUnit(pTarget);
    return;
  }
  if (pTarget->dwFlags & 2)
    EndDispatchOrder();
  else if (pUnion74)
    EndDispatchOrder();
  else
    StopUnitMovement();
}

// Order handler for a hidden gun trap: hides it, then wakes it on a target.
void CUnit::DispatchHiddenGun()
{
  switch (bOrderState) {
  case 0:
    dwFlags |= 0x200000;
    if (wType == 0xCB || wType == 0xD1)
      pSprite->bElevationLevel = 0;
    SetCloakStatus(3);
    dwUnionE4 = 0x80000000;
    bUnion91 = 0;
    bOrderState = 1;
    break;

  case 1:
    if (!(dwFlags & 0x400) && !bLockdownTimer && !bStasisTimer &&
        !bParalyzeTimer && !bUnion54) {
      bUnion54 = 0x0F;
      CUnit *pTarget = sub_432060();
      if (pTarget) {
        dwFlags &= ~0x200000;
        if (IsFloorTrap())
          pSprite->bElevationLevel = gubUnitPriorityPlane[wType];
        SetCloakStatus(0);
        bUnion91 = 0;
        SetOrder(0xAB);
        SetOrder_CUnit_interrupt(gubUnitOrderAttackUnit[wType], pTarget, 0);
        SetOrder_interrupt(0xAA, 0);
      }
    }
    break;
  }
}

// CUnit::DispatchHideTrap
//
// DispatchRevealTrap's two-state shape with a longer state 0; the opening movzx /
// sub eax, 0h / jz / dec eax / jnz is a dense-switch ladder, not an if/else-if chain.
// Case 1 is DispatchRevealTrap's case 1 instruction for instruction.
//
// Case 0 is a four-term && chain -- all four jnz leave for the SetAnimation tail -- the
// same flag and three timers CUnit::DisableDoodadState tests, in the same order. The attack
// arm ends on a return and not a break: the oracle leaves through
// jmp CUnit::EndUninterruptableDispatchOrder rather than falling into that tail.
void CUnit::DispatchHideTrap()
{
  switch (bOrderState) {
  case 0:
    if (!(dwFlags & 0x400) && !bLockdownTimer && !bStasisTimer && !bParalyzeTimer) {
      CUnit *pTarget = sub_432060();
      if (pTarget) {
        SetOrder_CUnit(gubUnitOrderAttackUnit[wType], pTarget);
        SetOrder_interrupt(0xAA, 0);
        EndUninterruptableDispatchOrder();
        return;
      }
    }
    pSprite->SetAnimation(0x0E, 1);
    bOrderFlags &= ~1;
    bOrderState = 1;
    break;

  case 1:
    if (bOrderFlags & 1) {
      bOrderFlags &= ~1;
      if (!pUnion74)
        SetOrder(gubUnitOrderGuard[wType]);
      EndUninterruptableDispatchOrder();
    }
    break;
  }
}

// Order handler that switches a doodad or trap back on; the mirror of
// DispatchDisableDoodad.
void CUnit::DispatchEnableDoodad()
{
  if ((dwFlags & 0x400) || bLockdownTimer || bStasisTimer || bParalyzeTimer) {
    if (!pUnion74)
      SetOrder(gubUnitOrderGuard[wType]);
    EndUninterruptableDispatchOrder();
    return;
  }
  if (dwFlags & 0x2000)
    return;
  pSprite->SetAnimation(0x1B, 1);
  if (wType == 0xCB || wType == 0xD1 || IsDoor())
    pSprite->bElevationLevel = gubUnitPriorityPlane[wType];
  if (wType == 0xCD || wType == 0xCE || wType == 0xCF || wType == 0xD0) {
    gamesnd_play(guwLastWhatSnd[wType], this, 1, 0);
    SetNextOrder(0xA9);
  } else if (!pUnion74) {
    SetOrder(gubUnitOrderGuard[wType]);
  }
  EndUninterruptableDispatchOrder();
}

// CUnit::DispatchIgnoreMove
//
// Order handler that walks to the order target ignoring everything on the way. Case 1's
// arrival test is an int local set to (dwFlags & 0x80000) ? 2 : 1, or to 0, and then
// tested: that shape is what emits the oracle's neg/sbb/neg/inc and reserves its unused
// [ebp-4] slot. The (WORD) cast on wPosY keeps the 16-bit compare.
void CUnit::DispatchIgnoreMove()
{
  switch (bOrderState) {
  case 0:
    {
      BYTE bArrived = pOrderTarget
                        ? SetDestination_CUnit(pOrderTarget)
                        : SetDestination_xy(wOrderTargetX, wOrderTargetY);
      if (bArrived)
        bOrderState = 1;
    }
    break;

  case 1:
    {
      int nState;
      if (wMoveTargetX == pSprite->wPosX && wMoveTargetY == PosU(pSprite->wPosY))
        nState = (dwFlags & 0x80000) ? 2 : 1;
      else
        nState = 0;
      if (nState) {
        EndDispatchOrder();
        if (dwUnion134)
          dwUnion7C = 0;
      }
    }
    break;
  }
}

// Order handler for a junkyard dog: roam at random, bite back at whatever
// attacked it, and otherwise pick fights with nearby enemies.
void CUnit::DispatchJunkYardDog()
{
  if (bOrderState == 0) {
    int nDir = net_randbetween(0, 0xFF, 0x30);
    int nDist = net_randbetween(0x400, 0x800, 0x30);
    wOrderTargetX = pSprite->wPosX + ((DIRECTIONS[nDir * 2] * nDist) >> 8);
    wOrderTargetY = pSprite->wPosY + ((DIRECTIONS[nDir * 2 + 1] * nDist) >> 8);
    ClipToMap(&orderTargetPos, wType);
    SetMoveTarget_xy(wOrderTargetX, wOrderTargetY);
    bOrderState = 1;
    bUnion57 = 0xFF;
  }
  CUnit *pAttacker = reinterpret_cast<CUnit *>(dwUnion7C);
  if (pAttacker) {
    BYTE bSelf = bPlayer;
    BYTE bOwner = pAttacker->bPlayer;
    if (bOwner == 11)
      bOwner = pAttacker->pSprite->bCreator;
    if (!sgbAlliances_g[bSelf][bOwner] && !(pAttacker->pSprite->flags.b & 0x20) &&
        !(pAttacker->dwFlags & 0x4000000) && !pAttacker->IsVisibleTo(this)) {
      SetOrder_CUnit(gubUnitOrderAttackUnit[wType], pAttacker);
      AppendOrder(0xBB, NULL);
      dwUnion7C = 0;
      HoldPosition();
      return;
    }
  }
  if (bUnion54)
    return;
  bUnion54 = 0x0F;
  int nState;
  if (wMoveTargetX == pSprite->wPosX && wMoveTargetY == PosU(pSprite->wPosY))
    nState = (dwFlags & 0x80000) ? 2 : 1;
  else
    nState = 0;
  if (nState || (!bUnion57 && !(dwFlags & 4))) {
    bOrderState = 0;
    return;
  }
  CUnit *pTarget = FindAttackTarget();
  if (pTarget) {
    SetOrder_CUnit(gubUnitOrderAttackUnit[wType], pTarget);
    AppendOrder(0xBB, NULL);
    dwFlags |= 0x8000000;
    if (subunit)
      subunit->dwFlags |= 0x8000000;
  }
}

// CUnit::DispatchRevealTrap
//
// A two-state order, and the oracle opens it as a switch and not as an if:
//     movzx eax, byte ptr [esi+4Eh] / sub eax, 0h / jz <0> / dec eax / jnz <ret>
// -- the sub reg, low plus dec-per-case ladder MSVC emits for a dense switch. An
// if (s == 0) ... else if (s == 1) compiles to compares, not to this.
void CUnit::DispatchRevealTrap()
{
  switch (bOrderState) {
  case 0:
    pSprite->SetAnimation(0x0D, 1);
    bOrderFlags &= ~1;
    bOrderState = 1;
    break;

  case 1:
    if (bOrderFlags & 1) {
      bOrderFlags &= ~1;
      if (!pUnion74)
        SetOrder(gubUnitOrderGuard[wType]);
      EndUninterruptableDispatchOrder();
    }
    break;
  }
}

// Order handler that keeps a unit within range of its target, turning the two
// siege types to face it.
void CUnit::DispatchStayInRange()
{
  CUnit *pTarget = pOrderTarget;
  if (pTarget) {
    wOrderTargetX = pTarget->pSprite->wPosX;
    wOrderTargetY = pTarget->pSprite->wPosY;
    if (!CanTarget(pTarget, 1)) {
      if (!(dwFlags & 0x8000000) && !dwUnion134)
        SetNextOrder_xy(6, wOrderTargetX, wOrderTargetY);
      EndDispatchOrder();
      return;
    }
    if (bOrderState == 0) {
      if (!IsUnitInWeaponRange(NULL) && !SetMoveTarget_CUnit(pOrderTarget))
        return;
      bOrderState = 1;
    } else if (IsUnitInWeaponRange(NULL)) {
      StopUnitMovement();
    } else if (!ApproachMoveTarget_CUnit(pOrderTarget)) {
      return;
    }
    if ((wType == 3 || wType == 0x11) && !(bMoveFlags & 2)) {
      upos_t wY = pSprite->wPosY;
      upos_t wX = pSprite->wPosX;
      int nAngle = AngleBetween(PosS(wX), PosS(wY), wOrderTargetX, wOrderTargetY);
      if (abs(TurnDelta(bUnion21, (BYTE)nAngle, 0x80)) >= 0x20)
        reinterpret_cast<CFlingy *>(this)->SetDestination(wOrderTargetX, wOrderTargetY);
    }
  } else {
    StopUnitMovement();
    EndDispatchOrder();
  }
}

// CUnit::DispatchStop
//
// Three thiscalls, the last one a tail jump.
void CUnit::DispatchStop()
{
  StopUnitMovement();
  BreakAnim();
  EndDispatchOrder();
}

// CUnit::DispatchWatchTarget
//
// Four tests, two destinations, and the branch senses split them cleanly. The first two
// (test eax,eax on pOrderTarget, then on CanTarget's result) leave with jz to the
// EndDispatchOrder_xy block -- failing them acts. The last two (jnz on
// IsUnitInWeaponRange, jnz on the GetRClickAction compare) leave with the opposite sense to
// the bare ret -- failing them returns. So the first pair is the && guard on an inner block
// and only the inner block can bail out.
//
// cmp al, 3h is a BYTE compare, which is CUnit::GetRClickAction's one-byte return arriving
// unwidened; an int return would emit a 32-bit compare here.
void CUnit::DispatchWatchTarget()
{
  if (pOrderTarget && CanTarget(pOrderTarget, 1)) {
    if (IsUnitInWeaponRange(0))
      return;
    if (GetRClickAction() != 3)
      return;
  }
  EndDispatchOrder_xy(wOrderTargetX, wOrderTargetY);
}

// CUnit::EnableDoodadState
//
// The || half of the pair whose derivation is on CUnit::DisableDoodadState (0x427E90,
// above): its first three jumps target the action block and only the fourth falls through
// to the ret, so any term acting is enough -- something is disabling this unit, so lift it.
// The bit-extract spelling of the flag term is required here rather than merely consistent,
// because this is the body ToggleDoodadState inlines. See CUnit::DisableDoodadState above.
void CUnit::EnableDoodadState()
{
  if (((dwFlags >> 10) & 1) || bLockdownTimer || bStasisTimer ||
      bParalyzeTimer) {
    dwFlags &= ~0x400;
    SetOrder(0xAC);
  }
}

// CUnit::GetAttackSpeedModifier
// CUnit::GetMovementSpeedModifier
//
// The same body with one immediate changed. The leading xor eax,eax / mov edx,1 / cmp /
// cmovnz is a ternary, not an if: an if branches.
int CUnit::GetAttackSpeedModifier()
{
  int nMod = 0;

  if (bStimTimer)
    nMod = 1;
  if (dwFlags & 0x20000000)
    ++nMod;
  if (bEnsnareTimer != 0)
    --nMod;
  return nMod;
}

int CUnit::GetMovementSpeedModifier()
{
  int nMod = 0;

  if (bStimTimer)
    nMod = 1;
  if (dwFlags & 0x10000000)
    ++nMod;
  if (bEnsnareTimer != 0)
    --nMod;
  return nMod;
}

// CUnit::GetTypeEx
//
// The unit's type, except that a zerg building mid-morph reports the thing it is turning
// into -- wBuildQueue[bBuildQueueSlot] -- rather than what it is.
//
// The return is 16 bits: both exits are mov ax, <word> and neither clears the top half of
// EAX. An int return would have to movzx, which the oracle does not do.
WORD CUnit::GetTypeEx()
{
  if (IsZergBldgUpgrading())
    return wBuildQueue[bBuildQueueSlot];

  return wType;
}

// CUnit::FromIdAlive Static __fastcall: ECX
// is a packed unit id, not an object.
//
// The decode half of CUnit::GetUnitID: the low 11 bits are the one-based table index and the
// high bits are checked against the slot's generation byte, so a stale id decodes to null,
// not to the wrong unit. The two failure tests are joined with && and the early return is
// flat. The id is cast signed for the arithmetic shift; its top bit is never set.
// Hyperion: a game past 2,047 units takes wide ids (poollimits.h), whose
// index past the table names no unit. A 1.17 game reads past its 1,700 for an index up to
// 2,047, as 1.17 does.
static bool IdPastTable(DWORD dwUnitId)
{
  return g_pools.idShift != UNIT_ID_SHIFT_117
      && (dwUnitId & ((1u << g_pools.idShift) - 1)) - 1 >= static_cast<DWORD>(g_pools.units);
}

CUnit *__fastcall CUnit::FromIdAlive(DWORD dwUnitId)
{
  if (!dwUnitId || IdPastTable(dwUnitId))  // Hyperion: IdPastTable
    return NULL;

  CUnit *pUnit = CUnit::Head() + (dwUnitId & ((1u << g_pools.idShift) - 1)) - 1;  // Hyperion: 0x7FF in 1.17

  if (!pUnit->IsDead() && pUnit->bIdGeneration == (int)dwUnitId >> g_pools.idShift)  // Hyperion: 11
    return pUnit;

  return NULL;
}

// Returns the unit slot an id names without checking it is alive.
CUnit *__fastcall CUnit::FromId(DWORD dwUnitId)
{
  if (dwUnitId && !IdPastTable(dwUnitId))  // Hyperion: IdPastTable
    return CUnit::Head() + (dwUnitId & ((1u << g_pools.idShift) - 1)) - 1;  // Hyperion: 0x7FF
  return NULL;
}

// CUnit::GetUnitID The encode half; see
// FromIdAlive.
//
// The divide by 336, sizeof(CUnit), is written as a division, and the byte difference goes
// through DWORD so the divide is unsigned. The NULL `this` check at the top is 1.17's, kept.
// 0x6A4 == 1700 is the slot cap; an out-of-range slot returns 0 through the null exit.
int CUnit::GetUnitID()
{
  if (!this)
    return 0;

  DWORD dwIndex = (DWORD)((BYTE *)this - (BYTE *)CUnit::Head()) / sizeof(CUnit) + 1;

  if (dwIndex > (DWORD)g_pools.units)
    return 0;

  return (bIdGeneration << g_pools.idShift) | dwIndex;  // Hyperion: 11 in 1.17
}

// CUnit::FlingyAccelWithModifiers
// CUnit::FlingyTurnRadiusWithModifiers
//
// The same body over two different flingy.cpp rows, and the pair is what fixes each
// other's return width. Both scale the raw table value by the movement modifier through the
// rule CUnit::GetModifiedSpeed (0x429350) implements, written out here rather than called;
// the callee only has to be in the TU for MSVC to inline it, and definition order does not
// matter. CUnit::GetMovementSpeedModifier (0x428AF0) is inlined into each.
//
// The table is read before the modifier, which is what stages the cmov constant in ESI
// while EDX carries the table value; the other way round the two registers swap. The return
// widths are the element widths, read off the modifier <= 0 exits -- mov ax, dx for the
// WORD table, mov al, dl for the BYTE one.
WORD CUnit::FlingyAccelWithModifiers()
{
  DWORD dwAccel = gxFlingyAccel[gubUnitFlingy[wType]];
  int nMod = GetMovementSpeedModifier();

  if (nMod > 0)
    dwAccel += dwAccel;
  else if (nMod < 0)
    dwAccel -= dwAccel / 4;

  return (WORD)dwAccel;
}

BYTE CUnit::FlingyTurnRadiusWithModifiers()
{
  DWORD dwTurn = gubFlingyMaxTurn[gubUnitFlingy[wType]];
  int nMod = GetMovementSpeedModifier();

  if (nMod > 0)
    dwTurn += dwTurn;
  else if (nMod < 0)
    dwTurn -= dwTurn / 4;

  return (BYTE)dwTurn;
}

// CUnit::FlingyMaxSpeedWithModifiers
//
// Two table hops -- unit type to flingy row, flingy row to top speed -- and the
// raw speed goes straight through the modifier. No frame at all: `this` is
// never copied out of ECX, because the only call made keeps it there.
//
// It is a `call` + `ret` rather than a tail `jmp` and could not be otherwise:
// ModifyGivenMovementSpeed ends `ret 4h` and cleans an argument this function
// does not have, so the frames cannot be shared.
int CUnit::FlingyMaxSpeedWithModifiers()
{
  return ModifyGivenMovementSpeed(gxFlingyMaxVel[gubUnitFlingy[wType]]);
}

// get_unit_name_from_type
//
// Three arms. Out of range (>= 228, the units.dat row count) yields the empty string: the
// address the oracle loads is a NUL byte in the .rdata padding, where the linker parks a
// pooled "" under /GF, so it is a string constant and not a global. Otherwise the map's own
// custom name wins when the scenario set one (guwUnitCustomName is a per-type string id, 0
// meaning none), and failing that the stock name comes from status_get_str at type + 1 --
// that table is 1-based. Both arms are tail jumps, as a return f(x); produces.
#include "../port/data/port_moddata.h"  // Hyperion: ModUnitName, ModUnitEditName

const char *__fastcall get_unit_name_from_type(WORD wUnitType)
{
  if (!UnitTypeValid(wUnitType))  // Hyperion: >= 228, and a mod's types
    return "";
  if (UnitTypeMod(wUnitType))  // Hyperion: a mod's unit's name is the mod's
    return ModUnitName(wUnitType);

  WORD wCustomName = guwUnitCustomName[wUnitType];

  if (wCustomName)
    return get_map_string(wCustomName);
  // Hyperion: a name a mod's edit gives the unit, under the map's own
  if (const char *pszEdit = ModUnitEditName(wUnitType))
    return pszEdit;

  return status_get_str(wUnitType + 1);
}

// CUnit::GetName
//
// One line, and the oracle shows it is one line. The body is GetTypeEx and
// get_unit_name_from_type inlined back to back: the head through
// movzx ecx, word ptr [esi+64h] is GetTypeEx with its mov ax replaced by a zero-extending
// load into the argument register, and everything from mov eax, 0E4h onward is
// get_unit_name_from_type instruction for instruction. The standalone COMDAT at 0x428CE0
// survives regardless, because that definition has external linkage and other TUs call it.
const char *CUnit::GetName()
{
  // Hyperion: a rich mineral field or geyser says so (rich bases)
  if (const char *pszRich = MutatorRichName(static_cast<int>(this - CUnit::Head()), wType))
    return pszRich;
  return get_unit_name_from_type(GetTypeEx());
}

// CUnit::GetRace
//
// Three arms off one gubUnitCountInfo byte. The last one is the interesting
// shape: the oracle has no branch for it at all --
//     and al, 2h / neg al / sbb al, al / and al, -3h / add al, 4h
// -- which is MSVC's branchless `cond ? 1 : 4` (sbb builds 0 or -1, & -3 turns
// that into 0 or -3, +4 lands on 4 or 1). Written as a ternary, not as an if,
// because an if is what produces the branch.
BYTE CUnit::GetRace()
{
  BYTE bInfo = gubUnitCountInfo[wType];

  if (bInfo & 1)
    return 0;
  if (bInfo & 4)
    return 2;
  if (bInfo & 2)
    return 1;
  return 4;
}

// CUnit::GetSizeType
//
// Bits 25 and 26 of the type's flag word. The return is A BYTE: the oracle's
// fast arm is `mov al, 1h`, which leaves the upper 24 bits of EAX holding the
// flag word it just loaded. An int return would have to zero them.
BYTE CUnit::GetSizeType()
{
  DWORD dwFlags_ = gulUnitTypeFlags[wType];

  if (dwFlags_ & 0x2000000)
    return 1;

  BYTE bSize = (BYTE)(dwFlags_ >> 25);
  bSize &= 2;
  return bSize;
}

// CUnit::GiveToNeutralSlot
//
// Player 11 is the neutral slot. The trailing test is `test dword ptr
// [esi+0DCh], 40000000h` at full width -- a mask that high cannot be reached by
// the byte form MSVC narrows small dwFlags masks into, so the dword declaration
// is what reproduces it.
void CUnit::GiveToNeutralSlot()
{
  if (IsConstructingAddon())
    CancelAddon();

  GiveToPlayer(11);

  if (!(dwFlags & 0x40000000))
    SetBackgroundOrder(0x17);
}

// Puts a doodad or trap into its disabled state when map load asks for it.
void CUnit::InitDisableDoodad()
{
  if (!(dwFlags & 0x400) && !bLockdownTimer && !bStasisTimer && !bParalyzeTimer) {
    dwFlags |= 0x400;
    pSprite->SetAnimation(0x0F, 1);
    dwFlags |= 0x200000;
    if (wType == 0xCB || wType == 0xD1 || wType == 0xCD || wType == 0xCE ||
        wType == 0xCF || wType == 0xD0)
      pSprite->bElevationLevel = 1;
    if (wType == 0xCB || wType == 0xD1 || wType == 0xD2 || wType == 0xD3 ||
        wType == 0xD4 || wType == 0xD5) {
      SetCloakStatus(3);
      dwUnionE4 = 0x80000000;
      bUnion91 = 0;
    }
  }
}

// Hands the unit to another player: moves its tech and upgrade state, drops
// any build or research in progress, and picks the new owner's idle order.
void CUnit::GiveToPlayer(int nPlayer)
{
  BYTE bNewPlayer = (BYTE)nPlayer;
  sub_4BBCB0(this, reinterpret_cast<PFN_TECH_ENUM>(research_tech_cb), bNewPlayer);
  sub_4BBD40(this, reinterpret_cast<PFN_TECH_ENUM>(increment_upgrade_cb), bNewPlayer);
  sub_49C8B0(this);
  RemoveFromStatusBar();
  if (dwFlags & 2) {
    if (UnitTypeQueueable(GetBuildUnit(0))) {  // Hyperion: <= 0x69
      RemoveAllBuildQueue();
      pSprite->SetAnimation(0x14, 1);
    }
    if (bTechType != 0x2C)
      CancelResearch();
    if (bUpgradeType != 0x3D)
      CancelUpgrade();
  }
  if (wType == 0x48 || wType == 0x52 || wType == 0x53 || wType == 0x51)
    RemoveAllBuildQueue();
  SetPlayer(nPlayer, 1);
  if (PlayerIsLobby((BYTE)nPlayer))  // Hyperion: < 8 in 1.17
    SetOwnerColor(nPlayer);
  sub_4BBD40(this, reinterpret_cast<PFN_TECH_ENUM>(sub_429500), 0);
  if (IsConstructingAddon() || !(dwFlags & 1))
    return;
  if ((gulUnitTypeFlags[wType] & 0x800) || wType == 0x49 || wType == 0x55 ||
      wType == 0x0E || (dwFlags & 0x40))
    return;
  switch (gPlayerData[bNewPlayer].nType) {
  case 1:
    SetOrder(gubUnitOrderAI[wType]);
    break;
  case 3:
    SetOrder(0xA1);
    break;
  case 7:
    SetOrder(0xA2);
    break;
  default:
    SetOrder(gubUnitOrderInitialize[wType]);
    break;
  }
}

// CUnit::IsCritter
//
// Six types in the oracle's own order -- 0x59, 0x5A, 0x5F, 0x5E, 0x5D, 0x60 --
// and the order is kept because MSVC evaluates `||` left to right and emits one
// compare per term in exactly that sequence. Sorting them would be a different
// function that happens to compute the same predicate.
BOOL CUnit::IsCritter()
{
  int nType = wType;

  return nType == 0x59 || nType == 0x5A || nType == 0x5F ||
         nType == 0x5E || nType == 0x5D || nType == 0x60;
}

// CUnit::IsDoor
//
// Four types in the oracle's own order -- 0xCD, 0xCE, 0xCF, 0xD0 -- kept in that sequence
// because MSVC emits one compare per || term, left to right. The compares are 16 bits where
// IsCritter's are 32 (mov ecx, 0CDh / cmp ax, cx against cmp eax, 59h) only because of the
// constants' magnitude: <= 0x7F widens to cmp r32, imm8, and above that MSVC stages a
// register to dodge the operand-size prefix. Neither predicate needs an int local.
BOOL CUnit::IsDoor()
{
  return wType == 0xCD || wType == 0xCE || wType == 0xCF || wType == 0xD0;
}

// CUnit::IsEnemyUnit
//
// sgbAlliances_g is a 12x12 BYTE matrix: the row is the asking player, the column the other,
// and a zero entry means not allied. Player 11, the neutral slot, is indirected through the
// sprite: the question goes to the player who created it (CSprite::bCreator at +0x0A), which
// is how neutral map objects inherit an owner.
BOOL CUnit::IsEnemyUnit(CUnit *pOther)
{
  BYTE bMine = bPlayer;
  BYTE bTheirs = pOther->bPlayer;

  if (bTheirs == 11)
    bTheirs = pOther->pSprite->bCreator;

  return sgbAlliances_g[bMine][bTheirs] == 0;
}

// IsEnemyUnit The free function: the member
// CUnit::IsEnemyUnit (0x4290E0) above, asked on behalf of an arbitrary player.
//
// __fastcall with two register arguments: ECX is a bare player index and EDX a CUnit.
// nPlayer is a full DWORD because the lea scales the whole register; the other index comes
// from a byte field.
BOOL __fastcall IsEnemyUnit(int nPlayer, CUnit *pUnit)
{
  BYTE bTheirs = pUnit->bPlayer;

  if (bTheirs == 11)
    bTheirs = pUnit->pSprite->bCreator;

  return sgbAlliances_g[nPlayer][bTheirs] == 0;
}

// CUnit::IsFloorTrap
//
// Two unit types, 0xCB and 0xD1. Which two is not derived -- the oracle gives
// the numbers and the approved sheets give the function its name; nothing in the
// instruction stream says what a "floor trap" is. Both constants are > 0x7F, so
// both compares are the register-staged 16-bit form:
//     mov ecx, 0CBh / cmp ax, cx / jz  ...  mov ecx, 0D1h / cmp ax, cx / jz
BOOL CUnit::IsFloorTrap()
{
  return wType == 0xCB || wType == 0xD1;
}

// CUnit::IsTrap
//
// IsFloorTrap's two constants followed by four more, in the oracle's own order. The oracle
// does not collapse the contiguous 0xD1..0xD5 into a range, while CUnit::IsMultiSelectable
// below does for 0xCB..0xD5: a written-out || chain of equalities stays a chain, so a range
// here would be a different function.
BOOL CUnit::IsTrap()
{
  return wType == 0xCB || wType == 0xD1 || wType == 0xD2 || wType == 0xD3 ||
         wType == 0xD4 || wType == 0xD5;
}

// CUnit::IsMultiSelectable
//
// The return sense is inverted from every other predicate here: the fallthrough at +0x88 is
// mov eax, 1h and all fifteen conditional jumps land on the +0x8E xor eax, eax, so each
// listed condition means not multi-selectable -- if (<big ||>) return 0; return 1;. Written
// return !(...), MSVC merges the two constant loads and adds a jmp.
//
// One table load serves both bit tests, and dwFlags & 0x400 is tested at full DWORD width,
// since bit 10 sits in the second byte and MSVC will not narrow to it. 0xCB..0xD5 is a
// range nested in the || chain, unsigned because the value is a zero-extended WORD; of the
// ten tail equalities eight encode as cmp edx, imm8 and 0xCA stages a register.
// Hyperion: in a game of Hyperion's selection a building selects with others too
// (selectlimits.h).
BOOL CUnit::IsMultiSelectable()
{
  DWORD dwTypeFlags = gulUnitTypeFlags[wType];

  if (((dwTypeFlags & 1) && !SelectionWide()) || (dwTypeFlags & 0x800) || (dwFlags & 0x400) ||
      bLockdownTimer || bStasisTimer || bParalyzeTimer ||
      (wType >= 0xCB && wType <= 0xD5) ||
      wType == 0x0D || wType == 0x24 || wType == 0x59 || wType == 0x5A ||
      wType == 0x5F || wType == 0x5E || wType == 0x5D || wType == 0x60 ||
      wType == 0xCA || wType == 0x69)
    return 0;

  return 1;
}

// CUnit::IsRallyer
//
// Twelve types in the oracle's own order (0x9B before 0xA7 is the oracle's, not a sorting
// slip), because MSVC emits one compare per || term, left to right. The chain mixes 32-bit
// and 16-bit compares from one expression: MSVC picks the encoding per constant, widening
// to cmp eax, imm8 where it fits and staging a register above 0x7F to dodge the
// operand-size prefix. The predicate is written at the member's own width, with no int
// local.
BOOL CUnit::IsRallyer()
{
  return wType == 0x6A || wType == 0x6F || wType == 0x71 || wType == 0x72 ||
         wType == 0x82 || wType == 0x83 || wType == 0x84 || wType == 0x85 ||
         wType == 0x9A || wType == 0xA0 || wType == 0xA7 || wType == 0x9B;
}

// CUnit::IsRescuable
//
// The odd one out: it never looks at wType. movzx eax, byte ptr [ecx+4Ch] is bPlayer, and
// [eax+eax*8]*4 + 0x6726D0 is a stride of 36 -- sizeof(PlayerData) -- over
// &gPlayerData[0].nType, so it asks what kind of slot owns the unit; slot types 3 and 4 are
// the rescuable ones. That address is a field inside a registered global, not one of its
// own. The local must be BYTE: the oracle loads mov al, byte ptr [...] and compares
// cmp al, 3h, leaving the upper 24 bits of EAX undefined, where an int local forces a movzx.
BOOL CUnit::IsRescuable()
{
  BYTE bSlotType = gPlayerData[bPlayer].nType;

  return bSlotType == 3 || bSlotType == 4;
}

// CUnit::IsBeaconType
//
// A range plus one equality, separated by the branch senses: the jb on the 0xC2 bound skips
// only the upper bound (a short-circuit &&), while the jbe on 0xC7 and the jz on 0xCC both
// leave for the same true block (the ||). No cast to unsigned is needed -- MSVC knows the
// movzx'd value is in [0, 0xFFFF] and narrows to the unsigned 16-bit compare itself, which
// is what jb/jbe rather than jl/jle records.
BOOL CUnit::IsBeaconType()
{
  return (wType >= 0xC2 && wType <= 0xC7) || wType == 0xCC;
}

// IsBuiltRefineryType A free function; its
// signature is explained on the declaration in src117/h/CUnitMisc.h.
//
// The argument is 16 bits wide: a raw parameter register compared with the 16-bit
// `cmp cx, 6Eh`, which an int parameter would compare at 32 bits. Three types, 0x6E, 0x9D and
// 0x95, one per race by inference.
BOOL __fastcall IsBuiltRefineryType(WORD wUnitType)
{
  return wUnitType == 0x6E || wUnitType == 0x9D || wUnitType == 0x95;
}

// CUnit::GetModifiedSpeed
// CUnit::ModifyGivenMovementSpeed
//
// The plain three-way scale, and the same scale wrapped in the unit-type special cases: a
// positive modifier doubles, a negative one takes a quarter off, zero passes through.
// ModifyGivenMovementSpeed also sends three types (0x46, 0x50, 0x58) straight to 0x6AB,
// scales by 1.5 rather than 2, and floors the result at 0x355.
//
// Both accumulators are unsigned -- every divide is a shr with no round-toward-zero
// correction and the floor compares with jnb -- so ModifyGivenMovementSpeed's parameter
// is DWORD. Each body accumulates into one variable returned once, so the single
// ret pins it to EAX; with an early return in the doubling arm the value lands in EDX and
// every use costs a lea. The oracle's two epilogues are MSVC duplicating one.
int CUnit::GetModifiedSpeed(DWORD dwSpeed)
{
  int nMod = GetMovementSpeedModifier();

  if (nMod > 0)
    dwSpeed += dwSpeed;
  else if (nMod < 0)
    dwSpeed -= dwSpeed / 4;

  return dwSpeed;
}

int CUnit::ModifyGivenMovementSpeed(DWORD dwSpeed)
{
  int nMod = GetMovementSpeedModifier();

  if (nMod > 0) {
    if (wType == 0x46 || wType == 0x50 || wType == 0x58)
      return 0x6AB;

    dwSpeed += dwSpeed / 2;
    if (dwSpeed < 0x355)
      dwSpeed = 0x355;
  } else if (nMod < 0) {
    dwSpeed /= 2;
  }

  return dwSpeed;
}

// CUnit::ToggleDoodadState
//
// DisableDoodadState and EnableDoodadState inlined back to back, sharing one dwFlags load
// and one bit test. The else arm is a bare SetOrder because Disable's guard folds away
// there -- all four terms are known zero -- while Enable's guard is tested again at +0x33,
// since the then path knows only their disjunction; spelling that arm DisableDoodadState()
// gives a conditional tail jump instead of the folded body, 94 bytes against 102.
//
// The flag term is a bit extract here too: it puts a 0/1 in EAX, which makes all six byte
// compares cmp byte ptr [ecx+N], al and leaves the raw flags in EDX. Spelled
// dwFlags & 0x400 the body is 101 bytes, as it is for !!(...) and for De Morgan over the
// whole condition.
void CUnit::ToggleDoodadState()
{
  if (((dwFlags >> 10) & 1) || bLockdownTimer || bStasisTimer ||
      bParalyzeTimer)
    EnableDoodadState();
  else
    SetOrder(0xAD);
}

// research_tech_cb
//
// One of the three __stdcall four-slot callbacks at the end of this TU (src117/h/CUnitMisc.h
// has the family's signature). Marks a tech researched for a player, if it is not already.
//
// The parameter slots are int and every use casts down, which is a uniform callback
// signature meeting a handler that wants bytes: the oracle loads a dword and narrows
// afterwards, or folds the narrowing into the operand (cmp byte ptr [ebp+10h], 0h). BYTE
// slots lose the first shape and wide uses the second. The (BYTE) on the player is the
// movzx esi, al that get_tech_researched needs; the (WORD) on the tech id costs nothing.
BOOL __stdcall research_tech_cb(int a1, int nTechId, int nEnable, int nPlayer)
{
  if ((BYTE)nEnable) {
    if (!get_tech_researched((BYTE)nPlayer, (WORD)nTechId))
      set_tech_researched((BYTE)nPlayer, (WORD)nTechId, 1);
  }
  return 1;
}

// increment_upgrade_cb
//
// research_tech_cb's sibling: raises a player's upgrade level, but only if the requested
// level is higher than the one held. Slot map off the frame displacements: +0x08 unread,
// +0x0C the id, +0x10 the level, +0x14 the player -- the uniform signature costing an
// unused slot.
//
// The compare is 8 bits and unsigned (cmp cl, al / jbe), so both sides must be BYTE. The
// setter gets the full dword (push ecx), because a byte argument rides a dword slot whose
// upper bits are undefined; (BYTE)nLevel is what makes CL the value and ECX the carrier.
BOOL __stdcall increment_upgrade_cb(int a1, int nUpgradeId, int nLevel, int nPlayer)
{
  if ((BYTE)nLevel > get_upgrade_level((BYTE)nPlayer, (WORD)nUpgradeId))
    set_upgrade_level((BYTE)nPlayer, (WORD)nUpgradeId, (BYTE)nLevel);

  return 1;
}

// sub_429500 The third of the callback
// family and the shortest.
//
// Slot 1 is a CUnit *, not an unread int as in its two siblings: it is the `this` of the
// __thiscall CUnit::sub_43B0B0. Slot 4 is unread and slot 3 is the guard, compared at byte
// width, so the cast sits at the use though the slot is an int. a2 is forwarded whole.
BOOL __stdcall sub_429500(CUnit *pUnit, int a2, int a3, int a4)
{
  if ((BYTE)a3)
    pUnit->sub_43B0B0(a2);

  return 1;
}
