// CThingy.cpp -- the thingy pools: 500 doodad and effect thingies (sgThingies1Arr_g
// @0x5C5AE0) and 500 fogged-unit thingies (sgThingies2Arr_g @0x5C7A30), each 16 bytes and
// each with a free and a used CLink list; the black-X marker; and the fogged-sprite
// right-click helpers.
//
// A body here that emits a real call to a callee in this TU depends on that callee not
// being inlined, and the other way round, so adding or removing a definition in this file
// can change another body's code.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playervision.h"  // Hyperion: the slots past 7
#include "../h/poollimits.h"  // Hyperion: the pools' sizes (stage C)
#include "../port/data/port_formats.h"  // Hyperion
#include "../gen/save117.h"
#include "CThingy.h"
#include "CSprite.h"
#include "CLink.h"
#include "CUnit.h"     // IsUnitRClickedCb / GetRClickTarget: pUnit->wType, ->pSprite
#include "CBullet.h"   // find_first_unit_near + pfn_bounce_filter_t (the search GetRClickTarget drives)
#include "minimap.h"   // AllocBlackX is DECLARED there -- do NOT redeclare (CThingy.h:175)
#include "usel.h"      // usel_type_unselectable(WORD)
#include "point16.h"   // point16_t -- gUnitPlaceboxSize / sub_418000's by-value argument
#include "saveload.h"  // pfread / decompress
#include "Storm.h"     // SMemAlloc / SMemFree
#include <string.h>    // memset
#include <stddef.h>    // offsetof

// The globals this TU reaches, each spelled for either build.
//
//   0x5C5AE0  sgThingies1Arr_g[500]   doodad and effect thingy pool, 16 B each, ending
//                                     at 0x5C7A20
//   0x5C7A20/24  free head, tail      0x5C7A28/2C  used head, tail   (pool 1)
//   0x5C7A30  sgThingies2Arr_g[500]   fogged-unit thingy pool, ending at 0x5C9970
//   0x5C9970/74  free head, tail      0x5C9978/7C  used head, tail   (pool 2)
//   0x5C9984  gBlackX                 the black-X thingy (InitThingies)
//   0x5C9988  gbBlackXActive          the byte gate DrawBlackX reads
#ifdef OC_DLL_MODE
#define sgThingies1Arr_g      (reinterpret_cast<CThingy *>(OC_VA(0x5C5AE0u)))
#define thingies1_sgpFreeHead (*reinterpret_cast<CThingy **>(OC_VA(0x5C7A20u)))
#define thingies1_sgpFreeTail (*reinterpret_cast<CThingy **>(OC_VA(0x5C7A24u)))
#define thingies1_sgpUsedHead (*reinterpret_cast<CThingy **>(OC_VA(0x5C7A28u)))
#define thingies1_sgpUsedTail (*reinterpret_cast<CThingy **>(OC_VA(0x5C7A2Cu)))
#define sgThingies2Arr_g      (reinterpret_cast<CThingy *>(OC_VA(0x5C7A30u)))
#define thingies2_sgpFreeHead (*reinterpret_cast<CThingy **>(OC_VA(0x5C9970u)))
#define thingies2_sgpFreeTail (*reinterpret_cast<CThingy **>(OC_VA(0x5C9974u)))
#define thingies2_sgpUsedHead (*reinterpret_cast<CThingy **>(OC_VA(0x5C9978u)))
#define thingies2_sgpUsedTail (*reinterpret_cast<CThingy **>(OC_VA(0x5C997Cu)))
#define gwMapWidth            (*reinterpret_cast<WORD *>(OC_VA(0x5264B4u)))
#define gpSquareMap           (*reinterpret_cast<DWORD **>(OC_VA(0x666454u)))
#define gdwLocalMask          (*reinterpret_cast<DWORD *>(OC_VA(0x672668u)))
#else
extern CThingy  sgThingies1Arr_g[POOL_THINGIES_STORE];
extern CThingy *thingies1_sgpFreeHead;
extern CThingy *thingies1_sgpFreeTail;
extern CThingy *thingies1_sgpUsedHead;
extern CThingy *thingies1_sgpUsedTail;
extern CThingy  sgThingies2Arr_g[POOL_THINGIES_STORE];
extern CThingy *thingies2_sgpFreeHead;
extern CThingy *thingies2_sgpFreeTail;
extern CThingy *thingies2_sgpUsedHead;
extern CThingy *thingies2_sgpUsedTail;
extern WORD     gwMapWidth;
extern DWORD   *gpSquareMap;
extern DWORD    gdwLocalMask;
#endif
#ifdef OC_DLL_MODE
#define gBlackX               (*reinterpret_cast<CThingy **>(OC_VA(0x5C9984u)))
#define gbBlackXActive        (*reinterpret_cast<BYTE *>(OC_VA(0x5C9988u)))
#else
extern CThingy *gBlackX;
extern BYTE     gbBlackXActive;
#endif

#define THINGY_POOL_SIZE g_pools.thingies

//   0x5264B6  gwMapHeight           WORD
//   0x67266C  gdwLocalBlackMask     DWORD, the pair of gdwLocalMask at 0x672668
//   0x67CC84  gdwReplayVisionMask   DWORD    0x67CC88  sgbShowAll_g  DWORD
//   0x67CC90  gPlayback             DWORD
//   0x522728  gUnitPlaceboxSize     point16_t[228]
//   0x5C9980  gnRClickX             short, with 0x5C9982 gnRClickY. GetRClickTarget
//             stores them 16 bits wide and IsUnitRClickedCb reads them with movsx, so
//             both are signed.
#ifdef OC_DLL_MODE
#ifndef gwMapHeight
#define gwMapHeight         (*reinterpret_cast<WORD *>(OC_VA(0x5264B6u)))
#endif
#ifndef gdwLocalBlackMask
#define gdwLocalBlackMask   (*reinterpret_cast<DWORD *>(OC_VA(0x67266Cu)))
#endif
#ifndef gdwReplayVisionMask
#define gdwReplayVisionMask (*reinterpret_cast<DWORD *>(OC_VA(0x67CC84u)))
#endif
#ifndef sgbShowAll_g
#define sgbShowAll_g        (*reinterpret_cast<DWORD *>(OC_VA(0x67CC88u)))
#endif
#ifndef gPlayback
#define gPlayback           (*reinterpret_cast<DWORD *>(OC_VA(0x67CC90u)))
#endif
#ifndef gUnitPlaceboxSize
#define gUnitPlaceboxSize   (reinterpret_cast<point16_t *>(OC_VA(0x522728u)))
#endif
#ifndef gnRClickX
#define gnRClickX           (*reinterpret_cast<short *>(OC_VA(0x5C9980u)))
#endif
#ifndef gnRClickY
#define gnRClickY           (*reinterpret_cast<short *>(OC_VA(0x5C9982u)))
#endif
#else
extern WORD      gwMapHeight;
extern DWORD     gdwLocalBlackMask;
extern DWORD     gdwReplayVisionMask;
extern DWORD     sgbShowAll_g;
extern DWORD     gPlayback;
extern point16_t gUnitPlaceboxSize[228] OC_UNALIGNED;
extern spos_t    gnRClickX;
extern spos_t    gnRClickY;
#endif

// 0x41D550, unported and declared nowhere else in the tree. This file's other free
// functions are declared in src117/h/CThingy.h instead, which is where a hooked row's
// declaration has to sit.
BYTE __fastcall get_selection_circle_color(BYTE bPlayer);

// The RecreateThingyList queries (0x418820 / 0x418830, 8 B each).
BOOL QueryNullSprite(CSprite *pSprite) { return pSprite == 0; }
BOOL QueryValidSprite(CSprite *pSprite) { return pSprite != 0; }

// CThingy::GetDoodadThingy -- oracle 0x418370 (17 B). 1-based id -> entry.
CThingy *CThingy::GetDoodadThingy(int nId) {
  if (nId <= 0 || nId > g_pools.thingies)  // Hyperion: a save's id past the pool is none
    return 0;
  return &sgThingies1Arr_g[nId - 1];
}

// CThingy::GetDoodadThingyID -- oracle 0x418390 (20 B). The inverse.
int CThingy::GetDoodadThingyID(CThingy *pThingy) {
  if (pThingy == 0)
    return 0;
  DWORD_PTR off = reinterpret_cast<DWORD_PTR>(pThingy);
  off -= reinterpret_cast<DWORD_PTR>(sgThingies1Arr_g);
  return (int)(off / sizeof(CThingy) + 1);
}

// CThingy::AllocThingy -- oracle 0x417B80 (116 B). Pop a free doodad-pool node,
// give it a sprite, prepend to the used list; on Create failure put it back
// on the free list and return NULL.
CThingy *CThingy::AllocThingy(int nType, int x, int y, int nPlayer) {
  CThingy *pThingy = thingies1_sgpFreeHead;
  if (pThingy == 0)
    return 0;
  reinterpret_cast<CLink *>(pThingy)->Unlink(
      reinterpret_cast<CLink **>(&thingies1_sgpFreeHead),
      reinterpret_cast<CLink **>(&thingies1_sgpFreeTail));
  pThingy->nHitPoints = 1;
  pThingy->pSprite = CSprite::Create(nType, x, y, nPlayer);
  if (pThingy->pSprite == 0) {
    reinterpret_cast<CLink *>(pThingy)->PrependThis(
        reinterpret_cast<CLink **>(&thingies1_sgpFreeHead),
        reinterpret_cast<CLink **>(&thingies1_sgpFreeTail));
    return 0;
  }
  reinterpret_cast<CLink *>(pThingy)->PrependThis(
      reinterpret_cast<CLink **>(&thingies1_sgpUsedHead),
      reinterpret_cast<CLink **>(&thingies1_sgpUsedTail));
  return pThingy;
}

// The empty swap this forwards to lives in another TU; /OPT:ICF folded it onto 0x4BE950.
void __fastcall thingy_swap_nothing(CThingy *pThingy);

// 0x417C00. Nothing of its own to swap. Defined in this TU so LoadThingyList
// inlines it to a direct call of the empty swap, as the oracle does.
void CThingy::BitSwap() {
  thingy_swap_nothing(this);
}

// CThingy::DecodeListFromSave -- oracle 0x418240 (139 B). Both pools: sprite
// id -> pointer (a real call to CSprite::GetSprite here), 1-based pool index
// -> pointer for the two links.
void CThingy::DecodeListFromSave() {
  CThingy *p;
  CThingy *q;
  DWORD n;

  p = sgThingies1Arr_g;
  n = THINGY_POOL_SIZE;
  while (n != 0) {
    n--;
    p->pSprite = CSprite::GetSprite((int)reinterpret_cast<intptr_t>(p->pSprite));
    q = p->pPrev;
    if (q != 0)
      q = &sgThingies1Arr_g[reinterpret_cast<intptr_t>(q) - 1];
    p->pPrev = q;
    q = p->pNext;
    if (q != 0)
      q = &sgThingies1Arr_g[reinterpret_cast<intptr_t>(q) - 1];
    p->pNext = q;
    p++;
  }

  p = sgThingies2Arr_g;
  n = THINGY_POOL_SIZE;
  while (n != 0) {
    n--;
    p->pSprite = CSprite::GetSprite((int)reinterpret_cast<intptr_t>(p->pSprite));
    q = p->pPrev;
    if (q != 0)
      q = &sgThingies2Arr_g[reinterpret_cast<intptr_t>(q) - 1];
    p->pPrev = q;
    q = p->pNext;
    if (q != 0)
      q = &sgThingies2Arr_g[reinterpret_cast<intptr_t>(q) - 1];
    p->pNext = q;
    p++;
  }
}

// Rebuilds one list from the pool entries whose sprite satisfies the query, in
// pool order.
#if !defined(OC_PAYLOAD) || defined(OC_WITH_HELD)
void CThingy::RecreateThingyList(CThingy **ppHead, CThingy **ppTail,
                                 CThingy *pPool, int nCount,
                                 thingy_sprite_query_t pfnQuery) {
  const unsigned wCount = static_cast<unsigned>(nCount);  // Hyperion: a WORD in 1.17
  CThingy *pLast;
  unsigned i = 0;
  while (i < wCount && pfnQuery(pPool[i].pSprite) == 0)
    i++;
  if (i == wCount) {
    *ppHead = 0;
    *ppTail = 0;
    return;
  }
  CThingy *pFirst = &pPool[i];
  *ppTail = *ppHead = pFirst;
  pFirst->pPrev = 0;
  pFirst->pNext = 0;
  pLast = *ppHead;
  for (i++; i < wCount; i++) {
    CThingy *pNode = &pPool[i];
    if (pfnQuery(pNode->pSprite)) {
      pNode->pPrev = 0;
      pNode->pNext = 0;
      reinterpret_cast<CLink *>(pLast)->AppendLink(
          reinterpret_cast<CLink *>(pNode),
          reinterpret_cast<CLink **>(ppTail));
      pLast = pNode;
    }
  }
}
#endif  // OC_PAYLOAD

// CThingy::RecreateThingyLists -- oracle 0x418910 (121 B).
void CThingy::RecreateThingyLists() {
  RecreateThingyList(&thingies1_sgpFreeHead, &thingies1_sgpFreeTail,
                     sgThingies1Arr_g, THINGY_POOL_SIZE, QueryNullSprite);
  RecreateThingyList(&thingies1_sgpUsedHead, &thingies1_sgpUsedTail,
                     sgThingies1Arr_g, THINGY_POOL_SIZE, QueryValidSprite);
  RecreateThingyList(&thingies2_sgpFreeHead, &thingies2_sgpFreeTail,
                     sgThingies2Arr_g, THINGY_POOL_SIZE, QueryNullSprite);
  RecreateThingyList(&thingies2_sgpUsedHead, &thingies2_sgpUsedTail,
                     sgThingies2Arr_g, THINGY_POOL_SIZE, QueryValidSprite);
}

// CThingy::UpdateThingies -- oracle 0x418990 (56 B). The next pointer is read
// before the call: UpdateThingy1/2 may unlink the node.
void CThingy::UpdateThingies() {
  CThingy *pNext;
  for (CThingy *p = thingies1_sgpUsedHead; p != 0; p = pNext) {
    pNext = p->pNext;
    p->UpdateThingy1();
  }
  for (CThingy *p = thingies2_sgpUsedHead; p != 0; p = pNext) {
    pNext = p->pNext;
    p->UpdateThingy2();
  }
}

// CThingy::UpdateVisibility -- oracle 0x417E50 (150 B). Recompute the sprite's
// visibility byte from the fog map at its tile; returns TRUE when the sprite
// just went from visible-to-local-player to hidden.
BOOL CThingy::UpdateVisibility() {
  CSprite *pSprite = this->pSprite;
  if (pSprite == 0 || (pSprite->flags.b & 0x20))
    return FALSE;
  BYTE bOld = pSprite->bVisibilityFlags;
  BYTE bNew = (BYTE) ~(BYTE)gpSquareMap[(pSprite->wPosY / 32) * gwMapWidth +
                                        (PosS(pSprite->wPosX) / 32)];
  if (g_hxPlayers)  // Hyperion: the slots past 7
    VisionHxSpriteSet(pSprite, VisionHxRect(PosS(pSprite->wPosX) / 32, pSprite->wPosY / 32, 1, 1, false));
  if (bOld == bNew)
    return FALSE;
  bOld &= (BYTE)gdwLocalMask;
  pSprite->SetMask(bNew);
  if (bOld != 0) {
    if (!(this->pSprite->bVisibilityFlags & (BYTE)gdwLocalMask))
      return TRUE;
  }
  return FALSE;
}

// CThingy::DrawBlackX -- oracle 0x418BE0 (52 B).
void CThingy::DrawBlackX() {
  if (gbBlackXActive) {
    gBlackX->pSprite->UpdateDrawInfo();
    gBlackX->pSprite->Draw();
    gBlackX->pSprite->MakeDirty();
  }
}

// CThingy::AllocFromSprite -- oracle 0x417B10 (104 B). The fogged pool's
// allocator: AllocThingy with CSprite::Duplicate for Create and the pool-2
// lists. `mov di, cx` keeps the WORD parameter across the calls.
CThingy *CThingy::AllocFromSprite(WORD wUnitType, CSprite *pSprite) {
  CThingy *pThingy = thingies2_sgpFreeHead;
  if (pThingy == 0)
    return 0;
  reinterpret_cast<CLink *>(pThingy)->Unlink(
      reinterpret_cast<CLink **>(&thingies2_sgpFreeHead),
      reinterpret_cast<CLink **>(&thingies2_sgpFreeTail));
  CSprite *pNewSprite = pSprite->Duplicate();
  pThingy->nHitPoints = 1;
  pThingy->pSprite = pNewSprite;
  if (pNewSprite == 0) {
    reinterpret_cast<CLink *>(pThingy)->PrependThis(
        reinterpret_cast<CLink **>(&thingies2_sgpFreeHead),
        reinterpret_cast<CLink **>(&thingies2_sgpFreeTail));
    return 0;
  }
  pThingy->wUnitType = wUnitType;
  reinterpret_cast<CLink *>(pThingy)->PrependThis(
      reinterpret_cast<CLink **>(&thingies2_sgpUsedHead),
      reinterpret_cast<CLink **>(&thingies2_sgpUsedTail));
  return pThingy;
}

// CThingy::Init -- oracle 0x4183B0 (47 B). `ret 10h`. Tests the Create result
// in EAX (a local), not the re-read field: `xor ecx,ecx / setnz cl` needs a
// zeroed register because EAX still holds the pointer.
BOOL CThingy::Init(int nSpriteID, int x, int y, int nPlayer) {
  this->nHitPoints = 1;
  CSprite *pSprite = CSprite::Create(nSpriteID, x, y, nPlayer);
  this->pSprite = pSprite;
  return pSprite != 0;
}

// CThingy::FirstFoggedThingy -- oracle 0x418360 (6 B). The oracle loads the
// global with the one-byte-shorter `A1` accumulator form; see the wave report.
CThingy *CThingy::FirstFoggedThingy() {
  CThingy *pThingy = thingies2_sgpUsedHead;
  return pThingy;
}

// CThingy::SpriteUpdate -- oracle 0x418B20 (33 B). The field is re-read after
// Update() (the call may alias it); a local copy would keep a callee-saved
// register the oracle never saves.
void CThingy::SpriteUpdate() {
  if (this->pSprite != 0) {
    this->pSprite->Update();
    if (this->pSprite->pImageHead == 0)
      this->pSprite = 0;
  }
}
// CThingy::EncodeListForSave -- oracle 0x4182D0 (139 B). The spine
// DecodeListFromSave inverted: the sprite pointer becomes its 1-based id via a
// real call to CSprite::GetSpriteID, and each link becomes its 1-based pool
// index (unsigned byte difference >> 4, +1 -- GetDoodadThingyID's arithmetic,
// written out inline: the oracle has no call here). The decoded/encoded link
// is stored unconditionally (the jz lands on the store -- spine lesson).
void CThingy::EncodeListForSave() {
  CThingy *p;
  CThingy *q;
  DWORD n;

  p = sgThingies1Arr_g;
  n = THINGY_POOL_SIZE;
  while (n != 0) {
    n--;
    p->pSprite = reinterpret_cast<CSprite *>((DWORD_PTR)CSprite::GetSpriteID(p->pSprite));
    q = p->pPrev;
    if (q != 0) {
      DWORD_PTR off = reinterpret_cast<DWORD_PTR>(q) -
                  reinterpret_cast<DWORD_PTR>(sgThingies1Arr_g);
      q = reinterpret_cast<CThingy *>(off / sizeof(CThingy) + 1);
    }
    p->pPrev = q;
    q = p->pNext;
    if (q != 0) {
      DWORD_PTR off = reinterpret_cast<DWORD_PTR>(q) -
                  reinterpret_cast<DWORD_PTR>(sgThingies1Arr_g);
      q = reinterpret_cast<CThingy *>(off / sizeof(CThingy) + 1);
    }
    p->pNext = q;
    p++;
  }

  p = sgThingies2Arr_g;
  n = THINGY_POOL_SIZE;
  while (n != 0) {
    n--;
    p->pSprite = reinterpret_cast<CSprite *>((DWORD_PTR)CSprite::GetSpriteID(p->pSprite));
    q = p->pPrev;
    if (q != 0) {
      DWORD_PTR off = reinterpret_cast<DWORD_PTR>(q) -
                  reinterpret_cast<DWORD_PTR>(sgThingies2Arr_g);
      q = reinterpret_cast<CThingy *>(off / sizeof(CThingy) + 1);
    }
    p->pPrev = q;
    q = p->pNext;
    if (q != 0) {
      DWORD_PTR off = reinterpret_cast<DWORD_PTR>(q) -
                  reinterpret_cast<DWORD_PTR>(sgThingies2Arr_g);
      q = reinterpret_cast<CThingy *>(off / sizeof(CThingy) + 1);
    }
    p->pNext = q;
    p++;
  }
}

// CThingy::PostLoad -- oracle 0x418700 (129 B), `ret 8h`. The sprite pointer is decoded
// first and stored unconditionally; then, only when the links are live, the pool argument
// selects which pool the 1-based link ids belong to: NULL -> the doodad pool,
// sgThingies2Arr_g -> the fogged pool, anything else -> no link work at all.
//
// The two links are spelled differently on purpose. The first is an if plus a store, whose
// `jz` lands on the shared store; the second is a ternary with the zero arm first, which
// is what gives that arm its own `xor eax,eax` block and the transform arm its own store
// and epilogue, as 1.17 has them.
void CThingy::PostLoad(BOOL bLinked, CThingy *pPool) {
  this->pSprite = CSprite::GetSprite((int)reinterpret_cast<intptr_t>(this->pSprite));
  if (!bLinked)
    return;
  if (pPool == 0) {
    CThingy *q = this->pPrev;
    if (q != 0)
      q = &sgThingies1Arr_g[reinterpret_cast<intptr_t>(q) - 1];
    this->pPrev = q;
    q = this->pNext;
    this->pNext = q == 0 ? 0 : &sgThingies1Arr_g[reinterpret_cast<intptr_t>(q) - 1];
  } else if (pPool == sgThingies2Arr_g) {
    CThingy *q = this->pPrev;
    if (q != 0)
      q = &sgThingies2Arr_g[reinterpret_cast<intptr_t>(q) - 1];
    this->pPrev = q;
    q = this->pNext;
    this->pNext = q == 0 ? 0 : &sgThingies2Arr_g[reinterpret_cast<intptr_t>(q) - 1];
  }
}

// CThingy::PreSave -- oracle 0x418790 (129 B), `ret 8h`. PostLoad's mirror:
// GetSpriteID, and the links become 1-based pool indices (unsigned byte
// difference >> 4, +1). Second link uses the same ternary shape PostLoad
// proved (zero arm materializes inline via xor, transform arm forward).
void CThingy::PreSave(BOOL bLinked, CThingy *pPool) {
  this->pSprite = reinterpret_cast<CSprite *>((DWORD_PTR)CSprite::GetSpriteID(this->pSprite));
  if (!bLinked)
    return;
  if (pPool == 0) {
    CThingy *q = this->pPrev;
    if (q != 0) {
      DWORD_PTR off = reinterpret_cast<DWORD_PTR>(q) -
                  reinterpret_cast<DWORD_PTR>(sgThingies1Arr_g);
      q = reinterpret_cast<CThingy *>(off / sizeof(CThingy) + 1);
    }
    this->pPrev = q;
    q = this->pNext;
    this->pNext = q == 0 ? 0
                         : reinterpret_cast<CThingy *>(
                               (reinterpret_cast<DWORD_PTR>(q) -
                                reinterpret_cast<DWORD_PTR>(sgThingies1Arr_g)) /
                                   sizeof(CThingy) +
                               1);
  } else if (pPool == sgThingies2Arr_g) {
    CThingy *q = this->pPrev;
    if (q != 0) {
      DWORD_PTR off = reinterpret_cast<DWORD_PTR>(q) -
                  reinterpret_cast<DWORD_PTR>(sgThingies2Arr_g);
      q = reinterpret_cast<CThingy *>(off / sizeof(CThingy) + 1);
    }
    this->pPrev = q;
    q = this->pNext;
    this->pNext = q == 0 ? 0
                         : reinterpret_cast<CThingy *>(
                               (reinterpret_cast<DWORD_PTR>(q) -
                                reinterpret_cast<DWORD_PTR>(sgThingies2Arr_g)) /
                                   sizeof(CThingy) +
                               1);
  }
}

// CThingy::LoadThingies -- oracle 0x4185C0 (64 B). Both pools through
// LoadThingyList, the two FALSE returns sharing one materialization, then the codec.
// pFile lives in ESI across the calls, 1.17 having no frame here.
BOOL CThingy::LoadThingies(void *pFile) {
  void *pF = pFile;
  if (!LoadThingyList(pF, sgThingies1Arr_g, THINGY_POOL_SIZE))
    return FALSE;
  if (!LoadThingyList(pF, sgThingies2Arr_g, THINGY_POOL_SIZE))
    return FALSE;
  DecodeListFromSave();
  RecreateThingyLists();
  return TRUE;
}

#pragma pack(push, 1)
struct thingy_saveentry_t {
  /*0x00*/ WORD idx;
  /*0x02*/ CThingy thingy;
};
#pragma pack(pop)
OC_X86_LAYOUT(sizeof(thingy_saveentry_t) == 0x12, "lea esi, [eax+eax*8] / add esi, esi");
static_assert(offsetof(thingy_saveentry_t, idx) == 0x00, "movzx ecx, word ptr [edi]");
static_assert(offsetof(thingy_saveentry_t, thingy) == 0x02, "movdqu xmm0, [edi+2h]");

// 0x418600. Read one pool back: a WORD record count, then the compressed
// records, each copied to the entry its index names. The cursor is set before
// the memset, and decompress's result is SMemFree's flags argument.
BOOL CThingy::LoadThingyList(void *pFile, CThingy *pPool, int nCount) {
  // Hyperion: the count and each slot in 4 bytes for a pool past 65,535
  const unsigned nSlotBytes = PoolSaveBytes(nCount);
  DWORD wCount = 0;
  if (!pfread(&wCount, nSlotBytes, pFile))
    return FALSE;
  memset(pPool, 0, static_cast<size_t>(nCount) * sizeof(CThingy));
  if (wCount != 0) {
    // Hyperion: a record is the index and the thingy in 1.17's layout,
    // thingy_saveentry_t on x86.
    DWORD nSize = wCount * (nSlotBytes + PortSave117Size(SAVE117_CThingy));
    BYTE *pBuf = static_cast<BYTE *>(SMemAlloc(nSize, "CThingy.cpp", 0x3DF, 0));
    const BYTE *pRec = pBuf;
    memset(pBuf, 0, nSize);
    BOOL bOk = decompress(pBuf, nSize, pFile);
    if (!bOk) {
      SMemFree(pBuf, "CThingy.cpp", 0x3E5, bOk);
      return FALSE;
    }
    for (DWORD i = 0; i < wCount; i++) {
      const unsigned wSlot = PoolSaveSlot(pRec, nSlotBytes);
      // Hyperion: a slot past the game's pool is a broken save
      if (wSlot >= static_cast<unsigned>(nCount)) {
        SMemFree(pBuf, "CThingy.cpp", 0, 0);
        return FALSE;
      }
      CThingy *pDest = &pPool[wSlot];
      pRec = PortSave117Get(SAVE117_CThingy, pRec + nSlotBytes, pDest);
      pDest->BitSwap();
    }
    SMemFree(pBuf, "CThingy.cpp", 0x3F7, 0);
  }
  return TRUE;
}

// CThingy::SaveThingies -- oracle 0x4189D0 (61 B). Encode, then both Save calls
// unconditionally, with no short-circuit, then the codec. The `b ? a : 0` select is a
// statement of its own before the codec call, so MSVC computes it arithmetically
// (neg/sbb/and) into ESI and carries it across; as the return expression it becomes a
// post-call cmov instead.
BOOL CThingy::SaveThingies(void *pFile) {
  void *pF = pFile;
  EncodeListForSave();
  BOOL a = SaveThingyList(pF, sgThingies1Arr_g, THINGY_POOL_SIZE);
  BOOL b = SaveThingyList(pF, sgThingies2Arr_g, THINGY_POOL_SIZE);
  BOOL nRet = b ? a : 0;
  DecodeListFromSave();
  return nRet;
}

// CThingy::sub_417C10 -- oracle 0x417C10 (259 B). Recompute the fog mask from
// the sprite's GRP footprint in tiles, clamped to the map. The three sprite
// fields go into int locals first (signed `/ 2`, unsigned `/ 32u` for the
// sizes), each position is folded in its own statement, and each size follows
// its position; with the sizes first the frame gains a spill (260 B).
BOOL CThingy::sub_417C10() {
  if (this->pSprite->flags.b & 0x20)
    return FALSE;
  int nPosX = PosS(this->pSprite->wPosX);
  int nGrpW = SpriteGrpWidth(this->pSprite);  // Hyperion: past 255
  int nGrpH = SpriteGrpHeight(this->pSprite);
  int nX = (nPosX - nGrpW / 2) / 32;
  int nW = (nGrpW + 31) / 32u;
  int nY = (PosS(this->pSprite->wPosY) - nGrpH / 2) / 32;
  int nH = (nGrpH + 31) / 32u;
  if (nX < 0) {
    nW += nX;
    nX = 0;
  } else if (nX + nW > gwMapWidth) {
    nW = gwMapWidth - nX;
  }
  if (nY < 0) {
    nH += nY;
    nY = 0;
  } else if (nY + nH > gwMapHeight) {
    nH = gwMapHeight - nY;
  }
  BYTE bNew;
  if (gPlayback != 0 && sgbShowAll_g != 0)
    bNew = 0xFF;
  else
    bNew = (BYTE)sub_418D00(nX, nY, nW, nH);
  if (g_hxPlayers)  // Hyperion: the slots past 7
    VisionHxSpriteSet(this->pSprite, bNew == 0xFF && gPlayback != 0 && sgbShowAll_g != 0
                                         ? PlayerSet::All() : VisionHxRect(nX, nY, nW, nH, true));
  BYTE bOld = this->pSprite->bVisibilityFlags;
  if (bOld == bNew)
    return FALSE;
  bOld &= (BYTE)gdwLocalMask;
  this->pSprite->SetMask(bNew);
  if (bOld != 0) {
    if (!(this->pSprite->bVisibilityFlags & (BYTE)gdwLocalMask))
      return TRUE;
  }
  return FALSE;
}

// CThingy::CalcVisibility -- oracle 0x417D20 (300 B). Recompute the fog mask over a
// given width and height; TRUE when the sprite has just left local vision.
BOOL CThingy::CalcVisibility(int nWidth, int nHeight) {
  CSprite *pSprite = this->pSprite;
  if (pSprite == 0 || (pSprite->flags.b & 0x20))
    return FALSE;
  // Hyperion: the clip to the map is VisionTileRect's, which the read-ahead shares
  int nX, nY, nW, nH;
  if (!VisionTileRect(PosS(pSprite->wPosX), PosS(pSprite->wPosY), nWidth, nHeight, gwMapWidth, gwMapHeight, &nX,
                      &nY, &nW, &nH))
    return pSprite->bVisibilityFlags != 0;
  // Hyperion: a unit's reads in the circle walk are made ahead on the worker threads
  // (playervision.h); in a game past 8 slots, the slots past 7 too
  BYTE bNew;
  PlayerSet qHigh;
  if (!VisionHxCircleRead(this, nX, nY, nW, nH, &bNew, &qHigh)) {
    bNew = (BYTE)tiles_seen_mask(nX, nY, nW, nH);
    if (g_hxPlayers)
      qHigh = VisionHxRect(nX, nY, nW, nH, false);
  }
  if (g_hxPlayers)
    VisionHxSpriteSet(pSprite, qHigh);
  BYTE bOld = pSprite->bVisibilityFlags;
  if (bOld == bNew)
    return FALSE;
  bOld &= (BYTE)gdwLocalMask;
  pSprite->SetMask(bNew);
  if (bOld != 0) {
    if (!(this->pSprite->bVisibilityFlags & (BYTE)gdwLocalMask))
      return TRUE;
  }
  return FALSE;
}

// CThingy::UpdateThingy1 -- oracle 0x418B50 (136 B), nullary void. Dispatches
// on the sprite's wSpriteID (doodad-cull band test) to CalcVisibility(w, h) or
// sub_417C10(), then runs the SpriteUpdate logic inline (a real call would be
// a wrong body -- /Ob2 splice watch at merge), and recycles the thingy onto
// the free list when the sprite died.
void CThingy::UpdateThingy1() {
  if (pSprite->wSpriteID > 0x81 &&
      (pSprite->wSpriteID < 0x182 || pSprite->wSpriteID > 0x1E0)) {
    CalcVisibility(SpriteGrpWidth(pSprite), SpriteGrpHeight(pSprite));  // Hyperion: past 255
  } else {
    sub_417C10();
  }
  if (pSprite != 0) {
    pSprite->Update();
    if (pSprite->pImageHead == 0)
      pSprite = 0;
  }
  if (pSprite == 0) {
    reinterpret_cast<CLink *>(this)->Unlink(
        reinterpret_cast<CLink **>(&thingies1_sgpUsedHead),
        reinterpret_cast<CLink **>(&thingies1_sgpUsedTail));
    reinterpret_cast<CLink *>(this)->PrependThis(
        reinterpret_cast<CLink **>(&thingies1_sgpFreeHead),
        reinterpret_cast<CLink **>(&thingies1_sgpFreeTail));
  }
}

// CThingy::UpdateThingy2 -- oracle 0x418C20 (218 B), nullary void. Flashes the
// fogged sprite, then fog-tests the unit's placebox footprint with
// sub_418520(FALSE); on fully-hidden it removes the selection circle, kills
// the sprite and recycles the thingy onto pool-2's free list.
// sub_418520 is inlined and its query pointer folds to a direct call to
// sub_418EB0 that MSVC treats as able to write memory, as the oracle does.
void CThingy::UpdateThingy2() {
  pSprite->UpdateFlash(get_selection_circle_color(pSprite->bCreator));
  if (sub_418520(FALSE) == 0) {
    pSprite->RemoveCircle();
    pSprite->Kill();
    pSprite = 0;
    reinterpret_cast<CLink *>(this)->Unlink(
        reinterpret_cast<CLink **>(&thingies2_sgpUsedHead),
        reinterpret_cast<CLink **>(&thingies2_sgpUsedTail));
    reinterpret_cast<CLink *>(this)->PrependThis(
        reinterpret_cast<CLink **>(&thingies2_sgpFreeHead),
        reinterpret_cast<CLink **>(&thingies2_sgpFreeTail));
  }
}

// sub_418D00 -- oracle 0x418D00 (116 B), ret 8h. OR-accumulate ~tile over the
// tile rectangle, rows top-down / columns right-to-left. Note: the oracle
// reads BYTE 1 of each fog dword (its `inc ecx` after the scaled lea), not
// byte 0 as UpdateVisibility does -- an oracle quirk, matched here on purpose.
int __fastcall sub_418D00(short x, short y, short w, short h) {
  int nMask = 0;
  for (int r = y - 1 + h; r >= y; r--) {
    for (int c = x + w - 1; c >= x; c--) {
      nMask |= ~((BYTE *)&gpSquareMap[r * gwMapWidth + c])[1];
    }
  }
  return nMask;
}

// tiles_seen_mask -- oracle 0x418D80 (115 B). The same scan reading BYTE 0.
int __fastcall tiles_seen_mask(short x, short y, short w, short h) {
  int nMask = 0;
  for (int r = y - 1 + h; r >= y; r--) {
    for (int c = x + w - 1; c >= x; c--) {
      nMask |= ~(BYTE)gpSquareMap[r * gwMapWidth + c];
    }
  }
  return nMask;
}

// sub_418E00 -- oracle 0x418E00 (170 B), ret 8h. TRUE on the first tile of the
// rectangle that is fogged/black; FALSE when none is.
BOOL __fastcall sub_418E00(short x, short y, short w, short h) {
  for (int r = y - 1 + h; r >= y; r--) {
    for (int c = x + w - 1; c >= x; c--) {
      if (gPlayback != 0 ? (((((BYTE)gdwReplayVisionMask << 8) &
                              ~gpSquareMap[r * gwMapWidth + c]) == 0) &&
                            sgbShowAll_g == 0 &&
                            !ViewHxTileExplored(r * gwMapWidth + c))  // Hyperion: the viewer's slots past 7 (stage D)
                         : (gpSquareMap[r * gwMapWidth + c] & gdwLocalBlackMask))
        return TRUE;
    }
  }
  return FALSE;
}

// sub_418EB0 -- oracle 0x418EB0 (161 B), ret 8h. FALSE on the first tile not
// visible, TRUE when the whole rectangle is visible.
BOOL __fastcall sub_418EB0(short x, short y, short w, short h) {
  for (int r = y - 1 + h; r >= y; r--) {
    for (int c = x + w - 1; c >= x; c--) {
      if (gPlayback != 0 ? ((((BYTE)gdwReplayVisionMask &
                              ~gpSquareMap[r * gwMapWidth + c]) == 0) &&
                            sgbShowAll_g == 0 &&
                            !ViewHxTileSeen(r * gwMapWidth + c))  // Hyperion: the viewer's slots past 7 (stage D)
                         : (gpSquareMap[r * gwMapWidth + c] & gdwLocalMask))
        continue;
      return FALSE;
    }
  }
  return TRUE;
}

// CThingy::sub_418520 -- oracle 0x418520 (147 B), ret 4h. The placebox tile
// rect from gUnitPlaceboxSize[wUnitType] and the sprite position; picks
// sub_418E00 (bFlag) / sub_418EB0 through a cmov-selected fn pointer.
BOOL CThingy::sub_418520(int bFlag) {
  CSprite *pSprite = this->pSprite;
  upos_t wPosY = pSprite->wPosY;
  upos_t wPosX = pSprite->wPosX;
  int nType = this->wUnitType;
  BOOL (__fastcall *pfnQuery)(short, short, short, short) =
      bFlag ? sub_418E00 : sub_418EB0;
  return pfnQuery((PosS(wPosX) - gUnitPlaceboxSize[nType].x / 2) / 32,
                  (PosS(wPosY) - gUnitPlaceboxSize[nType].y / 2) / 32,
                  (gUnitPlaceboxSize[nType].x + 31) / 32,
                  (gUnitPlaceboxSize[nType].y + 31) / 32);
}
// AllocBlackX -- oracle 0x417AD0 (55 B). `this`-less: ECX = nX, EDX = nY,
// bare `ret`. The black selection X is dropped at (x+1, y+1) -- the
// `lea eax,[edx+1h]` (nY+1) is pushed first, i.e. it is SetPosition's second
// argument. gBlackX is re-read per statement (`mov ecx,[5C9984h]` twice).
void __fastcall AllocBlackX(int nX, int nY) {
  gbBlackXActive = 0;
  gBlackX->pSprite->SetPosition(nX + 1, nY + 1);
  gBlackX->pSprite->SetAnimation(2, 1);
  gbBlackXActive = 1;
}

// Hide the black X: clear the gate DrawBlackX reads. CImage::RunScript calls it
// from one iscript opcode.
void sub_417EF0() {
  gbBlackXActive = 0;
}

// IsUnitRClickedCb -- oracle 0x417F00 (97 B). The find_first_unit_near filter for the
// right-click target search: the candidate must be of the requested unit type
// and its placebox must contain the click. The X sum goes through a named
// local so the add lands in the position's register, as the oracle has it.
BOOL __fastcall IsUnitRClickedCb(CUnit *pUnit, WORD wType) {
  point16_t size = gUnitPlaceboxSize[pUnit->wType];
  if (pUnit->wType == wType) {
    int nX = PosS(pUnit->pSprite->wPosX);
    nX += size.x / 2;
    if ((unsigned)(nX - gnRClickX) < (unsigned)size.x) {
      if ((unsigned)(PosS(pUnit->pSprite->wPosY) + size.y / 2 - gnRClickY) < (unsigned)size.y)
        return TRUE;
    }
  }
  return FALSE;
}

// GetRClickTarget -- oracle 0x418210 (40 B). ECX = nX, EDX = nY (stored as
// WORDs to 0x5C9980/82), [ebp+8h] = the unit type, read `mov eax,[ebp+8h] /
// movzx eax,ax` then pushed -- an INT parameter narrowed at the push. ret 4h.
// The filter is passed where CBullet.h's pfn_bounce_filter_t (one-argument)
// is declared; ours takes the type as a second EDX argument, hence the cast.
CUnit *__fastcall GetRClickTarget(int nX, int nY, int nType) {
  gnRClickX = nX;
  gnRClickY = nY;
  return find_first_unit_near(nX, nY, 0x60, (pfn_bounce_filter_t)IsUnitRClickedCb,
                    (WORD)nType);
}

// sub_417F70 -- oracle 0x417F70 (137 B). Walk the fogged-pool used list for
// the first thingy whose sprite footprint contains (nX, nY): unselectable
// types are skipped, the placebox half-extents position the footprint, and
// the GRP extents (bGrpWidth/bGrpHeight) bound it. The x test exits with
// `jnb` -> next, the y test hits with `jb` -> return.
CThingy *__fastcall sub_417F70(int nX, int nY) {
  for (CThingy *p = thingies2_sgpUsedHead; p != 0; p = p->pNext) {
    WORD wType = p->wUnitType;
    if (usel_type_unselectable(wType))
      continue;
    int nDx = gUnitPlaceboxSize[wType].x / 2 +
              PosS(p->pSprite->wPosX) - nX;
    if ((unsigned)nDx >= (unsigned)SpriteGrpWidth(p->pSprite))  // Hyperion: past 255
      continue;
    int nDy = gUnitPlaceboxSize[wType].y / 2 +
              PosS(p->pSprite->wPosY) - nY;
    if ((unsigned)nDy < (unsigned)SpriteGrpHeight(p->pSprite))
      return p;
  }
  return 0;
}

// sub_418000 -- oracle 0x418000 (147 B), `ret 8h`. Same walk, filtered by sprite id
// first, with the footprint coming from the point16_t argument rather than the placebox
// table. Both halves of that argument are read `movsx ..., ax`, so they are signed.
CThingy *__fastcall sub_418000(int nSpriteID, int nX, int nY, point16_t size) {
  for (CThingy *p = thingies2_sgpUsedHead; p != 0; p = p->pNext) {
    if (p->pSprite->wSpriteID != nSpriteID)
      continue;
    WORD wType = p->wUnitType;
    if (usel_type_unselectable(wType))
      continue;
    int nDx = PosS(p->pSprite->wPosX) + (short)size.x / 2 - nX;
    if ((unsigned)nDx >= (unsigned)(short)size.x)
      continue;
    int nDy = PosS(p->pSprite->wPosY) + (short)size.y / 2 - nY;
    if ((unsigned)nDy < (unsigned)(short)size.y)
      return p;
  }
  return 0;
}

// sub_4180A0 -- oracle 0x4180A0 (248 B), `ret 4h`. The fogged-pool walk for
// the first mineral field or geyser whose placebox, in tiles and widened by
// nRadius, contains the tile (nX, nY). The placebox is read through a byte
// offset: the oracle spills `wType * 4` and reads `[eax+522728h]`, where a
// point16_t index spills the raw index and folds the scale into SIB.
CThingy *__fastcall sub_4180A0(int nX, int nY, int nRadius) {
  for (CThingy *p = thingies2_sgpUsedHead; p != 0; p = p->pNext) {
    CSprite *pSprite = p->pSprite;
    WORD wType;
    switch (pSprite->wSpriteID) {
      case 0x117:
      case 0x118:
      case 0x119:
        wType = (WORD)(pSprite->wSpriteID - 0x67);
        break;
      case 0xB2:
      case 0xCC:
      case 0x107:
      case 0x113:
        wType = 0xBC;
        break;
      default:
        continue;
    }
    int nOff = wType * 4;
    int nDx = nX - (PosS(pSprite->wPosX) - *(const short *)((const BYTE *)gUnitPlaceboxSize + nOff) / 2) / 32 + nRadius;
    if ((unsigned)nDx >= (unsigned)(*(const short *)((const BYTE *)gUnitPlaceboxSize + nOff) / 32 + nRadius * 2))
      continue;
    int nDy = nY - (PosS(pSprite->wPosY) - *(const short *)((const BYTE *)gUnitPlaceboxSize + nOff + 2) / 2) / 32 + nRadius;
    if ((unsigned)nDy < (unsigned)(*(const short *)((const BYTE *)gUnitPlaceboxSize + nOff + 2) / 32 + nRadius * 2))
      return p;
  }
  return 0;
}

// Clears both thingy pools, chains every node onto its pool's free list, then
// allocates the black-X thingy and hides it.
// The allocation is AllocThingy inlined; its early return is what sends the
// empty-pool path through the shared null. SetMask's `push 0` lands above the
// two global stores only while gBlackX and gbBlackXActive are file-static.
void CThingy::InitThingies() {
  memset(sgThingies1Arr_g, 0, THINGY_POOL_SIZE * sizeof(CThingy));
  memset(sgThingies2Arr_g, 0, THINGY_POOL_SIZE * sizeof(CThingy));
  thingies1_sgpFreeHead = thingies1_sgpFreeTail = sgThingies1Arr_g;
  thingies1_sgpUsedHead = thingies1_sgpUsedTail = 0;
  thingies2_sgpFreeHead = thingies2_sgpFreeTail = sgThingies2Arr_g;
  thingies2_sgpUsedHead = thingies2_sgpUsedTail = 0;
  CThingy *pNode = &sgThingies1Arr_g[1];
  int n;
  for (n = THINGY_POOL_SIZE - 1; n != 0; n--) {
    reinterpret_cast<CLink *>(thingies1_sgpFreeHead)->AppendLink(
        reinterpret_cast<CLink *>(pNode),
        reinterpret_cast<CLink **>(&thingies1_sgpFreeTail));
    pNode++;
  }
  pNode = &sgThingies2Arr_g[1];
  for (n = THINGY_POOL_SIZE - 1; n != 0; n--) {
    reinterpret_cast<CLink *>(thingies2_sgpFreeHead)->AppendLink(
        reinterpret_cast<CLink *>(pNode),
        reinterpret_cast<CLink **>(&thingies2_sgpFreeTail));
    pNode++;
  }
  // A failed allocation leaves pBlackX null and the next line faults, as in
  // the oracle.
  CThingy *pBlackX = AllocThingy(0x13E, 0, 0, 0);
  CSprite *pSprite = pBlackX->pSprite;
  gBlackX = pBlackX;
  gbBlackXActive = 0;
  pSprite->flags.b |= 0x20;
  pSprite->SetMask(0);
}

// Writes one thingy pool: the used count, then each used entry's index and
// swapped record, compressed. The records go through a BYTE cursor advanced in
// two steps; a record-typed cursor permutes the callee-saved registers.
BOOL CThingy::SaveThingyList(void *pFile, CThingy *pPool, int nCount) {
  DWORD nUsed = 0;
  for (int i = 0; i < nCount; i++)
    if (pPool[i].pSprite != 0)
      nUsed++;
  // Hyperion: the count and each slot in 4 bytes for a pool past 65,535
  const unsigned nSlotBytes = PoolSaveBytes(nCount);
  BOOL bRet = pfwrite(&nUsed, nSlotBytes, pFile);
  if (bRet && nUsed != 0) {
    DWORD nSize = nUsed * (nSlotBytes + PortSave117Size(SAVE117_CThingy));  // Hyperion
    BYTE *pBuf = (BYTE *)SMemAlloc(nSize, "CThingy.cpp", 0x3A8, 0);
    BYTE *p = pBuf;
    memset(pBuf, 0, nSize);
    nUsed = 0;
    for (int i = 0; i < nCount; i++, pPool++) {
      if (pPool->pSprite != 0) {
        PoolSaveSlotPut(p, static_cast<unsigned>(i), nSlotBytes);
        p += nSlotBytes;
        pPool->BitSwap();
        p = PortSave117Put(SAVE117_CThingy, pPool, p);
        pPool->BitSwap();
        nUsed++;
      }
    }
    bRet = PortSaveChunk(pBuf, nSize, nUsed, pFile);
    SMemFree(pBuf, "CThingy.cpp", 0x3BE, 0);
  }
  return bRet;
}
