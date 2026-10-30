// Priority.cpp -- the sprite draw-priority family (oracle 0x47ED50 .. 0x47EF20): a
// max-heap of (priority, sprite) pairs the renderer pops in draw order.
//
// 1.17 keeps this family in a TU of its own, apart from CSprite.cpp: priority_add re-reads
// sguiTop after calling GetSpritePriority, which a compiler only emits when the callee is
// an extern it cannot see through.

#include "oc_data.h"  // Hyperion: OC_VA
#include "CSprite.h"
#include "Priority.h"
#include "../h/maplimits.h"  // Hyperion
#include "../h/poollimits.h"  // Hyperion: the pools' sizes (stage C)

extern "C" void *__cdecl memset(void *, int, size_t);

// The pair array is 1-based, like sgSprites: slot 0 is unused, so its extent is one more
// than the sprite pool's, where sgSprPriorityIterator's is the pool's. gdwLocalMask stays
// DWORD; priority_add's low-byte read is a cast at the use site.

// The pair record, as priority_add's two stores lay it out.
struct spr_priority_t {
  /*0x00*/ sprite_key_t dwPriority;  // Hyperion: DWORD in 1.17 (stage D)
  /*0x04*/ CSprite *pSprite;
};
OC_X86_LAYOUT(sizeof(spr_priority_t) == 0x8, "spr_priority_t stride  [eax*8+675278h] -- priority_add");

#ifdef OC_DLL_MODE
#define sgbVisHashTypeEnabled_g (reinterpret_cast<BYTE *>(OC_VA(0x5AEF30u)))
#define sgbPriorityVisHash_g    (reinterpret_cast<BYTE *>(OC_VA(0x5AF138u)))
#define gdwLocalMask            (*reinterpret_cast<DWORD *>(OC_VA(0x672668u)))
#define sgSprPriorities_g       (reinterpret_cast<spr_priority_t *>(OC_VA(0x675278u)))
#define sgSprPriorityIterator   (reinterpret_cast<CSprite **>(OC_VA(0x67A0A0u)))
#define sguiTop                 (*reinterpret_cast<DWORD *>(OC_VA(0x67C7B0u)))
#define sguiIndex               (*reinterpret_cast<DWORD *>(OC_VA(0x67C7B4u)))
#else
extern BYTE           sgbVisHashTypeEnabled_g[517];
// Hyperion: CSprite.cpp's, a row for each of the largest map's
extern BYTE           g_spritePriorityVisHash[];
extern DWORD          gdwLocalMask;
extern spr_priority_t sgSprPriorities_g[POOL_SPRITES_STORE + 1] OC_UNALIGNED;
extern CSprite       *sgSprPriorityIterator[POOL_SPRITES_STORE] OC_UNALIGNED;
extern DWORD          sguiTop;
extern DWORD          sguiIndex;
#endif

// Sift the pair just pushed at sguiTop up the max-heap by priority. The parent is copied as
// a whole record, which is what lets MSVC reuse the compare's load.
void priority_sort() {
  DWORD i = sguiTop;
  DWORD nParent = i >> 1;
  sprite_key_t dwPriority = sgSprPriorities_g[i].dwPriority;  // Hyperion: DWORD in 1.17
  CSprite *pSprite = sgSprPriorities_g[i].pSprite;

  while (i > 1 && sgSprPriorities_g[nParent].dwPriority < dwPriority) {
    sgSprPriorities_g[i] = sgSprPriorities_g[nParent];
    i = nParent;
    nParent >>= 1;
  }
  sgSprPriorities_g[i].pSprite = pSprite;
  sgSprPriorities_g[i].dwPriority = dwPriority;
}

void priority_init() {
  sguiTop = 0;
  memset(g_spritePriorityVisHash, 0, MAP_SIDE_MAX);  // Hyperion: 0x100 in 1.17
}

// Pop the whole heap into sgSprPriorityIterator, highest priority first, and return the
// lowest; priority_iter_next walks back up from there. The advance is written in both arms,
// which MSVC merges after allocation.
CSprite *priority_iter_begin() {
  DWORD n = sguiTop;
  if (n == 0)
    return 0;
  sguiIndex = n - 1;

  CSprite **ppOut = sgSprPriorityIterator;
  spr_priority_t *pLast = &sgSprPriorities_g[n];
  do {
    sprite_key_t dwPriority = pLast->dwPriority;  // Hyperion: DWORD in 1.17
    --n;
    *ppOut = sgSprPriorities_g[1].pSprite;
    sgSprPriorities_g[1].dwPriority = dwPriority;
    CSprite *pSprite = pLast->pSprite;
    --pLast;
    sgSprPriorities_g[1].pSprite = pSprite;

    DWORD i = 1;
    DWORD nHalf = n >> 1;
    while (i <= nHalf) {
      DWORD nChild = i * 2;
      if (nChild < n && sgSprPriorities_g[nChild].dwPriority < sgSprPriorities_g[nChild + 1].dwPriority)
        ++nChild;
      if (dwPriority >= sgSprPriorities_g[nChild].dwPriority)
        break;
      sgSprPriorities_g[i] = sgSprPriorities_g[nChild];
      i = nChild;
    }
    if (n)
      ++ppOut;
    else
      ++ppOut;
    sgSprPriorities_g[i].dwPriority = dwPriority;
    sgSprPriorities_g[i].pSprite = pSprite;
  } while (n > 0);

  sguiTop = n;
  return sgSprPriorityIterator[sguiIndex];
}

// Push one sprite onto the 1-based
//           priority pair array, keep it sorted, and toggle the sprite's row bit in the
//           vis-hash map when its type participates. sguiTop is reloaded after the
//           GetSpritePriority call, an extern this TU cannot see through.
void priority_add(CSprite *pSprite) {
  sguiTop++;
  sgSprPriorities_g[sguiTop].pSprite = pSprite;
  sgSprPriorities_g[sguiTop].dwPriority = pSprite->GetSpritePriority();
  if (sguiTop > 1)
    priority_sort();
  if (sgbVisHashTypeEnabled_g[pSprite->wSpriteID])
    g_spritePriorityVisHash[pSprite->GetYTile()] ^= (BYTE)gdwLocalMask;
}

// Pop the iterator stack.
CSprite *priority_iter_next() {
  DWORD n = sguiIndex;
  if (n == 0)
    return 0;
  n--;
  sguiIndex = n;
  return sgSprPriorityIterator[n];
}
