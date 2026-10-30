// CUnitCarrier.cpp -- the Carrier and Reaver hangar family: the two fighter lists a carrier
// keeps, the order dispatchers that drive them, and the interceptor scatter. Bodies are in
// oracle VA order.
//
// TU membership matters in both directions here. The file holds a tail-call pair --
// CUnit::DispatchCarrierIgnore ends in `jmp CUnit::DispatchCarrier`, and both bodies are
// here -- which is the shape /Ob2 likes to splice, so check both after a change to either.

#include "oc_data.h"  // Hyperion: OC_VA
#include <intrin.h>
#include "../h/CUnitCarrier.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"        // pSprite->bElevationLevel (DispatchCarrier)
#include "../h/rect16.h"         // gUnitBoundBox's element type
#include "../h/CUnitBuild.h"     // ours: ClipToMap(point16_t *, WORD)
#include "../h/CTarget.h"       // ours: XYInRadius (w64b, DispatchReturn)
#include "../h/CUnitTSpells.h"   // ours: net_randbetween
#include "../h/saveload.h"       // ours: get_upgrade_level
#include "../h/CFlingy.h"        // CFlingy::SetDestination + AngleBetween
                                 // (DispatchTrilobyte)
#include "../h/sai_path.h"       // ours: sai_path_compute_distance (sub_41D0B0)
#include "../h/CUnitCombat.h"    // ours: GetVectorComponents (HangarDeployFighter)
#include "../h/net_cmds.h"         // PlayerData (sub_41BFE0)
#include "../gen/globals117.h"
#include "../h/handicap.h"  // Hyperion: a player's handicap

// The globals this TU reaches, dual-spelled, one #ifndef per name: the plain extern arm
// alone leaves the shipped form's baked slots unchecked, and one guard over the whole
// block would go false and undeclare every name under it.
//
// The two array elements this TU folds are spelled as elements rather than coined as
// globals: 0x521051 is gubUnitOrderAttackUnit[0x49] (0x521008 + 73, unit type 73 being the
// Interceptor, whose units.dat attack-unit order it is), and 0x522E80 is
// gUnitBoundBox[0x49].left (0x522C38 + 73*8), with .top, .right and .bottom at
// 0x522E82/84/86 -- the same interceptor row.
#ifdef OC_DLL_MODE
  #ifndef gulUnitTypeFlags
  #define gulUnitTypeFlags (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
  #endif
  #ifndef guwUnitEnergy
  #define guwUnitEnergy (reinterpret_cast<WORD *>(OC_VA(0x520540u)))
  #endif
  #ifndef gubUnitOrderAttackUnit
  #define gubUnitOrderAttackUnit (reinterpret_cast<BYTE *>(OC_VA(0x521008u)))
  #endif
  #ifndef gUnitBoundBox
  #define gUnitBoundBox (reinterpret_cast<rect16_t *>(OC_VA(0x522C38u)))
  #endif
  #ifndef gwMapPixWidth
  #define gwMapPixWidth (*reinterpret_cast<WORD *>(OC_VA(0x543230u)))
  #endif
  #ifndef gwMapPixHeight
  #define gwMapPixHeight (*reinterpret_cast<WORD *>(OC_VA(0x543234u)))
  #endif
#else
extern DWORD    gulUnitTypeFlags[228] OC_UNALIGNED;
extern WORD     guwUnitEnergy[228] OC_UNALIGNED;
extern order_id_t gubUnitOrderAttackUnit[228] OC_UNALIGNED;
extern rect16_t gUnitBoundBox[228] OC_UNALIGNED;
extern upos_t   gwMapPixWidth;
extern upos_t   gwMapPixHeight;
#endif

// sai_path_compute_distance (0x48E9B0), the callee of sub_41D0B0, is declared in
// ../h/sai_path.h, where its derivation lives too.

// CUnit::sub_41BFE0 -- oracle 0x41BFE0 (630 B). The carrier/reaver target
// keeper: validate and chase the current order target, fall back to the next
// candidate, and report whether the unit can attack now.
// GetCarrierReaverAttackOrder_local is an internal copy of
// CUnit::GetCarrierReaverAttackOrder, same bytes, that sub_41BFE0 below calls. It keeps
// EDX live across the call at the second site.
static __declspec(noinline) int GetCarrierReaverAttackOrder_local(CUnit *pUnit)
{
  if (pUnit->wType == 0x53 || pUnit->wType == 0x51)
    return (pUnit->bOrderId == 0x3E) + 0x3D;
  return (pUnit->bOrderId == 0x39) + 0x38;
}

BOOL CUnit::sub_41BFE0(int nRange, int nArg1)
{
  BOOL bAttacking;
  if (bOrderId == 0x39 || bOrderId == 0x3E) {
    bAttacking = 1;
    if (bMoveFlags & 2)
      Stop();
  } else {
    bAttacking = 0;
  }
  CUnit *pTarget = pOrderTarget;
  if (pTarget != NULL) {
    if (!(pTarget->dwFlags & 0x4000000) && CanTarget(pTarget, 1)) {
      wOrderTargetX = pOrderTarget->pSprite->wPosX;
      wOrderTargetY = pOrderTarget->pSprite->wPosY;
      if (CanSeeFocusUnit() && sub_421120(nRange, NULL) &&
          (nArg1 == 0 || sub_41D0B0(nArg1))) {
        Stop();
      } else {
        if ((bAttacking || (dwFlags & 0x8000000)) &&
            (gPlayerData[bPlayer].nType != 1 || !(dwFlags & 8))) {
          if (pOrderTarget == (CUnit *)dwUnion7C)
            dwUnion7C = 0;
          pOrderTarget = NULL;
          return 0;
        }
        if (!SetDestination_xy(wOrderTargetX, wOrderTargetY))
          return 0;
      }
    } else {
      if (pOrderTarget == (CUnit *)dwUnion7C)
        dwUnion7C = 0;
      pOrderTarget = NULL;
    }
  }
  if (bHangarCountBuilding + bHangarCount == 0) {
    Stop();
    EndDispatchOrder();
    return 0;
  }
  if (bUnion54 != 0)
    goto fail;
  // The queue test reads the member and the second test the local, so MSVC
  // keeps the oracle's second `test eax, eax` instead of threading it.
  CUnit *pOld;
  if (pOrderTarget == NULL && pOrderQueueHead != NULL) {
    ManageQueuedOrders();
    return 0;
  }
  pOld = pOrderTarget;
  if (pOld == NULL || (dwFlags & 0x8000000)) {
    ChangeTarget();
    if ((wType == 0x48 || wType == 0x52) && pOrderTarget != pOld)
      OrderInterceptorsAttack();
  }
  if (pOrderTarget != NULL) {
    if (!bAttacking)
      bOrderId = (BYTE)GetCarrierReaverAttackOrder_local(this);
    if (!CanSeeFocusUnit() || !sub_421120(nRange, NULL))
      return 0;
    if (nArg1 != 0 && !sub_41D0B0(nArg1))
      return 0;
    return 1;
  }
  CUnit *pNext;  // Hyperion: set apart from its declaration, which a goto jumps past
  pNext = sub_431B70();
  if (pNext == NULL) {
    if (!bAttacking)
      bOrderId = (BYTE)GetCarrierReaverIgnoreOrder();
    // The barrier keeps this return block apart from the shared fail block.
    _ReadWriteBarrier();
    return 0;
  }
  if (!bAttacking)
    bOrderId = (BYTE)GetCarrierReaverAttackOrder_local(this);
  wOrderTargetX = pNext->pSprite->wPosX;
  wOrderTargetY = pNext->pSprite->wPosY;
  HoldPosition();
  if (nArg1 != 0 && !sub_41D0B0(nArg1))
    goto fail;
  pOrderTarget = pNext;
  return 1;
fail:
  return 0;
}

// CUnit::sub_41C260
//
// The four types are the hangar set and they recur in GetHangarType and GetHangarCapacity
// below: 0x48 Carrier, 0x52 Gantrithor, 0x53 Reaver, 0x51 Warbringer. The chain is written
// in the oracle's order (48, 52, 53, 51), which is source order for an || chain; a switch
// would have emitted them sorted. BOOL and not BYTE: `xor eax, eax / cmp byte ptr
// [ecx+0E2h], 2h / setz al` zeroes the whole register before the setcc, and both callers
// consume 32-bit EAX.
BOOL CUnit::sub_41C260()
{
  if (wType == 0x48 || wType == 0x52 || wType == 0x53 || wType == 0x51)
    return bUnionE2 == 2;
  return 0;
}

// CUnit::DispatchCarrier
//
// The `push 0h` at +0x01 is what proves sub_41BFE0 is a member, and it is the only thing
// that does: MSVC evaluates call arguments right to left, so a trailing constant scalar
// argument is pushed before the call that computes the argument to its left. That hoist is
// unavailable to every free spelling with this ABI -- ECX plus eight callee-cleaned bytes
// needs an 8-byte by-value aggregate or an __int64, and both materialise the whole argument
// after the inner call. The derivation is in src117/h/CUnit.h.
//
// EAX is reused as the zero constant in the failure arm: `cmp dword ptr [esi+134h], eax`
// and `cmp byte ptr [esi+54h], al` are both compares against the 0 that `test eax, eax`
// has just proved. Writing the two guards as literal comparisons against 0 is what lets
// MSVC do that. The polarity of the first guard is the trap here: `jz $+0x56` from +0x22
// lands on the epilogue, so the body continues only when dwUnion134 is non-zero.
void CUnit::DispatchCarrier()
{
  if (!sub_41BFE0(EnemyTrackingRange() << 5, 0)) {
    if (dwUnion134 != 0 && bUnion54 == 0)
      SetOrder(0x9C);
    return;
  }

  CUnit *pFighter = HangarDeployFighter();
  if (pFighter != NULL) {
    CSprite *pFighterSprite = pFighter->pSprite;
    pFighter->lShields = HandicapShields(guwUnitEnergy[pFighter->wType], pFighter->bPlayer) << 8;  // Hyperion: at the owner's handicap
    pFighterSprite->bElevationLevel = pSprite->bElevationLevel - 1;
    pFighter->SetOrder_CUnit(gubUnitOrderAttackUnit[0x49], pOrderTarget);
    bUnion54 = 7;
  }
}

// CUnit::DispatchCarrierAttack
// Two dwFlags bits are saved across the three calls and put back: bit 3 (0x8) and bit 27
// (0x8000000) are read out before OrderInterceptorsAttack and OR-ed back after
// ManageQueuedOrders over `dwFlags & ~0x08000008`. The oracle factors the common shift out
// of the reconstruction, which is MSVC reassociating (b27 << 27) | (b3 << 3).
//
// The field is read twice, which costs four bytes, so both reads go through the
// `dwFlagsVolatile` union arm: the plain member spelling lets MSVC fold the two six-byte
// loads into one load plus `mov ebx, edi`. The loads and the shifts are separate
// statements because the two orders disagree -- the oracle loads bit 3's copy first but
// shifts bit 27's first, and folding each load into its own shift forces one order on both.
// 38h` is arithmetic on the flag and not a ternary. The || arm is the jump target, which
// puts 0x3D on the taken arm and 0x38 on the fall-through.
void CUnit::DispatchCarrierAttack()
{
  CUnit *pTarget = pOrderTarget;
  if (pTarget == NULL) {
    EndDispatchOrder();
    return;
  }

  if (!CanTarget(pTarget, 1)) {
    SetOrder_xy(6, wOrderTargetX, wOrderTargetY);
    return;
  }

  DWORD dwFlags3 = dwFlagsVolatile;
  DWORD dwFlags27 = dwFlagsVolatile;
  DWORD dwBit27 = (dwFlags27 >> 27) & 1;
  DWORD dwBit3 = (dwFlags3 >> 3) & 1;

  OrderInterceptorsAttack();

  int nOrder;
  if (wType == 0x53 || wType == 0x51)
    nOrder = 0x3D + (bOrderId == 0x3E);
  else
    nOrder = 0x38 + (bOrderId == 0x39);

  SetNextOrder_CUnit(nOrder, pOrderTarget);
  ManageQueuedOrders();

  dwFlags = (dwFlags & ~0x08000008) | ((dwBit27 & 1) << 27) | ((dwBit3 & 1) << 3);
}

// CUnit::DispatchCarrierIgnore
//
// The tail `jmp CUnit::DispatchCarrier` is the fall-through arm, so the type test
// is spelled negated: `cmp eax, 53h / jz` and `cmp eax, 51h / jz` both land on the
// Trilobyte block at +0x36, so the carrier arm is what falls through. Spelled
// `if (reaver) {...} else {DispatchCarrier();}` the blocks come out the other way round
// and the shared tail is lost. DispatchCarrier is defined above at 124 B and /Ob2 is
// entitled to inline it here; it does not, because the oracle's own call is a tail jump
// MSVC forms from a plain call in tail position.
void CUnit::DispatchCarrierIgnore()
{
  if (bOrderState == 0) {
    if (!SetDestination_xy(wOrderTargetX, wOrderTargetY))
      return;
    bOrderState = 1;
  }

  if (wType != 0x53 && wType != 0x51) {
    DispatchCarrier();
    return;
  }

  DispatchTrilobyte();
  if (pOrderTarget != NULL)
    bOrderId = 0x3D;
}

// CUnit::DispatchCarrierStop
//
// The list is the deployed one: the walk starts at `this`'s own +0xC4 and advances through
// each fighter's +0xC4 (a carrier's +0xC4 is a head, a fighter's is a link, and both are
// the same dword). `push dword ptr [edi+5Ch]` before `push 32h` is argument order and not
// scheduling: SetOrder_CUnit(nOrderId, pTarget) pushes its target first.
void CUnit::DispatchCarrierStop()
{
  StopUnitMovement();

  for (CUnit *pFighter = pHangarDeployedHead; pFighter != NULL;
       pFighter = pFighter->pHangarNext)
    pFighter->SetOrder(0x45);

  SetOrder_CUnit(0x32, pOrderTarget);
}

// CUnit::DispatchReturn
// The interceptor's "come home" order: fly at the carrier, and once inside 10 pixels of it
// hand the fighter back to the hangar. The null-parent arm is a statement and a bare return,
// a tail jump to CUnit::Suicide. The X read carries a (short) cast and the Y read must not:
// wPosX is WORD and wPosY short. The elevation block re-reads `pHangarParent->pSprite`
// after CTarget::XYInRadius, which MSVC cannot prove left it alone.
void CUnit::DispatchReturn()
{
  if (pHangarParent == NULL) {
    Suicide();
    return;
  }

  int x = PosS(pHangarParent->pSprite->wPosX);
  int y = pHangarParent->pSprite->wPosY;

  if (bOrderState == 0) {
    if (reinterpret_cast<CTarget *>(this)->XYInRadius(0x3C, x, y)) {
      bOrderState = 1;
      if (wType == 0x49)
        pSprite->bElevationLevel = pHangarParent->pSprite->bElevationLevel - 2;
    }
  }

  if (!reinterpret_cast<CTarget *>(this)->XYInRadius(0x0A, x, y)) {
    SetDestination_xy(x, y);
    return;
  }

  pHangarParent->HangarHideFighter(this);
  SetOrder(0x17);
  status_set_update();
}

// CUnit::DispatchTrilobyte
//
// The Reaver twin of DispatchCarrier, and the two call sites together are what prove
// sub_41BFE0's signature (the derivation is in src117/h/CUnit.h): this one passes {r, 2*r}
// where DispatchCarrier passes {r, 0}, which refutes the __int64 spelling on its own.
// `lea eax, [edx+edx*1]` is the second argument evaluated first, off the named range local.
//
// The turn test is unsigned -- `sub ecx, eax / cmp ecx, 1h / jbe`, held in a DWORD -- so a
// scarab more than one facing step off target clears +0x85 and waits instead of being
// launched. The sprite coordinates pass through WORD locals: `movzx` then `movsx` on the
// same value is what a WORD intermediate narrowed to short at the call gives, where
// `(short)pSpr->wPosX` alone folds to a single movsx. bUnion21 is read in its own
// statement, which pins `mov bl, byte ptr [edi+21h]` above the call.
//
// `movzx ecx, byte ptr [0x52105D]` is an array element, not a global: 0x52105D - 0x521008
// == 0x55, and unit type 0x55 is the Scarab, the counterpart of DispatchCarrier's 0x49.
void CUnit::DispatchTrilobyte()
{
  int nRange = EnemyTrackingRange() << 5;
  if (!sub_41BFE0(nRange, nRange * 2)) {
    if (dwUnion134 != 0 && bUnion54 == 0)
      SetOrder(0x9C);
    return;
  }

  CFlingy *pFly = reinterpret_cast<CFlingy *>(this);
  pFly->SetDestination(wOrderTargetX, wOrderTargetY);

  CSprite *pSpr = pSprite;
  BYTE bDir = bUnion21;
  upos_t wSprY = PosU(pSpr->wPosY);
  upos_t wSprX = pSpr->wPosX;
  DWORD dwTurn = bDir - AngleBetween(PosS(wSprX), PosS(wSprY),
                                     pFly->wDestinationX, pFly->wDestinationY);
  if (dwTurn > 1) {
    bUnion85 = 0;
    return;
  }

  if ((dwFlags & 0x8000) == 0 && pHangarDeployedHead == NULL) {
    CUnit *pFighter = HangarDeployFighter();
    if (pFighter != NULL) {
      pFighter->SetOrder_CUnit(gubUnitOrderAttackUnit[0x55], pOrderTarget);
      pFighter->pHangarParent = this;
      bUnion54 = 0x3C;
    }
  }
}

// CUnit::DispatchTrilobyteAttack
//
// DispatchCarrierAttack without the interceptor scatter -- five bytes shorter for the
// missing `call CUnit::OrderInterceptorsAttack` and the `mov ecx, esi` that no longer has
// to serve two paths. Everything else, the two-read flag save included, is the same shape.
//
// The wType read is its own statement here and is not in the carrier twin, and that
// asymmetry is derived: the oracle emits `movzx eax, word ptr [esi+64h]` above the two flag
// loads. In DispatchCarrierAttack the call to OrderInterceptorsAttack sits between them and
// pins the load below it for free; with the call gone MSVC schedules the non-volatile load
// after the volatile ones unless the source reads it first.
void CUnit::DispatchTrilobyteAttack()
{
  CUnit *pTarget = pOrderTarget;
  if (pTarget == NULL) {
    EndDispatchOrder();
    return;
  }

  if (!CanTarget(pTarget, 1)) {
    SetOrder_xy(6, wOrderTargetX, wOrderTargetY);
    return;
  }

  WORD wUnitType = wType;

  DWORD dwFlags3 = dwFlagsVolatile;
  DWORD dwFlags27 = dwFlagsVolatile;
  DWORD dwBit27 = (dwFlags27 >> 27) & 1;
  DWORD dwBit3 = (dwFlags3 >> 3) & 1;

  int nOrder;
  if (wUnitType == 0x53 || wUnitType == 0x51)
    nOrder = 0x3D + (bOrderId == 0x3E);
  else
    nOrder = 0x38 + (bOrderId == 0x39);

  SetNextOrder_CUnit(nOrder, pOrderTarget);
  ManageQueuedOrders();

  dwFlags = (dwFlags & ~0x08000008) | ((dwBit27 & 1) << 27) | ((dwBit3 & 1) << 3);
}

// CUnit::DispatchTrilobyteStop
//
// DispatchCarrierStop with two different order ids, and the three-byte size difference is
// entirely `push 0A5h`: 0xA5 does not fit an imm8, so it costs five bytes where `push 45h`
// costs two.
void CUnit::DispatchTrilobyteStop()
{
  StopUnitMovement();

  for (CUnit *pFighter = pHangarDeployedHead; pFighter != NULL;
       pFighter = pFighter->pHangarNext)
    pFighter->SetOrder(0xA5);

  SetOrder_CUnit(0x3A, pOrderTarget);
}

// CUnit::FighterDoFcn
//
// Two walks, one shared body, and the count is the loop bound, not the null link: both
// loops are `test bl, bl / jz` on the entry count with `dec bl` inside and the link only
// advanced, where a null-terminated walk would have emitted `test esi, esi`. The counts are
// the two hangar counts (+0xC8 docked, +0xC9 deployed) and the heads their two matching
// list heads (+0xC0, +0xC4).
//
// `this` goes to a stack slot (`mov dword ptr [ebp-4h], ecx`) because ECX is the callback's
// first argument and is clobbered every iteration; the function pointer is reloaded from
// [ebp+8h] each iteration for the same reason, EAX being the callback's result.
BOOL CUnit::FighterDoFcn(TransportProc pfnProc, CUnit *pContext)
{
  BYTE bRemaining = bHangarCount;
  CUnit *pFighter = pHangarDockedHead;
  while (bRemaining != 0) {
    --bRemaining;
    if (pfnProc(pFighter, pContext))
      return 1;
    pFighter = pFighter->pHangarNext;
  }

  bRemaining = bHangarCountBuilding;
  pFighter = pHangarDeployedHead;
  while (bRemaining != 0) {
    --bRemaining;
    if (pfnProc(pFighter, pContext))
      return 1;
    pFighter = pFighter->pHangarNext;
  }

  return 0;
}

// CUnit::FighterReturn
//
// An interceptor heading home: fly at the carrier, re-validating through CanUseAutoTarget
// when the target is gone or has raised the 0x4000000 flag, and falling back to order 0x45.
//
// The first guard is a disjunction, and the oracle proves it with a jump that lands inside
// the other test: a null target skips the flag test and joins the CanUseAutoTarget call,
// while a clear flag skips that call entirely. The shared `SetOrder(0x45); return 0;` is
// one block reached from two places -- two textual copies are what MSVC cross-jumps.
//
// The owner select is two calls at one call site, and that is what buys the branch instead
// of a cmov: written as a value it gives `test ecx, ecx / cmove ecx, esi` and comes out one
// byte short. The `+ 2` has to be inside both arms for the same reason. pSprite is loaded
// twice because the store between the two loads could alias it; pOrderTarget is loaded
// once, since a store to this->wOrderTargetX cannot.
BOOL CUnit::FighterReturn()
{
  if (pOrderTarget == NULL || (pOrderTarget->dwFlags & 0x4000000)) {
    if (!CanUseAutoTarget()) {
      SetOrder(0x45);
      return 0;
    }
  }

  wOrderTargetX = pOrderTarget->pSprite->wPosX;
  wOrderTargetY = pOrderTarget->pSprite->wPosY;

  int nRange;
  if (pHangarParent != NULL)
    nRange = pHangarParent->EnemyTrackingRange() + 2;
  else
    nRange = EnemyTrackingRange() + 2;

  if (pHangarParent != NULL && !pHangarParent->sub_421120(nRange << 5, pOrderTarget)) {
    SetOrder(0x45);
    return 0;
  }
  return 1;
}

// CUnit::GetHangarCapacity
//
// BYTE return, proven from both ends: every exit writes AL alone (`xor al, al`,
// `mov al, 0Ah`, `mov al, cl`) and both callers consume EAX 8-bit, where an int return
// would emit `xor eax, eax` and `mov eax, 0Ah`. The 0x40 bit is tested on
// gulUnitTypeFlags[wType] with wType still in EAX from the entry movzx -- one load feeds
// the four compares and both table indexes, which is what makes the
// `test byte ptr [eax*4+521660h], 40h` form reachable.
//
// Two ternaries, two codegens, one source shape: the Reaver arm (5 against 10) becomes
// `mov ecx, 5h / mov edx, 0Ah / cmovnz ecx, edx` and the Carrier arm (4 against 8) becomes
// `setnz cl / lea ecx, dword ptr [ecx*4+4h]`, because 4 and 8 are an affine pair and 5 and
// 10 are not. Nothing in the source distinguishes them.
BYTE CUnit::GetHangarCapacity()
{
  if (wType == 0x48 || wType == 0x52) {
    if (gulUnitTypeFlags[wType] & 0x40)
      return 8;
    return get_upgrade_level(bPlayer, 0x2B) ? 8 : 4;
  }

  if (wType == 0x53 || wType == 0x51) {
    if (gulUnitTypeFlags[wType] & 0x40)
      return 10;
    return get_upgrade_level(bPlayer, 0x24) ? 10 : 5;
  }

  return 0;
}

// CUnit::GetHangarType
//
// Carrier (0x48) and Gantrithor (0x52) build Interceptors (0x49); Reaver (0x53) and
// Warbringer (0x51) build Scarabs (0x55). The `mov eax, 49h` is not evidence against the
// WORD return: CUnit.h declares it WORD on the callers' evidence (7 of 8 do
// `movzx eax, ax`), and a WORD-returning function is free to write the whole register,
// MSVC preferring `mov eax, imm32` to the 0x66-prefixed `mov ax, imm16` at equal cost.
WORD CUnit::GetHangarType()
{
  if (wType == 0x48 || wType == 0x52)
    return 0x49;
  if (wType == 0x53 || wType == 0x51)
    return 0x55;
  return 0;
}

// CUnit::HangarConstructing
//
// How many of the five build-queue slots hold a fighter -- Interceptor (0x49) or Scarab
// (0x55). The walk runs downward: `add ecx, 0A2h` puts the cursor one element past
// wBuildQueue (0x98 + 5*2 == 0xA2, which is &wBuildQueue[5]), and each iteration reads
// `[ecx-2h]` and then `lea ecx, [ecx-2h]`.
// It must be a real loop: a downward index loop over wBuildQueue[i] is fully unrolled by
// /Ox into five straight-line copies, the trip count and every address being compile-time
// constants. The pointer walk with its own counter keeps the loop, and MSVC then aligns
// the loop head with the two-byte `mov edi, edi` at +0x0E -- padding, not a move.
//
// Declaration order is the register assignment: declaring the counter first lets MSVC
// advance `this` in place, which is the oracle's `add ecx, 0A2h` and is legal only because
// `this` is dead afterwards. BL holds the count, which is what makes the BYTE local and
// the BYTE return honest.
BYTE CUnit::HangarConstructing()
{
  BYTE bCount = 0;
  int nSlots = 5;
  WORD *pSlot = &wBuildQueue[5];
  do {
    WORD wQueued = *--pSlot;
    --nSlots;
    if (wQueued == 0x49 || wQueued == 0x55)
      ++bCount;
  } while (nSlots != 0);
  return bCount;
}

// CUnit::HangarConstructionFinished
//
// The head is read twice on purpose: `mov eax, dword ptr [edi+0C0h]` appears at +0x11 and
// again at +0x27, either side of the two stores through the other pointer, because `this`
// and pUnit are both CUnit * and MSVC cannot prove they do not alias. Caching the head in
// a local removes the second load.
void CUnit::HangarConstructionFinished(CUnit *pUnit)
{
  pUnit->Hide();

  pUnit->pHangarNext = pHangarDockedHead;
  pUnit->pHangarPrev = NULL;
  if (pHangarDockedHead != NULL)
    pHangarDockedHead->pHangarPrev = pUnit;

  ++bHangarCount;
  pHangarDockedHead = pUnit;
  pUnit->_unmapped_CC[0] = 0;
}

// CUnit::HangarHideFighter
// `this` is the carrier and the parameter the fighter. It unlinks the fighter from the
// deployed list (+0xC4 head, +0xC9 count), links it onto the docked list (+0xC0 head, +0xC8
// count), clears the +0xCC "is deployed" flag and hides the sprite. The second half is
// HangarConstructionFinished's body, with the same double read of the docked head, since
// the two pointers may alias.
void CUnit::HangarHideFighter(CUnit *pFighter)
{
  if (pFighter->pHangarPrev != NULL)
    pFighter->pHangarPrev->pHangarNext = pFighter->pHangarNext;
  else
    pHangarDeployedHead = pFighter->pHangarNext;
  if (pFighter->pHangarNext != NULL)
    pFighter->pHangarNext->pHangarPrev = pFighter->pHangarPrev;
  --bHangarCountBuilding;

  pFighter->pHangarNext = pHangarDockedHead;
  pFighter->pHangarPrev = NULL;
  if (pHangarDockedHead != NULL)
    pHangarDockedHead->pHangarPrev = pFighter;
  ++bHangarCount;
  pHangarDockedHead = pFighter;
  pFighter->_unmapped_CC[0] = 0;
  pFighter->Hide();
}

// CUnit::HangarSelfDestruct
// The carrier is dying: docked fighters die with it, deployed ones are cut loose with a
// short fuse, net_randbetween(15, 45, 39), which only shortens an existing timer; Scarabs
// (0x55) are exempt. Both loops are count-bounded and decrement the counts in memory.
// wTimeToLive and the local are WORD. pHangarPrev is stored first in the source, which
// gives 1.17's register allocation; MSVC reorders the two stores itself.
void CUnit::HangarSelfDestruct()
{
  while (bHangarCount != 0) {
    CUnit *pFighter = pHangarDockedHead;
    pHangarDockedHead = pFighter->pHangarNext;
    pFighter->pHangarParent = NULL;
    pFighter->Suicide();
    --bHangarCount;
  }

  CUnit *pFighter = pHangarDeployedHead;
  while (bHangarCountBuilding != 0) {
    pFighter->pHangarParent = NULL;
    if (pFighter->wType != 0x55) {
      WORD wTtl = (WORD)net_randbetween(15, 45, 39);
      if (pFighter->wTimeToLive == 0 || wTtl < pFighter->wTimeToLive)
        pFighter->wTimeToLive = wTtl;
    }
    CUnit *pDone = pFighter;
    pFighter = pDone->pHangarNext;
    pDone->pHangarPrev = NULL;
    pDone->pHangarNext = NULL;
    --bHangarCountBuilding;
  }
  pHangarDeployedHead = NULL;
}

// CUnit::HoldPosition
//
// Not CUnit::DispatchHoldPosition. Sets one dwFlags bit on the unit and, if it
// has one, on its subunit -- the turret follows the hull.
void CUnit::HoldPosition()
{
  dwFlags |= 0x8000000;
  if (subunit != NULL)
    subunit->dwFlags |= 0x8000000;
}

// CUnit::OrderInterceptorsReturn
//
// The same deployed-list walk DispatchCarrierStop opens with, standing alone. The
// `jmp $+0x3 / lea ecx, dword ptr [ecx]` at +0x0B is alignment padding MSVC inserts to land
// the loop head on a 16-byte boundary, emitted by the loop and not by the source, and it is
// why this body is 37 B and not 34.
void CUnit::OrderInterceptorsReturn()
{
  for (CUnit *pFighter = pHangarDeployedHead; pFighter != NULL;
       pFighter = pFighter->pHangarNext)
    pFighter->SetOrder(0x45);
}

// CUnit::sub_41CFE0
// A fighter takes itself out of whichever of its carrier's two lists it is in: +0xC0 is its
// parent carrier, +0xC4/+0xC8 its own links, and the +0xCC byte picks the list, non-zero the
// deployed one (+0xC4 head, +0xC9 count), zero the docked one (+0xC0 head, +0xC8 count).
// The body has no local, and the orphan arm's two plain `= NULL` stores let MSVC store the
// register just proved zero; both give the reloads 1.17 has.
void CUnit::sub_41CFE0()
{
  if (pHangarParent == NULL) {
    pHangarNext = NULL;
    pHangarPrev = NULL;
    return;
  }

  if (pHangarPrev != NULL)
    pHangarPrev->pHangarNext = pHangarNext;
  else if (_unmapped_CC[0] != 0)
    pHangarParent->pHangarDeployedHead = pHangarNext;
  else
    pHangarParent->pHangarDockedHead = pHangarNext;

  if (_unmapped_CC[0] != 0)
    --pHangarParent->bHangarCountBuilding;
  else
    --pHangarParent->bHangarCount;

  if (pHangarNext != NULL)
    pHangarNext->pHangarPrev = pHangarPrev;
}

// CUnit::OrderInterceptorsAttack
//
// The deployed-list walk again (+0xC4 head, +0xC4 link), this time with a body: each
// fighter is pushed one elevation level below its carrier and re-ordered onto the carrier's
// own target.
//
// The elevation is 8-bit arithmetic: `mov cl, byte ptr [eax+0Dh] / dec cl / mov byte ptr
// [eax+0Dh], cl` never widens, which is what a BYTE-to-BYTE assignment of
// `pSprite->bElevationLevel - 1` gives, and the carrier's sprite is re-read every iteration
// because `this` and the fighter are both CUnit *. `movzx eax, byte ptr [eax+521008h]` is
// an array index and not a global -- gubUnitOrderAttackUnit[pFighter->wType], the same
// table DispatchCarrier folds to element 0x49, but with a variable index the base survives.
// `mov edi, edi` at +0x0F is alignment padding for the loop head.
void CUnit::OrderInterceptorsAttack()
{
  for (CUnit *pFighter = pHangarDeployedHead; pFighter != NULL;
       pFighter = pFighter->pHangarNext) {
    pFighter->pSprite->bElevationLevel = pSprite->bElevationLevel - 1;
    pFighter->SetOrder_CUnit(gubUnitOrderAttackUnit[pFighter->wType],
                             pOrderTarget);
  }
}

// CUnit::sub_41D0B0
//
// Is the order target closer than nRange, by path rather than by line?
//
// The argument widths are the callee's, not this body's: the oracle hands
// sai_path_compute_distance a 16-bit EDX (`mov dx, word ptr [ecx+58h]`, a partial register
// write only a 16-bit parameter can produce) and a dword-slot push widened with movzx from
// the WORD field, and that callee's own entry re-narrows both, so both are signed shorts.
// `setnle` is a signed `>`: `cmp dword ptr [ebp+8h], eax / setnle cl` is nRange > distance
// with both operands 32-bit signed, and the `xor ecx, ecx` before the compare plus
// `mov eax, ecx` after is the full-width BOOL all three callers consume.
BOOL CUnit::sub_41D0B0(int nRange)
{
  return nRange > sai_path_compute_distance(this, wOrderTargetX, wOrderTargetY);
}

// CUnit::GetCarrierReaverAttackOrder
//
// Picks the attack order for the hangar owner: the Reaver pair (0x53 Reaver, 0x51
// Warbringer) gets 0x3D/0x3E, everything else 0x38/0x39, and the low bit is "am I already
// in the second of the pair". That is why both arms are `xor eax, eax / cmp byte ptr
// [ecx+4Dh], K / setz al / add eax, K-1` -- a boolean added to the base order, not a
// ternary between two constants.
//
// The Reaver arm is the if and the carrier arm the fall-through, which puts the 0x38 block
// inline at +0x0A and the 0x3D block in the out-of-line tail both jzs target. The type
// chain is in the oracle's order (0x53 then 0x51) as an || chain. The return width is not
// proven by this body -- it is the same 40 bytes declared BYTE and declared int -- and it
// keeps the header's BYTE, which rests on both callers doing `mov byte ptr [esi+4Dh], al`.
int CUnit::GetCarrierReaverAttackOrder()
{
  if (wType == 0x53 || wType == 0x51)
    return (bOrderId == 0x3E) + 0x3D;
  return (bOrderId == 0x39) + 0x38;
}

// sub_41D100
//
// Scatter, mirror, clip, go. Each axis gets an independent jitter of
// net_randbetween(-127, 128) * bScale; if the jittered coordinate would leave the
// interceptor's own map-usable band the jitter is negated rather than clamped, so the
// scatter reflects off the edge. ClipToMap then clamps and the result is the destination.
//
// The interceptor row is folded into the addresses: `word ptr [0x522E80]` is not a global
// but gUnitBoundBox[0x49].left, since 0x522E80 - 0x522C38 == 0x248 == 73 * 8 and 73 is
// the Interceptor, the same 0x49 as ClipToMap's second argument. The bound-box members
// come in with movsx, signed as rect16.h derives, while the map extents come in movzx.
//
// The 32-bit test value must not be a named local, and that is this body's whole lever.
// `int nY = nPtY + nJitterY;` gives the same 189 bytes with ESI and EBX exchanged for the
// whole second half; writing the sum inline, reading the 16-bit local twice, is the
// oracle's shape. The X axis wants the opposite treatment in the same body.
void sub_41D100(CUnit *pUnit, BYTE bScale, mpoint_t pt)
{
  int nJitterX = net_randbetween(-127, 128, 11) * bScale;
  int nJitterY = net_randbetween(-127, 128, 11) * bScale;

  int nX = pt.x + nJitterX;
  if (nX < gUnitBoundBox[0x49].left ||
      nX >= gwMapPixWidth - gUnitBoundBox[0x49].right)
    nJitterX = -nJitterX;

  spos_t nPtY = pt.y;
  if (nPtY + nJitterY < gUnitBoundBox[0x49].top ||
      nPtY + nJitterY >= gwMapPixHeight - gUnitBoundBox[0x49].bottom)
    nJitterY = -nJitterY;

  pt.x = PosS(pt.x + nJitterX);
  nPtY = PosS(nPtY + nJitterY);
  pt.y = nPtY;

  ClipToMap(&pt, 0x49);
  pUnit->SetDestination_xy(pt.x, pt.y);
}

// CUnit::GetCarrierReaverIgnoreOrder
// The return is int, not BYTE: a BYTE return lets MSVC reuse the compared register in the
// first arm and comes out 3 bytes short. Both arms are plain ternaries: the Reaver arm's
// 0x3A/0x3E is an affine pair (setz and lea), the other's 0x32/0x39 becomes a cmov.
int CUnit::GetCarrierReaverIgnoreOrder()
{
  if (bOrderId == 0x37)
    return 0x37;

  if (wType == 0x53 || wType == 0x51)
    return (bOrderId == 0x3E) ? 0x3E : 0x3A;

  return (bOrderId == 0x39) ? 0x39 : 0x32;
}

// Forwards to CTarget::XYInRadius. Passing the coordinates through a helper
// makes MSVC load both before `mov ecx, esi`, as the oracle does.
static __forceinline BOOL target_in_radius(CTarget *pTarget, int nRadius,
                                           int x, int y) {
  return pTarget->XYInRadius(nRadius, x, y);
}

// An interceptor's strafing run: launch from the carrier, fly past the target
// point, turn back, and return when shields drop below a quarter.
void CUnit::DispatchStrafeUnit() {
  if (pHangarParent && (int)lShields < (int)((HandicapShields(guwUnitEnergy[0x49], bPlayer) << 8) >> 2)) {  // Hyperion: at the owner's handicap
    SetOrder(0x45);
    return;
  }
  if (!FighterReturn())
    return;
  switch (bOrderState) {
  case 0:
    if (!pHangarParent) {
      Suicide();
      return;
    }
    gamesnd_play(0x268, this, 1, 0);
    // The order target pair is passed as one point16_t, `push dword ptr [esi+58h]`.
    sub_41D100(this, 3, orderTargetPos);
    bUnion54 = 0xF;
    bOrderState = 1;
  case 1:
    if (bUnion54)
      return;
    pSprite->bElevationLevel += 2;
    bOrderState = 3;
  case 3:
    if (IsUnitInWeaponRange(NULL))
      sub_41F630(5);
    if (sub_421120(0x32, NULL)) {
      sub_41D100(this, 1, orderTargetPos);
      bOrderState = 4;
      return;
    }
    SetDestination_xy(wOrderTargetX, wOrderTargetY);
    return;
  case 4:
    if (target_in_radius(reinterpret_cast<CTarget *>(this), 0x32,
                         PosS(wMoveTargetX), PosS(wMoveTargetY))) {
      sub_41D100(this, 2, orderTargetPos);
      bOrderState = 5;
    }
    return;
  case 5:
    if (target_in_radius(reinterpret_cast<CTarget *>(this), 0x32,
                         PosS(wMoveTargetX), PosS(wMoveTargetY)) &&
        SetDestination_xy(wOrderTargetX, wOrderTargetY))
      bOrderState = 3;
    return;
  }
}

// A scarab's run: fly at the target (or its point) with a 90-tick fuse, then
// detonate on arrival.
void CUnit::DispatchTrilobyteStrafeUnit() {
  switch (bOrderState) {
  case 0: {
    WORD wMax = 0x5A;
    if (wTimeToLive == 0 || wMax < wTimeToLive)
      wTimeToLive = wMax;
    dwFlags |= 0x200000;
    bUnion54 = 7;
    if (pOrderTarget)
      SetMoveTarget_CUnit(pOrderTarget);
    else if (!SetDestination_xy(wOrderTargetX, wOrderTargetY))
      break;
    bOrderState = 2;
    break;
  }
  case 2:
    if (!bUnion54) {
      dwFlags &= ~0x200000;
      bOrderState = 3;
    }
  case 3: {
    BOOL bArrived;
    if (pOrderTarget) {
      if (IsWithinTargetRange(0x100, pOrderTarget) && (pOrderTarget->bMoveFlags & 2))
        reinterpret_cast<CFlingy *>(this)->InterceptTarget(pOrderTarget);
      else
        ApproachMoveTarget_CUnit(pOrderTarget);
      bArrived = IsWithinTargetRange(wpn_innersplash_g[gubUnitWeaponGnd[wType]] >> 1, pOrderTarget);
    } else {
      bArrived = AtMoveTarget();
    }
    if (!bArrived)
      break;
    wOrderTargetX = pSprite->wPosX;
    wOrderTargetY = pSprite->wPosY;
    pSprite->SetAnimation(0xD, 1);
    bOrderState = 6;
    return;
  }
  case 6:
    if (bOrderFlags & 1) {
      dwFlags |= 0x80000000;
      Suicide();
    }
    return;
  }
}

// Builds the next queued interceptor or scarab inside the hangar, and once the
// queue is empty tops up damaged docked interceptors.
void CUnit::DispatchTrainFighter() {
  if (wType != 0x48 && wType != 0x52 && wType != 0x53 && wType != 0x51)
    return;
  switch (bUnionE2) {
  case 0:
  case 1: {
    WORD wQueued = wBuildQueue[bBuildQueueSlot];
    if (wQueued == 0xE4) {
      bUnionE2 = 3;
      dwUnionEC = 0;
      return;
    }
    CUnit *pBuilt = sub_41BC30(wQueued, bUnionE2 == 0);
    dwUnionEC = reinterpret_cast<DWORD_PTR>(pBuilt);
    if (!pBuilt) {
      bUnionE2 = 1;
      return;
    }
    pBuilt->pHangarParent = this;
    bUnionE2 = 2;
    return;
  }
  case 2: {
    CUnit *pBuilt = reinterpret_cast<CUnit *>(dwUnionEC);
    if (pBuilt) {
      pBuilt->Build(pBuilt->GetBuildHpGain(), 0);
      if (!(reinterpret_cast<CUnit *>(dwUnionEC)->dwFlags & 1))
        return;
      HangarConstructionFinished(reinterpret_cast<CUnit *>(dwUnionEC));
      wBuildQueue[bBuildQueueSlot] = 0xE4;
      if (++bBuildQueueSlot >= 5)
        bBuildQueueSlot = 0;
    }
    status_set_update();
    dwUnionEC = 0;
    bUnionE2 = 0;
    return;
  }
  case 3:
    if (wType == 0x53 || wType == 0x51) {
      bUnionE2 = 4;
      return;
    }
    for (CUnit *pFighter = pHangarDockedHead; pFighter; pFighter = pFighter->pHangarNext) {
      if (pFighter->lHitPoints < HandicapLife(gxUnitLife[0x49], pFighter->bPlayer)) {  // Hyperion: at the owner's handicap
        pFighter->ModifyLife(0x80);
        return;
      }
    }
    return;
  }
}

// Launches the healthiest completed fighter docked in this hangar: unlinks it
// from the docked list, pushes it on the deployed list and shows it at the
// carrier (a scarab 25 pixels ahead). Returns NULL when none can launch. x is
// added before y, which gives dx 1.17's frame slot.
CUnit *CUnit::HangarDeployFighter() {
  CUnit *pFighter;
  for (pFighter = pHangarDockedHead; pFighter; pFighter = pFighter->pHangarNext) {
    if (pFighter->dwFlags & 1)
      break;
  }
  if (!pFighter)
    return NULL;
  LONG lLife = pFighter->lHitPoints;
  for (CUnit *pOther = pFighter->pHangarNext; pOther; pOther = pOther->pHangarNext) {
    if ((pOther->dwFlags & 1) && pOther->lHitPoints > lLife)
      pFighter = pOther;
  }
  if (pFighter->wType == 0x49 && pFighter->lHitPoints < HandicapLife(gxUnitLife[0x49], pFighter->bPlayer) / 2)  // Hyperion: at the owner's handicap
    return NULL;
  mpoint_t  pos;
  pos.x = pSprite->wPosX;
  pos.y = pSprite->wPosY;
  if (pFighter->wType == 0x55) {
    int dx;
    int dy;
    reinterpret_cast<CFlingy *>(this)->GetVectorComponents(bUnion21, 0x19, &dx, &dy);
    pos.x += dx;
    pos.y += dy;
    reinterpret_cast<CFlingy *>(pFighter)->FaceDirection(bUnion21);
  }
  status_set_update();
  if (pHangarDockedHead == pFighter)
    pHangarDockedHead = pFighter->pHangarNext;
  if (pFighter->pHangarNext)
    pFighter->pHangarNext->pHangarPrev = pFighter->pHangarPrev;
  if (pFighter->pHangarPrev)
    pFighter->pHangarPrev->pHangarNext = pFighter->pHangarNext;
  bHangarCount--;
  pFighter->pHangarNext = pHangarDeployedHead;
  pFighter->pHangarPrev = NULL;
  if (pHangarDeployedHead)
    pHangarDeployedHead->pHangarPrev = pFighter;
  bHangarCountBuilding++;
  pHangarDeployedHead = pFighter;
  pFighter->SetUnitPosition(pos.x, pos.y);
  pFighter->Show();
  pFighter->_unmapped_CC[0] = 1;
  return pFighter;
}
