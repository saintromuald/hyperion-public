// CFlingy.h -- the movement and destination prefix of a moving game object: its
// 0x4C-byte layout, the setters and the per-frame movement tick.
//
// The class name and its TU come from our own name sources, which put 0x40CBD0 in
// CFlingy.cpp as CFlingy::SetDestination; the layout and the signatures are read
// off the oracle. The header grows a member at a time, each with its address.
#pragma once

#include "platform.h"   // ours: <stddef.h> for offsetof
#include "point16.h"    // ours: the 4-byte signed point -- see the union at +0x10
#include "pos.h"        // Hyperion: spos_t, mpoint_t (stage E)

class CSprite;          // forward only: this header stores both as pointers and
class CUnit;            // dereferences neither, so an include of CUnit.h here
                        // would make every CFlingy user a CUnit user. The `class`
                        // tag matches both definitions, since a `struct` forward
                        // declaration against one is a C4099 tag mismatch.

// CFlingy -- the movement and destination prefix of a moving game object.
//
// No relationship to CUnit is claimed here, for the reason src117/h/CUnit.h
// declares no base class: what the oracle proves is the offsets, not which class
// in Blizzard's hierarchy contributed them. CUnit::DispatchTurretGuard reaches
// this class by handing a CUnit * in as the ECX of a CFlingy method, which says
// the two share a prefix and nothing more, and src117 spells that as a
// reinterpret_cast at each call site rather than a guess in the type system. The
// unnamed remainder is held at its true size, since the offsets are what the
// compiler encodes, and the static_asserts below are the machine check.
class CFlingy {
public:
  // The intrusive list pair, at the offsets CLink's own primitives take a node's
  // links to be; CFlingy::PostLoad turns a saved index back into a pointer with
  // this class's own 0x4C stride, which is what makes them CFlingy * and not
  // CLink *.
  /*0x00*/ CFlingy  *pPrev;
  /*0x04*/ CFlingy  *pNext;

  // The sprite, at the same offset CUnit.h carries its own pSprite at: five bodies
  // in src117/lang/CFlingy.cpp load it and use it straight as the ECX of a CSprite
  // method, which fixes both the width and the pointee.
  /*0x08*/ BYTE      _unmapped_08[0x04];
  /*0x0C*/ CSprite  *pSprite;
  // The waypoint this object is currently flying at -- the pair CBullet's flight
  // handlers test for "arrived". Signed and 2 bytes each: that test is a narrow
  // 16-bit compare against the position pair below, which the compiler emits only
  // for two operands of one signed 16-bit type. src117/h/CUnit.h declares the
  // same two offsets WORD, where no reader has made the sign observable. The
  // union is here because both setters move the pair as one dword.
  union {
    struct {
      /*0x10*/ spos_t wMoveTargetX;  // Hyperion: positions of 4 bytes on x64 (h/pos.h)
      /*0x12*/ spos_t wMoveTargetY;
    };
    /*0x10*/ mpoint_t moveTarget;
  };

  // The unit the move target is tracking, or NULL when the target is a bare
  // coordinate: SetTarget_xy stores 0 and SetTarget_CUnit the incoming pointer,
  // which CFlingy::PostLoad round-trips through CUnit::FromId.
  /*0x14*/ CUnit    *pMoveTargetUnit;

  // The turning point, written only as a whole-dword copy of the pair above, which
  // is what makes it a point and not two shorts. SetTargetTurningPoint is the one
  // body that reads the halves back on their own, and signed, so the scalar names
  // exist for it.
  union {
    struct {
      /*0x18*/ spos_t wNextWaypointX;
      /*0x1A*/ spos_t wNextWaypointY;
    };
    /*0x18*/ mpoint_t nextWaypoint;
  };

  // Where the object is headed. Signed and 2 bytes each: CFlingy::SetDestination
  // reads both back `movsx` into full registers, compares them against its int
  // arguments and stores only when one has changed.
  /*0x1C*/ spos_t wDestinationX;
  /*0x1E*/ spos_t wDestinationY;

  // The movement flags, a byte: every body that touches it does a byte-wide
  // read-modify-write, and ProgressMovement saves and restores the whole byte
  // around its update stages. Bit 0 is "the waypoint changed, recompute the
  // route"; bit 2 is cleared where bit 0 is set. The other bits are unread.
  /*0x20*/ BYTE  bMoveFlags;

  // The facing actually being drawn: BreakTurn passes it straight to
  // CSprite::SetFacing, so it is a direction in the same 0..255 encoding, and
  // `movzx` at both read sites makes it unsigned.
  /*0x21*/ BYTE  bCurrentDirection;

  // bTargetDirection is the facing being turned towards -- BreakTurn and
  // BreakToIdle both open by copying the movement direction into it, which is
  // "stop turning, aim where you are already going" -- and it is the offset
  // src117/h/CUnit.h calls bDirection. bTurnRadius takes
  // CUnit::FlingyTurnRadiusWithModifiers' return, which is flingy.dat's
  // gubFlingyMaxTurn scaled by the unit's upgrades.
  /*0x22*/ BYTE  bTurnRadius;
  /*0x23*/ BYTE  bTargetDirection;

  // The flingy.dat row, unsigned and 2 bytes: CFlingy::BreakingDistance indexes
  // three flingy.dat tables at three different strides with the one value, which
  // is what makes it a row number rather than a byte offset.
  /*0x24*/ WORD  wFlingyID;

  // Which way this object moves, a byte: BreakingDistance gates its table
  // fast path on it being 0 and UpdateSpeed dispatches a switch on it. The
  // enumerators past 0 are not known.
  /*0x26*/ BYTE  _unmapped_26[0x01];
  /*0x27*/ BYTE  bMovementType;

  // The whole-pixel position, signed and 2 bytes each. The sign is the derivation:
  // CBullet::DispatchFly and CFlingy::BindPositionToMap both test the sign bit
  // before clamping, which an unsigned field would make a compile-time false and
  // delete, and BindPositionToMap derives the 8.8 twins with a `movsx` before the
  // shift.
  /*0x28*/ spos_t wPositionX;
  /*0x2A*/ spos_t wPositionY;

  // The same position at 24.8 fixed point, kept in step by every setter: SetXY
  // writes it from the whole-pixel pair with `movsx / shl 8h` and SetPosition256
  // goes the other way with `sar 8h`, so both are signed dwords.
  /*0x2C*/ LONG  lPositionX256;
  /*0x30*/ LONG  lPositionY256;

  // lCurrentSpeed is the scalar speed the velocity pair below is derived from, at
  // the same 24.8 scale: four bodies multiply it by a DIRECTIONS component with a
  // signed `imul` and an arithmetic `sar`. lTopSpeed is its maximum, taking
  // CUnit::FlingyMaxSpeedWithModifiers' return, which is flingy.dat's
  // gxFlingyMaxVel through the unit's upgrades.
  /*0x34*/ LONG  lTopSpeed;
  /*0x38*/ LONG  lCurrentSpeed;

  // The speed being accelerated towards: ForceSpeed is SetSpeed with one extra
  // store to it, BreakToIdle zeroes it to coast to a stop, and BreakingDistance
  // reads it as the speed to stop from.
  /*0x3C*/ LONG  lTargetSpeed;

  // The speed above resolved onto the movement direction, 24.8 again, written only
  // as a pair and only by the DIRECTIONS product: the x component of the table
  // lands in the first slot and the y in the second, which GetVectorComponents
  // states independently through its two `int *` out parameters.
  /*0x40*/ LONG  lVelocityX;
  /*0x44*/ LONG  lVelocityY;

  // The acceleration, an unsigned WORD: BreakingDistance compares it 16-bit
  // against flingy.dat's gxFlingyAccel, which is what identifies the quantity, and
  // uses it as the divisor of the stopping-distance formula.
  /*0x48*/ WORD  wAcceleration;

  // The direction actually being travelled, as against the one drawn (+0x21) or
  // turned towards (+0x23). It is the index into DIRECTIONS in every velocity
  // recomputation, so a 0..255 direction, unsigned.
  /*0x4A*/ BYTE  bMovementDirection;

  // What the turn logic is steering bCurrentDirection towards: NeedsRotation
  // subtracts the two at byte width and reads the difference back sign-extended,
  // and subtracting it from a direction is what makes it one.
  /*0x4B*/ BYTE  bDesiredDirection;

  // 0x40CBD0. The guarded destination setter: it stores the pair only when one of
  // the two coordinates has changed. `ret 8h`, and the arguments are (x, y) --
  // the lower slot is the one compared against +0x1C and stored there. Void: EAX
  // holds only the sign-extended old coordinates, and the return type is part of
  // the mangled symbol a generated thunk has to match.
  void SetDestination(int x, int y);

  // 0x40CE10, declared for CUnit::Stop's call. __thiscall, nullary and void: no
  // caller consumes a result and no exit materialises one.
  void StopMovement();

  // 0x40BF00. How far this object would travel before it could stop, added to the
  // range a moving unit is allowed against a target it is closing on. That
  // reading comes from its one consumer, CUnit::sub_421120, and not from the body,
  // which src117 has not authored. __thiscall, nullary, and the int it returns is
  // signed: the caller shifts it `sar eax, 8h`, so it is 8.8 fixed point.
  int BreakingDistance();

  // 0x40C270. Splits a distance along a direction into its x and y components
  // through the DIRECTIONS table. It takes an object in ECX and never reads it,
  // but every caller sets ECX, so it is a member; `ret 10h` for its four stack
  // arguments.
  void GetVectorComponents(int nDirection, int nDistance, int *pnX, int *pnY);

  // The next three are src117/lang/CBullet.cpp's, reached the way CUnit reaches
  // SetDestination above: a CBullet * goes straight in as the ECX `this` with no
  // adjustment, through a reinterpret_cast at each call site.

  // 0x40CC40. `ret 8h`, void: the guarded store aimed at the move-target pair,
  // then the guarded store aimed at the destination pair. It is SetTarget_xy's
  // twin -- it clears pMoveTargetUnit, copies the pair into nextWaypoint and sets
  // the same flag bits -- and it calls nothing, since the compiler inlines both
  // halves, which makes it a TU-membership dependency (see the note in
  // src117/lang/CFlingy.cpp). The parameters are 32-bit, as SetDestination's are:
  // each is compared against a sign-extended 16-bit field before the low half is
  // stored back, and the arguments are (x, y).
  void SetTargetDest(int x, int y);

  // 0x40CDC0. __thiscall, nullary and void: the body ends in a tail jump to
  // CFlingy::BreakMovement, whose bare `ret` it inherits.
  void StartMovement();

  // 0x40BAE0. __thiscall, nullary and void. It clamps the whole-pixel pair into
  // the map and republishes the 8.8 fixed-point twins from it.
  void BindPositionToMap();

  // The convention below is __thiscall throughout, read off the bodies: `this`
  // arrives in ECX and is never pushed. A `ret N` is quoted where a body cleans
  // stack slots; a bare `ret` means it has no stack parameters.

  // 0x40C260. Releases the flingy.dat handle block at 0x501148. Static: the body
  // overwrites ECX before any read, so there is no `this`.
  static void __fastcall FreeFlingies();

  // 0x40CEB0. Bare `ret`, nullary: hand the sprite back to its idle animation and
  // clear bMoveFlags bit 3.
  void StopAnim();

  // 0x40BA70. Bare `ret`, nullary. It compares the destination pair against the
  // position pair with narrow 16-bit compares, which are available only because
  // both pairs are the same signed 16-bit type, and returns 0 or 1.
  BOOL AtDestination();

  // 0x40BEE0. Bare `ret`, nullary. Abandon the turn in
  // progress: aim at where we are already going (+0x4A -> +0x23), let
  // UpdateRotation settle +0x21, then republish it to the sprite.
  void BreakTurn();

  // 0x40CB60. Bare `ret`, nullary. Recompute the velocity
  // pair from the current speed and movement direction -- the DIRECTIONS
  // product quoted at +0x38, with nothing else touched.
  void SlideForward();

  // 0x40C6F0. Bare `ret`, nullary, and void despite the name: its only lasting
  // effect is to clear bMoveFlags bit 0 when the remaining turn is within +-10,
  // and the value left in AL is its own last comparison's, not a result. A caller
  // that consumes EAX would be reason to revisit the return type.
  void NeedsRotation();

  // 0x40CC00. `ret 8h`, void. The 24.8 setter: it stores the fixed-point pair
  // whole, derives the pixel pair from it with `sar 8h`, and forwards that pair
  // to CSprite::SetPosition. SetXY is the same function driven from the other end.
  void SetPosition256(int x256, int y256);

  // 0x40CD80. `ret 8h`, void.
  // The whole-pixel setter: stores the pair to +0x28/+0x2A, republishes it at
  // 24.8 with `movsx / shl 8h`, and forwards it to CSprite::SetPosition.
  // The slots are 16-bit: callers push 16-bit values, and the callee uses only si and dx.
  void SetXY(spos_t x, spos_t y);  // Hyperion: positions

  // 0x40CB90. `ret 4h`, void.
  // Guarded: if the requested speed already equals +0x38 the body does nothing,
  // otherwise it stores it and recomputes the velocity pair.
  void SetSpeed(int nSpeed);

  // 0x40C220. `ret 4h`, void. SetSpeed with one extra store ahead of the guard,
  // so it sets the target speed too and the acceleration ramp will not undo it.
  void ForceSpeed(int nSpeed);

  // 0x40BCD0. Bare `ret`, nullary, void.
  // One movement tick: stash bMoveFlags in sgbOldMoveFlags, run the four update
  // stages, publish the resulting flags to sgbStagedMoveFlags_g, then restore
  // the saved byte over +0x20. The save/restore is the reason the two globals
  // exist and is what makes the tick observable to callers without mutating the
  // object's own flags.
  void ProgressMovement();

  // 0x40CCA0. `ret 8h`, void. Aim at a bare coordinate: clear
  // pMoveTargetUnit, store the pair to +0x10/+0x12, copy it into nextWaypoint as
  // one dword, and flip bMoveFlags (clear bit 2, set bit 0).
  void SetTarget_xy(int x, int y);

  // 0x40CD30. `ret 8h`, void. SetDestination, inlined rather than called,
  // followed by the same guarded store aimed at nextWaypoint. The two guards are
  // independent, so the waypoint can move when the destination did not, and the
  // `or` that sets bMoveFlags bit 0 belongs to the second guard's store block.
  void SetTargetTurningPoint(int x, int y);

  // 0x40CCE0. `ret 4h`, void. Aim at a unit: the argument's own sprite supplies
  // the coordinates and the pointer itself is stored to pMoveTargetUnit;
  // otherwise SetTarget_xy exactly. The parameter is a CUnit * and not a
  // CFlingy *, since CFlingy::PostLoad round-trips that slot through
  // CUnit::FromId.
  void SetTarget_CUnit(CUnit *pTarget);

  // 0x40BA90. `ret 8h`, void, and only the first slot is ever read: the callee
  // cleans eight bytes, so the second is declared because the stack discipline
  // needs it.
  //
  // Turn bCurrentDirection towards the argument by at most +-0x20 per call, with
  // the +-0x80 wrap handled first (`cmp eax, 80h` on the sign-extended delta).
  void BindFacingDirection(int nDirection, int nUnusedSlot);

  // 0x40CF00. `ret 4h`, void. Project a point one unit out along a spawn
  // direction and, if it differs from the current destination, store it there.
  // The indexing is a stride of 64 into DIRECTIONS, whose element is 8 bytes, so
  // the argument is one of 32 spawn directions on the 256-step facing circle; at
  // 32 entries the largest index is still inside that table. The parameter is a
  // one-byte aggregate, which is what makes CUnit::DispatchBurrow write one byte
  // of its four-byte slot before the push.
  struct spawn_dir_t {
    BYTE bDir;
  };
  void SetSpawnFacing(spawn_dir_t dir);

  // 0x40BBC0. `ret 4h`, void. ProgressMovement with a direction
  // change spliced in front of the update stages: if the argument differs from
  // bMovementDirection, store it and recompute the velocity pair, then run the
  // same four stages -- except UpdateAcceleration, which this one skips.
  //
  // The parameter is a BYTE and that is measured, not styled: spelled `int` the
  // body compiles one byte short of the oracle's 106, because the oracle reads
  // the flags full-width and then overwrites only AL from the slot, which an int
  // parameter cannot produce. Neither a `(BYTE)` cast at each use nor a BYTE
  // local reproduces it -- only the declared parameter type expresses a
  // byte-width slot read -- and `ret 4h` still cleans a full dword slot, since
  // the width here is about how the slot is read, not how much stack it takes.
  void ProgressMovement_dir(BYTE bDirection);

  // 0x40C1B0. `ret 4h`, void. Slam all three facings
  // to the argument in three consecutive byte stores, recompute the velocity pair
  // from the new movement direction, take the destination from the sprite's own
  // position through the usual guarded store, then hand the same dword to
  // CSprite::SetFacing -- the whole slot and not the byte, which is what makes
  // this parameter int where ProgressMovement_dir's above is BYTE. Same encoding,
  // different declared width, and the bytes say which is which.
  void FaceDirection(int nDirection);

  // The two iscript clock turns, 0x40BFD0 and 0x40BF50. Both are
  // FaceDirection with the argument read as a count of eighths rather than an
  // absolute facing: CTurn folds the scale into `lea ebx, [eax+edx*8h]` and CCTurn
  // spells it out as `shl eax, 3h` / `sub ebx, eax`, which is the whole difference
  // between them. `ret 4h`, and the slot is read whole before it is narrowed to a
  // byte for the scale, so the body reads the same for BYTE and int; the one
  // caller, CImage::RunScript, matches only with BYTE.
  void CTurn(BYTE nSteps);
  void CCTurn(BYTE nSteps);

  // The update stages, declared only: no body of ours, so each call resolves
  // through a generated thunk into the oracle. They are __thiscall and nullary,
  // each body opening `mov <reg>, ecx` with nothing read off a frame.
  void UpdateAcceleration();   // oracle 0x40CF50 (196 B)
  void UpdateMovementFlags();  // oracle 0x40D110 (130 B)
  void UpdateNext();           // oracle 0x40D020 (234 B)
  BOOL UpdateRotation();       // oracle 0x40D1A0 (178 B). BOOL, not void: every
                               //   path to a `ret` materialises a constant. Its
                               //   three call sites discard it either way.
  void UpdateSpeed();          // oracle 0x40D260 (485 B)
  BOOL BreakMovement();        // oracle 0x40BDB0 (207 B). BOOL: the body returns
                               //   0 or 1 and CUnit::FinishUnitMovement
                               //   truth-tests it. Unported, callee only.

  void UpdateVelocity();       // oracle 0x40D450 (unported): UpdateAcceleration's
                               //   tail-jump target, and CUnit::PathTerrainSlide
                               //   calls it in its own right.
  void BreakToIdle();          // oracle 0x40BE80 (unported): void nullary, called
                               //   by CUnit::FinishUnitMovementOrStop.

  // 0x40C3F0, unported. __thiscall, `ret 4h` -- one stack
  // argument, dereferenced at the move-target x, so it is a CUnit * and the
  // function reads where the target is going rather than where it is, which is
  // what "intercept" names. The void return is the weaker half of this
  // declaration: no exit materialises a constant, but the one caller in reach
  // discards the result, so nothing positively bounds it. A later caller that
  // reads EAX settles it.
  void InterceptTarget(CUnit *pTarget);

  // 0x40C3A0. Static, bare `ret`: it loads arr\flingy.dat, zeroes the pool's one
  // dummy element and seats the free and used lists on it.
  static void __fastcall InitFlingies();

  // 0x40B9B0. Static __fastcall, `ret 8h`: ECX and EDX are read without being
  // written or dereferenced, so they are the first two parameters and not a
  // `this`, and two more dwords come off the frame. It returns the element taken
  // off the free list, or NULL. The first parameter reads the same as WORD or
  // int in the body, and the one caller, CImage::RunScript, matches only with int.
  static CFlingy * __fastcall AllocFlingy(int wFlingyID, int x, int y,
                                          int nPlayer);

  // 0x40C2B0. __thiscall, `ret 14h` -- five dword stack arguments. The widths come
  // off the slot reads: the flingy row and the two coordinates are read `word
  // ptr`, the player is forwarded unread to CThingy::Init's fourth argument, and
  // the direction's slot is read whole with only AL used, the same shape
  // FaceDirection has. It returns CThingy::Init's success, which both callers
  // truth-test.
  BOOL Init(WORD wFlingyID, spos_t x, spos_t y, int nPlayer, int nDirection);  // Hyperion: positions

  // 0x40BBB0, 0x40C720, 0x40C780. BitSwap is a bare `jmp CThingy::BitSwap`,
  // nullary; the other two are `ret 4h` whose one slot is read as a dword.
  void BitSwap();
  void PostLoad(BOOL bLinked);
  void SaveEncode(BOOL bLinked);
  // 0x40C050. Static: ECX is a flag, encode when nonzero. Bare `ret`.
  static void __fastcall EncodeDecodeListForSave(BOOL bEncode);
  // 0x40C7F0. Static, nullary, bare `ret`.
  static void __fastcall RecreateFlingyList();
  // 0x40C580 / 0x40C9F0. Static; ECX is the save-file handle, passed through
  // to pfread / pfwrite untouched. Both return BOOL.
  static BOOL __fastcall LoadFlingies(void *pFile);
  static BOOL __fastcall SaveFlingies(void *pFile);

  // 0x40C120. Bare `ret`, nullary. Points all of
  // bTargetDirection/bCurrentDirection at a net_rand roll and re-aims the move
  // target one pixel (0x100 at 24.8) along that facing from the SPRITE's own
  // position.
  void FaceAnywhere();

  // 0x40BC30. __thiscall, `ret 8h`. The tick with both a direction and a speed
  // change spliced in front of it -- ProgressMovement_dir with ForceSpeed inlined
  // after the direction block, and the acceleration and speed stages absent. The
  // first parameter is a BYTE for the measured reason ProgressMovement_dir's is
  // (see src117/lang/CFlingy.cpp); the second is a full dword.
  void ProgressMovement_dir_speed(BYTE bDirection, int nSpeed);

  // 0x40BD10. __thiscall, `ret 4h`, void. Add nDelta to lCurrentSpeed, clamp into
  // [0, lTopSpeed], and re-derive the velocity pair through the SetSpeed guard,
  // which the oracle inlines once per arm.
  void AccelerateClamped_g(int nDelta);

  // 0x40C910. Static, bare `ret`. Walks the used list, recycling any element whose
  // sprite has gone and running the whole five-stage tick on the rest.
  static void __fastcall UpdateFlingies();
};

// The machine check on the offsets above. Compile-time only: these emit no code.
//
// The size is 0x4C from three sites: InitFlingies memsets the pool element at
// exactly 0x4C bytes, PostLoad turns an index into a pointer with an `imul eax,
// eax, 4Ch`, and the next named global in .data begins exactly one element on.
// CUnit running to 0x150 is not a counter-argument: a CUnit is a longer object
// that shares this prefix.
OC_X86_LAYOUT(sizeof(CFlingy)                  == 0x4C,
              "sizeof(CFlingy)        InitFlingies memset 4Ch; PostLoad imul 4Ch");
static_assert(offsetof(CFlingy, pPrev)         == 0x00,
              "CFlingy::pPrev         PostLoad: [esi] index -> &base[i]");
OC_X86_LAYOUT(offsetof(CFlingy, pNext)         == 0x04,
              "CFlingy::pNext         PostLoad: [esi+4h] index -> &base[i]");
OC_X86_LAYOUT(offsetof(CFlingy, pSprite)       == 0x0C,
              "CFlingy::pSprite       [esi+0Ch] -> ECX of a CSprite method");
OC_X86_LAYOUT(offsetof(CFlingy, moveTarget)    == 0x10,
              "CFlingy::moveTarget    SetTarget_xy: mov eax, dword ptr [ecx+10h]");
OC_X86_LAYOUT(offsetof(CFlingy, pMoveTargetUnit) == 0x14,
              "CFlingy::pMoveTargetUnit  SetTarget_CUnit: mov [ecx+14h], edx");
OC_X86_LAYOUT(offsetof(CFlingy, nextWaypoint)  == 0x18,
              "CFlingy::nextWaypoint  SetTarget_xy: mov dword ptr [ecx+18h], eax");
// The scalar halves of the same four bytes get their own asserts: the union
// member above does not cover them, and a nameless struct inside a union is
// where a field could silently drift.
OC_X86_LAYOUT(offsetof(CFlingy, wNextWaypointX) == 0x18,
              "CFlingy::wNextWaypointX  SetTargetTurningPoint: movsx eax, word ptr [ecx+18h]");
OC_X86_LAYOUT(offsetof(CFlingy, wNextWaypointY) == 0x1A,
              "CFlingy::wNextWaypointY  SetTargetTurningPoint: movsx eax, word ptr [ecx+1Ah]");
OC_X86_LAYOUT(offsetof(CFlingy, bMoveFlags)    == 0x20,
              "CFlingy::bMoveFlags    or byte ptr [ecx+20h], 1h");
OC_X86_LAYOUT(offsetof(CFlingy, bCurrentDirection) == 0x21,
              "CFlingy::bCurrentDirection  movzx eax, byte ptr [esi+21h] -> SetFacing");
OC_X86_LAYOUT(offsetof(CFlingy, bTurnRadius)   == 0x22,
              "CFlingy::bTurnRadius   [esi+22h] byte store from FlingyTurnRadiusWithModifiers");
OC_X86_LAYOUT(offsetof(CFlingy, bTargetDirection)  == 0x23,
              "CFlingy::bTargetDirection   BreakTurn: mov byte ptr [esi+23h], al");
OC_X86_LAYOUT(offsetof(CFlingy, wFlingyID)     == 0x24,
              "CFlingy::wFlingyID     movzx esi, word ptr [ecx+24h] -> 3 dat tables");
OC_X86_LAYOUT(offsetof(CFlingy, bMovementType) == 0x27,
              "CFlingy::bMovementType cmp byte ptr [ecx+27h], 0h");
OC_X86_LAYOUT(offsetof(CFlingy, lPositionX256) == 0x2C,
              "CFlingy::lPositionX256 SetXY: movsx/shl 8h -> [ecx+2Ch]");
OC_X86_LAYOUT(offsetof(CFlingy, lPositionY256) == 0x30,
              "CFlingy::lPositionY256 SetXY: movsx/shl 8h -> [ecx+30h]");
OC_X86_LAYOUT(offsetof(CFlingy, lTopSpeed)     == 0x34,
              "CFlingy::lTopSpeed     [esi+34h] dword store from FlingyMaxSpeedWithModifiers");
OC_X86_LAYOUT(offsetof(CFlingy, lCurrentSpeed) == 0x38,
              "CFlingy::lCurrentSpeed imul eax, dword ptr [ecx+38h]");
OC_X86_LAYOUT(offsetof(CFlingy, lTargetSpeed)  == 0x3C,
              "CFlingy::lTargetSpeed  ForceSpeed's one extra store [ecx+3Ch]");
OC_X86_LAYOUT(offsetof(CFlingy, lVelocityX)    == 0x40,
              "CFlingy::lVelocityX    DIRECTIONS[.].x * speed >> 8 -> [ecx+40h]");
OC_X86_LAYOUT(offsetof(CFlingy, lVelocityY)    == 0x44,
              "CFlingy::lVelocityY    DIRECTIONS[.].y * speed >> 8 -> [ecx+44h]");
OC_X86_LAYOUT(offsetof(CFlingy, wAcceleration) == 0x48,
              "CFlingy::wAcceleration cmp ax, word ptr [esi*2+510FD0h]");
OC_X86_LAYOUT(offsetof(CFlingy, bMovementDirection) == 0x4A,
              "CFlingy::bMovementDirection  movzx esi, byte ptr [ecx+4Ah]");
OC_X86_LAYOUT(offsetof(CFlingy, bDesiredDirection)  == 0x4B,
              "CFlingy::bDesiredDirection   NeedsRotation: mov dl, byte ptr [ecx+4Bh]");
OC_X86_LAYOUT(offsetof(CFlingy, wMoveTargetX)  == 0x10,
              "CFlingy::wMoveTargetX  [esi+10h] word, narrow cmp");
OC_X86_LAYOUT(offsetof(CFlingy, wMoveTargetY)  == 0x12,
              "CFlingy::wMoveTargetY  [esi+12h] word, narrow cmp");
OC_X86_LAYOUT(offsetof(CFlingy, wDestinationX) == 0x1C,
              "CFlingy::wDestinationX [ecx+1Ch] word, movsx");
OC_X86_LAYOUT(offsetof(CFlingy, wDestinationY) == 0x1E,
              "CFlingy::wDestinationY [ecx+1Eh] word, movsx");
OC_X86_LAYOUT(offsetof(CFlingy, wPositionX)    == 0x28,
              "CFlingy::wPositionX    [esi+28h] word, js/movsx");
OC_X86_LAYOUT(offsetof(CFlingy, wPositionY)    == 0x2A,
              "CFlingy::wPositionY    [esi+2Ah] word, js/movsx");

// 0x40BA10, callee only: the angle from one point to another, used by
// CUnit::IsInAttackAngle. __fastcall with four ints -- the first pair arrives in
// ECX and EDX, `ret 8h` cleans the other two -- returning a full-width int whose
// low byte the caller stores as a direction.
int __fastcall AngleBetween(int x0, int y0, int x1, int y1);

// Two names this TU owns are deliberately not declared here: `get_angle`
// (0x40B990) and `GetVectorComponents` (0x40C270) both live in the CFlingy.cpp
// oracle TU, but src117/h/CUnitCombat.h has declared them since the waves that
// opened their callers, and src117/lang/CFlingy.cpp includes that header for
// them. Two declarations of one name under a single include root are ambiguous
// to the generator, which then refuses to write anything, so grep src117/h for
// the identifier before adding a name, not just for a header basename.

// 0x40B940, defined in CFlingy.cpp. __fastcall with one argument in ECX, a bare
// `ret` and an int return. The argument is a slope scaled by 256 -- AngleBetween
// builds it as (dy << 8) / dx -- and the result is a direction in the same 0..255
// encoding CSprite::SetFacing takes. The body is a 6-step binary search over
// TANGENT_TABLE_g's 64 entries with the sign stripped and reapplied.
int __fastcall vec_atan(int nSlope256);

// 0x40CED0. Clamp a turn. __fastcall with three parameters: two arrive in ECX and
// EDX, the third in the one dword slot `ret 4h` cleans.
//
// The first two are bytes and the third is not. `sub dl, cl` is an 8-bit
// subtraction whose result is then sign-extended, the direction-wrap idiom this
// file uses everywhere: the difference of two 0..255 facings taken modulo 256 and
// read back as a signed -128..127 turn. The limit is compared at full width, so
// it is an int. The return is char, not int: every exit ends `mov al, <reg>` with
// EAX never widened.
char __fastcall TurnDelta(BYTE bFrom, BYTE bTo, int nMaxTurn);
