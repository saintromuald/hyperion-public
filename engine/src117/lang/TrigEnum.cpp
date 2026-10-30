// TrigEnum.cpp -- the trigger enumeration family (oracle 0x4B95F0..0x4BA890). A
// trigger condition names a group -- a single player, or one of the
// pseudo-groups (foes, allies, neutrals, all, force 1..4, non-allied-victory)
// -- and every counting condition is evaluated by handing one callback to the
// enumerator that expands that group into concrete nations. The accessors the
// enumerators call back into are group-aware themselves, so the family is
// mutually recursive: a condition on all players re-enters its own accessor
// once per player, with the group id replaced by a nation id.
//
// Functions are in ascending oracle VA order, which under /Ox is also the
// declaration order the oracle was built from. opponent_count (0x4BA060) is the
// one body of this TU that is not written here.
#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../port/gameplay/port_eud.h"  // Hyperion: EUD maps
#include "platform.h"          // BOOL/BYTE/WORD/DWORD + <stddef.h> for offsetof
#include "CSprite.h"           // TrigEnum_HeightMatches derefs pUnit->pSprite
#include "TrigEnum.h"          // pulls CUnit.h and rect16.h
#include "net_cmds.h"          // Hyperion: PlayerData, for NationType
#include "../h/typelimits.h"  // Hyperion: a mod's types

// The CRT entry points this TU reaches, declared by hand rather than through
// <string.h> so the include closure stays the src117 headers above. /Oi expands
// a small constant-size memcpy inline and calls it for a large one, so
// TrigEnum_PrepCounts' 0x540 copy becomes `rep movsd` while its two 0x2AC0
// copies stay real calls.
extern "C" void *__cdecl memcpy(void *, const void *, size_t);
extern "C" void *__cdecl memset(void *, int, size_t);

// MapRegion, the trigger location record, is defined in src117/h/TrigEnum.h:
// src117/lang/TrigAct.cpp needs the same record, and the generated declaration
// of gMapRegions needs the type named on the include path.

// Globals, dual-spelled: an `extern` for the proof path and a baked absolute
// literal for the payload, so the --dll-mode gate exercises the shipped branch
// rather than being vacuous. The extents are memset or memcpy size literals in
// count_init or alliance_init, with the next sheet name closing each one:
// 0x6C4C10 sglCachedGameCounts_g 0x540, 0x6C5150 sglCachedCompletedCounts_g
// 0x2AC0, 0x6C7C10 sglCachedUnitCounts_g 0x2AC0, 0x529604 glCounts_g 0xAB00
// (== 4 * 0x2AC0), 0x5290C4 glGameCounts_g 0x540, 0x534914 sgbAlliances_g 0x90
// (12x12).
//
// The innermost index is the player everywhere: `inc dword ptr [eax*4+G]` with
// eax = player puts consecutive players 4 bytes apart. The cached unit and
// completed tables are [type][player], 228x12, and 228*12*4 == 0x2AC0 exactly;
// glGameCounts_g and sglCachedGameCounts_g are 28 rows of 12 players, a row
// index being (offset / 48). 0x52C0C4, 0x52EB84 and 0x531644 are glCounts_g[1],
// [2] and [3], produced from the parent rather than recorded as globals.
#ifdef OC_DLL_MODE
#define sgbCachedEnum_g            (*reinterpret_cast<BYTE *>(OC_VA(0x6CA6D0u)))
#define sgbCachedregion_g          (*reinterpret_cast<BYTE *>(OC_VA(0x6CA6D1u)))
#define sglCachedGameCounts_g      (reinterpret_cast<DWORD (*)[12]>(OC_VA(0x6C4C10u)))
#define sglCachedCompletedCounts_g (reinterpret_cast<DWORD (*)[12]>(OC_VA(0x6C5150u)))
#define sglCachedUnitCounts_g      (reinterpret_cast<DWORD (*)[12]>(OC_VA(0x6C7C10u)))
#define sgbAlliances_g             (reinterpret_cast<BYTE (*)[12]>(OC_VA(0x534914u)))
#define gubUnitCountInfo           (reinterpret_cast<BYTE *>(OC_VA(0x523C40u)))
#define gulUnitTypeFlags           (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
#define glGameCounts_g             (reinterpret_cast<DWORD (*)[12]>(OC_VA(0x5290C4u)))
#define glCounts_g                 (reinterpret_cast<DWORD (*)[228][12]>(OC_VA(0x529604u)))
#define gMapRegions                (reinterpret_cast<MapRegion *>(OC_VA(0x534F40u)))
#define gPlayerMinerals            (reinterpret_cast<int *>(OC_VA(0x5263D0u)))
#define gPlayerGas                 (reinterpret_cast<int *>(OC_VA(0x526400u)))
#else
// sgbCachedEnum_g @0x6CA6D0 -- the cached-enumeration mode. 0 invalid, 1 filled
// by TrigEnum_PrepCounts (whole map), 2 filled by owned_count_atloc (one
// region). Sheet name, File == TrigEnum.cpp.
extern BYTE      sgbCachedEnum_g;
// sgbCachedregion_g @0x6CA6D1 -- the region the mode-2 fill was for.
// `cmp byte ptr [6CA6D1h], bl` / `mov byte ptr [6CA6D1h], bl`. A distinct
// object from sgbCachedEnum_g, not a slice: the two are read and written
// independently and hold different value spaces. Sheet name and spelling.
extern BYTE      sgbCachedregion_g;
extern PlayerCachedGameCountsGrid sglCachedGameCounts_g;
extern PlayerCachedCompletedCountsGrid sglCachedCompletedCounts_g;
extern PlayerCachedUnitCountsGrid sglCachedUnitCounts_g;
extern PlayerAlliancesMatrix sgbAlliances_g;
// gubUnitCountInfo @0x523C40 -- `mov bl, byte ptr [eax+523C40h]`, scale 1 => a
// BYTE element indexed by unit type. Bits used here: 0x08 "counts as men",
// 0x10 "counts as a building", 0x20 "counts as a factory". Sheet name, File ==
// arr/units.cpp. See hazard 3.
extern BYTE      gubUnitCountInfo[228] OC_UNALIGNED;
// gulUnitTypeFlags @0x521660 -- `test byte ptr [eax*4+521660h], 8h/10h` with
// eax = unit type. The *4 scale gives a DWORD element; the 228-entry extent is
// units.dat's. Sheet name, File == arr/units.cpp.
extern DWORD     gulUnitTypeFlags[228] OC_UNALIGNED;
extern PlayerGameCountsGrid glGameCounts_g;
extern PlayerUnitCountsGrid glCounts_g;
// gMapRegions @0x534F40 -- base, not a phantom: ExtMapRegions is literally
// `mov eax, 534F40h / ret`, and sub_499C50 uses it as the destination base of a
// `rep movsd` (`mov edi, 534F40h`). Accessors index it 0-based (act_move_unit
// does `dec al` before `lea ecx,[eax+eax*4]`).
extern MapRegion gMapRegions[255];
// gPlayerMinerals @0x5263D0 / gPlayerGas @0x526400 -- already in
// re/globals117.json as int[12]. Separate objects, not
// slices of one table: the resource initialiser issues four distinct
// `memset(x, 0, 30h)` calls (0x5263D0, 0x526400, 0x526460, 0x526430) and in a
// non-contiguous order, which a single object could not produce.
extern PlayerMineralsTable gPlayerMinerals;
extern PlayerGasTable gPlayerGas;
#endif

// unchanged, so the spellings of the object stay one type.
struct netslot_t {
  /*0x00*/ BYTE bType;
  /*0x01*/ BYTE _unmapped01[1];
  /*0x02*/ BYTE bTeam;
  /*0x03*/ BYTE _unmapped03[0x24 - 0x03];
};
static_assert(sizeof(netslot_t) == 0x24, "netslot_t: GetNumTeamOccupiedSlots walks 0x6727F0-0x24 down to 0x6726D0 in 0x24 steps");
static_assert(offsetof(netslot_t, bType) == 0x00, "netslot_t::bType opponents_count: mov al, byte ptr [eax*4+6726D0h], eax = 9*player");
static_assert(offsetof(netslot_t, bTeam) == 0x02, "netslot_t::bTeam sub_4517D0: mov cl, byte ptr [eax*4+6726D2h]");

#ifndef OC_SGNETSLOTS
#define OC_SGNETSLOTS
#ifdef OC_DLL_MODE
#define sgNetSlots (*reinterpret_cast<netslot_t(*)[8]>(OC_VA(0x6726D0u)))
#else
extern netslot_t sgNetSlots[8];
#endif
#endif


// How many nations the group has as opponents. A pseudo-group re-enters through
// its own enumerator with this function as the callback; a concrete nation
// walks every nation allied with it and marks each one that is not mutually
// allied with that ally. The marks are a set, so two allies naming the same
// opponent count once.

// TU-local prototypes, deliberately not in src117/h/TrigEnum.h.

// count_game_local is the internal twin of count_game (0x4B9970, 99 B) that
// units_count_atloc calls; the external count_game is declared in TrigEnum.h
// and is the hooked copy. The line break after `static` is deliberate: the
// generator resolves a signature with a single-line regex and restates what it
// captured as the thunk's definition, where `static` is illegal.
static
void __fastcall count_game_local(CUnit *pUnit);

// TrigEnum_HeightMatches.
//
// Returns the caller's mask ANDed with the one bit that names this unit's height band, or 0
// when get_map_height returns something outside 0..2:
//     ground (dwFlags & 4 clear):  h=0 -> 0x01, h=1 -> 0x02, h=2 -> 0x04
//     air    (dwFlags & 4 set):    h=0 -> 0x08, h=1 -> 0x10, h=2 -> 0x20
int __fastcall TrigEnum_HeightMatches(CUnit *pUnit, WORD wMask)
{
  // Three spellings here are each worth bytes. The locals are `short`, not int
  // and not a bare cast in the call: the oracle loads with `movzx`, the field's
  // own width, and converts with `movsx`, the local's signedness, where the
  // inline cast fuses both into one `movsx word ptr`. Y comes before X, because
  // the oracle materialises the second argument first and lets the first
  // argument's load clobber the sprite pointer. And the reads live inside the
  // branches rather than in a shared prologue: hoisted by hand the temporary
  // coalesces with EDX, while hoisted by CSE out of the duplicated form it
  // stays in ECX, as the oracle has it.
  if (pUnit->dwFlags & 4) {
    spos_t y = PosS(pUnit->pSprite->wPosY);
    spos_t x = PosS(pUnit->pSprite->wPosX);
    switch (get_map_height(x, y)) {
    case 0: return wMask & 0x08;
    case 1: return wMask & 0x10;
    case 2: return wMask & 0x20;
    }
    return 0;
  }
  {
    spos_t y = PosS(pUnit->pSprite->wPosY);
    spos_t x = PosS(pUnit->pSprite->wPosX);
    switch (get_map_height(x, y)) {
    case 0: return wMask & 0x01;
    case 1: return wMask & 0x02;
    case 2: return wMask & 0x04;
    }
    return 0;
  }
}

// The family spine: six functions ride on this template. The first parameter is
// unused in five of the six and lives in ECX; it is real, not padding --
// group_enum reads it (`lea eax, [ecx-11h]`). All six share one signature
// because they are interchangeable through the accessors' jump tables.

// all_enum.
//
// Two loops: nations 0..7 come from the cursor walk, nations 8..11 are visited
// unconditionally. `ret 8h` == two stack arguments.
int __fastcall all_enum(int, WORD nParam, void *pCtx, NationEnumCb pfnCb)
{
  int nCursor;
  int nTotal = 0;
  for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots(); n = nation_find_next(&nCursor))
    nTotal += pfnCb(TrigSlotTag(n), nParam, pCtx);
  // Hyperion: in a game with Hyperion's slots the walk above has 8 to 10 already,
  // so only neutral's is left
  for (DWORD n = g_hxPlayers ? PLAYER_NEUTRAL : 8; n < 12; n++)
    nTotal += pfnCb(TrigSlotTag(n), nParam, pCtx);
  return nTotal;
}

// all_enum_count.
//
// The early-exit variant. `ret 0Ch` == three stack arguments; the extra one is
// the limit, which is also forwarded to the callback (`push ebx / push ecx`),
// so the callback here is a different type from the plain one. The early exit
// is `return nTotal` here and `break` in the filtered variants, and the two are
// not interchangeable: this one has a second loop after the first.
int __fastcall all_enum_count(int, WORD nParam, void *pCtx, NationCountCb pfnCb,
                              int nLimit)
{
  int nCursor;
  int nTotal = 0;
  for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots(); n = nation_find_next(&nCursor)) {
    nTotal += pfnCb(TrigSlotTag(n), nParam, pCtx, nLimit);
    if (nTotal == nLimit)
      return nTotal;
  }
  for (DWORD n = g_hxPlayers ? PLAYER_NEUTRAL : 8; n < 12; n++) {  // Hyperion: as all_enum
    nTotal += pfnCb(TrigSlotTag(n), nParam, pCtx, nLimit);
    if (nTotal == nLimit)
      return nTotal;
  }
  return nTotal;
}

// allies_enum.
//
// Filtered enumeration, with no 8..11 tail. allies, foes and neutrals differ only in the
// compared alliance constant (2, 0 and 1). TriggerPlayer() is called twice, as 1.17 does: it
// is opaque, and a local for it changes the code.
int __fastcall allies_enum(int, WORD nParam, void *pCtx, NationEnumCb pfnCb)
{
  int nCursor;
  int nTotal = 0;
  for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots(); n = nation_find_next(&nCursor)) {
    if (n != (DWORD)TriggerPlayer() && sgbAlliances_g[TriggerPlayer()][n] == 2)
      nTotal += pfnCb(TrigSlotTag(n), nParam, pCtx);
  }
  return nTotal;
}

// allies_enum_count.
//
// allies_enum plus the count tail: the limit, compared against the running total each
// iteration, is the second stack argument and ctx the first. The early exit is `break`, not
// `return nTotal`: the same meaning, but `break` gives 1.17's two epilogues.
int __fastcall allies_enum_count(int, WORD nParam, void *pCtx, NationCountCb pfnCb,
                                 int nLimit)
{
  int nCursor;
  int nTotal = 0;
  for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots(); n = nation_find_next(&nCursor)) {
    if (n != (DWORD)TriggerPlayer() && sgbAlliances_g[TriggerPlayer()][n] == 2) {
      nTotal += pfnCb(TrigSlotTag(n), nParam, pCtx, nLimit);
      if (nTotal == nLimit)
        break;
    }
  }
  return nTotal;
}

// clear_enum_cache.
void __fastcall clear_enum_cache(void)
{
  sgbCachedEnum_g = 0;
}

// 0x4B9830. Credits a unit to the completed-units snapshot, recurses into its
// cargo and powerup, then adds the derived rows. The oracle tests the hangar
// types twice (the inner test is always true); one test loses 18 bytes.
void __fastcall count_completed(CUnit *pUnit)
{
  BYTE bInfo = gubUnitCountInfo[pUnit->wType];

  sglCachedCompletedCounts_g[pUnit->wType][pUnit->bPlayer]++;

  if (pUnit->IsATransport()) {
    for (int i = 0; i < 8; i++) {
      CUnit *pCargo = pUnit->GetTransportUnit(i);
      if (pCargo)
        count_completed(pCargo);
    }
  }

  if (pUnit->wType == 0x48 || pUnit->wType == 0x52 ||
      pUnit->wType == 0x53 || pUnit->wType == 0x51) {
    BYTE bDocked = 0;
    // The first type is its own arm with the same store. As one `||`, MSVC
    // spills bDocked to [ebp-1] where the oracle spills bInfo.
    if (pUnit->wType == 0x48)
      bDocked = pUnit->bHangarCount;
    else if (pUnit->wType == 0x52 || pUnit->wType == 0x53 ||
             pUnit->wType == 0x51)
      bDocked = pUnit->bHangarCount;
    // Read before the call: in the subscript MSVC sinks the load past it and
    // the body is 13 bytes short.
    BYTE bOwner = pUnit->bPlayer;
    sglCachedCompletedCounts_g[pUnit->GetHangarType()][bOwner] += bDocked;
  }

  if (gulUnitTypeFlags[pUnit->wType] & 8) {
    CUnit *pPowerup = pUnit->pPowerup;
    if (pPowerup) {
      if (pPowerup->wType != 0xD7)
        sglCachedCompletedCounts_g[pPowerup->wType][pUnit->bPlayer]++;
      else
        count_completed(pPowerup);
    }
  }

  if (pUnit->wType == 0x6C && pUnit->pUnionD4 != 0)
    sglCachedCompletedCounts_g[14][pUnit->bPlayer]++;

  if (bInfo & 0x20)
    sglCachedGameCounts_g[15][pUnit->bPlayer]++;

  if (bInfo & 8)
    sglCachedGameCounts_g[1][pUnit->bPlayer]++;
  else if (bInfo & 0x10)
    sglCachedGameCounts_g[8][pUnit->bPlayer]++;
}

// count_game, the per-unit half of the
// "existing units" snapshot. This is the external, hooked copy; the static twin
// count_game_local below is the one units_count_atloc calls, because MSVC keeps
// ECX live across a call only for a callee it has already compiled in this TU
// with internal linkage, and a static symbol cannot be hooked. The 0x10 test is an
// else-if after the 0x08 test, not an independent if.
void __fastcall count_game(CUnit *pUnit)
{
  BYTE bFlags = gubUnitCountInfo[pUnit->wType];
  sglCachedUnitCounts_g[pUnit->wType][pUnit->bPlayer]++;
  if (bFlags & 0x20)
    sglCachedGameCounts_g[14][pUnit->bPlayer]++;
  if (bFlags & 8)
    sglCachedGameCounts_g[0][pUnit->bPlayer]++;
  else if (bFlags & 0x10)
    sglCachedGameCounts_g[7][pUnit->bPlayer]++;
  else if (pUnit->wType == 0x24 || pUnit->wType == 0x3B || pUnit->wType == 0x61)
    sglCachedGameCounts_g[0][pUnit->bPlayer]++;
}

// Internal copy of count_game, same bytes. units_count_atloc keeps ECX
// across the call, which only an internal callee allows; the hooked copy
// above is the external one.
static void __fastcall count_game_local(CUnit *pUnit)
{
  BYTE bFlags = gubUnitCountInfo[pUnit->wType];
  sglCachedUnitCounts_g[pUnit->wType][pUnit->bPlayer]++;
  if (bFlags & 0x20)
    sglCachedGameCounts_g[14][pUnit->bPlayer]++;
  if (bFlags & 8)
    sglCachedGameCounts_g[0][pUnit->bPlayer]++;
  else if (bFlags & 0x10)
    sglCachedGameCounts_g[7][pUnit->bPlayer]++;
  else if (pUnit->wType == 0x24 || pUnit->wType == 0x3B || pUnit->wType == 0x61)
    sglCachedGameCounts_g[0][pUnit->bPlayer]++;
}

// A trigger accessor and a nation-enumeration callback at once (`ret 4h` is the callback
// ABI's unused stack argument): the group cases re-enter the matching *_enum with
// death_count as pfnCb. nNation 13 is the current player, 14 foes, 15 allies, 16 neutrals,
// 17 all, 18..21 forces 1..4, 22..25 nothing, 26 the non-allied victory players, anything
// else a player index. nUnitType 229..232 are "Any unit" (men plus buildings), "Men",
// "Buildings" and "Factories".
int __fastcall death_count(DWORD nNation, WORD nUnitType, void *)
{
  DWORD nPlayer = nNation;

  // Hyperion: a slot an enumerator passed, not a group code; an EUD map's
  // player is its own value, bit 16 and all, and an EUD game has no slot past 11
  if ((nNation & TRIG_SLOT_TAG) && !EudGame())
    nPlayer = nNation &= ~TRIG_SLOT_TAG;
  else
  switch (nNation) {
  case 0x0D:
    nPlayer = (DWORD)TriggerPlayer();
    break;
  // Case 0x1A is out of numeric order on purpose: MSVC lays a jump-table
  // switch's bodies out in source order, and the oracle's first body after the
  // dispatch is nava_enum (0x4B9A12), ahead of foes at 0x4B9A22. In ascending
  // order this comes out as five callee deltas.
  case 0x1A: return nava_enum(nNation, nUnitType, 0, death_count);
  case 0x0E: return foes_enum(nNation, nUnitType, 0, death_count);
  case 0x0F: return allies_enum(nNation, nUnitType, 0, death_count);
  case 0x10: return neutrals_enum(nNation, nUnitType, 0, death_count);
  case 0x11: return all_enum(nNation, nUnitType, 0, death_count);
  case 0x12: case 0x13: case 0x14: case 0x15:
    return group_enum(nNation, nUnitType, 0, death_count);
  case 0x16: case 0x17: case 0x18: case 0x19:
    return 0;
  }

  WORD wType = (WORD)nUnitType;
  if (EudGame())  // Hyperion: an EUD game reads through 1.16.1's addresses
    return (int)EudDeaths(nPlayer, wType);
  switch (wType) {
  case 229: return glGameCounts_g[3][nPlayer] + glGameCounts_g[10][nPlayer];
  case 230: return glGameCounts_g[3][nPlayer];
  case 231: return glGameCounts_g[10][nPlayer];
  case 232: return glGameCounts_g[16][nPlayer];
  }
  return glCounts_g[3][wType][nPlayer];
}

// foes_enum. Shape ENUM_F, alliance == 0.
int __fastcall foes_enum(int, WORD nParam, void *pCtx, NationEnumCb pfnCb)
{
  int nCursor;
  int nTotal = 0;
  for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots(); n = nation_find_next(&nCursor)) {
    if (n != (DWORD)TriggerPlayer() && sgbAlliances_g[TriggerPlayer()][n] == 0)
      nTotal += pfnCb(TrigSlotTag(n), nParam, pCtx);
  }
  return nTotal;
}

// foes_enum_count. Shape ENUM_FC.
int __fastcall foes_enum_count(int, WORD nParam, void *pCtx, NationCountCb pfnCb,
                               int nLimit)
{
  int nCursor;
  int nTotal = 0;
  for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots(); n = nation_find_next(&nCursor)) {
    if (n != (DWORD)TriggerPlayer() && sgbAlliances_g[TriggerPlayer()][n] == 0) {
      nTotal += pfnCb(TrigSlotTag(n), nParam, pCtx, nLimit);
      if (nTotal == nLimit)
        break;
    }
  }
  return nTotal;
}

// The guarded one-shot cache fill for whole-map counting (mode 1). The first
// copy is spelled memcpy and the other two memmove because 0x540 is inlined to
// `rep movsd` under /Oi while the two 0x2AC0 copies stay real calls. The store
// that arms the cache flag has to come before the copies, as the oracle has it.
void __fastcall TrigEnum_PrepCounts(void)
{
  if (sgbCachedEnum_g == 1)
    return;
  sgbCachedEnum_g = 1;
  memcpy(sglCachedGameCounts_g.a, glGameCounts_g.a, 28 * 12 * 4);
  memcpy(sglCachedCompletedCounts_g.a, glCounts_g.a[1], 228 * 12 * 4);
  memcpy(sglCachedUnitCounts_g.a, glCounts_g.a[0], 228 * 12 * 4);
  // Hyperion: and Hyperion's slots
  for (int nSlot = PLAYER_SLOTS_117; nSlot < PlayerSlots(); nSlot++) {
    for (int nRow = 0; nRow < 28; nRow++)
      sglCachedGameCounts_g[nRow][nSlot] = glGameCounts_g[nRow][nSlot];
    for (int nType = 0; nType < 228; nType++) {
      sglCachedCompletedCounts_g[nType][nSlot] = glCounts_g[1][nType][nSlot];
      sglCachedUnitCounts_g[nType][nSlot] = glCounts_g[0][nType][nSlot];
    }
  }
  // Hyperion: and a mod's types, every slot
  for (unsigned nType = UNIT_TYPE_MOD_FIRST; UnitTypeMod(nType); nType++) {
    for (int nSlot = 0; nSlot < PlayerSlots(); nSlot++) {
      sglCachedCompletedCounts_g[nType][nSlot] = glCounts_g[1][nType][nSlot];
      sglCachedUnitCounts_g[nType][nSlot] = glCounts_g[0][nType][nSlot];
    }
  }
}

// group_enum.
//
// Shape ENUM_G -- the one that reads argument 1. force = arg1 - 0x11, compared
// against GetPlayerForce(nation). This is what proves the family's shared
// 4-argument signature: the other five never touch ECX.
int __fastcall group_enum(int nGroup, WORD nParam, void *pCtx, NationEnumCb pfnCb)
{
  int nCursor;
  int nForce = nGroup - 0x11;
  int nTotal = 0;
  for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots(); n = nation_find_next(&nCursor)) {
    if (GetPlayerForce((int)n) == nForce)
      nTotal += pfnCb(TrigSlotTag(n), nParam, pCtx);
  }
  return nTotal;
}

// group_enum_count.
//
// Shape ENUM_GC: group_enum plus the count tail.
int __fastcall group_enum_count(int nGroup, WORD nParam, void *pCtx,
                                NationCountCb pfnCb, int nLimit)
{
  int nCursor;
  int nForce = nGroup - 0x11;
  int nTotal = 0;
  for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots(); n = nation_find_next(&nCursor)) {
    if (GetPlayerForce((int)n) == nForce) {
      nTotal += pfnCb(TrigSlotTag(n), nParam, pCtx, nLimit);
      if (nTotal == nLimit)
        break;
    }
  }
  return nTotal;
}

// death_count's twin: the same two switches over
// six different global rows, where glCounts_g[2] is the kill slice and [3] the death slice.
int __fastcall kill_count(DWORD nNation, WORD nUnitType, void *)
{
  DWORD nPlayer = nNation;

  if (nNation & TRIG_SLOT_TAG)  // Hyperion: a slot an enumerator passed, not a group code
    nPlayer = nNation &= ~TRIG_SLOT_TAG;
  else
  switch (nNation) {
  case 0x0D:
    nPlayer = (DWORD)TriggerPlayer();
    break;
  case 0x1A: return nava_enum(nNation, nUnitType, 0, kill_count);  // see death_count
  case 0x0E: return foes_enum(nNation, nUnitType, 0, kill_count);
  case 0x0F: return allies_enum(nNation, nUnitType, 0, kill_count);
  case 0x10: return neutrals_enum(nNation, nUnitType, 0, kill_count);
  case 0x11: return all_enum(nNation, nUnitType, 0, kill_count);
  case 0x12: case 0x13: case 0x14: case 0x15:
    return group_enum(nNation, nUnitType, 0, kill_count);
  case 0x16: case 0x17: case 0x18: case 0x19:
    return 0;
  }

  WORD wType = (WORD)nUnitType;
  switch (wType) {
  case 229: return glGameCounts_g[4][nPlayer] + glGameCounts_g[11][nPlayer];
  case 230: return glGameCounts_g[4][nPlayer];
  case 231: return glGameCounts_g[11][nPlayer];
  case 232: return glGameCounts_g[17][nPlayer];
  }
  return glCounts_g[2][wType][nPlayer];
}

// nava_enum.
//
// "Not allied, not the trigger player": the guard is
// `n != tp && (A[n][tp] != 2 || A[tp][n] != 2)` over sgbAlliances_g, the two tests indexing
// it in opposite directions, so the callback runs unless the two are mutually allied.
// TriggerPlayer() is called three times per iteration, as 1.17 does.
int __fastcall nava_enum(int, WORD nParam, void *pCtx, NationEnumCb pfnCb)
{
  int nCursor;
  int nTotal = 0;
  for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots(); n = nation_find_next(&nCursor)) {
    if (n != (DWORD)TriggerPlayer() &&
        (sgbAlliances_g[n][TriggerPlayer()] != 2 ||
         sgbAlliances_g[TriggerPlayer()][n] != 2))
      nTotal += pfnCb(TrigSlotTag(n), nParam, pCtx);
  }
  return nTotal;
}

// nava_enum_count. Shape ENUM_NC.
int __fastcall nava_enum_count(int, WORD nParam, void *pCtx, NationCountCb pfnCb,
                               int nLimit)
{
  int nCursor;
  int nTotal = 0;
  for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots(); n = nation_find_next(&nCursor)) {
    if (n != (DWORD)TriggerPlayer() &&
        (sgbAlliances_g[n][TriggerPlayer()] != 2 ||
         sgbAlliances_g[TriggerPlayer()][n] != 2)) {
      nTotal += pfnCb(TrigSlotTag(n), nParam, pCtx, nLimit);
      if (nTotal == nLimit)
        break;
    }
  }
  return nTotal;
}

// neutrals_enum. Shape ENUM_F, alliance == 1.
int __fastcall neutrals_enum(int, WORD nParam, void *pCtx, NationEnumCb pfnCb)
{
  int nCursor;
  int nTotal = 0;
  for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots(); n = nation_find_next(&nCursor)) {
    if (n != (DWORD)TriggerPlayer() && sgbAlliances_g[TriggerPlayer()][n] == 1)
      nTotal += pfnCb(TrigSlotTag(n), nParam, pCtx);
  }
  return nTotal;
}

// neutrals_enum_count. Shape ENUM_FC.
int __fastcall neutrals_enum_count(int, WORD nParam, void *pCtx, NationCountCb pfnCb,
                                   int nLimit)
{
  int nCursor;
  int nTotal = 0;
  for (DWORD n = nation_find_first(&nCursor); n < (DWORD)PlayerLobbySlots(); n = nation_find_next(&nCursor)) {
    if (n != (DWORD)TriggerPlayer() && sgbAlliances_g[TriggerPlayer()][n] == 1) {
      nTotal += pfnCb(TrigSlotTag(n), nParam, pCtx, nLimit);
      if (nTotal == nLimit)
        break;
    }
  }
  return nTotal;
}

extern PlayerTable<PlayerData, PlayerTagData> gPlayerData;  // 0x6726C8

// Hyperion: a nation's type, which sgNetSlots names as 1.17's bytes at
// &gPlayerData[0].nType; past 7 through the player table.
static inline BYTE NationType(DWORD n)
{
  return n < 8 ? sgNetSlots[n].bType : gPlayerData[n].nType;
}

int opponents_count(DWORD nGroup, WORD, void *)
{
  BYTE bOpponent[PLAYER_SLOTS_MAX];  // Hyperion: 8 in 1.17
  int nCursorAlly;
  int nCursorFoe;
  DWORD nAlly;
  DWORD nFoe;
  int nCount;
  int i;

  if (nGroup & TRIG_SLOT_TAG)  // Hyperion: a slot an enumerator passed, not a group code
    nGroup &= ~TRIG_SLOT_TAG;
  else
  switch (nGroup) {
    case 13:
      nGroup = (DWORD)TriggerPlayer();
      break;
    case 26:
      return nava_enum(nGroup, 0, 0, opponents_count);
    case 14:
      return foes_enum(nGroup, 0, 0, opponents_count);
    case 15:
      return allies_enum(nGroup, 0, 0, opponents_count);
    case 16:
      return neutrals_enum(nGroup, 0, 0, opponents_count);
    case 17:
      return all_enum(nGroup, 0, 0, opponents_count);
    case 18: case 19: case 20: case 21:
      return group_enum(nGroup, 0, 0, opponents_count);
    case 22: case 23: case 24: case 25:
      return 0;
  }

  if (!PlayerIsLobby(nGroup))  // Hyperion: >= 8 in 1.17
    return 0;
  if (NationType(nGroup) == 7 || NationType(nGroup) == 3)
    return 0;

  memset(bOpponent, 0, sizeof(bOpponent));
  for (nAlly = nation_find_first(&nCursorAlly); nAlly < (DWORD)PlayerLobbySlots();
       nAlly = nation_find_next(&nCursorAlly)) {
    if (NationType(nAlly) == 7 || NationType(nAlly) == 3)
      continue;
    if (nAlly != nGroup &&
        (sgbAlliances_g[nAlly][nGroup] != 2 || sgbAlliances_g[nGroup][nAlly] != 2))
      continue;
    for (nFoe = nation_find_first(&nCursorFoe); nFoe < (DWORD)PlayerLobbySlots();
         nFoe = nation_find_next(&nCursorFoe)) {
      if (nFoe == nAlly)
        continue;
      if (NationType(nFoe) == 7 || NationType(nFoe) == 3)
        continue;
      if (sgbAlliances_g[nFoe][nAlly] != 2 || sgbAlliances_g[nAlly][nFoe] != 2)
        bOpponent[nFoe] = 1;
    }
  }

  nCount = 0;
  for (i = 0; i < PlayerLobbySlots(); i++)  // Hyperion: 8 in 1.17
    if (bOpponent[i])
      nCount++;
  return nCount;
}

// Argument 2 is discarded -- the enumerators are
// handed a literal 0 -- while argument 3, the CUnit * being tested, rides through as the
// context. The body is the ownership test itself, as 0 or 1.
int __fastcall owned_count(DWORD nNation, WORD, void *pCtx)
{
  DWORD nPlayer = nNation;

  if (nNation & TRIG_SLOT_TAG)  // Hyperion: a slot an enumerator passed, not a group code
    nPlayer = nNation &= ~TRIG_SLOT_TAG;
  else
  switch (nNation) {
  case 0x0D:
    nPlayer = (DWORD)TriggerPlayer();
    break;
  case 0x1A: return nava_enum(nNation, 0, pCtx, owned_count);
  case 0x0E: return foes_enum(nNation, 0, pCtx, owned_count);
  case 0x0F: return allies_enum(nNation, 0, pCtx, owned_count);
  case 0x10: return neutrals_enum(nNation, 0, pCtx, owned_count);
  case 0x11: return all_enum(nNation, 0, pCtx, owned_count);
  case 0x12: case 0x13: case 0x14: case 0x15:
    return group_enum(nNation, 0, pCtx, owned_count);
  case 0x16: case 0x17: case 0x18: case 0x19:
    return 0;
  }

  return ((CUnit *)pCtx)->bPlayer == nPlayer;
}

// owned_count_atloc.
//
// A memoised cache fill for one map region: mode 2 plus the same region means
// the three tables already hold this region's counts. The three memsets are
// real calls even though the sizes are compile-time constants -- MSVC does not
// expand memset at 0x540 or 0x2AC0, where a 0x540 memcpy does become
// `rep movsd` in TrigEnum_PrepCounts.
void __fastcall owned_count_atloc(BYTE nRegion)
{
  if (sgbCachedEnum_g == 2 && sgbCachedregion_g == nRegion)
    return;

  sgbCachedEnum_g = 2;
  sgbCachedregion_g = nRegion;
  PlayerClear(sglCachedGameCounts_g);  // Hyperion: and Hyperion's slots
  PlayerClear(sglCachedCompletedCounts_g);
  PlayerClear(sglCachedUnitCounts_g);

  // Assignment order matters, and it is left, right, top, bottom rather than
  // left, top, right, bottom: the oracle stores [ebp-0Ch], [ebp-8], [ebp-0Ah],
  // [ebp-6] in that sequence, and writing them in field order compiles to the
  // same four stores in a different sequence.
  rect16_t area;
  area.left   = (short)gMapRegions[nRegion].left;
  area.right  = (short)gMapRegions[nRegion].right;
  area.top    = (short)gMapRegions[nRegion].top;
  area.bottom = (short)gMapRegions[nRegion].bottom;

  DWORD dwFlags = gMapRegions[nRegion].wFlags;
  // The cast is type plumbing: units_count_atloc's second parameter is the
  // height mask it dereferences (`mov dx, word ptr [edx]`), a WORD *, not the
  // generic void * find_foreach_unit hands its callback.
  find_foreach_unit(&area, reinterpret_cast<FindUnitCb>(units_count_atloc),
                    &dwFlags);
}

// Only three resource kinds, so MSVC compiles the
// switch to a compare chain, not a jump table.
int __fastcall resource_count(DWORD nNation, WORD nResType, void *)
{
  DWORD nPlayer = nNation;

  if (nNation & TRIG_SLOT_TAG)  // Hyperion: a slot an enumerator passed, not a group code
    nPlayer = nNation &= ~TRIG_SLOT_TAG;
  else
  switch (nNation) {
  case 0x0D:
    nPlayer = (DWORD)TriggerPlayer();
    break;
  case 0x1A: return nava_enum(nNation, nResType, 0, resource_count);
  case 0x0E: return foes_enum(nNation, nResType, 0, resource_count);
  case 0x0F: return allies_enum(nNation, nResType, 0, resource_count);
  case 0x10: return neutrals_enum(nNation, nResType, 0, resource_count);
  case 0x11: return all_enum(nNation, nResType, 0, resource_count);
  case 0x12: case 0x13: case 0x14: case 0x15:
    return group_enum(nNation, nResType, 0, resource_count);
  case 0x16: case 0x17: case 0x18: case 0x19:
    return 0;
  }

  switch ((WORD)nResType) {
  case 0: return gPlayerMinerals[nPlayer];
  case 1: return gPlayerGas[nPlayer];
  case 2: return gPlayerGas[nPlayer] + gPlayerMinerals[nPlayer];
  }
  return 0;
}

// No frame: its stack argument is never read, and
// `ret 4h` alone shows it exists. MSVC orders the five-term sum in case 0 itself, whatever
// the source order.
int __fastcall score_count(DWORD nNation, WORD nScoreType, void *)
{
  DWORD nPlayer = nNation;

  if (nNation & TRIG_SLOT_TAG)  // Hyperion: a slot an enumerator passed, not a group code
    nPlayer = nNation &= ~TRIG_SLOT_TAG;
  else
  switch (nNation) {
  case 0x0D:
    nPlayer = (DWORD)TriggerPlayer();
    break;
  case 0x1A: return nava_enum(nNation, nScoreType, 0, score_count);
  case 0x0E: return foes_enum(nNation, nScoreType, 0, score_count);
  case 0x0F: return allies_enum(nNation, nScoreType, 0, score_count);
  case 0x10: return neutrals_enum(nNation, nScoreType, 0, score_count);
  case 0x11: return all_enum(nNation, nScoreType, 0, score_count);
  case 0x12: case 0x13: case 0x14: case 0x15:
    return group_enum(nNation, nScoreType, 0, score_count);
  case 0x16: case 0x17: case 0x18: case 0x19:
    return 0;
  }

  switch ((WORD)nScoreType) {
  case 7: return glGameCounts_g[27][nPlayer];
  case 1: return glGameCounts_g[5][nPlayer];
  case 2: return glGameCounts_g[12][nPlayer];
  case 3: return glGameCounts_g[12][nPlayer] + glGameCounts_g[5][nPlayer];
  case 4: return glGameCounts_g[6][nPlayer];
  case 5: return glGameCounts_g[13][nPlayer];
  case 6: return glGameCounts_g[13][nPlayer] + glGameCounts_g[6][nPlayer];
  case 0: return glGameCounts_g[13][nPlayer] + glGameCounts_g[6][nPlayer]
               + glGameCounts_g[12][nPlayer] + glGameCounts_g[5][nPlayer]
               + glGameCounts_g[27][nPlayer];
  }
  return 0;
}

// The largest body in the TU. Argument 3 is both
// the enumeration context and a boolean: nonzero picks the completed tables (rows 1, 8 and
// 15 plus sglCachedCompletedCounts_g), zero the existing ones (rows 0, 7 and 14 plus
// sglCachedUnitCounts_g), and it is forwarded to the enumerators unchanged, so the flag
// survives the recursion. Both arms switch on the same (wType - 0xE5).
int __fastcall units_count(DWORD nNation, WORD nUnitType, void *pCtx)
{
  DWORD nPlayer = nNation;

  if (nNation & TRIG_SLOT_TAG)  // Hyperion: a slot an enumerator passed, not a group code
    nPlayer = nNation &= ~TRIG_SLOT_TAG;
  else
  switch (nNation) {
  case 0x0D:
    nPlayer = (DWORD)TriggerPlayer();
    break;
  case 0x1A: return nava_enum(nNation, nUnitType, pCtx, units_count);
  case 0x0E: return foes_enum(nNation, nUnitType, pCtx, units_count);
  case 0x0F: return allies_enum(nNation, nUnitType, pCtx, units_count);
  case 0x10: return neutrals_enum(nNation, nUnitType, pCtx, units_count);
  case 0x11: return all_enum(nNation, nUnitType, pCtx, units_count);
  case 0x12: case 0x13: case 0x14: case 0x15:
    return group_enum(nNation, nUnitType, pCtx, units_count);
  case 0x16: case 0x17: case 0x18: case 0x19:
    return 0;
  }

  WORD wType = (WORD)nUnitType;
  if (pCtx != 0) {
    switch (wType) {
    case 0xE5: return sglCachedGameCounts_g[1][nPlayer]
                    + sglCachedGameCounts_g[8][nPlayer];
    case 0xE6: return sglCachedGameCounts_g[1][nPlayer];
    case 0xE7: return sglCachedGameCounts_g[8][nPlayer];
    case 0xE8: return sglCachedGameCounts_g[15][nPlayer];
    default:   return sglCachedCompletedCounts_g[wType][nPlayer];
    }
  } else {
    switch (wType) {
    case 0xE5: return sglCachedGameCounts_g[7][nPlayer]
                    + sglCachedGameCounts_g[0][nPlayer];
    case 0xE6: return sglCachedGameCounts_g[0][nPlayer];
    case 0xE7: return sglCachedGameCounts_g[7][nPlayer];
    case 0xE8: return sglCachedGameCounts_g[14][nPlayer];
    default:   return sglCachedUnitCounts_g[wType][nPlayer];
    }
  }
}

// units_count_atloc.
//
// The per-unit visitor of the "count units at a location" sweep: EDX points at
// the caller's height mask (a WORD), ECX is the unit. Four successive rejects,
// then the two counters. Always returns 0. The missing `mov ecx, esi` before
// count_completed is the whole function, and it is not an oracle bug: it is why
// count_game_local is `static` and is defined above rather than below.
int __fastcall units_count_atloc(CUnit *pUnit, WORD *pwHeightMask)
{
  if (TrigEnum_HeightMatches(pUnit, *pwHeightMask))
    return 0;
  if (pUnit->dwFlags & 0x40000000)
    return 0;
  if (gulUnitTypeFlags[pUnit->wType] & 0x10)
    return 0;
  count_game_local(pUnit);
  if (pUnit->dwFlags & 1)
    count_completed(pUnit);
  return 0;
}
