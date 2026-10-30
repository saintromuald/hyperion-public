// CBullet.cpp -- the bullet block: the splash-damage mode callbacks, the recycle
// and per-frame dispatch ends of a bullet's lifetime, the order re-issue
// primitive, the lurker-spine hit log and its ring cursor, the bounce search's
// filter, and two CUnit accessors the oracle keeps here.
//
// Bodies are in the classic game's own order, and the gaps are unported functions of
// this TU. That order
// also lets a body inline one defined below it, as CBullet::DispatchFollow does
// with CBullet::Order 2,352 bytes down, so a callee must not move above its
// caller here.
//
// src117/gen/globals117.h brings no includes of its own, so it comes last.
// ---------------------------------------------------------------------------
#include "../h/playervision.h"  // Hyperion: the slots past 7

#include "oc_data.h"  // Hyperion: OC_VA
#include "../port/data/port_formats.h"  // Hyperion
#include "../gen/save117.h"
#include "../h/CBullet.h"        // ours: CBullet/CTarget, TrackedHit, the free fns

#include "../h/CUnit.h"          // ours: lHitPoints (+0x08), wType (+0x64),
                                 // dwFlags (+0xDC), dwUnionE4 (+0xE4)

#include "../h/CSprite.h"        // ours: CSprite::SetAnimation and the position
                                 // pair wPosX (+0x14) / wPosY (+0x16)

#include "../h/CFlingy.h"        // ours: the movement prefix a CBullet * is cast
                                 // to -- wMoveTargetX/Y (+0x10/+0x12),
                                 // wPositionX/Y (+0x28/+0x2A) and the three
                                 // flight callees

#include "../h/CLink.h"          // ours: CBullet::DispatchFatal's two list
                                 // primitives

#include "../h/CThingy.h"
#include "../h/rect16.h"
#include "../h/saveload.h"
#include "../h/gds_file.h"
#include "../h/Storm.h"
#include "../h/TrigEnum.h"         // find_foreach_unit, FindUnitCb
#include "../h/TrigAct.h"          // net_rand
#include "../h/CUnitTSpells.h"     // gamesnd_play, status_set_update
#include "../h/CUnitCombat.h"      // tai_unit_was_attacked
#include "../h/gds_appcrit.h"      // appcritf
#include "../h/playerlimits.h"    // Hyperion: g_hxPlayers (stage D)
#include "../h/hxrules.h"         // Hyperion: HxRules
#include <string.h>              // memset -- 0x1000 and 0x80 are both past /Oi's
                                 // inline threshold, so the oracle calls it

#include "../gen/globals117.h"
#include "../h/handicap.h"  // Hyperion: a player's handicap

// The globals this TU reads that the generated header does not emit. Each is
// spelled exactly as gen spells a global, so that gen's later emission is an
// identical redefinition rather than a conflict: do not tidy the spacing. A plain
// `extern` would stay a symbol in both compiles and make the shipped-form check
// vacuous.
//
// The three sgBulletBounce* slots at 0x53DB80 are the bounce search's side
// channel: DispatchBounce publishes the shooter, the current target and the
// previous one before the search, because the filter takes only the candidate.
// gwMapPixWidth and gwMapPixHeight are the map extent in pixels, the bounds the
// flight handlers test a position against, read `movzx` and so unsigned.
// wpn_innersplash_g is the inner full-damage splash radius, a WORD per weapon.
#ifdef OC_DLL_MODE
#define sgBulletBounceOwner_g (*reinterpret_cast<CUnit **>(OC_VA(0x53DB80u)))
#define sgBulletBounceCurrentTarget_g (*reinterpret_cast<CUnit **>(OC_VA(0x53DB84u)))
#define sgBulletBounceNextTarget_g (*reinterpret_cast<CUnit **>(OC_VA(0x53DB88u)))
#define gwMapPixWidth (*reinterpret_cast<WORD*>(OC_VA(0x543230u)))
#define gwMapPixHeight (*reinterpret_cast<WORD*>(OC_VA(0x543234u)))
#define wpn_innersplash_g (reinterpret_cast<WORD*>(OC_VA(0x525968u)))
#else
extern CUnit *sgBulletBounceOwner_g;
extern CUnit *sgBulletBounceCurrentTarget_g;
extern CUnit *sgBulletBounceNextTarget_g;
extern upos_t gwMapPixWidth;
extern upos_t gwMapPixHeight;
extern WORD wpn_innersplash_g[130] OC_UNALIGNED;
#endif

// gpLastSpreadAttacker @0x540760 is the unit whose spread state the dword at
// 0x540764 describes: CBullet::Init alternates a weapon's angular spread while
// the same unit keeps firing and re-rolls it for a different one, so clearing
// this forces the next bullet to re-randomise. The name is ours.

// gbSplashFullDamagePass @0x541770 tells the shared apply_splash_damage which
// callback it was entered through, since find_foreach_unit's callback has no room
// for a third argument: set, it awards the inner full-damage ring and skips the
// medium ring's target exemptions; clear, it skips that ring and applies them.
// Only the two callbacks write it, so it carries nothing between splash events.
// The name is ours.

// gxUnitLife @0x520708 is units.dat's hit points in 8.8 fixed point, and signed:
// CUnit::GetLife shifts it with `sar` and CUnit::SetLife clamps it with a signed
// compare. CUnitMisc.cpp declares the same table DWORD, which its own body is
// indifferent to.

// sgTracked_g @0x540768 with sgnTrackedBucket_g and sgnTrackedNum_g is the
// lurker-spine hit log and its two cursors; the counters' signedness is not
// observable in any referent. The pool counter @0x53DB8C and the used and free
// list head/tail pairs @0x540750 to 0x54075C are each a CLink * the list
// primitives take by address.

#ifdef OC_DLL_MODE
#define gpLastSpreadAttacker   (*reinterpret_cast<CUnit **>(OC_VA(0x540760u)))
#define gbSplashFullDamagePass (*reinterpret_cast<BOOL *>(OC_VA(0x541770u)))
#define gxUnitLife             (reinterpret_cast<LONG *>(OC_VA(0x520708u)))
#define sgTracked_g            (reinterpret_cast<TrackedHit *>(OC_VA(0x540768u)))
#define sgnTrackedBucket_g     (*reinterpret_cast<int *>(OC_VA(0x541768u)))
#define sgnTrackedNum_g        (*reinterpret_cast<int *>(OC_VA(0x54176Cu)))
#define sgBulletsUsedHead_g    (*reinterpret_cast<CLink **>(OC_VA(0x540750u)))
#define sgBulletsUsedTail_g    (*reinterpret_cast<CLink **>(OC_VA(0x540754u)))
#define sgBulletsFreeHead_g    (*reinterpret_cast<CLink **>(OC_VA(0x540758u)))
#define sgBulletsFreeTail_g    (*reinterpret_cast<CLink **>(OC_VA(0x54075Cu)))
#define sgnNumUsedBullets_g    (*reinterpret_cast<int *>(OC_VA(0x53DB8Cu)))
#else
extern CUnit     *gpLastSpreadAttacker;
extern BOOL       gbSplashFullDamagePass;
extern LONG       gxUnitLife[228] OC_UNALIGNED;
extern TrackedHit sgTracked_g[kTrackedSlots];
extern int        sgnTrackedBucket_g;
extern int        sgnTrackedNum_g;
extern CLink     *sgBulletsUsedHead_g;
extern CLink     *sgBulletsUsedTail_g;
extern CLink     *sgBulletsFreeHead_g;
extern CLink     *sgBulletsFreeTail_g;
extern int        sgnNumUsedBullets_g;
#endif

// sgBullets @0x53DB90 -- the 100-slot bullet pool, walked `add ebx, 70h` with a
// `cmp ax, 64h` bound.
// sgSpreadVariance_g @0x502620 -- 15 {x, y} dword pairs (sheet name).
// DispatchHitNear subtracts `dword ptr [edx+502620h]` / `[edx+502624h]` with
// EDX = index*8; the fifteenth row is the (0, 0) CBullet::Init wraps on, and
// dword_502698 follows it.
// gpImageTargeter @0x543304 / gpImageBullet @0x543308 -- CImage.cpp's iscript
// context slots (sheet names). CBullet::Update stores each bullet into both.
struct spread_t {
  /*0x00*/ int nX;
  /*0x04*/ int nY;
};
static_assert(sizeof(spread_t) == 8, "spread_t: [edx*8] stride in DispatchHitNear");
static_assert(offsetof(spread_t, nX) == 0, "spread_t::nX [edx+502620h]");
static_assert(offsetof(spread_t, nY) == 4, "spread_t::nY [edx+502624h]");
#ifdef OC_DLL_MODE
#define sgBullets (reinterpret_cast<CBullet*>(OC_VA(0x53DB90u)))
#ifndef gpImageTargeter
#define gpImageTargeter (*reinterpret_cast<CUnit **>(OC_VA(0x543304u)))
#endif
#define gpImageBullet (*reinterpret_cast<CBullet **>(OC_VA(0x543308u)))
#else
extern CBullet sgBullets[POOL_BULLETS_STORE] OC_UNALIGNED;
extern CUnit *gpImageTargeter;
extern CBullet *gpImageBullet;
#endif
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgSpreadVariance_g (reinterpret_cast<spread_t*>(OC_VA(0x502620u)))
#else
extern spread_t sgSpreadVariance_g[15];
#endif

// apply_splash_damage -- 0x4095A0, this TU, declared only: the two callbacks
// below need it as their tail-jump target. It stays TU-local rather than moving
// into src117/h/CBullet.h because a prototype in one of our .cpp files is what the
// thunk generator prefers for an unported callee; hoist it when it is ported.
//
// __fastcall with two register arguments and nothing pushed: ECX a CUnit * (its
// player, type and flags, and the ECX of CUnit::DamageFromBullet), EDX a CBullet *
// (its weapon type, target and attacker). The int in EAX is what find_foreach_unit
// reads as "stop iterating", and pass 1 of the air path does return 1.
int __fastcall apply_splash_damage(CUnit *pUnit, CBullet *pBullet);
int __fastcall bullet_apply_shockwave_stun_cb(CUnit *pUnit, CBullet *pBullet);  // Hyperion

// find_first_unit_near -- 0x442B20, oracle TU find.cpp, unported. Its declaration
// and the filter typedef live in src117/h/CBullet.h, because every type a thunked
// declaration names has to be reachable from a header too.
//
// Its first two arguments are 32-bit, which was measured rather than read off the
// callee: that body does 32-bit arithmetic and stores only the low halves either
// way, while the caller's bytes differ -- 16-bit parameters emit a bare
// `mov cx, ...` where the oracle has `movzx`. That is what makes the cast on
// argument 2 at DispatchBounce's call site matter. Argument 3 is the search
// radius, 4 the filter and 5 is passed straight through; the CUnit * it returns is
// stored into CBullet::pOrderTarget.

// Take a bullet from the free pool, initialise it, and move it to the used
// list. A hallucination only fires the weapons listed in the switch; weapon
// 0x67 is capped at 80 live bullets (`jae`: the count is compared unsigned).
CBullet *__fastcall CBullet::AllocBullet(int nWeaponId, int nX, int nY, int nPlayer, int nUnion21, CUnit *pAttacker)
{
  CBullet *pBullet = reinterpret_cast<CBullet *>(sgBulletsFreeHead_g);
  if (!pBullet)
    return NULL;
  if (nWeaponId == 0x67 && (DWORD)sgnNumUsedBullets_g >= g_pools.bullets * 4 / 5)
    return NULL;
  if (pAttacker->dwFlags & 0x8000) {
    switch (nWeaponId) {
    case 6: case 32: case 33: case 34: case 50: case 51: case 54: case 56:
    case 57: case 58: case 59: case 60: case 61: case 68: case 69: case 82:
    case 83: case 84: case 102: case 105: case 106: case 107: case 108:
      break;
    default:
      return NULL;
    }
  }
  // Hyperion: a new bullet in a Hyperion game starts from a clear slot. 1.17's Init
  // leaves fields the slot's last bullet set (bMoveFlags is ORed, the waypoint compared),
  // and a save keeps only the live bullets, so a game loaded, its free slots cleared,
  // went on otherwise than the game saved (stage D). The links stay: the slot is still
  // in the free list.
  if (HxRules())
    memset(reinterpret_cast<BYTE *>(pBullet) + 2 * sizeof(void *), 0,
           sizeof(CBullet) - 2 * sizeof(void *));
  if (!pBullet->Init(nWeaponId, nX, nY, nPlayer, nUnion21, pAttacker))
    return NULL;
  reinterpret_cast<CLink *>(pBullet)->Unlink(&sgBulletsFreeHead_g, &sgBulletsFreeTail_g);
  reinterpret_cast<CLink *>(pBullet)->PrependThis(&sgBulletsUsedHead_g, &sgBulletsUsedTail_g);
  ++sgnNumUsedBullets_g;
  return pBullet;
}

// gubWeapDamageType @0x5258E0 -- weapons.dat's explosion type, one unsigned byte
// per weapon, indexed by a zero-extended weapon id. The extent is not known.
#ifdef OC_DLL_MODE
#define gubWeapDamageType (reinterpret_cast<BYTE*>(OC_VA(0x5258E0u)))
#else
extern BYTE gubWeapDamageType[];
#endif
#ifndef wpn_dmgUpg_g
#ifdef OC_DLL_MODE
#define wpn_dmgUpg_g (reinterpret_cast<BYTE*>(OC_VA(0x5256C0u)))
#else
extern upgrade_id_t wpn_dmgUpg_g[];
#endif
#endif

// The damage a bullet deals to pUnit, 8.8 fixed point. Explosion type 5 deals
// two thirds of the target's hit points plus shields, at least 500; every
// other weapon deals its base damage plus the upgrade bonus.
int CBullet::sub_409010(CUnit *pUnit)
{
  if (gubWeapDamageType[bWeaponType] != 5) {
    int nDamage = wpn_dmgAmt_g[bWeaponType];
    BYTE bLevel = get_upgrade_level(bPlayer, wpn_dmgUpg_g[bWeaponType]);
    return (nDamage + bLevel * wpn_dmgBonus_g[bWeaponType]) << 8;
  }
  int nLife;
  if (gbUnitHasShields[pUnit->wType])
    nLife = HandicapShields(guwUnitEnergy[pUnit->wType], pUnit->bPlayer) + pUnit->GetLife();  // Hyperion: at the owner's handicap
  else
    nLife = pUnit->GetLife();
  int nDamage = nLife * 2 / 3;
  if (nDamage < 500)
    nDamage = 500;
  return nDamage << 8;
}

// dword_541774 and dword_541778 -- the air-splash pass-1 hit count and hit list.
// The count is divided unsigned, the list holds CUnit pointers, and its extent is
// unknown since pass 1 appends with no bound check. Only Damage and pass 1 touch
// either. The count is static in the proof arm: an extern count moves Damage's
// store below the push of `this`.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define dword_541774 (*reinterpret_cast<DWORD *>(OC_VA(0x541774u)))
#else
static DWORD dword_541774;
#endif
#ifdef OC_DLL_MODE
#define dword_541778 (reinterpret_cast<CUnit **>(OC_VA(0x541778u)))
#else
// Hyperion: 1,700 slots, one per unit: the list runs from 0x541778 to
// 0x543208, where gwLastScrollMtxX starts, and pass 1 appends with no bound check,
// so the storage the x64 build gives it must hold them all.
extern CUnit *dword_541778[POOL_UNITS_STORE];
#endif

// Apply the bullet's weapon effect on impact, by explosion type.
void CBullet::Damage()
{
  weapon_id_t bWeapon = bWeaponType;
  switch (gubWeapDamageType[bWeapon]) {
  case 0:
    break;
  case 1:
    if (pOrderTarget) {
      if (!(bFlags62 & 1)) {
        BYTE bDivisor = 1;
        if (gubWeapFireType[bWeapon] == 7) {
          for (BYTE n = 2 - bBounceRemaining; n; --n)
            bDivisor *= 3;
        }
        pOrderTarget->DamageFromBullet(this, bDivisor);
      } else if (pOrderTarget->dwUnion134 && pAttacker) {
        pOrderTarget->OnDamaged(pAttacker, 1);
      }
    }
    break;
  case 15:
    if (pOrderTarget)
      pOrderTarget->DamageFromBullet(this, 1);
    break;
  case 24: {
    WORD wRadius = wpn_outersplash_g[bWeapon];
    mrect_t  rc;
    dword_541774 = 0;
    rc.left = pSprite->wPosX - wRadius;
    rc.top = pSprite->wPosY - wRadius;
    rc.right = pSprite->wPosX + wRadius;
    rc.bottom = pSprite->wPosY + wRadius;
    find_foreach_unit(&rc, reinterpret_cast<FindUnitCb>(bullet_apply_air_splash_pass1_cb), this);
    if (dword_541774) {
      DWORD nIndex = dword_541774 == 1 ? 0 : net_rand(0x3A) % dword_541774;
      dword_541778[nIndex]->DamageFromBullet(this, 1);
    }
    find_foreach_unit(&rc, reinterpret_cast<FindUnitCb>(bullet_apply_air_splash_pass2_cb), this);
    break;
  }
  case 2:
  case 3:
  case 5: {
    WORD wRadius = wpn_outersplash_g[bWeapon];
    mrect_t  rc;
    rc.left = pSprite->wPosX - wRadius;
    rc.top = pSprite->wPosY - wRadius;
    rc.right = pSprite->wPosX + wRadius;
    rc.bottom = pSprite->wPosY + wRadius;
    find_foreach_unit(&rc, reinterpret_cast<FindUnitCb>(bWeapon == 0x6D ? lurkerspines_apply_splash_damage_cb : bullet_apply_ground_splash_damage_cb), this);
    break;
  }
  case 4:
    if (pOrderTarget && !pOrderTarget->IsDeadOrDying())
      pOrderTarget->UnitApplyLockdown(0x83);
    break;
  case 12:
    if (wOrderTargetX || wOrderTargetY)
      CUnit::ApplyStasisLocation(wOrderTargetX, wOrderTargetY, reinterpret_cast<intptr_t>(pAttacker));
    break;
  case 6:
    if (pOrderTarget && !pOrderTarget->IsDeadOrDying()) {
      gamesnd_play(0x399, pOrderTarget, 1, 0);
      UnitParasiteAdd(pOrderTarget, bPlayer);  // Hyperion: |= 1 << bPlayer, every slot
      status_set_update();
      if (pAttacker)
        tai_unit_was_attacked(pOrderTarget, pAttacker, 0, 0);
    }
    break;
  case 7:
    if (pAttacker && pOrderTarget && !pOrderTarget->IsDeadOrDying()) {
      tai_unit_was_attacked(pOrderTarget, pAttacker, 1, 0);
      pAttacker->UnitApplySpawnBroodling(pOrderTarget);
    }
    break;
  case 8:
    CUnit::ApplyEMPLocation(PosS(pSprite->wPosX), pSprite->wPosY, pAttacker);
    break;
  case 16:
    if (pOrderTarget && !pOrderTarget->IsDeadOrDying()) {
      gamesnd_play(0x3E6, pOrderTarget, 1, 0);
      pOrderTarget->ApplyRestoration();
    }
    break;
  case 9:
    if (pOrderTarget && !pOrderTarget->IsDeadOrDying()) {
      gamesnd_play(0x15F, pOrderTarget, 1, 0);
      pOrderTarget->UnitApplyIrradiate(pAttacker, bPlayer);
    }
    break;
  case 10:
    CUnit::ApplyEnsnare(PosS(pSprite->wPosX), pSprite->wPosY, pAttacker);
    break;
  case 11:
    CUnit::ApplyBloodBoil(wOrderTargetX, wOrderTargetY, pAttacker);
    break;
  case 13:
    CUnit::ApplyDarkSwarm(wOrderTargetX, wOrderTargetY, bPlayer);
    break;
  case 14:
    if (pAttacker && pOrderTarget && !pOrderTarget->IsDeadOrDying())
      pAttacker->ApplyConsume(pOrderTarget);
    break;
  case 17:
    CUnit::ApplyDisruptorLocation(wOrderTargetX, wOrderTargetY, bPlayer);
    break;
  case 18:
    if (pOrderTarget && !(bFlags62 & 1)) {
      pOrderTarget->DamageFromBullet(this, 1);
      if (!(bFlags62 & 2))
        CUnit::ApplyCrunchy(PosS(pSprite->wPosX), pSprite->wPosY, bPlayer, pOrderTarget);
    }
    break;
  case 22:
    if (wOrderTargetX || wOrderTargetY)
      CUnit::ApplyParalyzeLocation(wOrderTargetX, wOrderTargetY, reinterpret_cast<intptr_t>(pAttacker));
    break;
  case 21:
    if (pOrderTarget && !pOrderTarget->IsDeadOrDying())
      pOrderTarget->UnitApplyMyopia(bPlayer);
    break;
  case 25: {
    // Hyperion: a shockwave, reachable from a mod's weapon only: the enemy
    // splash of 3, then a stun of 8 of the paralysis timer's ticks on every unit it reached
    // on the ground that is no building, Maelstrom's without its organic test
    WORD wRadius = wpn_outersplash_g[bWeapon];
    mrect_t rc;
    rc.left = pSprite->wPosX - wRadius;
    rc.top = pSprite->wPosY - wRadius;
    rc.right = pSprite->wPosX + wRadius;
    rc.bottom = pSprite->wPosY + wRadius;
    find_foreach_unit(&rc, reinterpret_cast<FindUnitCb>(bullet_apply_ground_splash_damage_cb), this);
    find_foreach_unit(&rc, reinterpret_cast<FindUnitCb>(bullet_apply_shockwave_stun_cb), this);
    break;
  }
  default:
    appcritf("CBullet: Damage");
    break;
  }
}

// bullet_apply_ground_splash_damage_cb
// The whole body is a store of 1 to gbSplashFullDamagePass and a jmp into
// apply_splash_damage: it selects full-damage-pass mode, then runs the
// shared splash body on one unit the area walk found. The ground path
// needs no pre-pass, so it awards the inner ring's full damage inline.
// CBullet::Damage picks between this and the lurker-spine callback with a
// cmovz. The tail jump is what `return f(a, b);` compiles to, and any
// shape that forces a frame would cost the 15 bytes.
int __fastcall bullet_apply_ground_splash_damage_cb(CUnit *pUnit, CBullet *pBullet)
{
  gbSplashFullDamagePass = TRUE;
  return apply_splash_damage(pUnit, pBullet);
}

// wpn_middlesplash_g @0x525A70 -- weapons.dat's medium splash radius, a WORD per
// weapon.
#ifdef OC_DLL_MODE
#define wpn_middlesplash_g (reinterpret_cast<WORD*>(OC_VA(0x525A70u)))
#else
extern WORD wpn_middlesplash_g[130] OC_UNALIGNED;
#endif

// Splash one unit from pBullet: full damage inside the inner ring on a
// full-damage pass, half in the middle ring, a quarter out to the outer ring.
// Psionic Storm (weapon 0x54) marks each unit it hits and skips it after.
// Hyperion: explosion 25's stun: an enemy of the bullet's player within the
// weapon's outer splash, on the ground, no building, not burrowed, living.
int __fastcall bullet_apply_shockwave_stun_cb(CUnit *pUnit, CBullet *pBullet)
{
  if (pUnit == pBullet->pAttacker || pUnit->bPlayer == pBullet->bPlayer || pUnit->IsDeadOrDying())
    return 0;
  if ((gulUnitTypeFlags[pUnit->wType] & 5) != 0 || (pUnit->dwFlags & 0x10) != 0)
    return 0;
  if (!pBullet->sub_40A9F0(wpn_outersplash_g[pBullet->bWeaponType], pUnit))
    return 0;
  pUnit->UnitApplyParalyze(8);
  return 0;
}

int __fastcall apply_splash_damage(CUnit *pUnit, CBullet *pBullet)
{
  weapon_id_t bWeapon = pBullet->bWeaponType;
  if (pUnit == pBullet->pAttacker && bWeapon != 0x54)
    return 0;
  int nWeapon = bWeapon;
  BYTE bType = gubWeapDamageType[nWeapon];
  // Hyperion: and 25, a mod's shockwave
  if ((bType == 3 || bType == 0x18 || bType == 25) && pUnit->bPlayer == pBullet->bPlayer && pUnit != pBullet->pOrderTarget)
    return 0;
  if (!pUnit->CanTargetWithWeapon(bWeapon, pUnit))
    return 0;
  if (!pBullet->sub_40A9F0(wpn_outersplash_g[nWeapon], pUnit))
    return 0;
  if (bWeapon == 0x54) {
    if (pUnit->_unmapped_11B != 0)
      return 0;
    pUnit->_unmapped_11B = 1;
  }
  if (gbSplashFullDamagePass && pBullet->sub_40A9F0(wpn_innersplash_g[nWeapon], pUnit)) {
    pUnit->DamageFromBullet(pBullet, 1);
    return 0;
  }
  if (pUnit->dwFlags & 0x10)
    return 0;
  if (!gbSplashFullDamagePass && (pUnit == pBullet->pOrderTarget || pUnit->wType == 0x49))
    return 0;
  if (pBullet->sub_40A9F0(wpn_middlesplash_g[nWeapon], pUnit))
    pUnit->DamageFromBullet(pBullet, 2);
  else
    pUnit->DamageFromBullet(pBullet, 4);
  return 0;
}

// bullet_apply_air_splash_pass2_cb
// The mirror of the ground callback above, storing 0 where that one
// stores 1, so the shared splash body runs in medium-and-outer-ring mode
// on one unit the walk found. CBullet::Damage runs this pass only after
// pass 1 has listed the inner-ring hits and awarded that ring's full
// damage to one of them at random, so clearing the flag is what stops
// apply_splash_damage awarding it again to everyone in the ring, and what
// re-enables the medium ring's target exemptions. Nothing else in the
// image ever clears the flag.
int __fastcall bullet_apply_air_splash_pass2_cb(CUnit *pUnit, CBullet *pBullet)
{
  gbSplashFullDamagePass = FALSE;
  return apply_splash_damage(pUnit, pBullet);
}

// First air-splash pass: collect every unit in the outer ring, and if the
// bullet's own target sits in the inner ring restart the list with it alone.
int __fastcall bullet_apply_air_splash_pass1_cb(CUnit *pUnit, CBullet *pBullet)
{
  weapon_id_t bWeapon = pBullet->bWeaponType;
  if (pUnit == pBullet->pAttacker)
    return 0;
  if (pUnit->bPlayer == pBullet->bPlayer && pUnit != pBullet->pOrderTarget)
    return 0;
  if (!pUnit->CanTargetWithWeapon(bWeapon, pUnit))
    return 0;
  if (!pBullet->sub_40A9F0(wpn_outersplash_g[bWeapon], pUnit))
    return 0;
  if (pBullet->sub_40A9F0(wpn_innersplash_g[bWeapon], pUnit) && pUnit == pBullet->pOrderTarget) {
    dword_541778[0] = pUnit;
    dword_541774 = 1;
    return 1;
  }
  dword_541778[dword_541774++] = pUnit;
  return 0;
}

// lurkerspines_apply_splash_damage_cb
// A find_foreach_unit callback for the lurker spine's damage sweep: for
// each unit the walk presents, reject the firer itself, a same-player unit
// that is not the spine's own target, a unit this weapon cannot target and
// a unit outside the weapon's inner splash radius -- then consult the
// (attacker, victim) log so one spine cannot damage a unit twice, log the
// pair, and award the damage.
//
//         The damage is not gated on the logging succeeding: a full bucket skips
//         the two stores and damages anyway, so past 16 victims in one bucket the
//         de-duplication stops recording while the damage keeps being applied.
//         That is the oracle's behaviour, kept as it is. The scan is an explicitly
//         strided pointer and the append is index-addressed, the two ways the
//         oracle writes them.
int __fastcall lurkerspines_apply_splash_damage_cb(CUnit *pUnit, CBullet *pBullet)
{
  weapon_id_t bWeapon = pBullet->bWeaponType;

  // Never splash the unit that fired the spine.
  if (pUnit == pBullet->pAttacker)
    return 0;

  // Own units are spared unless they are what the spine was aimed at.
  if (pUnit->bPlayer == pBullet->bPlayer && pUnit != pBullet->pOrderTarget)
    return 0;

  if (!pUnit->CanTargetWithWeapon(bWeapon, pUnit))
    return 0;

  // Inside the inner (full-damage) ring only -- this callback has no outer arm.
  if (!pBullet->sub_40A9F0(wpn_innersplash_g[bWeapon], pUnit))
    return 0;

  CUnit *pAttacker = pBullet->pAttacker;
  if (pAttacker != NULL) {
    // The walk can present one victim to one bullet more than once, so the whole
    // 512-pair log is scanned.
    TrackedHit *pHit = sgTracked_g;
    for (unsigned i = 0; i < kTrackedSlots; ++i, ++pHit) {
      if (pHit->pAttacker == pAttacker && pHit->pVictim == pUnit)
        return 0;
    }

    if (sgnTrackedNum_g != kTrackedPerBucket) {
      int nSlot = sgnTrackedBucket_g * kTrackedPerBucket + sgnTrackedNum_g;
      ++sgnTrackedNum_g;
      sgTracked_g[nSlot].pAttacker = pAttacker;
      sgTracked_g[nSlot].pVictim = pUnit;
    }
  }

  pUnit->DamageFromBullet(pBullet, 1);
  return 0;
}

// CBullet::DispatchBounce
// Order handler [3], for a bouncing weapon (the mutalisk glave's
// family): fly at the current target and, on arrival, hand off to the
// next victim instead of expiring, up to a per-bullet bounce budget.
//
//         It is CBullet::DispatchFly with two insertions, and the middle they
//         share is identical. Before it, a re-aim at the target's current sprite
//         position, unless the target is gone or bFlags62 bit 0 is set, which is
//         what makes that bit "do not re-aim". After it, the bounce: this handler
//         never tests bTimeRemaining, only clamping it to 0 when off-map, and
//         gates on the waypoint alone, then pre-decrements bBounceRemaining and
//         tests the new value, so the last bounce -- and a failed search -- falls
//         through to the same order-5 handoff DispatchFly ends with.
void CBullet::DispatchBounce()
{
  CFlingy *pFlingy = reinterpret_cast<CFlingy *>(this);

  // Re-aim at where the target is now. The cast is what widens both coordinates
  // `movsx` into SetTargetDest's 32-bit parameters, where CSprite.h's
  // `/*0x14*/ WORD wPosX` would widen `movzx`; the bounce search below reads the
  // same pair `movzx`, so each site spells its own cast.
  if (this->pOrderTarget != NULL && (this->bFlags62 & 1) == 0) {
    CSprite *pTargetSprite = this->pOrderTarget->pSprite;
    pFlingy->SetTargetDest(PosS(pTargetSprite->wPosX), pTargetSprite->wPosY);
  }

  pFlingy->StartMovement();

  if (pFlingy->wPositionX < 0 || pFlingy->wPositionX >= gwMapPixWidth ||
      pFlingy->wPositionY < 0 || pFlingy->wPositionY >= gwMapPixHeight)
    this->bTimeRemaining = 0;

  pFlingy->BindPositionToMap();

  // Not there yet, so keep flying. No bTimeRemaining test here; that is the gate
  // DispatchFly has and this handler does not.
  if (pFlingy->wMoveTargetX != pFlingy->wPositionX ||
      pFlingy->wMoveTargetY != pFlingy->wPositionY)
    return;

  // Arrived. Spend a bounce; when the budget runs out, fall through to the
  // ordinary order-5 handoff below.
  if (--this->bBounceRemaining != 0) {
    CUnit *pNextTarget;
    if (this->pAttacker == NULL) {
      pNextTarget = NULL;
    } else {
      sgBulletBounceOwner_g = this->pAttacker;
      sgBulletBounceCurrentTarget_g = this->pOrderTarget;
      sgBulletBounceNextTarget_g = this->pUnion68;

      // Both coordinates widen `movzx` here, the opposite of the re-aim above,
      // so wPosY's cast is what turns its `movsx` into the oracle's `movzx`.
      CSprite *pSprite = this->pSprite;
      pNextTarget = find_first_unit_near(pSprite->wPosX, PosU(pSprite->wPosY),
                               0x60, &cb_bullet_bounce_proc, 0);
    }

    // Remember which unit it was on, whether or not the search found anyone.
    this->pUnion68 = this->pOrderTarget;

    if (pNextTarget != NULL) {
      this->pSprite->SetAnimation(0x0D, 1);
      this->pOrderTarget = pNextTarget;
      return;
    }
  }

  if (this->pOrderTarget != NULL)
    this->SetTargetOrder_CUnit(5, this->pOrderTarget);
  else
    this->SetTargetOrder_xy(5, this->wOrderTargetX, this->wOrderTargetY);

  this->pSprite->SetAnimation(1, 1);
}

// CBullet::DispatchFatal
// A bullet whose sprite is already gone is unlinked from the used list, prepended to the
// free list, and the active count drops: the last step of a bullet's life. ECX is reloaded
// before the second CLink call, since a __thiscall callee does not preserve it.
void CBullet::DispatchFatal()
{
  if (pSprite == NULL) {
    reinterpret_cast<CLink *>(this)->Unlink(&sgBulletsUsedHead_g, &sgBulletsUsedTail_g);
    reinterpret_cast<CLink *>(this)->PrependThis(&sgBulletsFreeHead_g, &sgBulletsFreeTail_g);
    --sgnNumUsedBullets_g;
  }
}

// CBullet::DispatchFly
// Order handler [1]: advance the bullet one frame along its flight, and
// hand it to order 5 once it has run out of time or reached its waypoint.
//
//         The countdown's post-decrement is unconditional and the test is on the
//         old value, so bTimeRemaining wraps 0 to 0xFF on the frame it expires.
//         That is the oracle's own behaviour, reproduced rather than corrected,
//         and it is harmless only because the same frame moves the bullet to
//         order 5. The order-5 handoff is Order(5) spelled out rather than
//         called; here the two spellings were not told apart by the bytes.
void CBullet::DispatchFly()
{
  CFlingy *pFlingy = reinterpret_cast<CFlingy *>(this);

  pFlingy->StartMovement();

  // Off the map in any direction -> expire the countdown immediately, so the
  // block below transitions this frame rather than after another tick.
  if (pFlingy->wPositionX < 0 || pFlingy->wPositionX >= gwMapPixWidth ||
      pFlingy->wPositionY < 0 || pFlingy->wPositionY >= gwMapPixHeight)
    this->bTimeRemaining = 0;

  pFlingy->BindPositionToMap();

  // Still ticking and not yet at the waypoint -> keep flying.
  if (this->bTimeRemaining-- != 0 &&
      (pFlingy->wMoveTargetX != pFlingy->wPositionX ||
       pFlingy->wMoveTargetY != pFlingy->wPositionY))
    return;

  // Arrived or expired: re-issue the target as order 5, by unit while it still
  // has a live one and otherwise at the remembered point, and start the impact.
  if (this->pOrderTarget != NULL)
    this->SetTargetOrder_CUnit(5, this->pOrderTarget);
  else
    this->SetTargetOrder_xy(5, this->wOrderTargetX, this->wOrderTargetY);

  this->pSprite->SetAnimation(1, 1);
}

// CBullet::DispatchDamageOverTime
// Order handler [4], the damage-over-time tick: count the order timer
// down, deal the weapon's damage on every 7th frame of the countdown, and
// when the timer runs out re-issue the target under order 5 (Order's body
// spelled out here) and start the sprite's animation 1.
//
//         The decrement reads the value before and the modulo the value after, in
//         one pass: one local for the pre-value and a re-read of the field for the
//         post-value is what makes the compiler forward the stored CL to the
//         `movzx`. The `% 7` really is an `idiv` under this TU's flags rather than
//         a magic multiply, and the signed path is what a byte promoted to int
//         gives.
void CBullet::DispatchDamageOverTime()
{
  BYTE bLeft = this->bTimeRemaining;
  this->bTimeRemaining = bLeft - 1;

  if (bLeft == 0) {
    CUnit *pTarget = this->pOrderTarget;
    if (pTarget != NULL)
      this->SetTargetOrder_CUnit(5, pTarget);
    else
      this->SetTargetOrder_xy(5, this->wOrderTargetX, this->wOrderTargetY);
    this->pSprite->SetAnimation(1, 1);
    return;
  }

  if (this->bTimeRemaining % 7 == 0)
    this->Damage();
}

// CBullet::DispatchFollow
// Per-frame handler for a bullet that homes on its target: while the target lives and
// bFlags62 bit 0 is clear, re-aim the move target at the target's sprite; otherwise re-issue
// the follow order, by unit or at the remembered point. Every arm ends in DispatchFly. The
// else arm is Order(1) written out, and there is no pTarget local: reading the member keeps
// `this` in ECX up to the SetTargetDest call.
void CBullet::DispatchFollow()
{
  if (this->pOrderTarget != NULL && (this->bFlags62 & 1) == 0) {
    CSprite *pSprite = this->pOrderTarget->pSprite;
    reinterpret_cast<CFlingy *>(this)->SetTargetDest(PosS(pSprite->wPosX), pSprite->wPosY);
  } else {
    this->Order(1);
  }

  this->DispatchFly();
}

// Order 6: aim at the target unit offset by this bullet's spread row,
// clamped to the map, then fly. The spread row is a byte offset re-added at
// each read; a typed element folds the `shl edx, 3` into both loads.
void CBullet::DispatchHitNear()
{
  CUnit *pTarget = this->pOrderTarget;
  if (pTarget && !(this->bFlags62 & 1)) {
    int nOff = this->bSpreadIndex * 8;
    CSprite *pSprite = pTarget->pSprite;
    int x = PosS(pSprite->wPosX) - *(const int *)((const BYTE *)sgSpreadVariance_g + nOff);
    if (x < 0)
      x = 0;
    else if (x >= gwMapPixWidth)
      x = gwMapPixWidth - 1;
    int y = pSprite->wPosY - *(const int *)((const BYTE *)sgSpreadVariance_g + nOff + 4);
    if (y < 0)
      y = 0;
    else if (y >= gwMapPixHeight)
      y = gwMapPixHeight - 1;
    reinterpret_cast<CFlingy *>(this)->SetTargetDest(x, y);
  } else {
    this->Order(1);
  }
  this->DispatchFly();
}

// Rewrite the pool's list links and unit pointers as 1-based ids for a
// save (bEncode) or back to pointers. A null pNext is written back through
// the `!n` arm, which is what puts the zero store first.
void CBullet::EncodeDecodeListForSave(BOOL bEncode)
{
  CBullet *pBullet = sgBullets;
  int i = g_pools.bullets;
  do {
    --i;
    if (bEncode) {
      pBullet->PreSave(0);
      pBullet->pAttacker = (CUnit *)(intptr_t)pBullet->pAttacker->GetUnitID();
      pBullet->pUnion68 = (CUnit *)(intptr_t)pBullet->pUnion68->GetUnitID();
      DWORD_PTR n = (DWORD_PTR)pBullet->pPrev;
      if (n)
        n = (DWORD)(n - (DWORD_PTR)sgBullets) / sizeof(CBullet) + 1;
      pBullet->pPrev = (CTarget *)n;
      n = (DWORD_PTR)pBullet->pNext;
      if (!n)
        pBullet->pNext = (CTarget *)n;
      else
        pBullet->pNext = (CTarget *)((DWORD)(n - (DWORD_PTR)sgBullets) / sizeof(CBullet) + 1);
    } else {
      pBullet->PostLoad(0);
      pBullet->pAttacker = CUnit::FromIdAlive((DWORD)(DWORD_PTR)pBullet->pAttacker);
      pBullet->pUnion68 = CUnit::FromIdAlive((DWORD)(DWORD_PTR)pBullet->pUnion68);
      DWORD_PTR n = (DWORD_PTR)pBullet->pPrev;
      if (n)
        n = (DWORD_PTR)(sgBullets + n - 1);
      pBullet->pPrev = (CTarget *)n;
      n = (DWORD_PTR)pBullet->pNext;
      if (n)
        n = (DWORD_PTR)(sgBullets + n - 1);
      pBullet->pNext = (CTarget *)n;
    }
    ++pBullet;
  } while (i);
}

// CUnit::GetLife
// The unit type's maximum hit points in whole points, gxUnitLife[wType] >> 8, or when that
// is 0, this unit's own hit points rounded up and floored at 1. CUnit::MaxToughness inlines
// it. An early return for the first case and a ternary for the second keep the cmov.
int CUnit::GetLife()
{
  int nLife = HandicapLife(gxUnitLife[this->wType], this->bPlayer) >> 8;  // Hyperion: at the owner's handicap
  if (nLife != 0)
    return nLife;

  int nHitPoints = (this->lHitPoints + 0xFF) >> 8;
  return nHitPoints != 0 ? nHitPoints : 1;
}

// Fill pBounds with the unit's collision box around its sprite position.
// The two coordinate locals come first and in this order; declaring the box
// pointer first mirrors every register. Inline, like placebox.cpp's copy,
// which ResolveUnitCollision needs visible.
OC_EMIT inline void CUnit::GetBounds(mrect_t *pBounds)
{
  spos_t y = this->pSprite->wPosY;
  spos_t x = this->pSprite->wPosX;
  const rect16_t *pBox = &gUnitBoundBox[this->wType];
  pBounds->top    = y - pBox->top;
  pBounds->bottom = y + pBox->bottom;
  pBounds->left   = x - pBox->left;
  pBounds->right  = x + pBox->right;
}

// Fill pBounds with the unit type's collision box around (x, y). Inline, like
// placebox.cpp's copy; nothing here calls it out of line, so its object
// comes from placebox.cpp.
OC_EMIT inline void CUnit::GetBounds_xy(mrect_t *pBounds, spos_t x, spos_t y)
{
  const rect16_t *pBox = &gUnitBoundBox[this->wType];
  pBounds->top    = y - pBox->top;
  pBounds->bottom = y + pBox->bottom;
  pBounds->left   = x - pBox->left;
  pBounds->right  = x + pBox->right;
}

// Weapons.dat's bullet lifetime and launch spin, and the side the last spin went.
#ifdef OC_DLL_MODE
#define wpn_removeafter_g  (reinterpret_cast<BYTE *>(OC_VA(0x525858u)))
#define wpn_launchspin_g   (reinterpret_cast<BYTE *>(OC_VA(0x526028u)))
#define sgbSpinDirection_g (*reinterpret_cast<BOOL *>(OC_VA(0x540764u)))
#else
extern BYTE wpn_removeafter_g[130] OC_UNALIGNED;
extern BYTE wpn_launchspin_g[130] OC_UNALIGNED;
extern BOOL sgbSpinDirection_g;
#endif

// Creates a bullet for a weapon: lifetime, launch spin, attacker and target, then
// aims it according to the weapon's fire type. The fall-out arms of cases 2/4, 5
// and 6 break to one shared return; returning from each instead tail-merges case
// 9's SetTarget_xy into case 0/1's. The facing is read into an int before the
// gpLastSpreadAttacker store and the sum stays unnamed; a named sum puts the
// add's result in ECX.
BOOL CBullet::Init(int nWeaponId, int nX, int nY, int nPlayer, int nDirection,
                   CUnit *pAttacker)
{
  CFlingy *pFlingy = reinterpret_cast<CFlingy *>(this);
  if (!Create(wpn_gfx_g[nWeaponId], nX, nY, nPlayer, nDirection))
    return 0;
  bWeaponType = (weapon_id_t)nWeaponId;
  bTimeRemaining = wpn_removeafter_g[nWeaponId];
  pFlingy->bMoveFlags |= 8;
  bFlags62 = 0;
  bBounceRemaining = 0;
  SetTargetOrder(0);
  OrderInit();
  if (wpn_launchspin_g[nWeaponId]) {
    BOOL bSide;
    if (pAttacker == gpLastSpreadAttacker)
      bSide = sgbSpinDirection_g == 0;
    else
      bSide = net_rand(0) & 1;
    sgbSpinDirection_g = bSide;
    int nSpin = bSide ? wpn_launchspin_g[nWeaponId] : -wpn_launchspin_g[nWeaponId];
    int nFacing = pFlingy->bTargetDirection;
    gpLastSpreadAttacker = pAttacker;
    pFlingy->bCurrentDirection = pFlingy->bTargetDirection = (BYTE)((BYTE)nSpin + nFacing);
  }
  this->pAttacker = pAttacker;
  if (gulUnitTypeFlags[pAttacker->wType] & 0x10)
    this->pAttacker = pAttacker->subunit;
  if (pAttacker->wType == 0x55)
    this->pAttacker = pAttacker->pHangarParent;
  if (pAttacker->dwFlags & 0x40000000)
    bFlags62 |= 2;
  pUnion68 = NULL;
  CUnit *pTarget = pAttacker->pOrderTarget;
  if (pTarget != NULL) {
    pSprite->bElevationLevel = pTarget->pSprite->bElevationLevel + 1;
    pOrderTarget = pTarget;
    wOrderTargetX = pTarget->pSprite->wPosX;
    wOrderTargetY = pTarget->pSprite->wPosY;
  } else {
    pSprite->bElevationLevel = pAttacker->pSprite->bElevationLevel + 1;
    pOrderTarget = NULL;
    wOrderTargetX = pAttacker->wOrderTargetX;
    wOrderTargetY = pAttacker->wOrderTargetY;
  }
  switch (gubWeapFireType[nWeaponId]) {
  case 8: {
    WORD wIndex = *reinterpret_cast<WORD *>(pAttacker->_unmapped_130);
    bSpreadIndex = (BYTE)wIndex;
    wOrderTargetX -= sgSpreadVariance_g[(BYTE)wIndex].nX;
    wOrderTargetY -= sgSpreadVariance_g[(BYTE)wIndex].nY;
    pFlingy->SetTarget_xy(wOrderTargetX, wOrderTargetY);
    wIndex++;
    if (sgSpreadVariance_g[wIndex].nX == 0)
      wIndex = 0;
    *reinterpret_cast<WORD *>(pAttacker->_unmapped_130) = wIndex;
    return 1;
  }
  case 2:
  case 4:
    if (pTarget != NULL && this->pAttacker != NULL) {
      int nChance = this->pAttacker->GetChanceToHit(pTarget);
      // The unsigned literal gives the oracle's unsigned compare.
      if ((net_rand(1) & 0xFFu) <= nChance) {
        int nDx, nDy;
        pFlingy->GetVectorComponents(nDirection, 0x1E, &nDx, &nDy);
        pFlingy->SetXY(PosU(pSprite->wPosX - nDx), PosU(pSprite->wPosY - nDy));
        bFlags62 |= 1;
      }
    }
    break;
  case 3:
    pFlingy->SetXY(PosU(wOrderTargetX), PosU(wOrderTargetY));
    return 1;
  case 6:
    if (this->pAttacker != NULL) {
      this->pAttacker->dwFlags |= 0x80000000;
      this->pAttacker->SelfDestruct();
    }
    break;
  case 9: {
    int nDx, nDy;
    pFlingy->GetVectorComponents(pFlingy->bCurrentDirection,
                                 (wpn_maxrange_g[bWeaponType] + 0x14) << 8, &nDx, &nDy);
    wOrderTargetX = pAttacker->pSprite->wPosX + (nDx >> 8);
    wOrderTargetY = pAttacker->pSprite->wPosY + (nDy >> 8);
    pFlingy->SetTarget_xy(wOrderTargetX, wOrderTargetY);
    return 1;
  }
  case 7:
    bBounceRemaining = 3;
  case 0:
  case 1:
    if (pTarget != NULL && this->pAttacker != NULL) {
      int nChance = this->pAttacker->GetChanceToHit(pTarget);
      if ((net_rand(1) & 0xFFu) <= nChance) {
        int nDx, nDy;
        pFlingy->GetVectorComponents(nDirection, 0x1E, &nDx, &nDy);
        wOrderTargetX -= nDx;
        wOrderTargetY -= nDy;
        bFlags62 |= 1;
      }
    }
    pFlingy->SetTarget_xy(wOrderTargetX, wOrderTargetY);
    break;
  case 5:
    break;
  default:
    return 0;
  }
  return 1;
}

// Clear the bullet pool and chain every slot onto the free list.
void CBullet::InitBullets()
{
  memset(sgBullets, 0, g_pools.bullets * sizeof(CBullet));
  sgnNumUsedBullets_g = 0;
  sgBulletsFreeTail_g = reinterpret_cast<CLink *>(&sgBullets[0]);
  sgBulletsFreeHead_g = reinterpret_cast<CLink *>(&sgBullets[0]);
  sgBulletsUsedTail_g = NULL;
  sgBulletsUsedHead_g = NULL;
  CBullet *pBullet = &sgBullets[1];
  for (int i = 1; i < g_pools.bullets; i++) {
    reinterpret_cast<CLink *>(&sgBullets[0])->AppendLink(reinterpret_cast<CLink *>(pBullet), &sgBulletsFreeTail_g);
    pBullet++;
  }
}

// Clear the lurker-spine hit table and both of its cursors.
void CBullet::InitBulletTracking()
{
  memset(sgTracked_g, 0, kTrackedSlots * sizeof(TrackedHit));
  sgnTrackedBucket_g = 0;
  sgnTrackedNum_g = 0;
}

// CUnit::IsVisibleTo
// A unit is visible to another unit's owner only if it carries one of the dwFlags bits in
// 0x300 and that player's bit in the mask at +0xE4 is clear; a unit with neither bit reads
// as not visible, so this is a detection check, not a general predicate. dwUnionE4 stays a
// DWORD in memory: 1.17 tests it against a register without loading it.
BOOL CUnit::IsVisibleTo(CUnit *pOther)
{
  return (dwFlags & 0x300) && !UnitDetectedBy(this, pOther->bPlayer);  // Hyperion: every slot (stage D)
}

// Clear the pool, read it in the format the save version names, then
// relink it.
BOOL CBullet::LoadBullets(void *pFile, WORD wVersion)
{
  memset(sgBullets, 0, g_pools.bullets * sizeof(CBullet));  // Hyperion: 0x2BC0 in 1.17
  if (wVersion <= 2) {
    if (!LoadBulletsOld(pFile))
      return FALSE;
  } else {
    if (!LoadBulletsNew(pFile))
      return FALSE;
  }
  EncodeDecodeListForSave(0);
  RecreateBulletList();
  return TRUE;
}

#pragma pack(push, 1)
struct BulletRecNew {
  /*0x00*/ WORD wIndex;
  /*0x02*/ BYTE data[0x70];
};
struct BulletRecOld {
  /*0x00*/ WORD wIndex;
  /*0x02*/ BYTE data[0x6C];
};
#pragma pack(pop)
static_assert(sizeof(BulletRecNew) == 0x72, "imul 72h in LoadBulletsNew");
static_assert(offsetof(BulletRecNew, wIndex) == 0x00, "movzx eax, word ptr [ebx]");
static_assert(offsetof(BulletRecNew, data) == 0x02, "lea esi, [ebx+2h]");
static_assert(sizeof(BulletRecOld) == 0x6E, "imul 6Eh in LoadBulletsOld");
static_assert(offsetof(BulletRecOld, wIndex) == 0x00, "movzx eax, word ptr [ebx]");
static_assert(offsetof(BulletRecOld, data) == 0x02, "lea esi, [ebx+2h]");

// Read a compressed run of (WORD slot, 0x70-byte bullet) records.
// The cursor is set before decompress, as in CFlingy::LoadFlingies; that
// keeps pFile in the stack slot the loop counter later reuses.
BOOL CBullet::LoadBulletsNew(void *pFile)
{
  WORD wCount;
  if (!pfread(&wCount, 2, pFile))
    return FALSE;
  if (wCount) {
    int nSize = wCount * sizeof(BulletRecNew);
    BulletRecNew *pBuf =
        (BulletRecNew *)SMemAlloc(nSize, "CBullet.cpp", 0x558, 8);
    BulletRecNew *pRec = pBuf;
    if (!decompress(pBuf, nSize, pFile)) {
      SMemFree(pBuf, "CBullet.cpp", 0x55D, 0);
      return FALSE;
    }
    for (WORD i = 0; i < wCount; i++, pRec++) {
      WORD w = pRec->wIndex;
      // Hyperion: a slot past the game's pool is a broken save
      if (w >= g_pools.bullets) {
        SMemFree(pBuf, "CBullet.cpp", 0, 0);
        return FALSE;
      }
      PortSave117Get(SAVE117_CBullet, pRec->data, &sgBullets[w]);  // Hyperion
      sgBullets[w].BitSwap();
    }
    SMemFree(pBuf, "CBullet.cpp", 0x56F, 0);
  }
  return TRUE;
}

// The pre-version-3 reader: 0x6C-byte records.
BOOL CBullet::LoadBulletsOld(void *pFile)
{
  WORD wCount;
  if (!pfread(&wCount, 2, pFile))
    return FALSE;
  if (wCount) {
    int nSize = wCount * sizeof(BulletRecOld);
    BulletRecOld *pBuf =
        (BulletRecOld *)SMemAlloc(nSize, "CBullet.cpp", 0x52D, 8);
    BulletRecOld *pRec = pBuf;
    if (!decompress(pBuf, nSize, pFile)) {
      SMemFree(pBuf, "CBullet.cpp", 0x532, 0);
      return FALSE;
    }
    for (WORD i = 0; i < wCount; i++, pRec++) {
      WORD w = pRec->wIndex;
      // Hyperion: a slot past the game's pool is a broken save
      if (w >= g_pools.bullets) {
        SMemFree(pBuf, "CBullet.cpp", 0, 0);
        return FALSE;
      }
      // Hyperion: 1.17's record less its last dword, completed and
      // translated.
      BYTE ab117[0x70] = {0};
      memcpy(ab117, pRec->data, sizeof(pRec->data));
      PortSave117Get(SAVE117_CBullet, ab117, &sgBullets[w]);
      sgBullets[w].BitSwap();
    }
    SMemFree(pBuf, "CBullet.cpp", 0x545, 0);
  }
  return TRUE;
}

// Rebuild the free and used lists from the pool after a load: the free list
// from the slots without a sprite, the used list from the rest, recounting it.
void CBullet::RecreateBulletList()
{
  int i;
  for (i = 0; i < g_pools.bullets; ++i) {
    if (!sgBullets[i].pSprite)
      break;
  }
  if (i == g_pools.bullets) {
    sgBulletsFreeHead_g = NULL;
    sgBulletsFreeTail_g = NULL;
  } else {
    sgBulletsFreeHead_g = reinterpret_cast<CLink *>(&sgBullets[i]);
    sgBulletsFreeTail_g = reinterpret_cast<CLink *>(&sgBullets[i]);
    CBullet *pLast = &sgBullets[i++];
    for (; i < g_pools.bullets; ++i) {
      CBullet *p = &sgBullets[i];
      if (!p->pSprite) {
        reinterpret_cast<CLink *>(pLast)->AppendLink(reinterpret_cast<CLink *>(p), &sgBulletsFreeTail_g);
        pLast = p;
      }
    }
  }
  for (i = 0; i < g_pools.bullets; ++i) {
    if (sgBullets[i].pSprite)
      break;
  }
  if (i == g_pools.bullets) {
    sgBulletsUsedHead_g = NULL;
    sgBulletsUsedTail_g = NULL;
    sgnNumUsedBullets_g = 0;
  } else {
    sgBullets[i].pPrev = NULL;
    CBullet *pLast = &sgBullets[i];
    sgBulletsUsedHead_g = reinterpret_cast<CLink *>(pLast);
    sgBulletsUsedTail_g = reinterpret_cast<CLink *>(pLast);
    sgnNumUsedBullets_g = 1;
    sgBullets[i++].pNext = NULL;
    for (; i < g_pools.bullets; ++i) {
      CBullet *p = &sgBullets[i];
      if (p->pSprite) {
        p->pPrev = NULL;
        p->pNext = NULL;
        reinterpret_cast<CLink *>(pLast)->AppendLink(reinterpret_cast<CLink *>(p), &sgBulletsUsedTail_g);
        ++sgnNumUsedBullets_g;
        pLast = p;
      }
    }
  }
}

// CBullet::Order
// Re-issue the bullet's current target as a target order under a new order id: by unit while
// it has a live target unit, else by the remembered ground point. Both callees store the id
// into CUnit.h's `/*0x4D*/ BYTE bOrderId`. A CBullet * is a CTarget * with no adjustment, and
// the coordinates are pushed with movsx, so wOrderTargetX/Y are signed.
void CBullet::Order(order_id_t bOrder)
{
  CUnit *pTarget = this->pOrderTarget;
  if (pTarget != NULL)
    this->SetTargetOrder_CUnit(bOrder, pTarget);
  else
    this->SetTargetOrder_xy(bOrder, this->wOrderTargetX, this->wOrderTargetY);
}

// CBullet::RemoveTargetReferences
// Severs every in-flight bullet's references to a unit that is going
// away: a bullet still homing on it is turned to home on the ground
// square the unit occupies, and a bullet that names it as the shooter
// forgets the shooter.
//
//         The two tests are siblings and not nested -- the target-mismatch branch
//         lands exactly on the attacker compare -- so a bullet the dying unit
//         fired at somebody else still has its shooter cleared. The sprite is
//         re-read for the second coordinate because the store through the bullet
//         may alias it; hoisting it into a local kills that second load. Both
//         casts are identities, the link pair and the base sitting at offset 0.
void __fastcall CBullet::RemoveTargetReferences(CUnit *pUnit)
{
  for (CBullet *pBullet = reinterpret_cast<CBullet *>(sgBulletsUsedHead_g); pBullet != NULL;
       pBullet = static_cast<CBullet *>(pBullet->pNext)) {
    if (pBullet->pOrderTarget == pUnit) {
      pBullet->wOrderTargetX = pUnit->pSprite->wPosX;
      pBullet->wOrderTargetY = pUnit->pSprite->wPosY;
      pBullet->pOrderTarget = NULL;
    }

    if (pBullet->pAttacker == pUnit)
      pBullet->pAttacker = NULL;
  }
}

// Run every live bullet for one frame with it published as the iscript context.
// CBullet::RunOrders is inlined here, DispatchFatal included.
void CBullet::Update()
{
  CBullet *pBullet = reinterpret_cast<CBullet *>(sgBulletsUsedHead_g);
  while (pBullet) {
    CBullet *pNext = (CBullet *)pBullet->pNext;
    gpImageTargeter = reinterpret_cast<CUnit *>(pBullet);
    gpImageBullet = pBullet;
    reinterpret_cast<CThingy *>(pBullet)->UpdateVisibility();
    reinterpret_cast<CThingy *>(pBullet)->SpriteUpdate();
    pBullet->RunOrders();
    pBullet = pNext;
  }
  // Written Targeter first: the compiler emits the two stores in reverse.
  gpImageTargeter = NULL;
  gpImageBullet = NULL;
}

// CBullet::RunOrders
// The per-frame order dispatch: a dense switch on bOrderId (CTarget's byte at +0x4D) over
// [0] OrderInit, [1] DispatchFly, [2] DispatchFollow, [3] DispatchBounce,
// [4] DispatchDamageOverTime, [5] DispatchFatal and [6] DispatchHitNear, doing nothing for an
// id outside them. The jump table follows the body, and DispatchFatal is called, not inlined.
void CBullet::RunOrders()
{
  switch (bOrderId) {
  case 0: OrderInit(); break;
  case 1: DispatchFly(); break;
  case 2: DispatchFollow(); break;
  case 3: DispatchBounce(); break;
  case 4: DispatchDamageOverTime(); break;
  case 5: DispatchFatal(); break;
  case 6: DispatchHitNear(); break;
  }
}

// Order 0: on the first frame, pick the bullet's order and animation from the
// weapon's fire type, then run it. The homing fire types inline Order(5).
void CBullet::OrderInit()
{
  if (!(bOrderFlags & 1))
    return;
  bOrderFlags &= ~1;
  BYTE bFireType = gubWeapFireType[bWeaponType];
  if (bFireType == 2 || bFireType == 4 || bFireType == 5 || bFireType == 6) {
    Order(5);
    pSprite->SetAnimation(1, 1);
  } else if (bFireType == 1) {
    Order(2);
    pSprite->SetAnimation(2, 1);
  } else if (bFireType == 7) {
    Order(3);
    pSprite->SetAnimation(2, 1);
  } else if (bFireType == 3) {
    Order(4);
    pSprite->SetAnimation(0xE, 1);
  } else if (bFireType == 8) {
    Order(6);
    pSprite->SetAnimation(2, 1);
  } else {
    Order(1);
    pSprite->SetAnimation(2, 1);
  }
  RunOrders();
}

// Write the live bullets as compressed (WORD slot, bullet) records. The count
// loop needs its WORD index; an int one is unrolled five times.
BOOL CBullet::SaveBullets(void *pFile)
{
  EncodeDecodeListForSave(1);
  DWORD dwCount = 0;
  for (WORD i = 0; i < g_pools.bullets; ++i)
    if (sgBullets[i].pSprite)
      ++dwCount;
  BOOL bOk = pfwrite(&dwCount, 2, pFile);
  if (bOk && (WORD)dwCount) {
    int nSize = (WORD)dwCount * (2 + PortSave117Size(SAVE117_CBullet));  // Hyperion: 0x72
    BYTE *pBuf = (BYTE *)SMemAlloc(nSize, "CBullet.cpp", 0x4FC, 8);
    BYTE *p = pBuf;
    dwCount = 0;
    for (WORD i = 0; i < g_pools.bullets; ++i) {
      CBullet *pBullet = &sgBullets[i];
      if (pBullet->pSprite) {
        *(WORD *)p = i;
        p += 2;
        pBullet->BitSwap();
        p = PortSave117Put(SAVE117_CBullet, pBullet, p);
        pBullet->BitSwap();
        ++dwCount;
      }
    }
    bOk = PortSaveChunk(pBuf, nSize, dwCount, pFile);
    SMemFree(pBuf, "CBullet.cpp", 0x513, 0);
  }
  EncodeDecodeListForSave(0);
  return bOk;
}

// cb_bullet_bounce_proc
// Decides whether one candidate may receive the bounce
// CBullet::DispatchBounce is looking for a home for: the shooter must be
// able to target it, a cloaked or burrowed candidate must already be
// detected by the shooter's player, it must be an enemy, and it must be
// neither the unit the bullet is on now nor the one before it.
//
//         The player-bit test has the opposite polarity to CUnit::IsVisibleTo's
//         above, so this is the complementary test and not that function inlined.
//         The last guard stands on its own rather than joining the final return:
//         `setnz` appears only when that return has one comparison left to
//         materialise, which is also why this guard jumps forward to its own
//         epilogue while the first three jump back to the shared one.
int __fastcall cb_bullet_bounce_proc(CUnit *pUnit)
{
  if (!sgBulletBounceOwner_g->CanTarget(pUnit, 1))
    return 0;

  if ((pUnit->dwFlags & 0x300) && !UnitDetectedBy(pUnit, sgBulletBounceOwner_g->bPlayer))  // Hyperion
    return 0;

  if (!sgBulletBounceOwner_g->IsEnemyUnit(pUnit))
    return 0;

  if (sgBulletBounceCurrentTarget_g == pUnit)
    return 0;

  return sgBulletBounceNextTarget_g != pUnit;
}

// CBullet::NextBucket
// Ages the lurker-spine hit log by one frame: the ring cursor advances (31 wraps to 0), the
// 16 eight-byte entries it now points at are wiped and the fill level resets, so a logged
// pair survives 32 calls. CUnit::EntityUpdate calls it once per unit-update tick. The wrap
// is `if (n == 32) n = 0;`, not a modulo.
void CBullet::NextBucket()
{
  int nBucket = sgnTrackedBucket_g + 1;
  if (nBucket == kTrackedBuckets)
    nBucket = 0;
  sgnTrackedBucket_g = nBucket;

  memset(&sgTracked_g[nBucket * kTrackedPerBucket], 0,
         kTrackedPerBucket * sizeof(TrackedHit));
  sgnTrackedNum_g = 0;
}

// sub_40A9E0
// Forgets which unit last fired a spread weapon, so the next CBullet::Init re-rolls the
// spread side instead of alternating it. Both callers are attack-start paths.
void sub_40A9E0()
{
  gpLastSpreadAttacker = 0;
}

// Is pUnit's bounding box within nRange of this bullet? The final compare is
// unsigned (`cmp` / `sbb` / `inc`).
// The sprite position is read at each use; an int local for it gives the
// sprite pointer EDI instead of ECX.
BOOL CBullet::sub_40A9F0(int nRange, CUnit *pUnit)
{
  mrect_t rc;  // Hyperion: a box of map positions
  pUnit->GetBounds(&rc);
  rc.right++;
  rc.bottom++;
  CSprite *pMine = this->pSprite;
  int dx = PosS(pMine->wPosX) - rc.right;
  if (dx < 0) {
    dx = rc.left - PosS(pMine->wPosX);
    if (dx < 0)
      dx = 0;
  }
  int dy = PosS(pMine->wPosY) - rc.bottom;
  if (dy < 0) {
    dy = rc.top - PosS(pMine->wPosY);
    if (dy < 0)
      dy = 0;
  }
  return (DWORD)nRange >= (DWORD)veclen(0, dx, 0, dy);
}
