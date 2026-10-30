// SAI_Banker.cpp -- the AI banker: the per-player record that tracks what the
// computer player is currently trying to afford, what it has staged, what it
// wants defended, and the timers that pace all of it. The aiscript_* entry
// points are the opcodes the AI script VM dispatches into; a sub_* name is
// positional and means only that no approved naming source names it. Bodies are
// in oracle VA order.
//
// gAIBanker is 8 x 0x4E8 at 0x67D068. Its layout and every offset's citation
// live in src117/h/SAI_Banker.h; read that header before adding a field here.

// The TU compiles against `/I src117/h` alone, with no reference-tree header on
// the path, and is meant to stay that way. Globals come from the generated
// dual-mode header ../gen/globals117.h, never a local `extern`, with the two
// flagged exceptions below, and that header must sit after the game headers,
// because its `extern AI_Banker gAIBanker[8]` needs AI_Banker complete.

// Fifteen bodies are byte-identical to the oracle under the plain compile and
// emit different bytes under /DOC_DLL_MODE, the form the injected payload
// ships. None is reshaped to make the DLL branch agree: that would steer the source
// away from the form the oracle is proven against. What decides it is whether the source
// materialises a base MSVC can either keep in a register
// (`add esi, <symbol+off>`) or dissolve into every operand's disp32 -- with a
// relocatable symbol only the first is available, with a baked literal both
// are. The nine pointer getters materialise an address and never dereference
// it, so both forms coincide for them.

// The three ai_stage_*_cost bodies are size-exact and instruction-for-
// instruction identical to the oracle and still differ in register allocation
// and in where the shared `return 0` block lands.

typedef int                 BOOL;
typedef unsigned char       BYTE;
typedef unsigned short      WORD;
#include "platform.h"  // DWORD

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../port/data/port_formats.h"  // Hyperion: PortSave117Decompress, PortSave117Get, PortSave117Compress
#include "../gen/save117.h"
#include <stddef.h>        // offsetof -- the layout static_asserts in the headers
#include <string.h>        // memset -- /Oi expands the 40 B fills inline
                           // (xorps/movdqu/movq) and leaves the 128 B one a real
                           // __cdecl call, exactly as the oracle does

#include "CUnit.h"         // bPlayer +0x4C, wType +0x64, pPlayerUnitNext +0x6C,
                           // GetUnitID / FromId / GetPlayerUnitList
#include "SAI_Banker.h"    // AI_Banker, AI_GenCmd -- the layout and its proof
#include "net_cmds.h"      // PlayerData -- gPlayerData indexes it
#include "saveload.h"      // compress / decompress -- the save-file codec pair
                           // sai_read/write_banker_data drive
#include "Storm.h"         // SMemAlloc / SMemFree -- SAI_Banker_LoadFromSave's
                           // scratch buffer for the legacy record
#include "../h/port_hxsave.h"  // Hyperion: HxSaveLoading
#include "../port/gameplay/port_ai.h"    // Hyperion: AiDifficulty
#include "../gen/globals117.h"

// The two globals this TU declares itself. Both are debts rather than a
// pattern, and both are spelled exactly as the generator spells its own output,
// so that closing either is a deletion and nothing else. Neither VA may be
// registered for generation while the block below survives, or the generated
// declaration and this one collide.
//
// dword_67F7AC is reachable only from banker_start_nation, a dll-mode
// divergence, so no generated row lists it. Its width comes from
// sai_banker_init's `mov dword ptr [67F7ACh], 1h`; banker_start_nation reads
// only the low byte (`mov al, byte ptr [67F7ACh]`, the A0 moffs8 form). It sits
// immediately after gAIBanker's end -- 0x67D068 + 0x2740 == 0x67F7A8.
#ifdef OC_DLL_MODE
#define dword_67F7AC (*reinterpret_cast<int*>(OC_VA(0x67F7ACu)))
#else
extern int dword_67F7AC;
#endif

// sgCaptain @0x6B1A74 is declared here because re/globals117.json carries no
// type for it, and an untyped global cannot be generated. Every site loads it as
// `dword ptr [<player>*4 + 6B1A74h]`, so the element size is 4, one per player;
// the count 8 comes from the player count, not from a bound check in the
// instruction stream. To close it: give 0x6B1A74 the type `TAI_Captain*[8]`,
// register it for banker_captain_for_request, and delete this block.
//
// TAI_Captain and SAI_Request, the two records banker_captain_for_request's
// signature names, live in src117/h/SAI_Banker.h, because a hooked symbol's
// header is resolved from the symbol and needs those declarations to compile.
#ifdef OC_DLL_MODE
#define sgCaptain (reinterpret_cast<TAI_Captain**>(OC_VA(0x6B1A74u)))
#else
extern PlayerTable<TAI_Captain *, PlayerTagCaptains, 8> sgCaptain;
#endif

// The functions, in oracle VA order, which is declaration order.

// The load-side counterpart of sai_write_banker_data.
// The second register argument is a WORD, tested at 16 bits. The memset clears the demand
// heap, bytes 0x018..0x20F of each record, in one call.
int __fastcall sai_read_banker_data(void *pFile, WORD wVersion)
{
  unsigned i;
  int j;
  int nBias;

  if (wVersion <= 2) {
    if (!SAI_Banker_LoadFromSave(pFile))
      return 0;
  } else {
    if (!PortSave117Decompress(SAVE117_Banker, gAIBanker.a, 8, pFile))  // Hyperion
      return 0;
    // Hyperion: the nations past 7
    if (g_hxPlayers && !PortSave117Decompress(SAVE117_Banker, gAIBanker.Extra(), gAIBanker.kExtraCount, pFile))
      return 0;
  }

  for (i = 0; i < (unsigned)PlayerLobbySlots(); i++)  // Hyperion
    gAIBanker[i].pMedic = CUnit::FromId((DWORD)(DWORD_PTR)gAIBanker[i].pMedic);

  // 32-bit local, 16-bit TEST. `xor edi, edi / mov eax, 0B0h / cmovz edi, eax`
  // leaves the bias in a full register with no zero-extension, which a WORD
  // local does not produce (it costs a `movzx edi, ax`, +14 B measured), while
  // the loop's own guard is `test di, di` -- 16 bits.
  nBias = (wVersion == 0) ? 0xB0 : 0;

  // The loop counts down with the decrement at the top: the oracle's cursor
  // starts at 0x67F9C2, one past the last record's wPanicBlock, `sub esi, 4E8h`
  // is the body's first instruction, and the exit compare is against
  // 0x67D282 == &gAIBanker[0].wPanicBlock. `for (j = 7; j >= 0; j--)` puts the
  // decrement at the bottom and shifts every following byte (same size, 5 byte
  // deltas); the pointer forms cost 5-7 B more.
  j = PlayerLobbySlots();  // Hyperion: 8 in 1.17
  do {
    --j;
    // Hyperion: a Hyperion save keeps the demands, so its game goes on as saved
    if (!HxSaveLoading()) {
      memset(gAIBanker[j].aDemands, 0, sizeof(gAIBanker[j].aDemands));
      gAIBanker[j].bDemandCount = 0;
    }
    if ((WORD)nBias) {
      WORD w = gAIBanker[j].wPanicBlock;
      if (w)
        gAIBanker[j].wPanicBlock = w + nBias;
    }
  } while (j != 0);

  return 1;
}

// The legacy save reader: the pre-version-3 file holds
// eight 0x4D4-byte records back to back (0x26A0 == 8 * 0x4D4), each an AI_Banker truncated
// just before pMedic (0x4D4 is offsetof(pMedic)), so bUnk22F is re-seeded by hand afterwards.
// The line numbers 296, 301 and 322 are 1.17's __LINE__ literals, written out. The failure
// free's flags argument is decompress's own result, not a 0.
int __fastcall SAI_Banker_LoadFromSave(void *pFile)
{
  unsigned i;
  BYTE *pBuf;
  int nOk;

  pBuf = (BYTE *)SMemAlloc(0x26A0, "SAI_Banker.cpp", 296, 8);
  nOk = decompress(pBuf, 0x26A0, pFile);
  if (!nOk) {
    SMemFree(pBuf, "SAI_Banker.cpp", 301, nOk);
    return 0;
  }

  memset(gAIBanker.a, 0, 8 * sizeof(AI_Banker));
  for (i = 0; i < 8; i++) {
    // Hyperion: 1.17's record up to pMedic, completed with zeros to its
    // 0x4E8 bytes and translated.
    BYTE ab117[0x4E8] = {0};
    memcpy(ab117, pBuf + i * 0x4D4, 0x4D4);
    PortSave117Get(SAVE117_Banker, ab117, &gAIBanker[i]);
    gAIBanker[i].bUnk22F = 1;
  }

  SMemFree(pBuf, "SAI_Banker.cpp", 322, 0);
  return 1;
}


// The save-side pointer swizzle: turn every banker's
// pMedic into a unit id, hand the whole array to compress, then turn the ids back into
// pointers. The id is stored as a full EAX, so CUnit::GetUnitID returns `int`.
int __fastcall sai_write_banker_data(void *pFile)
{
  unsigned i;      // Unsigned is load-bearing: `int` costs 11 B. The oracle
                   // strength-reduces to a byte offset (`xor esi, esi` /
                   // `mov ecx, [esi+67D53Ch]` / `cmp esi, 2740h` / `jb`),
                   // which is the unsigned shape; a signed counter makes cl
                   // fold the base into the pointer and compare with `jl`,
                   // shortening the prologue and losing the loop-alignment
                   // `jmp $+0x8` with it.
  int nResult;

  for (i = 0; i < (unsigned)PlayerLobbySlots(); i++)  // Hyperion: every nation in a Hyperion game
    gAIBanker[i].pMedic = (CUnit *)(intptr_t)gAIBanker[i].pMedic->GetUnitID();

  nResult = PortSave117Compress(SAVE117_Banker, gAIBanker.a, 8, pFile);  // Hyperion
  if (g_hxPlayers)  // Hyperion: the nations past 7
    nResult = PortSave117Compress(SAVE117_Banker, gAIBanker.Extra(), gAIBanker.kExtraCount, pFile) ? nResult : 0;

  for (i = 0; i < (unsigned)PlayerLobbySlots(); i++)  // Hyperion: every nation in a Hyperion game
    gAIBanker[i].pMedic = CUnit::FromId((DWORD)(DWORD_PTR)gAIBanker[i].pMedic);

  return nResult;
}

// Pure dispatcher, no banker fields.
// __fastcall + `ret 10h` == 2 register params and 4 stack params. The BYTE
// out-param is zeroed by the caller on the upgrade/tech paths and passed
// through on the unit path, which is exactly how banker_charge_plan_cost and banker_demand_nuke use
// the same three callees.
void __fastcall sai_banker_GetPlanCost(int nation, int planType, int id,
                                       DWORD *outOre, DWORD *outGas,
                                       BYTE *outSupply) {
  if (planType == 5) {
    *outSupply = 0;
    return GetUpgradeCost(nation, id, outOre, outGas);
  }
  if (planType == 6) {
    *outSupply = 0;
    return GetTechCost(id, outOre, outGas);
  }
  return GetUnitCost(id, outOre, outGas, outSupply);
}

// The demand heap as sai_add_demand and sai_pop_demand walk it.
static SAI_Demand *demand_heap(AI_Banker *pBanker)
{
  return pBanker->aDemands;
}

// Push one demand onto the max-heap and sift
// it up. Kinds 1/2/4/8 are gated by banker_at_define_max; MSVC compresses the 1..8 switch
// into a byte index table plus a two-entry jump table. At 0x3F the heap is full
// and the last slot is overwritten in place, count unchanged.
void __fastcall sai_add_demand(int nation, BYTE kind, int unitType, BYTE a4,
                               intptr_t a5)
{
  AI_Banker *pBanker;
  SAI_Demand d;
  SAI_Demand tmp;
  BYTE bCount;
  int nIdx;
  int nParent;

  switch (kind) {
  case 1:
  case 2:
  case 4:
  case 8:
    if (banker_at_define_max(nation, unitType))
      return;
    break;
  }

  pBanker = &gAIBanker[nation];

  d.bPrio = a4;
  d.bKind = kind;
  d.wArg = (WORD)unitType;
  d.dwCaptain = a5;
  bCount = pBanker->bDemandCount;
  if (bCount == 0x3F) {
    nIdx = 0x3E;
  } else {
    nIdx = bCount;
    pBanker->bDemandCount = bCount + 1;
  }

  nParent = (nIdx - 1) / 2;
  demand_heap(pBanker)[nIdx] = d;
  while (nIdx > 0) {
    if (demand_heap(pBanker)[nIdx].bPrio <= demand_heap(pBanker)[nParent].bPrio)
      break;
    tmp = demand_heap(pBanker)[nIdx];
    demand_heap(pBanker)[nIdx] = demand_heap(pBanker)[nParent];
    demand_heap(pBanker)[nParent] = tmp;
    nIdx = nParent;
    nParent = (nIdx - 1) / 2;
  }
}

// Five `__declspec(noinline)` levers, not to be tidied away.
// get_player_worker_type, get_player_refinery_type, aiscript_count_units,
// banker_charge_plan_cost and banker_captain_for_request each carry the
// attribute on both their definition below and their declaration in
// src117/h/SAI_Banker.h. Without it, /Ob2 (implied by /Ox) splices each of them
// into the bodies that call them, where the oracle emits a real `call`:

//     banker_worker_bonus_allowed   63 B oracle -> 155 B  (get_player_worker_type + aiscript_count_units)
//     banker_start_plan  122 B oracle -> 161 B  (get_player_worker_type)
//     banker_demand_guard_unit  150 B oracle -> 167 B  (banker_captain_for_request)
//     banker_demand_structure  128 B oracle -> 157 B  (get_player_refinery_type + banker_charge_plan_cost)

// The attribute is our mechanism and says nothing about Blizzard's source: a
// separate TU for either side, or a different /Ob setting, produces the same
// `call`, and nothing in the bytes tells the three apart. It is the same lever
// statbtn.cpp carries on bf_can_attack. Definition order is not an alternative,
// since cl 18 defers and will inline a callee whose body appears later in the
// file. All five are byte-identical with the attribute and without it.


// __fastcall with two register and two stack
// parameters. The count goes through one `(BYTE)(count - 1)` expression, so its slot is
// 32-bit.
void AddPrereqUnitToAINeeds(int nation, int unitType, int nCount, intptr_t a4)
{
  DWORD dwOre, dwGas;
  BYTE bSupply;

  if (unitType == 0x2C || unitType == 0x3E) {
    GetUnitCost(0x2B, &dwOre, &dwGas, &bSupply);
    gAIBanker[nation].dwCostOre += dwOre;
    gAIBanker[nation].dwCostGas += dwGas;
    gAIBanker[nation].dwCostSupply += bSupply;
    sai_add_demand(nation, 1, 0x2B, (BYTE)(nCount - 1), a4);
  } else if (unitType == 0x44) {
    banker_charge_unit_cost(nation, 0x43);
    sai_add_demand(nation, 1, 0x43, (BYTE)(nCount - 1), a4);
    banker_charge_unit_cost(nation, 0x43);
    sai_add_demand(nation, 1, 0x43, (BYTE)(nCount - 1), a4);
  } else if (unitType == 0x3F) {
    banker_charge_unit_cost(nation, 0x3D);
    sai_add_demand(nation, 1, 0x3D, (BYTE)(nCount - 1), a4);
    banker_charge_unit_cost(nation, 0x3D);
    sai_add_demand(nation, 1, 0x3D, (BYTE)(nCount - 1), a4);
  } else if (unitType == 0x67) {
    banker_charge_unit_cost(nation, 0x26);
    sai_add_demand(nation, 1, 0x26, (BYTE)(nCount - 1), a4);
  }
}

// Refund the top demand's cost back out of
// the banker, saturating at zero. The five early-exit types are the same set
// AddPrereqUnitToAINeeds special-cases, in the oracle's own compare order.
void __fastcall banker_refund_top_demand(int nation, int alsoDebit)
{
  AI_Banker *pBanker = &gAIBanker[nation];
  unsigned nId = pBanker->aDemands[0].wArg;
  if (nId == 0x2C || nId == 0x67 || nId == 0x3E || nId == 0x44 || nId == 0x3F)
    return;

  DWORD dwOre, dwGas;
  BYTE bSupply;
  sai_banker_GetPlanCost(nation, pBanker->aDemands[0].bKind, nId, &dwOre, &dwGas,
                         &bSupply);

  if (pBanker->dwCostOre < dwOre)
    pBanker->dwCostOre = 0;
  else
    pBanker->dwCostOre -= dwOre;
  if (pBanker->dwCostGas < dwGas)
    pBanker->dwCostGas = 0;
  else
    pBanker->dwCostGas -= dwGas;
  if (pBanker->dwCostSupply < bSupply)
    pBanker->dwCostSupply = 0;
  else
    pBanker->dwCostSupply -= bSupply;

  if (alsoDebit) {
    pBanker->dwAvailOre -= dwOre;
    pBanker->dwAvailGas -= dwGas;
    pBanker->dwAvailSupply -= bSupply;
  }
}

// Pop the max-priority demand: overwrite the root with the last entry, then
// sift down. The heap subscripts must stay spelled out -- a named head local
// puts the pointer in EBX where the oracle has EAX.
void __fastcall sai_pop_demand(int nation)
{
  AI_Banker *pBanker = &gAIBanker[nation];
  BYTE bLast = pBanker->bDemandCount;
  BYTE i;

  bLast--;
  pBanker->bDemandCount = bLast;
  if (bLast == 0)
    return;
  demand_heap(pBanker)[0] = demand_heap(pBanker)[bLast];
  if (pBanker->bDemandCount == 1)
    return;

  i = 0;
  while (2 * i + 1 <= pBanker->bDemandCount - 1) {
    unsigned nCount = pBanker->bDemandCount;
    int nLeft = 2 * i + 1;
    int nBest = nLeft + 1;
    SAI_Demand tmp;

    if (nBest >= nCount ||
        demand_heap(pBanker)[nLeft].bPrio > demand_heap(pBanker)[nBest].bPrio)
      nBest = nLeft;
    if (demand_heap(pBanker)[i].bPrio >= demand_heap(pBanker)[nBest].bPrio)
      break;
    tmp = demand_heap(pBanker)[i];
    demand_heap(pBanker)[i] = demand_heap(pBanker)[nBest];
    demand_heap(pBanker)[nBest] = tmp;
    i = (BYTE)nBest;
  }
}

// Nullary: no register input, no stack parameters and
// a plain `ret`. It forms the banker's address once and touches all nine fields off it.
// Write `b->bDemandCount = 0` through the member, not as a byte offset off a cast pointer,
// which MSVC schedules differently.
void __fastcall banker_start_plan()
{
  AI_Banker *b = &gAIBanker[gActiveNation];
  b->dwAvailOre = gPlayerMinerals[gActiveNation];
  b->wFlags |= 1;
  b->dwCostOre = 0;
  b->dwCostGas = 0;
  b->dwCostSupply = 0;
  b->bDemandCount = 0;
  b->dwAvailGas = gPlayerGas[gActiveNation];
  town_pending_costs(gActiveNation, &b->dwAvailOre, &b->dwAvailGas);
  b->dwAvailSupply = count_food_available(get_player_worker_type(gActiveNation),
                                          (BYTE)gActiveNation);
  b->bUnk223 = 9;
}

// CUnit* in ECX; bPlayer indexes the banker.
BOOL __fastcall banker_is_player_medic(CUnit *unit) {
  return unit == gAIBanker[unit->bPlayer].pMedic;
}

int __fastcall banker_plan_is_secure(int nation) {
  return gAIBanker[nation].wFlags & 1;
}

void __fastcall banker_start_nation(int nation) {
  gAIBanker[nation].wFlags |= 2;
  // Hyperion: 1.17's difficulty (always 1), which a computer's profile may change
  gAIBanker[nation].bUnk22F = AiDifficulty(nation, (BYTE)dword_67F7AC);
}

// Scan aDefenseUseGG for unitType+1, stop at the
// first empty slot.
int __fastcall banker_has_defenseuse_gg(int nation, int unitType) {
  WORD *p = gAIBanker[nation].aDefenseUseGG;
  WORD v = *p;
  while (v != 0) {
    ++p;
    if (v == unitType + 1)
      return 1;
    v = *p;
  }
  return 0;
}

// ---- 12 B: pointer returners. The shape is `add eax, <addr>` then `ret` --
// the address is returned, not dereferenced, so these hand back the array
// itself. A value getter would have emitted movzx/mov from the slot. They are
// also the control group for the dll-mode trigger: zero dereferences, so there
// is nothing for MSVC to fold the baked literal into, and both forms agree.
WORD *__fastcall get_defensebuild_aa(int nation) { return gAIBanker[nation].aDefenseBuildAA; }

WORD *__fastcall get_defensebuild_ga(int nation) { return gAIBanker[nation].aDefenseBuildGA; }

WORD *__fastcall get_defensebuild_ag(int nation) { return gAIBanker[nation].aDefenseBuildAG; }

WORD *__fastcall get_defensebuild_gg(int nation) { return gAIBanker[nation].aDefenseBuildGG; }

// Registers the player's Terran Medic with
// its banker; first one wins.
void __fastcall banker_register_medic(CUnit *pUnit) {
  int nPlayer = pUnit->bPlayer;
  if (gPlayerData[nPlayer].nType != 1)     // 1 == computer-controlled
    return;
  if (pUnit->wType != 34)                  // 34 == Terran Medic
    return;
  if (gAIBanker[nPlayer].pMedic == 0)
    gAIBanker[nPlayer].pMedic = pUnit;
}

// The 88 B defensebuild/defenseuse body, eight times over. __fastcall with
// nation in ECX, count in EDX and unitType on the stack ([ebp+8], `ret 4`).
// `cmp edx,0Ah / jnbe` and `cmp edi,0E4h / jnb` are unsigned compares, hence
// the unsigned parameters: signed types flip the branch sense.
//
// All eight diverge under /DOC_DLL_MODE, identically (only the array's own
// displacement differs): 88 B here, where `add esi,<array offset>`
// materialises the base once and the operands are disp0/disp8 off ESI, against
// 92 B there, where the `add` is gone and each of the three array operands
// carries the full disp32.
void __fastcall aiscript_defensebuild_aa(int nation, unsigned count, unsigned unitType) {
  WORD *arr = gAIBanker[nation].aDefenseBuildAA;
  if (count > 10)
    return;
  if (unitType >= 228)
    return;
  unsigned i = 0;
  while (arr[i] != 0) {
    if (i >= 20)
      return;
    ++i;
  }
  while (count != 0) {
    --count;
    if (i >= 20)
      return;
    arr[i] = (WORD)(unitType + 1);
    ++i;
  }
}

void __fastcall aiscript_defensebuild_ga(int nation, unsigned count, unsigned unitType) {
  WORD *arr = gAIBanker[nation].aDefenseBuildGA;
  if (count > 10)
    return;
  if (unitType >= 228)
    return;
  unsigned i = 0;
  while (arr[i] != 0) {
    if (i >= 20)
      return;
    ++i;
  }
  while (count != 0) {
    --count;
    if (i >= 20)
      return;
    arr[i] = (WORD)(unitType + 1);
    ++i;
  }
}

void __fastcall aiscript_defensebuild_ag(int nation, unsigned count, unsigned unitType) {
  WORD *arr = gAIBanker[nation].aDefenseBuildAG;
  if (count > 10)
    return;
  if (unitType >= 228)
    return;
  unsigned i = 0;
  while (arr[i] != 0) {
    if (i >= 20)
      return;
    ++i;
  }
  while (count != 0) {
    --count;
    if (i >= 20)
      return;
    arr[i] = (WORD)(unitType + 1);
    ++i;
  }
}

void __fastcall aiscript_defensebuild_gg(int nation, unsigned count, unsigned unitType) {
  WORD *arr = gAIBanker[nation].aDefenseBuildGG;
  if (count > 10)
    return;
  if (unitType >= 228)
    return;
  unsigned i = 0;
  while (arr[i] != 0) {
    if (i >= 20)
      return;
    ++i;
  }
  while (count != 0) {
    --count;
    if (i >= 20)
      return;
    arr[i] = (WORD)(unitType + 1);
    ++i;
  }
}

// Whether the player's banked ore, gas and supply cover the costs just staged.
// A static helper inlined into each of the three below: the inline's own copy
// of the player index is what puts it in EBX and the product in ESI, and the
// shared `return 0` after the first compare. Written out in each body, MSVC
// takes ESI/EDI and moves that block.
static BOOL banker_covers_staged(int nation) {
  if (gAIBanker[nation].dwAvailOre < get_staged_ore_cost(nation))
    return 0;
  if (gAIBanker[nation].dwAvailGas < get_staged_gas_cost(nation))
    return 0;
  return gAIBanker[nation].dwAvailSupply >= get_staged_food_cost(nation);
}

// Stage a unit's costs and report whether the player can pay them.
int __fastcall ai_stage_unit_cost(int nation, int id) {
  if (!bldg_get_unit_cost(nation, id, 1, 1))
    return 0;
  return banker_covers_staged(nation);
}

// Stage a tech's costs and report whether the player can pay them.
int __fastcall ai_stage_tech_cost(int nation, int id) {
  if (!bldg_get_tech_cost((BYTE)nation, (BYTE)id, 0))
    return 0;
  return banker_covers_staged(nation);
}

// Stage an upgrade's costs and report whether the player can pay them.
int __fastcall ai_stage_upgrade_cost(int nation, int id) {
  if (!bldg_get_upgrade_cost((BYTE)nation, (BYTE)id, 0))
    return 0;
  return banker_covers_staged(nation);
}

// The three bodies below are guarded on OC_PAYLOAD, not OC_DLL_MODE, and the
// distinction matters. OC_DLL_MODE selects the globals form and nothing else:
// every compile agrees about which bodies exist, so guarding a body on it would
// delete the body from the `--dll-mode --abs-literal` gate, which is the only
// thing that measures the fifteen divergences.

// OC_PAYLOAD is set only by tools/build_oc117_cpp.bat and means "this compile
// is being linked into oc117_cpp.dll". It guards the three bodies that cannot
// link there: only the callees of ported rows get a thunk, so
// bldg_get_{tech,upgrade}_cost and get_staged_*_cost have none, and they are
// reachable only from the ai_stage_*_cost trio. The other non-landing bodies
// link fine and are simply never trampolined -- dead code in the DLL.

// The define was measured, not assumed: every landing COMDAT compiled with it
// is byte-identical to the same COMDAT compiled without it, so it removes
// definitions and perturbs nothing else. Re-measure if this list changes.

// The return is int, not the BYTE field's type:
// the load is a movzx, which a BYTE return would not emit.
int __fastcall banker_request_budget(int nation) { return gAIBanker[nation].bUnk223; }

// ---- 0x4827D0..0x4828C0: aiscript_defenseclear_{aa,ga,ag,gg}, 58 B each.
// One shape at four offset pairs: two 40-byte zero-fills, the build array then
// the matching use array. /Oi expands each memset inline as
// `xorps xmm0,xmm0 / movdqu / movdqu / movq`, and the single
// `imul eax,ecx,4E8h` is shared by both. These take the address of a struct
// field and still match in both forms -- memset's inline expansion writes
// through the base once per store with no reload, so there is no second
// dereference for the literal to fold into.
void __fastcall aiscript_defenseclear_aa(int nation) {
  memset(gAIBanker[nation].aDefenseBuildAA, 0, sizeof(gAIBanker[nation].aDefenseBuildAA));
  memset(gAIBanker[nation].aDefenseUseAA, 0, sizeof(gAIBanker[nation].aDefenseUseAA));
}

void __fastcall aiscript_defenseclear_ga(int nation) {
  memset(gAIBanker[nation].aDefenseBuildGA, 0, sizeof(gAIBanker[nation].aDefenseBuildGA));
  memset(gAIBanker[nation].aDefenseUseGA, 0, sizeof(gAIBanker[nation].aDefenseUseGA));
}

void __fastcall aiscript_defenseclear_ag(int nation) {
  memset(gAIBanker[nation].aDefenseBuildAG, 0, sizeof(gAIBanker[nation].aDefenseBuildAG));
  memset(gAIBanker[nation].aDefenseUseAG, 0, sizeof(gAIBanker[nation].aDefenseUseAG));
}

void __fastcall aiscript_defenseclear_gg(int nation) {
  memset(gAIBanker[nation].aDefenseBuildGG, 0, sizeof(gAIBanker[nation].aDefenseBuildGG));
  memset(gAIBanker[nation].aDefenseUseGG, 0, sizeof(gAIBanker[nation].aDefenseUseGG));
}

//   cmp dl, 4h    <- BYTE-width compare: the parameter is 8-bit
// 0x80 does not fit a sign-extended imm8, so the constant is materialised
// (`mov ecx, 80h` / `or word ptr [...], cx`) instead of folded. Compare
// aiscript_set_farms_timing, where 0x10 does fit and is folded: the rule is
// [0, 0x7F], and it holds at every AI_Banker flag write here.
void __fastcall aiscript_creep(int nation, BYTE bValue) {
  if (bValue == 4)
    gAIBanker[nation].wFlags |= 0x80;
  else
    gAIBanker[nation].wFlags &= ~0x80;
}

// Consume one tick of a countdown, then
// clear bit 6.
void __fastcall banker_consume_request_budget(int nation) {
  WORD f = gAIBanker[nation].wFlags;
  if (f & 0x40) {
    BYTE c = gAIBanker[nation].bUnk223;
    if (c != 0)
      gAIBanker[nation].bUnk223 = c - 1;
    f &= ~0x40;
    gAIBanker[nation].wFlags = f;
  }
}

// Charge one unit's cost to the nation's
// running totals, then register the demand.
void __fastcall banker_demand_nuke(int nation, int unitType, int a3) {
  DWORD ore, gas;
  BYTE supply;
  GetUnitCost(unitType, &ore, &gas, &supply);
  gAIBanker[nation].dwCostOre += ore;
  gAIBanker[nation].dwCostGas += gas;
  gAIBanker[nation].dwCostSupply += supply;
  sai_add_demand(nation, 8, unitType, a3, 0);
}

// __fastcall with two register and four stack
// parameters. MSVC parks two of the three locals in the dead a5 and a6 parameter slots.
void __fastcall banker_demand_unit_for_captain(int nation, intptr_t a2, int unitType, int a4,
                           int a5, int a6)
{
  DWORD dwOre, dwGas;
  BYTE bSupply;

  if (!a6 && (gAIBanker[nation].wFlags & 0x200))
    return;
  if (!a5)
    gAIBanker[nation].wFlags &= 0xFFFE;

  gAIBanker[nation].wFlags |= 0x40;
  GetUnitCost(unitType, &dwOre, &dwGas, &bSupply);
  gAIBanker[nation].dwCostOre += dwOre;
  gAIBanker[nation].dwCostGas += dwGas;
  gAIBanker[nation].dwCostSupply += bSupply;

  sai_add_demand(nation, 1, unitType, a4, a2);
  AddPrereqUnitToAINeeds(nation, unitType, a4, a2);
}

// The stamp-staleness predicate, and the inlining of it is visible in the
// bytes. banker_demand_guard_unit and banker_demand_structure both open with
//     cmp eax, 1Eh / sbb eax, eax / inc eax / j(n)z
// -- the predicate materialised into EAX and only then tested. The same
// condition written inline as `stamp != 0 && !(g_dwElapsedTime - stamp >= 30)`
// folds into `cmp eax,1Eh / jae` and costs exactly the 3 bytes (sbb+inc) both
// bodies were short. A helper call that MSVC inlines is what materialises it:
// the constant `return 1` path short-circuits to a plain branch with EAX never
// set, which is what the oracle does on the `t == 0` edge, while the computed
// path goes through the return slot. Six spellings were probed and only this
// one produced sbb/inc.

// The body is banker_request_throttle_passed's, which stands on its own at
// 0x483570: one source predicate, emitted out of line once and inlined at these
// two call sites. `static` so no second COMDAT is emitted.
static int banker_stamp_stale(int nation)
{
  DWORD t = gAIBanker[nation].dwStamp224;
  if (t == 0)
    return 1;
  return g_dwElapsedTime - t >= 30;
}

// Stage the cost of one requested unit and register
// the demand plus its prerequisites, but only once `elapsed - stamp >= 30`: the body is
// skipped while the stamp is still recent. pRequest's wType is read unsigned, and type 30
// is aliased to type 5, as in banker_clear_harass_type.
void __fastcall banker_demand_guard_unit(int nation, SAI_Request *pRequest)
{
  if (!banker_stamp_stale(nation))
    return;

  unsigned unitType = pRequest->wType;
  if (unitType == 30)
    unitType = 5;

  DWORD ore, gas;
  BYTE supply;
  GetUnitCost(unitType, &ore, &gas, &supply);
  gAIBanker[nation].dwCostOre += ore;
  gAIBanker[nation].dwCostGas += gas;
  gAIBanker[nation].dwCostSupply += supply;

  sai_add_demand(nation, 2, unitType, 0x32, (intptr_t)pRequest);
  AddPrereqUnitToAINeeds(nation, unitType, 0x32,
                         (intptr_t)banker_captain_for_request(nation, pRequest));
}

// The refinery gate, then the same
// flags-to-plan-type fold banker_demand_town_request uses: flags bit 3 gives plan type 4,
// else ((flags & 2) << 1) | 3. The gate applies only while the stamp is recent.
void __fastcall banker_demand_structure(int nation, intptr_t a2, int unitType, int a4)
{
  if (!banker_stamp_stale(nation)) {
    if (unitType != get_player_refinery_type(nation) && a4 != 0x8C)
      return;
  }

  int planType;
  if (gulUnitTypeFlags[unitType] & 8)
    planType = 4;
  else
    planType = ((gulUnitTypeFlags[unitType] & 2) << 1) | 3;

  banker_charge_plan_cost(nation, planType, unitType);
  sai_add_demand(nation, (BYTE)planType, unitType, a4, a2);
}

// banker_demand_structure without the refinery gate and with a
// second selector: when the first stack argument is non-zero the plan type is 5 for
// exactly 1 and 6 otherwise.
void __fastcall banker_demand_town_request(int nation, intptr_t a2, int a3, int unitType, int a5)
{
  int planType;
  if (a3 == 0) {
    if (gulUnitTypeFlags[unitType] & 8)
      planType = 4;
    else
      planType = ((gulUnitTypeFlags[unitType] & 2) << 1) | 3;
  } else {
    planType = (a3 != 1) + 5;
  }

  banker_charge_plan_cost(nation, planType, unitType);
  sai_add_demand(nation, (BYTE)planType, unitType, a5, a2);
}

// Spend-and-pop with the flag set.
void __fastcall banker_spend_top_demand(int nation) {
  banker_refund_top_demand(nation, 1);
  sai_pop_demand(nation);
}

// Take-and-clear, through a WORD local.
int __fastcall banker_take_pending_type(int nation, int *out) {
  WORD v = gAIBanker[nation].wPendingPlus1;
  if (v == 0)
    return 0;
  *out = v - 1;
  gAIBanker[nation].wPendingPlus1 = 0;
  return 1;
}

int __fastcall get_panic_block(int nation) { return gAIBanker[nation].wPanicBlock; }

int __fastcall banker_is_ahead(int nation) {
  return gAIBanker[nation].wFlags & 0x200;
}

// The third argument is on the stack; aDefineMax's
// elements are bytes.
void __fastcall aiscript_define_max(int nation, int v, int unitType) {
  gAIBanker[nation].aDefineMax[unitType] = v;
}

void __fastcall aiscript_set_panic_block(int nation, int v) {
  gAIBanker[nation].wPanicBlock = v;
}

// Tail-jmp into sai_pop_demand.
void __fastcall banker_drop_top_demand(int nation) {
  banker_refund_top_demand(nation, 0);
  sai_pop_demand(nation);
}

int __fastcall sai_is_creep(int nation) {
  return gAIBanker[nation].wFlags & 0x80;
}

// Set/clear bit 9 on a flag argument.
void __fastcall banker_set_is_ahead(int nation, int on) {
  if (on)
    gAIBanker[nation].wFlags |= 0x200;
  else
    gAIBanker[nation].wFlags &= ~0x200;
}

WORD *__fastcall get_defenseuse_aa(int nation) { return gAIBanker[nation].aDefenseUseAA; }

WORD *__fastcall get_defenseuse_ga(int nation) { return gAIBanker[nation].aDefenseUseGA; }

WORD *__fastcall get_defenseuse_ag(int nation) { return gAIBanker[nation].aDefenseUseAG; }

WORD *__fastcall get_defenseuse_gg(int nation) { return gAIBanker[nation].aDefenseUseGG; }

void __fastcall aiscript_defenseuse_aa(int nation, unsigned count, unsigned unitType) {
  WORD *arr = gAIBanker[nation].aDefenseUseAA;
  if (count > 10)
    return;
  if (unitType >= 228)
    return;
  unsigned i = 0;
  while (arr[i] != 0) {
    if (i >= 20)
      return;
    ++i;
  }
  while (count != 0) {
    --count;
    if (i >= 20)
      return;
    arr[i] = (WORD)(unitType + 1);
    ++i;
  }
}

void __fastcall aiscript_defenseuse_ga(int nation, unsigned count, unsigned unitType) {
  WORD *arr = gAIBanker[nation].aDefenseUseGA;
  if (count > 10)
    return;
  if (unitType >= 228)
    return;
  unsigned i = 0;
  while (arr[i] != 0) {
    if (i >= 20)
      return;
    ++i;
  }
  while (count != 0) {
    --count;
    if (i >= 20)
      return;
    arr[i] = (WORD)(unitType + 1);
    ++i;
  }
}

void __fastcall aiscript_defenseuse_ag(int nation, unsigned count, unsigned unitType) {
  WORD *arr = gAIBanker[nation].aDefenseUseAG;
  if (count > 10)
    return;
  if (unitType >= 228)
    return;
  unsigned i = 0;
  while (arr[i] != 0) {
    if (i >= 20)
      return;
    ++i;
  }
  while (count != 0) {
    --count;
    if (i >= 20)
      return;
    arr[i] = (WORD)(unitType + 1);
    ++i;
  }
}

void __fastcall aiscript_defenseuse_gg(int nation, unsigned count, unsigned unitType) {
  WORD *arr = gAIBanker[nation].aDefenseUseGG;
  if (count > 10)
    return;
  if (unitType >= 228)
    return;
  unsigned i = 0;
  while (arr[i] != 0) {
    if (i >= 20)
      return;
    ++i;
  }
  while (count != 0) {
    --count;
    if (i >= 20)
      return;
    arr[i] = (WORD)(unitType + 1);
    ++i;
  }
}

void __fastcall banker_set_build_site_failed(int nation) {
  gAIBanker[nation].bInUse = 1;
}

// The increment is on the 32-bit parameter and
// the result is stored as a WORD.
void __fastcall banker_set_pending_type(int nation, int v) {
  gAIBanker[nation].wPendingPlus1 = (WORD)(v + 1);
}

BOOL __fastcall banker_build_site_failed(int nation) {
  return gAIBanker[nation].bInUse != 0;
}

void __fastcall aiscript_start_campaign(int nation)
{
  gAIBanker[nation].wFlags |= 0x20;
}

void __fastcall banker_hold_ai_spells(int nation)
{
  gAIBanker[nation].bUnk22D = 1;
}

// The third parameter is on the stack.
void __fastcall banker_add_current_resources(int nation, int nOre, int nGas)
{
  gAIBanker[nation].dwAvailOre += nOre;
  gAIBanker[nation].dwAvailGas += nGas;
}

int __fastcall banker_is_attacking(int nation) {
  return gAIBanker[nation].wFlags & 0x100;
}

// ECX is a CUnit *, read at +0x134 first. The tail is
// an unsigned `>= 12` worker-count test. get_player_worker_type is declared `int`, and the
// explicit (WORD) cast narrows its result before aiscript_count_units, as 1.17 does.
int __fastcall banker_worker_bonus_allowed(CUnit *pUnit)
{
  if (pUnit->dwUnion134 == 0)
    return 0;
  int nation = pUnit->bPlayer;
  if (gAIBanker[nation].bUnk22F != 2)
    return 0;
  // The named local is there for the scheduling, not the narrowing:
  // written as one nested expression MSVC hoists `push 1` above the inner call,
  // where the oracle pushes it immediately before the outer one.
  WORD workerType = (WORD)get_player_worker_type(nation);
  return (unsigned)aiscript_count_units(workerType, nation, 1) >= 0x0C;
}

void __fastcall aiscript_fake_nuke(int nation) {
  gAIBanker[nation].dwLastNukeTime = g_dwElapsedTime + 1;
}

// A nonzero argument clears the bit. The
// parameter is tested at 32 bits.
void __fastcall aiscript_set_farms_timing(int nation, int nValue) {
  if (nValue)
    gAIBanker[nation].wFlags &= ~0x10;
  else
    gAIBanker[nation].wFlags |= 0x10;
}

int __fastcall aiscript_get_difficulty(int nation) {
  return gAIBanker[nation].bUnk22F;
}

// A WORD field returned as int, zero-extended.
int __fastcall banker_get_max_force(int nation) {
  return gAIBanker[nation].wMaxForce;
}

// The balances are signed ints, compared against
// 0x1F4 == 500; the bonus is 0x7D0 == 2000.
void __fastcall aiscript_give_money(int nation) {
  if (gPlayerMinerals[nation] < 500)
    gPlayerMinerals[nation] += 2000;
  if (gPlayerGas[nation] < 500)
    gPlayerGas[nation] += 2000;
}

// __fastcall with a third argument on the stack. Both
// out-parameters are BYTE *. The two cost fields are divided unsigned, while the two
// shortfalls are compared signed.
void __fastcall banker_resource_split(int nation, BYTE *pOrePct, BYTE *pGasPct)
{
  DWORD dwGas = gAIBanker[nation].dwCostGas;
  DWORD dwOre;
  int nNeedOre, nNeedGas;
  BYTE bGasPct;

  if (dwGas == 0) {
    *pGasPct = 0;
    *pOrePct = 100;
    return;
  }

  dwOre = gAIBanker[nation].dwCostOre;
  if (dwOre == 0) {
    *pGasPct = 100;
    *pOrePct = 0;
    return;
  }

  nNeedOre = (int)(dwOre - gPlayerMinerals[nation]);
  nNeedGas = (int)(dwGas - gPlayerGas[nation]);

  if (nNeedOre > 0 && nNeedGas <= 0) {
    *pOrePct = 100;
    *pGasPct = 0;
    return;
  }
  if (nNeedGas > 0 && nNeedOre <= 0) {
    *pOrePct = 0;
    *pGasPct = 100;
    return;
  }

  bGasPct = (BYTE)(dwGas * 100 / (dwOre + dwGas));
  *pGasPct = bGasPct;
  *pOrePct = (BYTE)(100 - bGasPct);
}

//   push 2740h / push 0h / push 67D068h / call memset   <- a real __cdecl call
//   then eight (gPlayerMinerals[i] -> +0x0C, gPlayerGas[i] -> +0x10) pairs,
//   `mov dword ptr [0x67F7A8], 0h`, eight `mov byte ptr [...+22Fh], 1h`, and
//   `mov dword ptr [0x67F7AC], 1h`.
void __fastcall sai_banker_init()
{
  int i;

  PlayerClear(gAIBanker);  // Hyperion: and Hyperion's slots

  gActiveNation = 0;

  for (i = 0; i < PlayerLobbySlots(); i++) {
    gAIBanker[i].dwAvailOre = gPlayerMinerals[i];
    gAIBanker[i].dwAvailGas = gPlayerGas[i];
    gAIBanker[i].bUnk22F = 1;
  }

  dword_67F7AC = 1;
}

int __fastcall aiscript_has_campaign(int nation)
{
  return gAIBanker[nation].wFlags & 0x20;
}

int __fastcall aiscript_has_upgrades(int nation) {
  return gAIBanker[nation].wFlags & 0x400;
}

// Unit type 0x22 == 34. When the banker's
// cached unit of that type goes away, clear the slot and re-scan the player's
// unit list for a replacement.
void __fastcall SAI_Banker_RemoveUnit(CUnit *pUnit) {
  if (pUnit->wType != 34)
    return;

  int player = pUnit->bPlayer;
  if (gAIBanker[player].pMedic != pUnit)
    return;

  gAIBanker[player].pMedic = 0;
  for (CUnit *u = CUnit::GetPlayerUnitList(player); u; u = u->pPlayerUnitNext) {
    if (u->wType == 34) {
      gAIBanker[player].pMedic = u;
      return;
    }
  }
}

void __fastcall banker_set_last_nuke_time(int nation) {
  gAIBanker[nation].dwLastNukeTime = g_dwElapsedTime;
}

// A WORD setter.
void __fastcall banker_set_max_force(int nation, int v) {
  gAIBanker[nation].wMaxForce = v;
}

int __fastcall banker_nuke_ready(int nation) {
  return g_dwElapsedTime
         >= gAIBanker[nation].dwLastNukeTime + gAIBanker[nation].bNukeRate * 60;
}

void __fastcall banker_set_attacking(int nation) {
  gAIBanker[nation].wFlags |= 0x100;
}

// A 1.17 bug, kept: each of the four tests checks the
// address of a defensebuild array, not its contents. An array's address is never null, so
// the function always returns 0 and never reports "empty". In C++ that is an array
// decaying to a pointer in a boolean context; do not fix it.
int __fastcall banker_all_defensebuild_empty(int nation) {
  if (gAIBanker[nation].aDefenseBuildGG
   || gAIBanker[nation].aDefenseBuildAG
   || gAIBanker[nation].aDefenseBuildGA
   || gAIBanker[nation].aDefenseBuildAA)
    return 0;
  return 1;
}

// The bit is normalised to 0 or 1.
int __fastcall banker_farms_timing(int nation) {
  return (gAIBanker[nation].wFlags >> 4) & 1;
}

int __fastcall banker_transports_wanted(int nation) {
  return (gAIBanker[nation].wFlags >> 3) & 1;
}

// A BYTE setter.
void __fastcall aiscript_nuke_rate(int nation, int v) {
  gAIBanker[nation].bNukeRate = v;
}

int __fastcall banker_attack_cooldown(int nation) { return gAIBanker[nation].bUnk22C; }

//   cmp byte ptr [eax+67D27Bh], 0h / jz  ---+
//   mov ecx, 800h                           |   0x800 needs imm16, so it is
//   test word ptr [eax+67D280h], cx / jz ---+   materialised, not folded
//   mov eax, 1h / ret                       |
//   xor eax, eax / ret  <--------------------+  one shared false tail
//
// The two conditions must sit in a branch context, not a value context. Written
// as two separate `if (...) return 0;` statements the second stops being a
// branch at all: MSVC contracts it to a bit extract, `movzx / shr 0Bh / and 1`
// -- correct, 6 bytes shorter, and not the oracle. Short-circuit && guarding a
// single `return 1` keeps both tests as tests and gives the shared tail.
int __fastcall banker_may_attack_expansion(int nation) {
  if (gAIBanker[nation].bAttacks != 0 && (gAIBanker[nation].wFlags & 0x800) != 0)
    return 1;
  return 0;
}

// The inverse of banker_build_site_failed.
BOOL __fastcall banker_ai_spells_allowed(int nation) {
  return gAIBanker[nation].bUnk22D == 0;
}

BOOL __fastcall banker_attack_not_held(int nation) {
  return gAIBanker[nation].bUnk22E == 0;
}

// Nullary, as is banker_start_plan, which it calls
// with ECX already clobbered. Each of the other three callees re-reads the nation from its
// global rather than holding it in a local across the calls.
void __fastcall ai_plan_next_nation()
{
  int n;

  n = gActiveNation + 1;
  if (n >= PlayerLobbySlots())  // Hyperion
    n = 0;
  gActiveNation = n;

  if (gPlayerData[n].nType != 1)
    return;
  if ((gAIBanker[n].wFlags & 2) == 0)
    return;

  if (gAIBanker[n].bInUse)
    gAIBanker[n].bInUse--;
  if (gAIBanker[n].bUnk22C)
    gAIBanker[n].bUnk22C--;
  if (gAIBanker[n].bUnk22D)
    gAIBanker[n].bUnk22D--;
  if (gAIBanker[n].bUnk22E)
    gAIBanker[n].bUnk22E--;

  banker_start_plan();
  guard_update_posts(gActiveNation);
  if (g_dwElapsedTime >= 10)
    tai_captain_plan_all_needs(gActiveNation);
  town_player_tick(gActiveNation);
}

void __fastcall banker_set_attack_cooldown(int nation) { gAIBanker[nation].bUnk22C = 0x18; }

void __fastcall aiscript_set_attacks(int nation, int v) {
  gAIBanker[nation].bAttacks = v;
}

// `if (x != 0) --x;` on a BYTE field, one load
// for the test and the decrement.
void __fastcall banker_attack_done(int nation) {
  gAIBanker[nation].wFlags &= ~0x800;
  if (gAIBanker[nation].bAttacks != 0)
    --gAIBanker[nation].bAttacks;
}

void __fastcall banker_hold_attack(int nation) { gAIBanker[nation].bUnk22E = 1; }

void __fastcall aiscript_target_expansion(int nation) {
  gAIBanker[nation].wFlags |= 0x800;
}

// A DWORD field, loaded whole.
intptr_t __fastcall sai_top_demand_captain(int nation) {
  return gAIBanker[nation].aDemands[0].dwCaptain;
}

int __fastcall sai_top_demand_arg(int nation) {
  return gAIBanker[nation].aDemands[0].wArg;
}

// The base-pointer form: the banker's address is
// formed first.
int __fastcall sai_top_demand_type(int nation) {
  AI_Banker *p = &gAIBanker[nation];
  if (p->bDemandCount != 0)
    return p->aDemands[0].bKind;
  return 0;
}

// An unset timestamp reads as "yes";
// otherwise "at least 30 units have passed".
int __fastcall banker_request_throttle_passed(int nation) {
  DWORD t = gAIBanker[nation].dwStamp224;
  if (t == 0)
    return 1;
  return g_dwElapsedTime - t >= 30;
}

void __fastcall banker_stamp_request(int nation) {
  gAIBanker[nation].dwStamp224 = g_dwElapsedTime;
}

// Byte-for-byte the same shape as
// aiscript_set_farms_timing with 0x8 in place of 0x10 (and 0xFFF7 in place of
// 0xFFEF); same inverted sense.
void __fastcall aiscript_transports(int nation, int nValue) {
  if (nValue)
    gAIBanker[nation].wFlags &= ~8;
  else
    gAIBanker[nation].wFlags |= 8;
}

// aDefineMax semantics: 0 == "none" (return 0),
// 0xFF == "unlimited" (return 1), otherwise "has the current count reached the cap". The
// index is the unit type, not bounds-checked. The element address is cached in a pointer
// local and dereferenced twice, as in 1.17.
int __fastcall banker_at_define_max(int nation, int unitType) {
  BYTE *p = &gAIBanker[nation].aDefineMax[unitType];
  if (*p == 0)
    return 0;
  if (*p == 0xFF)
    return 1;
  unsigned n = count_get_existence(unitType, (BYTE)nation);
  if (unitType == 5)
    n += count_get_existence(unitType + 25, (BYTE)nation);
  // `sbb eax,eax / inc eax` is `>=`, not `<` (which would be sbb/neg -- that
  // was the one-byte delta on the first attempt). So the predicate is "count
  // has reached the cap", and note it therefore returns 1 for the 0xFF
  // "unlimited" case too. Reproduced as-is; the semantics are the oracle's.
  return n >= *p;
}

// Same shape as banker_set_is_ahead, bit 10.
void __fastcall banker_set_has_upgrades(int nation, int on) {
  if (on)
    gAIBanker[nation].wFlags |= 0x400;
  else
    gAIBanker[nation].wFlags &= ~0x400;
}

// Charges one unit's cost to the nation's
// running totals. GetUnitCost fills three locals through out-parameters, the last a BYTE.
void __fastcall banker_charge_unit_cost(int nation, int unitType) {
  DWORD ore, gas;
  BYTE supply;

  GetUnitCost(unitType, &ore, &gas, &supply);
  gAIBanker[nation].dwCostOre += ore;
  gAIBanker[nation].dwCostGas += gas;
  gAIBanker[nation].dwCostSupply += supply;
}

// The same three-way dispatch as
// sai_banker_GetPlanCost, but the results go into locals and are then accumulated into
// the banker, the fields indexed directly rather than through a cached pointer.
__declspec(noinline) void __fastcall banker_charge_plan_cost(int nation, int planType, int id) {
  DWORD ore, gas;
  BYTE supply;
  if (planType == 5) {
    supply = 0;
    GetUpgradeCost(nation, id, &ore, &gas);
  } else if (planType == 6) {
    supply = 0;
    GetTechCost(id, &ore, &gas);
  } else {
    GetUnitCost(id, &ore, &gas, &supply);
  }
  gAIBanker[nation].dwCostOre += ore;
  gAIBanker[nation].dwCostGas += gas;
  gAIBanker[nation].dwCostSupply += supply;
}

// The inverse of bit 2.
int __fastcall banker_default_build_on(int nation) {
  return ~(gAIBanker[nation].wFlags >> 2) & 1;
}

// The 0x80-byte memset stays a call, and the last
// statement is a tail call to tai_captain_cancel_gather_jobs.
void __fastcall aiscript_attack_clear(int nation, int bClearTimer) {
  memset(gAIBanker[nation].aHarassNeed, 0, sizeof(gAIBanker[nation].aHarassNeed));
  gAIBanker[nation].wUnk21E = 0;
  if (bClearTimer)
    gAIBanker[nation].dwAttackTime = 0;
  tai_captain_cancel_gather_jobs(nation);
}

// Clears bit 2.
void __fastcall aiscript_default_build(int nation) {
  gAIBanker[nation].wFlags &= ~4;
}

int __fastcall banker_get_defaultmin(int nation) {
  return gAIBanker[nation].bDefaultMin;
}

void __fastcall aiscript_defaultmin(int nation, int v) {
  gAIBanker[nation].bDefaultMin = v;
}

WORD *__fastcall aiscript_get_harass_need(int nation) {
  return gAIBanker[nation].aHarassNeed;
}

int __fastcall banker_attack_target_area(int nation) { return gAIBanker[nation].wUnk21E; }

// Returns the 16-byte object by value: the hidden
// struct-return pointer arrives on the stack, comes back in EAX and is popped by the
// callee, where a second explicit parameter would arrive in EDX under /Gr.
AI_GenCmd __fastcall banker_get_gencmd(int nation) {
  return gAIBanker[nation].genCmd;
}

//
// The two CUnit fields at +0x138 and +0x13A are reached by cast because
// src117/h/CUnit.h has `_unmapped_138[0x18]` there. Both are
// `movzx ecx, word ptr [esi+138h]` / `movzx eax, word ptr [esi+13Ah]`, unsigned
// 16-bit, with the pair reduced by `cmp ecx, eax / cmovnb eax, ecx` to the
// larger of the two and accumulated into a 32-bit total with a plain `add`.
//
// 1.17's own off-by-one is reproduced: the append stops at index 0x3F, one short
// of the 64 slots aiscript_attack_clear's `push 80h` memset clears, so the last
// harass slot is unreachable by this writer. Same class as
// banker_clear_harass_type's `i < 0x3F`.
void __fastcall aiscript_harass_factor(int nation, unsigned nFactor)
{
  WORD aTmp[64];
  AI_Banker *p;
  WORD wTarget;
  int nRegion;
  CUnit *u;
  unsigned nStrength;
  unsigned nCount;
  unsigned k, m;

  p = &gAIBanker[nation];
  wTarget = p->wUnk21E;
  if (wTarget == 0)
    return;

  nRegion = ai_nearest_enemy_owner(nation, wTarget);
  if (nRegion == -1)
    return;

  nStrength = 0;
  for (u = CUnit::GetPlayerUnitList(nRegion); u != NULL; u = u->pPlayerUnitNext) {
    if (!u->IsDeadOrDying()) {
      unsigned a = *(WORD *)((BYTE *)u + offsetof(CUnit, _unmapped_138));
      unsigned b = *(WORD *)((BYTE *)u + offsetof(CUnit, _unmapped_138) + 2);
      nStrength += (a >= b) ? a : b;
    }
  }
  if (nStrength == 0)
    return;

  nCount = (nStrength + nFactor - 1) / nFactor - 1;
  if (nCount > 3)
    nCount = 3;

  memcpy(aTmp, p->aHarassNeed, sizeof(aTmp));

  // Find the first empty slot. cl rotates this into the oracle's exact shape:
  // load aTmp[0] into DI, `test di, di / jz` past the scan, then
  // `inc eax / cmp word ptr [ebp+eax*2-84h], 0 / jnz`.
  for (k = 0; aTmp[k] != 0; k++)
    ;

  // The inner `for` is the whole reason the source reads aTmp[m] and not
  // aTmp[m+1]: cl rotates the test to the bottom, so the emitted load is
  // `movzx ecx, word ptr [ebp+edx*2-82h]` with edx still 0 -- the +1 is the
  // rotation, not the source. Writing aTmp[m+1] with a do/while instead makes
  // cl strength-reduce to a pointer induction and costs 5 B.
  while (nCount > 1) {
    nCount--;
    for (m = 0; aTmp[m] != 0; m++) {
      p->aHarassNeed[k] = aTmp[m];
      k++;
      if (k == 0x3F)
        return;
    }
  }
}

// cmov picks the window; the timer is
// cleared when the answer is 1, which is the opposite of what "expired" would
// do. Matched as the oracle has it.
int __fastcall banker_attack_window_expired(int nation) {
  if (gAIBanker[nation].dwAttackTime == 0)
    return 0;
  unsigned limit = (gAIBanker[nation].wFlags & 0x20) ? 180 : 120;
  int r = g_dwElapsedTime - gAIBanker[nation].dwAttackTime >= limit;
  if (r)
    gAIBanker[nation].dwAttackTime = 0;
  return r;
}

void __fastcall aiscript_defaultbuild_off(int nation) {
  gAIBanker[nation].wFlags |= 4;
}

// Find the slot holding unitType+1 and
// overwrite it with 229 == 0xE5 == 228+1, the "no unit" sentinel in the
// +1-biased space. Unit type 30 is remapped to 5 first (a hardcoded alias in
// the oracle). The index is bounded `cmp eax, 3Fh` (63) while
// aiscript_attack_clear's memset clears 64 slots, so the last slot is
// unreachable by this writer. Reproduced, not corrected.
void __fastcall banker_clear_harass_type(int nation, int unitType) {
  if (unitType == 30)
    unitType = 5;
  unsigned i = 0;
  int want = unitType + 1;
  const WORD *p = gAIBanker[nation].aHarassNeed;
  for (; i < 0x3F; ++i) {
    if (*p == want) {
      gAIBanker[nation].aHarassNeed[i] = 229;
      return;
    }
    ++p;
  }
}

// Append `count` copies of unitType+1 into the harass
// need list, starting at the first empty slot. Both loops bound the index at 63 while
// aiscript_attack_clear's memset clears 64 WORDs, so the last slot is unreachable by this
// writer, as in banker_clear_harass_type: kept, not corrected.
void __fastcall set_harass(unsigned nation, unsigned count, unsigned unitType)
{
  if (nation >= PlayerLobbySlots())  // Hyperion
    return;
  if (count >= 0x3F)
    return;
  if (unitType >= 0xE4)
    return;

  unsigned i = 0;
  while (gAIBanker[nation].aHarassNeed[i] != 0) {
    if (i >= 0x3F)
      return;
    ++i;
  }
  while (count-- != 0) {
    if (i >= 0x3F)
      return;
    gAIBanker[nation].aHarassNeed[i] = (WORD)(unitType + 1);
    ++i;
  }
}

// `ret 4h`: one stack argument on top of the two
// register ones. The wUnk21E store is made once per path, as 1.17 has it. a3 is only
// tested against zero, so whether it is a flag or a value is undecided; it stays a3.
void __fastcall banker_set_attack_target(int nation, int a2, int a3) {
  if (a3 == 0 && gAIBanker[nation].dwAttackTime != 0) {
    gAIBanker[nation].wUnk21E = (WORD)(a2 + 1);
    return;
  }
  gAIBanker[nation].dwAttackTime = g_dwElapsedTime;
  gAIBanker[nation].wUnk21E = (WORD)(a2 + 1);
}

// Whole-struct copy, a movdqu pair (/arch:SSE2).
void __fastcall aiscript_set_gencmd(int nation, const AI_GenCmd *src) {
  gAIBanker[nation].genCmd = *src;
}

// Backdates the attack timestamp so
// banker_attack_window_expired's 180-frame (0xB4) timeout is `delay` units from expiring.

// The second parameter is the delay, not a flag, and the branch target shows
// it: `test edx,edx / jne` at +0x05 jumps to +0x1A, past the `mov edx, 14h` at
// +0x15, so EDX reaching the final `add ecx, edx` is either the caller's own
// argument (delay != 0) or the literal 20 (delay == 0). That is why the oracle
// never folds `-180 + 20` into `-160`: on one path the addend is a variable.
// Reading the second argument as a boolean and the 20 as a literal costs 7
// bytes, since MSVC folds it to `add ecx, -0A0h`, and no source shape prevents
// that fold; thirteen were tried.

// This divergence is a third trigger for the dll-mode idiom, and neither of the
// two known ones: the address is not used as a pointer value and the global is
// read exactly once. The baked literal makes the accumulator short form
// `A1 disp32` (5 B) legal where the symbol form had to use `8B 0D disp32`
// (6 B); MSVC takes the shorter load into EAX and then needs an extra `lea` to
// land the value in ECX, netting -1 byte. It bites only where the oracle loaded
// a scalar global into a register other than EAX: banker_set_attack_target
// reads the same global one line away and agrees in both forms, because there
// the oracle itself uses `A1`.
void __fastcall aiscript_quick_attack(int nation, int delay) {
  if (delay == 0) {
    if (gAIBanker[nation].dwAttackTime != 0)
      return;
    delay = 20;
  }
  gAIBanker[nation].dwAttackTime = g_dwElapsedTime - 180 + delay;
}

//   lea eax, dword ptr [ecx+ecx*8]      <- player * 9
//   mov cl, byte ptr [eax*4+6726D1h]    <- ... * 4 + &gPlayerData[0].nRace
//   cmp cl, 1h / jnz / mov eax, 7h / ret
//   test cl, cl / mov eax, 40h / mov edx, 29h / cmovz eax, edx
// 7 == Terran SCV, 0x40 == 64 == Protoss Probe, 0x29 == 41 == Zerg Drone.
//
// The condition is spelled `== 0` for the codegen, not for style. For
// `c ? T : F` MSVC materialises F into EAX, T into EDX, and cmov<c>. Written
// `nRace ? 64 : 41` that is `mov eax,29h / mov edx,40h / cmovne`: the oracle's
// two constants swapped and CMOVNE for its CMOVE.
// Written `nRace == 0 ? 41 : 64` it is `mov eax,40h / mov edx,29h / cmove`,
// which is the oracle. Same semantics either way; only one is the same bytes.
__declspec(noinline) int __fastcall get_player_worker_type(int nPlayer) {
  if (gPlayerData[nPlayer].nRace == 1)     // 1 == Terran
    return 7;
  return gPlayerData[nPlayer].nRace == 0 ? 41 : 64;
}

// The same source shape as
// get_player_worker_type; the select compiles differently because 149 and 157 differ by 8.
// 0x6E == 110 == Terran Refinery, 0x95 == 149 == Zerg Extractor, 149+8 == 157 == Protoss
// Assimilator.
__declspec(noinline) int __fastcall get_player_refinery_type(int nPlayer) {
  if (gPlayerData[nPlayer].nRace == 1)     // 1 == Terran
    return 110;
  return gPlayerData[nPlayer].nRace ? 157 : 149;
}

// Two mirrored paths (completion vs
// existence) each with the same zerg-egg style type folding: type 30 also
// counts type 5, type 5 also counts type 30. The plain `return n` paths work
// because EAX still holds the first call's result -- `mov edi, eax` does not
// clobber it.
__declspec(noinline) int __fastcall aiscript_count_units(int unitType, int nation, int completed) {
  if (completed) {
    unsigned n = count_get_completion(unitType, (BYTE)nation);
    if (unitType == 30)
      return count_get_completion(unitType - 25, (BYTE)nation) + n;
    if (unitType == 5)
      return count_get_completion(unitType + 25, (BYTE)nation) + n;
    return n;
  }
  unsigned n = count_get_existence(unitType, (BYTE)nation);
  if (unitType == 30)
    return count_get_existence(unitType - 25, (BYTE)nation) + n;
  if (unitType == 5)
    return count_get_existence(unitType + 25, (BYTE)nation) + n;
  return n;
}

// &sgCaptain[nation][region-of(request
// position)]. GetRegion_xy's 16-bit return is zero-extended before the index.
__declspec(noinline) TAI_Captain *__fastcall banker_captain_for_request(int nation, const SAI_Request *pRequest) {
  return &sgCaptain[nation][GetRegion_xy(pRequest->sX, pRequest->sY)];
}
