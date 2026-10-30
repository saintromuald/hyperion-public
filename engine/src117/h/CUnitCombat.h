// CUnitCombat.h -- the free functions of CUnitCombat.cpp's oracle TU, plus the
// free callees its bodies reach in other TUs, noted per line. The TU's members
// are declared with their class, in CUnit.h.
//
// Every signature here is spelled __fastcall even though /Gr makes it the
// default: these state an ABI contract with code the port has not authored.
#pragma once

#include "platform.h"   // ours: BOOL, WORD
#include "order_target.h"   // ours: order_target_t, for ai_get_flee_target below

class CUnit;            // a forward declaration is enough: no signature below
                        // dereferences the type, and including CUnit.h from here
                        // would make this header order-dependent for every
                        // consumer that already includes CUnit.h itself.

// IsVulnerableType -- oracle 0x420A90, 32 B, defined in CUnitCombat.cpp. Returns
// the inverse of bit 29 of gulUnitTypeFlags[wUnitType]: bit 29 set means
// invulnerable, so TRUE is a type that can be hurt. A type at or past 228, the
// units.dat entry count, reports not vulnerable. The argument is two bytes, the
// width of CUnit::wType at +0x64, and the body compares it through CX.
BOOL __fastcall IsVulnerableType(WORD wUnitType);

// sub_421370 -- oracle 0x421370, 65 B, defined in CUnitCombat.cpp. A free
// function, not a member: the unit in ECX, the X in EDX and the Y in one dword
// stack slot cleaned by `ret 4h`. It reads CUnit::bPathingFlags (+0x105) and
// answers 1 only when bit 0 is set and at least one of the two reachability
// tests below fails, the inverse of its callees' sense. What the bit means is
// not derived; see CUnit.h's note on the field.
BOOL __fastcall sub_421370(CUnit *pUnit, int nX, int nY);

// sub_421320 -- oracle 0x421320, 66 B. sub_421370's (pUnit, pTarget) wrapper and
// free in the same way: the unit in ECX, the target in EDX, a bare `ret`, and
// sub_421370's inverted sense. 1.17 inlines the sibling instead of calling it and
// reaches UnitCanReachGroundTarget_xy / tai_captain_unit_connected_to_xy
// directly, so the two bodies have to stay in one TU.
BOOL __fastcall sub_421320(CUnit *pUnit, CUnit *pTarget);

// oracle 0x481D00 (sai.cpp) and 0x4AB450 (TAI_Captain.cpp). Callees only. Both
// take the same three arguments in the same places -- unit in ECX, X in EDX, Y on
// the stack -- and the pairing is the callee's own: each calls GetRegion_xy for
// the unit's sprite position, then again with EDX as the X and the stack slot as
// the Y. Results truth-tested, so BOOL.
BOOL __fastcall UnitCanReachGroundTarget_xy(CUnit *pUnit, int nX, int nY);
BOOL __fastcall tai_captain_unit_connected_to_xy(CUnit *pUnit, int nX, int nY);

// get_angle -- oracle 0x40B990, 27 B, CFlingy.cpp. Callee only. Two register
// arguments and nothing pushed. The return is `int`, not DWORD: its caller does
// `cmp eax, 20h / jle`, a signed compare. The two values are zero-extended
// CUnit::bDirection bytes, widened by the caller rather than here.
int __fastcall get_angle(int nAngle1, int nAngle2);

// oracle 0x4B2110 (TAI_GuardPost.cpp). Callee only. The unit in ECX and a second
// argument in EDX that CUnit::DispatchAttackUnit, the one call site, passes as 0;
// what that argument means is not derived. Nothing is pushed, and the result is
// truth-tested, so BOOL.
BOOL __fastcall guard_return_to_post(CUnit *pUnit, int nControl);

// oracle 0x421180, this TU. A pointer in ECX and a CUnit* in EDX; the result
// comes back in EAX:EDX, the 8-byte order_target_t by value, which TAI_Captain's
// tai_reposition_for_attack pushes straight into CUnit::SetOrder_Target. The
// first parameter is known only to be a pointer.
order_target_t __fastcall ai_get_flee_target(void *pArg0, CUnit *pUnit);

// oracle 0x4B1100 (TAI_Captain.cpp). Callee only. One CUnit* in ECX, nothing
// pushed, the result unread at the one call site, so void.
void __fastcall tai_soldier_try_stim(CUnit *pUnit);

// oracle 0x446F00 (gamesnd.cpp). Callee only. One CUnit* in ECX, nothing pushed,
// result unread, so void. CUnit::DamageNotify reaches it only when the attacker
// belongs to another player: the unit's owner is told it is under attack.
void __fastcall gamesnd_under_attack(CUnit *pUnit);

// 0x4202C0 is the static __fastcall member CUnit::NewTarget, declared in CUnit.h.

// GetVectorComponents (0x40C270) is a CFlingy member, declared in CFlingy.h:
// every caller hands it an object in ECX that it never reads.

// oracle 0x4B2270, unported. Callee only, with the (unit, x, y) shape of
// UnitCanReachGroundTarget_xy above: CUnit::DispatchAIReturn puts the unit in
// ECX and the sprite X in EDX and pushes the sprite Y. The result is unread at
// both referents, so void. The coordinates are 32 bits although the body reads
// its stack slot `mov ax`: the caller sign-extends both halves, which only a
// 32-bit parameter requires. `int` follows the sources, CSprite::wPosX and wPosY.
void __fastcall guard_move_post(CUnit *pUnit, int nX, int nY);

// oracle 0x48D2E0, 57 B. Callee only. Both arguments are in registers, the
// target in EDX and the unit in ECX, with nothing cleaned after. The result is
// truth-tested, so BOOL; CUnit::ChangeTarget, the only caller, takes TRUE as
// disqualifying a candidate as this unit's next attack target.
BOOL __fastcall ai_target_unreachable(CUnit *pA, CUnit *pB);

// oracle 0x4AEBC0, 1632 B. Callee only. Victim in ECX, attacker in EDX, then two
// more dword arguments the callee cleans, which OnDamaged's site passes as
// (1, 0). It re-reads gbySlotRecords[...] itself, so those two ints are opaque
// here.
void __fastcall tai_unit_was_attacked(CUnit *pVictim, CUnit *pAttacker, int a3, int a4);

// oracle 0x46BBB0, 267 B. Callee only. Six arguments, all read off OnDamaged's
// one call site: the warn-site sprite X in ECX and its Y in EDX, then the bit-2
// flag of [attacker+DC], the one-hot player mask 1 << bPlayer, TRUE, and the
// IsVisibleTo result. The roles past X and Y are positional only.
void __fastcall sub_46BBB0(int nX, int nY, int a3, int a4, int a5, int a6);

// oracle 0x4AE3E0, 672 B. Callee only. (source, target) are the two register
// arguments: NewTarget's site does `mov edx, edi` with ECX already the source
// and pushes nothing, then cleans its own `ret 8h` frame. Result unread, so
// void.
void __fastcall tai_call_support_units(CUnit *pSource, CUnit *pTarget);
