// CUnitSearches.h -- the free functions of CUnitSearches.cpp's oracle TU: the
// unit-search predicates, the AI spell-target dispatchers and their helpers, and
// the unported callees those bodies reach. The TU's CUnit members are declared
// with their class, in CUnit.h.
#pragma once

#include "platform.h"   // ours: BOOL/BYTE/WORD/DWORD
#include "rect16.h"     // ours: rect16_t -- sub_432070's search area
#include "point16.h"
#include "pos.h"       // Hyperion: upos_t

// CUnit is only ever named through a pointer here, so a forward declaration is
// enough and keeps this header out of CUnit.h's own include closure. The `class`
// tag, not `struct`, matches CUnit.h's definition; a `struct` spelling would be a
// C4099 tag mismatch.
class CUnit;

// The one shape every generic unit search in this block takes its per-candidate
// filter in -- CUnit::FindNearestInRect, CUnit::sub_442BC0, find_first_unit_near
// and sub_442CC0 all dispatch it: two register arguments, nothing pushed, a bare
// `ret`, EAX defined at full width at every exit. The typedef states that
// contract once. A few predicates below take a different second argument and
// deliberately do not use it.
typedef BOOL (__fastcall *CUnitSearchPredicate)(CUnit *pCandidate, CUnit *pSource);

// The unported callees the bodies below reach, each with the shape its own oracle
// body and this TU's call sites give it.

// oracle 0x4143E0, 20 B, oracle TU count.cpp. One register argument plus the
// player byte in DL, nothing pushed. Spelled exactly as src117/h/SAI_Banker.h
// declares it -- `unsigned` return, these parameter names -- so a TU that sees
// both headers gets two identical declarations rather than a C2371.
unsigned __fastcall count_get_completion(int unitType, BYTE nation);

// oracle 0x432420, 236 B, this TU's own block, unported. ECX is a CUnit* used as
// the `this` of CUnit::IsMineralSrc and EDX is a second CUnit*; two register
// arguments, and sub_432510 truth-tests the result, so BOOL.
BOOL __fastcall sub_432420(CUnit *pUnit, CUnit *pHarvester);

// oracle 0x432620, 41 B, this TU's own block, unported. Address-taken only:
// sub_432BD0 pushes it as CUnit::sub_442BC0's filter, so only its type matters,
// and that type is the search-predicate family's.
BOOL __fastcall sub_432620(CUnit *pCandidate, CUnit *pSource);

// oracle 0x4327E0, 131 B, this TU's own block, unported. Address-taken only:
// AIFindCureTarget pushes it as CUnit::sub_442BC0's filter.
BOOL __fastcall ai_test_best_cure_target(CUnit *pCandidate, CUnit *pSource);

// oracle 0x432E70, 63 B, this TU's own block, unported. Address-taken only:
// CUnit::sub_432BB0 pushes it as CUnit::FindNearestInRect's filter.
BOOL __fastcall sub_432E70(CUnit *pCandidate, CUnit *pSource);

// oracle 0x48E980, 37 B, oracle TU sai_path.cpp, unported. A `this`-shaped unit
// in ECX, one more register argument in EDX, and one stack dword retired by
// `ret 4h`. x and y are WORD, not short, and the call site is what shows it:
// SearchResourceDepot zero-extends both to 32 bits before handing them over,
// where a `short` parameter would let MSVC narrow the load to a bare 16-bit
// `mov`. SearchResourceDepot truth-tests the result, so BOOL.
BOOL __fastcall sai_path_can_reach_target(CUnit *pUnit, upos_t x, upos_t y);

// The free functions defined in src117/lang/CUnitSearches.cpp, in oracle address
// order, which is also their order in that file. Only what a caller needs is
// repeated here.

// oracle 0x431980 (72 B). Search predicate: five "is this candidate a valid
// powerup pickup" guards sharing one failure epilogue, then a same-player check
// against the searching unit.
BOOL __fastcall sub_431980(CUnit *pUnit, CUnit *pOther);

// sub_432070 (0x432070, 27 B) is a CUnit member now; its declaration is in
// CUnit.h. It is __stdcall, not fastcall: both operands come off the stack at
// [ebp+8] and [ebp+0Ch] and `ret 8h` retires those two dword slots. Its
// unit-type slot is a full dword rather than a WORD; the narrowing to 16 bits
// happens later, where the value is handed to find_foreach_unit as its context
// word.

// oracle 0x4323D0 (59 B). Search predicate: a resource-return destination owned
// by the harvester's player that the harvester can actually path to.
BOOL __fastcall SearchResourceDepot(CUnit *pUnit, CUnit *pHarvester);

// oracle 0x432410, 10 B. The find_foreach_unit callback sub_432070 hands out;
// the whole body is `xor eax,eax / cmp word ptr [ecx+64h], dx / setz al / ret`.
// The second parameter is declared at its true width, the WORD type id riding in
// DX, rather than at FindUnitCb's generic `void *`, which is what compiles to the
// oracle's ten bytes; sub_432070's call site bridges the two with a
// reinterpret_cast.
BOOL __fastcall test_has_type_in_rect(CUnit *pUnit, WORD wType);

// oracle 0x432510, 70 B. Search predicate, itself registered as one by
// CUnit::FindFreeMineral, which pushes this address as a generic dispatcher's
// filter: it carries no state of its own and reads its configuration out of
// dword_665D18.
BOOL __fastcall sub_432510(CUnit *pUnit, CUnit *pHarvester);

// oracle 0x432A60 (88 B). Search predicate: the attack-target filter -- not
// itself, an enemy, visible if flagged, targetable, and accepted by
// CUnit::IsUnitInWeaponRange.
BOOL __fastcall sub_432A60(CUnit *pTarget, CUnit *pUnit);

// oracle 0x432BD0, 17 B. A free function although it reads like a member: it
// forwards its one incoming CUnit* to CUnit::sub_442BC0 as both the `this` and
// the trailing context argument, pushing the pointer and leaving it in ECX. A
// member spelling would pass `this` once and could not produce the push.
CUnit *__fastcall sub_432BD0(CUnit *pUnit);

// oracle 0x432C30 (24 B). Search predicate: the candidate is a Bunker (unit type
// 0x7D) owned by the same player as the unit that wants to enter it.
BOOL __fastcall ai_test_bunker_target(CUnit *pBunker, CUnit *pUnit);

// oracle 0x432C50, 17 B. The same free-function shape as sub_432BD0 above, with a
// different filter and radius. The body never reads EDX; the parameter is
// declared anyway because every caller in this family sets it, and dropping it
// would change the ABI this symbol advertises. The one caller,
// ai_update_unit_spells, loads its int nMode into EDX, so that is the flag.
CUnit *__fastcall AIFindCureTarget(CUnit *pUnit, int nFlags);

// The AI spell-target dispatchers, oracle 0x432BF0 to 0x433240. One family, one
// signature -- `CUnit *__fastcall(CUnit *pUnit, int nFlags)`, the unit in ECX and
// a mode flag in EDX -- because the single caller, ai_update_unit_spells,
// dispatches every one of them the same way; the bodies that never read EDX still
// declare it. The flag is a dword, by the `mov dword ptr [0x665D30], edx` stores,
// but its signedness and meaning are not derived.

// oracle 0x4831E0, 17 B, oracle TU SAI_Banker.cpp, unported. One register
// argument in ECX, a player index scaled by the 0x4E8 AI-player stride, and a
// nonzero-is-true result. The `int` return is what the callers need: each feeds
// it into a two-constant ternary, and VS2013 /Ox emits the oracle's
// `neg/sbb/and/add` mask form only for an arbitrary nonzero int, where a `bool`
// would already be 0/1.
int __fastcall aiscript_has_campaign(int nPlayer);

// The address-taken predicates, in address order. Each dispatcher pushes one of
// these as its filter, so only the type matters; none of them is ported. They all
// carry the search-predicate family's shape, which CUnitSearchPredicate above
// states.

BOOL __fastcall ai_test_best_broodling_target(CUnit *pCandidate, CUnit *pSource);   // 0x432650
BOOL __fastcall ai_test_best_disruptor_target(CUnit *pCandidate, CUnit *pSource);   // 0x4326E0
BOOL __fastcall sub_432870(CUnit *pCandidate, CUnit *pSource);                      // 0x432870
BOOL __fastcall ai_test_best_matrix_target(CUnit *pCandidate, CUnit *pSource);      // 0x4328D0
BOOL __fastcall ai_test_best_energy_target(CUnit *pCandidate, CUnit *pSource);      // 0x432950
BOOL __fastcall ai_test_best_shields_target(CUnit *pCandidate, CUnit *pSource);     // 0x4329E0
BOOL __fastcall ai_test_best_ensnare_target(CUnit *pCandidate, CUnit *pSource);     // 0x432AC0
BOOL __fastcall ai_test_best_hallucinate_target(CUnit *pCandidate, CUnit *pSource); // 0x433260
BOOL __fastcall ai_test_best_irradiate_target(CUnit *pCandidate, CUnit *pSource);   // 0x4332F0
BOOL __fastcall ai_test_best_magnapulse_target(CUnit *pCandidate, CUnit *pSource);  // 0x433360
BOOL __fastcall ai_test_best_myopia_target(CUnit *pCandidate, CUnit *pSource);      // 0x433550
BOOL __fastcall ai_test_best_nuke_target(CUnit *pCandidate, CUnit *pSource);        // 0x4335B0
BOOL __fastcall ai_test_best_paralyze_target(CUnit *pCandidate, CUnit *pSource);    // 0x4336B0
BOOL __fastcall ai_test_best_parasite_cure_target(CUnit *pCandidate, CUnit *pSource);                      // 0x433840
BOOL __fastcall ai_test_best_parasite_target(CUnit *pCandidate, CUnit *pSource);    // 0x433870
BOOL __fastcall ai_test_best_plague_target(CUnit *pCandidate, CUnit *pSource);      // 0x433A50
BOOL __fastcall ai_test_best_psistorm_target(CUnit *pCandidate, CUnit *pSource);    // 0x433B10
BOOL __fastcall ai_test_best_feedback_target(CUnit *pCandidate, CUnit *pSource);    // 0x433BB0
BOOL __fastcall ai_test_best_stasis_target(CUnit *pCandidate, CUnit *pSource);      // 0x433D10
BOOL __fastcall ai_test_best_mind_control_target(CUnit *pCandidate, CUnit *pSource);// 0x433460
BOOL __fastcall ai_test_best_teleport_target(CUnit *pCandidate, CUnit *pSource);    // 0x433C10
BOOL __fastcall ai_test_best_swarm_target(CUnit *pCandidate, CUnit *pSource);       // 0x433E60
BOOL __fastcall ai_test_best_nuke_target_in_area(CUnit *pCandidate, CUnit *pSource);                      // 0x433620
BOOL __fastcall ai_test_best_yamato_target(CUnit *pCandidate, CUnit *pSource);      // 0x433F40

// oracle 0x433F20, 29 B, unported, address-taken only by AIFindUnpoweredBldg. Not
// a member of the CUnitSearchPredicate family and must not be declared as one:
// its EDX is an int player index, not a CUnit*, compared against the candidate's
// zero-extended bPlayer. sub_442D40, the search it filters for, carries a
// different context type from the two searches above.
BOOL __fastcall ai_test_best_unpowered_target(CUnit *pCandidate, int nPlayer);

// The two collateral counters carry the search-predicate signature but are not
// predicates: each adds into dword_665D28 and then returns FALSE unconditionally,
// so the search that dispatches them visits every candidate and never stops
// early. That is 1.17's design, kept rather than corrected. No dispatcher above
// takes their address.
BOOL __fastcall ai_count_collateral_energy(CUnit *pCandidate, CUnit *pSource);   // 0x433410
BOOL __fastcall ai_count_collateral_shields(CUnit *pCandidate, CUnit *pSource);  // 0x433CD0

// oracle 0x433DF0, 112 B. Its argument order is the reverse of the family's, and
// the spelling below says so rather than quietly conforming: ECX supplies only a
// bPlayer, whose visibility bit is tested in the other unit's sprite, and is the
// `this` of CUnit::IsEnemyUnit; EDX is the unit whose sprite, dwFlags, dwUnionE4,
// wType and bOrderId are read. Both call sites swap their registers before
// calling, which is the corroboration.
BOOL __fastcall sub_433DF0(CUnit *pSource, CUnit *pCandidate);

// The non-search callees the odd members of that family reach.

// oracle 0x481CB0 (sai.cpp), unported. Not to be confused with
// UnitCanReachGroundTarget_xy (0x481D00), which src117/h/CUnitCombat.h declares
// and which is a different function with three arguments. Two register arguments,
// both dereferenced as CUnit* at +0x0C, nothing pushed, bare `ret`.
// CUnit::CanTarget, CUnit::DispatchFollow and sub_432870 all truth-test the
// result, so BOOL.
BOOL __fastcall UnitCanReachGroundTarget(CUnit *pCandidate, CUnit *pSource);

// oracle 0x481BA0, unported. A `this`-shaped CUnit* in ECX; it loads the sprite
// at +0x0C and tail-jumps GetRegion_xy with its position. The WORD return comes
// from the call site: AIFindRecallTarget widens the result with `movzx edx, ax`,
// which an int return would not need. The body, being a tail jump, says nothing
// about its own return width.
WORD __fastcall GetRegion_CUnit(CUnit *pUnit);

// oracle 0x4AEA70, 43 B, unported. Two register arguments: a player index scaled
// into a pointer table (`add eax, dword ptr [ecx*4+6B1A74h]`) and a region index
// scaled by the 0x34-byte record (`imul eax, edx, 34h`). Its two exits are
// `mov eax, 1h` and `xor eax, eax`, and AIFindRecallTarget truth-tests the
// result. The record's contents are not derived, only those two strides.
BOOL __fastcall tai_captain_region_is_calm(int nPlayer, int nRegion);

// oracle 0x487530, 18 B, oracle TU SAI_Build.cpp, unported. One register argument
// used as `this`, dereferenced at +0x134 (CUnit::dwUnion134), nothing pushed,
// bare `ret`. The body only proves it returns the dword at [[ecx+134h]+14h]+28h;
// sub_432E70 compares that against its own candidate CUnit*, a pointer-identity
// test, which pins the return type. Declared free rather than as a CUnit member:
// nothing here shows it belongs to that class.
CUnit *__fastcall town_repair_worker(CUnit *pUnit);

// The AI area-survey scorers' own helpers.

// oracle 0x431ED0, 42 B. A free __fastcall although it stores its ECX as a whole
// CUnit*: it stashes that pointer in dword_665D08 for its filter to read back,
// then hands sub_442D40 the map rectangle and sub_432250's address. The second
// parameter is 16 bits, and the entry says so -- `mov si, dx`, a bare 16-bit
// register move -- widened only later to fill sub_442D40's fifth dword slot.
CUnit *__fastcall ai_find_nearest_enemy_type(CUnit *pUnit, WORD wType);

// oracle 0x432100, 68 B. The ally-side area accumulator: adds each passing
// candidate's CUnit::GetToughness into dword_665D2C and returns FALSE
// unconditionally, so the sweep visits every candidate and never stops early,
// the same not-really-a-predicate shape as the collateral counters above.
// Address-taken as find_first_unit_near's filter.
BOOL __fastcall ai_query_area_ally_hp(CUnit *pCandidate, CUnit *pSource);

// oracle 0x432090, 103 B. The enemy-side area accumulator and the twin of
// ai_query_area_ally_hp above: the same dword_665D24 weapon id, the same callees,
// the same always-FALSE return. It adds a self-comparison guard the ally side has
// no use for, and a second arm on `dword_665D24 == 0x3C` accumulating
// `(hitPoints + 255) >> 8`, the x256 round-up, an arithmetic shift rather than a
// plain one. Address-taken as find_first_unit_near's filter.
BOOL __fastcall ai_query_area_enemy_hp(CUnit *pCandidate, CUnit *pSource);

// oracle 0x432150, 142 B. The collateral-damage accumulator: adds each
// candidate's effective hit points, plus shields where the type has them, into
// dword_665D28, and saturates the total at 0x320 for a candidate that is either a
// specific pair of unit types or accepted by CUnit::sub_431A90 -- one hit that
// alone justifies the strike. Its only exit is `xor eax, eax`. Address-taken as
// find_first_unit_near's filter by the two nuke scorers.
BOOL __fastcall ai_query_collateral_damage(CUnit *pCandidate, CUnit *pSource);

// oracle 0x432250, 38 B. The filter ai_find_nearest_enemy_type registers with
// sub_442D40. Its EDX is a WORD unit type (`cmp word ptr [ecx+64h], dx`), not a
// CUnit*, so it is deliberately not spelled with the CUnitSearchPredicate
// typedef -- the same distinction ai_test_best_unpowered_target draws for its int
// player index. 0xE4 is one past the last units.dat row, the "any type" sentinel.
BOOL __fastcall sub_432250(CUnit *pCandidate, WORD wType);

// oracle 0x442D40, 76 B, oracle TU find.cpp, unported. A third generic search
// alongside CUnit::sub_442BC0 and FindNearestInRect, and a free function rather
// than a member: an x and a y in ECX/EDX and three pushed dwords, retired with
// `ret 0Ch`. The stack order (area, filter, context) is the right-to-left push
// order at AIFindUnpoweredBldg's call site. x and y are spelled `int`, which the
// one call site does not tell apart from `short`: both compile the same here.
CUnit *__fastcall sub_442D40(int x, int y, const mrect_t *pSearchData,
                             BOOL (__fastcall *pFilter)(CUnit *, int), intptr_t nPlayer);

// The landed dispatchers, in oracle address order, which is also their order in
// the .cpp. Three shapes: a bare forward (17 B); a guarded forward (24 B), an
// early NULL on the nonzero flag; and a range-selecting forward (56 to 71 B),
// where the range is a local variable rather than two returns, which cost 7 bytes
// more. Where a bare `mov eax, K` follows the aiscript_has_campaign call, both
// ternary arms are K, and the ternary stays written out so the call survives.

CUnit *__fastcall AIFindBroodlingTarget(CUnit *pUnit, int nFlags);    // 0x432BF0  62 B
CUnit *__fastcall AIFindMatrixTarget(CUnit *pUnit, int nFlags);       // 0x432C70  56 B
CUnit *__fastcall AIFindDisruptorTarget(CUnit *pUnit, int nFlags);    // 0x432CB0  65 B
CUnit *__fastcall AIFindEMPTarget(CUnit *pUnit, int nFlags);          // 0x432D00  72 B
CUnit *__fastcall AIFindEnsnareTarget(CUnit *pUnit, int nFlags);      // 0x432D50  65 B
CUnit *__fastcall AIFindHallucinateTarget(CUnit *pUnit, int nFlags);  // 0x432DA0  24 B
CUnit *__fastcall AIFindIrradiateTarget(CUnit *pUnit, int nFlags);    // 0x432DC0  62 B
CUnit *__fastcall AIFindRepairTarget(CUnit *pUnit, int nFlags);       // 0x432E00  17 B
CUnit *__fastcall AIFindMagnapulseTarget(CUnit *pUnit, int nFlags);   // 0x432E20  65 B
CUnit *__fastcall AIFindMindControlTarget(CUnit *pUnit, int nFlags);  // 0x432EB0  17 B
CUnit *__fastcall AIFindMyopiaTarget(CUnit *pUnit, int nFlags);       // 0x432ED0  56 B
CUnit *__fastcall AIFindNukeTarget(CUnit *pUnit);                     // 0x432F40  17 B; the caller sets no EDX
CUnit *__fastcall AIFindParalyzeTarget(CUnit *pUnit, int nFlags);     // 0x432FA0  71 B
CUnit *__fastcall AIFindParasiteCureTarget(CUnit *pUnit, int nFlags);               // 0x433000  24 B
CUnit *__fastcall AIFindParasiteTarget(CUnit *pUnit, int nFlags);     // 0x433020  56 B
CUnit *__fastcall AIFindPlagueTarget(CUnit *pUnit, int nFlags);       // 0x433060  56 B
CUnit *__fastcall AIFindPsiStormTarget(CUnit *pUnit, int nFlags);     // 0x4330A0  71 B
CUnit *__fastcall AIFindPsyFeedbackTarget(CUnit *pUnit, int nFlags);  // 0x4330F0  65 B
CUnit *__fastcall AIFindRecallTarget(CUnit *pUnit);                   // 0x433140  51 B; the caller sets no EDX
CUnit *__fastcall AIFindStasisTarget(CUnit *pUnit, int nFlags);       // 0x433180  65 B
CUnit *__fastcall AIFindSwarmTarget(CUnit *pUnit, int nFlags);        // 0x4331D0  70 B
CUnit *__fastcall AIFindYamatoTarget(CUnit *pUnit, int nFlags);       // 0x433240  32 B
// Hyperion: Psionic Storm's finder for a mod's spell that spares the caster's side,
// with its weapon, searching nRange pixels (0 for the storm's)
CUnit *__fastcall AIFindModClusterTarget(CUnit *pUnit, int nFlags, int nWeapon, int nRange);

// The three that do not carry the family signature.

// oracle 0x432F60, 62 B. A free __fastcall taking the search rectangle by value.
// An aggregate is never register-eligible under __fastcall, so it occupies the
// two stack slots the oracle reads at `[ebp+8h]` and `[ebp+0Ch]` and its `ret 8h`
// retires, leaving ECX holding the CUnit* the body searches from. A pair of dword
// parameters compiles to the same bytes; the aggregate reading comes from
// ai_test_best_nuke_target_in_area, which reads four adjacent WORDs back.
CUnit *__fastcall AIFindNukeTargetInArea(CUnit *pUnit, mrect_t area);            // 0x432F60  62 B

// oracle 0x433220, 31 B. ECX is an int player index, not a CUnit*: the caller
// computes `movzx eax, byte ptr [ebx+4Ch]` and passes that, and the filter
// ai_test_best_unpowered_target compares its context against a candidate's
// zero-extended bPlayer. The packed position rides the one pushed dword that
// `ret 4h` retires, and it is unsigned: the body splits it with `shr edx, 10h`,
// where a signed int would give `sar`.
CUnit *__fastcall AIFindUnpoweredBldg(int nPlayer, mpoint_t pos);  // 0x433220  31 B

// oracle 0x4332C0, 33 B. A search predicate, not a dispatcher: two register
// arguments, no stack, bare `ret`. Two `if (...) return FALSE;` guards share a
// failure epilogue that sits first in the body, then a call to CUnit::sub_437060
// with the two units swapped -- the source becomes `this` and the candidate the
// argument. CUnit::FindHealTarget open-codes the same guard and the same swapped
// call one level up.
BOOL __fastcall ai_test_best_heal_target(CUnit *pCandidate, CUnit *pSource);        // 0x4332C0  33 B

// TTBaseUnitType -- oracle 0x4B3850, 77 B, unported, called from maphdr.cpp and
// placebox.cpp. `ret` with ECX live-in only: ECX is a 16-bit unit type used as a
// scaled index into 0x523A78, and all three callers consume EAX 16 bits wide, so
// both ends are WORD -- one path ends `mov ax, word ptr [eax*2+50DA8Ah]`, which
// writes AX only. It returns 0xE4 == 228, the units.dat entry count and so
// "none", when the lookup fails. Neither end's sign is derivable.
WORD __fastcall TTBaseUnitType(WORD wType);                  // 0x4B3850  77 B

// Search predicates of the family above, (candidate in ECX, source in EDX),
// bare `ret`. SearchMineTarget is CUnit::FindMineTarget's filter; sub_4321E0
// and sub_432280 are pushed as filters by ai_test_best_paralyze_target and by
// CUnit::sub_431B70 / CUnit::FindAttackTarget.
BOOL __fastcall SearchMineTarget(CUnit *pCandidate, CUnit *pSource);  // 0x432390  56 B
BOOL __fastcall sub_4321E0(CUnit *pCandidate, CUnit *pSource);        // 0x4321E0  98 B
BOOL __fastcall sub_432280(CUnit *pCandidate, CUnit *pSource);        // 0x432280 259 B
// oracle 0x433910 (314 B). CUnit::FindAttackTarget tail-jumps here with the
// unit in ECX and a bucket index of sub_432280's survey in EDX; returns the
// bucket's pick.
CUnit *__fastcall AIPickBestTarget(CUnit *pUnit, int nBucket);             // 0x433910 314 B
// CUnit::sub_431AE0 hands sub_4317D0 to the same search in EDX.
BOOL __fastcall sub_4317D0(CUnit *pCandidate, CUnit *pSource);        // 0x4317D0 274 B

// The context sub_4318F0 receives through find_first_unit_near's unit slot:
// CUnit::sub_4316B0 builds it at [ebp-8h] with the mask at +0 and `this` at +4,
// and passes `lea edx, [ebp-8h]` when it calls the predicate directly.
struct vision_ctx_t {
  /*0x00*/ DWORD  dwMask;
  /*0x04*/ CUnit *pSource;
};
OC_X86_LAYOUT(sizeof(vision_ctx_t) == 8, "vision_ctx_t: [ebp-8h]..[ebp-1h]");
static_assert(offsetof(vision_ctx_t, dwMask) == 0x00, "vision_ctx_t::dwMask [ebp-8h]");
OC_X86_LAYOUT(offsetof(vision_ctx_t, pSource) == 0x04, "vision_ctx_t::pSource [ebp-4h]");

// oracle 0x4318F0 (137 B), unported. The predicate CUnit::sub_4316B0 hands to
// find_first_unit_near and also calls directly for each scanner; a nonzero result ends
// the scanner walk.
BOOL __fastcall sub_4318F0(CUnit *pCandidate, vision_ctx_t *pCtx);   // 0x4318F0 137 B
// Hyperion: CUnit::sub_4325A0 over a set of every slot, for a game with Hyperion's
// slots (stage D, CUnitSearches.cpp)
void DetectionUpdateHx(CUnit *pUnit);
