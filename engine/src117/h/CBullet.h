// CBullet.h -- the bullet object and the free functions of src117/lang/CBullet.cpp:
// the splash-damage callbacks, the lurker-spine hit log's entry type and the
// bounce search's filter. Members belong to their class; only that TU's
// namespace-scope functions are declared loose at the bottom.
//
// The header owns the CBullet.h basename on purpose. Generated code resolves a TU
// and a class to a header by that basename over an include path that also reaches
// the quarantined reference tree, and hook_table.cpp names `&CBullet::Order` and
// the callbacks directly, so each needs a declaration of ours in scope.
// src117/h/CUnitMisc.h and src117/h/CTarget.h exist for the same reason.
#pragma once

#include "platform.h"   // ours: BYTE/WORD/BOOL + <stddef.h> for offsetof
#include "CTarget.h"    // ours: the order-state base, which sits at offset 0

class CUnit;

// The lurker-spine hit log's entry: who fired, and who was hit. Lurker-spine
// splash damage is applied by an area walk that can present the same victim to
// the same bullet more than once, so the callback scans all 512 (attacker, victim)
// pairs and bails when the pair is already listed. The stride is 8 and both halves
// are DWORD, stated outright by that scan's `add eax, 8h`. The table itself,
// sgTracked_g @0x540768, and its two cursors are declared in the .cpp.
struct TrackedHit {
  /*0x00*/ CUnit *pAttacker;
  /*0x04*/ CUnit *pVictim;
};

OC_X86_LAYOUT(sizeof(TrackedHit)              == 0x08, "TrackedHit size       (add eax,8h stride / [eax*8+...])");
static_assert(offsetof(TrackedHit, pAttacker) == 0x00, "TrackedHit::pAttacker [eax]");
OC_X86_LAYOUT(offsetof(TrackedHit, pVictim)   == 0x04, "TrackedHit::pVictim   [eax+4h]");

// 32 buckets of 16 slots, 512 entries in all: the scan's own `cmp ecx, 200h`, the
// 0x1000-byte memset CBullet::InitBulletTracking passes and the next global
// starting at 0x540768 + 0x1000 all agree. Both factors are named because
// CBullet::NextBucket needs them apart: it wraps on the bucket count and clears
// one bucket's worth. They are constants rather than sizeof(sgTracked_g), which
// in the payload is a pointer and would make every length 4.
const int kTrackedBuckets   = 32;   // NextBucket's `cmp eax, 20h`
const int kTrackedPerBucket = 16;   // NextBucket's `push 80h` of 8-byte entries,
                                    // and the append path's `cmp ecx, 10h` cap
const int kTrackedSlots     = kTrackedBuckets * kTrackedPerBucket;   // 512

// CBullet -- a fired weapon in flight, derived from CTarget, whose base sits at
// offset 0. 0x70 bytes: CBullet::InitBullets memsets 0x2BC0 bytes at 0x53DB90 and
// links 100 elements with `add esi, 70h`, and 100 * 0x70 is that length exactly.
class CBullet : public CTarget {
public:
  // The fields a body of ours reads. +0x62 bit 0 is set on the arm of Init that
  // fires at a unit target and read as "do not re-aim"; CUnit::DamageFromBullet
  // tests bit 1. +0x6D to +0x6F is unmapped and held at its true size so that
  // sizeof(CBullet) is the extent the oracle proves.
  /*0x60*/ weapon_id_t bWeaponType;      // the weapons.dat row it was fired from
  /*0x61*/ BYTE   bTimeRemaining;    // frames left, seeded per weapon, counted down per frame
  /*0x62*/ BYTE   bFlags62;          // flags, two bits of them read
  /*0x63*/ BYTE   bBounceRemaining;  // bounces left; DispatchBounce decrements it byte-wide
  /*0x64*/ CUnit *pAttacker;         // the unit that fired it, nulled when that unit goes
  /*0x68*/ CUnit *pUnion68;          // the unit it was aimed at on the previous bounce
  // The row of sgSpreadVariance_g this bullet was offset by: Init stores it and
  // DispatchHitNear reads it back to index the same 8-byte table.
  /*0x6C*/ BYTE   bSpreadIndex;
  /*0x6D*/ BYTE   _unmapped_6D[0x70 - 0x6D];

  // 0x40A3D0. Re-issues the bullet's current target as a fresh target order under
  // a new order id -- by unit while it still has a live target unit, otherwise by
  // the remembered ground point. `ret 4h`, and the order id is a BYTE: it is
  // forwarded to CTarget::SetTargetOrder_{CUnit,xy}, which store it into CUnit.h's
  // `/*0x4D*/ BYTE bOrderId`, and every call site pushes a literal from 1 to 8.
  void Order(order_id_t bOrder);

  // 0x40A880. Ages the lurker-spine hit log by one frame: advances the ring cursor
  // (31 wraps to 0), wipes the 16 entries the cursor now points at, and resets the
  // fill level. Static: no register is read on entry, the body ends on a bare
  // `ret`, and its one caller, CUnit::EntityUpdate, sets up no ECX for it.
  static void NextBucket();

  // 0x409950. The terminal step of a bullet's lifetime: its sprite is already torn
  // down, so it is unlinked from the active-bullets list, prepended to the free
  // pool, and the active count drops. __thiscall, nullary, void. Our two name
  // sources disagree on this row; the other one calls it CBullet::DispatchDie.
  void DispatchFatal();

  // 0x40A6D0. The bullet's per-frame order dispatch: reads its own
  // CTarget::bOrderId and jumps to that order's handler through a 7-entry table at
  // 0x40A704. The range check is unsigned and there is no default arm, so an id
  // outside 0 to 6 falls out of the switch and the body returns having done
  // nothing. __thiscall, nullary, void.
  void RunOrders();

  // The six other arms of RunOrders's jump table, in the table's index order:
  //   [0] OrderInit  [1] DispatchFly  [2] DispatchFollow  [3] DispatchBounce
  //   [4] DispatchDamageOverTime  [5] DispatchFatal (above)  [6] DispatchHitNear
  // All __thiscall with no arguments. OrderInit and DispatchHitNear have no body
  // of ours and resolve through the generated thunk.
  void OrderInit();
  void DispatchFly();
  void DispatchFollow();
  void DispatchBounce();
  void DispatchDamageOverTime();
  void DispatchHitNear();

  // 0x40A5A0. Severs every in-flight bullet's references to a unit that is going
  // away: a bullet still homing on it is turned to home on the ground square the
  // unit occupies, and a bullet that names it as the shooter forgets the shooter.
  // Static __fastcall: ECX is the unit and not a `this`, since it is what every
  // bullet in the list is compared against, and the bare `ret` cleans no stack.
  static void __fastcall RemoveTargetReferences(CUnit *pUnit);

  // -- declared for the call, with no body of ours --------------------------

  // 0x40A9F0, this TU, unnamed in every source we may read, so it keeps its
  // address name. __thiscall with two stack slots (`ret 8h`): the second argument
  // is a CUnit *, dereferenced at its sprite and its wType, and the first is a
  // plain int range, compared full width against a computed vector length even
  // though both call sites feed it a widened WORD. The result is a 0 or a 1.
  BOOL sub_40A9F0(int nRange, CUnit *pUnit);

  // 0x40C520. Applies this bullet's damage. __thiscall, nullary and void:
  // CBullet::DispatchDamageOverTime tail-jumps into it with nothing pushed.
  void Damage();

  // -- declared for src117/lang/CUnitCombat.cpp's calls ----------------------

  // 0x408F00. Takes a bullet off the free pool and fires it, or returns null when
  // none was free. Static __fastcall with six arguments: ECX is the weapon id,
  // read as an integer (`cmp ecx, 67h`), EDX the x, and `ret 10h` cleans four
  // stack words -- the y, the player, CUnit::bUnion21 passed straight through, and
  // the attacker, dereferenced at dwFlags. It is declared here and not in the
  // calling TU because a thunk for a member callee includes only its class header.
  static CBullet *__fastcall AllocBullet(int nWeaponId, int nX, int nY, int nPlayer, int nUnion21, CUnit *pAttacker);

  // 0x409010, unported, and declared here for the reason AllocBullet gives.
  // __thiscall, `ret 4h`. The argument is dereferenced at wType and lHitPoints, so
  // it is a CUnit *, and every exit ends `shl eax, 8h`, so the damage comes back
  // in the 8.8 fixed point CUnit::lHitPoints uses. CUnit::DamageFromBullet feeds
  // the result straight in as CUnit::Damage's first argument.
  int sub_409010(CUnit *pUnit);

  // 0x40A0E0. Memset the 100-entry pool and link it free. Static and nullary:
  // neither call site loads ECX, and both discard EAX.
  static
  void InitBullets();

  // 0x40A150. Clear the 512-entry tracking table, the 0x1000-byte memset cited
  // above. Static and nullary; its one caller discards EAX.
  static
  void InitBulletTracking();

  // 0x40A5F0. Step every live bullet once per frame. Static and nullary: its one
  // call site loads no ECX and discards EAX.
  static
  void Update();

  // 0x409D70. `ret 18h`: the weapons.dat id, the spawn point, the player, the
  // direction and the firing unit, forwarded from AllocBullet.
  BOOL Init(int nWeaponId, int nX, int nY, int nPlayer, int nDirection,
            CUnit *pAttacker);

  // 0x409BB0. ECX nonzero turns pointers into 1-based pool ids before a save, zero
  // turns them back after a save or a load. noinline: SaveBullets calls it twice
  // and the second call would otherwise be inlined.
  static
  __declspec(noinline) void EncodeDecodeListForSave(BOOL bEncode);

  // 0x40A1C0, 0x40A210, 0x40A2F0 and 0x40A720. ECX is the save file,
  // forwarded to pfread/pfwrite; LoadBullets' EDX is the save version, read
  // 16-bit (`mov si, dx` / `cmp si, 2h`). Each returns the BOOL its callers test.
  static
  BOOL LoadBullets(void *pFile, WORD wVersion);
  static
  BOOL LoadBulletsNew(void *pFile);
  static
  BOOL LoadBulletsOld(void *pFile);
  static
  BOOL SaveBullets(void *pFile);

  // 0x40A400. Rebuilds the used and free lists after a load.
  static
  void RecreateBulletList();
};

// The machine check on the layout above; these emit no code.
OC_X86_LAYOUT(sizeof(CBullet)                    == 0x70, "CBullet size             (InitBullets `add esi,70h` stride / 100 * 0x70 == 0x2BC0 memset)");
OC_X86_LAYOUT(offsetof(CBullet, bWeaponType)     == 0x60, "CBullet::bWeaponType     [edi+60h] byte");
OC_X86_LAYOUT(offsetof(CBullet, bTimeRemaining)  == 0x61, "CBullet::bTimeRemaining  [esi+61h] byte");
OC_X86_LAYOUT(offsetof(CBullet, bFlags62)        == 0x62, "CBullet::bFlags62        [esi+62h] test byte");
OC_X86_LAYOUT(offsetof(CBullet, bBounceRemaining)== 0x63, "CBullet::bBounceRemaining [esi+63h] dec byte");
OC_X86_LAYOUT(offsetof(CBullet, pAttacker)       == 0x64, "CBullet::pAttacker       [edi+64h] dword");
OC_X86_LAYOUT(offsetof(CBullet, pUnion68)        == 0x68, "CBullet::pUnion68        [esi+68h] dword");
OC_X86_LAYOUT(offsetof(CBullet, bSpreadIndex)    == 0x6C, "CBullet::bSpreadIndex    [esi+6Ch] byte");

// The namespace-scope functions of src117/lang/CBullet.cpp.

// 0x409590 and 0x4096C0. The two find_foreach_unit callbacks that pick a splash
// damage mode: each stores it to the flag at 0x541770 and jumps into the shared
// apply_splash_damage, forwarding both register arguments untouched, so both carry
// that function's __fastcall(CUnit *, CBullet *) signature. The names are ours:
// the ground path awards the inner ring's full damage inline and sets the flag,
// the air path's pass 2 runs after a pass 1 that has awarded it and clears it.
int __fastcall bullet_apply_ground_splash_damage_cb(CUnit *pUnit, CBullet *pBullet);
// 0x4096D0, unported: pass 1 of the air splash, which lists the inner-ring hits
// in dword_541774 and dword_541778 for CBullet::Damage.
int __fastcall bullet_apply_air_splash_pass1_cb(CUnit *pUnit, CBullet *pBullet);
int __fastcall bullet_apply_air_splash_pass2_cb(CUnit *pUnit, CBullet *pBullet);

// 0x40A9E0, unnamed in every source we may read, so it keeps its address name.
// The whole body is `mov dword ptr [540760h], 0h / ret`: nothing is pushed and no
// register is read at either call site, so it takes no argument. It forgets which
// unit last fired a spread weapon, so the next CBullet::Init re-rolls the spread
// side instead of alternating it.
void sub_40A9E0();

// 0x409770. A find_foreach_unit callback for the lurker spine's damage sweep, and
// the name our name sources carry for it. __fastcall with two register arguments
// and no stack slots: ECX is the unit the walk found, EDX the bullet (+0x60 weapon
// type, +0x5C target, +0x64 attacker). It always returns 0, which
// find_foreach_unit reads as "keep iterating", and CBullet::Damage picks between
// it and the ground callback with a cmovz.
int __fastcall lurkerspines_apply_splash_damage_cb(CUnit *pUnit, CBullet *pBullet);

// 0x40A820. The filter CBullet::DispatchBounce hands find_first_unit_near when it
// picks a bullet's next bounce victim, under the name our name sources carry. The
// only incoming value it consumes is ECX, the candidate unit; EDX is unused, and
// the verdict comes back in EAX as a 0 or a 1.
int __fastcall cb_bullet_bounce_proc(CUnit *pUnit);

// 0x4095A0, unported. Declared here so the generated thunk takes its signature
// from a header that thunk can include; a declaration found only inside a TU
// leaves that TU's types out of reach. Both splash callbacks tail into it with
// ECX a CUnit * and EDX a CBullet *, and it returns int in EAX.
int __fastcall apply_splash_damage(CUnit *pUnit, CBullet *pBullet);

// 0x442B20, unported: the proximity search CBullet::DispatchBounce uses to pick a
// bullet's next bounce victim. (x, y, radius, filter, unused) gives the chosen
// unit, or null when nothing passes the filter, which DispatchBounce null-tests
// and stores into CBullet::pOrderTarget. The filter is cb_bullet_bounce_proc
// above, and its typedef sits here beside it because every type a thunked
// declaration names has to be reachable from a header too.
typedef int(__fastcall *pfn_bounce_filter_t)(CUnit *pUnit);

extern "C" CUnit *__fastcall find_first_unit_near(int x, int y, int nRadius,
                                         pfn_bounce_filter_t pfnFilter,
                                         intptr_t nUnused);
// The same prototype again, in the plain form the thunk generator reads the ABI
// from; the declaration above gives it C linkage. CUnitTSpells.h carries a second
// source view of it, and both decorate to `@find_first_unit_near@20`.
CUnit *__fastcall find_first_unit_near(int x, int y, int nRadius,
                             pfn_bounce_filter_t pfnFilter,
                             intptr_t nUnused);
