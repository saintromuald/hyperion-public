// banker.cpp -- the resource banker: the unit, tech and upgrade cost queries,
// the staged-plan cost accessors, and the build-time affordability checks that
// debit and refund a player's minerals, gas and supply. Functions are in oracle
// VA order.
//
// Every function here is declared in a header already, ten in
// src117/h/SAI_Banker.h and four in src117/h/banker.h, and nothing here
// re-declares one: a second declaration of a name makes the generator emit no
// thunk at all.

// SAI_Banker.h declares bldg_get_unit_cost over the WORD its callers pass; this
// TU defines the int view under the same C symbol.
#define bldg_get_unit_cost bldg_get_unit_cost_word_view
#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "banker.h"         // bldg_add_resources, refund_unit_partial,
                            // bldg_check_food, bldg_check_resources
#include "SAI_Banker.h"     // the other ten rows + count_food_available
#include "CUnitUpgrade.h"   // get_upgrade_ore_cost / get_upgrade_gas_cost
#include "CUnitHarvest.h"   // gamesnd_show_player_error
#include "count.h"          // count_race, count_get_supplies_used
#undef bldg_get_unit_cost

// One callee from another TU is declared nowhere in src117/h: count_food_max
// @0x4142E0 (57 B, count.cpp), which bldg_check_food calls. The TU-local
// declaration below carries an explicit __fastcall, which is what makes a local
// declaration be consulted at all; it belongs in src117/h/count.h next to
// count_get_supplies_available.
//
// Its contract: bare `ret`, ECX and EDX live-in, no stack slots, EAX consumed
// 32-bit at all 4 call sites. The widths come off this body's call site, which
// is the count_food_available shape exactly: `mov ecx, ebx`, the full 32-bit
// unit type, and `mov dl, byte ptr [ebp-1h]`, an 8-bit move of the player
// rather than a widening.

// The globals this TU reaches, dual-spelled with one guard per name, never one
// guard over the whole block.

// Registered in re/globals117.json:
//   0x523520 guwUnitMineralsCost WORD[228]
//   0x5236E8 guwUnitGasesCost    WORD[228]
//   0x5263D0 gPlayerMinerals     int[12]
//   0x526400 gPlayerGas          int[12]
//   0x51F760 techdata_oreCost_g  WORD[44]
//   0x51F7B8 techdata_gasCost_g  WORD[44]
//   0x521660 gulUnitTypeFlags    DWORD[228]
// Not registered; these names come from the naming sheet, not from here:
//   0x53DAD0 banker_staging_orecost_g      sc-refs/sheets/data_naming_1.17.csv:635
//   0x53DB00 banker_staging_gascost_g      sheet row 636
//   0x53DB30 banker_staging_supplycost_g   sheet row 637
//   0x523E10 gubUnitFoodUsed               sheet row 590 (arr/units.cpp)
//   0x5264C2 byte_5264C2                   no sheet name; 0x5264C1 is
//            registered `byte_5264C1 BYTE`, so this is the next byte along
#ifdef OC_DLL_MODE
  #ifndef guwUnitMineralsCost
  #define guwUnitMineralsCost (reinterpret_cast<WORD *>(OC_VA(0x523520u)))
  #endif
  #ifndef guwUnitGasesCost
  #define guwUnitGasesCost (reinterpret_cast<WORD *>(OC_VA(0x5236E8u)))
  #endif
  #ifndef gPlayerMinerals
  #define gPlayerMinerals (reinterpret_cast<int *>(OC_VA(0x5263D0u)))
  #endif
  #ifndef gPlayerGas
  #define gPlayerGas (reinterpret_cast<int *>(OC_VA(0x526400u)))
  #endif
  #ifndef techdata_oreCost_g
  #define techdata_oreCost_g (reinterpret_cast<WORD *>(OC_VA(0x51F760u)))
  #endif
  #ifndef techdata_gasCost_g
  #define techdata_gasCost_g (reinterpret_cast<WORD *>(OC_VA(0x51F7B8u)))
  #endif
  #ifndef gulUnitTypeFlags
  #define gulUnitTypeFlags (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
  #endif
  #ifndef banker_staging_orecost_g
  #define banker_staging_orecost_g (reinterpret_cast<int *>(OC_VA(0x53DAD0u)))
  #endif
  #ifndef banker_staging_gascost_g
  #define banker_staging_gascost_g (reinterpret_cast<int *>(OC_VA(0x53DB00u)))
  #endif
  #ifndef banker_staging_supplycost_g
  #define banker_staging_supplycost_g (reinterpret_cast<int *>(OC_VA(0x53DB30u)))
  #endif
  #ifndef gubUnitFoodUsed
  #define gubUnitFoodUsed (reinterpret_cast<BYTE *>(OC_VA(0x523E10u)))
  #endif
  #ifndef byte_5264C2
  #define byte_5264C2 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C2u)))
  #endif
#else
extern WORD guwUnitMineralsCost[228] OC_UNALIGNED;
extern WORD guwUnitGasesCost[228] OC_UNALIGNED;
extern PlayerMineralsTable gPlayerMinerals;
extern PlayerGasTable gPlayerGas;
extern WORD techdata_oreCost_g[44];
extern WORD techdata_gasCost_g[44] OC_UNALIGNED;
extern DWORD gulUnitTypeFlags[228] OC_UNALIGNED;
extern PlayerBankerOreTable banker_staging_orecost_g;  // Hyperion: and the nations past 11
extern PlayerBankerGasTable banker_staging_gascost_g;
extern PlayerBankerSupplyTable banker_staging_supplycost_g;
extern BYTE gubUnitFoodUsed[228] OC_UNALIGNED;
extern BYTE byte_5264C2;
#endif

// The bodies, in oracle VA order.

// 0x408460 (37 B). Both `movzx` widenings are the parameters being
// zero-extended, which is what pins (BYTE, WORD).
void bldg_add_resources(BYTE bPlayer, WORD wUnitType) {
  gPlayerMinerals[bPlayer] += guwUnitMineralsCost[wUnitType];
  gPlayerGas[bPlayer] += guwUnitGasesCost[wUnitType];
}

// 0x408490 (63 B). `lea eax, [eax+eax*2]` then `cdq / and edx, 3 / add eax, edx
// / sar eax, 2` is MSVC's signed divide by 4, so the cost*3 product is an int:
// a WORD operand promotes to int and the quotient is the 3/4 refund.
void refund_unit_partial(BYTE bPlayer, WORD wUnitType) {
  gPlayerMinerals[bPlayer] += guwUnitMineralsCost[wUnitType] * 3 / 4;
  gPlayerGas[bPlayer] += guwUnitGasesCost[wUnitType] * 3 / 4;
}

// 0x4084D0 (268 B).
// The second parameter is an int and the truncation to a WORD is in the body,
// not in the declaration. The body passes it to count_race,
// count_food_available and count_food_max as `mov ecx, ebx`, a full 32-bit
// forward that a WORD value could not produce -- it would need a movzx, and
// count_race's own parameter is an int -- while every DAT index and the
// `cmp bx, 67h` read 16 bits. A WORD local reconciles the two; a WORD parameter
// cannot.

// The error blocks' 32-bit widening is scoped to the block on purpose.
// gamesnd_show_player_error's second parameter is a WORD, its own body opening
// `movzx esi, dx`, so writing the arguments as `bRace + 1` lets MSVC narrow the
// whole chain to sixteen bits. That narrowing is not local: it makes bRace cheap
// enough to win a byte register, nUnitType loses EBX and spills, the frame grows
// from `sub esp, 8` to 12, and the two error blocks re-zero-extend BL twice
// each, measuring 275 B against the oracle's 268. Hoisting the widening to
// function scope over-corrects the other way, the int wanting EBX for its whole
// live range, and the body comes out 265. Only a widening confined to the cold
// block reproduces the oracle: the zero-extend happens there, from bRace's byte
// spill slot, `lea` builds the string id and the sound id increments the same
// dead 32-bit temp in place -- `movzx edx, [ebp-2h] / lea eax, [edx+34Fh] /
// inc edx`. Six shapes were measured and this is the only one that matches.
BOOL bldg_check_food(BYTE bPlayer, int nUnitType, int bShowError) {
  const WORD wUnit = (WORD)nUnitType;
  const int nFood = gubUnitFoodUsed[wUnit];
  const DWORD ulFlags = gulUnitTypeFlags[wUnit];

  banker_staging_supplycost_g[bPlayer] = nFood;
  if (ulFlags & 0x400)
    banker_staging_supplycost_g[bPlayer] = nFood + nFood;

  int nStaged = banker_staging_supplycost_g[bPlayer];
  if (nStaged == 0)
    return 1;
  if (ulFlags & 0x1000000)
    return 1;
  if (wUnit == 0x67) {
    nStaged -= gubUnitFoodUsed[0x26];
    banker_staging_supplycost_g[bPlayer] = nStaged;
  }

  const BYTE bRace = (BYTE)count_race(nUnitType);
  const int nUsed = count_get_supplies_used(bRace, bPlayer)
                  + banker_staging_supplycost_g[bPlayer];
  const int nAvail = (int)count_food_available(nUnitType, bPlayer)
                   - banker_staging_supplycost_g[bPlayer];
  if (nUsed > (int)count_food_max(nUnitType, bPlayer)) {
    if (bShowError) {
      const int nRace = bRace;
      gamesnd_show_player_error(bPlayer, (WORD)(nRace + 1), nRace + 0x34F);
    }
    return 0;
  }
  if (nAvail < 0) {
    if (bShowError) {
      const int nRace = bRace;
      gamesnd_show_player_error(bPlayer, (WORD)(nRace + 0x99), nRace + 0x34C);
    }
    return 0;
  }
  return 1;
}

// 0x4085E0 (105 B). `jnl` on both compares makes the staged cost and the player
// balance signed ints. The second guard's no-error branch jumps back to the
// first one's `xor eax, eax` epilogue, which is MSVC's tail sharing of two
// `return 0`s and needs no source expression of its own.

// The four bytes this body was once short of were in the callee's declaration,
// not here. The oracle emits, twice,
//     movzx edx, byte ptr [0x5264C2]
//     mov   eax, 93h
//     push  352h
//     add   dx, ax                 <- a 16-bit add, 8 B for the pair
// where a 32-bit `add edx, 93h` is 6 B. MSVC only reaches the load-then-16-bit-
// add form when the destination really is 16 bits, dodging the
// length-changing-prefix stall of `66 81 c2 93 00`, so the argument's type is 16
// bits. gamesnd_show_player_error shows it too: its body opens `movzx esi, dx`
// and never reads EDX wider. src117/h/CUnitHarvest.h said `int a2` and now says
// WORD, which is 2 B in each of the two blocks. The same emit appears in
// build_check_money (0x41BA50, CUnitBuild.cpp).
int bldg_check_resources(BYTE bPlayer, int bShowError) {
  if (gPlayerMinerals[bPlayer] < banker_staging_orecost_g[bPlayer]) {
    if (bShowError)
      gamesnd_show_player_error(bPlayer, byte_5264C2 + 0x93, 0x352);
    return 0;
  }
  if (gPlayerGas[bPlayer] < banker_staging_gascost_g[bPlayer]) {
    if (bShowError)
      gamesnd_show_player_error(bPlayer, byte_5264C2 + 0x96, 0x353);
    return 0;
  }
  return 1;
}

// 0x408650 (32 B). `movzx ecx, cl` is the one parameter being zero-extended for
// use as an index, which is what makes it a BYTE rather than an int.
void bldg_deduct_resources(BYTE bPlayer) {
  gPlayerMinerals[bPlayer] -= banker_staging_orecost_g[bPlayer];
  gPlayerGas[bPlayer] -= banker_staging_gascost_g[bPlayer];
}

// 0x408670 (64 B). The two techdata reads are written out rather than calling
// GetTechCost: the oracle carries neither of GetTechCost's null guards here, and
// writing the block out takes the inline decision away from the compiler.
int bldg_get_tech_cost(BYTE bPlayer, tech_id_t bTechId, int nQuiet) {
  banker_staging_orecost_g[bPlayer] = techdata_oreCost_g[bTechId];
  banker_staging_gascost_g[bPlayer] = techdata_gasCost_g[bTechId];
  banker_staging_supplycost_g[bPlayer] = 0;
  return bldg_check_resources(bPlayer, nQuiet);
}

// 0x4086B0 (86 B). Stage the unit's mineral and gas cost, then check supply
// (when asked) and resources. The early exit returns bldg_check_food's own EAX.
// The unit type is an int here: the body forwards EDX whole to bldg_check_food.
// Callers pass a WORD (town_pending_costs moves GetBuildUnit's 16-bit AX with no
// widening), so SAI_Banker.h keeps that view and C linkage joins the two.
extern "C" BOOL __fastcall bldg_get_unit_cost(BYTE bPlayer, int nUnitType,
                                              BOOL bCheckFood, BOOL bShowError) {
  banker_staging_orecost_g[bPlayer] = guwUnitMineralsCost[(WORD)nUnitType];
  banker_staging_gascost_g[bPlayer] = guwUnitGasesCost[(WORD)nUnitType];
  if (bCheckFood) {
    const BOOL bOk = bldg_check_food(bPlayer, nUnitType, bShowError);
    if (!bOk)
      return bOk;
  }
  return bldg_check_resources(bPlayer, bShowError);
}

// 0x408710 (73 B). `mov bh, cl` / `mov bl, dl` packs both parameters into one
// callee-saved register across the two calls, which is what BYTE, BYTE means
// here; an int would have needed two registers or two spills.
int bldg_get_upgrade_cost(BYTE bPlayer, upgrade_id_t bUpgradeId, int nQuiet) {
  banker_staging_orecost_g[bPlayer] = get_upgrade_ore_cost(bPlayer, bUpgradeId);
  banker_staging_gascost_g[bPlayer] = get_upgrade_gas_cost(bPlayer, bUpgradeId);
  banker_staging_supplycost_g[bPlayer] = 0;
  return bldg_check_resources(bPlayer, nQuiet);
}

// 0x408760 / 0x408770 / 0x408780 (8 B each). Leaves this small come out the
// same from any spelling, so matching one says nothing about the rest.
unsigned get_staged_food_cost(int nation) {
  return banker_staging_supplycost_g[nation];
}

unsigned get_staged_gas_cost(int nation) {
  return banker_staging_gascost_g[nation];
}

unsigned get_staged_ore_cost(int nation) {
  return banker_staging_orecost_g[nation];
}

// 0x408790 (38 B). EAX at the ret is the last load's leftover, and no caller
// reads it, so the return is void.
void GetTechCost(int id, DWORD *outOre, DWORD *outGas) {
  if (outOre)
    *outOre = techdata_oreCost_g[id];
  if (outGas)
    *outGas = techdata_gasCost_g[id];
}

// 0x4087C0 (84 B). `mov si, cx` is the parameter itself, not a truncation of
// it. With a fully defined ECX, MSVC copies it whole as `mov esi, ecx`, 8b f1,
// two bytes, and the body comes out 83 against the oracle's 84. The oracle's
// `66 8b f1` moves only SI, which is what MSVC emits when the upper half of ECX
// is not part of the value, so the parameter is 16 bits. The three
// `movzx eax, si` are that WORD zero-extended once per index, with no shared
// 32-bit copy to hoist. Only this derivation settles the width on its own: a
// `mov si, cx` prologue has no int spelling at all, where bldg_get_unit_cost's
// 86 bytes come out of two different chains.
void GetUnitCost(WORD unitType, DWORD *outOre, DWORD *outGas, BYTE *outSupply) {
  const WORD wUnit = unitType;
  if (outOre)
    *outOre = guwUnitMineralsCost[wUnit];
  if (outGas)
    *outGas = guwUnitGasesCost[wUnit];
  if (outSupply) {
    BYTE bSupply = gubUnitFoodUsed[wUnit];
    *outSupply = bSupply;
    if (gulUnitTypeFlags[wUnit] & 0x400) {
      bSupply = (BYTE)(bSupply + bSupply);
      *outSupply = bSupply;
    }
  }
}

// 0x408820 (62 B). The first call takes ECX and EDX as they arrive and the
// second reloads `mov dl, bl / mov cl, al` from the spill: the nation is spilled
// to [ebp-4] across the first call, so the frame has one local slot.
void GetUpgradeCost(int nation, int id, DWORD *outOre, DWORD *outGas) {
  if (outOre)
    *outOre = get_upgrade_ore_cost((BYTE)nation, (BYTE)id);
  if (outGas)
    *outGas = get_upgrade_gas_cost((BYTE)nation, (BYTE)id);
}

