// CUnitCloak.cpp -- unit cloak state transitions. Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the oracle (WPO
// off) never does across a TU; that is what the __declspec(noinline) markers on some
// declarations are for, and why moving a body between TUs can change the code generated.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>                 // memmove -- ShuffleColorSlots's shuffle

#include "../h/CUnitCloak.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/CUnitTSpells.h"      // ours: gamesnd_play, net_randbetween, status_set_update
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one over the
// block, so adding a name cannot undeclare the ones below it.
//
// Neither address is in re/globals117.json or the sheets, so both keep the
// registry's addressed spelling until a name is minted.

// 0x67294A -- `cmp byte ptr [0x67294A], 0h`, the only reference in ShuffleColorSlots.
#ifndef byte_67294A
#ifdef OC_DLL_MODE
#define byte_67294A (*reinterpret_cast<BYTE *>(OC_VA(0x67294Au)))
#else
extern BYTE byte_67294A;
#endif
#endif

// 0x5264FC -- `mov dword ptr [esi*4+5264FCh], eax` with esi counting 7..0, so
// eight DWORD slots, written with a permutation of 0..7.
#ifndef dword_5264FC
#ifdef OC_DLL_MODE
#define dword_5264FC (reinterpret_cast<DWORD *>(OC_VA(0x5264FCu)))
#else
extern DWORD dword_5264FC[8] OC_UNALIGNED;
#endif
#endif

// The bodies, in oracle VA order.

// Cloaks a unit and its subunit, or finishes the cloak transition.
void CUnit::sub_41D200(int nSound)
{
  if ((dwFlags & 0x10) == 0)
    gamesnd_play(nSound, this, 1, 0);
  if (gPlayback == 0 && bPlayer == g_LocalNationID)
    status_set_update();

  DWORD dwState = dwFlags;
  if (dwState & 0x10) {
    if ((dwState & 0x100) == 0) {
      sub_4340D0();
      pSprite->sub_415E50(3, 0, 0);
      dwFlags |= 0x300;
      SetBackgroundOrder(0x6D);
      dwUnionE4 = 0x80000000;
    }
  } else {
    pSprite->Cloak();
    dwUnionE4 = 0x80000000;
    dwFlags = (dwFlags & ~0x200) | 0x100;
    CUnit *pSub = subunit;
    if (pSub != NULL) {
      pSub->pSprite->Cloak();
      pSub->dwUnionE4 = 0x80000000;
      pSub->dwFlags = (pSub->dwFlags & ~0x200) | 0x100;
    }
  }
  sub_4325A0();
  _unmapped_90[1] = 0x1E;
}

// Starts a cloak: burrow-style, without the sound or the subunit walk. The
// sprite goes into a local before the +0x91 store, which is what puts its load
// ahead of the three pushes.
void CUnit::sub_41D300()
{
  sub_4340D0();
  if (gPlayback == 0 && bPlayer == g_LocalNationID)
    status_set_update();
  CSprite *pSp = pSprite;
  _unmapped_90[1] = 0;
  pSp->sub_415E50(3, 0, 0);
  dwFlags |= 0x300;
  SetBackgroundOrder(0x6D);
  dwUnionE4 = 0x80000000;
}

// Decloaks a unit and its subunit, or finishes the decloak transition.
void CUnit::sub_41D360(int nSound)
{
  if ((dwFlags & 0x10) == 0)
    gamesnd_play(nSound, this, 1, 0);
  if (gPlayback == 0 && bPlayer == g_LocalNationID)
    status_set_update();

  if (dwFlags & 0x10) {
    pSprite->ShowNormal();
    pSprite->ShowCloaked();
    dwFlags &= ~0x300;
    return;
  }
  pSprite->Decloak();
  dwFlags = (dwFlags & ~0x100) | 0x200;
  CUnit *pSub = subunit;
  if (pSub != NULL) {
    pSub->pSprite->Decloak();
    pSub->dwFlags = (pSub->dwFlags & ~0x100) | 0x200;
  }
}

// sub_41D300's guarded twin: does nothing once the cloak bit is already set.
void CUnit::sub_41D410()
{
  if (dwFlags & 0x100)
    return;
  sub_4340D0();
  pSprite->sub_415E50(3, 0, 0);
  dwFlags |= 0x300;
  SetBackgroundOrder(0x6D);
  dwUnionE4 = 0x80000000;
}

// Fills the eight slots with a random permutation of 0..7.
void ShuffleColorSlots(int nArg0)
{
  BYTE bPool[8];

  if (byte_67294A != 0 && nArg0 == 0)
    return;

  int i = 8;
  do {
    --i;
    bPool[i] = static_cast<BYTE>(i);
  } while (i != 0);

  int n = 8;
  do {
    --n;
    BYTE bPick = static_cast<BYTE>(net_randbetween(0, n, 0x1C));
    dword_5264FC[n] = bPool[bPick];
    memmove(&bPool[bPick], &bPool[bPick + 1], n - bPick);
  } while (n != 0);
}

// Writes the two cloak bits of dwFlags from the low two bits of the argument.
void CUnit::SetCloakStatus(BYTE bStatus)
{
  dwFlags = (dwFlags & ~0x300) | ((static_cast<DWORD>(bStatus) << 8) & 0x300);
}
