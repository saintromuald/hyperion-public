// CUnitProtoss.cpp -- the protoss psi ("juice") power grid, the shield battery recharge
// order, the Archon and Dark Archon merge, and the transport-warp helpers that ride with
// them. Bodies are in oracle VA order.
//
// TU membership matters in both directions here, and this TU holds a callee the oracle
// inlines in one caller and calls for real in another: CUnit::CreatePowerTemplate (155 B)
// is a real call from ShowPowerTemplates and is emitted inline inside init_juice_matrix.
// Both shapes have to survive in one TU, so check both after a change to either.

// CBullet.h declares find_first_unit_near with int coordinates; this TU calls it with
// WORD ones through the view below, so the header's view is renamed away here.
#define find_first_unit_near sub_442B20_int_view
#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playervision.h"  // Hyperion: the slots past 7
#include "../h/CUnitProtoss.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"        // CSprite::Create/SetMask/SetAnimation, +0x0E
#include "../h/CImage.h"         // ours: GetJuiceFrame, frame_clickable_at
#include "../h/grp.h"            // FRAME::bWidth/bHeight by VALUE -- CImage.h only
                                 // forward-declares FRAME
#include "../h/CBullet.h"        // CBullet::RemoveTargetReferences (sub_430390)
#include "../h/CFlingy.h"        // CFlingy::SetPosition256 (sub_430430)
#include "../h/CTarget.h"        // ours: veclen -- extern "C" @veclen@16
#include "../h/CUnitTerran.h"    // ours: status_set_update, spelled __fastcall
                                 // exactly as four existing headers spell it
#include "../h/CUnitBuild.h"     // ours: gamesnd_create_man, one spelling only
#include "../h/Storm.h"          // SMemAlloc / SMemFree
#include "../h/net_cmds.h"       // ours: PlayerData -- gPlayerData's element type.
                                 // It must be complete before globals117.h, which
                                 // declares the array as an eager extern in one arm and
                                 // a lazy #define in the other.
#include <stdlib.h>              // abs() -- an MSVC intrinsic under /Oi (implied by
                                 // /Ox), lowered inline to `cdq / xor / sub`
#include "../h/CThingy.h"        // ours: CThingy::AllocThingy / UpdateVisibility
#include "../h/CUnitZSpells.h"   // ours: FindPlaceForUnit
#include "../gen/globals117.h"
#undef find_first_unit_near
#include "../h/count.h"          // count_race
#include "../h/order.h"          // status_get_str, map_msg_target_self
#include "../h/gamesnd.h"        // gamesnd_play_mtx, snd_pos_t
#include "../h/CUnitTSpells.h"   // gamesnd_play, CUnitTSpellsFindProc
#include "../h/TrigAct.h"        // net_rand
#include "../h/handicap.h"  // Hyperion: a player's handicap

// Hyperion: the definition (find.cpp) reads the position as ints, and this
// TU's callers pass it as 16 bits. The view takes the definition's types, which
// the x64 build needs, and the macro narrows the arguments as 1.17's did
//.
extern "C" CUnit *__fastcall find_first_unit_near(int x, int y, int nRadius,
                                         CUnitTSpellsFindProc pfnFilter,
                                         CUnit *pContext);
#define find_first_unit_near(a0, a1, a2, a3, a4) (find_first_unit_near)(PosU(a0), PosU(a1), (a2), (a3), (a4))

// The globals this TU reaches, dual-spelled, one #ifndef per name: the plain extern arm
// alone leaves the shipped form's baked slots unchecked, and one guard over the whole
// block would go false and undeclare every name under it.
//
// The 0x665B50..0x665B68 block is this TU's own static state (the sheet's `sg` prefix, and
// no body outside this TU touches any of it), so it is spelled here rather than registered.
// The names are the sheet's and the widths come from the oracle's operands.

// 0x665B50 -- the psi matrix. `mov eax, dword ptr [0x665B50]` (whole-dword
// load), SMemAlloc's result, and indexed as BYTEs (`mov byte ptr [eax+esi*1]`,
// `cmp byte ptr [eax+esi*2], 0h`) => a BYTE *. Only this TU reaches it, so the
// proof arm is file-static here; the payload keeps the oracle's storage.
#ifndef sgJuiceMtx_g
#ifdef OC_DLL_MODE
#define sgJuiceMtx_g (*reinterpret_cast<BYTE **>(OC_VA(0x665B50u)))
#elif defined(OC_PAYLOAD)
extern BYTE *sgJuiceMtx_g;
#else
static BYTE *sgJuiceMtx_g;
#endif
#endif

// 0x665B54 -- the head of the pylon psi-link chain. Loaded whole and walked as
// `->pPsiLinkNext`, its nodes dereferenced at +0x4C and +0x0C => CUnit *.
#ifndef sgJuiceLink_g
#ifdef OC_DLL_MODE
#define sgJuiceLink_g (*reinterpret_cast<CUnit **>(OC_VA(0x665B54u)))
#else
extern CUnit *sgJuiceLink_g;
#endif
#endif

// 0x665B58 / 0x665B5C -- the matrix extents. Both are `mov ecx/edx, dword ptr`
// whole-dword, both compared with the signed `jle` / `jl` / `jnl`, and both are
// `sar`-ed (not `shr`) => signed int. File-static here, as above.
#ifndef sgJuiceRangeHorz
#ifdef OC_DLL_MODE
#define sgJuiceRangeHorz (*reinterpret_cast<int *>(OC_VA(0x665B58u)))
#elif defined(OC_PAYLOAD)
extern int sgJuiceRangeHorz;
#else
static int sgJuiceRangeHorz;
#endif
#endif

#ifndef sgJuiceRangeVert
#ifdef OC_DLL_MODE
#define sgJuiceRangeVert (*reinterpret_cast<int *>(OC_VA(0x665B5Cu)))
#elif defined(OC_PAYLOAD)
extern int sgJuiceRangeVert;
#else
static int sgJuiceRangeVert;
#endif
#endif

// 0x665B60 -- "the grid is stale". `mov dword ptr [..], 1h` and
// `cmp dword ptr [..], 0h` => DWORD, never a byte at any of its four sites.
#ifndef sgbNeedsJuiceUpdate_g
#ifdef OC_DLL_MODE
#define sgbNeedsJuiceUpdate_g (*reinterpret_cast<DWORD *>(OC_VA(0x665B60u)))
#else
extern DWORD sgbNeedsJuiceUpdate_g;
#endif
#endif

// 0x665B64 -- "the templates are visible". Same evidence as above: written
// `mov dword ptr [..], 1h` / `0h`, read `cmp dword ptr [..], 0h`.
#ifndef sgbShowJuice_g
#ifdef OC_DLL_MODE
#define sgbShowJuice_g (*reinterpret_cast<DWORD *>(OC_VA(0x665B64u)))
#else
extern DWORD sgbShowJuice_g;
#endif
#endif
// 0x665B68 -- dword_665B68 (sheet row 802, CUnitProtoss.cpp). Set to 1 by
// CUnit::sub_4304D0 after a recall lands and cleared by DispatchTeleport
// before its search, which then tests it => DWORD. File-static here, because
// DispatchTeleport's clear sinks below its argument pushes only for a TU-local.
#ifndef dword_665B68
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define dword_665B68 (*reinterpret_cast<DWORD *>(OC_VA(0x665B68u)))
#else
static DWORD dword_665B68;
#endif
#endif

// 0x520E38 -- the units.dat "order to issue when the unit finishes warping in" column, the
// sibling of gubUnitOrderGuard@0x520F20 and read the same way: `movzx eax, word ptr
// [esi+64h]` for the unbiased wType, then `movzx eax, byte ptr [eax+520E38h]`, a
// scale-free BYTE load off a register index, so this is the table base and not a folded
// element address.
#ifndef gubUnitOrderInitialize
#ifdef OC_DLL_MODE
#define gubUnitOrderInitialize (reinterpret_cast<BYTE *>(OC_VA(0x520E38u)))
#else
extern order_id_t gubUnitOrderInitialize[228] OC_UNALIGNED;
#endif
#endif

// 0x42FFC0 (11 B).
void RequestJuiceUpdate()
{
  sgbNeedsJuiceUpdate_g = 1;
}

// 0x42FFD0 (21 B).
// `cmp byte ptr [ecx+4Dh], 6Ah` puts the field first and the constant second, which is a
// source-steerable choice (`this == arr[i]` emits 0x3B, the reverse 0x39), so both
// compares are spelled field-first.
BOOL CUnit::IsCompletingSummoning()
{
  return this->bOrderId == 0x6A && this->bOrderState == 0;
}

// 0x430780 (31 B).
int do_dispatch_recharge_finder(CUnit *pCandidate, CUnit *pSeeker)
{
  if (pCandidate->CanRechargeShieldsAt(pSeeker))
    pCandidate->SetOrder_CUnit(0x31, pSeeker);
  return 0;
}

// 0x42FD70 (35 B).
// The null store is outside the guard, and that is measured: `jz $+0xF` is taken from the
// end of the two-byte jz, so it clears the fifteen bytes of the four pushes and the call
// and lands on the `mov dword ptr [0x665B50], 0h`, not after it. The store inside the `if`
// costs a jump.
void free_juice_matrix()
{
  BYTE *pMatrix = sgJuiceMtx_g;

  if (pMatrix != NULL)
    SMemFree(pMatrix, "CUnitProtoss.cpp", 102, 0);
  sgJuiceMtx_g = NULL;
}

// 0x42F510 (60 B).
// `movzx eax, byte ptr [0x520F67]` is not a global: it is gubUnitOrderGuard[0x47] with the
// index folded into the address by the compiler (0x520F67 - 0x520F20 == 0x47 == 71, the
// Arbiter's units.dat row).
void CUnit::DispatchArbiter()
{
  if (this->pUnion74 == NULL)
    this->SetOrder(gubUnitOrderGuard[0x47]);
  if (this->dwFlags & 0x40000000) {
    this->EndUninterruptableDispatchOrder();
    return;
  }
  this->SetBackgroundOrder(0x83);
  this->EndUninterruptableDispatchOrder();
}

// 0x430610 (84 B).
// The standard doubly-linked unlink, plus the head fix-up. The oracle clears pPsiLinkPrev
// before testing the head and pPsiLinkNext after, so the two stores are written in that
// order rather than adjacently.
void CUnit::RemoveFromPsiLink()
{
  CUnit *pPrev = this->pPsiLinkPrev;
  if (pPrev != NULL)
    pPrev->pPsiLinkNext = this->pPsiLinkNext;

  CUnit *pNext = this->pPsiLinkNext;
  if (pNext != NULL)
    pNext->pPsiLinkPrev = this->pPsiLinkPrev;

  this->pPsiLinkPrev = NULL;
  if (sgJuiceLink_g == this)
    sgJuiceLink_g = this->pPsiLinkNext;
  this->pPsiLinkNext = NULL;
}

// 0x42F770 (103 B).
// `movzx eax, byte ptr [esi+0E2h] / sub eax, 0h / jz / dec eax / jz / dec eax / jnz` is
// MSVC's dense small-switch lowering, so the source is a switch and not a chain of ifs.
// This is the body that gave +0xE2 the name bShieldBatteryState: it both reads the slot
// and stores 0, 1 and 2 back to it.
void CUnit::DispatchShieldBattery()
{
  switch (this->bShieldBatteryState) {
  case 0:
    if (this->pOrderTarget != NULL) {
      this->pSprite->SetAnimation(0x13, 1);
      this->bShieldBatteryState = 1;
    }
    break;

  case 1:
    // The null arm skips the store, read off the branch target: `test eax, eax / jz $+0xD`
    // clears the cmp, the jz and the `mov dword ptr [esi+5Ch], 0h`, landing on the state
    // store. A flat `if (target == NULL || target->bOrderId != 0x42)` is the same program
    // but writes the null back over a slot already null.
    if (this->pOrderTarget != NULL) {
      if (this->pOrderTarget->bOrderId == 0x42)
        break;
      this->pOrderTarget = NULL;
    }
    this->bShieldBatteryState = 2;
    break;

  case 2:
    // The state machine: 0 idle, 1 charging once a target appears, 2 when the target stops
    // asking, and back to 0 when it is gone or 1 when another arrives. This store is
    // written out and MSVC cross-jumps it into case 0's identical tail.
    if (this->pOrderTarget != NULL) {
      this->bShieldBatteryState = 1;
      break;
    }
    this->pSprite->SetAnimation(0x14, 1);
    this->bShieldBatteryState = 0;
    break;
  }
}

// 0x430320 (108 B).
// gdwLocalMask @0x672668 is read `movzx eax, byte ptr [0x672668]` here even though it is a
// registered DWORD: MSVC narrows the load to feed CSprite::SetMask's BYTE parameter.
void ShowPowerTemplates()
{
  for (CUnit *pUnit = sgJuiceLink_g; pUnit != NULL;
       pUnit = pUnit->pPsiLinkNext) {
    if (gPlayback != 0)
      continue;
    if (pUnit->bPlayer != g_LocalNationID)
      continue;
    if (pUnit->pPowerTemplate == NULL && !pUnit->CreatePowerTemplate())
      continue;
    pUnit->pPowerTemplate->flags.b &= ~0x20;
    pUnit->pPowerTemplate->SetMask((BYTE)gdwLocalMask);
  }
  sgbShowJuice_g = 1;
}

// 0x42FE60 (340 B). Allocates the psi matrix and stamps the juice GRP frame's clickable
// mask into it with four-way mirroring, then converts both extents from cell counts to
// pixels. The loops run over i and j only: the oracle's y, k and x are MSVC's
// strength-reduced induction variables. The matrix and both extents must be file-static --
// as externs, each byte store reloads them.
void cunit_protoss_init()
{
  sgJuiceLink_g = NULL;

  PFRAME pFrame = GetJuiceFrame();

  sgJuiceRangeHorz = (pFrame->bWidth + 15) / 32;
  sgJuiceRangeVert = (pFrame->bHeight + 15) / 32;
  sgJuiceMtx_g = (BYTE *)SMemAlloc(sgJuiceRangeVert * sgJuiceRangeHorz * 4,
                                   "CUnitProtoss.cpp", 74, 0);

  for (int i = 0; i < sgJuiceRangeVert; i++) {
    for (int j = 0; j < sgJuiceRangeHorz; j++) {
      int nClickable = frame_clickable_at(pFrame, j * 32 + 16, i * 32 + 16);
      sgJuiceMtx_g[(2 * i + 1) * sgJuiceRangeHorz + j] = (BYTE)nClickable;
      sgJuiceMtx_g[(2 * i + 1) * sgJuiceRangeHorz - j - 1] = (BYTE)nClickable;

      sgJuiceMtx_g[((2 * sgJuiceRangeVert - i) * 2 - 1) * sgJuiceRangeHorz + j] = (BYTE)nClickable;
      sgJuiceMtx_g[((2 * sgJuiceRangeVert - i) * 2 - 1) * sgJuiceRangeHorz - j - 1] = (BYTE)nClickable;
    }
  }

  sgJuiceRangeVert *= 32;
  sgJuiceRangeHorz *= 32;
  sgbNeedsJuiceUpdate_g = 0;
  sgbShowJuice_g = 0;
}

// 0x42F7E0 (67 B). The shield battery's order interrupt: kill the recharge overlay image,
// then re-issue the type's guard order unless an order is already queued, and hand back to
// the dispatcher. Both guards are read off their branch targets: the first clears only the
// `mov ecx, eax` and the `call CImage::Kill`, so it is a plain null check around the kill,
// while the second clears the whole SetOrder_interrupt set-up and call and lands on the
// shared tail, so the tail is not inside the guard.
void CUnit::DispatchShieldBatteryInterrupt()
{
  CImage *pOverlay = this->pSprite->OverlayGetImage(0x170, 0x172);
  if (pOverlay != NULL)
    pOverlay->Kill();

  if (this->pUnion74 == NULL)
    this->SetOrder_interrupt(gubUnitOrderGuard[this->wType], 0);

  this->EndUninterruptableDispatchOrder();
}

// 0x42F5B0 (129 B). A pylon, or any psi source, coming online: prepend `this` to the
// sgJuiceLink_g chain if it is not on it already, create its power template, mark the grid
// stale, then issue the type's guard order and hand back.
//
// The three guards are one conjunction and not nested arms: all three jumps land on the
// same block, the duplicated SetOrder and EndUninterruptableDispatchOrder tail, which is
// what makes them a flat &&. pPsiLinkPrev is assigned NULL even though the guard just
// proved it null -- `mov dword ptr [esi+0F8h], 0h` is ten real bytes in the oracle, so the
// store is in the source.
void CUnit::DispatchJuiceInit()
{
  CUnit *pHead = sgJuiceLink_g;

  if (pHead == this || this->pPsiLinkNext != NULL ||
      this->pPsiLinkPrev != NULL) {
    this->SetOrder(gubUnitOrderGuard[this->wType]);
    this->EndUninterruptableDispatchOrder();
    return;
  }

  this->pPsiLinkPrev = NULL;
  this->pPsiLinkNext = pHead;
  if (pHead != NULL)
    pHead->pPsiLinkPrev = this;
  sgJuiceLink_g = this;
  this->CreatePowerTemplate();

  WORD wType = this->wType;
  sgbNeedsJuiceUpdate_g = 1;
  this->SetOrder(gubUnitOrderGuard[wType]);
  this->EndUninterruptableDispatchOrder();
}

// 0x42F9E0 (129 B). The tail of a warp-in: state 0 ticks the completion counter and flips
// the sprite to its finished animation, state 1 clears the "still warping" order flag,
// restores the button set and re-issues the type's initialize order. The compare chain is
// MSVC's dense small-switch lowering, so the source is a switch; the case-1 block is the
// fall-through and case 0 is placed last, which the chain forces rather than the order the
// cases are written in.
void CUnit::DispatchSummonComplete()
{
  switch (this->bOrderState) {
  case 0:
    // `jz 0x42FA5F` clears nineteen bytes of animation set-up and the four of
    // `mov byte ptr [esi+4Eh], 1h`, so the state advance is inside the guard.
    if (this->CountDownToCompletion()) {
      this->pSprite->flags.b &= 0x7F;
      this->pSprite->SetAnimation(0, 1);
      this->bOrderState = 1;
    }
    break;

  case 1: {
    // The flag byte is loaded once into AL and the cleared value written back from the same
    // register, which is a named local and not two reads.
    BYTE bFlags = this->bOrderFlags;
    if ((bFlags & 4) == 0)
      break;

    this->dwFlags |= 0x20000;
    this->bOrderFlags = bFlags & ~4;

    this->ResetMovementState();
    this->SetButtonset(this->wType);
    status_set_update();
    gamesnd_create_man(this);
    this->SetOrder_interrupt(gubUnitOrderInitialize[this->wType], 0);
    this->EndUninterruptableDispatchOrder();
    break;
  }
  }
}

// 0x430390 (156 B). Takes this unit off the board: either hide it into the transport list
// (bDetach) or merely force its sprites to redraw, then reset its movement.
//
// The early exit does no work at all -- its jump lands on the bare `pop esi / pop ebp /
// ret 4h` and not on the ResetMovementState that precedes it -- so an already-hidden unit
// returns untouched. The two copies of the reset tail are one source statement: MSVC
// tail-duplicated the join rather than spend a jmp, and then cross-jumped the if-arm's two
// inner early exits into the second copy.
void CUnit::sub_430390(BOOL bDetach)
{
  if (this->pSprite->flags.b & 0x20)
    return;

  this->RemoveTargetReferences(0);
  CBullet::RemoveTargetReferences(this);
  this->UntrackCollision();

  if (bDetach) {
    this->UnloadFromUsedList();
    this->MoveToTransList();

    CSprite *pSprite = this->pSprite;
    pSprite->flags.b |= 0x20;
    pSprite->SetMask(0);

    CUnit *pSub = this->subunit;
    if (pSub != NULL && (gulUnitTypeFlags[pSub->wType] & 0x10)) {
      CSprite *pSubSprite = pSub->pSprite;
      pSubSprite->flags.b |= 0x20;
      pSubSprite->SetMask(0);
    }
  } else {
    this->pSprite->ForceInvalid();

    CUnit *pSub = this->subunit;
    if (pSub != NULL)
      pSub->pSprite->ForceInvalid();
  }

  this->ResetMovementState();
}


// 0x430430 (145 B). The mirror of sub_430390: put the unit back on the board. Snap the
// subunit onto this unit's 24.8 position, un-hide both sprites, move it back from the
// transport list to the used list, and re-establish the collision and circle state.
//
// The +0x2C / +0x30 pair is reached through `reinterpret_cast<CFlingy *>` rather than by
// adding a CUnit field: CUnit.h maps 0x2C..0x4B as `_unmapped_2C[0x20]` and CFlingy.h
// already owns and asserts lPositionX256 and lPositionY256 at those offsets. The last
// block is a tail call on the subunit and not on `this`: ECX holds the subunit at the jump,
// and the `pop esi` is hoisted above the test, so the frame is already unwound.
void CUnit::sub_430430()
{
  CUnit *pSubPos = this->subunit;
  if (pSubPos != NULL && (gulUnitTypeFlags[pSubPos->wType] & 0x10))
    reinterpret_cast<CFlingy *>(pSubPos)->SetPosition256(
        reinterpret_cast<CFlingy *>(this)->lPositionX256,
        reinterpret_cast<CFlingy *>(this)->lPositionY256);

  // The sprite local is declared inside the guard, which buys two bytes: testing and
  // clearing the byte through one expression lets MSVC share the load across the branch
  // (15 bytes), where the oracle keeps the byte in memory at both ends (13).
  if (this->pSprite->flags.b & 0x20) {
    CSprite *pSprite = this->pSprite;
    pSprite->flags.b &= ~0x20;

    CUnit *pSub = this->subunit;
    if (pSub != NULL && (gulUnitTypeFlags[pSub->wType] & 0x10))
      pSub->pSprite->flags.b &= ~0x20;

    this->UnloadFromTransList();
    this->MoveToUsedList();
  }

  this->sub_423260();
  this->CircleUpdate();
  this->TrackCollision();
  this->ResetMovementState();

  CUnit *pSubReset = this->subunit;
  if (pSubReset != NULL && (gulUnitTypeFlags[pSubReset->wType] & 0x10))
    pSubReset->ResetMovementState();
}

// 0x42F270 (155 B). Creates the pylon's psi-field template sprite (sprite id 0x141) at the
// pylon's own position, then decides how it should be masked.
//
// The two coordinates go through `short` locals, and that is measured: the oracle emits a
// movzx on each field and only then a movsx on the register half, the two-step form of
// `short x = this->wPosX;`. Written inline as `(short)pSprite->wPosX` it is a single
// movsx, which is what IsCellPowered below does emit.
BOOL CUnit::CreatePowerTemplate()
{
  CSprite *pSprite = this->pSprite;
  spos_t y = pSprite->wPosY;
  spos_t x = pSprite->wPosX;

  this->pPowerTemplate = CSprite::Create(0x141, x, y, 0);
  if (this->pPowerTemplate == NULL)
    return FALSE;

  this->pPowerTemplate->bElevationLevel = 1;

  if (gPlayback == 0 && this->bPlayer == g_LocalNationID) {
    if (sgbShowJuice_g != 0) {
      this->pPowerTemplate->SetMask((BYTE)gdwLocalMask);
      return TRUE;
    }
    if (this->pSprite->flags.b & 8) {
      ShowPowerTemplates();
      return TRUE;
    }
  }

  // The hide tail names a local where every other site does not, and the six bytes between
  // 155 and 161 are exactly that: spelled `this->pPowerTemplate` twice, MSVC cannot prove
  // the `|=` left the field alone and reloads it for the call.
  CSprite *pTemplate = this->pPowerTemplate;
  pTemplate->flags.b |= 0x20;
  pTemplate->SetMask(0);
  return TRUE;
}

// 0x42FDA0 (191 B). The exact mirror of ShowPowerTemplates: the same walk over the pylon
// psi-link chain, but it creates the template where one is missing, hides it, and clears
// sgbShowJuice_g at the end. (The corpus name says it inits the juice matrix; the body
// never touches sgJuiceMtx_g. The name is kept as the ledger spells it.)
//
// The splice is self-resolving, and the reason is mutual recursion. CreatePowerTemplate is
// emitted inline here, every byte of it down to its own `call ShowPowerTemplates`, while
// ShowPowerTemplates emits a real call -- MSVC cannot inline CreatePowerTemplate into
// ShowPowerTemplates, because CreatePowerTemplate calls it. No inlining directive is used.
void init_juice_matrix()
{
  for (CUnit *pUnit = sgJuiceLink_g; pUnit != NULL;
       pUnit = pUnit->pPsiLinkNext) {
    if (pUnit->pPowerTemplate == NULL && !pUnit->CreatePowerTemplate())
      continue;
    // One load, so one local: the oracle loads once into ECX and ORs through it, and the
    // field spelling costs six bytes. In ShowPowerTemplates the two statements are
    // separated by a read of gdwLocalMask, so the field spelling is right there.
    CSprite *pTemplate = pUnit->pPowerTemplate;
    pTemplate->flags.b |= 0x20;
    pTemplate->SetMask(0);
  }
  sgbShowJuice_g = 0;
}

// 0x430000 (213 B). Whether the cell at (x, y) lies in a psi field of one of bPlayer's
// pylons. Signed `/ 32`, not shifts; the stride multiplies left to right, so the `* 2` is
// the SIB scale. Re-reading wPosX in the horizontal test orders the reloads at the
// loop-continue join.
BOOL IsCellPowered(WORD wType, int x, int y, BYTE bPlayer)
{
  if ((gulUnitTypeFlags[wType] & 0x80000) == 0)
    return TRUE;

  for (CUnit *pUnit = sgJuiceLink_g; pUnit != NULL;
       pUnit = pUnit->pPsiLinkNext) {
    if (bPlayer != pUnit->bPlayer)
      continue;

    CSprite *pSprite = pUnit->pSprite;
    int nPylonX = PosS(pSprite->wPosX);
    if (abs(PosS(pSprite->wPosX) - x) >= sgJuiceRangeHorz)
      continue;

    int nPylonY = pSprite->wPosY;
    if (abs(nPylonY - y) >= sgJuiceRangeVert)
      continue;

    int nRow   = (sgJuiceRangeVert - nPylonY + y) / 32;
    int nWidth = sgJuiceRangeHorz / 32;
    int nCol   = (sgJuiceRangeHorz - nPylonX + x) / 32;

    if (sgJuiceMtx_g[nRow * nWidth * 2 + nCol] != 0)
      return TRUE;
  }
  return FALSE;
}

// 0x430670 (258 B). The grid refresh. While sgbNeedsJuiceUpdate_g is set, walk the used
// list and turn each protoss building's unpowered bit (dwFlags & 0x400) and its warp
// animation on or off, then clear the flag and repaint the status panel.
//
// `cmp byte ptr [eax*4+6726D0h], 3h` is not a global: eax is `bPlayer * 9`, so the scaled
// index is `bPlayer * 36` and 36 is sizeof(PlayerData), while 0x6726D0 is
// `&gPlayerData[0].nType` (gPlayerData @0x6726C8, nType at +0x08). It is one member
// access, `gPlayerData[bPlayer].nType`.
//
// bWasPowered is computed before the call because the oracle computes it there:
// `shr edi, 0Ah / not edi / and edi, 1h` is MSVC's value form of `!(dwFlags & 0x400)`,
// materialised into a callee-saved register ahead of IsCellPowered because both arms read
// it afterwards. The two animation arms are edge-triggered: 0x1B, then 0x13, on unpowered
// to powered, and 0x18 on powered to unpowered.
void UpdateProtossPowerGrid()
{
  if (sgbNeedsJuiceUpdate_g == 0)
    return;

  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL;
       pUnit = pUnit->pNext) {
    if ((pUnit->dwFlags & 2) == 0)
      continue;
    if (pUnit->GetRace() != 2)
      continue;
    if (gPlayerData[pUnit->bPlayer].nType == 3)
      continue;

    DWORD dwFlags = pUnit->dwFlags;
    int bWasPowered = !(dwFlags & 0x400);

    // The test is written negated, which is the block-placement lever and nothing else.
    // `!IsCellPowered(..)` makes the unpowered arm the fall-through, which is where the
    // oracle puts it; spelled positively the two arms are laid out the other way round.
    if (!IsCellPowered(pUnit->wType, PosS(pUnit->pSprite->wPosX),
                       pUnit->pSprite->wPosY, pUnit->bPlayer)) {
      dwFlags |= 0x400;
      pUnit->dwFlags = dwFlags;
      pUnit->BreakAnim();
      if ((pUnit->dwFlags & 1) && bWasPowered)
        pUnit->pSprite->SetAnimation(0x18, 1);
    } else {
      dwFlags &= ~0x400;
      pUnit->dwFlags = dwFlags;
      if ((pUnit->dwFlags & 1) && !bWasPowered) {
        pUnit->pSprite->SetAnimation(0x1B, 1);
        if (pUnit->IsBuildingProtoss() || pUnit->bUpgradeType != 0x3D)
          pUnit->pSprite->SetAnimation(0x13, 1);
      }
    }
  }

  sgbNeedsJuiceUpdate_g = 0;
  status_set_update();
}

// TRUE when this unit may recharge its shields at pTarget (a shield battery). The 134 arm
// reaches the FALSE tail's epilogue with EAX already 1; only the shared bResult return plus
// `goto done` keeps it from merging with the pUnion74 `return TRUE`.
BOOL CUnit::CanRechargeShieldsAt(CUnit *pTarget)
{
  BOOL bResult;
  if (this->bPlayer == pTarget->bPlayer &&
      gbUnitHasShields[this->wType] != 0 &&
      (this->dwFlags & 0x1) &&
      (this->dwFlags & 0x20000) &&
      !(this->dwFlags & 0x2) &&
      this->GetRace() == 2 &&
      (int)this->lShields < (HandicapShields(guwUnitEnergy[this->wType], this->bPlayer) << 8) && // Hyperion: at the owner's handicap
      (pTarget->dwFlags & 0x1) &&
      pTarget->wEnergy != 0 &&
      !pTarget->IsDisabled() &&
      !this->IsFighterType())
  {
    bResult = TRUE;
    if (this->dwUnion134 == 0)
      goto done;
    int nOrderId = this->bOrderId;
    if (nOrderId != 0x42 && nOrderId != 0x60) {
      if (this->pUnion74 == NULL)
        return TRUE;
      return this->GetFirstQueuedOrder() != 0x42;
    }
  }
  bResult = FALSE;
done:
  return bResult;
}

// 0x42F3F0 (285 B). The status-effect merge the two Archon dispatchers run over the unit
// being consumed. Every compare is `jbe`, that is unsigned, which the BYTE and WORD field
// types give for free. The irradiate arm carries its source and player with it -- `jbe
// $+0x1E` clears exactly the thirty bytes of the three stores.
void CUnit::CopyStatus(CUnit *pOther)
{
  if (pOther->wDefMatrixHitPoints > this->wDefMatrixHitPoints)
    this->wDefMatrixHitPoints = pOther->wDefMatrixHitPoints;
  if (pOther->bDefMatrixTimer > this->bDefMatrixTimer)
    this->bDefMatrixTimer = pOther->bDefMatrixTimer;
  if (pOther->bStimTimer > this->bStimTimer)
    this->bStimTimer = pOther->bStimTimer;
  if (pOther->bEnsnareTimer > this->bEnsnareTimer)
    this->bEnsnareTimer = pOther->bEnsnareTimer;
  if (pOther->bLockdownTimer > this->bLockdownTimer)
    this->bLockdownTimer = pOther->bLockdownTimer;
  if (pOther->bParalyzeTimer > this->bParalyzeTimer)
    this->bParalyzeTimer = pOther->bParalyzeTimer;
  if (pOther->bIrradiateTimer > this->bIrradiateTimer) {
    this->bIrradiateTimer = pOther->bIrradiateTimer;
    this->pIrradiateSource = pOther->pIrradiateSource;
    this->bIrradiatePlayer = pOther->bIrradiatePlayer;
  }
  if (pOther->bStasisTimer > this->bStasisTimer)
    this->bStasisTimer = pOther->bStasisTimer;
  if (pOther->bBloodBoilTimer > this->bBloodBoilTimer)
    this->bBloodBoilTimer = pOther->bBloodBoilTimer;

  this->_unmapped_121[0] |= pOther->_unmapped_121[0];
  UnitHxMerge(this, pOther);  // Hyperion: the casters past 7
  this->bUnion123 |= pOther->bUnion123;
  this->bCrunchyCount = pOther->bCrunchyCount;
  memcpy(this->bCrunchyTimers, pOther->bCrunchyTimers, 9);
}

// An external free callee the two merge dispatchers reach. veclen comes from
// ../h/CTarget.h, which can carry the extern "C" its @veclen@16 decoration needs.

// 0x42F830 (427 B). The Archon merge. Five exits, and the shape comes from which arm is
// the early exit.
//
// +0x3C is CFlingy::lTargetSpeed, reached through CUnit's `_unmapped_2C[0x20]` placeholder:
// `mov eax, dword ptr [ebx+3Ch] / add eax, eax / sar eax, 8h` is a full-dword signed read,
// and CFlingy.h already names +0x3C `long lTargetSpeed` with a proven sizeof(CFlingy) ==
// 0x4C, so CUnit's first 0x4C bytes are that object.
void CUnit::DispatchSummonArchon()
{
  CUnit *pTarget = this->pOrderTarget;

  if (pTarget != NULL && pTarget->bPlayer == this->bPlayer &&
      pTarget->bOrderId == 0x69 && pTarget->pOrderTarget == this)
  {
    upos_t wTgtY, wTgtX, wMyY, wMyX;
    DWORD dwDist;

    if (this->dwFlags & 0x100000) {
      LONG lTargetSpeed = *reinterpret_cast<LONG *>(this->_unmapped_2C + 0x10);

      if (this->IsWithinTargetRange((lTargetSpeed * 2) >> 8, pTarget)) {
        this->dwFlags &= ~0x100000;
        this->SetNextOrder(0x96);
      }
    }

    wTgtY = pTarget->pSprite->wPosY;
    wTgtX = pTarget->pSprite->wPosX;
    wMyY = this->pSprite->wPosY;
    wMyX = this->pSprite->wPosX;
    dwDist = veclen(PosS(wMyX), PosS(wTgtX), PosS(wMyY), PosS(wTgtY));
    if (dwDist > 2) {
      if (this->bOrderState != 0 && this->AtMoveTarget() == 2) {
        this->StopUnitMovement();
        this->EndDispatchOrder();
        return;
      }
      this->bOrderState = 1;
      if (dwDist > 0x0A) {
        CSprite *pTgtSprite = pTarget->pSprite;

        this->ApproachMoveTarget_xy(PosS(pTgtSprite->wPosX),
                                    PosS(pTgtSprite->wPosY));
        return;
      }
      {
        CSprite *pMySprite = this->pSprite;
        CSprite *pTgtSprite = pTarget->pSprite;
        mpoint_t  mid;

        mid.x = PosS((PosS(pMySprite->wPosX)
                         + PosS(pTgtSprite->wPosX)) / 2);
        mid.y = PosS((PosS(pMySprite->wPosY)
                         + PosS(pTgtSprite->wPosY)) / 2);
        this->ApproachMoveTarget_xy(mid.x, mid.y);
      }
      return;
    }

    this->CopyStatus(pTarget);
    this->TransformType(0x44);
    this->pSprite->flags.b &= 0x7F;
    this->pSprite->SetAnimation(0x0D, 1);
    this->SetButtonset(0xE4);
    status_set_update();
    this->bKillCount = (BYTE)(this->bKillCount + pTarget->bKillCount);
    pTarget->SelfDestruct();
    this->dwFlags &= ~0x20000;
    this->ResetMovementState();
    this->SetOrder(0x6A);
    return;
  }

  this->StopUnitMovement();
  this->EndDispatchOrder();
}


// 0x42FA70 (451 B). The Dark Archon merge -- the same body as DispatchSummonArchon with
// five differences read off the two listings: the target order is 0xB7, the two distance
// rungs are 0x14 and 0x13 rather than 0x0A and 0x02, TransformType is given 0x3F, an extra
// sub_41D360(0x112) runs right after it, and the merged unit's energy is seeded to 0x3200.
// That energy store is written as the plain assignment: MSVC spends twelve bytes on
// `mov eax, 3200h / mov word ptr [ebx+0A2h], ax` to avoid the prefix stall on a 16-bit
// immediate store, which is not a source difference.
void CUnit::DispatchSummonDArchon()
{
  CUnit *pTarget = this->pOrderTarget;

  if (pTarget != NULL && pTarget->bPlayer == this->bPlayer &&
      pTarget->bOrderId == 0xB7 && pTarget->pOrderTarget == this)
  {
    upos_t wTgtY, wTgtX, wMyY, wMyX;
    DWORD dwDist;

    if (this->dwFlags & 0x100000) {
      LONG lTargetSpeed = *reinterpret_cast<LONG *>(this->_unmapped_2C + 0x10);

      if (this->IsWithinTargetRange((lTargetSpeed * 2) >> 8, pTarget)) {
        this->dwFlags &= ~0x100000;
        this->SetNextOrder(0x96);
      }
    }

    wTgtY = pTarget->pSprite->wPosY;
    wTgtX = pTarget->pSprite->wPosX;
    wMyY = this->pSprite->wPosY;
    wMyX = this->pSprite->wPosX;
    dwDist = veclen(PosS(wMyX), PosS(wTgtX), PosS(wMyY), PosS(wTgtY));
    if (dwDist > 0x14) {
      if (this->bOrderState != 0 && this->AtMoveTarget() == 2) {
        this->StopUnitMovement();
        this->EndDispatchOrder();
        return;
      }
      this->bOrderState = 1;
      if (dwDist > 0x13) {
        CSprite *pTgtSprite = pTarget->pSprite;

        this->ApproachMoveTarget_xy(PosS(pTgtSprite->wPosX),
                                    PosS(pTgtSprite->wPosY));
        return;
      }
      {
        CSprite *pMySprite = this->pSprite;
        CSprite *pTgtSprite = pTarget->pSprite;
        mpoint_t  mid;

        mid.x = PosS((PosS(pMySprite->wPosX)
                         + PosS(pTgtSprite->wPosX)) / 2);
        mid.y = PosS((PosS(pMySprite->wPosY)
                         + PosS(pTgtSprite->wPosY)) / 2);
        this->ApproachMoveTarget_xy(mid.x, mid.y);
      }
      return;
    }

    this->CopyStatus(pTarget);
    this->TransformType(0x3F);
    this->sub_41D360(0x112);
    this->pSprite->flags.b &= 0x7F;
    this->pSprite->SetAnimation(0x0D, 1);
    this->SetButtonset(0xE4);
    status_set_update();
    this->bKillCount = (BYTE)(this->bKillCount + pTarget->bKillCount);
    this->wEnergy = 0x3200;
    pTarget->SelfDestruct();
    this->dwFlags &= ~0x20000;
    this->ResetMovementState();
    this->SetOrder(0x6A);
    return;
  }

  this->StopUnitMovement();
  this->EndDispatchOrder();
}

// Before a save: kills every completed pylon's power template and unlinks the
// pylon from the psi-link chain.
void DestroyPsiLinks() {
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit; pUnit = pUnit->pNext) {
    if (pUnit->wType != 0x9C || !(pUnit->dwFlags & 1))
      continue;
    if (pUnit->pPowerTemplate)
      pUnit->pPowerTemplate->Kill();
    pUnit->pPowerTemplate = NULL;
    if (pUnit->pPsiLinkPrev)
      pUnit->pPsiLinkPrev->pPsiLinkNext = pUnit->pPsiLinkNext;
    if (pUnit->pPsiLinkNext)
      pUnit->pPsiLinkNext->pPsiLinkPrev = pUnit->pPsiLinkPrev;
    if (sgJuiceLink_g == pUnit)
      sgJuiceLink_g = pUnit->pPsiLinkNext;
    pUnit->pPsiLinkNext = NULL;
    pUnit->pPsiLinkPrev = NULL;
  }
}

// After a save: links every completed pylon back into the psi-link chain and
// restarts its order.
void RestorePsiLinks() {
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit; pUnit = pUnit->pNext) {
    if (pUnit->wType != 0x9C || !(pUnit->dwFlags & 1))
      continue;
    CUnit *pHead = sgJuiceLink_g;
    if (pHead != pUnit && !pUnit->pPsiLinkNext && !pUnit->pPsiLinkPrev) {
      pUnit->pPsiLinkPrev = NULL;
      pUnit->pPsiLinkNext = pHead;
      if (pHead)
        pHead->pPsiLinkPrev = pUnit;
      sgJuiceLink_g = pUnit;
      pUnit->CreatePowerTemplate();
      WORD wType = pUnit->wType;
      RequestJuiceUpdate();
      pUnit->SetOrder(gubUnitOrderGuard[wType]);
    } else {
      pUnit->SetOrder(gubUnitOrderGuard[pUnit->wType]);
    }
    pUnit->EndUninterruptableDispatchOrder();
    if (gPlayerData[pUnit->bPlayer].nType == 3)
      pUnit->SetOrder(0xA1);
  }
}

// The recall area-search callback: pulls each eligible unit of the caster's
// player to the caster. Always returns 0, so the search never stops early.
int do_recall_single(CUnit *pUnit, CUnit *pCaster) {
  if (!(pUnit->pSprite->flags.b & 0x20) && pUnit->bPlayer == pCaster->bPlayer &&
      !(gulUnitTypeFlags[pUnit->wType] & 1) && !(pUnit->dwFlags & 0x4000000) &&
      !(pUnit->dwFlags & 0x10) && pUnit->wType != 0x23 && pUnit->wType != 0x24 &&
      pUnit->wType != 0x61 && !(pUnit->dwFlags & 0x40000000) && pUnit != pCaster)
    pUnit->sub_4304D0(pCaster);
  return 0;
}

// Recalls this unit to pCaster: moves it to the nearest free spot around the
// caster, or puts it back when there is none.
void CUnit::sub_4304D0(CUnit *pCaster) {
  mpoint_t  old;
  old.x = pSprite->wPosX;
  old.y = pSprite->wPosY;
  mpoint_t  dest;
  dest.x = pCaster->pSprite->wPosX;
  dest.y = pCaster->pSprite->wPosY;
  SetUnitPosition(dest.x, dest.y);
  mpoint_t  pos;  // Hyperion: a map point
  if (!FindPlaceForUnit(this, &dest, &pos, NULL, 0, 0)) {
    SetUnitPosition(old.x, old.y);
    return;
  }
  if (!(pSprite->flags.b & 0x20)) {
    RemoveTargetReferences(FALSE);
    CBullet::RemoveTargetReferences(this);
    UntrackCollision();
    pSprite->ForceInvalid();
    if (subunit)
      subunit->pSprite->ForceInvalid();
    ResetMovementState();
  }
  pOrderTarget = NULL;
  SetUnitPosition(pos.x, pos.y);
  sub_430430();
  if (wType != 0x3B)
    SetOrder(gubUnitOrderGuard[wType]);
  CThingy *pFlash = CThingy::AllocThingy(0x17B, pos.x, pos.y, 0);
  if (pFlash) {
    pFlash->pSprite->bElevationLevel = pSprite->bElevationLevel + 1;
    pFlash->UpdateVisibility();
  }
  if (wType == 1 || wType == 0x10 || wType == 0x64 || wType == 0x63 || wType == 0x68)
    CancelNuke();
  dword_665B68 = 1;
}

// Recharges this unit's shields at the battery it targets: walk to it, start
// the recharge overlay, then pull shields until full or the battery is empty.
void CUnit::DispatchRechargeUnit() {
  CUnit *pBattery = pOrderTarget;
  int nImage;
  if (!pBattery || !pBattery->wEnergy || (pBattery->dwFlags & 0x400) ||
      pBattery->bLockdownTimer || pBattery->bStasisTimer || pBattery->bParalyzeTimer) {
    EndDispatchOrder();
    return;
  }
  switch (bOrderState) {
  case 0:
    if (SetDestination_CUnit(pBattery))
      bOrderState = 1;
    return;
  case 1:
    switch (AtMoveTarget()) {
    case 0:
      if (!IsWithinTargetRange(0x80, pBattery))
        return;
    case 1:
      break;
    default:
      EndDispatchOrder();
      return;
    }
    StopUnitMovement();
    bOrderState = 2;
  case 2:
    if (pBattery->bBackgroundOrderId != 0x44)
      pBattery->SetBackgroundOrder(0x44);
    nImage = GetSizeType() + 0x170;
    pSprite->AllocImage(nImage, 0, 0, 0);
    SetForegroundOrder(0x5B);
    bOrderState = 3;
  case 3:
    if (!pBattery->pOrderTarget)
      pBattery->pOrderTarget = this;
    sub_4300E0(pBattery);
    if ((int)lShields < (int)(HandicapShields(guwUnitEnergy[wType], bPlayer) << 8) && pBattery->wEnergy) // Hyperion: at the owner's handicap
      return;
    if (pBattery->pOrderTarget == this)
      pBattery->pOrderTarget = NULL;
    EndDispatchOrder();
    return;
  }
}

// Recall: pay the energy and flash at the target point, then eight frames later
// pull every eligible unit there.
void CUnit::DispatchTeleport() {
  if (bOrderState == 1) {
    if (bUnion54 != 0)
      return;
    {
      // __restrict: the payload's literal dword_665B68 store must not pin the
      // target reads below it.
      CUnit *__restrict pThis = this;
      dword_665B68 = 0;
      find_first_unit_near(pThis->wOrderTargetX, pThis->wOrderTargetY, 0x40, do_recall_single, pThis);
    }
    if (dword_665B68 != 0) {
      int nSound = (net_rand(0x12) & 1) + 0x228;
      gamesnd_play(nSound, this, 1, 0);
    }
    EndDispatchOrder();
    return;
  }
  if (NeedsMoreEnergy(techdata_energy_g[21] << 8)) {
    char *pszMsg = status_get_str(count_race(wType) + 0x360);
    map_msg_target_self(pszMsg, bPlayer);
    EndDispatchOrder();
    return;
  }
  SubtractEnergy(techdata_energy_g[21] << 8);
  CUnit *pTarget = pOrderTarget;
  if (pTarget != NULL) {
    wOrderTargetX = pTarget->pSprite->wPosX;
    wOrderTargetY = pTarget->pSprite->wPosY;
  }
  CThingy *pFlash = CThingy::AllocThingy(0x17B, wOrderTargetX, wOrderTargetY, 0);
  if (pFlash != NULL) {
    pFlash->pSprite->bElevationLevel = pSprite->bElevationLevel + 1;
    pFlash->UpdateVisibility();
  }
  int nSound = (net_rand(0x11) & 1) + 0x226;
  snd_pos_t pos;  // Hyperion: by field (stage E)
  pos.x = PosU(wOrderTargetX);
  pos.y = PosU(wOrderTargetY);
  gamesnd_play_mtx(nSound, 1, pos, 0);
  bUnion54 = 0x16;
  bOrderState = 1;
}

// Sends a unit to its target battery, or searches for one around the target point.
void CUnit::DispatchRecharge() {
  if (bBackgroundOrderId != 0x44)
    SetBackgroundOrder(0x44);
  CUnit *pTarget = pOrderTarget;
  if (pTarget && pTarget->CanRechargeShieldsAt(this))
    pTarget->SetOrder_CUnit(0x31, this);
  else
    find_first_unit_near(wOrderTargetX, wOrderTargetY, 0x80, do_dispatch_recharge_finder, this);
  EndDispatchOrder();
}

// Recharges this unit's shields from a battery: up to 0x500 shield points at
// half an energy point each, capped by the battery's energy and the maximum.
void CUnit::sub_4300E0(CUnit *pBattery) {
  int nGain = 0x500;
  int nCost = 0x280;
  int nMissing = (HandicapShields(guwUnitEnergy[wType], bPlayer) << 8) - lShields;  // Hyperion: at the owner's handicap
  if (nMissing < 0x500) {
    nGain = nMissing;
    nCost = nMissing / 2;
  }
  if (nCost > pBattery->wEnergy) {
    nCost = pBattery->wEnergy;
    nGain = nCost * 2;
  }
  lShields += nGain;
  int nMax = HandicapShields(guwUnitEnergy[wType], bPlayer) << 8;
  if ((int)lShields > nMax)
    lShields = nMax;
  pBattery->SubtractEnergy(nCost);
}
