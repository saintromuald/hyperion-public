// CUnit.h -- the game's unit object: its 336-byte layout and the member functions src117
// declares, in the order each arrived.
//
// Every field offset, width and signature here was read off the oracle's own instruction
// stream, with the derivation beside it. Only the fields a body has needed are named, and
// the padding is part of the layout: the offsets are what the compiler encodes, so
// mov ecx,[ecx+0Ch] matches only if pSprite really sits at +0x0C. The _unmapped_* arrays
// hold the unnamed remainder at its true size -- real fields no body has derived yet.
// Replacing a slice of one with a named member is the way to extend this file, and the
// static_asserts at the bottom are the machine check on the arithmetic. The header is
// self-contained, so compiling it alone is the check.
#pragma once

#include "platform.h"   // ours: BOOL/BYTE/WORD/DWORD + <stddef.h> for offsetof
#include "pos.h"        // Hyperion: upos_t, spos_t (stage E)
#include "typelimits.h"  // Hyperion: order_id_t and the other ids

// Not included here: unittypes.h. Nothing in this header uses UNI_T_*, and pulling it in
// made the header unsafe to include in a TU that also sees the reference tree's
// arr/units.hxc -- both define UNI_T_GOLIATH and UNI_T_SCHEZAR, which is C2365. The
// generated files (oracle_thunks.cpp, hook_table.cpp) span both trees by necessity, so a
// header of src117 that they include must not carry avoidable definitions. The one consumer
// includes it directly.

// Forward declaration only -- CUnit stores a CSprite *, and a pointer needs no definition.
// The class tag, rather than struct, is not a guess: the compiler said so, through a C4099
// tag-mismatch diagnostic on a deliberate struct CSprite; probe. The one call site that
// needs CSprite complete, CImage.cpp's CUnit::CreateOverlay body, gets it from the headers
// that TU already includes.
class CSprite;

// The formation context CUnit::sub_49C5A0 fills in. Taken by pointer only, so a forward
// declaration keeps this header standalone; the definition is in src117/h/select.h, which
// the one TU that needs it includes itself.
struct select_form_t;

// Likewise for the fired weapon CUnit::DamageFromBullet takes by pointer: a forward
// declaration only, so this header stays includable on its own, and #include "CBullet.h"
// here would be a cycle -- CBullet.h -> CTarget.h, and CBullet.h's own callee block already
// names CUnit. The one TU that needs CBullet complete, src117/lang/CBullet.cpp, includes
// that header itself.
class CBullet;

// The out-rectangle CUnit::GetBounds and GetBounds_xy fill. Taken by pointer only, so a
// forward declaration keeps this header standalone. The definition -- four shorts,
// left/top/right/bottom -- is src117/h/rect16.h, which the implementing TU includes; the
// four WORD stores at [ecx+0]/[ecx+2]/[ecx+4]/[ecx+6] in both callees are what fix that
// layout.
struct rect16_t;

// sub_4BC460's by-value point; src117/h/point32.h, included by usel.cpp.
struct point32_t;

// The unit's path object. CUnit::pPath at +0x100 points at it and three bodies dereference
// it (a unit id at +0x10, a flag byte at +0x18), but a pointer member needs no definition,
// so this stays forward: CUnitPath.cpp owns the derived struct definition, exactly as
// rect16.h is owned by its implementing TU.
struct sai_path;


// The 8-byte (position, unit) pair the order-installing family passes by value, and the
// packed point that is its first half.
//
// Included, not forward-declared like CSprite and CBullet above, because
// src117/gen/oracle_thunks.cpp is generated and emits thunk bodies for unported callees
// that take an order_target_t by value: passing the argument needs the complete type, and a
// forward declaration failed there as C2027. A header whose signatures take a type by value
// must provide that type, since its consumers include generated code. order_target.h only
// forward-declares CUnit, so there is no cycle.
#include "order_target.h"   // ours: order_target_t, and point16_t through it

// The order-queue node a unit's +0x74/+0x78 pair links. Held and passed by pointer only, so
// a forward declaration keeps this header standalone. src117/h/COrder.h has the definition,
// the derivation of its three mapped fields, and the reason it needs a header of src117's at
// that exact basename -- gen.py would otherwise resolve COrder.h into the reference tree.
class COrder;

// Hyperion: a unit id where 1.17 keeps one in 16 bits, the cargo list's
// (h/poollimits.h): 32 bits on 64, where a game of more than 2,047 units takes wide ids;
// 1.17's WORD on 32. A 1.17 game's ids fit in 16 bits on either.
#if defined(_M_IX86)
typedef WORD unit_id_t;
#else
typedef DWORD unit_id_t;
#endif

// CUnit -- the game's unit object. 336 bytes (0x150), corroborated by the unit table's array
// stride in the oracle (imul ecx, ebx, 150h) at ten or more independent sites.
//
// No base class: the oracle proves the offsets, not which of them a base contributed. A flat
// layout keeps this file free of the reference tree's CThingy/CFlingy headers and keeps
// CUnit non-polymorphic and single-inheritance, which src117/gen/hook_table.cpp depends on
// -- it recovers a member function's code address by memcpy out of a pointer-to-member.
// Derive the bases the day a CUnit * is passed where a CThingy * is expected.
class CUnit {
public:
  // The members. Each offset and width comes from the instruction that encodes it, and a
  // width is as much part of the layout as an offset.
  //   +0x00/+0x04  pPrev / pNext  the master used-list links, one intrusive pair of dword
  //                  pointers (GetUsedList's head, CUnit::sub_437290's walk).
  //   +0x08  lHitPoints  dword, signed (GetLife's sar where unsigned emits shr), 8.8 fixed
  //                  point. lShields at +0x60 is unsigned because its readers divide with
  //                  no rounding correction.
  //   +0x0C  pSprite   dword pointer, in ECX before a CSprite:: call in eight bodies.
  //   +0x4C  bPlayer   byte, unsigned (gamesnd_ack widens it with movzx).
  //   +0x64  wType     word, the units.dat index; a DWORD field would emit a plain mov.
  //   +0x70  subunit   dword pointer, null-tested and used as a CUnit (the turret).
  //   +0xDC  dwFlags   DWORD, although three bodies test it with a byte form: gamesnd_ack
  //                  tests 0x400, which no byte at this offset holds.
  // Everything else is unmapped: real fields no body has needed, held at true size so the
  // named ones keep their offsets, and where a sheet and the oracle disagree the oracle wins.
  /*0x00*/ CUnit   *pPrev;
  /*0x04*/ CUnit   *pNext;
  /*0x08*/ LONG     lHitPoints;
  /*0x0C*/ CSprite *pSprite;
  //   +0x10 / +0x12  wMoveTargetX / wMoveTargetY, from DispatchIgnoreMove, which compares
  //          each against the sprite's own wPosX/wPosY through AX -- has it arrived. 16-bit
  //          loads on both sides, so both are WORDs, and the pairing with an already-derived
  //          (x,y) pair is what names them. The sign is not claimed: the compare is 16-bit
  //          and equality-only.
  // Hyperion: positions of 4 bytes on x64 (h/pos.h), and the pair as one point, as
  // CFlingy has it, where 1.17 read the two through one DWORD (stage E)
  union {
    struct {
      /*0x10*/ upos_t   wMoveTargetX;
      /*0x12*/ upos_t   wMoveTargetY;
    };
    /*0x10*/ mpoint_t moveTarget;
  };
  //   +0x20  bMoveFlags  a flags byte: sub_421120 tests bit 1 in place on both the unit and
  //          its target, and a wider declaration emits test dword ptr. Read as flags because
  //          both sites mask one bit; the name is the port's and the bit is not decoded.
  //   +0x21  bUnion21  named for the offset. Its one reader, FireWeapon, pushes the
  //          zero-extended byte through as CBullet::AllocBullet's fifth argument and nothing
  //          in reach discriminates the value: the width is derived, the meaning is not.
  //   +0x14  holds a CUnit * (four TUs read it through _unmapped_14). The second arm gives
  //          it a pointer's size and alignment on x64; on x86 it is the same four bytes.
  union {
    /*0x14*/ BYTE     _unmapped_14[0x04];
    /*0x14*/ CUnit   *_unmapped_14_width;
  };
  //   +0x18 / +0x1A  wNextWaypointX / wNextWaypointY, a waypoint pair in the same movement
  //          cluster as +0x10's move target. CUnit::Stop stores the clipped move target back
  //          to both slots as single DWORDs (mov dword ptr [edi+10h], eax / mov dword ptr
  //          [edi+18h], eax), i.e. one 4-byte copy through the pair, so the source assigns
  //          whole coordinates rather than a field; the widths are what that store covers.
  union {  // Hyperion: as moveTarget (stage E)
    struct {
      /*0x18*/ upos_t   wNextWaypointX;
      /*0x1A*/ upos_t   wNextWaypointY;
    };
    /*0x18*/ mpoint_t nextWaypoint;
  };
  // Hyperion: CFlingy's destination (the next target waypoint), read here as two
  // WORDs; named when positions widened (stage E), where it was _unmapped_1C.
  /*0x1C*/ upos_t   wDestinationX;
  /*0x1E*/ upos_t   wDestinationY;
  /*0x20*/ BYTE     bMoveFlags;
  /*0x21*/ BYTE     bUnion21;
  //   +0x22  bTurnRadius  the same memory src117/h/CFlingy.h names bTurnRadius at this
  //          offset -- CUnit is that class's layout prefix, so the two spellings are carried
  //          in step. Three bodies end call FlingyTurnRadiusWithModifiers /
  //          mov byte ptr [esi+22h], al, and that callee's own source is gubFlingyMaxTurn
  //          (0x5114C0, BYTE).
  //   +0x23  bDirection  one byte, zero-extended, at all 34 sites in the corpus, so BYTE is
  //          proven and the value unsigned. sub_421120 feeds this unit's and its target's to
  //          get_angle and tests the result against 0x20, which is a facing in a 256-step
  //          byte domain. The role is derived, the units are not, and the name is the port's.
  /*0x22*/ BYTE     bTurnRadius;
  /*0x23*/ BYTE     bDirection;
  /*0x24*/ BYTE     _unmapped_24[0x04];
  //   +0x28 / +0x2A  wPositionX / wPositionY. IsTargetInWeaponRange compares each against
  //          the destination pair at +0x1C/+0x1E in the same two-coordinate has-it-arrived
  //          shape DispatchIgnoreMove uses for the move target, and 16-bit compares on both
  //          sides fix the width at 2. The sign is not discriminated -- the compares are
  //          equality-only -- so WORD follows the load and is a judgement, not a proof. The
  //          name is src117/h/CFlingy.h's at these exact offsets (CUnit is that class's
  //          layout prefix), carried over rather than renamed to CSprite's wPosX/wPosY: two
  //          names for one field in one object family is how a corpus stops being greppable.
  /*0x28*/ upos_t   wPositionX;
  /*0x2A*/ upos_t   wPositionY;
  /*0x2C*/ BYTE     _unmapped_2C[0x08];
  //   +0x34  lTopSpeed  /  +0x48  wAcceleration  both already named at these offsets in
  //          src117/h/CFlingy.h and carried over for the same prefix reason as +0x22. The
  //          three movement-refresh bodies (ResetMovementSpeed, SetMovementUpgrade,
  //          ApplyUpgrades) emit one store per field from one call each:
  //              mov dword ptr [esi+34h], eax   ; FlingyMaxSpeed...
  //              mov word  ptr [esi+48h], ax    ; FlingyAccel...
  //              mov byte  ptr [esi+22h], al    ; FlingyTurnRadius...
  //          Three different store widths on three adjacent calls is what fixes all three
  //          widths at once. The 0x20 bytes of _unmapped_2C are split, not extended:
  //          8 + 4 + 16 + 2 + 2, and sizeof(CUnit) == 0x150 below is the machine check.
  /*0x34*/ LONG     lTopSpeed;
  /*0x38*/ BYTE     _unmapped_38[0x10];
  /*0x48*/ WORD     wAcceleration;
  /*0x4A*/ BYTE     _unmapped_4A[0x02];
  /*0x4C*/ BYTE     bPlayer;
  //   +0x4D  bOrderId  cmp byte ptr [ecx+4Dh], 21h in bf_stop_build and
  //          bf_scv_attack_orders. A BYTE compare against a small constant, and the field
  //          sits directly after bPlayer.
  /*0x4D*/ order_id_t bOrderId;  // Hyperion: 16 bits on x64, a mod's orders
  //   +0x4E  bOrderState  the per-order step counter. Every Dispatch* body in CUnitMisc
  //          opens on the same three instructions -- movzx eax, byte ptr [esi+4Eh] / sub
  //          eax, 0h / jz <state 0> / dec eax / jnz <done> -- i.e. a switch over 0 and 1,
  //          and each state-0 arm ends by writing mov byte ptr [esi+4Eh], 1h. One byte
  //          (movzx from byte, stored as byte), and its domain here is {0,1}.
  /*0x4E*/ BYTE     bOrderState;
  //   +0x4F  bOrderFlags  read as a bit field rather than a counter because every access
  //          masks bit 0 and leaves the rest alone: mov al, byte ptr [esi+4Fh] / test al, 1h
  //          then and al, -2h before storing back (DispatchDoorOpen, DispatchDoorClose,
  //          DispatchHideTrap, DispatchRevealTrap). A read-modify-write that preserves seven
  //          bits it never inspects is a flags byte; the name of bit 0 is not claimed.
  /*0x4F*/ BYTE     bOrderFlags;
  //   +0x54  bUnion54  named for the offset like bUnion21: the width is derived, the meaning
  //          is not. DispatchTowerGuard both reads and writes it -- cmp byte ptr [esi+54h],
  //          0h / jnz as the last of five guards, then mov byte ptr [esi+54h], 0Fh to claim
  //          the slot -- and a byte compare against 0 plus a byte store of an immediate fix
  //          the width at 1. The 0x0F is the oracle's immediate and is not read as a count,
  //          a mask or an id. The role is already handled this dispatch, do not redo it: the
  //          guard is tested last, after the four disable timers, and the store is the first
  //          thing the taken path does. DispatchAttackMove reads and writes the same byte in
  //          the same order-dispatch shape, which corroborates the role without naming it.
  /*0x50*/ BYTE     _unmapped_50[0x04];
#if !defined(_M_IX86)
  // Hyperion: where CTarget's dwOrderUnion54 falls after the wider order id
  BYTE              _x64_pad_order[2];
#endif
  /*0x54*/ BYTE     bUnion54;
  /*0x55*/ BYTE     _unmapped_55[0x02];
  //   +0x57  bUnion57  CUnit::DispatchJunkYardDog stores 0xFF on its wander
  //                    arm and later tests it against 0 (`byte ptr [esi+57h]`).
  /*0x57*/ BYTE     bUnion57;
  //   +0x58 / +0x5A  wOrderTargetX / wOrderTargetY. Both are loaded with movsx eax, word
  //          ptr and pushed as int arguments to SetDestination_xy and EndDispatchOrder_xy.
  //          The sign is proven: movsx, not movzx, at every site -- a sign-extending 16->32
  //          load is only emitted for a signed source, and it is what makes these short
  //          where wMoveTargetX/Y stay WORD. They sit immediately below pOrderTarget, the
  //          same what-this-order-is-aimed-at pair in its other form, and DispatchIgnoreMove
  //          selects between them in a single if/else.
  // Hyperion: the pair as one point, as moveTarget (stage E); OrderTarget() gives it
  // with pOrderTarget as the order_target_t 1.17 read the three as through a cast
  union {
    struct {
      /*0x58*/ spos_t   wOrderTargetX;
      /*0x5A*/ spos_t   wOrderTargetY;
    };
    /*0x58*/ mpoint_t orderTargetPos;
  };
  //   +0x5C  pOrderTarget  mov eax, dword ptr [esi+5Ch] / test eax, eax / jz guards its use
  //          in both DispatchWatchTarget and DispatchIgnoreMove, and the loaded dword is
  //          then passed as the CUnit * argument to CanTarget and SetDestination_CUnit. A
  //          dword load, a null guard, and consumption AS a CUnit by two callees.
  /*0x5C*/ CUnit   *pOrderTarget;
  order_target_t OrderTarget() const {  // Hyperion
    order_target_t t;
    t.pt = orderTargetPos;
    t.pUnit = pOrderTarget;
    return t;
  }
  //   +0x60  lShields  set_shields ends shl edx, 8h / mov dword ptr [edi+60h], edx -- a
  //          full DWORD store, so 32 bits, and that shl 8 is the x256 fixed point made
  //          explicit. The width is forced by the data as well as by the store: the stored
  //          value is guwUnitEnergy[type] (<= 0xFFFF) shifted left 8, up to 24 significant
  //          bits, which a WORD could not hold. The clamp ahead of it is cmp esi, edx /
  //          cmovl edx, esi, a signed compare, so the value is an int; the field is spelled
  //          DWORD to match the store width and its neighbours.
  /*0x60*/ DWORD    lShields;
  /*0x64*/ WORD     wType;
  /*0x66*/ BYTE     _unmapped_66[0x02];
  //   +0x68  pPlayerUnitPrev  the back link of the per-player chain whose forward link is
  //          pPlayerUnitNext at +0x6C. sub_426530's doubly-linked unlink types both slots at
  //          once: each is dereferenced at the other's offset on a value loaded from this
  //          same object, and both the loads and the stores are dwords. It does not fix the
  //          direction, which comes from SAI_Banker_RemoveUnit: that walk starts at
  //          GetPlayerUnitList's head and advances through +0x6C, so +0x6C is forward.
  /*0x68*/ CUnit   *pPlayerUnitPrev;
  //   +0x6C  pPlayerUnitNext  mov eax, dword ptr [eax+6Ch] is the loop advance in
  //          SAI_Banker_RemoveUnit: the value starts at GetPlayerUnitList's return and each
  //          member is dereferenced at +0x64 exactly as `this` is, so a CUnit *, 4 bytes.
  //          This is the per-PLAYER list link, a different chain from the global prev/next
  //          pair at +0x00/+0x04 -- same object, two memberships.
  /*0x6C*/ CUnit   *pPlayerUnitNext;
  /*0x70*/ CUnit   *subunit;
  //   +0x74  pUnion74 / pOrderQueueHead  DispatchHideTrap and DispatchRevealTrap only
  //          null-test the dword, which gates whether the trap reverts to its guard order,
  //          so void * is the strongest spelling for that arm. The second arm is the head of
  //          the order queue: RemoveQueuedOrder hands this slot and +0x78 to CLink::Unlink as
  //          the (ppHead, ppTail) pair, RemoveOrder walks the list through each node's +0x04,
  //          and GetFirstQueuedOrder reads COrder::bOrderId straight off it. Merging the arms
  //          would upgrade a null test into a claim about a pointee.
  union {
    /*0x74*/ void   *pUnion74;
    /*0x74*/ COrder *pOrderQueueHead;
  };
  //   +0x78  pOrderQueueTail  the ppTail half of the pair quoted just above -- never read
  //          as a value in the family, only addressed (lea eax, [ecx+78h], add esi, 78h) and
  //          handed to CLink::Unlink / AppendLink / PrependThis as their CLink ** tail
  //          parameter. SetForegroundOrder is the one body that also loads it, to choose
  //          between appending and seeding an empty list (mov ecx, dword ptr [esi+78h] /
  //          add esi, 78h / push esi / test ecx, ecx / jz <seed>), and that dword load used
  //          as a node pointer fixes both the width and the pointee.
  /*0x78*/ COrder  *pOrderQueueTail;
  //   +0x7C  dwUnion7C  DispatchIgnoreMove writes it and only writes it -- mov dword ptr
  //          [esi+7Ch], 0h, on the arm where the unit has arrived and the order ends. A
  //          write-only dword store of an immediate 0 fixes the width at 4 and leaves the
  //          type undiscriminated; DWORD is the store, not a claim.
  /*0x7C*/ DWORD_PTR    dwUnion7C;
  //   +0x80  pUnion80  named for the offset: more than one subsystem reads this dword as
  //          more than one thing. ExitTransport dereferences it at +0x0C, +0x54, +0xDC and
  //          three more exactly as `this` is, and EnterTransport names it outright, storing
  //          the transport and then setting UF_InTransport. But FindArmedSilo reaches +0x80
  //          only for the Nuclear Silo type and uses it as the nuke, and the build path
  //          (CancelBuild, DispatchConstructBuilding, DispatchPlaceAddon) as the addon or
  //          building link. Three readings, one slot; name the arms when a body derives the
  //          whole union.
  /*0x80*/ CUnit   *pUnion80;
  //   +0x84  bQueuedOrderCount  a BYTE counter of queued orders, named by the arithmetic.
  //          Five bodies in CUnitOrder.cpp move it, each guarded by the same test that the
  //          order being added or removed has an order_highlight entry (cmp word ptr
  //          [eax*2+51B980h], -1h), so it counts only the orders the UI can show and is not
  //          the list length. The width is not just the access size: every decrement is
  //          followed by a clamp of 0xFF back to 0, which only makes sense on one byte, and
  //          the field is stored unsigned while the clamp compares against -1.
  /*0x84*/ BYTE     bQueuedOrderCount;
  //   +0x85  bUnion85  BYTE at 22 independent sites and no wider access anywhere: byte
  //          immediates 0x0F (DestroyOverlays, DispatchCTFCop), 0x4B (DispatchReturnResource),
  //          0x08 (RunOrders, RunTransportOrders) and 0x00 (StepUpdate, ManageQueuedOrders,
  //          sub_424230, DispatchTrilobyte, DispatchGuardBunker, DispatchHoldPosition);
  //          byte-register stores in EntityUpdate and IsTargetInWeaponRange; byte reads in
  //          DispatchSpawningLarva, sub_41F530 and sub_420BD0.
  //          The name is bUnion85 and not bOrderTimer: a read-modify-write in RunOrders
  //          makes a timer tempting, and a decrement-shaped access is a lead rather than a
  //          derivation. The lead is recorded so the next reader can finish it -- if a body
  //          is ever matched that decrements this field on a clock, rename it then.
  /*0x85*/ BYTE     bUnion85;
  // wPrevUnitType is the type a unit had before CUnit::sub_426750 morphed it;
  // a union arm so the bodies that index _unmapped_86 keep their spelling.
#pragma pack(push, 1)
  union {
    /*0x86*/ BYTE     _unmapped_86[0x09];
    struct {
      /*0x86*/ BYTE   _unmapped_86b[0x02];
      /*0x88*/ WORD   wPrevUnitType;
    };
  };
#pragma pack(pop)
  //   +0x8F  bKillCount  AddKill both reads and writes it a byte at a time -- mov al, byte
  //          ptr [ecx+8Fh] / cmp al, -1h / jnb / inc al / mov byte ptr [ecx+8Fh], al -- so
  //          the width is 1, and jnb rather than jge against 0xFF makes the field unsigned:
  //          a saturating increment that stops at 255 is only a meaningful clamp for an
  //          unsigned byte. Every access in the corpus is byte-wide: AddKill,
  //          DispatchSummonArchon and DispatchSummonDArchon (which add the merging unit's
  //          kills to the archon's, sound only for a count) and statdraw_draw_kills, whose
  //          movzx for display is the independent proof of the sign. The name comes from
  //          that one consumer, the kill-count stat panel.
  /*0x8F*/ BYTE     bKillCount;
  // +0x90..+0x92 as three named bytes, a union arm so _unmapped_90 keeps its
  // spelling: CUnit::sub_425A70 stores 8, 0 and 0 to them one field at a time.
  union {
    /*0x90*/ BYTE     _unmapped_90[0x03];
    struct {
      /*0x90*/ BYTE   bUnion90;
      /*0x91*/ BYTE   bUnion91;
      /*0x92*/ BYTE   bUnion92;
    };
  };
  //   +0x93  bUnion93  named for the offset rather than the reading: a single bit that is
  //          only ever set and only ever tested supports a width claim and not a meaning
  //          claim. What is proven is bit 0, one byte wide. Eight order installers set it
  //          (or byte ptr [esi+93h], 1h) immediately before calling ManageQueuedOrders, and
  //          one body reads it, EndNoBreakCodeSection, which breaks the animation when the
  //          queue is non-empty and the bit is set. An order-changed-since-the-last-break
  //          latch is the lead, recorded as one until a body forces a second bit or a clear.
  /*0x93*/ BYTE     bUnion93;
  //   +0x94  wButtonset  SetButtonset's only store is mov word ptr [ecx+94h], dx, and +0x96
  //          is already bCloakCount, so the slot is exactly two bytes wide.
  /*0x94*/ WORD     wButtonset;
  //   +0x96  bCloakCount  a per-tick counter, not a flag, and BYTE at all five accesses:
  //          sub_4340D0 post-increments it and gates the link-in on the old value,
  //          sub_434DA0 refuses to unlink while it is non-zero, CloakUpdate has an arm for
  //          exactly 1, EntityUpdate resets it per tick under dwFlags & 0x300 (the
  //          cloak/burrow pair), and DestroyImpl clears it to force the unlink. The name is
  //          the port's, from the company it keeps; the unit of the count is not derived.
  /*0x96*/ BYTE     bCloakCount;
  //   +0x97  bPathState  the path-order state machine byte. BYTE by every access:
  //          mov byte ptr [esi+97h], 0 (ResetMovementState), 10h (PathFailedPath),
  //          mov cl, byte ptr (FixupPathState), and the write side is always a full byte
  //          store -- 0x00, 0x09, 0x0B, 0x0C, 0x0D, 0x0F, 0x10, 0x13, 0x16, 0x19, 0x1A and
  //          FixupPathState's conditional 0x13/0x15 pair. That compare is unsigned
  //          (cmp cl, 1Ch / jb), so the byte is unsigned in the consumer too, and the values
  //          cluster in two ranges (<= 0x1C flips 0x15 to 0x13), so it reads as a state code
  //          and not a counter.
  /*0x97*/ BYTE     bPathState;

  //   +0x98  wBuildQueue[5]  the extent is forced by the struct, not guessed. GetTypeEx and
  //          GetName both index it with the byte at +0xA4 (movzx eax, byte ptr [esi+0A4h] /
  //          mov ax, word ptr [esi+eax*2+98h]): scale 2 and a 16-bit load mean WORD
  //          elements, and since wEnergy is proven at +0xA2 the array cannot be longer than
  //          (0xA2-0x98)/2 = 5. Five is also the build-queue depth the game exposes, and the
  //          value read goes straight to the unit-name lookup, so the queued item is a unit
  //          type, matching wType's width.
  /*0x98*/ WORD     wBuildQueue[5];
  //   +0xA2  wEnergy  set_energy ends shl edi, 8h / mov word ptr [esi+0A2h], di -- a 16-bit
  //          store through DI, so a WORD, on the same x256 fixed point as lShields. The
  //          width is what makes set_energy's upstream truncation a real defect rather than
  //          a width artifact: that body keeps only the low BYTE of max*percent/100 before
  //          shifting, and this destination is wide enough to have held the untruncated
  //          value. Recorded in docs/open-issues.md; reproduced, not fixed.
  /*0xA2*/ WORD     wEnergy;
  //   +0xA4  bBuildQueueSlot  the index into wBuildQueue above: movzx eax, byte ptr
  //          [esi+0A4h] immediately before mov ax, word ptr [esi+eax*2+98h] in both GetTypeEx
  //          and GetName. Byte load, so 1 byte; its role is fixed by being the scale-2 index
  //          of the array that ends two bytes below it.
  /*0xA4*/ BYTE     bBuildQueueSlot;
  //   +0xA5  bIdGeneration  derived from both sides of one encoding. GetUnitID builds the
  //          id -- movzx eax, byte ptr [esi+0A5h] / shl eax, 0Bh / or eax, ecx, with ECX the
  //          1-based table index -- and FromIdAlive takes it apart: and ecx, 7FFh for the
  //          index, imul edi, ecx, 150h, then movzx ecx, byte ptr [edi-0ABh] (which is +0xA5
  //          of that unit, since EDI is one stride past the object: 0x150 - 0xAB = 0xA5) and
  //          cmp ecx, esi against the id's high bits. So the byte is the generation counter
  //          that makes a recycled slot's id stale, and 11 bits of index is consistent with
  //          the 0x6A4 (1700) slot cap GetUnitID checks.
  /*0xA5*/ BYTE     bIdGeneration;
  //   +0xA6  bBackgroundOrderId  this one does get a semantic name, because two functions
  //          whose own names come from the map write exactly this slot:
  //          ReplaceBackgroundOrder stores its only argument unconditionally, and
  //          SetBackgroundOrder is the same body with an early-out when the value is already
  //          there -- mov al, byte ptr [ebp+8h] / cmp byte ptr [ecx+0A6h], al / jz, then the
  //          store. A byte-wide compare-and-store of an order id, in a pair of functions
  //          named for the background order, resting on two bodies rather than one. The
  //          three slots it clears alongside (+0xE8, +0xEC, +0xE2) are that order's own
  //          state, named for their offsets below.
  /*0xA6*/ order_id_t bBackgroundOrderId;  // Hyperion: 16 bits on x64
  //   +0xA7  bDmgOverlayCount  a cache of the sprite's current damage-overlay count:
  //          SyncConstructionGraphic and sub_425A70 each end call
  //          CSprite::GetDamageOverlayCount / mov byte ptr [esi+0A7h], al, storing it
  //          straight from the accessor, and UpdateDmgOverlays walks it toward a computed
  //          target with an unsigned byte compare, stepping it with DamageOverlaysDown/Up.
  //          Width 1 from the byte store and the byte compare.
  /*0xA7*/ BYTE     bDmgOverlayCount;
  //   +0xA8  wHPGain  AssignHPGain is the only body that touches it, and it is wShieldGain's
  //          twin two bytes below: same skeleton, same 16-bit store through AX, same
  //          clamp-to-1 when the quotient truncates to nothing. The 16-bit store fixes the
  //          width at 2 and the clamp says it is a per-tick increment -- a gain of zero
  //          would freeze a building's health bar for the whole of its construction. The two
  //          are not interchangeable: this one divides gxUnitLife[type] - gxUnitLife[type]/10
  //          by the build time and rounds up (lea eax,[esi-1] before the divide), where
  //          wShieldGain's quotient truncates.
  /*0xA8*/ WORD     wHPGain;
  //   +0xAA  wShieldGain  AssignShieldGain is the only body that touches it and writes it
  //          twice: a 16-bit store of (guwUnitEnergy[type]<<8 - startingShields) divided by
  //          guwUnitTimeToBuild[type], and then 1 when that quotient came out zero. The
  //          16-bit store fixes the width at 2, and the clamp-to-1 says it is a per-tick
  //          increment and not a total: the value is added each frame while the unit builds,
  //          so zero would stall it.
  /*0xAA*/ WORD     wShieldGain;
  //   +0xAC  wRemainingBuildTime  WORD on both the load and the store, never a byte and
  //          never a dword, at every site. CountDownToCompletion is the whole derivation:
  //          zero means done, otherwise it subtracts and clamps at zero, so this is time
  //          remaining and it counts down rather than a progress accumulator.
  //          GetBuildPercentage agrees, computing (total - this) * 100 / total against the
  //          units.dat build time. Its two writers also pin bit 1 of glCheatBits
  //          (0x5432FC): both branch on it and take a x16 path when it is set.
  /*0xAC*/ WORD     wRemainingBuildTime;
  // +0xAE wPrevHP: the whole hit points a morphing unit had, stored as a WORD
  // by CUnit::sub_426750 (1 when the value would be 0).
  union {
    /*0xAE*/ BYTE     _unmapped_AE[0x02];
    /*0xAE*/ WORD     wPrevHP;
  };
  //   +0xB0  wCargoUnitIds  eight WORD unit ids, and the extent is proven by a second datum
  //          rather than inferred from the free space here: IsUnitInside walks
  //          [ecx+0B0h..0BFh] eight times through FromIdAlive, and statdraw_query_transport
  //          walks the same eight slots with a loop bound of its own -- it stops against
  //          0x6AABC8 from a base of 0x6AABB8, i.e. sgwSavedTransport's 8 WORDs. The
  //          elements are unit IDs and not pointers: every load is fed to FromIdAlive, and
  //          the WORD compare fixes the width at 2.
  /*0xB0*/ unit_id_t wCargoUnitIds[8];  // Hyperion: unit_id_t, WORD in 1.17
  //   +0xC0  pPowerup / pHangarParent  the head of a type-specific union, and the two arms
  //          are mutually exclusive unit classes -- a worker carrying a powerup cannot be a
  //          docked interceptor -- so naming these bytes only pPowerup would make set_hangar
  //          read as a bug. count_completed loads [esi+0C0h], null-tests it and then reads
  //          +0x64 off it exactly as off `this`, so a CUnit *, reached only under
  //          gulUnitTypeFlags[type] & 8, the carries-a-powerup bit. set_hangar stores the
  //          carrier into a freshly created FIGHTER's +0xC0, and
  //          HangarConstructionFinished reads the carrier's own +0xC0 as the head of the
  //          hangar list. Same 4 bytes, same width.
  union {
    /*0xC0*/ CUnit *pPowerup;
    /*0xC0*/ CUnit *pHangarParent;
    // DetachAddon loads this arm as the attached unit, invokes a CUnit member through it
    // and clears it. The neutral alias is kept for sub_437290, whose equality test proves
    // only a CUnit * relation, not which mutually exclusive role owns the slot.
    /*0xC0*/ CUnit *pAddon;
    // The carrier side of the same slot, and the reason the note above says
    // HangarConstructionFinished reads the CARRIER's own +0xC0 as the head of the hangar
    // list: that body does mov eax, dword ptr [edi+0C0h] (edi = the carrier), stores it into
    // the docking fighter's +0xC4, and then writes the fighter into [edi+0C0h] -- a dword
    // read and a dword write of a value that is a CUnit * on both sides. FighterDoFcn
    // corroborates: it starts its docked walk at [ecx+0C0h] and then follows pHangarNext,
    // pairing this head with the bHangarCount at +0xC8. A fighter's own +0xC0 is
    // pHangarParent above, and a carrier is never itself docked, so the arms are exclusive.
    /*0xC0*/ CUnit *pHangarDockedHead;
    /*0xC0*/ CUnit *pUnionC0;
    // A Vulture's (types 0x02 and 0x13) spider mines: CUnit::sub_425710 sets
    // the byte to 3 for exactly those two types.
    /*0xC0*/ BYTE   bSpiderMineCount;
  };
  //   +0xC4  dwUnionC4  the first half of the pair the +0xC8 note describes: DispatchHarvest
  //          stores the dword at +0x58 here, wOrderTargetX and wOrderTargetY read as one
  //          dword, which is where both the width and the contents come from.
  //   +0xC4  pHangarNext / pHangarDeployedHead  the same dword on two unit classes. On a
  //          fighter it is the forward link of the hangar list, which three walkers advance
  //          through; on a carrier it is the head of the deployed list, paired with the count
  //          at +0xC9 as +0xC0 is with +0xC8. So a carrier has two heads and both lists
  //          thread through a fighter's single +0xC4. Every access is a dword.
  union {
    struct {
      /*0xC4*/ BYTE   _unmapped_C4[0x02];
      //   +0xC6  wRemainingResearchTime  one field serving both the tech and the upgrade
      //          arm: StartResearch seeds it from techdata_timeCost_g and StartUpgrade from
      //          upg_timeCostBase_g + level * upg_timeCostFactor_g, and both dispatchers
      //          then run it down (movzx edx, word ptr / lea eax, [edx-1h] / mov word ptr),
      //          which fixes the polarity and the name as for +0xAC. All four cancel and
      //          refund paths clear it with a 16-bit store. WORD by every access. It lives
      //          inside the +0xC4 union's struct arm rather than beside it so the pointer
      //          arms keep their 4 bytes.
      /*0xC6*/ WORD   wRemainingResearchTime;
    };
    /*0xC4*/ DWORD    dwUnionC4_SavedOrderTargetPos;
    /*0xC4*/ CUnit   *pHangarNext;
    /*0xC4*/ CUnit   *pHangarDeployedHead;
  };
  //   +0xC8  bTechType     cmp byte ptr [ecx+0C8h], 2Ch in bf_stop_research
  //   +0xC9  bUpgradeType  cmp byte ptr [ecx+0C9h], 3Dh in bf_stop_upgrade
  //          One byte each, from the BYTE immediates. The constants name them: techdata
  //          carries 44 techs so 0x2C is TECH_NONE, and upgrades.dat 61 so 0x3D is
  //          UPGRADE_NONE -- a building's in-progress tech and upgrade slots.
  //          Three more readings share these bytes, which is what the union is for:
  //          count_completed reads +0xC8 as a hangar count for the Carrier and Reaver
  //          types; DispatchHarvest stores the order target over +0xC4/+0xC8 as two dwords;
  //          and HangarConstructionFinished writes +0xC8 as a CUnit * on a fighter.
  //          The nesting is not decoration: a 4-byte arm in the flat union would make it 4
  //          bytes wide and push +0xC9 out to +0xCC, moving every member up to dwFlags. So
  //          the dword arm wraps a struct holding the byte arms, and the static_asserts at
  //          the bottom are what prove the offsets stayed put.
  union {
    /*0xC8*/ CUnit   *pUnionC8_SavedOrderTarget;
    /*0xC8*/ CUnit   *pHangarPrev;
    struct {
      union {
        /*0xC8*/ tech_id_t bTechType;  // Hyperion: 16 bits on x64
        /*0xC8*/ BYTE bHangarCount;
      };
      union {
        /*0xC9*/ upgrade_id_t bUpgradeType;
        /*0xC9*/ BYTE bHangarCountBuilding;
      };
      /*0xCA*/ BYTE   _unmapped_CA[0x01];
      // Byte-wide in every access: DispatchBuildingLand stores 1 and 0, and
      // DestroyImpl only unplaces a building when it is nonzero.
      /*0xCB*/ BYTE   bBuildingPlaced;
    };
  };
  /*0xCC*/ BYTE     _unmapped_CC[0x01];
  //   +0xCD  bTargetUpgradeLevel  StartUpgrade stores get_upgrade_level(bPlayer,
  //          bUpgradeType) + 1 into it (call / inc al / mov byte ptr [esi+0CDh], al) and
  //          DispatchUpgrade tests the current level against it twice (cmp al, byte ptr
  //          [edi+0CDh] / jnb) before crediting the upgrade, so the two bodies jointly say
  //          a level being worked towards, and the level+1 seed is what makes it a target
  //          rather than a copy. Both cancel paths clear it. Every access is BYTE.
  /*0xCD*/ BYTE     bTargetUpgradeLevel;
  //   +0xC9  gets a second arm on the same evidence +0xC8 carries: CanAttack adds the two
  //          bytes together on a path reached only for the carrier/reaver types, where +0xC8
  //          is already the hangar arm, so +0xC9 declared only as bUpgradeType made that
  //          body read as a bug while emitting correct bytes. What is proven is the offset,
  //          the BYTE width and that this site is not the building arm. Both arms sit inside
  //          the +0xC8 union's struct arm, so the dword arm cannot move them.
  //   +0xCE  bUnionCE  named for the offset: three writers and one reader, all BYTE, and
  //          each is paired with the pUnionD0 slot four bytes later on the same referent. So
  //          it is a flag over the harvest-target slot, but which flag is an inference.
  /*0xCE*/ BYTE     bUnionCE;
  /*0xCF*/ BYTE     _unmapped_CF;
  //   +0xD0  pUnionD0  named for the offset on purpose, because four oracle functions read
  //          these bytes as four different things:
  //            bf_no_exit does a dword null test (its name says exit);
  //            FindArmedSilo reaches the offset only for the Nuclear Silo type and
  //              dereferences the result at +0x80, a nuke pointer;
  //            set_resources_unit stores a WORD and UpdateResourceAnim reads the same bytes
  //              against the three mineral-patch amounts, so on a resource container these
  //              bytes are the amount remaining;
  //            ShowPowerTemplates fills a pylon's slot when null and then passes it as
  //              CSprite::SetMask's `this`, which fixes that pointee.
  //          +0xD2 is the animation currently applied and +0xD3 a worker's claim on the
  //          resource, counted up and down but not separable from a flag. A worker's own
  //          slot holds the resource pointer, the fifth reading and why the union exists.
  union {
    /*0xD0*/ CUnit   *pUnionD0;
    /*0xD0*/ CSprite *pPowerTemplate;   // a pylon's arm of the same slot
    struct {
      /*0xD0*/ WORD wResourceAmount;
      /*0xD2*/ BYTE bResourceAnim;
      /*0xD3*/ BYTE bUnionD3;
    };
    // A units.dat 0x800 (single-entity) unit's pick-up position:
    // CUnit::Complete stores the sprite's x and y here as two WORDs.
    struct {
      /*0xD0*/ upos_t wPowerupOriginX;  // Hyperion: positions of 4 bytes on x64 (stage E)
      /*0xD2*/ upos_t wPowerupOriginY;
    };
  };
  //   +0xD4  pUnionD4  named for the offset, like pUnionD0. count_completed only null-tests
  //          the dword, reached when wType == 0x6C (Nuclear Silo) and gating a count of unit
  //          type 14, the silo's nuke slot; a null test cannot tell a pointer from a count,
  //          so it is void *. A second arm and a sibling at +0xD8 come from
  //          RemoveHarvestUnit, a doubly-linked unlink: when +0xD4 is null the write goes to
  //          the resource's own +0xD4, so a resource's slot is the list head while a
  //          worker's is its prev link. +0xD8 is dereferenced as a CUnit, and it overlaps
  //          bUnionD9 inside the union.
  union {
    /*0xD4*/ void    *pUnionD4;
    /*0xD4*/ CUnit   *pUnionD4_HarvestPrev;
  };
  union {
    /*0xD8*/ CUnit   *pUnionD8_HarvestNext;
    struct {
      /*0xD8*/ BYTE  _unmapped_D8;
  //   +0xD9  bUnionD9  named for the offset. One reader, sub_432510, tests it against zero
  //          on the search candidate, gating the harvest-target rejection alongside a global
  //          flag and the searching player's slot type. BYTE by the encoding; no writer in
  //          the corpus explains the meaning. Its neighbours stay unmapped, because an
  //          unused declaration is avoidable C2371 exposure in the generated TUs.
      /*0xD9*/ BYTE  bUnionD9;
      /*0xDA*/ BYTE  _unmapped_DA[0x02];
    };
  };
  union {
    /*0xDC*/ DWORD          dwFlags;
    /*0xDC*/ volatile DWORD dwFlagsVolatile;  // the same bits, for a body that needs
                                              // the reload the oracle emits
  };
  //   +0xE0  bCarryResource  the kind of resource carried: 0 none, 1 gas, 2 minerals.
  //          DepositInventory reads it and credits gPlayerMinerals on test al, 2h and
  //          gPlayerGas on test al, 1h -- the amount comes from a separate byte at +0xCF --
  //          while AddInventory writes it with setnz al / inc al, i.e. 1 or 2. The load
  //          filters test it as & 3, carrying anything, which is consistent. BYTE at every
  //          one of the ~25 CUnit sites; the dword ptr [x+0E0h] sites in the corpus are
  //          CBullet and other types, not this one.
  /*0xE0*/ BYTE     bCarryResource;
  /*0xE1*/ BYTE     _unmapped_E1[0x01];
  //   +0xE2 / +0xE8 / +0xEC  the background order's cached state. Both background-order
  //          setters zero this byte and the two dwords in the same breath as writing
  //          bBackgroundOrderId (xor eax, eax then three stores), which fixes each width --
  //          1, 4 and 4 -- and nothing else, so all three are named for their offsets.
  //          +0xE2 has a reader after all: DispatchShieldBattery loads it with movzx and
  //          runs a three-way switch, storing 0, 1 and 2 back while driving the shield
  //          battery's recharge animation (CSprite::SetAnimation 0x13 and 0x14). So on a
  //          Shield Battery the slot is a small state machine and the setters' clear is
  //          that state being reset -- a union arm, not a rename: the two readings are about
  //          different unit types and both hold.
  union {
    /*0xE2*/ BYTE   bUnionE2;
    /*0xE2*/ BYTE   bShieldBatteryState;   // 0/1/2 on a shield battery; see above
  };
  //   +0xE3  bUnionE3  named for the offset. Two readers in one slate:
  //          ApproachMoveTarget_xy early-exits on it (cmp byte ptr [ecx+0E3h], 0h / jz) and
  //          CUnit::Stop sets it to 0Fh after clipping the move target. BYTE; all four low
  //          bits set on the write side says a flag nibble at most.
  /*0xE3*/ BYTE     bUnionE3;
  //   +0xE4  dwUnionE4  named for the offset. Its one reader, IsVisibleTo, builds a mask at
  //          runtime from another unit's bPlayer and does test dword ptr [esi+0E4h], edx --
  //          a memory-against-REGISTER test, so the field is tested in place and the width
  //          is proven by the encoding: a narrower field would have to be loaded first and
  //          the oracle never loads it. Read as behaviour it is a per-player bit -- clear
  //          for a player's index means visible to that player, i.e. a detection mask --
  //          but one function exercising one bit does not earn a semantic name.
  /*0xE4*/ DWORD    dwUnionE4;
  //   +0x105  bPathingFlags  BYTE across the whole corpus, not from one site: 21 files
  //          address this offset and every access is byte-wide -- 19 reads of the
  //          test byte ptr [reg+105h], <1|2> form across 27 functions, plus the writes
  //          or byte ptr, and byte ptr, mov byte ptr and xor byte ptr. There is NO wider
  //          access anywhere, and a DWORD declaration emits test dword ptr. The
  //          read-modify-write writers preserve bits they never inspect, which is what
  //          makes it a flags byte; the name is the port's, from the company it keeps (PathInit
  //          and ResetMovementState write it; PathCheckIllegal, PathFixCollision,
  //          PathMoveToLegal, GetCollisionUnit, IsTargetInWeaponRange and
  //          sai_path_can_reach_target read it). The meaning of each bit is not derived.
  /*0xE8*/ DWORD    dwUnionE8;   // see the +0xE2 note above -- cleared, never read
  /*0xEC*/ DWORD_PTR    dwUnionEC;
  //   +0xF0 / +0xF4  the cloak/burrow link, a prev/next pair derived together because
  //          neither slot is nameable alone. The head is 0x665D40 and the two bodies that
  //          maintain it fix which is which: sub_4340D0 prepends, writing the old head into
  //          +0xF4 (so +0xF4 is next) and splicing +0xF0 backwards, and sub_434DA0 unlinks
  //          with the mirror. Pointer width comes from the writes storing a CUnit * and from
  //          EncodeToSave running both slots through the save-time pointer codec. Named
  //          after the sheet's head; no body says which subsystem owns the list.
  /*0xF0*/ CUnit    *pBurrowLinkPrev;
  /*0xF4*/ CUnit    *pBurrowLinkNext;
  //   +0xF8 / +0xFA / +0xFC  the rally point, derived as a triple. SetRallyTarget_xy stores
  //          a WORD to +0xF8 and +0xFA and clears the dword at +0xFC; SetRallyTarget_CUnit
  //          does the mirror, so the pair always mirrors the target and a null argument
  //          rallies the unit to itself. DisplayRallyTarget sign-extends both with movsx, so
  //          they are signed map coordinates, and +0xFC is dereferenced at +0x0C as `this`
  //          is. The same four bytes at +0xF8 are a list link on a pylon, hence the union:
  //          DispatchJuiceInit links the unit into sgJuiceLink_g through +0xF8 and +0xFC.
  //          That union is exactly 4 bytes wide, so it shifts nothing after it, and +0xFC is
  //          a CUnit * under both readings.
  union {
    /*0xF8*/ CUnit *pPsiLinkPrev;        // a pylon's arm of the same four bytes
    struct {
      /*0xF8*/ spos_t wRallyX;  // Hyperion: positions of 4 bytes on x64 (stage E)
      /*0xFA*/ spos_t wRallyY;
    };
  };
  union {
    /*0xFC*/ CUnit *pRallyUnit;
    /*0xFC*/ CUnit *pPsiLinkNext;        // same type, same slot, other list
  };
  //   +0x100  pPath  the unit's sai_path pointer, a dword at every site: sub_42C790 tests
  //          it whole and then flags the struct it points at (or byte ptr [eax+18h], 1),
  //          FixupPathState compares it whole and re-tests that same +0x18 bit, and
  //          PathRepath loads pPath->[10h] as a dword straight into FromIdAlive -- so the
  //          pointee carries a unit id at +0x10 and a flag byte at +0x18. The struct stays
  //          incomplete here; CUnitPath.cpp owns the derived definition.
  /*0x100*/ sai_path *pPath;
  //   +0x104  bPathSubState  BYTE: mov byte ptr [esi+104h], 0Ah is PathFailedPath's
  //          fail-state store, WalkInterrupt compares it unsigned (cmp byte ptr
  //          [ecx+104h], 2h / jbe), and PathRetryPath counts it up with a byte load/inc/
  //          store bounded by cmp al, 14h / jnb -- a retry counter, 0x14 = 20 tries.
  /*0x104*/ BYTE    bPathSubState;
  /*0x105*/ BYTE    bPathingFlags;
  /*0x106*/ BYTE    _unmapped_106[0x0A];
  //   +0x110  wTimeToLive  WORD by every access (movzx eax, word ptr and mov word ptr
  //          [esi+110h], cx in ApplyDarkSwarm, HangarSelfDestruct and StepUpdate). The name
  //          comes from StepUpdate, which decrements it once per frame and fires something
  //          when it reaches zero; the two clamp sites agree, capping it at 0x384 (900
  //          frames) and at a net_randbetween result, both with jnb so a longer timer wins.
  //          It is the first WORD of a larger timer block: CopyStatusEffects moves
  //          0x110..0x11F and 0x120..0x12F with two movdqu pairs, so the whole region is
  //          copied as one unit when status effects transfer. Only this WORD is derived.
  /*0x110*/ WORD    wTimeToLive;
  // Defensive Matrix state, independently read/written by DamageDefMatrix,
  // SyncDefMatrix and UnitApplyDefMatrix. Every +0x112 access is WORD-width;
  // every +0x114 access is BYTE-width.
  /*0x112*/ WORD    wDefMatrixHitPoints;
  /*0x114*/ BYTE    bDefMatrixTimer;
  //   +0x115 / +0x116  bStimTimer / bEnsnareTimer, derived together because it is the pair
  //          that names them. GetAttackSpeedModifier and GetMovementSpeedModifier are the
  //          same body with one immediate changed: a non-zero +0x115 adds 1 to both attack
  //          rate and movement, a non-zero +0x116 subtracts 1 from both. Exactly one effect
  //          does the former (Stim Packs) and one the latter (Ensnare), and
  //          ApplyRestoration independently dispatches +0x116 to EnsnareEnd, a
  //          Blizzard-named callee -- so +0x116 is named by symbol evidence and +0x115 by
  //          elimination. BYTE at both sites, tested only against zero.
  /*0x115*/ BYTE    bStimTimer;
  /*0x116*/ BYTE    bEnsnareTimer;
  //   +0x117 / +0x119 / +0x124  the three is-this-unit-disabled timers, bytes at every site.
  //          bf_battery and bf_tower test all three against 0 in the same order, right after
  //          a dwFlags & 0x400 test. Each offset has its own matched triple of
  //          Blizzard-named callees and the triples do not overlap (LockdownEnd /
  //          SyncLockdown / UnitApplyLockdown, and so on for stasis and paralyze), and
  //          ApplyRestoration agrees from the other side. Blizzard's name is used rather
  //          than the player-facing one: +0x124 is bParalyzeTimer although the ability ships
  //          as Maelstrom. The neighbours at +0x118, +0x11A and +0x126 wait for a body that
  //          needs them.
  /*0x117*/ BYTE    bLockdownTimer;
  /*0x118*/ BYTE    bIrradiateTimer;
  /*0x119*/ BYTE    bStasisTimer;
  // BloodBoilEnd, SyncBloodBoil and UnitApplyBloodBoil all access +0x11A as a single BYTE
  // timer. The adjacent +0x11B byte remains unknown.
  /*0x11A*/ BYTE    bBloodBoilTimer;
  /*0x11B*/ BYTE    _unmapped_11B;
  // IrradiateProc consumes these as the source unit and source-player value
  // when it forwards periodic damage; UnitApplyIrradiate writes the same pair.
  /*0x11C*/ CUnit  *pIrradiateSource;
  /*0x120*/ BYTE    bIrradiatePlayer;
  // A player mask: CBullet::Damage ORs in 1 << bPlayer and ApplyRestoration
  // clears it. That clear only schedules like the oracle through a scalar name.
  /*0x121*/ union {
    /*0x121*/ BYTE  _unmapped_121[0x02];
    /*0x121*/ BYTE  bParasiteFlags;
  };
  //   +0x123  bUnion123  named for the offset. One reader, sub_431A90, tests it against
  //          zero as a fourth disable-style timer in the same && chain as Lockdown (+0x117),
  //          Stasis (+0x119) and Paralyze (+0x124) -- same shape, same width. It is none of
  //          the five named ApplyRestoration enders, so the role is inferred from the
  //          company it keeps and the specific status effect is not derived.
  /*0x123*/ union {
    /*0x123*/ BYTE bUnion123;
    /*0x123*/ BYTE bMyopiaPlayers;
  };
  /*0x124*/ BYTE    bParalyzeTimer;
  /*0x125*/ BYTE    _unmapped_125;
  // ApplyUnitCrunchy increments the count at +0x126 (capped at 9) and chooses one of the
  // nine BYTE timers at +0x127..+0x12F. CrunchyEnd clears exactly those nine bytes with
  // movq plus one byte store, while UpdateCrunchyTimers visits all nine individually.
  /*0x126*/ BYTE    bCrunchyCount;
  /*0x127*/ BYTE    bCrunchyTimers[9];
  /*0x130*/ BYTE    _unmapped_130[0x04];
  //   +0x134  dwUnion134  named for the offset. Its one appearance is a dword truth test in
  //          DispatchIgnoreMove, choosing whether to also clear dwUnion7C when the move
  //          ends. Width proven, meaning not.
  /*0x134*/ DWORD_PTR   dwUnion134;
  // +0x13C / +0x140 / +0x144 / +0x148 are four DWORD unit-finder indices, derived and
  // deliberately not named (from CUnit::mtx_unit_there, 0x413390). Each is loaded whole and
  // then used as an 8-byte-stride index, not a pointer, into one of the two bounds
  // orderings: +0x13C and +0x140 index g_pos_sort_x @0x5C99B8, +0x144 and +0x148 index
  // g_pos_sort_y @0x5D03F8. The axis of each is proven and so is the width; the polarity
  // within a pair -- which is the low edge -- was inferred from the compare senses alone.
  // The extent corroborates the reading: each ordering is 3,400 entries of 8 B, which is
  // 1,700 (the unit cap) x 2 bounds per unit per axis. Not named because all four referents
  // are in one body; a later reader with a second one should name them.
  /*0x138*/ BYTE    _unmapped_138[0x18];

  // dwFlags bits. Nested on purpose: a namespace-scope enumerator is one more global name a
  // reference-tree header could collide with. Each value is the oracle's immediate; each
  // name is a reading.
  //   UF_Building     CanMove returns 0 when it is set; order_pickup picks the building
  //                   cursor pair.
  //   UF_Burrowed     bf_burrowed tests it right after the units.dat can-burrow flag.
  //   UF_InTransport  EnterTransport sets the transport link and this bit back to back.
  // Every referent tests these at BYTE width, so they are low-byte flags, while dwFlags
  // itself is a DWORD -- see the +0xDC note above.
  enum { UF_Building    = 0x00000002,
         UF_Burrowed    = 0x00000010,
         UF_InTransport = 0x00000040 };

  // The member functions. All are thiscall: `this` arrives in ECX and every declared
  // argument is on the stack, and /Gr (default __fastcall) does not change that for a
  // non-static member. IsGoliathType and CreateOverlay are defined out of line in
  // src117/lang/CImage.cpp; a declaration with no definition in src117 resolves through the
  // generated oracle thunk. No body lives in this header.

  // oracle 0x40FD10 (23 B). `this` in ECX, no stack args, bare ret, so no arguments and
  // thiscall; EAX is 0 or 1. Reads only wType, so it could be const -- see CanUseStim below
  // for why none of these is.
  BOOL IsGoliathType() const;

  // oracle 0x422AE0 (20 B). Tail-jmps into CSprite::AllocImageAbove with no ret of its own;
  // the callee's ret 10h retires the frame for both. That sibling call is only legal because
  // the two argument lists are identical, so AllocImageAbove's 0x10 bytes -- four dword
  // slots -- are this function's argument list. The 4th type is int and x and y are char;
  // the derivation is in src117/h/CSprite.h, since this body forwards the slots untouched
  // and only ever showed that the two signatures agree. Widening one alone costs the tail
  // call: measured at 47 B against the oracle's 20.
  void CreateOverlay(int imgNo, char x, char y, int nDirection);

  // oracle 0x4383D0 (13 B), whole body: xor eax,eax / cmp dword ptr [ecx+8h], 0A00h /
  // setnle al / ret. `this` in ECX and a bare ret, so thiscall with no stack arguments, and
  // EAX is 0 or 1. It reads only [ecx+8] (hit points, x256 -- 0xA00 is 10 HP) and writes
  // nothing, so it could be const. It is not, because const is part of the C++ mangling and
  // the call site in order_stim (0x47C710) is matched against the oracle's own callee
  // symbol; adding const later is a re-gate, not a comment change.
  BOOL CanUseStim();

  // The Terran spell and status family at 0x4384C0..0x438AF0. Parameter widths follow each
  // callee's ret-N and operand width; notably the lockdown, myopia and paralyze values
  // occupy full dword stack slots even where AL is later consumed.
  void DamageDefMatrix(int nDamage);
  void DispatchDefMatrix();
  void IrradiateDamage();
  void IrradiateEnd();
  void LockdownEnd();
  void ParalyzeEnd();
  void SyncDefMatrix();
  void SyncIrradiate();
  void SyncLockdown();
  void SyncParalyze();
  void UnitApplyDefMatrix();
  void UnitApplyIrradiate(CUnit *pSource, BYTE bPlayer);
  void UnitApplyLockdown(int nTime);
  void UnitApplyMyopia(int nPlayer);
  void UnitApplyParalyze(int nTime);
  void UnitApplyStim();
  // 0x4383E0 (219 B), unported. Nullary thiscall; CBullet::Damage discards EAX.
  void ApplyRestoration();
  // 0x431400, unported. Static: ECX is the save file. saveload_write_save_data
  // tests the full EAX.
  static BOOL WriteSave(void *pFile);
  // 0x4202C0, CUnitCombat.cpp. Static __fastcall: source and target in ECX/EDX, ret 8h.
  // The sheet's name, which sorts between MaxToughness and OnDamaged.
  static void __fastcall NewTarget(CUnit *pSource, CUnit *pTarget, int a3, int a4);

  // The Zerg spell and status family at 0x43DB50..0x43E409. The four location dispatchers
  // are static fastcalls: x and y arrive in ECX/EDX and the remaining context occupies
  // stack slots, as the CBullet::Damage call sites fix. The three search callbacks likewise
  // take their candidate and context in ECX/EDX.
  static void __fastcall ApplyBloodBoil(int x, int y, CUnit *pAttacker);
  static void __fastcall ApplyCrunchy(int x, int y, int nPlayer,
                                     CUnit *pIgnoredTarget);
  static void __fastcall ApplyDarkSwarm(int x, int y, BYTE bPlayer);
  static void __fastcall ApplyEnsnare(int x, int y, CUnit *pAttacker);
  static int __fastcall BloodBoilProc(CUnit *pCandidate, CUnit *pAttacker);
  static int __fastcall CrunchyApplyProc(CUnit *pCandidate, int nPlayer);
  static int __fastcall EnsnareProc(CUnit *pCandidate, CUnit *pAttacker);
  void ApplyConsume(CUnit *pTarget);
  void ApplyUnitCrunchy();
  void BloodBoilDamage();
  void BloodBoilEnd();
  void CrunchyEnd();
  void EnsnareEnd();
  void HideSpells();
  void SyncBloodBoil();
  void SyncCrunchy();
  void SyncEnsnare();
  void UnitApplyBloodBoil();
  void UnitApplyEnsnare();
  void UnitApplySpawnBroodling(CUnit *pTarget);
  void UpdateCrunchyTimers();

  // Unported callees the spell family reaches.
  void RemoveImageOverlay(int nFirstImageId, int nLastImageId);
  int sub_423150(int a1);
  BOOL sub_434B80(int a1, int a2, int a3, WORD *pCost);
  void SubtractEnergy(WORD wEnergy);

  // Unported callees the CUnitUpdate.cpp dispatch family reaches.
  //
  // oracle 0x431CE0, FindHealTarget. Thiscall, no stack args. The return is a pointer and
  // not BOOL: the frame probes mov eax, 1AACh / call __chkstk (a large local array), and at
  // its one early exit EAX is the tested-and-false path value 0. The callers settle it --
  // DispatchMedic does mov edi, eax and consumes it as a unit pointer through three
  // successive callees, and DispatchMedicHoldPos stores it straight into pOrderTarget.
  CUnit *FindHealTarget();
  // oracle 0x437440, unported. One pushed dword the body dereferences as a CUnit *; every
  // oracle caller follows the call with test eax, eax.
  BOOL CanEnterTransport(CUnit *pTarget);
  // oracle 0x43A270, unported, CUnit::SetUnitPosition. Two stack slots, cleaned by the
  // callee (ret 8h). WORD, WORD: the callee reads its slots at WORD width (mov ax, word ptr
  // [ebp+8h] at 0x43A276 and 0x43A281), and for a parameter the callee is the authority --
  // only the declared type expresses a narrow slot read. The caller pushes whole dwords,
  // which is width-independent.
  void SetUnitPosition(upos_t nX, upos_t nY);  // Hyperion: positions
  // oracle 0x430F60, unported. Thiscall with one stack arg (ret 4h), dereferenced as a
  // CUnit *; its first reader is test dword ptr [esi+0DCh], 40000000h against that arg.
  void sub_430F60(CUnit *pTarget);
  // oracle 0x4371A0, unported. Thiscall with one stack arg -- both medic callers load push
  // dword ptr [esi+5Ch] (the pOrderTarget slot) then mov ecx, esi. The return is an
  // ordinal: DispatchMedicHoldPos compares it against 3 with jnbe and indexes a jump table
  // on it, so a small non-boolean code; int until its own body narrows it.
  int sub_4371A0(CUnit *pTarget);
  // oracle 0x4AF810 is a free function and lives in src117/h/TAI_Captain.h as
  // tai_soldier_unload_into_neighbor(CUnit *), not as a member here: the ledger and the map
  // both name it free and its oracle TU is TAI_Captain.cpp. A mov ecx, p / call site cannot
  // tell a thiscall member from a free __fastcall(ptr), and the byte comparison de-classes
  // Class::sub_ names on both sides, so a member declaration here matched while emitting a
  // mangled name no caller could link -- LNK2019 the day DispatchUnload landed.

  typedef int (__fastcall *TransportProc)(CUnit *pUnit, CUnit *pContext);
  BOOL IterateTransport(TransportProc pfnProc, CUnit *pContext);
  // The fourth parameter is one BYTE wide, which a contrast inside one call proves rather
  // than anything in 0x41DFC0's own body. In DamageFromBullet (0x41E290) argument 4 stores
  // one byte over a dead four-byte slot that still holds the upper three bytes of pBullet
  // and pushes the whole dword, exactly as bWeapon (a known BYTE) does, where the int
  // parameter at argument 6 is pushed from a widened register. MSVC emits that only when
  // the callee's parameter is one byte.
  //
  // The name ShieldEffect is unproven and probably wrong, and is left rather than replaced
  // with a second guess: every referent that carries a value here carries CTarget +0x21, the
  // byte DamageNotify's third parameter is named bDirection for.
  void Damage(int nDamage, weapon_id_t bWeapon, int nDivisor, BYTE bShieldEffect,
              CUnit *pSource, int nSourcePlayer);
  void sub_434C60();
  void DestroyOverlays(int nFirstImageId, int nLastImageId);
  void DamageApply(int nDamage, CUnit *pSource, int nSourcePlayer, int a4);
  void ResetMovementSpeed();
  int GetMaxEnergy();
  // WORD: all seven callers read AX, and an int return is if-converted to cmov.
  WORD GetMaxTimeToLive();
  BOOL HasImageOverlay(int nImageId);

  // oracle 0x428BD0 (59 B). `this` in ECX and nothing else is set up at any call site, so
  // thiscall with no arguments.
  // The return is int, and for a return width the callers decide. Two of them store the
  // full EAX with no zero-extension -- CImage::EncodeListForSave (0x40F000) into
  // coloringData and sai_write_banker_data into its own table -- which a WORD return cannot
  // produce (measured: WORD costs a movzx eax,ax the oracle does not have). The net_cmds.cpp
  // callers store the result into WORD packet fields and are byte-identical at either width.
  // The body composes (byte[this+0A5h] << 11) | index, up to 19 significant bits, and the
  // packet keeps only the low 16: that truncation is 1.17's own, at the call site.
  int GetUnitID();

  // oracle 0x41A340. Thiscall: both call sites (recv_net_cmd_Kill, recv_net_cmd_KillEgg)
  // set only ECX, and the oracle tail-JMPs to it from a void function, so no result is
  // consumed.
  void CancelBuild();

  // oracle 0x43ABE0 / 0x43AB80 / 0x435A40, for the recv_net_cmd_Stop{Upgrade,Research} and
  // recv_net_cmd_CancelNuke receivers. All three are reached as mov ecx, <unit> / call with
  // no stack argument set up, and every call site is a statement whose result is dead --
  // two are immediately followed by the caller's pop esi / jmp status_set_update epilogue,
  // which cannot inspect EAX. So thiscall, no arguments, void.
  void CancelUpgrade();
  void CancelResearch();
  void CancelNuke();

  // oracle 0x43AF40 / 0x43AFB0, for recv_net_cmd_Research and recv_net_cmd_Upgrade. Thiscall
  // with one stack argument, widened from the packet's payload BYTE by the caller, and the
  // result is consumed: test eax, eax / jz gates the follow-up SetOrder.
  // The parameter is BYTE and the callee is the authority. Both bodies open
  // mov bl, byte ptr [ebp+8h], which no int spelling reaches -- a cast at the use is a
  // no-op subregister view MSVC coalesces with the parameter, keeping the slot's width.
  // Either width leaves both net_cmds.cpp call sites unchanged, so no caller moves.
  BOOL StartResearch(tech_id_t bTechId);
  BOOL StartUpgrade(upgrade_id_t bUpgradeId);

  // oracle 0x437E00, for recv_net_cmd_ExitTransport. Thiscall with no arguments: no stack
  // setup at any call site and a bare ret. The return is BOOL and not void, which changes
  // the mangled name, so getting it wrong makes every caller reference a symbol the
  // generator never thunks; all four exits set EAX to a literal and DispatchUnload consumes
  // it. __declspec(noinline) is needed because DispatchUnload lives in the same TU and calls
  // this with a real call: without the directive /Ob2 splices all 372 bytes in. It
  // constrains callers only and leaves this function's own COMDAT byte-identical.
  __declspec(noinline) BOOL ExitTransport();

  // The build-queue and toggle-order operations the recv_net_cmd_* receivers reach. Still
  // sub_-named: the archaeology sources do not name them, and inventing a semantic name is
  // worse than the address. Declared here rather than through a stand-in class on purpose:
  // the byte comparison de-classes Class::sub_XXXXXX on both sides, so it cannot see a body
  // calling one of these through the wrong class -- it proves clean, and then the generator
  // never thunks the mangled name.
  // 0x41B810. ret 4h => one stack argument, read mov edi, dword ptr [ebp+8h] and bounds-
  // checked cmp edi, 4h / jnbe -- unsigned, and 4 is the last index of the 5-slot build
  // queue, so an int index and not a unit type. The caller tail-jumps away from it, so no
  // result is consumed.
  void RemoveBuildQueue(int nQueueSlot);

  // 0x41B6F0. Bare `ret` and nothing pushed at the call site => no arguments.
  void sub_41B6F0();

  // 0x41B4B0. ret 4h => one stack argument. The slot is A DWORD even though the value is
  // used narrowly: the body reads it both as movzx eax, bx into gulUnitTypeFlags and as the
  // WORD it stores into the build queue, and a WORD parameter would make the caller emit
  // the narrowing instead. Result consumed -- test eax, eax / jz at both call sites, exits
  // xor eax,eax and mov eax, 1h.
  BOOL AddBuildQueue(int nUnitType);

  // 0x42A880. ret 0Ch => three stack dwords, and the body examines none of them: it forwards
  // all three into sub_42A8B0. arg1 is a normalised 0/1, arg2 an order id, and arg3 a
  // CUnit * settled by the callee chain rather than by the one site that pushes a literal 0
  // -- a pure dword forward is identical under both spellings, so only reading catches a
  // wrong type. nOrderId is a BYTE, and it moves together with sub_42A8B0's arg3 and
  // sub_42A850's p2: narrowing this one alone regrows the body from 44 bytes to 48, which is
  // the cost of one link of a forwarding chain disagreeing with the next. The affirmative
  // evidence is caller-side, as no landed body in the chain reads the value narrowly:
  // recv_net_cmd_HoldPosition stores a byte and pushes the slot whole with stale upper bytes.
  void sub_42A880(int nEnable, order_id_t nOrderId, CUnit *pTarget);

  // oracle 0x427E60 (43 B). Reads dwFlags (test byte ptr [esi+0DCh], 2h -- see the
  // UF_Building note above), returns 0 when the bit is set, otherwise calls GetRClickAction
  // twice and returns action != 3. Thiscall, bare ret, EAX in {0,1}. Not const for the same
  // mangling reason as CanUseStim.
  BOOL CanMove();

  // oracle 0x435990 (46 B), for bf_stop_addon (0x49F5F0). `this` in ECX, no stack argument
  // set up at the call site and a bare ret, so thiscall with no arguments. The two exits
  // are mov eax,1 and xor eax,eax.
  BOOL IsConstructingAddon();

  // oracle 0x407B10 (51 B). Thiscall with no arguments: `this` in ECX (mov esi, ecx on
  // entry), no stack argument at either call site, bare ret. The push 0h in the body is not
  // this function's argument -- it belongs to the GetBuildUnit(0) call on the next line,
  // which is exactly the misread the bare ret rules out. Two exits: mov eax, 1h and the
  // fallthrough EAX of IsBuildingProtoss.
  BOOL IsConstructing();

  // oracle 0x428FE0 (62 B). Thiscall with no arguments: `this` in ECX (mov edi, ecx), no
  // stack argument at the call site, bare ret.
  // The return is a signed int and the division proves it. The value-producing tail is
  // imul eax, ecx, 64h / cdq / idiv esi -- cdq and idiv, not xor edx,edx and div. An
  // unsigned return would emit the latter, and a BYTE or WORD return would discard a
  // quotient deliberately computed at 32 bits (x100 before the divide).
  int GetBuildPercentage();

  // oracle 0x4118D0 (38 B). Nine instructions, no prologue: `this` is dereferenced straight
  // out of ECX at entry, nothing is set up at the call site, and the body ends on a bare
  // ret, so thiscall with no arguments. The two exits are mov eax, 1h and xor eax, eax.
  // The two masks it tests are also run, in the same order, by the matched
  // ai_count_collateral_energy: gulUnitTypeFlags[wType] & 0x200000 then dwFlags &
  // 0x40000000. Neither bit is named here -- 0x200000 is a units.dat column and not a CUnit
  // flag at all -- so they stay raw masks until a session earns the names.
  BOOL HasEnergy();

  // oracle 0x41B300 (35 B). ret 4h => exactly one stack argument, read as add eax, dword
  // ptr [ebp+8h] after a movzx -- int-width, and an offset into the build queue (the body
  // computes (queueHead + n) % 5). The return width is 16 bits and it matters: the last
  // value-producing instruction is mov ax, word ptr [ecx+edx*2+98h], a WORD load into AX
  // that leaves the upper half of EAX untouched, and bf_stop_unit consumes it with a 16-bit
  // compare. int would make that caller compare at 32 bits and lose the match.
  WORD GetBuildUnit(int nIndex);

  // oracle 0x436F60 (84 B). ret 8h => two stack arguments: [ebp+8h] is truth-tested, so an
  // int-width flag, and [ebp+0Ch] is compared against a CUnit *. Returns the matching unit
  // or xor eax,eax. Both arguments are 0 at the one landed call site, so neither name is
  // derived -- only the widths and the pointer-ness are.
  CUnit *FindArmedSilo(int nFlags, CUnit *pExclude);

  // The gpStatUnits-loop filters. All five: `this` in ECX, no stack argument set up at any
  // call site, and a bare ret in the callee. What differs between them is the return width,
  // read off each body's own last value-producing instruction plus how its callers consume
  // it. None is declared const.

  // oracle 0x422D90 (63 B). One BYTE, and not a boolean: the exits are mov al, 3h and mov
  // al, bl, both 8-bit writes that leave the upper 24 bits of EAX untouched, and the values
  // seen are 0, 2 and 3 -- a right-click action code, not a flag. Every caller reads it
  // 8-bit (bf_can_move_stop test al, al; bf_can_move_patrol adds cmp al, 3h), and CanMove
  // returns action != 3. int would make MSVC widen at the call site and lose those matches.
  BYTE GetRClickAction();

  // oracle 0x438260 (48 B). Returns in EAX at full width -- the taken exit is mov eax, 1h
  // and the fallthrough leaves FromIdAlive's own zero -- and bf_trans_has_passenger consumes
  // it with test eax, eax. Scans eight WORD unit ids at [this+0B0h..0BFh].
  BOOL IsUnitInside();

  // oracle 0x4381F0 (61 B). xor eax,eax / setnz al and an early xor eax, eax, so EAX is
  // fully defined 0 or 1; bf_trans reads it with test eax, eax.
  BOOL IsATransport();

  // oracle 0x4382F0 (81 B). Ends sbb eax,eax / neg eax -- the full-width 0/1 idiom -- and
  // bf_trans reads it with test eax, eax.
  BOOL HasEmptyCargoSpace();

  // oracle 0x41D9C0 (133 B). Exits mov eax, 1h, xor eax,eax and setnz al over a zeroed EAX,
  // so full-width 0/1; bf_can_attack consumes it with test eax, eax.
  BOOL CanAttack();

  // Unported callees the AI spell-target scorers in CUnitSearches.cpp reach. Both were
  // verified against the oracle before being written here: a wrong declaration is an
  // overload rather than a conflict, so the TU still compiles, the byte comparison still
  // reports a match, and the generator emits a hook for a symbol with no body.

  // oracle 0x438140 (58 B), oracle TU CUnitTransport.cpp. Thiscall with a bare ret -- no
  // stack slots -- so `this` in ECX is its only input.
  // Walks this unit's cargo array and returns the first entry still alive: wType indexes a
  // per-type cargo capacity at 0x523FE0, then it walks wCargoUnitIds[8], the field this
  // header names at +0xB0, handing each WORD id to FromIdAlive and returning the first
  // non-null. The no-cargo and nothing-alive paths both fall into xor eax, eax.
  // Returns CUnit *: the value IS FromIdAlive's result passed straight through, and the
  // caller (ai_test_best_disruptor_target) dereferences it at +0x5C after a null test.
  CUnit *GetFirstTransportUnit();

  // oracle 0x41FD10 (114 B), oracle TU CUnitCombat.cpp. Thiscall, and every exit is ret 4h,
  // so exactly one pushed argument besides the register `this`. That argument is a CUnit *:
  // the callee dereferences it at +0xDC (dwFlags) and +0x0C (pSprite), which is the
  // authority for a parameter.
  // Returns int, not BOOL, and only the callee can say so. Its two exits are mov eax, 77h /
  // cmp edi, eax / cmovnbe eax, edi -- max(edi, 0x77) -- and a bare mov eax, edi, where EDI
  // carries sub_41FA00's result. That is a magnitude clamped at 119, not a normalised 0/1;
  // the one caller only tests the result against zero and so discriminates nothing.
  int GetChanceToHit(CUnit *pTarget);

  // Callees of count_completed (src117/lang/TrigEnum.cpp).

  // oracle 0x4381D0 (23 B). push edi / mov ecx, esi / call -- `this` in ECX with one stack
  // argument, so thiscall and not __fastcall; the pushed value is a 0..7 loop counter,
  // int-width. The result is null-tested and then used as ECX for a recursive
  // count_completed, so a CUnit *. Returns the cargo unit in that transport slot, or null.
  CUnit *GetTransportUnit(int nIndex);

  // oracle 0x41CC20 (39 B). mov ecx, esi / call with NO push, so no arguments. The return
  // width is 16 bits and it matters: the caller does movzx eax, ax on the result before
  // using it as a unit-type row index, so the callee leaves only AX defined, and int would
  // drop that movzx. Maps a hangar unit (Carrier, Reaver and their heroes) to the unit type
  // its hangar holds.
  WORD GetHangarType();

  // The per-unit trigger-action setters reach these (src117/lang/TrigAct.cpp: set_energy,
  // set_hangar, set_owner_inside, set_resources_unit). All thiscall, with `this` in ECX and
  // every declared argument on the stack. None is declared const.

  // oracle 0x4293F0 (9 B), whole body call CUnit::GetMaxEnergy / sar eax, 8h / ret.
  // Thiscall with no arguments. The result comes from an arithmetic (signed) shift of the
  // full EAX, so int-width and signed -- the x256 fixed-point maximum converted back to
  // whole energy points, which is what Real names. set_energy calls it three times and MSVC
  // CSEs none of them, which is itself a sign it is a real out-of-line call.
  int GetMaxEnergyReal();

  // oracle 0x428DF0 (369 B). ret 4h => exactly one stack argument, read as a full dword
  // (mov ebx, dword ptr [ebp+8h]) and only then narrowed by the body itself with movzx edi,
  // bl -- an int parameter the callee truncates, not a BYTE parameter. Nothing consumes its
  // result at either call site, so void.
  void GiveToPlayer(int nPlayer);

  // oracle 0x424380 (33 B). mov ecx, esi / call with no push, so no arguments. Its exits
  // are mov eax, 1h and xor eax, eax at full width, and set_resources_unit tests it.
  BOOL IsMineralSrc();

  // oracle 0x4247B0 (85 B). mov ecx, esi / call with no push, so no arguments; its own ret
  // is bare and its result is never read. Re-picks the mineral-patch animation from
  // [this+0D0h] -- the same bytes wResourceAmount names, which is where that arm's 16-bit
  // width is corroborated from the other direction.
  void UpdateResourceAnim();

  // oracle 0x424DA0 (16 B), whole body: push esi / mov esi, ecx /
  // call CUnit::FinishConstruction / mov ecx, esi / pop esi / jmp CUnit::Complete.
  // Thiscall, no stack argument at the call site, and it tail-jmps into a callee that
  // cleans nothing.
  void FinishedMaking();

  // oracle 0x41CC80 (80 B). ret 4h => exactly one stack argument, read as mov esi, dword
  // ptr [ebp+8h] and then used as the ECX of CUnit::Hide and written at +0xC4/+0xC8/+0xCC,
  // so a CUnit *. Result never read. Links a freshly created fighter into this carrier's
  // hangar list.
  void HangarConstructionFinished(CUnit *pUnit);

  // src117/lang/CUnitMisc.cpp -- the bodies that TU defines and the callees it reaches, in
  // one block: a name here has a body in CUnitMisc.cpp, or it is marked callee-only and
  // resolves through the generated thunk until some session writes it.
  // Every convention below is read off a call site, never assumed. `this` in ECX with each
  // declared argument pushed is thiscall, which is what a plain non-static member gets
  // under /Gr. The argument count is the push count at the call and, where the callee is
  // unported, the ret N that balances it. None is declared const.

  // Defined here.

  // oracle 0x427DE0 (116 B). Computes the per-tick shield regain for a unit
  // under construction and stores it to wShieldGain; also seeds lShields.
  void AssignShieldGain();

  // oracle 0x427E90 (50 B) / 0x428A80 (62 B). The doodad-state pair. Both test
  // dwFlags & 0x400 and the three disable timers, then issue SetOrder with
  // 0xAD / 0xAC respectively; Enable additionally clears the 0x400 bit with the
  // dwFlags value it already had in EAX.
  void DisableDoodadState();
  void EnableDoodadState();

  // oracle 0x428150 (77 B) / 0x4281A0 (76 B). The two door orders. Both gate on
  // bOrderFlags bit 0, clear it, move the sprite's elevation, re-issue the
  // type's guard order and tail-jump onward -- Close into
  // CUnit::UpdateUnitCollision, Open into ai_mark_targets_stale.
  void DispatchDoorClose();
  void DispatchDoorOpen();

  // oracle 0x4285A0 (178 B) / 0x4288A0 (80 B) / 0x428660 (133 B). The three
  // two-state order handlers; see bOrderState above for the shared opening.
  void DispatchHideTrap();
  void DispatchRevealTrap();
  void DispatchIgnoreMove();

  // oracle 0x428A10 (23 B) / 0x428A30 (65 B).
  void DispatchStop();
  void DispatchWatchTarget();

  // Order handlers 0x427ED0 .. 0x4288F0, defined in CUnitMisc.cpp. Nullary
  // thiscall with a bare `ret`; reached only through the order table.
  void DispatchCritter();
  void DispatchDisableDoodad();
  void DispatchEnableDoodad();
  void DispatchFollow();
  void DispatchHiddenGun();
  void DispatchJunkYardDog();
  void DispatchStayInRange();

  // oracle 0x41EBA0 (55 B) / 0x41EA40 (350 B), oracle TU CUnitCombat.cpp. The two nuke-order
  // handlers, both thiscall with no argument: DispatchNukeUnit is entered mov esi, ecx with
  // nothing pushed, and DispatchNukeGround is reached by tail jump from it with ECX reloaded
  // to `this` first, which is the same contract. Void, and the tail jump is why that has to
  // be stated rather than inferred: a tail-jumped callee shares its caller's return, so the
  // jmp says nothing about EAX. Neither body defines EAX on any exit.
  void DispatchNukeUnit();
  void DispatchNukeGround();

  // Three more order dispatchers, all thiscall / no argument / no result: `this` in ECX with
  // nothing pushed at every call site, and exits that are either a bare ret or a tail jump
  // (jmp CUnit::EndDispatchOrder, jmp CUnit::StopUnitMovement) -- and only a void function
  // can be tail-called from, since the jump donates its own return.
  void DispatchAttackMoveEP();    // 0x41E510 (174 B)
  void DispatchGuardBunker();     // 0x41E760 (140 B)
  void DispatchHoldPosition();    // 0x41E9B0 (143 B)
  void DispatchHarassMoveEP();   // 0x41E890 (276 B)
  void DispatchTurretAttack();   // 0x41F170 (393 B)

  // The two AI-rescue dispatchers, seated as members so the symbol agrees with the name
  // authority: declared free, their mangled names matched no ledger row, so those rows had
  // nothing to compare against at all. The move is byte-neutral, since under /Gr a free
  // __fastcall(CUnit *) and a thiscall both ride the pointer in ECX. Both are void and
  // nullary on the dispatch-family reading above, entered with `this` in ECX and leaving on
  // a bare ret or a tail jump; DispatchComputerAI_Bunker is the whole shape in 14 bytes.
  void DispatchRescuePassive();        // 0x48D120 (423 B)
  void DispatchComputerAI_Bunker();    // 0x48D2D0 (14 B)

  // The other three SAI_Main.cpp dispatchers, seated for the same reason: the ledger keys
  // them CUnit::, so a free spelling produces no verdict at all. All three are nullary and
  // void -- `this` in ECX, nothing pushed, every exit a bare ret or a tail jump.
  void DispatchComputerAI();           // 0x48CD70 (364 B)
  void DispatchAIPatrol();             // 0x48CEE0 (207 B)
  void DispatchNeutral();              // 0x48CFB0 (364 B)

  // 0x4B1FE0 (41 B), TU TAI_GuardPost.cpp. Nullary and void: all four callers
  // set only ECX and discard EAX, and the body tail-jumps CUnit::MakeGuardPost.
  void AttachAiRecord();

  // Six more order dispatchers, all thiscall / no argument / no result on the footing of
  // the three above: `this` in ECX with nothing pushed, and exits that are a bare ret or a
  // tail jump -- EndDispatchOrder (DispatchAttackFixedRange, DispatchGuardPost,
  // DispatchSapLocation), Suicide (DispatchSapLocation), Destroy and DestroyImpl
  // (DispatchDie, which has no ret instruction at all). Only a void function can be
  // tail-called from, since the jump donates its own return.
  void DispatchGuard();               // 0x41E700 (83 B)
  void DispatchAttackMove();          // 0x41E4A0 (102 B)
  void DispatchAIReturn();            // 0x41E370 (138 B)
  void DispatchAttackFixedRange();    // 0x41E400 (145 B)
  void DispatchGuardPost();           // 0x41E7F0 (156 B)
  void DispatchSapLocation();         // 0x41EEB0 (191 B)
  void DispatchSapUnit();             // 0x41EF70 (274 B). void:
                                      // three tail jmps (EndDispatchOrder twice,
                                      // Suicide once), and only a void function
                                      // can be tail-jumped from a void one.
  void DispatchDie();                 // 0x41E630 (196 B)

  // 0x41DCE0 (84 B). Thiscall, no argument, bare ret. BOOL and not void: the two exits are
  // mov eax, 1h and xor eax, eax, full-width 0/1, and FighterReturn truth-tests the result
  // -- call CanUseAutoTarget at 0x41CB36 followed by test eax, eax / jne. It is the only
  // caller in the corpus.
  BOOL CanUseAutoTarget();

  // Order-dispatch handlers for the medic and support, zerg lifecycle and transport
  // load/unload group. All thiscall / no argument / void on the footing of the blocks above:
  // `this` in ECX with nothing pushed at every oracle call site, exits a bare ret or a tail
  // jump into EndDispatchOrder. DispatchMedic is not here -- it has a callee-only
  // declaration below, and the ledger assigns its row to CUnitUpdate.cpp.
  void DispatchMedicHeal();           // 0x436250 (177 B)
  void DispatchMedicHoldPos();        // 0x436430 (218 B)
  void DispatchMindControl();         // 0x430CE0 (173 B)
  void DispatchZergBldgMorph();       // 0x43BE80 (139 B)
  void DispatchInfest();              // 0x43CF10 (198 B)
  void DispatchInfestEnd();           // 0x43CFE0 (237 B)
  void DispatchEnterTransport();      // 0x4376D0 (198 B)
  void DispatchLoadUnitTransport();   // 0x4377A0 (163 B)
  void DispatchUnload();              // 0x437BA0 (198 B)

  // The click and per-race build dispatch family: nullary thiscall, bare ret, EAX inherited
  // and discarded.
  void DispatchLClick();              // 0x427C90 (252 B)
  void DispatchBuildProtoss();        // 0x42EE00 (520 B)
  void DispatchBuildSelfProtoss();    // 0x42F020 (374 B)
  void DispatchProtossWarpIn();       // 0x42F1B0 (118 B)
  void DispatchPylonBuild();          // 0x42F230 (11 B)
  void DispatchRClick();              // 0x431200 (128 B)
  void DispatchBuildAddon();          // 0x4355E0 (213 B)
  void DispatchNukeTrain();           // 0x4356C0 (109 B)
  void DispatchPlaceAddon();          // 0x435730 (597 B)

  // 0x431160 (152 B). The rank string for the unit's type, or NULL; three arms tail-jump
  // status_get_str.
  char *GetRankStr();

  // 0x429B70 (78 B). ret Ch; the order id is arg0 and one byte wide.
  void SetNextOrder_xy(order_id_t bOrderId, int nX, int nY);

  // 0x41BCB0 (179 B). Every ret materialises 0 or 1 and both consuming callers read the
  // full 32 bits.
  BOOL sub_41BCB0();

  // 0x4249E0 (32 B), oracle TU CUnitInit.cpp -- an unported callee AbandonTransport reaches,
  // verified against the oracle before being written here. Bare ret, no stack slots, so a
  // nullary thiscall, and void: EAX at the ret is an intermediate the three call sites
  // inside AbandonTransport discard. The body slides gulUnitTypeFlags[wType] bit 0x1B down
  // onto dwFlags bit 0x11 (shr eax, 0Ah is that alignment, not a bit index), and the
  // xor/and/xor triple sets that flag to the table's value rather than toggling it.
  void ApplyPrototypeFlags();

  // The rest of src117/lang/CUnitTransport.cpp. Every signature is read off the oracle
  // before it is written: ret N, which of ECX/EDX is a real live-in, each stack slot's read
  // width and sign, and what the callers do with EAX. A wrong declaration is an overload
  // the byte comparison cannot see.

  // 0x438230 (33 B). ret 4h => thiscall with one stack argument. The argument is the
  // transport and `this` is the cargo, which is the opposite of what the name suggests;
  // the two table indices settle it -- `this` indexes gubUnitVolume (what a unit occupies)
  // and the argument indexes gubUnitCargoHold (what a transport provides), and the callee's
  // own dereference at +0x64 is the authority for the parameter being a CUnit *. BOOL: the
  // tail is cmp al, ... / sbb eax, eax / inc eax and the one caller consumes all 32 bits.
  BOOL CanFitInTransport(CUnit *pTransport);

  // 0x438350 (53 B). Bare ret, no stack slots. Sums gubUnitVolume over the eight
  // wCargoUnitIds entries still alive. int, not BOOL: the value is an accumulated magnitude
  // and all three consuming callers use the full EAX without narrowing it.
  int GetTotalOccupiedCargo();

  // 0x438390 (64 B). Bare ret, no stack slots. Walks the eight cargo slots testing
  // gulUnitTypeFlags bit 3. BOOL: both exits materialise a literal and the single consuming
  // caller reads the full EAX.
  BOOL TransportHasWorker();

  // 0x438290 (82 B). Bare ret, no stack slots. The same eight-slot walk, but it follows
  // each worker's +0xC0 link and compares that unit's wType against 0xD7. No direct caller
  // in the corpus; both exits are literals, so BOOL states the exits without claiming one.
  BOOL IsFlagInside();

  // 0x437850 (58 B) / 0x437B50 (66 B). Order dispatchers: nullary thiscall, and neither has
  // a ret of its own -- each leaves through a tail jmp to EndDispatchOrder /
  // ManageQueuedOrders. A tail jmp pins the return type as well as the arity, and both
  // targets are void.
  void DispatchLoadUnitBunker();
  void DispatchPickUp();

  // 0x437890 (362 B). Nullary thiscall, bare ret. EAX at the ret is whatever
  // ManageQueuedOrders left -- inherited, not materialised -- and no caller reads it.
  void DispatchMoveUnload();

  // 0x437320 (278 B). ret 4h => one stack argument. Void: EAX at the ret is inherited from
  // a sub-call and both call sites (sub_425D10, ExitTransport) discard it.
  // The argument is declared int deliberately. The slot is read 4-byte six times and also
  // written, and a written parameter slot is one MSVC has reused as a spill, so the reads
  // do not all belong to the parameter and cannot be used to claim a pointee. int is 4
  // bytes in the slot either way; whoever lands this body supplies the real type.
  void AbandonTransport(int nArg0);

  // 0x437F80 (436 B). __fastcall on a member: ECX is `this` (dereferenced at +0x4Ch and
  // handed to GetPlayerUnitList) and EDX a second register argument written through before
  // anything else, so it is an out-parameter. /Gr makes __fastcall the default for free
  // functions only, so the keyword is needed here. The out-parameter is a CUnit **: the body
  // clears it and stores the walk cursor into it, and DispatchLoadUnit passes the address of
  // its own pOrderTarget. The return is BOOL -- [ebp-4h] is seeded zero, set to 1 when a
  // candidate survives CanEnterTransport, and returned. A pointer and a BOOL are both 4
  // bytes in EAX, and a CUnit ** and an int * both 4 in EDX, so a body written against the
  // wrong signature still matched 436/436.
  BOOL __fastcall FindClosestTransport(CUnit **ppBestTransport);
  // The per-frame update and movement core, sharing CUnitUpdate.cpp with the dispatch
  // pipeline above. Nullary on every one: each prologue is push esi / mov esi, ecx (or the
  // push ebp form) and no call site pushes.
  void UpdateFlash();                 // 0x43A580 (107 B), `push ebp; mov edx, ecx`
  void StepUpdate();                  // 0x43A320 (367 B)
  // 0x439420 (1437 B). Blocked from landing -- three two-level jump tables (a byte-index
  // table plus jmp dword ptr [eax*4+...] at +0x04, +0x1F and +0x9B) put un-relocated index
  // bytes inside its own COMDAT. Declared anyway: UpdatePrimary and EntityUpdate call it.
  void RunOrders();
  void RunBGOrders();                 // 0x439310 (124 B)
  // 0x43A720 (78 B). Nullary the same way; recurses on its subunit.
  void UpdatePrimary();
  // 0x438EF0 (1048 B). Step every used unit once per frame. Static and nullary:
  // its one call site loads no ECX and discards EAX.
  static
  void EntityUpdate();
  // 0x438DD0 (69 B). Body returns one int predicate through
  // `sbb eax, eax / inc eax`. noinline: StepUpdate calls it in-TU and keeps the call.
  __declspec(noinline) BOOL IsBurning();
  // oracle 0x42AD10 (21 B), unported. Reads bPathingFlags and sets bit 2 when bit 1 is set.
  // int, corrected from BOOL on this body's own bytes: the oracle is mov al,[ecx+105h] /
  // test al,2 / jz L / or al,4 / mov [ecx+105h],al / xor eax,eax / L: ret, so the
  // fallthrough exit leaves the old flags in AL with EAX's upper bits dirty rather than a
  // normalised 0/1. Stop's guard (call / test eax,eax / jnz) cannot see those upper bits,
  // so the callers are neutral here and the callee is the only evidence.
  int  sub_42AD10();
  // The movement and target Set* family. Signatures from the callee's own listing; stack
  // widths from each epilogue's ret N.
  // 0x43A0A0 (48 B). ret 4h, one CUnit * argument; forwards SetMoveTarget_CUnit's EAX (mov
  // edi, eax after the call, mov eax, edi at the exit), so BOOL. Declared earlier -- do not
  // duplicate it.
  // 0x43A260 (9 B): push ebp / mov ebp, esp / pop ebp / jmp CFlingy::SetTargetTurningPoint
  // -- the arguments pass through untouched, so the (int, int) void signature of that callee.
  void SetSubTarget(int nX, int nY);
  // 0x438B80 (27 B) ret 8h / 0x438BA0 (113 B) ret 4h -- the ApproachMoveTarget twins,
  // declared earlier as BOOL(int,int) and BOOL(CUnit *).
  // 0x43A4E0 (85 B). Nullary void (ret, nothing pushed, no EAX consumer), declared earlier.
  // 0x43A5F0 (293 B). Nullary void; calls UpdateTurret with one pushed argument.
  void UpdateMovement();
  // 0x438C20 (163 B). Nullary void (`push esi / push edi / mov edi, ecx`).
  void CircleUpdate();
  // 0x438E20 (187 B). Nullary void; tail-jmps CSprite::RedrawHP (void).
  void EnergyUpdate();
  // 0x43A770 (130 B). Nullary void; tail-jmps CThingy::SpriteUpdate on the
  // subunit. UpdatePrimary's sibling.
  void UpdateSecondary();
  // 0x43A800 (118 B). `ret 4h`, one pushed argument -- the desired turret
  // facing UpdateMovement computes (its call site is the only caller:
  // `push eax / call CUnit::UpdateTurret`). VOID: no EAX consumer at the site.
  void UpdateTurret(int nDirection);
  // 0x439E20 (187 B). Nullary void; tail-jmps CUnit::DispatchLoadUnit.
  void RunTransportOrders();
  //
  // The burrow-state dispatchers, the arms of the RunOrders switch. All nullary void
  // thiscalls: push esi / mov esi, ecx, nothing pushed.
  //
  void DispatchBurrowed();       // 0x43C810 (106 B)
  void DispatchDeburrow();       // 0x43C880 (212 B); tail-jmps status_set_update
  void DispatchBurrow();         // 0x43C6F0 (265 B)
  void DispatchSpawningLarva();  // 0x43D0D0 (204 B)
  // 0x43C560 (55 B), unported. The action, not the dispatcher above: recv_net_cmd_Deburrow
  // calls it once per selected unit. Nullary void thiscall -- mov esi, ecx at entry with EDX
  // never read, no stack parameters, and the result discarded at all four call sites, which
  // is what fixes the return as void. It tail-jmps EndUninterruptableDispatchOrder.
  void Deburrow();               // 0x43C560 (55 B)
  // 0x437A00 (323 B), unported. Nullary void (`push esi / mov esi, ecx`,
  // nothing pushed, plain `ret`); RunTransportOrders tail-jmps it.
  void DispatchLoadUnit();
  //
  // Unported callees these bodies call, each signature from the callee's own oracle listing.
  //
  static void DisruptorsUpdate();   // 0x438CD0 (246 B): nullary void, used-list walk; no ECX at its one call site
  void PathCalcMove();       // 0x42AD30 (530 B): nullary void (bare ret)
  // 0x41C630 (334 B): its one caller, UpdateSecondary, discards EAX => void.
  void DispatchTrainFighter();
  // CUnitCarrier.cpp order handlers (0x41C4E0, 0x41C950): thiscall, no stack
  // arguments; the interceptor and scarab strafing runs.
  void DispatchStrafeUnit();
  void DispatchTrilobyteStrafeUnit();
  // 0x41EBE0 (547 B), CUnitCombat.cpp: the patrol order handler, thiscall,
  // no stack arguments.
  void DispatchPatrol();
  WORD GetCloakingCost();    // 0x4348C0 (59 B): callers emit `movzx eax, ax`
  int GetMaxEnergyInProgress();  // 0x434A50 (34 B): callers cmp full EAX
  // 0x437540 (391 B): `ret 8h`; `mov eax,1h` / `xor eax,eax` exits => BOOL;
  // call sites push y then x.
  BOOL ComputerTransport(int nX, int nY);
  // 0x42E520 (216 B): the second slot is an int the callee unpacks with one
  // dword load and two `shr`, not a point16_t by value.
  BOOL IsIntersecting(CUnit *pTarget, int nPackedPos);
  // 0x419820 (1239 B). The map names it CUnit::ReinsertFinder_g and the ledger
  // CUnit::CoordSort, the sheet's proposal; the ledger spelling is the one the
  // generated code resolves. ret 8h at both exits and no EAX result, so void.
  void CoordSort(int nX, int nY);
  // 0x43DA00 (48 B) / 0x434D10 (132 B): nullary void members (DispatchDeburrow).
  void MakeUnburrowed();
  void SyncAllStatusEffects();
  // 0x42C790 (15 B): `mov eax, [ecx+100h] / ret`. The ledger names it with
  // the member prefix and the asm117o listings call it as a member, so the
  // member spelling is what gen thunks; ECX = this either way.
  void sub_42C790();
  //
  // oracle 0x437C70, unported. Thiscall with one stack argument, callee-cleaned (ret 4h at
  // both exits). `this` is the unit boarding and the argument is the transport: the body
  // writes this->GetUnitID() into the argument's wCargoUnitIds (mov word ptr
  // [edi+esi*2+0B0h], ax) and bounds its loop with gubUnitCargoHold[pArg->wType], a second
  // reader of that table's extent. It also reads the argument at +0x64, +0xDC and +0x4D.
  // Void and not BOOL: neither exit materialises a value and the opening caller,
  // DispatchEnterTransport's own tail, discards it.
  void EnterTransport(CUnit *pTransport);


  // 0x41DD40 (198 B). Thiscall, no argument (push ebx / mov ebx, ecx with nothing pushed at
  // the one call site), bare ret. Void by the caller: SetPlayer does mov ecx, esi / call and
  // its next instruction is another call, so EAX is dead across the return.
  void CancelAlliedTargets();

  // 0x41D8F0 (196 B). ret 4h -- one pushed dword, `this` in ECX. The argument is a CUnit *:
  // both call sites in sub_420770 push dword ptr [esi+5Ch], i.e. pOrderTarget. Void by the
  // callers: both sites materialise their own return value immediately after, so EAX from
  // this call is dead.
  // The argument slot is reused as a local inside the body (mov dword ptr [ebp+8h], ecx at
  // 0x41D92E, read back at 0x41D990). Reproduced, not cleaned up: writing to a by-value
  // parameter is ordinary C++.
  void ApproachAttackTarget(CUnit *pTarget);

  // 0x41FD90 (135 B). ret 4h, one pushed dword, `this` in ECX. BOOL: exits mov eax, 1h /
  // xor eax, eax and all five call sites truth-test it. The argument is a CUnit * that may
  // BE NULL, and the body says so rather than the callers: test edi, edi / jne falls back
  // to [ebx+5Ch] (pOrderTarget) when the argument is 0, then hands the survivor to
  // IsEnemyUnit. ai_update_unit_idle passes a literal 0 on that footing.
  BOOL IsAttackable(CUnit *pTarget);

  // 0x420120 (111 B). ret 4h, one pushed dword, `this` in ECX. The argument is a CUnit *:
  // it is forwarded to sub_4212B0 (declared above with two CUnit * parameters) and then
  // stored into dwUnion7C on two objects. Void by the caller: OnDamaged walks an array and
  // its next instruction is add esi, 4h, never reading EAX.
  void sub_420120(CUnit *pTarget);

  // 0x420190 (298 B), declared for the call only. ret 4h with `this` in ECX and one pushed
  // dword: OnDamaged's site is mov edx, [edi+7Ch] / mov ecx, edi / push ebx / call, and the
  // pushed value is dwUnion7C's attacker slot -- a CUnit * on sub_421120's footing. EAX is
  // not read after the call.
  void sub_420190(CUnit *pTarget);

  // 0x420DB0 (131 B). ret 8h -- two pushed dwords, `this` in ECX, and the first is a sign-
  // extended coordinate: its one caller sai_path_waypoint pushes an opaque dword, then
  // movsx eax, cx / push eax, and pushes run right to left, so [ebp+8h] is the movsx one.
  // Both are read as full dwords and forwarded unchanged. Void by the caller: the next
  // instruction there redefines EAX.
  void sub_420DB0(int a1, int a2);

  // 0x41FA00 (158 B). Thiscall, no argument, bare ret. int and not BYTE, decided by a
  // caller: sub_420190 does cmp eax, 0FFh with no movzx first, which is only sound if the
  // callee defines all of EAX, and the body's own sentinel is the full-width mov eax, 0FFh.
  // GetChanceToHit does mov edi, eax, agreeing. 0xFF is a no-weapon sentinel, not a boolean.
  int sub_41FA00();

  // 0x432070 (27 B), ret 8h. Finds a unit of one type inside a rect. The body never reads
  // ECX, but its one caller, sub_41FA00, loads mov ecx, esi before the call, so the receiver
  // is this unit.
  CUnit *sub_432070(const rect16_t *pArea, DWORD nUnitType);

  // 0x41FC70 (159 B). Thiscall, no argument, bare ret, and void: its one caller
  // CImage::RunScript loads mov ecx, [543300h] (gpImageUnit) and then reads a script byte,
  // never EAX. The ledger and the map agree on the name HitMelee for this VA.
  void HitMelee();

  // 0x420E40 (105 B). Spelled exactly like its proven sibling GetDisplayBaseDamage
  // (0x420EB0), because the two prologues are the same instruction sequence -- mov eax,
  // dword ptr [ebp+8h] then movzx <reg>, al -- and that one is already matched at (BYTE).
  // WORD return, decided by the callers and not by the body: all four sites in
  // statdata_ctx_status emit movzx <reg>, ax on the result, unemittable for an int return.
  WORD GetDisplayBonusDamage(weapon_id_t bWeapon);

  // The combat predicates and accessors this TU defines. Every signature is read off the
  // member's own body plus, where the body cannot decide it, the callers.
  // GetDisplayBaseDamage returns WORD and the callers decide it: the body's exit is a
  // 32-bit imul eax, ecx, so the body alone would say int, but all three call sites in
  // statdata_ctx_status do movzx eax, ax on the result. Only AX is defined by contract. Its
  // BYTE parameter is proven inside the callee (mov eax, [ebp+8h] / movzx esi, al).
  WORD GetDisplayBaseDamage(weapon_id_t bWeapon);      // 0x420EB0 (75 B)

  // 0x41FED0 (83 B). Thiscall, no argument, bare ret; exits mov eax, 1h and xor eax, eax.
  BOOL IsInfestable();

  // 0x41DB90 (85 B). One stack argument loaded 16-bit bare -- mov ax, word ptr [ebp+8h] --
  // so WORD, not BYTE and not int. Corroborated by the compare that follows: mov edx, 0E4h
  // / cmp ax, dx, MSVC materialising 228 in a register rather than emitting a 16-bit
  // immediate. The jnb makes that compare unsigned.
  BOOL CanTargetGroundType(WORD wUnitType);

  // 0x41FF50 (163 B). BYTE arg: mov ebx, [ebp+8h] then movzx eax, bl to index wpn_minrange_g.
  // The two calls that forward it push EBX raw, which is sound only because both callees
  // also take it as a BYTE -- the same aliasing AttackWith records.
  BOOL IsTargetInWeaponRange(weapon_id_t bWeapon);

  // Declared for the call only.
  // 0x420600 / 0x4206E0. Both ret 4h with `this` in ECX and one pushed dword widened from
  // bOrderId (BYTE). sub_420600's exits are mov eax, 1h and a zero path and its caller tests
  // EAX; sub_4206E0's result is never read at any call site.
  BOOL sub_420600(int nOrderId);
  void sub_4206E0(int nOrderId);

  // 0x41A250 (45 B). ECX = this, nothing pushed, bare ret; exits sbb eax, eax / neg eax /
  // inc eax and xor eax, eax, so full-width 0/1.
  BOOL AtMoveTarget();

  // 0x41F320 (269 B). BYTE and not BOOL, and both sides say so: the callee has a xor al, al
  // / pop edi / ret exit -- a partial write that leaves EAX's upper 24 bits undefined, which
  // an int return could not do -- and both call sites test it with test al, al. Its other
  // exits are movzx eax, byte ptr [eax+5219F0h] / add eax, esi, a sight-range byte plus a
  // small upgrade bonus, so BYTE is value-safe too.
  BYTE EnemyTrackingRange();

  // 0x431C40 (146 B). `this` in ECX, nothing pushed. Its result is null-tested and then
  // loaded into EDX as NewTarget's second __fastcall register argument, which that
  // function's other call site fills from a unit pointer.
  CUnit *FindAttackTarget();

  // 0x422850 (329 B) / 0x422760 (229 B). Range helpers, both self-cleaning with `this` in
  // ECX. The results are unsigned: IsTargetInWeaponRange keeps the value in EDI and compares
  // it with jb and jnbe. GetDistanceTo_xy's parameters are full dwords, proven inside the
  // callee; the caller's movzx eax, word ptr [esi+58h] is a zero-extending cast at the call
  // site over the short wOrderTargetX/Y, not a narrow parameter.
  DWORD GetDistanceTo_CUnit(CUnit *pTarget);
  DWORD GetDistanceTo_xy(int nX, int nY);

  // 0x420F00 (229 B). ret 4h; the BYTE parameter is proven inside the callee (mov bl, byte
  // ptr [ebp+8h]). The result is compared cmp edi, eax / jnbe, unsigned.
  DWORD GetWeaponRange(weapon_id_t bWeapon);

  // 0x420970 (276 B). ret 8h. Both arguments are OUT-pointers: MakeBullet calls it with two
  // lea/push pairs, then reads the slots back and forwards them as CBullet::AllocBullet's
  // nX/nY. Result unused.
  void sub_420970(int *pnX, int *pnY);

  // The definitions in src117/lang/CUnitCombat.cpp. Arity from `ret N`, widths from where
  // the callee itself reads each slot.
  // sub_41F530: ret 0Ch = three dword slots plus `this`. nSlot is a full DWORD ([edi+esi*1
  // +54h] and cmp edi, 1h / 2h); bWeapon and bAnim are BYTEs, and the evidence is the dirty-
  // slot push at the caller -- sub_41F630 stores one byte over a slot and pushes the whole
  // dword, which MSVC only does when the callee's parameter is sub-dword.
  BOOL sub_41F530(int nSlot, weapon_id_t bWeapon, BYTE bAnim);

  // sub_420AB0: ret 4h; the argument is passed to CanSeeUnit as its CUnit * and read as
  // test byte ptr [ebx+0DCh], 4h (dwFlags).
  BOOL sub_420AB0(CUnit *pTarget);

  // sub_420B50: sub_420AB0's sibling and the reason it sits here. The two are the range-
  // rejection pair, called back to back by usel_click_target (0x4BC947) and each setting a
  // different error string (0x378 / 0x379); the naming sheet annotates 0x420AB0 as out of
  // range and 0x420B50 as too close. The call site is push edi / mov ecx, esi / call, so
  // `this` is the candidate unit and the one stack slot a CUnit *; the result is tested.
  BOOL sub_420B50(CUnit *pTarget);

  // AIPickAttackTarget: ret 0Ch; all three slots are used at dword width (a1 is forwarded
  // to NewTarget, a2/a3 are truth-tested with cmp dword ptr [ebp+0Ch], 0), and a3's slot is
  // reused as a local. The result is normalised 0/1 (neg/sbb/inc) at its other call site.
  BOOL AIPickAttackTarget(int a1, int a2, int a3);

  // 0x429C40, `ret 0Ch` = 4 + 8: the pair at [ebp+0Ch] is forwarded whole, so
  // callers pass CUnit +0x58..+0x5F as one order_target_t.
  void SetNextOrder_Target(int nOrderId, order_target_t t);

  // PrependOrder_CUnit: ret 0Ch; the body does mov esi, [ebp+0Ch] then [esi+0Ch] (pSprite),
  // which types arg 2 as CUnit *.
  // Arg 3 is A COrder *, not the void * it was first spelled, and the CALLEE's evidence is
  // what settles it: this body forwards arg 3 unchanged into PrependOrder_Target_type's 4th
  // slot, and that body loads the slot into ECX as `this` for CLink::PrependLink, so the
  // parameter is a list node whatever a given caller keeps in it. An argument from the call
  // sites would have been wrong: the three oracle sites do not agree on the field they pass
  // (+0x7C twice, +0x74 twice), so reasoning from them proves whichever one is read first.
  // That disagreement is a fact about those two fields, not about this parameter.
  void PrependOrder_CUnit(int nOrderId, CUnit *pTarget, COrder *pPrevOrder);

  // CanSeeUnit: ret 4h; the body null-tests the argument then reads [eax+0DCh] / [eax+0Ch].
  // The result is a mask (and eax, ebx) that is only ever truth-tested, hence BOOL rather
  // than the raw flag word.
  BOOL CanSeeUnit(CUnit *pTarget);

  // GetWeaponCD: 0x41FB70, ret 4h; its caller takes the result as mov bl, al, so BYTE.
  BYTE GetWeaponCD(weapon_id_t bWeapon);

  // oracle 0x428AC0 (40 B) / 0x428AF0 (40 B). Signed: each returns a small integer in -1..+2
  // built by cmov/inc/dec over the full EAX, so int and not BYTE -- a narrow return would
  // drop the sign the callers need.
  int GetAttackSpeedModifier();
  int GetMovementSpeedModifier();

  // oracle 0x428B20 (35 B). The return width is 16 bits, the same way GetHangarType's is:
  // both exits are mov ax, ..., leaving only AX defined. The current type, or the queued
  // one while a zerg building morphs.
  WORD GetTypeEx();

  // oracle 0x428BD0 (59 B) is already declared above as GetUnitID; the body is in
  // src117/lang/CUnitMisc.cpp.

  // oracle 0x428CC0 (24 B). Reads the type's flingy row, then its max velocity, and hands
  // that to ModifyGivenMovementSpeed, so it returns whatever that returns, at full width.
  int FlingyMaxSpeedWithModifiers();

  // oracle 0x428D10 (79 B). Returns a pointer into string storage -- both exits are a tail
  // jump to a string getter, plus one literal mov eax, 4EC14Eh pointing at .rdata -- so the
  // return type is a C string.
  const char *GetName();

  // oracle 0x428D60 (35 B) / 0x428D90 (27 B). Both return a BYTE and the oracle is explicit
  // about it: every exit writes AL only (mov al, 1h, xor al, al, and al, 2h) and leaves the
  // upper 24 bits of EAX holding the flags word it loaded. An int return would force a
  // zero-extension the oracle does not have.
  BYTE GetRace();
  BYTE GetSizeType();

  // oracle 0x446080 (72 B) -- its body lives in gamesnd.cpp, not in any CUnit TU, which is
  // where both the map and the ledger seat it.
  // int, not BOOL, and the distinction is the return width rather than the spelling: two
  // consuming callers use the full EAX and narrow it nowhere. The tail is sbb eax, eax /
  // inc eax, which materialises a whole 0 or 1, so BOOL would be honest about the value,
  // but the callers are the authority on the width and they read 32 bits.
  int IsOnFire();

  // oracle 0x428DB0 (51 B). Hands the unit to the neutral player (slot 11),
  // cancelling an addon under construction first.
  void GiveToNeutralSlot();

  // oracle 0x429070 (43 B) / 0x4290A0 (53 B). Type predicates over wType.
  BOOL IsCritter();
  BOOL IsDoor();

  // oracle 0x4290E0 (48 B). `ret 4h` => exactly one stack argument.
  BOOL IsEnemyUnit(CUnit *pOther);

  // The three CUnit members src117/lang/TAI_Captain.cpp calls. Declared here, not defined:
  // AirStrength and GroundStrength live in TAI_AreaStrength.cpp. All three were absent from
  // src117/h, which is the hazard -- a name no header of src117 declares is one the generated
  // code resolves out of the quarantined reference tree, with their spelling.

  // oracle 0x4A9A10 / 0x4A9A20, 8 B each: movzx eax, word ptr [ecx+138h], the same at
  // +0x13A, and a ret. The return is int and the callers decide it, since the body is
  // byte-identical either way: the movzx eax, ax that a WORD return must emit wherever the
  // value is bound appears at none of the eleven call sites, and five consume the full EAX.
  // Measured, under WORD tai_captain_add_unit_strength_cb is 74 B against 68, the extra
  // bytes being two such movzx. The fields at +0x138 / +0x13A are unsigned WORDs by that
  // load and stay unnamed inside _unmapped_138[0x18]; naming them is the user's call.
  int AirStrength();
  int GroundStrength();

  // oracle 0x4ABC90 (35 B), oracle TU TAI_Captain.cpp. A `this`-only member with no stack
  // parameters: movzx eax, word ptr [ecx+64h], add eax, -9h, cmp eax, 3Ah / jnbe, then
  // movzx eax, byte ptr [eax+4ABCBCh] -- a 59-byte membership table. BOOL because both
  // consuming callers take the full EAX and every ret materialises 0 or 1.
  BOOL IsPureSpellcaster();

  // The rest of src117/lang/CUnitMisc.cpp: the remaining wType predicates, the speed family
  // and the two per-tick gain seeders. Every convention is read off a call site, and nothing
  // is declared const.

  // oracle 0x427D90 (78 B). wShieldGain's twin: seeds wHPGain with the per-tick health a
  // building regains while it builds. See wHPGain above for why the two are not one
  // function with a different field.
  void AssignHPGain();

  // oracle 0x428C10 (81 B) / 0x428C70 (79 B). Flingy accel and turn radius, each scaled by
  // the movement modifier. The return widths are the table element widths and the oracle
  // says so at the exit: the modifier<=0 arm of the first ends mov ax, dx and of the second
  // mov al, dl, neither defining the whole of EAX, which an int return could not do. The
  // matching arm in each, lea eax, [edx+edx*1], writes all 32 bits, which a narrow return
  // may do. gxFlingyAccel is WORD[209] and gubFlingyMaxTurn is BYTE[209].
  WORD FlingyAccelWithModifiers();
  BYTE FlingyTurnRadiusWithModifiers();

  // oracle 0x429110 (32 B) is not a member -- see the free IsEnemyUnit declared in
  // src117/lang/CUnitMisc.cpp. Its entry takes a player index in ECX and a CUnit * in EDX,
  // so nothing is a `this`.

  // oracle 0x429130 (33 B) / 0x429160 (73 B) / 0x4291B0 (145 B) / 0x429250 (113 B) /
  // 0x4292D0 (31 B) / 0x4292F0 (43 B). Predicates. IsRescuable is the odd one out: it reads
  // the owning PLAYER's slot type out of gPlayerData rather than anything about the type.
  BOOL IsFloorTrap();
  BOOL IsTrap();
  BOOL IsMultiSelectable();
  BOOL IsRallyer();

  // Members src117/lang/usel.cpp reaches.

  // DisplayRallyTarget -- oracle 0x41B250 (60 B, CUnitBuild.cpp). Nullary and void, and
  // both halves are derived: the call site is mov ecx, <unit> / call with no pushes and no
  // stack cleanup after it, the callee ends pop esi / ret rather than ret N, and the return
  // value is read at none of its four call sites.
  // It must be a genuine member, and one missing line here cost a proven body: an author
  // who correctly refused to edit this header had to call a free DisplayRallyTarget(CUnit *)
  // stand-in, which emits identical bytes -- a thiscall member call and a one-pointer
  // __fastcall free call are both mov ecx, p / call rel32 -- yet can never match the mangled
  // CUnit::DisplayRallyTarget, and usel_drag_select carried a lone [CALLEE] delta for it.
  void DisplayRallyTarget();

  // NeedsMoreEnergy -- oracle 0x434B50 (CUnitSpells.cpp). The parameter is a WORD and the
  // CALLEE's own body proves it, not the call site: after the cheat-bit guard it does
  // movzx eax, word ptr [ecx+0A2h] (this->wEnergy, /*0xA2*/ WORD above) then cmp ax, word
  // ptr [ebp+8h] -- the stack slot read as a WORD. ret 4h gives one stack slot, and the
  // sbb/neg pair normalises the carry to 0/1, a BOOL rather than a passthrough int.
  BOOL NeedsMoreEnergy(WORD wCost);
  BOOL IsRescuable();
  BOOL IsBeaconType();

  // oracle 0x429350 (68 B). One pushed argument, full-width result. The plain three-way
  // scale -- double, unchanged, less a quarter -- that ModifyGivenMovementSpeed wraps with
  // its unit-type special cases and floor.
  int GetModifiedSpeed(DWORD dwSpeed);

  // oracle 0x429420 (102 B). DisableDoodadState and EnableDoodadState back to back; both
  // are defined earlier in the TU, so MSVC inlines both and CSEs the dwFlags load and the
  // 0x400 bit test they share.
  void ToggleDoodadState();

  // Members defined in src117/lang/CBullet.cpp.

  // oracle 0x409C90 (41 B). Defined in src117/lang/CBullet.cpp, which is where the map puts
  // it, next to CUnit::GetBounds (0x409CC0). The class is CUnit and the TU is CBullet.cpp;
  // those are allowed to differ, and the hook include resolves from the symbol for this case.
  // Thiscall, no argument: `this` in ECX, nothing pushed, bare ret. Returns a 32-bit value
  // in EAX -- int, not WORD and not BOOL, because the value is real whole hit points and
  // because MaxToughness adds it to a second quantity in full 32 bits.
  // The unit type's maximum hit points in whole hp, gxUnitLife[wType] >> 8. If that comes
  // out 0 -- a type whose units.dat hit points are below one whole point -- it falls back to
  // this unit's own current hit points rounded UP (+0FFh before the >>8), floored at 1. The
  // floor and the round-up together guarantee the non-zero answer a dividing caller needs.
  int GetLife();


  // oracle 0x40A180 (56 B). Defined in src117/lang/CBullet.cpp, the same class-against-TU
  // split GetLife above has. Thiscall, one stack argument cleaned by ret 4h, result in
  // {0,1}. `this` is copied to ESI on entry because the second half needs the shift count in
  // CL, the low byte of the register it arrived in; no source spelling forces that copy.
  // A unit is visible to another unit's owner only if it carries one of the two dwFlags bits
  // at mask 0x300 and the querying player's bit in dwUnionE4 is not set. A unit with neither
  // 0x300 bit is reported not-visible, so this is a helper for callers that already know a
  // detection check is needed, not a general can-anyone-see-this-unit predicate.
  BOOL IsVisibleTo(CUnit *pOther);

  // Five members of the unit-combat block. Unlike GetLife and IsVisibleTo above these need
  // no class-against-TU caveat: the map puts CUnitCombat.cpp and the CUnit class together.

  // oracle 0x41F300 (29 B). Thiscall, no argument, no result -- `this` in ECX, nothing
  // pushed, and the body ends on a tail jump to DispatchPlayerGuard rather than a ret,
  // which is what a void function whose last statement is a void call compiles to.
  // Points the unit at its TURRET's destination and then runs the ordinary player-guard
  // order: it reads this->subunit's CFlingy destination pair (+0x1C/+0x1E) and feeds it to
  // its own CFlingy::SetDestination, so the base follows where the turret is already aimed.
  void DispatchTurretGuard();

  // oracle 0x41F4A0 (42 B). Thiscall with one stack argument, cleaned by ret 4h. The
  // argument is read as a dword and then used only through its low byte, so the slot is a
  // dword while the value is a weapon id -- declared BYTE because that is what the body and
  // both call sites treat it as, and the promotion costs no instruction. No result: EAX
  // holds a table element at the exit, the compare's leftover.
  // Fires the weapon's bullet once, or twice when the weapons.dat byte column at 0x525F18
  // reads 2 for that weapon -- the twin-bullet weapons. That duplicate call is the whole
  // body, and it is what separates this function from MakeBullet.
  void FireSpell(weapon_id_t bWeaponId);

  // oracle 0x41FAA0 (49 B). Thiscall, no argument, bare ret, result in EAX. int, not WORD
  // and not DWORD: both halves are produced with sar, a signed shift, and added in full 32
  // bits.
  // This unit's current effective hit points in whole units -- hit points rounded UP (add
  // eax, 0FFh before sar eax, 8h, the 8.8 fixed point) plus, for a type that has shields at
  // all, its shields rounded down. The asymmetry is the oracle's. Pairs with MaxToughness
  // (0x4200E0), which computes the same quantity from the units.dat maxima.
  int GetToughness();

  // oracle 0x41FB40 (35 B). Thiscall, no argument, bare ret.
  // Returns char, not BYTE, and the width is what makes the sentinel work: the result comes
  // from mov al, byte ptr [eax+5213A8h] and is compared with cmp al, -7Eh, i.e. 0x82 read
  // as a signed byte. 0x82 is weapons.dat's no-weapon row (130 == the weapon count), so the
  // function returns this unit's units.dat Air Weapon column and, when it has none, falls
  // through to its subunit's, still answering 0x82 if there is no subunit.
  // The ledger spells this GetWeaponAir and the map CUnit::GetCombatAirWeapon; the ledger
  // spelling is what the proof is keyed to, and reconciling the two is a ledger decision.
  weapon_id_t GetWeaponAir();

  // oracle 0x41FC40 (36 B). Thiscall, no argument, no result.
  // Credits this unit with one kill, saturating at 255 (see bKillCount above), and then --
  // for unit type 0x49 only -- recurses into the unit at +0xC0 through a tail jump back into
  // itself, so the credit propagates up the chain. 0x49 == 73 is the interceptor, whose
  // +0xC0 is the pHangarParent arm of that union: its kills go to its carrier. The
  // recursion is the source's, not an optimisation -- a plain call in tail position.
  void AddKill();

  // Six more members of the unit-combat block, all defined in that TU.
  // None is declared const, and for these six that is more than the usual convention: four
  // read nothing but their own fields and would be const in ordinary C++. It costs a proof.
  // The hook generator's declaration pattern matches a trailing cv-qualifier and discards
  // it, so a const member's generated thunk or hook entry restates the signature without
  // the const and no longer matches the definition. IsDead makes that concrete: it was
  // already declared here non-const and CUnitMisc.cpp's FromIdAlive calls it, so a const
  // definition would be a second, different member function. Dropping const is byte-neutral.

  // oracle 0x41F4D0 (95 B). Thiscall with one dword stack argument, cleaned by ret 4h. The
  // argument is read whole and then narrowed by the body itself (movzx edi, al), so the slot
  // is 4 bytes while the value is a weapon id: BYTE, as for FireSpell and MakeBullet. MSVC
  // reuses that slot as a spill for the target's Y coordinate, which is what makes the
  // second AllocBullet call push [ebp+8h] rather than recompute it. No result.
  // Fires the weapon directly as a bullet at this unit's order target rather than through
  // MakeBullet the way FireSpell does, and only if the weapon has a graphic. Twice for a
  // weapon whose gnWeapLaunch column reads 2, as FireSpell and AttackWith do.
  void FireWeapon(weapon_id_t bWeaponId);

  // oracle 0x41F5D0 (94 B). Thiscall with one dword stack argument, cleaned by ret 4h and
  // read as a BYTE (cmp byte ptr [ebp+8h], 1h). The body writes its own parameter slot (mov
  // byte ptr [ebp+8h], bl) and then pushes that whole dword twice, so the source assigns the
  // chosen weapon id back over the argument instead of using a fresh local: a
  // separate local would get its own stack slot and lose the shape. No result.
  // Picks the ground weapon when the argument is 1 and the air weapon otherwise,
  // reproducing GetWeaponGndEx's burrowed-Lurker case inline, then fires it once -- or
  // twice for a gnWeapLaunch == 2 weapon.
  void AttackWith(BYTE bGroundArm);

  // oracle 0x41FAE0 (73 B). Thiscall, no argument, bare ret.
  // Returns a BYTE, and the two exit shapes fix it: four of the five arms end mov al, <imm8>
  // and leave the upper 24 bits of EAX holding the zero-extended wType the entry loaded,
  // while only the last arm writes all 32. A caller reading EAX would read wType on four
  // paths out of five, so the contract is one byte wide.
  // Maps a unit type to the order id its Hold Position becomes: 0x39 for 0x48/0x52, 0x3E
  // for 0x53/0x51, 0x6C for 0x2D/0x31, 0x88 for 0x2F/0x32, 0xB2 for 0x22, 0x6B otherwise.
  // The constants are the oracle's immediates; the order names behind them are not derived.
  BYTE HoldOrder();

  // oracle 0x41FC20 (28 B). Thiscall, no argument, bare ret.
  // Returns char, not BYTE, for the same reason GetWeaponAir above does: every path defines
  // only AL and the sentinel is written as the negative literal -0x7E, i.e. 0x82 read
  // signed -- weapons.dat's no-weapon row (130 == the weapon count).
  // The units.dat Ground Weapon column for this unit, except that unit type 0x67 (103, the
  // Lurker) reports the sentinel while it is not burrowed. The two guards are one joined
  // condition and the oracle says so: both jnz targets are the shared table-lookup tail.
  // The ledger spells this GetWeaponGndEx and the map CUnit::GetCombatBaseGroundWeapon; the
  // ledger spelling is what the proof is keyed to.
  weapon_id_t GetWeaponGndEx();

  // oracle 0x41FE20 (27 B). Thiscall, no argument, bare ret, EAX defined at full width on
  // both exits (mov eax, 1h / xor eax, eax). Declared here as a callee since CUnitMisc.cpp's
  // FromIdAlive calls it; the body is in CUnitCombat.cpp.
  // A unit is dead when it has no sprite left, or when it is one step into the die order
  // (bOrderId == 0 && bOrderState == 1). The second pair is one && and not two guards:
  // written as separate flat ifs, MSVC folds them into sete-style flag arithmetic and 15
  // bytes go wrong.
  BOOL IsDead();

  // oracle 0x41FF30 (23 B). Thiscall, no argument, bare ret, EAX defined at full width on
  // both exits. True for unit types 0x2D and 0x31.
  BOOL IsQueenType();

  // oracle 0x420000 (39 B), oracle TU CUnitCombat.cpp. A non-static thiscall member that
  // ignores `this`, and the callee's own bytes mislead because of it: it takes its operand off
  // the stack (mov eax, dword ptr [ebp+8h]), retires one slot (ret 4h) and never touches
  // ECX -- exactly what a member ignoring `this` compiles to. Six oracle call sites settle
  // it, every one loading ECX with the unit whose +0x70 subunit it then pushes (CanTarget,
  // ComputerTransport, DispatchNeutral twice, select_target, sub_432280). A static
  // __stdcall spelling costs 2 bytes per call site and the landed body does not move either
  // way, so no byte stage could report the error.
  // BOOL, not bool: both exits define EAX at full width in separate blocks. Reads only
  // gulUnitTypeFlags[wType] & 0x10.
  BOOL IsTurret(CUnit *pUnit);

  // oracle 0x425960 (266 B). Loads the orders and units dat files and builds the pool.
  // Static and nullary: neither call site loads ECX, and both callers discard EAX.
  static
  void InitUnits();

  // Ten more members of the unit-combat block, all defined and matched in
  // src117/lang/CUnitCombat.cpp, listed in ascending oracle VA, the order the definitions
  // run in. Every signature is read off the member's own body -- the entry register use,
  // the pushes, and whether the exit is a bare ret or a ret <n> -- not off a call site.

  // oracle 0x41E2F0 (122 B). Thiscall with three dword stack arguments (ret 0Ch). The first
  // is read as a byte and used both as a compare operand and as a zero-extended table index,
  // so a weapon id in a dword slot: BYTE. The second is never inspected here, only forwarded
  // whole to CreateShieldEffect, so int at the slot's width with the name from that callee.
  // The third is null-tested and used as a CUnit. No result.
  // The damage-side notification hook: tell the unit it was damaged, play the under-attack
  // sound when the attacker is another player, and spawn the shield impact effect when the
  // type has shields, has at least 1.0 shield point left (cmp dword ptr [esi+60h], 100h,
  // the 8.8 fixed point) and the weapon's 0x525748 column is nonzero. Weapon id 0x22 is
  // suppressed from both the notify flag and the sound, and the oracle tests it twice.
  void DamageNotify(weapon_id_t bWeapon, int bDirection, CUnit *pAttacker);

  // oracle 0x41E5C0 (97 B). Thiscall, no argument, bare ret, no result.
  // The attack-unit order's dispatch step. For a computer slot only
  // (gPlayerData[bPlayer].nType == 1) whose order target is absent or is not a units.dat
  // bit-0 type, it first offers the decision to guard_return_to_post and returns if that
  // claims it. Otherwise it retargets, and on a successful sub_420770 runs
  // tai_soldier_try_stim (guarded on dwUnion134) and hands off to sub_41F630 with 5.
  void DispatchAttackUnit();

  // oracle 0x41F090 (103 B). Thiscall, no argument, bare ret, no result.
  // Five guards sharing one void exit: dwFlags bit 0x400, then the three disable timers
  // (+0x117, +0x119, +0x124), then bUnion54. Past them it claims the slot (bUnion54 = 0x0F),
  // asks sub_432060 for a target, stores it into pOrderTarget and, only if non-null, issues
  // order 0x13 at it and sets dwFlags bit 0x8000000 on itself and on any subunit. The store
  // into pOrderTarget happens before the null test, which is the oracle's shape.
  void DispatchTowerGuard();

  // oracle 0x41F100 (98 B). Thiscall, no argument, no result, and two exits of different
  // kinds: a ret on the taken path and a tail jmp to EndDispatchOrder on the fallthrough,
  // which is what a void function ending in a void call compiles to.
  // The same four leading guards as DispatchTowerGuard plus sub_420BD0; past them it points
  // the unit at its own sprite position and then at its order target. Any guard failing
  // ends the order instead.
  void DispatchTowerTarget();

  // oracle 0x41FB30 (13 B). Thiscall, no argument, bare ret. The result is full-width EAX,
  // which is what fixes the return at int: the value comes from movzx eax, word ptr
  // [eax*2+524290h], a zero-extending load into the whole register, where a WORD return
  // would emit a plain 16-bit mov. This unit TYPE's units.dat Kill Score column.
  int KillScore();

  // oracle 0x41FE40 (21 B). Thiscall, no argument, bare ret, EAX defined at full width on
  // both exits. A strict weakening of IsDead above: no sprite, or bOrderId == 0, without
  // IsDead's additional bOrderState == 1. So a unit that has just entered the die order is
  // dead-or-dying here and not yet dead there. The two are separate functions 0x20 bytes
  // apart in the oracle, and the difference is the point of both.
  BOOL IsDeadOrDying();

  // oracle 0x4200E0 (64 B). Thiscall, no argument, bare ret, result in EAX at full width,
  // and signed for the same reason GetToughness is: every term is produced with sar.
  // The .dat counterpart of GetToughness above -- what this unit's type could take at full
  // health rather than what this instance can take now. The hit-points term comes from
  // units.dat (0x520708 >> 8) and falls back to this unit's own rounded-up current hit
  // points, floored at 1, when the table reads 0; the shield term is 0 for a type without.
  int MaxToughness();

  // oracle 0x420950 (20 B). Thiscall, no argument, bare ret, no result. Drops the order
  // target when it is not an enemy. The zero is written from EAX and not from an immediate,
  // because on that path EAX is the call's own 0 and /Ox reuses it.
  void ClearTargetIfAllied();

  // oracle 0x41DF90 (36 B). Thiscall, no argument, bare ret, no result, and no frame and no
  // register save: the body reuses ECX itself as the subunit pointer once `this` is dead.
  // Not a sibling of ClearTargetIfAllied above despite the name -- that one asks whether
  // the target is an enemy; this clears pOrderTarget unconditionally and then the subunit's
  // too, but only for a subunit whose type carries gulUnitTypeFlags bit 0x10.
  void ClearTarget();

  // oracle 0x421120 (95 B). Thiscall with two dword stack arguments (ret 8h). The argument
  // order is (range, target): the pushes at the two IsWithinTargetRange call sites are the
  // target then the range, right to left, so the range is first. Result truth-tested.
  // A range test with a lead allowance. The target defaults to pOrderTarget when the caller
  // passes null. When this unit is moving (bMoveFlags bit 1) and either the target is not
  // or the two headings differ by more than 0x20 (get_angle at +0x23 on both), the range is
  // widened by CFlingy::BreakingDistance() >> 8 -- the distance it cannot stop within.
  BOOL sub_421120(int nRange, CUnit *pTarget);

  // oracle 0x4212B0 (102 B). __fastcall on a member, which is unusual enough here to state
  // the evidence: `this` arrives in ECX (mov edi, ecx) and the first declared argument in
  // EDX (mov esi, edx), with a single dword left on the stack and cleaned by ret 4h. A
  // plain thiscall member would have both arguments pushed. Result truth-tested.
  // No field of CUnit is touched in the body -- every object access belongs to the three
  // callees -- so this is a target-comparison predicate.
  BOOL __fastcall sub_4212B0(CUnit *pTarget, CUnit *pOther);

  // Declared for the call, with no body in src117.

  // 0x4B7E20 / 0x4977C0 / 0x4EBFB0 -- `mov ecx, esi / call` with no push.
  void StopUnitMovement();
  void UpdateUnitCollision();

  // BreakAnim's body is in src117/lang/CUnitCombat.cpp (oracle 0x420910). The declaration
  // stays here so the TUs that call it do not churn.
  void BreakAnim();

  // 0x422BA0 (119 B), oracle TU CUnitMisc.cpp, declared for the call only. Thiscall, no
  // argument, bare ret: the entry is push esi / mov esi, ecx and DispatchDie calls it with
  // nothing pushed and EAX unread. Not CSprite::Hide or CImage::Hide, which exist under
  // those class names; this one reaches CSprite::SetMask twice and the used/trans-list moves.
  void Hide();

  // 0x422F60 (unported, ledger CUnit::Show) -- the mirror of Hide above, on the same
  // CUnit-member footing, declared for the call only. Thiscall, no argument;
  // DispatchInfestEnd calls it with EAX unread, which constrains nothing, so void rests on
  // the callee's own silence.
  void Show();

  // 0x4361B0 (146 B), oracle TU CUnitTerran.cpp. Thiscall, no argument, both exits pop esi
  // / ret; DispatchAIReturn calls it with the result unread. Distinct from
  // DispatchMedicReturnToIdle (0x436280), which is landed in CUnitTerran.cpp.
  void DispatchMedic();

  // 0x438B80 (27 B) and 0x438BA0 (113 B), oracle TU CUnitUpdate.cpp, declared for the call
  // only. ret 8h and ret 4h with `this` in ECX, so two stack arguments and one.
  // Both return BOOL and the callees are what show it: neither call site reads EAX, but _xy
  // ends mov eax, 1h on one path and tail-jumps SetMoveTarget_xy (declared BOOL) on the
  // other, while _CUnit materialises 1 on two paths and 0 on a third. A void declaration
  // would compile here and make the body unwritable. _xy's arguments are int/int, not
  // short: ApproachAttackTarget builds both operands as full dwords and pushes them raw.
  BOOL ApproachMoveTarget_xy(int nX, int nY);
  BOOL ApproachMoveTarget_CUnit(CUnit *pTarget);


  // oracle 0x43A180 (211 B, unported). Thiscall, ret 4h -- one stack argument, callee-
  // cleaned. BOOL: three exits do mov eax, 1 and the one referent in reach (DispatchSapUnit)
  // truth-tests the result to decide whether to advance bOrderState. The argument is
  // compared against the move-target slot at +0x14 and every call site passes pOrderTarget.
  BOOL SetMoveTarget_CUnit(CUnit *pTarget);

  // 0x41CF90 (28 B), oracle TU CUnitCarrier.cpp, declared for the call only. Nullary, bare
  // ret, and genuinely void -- the body has no write to EAX at all: it ORs 0x8000000 into
  // this->dwFlags and into the subunit's, and returns without building a frame. Not
  // DispatchHoldPosition (0x41E9B0), which is a landed order dispatcher declared above.
  void HoldPosition();

  // 0x42C7E0 (41 B), oracle TU CUnitPath.cpp, for DispatchUndoFlyer (0x41B1F0), which calls
  // it two instructions after UpdateUnitCollision and on the same footing: mov ecx, esi with
  // nothing pushed, and EAX dead at the return -- the next instruction reads a field.
  void ResetMovementState();

  // For CUnit::Build, ReplaceGeyser and DispatchBuildSelf. All thiscall (mov ecx, esi at
  // every call site), and every address, size and oracle TU below was cross-checked against
  // the map before being written here.
  // The three sub_ names are declared as members and the bytes do not prove that: mov ecx,
  // <this> / call is identical for a thiscall member and a one-argument __fastcall free
  // function, and the map carries these three without a class prefix. Member is chosen so
  // that the call site and the generated thunk agree, which resolves from the symbol's class.

  // 0x431AE0 (130 B), oracle TU CUnitSearches.cpp. Nullary, bare `ret`. Its
  // result is null-tested and then passed as sub_4359C0's only stack argument,
  // where that callee does `mov ecx, edi` on it => CUnit *.
  CUnit *sub_431AE0();

  // 0x4359C0 (109 B), oracle TU CUnitTBuild.cpp. `ret 4h`; result never read.
  void sub_4359C0(CUnit *pUnit);

  // 0x423330 (196 B), oracle TU CUnitGUI.cpp. `ret 4h`, and the CALLEE's own
  // body reads the slot as `cmp dword ptr [ebp+8h], 0h` => 32-bit, not BYTE.
  void SyncConstructionGraphic(BOOL bFlag);

  // 0x437050 (8 B), oracle TU CUnitTerran.cpp. The whole body is
  // `mov ecx, dword ptr [ecx+0Ch] / jmp 0x40FE50` -- nullary, void, and a tail
  // call through pSprite.
  void RemovePlaceboxSelf();

  // 0x426ED0 (412 B), oracle TU CUnitInit.cpp. `ret 8h`; the callee's own
  // `mov ebx, dword ptr [ebp+8h]` then `cmp bl, byte ptr [esi+4Ch]` reads a
  // 32-bit slot carrying a player id (compared against bPlayer as a byte).
  void SetPlayer(int nPlayer, int nFlag);

  // 0x424AD0 (252 B), oracle TU CUnitInit.cpp. `ret 4h`; the slot is loaded whole but
  // read as a word (`cmp bx, 24h`, `movzx eax, bx`), and 1.16.1 pushes it `movzx cx, byte`.
  void TransformType(WORD nUnitType);

  // 0x422F00 (81 B), oracle TU CUnitGUI.cpp. ret 4h, and the CALLEE reads it as mov dx,
  // word ptr [ebp+8h], so a 16-bit parameter. The one call site pushes 0xE7, which encodes
  // identically at either width, so the width comes from the callee -- int would be
  // unfalsified there and wrong at the next caller. The same trap as GetBuildHpGain, the
  // other way round.
  void SetButtonset(WORD wButtonset);

  // 0x43A490 (68 B), oracle TU CUnitUpdate.cpp. No push at the call site and a bare ret at
  // the callee, so thiscall with no argument. The caller reloads mov ecx, esi immediately
  // afterwards, which is ordinary and not evidence of a return value; no caller reads EAX.
  void Stop();

  // The order terminators. EndDispatchOrder and
  // EndUninterruptableDispatchOrder take no argument (`mov ecx, esi` then a
  // bare call or a tail jmp); the _xy form takes two, pushed y-then-x, which is
  // right-to-left for (x, y).
  void EndDispatchOrder();
  void EndUninterruptableDispatchOrder();
  void EndDispatchOrder_xy(int x, int y);

  // The order setters. One pushed argument, two, or two, in the source order
  // the push order implies.
  // SetOrder forwards its slot unread to SetOrder_Target_type_interrupt, whose
  // arg0 is a one-byte read, so the id is BYTE here too.
  void SetOrder(order_id_t bOrderId);
  // SetOrder_CUnit's id is BYTE: CUnit::SetOrderEx pushes its own BYTE slot
  // raw, and an `int` here makes it widen with `movzx` first.
  void SetOrder_CUnit(order_id_t bOrderId, CUnit *pTarget);

  // 0x429F10, declared for the call only. The one readable call site is NewTarget's
  // tail (push 0h / push edi / push eax / mov ecx, esi / call), so `this` in ECX plus three
  // stack dwords cleaned by ret 0Ch: order id, target, flag. The third slot's role is
  // observed and not decoded -- every path reaching that site pushes literal 0 right after
  // SetOrder(0x63) swaps the order away from attack, so it reads as an interrupt
  // suppressor. BOOL against int is codegen-neutral here; BOOL follows that reading.
  void SetOrder_CUnit_interrupt(int nOrderId, CUnit *pTarget, BOOL bInterrupt);

  void SetOrder_interrupt(int nOrderId, int nFlag);

  // src117/lang/CUnitPath.cpp -- the path family. Every signature is read off its own body:
  // a ret with no cleanup is a nullary thiscall, ret 4h carries one stack dword, and the
  // return width is pinned by the exits that set EAX -- a nullary leaf's return width is a
  // question only its callers can answer. sub_42C790 and ResetMovementState are declared
  // above; this block adds only the new members.
  void WalkInterrupt();         // 0x42C810 (46 B): bare `ret` after SetAnimation
  BOOL PathFailedPath();        // 0x42B370 (57 B): xor eax,eax shared tail
  int  PathRepath();            // 0x42BF10 (79 B): lea eax,[edx-0Eh] == constant 1
  int  PathCheckIllegal();      // 0x42B1B0 (137 B): EAX 0 or 1 per exit
  BOOL FixupPathState(int b);   // 0x42AC20 (227 B): ret 4h; the arg is tested `cmp dword ptr [ebp+8h], 0`

  // 0x48EB90 (141 B). Halves read at [ebp+8h] and [ebp+0Ah] with `cwde`, so it is a
  // signed pair by value.
  int  PathFollow(mpoint_t ptTarget);

  // The remaining 17 bodies. All bare ret and nullary thiscall; return widths come from each
  // body's own EAX-setting exits, with callers that discard EAX noted where the width is free.
  void MakeImmovable();         // 0x42C7A0 (56 B): no EAX-set exit; callers discard eax
  BOOL PathAtMoveTarget();      // 0x42AFE0 (65 B): only `mov eax,1h` exit
  BOOL PathRetryPath();         // 0x42BFD0 (82 B): xor eax / mov eax,1 exits
  BOOL PathTurret();            // 0x42C610 (84 B): BOOL, not void -- both exits are
                                //   33 c0 / 5e / c3 (return 0), and a void body cannot
                                //   emit that xor
  BOOL PathLumpWannabe();       // 0x42BE10 (94 B): xor / mov 1 / inc
  BOOL PathRepathMovers();      // 0x42BF60 (97 B): xor / mov 1
  BOOL PathStartPath();         // 0x42C400 (99 B): mov eax,1
  BOOL PathSlidePrep();         // 0x42C390 (101 B): mov eax,1
  BOOL PathDormant();           // 0x42B240 (109 B): xor / mov 1
  BOOL PathForceMoveFree();     // 0x42BB00 (121 B): xor / mov 1 / inc
  BOOL FinishUnitMovement();    // 0x42AB10 (152 B): xor / mov 1
  void MoveInBounds();          // 0x42AA60 (166 B): no EAX-set exit
  BOOL PathFaceTarget();        // 0x42B2B0 (191 B): xor / mov 1 / inc
  BOOL PathFixTerrain();        // 0x42B700 (233 B): xor / mov 1
  BOOL PathTerrainSlide();      // 0x42C470 (247 B): xor / mov 1
  BOOL PathWaitFree();          // 0x42C670 (277 B): xor / mov 1
  BOOL FinishUnitMovementOrStop();            // 0x42ABB0 (102 B): only xor eax

  // Callee-only declarations for the unported CUnitPathCollide.cpp bodies, so that a caller
  // here reaches the member spelling: the oracle callee symbols carry the CUnit:: prefix,
  // and a free spelling is a [CALLEE] delta.
  CUnit *GetCollisionUnit();        // 0x42D900 (143 B): returns a CUnit* (this/NULL shape)
  BOOL   GetCollisionWithTerrain(); // 0x42D990 (158 B): `mov eax,1h` exits
  int    sub_42DB60();              // 0x42DB60 (242 B): returns in ESI then `mov eax, esi`,
                                    //   a 32-bit value; PathFixTerrain stores the low byte
                                    //   into pPath->bDirection. Declared here (not TU-local)
                                    //   because gen must find a unique declaration to
                                    //   emit the callee thunk for the marked row.
  CUnit *GetGroundCollisionUnit();  // 0x42DA30 (20 B): a pointer, not void --
                                    //   the callee's own body is `xor eax,eax / ret` else
                                    //   `jmp CUnit::GetCollisionUnit`, and FinishUnitMovementOrStop
                                    //   truth-tests the result

  // The collision, overlap and path-lifecycle members of CUnitPathCollide.cpp,
  // CUnitPathUtil.cpp, CUnitPathBuild.cpp and the rest of CUnitPath.cpp. Every signature is
  // derived off the oracle bytes; where a derivation was overruled the reason is on the line.
  BOOL CheckResolveCollision();    // 0x42CA80 (264 B)
  int  sub_42CC70(void *pArg0);    // 0x42CC70 (359 B)
  int  sub_42CDE0(void *pArg0);    // 0x42CDE0 (116 B)

  // Both return the pointer they were handed: each reloads its own argument
  // into EAX before the epilogue, which no caller reads.
  mpoint_t *FindFree(mpoint_t *pPos);              // 0x42CE60 (139 B); Hyperion: map points
  mpoint_t *FindFree_FromPoint(mpoint_t *pPos);    // 0x42CEF0 (935 B)

  // EAX here is incidental, so void stands: one holds the argument only
  // because the last store goes through it, the other a leftover WORD.
  void FindFree_FromUnit(point16_t *pPos, void *pArg1);  // 0x42D2A0 (1235 B)
  void sub_42D780(void *pArg0, void *pArg1);             // 0x42D780 (170 B)

  BOOL sub_42D830();                        // 0x42D830 (203 B)
  int  sub_42DA50(int nArg0, int nArg1);    // 0x42DA50 (262 B)
  int  sub_42DC70(void *pArg0);             // 0x42DC70 (638 B)
  void DispatchUndoTransparentWorker();     // 0x42E0D0 (91 B), tail jmp pins void

  // The coordinates are short, not WORD: the compares are `cmp ax, cx / jl`,
  // a signed 16-bit compare.
  BOOL IntersectsType(WORD wType, spos_t x, spos_t y);   // 0x42E360 (151 B)
  BOOL IntersectsTypeAt(upos_t wX, upos_t wY, int nType, upos_t wOtherX, upos_t wOtherY);

  int  sub_42E6C0(int nArg0, int nArg1, mpoint_t *pOut);  // 0x42E6C0 (738 B); Hyperion: map points
  BOOL SetPathScript(BYTE bArg0);   // 0x42E9B0 (86 B), ECX unread, class-prefixed
  intptr_t  sub_42EAB0(mpoint_t pt);     // 0x42EAB0 (313 B), the point by value

  // Callee-only, so a caller here thunks the member spelling gen expects.
  intptr_t  GetUnitsInRectangle(int nArg0, int nArg1);  // 0x421DC0 (910 B)
  intptr_t  sub_4217F0(int nArg0, int nArg1);           // 0x4217F0 (1002 B)
  intptr_t  sub_421C80(int nArg0, int nArg1);           // 0x421C80 (309 B)
  BOOL sub_48EE40(int nArg0);                      // 0x48EE40 (202 B)
  void PathOneStep(point16_t ptA, point16_t ptB);   // 0x48EF10 (183 B): slots read only as
                                // whole dwords; the struct seat is the call site's, not this body's.
  int  ProgressRepulse();                          // 0x481880 (373 B)
  void FinishRepulse(int nArg0);                   // 0x4815D0 (231 B)
  void UpdateRepulseDirection();                   // 0x4814E0 (234 B)

  // `this` is never dereferenced here: it is stored whole into a local request
  // block, and the one caller passes `xor ecx, ecx`. The points are by value:
  // as ints the from-pair costs a second callee-saved register.
  WORD CreatePath(point16_t ptFrom, point16_t ptTo);  // 0x48E690 (155 B)

  void KillPath();                 // 0x42C8A0 (30 B)
  BOOL sub_42C8C0(point16_t *pPos);// 0x42C8C0 (108 B), materialised 0 on the guard arm
  BOOL PathAtRest();               // 0x42B030 (376 B)
  int  PathFixCollision();         // 0x42B3B0 (796 B)
  BOOL PathFlyer();                // 0x42B7F0 (211 B)
  BOOL PathFollowPath();           // 0x42B8D0 (558 B)
  BOOL PathGetFree();              // 0x42BB80 (313 B)
  BOOL PathInit();                 // 0x42BCC0 (333 B)
  int  PathMoveToLegal();          // 0x42BE70 (151 B), four materialised values
  BOOL PathScoutPath();            // 0x42C030 (430 B)
  int  PathSlideFree();            // 0x42C1E0 (428 B)
  BOOL PathTurnAndStart();         // 0x42C570 (158 B)

  // 0x42A1E0, ret 4h. The parameter is BYTE, and neither end of the call can show it: the
  // body opens mov al, byte ptr [ebp+8h], which MSVC emits out of a 4-byte slot at either
  // declared width, and both landed callers pass a constant, which is pushed as a dword
  // either way. The sibling ReplaceBackgroundOrder (0x42A180) is BYTE on the same footing
  // and stores the same field.
  void SetBackgroundOrder(order_id_t bOrderId);

  // 0x429DE0 -- `mov ecx, esi / call / test eax, eax` => __thiscall, no
  // argument, truth-tested result.
  BOOL IsZergBldgUpgrading();

  // CUnit::IsDead is declared above, next to its body's TU; FromIdAlive calls it with
  // `this` computed into ECX, nothing pushed, and test eax, eax after.

  // 0x422C00 -- `call CUnit::CancelAddon` with `this` already in ECX.
  void CancelAddon();

  // 0x4E3B90 -- `push 1h / push eax / call`, the pushed CUnit* first, so the
  // source order is (target, flag).
  BOOL CanTarget(CUnit *pTarget, int nFlags);

  // 0x421060 (184 B) -- one pushed argument, result truth-tested. The argument is a CUnit *
  // and the body settles that, not the call site: it loads [ebp+8h], tests it, and on null
  // falls back to [ebx+5Ch], pOrderTarget -- a parameter that defaults to pOrderTarget has
  // pOrderTarget's type. It is then passed to CanSeeUnit and tested at +0xDC. The same
  // default-to-pOrderTarget idiom as sub_421120. A cast written only to satisfy this header
  // is a sign to re-derive the header, not to explain the cast.
  BOOL IsUnitInWeaponRange(CUnit *pTarget);

  // 0x432060 -- `call` with `this` in ECX and no push; the result is null-tested
  // and then handed to SetOrder_CUnit as its target, so it is a CUnit *.
  CUnit *sub_432060();

  // Callees of the CUnitCombat.cpp bodies, none of them ported. Each signature comes from
  // the call site in the matched caller -- what is in ECX, what is pushed in what order,
  // and what the caller does with EAX next. Argument order follows the pushes right to left.

  // 0x4229A0 (CUnitFind.cpp). sub_421120's two call sites both push the target then the
  // widened range, so the range is the first argument. The result is the caller's own
  // return value, which its callers truth-test.
  BOOL IsWithinTargetRange(int nRange, CUnit *pTarget);

  // 0x4203D0. DamageNotify pushes a 0/1 built by xor/cmp/setnz, then the attacker, then
  // calls, so the attacker is first and the flag second. EAX is not read after.
  void OnDamaged(CUnit *pAttacker, int bNotify);

  // 0x422FE0 (CUnitGUI.cpp). One argument, forwarded by DamageNotify straight out of its
  // own second parameter slot. BYTE is the parameter's own width -- the whole-dword push is
  // MSVC declining to clear upper bytes a sub-dword parameter does not define, which is
  // what the oracle emits and what the caller's proof depends on. EAX unread.
  void CreateShieldEffect(BYTE bDirection);

  // 0x41DE10. `mov ecx, esi / call` with no push and EAX unread.
  void ChangeTarget();

  // 0x420770. Same nullary shape, but the result IS read (`test eax, eax / jz`)
  // => BOOL.
  BOOL sub_420770();

  // 0x41F630. One pushed immediate at the one call site (push 5h), which fixes the count
  // but not the meaning of 5; int at the slot's width, since nothing narrows it. EAX unread.
  void sub_41F630(int nArg);

  // 0x420BD0. Nullary, result truth-tested => BOOL.
  BOOL sub_420BD0();
  void sub_420520();             // 0x420520 (223 B), nullary; its one caller discards EAX

  // 0x43A0D0 (CUnitUpdate.cpp). Two pushed arguments in DispatchTowerTarget, y then x, so
  // the source order is (x, y); both are movsx-widened sprite coordinates, hence int. The
  // result is not read at that call site, so BOOL states the callee's own contract -- but
  // the return type rides in the mangling and so in the generated thunk's symbol.
  BOOL SetMoveTarget_xy(int x, int y);

  // 0x43A0A0 (48 B) / 0x43A070 (46 B) -- the two destination setters DispatchIgnoreMove
  // chooses between. The result IS consumed, and void could not express the merge: the
  // CUnit form looks like a discarded call because the `test al, al` sits under the _xy
  // arm, but the two arms merge onto that test (the jmp at +0x67 lands on +0x78).
  // BOOL and not BYTE. A byte-wide test proves only that the caller wanted the low byte.
  // Both bodies pass their result through at full width (mov esi, eax then mov eax, esi),
  // what they pass through is SetMoveTarget_xy's already-derived BOOL, and
  // DispatchAttackMoveEP +0x82 does call / test eax, eax with no widening between them,
  // which a BYTE return cannot produce. A BOOL return produces both call sites.
  BOOL SetDestination_CUnit(CUnit *pTarget);
  BOOL SetDestination_xy(int x, int y);

  // src117/lang/CUnitOrder.cpp -- the order queue. Each installer passes its coordinates
  // by value as one 4-byte point16_t (src117/h/point16.h), beside a target and a unit type.

  // SetOrder (0x42A2C0) and EndDispatchOrder (0x42A5A0) are declared in the block above and
  // are not re-declared here; a second declaration is a C2535.

  // 0x42A340, ret 0Ch. The canonical builder: two int parameters narrowed into the point.
  void SetOrder_xy(order_id_t bOrderId, int x, int y);

  // 0x429D20, ret 4h. Unlink one node, clamp the counter, free it. The argument is a
  // COrder *: the body reads COrder::bOrderId off it and hands it to CLink::Unlink as this.
  void RemoveQueuedOrder(COrder *pOrder);

  // Callees the four above reach, declared for the call only.

  // 0x429F60, ret 14h = five stack arguments, in the order the pushes imply right to left;
  // the constant 0xE4 fills the unit-type slot when a caller has no type.
  void SetOrder_Target_type_interrupt(order_id_t bOrderId, order_target_t t,
                                      int nUnitType, BOOL bInterrupt);

  // 0x42A610. Nullary thiscall; every order installer ends with it, right after setting
  // bUnion93 bit 0.
  void ManageQueuedOrders();

  // The rest of the family. Each signature comes from `ret N` plus the whole push sequence:
  // order_target_t is 8 bytes, everything else 4.

  // 0x429D10, 7 B. No arguments; only AL is defined at the ret, the upper three bytes of
  // EAX still holding the queue-head pointer it was loaded from.
  order_id_t GetFirstQueuedOrder();

  // 0x429CC0. No arguments.
  void EndNoBreakCodeSection();

  // 0x429AC0. No arguments; drains the whole queue.
  void RemoveAllOrders();

  // 0x42A0E0, ret 4h. BYTE, not int: the body compares the byte straight against
  // COrder::bOrderId and re-widens it with movzx for the table index.
  void RemoveOrder(order_id_t bOrderId);

  // 0x42A140. No arguments; no frame, ends in a tail jmp to COrder::FreeOrder.
  void RemovePreviousOrder();

  // 0x42A180, ret 4h. A byte store to bBackgroundOrderId, so BYTE as for RemoveOrder.
  void ReplaceBackgroundOrder(order_id_t bOrderId);

  // The Append/Prepend pair. ret 8h = two dwords; the second is the queue position,
  // forwarded into *_Target_type's fifth slot, which uses it as a CLink node.
  void AppendOrder(int nOrderId, COrder *pPrevOrder);
  void PrependOrder(int nOrderId, COrder *pPrevOrder);

  // 0x429D70, ret 10h = 4 + 4 + 4 + 4.
  void PrependOrder_xy(int nOrderId, int x, int y, COrder *pPrevOrder);

  // 0x429E90, ret 10h = 4 + 8 + 4. A pure forwarder into PrependOrder_Target_type with
  // nUnitType folded to 0xE4. The order id is int on the callers' evidence, since the
  // forwarder pushes the slot whole at either width: ComputerTransport hoists one movzx
  // above the branch and shares that EAX with SetOrder_Target_interrupt's int parameter.
  void PrependOrder_Target(int nOrderId, order_target_t t, COrder *pPrevOrder);

  // 0x429E10, ret 14h = 4 + 8 + 4 + 4. The 8 is order_target_t.
  void PrependOrder_Target_type(order_id_t bOrderId, order_target_t t, int nUnitType,
                                COrder *pPrevOrder);

  // 0x4295D0, ret 14h = 4 + 8 + 4 + 4 -- the Append twin of the line above, agreeing slot
  // for slot: +0x0C and +0x10 reach COrder::AllocOrder as pt and pTarget, so they are the
  // order_target_t halves, and [ebp+18h] is reached with lea eax, [esi+78h], a CLink node.
  void AppendOrder_Target_type(order_id_t bOrderId, order_target_t t, int nUnitType,
                               COrder *pPrevOrder);

  // 0x42A3E0, ret 0Ch = 4 + 8, forwards [ebp+0Ch] as one order_target_t. BYTE:
  // ManageQueuedOrders pushes its BYTE local raw.
  void SetOrder_Target(order_id_t bOrderId, order_target_t t);

  // 0x42A0C0, ret 10h = 4 + 8 + 4.
  void SetOrder_Target_interrupt(int nOrderId, order_target_t t, BOOL bInterrupt);

  // 0x42A300 / 0x429EE0, ret 10h = 4 + 4 + 4 + 4 each. SetOrder_type_xy pushes its own
  // unit type where the rest of the family pushes the constant 0xE4.
  void SetOrder_type_xy(order_id_t bOrderId, int nUnitType, int x, int y);
  void SetOrder_xy_interrupt(int nOrderId, int x, int y, BOOL bInterrupt);

  // 0x42A4D0. No arguments.
  void Suicide();

  // 0x42A850, ret 14h = five stack dwords, all forwarded. The roles come from lining the
  // two forwarders up against the one callee they share: slot a5 is 0xE4, the family's
  // no-unit-type sentinel, which makes p3 a unit type and a6/a7 the (x, y) pair. The names
  // are roles read that way and their signedness is not discriminated.
  // p2 is BYTE and must stay in step with sub_42A880's nOrderId and sub_42A8B0's arg3:
  // this body forwards it raw, and narrowing one link alone costs it a widening step (43
  // against the oracle's 47 bytes).
  void sub_42A850(int nEnable, order_id_t nOrderId, int nUnitType, int x, int y);

  // 0x4297F0, ret 14h. p1 is BYTE (mov al, byte ptr [ebp+8h]); p2 is a unit type and not a
  // unit -- the sibling ErrorRClick takes a CUnit * in that slot and dereferences it. p3/p4
  // are map coordinates this body never reads; p5 is the WORD * the failure path writes
  // under a null guard.
  BOOL ErrorLClick(order_id_t bAction, WORD wUnitType, int x, int y, WORD *pwError);

  // 0x42A8B0, ret 24h = nine stack dwords: seven scalars plus the by-value order_target_t.
  // Slot 4 is a CUnit *, forwarded into SetOrderEx p3. arg3 is BYTE, the middle link of the
  // chain the two forwarders above feed; declared only, so the width costs nothing here and
  // buys both landed forwarders their raw push and their oracle sizes.
  void sub_42A8B0(int nKind, int nEnable, order_id_t nOrderId, CUnit *pTarget,
                  int nUnitType, int x, int y, order_target_t t);

  // 0x42A410 / 0x429650, ret 20h: sub_42A8B0's two dispatch targets; nKind picks which
  // installer gets the (target, type, x, y, t) payload.
  void SetOrderEx(int nKind, order_id_t bOrderId, CUnit *pTarget, int nUnitType,
                  int x, int y, order_target_t t);
  void AppendOrderEx(int nKind, order_id_t bOrderId, CUnit *pTarget, int nUnitType,
                     int x, int y, order_target_t t);

  // Declared for the call only. CUnit::Suicide calls it with `this` still in ECX and
  // nothing pushed; EAX is dead across it.
  void DropInventory();

  // 0x4293A0 -- one pushed argument, a raw speed, full-width result. DWORD and not int:
  // the body divides by 2 with shr and compares against its 0x355 floor with jnb, both the
  // unsigned form. Written signed, MSVC emits the rounding correction a signed divide needs
  // and the wrong branch. GetModifiedSpeed below is the same story.
  int ModifyGivenMovementSpeed(DWORD dwSpeed);

  // 0x43B0B0 -- one stack argument whose low byte is all the callee reads, though the slot
  // is a dword and sub_429500 passes one of its own dword parameters through. Void:
  // sub_429500 overwrites EAX on the next instruction.
  void sub_43B0B0(int a1);

  // Callees of src117/lang/CUnitCombat.cpp.

  // oracle 0x41EE10 (156 B). The tail-jump target of DispatchTurretGuard: nullary, with no
  // result, and void because a non-void callee cannot be tail-called from a void function.
  void DispatchPlayerGuard();

  // oracle 0x420030 (175 B), ret 4h. One dword slot of which only the low byte is used, a
  // weapon id, so BYTE. No result: FireSpell overwrites EAX straight after each call.
  void MakeBullet(weapon_id_t bWeaponId);

  // src117/lang/CUnitSearches.cpp -- the AI target searches. Each line says whether the
  // body is in src117 or unported, which decides whether the hook table or the thunk binds it.

  // oracle 0x424460 (94 B), unported. Nullary; SearchResourceDepot truth-tests the full EAX.
  BOOL IsResourceDest();

  // oracle 0x4242F0 (8 B), unported, whole body movzx eax, byte ptr [ecx+0D3h] / ret.
  // BOOL and not BYTE: sub_432510 consumes the whole result with neg eax.
  BOOL sub_4242F0();

  // oracle 0x41FBE0 (49 B), unported, and a different function from GetWeaponGndEx
  // (0x41FC20) declared above: AISetMinRange calls both weapon-column accessors back to
  // back. Returns char, not BYTE -- the no-weapon sentinel is compared signed, cmp bl, -7Eh.
  weapon_id_t GetWeaponGnd();

  // oracle 0x426B80 (431 B), unported. One stack argument, the bitmask of players this unit
  // just became invisible to. No result.
  void RemoveTargetReferencesForPlayers(DWORD dwRemoved);
  // Hyperion: the same over a set of every slot, for a game with Hyperion's slots
  // (stage D, playerset.h)
  void RemoveTargetReferencesForPlayers(const struct PlayerSet &qRemoved);

  // oracle 0x4316B0 (229 B), unported. Nullary; its result is compared against dwUnionE4 at
  // full width -- the freshly recomputed detection/vision mask.
  DWORD sub_4316B0();

  // 0x4317A0 (36 B), declared for the call only. Nullary; the result passes through
  // neg/sbb/neg and is truth-tested.
  BOOL HasEnemyInWeaponRange();

  // 0x4319D0, declared for the call only. One pushed unit argument. int and signed, not
  // BOOL: ChangeTarget consumes the result as a score, with cmp/jnl and cmp/cmovl picking
  // the smaller candidate, which a 0/1 contract cannot express.
  int sub_4319D0(CUnit *pTarget);

  // oracle 0x431A90 (79 B), defined in src117. Nullary; both exits define EAX at full width.
  BOOL sub_431A90();

  // oracle 0x431B70 (168 B), unported, and the whole of sub_432060 is a 5-byte tail jmp to
  // it, so the two share one signature. The return type is read off sub_432060's own
  // caller, which null-tests it and hands it to SetOrder_CUnit as a target.
  CUnit *sub_431B70();

  // oracle 0x431F00 (110 B), defined in src117.
  CUnit *sub_431F00();

  // oracle 0x431FE0 (120 B), defined in src117. ret 8h over a packed point16_t and a flag.
  // The ledger names it a member, so a free spelling is a mangled name the thunk misses.
  CUnit *FindFreeMineral(mpoint_t pt, int nFlag);  // Hyperion: a map point
  // oracle 0x431F70 (102 B), in src117. ret 4h over one packed point16_t; both call sites
  // take the full EAX as a mineral field.
  CUnit *sub_431F70(mpoint_t pt);  // Hyperion: a map point

  // oracle 0x432560 (50 B), in src117. Void, and a tail call proves it: its last act is
  // jmp CSprite::Hide.
  void sub_432560();

  // oracle 0x4325A0 (123 B), in src117. Void for the same reason -- two of its three exits
  // are tail jmps.
  void sub_4325A0();

  // oracle 0x432BB0 (17 B), in src117.
  CUnit *sub_432BB0();

  // oracle 0x432F10 (44 B), in src117. The name is the ledger's and the map's.
  CUnit *AIFindNearestBunker();

  // oracle 0x433C50 (113 B), in src117. Void: every exit stores to dword_665D20 and leaves
  // nothing in EAX that a caller reads.
  void AISetMinRange();

  // oracle 0x4A7B80 (58 B), oracle TU statselect.cpp, unported. No arguments, no result:
  // sub_432560 calls it with no push, no stack adjustment and no inspection of EAX.
  void sub_4A7B80();

  // oracle 0x442BC0 (81 B) / 0x442C20 (156 B), oracle TU find.cpp, both unported. The two
  // generic unit searches this block dispatches through: thiscall with three pushed dwords
  // (ret 0Ch), sub_442BC0 taking a search radius and FindNearestInRect a pointer to the
  // search area, both returning the unit found. The filter type is spelled inline rather
  // than through CUnitSearches.h's typedef so this header stays includable on its own.
  CUnit *sub_442BC0(int nRange, BOOL (__fastcall *pFilter)(CUnit *, CUnit *), CUnit *pUnit);
  // Hyperion: the area is a box of map positions, typed so that every
  // caller's box is checked
  CUnit *FindNearestInRect(const mrect_t *pSearchData, BOOL (__fastcall *pFilter)(CUnit *, CUnit *), CUnit *pUnit);

  // oracle 0x437060 (125 B), oracle TU CUnitMedic.cpp, unported. Thiscall with one pushed
  // dword; both operands are dereferenced as CUnit *. BOOL from the callers -- three of the
  // four follow the call with test eax, eax, and the callee's exits are neg/sbb/inc and
  // xor eax, eax. ai_test_best_heal_target calls it with the two units swapped: the search
  // source becomes `this` and the candidate the argument.
  BOOL sub_437060(CUnit *pCandidate);

  // oracle 0x438B50 (46 B), unported. No arguments: the whole body indexes gulUnitTypeFlags
  // off wType, runs a test/cmp ladder and leaves on a bare ret. BOOL -- its exits are
  // mov eax, 1h and xor eax, eax, and its one caller follows with test eax, eax.
  BOOL IsIrradiateTargetForAI();

  // oracle 0x429870 (ErrorRClick) and 0x434130, both unported. Arity from the callee's own
  // retire count: ret 14h is five dword slots, ret 18h six, with `this` in ECX on top of
  // that. BOOL from the callers, which truth-test or neg the result.
  //
  // ErrorRClick's first parameter is BYTE, and only a caller can show that: a callee that
  // narrows a dword slot to a byte is indistinguishable from one whose parameter is a byte.
  // usel_click_target keeps the value in a BYTE local and pushes that slot raw, upper three
  // bytes stale, which is only correct if the callee never reads them -- an int parameter
  // makes MSVC materialise a valid int instead, which CSEs with the order index, spills ESI
  // and costs 7 bytes in the caller. ErrorLClick's own first parameter was already BYTE.
  BOOL ErrorRClick(order_id_t bErrorKind, CUnit *pTarget, int nX, int nY,
                   WORD *pwErrorString);
  BOOL sub_434130(WORD wOrderId, CUnit *pTarget, int nUnitType,
                  int nX, int nY, WORD *pwErrorString);

  // oracle 0x41FE60 (110 B), oracle TU CUnitCombat.cpp. The attack-cone test: is the
  // target's angle within this weapon's permitted arc. ret 0Ch proves the three stack
  // slots, bWeapon is consumed through AL as a byte table index, and both oracle callers
  // test the result, which a setle al exit produces.
  BOOL IsInAttackAngle(weapon_id_t bWeapon, int x, int y);

  // oracle 0x431C20 (27 B), oracle TU CUnitSearches.cpp. A radius-24 vulnerability search
  // around this unit's sprite position: pushes 0x18 and the filter sub_431980, reads the
  // sprite x and y into ECX/EDX, tail-calls find_first_unit_near and passes its CUnit *
  // through. It returns that pointer and not void: its own body is byte-identical either
  // way, but both oracle callers (DispatchPowerup, DispatchPowerupCTF) test EAX.
  CUnit *sub_431C20();

  // Callees of CBullet.cpp's lurker-spine splash callback (0x409770), both in the oracle's
  // CUnitCombat.cpp and both unported.

  // oracle 0x41DBF0 (230 B). Thiscall with two dword slots (ret 8h): a BYTE weapon id,
  // range-checked unsigned against 0x82 (130, the weapons.dat row count) and used as a
  // weapons.dat index, and a CUnit * target dereferenced at +0xDC, +0x64 and +0x4C. `this`
  // is the attacking unit, used once for this->bPlayer against pTarget->bPlayer. The result
  // is a masked flag (and eax, 40h), not a normalised 0/1 -- do not compare it against 1.
  // The splash caller never reloads ECX, so `this` is the same object it passes as pTarget
  // and that player comparison can never fail; reproduced as 1.17 has it.
  BOOL CanTargetWithWeapon(weapon_id_t bWeapon, CUnit *pTarget);

  // oracle 0x409CC0 (88 B) and 0x409D20 (79 B), both unported. Thiscall, ret 4h / ret 0Ch,
  // and both fill an out-rectangle: four WORD stores at [ecx+0]/[ecx+2]/[ecx+4]/[ecx+6].
  // Void, and only the callers can say so, since call/ret compiles the same either way:
  // none tests EAX, each reads back the stack local it passed in. GetBounds_xy's x and y
  // are 16-bit on the same evidence -- one caller pushes a packed dword whose upper half is
  // y, another a misaligned dword, and a slot is left dirty only for a narrow parameter.
  // Hyperion: boxes of map positions; on x64 the 1.17 box is filled
  // through one, for the callers not yet moved
  void GetBounds(mrect_t *pBounds);
  void GetBounds_xy(mrect_t *pBounds, spos_t x, spos_t y);
#if defined(_WIN64) || defined(__LP64__)
  void GetBounds(rect16_t *pBounds) { mrect_t rc; GetBounds(&rc); *pBounds = rc; }
  void GetBounds_xy(rect16_t *pBounds, spos_t x, spos_t y) { mrect_t rc; GetBounds_xy(&rc, x, y); *pBounds = rc; }
#endif

  // oracle 0x41E290 (91 B). Thiscall with two dword slots (ret 8h) plus `this`.
  //   arg 1 is the bullet, dereferenced at four CBullet offsets (+0x62, +0x21, +0x60, +0x64).
  //   arg 2 is a plain dword forwarded into CBullet::sub_409010, never narrowed or tested.
  //     The splash paths pass 1, 2 and 4, which reads as a damage divisor, but that comes
  //     from the callers, so the name does not claim it.
  //   Void: EAX is live out of both arms as the tail call's register, not a result.
  void DamageFromBullet(CBullet *pBullet, int nDivisor);

  // The static members. Static and not thiscall, and the oracle says so at both ends: the
  // call sites load ECX with a plain integer -- a player index, a unit id -- rather than
  // with an object pointer, and neither body treats ECX as `this`. One register argument
  // and no stack argument, so __fastcall.

  // oracle 0x4256F0 (8 B), whole body mov eax,[ecx*4+665B08h] / ret. The head of the
  // per-player unit list; the table at 0x665B08 is sgpPlayerLists_g. Declared only.
  static CUnit *__fastcall GetPlayerUnitList(int player);


  // oracle 0x4255F0 (6 B), declared for the call only. No argument at all: both GetUnitID
  // and FromIdAlive turn the result into a table index, so it returns the first element.
  static CUnit *__fastcall Head();

  // oracle 0x428B50 (76 B), in CUnitMisc.cpp. Static, and the body says so: it opens
  // mov esi, ecx and then does integer arithmetic on ESI, so ECX is the packed unit id and
  // not an object. Returns the live unit that id names, or null when the slot was recycled
  // (bIdGeneration disagrees) or its occupant is dead.
  static CUnit *__fastcall FromIdAlive(DWORD dwUnitId);

  // 0x428BA0 (37 B). FromIdAlive's unconditional sibling: static, the id in ECX, and
  // Head() - 0x150 + (id & 0x7FF) * 0x150, with a xor eax, eax arm for id 0.
  static CUnit *__fastcall FromId(DWORD dwUnitId);

  // The unit master lists (src117/lang/CUnitInit.cpp). Every CUnit is on exactly one
  // intrusive list through the pPrev/pNext pair at +0x00/+0x04, and that list is its
  // lifecycle state. Each is a (head, tail) pair of globals, contiguous in .data:
  //     0x665AE0/AE4  sgpUsedHead    / sgpUsedTail      live units
  //     0x665AE8/AEC  sgpDeadHead    / sgpDeadTail      dying, orders draining
  //     0x665AF0/AF4  sgpFreeHead    / sgpFreeTail      recycled slots
  //     0x665AF8/AFC  sgpTransHead   / sgpTransTail     in transports
  //     0x665B00/B04  sgpScannerHead / sgpScannerTail   scanner sweeps
  //     0x665B08      sgpPlayerLists_g[12]              per-player heads, +0x68/+0x6C
  // Each accessor below is mov eax, dword ptr [<addr>] / ret with no ECX use, so each is a
  // static returning the pointer at that address. Which half is the head comes from the
  // (ppHead, ppTail) order the CLink routines take, and four bodies depend on it.
  // __fastcall on a zero-argument static decorates the name and changes no code.

  static CUnit *__fastcall GetUsedList();          // 0x425600, [0x665AE0]
  static CUnit *__fastcall GetUsedListTail();      // 0x425DB0, [0x665AE4]
  static CUnit *__fastcall GetDeadList();          // 0x4255B0, [0x665AE8]
  static CUnit *__fastcall GetDeadListTail();      // 0x425D80, [0x665AEC]
  static CUnit *__fastcall GetTransList();         // 0x4255E0, [0x665AF8]
  static CUnit *__fastcall GetTransListTail();     // 0x425DA0, [0x665AFC]
  static CUnit *__fastcall GetScannerList();       // 0x4255D0, [0x665B00]
  static CUnit *__fastcall GetScannerListTail();   // 0x425D90, [0x665B04]

  // oracle 0x4255C0 (11 B): movzx eax, cl / mov eax,[eax*4+665B08h] / ret. The parameter is
  // a BYTE and the movzx proves it -- GetPlayerUnitList three lines below reads the same
  // table with no widening at all, so that index arrives 32 bits wide and this one narrow.
  static CUnit *__fastcall GetPlayerList(BYTE bPlayer);

  // oracle 0x425690 (25 B). No ECX use, no arguments; releases the two gamedata array
  // tables and squelches voice.
  static void __fastcall FreeUnits();

  // oracle 0x425020 (44 B). movzx esi, cl, so ECX is a BYTE value and not a `this`: a
  // static __fastcall(BYTE). Drains the pending create-unit error toward that player.
  static void __fastcall ShowCreateError(BYTE bPlayer);

  // The four (head, tail) list operations, thiscall with no arguments. Each is three
  // instructions: push the tail address, push the head address, call the CLink routine.
  // The pushes run right to left, so the head is argument one, matching
  // CLink::PrependThis(ppHead, ppTail) and CLink::Unlink(ppHead, ppTail).
  void MoveToTransList();        // 0x425DC0, PrependThis(&TransHead, &TransTail)
  void MoveToUsedList();         // 0x425DD0, PrependThis(&UsedHead,  &UsedTail)
  void UnloadFromTransList();    // 0x427070, Unlink(&TransHead, &TransTail)
  void UnloadFromUsedList();     // 0x427080, Unlink(&UsedHead,  &UsedTail)

  // oracle 0x425700 (12 B): or byte ptr [ecx+93h], 4h / jmp CUnit::Suicide. A tail jump, so
  // it returns exactly what Suicide returns -- void -- and takes no arguments. Sets bit 2
  // of bUnion93 before dying.
  void SelfDestruct();

  // oracle 0x424DB0 (35 B). FinishedMaking's conditional sibling: run FinishConstruction,
  // then Complete only if sub_426600 says so. BOOL and not void -- mov eax, 1h on that arm,
  // and the fallthrough leaves sub_426600's own zero.
  BOOL sub_424DB0();

  // oracle 0x4256B0 (57 B). Decrements the live-unit counter, drains the order queue, then
  // moves the unit from the dead list to the free list -- the one body that names
  // sgpFreeHead/sgpFreeTail, which have no accessor of their own. Clears pSprite first.
  void RemoveDead();

  // oracle 0x425CA0 (33 B). wType >= 0xCB && wType <= 0xD5, an unsigned range test (jb and
  // jnbe), returning 1 or 0 in EAX.
  BOOL IsDoodad();

  // oracle 0x425CD0 (60 B). Destroys each linked unit carrying units.dat flag 0x10, then
  // runs DestroyImpl and kills the sprite. Recursive: the chain walk is a self-call.
  void Destroy();

  // oracle 0x426530 (151 B). Takes the unit off both of its memberships -- the per-player
  // chain (+0x68/+0x6C, repairing sgpPlayerLists_g[bPlayer] when it was the head) and
  // whichever master list its type and sprite flags put it on -- then prepends it to the
  // dead list. The three-way choice of source list is trans / used / scanner.
  void sub_426530();

  // oracle 0x426E80 (68 B), ret 4h. One stack argument read as a DWORD and pushed whole
  // into CSprite::SetCreator, whose own parameter is a BYTE: a narrow callee slot lets MSVC
  // push a register it already has rather than narrowing it first.
  void SetOwnerColor(int nCreator);

  // Callees of these bodies, declared and not defined here. 1.17 is WPO-off, so a call to a
  // body src117 does not define compiles to a real REL32 the generated thunk resolves, which
  // is what the oracle emits.
  void FinishConstruction();     // 0x424F40 (213 B), `mov ecx, esi / call`, bare ret
  void Complete();               // 0x424DE0 (342 B), reached by tail jmp => void
  void DestroyImpl();            // 0x425050 (366 B), `mov ecx, esi / call`, bare ret
  BOOL sub_426600();             // 0x426600 (205 B), result feeds `test eax, eax`

  // The CUnitInit.cpp members with no other seat. Each is thiscall with `this` the only
  // register input unless a stack parameter is listed.
  void sub_425D10();             // 0x425D10 (105 B), its one caller discards EAX
  void sub_4251C0();             // 0x4251C0 (168 B), bare ret
  // 0x425710 (592 B), `ret 14h`; the caller tests EAX. The fifth slot is the
  // BYTE sub_424860 forwards.
  BOOL sub_425710(int nUnitType, int x, int y, int nPlayer, BYTE bRand);
  void sub_424A00(WORD nArg);    // 0x424A00 (197 B), `ret 4h`, slot forwarded whole
  void sub_426750(WORD nArg);    // 0x426750 (377 B), `ret 4h`, callers discard EAX
  void sub_424BD0(WORD nUnitType); // 0x424BD0 (345 B), `ret 4h`, slot pushed whole
  BOOL sub_425270();             // 0x425270 (303 B), returns 1 or 0 in EAX
  void RemoveUnitReferences(CUnit *pUnit); // 0x426D30 (325 B), `ret 4h`, compared against unit pointers
  // 0x425A70 (556 B), `ret 10h`: the type slot is read as `mov bx, word ptr
  // [ebp+8h]`, the rest are pushed whole; one caller tests EAX.
  BOOL sub_425A70(WORD wUnitType, int x, int y, int nPlayer);
  // 0x41BF70 (19 B, CUnitBuild.cpp, unported), `ret 4h` with the slot never
  // read; every caller discards EAX.
  void sub_41BF70(int nArg);
  void sub_4253A0();             // 0x4253A0 (350 B), its one caller discards EAX
  // 0x423400 (123 B, CUnitHarvest.cpp, unported), nullary; its one caller
  // discards EAX.
  void sub_423400();
  void DispatchReturnResource();                 // 0x423CE0, tail jmp EndDispatchOrder
  void SetHarvestInventory(CUnit *pHarvester);   // 0x423FD0, `ret 4h`, callers discard EAX
  void DispatchInteractWithGasMine();            // 0x4235C0, no stack parameters
  void DispatchHarvestMinerals();                // 0x4239A0, no stack parameters
  void DispatchHarvestingMinerals();             // 0x423810, no stack parameters

  // oracle 0x41D4C0 (33 B), unported -- the cloak-state setter CImage.cpp's
  // rle_advance_cloak and rle_advance_decloak reach. ret 4h with one dword slot the body
  // narrows itself (mov eax, [ebp+8h] / movzx eax, al), which is what a BYTE parameter in a
  // dword slot looks like; do not widen it. The whole body is a masked write of the
  // argument's low two bits into dwFlags bits 8-9, and all eight call sites discard EAX.
  void SetCloakStatus(BYTE bStatus);

  // oracle 0x4BC460 (84 B). A point hit test without the subunit recursion sub_4BC400 adds;
  // sub_4BC400 tail-calls it on this->subunit. Thiscall, read off the oracle: mov esi, ecx
  // for `this`, the two coordinates at [ebp+8h] and [ebp+0Ch], ret 8h on all three exits.
  // /Gr's default __fastcall does not apply to a non-static member, and a __fastcall
  // spelling would put nX in EDX and lose 0x4BC400 too, since its call site pushes both.
  // int, not BOOL: both coordinates are forwarded whole and each exit returns a callee's
  // result untouched.
  int sub_4BC460(point32_t pt);

  // The harvest block (the oracle's CUnitHarvest.cpp, the 0x4237A0-0x4247B0 span). Arity is
  // `ret N` and it is exact under /Gr: a non-static member is thiscall, so ret 4 is one
  // stack parameter, ret 8 two and a bare ret none. Six of these pairs would be
  // indistinguishable without it -- IsMineralSrc and IsTargetMineralSrc have the same body
  // and differ only in `ret` against `ret 4h`.

  // Five of this block's targets are already declared above as callees of earlier work --
  // IsMineralSrc, UpdateResourceAnim, IsResourceDest, sub_4242F0 -- and a second declaration
  // of any of them is a C2535 rather than a silent overload. Grep this tree, .cpp files
  // included, for a colliding symbol before declaring a name.

  // 0x424380 (33 B) and 0x4243B0 (37 B). The same body; the only difference is the epilogue,
  // `ret` against `ret 4h`. So IsTargetMineralSrc takes exactly one stack parameter and
  // never reads it -- no [ebp+8] access, no frame. Do not declare it nullary: the oracle's
  // arity is the contract every caller was compiled against, and the epilogue would move
  // two bytes. The unread parameter is typed CUnit * from the call sites.
  BOOL IsTargetMineralSrc(CUnit *pHarvester);

  // 0x424300 (47 B) and 0x424330 (67 B). Same shape, the epilogue again the separator, but
  // here the parameter is read: IsTargetRefinery compares the two units' bPlayer, so the
  // extra test is a same-owner check and the parameter is a CUnit *.
  BOOL IsRefinery();
  BOOL IsTargetRefinery(CUnit *pHarvester);

  // 0x4243E0, 53 B. Bare ret, no frame; four equality tests against wType and a 1/0 return.
  BOOL IsNeutralResource();

  // 0x424550, 56 B. Bare ret. Calls GetFirstQueuedOrder with ECX unreloaded, so on `this`,
  // and compares AL against nine order ids.
  BOOL IsHarvestOrderQueued();

  // 0x424750, 94 B, ret 4h. The unlink described at +0xD4/+0xD8 above: `this` is the
  // resource holding the list head, the pushed argument the worker. No path defines EAX.
  void RemoveHarvestUnit(CUnit *pHarvester);

  // 0x423ED0, 182 B. No stack parameter. Walks the +0xD8 chain from this->+0xD4, unlinks
  // the last acceptable node and returns it, or null when the walk is exhausted. Both
  // callers in this TU test the result and use it as `this` for sub_4240D0.
  CUnit *sub_423ED0();

  // 0x4240D0, 93 B, ret 4h. `this` is the worker (its +0x4D read, its +0xCE/+0xD0 written)
  // and the pushed argument the resource, whose +0xD3 gates the whole body. BOOL by the
  // mov eax, 1h / xor eax, eax split and not by its callers, which all discard it.
  BOOL sub_4240D0(CUnit *pResource);

  // 0x424130 (58 B) and 0x424170 (187 B). A mirrored pair, and swapping them costs the
  // register assignment of both: in sub_424130 the worker fields are on ECX, so `this` is
  // the worker and the argument the resource; in sub_424170 they are on the argument, so
  // `this` is the resource. Both ret 4h, both leave EAX undefined on at least one exit.
  void sub_424130(CUnit *pResource);
  void sub_424170(CUnit *pHarvester);

  // 0x424230, 179 B, ret 4h. `this` is the worker, the argument the resource (+0xD3 read,
  // +0xD4 chain spliced). No path defines EAX.
  void sub_424230(CUnit *pResource);

  // 0x4244C0, 133 B. No frame, bare ret, no stack parameter; a guard cascade over `this`
  // and its +0xD0 resource ending in RemoveHarvestUnit / RemoveOrder / sub_4240D0.
  void sub_4244C0();

  // 0x423E40, 133 B, ret 4h. The parameter is read as one byte at both sites, which an int
  // spelling would emit as a dword compare. Every exit defines only AL, so a BYTE return.
  BYTE GetHarvest(BYTE bSuicideWhenEmpty);

  // 0x423C30 (55 B), 0x423C70 (74 B), 0x423CC0 (27 B), 0x4237A0 (101 B), 0x423480 (311 B).
  // Order dispatchers: no stack parameter, and each exits on a bare ret or a tail jmp into
  // EndDispatchOrder / EndUninterruptableDispatchOrder, which only a void function can do.
  void DispatchHarvestMineralsWait();
  void DispatchHarvestInterrupt();
  void DispatchHarvest4();
  void DispatchGasMinePrecondition();
  void DispatchHarvest();

  // 0x423F90, 61 B. Bare ret, no stack parameter. Walks the owner's unit list from
  // GetPlayerUnitList(this->bPlayer) for a unit whose +0x5C is `this`, and returns it or null.
  CUnit *sub_423F90();

  // External callees this block reaches, unported, so the thunk covers them.

  // 0x429B20 (67 B) ret 4h and 0x429BC0 (128 B) ret 8h -- one and two stack parameters.
  // Spelled `int nOrderId` to match the eight SetOrder_* siblings above rather than BYTE:
  // every call site in this block pushes the id as an imm8, which both spellings emit
  // identically, so the width is not observable here and consistency is the only tiebreak.
  void SetNextOrder(int nOrderId);
  void SetNextOrder_CUnit(order_id_t bOrderId, CUnit *pTarget);

  // The build and rally block (src117/lang/CUnitBuild.cpp). Five unit-type predicates share
  // one shape: movzx eax, word ptr [ecx+64h], a chain of equality compares against
  // units.dat ids, and a mov eax, 1h or xor eax, eax exit. The compare encoding splits the
  // family and the source cannot ask for either: 0x48 and 0x53 fit a signed imm8 and emit
  // cmp eax, 48h, while IsZergTownhall's 0x83 does not and emits mov ecx, 83h / cmp ax, cx.
  // Do not chase that difference with casts or a different comparison spelling.
  BOOL IsCarrier();       // 0x41B380 (23 B) -- 0x48 Carrier, 0x52 Carrier(hero)
  BOOL IsReaver();        // 0x41B3D0 (23 B) -- 0x53 Reaver, 0x51 Reaver(hero)
  BOOL IsCarrierType();   // 0x41B3A0 (33 B) -- the IsCarrier pair plus 0x53/0x51
  BOOL IsFighterType();   // 0x41BF90 (23 B) -- 0x49 Interceptor, 0x55 Scarab
  BOOL IsZergTownhall();  // 0x41BFB0 (43 B) -- 0x83 Hatchery, 0x85 Hive, 0x84 Lair

  // 0x41B290 (20 B). Bare ret, no argument. The per-frame hit-point increment a unit under
  // construction gains: wHPGain as-is, or x16 when the cheat bit at 0x5432FC (glCheatBits)
  // is set. The return is WORD and only the callers show it -- a WORD return lives in AX and
  // this callee leaves the high half dirty, while both callers emit movzx eax, ax on the
  // result, which an int return cannot produce. The body is byte-identical at either width.
  WORD GetBuildHpGain();

  // 0x41BB50 (111 B), ret 4h. One stack argument stored straight into lHitPoints at full
  // width, so int. Clamps to the type's units.dat maximum, redraws the HP bar if the sprite
  // wants it, and refreshes the damage overlays and the AI strength score for real units.
  // The clamp compare is jle and not jbe: the value is signed, which matters because
  // callers pass deltas that can go negative.
  void SetLife(int nLife);

  // 0x41B900 (15 B), ret 4h. The tail call is the whole body: it adds lHitPoints to its
  // argument and jmps to SetLife, so SetLife must be in this TU and must not be spliced in
  // here -- at 111 B against this body's 15 it comfortably is not.
  void ModifyLife(int nDelta);

  // 0x41BBC0 (39 B), ret 8h. Two stack arguments, both read as WORDs into wRallyX/wRallyY,
  // with pRallyUnit cleared in the same body. Spelled short to match the field, which the
  // sole consumer sign-extends.
  void SetRallyTarget_xy(spos_t x, spos_t y);

  // 0x41BBF0 (49 B), ret 4h. The mirror of the above: stores the target and copies its
  // sprite position into the pair. A NULL argument rallies the unit to itself (test edx,
  // edx / cmovz edx, ecx), so the parameter is optional and the body never null-derefs.
  void SetRallyTarget_CUnit(CUnit *pTarget);

  // 0x41A540 (63 B). Bare ret, no argument. Ticks wRemainingBuildTime down by 1, or by 16
  // under the same glCheatBits bit GetBuildHpGain reads, clamping at zero. It returns 1
  // only on the already-zero entry path and 0 on every path that decremented, including the
  // one that just hit zero -- it answers whether the unit was already finished.
  BOOL CountDownToCompletion();

  // 0x41B330 (67 B). Bare ret, no argument. Returns dwUnionEC for the addon orders and
  // pOrderTarget for the protoss-warp ones, or falls through to IsConstructingAddon's zero
  // when the unit is building nothing.
  CUnit *GetConstructionTarget();

  // 0x41B1F0 (88 B). Bare ret, no argument.
  void DispatchUndoFlyer();

  // 0x41A280 (182 B) and 0x41A770 (227 B).
  //   arity     `ret 8h` at all five exits, so two stack arguments.
  //   [ebp+8]   read as a full dword and added to lHitPoints before SetLife: an hp amount.
  //   [ebp+0Ch] a 32-bit flag. DispatchConstructBuilding pushes 0 and DispatchTrain 1, and
  //             it selects sub_424DB0() against FinishedMaking().
  //   return    four mov eax, 1h exits against sub_424DB0's zero; DispatchTrain tests it.
  BOOL Build(int nHpGain, BOOL bTrainingUnit);
  void DispatchBuildSelf();   // nullary, bare `ret`, void -- this one was right
  // 0x41A580 (482 B) / 0x41A870 (1169 B) / 0x41AD30 (765 B). Order handlers:
  // nullary with a bare `ret` or a tail `jmp`, reached through the order table.
  void DispatchBuild();
  void DispatchConstructBuilding();
  void DispatchRepair();

  // 0x41B3F0 (189 B), ret 4h. One stack argument, read twice as movzx from a byte, so BYTE;
  // the narrowing is on this side, since Build's call site pushes a zero-extended WORD out
  // of wBuildQueue. Exits mov eax, esi (the geyser it found) and xor eax, eax, consumed by
  // the caller as mov edi, eax / test edi, edi.
  CUnit *ReplaceGeyser(BYTE bUnitType);

  // External callees this block reaches, unported, so the thunk covers them.

  // 0x422DD0 (154 B), oracle TU CUnitGUI.cpp. Called from SetLife with `this` in ECX and
  // nothing pushed; its result is never read there.
  void UpdateDmgOverlays();

  // 0x42F240 (40 B), TU CUnitPBuild.cpp. Called from GetConstructionTarget on the
  // fallthrough path -- call then test eax, eax -- and IsConstructing reads its EAX as that
  // function's zero exit.
  BOOL IsBuildingProtoss();

  // src117/lang/CUnitTerran.cpp. All the nullary members below end in a bare ret or a
  // symbolic tail jump unless their declaration says otherwise.
  void AlignWithTurretBase();
  void BuildingLandCrushBurrowed();
  void DetachAddon();
  void DispatchBuildingLiftoff();
  void DispatchCloak();
  void DispatchDecloak();
  void DispatchMedicReturnToIdle();
  void DispatchScanner();
  void MakeLanded();
  void ToggleFlyingStatus(BYTE bFlying);  // ret 4; callee explicitly widens AL
  void MakeLifted();
  void sub_4370E0();
  void sub_437180();
  CUnit *sub_437290();
  BOOL BuildingCrushTank();               // 0x435A70; neg/sbb/neg on the find result
  void sub_4372F0();                      // 0x4372F0; tail jmp, no stack slot
  void DispatchPlaceScanner();            // 0x436990
  void DispatchNukePaint();               // 0x4366F0
  void DispatchNukeTrack();               // 0x436720
  void DispatchNukeLaunch();              // 0x436540
  void DispatchBuildingLand();            // 0x435BF0
  void DispatchTankMode();                // 0x436BE0
  void DispatchSiegeMode();               // 0x436A80
  // 0x42A1B0 (41 B, CUnitUpdate), `ret 4h`: the slot is pushed whole to
  // CSprite::SetAnimation for this unit and its subunit.
  void SetAnimTurret(int nAnim);
  void DispatchMedicHealMove();           // 0x436310
  void DispatchLiftOff();                 // 0x436030
  void DispatchVultureMine();             // 0x436CF0
  void DispatchPlaceMine();               // 0x436820
  // 0x431E90 (50 B), nullary; the caller stores EAX into pOrderTarget.
  CUnit *FindMineTarget();
  // 0x429550 / 0x429520; the last slot is AppendOrder_Target_type's pPrevOrder.
  // BYTE: CUnit::AppendOrderEx pushes its own BYTE slot raw.
  void AppendOrder_CUnit(order_id_t bOrderId, CUnit *pTarget, COrder *pPrevOrder);
  void AppendOrder_xy(int nOrderId, int x, int y, COrder *pPrevOrder);

  // External member callees these bodies reach.
  void TrackCollision();
  void UntrackCollision();
  void sub_4340D0();

  // Unported callees of CSprite::RestoreSelectsFromSave.
  // oracle 0x422EB0 (65 B). Thiscall, one dword slot (ret 4h); the callee reads it wide
  // (mov ebx, dword ptr [ebp+8h]) and only narrows later, so int and not BYTE. Restores one
  // selection-group slot: routes through the subunit at +0x70 when gulUnitTypeFlags bit 4
  // says so, then re-seats gpNewUnits[nGroup].
  void sub_422EB0(int nGroup);

  // oracle 0x423100 (21 B). Thiscall, bare ret, void: forwards the sprite at +0x0C and the
  // player's circle byte into CSprite::AllocCircle.
  void CreateTeamCircle();

  // src117/lang/CUnitSpells.cpp -- the cloak, energy and status-effect core. For a
  // parameter the callee is the authority; for a return width the callers are.

  // 0x433FC0 (257 B), ret 4h. Not a thiscall: the entry reads both register arguments as
  // 16-bit coordinates (mov ax, cx / mov bx, dx) to build the search rect, and the single
  // caller, CBullet::Damage, loads them with movsx from the sprite position before pushing
  // the caster. That is the ApplyBloodBoil / ApplyEnsnare shape: a static __fastcall with x
  // in ECX, y in EDX, the caster on the stack, callee pops 4. Void -- nothing sets EAX on
  // the exit path.
  static void __fastcall ApplyEMPLocation(int x, int y, CUnit *pCaster);

  // 0x434270 (135 B). Bare ret, no argument. Inlines GetCloakingTech's eight-way switch
  // (the 0x2C/0x09/0x0A ladder appears verbatim inside it), so that callee's TU membership
  // matters here.
  void Cloak();

  // 0x4348C0 (59 B). Nullary. Declared above as WORD on the callers' evidence --
  // EnergyUpdate emits movzx eax, ax.

  // 0x434900 (53 B). Nullary; returns through AL only (mov al, 2Ch / 9h / 0Ah). Three
  // callers emit movzx <reg>, al straight after the call, which an int return cannot
  // produce, so BYTE.
  BYTE GetCloakingTech();

  // 0x4343B0 (169 B) DestroyOverlays and the 0x434A50..0x435300 group are already declared
  // above. Their signatures were re-read against the oracle and none moved.

  // 0x434460 (565 B). Nullary, bare ret. The click-ability dispatcher.
  void DispatchClickAbility();

  // 0x434710 (362 B). Nullary, bare `ret`.
  void DispatchCloakNeighbors();

  // 0x434880 (58 B). `ret 4h`; the slot is read `mov eax, dword ptr [ebp+8h]`
  // as the source unit whose 0x110..0x11F status block is copied over this unit's.
  void CopyStatusEffects(CUnit *pSource);

  // 0x434AC0 (82 B). Nullary. Eight `cmp <slot>, 0h / jnz` rungs falling into xor eax, eax
  // with the taken arm mov eax, 1h -- two written-out returns, not a sete.
  BOOL IsRevealedBySpell();

  // 0x434DA0 (133 B). Nullary. Unlinks `this` from the 0x665D40 list and returns 1 when it
  // did any work, 0 when bCloakCount was still non-zero; CloakUpdate tests the result.
  // __declspec(noinline) is a steering lever and not a derivation: the oracle's own caller
  // emits a real call, while with both bodies in one TU /Ob2 splices all 133 bytes into that
  // site and CloakUpdate measures 283 B against the oracle's 157. The attribute leaves this
  // body's own COMDAT byte-identical, and it goes on the declaration and the definition.
  __declspec(noinline) BOOL sub_434DA0();

  // 0x434E70 (368 B). Nullary, `pop ebp / ret` -- no argument slot at all.
  BOOL CanFireSpell();

  // 0x435020 (135 B). `ret 4h`; the slot is read wide (`cmp dword ptr
  // [ebp+8h], eax`) => int, not BYTE.
  BOOL sub_435020(int a1);

  // 0x4350B0 (301 B), ret 4h. The slot is only ever forwarded, so the callee cannot narrow
  // it: int. BOOL and not void -- all four exits materialise an explicit value (xor eax,
  // eax at 0x4350CB / 0x4351C0 / 0x4351D6, mov eax, 1h at 0x435175), which a void body
  // cannot emit. The single caller discards EAX, so here the callee is the whole proof.
  BOOL sub_4350B0(int a1);

  // 0x4351E0 (279 B). Nullary; ends in a tail jmp to UpdateCrunchyTimers, declared void
  // above, and a tail jmp pins the return type as well as the arity.
  void SpellUpdate();

  // 0x435530 (133 B): nullary, pop ebp / ret, and 0x434D10's sibling. It returns a value --
  // its only caller, sub_435020, tests EAX right after the call, and every path to the ret
  // leaves EAX holding a callee's result (XYInRadius / CanSeeFocusUnit / sub_421120, all
  // BOOL). Written void it measures 128 B against 133: five bytes of return plumbing.
  BOOL sub_435530();

  // External member callees the bodies above reach.

  // 0x42FFD0 (21 B), TU CUnitProtoss.cpp. Nullary; `mov eax, 1h` / `xor eax,
  // eax` exits, consumed by GetMaxEnergyInProgress as `test eax, eax / jz`.
  BOOL IsCompletingSummoning();

  // 0x41D200 (253 B) / 0x41D360 (164 B), TU CUnitCloak.cpp. Both ret 4h and both read the
  // slot wide, so int; CloakUpdate calls them with 0x111 and 0x112, which a BYTE could not
  // hold anyway.
  void sub_41D200(int nImageId);
  void sub_41D360(int nImageId);

  // 0x422D10 (118 B), TU CUnitGUI.cpp. Nullary; accumulates into EBX and exits
  // `and eax, ebx / ret` => BOOL.
  BOOL CanSeeFocusUnit();

  // 0x41CFB0 (37 B), TU CUnitCarrier.cpp. Nullary; walks the +0xC4 list and
  // ends `pop esi / ret` with nothing in EAX => void.
  void OrderInterceptorsReturn();

  // 0x4310F0 (101 B), TU CUnitPSpells.cpp. Nullary; clears bStasisTimer and
  // ends in a `call CUnit::DestroyOverlays` before the epilogue => void.
  void RemoveStasis();

  // 0x431030 (88 B), TU CUnitPSpells.cpp -- the one Sync* member not already declared
  // above. Nullary; ends in a tail jmp to sub_434C60, declared void, so void.
  void SyncStasis();

  // src117/lang/CUnitCarrier.cpp -- the hangar family.

  // 0x41CBA0 (116 B). Nullary. BYTE, and both ends say so: all four exits write AL alone,
  // and both call sites (SetHangarContents, TTAllowed) consume EAX 8-bit.
  BYTE GetHangarCapacity();

  // 0x41C260 (40 B). Nullary. The tail is xor eax, eax / cmp byte ptr [ecx+0E2h], 2h /
  // setz al, and both callers consume the full 32-bit EAX, so BOOL and not BYTE.
  BOOL sub_41C260();

  // 0x41C410 (53 B) / 0x41C3C0 (73 B) / 0x41C290 (124 B). The three carrier order
  // dispatchers: nullary thiscall with a bare ret, and none leaves a value of its own in
  // EAX -- each inherits a void callee's register or leaves through a tail jmp, which pins
  // the return type as well as the arity.
  void DispatchCarrierStop();
  void DispatchCarrierIgnore();
  void DispatchCarrier();

  // 0x41CA90 (130 B), ret 8h. Two stack arguments, the first a function pointer: it is
  // reloaded inside both loops and invoked as mov edx, edi / mov ecx, esi / call eax, i.e.
  // __fastcall(the fighter in ECX, the caller's opaque context in EDX) -- the shape
  // IterateTransport already declares as TransportProc, reused rather than coined again.
  // It visits both hangar lists, docked (+0xC0 head, +0xC8 count) then deployed (+0xC4,
  // +0xC9), stopping on the first non-zero result. Exits xor eax, eax and mov eax, 1h.
  BOOL FighterDoFcn(TransportProc pfnProc, CUnit *pContext);

  // The rest of the hangar family, each a plain thiscall member -- ECX live in, EDX never.

  // 0x41C310 (175 B) / 0x41C860 (170 B): both end in jmp CUnit::EndDispatchOrder, which
  // pins void, and take no stack parameters.
  void DispatchCarrierAttack();
  void DispatchTrilobyteAttack();
  // 0x41C910 (56 B): the Reaver twin of DispatchCarrierStop; no stack params.
  void DispatchTrilobyteStop();
  // 0x41C450 (140 B): tail-jmps CUnit::Suicide / status_set_update => void.
  void DispatchReturn();
  // 0x41D060 (65 B): EAX not defined at any ret; both callers discard.
  void OrderInterceptorsAttack();
  // 0x41CE40 (134 B), `ret 4h`, one dword slot read once (`mov ecx, [ebp+8h]`
  // and then dereferenced at +0xC4/+0xC8 -- the fighter); its one caller
  // (DispatchReturn) discards EAX.
  void HangarHideFighter(CUnit *pFighter);
  // 0x41CED0 (186 B): no stack params, EAX undefined at ret.
  void HangarSelfDestruct();
  // 0x41CFE0 (124 B): the unlink of `this` from its carrier's list (+0xC0 owner,
  // +0xC4 next, +0xC8 prev); no stack params, no defined EAX at any exit.
  void sub_41CFE0();
  // 0x41CB20 (125 B): exits `xor eax, eax` / an immediate, and its one caller
  // (DispatchStrafeUnit) tests the full EAX => BOOL.
  BOOL FighterReturn();
  // 0x41D0B0 (31 B), `ret 4h`: `cmp dword ptr [ebp+8h], eax / setnle cl` against
  // sai_path_compute_distance's 32-bit result, the slot read as a dword once;
  // all three callers (sub_41BFE0) consume 32-bit EAX => BOOL, int parameter.
  BOOL sub_41D0B0(int nRange);
  // 0x41D0D0 (40 B): returns 0x38/0x39 or 0x3D/0x3E through setz al / add eax. The body is
  // byte-identical at BYTE and at int (measured, 40 B both) and the callers store AL, a
  // truncating store that is silent on width, so nothing here decides it; int follows its
  // twin below and that is the only evidence claimed.
  int GetCarrierReaverAttackOrder();
  // 0x41D1C0 (60 B): returns 0x37 / 0x32 / 0x39 / 0x3A / 0x3E. int and not BYTE, measured:
  // at BYTE the body is 57 B against the oracle's 60, because an 8-bit return lets a
  // `return 0x37` on the path where DL is provably 0x37 reuse DL (mov al, dl, 2 B) while a
  // wider return must materialise the constant (mov eax, 37h, 5 B). At int and at WORD it
  // is 60 B. The three callers all store AL, which fits any width, so the callee is the
  // only evidence; int against WORD is not decidable, and int suits an order id.
  int GetCarrierReaverIgnoreOrder();
  // 0x41CC50 (44 B): walks the five build-queue WORDs (+0xA0 down to +0x98)
  // counting Interceptor (0x49) / Scarab (0x55) entries in BL, `mov al, bl` at
  // the exit; its one caller (TTAllowed) reads AL => BYTE.
  BYTE HangarConstructing();

  // External member callees these bodies reach.

  // 0x41C790 (206 B), TU CUnitCarrier.cpp. Nullary; its one caller discards EAX and the
  // body's own exits are inherited from SetOrder / AngleBetween.
  void DispatchTrilobyte();

  // 0x41BFE0 (630 B), TU CUnitCarrier.cpp. A member, although the ledger and the map first
  // spelled it free, and measurement is what settles it: the ABI is ECX plus eight bytes of
  // callee-cleaned stack (ret 8h) read back as two independent dwords, with EDX never read
  // and DispatchCarrier setting ECX only. Under /Gr the only free spellings that give ECX
  // plus two pushes are an 8-byte by-value aggregate or an __int64, and both put the pushes
  // after the call that computes the first, where the oracle hoists `push 0h` to +0x01;
  // DispatchTrilobyte passing {r, 2*r} refutes __int64 on its own.
  BOOL sub_41BFE0(int nWord0, int nWord1);

  // 0x41CCD0 (357 B), TU CUnitCarrier.cpp. Nullary. Returns a CUnit *: both callers take
  // the result 32-bit and immediately dereference it at +0x64 and +0x0C, `this`'s layout.
  CUnit *HangarDeployFighter();

  // src117/lang/CUnitProtoss.cpp -- the psi and power family.

  // 0x42F510 (60 B) / 0x42F7E0 (67 B) / 0x42F5B0 (129 B). Nullary thiscall order
  // dispatchers, all three ending in jmp CUnit::EndUninterruptableDispatchOrder, which is
  // declared void above, and a tail jmp pins the return type.
  void DispatchArbiter();
  void DispatchShieldBatteryInterrupt();
  void DispatchJuiceInit();

  // 0x42F770 (103 B). Nullary; three pop esi / ret exits, and no instruction on any path
  // writes EAX for its own sake. It is the reader that gave +0xE2 a name; see
  // bShieldBatteryState above.
  void DispatchShieldBattery();

  // 0x42F9E0 (129 B). Nullary. One arm tail-jmps EndUninterruptableDispatchOrder and the
  // other falls out of pop esi / ret with EAX inherited from CountDownToCompletion; nothing
  // materialises a value.
  void DispatchSummonComplete();

  // 0x430390 (156 B), ret 4h. One stack dword, read once and only as a truth test, so the
  // callee never sees a width narrower than the slot. All four callers discard EAX, and the
  // only thing in it at the ret is ResetMovementState's inherited value.
  void sub_430390(BOOL bDetach);

  // 0x430430 (145 B). Nullary; the last arm is a tail JMP to
  // CUnit::ResetMovementState (void), the other a bare `ret` => void.
  void sub_430430();

  // 0x42F310 (221 B), ret 4h. The slot is dereferenced at +0x4C, +0xDC and +0xA2 and passed
  // as IsDisabled's ECX, which fixes it as a CUnit *. BOOL, and both ends agree: the callee
  // has setnz and literal exits, and all four callers consume EAX 32-bit.
  BOOL CanRechargeShieldsAt(CUnit *pTarget);

  // CUnitProtoss.cpp order handlers (0x42F550, 0x42F640, 0x42FC40): thiscall,
  // no stack arguments, each ends in a tail jump to EndDispatchOrder.
  void DispatchRecharge();
  void DispatchRechargeUnit();
  void DispatchTeleport();
  // 0x4304D0 (317 B), `ret 4h`: recalls this unit to the caster passed on the
  // stack (do_recall_single hands it the recalling unit).
  void sub_4304D0(CUnit *pCaster);
  // 0x4300E0 (103 B), `ret 4h`: a member the ledger names free. Moves shields
  // from the battery passed on the stack into this unit.
  void sub_4300E0(CUnit *pBattery);

  // 0x42F3F0 (285 B), ret 4h. The slot is dereferenced at +0x112..+0x12F, this unit's own
  // status-effect block, so a CUnit *. Void, not the BYTE the raw EAX evidence suggests:
  // the zx8 is the body's last statement, so EAX holds the final copied byte purely as a
  // side effect of the copy, no exit materialises a value, and both callers discard it.
  void CopyStatus(CUnit *pOther);

  // 0x42F270 (155 B). Nullary. BOOL: three exits are mov eax, 1h and the fourth falls
  // through with CSprite::Create's null result, which ShowPowerTemplates tests.
  BOOL CreatePowerTemplate();

  // 0x430610 (84 B). Nullary; unlinks `this` from the sgJuiceLink_g chain through
  // pPsiLinkPrev/pPsiLinkNext and ends on a bare ret with a link value no caller reads.
  void RemoveFromPsiLink();

  // 0x42F830 (427 B) / 0x42FA70 (451 B). The Archon and Dark Archon merges. Nullary
  // thiscall; every exit leaves EAX inherited from a void callee.
  void DispatchSummonArchon();
  void DispatchSummonDArchon();

  // External member callees these bodies reach.

  // 0x4269B0 (453 B), TU CBullet.cpp. `ret 4h`; the slot is forwarded whole,
  // and all four callers discard EAX (inherited from statport_portrait_hide).
  void RemoveTargetReferences(BOOL bAll);

  // 0x423260 (200 B), TU CUnitGUI.cpp. Nullary; contracts.py says EAX is not
  // defined at the ret and all six callers discard it => void.
  void sub_423260();

  // 0x407B50 (48 B), TU Allowed.cpp. Nullary; `mov eax, 1h` / `xor eax, eax`
  // exits and all twelve callers consume EAX 32-bit => BOOL.
  BOOL IsDisabled();

  // 0x407B80 (38 B), TU Allowed.cpp. Nullary; +0x64 is widened once with
  // `movzx eax, word ptr` and tested against five unit types 32-bit.
  BOOL IsGhostType();

  // src117/lang/CUnitGUI.cpp -- the unit GUI layer.

  // 0x422B00 (15 B). Nullary thiscall ending in a tail jmp to gamesnd_screendist_pan; its
  // one caller consumes EAX 32-bit and a tail jmp forwards the callee's return, so int.
  int GetPan();

  // 0x422B10 (40 B). Nullary thiscall, tail jmp to gamemap_center_on_mtx. Its
  // one caller (statport_portrait_ctrl_fcn) discards EAX => void.
  void CenterView();

  // 0x422C20 (88 B). Nullary thiscall. Two explicit exits, `xor eax, eax` and
  // `mov eax, 1h`, and its one caller (gamesnd_under_attack) consumes EAX
  // 32-bit => BOOL.
  BOOL IsOffScreen();

  // 0x423000 (89 B). Nullary thiscall; EAX at the ret is inherited from a void callee on one
  // path and from the init_juice_matrix tail jmp on the other, and both callers discard it.
  // It tail-jmps into CUnitProtoss.cpp, so a change to init_juice_matrix can move this
  // REL32 -- verify the two together.
  // `noinline` matters and goes on the declaration, not the definition: sub_422B40 is a
  // 22-byte tail-jmp thunk into this function, and without it /Ob2 splices all 89 bytes in
  // and that thunk measures 106 = 22 - 5 + 89. This function's own COMDAT is unchanged.
  __declspec(noinline) void sub_423000();

  // 0x423060 (120 B), `ret 4h`. BYTE because sub_424BD0 and SetPlayer push
  // a byte local's slot whole; the body is byte-identical at either width.
  void sub_423060(BYTE bSelectionIndex);

  // 0x4230E0 (8 B) / 0x4230F0 (8 B). Two-instruction thunks: load pSprite into
  // ECX and tail jmp. Nullary, and every caller discards EAX => void.
  void sub_4230E0();
  void RemoveTeamCircle();

  // External member callees these bodies reach.

  // 0x4268D0 (TU CUnitInit.cpp). Nullary thiscall; CUnit::Hide discards EAX.
  void RemoveFromStatusBar();

  // 0x4273C0 (66 B), TU CUnitInv.cpp. Nullary thiscall -- bare ret, no stack slots, ECX an
  // input and EDX never. BOOL from the body itself: explicit mov eax, 1h and xor eax, eax
  // exits plus a tail jmp to IsFlagInside, and both callers consume EAX 32-bit.
  BOOL HasFlag();


  // src117/lang/CUnitUpgrade.cpp -- research and upgrades.

  // 0x43A8E0 (389 B). Nullary thiscall whose last act on every reached path is the movement
  // refresh, either jmp CUnit::sub_42DFF0 or the three Flingy*WithModifiers stores; a tail
  // jmp pins void. Its dispatch is a two-level jump table (byte index at 0x43AA9C, jump
  // table at 0x43AA68) that lands in the function's own COMDAT under /Gy.
  void ApplyUpgrades();

  // 0x43AB10 (104 B). Nullary thiscall, tail jmp to get_upgrade_level on the fast path.
  // BYTE, and all three callers agree -- Damage movzx ecx, al, statdata_ctx_status mov bl,
  // al, statdraw_draw_armor mov cl, al. The slow path's 32-bit `add eax, ecx` looks like an
  // int return and is not: with a BYTE return the upper half of EAX is dead, so MSVC narrows
  // neither that add nor the call result, and the two widenings it skips are the 7 bytes
  // between int (111 B) and BYTE (104 B, byte-exact).
  BYTE ArmorUpgrades();

  // 0x43AC40 (154 B) / 0x43ACE0 (271 B). The two per-tick order dispatchers, both ending
  // pop <reg> / jmp CUnit::EndDispatchOrder, which is declared void.
  void DispatchResearchTech();
  void DispatchUpgrade();

  // 0x43ADF0 (110 B) / 0x43AE60 (61 B). The two cancel-time refunds. Nullary thiscall; EAX
  // is not defined at the ret and their one caller discards it.
  void RefundResearch();
  void RefundUpgrade();

  // 0x43AEF0 (75 B). Byte-for-byte CUnit::ResetMovementSpeed, declared void above, plus one
  // leading `or dword ptr [esi+0DCh], 10000000h`, and the same tail jmp to sub_42DFF0.
  // `noinline` is on the declaration and it matters: sub_43B0B0 calls it from a single site
  // cheap enough for /Ob2, and with the splice that body is 413 B against the oracle's 262.
  // ApplyUpgrades' eleven call sites are over the growth budget and stay real calls either
  // way. Definition order is not the lever -- moving sub_43B0B0 to either side leaves 413.
  __declspec(noinline) void SetMovementUpgrade();

  // External member callees these bodies reach.

  // 0x42DFF0, TU CUnitPathUtil.cpp. Nullary thiscall, reached only as a tail jmp from the
  // three movement-refresh bodies above, so no caller can inspect EAX.
  void sub_42DFF0();

  // src117/lang/maphdr.cpp reaches these.

  // 0x4B2010, 249 B. thiscall, ECX = `this`, no stack parameters; void by its
  // epilogue -- every exit reaches `pop edi/ebx/esi; mov esp,ebp; pop ebp; ret`.
  void MakeGuardPost();

  // 0x428F70 (245 B). Nullary thiscall. void: no exit sets EAX, and the one
  // caller (maphdr_THG2) discards it.
  void InitDisableDoodad();

  // src117/lang/placebox.cpp reaches these.

  // 0x436E70 (165 B), oracle TU CUnitTerran.cpp. Thiscall, ret 4h -- one stack argument.
  // The ledger and the map disagree on the name (CUnit::DisplayLandingOverlay against
  // CUnit::CreatePlaceboxOverlay); the generated code resolves by the ledger spelling, and
  // the disagreement is recorded rather than reconciled here.
  // The parameter is an int: the slot is read narrow and also written, so the callee reuses
  // it as a scratch point16_t, and what is passed is a zero-extended WORD in a dword slot.
  // Returns int -- all three call sites use the full EAX.
  int DisplayLandingOverlay(int nUnitType);

  // 0x41B5C0 (290 B), nullary. All nine callers discard EAX, and the only immediate at the
  // ret is the body's own 0xE400E4 queue-fill store, so void.
  void RemoveAllBuildQueue();

  // 0x43D990 (103 B), oracle TU CUnitZerg.cpp, declared here because PlaceLarva calls it
  // and no header of src117 did. An undeclared callee is not an error but a silent reach: the
  // generated code's include roots put src117/h first and the quarantined cpp-1.17/h
  // second, so a name src117 never declares resolves out of the quarantine and only the payload
  // build sees it. Nullary thiscall, bare ret; void, since every exit is a byte store to
  // +0x4E with no write to EAX and both callers discard it.
  void sub_43D990();

  // 0x43D360 (1184 B), `ret 4h`. Finds a free tile for a larva around this
  // hatchery and writes it through pOut, a DWORD pair; nonzero on success.
  // Both callers load the hatchery into ECX and push only the out-pointer.
  int sub_43D360(void *pOut);

  // 0x486BF0 (84 B), oracle TU SAI_Build.cpp. Nullary thiscall, bare ret, ECX an input and
  // EDX never. Void: EAX at the ret is only inherited from SetDestination_xy or
  // patrol_pick_area and no caller consumes it. Unlike RemoveAllBuildQueue above, whose
  // tail is an explicit immediate that void would break with C4716.
  void AIUpdateOverlord();

  // 0x41B550 (108 B), oracle TU CUnitBuild.cpp, declared here because AIUpdateSilo and
  // bldg_arm_nuke call it and no header of src117 did. Thiscall, ret Ch -> three stack words,
  // each read once as 4 bytes; four consuming callers take the full EAX and every ret
  // materialises 0 or 1.
  BOOL AIAddBuildQueue(int nArg0, int nArg1, intptr_t nArg2);
  // 0x41BAB0 (146 B), `ret 4h` with ECX live and EDX never read, so a member
  // even though the ledger spells it free. Charges one repair step.
  BOOL sub_41BAB0(CUnit *pTarget);
  // 0x41BDE0 (390 B), `ret 4h` with ECX live. Counts this unit's queued
  // instances of nUnitType; all 17 call sites take the full EAX.
  int CountInBuildQueue(int nUnitType);
  // 0x41B060 (392 B), nullary. RunBGOrders' order 0x6E handler.
  void DispatchTrain();
  // 0x41BC30 (123 B), `ret 8h`. Returns CreateUnitAt's unit or 0.
  CUnit *sub_41BC30(int nUnitType, int nArg1);

  // src117/lang/CUnitBounds.cpp -- this unit's place in the unit finder's position
  // orderings. All thiscall; the stack parameters are on each line.

  // 0x419460 (158 B).
  void RemoveFromFinder();

  // 0x4195A0 (628 B).
  void BeginTracking();

  // 0x419F40 (139 B). One stack argument; the bytes prove only that it is a pointer.
  BOOL OverlapsWith(CUnit *pOther);

  // 0x419FD0 (121 B) / 0x41A190 (121 B). Two stack words each, the tile position. WORD
  // and not int: TrackCollision loads CSprite +0x16 with movzx, where int against a short
  // would give movsx. Void -- EAX at the ret is the mtx_* callee's leftover.
  void PlaceBldg(upos_t wX, upos_t wY);

  void UnplaceBldg(upos_t wX, upos_t wY);

  // Callees of the CUnitZBuild and CUnitZerg bodies, declared here only.

  // 0x41BD70 (102 B), oracle TU CUnitBuild.cpp. One stack argument, the factory: tested
  // for NULL, then IsRallyer on this unit.
  void RallyToFactoryTarget(CUnit *pFactory);

  // 0x424810 (79 B), oracle TU CUnitInit.cpp. Nullary; every ret materialises 0 or 1.
  BOOL IsFacingMoveTarget();

  // 0x42AA40 (21 B), oracle TU CUnitOrder.cpp. The same shape.
  BOOL IsInterruptable();

  // 0x429AA0 (21 B), CUnitOrder.cpp. The inverse of IsInterruptable.
  BOOL IsUninterruptable();

  // 0x42A210 (175 B), CUnitOrder.cpp. One stack argument; the order id reaches
  // COrder::AllocOrder as a byte, so BYTE.
  void SetForegroundOrder(order_id_t bOrderId);

  // Callees of the CUnitBounds.cpp bodies, in the oracle's repulse.cpp.

  // 0x481760 (195 B) / 0x481830 (70 B). Nullary; the two CUnitBounds tail jumps reach them.
  void AddToRepulseChunk();
  void RemoveFromRepulseChunk();

  // src117/lang/CUnitCloak.cpp -- the cloak transitions.

  // 0x41D300 (86 B).
  void sub_41D300();

  // 0x41D410 (65 B).
  void sub_41D410();

  // src117/lang/CUnitCOP.cpp -- the capture-the-flag cop orders.

  // 0x4213C0 (114 B).
  void DispatchCTFCop();

  // 0x421440 (136 B).
  void DispatchCTFCopInit();

  // 0x4214D0 (206 B).
  void DispatchCTFCop2();

  // 0x421600 (24 B). Its one caller takes the full EAX.
  intptr_t GetHeldPowerup();

  // 0x421620 (33 B). Five callers take the full EAX.
  BOOL IsCircleOfPower();

  // src117/lang/CUnitInv.cpp -- the powerup or resource a unit carries.

  // 0x427090 (95 B). Three stack words; the callee reads the last two one byte wide.
  void AddInventory(int nArg0, BYTE bArg1, BYTE bArg2);

  // 0x4270F0 (211 B). One stack argument, a pointer the bytes do not type.
  void AddInventory_CUnit(void *pArg0);

  // 0x4271D0 (51 B).
  void DestroyInventory();

  // 0x427210 (127 B).
  void DispatchPowerupCTF();

  // 0x427290 (44 B).
  void DispatchPowerup();

  // 0x427410 (102 B), nullary thiscall. WORD not `void`: the body materialises
  // an explicit 16-bit value into AX at both exits. noinline, or /Ob2 folds it.
  __declspec(noinline) WORD RemoveInventoryOverlay();

  // 0x427480 (147 B). One stack word the callee never reads, so only its 4-byte footprint
  // is proven.
  void DepositInventory(intptr_t nArg0);

  // src117/lang/CUnitIO.cpp -- the unit's id hash and its save codec.

  // 0x427520 (70 B). One caller takes the full EAX.
  int GetHash();

  // 0x4275E0 (893 B). One stack word, the save file.
  void DecodeFromSave(int nArg0);

  // 0x427970 (794 B). One stack word, the save file.
  void EncodeToSave(int nArg0);

  // src117/lang/CUnitPSpells.cpp -- the protoss spells.

  // 0x4307A0 (128 B). Static: the location arrives in ECX/EDX, read 16 bits wide, and one
  // stack byte follows. A CUnit:: name whose EDX is a live input cannot be a non-static
  // member -- thiscall would put that argument on the stack.
  static void ApplyDisruptorLocation(int wArg0, int wArg1, BYTE bArg2);

  // 0x430820 (84 B). Static for the same reason: x and y in ECX/EDX, one stack word.
  static void ApplyParalyzeLocation(int nArg0, int nArg1, intptr_t nArg2);

  // 0x430880 (67 B). Static, x and y in ECX/EDX, one stack word.
  static void ApplyStasisLocation(int nArg0, int nArg1, intptr_t nArg2);

  // 0x4308D0 (73 B). A callback: its address is taken and it has no direct call site, so
  // the arity is a lower bound and the indirect call site is the authority. Static (EDX is
  // a live input); every ret materialises zero, which a void body would not emit.
  static BOOL ApplyUnitParalyze(void *pArg0, intptr_t nArg1);

  // 0x430920 (50 B). The same callback shape as ApplyUnitParalyze.
  static BOOL ApplyUnitStasis(void *pArg0, intptr_t nArg1);

  // 0x430960 (158 B).
  void sub_430960();

  // 0x430A00 (251 B).
  void sub_430A00();

  // 0x430BD0 (152 B).
  void DispatchHallucinate();

  // 0x430C70 (106 B).
  void DispatchHallucination();

  // 0x430D90 (309 B).
  void DispatchPsyFeedback();

  // 0x430FA0 (136 B). One caller takes the full EAX.
  int sub_430FA0();

  // 0x431090 (92 B). One stack word.
  void UnitApplyStasis(int nArg0);

  // src117/lang/CUnitZBuild.cpp -- the zerg build and morph orders.

  // 0x43B500 (122 B).
  void sub_43B500();

  // 0x43B5E0 (243 B). One caller takes the full EAX.
  intptr_t BirthCopy();

  // 0x43B6E0 (515 B).
  void DispatchBuildSelfZerg();

  // 0x43B900 (397 B).
  void DispatchDroneBuild();

  // 0x43BA90 (43 B).
  void DispatchDroneStartBuild();

  // 0x43BAC0 (396 B).
  void DispatchMorph();

  // 0x43BC50 (242 B). No ret of its own; the tail jmp to EndDispatchOrder pins void.
  void DispatchMorphCanal();

  // 0x43BD50 (301 B).
  void DispatchZergBirth();

  // 0x43BF10 (100 B).
  void DroneBuild();

  // 0x43BF80 (127 B).
  void DroneBuildExtractor();

  // 0x43C000 (49 B). One stack word.
  void DroneBuildStructure(int nArg0);

  // 0x43C090 (151 B). The body touches x87/SSE, so a floating-point return would be
  // invisible to the EAX evidence.
  void SelectBirthedCopy();

  // 0x43C130 (227 B). Static: x and y arrive in ECX/EDX, three stack words follow. A
  // CUnit:: name whose EDX is a live input cannot be a non-static member.
  static void SpreadBldgCreep(int nArg0, int nArg1, int nArg2, int nArg3, int nArg4);

  // 0x43C220 (248 B). Static for the same reason: ECX/EDX plus one stack word, whose
  // slot the callee reuses as scratch.
  static void ZergPlaceBldg(int nArg0, int nArg1, int nArg2);

  // src117/lang/CUnitZerg.cpp -- the zerg unit orders.

  // 0x43C360 (75 B). One stack argument, a pointer the bytes do not type.
  BOOL CanTravelThroughNydus(void *pArg0);

  // 0x43C3B0 (195 B). Two stack words; two callers take the full EAX.
  int ChkLarvaStray(int nArg0, int nArg1);

  // 0x43C5A0 (328 B).
  void DispatchDroneInfesting();

  // 0x43C970 (842 B).
  void DispatchDroneLand();

  // 0x43CCC0 (141 B).
  void DispatchEnterNydus();

  // 0x43CD50 (61 B).
  void DispatchCreepInit();

  // 0x43CD90 (377 B).
  void DispatchIdleLarva();

  // 0x43D1A0 (163 B).
  void DispatchSpreadCreep();

  // 0x43D250 (61 B).
  void DispatchStopSpreadCreep();

  // 0x43D290 (202 B). One stack argument, a pointer the bytes do not type.
  void sub_43D290(void *pArg0);

  // 0x43D800 (241 B). Two stack arguments, pointers the bytes do not type.
  void sub_43D800(void *pArg0, void *pArg1);

  // 0x43D940 (73 B).
  void ReplaceWithBurrowedSprite();

  // 0x43DA30 (279 B). One stack argument, a pointer the bytes do not type.
  void StabilizeHatcheryBounds(void *pArg0);

  // src117/lang/select.cpp

  // oracle 0x49C5A0 (553 B). Places this unit inside the formation select_target is
  // building: seeds pForm->ptUnit from pForm->ptTarget, offsets it by the sprite position
  // under pForm->bFlags, clips it to the map and, for a ground unit, walks it back toward
  // the reference point until the region is one the target's region has a path to. One
  // pointer argument; both callers discard EAX, which the epilogue only inherits.
  void sub_49C5A0(select_form_t *pForm);

};

// The machine check on every offset above. Compile-time only, so it cannot perturb a
// byte-match: replace a slice of an _unmapped_ array with a named field and get the
// arithmetic wrong and the build stops here instead of silently moving pSprite.
// OC_X86_LAYOUT checks the 32-bit layout only -- positions are wider on x64.
OC_X86_LAYOUT(sizeof(CUnit)            == 0x150, "CUnit size    (unit-table stride imul 150h)");
static_assert(offsetof(CUnit, pPrev)    == 0x00, "CUnit::pPrev    master-list link");
OC_X86_LAYOUT(offsetof(CUnit, pNext)    == 0x04, "CUnit::pNext    master-list link");
OC_X86_LAYOUT(offsetof(CUnit, lHitPoints) == 0x08, "CUnit::lHitPoints [ecx+8h]");
OC_X86_LAYOUT(offsetof(CUnit, pSprite) == 0x0C,  "CUnit::pSprite  [ecx+0Ch]");
OC_X86_LAYOUT(offsetof(CUnit, bMoveFlags) == 0x20, "CUnit::bMoveFlags [edi+20h] byte, test imm8");
OC_X86_LAYOUT(offsetof(CUnit, bUnion21) == 0x21, "CUnit::bUnion21 [esi+21h] byte, movzx");
OC_X86_LAYOUT(offsetof(CUnit, bTurnRadius) == 0x22, "CUnit::bTurnRadius [esi+22h] byte store");
OC_X86_LAYOUT(offsetof(CUnit, bDirection) == 0x23, "CUnit::bDirection [edi+23h] byte, movzx");
OC_X86_LAYOUT(offsetof(CUnit, wMoveTargetX) == 0x10, "CUnit::wMoveTargetX [esi+10h]");
OC_X86_LAYOUT(offsetof(CUnit, wMoveTargetY) == 0x12, "CUnit::wMoveTargetY [esi+12h]");
OC_X86_LAYOUT(offsetof(CUnit, wNextWaypointX) == 0x18, "CUnit::wNextWaypointX [esi+18h] -- Stop's dword store pair");
OC_X86_LAYOUT(offsetof(CUnit, wNextWaypointY) == 0x1A, "CUnit::wNextWaypointY [esi+1Ah]");
OC_X86_LAYOUT(offsetof(CUnit, lTopSpeed) == 0x34, "CUnit::lTopSpeed [esi+34h] dword store");
OC_X86_LAYOUT(offsetof(CUnit, wAcceleration) == 0x48, "CUnit::wAcceleration [esi+48h] word store");
OC_X86_LAYOUT(offsetof(CUnit, bPlayer) == 0x4C,  "CUnit::bPlayer  [eax+4Ch]");
OC_X86_LAYOUT(offsetof(CUnit, bOrderId) == 0x4D, "CUnit::bOrderId [ecx+4Dh]");
OC_X86_LAYOUT(offsetof(CUnit, bOrderState) == 0x4E, "CUnit::bOrderState [esi+4Eh]");
OC_X86_LAYOUT(offsetof(CUnit, bOrderFlags) == 0x4F, "CUnit::bOrderFlags [esi+4Fh]");
OC_X86_LAYOUT(offsetof(CUnit, bUnion54) == 0x54, "CUnit::bUnion54 [esi+54h] byte, cmp/store imm8");
OC_X86_LAYOUT(offsetof(CUnit, bUnion57) == 0x57, "CUnit::bUnion57 [esi+57h] byte, DispatchJunkYardDog");
OC_X86_LAYOUT(offsetof(CUnit, wOrderTargetX) == 0x58, "CUnit::wOrderTargetX [esi+58h]");
OC_X86_LAYOUT(offsetof(CUnit, wOrderTargetY) == 0x5A, "CUnit::wOrderTargetY [esi+5Ah]");
OC_X86_LAYOUT(offsetof(CUnit, pOrderTarget) == 0x5C, "CUnit::pOrderTarget [esi+5Ch]");
OC_X86_LAYOUT(offsetof(CUnit, lShields) == 0x60, "CUnit::lShields [edi+60h]");
OC_X86_LAYOUT(offsetof(CUnit, wType)   == 0x64,  "CUnit::wType    [ecx+64h]");
OC_X86_LAYOUT(offsetof(CUnit, pPlayerUnitPrev) == 0x68, "CUnit::pPlayerUnitPrev [esi+68h] -- sub_426530's unlink");
OC_X86_LAYOUT(offsetof(CUnit, pPlayerUnitNext) == 0x6C, "CUnit::pPlayerUnitNext [eax+6Ch]");
OC_X86_LAYOUT(offsetof(CUnit, subunit) == 0x70,  "CUnit::subunit  [ecx+70h]");
OC_X86_LAYOUT(offsetof(CUnit, pUnion74) == 0x74, "CUnit::pUnion74 [esi+74h]");
OC_X86_LAYOUT(offsetof(CUnit, dwUnion7C) == 0x7C, "CUnit::dwUnion7C [esi+7Ch]");
OC_X86_LAYOUT(offsetof(CUnit, pUnion80) == 0x80, "CUnit::pUnion80 [esi+80h] dword, deref'd as CUnit*");
OC_X86_LAYOUT(offsetof(CUnit, bKillCount) == 0x8F, "CUnit::bKillCount [ecx+8Fh] byte, movzx");
OC_X86_LAYOUT(offsetof(CUnit, wBuildQueue) == 0x98, "CUnit::wBuildQueue [esi+eax*2+98h]");
static_assert(sizeof(((CUnit *)0)->wBuildQueue) == 0x0A, "CUnit::wBuildQueue extent (ends at wEnergy)");
OC_X86_LAYOUT(offsetof(CUnit, wEnergy) == 0xA2,  "CUnit::wEnergy  [esi+0A2h]");
OC_X86_LAYOUT(offsetof(CUnit, bBuildQueueSlot) == 0xA4, "CUnit::bBuildQueueSlot [esi+0A4h]");
OC_X86_LAYOUT(offsetof(CUnit, bIdGeneration) == 0xA5, "CUnit::bIdGeneration [esi+0A5h] / [edi-0ABh]");
OC_X86_LAYOUT(offsetof(CUnit, wHPGain)     == 0xA8, "CUnit::wHPGain     [edi+0A8h]");
OC_X86_LAYOUT(offsetof(CUnit, wShieldGain) == 0xAA, "CUnit::wShieldGain [esi+0AAh]");
OC_X86_LAYOUT(offsetof(CUnit, wCargoUnitIds) == 0xB0, "CUnit::wCargoUnitIds  [ecx+0B0h] eight WORD unit ids");
OC_X86_LAYOUT(offsetof(CUnit, pPowerup)     == 0xC0, "CUnit::pPowerup     [esi+0C0h]");
OC_X86_LAYOUT(offsetof(CUnit, pHangarParent) == 0xC0, "CUnit::pHangarParent [edi+0C0h] (union arm)");
OC_X86_LAYOUT(offsetof(CUnit, pAddon)       == 0xC0, "CUnit::pAddon       DetachAddon [esi+0C0h]");
OC_X86_LAYOUT(offsetof(CUnit, pHangarDockedHead) == 0xC0,
              "CUnit::pHangarDockedHead  HangarConstructionFinished mov [edi+0C0h], esi");
OC_X86_LAYOUT(offsetof(CUnit, pUnionC0)     == 0xC0, "CUnit::pUnionC0     neutral CUnit* union arm");
OC_X86_LAYOUT(offsetof(CUnit, pHangarNext)  == 0xC4,
              "CUnit::pHangarNext  DispatchCarrierStop mov esi, dword ptr [esi+0C4h]");
OC_X86_LAYOUT(offsetof(CUnit, pHangarDeployedHead) == 0xC4,
              "CUnit::pHangarDeployedHead  OrderInterceptorsReturn mov esi, dword ptr [ecx+0C4h]");
OC_X86_LAYOUT(offsetof(CUnit, pHangarPrev)  == 0xC8,
              "CUnit::pHangarPrev  HangarConstructionFinished mov dword ptr [eax+0C8h], esi");
OC_X86_LAYOUT(offsetof(CUnit, bTechType)    == 0xC8, "CUnit::bTechType    [ecx+0C8h]");
OC_X86_LAYOUT(offsetof(CUnit, bHangarCount) == 0xC8, "CUnit::bHangarCount [esi+0C8h] (union arm)");
OC_X86_LAYOUT(offsetof(CUnit, bUpgradeType) == 0xC9, "CUnit::bUpgradeType [ecx+0C9h]");
OC_X86_LAYOUT(offsetof(CUnit, bHangarCountBuilding) == 0xC9,
              "CUnit::bHangarCountBuilding [ecx+0C9h] (union arm, CanAttack)");
OC_X86_LAYOUT(offsetof(CUnit, pUnionD0)     == 0xD0, "CUnit::pUnionD0     [ecx+0D0h]");
OC_X86_LAYOUT(offsetof(CUnit, wResourceAmount) == 0xD0, "CUnit::wResourceAmount [esi+0D0h] (union arm)");
OC_X86_LAYOUT(offsetof(CUnit, dwUnionC4_SavedOrderTargetPos) == 0xC4,
              "CUnit::dwUnionC4_SavedOrderTargetPos [esi+0C4h] dword (union arm)");
OC_X86_LAYOUT(offsetof(CUnit, pUnionC8_SavedOrderTarget) == 0xC8,
              "CUnit::pUnionC8_SavedOrderTarget [esi+0C8h] dword (union arm)");
OC_X86_LAYOUT(offsetof(CUnit, wRemainingResearchTime) == 0xC6,
              "CUnit::wRemainingResearchTime [esi+0C6h] word, seeded from the time-cost tables and counted down");
OC_X86_LAYOUT(offsetof(CUnit, bTargetUpgradeLevel)    == 0xCD,
              "CUnit::bTargetUpgradeLevel    [esi+0CDh] byte, level+1 seed compared with cmp al, jnb");
OC_X86_LAYOUT(offsetof(CUnit, bUnionCE)     == 0xCE, "CUnit::bUnionCE     [esi+0CEh] byte, store imm8 0/1");
OC_X86_LAYOUT(offsetof(CUnit, bResourceAnim) == 0xD2, "CUnit::bResourceAnim [ecx+0D2h] byte, cmp/store AL");
OC_X86_LAYOUT(offsetof(CUnit, bUnionD3)     == 0xD3, "CUnit::bUnionD3     [edi+0D3h] byte, dec / cmp 0 / store 1");
OC_X86_LAYOUT(offsetof(CUnit, bUnion90)       == 0x90, "CUnit::bUnion90       [esi+90h] byte store of 8 in CUnit::sub_425A70");
OC_X86_LAYOUT(offsetof(CUnit, bUnion91)       == 0x91, "CUnit::bUnion91       [esi+91h] byte store of 0 in CUnit::Complete");
OC_X86_LAYOUT(offsetof(CUnit, bUnion92)       == 0x92, "CUnit::bUnion92       [esi+92h] byte store of 0 in CUnit::sub_425A70");
OC_X86_LAYOUT(offsetof(CUnit, bSpiderMineCount) == 0xC0, "CUnit::bSpiderMineCount [esi+0C0h] byte store of 3 in CUnit::sub_425710");
OC_X86_LAYOUT(offsetof(CUnit, wPrevUnitType)  == 0x88, "CUnit::wPrevUnitType  [esi+88h] word store of wType in CUnit::sub_426750");
OC_X86_LAYOUT(offsetof(CUnit, wPrevHP)        == 0xAE, "CUnit::wPrevHP        [esi+0AEh] word store in CUnit::sub_426750");
OC_X86_LAYOUT(offsetof(CUnit, wPowerupOriginX)       == 0xD0, "CUnit::wPowerupOriginX       [esi+0D0h] word store in CUnit::Complete");
OC_X86_LAYOUT(offsetof(CUnit, wPowerupOriginY)       == 0xD2, "CUnit::wPowerupOriginY       [esi+0D2h] word store in CUnit::Complete");
OC_X86_LAYOUT(offsetof(CUnit, pUnionD4)     == 0xD4, "CUnit::pUnionD4     [esi+0D4h]");
OC_X86_LAYOUT(offsetof(CUnit, pUnionD4_HarvestPrev) == 0xD4, "CUnit::pUnionD4_HarvestPrev [edx+0D4h] (union arm)");
OC_X86_LAYOUT(offsetof(CUnit, pUnionD8_HarvestNext) == 0xD8, "CUnit::pUnionD8_HarvestNext [edx+0D8h] dword, deref'd as CUnit*");
OC_X86_LAYOUT(offsetof(CUnit, bUnionD9)     == 0xD9, "CUnit::bUnionD9     [esi+0D9h] byte, cmp vs 0");
OC_X86_LAYOUT(offsetof(CUnit, dwFlags) == 0xDC,  "CUnit::dwFlags  [esi+0DCh]");
OC_X86_LAYOUT(offsetof(CUnit, dwFlagsVolatile) == 0xDC,
              "CUnit::dwFlagsVolatile wave-48 codegen alias");
OC_X86_LAYOUT(offsetof(CUnit, bCarryResource) == 0xE0,  "CUnit::bCarryResource [ecx+0E0h]");
OC_X86_LAYOUT(offsetof(CUnit, dwUnionE4)      == 0xE4,  "CUnit::dwUnionE4      [esi+0E4h] test dword vs reg");
OC_X86_LAYOUT(offsetof(CUnit, bBuildingPlaced) == 0xCB, "CUnit::bBuildingPlaced [esi+0CBh] byte, cmp imm8");
OC_X86_LAYOUT(offsetof(CUnit, bPathingFlags)  == 0x105, "CUnit::bPathingFlags  [esi+105h] byte, test imm8");
OC_X86_LAYOUT(offsetof(CUnit, wTimeToLive)    == 0x110, "CUnit::wTimeToLive    [esi+110h] word, dec per frame");
OC_X86_LAYOUT(offsetof(CUnit, wDefMatrixHitPoints) == 0x112, "CUnit::wDefMatrixHitPoints [edi+112h] word");
OC_X86_LAYOUT(offsetof(CUnit, bDefMatrixTimer)== 0x114, "CUnit::bDefMatrixTimer [edi+114h] byte");
OC_X86_LAYOUT(offsetof(CUnit, bStimTimer)     == 0x115, "CUnit::bStimTimer     [ecx+115h]");
OC_X86_LAYOUT(offsetof(CUnit, bEnsnareTimer)  == 0x116, "CUnit::bEnsnareTimer  [ecx+116h]");
OC_X86_LAYOUT(offsetof(CUnit, bLockdownTimer) == 0x117, "CUnit::bLockdownTimer [eax+117h]");
OC_X86_LAYOUT(offsetof(CUnit, bIrradiateTimer)== 0x118, "CUnit::bIrradiateTimer [edi+118h] byte");
OC_X86_LAYOUT(offsetof(CUnit, bStasisTimer)   == 0x119, "CUnit::bStasisTimer   [eax+119h]");
OC_X86_LAYOUT(offsetof(CUnit, bBloodBoilTimer) == 0x11A, "CUnit::bBloodBoilTimer [esi+11Ah] byte");
OC_X86_LAYOUT(offsetof(CUnit, pIrradiateSource) == 0x11C, "CUnit::pIrradiateSource [edi+11Ch] dword");
OC_X86_LAYOUT(offsetof(CUnit, bIrradiatePlayer) == 0x120, "CUnit::bIrradiatePlayer [edi+120h] byte");
OC_X86_LAYOUT(offsetof(CUnit, bParasiteFlags) == 0x121, "CUnit::bParasiteFlags [esi+121h] byte mask");
OC_X86_LAYOUT(offsetof(CUnit, bUnion123)      == 0x123, "CUnit::bUnion123      [ecx+123h] byte, cmp vs 0");
OC_X86_LAYOUT(offsetof(CUnit, bMyopiaPlayers) == 0x123, "CUnit::bMyopiaPlayers [ecx+123h] byte mask");
OC_X86_LAYOUT(offsetof(CUnit, bParalyzeTimer) == 0x124, "CUnit::bParalyzeTimer [eax+124h]");
OC_X86_LAYOUT(offsetof(CUnit, bCrunchyCount)  == 0x126, "CUnit::bCrunchyCount [edi+126h] byte");
OC_X86_LAYOUT(offsetof(CUnit, bCrunchyTimers) == 0x127, "CUnit::bCrunchyTimers [edi+127h] nine bytes");
OC_X86_LAYOUT(offsetof(CUnit, dwUnion134)     == 0x134, "CUnit::dwUnion134     [esi+134h]");

OC_X86_LAYOUT(offsetof(CUnit, wPositionX)     == 0x28,  "CUnit::wPositionX     [esi+28h] word cmp");
OC_X86_LAYOUT(offsetof(CUnit, wPositionY)     == 0x2A,  "CUnit::wPositionY     [esi+2Ah] word cmp");
OC_X86_LAYOUT(offsetof(CUnit, bUnion85)       == 0x85,  "CUnit::bUnion85       [esi+85h] byte, 22 sites");

// pOrderQueueHead shares +0x74 with pUnion74, so asserting both checks that the union's
// arms land on one offset.
OC_X86_LAYOUT(offsetof(CUnit, pOrderQueueHead)   == 0x74,  "CUnit::pOrderQueueHead   [ecx+74h] -- CLink::Unlink ppHead");
OC_X86_LAYOUT(offsetof(CUnit, pOrderQueueTail)   == 0x78,  "CUnit::pOrderQueueTail   [ecx+78h] -- CLink::Unlink ppTail");
OC_X86_LAYOUT(offsetof(CUnit, bQueuedOrderCount) == 0x84,  "CUnit::bQueuedOrderCount [esi+84h] byte, inc/dec + underflow clamp");
OC_X86_LAYOUT(offsetof(CUnit, bUnion93)          == 0x93,  "CUnit::bUnion93          [esi+93h] byte, or/test imm 1");
OC_X86_LAYOUT(offsetof(CUnit, bBackgroundOrderId)== 0xA6,  "CUnit::bBackgroundOrderId[ecx+0A6h] byte, cmp-and-store");
OC_X86_LAYOUT(offsetof(CUnit, bUnionE2)          == 0xE2,  "CUnit::bUnionE2          [ecx+0E2h] byte, cleared with +0xE8/+0xEC");
OC_X86_LAYOUT(offsetof(CUnit, bUnionE3)          == 0xE3,  "CUnit::bUnionE3          [ecx+0E3h] byte -- ApproachMoveTarget_xy tests, Stop sets 0Fh");
OC_X86_LAYOUT(offsetof(CUnit, dwUnionE8)         == 0xE8,  "CUnit::dwUnionE8         [ecx+0E8h] dword, cleared");
OC_X86_LAYOUT(offsetof(CUnit, dwUnionEC)         == 0xEC,  "CUnit::dwUnionEC         [ecx+0ECh] dword, cleared");

OC_X86_LAYOUT(offsetof(CUnit, wRemainingBuildTime) == 0xAC, "CUnit::wRemainingBuildTime [ecx+0ACh] word, movzx + store back");
OC_X86_LAYOUT(offsetof(CUnit, wRallyX)             == 0xF8, "CUnit::wRallyX             [ecx+0F8h] word, movsx at the consumer");
OC_X86_LAYOUT(offsetof(CUnit, wRallyY)             == 0xFA, "CUnit::wRallyY             [ecx+0FAh] word, movsx at the consumer");
OC_X86_LAYOUT(offsetof(CUnit, pRallyUnit)          == 0xFC, "CUnit::pRallyUnit          [ecx+0FCh] dword, deref'd at +0x0C as CUnit*");

OC_X86_LAYOUT(offsetof(CUnit, pPowerTemplate)   == 0xD0, "CUnit::pPowerTemplate   [esi+0D0h] dword, passed as a CSprite `this`");
OC_X86_LAYOUT(offsetof(CUnit, bShieldBatteryState) == 0xE2, "CUnit::bShieldBatteryState [esi+0E2h] byte, movzx + 3-way switch");
OC_X86_LAYOUT(offsetof(CUnit, pPsiLinkPrev)     == 0xF8, "CUnit::pPsiLinkPrev     [esi+0F8h] dword, `mov dword ptr [eax+0F8h], esi`");
OC_X86_LAYOUT(offsetof(CUnit, pPsiLinkNext)     == 0xFC, "CUnit::pPsiLinkNext     [esi+0FCh] dword, the walked link");

OC_X86_LAYOUT(offsetof(CUnit, bPathState)    == 0x97, "CUnit::bPathState    [esi+97h] byte, the path-order state machine");
OC_X86_LAYOUT(offsetof(CUnit, pPath)         == 0x100, "CUnit::pPath         [ecx+100h] dword, tested whole; pointee flagged at +18h");
OC_X86_LAYOUT(offsetof(CUnit, bPathSubState) == 0x104, "CUnit::bPathSubState [esi+104h] byte, jbe-2 unsigned; PathRetryPath counts it to 0x14");
OC_X86_LAYOUT(sizeof(CUnit)                  == 0x150, "CUnit size    (unit-table stride imul 150h)");
OC_X86_LAYOUT(offsetof(CUnit, bCloakCount)     == 0x96, "CUnit::bCloakCount     [ecx+96h] byte, post-increment in sub_4340D0, reset in EntityUpdate");
OC_X86_LAYOUT(offsetof(CUnit, pBurrowLinkPrev) == 0xF0, "CUnit::pBurrowLinkPrev [ecx+0F0h] dword, cleared on prepend, spliced on unlink");
OC_X86_LAYOUT(offsetof(CUnit, pBurrowLinkNext) == 0xF4, "CUnit::pBurrowLinkNext [ecx+0F4h] dword, takes the old head on prepend");

OC_X86_LAYOUT(offsetof(CUnit, bDmgOverlayCount) == 0xA7, "CUnit::bDmgOverlayCount [esi+0A7h] byte, stored from CSprite::GetDamageOverlayCount");
OC_X86_LAYOUT(offsetof(CUnit, wButtonset)       == 0x94, "CUnit::wButtonset       [ecx+94h] word, `mov word ptr [ecx+94h], dx` in SetButtonset");


// The free functions of src117/lang/CUnitMisc.cpp are declared in src117/h/CUnitMisc.h,
// not here: this header is the class.
