// TAI_Captain.cpp -- the AI captain system: the soldier and captain
// id-to-pointer tables, the per-captain job handlers, and the aiscript captain
// handlers. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../h/playervision.h"  // Hyperion: the blinders past 7 (stage D)
#include "../port/data/port_formats.h"  // Hyperion: PortSave117Decompress, PortSave117Compress
#include "../gen/save117.h"
#include <string.h>        // memset -- captains_alloc

#include "../h/platform.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"       // CSprite::wPosX / wPosY -- tai_call_attackers_on_unit/4AF270
#include "../h/CFlingy.h"       // CFlingy::GetVectorComponents, AngleBetween -- tai_soldier_move_toward
#include "../h/CTarget.h"       // CTarget::XYInRadius -- patroller_follow_captain
#include "../h/CUnitCombat.h"   // tai_unit_was_attacked
#include "../h/CUnitFind.h"     // get_units_in_region
#include "../h/CUnitSearches.h" // ours: tai_captain_region_is_calm, GetRegion_CUnit
#include "../h/order.h"         // get_units_in_rect (rect16_t by value)
#include "../h/sai.h"           // ours: NumAreas and the region-table readers
#include "../h/sai_PathCreate.h" // ep_glob_t -- tai_captain_job_lost
#include "../h/TAI_GuardPost.h" // TGuard -- tai_call_allies_on_attacker
#include "../h/sai_TownBuilder.h" // sai_town_x / sai_town_y -- tai_captain_start_expansion
#include "../h/SAI_Banker.h"    // GetRegion_xy
#include "../h/SAI_Build.h"     // region_has_worker_or_building
#include "../h/SAI_Main.h"      // SAI_RemoveUnit
#include "../h/saveload.h"      // get_upgrade_level
#include "../h/net_cmds.h"      // PlayerData -- gPlayerData's element type
#include "../h/CUnitMisc.h"     // the free IsEnemyUnit(nPlayer, pUnit)
#include "../h/statbtn.h"       // TechUseAllowed
#include "../h/TAI_AreaStrength.h" // the two threat-region readers
#include "../h/Storm.h"         // SMemAlloc / SMemFree
#include "../h/TrigAct.h"       // net_rand
#include "../h/TAI_Captain.h"
#include "../gen/globals117.h"
#include "../h/CUnitBounds.h"   // Hyperion: g_bFinderSideCheck, FinderSideDiffer
#include <vector>               // Hyperion: the captains' unit slots
#include "../h/handicap.h"  // Hyperion: a player's handicap

// The globals this TU reaches, dual-spelled, one `#ifndef` per name: the plain
// `extern` arm alone would make the --dll-mode check vacuous, and the literal
// arm is what the payload ships.

// Hyperion: declared as the whole pool, the free head after the array
// included, so that the engine's own storage for it (gen/wide117.h) has room
// for the head; declared as the array alone, the head was sgCaptain[0]
//.
#ifdef OC_DLL_MODE
#define gSoldiers_g   (*reinterpret_cast<TSoldierPool *>(OC_VA(0x6ACC50u)))
#else
extern TSoldierPool gSoldiers_g;     // 0x6ACC50
#endif

// sgCaptain: the proof arm is file-static (every oracle reader of 0x6B1A74 is in
// this TU, and tai_captain_recompute_threats needs the internal linkage to hoist the row load out of
// its loop); the engine binds it as an extern.
#ifdef OC_DLL_MODE
#define sgCaptain     (reinterpret_cast<TAI_Captain **>(OC_VA(0x6B1A74u)))
#else
extern PlayerTable<TAI_Captain *, PlayerTagCaptains, 8> sgCaptain;    // 0x6B1A74
#endif

// gulUnitTypeFlags is spelled exactly this way in CSprite.cpp, CUnitCarrier.cpp
// and CUnitCombat.cpp already. Six rows here do `test byte ptr [eax*4+521660h],
// 1h` right after `movzx eax, word ptr [ecx+64h]`, so the stride is 4 indexed
// by CUnit::wType -- a DWORD table whose 228 rows are units.dat's row count.
#ifdef OC_DLL_MODE
#define gulUnitTypeFlags (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
#else
extern DWORD gulUnitTypeFlags[228] OC_UNALIGNED;  // 0x521660
#endif

// dword_6B1A9C @0x6B1A9C is `static`, and the internal linkage is visible in
// the code: under `extern` alone tai_call_attackers_on_unit and
// tai_call_attackers_if_disabled revert to size-exact misses that 21 source
// shapes did not recover, because internal linkage is what lets MSVC prove the
// flag store cannot alias the sprite reads. It is also true of the original:
// every reader of this address -- tai_attack_helpless_cb,
// tai_call_attackers_on_unit and tai_call_attackers_if_disabled -- is a
// TAI_Captain.cpp row. A regenerated globals117.h emits a bare
// `extern DWORD dword_6B1A9C;` ahead of this definition, which MSVC accepts
// before a `static` with no diagnostic.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#ifndef dword_6B1A9C
#define dword_6B1A9C (*reinterpret_cast<DWORD *>(OC_VA(0x6B1A9Cu)))
#endif
#else
static DWORD dword_6B1A9C;           // 0x6B1A9C -- internal linkage, see above
#endif

// gPlayerData @0x6726C8 is registered. 0x6726D0 is &gPlayerData[0].nType, not a
// global, and must never be registered as one.
#ifdef OC_DLL_MODE
#define gPlayerData   (reinterpret_cast<PlayerData *>(OC_VA(0x6726C8u)))
#else
extern PlayerTable<PlayerData, PlayerTagData> gPlayerData;    // 0x6726C8
#endif

// The round-robin cursor pair tai_captain_service_round_robin drives. 0x6B1A98
// is the area cursor (it resets to 1 at 0x9C4 == 2500, the same
// captains-per-nation constant captain_ptr_to_id biases the id with) and
// 0x6B1A94 the nation cursor (it wraps at 8, `cmp ecx, 8h / cmovz ecx, eax`).
#ifdef OC_DLL_MODE
#define dword_6B1A94  (*reinterpret_cast<DWORD *>(OC_VA(0x6B1A94u)))
#else
extern DWORD dword_6B1A94;           // 0x6B1A94
#endif

#ifdef OC_DLL_MODE
#define dword_6B1A98  (*reinterpret_cast<DWORD *>(OC_VA(0x6B1A98u)))
#else
extern DWORD dword_6B1A98;           // 0x6B1A98
#endif

// dword_6B6900 @0x6B6900 is deliberately unregistered: CUnitUpdate2.cpp already
// carries it TU-locally, and a registry row would risk a C2371 collision
// against that declaration for nothing. The spelling below is copied from
// CUnitUpdate2.cpp so the two cannot drift. tai_call_attackers_when_hit reads it
// `cmp dword ptr [6B6900h], 0h` and writes `mov dword ptr [6B6900h], 1h`.
#ifdef OC_DLL_MODE
#ifndef dword_6B6900
#define dword_6B6900 (*reinterpret_cast<DWORD *>(OC_VA(0x6B6900u)))
#endif
#else
extern DWORD dword_6B6900;
#endif

// Three unregistered TAI_Captain.cpp globals tai_captain_regions_connected reaches, spelled in-TU
// for the same reason as dword_6B6900 above. Names are the sheet's own
// (data_naming_1.17.csv assigns all three to this TU); the widths are the
// oracle's: `mov eax, dword ptr [ebx*4+6B68C0h]`, `cmp dword ptr
// [ebx*4+6B68E0h], esi` and `cmp byte ptr [ecx+edi*1+6B1AA0h], 2h` with ECX =
// nation * 0x9C4, the captains-per-nation stride captain_ptr_to_id also uses.
#ifdef OC_DLL_MODE
#define dword_6B68C0  (reinterpret_cast<DWORD *>(OC_VA(0x6B68C0u)))
#else
extern PlayerTable<DWORD, PlayerTag6B68C0, 8> dword_6B68C0;
#endif

#ifdef OC_DLL_MODE
#define dword_6B68E0  (reinterpret_cast<DWORD *>(OC_VA(0x6B68E0u)))
#else
extern PlayerTable<DWORD, PlayerTag6B68E0, 8> dword_6B68E0;
#endif

#ifdef OC_DLL_MODE
#define sgRegions     (reinterpret_cast<BYTE *>(OC_VA(0x6B1AA0u)))
#else
extern PlayerRegionsTable sgRegions;
#endif

// Hyperion: the areas whose unit slots may hold a unit, a set for each nation.
// TAI_Captain_RemoveUnit clears a unit from the four slots of every area of every computer's,
// in a game of 64 up to 63 nations times the map's areas for each unit that dies, and most
// hold none. A unit goes into a slot in four places alone (tai_captain_take_patroller,
// tai_captain_pick_leader, tai_captain_retarget_soldiers, tai_captain_transfer_soldiers), and
// each marks its area here. The set is made by a whole walk, after which a removal walks the
// set alone, in any order, since each slot is cleared or not by itself, and leaves out an
// area once its four slots are empty. The tables made, read, grown or freed, or a snapshot
// restored (CaptainSlotsForget), have the next removal walk them all again.
// HYPERION_FINDER_CHECK=1 looks at every area with a unit in a slot and logs any the set
// leaves out.
namespace {
struct captain_slots_t {
  const TAI_Captain *pTable = nullptr;  // the nation's table the set is of
  unsigned nAreas = 0;
  unsigned nMade = 0;                   // s_nCaptainTables when it was made
  std::vector<unsigned> areas;          // the areas that may hold a unit, each once
  std::vector<unsigned char> in;        // by area, 1 when it is in areas
};
std::vector<captain_slots_t> s_captainSlots;  // by nation
unsigned s_nCaptainTables = 1;  // the times the tables were made, read, grown or freed
}

void CaptainSlotsForget()
{
  ++s_nCaptainTables;
}

static inline bool CaptainHolds(const TAI_Captain *pCaptain)
{
  return pCaptain->pUnit1C != NULL || pCaptain->pUnit20 != NULL || pCaptain->pUnit24 != NULL
      || pCaptain->pUnit28 != NULL;
}

// The nation's set, when it is of the table there now; NULL when a removal is to walk them all.
static captain_slots_t *CaptainSlots(unsigned nNation)
{
  if (nNation >= s_captainSlots.size())
    return NULL;
  captain_slots_t &s = s_captainSlots[nNation];
  if (s.nMade != s_nCaptainTables || s.pTable != sgCaptain[nNation] || s.nAreas != NumAreas())
    return NULL;
  return &s;
}

// The nation's set from its table, as a whole walk leaves it.
static void CaptainSlotsMake(unsigned nNation)
{
  if (nNation >= s_captainSlots.size())
    s_captainSlots.resize(nNation + 1);
  captain_slots_t &s = s_captainSlots[nNation];
  s.pTable = sgCaptain[nNation];
  s.nAreas = NumAreas();
  s.nMade = s_nCaptainTables;
  s.areas.clear();
  s.in.assign(s.nAreas, 0);
  for (unsigned j = 1; j < s.nAreas; j++) {
    if (CaptainHolds(&s.pTable[j])) {
      s.in[j] = 1;
      s.areas.push_back(j);
    }
  }
}

// A unit stored in one of pCaptain's slots: its area joins its nation's set. A table with no
// set is walked whole by the next removal, and an area past the count is not walked at all.
static void CaptainSlotsMark(const TAI_Captain *pCaptain)
{
  for (unsigned i = 0; i < s_captainSlots.size(); i++) {
    captain_slots_t *pSlots = CaptainSlots(i);
    if (pSlots == NULL || pCaptain < pSlots->pTable || pCaptain >= pSlots->pTable + pSlots->nAreas)
      continue;
    const unsigned j = static_cast<unsigned>(pCaptain - pSlots->pTable);
    if (j != 0 && !pSlots->in[j]) {
      pSlots->in[j] = 1;
      pSlots->areas.push_back(j);
    }
    return;
  }
}

static void CaptainSlotsVerify(unsigned nNation)
{
  const captain_slots_t &s = s_captainSlots[nNation];
  for (unsigned j = 1; j < s.nAreas; j++)
    if (!s.in[j] && CaptainHolds(&s.pTable[j]))
      FinderSideDiffer("a captain's unit slots", static_cast<int>(j));
}

// captains_alloc, oracle 0x4AA310 (63 B)
//
// The eight-array loop, and the shape half this TU is built on: the oracle
// walks `sgCaptain` by byte offset, `add esi, 4h` / `cmp esi, 20h`, not by
// element index, which is what an `int i` loop over a pointer array lowers to.
// `cmp esi, 20h` on a 4-byte stride is also what proves the count 8, with
// captain_id_to_ptr's `cmp dl, 8h / jnb` proving it a second time.
//
// The allocation is `nCount * sizeof(TAI_Captain)`, `imul edi, ecx, 34h` hoisted
// out of the loop, and the owner tag is Storm's convention: the string at
// 0x4F05A0 reads "TAI_Captain.cpp", which is also how this TU's name is proven,
// and 0x191 == 401 is the oracle's own line number written out, since `#line`
// is forbidden and __LINE__ cannot reproduce it.
void captains_alloc(int nCount)
{
  const DWORD dwBytes = nCount * sizeof(TAI_Captain);
  unsigned i;

  for (i = 0; i < PlayerLobbySlots(); i++) {  // Hyperion
    sgCaptain[i] = static_cast<TAI_Captain *>(
        SMemAlloc(dwBytes, "TAI_Captain.cpp", 401, 0));
    memset(sgCaptain[i], 0, dwBytes);
  }
  CaptainSlotsForget();  // Hyperion
}

// captain_free, oracle 0x4AAEB0 (51 B)
//
// captains_alloc's mirror, same byte-offset loop. The null guard covers only
// the free: `test eax, eax / je` lands on +0x1F, the
// `mov dword ptr [esi+6B1A74h], 0h` itself, so the slot is cleared on both
// paths. Writing the store inside the `if` instead is size-exact and differs in
// one byte, the `je` displacement. 0x19C == 412 is the owner tag's line number.
void captain_free()
{
  unsigned i;

  for (i = 0; i < 8; i++) {
    if (sgCaptain[i] != NULL)
      SMemFree(sgCaptain[i], "TAI_Captain.cpp", 412, 0);
    sgCaptain[i] = NULL;
  }
  // Hyperion: the extra nations' too, whether or not this game has them
  for (i = 0; i < sgCaptain.kExtraCount; i++) {
    if (sgCaptain.Extra()[i] != NULL)
      SMemFree(sgCaptain.Extra()[i], "TAI_Captain.cpp", 412, 0);
    sgCaptain.Extra()[i] = NULL;
  }
  CaptainSlotsForget();  // Hyperion
}

// soldier_ptr, oracle 0x4AD9C0 (18 B)
//
// The ids are 1-based and the oracle folds the bias into the base:
//   lea eax, dword ptr [ecx+ecx*4]      ; id * 5
//   lea eax, dword ptr [eax*4+6ACC3Ch]  ; * 4, + (gSoldiers_g - 20)
// 0x6ACC3C is not a global -- it is `&gSoldiers_g.aSoldiers[-1]`, and writing
// the source as `&gSoldiers_g.aSoldiers[id - 1]` is what emits that constant.
TSoldier *soldier_ptr(int nSoldierId)
{
  if (nSoldierId <= 0 || nSoldierId > AI_RECORDS_STORE)  // Hyperion: a save's id past the table is none
    return NULL;
  return &gSoldiers_g.aSoldiers[nSoldierId - 1];
}

// soldier_id, oracle 0x4AD9A0 (27 B)
//
// soldier_ptr's inverse, and the division is the compiler's, not the source's:
// `mov eax, -33333333h / mul ecx / shr edx, 4h` is MSVC's unsigned divide by
// 20, which is `sizeof(TSoldier)`. A pointer difference is the only spelling
// that produces it; an explicit `/ 20` would be a claim about the stride rather
// than a use of it.
int soldier_id(TSoldier *pSoldier)
{
  if (pSoldier == NULL)
    return 0;
  return static_cast<unsigned>(reinterpret_cast<BYTE *>(pSoldier)
                               - reinterpret_cast<BYTE *>(gSoldiers_g.aSoldiers))
         / sizeof(TSoldier) + 1;
}

// tai_unit_is_firebat, oracle 0x4AA4F0 (11 B)
//
// The CUnit-predicate shape: `xor eax, eax / cmp word ptr [ecx+64h], 20h /
// setz al / ret`. A shape-insensitive leaf (<= ~35 B), so this match is not
// evidence of a derivation in either direction -- it is here as the template
// for the dozen wType predicates in this TU.
BOOL tai_unit_is_firebat(CUnit *pUnit)
{
  return pUnit->wType == 0x20;
}

// tai_captain_job_is_gather, oracle 0x4AB0F0 (20 B)
//
// The branchy boolean, and it is not a `setcc`: the oracle spends
// `cmp al, 8h / jz` + `cmp al, 9h / jz` + `xor eax, eax / ret` +
// `mov eax, 1h / ret`. An `||` written as a returned expression folds to
// arithmetic, where the early `return 0` with a trailing `return 1` keeps the
// two compares and the two exits.
BOOL tai_captain_job_is_gather(TAI_Captain *pCaptain)
{
  const BYTE bJob = pCaptain->bJob;

  if (bJob != 8 && bJob != 9)
    return 0;
  return 1;
}

// Internal copy of tai_captain_job_is_gather, same bytes. Its caller keeps a value in a register
// across the call, which only an internal callee allows; the link folded the
// two copies into one address.
static __declspec(noinline) BOOL tai_captain_job_is_gather_local(TAI_Captain *pCaptain)
{
  const BYTE bJob = pCaptain->bJob;

  if (bJob != 8 && bJob != 9)
    return 0;
  return 1;
}

// tai_captain_region_is_calm, oracle 0x4AEA70 (43 B). Declared in
// CUnitSearches.h, so `__fastcall` is carried explicitly to match that declaration.
//
// The captain-indexing shape every area scan in this TU uses is `&sgCaptain[nation][area]`,
// 0x34 being sizeof(TAI_Captain). Four reject tests and one accept, each reject its own
// early `return 0`.
BOOL __fastcall tai_captain_region_is_calm(int nPlayer, int nRegion)
{
  const TAI_Captain *pCaptain = &sgCaptain[nPlayer][nRegion];

  if (pCaptain->bJob == 3 || pCaptain->pUnit1C != NULL ||
      pCaptain->pUnit20 != NULL || (pCaptain->bFlags & 0x20) != 0)
    return 0;
  return 1;
}

// tai_soldier_captain_is_attacking, oracle 0x4B0580 (51 B)
//
// The three-hop chain this TU is built on, and the reason TSoldier and
// TAI_Captain had to be derived before anything could be authored:
//   mov   eax, dword ptr [ecx+134h]   ; CUnit::dwUnion134 -> TSoldier *
//   cmp   byte ptr [eax+8h], 4h       ; TSoldier::bType
//   mov   eax, dword ptr [eax+10h]    ; TSoldier::pCaptain -> TAI_Captain *
//   mov   al,  byte ptr [eax+5h]      ; TAI_Captain::bJob
// The null test and the type test share one exit -- `jnz` jumps backward to the
// `xor eax, eax` the null path already used -- which is what a single
// `return 0` from two conditions produces. dwUnion134 stays a `DWORD` in
// CUnit.h: re-typing it to `TSoldier *` would touch a header 40 TUs include for
// zero bytes, and the cast is local.
BOOL tai_soldier_captain_is_attacking(CUnit *pUnit, int nArg1)
{
  const TSoldier *pSoldier =
      reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
  BYTE bJob;

  if (pSoldier == NULL)
    return 0;
  if (pSoldier->bType != 4)
    return 0;

  bJob = pSoldier->pCaptain->bJob;
  return bJob == 1 || (nArg1 == 0 && (bJob == 8 || bJob == 9)) || bJob == 2;
}

// aiscript_wait_bunkers, oracle 0x4B05C0 (55 B), and
// aiscript_wait_turrets, oracle 0x4B0600 (55 B)
//
// The area-scan family: the two bodies are instruction-for-instruction identical
// except for the bitmask, 2h against 10h.
//
// The loop starts at area 1, not 0 (`mov edx, 1h`), and the oracle walks a
// pointer pre-biased to `&sgCaptain[nation][1].bFlags`:
//   mov ecx, dword ptr [edi*4+6B1A74h] / add ecx, 3Ch      ; 0x34 + 8
//   test byte ptr [ecx], 10h / jnz <found> / inc edx / add ecx, 34h
// which is what indexing `sgCaptain[nNation][i].bFlags` in an `i = 1` loop
// strength-reduces to.
//
// The return is "ran off the end", not "found": `xor eax, eax / cmp edx, esi /
// setz al` is true exactly when the induction variable reached the bound, so
// the source is `i == nAreas` after the loop and not a flag set inside it.
BOOL aiscript_wait_bunkers(int nNation)
{
  const unsigned nAreas = NumAreas();
  unsigned i;

  for (i = 1; i < nAreas; i++) {
    if ((sgCaptain[nNation][i].bFlags & 0x02) != 0)
      break;
  }
  return i == nAreas;
}

BOOL aiscript_wait_turrets(int nNation)
{
  const unsigned nAreas = NumAreas();
  unsigned i;

  for (i = 1; i < nAreas; i++) {
    if ((sgCaptain[nNation][i].bFlags & 0x10) != 0)
      break;
  }
  return i == nAreas;
}


// tai_soldier_captain_is_idle, oracle 0x4AE8E0 (28 B). tai_soldier_captain_is_attacking's chain with a `setz` tail:
//   mov ecx, [ecx+134h] / xor eax, eax / test ecx, ecx / jz ->ret
//   cmp byte [ecx+8h], 4h / jnz ->ret
//   mov ecx, [ecx+10h] / cmp byte [ecx+5h], al / setz al / ret
// `cmp ... , al` rather than `, 0h` is MSVC reusing the zero it already made
// for the two early exits -- so `bJob == 0` is the source, not a literal 0
// compare against a fresh register.
int tai_soldier_captain_is_idle(CUnit *pUnit)
{
  const TSoldier *pSoldier =
      reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);

  if (pSoldier == NULL)
    return 0;
  if (pSoldier->bType != 4)
    return 0;
  return pSoldier->pCaptain->bJob == 0;
}

// tai_unit_is_forming_or_incomplete, oracle 0x4B14D0 (34 B). Three jumps, all to the same
// `mov eax, 1h` at +0x1C -- so all three are `return 1` and the fallthrough is
// the `return 0`. The order id is read `movzx eax, [ecx+4Dh]` and then
// compared 32-bit twice, which is an INT local, not a BYTE one: a BYTE local
// would compare `cmp al, 6Ah`.
BOOL tai_unit_is_forming_or_incomplete(CUnit *pUnit)
{
  if ((pUnit->dwFlags & 0x01) != 0) {
    const int nOrder = pUnit->bOrderId;

    if (nOrder != 0x6A && nOrder != 0x96)
      return 0;
  }
  return 1;
}

// tai_count_zerg_ground_defence, oracle 0x4ADDE0 (36 B). Two count_get_completion calls over one
// nation byte; the 0x92 call happens first and its result is the one scaled by
// 2 (`lea eax, [eax+esi*2]`).
unsigned tai_count_zerg_ground_defence(int nNation)
{
  const unsigned nA = count_get_completion(0x92, static_cast<BYTE>(nNation));
  const unsigned nB = count_get_completion(0x26, static_cast<BYTE>(nNation));

  return nB + nA * 2;
}

// tai_count_marine_defence, oracle 0x4AA4C0 (40 B). `cmp esi, eax / cmovnbe esi, eax` is an
// unsigned min -- count_get_completion returns unsigned, so the ternary is
// unsigned by inheritance and no cast is needed.
unsigned tai_count_marine_defence(int nNation)
{
  const unsigned nA = count_get_completion(0x7D, static_cast<BYTE>(nNation));
  const unsigned nB = count_get_completion(0, static_cast<BYTE>(nNation));
  const unsigned nC = nA * 4;

  return nB + (nC > nB ? nB : nC);
}

// IsAiUsableSpellOrder, oracle 0x4ADB40 (74 B). Ten compares, one shared
// `mov eax, 1h` -- tai_captain_job_is_gather's shape at scale. A `switch` would give the same
// answer and not the same bytes; the chained `!=` in one `if` is what keeps
// every compare and exactly two exits. Order preserved from the oracle.
BOOL IsAiUsableSpellOrder(int nOrderId)
{
  if (nOrderId != 0x90 && nOrderId != 0x77 && nOrderId != 0x92 &&
      nOrderId != 0x8F && nOrderId != 0x93 && nOrderId != 0x7A &&
      nOrderId != 0x73 && nOrderId != 0x8E && nOrderId != 0x79 &&
      nOrderId != 0x71)
    return 0;
  return 1;
}

// tai_count_zerg_air_defence, oracle 0x4ADB90 (54 B).
// tai_count_zerg_ground_defence with a third count: `add eax, esi` then
// `lea eax, [eax+edi*2]`, so the 0x90 count is the doubled one and it is the
// first call. One call per statement and the arithmetic last: folding the `* 2`
// into the call's own expression makes MSVC double the value before the next
// call and lose the `lea`.
unsigned tai_count_zerg_air_defence(int nNation)
{
  const unsigned nA = count_get_completion(0x90, static_cast<BYTE>(nNation));
  const unsigned nB = count_get_completion(0x26, static_cast<BYTE>(nNation));
  const unsigned nC = count_get_completion(0x2B, static_cast<BYTE>(nNation));

  return nC + nB + nA * 2;
}

// tai_has_transports, oracle 0x4AA8A0 (76 B). Three counts and an upgrade level, all
// short-circuited, with three `return 1`s tail-merged onto one `mov eax, 1h`.
// `lea edx, dword ptr [eax+18h]` is not an address -- EAX is provably 0 at
// that point (the branch above tested it), so it is MSVC materialising the
// constant 24 out of a known-zero register.
BOOL tai_has_transports(int nNation)
{
  if (count_get_completion(0x45, static_cast<BYTE>(nNation)) != 0)
    return 1;
  if (count_get_completion(0x0B, static_cast<BYTE>(nNation)) != 0)
    return 1;
  if (get_upgrade_level(nNation, 24) != 0 &&
      count_get_completion(0x2A, static_cast<BYTE>(nNation)) != 0)
    return 1;
  return 0;
}

// tai_cloak_if_allowed, oracle 0x4B0210 (52 B). `cmp eax, 1h` and not `test eax, eax`,
// so the source compares TechUseAllowed's result to 1 rather than testing it.
// The trailing `mov ecx, esi / pop esi / jmp CUnit::Cloak` is MSVC's tail call
// for a void member in tail position; nothing in the source says `jmp`.
void tai_cloak_if_allowed(CUnit *pUnit)
{
  BYTE bTech;

  if ((pUnit->dwFlags & 0x100) != 0)
    return;

  bTech = pUnit->GetCloakingTech();
  if (bTech == 0x2C)
    return;
  if (TechUseAllowed(bTech, pUnit->bPlayer, pUnit) != 1)
    return;
  pUnit->Cloak();
}

// tai_attack_helpless_cb, oracle 0x4AAAA0 (117 B). Every path returns 0 -- the
// seven rejects and the accept alike land on the same `xor eax, eax`, so the
// BOOL comes off the materialised zero rather than off any caller. The global
// test is nested inside the `(dwFlags & 4) == 0` arm: `jnz` skips it, so it is
// not a fourth term of a flat chain.
BOOL tai_attack_helpless_cb(CUnit *pUnit, CUnit *pTarget)
{
  if (pUnit->dwUnion134 == 0)
    return 0;
  if ((pUnit->dwFlags & 0x04) == 0) {
    if (dword_6B1A9C != 0)
      return 0;
    if ((pTarget->dwFlags & 0x8000) != 0)
      return 0;
  }
  if (!pUnit->IsEnemyUnit(pTarget))
    return 0;
  if (pUnit->dwUnion7C != 0)
    return 0;
  if (!pUnit->CanTarget(pTarget, 1))
    return 0;
  if ((pUnit->dwFlags & 0x20000) == 0 && !pUnit->IsUnitInWeaponRange(pTarget))
    return 0;

  tai_unit_was_attacked(pUnit, pTarget, 0, 1);
  return 0;
}

// tai_soldier_sibling_boarding, oracle 0x4AFEC0 (90 B). The chain-family reject prologue, then a
// walk of the captain's TSoldier list looking for a sibling (`cmp ecx, edx`
// skips the unit itself) on order 0x60 whose dwFlags bit 0x40 is clear.
// The loop is MSVC's rotated `for`: a `test`/`jz` guard, the body, then
// `mov eax, [eax] / test eax, eax / jnz` back to the body's first instruction.
BOOL tai_soldier_sibling_boarding(CUnit *pUnit)
{
  const TSoldier *pSoldier =
      reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
  const TAI_Captain *pCaptain;
  const TSoldier *p;

  if (pSoldier == NULL || pSoldier->bType != 4 || pSoldier->pUnit != pUnit ||
      (gulUnitTypeFlags[pUnit->wType] & 0x01) != 0)
    return 0;

  pCaptain = pSoldier->pCaptain;
  if (pCaptain->bJob == 2)
    return 0;

  for (p = pCaptain->pSoldiers; p != NULL; p = p->pNext) {
    CUnit *const pOther = p->pUnit;

    if (pOther != pUnit && pOther->bOrderId == 0x60 &&
        (pOther->dwFlags & 0x40) == 0)
      return 1;
  }
  return 0;
}

// CUnit::IsPureSpellcaster, oracle 0x4ABC90 (35 B) plus a 4-byte-aligned jump
// table at 0x4ABCB4 and a 59-byte index table at 0x4ABCBC.
//
//   movzx eax, word ptr [ecx+64h] / add eax, -9h / cmp eax, 3Ah / jnbe <ret 0>
//   movzx eax, byte ptr [eax+4ABCBCh] / jmp dword ptr [eax*4+4ABCB4h]
// is MSVC's two-level switch: bias by the lowest case (9), bound-check the span
// (0x3A -> 59 values, 9..67 inclusive), index a BYTE table of arm numbers, then
// jump through a 2-entry table. The arms, read out of the oracle image: entry 0
// is `mov eax, 1h`, entry 1 `xor eax, eax`, and the index table carries 0 at
// exactly four positions -- 9, 45, 46 and 67. Those four types return 1 and
// everything else, in range or not, returns 0.
__declspec(noinline) BOOL CUnit::IsPureSpellcaster()
{
  switch (this->wType) {
  case 9:
  case 45:
  case 46:
  case 67:
    return 1;
  default:
    return 0;
  }
}

// tai_captain_free, oracle 0x4AF920 (5 B) -- a bare `jmp captain_free`. A void
// function whose whole body is one call to a void sibling is a tail call and
// MSVC emits the `jmp`; nothing in the source says so. It is a call at all
// rather than an inline because of captain_free's own shape, a counted loop
// around two Storm calls, which /Ob2 declines. captain_free is defined two
// hundred lines above, so MSVC gets the chance to splice it in, and the oracle
// proves it did not: 5 bytes, not 51.
void tai_captain_free()
{
  captain_free();
}

// tai_soldier_captain_air_threat / tai_soldier_captain_ground_threat /
// tai_soldier_captain_leader, oracle 0x4AE840 / 0x4AE870 / 0x4AFAF0 (45 B each).
// guard_unit_is_soldier's shape with the constant 1 replaced by a field load --
// instruction-for-instruction identical except the final offset (+0x1C / +0x20
// / +0x24). The first two are declared `void` and cannot express the load.
CUnit *tai_soldier_captain_air_threat(CUnit *pUnit)
{
  const TSoldier *pSoldier =
      reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);

  if (pSoldier == NULL || pSoldier->bType != 4 || pSoldier->pUnit != pUnit ||
      (gulUnitTypeFlags[pUnit->wType] & 0x01) != 0)
    return NULL;
  return pSoldier->pCaptain->pUnit1C;
}

CUnit *tai_soldier_captain_ground_threat(CUnit *pUnit)
{
  const TSoldier *pSoldier =
      reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);

  if (pSoldier == NULL || pSoldier->bType != 4 || pSoldier->pUnit != pUnit ||
      (gulUnitTypeFlags[pUnit->wType] & 0x01) != 0)
    return NULL;
  return pSoldier->pCaptain->pUnit20;
}

CUnit *tai_soldier_captain_leader(CUnit *pUnit)
{
  const TSoldier *pSoldier =
      reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);

  if (pSoldier == NULL || pSoldier->bType != 4 || pSoldier->pUnit != pUnit ||
      (gulUnitTypeFlags[pUnit->wType] & 0x01) != 0)
    return NULL;
  return pSoldier->pCaptain->pUnit24;
}

// tai_soldier_may_pick_own_target, oracle 0x4AE040 (49 B).
//
// An original bug, reproduced on purpose: there is no null test on the +0x134
// load. `mov esi, dword ptr [ecx+134h]` is followed straight by `mov al, byte
// ptr [esi+8h]`, so a unit with no soldier record dereferences 0x00000008.
// Every sibling in this family guards it; this one does not. Do not add the
// guard.
//
// The type tag is a BYTE local, not an int one: `mov al, [esi+8h]` then
// `cmp al, 1h` / `cmp al, 4h`, two 8-bit compares off one load, which is
// tai_captain_job_is_gather's shape. The `movzx` plus 32-bit compare an `int`
// local gives is 2 bytes longer here. pCaptain is loaded before the call, so it
// is its own statement rather than a subexpression of the comparison.
BOOL tai_soldier_may_pick_own_target(CUnit *pUnit)
{
  const TSoldier *pSoldier =
      reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
  const BYTE bType = pSoldier->bType;
  const TAI_Captain *pCaptain;

  if (bType == 1)
    return 1;
  if (bType != 4)
    return 1;

  pCaptain = pSoldier->pCaptain;
  if (GetRegion_CUnit(pUnit) != pCaptain->wRegion)
    return 1;
  return pCaptain->bJob != 7;
}

// tai_captain_add_unit_strength_cb, oracle 0x4AA2C0 (68 B). The area-strength
// accumulator, and its EDX argument is a TAI_Captain * by four dereferences
// (+0x00 WORD region, +0x04 BYTE nation, +0x10/+0x12/+0x14/+0x16 WORD
// accumulators). Each strength is added twice, air to +0x16 then +0x12 and
// ground to +0x14 then +0x10 -- the oracle's order, and the order the source
// must be in. Every path returns 0.
BOOL tai_captain_add_unit_strength_cb(CUnit *pUnit, TAI_Captain *pCaptain)
{
  if ((pUnit->dwFlags & 0x20) != 0)
    return 0;
  if (pUnit->bPlayer != pCaptain->bNation)
    return 0;
  if (GetRegion_CUnit(pUnit) != pCaptain->wRegion)
    return 0;

  {
    const int nAir = pUnit->AirStrength();

    pCaptain->wAirStr16 = static_cast<WORD>(pCaptain->wAirStr16 + nAir);
    pCaptain->wAirStr12 = static_cast<WORD>(pCaptain->wAirStr12 + nAir);
  }
  {
    const int nGround = pUnit->GroundStrength();

    pCaptain->wGroundStr14 = static_cast<WORD>(pCaptain->wGroundStr14 + nGround);
    pCaptain->wGroundStr10 = static_cast<WORD>(pCaptain->wGroundStr10 + nGround);
  }
  return 0;
}

// tai_captain_enlist_guard_cb, oracle 0x4ADA40 (74 B). Five rejects sharing one exit, so one
// `||` chain (tai_captain_region_is_calm's lever) -- and the last term calls, which is fine:
// the chain is short-circuited in the source exactly as the oracle's `jnz`s
// short-circuit it. Every path returns 0.
BOOL tai_captain_enlist_guard_cb(CUnit *pUnit, TAI_Captain *pCaptain)
{
  const TSoldier *pSoldier =
      reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);

  if (pSoldier == NULL || pSoldier->bType != 1 ||
      (pUnit->dwFlags & 0x20) != 0 || pUnit->bPlayer != pCaptain->bNation ||
      GetRegion_CUnit(pUnit) != pCaptain->wRegion)
    return 0;

  SAI_RemoveUnit(pUnit, 0);
  tai_soldier_add_to_captain(pCaptain, pUnit, 1);
  return 0;
}

// tai_captain_assign_unit_to_region, oracle 0x4B13D0 (71 B). The `push 1h` sits
// above `call GetRegion_xy`, which is the whole shape question: it is
// tai_soldier_add_to_captain's stack argument pushed before its own register
// arguments are computed, that is, right-to-left evaluation of one nested
// expression. Hoisting the region into a local would sink the push to just
// before `call tai_soldier_add_to_captain`, the opposite of the
// count_get_completion lever three neighbouring rows needed, so it is measured,
// not assumed. `mov bl, byte ptr [esi+4Ch]` is likewise hoisted above the call
// by the scheduler, not by the source.
void tai_captain_assign_unit_to_region(CUnit *pUnit, int nX, int nY)
{
  if (pUnit->dwUnion134 != 0)
    SAI_RemoveUnit(pUnit, 0);
  {
    const BYTE bNation = pUnit->bPlayer;

    tai_soldier_add_to_captain(&sgCaptain[bNation][GetRegion_xy(nX, nY)], pUnit, 1);
  }
}

// tai_soldier_issue_order, oracle 0x4ADA90 (120 B). A genuine if/else -- the
// `jmp $+0xCh` after SetOrder_interrupt skips the else arm at +0x33, it is not
// a shared tail. `cmp word ptr [esi+64h], 67h` is a direct field compare with
// no `movzx`, so wType is not hoisted into a local here.
//
// The region is cached on the soldier, not the captain: EDI is reloaded from
// +0x134 and checked at +0x08/+0x0C before `mov word ptr [edi+0Ah], ax`. That
// is TSoldier +0x0A, not TAI_Captain +0x0A; the two records share the offset.
void tai_soldier_issue_order(CUnit *pUnit, int nOrderId)
{
  TSoldier *pSoldier;

  if ((pUnit->dwFlags & 0x10) != 0) {
    if (pUnit->wType == 0x67 && pUnit->pOrderTarget != NULL)
      return;
    pUnit->Deburrow();
    if (pUnit->pUnion74 == NULL)
      pUnit->SetOrder_interrupt(nOrderId, 0);
  } else if (pUnit->pOrderTarget == NULL) {
    pUnit->SetOrder(nOrderId);
  }

  if (nOrderId != 0x9E)
    return;

  pSoldier = reinterpret_cast<TSoldier *>(pUnit->dwUnion134);
  if (pSoldier == NULL || pSoldier->bType != 4 || pSoldier->pUnit != pUnit ||
      (gulUnitTypeFlags[pUnit->wType] & 0x01) != 0)
    return;
  pSoldier->wRegion = GetRegion_CUnit(pUnit);
}


// ---------------------------------------------------------------------------

// tai_captain_units_connected, oracle 0x4AB420 (39 B)
//
// The player is read into a local before the calls, and that is forced, not a
// style choice: `mov bl, byte ptr [esi+4Ch]` sits at +0x04, ahead of both
// GetRegion_CUnit calls. A field load cannot be sunk past a call that might
// write it, so the oracle can only have that shape if the source read it into a
// local first. The argument order is MSVC's right-to-left evaluation, unedited:
// the stack word (`push eax`) is computed first, then EDX, then ECX. nArg1 is a
// `CUnit *` where the header says `int` -- `mov ecx, edx` /
// `call GetRegion_CUnit`, whose ECX the callee dereferences -- so the cast is
// local.
int tai_captain_units_connected(CUnit *pUnit, CUnit *pUnit2)
{
  const BYTE bPlayer = pUnit->bPlayer;

  return tai_captain_regions_connected(bPlayer, GetRegion_CUnit(pUnit),
                    GetRegion_CUnit(pUnit2));
}

// tai_captain_unit_connected_to_xy, oracle 0x4AB450 (50 B). Declared in
// CUnitCombat.h, so `__fastcall` is carried explicitly to match that declaration.
//
// tai_captain_units_connected's twin with GetRegion_xy in place of the second
// GetRegion_CUnit; X arrives in a register and Y on the stack (`ret 4h`).
BOOL __fastcall tai_captain_unit_connected_to_xy(CUnit *pUnit, int nX, int nY)
{
  const BYTE bPlayer = pUnit->bPlayer;

  return tai_captain_regions_connected(bPlayer, GetRegion_CUnit(pUnit),
                    GetRegion_xy(nX, nY));
}

// captain_ptr_to_id, oracle 0x4AE8A0 (56 B)
//
// captain_id_to_ptr's inverse. `mov eax, 4EC4EC4Fh / mul edi / shr edx, 4h` is
// MSVC's unsigned divide by 0x34, so, exactly as in soldier_id, the byte
// difference must be cast to `unsigned` before dividing; a plain ptrdiff_t
// subtraction gives `imul`/`sar`. The field at +0x04 is the first sgCaptain
// index, `movzx ecx, byte ptr [edi+4h]` then `sub edi, [ecx*4+6B1A74h]`, and
// the quotient is bounds-checked against NumAreas(); the id is
// `nation*0x9C4 + 1 + area`, 1-based like the areas everywhere in this TU.
//
// `bNation` is declared `BYTE` deliberately, out of five shapes measured: an
// 8-bit local cannot live in ESI or EDI, so BYTE forces it into ECX and
// re-ranks the whole callee-saved assignment. The oracle's EDI=pCaptain /
// ESI=nAreas / ECX=nNation falls out, and with it the shared exit, where
// `cmp edx, esi / pop esi / jae` restores ESI before the branch. Spelled
// `unsigned` the body is 61 B with an unmergeable second exit block.
int captain_ptr_to_id(TAI_Captain *pCaptain)
{
  unsigned nAreas;
  BYTE bNation;
  unsigned nArea;

  if (pCaptain == NULL)
    return 0;

  nAreas = NumAreas();
  bNation = pCaptain->bNation;
  nArea = static_cast<unsigned>(reinterpret_cast<BYTE *>(pCaptain)
                                - reinterpret_cast<BYTE *>(sgCaptain[bNation]))
          / sizeof(TAI_Captain);
  if (nArea >= nAreas)
    return 0;
  return bNation * AI_AREAS_117 + 1 + nArea;
}

// tai_captain_has_forming_soldier, oracle 0x4AB110 (60 B)
//
// The TSoldier list walk off `TAI_Captain::pSoldiers`, and the polarity is the
// oracle's: `test byte ptr [ecx+0DCh], 1h / je` lands on `mov eax, 1h`, so a
// unit whose flag 1 is clear returns TRUE. Written the intuitive way round, the
// `je` lands on the next-link instead -- same size, opposite meaning, and only
// the branch target says which.
//
// nOrder is `unsigned`, not `BYTE`: the oracle spends
// `movzx ecx, byte ptr [ecx+4Dh] / cmp ecx, 6Ah / cmp ecx, 96h`, the 32-bit
// compares a widened local produces, where a BYTE local gives
// `mov cl, ... / cmp cl, 6Ah`, four bytes shorter. It sits inside its own block
// so the widening stays after the flag test, on the short-circuit side of the
// `je`. `jmp $+0x7 / lea esp, dword ptr [esp]` at +0x07 is loop alignment.
BOOL tai_captain_has_forming_soldier(TAI_Captain *pCaptain)
{
  TSoldier *pSoldier = pCaptain->pSoldiers;

  while (pSoldier != NULL) {
    const CUnit *pUnit = pSoldier->pUnit;
    if ((pUnit->dwFlags & 1) == 0)
      return 1;
    {
      const unsigned nOrder = pUnit->bOrderId;
      if (nOrder == 0x6A || nOrder == 0x96)
        return 1;
    }
    pSoldier = pSoldier->pNext;
  }
  return 0;
}

// Internal copy of tai_captain_has_forming_soldier, same bytes. Its caller keeps a value in a register
// across the call, which only an internal callee allows; the link folded the
// two copies into one address.
static __declspec(noinline) BOOL tai_captain_has_forming_soldier_local(TAI_Captain *pCaptain)
{
  TSoldier *pSoldier = pCaptain->pSoldiers;

  while (pSoldier != NULL) {
    const CUnit *pUnit = pSoldier->pUnit;
    if ((pUnit->dwFlags & 1) == 0)
      return 1;
    {
      const unsigned nOrder = pUnit->bOrderId;
      if (nOrder == 0x6A || nOrder == 0x96)
        return 1;
    }
    pSoldier = pSoldier->pNext;
  }
  return 0;
}

// tai_captain_job_idle, oracle 0x4AC9B0 (65 B)
//
// `neg eax / sbb eax, eax / lea edx, dword ptr [eax+1h]` is the branchless
// lowering of `!x`: neg sets CF iff EAX was nonzero, sbb turns that into 0/-1,
// and the lea adds one, so the second argument is
// `!aiscript_has_campaign(...)`. The last call is a tail `jmp` because it is
// the last statement of a void function; the `pop esi` scheduled into the
// argument setup is MSVC's.
void tai_captain_job_idle(TAI_Captain *pCaptain)
{
  tai_captain_recount_strength(pCaptain);
  if ((pCaptain->bFlags & 0x40) != 0)
    return;
  tai_captain_decay_need(&pCaptain->wUnknown0E, !aiscript_has_campaign(pCaptain->bNation));
  tai_captain_decay_need(&pCaptain->wUnknown0C, !aiscript_has_campaign(pCaptain->bNation));
}

// oracle 0x4AEAA0 (79 B)
BOOL tai_captain_unit_region_is_calm(CUnit *pUnit)
{
  const TAI_Captain *pCaptain;
  unsigned nRegion;
  BYTE bPlayer;

  if (pUnit == NULL)
    return 0;
  if (pUnit->IsDeadOrDying())
    return 0;

  bPlayer = pUnit->bPlayer;
  nRegion = GetRegion_CUnit(pUnit);
  pCaptain = &sgCaptain[bPlayer][nRegion];
  if (pCaptain->bJob == 3 || pCaptain->pUnit1C != NULL ||
      pCaptain->pUnit20 != NULL || (pCaptain->bFlags & 0x20) != 0)
    return 0;
  return 1;
}

// oracle 0x4AE900 (83 B)
TAI_Captain *captain_id_to_ptr(int nCaptainId)
{
  unsigned nAreas;
  BYTE bNation;
  unsigned nArea;
  TAI_Captain *pBase;

  if (nCaptainId == 0)
    return NULL;

  nAreas = NumAreas();
  bNation = static_cast<BYTE>(static_cast<unsigned>(nCaptainId) / AI_AREAS_117);
  nArea = nCaptainId - bNation * AI_AREAS_117;
  if (bNation >= PlayerLobbySlots())  // Hyperion
    return NULL;
  pBase = sgCaptain[bNation];
  if (pBase == NULL)
    return NULL;
  if (nArea == 0)
    return NULL;
  if (nArea <= nAreas)
    return &pBase[nArea - 1];
  return NULL;
}

// tai_captain_any_attack_job_active, oracle 0x4AB5D0 (76 B). A two-level jump
// table on bJob: jobs 1, 2, 8 and 9 return TRUE and 3..7 continue. The explicit `break` arms
// for 3..7 follow the table; `default: break;` compiles the same. The area scan runs from 1
// up to NumAreas(), exclusive.
BOOL tai_captain_any_attack_job_active(int nNation)
{
  const unsigned nAreas = NumAreas();
  unsigned i;

  for (i = 1; i < nAreas; i++) {
    switch (sgCaptain[nNation][i].bJob) {
      case 1:
      case 2:
      case 8:
      case 9:
        return 1;
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
        break;
    }
  }
  return 0;
}

// tai_captain_take_bunker_site, oracle 0x4AE380 (85 B), and
// tai_captain_take_turret_site, oracle 0x4B1470 (85 B)
//
// The area scan with a consuming tail, and the pair shows again that the TU
// has a repeated shape: instruction-for-instruction identical but for
// the bitmask, 2h against 10h (and its complement, -3h against -11h).
//
// The return is the "ran off the end" test inverted: `cmp ecx, eax / jnz` after
// the loop, so `i != nAreas` means the loop broke and the found area is
// consumed -- flag cleared, position handed to GetRegionPosition, return 1 --
// while `i == nAreas` returns 0. The oracle re-indexes `sgCaptain[nation][i]`
// after the loop (`mov eax, [edi*4+6B1A74h] / imul edx, ecx, 34h`) instead of
// reusing the strength-reduced pointer, which is what the plain subscript
// spells. nArg1 is a `point16_t *` where the header says `int`: it goes into
// GetRegionPosition's EDX out-param, whose callee writes two WORDs at +0/+2.
BOOL tai_captain_take_bunker_site(int nNation, mpoint_t *pPos)
{
  const unsigned nAreas = NumAreas();
  unsigned i;

  for (i = 1; i < nAreas; i++) {
    if ((sgCaptain[nNation][i].bFlags & 0x02) != 0)
      break;
  }
  if (i == nAreas)
    return 0;
  sgCaptain[nNation][i].bFlags &= ~0x02;
  GetRegionPosition(static_cast<WORD>(i), pPos);
  return 1;
}

// tai_captain_take_turret_site -- the twin; see tai_captain_take_bunker_site above.
BOOL tai_captain_take_turret_site(int nNation, mpoint_t *pPos)
{
  const unsigned nAreas = NumAreas();
  unsigned i;

  for (i = 1; i < nAreas; i++) {
    if ((sgCaptain[nNation][i].bFlags & 0x10) != 0)
      break;
  }
  if (i == nAreas)
    return 0;
  sgCaptain[nNation][i].bFlags &= ~0x10;
  GetRegionPosition(static_cast<WORD>(i), pPos);
  return 1;
}

// tai_captain_region_or_neighbor_held, oracle 0x4AB810 (85 B)
//
// The decrement must happen before the call, and that is the whole delta: the
// oracle's `dec esi` sits at +0x34, ahead of region_has_worker_or_building, so
// the loop test at the bottom is a fresh `test esi, esi / jne`. Decrementing
// after the call lets MSVC branch on the `dec`'s own flags and the body comes
// out 83 B, instruction-exact everywhere else.
//
// `[ebp-4h]` is address-taken (`lea edx, dword ptr [ebp-4h]` for
// GetRegionPaths' out-param), so MSVC keeps the walking pointer in memory and
// re-stores it every iteration; the load at +0x2E is the peeled first
// iteration, and the back edge lands on the `movzx`.
BOOL tai_captain_region_or_neighbor_held(int nPlayer, int nRegion)
{
  WORD *pPaths;
  int nCount;

  if (region_has_worker_or_building(nPlayer, nRegion))
    return 1;

  nCount = GetRegionPaths(static_cast<WORD>(nRegion), &pPaths);
  while (nCount != 0) {
    const WORD wRegion = *pPaths;

    nCount--;
    if (region_has_worker_or_building(nPlayer, wRegion))
      return 1;
    pPaths++;
  }
  return 0;
}

// tai_captain_neighbors_calm, oracle 0x4AE700 (95 B)
//
// tai_captain_region_or_neighbor_held's sibling over GetRegionNeighbors's path
// list, with tai_captain_region_is_calm's reject chain as the per-region test,
// written out rather than called because the oracle's four rejects share one
// `xor eax, eax` exit inside this body.
//
// The empty list returns TRUE: `test edx, edx / jz` jumps to `mov eax, 1h`, so
// a region with no paths is vacuously all clear. The loop is a do/while under
// that guard, which is what the rotated `test edx, edx / jnz` bottom says.
//
// `sgCaptain[nNation]` is hoisted into ESI by MSVC -- there is no call in the
// loop, so the subscript can be spelled inline and still produce the single
// `mov esi, dword ptr [esi*4+6B1A74h]` before it. Contrast
// tai_captain_cancel_gather_jobs, which calls aiscript_has_campaign inside its
// loop and therefore reloads from the same spelling.
BOOL tai_captain_neighbors_calm(int nNation, int nRegion)
{
  WORD *pPaths;
  int nCount;

  nCount = GetRegionNeighbors(static_cast<WORD>(nRegion), &pPaths);
  if (nCount == 0)
    return 1;

  do {
    const TAI_Captain *pCaptain = &sgCaptain[nNation][*pPaths];
    nCount--;
    pPaths++;
    if (pCaptain->bJob == 3 || pCaptain->pUnit1C != NULL ||
        pCaptain->pUnit20 != NULL || (pCaptain->bFlags & 0x20) != 0)
      return 0;
  } while (nCount != 0);
  return 1;
}

// tai_captain_job_defend, oracle 0x4AC740 (116 B)
//
// tai_captain_job_idle's tail verbatim, behind a two-armed guard whose branch
// targets are the only thing that fixes its polarity: `jz` off the first
// IsCritter at +0x1D lands on the call setup (+0x2F) while `jnz` off the second
// at +0x2D skips it (+0x3B). So the call fires when a slot is non-NULL and its
// unit is not a critter -- `(a && !crit(a)) || (b && !crit(b))`. Reading the
// mnemonics without the targets gives the negation of this.
void tai_captain_job_defend(TAI_Captain *pCaptain)
{
  tai_captain_recount_strength(pCaptain);
  tai_captain_retarget_soldiers(pCaptain);

  if ((pCaptain->pUnit1C != NULL && !pCaptain->pUnit1C->IsCritter()) ||
      (pCaptain->pUnit20 != NULL && !pCaptain->pUnit20->IsCritter()))
    sai_soldier_set_type(pCaptain, 6);

  if ((pCaptain->bFlags & 0x40) != 0)
    return;
  tai_captain_decay_need(&pCaptain->wUnknown0E, !aiscript_has_campaign(pCaptain->bNation));
  tai_captain_decay_need(&pCaptain->wUnknown0C, !aiscript_has_campaign(pCaptain->bNation));
}

// tai_captain_service, oracle 0x4B0CE0 (92 B). A ten-arm jump table on
// TAI_Captain::bJob. The case order in the source is the arm order in the body, not 0..9:
// 0, 6, 3, 7, {8,9}, {4,5}, {1,2}. MSVC emits case blocks in source order, so numeric order
// would move the table's entries. Each arm is a tail jump. The `__declspec(noinline)` is for
// tai_captain_service_round_robin's sake (see the note there).
__declspec(noinline) void tai_captain_service(int nNation, int nArea)
{
  TAI_Captain *pCaptain = &sgCaptain[nNation][nArea];

  pCaptain->bFlags &= ~0x04;
  if (pCaptain->bTimer != 0)
    pCaptain->bTimer--;

  switch (pCaptain->bJob) {
    case 0: tai_captain_job_idle(pCaptain); break;
    case 6: tai_captain_job_under_attack(pCaptain); break;
    case 3: tai_captain_job_lost(pCaptain); break;
    case 7: tai_captain_job_reinforce(pCaptain); break;
    case 8:
    case 9: tai_captain_job_gather(pCaptain); break;
    case 4:
    case 5: tai_captain_job_defend(pCaptain); break;
    case 1:
    case 2: tai_captain_job_attack(pCaptain); break;
  }
}

// tai_captain_cancel_gather_jobs, oracle 0x4AD230 (102 B). Declared in
// SAI_Banker.h, so `__fastcall` is carried explicitly to match that declaration.
//
// The two adjacent byte assignments at +0x05 (bJob) and +0x06 compile to one merged WORD
// store; there is no WORD field there. The sgCaptain base is reloaded every iteration
// because aiscript_has_campaign, called inside the loop, could write it.
void __fastcall tai_captain_cancel_gather_jobs(int nation)
{
  const unsigned nAreas = NumAreas();
  unsigned i;

  for (i = 1; i < nAreas; i++) {
    TAI_Captain *pCaptain = &sgCaptain[nation][i];

    if (pCaptain->bJob != 8)
      continue;
    if (pCaptain->wRegion == 0)
      continue;
    pCaptain->bFlags &= ~0x20;
    pCaptain->bJob = 0;
    pCaptain->bUnknown06 = 10;
    if ((pCaptain->bFlags & 0x40) != 0 && aiscript_has_campaign(pCaptain->bNation) == 0)
      pCaptain->bUnknown06 = 0x28;
  }
}

// tai_captain_service_round_robin, oracle 0x4B0D70 (160 B)
//
// The round-robin driver: 0x14D iterations, each servicing one captain of one
// nation and advancing the cursor pair.
//
// This body depends on tai_captain_service not being inlined: both live in this
// TU, and /Ob2 would splice the callee and its ten-arm jump table into the loop.
// The `__declspec(noinline)` that keeps the call sits on the declaration in
// src117/h/TAI_Captain.h.
// careless edit from silently costing 124 bytes.
//
// `cmp ecx, 8h / cmovz ecx, eax` is MSVC if-converting the wrap, so the source
// keeps the plain `if`, and the nation cursor's reload after every call is the
// loop rotation MSVC applies to a global read across calls.
void tai_captain_service_round_robin()
{
  const unsigned nAreas = NumAreas();
  // Hyperion: 1.17's 333 captain slots a frame for 8 nations, and as many more for
  // each nation past them, so a pass over every nation still needs 60 frames (revision 17)
  int i = 0x14D * PlayerLobbySlots() / PLAYER_LOBBY_117;

  do {
    if (dword_6B1A98 < nAreas && gPlayerData[dword_6B1A94].nType == 1)
      tai_captain_service(dword_6B1A94, dword_6B1A98);

    dword_6B1A98++;
    if (dword_6B1A98 == AI_AREAS_117) {
      DWORD dwNation = dword_6B1A94 + 1;

      dword_6B1A98 = 1;
      if (dwNation >= (DWORD)PlayerLobbySlots())  // Hyperion
        dwNation = 0;
      dword_6B1A94 = dwNation;
      tai_captain_recompute_threats(dword_6B1A94);
      if (gPlayerData[dword_6B1A94].nType == 1 &&
          aiscript_has_campaign(dword_6B1A94) == 0)
        tai_captain_start_expansion(dword_6B1A94);
    }
  } while (--i != 0);
}


// (dword_6B1A9C's `static` is seated in the globals block at the top of this
// TU, ahead of tai_attack_helpless_cb's read of it.)

// find_first_unit_near -- oracle 0x442B20, oracle TU find.cpp, unported,
// declared here with C linkage, following src117/lang/CUnitSearches.cpp. x and
// y are 16-bit and this TU's own callers prove it: tai_call_attackers_on_unit
// and tai_call_attackers_if_disabled both emit
//   mov dx, word ptr [eax+16h] / mov cx, word ptr [eax+14h]
// -- bare 16-bit register writes that CBullet.h's `int x, int y` view cannot
// produce (it gives `movzx`). Both objects this TU produces carry exactly one
// symbol spelling, `@find_first_unit_near@20`, byte-identical to what
// CBullet.h's `int` view emits, because `extern "C"` __fastcall decorates on
// total argument bytes (5 x 4) and a WORD parameter still occupies a full slot.
// Kept TU-local deliberately so an aggregate TU can include CBullet.h as well.
// Hyperion: the definition (find.cpp) reads the position as ints, and this
// TU's callers pass it as 16 bits. The view takes the definition's types, which
// the x64 build needs, and the macro narrows the arguments as 1.17's did
//.
extern "C" CUnit *__fastcall find_first_unit_near(int x, int y, int nRadius, CUnitSearchPredicate pfnFilter, CUnit *pContext);
#define find_first_unit_near(a0, a1, a2, a3, a4) (find_first_unit_near)(PosU(a0), PosU(a1), (a2), (a3), (a4))


// TAI_Captain_aiscript_clear_combat_data, oracle 0x4AD2A0 (108 B)
void TAI_Captain_aiscript_clear_combat_data(int nNation, rect32_t *pArea)
{
  const unsigned nAreas = NumAreas();
  unsigned i;
  int x, y;

  for (i = 1; i < nAreas; i++) {
    GetRegionCenter(i, &x, &y);
    if (x >= pArea->left && x <= pArea->right &&
        y >= pArea->top && y <= pArea->bottom)
      sai_soldier_set_type(&sgCaptain[nNation][i], 0);
  }
}

// TAI_Captain_aiscript_value_area, oracle 0x4AD9E0 (81 B)
void TAI_Captain_aiscript_value_area(int nNation, int nX, int nY)
{
  TAI_Captain *const pCaptain = &sgCaptain[nNation][GetRegion_xy(nX, nY)];

  if (pCaptain->bJob < 5) {
    sai_soldier_set_type(pCaptain, 5);
    if (pCaptain->wUnknown0C < 200)
      pCaptain->wUnknown0C = 200;
    if (pCaptain->wUnknown0E < 200)
      pCaptain->wUnknown0E = 200;
  }
}

// tai_call_attackers_on_unit, oracle 0x4AF220 (79 B)
//
// Two proximity sweeps around the unit, at radius 0x280 and then 0x500, with a
// flag global raised between them so the shared filter
// (tai_attack_helpless_cb) can tell the two passes apart.
//
// The coordinates are 16-bit: `mov dx, word ptr [eax+16h]` / `mov cx, word ptr
// [eax+14h]` are bare 16-bit register writes, so this TU declares the WORD
// view of find_first_unit_near above rather than CBullet.h's `int` one.
//
// The oracle schedules the two sweeps asymmetrically -- the first store
// precedes the whole call setup, the second follows it -- and that is not in
// the source: it is what MSVC does once dword_6B1A9C is `static` and it can
// prove the store does not alias the sprite reads. Caching the two coordinates
// in locals ahead of the second sweep would move two payload-form bytes and is
// not taken; the natural form is kept.
void tai_call_attackers_on_unit(CUnit *pUnit)
{
  dword_6B1A9C = 0;
  find_first_unit_near(pUnit->pSprite->wPosX, pUnit->pSprite->wPosY, 0x280,
             tai_attack_helpless_cb, pUnit);
  {
    // __restrict: the payload's literal flag must not pin the sprite reads.
    CSprite *__restrict pSprite = pUnit->pSprite;
    dword_6B1A9C = 1;
    find_first_unit_near(pSprite->wPosX, pSprite->wPosY, 0x500, tai_attack_helpless_cb, pUnit);
  }
}

// tai_call_attackers_if_disabled, oracle 0x4AF270 (78 B)
//
// tai_call_attackers_on_unit's single-sweep sibling behind a cooldown and a
// three-way guard. Every one of the four branches lands on one of two
// addresses, offset 77 (the `ret`) or offset 50 (the search), which is what
// makes the guard a single early `return`:
//   +04 jnz -> 77   bUnion54 != 0
//   +1B jnz -> 50   bLockdownTimer != 0
//   +24 jnz -> 50   bParalyzeTimer != 0
//   +30 jz  -> 77   (dwFlags & 0x8000) == 0
// so the last three are `A == 0 && B == 0 && (F & m) == 0 -> return`.

// The oracle hoists `cmp byte ptr [ecx+117h], 0h` above the two stores and
// leaves its `jnz` behind -- tai_call_attackers_on_unit's alias fact again:
// with dword_6B1A9C `static` MSVC may move the CUnit load across the store to
// it, and it does. `__restrict` gives the payload, where the global is a
// literal address, the same fact.
void tai_call_attackers_if_disabled(CUnit *__restrict pUnit)
{
  if (pUnit->bUnion54 != 0)
    return;

  pUnit->bUnion54 = 0x0F;
  dword_6B1A9C = 0;
  if (pUnit->bLockdownTimer == 0 && pUnit->bParalyzeTimer == 0 &&
      (pUnit->dwFlags & 0x8000) == 0)
    return;

  find_first_unit_near(pUnit->pSprite->wPosX, pUnit->pSprite->wPosY, 0x280,
             tai_attack_helpless_cb, pUnit);
}

// tai_call_attackers_when_hit, oracle 0x4AF2C0 (55 B)
//
// A conditional tail jump into a sibling in this same TU: `jnb
// tai_call_attackers_on_unit` with ECX never touched, which is MSVC folding
// `if (c) { f(pUnit); return; }` into a tail call and then into the branch
// itself. It constrains both bodies -- tai_call_attackers_on_unit must not be
// inlined here or the jump has nowhere to go. The other three branches all land
// on the single `ret` at offset 54.
void tai_call_attackers_when_hit(CUnit *pUnit)
{
  if (pUnit->bCrunchyCount >= 4) {
    tai_call_attackers_on_unit(pUnit);
    return;
  }
  if (dword_6B6900 != 0)
    return;
  if (pUnit->wType != 0x49)
    return;
  if (pUnit->pUnionC0 == NULL)
    return;

  tai_call_attackers_on_unit(pUnit->pUnionC0);
  dword_6B6900 = 1;
}

// aiscript_enter_bunker, oracle 0x4AF490 (137 B)
//
// The shared shape of the three rect walkers (enter_bunker, enter_transport,
// exit_transport) and of aiscript_assign_all_order. Two levers, both measured:
//
//  1. The register copy is assigned first. `pp = get_units_in_rect(&rc);` then
//     `ppList = pp;` gives the oracle's `mov esi, eax / mov [ebp-4], esi`.
//     Writing the spilled copy first inverts it to `mov [ebp-4], eax /
//     mov esi, eax` and routes the first dereference through EAX -- a 2-delta
//     miss in all four bodies. The definition order of the two locals is not
//     the lever; the order of the two assignments is.
//
//  2. The narrowing stores are in source order top/bottom/left/right, which is
//     what the oracle emits (+4, +0xC, +0, +8 -- the interleaving is the
//     addressing, not a reordering).
void aiscript_enter_bunker(int nNation, rect32_t *pArea)
{
  rect16_t rc;
  CUnit **ppList;
  CUnit **pp;
  CUnit *pUnit;

  rc.top = static_cast<short>(pArea->top);
  rc.bottom = static_cast<short>(pArea->bottom);
  rc.left = static_cast<short>(pArea->left);
  rc.right = static_cast<short>(pArea->right);

  pp = get_units_in_rect(&rc);
  ppList = pp;
  for (; (pUnit = *pp) != NULL; pp++) {
    if (pUnit->bPlayer == nNation) {
      CUnit *const pBunker = pUnit->AIFindNearestBunker();

      if (pBunker != NULL && pUnit->CanEnterTransport(pBunker))
        pUnit->SetOrder_CUnit(0x5C, pBunker);
    }
  }
  finish_unit_iteration(ppList);
}

// aiscript_enter_transport, oracle 0x4AF520 (131 B)
void aiscript_enter_transport(int nNation, rect32_t *pArea)
{
  rect16_t rc;
  CUnit **ppList;
  CUnit **pp;
  CUnit *pUnit;

  rc.top = static_cast<short>(pArea->top);
  rc.bottom = static_cast<short>(pArea->bottom);
  rc.left = static_cast<short>(pArea->left);
  rc.right = static_cast<short>(pArea->right);

  pp = get_units_in_rect(&rc);
  ppList = pp;
  for (; (pUnit = *pp) != NULL; pp++) {
    if (pUnit->bPlayer == nNation) {
      CUnit *const pTransport =
          pUnit->FindNearestInRect(&sgMapPixRect_g, tai_unit_is_boardable_transport_cb, pUnit);

      if (pTransport != NULL)
        pUnit->SetOrder_CUnit(0x5C, pTransport);
    }
  }
  finish_unit_iteration(ppList);
}

// aiscript_exit_transport, oracle 0x4AF6D0 (120 B)
void aiscript_exit_transport(int nNation, rect32_t *pArea)
{
  rect16_t rc;
  CUnit **ppList;
  CUnit **pp;
  CUnit *pUnit;

  rc.top = static_cast<short>(pArea->top);
  rc.bottom = static_cast<short>(pArea->bottom);
  rc.left = static_cast<short>(pArea->left);
  rc.right = static_cast<short>(pArea->right);

  pp = get_units_in_rect(&rc);
  ppList = pp;
  for (; (pUnit = *pp) != NULL; pp++) {
    if (pUnit->bPlayer == nNation && pUnit->IsATransport())
      pUnit->SetOrder_CUnit(0x70, pUnit);
  }
  finish_unit_iteration(ppList);
}

// tai_captain_claim_defence_region, oracle 0x4B0030 (101 B)
void tai_captain_claim_defence_region(int nNation, int nRegion, int nArg2)
{
  TAI_Captain *const pCaptain = &sgCaptain[nNation][nRegion];

  if (pCaptain->bJob != 0)
    return;

  sai_soldier_set_type(pCaptain, 4);
  pCaptain->bFlags |= 0x01;
  if (nArg2 == 0 && aiscript_has_campaign(nNation) == 0)
    return;

  pCaptain->bFlags |= 0x40;
  if (nArg2 == 0)
    return;

  if (!IsInaccessibleRegion(nRegion))
    pCaptain->wUnknown0C = 1000;
}

// tai_captain_find_spare_hydralisk, oracle 0x4B0350 (107 B)
CUnit *tai_captain_find_spare_hydralisk(int nPlayer)
{
  CUnit *pResult = NULL;
  CUnit *pUnit;

  for (pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    const TSoldier *pSoldier;
    int nJob;

    if (pUnit->IsDeadOrDying())
      continue;
    if (pUnit->wType != 0x26)
      continue;
    if (pUnit->dwUnion7C != 0)
      continue;
    if (pUnit->pOrderTarget != NULL)
      continue;

    pSoldier = reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
    if (pSoldier == NULL)
      continue;
    if (pSoldier->bType == 1)
      return pUnit;
    if (pSoldier->bType != 4)
      continue;

    pResult = pUnit;
    nJob = pSoldier->pCaptain->bJob;
    if (nJob == 0 || nJob == 5 || nJob == 4)
      return pUnit;
  }
  return pResult;
}

// tai_captain_find_spare_mutalisk, oracle 0x4B03C0 (107 B). tai_captain_find_spare_hydralisk's twin; the only
// difference in the whole body is the unit type, 0x2B against 0x26.
CUnit *tai_captain_find_spare_mutalisk(int nPlayer)
{
  CUnit *pResult = NULL;
  CUnit *pUnit;

  for (pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    const TSoldier *pSoldier;
    int nJob;

    if (pUnit->IsDeadOrDying())
      continue;
    if (pUnit->wType != 0x2B)
      continue;
    if (pUnit->dwUnion7C != 0)
      continue;
    if (pUnit->pOrderTarget != NULL)
      continue;

    pSoldier = reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
    if (pSoldier == NULL)
      continue;
    if (pSoldier->bType == 1)
      return pUnit;
    if (pSoldier->bType != 4)
      continue;

    pResult = pUnit;
    nJob = pSoldier->pCaptain->bJob;
    if (nJob == 0 || nJob == 5 || nJob == 4)
      return pUnit;
  }
  return pResult;
}

// tai_captain_for_unit, oracle 0x4B0960 (27 B)
//
// The `&sgCaptain[nation][area]` shape with the two indices read out of the
// unit rather than passed in: `mov bl, byte ptr [ecx+4Ch]` is CUnit::bPlayer
// (saved across the call, hence EBX and not a scratch register) and
// `call GetRegion_CUnit` with ECX untouched is the region. `imul eax, eax, 34h`
// then `add eax, dword ptr [ecx*4+6B1A74h]` is the two strides, exactly as in
// tai_captain_region_is_calm.
//
// GetRegion_CUnit(pUnit) must stay inside the subscript: binding the region to
// a named local transposes `imul eax, eax, 34h` and `movzx ecx, bl` and loses
// the match (six shapes measured; the same lever appears in
// tai_captain_enlist_as_attacker).
TAI_Captain *tai_captain_for_unit(CUnit *pUnit)
{
  const BYTE bPlayer = pUnit->bPlayer;

  return &sgCaptain[bPlayer][GetRegion_CUnit(pUnit)];
}

// aiscript_sharedvision, oracle 0x4B0FE0 (43 B)
//
// A bit set or clear over the SharedVision mask table. The mask is hoisted
// above the test (`mov eax, 1h / shl eax, cl` before `cmp [ebp+8h], 0h`)
// because both arms need it; MSVC does that CSE itself, and writing the shift
// twice is what the oracle's register assignment wants.
//
// The mask must not be the first named local, and that is the whole delta.
// Binding it to a leading `const DWORD dwMask` is size-exact + 3 and pushes a
// callee-saved register: MSVC then puts the mask in ESI and moves the nation
// index out of EDX into EAX, where the oracle keeps the index in EDX and widens
// it in place (`mov eax, 1h / movzx edx, dl / shl eax, cl`). Three sources
// produce the oracle's body -- this one, a leading `const int nNation = bArg1;`
// before the mask local, and a `DWORD *` bound to `&SharedVision[bArg1]` -- and
// the one that fails is the one that defines the mask first. `not eax / and` is
// `&= ~mask` and not a second literal.
void aiscript_sharedvision(int nArg0, BYTE bArg1, int nArg2)
{
  // Hyperion: a game with Hyperion's slots reaches the slots past 31 (stage D)
  if (g_hxPlayers) {
    if (nArg2 != 0)
      PlayerVisionAdd(bArg1, nArg0);
    else
      PlayerVisionRemove(bArg1, nArg0);
    return;
  }
  if (nArg2 != 0)
    SharedVision[bArg1] |= 1 << nArg0;
  else
    SharedVision[bArg1] &= ~(1 << nArg0);
}

// tai_captain_enlist_as_attacker, oracle 0x4B0980 (64 B)
void tai_captain_enlist_as_attacker(CUnit *pUnit, CUnit *pUnit2)
{
  const BYTE bPlayer = pUnit->bPlayer;
  TAI_Captain *const pCaptain = &sgCaptain[bPlayer][GetRegion_CUnit(pUnit2)];

  if (pCaptain->bJob == 0)
    sai_soldier_set_type(pCaptain, 1);
  tai_soldier_add_to_captain(pCaptain, pUnit2, 1);
}

// aiscript_assign_all_order, oracle 0x4B0EC0 (75 B)
void aiscript_assign_all_order(int nNation, int nOrderId, mrect_t rect)
{
  CUnit **pp = get_units_in_rect(&rect);
  CUnit **const ppList = pp;
  CUnit *pUnit;

  for (; (pUnit = *pp) != NULL; pp++) {
    if (pUnit->bPlayer == nNation)
      pUnit->SetOrder(nOrderId);
  }
  finish_unit_iteration(ppList);
}


// ---------------------------------------------------------------------------

BOOL tai_collect_pair(void *pArg0, void *pArg1, void *pArg2, intptr_t nArg3)
{
  if (*static_cast<int *>(pArg0) == 1) {
    *static_cast<intptr_t *>(pArg1) = nArg3;
    *static_cast<int *>(pArg0) = 2;
    return 0;
  }
  *static_cast<intptr_t *>(pArg2) = nArg3;
  return 1;
}

// Internal copy of tai_collect_pair, same bytes. Its caller keeps a value in a register
// across the call, which only an internal callee allows; the link folded the
// two copies into one address.
static __declspec(noinline) BOOL tai_collect_pair_local(void *pArg0, void *pArg1, void *pArg2, intptr_t nArg3)
{
  if (*static_cast<int *>(pArg0) == 1) {
    *static_cast<intptr_t *>(pArg1) = nArg3;
    *static_cast<int *>(pArg0) = 2;
    return 0;
  }
  *static_cast<intptr_t *>(pArg2) = nArg3;
  return 1;
}

// --- tai_unit_is_boardable_transport_cb, oracle 0x4AADC0 (48 B) -------------------------------------
BOOL tai_unit_is_boardable_transport_cb(CUnit *pUnit, CUnit *pUnit2)
{
  if (!pUnit->IsATransport() || pUnit->bPlayer != pUnit2->bPlayer ||
      !pUnit2->CanEnterTransport(pUnit))
    return 0;
  return 1;
}

// --- tai_captain_regions_connected_xy, oracle 0x4AB2F0 (50 B) -------------------------------------
int tai_captain_regions_connected_xy(int nArg0, int nArg1, int nArg2, int nArg3, int nArg4)
{
  return tai_captain_regions_connected(nArg0, GetRegion_xy(nArg1, nArg2),
                    GetRegion_xy(nArg3, nArg4));
}

// --- tai_resolve_carrier_target, oracle 0x4B1380 (72 B) ---------------------
// The two null tests must be one `||`. Written as two separate `if`s the bodies
// are identical but MSVC lays the shared `xor eax, eax` first, inline after the
// first test, where the oracle keeps it last; 74 B against 72.
CUnit *tai_resolve_carrier_target(CUnit *pUnit0, CUnit *pUnit1)
{
  if (pUnit1 == NULL || pUnit0 == NULL)
    return NULL;
  if (pUnit1->wType != 0x49)
    return pUnit1;
  if (pUnit1->pUnionC0 == NULL)
    return pUnit1;
  if (pUnit0->IsUnitInWeaponRange(pUnit1->pUnionC0) ||
      (pUnit0->dwFlags & 0x20000) != 0)
    return pUnit1->pUnionC0;
  return pUnit1;
}

// --- tai_captain_decay_need, oracle 0x4AC080 (78 B) -------------------------
// The `const WORD` local is deliberate. The three big comparisons are
// `mov edx, imm32` + `cmp ax, dx` (8 B) where a 32-bit `cmp eax, imm32` (5 B)
// would do, and the fourth is a plain `cmp eax, 2h` -- MSVC avoiding a
// length-changing prefix on a 16-bit immediate, and only doing so when the
// value is a 16-bit lvalue. Spelling `w` `unsigned` compiles 69 B against the
// oracle's 78, exactly 3 B short at each of the three sites.
void tai_captain_decay_need(WORD *pw, int nArg1)
{
  const WORD w = *pw;

  if (nArg1 != 0) {
    if (w > 300) {
      *pw = static_cast<WORD>(w - 300);
      return;
    }
  } else {
    if (w > 1000) {
      *pw = static_cast<WORD>(w - 13);
      return;
    }
    if (w > 200) {
      *pw = static_cast<WORD>(w - 6);
      return;
    }
    if (w > 2) {
      *pw = static_cast<WORD>(w - 2);
      return;
    }
  }
  *pw = 0;
}

// --- tai_captain_request_turret_build, oracle 0x4B1420 (79 B) -------------------------------------
void tai_captain_request_turret_build(intptr_t nArg0, int nArg1)
{
  const unsigned nAreas = NumAreas();
  unsigned i;

  for (i = 1; i < nAreas; i++) {
    if ((sgCaptain[nArg1][i].bFlags & 0x10) != 0) {
      banker_demand_town_request(nArg1, nArg0, 0, 0x7C, 0x50);
      return;
    }
  }
}

// --- tai_soldier_enter_bunker_in_region, oracle 0x4AB630 (147 B) ------------------------------------
BOOL tai_soldier_enter_bunker_in_region(CUnit *pUnit, int nArg1)
{
  CUnit **pp;
  CUnit *pFound;
  CUnit **ppList;
  CUnit *p;

  pp = get_units_in_region(nArg1);
  pFound = NULL;
  ppList = pp;

  for (p = *pp; p != NULL; p = *++pp) {
    if (p->wType != 0x7D)
      continue;
    if (p->bPlayer != pUnit->bPlayer)
      continue;
    if (pUnit->wType == 0x20 &&
        p->IterateTransport(
            reinterpret_cast<CUnit::TransportProc>(tai_unit_is_firebat), NULL))
      continue;
    if (pUnit->CanEnterTransport(p)) {
      pFound = p;
      break;
    }
  }
  finish_unit_iteration(ppList);
  if (pFound != NULL) {
    pUnit->SetOrder_CUnit(0x5C, pFound);
    return 1;
  }
  return 0;
}

// --- tai_captain_survey_weapons, oracle 0x4AAEF0 (119 B) -------------------
void tai_captain_survey_weapons(TAI_Captain *pCaptain, int *pAny, int *pAllGnd, int *pAllAir)
{
  const TSoldier *pSoldier;

  *pAny = 0;
  *pAllGnd = 1;
  *pAllAir = 1;

  for (pSoldier = pCaptain->pSoldiers; pSoldier != NULL;
       pSoldier = pSoldier->pNext) {
    CUnit *const pUnit = pSoldier->pUnit;

    if (pUnit->GetWeaponGndEx() != WEAPON_NONE)
      *pAllGnd = 0;
    if (pUnit->GetWeaponAir() != WEAPON_NONE)
      *pAllAir = 0;
    if ((pUnit->dwFlags & 0x04) == 0)
      *pAny = 1;
  }
}

// --- tai_captain_gather_still_moving, oracle 0x4AB150 (117 B) --------------
// A fourth reading of TAI_Captain::wRegion (+0x00, WORD):
// `mov ecx, dword ptr [edi+10h]` (pSoldier->pCaptain) then
// `cmp ax, word ptr [ecx]` against GetRegion_CUnit's WORD return.
BOOL tai_captain_gather_still_moving(int nArg0)
{
  CUnit *pUnit;

  for (pUnit = CUnit::GetPlayerUnitList(nArg0); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    const TSoldier *pSoldier;
    CUnit *pTarget;
    BYTE bJob;

    if (pUnit->IsDeadOrDying())
      continue;
    pSoldier = reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
    if (pSoldier == NULL)
      continue;
    if (pSoldier->bType != 4)
      continue;
    bJob = pSoldier->pCaptain->bJob;
    if (bJob != 8 && bJob != 9)
      continue;
    pTarget = pUnit->pOrderTarget;
    if (pTarget != NULL && pUnit->IsEnemyUnit(pTarget))
      continue;
    if (pUnit->dwUnion7C != 0)
      continue;
    if (GetRegion_CUnit(pUnit) != pSoldier->pCaptain->wRegion)
      return 1;
  }
  return 0;
}

// --- tai_captain_take_patroller, oracle 0x4ADF80 (157 B) -------------------
// The parameter is a `CUnit *` (`movzx ecx, byte ptr [edi+4Ch]` twice, bPlayer)
// and TAI_Captain +0x28 is a `CUnit *` (`cmp dword ptr [ecx+28h], 0h` /
// `mov dword ptr [ecx+28h], edi`), so the body reads pUnit28 directly.
//
// The two-level switch is read off the oracle image, not guessed:
//   0x4AE020  dd 0x4ADFDE, 0x4ADFE4      (+0x5E = the +0x28 test, +0x64 = next)
//   0x4AE028  db 0,0,1,1,1,1,0,0,0       (indexed by bJob - 1, range [1..9])
// so bJob 1, 2, 7, 8 and 9 take the body and 3, 4, 5 and 6 join the default.
int tai_captain_take_patroller(CUnit *pUnit)
{
  unsigned nAreas;
  unsigned i;

  if (!banker_is_attacking(pUnit->bPlayer))
    return 0;

  nAreas = NumAreas();
  for (i = 1; i < nAreas; i++) {
    TAI_Captain *const pCaptain = &sgCaptain[pUnit->bPlayer][i];

    if (pCaptain->pSoldiers == NULL)
      continue;
    switch (pCaptain->bJob) {
    case 1:
    case 2:
    case 7:
    case 8:
    case 9:
      if (pCaptain->pUnit28 == NULL) {
        int x;
        int y;

        pCaptain->pUnit28 = pUnit;
        CaptainSlotsMark(pCaptain);  // Hyperion
        GetRegionCenter(i, &x, &y);
        pUnit->SetDestination_xy(x, y);
        return 1;
      }
      break;
    }
  }
  return 0;
}

// --- tai_captain_pick_leader, oracle 0x4AB870 (139 B) ----------------------
// The argument is a `TAI_Captain *`, not a `CUnit *`: `movzx ecx, byte ptr
// [eax+4h]` handed to aiscript_has_campaign is bNation, and
// `mov eax, dword ptr [ecx+30h]` is walked as a TSoldier list ([eax], [eax+0Ch]).
// +0x24 is a `CUnit *` on the same evidence.
//
// A local copy of `pUnit24` costs 4 bytes, and that is the whole delta. Four
// values are live across the loop (two candidate pointers, two speeds) and only
// three callee-saved registers exist, so exactly one spills. Introducing
// `CUnit *p = pCaptain->pUnit24;` adds a fifth live value in the prologue,
// which makes MSVC take EBX before the aiscript_has_campaign test, so the early
// return has to `pop ebx`, and spill the pointer pBest2 instead of the int
// nBest1 -- a spilled int is a free `cmp ecx, dword ptr [ebp-4h]` memory
// operand where a spilled pointer needs its own reload. 143 B against 139.
void tai_captain_pick_leader(TAI_Captain *pCaptain)
{
  const TSoldier *pSoldier;
  CUnit *pBest1;
  CUnit *pBest2;
  int nBest1;
  int nBest2;

  if (!aiscript_has_campaign(pCaptain->bNation))
    return;

  pBest2 = NULL;
  nBest2 = 99999999;
  pBest1 = NULL;
  nBest1 = 99999999;
  if (pCaptain->pUnit24 != NULL) {
    if ((pCaptain->pUnit24->dwFlags & 0x04) != 0) {
      pBest1 = pCaptain->pUnit24;
      nBest1 = pBest1->lTopSpeed;
    } else {
      pBest2 = pCaptain->pUnit24;
      nBest2 = pBest2->lTopSpeed;
    }
  }

  for (pSoldier = pCaptain->pSoldiers; pSoldier != NULL;
       pSoldier = pSoldier->pNext) {
    CUnit *const pU = pSoldier->pUnit;
    const int nSpeed = pU->lTopSpeed;

    if ((pU->dwFlags & 0x04) != 0) {
      if (nSpeed < nBest1) {
        nBest1 = nSpeed;
        pBest1 = pU;
      }
    } else {
      if (nSpeed < nBest2) {
        nBest2 = nSpeed;
        pBest2 = pU;
      }
    }
  }

  pCaptain->pUnit24 = pBest2 != NULL ? pBest2 : pBest1;
  CaptainSlotsMark(pCaptain);  // Hyperion
}

// --- tai_captain_attack_map_spread_front, oracle 0x4AAB20 (111 B) ----------
// No _chkstk: `sub esp, 9C8h` is emitted directly, 2504 B being under one page.
// The parameter is a `BYTE *` over 0x9C4 area states (`mov ecx, 271h` /
// `rep movsd` == 2500 B, and the loop bound is `cmp esi, 9C4h`), and
// GetRegionNeighbors's out-param is a `WORD **`, so both casts are gone.
void tai_captain_attack_map_spread_front(BYTE *pStates)
{
  BYTE aCopy[AI_AREAS_117];
  WORD *pCursor;
  unsigned i;
  int n;

  memcpy(aCopy, pStates, sizeof(aCopy));

  for (i = 0; i < AI_AREAS_117; i++) {
    if (aCopy[i] != 2)
      continue;
    pStates[i] = 3;
    n = GetRegionNeighbors(i, &pCursor);
    while (n != 0) {
      n--;
      if (pStates[*pCursor] == 0)
        pStates[*pCursor] = 4;
      pCursor++;
    }
  }
}

// tai_captain_attack_map_spread_own, oracle 0x4AAB90 (137 B). Copies the area
// states, then turns every region listed for an area at 5 or more from 1 into 2
// and from 0 into 6. Returns TRUE if it changed any. ECX is ignored. The states
// are written through a pointer to the entry, as 1.17 reloads its cursor after
// the store.
int tai_captain_attack_map_spread_own(int nNation, BYTE *pStates)
{
  BYTE aCopy[AI_AREAS_117];
  WORD *pCursor;
  unsigned i;
  int n;
  int bResult;

  memcpy(aCopy, pStates, sizeof(aCopy));
  bResult = 0;

  for (i = 0; i < AI_AREAS_117; i++) {
    if (aCopy[i] < 5)
      continue;
    n = GetRegionNeighbors(i, &pCursor);
    while (n != 0) {
      BYTE *pb = &pStates[*pCursor];
      BYTE b;

      n--;
      pCursor++;
      b = *pb;
      if (b == 1)
        *pb = 2;
      else if (b == 0)
        *pb = 6;
      else
        continue;
      bResult = 1;
    }
  }
  return bResult;
}

// --- tai_captain_unlink_soldier, oracle 0x4A9A30 (94 B) --------------------
// The soldier unlink: drop pSoldier out of its captain's list and push it onto
// the pool free list, through TSoldier::pPrev (+0x04) and TAI_Captain::pPool
// (+0x2C).
void tai_captain_unlink_soldier(TAI_Captain *pCaptain, TSoldier *pSoldier)
{
  TSoldierPool *pPool;

  if ((pSoldier->pUnit->dwFlags & 0x04) == 0)
    pCaptain->wUnknown0A += 0xFFFF;

  if (pCaptain->pSoldiers == pSoldier)
    pCaptain->pSoldiers = pSoldier->pNext;

  if (pSoldier->pPrev != NULL)
    pSoldier->pPrev->pNext = pSoldier->pNext;
  if (pSoldier->pNext != NULL)
    pSoldier->pNext->pPrev = pSoldier->pPrev;

  pPool = pCaptain->pPool;
  pSoldier->pPrev = NULL;
  pSoldier->pNext = pPool->pFreeHead;
  if (pPool->pFreeHead != NULL)
    pPool->pFreeHead->pPrev = pSoldier;
  pPool->pFreeHead = pSoldier;
}

// The rest of the captain jobs, the aiscript handlers and the per-unit AI
// dispatch helpers.

// tai_captain_retarget_soldiers, oracle 0x4ADCF0 (225 B)
// Refreshes the captain's two threat slots, then re-aims every soldier that is
// not already engaged with one of them. The guard after the two calls is an and
// of two null tests: the `jnz` off the ground result skips the air compare, so
// reading the mnemonics alone gives the negation.
void tai_captain_retarget_soldiers(TAI_Captain *pCaptain)
{
  TSoldier *pSoldier;

  pCaptain->pUnit1C =
      tai_get_greatest_air_threat_region(pCaptain->bNation, pCaptain->wRegion);
  pCaptain->pUnit20 =
      tai_get_greatest_ground_threat_region(pCaptain->bNation, pCaptain->wRegion);
  CaptainSlotsMark(pCaptain);  // Hyperion
  if (pCaptain->pUnit20 == NULL && pCaptain->pUnit1C == NULL)
    return;

  for (pSoldier = pCaptain->pSoldiers; pSoldier != NULL;
       pSoldier = pSoldier->pNext) {
    CUnit *const pUnit = pSoldier->pUnit;
    const TSoldier *const pRec =
        reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);

    if (pRec == NULL || pRec->bType != 4 || pRec->pUnit != pUnit)
      continue;
    if ((gulUnitTypeFlags[pUnit->wType] & 0x01) != 0)
      continue;
    if ((pUnit->dwFlags & 0x3000) != 0)
      continue;
    if (pUnit->pOrderTarget == pCaptain->pUnit20 ||
        pUnit->pOrderTarget == pCaptain->pUnit1C)
      continue;
    if (pUnit->bOrderId == 0x0E || pUnit->bOrderId == 0x9D ||
        pUnit->bOrderId == 0x9E)
      continue;
    if (pUnit->IsATransport())
      continue;
    if ((pCaptain->pUnit20 != NULL && pUnit->CanTarget(pCaptain->pUnit20, 1)) ||
        (pCaptain->pUnit1C != NULL && pUnit->CanTarget(pCaptain->pUnit1C, 1)))
      tai_soldier_issue_order(pUnit, 0x9C);
  }
}

// tai_captain_job_under_attack, oracle 0x4AC690 (167 B)
// The bJob == 6 arm of tai_captain_service. Three tail `jmp`s, so the two set-type calls
// must stay ahead of the region_has_worker_or_building tail in the source or
// the blocks swap.
void tai_captain_job_under_attack(TAI_Captain *pCaptain)
{
  tai_captain_recount_strength(pCaptain);
  tai_captain_retarget_soldiers(pCaptain);
  if (pCaptain->bTimer != 0)
    return;

  if (!aiscript_has_campaign(pCaptain->bNation)) {
    if (IsInaccessibleRegion(pCaptain->wRegion)) {
      if (pCaptain->wUnknown0C < 1000)
        pCaptain->wUnknown0C = 1000;
    } else if (pCaptain->wUnknown0E < 1000) {
      pCaptain->wUnknown0E = 1000;
    }
  }

  if (pCaptain->pUnit1C == NULL && pCaptain->pUnit20 == NULL) {
    if (aiscript_has_campaign(pCaptain->bNation) &&
        !region_has_worker_or_building(pCaptain->bNation, pCaptain->wRegion))
      sai_soldier_set_type(pCaptain, 0);
    else
      sai_soldier_set_type(pCaptain, 5);
    return;
  }

  if (!region_has_worker_or_building(pCaptain->bNation, pCaptain->wRegion))
    tai_captain_disband_if_weak(pCaptain);
}

// tai_captain_job_attack, oracle 0x4AC7C0 (163 B)
// The bJob 1 and 2 arm of tai_captain_service. tai_captain_cannot_fight_region takes the captain twice, which
// is what both call sites pass.
void tai_captain_job_attack(TAI_Captain *pCaptain)
{
  CUnit *pUnit;

  tai_captain_retarget_soldiers(pCaptain);
  if (pCaptain->pSoldiers == NULL) {
    sai_soldier_set_type(pCaptain, 0);
    return;
  }
  if (pCaptain->bJob != 2 && pCaptain->wUnknown0A >= 0x14) {
    sai_soldier_set_type(pCaptain, 2);
    return;
  }

  if (pCaptain->pUnit24 == NULL)
    tai_captain_pick_leader(pCaptain);
  if ((pCaptain->pUnit1C != NULL || pCaptain->pUnit20 != NULL) &&
      !tai_captain_cannot_fight_region(pCaptain, pCaptain))
    return;

  pUnit = tai_captain_pick_attack_target(pCaptain);
  if (pUnit != NULL) {
    TAI_Captain *const pOther =
        &sgCaptain[pCaptain->bNation][GetRegion_CUnit(pUnit)];

    sai_soldier_set_type(pOther, pCaptain->bJob);
    tai_captain_transfer_soldiers(pCaptain, pOther, 0, 0, 1);
  }
  sai_soldier_set_type(pCaptain, 0);
}

// tai_captain_job_reinforce, oracle 0x4ACBC0 (213 B)
// The bJob == 7 arm of tai_captain_service. wUnknown02 is the other captain's area index,
// so the second record is &sgCaptain[nation][pCaptain->wUnknown02].
void tai_captain_job_reinforce(TAI_Captain *pCaptain)
{
  TAI_Captain *pOther;

  tai_captain_recount_strength(pCaptain);
  tai_captain_retarget_soldiers(pCaptain);
  if ((pCaptain->bFlags & 0x20) != 0 && tai_captain_disband_if_weak(pCaptain))
    return;

  pOther = &sgCaptain[pCaptain->bNation][pCaptain->wUnknown02];
  if (pCaptain->bUnknown06 != 0x32 && pCaptain->pUnit1C == NULL &&
      pCaptain->pUnit20 == NULL && pOther->pUnit1C == NULL &&
      pOther->pUnit20 == NULL)
    pCaptain->bUnknown06 = 0x32;

  if ((pCaptain->pUnit1C != NULL && !pCaptain->pUnit1C->IsCritter()) ||
      (pCaptain->pUnit20 != NULL && !pCaptain->pUnit20->IsCritter())) {
    sai_soldier_set_type(pCaptain, 6);
    return;
  }

  if (pCaptain->bUnknown09 < 0x0A) {
    pCaptain->bUnknown09++;
    if (pCaptain->wAirStr12 < pOther->wUnknown0E ||
        pCaptain->wGroundStr10 < pOther->wUnknown0C)
      return;
  }
  sai_soldier_set_type(pOther, 6);
  tai_captain_transfer_soldiers(pCaptain, pOther, 1, 1, 1);
  sai_soldier_set_type(pCaptain, 5);
  pOther->bTimer = 5;
}

// tai_captain_disband_if_weak, oracle 0x4AA9B0 (240 B).
// Disbands a captain whose strength no longer meets its two thresholds: every
// soldier is unlinked and handed to the captain of the region it is standing
// in. The loop's exit is the CAPTAIN's list going empty, tested at the bottom,
// while the top test is on the cursor -- two different conditions. The region
// goes through an int local, which puts it in ECX ahead of the nation load.
BOOL tai_captain_disband_if_weak(TAI_Captain *pCaptain)
{
  TSoldier *pSoldier;
  int nType;

  if (pCaptain->wAirStr12 >= pCaptain->wUnknown0E &&
      pCaptain->wGroundStr10 >= pCaptain->wUnknown0C)
    return 0;

  if (!aiscript_has_campaign(pCaptain->bNation) && pCaptain->pSoldiers != NULL) {
    const WORD wType = pCaptain->pSoldiers->pUnit->wType;

    if (wType == 0x20 || wType == 0x25)
      return 0;
  }

  nType = 3;
  if ((aiscript_has_campaign(pCaptain->bNation) &&
       tai_captain_region_or_neighbor_held(pCaptain->bNation, pCaptain->wRegion)) ||
      pCaptain->bJob == 8 || pCaptain->bJob == 9 || pCaptain->bJob == 1 ||
      pCaptain->bJob == 2)
    nType = 0;
  sai_soldier_set_type(pCaptain, nType);

  pSoldier = pCaptain->pSoldiers;
  while (pCaptain->pSoldiers != NULL) {
    TSoldier *pNext;
    CUnit *pUnit;
    TAI_Captain *pOther;

    if (pSoldier == NULL)
      break;
    pNext = pSoldier->pNext;
    pUnit = pSoldier->pUnit;
    int nRgn = GetRegion_CUnit(pUnit);
    pOther = &sgCaptain[pCaptain->bNation][nRgn];

    tai_captain_unlink_soldier(pCaptain, pSoldier);
    pUnit->dwUnion134 = 0;
    tai_soldier_add_to_captain(pOther, pUnit, 1);
    pSoldier = pNext;
  }
  return 1;
}

// tai_captain_mark_enemy_regions, oracle 0x4AB6D0 (132 B)
// Marks every region holding an enemy unit that can shoot. The player bit is
// computed before the list walk because the oracle has it in EDI across the
// whole loop.
void tai_captain_mark_enemy_regions(int nPlayer, BYTE *pStates)
{
  CUnit *pUnit;

  for (pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
    if (pUnit->IsDeadOrDying())
      continue;
    if (!pUnit->CanAttack() && (gulUnitTypeFlags[pUnit->wType] & 0x01) == 0)
      continue;
    if ((pUnit->dwFlags & 0x300) != 0 && !UnitDetectedBy(pUnit, nPlayer))  // Hyperion: every slot (stage D)
      continue;
    if (!IsEnemyUnit(nPlayer, pUnit))
      continue;
    pStates[GetRegion_CUnit(pUnit)] = 1;
  }
}

// tai_captain_mark_own_regions, oracle 0x4AB760 (165 B)
// Marks the player's own regions, preferring the units that pass both flag
// tests and falling back to a looser sweep when the first pass marked nothing.
// The two tests in the first loop are a single DWORD load read twice as a byte.
void tai_captain_mark_own_regions(int nPlayer, BYTE *pStates)
{
  CUnit *pUnit;
  BOOL bAny = 0;

  for (pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (!pUnit->IsDeadOrDying()) {
      const DWORD dwFlags = gulUnitTypeFlags[pUnit->wType];

      if ((dwFlags & 0x10) == 0 && (dwFlags & 0x01) != 0) {
        pStates[GetRegion_CUnit(pUnit)] = 5;
        bAny = 1;
      }
    }
  }
  if (bAny)
    return;

  for (pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (!pUnit->IsDeadOrDying() &&
        (gulUnitTypeFlags[pUnit->wType] & 0x10) == 0)
      pStates[GetRegion_CUnit(pUnit)] = 5;
  }
}

// TAI_Captain_aiscript_build_turrets, oracle 0x4ADED0 (175 B)
// Flags up to six flagged areas that hold no missile turret of this nation.
// The counter is zeroed ahead of the NumAreas() call, which is the order the
// oracle stores it in.
void TAI_Captain_aiscript_build_turrets(int nNation)
{
  int nMarked = 0;
  const unsigned nAreas = NumAreas();
  unsigned i;

  for (i = 1; i < nAreas; i++) {
    CUnit **ppList;
    CUnit **pp;
    CUnit *pUnit;
    BOOL bFound;

    if ((sgCaptain[nNation][i].bFlags & 0x01) == 0)
      continue;

    pp = get_units_in_region(i);
    ppList = pp;
    bFound = 0;
    for (; (pUnit = *pp) != NULL; pp++) {
      if (pUnit->wType == 0x7C && pUnit->bPlayer == nNation) {
        bFound = 1;
        break;
      }
    }
    finish_unit_iteration(ppList);
    if (bFound)
      continue;

    sgCaptain[nNation][i].bFlags |= 0x10;
    if (++nMarked == 6)
      break;
  }
}

// TAI_Captain_aiscript_build_bunkers, oracle 0x4ADE10 (192 B)
// The turret sweep's twin: three areas rather than six, bunkers rather than
// turrets, and one extra guard on region_has_worker_or_building.
void TAI_Captain_aiscript_build_bunkers(int nNation)
{
  int nMarked = 0;
  const unsigned nAreas = NumAreas();
  unsigned i;

  for (i = 1; i < nAreas; i++) {
    CUnit **ppList;
    CUnit **pp;
    CUnit *pUnit;
    BOOL bFound;

    if ((sgCaptain[nNation][i].bFlags & 0x01) == 0)
      continue;
    if (!region_has_worker_or_building(nNation, i))
      continue;

    pp = get_units_in_region(i);
    ppList = pp;
    bFound = 0;
    for (; (pUnit = *pp) != NULL; pp++) {
      if (pUnit->wType == 0x7D && pUnit->bPlayer == nNation) {
        bFound = 1;
        break;
      }
    }
    finish_unit_iteration(ppList);
    if (bFound)
      continue;

    sgCaptain[nNation][i].bFlags |= 0x02;
    if (++nMarked == 3)
      break;
  }
}

// tai_soldier_try_stim, oracle 0x4B1100 (171 B)
// Stims a marine or firebat that is healthy enough and whose target is worth
// it. The half-health term recomputes (hp + 255) >> 8 rather than reusing the
// value the default arm just built, which is the oracle's own shape.
void tai_soldier_try_stim(CUnit *pUnit)
{
  CUnit *pTarget;
  int nFull;

  if (pUnit->wType != 0 && pUnit->wType != 0x20)
    return;
  pTarget = pUnit->pOrderTarget;
  if (pTarget == NULL)
    return;
  if (pUnit->GetMovementSpeedModifier() > 0)
    return;

  nFull = HandicapLife(gxUnitLife[pUnit->wType], pUnit->bPlayer) >> 8;  // Hyperion: at the owner's handicap
  if (nFull == 0) {
    const int nCur = (pUnit->lHitPoints + 0xFF) >> 8;

    nFull = nCur != 0 ? nCur : 1;
  }
  if (((pUnit->lHitPoints + 0xFF) >> 8) < nFull / 2)
    return;

  if (!pTarget->CanTarget(pUnit, 1))
    return;
  if (!pUnit->IsUnitInWeaponRange(NULL))
    return;
  if (TechUseAllowed(0, pUnit->bPlayer, pUnit) != 1)
    return;
  if (pTarget->GetToughness() < 100)
    return;
  pUnit->UnitApplyStim();
}

// The at-move-target test tai_soldier_step_to_captain_region inlines. CUnit::AtMoveTarget has the same
// body at 0x41A250 but is a real call from five other TUs, so this copy is
// TU-local; its three-valued return is what keeps the caller's dead `inc / jz`.
// Source the flag arm as a ternary, not as `1 + (flag != 0)`.
static int soldier_at_move_target(CUnit *pUnit)
{
  CSprite *pSprite = pUnit->pSprite;

  if (pUnit->wMoveTargetX == pSprite->wPosX &&
      pUnit->wMoveTargetY == PosU(pSprite->wPosY))
    return (pUnit->dwFlags & 0x80000) ? 2 : 1;

  return 0;
}

void tai_soldier_step_to_captain_region(CUnit *pUnit)
{
  const TSoldier *const pSoldier =
      reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
  const TAI_Captain *pCaptain;
  WORD wRegion;
  int x, y;

  if (pSoldier == NULL || pSoldier->bType != 4 || pSoldier->pUnit != pUnit)
    return;
  if ((gulUnitTypeFlags[pUnit->wType] & 0x01) != 0)
    return;

  pCaptain = pSoldier->pCaptain;
  if (!soldier_at_move_target(pUnit))
    return;

  wRegion = pCaptain->wRegion;
  if (GetRegion_CUnit(pUnit) == wRegion)
    return;
  GetRegionCenter(wRegion, &x, &y);
  pUnit->SetOrder_xy(pUnit->wType == 0x22 ? 0xB1 : 0x9E, x, y);
}

// patroller_follow_captain, oracle 0x4B0AF0 (198 B)
// Walks the unit's own nation for the captain holding it in +0x28 and either
// steers it to that area's centre or releases it. The nine-arm table sends
// jobs 1, 2, 7, 8 and 9 to the steer block and 3..6 to the release block,
// which is also the default, so the steer cases must come first in source.
BOOL patroller_follow_captain(CUnit *pUnit)
{
  const unsigned nAreas = NumAreas();
  unsigned i;

  for (i = 1; i < nAreas; i++) {
    TAI_Captain *const pCaptain = &sgCaptain[pUnit->bPlayer][i];

    if (pCaptain->pUnit28 != pUnit)
      continue;

    switch (pCaptain->bJob) {
      case 1:
      case 2:
      case 7:
      case 8:
      case 9: {
        int x, y;

        GetRegionCenter(static_cast<WORD>(i), &x, &y);
        if (reinterpret_cast<CTarget *>(pUnit)->XYInRadius(0x160, x, y))
          pUnit->StopUnitMovement();
        else
          pUnit->SetDestination_xy(x, y);
        return 1;
      }
    }

    pCaptain->pUnit28 = NULL;
    pUnit->SetDestination_xy(PosS(pUnit->pSprite->wPosX),
                             pUnit->pSprite->wPosY);
    return 0;
  }
  return 0;
}

/// tai_captain_unit_is_recruitable, oracle 0x4AAF70 (200 B).
// The harass-candidate filter. Type 0 answers on the order id alone, five
// listed types answer yes outright, and everything else compares current
// toughness against half the maximum. The first two tests share one exit, so
// the type is read after the call without ESI being shrink-wrapped; the type-0
// test inside the `||` chain keeps its order test a branch, not a setne.
int tai_captain_unit_is_recruitable(CUnit *pUnit, int nUnitType, int nArg2)
{
  int nType;

  if (pUnit->IsDeadOrDying() ||
      (gulUnitTypeFlags[nType = pUnit->wType] & 0x10) != 0)
    return 0;
  if (pUnit->dwUnion134 == 0)
    return 0;
  if (nType != nUnitType && (nUnitType != 5 || nType != 0x1E))
    return 0;
  if ((pUnit->dwFlags & 0x01) == 0)
    return 0;
  int nOrder = pUnit->bOrderId;
  if (nOrder == 0x6A || nOrder == 0x96)
    return 0;
  if (pUnit->IsAttackable(NULL))
    return 0;
  if (nArg2 != 0 && (pUnit->dwFlags & 0x20) != 0)
    return 0;

  if (nType == 0 && pUnit->bOrderId == 0x5C)
    return 0;
  if (nType == 0 || nType == 0x20 || nType == 0x25 || nType == 0x26 ||
      nType == 0x41 || nType == 0x42)
    return 1;
  int nHalf = pUnit->MaxToughness() / 2;
  return pUnit->GetToughness() >= nHalf;
}

// tai_captain_release_all_soldiers, oracle 0x4B1500 (201 B)
// Empties a captain's soldier list back into the pool and re-enlists every unit
// with the captain of the region it stands in. The unlink is
// tai_captain_unlink_soldier's body spelled out: the oracle has it inline here
// while its other four call sites are real calls, so the declaration carries
// `noinline`.
void tai_captain_release_all_soldiers(TAI_Captain *pCaptain)
{
  TSoldier *pSoldier = pCaptain->pSoldiers;

  while (pCaptain->pSoldiers != NULL) {
    TSoldierPool *pPool;
    TSoldier *pNext;
    CUnit *pUnit;

    if (pSoldier == NULL)
      break;
    pUnit = pSoldier->pUnit;
    pNext = pSoldier->pNext;

    if ((pUnit->dwFlags & 0x04) == 0)
      pCaptain->wUnknown0A += 0xFFFF;
    if (pCaptain->pSoldiers == pSoldier)
      pCaptain->pSoldiers = pSoldier->pNext;
    if (pSoldier->pPrev != NULL)
      pSoldier->pPrev->pNext = pSoldier->pNext;
    if (pSoldier->pNext != NULL)
      pSoldier->pNext->pPrev = pSoldier->pPrev;
    pPool = pCaptain->pPool;
    pSoldier->pPrev = NULL;
    pSoldier->pNext = pPool->pFreeHead;
    if (pPool->pFreeHead != NULL)
      pPool->pFreeHead->pPrev = pSoldier;
    pPool->pFreeHead = pSoldier;

    pUnit->dwUnion134 = 0;
    {
      const BYTE bNation = pUnit->bPlayer;

      tai_soldier_add_to_captain(&sgCaptain[bNation][GetRegion_CUnit(pUnit)], pUnit, 1);
    }
    pSoldier = pNext;
  }
}

// tai_captain_find_spare_unit_near, oracle 0x4AF750 (186 B)
// The nearest unit of a given type belonging to a captain with spare ground
// strength. The distance compare is unsigned, which is what fixes the
// accumulator's type.
CUnit *tai_captain_find_spare_unit_near(int nPlayer, int nUnitType, int nX, int nY)
{
  CUnit *pBest = NULL;
  unsigned nBest = 9999999;
  CUnit *pUnit;

  for (pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    const TSoldier *pSoldier;
    const TAI_Captain *pCaptain;
    unsigned nDist;

    if (pUnit->IsDeadOrDying())
      continue;
    if (pUnit->wType != nUnitType)
      continue;
    pSoldier = reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
    if (pSoldier == NULL || pSoldier->bType != 4)
      continue;

    pCaptain = pSoldier->pCaptain;
    if (pCaptain->bJob == 1 || pCaptain->bJob == 8 || pCaptain->bJob == 9 ||
        pCaptain->bJob == 7)
      continue;
    if (pCaptain->wGroundStr14 - pUnit->GroundStrength() <=
        pCaptain->wUnknown0C)
      continue;

    {
      const upos_t wY = pUnit->pSprite->wPosY;
      const upos_t wX = pUnit->pSprite->wPosX;

      nDist = veclen(nX, PosS(wX), nY, PosS(wY));
    }
    if (nDist < nBest) {
      pBest = pUnit;
      nBest = nDist;
    }
  }
  return pBest;
}

// tai_captain_request_missing_units, oracle 0x4AB040 (161 B)
// Files a build demand for every unit type tai_captain_list_missing_harass_types collected that the
// captain does not already have. The 0xE4 bound is units.dat's row count, which
// is what says the buffer holds unit types rather than regions.
void tai_captain_request_missing_units(TAI_Captain *pCaptain)
{
  int anTypes[63];
  unsigned nCount;
  unsigned i;
  int nArg;

  nCount = tai_captain_list_missing_harass_types(pCaptain, anTypes, !aiscript_has_campaign(pCaptain->bNation));
  if (!aiscript_has_campaign(pCaptain->bNation) && banker_request_throttle_passed(pCaptain->bNation))
    nArg = 0xA0;
  else
    nArg = 0x3C;

  for (i = 0; i < nCount; i++) {
    const int nType = anTypes[i];

    if (static_cast<unsigned>(nType) >= 0xE4)
      continue;
    if (!tai_captain_enlist_type(pCaptain, nType, nArg))
      banker_demand_unit_for_captain(pCaptain->bNation, reinterpret_cast<intptr_t>(pCaptain), nType,
                 0x3C, 1, 1);
  }
}

// tai_captain_start_expansion, oracle 0x4AA8F0 (177 B)
// Opens an expansion captain on the area sai_find_attack_route picks. The push of
// sai_find_attack_route's out-pointer sits above `call GetRegion_xy` because the two are
// one nested expression evaluated right to left.
void tai_captain_start_expansion(int nNation)
{
  int nTown;
  int nArea;
  int nOther;

  if (!banker_may_attack_expansion(nNation))
    return;
  if (!tai_captain_harass_list_filled(nNation))
    return;
  if (tai_captain_any_attack_job_active(nNation))
    return;

  nTown = sai_nearest_town_plan(nNation, 0, 0);
  {
    const int nX = sai_town_x(nTown);
    const int nY = sai_town_y(nTown);

    nArea = 0x9C4;
    nOther = sai_find_attack_route(nNation, GetRegion_xy(nX, nY), &nArea);
  }
  if (nOther == 0x9C4 || nArea == 0x9C4)
    return;

  banker_set_attack_target(nNation, nArea, 1);
  {
    TAI_Captain *const pCaptain = &sgCaptain[nNation][nArea];

    sai_soldier_set_type(pCaptain, 8);
    pCaptain->wUnknown02 = static_cast<WORD>(nOther);
    banker_attack_done(nNation);
  }
}

// tai_captain_region_enemy_strength, oracle 0x4AEAF0 (198 B)
// Sums the enemy strength in a 0x800-pixel box around a region centre. The
// stack parameter is tested for zero on entry and then reused as
// GetRegionCenter's Y out-slot, which is the oracle's own aliasing of it.
int tai_captain_region_enemy_strength(int nPlayer, int nRegion, int nY)
{
  CUnit **ppList;
  CUnit **pp;
  CUnit *pUnit;
  mrect_t  rc;
  int nTotal;
  int nX;

  if (nY == 0) {
    const TAI_Captain *const pCaptain = &sgCaptain[nPlayer][nRegion];

    return pCaptain->wUnknown1A + pCaptain->wUnknown18;
  }

  GetRegionCenter(static_cast<WORD>(nRegion), &nX, &nY);
  nTotal = 0;
  rc.top = PosS(nY - 0x400);
  rc.bottom = PosS(nY + 0x400);
  rc.left = PosS(nX - 0x400);
  rc.right = PosS(nX + 0x400);

  pp = get_units_in_rect(&rc);
  ppList = pp;
  for (; (pUnit = *pp) != NULL; pp++) {
    if (IsEnemyUnit(nPlayer, pUnit)) {
      unsigned nBest = *reinterpret_cast<WORD *>(&pUnit->_unmapped_138[0x00]);
      const WORD wGnd = *reinterpret_cast<WORD *>(&pUnit->_unmapped_138[0x02]);

      if (static_cast<WORD>(nBest) <= wGnd)
        nBest = wGnd;
      nTotal += nBest;
    }
  }
  finish_unit_iteration(ppList);
  return nTotal;
}

// aiscript_make_patrol, oracle 0x4B0F10 (206 B)
// Orders every flagged unit of the nation inside the script area to patrol to
// the centre of the banker's command rectangle. banker_get_gencmd returns its 16-byte
// AI_GenCmd through a pushed buffer, so the movdqu reads through EAX.
void aiscript_make_patrol(int nNation, rect32_t *pArea)
{
  AI_GenCmd cmd;
  rect16_t rc;
  CUnit **ppList;
  CUnit **pp;
  CUnit *pUnit;
  int nX, nY;

  rc.top = static_cast<short>(pArea->top);
  rc.bottom = static_cast<short>(pArea->bottom);
  rc.left = static_cast<short>(pArea->left);
  rc.right = static_cast<short>(pArea->right);

  cmd = banker_get_gencmd(nNation);
  nX = static_cast<int>(cmd.dw[0])
       + (static_cast<int>(cmd.dw[2]) - static_cast<int>(cmd.dw[0])) / 2;
  nY = static_cast<int>(cmd.dw[1])
       + (static_cast<int>(cmd.dw[3]) - static_cast<int>(cmd.dw[1])) / 2;

  pp = get_units_in_rect(&rc);
  ppList = pp;
  for (; (pUnit = *pp) != NULL; pp++) {
    if (pUnit->bPlayer == nNation && (pUnit->dwFlags & 0x20000) != 0)
      pUnit->SetOrder_xy(0x98, nX, nY);
  }
  finish_unit_iteration(ppList);
}

// Internal copy of TAI_GuardPost's guard_unit_is_soldier, same bytes. Its caller keeps a
// value in a register across the call, which only an internal callee allows;
// the link folded the two copies into one address.
static __declspec(noinline) BOOL guard_unit_is_soldier_local(CUnit *pUnit)
{
  const TSoldier *pSoldier =
      reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);

  if (pSoldier == NULL || pSoldier->bType != 4 || pSoldier->pUnit != pUnit ||
      (gulUnitTypeFlags[pUnit->wType] & 0x01) != 0)
    return 0;
  return 1;
}

// Internal copy of CUnit::IsPureSpellcaster, same bytes. Its caller keeps a value in a register
// across the call, which only an internal callee allows; the link folded the
// two copies into one address.
static __declspec(noinline) BOOL IsPureSpellcaster_local(CUnit *pUnit)
{
  switch (pUnit->wType) {
  case 9:
  case 45:
  case 46:
  case 67:
    return 1;
  default:
    return 0;
  }
}

// Sends a captain's soldier toward (nX, nY), or a random point on the far side
// of its region's centre, unless it is busy, burrowed or a pure spellcaster.
void tai_soldier_move_toward(CUnit *pUnit, int nOrder, int nX, int nY)
{
  const TSoldier *pSoldier = reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);

  if (pSoldier == NULL || pSoldier->bType != 4 || pSoldier->pUnit != pUnit ||
      (gulUnitTypeFlags[pUnit->wType] & 1) || !(pUnit->dwFlags & 1))
    return;
  const int nCurOrder = pUnit->bOrderId;
  if (nCurOrder == 0x6A || nCurOrder == 0x96)
    return;
  if (pUnit->dwFlags & 0x20)
    pUnit->ExitTransport();
  else if (pUnit->bOrderId == 0x62 || pUnit->bOrderId == 6)
    return;

  BOOL bInterrupt = FALSE;
  if (pUnit->wType == 0x1E) {
    if (pUnit->bOrderId == 0x63)
      return;
    if (pUnit->HasEnemyInWeaponRange()) {
      if (!pUnit->AIPickAttackTarget(0, 1, 0)) {
        CUnit *pTarget = pUnit->FindAttackTarget();
        if (pTarget != NULL) {
          pUnit->ClearTarget();
          CUnit::NewTarget(pUnit, pTarget, 0, 0);
        }
      }
      return;
    }
  }
  if (pUnit->wType == 0x1E) {
    pUnit->SetOrder(0x63);
    bInterrupt = TRUE;
  }
  if (!guard_unit_is_soldier_local(pUnit))
    return;
  TAI_Captain *pCaptain = reinterpret_cast<const TSoldier *>(pUnit->dwUnion134)->pCaptain;
  if (!IsPureSpellcaster_local(pUnit) && (pCaptain->bJob == 1 || tai_captain_job_is_gather(pCaptain)))
    nOrder = pUnit->wType == 0x22 ? 0xB1 : 0x9E;
  if ((pUnit->dwFlags & 0x40) && !(pUnit->dwFlags & 0x20)) {
    tai_soldier_issue_order(pUnit, 0x9C);
    return;
  }

  const BYTE bPlayer = pUnit->bPlayer;
  if (tai_captain_regions_connected(bPlayer, GetRegion_CUnit(pUnit), GetRegion_xy(nX, nY))) {
    int nCX, nCY;
    GetRegionCenter(GetRegion_CUnit(pUnit), &nCX, &nCY);
    unsigned nAngle = AngleBetween(nCX, nCY, nX, nY);
    if (net_rand(0x38) & 8)
      nAngle += 0x40;
    else
      nAngle += 0xC0;
    if (nAngle >= 0x100)
      nAngle -= 0x100;

    int nDX, nDY;
    reinterpret_cast<CFlingy *>(pUnit)->GetVectorComponents(nAngle, net_rand(0x38) & 0x7F, &nDX, &nDY);
    int nX2 = nDX + nX;
    int nY2 = nDY + nY;
    int nAngle2 = AngleBetween(nCX, nCY, nX2, nY2);
    int nLen = veclen(nCX, nX2, nCY, nY2) - 0x40 + (net_rand(0x38) & 0x7F);
    if (nLen < 0)
      nLen = 0;
    reinterpret_cast<CFlingy *>(pUnit)->GetVectorComponents(nAngle2, nLen, &nDX, &nDY);
    int nX3 = nDX + nCX;
    int nY3 = nDY + nCY;
    if (nX3 >= 0 && nX3 < gwMapPixWidth && nY3 >= 0 && nY3 < gwMapPixHeight) {
      ep_glob_t *pGlob = static_cast<ep_glob_t *>(epGlob);
      if (pGlob->aRegions[GetRegion_xy(nX3, nY3)].wArea ==
          pGlob->aRegions[GetRegion_xy(nX, nY)].wArea) {
        nX = nX3;
        nY = nY3;
      }
    }
  }

  if (bInterrupt) {
    if (pUnit->pOrderQueueHead == NULL) {
      pUnit->SetOrder_xy_interrupt(nOrder, nX, nY, 0);
      pUnit->EndDispatchOrder();
    }
  } else {
    pUnit->SetOrder_xy(nOrder, nX, nY);
  }
}

// tai_soldier_add_to_captain, oracle 0x4AE760 (224 B).
// Enlists a unit: pop a soldier off the captain's pool, link it at the head and
// send the unit to the captain's region. The stack parameter doubles as
// GetRegionCenter's Y out-slot.
// The soldier is bound before the pop, the pop tests the member and the rest
// tests the local; that keeps the oracle's second `test ecx, ecx`.
void tai_soldier_add_to_captain(TAI_Captain *pCaptain, CUnit *pUnit, int nY)
{
  TSoldierPool *pPool;
  TSoldier *pSoldier;
  int nX;

  if (pCaptain->bJob == 3 && nY == 0) {
    const BYTE bNation = pUnit->bPlayer;

    pCaptain = &sgCaptain[bNation][GetRegion_CUnit(pUnit)];
  }

  pPool = pCaptain->pPool;
  pSoldier = pPool->pFreeHead;
  if (pPool->pFreeHead != NULL) {
    pPool->pFreeHead = pPool->pFreeHead->pNext;
    if (pPool->pFreeHead != NULL)
      pPool->pFreeHead->pPrev = NULL;
  }
  if (pSoldier != NULL) {
    pSoldier->pPrev = NULL;
    pSoldier->pNext = pCaptain->pSoldiers;
    if (pCaptain->pSoldiers != NULL)
      pCaptain->pSoldiers->pPrev = pSoldier;
    pCaptain->pSoldiers = pSoldier;
    pSoldier->bType = 4;
    pSoldier->pUnit = pUnit;
    pSoldier->pCaptain = pCaptain;
    if ((pUnit->dwFlags & 0x04) == 0)
      pCaptain->wUnknown0A++;
    pUnit->dwUnion134 = reinterpret_cast<DWORD_PTR>(pSoldier);

    GetRegionCenter(pCaptain->wRegion, &nX, &nY);
    tai_soldier_move_toward(pUnit, pUnit->wType == 0x22 ? 0xB1 : 0x9D, nX, nY);

    if (pCaptain->bJob == 8 || pCaptain->bJob == 9 || pCaptain->bJob == 1 ||
        pCaptain->bJob == 2)
      tai_captain_pick_leader(pCaptain);
  }
}

// tai_soldier_update, oracle 0x4B1010 (237 B).
// The per-tick AI for a soldier: cast, retarget, then walk toward the captain's
// region or its attack unit's region. Job 1 substitutes the +0x24 unit's region
// for the captain's own. It calls tai_captain_has_forming_soldier_local, which keeps EDX live.
void tai_soldier_update(CUnit *pUnit)
{
  const TSoldier *const pSoldier =
      reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
  TAI_Captain *pCaptain;
  int nRegion;
  int nX, nY;

  if (pSoldier == NULL || pSoldier->bType != 4 || pSoldier->pUnit != pUnit)
    return;
  if ((gulUnitTypeFlags[pUnit->wType] & 0x01) != 0)
    return;

  pCaptain = pSoldier->pCaptain;
  if ((pUnit->wType == 0 || pUnit->wType == 0x20) &&
      tai_soldier_enter_bunker_in_region(pUnit, pCaptain->wRegion))
    return;
  if (ai_update_unit_spells(pUnit, 0))
    return;
  if (pUnit->AIPickAttackTarget(0, 1, 0))
    return;

  nRegion = pCaptain->wRegion;
  if (pCaptain->bJob == 1) {
    CUnit *pTarget;

    if (pCaptain->pUnit24 == NULL)
      tai_captain_pick_leader(pCaptain);
    pTarget = pCaptain->pUnit24;
    if (pTarget != NULL && tai_captain_has_forming_soldier_local(pCaptain))
      nRegion = GetRegion_CUnit(pTarget);
  }

  if (GetRegion_CUnit(pUnit) != nRegion) {
    GetRegionCenter(static_cast<WORD>(nRegion), &nX, &nY);
    tai_soldier_move_toward(pUnit, 0x0E, nX, nY);
    return;
  }
  pUnit->StopUnitMovement();
}

// tai_soldier_unload_into_neighbor, oracle 0x4AF810 (238 B).
// Picks a neighbouring region to unload into: the first adjacent region whose
// captain has a job in each of three classes wins, in the order 4/5/8/9, then
// 0, then anything else. The ten-arm table's two named arms must precede the
// default in source or the case blocks swap. The three 0x9C4 stores go after
// GetRegionPaths in the order A, B, C; `r = *pPaths++` gives pPaths EDX.
void tai_soldier_unload_into_neighbor(CUnit *pUnit)
{
  WORD *pPaths;
  int nCount;
  int nRegion;
  int nA, nB, nC;

  nRegion = GetRegion_CUnit(pUnit);
  nCount = GetRegionPaths(static_cast<WORD>(nRegion), &pPaths);
  nA = AI_AREAS_117;
  nB = AI_AREAS_117;
  nC = AI_AREAS_117;

  while (nCount != 0) {
    const unsigned r = *pPaths++;

    nCount--;
    switch (sgCaptain[pUnit->bPlayer][r].bJob) {
      case 4:
      case 5:
      case 8:
      case 9:
        nA = r;
        break;
      case 0:
        nB = r;
        break;
      default:
        nC = r;
        break;
    }
  }

  if (nA == AI_AREAS_117) {
    if (nB != AI_AREAS_117)
      nA = nB;
    else if (nC != AI_AREAS_117)
      nA = nC;
    else
      nA = nRegion;
  }

  if (IsInaccessibleRegion(static_cast<WORD>(nA))) {
    AIMoveWhereNeeded(pUnit);
    return;
  }
  {
    int nX, nY;
    GetRegionCenter(static_cast<WORD>(nA), &nX, &nY);
    pUnit->SetOrder_xy(0x70, nX, nY);
  }
}

// tai_captain_recompute_threats, oracle 0x4ABF90 (239 B).
// Rebuilds every captain's two threat accumulators from the units on the map,
// air into +0x1A and ground into +0x18. The chained clear gives the in-loop
// zero register; the row is indexed in the loop and hoisted only because
// sgCaptain is static.
void tai_captain_recompute_threats(int nArg0)
{
  CUnit *pUnit;
  unsigned nAreas;
  unsigned n;

  if (dword_6B1A94 != 0)
    return;
  nAreas = NumAreas();

  for (n = 0; n < PlayerLobbySlots(); n++) {  // Hyperion
    unsigned i;

    for (i = 1; i < nAreas; i++)
      sgCaptain[n][i].wUnknown18 = sgCaptain[n][i].wUnknown1A = 0;
  }

  for (pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
    unsigned nStrength;
    unsigned nRegion;
    unsigned p;

    if (pUnit->IsDeadOrDying())
      continue;
    {
      const unsigned nA = *reinterpret_cast<WORD *>(&pUnit->_unmapped_138[0x00]);
      const unsigned nB = *reinterpret_cast<WORD *>(&pUnit->_unmapped_138[0x02]);

      nStrength = nA > nB ? nA : nB;
    }
    if (nStrength == 0)
      continue;

    nRegion = GetRegion_CUnit(pUnit);
    for (p = 0; p < PlayerLobbySlots(); p++) {  // Hyperion
      if (gPlayerData[p].nType != 1)
        continue;
      if (pUnit->bPlayer == p)
        continue;
      if (!IsEnemyUnit(p, pUnit))
        continue;
      if ((pUnit->dwFlags & 0x04) != 0)
        sgCaptain[p][nRegion].wUnknown18 =
            static_cast<WORD>(sgCaptain[p][nRegion].wUnknown18 + nStrength);
      else
        sgCaptain[p][nRegion].wUnknown1A =
            static_cast<WORD>(sgCaptain[p][nRegion].wUnknown1A + nStrength);
    }
  }
}

// tai_captain_regions_connected, oracle 0x4AB330 (234 B).
// Answers whether the nation may treat two regions as one front. GetRegionNeighbors's
// out-pointer lands in the third parameter's own stack slot, which is MSVC
// reusing a dead argument home and not an aliasing claim. With the count
// decremented before the compare, the count stays in EAX and nRegion gets ESI.
BOOL tai_captain_regions_connected(int nNation, int nRegion, int nOther)
{
  WORD *pPaths;
  int n;

  if (static_cast<unsigned>(nNation) >= (unsigned)PlayerLobbySlots())  // Hyperion
    return 1;
  if (nRegion == nOther)
    return 1;
  if (GetAreaIndex(static_cast<WORD>(nRegion))
      != GetAreaIndex(static_cast<WORD>(nOther)))
    return 0;

  n = GetRegionNeighbors(static_cast<WORD>(nRegion), &pPaths);
  while (n != 0) {
    n--;
    if (*pPaths++ == nOther)
      return 1;
  }

  const TAI_Captain *pCapt = &sgCaptain[nNation][nRegion];
  if (pCapt->pUnit1C != NULL
      && pCapt->pUnit1C->GetWeaponAir() != WEAPON_NONE)
    return 1;

  if (g_dwElapsedTime >= dword_6B68C0[nNation] + 10
      || dword_6B68E0[nNation] != static_cast<DWORD>(nRegion))
    tai_captain_flood_reachable_regions(nNation, nRegion);
  return sgRegions[nNation][nOther] == 2;  // Hyperion: a row a nation
}

// tai_reposition_for_attack
// Re-aims a harassing unit at the position ai_get_flee_target hands back, unless the
// unit is already standing on it. The 0x40000000 test takes the unit when the bit is
// clear. The type is read from the unit at each use.
BOOL tai_reposition_for_attack(CUnit *pUnit, void *pArg1)
{
  const CSprite *pSprite;
  order_target_t t;

  if ((pUnit->dwFlags & 0x20000) != 0
      && pUnit->bOrderId != 0x76
      && pUnit->wType != 0x23
      && !(pUnit->wType == 0x67 && pUnit->bIrradiateTimer != 0)) {
    if (((gulUnitTypeFlags[pUnit->wType] & 0x200000) != 0
         && (pUnit->dwFlags & 0x40000000) == 0)
        || pUnit->wType == 0x2A) {
      if (AIMoveWhereNeeded(pUnit))
        return 1;
    }
    if (pUnit->wType == 0x48
        && static_cast<BYTE>(pUnit->bUpgradeType + pUnit->bTechType) <= 4) {
      if (AIMoveWhereNeeded(pUnit))
        return 1;
    }

    t = ai_get_flee_target(pArg1, pUnit);
    pSprite = pUnit->pSprite;
    if (t.pt.x != PosS(pSprite->wPosX)
        || t.pt.y != pSprite->wPosY) {
      pUnit->SetOrder_Target(6, t);
      return 1;
    }
  }
  return 0;
}

// Globals only this TU's newer bodies read, spelled as the TUs that already use
// them spell them.
#ifdef OC_DLL_MODE
#define epGlob (*reinterpret_cast<void **>(OC_VA(0x69B584u)))
#else
extern void *epGlob;                 // 0x69B584
#endif

#ifdef OC_DLL_MODE
#define gubUnitAIData (reinterpret_cast<BYTE *>(OC_VA(0x521578u)))
#else
extern BYTE gubUnitAIData[228] OC_UNALIGNED;      // 0x521578
#endif

// tai_captain_defend_paths_to_enemies, oracle 0x4B0BE0 (247 B)
// Marks the idle captains along each short path from a position to the nearest enemy
// unit of every unallied player as helpers.
void tai_captain_defend_paths_to_enemies(int nNation, int nX, int nY)
{
  const int nRegion = GetRegion_xy(nX, nY);

  for (unsigned nPlayer = 0; nPlayer < (unsigned)PlayerLobbySlots(); nPlayer++) {  // Hyperion
    if (sgbAlliances_g[nNation][nPlayer] != 0)
      continue;
    CUnit *pUnit = ai_nearest_enemy_of_player(nNation, nPlayer, nX, nY);
    if (pUnit == NULL)
      continue;
    const int nTarget = GetRegion_CUnit(pUnit);
    WORD awPath[4];
    int nTmp;
    unsigned nCount;
    unsigned nRange;
    for (nRange = 2; nRange <= 6; nRange++) {
      nCount = find_choke_regions(nRegion, nTarget, nRange, awPath, &nTmp, 2);
      if (nCount != 0) {
        for (unsigned i = 0; i < nCount; i++) {
          const int nArea = awPath[i];
          TAI_Captain *pCaptain = &sgCaptain[nNation][nArea];
          if (pCaptain->bJob == 0) {
            sai_soldier_set_type(pCaptain, 4);
            pCaptain->bFlags |= 0x41;
            if (!IsInaccessibleRegion(nArea))
              pCaptain->wUnknown0C = 1000;
          }
        }
        break;
      }
    }
  }
}

// tai_captain_find_two_dark_templar, oracle 0x4B0250 (249 B).
// Finds two ready dark templar (type 0x3D) for the player to merge, or lets tai_collect_pair take the
// first idle one. (The comment here said science vessels until 2026-10-16.)
// It calls tai_collect_pair_local, which keeps EDX live.
BOOL tai_captain_find_two_dark_templar(int nPlayer, CUnit **ppFirst, CUnit **ppSecond)
{
  int nState = 1;
  CUnit *pFirst = NULL;
  CUnit *pSecond = NULL;

  for (CUnit *pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (pUnit->IsDeadOrDying())
      continue;
    if (pUnit->wType != 0x3D)
      continue;
    if (!(pUnit->dwFlags & 1) || (pUnit->dwFlags & 0x40))
      continue;
    if (pUnit->bOrderId == 0xB7)
      continue;
    const TSoldier *pSoldier =
        reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
    if (pSoldier == NULL)
      continue;
    if (pSoldier->bType == 1 &&
        tai_collect_pair_local(&nState, ppFirst, ppSecond, reinterpret_cast<intptr_t>(pUnit)))
      return 1;
    if (pSoldier->bType == 4) {
      if (pFirst != NULL)
        pSecond = pUnit;
      else
        pFirst = pUnit;
      const int nJob = pSoldier->pCaptain->bJob;
      if ((nJob == 0 || nJob == 5 || nJob == 4 || nJob == 8 || nJob == 9) &&
          tai_collect_pair_local(&nState, ppFirst, ppSecond, reinterpret_cast<intptr_t>(pUnit)))
        return 1;
    }
  }
  if (pFirst != NULL && pSecond != NULL) {
    *ppFirst = pFirst;
    *ppSecond = pSecond;
    return 1;
  }
  return 0;
}

// tai_captain_flood_reachable_regions
// Floods the nation's region states outward from one region through regions no captain
// is defending. The neighbour test is indexed and its store goes through pStates, which
// gives the oracle's reload of nBase for the store.
#if !defined(OC_PAYLOAD) || defined(OC_WITH_HELD)
void tai_captain_flood_reachable_regions(int nNation, int nRegion)
{
  dword_6B68C0[nNation] = g_dwElapsedTime;
  dword_6B68E0[nNation] = nRegion;
  BYTE *pStates = sgRegions[nNation];  // Hyperion: a row a nation
  memset(pStates, 0, AI_AREAS_117);
  pStates[nRegion] = 1;
  const unsigned nAreas = NumAreas();
  BOOL bChanged;

  do {
    bChanged = 0;
    for (unsigned i = 1; i < nAreas; i++) {
      if (pStates[i] != 1)
        continue;
      pStates[i] = 2;
      WORD *pPaths;
      int nPaths = GetRegionPaths(i, &pPaths);
      while (nPaths != 0) {
        const WORD wNext = *pPaths;
        nPaths--;
        if (pStates[wNext] == 0) {
          const TAI_Captain *pCaptain = &sgCaptain[nNation][wNext];
          pStates[wNext] = (pCaptain->pUnit1C == NULL && pCaptain->pUnit20 == NULL) ? 1 : 2;
          bChanged = 1;
        }
        pPaths++;
      }
    }
  } while (bChanged);
}
#endif  // OC_PAYLOAD  (tai_captain_flood_reachable_regions)

// tai_soldier_is_in_position, oracle 0x4B09C0 (293 B)
// True unless the soldier is close to its captain's leader and inside the captain's region.
BOOL tai_soldier_is_in_position(CUnit *pUnit)
{
  const TSoldier *pSoldier =
      reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
  if (pSoldier == NULL || pSoldier->bType != 4 || pSoldier->pUnit != pUnit ||
      (gulUnitTypeFlags[pUnit->wType] & 1))
    return 1;
  TAI_Captain *pCaptain = pSoldier->pCaptain;
  CUnit *pLeader = pCaptain->pUnit24;
  if (pLeader == NULL || (pLeader->dwFlags & 0x40) || pLeader == pUnit ||
      pCaptain->bJob == 2)
    return 1;
  if (static_cast<ep_glob_t *>(epGlob)->aRegions[GetRegion_CUnit(pLeader)].wTiles < 0x3C)
    return 1;
  if (!tai_captain_units_connected(pUnit, pCaptain->pUnit24))
    return 1;
  unsigned nDist;
  {
    const upos_t wY = pUnit->pSprite->wPosY;
    const upos_t wLeaderY = pCaptain->pUnit24->pSprite->wPosY;
    const upos_t wX = pUnit->pSprite->wPosX;
    const upos_t wLeaderX = pCaptain->pUnit24->pSprite->wPosX;
    nDist = veclen(PosS(wLeaderX), PosS(wX),
                   PosS(wLeaderY), PosS(wY));
  }
  if (nDist <= 0x80)
    return 1;
  int nX;
  int nY;
  GetRegionCenter(pCaptain->wRegion, &nX, &nY);
  const upos_t wY = pUnit->pSprite->wPosY;
  const upos_t wX = pUnit->pSprite->wPosX;
  return static_cast<unsigned>(veclen(PosS(wX), nX,
                                      PosS(wY), nY)) <= 0x180;
}

// tai_soldier_follower_lagging, oracle 0x4AFF20 (260 B)
// True when another soldier of the same captain is within reach on a connected region
// and is itself out of place.
BOOL tai_soldier_follower_lagging(CUnit *pUnit)
{
  const TSoldier *pSoldier =
      reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
  if (pSoldier == NULL || pSoldier->bType != 4 || pSoldier->pUnit != pUnit ||
      (gulUnitTypeFlags[pUnit->wType] & 1))
    return 0;
  const TAI_Captain *pCaptain = pSoldier->pCaptain;
  if (pCaptain->bJob == 2)
    return 0;
  for (const TSoldier *p = pCaptain->pSoldiers; p != NULL; p = p->pNext) {
    CUnit *pOther = p->pUnit;
    if (pOther == pUnit)
      continue;
    unsigned nDist;
    {
      const upos_t wY = pUnit->pSprite->wPosY;
      const upos_t wOtherY = pOther->pSprite->wPosY;
      const upos_t wX = pUnit->pSprite->wPosX;
      const upos_t wOtherX = pOther->pSprite->wPosX;
      nDist = veclen(PosS(wOtherX), PosS(wX),
                     PosS(wOtherY), PosS(wY));
    }
    if (nDist <= 0x180) {
      const BYTE bPlayer = pUnit->bPlayer;
      if (tai_captain_regions_connected(bPlayer, GetRegion_CUnit(pUnit), GetRegion_CUnit(pOther)) &&
          !tai_soldier_is_in_position(pOther) && nDist > 0x100)
        return 1;
    }
  }
  return 0;
}

// TAI_Captain_aiscript_implode, oracle 0x4AD430 (274 B)
// Sends every nation captain whose region has at most one live neighbour back to
// defend (job 4); the set_type(4) body is written out because the oracle inlines it.
void TAI_Captain_aiscript_implode(int nNation)
{
  const unsigned nAreas = NumAreas();
  TAI_Captain *pCaptain = &sgCaptain[nNation][1];

  for (unsigned i = 1; i < nAreas; i++, pCaptain++) {
    if (pCaptain->bJob > 3)
      continue;
    WORD *pPaths;
    const unsigned nPaths = GetRegionNeighbors(pCaptain->wRegion, &pPaths);
    BOOL bFound = 0;
    unsigned j;
    for (j = 0; j < nPaths; j++, pPaths++) {
      if (sgCaptain[nNation][*pPaths].bJob <= 3) {
        if (bFound)
          break;
        bFound = 1;
      }
    }
    if (j != nPaths || pCaptain->wRegion == 0)
      continue;
    if (pCaptain->bJob == 0) {
      const BOOL bInaccessible = IsInaccessibleRegion(pCaptain->wRegion);
      const int nMin = banker_get_defaultmin(pCaptain->bNation);
      const int nValue = aiscript_has_campaign(pCaptain->bNation) ? nMin : 0;
      pCaptain->wUnknown0C = static_cast<WORD>(bInaccessible ? 0 : nValue);
      pCaptain->wUnknown0E = static_cast<WORD>(bInaccessible ? nValue : 0);
      pCaptain->wGroundStr10 = 0;
      pCaptain->wAirStr12 = 0;
      pCaptain->wGroundStr14 = 0;
      pCaptain->wAirStr16 = 0;
      pCaptain->pUnit1C = NULL;
      pCaptain->pUnit20 = NULL;
      pCaptain->pUnit24 = NULL;
      pCaptain->pUnit28 = NULL;
      pCaptain->wUnknown18 = 0;
      pCaptain->wUnknown1A = 0;
      pCaptain->wUnknown02 = 0;
    }
    pCaptain->bFlags &= ~0x20;
    pCaptain->bJob = 4;
    pCaptain->bUnknown06 = 0x28;
  }
}

// TAI_Captain_aiscript_capt_expand, oracle 0x4AD310 (275 B)
// Marks the regions next to the nation's defended ones, in one or two passes.
void TAI_Captain_aiscript_capt_expand(int nNation, int nPass)
{
  for (;;) {
    BYTE abMarked[AI_AREAS_117];
    memset(abMarked, 0, sizeof(abMarked));
    const unsigned nAreas = NumAreas();
    TAI_Captain *pCaptain = &sgCaptain[nNation][1];
    unsigned i;

    for (i = 1; i < nAreas; i++, pCaptain++) {
      if (nPass != 0 &&
          static_cast<ep_glob_t *>(epGlob)->aRegions[static_cast<WORD>(i)].wTiles > 10)
        continue;
      pCaptain->bFlags &= ~0x01;
      if (aiscript_has_campaign(nNation))
        pCaptain->bFlags &= ~0x40;
      if (pCaptain->bJob > 3) {
        WORD *pPaths;
        int nPaths = GetRegionNeighbors(pCaptain->wRegion, &pPaths);
        while (nPaths != 0) {
          abMarked[*pPaths++] = 1;
          nPaths--;
        }
      }
    }
    pCaptain = &sgCaptain[nNation][1];
    for (i = 1; i < nAreas; i++, pCaptain++) {
      if (abMarked[i] != 0 && pCaptain->bJob == 0) {
        sai_soldier_set_type(pCaptain, 4);
        pCaptain->bFlags |= 0x01;
        if (aiscript_has_campaign(nNation))
          pCaptain->bFlags |= 0x40;
      }
    }
    if (nPass != 0)
      break;
    nPass = 1;
  }
}

// tai_captain_recount_strength, oracle 0x4ADBD0 (275 B)
// Recounts a captain's air and ground strength and raises its two needs to match,
// capped by the banker's maximum force. The air add comes before the ground add.
void tai_captain_recount_strength(TAI_Captain *pCaptain)
{
  pCaptain->wGroundStr10 = 0;
  pCaptain->wAirStr12 = 0;
  pCaptain->wGroundStr14 = 0;
  pCaptain->wAirStr16 = 0;
  for (const TSoldier *p = pCaptain->pSoldiers; p != NULL; p = p->pNext) {
    CUnit *pUnit = p->pUnit;
    if (pUnit->dwFlags & 0x20)
      continue;
    const int nAir = pUnit->AirStrength();
    const int nGround = pUnit->GroundStrength();
    pCaptain->wAirStr16 += nAir;
    pCaptain->wGroundStr14 += nGround;
    if (GetRegion_CUnit(pUnit) == pCaptain->wRegion) {
      pCaptain->wAirStr12 += nAir;
      pCaptain->wGroundStr10 += nGround;
    }
  }
  if (aiscript_has_campaign(pCaptain->bNation)) {
    int nX;
    int nY;
    GetRegionCenter(pCaptain->wRegion, &nX, &nY);
    find_first_unit_near(nX, nY, 0x100,
               reinterpret_cast<CUnitSearchPredicate>(tai_captain_add_unit_strength_cb),
               reinterpret_cast<CUnit *>(pCaptain));
  }
  unsigned nNeed = pCaptain->wUnknown18;
  if (!aiscript_has_campaign(pCaptain->bNation))
    nNeed += nNeed >> 1;
  if (nNeed > pCaptain->wUnknown0E)
    pCaptain->wUnknown0E = nNeed;
  nNeed = pCaptain->wUnknown1A;
  if (!aiscript_has_campaign(pCaptain->bNation))
    nNeed += nNeed >> 1;
  if (nNeed > pCaptain->wUnknown0C)
    pCaptain->wUnknown0C = nNeed;
  const unsigned nMax = banker_get_max_force(pCaptain->bNation);
  if (nMax != 0) {
    if (pCaptain->wUnknown0E > nMax)
      pCaptain->wUnknown0E = nMax;
    if (pCaptain->wUnknown0C > nMax)
      pCaptain->wUnknown0C = nMax;
  }
}

// tai_captain_harass_list_filled, oracle 0x4AB1D0 (277 B)
// True when the nation already has every unit its harass list asks for.
BOOL tai_captain_harass_list_filled(int nNation)
{
  int aNeeded[63];
  unsigned nNeeded = 0;
  const WORD *pNeed = aiscript_get_harass_need(nNation);

  while (*pNeed != 0)
    aNeeded[nNeeded++] = *pNeed++ - 1;

  for (CUnit *pUnit = CUnit::GetPlayerUnitList(nNation); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (pUnit->IsDeadOrDying())
      continue;
    int nType = pUnit->wType;
    if (gulUnitTypeFlags[nType] & 0x10)
      continue;
    if (nType == 0x1E)
      nType = 5;
    if (gubUnitAIData[nType] & 1)
      continue;
    if (!(pUnit->dwFlags & 1))
      continue;
    const int nOrder = pUnit->bOrderId;
    if (nOrder == 0x6A || nOrder == 0x96 || gubOrderAttack[nOrder] != 0)
      continue;
    if (pUnit->dwUnion134 == 0)
      continue;
    for (unsigned i = 0; i < nNeeded; i++) {
      if (nType == aNeeded[i]) {
        aNeeded[i] = 0xE4;
        if (i == nNeeded - 1)
          nNeeded--;
        break;
      }
    }
  }
  for (unsigned i = 0; i < nNeeded; i++) {
    if (aNeeded[i] != 0xE4)
      return 0;
  }
  return 1;
}

// aiscript_eval_harass, oracle 0x4AF5B0 (281 B)
// Starts an attack, or reports that the enemy near the rally point outguns the harass list.
// The nIndex local is needed for the match.
BOOL aiscript_eval_harass(int nNation, int nArg1, int nArg2)
{
  if (TAI_Captain_aiscript_attack(nNation, nArg1, nArg2, 0, 0))
    return 0;
  unsigned nAirNeed = 0;
  unsigned nGroundNeed = 0;
  const WORD *pNeed = aiscript_get_harass_need(nNation);
  for (WORD wType = *pNeed; wType != 0; wType = *++pNeed) {
    nAirNeed += tai_get_air_threat_base(wType - 1);
    nGroundNeed += tai_get_ground_threat_base(wType - 1);
  }
  int nX;
  int nY;
  const int nIndex = banker_attack_target_area(nNation) - 1;
  GetRegionCenter(sgCaptain[nNation][nIndex].wUnknown02, &nX, &nY);
  unsigned nGround = 0;
  unsigned nAir = 0;
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
    if (pUnit->IsDeadOrDying())
      continue;
    if (!IsEnemyUnit(nNation, pUnit))
      continue;
    const upos_t wY = pUnit->pSprite->wPosY;
    const upos_t wX = pUnit->pSprite->wPosX;
    if (veclen(PosS(wX), nX, PosS(wY), nY) >= 0x400)
      continue;
    nAir += pUnit->AirStrength();
    nGround += pUnit->GroundStrength();
  }
  if (nAirNeed > nAir || nGroundNeed > nGround)
    return 1;
  return 0;
}

// sai_soldier_set_type, oracle 0x4AB900 (319 B)
// Gives a captain a new job and resets its timer; a captain coming off job 0 is cleared first.
// The case order sets the jump table's block order.
void sai_soldier_set_type(TAI_Captain *pCaptain, int nType)
{
  if (pCaptain->wRegion == 0)
    return;
  if (pCaptain->bJob == 0) {
    const BOOL bInaccessible = IsInaccessibleRegion(pCaptain->wRegion);
    const int nMin = banker_get_defaultmin(pCaptain->bNation);
    const int nValue = aiscript_has_campaign(pCaptain->bNation) ? nMin : 0;
    pCaptain->wUnknown0C = static_cast<WORD>(bInaccessible ? 0 : nValue);
    pCaptain->wUnknown0E = static_cast<WORD>(bInaccessible ? nValue : 0);
    pCaptain->wGroundStr10 = 0;
    pCaptain->wAirStr12 = 0;
    pCaptain->wGroundStr14 = 0;
    pCaptain->wAirStr16 = 0;
    pCaptain->pUnit1C = NULL;
    pCaptain->pUnit20 = NULL;
    pCaptain->pUnit24 = NULL;
    pCaptain->pUnit28 = NULL;
    pCaptain->wUnknown18 = 0;
    pCaptain->wUnknown1A = 0;
    if (nType != 7)
      pCaptain->wUnknown02 = 0;
  }
  pCaptain->bFlags &= ~0x20;
  pCaptain->bJob = nType;
  switch (pCaptain->bJob) {
  case 3:
    pCaptain->bFlags &= ~0x01;
    pCaptain->bUnknown06 = 10;
    break;
  default:
    pCaptain->bUnknown06 = 10;
    break;
  case 0:
    pCaptain->bUnknown06 = 10;
    if ((pCaptain->bFlags & 0x40) && !aiscript_has_campaign(pCaptain->bNation))
      pCaptain->bUnknown06 = 0x28;
    break;
  case 4:
    pCaptain->bUnknown06 = 0x28;
    break;
  case 8:
  case 9:
    pCaptain->bUnknown06 = aiscript_has_campaign(pCaptain->bNation) ? 0x32 : 0xA0;
    break;
  case 5:
    pCaptain->bUnknown06 = 0x32;
    break;
  case 1:
  case 2:
    pCaptain->bUnknown06 = aiscript_has_campaign(pCaptain->bNation) ? 0x46 : 0xA0;
    break;
  case 7:
    pCaptain->bUnknown09 = 0;
    if (region_has_worker_or_building(pCaptain->bNation, pCaptain->wUnknown02)) {
      pCaptain->bUnknown06 = 0x6E;
      break;
    }
  case 6:
    pCaptain->bUnknown06 =
        region_has_worker_or_building(pCaptain->bNation, pCaptain->wRegion) ? 0x6E : 0x64;
    break;
  }
}

// tai_captain_cannot_fight_region, oracle 0x4AB490 (313 B)
// True when a captain cannot usefully help the target region.
// The air test is one `||`; two ifs swap the frame slots of nAny and nAir.
// The region reaches ECX directly only through get_units_in_region's WORD seat.
int tai_captain_cannot_fight_region(TAI_Captain *pCaptain, TAI_Captain *pTarget)
{
  int nAny;
  int nGround;
  int nAir;

  tai_captain_survey_weapons(pTarget, &nAny, &nGround, &nAir);
  if (nAny != 0 && !tai_has_transports(pCaptain->bNation)) {
    const TSoldier *pSoldier = pCaptain->pSoldiers;
    if (pSoldier == NULL)
      return 1;
    int nX;
    int nY;
    GetRegionCenter(pTarget->wRegion, &nX, &nY);
    if (!sai_path_can_reach_target(pSoldier->pUnit, nX, nY))
      return 1;
  }
  if (nAir == 0 && nGround == 0)
    return 0;
  if (pCaptain->wRegion == pTarget->wRegion)
    return 0;
  CUnit **pp = get_units_in_region(pTarget->wRegion);
  CUnit **ppUnits = pp;
  int nResult = 1;
  CUnit *pUnit = *pp;
  if (pUnit != NULL) {
    do {
      if (!IsEnemyUnit(pTarget->bNation, pUnit))
        continue;
      if ((((pUnit->dwFlags >> 2) & 1) == 0 && nAir != 0) ||
          (((pUnit->dwFlags >> 2) & 1) != 0 && nGround != 0)) {
        nResult = 0;
        break;
      }
    } while ((pUnit = *++pp) != NULL);
  }
  finish_unit_iteration(ppUnits);
  return nResult;
}

// tai_captain_job_gather, oracle 0x4AC870 (320 B)
// Jobs 8 and 9's update (tai_captain_service's table sends both here): re-counts, then either
// retreats the captain's soldiers or returns them. (The comment here said job 4 until 2026-10-16.)
void tai_captain_job_gather(TAI_Captain *pCaptain)
{
  tai_captain_recount_strength(pCaptain);
  tai_captain_retarget_soldiers(pCaptain);
  if ((pCaptain->bFlags & 0x20) && tai_captain_disband_if_weak(pCaptain))
    return;
  int nAny;
  int nGround;
  int nAir;
  tai_captain_survey_weapons(pCaptain, &nAny, &nGround, &nAir);
  if ((nAny != 0 && IsInaccessibleRegion(pCaptain->wRegion)) ||
      tai_captain_cannot_fight_region(pCaptain, pCaptain)) {
    sai_soldier_set_type(pCaptain, 0);
    CUnit *pUnit = tai_captain_pick_attack_target(pCaptain);
    if (pUnit != NULL) {
      const int nRegion = GetRegion_CUnit(pUnit);
      TAI_Captain *pTarget = &sgCaptain[pCaptain->bNation][nRegion];
      sai_soldier_set_type(pTarget, 9);
      tai_captain_transfer_soldiers(pCaptain, pTarget, 0, 0, 1);
      return;
    }
    tai_captain_release_all_soldiers(pCaptain);
    return;
  }
  if (!banker_attack_window_expired(pCaptain->bNation)) {
    int anRegions[63];
    const unsigned nCount = tai_captain_list_missing_harass_types(pCaptain, anRegions, 1);
    for (unsigned i = 0; i < nCount; i++) {
      if (static_cast<unsigned>(anRegions[i]) < 0xE4)
        return;
    }
    if (tai_captain_gather_still_moving(pCaptain->bNation))
      return;
  }
  if (pCaptain->wUnknown02 != 0) {
    TAI_Captain *pTarget = &sgCaptain[pCaptain->bNation][pCaptain->wUnknown02];
    sai_soldier_set_type(pTarget, 1);
    tai_captain_transfer_soldiers(pCaptain, pTarget, 0, 0, 1);
  }
  sai_soldier_set_type(pCaptain, 0);
}

// tai_captain_enlist_type, oracle 0x4AE960 (261 B)
// Enlists the nearest suitable unit of a type for a captain and settles the donor's strength.
BOOL tai_captain_enlist_type(TAI_Captain *pCaptain, int nType, int nLimit)
{
  int nX;
  int nY;
  GetRegionCenter(pCaptain->wRegion, &nX, &nY);
  TAI_Captain *pOther = &sgCaptain[pCaptain->bNation][pCaptain->wUnknown02];
  TSoldier *pSoldier = tai_captain_find_spare_soldier(pCaptain, nType, nX, nY, pOther);
  if (pSoldier == NULL) {
    pSoldier = tai_captain_find_loose_soldier(pCaptain, nLimit, nType, nX, nY, pOther);
    if (pSoldier == NULL)
      return 0;
  }
  TAI_Captain *pFrom;
  const BYTE bType = pSoldier->bType;
  if (bType == 1) {
    CUnit *pUnit = pSoldier->pUnit;
    pUnit->dwUnion134 = 0;
    tai_soldier_add_to_captain(pCaptain, pUnit, 1);
    pSoldier->pUnit = NULL;
    pFrom = &sgCaptain[pCaptain->bNation][GetRegion_CUnit(pUnit)];
  } else {
    if (bType != 4)
      return 0;
    pFrom = pSoldier->pCaptain;
    CUnit *pUnit = pSoldier->pUnit;
    tai_captain_unlink_soldier(pFrom, pSoldier);
    pUnit->dwUnion134 = 0;
    tai_soldier_add_to_captain(pCaptain, pUnit, 1);
    if (pUnit->dwFlags & 0x20)
      pUnit->ExitTransport();
  }
  tai_captain_recount_strength(pFrom);
  return 1;
}

// tai_captain_enlist_need_list, oracle 0x4AC530 (341 B)
// Enlists one unit of every type in a need list and sums their threat.
int tai_captain_enlist_need_list(TAI_Captain *pCaptain, TAI_Captain *pTarget, const WORD *pNeed, BOOL bAir)
{
  int nSum = 0;

  for (WORD wNeed = *pNeed; wNeed != 0; wNeed = *++pNeed) {
    const int nType = wNeed - 1;
    const int nLimit = pCaptain->bUnknown06;
    int nX;
    int nY;
    GetRegionCenter(pCaptain->wRegion, &nX, &nY);
    TAI_Captain *pOther = &sgCaptain[pCaptain->bNation][pCaptain->wUnknown02];
    TSoldier *pSoldier = tai_captain_find_spare_soldier(pCaptain, nType, nX, nY, pOther);
    if (pSoldier == NULL) {
      pSoldier = tai_captain_find_loose_soldier(pCaptain, nLimit, nType, nX, nY, pOther);
      if (pSoldier == NULL)
        continue;
    }
    TAI_Captain *pFrom;
    const BYTE bType = pSoldier->bType;
    if (bType == 1) {
      CUnit *pUnit = pSoldier->pUnit;
      pUnit->dwUnion134 = 0;
      tai_soldier_add_to_captain(pCaptain, pUnit, 1);
      pSoldier->pUnit = NULL;
      pFrom = &sgCaptain[pCaptain->bNation][GetRegion_CUnit(pUnit)];
    } else {
      if (bType != 4)
        continue;
      pFrom = pSoldier->pCaptain;
      CUnit *pUnit = pSoldier->pUnit;
      tai_captain_unlink_soldier(pFrom, pSoldier);
      pUnit->dwUnion134 = 0;
      tai_soldier_add_to_captain(pCaptain, pUnit, 1);
      if (pUnit->dwFlags & 0x20)
        pUnit->ExitTransport();
    }
    tai_captain_recount_strength(pFrom);
    nSum += bAir ? tai_get_air_threat_base(nType)
                 : tai_get_ground_threat_base(nType);
  }
  return nSum;
}

// tai_captain_raise_defence, oracle 0x4AC3C0 (363 B)
// Enlists defenders for a captain, then queues defence builds until the threat reaches nMax.
int tai_captain_raise_defence(TAI_Captain *pCaptain, TAI_Captain *pTarget, BOOL bAir, unsigned nMax)
{
  unsigned nSum = 0;

  if (pTarget->bJob == 4) {
    if (static_cast<ep_glob_t *>(epGlob)->aRegions[pTarget->wRegion].wTiles < 0x10)
      return 999;
  } else {
    const BOOL bInaccessible = IsInaccessibleRegion(pCaptain->wRegion) ||
                               IsInaccessibleRegion(pTarget->wRegion);
    WORD *pUse;
    if (bAir) {
      if (bInaccessible)
        pUse = get_defenseuse_aa(pCaptain->bNation);
      else
        pUse = get_defenseuse_ag(pCaptain->bNation);
    } else {
      if (bInaccessible)
        pUse = get_defenseuse_ga(pCaptain->bNation);
      else
        pUse = get_defenseuse_gg(pCaptain->bNation);
    }
    nSum = tai_captain_enlist_need_list(pCaptain, pTarget, pUse, bAir);
    if (nSum != 0)
      return nSum;
  }
  {
    WORD *pBuild;
    if (!IsInaccessibleRegion(pCaptain->wRegion) &&
        !IsInaccessibleRegion(pTarget->wRegion)) {
      if (bAir)
        pBuild = get_defensebuild_ag(pCaptain->bNation);
      else
        pBuild = get_defensebuild_gg(pCaptain->bNation);
    } else {
      if (bAir)
        pBuild = get_defensebuild_aa(pCaptain->bNation);
      else
        pBuild = get_defensebuild_ga(pCaptain->bNation);
    }
    if (*pBuild == 0)
      return 999;
    const int nLimit = banker_request_budget(pCaptain->bNation) + pCaptain->bUnknown06;
    for (WORD wBuild = *pBuild; wBuild != 0; wBuild = *++pBuild) {
      const int nType = wBuild - 1;
      banker_demand_unit_for_captain(pCaptain->bNation, reinterpret_cast<intptr_t>(pCaptain), nType,
                 nLimit, 0, 0);
      nSum += bAir ? tai_get_air_threat_base(nType)
                   : tai_get_ground_threat_base(nType);
      if (aiscript_has_campaign(pCaptain->bNation) && nSum >= nMax)
        return nSum;
    }
  }
  return nSum;
}

// tai_captain_find_spare_soldier, oracle 0x4AC230 (395 B).
// The nearest soldier of a type another captain can spare. It calls
// tai_captain_job_is_gather_local, which keeps DX live.
TSoldier *tai_captain_find_spare_soldier(TAI_Captain *pCaptain, int nType, int nX, int nY, TAI_Captain *pOther)
{
  if (nType == 5)
    return NULL;
  TSoldier *pBest = NULL;
  unsigned nBest = 9999999;
  for (CUnit *pUnit = CUnit::GetPlayerUnitList(pCaptain->bNation); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (!tai_captain_unit_is_recruitable(pUnit, nType, 1))
      continue;
    TSoldier *pSoldier = reinterpret_cast<TSoldier *>(pUnit->dwUnion134);
    if (pSoldier == NULL || pSoldier->bType != 4)
      continue;
    const TAI_Captain *pFrom = pSoldier->pCaptain;
    const WORD wRegion = pFrom->wRegion;
    if (wRegion == pCaptain->wRegion)
      continue;
    const BYTE bJob = pFrom->bJob;
    if (bJob == 7 || bJob == 8 || bJob == 9)
      continue;
    if ((pCaptain->bJob == 7 || tai_captain_job_is_gather_local(pCaptain)) && wRegion == pOther->wRegion)
      continue;
    const int nAir = pUnit->AirStrength();
    if (nAir != 0) {
      const int nLeft = pFrom->wAirStr16 - nAir;
      if (nLeft < pFrom->wUnknown0E)
        continue;
      if (pFrom->bJob == 7 && nLeft <= pOther->wUnknown0E)
        continue;
    }
    const int nGround = pUnit->GroundStrength();
    if (nGround != 0) {
      const int nLeft = pFrom->wGroundStr14 - nGround;
      if (nLeft <= pFrom->wUnknown0C)
        continue;
      if (pFrom->bJob == 7 && nLeft <= pOther->wUnknown0C)
        continue;
    }
    if (!(pUnit->dwFlags & 4)) {
      const BYTE bPlayer = pUnit->bPlayer;
      sai_route_is_open(bPlayer, GetRegion_CUnit(pUnit), GetRegion_xy(nX, nY));
    }
    const upos_t wY = pUnit->pSprite->wPosY;
    const upos_t wX = pUnit->pSprite->wPosX;
    const unsigned nDist =
        veclen(nX, PosS(wX), nY, PosS(wY));
    if (nDist < nBest) {
      pBest = pSoldier;
      nBest = nDist;
    }
  }
  return pBest;
}

// tai_captain_find_loose_soldier, oracle 0x4AC0D0 (342 B).
// The nearest free or low-priority soldier of a type. The job test is tai_soldier_captain_is_attacking inlined.
// The type-1 arm takes its own accept block; sharing the tail swaps EBX and EDI.
TSoldier *tai_captain_find_loose_soldier(TAI_Captain *pCaptain, int nLimit, int nType, int nX, int nY, TAI_Captain *pOther)
{
  TSoldier *pBest = NULL;
  unsigned nBest = 9999999;
  for (CUnit *pUnit = CUnit::GetPlayerUnitList(pCaptain->bNation); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (!tai_captain_unit_is_recruitable(pUnit, nType, 0))
      continue;
    const upos_t wY = pUnit->pSprite->wPosY;
    const upos_t wX = pUnit->pSprite->wPosX;
    const unsigned nDist =
        veclen(nX, PosS(wX), nY, PosS(wY));
    if (nDist >= nBest)
      continue;
    // Testing the field, not the local, lets the inlined tai_soldier_captain_is_attacking drop its null test.
    if (pUnit->dwUnion134 == 0)
      continue;
    TSoldier *pSoldier = reinterpret_cast<TSoldier *>(pUnit->dwUnion134);
    if (pSoldier->bType == 1) {
      if (aiscript_has_campaign(pCaptain->bNation)) {
        if (pCaptain->bJob != 7)
          goto take;
        continue;
      }
      if (!tai_captain_job_is_gather(pCaptain))
        continue;
take:
      pBest = pSoldier;
      nBest = nDist;
      continue;
    } else {
      if (pSoldier->bType != 4)
        continue;
      TAI_Captain *pFrom = pSoldier->pCaptain;
      if (pFrom->wRegion == pCaptain->wRegion)
        continue;
      if (tai_soldier_captain_is_attacking(pUnit, 1) && !aiscript_has_campaign(pCaptain->bNation))
        continue;
      if (pUnit->dwFlags & 0x20) {
        if (nLimit == 0x14)
          continue;
        if (!aiscript_has_campaign(pUnit->bPlayer) && !tai_captain_job_is_gather(pCaptain))
          continue;
      }
      if ((pCaptain->bJob == 7 || tai_captain_job_is_gather(pCaptain)) &&
          pFrom->wRegion == pOther->wRegion)
        continue;
      if (pFrom->bUnknown06 >= static_cast<unsigned>(nLimit))
        continue;
    }
    pBest = pSoldier;
    nBest = nDist;
  }
  return pBest;
}

// tai_captain_find_two_high_templar, oracle 0x4B0430 (322 B).
// Finds the two lowest-energy ready high templar (type 0x43) for the player to merge, or lets
// tai_collect_pair take an idle one. (The comment here said defilers until 2026-10-16.)
// The energy byte is re-read at each use; tai_collect_pair_local keeps EDX live.
BOOL tai_captain_find_two_high_templar(int nPlayer, CUnit **ppFirst, CUnit **ppSecond)
{
  int nState = 1;
  CUnit *pFirst = NULL;
  unsigned nFirstEnergy = 9999;
  CUnit *pSecond = NULL;
  unsigned nSecondEnergy = 9999;

  for (CUnit *pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (pUnit->IsDeadOrDying())
      continue;
    if (pUnit->wType != 0x43)
      continue;
    if (!(pUnit->dwFlags & 1) || (pUnit->dwFlags & 0x40))
      continue;
    if (pUnit->bOrderId == 0x69)
      continue;
    const TSoldier *pSoldier =
        reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
    if (pSoldier == NULL)
      continue;
    if (pSoldier->bType == 1 &&
        tai_collect_pair_local(&nState, ppFirst, ppSecond, reinterpret_cast<intptr_t>(pUnit)))
      return 1;
    if (pSoldier->bType == 4) {
      if (pFirst != NULL &&
          static_cast<BYTE>(pUnit->wEnergy >> 8) >= nFirstEnergy) {
        const unsigned nEnergy = static_cast<BYTE>(pUnit->wEnergy >> 8);
        if (pSecond == NULL || nEnergy < nSecondEnergy) {
          pSecond = pUnit;
          nSecondEnergy = nEnergy;
        }
      } else {
        nFirstEnergy = static_cast<BYTE>(pUnit->wEnergy >> 8);
        pFirst = pUnit;
      }
      if (static_cast<BYTE>(pUnit->wEnergy >> 8) < 0x4B)
        continue;
      const int nJob = pSoldier->pCaptain->bJob;
      if ((nJob == 0 || nJob == 5 || nJob == 4 || nJob == 8 || nJob == 9) &&
          tai_collect_pair_local(&nState, ppFirst, ppSecond, reinterpret_cast<intptr_t>(pUnit)))
        return 1;
    }
  }
  if (pFirst != NULL && pSecond != NULL) {
    *ppFirst = pFirst;
    *ppSecond = pSecond;
    return 1;
  }
  return 0;
}

// tai_captain_crew_bunkers, oracle 0x4AE220 (341 B)
// Loads every empty bunker with marines or other infantry for the nation.
void tai_captain_crew_bunkers(intptr_t nArg0, int nNation)
{
  const unsigned nAreas = NumAreas();
  for (unsigned i = 1; i < nAreas; i++) {
    if (sgCaptain[nNation][i].bFlags & 2) {
      banker_demand_town_request(nNation, nArg0, 0, 0x7D, 0x50);
      break;
    }
  }
  for (CUnit *pUnit = CUnit::GetPlayerUnitList(nNation); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (pUnit->IsDeadOrDying())
      continue;
    if (pUnit->wType != 0x7D)
      continue;
    if (!(pUnit->dwFlags & 1))
      continue;
    if (!pUnit->HasEmptyCargoSpace())
      continue;
    int nKind;
    if (banker_has_defenseuse_gg(nNation, 0x20) &&
        !pUnit->IterateTransport(reinterpret_cast<CUnit::TransportProc>(tai_unit_is_firebat), NULL) &&
        count_get_completion(0x20, static_cast<BYTE>(nNation)) <
            count_get_completion(0x7D, static_cast<BYTE>(nNation))) {
      nKind = 0x20;
    } else {
      if (!banker_has_defenseuse_gg(nNation, 0))
        return;
      nKind = 0;
    }
    TAI_Captain *pTarget = &sgCaptain[nNation][GetRegion_CUnit(pUnit)];
    const int nLimit =
        (aiscript_has_campaign(nNation) && pUnit->GetTotalOccupiedCargo()) ? 0x14 : 0x64;
    if (tai_captain_enlist_type(pTarget, nKind, nLimit))
      continue;
    banker_demand_unit_for_captain(nNation, reinterpret_cast<intptr_t>(pTarget), nKind, nLimit, 1, 1);
    if (nLimit == 0x64)
      return;
  }
}

// True for the two orders a morphing or constructing unit sits in.
static inline bool IsBuildingOrder(int nOrder)
{
  return nOrder == 0x6A || nOrder == 0x96;
}

// tai_captain_list_missing_harass_types, oracle 0x4AA500 (352 B)
// Fills pnOut with the harass list's unit types the nation does not have yet and returns
// the count. The order test goes through an int helper; a BYTE compare reloads the order.
int tai_captain_list_missing_harass_types(TAI_Captain *pCaptain, int *pnOut, int bSkipBusy)
{
  unsigned nOut = 0;
  const WORD *pNeed = aiscript_get_harass_need(pCaptain->bNation);

  while (*pNeed != 0)
    pnOut[nOut++] = *pNeed++ - 1;

  for (CUnit *pUnit = CUnit::GetPlayerUnitList(pCaptain->bNation); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (pUnit->IsDeadOrDying())
      continue;
    int nType = pUnit->wType;
    if (gulUnitTypeFlags[nType] & 0x10)
      continue;
    if (nType == 0x1E)
      nType = 5;
    unsigned nCount = 1;
    const DWORD dwDone = pUnit->dwFlags & 1;
    if (dwDone == 0 || IsBuildingOrder(pUnit->bOrderId)) {
      if (bSkipBusy != 0)
        continue;
      if (nType == 0x24 || nType == 0x3B || nType == 0x61) {
        nType = pUnit->wBuildQueue[pUnit->bBuildQueueSlot];
        if (gulUnitTypeFlags[nType] & 0x400)
          nCount = 2;
      }
    }
    if (gubUnitAIData[nType] & 1)
      continue;
    if (dwDone != 0 && !IsBuildingOrder(pUnit->bOrderId)) {
      const TSoldier *pSoldier =
          reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
      if (pSoldier == NULL || pSoldier->bType != 4 ||
          pSoldier->pCaptain != pCaptain)
        continue;
    }
    while (nCount != 0) {
      nCount--;
      for (unsigned i = 0; i < nOut; i++) {
        if (nType == pnOut[i]) {
          pnOut[i] = 0xE4;
          if (i == nOut - 1)
            nOut--;
          break;
        }
      }
    }
  }
  return nOut;
}

// TAI_Captain_aiscript_guard, oracle 0x4B00A0 (358 B)
// Sets the unit's region captain to guard it and pulls allied idle captains in.
// The nType local loads the type ahead of the depot call, as the oracle does.
void TAI_Captain_aiscript_guard(int nNation, CUnit *pUnit)
{
  const int nRegion = GetRegion_CUnit(pUnit);
  TAI_Captain *pCaptain = &sgCaptain[nNation][nRegion];
  if (pCaptain->bJob == 0 || pCaptain->bJob == 4) {
    sai_soldier_set_type(pCaptain, 5);
    pCaptain->bFlags |= 1;
    if (!aiscript_has_campaign(nNation))
      pCaptain->bFlags |= 0x40;
    for (int i = 0; i < PlayerLobbySlots(); i++) {  // Hyperion
      if (!aiscript_has_campaign(i) && sgbAlliances_g[i][nNation] != 0 &&
          sgCaptain[i][nRegion].bJob == 0)
        sai_soldier_set_type(&sgCaptain[i][nRegion], 4);
    }
  }
  if (!(((pUnit->dwFlags & 1) != 0 || pUnit->wType == 0x85 || pUnit->wType == 0x84 ||
       (pUnit->wType == 0x83 && pUnit->IsZergBldgUpgrading())) &&
      (pUnit->dwFlags & 2) != 0 && (gulUnitTypeFlags[pUnit->wType] & 0x1000) != 0)) {
    const int nType = pUnit->wType;
    if (nType != get_player_depot_type(nNation))
      return;
  }
  if (aiscript_has_campaign(nNation)) {
    pCaptain->wUnknown0C = static_cast<WORD>(banker_get_defaultmin(nNation) * 6);
    pCaptain->wUnknown0E = static_cast<WORD>(banker_get_defaultmin(nNation) * 6);
    sai_soldier_set_type(pCaptain, 6);
    pCaptain->bTimer = 0x2D;
  } else {
    sai_soldier_set_type(pCaptain, 6);
    pCaptain->bFlags |= 0x40;
    pCaptain->wUnknown0C = 1500;
    pCaptain->wUnknown0E = 1000;
    pCaptain->bTimer = 0x2D;
  }
}

// tai_captain_pick_weakest_near_region, oracle 0x4AA350 (362 B)
// Picks the index of the listed region nearest the nation's base with the least enemy strength.
// The air total is declared unsigned and the ground total int, which makes the air zero an
// immediate store; nBest unsigned and declared last keeps it in EBX, and the list pointer is
// walked through the call's result while a copy is kept for finish_unit_iteration.
int tai_captain_pick_weakest_near_region(int nNation, unsigned nCount, const int *pnRegions)
{
  int nX, nY;
  if (nCount == 0 || !town_first_town_xy(nNation, &nX, &nY))
    return 0;
  if (nCount == 1)
    return pnRegions[0];
  unsigned nBestDist = 99999999;
  unsigned nBestStr = 99999999;
  unsigned nBest = 0;
  for (unsigned i = 0; i < nCount; i++) {
    int nCX, nCY;
    GetRegionCenter(static_cast<WORD>(pnRegions[i]), &nCX, &nCY);
    const unsigned nDist = veclen(nX, nCX, nY, nCY);
    if (nDist > nBestDist + 0x280)
      continue;
    rect16_t rc;
    rc.top = static_cast<short>(nCY - 0x200);
    rc.bottom = static_cast<short>(nCY + 0x200);
    rc.left = static_cast<short>(nCX - 0x200);
    rc.right = static_cast<short>(nCX + 0x200);
    CUnit **pp = get_units_in_rect(&rc);
    CUnit **ppList = pp;
    unsigned nAir = 0;
    int nGround = 0;
    for (CUnit *pUnit = *pp; pUnit != NULL; pUnit = *++pp) {
      if (IsEnemyUnit(nNation, pUnit)) {
        nAir += pUnit->AirStrength();
        nGround += pUnit->GroundStrength();
      }
    }
    finish_unit_iteration(ppList);
    const unsigned nStr = nAir > nGround ? nAir : nGround;
    if (nStr < nBestStr || nDist + 0x280 < nBestDist) {
      nBestDist = nDist;
      nBestStr = nStr;
      nBest = i;
    }
  }
  return nBest;
}

// AIMoveWhereNeeded, oracle 0x4AFD40 (376 B)
// Sends a soldier to the nearest region whose captain is defending a worker or building.
int AIMoveWhereNeeded(CUnit *pUnit)
{
  if (pUnit->dwUnion134 == 0)
    return 0;
  const int nPlayer = pUnit->bPlayer;
  const int nRegion = GetRegion_CUnit(pUnit);
  const BYTE bJob = sgCaptain[nPlayer][nRegion].bJob;
  if ((bJob == 5 || bJob == 4) && region_has_worker_or_building(nPlayer, nRegion))
    return 0;
  int nBestX = -1;
  unsigned nBestDist = 9999999;
  int nBestY = -1;
  const unsigned nAreas = NumAreas();
  const TAI_Captain *pCaptain = &sgCaptain[nPlayer][1];
  for (unsigned i = 1; i < nAreas; i++, pCaptain++) {
    if ((pCaptain->bJob == 5 || pCaptain->bJob == 4) &&
        region_has_worker_or_building(nPlayer, i)) {
      int nX, nY;
      GetRegionCenter(static_cast<WORD>(i), &nX, &nY);
      const upos_t wY = pUnit->pSprite->wPosY;
      const upos_t wX = pUnit->pSprite->wPosX;
      const unsigned nDist = veclen(nX, PosS(wX), nY, PosS(wY));
      if (nDist < nBestDist) {
        nBestDist = nDist;
        nBestX = nX;
        nBestY = nY;
      }
    }
  }
  if (nBestX == -1)
    return 0;
  if (reinterpret_cast<const TSoldier *>(pUnit->dwUnion134)->bType == 4) {
    SAI_RemoveUnit(pUnit, 0);
    tai_soldier_add_to_captain(&sgCaptain[nPlayer][GetRegion_xy(nBestX, nBestY)], pUnit, 1);
  }
  int nOrder = pUnit->wType == 0x22 ? 0xB1 : 6;
  if (pUnit->bOrderId == 0x6F || pUnit->bOrderId == 0x70)
    nOrder = 0x70;
  pUnit->SetOrder_xy(static_cast<BYTE>(nOrder), nBestX, nBestY);
  return 1;
}

// tai_send_unit_at_nearest_enemy, oracle 0x4ABD00 (385 B)
// Points an idle soldier at the nearest enemy unit of a randomly chosen enemy player.
void tai_send_unit_at_nearest_enemy(CUnit *pUnit)
{
  if ((pUnit->dwFlags & 0x20000) == 0 && (pUnit->dwFlags & 0x10) == 0)
    return;
  CUnit *apHeads[PLAYER_SLOTS_MAX];  // Hyperion: 8 in 1.17
  unsigned nHeads;
  if (pUnit->pOrderTarget != NULL && pUnit->IsEnemyUnit(pUnit->pOrderTarget)) {
    nHeads = 1;
    apHeads[0] = pUnit->pOrderTarget;
  } else {
    nHeads = 0;
    for (unsigned i = 0; i < (unsigned)PlayerLobbySlots(); i++) {
      CUnit *pHead = CUnit::GetPlayerUnitList(i);
      if (pHead != NULL && pUnit->IsEnemyUnit(pHead))
        apHeads[nHeads++] = pHead;
    }
    if (nHeads == 0)
      return;
  }
  unsigned nIndex = static_cast<unsigned>(net_rand(0x36)) % nHeads;
  for (unsigned n = 0; n < nHeads; n++, nIndex++) {
    if (nIndex >= nHeads)
      nIndex = 0;
    unsigned nBestDist = 0xFFFFFFFF;
    CUnit *pBest = NULL;
    for (CUnit *pOther = apHeads[nIndex]; pOther != NULL; pOther = pOther->pPlayerUnitNext) {
      if (pOther->IsDeadOrDying())
        continue;
      if (pUnit->CanAttack() && !pUnit->CanTarget(pOther, 1))
        continue;
      const int nDX = PosS(pOther->pSprite->wPosX) - PosS(pUnit->pSprite->wPosX);
      const int nDY = PosS(pOther->pSprite->wPosY) - PosS(pUnit->pSprite->wPosY);
      const unsigned nDist = nDX * nDX + nDY * nDY;
      if (nDist < nBestDist) {
        nBestDist = nDist;
        pBest = pOther;
      }
    }
    if (pBest != NULL) {
      if (pUnit->CanAttack())
        CUnit::NewTarget(pUnit, pBest, 0, 0);
      else
        pUnit->SetOrder_xy(6, PosS(pBest->pSprite->wPosX), PosS(pBest->pSprite->wPosY));
      SAI_RemoveUnit(pUnit, 0);
      return;
    }
  }
}

// Wipes a soldier's payload, leaving its two links alone.
static inline void ClearSoldier(TSoldier *pSoldier)
{
  pSoldier->bType = 0;
  pSoldier->bUnknown09 = 0;
  pSoldier->wRegion = 0;
  pSoldier->pUnit = NULL;
  pSoldier->pCaptain = NULL;
}

// The save form of a pool link is a 1-based index into the pool.
static inline TSoldier *SoldierToIndex(const TSoldierPool *pPool, TSoldier *pSoldier)
{
  if (pSoldier == NULL)
    return NULL;
  return reinterpret_cast<TSoldier *>((reinterpret_cast<DWORD_PTR>(pSoldier) - reinterpret_cast<DWORD_PTR>(pPool)) / sizeof(TSoldier) + 1);
}

static inline TSoldier *SoldierFromIndex(const TSoldierPool *pPool, TSoldier *pSoldier)
{
  if (pSoldier == NULL)
    return NULL;
  return reinterpret_cast<TSoldier *>(reinterpret_cast<DWORD_PTR>(pSoldier) * sizeof(TSoldier) - sizeof(TSoldier) + reinterpret_cast<DWORD_PTR>(pPool));
}

// TSaveNode<TSoldier,1000>::Init, oracle 0x4A9A90 (130 B)
// Lays the whole pool out as one free list. Hyperion: the game's records, 1000 in
// 1.17.
void TSaveNode<TSoldier,1000>::Init()
{
  const int n = g_aiPools.records;
  aSoldiers[0].pPrev = NULL;
  aSoldiers[0].pNext = &aSoldiers[1];
  ClearSoldier(&aSoldiers[0]);
  for (int i = 1; i < n - 1; i++) {
    aSoldiers[i].pPrev = &aSoldiers[i - 1];
    aSoldiers[i].pNext = &aSoldiers[i + 1];
    ClearSoldier(&aSoldiers[i]);
  }
  aSoldiers[n - 1].pPrev = &aSoldiers[n - 2];
  aSoldiers[n - 1].pNext = NULL;
  ClearSoldier(&aSoldiers[n - 1]);
  pFreeHead = &aSoldiers[0];
}

// TSaveNode<TSoldier,1000>::PreSave, oracle 0x4A9F20 (256 B)
// Turns the pool into its save form: wipes the free soldiers and replaces every pointer with an id.
void TSaveNode<TSoldier,1000>::PreSave()
{
  for (TSoldier *p = pFreeHead; p != NULL; p = p->pNext)
    ClearSoldier(p);
  TSoldier *q = aSoldiers;
  for (int i = 0; i < g_aiPools.records; i++) {  // Hyperion: 1000 in 1.17
    q->pUnit = reinterpret_cast<CUnit *>((intptr_t)q->pUnit->GetUnitID());
    // Hyperion: the field by name, where 1.17's code stored dword [4].
    q->pCaptain = reinterpret_cast<TAI_Captain *>((intptr_t)captain_ptr_to_id(q->pCaptain));
    q->pNext = SoldierToIndex(this, q->pNext);
    q->pPrev = SoldierToIndex(this, q->pPrev);
    q++;
  }
  pFreeHead = SoldierToIndex(this, pFreeHead);
}

// TSaveNode<TSoldier,1000>::PostLoad, oracle 0x4A9E40 (213 B)
// The inverse of PreSave, less the wipe; a soldier whose unit is gone loses its captain.
void TSaveNode<TSoldier,1000>::PostLoad()
{
  TSoldier *q = aSoldiers;
  for (int i = 0; i < g_aiPools.records; i++) {  // Hyperion: 1000 in 1.17
    q->pUnit = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(q->pUnit));
    q->pCaptain = q->pUnit != NULL ? captain_id_to_ptr((int)reinterpret_cast<intptr_t>(q->pCaptain)) : NULL;
    q->pNext = SoldierFromIndex(this, q->pNext);
    q->pPrev = SoldierFromIndex(this, q->pPrev);
    q++;
  }
  pFreeHead = SoldierFromIndex(this, pFreeHead);
}

// sai_read_captain_data, oracle 0x4AA020 (280 B)
// Reads the soldier pool and every nation's captains back, then turns each saved id into a pointer.
int sai_read_captain_data(intptr_t nFile)
{
  // Hyperion: the loops run over every nation in a Hyperion game, 8 in 1.17's
  unsigned nAreas;
  if (!decompress(&nAreas, 4, reinterpret_cast<void *>(nFile)))
    return 0;
  captain_free();
  captains_alloc(nAreas);
  // Hyperion: 1.17's pool whole, or a grown pool's records and head
  if (!AiPoolLoad<TSoldier, AI_RECORDS_117>(SAVE117_SoldierPool, SAVE117_Soldier, gSoldiers_g.aSoldiers,
                                            g_aiPools.records, &gSoldiers_g.pFreeHead, reinterpret_cast<void *>(nFile)))
    return 0;
  gSoldiers_g.PostLoad();
  for (int i = PlayerLobbySlots(); i-- != 0;) {
    if (!PortSave117Decompress(SAVE117_Captain, sgCaptain[i], nAreas, reinterpret_cast<void *>(nFile)))
      return 0;
  }
  for (int i = PlayerLobbySlots(); i-- != 0;) {
    for (unsigned j = nAreas; j-- != 0;) {
      TAI_Captain *pCaptain = &sgCaptain[i][j];
      pCaptain->pPool = &gSoldiers_g;
      pCaptain->pSoldiers = soldier_ptr((int)reinterpret_cast<intptr_t>(pCaptain->pSoldiers));
      pCaptain->pUnit1C = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pCaptain->pUnit1C));  // Hyperion: [7]
      pCaptain->pUnit20 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pCaptain->pUnit20));
      pCaptain->pUnit24 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pCaptain->pUnit24));
      pCaptain->pUnit28 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pCaptain->pUnit28));
    }
  }
  CaptainSlotsForget();  // Hyperion
  dword_6B1A94 = 0;
  dword_6B1A98 = 0;
  return 1;
}

// sai_write_captain_data, oracle 0x4AA140 (381 B)
// Writes the soldier pool and every nation's captains with their pointers turned into ids.
int sai_write_captain_data(intptr_t nFile)
{
  unsigned nAreas = NumAreas();
  int nResult = compress(&nAreas, 4, reinterpret_cast<void *>(nFile));
  gSoldiers_g.PreSave();
  // Hyperion: 1.17's pool whole, or a grown pool's records and head
  nResult = AiPoolSave<TSoldier, AI_RECORDS_117>(SAVE117_SoldierPool, SAVE117_Soldier, gSoldiers_g.aSoldiers,
                                                 g_aiPools.records, gSoldiers_g.pFreeHead, reinterpret_cast<void *>(nFile))
                ? nResult : 0;
  gSoldiers_g.PostLoad();
  for (int i = PlayerLobbySlots(); i-- != 0;) {
    for (unsigned j = nAreas; j-- != 0;) {
      TAI_Captain *pCaptain = &sgCaptain[i][j];
      pCaptain->pSoldiers = SoldierToIndex(pCaptain->pPool, pCaptain->pSoldiers);
      pCaptain->pPool = NULL;
      pCaptain->pUnit1C = reinterpret_cast<CUnit *>((intptr_t)pCaptain->pUnit1C->GetUnitID());  // Hyperion: [7]
      pCaptain->pUnit20 = reinterpret_cast<CUnit *>((intptr_t)pCaptain->pUnit20->GetUnitID());
      pCaptain->pUnit24 = reinterpret_cast<CUnit *>((intptr_t)pCaptain->pUnit24->GetUnitID());
      pCaptain->pUnit28 = reinterpret_cast<CUnit *>((intptr_t)pCaptain->pUnit28->GetUnitID());
    }
  }
  for (int i = PlayerLobbySlots(); i-- != 0;)
    nResult = PortSave117Compress(SAVE117_Captain, sgCaptain[i], nAreas, reinterpret_cast<void *>(nFile)) ? nResult : 0;
  for (int i = PlayerLobbySlots(); i-- != 0;) {
    for (unsigned j = nAreas; j-- != 0;) {
      TAI_Captain *pCaptain = &sgCaptain[i][j];
      pCaptain->pPool = &gSoldiers_g;
      pCaptain->pSoldiers = soldier_ptr((int)reinterpret_cast<intptr_t>(pCaptain->pSoldiers));
      pCaptain->pUnit1C = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pCaptain->pUnit1C));
      pCaptain->pUnit20 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pCaptain->pUnit20));
      pCaptain->pUnit24 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pCaptain->pUnit24));
      pCaptain->pUnit28 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pCaptain->pUnit28));
    }
  }
  return nResult;
}

// tai_captain_choose_attack_target, oracle 0x4B1710 (255 B)
// Scores every region as an attack target, picks one and sends the region's captain there.
BOOL tai_captain_choose_attack_target(int nNation, const BYTE *pbStates, int nArg0, int nArg1, int nArg2, int nArg3, int nArg4)
{
  WORD awScore[AI_AREAS_117];
  int anRegion[AI_AREAS_117];
  int anTarget[AI_AREAS_117];
  for (unsigned i = 1; i < AI_AREAS_117; i++)
    awScore[i] = tai_captain_find_staging_region(nNation, i, pbStates, nArg0, nArg1, nArg2, nArg3);
  unsigned nCount = 0;
  for (unsigned i = 1; i < AI_AREAS_117; i++) {
    if (awScore[i] != 0) {
      anRegion[nCount] = i;
      anTarget[nCount] = awScore[i] - 1;
      nCount++;
    }
  }
  if (nCount == 0)
    return 0;
  unsigned nPick;
  if (nCount == 1)
    nPick = 0;
  else if (aiscript_has_campaign(nNation))
    nPick = static_cast<unsigned>(net_rand(0x28)) % nCount;
  else
    nPick = tai_captain_pick_weakest_near_region(nNation, nCount, anRegion);
  const int nTarget = anTarget[nPick];
  banker_set_attack_target(nNation, nTarget, nArg4);
  TAI_Captain *pCaptain = &sgCaptain[nNation][nTarget];
  sai_soldier_set_type(pCaptain, 8);
  pCaptain->wUnknown02 = static_cast<WORD>(anRegion[nPick]);
  return 1;
}

// tai_captain_init, oracle 0x4B0640 (299 B).
// Resets the captain tables, or on a reload grows every nation's captain array to the map's area count.
// The clear goes through the indexed row and the last three stores through a pointer to it,
// which keeps the dead zero stores and holds those three at the end.
void tai_captain_init(int nArg0)
{
  PlayerClear(sgRegions);  // Hyperion: and Hyperion's slots
  PlayerClear(dword_6B68C0);
  if (nArg0 != 0) {
    const DWORD dwBytes = NumAreas() * sizeof(TAI_Captain);
    for (unsigned i = 0; i < static_cast<unsigned>(PlayerLobbySlots()); i++) {  // Hyperion
      TAI_Captain *pNew = static_cast<TAI_Captain *>(SMemAlloc(dwBytes, "TAI_Captain.cpp", 1945, 0));
      TAI_Captain *pOld = sgCaptain[i];
      memcpy(pNew, pOld, dwBytes);
      SMemFree(pOld, "TAI_Captain.cpp", 1948, 0);
      sgCaptain[i] = pNew;
    }
    CaptainSlotsForget();  // Hyperion
    return;
  }
  dword_6B1A94 = 0;
  dword_6B1A98 = 0;
  gSoldiers_g.Init();
  captains_alloc(AI_AREAS_117);
  for (unsigned i = 0; i < static_cast<unsigned>(PlayerLobbySlots()); i++) {  // Hyperion
    for (unsigned j = 0; j < AI_AREAS_117; j++) {
      TAI_Captain *pCaptain = &sgCaptain[i][j];
      sgCaptain[i][j].wRegion = 0;
      sgCaptain[i][j].wUnknown02 = 0;
      sgCaptain[i][j].bNation = 0;
      sgCaptain[i][j].bJob = 0;
      sgCaptain[i][j].bUnknown06 = 0;
      sgCaptain[i][j].bTimer = 0;
      sgCaptain[i][j].bFlags = 0;
      sgCaptain[i][j].bUnknown09 = 0;
      sgCaptain[i][j].wUnknown0A = 0;
      sgCaptain[i][j].wUnknown0C = 0;
      sgCaptain[i][j].wUnknown0E = 0;
      sgCaptain[i][j].wGroundStr10 = 0;
      sgCaptain[i][j].wAirStr12 = 0;
      sgCaptain[i][j].wGroundStr14 = 0;
      sgCaptain[i][j].wAirStr16 = 0;
      sgCaptain[i][j].wUnknown18 = 0;
      sgCaptain[i][j].wUnknown1A = 0;
      sgCaptain[i][j].pUnit1C = NULL;
      sgCaptain[i][j].pUnit20 = NULL;
      sgCaptain[i][j].pUnit24 = NULL;
      sgCaptain[i][j].pUnit28 = NULL;
      sgCaptain[i][j].pPool = &gSoldiers_g;
      sgCaptain[i][j].pSoldiers = NULL;
      pCaptain->wRegion = static_cast<WORD>(j);
      pCaptain->bNation = static_cast<BYTE>(i);
      pCaptain->bJob = 0;
    }
    aiscript_defaultmin(i, 100);
  }
}

// tai_captain_transfer_soldiers, oracle 0x4B15D0 (306 B).
// Moves a captain's soldiers over to another captain, optionally keeping spellcasters and
// cloaked units of computer players, then optionally rallies units near the new region.
// The rally point is declared at function scope, so it gets its own frame slots
// instead of the dead parameter slots.
void tai_captain_transfer_soldiers(TAI_Captain *pCaptain, TAI_Captain *pCaptain2, int nArg2, int nArg3, int nArg4)
{
  int nX, nY;

  if (pCaptain2->pUnit28 == NULL) {
    pCaptain2->pUnit28 = pCaptain->pUnit28;
    pCaptain->pUnit28 = NULL;
    CaptainSlotsMark(pCaptain2);  // Hyperion
  }
  TSoldier *pSoldier = pCaptain->pSoldiers;
  while (pCaptain->pSoldiers != NULL) {
    if (pSoldier == NULL)
      break;
    TSoldier *pNext = pSoldier->pNext;
    CUnit *pUnit = pSoldier->pUnit;
    if (nArg3 == 0 && IsPureSpellcaster_local(pUnit))
      goto next;
    if ((pUnit->dwFlags & 0x20) != 0 && nArg4 != 0 && !aiscript_has_campaign(pUnit->bPlayer))
      goto next;
    {
      TSoldierPool *pPool;
      if ((pSoldier->pUnit->dwFlags & 0x04) == 0)
        pCaptain->wUnknown0A += 0xFFFF;
      if (pCaptain->pSoldiers == pSoldier)
        pCaptain->pSoldiers = pSoldier->pNext;
      if (pSoldier->pPrev != NULL)
        pSoldier->pPrev->pNext = pSoldier->pNext;
      if (pSoldier->pNext != NULL)
        pSoldier->pNext->pPrev = pSoldier->pPrev;
      pPool = pCaptain->pPool;
      pSoldier->pPrev = NULL;
      pSoldier->pNext = pPool->pFreeHead;
      if (pPool->pFreeHead != NULL)
        pPool->pFreeHead->pPrev = pSoldier;
      pPool->pFreeHead = pSoldier;
      pUnit->dwUnion134 = 0;
      tai_soldier_add_to_captain(pCaptain2, pUnit, 1);
    }
  next:
    pSoldier = pNext;
  }
  if (nArg2 != 0) {
    GetRegionCenter(pCaptain2->wRegion, &nX, &nY);
    find_first_unit_near(nX, nY, 0x100, reinterpret_cast<CUnitSearchPredicate>(tai_captain_enlist_guard_cb), reinterpret_cast<CUnit *>(pCaptain2));
  }
}

// aiscript_help_iftrouble, oracle 0x4ACCA0 (335 B)
// Sends every mutually allied nation's idle captain to help wherever this nation has a captain at work.
// Spelled through sgCaptain[i][j] so set_type's inlined bJob test is not folded into this one.
void aiscript_help_iftrouble(int nNation)
{
  const unsigned nAreas = NumAreas();
  for (unsigned i = 0; i < (unsigned)PlayerLobbySlots(); i++) {  // Hyperion
    if (sgbAlliances_g[nNation][i] == 0 || sgbAlliances_g[i][nNation] == 0)
      continue;
    for (unsigned j = 1; j < nAreas; j++) {
      if (sgCaptain[nNation][j].bJob != 0 && sgCaptain[i][j].bJob == 0)
        sai_soldier_set_type(&sgCaptain[i][j], 4);
    }
  }
}

// tai_pick_better_target, oracle 0x4AE080 (402 B)
// Picks the better of two targets for a unit: an interceptor stands in for its carrier, a
// target the unit cannot hit drops out, then armed beats unarmed and nearer beats farther.
CUnit *tai_pick_better_target(CUnit *pUnit, CUnit *pFirst, CUnit *pSecond)
{
  CUnit *pA;
  CUnit *pB;
  if (pFirst == NULL || pUnit == NULL) {
    pA = NULL;
  } else {
    pA = pFirst;
    if (pFirst->wType == 0x49 && pFirst->pHangarParent != NULL &&
        (pUnit->IsUnitInWeaponRange(pFirst->pHangarParent) || (pUnit->dwFlags & 0x20000) != 0))
      pA = pFirst->pHangarParent;
  }
  if (pSecond == NULL || pUnit == NULL) {
    pB = NULL;
  } else {
    pB = pSecond;
    if (pSecond->wType == 0x49 && pSecond->pHangarParent != NULL &&
        (pUnit->IsUnitInWeaponRange(pSecond->pHangarParent) || (pUnit->dwFlags & 0x20000) != 0))
      pB = pSecond->pHangarParent;
  }
  if (pA == NULL || ai_target_unreachable(pUnit, pA) || !pUnit->CanTarget(pA, 1))
    pA = NULL;
  if (pB == NULL || ai_target_unreachable(pUnit, pB) || !pUnit->CanTarget(pB, 1))
    pB = NULL;
  if (pA == NULL)
    return pB;
  if (pB == NULL)
    return pA;
  if (!(pB->wType == 0x7D && pB->IsUnitInside()) && !pB->CanAttack() && pA->CanAttack())
    return pA;
  if (!(pA->wType == 0x7D && pA->IsUnitInside()) && !pA->CanAttack() && pB->CanAttack())
    return pB;
  if (!pUnit->IsAttackable(pA) && pUnit->IsAttackable(pB))
    return pB;
  if (pUnit->IsUnitInWeaponRange(pA)) {
    pB = pA;
  } else if (!pUnit->IsUnitInWeaponRange(pB)) {
    const DWORD nDistA = pUnit->GetDistanceTo_CUnit(pA);
    if (nDistA <= pUnit->GetDistanceTo_CUnit(pB))
      pB = pA;
  }
  return pB;
}

// tai_pick_spider_mine_spot, oracle 0x4AAC20 (412 B)
// Picks a random reachable spot near one of the player's defended regions where the player
// has no spider mine yet.
BOOL tai_pick_spider_mine_spot(CUnit *pUnit, int *pnX, int *pnY)
{
  int anRegions[AI_AREAS_117];
  const int nPlayer = pUnit->bPlayer;
  unsigned nCount = 0;
  const unsigned nAreas = NumAreas();
  for (unsigned i = 1; i < nAreas; i++) {
    if ((sgCaptain[nPlayer][i].bJob == 5 || sgCaptain[nPlayer][i].bJob == 4) && (sgCaptain[nPlayer][i].bFlags & 0x41) != 0)
      anRegions[nCount++] = i;
  }
  if (nCount == 0)
    return 0;
  int nX, nY;
  const int nRegion = anRegions[static_cast<unsigned>(net_rand(0x2C)) % nCount];
  GetRegionCenter(nRegion, &nX, &nY);
  nX = nX - 0x100 + (net_rand(0x2D) & 0x1FF);
  nY = nY - 0x100 + (net_rand(0x2E) & 0x1FF);
  if (nX < 0)
    nX = 0;
  else if (nX >= gwMapPixWidth)
    nX = gwMapPixWidth - 1;
  if (nY < 0)
    nY = 0;
  else if (nY >= gwMapPixHeight)
    nY = gwMapPixHeight - 1;
  if (!UnitCanReachGroundTarget_xy(pUnit, nX, nY))
    return 0;
  if (IsInaccessibleRegion_xy(nX, nY))
    return 0;
  mrect_t  rc;
  rc.top = PosS(nY - 0x60);
  rc.bottom = PosS(nY + 0x60);
  rc.left = PosS(nX - 0x60);
  rc.right = PosS(nX + 0x60);
  CUnit **pp = get_units_in_rect(&rc);
  CUnit **const ppList = pp;
  CUnit *pOther;
  for (; (pOther = *pp) != NULL; pp++) {
    if (pOther->wType == 0x0D && pOther->bPlayer == pUnit->bPlayer)
      break;
  }
  finish_unit_iteration(ppList);
  if (pOther != NULL)
    return 0;
  *pnX = nX;
  *pnY = nY;
  return 1;
}

// True when the soldier's captain holds its area, so losing the unit does not
// demote it.
static inline BOOL soldier_holds(CUnit *pUnit)
{
  TSoldier *pSoldier = reinterpret_cast<TSoldier *>(pUnit->dwUnion134);
  if (pSoldier->bType == 4) {
    BYTE bJob = pSoldier->pCaptain->bJob;
    if (bJob == 1 || bJob == 8 || bJob == 9 || bJob == 2)
      return 1;
  }
  return 0;
}

// Takes a unit out of every captain: clears it from each area's four unit
// slots, credits its threat back to the area it stood in, then releases its
// soldier record, demoting the captain when the unit left its area.
BOOL TAI_Captain_RemoveUnit(CUnit *pUnit, int nFlag)
{
  for (unsigned i = 0; i < (unsigned)PlayerLobbySlots(); i++) {  // Hyperion
    if (gPlayerData[i].nType != 1)
      continue;
    unsigned nAreas = NumAreas();
    // The guard keeps the row load after the area-count test.
    if (nAreas > 1) {
      TAI_Captain *pAreas = sgCaptain[i];
      // Hyperion: the areas of the nation's set alone, once it has one
      if (captain_slots_t *pSlots = CaptainSlots(i)) {
        if (g_bFinderSideCheck)
          CaptainSlotsVerify(i);
        for (size_t k = 0; k < pSlots->areas.size();) {
          TAI_Captain *pCaptain = &pAreas[pSlots->areas[k]];
          if (pCaptain->pUnit1C == pUnit)
            pCaptain->pUnit1C = NULL;
          if (pCaptain->pUnit20 == pUnit)
            pCaptain->pUnit20 = NULL;
          if (pCaptain->pUnit24 == pUnit)
            pCaptain->pUnit24 = NULL;
          if (pCaptain->pUnit28 == pUnit)
            pCaptain->pUnit28 = NULL;
          if (CaptainHolds(pCaptain)) {
            k++;
            continue;
          }
          pSlots->in[pSlots->areas[k]] = 0;
          pSlots->areas[k] = pSlots->areas.back();
          pSlots->areas.pop_back();
        }
      } else {
        for (unsigned j = 1; j < nAreas; j++) {
          TAI_Captain *pCaptain = &pAreas[j];
          if (pCaptain->pUnit1C == pUnit)
            pCaptain->pUnit1C = NULL;
          if (pCaptain->pUnit20 == pUnit)
            pCaptain->pUnit20 = NULL;
          if (pCaptain->pUnit24 == pUnit)
            pCaptain->pUnit24 = NULL;
          if (pCaptain->pUnit28 == pUnit)
            pCaptain->pUnit28 = NULL;
        }
        CaptainSlotsMake(i);
      }
    }
    if (IsEnemyUnit(i, pUnit)) {
      unsigned nRegion = GetRegion_CUnit(pUnit);
      int nType = pUnit->wType;
      unsigned nAir = tai_get_air_threat_base(nType);
      if (nAir >= sgCaptain[i][nRegion].wUnknown0E)
        sgCaptain[i][nRegion].wUnknown0E = 0;
      else
        sgCaptain[i][nRegion].wUnknown0E -= nAir;
      unsigned nGround = tai_get_ground_threat_base(nType);
      // The row base is reloaded into its own register after the call; a
      // pointer to the element instead folds the offset in first.
      TAI_Captain *pRow = sgCaptain[i];
      if (nGround >= pRow[nRegion].wUnknown0C)
        pRow[nRegion].wUnknown0C = 0;
      else
        pRow[nRegion].wUnknown0C -= nGround;
    }
  }
  TSoldier *pSoldier = reinterpret_cast<TSoldier *>(pUnit->dwUnion134);
  if (pSoldier == NULL)
    return 0;
  if (pSoldier->bType != 4)
    return 0;
  TAI_Captain *pCaptain = pSoldier->pCaptain;
  if (pCaptain->bJob == 8) {
    banker_clear_harass_type(pUnit->bPlayer, pUnit->wType);
  } else if (nFlag != 0 && pCaptain->bJob != 0 && !soldier_holds(pUnit)) {
    WORD wRegion = pCaptain->wRegion;
    if (GetRegion_CUnit(pUnit) != wRegion
        && !region_has_worker_or_building(pCaptain->bNation, wRegion))
      sai_soldier_set_type(&sgCaptain[pCaptain->bNation][pCaptain->wRegion], 3);
  }
  tai_captain_unlink_soldier(pCaptain, pSoldier);
  return 1;
}

// tai_captain_plan_all_needs, oracle 0x4AF930 (437 B)
// Tops up each captain's air and ground strength, then orders transports for a Terran or Protoss nation.
void tai_captain_plan_all_needs(int nation)
{
  TAI_Captain *pTransport = NULL;
  const unsigned nAreas = NumAreas();
  unsigned i;
  int nType;

  for (i = 1; i < nAreas; i++) {
    TAI_Captain *pCaptain = &sgCaptain[nation][i];
    const BYTE bJob = pCaptain->bJob;
    if (bJob <= 3)
      continue;
    if (bJob == 8) {
      tai_captain_request_missing_units(pCaptain);
      continue;
    }
    if (bJob == 5 || bJob == 4)
      pTransport = pCaptain;
    TAI_Captain *pTarget =
        &sgCaptain[nation][bJob >= 7 ? pCaptain->wUnknown02 : pCaptain->wRegion];
    unsigned nAir = pCaptain->wAirStr16;
    if (pTarget->wUnknown0E != 0) {
      while (pTarget->wUnknown0E > nAir)
        nAir += tai_captain_raise_defence(pCaptain, pTarget, 1, pTarget->wUnknown0E - nAir);
    }
    unsigned nGround = pCaptain->wGroundStr14;
    if (pTarget->wUnknown0C != 0) {
      while (pTarget->wUnknown0C > nGround)
        nGround += tai_captain_raise_defence(pCaptain, pTarget, pTarget->wUnknown18 != 0,
                              pTarget->wUnknown0C - nGround);
    }
    banker_consume_request_budget(nation);
  }

  if (banker_transports_wanted(nation))
    return;
  const int nRace = gPlayerData[nation].nRace;
  if (nRace == 1) {
    if (!count_get_completion(0x73, nation))
      return;
    nType = 0x0B;
  } else if (nRace == 2) {
    for (i = count_get_existence(0x54, nation); i < 2; i++)
      banker_demand_nuke(nation, 0x54, 0x50);
    nType = 0x45;
  } else {
    return;
  }
  if (pTransport != NULL) {
    const unsigned nHave = count_get_existence(nType, nation);
    const unsigned nWant = aiscript_has_campaign(nation) ? 2 : 5;
    const int nPriority = nHave == 0 ? 0x50 : 0x32;
    for (i = nHave; i < nWant; i++)
      banker_demand_unit_for_captain(nation, reinterpret_cast<intptr_t>(pTransport), nType, nPriority, 1, 1);
  }
}

// tai_send_everything_to_attack, oracle 0x4B11B0 (455 B)
// With bAll set, sends every idle combat unit of the nation to tai_send_unit_at_nearest_enemy; otherwise moves its free soldiers into a fresh attack captain.
void tai_send_everything_to_attack(int nation, BYTE bAll)
{
  CUnit *pUnit;

  if (bAll) {
    for (pUnit = CUnit::GetPlayerUnitList(nation); pUnit != NULL;
         pUnit = pUnit->pPlayerUnitNext) {
      if (pUnit->IsDeadOrDying())
        continue;
      if (gulUnitTypeFlags[pUnit->wType] & 0x10)
        continue;
      const WORD wType = pUnit->wType;
      if (wType == 0x0E || wType == 0x23)
        continue;
      if (pUnit->pSprite->flags.b & 0x20)
        continue;
      if (pUnit->IsAttackable(NULL))
        continue;
      tai_send_unit_at_nearest_enemy(pUnit);
    }
    return;
  }

  if (banker_attack_cooldown(nation))
    return;
  TAI_Captain *pAttack = NULL;
  int nArea = AI_AREAS_117;
  for (pUnit = CUnit::GetPlayerUnitList(nation); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (pUnit->IsDeadOrDying())
      continue;
    if (pUnit->dwUnion134 == 0)
      continue;
    if (pUnit->IsATransport())
      continue;
    if ((pUnit->dwFlags & 0x20) && !aiscript_has_campaign(nation))
      continue;
    TSoldier *pSoldier = reinterpret_cast<TSoldier *>(pUnit->dwUnion134);
    const WORD wRegion = GetRegion_CUnit(pUnit);
    if (pSoldier->bType == 1) {
      pSoldier->pUnit->dwUnion134 = 0;
      tai_soldier_add_to_captain(&sgCaptain[nation][wRegion], pSoldier->pUnit, 1);
      pSoldier->pUnit = NULL;
    } else if (pSoldier->bType != 4) {
      continue;
    }
    const TSoldier *pNow = reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
    if (pNow != NULL && pNow->bType == 4) {
      const BYTE bJob = pNow->pCaptain->bJob;
      if (bJob == 1 || bJob == 8 || bJob == 9 || bJob == 2)
        continue;
    }
    if (nArea == AI_AREAS_117) {
      aiscript_attack_clear(nation, 0);
      const upos_t wY = pUnit->pSprite->wPosY;
      const upos_t wX = pUnit->pSprite->wPosX;
      if (TAI_Captain_aiscript_attack(nation, PosS(wX),
                                      PosS(wY), 0, 1))
        continue;
      banker_set_attack_cooldown(nation);
      nArea = banker_attack_target_area(nation) - 1;
      pAttack = &sgCaptain[nation][nArea];
    }
    SAI_RemoveUnit(pUnit, 0);
    tai_soldier_add_to_captain(pAttack, pUnit, 1);
  }
}

// TAI_Captain_aiscript_attack, oracle 0x4AF300 (397 B)
// Launches the nation's attack toward (nX, nY); returns 0 once tai_captain_choose_attack_target accepts a target area.
BOOL TAI_Captain_aiscript_attack(int nNation, int nX, int nY, int nArg3, int nArg4)
{
  BYTE abStates[AI_AREAS_117];

  if (!banker_attack_not_held(nNation))
    return 1;
  const int nArea = banker_attack_target_area(nNation);
  if (nArea != 0)
    return sgCaptain[nNation][nArea - 1].bJob != 8;

  memset(abStates, 0, sizeof(abStates));
  tai_captain_mark_own_regions(nNation, abStates);
  tai_captain_mark_enemy_regions(nNation, abStates);
  while (tai_captain_attack_map_spread_own(nNation, abStates))
    tai_captain_attack_map_spread_front(abStates);

  WORD *pNeed = aiscript_get_harass_need(nNation);
  BOOL bAir = *pNeed != 0;
  for (; *pNeed != 0; pNeed++) {
    if (!(gulUnitTypeFlags[*pNeed - 1] & 0x04)) {
      bAir = 0;
      break;
    }
  }
  banker_set_attacking(nNation);
  if (tai_captain_choose_attack_target(nNation, abStates, nX, nY, 0, bAir, nArg3) ||
      tai_captain_choose_attack_target(nNation, abStates, nX, nY, 1, bAir, nArg3))
    return 0;
  if (nArg4 != 0 && !bAir) {
    if (tai_captain_choose_attack_target(nNation, abStates, nX, nY, 0, 1, nArg3) ||
        tai_captain_choose_attack_target(nNation, abStates, nX, nY, 1, 1, nArg3))
      return 0;
    banker_hold_attack(nNation);
  }
  return 1;
}

// patrol_pick_area, oracle 0x4AFB20 (529 B)
// Picks a transport area next to the unit's region, or the nearest one, and returns its center.
void patrol_pick_area(CUnit *pUnit, int *pOutX, int *pOutY)
{
  unsigned anReady[16];
  unsigned anClaimed[16];
  unsigned anIdle[16];
  unsigned anIdleClaimed[16];
  const int nNation = pUnit->bPlayer;
  unsigned nRegion = GetRegion_CUnit(pUnit);
  unsigned nClaimed = 0;
  unsigned nIdle = 0;
  unsigned nIdleClaimed = 0;
  unsigned nReady = 0;
  WORD *pNeighbors;
  int n = GetRegionNeighbors(nRegion, &pNeighbors);

  if (n != 0) {
    // Hoisted by hand: the shipped form does not hoist the global's load itself.
    const TAI_Captain *const pCaptains = sgCaptain[nNation];
    while (n != 0) {
      n--;
      const unsigned w = *pNeighbors++;
      const TAI_Captain *pCaptain = &pCaptains[w];
      if (pCaptain->bJob != 5 && pCaptain->bJob != 4)
        continue;
      if (pCaptain->bFlags & 0x01) {
        if (!(pCaptain->bFlags & 0x04)) {
          if (nReady != 16)
            anReady[nReady++] = w;
        } else {
          if (nClaimed != 16)
            anClaimed[nClaimed++] = w;
        }
      } else {
        if (pCaptain->bFlags & 0x04) {
          if (nIdleClaimed != 16)
            anIdleClaimed[nIdleClaimed++] = w;
        } else {
          if (nIdle != 16)
            anIdle[nIdle++] = w;
        }
      }
    }
  }

  if (nReady != 0)
    nRegion = anReady[net_rand(0x37) % nReady];
  else if (nClaimed != 0)
    nRegion = anClaimed[net_rand(0x37) % nClaimed];
  else if (nIdle != 0)
    nRegion = anIdle[net_rand(0x37) % nIdle];
  else if (nIdleClaimed != 0)
    nRegion = anIdleClaimed[net_rand(0x37) % nIdleClaimed];

  unsigned nBest = 99999999;
  const BYTE bJob = sgCaptain[nNation][nRegion].bJob;
  if (bJob != 5 && bJob != 4) {
    const unsigned nAreas = NumAreas();
    for (unsigned i = 1; i < nAreas; i++) {
      if (sgCaptain[nNation][i].bJob == 5 || sgCaptain[nNation][i].bJob == 4) {
        int nX, nY;
        GetRegionCenter(i, &nX, &nY);
        const upos_t wY = pUnit->pSprite->wPosY;
        const upos_t wX = pUnit->pSprite->wPosX;
        const unsigned nDist =
            veclen(nX, PosS(wX), nY, PosS(wY));
        if (nDist < nBest) {
          nBest = nDist;
          nRegion = i;
        }
      }
    }
  }
  GetRegionCenter(nRegion, pOutX, pOutY);
  sgCaptain[nNation][nRegion].bFlags |= 0x04;
}

// tai_captain_job_lost, oracle 0x4ACA00 (445 B).
// Refreshes a captain's needs, then hands its region to the largest free neighbour captain as a job-7 escort.
void tai_captain_job_lost(TAI_Captain *pCaptain)
{
  WORD *pPaths;
  WORD *pCur;
  WORD *p;
  unsigned n;
  unsigned i;

  tai_captain_recount_strength(pCaptain);
  tai_captain_retarget_soldiers(pCaptain);
  if (pCaptain->pUnit1C == NULL && pCaptain->pUnit20 == NULL &&
      !(pCaptain->bFlags & 0x40)) {
    tai_captain_decay_need(&pCaptain->wUnknown0E, !aiscript_has_campaign(pCaptain->bNation));
    tai_captain_decay_need(&pCaptain->wUnknown0C, !aiscript_has_campaign(pCaptain->bNation));
  }
  if (!IsInaccessibleRegion(pCaptain->wRegion))
    n = GetRegionPaths(pCaptain->wRegion, &pPaths);
  else
    n = GetRegionNeighbors(pCaptain->wRegion, &pPaths);

  pCur = pPaths;
  p = pCur;
  for (i = 0; i < n; i++, p++) {
    const TAI_Captain *pOther = &sgCaptain[pCaptain->bNation][*p];
    if (pOther->bJob == 7 && pOther->wUnknown02 == pCaptain->wRegion)
      return;
  }

  TAI_Captain *pSecond = NULL;
  TAI_Captain *pFirst = NULL;
  unsigned nSecond = 0;
  unsigned nFirst = 0;
  for (; n != 0; n--, pCur++) {
    TAI_Captain *pOther = &sgCaptain[pCaptain->bNation][*pCur];
    if (pOther->pUnit1C != NULL || pOther->pUnit20 != NULL)
      continue;
    if (pOther->bJob == 0 && aiscript_has_campaign(pOther->bNation))
      continue;
    if (pOther->bJob != 5 && pOther->bJob != 4 && pOther->bJob != 0)
      continue;
    int nX, nY;
    if (town_first_town_xy(pCaptain->bNation, &nX, &nY) &&
        !sai_route_is_open(pCaptain->bNation, GetRegion_xy(nX, nY), pOther->wRegion))
      continue;
    // A record pointer read at each use keeps the 0x44A00 displacement unfolded.
    const ep_region_t *pRec = &static_cast<ep_glob_t *>(epGlob)->aRegions[*pCur];
    if (pRec->wTiles > nFirst) {
      nFirst = pRec->wTiles;
      pFirst = pOther;
    } else if (pRec->wTiles > nSecond) {
      nSecond = pRec->wTiles;
      pSecond = pOther;
    }
  }
  if (pSecond == NULL)
    pSecond = pFirst;
  if (pSecond != NULL) {
    pSecond->wUnknown02 = pCaptain->wRegion;
    sai_soldier_set_type(pSecond, 7);
  }
}

// The five search depths tai_captain_find_staging_region tries in order: 4, 3, 5, 6, 2.
#ifdef OC_DLL_MODE
#define dword_50DA70  (reinterpret_cast<DWORD *>(OC_VA(0x50DA70u)))
#else
extern DWORD dword_50DA70[5];
#endif

// tai_captain_find_staging_region, oracle 0x4B1810 (541 B).
// Scores the areas reachable from nRegion within five steps and returns the nearest suitable one, plus one.
int tai_captain_find_staging_region(int nNation, int nRegion, const BYTE *pbStates, int nX, int nY,
               int nArg2, int nArg3)
{
  BYTE abDepth[AI_AREAS_117];
  unsigned nLevel;
  unsigned i;

  if (pbStates[nRegion] != 3)
    return 0;
  memset(abDepth, 0, sizeof(abDepth));
  abDepth[nRegion] = 1;
  const unsigned nAreas = NumAreas();
  for (nLevel = 1; nLevel <= 5; nLevel++) {
    for (i = 1; i < nAreas; i++) {
      if (abDepth[i] != nLevel)
        continue;
      WORD *pPaths;
      int n;
      if (nArg3 != 0)
        n = GetRegionNeighbors(i, &pPaths);
      else
        n = GetRegionPaths(i, &pPaths);
      while (n != 0) {
        n--;
        const WORD w = *pPaths++;
        const TAI_Captain *pC = &sgCaptain[nNation][w];
        if ((pC->bJob == 0 || pC->bJob == 5 || pC->bJob == 4) && abDepth[w] == 0 &&
            pbStates[w] == 6)
          abDepth[w] = nLevel + 1;
      }
    }
  }

  unsigned nBestDist = 99999999;
  unsigned nLargest = 0;
  unsigned nMaxTiles = 0;
  unsigned nPick = 0;
  for (unsigned k = 0; k < 5; k++) {
    const unsigned nWant = dword_50DA70[k];
    if (nArg2 == 0 && nWant < 4)
      continue;
    // Its own counter; reusing i puts the row pointer in the SIB index.
    for (unsigned j = 1; j < nAreas; j++) {
      if (sgCaptain[nNation][j].bFlags & 0x20)
        continue;
      if (abDepth[j] != nWant)
        continue;
      const WORD wArea = j;
      const ep_region_t *pRec = &static_cast<ep_glob_t *>(epGlob)->aRegions[wArea];
      if (pRec->wTiles > nMaxTiles) {
        nLargest = j + 1;
        nMaxTiles = pRec->wTiles;
      }
      if (pRec->wTiles < 0x3C)
        continue;
      int nCX, nCY;
      GetRegionCenter(wArea, &nCX, &nCY);
      const unsigned nDist = veclen(nX, nCX, nY, nCY);
      if (nDist < nBestDist) {
        nPick = j + 1;
        nBestDist = nDist;
      }
    }
    if (nPick != 0)
      return nPick;
  }
  return aiscript_has_campaign(nNation) ? nLargest : 0;
}

// tai_call_allies_on_attacker, oracle 0x4AA660 (574 B)
// Calls the attacked unit's nearby allies onto the attacker; a guard only answers if the attacker is inside its weapon range.
void tai_call_allies_on_attacker(CUnit *pUnit, CUnit *pAttacker, int bCaptainsOnly)
{
  int nRadius;
  mrect_t  rc;
  CUnit **ppList;
  CUnit **pp;
  CUnit *pOther;

  if (gulUnitTypeFlags[pUnit->wType] & 0x01)
    nRadius = aiscript_has_campaign(pUnit->bPlayer) ? 0x120 : 0x240;
  else
    nRadius = aiscript_has_campaign(pUnit->bPlayer) ? 0x60 : 0x120;
  rc.top = pUnit->pSprite->wPosY - nRadius;
  rc.bottom = pUnit->pSprite->wPosY + nRadius;
  rc.left = pUnit->pSprite->wPosX - nRadius;
  rc.right = pUnit->pSprite->wPosX + nRadius;

  pp = get_units_in_rect(&rc);
  ppList = pp;
  const BOOL bNoDefense = !banker_all_defensebuild_empty(pUnit->bPlayer);
  const int nPlayer = pUnit->bPlayer;
  for (; (pOther = *pp) != NULL; pp++) {
    if (pOther == pUnit || pOther->bPlayer != nPlayer)
      continue;
    if (gulUnitTypeFlags[pOther->wType] & 0x08) {
      if (!(gulUnitTypeFlags[pAttacker->wType] & 0x08) &&
          (bNoDefense || pAttacker->wType != 0x25))
        continue;
    }
    if (pOther->wType == 0x47)
      continue;
    if (!pOther->CanTarget(pAttacker, 1) &&
        (!pAttacker->CanTarget(pOther, 1) || (pOther->dwFlags & 0x10)))
      continue;
    if (bCaptainsOnly) {
      const TSoldier *pSoldier =
          reinterpret_cast<const TSoldier *>(pOther->dwUnion134);
      if (pSoldier == NULL || pSoldier->bType != 4)
        continue;
      const BYTE bJob = pSoldier->pCaptain->bJob;
      if (bJob != 1 && bJob != 8 && bJob != 9 && bJob != 2)
        continue;
      tai_unit_was_attacked(pOther, pAttacker, 0, 1);
    }
    const TGuard *pGuard = reinterpret_cast<const TGuard *>(pOther->dwUnion134);
    // Two separate calls: MSVC merges them into the one block the oracle has.
    if (pGuard == NULL) {
      tai_unit_was_attacked(pOther, pAttacker, 0, 1);
      continue;
    }
    if (pGuard->bType != 1) {
      tai_unit_was_attacked(pOther, pAttacker, 0, 1);
      continue;
    }
    const upos_t wY = pAttacker->pSprite->wPosY;
    const upos_t wX = pAttacker->pSprite->wPosX;
    const unsigned nDist =
        veclen(PosS(wX), PosS(pGuard->wPosX),
               PosS(wY), PosS(pGuard->wPosY));
    CUnit *pArmed = pOther->subunit;
    if (!pOther->IsTurret(pArmed))
      pArmed = pOther;
    weapon_id_t bWeapon;
    if (pOther->dwFlags & 0x04)
      bWeapon = gubUnitWeaponAir[pArmed->wType];
    else
      bWeapon = pArmed->GetWeaponGndEx();
    if (bWeapon != WEAPON_NONE && nDist <= pArmed->GetWeaponRange(bWeapon) + 0xC0)
      tai_unit_was_attacked(pOther, pAttacker, 0, 1);
  }
  finish_unit_iteration(ppList);
}

// tai_captain_pick_attack_target, oracle 0x4ABA70 (538 B)
// Picks the enemy unit a captain should go after: the nearest reachable one it can hit, or the first enemy seen in an unclaimed region.
CUnit *tai_captain_pick_attack_target(TAI_Captain *pCaptain)
{
  int nAny;
  int nAir;
  int nGround;

  tai_captain_survey_weapons(pCaptain, &nAny, &nGround, &nAir);
  const TSoldier *pSoldiers = pCaptain->pSoldiers;
  if (pSoldiers == NULL)
    return NULL;
  int nX, nY;
  GetRegionCenter(pCaptain->wRegion, &nX, &nY);

  CUnit *pNearFlag2 = NULL;
  CUnit *pNear = NULL;
  CUnit *pFirst = NULL;
  unsigned nNearFlag2 = 9999999;
  unsigned nNear = 9999999;
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
    if (pUnit->IsDeadOrDying())
      continue;
    if (!IsEnemyUnit(pCaptain->bNation, pUnit))
      continue;
    if (sgCaptain[pCaptain->bNation][GetRegion_CUnit(pUnit)].bJob != 0) {
      pFirst = pUnit;
      continue;
    }
    const unsigned nType = pUnit->wType;
    if (nType >= 0xC2 && nType <= 0xC7)
      continue;
    if (pFirst == NULL)
      pFirst = pUnit;
    if (nAny == 0) {
      if (nType == 0x7C || nType == 0xA2 || nType == 0x90 || nType == 0x7D)
        continue;
    } else {
      if (static_cast<ep_glob_t *>(epGlob)->aRegions[GetRegion_CUnit(pUnit)].wGroup == 0x1FFD)
        continue;
      if (!tai_has_transports(pCaptain->bNation)) {
        const upos_t wY = pUnit->pSprite->wPosY;
        const upos_t wX = pUnit->pSprite->wPosX;
        if (!sai_path_can_reach_target(pSoldiers->pUnit, wX, wY))
          continue;
      }
    }
    if (pUnit->dwFlags & 0x04) {
      if (nAir != 0)
        continue;
    } else {
      if (nGround != 0)
        continue;
    }
    const upos_t wY = pUnit->pSprite->wPosY;
    const upos_t wX = pUnit->pSprite->wPosX;
    const unsigned nDist =
        veclen(nX, PosS(wX), nY, PosS(wY));
    if (pUnit->dwFlags & 0x02) {
      if (nDist < nNearFlag2) {
        nNearFlag2 = nDist;
        pNearFlag2 = pUnit;
      }
    } else if (nDist < nNear) {
      nNear = nDist;
      pNear = pUnit;
    }
  }
  if (pNearFlag2 != NULL)
    return pNearFlag2;
  if (pNear != NULL)
    return pNear;
  if (nAny != 0 && pFirst != NULL) {
    if (static_cast<ep_glob_t *>(epGlob)->aRegions[GetRegion_CUnit(pFirst)].wGroup == 0x1FFD)
      return NULL;
    if (!tai_has_transports(pCaptain->bNation)) {
      const upos_t wY = pFirst->pSprite->wPosY;
      const upos_t wX = pFirst->pSprite->wPosX;
      if (!sai_path_can_reach_target(pSoldiers->pUnit, wX, wY))
        return NULL;
    }
  }
  return pFirst;
}

// A tech's energy cost in the x256 units NeedsMoreEnergy compares. Returning
// int keeps the oracle's full movzx load; written inline, MSVC narrows it.
static __forceinline int tech_energy_cost(int nTech) {
  return techdata_energy_g[nTech] << 8;
}

// tai_call_support_units, oracle 0x4AE3E0 (672 B).
// Answers an attack on an AI unit: sends the captain's +0x28 unit at it, or a nearby detector, medic or caster.
void __fastcall tai_call_support_units(CUnit *pSource, CUnit *pTarget)
{
  if (pSource->dwUnion134 == 0)
    return;
  banker_set_attacking(pSource->bPlayer);
  const TSoldier *pSoldier = reinterpret_cast<const TSoldier *>(pSource->dwUnion134);
  TAI_Captain *pCaptain = NULL;
  if (pSoldier->bType == 4) {
    pCaptain = pSoldier->pCaptain;
  } else if (pSoldier->bType == 1) {
    const TGuard *pGuard = reinterpret_cast<const TGuard *>(pSoldier);
    const BYTE bPlayer = pSource->bPlayer;
    pCaptain = &sgCaptain[bPlayer][GetRegion_xy(PosS(pGuard->wHomeX),
                                                PosS(pGuard->wHomeY))];
  }
  if (pCaptain != NULL && pCaptain->pUnit28 != NULL) {
    pCaptain->pUnit28->SetOrder_CUnit(0x9F, pSource);
    return;
  }

  CUnit *pNearDetector = NULL;
  unsigned nNearDetector = 9999999;
  CUnit *pNearCaster = NULL;
  unsigned nNearCaster = 9999999;
  for (CUnit *pUnit = CUnit::GetPlayerUnitList(pSource->bPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (pUnit->IsDeadOrDying())
      continue;
    switch (pUnit->wType) {
    case 0x6B:
      if (!(pUnit->dwFlags & 0x01))
        break;
      if (!(pSource->wType == 0x7D && pSource->IsUnitInside())) {
        if (!pSource->CanAttack())
          break;
        if (gulUnitTypeFlags[pSource->wType] & 0x08)
          break;
      }
      if (pUnit->NeedsMoreEnergy(tech_energy_cost(4)))
        break;
      if (pUnit->bOrderId != 0x8B) {
        pUnit->SetOrder_CUnit(0x8B, pTarget);
        return;
      }
      break;
    case 0x2A:
      if (pUnit->IsUnitInside())
        break;
    case 0x09:
    case 0x54: {
      if (UnitMyopia(pUnit).Any())  // Hyperion: bUnion123, and the blinders past 7 (stage D)
        break;
      const int nOrder = pUnit->bOrderId;
      if (nOrder != 0x9C && nOrder != 0xA3 && nOrder != 0xA0 && nOrder != 0x7A &&
          nOrder != 0x9F)
        break;
      CSprite *pUS = pUnit->pSprite;
      const upos_t wUY = pUS->wPosY;
      const upos_t wSY = pSource->pSprite->wPosY;
      const upos_t wSX = pSource->pSprite->wPosX;
      const upos_t wUX = pUS->wPosX;
      const unsigned nDist =
          veclen(PosS(wSX), PosS(wUX),
                 PosS(wSY), PosS(wUY));
      if (nDist < nNearDetector) {
        nNearDetector = nDist;
        pNearDetector = pUnit;
      }
      break;
    }
    case 0x2D: {
      if (!pSource->CanTarget(pTarget, 1))
        break;
      if (pTarget->wType == 0x67)
        break;
      if (pUnit->NeedsMoreEnergy(get_order_tech_energy_cost(0x92)))
        break;
      if (TechUseAllowed(0x11, pUnit->bPlayer, pUnit) != 1)
        break;
      CSprite *pUS = pUnit->pSprite;
      const upos_t wUY = pUS->wPosY;
      const upos_t wSY = pSource->pSprite->wPosY;
      const upos_t wSX = pSource->pSprite->wPosX;
      const upos_t wUX = pUS->wPosX;
      const unsigned nDist =
          veclen(PosS(wSX), PosS(wUX),
                 PosS(wSY), PosS(wUY));
      if (nDist < nNearCaster) {
        pNearCaster = pUnit;
        nNearCaster = nDist;
      }
      break;
    }
    }
  }
  if (pNearDetector != NULL)
    pNearDetector->SetOrder_xy(0xA3, PosS(pSource->pSprite->wPosX),
                               pSource->pSprite->wPosY);
  if (pNearCaster != NULL)
    pNearCaster->SetOrder_xy(0x92, PosS(pTarget->pSprite->wPosX),
                             pTarget->pSprite->wPosY);
}

// TAI_Captain_aiscript_should_rush, oracle 0x4AD550 (1041 B)
// Finds the nearest enemy with buildings and tests whether its army is small enough for rush plan nCase.
BOOL TAI_Captain_aiscript_should_rush(int nNation, int nCase, int nX, int nY)
{
  if (TAI_Captain_aiscript_attack(nNation, nX, nY, 0, 0))
    return 0;
  const int nArea = banker_attack_target_area(nNation) - 1;
  GetRegionCenter(sgCaptain[nNation][nArea].wUnknown02, &nX, &nY);

  unsigned nBest = 99999999;
  int nEnemy = -1;
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
    if (pUnit->IsDeadOrDying())
      continue;
    if (!(gulUnitTypeFlags[pUnit->wType] & 0x01))
      continue;
    if (!IsEnemyUnit(nNation, pUnit))
      continue;
    const upos_t wY = pUnit->pSprite->wPosY;
    const upos_t wX = pUnit->pSprite->wPosX;
    const unsigned nDist =
        veclen(PosS(wX), nX, PosS(wY), nY);
    if (nDist <= 0x140 && nEnemy == pUnit->bPlayer) {
      nEnemy = pUnit->bPlayer;
      break;
    }
    if (nDist <= nBest) {
      nEnemy = pUnit->bPlayer;
      nBest = nDist;
    }
  }
  if (nEnemy == -1)
    return 0;
  aiscript_attack_clear(nNation, 1);

  switch (nCase) {
  case 0:
    if (!count_get_existence(0x6F, nEnemy) && !count_get_existence(0x8E, nEnemy) &&
        !count_get_existence(0xA0, nEnemy))
      return 1;
    break;
  case 1:
    if (tai_count_marine_defence(nEnemy) <= 16 && tai_count_zerg_ground_defence(nEnemy) <= 10 &&
        count_get_completion(0x41, nEnemy) <= 6)
      return 1;
    break;
  case 2:
    if (tai_count_marine_defence(nEnemy) <= 24 && tai_count_zerg_air_defence(nEnemy) <= 10)
      return 1;
    break;
  case 12:
    if (tai_count_marine_defence(nEnemy) <= 16 && tai_count_zerg_air_defence(nEnemy) <= 5 &&
        count_get_completion(0x46, nEnemy) + count_get_completion(0x42, nEnemy) <= 2)
      return 1;
    break;
  case 13:
    if (tai_count_marine_defence(nEnemy) <= 24 && tai_count_zerg_air_defence(nEnemy) <= 10 &&
        count_get_completion(0x46, nEnemy) + count_get_completion(0x42, nEnemy) <= 7)
      return 1;
    break;
  case 3:
    if (tai_count_marine_defence(nEnemy) <= 5 && tai_count_zerg_ground_defence(nEnemy) <= 2 &&
        !count_get_completion(0x87, nEnemy) && count_get_completion(0x41, nEnemy) <= 1)
      return 1;
    break;
  case 8:
    if (tai_count_marine_defence(nEnemy) <= 5 && tai_count_zerg_ground_defence(nEnemy) <= 2 &&
        count_get_completion(0x41, nEnemy) <= 1)
      return 1;
    break;
  case 9:
    if (tai_count_marine_defence(nEnemy) <= 9 && tai_count_zerg_ground_defence(nEnemy) <= 4 &&
        count_get_completion(0x41, nEnemy) <= 5)
      return 1;
    break;
  case 4:
    if (tai_count_marine_defence(nEnemy) <= 16 && tai_count_zerg_ground_defence(nEnemy) <= 10 &&
        count_get_completion(0x41, nEnemy) <= 8)
      return 1;
    break;
  case 10:
    if (tai_count_marine_defence(nEnemy) <= 4 && tai_count_zerg_ground_defence(nEnemy) <= 4 &&
        count_get_completion(0x41, nEnemy) <= 2)
      return 1;
    break;
  case 5:
    if (tai_count_marine_defence(nEnemy) <= 6 && tai_count_zerg_ground_defence(nEnemy) <= 6 &&
        count_get_completion(0x41, nEnemy) <= 3)
      return 1;
    break;
  case 11:
    if (tai_count_marine_defence(nEnemy) <= 10 && tai_count_zerg_ground_defence(nEnemy) <= 10 &&
        count_get_completion(0x41, nEnemy) <= 5)
      return 1;
    break;
  case 6:
    if (tai_count_marine_defence(nEnemy) <= 12 && count_get_completion(0x92, nEnemy) <= 1 &&
        count_get_completion(0x42, nEnemy) <= 1)
      return 1;
    break;
  case 7:
    if (!count_get_completion(0x05, nEnemy) && !count_get_completion(0x2D, nEnemy) &&
        count_get_completion(0x41, nEnemy) <= 6)
      return 1;
    break;
  }
  return 0;
}

// tai_review_allied_strength, oracle 0x4ACDF0 (1074 B).
// Once the game is six minutes old and no player is human-controlled, sums each
// player's ground strength, the strength allied to and hostile to each player,
// and the strongest hostile alliance, then lets each computer player decide to
// defend or attack. The counters are unsigned (`jb` on every loop) and the four
// arrays are cleared with memset, which is what aligns the frame.
void tai_review_allied_strength()
{
  DWORD dwOwn[PLAYER_SLOTS_MAX];  // Hyperion: 8 in 1.17, and the loops below
  DWORD dwFoe[PLAYER_SLOTS_MAX];
  DWORD dwThreat[PLAYER_SLOTS_MAX];
  DWORD dwAllied[PLAYER_SLOTS_MAX];
  if (g_dwElapsedTime < 360)
    return;
  for (DWORD i = 0; i < (DWORD)PlayerLobbySlots(); i++)
    if (aiscript_has_campaign(i))
      return;
  memset(dwOwn, 0, sizeof(dwOwn));
  memset(dwAllied, 0, sizeof(dwAllied));
  memset(dwThreat, 0, sizeof(dwThreat));
  memset(dwFoe, 0, sizeof(dwFoe));
  // Hyperion: each player's strength summed first, then handed to the players it is
  // allied to or a threat to. 1.17 walked the players for each unit, which at 64
  // slots and 12,000 units is a million and a half steps a review; the sums are the same, as
  // unsigned addition is the same in any order.
  DWORD dwGround[PLAYER_SLOTS_MAX];
  memset(dwGround, 0, sizeof(dwGround));
  const DWORD nSlots = (DWORD)PlayerLobbySlots();
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit; pUnit = pUnit->pNext) {
    if (pUnit->IsDeadOrDying())
      continue;
    DWORD nPlayer = pUnit->bPlayer;
    if (nPlayer >= nSlots)
      continue;
    // +0x13A is the ground strength CUnit::GroundStrength returns.
    DWORD dwStrength = *reinterpret_cast<WORD *>(&pUnit->_unmapped_138[0x02]);
    dwOwn[nPlayer] += dwStrength;
    if (!(gulUnitTypeFlags[pUnit->wType] & 9))
      dwGround[nPlayer] += dwStrength;
  }
  for (DWORD nPlayer = 0; nPlayer < nSlots; nPlayer++) {
    for (DWORD j = 0; j < nSlots; j++)
      if (j == nPlayer || sgbAlliances_g[nPlayer][j] == 2)
        dwAllied[j] += dwGround[nPlayer];
    for (DWORD j = 0; j < nSlots; j++)
      if (!sgbAlliances_g[j][nPlayer] || !sgbAlliances_g[nPlayer][j])
        dwThreat[j] += dwOwn[nPlayer];
  }
  for (DWORD k = 0; k < (DWORD)PlayerLobbySlots(); k++)
    for (DWORD m = 0; m < (DWORD)PlayerLobbySlots(); m++)
      if (!sgbAlliances_g[k][m])
        dwFoe[m] = dwAllied[k] > dwFoe[m] ? dwAllied[k] : dwFoe[m];
  for (DWORD i = 0; i < (DWORD)PlayerLobbySlots(); i++) {
    if (gPlayerData[i].nType != 1)
      continue;
    banker_set_is_ahead(i, dwOwn[i] > dwFoe[i]);
    if ((dwAllied[i] >= 6000 || g_dwElapsedTime >= 2700) && dwThreat[i] &&
        dwAllied[i] > dwThreat[i] * 2) {
      tai_send_everything_to_attack(i, 0);
      aiscript_quick_attack(i, 0);
    }
  }
}

// tai_unit_was_attacked, oracle 0x4AEBC0 (1632 B)
// Reacts to an AI unit being hit: flags its region's captain, may cancel a
// build or pull the unit back, and turns it on the attacker.
// Both captain pointers read bPlayer into a BYTE local before GetRegion_CUnit,
// and the recomputed flag goes through its own temporary (the cmov).
// guard_unit_is_soldier must stay out of this TU: with its body visible MSVC keeps
// pCaptain across the call where the oracle reloads it.
void __fastcall tai_unit_was_attacked(CUnit *pUnit, CUnit *pAttacker, int a3, int a4)
{
  CUnit *pTarget;
  const TSoldier *pSoldier;
  TAI_Captain *pCapA;
  TAI_Captain *pCapB;
  int bBusy;
  int bFlag;
  BOOL bUnint;
  unsigned nOrder;
  int nType;
  order_target_t t;

  if (gPlayerData[pUnit->bPlayer].nType != 1 || pUnit->bPlayer >= PlayerLobbySlots())  // Hyperion
    return;
  if ((gulUnitTypeFlags[pUnit->wType] & 1) != 0 && pUnit->dwUnion134 != 0) {
    banker_stamp_request(pUnit->bPlayer);
    town_hold_workers(pUnit);
    if (a3 && pAttacker != NULL
        && (gulUnitTypeFlags[pAttacker->wType] & 8) == 0
        && !pUnit->IsDeadOrDying() && (pUnit->dwFlags & 1) == 0
        && pUnit->GetToughness() < pUnit->MaxToughness() / 4) {
      pUnit->CancelBuild();
      return;
    }
  }

  pTarget = tai_resolve_carrier_target(pUnit, pAttacker);
  if ((pTarget->dwFlags & 0x20) != 0) {
    pTarget = pTarget->pUnion80;
    if (pTarget == NULL)
      return;
  }
  if ((pTarget->dwFlags & 0x800) != 0 && (pTarget->dwFlags & 0x10) == 0) {
    pTarget = ai_find_nearest_enemy_type(pUnit, 0x47);
    if (pTarget == NULL)
      return;
  }
  if (a3) {
    tai_cloak_if_allowed(pUnit);
    if (pUnit->wType == 0x67 && (pUnit->dwFlags & 0x10) == 0
        && pUnit->bOrderId != 0x74)
      pUnit->SetOrder(0x74);
  }

  bBusy = 0;
  bFlag = 0;
  pSoldier = reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
  if (pSoldier != NULL) {
    if (pSoldier->bType == 4) {
      if (pSoldier->pCaptain->bJob == 1 && guard_unit_is_soldier(pUnit))
        sai_soldier_set_type(pSoldier->pCaptain, 2);
      bFlag = tai_soldier_captain_is_attacking(pUnit, 0);
    }
    if (a3 && pUnit->IsATransport() && (pUnit->dwFlags & 0x20000) != 0) {
      if (pUnit->IsUnitInside()) {
        if (pUnit->bOrderId != 0x6F)
          pUnit->SetOrder(0x6F);
        return;
      }
      if (!guard_return_to_post(pUnit, 0) && AIMoveWhereNeeded(pUnit))
        return;
      if (aiscript_has_campaign(pUnit->bPlayer))
        bBusy = 1;
    }
  }

  {
    const BYTE bNation = pUnit->bPlayer;

    pCapA = &sgCaptain[bNation][GetRegion_CUnit(pUnit)];
  }
  if (!bBusy) {
    if (!a4)
      tai_call_allies_on_attacker(pUnit, pTarget, bFlag);
    if (!bFlag) {
      pSoldier = reinterpret_cast<const TSoldier *>(pUnit->dwUnion134);
      if (pSoldier == NULL || tai_soldier_captain_is_attacking(pUnit, 0)) {
        bFlag = 0;
      } else {
        int bRet = 1;

        if (pSoldier->bType == 1 && aiscript_has_campaign(pUnit->bPlayer))
          bRet = 0;
        bFlag = bRet;
      }
      {
        const BYTE bNation = pUnit->bPlayer;

        pCapB = &sgCaptain[bNation][GetRegion_CUnit(pTarget)];
      }
      if (bFlag) {
        if (pCapA->bJob == 5 || pCapA->bJob == 4)
          sai_soldier_set_type(pCapA, 6);
        if (pCapA->bJob != 0 && pCapB->bJob == 0 && g_dwElapsedTime > 60)
          sai_soldier_set_type(pCapB, 5);
      }
      if (!a4) {
        pCapB->wUnknown18 =
            tai_get_total_ground_threat_region(pUnit->bPlayer, pCapB->wRegion);
        pCapB->wUnknown1A =
            tai_get_total_air_threat_region(pUnit->bPlayer, pCapB->wRegion);
        if (pCapB->wUnknown0E > pCapA->wUnknown0E)
          pCapA->wUnknown0E = pCapB->wUnknown0E;
        if (pCapB->wUnknown0C > pCapA->wUnknown0C)
          pCapA->wUnknown0C = pCapB->wUnknown0C;
      }
    }
  }
  pCapA->bFlags |= 0x20;

  if ((pUnit->dwFlags & 1) == 0)
    return;
  nOrder = pUnit->bOrderId;
  if (nOrder == 0x6A || nOrder == 0x96 || nOrder == 0x21)
    return;
  if (!a3 && IsAiUsableSpellOrder(nOrder))
    return;
  nType = pUnit->wType;
  if (nType == 0 && nOrder == 0x5C)
    return;
  bUnint = pUnit->IsUninterruptable();
  if (!bUnint && ai_update_unit_spells(pUnit, a3))
    return;
  if (nOrder == 0x42 || nOrder == 6)
    return;

  if (a3) {
    if (nType == 0x48
        && static_cast<BYTE>(pUnit->bUpgradeType + pUnit->bTechType) <= 2
        && AIMoveWhereNeeded(pUnit))
      return;
    if ((pTarget == NULL || (pTarget->dwFlags & 0x20000) == 0)
        && !aiscript_has_campaign(pUnit->bPlayer)) {
      if (pUnit->GetToughness() <= pUnit->MaxToughness() * 3 / 4
          && (nType == 0x47 || nType == 8 || nType == 0x2B)
          && AIMoveWhereNeeded(pUnit))
        return;
      if (pUnit->GetToughness() <= pUnit->MaxToughness() * 2 / 3
          && (nType == 0x0C || nType == 0x48 || nType == 0x46)
          && AIMoveWhereNeeded(pUnit))
        return;
    }
    if (pUnit->wType == 0x67 && (pUnit->dwFlags & 0x10) == 0
        && nOrder != 0x74 && nOrder != 0x76 && tai_reposition_for_attack(pUnit, pTarget))
      return;
    if (!bUnint && (pUnit->dwFlags & 0x20000) != 0) {
      upos_t y = pTarget->pSprite->wPosY;
      upos_t x = pTarget->pSprite->wPosX;

      if (!sai_path_can_reach_target(pUnit, x, y) && tai_reposition_for_attack(pUnit, pTarget))
        return;
    }
  }

  if (pUnit->pOrderTarget == pTarget)
    return;
  if ((gulUnitTypeFlags[pUnit->wType] & 8) != 0
      && (gulUnitTypeFlags[pTarget->wType] & 8) == 0
      && pTarget->wType != 0x25
      && !banker_all_defensebuild_empty(pUnit->bPlayer)) {
    if (!a3)
      return;
    tai_reposition_for_attack(pUnit, pTarget);
    return;
  }
  if (a3 && pUnit->dwUnion134 != 0 && pTarget->IsVisibleTo(pUnit))
    tai_call_support_units(pUnit, pTarget);
  if (bUnint)
    return;
  if (!pUnit->CanTarget(pTarget, 1)) {
    if (pUnit->dwUnion134 == 0)
      return;
    if (nOrder == 0x9F) {
      const CSprite *pSprite;

      t = ai_get_flee_target(pTarget, pUnit);
      pSprite = pUnit->pSprite;
      if (t.pt.x != PosS(pSprite->wPosX)
          || t.pt.y != pSprite->wPosY) {
        pUnit->SetOrder_Target(6, t);
        return;
      }
    }
    if (!a3 || pUnit->wType == 0x22)
      return;
    // The oracle asks the order target whether it can target itself.
    if (pUnit->pOrderTarget != NULL
        && pUnit->pOrderTarget->CanTarget(pUnit->pOrderTarget, 1))
      return;
    tai_reposition_for_attack(pUnit, pTarget);
    return;
  }
  {
    CUnit *pSub = pUnit->subunit;

    pUnit->dwUnion7C = reinterpret_cast<DWORD_PTR>(pTarget);
    if (pUnit->IsTurret(pSub))
      pSub->dwUnion7C = reinterpret_cast<DWORD_PTR>(pTarget);
    pUnit->AIPickAttackTarget(0, 1, 0);
  }
}
