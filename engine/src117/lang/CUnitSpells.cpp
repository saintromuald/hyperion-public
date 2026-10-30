// CUnitSpells.cpp -- the cloak, energy and status-effect core. Bodies are in oracle VA
// order.
//
// TU membership matters in both directions here. CUnit::Cloak inlines
// CUnit::GetCloakingTech's eight-way ladder, so moving that callee out of this TU would
// change Cloak, and a callee the oracle emits a real call to would be spliced by /Ob2 if
// it were defined here. Check both before moving a body in or out.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playervision.h"  // Hyperion: the slots past 7
#include "../port/data/port_formats.h"  // Hyperion: PortSaveReadIds, PortSaveWriteIds
#include "../gen/save117.h"
#include <intrin.h>
#include "../h/CUnitSpells.h"
#include "../h/CUnit.h"
#include "../h/CUnitTSpells.h"   // status_set_update
#include "../h/CSprite.h"        // pSprite->flags (sub_434C60)
#include "../h/saveload.h"       // pfread / pfwrite
#include "../h/CImage.h"         // CImage::SetAnimation (DestroyOverlays)
#include "../h/order.h"          // get_units_in_rect + rect16_t; status_get_str / map_msg_target_self / gamesnd_error
#include "../h/CUnitFind.h"      // finish_unit_iteration
#include "../h/CFlingy.h"        // BreakingDistance / SetDestination (w63b)
#include "../h/CTarget.h"        // XYInRadius (w63b)
#include "../h/count.h"          // count_race (w63b)
#include "../h/rect16.h"         // rect16_t (ApplyEMPLocation, w63c)
#include "../h/collide.h"        // mtx_has_flags_px
#include "../h/CUnitCombat.h"    // IsVulnerableType
#include "../h/CUnitInit.h"      // get_order_tech_energy_cost
#include "../h/TrigAct.h"        // net_rand
#include "../h/SAI_Banker.h"     // banker_hold_ai_spells
#include "../gen/globals117.h"
#include "../port/data/port_moddata.h"  // Hyperion: ModTechUseTree

// Registry rows no generated header carries yet, one #ifndef per name: the set grows over
// time and a single guard over the block would undeclare every other name in it.
#ifndef glCheatBits
#ifdef OC_DLL_MODE
#define glCheatBits (*reinterpret_cast<DWORD *>(OC_VA(0x5432FCu)))
#else
extern DWORD glCheatBits;
#endif
#endif

#ifndef sgpBurrowLinkHead_g
#ifdef OC_DLL_MODE
#define sgpBurrowLinkHead_g (*reinterpret_cast<CUnit **>(OC_VA(0x665D40u)))
#else
extern CUnit *sgpBurrowLinkHead_g;
#endif
#endif

#ifndef order_unk9
#ifdef OC_DLL_MODE
#define order_unk9 (reinterpret_cast<BYTE *>(OC_VA(0x51B440u)))
#else
extern BYTE order_unk9[189] OC_UNALIGNED;
#endif
#endif

#ifndef gubOrderWeapon
#ifdef OC_DLL_MODE
#define gubOrderWeapon (reinterpret_cast<BYTE *>(OC_VA(0x51B740u)))
#else
extern weapon_id_t gubOrderWeapon[189];
#endif
#endif

#ifndef techdata_energy_g
#ifdef OC_DLL_MODE
#define techdata_energy_g (reinterpret_cast<WORD *>(OC_VA(0x51F868u)))
#else
extern WORD techdata_energy_g[44] OC_UNALIGNED;
#endif
#endif

#ifndef gwMapPixWidth
#ifdef OC_DLL_MODE
#define gwMapPixWidth (*reinterpret_cast<WORD *>(OC_VA(0x543230u)))
#else
extern upos_t gwMapPixWidth;
#endif
#endif

#ifndef gwMapPixHeight
#ifdef OC_DLL_MODE
#define gwMapPixHeight (*reinterpret_cast<WORD *>(OC_VA(0x543234u)))
#else
extern upos_t gwMapPixHeight;
#endif
#endif

#ifndef wpn_innersplash_g
#ifdef OC_DLL_MODE
#define wpn_innersplash_g (reinterpret_cast<WORD *>(OC_VA(0x525968u)))
#else
extern WORD wpn_innersplash_g[130] OC_UNALIGNED;
#endif
#endif
// 0x51B800 order_techType, sheet row 511; spelled as select.cpp spells it.
#ifndef order_techType
#ifdef OC_DLL_MODE
#define order_techType (reinterpret_cast<BYTE *>(OC_VA(0x51B800u)))
#else
extern tech_id_t order_techType[189];
#endif
#endif

void cunitspells_init() {
  sgpBurrowLinkHead_g = 0;
}

int CUnit::GetMaxEnergyReal() {
  return GetMaxEnergy() >> 8;
}

void CUnit::SubtractEnergy(WORD wCost) {
  if ((glCheatBits & 0x800) == 0)
    wEnergy -= wCost;
}

int CUnit::GetMaxEnergyInProgress() {
  if (wType == 0x3F && IsCompletingSummoning())
    return 0x3200;
  return GetMaxEnergy();
}

// A hallucination lives 1350 frames, a broodling 1800, anything else forever.
// The WORD return is what keeps both constants as branches (int gives cmov).
WORD CUnit::GetMaxTimeToLive() {
  if (dwFlags & 0x40000000)
    return 1350;
  if (wType != 0x28)
    return 0;
  return 1800;
}

BOOL CUnit::NeedsMoreEnergy(WORD wCost) {
  if (glCheatBits & 0x800)
    return FALSE;
  return wEnergy < wCost;
}

BYTE CUnit::GetCloakingTech() {
  if (wType == 0x01 || wType == 0x10 || wType == 0x64 || wType == 0x63 ||
      wType == 0x68 || wType == 0x33)
    return 0x0A;
  if (wType == 0x08 || wType == 0x15)
    return 0x09;
  return 0x2C;
}

BOOL CUnit::IsRevealedBySpell() {
  if (wDefMatrixHitPoints != 0 || bLockdownTimer != 0 || bParalyzeTimer != 0 ||
      bIrradiateTimer != 0 || bEnsnareTimer != 0 || bStasisTimer != 0 ||
      bBloodBoilTimer != 0 || bCrunchyCount != 0)
    return TRUE;
  return FALSE;
}

void CUnit::sub_4340D0() {
  BYTE bOld = bCloakCount;
  bCloakCount = (BYTE)(bOld + 1);
  if (bOld != 0)
    return;
  if (dwFlags & 0x100)
    return;
  if (pBurrowLinkNext != 0)
    return;
  pBurrowLinkNext = sgpBurrowLinkHead_g;
  pBurrowLinkPrev = 0;
  if (sgpBurrowLinkHead_g != 0)
    sgpBurrowLinkHead_g->pBurrowLinkPrev = this;
  sgpBurrowLinkHead_g = this;
  status_set_update();
}

void CUnit::SyncAllStatusEffects() {
  if (bDefMatrixTimer != 0)
    SyncDefMatrix();
  if (bLockdownTimer != 0)
    SyncLockdown();
  if (bParalyzeTimer != 0)
    SyncParalyze();
  if (bIrradiateTimer != 0)
    SyncIrradiate();
  if (bEnsnareTimer != 0)
    SyncEnsnare();
  if (bStasisTimer != 0)
    SyncStasis();
  if (bBloodBoilTimer != 0)
    SyncBloodBoil();
  if (bCrunchyCount != 0)
    SyncCrunchy();
}

// The __declspec(noinline) repeats the declaration in CUnit.h: it is a steering lever, not
// a derivation. Without it /Ob2 splices this body into CloakUpdate, whose oracle emits a
// real call. The code is the same with the attribute and without it.
__declspec(noinline) BOOL CUnit::sub_434DA0() {
  BOOL bChanged = FALSE;

  if (bCloakCount == 0) {
    if (sgpBurrowLinkHead_g == this) {
      sgpBurrowLinkHead_g = pBurrowLinkNext;
      bChanged = TRUE;
    }
    if (pBurrowLinkPrev != 0) {
      pBurrowLinkPrev->pBurrowLinkNext = pBurrowLinkNext;
      bChanged = TRUE;
    }
    if (pBurrowLinkNext != 0) {
      pBurrowLinkNext->pBurrowLinkPrev = pBurrowLinkPrev;
      bChanged = TRUE;
    }
    dwFlags &= ~0x800;
    pBurrowLinkPrev = 0;
    pBurrowLinkNext = 0;
    if (bChanged)
      status_set_update();
    bChanged = TRUE;
  }
  return bChanged;
}

// 0x434310 (157 B). Walks the cloak list head. pBurrowLinkNext is read before
// the sub_434DA0 call that may unlink the node. EDI is the "anything changed"
// accumulator; the two overlay arms assign it (mov edi,1) while the flag-clear
// arm ORs its result in (or edi,eax).
void CloakUpdate() {
  CUnit *pNode = sgpBurrowLinkHead_g;
  int nChanged = 0;
  if (pNode == 0)
    return;
  while (pNode != 0) {
    CUnit *pNext = pNode->pBurrowLinkNext;
    if (pNode->sub_434DA0()) {
      pNode->sub_41D360(0x112);
      nChanged = 1;
    } else {
      DWORD dwFlags = pNode->dwFlags;
      int nCleared;   // Not initialized here: sharing EAX with the dwFlags load
                      // (zero-init sunk into the else arm) is what matches.
      if ((dwFlags & 0x10) == 0 && pNode->bBackgroundOrderId == 0x6D &&
          pNode->bCloakCount == 1 && (dwFlags & 0x800) != 0) {
        pNode->dwFlags = dwFlags & ~0x800;
        nCleared = 1;
      } else {
        nCleared = 0;
      }
      nChanged |= nCleared;
      if ((pNode->dwFlags & 0x100) == 0) {
        pNode->sub_41D200(0x111);
        nChanged = 1;
      }
    }
    pNode = pNext;
  }
  if (nChanged)
    status_set_update();
}

// 0x434B20 (43 B). The list head is the codec's own scratch slot: the raw unit
// id is pfread straight into it, then converted back to a pointer.
BOOL LoadCloakList(void *pFile) {
  if (!PortSaveReadIds(pFile, &sgpBurrowLinkHead_g, 1))  // Hyperion
    return FALSE;
  sgpBurrowLinkHead_g =
      CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(sgpBurrowLinkHead_g));
  return TRUE;
}

// 0x434E30 (63 B). Mirror of LoadCloakList: GetUnitID into the head slot,
// pfwrite against &sgpBurrowLinkHead_g, then FromId puts the pointer back.
BOOL SaveCloakList(void *pFile) {
  sgpBurrowLinkHead_g =
      reinterpret_cast<CUnit *>((intptr_t)sgpBurrowLinkHead_g->GetUnitID());
  if (!PortSaveWriteIds(pFile, &sgpBurrowLinkHead_g, 1))
    return FALSE;
  sgpBurrowLinkHead_g =
      CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(sgpBurrowLinkHead_g));
  return TRUE;
}

// 0x434C60 (166 B). The Sync* family's shared worker (SyncStasis tail-jmps
// here): stops and disarms the unit, cancels type-specific orders, and
// rebuilds the buttonset.
void CUnit::sub_434C60() {
  if (subunit != 0)
    subunit->dwFlags |= 0x400;
  Stop();
  if (wType == 1 || wType == 0x10 || wType == 0x64 || wType == 0x63 ||
      wType == 0x68)
    CancelNuke();
  if (wType == 0x48 || wType == 0x52)
    OrderInterceptorsReturn();
  if (bOrderId == 0x22 || bOrderId == 0x46)
    bOrderState = 0;
  if (order_unk9[bOrderId] == 0)
    pOrderTarget = 0;
  BreakAnim();
  if ((pSprite->flags.b & 8) != 0 && byte_6AB4C0 > 1)
    sub_4A7B80();
  SetButtonset(0xE4);
  status_set_update();
}

// 0x4348C0 (59 B). The GetCloakingTech ladder again, with different arms. From the
// oracle's own offsets the two forward blocks sit at +0x2F (`mov eax, 0Dh`) and +0x35
// (`mov eax, 0Ah`): the six-type group reaches the later block, 0x0A, and the two-type
// group the earlier one, 0x0D. The default is the fall-through `xor eax, eax`.
WORD CUnit::GetCloakingCost() {
  if (wType == 0x01 || wType == 0x10 || wType == 0x64 || wType == 0x63 ||
      wType == 0x68 || wType == 0x33)
    return 0x0A;
  if (wType == 0x08 || wType == 0x15)
    return 0x0D;
  return 0;
}

// 0x434270 (135 B). GetCloakingTech's ladder is written out here because the oracle emits
// it inline, the eight cmp/jz rungs then `movzx eax, al`; a call would cost the callee's
// whole body. NeedsMoreEnergy and SubtractEnergy are this TU's own bodies and the oracle
// inlines both -- the single `and esi, 800h` shared by the guard and the subtraction is
// the common subexpression of their cheat-bit test.
void CUnit::Cloak() {
  if (dwFlags & 0x100)
    return;

  BYTE bTech;
  if (wType == 0x01 || wType == 0x10 || wType == 0x64 || wType == 0x63 ||
      wType == 0x68 || wType == 0x33)
    bTech = 0x0A;
  else if (wType == 0x08 || wType == 0x15)
    bTech = 0x09;
  else
    bTech = 0x2C;

  // The cost is computed wide and truncated at each use, as the oracle does. A WORD
  // product makes MSVC shift 16-bit, which costs 4 bytes and pushes glCheatBits out of ESI.
  int nCost = techdata_energy_g[bTech] << 8;
  if (NeedsMoreEnergy((WORD)nCost))
    return;
  SubtractEnergy((WORD)nCost);
  SetBackgroundOrder(0x6D);
}

// 0x4343B0 (169 B), `ret 8h`. Looks the overlay up on this unit's sprite and
// falls back to the SUBUNIT's sprite when the primary yields nothing; the
// whole subunit arm (including the flag clear and the order reset) is guarded
// by `subunit != NULL`.
void CUnit::DestroyOverlays(int nFirstImageId, int nLastImageId) {
  CImage *pImage = pSprite->OverlayGetImage(nFirstImageId, nLastImageId);
  if (subunit != NULL) {
    if (pImage == NULL)
      pImage = subunit->pSprite->OverlayGetImage(nFirstImageId, nLastImageId);
    if ((dwFlags & 0x400) == 0 && bLockdownTimer == 0 && bStasisTimer == 0 &&
        bParalyzeTimer == 0) {
      subunit->dwFlags &= ~0x400;
      subunit->SetOrder(gubUnitOrderGuard[subunit->wType]);
    }
  }
  if (pImage != NULL)
    pImage->SetAnimation(1);
  if (gulUnitTypeFlags[wType] & 8)
    sub_4244C0();
  bUnion85 = 0x0F;
}

// Builds a weapon-range rect around this unit's sprite, clamps it against the
// map, and cloaks every eligible friendly unit inside. The clamp is an if/else-if
// in each axis, as the oracle's `jns` over the whole clamp block says. The weapon
// id goes through an int local before GetWeaponRange: read inline, the sprite
// pointer and the left edge swap ECX and EDX.
void CUnit::DispatchCloakNeighbors() {
  mrect_t  rect;
  int nAnyCloaked = 0;
  int nWeapon = gubUnitWeaponAir[wType];
  int nRange = GetWeaponRange(nWeapon);

  rect.left = pSprite->wPosX - nRange;
  rect.right = pSprite->wPosX + nRange;
  rect.top = pSprite->wPosY - nRange;
  rect.bottom = pSprite->wPosY + nRange;

  if (rect.left < 0)
    rect.left = 0;
  else if (rect.right > gwMapPixWidth)
    rect.right = gwMapPixWidth;

  if (rect.top < 0)
    rect.top = 0;
  else if (rect.bottom > gwMapPixHeight)
    rect.bottom = gwMapPixHeight;

  CUnit **ppList = get_units_in_rect(&rect);
  CUnit *pTarget;
  for (CUnit **pp = ppList; (pTarget = *pp) != NULL; ++pp) {
    WORD wTargetType = pTarget->wType;
    if (wTargetType == 0x47)
      continue;
    if (wTargetType == 0x56)
      continue;
    DWORD ulTypeFlags = gulUnitTypeFlags[wTargetType];
    if (ulTypeFlags & 1)
      continue;
    if (pTarget->dwFlags & 0x40000000)
      continue;
    if (ulTypeFlags & 0x800)
      continue;
    if (wTargetType == 0x0E)
      continue;
    if (pTarget->bOrderId == 0xAE)
      continue;
    if (pTarget->bPlayer != bPlayer)
      continue;
    if (!IsWithinTargetRange(nRange, pTarget))
      continue;
    pTarget->sub_4340D0();
    DWORD dwTargetFlags = pTarget->dwFlags;
    if (dwTargetFlags & 0x800)
      continue;
    pTarget->dwFlags = dwTargetFlags | 0x800;
    nAnyCloaked = 1;
  }
  finish_unit_iteration(ppList);
  if (nAnyCloaked)
    status_set_update();
}

// CUnit::sub_435530 -- oracle 0x435530 (133 B). Is this unit close enough to its current
// order target to act on it? Three exits, each a callee's BOOL passed straight through in
// EAX, which makes this function BOOL and not void. With an order target it asks
// sub_421120(range, NULL), and the oracle pushes the NULL first: right-to-left evaluation,
// so the two pushes straddle the GetWeaponRange call. Without one it is a radius test
// against the stored order-target coordinates, widened by this unit's breaking distance.
BOOL CUnit::sub_435530() {
  if (!CanSeeFocusUnit())
    return FALSE;

  if (pOrderTarget != NULL)
    return sub_421120(GetWeaponRange(gubOrderWeapon[bOrderId]), NULL);

  order_id_t bOrder = bOrderId;
  int nBreak = reinterpret_cast<CFlingy *>(this)->BreakingDistance();
  int nY = wOrderTargetY;
  int nX = wOrderTargetX;
  int nRange = GetWeaponRange(gubOrderWeapon[bOrder]) + (nBreak >> 8);
  return reinterpret_cast<CTarget *>(this)->XYInRadius(nRange, nX, nY);
}

// CUnit::sub_435020 -- oracle 0x435020 (135 B), `ret 4h`. The "get into range for the
// spell" step of the click-ability dispatcher: refresh the cached order-target coordinates
// from the target's sprite, ask sub_435530 whether the unit is in range already, then stop
// or start approaching, re-seed the flingy destination and return the in-range answer. The
// oracle re-reads pOrderTarget after the sub_435530 call, so the source must not cache it
// across that call.
BOOL CUnit::sub_435020(int a1) {
  CUnit *pTarget = pOrderTarget;
  if (pTarget != NULL) {
    wOrderTargetX = pTarget->pSprite->wPosX;
    wOrderTargetY = pTarget->pSprite->wPosY;
  }

  BOOL bInRange = sub_435530();
  if (!bInRange) {
    CUnit *pApproach = pOrderTarget;
    if (pApproach != NULL) {
      if (a1 != 0)
        SetMoveTarget_CUnit(pApproach);
      else
        ApproachMoveTarget_CUnit(pApproach);
    } else {
      ApproachMoveTarget_xy(wOrderTargetX, wOrderTargetY);
    }
  } else if ((bMoveFlags & 4) == 0) {
    Stop();
  }

  reinterpret_cast<CFlingy *>(this)->SetDestination(wOrderTargetX,
                                                    wOrderTargetY);
  return bInRange;
}

// CUnit::sub_434B80 -- oracle 0x434B80 (223 B), `ret 10h`. The shared "may I cast this
// spell on my order target?" gate for Hallucinate, MindControl, PsyFeedback and DefMatrix:
// validate the target through sub_434130, publish the energy cost through the
// out-parameter, check that cost against the unit's energy, then either hand off to
// sub_4350B0 to move into range and cast, or play the failure message and end the order.
//
// CUnit::NeedsMoreEnergy is inlined here by /Ob2, so that callee has to stay in this TU.
// a3 is the error-string slot, in and out: the oracle narrows it in place at entry and
// then passes its address as sub_434130's WORD out-parameter, so the addressable object is
// the parameter itself and this frame has no `sub esp` at all. The success exit sets no
// EAX of its own and falls out of the sub_4350B0 call, which is how that callee's BOOL
// return shows here. count_race takes an int: all four of its oracle call sites widen the
// WORD field to 32 bits, which only an int parameter produces.
BOOL CUnit::sub_434B80(int a1, int a2, int a3, WORD *pCost) {
  a3 = (WORD)a3;

  CUnit *pTarget = pOrderTarget;
  if (pTarget != NULL) {
    upos_t wY = pTarget->pSprite->wPosY;
    upos_t wX = pTarget->pSprite->wPosX;
    if (sub_434130(a1, pTarget, 0xE4, PosS(wX), PosS(wY), (WORD *)&a3)) {
      *pCost = (WORD)(techdata_energy_g[(WORD)a1] << 8);
      if (!NeedsMoreEnergy(*pCost))
        return sub_4350B0(a2);

      map_msg_target_self(
          status_get_str((WORD)(0x360 + (BYTE)count_race(wType))), bPlayer);
      EndDispatchOrder();
      return FALSE;
    }
  }

  gamesnd_error(this, status_get_str((WORD)a3), bPlayer);
  EndDispatchOrder();
  return FALSE;
}

// The CRT memcpy, declared by hand rather than through <string.h> so this TU's
// include closure stays inside src117/h -- the CImage.cpp / CSprite.cpp
// convention. /Oi expands a 32-byte constant-size copy inline, which under
// /arch:SSE2 is the two movdqu load/store pairs the oracle emits.
extern "C" void *__cdecl memcpy(void *, const void *, size_t);

// 0x434880 (58 B). Copies the source unit's whole status-effect block over this one's,
// then re-runs the per-effect sync and the movement-speed recompute. The copy is one
// range, not eight fields: the oracle moves [+0x110 .. +0x12F] as two movdqu pairs,
// wTimeToLive through the last crunchy timer inclusive, where field-by-field assignment
// would emit sixteen byte moves. SyncAllStatusEffects is reached with ECX untouched since
// entry, so the oracle has no `mov ecx, esi` before it and does have one before
// ResetMovementSpeed.
void CUnit::CopyStatusEffects(CUnit *pSource) {
  memcpy(&wTimeToLive, &pSource->wTimeToLive,
         offsetof(CUnit, _unmapped_130) - offsetof(CUnit, wTimeToLive));  // Hyperion: the same fields, 32 bytes in 1.17
  SyncAllStatusEffects();
  ResetMovementSpeed();
}

// 0x4351E0 (279 B). The per-tick status-effect driver: nine countdowns, each firing its
// ender on the tick that reaches zero. Two rung shapes, and the difference is in the
// source. Six rungs are `--t; if (t == 0) End();`, where the store's own flags feed the
// branch; Irradiate (+0x118) and BloodBoil (+0x11A) call their Damage member
// unconditionally after the decrement and only then re-test, which leaves the flags dead
// across the call. The tail is a real tail call into CUnit::UpdateCrunchyTimers, and that
// is what pins this function's void return.
void CUnit::SpellUpdate() {
  if (bStasisTimer != 0) {
    --bStasisTimer;
    if (bStasisTimer == 0)
      RemoveStasis();
  }
  if (bStimTimer != 0) {
    --bStimTimer;
    if (bStimTimer == 0)
      ResetMovementSpeed();
  }
  if (bEnsnareTimer != 0) {
    --bEnsnareTimer;
    if (bEnsnareTimer == 0)
      EnsnareEnd();
  }
  if (bDefMatrixTimer != 0) {
    --bDefMatrixTimer;
    if (bDefMatrixTimer == 0)
      DamageDefMatrix(wDefMatrixHitPoints);
  }
  if (bIrradiateTimer != 0) {
    --bIrradiateTimer;
    IrradiateDamage();
    if (bIrradiateTimer == 0)
      IrradiateEnd();
  }
  if (bLockdownTimer != 0) {
    --bLockdownTimer;
    if (bLockdownTimer == 0)
      LockdownEnd();
  }
  if (bParalyzeTimer != 0) {
    --bParalyzeTimer;
    if (bParalyzeTimer == 0)
      ParalyzeEnd();
  }
  if (bBloodBoilTimer != 0) {
    --bBloodBoilTimer;
    BloodBoilDamage();
    if (bBloodBoilTimer == 0)
      BloodBoilEnd();
  }
  if (_unmapped_11B != 0)
    --_unmapped_11B;
  UpdateCrunchyTimers();
}

// 0x433FC0 (257 B). The EMP shockwave's area effect: drain every unit in the splash box,
// kill the ones flagged as energy-only, and skip the caster and its subunit.
//
// Static __fastcall (short, short, CUnit *), not a thiscall: the entry reads both register
// arguments as 16-bit coordinates and never dereferences either, and the caster arrives on
// the stack with the callee popping it (`ret 4h`). 0x5259AA is not a global but
// wpn_innersplash_g[33], weapon 33 being the EMP shockwave, with its index folded in.
//
// The two clamps are an if/else and not two guards, so a box whose left edge is off the
// map never has its right edge clipped; 1.17's behaviour is kept. The null test on the
// sprite comes after the sprite's own dereference: spelling all three uses as
// `pUnit->pSprite` keeps it, where a local would let MSVC delete it. The splash radius
// must not be a named local either -- a short local goes through the frame and turns all
// four coordinate subtractions into 32-bit operations.
void __fastcall CUnit::ApplyEMPLocation(int x, int y, CUnit *pCaster) {
  spos_t nLeft   = x - wpn_innersplash_g[33];
  short nTop    = y - wpn_innersplash_g[33];
  short nRight  = x + wpn_innersplash_g[33];
  short nBottom = y + wpn_innersplash_g[33];
  mrect_t  rect;
  rect.left   = nLeft;
  rect.top    = nTop;
  rect.right  = nRight;
  rect.bottom = nBottom;

  if (rect.left < 0)
    rect.left = 0;
  else if (rect.right > gwMapPixWidth)
    rect.right = PosS(gwMapPixWidth);

  if (rect.top < 0)
    rect.top = 0;
  else if (rect.bottom > gwMapPixHeight)
    rect.bottom = PosS(gwMapPixHeight);

  CUnit **ppList = get_units_in_rect(&rect);
  CUnit **ppIt = ppList;
  CUnit *pUnit = *ppIt;
  while (pUnit != 0) {
    if (pUnit != pCaster && (pCaster == 0 || pUnit != pCaster->subunit)) {
      if (pUnit->dwFlags & 0x40000000) {
        pUnit->Suicide();
      } else if (pUnit->bStasisTimer == 0) {
        pUnit->wEnergy = 0;
        pUnit->lShields = 0;
        if (pUnit->pSprite->flags.b & 8) {
          if (pUnit->pSprite != 0)
            pUnit->pSprite->RedrawHP();
        }
      }
    }
    pUnit = *++ppIt;
  }
  finish_unit_iteration(ppList);
}

// CUnit::AtMoveTarget inlined; the carry-form `? 2 : 1` is its lowering.
static __forceinline BOOL spells_at_move_target(CUnit *pUnit)
{
  CSprite *pSprite = pUnit->pSprite;

  if (pUnit->wMoveTargetX == pSprite->wPosX
      && pUnit->wMoveTargetY == PosU(pSprite->wPosY))
    return (pUnit->dwFlags & 0x80000) ? 2 : 1;

  return 0;
}

// 0x4350B0 (301 B). The spell-order approach step: face the order target, and
// report TRUE once the unit is on top of it and pointed at it.
// BOOL: one exit is `mov eax, 1` and the one caller discards EAX.
// The destination compares cast to WORD; the sprite coordinates go through WORD
// locals, and bUnion21 is read into a local before AngleBetween.
BOOL CUnit::sub_4350B0(int a1) {
  CUnit *pTarget = pOrderTarget;
  if (pTarget == 0) {
    Stop();
    EndDispatchOrder();
    return FALSE;
  }

  wOrderTargetX = pTarget->pSprite->wPosX;
  wOrderTargetY = pTarget->pSprite->wPosY;
  reinterpret_cast<CFlingy *>(this)->SetDestination(wOrderTargetX, wOrderTargetY);

  if (CanSeeFocusUnit() && sub_421120(a1, 0)) {
    Stop();
    CFlingy *pFly = reinterpret_cast<CFlingy *>(this);
    if (PosU(pFly->wDestinationX) != wPositionX ||
        PosU(pFly->wDestinationY) != wPositionY) {
      CSprite *pSpr = pSprite;
      BYTE bDir = bUnion21;
      upos_t wSprY = PosU(pSpr->wPosY);
      upos_t wSprX = pSpr->wPosX;
      int nTurn = bDir - AngleBetween(PosS(wSprX), PosS(wSprY),
                                      pFly->wDestinationX, pFly->wDestinationY);
      if (nTurn < 0)
        nTurn += 0x100;
      if (nTurn > 0x80)
        nTurn = 0x100 - nTurn;
      if (nTurn > 5)
        return FALSE;
    }
    return TRUE;
  }

  if (spells_at_move_target(this) == 2) {
    Stop();
    EndDispatchOrder();
    return FALSE;
  }
  if (pOrderTarget != 0)
    ApproachMoveTarget_CUnit(pOrderTarget);
  else
    ApproachMoveTarget_xy(wOrderTargetX, wOrderTargetY);
  return FALSE;
}

// Maps an order id to the string id shown when a spell has no valid use.
WORD get_spell_err_str(int nOrderId, WORD wUnitType) {
  switch (nOrderId) {
  case 6:
  case 7:
    return 0x36D;
  case 12:
    if (wUnitType == 0x6A)
      return 0;
    return 0x366;
  case 16:
    return 0x381;
  default:
    return 0x36C;
  }
}

// Returns the unit's maximum energy (x256): 250 with its energy upgrade or as a
// hero, 200 otherwise.
int CUnit::GetMaxEnergy() {
  if (gulUnitTypeFlags[wType] & 0x40)
    return 0xFA00;
  switch (wType) {
  case 9:
    if (get_upgrade_level(bPlayer, 0x13))
      return 0xFA00;
    break;
  case 1:
    if (get_upgrade_level(bPlayer, 0x15))
      return 0xFA00;
    break;
  case 8:
    if (get_upgrade_level(bPlayer, 0x16))
      return 0xFA00;
    break;
  case 12:
    if (get_upgrade_level(bPlayer, 0x17))
      return 0xFA00;
    break;
  case 45:
    if (get_upgrade_level(bPlayer, 0x1F))
      return 0xFA00;
    break;
  case 46:
    if (get_upgrade_level(bPlayer, 0x20))
      return 0xFA00;
    break;
  case 67:
    if (get_upgrade_level(bPlayer, 0x28))
      return 0xFA00;
    break;
  case 71:
    if (get_upgrade_level(bPlayer, 0x2C))
      return 0xFA00;
    break;
  case 60:
    if (get_upgrade_level(bPlayer, 0x2F))
      return 0xFA00;
    break;
  case 34:
    if (get_upgrade_level(bPlayer, 0x33))
      return 0xFA00;
    break;
  case 63:
    if (get_upgrade_level(bPlayer, 0x31))
      return 0xFA00;
    break;
  }
  return 0xC800;
}

// Checks whether an order can be cast on a target unit or position, storing the
// refusal string id through pwErrorString when it cannot.
BOOL CUnit::sub_434130(WORD wOrder, CUnit *pTarget, int nUnitType,
                       int nX, int nY, WORD *pwErrorString) {
  switch (wOrder) {
  case 2:
  case 4:
  case 14:
  case 15:
  case 17:
  case 19:
  case 21:
  case 22:
  case 25:
  case 31:
    return TRUE;
  }
  // Hyperion: a mod's tech is cast at a place, its weapon choosing what it hits
  if (ModTechUseTree(wOrder))
    return TRUE;
  if (wOrder == 3) {
    if (mtx_has_flags_px(nX, nY, 0x410000, 0))
      return TRUE;
    if (pwErrorString)
      *pwErrorString = 0x386;
    return FALSE;
  }
  if (!pTarget && (WORD)nUnitType == 0xE4) {
    if (pwErrorString)
      *pwErrorString = 0x36E;
    return FALSE;
  }
  if ((!pTarget || (pTarget->dwFlags & 0x4000000)) && !IsVulnerableType(nUnitType)) {
    if (pwErrorString)
      *pwErrorString = 0x36C;
    return FALSE;
  }
  WORD wError;
  if ((WORD)nUnitType == 0xE4) {
    spell_player_t player;
    player.bPlayer = bPlayer;
    wError = get_target_spell_err_str(wOrder, pTarget, player);
  } else
    wError = get_spell_err_str(wOrder, nUnitType);
  if (wError && pwErrorString)
    *pwErrorString = wError;
  return wError == 0;
}

// Maps an order id and its target to the string id shown when the target is invalid.
// The WORD return keeps case 6's `return 0` a branch; with int it becomes a cmov, and
// with `break` the three 0x36D returns merge into one.
WORD get_target_spell_err_str(int nOrderId, CUnit *pTarget, spell_player_t player) {
  if (pTarget->bStasisTimer)
    return 0x384;
  switch (nOrderId) {
  case 29:
    if (gulUnitTypeFlags[pTarget->wType] & 1)
      return 0x36D;
    if (!pTarget->HasEnergy())
      return 0x532;
    break;
  case 27:
    if (player.bPlayer == pTarget->bPlayer)
      return 0x52F;
    if (gulUnitTypeFlags[pTarget->wType] & 1)
      return 0x36D;
    switch (pTarget->wType) {
    case 13:
    case 35:
    case 36:
    case 59:
    case 73:
    case 85:
    case 97:
      return 0x36C;
    }
    return 0;
  case 20:
    if (pTarget->wType == 0x49)
      return 0x36C;
  case 6:
  case 7:
  case 24:
  case 30:
    if (gulUnitTypeFlags[pTarget->wType] & 1)
      return 0x36D;
    return 0;
  case 12:
    if (!pTarget->IsInfestable())
      return 0x366;
    break;
  case 16:
    if ((gulUnitTypeFlags[pTarget->wType] & 1) || pTarget->bPlayer != player.bPlayer ||
        pTarget->GetRace() || pTarget->wType == 0x23)
      return 0x381;
    break;
  default:
    return 0x36C;
  }
  return 0;
}

// Returns TRUE when the unit's current spell order has nothing valid left to
// hit and should stop.
// x is read at entry and again after the two calls; y is read in both arms so
// that its register is ECX and the tech byte stays in AL.
BOOL CUnit::CanFireSpell() {
  CUnit *pTarget = pOrderTarget;
  order_id_t bOrder = bOrderId;
  weapon_id_t bWeapon = gubOrderWeapon[bOrder];
  int x = wOrderTargetX;
  if (dwUnion134) {
    switch (bOrderId) {
    case 144:
      if (!pTarget || pTarget->bBloodBoilTimer)
        return TRUE;
      break;
    case 146:
      if (pTarget && pTarget->bEnsnareTimer)
        return TRUE;
      break;
    case 143:
      if (!pTarget || pTarget->bIrradiateTimer)
        return TRUE;
      break;
    case 120:
      if (!pTarget || UnitParasite(pTarget).Any())  // Hyperion: every slot
        return TRUE;
      break;
    case 115:
    case 147:
      if (!pTarget || pTarget->IsDisabled())
        return TRUE;
      goto reload;
    case 122:
      if (pTarget) {
        if (!pTarget->HasEnergy() || (BYTE)(pTarget->wEnergy >> 8) < 100) {
          if (!gbUnitHasShields[pTarget->wType] || ((LONG)pTarget->lShields >> 8) < 100)
            return TRUE;
        }
        goto reload;
      }
      break;
    }
    if (0) {
    reload:
      x = wOrderTargetX;
    }
  }
  if (gubOrderAttack[bOrder]) {
    if (!CanTargetWithWeapon(bWeapon, pTarget))
      return TRUE;
  } else {
    tech_id_t bTech = order_techType[bOrder];
    if (TechTypeValid(bTech)) {  // Hyperion: and a mod's past 1.17's rows
      int y;
      if (pTarget)
        y = wOrderTargetY;
      else
        y = wOrderTargetY;
      if (!sub_434130(bTech, pTarget, 0xE4, x, y, NULL))
        return TRUE;
    }
  }
  return FALSE;
}

#ifndef gubOrderSeq
#ifdef OC_DLL_MODE
#define gubOrderSeq (reinterpret_cast<BYTE *>(OC_VA(0x51B8C0u)))
#else
extern BYTE gubOrderSeq[189] OC_UNALIGNED;
#endif
#endif

// Casts a click-targeted spell: on arrival in range with the cooldown clear it
// pays the energy, starts the cooldown and the cast animation. An AI caster
// drops a spell whose target already carries the effect.
void CUnit::DispatchClickAbility() {
  order_id_t bOrder = bOrderId;
  int nOrder = bOrder;
  weapon_id_t bWeapon = gubOrderWeapon[nOrder];
  if (bOrderState == 2) {
    if (bOrderFlags & 2) {
      bOrderFlags &= ~2;
      EndDispatchOrder();
      return;
    }
    CUnit *pTarget = pOrderTarget;
    if (pTarget) {
      wOrderTargetX = pTarget->pSprite->wPosX;
      wOrderTargetY = pTarget->pSprite->wPosY;
      reinterpret_cast<CFlingy *>(this)->SetDestination(PosS(wOrderTargetX), PosS(wOrderTargetY));
    }
    return;
  }
  if (CanFireSpell()) {
    gamesnd_error(this, status_get_str(0x36C), bPlayer);
    Stop();
    EndDispatchOrder();
    return;
  }
  int nCost = get_order_tech_energy_cost(bOrder);
  if (NeedsMoreEnergy(nCost)) {
    map_msg_target_self(status_get_str(count_race(wType) + 0x360), bPlayer);
    EndDispatchOrder();
    return;
  }
  BOOL bReady = sub_435020(bOrderState == 0);
  if (bOrderState == 0)
    bOrderState = 1;
  if (!bReady) {
    if (AtMoveTarget() == 2)
      EndDispatchOrder();
    return;
  }
  if (bUnion57 || !IsTargetInWeaponRange(bWeapon) || (bMoveFlags & 2) || !AtMoveTarget())
    return;
  if (dwUnion134 && pOrderTarget) {
    CUnit *pTarget = pOrderTarget;
    int nAffected = pTarget->bIrradiateTimer;
    // Case order is the oracle's block order; the IsDisabled arm is last.
    switch (bWeapon) {
    case 0x22:
      break;
    case 0x38:
      nAffected |= UnitParasite(pTarget).Any();  // Hyperion: every slot
      break;
    case 0x3A:
      nAffected |= pTarget->bEnsnareTimer;
      break;
    case 0x3C:
      nAffected |= pTarget->bBloodBoilTimer;
      break;
    case 0x6B:
      nAffected |= UnitMyopia(pTarget).Any();  // Hyperion: every slot
      break;
    case 0x20:
    case 0x6C:
      nAffected |= pTarget->IsDisabled();
      break;
    default:
      goto cast;
    }
    if (nAffected) {
      EndDispatchOrder();
      return;
    }
  }
cast:
  SubtractEnergy(nCost);
  bUnion57 = GetWeaponCD(bWeapon) + (net_rand(0x31) & 3) - 1;
  // The volatile read loads the order index before the cooldown store.
  int nSeq = *(volatile int *)&nOrder;
  // The barrier keeps both flag updates after the cooldown store.
  _ReadWriteBarrier();
  bMoveFlags |= 8;
  bOrderFlags &= ~2;
  pSprite->SetAnimation(gubOrderSeq[nSeq], 1);
  bOrderState = 2;
  if (dwUnion134)
    banker_hold_ai_spells(bPlayer);
}
