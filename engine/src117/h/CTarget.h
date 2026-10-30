// CTarget.h -- the order-state base class shared by CUnit and CBullet: the target a thing
// is currently ordered at, and the setters that install one.
//
// It owns the basename on purpose: generated code resolves a class by exact `<Class>.h`
// over an include path whose later roots are the quarantined reference tree, so without
// this file theirs would win the match and be included.
//
// CBullet::Order reads `[ecx+5Ch]` off its own `this` and then calls
// CTarget::SetTargetOrder_CUnit without touching ECX, and that callee is __thiscall on
// ECX, so a CBullet* is a CTarget* with no adjustment: single inheritance at offset 0.
// CUnit is authored flat instead, src117/gen/hook_table.cpp depending on it staying
// non-polymorphic, so a CUnit call site spells `reinterpret_cast<CTarget *>(this)->...`.
#pragma once

#include "platform.h"   // ours: BYTE/WORD + <stddef.h> for offsetof
#include "pos.h"        // Hyperion: spos_t (stage E)
#include "typelimits.h" // Hyperion: order_id_t

// Forward declarations only: CTarget stores a CUnit* and holds a CSprite* it never
// dereferences. Not includes, because this header is pulled into the generated TUs, which
// span both trees, and every include here is avoidable collision surface.
class CUnit;
class CSprite;

// CTarget -- 0x60 bytes. Only the fields a ported body reads are named; the rest is held
// at true size so those land where 1.17 puts them, the same convention as
// src117/h/CUnit.h's.

//   +0x58  wOrderTargetX  `movsx eax, word ptr [ecx+58h]` in CBullet::Order --
//   +0x5A  wOrderTargetY  `movsx eax, word ptr [ecx+5Ah]`. Two bytes each, and signed:
//                    movsx and not movzx, so an unsigned field loses 1.17's form.
//                    Corroborated from the write side by SetTargetOrder_xy's WORD stores.

//   +0x5C  pOrderTarget   a DWORD tested against zero in CBullet::Order, so a pointer.
//                    The pointee is CUnit: SetTargetOrder_CUnit stores the slot from EDX
//                    and then reads `[edx+0Ch]` and that sprite's posX/posY pair, and only
//                    a CUnit-like thing has a CSprite at +0x0C.

// The extent, 0x60, needs three facts, a base class's size being observable from no single
// instruction: no CTarget method in the corpus touches past +0x5C; CUnit's own first field
// past that point is +0x60 lShields; and CBullet's is also +0x60 and a different thing, the
// weapon-type byte. Two derived classes beginning their own data at 0x60 and giving that
// offset incompatible meanings puts 0x60 outside the shared base.

// The first 0x58 bytes are split only where a ported body reads them.

//   +0x00  pPrev     the CLink node pair, the two halves resting on different amounts of
//   +0x04  pNext     evidence. pNext is exercised: CBullet::RemoveTargetReferences walks
//                    the whole active-bullet list with `mov eax, dword ptr [eax+4h]`.
//                    pPrev is inferred and not exercised -- no body here reads it -- and
//                    named from the node contract CLink::Unlink states by clearing both
//                    dwords. The pointee is spelled CTarget *, this being the base's own
//                    field, and the one walker we have downcasts at the loop step, which
//                    costs no instruction.

//   +0x4C  bPlayer   the owning player. lurkerspines_apply_splash_damage_cb compares it
//                    byte to byte against a CUnit's own +0x4C, so it is one byte and its
//                    sign is unobservable. Both derived classes carry a player byte there,
//                    which is what a shared base predicts.

//   +0x0C  pSprite   a DWORD null-tested in CBullet::DispatchFatal, so the width is
//                    proven and the name is not: a null test cannot tell a sprite pointer
//                    from any other. The name is carried from src117/h/CUnit.h, which
//                    derives pSprite at the same offset from bodies that dereference it.

//   +0x4D  bOrderId  the one field in this block that is CTarget's own beyond doubt,
//                    because a CTarget method writes it: SetTargetOrder_CUnit and
//                    SetTargetOrder_xy both store their first argument there. The read
//                    side, CBullet::RunOrders, uses `movzx eax, byte ptr [ecx+4Dh]`
//                    directly as a dense switch index 0..6, so one byte and unsigned.

// What is still not derived: CTarget almost certainly has a base of its own, both CUnit and
// CBullet sharing +0x00/+0x04 and +0x0C, which is the CThingy shape. pSprite is named at
// the CTarget level because that is the lowest class this tree has, not as a claim about
// where Blizzard declared it. The remaining spans stay unmapped; split them one field and
// one reader at a time, not on a guess.
class CTarget {
public:
  /*0x00*/ CTarget *pPrev;   // inferred from the node contract, not exercised
  /*0x04*/ CTarget *pNext;
  /*0x08*/ BYTE     _unmapped_08[0x0C - 0x08];
  /*0x0C*/ CSprite *pSprite;
  // Hyperion: CFlingy's move target, positions of 4 bytes on x64 (h/pos.h, stage E)
  /*0x10*/ spos_t   aFlingyMoveTarget[2];
  // Hyperion: CFlingy's pMoveTargetUnit and CUnit's _unmapped_14_width, a
  // pointer, so that every field after it falls where theirs do on x64 as on x86: a
  // bullet is also read as a CFlingy, and as a CUnit through gpImageTargeter
  //. The asserts are in CTarget.cpp.
  union {
    /*0x14*/ BYTE   _unmapped_14[0x04];
    /*0x14*/ CUnit *_unmapped_14_width;
  };
  // Hyperion: CFlingy's next waypoint, destination, movement bytes and position,
  // its positions of 4 bytes on x64 as CFlingy's are (stage E)
  /*0x18*/ spos_t   aFlingyWaypoints[4];
  /*0x20*/ BYTE     _unmapped_20[0x28 - 0x20];
  /*0x28*/ spos_t   aFlingyPosition[2];
  /*0x2C*/ LONG     lPositionX256;
  /*0x30*/ LONG     lPositionY256;
  /*0x34*/ BYTE     _unmapped_34[0x4C - 0x34];
  /*0x4C*/ BYTE     bPlayer;
  /*0x4D*/ order_id_t bOrderId;  // Hyperion: 16 bits on x64, as CUnit's
  /*0x4E*/ BYTE     bOrderState;
  /*0x4F*/ BYTE     bOrderFlags;
  /*0x50*/ WORD     wOrderUnitType;
  /*0x52*/ BYTE     _unmapped_52[0x54 - 0x52];
  /*0x54*/ DWORD    dwOrderUnion54;
  /*0x58*/ spos_t wOrderTargetX;
  /*0x5A*/ spos_t wOrderTargetY;
  /*0x5C*/ CUnit *pOrderTarget;

  // Callees only: declared for the call, with no body of ours.
  //
  // SetTargetOrder_CUnit -- `ret 8h`, two stack arguments callee-cleaned plus the register
  //   `this`. The first is a BYTE stored to +0x4D, the second the CUnit* derived above.
  //
  // SetTargetOrder_xy -- `ret 0Ch`, three stack arguments. Same BYTE order id first; the
  //   other two land in +0x58 and +0x5A.
  //
  //   The coordinate parameters are 32-bit, and the caller's bytes are what prove it. The
  //   callee reads them `mov ax, word ptr [ebp+0Ch]`, which reads like a `short`, and with
  //   a `short` parameter the upper half of the slot is dead, so MSVC widens with movzx at
  //   CBullet::Order's read; only a 32-bit parameter makes the sign extension required and
  //   forces 1.17's movsx. The callee's own listing cannot settle this one.
  void SetTargetOrder_CUnit(order_id_t bOrder, CUnit *pTarget);
  void SetTargetOrder_xy(order_id_t bOrder, int x, int y);

  // oracle 0x417AA0 (40 B), a callee only, with no body of ours.
  //
  // `ret 0Ch` plus `this` in ECX, and all three arguments are read as full dwords -- a
  // 16-bit parameter would have loaded `mov ax, word ptr`. Both coordinates are shifted
  // left by 8, the fixed-point x256 convention, so the caller passes pixel coordinates and
  // this scales them itself. It returns 0/1 off the compare: true when nRadius is at least
  // the veclen distance from (+0x2C, +0x30) to (x, y).
  BOOL XYInRadius(int nRadius, int x, int y);

  // oracle 0x417980 (46 B), a callee only. `ret 4h` plus `this` in ECX: the argument is
  // stored to +0x5C, and then, when it is non-null, the target's sprite position is copied
  // into +0x58/+0x5A; the null path zeroes that dword pair instead. Void: no exit
  // materialises a value and every referent discards EAX.
  void SetFocusTarget(CUnit *pTarget);

  // 0x417960 (27 B) and 0x417940 (25 B): thiscall, `ret 4h` with the one stack
  // word never read, EAX only inherited. CUnit::EncodeToSave is the caller.
  void PreSave(BOOL bLinked);
  void PostLoad(BOOL bLinked);

  // 0x4178D0 (5 B), a bare `jmp CFlingy::BitSwap`: nullary thiscall, its chain
  // of thunks bottoming out at 0x4BE950's one-byte `ret`.
  void BitSwap();

  // 0x4178E0 (90 B). Forwards all five slots unchanged to CFlingy::Init, whose
  // types they therefore are, and returns its BOOL; both callers read EAX.
  BOOL Create(int nFlingyID, spos_t x, spos_t y, int nPlayer, int nDirection);  // Hyperion: positions

  // 0x4179B0 (37 B), `ret 4h`, arg0 read one byte wide. Returns nothing: all
  // three callers discard EAX.
  void SetTargetOrder(order_id_t bOrder);

  // 0x4179E0 (48 B), `ret 10h`. The coordinates are 32-bit because
  // CUnit::ManageQueuedOrders widens both with `movsx eax, bx` before pushing.
  void SetTargetOrder_fogged_xy(order_id_t bOrder, int nUnitType, int x, int y);
};

// The machine check on the offsets above. Compile-time only: these emit no code
// and cannot perturb a byte-match.
OC_X86_LAYOUT(sizeof(CTarget)                  == 0x60, "CTarget size           (both derived classes' own data starts at 0x60)");
static_assert(offsetof(CTarget, pPrev)         == 0x00, "CTarget::pPrev         [ecx]      -- the CLink node contract (INFERRED)");
OC_X86_LAYOUT(offsetof(CTarget, pNext)         == 0x04, "CTarget::pNext         [eax+4h] dword -- RemoveTargetReferences' walk");
OC_X86_LAYOUT(offsetof(CTarget, pSprite)       == 0x0C, "CTarget::pSprite       [esi+0Ch] dword");
OC_X86_LAYOUT(offsetof(CTarget, bPlayer)       == 0x4C, "CTarget::bPlayer       [edi+4Ch] byte");
OC_X86_LAYOUT(offsetof(CTarget, bOrderId)      == 0x4D, "CTarget::bOrderId      [ecx+4Dh] movzx byte");
OC_X86_LAYOUT(offsetof(CTarget, lPositionX256) == 0x2C, "CTarget::lPositionX256 XYInRadius: `mov ecx, [ecx+2Ch]` -> veclen's ax");
OC_X86_LAYOUT(offsetof(CTarget, lPositionY256) == 0x30, "CTarget::lPositionY256 XYInRadius: `push dword ptr [ecx+30h]` -> veclen's ay");
OC_X86_LAYOUT(offsetof(CTarget, bOrderState)   == 0x4E, "CTarget::bOrderState   [ecx+4Eh] byte -- zeroed by all four setters");
OC_X86_LAYOUT(offsetof(CTarget, bOrderFlags)   == 0x4F, "CTarget::bOrderFlags   [esi+4Fh] byte -- Create zeroes it");
OC_X86_LAYOUT(offsetof(CTarget, wOrderUnitType)== 0x50, "CTarget::wOrderUnitType[ecx+50h] word -- the 0E4h sentinel");
OC_X86_LAYOUT(offsetof(CTarget, dwOrderUnion54)== 0x54, "CTarget::dwOrderUnion54[esi+54h] dword -- Create zeroes it");
OC_X86_LAYOUT(offsetof(CTarget, wOrderTargetX) == 0x58, "CTarget::wOrderTargetX [ecx+58h] movsx word");
OC_X86_LAYOUT(offsetof(CTarget, wOrderTargetY) == 0x5A, "CTarget::wOrderTargetY [ecx+5Ah] movsx word");
OC_X86_LAYOUT(offsetof(CTarget, pOrderTarget)  == 0x5C, "CTarget::pOrderTarget  [ecx+5Ch] dword");

// veclen -- oracle 0x405CF0 (77 B), the distance primitive XYInRadius above is documented
// against. It belongs to no class, so it sits here.
//
// __fastcall with ECX = ax, EDX = bx and two pushed dwords; the pushes are right to left,
// so the first is `by` and the second `ay`, and the caller pairs them (ax, ay) and
// (bx, by). The distance comes back in EAX, consumed 32 bits wide.
//
// The `extern "C"` matters: the oracle symbol is `@veclen@16`, the C fastcall decoration,
// and a C++-linkage declaration mangles to something else and dies at the payload link.
// The two-declaration shape is CBullet.h's treatment of find_first_unit_near -- the
// `extern "C"` line gives the name C linkage, and the plain redeclaration is what the
// thunk generator parses the ABI out of.
extern "C" int __fastcall veclen(int ax, int bx, int ay, int by);
// Parser-visible redeclaration for the thunk generator; C linkage is inherited
// from the declaration above, so both decorate to `@veclen@16`.
int __fastcall veclen(int ax, int bx, int ay, int by);

// veclen_precise -- oracle 0x405C90 (93 B), the finer distance estimate
// CFlingy::InterceptTarget calls. Same contract and same C decoration
// (`@veclen_precise@16`) as veclen above: ECX/EDX and two dwords, `ret 8h`.
extern "C" int __fastcall veclen_precise(int ax, int bx, int ay, int by);
int __fastcall veclen_precise(int ax, int bx, int ay, int by);
