// SAI_Build.h -- the SAI_Build.cpp entry points src117 code calls, and the
// TWorker / TBldg / TTown record family with their pools.
//
// Every one of these names is declared in a header of ours. The reference tree's
// own SAI_Build.h spells region_has_worker_or_building
// `BOOL (int plyr, WORD rgn)`, a narrowing Blizzard's bytes do not support and
// one no byte gate would catch, so the declaration below is the one that wins.
#pragma once

#include "platform.h"

// region_has_worker_or_building -- 0x4861A0, 87 B. Two register arguments, both
// read 32 bits wide by the callee itself, and no stack parameters (bare `ret`);
// for a parameter the callee is the authority, so both are `int`. `int` for the
// return as well, not BOOL: 10 of 10 consuming callers take the full EAX and the
// exits are not all literal 0/1, so only a 32-bit result is proven.
int region_has_worker_or_building(int nPlayer, int nRegion);

#include "CUnit.h"
#include "pos.h"   // Hyperion: spos_t (stage E)
#include "aipools.h"  // Hyperion: AI_RECORDS_STORE, AI_TOWNS_STORE

// Each stride of the family is stated twice, by the id-to-pointer multiply in
// *_ptr and by the pointer-to-id reciprocal in *_id: 24 for TWorker
// (`lea eax,[ecx+ecx*2]` / `lea eax,[eax*8+67F798h]` against `shr edx,4h` of
// -55555555h), 44 for TBldg (`imul eax, ecx, 2Ch` against `shr edx,5h` of
// -45D1745Dh) and 460 for TTown (`imul eax, ecx, 1CCh` against 1CF06ADBh's
// shift chain). Each count comes from the ledger's mangled TSaveNode<T,N> rows
// and ends just before the next named global: gPeons_g @0x67F7B0 + 1000*24,
// gBldgs_g @0x685578 + 1000*44, gTowns_g @0x690160 + 100*460.
//
// The ids are 1-based in all three: each *_ptr folds a one-element bias into its
// base (0x67F798 == &gPeons_g[-1], 0x68554C == &gBldgs_g[-1], 0x68FF94 ==
// &gTowns_g[-1]) and each *_id adds one back. Those three biased addresses are
// not globals.

struct TTown;
struct TBldg;
struct TWorker;
// The three pool objects are defined below TTown because they embed the
// record arrays; TTown +0x08/+0x10 point AT them, so they need seating here.
struct TWorkerPool;
struct TBldgPool;
struct TTownPool;

// TWorker -- the per-worker AI record, 24 B. CUnit +0x134 points at one of this
// family and the record's own +0x08 is the discriminator (TBldg carries 3 --
// bldg_clear_slot `cmp byte ptr [eax+8h], 3h` -- and TSoldier 4). TWorker and
// TBldg share their first 0x18 bytes: town_repair_worker, town_hold_workers and
// town_clear_repair_worker all reach the owning town as `[+0x134]` then
// `[+0x14]` without testing the tag, which only works if the field sits at the
// same offset in both. The fields below +0x08 are underived rather than absent;
// name one only with an instruction that states its width.
struct TWorker {
    /*0x00*/ TWorker *pNext;      // the list link TTown::pWorkers heads
    /*0x04*/ TWorker *pPrev;      // TSaveNode's unlink writes both directions
    /*0x08*/ BYTE     bType;      // the +0x08 family tag
    /*0x09*/ BYTE     bUnknown09;
    union {
        /*0x0A*/ WORD wUnknown0A;
        struct {
            /*0x0A*/ BYTE bUnknown0A;  // town_tick `inc byte ptr [esi+0Ah]`
            /*0x0B*/ BYTE bUnknown0B;  // town_tick `mov byte ptr [esi+0Bh], 0Ah`
        };
    };
    /*0x0C*/ DWORD    dwUnknown0C;
    /*0x10*/ CUnit   *pUnit10;      // town_pending_costs: `mov ecx,[esi+10h]` then
                                    // `movzx eax, byte ptr [ecx+4Dh]`
                                    // (CUnit::bOrderId), and ECX is `this`
                                    // for CUnit::GetBuildUnit
    /*0x14*/ TTown   *pTown;      // town_repair_worker: [+0x14] then [+0x28] is a CUnit *
};
OC_X86_LAYOUT(sizeof(TWorker) == 24, "TWorker stride (worker_ptr x24 / worker_id /24)");
static_assert(offsetof(TWorker, pNext) == 0x00, "TWorker.pNext");
OC_X86_LAYOUT(offsetof(TWorker, bType) == 0x08, "TWorker.bType [eax+8h] BYTE");
OC_X86_LAYOUT(offsetof(TWorker, pTown) == 0x14, "TWorker.pTown [eax+14h]");
OC_X86_LAYOUT(offsetof(TWorker, pPrev)       == 0x04, "TWorker.pPrev");
OC_X86_LAYOUT(offsetof(TWorker, bUnknown09)  == 0x09, "TWorker.bUnknown09");
OC_X86_LAYOUT(offsetof(TWorker, wUnknown0A)  == 0x0A, "TWorker.wUnknown0A");
OC_X86_LAYOUT(offsetof(TWorker, bUnknown0A)  == 0x0A, "TWorker.bUnknown0A");
OC_X86_LAYOUT(offsetof(TWorker, bUnknown0B)  == 0x0B, "TWorker.bUnknown0B");
OC_X86_LAYOUT(offsetof(TWorker, dwUnknown0C) == 0x0C, "TWorker.dwUnknown0C");
OC_X86_LAYOUT(offsetof(TWorker, pUnit10) == 0x10, "TWorker.pUnit10");

// TBldg -- the per-building AI record, 44 B, packed edge to edge: TBldg::PreSave
// walks five slots as `lea esi,[ebx+18h]` / `add esi,4h` / `cmp edi,5h`, so
// +0x18..+0x2B is a DWORD[5] and 0x18 + 20 == 0x2C == 44. The parallel BYTE[5]
// at +0x09 selects between them, and its live range is 0..8, not 0..3:
// PostLoad's jump table at 0x484094 has nine entries -- [0] continue,
// [1] captain_id_to_ptr, [2] guard_ptr, [3] town_ptr, [4..8] store 0 -- and
// PreSave's dec-chain agrees, so 1 is a captain, 2 a guard and 3 a town.
// bldg_set_slot and bldg_move_slot write the same pair (`[+edx*1+9h]` BYTE,
// `[+edx*4+18h]` DWORD).
struct TBldg {
    /*0x00*/ TBldg  *pNext;
    /*0x04*/ TBldg  *pPrev;
    /*0x08*/ BYTE    bType;         // == 3 in every body that tests it
    /*0x09*/ BYTE    aSlotKind[5];  // 0 = empty, 1 = captain, 2 = guard,
                                    // 3 = town, 4..8 = store 0
    /*0x0E*/ WORD    wUnknown0E;
    /*0x10*/ CUnit  *pUnit;         // PreSave -> CUnit::GetUnitID
    /*0x14*/ TTown  *pTown;         // PreSave -> town id via the /460 magic
    /*0x18*/ DWORD_PTR aSlots[5];     // union of TTown * / TGuard * / TAI_Captain *

    // 0x484010 (132 B) / 0x484380 (136 B). Both `ret` with ECX live on a
    // `Class::` name -> `this` (thiscall), no stack parameters, and EAX at the
    // ret is an incidental leftover no caller reads -> void. PostLoad turns the
    // saved ids back into pointers (CUnit::FromId, town_ptr, guard_ptr,
    // captain_id_to_ptr); PreSave is its exact mirror.
    void PostLoad();
    void PreSave();
};
OC_X86_LAYOUT(sizeof(TBldg) == 44, "TBldg stride (bldg_ptr x44 / bldg_id /44)");
OC_X86_LAYOUT(offsetof(TBldg, bType)     == 0x08, "TBldg.bType");
OC_X86_LAYOUT(offsetof(TBldg, aSlotKind) == 0x09, "TBldg.aSlotKind [ebx+edi+9h]");
OC_X86_LAYOUT(offsetof(TBldg, pUnit)     == 0x10, "TBldg.pUnit");
OC_X86_LAYOUT(offsetof(TBldg, pTown)     == 0x14, "TBldg.pTown");
OC_X86_LAYOUT(offsetof(TBldg, aSlots)    == 0x18, "TBldg.aSlots [ebx+18h] DWORD[5]");
static_assert(offsetof(TBldg, pNext)      == 0x00, "TBldg.pNext");
OC_X86_LAYOUT(offsetof(TBldg, pPrev)      == 0x04, "TBldg.pPrev");
OC_X86_LAYOUT(offsetof(TBldg, wUnknown0E) == 0x0E, "TBldg.wUnknown0E");

// A town's request queue entry, 4 B. add_request_to_town_queue writes all three
// fields at `[edi+esi*4+3Ch]` (BYTE), `[edi+esi*4+3Dh]` (BYTE) and
// `[edi+esi*4+3Eh]` (WORD), scans for a free slot with
// `test byte ptr [eax], -8h` and gives up at `cmp esi, 64h`, so 100 entries. The
// packed first byte is ((prio << 2 | kind & 3) << 1) | (flag & 1); read the
// oracle rather than this sentence when writing that body.
struct TTownRequest {
    /*0x00*/ BYTE bPacked;
    /*0x01*/ BYTE bUnknown01;
    /*0x02*/ WORD wId;
};
static_assert(sizeof(TTownRequest) == 4, "TTownRequest (stride 4 -- [edi+esi*4+3Ch])");
static_assert(offsetof(TTownRequest, bPacked)    == 0x00, "TTownRequest.bPacked");
static_assert(offsetof(TTownRequest, bUnknown01) == 0x01, "TTownRequest.bUnknown01");
static_assert(offsetof(TTownRequest, wId)        == 0x02, "TTownRequest.wId");

// TTown -- the per-town AI record, 460 B, packed edge to edge: the request queue
// starts at +0x3C and NewTown clears it with `push 190h` / `call memset`, so
// 0x3C + 0x190 == 0x1CC == 460 exactly, with no slack. The pool-base and
// list-head pairs at +0x08 and +0x10 are what TTown::PreSave shows: it turns
// `[esi+0Ch]` into an id by `sub ecx,[esi+8]` then the /24 magic (a TWorker) and
// `[esi+14h]` by `sub ecx,[esi+10h]` then the /44 magic (a TBldg), zeroing the
// base of each pair afterwards.
//
// NewTown zeroes +0x19 and +0x1C with 16-bit stores, while town_forget_unit reads
// +0x1C and writes +0x1D one byte at a time, and both spellings cannot come from
// the same field declaration. The two BYTEs below are the shape the readers
// need; whoever writes NewTown has to find the spelling that emits the WORD
// stores (a union, or a WORD-typed overlay) and measure it, rather than assume
// MSVC merges adjacent byte stores.
struct TTown {
    /*0x000*/ TTown   *pNext;
    /*0x004*/ TTown   *pPrev;
    /*0x008*/ TWorkerPool *pWorkerPool; // the pool object, not the array:
                                        // town_add_unit reaches the free head as
                                        // `mov edx,[esi+8h]`/`mov ecx,[edx+5DC0h]`
                                        // and 0x5DC0 == 1000 * 24. PreSave: the
                                        // base the /24 magic subtracts
    /*0x00C*/ TWorker *pWorkers;
    /*0x010*/ TBldgPool   *pBldgPool;   // likewise the pool object (0xABE0 ==
                                        // 1000 * 44). PreSave: the base the /44
                                        // magic subtracts
    /*0x014*/ TBldg   *pBldgs;
    /*0x018*/ BYTE     bPlayer;       // NewTown `mov byte ptr [esi+18h], cl`
    /*0x019*/ BYTE     bUnknown19;
    /*0x01A*/ BYTE     bUnknown1A;
    /*0x01B*/ BYTE     bUnknown1B;    // NewTown `mov byte ptr [esi+1Bh], 0h`
    /*0x01C*/ BYTE     bTownPlan;     // town_forget_unit `cmp byte ptr [eax+1Ch], 0h`.
                                    // player_mineral_fields and town_delete_if_empty
                                    // each do `movzx ecx, byte ptr [<town>+1Ch]` and
                                    // hand the result to sai_town_mineral_fields /
                                    // sai_townbuilder_clear, which index `town_plan`
                                    // @0x6A2E88 with it and bound it at 250.
    /*0x01D*/ BYTE     bUnknown1D;    // town_forget_unit `mov byte ptr [eax+1Dh], 1h`
    /*0x01E*/ BYTE     bUnknown1E;    // town_hold_workers `mov byte ptr [eax+1Eh], 1h`
    /*0x01F*/ BYTE     bUnknown1F;
    /*0x020*/ spos_t   nX;            // town_first_town_xy `movsx eax, word ptr [ecx+20h]`; 4 bytes on x64 (stage E)
    /*0x022*/ spos_t   nY;            // town_first_town_xy `movsx ecx, word ptr [ecx+22h]`
    /*0x024*/ CUnit   *pUnit24;       // the six CUnit * slots PreSave maps
    /*0x028*/ CUnit   *pUnit28;       //   through CUnit::GetUnitID, in the
    /*0x02C*/ CUnit   *pUnit2C;       //   order 2C, 30, 34, 38, 28, 24
    /*0x030*/ CUnit   *pUnit30;
    /*0x034*/ CUnit   *pUnit34;
    /*0x038*/ CUnit   *pUnit38;
    /*0x03C*/ TTownRequest aQueue[100];

    // 0x4840C0 (124 B) / 0x484410 (141 B). Same contract as TBldg's pair:
    // thiscall, no stack parameters, void. PreSave converts pWorkers with the
    // /24 magic against pWorkerPool and pBldgs with the /44 magic against
    // pBldgPool, then maps the six CUnit * slots through CUnit::GetUnitID.
    void PostLoad();
    void PreSave();
};
OC_X86_LAYOUT(sizeof(TTown) == 460, "TTown stride (town_ptr x460 / town_id /460)");
OC_X86_LAYOUT(offsetof(TTown, pWorkerPool) == 0x08, "TTown.pWorkerPool");
OC_X86_LAYOUT(offsetof(TTown, pBldgPool)   == 0x10, "TTown.pBldgPool");
OC_X86_LAYOUT(offsetof(TTown, bPlayer)     == 0x18, "TTown.bPlayer");
OC_X86_LAYOUT(offsetof(TTown, nX)          == 0x20, "TTown.nX  movsx WORD");
OC_X86_LAYOUT(offsetof(TTown, pUnit24)     == 0x24, "TTown.pUnit24");
OC_X86_LAYOUT(offsetof(TTown, aQueue)      == 0x3C, "TTown.aQueue -- memset 0x190 from +0x3C");
static_assert(offsetof(TTown, pNext)      == 0x00, "TTown.pNext");
OC_X86_LAYOUT(offsetof(TTown, pPrev)      == 0x04, "TTown.pPrev");
OC_X86_LAYOUT(offsetof(TTown, pWorkers)   == 0x0C, "TTown.pWorkers");
OC_X86_LAYOUT(offsetof(TTown, pBldgs)     == 0x14, "TTown.pBldgs");
OC_X86_LAYOUT(offsetof(TTown, bUnknown19) == 0x19, "TTown.bUnknown19");
OC_X86_LAYOUT(offsetof(TTown, bUnknown1A) == 0x1A, "TTown.bUnknown1A");
OC_X86_LAYOUT(offsetof(TTown, bUnknown1B) == 0x1B, "TTown.bUnknown1B");
OC_X86_LAYOUT(offsetof(TTown, bTownPlan) == 0x1C, "TTown.bTownPlan");
OC_X86_LAYOUT(offsetof(TTown, bUnknown1D) == 0x1D, "TTown.bUnknown1D");
OC_X86_LAYOUT(offsetof(TTown, bUnknown1E) == 0x1E, "TTown.bUnknown1E");
OC_X86_LAYOUT(offsetof(TTown, bUnknown1F) == 0x1F, "TTown.bUnknown1F");
OC_X86_LAYOUT(offsetof(TTown, nY)         == 0x22, "TTown.nY");
OC_X86_LAYOUT(offsetof(TTown, pUnit28)    == 0x28, "TTown.pUnit28");
OC_X86_LAYOUT(offsetof(TTown, pUnit2C)    == 0x2C, "TTown.pUnit2C");
OC_X86_LAYOUT(offsetof(TTown, pUnit30)    == 0x30, "TTown.pUnit30");
OC_X86_LAYOUT(offsetof(TTown, pUnit34)    == 0x34, "TTown.pUnit34");
OC_X86_LAYOUT(offsetof(TTown, pUnit38)    == 0x38, "TTown.pUnit38");

// The three pools, each an array followed by a free head, TSoldierPool's shape.
// gTowns_g's free head is read directly -- NewTown does
// `mov esi, dword ptr [edi+0B3B0h]` with EDI the pool base, and
// 0xB3B0 == 100 * 460. The other two are the same shape at the same relative
// offset, and each bound leaves room for exactly it.
// Hyperion: 1.17's 1000 and 100 on x86, the most a game takes on 64 bits.
struct TWorkerPool { /*0x0000*/ TWorker aWorkers[AI_RECORDS_STORE]; /*0x5DC0*/ TWorker *pFreeHead; };
struct TBldgPool   { /*0x0000*/ TBldg   aBldgs[AI_RECORDS_STORE];   /*0xABE0*/ TBldg   *pFreeHead; /*0xABE4*/ DWORD dwReserved; };
struct TTownPool   { /*0x0000*/ TTown   aTowns[AI_TOWNS_STORE];     /*0xB3B0*/ TTown   *pFreeHead; /*0xB3B4*/ DWORD dwReserved; };
OC_X86_LAYOUT(sizeof(TWorkerPool) == 0x5DC4, "TWorkerPool -- gPeons_g .. dword_685574");
OC_X86_LAYOUT(sizeof(TBldgPool)   == 0xABE8, "TBldgPool   -- gBldgs_g .. gTowns_g");
OC_X86_LAYOUT(sizeof(TTownPool)   == 0xB3B8, "TTownPool   -- gTowns_g .. gPlayerTowns_g");
OC_X86_LAYOUT(offsetof(TTownPool, pFreeHead) == 0xB3B0, "TTownPool.pFreeHead [edi+0B3B0h] -- NewTown");

// gPlayerTowns_g @0x69B518 -- eight 8-byte records, one per player.
// sai_build_init writes all eight: `[0x69B518 + i*8] = 690160h` (the TTown pool
// base) and `[0x69B51C + i*8] = 0` (the head of that player's town list), the
// last landing at 0x69B554. The sheet's next name is dword_69B558, so the
// object is exactly 0x40 == 8 * 8 bytes.
struct TPlayerTowns {
    /*0x00*/ TTownPool *pPool;
    /*0x04*/ TTown     *pTowns;
};
OC_X86_LAYOUT(sizeof(TPlayerTowns) == 8, "TPlayerTowns -- stride 8 ([ecx*8+69B518h])");
static_assert(offsetof(TPlayerTowns, pPool)  == 0x00, "TPlayerTowns.pPool");
OC_X86_LAYOUT(offsetof(TPlayerTowns, pTowns) == 0x04, "TPlayerTowns.pTowns");

// The save pools and their list heads, as the oracle's template rows name them.
// Each instantiation is an explicit specialization so that
// `TSaveNode<TBldg,1000>::Init` and its siblings resolve; the argument lists
// are spelled as the ledger spells them, without spaces. A pool is
// the record array followed by its free head; a container is a pool pointer
// followed by a list head, the shape TTown +0x08 and +0x10 have.
template <class T, int N> struct TSaveNode;
template <class T, int N> struct TSaveContainer;

template <> struct TSaveNode<TWorker,1000> {
    /*0x0000*/ TWorker  aNodes[AI_RECORDS_STORE];  // Hyperion: 1000 in 1.17
    /*0x5DC0*/ TWorker *pFree;
    void Init();
    void PostLoad();
    void PreSave();
};
template <> struct TSaveNode<TBldg,1000> {
    /*0x0000*/ TBldg  aNodes[AI_RECORDS_STORE];  // Hyperion: 1000 in 1.17
    /*0xABE0*/ TBldg *pFree;
    void Init();
    void PostLoad();
    void PreSave();
};
template <> struct TSaveNode<TTown,100> {
    /*0x0000*/ TTown  aNodes[AI_TOWNS_STORE];  // Hyperion: 100 in 1.17
    /*0xB3B0*/ TTown *pFree;
    void Init();
    void PostLoad();
    void PreSave();
};
typedef TSaveNode<TWorker,1000> TWorkerSaveNode;
typedef TSaveNode<TBldg,1000>   TBldgSaveNode;
typedef TSaveNode<TTown,100>    TTownSaveNode;
static_assert(offsetof(TWorkerSaveNode, aNodes) == 0x0000, "TSaveNode<TWorker,1000>.aNodes");
OC_X86_LAYOUT(offsetof(TWorkerSaveNode, pFree)  == 0x5DC0, "TSaveNode<TWorker,1000>.pFree [edx+5DC0h]");
OC_X86_LAYOUT(sizeof(TWorkerSaveNode) == 0x5DC4, "sai_read_build_data decompresses 5DC4h");
static_assert(offsetof(TBldgSaveNode, aNodes) == 0x0000, "TSaveNode<TBldg,1000>.aNodes");
OC_X86_LAYOUT(offsetof(TBldgSaveNode, pFree)  == 0xABE0, "TSaveNode<TBldg,1000>.pFree [edx+0ABE0h]");
OC_X86_LAYOUT(sizeof(TBldgSaveNode) == 0xABE4, "sai_read_build_data decompresses 0ABE4h");
static_assert(offsetof(TTownSaveNode, aNodes) == 0x0000, "TSaveNode<TTown,100>.aNodes");
OC_X86_LAYOUT(offsetof(TTownSaveNode, pFree)  == 0xB3B0, "TSaveNode<TTown,100>.pFree [edi+0B3B0h]");
OC_X86_LAYOUT(sizeof(TTownSaveNode) == 0xB3B4, "sai_read_build_data decompresses 0B3B4h");

// Free returns one record to the pool; New takes one and pushes it on the list.
template <> struct TSaveContainer<TBldg,1000> {
    /*0x00*/ TSaveNode<TBldg,1000> *pPool;
    /*0x04*/ TBldg                 *pHead;
    void Free(TBldg *pNode);
    TBldg *New();
};
typedef TSaveContainer<TBldg,1000> TBldgSaveContainer;
static_assert(offsetof(TBldgSaveContainer, pPool) == 0x00, "TSaveContainer<TBldg,1000>.pPool [ecx]");
OC_X86_LAYOUT(offsetof(TBldgSaveContainer, pHead) == 0x04, "TSaveContainer<TBldg,1000>.pHead [ecx+4]");
OC_X86_LAYOUT(sizeof(TBldgSaveContainer) == 8, "TSaveContainer<TBldg,1000>");

// TSaveContainer<TWorker,1000> lives in its own header: find_decl reads the
// first `Free(` in a header, and the two Free members differ in their argument.
#include "SAI_BuildWorkers.h"

// The declarations. Each block keeps the evidence for its signature: the `ret`
// shape, which registers are real inputs, each stack slot's width and sign, and
// what the callers do with EAX. Every width came off the oracle.
//
// What is not derived: parameter names, and what a `void *` points at. Both are
// 4 bytes in every slot, so neither can move a byte, while a width or a sign
// can. Read the oracle before writing a body against one of these, and if a
// declaration here is wrong, say so rather than editing it in passing: a
// signature change compiles silently as an overload and only the payload link
// objects.

// 0x4844A0 bldg_set_slot (27 B)
//   `ret 8h`; EDX read before written -> a second register argument
//     ECX is dereferenced -> a pointer
//     EDX is read as 32 bits
//   `ret 8h` -> 2 stack words
//     [ebp+08h] is only ever read 1 byte(s) -> 8-bit (for a parameter the callee is the authority)
//     [ebp+0Ch] is read 4-byte x1
//   void: EAX at the ret is an incidental write and no caller reads it.
//   ECX is dereferenced at +0x134, the CUnit AI-record pointer. EDX indexes
//   TBldg::aSlotKind (`[ecx+edx*1+9h]`) and TBldg::aSlots (`[ecx+edx*4+18h]`).
void bldg_set_slot(CUnit *pUnit, int nSlot, BYTE bKind, intptr_t nValue);

// 0x484F40 town_add_unit_to_first_town (41 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   void: EAX at the ret is inherited and no caller reads it.
void town_add_unit_to_first_town(intptr_t nArg0);

// 0x484F70 bldg_clear_slot (30 B)
//   `ret`; EDX read before written -> a second register argument
//     ECX is dereferenced -> a pointer
//     EDX is read as 32 bits
//   no stack parameters
//   void: EAX at the ret is inherited and no caller reads it.
//   ECX is a `CUnit *` by the +0x134 read; the body then tests TBldg::bType == 3.
void bldg_clear_slot(CUnit *pUnit, int nSlot);

// 0x484F90 bldg_move_slot (44 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX is dereferenced -> a pointer
//     EDX is read as 32 bits
//   `ret 4h` -> one stack word
//     [ebp+08h] is read 4-byte x1
//   void: EAX is written on no path to a `ret`.
//   ECX is a `CUnit *` by the +0x134 read. EDX is the destination index and the
//   stack word the source -- read the copy direction off the oracle.
void bldg_move_slot(CUnit *pUnit, int nDstSlot, int nSrcSlot);

// 0x484FC0 town_needs_more_power (84 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   BOOL: every ret materialises 0 or 1 and 2 callers use the full EAX.
BOOL town_needs_more_power(int nArg0);

// 0x485020 town_tick (652 B)
//   thiscall-shaped free function: the town in ECX, no stack words, plain `ret`.
//   The one caller (town_expand_tick) discards EAX -> void.
void town_tick(TTown *pTown);

// 0x485390 AIUpdateSilo (109 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   void: EAX at the ret is inherited and no caller reads it.
void AIUpdateSilo(CUnit *pUnit);

// 0x4856A0 town_delete_if_empty (181 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   void: EAX at the ret is inherited and no caller reads it.
void town_delete_if_empty(intptr_t nArg0);

// 0x485760 unit_belongs_to_player (12 B)
//   `ret`; EDX read before written -> a second register argument
//     ECX is dereferenced -> a pointer
//     EDX is read as 32 bits
//   no stack parameters
//   BOOL: every path to a ret materialises an explicit value into EAX (setcc)
//   that the body never consumes, and a void body emits no dead immediate.
//   The body's address is taken 1 time(s) and it has no direct call site, so
//   it is a callback and the arity below is a lower bound: a register it
//   ignores is invisible here. The indirect call site is the authority --
//   read what the caller loads into ECX/EDX before `call <reg>`.
//   ECX is dereferenced at +0x4C, which is CUnit::player (a BYTE), so ECX is a
//   `CUnit *`. The body is `movzx ecx,[ecx+4Ch] / cmp ecx,edx / setz al`.
BOOL unit_belongs_to_player(CUnit *pUnit, int nPlayer);

// 0x485770 NewTown (198 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX is read as 32 bits
//     EDX is read as 16 bits
//   `ret 4h` -> one stack word
//     [ebp+08h] is only ever read 2 byte(s) -> 16-bit (for a parameter the callee is the authority)
//   int: 2 consuming callers use the full EAX and none narrows it.
intptr_t NewTown(int nArg0, WORD wArg1, WORD wArg2);

// 0x485970 town_queue_supply (315 B). Held; declared so gen thunks it for town_player_tick.
void town_queue_supply(TTown *pTown);

// 0x485AB0 town_queue_refinery (192 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   void: EAX is banker_demand_structure's leftover and no caller reads it.
void town_queue_refinery(TTown *pTown);

// 0x485CA0 sai_process_top_demand_tech (129 B)
//   `ret`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   no stack parameters
//   void: EAX at the ret is inherited and no caller reads it.
void sai_process_top_demand_tech(int nArg0, CUnit *pUnit);

// 0x485D30 sai_process_top_demand_upgrade (129 B)
//   `ret`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   no stack parameters
//   void: EAX at the ret is inherited and no caller reads it.
void sai_process_top_demand_upgrade(int nArg0, CUnit *pUnit);

// 0x485DC0 worker_build_failed (32 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   BOOL: every path to a ret materialises an explicit value into EAX (zero)
//   that the body never consumes, and a void body emits no dead immediate.
//   All 1 callers discard the result, so the callee is the only evidence.
BOOL worker_build_failed(intptr_t nArg0);

// 0x485DE0 town_gas_slot (148 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 4h` -> one stack word
//     [ebp+08h] is read 4-byte x1
//   int: 17 consuming callers use the full EAX and none narrows it.
CUnit *town_gas_slot(TTown *pTown, int nArg1, int nArg2);

// 0x485E80 town_gas_class (107 B)
//   `ret`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   no stack parameters
//   int: 9 consuming callers use the full EAX and none narrows it.
int town_gas_class(int nArg0, CUnit *pUnit);

// 0x485EF0 get_player_depot_type (37 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is dereferenced -> a pointer
//   no stack parameters
//   int: the one consuming caller uses the full EAX and none narrows it.
//   ECX is not a pointer: `lea eax, [ecx+ecx*8]` then `[eax*4+6726D1h]` is
//   gPlayerData (stride 36) indexed by ECX, a scaled-index address computation
//   rather than a dereference.
int get_player_depot_type(int nPlayer);

// 0x485F20 worker_harvest_order (114 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX is dereferenced -> a pointer
//     EDX is read as 32 bits
//   `ret 4h` -> one stack word
//     [ebp+08h] is read 4-byte x3
//   void: EAX at the ret is inherited and no caller reads it.
//   arg0's slot is written by the callee, a scratch use and not a reference.
void worker_harvest_order(CUnit *pUnit, CUnit *pTarget1, CUnit *pTarget2);

// 0x485FA0 add_request_to_town_queue (94 B)
//   `ret 10h`; EDX read before written -> a second register argument
//     ECX is read as 32 bits
//     EDX is read as 8 bits
//   `ret 10h` -> 4 stack words
//     [ebp+08h] is read 4-byte x1
//     [ebp+0Ch] is only ever read 2 byte(s) -> 16-bit (for a parameter the callee is the authority)
//     [ebp+10h] is only ever read 1 byte(s) -> 8-bit (for a parameter the callee is the authority)
//     [ebp+14h] is read 4-byte x1
//   void: EAX at the ret is inherited and no caller reads it.
//   The EDX parameter is 32-bit, not BYTE, and the difference shows in the
//   callers: the callee reads it one byte wide (`and dl, 3h`), but all three
//   forwarders set it up with a 32-bit immediate move, and declared BYTE MSVC
//   emits `mov dl, 2` and aiscript_tech comes out three bytes short. ECX is a
//   `TTown *`: the body opens `movzx eax, byte ptr [edi+18h]`, TTown::bPlayer,
//   then walks the request queue at `[edi+esi*4+3Ch]`, TTown::aQueue.
void add_request_to_town_queue(TTown *pTown, int nKind, int nPrio, WORD wId, BYTE bArg4, int nFlag);

// town_add_unit -- 0x484CD0, 568 B, oracle TU SAI_Build.cpp. Declared here
// because town_add_unit_to_first_town, town_move_record, town_rejoin_worker and
// town_rejoin_building all call or tail-jmp into it and no header of ours did.
// Two register arguments, no stack words, plain `ret`, and all six call sites
// discard EAX, so void. The pointees are the body's own: ECX is copied to ESI
// and dereferenced at `[esi+8h]`, TTown::pWorkerPool, and EDX to EDI and
// dereferenced at `movzx ecx, word ptr [edi+64h]`, CUnit::wType.
void town_add_unit(TTown *pTown, CUnit *pUnit);

// bldg_tick -- 0x487110, 349 B, oracle TU SAI_Build.cpp. No body of ours yet;
// declared because sai_process_top_demand_tech, sai_process_top_demand_upgrade
// and bldg_arm_nuke all call it and nothing else declared it. One register
// argument in ECX, no stack words, plain `ret`; one consuming caller takes the
// full EAX and does not narrow it, so `int`. ECX is a `CUnit *` on the callee's
// own evidence: it copies ECX to ESI and opens with
// `movzx eax, word ptr [esi+64h]`, CUnit::wType, which it dispatches on with a
// sub/sub/dec chain. No `__fastcall` keyword, as elsewhere in this file: /Gr
// already makes it the default for a free function.
int bldg_tick(CUnit *pUnit);

// town_merge_into_nearest -- 0x485840, 291 B, oracle TU SAI_Build.cpp. No body
// of ours yet; declared because town_delete_if_empty calls it and nothing else
// declared it. The two are in a call cycle, so the `call` shape is guaranteed
// and no inline directive is needed either way. One 32-bit register argument in
// ECX, no stack words, plain `ret`; one consuming caller takes the full EAX and
// does not narrow it, so `int`. The argument is a `TTown *`: the body copies ECX
// to EBX and dereferences it four times, at `[ebx+14h]` (pBldgs), `[ebx+18h]`
// (bPlayer) and `[ebx+20h]`/`[ebx+22h]` (nX/nY).
int town_merge_into_nearest(TTown *pTown);

// 0x486000 town_forget_unit (105 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is dereferenced -> a pointer
//   no stack parameters
//   void: EAX at the ret is inherited and no caller reads it.
//   ECX is a `CUnit *`: the first instruction is
//   `movzx eax, word ptr [ecx+64h]`, CUnit::unitType.
void town_forget_unit(CUnit *pUnit);

// 0x4860A0 unit_can_make_type (83 B)
//   `ret`; EDX read before written -> a second register argument
//     ECX is dereferenced -> a pointer
//     EDX is read as 16 bits
//   no stack parameters
//   BOOL: every ret materialises 0 or 1, and 7 consuming callers use the
//   full EAX.
BOOL unit_can_make_type(void *pArg0, WORD wArg1);

// 0x486130 player_mineral_fields (44 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   int: 2 consuming callers use the full EAX and none narrows it.
int player_mineral_fields(int nArg0);

// 0x486160 bldg_id (27 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   int: the one consuming caller uses the full EAX and none narrows it.
//   The argument is a pointer, not an int -- `sub ecx, 685578h` subtracts the
//   gBldgs_g base from it before the /44 magic. Byte-identical; the type is the
//   honest claim.
int bldg_id(TBldg *pBldg);

// 0x486180 bldg_ptr (17 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   The callee materialises its own return at both exits -- `xor eax, eax` on
//   the null path and `lea eax, [eax+68554Ch]` on the other -- so it needs no
//   caller to settle the return, and no caller consumes EAX yet. It is a
//   pointer, not an int: the value is a biased array address.
TBldg *bldg_ptr(int nId);

// 0x486340 town_move_record (103 B)
//   `ret`; EDX read before written -> a second register argument
//     ECX is dereferenced -> a pointer
//     EDX is read as 32 bits
//   no stack parameters
//   void: EAX at the ret is an incidental write and no caller reads it.
void town_move_record(CUnit *pUnit, CUnit *pOther);

// 0x4863B0 town_is_researching (88 B)
//   `ret`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   no stack parameters
//   BOOL: every ret materialises 0 or 1 and 1 callers use the full EAX.
// noinline: town_player_tick calls it; inlined there it is 88 B too long.
__declspec(noinline) BOOL town_is_researching(TTown *pTown, int nArg1);

// 0x486410 aiscript_is_groundmap (112 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 4h` -> one stack word
//     [ebp+08h] is read 4-byte x1
//   BOOL: every ret materialises 0 or 1 and 1 callers use the full EAX.
BOOL aiscript_is_groundmap(int nArg0, int nArg1, int nArg2);

// 0x486A20 sai_build_init (201 B)
//   `ret`; no register input, no stack parameters, plain `ret` -- every convention is byte-identical for a nullary function
//   no stack parameters
//   void: EAX is TSaveNode<TTown,100>::Init's leftover and no caller reads it.
void sai_build_init();

// 0x486AF0 SAI_Build_RemoveUnit (172 B)
//   `ret`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   no stack parameters
//   BOOL: every path to a ret materialises an explicit value into EAX (imm, zero)
//   that the body never consumes, and a void body emits no dead immediate.
//   All 3 callers discard the result, so the callee is the only evidence.
BOOL SAI_Build_RemoveUnit(CUnit *pUnit, int nArg1);

// 0x486BA0 town_rejoin_worker (69 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   void: EAX at the ret is inherited and no caller reads it.
void town_rejoin_worker(CUnit *pUnit);

// 0x486C50 get_town_race (44 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is dereferenced -> a pointer
//   no stack parameters
//   int: the one consuming caller uses the full EAX and none narrows it.
int get_town_race(void *pArg0);

// 0x487030 town_from_point (218 B)
//   `ret 8h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 8h` -> 2 stack words
//     [ebp+08h] through [ebp+0Ch] are each read 4-byte x1
//   Not void, even though no caller consumes EAX: the callee materialises its
//   own return. Its last tail is `pop ebx` / `mov eax, edi` / ... / `ret 8h`
//   with EDI holding the TTown it just found, and declared `void` MSVC merges
//   the second early-exit epilogue into the normal one and the body lands at
//   203 B against the oracle's 218 -- exactly the 15 bytes of the third tail.
//   The oracle has three distinct tails and `void` can only produce two.
//   `TTown *` is what EDI holds; `int` is byte-identical, and the pointer is
//   what the evidence supports.
TTown *town_from_point(int nArg0, int nArg1, int nArg2, int nArg3);

// 0x4872A0 town_first_town_xy (44 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX is read as 32 bits
//     EDX is dereferenced -> a pointer
//   `ret 4h` -> one stack word
//     [ebp+08h] is loaded and then dereferenced -> a pointer
//   BOOL: every ret materialises 0 or 1 and 2 callers use the full EAX.
BOOL town_first_town_xy(int nArg0, void *pArg1, void *pArg2);

// 0x4872D0 town_count (24 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   int: 2 consuming callers use the full EAX and none narrows it.
int town_count(int nArg0);

// 0x487510 town_id (32 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   int: the one consuming caller uses the full EAX and none narrows it.
//   The argument is a pointer: `sub ecx, 690160h` (the gTowns_g base) before the
//   /460 magic.
int town_id(TTown *pTown);

// 0x487550 town_need (37 B)
//   `ret Ch`; EDX read before written -> a second register argument
//     ECX is only ever forwarded to a callee, never read here
//     EDX is read as 32 bits
//   `ret Ch` -> 3 stack words
//     [ebp+08h] through [ebp+10h] are each read 4-byte x1
//   void: EAX is written on no path to a `ret`.
//   ECX is the `TTown *` it forwards to a callee untouched, so this body
//   shows no width for it.
void town_need(TTown *pTown, int nKind, int nId, int nArg3, int nArg4);

// 0x4877D0 town_ptr (20 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   int: the one consuming caller uses the full EAX and none narrows it.
//   The derived `int` has the right width and the wrong type: the value is
//   `&gTowns_g[nId-1]`, a pointer. Byte-identical either way; `TTown *` is the
//   claim the bytes support.
TTown *town_ptr(int nId);

// 0x4877F0 aiscript_get_region_size (158 B)
//   `ret`; EDX read before written -> a second register argument
//     ECX is dereferenced -> a pointer
//     EDX is read as 32 bits
//   no stack parameters
//   int: the one consuming caller uses the full EAX and none narrows it.
int aiscript_get_region_size(TTown *pTown, int nArg1);

// 0x487890 aiscript_tech (25 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX is only ever forwarded to a callee, never read here
//     EDX is read as 32 bits
//   `ret 4h` -> one stack word
//     [ebp+08h] is read 4-byte x1
//   void: EAX is add_request_to_town_queue's leftover and no caller reads it.
//   ECX is the `TTown *` it forwards untouched to add_request_to_town_queue,
//   so this body shows no width for it.
void aiscript_tech(TTown *pTown, int nTech, int nArg2);

// 0x4878B0 aiscript_upgrade (26 B)
//   `ret 8h`; EDX read before written -> a second register argument
//     ECX is only ever forwarded to a callee, never read here
//     EDX is read as 32 bits
//   `ret 8h` -> 2 stack words
//     [ebp+08h] through [ebp+0Ch] are each read 4-byte x1
//   void: EAX is add_request_to_town_queue's leftover and no caller reads it.
//   ECX is the `TTown *` it forwards to a callee untouched, so this body
//   shows no width for it.
void aiscript_upgrade(TTown *pTown, int nPrio, int nUpgrade, int nArg3);

// 0x4878D0 bldg_cannot_build (43 B)
//   `ret`; EDX read before written -> a second register argument
//     ECX is dereferenced -> a pointer
//     EDX is read as 32 bits
//   no stack parameters
//   BOOL: every ret materialises 0 or 1, and 9 consuming callers use the
//   full EAX.
BOOL bldg_cannot_build(CUnit *pUnit, int nArg1);

// 0x487900 town_rejoin_building (66 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   void: EAX at the ret is inherited and no caller reads it.
void town_rejoin_building(CUnit *pUnit);

// 0x487950 worker_id (27 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   int: the one consuming caller uses the full EAX and none narrows it.
//   The argument is a pointer: `sub ecx, 67F7B0h` (the gPeons_g base) before the
//   /24 magic.
int worker_id(TWorker *pWorker);

// 0x487970 worker_ptr (18 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is dereferenced -> a pointer
//   no stack parameters
//   The callee materialises its return at both exits (`xor eax,eax` /
//   `lea eax,[eax*8+67F798h]`), so it is a pointer and not void. ECX is a
//   1-based id, not a pointer: `lea eax, [ecx+ecx*2]` is a scaled-index address
//   computation rather than a dereference.
TWorker *worker_ptr(int nId);

// 0x487AC0 town_place_point (164 B)
//   `ret 8h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 8h` -> 2 stack words
//     [ebp+08h] is loaded and then dereferenced -> a pointer
//     [ebp+0Ch] is loaded and then dereferenced -> a pointer
//   BOOL: every ret materialises 0 or 1 and 1 callers use the full EAX.
BOOL town_place_point(TTown *pTown, int nArg1, int *pX, int *pY);

// 0x487B70 town_get_base_id (5 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is dereferenced -> a pointer
//   no stack parameters
//   int: 3 consuming callers use the full EAX and none narrows it.
//   ECX is dereferenced at +0x1C, inside TTown. `movzx eax, byte ptr [ecx+1Ch]`
//   is a zero-extending BYTE read, so the result is 0..255 and `int` is right.
int town_get_base_id(TTown *pTown);

// 0x487B80 town_hold_workers (18 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is dereferenced -> a pointer
//   no stack parameters
//   void: EAX at the ret is inherited and no caller reads it.
//   ECX is a `CUnit *` by the +0x134 read; then [+0x14] is the owning TTown.
void town_hold_workers(CUnit *pUnit);

// 0x487D30 bldg_train_top_demand (167 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 4h` -> one stack word
//     [ebp+08h] is loaded and then dereferenced -> a pointer
//   void: EAX at the ret is banker_spend_top_demand's or
//   banker_drop_top_demand's leftover and no caller reads it.
void bldg_train_top_demand(int nArg0, CUnit *pUnit, TBldg *pBldg);

// 0x488030 bldg_arm_nuke (194 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 4h` -> one stack word
//     [ebp+08h] is never read by the callee
//   BOOL: every path to a ret materialises an explicit value into EAX (imm, zero)
//   that the body never consumes, and a void body emits no dead immediate.
//   arg0 is never read here, so only its 4-byte footprint is proven.
//   All 1 callers discard the result, so the callee is the only evidence.
BOOL bldg_arm_nuke(int nArg0, CUnit *pUnit, intptr_t nArg2);
// The callees SAI_Main.cpp reaches that no header of ours declared. A name no
// header of ours declares is one the generator resolves out of the reference
// tree instead, silently and with that tree's spelling. None of these bodies is
// written yet.

// bldg_hand_over_product -- 0x487990, 301 B. Called by register_unit_for_ai. EDX is the
// produced unit: the body reads its +0x64 type and +0xDC flags.
void bldg_hand_over_product(CUnit *pUnit, CUnit *pParent);

// town_expand_tick -- 0x486C80, 499 B. Called by ai_periodic_timer_tick
// (SAI_Main.cpp). Nullary; EAX at the ret is inherited and no caller consumes
// it, so void.
//
// It is not `sai_recount_town_resources`, which sai_TownBuilder.h declares and
// which is also nullary and also void. The two differ by one digit of address,
// live in different oracle TUs (SAI_Build.cpp against sai_TownBuilder.cpp), are
// 499 B and 398 B, and ai_periodic_timer_tick calls them four lines apart. A
// thunk emitted for the wrong one of the two would compile, link and compare
// clean, so keep the two declarations apart.
void town_expand_tick();

// Declarations for bodies this TU defines that no header declared: a hooked
// free function needs one. Each pointee type is named by the fields the body
// dereferences. No `__fastcall`: /Gr makes it the default and a redundant one
// makes the name read as ambiguous.

// 0x4852B0 (222 B). The unit is tested for its type and life fraction, the
// town for its plan, so both parameters are pointers to those records.
BOOL town_repair_order(CUnit *pUnit, TTown *pTown);

// 0x485B70 (296 B). Declared because bldg_make_top_demand calls it and an
// unresolvable callee blocks every mark in the TU.
void sai_process_top_demand_unit(int nArg0, CUnit *pUnit, TBldg *pBldg);

// 0x487DE0 (581 B). `ret 8h`; bldg_tick tail-returns its result.
int bldg_make_top_demand(int nPlayer, CUnit *pUnit, TBldg *pBldg, int nFlag);

// 0x486480 (296 B). Reached cross-TU: SAI_Scripts_aiscript_progress calls it.
// The count arrives as a full 32-bit word and is compared unsigned.
void SAI_Build_aiscript_get_old_peons(TTown *pTown, unsigned nCount);

// 0x486E80 (272 B). Returns the town it created, or null. The rect is a
// rect32_t formed on the caller's stack and passed by address.
struct rect32_t;
TTown *town_from_area(int nPlayer, rect32_t *pRect, int nPlan);

// 0x487BA0 (362 B). Counts what the player already has of one unit type.
int player_unit_count(int nPlayer, int nUnitType);

// 0x488100 (283 B). The demand dispatcher's building arm.
BOOL bldg_build_addon(int nArg0, CUnit *pUnit, TBldg *pBldg);

// The build-data save codec, called from read_save_ai_data and
// sai_write_save_data. One register argument, the save file; the result is a
// success flag the callers read as a full dword.
int sai_read_build_data(intptr_t nFile);
int sai_write_build_data(intptr_t nFile);

// town_queue_idle_building -- 0x4849C0, 602 B. `ret 4`: ECX the player, EDX the building,
// one stack word passed through to banker_demand_unit_for_captain. Called by bldg_tick.
int __fastcall town_queue_idle_building(int nPlayer, CUnit *pUnit, int nArg2);

// town_unit_count -- 0x4872F0, 508 B. `ret 4`: ECX the town, EDX the unit type,
// the stack word a completed-only flag. Called by SAI_Scripts_aiscript_progress.
int __fastcall town_unit_count(TTown *pTown, int nUnitType, int bCompleted);

// worker_build_top_demand -- 0x485400, 658 B. ECX the town, EDX the worker, plain `ret`.
// Returns 1 when the worker was given a build order; worker_tick tests it.
int worker_build_top_demand(TTown *pTown, CUnit *pUnit);
