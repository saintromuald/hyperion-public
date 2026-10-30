// CUnitInit.cpp -- the unit master lists: where a CUnit lives, how it moves between lists,
// and the small accessors the rest of the engine reaches them through. Oracle TU
// CUnitInit.cpp, 0x424810-0x4271D0; bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the oracle (WPO
// off) never does across a TU. FinishConstruction, Complete, DestroyImpl and sub_426600
// are defined here and every caller still emits a real call to them, so a change to any of
// them has to be re-checked across this whole TU, callers included.

// The six lists, and the one thing to know before editing this file. Every CUnit is on
// exactly one intrusive list through the pPrev/pNext pair at +0x00/+0x04, and that list is
// its lifecycle state. Each is a (head, tail) pair of globals, contiguous in .data:
//     0x665AE0/AE4  sgpUsedHead    / sgpUsedTail      live units
//     0x665AE8/AEC  sgpDeadHead    / sgpDeadTail      dying, orders draining
//     0x665AF0/AF4  sgpFreeHead    / sgpFreeTail      recycled slots
//     0x665AF8/AFC  sgpTransHead   / sgpTransTail     units inside transports
//     0x665B00/B04  sgpScannerHead / sgpScannerTail   scanner sweeps
// A second membership runs alongside: the per-player chain at +0x68/+0x6C, with its heads
// in sgpPlayerLists_g[12] at 0x665B08. One object, two chains -- confusing them is the
// easiest way to misread a body here. The head is the first of every (ppHead, ppTail) pair.

// Eight of the functions below are mov eax, dword ptr [<addr>] / ret. Bodies that small are
// shape-insensitive: matching one is not evidence that the derivation behind it is right,
// in either direction. What carries the weight is that the same addresses reappear as CLink
// arguments in MoveToUsedList, MoveToTransList, RemoveFrom*, RemoveDead and sub_426530,
// where the head/tail pairing decides the bytes and a swapped pair would not match. Read
// the accessors as a naming layer over evidence that lives in the bigger bodies, not as
// evidence in themselves.
#include "oc_data.h"  // Hyperion: OC_VA
#include <vector>
#include "../h/playervision.h"  // Hyperion: the slots past 7
#include "../h/handicap.h"  // Hyperion: a player's handicap
#include "../h/mutators.h"  // Hyperion: what a resource holds
#include "../port/data/port_formats.h"  // Hyperion: PortSave117Get, PortSave117Size, PortSaveReadIds
#include "../gen/save117.h"
#include "../h/platform.h"
#include "../h/CLink.h"          // ours: PrependThis/Unlink -- the (ppHead, ppTail) contract
#include "../h/CSprite.h"        // ours: flags at +0x0E, SetCreator, Kill
#include "../h/CUnit.h"
#include "../h/CUnitInit.h"      // ours: the file that OWNS the basename gen.py emits for
#include "../h/CUnitSave.h"      // saveload_unitlist
#include "../h/repulse.h"        // repulse_init_directions
                                 // this TU -- see its banner, and the two callee
                                 // declarations it records as removed
#include "../h/net_cmds.h"       // ours: error_get_str
#include "../h/order.h"          // ours: map_msg_target_self
#include <stddef.h>
#include <string.h>
#include "../h/CFlingy.h"        // ours: AngleBetween
#include "../h/rect16.h"         // ours: rect16_t, the gUnitBoundBox row
#include "../h/SAI_Main.h"
#include "../h/CImage.h"         // ours: GetOverlayOffset, SetOffset
#include "../h/TrigAct.h"        // ours: CreateUnitAt, net_rand
#include "../h/CUnitTerran.h"    // ours: placebox_is_on, sub_47D530, sub_4B47F0
#include "../h/point16.h"
#include "../h/CUnitZSpells.h"   // ours: FindPlaceForUnit
#include "../h/count.h"          // ours: count_add_completion
#include "../h/CUnitGUI.h"       // ours: unit_gui_reset
#include "../h/select.h"         // ours: select_units
#include "../h/saveload.h"       // ours: decompress
#include "../h/Storm.h"
#include "../h/CUnitBuild.h"    // ours: ClipToMap
#include "../h/TAI_AreaStrength.h"  // ours: tai_assign_unit_strength
#include "../h/CUnitBounds.h"   // ours: cunit_bounds_init
#include "../h/snd.h"           // ours: load_array_data
#include "../h/creep.h"         // ours: remove_creep_completely
#include "../h/placebox.h"      // ours: placebox_show_geyser
#include "../h/CTarget.h"
#include "../h/COrder.h"
#include "../h/sai_path.h"
#include "../h/CUnitProtoss.h"   // ours: RequestJuiceUpdate
#include "../h/statport.h"       // ours: statport_portrait_hide
#include "../h/CUnitSpells.h"    // ours: LoadCloakList
#include "../h/CThingy.h"
#include "../h/CBullet.h"
#include "../h/statbtn.h"
#include "../h/collide.h"
#include "../h/CUnitMisc.h"
#include "../h/gds_local.h"
#include "../h/mapmsg.h"
#include "../h/tile2.h"
#include "../gen/globals117.h"
#include "../h/gamedata.h"  // Hyperion: array_data_t
#include "../h/hxrules.h"  // Hyperion: HxRules (revision 11)
#include "../h/typelimits.h"  // Hyperion: a mod's types
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX

// 0x51B800 order_techType, sheet row 511; the same spelling as select.cpp.
#ifdef OC_DLL_MODE
  #ifndef order_techType
  #define order_techType (reinterpret_cast<BYTE *>(OC_VA(0x51B800u)))
  #endif
#else
extern tech_id_t order_techType[189];
#endif

// 0x520370 gubUnitFaceAngle, sheet row 549: units.dat's start direction, BYTE
// from `mov al, byte ptr [eax+520370h]` indexed by the unit type.
#ifdef OC_DLL_MODE
  #ifndef gubUnitFaceAngle
  #define gubUnitFaceAngle (reinterpret_cast<BYTE *>(OC_VA(0x520370u)))
  #endif
#else
extern BYTE gubUnitFaceAngle[228] OC_UNALIGNED;
#endif

// 0x5DA390 / 0x5DA394, sheet rows 777-778, file column CUnitInit.cpp. DWORD
// from the two immediate stores in CUnit::InitUnits.
#ifdef OC_DLL_MODE
  #ifndef sgdwOrderSeqDelay_g
  #define sgdwOrderSeqDelay_g (*reinterpret_cast<DWORD *>(OC_VA(0x5DA390u)))
  #endif
  #ifndef sgdwBackgroundOrderSeqDelay_g
  #define sgdwBackgroundOrderSeqDelay_g (*reinterpret_cast<DWORD *>(OC_VA(0x5DA394u)))
  #endif
#else
extern DWORD sgdwOrderSeqDelay_g;
extern DWORD sgdwBackgroundOrderSeqDelay_g;
#endif

// The orders.dat and units.dat descriptor blocks load_array_data fills, sheet
// rows 111 and 116. Only ever addressed; extents run to the next sheet name.
#ifdef OC_DLL_MODE
  #ifndef gORDERSArrayDataTbl_g
  #define gORDERSArrayDataTbl_g (reinterpret_cast<array_data_t*>(OC_VA(0x501278u)))
  #endif
  #ifndef gUnitsArrayDataTbl
  #define gUnitsArrayDataTbl (reinterpret_cast<array_data_t*>(OC_VA(0x5014F0u)))
  #endif
#else
extern array_data_t gORDERSArrayDataTbl_g[20] OC_UNALIGNED;
extern array_data_t gUnitsArrayDataTbl[55] OC_UNALIGNED;
#endif

// 0x6AB4C0 byte_6AB4C0, registered; compared unsigned against 1.
#ifdef OC_DLL_MODE
  #ifndef byte_6AB4C0
  #define byte_6AB4C0 (*reinterpret_cast<BYTE *>(OC_VA(0x6AB4C0u)))
  #endif
#else
extern BYTE byte_6AB4C0;
#endif

// 0x520D50 gubUnitOrderAI, sheet row 556: units.dat's computer idle order,
// BYTE from `movzx eax, byte ptr [eax+520D50h]` indexed by the unit type.
#ifdef OC_DLL_MODE
  #ifndef gubUnitOrderAI
  #define gubUnitOrderAI (reinterpret_cast<BYTE *>(OC_VA(0x520D50u)))
  #endif
#else
extern order_id_t gubUnitOrderAI[228] OC_UNALIGNED;
#endif

// 0x51B200 order_canBeInterrupted, sheet row 503. BYTE from `cmp byte ptr
// [eax+51B200h], cl`, indexed by an order id.
#ifdef OC_DLL_MODE
  #ifndef order_canBeInterrupted
  #define order_canBeInterrupted (reinterpret_cast<BYTE *>(OC_VA(0x51B200u)))
  #endif
#else
extern BYTE order_canBeInterrupted[189] OC_UNALIGNED;
#endif

// 0x520A98 gubUnitPriorityPlane, sheet row 553: units.dat's elevation level,
// BYTE from `mov al, byte ptr [edi+520A98h]` into CSprite::bElevationLevel.
#ifdef OC_DLL_MODE
  #ifndef gubUnitPriorityPlane
  #define gubUnitPriorityPlane (reinterpret_cast<BYTE *>(OC_VA(0x520A98u)))
  #endif
#else
extern BYTE gubUnitPriorityPlane[228] OC_UNALIGNED;
#endif

// 0x5238B0 guwUnitTimeToBuild, registered but not yet emitted by gen.
#ifdef OC_DLL_MODE
  #ifndef guwUnitTimeToBuild
  #define guwUnitTimeToBuild (reinterpret_cast<WORD *>(OC_VA(0x5238B0u)))
  #endif
#else
extern WORD guwUnitTimeToBuild[228] OC_UNALIGNED;
#endif

// 0x543300 gpImageUnit, registered but not yet emitted by gen.
#ifdef OC_DLL_MODE
  #ifndef gpImageUnit
  #define gpImageUnit (*reinterpret_cast<CUnit **>(OC_VA(0x543300u)))
  #endif
#else
extern CUnit *gpImageUnit;
#endif

// The list globals, dual-spelled on purpose: a plain extern leaves each one a relocation in
// both objects, so the shipped-form check would pass without proving the address, where
// this spelling bakes a concrete one to check. src117/lang/dlg_dlgdraw.cpp is the reference
// form.
//
// Every name needs its own #ifndef. gen defines only the globals of rows it has marked, so
// one guard over the block goes false as soon as the first of them is marked and every
// other name in it becomes undeclared. Names from data_naming_1.17.csv, CUnitInit.cpp.
#ifdef OC_DLL_MODE
  #ifndef sgpUsedHead
  #define sgpUsedHead      (*reinterpret_cast<CUnit **>(OC_VA(0x665AE0u)))
  #endif
  #ifndef sgpUsedTail
  #define sgpUsedTail      (*reinterpret_cast<CUnit **>(OC_VA(0x665AE4u)))
  #endif
  #ifndef sgpDeadHead
  #define sgpDeadHead      (*reinterpret_cast<CUnit **>(OC_VA(0x665AE8u)))
  #endif
  #ifndef sgpDeadTail
  #define sgpDeadTail      (*reinterpret_cast<CUnit **>(OC_VA(0x665AECu)))
  #endif
  #ifndef sgpFreeHead
  #define sgpFreeHead      (*reinterpret_cast<CUnit **>(OC_VA(0x665AF0u)))
  #endif
  #ifndef sgpFreeTail
  #define sgpFreeTail      (*reinterpret_cast<CUnit **>(OC_VA(0x665AF4u)))
  #endif
  #ifndef sgpTransHead
  #define sgpTransHead     (*reinterpret_cast<CUnit **>(OC_VA(0x665AF8u)))
  #endif
  #ifndef sgpTransTail
  #define sgpTransTail     (*reinterpret_cast<CUnit **>(OC_VA(0x665AFCu)))
  #endif
  #ifndef sgpScannerHead
  #define sgpScannerHead   (*reinterpret_cast<CUnit **>(OC_VA(0x665B00u)))
  #endif
  #ifndef sgpScannerTail
  #define sgpScannerTail   (*reinterpret_cast<CUnit **>(OC_VA(0x665B04u)))
  #endif
  #ifndef sgpPlayerLists_g
  #define sgpPlayerLists_g (reinterpret_cast<CUnit **>(OC_VA(0x665B08u)))
  #endif
  #ifndef sguiCount
  #define sguiCount        (*reinterpret_cast<DWORD *>(OC_VA(0x665B38u)))
  #endif
  #ifndef guwCreateUnitErr
  #define guwCreateUnitErr (*reinterpret_cast<WORD *>(OC_VA(0x5DA398u)))
  #endif
  // The three units.dat/weapons.dat tables cunit_init_findrange walks.
  #ifndef guwAttachedUnit
  #define guwAttachedUnit  (reinterpret_cast<WORD *>(OC_VA(0x51FB90u)))
  #endif
  #ifndef gubUnitFindEnemy
  #define gubUnitFindEnemy (reinterpret_cast<BYTE *>(OC_VA(0x5219F0u)))
  #endif
  #ifndef wpn_maxrange_g
  #define wpn_maxrange_g   (reinterpret_cast<DWORD *>(OC_VA(0x5254B8u)))
  #endif
  // sgUnitsMem is registered with its type withheld, because `CUnit[1700]` would force
  // CUnit to be complete in every TU including gen/globals117.h, so
  // gen never emits it in either form and this declaration is unconditional.
  #define sgUnitsMem       (reinterpret_cast<CUnit *>(OC_VA(0x5DA3A0u)))
#else
extern CUnit *sgpUsedHead;
extern CUnit *sgpUsedTail;
extern CUnit *sgpDeadHead;
extern CUnit *sgpDeadTail;
extern CUnit *sgpFreeHead;
extern CUnit *sgpFreeTail;
extern CUnit *sgpTransHead;
extern CUnit *sgpTransTail;
extern CUnit *sgpScannerHead;
extern CUnit *sgpScannerTail;
extern PlayerTable<CUnit *, PlayerTagUnitLists> sgpPlayerLists_g;
extern DWORD  sguiCount;
extern WORD   guwCreateUnitErr;
extern WORD   guwAttachedUnit[228] OC_UNALIGNED;
extern BYTE   gubUnitFindEnemy[228] OC_UNALIGNED;
extern DWORD  wpn_maxrange_g[130] OC_UNALIGNED;
// sgUnitsMem -- the unit table itself, and its extent is exact rather than bounded.
// Nothing is named between it and the next entry in the sheet (sgpUsedHead @0x665AE0), and
// that gap is 0x8B740 = 571,200 bytes = 1700 x 0x150 exactly, with 0x150 the CUnit stride
// src117/h/CUnit.h already asserts and 1700 the known unit limit.
extern CUnit  sgUnitsMem[POOL_UNITS_STORE];
#endif

// The accessors. Each is mov eax, dword ptr [<addr>] / ret, six bytes, and static: no ECX
// is touched, so there is no `this`.

// CUnit::GetDeadList -- oracle 0x4255B0, 6 B.
CUnit *CUnit::GetDeadList() { return sgpDeadHead; }

// CUnit::GetDeadListTail -- oracle 0x425D80, 6 B.
CUnit *CUnit::GetDeadListTail() { return sgpDeadTail; }

// CUnit::GetScannerList -- oracle 0x4255D0, 6 B.
CUnit *CUnit::GetScannerList() { return sgpScannerHead; }

// CUnit::GetScannerListTail -- oracle 0x425D90, 6 B.
CUnit *CUnit::GetScannerListTail() { return sgpScannerTail; }

// CUnit::GetTransList -- oracle 0x4255E0, 6 B.
CUnit *CUnit::GetTransList() { return sgpTransHead; }

// CUnit::GetTransListTail -- oracle 0x425DA0, 6 B.
CUnit *CUnit::GetTransListTail() { return sgpTransTail; }

// CUnit::GetUsedList -- oracle 0x425600, 6 B.
CUnit *CUnit::GetUsedList() { return sgpUsedHead; }

// CUnit::GetUsedListTail -- oracle 0x425DB0, 6 B.
CUnit *CUnit::GetUsedListTail() { return sgpUsedTail; }

// CUnit::Head -- oracle 0x4255F0, 6 B.
//
// mov eax, 5DA3A0h / ret -- an address, not a load, which is what separates this from the
// eight accessors above even though the byte count is the same. It returns the base of the
// unit table, corroborated by its users rather than assumed: CUnit::GetUnitID and
// CUnit::FromIdAlive turn this - Head() and Head() + id*0x150 into a table index, which is
// only meaningful if the result is the first element.
CUnit *CUnit::Head() { return sgUnitsMem; }

// CUnit::GetPlayerList -- oracle 0x4255C0, 11 B.
// CUnit::GetPlayerUnitList -- oracle 0x4256F0, 8 B.
//
// Two accessors over one array, differing only in the declared width of the index, which
// is why the pair is worth reading together. GetPlayerList opens movzx eax, cl;
// GetPlayerUnitList scales straight off ECX with no widening at all. The index arrives
// narrow in one and 32 bits wide in the other, so the parameter really is BYTE in the
// first and int in the second -- and these are not forwarders, they consume the value,
// which is the case where a parameter width can be seen at all.
CUnit *CUnit::GetPlayerList(BYTE bPlayer) { return sgpPlayerLists_g[bPlayer]; }

CUnit *CUnit::GetPlayerUnitList(int player) { return sgpPlayerLists_g[player]; }

// The four (head, tail) list operations. Each is three instructions: push the tail address,
// push the head address, call the CLink routine -- right-to-left, so the head is argument
// one, matching CLink::PrependThis(ppHead, ppTail) and CLink::Unlink(ppHead, ppTail).
//
// The CLink cast is free and it is the tree's convention: src117/h/CLink.h deliberately
// does not make its users derive from CLink, so callers cast at the call site, and the
// prev/next pair sits at offset 0 of a CUnit, so the cast is an identity and costs no
// instruction. The oracle's ECX is just the thiscall `this`, which happens either way.

// CUnit::MoveToTransList -- oracle 0x425DC0, 16 B.
void CUnit::MoveToTransList() {
  reinterpret_cast<CLink *>(this)->PrependThis(
      reinterpret_cast<CLink **>(&sgpTransHead),
      reinterpret_cast<CLink **>(&sgpTransTail));
}

// CUnit::MoveToUsedList -- oracle 0x425DD0, 16 B.
void CUnit::MoveToUsedList() {
  reinterpret_cast<CLink *>(this)->PrependThis(
      reinterpret_cast<CLink **>(&sgpUsedHead),
      reinterpret_cast<CLink **>(&sgpUsedTail));
}

// CUnit::UnloadFromTransList -- oracle 0x427070, 16 B.
void CUnit::UnloadFromTransList() {
  reinterpret_cast<CLink *>(this)->Unlink(
      reinterpret_cast<CLink **>(&sgpTransHead),
      reinterpret_cast<CLink **>(&sgpTransTail));
}

// CUnit::UnloadFromUsedList -- oracle 0x427080, 16 B.
void CUnit::UnloadFromUsedList() {
  reinterpret_cast<CLink *>(this)->Unlink(
      reinterpret_cast<CLink **>(&sgpUsedHead),
      reinterpret_cast<CLink **>(&sgpUsedTail));
}

// CUnit::SelfDestruct -- oracle 0x425700, 12 B.
//
// or byte ptr [ecx+93h], 4h / jmp CUnit::Suicide. The tail jump is what fixes the return
// type: this returns exactly what Suicide returns, i.e. void, and it only takes that form
// because the call is the last statement with nothing after it. SetOrder_xy sets bit 0 of
// the same byte, the second independent body fixing bUnion93's width at one.
void CUnit::SelfDestruct() {
  bUnion93 |= 4;
  Suicide();
}

// CUnit::FinishedMaking -- oracle 0x424DA0, 16 B.
//
// Two calls, the second a tail jump. The push esi / mov esi, ecx and then the reload
// mov ecx, esi before the second call is MSVC keeping `this` alive across a thiscall that
// may clobber ECX; it is not something the source says.
void CUnit::FinishedMaking() {
  FinishConstruction();
  Complete();
}

// CUnit::IsDoodad -- oracle 0x425CA0, 33 B.
//
// An unsigned range test on the type id -- the branches are jb and jnbe, so a signed
// comparison loses the match. The oracle spells the bounds mov ecx, 0CBh / cmp ax, cx
// rather than cmp ax, 0CBh; that is MSVC's own choice for a 16-bit compare against a
// constant and falls out of the natural source, so do not try to write it.
BOOL CUnit::IsDoodad() {
  return wType >= 0xCB && wType <= 0xD5;
}

// CUnit::ShowCreateError -- oracle 0x425020, 44 B.
//
// A static, not a member: movzx esi, cl treats ECX as a BYTE value, so ECX is the player
// and not a `this`.
//
// The cast below reads a WORD global through a DWORD lvalue, which looks wrong and is
// measured. The oracle tests and clears guwCreateUnitErr (0x5DA398) at 16 bits but loads
// the call argument from it at 32:
//     cmp  word  ptr [5DA398h], 0h
//     mov  ecx,  dword ptr [5DA398h]
//     mov  word  ptr [5DA398h], ax
// A WORD read shares the test's subexpression and comes out 42 bytes. Reading two bytes
// past the WORD is safe in this image -- 0x5DA39A..0x5DA39F is padding -- but the compiler
// could not have known that, so this is a measured spelling and not a recovered one.
void CUnit::ShowCreateError(BYTE bPlayer) {
  if (guwCreateUnitErr != 0) {
    map_msg_target_self((char *)error_get_str(*(DWORD *)&guwCreateUnitErr), bPlayer);
    guwCreateUnitErr = 0;
  }
}

// CUnit::SetOwnerColor -- oracle 0x426E80, 68 B.
//
// Re-stamp the creator on this unit's sprite and on every subunit below it that carries
// units.dat flag 0x10.
//
// The parameter is 32 bits here and 8 at the callee: the oracle loads its own argument as a
// DWORD and pushes it whole into CSprite::SetCreator, whose parameter src117/h/CSprite.h
// derives as a BYTE, because a narrow callee slot lets MSVC push a register it already has.
// The lea ebx, dword ptr [ebx] in the listing is an alignment nop, not written here.
void CUnit::SetOwnerColor(int nCreator) {
  pSprite->SetCreator((BYTE)nCreator);
  for (CUnit *u = subunit; u != NULL; u = u->subunit) {
    if (!(gulUnitTypeFlags[u->wType] & 0x10))
      break;
    u->pSprite->SetCreator((BYTE)nCreator);
  }
}

// CUnit::sub_424DB0 -- oracle 0x424DB0, 35 B.
//
// FinishedMaking's conditional sibling, and worth reading next to it: the same
// FinishConstruction-then-Complete pair, with a predicate between them and a BOOL out.
// mov eax, 1h on the taken arm, and the fallthrough returns whatever sub_426600 left in
// EAX -- its own zero, since that arm is only reached when test eax, eax said so.
// Declaring this void would drop the mov eax, 1h and lose 5 bytes.
BOOL CUnit::sub_424DB0() {
  FinishConstruction();
  if (!sub_426600())
    return FALSE;
  Complete();
  return TRUE;
}

// CUnit::Destroy -- oracle 0x425CD0, 60 B.
//
// Recursive, not a loop: the inner call CUnit::Destroy is a rel32 back into this same body
// with ECX = the subunit, so a subunit chain of depth n costs n frames. Compare
// CUnit::SetOwnerColor above, which walks the same chain as an iteration -- the two shapes
// sit six functions apart in one TU, and reading either as the other loses the match.
//
// The subunit null-out is inside the guard. Both jz land past the
// mov dword ptr [esi+70h], 0h (they share the target at +0x36), so the slot is cleared
// only on the arm that actually recursed. Hoisting it out compiles to a store on every
// path and a different branch target.
void CUnit::Destroy() {
  CUnit *pSub = subunit;
  if (pSub != NULL && (gulUnitTypeFlags[pSub->wType] & 0x10)) {
    pSub->Destroy();
    subunit = NULL;
  }
  DestroyImpl();
  pSprite->Kill();
  pSprite = NULL;
}

// CUnit::sub_426530 -- oracle 0x426530, 151 B.
//
// Two memberships come off and one goes on: first the per-player chain (+0x68/+0x6C),
// repairing sgpPlayerLists_g[bPlayer] if this unit was its head, then whichever master list
// it was on, then onto the dead list. It names four of the six (head, tail) pairs in
// argument positions, so a swapped pair shows as a byte delta.
//
// The three-way choice is one if/else-if/else selecting a list, then one Unlink -- the
// oracle's jump chain converges on a single call, where three Unlink calls would emit
// three. The comparison is written this == sgpPlayerLists_g[bPlayer] and not the other way
// round, since MSVC keeps the written order (cmp r32, r/m32 against cmp r/m32, r32).
void CUnit::sub_426530() {
  if (pPlayerUnitPrev != NULL)
    pPlayerUnitPrev->pPlayerUnitNext = pPlayerUnitNext;
  if (pPlayerUnitNext != NULL)
    pPlayerUnitNext->pPlayerUnitPrev = pPlayerUnitPrev;
  if (this == sgpPlayerLists_g[bPlayer])
    sgpPlayerLists_g[bPlayer] = pPlayerUnitNext;
  pPlayerUnitPrev = NULL;
  pPlayerUnitNext = NULL;

  if (wType == 0x21 || wType == 0x65) {
    reinterpret_cast<CLink *>(this)->Unlink(
        reinterpret_cast<CLink **>(&sgpScannerHead),
        reinterpret_cast<CLink **>(&sgpScannerTail));
  } else if (pSprite->flags.b & 0x20) {
    reinterpret_cast<CLink *>(this)->Unlink(
        reinterpret_cast<CLink **>(&sgpTransHead),
        reinterpret_cast<CLink **>(&sgpTransTail));
  } else {
    reinterpret_cast<CLink *>(this)->Unlink(
        reinterpret_cast<CLink **>(&sgpUsedHead),
        reinterpret_cast<CLink **>(&sgpUsedTail));
  }

  reinterpret_cast<CLink *>(this)->PrependThis(
      reinterpret_cast<CLink **>(&sgpDeadHead),
      reinterpret_cast<CLink **>(&sgpDeadTail));
}

// CUnit::RemoveDead -- oracle 0x4256B0, 57 B.
//
// The last step of a unit's death: it is already on the dead list with its orders draining,
// and this drops the live-unit count, finishes the drain, releases the sprite and moves the
// slot from dead to free. It is the only body that names sgpFreeHead/sgpFreeTail -- that
// pair has no accessor -- so it fixes their order (0x665AF0 in PrependThis's first slot,
// and swapping them is a byte delta), while which slot is the head comes from
// CLink::PrependThis(ppHead, ppTail) and the CSprite bodies.
//
// The counter decrement is the first instruction, before the frame exists, which fixes its
// place in the statement order. The pSprite clear landing between the two pushes of the
// Unlink call is MSVC scheduling an independent store: write it plainly before the call.
void CUnit::RemoveDead() {
  --sguiCount;
  RemoveAllOrders();
  pSprite = NULL;
  reinterpret_cast<CLink *>(this)->Unlink(
      reinterpret_cast<CLink **>(&sgpDeadHead),
      reinterpret_cast<CLink **>(&sgpDeadTail));
  // Hyperion: in a game by Hyperion's rules, the slot is cleared, as a load leaves
  // every free slot (read_save_units). 1.17 left the last unit's values in it, and a unit
  // made there later keeps some of them, which a game loaded from a save would not have
  // (revision 11, found on Big Game Hunters 3x3 with its detection mask)
  if (HxRules())
    memset(this, 0, sizeof(CUnit));
  reinterpret_cast<CLink *>(this)->PrependThis(
      reinterpret_cast<CLink **>(&sgpFreeHead),
      reinterpret_cast<CLink **>(&sgpFreeTail));
}

// cunit_init_findrange -- oracle 0x425610, 114 B.
//
// For every units.dat entry, raise its enemy-acquisition range to at least the max range of
// the weapons that actually fire for it, in tiles (>>5 = /32 px).
//
// The turret indirection is the point of the function: a unit with a subunit
// (guwAttachedUnit[i] != 228, the Siege Tank, Goliath and Terran turret pairs) does not
// carry its own weapons, so all three lookups are taken at the turret's index while the
// result is stored at the base unit's, which is the entry the enemy search queries. Two
// consequences are the oracle's and are kept: a base unit's own range is discarded whenever
// it has a turret, and the pass writes into the table it reads while walking i upward.
//
// The ternary must be (w != 228) ? w : i and the local a WORD: the other operand order
// gives the same 114 bytes with the arms swapped, an if statement if-converts to cmove at
// 113, and an unsigned int local loses the 16-bit compare (mov edi, 0E4h / cmp ax, di).
void cunit_init_findrange() {
  // Hyperion: and a mod's types, from 256
  for (unsigned int i = 0; UnitTypeValid(i); i = UnitTypeNext(i)) {
    WORD wSubunit = guwAttachedUnit[i];
    unsigned int nUnit = (wSubunit != 228) ? wSubunit : i;

    unsigned int nRange = gubUnitFindEnemy[nUnit];

    unsigned int nWeapon = gubUnitWeaponGnd[nUnit];
    if (WeaponTypeValid(nWeapon)) {  // Hyperion: < 130, and a mod's weapons
      unsigned int nWpnRange = wpn_maxrange_g[nWeapon] >> 5;
      if (nWpnRange > nRange)
        nRange = nWpnRange;
    }

    nWeapon = gubUnitWeaponAir[nUnit];
    if (WeaponTypeValid(nWeapon)) {
      unsigned int nWpnRange = wpn_maxrange_g[nWeapon] >> 5;
      if (nWpnRange > nRange)
        nRange = nWpnRange;
    }

    gubUnitFindEnemy[i] = (BYTE)nRange;
  }
}

void __fastcall voice_squelch();

// Releases the unit and order data tables at shutdown.
void __fastcall CUnit::FreeUnits() {
  voice_squelch();
  gamedata_free_data(gUnitsArrayDataTbl);
  gamedata_free_data(gORDERSArrayDataTbl_g);
}

// Creates a finished unit for a trigger or an AI script: the position is
// pulled inside the map, a building is snapped to the tile grid and refused
// when its footprint is blocked. The bounds sit in a rect16_t, left before
// right; the snap reads the halves from the table and the footprint test
// from WORD locals, so both are recomputed; the sub_426600 failure is last.
CUnit *trigger_create_unit(int nUnitType, int x, int y, int nPlayer) {
  char szBuf[256];
  if (!UnitTypeValid(static_cast<WORD>(nUnitType)))  // Hyperion: >= 228, and a mod's types
    return NULL;
  int nType = static_cast<WORD>(nUnitType);
  if (!(gfEditFlags[nType] & 2))
    return NULL;
  mrect_t  box;
  box.left = x - gUnitBoundBox[nType].left;
  box.right = gUnitBoundBox[nType].right + x;
  upos_t wMapW = gwMapPixWidth;
  if (box.left < 0)
    x -= box.left;
  else if (box.right >= wMapW)
    x += wMapW - box.right - 1;
  box.top = y - gUnitBoundBox[nType].top;
  box.bottom = gUnitBoundBox[nType].bottom + y;
  if (box.top < 0)
    y -= box.top;
  else if (box.bottom >= gwMapPixHeight)
    y += gwMapPixHeight - box.bottom - 1;
  if (gulUnitTypeFlags[nType] & 1) {
    int nHalfW = gUnitPlaceboxSize[nType].x / 2;
    x -= nHalfW;
    if (x < 0)
      x = 0;
    else if (x >= wMapW)
      x = wMapW - 1;
    x = (x & ~31) + nHalfW;
    int nHalfH = gUnitPlaceboxSize[nType].y / 2;
    y -= nHalfH;
    if (y < 0)
      y = 0;
    else if (y >= gwMapPixHeight)
      y = gwMapPixHeight - 1;
    y = (y & ~31) + nHalfH;
    WORD wH = gUnitPlaceboxSize[nType].y;
    WORD wW = gUnitPlaceboxSize[nType].x;
    if (mtx_query_place(nPlayer, x / 32 - static_cast<short>(wW) / 2 / 32,
                   y / 32 - static_cast<short>(wH) / 2 / 32, nUnitType, 0)) {
      psnprintf(szBuf, sizeof(szBuf), error_get_str(0x12),
                get_unit_name_from_type(nUnitType), x, y);
      map_msg_error(szBuf, 3);
      return NULL;
    }
  }
  CUnit *pUnit = CreateUnitAt(nType, x, y, nPlayer);
  if (pUnit == NULL) {
    CUnit::ShowCreateError(nPlayer);
    return NULL;
  }
  if (unit_type_spreads_creep(nUnitType, 1)) {
    CUnit::ZergPlaceBldg(nUnitType, PosS(pUnit->pSprite->wPosX),
                         PosS(pUnit->pSprite->wPosY));
    sub_4B4DD0(pUnit);
  } else if (gulUnitTypeFlags[nType] & 0x20000) {
    spread_creep_completely(nUnitType, PosS(pUnit->pSprite->wPosX),
                            PosS(pUnit->pSprite->wPosY));
    sub_4B4DD0(pUnit);
  }
  pUnit->FinishConstruction();
  if (pUnit->sub_426600()) {
    pUnit->Complete();
    if (unit_type_spreads_creep(nUnitType, 1))
      sub_4B4DD0(pUnit);
    if (gulUnitTypeFlags[pUnit->wType] & 2)
      pUnit->SetPlayer(0x0B, 0);
    if ((pUnit->dwFlags & 2) && pUnit->GetRace() == 1)
      pUnit->sub_437180();
    pUnit->AttachAiRecord();
    RequestJuiceUpdate();
    // Hyperion: 1.17's amounts at a mutator's share
    if (pUnit->wType == 0xBC || pUnit->IsRefinery()) {
      pUnit->wResourceAmount = MutatorResource(5000, true);
    } else if (pUnit->IsMineralSrc()) {
      pUnit->wResourceAmount = MutatorResource(1500, false);
      pUnit->UpdateResourceAnim();
    }
    return pUnit;
  }
  upos_t wY = pUnit->pSprite->wPosY;
  upos_t wX = pUnit->pSprite->wPosX;
  psnprintf(szBuf, sizeof(szBuf), error_get_str(0x12),
            get_unit_name_from_type(nUnitType), PosS(wX), PosS(wY));
  map_msg_error(szBuf, 3);
  pUnit->Suicide();
  return NULL;
}

// Always rejects: three stack arguments, none read.
BOOL __stdcall sub_4265D0(int nX, int nY, rect32_t *pBox) { return FALSE; }

// Energy cost of the tech an order uses, x256; zero for an order with no tech.
WORD get_order_tech_energy_cost(order_id_t bOrderId) {
  tech_id_t bTech = order_techType[bOrderId];
  if (TechTypeValid(bTech))  // Hyperion: and a mod's past 1.17's rows
    return techdata_energy_g[bTech] << 8;
  return 0;
}

// Copies units.dat flag 0x80000 into status bit 0x20000.
void CUnit::ApplyPrototypeFlags() {
  dwFlags = (dwFlags & ~0x20000) | ((gulUnitTypeFlags[wType] >> 10) & 0x20000);
}

// Recounts the live units as 1700 minus the length of the free list.
void sub_4266D0() {
  DWORD nCount = g_pools.units;
  sguiCount = nCount;
  CUnit *u = sgpFreeHead;
  if (u != NULL) {
    do {
      u = u->pNext;
      nCount--;
    } while (u != NULL);
    sguiCount = nCount;
  }
}

// Rebuilds the free list from a per-slot in-use byte map.
void sub_426700(BYTE *pUsed, int nCount) {
  sgpFreeHead = NULL;
  sgpFreeTail = NULL;
  CUnit *u = sgUnitsMem;
  for (unsigned int i = 0; i < g_pools.units; i++, u++) {
    if (pUsed[i] == 0)
      reinterpret_cast<CLink *>(u)->PrependThis(
          reinterpret_cast<CLink **>(&sgpFreeHead),
          reinterpret_cast<CLink **>(&sgpFreeTail));
  }
}

// TRUE when the unit already stands on its move target or faces it.
BOOL CUnit::IsFacingMoveTarget() {
  upos_t wX = PosU(wDestinationX);
  if (wX != wPositionX ||
      PosU(wDestinationY) != wPositionY) {
    BYTE bDir = bUnion21;
    upos_t wSpY = pSprite->wPosY;
    upos_t wSpX = pSprite->wPosX;
    if (bDir != AngleBetween(PosS(wSpX), PosS(wSpY),
                             PosS(wX),
                             PosS(wDestinationY)))
      return FALSE;
  }
  return TRUE;
}

// TRUE when a unit of this type placed at (x, y) fits inside the map.
BOOL sub_424D30(int nUnitType, int x, int y) {
  WORD wUnitType = static_cast<WORD>(nUnitType);
  if (x - gUnitBoundBox[wUnitType].left >= 0 &&
      gUnitBoundBox[wUnitType].right + x < gwMapPixWidth &&
      y - gUnitBoundBox[wUnitType].top >= 0 &&
      gUnitBoundBox[wUnitType].bottom + y < gwMapPixHeight)
    return TRUE;
  return FALSE;
}

// Empties a dying transport: kills its cargo, or unloads a bunker's.
void CUnit::sub_425D10() {
  if (dwFlags & 0x40) {
    AbandonTransport(1);
    return;
  }
  if (IsATransport()) {
    for (int i = 0; i < 8; i++) {
      CUnit *pCargo = GetTransportUnit(i);
      if (pCargo != NULL && !pCargo->IsDeadOrDying()) {
        if (wType != 0x7D || !pCargo->ExitTransport())
          pCargo->Suicide();
      }
    }
    memset(wCargoUnitIds, 0, sizeof(wCargoUnitIds));
  }
}

// Takes a slot off the free list and initialises it; NULL with
// guwCreateUnitErr set when the table is full or the unit would leave the map.
CUnit *sub_424860(int nUnitType, int x, int y, int nPlayer, BYTE bRand) {
  if (sguiCount == g_pools.units) {
    guwCreateUnitErr = 0x3D;
    return NULL;
  }
  BOOL bFits = sub_424D30(nUnitType, x, y);
  if (!bFits) {
    guwCreateUnitErr = bFits;
    return NULL;
  }
  CUnit *pUnit = sgpFreeHead;
  if (pUnit != NULL && pUnit->sub_425710(nUnitType, x, y, nPlayer, bRand)) {
    sguiCount++;
    reinterpret_cast<CLink *>(pUnit)->Unlink(
        reinterpret_cast<CLink **>(&sgpFreeHead),
        reinterpret_cast<CLink **>(&sgpFreeTail));
    return pUnit;
  }
  guwCreateUnitErr = 0x3E;
  return NULL;
}

// Creates a unit and, for a type with a turret, its subunit.
CUnit *__fastcall CreateUnitAt(int nUnitType, int x, int y, int nPlayer) {
  shield_offset_t off;
  BYTE bRand = static_cast<BYTE>(net_rand(14));
  CUnit *pUnit = sub_424860(nUnitType, x, y, nPlayer, bRand);
  if (pUnit == NULL)
    return NULL;
  if (pUnit->dwFlags & 2)
    pUnit->MoveToUsedList();
  else
    pUnit->MoveToTransList();
  // Hyperion: and a mod's unit, whose subunit is its record's
  if (static_cast<unsigned int>(nUnitType) <= 0x69 || UnitTypeMod(nUnitType)) {
    WORD wSubType = guwAttachedUnit[nUnitType];
    if (wSubType != 228) {
      CUnit *pSub = sub_424860(wSubType, x, y, nPlayer, bRand);
      if (pSub == NULL) {
        pUnit->Destroy();
        return NULL;
      }
      pUnit->subunit = pSub;
      pSub->subunit = pUnit;
      pUnit->pSprite->pMainImage->GetOverlayOffset(&off, 2, 0);
      pSub->pSprite->pMainImage->SetOffset(static_cast<char>(off.x),
                                           static_cast<char>(off.y));
      return pUnit;
    }
  }
  pUnit->subunit = NULL;
  return pUnit;
}

// Undoes a completed building's pending work before it is destroyed.
void CUnit::sub_4251C0() {
  if (dwFlags & 2) {
    if (IsConstructingAddon())
      CancelAddon();
    if (pAddon != NULL)
      DetachAddon();
    if (bTechType != 0x2C)
      RefundResearch();
    if (bUpgradeType != 0x3D)
      RefundUpgrade();
    if (UnitTypeQueueable(GetBuildUnit(0)))  // Hyperion: <= 0x69
      RemoveAllBuildQueue();
    if (placebox_is_on())
      sub_47D530();
    if (GetRace() == 0) {
      if (IsZergBldgUpgrading())
        dwFlags |= 1;
      if (!sub_4B47F0(this))
        dwFlags |= 0x4000;
    }
  }
}

// Places a hidden unit (and a turret-less subunit) on the map; FALSE with the
// create error shown when there is no room.
BOOL CUnit::sub_426600() {
  if (pSprite->flags.b & 0x20) {
    mpoint_t  in;
    mpoint_t  out;  // Hyperion: a map point
    in.x = pSprite->wPosX;
    in.y = pSprite->wPosY;
    if (!::FindPlaceForUnit(this, &in, &out, NULL, 0, 0)) {
      ShowCreateError(bPlayer);
      return FALSE;
    }
    SetUnitPosition(out.x, out.y);
    if (subunit != NULL && !(gulUnitTypeFlags[wType] & 0x10)) {
      count_add_completion(subunit, 0);
      subunit->dwFlags |= 1;
      subunit->lHitPoints = HandicapLife(gxUnitLife[subunit->wType], subunit->bPlayer);  // Hyperion: at the owner's handicap
      subunit->SetUnitPosition(out.x, out.y);
    }
  }
  return TRUE;
}

// Tops up a finished unit's hit points and shields and sets its buttons,
// graphic and facing.
void CUnit::FinishConstruction() {
  if (wRemainingBuildTime != 0) {
    // Hyperion: at the owner's handicap
    lHitPoints = HandicapLife(gxUnitLife[wType], bPlayer);
    lShields = HandicapShields(guwUnitEnergy[wType], bPlayer) << 8;
    wRemainingBuildTime = 0;
  }
  SetButtonset(wType);
  if (dwFlags & 2) {
    // +0x121 and +0x123 are cleared as two elements of one byte run; a named
    // store to bUnion123 schedules the argument push after the stores.
    BYTE *pbTimers = _unmapped_121;
    pbTimers[0] = 0;
    pbTimers[2] = 0;
    UnitHxClear(this);  // Hyperion: the casters past 7 too (stage D)
    SyncConstructionGraphic(0);
    pSprite->SetAnimation(0x10, 1);
    if (gPlayback == 0 && bPlayer == g_LocalNationID)
      status_set_update();
  } else {
    if (dwFlags & 0x10000) {
      BYTE bDir = gubUnitFaceAngle[wType];
      if (bDir == 0x20)
        bDir = net_rand(0x24) & 0x1F;
      reinterpret_cast<CFlingy *>(this)->FaceDirection(bDir * 8);
    }
    if (IsDoodad())
      Show();
  }
}

// Builds a turret's graphic over its base unit.
void CUnit::sub_424A00(WORD nArg) {
  BYTE bRleFrame = 0;
  BYTE bRleWait = 0;
  DWORD dwRle = dwFlags >> 8;
  BYTE bRle = dwRle & 3;
  if (dwRle & 3)
    pSprite->GetRLEInfo(&bRleFrame, &bRleWait);
  gpImageUnit = this;
  sub_426750(nArg);
  gpImageUnit = subunit;
  pSprite->flags.b |= 0x10;
  if (bRle) {
    pSprite->sub_415E50(bRle, bRleFrame, bRleWait);
    if (subunit->dwUnionE4 & gdwLocalMask)
      pSprite->Show();
  }
  shield_offset_t off;
  subunit->pSprite->pMainImage->GetOverlayOffset(&off, 2, 0);
  pSprite->pMainImage->SetOffset(static_cast<char>(off.x),
                                 static_cast<char>(off.y));
  pSprite->SetMask(subunit->pSprite->bVisibilityFlags);
  VisionHxSpriteSet(pSprite, VisionHxSprite(subunit->pSprite));  // Hyperion
}

// Reads nCount saved unit records in the older 0x144-byte layout: an id, the
// first 0x124 bytes of the unit, then its last 0x1C bytes.
// The cursor is declared right after the allocation, and the loop counts up
// with `i < nCount`; that keeps the oracle's second nCount guard.
BOOL sub_426000(void *pFile, DWORD nCount) {
  if (nCount != 0) {
    int nSize = nCount * 0x144;
    BYTE *pBuf = static_cast<BYTE *>(SMemAlloc(nSize, "CUnitInit.cpp", 0x7B5, 0));
    BYTE *p = pBuf;
    memset(pBuf, 0, nSize);
    if (!decompress(pBuf, nSize, pFile)) {
      SMemFree(pBuf, "CUnitInit.cpp", 0x7BB, 0);
      return FALSE;
    }
    for (DWORD i = 0; i < nCount; i++) {
      DWORD dwId = *reinterpret_cast<DWORD *>(p);
      p += 4;
      // Hyperion: a slot past the game's pool is a broken save
      if (dwId >= static_cast<DWORD>(g_pools.units)) {
        SMemFree(pBuf, "CUnitInit.cpp", 0, 0);
        return FALSE;
      }
      CUnit *pUnit = &sgUnitsMem[dwId];
      // Hyperion: the record is 1.17's without +0x124..+0x133; it is put
      // together as a 1.17 record, 0x150 bytes, and translated.
      BYTE ab117[0x150] = {0};
      memcpy(ab117, p, 0x124);
      memcpy(ab117 + 0x134, p + 0x124, 0x1C);
      p += 0x140;
      PortSave117Get(SAVE117_CUnit, ab117, pUnit);
      pUnit->sub_41BF70(1);
      if (gulUnitTypeFlags[pUnit->wType] & 1) {
        if (pUnit->bTechType == 0x18)
          pUnit->bTechType = 0x2C;
        if (pUnit->bUpgradeType == 0x2E)
          pUnit->bUpgradeType = 0x3D;
      }
    }
    SMemFree(pBuf, "CUnitInit.cpp", 0x7E1, 0);
  }
  return TRUE;
}

// Reads nCount saved unit records (id + 0x150-byte body) back into the table.
// Same shape as sub_426000: the cursor right after the allocation, `i < nCount`.
BOOL sub_426130(void *pFile, DWORD nCount) {
  if (nCount != 0) {
    int nSize = nCount * (4 + PortSave117Size(SAVE117_CUnit));  // Hyperion: 0x154
    BYTE *pBuf = static_cast<BYTE *>(SMemAlloc(nSize, "CUnitInit.cpp", 0x7EE, 0));
    BYTE *p = pBuf;
    memset(pBuf, 0, nSize);
    if (!decompress(pBuf, nSize, pFile)) {
      SMemFree(pBuf, "CUnitInit.cpp", 0x7F4, 0);
      return FALSE;
    }
    for (DWORD i = 0; i < nCount; i++) {
      DWORD dwId = *reinterpret_cast<DWORD *>(p);
      p += 4;
      // Hyperion: a slot past the game's pool is a broken save
      if (dwId >= static_cast<DWORD>(g_pools.units)) {
        SMemFree(pBuf, "CUnitInit.cpp", 0, 0);
        return FALSE;
      }
      CUnit *pUnit = &sgUnitsMem[dwId];
      PortSave117Get(SAVE117_CUnit, p, pUnit);
      pUnit->sub_41BF70(1);
      p += PortSave117Size(SAVE117_CUnit);
    }
    SMemFree(pBuf, "CUnitInit.cpp", 0x807, 0);
  }
  return TRUE;
}

// Drops this unit from the current selection.
void CUnit::RemoveFromStatusBar() {
  if (pSprite->flags.b & 8) {
    int nIndex = pSprite->bSelectionIndex;
    int nCount;
    for (nCount = 0; nCount < SelectionMax(); nCount++) {
      if (NewUnits()[nCount] == NULL)
        break;
    }
    CUnit *units[SELECTION_WIDE];
    memcpy(units, NewUnits(), nCount * sizeof(CUnit *));
    nCount--;
    memmove(&units[nIndex], &units[nIndex + 1],
            (nCount - nIndex) * sizeof(CUnit *));
    units[nCount] = NULL;
    select_units(nCount, units, 0, 0);
    if (nCount == 0)
      unit_gui_reset();
  }
}

#include "../port/data/port_moddata.h"  // Hyperion: ModDataForGame

// Loads orders.dat and units.dat and builds an empty unit pool.
void CUnit::InitUnits() {
  repulse_init_directions();
  sgdwOrderSeqDelay_g = 10;
  sgdwBackgroundOrderSeqDelay_g = 150;
  load_array_data("arr\\orders.dat", gORDERSArrayDataTbl_g);
  load_array_data("arr\\units.dat", gUnitsArrayDataTbl);
  ModDataForGame();  // Hyperion: a mod's types, in a game that has them
  cunit_init_findrange();
  memset(sgUnitsMem, 0, sizeof(CUnit) * g_pools.units);
  sguiCount = 0;
  PlayerClear(sgpPlayerLists_g);  // Hyperion: and Hyperion's slots
  sgpUsedTail = NULL;
  sgpUsedHead = NULL;
  sgpScannerTail = NULL;
  sgpScannerHead = NULL;
  sgpDeadTail = NULL;
  sgpDeadHead = NULL;
  sgpTransTail = NULL;
  sgpTransHead = NULL;
  sgpFreeTail = NULL;
  sgpFreeHead = NULL;
  CUnit *pUnit = sgUnitsMem;
  for (int i = g_pools.units; i != 0; i--, pUnit++)
    reinterpret_cast<CLink *>(pUnit)->PrependThis(
        reinterpret_cast<CLink **>(&sgpFreeHead),
        reinterpret_cast<CLink **>(&sgpFreeTail));
  cunit_bounds_init();
  guwCreateUnitErr = 0;
}

// Turns this unit into another type in place, keeping its position legal.
// The sprite y is read into a WORD and widened into a DWORD after the x store;
// that DWORD is what the oracle keeps in the dead parameter slot.
void CUnit::TransformType(WORD nUnitType) {
  if (!(pSprite->flags.b & 0x20))
    UntrackCollision();
  sub_424BD0(nUnitType);
  if ((gulUnitTypeFlags[wType] & 1) && (dwFlags & 0x300)) {
    SetBackgroundOrder(0x17);
    SetCloakStatus(0);
  }
  if ((pSprite->flags.b & 8) && !IsMultiSelectable() && byte_6AB4C0 > 1 &&
      nUnitType != 0x24) {
    sub_4A7B80();
    RemoveFromStatusBar();
  }
  status_set_update();
  mpoint_t  pos;
  upos_t wX = pSprite->wPosX;
  upos_t tY = pSprite->wPosY;
  pos.x = wX;
  DWORD dwY = tY;
  pos.y = static_cast<short>(dwY);
  WORD wY = static_cast<WORD>(dwY);
  ClipToMap(&pos, wType);
  if (wX != PosU(pos.x) || wY != PosU(pos.y))
    SetUnitPosition(pos.x, pos.y);
  if (!(pSprite->flags.b & 0x20))
    TrackCollision();
  if (subunit != NULL)
    subunit->sub_424A00(guwAttachedUnit[nUnitType]);
  SyncAllStatusEffects();
}

// Turns a finished refinery back into a geyser; for a subunit, just moves it
// to the dead list. FALSE for anything else.
BOOL CUnit::sub_425270() {
  WORD nType = wType;
  if (gulUnitTypeFlags[nType] & 0x10) {
    reinterpret_cast<CLink *>(this)->PrependThis(
        reinterpret_cast<CLink **>(&sgpDeadHead),
        reinterpret_cast<CLink **>(&sgpDeadTail));
    if (pPlayerUnitPrev != NULL)
      pPlayerUnitPrev->pPlayerUnitNext = pPlayerUnitNext;
    if (pPlayerUnitNext != NULL)
      pPlayerUnitNext->pPlayerUnitPrev = pPlayerUnitPrev;
    if (this == sgpPlayerLists_g[bPlayer])
      sgpPlayerLists_g[bPlayer] = pPlayerUnitNext;
    pPlayerUnitPrev = NULL;
    pPlayerUnitNext = NULL;
    count_remove_existence(this);
    subunit = NULL;
    return TRUE;
  }
  if (!(pSprite->flags.b & 0x20)) {
    // The call re-reads wType rather than passing nType; with nType the
    // compiler folds the case constant into ECX.
    switch (nType) {
    case 0x95:
      remove_creep_completely(wType, PosS(pSprite->wPosX),
                              PosS(pSprite->wPosY),
                              sub_4265D0);
    case 0x6E:
    case 0x9D:
      if (dwFlags & 1)
        count_remove_completion(this, 0);
      dwFlags &= ~1;
      TransformType(0xBC);
      reinterpret_cast<CTarget *>(this)->SetTargetOrder(gubUnitOrderInitialize[wType]);
      lHitPoints = HandicapLife(gxUnitLife[wType], bPlayer);  // Hyperion: at the owner's handicap
      SetPlayer(0x0B, 0);
      SetOwnerColor(0x0B);
      dwFlags |= 1;
      count_add_completion(this, 1);
      if (placebox_is_on())
        placebox_show_geyser(this);
      return TRUE;
    }
  }
  return FALSE;
}

// Per-type teardown before a unit dies: hangars, nuke painters, resource
// holders, silos, nydus pairs and pylons each release what they own.
void CUnit::sub_4253A0() {
  WORD nType = wType;
  if (nType == 0x48 || nType == 0x52 || nType == 0x53 || nType == 0x51) {
    HangarSelfDestruct();
    return;
  }
  if (nType == 0x01 || nType == 0x10 || nType == 0x64 || nType == 0x63 ||
      nType == 0x68) {
    CUnit *pNuke = pUnionD0;
    if (pNuke != NULL) {
      pNuke->pSprite->SetAnimation(1, 1);
      pUnionD0 = NULL;
    }
    return;
  }
  if (IsMineralSrc() || IsRefinery())
    sub_423400();
  switch (wType) {
  case 0x49:
  case 0x55:
    if (dwFlags & 1)
      sub_41CFE0();
    break;
  case 0x6C:
    if (pUnionD0 != NULL) {
      pUnionD0->Suicide();
      pUnionD0 = NULL;
    }
    break;
  case 0x0E: {
    CUnit *pSilo = pUnion80;
    if (pSilo != NULL && pSilo->wType == 0x6C) {
      pSilo->pUnionD0 = NULL;
      pUnion80->pUnionD4 = NULL;
    }
    break;
  }
  case 0x9C:
    if (pPowerTemplate != NULL) {
      pPowerTemplate->Kill();
      pPowerTemplate = NULL;
    }
    RemoveFromPsiLink();
    RequestJuiceUpdate();
    break;
  case 0x86: {
    CUnit *pExit = pUnionD0;
    if (pExit != NULL) {
      pUnionD0 = NULL;
      pExit->pUnionD0 = NULL;
      pExit->Suicide();
    }
    break;
  }
  }
}

void __fastcall voice_squelch(CUnit *pUnit);

// Tears a dying unit out of every system that references it.
void CUnit::DestroyImpl() {
  ai_mark_targets_stale_on_death(this);
  sub_4253A0();
  voice_squelch(this);
  sub_425D10();
  RemoveTargetReferences(TRUE);
  CBullet::RemoveTargetReferences(this);
  SAI_RemoveUnit(this, 1);
  if (sub_425270())
    return;
  Stop();
  if (!(pSprite->flags.b & 0x20))
    UntrackCollision();
  if (!(gulUnitTypeFlags[wType] & 1)) {
    bCloakCount = 0;
    sub_434DA0();
    if (dwFlags & 0x100)
      sub_41D360(0x112);
  }
  sub_4251C0();
  if ((gulUnitTypeFlags[wType] & 0x20) && bBuildingPlaced != 0)
    UnplaceBldg(wOrderTargetX, wOrderTargetY);
  if (pPath != NULL)
    sai_path_kill(this);
  CUnit *pAddon = reinterpret_cast<CUnit *>(dwUnionEC);
  if (pAddon != NULL && (bBackgroundOrderId == 0x26 || bBackgroundOrderId == 0x3F))
    pAddon->Suicide();
  SetBackgroundOrder(0x17);
  DropInventory();
  sub_49C8B0(this);
  RemoveFromStatusBar();
  pSprite->RemoveCircle();
  count_remove_existence(this);
  if (dwFlags & 1)
    count_remove_completion(this, 0);
  if (!gPlayback && bPlayer == g_LocalNationID && placebox_is_on() &&
      !sub_47DB60())
    sub_47D290(1, 0);
  sub_426530();
  status_set_update();
}

// Marks a unit complete: tallies, list membership, visibility and its idle
// order.
void CUnit::Complete() {
  if (gulUnitTypeFlags[wType] & 0x400) {
    count_remove_existence(this);
    dwFlags |= 1;
    count_add_existence(this);
  } else {
    dwFlags |= 1;
  }
  count_add_completion(this, 1);
  if (gulUnitTypeFlags[wType] & 0x400000)
    sub_41D200(0x111);
  int nType = wType;
  if (nType == 0x21 || nType == 0x65) {
    reinterpret_cast<CLink *>(this)->Unlink(
        reinterpret_cast<CLink **>(&sgpTransHead),
        reinterpret_cast<CLink **>(&sgpTransTail));
    reinterpret_cast<CLink *>(this)->PrependThis(
        reinterpret_cast<CLink **>(&sgpScannerHead),
        reinterpret_cast<CLink **>(&sgpScannerTail));
    sub_423260();
  } else if ((pSprite->flags.b & 0x20) && nType != 0x49 && nType != 0x55) {
    Show();
  }
  if (IsTrap()) {
    SetCloakStatus(3);
    dwUnionE4 = 0x80000000;
    bUnion91 = 0;
  }
  BYTE bPlayerType = gPlayerData[bPlayer].nType;
  if (bPlayerType == 3)
    SetOrder(0xA1);
  else if (bPlayerType == 7)
    SetOrder(0xA2);
  else if (bPlayerType == 1)
    SetOrder(gubUnitOrderAI[wType]);
  else
    SetOrder(gubUnitOrderInitialize[wType]);
  if (gulUnitTypeFlags[wType] & 0x800) {
    wPowerupOriginX = pSprite->wPosX;
    wPowerupOriginY = pSprite->wPosY;
    SetOrder(gubUnitOrderInitialize[wType]);
  }
  tai_assign_unit_strength(this);
}

// Clears every reference this unit (and its subunit, and its queued orders)
// holds to pUnit.
void CUnit::RemoveUnitReferences(CUnit *pUnit) {
  if (dwUnionEC == reinterpret_cast<DWORD_PTR>(pUnit))
    dwUnionEC = 0;
  if (pOrderTarget == pUnit)
    pOrderTarget = NULL;
  if (*reinterpret_cast<CUnit **>(_unmapped_14) == pUnit)
    *reinterpret_cast<CUnit **>(_unmapped_14) = NULL;
  if (dwUnion7C == reinterpret_cast<DWORD_PTR>(pUnit))
    dwUnion7C = 0;
  if (pUnion80 == pUnit)
    pUnion80 = NULL;
  if (pIrradiateSource == pUnit)
    pIrradiateSource = NULL;
  if (gulUnitTypeFlags[wType] & 8) {
    if (pUnionC8_SavedOrderTarget == pUnit)
      pUnionC8_SavedOrderTarget = NULL;
    if (bUnionCE != 0 && pUnionD0 == pUnit) {
      bUnionCE = 0;
      pUnionD0 = NULL;
    }
  } else if (dwFlags & 2) {
    if (pUnionC0 == pUnit)
      pUnionC0 = NULL;
  }
  if (IsRallyer() && pRallyUnit == pUnit) {
    pRallyUnit = NULL;
    wRallyX = pUnit->pSprite->wPosX;
    wRallyY = pUnit->pSprite->wPosY;
  }
  if (subunit != NULL && (gulUnitTypeFlags[subunit->wType] & 0x10))
    subunit->RemoveUnitReferences(pUnit);
  COrder *pNext;
  for (COrder *pOrder = pOrderQueueHead; pOrder != NULL; pOrder = pNext) {
    order_target_t target = pOrder->target;
    pNext = pOrder->pNext;
    if (target.pUnit == pUnit)
      RemoveQueuedOrder(pOrder);
  }
}

// Morphs this unit into nUnitType in place, keeping its order, owner, sprite
// position and hit-point ratio.
void CUnit::sub_426750(WORD nUnitType) {
  order_id_t bOrder = bOrderId;
  int nLife = lHitPoints >> 8;
  BYTE bState = bOrderState;
  mpoint_t ptTargetPos = orderTargetPos;  // Hyperion: the point, not its DWORD (stage E)
  CUnit *pTarget = pOrderTarget;
  wPrevUnitType = wType;
  wPrevHP = static_cast<WORD>(nLife);
  if (static_cast<WORD>(nLife) == 0)
    wPrevHP = 1;
  CSprite *pSpr = pSprite;
  BYTE bCreator = pSpr->bCreator;
  int x = PosS(pSpr->wPosX);
  int y = PosS(pSpr->wPosY);
  if (pSpr->pImageHead != NULL) {
    pSpr->Kill();
    pSprite = NULL;
  }
  if (pPath != NULL)
    sai_path_kill(this);
  sub_425A70(nUnitType, x, y, bPlayer);
  dwFlags &= ~0x400;
  // Hyperion: both maximums at the owner's handicap
  lHitPoints = (HandicapLife(gxUnitLife[wType], bPlayer) >> 8) * nLife;
  int nOldMax = HandicapLife(gxUnitLife[wPrevUnitType], bPlayer) >> 8;
  int nNewLife;
  if (nOldMax != 0)
    nNewLife = lHitPoints / nOldMax;
  else
    nNewLife = 1;
  nNewLife <<= 8;
  if (nNewLife == 0)
    nNewLife = 1;
  lHitPoints = nNewLife;
  tai_assign_unit_strength(this);
  dwFlags = (dwFlags & ~0x3000) | (((order_canBeInterrupted[bOrder] == 0) & 1) << 12);
  pSprite->SetCreator(bCreator);
  if (dwFlags & 2)
    wBuildQueue[bBuildQueueSlot] = 0xE4;
  bOrderId = bOrder;
  bOrderState = bState;
  orderTargetPos = ptTargetPos;
  pOrderTarget = pTarget;
  SetButtonset((dwFlags & 1) ? nUnitType : 0xE4);
  status_set_update();
}

// Morphs this unit and rebuilds its selection circle, team circles, cloak
// effect and visibility around the change.
// The sprite flags are a BYTE local: a DWORD one is spilled and repacks the frame.
// bSelectFlag is declared before nTeamCircles so its zero is an immediate store.
void CUnit::sub_424BD0(WORD nUnitType) {
  BYTE bSelIndex = 0;
  BOOL bSelectFlag = FALSE;
  int nTeamCircles = 0;
  CSprite *pSpr = pSprite;
  BYTE bSprFlags = pSpr->flags.b;
  BYTE bVisibility = pSpr->bVisibilityFlags;
  const PlayerSet qVisibility = VisionHxSprite(pSpr);  // Hyperion: the slots past 7
  BOOL bSelected = (bSprFlags >> 3) & 1;
  BYTE bFlash = pSpr->bFlashCounter;
  // Tested after the dereferences above, through a fresh read of pSprite.
  if (pSprite != NULL) {
    bSelectFlag = bSprFlags & 1;
    nTeamCircles = (bSprFlags >> 1) & 3;
  }
  if (bSelected) {
    bSelIndex = pSpr->bSelectionIndex;
    sub_423000();
  }
  BYTE bCircle = pSprite->RemoveCircle();
  BYTE bTeamCircle = pSprite->RemoveTeamCircleIf();
  BYTE bRleFrame = 0;
  BYTE bRleWait = 0;
  DWORD dwRle = dwFlags >> 8;
  BYTE bRle = dwRle & 3;
  if (dwRle & 3)
    pSprite->GetRLEInfo(&bRleFrame, &bRleWait);
  count_remove_existence(this);
  if (dwFlags & 1)
    count_remove_completion(this, 1);
  sub_426750(nUnitType);
  count_add_existence(this);
  if (dwFlags & 1)
    count_add_completion(this, 1);
  if (bRle) {
    pSprite->sub_415E50(bRle, bRleFrame, bRleWait);
    if (dwUnionE4 & gdwLocalMask)
      pSprite->Show();
  }
  if (bSelected)
    sub_423060(bSelIndex);
  else if (bSelectFlag)
    pSprite->AllocSelect(bCircle);
  for (; nTeamCircles != 0; nTeamCircles--)
    pSprite->AllocCircle(bTeamCircle);
  pSprite->bFlashCounter = bFlash;
  pSprite->SetMask(bVisibility);
  VisionHxSpriteSet(pSprite, qVisibility);  // Hyperion
}

// Clears every reference to this unit held by the units of each player whose
// bit is set in dwRemoved.
void CUnit::RemoveTargetReferencesForPlayers(DWORD dwRemoved) {
  DWORD dwBit = 1;
  dwRemoved &= 0xFFF;
  BYTE bPlayerIdx = 0;
  while (dwRemoved != 0) {
    if (dwRemoved & dwBit) {
      dwRemoved &= ~dwBit;
      for (CUnit *pUnit = sgpPlayerLists_g[bPlayerIdx]; pUnit != NULL;
           pUnit = pUnit->pPlayerUnitNext)
        pUnit->RemoveUnitReferences(this);
    }
    dwBit += dwBit;
    bPlayerIdx++;
  }
}

// Hyperion: the same for a game with Hyperion's slots, over every slot; with 1.17's
// 0xFFF the units of the slots past 11 kept a cloaked unit as their target once they lost
// sight of it, and a DWORD holds slots 0 to 31 (stage D).
void CUnit::RemoveTargetReferencesForPlayers(const PlayerSet &qRemoved) {
  for (int nSlot = 0; nSlot < PlayerSlots(); nSlot++) {
    if (!qRemoved.Has(nSlot))
      continue;
    for (CUnit *pUnit = sgpPlayerLists_g[nSlot]; pUnit != NULL; pUnit = pUnit->pPlayerUnitNext)
      pUnit->RemoveUnitReferences(this);
  }
}

// Hands this unit (and its turret) to nPlayer, moving it between the
// per-player lists and tallies.
void CUnit::SetPlayer(int nPlayer, int nFlag) {
  if (static_cast<BYTE>(nPlayer) == bPlayer)
    return;
  SAI_RemoveUnit(this, 0);
  BYTE bUpgrading = 0;
  if (IsZergBldgUpgrading()) {
    dwFlags |= 1;
    bUpgrading = 1;
  }
  count_remove_existence(this);
  if (dwFlags & 1)
    count_remove_completion(this, 0);
  BYTE bSelIndex = 0;
  DWORD dwSelected = (pSprite->flags.b >> 3) & 1;
  if (dwSelected) {
    bSelIndex = pSprite->bSelectionIndex;
    sub_423000();
  }
  if (pPlayerUnitPrev != NULL)
    pPlayerUnitPrev->pPlayerUnitNext = pPlayerUnitNext;
  if (pPlayerUnitNext != NULL)
    pPlayerUnitNext->pPlayerUnitPrev = pPlayerUnitPrev;
  if (this == sgpPlayerLists_g[bPlayer])
    sgpPlayerLists_g[bPlayer] = pPlayerUnitNext;
  pPlayerUnitPrev = NULL;
  pPlayerUnitNext = NULL;
  const int nFrom = bPlayer;  // Hyperion: the owner's handicap before
  bPlayer = static_cast<BYTE>(nPlayer);
  HandicapOwnerChanged(this, nFrom);  // Hyperion: life and shields keep their share
  if (dwSelected)
    sub_423060(bSelIndex);
  pPlayerUnitPrev = NULL;
  pPlayerUnitNext = sgpPlayerLists_g[static_cast<BYTE>(nPlayer)];
  if (sgpPlayerLists_g[static_cast<BYTE>(nPlayer)] != NULL)
    sgpPlayerLists_g[static_cast<BYTE>(nPlayer)]->pPlayerUnitPrev = this;
  sgpPlayerLists_g[static_cast<BYTE>(nPlayer)] = this;
  count_add_existence(this);
  if (dwFlags & 1)
    count_add_completion(this, nFlag);
  if (bUpgrading)
    dwFlags &= ~1;
  CancelAlliedTargets();
  status_set_update();
  if (GetRace() == 2 && (dwFlags & 2))
    RequestJuiceUpdate();
  AttachAiRecord();
  if (subunit != NULL && (gulUnitTypeFlags[subunit->wType] & 0x10))
    subunit->SetPlayer(nPlayer, nFlag);
  ApplyUpgrades();
  if (gulUnitTypeFlags[wType] & 8) {
    CUnit *pPowerup = this->pPowerup;
    if (pPowerup != NULL && pPowerup->wType == 0xD7 &&
        (static_cast<BYTE>(nPlayer) == pPowerup->bPlayer ||
         !PlayerIsLobby(static_cast<BYTE>(nPlayer))))  // Hyperion: >= 8 in 1.17
      DropInventory();
  }
}

// Clears every live unit's references to this one; with bAll also the units
// inside transports and a harvester's resource link.
void CUnit::RemoveTargetReferences(BOOL bAll) {
  for (CUnit *pUnit = sgpUsedHead; pUnit != NULL; pUnit = pUnit->pNext)
    pUnit->RemoveUnitReferences(this);
  if (!bAll) {
    if (pSprite->flags.b & 0x20)
      statport_portrait_hide(reinterpret_cast<intptr_t>(this));
    return;
  }
  statport_portrait_hide(reinterpret_cast<intptr_t>(this));
  for (CUnit *pUnit = sgpTransHead; pUnit != NULL; pUnit = pUnit->pNext)
    pUnit->RemoveUnitReferences(this);
  if ((gulUnitTypeFlags[wType] & 8) && pUnionD0 != NULL)
    pUnionD0->sub_424170(this);
}

// Loads the unit table and every unit list from a save file; wVersion 2 and
// below use the older 0x144-byte records.
BOOL read_save_units(void *pFile, WORD wVersion) {
  std::vector<BYTE> used(g_pools.units);  // Hyperion: on the stack in 1.17
  DWORD nCount;
  if (!decompress(used.data(), g_pools.units, pFile))
    return FALSE;
  if (!pfread(&nCount, sizeof(nCount), pFile))
    return FALSE;
  memset(sgUnitsMem, 0, sizeof(CUnit) * g_pools.units);
  if (wVersion <= 2) {
    if (!sub_426000(pFile, nCount))
      return FALSE;
  } else {
    if (!sub_426130(pFile, nCount))
      return FALSE;
  }
  saveload_unitlist(0, used.data(), g_pools.units);
  if (!LoadCloakList(pFile))
    return FALSE;
  if (!PortSaveReadIds(pFile, &sgpUsedHead, 1))
    return FALSE;
  sgpUsedHead = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(sgpUsedHead));
  if (!PortSaveReadIds(pFile, &sgpTransHead, 1))
    return FALSE;
  sgpTransHead = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(sgpTransHead));
  if (!PortSaveReadIds(pFile, &sgpDeadHead, 1))
    return FALSE;
  sgpDeadHead = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(sgpDeadHead));
  if (!PortSaveReadIds(pFile, &sgpScannerHead, 1))
    return FALSE;
  sgpScannerHead = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(sgpScannerHead));
  if (!PortSaveReadIds(pFile, &sgpUsedTail, 1))
    return FALSE;
  sgpUsedTail = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(sgpUsedTail));
  if (!PortSaveReadIds(pFile, &sgpTransTail, 1))
    return FALSE;
  sgpTransTail = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(sgpTransTail));
  if (!PortSaveReadIds(pFile, &sgpDeadTail, 1))
    return FALSE;
  sgpDeadTail = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(sgpDeadTail));
  if (!PortSaveReadIds(pFile, &sgpScannerTail, 1))
    return FALSE;
  sgpScannerTail = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(sgpScannerTail));
  sub_426700(used.data(), g_pools.units);
  for (int i = 0; i < PlayerSlots(); i++) {  // Hyperion: every slot of a Hyperion game
    if (!PortSaveReadIds(pFile, &sgpPlayerLists_g[i], 1))
      return FALSE;
    sgpPlayerLists_g[i] = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(sgpPlayerLists_g[i]));
  }
  return TRUE;
}

// Initialises a free unit slot as a new unit of nUnitType owned by nPlayer.
BOOL CUnit::sub_425710(int nUnitType, int x, int y, int nPlayer, BYTE bRand) {
  memset(&pUnionD0, 0, offsetof(CUnit, dwFlags) - offsetof(CUnit, pUnionD0));  // Hyperion: the same fields, 0x0C bytes in 1.17
  memset(&pPsiLinkPrev, 0, offsetof(CUnit, pPath) - offsetof(CUnit, pPsiLinkPrev));  // Hyperion: the same fields, 8 bytes in 1.17
  pBurrowLinkPrev = NULL;
  pBurrowLinkNext = NULL;
  memset(&pUnion74, 0, offsetof(CUnit, _unmapped_90) - offsetof(CUnit, pUnion74));  // Hyperion: the same fields, 0x1C bytes in 1.17
  memset(&wTimeToLive, 0, offsetof(CUnit, _unmapped_130) - offsetof(CUnit, wTimeToLive));  // Hyperion: the same fields, 0x20 bytes in 1.17
  UnitHxClear(this);  // Hyperion: and the casters past 7
  MutatorUnitMade(static_cast<int>(this - CUnit::Head()));  // Hyperion: not rich (rich bases)
  dwFlags = 0;
  bUnion93 = 0;
  bPathingFlags = 0;
  wPrevHP = 1;
  dwUnion134 = 0;
  if (!sub_425A70(nUnitType, x, y, nPlayer))
    return FALSE;
  for (int i = 0; i < 5; i++)
    wBuildQueue[i] = 0xE4;
  bIdGeneration = (bIdGeneration + 1) & 0x1F;
  bBuildQueueSlot = 0;
  if (IsRallyer())
    SetButtonset(0xE8);
  else
    SetButtonset(0xE7);
  _unmapped_E1[0] = static_cast<BYTE>(net_rand(0x0F));
  DWORD dwLife;
  if (gulUnitTypeFlags[wType] & 0x10)
    dwLife = 1;
  else
    dwLife = static_cast<DWORD>(HandicapLife(gxUnitLife[static_cast<WORD>(nUnitType)], nPlayer)) / 10;  // Hyperion: at the owner's handicap
  lHitPoints = dwLife;
  reinterpret_cast<CTarget *>(this)->SetTargetOrder(
      (dwFlags & 2) ? gubUnitOrderInitialize[static_cast<WORD>(nUnitType)] : 0x17);
  SetBackgroundOrder(0x17);
  DWORD *pdw138 = reinterpret_cast<DWORD *>(_unmapped_138);
  pdw138[1] = 0xFFFFFFFF;
  pdw138[2] = 0xFFFFFFFF;
  pdw138[3] = 0xFFFFFFFF;
  pdw138[4] = 0xFFFFFFFF;
  pPlayerUnitPrev = NULL;
  pPlayerUnitNext = sgpPlayerLists_g[nPlayer];
  if (sgpPlayerLists_g[nPlayer] != NULL)
    sgpPlayerLists_g[nPlayer]->pPlayerUnitPrev = this;
  sgpPlayerLists_g[nPlayer] = this;
  count_add_existence(this);
  if (dwFlags & 2) {
    TrackCollision();
    SyncConstructionGraphic(1);
    pSprite->SetMask(0);
    if (placebox_is_on())
      sub_47D530();
  } else {
    if (wType == 2 || wType == 0x13)
      bSpiderMineCount = 3;
    CSprite *pSpr = pSprite;
    pSpr->flags.b |= 0x20;
    pSpr->SetMask(0);
  }
  dwUnionE4 = 0xFFFFFFFF;
  if (g_hxPlayers)
    UnitDetectionSet(this, PlayerSet::All());  // Hyperion: and the slots past 31 (stage D)
  if (gulUnitTypeFlags[wType] & 0x10)
    pSprite->flags.b |= 0x10;
  else if (!(pSprite->flags.b & 0x20))
    sub_423260();
  return TRUE;
}

// A one-bit bitfield store is what emits the memory-operand xor merge on
// bPathingFlags.
struct path_flags_bits {
  BYTE bLowGround : 1;
  BYTE _rest : 7;
};

// Builds the flingy and sprite for a unit of wUnitType and resets its
// type-derived state.
BOOL CUnit::sub_425A70(WORD wUnitType, int x, int y, int nPlayer) {
  CUnit *pSaved = gpImageUnit;
  gpImageUnit = this;
  if (!reinterpret_cast<CTarget *>(this)->Create(gubUnitFlingy[wUnitType],
                                                 static_cast<short>(x),
                                                 static_cast<short>(y),
                                                 nPlayer, 0)) {
    gpImageUnit = pSaved;
    return FALSE;
  }
  memset(&pUnionC0, 0, offsetof(CUnit, pUnionD0) - offsetof(CUnit, pUnionC0));  // Hyperion: the same fields, 0x10 bytes in 1.17
  bCarryResource = 0;
  bUnion91 = 0;
  wType = wUnitType;
  reinterpret_cast<CThingy *>(this)->SpriteUpdate();
  gpImageUnit = pSaved;
  bUnion90 = 8;
  lShields = HandicapShields(guwUnitEnergy[wType], nPlayer) << 8;  // Hyperion: at the owner's handicap
  if (wUnitType == 0xAC)
    wEnergy = 0x6400;
  else
    wEnergy = static_cast<WORD>(GetMaxEnergy() / 4);
  bUnion92 = 0;
  pSprite->bElevationLevel = gubUnitPriorityPlane[wUnitType];
  dwFlags = (dwFlags & ~2) | ((gulUnitTypeFlags[wUnitType] << 1) & 2);
  dwFlags = (dwFlags & ~4) | (gulUnitTypeFlags[wUnitType] & 4);
  dwFlags = (dwFlags & ~0x10000) | ((gulUnitTypeFlags[wUnitType] >> 12) & 0x10000);
  ApplyPrototypeFlags();
  dwFlags = (dwFlags & ~0x100000) | (~(dwFlags << 18) & 0x100000);
  reinterpret_cast<path_flags_bits *>(&bPathingFlags)->bLowGround =
      gubUnitPriorityPlane[wUnitType] < 12;
  if (IsBeaconType())
    dwFlags |= 0x200000;
  if (gulUnitTypeFlags[wType] & 1) {
    bTechType = 0x2C;
    bUpgradeType = 0x3D;
  }
  pPath = NULL;
  bPathState = 0;
  bUnionE3 = 0;
  if (gulUnitTypeFlags[wUnitType] & 0x20000000)
    dwFlags |= 0x4000000;
  else if (!(gulUnitTypeFlags[wType] & 0x20000000))
    dwFlags &= ~0x4000000;
  bDmgOverlayCount = pSprite->GetDamageOverlayCount();
  wRemainingBuildTime = guwUnitTimeToBuild[wType];
  if (wRemainingBuildTime == 0)
    wRemainingBuildTime = 1;
  AssignHPGain();
  AssignShieldGain();
  ApplyUpgrades();
  ResetMovementSpeed();
  return TRUE;
}
