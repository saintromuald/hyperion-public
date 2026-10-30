// CUnitZerg.cpp -- zerg larva, burrow and creep. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller here by /Ob2, while the
// oracle, built with WPO off, cannot splice across a TU boundary: a callee left in another
// TU reproduces that for free, and __declspec(noinline) is the lever when one must live
// here.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/platform.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/CFlingy.h"
#include "../h/rect16.h"
#include "../h/point16.h"
// collide.h declares mtx_query_place_bldg with the body's int nType; this TU
// calls it through the WORD view below, so the header's view is renamed away.
#define mtx_query_place_bldg mtx_query_place_bldg_int_view
#include "../h/collide.h"       // ours: mtx_has_flags_px
#include "../h/count.h"        // ours: count_add_existence
#include "../h/creep.h"         // ours: spread_creep
#include "../h/CUnitBuild.h"    // ours: ClipToMap
#include "../h/CUnitTSpells.h"   // ours: gamesnd_play
#include "../h/CTarget.h"         // ours: CTarget::XYInRadius
#include "../h/gamesnd.h"         // ours: gamesnd_land_error
#include "../h/CUnitZSpells.h"   // ours: FindPlaceForUnit
#include "../h/TrigAct.h"         // ours: CreateUnitAt, net_rand
#include "../h/placebox.h"        // ours: sub_43D360, town_move_morphed_building
#include "../h/CUnitZerg.h"

// The unit search around a point, typed here for the larva filter it is
// handed. C linkage names one symbol for every TU's spelling.
// Hyperion: the definition (find.cpp) reads the position as ints, and this
// TU's callers pass it as 16 bits. The view takes the definition's types, which
// the x64 build needs, and the macro narrows the arguments as 1.17's did
//.
extern "C" CUnit *__fastcall find_first_unit_near(int x, int y, int nRadius, int (__fastcall *pfnFilter)(void *, intptr_t), intptr_t nContext);
#define find_first_unit_near(a0, a1, a2, a3, a4) (find_first_unit_near)(PosU(a0), PosU(a1), (a2), (a3), (a4))
#undef mtx_query_place_bldg
#include "../gen/globals117.h"
#include "../h/typelimits.h"  // Hyperion: a mod's types
#include "../h/handicap.h"  // Hyperion: a player's handicap

// The unit type goes out unconverted: DispatchDroneLand pushes the WORD
// register and indexes gUnitPlaceboxSize through a separate copy.
// Hyperion: the definition (collide.cpp) reads the cell and the type as
// ints, and this TU's callers pass them as 16 bits. The view takes the
// definition's types, which the x64 build needs, and the macro narrows the
// arguments as 1.17's did.
extern "C" int __fastcall mtx_query_place_bldg(CUnit *pUnit, BYTE bPlayer, int sX, int sY, int nType, int nSlot, int nArg6, int nArg7, int nArg8);
#define mtx_query_place_bldg(a0, a1, a2, a3, a4, a5, a6, a7, a8) (mtx_query_place_bldg)((a0), (a1), (short)(a2), (short)(a3), (WORD)(a4), (a5), (a6), (a7), (a8))

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one over the whole
// block, since the generated set grows over time and a single guard would go false and
// undeclare every name under it.

// gUnitBoundBox @0x522C38 is sheet-named (data_naming_1.17.csv, arr/units.cpp)
// and unregistered; collide.cpp, CUnitBuild.cpp and CUnitPath.cpp spell it the
// same way. 228 entries of rect16_t, the stride-8 indexing the oracle uses.
#ifdef OC_DLL_MODE
#ifndef gUnitBoundBox
#define gUnitBoundBox (reinterpret_cast<rect16_t *>(OC_VA(0x522C38u)))
#endif
#else
extern rect16_t gUnitBoundBox[228] OC_UNALIGNED;        // 0x522C38
#endif


// CUnit +0xCC, the creep-spread cooldown, reached by offset because CUnit.h
// still spells it `_unmapped_CC[0x01]`. The array-element spelling blocks MSVC
// from hoisting the pSprite load above the store; a scalar lvalue does not.
#define UNIT_CREEP_TIMER(u)   (*(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_CC)))

// CUnit +0xD0/+0xD2/+0xD4/+0xD6, four signed WORD stray counters a hatchery
// keeps per side. CUnit.h spells the range as two pointer unions, so they are
// reached as offsets from their starts. Hyperion: the second pair from
// pUnionD4's, not pUnionD0's + 4, which at 64 bits is the high half of that
// slot and was lost from a save.
#define UNIT_STRAY_LEFT(u)   (*reinterpret_cast<short *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, pUnionD0) + 0))
#define UNIT_STRAY_TOP(u)    (*reinterpret_cast<short *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, pUnionD0) + 2))
#define UNIT_STRAY_RIGHT(u)  (*reinterpret_cast<short *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, pUnionD4) + 0))
#define UNIT_STRAY_BOTTOM(u) (*reinterpret_cast<short *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, pUnionD4) + 2))

// CUnit +0x14, a CUnit * inside CUnit.h's `_unmapped_14[0x04]`: DispatchIdleLarva
// loads it and dereferences +0x0C off it exactly as it does off `this`.
#define UNIT_CUNIT_14(u) (*reinterpret_cast<CUnit **>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_14)))

// CUnit +0x3C, a DWORD inside CUnit.h's `_unmapped_38[0x10]`.
#define UNIT_DWORD_3C(u) (*reinterpret_cast<DWORD *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_38) + 4))

// CUnit +0xCA, the larva-spawn cooldown, inside CUnit.h's `_unmapped_CA[0x02]`.
#define UNIT_LARVA_TIMER(u) (*(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_CA)))

// Free callees in other TUs that no header here declares, spelled exactly as
// src117/lang/{CImage,dlg_draw,init}.cpp spell them: two .cpp-local declarations that
// disagree send the name's resolution elsewhere.

// appcritf -- oracle 0x4CC190, __cdecl varargs; the call site pushes one
// argument and retires it with `add esp, 4h`.
void __cdecl appcritf(const char *pszFmt, ...);

// Sheet-named .data tables this TU reaches that the registry does not carry.
// guwInfestedType is indexed by wType - 106, the same base-106 convention
// placebox.cpp uses for gUnitAddOnOffset.
#ifdef OC_DLL_MODE
#ifndef gubUnitPriorityPlane
#define gubUnitPriorityPlane (reinterpret_cast<BYTE *>(OC_VA(0x520A98u)))
#endif
#ifndef guwInfestedType
#define guwInfestedType (reinterpret_cast<WORD *>(OC_VA(0x51FF20u)))
#endif
#ifndef guiUnitBuildImage
#define guiUnitBuildImage (reinterpret_cast<DWORD *>(OC_VA(0x51FFE0u)))
#endif
#else
extern BYTE  gubUnitPriorityPlane[228] OC_UNALIGNED; // 0x520A98
extern WORD  guwInfestedType[96];       // 0x51FF20
extern DWORD guiUnitBuildImage[228] OC_UNALIGNED;    // 0x51FFE0
#endif

// 0x665D54, the larva count the hatchery search tallies. Only this TU reaches
// it, and internal linkage lets the oracle's schedule store it after the
// search's argument loads.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define gLarvaNearCount (*reinterpret_cast<BYTE *>(OC_VA(0x665D54u)))
#else
static BYTE gLarvaNearCount;
#endif

// The bodies, in oracle VA order.

// A pixel position that lies on creep. The int parameters are what widen the
// callers' WORD arguments with movzx before the short-typed call.
static BOOL IsCreepPx(int x, int y)
{
  return mtx_has_flags_px(x, y, 0x400000, 0);
}

static void GetUnitBounds(CUnit *pUnit, mrect_t *pOut)  // Hyperion: map positions
{
  CSprite *pSprite = pUnit->pSprite;
  spos_t    nY      = pSprite->wPosY;
  spos_t    nX      = pSprite->wPosX;
  int      nType   = pUnit->wType;

  pOut->top    = nY - gUnitBoundBox[nType].top;
  pOut->bottom = nY + gUnitBoundBox[nType].bottom;
  pOut->left   = nX - gUnitBoundBox[nType].left;
  pOut->right  = nX + gUnitBoundBox[nType].right;
}

// True when this unit may use pOther as a nydus entrance: both ends built, same
// player, this one not a building, and the race check last.
BOOL CUnit::CanTravelThroughNydus(void *pArg0)
{
  CUnit *pOther = static_cast<CUnit *>(pArg0);

  return (pOther->dwFlags & 1)
      && pOther->pUnionD0 != 0
      && (pOther->pUnionD0->dwFlags & 1)
      && this->bPlayer == pOther->bPlayer
      && !(this->dwFlags & 4)
      && this->GetRace() == 0;
}

// True while the larva is still inside its parent's footprint on the axis its
// order state names. Case arms are in oracle jump-table order.
int CUnit::ChkLarvaStray(int nArg0, int nArg1)
{
  CUnit *pParent = this->pUnion80;

  if (pParent == 0)
    return 1;

  mrect_t  bounds;

  GetUnitBounds(pParent, &bounds);

  switch (this->bOrderState)
  {
    case 0:  if ((DWORD)nArg0 > PosU(bounds.left))   return 0; break;
    case 1:  if ((DWORD)nArg1 > PosU(bounds.top))    return 0; break;
    case 2:  if ((DWORD)nArg0 < PosU(bounds.right))  return 0; break;
    case 3:  if ((DWORD)nArg1 < PosU(bounds.bottom)) return 0; break;
    default: appcritf("ChkLarvaStray: 1"); break;
  }

  return 1;
}

// True when the tile pair (base, base + delta) is buildable creep: both ends
// survive ClipToMap unchanged and carry the matrix flags. The two identical arm
// pairs merge after allocation; their cur.y tests rank cur.y above nArg1, and the
// first keeps cur.x's add apart from its store so cur.x and nArg2 share EBX.
int sub_43C490(int *pPos, int nArg1, int nArg2)
{
  point16_t pt;
  point16_t cur;

  pt.x = (short)pPos[0];
  pt.y = (short)pPos[1];
  ClipToMap(&pt, 0x23);
  cur.x = pt.x;
  if (cur.x != pPos[0] || (cur.y = pt.y) != pPos[1])
    return 0;
  if (!IsCreepPx((WORD)pPos[0], (WORD)pPos[1]))
    return 0;
  cur.x += nArg1;
  if (cur.y)
    pt.x = cur.x;
  else
    pt.x = cur.x;
  cur.y += nArg2;
  pt.y = cur.y;
  ClipToMap(&pt, 0x23);

  if (pt.x != pPos[0] + nArg1 || pt.y != pPos[1] + nArg2)
    return 0;

  if (cur.y)
    return mtx_has_flags_px(pt.x, pt.y, 0x410000, 0x8000000) != 0;
  return mtx_has_flags_px(pt.x, pt.y, 0x410000, 0x8000000) != 0;
}

// Ends a burrow-family order: the lurker arm also raises the 0x1000 flag.
void CUnit::Deburrow()
{
  if (this->bOrderId == 0x74 || this->bOrderId == 0x75 || this->wType == 0x67)
  {
    if (this->wType == 0x67)
      this->dwFlags |= 0x1000;

    this->SetOrder(0x76);
    this->EndUninterruptableDispatchOrder();
  }
}

// Turns a captured command center into its infested type: transfers the unit
// counts to the infesting player, swaps wType and re-skins the sprite.
void CUnit::DispatchDroneInfesting()
{
  CUnit *pDrone = this->pOrderTarget;

  if (pDrone != 0 && this->IsInfestable())
  {
    if (this->bOrderState == 0)
    {
      this->bOrderState = 2;
      this->wRemainingBuildTime = 3;
      return;
    }

    if (this->wRemainingBuildTime-- != 0)
      return;

    if (this->pUnionC0 != 0)
      this->DetachAddon();

    if (this->IsRallyer())
      this->pRallyUnit = this;

    WORD wNewType = guwInfestedType[this->wType - 106];

    count_add_killed(this, pDrone->bPlayer);
    count_remove_existence(this);
    if (this->dwFlags & 1)
      count_remove_completion(this, 0);

    this->wType = wNewType;
    count_add_existence(this);
    if (this->dwFlags & 1)
      count_add_completion(this, 0);

    this->SetPlayer(pDrone->bPlayer, 1);
    this->SetOwnerColor(pDrone->bPlayer);
    this->ReplaceBackgroundOrder(0x17);
    this->RemoveAllBuildQueue();
    this->RemoveAllOrders();

    if (this->dwFlags & 2)
      this->pSprite->SetAnimation(0x14, 1);

    WORD wImage = (WORD)guiUnitBuildImage[wNewType];
    if (ImageTypeValid(wImage))  // Hyperion: < 999, and a mod's images
      this->pSprite->AllocImage(wImage, 0, 0, 0);

    this->SetLife(HandicapLife(gxUnitLife[wNewType], this->bPlayer));  // Hyperion: at the owner's handicap
    this->SetButtonset(wNewType);
    status_set_update();
  }

  this->EndUninterruptableDispatchOrder();
}

// Burrows the unit: stop, play the burrow animation facing the spawn
// direction, then swap in the burrowed sprite and hand the order off.
void CUnit::DispatchBurrow()
{
  switch (this->bOrderState)
  {
    case 0:
      this->Stop();
      this->bOrderState = 1;
      // fall through
    case 1:
    {
      if (this->bMoveFlags & 2)
      {
        if (UNIT_DWORD_3C(this) != 0)
          return;
        this->MakeImmovable();
        return;
      }

      this->pSprite->SetAnimation(0x0C, 1);

      BYTE bDir = 0x0C;
      if (this->wType == 0x2E || this->wType == 0x34)
        bDir = 0x0B;
      if (this->wType == 0x29)
        bDir = 0x0D;

      CFlingy::spawn_dir_t sd;
      sd.bDir = bDir;
      reinterpret_cast<CFlingy *>(this)->SetSpawnFacing(sd);
      this->bOrderState = 2;
    }
      // fall through
    case 2:
      if (!this->IsFacingMoveTarget())
        return;

      this->pSprite->SetAnimation(0x19, 1);
      this->bOrderState = 3;
      gamesnd_play(0x10, this, 1, 0);
      // fall through
    case 3:
      if (this->bOrderFlags & 4)
      {
        this->bOrderFlags &= ~4u;
        this->pSprite->HideCloaked();
        this->pSprite->HidePowerup();
        this->dwFlags |= 0x200000;
        this->ReplaceWithBurrowedSprite();
        this->SetBackgroundOrder(0x6D);
        this->dwUnionE4 = 0x80000000;
        this->HideSpells();
        status_set_update();
        this->SetOrder(0x75);
        this->EndUninterruptableDispatchOrder();
      }
      return;
  }
}

// A landing drone: state 0 walks to the site, state 1 checks the ground is
// still buildable and state 2 finishes the landing. gUnitPlaceboxSize halves
// are subtracted before the /32 tile conversion.
// The placebox index is a copy of the pushed type only through the WORD view
// of mtx_query_place_bldg above; the int view shares one register for both.
void CUnit::DispatchDroneLand()
{
  if (this->dwUnion134 != 0)
    this->dwFlags |= 0x800000;

  switch (this->bOrderState)
  {
    case 0:
      if (this->SetDestination_xy(this->wOrderTargetX, this->wOrderTargetY - 7))
        this->bOrderState = 1;
      return;

    case 1:
    {
      CSprite *pSprite = this->pSprite;

      if (this->wMoveTargetX != pSprite->wPosX
          || this->wMoveTargetY != PosU(pSprite->wPosY))
        return;

      int nMode = (this->dwFlags & 0x80000) ? 2 : 1;

      if (nMode == 0)
        return;

      WORD wType   = this->wBuildQueue[this->bBuildQueueSlot];
      BYTE bPlayer = this->bPlayer;
      WORD wSizeY  = (WORD)gUnitPlaceboxSize[wType].y;
      WORD wSizeX  = (WORD)gUnitPlaceboxSize[wType].x;
      int  nPlace  = mtx_query_place_bldg(
                       this, bPlayer,
                       PosS((this->wOrderTargetX - (short)wSizeX / 2) / 32),
                       PosS((this->wOrderTargetY - (short)wSizeY / 2) / 32),
                       wType, 0, 0, 1, 1);

      if (wType == 0x95 && nMode == 1 && nPlace == 0)
      {
        CUnit *pGas = sub_4132B0(0xBC, this->wOrderTargetX,
                                 this->wOrderTargetY);

        if (pGas != 0 && this->IsWithinTargetRange(0x20, pGas))
        {
          this->dwFlags &= ~0x100000;
          this->pSprite->bElevationLevel =
            (BYTE)(gubUnitPriorityPlane[this->wType] + 1);
          this->SetOrder_interrupt(0x96, 0);
          this->SetOrder_interrupt(gubUnitOrderGuard[this->wType], 0);

          if (this->SetDestination_xy(this->wOrderTargetX,
                                      this->wOrderTargetY))
          {
            this->dwFlags |= 0x1000;
            this->bOrderState = 2;
          }
          return;
        }
      }
      else if (nPlace != 0 || nMode == 2)
      {
        goto landError;
      }

      {
        int nY = this->wOrderTargetY;
        int nX = this->wOrderTargetX;
        if (!reinterpret_cast<CTarget *>(this)->XYInRadius(0x80, nX, nY))
          goto landError;
      }

      if (!this->SetDestination_xy(this->wOrderTargetX, this->wOrderTargetY)
          && this->IsInterruptable())
        return;

      this->pSprite->sub_416970();
      this->dwFlags |= 0x1000;
      this->bOrderState = 2;
      return;

landError:
      gamesnd_land_error(this);

      if (this->pOrderQueueHead != 0 && this->GetFirstQueuedOrder() == 0x1A)
        this->RemovePreviousOrder();

      this->SetOrder(gubUnitOrderGuard[this->wType]);
      return;
    }

    case 2:
    {
      CSprite *pSprite = this->pSprite;

      if (this->wMoveTargetX != pSprite->wPosX
          || this->wMoveTargetY != PosU(pSprite->wPosY))
        return;

      int nMode = (this->dwFlags & 0x80000) ? 2 : 1;

      if (nMode == 0)
        return;

      pSprite->SetShadowOffset(0);
      this->pSprite->sub_417840();

      if (nMode == 2)
      {
        gamesnd_land_error(this);

        if (this->pOrderQueueHead != 0 && this->GetFirstQueuedOrder() == 0x1A)
          this->RemovePreviousOrder();

        this->pSprite->SetShadowOffset(7);
        this->pSprite->InvalidateDrawInfo();
        this->SetNextOrder(0x96);
        this->EndUninterruptableDispatchOrder();
        return;
      }

      if (this->pOrderQueueHead != 0 && this->GetFirstQueuedOrder() == 0x1A)
      {
        this->SetUnitPosition(this->wOrderTargetX, this->wOrderTargetY);
        status_set_update();
        this->EndUninterruptableDispatchOrder();
        return;
      }

      this->pSprite->SetShadowOffset(7);
      this->pSprite->InvalidateDrawInfo();
      this->SetNextOrder(0x96);
      status_set_update();
      this->EndUninterruptableDispatchOrder();
      return;
    }
  }
}

// Walks a unit into a nydus canal: move to the entrance, then hand off to the
// exit placement. The nydus validity test is CanTravelThroughNydus, which the
// oracle inlines here.
void CUnit::DispatchEnterNydus()
{
  CUnit *pTarget = this->pOrderTarget;

  if (pTarget == 0 || !this->CanTravelThroughNydus(pTarget))
  {
    this->EndDispatchOrder();
    return;
  }

  switch (this->bOrderState)
  {
    case 0:
      if (this->SetDestination_CUnit(pTarget))
        this->bOrderState = 1;
      return;

    case 1:
      switch (this->AtMoveTarget())
      {
        case 0:
          return;
        case 1:
          break;
        default:
          this->EndDispatchOrder();
          return;
      }
      this->bOrderState = 2;
      // fall through
    case 2:
      this->sub_43D290(pTarget);
      return;
  }
}

// Starts creep spreading: order 0x9C when dwUnion134 is set, else the type's
// guard order, then the creep background order.
void CUnit::DispatchCreepInit()
{
  if (this->dwUnion134 != 0)
    this->SetOrder(0x9C);
  else
    this->SetOrder(gubUnitOrderGuard[this->wType]);

  this->SetBackgroundOrder(0x66);
}

// An idle larva drifts around its hatchery: stay in range, take a random step
// that keeps it inside the footprint, and suicide when it is walled off.
void CUnit::DispatchIdleLarva()
{
  int x, y;

  if (this->pUnion80 != 0 && !this->IsWithinTargetRange(10, this->pUnion80))
    this->SetDestination_CUnit(this->pUnion80);

  CSprite *pSprite = this->pSprite;

  if (this->wMoveTargetX != pSprite->wPosX
      || this->wMoveTargetY != PosU(pSprite->wPosY)
      || ((this->dwFlags & 0x80000) ? 2 : 1) == 0)
  {
    CUnit *pHome = UNIT_CUNIT_14(this);

    if (pHome == 0)
      return;

    if (this->wMoveTargetX == pHome->pSprite->wPosX
        && this->wMoveTargetY == PosU(pHome->pSprite->wPosY))
      return;

    if (IsCreepPx(this->wMoveTargetX, this->wMoveTargetY))
      return;
  }

  if ((this->pUnion80 == 0 || UNIT_CUNIT_14(this) != this->pUnion80)
      && !IsCreepPx(this->pSprite->wPosX, PosU(this->pSprite->wPosY)))
  {
    this->Suicide();
    return;
  }

  int      nRand = net_rand(0x14);
  CSprite *pSpr  = this->pSprite;

  x = ((nRand & 8) ? 10 : -10) + PosS(pSpr->wPosX);
  y = ((nRand & 0x80) ? 10 : -10) + pSpr->wPosY;

  if (PosU(x) >= gwMapPixWidth || PosU(y) >= gwMapPixHeight
      || !IsCreepPx(x, y))
    return;

  if (!this->ChkLarvaStray(x, y))
  {
    CSprite *pCur = this->pSprite;
    upos_t     wCy  = pCur->wPosY;
    upos_t     wCx  = pCur->wPosX;

    if (this->ChkLarvaStray(PosS(wCx), PosS(wCy)))
      return;

    this->sub_43D800(&x, &y);
  }

  this->SetDestination_xy(x, y);
}

// Spawns a larva at a free tile around the hatchery, at most three alive.
void CUnit::DispatchSpawningLarva()
{
  if (this->bUnion85 != 0)
    return;
  if (this->bPlayer == 11)
    return;

  // The sprite never overlaps the counter; __restrict keeps the payload's
  // literal store after the argument loads, as in the oracle.
  CSprite *__restrict pSprite = this->pSprite;

  gLarvaNearCount = 0;
  find_first_unit_near(pSprite->wPosX, pSprite->wPosY, 0x100, FindCountLarvae, (intptr_t)this);

  if (gLarvaNearCount >= 3)
    return;

  if (UNIT_LARVA_TIMER(this) != 0)
  {
    --UNIT_LARVA_TIMER(this);
    return;
  }

  DWORD dwPt[2];

  UNIT_LARVA_TIMER(this) = 0x25;
  if (this->sub_43D360(dwPt) == 0)
  {
    UNIT_LARVA_TIMER(this) = 3;
    return;
  }

  CUnit *pLarva = CreateUnitAt(0x23, dwPt[0], dwPt[1], this->bPlayer);
  if (pLarva == 0)
    return;

  pLarva->FinishedMaking();
  pLarva->pUnion80 = this;
  pLarva->sub_43D990();

  if (this->dwUnion134 != 0)
    town_move_morphed_building(this, pLarva);
}

// Ticks the creep-spread cooldown and, when it expires, spreads one tile; a
// failed spread re-queues the background order.
void CUnit::DispatchSpreadCreep()
{
  if (this->wType == 0x83 || this->wType == 0x84 || this->wType == 0x85)
    this->DispatchSpawningLarva();

  if (UNIT_CREEP_TIMER(this) != 0)
  {
    --UNIT_CREEP_TIMER(this);
    return;
  }

  UNIT_CREEP_TIMER(this) = 0x0F;

  upos_t wY = this->pSprite->wPosY;
  upos_t wX = this->pSprite->wPosX;

  if (!spread_creep(0x85, PosS(wX), PosS(wY)))
  {
    int bHatchery = (this->wType == 0x83 || this->wType == 0x84
                     || this->wType == 0x85);
    this->SetBackgroundOrder(bHatchery ? 0x4E : 0x17);
  }
}

// The same shape as DispatchCreepInit with the idle background order.
void CUnit::DispatchStopSpreadCreep()
{
  if (this->dwUnion134 != 0)
    this->SetOrder(0x9C);
  else
    this->SetOrder(gubUnitOrderGuard[this->wType]);

  this->SetBackgroundOrder(0x17);
}

// Teleports the unit to the far end of the nydus pair, restoring its old
// position when no free tile is found there.
void CUnit::sub_43D290(void *pArg0)
{
  CUnit    *pEntrance = static_cast<CUnit *>(pArg0);
  CUnit    *pExit     = pEntrance->pUnionD0;
  mpoint_t  ptOld;
  mpoint_t  ptDest;
  mpoint_t  ptFree;  // Hyperion: a map point

  ptOld.x  = this->pSprite->wPosX;
  ptOld.y  = this->pSprite->wPosY;
  ptDest.x = pExit->pSprite->wPosX;
  ptDest.y = pExit->pSprite->wPosY;

  this->sub_430390(1);
  this->SetUnitPosition(ptDest.x, ptDest.y);

  if (!FindPlaceForUnit(this, &ptDest, &ptFree, 0, 0, 0))
  {
    this->SetUnitPosition(ptOld.x, ptOld.y);
    this->sub_430430();
    return;
  }

  gamesnd_play(0x13, pEntrance, 1, 0);
  this->SetUnitPosition(ptFree.x, ptFree.y);
  this->sub_430430();
  gamesnd_play(0x13, pExit, 1, 0);
  this->EndDispatchOrder();
}

#if !defined(OC_PAYLOAD) || defined(OC_WITH_HELD)
// Finds a free creep tile beside the unit for a new larva, trying the sides
// nearer than the recorded stray distances first. Returns 1 when found.
int CUnit::sub_43D360(void *pOut)
{
  int      bFound = 0;
  mrect_t  rc;
  int      pt[2];
  int      nBest;

  upos_t     wX    = this->pSprite->wPosX;
  GetUnitBounds(this, &rc);
  nBest = 101;

  if (UNIT_STRAY_BOTTOM(this) < nBest) {
    pt[0] = PosS(this->pSprite->wPosX);
    pt[1] = rc.bottom + 10;
    if (sub_43C490(pt, 0, 22)) {
      nBest = UNIT_STRAY_BOTTOM(this);
      static_cast<int *>(pOut)[0] = PosS(wX);
      static_cast<int *>(pOut)[1] = pt[1];
      bFound = 1;
    }
  }
  if (UNIT_STRAY_LEFT(this) < nBest) {
    pt[0] = rc.left - 10;
    pt[1] = PosS(this->pSprite->wPosY);
    if (sub_43C490(pt, -22, 0)) {
      nBest = UNIT_STRAY_LEFT(this);
      static_cast<int *>(pOut)[0] = pt[0];
      static_cast<int *>(pOut)[1] = pt[1];
      bFound = 1;
    }
  }
  if (UNIT_STRAY_RIGHT(this) < nBest) {
    pt[0] = rc.right + 10;
    pt[1] = PosS(this->pSprite->wPosY);
    if (sub_43C490(pt, 22, 0)) {
      nBest = UNIT_STRAY_RIGHT(this);
      static_cast<int *>(pOut)[0] = pt[0];
      static_cast<int *>(pOut)[1] = pt[1];
      bFound = 1;
    }
  }
  if (UNIT_STRAY_TOP(this) < nBest) {
    pt[0] = PosS(this->pSprite->wPosX);
    pt[1] = rc.top - 10;
    if (sub_43C490(pt, 0, -22)) {
      static_cast<int *>(pOut)[0] = pt[0];
      static_cast<int *>(pOut)[1] = pt[1];
      return 1;
    }
  }
  if (bFound)
    return bFound;

  {
    pt[0] = PosS(this->pSprite->wPosX) - (short)((short)(gUnitBoundBox[wType].right + 1 + gUnitBoundBox[wType].left) / 2);
    pt[1] = rc.bottom + 10;
    if (!sub_43C490(pt, 0, 22)) {
      pt[0] = PosS(this->pSprite->wPosX) + (short)((short)(gUnitBoundBox[wType].right + 1 + gUnitBoundBox[wType].left) / 2);
      pt[1] = rc.bottom + 10;
      if (!sub_43C490(pt, 0, 22))
        goto left2;
    }
    nBest = UNIT_STRAY_BOTTOM(this);
    bFound = 1;
    static_cast<int *>(pOut)[0] = pt[0];
    static_cast<int *>(pOut)[1] = pt[1];
  }
left2:
  if (UNIT_STRAY_LEFT(this) < nBest) {
    pt[0] = rc.left - 10;
    pt[1] = PosS(this->pSprite->wPosY) - (short)((short)(gUnitBoundBox[wType].bottom + 1 + gUnitBoundBox[wType].top) / 2);
    if (!sub_43C490(pt, -22, 0)) {
      pt[0] = rc.left - 10;
      pt[1] = PosS(this->pSprite->wPosY) + (short)((short)(gUnitBoundBox[wType].bottom + 1 + gUnitBoundBox[wType].top) / 2);
      if (!sub_43C490(pt, -22, 0))
        goto right2;
    }
    nBest = UNIT_STRAY_LEFT(this);
    bFound = 1;
    static_cast<int *>(pOut)[0] = pt[0];
    static_cast<int *>(pOut)[1] = pt[1];
  }
right2:
  if (UNIT_STRAY_RIGHT(this) < nBest) {
    pt[0] = rc.right + 10;
    pt[1] = PosS(this->pSprite->wPosY) - (short)((short)(gUnitBoundBox[wType].bottom + 1 + gUnitBoundBox[wType].top) / 2);
    if (sub_43C490(pt, 22, 0)) {
    found_right:
      nBest = UNIT_STRAY_RIGHT(this);
      bFound = 1;
      static_cast<int *>(pOut)[0] = pt[0];
      static_cast<int *>(pOut)[1] = pt[1];
    } else {
      pt[0] = rc.right + 10;
      pt[1] = PosS(this->pSprite->wPosY) + (short)((short)(gUnitBoundBox[wType].bottom + 1 + gUnitBoundBox[wType].top) / 2);
      if (sub_43C490(pt, 22, 0))
        goto found_right;
    }
  }
top2:
  if (UNIT_STRAY_TOP(this) < nBest) {
    pt[0] = PosS(this->pSprite->wPosX) - (short)((short)(gUnitBoundBox[wType].right + 1 + gUnitBoundBox[wType].left) / 2);
    pt[1] = rc.top - 10;
    if (sub_43C490(pt, 0, -22)) {
      static_cast<int *>(pOut)[0] = pt[0];
      static_cast<int *>(pOut)[1] = pt[1];
      return 1;
    }
    pt[0] = PosS(this->pSprite->wPosX) + (short)((short)(gUnitBoundBox[wType].right + 1 + gUnitBoundBox[wType].left) / 2);
    pt[1] = rc.top - 10;
    if (sub_43C490(pt, 0, -22)) {
      static_cast<int *>(pOut)[1] = pt[1];
      static_cast<int *>(pOut)[0] = pt[0];
      return 1;
    }
    return bFound;
  }
  return bFound;
}
#endif  // OC_PAYLOAD

// Picks the tile a strayed larva should walk back to, one side of the parent
// hatchery's footprint per order state. Case arms are in jump-table order.
void CUnit::sub_43D800(void *pArg0, void *pArg1)
{
  int   *pX      = static_cast<int *>(pArg0);
  int   *pY      = static_cast<int *>(pArg1);
  CUnit *pParent = this->pUnion80;

  if (pParent == 0)
    return;

  mrect_t  bounds;

  GetUnitBounds(pParent, &bounds);

  switch (this->bOrderState)
  {
    case 0:
      *pX = bounds.left - 10;
      *pY = this->pUnion80->pSprite->wPosY;
      return;
    case 1:
      *pX = PosS(this->pUnion80->pSprite->wPosX);
      *pY = bounds.top - 10;
      return;
    case 2:
      *pX = bounds.right + 10;
      *pY = this->pUnion80->pSprite->wPosY;
      return;
    case 3:
      *pX = PosS(this->pUnion80->pSprite->wPosX);
      *pY = bounds.bottom + 10;
      return;
  }

  appcritf("ChkLarvaStray: 2");
}

// find_first_unit_near filter: counts live larvae parented to the searching hatchery and
// answers true once three have been seen.
int FindCountLarvae(void *pArg0, intptr_t nArg1)
{
  CUnit *pUnit = static_cast<CUnit *>(pArg0);

  if (pUnit->wType != 0x23)
    return 0;
  if (pUnit->pSprite->flags.b & 0x20)
    return 0;
  if ((intptr_t)pUnit->pUnion80 != nArg1)
    return 0;

  return ++gLarvaNearCount >= 3;
}

// Swaps in the burrowed sprite and drops the unit to the ground elevation.
void CUnit::ReplaceWithBurrowedSprite()
{
  this->ResetMovementState();
  this->dwFlags |= 0x810;
  this->pSprite->flags.b |= 0x40;

  if (this->wType == 0x67)
    this->pSprite->ReplaceWith(0x1F8);
  else
    this->pSprite->ReplaceWith(0x12C);

  this->pSprite->bElevationLevel = 1;
}

// Records which side of the parent hatchery's footprint the larva sits past.
void CUnit::sub_43D990()
{
  mrect_t  bounds;
  spos_t    nX;

  GetUnitBounds(this->pUnion80, &bounds);
  nX = this->pSprite->wPosX;

  if (nX < bounds.left)
    this->bOrderState = 0;
  else if (PosS(this->pSprite->wPosY) < bounds.top)
    this->bOrderState = 1;
  else
    this->bOrderState = (BYTE)((nX <= bounds.right) + 2);
}

// Restores the unburrowed sprite from the unit type's flingy entry.
void CUnit::MakeUnburrowed()
{
  this->ResetMovementState();
  this->dwFlags &= ~0x810;
  this->pSprite->ReplaceWith(guwFlingySprite[gubUnitFlingy[this->wType]]);
}

// Ages the four per-side stray counters and bumps the one the argument unit
// has wandered past.
void CUnit::StabilizeHatcheryBounds(void *pArg0)
{
  if (UNIT_STRAY_TOP(this) != 0)
    --UNIT_STRAY_TOP(this);
  if (UNIT_STRAY_BOTTOM(this) != 0)
    --UNIT_STRAY_BOTTOM(this);
  if (UNIT_STRAY_LEFT(this) != 0)
    --UNIT_STRAY_LEFT(this);
  if (UNIT_STRAY_RIGHT(this) != 0)
    --UNIT_STRAY_RIGHT(this);

  mrect_t  bounds;

  GetUnitBounds(this, &bounds);

  CSprite *pOSpr = static_cast<CUnit *>(pArg0)->pSprite;

  if (pOSpr->wPosY < bounds.top)
  {
    if (UNIT_STRAY_TOP(this) < 100)
      UNIT_STRAY_TOP(this) += 25;
  }
  else if (pOSpr->wPosY > bounds.bottom)
  {
    if (UNIT_STRAY_BOTTOM(this) < 100)
      UNIT_STRAY_BOTTOM(this) += 25;
  }
  else if (PosS(pOSpr->wPosX) < bounds.left)
  {
    if (UNIT_STRAY_LEFT(this) < 100)
      UNIT_STRAY_LEFT(this) += 25;
  }
  else
  {
    if (UNIT_STRAY_RIGHT(this) < 100)
      UNIT_STRAY_RIGHT(this) += 25;
  }
}
