// CUnitCombat.cpp -- the unit-combat block: the order-dispatch steps, the
// damage-notification hook, the three ways this engine fires a weapon, the "how
// much can this unit take" accessors and their units.dat counterparts, the weapon
// column lookups, the hold-order map, the unit-state and targeting predicates,
// the kill-credit counter and kill-score lookup, and the unit-type vulnerability
// predicate. Bodies are in ascending oracle address, 1.17's own order.
//
// That order is not cosmetic. CUnit::CanTargetGroundType (0x41DB90) matches only
// because /Ob2 inlines IsVulnerableType (0x420A90) into it, which MSVC does
// backwards, deferring codegen to the end of the TU; spelling the guard out by
// hand measures 81 B against 85. CUnit::sub_420AB0 and CUnit::IsUnitInWeaponRange
// depend the same way on CUnit::GetWeaponGndEx above them, and must call it.

#include "../h/playervision.h"  // Hyperion: the slots past 7
#include "../h/hxrules.h"       // Hyperion: HxRules

// Include order matters. src117/gen/globals117.h is generated with no includes of
// its own and relies on its includer having BYTE, DWORD and its rows' struct
// types in scope, so it comes last; net_cmds.h comes before it, because
// `extern PlayerData gPlayerData[8];` needs that type complete.
#include "oc_data.h"  // Hyperion: OC_VA
#include <intrin.h>
#include "../h/CUnitCombat.h"    // ours: this TU's own header -- IsVulnerableType,
                                 // sub_421370 and the free callees these bodies
                                 // reach
#include "../h/SAI_Main.h"       // ours: ai_update_unit_spells

#include "../h/CUnit.h"          // ours: the unit fields these bodies read and
                                 // the CUnit members they call

#include "../h/CFlingy.h"        // ours: the destination pair at +0x1C/+0x1E,
                                 // CFlingy::SetDestination and
                                 // CFlingy::BreakingDistance -- callees this TU
                                 // reaches through a cast rather than through
                                 // CUnit

#include "../h/CSprite.h"        // ours: wPosX (+0x14) / wPosY (+0x16), the
                                 // sprite position CUnit::DispatchTowerTarget
                                 // hands to CUnit::SetMoveTarget_xy. The two
                                 // spellings differ: wPosX is WORD with its sign
                                 // undiscriminated and wPosY is short, so 1.17's
                                 // `movsx` on both coordinates needs an explicit
                                 // (short) on the X and nothing on the Y

#include "../h/unittypes.h"      // ours: UNI_T_QUEEN / UNI_T_MATRIARCH, the two
                                 // units.dat ids CUnit::IsQueenType tests. The
                                 // one home for a type id, which CUnit.h
                                 // deliberately does not pull in, so a consumer
                                 // includes it directly

#include "../h/CBullet.h"        // ours: CBullet::AllocBullet, the one callee
                                 // CUnit::FireWeapon reaches outside CUnit

#include "../h/net_cmds.h"       // ours: PlayerData, gPlayerData's element type,
                                 // which CUnit::DispatchAttackUnit indexes by
                                 // player and whose nType it compares against 1

#include "../h/CUnitSearches.h"  // ours: GetRegion_CUnit, the first half of
                                 // CUnit::DispatchAttackMoveEP's region
                                 // comparison; its partner GetRegion_xy is in
                                 // SAI_Banker.h below, each header owning what
                                 // its own oracle TU emits

#include "../h/SAI_Banker.h"     // ours: GetRegion_xy

#include "../h/TrigAct.h"        // ours: net_rand, the cooldown jitter source
                                 // CUnit::sub_41F530 reaches

#include "../h/CUnitTSpells.h"   // ours: net_randbetween, the 0..15 roll
                                 // CUnit::DispatchGuard stores into bUnion54
#include "../h/CUnitZSpells.h"   // ours: CThingy::AllocThingy, the 0x176 effect
                                 // CUnit::DispatchDie spawns before it destroys
                                 // the unit
#include "../h/TrigEnum.h"       // ours: get_map_height (0x445690), the two
                                 // terrain heights CUnit::GetChanceToHit
                                 // compares for the high-ground miss roll
#include "../h/saveload.h"       // ours: get_upgrade_level (0x49A470), the
                                 // per-player upgrade tier both damage display
                                 // bodies multiply their bonus by

#include "../h/rect16.h"         // ours: rect16_t, the unit-search rectangle
                                 // OnDamaged builds around its own sprite for
                                 // get_units_in_rect, assigned left, right, top
                                 // and bottom in stream order -- see that body
#include "../h/order.h"          // ours: get_units_in_rect (0x422330), which
                                 // OnDamaged walks for the same-player
                                 // notification pass
#include "../h/CUnitFind.h"      // ours: finish_unit_iteration (0x4222B0), which
                                 // releases the list the call above returned

#include "../h/TAI_GuardPost.h"   // ours: guard_is_at_post
#include "../h/TAI_AreaStrength.h"  // ours: the two threat searches
#include "../h/damage.h"          // compute_damage_from_types
#include "../h/COrder.h"          // COrder::bOrderId, DispatchPatrol's queue test
#include "../h/statbtn.h"         // TechUseAllowed
#include "../h/Allowed.h"         // CUnit::IsDisabled's inline body
#include "../h/TAI_Captain.h"     // tai_cloak_if_allowed
#include "../h/CTarget.h"         // CTarget::XYInRadius
#include "../h/CImage.h"          // CImage::GetOverlayOffset, shield_offset_t
#include "../h/sai_influence.h"   // LineClip
#include "../h/collide.h"         // mtx_flags

#include "../gen/globals117.h"
#include "../h/WeaponGndEx.h"    // CUnit::GetWeaponGndEx's inline body
#include "../h/typelimits.h"  // Hyperion: a mod's types
#include "../h/handicap.h"  // Hyperion: a player's handicap

// 0x536720, the expansion gate; unregistered, spelled as gametype.cpp does.
#ifndef gbIsExpansion
#ifdef OC_DLL_MODE
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbIsExpansion;
#endif
#endif

// 0x51B5C0, an orders.dat byte column; unregistered, the sheet's name.
#ifndef order_unk11
#ifdef OC_DLL_MODE
#define order_unk11 (reinterpret_cast<BYTE *>(OC_VA(0x51B5C0u)))
#else
extern BYTE order_unk11[189] OC_UNALIGNED;
#endif
#endif

// gubUnitWeaponGnd @0x5211D8 and wpn_gfx_g @0x524F18, two weapon-table globals
// the generated header does not carry yet: spelled here in the same dual
// `#ifdef OC_DLL_MODE` form it uses, each behind its own `#ifndef` so a name
// that lands later takes over, and with the sheet's names.
#ifndef gubUnitWeaponGnd
#ifdef OC_DLL_MODE
#define gubUnitWeaponGnd (reinterpret_cast<BYTE*>(OC_VA(0x5211D8u)))
#else
extern weapon_id_t gubUnitWeaponGnd[228] OC_UNALIGNED;
#endif
#endif

#ifndef wpn_gfx_g
#ifdef OC_DLL_MODE
#define wpn_gfx_g (reinterpret_cast<DWORD*>(OC_VA(0x524F18u)))
#else
extern DWORD wpn_gfx_g[130] OC_UNALIGNED;
#endif
#endif

// gpImageUnit @0x543300 and gpImageTargeter @0x543304, adjacent CImage.cpp
// pointers that CUnit::BreakAnim below swaps to `this` around
// CFlingy::StopAnim and puts back. They stay spelled here rather than
// recorded, because CImage.cpp, the TU that owns them, still resolves headers
// out of the reference tree, where a recorded global collides as a C2371.
// src117/lang/CUnitTerran.cpp spells gpImageUnit at this VA character for
// character, which keeps the two from becoming a C2369 in one link.
#ifndef gpImageUnit
#ifdef OC_DLL_MODE
#define gpImageUnit (*reinterpret_cast<CUnit **>(OC_VA(0x543300u)))
#else
extern CUnit *gpImageUnit;
#endif
#endif

#ifndef gpImageTargeter
#ifdef OC_DLL_MODE
#define gpImageTargeter (*reinterpret_cast<CUnit **>(OC_VA(0x543304u)))
#else
extern CUnit *gpImageTargeter;
#endif
#endif

// wpn_outersplash_g @0x525B78 -- weapons.dat's outer splash radius, 130 WORDs.
// Three other bodies index it with a register, `movzx <r>, word ptr
// [<r>*2 + 525B78h]`, so the element is an unsigned two bytes and this address
// is the base. CUnit::DispatchSapLocation and CUnit::DispatchSapUnit read it at
// the constant 0x525BE4, which is element 54, the Infested Terran suicide
// weapon: one array element, not a global of its own.
#ifndef wpn_outersplash_g
#ifdef OC_DLL_MODE
#define wpn_outersplash_g (reinterpret_cast<WORD*>(OC_VA(0x525B78u)))
#else
extern WORD wpn_outersplash_g[130] OC_UNALIGNED;
#endif
#endif


// guwUnitScoreKill -- WORD[228] @0x524290, TU arr/units.cpp. units.dat's Kill
// Score column: the score credited for destroying a unit of this type. Element
// width 2 and scale 2 from its one referent, CUnit::KillScore below, and the
// extent is exact -- the next sheet name at 0x524458 is 456 bytes on, 228 times
// 2. The element is read unsigned, which 1.16.1's incrementUnitDeathScores
// corroborates by accumulating the same table into a running total.
#ifndef guwUnitScoreKill
#ifdef OC_DLL_MODE
#define guwUnitScoreKill (reinterpret_cast<WORD*>(OC_VA(0x524290u)))
#else
extern WORD guwUnitScoreKill[228] OC_UNALIGNED;
#endif
#endif

// gxUnitLife -- long[228] @0x520708, TU arr/units.cpp. units.dat's Hit Points
// column in 8.8 fixed point, one long per unit type. The element is `long`
// rather than DWORD because both readers shift it with `sar` and not `shr`:
// CUnit::GetLife (0x409C90) in CBullet.cpp, and CUnit::MaxToughness below,
// whose `mov eax, dword ptr [eax*4+520708h]` also gives the element width.
#ifndef gxUnitLife
#ifdef OC_DLL_MODE
#define gxUnitLife (reinterpret_cast<LONG*>(OC_VA(0x520708u)))
#else
extern LONG gxUnitLife[228] OC_UNALIGNED;
#endif
#endif

// wpn_wpnType_g -- BYTE[] @0x525748, TU arr/weapons.cpp. A weapons.dat byte
// column indexed by an unbiased weapon id: every referent loads a BYTE with no
// scale factor, so this address is the table base. The extent is not claimed,
// storage to the next sheet name being 136 bytes, which does not factor as
// weapons.dat's 130 rows; an unsized extern compiles the same indexed load.
// CUnit::DamageNotify below reads a nonzero value as the last of three
// conditions gating the shield-impact effect.
#ifndef wpn_wpnType_g
#ifdef OC_DLL_MODE
#define wpn_wpnType_g (reinterpret_cast<BYTE*>(OC_VA(0x525748u)))
#else
extern BYTE wpn_wpnType_g[];
#endif
#endif

// gubUnitOrderAttackUnit -- BYTE[228] @0x521008, TU arr/units.cpp. units.dat's
// Attack Unit order column, one order id per unit type. Element width 1 and
// unbiased indexing from its single referent, CUnit::sub_4206E0 below; extent
// 228, units.dat's row count, under the sheet's own 232-byte ceiling.
#ifndef gubUnitOrderAttackUnit
#ifdef OC_DLL_MODE
#define gubUnitOrderAttackUnit (reinterpret_cast<BYTE*>(OC_VA(0x521008u)))
#else
extern order_id_t gubUnitOrderAttackUnit[228] OC_UNALIGNED;
#endif
#endif

// wpn_dmgUpg_g @0x5256C0 -- weapons.dat's damage-upgrade id, the row
// get_upgrade_level is asked about. Spelled unbounded on purpose, as
// wpn_wpnType_g above is: storage to the next sheet name is 136 bytes while
// weapons.dat has 130 rows, so that is storage and not the count, and no
// memset in the program covers this table. Width 1 and unbiased at all four
// referents, none of them scaled.
#ifndef wpn_dmgUpg_g
#ifdef OC_DLL_MODE
#define wpn_dmgUpg_g (reinterpret_cast<BYTE*>(OC_VA(0x5256C0u)))
#else
extern upgrade_id_t wpn_dmgUpg_g[];
#endif
#endif

// wpn_dmgBonus_g @0x525D88 -- weapons.dat's per-upgrade damage bonus, WORD[130].
// The extent comes from a memset the program itself executes: read_save_mapdata
// clears 0x104 == 260 bytes here, 130 times 2 with no padding, where the
// next-sheet-name subtraction would have given 0x218 and been wrong, an unnamed
// table sitting in between. Width 2 and scale 2 at four referents, read and
// write, indexed by an unbiased weapon id, so this address is the base.
#ifndef wpn_dmgBonus_g
#ifdef OC_DLL_MODE
#define wpn_dmgBonus_g (reinterpret_cast<WORD*>(OC_VA(0x525D88u)))
#else
extern WORD wpn_dmgBonus_g[130] OC_UNALIGNED;
#endif
#endif

// wpn_targetflag_g @0x5251A8 -- weapons.dat's target-class flag word, 130 WORDs.
// The stride to the next table, wpn_minrange_g @0x5252B0, is 0x108: 130 WORDs
// plus 4 bytes of padding, the same cadence as the three splash-radius tables,
// so the raw subtraction is the storage and not the count. Width 2 and unbiased
// from every referent.
#ifndef wpn_targetflag_g
#ifdef OC_DLL_MODE
#define wpn_targetflag_g (reinterpret_cast<WORD*>(OC_VA(0x5251A8u)))
#else
extern WORD wpn_targetflag_g[130] OC_UNALIGNED;
#endif
#endif

// gubWeapFireType -- BYTE[] @0x5257D0, TU arr/weapons.cpp. A weapons.dat byte
// column indexed by an unbiased weapon id, width 1 and unscaled at its one
// referent, CUnit::MakeBullet below. The extent is not claimed: storage to the
// next sheet name is 136 bytes, which does not factor as weapons.dat's 130 rows.
//
// MakeBullet reads it to pick where the bullet is spawned: 2 takes the position
// from CUnit::sub_420970 and needs an order target, 4 takes the unit's own
// order-target coordinates, and anything else projects a vector out of the
// unit's facing.
#ifndef gubWeapFireType
#ifdef OC_DLL_MODE
#define gubWeapFireType (reinterpret_cast<BYTE*>(OC_VA(0x5257D0u)))
#else
extern BYTE gubWeapFireType[];
#endif
#endif

// wpn_dmgAmt_g -- WORD[130] @0x525C80, TU arr/weapons.cpp. weapons.dat's base
// Damage Amount column, width 2 and scale 2 at its one referent,
// CUnit::GetDisplayBaseDamage below. The extent comes from a memset the program
// itself executes over 0x104 == 260 bytes, 130 times 2 with no padding, which is
// also what makes 0x82 the "no weapon" sentinel elsewhere in this TU.
#ifndef wpn_dmgAmt_g
#ifdef OC_DLL_MODE
#define wpn_dmgAmt_g (reinterpret_cast<WORD*>(OC_VA(0x525C80u)))
#else
extern WORD wpn_dmgAmt_g[130] OC_UNALIGNED;
#endif
#endif

// wpn_attackangle_g -- BYTE[] @0x525FA0, TU arr/weapons.cpp. A weapons.dat byte
// column indexed by an unbiased weapon id: the half-arc, in the engine's
// 256-unit angle, within which a weapon may fire without turning further. Width
// 1 and unscaled at its one referent, CUnit::IsInAttackAngle below, where the
// value is the right-hand side of a `setle` against a folded angle difference.
// The extent is not claimed.
#ifndef wpn_attackangle_g
#ifdef OC_DLL_MODE
#define wpn_attackangle_g (reinterpret_cast<BYTE*>(OC_VA(0x525FA0u)))
#else
extern BYTE wpn_attackangle_g[];
#endif
#endif

// wpn_forwardOffset_g -- BYTE[] @0x5260B0 and wpn_upwardOffset_g -- BYTE[]
// @0x526138, both TU arr/weapons.cpp: weapons.dat's two muzzle-offset columns,
// width 1 and unscaled at CUnit::MakeBullet's two reads, with neither extent
// claimed. It is the pair that shows what they are. The forward one is handed to
// GetVectorComponents as the distance along the unit's facing, moving the spawn
// point out along the barrel; the upward one is subtracted from the sprite's Y
// afterwards, straight up the screen whatever the facing.
#ifndef wpn_forwardOffset_g
#ifdef OC_DLL_MODE
#define wpn_forwardOffset_g (reinterpret_cast<BYTE*>(OC_VA(0x5260B0u)))
#else
extern BYTE wpn_forwardOffset_g[];
#endif
#endif

#ifndef wpn_upwardOffset_g
#ifdef OC_DLL_MODE
#define wpn_upwardOffset_g (reinterpret_cast<BYTE*>(OC_VA(0x526138u)))
#else
extern BYTE wpn_upwardOffset_g[];
#endif
#endif

// WPN_NONE -- weapons.dat's "no weapon" sentinel, 130 (0x82). Not a global and
// not a units.dat value: it is weapons.dat's row count used as a
// one-past-the-end marker, so wpn_minrange_g is only indexed when the id is
// strictly below it (CUnit::IsUnitInWeaponRange's unsigned `jnb`).
//
// It is spelled three ways in this file, each right where it sits: the bodies
// that hold it in a `char` say -0x7E, where the negative literal carries the
// sign into the compare; the bodies that hold it in a BYTE say WPN_NONE; and
// CUnit::AttackWith says 0x82, assigning into a BYTE. One byte, three types.
#define WPN_NONE 130

// gubUnitWeaponAir -- BYTE[228] @0x5213A8, TU arr/units.cpp. units.dat's "Air
// Weapon" column, one weapon id per unit type. Width 1 and unbiased indexing at
// all seventeen referents in the corpus: scale 1, a BYTE operand, and an
// unbiased `movzx` of the WORD wType as the index, so this address is the table
// base. Extent 228, units.dat's entry count, as the rest of this table family
// carries. The element is read signed, and that matters: the sentinel is 0x82
// and 1.17 tests it with `cmp al, -7Eh`. See GetWeaponAir below.

// gnWeapLaunch -- BYTE[] @0x525F18, TU arr/weapons.cpp. A weapons.dat byte
// column indexed by an unbiased weapon id, width 1 and unscaled at all six
// referents. The extent is not claimed: storage to the next sheet name is 136
// bytes, and weapons.dat's 130 rows fit under that ceiling without being
// implied by it.
//
// What its readers here agree on: the value 2 is a distinguished case meaning
// "fire the bullet twice", since FireSpell, AttackWith and FireWeapon all branch
// on `== 2` and duplicate a MakeBullet call under it, while GetDisplayBaseDamage
// and GetDisplayBonusDamage feed the same byte into a damage computation. A
// bullet-count or attack-behaviour column, which is a reading, not the name.

// gubUnitWeaponGnd -- BYTE[228] @0x5211D8, TU arr/units.cpp. units.dat's "Ground
// Weapon" column, the ground twin of gubUnitWeaponAir 0x1D0 bytes above it, one
// weapon id per unit type. Width 1 and unbiased indexing from its referents,
// CUnit::GetWeaponGndEx and CUnit::AttackWith below, both `mov <r8>, byte ptr
// [eax+5211D8h]` over the widened WORD wType. Extent 228, units.dat's row count,
// under the 232-byte ceiling to gnUnitWeapGndLaunches @0x5212C0. The element is
// read signed, as its air twin is: the sentinel is 0x82 and GetWeaponGndEx
// returns it as -0x7E.

// wpn_gfx_g -- DWORD[130] @0x524F18, TU arr/weapons.cpp. A weapons.dat pointer
// column indexed by an unbiased weapon id; CUnit::FireWeapon below tests it for
// null and fires nothing when it is, which is what makes "the weapon has no
// graphic" the reading. Width 4 and scale 4 at that one referent. Extent 130 is
// exact: the next sheet name, wpn_tech_g @0x525120, is 0x208 bytes on, 130 times
// 4 with no padding.

// Approaches a visible moving target at the point three braking distances ahead
// of it, or at the target itself when that point is off the map or the target
// is hidden or still.
// The target's position is read before the heading and the in-bounds test only
// reassigns it; MSVC then duplicates the ApproachMoveTarget_xy call block.
void CUnit::ApproachAttackTarget(CUnit *pTarget)
{
  if (!CanSeeUnit(pTarget) || !(pTarget->bMoveFlags & 2)) {
    ApproachMoveTarget_CUnit(pTarget);
    return;
  }
  int nX = PosS(pTarget->pSprite->wPosX);
  int nY = PosS(pTarget->pSprite->wPosY);
  BYTE bDir = pTarget->bDirection;
  int nDx, nDy;
  reinterpret_cast<CFlingy *>(this)->GetVectorComponents(
      bDir, 3 * reinterpret_cast<CFlingy *>(this)->BreakingDistance(), &nDx, &nDy);
  CSprite *pSprite = pTarget->pSprite;
  int nAheadX = PosS(pSprite->wPosX) + (nDx >> 8);
  int nAheadY = PosS(pSprite->wPosY) + (nDy >> 8);
  if ((unsigned int)nAheadX < gwMapPixWidth &&
      (unsigned int)nAheadY < gwMapPixHeight) {
    nX = nAheadX;
    nY = nAheadY;
  }
  ApproachMoveTarget_xy(nX, nY);
}

// CUnit::CanAttack
// A unit can attack if either of its own weapon columns is populated, or it is a carrier or
// reaver type with something in its hangar, or its turret's columns are populated; a Lurker's
// ground weapon counts only while burrowed, on both objects. The ground arm writes
// CUnit::GetWeaponGndEx out, so its -0x7E literal folds. The hangar test adds +0xC9 and +0xC8
// as bytes, so 0x80 + 0x80 answers "cannot attack": 1.17's answer, kept.
BOOL CUnit::CanAttack()
{
  weapon_id_t bGround;
  if (wType == 0x67 && !(dwFlags & 0x10))
    bGround = WEAPON_NONE;
  else
    bGround = gubUnitWeaponGnd[wType];
  if (bGround != WEAPON_NONE)
    return TRUE;

  if (gubUnitWeaponAir[wType] != WEAPON_NONE)
    return TRUE;

  if (wType == 0x48 || wType == 0x52) {
    BYTE bDocked = bHangarCountBuilding + bHangarCount;
    if (bDocked)
      return TRUE;
  }

  if (wType == 0x53 || wType == 0x51) {
    if (bHangarCount)
      return TRUE;
  }

  CUnit *pTurret = subunit;
  if (!pTurret)
    return FALSE;

  weapon_id_t bTurretGround;
  if (pTurret->wType == 0x67 && !(pTurret->dwFlags & 0x10))
    bTurretGround = WEAPON_NONE;
  else
    bTurretGround = gubUnitWeaponGnd[pTurret->wType];
  if (bTurretGround != WEAPON_NONE)
    return TRUE;

  return gubUnitWeaponAir[pTurret->wType] != WEAPON_NONE;
}

#if !defined(OC_PAYLOAD) || defined(OC_WITH_HELD)
// Whether this unit can target pTarget. With nFlags set, a target visible to this unit is refused.
BOOL CUnit::CanTarget(CUnit *pTarget, int nFlags)
{
  if ((dwFlags & 0x400) == 0 && !bLockdownTimer && !bStasisTimer &&
      !bParalyzeTimer) {
    CUnit *pOrig = pTarget;
    if (pOrig != 0) {
      DWORD dwTgtFlags = pOrig->dwFlags;
      if ((dwTgtFlags & 0x4000000) == 0 &&
          (pOrig->pSprite->flags.b & 0x20) == 0) {
        if (nFlags == 0 || !pOrig->IsVisibleTo(this)) {
          const BOOL bTurret = IsTurret(subunit);
          // Volatile-read lever; it stays only if it proves the only matching shape in both forms.
          CUnit *pShoot = *(CUnit *volatile *)&subunit;
          if (!bTurret)
            pShoot = this;
          if (wType == 0x48 || wType == 0x52)
            return TRUE;
          if (IsReaver()) {
            if ((dwTgtFlags & 4) || !UnitCanReachGroundTarget(this, pOrig))
              return FALSE;
            return TRUE;
          }
          if (IsQueenType())
            return pOrig->IsInfestable() ? 1 : 0;
          if (wType == 0x47 && dwUnion134 != 0)
            return FALSE;
          if (dwTgtFlags & 4)
            return gubUnitWeaponAir[pShoot->wType] != WEAPON_NONE;
          return pShoot->GetWeaponGndEx() != WEAPON_NONE;
        }
      }
    }
  }
  return FALSE;
}
#endif

// CUnit::CanTargetGroundType
// This unit can attack a target of the given type on the ground if that type is in range
// and vulnerable, and either this unit is a hangar unit, whose real weapon is its
// interceptors or scarabs, or its ground weapon column is populated. The parameter is read
// 16-bit, so WORD. The first two guards are IsVulnerableType inlined, so that function stays
// below in this file; the tail calls GetWeaponGnd (0x41FBE0), not GetWeaponGndEx.
BOOL CUnit::CanTargetGroundType(WORD wUnitType)
{
  if (IsVulnerableType(wUnitType) &&
      (wType == 0x53 || wType == 0x51 || wType == 0x48 || wType == 0x52 ||
       GetWeaponGnd() != WEAPON_NONE))
    return TRUE;

  return FALSE;
}

// CUnit::CanTargetWithWeapon -- oracle 0x41DBF0 (230 B)
//
// Six consecutive "if this weapon flag, then require or forbid that unit-type
// flag" rungs, the shape CUnit::CanTarget (0x41DA50) and CUnit::sub_420520 also
// wear. `ret 8h`, so two stack arguments, callee-cleaned.
//
// The null-target arm returns the masked value, 0x40 or 0, not a normalised
// boolean, so the source says `return flags & 0x40;` and not `!= 0`.
// `neg ecx / sbb ecx, ecx / add ecx, 2` is a ternary rather than a branch: 1
// when the target's flag bit 2 is set and 2 when it is not, the air/ground
// selector. One WORD local produces both operand widths the rungs use, `bl` for
// the bits inside byte range and `ebx, 100h` for bit 8; two reads would not.
BOOL CUnit::CanTargetWithWeapon(weapon_id_t bWeapon, CUnit *pTarget) {
  if (!WeaponTypeValid(bWeapon))  // Hyperion: >= 0x82, and a mod's weapons
    return 0;

  if (pTarget == 0)
    return wpn_targetflag_g[bWeapon] & 0x40;

  if (pTarget->dwFlags & 0x4000000)
    return 0;

  WORD wFlags = wpn_targetflag_g[bWeapon];
  int nAirGround = (pTarget->dwFlags & 4) ? 1 : 2;
  if ((wFlags & nAirGround) == 0)
    return 0;

  if ((wFlags & 4) && !(gulUnitTypeFlags[pTarget->wType] & 0x40000000))
    return 0;
  if ((wFlags & 0x20) && (gulUnitTypeFlags[pTarget->wType] & 0x4000))
    return 0;
  if ((wFlags & 8) && !(gulUnitTypeFlags[pTarget->wType] & 0x10000))
    return 0;
  if ((wFlags & 0x10) && (gulUnitTypeFlags[pTarget->wType] & 1))
    return 0;
  if ((wFlags & 0x80) && !(gulUnitTypeFlags[pTarget->wType] & 0x40010000))
    return 0;
  if ((wFlags & 0x100) && bPlayer != pTarget->bPlayer)
    return 0;

  return 1;
}

// CUnit::CanUseAutoTarget -- oracle 0x41DCE0 (84 B)
//
// Keep this auto-target if it still qualifies, else drop it: four guards over
// one shared false tail, which clears dwUnion7C. Returns 0 or 1.
//
// dwUnion7C is reloaded, not cached -- 1.17 reads [esi+7Ch] three times, at the
// guard, at CanTarget's push and as NewTarget's EDX -- where one local would
// emit a single load and reuse the register. IsEnemyUnit's `push eax` reusing
// the guard's load is MSVC's own common-subexpression work across two adjacent
// uses, not a difference in the source.
BOOL CUnit::CanUseAutoTarget() {
  CUnit *pTarget = (CUnit *)dwUnion7C;
  if (pTarget == 0 || dwUnion134 != 0)
    return 0;

  if (IsEnemyUnit(pTarget) && CanTarget((CUnit *)dwUnion7C, 1)) {
    NewTarget(this, (CUnit *)dwUnion7C, 1, 0);
    dwFlags |= 8;
    return 1;
  }

  dwUnion7C = 0;
  return 0;
}

// CUnit::CancelAlliedTargets -- oracle 0x41DD40 (198 B). Drops every stale
// attack target that points at this unit: its own order target first, then every
// unit on the used list and then the transport list, charging each unit's
// weapon-bearing subunit where it has one, clearing any that still aims here on
// an attack-class order but is no longer an enemy. __thiscall, no argument,
// void.
//
// The two loops are written out twice rather than factored: a helper MSVC
// declined to inline would come in about 75 bytes short, and one it did inline
// is byte-identical to writing them twice. The subunit pick is a `cmov` whose
// destination is seeded with the list node, so the source is the in-place
// `pOwner = pUnit; if (...) pOwner = pSub;` -- a ternary seeds the other way and
// inverts the condition -- and the null test stays a branch nested outside the
// flag test, since it jumps over the whole flag lookup while only the flag
// result feeds the move.
void CUnit::CancelAlliedTargets()
{
  if (pOrderTarget && gubOrderAttack[bOrderId] && !IsEnemyUnit(pOrderTarget))
    pOrderTarget = 0;

  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit; pUnit = pUnit->pNext) {
    CUnit *pOwner = pUnit;
    CUnit *pSub = pUnit->subunit;
    if (pSub) {
      if (gulUnitTypeFlags[pSub->wType] & 0x10)
        pOwner = pSub;
    }
    if (pOwner->pOrderTarget == this && gubOrderAttack[pOwner->bOrderId] &&
        !pOwner->IsEnemyUnit(pOwner->pOrderTarget))
      pOwner->pOrderTarget = 0;
  }

  for (CUnit *pUnit = CUnit::GetTransList(); pUnit; pUnit = pUnit->pNext) {
    CUnit *pOwner = pUnit;
    CUnit *pSub = pUnit->subunit;
    if (pSub) {
      if (gulUnitTypeFlags[pSub->wType] & 0x10)
        pOwner = pSub;
    }
    if (pOwner->pOrderTarget == this && gubOrderAttack[pOwner->bOrderId] &&
        !pOwner->IsEnemyUnit(pOwner->pOrderTarget))
      pOwner->pOrderTarget = 0;
  }
}

// CUnit::ChangeTarget -- oracle 0x41DE10 (369 B). Rescores this unit's attack
// target in four stages, spelled with gotos because that reproduces 1.17's
// shared-tail layout. dwUnion7C is re-read at every use, never cached.
//
// dwFlags bit 27 gates everything, and clear is a bare return. The pick stage
// scores pOrderTarget, setting the score to the maximum when the candidate is
// discarded: an IsEnemyUnit failure returns outright, a CanTarget or
// ai_target_unreachable failure falls into the discard block, and a zero score
// returns when the target is already in weapon range and otherwise becomes 1.
// The dwUnion7C stage scores the same way, but all three predicate failures
// share one zero-store tail laid between the adopt path and the refresh entry,
// and adopt only on a strictly smaller score. The refresh stage is gated
// separately by candidate and carried score, so one adopted with score 0 skips
// FindAttackTarget. The store stage needs CanTarget, then mirrors the pointer
// into the subunit when its bOrderId is that type's attack-unit order or 0x6B.
void CUnit::ChangeTarget()
{
  if (!(dwFlags & 0x8000000))
    return;

  CUnit *pTgt = pOrderTarget;
  unsigned int nBest;

  if (!pTgt) {
    nBest = 0x7FFFFFFF;
  } else {
    if (dwUnion134 && !IsEnemyUnit(pTgt))
      return;
    if (!CanTarget(pTgt, 1)) {
      pTgt = NULL;
      nBest = 0x7FFFFFFF;
    } else if (ai_target_unreachable(this, pTgt)) {
      pTgt = NULL;
      nBest = 0x7FFFFFFF;
    } else {
      unsigned int nSc = (unsigned int)sub_4319D0(pTgt);
      if (!nSc) {
        int nReach = IsUnitInWeaponRange(pTgt);
        if (nReach)
          return;
        nBest = (unsigned int)(nReach + 1);   // lea ebx,[eax+1]
      } else {
        nBest = nSc;
      }
    }
  }

  // --- dwUnion7C stage: the field is re-read at every use, never cached ---
  if (!dwUnion7C)
    goto stage_refresh;
  if (!CanTarget(reinterpret_cast<CUnit *>(dwUnion7C), 1))
    goto stage_drop;
  if (!IsEnemyUnit(reinterpret_cast<CUnit *>(dwUnion7C)))
    goto stage_drop;
  if (ai_target_unreachable(this, reinterpret_cast<CUnit *>(dwUnion7C)))
    goto stage_drop;

  {
    int nAlt = sub_4319D0(reinterpret_cast<CUnit *>(dwUnion7C));
    if (!nAlt) {
      if (!IsUnitInWeaponRange(reinterpret_cast<CUnit *>(dwUnion7C)))
        nAlt = 1;               // cmovz ecx,edx
    }
    if ((int)nAlt < (int)nBest) {
      pTgt = reinterpret_cast<CUnit *>(dwUnion7C);
      nBest = (unsigned int)nAlt;
    }
  }
  goto stage_refresh;

stage_drop:
  dwUnion7C = 0;
stage_refresh:
  // --- refresh stage: entered when no candidate OR the carried best exists --
  if (!pTgt || nBest != 0) {
    if (dwUnion134) {
      if (AIPickAttackTarget(0, 1, 0))
        return;
    }
    CUnit *pFresh = FindAttackTarget();
    if (pFresh) {
      int nNew = sub_4319D0(pFresh);
      if (nNew < (int)nBest)
        pTgt = pFresh;
    }
  }

  // --- store stage ---
  if (pTgt) {
    if (!CanTarget(pTgt, 1))
      return;
  }
  CUnit *pSub = subunit;
  pOrderTarget = pTgt;
  if (!pSub)
    return;
  order_id_t bOrder = pSub->bOrderId;
  if (bOrder != gubUnitOrderAttackUnit[pSub->wType] && bOrder != 0x6B)
    return;
  pSub->pOrderTarget = pTgt;
}

// CUnit::ClearTarget -- oracle 0x41DF90 (36 B). Clears this unit's order target
// and its weapon-bearing subunit's. __thiscall, no argument, void, with no frame
// and no register save: ECX is reused as the subunit pointer after the first
// store, which is legal only because `this` is dead from there on.
//
// `test byte ptr [eax*4+521660h], 10h` is MSVC narrowing a low-byte mask over
// gulUnitTypeFlags' DWORD element; the source says `& 0x10` on the whole
// element, the form CUnit::DispatchAttackUnit also carries with mask 1.
void CUnit::ClearTarget() {
  pOrderTarget = 0;
  CUnit *pSub = subunit;
  if (pSub != 0 && (gulUnitTypeFlags[pSub->wType] & 0x10))
    pSub->pOrderTarget = 0;
}

// Damages this unit with a bullet's hit, or only notifies it when the bullet's flag bit 1 is set.
// The damage arm reads the direction, the player and the weapon into locals in that order.
void CUnit::DamageFromBullet(CBullet *pBullet, int nDivisor) {
  if (pBullet->bFlags62 & 2) {
    DamageNotify(pBullet->bWeaponType, *(reinterpret_cast<const BYTE *>(pBullet) + offsetof(CFlingy, bCurrentDirection)), pBullet->pAttacker);
    return;
  }
  BYTE bDirection = *(reinterpret_cast<const BYTE *>(pBullet) + offsetof(CFlingy, bCurrentDirection));
  BYTE bPlayer = pBullet->bPlayer;
  weapon_id_t bWeapon = pBullet->bWeaponType;
  Damage(pBullet->sub_409010(this), bWeapon, nDivisor, bDirection, pBullet->pAttacker, bPlayer);
}

// CUnit::DamageNotify
// A unit with no hit points, or carrying dwFlags bit 0x4000000, is notified of nothing.
// Otherwise the attacker gets an OnDamaged call, its owner hears the under-attack sound when
// it belongs to another player, and a shield impact is spawned when all three shield
// conditions hold; weapon 0x22 is silent but still spawns it. The 0x22 test is written twice,
// as 1.17 tests it. +0x60 is read signed with `(long)`, as in GetToughness.
void CUnit::DamageNotify(weapon_id_t bWeapon, int bDirection, CUnit *pAttacker) {
  if (!lHitPoints || (dwFlags & 0x04000000)) {
    return;
  }

  if (pAttacker) {
    OnDamaged(pAttacker, bWeapon != 0x22);
    if (bWeapon != 0x22 && bPlayer != pAttacker->bPlayer) {
      gamesnd_under_attack(this);
    }
  }

  if (gbUnitHasShields[wType] && (LONG)lShields >= 0x100 &&
      wpn_wpnType_g[bWeapon]) {
    CreateShieldEffect(bDirection);
  }
}

// CUnit::DispatchAIReturn -- oracle 0x41E370 (138 B)
//
// `cmp word ptr [esi+64h], 22h` is a 16-bit compare straight out of memory,
// where CUnit::DispatchGuardPost below emits `movzx ecx, word ptr [esi+64h] /
// cmp ecx, 54h` for the same field. Nothing in the source differs: there wType
// is compared twice and MSVC hoists it into a register, here once and it stays a
// memory operand. The same AIPickAttackTarget(0, 1, 0) is three literal pushes
// here against that body's `push eax / push 1h / push eax`, where EAX is known
// zero from the preceding branch. One source, two encodings, in both cases.
//
// CSprite::wPosX is WORD and takes a (short) cast; wPosY is already `short` and
// does not. Both arrive at guard_move_post through `movsx`, so both have to be
// signed 16-bit at the point of the widening.
void CUnit::DispatchAIReturn() {
  if (AIPickAttackTarget(0, 1, 0))
    return;

  if (wType == 0x22) {
    DispatchMedic();
    if (bOrderId != 0xA3)
      return;
  }

  if (bOrderState == 0) {
    if (!SetDestination_xy(wOrderTargetX, wOrderTargetY))
      return;
    bOrderState = 1;
  }

  DWORD_PTR dwAI = dwUnion134;
  if (dwAI == 0)
    return;

  if (*(const BYTE *)(dwAI + offsetof(TGuard, bType)) != 1) {
    SetOrder(0x9C);
    return;
  }

  if (!AtMoveTarget())
    return;

  guard_move_post(this, PosS(pSprite->wPosX), pSprite->wPosY);
  SetOrder(2);
}

// CUnit::DispatchAttackFixedRange -- oracle 0x41E400 (145 B)
//
// The `jmp` after the FindAttackTarget store makes the first test a real
// if/else rather than an early return: both arms rejoin.
//
// `cmp dword ptr [esi+134h], eax` compares against zero, not against the call
// result: control reaches it only through `test eax, eax / jne`, so MSVC knows
// EAX is 0 and spends a register instead of a 4-byte immediate. Written
// `dwUnion134 == 0`.
//
// The target pointer is cached once for the stores and the CanTarget push but
// reloaded for IsUnitInWeaponRange: one local used at all three sites emits one
// load, and spelling the field at the third site is what produces the reload.
// CFlingy::SetDestination is reached through a cast, CUnit not deriving from
// CFlingy in this tree.
void CUnit::DispatchAttackFixedRange() {
  if (dwUnion134 != 0)
    pOrderTarget = FindAttackTarget();
  else
    ChangeTarget();

  CUnit *pTarget = pOrderTarget;
  if (pTarget != 0) {
    wOrderTargetX = pTarget->pSprite->wPosX;
    wOrderTargetY = pTarget->pSprite->wPosY;
  }

  if (!CanTarget(pTarget, 1)) {
    EndDispatchOrder();
    return;
  }

  if (!IsUnitInWeaponRange(pOrderTarget)) {
    if (dwUnion134 != 0) {
      SetOrder_xy(0xE, wOrderTargetX, wOrderTargetY);
      return;
    }
    EndDispatchOrder();
    return;
  }

  reinterpret_cast<CFlingy *>(this)->SetDestination(wOrderTargetX,
                                                    wOrderTargetY);
  sub_41F630(5);
}

// CUnit::AtMoveTarget inlined; the oracle's unused [ebp-4] is its home slot.
static __forceinline BOOL combat_at_move_target(CUnit *pUnit)
{
  CSprite *pSprite = pUnit->pSprite;

  if (pUnit->wMoveTargetX == pSprite->wPosX
      && pUnit->wMoveTargetY == PosU(pSprite->wPosY))
    return (pUnit->dwFlags & 0x80000) ? 2 : 1;

  return 0;
}

// oracle 0x41E4A0 (102 B). Attack-move step: ends the order at the move
// target, otherwise passes the order id on to sub_4206E0.
// The order id is one local so EDI carries it across the first call.
void CUnit::DispatchAttackMove() {
  int nOrderId = bOrderId;
  if (!sub_420600(nOrderId))
    return;
  if (bUnion54 != 0)
    return;

  bUnion54 = 0xF;
  if (combat_at_move_target(this)) {
    EndDispatchOrder();
    return;
  }

  sub_4206E0(nOrderId);
}

// oracle 0x41F170 (393 B). Turret attack order: track the target and fall back
// to order 4 when it cannot be hit. A type-6 turret whose subunit has
// dwUnion134 set re-orders both (0x17 / 0x62) for a target between the ranges
// of weapons [31] and [6].
// Both weapon ids are int locals read up front; the one used second is spilled.
void CUnit::DispatchTurretAttack()
{
  ChangeTarget();
  CUnit *pTarget = pOrderTarget;
  if (pTarget) {
    wOrderTargetX = pTarget->pSprite->wPosX;
    wOrderTargetY = pTarget->pSprite->wPosY;
  }
  if (!CanTarget(pTarget, 1) || !CanSeeUnit(pOrderTarget)) {
    SetOrder(4);
    return;
  }

  CUnit *pSub = subunit;
  if (wType == 6 && pSub->dwUnion134 != 0) {
    int nInner = gubUnitWeaponGnd[6];
    int nOuter = gubUnitWeaponGnd[31];
    if (TechUseAllowed(5, bPlayer, pSub) == 1
        && CanSeeFocusUnit()
        && pSub->sub_421120(GetWeaponRange(nOuter), pOrderTarget)
        && !pSub->sub_421120(GetWeaponRange(nInner), pOrderTarget)
        && !pOrderTarget->IsCritter()) {
      CUnit *pVictim = pOrderTarget;
      SetOrder(0x17);
      SetOrder_interrupt(gubUnitOrderAttackUnit[31], 0);
      pSub->SetOrder(0x62);
      pSub->SetOrder_CUnit_interrupt(gubUnitOrderAttackUnit[30], pVictim, 0);
      return;
    }
  }

  if (!IsUnitInWeaponRange(0)) {
    CUnit *pNow = subunit;
    if (pNow->bOrderId != gubUnitOrderAttackUnit[pNow->wType]
        || pNow->GetRClickAction() == 3) {
      SetOrder(4);
      return;
    }
  }

  reinterpret_cast<CFlingy *>(this)->SetDestination(wOrderTargetX, wOrderTargetY);
  if (subunit->bMoveFlags & 2)
    return;
  sub_41F630(5);
}

// A forwarding helper: the coordinates are loaded before `this` goes to ECX.
static __forceinline BOOL combat_in_radius(CTarget *pTarget, int nRadius, int x, int y)
{
  return pTarget->XYInRadius(nRadius, x, y);
}

// oracle 0x41EA40 (350 B). Nuke launch: walk into range of the target, face
// it within one step, then hand the order to the ghost parked in an armed silo.
// bUnion21 is read into a local before AngleBetween; the facing compares repeat
// the subtraction, since a named difference folds into one range test.
void CUnit::DispatchNukeGround()
{
  reinterpret_cast<CFlingy *>(this)->SetDestination(wOrderTargetX, wOrderTargetY);
  int nRadius = sub_423150(0) << 5;

  if (dwUnion134 != 0 &&
      combat_in_radius(reinterpret_cast<CTarget *>(this), nRadius * 3,
                       wOrderTargetX, wOrderTargetY))
    tai_cloak_if_allowed(this);

  if (!combat_in_radius(reinterpret_cast<CTarget *>(this), nRadius,
                        wOrderTargetX, wOrderTargetY)) {
    ApproachMoveTarget_xy(wOrderTargetX, wOrderTargetY);
    return;
  }

  Stop();
  CFlingy *pFly = reinterpret_cast<CFlingy *>(this);
  if (PosU(pFly->wDestinationX) != wPositionX ||
      PosU(pFly->wDestinationY) != wPositionY) {
    CSprite *pSpr = pSprite;
    BYTE bDir = bUnion21;
    upos_t wSprY = PosU(pSpr->wPosY);
    upos_t wSprX = pSpr->wPosX;
    int nAngle = AngleBetween(PosS(wSprX), PosS(wSprY),
                              pFly->wDestinationX, pFly->wDestinationY);
    if (bDir - nAngle < -1 || bDir - nAngle > 1)
      return;
  }

  CUnit *pSilo = FindArmedSilo(0, 0);
  if (!pSilo) {
    EndDispatchOrder();
    return;
  }

  CUnit *pGhost = pSilo->pUnionD0;
  pSilo->pUnionD0 = 0;
  pSilo->pUnionD4 = 0;
  gamesnd_play(0xEF, this, 1, 0);
  pGhost->SetOrder_xy(0x7D, wOrderTargetX, wOrderTargetY);
  pGhost->pUnion80 = this;
  pGhost->pSprite->FaceDirection(0);
  pGhost->Show();
  pUnion80 = pGhost;
  SetOrder_xy(0x81, PosS(pSprite->wPosX), pSprite->wPosY);
  SetButtonset(0xED);
  status_set_update();
}

// CUnit::DispatchAttackMoveEP
// bOrderId is one local, kept in EBX across three calls. The region comparison reads
// `GetRegion_CUnit(this) == GetRegion_xy(x, y)`, the reverse of the call order, since MSVC
// evaluates the operands right to left. Both coordinate compares are 16-bit: wPosY is short
// and X takes an explicit (WORD), on the compare only; the second pair of reads is a reload.
void CUnit::DispatchAttackMoveEP()
{
  int nOrderId = bOrderId;
  if (!sub_420600(nOrderId))
    return;
  if (bUnion54)
    return;

  bUnion54 = 0x0F;
  if (GetRegion_CUnit(this) == GetRegion_xy(wOrderTargetX, wOrderTargetY)) {
    StopUnitMovement();
    EndDispatchOrder();
    return;
  }

  if (AtMoveTarget()) {
    if (PosU(wOrderTargetX) != pSprite->wPosX ||
        wOrderTargetY != pSprite->wPosY) {
      if (!SetDestination_xy(wOrderTargetX, wOrderTargetY)) {
        if (sub_421370(this, wOrderTargetX, wOrderTargetY))
          StopUnitMovement();
        return;
      }
    }
  }

  sub_4206E0(nOrderId);
}

static __forceinline int hm_dist(upos_t wTX, int wTY, int wSX, int wSY)
{
  int nX1 = PosS(wTX);
  return veclen(nX1, PosS(wSX), wTY, PosS(wSY));
}

static __forceinline BOOL hm_at(upos_t wTX, upos_t wSX, spos_t wTY, CSprite *pSp)
{
  return wTX == wSX && wTY == pSp->wPosY;
}

// Harass-move step: chase the captain's target or stop, then, once at the move
// target, either end the order (same region and within 0x60 of the order
// target) or head for the order target again.
// The helper binds sx after ty and casts tx into a local, which orders the
// webs so sx gets ESI and tx is re-read for veclen.
void CUnit::DispatchHarassMoveEP()
{
  int nOrderId = bOrderId;
  if (!sub_420600(nOrderId))
    return;
  if (bUnion54)
    return;

  bUnion54 = 0x0F;
  CUnit *pTarget = tai_soldier_captain_leader(this);
  if (this == pTarget) {
    if (tai_soldier_follower_lagging(this)) {
      StopUnitMovement();
      return;
    }
  } else {
    if (pTarget && !tai_soldier_is_in_position(this)) {
      SetMoveTarget_CUnit(pTarget);
      return;
    }
    if (tai_soldier_sibling_boarding(this)) {
      StopUnitMovement();
      return;
    }
  }

  tai_soldier_step_to_captain_region(this);
  if (AtMoveTarget()) {
    if (GetRegion_CUnit(this) == GetRegion_xy(wOrderTargetX, wOrderTargetY)) {
      CSprite *pSp = pSprite;
      upos_t wSX = pSp->wPosX;
      if (hm_at(wOrderTargetX, wSX, wOrderTargetY, pSp) ||
          hm_dist(wOrderTargetX, wOrderTargetY, PosU(pSp->wPosX), PosU(pSp->wPosY)) > 0x60)
        goto end;
      // Decision 16: the only shape found that matches in both forms.
      _ReadWriteBarrier();
    }
    if (!SetDestination_xy(wOrderTargetX, wOrderTargetY))
      return;
    sub_4206E0(nOrderId);
    // Decision 16: the only shape found that matches in both forms.
    _ReadWriteBarrier();
    return;
end:
    EndDispatchOrder();
  }
  sub_4206E0(nOrderId);
}

// CUnit::DispatchAttackUnit
// A computer-controlled slot (nType == 1) whose order target is absent or is not a bit-0
// unit type offers the decision to guard_return_to_post and returns if it claims it.
// Everything else retargets and, on a successful CUnit::sub_420770, runs
// tai_soldier_try_stim when dwUnion134 is set, then hands off to CUnit::sub_41F630 with 5.
// The guard is one `&&` over an `||`; separate `if`s jump differently.
void CUnit::DispatchAttackUnit() {
  if (gPlayerData[bPlayer].nType == 1 &&
      (!pOrderTarget || !(gulUnitTypeFlags[pOrderTarget->wType] & 1))) {
    if (guard_return_to_post(this, 0)) {
      return;
    }
  }

  ChangeTarget();
  if (!sub_420770()) {
    return;
  }
  if (dwUnion134) {
    tai_soldier_try_stim(this);
  }
  sub_41F630(5);
}

// CUnit::DispatchDie -- oracle 0x41E630 (196 B)
//
// No `ret` exists in this function: both exits are tail jumps, into
// CUnit::Destroy and CUnit::DestroyImpl, legal only from a void function.
//
// The CSprite::SetAnimation call has no `mov ecx` of its own, ECX still holding
// the sprite loaded further up, because the path between tests only EAX, so the
// source spells `pSprite->SetAnimation(1, 1)` and shares that load with the
// flags test above it. The second flag test is the sign bit, 0x80000000, which
// MSVC reaches through the cheaper `js`. The subunit is read three times with
// one reload, MSVC being unable to carry a field across the intervening call.
//
// CThingy has no members here, so the thingy's sprite is reached through
// CTarget, which declares pSprite at +0x0C -- a view, not a claim about
// derivation.
void CUnit::DispatchDie() {
  if (bUnion93 & 4)
    Hide();

  if (subunit != 0) {
    pSprite->ClaimOwnership(subunit->pSprite);
    subunit->Destroy();
    subunit = 0;
  }

  bOrderState = 1;
  if (!(pSprite->flags.b & 0x20)) {
    if (!(dwFlags & 0x40000000) || (dwFlags & 0x80000000)) {
      pSprite->SetAnimation(1, 1);
      DestroyImpl();
      return;
    }

    gamesnd_play(0x26B, this, 1, 0);
    upos_t wY = pSprite->wPosY;
    upos_t wX = pSprite->wPosX;
    CThingy *pThingy = CThingy::AllocThingy(0x176, PosS(wX), PosS(wY), 0);
    if (pThingy != 0) {
      reinterpret_cast<CTarget *>(pThingy)->pSprite->bElevationLevel =
          pSprite->bElevationLevel + 1;
      reinterpret_cast<CTarget *>(pThingy)->pSprite->SetMask(
          pSprite->bVisibilityFlags);
      VisionHxSpriteSet(reinterpret_cast<CTarget *>(pThingy)->pSprite,
                        VisionHxSprite(pSprite));  // Hyperion
    }
  }

  Hide();
  Destroy();
}

// CUnit::DispatchGuard -- oracle 0x41E700 (83 B)
//
// The nesting is in the branch targets, not the mnemonics. Read cold, the `jnz`
// past a `cmp ...,1` looks like an early exit; that target is the common tail
// assigning order 3, and the computer-slot test falls into the AI block. The
// next `jnz` is likewise the then-arm of the dwUnion134 test, whose else-arm
// continues into the unit-flag test and only then falls into the tail. bOrderId
// takes three values: 0x9C inside the AI arm, 0xA0 if that byte is 1, and 3 on
// both non-AI paths, with the 0x9C store before the test.
//
// dwUnion134 is dereferenced here, where every other reader only truth-tests it
// as a DWORD: `cmp byte ptr [eax+8h], 1h` reads a byte at +8 of whatever it
// points at. The cast is at the point of use and the field keeps its DWORD
// declaration; naming the pointee would need more than this one referent.
void CUnit::DispatchGuard() {
  bUnion54 = (BYTE)net_randbetween(0, 15, 0x1D);
  if (gPlayerData[bPlayer].nType != 1) {
    bOrderId = 3;
    return;
  }

  DWORD_PTR dwAI = dwUnion134;
  bOrderId = 0x9C;
  if (dwAI == 0) {
    if (gulUnitTypeFlags[wType] & 0x40)
      bOrderId = 3;
    return;
  }

  if (*(const BYTE *)(dwAI + offsetof(TGuard, bType)) == 1)
    bOrderId = 0xA0;
}

// CUnit::DispatchGuardBunker
// The nine-way type test is one `||` chain in 1.17's unsorted order, not a switch. The store
// to pOrderTarget precedes its null test, unconditionally, and the guards past the flag
// stores are flat early returns, not an `&&` chain.
void CUnit::DispatchGuardBunker()
{
  if (wType == 0 || wType == 0x14 || wType == 1 || wType == 0x10 ||
      wType == 0x64 || wType == 0x63 || wType == 0x68 || wType == 0x20 ||
      wType == 0x0A) {
    dwFlags |= 0x08000000;
    if (subunit)
      subunit->dwFlags |= 0x08000000;

    if (sub_420BD0()) {
      reinterpret_cast<CFlingy *>(this)->SetDestination(wOrderTargetX, wOrderTargetY);
      return;
    }

    if (bUnion54)
      return;
    bUnion54 = 0x0F;
    pOrderTarget = sub_432060();
    if (pOrderTarget)
      bUnion85 = 0;
  }
}

// CUnit::DispatchGuardPost -- oracle 0x41E7F0 (156 B)
//
// The nesting is in the branch targets: the `je` off the second coordinate
// compare is the equal case going to the order-0x9C block, so the guarded arm
// runs when the position differs -- a disjunction of two inequalities, which
// read by mnemonic gives the opposite program.
//
// `push eax / push 1h / push eax` is AIPickAttackTarget(0, 1, 0): the
// fallthrough of `test eax, eax / jne` proves EAX is 0, so MSVC spends the live
// zero twice instead of two 5-byte `push imm32`. wType is read once into ECX and
// compared twice with no call between, which is MSVC's own work on a plain field
// read and not a local. The AI block's two coordinates are signed shorts, and
// the compares against the sprite are 16-bit with no widening, which needs both
// sides at one signedness: CSprite::wPosX is WORD and takes the cast, wPosY is
// already short.
void CUnit::DispatchGuardPost() {
  if (bUnion54 != 0)
    return;

  bUnion54 = 0xF;
  if (ai_update_unit_spells(this, 0))
    return;
  if (AIPickAttackTarget(0, 1, 0))
    return;
  if (wType == 0x54)
    return;

  DWORD_PTR dwAI = dwUnion134;
  if (dwAI != 0 && *(const BYTE *)(dwAI + offsetof(TGuard, bType)) == 1) {
    const TGuard *pGuard = reinterpret_cast<const TGuard *>(dwAI);  // Hyperion: by field (stage E)
    if (PosS(pSprite->wPosX) != PosS(pGuard->wPosX) ||
        pSprite->wPosY != PosS(pGuard->wPosY)) {
      if (wType == 0x1E)
        SetOrder_interrupt(0x63, 0);
      SetOrder_xy_interrupt(0xA3, PosS(pGuard->wPosX), PosS(pGuard->wPosY), 0);
      EndDispatchOrder();
      return;
    }
  }

  SetOrder(0x9C);
}

// CUnit::DispatchHoldPosition
// The first guard is a one-shot initialiser, `if (!bOrderState) { ... }`, not an early
// return. The four-term middle test is an inverted `&&` chain: the early exit is taken when
// all four hold. Its first two terms are CUnit::IsTurret written out, since IsTurret is
// defined later in the TU and a call would not be inlined.
void CUnit::DispatchHoldPosition()
{
  if (!bOrderState) {
    StopUnitMovement();
    dwFlags |= 0x08000000;
    if (subunit)
      subunit->dwFlags |= 0x08000000;
    bOrderState = 1;
  }

  if (sub_420BD0()) {
    if (subunit && (gulUnitTypeFlags[subunit->wType] & 0x10) &&
        wType != 3 && wType != 0x11)
      return;
    reinterpret_cast<CFlingy *>(this)->SetDestination(wOrderTargetX, wOrderTargetY);
    return;
  }

  if (bUnion54)
    return;
  bUnion54 = 0x0F;
  pOrderTarget = sub_432060();
  if (pOrderTarget)
    bUnion85 = 0;
}

// CUnit::DispatchNukeUnit
// pSprite is reloaded for the second coordinate, as two separate statements give. Both exits
// are tail jumps.
void CUnit::DispatchNukeUnit()
{
  CUnit *pTarget = pOrderTarget;
  if (!pTarget) {
    Stop();
    EndDispatchOrder();
    return;
  }

  wOrderTargetX = pTarget->pSprite->wPosX;
  wOrderTargetY = pTarget->pSprite->wPosY;
  DispatchNukeGround();
}

// CUnit::DispatchPlayerGuard
// The outer pair of guards is one `if (target && !dwUnion134) { ... }` whose body ends by
// clearing the slot, the inner pair an `&&` inside it that returns early on success.
// dwUnion7C is read through the member each time, and the subunit is loaded once for both
// destination fields. CUnit::EnemyTrackingRange is declared BYTE: the site tests AL, and the
// callee has a `xor al, al` exit.
void CUnit::DispatchPlayerGuard()
{
  if (dwUnion7C && !dwUnion134) {
    if (IsEnemyUnit(reinterpret_cast<CUnit *>(dwUnion7C)) &&
        CanTarget(reinterpret_cast<CUnit *>(dwUnion7C), 1)) {
      NewTarget(this, reinterpret_cast<CUnit *>(dwUnion7C), 1, 0);
      dwFlags |= 0x08;
      return;
    }
    dwUnion7C = 0;
  }

  if (bUnion54)
    return;
  bUnion54 = 0x0F;
  if (gulUnitTypeFlags[wType] & 0x10)
    reinterpret_cast<CFlingy *>(this)->SetDestination(
        reinterpret_cast<CFlingy *>(subunit)->wDestinationX,
        reinterpret_cast<CFlingy *>(subunit)->wDestinationY);

  if (EnemyTrackingRange()) {
    CUnit *pTarget = FindAttackTarget();
    if (pTarget)
      NewTarget(this, pTarget, 1, 0);
  }
}

// CUnit::DispatchSapLocation -- oracle 0x41EEB0 (191 B)
//
// `sub eax, 0h / je / dec / je / dec / je` is a switch on bOrderState, not an
// if-ladder, in the dense form CUnitMisc.cpp's CUnit::DispatchRevealTrap also
// carries -- and all three arms fall through, case 0's tail storing
// bOrderState = 1 and landing on case 1's first instruction, case 1's storing 2
// and landing on case 2's, so the source omits the `break` deliberately.
//
// The `and 80000h / neg / sbb / neg / inc` ternary is three-valued in the source
// and two-valued in fact: it is tested against 2 and then against 0, but +1 on a
// 0/1 flag can never give 0, so that second `je` can never be taken. The dead
// sentinel is kept rather than simplified. The two false exits differ -- mode 2
// failing the radius test tail-jumps to EndDispatchOrder where mode 0 returns
// bare -- which is what forces the if / else-if shape. 0x525BE4 is
// wpn_outersplash_g[54].
void CUnit::DispatchSapLocation() {
  switch (bOrderState) {
  case 0:
    if (!SetDestination_xy(wOrderTargetX, wOrderTargetY))
      return;
    bOrderState = 1;
    // fall through

  case 1: {
    if (wMoveTargetX != pSprite->wPosX ||
        PosS(wMoveTargetY) != pSprite->wPosY)
      return;

    int nMode = (dwFlags & 0x80000) ? 2 : 1;
    if (nMode == 2) {
      int nY = wOrderTargetY;
      int nX = wOrderTargetX;
      int nRadius = wpn_outersplash_g[54];
      if (!reinterpret_cast<CTarget *>(this)->XYInRadius(nRadius, nX, nY)) {
        EndDispatchOrder();
        return;
      }
    } else if (nMode == 0) {
      return;
    }

    pOrderTarget = this;
    pSprite->SetAnimation(0xD, 1);
    dwFlags |= 0x1000;
    bOrderState = 2;
  }
    // fall through

  case 2:
    if (bOrderFlags & 1) {
      dwFlags |= 0x80000000;
      Suicide();
    }
    break;
  }
}

// CUnit::DispatchSapUnit -- oracle 0x41EF70 (274 B)
//
// Three tail jumps, so this and each of those callees is void -- only a void
// function can be tail-jumped from a void one.
//
// `sub eax,0 / dec / dec` is the switch lowering, not three compares: a dense
// small switch on bOrderState is what MSVC turns into that chain, where an
// if/else ladder gives three `cmp`s. Arms 0 and 1 both fall through into the
// next, as the sibling CUnit::DispatchSapLocation above does, so the source
// omits the `break`. The order target is re-read at three separate points, the
// CanTarget failure arm reloading it even though EAX still holds it; one cached
// local keeps the register and the reloads vanish. 0x525BE4 is
// wpn_outersplash_g[54], weapon 54 being the Infested Terran suicide weapon,
// which is what "Sap" names.
void CUnit::DispatchSapUnit() {
  if (pOrderTarget == 0) {
    EndDispatchOrder();
    return;
  }

  if (!CanTarget(pOrderTarget, 1)) {
    CSprite *pS = pOrderTarget->pSprite;
    // (short) on wPosX only. CSprite::wPosX is declared WORD and both operands
    // reach SetOrder_xy through `movsx`, so the X needs the cast to emit 0F BF
    // rather than 0F B7; wPosY is already `short` and needs nothing. Exactly
    // the treatment src117/h/CSprite.h settles and sub_421320 carries -- do not
    // "fix" it by retyping the field, which regresses CBullet::DispatchBounce.
    SetOrder_xy(6, PosS(pS->wPosX), pS->wPosY);
    return;
  }

  switch (bOrderState) {
  case 0:
    if (!SetMoveTarget_CUnit(pOrderTarget))
      return;
    bOrderState = 1;
    // fall through
  case 1: {
    int nAtTarget = AtMoveTarget();
    if (!IsWithinTargetRange(4, pOrderTarget) && nAtTarget != 2) {
      if (IsWithinTargetRange(0x100, pOrderTarget) &&
          (reinterpret_cast<CFlingy *>(pOrderTarget)->bMoveFlags & 2)) {
        reinterpret_cast<CFlingy *>(this)->InterceptTarget(pOrderTarget);
        return;
      }
      ApproachMoveTarget_CUnit(pOrderTarget);
      return;
    }
    Stop();
    if (!IsWithinTargetRange(wpn_outersplash_g[54], pOrderTarget)) {
      EndDispatchOrder();
      return;
    }
    pOrderTarget = this;
    pSprite->SetAnimation(0xD, 1);
    dwFlags |= 0x1000;
    bOrderState = 2;
    // fall through
  }
  case 2:
    if (bOrderFlags & 1) {
      dwFlags |= 0x80000000;
      Suicide();
    }
    break;
  }
}

// CUnit::DispatchTowerGuard
// Six guards, one exit: a chain of flat early returns. The store to pOrderTarget precedes its
// null test, unconditionally. The two `or ... 8000000h` set the same bit on this unit,
// unguarded, and on its subunit where it has one; nothing here decodes the bit, so it stays
// unnamed. +0x54 is byte-wide at this site, and its 0x0F is 1.17's immediate.
void CUnit::DispatchTowerGuard()
{
  if (dwFlags & 0x400)
    return;
  if (bLockdownTimer)
    return;
  if (bStasisTimer)
    return;
  if (bParalyzeTimer)
    return;
  if (bUnion54)
    return;

  bUnion54 = 0x0F;
  pOrderTarget = sub_432060();
  if (!pOrderTarget)
    return;

  SetOrder_CUnit(0x13, pOrderTarget);
  dwFlags |= 0x08000000;
  if (subunit)
    subunit->dwFlags |= 0x08000000;
}

// CUnit::DispatchTowerTarget
// A tower that is not disabled and whose sub_420BD0 check passes parks its move target on its
// own sprite and aims its flingy destination at its order target; anything else ends the
// order through a tail jump. The five guards are one `&&` chain sharing one tail, unlike
// DispatchTowerGuard's flat returns. The X read takes an explicit (short), wPosX being WORD
// and wPosY short.
void CUnit::DispatchTowerTarget()
{
  if (!(dwFlags & 0x400) &&
      !bLockdownTimer &&
      !bStasisTimer &&
      !bParalyzeTimer &&
      sub_420BD0()) {
    SetMoveTarget_xy(PosS(pSprite->wPosX), pSprite->wPosY);
    reinterpret_cast<CFlingy *>(this)->SetDestination(wOrderTargetX, wOrderTargetY);
    return;
  }

  EndDispatchOrder();
}

// CUnit::DispatchTurretGuard
// The unit points itself at where its turret is already headed: the ECX reaching
// CFlingy::SetDestination is `this`, and the subunit only supplies the two coordinates. Those
// loads are movsx, which makes CFlingy::wDestinationX and Y `short`. There is no null check
// on the subunit: the order is only dispatched for a unit that has a turret.
void CUnit::DispatchTurretGuard()
{
  reinterpret_cast<CFlingy *>(this)->SetDestination(
      reinterpret_cast<CFlingy *>(subunit)->wDestinationX,
      reinterpret_cast<CFlingy *>(subunit)->wDestinationY);
  DispatchPlayerGuard();
}

// CUnit::FireSpell
// Fires the weapon's bullet once, or twice for a weapon whose gnWeapLaunch column reads 2,
// the twin-bullet weapons. The argument slot is a dword consumed through its low byte, so the
// parameter is BYTE; there is no result. The second shot is a second call under an `if`, not
// a loop.
void CUnit::FireSpell(weapon_id_t bWeaponId)
{
  MakeBullet(bWeaponId);
  if (gnWeapLaunch[bWeaponId] == 2)
    MakeBullet(bWeaponId);
}

// CUnit::FireWeapon
// Allocates the weapon's bullet at the order target, twice for a weapon whose gnWeapLaunch
// column reads 2, where FireSpell routes through CUnit::MakeBullet instead. The parameter is
// BYTE, and `int nWeaponId = bWeaponId;` keeps the widened id in EDI for both indexes. The two
// coordinates are hoisted into plain `int` locals, which lets MSVC spill Y into the dead
// argument slot as 1.17 does.
void CUnit::FireWeapon(weapon_id_t bWeaponId)
{
  int nWeaponId = bWeaponId;

  if (!wpn_gfx_g[nWeaponId])
    return;

  int nX = wOrderTargetX;
  int nY = wOrderTargetY;
  CBullet::AllocBullet(nWeaponId, nX, nY, bPlayer, bUnion21, this);

  if (gnWeapLaunch[nWeaponId] == 2)
    CBullet::AllocBullet(nWeaponId, nX, nY, bPlayer, bUnion21, this);
}

// CUnit::sub_41F530
// Fires one weapon slot. The slot index is a byte offset off bUnion54, `(&bUnion54)[nSlot]`,
// with siblings at +0x55 and +0x56; a named field would fold it away. The cooldown is
// computed in bytes, the jitter a BYTE expression and the sum one unnamed expression, which
// gives 1.17's `add bl, al`. GetWeaponCD is called before net_rand.
BOOL CUnit::sub_41F530(int nSlot, weapon_id_t bWeapon, BYTE bAnim)
{
  if (bWeapon == WPN_NONE)
    return FALSE;

  BYTE bCooldown = (&bUnion54)[nSlot];
  if (bCooldown) {
    if (bUnion85 > bCooldown - 1)
      bUnion85 = bCooldown - 1;
    return FALSE;
  }

  if (!IsTargetInWeaponRange(bWeapon))
    return FALSE;

  bMoveFlags |= 8;

  (&bUnion54)[nSlot] = GetWeaponCD(bWeapon) + ((BYTE)(net_rand(12) & 3) - 1);

  if (nSlot == 1)
    _unmapped_55[1] = _unmapped_55[0];
  else if (nSlot == 2)
    _unmapped_55[0] = _unmapped_55[1];

  pSprite->SetAnimation(bAnim, 1);
  return TRUE;
}

// CUnit::AttackWith
// The argument picks the column, 1 meaning ground and anything else air. The body stores the
// chosen weapon id back over its own parameter, and both MakeBullet calls push the whole
// dword; that aliasing is 1.17's. The ground arm writes GetWeaponGndEx out, where
// CUnit::sub_420AB0 and CUnit::IsUnitInWeaponRange must call it: there the sentinel would
// fold into their `== 130` test. Do not make either site match the other.
void CUnit::AttackWith(BYTE bGroundArm)
{
  weapon_id_t bWeaponId;

  if (!pOrderTarget)
    return;

  if (bGroundArm == 1) {
    if (wType == 0x67 && !(dwFlags & 0x10))
      bWeaponId = WEAPON_NONE;
    else
      bWeaponId = gubUnitWeaponGnd[wType];
  }
  else {
    bWeaponId = gubUnitWeaponAir[wType];
  }

  // Hyperion: the id is no longer stored over the BYTE parameter, which would cut a mod's
  // weapon
  MakeBullet(bWeaponId);

  if (gnWeapLaunch[bWeaponId] == 2)
    MakeBullet(bWeaponId);
}

// CUnit::sub_41F630
// The 0x8000 guard is a plain early return. The two calls pass the anim argument
// differently, and both spellings matter: the air path passes `(BYTE)(nArg + 1)`, the ground
// path forwards `nArg` plain, its upper bytes untouched. dwFlags is read once into a local,
// kept in ECX across both tests.
void CUnit::sub_41F630(int nArg)
{
  sub_40A9E0();

  DWORD dwF = dwFlags;
  if (dwF & 0x8000)
    return;

  if (pOrderTarget && (pOrderTarget->dwFlags & 4)) {
    sub_41F530(2, gubUnitWeaponAir[wType], (BYTE)(nArg + 1));
    return;
  }

  weapon_id_t bWeapon;
  if (wType == 0x67 && !(dwF & 0x10))
    bWeapon = WPN_NONE;
  else
    bWeapon = gubUnitWeaponGnd[wType];

  sub_41F530(1, bWeapon, nArg);
}

// CUnit::sub_41FA00 -- oracle 0x41FA00 (158 B). The unit's chance-to-hit
// modifier: 0 when flag 4 is set, 0xFF when a unit of type 0xCA stands inside
// its bounds, else 0x77 when the cell under the sprite has flag 0x100000.
// `(short)wPos / 32` is signed division (the cdq / and 1Fh bias), and the
// WORD locals reproduce the movzx-then-movsx pair. re/attempts/CUnit__sub_41FA00.md
int CUnit::sub_41FA00()
{
  rect16_t bounds;

  if (dwFlags & 4)
    return 0;

  if (!(gulUnitTypeFlags[wType] & 1) && count_get_completion(0xCA, 11)) {
    GetBounds(&bounds);
    if (sub_432070(&bounds, 0xCA))
      return 0xFF;
  }

  upos_t wPosY = pSprite->wPosY;
  upos_t wPosX = pSprite->wPosX;
  return (mtx_flags(PosS(wPosX) / 32, PosS(wPosY) / 32) & 0x100000) ? 0x77
                                                                      : 0;
}

// CUnit::GetToughness
// `int`, not DWORD: both terms come from `sar`. One expression, `A + (cond ? B : 0)`, not two
// returns. The asymmetric rounding is 1.17's: hit points are rounded up before the shift and
// shields are not. +0x60 is read signed with `(long)`: CUnit.h declares it DWORD because
// CUnit::AssignShieldGain needs it unsigned.
// noinline: /Ob2 would inline this into IsAttackable, which the oracle calls.
__declspec(noinline) int CUnit::GetToughness()
{
  return ((lHitPoints + 0xFF) >> 8) +
         (gbUnitHasShields[wType] ? ((LONG)lShields >> 8) : 0);
}

// CUnit::HoldOrder
// Nine unit type ids map to five order ids. The result is a byte: four exits write AL alone
// and leave the widened wType above it. Eight compares fall into four
// `if (a || b) return X;` pairs; the ninth is a trailing
// `return wType == 0x22 ? 0xB2 : 0x6B;`, a cmov. The constants stay raw: nothing here decodes
// them.
BYTE CUnit::HoldOrder()
{
  if (wType == 0x48 || wType == 0x52)
    return 0x39;
  if (wType == 0x53 || wType == 0x51)
    return 0x3E;
  if (wType == 0x2D || wType == 0x31)
    return 0x6C;
  if (wType == 0x2F || wType == 0x32)
    return 0x88;
  return wType == 0x22 ? 0xB2 : 0x6B;
}

// CUnit::KillScore
// The return is 32-bit: a movzx defines all of EAX, where a WORD return emits a 16-bit mov.
// wType and the guwUnitScoreKill element are both WORD.
int CUnit::KillScore() {
  return guwUnitScoreKill[wType];
}

// CUnit::GetWeaponAir
// A unit whose own Air Weapon column is empty answers with its turret's instead, which is how
// a tank or goliath reports the weapon that actually fires; with no subunit it answers the
// sentinel. The result is one signed byte, `char`: it is tested against -0x7E, 0x82 signed.
// 0x82 is 130, weapons.dat's row count, so valid ids are 0 to 129.
weapon_id_t CUnit::GetWeaponAir()
{
  weapon_id_t bWeapon = gubUnitWeaponAir[wType];
  if (bWeapon != WEAPON_NONE)
    return bWeapon;
  if (!subunit)
    return WEAPON_NONE;
  return gubUnitWeaponAir[subunit->wType];
}

// CUnit::GetWeaponCD -- oracle 0x41FB70 (97 B)
//
// The cooldown is unsigned throughout: `shr` three times, never `sar`, and
// `cmovb` three times, never `cmovl`. The three clamps are two ternary shapes
// and the polarity is the tell -- `mov eax, K / cmp x, K / cmovb eax, x` seeds
// the destination with the constant and overwrites it with the variable, so the
// variable is the true arm, a min, where seeding with the variable gives a max.
//
// The speed modifier is signed and three-valued, `jns` then `jle` splitting it
// into negative, zero and positive, so GetAttackSpeedModifier is declared
// `int`. bCrunchyCount is an `int` local, widened once at the load and
// used whole by `test` and `imul`. The last clamp has to be the return
// expression and not an assignment plus a cast: as two statements MSVC
// materialises the (BYTE) conversion as a real `movzx` and runs the whole chain
// in ECX/EDX.
BYTE CUnit::GetWeaponCD(weapon_id_t bWeapon)
{
  int nSpores = bCrunchyCount;
  unsigned int nCooldown = gnWeapCooldown[bWeapon];

  if (nSpores) {
    unsigned int nStep = nCooldown >> 3;
    nStep = (nStep < 3) ? 3 : nStep;
    nCooldown += nStep * nSpores;
  }

  int nModifier = GetAttackSpeedModifier();
  if (nModifier < 0)
    nCooldown += nCooldown >> 2;
  else if (nModifier > 0)
    nCooldown >>= 1;

  nCooldown = (nCooldown < 250) ? nCooldown : 250;
  return (nCooldown < 5) ? 5 : nCooldown;
}

// CUnit::GetWeaponGnd -- oracle 0x41FBE0 (49 B)
//
// The ground twin of CUnit::GetWeaponAir above with one extra guard in front:
// everything below the guard is that body over gubUnitWeaponGnd, with the same
// fall-through to the subunit's column, the same -0x7E sentinel and the same one
// level of indirection. `char` and not BYTE, since the sentinel is tested
// `cmp al, -7Eh` and the result is AL alone.
//
// The guard's nesting is in its branch targets: both `jnz` leave for the main
// body, so the two tests are the short-circuit `&&` of one condition whose
// then-arm is the -0x7E return, and the fall-through of the pair is the early
// exit. That tail is reached from two places, the guard falling into it and the
// null-subunit test jumping backwards, so the source has three textual
// `return WEAPON_NONE;` and MSVC merges the two identical blocks. Unit type 0x67 and
// dwFlags bit 4 are not decoded here.
weapon_id_t CUnit::GetWeaponGnd()
{
  if (wType == 0x67 && !(dwFlags & 0x10))
    return WEAPON_NONE;

  weapon_id_t bWeapon = gubUnitWeaponGnd[wType];
  if (bWeapon != WEAPON_NONE)
    return bWeapon;
  if (!subunit)
    return WEAPON_NONE;
  return gubUnitWeaponGnd[subunit->wType];
}

// CUnit::GetWeaponGndEx
// Unit type 0x67, the Lurker, reports "no weapon" (-0x7E, as a signed `char`) while not
// burrowed; every other unit, and a burrowed Lurker, reports its units.dat Ground Weapon
// column. The test is one joined condition, `wType == 0x67 && !(dwFlags & 0x10)`. The body is
// inline in src117/h/WeaponGndEx.h, so CImage.cpp's RunScript can compile against it
// (decision 30).

// CUnit::AddKill
// bKillCount is an unsigned BYTE and the increment saturates at 255, so a long-lived unit's
// count sticks there. An Interceptor (0x49) also credits its carrier: a tail call
// `pPowerup->AddKill()` under `wType == 0x49 && pPowerup`, the type tested first. +0xC0 is the
// carrier on this arm; the field is named pPowerup because the two union arms share it.
void CUnit::AddKill()
{
  if (bKillCount < 0xFF)
    ++bKillCount;
  if (wType == 0x49 && pPowerup)
    pPowerup->AddKill();
}

// Deals this unit's ground-weapon damage to its order target, or only notifies it.
// The owner byte and the upgrade level go through BYTE locals and the bonus is
// computed inside the Damage call: that gives the target EDX and the weapon CL,
// and pushes the other arguments before the damage sum, as the oracle does.
void CUnit::HitMelee()
{
  if (pOrderTarget != 0) {
    weapon_id_t bWeapon = GetWeaponGndEx();
    if (dwFlags & 0x40000000) {
      pOrderTarget->DamageNotify(bWeapon, bUnion21, this);
    } else {
      BYTE bP = bPlayer;
      BYTE bLevel = get_upgrade_level(bP, wpn_dmgUpg_g[bWeapon]);
      pOrderTarget->Damage(((WORD)(bLevel * wpn_dmgBonus_g[bWeapon]) + wpn_dmgAmt_g[bWeapon]) << 8,
                           bWeapon, 1, bUnion21, this, bPlayer);
    }
  }
}

// CUnit::GetChanceToHit -- oracle 0x41FD10 (114 B)
//
// A miss chance out of 256, not a hit chance: an attacker on lower ground than
// its target gets at least 0x77, BW's high-ground miss rate, where air units on
// either side skip the terrain rule and equal or higher ground leaves the
// target's own value alone. The first call is on the argument, not on `this`,
// and precedes both flag tests, so the base chance is the target's own property.
//
// The sprite reads are TrigEnum_HeightMatches' idiom exactly, including its two
// halves: `short` locals rather than a cast inside the call, 1.17 loading with
// `movzx` for the field's width and converting with `movsx` for the local's
// signedness where an inline cast fuses both into one instruction, and Y
// declared before X, which is what makes MSVC materialise +0x16 first. Both
// comparisons are unsigned on the opcodes alone, `jnb` and `cmova`, where
// signed spellings give `jge` and `cmovg`.
int CUnit::GetChanceToHit(CUnit *pTarget)
{
  unsigned int nChance = pTarget->sub_41FA00();

  if ((dwFlags & 4) || (pTarget->dwFlags & 4))
    return nChance;

  spos_t y = PosS(pSprite->wPosY);
  spos_t x = PosS(pSprite->wPosX);
  unsigned int nHeight = get_map_height(x, y);

  spos_t ty = PosS(pTarget->pSprite->wPosY);
  spos_t tx = PosS(pTarget->pSprite->wPosX);
  if (nHeight < (unsigned int)get_map_height(tx, ty))
    return (nChance > 0x77) ? nChance : 0x77;

  return nChance;
}

// CUnit::IsAttackable -- oracle 0x41FD90 (135 B)
//
// The argument slot is a local, and the body says so: EDI is loaded from the
// slot and then reassigned from pOrderTarget on the null path, so everything
// after the first guard reads the survivor.
//
// The nesting is in the branch targets, twice. The `jnz` past the first
// `test edi, edi` is not an early exit but the shared continuation, so the null
// test is a fallback and only the second test rejects; and the `jz` past
// CanTarget is a second chance, the bunker case, so CanTarget failing does not
// end the function. `neg / sbb / inc` on the sub_420AB0 result is `== 0` and not
// a truth test: the out-of-range predicate is inverted, so the target is
// attackable when it is not out of range. 0x7D is the bunker, left raw, and
// `return FALSE;` is the trailing statement all five rejections fold onto.
BOOL CUnit::IsAttackable(CUnit *pTarget)
{
  if (!pTarget) {
    pTarget = pOrderTarget;
    if (!pTarget)
      return FALSE;
  }

  if (!IsEnemyUnit(pTarget))
    return FALSE;

  if (pTarget->dwFlags & 0x40000000) {
    int nToughness = pTarget->GetToughness();
    if (nToughness != pTarget->MaxToughness())
      return FALSE;
  }

  if (pTarget->CanTarget(this, 1))
    return pTarget->sub_420AB0(this) == 0;

  if (pTarget->wType == 0x7D && pTarget->IsUnitInside())
    return TRUE;

  return FALSE;
}

// CUnit::IsDead
// A unit with no sprite is already gone; a unit running any order other than id 0 is not
// dead; order id 0, the die order, reports dead only once its step counter has reached 1.
// The trailing two guards are joined with `&&` inside one `if`; flat returns or an else-if
// chain let MSVC fold them into flag arithmetic. The member is non-const: a const one is a
// different function from the one CUnit::FromIdAlive calls.
BOOL CUnit::IsDead()
{
  if (pSprite == 0)
    return TRUE;
  if (bOrderId == 0 && bOrderState == 1)
    return TRUE;
  return FALSE;
}

// CUnit::IsDeadOrDying
// A unit with no sprite is already gone; otherwise it is dying exactly when it is running
// order id 0, the die order. It lacks IsDead's `bOrderState == 1`, so a unit one instant into
// the die order is dead-or-dying here and not yet dead there. Here the flat
// `return bOrderId == 0;` is the right source: 1.17 emits setz.
BOOL CUnit::IsDeadOrDying()
{
  if (pSprite == 0)
    return TRUE;
  return bOrderId == 0;
}

// CUnit::IsInAttackAngle
// Both coordinates are loaded movzx then movsx, through named WORD intermediates declared y
// then x; a cast in the argument folds each pair into one movsx. The widening matters here
// because the coordinates feed AngleBetween's `int` parameters. The comparison is signed, on
// int, and the result is 0 or 1.
BOOL CUnit::IsInAttackAngle(weapon_id_t bWeapon, int x, int y)
{
  upos_t wY = pSprite->wPosY;
  upos_t wX = pSprite->wPosX;
  int nAngle = AngleBetween(PosS(wX), PosS(wY), x, y);
  if (wType == 0x67) {
    bUnion21 = (BYTE)nAngle;
    return TRUE;
  }

  int nDiff = bUnion21 - nAngle;
  if (nDiff < 0)
    nDiff += 0x100;
  if (nDiff > 0x80)
    nDiff = 0x100 - nDiff;

  return nDiff <= wpn_attackangle_g[bWeapon];
}

// CUnit::IsInfestable
// A Queen infests a damaged Command Center: type 0x6A below 50 percent of its units.dat max
// HP, tested signed, so every intermediate is `int`. The table is indexed with the literal 106.
// The rounding `(lHitPoints + 0xFF) >> 8`, CUnit::GetLife inlined, is written out twice, and
// the fallback is `nMaxLife = 1; if (nCur) nMaxLife = nCur;`, not a ternary. A zero max-HP
// row never happens, so that arm is dead code.
BOOL CUnit::IsInfestable()
{
  if ((dwFlags & 1) && wType == 0x6A) {
    int nMaxLife = HandicapLife(gxUnitLife[0x6A], bPlayer) >> 8;  // Hyperion: at the owner's handicap
    if (!nMaxLife) {
      int nCur = (lHitPoints + 0xFF) >> 8;
      nMaxLife = 1;
      if (nCur)
        nMaxLife = nCur;
    }

    if (((lHitPoints + 0xFF) >> 8) * 100 / nMaxLife < 50)
      return TRUE;
  }

  return FALSE;
}

// CUnit::IsQueenType
// Both exits define all of EAX, so BOOL, where HoldOrder's arms write AL alone. Both compares
// jump to one block, `if (a || b) return TRUE;`. wType is read at 16 bits. 1.17 treats the
// two ids as one kind, and HoldOrder maps both to hold order 0x6C.
BOOL CUnit::IsQueenType()
{
  return wType == UNI_T_QUEEN || wType == UNI_T_MATRIARCH;
}

// CUnit::IsTargetInWeaponRange
// One BYTE argument, forwarded whole to two callees that take a BYTE too. Both range
// comparisons are unsigned, so the distance and both bounds are DWORD; the minimum-range test
// is skipped when the column is zero, so the zero test comes first in the `&&`. The two (WORD)
// casts on the distance call matter: the fields are `short`. The body also writes
// bUnion85 = 0 on the out-of-arc path, a side effect in a predicate.
BOOL CUnit::IsTargetInWeaponRange(weapon_id_t bWeapon)
{
  if (bMoveFlags & 8)
    return FALSE;

  DWORD dwDistance;
  if (pOrderTarget)
    dwDistance = GetDistanceTo_CUnit(pOrderTarget);
  else
    dwDistance = GetDistanceTo_xy(PosU(wOrderTargetX), PosU(wOrderTargetY));

  DWORD dwMinRange = wpn_minrange_g[bWeapon];
  if (dwMinRange && dwDistance < dwMinRange)
    return FALSE;

  if (dwDistance > GetWeaponRange(bWeapon))
    return FALSE;

  if (bPathingFlags & 2)
    return FALSE;

  CFlingy *pFlingy = reinterpret_cast<CFlingy *>(this);
  if (PosU(pFlingy->wDestinationX) == wPositionX &&
      PosU(pFlingy->wDestinationY) == wPositionY)
    return TRUE;

  if (!IsInAttackAngle(bWeapon, pFlingy->wDestinationX,
                       pFlingy->wDestinationY)) {
    bUnion85 = 0;
    return FALSE;
  }

  return TRUE;
}

// CUnit::IsTurret
// Two guards and one failure block: a short-circuit `&&` reaching a single `return FALSE`.
// Only bit 4 of the DWORD table is tested, at byte width; the source indexes the DWORD table.
BOOL CUnit::IsTurret(CUnit *pUnit)
{
  if (pUnit && (gulUnitTypeFlags[pUnit->wType] & 0x10))
    return TRUE;
  return FALSE;
}

// CUnit::MakeBullet
// Works out where this weapon's bullet is born -- a position sub_420970 computes for a weapon
// that needs a live order target, the order-target coordinates, or a point projected out of
// the unit's sprite along its facing -- and allocates it there. Two `int` locals have their
// addresses taken, and `int nWeaponId = bWeaponId;` keeps the widened id in EBX. The X read
// takes an explicit (short), and the wpn_gfx_g gate covers only the allocation.
void CUnit::MakeBullet(weapon_id_t bWeaponId)
{
  int nWeaponId = bWeaponId;
  int nX, nY;

  BYTE bFireType = gubWeapFireType[nWeaponId];
  if (bFireType == 2) {
    if (!pOrderTarget)
      return;
    sub_420970(&nX, &nY);
  } else if (bFireType == 4) {
    nX = wOrderTargetX;
    nY = wOrderTargetY;
  } else {
    reinterpret_cast<CFlingy *>(this)->GetVectorComponents(
        bUnion21, wpn_forwardOffset_g[nWeaponId], &nX, &nY);
    nX += PosS(pSprite->wPosX);
    nY += pSprite->wPosY - wpn_upwardOffset_g[nWeaponId];
  }

  if (wpn_gfx_g[nWeaponId])
    CBullet::AllocBullet(nWeaponId, nX, nY, bPlayer, bUnion21, this);
}

// CUnit::MaxToughness
// The unit type's maximum toughness: units.dat hit points plus, where the type has shields,
// its units.dat shield amount, falling back to this instance's current hit points when the
// type's entry reads 0. Signed, like CUnit::GetToughness, and the shield term is added once
// after both paths. The table hit points are truncated while the fallback is rounded up and
// floored at 1, `(n != 0 ? n : 1)`: 1.17's asymmetry.
int CUnit::MaxToughness()
{
  // The barrier keeps IsAttackable's calls in source order (Get, then Max).
  _ReadWriteBarrier();
  // Hyperion: at the owner's handicap
  int nShields = gbUnitHasShields[wType] ? HandicapShields(guwUnitEnergy[wType], bPlayer) : 0;

  int nLife = HandicapLife(gxUnitLife[wType], bPlayer) >> 8;
  if (nLife != 0)
    return nLife + nShields;

  int nHitPoints = (lHitPoints + 0xFF) >> 8;
  return (nHitPoints != 0 ? nHitPoints : 1) + nShields;
}

// CUnit::sub_420120 -- oracle 0x420120 (111 B)
//
// Adopts pTarget as this unit's auto-attack target if CUnit::sub_4212B0 prefers
// it over the one already held, and pushes the same target, and the +0x90 byte,
// down into the turret. Four unit-type and flag guards and CanAttack gate it.
// One CUnit* argument, void.
//
// Five guards, one shared exit, all five jumping to the same place, so this is
// one `||` chain and not nested `if`s. The store to dwUnion7C sits before the
// IsTurret test, not inside its then-arm; putting it inside costs the guard.
// The subunit is read twice and cached once, EBX holding it across IsTurret
// while the last store reloads it, which is what writing the field fresh at
// every mention produces. +0x90 is unnamed in CUnit.h and reached through a
// byte cast; what 1.17 states is its width and its direction, one byte copied
// from this unit to the turret alongside the same target pointer.
void CUnit::sub_420120(CUnit *pTarget)
{
  if (wType == 0x47 || wType == 0x56 || (gulUnitTypeFlags[wType] & 8) ||
      (dwFlags & 0x10) || !CanAttack())
    return;

  if (!sub_4212B0(reinterpret_cast<CUnit *>(dwUnion7C), pTarget))
    return;

  CUnit *pTurret = subunit;
  dwUnion7C = reinterpret_cast<DWORD_PTR>(pTarget);
  if (IsTurret(pTurret)) {
    pTurret->bUnion90 = bUnion90;  // Hyperion: was +0x90 by number
    subunit->dwUnion7C = reinterpret_cast<DWORD_PTR>(pTarget);
  }
}

// CUnit::NewTarget -- oracle 0x4202C0 (264 B), __fastcall(ECX=source unit,
// EDX=target), ret 8h.
//
//   * Type 0x49 redirects through the +0xC0 union arm with a cmov when nonzero.
//   * gubUnitOrderAttackUnit supplies the attack order id per wType; index 5,
//     1.17's absolute 0x52100D read, is the interrupt-order id of the
//     SetOrder_CUnit_interrupt tail -- an element of the registered BYTE[228],
//     not a global of its own.
//   * Flag 0x8000000 at dwFlags marks "recently attacked" for the source, its
//     subunit, pSub and pSub's subunit -- written twice, as 1.17 does.
//   * Tail dispatch needs dwUnion134 != 0, wType == 0x1E and bOrderId != 0x63:
//     IsUnitInWeaponRange true returns silently, else HasEnemyInWeaponRange
//     false gives SetOrder(0x63) and the interrupted order chain, true gives
//     tai_call_support_units or AIPickAttackTarget(1,1,0) on the fourth
//     argument.
void __fastcall CUnit::NewTarget(CUnit *pSource, CUnit *pTarget, int a3, int a4)
{
    if (pTarget->wType == 0x49) {
        if (pTarget->pUnionC0)
            pTarget = pTarget->pUnionC0;
    }

    pSource->SetOrder_CUnit(gubUnitOrderAttackUnit[pSource->wType], pTarget);

    if (a3) {
        pSource->dwFlags |= 0x8000000;
        if (pSource->subunit)
            pSource->subunit->dwFlags |= 0x8000000;
    }

    CUnit *pSub = pSource->subunit;
    if (pSub && (gulUnitTypeFlags[pSub->wType] & 0x10)) {
        pSub->SetOrder_CUnit(gubUnitOrderAttackUnit[pSub->wType], pTarget);
        if (a3) {
            pSub->dwFlags |= 0x8000000;
            if (pSub->subunit)
                pSub->subunit->dwFlags |= 0x8000000;
        }
    }

    if (!pSource->dwUnion134)
        return;
    if (pSource->wType != 0x1E || pSource->bOrderId == 0x63)
        return;

    if (!pSource->IsUnitInWeaponRange(pTarget)) {
        if (pSource->HasEnemyInWeaponRange()) {
            if (a4) {
                tai_call_support_units(pSource, pTarget);
                return;
            }
            pSource->AIPickAttackTarget(1, 1, 0);
            return;
        }
        pSource->SetOrder(0x63);
        pSource->SetOrder_CUnit_interrupt(
            gubUnitOrderAttackUnit[5], pTarget, FALSE);
    }
}

// CUnit::OnDamaged -- oracle 0x4203D0 (335 B), ret 8h.
//
//  * The +0x90 byte stores the attacker's player for both this unit and its
//    turret subunit; the subunit copy runs only when dwUnion134 != 0,
//    dwUnion7C == 0 and IsTurret(subunit).
//  * dwUnion7C is the last-attacker slot: assigned the attacker pointer, then
//    re-read at the tail for sub_4212B0's first argument.
//  * bNotify gates only the visible-check and warn block; the pUnion80
//    redirect, sub_4212B0 and sub_420190 sit outside it -- inside, they cost a
//    displacement byte and skipped the notify pair.
//  * The sixth sub_46BBB0 argument is the live IsVisibleTo result, still in EAX
//    and pushed unmodified, so passing bVisible keeps that reuse.
//  * The rect16_t fields are assigned left, right, top, bottom in stream order,
//    the x-pair then the y-pair, not natural left, top, right, bottom.
//  * dwUnion134 == 0 is re-tested separately for the unit-list section.
void CUnit::OnDamaged(CUnit *pAttacker, int bNotify)
{
    if (!pAttacker || pAttacker->bPlayer == bPlayer)
        return;

    if (dwUnion134 != 0) {
        _unmapped_90[0] = pAttacker->bPlayer;
        if (dwUnion7C == 0) {
            CUnit *pSub = subunit;
            dwUnion7C = (DWORD_PTR)pAttacker;
            if (IsTurret(pSub)) {
                pSub->_unmapped_90[0] = _unmapped_90[0];
                subunit->dwUnion7C = (DWORD_PTR)pAttacker;
            }
        }
    }
    else {
        _unmapped_90[0] = pAttacker->bPlayer;
    }

    tai_unit_was_attacked(this, pAttacker, 1, 0);

    if (bNotify) {
        BOOL bVisible = pAttacker->IsVisibleTo(this);
        if (!bVisible && bPlayer < 8) {
            CSprite *pSpr = pAttacker->pSprite;
            sub_46BBB0((int)PosS(pSpr->wPosX), (int)pSpr->wPosY,
                       (pAttacker->dwFlags >> 2) & 1,
                       1 << bPlayer, TRUE, bVisible);
        } else if (!bVisible && g_hxPlayers && PlayerIsLobby(bPlayer)) {
            // Hyperion: the slots past 7 see who hit them too
            CSprite *pSpr = pAttacker->pSprite;
            MaskStampHx((int)PosS(pSpr->wPosX), (int)pSpr->wPosY,
                        (pAttacker->dwFlags >> 2) & 1, PlayerBit(bPlayer), TRUE);
        }
    }

    if ((pAttacker->dwFlags & 0x20))
        pAttacker = pAttacker->pUnion80;

    if (this->sub_4212B0((CUnit *)dwUnion7C, pAttacker))
        sub_420190(pAttacker);

    if (dwUnion134 == 0) {
        CSprite *pSpr = pSprite;
        mrect_t  rect;
        rect.left   = pSpr->wPosX - 0x60;
        rect.right  = pSpr->wPosX + 0x60;
        rect.top    = pSpr->wPosY - 0x60;
        rect.bottom = pSpr->wPosY + 0x60;

        CUnit **ppList = get_units_in_rect(&rect);
        CUnit **ppIt = ppList;
        if (*ppList) {
            while (*ppIt) {
                CUnit *pOther = *ppIt;
                if (pOther != this && pOther->bPlayer == bPlayer)
                    pOther->sub_420120(pAttacker);
                ++ppIt;
            }
        }
        finish_unit_iteration(ppList);
    }
}

// Spawns the flame overlay thingy in front of a firing unit, at its turret's overlay offset.
// Each arm calls AllocThingy and MSVC merges the two calls; one shared call loses the
// per-arm adds. The two offsets are separate locals, and the coordinates are WORD locals.
void CUnit::sub_420520()
{
  int nFace;
  CThingy *pThingy;
  shield_offset_t offA, offB;
  if (wType != 0x20 && wType != 0x0A) {
    int nRot = ((bUnion21 + 16) >> 5) & 7;
    nFace = nRot * 2;
    pUnion80->pSprite->pMainImage->GetOverlayOffset(&offA, 0, nRot);
    upos_t wY = pSprite->wPosY;
    upos_t wX = pSprite->wPosX;
    pThingy = CThingy::AllocThingy(0x179, PosS(wX) + offA.x, PosS(wY) + offA.y, 0);
  } else {
    int nDir = bUnion21;
    nFace = ((nDir + 8) >> 4) & 0xF;
    pUnion80->pSprite->pMainImage->GetOverlayOffset(&offB, 0, ((nDir + 16) >> 5) & 7);
    upos_t wY = pSprite->wPosY;
    upos_t wX = pSprite->wPosX;
    pThingy = CThingy::AllocThingy(0x17A, PosS(wX) + offB.x, PosS(wY) + offB.y, 0);
  }
  if (pThingy) {
    pThingy->pSprite->bElevationLevel = pSprite->bElevationLevel + 1;
    pThingy->pSprite->SetFacing(nFace << 4);
    pThingy->UpdateVisibility();
  }
}

// CUnit::sub_420600 -- oracle 0x420600 (212 B)
//
// The five rejections are one `&&` chain, not five early returns, and it is
// worth 8 bytes: written as guards, MSVC makes the `return TRUE` block the
// fall-through of the first test and places it early, keeping every later
// rejection inside `jcc rel8` range, at 204 bytes. 1.17 puts that block last, so
// its first three rejections need `jcc rel32`, four bytes more each, and the
// shorter tail gives four back -- `return TRUE;` is the trailing statement.
//
// dwUnion7C is re-read three times, not cached: a local would have to survive
// the IsEnemyUnit call and the frame has nowhere to put it. `cmp dword ptr
// [esi+134h], eax` compares against EAX because EAX is known zero, IsVisibleTo
// having just returned false. The clear `dwUnion7C = 0` sits after the field has
// been pushed as PrependOrder_CUnit's second argument; hoisting it would hand it
// a null pointer.
BOOL CUnit::sub_420600(int nOrderId)
{
  if (bOrderState == 0) {
    if (!SetDestination_xy(wOrderTargetX, wOrderTargetY))
      return FALSE;
    bOrderState = 1;
  }

  if (dwUnion7C != 0 && IsEnemyUnit((CUnit *)dwUnion7C) &&
      !(((CUnit *)dwUnion7C)->pSprite->flags.b & 0x20) &&
      !(((CUnit *)dwUnion7C)->dwFlags & 0x4000000) &&
      !((CUnit *)dwUnion7C)->IsVisibleTo(this)) {
    if (dwUnion134 != 0)
      return AIPickAttackTarget(0, 0, 0) == 0;

    Stop();
    SetNextOrder_Target(nOrderId, OrderTarget());
    PrependOrder_CUnit(gubUnitOrderAttackUnit[wType], (CUnit *)dwUnion7C,
                       (COrder *)pUnion74);
    dwUnion7C = 0;
    EndUninterruptableDispatchOrder();
    HoldPosition();
    return FALSE;
  }

  return TRUE;
}

// CUnit::sub_4206E0
// SetNextOrder_Target's second parameter is a packed 4-byte position, read as one dword from
// wOrderTargetX and wOrderTargetY, as at all three of its call sites. There is no 4-byte
// point type, so the call is spelled `*(DWORD *)&wOrderTargetX`.
void CUnit::sub_4206E0(int nOrderId)
{
  if (!EnemyTrackingRange())
    return;

  if (dwUnion134) {
    AIPickAttackTarget(0, 0, 0);
    return;
  }

  CUnit *pTarget = FindAttackTarget();
  if (!pTarget)
    return;

  Stop();
  SetNextOrder_Target(nOrderId, OrderTarget());
  PrependOrder_CUnit(gubUnitOrderAttackUnit[wType], pTarget, pOrderQueueHead);
  EndUninterruptableDispatchOrder();

  dwFlags |= 0x8000000;
  if (subunit)
    subunit->dwFlags |= 0x8000000;
}


// True when this unit's ground weapon is none (0x82) or has a flingy.
// A helper: the oracle materialises its result with `setne` and tests it.
static __forceinline BOOL s770_has_gfx(int nType)
{
  weapon_id_t bWeapon = gubUnitWeaponGnd[nType];
  if (bWeapon == WEAPON_NONE)
    return TRUE;
  return wpn_gfx_g[bWeapon] != 0;
}

// Keeps chasing the order target: retargets or stops when it can no longer be attacked,
// otherwise moves toward it until it is in range. TRUE means the order may continue.
// The Stop arm is written first: its `return FALSE` is the one the later ones share.
BOOL CUnit::sub_420770()
{
  if (!CanTarget(pOrderTarget, 1)) {
    CUnit *pTarget = pOrderTarget;
    if (!pTarget || (dwFlags & 0x8000000)) {
      Stop();
      EndDispatchOrder();
      return FALSE;
    }
    if (!dwUnion134)
      SetNextOrder_xy(6, PosS(pTarget->pSprite->wPosX), pTarget->pSprite->wPosY);
    EndDispatchOrder();
    return FALSE;
  }
  wOrderTargetX = pOrderTarget->pSprite->wPosX;
  wOrderTargetY = pOrderTarget->pSprite->wPosY;
  reinterpret_cast<CFlingy *>(this)->SetDestination(wOrderTargetX, wOrderTargetY);
  if (!bOrderState) {
    if (IsUnitInWeaponRange(0) || SetMoveTarget_CUnit(pOrderTarget)) {
      bOrderState = 1;
      return TRUE;
    }
    return FALSE;
  }
  if (sub_420AB0(pOrderTarget)) {
    if (dwUnion134) {
      if (sub_421320(this, pOrderTarget) || AtMoveTarget() == 2) {
        if (AIPickAttackTarget(0, 1, 0))
          return FALSE;
        StopUnitMovement();
        EndDispatchOrder();
        return FALSE;
      }
    } else if (((dwFlags & 0x8000000) && !(dwFlags & 0xC) && (pOrderTarget->dwFlags & 4)) ||
               AtMoveTarget() == 2) {
      StopUnitMovement();
      EndDispatchOrder();
      return FALSE;
    }
    if (wType != 0x2F && s770_has_gfx(wType)) {
      ApproachMoveTarget_CUnit(pOrderTarget);
      return FALSE;
    }
    ApproachAttackTarget(pOrderTarget);
    return FALSE;
  }
  if (wType != 0x2F) {
    Stop();
    return TRUE;
  }
  ApproachAttackTarget(pOrderTarget);
  return TRUE;
}

// CUnit::BreakAnim -- oracle 0x420910 (63 B)
//
// Clears dwFlags bit 13 and the sprite's own flag bit 7, then calls
// CFlingy::StopAnim with gpImageUnit and gpImageTargeter pointed at this unit
// and put back afterwards. ECX survives untouched to the call, so StopAnim
// takes `this` as its own, through the reinterpret_cast CUnit not deriving from
// CFlingy forces.
//
// Which global lands in which callee-saved register follows the declaration
// order, and the restore order is strict LIFO around the call, 0x543304 first
// and 0x543300 last. Both saves are used exactly twice, so spill weight ties
// and definition order is the tie-break -- reversing the declarations reverses
// the registers and the addresses together.
void CUnit::BreakAnim() {
  dwFlags &= 0xFFFFDFFF;
  pSprite->flags.b &= 0x7F;
  CUnit *pSaveUnit = gpImageUnit;
  CUnit *pSaveTargeter = gpImageTargeter;
  gpImageUnit = this;
  gpImageTargeter = this;
  reinterpret_cast<CFlingy *>(this)->StopAnim();
  gpImageUnit = pSaveUnit;
  gpImageTargeter = pSaveTargeter;
}

// CUnit::ClearTargetIfAllied
// Forgets the order target unless it is an enemy; "allied" here is the complement of
// IsEnemyUnit, which includes a null target, since the call is made unconditionally:
// `this->IsEnemyUnit(this->pOrderTarget)`.
void CUnit::ClearTargetIfAllied() {
  if (!IsEnemyUnit(pOrderTarget)) {
    pOrderTarget = 0;
  }
}

#if !defined(OC_PAYLOAD) || defined(OC_WITH_HELD)
// Shrinks the order target's bounding box by a quarter on each side and clips the line from
// its centre to this unit; the answer is this unit's position when LineClip accepts it.
void CUnit::sub_420970(int *pnX, int *pnY)
{
  mrect_t  rc;
  mpoint_t  mid, self;
  mrect_t  box;
  CUnit *pTarget = pOrderTarget;
  upos_t wY = pTarget->pSprite->wPosY;
  upos_t wX = pTarget->pSprite->wPosX;
  box.top = wY - gUnitBoundBox[pTarget->wType].top;
  box.left = wX - gUnitBoundBox[pTarget->wType].left;
  box.bottom = gUnitBoundBox[pTarget->wType].bottom + wY;
  box.right = gUnitBoundBox[pTarget->wType].right + wX;
  int nQx = (box.right - box.left) >> 2;
  int nQy = (box.bottom - box.top) >> 2;
  rc.left = box.left += nQx;
  rc.top = box.top += nQy;
  // Barrier lever; it stays only if it proves the only matching shape in both forms.
  _ReadWriteBarrier();
  rc.right = box.right -= nQx;
  rc.bottom = box.bottom -= nQy;
  mid.x = (box.right - box.left) / 2 + rc.left;
  mid.y = (box.bottom - box.top) / 2 + rc.top;
  self.x = pSprite->wPosX;
  self.y = pSprite->wPosY;
  if (LineClip(&mid, &self, &rc)) {
    *pnX = self.x;
    *pnY = self.y;
  } else {
    *pnX = mid.x;
    *pnY = mid.y;
  }
}
#endif

// Retargets toward the point the move-target unit is heading for, three
// braking distances ahead, or to (a1, a2) when that point is off the map;
// the move-target unit survives SetTarget_xy.
// The heading is read before BreakingDistance so it stays in BL, and the
// fallback is assigned in both else arms rather than pre-seeded.
void CUnit::sub_420DB0(int a1, int a2)
{
  CUnit *pTargetUnit = reinterpret_cast<CFlingy *>(this)->pMoveTargetUnit;

  int x, y;
  if (pTargetUnit && (pTargetUnit->bMoveFlags & 2)) {
    BYTE bDir = pTargetUnit->bDirection;
    int nOffsetX, nOffsetY;
    reinterpret_cast<CFlingy *>(this)->GetVectorComponents(
        bDir, 3 * reinterpret_cast<CFlingy *>(this)->BreakingDistance(),
        &nOffsetX, &nOffsetY);
    CSprite *pSprite = pTargetUnit->pSprite;
    int nLeadX = PosS(pSprite->wPosX) + (nOffsetX >> 8);
    int nLeadY = PosS(pSprite->wPosY) + (nOffsetY >> 8);
    if ((unsigned int)nLeadX < gwMapPixWidth &&
        (unsigned int)nLeadY < gwMapPixHeight) {
      x = nLeadX;
      y = nLeadY;
    } else {
      x = a1;
      y = a2;
    }
  } else {
    x = a1;
    y = a2;
  }

  reinterpret_cast<CFlingy *>(this)->SetTarget_xy(x, y);
  reinterpret_cast<CFlingy *>(this)->pMoveTargetUnit = pTargetUnit;
}

// IsVulnerableType
// A free __fastcall function: ECX is compared as a 16-bit integer and used as a table index,
// never dereferenced. The bound is unsigned, so the parameter is WORD and the test `>= 228`,
// units.dat's entry count. The answer is `!(flags & 0x20000000)`; an out-of-range type
// answers 0, not vulnerable.
BOOL __fastcall IsVulnerableType(WORD wUnitType)
{
  if (!UnitTypeValid(wUnitType))  // Hyperion: >= 228, and a mod's types
    return FALSE;
  return !(gulUnitTypeFlags[wUnitType] & 0x20000000);
}

// CUnit::sub_420AB0
// "Is pTarget out of reach?" -- true when this unit cannot see it, false when it has no
// weapon for it, otherwise the negation of the range test. The turret selection picks the
// subunit when its type carries gulUnitTypeFlags bit 4; everything but GetWeaponRange runs on
// it. The ground arm calls CUnit::GetWeaponGndEx and must not write it out, unlike
// CUnit::AttackWith, so that definition stays above this body and IsUnitInWeaponRange.
BOOL CUnit::sub_420AB0(CUnit *pTarget)
{
  if (!CanSeeUnit(pTarget))
    return TRUE;

  CUnit *pAttacker = subunit;
  if (!pAttacker || !(gulUnitTypeFlags[pAttacker->wType] & 0x10))
    pAttacker = this;

  weapon_id_t bWeapon = (pTarget->dwFlags & 4) ? gubUnitWeaponAir[pAttacker->wType]
                                        : pAttacker->GetWeaponGndEx();

  if (bWeapon == WPN_NONE)
    return FALSE;

  return pAttacker->sub_421120(GetWeaponRange(bWeapon), pTarget) == 0;
}

// CUnit::sub_420B50 -- oracle 0x420B50 (120 B). "Target is too close": true
// when the chosen attacker's weapon has a minimum range at all and the target
// is inside it. sub_420AB0's exact sibling, minus its CanSeeUnit guard.
//
// The ground arm has to stay an ordinary call to GetWeaponGndEx, as it does
// there: MSVC 2013 splices the callee in at the end of the TU, after value
// numbering, so during const-prop the result is opaque and `cmp al, 82h` cannot
// be folded. Spelled inline here, the sentinel path folds and `mov al, 82h /
// jmp` disappears, four bytes short.
//
// The three guards are nested, not early returns, which is worth 3 bytes:
// written flat, MSVC sees three paths converging on `return FALSE` against one
// on `return TRUE`, recognises `return !!IsWithinTargetRange(...)` and
// boolean-normalises it, 117 B. Nesting keeps the two exit blocks apart, and
// `return FALSE;` stays the trailing statement, which puts that block last.
BOOL CUnit::sub_420B50(CUnit *pTarget)
{
  CUnit *pAttacker = subunit;
  if (!pAttacker || !(gulUnitTypeFlags[pAttacker->wType] & 0x10))
    pAttacker = this;

  weapon_id_t bWeapon = (pTarget->dwFlags & 4) ? gubUnitWeaponAir[pAttacker->wType]
                                        : pAttacker->GetWeaponGndEx();
  if (bWeapon != WPN_NONE) {
    int nMinRange = wpn_minrange_g[bWeapon];
    if (nMinRange != 0) {
      if (pAttacker->IsWithinTargetRange(nMinRange, pTarget))
        return TRUE;
    }
  }

  return FALSE;
}

// CUnit::GetDisplayBonusDamage -- oracle 0x420E40 (105 B)
//
// The bonus damage a stat panel shows: weapons.dat's Damage Bonus column times
// the researched level of that weapon's damage upgrade times the number of shots
// per attack. The sibling of CUnit::GetDisplayBaseDamage below, whose middle this
// body shares, with an upgrade-level lookup in front and a second `imul` behind.
//
// It is not the sibling's ternary, though. The sibling writes
// `BYTE bShots = (wType == 0x3A) ? 1 : bLaunch;`, a new local; written that way
// here MSVC multiplies by the shots first and by the level second, the reverse
// of 1.17, and no rewriting of the multiply reaches it, the product being
// commutative and canonicalised. What moves it is the shot count's own
// spelling: the in-place `if (wType == 0x3A) bLaunch = 1;` seeds the cmov's
// destination with the variable and moves the constant in, where a ternary into
// a new local seeds the other way, which also decides which `imul` is first.
WORD CUnit::GetDisplayBonusDamage(weapon_id_t bWeapon)
{
  BYTE bLaunch = gnWeapLaunch[bWeapon];
  BYTE bLevel = get_upgrade_level(bPlayer, wpn_dmgUpg_g[bWeapon]);

  if (wType == 0x20 || wType == 0x0A || wType == 0x41 || wType == 0x4D)
    bLaunch = 2;
  if (wType == 0x3A)
    bLaunch = 1;

  return wpn_dmgBonus_g[bWeapon] * bLevel * bLaunch;
}

// CUnit::GetDisplayBaseDamage
// The base damage a stat panel shows for this unit firing this weapon: weapons.dat's Damage
// Amount column times the number of shots per attack. The return is WORD: all four callers in
// statdata_ctx_status read AX alone. The four equality tests are one `||` chain in source
// order, and the ternary is byte-typed. The five unit-type ids stay raw and undecoded.
WORD CUnit::GetDisplayBaseDamage(weapon_id_t bWeapon)
{
  BYTE bLaunch = gnWeapLaunch[bWeapon];
  if (wType == 0x20 || wType == 0x0A || wType == 0x41 || wType == 0x4D)
    bLaunch = 2;

  BYTE bShots = (wType == 0x3A) ? 1 : bLaunch;
  return wpn_dmgAmt_g[bWeapon] * bShots;
}

// CUnit::IsUnitInWeaponRange
// The sister of CUnit::sub_420AB0 the other way up, "can this unit engage pTarget?", with the
// same turret selection and GetWeaponGndEx call. It differs in three places: a null argument
// defaults to pOrderTarget and a null default returns TRUE; the no-weapon test is an unsigned
// `>=` that returns FALSE, so a weaponless unit never indexes weapons.dat past its 130 rows;
// and it adds the minimum-range rejection and returns the range test unnegated.
BOOL CUnit::IsUnitInWeaponRange(CUnit *pTarget)
{
  if (!pTarget) {
    pTarget = pOrderTarget;
    if (!pTarget)
      return TRUE;
  }

  if (!CanSeeUnit(pTarget))
    return FALSE;

  CUnit *pAttacker = subunit;
  if (!pAttacker || !(gulUnitTypeFlags[pAttacker->wType] & 0x10))
    pAttacker = this;

  weapon_id_t bWeapon = (pTarget->dwFlags & 4) ? gubUnitWeaponAir[pAttacker->wType]
                                        : pAttacker->GetWeaponGndEx();

  if (!WeaponTypeValid(bWeapon))  // Hyperion: >= WPN_NONE, and a mod's weapons
    return FALSE;

  DWORD dwMinRange = wpn_minrange_g[bWeapon];
  if (dwMinRange && pAttacker->IsWithinTargetRange(dwMinRange, pTarget))
    return FALSE;

  return pAttacker->sub_421120(GetWeaponRange(bWeapon), pTarget);
}

// CUnit::sub_421120
// A range test with a lead allowance: when this unit is moving and either the target is not
// moving or the two headings differ by more than 0x20, the range is widened by the distance
// this unit could not stop within, in 8.8 fixed point. The parameters are (range, target); a
// null target is replaced by pOrderTarget in the parameter itself. get_angle's return and
// CFlingy::BreakingDistance are signed.
BOOL CUnit::sub_421120(int nRange, CUnit *pTarget) {
  if (!pTarget) {
    pTarget = pOrderTarget;
  }

  if ((bMoveFlags & 2) && (!(pTarget->bMoveFlags & 2) ||
      get_angle(bDirection, pTarget->bDirection) > 0x20)) {
    nRange += reinterpret_cast<CFlingy *>(this)->BreakingDistance() >> 8;
  }

  return IsWithinTargetRange(nRange, pTarget);
}

// CUnit::sub_4212B0
// __fastcall on a member: `this` in ECX, the first declared argument in EDX and one dword on
// the stack. The result is truth-tested, so BOOL. The backward branches are not a loop, only
// the shared failure block placed first. The IsUnitInWeaponRange arguments take no `(int)`
// cast: the parameter has pOrderTarget's type.

//
//         Behaviour: a target-preference predicate. FALSE when the two candidates
//         are the same unit, or pOther is not an enemy, or IsUnitInWeaponRange rejects
//         pOther, or it accepts pTarget. TRUE when pTarget is absent, not
//         targetable, or not an enemy -- i.e. when there is nothing worth
//         keeping -- and otherwise only when pOther passes the IsUnitInWeaponRange filter
//         that pTarget fails.
BOOL __fastcall CUnit::sub_4212B0(CUnit *pTarget, CUnit *pOther)
{
  if (pOther == pTarget)
    return FALSE;
  if (!IsEnemyUnit(pOther))
    return FALSE;
  if (!pTarget || !CanTarget(pTarget, 1) || !IsEnemyUnit(pTarget))
    return TRUE;
  if (!IsUnitInWeaponRange(pOther) || IsUnitInWeaponRange(pTarget))
    return FALSE;
  return TRUE;
}

// sub_421320 -- oracle 0x421320 (66 B). Whether pUnit must path to reach
// pTarget: 0 for a unit without pathing flag 1 or one that reaches the target's
// position directly, else 1. The two zero cases are separate arms storing into a
// local: one `||` puts the zero block after the one block. The X coordinate is
// `movsx` and the Y is declared first, so X lands in EBX.
BOOL __fastcall sub_421320(CUnit *pUnit, CUnit *pTarget) {
  int nY = pTarget->pSprite->wPosY;
  int nX = PosS(pTarget->pSprite->wPosX);
  BOOL bResult;
  if (!(pUnit->bPathingFlags & 1))
    bResult = 0;
  else if (UnitCanReachGroundTarget_xy(pUnit, nX, nY) && tai_captain_unit_connected_to_xy(pUnit, nX, nY))
    bResult = 0;
  else
    bResult = 1;
  return bResult;
}

// sub_421370
// A free __fastcall function: ECX is the unit, and EDX and the stack slot its (x, y), passed
// on to both callees. It answers 1 when the unit carries bPathingFlags bit 0 and the two
// reachability tests do not both succeed: "this unit cares about pathing and cannot get
// there". Both `return 0` sites share one block; the backward branch is not a loop.
BOOL __fastcall sub_421370(CUnit *pUnit, int nX, int nY) {
  if (!(pUnit->bPathingFlags & 1))
    return 0;

  if (UnitCanReachGroundTarget_xy(pUnit, nX, nY) && tai_captain_unit_connected_to_xy(pUnit, nX, nY))
    return 0;

  return 1;
}


// Returns the unit's enemy acquisition range in tiles, with upgrade bonuses.
// Cases stay in this order; source order sets the blocks' physical order.
BYTE CUnit::EnemyTrackingRange() {
  if ((wType == 0x01 || wType == 0x10 || wType == 0x64 || wType == 0x63 ||
       wType == 0x68) &&
      (dwFlags & 0x300) && bOrderId != 0x6B)
    return 0;
  int nBonus = 0;
  switch (wType) {
  case 0x00:
    if (get_upgrade_level(bPlayer, 0x10))
      nBonus = 1;
    break;
  case 0x26:
    if (get_upgrade_level(bPlayer, 0x1E))
      nBonus = 1;
    break;
  case 0x42:
    if (get_upgrade_level(bPlayer, 0x21))
      nBonus = 2;
    break;
  case 0x4E:
    if (gbIsExpansion)
      nBonus = 2;
    break;
  case 0x03:
  case 0x04:
    if (get_upgrade_level(bPlayer, 0x36))
      nBonus = 3;
    break;
  case 0x11:
  case 0x12:
    if (gbIsExpansion)
      nBonus = 3;
    break;
  }
  return gubUnitFindEnemy[wType] + nBonus;
}

// Returns a weapon's maximum range in pixels for this unit, with bonuses.
DWORD CUnit::GetWeaponRange(weapon_id_t bWeapon) {
  DWORD nBonus = 0;
  if (dwFlags & 0x20)
    nBonus = 0x40;
  switch (wType) {
  case 0x00:
    if (get_upgrade_level(bPlayer, 0x10))
      nBonus += 0x20;
    break;
  case 0x26:
    if (get_upgrade_level(bPlayer, 0x1E))
      nBonus += 0x20;
    break;
  case 0x42:
    if (get_upgrade_level(bPlayer, 0x21))
      nBonus += 0x40;
    break;
  case 0x4E:
    if (gbIsExpansion)
      nBonus += 0x40;
    break;
  case 0x03:
  case 0x04:
    if (bWeapon == 8 && get_upgrade_level(bPlayer, 0x36))
      nBonus += 0x60;
    break;
  case 0x11:
  case 0x12:
    if (bWeapon == 0x0A && gbIsExpansion)
      nBonus += 0x60;
    break;
  }
  return wpn_maxrange_g[bWeapon] + nBonus;
}


// Reacts to being attacked: unburrows, records the attacker, or runs from it.
void CUnit::sub_420190(CUnit *pAttacker) {
  if (wType == 0x23)
    return;
  if ((dwFlags & 0x10) && bIrradiateTimer == 0 && wType != 0x67) {
    if (gulUnitTypeFlags[wType] & 0x100000)
      Deburrow();
    return;
  }
  if ((gulUnitTypeFlags[wType] & 1) ||
      (CanTarget(pAttacker, 1) && !(gulUnitTypeFlags[wType] & 8))) {
    dwUnion7C = (DWORD_PTR)pAttacker;
    if (subunit && (gulUnitTypeFlags[subunit->wType] & 0x10)) {
      subunit->bUnion90 = bUnion90;
      subunit->dwUnion7C = (DWORD_PTR)pAttacker;
    }
    return;
  }
  if (wType == 0x67 && (dwFlags & 0x10) &&
      (dwUnion134 == 0 || bIrradiateTimer != 0))
    return;
  CUnit *pUnit = this;
  if (gulUnitTypeFlags[wType] & 0x10)
    pUnit = subunit;
  if (order_unk11[pUnit->bOrderId] && (pUnit->dwFlags & 0x20000) &&
      guard_is_at_post(pUnit) && pUnit->sub_41FA00() != 0xFF)
    pUnit->SetOrder_Target(6, ai_get_flee_target(pAttacker, pUnit));
}


// Tries to fire on the order target; TRUE while the attack is in progress.
// wX/wY are WORD locals so the sign extension stays two instructions.
BOOL CUnit::sub_420BD0() {
  if (!pOrderTarget)
    return FALSE;
  ChangeTarget();
  if (!CanSeeFocusUnit())
    return FALSE;
  if (!CanTarget(pOrderTarget, 1))
    return FALSE;
  CUnit *pTarget = pOrderTarget;
  wOrderTargetX = pTarget->pSprite->wPosX;
  wOrderTargetY = pTarget->pSprite->wPosY;
  if (subunit && (gulUnitTypeFlags[subunit->wType] & 0x10))
    return TRUE;
  weapon_id_t bWeapon;
  BYTE bSlot;
  if (pTarget->dwFlags & 4) {
    bSlot = 2;
    bWeapon = gubUnitWeaponAir[wType];
  } else {
    bWeapon = GetWeaponGndEx();
    bSlot = 1;
  }
  BYTE bCooldown = (&bUnion54)[bSlot];
  if (bCooldown) {
    if (bUnion85 > bCooldown - 1)
      bUnion85 = bCooldown - 1;
    return TRUE;
  }
  if (bMoveFlags & 8)
    return TRUE;
  DWORD dwDistance = GetDistanceTo_CUnit(pTarget);
  DWORD dwMinRange = wpn_minrange_g[bWeapon];
  if (dwMinRange && dwDistance < dwMinRange)
    return FALSE;
  if (dwDistance > GetWeaponRange(bWeapon))
    return FALSE;
  BYTE bFacing = bUnion21;
  upos_t wY = pSprite->wPosY;
  upos_t wX = pSprite->wPosX;
  int nDiff = bFacing - AngleBetween(PosS(wX), PosS(wY), wOrderTargetX, wOrderTargetY);
  if (nDiff < 0)
    nDiff += 0x100;
  if (nDiff > 0x80)
    nDiff = 0x100 - nDiff;
  if (nDiff > wpn_attackangle_g[bWeapon]) {
    if ((dwFlags >> 16) & 1) {
      bUnion85 = 0;
      return TRUE;
    }
    return FALSE;
  }
  DWORD dwF = dwFlags;
  if (dwF & 0x8000)
    return FALSE;
  if (dwF & 0x20)
    sub_420520();
  sub_40A9E0();
  bMoveFlags |= 8;
  BYTE bNew = GetWeaponCD(bWeapon) + ((BYTE)(net_rand(13) & 3) - 1);
  _unmapped_55[0] = bNew;
  _unmapped_55[1] = bNew;
  pSprite->SetAnimation(bSlot + 4, 1);
  return TRUE;
}


// Hyperion: past this many pixels a computer's target is one it is on its
// way to, and an enemy met on the way may be fought first.
#define HX_AI_MEET_RANGE (16 * 32)

// Hyperion: by Hyperion's rules, the target a computer's unit fights when its
// chosen one is farther off than HX_AI_MEET_RANGE and out of its weapon's range: the target
// FindAttackTarget finds near it, where the AI's own choice between the two prefers it (an
// armed one over an unarmed one, one in range, then the nearer), and the far one otherwise.
// 1.17's soldiers took their captain's target, the greatest threat in the captain's region
// wherever they stood, and walked past every enemy between until one shot at them; on a
// large map that is most of the way across it.
static CUnit *HxMeetOnTheWay(CUnit *pUnit, CUnit *pFar, int a2, int a3)
{
  if (pUnit->IsUnitInWeaponRange(pFar) || pUnit->GetDistanceTo_CUnit(pFar) <= HX_AI_MEET_RANGE)
    return pFar;
  CUnit *pNear = pUnit->FindAttackTarget();
  if (pNear == NULL || pNear == pFar || (a3 && sub_421320(pUnit, pNear)) || !pUnit->CanTarget(pNear, 1) ||
      (!a2 && pNear->IsCritter()))
    return pFar;
  return tai_pick_better_target(pUnit, pNear, pFar);
}

// Picks the unit's next attack target from threats, its order target and its
// remembered attacker; TRUE when one was chosen. IsDisabled's body is visible
// here through Allowed.h, which lets the attacker value cross that call.
BOOL CUnit::AIPickAttackTarget(int a1, int a2, int a3) {
  if (!dwUnion134 || !CanAttack())
    return FALSE;
  if (pOrderQueueHead && GetFirstQueuedOrder() == 0x98)
    return FALSE;
  if (bOrderId == 0x60) {
    a3 = 1;
    if (!dwUnion7C && pOrderQueueHead && GetFirstQueuedOrder() == 0x0A)
      return FALSE;
  }
  if (pOrderTarget && (dwFlags & 0x20000)) {
    CUnit *pMoveTarget = *reinterpret_cast<CUnit **>(_unmapped_14);
    if (pMoveTarget && IsEnemyUnit(pMoveTarget) && AtMoveTarget() == 2) {
      wMoveTargetX = pSprite->wPosX;
      wMoveTargetY = pSprite->wPosY;
      dwFlags &= ~0x80000;
      pOrderTarget->dwFlags |= 0x80;
      if (pOrderTarget == (CUnit *)dwUnion7C)
        dwUnion7C = 0;
      ClearTarget();
    }
  }
  CUnit *pThreat = (dwFlags & 4) ? tai_get_greatest_air_threat_near(this)
                                 : tai_get_greatest_ground_threat_near(this);
  if (a3 && (!pThreat || sub_421320(this, pThreat)))
    pThreat = NULL;
  else if (!pThreat || !CanTarget(pThreat, 1) || (!a2 && pThreat->IsCritter()))
    pThreat = NULL;
  if (!pThreat) {
    pThreat = (dwFlags & 4) ? tai_get_greatest_ground_threat_near(this)
                            : tai_get_greatest_air_threat_near(this);
    if (a3 && (!pThreat || sub_421320(this, pThreat)))
      pThreat = NULL;
    else if (!pThreat || !CanTarget(pThreat, 1) || (!a2 && pThreat->IsCritter()))
      pThreat = NULL;
  }
  CUnit *pOrder = gubOrderAttack[bOrderId] ? pOrderTarget : NULL;
  if (a3 && (!pOrder || sub_421320(this, pOrder)))
    pOrder = NULL;
  else if (!pOrder || !CanTarget(pOrder, 1) || (!a2 && pOrder->IsCritter()))
    pOrder = NULL;
  if (a3 && dwUnion7C && sub_421320(this, (CUnit *)dwUnion7C))
    dwUnion7C = 0;
  CUnit *pRemembered = (CUnit *)dwUnion7C;
  if (pRemembered && !pRemembered->IsDisabled()) {
    CUnit *pAttacker = (CUnit *)dwUnion7C;
    CUnit *pItsTarget = pAttacker->pOrderTarget;
    if (pAttacker->wType != 0x7D &&
        (!pItsTarget || pItsTarget->bPlayer != bPlayer))
      dwUnion7C = 0;
    else if (a3 && sub_421320(this, pAttacker))
      dwUnion7C = 0;
    if (!dwUnion7C || !CanTarget((CUnit *)dwUnion7C, 1) ||
        (!a2 && ((CUnit *)dwUnion7C)->IsCritter()))
      dwUnion7C = 0;
  }
  CUnit *pChosen = tai_pick_better_target(this, tai_pick_better_target(this, pOrder, pThreat),
                              (CUnit *)dwUnion7C);
  if (pChosen && HxRules())
    pChosen = HxMeetOnTheWay(this, pChosen, a2, a3);  // Hyperion
  if (!pChosen) {
    if (!tai_soldier_may_pick_own_target(this))
      return FALSE;
    CUnit *pFound = guard_return_to_post(this, (int)((intptr_t)pChosen + 1)) ? NULL : FindAttackTarget();
    pChosen = tai_pick_better_target(this, NULL, pFound);
    if (a3 && (!pChosen || sub_421320(this, pChosen)))
      return FALSE;
    if (!pChosen || !CanTarget(pChosen, 1) || (!a2 && pChosen->IsCritter()))
      return FALSE;
  }
  if (pOrderTarget != pChosen) {
    if (dwFlags & 0x20)
      pOrderTarget = pChosen;
    else
      NewTarget(this, pChosen, 1, a1);
    if (IsCarrierType() && !IsUnitInWeaponRange(pChosen))
      dwFlags |= 8;
  }
  return TRUE;
}

// 0x521D90 / 0x521CA8, units.dat armor and armor size (sheet gubUnitArmor /
// gubUnitArmorClass); byte tables indexed by the unbiased unit type.
#ifndef gubUnitArmor_g
#ifdef OC_DLL_MODE
#define gubUnitArmor_g (reinterpret_cast<BYTE *>(OC_VA(0x521D90u)))
#else
extern BYTE gubUnitArmor_g[228] OC_UNALIGNED;
#endif
#endif
#ifndef gubUnitArmorClass_g
#ifdef OC_DLL_MODE
#define gubUnitArmorClass_g (reinterpret_cast<BYTE *>(OC_VA(0x521CA8u)))
#else
extern BYTE gubUnitArmorClass_g[228] OC_UNALIGNED;
#endif
#endif

// Takes the size as an int: a BYTE argument formed at the call puts the armor
// sum above it in edx, where the oracle has ecx.
static __forceinline int damage_by_unit_size(int nDamage, BYTE bDamageType, int nUnitSize) {
  return compute_damage_from_types(nDamage, bDamageType, nUnitSize);
}


// Applies weapon damage: cheats and hallucination scale it, the defensive
// matrix and shields (less the shield upgrade) absorb first, armor comes off
// the rest, and the damage-type table scales what remains.
void CUnit::Damage(int nDamage, weapon_id_t bWeapon, int nDivisor, BYTE bShieldEffect,
                   CUnit *pSource, int nSourcePlayer) {
  if (lHitPoints == 0 || (dwFlags & 0x4000000))
    return;
  if ((glCheatBits & 4) && gPlayerData[nSourcePlayer].nType != 2)
    nDamage = 0;
  if (dwFlags & 0x40000000)
    nDamage += nDamage;
  int nTotal = (bCrunchyCount << 8) + (DWORD)nDamage / (DWORD)nDivisor;
  if (nTotal < 0x80)
    nTotal = 0x80;
  if (wDefMatrixHitPoints) {
    int nMatrix = nTotal > wDefMatrixHitPoints ? wDefMatrixHitPoints : nTotal;
    nTotal -= nMatrix;
    DamageDefMatrix(nMatrix);
  }
  int nShields = 0;
  if (gbUnitHasShields[wType] && (LONG)lShields >= 0x100) {
    if (wpn_wpnType_g[bWeapon] != 4) {
      int nUpgrade = get_upgrade_level(bPlayer, 0xF) << 8;
      if (nUpgrade >= nTotal)
        nTotal = 0x80;
      else
        nTotal -= nUpgrade;
    }
    nShields = nTotal > (LONG)lShields ? (LONG)lShields : nTotal;
    nTotal -= nShields;
  }
  if (wpn_wpnType_g[bWeapon] != 4) {
    WORD wT = wType;
    int nArmor = (ArmorUpgrades() + gubUnitArmor_g[wT]) << 8;
    nTotal -= nTotal > nArmor ? nArmor : nTotal;
  }
  int nHit = damage_by_unit_size(nTotal, wpn_wpnType_g[bWeapon], gubUnitArmorClass_g[wType]);
  if (nShields == 0 && nHit < 0x80)
    nHit = 0x80;
  DamageApply(nHit, pSource, nSourcePlayer, bWeapon != 0x22);
  if (nShields) {
    lShields -= nShields;
    if (wpn_wpnType_g[bWeapon] != 0 && lShields != 0)
      CreateShieldEffect(bShieldEffect);
  }
  tai_assign_unit_strength(this);
}

// CUnit::AtMoveTarget inlined against the caller's sprite x. y is read only
// once x compares equal and goes back through wSpY; that slot is the frame.
static __forceinline BOOL combat_at_move_target_xy(CUnit *pUnit, upos_t wSpX, upos_t &wSpY)
{
  if (pUnit->wMoveTargetX == wSpX) {
    wSpY = pUnit->pSprite->wPosY;
    if (pUnit->wMoveTargetY == wSpY)
      return (pUnit->dwFlags & 0x80000) ? 2 : 1;
  }
  return 0;
}

// Patrols between the order point and the start point: queues the return leg,
// then breaks off to attack a visible enemy or (for a medic) heal, re-queueing
// the patrol behind that order.
void CUnit::DispatchPatrol() {
  if (bOrderState == 0) {
    COrder *pNext = pOrderQueueHead;
    if (!pNext || pNext->bOrderId != 0x98) {
      SetOrder_Target(0x98, OrderTarget());
      AppendOrder_xy(0x98, PosS(pSprite->wPosX), pSprite->wPosY, NULL);
      return;
    }
    if (!SetDestination_xy(wOrderTargetX, wOrderTargetY))
      return;
    bOrderState = 1;
  }
  CUnit *pAuto = reinterpret_cast<CUnit *>(dwUnion7C);
  if (pAuto && IsEnemyUnit(pAuto)) {
    pAuto = reinterpret_cast<CUnit *>(dwUnion7C);
    if (!(pAuto->pSprite->flags.b & 0x20) && !(pAuto->dwFlags & 0x4000000) &&
        !pAuto->IsVisibleTo(this)) {
      PrependOrder_Target(0x98, OrderTarget(), pOrderQueueHead);
      PrependOrder_CUnit(gubUnitOrderAttackUnit[wType], reinterpret_cast<CUnit *>(dwUnion7C),
                         pOrderQueueHead);
      dwUnion7C = 0;
      EndUninterruptableDispatchOrder();
      HoldPosition();
      return;
    }
  }
  if (bUnion54)
    return;
  bUnion54 = 0xF;
  if (wType == 0x22) {
    CUnit *pHeal = FindHealTarget();
    if (pHeal && !pOrderTarget) {
      PrependOrder_Target(0x98, OrderTarget(), pOrderQueueHead);
      PrependOrder_CUnit(0xB0, pHeal, pOrderQueueHead);
      dwUnion7C = 0;
      EndUninterruptableDispatchOrder();
      HoldPosition();
      return;
    }
  }
  upos_t x = pSprite->wPosX;
  upos_t y;
  if (combat_at_move_target_xy(this, x, y)) {
    if (AtMoveTarget() == 2)
      AppendOrder_xy(0x98, wOrderTargetX, wOrderTargetY, NULL);
    else
      AppendOrder_xy(0x98, PosS(x), PosS(y), NULL);
    EndUninterruptableDispatchOrder();
    return;
  }
  if (EnemyTrackingRange()) {
    CUnit *pTarget = FindAttackTarget();
    if (pTarget) {
      PrependOrder_Target(0x98, OrderTarget(), pOrderQueueHead);
      PrependOrder_CUnit(gubUnitOrderAttackUnit[wType], pTarget, pOrderQueueHead);
      EndUninterruptableDispatchOrder();
      HoldPosition();
    }
  }
}

// dword_5031AC -- DWORD[5] @0x5031AC, sheet name; the step distances ai_get_flee_target
// tries in turn, {0xC0, 0x80, 0x40, 0x20, 0x10} in the exe.
#ifndef dword_5031AC
#ifdef OC_DLL_MODE
#define dword_5031AC (reinterpret_cast<DWORD *>(OC_VA(0x5031ACu)))
#else
extern DWORD dword_5031AC[5] OC_UNALIGNED;
#endif
#endif

// Picks a point up to five steps away from pArg0 on the far side of pUnit,
// clamped to the map and reachable when pUnit paths on the ground.
// The reach test takes copies of x and y, so they stay in registers across both calls.
order_target_t __fastcall ai_get_flee_target(void *pArg0, CUnit *pUnit)
{
  CUnit *pSource = static_cast<CUnit *>(pArg0);
  upos_t wDstY = pUnit->pSprite->wPosY;
  upos_t wDstX = pUnit->pSprite->wPosX;
  upos_t wSrcY = pSource->pSprite->wPosY;
  upos_t wSrcX = pSource->pSprite->wPosX;
  int nAngle = AngleBetween(PosS(wSrcX), PosS(wSrcY),
                            PosS(wDstX), PosS(wDstY));
  order_target_t t;
  t.pt.x = pUnit->pSprite->wPosX;
  t.pt.y = pUnit->pSprite->wPosY;
  for (unsigned i = 0; i < 5; i++) {
    int x, y;
    reinterpret_cast<CFlingy *>(pSource)->GetVectorComponents(nAngle, dword_5031AC[i], &x, &y);
    x += PosS(pUnit->pSprite->wPosX);
    y += PosS(pUnit->pSprite->wPosY);
    if (x < 0)
      x = 0;
    else if (x >= gwMapPixWidth)
      x = gwMapPixWidth - 1;
    if (y < 0)
      y = 0;
    else if (y >= gwMapPixHeight)
      y = gwMapPixHeight - 1;
    int nX = x, nY = y;
    if (!(pUnit->bPathingFlags & 1) ||
        (UnitCanReachGroundTarget_xy(pUnit, nX, nY) && tai_captain_unit_connected_to_xy(pUnit, nX, nY))) {
      t.pt.x = static_cast<short>(x);
      t.pt.y = static_cast<short>(y);
      if (pUnit->dwUnion134 != 0)
        guard_move_post(pUnit, static_cast<short>(x), static_cast<short>(y));
      break;
    }
  }
  t.pUnit = NULL;
  return t;
}

// 0x516D38 img_dmgOverlay, the per-image damage-overlay table; only indexed here.
#ifndef img_dmgOverlay
#ifdef OC_DLL_MODE
#define img_dmgOverlay (reinterpret_cast<DWORD *>(OC_VA(0x516D38u)))
#else
extern DWORD img_dmgOverlay[999];
#endif
#endif

// 0x4BE950 with the attacker in ECX; one of gen's folded-alias spellings.
void __fastcall voice_squelch(CUnit *);

// Applies final damage: a kill empties a transport, credits the attacker and
// counts the death; otherwise the unit is re-rated and may play the alert.
// The passenger test is IsDeadOrDying inlined, which keeps its `sete`.
void CUnit::DamageApply(int nDamage, CUnit *pAttacker, int nSourcePlayer, int a4)
{
  if (!lHitPoints)
    return;
  if (pSprite->flags.b & 8)
    pSprite->RedrawHP();
  if (pAttacker)
    OnDamaged(pAttacker, a4);
  int nApplied = nDamage;
  if ((glCheatBits & 4) && gPlayerData[nSourcePlayer].nType != 2)
    nApplied = 0;
  if (nApplied >= lHitPoints) {
    if (IsATransport()) {
      if (!(gulUnitTypeFlags[wType] & 1)) {
        for (int i = 0; i < 8; ++i) {
          CUnit *pPassenger = GetTransportUnit(i);
          if (pPassenger && !pPassenger->IsDeadOrDying()) {
            pPassenger->Suicide();
            count_add_killed(pPassenger, nSourcePlayer);
          }
        }
      }
    }
    lHitPoints = 0;
    Suicide();
    count_add_killed(this, nSourcePlayer);
    if (pAttacker) {
      if (pAttacker->IsEnemyUnit(this)) {
        pAttacker->AddKill();
        voice_squelch(pAttacker);
      }
    }
    return;
  }
  lHitPoints -= nApplied;
  tai_assign_unit_strength(this);
  if (dwFlags & 1) {
    WORD wImageId = pSprite->pMainImage->wImageID;
    if (img_dmgOverlay[wImageId])
      UpdateDmgOverlays();
  }
  if (pAttacker && bPlayer != pAttacker->bPlayer && a4)
    gamesnd_under_attack(this);
}
