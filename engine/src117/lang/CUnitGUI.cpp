// CUnitGUI.cpp -- the unit GUI layer: selection circles and team circles, the damage and
// shield overlay images, the screen-visibility queries the sound and camera code asks, and
// the buttonset. Bodies are reconstructed from the 1.17 oracle's instruction stream.
//
// TU membership matters in both directions here. Two pairs are close enough for /Ob2 to
// splice -- CUnit::Show calls sub_423260, and CUnit::sub_422EB0 calls sub_423060 -- and in
// each case the oracle emits a real call, so a change to either half of either pair has to
// be re-checked on both.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../h/CUnitGUI.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"        // AllocCircle/AllocSelect/SelectAdd/Deselect,
                                 // RemoveCircle, ReleaseTeamCircle, RemoveTeamCircleIf,
                                 // OverlayGetImage, DamageOverlaysUp/Down, +0x0E
#include "../h/CImage.h"         // CImage::Kill
#include "../h/CBullet.h"        // CBullet::RemoveTargetReferences
#include "../h/CUnitProtoss.h"   // ours: init_juice_matrix -- sub_423000 tail
                                 // jmps into it
#include "../h/gamesnd.h"        // ours: gamesnd_screendist_pan
#include "../h/minimap.h"        // ours: gamemap_center_on_mtx
#include "../h/statbtn.h"        // ours: target_is_on / target_off /
                                 // placebox_is_on, spelled __fastcall exactly as
                                 // that header spells them: a disagreeing second
                                 // spelling makes gen.py::find_decl emit nothing
                                 // at all
#include "../h/placebox.h"       // ours: set_placebox
#include "../h/CUnitCombat.h"    // ours: sub_46BBB0 -- sub_423260's vision probe
#include "../h/saveload.h"       // ours: get_upgrade_level -- sub_423150
#include "../h/target.h"       // ours: target_off
#include "../gen/globals117.h"
#include "../port/render/port_view.h"  // Hyperion: PortViewSeenRect
#include "../h/playervision.h"  // Hyperion: the slots past 7
#include "../h/mask.h"          // Hyperion: MaskStampHx
#include "../h/typelimits.h"  // Hyperion: a mod's types
#include "../h/selectlimits.h"  // Hyperion: NewUnits, 12 units or 255 (stage S)

// The globals this TU reaches, dual-spelled, one #ifndef per name. The plain extern arm
// alone makes the --dll-mode check vacuous: the name stays a symbol in both compiles, so
// the two objects are byte-identical whatever the address is. The literal arm is what the
// payload ships. One guard per name and never one over the block -- gen emits only marked
// rows' globals, so that set grows over time, and a single guard would go false
// and undeclare every other name under it.

// 0x52904A -- the per-player selection-circle colour table, BYTE[12]: every reference is
// movzx eax, byte ptr [eax+52904Ah] at scale 1, the index is CUnit::bPlayer unbiased, and
// the next address the oracle touches in this block is 0x529056, twelve bytes up. The value
// goes straight into CSprite::AllocCircle(BYTE bSelIndex) and AllocSelect. It keeps a
// TU-local dual spelling because the sheet names nothing near it.
#ifndef sgbPlayerSelCircleColor_g
#ifdef OC_DLL_MODE
#define sgbPlayerSelCircleColor_g (reinterpret_cast<BYTE *>(OC_VA(0x52904Au)))
#else
extern PlayerPlayerSelCircleColorTable sgbPlayerSelCircleColor_g;
#endif
#endif

// set_placebox -- oracle 0x47D300 (placebox.cpp), declared in src117/h/placebox.h: a
// .cpp-local declaration is invisible to every other TU, so placebox.cpp could not have
// defined the body without a second declaration, and two declarations of one name make gen
// emit nothing. That header also carries the arg2 correction (int a3 -> BYTE a3); the call
// site in this file passes a literal, so it cannot move a byte.

// target_off -- oracle 0x4B2900 (statcmd.cpp). statbtn.h declares target_is_on and
// placebox_is_on but not this one; src117/lang/usel.cpp does, and this is that spelling
// character for character.

#ifndef gpSquareMap
#ifdef OC_DLL_MODE
#define gpSquareMap           (*reinterpret_cast<DWORD **>(OC_VA(0x666454u)))
#else
extern DWORD   *gpSquareMap;
#endif
#endif

// 0x521E78 -- gubRClickActionType, BYTE[228] (arr/units.cpp). Registered but not yet
// emitted, so it is spelled here with the registry's own type.
#ifndef gubRClickActionType
#ifdef OC_DLL_MODE
#define gubRClickActionType   (reinterpret_cast<BYTE *>(OC_VA(0x521E78u)))
#else
extern BYTE     gubRClickActionType[228] OC_UNALIGNED;
#endif
#endif

// 0x5264B0 / 0x5264B2 -- the screen's top-left position in tiles, X then Y. Two WORD scalars
// and not an array: every reference in the corpus names one absolute address, all 48 are
// 16-bit and none sign-extends, so they are unsigned. They are the scroll origin --
// IsOffScreen bounds the sprite's tile against [X - 2, X + 0x16] and [Y - 2, Y + 0x0F], a
// 24 x 17 tile window, the 640x480 viewport plus a two-tile margin -- and they sit
// immediately below gwMapWidth (0x5264B4) and gwMapHeight (0x5264B6), two scalars of the
// same shape. They keep a TU-local dual spelling because the sheet names nothing in this
// block (0x5263D0..0x53DAD0 is one unnamed arr/all.cpp entry).
#ifndef gwScreenTileX
#ifdef OC_DLL_MODE
#define gwScreenTileX         (*reinterpret_cast<WORD *>(OC_VA(0x5264B0u)))
#else
extern WORD     gwScreenTileX;
#endif
#endif

#ifndef gwScreenTileY
#ifdef OC_DLL_MODE
#define gwScreenTileY         (*reinterpret_cast<WORD *>(OC_VA(0x5264B2u)))
#else
extern WORD     gwScreenTileY;
#endif
#endif

// sub_49C8B0 -- oracle 0x49C8B0 (285 B), callee only.
//
// ECX is read as an input and EDX never is, there are no stack parameters, and the ledger
// name is free, so one register argument. All three call sites (CUnit::Hide,
// CUnit::DestroyImpl, CUnit::GiveToPlayer) load ECX and discard EAX, and the callee's own
// EAX at the ret is inherited from a tail call sub_472100 rather than produced, so void.
// Not renamed to a CUnit member: a nullary callee offers no evidence either way.
void __fastcall sub_49C8B0(CUnit *pUnit);

// guiUnitBuildImage @0x51FFE0 -- units.dat's construction image row, DWORD[228]: the row
// count and the boundary with the next sheet name agree on the extent.
#ifndef guiUnitBuildImage
#ifdef OC_DLL_MODE
#define guiUnitBuildImage (reinterpret_cast<DWORD *>(OC_VA(0x51FFE0u)))
#else
extern DWORD guiUnitBuildImage[228] OC_UNALIGNED;
#endif
#endif

// guwSpriteImage @0x51EAA8 -- sprites.dat's image row. Spelled here exactly as
// src117/lang/CSprite.cpp:307/348 already spells it (WORD *, [517]); the
// referent here is `movzx ecx, word ptr [eax*2+51EAA8h]` -- stride 2, indexed
// by CSprite::wSpriteID -- which agrees with that declaration.
#ifndef guwSpriteImage
#ifdef OC_DLL_MODE
#define guwSpriteImage (reinterpret_cast<WORD *>(OC_VA(0x51EAA8u)))
#else
extern WORD guwSpriteImage[517];
#endif
#endif

// gubUnitUnmaskTbl @0x521AD8 -- sheet-named (data_naming_1.17.csv, arr/units.cpp). The
// units.dat sight range: sub_423150 is a sight-range query -- its four switch arms each
// test a sight upgrade (0x14 Ocular Implants for the Ghost, 0x19 Antennae for the Overlord,
// 0x29 Sensor Array for the Observer, 0x26 Apial Sensors for the Arbiter) and return 0xB
// when it is researched -- and every other path returns this table's element.
//   * width 1 -- movzx eax, byte ptr [eax+521AD8h], scale 1, byte operand.
//   * extent 228 -- units.dat's row count; the next sheet name is gubUnitArmorType
//     @0x521BC0, i.e. 232 bytes of storage, which is 228 plus the 4 bytes of alignment
//     padding every neighbouring BYTE[228] table in this block carries.
#ifndef gubUnitUnmaskTbl
#ifdef OC_DLL_MODE
#define gubUnitUnmaskTbl (reinterpret_cast<BYTE *>(OC_VA(0x521AD8u)))
#else
extern BYTE gubUnitUnmaskTbl[228] OC_UNALIGNED;
#endif
#endif

// gpImageUnit @0x543300 -- the CImage.cpp scratch pointer the iscript
// interpreter reads. Spelled exactly as src117/lang/CUnitTerran.cpp:35/44 and
// CUnitCombat.cpp:317/319 already spell it; SyncConstructionGraphic saves it,
// points it at `this` across CSprite::ChangeImage, and restores it.
#ifndef gpImageUnit
#ifdef OC_DLL_MODE
#define gpImageUnit (*reinterpret_cast<CUnit **>(OC_VA(0x543300u)))
#else
extern CUnit *gpImageUnit;
#endif
#endif

// 0x4230E0 / 0x4230F0 -- the two sprite thunks. Eight bytes each: load pSprite into ECX and
// tail jmp. The oracle emits jmp and not call, so the C++ must be a tail-position call of a
// void function from a void function -- any trailing statement at all would cost a ret.

void CUnit::sub_4230E0()
{
  this->pSprite->ReleaseTeamCircle();
}

void CUnit::RemoveTeamCircle()
{
  this->pSprite->RemoveTeamCircleIf();
}

// 0x422B00 -- CUnit::GetPan (15 B).
int CUnit::GetPan()
{
  int nX = this->pSprite->wPosX;

  return gamesnd_screendist_pan(PosS(nX));
}

// 0x423100 -- CUnit::CreateTeamCircle (21 B).
//
// The oracle loads bPlayer, then pSprite, then indexes the colour table -- the sprite load
// sits between the two halves of the table lookup. That is what MSVC produces for a single
// expression statement; hoisting the colour into a local separates them and costs a reload.
void CUnit::CreateTeamCircle()
{
  this->pSprite->AllocCircle(sgbPlayerSelCircleColor_g[this->bPlayer]);
}

// 0x423120 -- unit_gui_reset (34 B).
//
// The last statement is a conditional tail jmp: call target_is_on / test al, al /
// jnz target_off / ret. One textual call site in tail position, reached only on the true
// arm, is what buys the jmp; any trailing statement after the if emits a call and a ret
// instead.
void unit_gui_reset()
{
  if (placebox_is_on())
    set_placebox(0, NULL, 1);

  if (target_is_on())
    target_off();
}

// 0x422B10 -- CUnit::CenterView (40 B).
//
// movsx eax, word / cdq / and edx, 1Fh / lea / sar 5 is MSVC's signed divide by 32: the
// sign bias is added before the arithmetic shift. That is what x / 32 emits for a signed x,
// not what x >> 5 emits (one bare sar).
//
// Y is computed first because MSVC evaluates call arguments right to left, and the oracle's
// first divide is of +0x16 (wPosY) into ESI, so the natural two-argument call in source
// order is already the oracle's order and no address-taken local is needed here.
void CUnit::CenterView()
{
  CSprite *pSprite = this->pSprite;

  gamemap_center_on_mtx(PosS(pSprite->wPosX) / 32, pSprite->wPosY / 32);
}

// 0x422B60 -- CUnit::HasImageOverlay (58 B).
//
// The sprite and subunit pair, and the subunit arm is not guarded by a units.dat flag here
// (unlike Show and sub_422EB0) -- it is a plain null test on +0x70. Read the branch targets
// rather than the mnemonics.
//
// The return is xor eax, eax / test edx, edx / setnz al, so the two OverlayGetImage results
// must be merged into one value: two returns would give two setcc.
BOOL CUnit::HasImageOverlay(int nImageId)
{
  CImage *pImage = this->pSprite->OverlayGetImage(nImageId, nImageId);

  if (pImage == NULL && this->subunit != NULL)
    pImage = this->subunit->pSprite->OverlayGetImage(nImageId, nImageId);

  return pImage != NULL;
}

// 0x423000 -- CUnit::sub_423000 (89 B).
//
// One textual epilogue, cross-jumped: the flag-set arm falls through to RemoveCircle,
// clears sprite bit 3 and returns, and every failing test in the other arm jumps backwards
// into that same block, so the source has one flags.b &= ~0x08 and one return path, with
// the Protoss-grid tail jmp as an early exit. pSprite is re-read in the tail even though
// the entry loaded it into ECX -- there is a call between the two uses, so it must not be
// cached in a local.
void CUnit::sub_423000()
{
  if (gulUnitTypeFlags[this->wType] & 0x20000000)
  {
    this->pSprite->RemoveCircle();
    this->pSprite->flags.b &= ~0x08;
    return;
  }

  this->pSprite->Deselect();

  if (gPlayback == 0 && this->bPlayer == g_LocalNationID
      && (this->dwFlags & 1) && this->wType == 0x9C)
  {
    init_juice_matrix();
  }
}

// 0x422F60 -- CUnit::Show (115 B).
//
// The whole body is inside one guard on the sprite's hidden bit, which skips 0x65 bytes to
// a bare pop esi / ret. The subunit is tested twice with the same two-level shape
// (subunit != NULL, then a units.dat 0x10 flag) and the oracle does not merge them:
// hoisted into one test they fold and the body comes up short.
void CUnit::Show()
{
  if (this->pSprite->flags.b & 0x20)
  {
    CSprite *pSprite = this->pSprite;

    pSprite->flags.b &= ~0x20;

    if (this->subunit != NULL && (gulUnitTypeFlags[this->subunit->wType] & 0x10))
      this->subunit->pSprite->flags.b &= ~0x20;

    this->sub_423260();
    this->CircleUpdate();
    this->TrackCollision();
    this->ResetMovementState();

    if (this->subunit != NULL && (gulUnitTypeFlags[this->subunit->wType] & 0x10))
      this->subunit->ResetMovementState();

    this->UnloadFromTransList();
    this->MoveToUsedList();
  }
}

// Hides a unit: drops every reference to it, takes it off the collision grid and the
// status bar, moves it from the used list to the trans list, and masks its sprite and its
// turret's. Nothing happens if the sprite is already hidden.
void CUnit::Hide()
{
  if (!(this->pSprite->flags.b & 0x20))
  {
    this->RemoveTargetReferences(0);
    CBullet::RemoveTargetReferences(this);
    sub_49C8B0(this);
    this->RemoveFromStatusBar();
    this->UntrackCollision();
    this->ResetMovementState();
    this->UnloadFromUsedList();
    this->MoveToTransList();

    CSprite *pSprite = this->pSprite;

    pSprite->flags.b |= 0x20;
    pSprite->SetMask(0);

    if (this->subunit != NULL && (gulUnitTypeFlags[this->subunit->wType] & 0x10))
    {
      CSprite *pSubSprite = this->subunit->pSprite;

      pSubSprite->flags.b |= 0x20;
      pSubSprite->SetMask(0);
    }
  }
}

// 0x422C20 -- CUnit::IsOffScreen (88 B).
//
// Four disjuncts and one fallthrough: every test that succeeds jumps to the same
// mov eax, 1h epilogue and only falling off the end reaches xor eax, eax, so it is a plain
// return A || B || C || D;. The twice-repeated movsx / cdq / and edx, 1Fh / lea / sar 5 is
// a signed divide by 32 rather than >> 5, and it is what forces the (short) on wPosX, which
// is declared WORD where wPosY is short. Each screen global is read once and reused by an
// add/lea for its upper bound.
BOOL CUnit::IsOffScreen()
{
  CSprite *pSprite = this->pSprite;

  int nTileX = PosS(pSprite->wPosX) / 32;
  int nTileY = pSprite->wPosY / 32;

  // Hyperion: two megatiles past what the camera shows, 0x16 and 0x0F
  // past the view's first in 1.17.
  int nLeft, nTop, nRight, nBottom;
  PortViewSeenRect(&nLeft, &nTop, &nRight, &nBottom);
  int nTileLeft = gwScreenTileX;
  int nTileTop = gwScreenTileY;
  if (nLeft != glScrollPixelX || nTop != glScrollPixelY) {
    nTileLeft = nLeft / 32;
    nTileTop = nTop / 32;
  }
  return nTileX + 2 < nTileLeft
      || nTileY + 2 < nTileTop
      || nTileX > nTileLeft + (nRight - nLeft + 31) / 32 + 2
      || nTileY > nTileTop + (nBottom - nTop + 31) / 32 + 2;
}

// 0x422C80 -- CUnit::CanSeeUnit (130 B).
//
// mov ebx, 1h / mov cl, byte ptr [edi+4Ch] / shl ebx, cl is the player mask, computed
// before the null test and kept in EBX on every path. The source tests pTarget != NULL
// positively and lets the tile lookup fall out of the bottom, the null arm jumping 46 bytes
// to the fog-map tail; the one early return FALSE inside that arm is reached only when the
// 0x300 flags are set and the E4 mask bit is clear.
//
// The fog read is movzx eax, byte ptr [eax+esi*4] off gpSquareMap, whose low byte is the
// visibility mask, then not eax at 32 bits -- the complement follows the widening.
BOOL CUnit::CanSeeUnit(CUnit *pTarget)
{
  // Hyperion: a slot past 7 sees by Hyperion's masks
  if (g_hxPlayers && this->bPlayer >= PLAYER_LOBBY_117) {
    if (pTarget != NULL) {
      if ((pTarget->dwFlags & 0x300) && !UnitDetectedBy(pTarget, this->bPlayer))
        return FALSE;
      return SpriteSeenBy(pTarget->pSprite, this->bPlayer);
    }
    return VisionHxTileSeen((this->wOrderTargetY / 32) * gwMapWidth + this->wOrderTargetX / 32,
                            this->bPlayer);
  }
  int nMask = 1 << this->bPlayer;

  if (pTarget != NULL)
  {
    if ((pTarget->dwFlags & 0x300) && !(pTarget->dwUnionE4 & nMask))
      return FALSE;

    return pTarget->pSprite->bVisibilityFlags & (BYTE)nMask;
  }

  return ~(BYTE)gpSquareMap[(this->wOrderTargetY / 32) * gwMapWidth
                            + this->wOrderTargetX / 32] & nMask;
}

// 0x422D10 -- CUnit::CanSeeFocusUnit (118 B).
//
// CanSeeUnit with the target read from pOrderTarget (+0x5C) instead of a stack slot -- same
// mask, same two guards, same fog tail, ret instead of ret 4h. The twelve bytes of
// difference are exactly the push ebp / mov ebp, esp / mov eax, [ebp+8h] frame and the two
// pop ebp / ret 4h widenings.
BOOL CUnit::CanSeeFocusUnit()
{
  CUnit *pTarget = this->pOrderTarget;

  // Hyperion: a slot past 7 sees by Hyperion's masks
  if (g_hxPlayers && this->bPlayer >= PLAYER_LOBBY_117) {
    if (pTarget != NULL) {
      if ((pTarget->dwFlags & 0x300) && !UnitDetectedBy(pTarget, this->bPlayer))
        return FALSE;
      return SpriteSeenBy(pTarget->pSprite, this->bPlayer);
    }
    return VisionHxTileSeen((this->wOrderTargetY / 32) * gwMapWidth + this->wOrderTargetX / 32,
                            this->bPlayer);
  }
  int nMask = 1 << this->bPlayer;

  if (pTarget != NULL)
  {
    if ((pTarget->dwFlags & 0x300) && !(pTarget->dwUnionE4 & nMask))
      return FALSE;

    return pTarget->pSprite->bVisibilityFlags & (BYTE)nMask;
  }

  return ~(BYTE)gpSquareMap[(this->wOrderTargetY / 32) * gwMapWidth
                            + this->wOrderTargetX / 32] & nMask;
}

// 0x422D90 -- CUnit::GetRClickAction (63 B).
//
// Only AL is ever defined (mov al, 3h, mov al, bl), so the return is BYTE, and the table
// index is the wType still in EAX from the entry movzx rather than a re-read.
//
// wType must not be a named local: spelled int nType = this->wType; MSVC gives it EBX, the
// register bAction needs later, and hoists push ebx above the early return 3, which costs a
// pop ebx on that path -- 64 bytes against 63. The cmov is seeded with the value already in
// the destination, which is what an in-place if (cond) x = 2; produces; a ternary seeds the
// constant and inverts the condition.
BYTE CUnit::GetRClickAction()
{
  if (this->wType == 0x67 && (this->dwFlags & 0x10))
    return 3;

  BYTE bAction = gubRClickActionType[this->wType];

  if (bAction == 0 && (this->dwFlags & 2) && this->IsRallyer())
    bAction = 2;

  return bAction;
}

void sub_422B40(CUnit *pUnit)
{
  if (gulUnitTypeFlags[pUnit->wType] & 0x10)
    pUnit = pUnit->subunit;

  pUnit->sub_423000();
}

// 0x422FE0 -- CUnit::CreateShieldEffect (30 B).
//
// The parameter's width is not decided here: the entry's mov eax, dword ptr [ebp+8h] /
// movzx eax, al is what a BYTE parameter widened to unsigned int emits as well, since the
// slot is a dword and MSVC loads it before narrowing. Both spellings are 30 bytes, so
// CUnit.h's BYTE stands unrefuted rather than proved.
//
// shr, not sar: the subtraction is unsigned byte-wrap arithmetic written in 32 bits,
// ((unsigned)(BYTE)dir - 0x7C) >> 3 & 0x1F, and the trailing mask normalises a value below
// 0x7C. mov dword ptr [ebp+8h], eax is the tail-call argument slot and not a write to the
// parameter: CSprite::AllocShield takes one dword, so MSVC reuses the incoming slot.
void CUnit::CreateShieldEffect(BYTE bDirection)
{
  this->pSprite->AllocShield((((unsigned int)bDirection - 0x7C) >> 3) & 0x1F);
}

// 0x422E70 -- CUnit::RemoveImageOverlay (61 B).
//
// The sprite and subunit pair, the same shape as HasImageOverlay: the subunit arm
// is a plain null test on +0x70, not a units.dat flag. The branch targets say so rather
// than the mnemonics -- jnz $+0x19 (first lookup hit) and jz $+0x19 (no subunit) both land
// past the second lookup, the first at mov ecx, eax and the second at pop esi.
//
// One merged result and one kill: two separate if (p) p->Kill(); arms would give two calls.
void CUnit::RemoveImageOverlay(int nFirstImageId, int nLastImageId)
{
  CImage *pImage = this->pSprite->OverlayGetImage(nFirstImageId, nLastImageId);

  if (pImage == NULL && this->subunit != NULL)
    pImage = this->subunit->pSprite->OverlayGetImage(nFirstImageId, nLastImageId);

  if (pImage != NULL)
    pImage->Kill();
}

// 0x422EB0 -- CUnit::sub_422EB0 (65 B).
//
// jz $+0x20 is exactly the then-arm, so the units.dat 0x10 flag being set routes through
// the subunit while the else arm calls on `this` -- and that arm emits no mov ecx at all,
// since ESI holds the entry copy and nothing clobbers ECX. The subunit is read twice with
// the call in between, so it must not be bound to a local. The index is movzx ecx, bl off
// the dword the parameter was loaded into, gpNewUnits[(BYTE)nGroup] with an int parameter.
void CUnit::sub_422EB0(int nGroup)
{
  if (gulUnitTypeFlags[this->wType] & 0x10)
  {
    this->subunit->sub_423060(nGroup);
    NewUnits()[(BYTE)nGroup] = this->subunit;
  }
  else
  {
    this->sub_423060(nGroup);
  }
}

// 0x422F00 -- CUnit::SetButtonset (81 B).
//
// Four guards land on the same byte: the jnz on dwFlags & 0x400 and those on bLockdownTimer
// and bStasisTimer all target offset 46, the units.dat test, and the jz on bParalyzeTimer
// targets the store at offset 70. So the predicate is
//     (no disable) || (units.dat bit 0) || (wButtonset == 0xE4)
// around one guarded store. mov eax, 0E4h / cmp dx, ax stages the 16-bit immediate in a
// register to avoid the length-changing prefix, and the operand order -- variable first --
// is what puts the parameter in DX. +0x94 is wButtonset: the store is WORD-wide and +0x96
// is already bCloakCount, so the slot is two bytes.
#include "../port/data/port_moddata.h"  // Hyperion: ModUnitButtonset, ModUnitHasCard
#include "../h/handicap.h"  // Hyperion: a player's handicap

void CUnit::SetButtonset(WORD wButtonset)
{
  // Hyperion: a mod's unit has a set of 1.17's units', the button sets being 1.17's
  // table
  if (UnitTypeMod(wButtonset) && !ModUnitHasCard(wButtonset))
    wButtonset = static_cast<WORD>(ModUnitButtonset(wButtonset));
  if ((this->dwFlags & 0x400) == 0 && this->bLockdownTimer == 0
      && this->bStasisTimer == 0 && this->bParalyzeTimer == 0
      || (gulUnitTypeFlags[this->wType] & 1)
      || wButtonset == 0xE4)
  {
    this->wButtonset = wButtonset;
  }
}

// 0x423060 -- CUnit::sub_423060 (120 B).
//
// The selection-circle branch. The colour lookup and its push are hoisted above the branch
// because both arms pass it, so the source writes the expression out in both arms and lets
// /Ox share it. In the then-arm (units.dat bit 29 set) the two stores after AllocSelect go
// through one reload of pSprite, which has to be bound inside the arm because the entry's
// ECX copy is dead across the call. The else-arm passes SelectAdd's parameter first (it is
// pushed last) and repeats sub_423000's four-term Protoss-grid guard.
void CUnit::sub_423060(BYTE nSelectionIndex)
{
  if (gulUnitTypeFlags[this->wType] & 0x20000000)
  {
    this->pSprite->AllocSelect(sgbPlayerSelCircleColor_g[this->bPlayer]);

    CSprite *pSprite = this->pSprite;

    pSprite->flags.b |= 0x08;
    pSprite->bSelectionIndex = (BYTE)nSelectionIndex;
  }
  else
  {
    this->pSprite->SelectAdd((BYTE)nSelectionIndex,
                             sgbPlayerSelCircleColor_g[this->bPlayer]);

    if (gPlayback == 0 && this->bPlayer == g_LocalNationID
        && (this->dwFlags & 1) && this->wType == 0x9C)
    {
      ShowPowerTemplates();
    }
  }
}

// 0x422DD0 -- CUnit::UpdateDmgOverlays (154 B).
//
// mov eax, 55555556h / imul / shr 1Fh / add is MSVC's signed divide by 3, so the first
// quantity is gxUnitLife[wType] / 3.
//
// this->wType is spelled out three times rather than cached: MSVC cannot keep a member
// across the CSprite::GetDamageOverlayCount call, so it holds the index in EDI for the two
// subscripts around the call and re-reads it for the third, which is where EDI becomes
// nStep. The two div are unsigned and the final idiv signed, and both loops compare
// bDmgOverlayCount against BL unsigned, so the target is truncated to a byte at the
// compare.
void CUnit::UpdateDmgOverlays()
{
  WORD wType = this->wType;
  // Hyperion: the maximum at the owner's handicap
  const LONG lMax = HandicapLife(gxUnitLife[wType], this->bPlayer);
  int nBase  = lMax / 3;
  int nCount = this->pSprite->GetDamageOverlayCount();
  int nStep  = (int)((unsigned int)(lMax - nBase)
                     / (unsigned int)(nCount + 1));
  int nRem   = (int)((unsigned int)(lMax - nBase)
                     % (unsigned int)nStep);
  int nWant  = (this->lHitPoints - nRem - nBase - 1) / nStep;

  if (nWant < 0)
    nWant = 0;

  while (this->bDmgOverlayCount < (BYTE)nWant)
  {
    this->pSprite->DamageOverlaysDown();
    this->bDmgOverlayCount++;
  }

  while (this->bDmgOverlayCount > (BYTE)nWant)
  {
    this->pSprite->DamageOverlaysUp();
    this->bDmgOverlayCount--;
  }
}

// 0x423330 -- CUnit::SyncConstructionGraphic (196 B).
//
// The shifted byte is masked twice off one CSE'd intermediate rather than tested through
// the stored local, so the and is followed by its own test. The image choice is one CSE'd
// load in ECX -- an if/else, not a zero-initialised local, since nothing ever writes 0.
//
// This body needs CSprite::sub_415E50's first parameter declared BYTE, a cross-file
// constraint: it is int a1 in src117/h/CSprite.h today, and at int MSVC widens the BYTE
// local into the push (194 bytes) where at BYTE it spills the byte to a dword slot and
// pushes the slot whole, the oracle's 196. The callee's own entry is compatible with either
// width, so this call site is the only place it shows.
void CUnit::SyncConstructionGraphic(BOOL bFlag)
{
  BYTE bRleFrame = 0;
  BYTE bRleWait = 0;
  int nImageId;

  DWORD dwConstruct = this->dwFlags >> 8;
  BYTE bMode = (BYTE)dwConstruct & 3;

  if (dwConstruct & 3)
    this->pSprite->GetRLEInfo(&bRleFrame, &bRleWait);

  CUnit *pPrevImageUnit = gpImageUnit;
  gpImageUnit = this;

  if (bFlag != 0 && guiUnitBuildImage[this->wType] != 0)
    nImageId = guiUnitBuildImage[this->wType];
  else
    nImageId = guwSpriteImage[this->pSprite->wSpriteID];

  this->pSprite->ChangeImage(nImageId, this->bUnion21);

  gpImageUnit = pPrevImageUnit;

  if (bMode)
  {
    this->pSprite->sub_415E50(bMode, bRleFrame, bRleWait);

    if (this->dwUnionE4 & gdwLocalMask)
      this->pSprite->Show();
  }

  this->SyncAllStatusEffects();
  this->bDmgOverlayCount = this->pSprite->GetDamageOverlayCount();
}

int CUnit::sub_423150(int a1)
{
  if ((this->dwFlags & 2) && !(this->dwFlags & 1)
      && !this->IsZergBldgUpgrading())
  {
    return 4;
  }

  if (a1 == 0 && UnitMyopia(this).Any())  // Hyperion: bMyopiaPlayers, every slot
    return 2;

  switch (this->wType)
  {
  case 0x01:
    if (get_upgrade_level(this->bPlayer, 0x14))
      return 0x0B;
    break;

  case 0x2A:
    if (get_upgrade_level(this->bPlayer, 0x19))
      return 0x0B;
    break;

  case 0x54:
    if (get_upgrade_level(this->bPlayer, 0x26))
      return 0x0B;
    break;

  case 0x46:
    if (get_upgrade_level(this->bPlayer, 0x29))
      return 0x0B;
    break;
  }

  return gubUnitUnmaskTbl[this->wType];
}

// 0x423260 -- CUnit::sub_423260 (200 B).
//
// Recomputes the unit's fog-of-war vision mask and hands it, with the sprite's pixel
// position and the unit's sight range, to sub_46BBB0.
//
// The mask loop walks &SharedVision[0..11] against a strength-reduced end pointer while EDI
// holds the raw +0x121 byte, so the accumulator and the bit source are separate variables
// and the step is written out in both arms of the bit test. It is a bottom-tested do/while
// over an int index: a for with a constant trip count unrolls six times, and a pointer
// comparison emits jb where the oracle's jl is signed.
//
// Four declarations are fixed by the registers they land in: the position locals are WORD
// or MSVC narrows the load itself, wY comes before wX because arguments evaluate right to
// left, nFlag comes before pSprite to put `this` in ESI, and nVision is BYTE or the
// shared-vision merge is recomputed rather than merged in place.
void CUnit::sub_423260()
{
  int nMask;

  // Hyperion: in a game with slots past 7 the sight is a mask of every slot, its
  // bits past 7 stamped by MaskStampHx; a lobby slot's units see for it, and neutral's
  // only through a parasite, as in 1.17
  if (g_hxPlayers) {
    const PlayerSet qParasite = UnitParasite(this);
    if (this->bPlayer == PLAYER_NEUTRAL && !qParasite.Any())
      return;
    if (this->wType == 0x0E)
      return;
    PlayerSet q;
    if (this->HasFlag() || (this->wType == 0xD7 && this->bOrderId == 0x11)) {
      q = PlayerSet::All();
    } else {
      q = qParasite;
      if (this->bPlayer != PLAYER_NEUTRAL)
        q |= PlayerVision(this->bPlayer);
      if (qParasite.Any())  // almost always empty: then the walk adds nothing
        for (int i = 0; i < PlayerSlots(); i++)
          if (qParasite.Has(i))
            q |= PlayerVision(i);
    }
    const int nRange = this->sub_423150(0);
    const int bAir = (this->dwFlags >> 2) & 1;
    if (q.Low8() != 0)
      sub_46BBB0(PosS(this->pSprite->wPosX), PosS(this->pSprite->wPosY), bAir,
                 static_cast<int>(q.Low8()), nRange, 0);
    MaskStampHx(PosS(this->pSprite->wPosX), PosS(this->pSprite->wPosY), bAir, q, nRange);
    return;
  }

  if (this->bPlayer >= 8 && this->_unmapped_121[0] == 0)
    return;

  if (this->wType == 0x0E)
    return;

  if (this->HasFlag() || (this->wType == 0xD7 && this->bOrderId == 0x11))
  {
    nMask = 0xFF;
  }
  else
  {
    BYTE nVision = this->_unmapped_121[0];

    nMask = nVision;

    if (this->bPlayer < 8)
      nMask |= SharedVision[this->bPlayer];

    int i = 0;
    int nBit = 1;

    do
    {
      if (nBit & nVision) {
        nMask |= SharedVision[i];
        i++;
        nBit += nBit;
      } else {
        i++;
        nBit += nBit;
      }
    }
    while (i < 12);
  }

  int nFlag = (this->dwFlags >> 2) & 1;
  CSprite *pSprite = this->pSprite;
  upos_t wY = pSprite->wPosY;
  upos_t wX = pSprite->wPosX;

  sub_46BBB0(PosS(wX), PosS(wY), nFlag, nMask, this->sub_423150(0), 0);
}


