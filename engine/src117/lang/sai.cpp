// sai.cpp -- the AI region and area accessor layer. Bodies are in oracle VA
// order.
//
// GetRegion_xy's definition is visible to UnitCanReachGroundTarget, which keeps
// the oracle's code only with pCandSprite declared before pSrcSprite.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/sai.h"
#include "../h/sai_PathCreate.h"  // sub_490D80
#include "../h/SAI_Banker.h"
#include "../h/CUnitSearches.h"
#include "../h/CUnitCombat.h"
#include "../h/sai_TownBuilder.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"      // CUnit.h only forward-declares it; wPosX/wPosY
#include "../h/point16.h"
#include "../h/rect16.h"
#include "../h/net_cmds.h"

#include "../gen/globals117.h"


// The globals this TU reaches, dual-spelled with one guard per name:
//
//   #ifdef OC_DLL_MODE
//   #define NAME (*reinterpret_cast<T *>(0xVAu))
//   #else
//   extern T NAME;
//   #endif

// epGlob @0x69B584 -- the pathing and region block's base. Spelled the same way
// as SAI_Build.cpp's and sai_TownBuilder.cpp's, deliberately: a second,
// disagreeing spelling of a registered global is a C2371 in some other TU. The
// generator emits no row for this address, so the dual spelling is needed here.
#ifdef OC_DLL_MODE
#define epGlob          (*reinterpret_cast<void **>(OC_VA(0x69B584u)))
#else
extern void *epGlob;                        // 0x69B584
#endif

// gwMapWidth (0x5264B4), gwMapHeight (0x5264B6) and gpSquareMap (0x666454)
// belong to marked rows, so src117/gen/globals117.h emits them and they are not
// re-spelled here: a second declaration of an emitted global is a C2371.

// The bodies, in oracle VA order.

// GetRegionBounds -- oracle 0x481A00 (34 B).
//
// The odd one out of the family: it is an 8-byte struct copy, not two stores.
// Every other two-deref accessor here folds the base into the index
// (`add ecx, [epGlob]`, 30 B for this body); the oracle instead pays a
// `push esi` and two 7-byte SIB loads to keep the two addends apart, which no
// arrangement of two separate dword statements produces. A whole-object
// assignment emits it, because MSVC lowers the copy off one address expression
// it never re-materialises and so never hoists a base out of.
//
// The pointee is not derived: the bytes move 8 bytes as two dwords and say
// nothing about what they mean. `rect16_t` is the tree's 8-byte geometry type
// and is used here only as a copy width -- its four `short` members are never
// touched individually -- so the header's declared `void *` stands.
void GetRegionBounds(WORD wRegion, void *pBounds)
{
  *(rect16_t *)pBounds = ((const ep_glob_t *)epGlob)->aRegions[wRegion].rcBounds;  // Hyperion: the box by field (stage E)
}

// GetRegionCenter -- oracle 0x481A30 (44 B).
//
// The same two x256 fixed-point fields as GetRegionPosition below, delivered
// 32-bit through two separate out-params instead of one point16_t. `ret 4h`, so
// the third argument is the single stack word, and the frame pointer the oracle
// sets up (`push ebp / mov ebp, esp`) is how it reaches [ebp+8h].
void GetRegionCenter(WORD wRegion, int *pX, int *pY)
{
  const ep_region_t *pRec = &((const ep_glob_t *)epGlob)->aRegions[wRegion];

  *pX = pRec->nX >> 8;
  *pY = pRec->nY >> 8;
}

// GetRegionPosition -- oracle 0x481A60 (38 B).
//
// GetRegionCenter's 16-bit sibling: the `sar 8` is the /256 of the engine's
// fixed point and the two WORD stores at +0/+2 off one pointer are point16_t by
// layout. The shift is signed, so the source fields are `int`, not DWORD.
void GetRegionPosition(WORD wRegion, point16_t *pPos)
{
  const ep_region_t *pRec = &((const ep_glob_t *)epGlob)->aRegions[wRegion];

  pPos->x = PosS(pRec->nX >> 8);
  pPos->y = PosS(pRec->nY >> 8);
}

// GetDefPriority -- oracle 0x481A90 (20 B). The region's default priority, the
// byte at +0x20 of its 64-byte record.
DWORD GetDefPriority(WORD wRegion)
{
  return ((const ep_glob_t *)epGlob)->aRegions[wRegion].bAreaLinks;
}

// GetRegionPaths -- oracle 0x481AB0 (28 B).
//
//   movzx ecx, cx / shl ecx, 6h / add ecx, [epGlob]
//   mov eax, [ecx+44A08h] / mov [edx], eax
//   movzx eax, byte ptr [ecx+44A02h] / ret
//
// The out-param takes the region's path-set pointer; the return is a
// zero-extended BYTE, so all 32 bits are the callee's own and `int` is honest.
int GetRegionPaths(WORD wRegion, WORD **pPaths)
{
  const ep_region_t *pRec = &((const ep_glob_t *)epGlob)->aRegions[wRegion];

  *pPaths = pRec->pPaths;
  return pRec->bPathFirst;
}

// GetRegionNeighbors -- oracle 0x481AD0 (39 B).
//
// GetRegionPaths' sibling, with the same out-param store and a two-term result:
// `movzx eax, byte ptr [esi+44A03h]` minus `movsx ecx, byte ptr [esi+44A1Dh]`,
// an unsigned count less a signed adjustment, so the result is `int` and not a
// byte. Spell either field the other way and the movzx / movsx pair swaps.
int GetRegionNeighbors(WORD wRegion, WORD **pPaths)
{
  const ep_region_t *pRec = &((const ep_glob_t *)epGlob)->aRegions[wRegion];

  *pPaths = pRec->pPaths;
  return pRec->bPathCount - pRec->nExtraPaths;
}

// NumAreas -- oracle 0x481B00 (9 B).
//
// The area count is the WORD at the block's own +0, loaded with movsx, so the
// callee defines all 32 bits itself and the return is `int`. The field is signed
// in the image: `(const short *)`, not `(const WORD *)`, is what emits the movsx.
// The split records (ep_split_t in sai_PathCreate.cpp, six bytes each) carry the
// region ids from 0x2000 up, so id w's record is at bScratch + (w - 0x2000) * 6.
// The readers below keep the oracle's base + w * 6 shape and fold the rest into
// this constant, spelled from ep_glob_t so that it follows the block on x64.
#define OC_SPLIT_BASE ((int)offsetof(ep_glob_t, bScratch) - 0x2000 * 6)

// A region record's field as an offset from the block, for the readers that
// step records by byte offset.
#define OC_REGION_FIELD(f) ((int)(offsetof(ep_glob_t, aRegions) + offsetof(ep_region_t, f)))

int NumAreas()
{
  return ((const ep_glob_t *)epGlob)->nRegions;
}

// A minitile's column and row inside its 32-pixel tile, 0..3 each.
struct minitile_pos_t {
  int x;
  int y;
};

// Returns the region at pixel (x, y). A tile value below 0x2000 is the region;
// otherwise it names a split record whose minitile mask picks one of two ids.
// The quadrants go through a struct local: as plain locals MSVC narrows the
// masks along with the shift count, which the oracle does not.
WORD __fastcall GetRegion_xy(int x, int y)
{
  const BYTE *pBase = (const BYTE *)epGlob;
  WORD wRegion = ((const WORD *)(pBase + offsetof(ep_glob_t, wMap)))[(y / 32) * EP_STRIDE + (x / 32)];
  const BYTE *pRec;
  minitile_pos_t mini;

  if (wRegion < 0x2000)
    return wRegion;

  pRec = pBase + wRegion * 6;
  mini.y = (y / 8) & 3;
  mini.x = (x / 8) & 3;
  if (*(const WORD *)(pRec + OC_SPLIT_BASE) & (1 << (BYTE)(mini.y * 4 + mini.x)))
    return *(const WORD *)(pRec + (OC_SPLIT_BASE + 4));

  return *(const WORD *)(pRec + (OC_SPLIT_BASE + 2));
}

// GetRegion_CUnit -- oracle 0x481BA0 (16 B).
//
//   mov eax, [ecx+0Ch] / movsx edx, word ptr [eax+16h]
//   movsx ecx, word ptr [eax+14h] / jmp GetRegion_xy
//
// A tail jump: the unit's sprite (CUnit +0x0C) supplies the position pair and
// GetRegion_xy does the rest. The `(short)` on wPosX is not symmetry --
// src117/h/CSprite.h declares +0x14 `WORD` and +0x16 `short`, so the cast is
// what turns +0x14's load into the oracle's `movsx` while +0x16 needs none.
WORD __fastcall GetRegion_CUnit(CUnit *pUnit)
{
  CSprite *pSprite = pUnit->pSprite;

  return GetRegion_xy(PosS(pSprite->wPosX), pSprite->wPosY);
}

// IsInaccessibleRegion -- oracle 0x481BB0 (33 B).
//
// A WORD field of the 64-byte region record equal to the sentinel 0x1FFD. The
// oracle's `mov esi, 1FFDh`, with its push/pop pair, is MSVC 2013 materialising
// a 16-bit immediate into a register rather than paying the operand-size prefix
// on the compare -- the same idiom as GetRegionFromPx's `mov ecx, 2000h`, and
// evidence that the comparison really is 16-bit rather than promoted.
BOOL IsInaccessibleRegion(WORD wRegion)
{
  return ((const ep_glob_t *)epGlob)->aRegions[wRegion].wGroup == 0x1FFD;
}

// IsInaccessibleRegion_xy -- oracle 0x481BE0 (38 B).
//
// IsInaccessibleRegion's (x,y) front end, with the sibling's test written out
// rather than called: the oracle emits no `call` for it, so the expression is
// expanded here by hand instead of relying on /Ob2 to splice the neighbour in.
//
// Both register arguments are forwarded to GetRegion_xy untouched, so this body
// shows no width for either; `int` is the spelling that agrees with that
// callee's own contract.
BOOL IsInaccessibleRegion_xy(int x, int y)
{
  WORD wRegion = GetRegion_xy(x, y);

  return ((const ep_glob_t *)epGlob)->aRegions[wRegion].wGroup == 0x1FFD;
}

// IsValidInaccessibleRegion -- oracle 0x481C10 (46 B).
//
//   mov eax, 2000h / cmp cx, ax / jb <+3>
//   xor eax, eax / ret
//   <IsInaccessibleRegion's body>
//
// The `jb` target is what gives the shape: the rel8 is +3, which clears exactly
// `xor eax, eax` (2 B) plus `ret` (1 B) and lands on `movzx edx, cx`. So the
// below arm runs the sentinel test, and the fall-through -- a region id at or
// above 0x2000, which GetRegionFromPx shows is the not-a-region range --
// returns 0. The guard comes first in the body, so it costs a whole early exit
// rather than folding into the `setz`.
int IsValidInaccessibleRegion(WORD wRegion)
{
  if (wRegion >= 0x2000)
    return 0;

  return ((const ep_glob_t *)epGlob)->aRegions[wRegion].wGroup == 0x1FFD;
}


// GetRegionFromPx
//
// Pixel coordinates to a region id, in two steps. The tile index is
// `(y / 32) * 256 + x / 32`, and the oracle spells both divisions as signed
// division by 32 (`cdq` / `and edx, 1Fh` / `lea` / `sar 5h`, MSVC's
// round-toward-zero lowering rather than a `sar` alone), so the operands are
// `int` and not unsigned. That index reads a WORD table at epGlob+0x0C: a value
// below 0x2000 is already a region id and is returned as is, and at or above it
// the value indexes a six-byte record array whose first WORD is the answer.
//
// The WORD return is the callers' call: 2 of the 3 consuming sites narrow EAX
// 16-bit right after the call, and `movzx eax, ax` is not emittable for an
// `int` return.

// The early return is worth two bytes. As one exit
// (`if (w >= 0x2000) w = ...; return w;`) MSVC lowers the record load to
// `lea eax, [eax+eax*2]` / `movzx eax, word ptr [...]`, 63 bytes and a full
// 32-bit define. With the common arm returning early it emits the oracle's
// `mov ecx, eax` / `lea eax, [ecx+ecx*2]` / `mov ax, word ptr [...]`: a 16-bit
// store into AX, safe because the `movzx` that produced the tile value already
// zeroed the upper half, where the index copy to ECX is what frees AX.
WORD GetRegionFromPx(int x, int y)
{
  int nIndex;
  WORD wRegion;

  nIndex = (y / 32) * EP_STRIDE + (x / 32);
  wRegion = ((const WORD *)((const BYTE *)epGlob + offsetof(ep_glob_t, wMap)))[nIndex];

  if (wRegion < 0x2000)
    return wRegion;

  return *(const WORD *)((const BYTE *)epGlob + (OC_SPLIT_BASE + 2) + wRegion * 6);
}

// GetAreaIndex -- oracle 0x481C90 (20 B).
//
// The 16-bit store into AX is the return type: the upper half of EAX still holds
// the epGlob pointer at the return, which is only legal because the caller reads
// AX alone. An `int` spelling could not emit this.
WORD GetAreaIndex(WORD wRegion)
{
  return ((const ep_glob_t *)epGlob)->aRegions[wRegion].wArea;
}

// UnitCanReachGroundTarget -- oracle 0x481CB0 (78 B).
//
// "Same walkable area?" -- GetAreaIndex applied to each unit's region, compared
// 16-bit. Both accessors are expanded by hand for the reason given on
// IsInaccessibleRegion_xy above: the oracle calls GetRegion_xy twice and
// nothing else.
//
// The call order is the oracle's and the named locals hold it: `mov eax,
// [edx+0Ch]` comes first, so the EDX argument, pSource, is the first
// GetRegion_xy call and pCandidate the second. Both sprite pointers are hoisted
// before either call because ECX does not survive one.
BOOL __fastcall UnitCanReachGroundTarget(CUnit *pCandidate, CUnit *pSource)
{
  CSprite *pCandSprite = pCandidate->pSprite;
  CSprite *pSrcSprite = pSource->pSprite;
  const BYTE *pBase = (const BYTE *)epGlob;
  int nSrcOff;
  int nCandOff;

  nSrcOff  = GetRegion_xy(PosS(pSrcSprite->wPosX),  pSrcSprite->wPosY)  * (int)sizeof(ep_region_t);
  nCandOff = GetRegion_xy(PosS(pCandSprite->wPosX), pCandSprite->wPosY) * (int)sizeof(ep_region_t);

  return *(const WORD *)(pBase + nCandOff + OC_REGION_FIELD(wArea))
      == *(const WORD *)(pBase + nSrcOff  + OC_REGION_FIELD(wArea));
}

// UnitCanReachGroundTarget_xy -- oracle 0x481D00 (80 B).
//
// The same comparison with an explicit (x, y) destination instead of a second
// unit. `ret 4h`: the unit is ECX, nX is EDX and nY the one stack word, and the
// split is the callee's own -- it parks EDX in EDI across the first call and
// re-pairs it with [ebp+8h] for the second.
//
// Here the first call is the unit's own sprite position and the second the
// explicit destination, the opposite role order from the sibling above, so both
// bodies name their results rather than nesting the calls.
BOOL __fastcall UnitCanReachGroundTarget_xy(CUnit *pUnit, int nX, int nY)
{
  CSprite *pSprite = pUnit->pSprite;
  const BYTE *pBase = (const BYTE *)epGlob;
  int nSelfOff;
  int nDestOff;

  nSelfOff = GetRegion_xy(PosS(pSprite->wPosX), pSprite->wPosY) * (int)sizeof(ep_region_t);
  nDestOff = GetRegion_xy(nX, nY) * (int)sizeof(ep_region_t);

  return *(const WORD *)(pBase + nSelfOff + OC_REGION_FIELD(wArea))
      == *(const WORD *)(pBase + nDestOff + OC_REGION_FIELD(wArea));
}

// sai_map_init -- oracle 0x481D50 (26 B).
//
// __fastcall with three arguments: width in ECX, height in EDX, the fog map
// pushed. A `call` and a bare `ret` with nothing written to EAX in between, so
// this body's 32-bit result is sub_490D80's, which is what sai.h's `int` return
// records and what maphdr_MTXM consumes as `!= 0`. The plain `return` below only
// compiles because sai_PathCreate.h gives sub_490D80 an `int` return; against a
// `void` one it is C2440, and dropping the `return` is C4716, which MSVC 2013
// makes an error that /WX- does not reach.
int sai_map_init(void)
{
  return sub_490D80(gwMapWidth, gwMapHeight, gpSquareMap);
}

// sai_path_free -- oracle 0x481D70 (15 B). Frees the path pool and the glob.
// The oracle tail-jumps the folded one-byte `ret` at 0x4BE950 with no
// arguments, through gen's nullary alias of it.
void __fastcall voice_squelch();
void sai_path_free()
{
  sai_pathpool_free();
  sai_glob_free();

  voice_squelch();
}

// sai_path_init -- oracle 0x481D80 (10 B).
//
//   call sai_pathpool_init / jmp sai_debug_init
//
// Nullary. No `ret` of its own -- the tail jump makes sai_debug_init's result
// this one's, which is what pins BOOL (sai.h:270).
BOOL sai_path_init()
{
  sai_pathpool_init();

  return sai_debug_init();
}
