// CUnitZSpells.cpp -- the zerg spell dispatch and the status it leaves.

#include <string.h>

#include "../h/CUnitZSpells.h"
#include "../h/CUnit.h"
#include "../h/CUnitBuild.h"
#include "../h/CSprite.h"
#include "../h/TrigAct.h"
#include "../gen/globals117.h"
#include "../h/playervision.h"  // Hyperion: the slots past 31 (stage D)

// Hyperion: the definition (find.cpp) reads the position as ints, and this
// TU's callers pass it as 16 bits. The view takes the definition's types, which
// the x64 build needs, and the macro narrows the arguments as 1.17's did
//.
extern "C" CUnit *__fastcall find_first_unit_near(int x, int y, int nRadius, CUnitZSpellsFindProc pfnFilter, CUnit *pContext);
#define find_first_unit_near(a0, a1, a2, a3, a4) (find_first_unit_near)(PosU(a0), PosU(a1), (a2), (a3), (a4))

void __fastcall CUnit::ApplyBloodBoil(int x, int y, CUnit *pAttacker)
{
  find_first_unit_near(x, y, 0x40, BloodBoilProc, pAttacker);
}

void CUnit::ApplyConsume(CUnit *pTarget)
{
  CSprite *pSprite = pTarget->pSprite;
  spos_t y = pSprite->wPosY;
  spos_t x = PosS(pSprite->wPosX);
  if (sub_434130(0x10, pTarget, 0xE4, x, y, NULL)) {
    count_add_killed(pTarget, bPlayer);
    pTarget->Suicide();
    if ((pTarget->dwFlags & 0x40000000) == 0) {
      unsigned int nEnergy = wEnergy + 0x3200;
      if (nEnergy > static_cast<unsigned int>(GetMaxEnergy()))
        nEnergy = GetMaxEnergy();
      wEnergy = static_cast<WORD>(nEnergy);
    }
  }
}

void __fastcall CUnit::ApplyCrunchy(int x, int y, int nPlayer,
                                    CUnit *pIgnoredTarget)
{
  // Hyperion: the search called as it is defined, the player where its context goes,
  // with the position narrowed as 1.17's 16-bit pushes did; a direct call through a pointer
  // of another type traps in WebAssembly
  find_first_unit_near(x, y, 0x40, reinterpret_cast<CUnitZSpellsFindProc>(CrunchyApplyProc),
                       reinterpret_cast<CUnit *>(static_cast<intptr_t>(nPlayer)));
}

void __fastcall CUnit::ApplyDarkSwarm(int x, int y, BYTE bPlayer)
{
  point16_t pos = {x, y};
  ClipToMap(&pos, 0xCA);
  CUnit *pUnit = CreateUnitAt(0xCA, pos.x, pos.y, 0x0B);
  if (pUnit == NULL) {
    ShowCreateError(bPlayer);
    return;
  }
  pUnit->dwFlags |= 0x00200000;
  pUnit->pSprite->bElevationLevel = 0x0B;
  pUnit->FinishedMaking();
  WORD wTime = pUnit->wTimeToLive;
  if (wTime == 0 || 0x384 < wTime)
    pUnit->wTimeToLive = 0x384;
}

void __fastcall CUnit::ApplyEnsnare(int x, int y, CUnit *pAttacker)
{
  // Hyperion: called as it is defined, the position narrowed to the shorts 1.17
  // passed; a direct call through a pointer of another type traps in WebAssembly
  CThingy *pThingy = CThingy::AllocThingy(0x16C, static_cast<short>(x), static_cast<short>(y), 0);
  if (pThingy != NULL) {
    CSprite *pThingySprite = *reinterpret_cast<CSprite **>(
        reinterpret_cast<BYTE *>(pThingy) + offsetof(CThingy, pSprite));
    pThingySprite->bElevationLevel = 0x13;
    pThingy->UpdateVisibility();
  }
  find_first_unit_near(x, y, 0x40, EnsnareProc, pAttacker);
}

void CUnit::ApplyUnitCrunchy()
{
  if (bCrunchyCount < 9)
    ++bCrunchyCount;

  unsigned int nLevel = bCrunchyCount >> 1;
  unsigned int nCappedLevel = nLevel < 3 ? nLevel : 3;
  int nImage = GetSizeType() * 4 + nCappedLevel + 0x3DA;
  if (!HasImageOverlay(nImage)) {
    RemoveImageOverlay(0x3DA, 0x3E5);
    nLevel = bCrunchyCount >> 1;
    nCappedLevel = nLevel < 3 ? nLevel : 3;
    CreateOverlay(GetSizeType() * 4 + nCappedLevel + 0x3DA, 0, 0, 0);
  }

  unsigned int nShortest = 99999;
  unsigned int nSlot = 0;
  for (unsigned int i = 0; i < 9; ++i) {
    if (bCrunchyTimers[i] == 0) {
      bCrunchyTimers[i] = 0x96;
      return;
    }
    if (bCrunchyTimers[i] < nShortest) {
      nSlot = i;
      nShortest = bCrunchyTimers[i];
    }
  }
  bCrunchyTimers[nSlot] = 0x96;
}

void CUnit::BloodBoilDamage()
{
  if ((dwFlags & 0x04000000) == 0) {
    int nDamage = (wpn_dmgAmt_g[60] << 8) / 76;
    if (lHitPoints > nDamage)
      DamageApply(nDamage, NULL, -1, 1);
  }
}

void CUnit::BloodBoilEnd()
{
  RemoveImageOverlay(0x184, 0x186);
  bBloodBoilTimer = 0;
}

int __fastcall CUnit::BloodBoilProc(CUnit *pCandidate, CUnit *pAttacker)
{
  if (pCandidate != pAttacker &&
      (pCandidate->dwFlags & 0x04000000) == 0 &&
      (pCandidate->dwFlags & UF_Burrowed) == 0) {
    pCandidate->UnitApplyBloodBoil();
    if (pAttacker != NULL)
      pCandidate->OnDamaged(pAttacker, 1);
  }
  return 0;
}

int __fastcall CUnit::CrunchyApplyProc(CUnit *pCandidate, int nPlayer)
{
  if (pCandidate->bPlayer != nPlayer &&
      (gulUnitTypeFlags[pCandidate->wType] & 1) == 0 &&
      (pCandidate->dwFlags & 4) != 0 &&
      (pCandidate->dwFlags & 0x04000000) == 0 &&
      ((pCandidate->dwFlags & 0x300) == 0 ||
       UnitDetectedBy(pCandidate, nPlayer))) {  // Hyperion: every slot (stage D)
    pCandidate->ApplyUnitCrunchy();
  }
  return 0;
}

void CUnit::CrunchyEnd()
{
  if (bCrunchyCount != 0) {
    bCrunchyCount = 0;
    memset(bCrunchyTimers, 0, sizeof(bCrunchyTimers));
    RemoveImageOverlay(0x3DA, 0x3E5);
  }
}

void CUnit::EnsnareEnd()
{
  RemoveImageOverlay(0x180, 0x182);
  bEnsnareTimer = 0;
  ResetMovementSpeed();
}

int __fastcall CUnit::EnsnareProc(CUnit *pCandidate, CUnit *pAttacker)
{
  if (pCandidate != pAttacker &&
      (gulUnitTypeFlags[pCandidate->wType] & 1) == 0 &&
      (pCandidate->dwFlags & 0x04000000) == 0 &&
      (pCandidate->dwFlags & UF_Burrowed) == 0) {
    pCandidate->UnitApplyEnsnare();
  }
  return 0;
}

void CUnit::HideSpells()
{
  if (wDefMatrixHitPoints != 0) {
    BYTE bTimer = bDefMatrixTimer;
    WORD wMatrix = wDefMatrixHitPoints;
    DamageDefMatrix(wDefMatrixHitPoints);
    wDefMatrixHitPoints = wMatrix;
    bDefMatrixTimer = bTimer;
  }
  if (bIrradiateTimer != 0)
    RemoveImageOverlay(0x17C, 0x17E);
  if (bEnsnareTimer != 0)
    RemoveImageOverlay(0x180, 0x182);
  if (bBloodBoilTimer != 0)
    RemoveImageOverlay(0x184, 0x186);
}

void CUnit::SyncBloodBoil()
{
  BYTE bTimer = bBloodBoilTimer;
  if (bTimer != 0) {
    bBloodBoilTimer = 0;
    if ((dwFlags & UF_Burrowed) == 0)
      CreateOverlay(GetSizeType() + 0x184, 0, 0, 0);
    bBloodBoilTimer = bTimer;
  }
}

void CUnit::SyncCrunchy()
{
  if (bCrunchyCount != 0) {
    unsigned int nLevel = bCrunchyCount >> 1;
    unsigned int nCappedLevel = nLevel < 3 ? nLevel : 3;
    CreateOverlay(GetSizeType() * 4 + nCappedLevel + 0x3DA, 0, 0, 0);
  }
}

void CUnit::SyncEnsnare()
{
  BYTE bTimer = bEnsnareTimer;
  if (bTimer != 0) {
    bEnsnareTimer = 0;
    if ((dwFlags & UF_Burrowed) == 0)
      CreateOverlay(GetSizeType() + 0x180, 0, 0, 0);
    bEnsnareTimer = 0x4B;
    ResetMovementSpeed();
    bEnsnareTimer = bTimer;
  }
}

void CUnit::UnitApplyBloodBoil()
{
  if (bBloodBoilTimer == 0) {
    if ((dwFlags & UF_Burrowed) == 0)
      CreateOverlay(GetSizeType() + 0x184, 0, 0, 0);
  }
  bBloodBoilTimer = 0x4B;
}

void CUnit::UnitApplyEnsnare()
{
  if (bEnsnareTimer == 0 && (dwFlags & UF_Burrowed) == 0)
    CreateOverlay(GetSizeType() + 0x180, 0, 0, 0);
  bEnsnareTimer = 0x4B;
  ResetMovementSpeed();
}

void CUnit::UnitApplySpawnBroodling(CUnit *pTarget)
{
  CUnit *pKiller = this;

  if ((pTarget->dwFlags & 0x40000000) == 0) {
    int x = PosS(pTarget->pSprite->wPosX) - 2;
    int y = pTarget->pSprite->wPosY - 2;

    for (int i = 0; i < 2; ++i, x += 4, y += 4) {
      if (!mtx_passable(0x28, x, y)) {
        point16_t input = {static_cast<short>(x), static_cast<short>(y)};
        point16_t output;
        rect16_t bounds = {
          static_cast<short>(x - 0x20),
          static_cast<short>(y - 0x20),
          static_cast<short>(x + 0x20),
          static_cast<short>(y + 0x20)
        };

        if (!::FindPlaceForUnit(this, &input, &output, &bounds, 0, 0))
          goto next_position;
        x = output.x;
        y = output.y;
      }

      {
        point16_t pos = {static_cast<short>(x), static_cast<short>(y)};
        ClipToMap(&pos, 0x28);

        CUnit *pBroodling = CreateUnitAt(0x28, pos.x, pos.y, bPlayer);
        if (pBroodling == NULL) {
          ShowCreateError(bPlayer);
        } else {
          pBroodling->FinishedMaking();
          WORD wMaxTimeToLive =
              static_cast<WORD>(pBroodling->GetMaxTimeToLive());
          if (pBroodling->wTimeToLive == 0 ||
              wMaxTimeToLive < pBroodling->wTimeToLive) {
            pBroodling->wTimeToLive = wMaxTimeToLive;
          }
          if (dwUnion134 != 0)
            register_unit_for_ai(this, pBroodling);
        }
      }
next_position:;
    }
  }

  count_add_killed(pTarget, pKiller->bPlayer);
  pTarget->Suicide();
  pKiller->AddKill();
}

void CUnit::UpdateCrunchyTimers()
{
  unsigned int nInitialCount = bCrunchyCount;
#define UPDATE_CRUNCHY_TIMER(N) \
  do { \
    if (bCrunchyTimers[N] != 0) { \
      --bCrunchyTimers[N]; \
      if (bCrunchyTimers[N] == 0) \
        --bCrunchyCount; \
    } \
  } while (0)
  UPDATE_CRUNCHY_TIMER(0);
  UPDATE_CRUNCHY_TIMER(1);
  UPDATE_CRUNCHY_TIMER(2);
  UPDATE_CRUNCHY_TIMER(3);
  UPDATE_CRUNCHY_TIMER(4);
  UPDATE_CRUNCHY_TIMER(5);
  UPDATE_CRUNCHY_TIMER(6);
  UPDATE_CRUNCHY_TIMER(7);
  UPDATE_CRUNCHY_TIMER(8);
#undef UPDATE_CRUNCHY_TIMER

  if (bCrunchyCount != 0) {
    unsigned int nLevel = bCrunchyCount >> 1;
    unsigned int nCappedLevel = nLevel < 3 ? nLevel : 3;
    int nImage = GetSizeType() * 4 + nCappedLevel + 0x3DA;
    if (!HasImageOverlay(nImage)) {
      RemoveImageOverlay(0x3DA, 0x3E5);
      nLevel = bCrunchyCount >> 1;
      nCappedLevel = nLevel < 3 ? nLevel : 3;
      CreateOverlay(GetSizeType() * 4 + nCappedLevel + 0x3DA, 0, 0, 0);
    }
  } else if (nInitialCount != 0) {
    RemoveImageOverlay(0x3DA, 0x3E5);
  }
}
