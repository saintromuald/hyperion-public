// CFlingy.cpp -- the movement core: where a moving object is, where it is going,
// how fast, and which way it is pointed. Oracle TU 0x40B940-0x40D930, bodies in
// ascending oracle address, 1.17's own order.
//
// The thing to know before editing is that there are four directions, each a
// byte in the same 0..255 encoding and routinely a few steps from the others:
// bCurrentDirection (+0x21) is what the sprite is drawn facing, bTargetDirection
// (+0x23) what the turn logic is turning towards, bMovementDirection (+0x4A)
// which way the object is actually travelling and the DIRECTIONS index the
// velocity pair comes from, and bDesiredDirection (+0x4B) where the movement
// wants to go next. There are likewise three positions, kept in step by hand:
// the whole-pixel pair at +0x28, the same point at 24.8 at +0x2C, and a third
// copy in the CSprite, which every position setter republishes before it
// returns. SetTargetDest and SetTargetTurningPoint also depend on callees in
// this TU being inlined, so moving a body in or out is a change to re-prove.
#include "oc_data.h"  // Hyperion: OC_VA
#include "../port/data/port_formats.h"  // Hyperion
#include "../gen/save117.h"
#include "../h/platform.h"
#include "../h/CFlingy.h"
#include "../h/typelimits.h"  // Hyperion: FlingyTypeValid
#include "../h/CSprite.h"       // ours: SetPosition, SetFacing, SwitchToIdle,
                                //       and the free fn gamedata_free_data
#include "../h/CUnit.h"         // ours: CFlingy::SetTarget_CUnit DEREFERENCES its
                                // argument (pTarget->pSprite), and CFlingy.h only
                                // forward-declares CUnit -- deliberately, so that
                                // holding a CFlingy does not make every user a
                                // CUnit user. The one TU that needs the definition
                                // includes it.
#include "../h/CUnitCombat.h"   // ours: get_angle + GetVectorComponents, which
                                // live in this oracle TU but are declared there
                                // for their CUnit callers; see the note at the
                                // foot of src117/h/CFlingy.h.
#include "../h/CLink.h"        // ours: CLink::Unlink / PrependThis -- the free/used lists
#include "../h/CThingy.h"      // ours: CThingy::Init + CThingy::SpriteUpdate, reached through
                               // the same CFlingy*-as-CThingy* prefix cast the CFlingy.h
                               // banner prescribes for CBullet
#include "../h/CTarget.h"      // ours: veclen (0x405CF0)
#include "../h/TrigAct.h"      // ours: net_rand (0x478200). src117/lang/CSprite.cpp
                               // includes this header for the same name; a
                               // prototype here would be a second declaration of
                               // a name a header of ours already owns.
#include <string.h>            // memset -- CFlingy::InitFlingies zeroes the pool element
#include "../h/saveload.h"      // pfread / pfwrite / compress / decompress
#include "../h/Storm.h"
#include "../gen/globals117.h"
#include "../h/gamedata.h"  // Hyperion: array_data_t

// 0x4442B0, ported in src117/lang/gamedata.cpp and declared here the way
// src117/lang/CSprite.cpp declares it. No convention token on purpose:
// src117/h/snd.h already declares this name, and a redundant __fastcall would
// make the generator call it ambiguous.
void load_array_data(const char *pszFile, void *pArrayData);

// The globals this TU reaches, each spelled once for the standalone build and
// once for the payload, with one guard per name.
#ifdef OC_DLL_MODE
  #ifndef DIRECTIONS
  #define DIRECTIONS            (reinterpret_cast<LONG *>(OC_VA(0x5026B0u)))
  #endif
  #ifndef sgbOldMoveFlags
  #define sgbOldMoveFlags       (*reinterpret_cast<BYTE *>(OC_VA(0x5432FAu)))
  #endif
  #ifndef sgbStagedMoveFlags_g
  #define sgbStagedMoveFlags_g  (*reinterpret_cast<BYTE *>(OC_VA(0x5432FBu)))
  #endif
  #ifndef gFLINGYArrayDataTbl_g
  #define gFLINGYArrayDataTbl_g (reinterpret_cast<array_data_t*>(OC_VA(0x501148u)))
  #endif
  #ifndef gxFlingyMaxVel
  #define gxFlingyMaxVel        (reinterpret_cast<int *>(OC_VA(0x510C88u)))
  #endif
  #ifndef gxFlingyAccel
  #define gxFlingyAccel         (reinterpret_cast<WORD *>(OC_VA(0x510FD0u)))
  #endif
  #ifndef gxFlingySlow
  #define gxFlingySlow          (reinterpret_cast<DWORD *>(OC_VA(0x511178u)))
  #endif
  #ifndef sgbMoving_g
  #define sgbMoving_g           (*reinterpret_cast<BYTE *>(OC_VA(0x5432F8u)))
  #endif
  #ifndef sgbStopping_g
  #define sgbStopping_g         (*reinterpret_cast<BYTE *>(OC_VA(0x5432F9u)))
  #endif
  #ifndef sgpFreeHead_g
  #define sgpFreeHead_g         (*reinterpret_cast<CFlingy **>(OC_VA(0x54329Cu)))
  #endif
  #ifndef sgpFreeTail_g
  #define sgpFreeTail_g         (*reinterpret_cast<CFlingy **>(OC_VA(0x5432ECu)))
  #endif
  #ifndef sgpUsedHead_g
  #define sgpUsedHead_g         (*reinterpret_cast<CFlingy **>(OC_VA(0x5432F0u)))
  #endif
  #ifndef sgpUsedTail_g
  #define sgpUsedTail_g         (*reinterpret_cast<CFlingy **>(OC_VA(0x5432F4u)))
  #endif
  #ifndef sgwStagedX_g
  #define sgwStagedX_g          (*reinterpret_cast<WORD *>(OC_VA(0x543280u)))
  #endif
  #ifndef sgwStagedY_g
  #define sgwStagedY_g          (*reinterpret_cast<WORD *>(OC_VA(0x543284u)))
  #endif
  #ifndef sgwOldX_g
  #define sgwOldX_g             (*reinterpret_cast<WORD *>(OC_VA(0x543288u)))
  #endif
  #ifndef sgwOldY_g
  #define sgwOldY_g             (*reinterpret_cast<WORD *>(OC_VA(0x54328Cu)))
  #endif
  #ifndef sgnStagedDX_g
  #define sgnStagedDX_g         (*reinterpret_cast<LONG *>(OC_VA(0x543290u)))
  #endif
  #ifndef sgnStagedDY_g
  #define sgnStagedDY_g         (*reinterpret_cast<LONG *>(OC_VA(0x543294u)))
  #endif
  #ifndef sgnStagedSpeed_g
  #define sgnStagedSpeed_g      (*reinterpret_cast<LONG *>(OC_VA(0x543298u)))
  #endif
  #ifndef gwMapPixWidth
  #define gwMapPixWidth         (*reinterpret_cast<WORD *>(OC_VA(0x543230u)))
  #endif
  #ifndef gwMapPixHeight
  #define gwMapPixHeight        (*reinterpret_cast<WORD *>(OC_VA(0x543234u)))
  #endif
  #ifndef gubFlingyMaxTurn
  #define gubFlingyMaxTurn      (reinterpret_cast<BYTE *>(OC_VA(0x5114C0u)))
  #endif
  #ifndef gubFlingyMoveType
  #define gubFlingyMoveType     (reinterpret_cast<BYTE *>(OC_VA(0x511670u)))
  #endif
  #ifndef guwFlingySprite
  #define guwFlingySprite       (reinterpret_cast<WORD *>(OC_VA(0x510AE0u)))
  #endif
  #ifndef TANGENT_TABLE_g
  #define TANGENT_TABLE_g       (reinterpret_cast<DWORD *>(OC_VA(0x502EB0u)))
  #endif
#else
// DIRECTIONS is {long x, long y}[256] and is declared flat because gen.py's
// parse_ctype takes no struct types. Index it as [dir*2] / [dir*2+1]: that
// lowers to the oracle's [reg*8+5026B0h] / [reg*8+5026B4h] exactly.
extern LONG  DIRECTIONS[512];
extern BYTE  sgbOldMoveFlags;
extern BYTE  sgbStagedMoveFlags_g;
extern array_data_t  gFLINGYArrayDataTbl_g[8] OC_UNALIGNED;   // seven entries and the NULL after them
// 209 rather than 210, and int rather than DWORD: src117/lang/CUnitMisc.cpp
// declares both this way, and a [210] registration breaks that TU with C2369.
// 209 is the flingy count; 210 was a storage bound.
extern int   gxFlingyMaxVel[209] OC_UNALIGNED;
extern WORD  gxFlingyAccel[209] OC_UNALIGNED;
extern DWORD gxFlingySlow[209] OC_UNALIGNED;
extern BYTE  sgbMoving_g;              // 0x5432F8
extern BYTE  sgbStopping_g;            // 0x5432F9
extern CFlingy *sgpFreeHead_g;         // 0x54329C
extern CFlingy *sgpFreeTail_g;         // 0x5432EC
extern CFlingy *sgpUsedHead_g;         // 0x5432F0
extern CFlingy *sgpUsedTail_g;         // 0x5432F4
extern upos_t sgwStagedX_g;             // 0x543280
extern upos_t sgwStagedY_g;             // 0x543284
extern upos_t sgwOldX_g;                // 0x543288
extern upos_t sgwOldY_g;                // 0x54328C
extern LONG  sgnStagedDX_g;            // 0x543290
extern LONG  sgnStagedDY_g;            // 0x543294
extern LONG  sgnStagedSpeed_g;         // 0x543298
extern upos_t gwMapPixWidth;            // 0x543230
extern upos_t gwMapPixHeight;           // 0x543234
extern BYTE  gubFlingyMaxTurn[209];    // 0x5114C0
extern BYTE  gubFlingyMoveType[209] OC_UNALIGNED;   // 0x511670
extern WORD  guwFlingySprite[209] OC_UNALIGNED;     // 0x510AE0
extern DWORD TANGENT_TABLE_g[64];      // 0x502EB0; 0x100 B to sgszRegSig1
#endif
#ifdef OC_DLL_MODE
  #ifndef sgDummyFlingy_g
  #define sgDummyFlingy_g       (*reinterpret_cast<CFlingy *>(OC_VA(0x5432A0u)))
  #endif
#else
extern CFlingy sgDummyFlingy_g;        // 0x5432A0; memset 0x4C == sizeof(CFlingy)
#endif

// ---------------------------------------------------------------------------
// the list and the pool element's lifetime
// ---------------------------------------------------------------------------

// 0x40C260. Static: the body overwrites ECX before using it, so there is no
// `this` to declare.
void CFlingy::FreeFlingies() {
  gamedata_free_data(gFLINGYArrayDataTbl_g);
}

// ===========================================================================
// position
// ===========================================================================

// 0x40CD80. The store order is not the one anyone would write: the oracle
// interleaves them, computing x at 24.8 and storing it, computing y, storing the
// pixel pair, and only then storing y at 24.8. So the pixel pair goes between
// the two fixed-point stores, and writing those two together costs 2 bytes.
void CFlingy::SetXY(spos_t x, spos_t y) {
  lPositionX256 = PosS(x) << 8;
  wPositionX = PosS(x);
  wPositionY = PosS(y);
  lPositionY256 = PosS(y) << 8;
  pSprite->SetPosition(x, y);
}

// 0x40CC00. SetXY driven from the other end: it takes the 24.8 pair and derives
// the pixel pair with `sar 8h`. It does not share SetXY's interleave -- here the
// pixel pair goes first and the two fixed-point stores are adjacent. Two sibling
// setters, opposite store orders.
void CFlingy::SetPosition256(int x256, int y256) {
  wPositionX = PosS(x256 >> 8);
  wPositionY = PosS(y256 >> 8);
  lPositionX256 = x256;
  lPositionY256 = y256;
  pSprite->SetPosition(x256 >> 8, y256 >> 8);
}

// ---------------------------------------------------------------------------
// speed and velocity
// ---------------------------------------------------------------------------

// 0x40CB60. Re-resolve the current speed onto the current movement direction.
// The index is bMovementDirection (+0x4A), never bCurrentDirection (+0x21) --
// see this file's header.
void CFlingy::SlideForward() {
  unsigned int nDir = bMovementDirection;
  lVelocityX = (DIRECTIONS[nDir * 2]     * lCurrentSpeed) >> 8;
  lVelocityY = (DIRECTIONS[nDir * 2 + 1] * lCurrentSpeed) >> 8;
}

// 0x40CB90. Guarded: if the requested speed already equals lCurrentSpeed the body
// does nothing. Two shapes here survive into the encoding and neither is obvious:
// the guard's operand order (the field on the left, not the parameter), and the
// multiplicand being the parameter rather than the field just stored to -- the
// same value in a different register.
void CFlingy::SetSpeed(int nSpeed) {
  if (lCurrentSpeed != nSpeed) {
    unsigned int nDir = bMovementDirection;
    lCurrentSpeed = nSpeed;
    lVelocityX = (DIRECTIONS[nDir * 2]     * nSpeed) >> 8;
    lVelocityY = (DIRECTIONS[nDir * 2 + 1] * nSpeed) >> 8;
  }
}

// 0x40C220. SetSpeed plus one store, outside and ahead of the guard: set the
// target speed too, so the acceleration ramp will not undo it.
void CFlingy::ForceSpeed(int nSpeed) {
  lTargetSpeed = nSpeed;
  if (lCurrentSpeed != nSpeed) {
    unsigned int nDir = bMovementDirection;
    lCurrentSpeed = nSpeed;
    lVelocityX = (DIRECTIONS[nDir * 2]     * nSpeed) >> 8;
    lVelocityY = (DIRECTIONS[nDir * 2 + 1] * nSpeed) >> 8;
  }
}

// 0x40BF00. How far this object would travel before it could stop: speed^2 /
// (2*accel), the textbook stopping distance, with a fast path that returns a
// precomputed value when the flingy is at both its stock max velocity and its
// stock acceleration.
int CFlingy::BreakingDistance() {
  // The local pins lTargetSpeed in a register before the baked literals below
  // create the pressure; read from the member, the payload build picks others.
  LONG lSpeed = lTargetSpeed;
  if (lSpeed != 0 && bMovementType == 0) {
    if (lSpeed == (LONG)gxFlingyMaxVel[wFlingyID]
        && wAcceleration == gxFlingyAccel[wFlingyID])
      return gxFlingySlow[wFlingyID];
    return (lSpeed * lSpeed) / (2 * wAcceleration);
  }
  return 0;
}

// 0x40C270. The same arithmetic as SlideForward against two out-parameters
// instead of the object: a member whose `this` is never read, since the callers
// pass the object in ECX but the body writes only through the parameters.
void CFlingy::GetVectorComponents(int nDirection, int nDistance,
                                  int *pnX, int *pnY) {
  *pnX = (DIRECTIONS[nDirection * 2]     * nDistance) >> 8;
  *pnY = (DIRECTIONS[nDirection * 2 + 1] * nDistance) >> 8;
}

// ---------------------------------------------------------------------------
// the destination and arriving at it
// ---------------------------------------------------------------------------

// 0x40CBD0. The guarded 2-coordinate store this file repeats five times:
// read both slots sign-extended, compare against the int arguments, and write
// only if either moved.
void CFlingy::SetDestination(int x, int y) {
  if (x != wDestinationX || y != wDestinationY) {
    wDestinationX = (short)x;
    wDestinationY = (short)y;
  }
}

// 0x40BA70, 29 B. Narrow 16-bit compares (`mov ax, word ptr [ecx+1Ch] / cmp ax,
// word ptr [ecx+28h]`), which MSVC will only emit when both operands are the
// same 16-bit type -- so this body is a second, independent statement that the
// destination pair and the position pair share a signedness.
BOOL CFlingy::AtDestination() {
  return wDestinationX == wPositionX && wDestinationY == wPositionY;
}

// 0x40CF00. Project a point out along a spawn direction and store it as the
// destination if it moved. DIRECTIONS is indexed at a stride of 64 bytes, every
// eighth entry of the 256-step circle, so nSpawnDir runs 0..31 and the largest
// offset stays inside the table; a full facing would run 14 KB past its end.
// That the 0x1000 is 16 pixels at 24.8 is a reading -- the bytes only prove the
// shift. The BYTE index local and the two coordinate expressions written inside
// the call, so they are evaluated right to left, are what pick the oracle's own
// registers.
void CFlingy::SetSpawnFacing(spawn_dir_t dir) {
  BYTE bDir = dir.bDir;
  SetDestination((((DIRECTIONS[bDir * 16]     * 0x1000) >> 8) + lPositionX256) >> 8,
                 (((DIRECTIONS[bDir * 16 + 1] * 0x1000) >> 8) + lPositionY256) >> 8);
}

// ---------------------------------------------------------------------------
// the move target: the +0x10 pair, not the destination pair
// ---------------------------------------------------------------------------

// 0x40CCA0. Aim at a bare coordinate. `(bMoveFlags & ~4) | 1` as one
// assignment keeps the flag byte in AL across the coordinate stores; two
// statements do not. It follows `pMoveTargetUnit = NULL` because Init inlines
// this with the flag folded, and only that order stores it after +0x14.
// `nextWaypoint = moveTarget` copies the +0x10 pair to +0x18 with one dword
// move, which two `short` assignments cannot express.
void CFlingy::SetTarget_xy(int x, int y) {
  if (x != wMoveTargetX || y != wMoveTargetY) {
    wMoveTargetX = (short)x;
    wMoveTargetY = (short)y;
    pMoveTargetUnit = NULL;
    bMoveFlags = (bMoveFlags & ~4) | 1;
    nextWaypoint = moveTarget;
  }
}

// 0x40CCE0. SetTarget_xy against a unit: the coordinates come from the target's
// own sprite and the pointer is kept, so a third term joins the guard --
// re-aiming at the same coordinates but a different unit counts as a change.
//
// The `(short)` on wPosX is not symmetry. src117/h/CSprite.h declares wPosX WORD
// and wPosY short, a real asymmetry in that header, so the oracle's `movsx` on
// the x half only comes out if it is cast at the use site; without the cast the
// compiler emits `movzx`. The y half needs no cast, its declared type carrying
// the sign already.
void CFlingy::SetTarget_CUnit(CUnit *pTarget) {
  int x = PosS(pTarget->pSprite->wPosX);
  int y = pTarget->pSprite->wPosY;
  if (x != wMoveTargetX || y != wMoveTargetY || pTarget != pMoveTargetUnit) {
    bMoveFlags = (bMoveFlags & ~4) | 1;
    wMoveTargetX = (short)x;
    wMoveTargetY = (short)y;
    pMoveTargetUnit = pTarget;
    nextWaypoint = moveTarget;
  }
}

// 0x40CC40. Both halves are inlined -- the oracle emits no call. Moving either
// callee out of this TU changes this body; see the file's header.
void CFlingy::SetTargetDest(int x, int y) {
  SetTarget_xy(x, y);
  SetDestination(x, y);
}

// 0x40CD30. SetDestination inlined, then the same guarded store aimed at
// nextWaypoint -- read back through the scalar names here, since this is the one
// body that compares those halves individually and signed. `bMoveFlags |= 1`
// sits inside the second guard, not between the two.
void CFlingy::SetTargetTurningPoint(int x, int y) {
  SetDestination(x, y);
  if (x != wNextWaypointX || y != wNextWaypointY) {
    bMoveFlags |= 1;
    wNextWaypointX = (short)x;
    wNextWaypointY = (short)y;
  }
}

// ---------------------------------------------------------------------------
// the movement tick
// ---------------------------------------------------------------------------

// 0x40BCD0. Save the flags byte, run the four update stages, publish what they
// left, then put the saved byte back. The restore is the point of the two
// globals: the stages mutate the flags freely and the object comes out unchanged.
void CFlingy::ProgressMovement() {
  sgbOldMoveFlags = bMoveFlags;
  UpdateAcceleration();
  UpdateSpeed();
  UpdateMovementFlags();
  UpdateNext();
  sgbStagedMoveFlags_g = bMoveFlags;
  bMoveFlags = sgbOldMoveFlags;
}

// 0x40CDC0. ProgressMovement plus one trailing BreakMovement in tail position,
// which the compiler turns into the oracle's `pop esi / jmp`.
void CFlingy::StartMovement() {
  sgbOldMoveFlags = bMoveFlags;
  UpdateAcceleration();
  UpdateSpeed();
  UpdateMovementFlags();
  UpdateNext();
  sgbStagedMoveFlags_g = bMoveFlags;
  bMoveFlags = sgbOldMoveFlags;
  BreakMovement();
}

// 0x40BBC0. The tick with a direction change spliced in front, and the
// acceleration stage genuinely absent from this variant, which is the one
// structural difference from ProgressMovement above.
//
// The BYTE parameter is measured, and the byte it fixes is not where you would
// look for it. At `int` this body was 105 against the oracle's 106: the oracle
// reads the flags full-width and then overwrites only AL from the parameter slot,
// and an int parameter makes the compiler materialise the whole slot, so --
// knowing EAX is about to die -- it downgrades the flags read three instructions
// earlier to a 3-byte form. Neither a `(BYTE)` cast at each use nor a BYTE local
// reproduces it: only the declared parameter type expresses a byte-width slot
// read.
void CFlingy::ProgressMovement_dir(BYTE bDirection) {
  sgbOldMoveFlags = bMoveFlags;
  if (bMovementDirection != bDirection) {
    unsigned int nDir = bDirection;
    bMovementDirection = bDirection;
    lVelocityX = (DIRECTIONS[nDir * 2]     * lCurrentSpeed) >> 8;
    lVelocityY = (DIRECTIONS[nDir * 2 + 1] * lCurrentSpeed) >> 8;
  }
  UpdateSpeed();
  UpdateMovementFlags();
  UpdateNext();
  sgbStagedMoveFlags_g = bMoveFlags;
  bMoveFlags = sgbOldMoveFlags;
}

// ---------------------------------------------------------------------------
// rotation: the turn clamp and its three users
// ---------------------------------------------------------------------------

// 0x40CED0. The signed byte difference of two facings, clamped to
// +-nMaxTurn. The negative clamp goes through the char local so it falls into
// the shared `mov al, cl`; an early return there, or a third test spelled
// `> -nMaxTurn`, moves nMaxTurn out of ECX.
char __fastcall TurnDelta(BYTE bFrom, BYTE bTo, int nMaxTurn) {
  char cDelta = (char)(bTo - bFrom);
  char cMax = (char)nMaxTurn;
  if (cDelta >= nMaxTurn) {
    return cMax;
  }
  if (cDelta <= -nMaxTurn) {
    cMax = -cMax;
  } else {
    return cDelta;
  }
  return cMax;
}

// 0x40BEE0. Abandon the turn in progress: aim at where we are
// already going, let UpdateRotation settle the drawn facing, then republish it
// to the sprite. `this` survives in ECX across UpdateRotation, so there is no
// reload.
void CFlingy::BreakTurn() {
  bTargetDirection = bMovementDirection;
  UpdateRotation();
  pSprite->SetFacing(bCurrentDirection);
}

// 0x40C6F0. Void: nothing normalises AL, and the value left there is the range
// test's own operand. The only lasting effect is clearing bMoveFlags bit 0 when
// the remaining turn is within +-10.
//
// Two source facts hold this body up and they pull in opposite directions.
// `char >= 128` is folded away by the front end, so keeping the delta in a char
// for TurnDelta's first test deletes the whole clamp; widening to an int local
// keeps it. But the second test must go back to the char -- testing the int emits
// a dword compare where the oracle has a byte one, and the byte-width compare is
// what keeps the raw delta alive in DL. The range fold is hand-written for a
// related reason: written as two comparisons against the clamp's constant arms,
// the compiler proves both fail and jumps the clamped paths straight to the
// `ret`.
void CFlingy::NeedsRotation() {
  char cTurn = TurnDelta(bCurrentDirection, bDesiredDirection, 0x80);
  if ((BYTE)(cTurn + 10) <= 20)
    bMoveFlags &= ~1;
}

// 0x40BA90. Bind bCurrentDirection to within +-0x20 of nDirection -- not a
// per-call turn limit, which is what this file said before the arms were worked
// out. When the delta is within 0x20 it stores nothing; outside it, the stored
// value is always nDirection -+ 0x20, a jump of up to 0x60 in one call. The
// oracle spells it as the current direction plus 0x20 plus the clamped turn
// rather than nDirection - 0x20, which is what makes the arithmetic read like a
// turn step, the algebra collapsing because the turn is itself the difference of
// the two.
//
// Reading the member in each arm is deliberate: caching it in a local merges two
// of the three store tails and comes out 2 bytes short, where letting the
// compiler do the work duplicates all three.
void CFlingy::BindFacingDirection(int nDirection, int nUnusedSlot) {
  char cTurn = TurnDelta(bCurrentDirection, (BYTE)nDirection, 0x80);
  if (cTurn < -0x20)
    bCurrentDirection = (BYTE)(bCurrentDirection + 0x20 + cTurn);
  else if (cTurn > 0x20)
    bCurrentDirection = (BYTE)(bCurrentDirection - 0x20 + cTurn);
}

// 0x40C1B0. Slam all three facings to the argument, recompute the velocity pair,
// take the destination from the sprite's own position, and hand the same dword to
// CSprite::SetFacing -- the whole slot and not the byte, which is what makes this
// parameter int where ProgressMovement_dir's is BYTE.
//
// The `(short)` on wPosX matters for the same reason as in SetTarget_CUnit.
void CFlingy::FaceDirection(int nDirection) {
  BYTE bDir = (BYTE)nDirection;
  bTargetDirection = bDir;
  bCurrentDirection = bDir;
  bMovementDirection = bDir;
  lVelocityX = (DIRECTIONS[bDir * 2]     * lCurrentSpeed) >> 8;
  lVelocityY = (DIRECTIONS[bDir * 2 + 1] * lCurrentSpeed) >> 8;
  CSprite *pSpr = pSprite;
  SetDestination(PosS(pSpr->wPosX), pSpr->wPosY);
  pSpr->SetFacing(nDirection);
}

// 0x40BFD0 and 0x40BF50. The iscript turn pair, clockwise and
// counter-clockwise by N eighths: FaceDirection above with the facing computed
// relative to the current one instead of taken absolute. Three shapes are read
// off the two streams: the scale is applied to the argument's low byte while the
// sum stays a full int, which is what SetFacing gets; the three facing stores
// take that sum's low byte and the DIRECTIONS index is the byte re-widened; and
// SetDestination is inlined here where FaceDirection emits a real call, which is
// the compiler's inline-budget decision over one visible definition rather than
// two different spellings.
void CFlingy::CTurn(BYTE nSteps) {
  int nDir = bCurrentDirection + (BYTE)nSteps * 8;
  BYTE bDir = (BYTE)nDir;
  bTargetDirection = bDir;
  bCurrentDirection = bDir;
  bMovementDirection = bDir;
  lVelocityX = (DIRECTIONS[bDir * 2]     * lCurrentSpeed) >> 8;
  lVelocityY = (DIRECTIONS[bDir * 2 + 1] * lCurrentSpeed) >> 8;
  CSprite *pSpr = pSprite;
  SetDestination(PosS(pSpr->wPosX), pSpr->wPosY);
  pSpr->SetFacing(nDir);
}

void CFlingy::CCTurn(BYTE nSteps) {
  int nDir = bCurrentDirection - (BYTE)nSteps * 8;
  BYTE bDir = (BYTE)nDir;
  bTargetDirection = bDir;
  bCurrentDirection = bDir;
  bMovementDirection = bDir;
  lVelocityX = (DIRECTIONS[bDir * 2]     * lCurrentSpeed) >> 8;
  lVelocityY = (DIRECTIONS[bDir * 2 + 1] * lCurrentSpeed) >> 8;
  CSprite *pSpr = pSprite;
  SetDestination(PosS(pSpr->wPosX), pSpr->wPosY);
  pSpr->SetFacing(nDir);
}

// ---------------------------------------------------------------------------
// animation
// ---------------------------------------------------------------------------

// 0x40CEB0.
void CFlingy::StopAnim() {
  pSprite->SwitchToIdle();
  bMoveFlags &= ~8;
}

// ---------------------------------------------------------------------------
// angles
// ---------------------------------------------------------------------------

// 0x40B990. The shortest angular distance between two facings, folded onto
// 0..128: take the difference modulo 256 and reflect anything past half a turn.
// Declared in src117/h/CUnitCombat.h, not here -- see the note at the foot of
// src117/h/CFlingy.h. Reflecting into nDelta keeps UpdateSpeed's inlined copies
// in one register.
int __fastcall get_angle(int nAngle1, int nAngle2) {
  int nDelta = (nAngle2 - nAngle1) & 0xFF;
  if (nDelta > 0x80)
    nDelta = 0x100 - nDelta;
  return nDelta;
}

// 0x40B940. The direction (0..0x40) whose tangent brackets a slope
// scaled by 256: a binary search over TANGENT_TABLE_g on the magnitude, sign
// reapplied. The magnitude goes to its own unsigned local, assigned in both
// arms; negating the parameter in place moves it out of ECX.
int __fastcall vec_atan(int nSlope256) {
  BOOL bNegative;
  unsigned int nValue;
  if (nSlope256 < 0) {
    bNegative = 1;
    nValue = -nSlope256;
  } else {
    bNegative = 0;
    nValue = nSlope256;
  }
  unsigned int nHigh = 64;
  unsigned int nLow = 0;
  unsigned int nMid = 32;
  do {
    if (nValue > TANGENT_TABLE_g[nMid])
      nLow = nMid + 1;
    else
      nHigh = nMid;
    nMid = (nLow + nHigh) >> 1;
  } while (nHigh != nLow);
  return bNegative ? -(int)nMid : (int)nMid;
}

// 0x40BA10. The facing from one point to another. __fastcall with the first pair
// in ECX and EDX and the second on the stack.
//
// The `cmov` operand orders and the sign-test sense look like scheduling but come
// from the source: `dy <= 0 ? 0 : 0x80` gives the oracle's `cmovle` where the
// inverted ternary gives `cmovg`, and `if (dx >= 0) return ...;` gives its
// forward `js` where testing for negative swaps the two blocks.
int __fastcall AngleBetween(int x0, int y0, int x1, int y1) {
  int nDeltaX = x1 - x0;
  int nDeltaY = y1 - y0;
  if (nDeltaX == 0)
    return nDeltaY <= 0 ? 0 : 0x80;
  int nAngle = vec_atan((nDeltaY << 8) / nDeltaX);
  if (nDeltaX >= 0)
    return nAngle + 0x40;
  nAngle += 0xC0;
  return nAngle == 0x100 ? 0 : nAngle;
}


// The list and lifetime half, the two remaining movement entry points, and five
// of the six update stages; UpdateSpeed (0x40D260) is still unported.
//
// Definition order inside the TU does not decide which calls survive: this
// compiler has whole-TU IL before it inlines, so what keeps a `call` is the
// inline budget, not visibility. The oracle says the same three times --
// AccelerateClamped_g inlines SetSpeed, ProgressMovement_dir_speed inlines
// ForceSpeed and SetTargetDest inlines SetTarget_xy, every one a callee defined
// later, while StopMovement calls BreakingDistance, defined earlier. What does
// matter is that landing a stage body here changes the budget every caller in
// the file is scored against.

// ---------------------------------------------------------------------------
// 0x40B9B0, 83 B. Take the head of the free list, initialise it, and move it to
// the used list. Static: ECX and EDX arrive as the first two arguments and go
// straight through to Init without ever being dereferenced.
//
// The CLink argument order is read off the pushes, which are right-to-left, so
// `push 5432ECh / push 54329Ch` is Unlink(&free_head, &free_tail) and
// `push 5432F4h / push 5432F0h` is PrependThis(&used_head, &used_tail) -- the
// element moves out of the free list and onto the front of the used one.
CFlingy * __fastcall CFlingy::AllocFlingy(int wFlingyID, int x, int y,
                                          int nPlayer) {
  CFlingy *pFlingy = sgpFreeHead_g;
  if (pFlingy != NULL && pFlingy->Init(wFlingyID, x, y, nPlayer, 0)) {
    reinterpret_cast<CLink *>(pFlingy)->Unlink(
        reinterpret_cast<CLink **>(&sgpFreeHead_g),
        reinterpret_cast<CLink **>(&sgpFreeTail_g));
    reinterpret_cast<CLink *>(pFlingy)->PrependThis(
        reinterpret_cast<CLink **>(&sgpUsedHead_g),
        reinterpret_cast<CLink **>(&sgpUsedTail_g));
    return pFlingy;
  }
  return NULL;
}

// ---------------------------------------------------------------------------
// 0x40BAE0, 195 B. Clamp the pixel pair into the map and republish both 24.8
// twins and the sprite position -- four independent arms, one per edge, each
// re-deriving the fixed-point pair from the pixel pair rather than adjusting it.
//
// The pixel pair is stored before the 24.8 pair. That order lets the negative-x
// arm share the over-width arm's tail, as in the oracle.
static __forceinline void bind_set(CFlingy *pFlingy, upos_t x, upos_t y) {
  pFlingy->wPositionX = x;
  pFlingy->wPositionY = y;
  pFlingy->lPositionX256 = PosS(x) << 8;
  pFlingy->lPositionY256 = PosS(y) << 8;
  pFlingy->pSprite->SetPosition(x, y);
}

// set_move_dir is not an oracle function: it is the guarded "adopt this movement
// direction and re-derive the velocity pair" block, which appears verbatim in
// ProgressMovement_dir, in ProgressMovement_dir_speed and twice in
// UpdateVelocity. Static, so the linker drops the body nothing calls.
static void __fastcall set_move_dir(CFlingy *pFlingy, BYTE bDir) {
  if (pFlingy->bMovementDirection != bDir) {
    unsigned int nDir = bDir;
    pFlingy->bMovementDirection = bDir;
    pFlingy->lVelocityX = (DIRECTIONS[nDir * 2]     * pFlingy->lCurrentSpeed) >> 8;
    pFlingy->lVelocityY = (DIRECTIONS[nDir * 2 + 1] * pFlingy->lCurrentSpeed) >> 8;
  }
}

void CFlingy::BindPositionToMap() {
  if (wPositionX < 0) {
    bind_set(this, 0, wPositionY);
  } else if (wPositionX >= (int)gwMapPixWidth) {
    bind_set(this, gwMapPixWidth - 1, wPositionY);
  }
  if (wPositionY < 0) {
    bind_set(this, wPositionX, 0);
  } else if (wPositionY >= (int)gwMapPixHeight) {
    // Read into a local first so the dimension stays in DI, as in the oracle.
    upos_t x = wPositionX;
    bind_set(this, x, gwMapPixHeight - 1);
  }
}

// ---------------------------------------------------------------------------
// 0x40BC30, 151 B. ProgressMovement_dir plus ForceSpeed, and with
// UpdateAcceleration absent the same way it is absent from
// ProgressMovement_dir. The second guarded velocity block reloads
// bMovementDirection from the object (`movzx ecx, byte ptr [esi+4Ah]`) rather
// than reusing the byte just stored, which is ForceSpeed's own body reading its
// own member -- i.e. the evidence that this is the inline and not a hand-copy.
void CFlingy::ProgressMovement_dir_speed(BYTE bDirection, int nSpeed) {
  sgbOldMoveFlags = bMoveFlags;
  if (bMovementDirection != bDirection) {
    unsigned int nDir = bDirection;
    bMovementDirection = bDirection;
    lVelocityX = (DIRECTIONS[nDir * 2]     * lCurrentSpeed) >> 8;
    lVelocityY = (DIRECTIONS[nDir * 2 + 1] * lCurrentSpeed) >> 8;
  }
  ForceSpeed(nSpeed);
  UpdateMovementFlags();
  UpdateNext();
  sgbStagedMoveFlags_g = bMoveFlags;
  bMoveFlags = sgbOldMoveFlags;
}

// ---------------------------------------------------------------------------
// 0x40BD10. Accelerate by nDelta and clamp to [0, lTopSpeed], with three inlined
// copies of SetSpeed, one per arm; in the zero arm the two DIRECTIONS products
// fold to a pair of zero stores, which is what identifies it as SetSpeed(0)
// rather than three bare stores.
void CFlingy::AccelerateClamped_g(int nDelta) {
  // volatile: the oracle re-reads [edx+38h] in each SetSpeed guard; a plain read is CSE'd.
  int nSpeed = *(volatile LONG *)&lCurrentSpeed + nDelta;
  if (nSpeed > lTopSpeed)
    SetSpeed(lTopSpeed);
  else if (nSpeed < 0)
    SetSpeed(0);
  else
    SetSpeed(nSpeed);
}

// ---------------------------------------------------------------------------
// 0x40BDB0, 207 B. Commit the tick: snapshot where we were, adopt the staged
// position/flags the stages left in the six sg* globals, republish to the
// sprite, settle the facing, and pick the animation. The BOOL it returns is "did
// the object actually move", from this body's own 0/1 pair.
BOOL CFlingy::BreakMovement() {
  BOOL bMoved;
  if (PosU(wPositionX) == sgwStagedX_g && PosU(wPositionY) == sgwStagedY_g)
    bMoved = 0;
  else
    bMoved = 1;
  sgwOldX_g = PosU(wPositionX);
  sgwOldY_g = PosU(wPositionY);
  bTargetDirection = bMovementDirection;
  bMoveFlags = sgbStagedMoveFlags_g;
  lTargetSpeed = lCurrentSpeed;
  wPositionX = PosS(sgwStagedX_g);
  wPositionY = PosS(sgwStagedY_g);
  lPositionX256 = sgnStagedDX_g;
  lPositionY256 = sgnStagedDY_g;
  pSprite->SetPosition(PosU(wPositionX), PosU(wPositionY));
  UpdateRotation();
  pSprite->SetFacing(bCurrentDirection);
  if (sgbStopping_g)
    pSprite->SetAnimation(0x0C, 0);
  else if (sgbMoving_g)
    pSprite->SetAnimation(0x0B, 1);
  return bMoved;
}

// ---------------------------------------------------------------------------
// 0x40BE80, 91 B. BreakMovement's tail without the position commit: aim the
// turn where we are travelling, adopt the staged flags, drop the target speed,
// settle the facing and pick the animation.
void CFlingy::BreakToIdle() {
  bTargetDirection = bMovementDirection;
  bMoveFlags = sgbStagedMoveFlags_g;
  lTargetSpeed = 0;
  UpdateRotation();
  pSprite->SetFacing(bCurrentDirection);
  if (sgbStopping_g)
    pSprite->SetAnimation(0x0C, 0);
  else if (sgbMoving_g)
    pSprite->SetAnimation(0x0B, 1);
}

// ---------------------------------------------------------------------------
// 0x40C120, 143 B. Face a random direction and re-aim one pixel that way, with
// SetTargetDest inlined. The distance is 0x100 at 24.8, so the multiply and the
// shift cancel and MSVC still emits the `shl 8h / sar 8h` pair. The roll is
// masked into an int and the index is a separate `(BYTE)` copy of it; that
// keeps the oracle's `and eax, 0FFh` / `movzx edx, al` pair.
void CFlingy::FaceAnywhere() {
  int nRoll = net_rand(2) & 0xFF;
  bTargetDirection = (BYTE)nRoll;
  bCurrentDirection = (BYTE)nRoll;
  unsigned int nDir = (BYTE)nRoll;
  int nX = ((DIRECTIONS[nDir * 2]     * 0x100) >> 8) + PosS(pSprite->wPosX);
  int nY = ((DIRECTIONS[nDir * 2 + 1] * 0x100) >> 8) + pSprite->wPosY;
  SetTargetDest(nX, nY);
}

// ---------------------------------------------------------------------------
// 0x40C2B0, 238 B. Seat a pool element: the four flingy.dat columns, the
// position pair and its 24.8 twins, SetTargetDest (inlined, with the flag
// read-modify-write constant-folded to `mov byte ptr [esi+20h], 1h` because
// bMoveFlags was set to 0 three statements earlier), then the CThingy half and
// the initial facing.
BOOL CFlingy::Init(WORD wFlingyID_, spos_t x, spos_t y, int nPlayer,
                   int nDirection) {
  unsigned int nRow = wFlingyID_;
  if (!FlingyTypeValid(nRow))  // Hyperion: a mod's script or table may name any flingy
    return FALSE;
  wFlingyID = wFlingyID_;
  bMoveFlags = 0;
  lTargetSpeed = 0;
  lTopSpeed = gxFlingyMaxVel[nRow];
  wAcceleration = gxFlingyAccel[nRow];
  bTurnRadius = gubFlingyMaxTurn[nRow];
  bMovementType = gubFlingyMoveType[nRow];
  wPositionX = x;
  wPositionY = y;
  lPositionX256 = x << 8;
  lPositionY256 = y << 8;
  int nX = x;
  int nY = y;
  SetTargetDest(nX, nY);
  bCurrentDirection = (BYTE)nDirection;
  bTargetDirection = (BYTE)nDirection;
  if (!reinterpret_cast<CThingy *>(this)->Init(guwFlingySprite[nRow],
                                              x, y, nPlayer))
    return 0;
  pSprite->SetFacing(bCurrentDirection);
  return 1;
}

// ---------------------------------------------------------------------------
// 0x40C3A0. Static. arr\flingy.dat in, the pool element zeroed, and the two
// lists seated. The store order is the oracle's: each tail before its head.
void __fastcall CFlingy::InitFlingies() {
  load_array_data("arr\\flingy.dat", gFLINGYArrayDataTbl_g);
  memset(&sgDummyFlingy_g, 0, sizeof(CFlingy));
  sgpFreeTail_g = &sgDummyFlingy_g;
  sgpFreeHead_g = &sgDummyFlingy_g;
  sgpUsedTail_g = NULL;
  sgpUsedHead_g = NULL;
}

// ---------------------------------------------------------------------------
// 0x40C910, 209 B. Static; the per-frame walk. pNext is cached before
// SpriteUpdate because the body may unlink the element it is standing on.
// The map-bounds pair compares a sign-extended coordinate unsigned, so a
// negative coordinate fails the same branch as a too-large one.
void __fastcall CFlingy::UpdateFlingies() {
  for (CFlingy *pFlingy = sgpUsedHead_g; pFlingy != NULL; ) {
    CFlingy *pNext = pFlingy->pNext;
    reinterpret_cast<CThingy *>(pFlingy)->SpriteUpdate();
    if (pFlingy->pSprite == NULL) {
      reinterpret_cast<CLink *>(pFlingy)->Unlink(
          reinterpret_cast<CLink **>(&sgpUsedHead_g),
          reinterpret_cast<CLink **>(&sgpUsedTail_g));
      reinterpret_cast<CLink *>(pFlingy)->PrependThis(
          reinterpret_cast<CLink **>(&sgpFreeHead_g),
          reinterpret_cast<CLink **>(&sgpFreeTail_g));
    } else {
      sgbOldMoveFlags = pFlingy->bMoveFlags;
      pFlingy->UpdateAcceleration();
      pFlingy->UpdateSpeed();
      pFlingy->UpdateMovementFlags();
      pFlingy->UpdateNext();
      sgbStagedMoveFlags_g = pFlingy->bMoveFlags;
      pFlingy->bMoveFlags = sgbOldMoveFlags;
      pFlingy->BreakMovement();
      CSprite *pSpr = pFlingy->pSprite;
      if ((unsigned int)(int)PosS(pSpr->wPosX) >= (unsigned int)gwMapPixWidth
          || (unsigned int)(int)pSpr->wPosY >= (unsigned int)gwMapPixHeight
          || (pFlingy->wMoveTargetX == pFlingy->wPositionX
              && pFlingy->wMoveTargetY == pFlingy->wPositionY))
        pSpr->SetAnimation(1, 1);
    }
    pFlingy = pNext;
  }
}

// ---------------------------------------------------------------------------
// 0x40CE10, 149 B. Cancel the move: park the move target on the current
// position, then, when moving on the ground, project it BreakingDistance ahead
// along bTargetDirection so the object coasts to a stop.
// BreakingDistance stays a real call although it is defined earlier in this TU
// (see the ordering note at the head of this block).
void CFlingy::StopMovement() {
  BYTE bFlags = bMoveFlags;
  if (bFlags & 4)
    return;
  bFlags &= ~0x70;
  wMoveTargetX = wPositionX;
  wMoveTargetY = wPositionY;
  bMoveFlags = bFlags;
  pMoveTargetUnit = NULL;
  nextWaypoint = moveTarget;
  if (!(bFlags & 2))
    return;
  if (bMovementType != 0)
    return;
  int nDist = BreakingDistance();
  unsigned int nDir = bTargetDirection;
  LONG lTermX = DIRECTIONS[nDir * 2]     * nDist;
  LONG lTermY = DIRECTIONS[nDir * 2 + 1] * nDist;
  wMoveTargetX = (short)(((lTermX >> 8) + lPositionX256) >> 8);
  // volatile keeps the Y load after the X store (147 B without it). This is the
  // field's only read here, taken once, in order; both forms are the oracle's bytes.
  int nY = (*static_cast<volatile LONG *>(&lPositionY256) + (lTermY >> 8)) >> 8;
  wMoveTargetY = PosS(nY);
  nextWaypoint = moveTarget;
  if (wMoveTargetX != PosS(pSprite->wPosX) || PosS(nY) != pSprite->wPosY) {
    bFlags |= 4;
    bMoveFlags = bFlags;
  }
}

// ---------------------------------------------------------------------------
// 0x40CF50, 196 B. Stage 1: decide which way we want to go, then hand off to
// UpdateVelocity with a tail `jmp`.
//
// AngleBetween is inlined at one call site and a real call at the other, which is
// not a source asymmetry: the compiler spends a per-function inline budget on the
// first site it reaches, and nothing in the source distinguishes the two.
void CFlingy::UpdateAcceleration() {
  if (wNextWaypointX == wPositionX && wNextWaypointY == wPositionY) {
    if (wDestinationX == wPositionX && wDestinationY == wPositionY)
      bDesiredDirection = bCurrentDirection;
    else
      bDesiredDirection = (BYTE)AngleBetween(wPositionX, wPositionY,
                                             wDestinationX, wDestinationY);
  } else {
    bDesiredDirection = (BYTE)AngleBetween(wPositionX, wPositionY,
                                           wNextWaypointX, wNextWaypointY);
  }
  UpdateVelocity();
}

// ---------------------------------------------------------------------------
// 0x40D020. Stage 4: work out where the object would be next frame and publish it
// to the staged globals. The waypoint is snapped to rather than overshot when
// this frame's speed would carry the object past it, and the distance left over
// becomes the staged speed.
void CFlingy::UpdateNext() {
  sgnStagedSpeed_g = lCurrentSpeed;
  if ((bMoveFlags & 2) && lCurrentSpeed != 0) {
    int nDist = veclen(lPositionX256, PosS(wNextWaypointX) << 8,
                       lPositionY256, PosS(wNextWaypointY) << 8);
    if (lCurrentSpeed >= nDist) {
      sgwStagedX_g = wNextWaypointX;
      sgwStagedY_g = wNextWaypointY;
      sgnStagedDX_g = PosS(sgwStagedX_g) << 8;
      sgnStagedDY_g = PosS(sgwStagedY_g) << 8;
      sgnStagedSpeed_g = nDist;
    } else {
      LONG lNextX = lVelocityX + lPositionX256;
      sgnStagedDX_g = lNextX;
      LONG lNextY = lVelocityY + lPositionY256;
      sgnStagedDY_g = lNextY;
      sgwStagedX_g = (WORD)(lNextX >> 8);
      sgwStagedY_g = (WORD)(lNextY >> 8);
    }
    if (bMovementType == 2 && lCurrentSpeed != 0) {
      lCurrentSpeed = 0;
      lVelocityX = 0;
      lVelocityY = 0;
    }
    return;
  }
  sgwStagedX_g = PosU(wPositionX);
  sgwStagedY_g = PosU(wPositionY);
  sgnStagedDX_g = lPositionX256;
  sgnStagedDY_g = lPositionY256;
}

// ---------------------------------------------------------------------------
// 0x40D110, 130 B. Publishes whether the object is moving or stopping this
// tick and keeps bMoveFlags bit 1 in step with reaching the move target.
// The bit is tested as an expression in both ifs, not through a local, so the
// oracle's second `test bl, bl` survives; the zero local feeds both stores and
// the cmovz tail. The final sgbMoving_g store runs only on the not-moving,
// not-at-target path.
// noinline: every 1.17 caller calls it, and UpdateFlingies would inline it
// otherwise.
__declspec(noinline) void CFlingy::UpdateMovementFlags() {
  BYTE bFlags = bMoveFlags;
  BYTE bZero = 0;
  sgbMoving_g   = bZero;
  sgbStopping_g = bZero;
  if ((bFlags >> 1) & 1) {
    if (wMoveTargetX == wPositionX && wMoveTargetY == wPositionY) {
      bFlags &= ~2;
      bMoveFlags = bFlags;
      if (!(bFlags & 8))
        sgbStopping_g = 1;
      return;
    }
  }
  if (!((bFlags >> 1) & 1)) {
    if (wMoveTargetX != wPositionX || wMoveTargetY != wPositionY) {
      if (bMovementType != 2 || !(bFlags & 8)) {
        bFlags |= 2;
        bMoveFlags = bFlags;
      }
      if (!(bMoveFlags & 8))
        bZero = 1;
      sgbMoving_g = bZero;
    }
  }
}

// ---------------------------------------------------------------------------
// 0x40D1A0, 178 B. Turn the drawn facing towards the desired one by at most
// bTurnRadius, bump the radius for two flingy-id windows, then clear bMoveFlags
// bit 0 once all three facings agree. The store through rCur makes later reads
// re-read the member, and arm 1 reads the radius member so its sum goes to AL.
// Arm 3 written twice under a bRadius test merges after allocation; the extra
// references give the facing CL and the delta CH.
BOOL CFlingy::UpdateRotation() {
  BYTE bFlags = bMoveFlags;
  if ((bFlags & 2) && !(bFlags & 1))
    return 0;
  char cTurn = TurnDelta(bCurrentDirection, bDesiredDirection, 0x80);
  BYTE bRadius = bTurnRadius;
  BYTE &rCur = bCurrentDirection;
  if (cTurn > (char)bRadius)
    bCurrentDirection = (BYTE)(bTurnRadius + bCurrentDirection);
  else if ((int)cTurn < -(int)(char)bRadius)
    rCur -= bRadius;
  else if (bRadius)
    bCurrentDirection = (BYTE)(bCurrentDirection + cTurn);
  else
    bCurrentDirection = (BYTE)(bCurrentDirection + cTurn);
  WORD wId = wFlingyID;
  if ((wId >= 0x8D && wId <= 0xAB) || (WORD)(wId - 0xC9) <= 5)
    bTurnRadius = (BYTE)(bRadius + 1);
  if (bTargetDirection == bDesiredDirection && bCurrentDirection == bDesiredDirection)
    bMoveFlags = (BYTE)(bFlags & ~1);
  return 1;
}

// ---------------------------------------------------------------------------
// Is the flingy standing on its move target? UpdateSpeed's three tests call
// this; the call is what sets MSVC's frequency estimate for the stop block.
static inline bool IsAtMoveTarget(const CFlingy *p) {
  return p->wMoveTargetX == p->wPositionX && p->wMoveTargetY == p->wPositionY;
}

// veclen over two points given as (x, y) pairs. The parameters are evaluated
// before veclen's own arguments, which gives the oracle's load order.
static __forceinline unsigned int FlingyDistance(int x1, int y1, int x2,
                                                 int y2) {
  return veclen(x1, x2, y1, y2);
}

// ---------------------------------------------------------------------------
// 0x40D260, 485 B. Stage 2: choose this tick's speed from the movement type,
// the turn still to make and the distance left to the waypoint.
// MSVC inlines SetSpeed only where it estimates the block to be hot. The
// at-target test as a predicate call is what makes the stop block hot and the
// case 2 SetSpeed(lTargetSpeed) cold, as in the oracle. Case 2 keeps its own
// stop, which MSVC tail-merges into case 1's; a fallthrough re-tests instead.
void CFlingy::UpdateSpeed() {
  switch (bMovementType) {
  case 2:
    if (IsAtMoveTarget(this)) {
      bMoveFlags &= ~4;
      SetSpeed(0);
      return;
    }
    {
      BYTE bFlags = bMoveFlags;
      if ((bFlags & 1) &&
          get_angle(bDesiredDirection, bCurrentDirection) >= 0x20) {
        if (bFlags & 2) {
          bMoveFlags = bFlags & ~6;
          SetSpeed(0);
        }
        return;
      }
      SetSpeed(lTargetSpeed);
      return;
    }
  case 1:
    if (IsAtMoveTarget(this)) {
      bMoveFlags &= ~4;
      SetSpeed(0);
      return;
    }
    break;
  case 0:
    if (IsAtMoveTarget(this) && lTargetSpeed < 0xC0 &&
        !(bMoveFlags & 0x30)) {
      bMoveFlags &= ~4;
      SetSpeed(0);
      return;
    }
    break;
  }
  SetSpeed(lTargetSpeed);
  unsigned int nLen = FlingyDistance(wPositionX, wPositionY, wNextWaypointX,
                                     wNextWaypointY);
  BYTE bMoving = bMovementDirection;
  BYTE bDesired = bDesiredDirection;
  if (bMoving != bDesired && nLen < 0x20) {
    int nAngle = get_angle(bMoving, bDesired);
    unsigned int nTurns = (bTurnRadius - 1 + nAngle * 2) / bTurnRadius;
    if (((nTurns * lTargetSpeed) >> 8) * 3 / 2 > nLen) {
      AccelerateClamped_g(-wAcceleration);
      return;
    }
  }
  if (bMovementType == 0 && (!(bMoveFlags & 0x10) || (bMoveFlags & 0x40)) &&
      (!(bMoveFlags & 0x20) ||
       wNextWaypointX != wMoveTargetX || wNextWaypointY != wMoveTargetY)) {  // Hyperion: by field (stage E)
    unsigned int nBrake = BreakingDistance();
    if (nBrake >= (unsigned int)veclen(lPositionX256, wNextWaypointX << 8,
                                       lPositionY256, wNextWaypointY << 8)) {
      AccelerateClamped_g(-wAcceleration);
      return;
    }
  }
  AccelerateClamped_g(wAcceleration);
}

// ---------------------------------------------------------------------------
// 0x40D450, 154 B. Step bMovementDirection towards bTargetDirection, by the whole
// remaining delta once the desired facing is reached and otherwise clamped to the
// turn radius (halved unless bMovementType == 2), and re-derive the velocity pair.
// The set block is written out on each path; the two merged pairs keep the turn
// path's copy apart from the equal path's until after allocation.
void CFlingy::UpdateVelocity() {
  if (bDesiredDirection == bTargetDirection) {
    if (bMovementDirection != bTargetDirection) {
      bMovementDirection = bTargetDirection;
      lVelocityX = (DIRECTIONS[bTargetDirection * 2] * lCurrentSpeed) >> 8;
      lVelocityY = (DIRECTIONS[bTargetDirection * 2 + 1] * lCurrentSpeed) >> 8;
    }
    return;
  }
  unsigned nMax = bTurnRadius;
  char cTurn = (char)(bDesiredDirection - bTargetDirection);
  int nDelta = cTurn;
  if (bMovementType != 2)
    nMax >>= 1;
  if (nDelta >= (int)nMax)
    cTurn = (char)nMax;
  else if (nDelta <= -(int)nMax) {
    // Merged arms under the equal path's test; they compile to one store.
    if (bMovementDirection != bTargetDirection)
      cTurn = (char)-(char)nMax;
    else
      cTurn = (char)-(char)nMax;
  }
  BYTE bNew = (BYTE)(bTargetDirection + cTurn);
  if (bMovementDirection != bNew) {
    bMovementDirection = bNew;
    // Merged arms under the clamp test; they compile to one pair of stores.
    if (nDelta >= (int)nMax) {
      lVelocityX = (DIRECTIONS[bNew * 2] * lCurrentSpeed) >> 8;
      lVelocityY = (DIRECTIONS[bNew * 2 + 1] * lCurrentSpeed) >> 8;
    } else {
      lVelocityX = (DIRECTIONS[bNew * 2] * lCurrentSpeed) >> 8;
      lVelocityY = (DIRECTIONS[bNew * 2 + 1] * lCurrentSpeed) >> 8;
    }
  }
}

// 0x40BBB0. Nothing of its own to swap; forwards to the base.
void CFlingy::BitSwap() {
  reinterpret_cast<CThingy *>(this)->BitSwap();
}

// 0x40C720. The move-target id becomes a pointer again, then, when the links
// were saved, the 1-based pool indices do too. The second link is a ternary so
// its zero arm keeps its own store and epilogue, as in CThingy::PostLoad.
void CFlingy::PostLoad(BOOL bLinked) {
  reinterpret_cast<CThingy *>(this)->PostLoad(0, 0);
  pMoveTargetUnit = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pMoveTargetUnit));
  if (!bLinked)
    return;
  CFlingy *q = pPrev;
  if (q != 0)
    q = &(&sgDummyFlingy_g)[reinterpret_cast<intptr_t>(q) - 1];
  pPrev = q;
  q = pNext;
  pNext = q == 0 ? 0 : &(&sgDummyFlingy_g)[reinterpret_cast<intptr_t>(q) - 1];
}

// 0x40C780. PostLoad's inverse; the id conversion precedes the base call here.
void CFlingy::SaveEncode(BOOL bLinked) {
  pMoveTargetUnit = reinterpret_cast<CUnit *>((intptr_t)pMoveTargetUnit->GetUnitID());
  reinterpret_cast<CThingy *>(this)->PreSave(0, 0);
  if (!bLinked)
    return;
  CFlingy *q = pPrev;
  if (q != 0) {
    DWORD_PTR off = reinterpret_cast<DWORD_PTR>(q) -
                reinterpret_cast<DWORD_PTR>(&sgDummyFlingy_g);
    q = reinterpret_cast<CFlingy *>(off / sizeof(CFlingy) + 1);
  }
  pPrev = q;
  q = pNext;
  pNext = q == 0 ? 0
                 : reinterpret_cast<CFlingy *>(
                       (reinterpret_cast<DWORD_PTR>(q) -
                        reinterpret_cast<DWORD_PTR>(&sgDummyFlingy_g)) /
                           sizeof(CFlingy) +
                       1);
}

// 0x40C050. Converts the pool's one element to or from its saved form, links
// included. Through a pointer: named directly, both inlined zero arms store an
// immediate 0 and MSVC merges them into one tail (2 B short). noinline: the
// oracle's SaveFlingies calls it, and the shipped form inlines it otherwise.
__declspec(noinline) void __fastcall CFlingy::EncodeDecodeListForSave(BOOL bEncode) {
  CFlingy *p = &sgDummyFlingy_g;
  if (bEncode)
    p->SaveEncode(TRUE);
  else
    p->PostLoad(TRUE);
}

// 0x40C7F0. Rebuilds the free and used lists from the pool after a load: the
// free list from the elements without a sprite, the used list from the rest.
// The searches walk a byte offset beside the index: indexed, the shipped form
// turns the loop into a pointer walk that bakes the end address 0x5432F8.
void CFlingy::RecreateFlingyList() {
  unsigned i;
  DWORD off;
  for (i = 0, off = 0; off < sizeof(CFlingy); off += sizeof(CFlingy), i++) {
    if (*reinterpret_cast<CSprite **>(
            reinterpret_cast<char *>(&sgDummyFlingy_g.pSprite) + off) == 0)
      break;
  }
  if (i == 1) {
    sgpFreeHead_g = 0;
    sgpFreeTail_g = 0;
  } else {
    sgpFreeHead_g = &(&sgDummyFlingy_g)[i];
    sgpFreeTail_g = &(&sgDummyFlingy_g)[i];
    CFlingy *pLast = &(&sgDummyFlingy_g)[i++];
    for (; i < 1; i++) {
      CFlingy *p = &(&sgDummyFlingy_g)[i];
      if (p->pSprite == 0) {
        reinterpret_cast<CLink *>(pLast)->AppendLink(
            reinterpret_cast<CLink *>(p),
            reinterpret_cast<CLink **>(&sgpFreeTail_g));
        pLast = p;
      }
    }
  }
  for (i = 0, off = 0; off < sizeof(CFlingy); off += sizeof(CFlingy), i++) {
    if (*reinterpret_cast<CSprite **>(
            reinterpret_cast<char *>(&sgDummyFlingy_g.pSprite) + off) != 0)
      break;
  }
  if (i == 1) {
    sgpUsedHead_g = 0;
    sgpUsedTail_g = 0;
  } else {
    (&sgDummyFlingy_g)[i].pPrev = 0;
    CFlingy *pLast = &(&sgDummyFlingy_g)[i];
    sgpUsedHead_g = pLast;
    sgpUsedTail_g = pLast;
    (&sgDummyFlingy_g)[i++].pNext = 0;
    for (; i < 1; i++) {
      CFlingy *p = &(&sgDummyFlingy_g)[i];
      if (p->pSprite != 0) {
        p->pPrev = 0;
        p->pNext = 0;
        reinterpret_cast<CLink *>(pLast)->AppendLink(
            reinterpret_cast<CLink *>(p),
            reinterpret_cast<CLink **>(&sgpUsedTail_g));
        pLast = p;
      }
    }
  }
}

// One saved pool element: its pool index, then the element itself, packed.
#pragma pack(push, 1)
struct FlingySaveRec {
  /*0x00*/ WORD wIndex;
  /*0x02*/ CFlingy flingy;
};
#pragma pack(pop)
OC_X86_LAYOUT(sizeof(FlingySaveRec) == 0x4E, "imul 4Eh in Load/SaveFlingies");
static_assert(offsetof(FlingySaveRec, wIndex) == 0x00, "movzx eax, word ptr [edi]");
static_assert(offsetof(FlingySaveRec, flingy) == 0x02, "movdqu xmm0, [edi+2h]");

// 0x40C580. Reads the pool back: a WORD record count, then the compressed
// records, each copied to the element its index names.
BOOL CFlingy::LoadFlingies(void *pFile) {
  WORD wCount;
  if (!pfread(&wCount, 2, pFile))
    return FALSE;
  memset(&sgDummyFlingy_g, 0, sizeof(CFlingy));
  if (wCount != 0) {
    // Hyperion: a record is the index and the flingy in 1.17's layout,
    // FlingySaveRec on x86.
    int nSize = wCount * (2 + PortSave117Size(SAVE117_CFlingy));
    BYTE *pBuf = static_cast<BYTE *>(SMemAlloc(nSize, "CFlingy.cpp", 1362, 0));
    const BYTE *pRec = pBuf;
    memset(pBuf, 0, nSize);
    if (!decompress(pBuf, nSize, pFile)) {
      SMemFree(pBuf, "CFlingy.cpp", 1368, 0);
      return FALSE;
    }
    for (WORD i = 0; i < wCount; i++) {
      WORD w = *reinterpret_cast<const WORD *>(pRec);
      if (w != 0) {  // Hyperion: the pool is the one flingy; a save naming another is broken
        SMemFree(pBuf, "CFlingy.cpp", 1368, 0);
        return FALSE;
      }
      pRec = PortSave117Get(SAVE117_CFlingy, pRec + 2, &(&sgDummyFlingy_g)[w]);
      (&sgDummyFlingy_g)[w].BitSwap();
    }
    SMemFree(pBuf, "CFlingy.cpp", 1386, 0);
  }
  // A loop over the pool, not a call to EncodeDecodeListForSave: the unrolled
  // loop keeps its aligned head, which is the oracle's `jmp` over padding.
  for (int k = 0; k < 1; k++)
    (&sgDummyFlingy_g)[k].PostLoad(TRUE);
  RecreateFlingyList();
  return TRUE;
}

// 0x40C9F0. Writes the pool: a WORD count of elements with a sprite, then those
// elements compressed, each behind its pool index. The count variable is
// reused as the record cursor.
BOOL CFlingy::SaveFlingies(void *pFile) {
  EncodeDecodeListForSave(TRUE);
  DWORD dwCount = 0;
  if (sgDummyFlingy_g.pSprite != 0)
    dwCount = 1;
  BOOL bOk = pfwrite(&dwCount, 2, pFile);
  if (bOk && (WORD)dwCount != 0) {
    const int nRecord = 2 + PortSave117Size(SAVE117_CFlingy);  // Hyperion
    int nSize = (WORD)dwCount * nRecord;
    BYTE *pBuf = static_cast<BYTE *>(SMemAlloc(nSize, "CFlingy.cpp", 1318, 0));
    memset(pBuf, 0, nSize);
    dwCount = 0;
    for (int i = 0; i < 1; i++) {
      CFlingy *p = &(&sgDummyFlingy_g)[i];
      if (p->pSprite != 0) {
        BYTE *pRec = pBuf + dwCount * nRecord;
        *reinterpret_cast<WORD *>(pRec) = (WORD)i;
        p->BitSwap();
        PortSave117Put(SAVE117_CFlingy, p, pRec + 2);
        p->BitSwap();
        dwCount++;
      }
    }
    bOk = PortSaveChunk(pBuf, nSize, dwCount, pFile);
    SMemFree(pBuf, "CFlingy.cpp", 1339, 0);
  }
  for (int k = 0; k < 1; k++)
    (&sgDummyFlingy_g)[k].PostLoad(TRUE);
  return bOk;
}

// 0x40C3F0. Aims at where the target will be by the time this object can get
// there: its sprite position pushed along its facing by the distance it covers
// in that time, clamped to the map. A target at rest is aimed at directly.
void CFlingy::InterceptTarget(CUnit *pTarget) {
  CFlingy *t = reinterpret_cast<CFlingy *>(pTarget);
  int nMySpeed = (lTopSpeed * 2) >> 8;
  if ((t->wMoveTargetX != t->wPositionX || t->wMoveTargetY != t->wPositionY) &&
      nMySpeed > 0) {
    if (t->bMoveFlags & 4) {
      SetTargetDest(t->wMoveTargetX, t->wMoveTargetY);
      return;
    }
    upos_t wTY = t->pSprite->wPosY;
    upos_t wMX = pSprite->wPosX;
    upos_t wMY = pSprite->wPosY;
    upos_t wTX = t->pSprite->wPosX;
    // A short copy of x ahead of the call sets the argument registers.
    spos_t nMX = PosS(wMX);
    int nTime =
        veclen_precise(nMX, PosS(wTX), PosS(wMY), PosS(wTY)) /
        nMySpeed;
    int nSpeed;
    if (bMovementType == 2) {
      nSpeed = t->lTopSpeed;
    } else {
      nSpeed = t->wAcceleration * nTime / 2 + t->lTargetSpeed;
      if (nSpeed > t->lTopSpeed)
        nSpeed = t->lTopSpeed;
    }
    int nDir = t->bTargetDirection;
    int nDist = nSpeed * nTime;
    int x = ((DIRECTIONS[nDir * 2] * nDist) >> 16) +
            PosS(t->pSprite->wPosX);
    int y = ((DIRECTIONS[nDir * 2 + 1] * nDist) >> 16) +
            PosS(t->pSprite->wPosY);
    if (x < 0)
      x = 0;
    else if (x >= gwMapPixWidth)
      x = gwMapPixWidth - 1;
    if (y < 0)
      y = 0;
    else if (y >= gwMapPixHeight)
      y = gwMapPixHeight - 1;
    SetTargetDest(x, y);
    return;
  }
  SetTargetDest(PosS(t->pSprite->wPosX), PosS(t->pSprite->wPosY));
}
