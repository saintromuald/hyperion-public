// CUnitTSpells.cpp -- the terran unit spells and the status they leave: Defensive Matrix,
// Irradiate, Lockdown, Myopia, Paralyze and Stim.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playervision.h"  // Hyperion: the slots past 7
#include "../h/CUnitTSpells.h"
#include "../h/CUnit.h"
#include "../h/CImage.h"
#include "../h/CSprite.h"
#include "../gen/globals117.h"

// The binary's 0x442B20 search is called through two source prototypes. This
// TU passes WORD coordinates and a contextual two-register callback; CBullet
// passes widened int coordinates and a callback that ignores EDX. Both have a
// 20-byte fastcall argument footprint and intentionally share the same C-link
// symbol. Keep this view TU-local so generated aggregate TUs can include
// CBullet.h and CUnitTSpells.h without declaring incompatible C overloads.
// Hyperion: the definition (find.cpp) reads the position as ints, and this
// TU's callers pass it as 16 bits. The view takes the definition's types, which
// the x64 build needs, and the macro narrows the arguments as 1.17's did
//.
extern "C" CUnit *__fastcall find_first_unit_near(int x, int y, int nRadius, CUnitTSpellsFindProc pfnFilter, CUnit *pContext);
#define find_first_unit_near(a0, a1, a2, a3, a4) (find_first_unit_near)(PosU(a0), PosU(a1), (a2), (a3), (a4))

// These registry rows are not emitted by gen until their first ledger mark.
#ifndef gnWeapCooldown
#ifdef OC_DLL_MODE
#define gnWeapCooldown (reinterpret_cast<BYTE *>(OC_VA(0x525E90u)))
#else
extern BYTE gnWeapCooldown[130] OC_UNALIGNED;
#endif
#endif

#ifndef dword_6663E4
#ifdef OC_DLL_MODE
#define dword_6663E4 (*reinterpret_cast<DWORD *>(OC_VA(0x6663E4u)))
#else
extern DWORD dword_6663E4;
#endif
#endif

BOOL CUnit::CanUseStim()
{
  return lHitPoints > 0xA00;
}

void CUnit::DamageDefMatrix(int nDamage)
{
  if (nDamage >= wDefMatrixHitPoints) {
    wDefMatrixHitPoints = 0;
    bDefMatrixTimer = 0;
    RemoveImageOverlay(0x173, 0x175);
    CImage *pImage = pSprite->OverlayGetImage(0x176, 0x178);
    if (pImage != NULL)
      pImage->Kill();
  } else {
    wDefMatrixHitPoints -= nDamage;
  }

  if (bDefMatrixTimer != 0 && (dwFlags & UF_Burrowed) == 0)
    CreateOverlay(GetSizeType() + 0x179, 0, 0, 0);
}

void CUnit::DispatchDefMatrix()
{
  WORD wEnergy;
  if (sub_434B80(6, sub_423150(1) << 5, 0x370, &wEnergy)) {
    SubtractEnergy(wEnergy);
    pOrderTarget->UnitApplyDefMatrix();
    gamesnd_play(0x15D, this, 1, 0);
    pSprite->AllocImage(0x21B, 0, 0, 0);
    EndDispatchOrder();
  }
}

int __fastcall IrradiateProc(CUnit *pCandidate, CUnit *pIrradiated)
{
  WORD nType = pCandidate->wType;
  if ((gulUnitTypeFlags[nType] & 0x10000) != 0 &&
      (gulUnitTypeFlags[nType] & 1) == 0 &&
      nType != 0x23 && nType != 0x24 && nType != 0x61 &&
      ((pCandidate->dwFlags & CUnit::UF_Burrowed) == 0 ||
       pIrradiated == pCandidate) &&
      ((pIrradiated->dwFlags & CUnit::UF_InTransport) != 0 ||
       pIrradiated->IsWithinTargetRange(0x20, pCandidate))) {
    pCandidate->Damage((wpn_dmgAmt_g[0x22] << 8) / gnWeapCooldown[0x22],
                       0x22, 1, 0, pIrradiated->pIrradiateSource,
                       pIrradiated->bIrradiatePlayer);
  }
  return 0;
}

void CUnit::IrradiateDamage()
{
  DWORD nFlags = dwFlags;
  if ((nFlags & UF_Burrowed) != 0) {
    IrradiateProc(this, this);
    return;
  }

  if ((nFlags & UF_InTransport) == 0) {
    find_first_unit_near(pSprite->wPosX, PosU(pSprite->wPosY), 0xA0,
               IrradiateProc, this);
    return;
  }

  if (pUnion80 != NULL)
    pUnion80->IterateTransport(IrradiateProc, this);
}

void CUnit::IrradiateEnd()
{
  RemoveImageOverlay(0x17C, 0x17E);
  bIrradiateTimer = 0;
  pIrradiateSource = NULL;
  bIrradiatePlayer = 8;
}

void CUnit::LockdownEnd()
{
  bLockdownTimer = 0;
  if ((dwFlags & 0x400) == 0 && bStasisTimer == 0 && bParalyzeTimer == 0) {
    SetButtonset(wType);
    status_set_update();
  }
  DestroyOverlays(0x169, 0x16B);
}

void CUnit::ParalyzeEnd()
{
  bParalyzeTimer = 0;
  if ((dwFlags & 0x400) == 0 && bLockdownTimer == 0 && bStasisTimer == 0) {
    SetButtonset(wType);
    status_set_update();
  }
  DestroyOverlays(0x3D6, 0x3D8);
}

void CUnit::SyncDefMatrix()
{
  if (bDefMatrixTimer != 0) {
    upos_t nPosY = pSprite->wPosY;
    upos_t nPosX = pSprite->wPosX;
    if (!sub_434130(6, this, 0xE4, PosS(nPosX),
                    PosS(nPosY), NULL)) {
      bDefMatrixTimer = 0;
      wDefMatrixHitPoints = 0;
      return;
    }

    WORD nMatrix = wDefMatrixHitPoints;
    BYTE nTimer = bDefMatrixTimer;
    bDefMatrixTimer = 0;
    wDefMatrixHitPoints = 0;
    UnitApplyDefMatrix();
    bDefMatrixTimer = nTimer;
    wDefMatrixHitPoints = nMatrix;
  }
}

void CUnit::SyncIrradiate()
{
  if (bIrradiateTimer != 0) {
    if (!CanTargetWithWeapon(0x22, this)) {
      bIrradiateTimer = 0;
      pIrradiateSource = NULL;
      return;
    }

    BYTE bTimer = bIrradiateTimer;
    bIrradiateTimer = 0;
    UnitApplyIrradiate(pIrradiateSource, bIrradiatePlayer);
    bIrradiateTimer = bTimer;
  }
}

void CUnit::SyncLockdown()
{
  BYTE bTime = bLockdownTimer;
  bLockdownTimer = 0;
  unsigned int nTime = bTime;
  CreateOverlay(GetSizeType() + 0x169, 0, 0, 0);
  BYTE bCurrent = bLockdownTimer;
  bLockdownTimer = static_cast<BYTE>(nTime) > bCurrent
                       ? static_cast<BYTE>(nTime) : bCurrent;
  sub_434C60();
}

void CUnit::SyncParalyze()
{
  BYTE bTime = bParalyzeTimer;
  bParalyzeTimer = 0;
  unsigned int nTime = bTime;
  CreateOverlay(GetSizeType() + 0x3D6, 0, 0, 0);
  BYTE bCurrent = bParalyzeTimer;
  bParalyzeTimer = static_cast<BYTE>(nTime) > bCurrent
                       ? static_cast<BYTE>(nTime) : bCurrent;
  sub_434C60();
}

void CUnit::UnitApplyDefMatrix()
{
  if (bDefMatrixTimer == 0 && (dwFlags & UF_Burrowed) == 0) {
    BYTE bSize = GetSizeType();
    CreateOverlay(bSize + 0x173, 0, 0, 0);
    pSprite->AllocImageBelow(bSize + 0x176, 0, 0, 0);
  }
  bDefMatrixTimer = 168;
  wDefMatrixHitPoints = 0xFA00;
}

void CUnit::UnitApplyIrradiate(CUnit *pSource, BYTE bPlayer)
{
  if (bIrradiateTimer == 0 && (dwFlags & UF_Burrowed) == 0)
    CreateOverlay(GetSizeType() + 0x17C, 0, 0, 0);

  bIrradiateTimer = gnWeapCooldown[0x22];
  pIrradiateSource = pSource;
  bIrradiatePlayer = bPlayer;
}

void CUnit::UnitApplyLockdown(int nTime)
{
  if (bLockdownTimer == 0)
    CreateOverlay(GetSizeType() + 0x169, 0, 0, 0);
  BYTE bCurrent = bLockdownTimer;
  BYTE bTime = static_cast<BYTE>(nTime);
  bLockdownTimer = bTime > bCurrent ? bTime : bCurrent;
  sub_434C60();
}

void CUnit::UnitApplyMyopia(int nPlayer)
{
  if ((dwFlags & 0x40000000) != 0) {
    Suicide();
    return;
  }

  UnitMyopiaAdd(this, nPlayer);  // Hyperion: |= 1 << nPlayer, every slot
  gamesnd_play(0x3FB, this, 1, 0);
  CreateOverlay(GetSizeType() + 0x3D0, 0, 0, 0);
  status_set_update();
  dword_6663E4 = 1;
}

void CUnit::UnitApplyParalyze(int nTime)
{
  if (bParalyzeTimer == 0)
    CreateOverlay(GetSizeType() + 0x3D6, 0, 0, 0);
  BYTE bCurrent = bParalyzeTimer;
  BYTE bTime = static_cast<BYTE>(nTime);
  bParalyzeTimer = bTime > bCurrent ? bTime : bCurrent;
  sub_434C60();
}

void CUnit::UnitApplyStim()
{
  if (lHitPoints > 0xA00) {
    gamesnd_play(net_randbetween(0x116, 0x117, 0x1F), this, 1, 0);
    DamageApply(0xA00, NULL, -1, 1);
    if (bStimTimer < 37) {
      bStimTimer = 37;
      ResetMovementSpeed();
    }
  }
}

// Whether the AI may irradiate this unit: organic, not a building, and not a
// larva, egg or lurker egg.
BOOL CUnit::IsIrradiateTargetForAI()
{
  WORD nType = wType;
  if ((gulUnitTypeFlags[nType] & 0x10000) != 0 &&
      (gulUnitTypeFlags[nType] & 1) == 0 &&
      nType != 0x23 && nType != 0x24 && nType != 0x61)
    return TRUE;
  return FALSE;
}

// Restoration: clears every curable status, or kills a hallucination outright.
// The +0x121 clear goes through the scalar bParasiteFlags; the array spelling
// schedules the store before the ensnare test.
void CUnit::ApplyRestoration()
{
  BYTE bSize = GetSizeType();
  if ((dwFlags & 0x40000000) != 0) {
    Suicide();
    return;
  }

  CreateOverlay(bSize + 0x3C7, 0, 0, 0);
  bParasiteFlags = 0;
  bMyopiaPlayers = 0;
  UnitHxClear(this);  // Hyperion: the casters past 7
  if (bEnsnareTimer != 0) {
    bEnsnareTimer = 0;
    EnsnareEnd();
  }
  if (bBloodBoilTimer != 0) {
    bBloodBoilTimer = 0;
    BloodBoilEnd();
  }
  if (bIrradiateTimer != 0) {
    bIrradiateTimer = 0;
    IrradiateEnd();
  }
  if (bLockdownTimer != 0)
    LockdownEnd();
  if (bParalyzeTimer != 0)
    ParalyzeEnd();
  if (bCrunchyCount != 0)
    CrunchyEnd();
  status_set_update();
}
