// SAI_Build.cpp -- the AI's build and town system: the per-player town list,
// the worker and building records the towns own, the request queue the AI script
// VM feeds, and the save/load codecs for all three. The aiscript_* entry points
// are opcodes the script VM dispatches into; a sub_* name is positional and
// means only that no approved naming source names it. Functions are in oracle
// VA order.

// Three 1-based pools and a per-player index:
//   gPeons_g       @0x67F7B0  TWorker[1000] + free head   (0x5DC4)
//   gBldgs_g       @0x685578  TBldg[1000]   + free head   (0xABE8)
//   gTowns_g       @0x690160  TTown[100]    + free head   (0xB3B8)
//   gPlayerTowns_g @0x69B518  TPlayerTowns[8]              (0x40)

// Every stride is derived twice, from the two halves of one bijection, and
// every extent agrees with the next name in the naming sheet. The layouts, the
// field evidence and the full derivation live in src117/h/SAI_Build.h; read
// that header before adding a field here.
//
// The ids are 1-based in all three pools, and each *_ptr accessor folds a
// one-element bias into its base address, so 0x67F798, 0x68554C and 0x68FF94
// are not globals but &pool[-1]. Do not register them.

// Which bodies live in this TU matters in both directions, and both have to be
// re-checked after a change here:
//   * A callee defined here that a landed body calls with a real `call` can be
//     spliced into it by /Ob2. The oracle has WPO off, so a cross-TU call
//     cannot be inlined, and leaving such a callee as an oracle thunk
//     reproduces the oracle for free.
//   * A body whose size is exactly caller plus callee to the byte needs the
//     callee in this TU. The strong case is town_expand_tick (499 B, not
//     landed) containing player_mineral_fields (44 B, landed): whoever writes
//     0x486C80 has to write that code out rather than calling it.
//   * TBldg::PostLoad and TBldg::PreSave inline town_ptr and town_id, so both
//     are defined here and removing either would break the codecs.

// A held body stays in the TU behind `#ifndef OC_PAYLOAD`, so the plain compile
// keeps checking it while the payload calls the oracle's copy instead.
//
// With the globals spelled as literals, MSVC folds `p - base` into
// `add p, -base`, so the baked slot carries the two's complement of the address
// and the oracle's `sub` becomes an `add`; the *_ptr accessors lose one byte the
// same way, `lea eax,[eax+bias]` folding to `add eax, bias`. That is a property
// of the stride and of the scaffolding, not of this source, and there is no
// source fix for it.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../port/data/port_formats.h"  // Hyperion: PortSave117Decompress, PortSave117Compress
#include "../port/gameplay/port_ai.h"  // Hyperion: a computer's level
#include "../gen/save117.h"
#include "../h/SAI_Build.h"
#include "../h/sai.h"
#include "../h/net_cmds.h"        // PlayerData (gPlayerData), set_allowed_err
#include "../h/saveload.h"        // get_tech_researched
#include "../h/TAI_Captain.h"     // captain_id_to_ptr / captain_ptr_to_id / patrol_pick_area
#include "../h/TAI_GuardPost.h"   // guard_ptr / guard_id
#include "../h/CUnitSearches.h"   // town_repair_worker's declaration
#include "../h/SAI_Banker.h"      // town_clear_repair_worker / town_pending_costs / the staged-cost readers
#include "../h/CSprite.h"         // CUnit::AIUpdateOverlord reads CSprite::wPosX/wPosY
#include "../h/statbtn.h"        // TTAllowed / ResearchAllowed / UpgradeAllowed
#include "../h/CUnitMisc.h"       // the FREE IsEnemyUnit (0x429110)
#include "../h/CUnitFind.h"       // get_units_in_region / finish_unit_iteration
#include "../h/rect16.h"          // the WORD rect get_units_in_rect takes
#include "../h/rect32.h"          // town_from_area's caller passes a long rect
#include "../h/CTarget.h"         // veclen
#include "../h/SAI_Main.h"        // SAI_RemoveUnit
#include "../h/sai_TownBuilder.h" // ai_get_build_location_at_unit / ai_get_build_location_near / point16_t
#include "../h/CUnitCombat.h"     // ai_update_unit_spells
#include "../h/count.h"           // count_race / count_get_supplies_used
#include "../h/SAI_Scripts.h"     // aiscript_run_panic
#include "../h/sai_PathCreate.h"  // ep_glob_t, the region block epGlob points at
#include <string.h>               // memset
#include "../gen/globals117.h"
#include "../h/handicap.h"  // Hyperion: a player's handicap
#include "../h/mutators.h"  // Hyperion: the supply boost

// The globals this TU reaches, dual-spelled with one guard per name: the
// literal arm is what the payload ships. The guard is per name and never one
// over the whole block, because the generator emits only registered rows'
// globals, so the set grows and a single guard would undeclare every other name
// under it.
//
// gPlayerData is not here: src117/gen/globals117.h already emits it as
// PlayerData[8] @0x6726C8, and re-spelling it is a C2369.

// Hyperion: each is declared as its whole pool, the free head after the
// array included, so that the engine's own storage for it (gen/wide117.h) has
// room for the head; declared as the array alone, the head of one pool was the
// first record of the next.
#ifdef OC_DLL_MODE
#define gPeons_g        (*reinterpret_cast<TWorkerPool *>(OC_VA(0x67F7B0u)))
#else
extern TWorkerPool gPeons_g;  // 0x67F7B0
#endif

#ifdef OC_DLL_MODE
#define gBldgs_g        (*reinterpret_cast<TBldgPool *>(OC_VA(0x685578u)))
#else
extern TBldgPool gBldgs_g;  // 0x685578
#endif

#ifdef OC_DLL_MODE
#define gTowns_g        (*reinterpret_cast<TTownPool *>(OC_VA(0x690160u)))
#else
extern TTownPool gTowns_g;  // 0x690160
#endif

#ifdef OC_DLL_MODE
#define gPlayerTowns_g  (reinterpret_cast<TPlayerTowns *>(OC_VA(0x69B518u)))
#else
extern PlayerTable<TPlayerTowns, PlayerTagTowns, 8> gPlayerTowns_g;      // 0x69B518
#endif

// epGlob @0x69B584 -- the pathing and region block's base. Registered, but not
// emitted by src117/gen/globals117.h, since no landed row lists this address,
// so the dual spelling here is what gives the shipped form its literal. If the
// generator's set ever widens to include it, this block has to go in the same
// commit, or the two declarations collide in a C2371.
#ifdef OC_DLL_MODE
#define epGlob          (*reinterpret_cast<void **>(OC_VA(0x69B584u)))
#else
extern void *epGlob;                        // 0x69B584
#endif

// dword_69B558 @0x69B558 -- the re-entrancy guard town_merge_into_nearest
// raises while it moves a town's units. The naming sheet's own placeholder
// name, kept as it stands. It sits exactly one past gPlayerTowns_g's 0x40
// bytes, which is what bounds it, and nothing registers it, so this dual
// spelling is what the shipped form bakes.
#ifdef OC_DLL_MODE
#define dword_69B558    (*reinterpret_cast<DWORD *>(OC_VA(0x69B558u)))
#else
extern DWORD dword_69B558;                  // 0x69B558
#endif

// dword_685574 @0x685574 -- sai_build_init's last store. The naming sheet's own
// placeholder name, kept rather than coined over. It sits between gPeons_g's
// free head and gBldgs_g, so the sheet bounds it at exactly 4 bytes.
#ifdef OC_DLL_MODE
#define dword_685574    (*reinterpret_cast<DWORD *>(OC_VA(0x685574u)))
#else
extern DWORD dword_685574;                  // 0x685574
#endif

// glCounts_g @0x529604 -- DWORD[4][228][12], the spelling TrigEnum.cpp already
// carries. town_needs_more_power reads slice [0] and slice [1] at wType 156.
#ifdef OC_DLL_MODE
#define glCounts_g      (reinterpret_cast<DWORD (*)[228][12]>(OC_VA(0x529604u)))
#else
extern PlayerUnitCountsGrid glCounts_g;        // 0x529604
#endif

// The TSaveNode / TSaveContainer specializations are declared in SAI_Build.h.

// The pool-base and list-head pairs inside TTown are sub-objects, and the
// difference is visible in the bytes. TSaveContainer<TBldg,1000>::New
// (0x483E40) and ::Free (0x483B10) read their pool as `[this]` and their list
// head as `[this+4]`, so TTown +0x08/+0x0C and +0x10/+0x14 are two sub-objects
// rather than four flat fields, and gPlayerTowns_g is an array of the same two
// words. Reached through each sub-object's own pointer, MSVC may not assume the
// second pair's load cannot alias the first pair's store, and does not hoist
// it. Spelled flat against `this` it does hoist: TTown::PostLoad then misses by
// one delta, ::PreSave by two, and the inlined Free in town_delete_if_empty by
// ten register-allocation bytes. `(BYTE *)this + 0xNN` casts,
// `*(DWORD *)&field` casts and a `volatile TTown *` alias were all tried and
// none of them works.

// It is a local cast here rather than a change to TTown: turning those four
// fields into two sub-objects would re-spell every accessor in bodies that
// already match, for no bytes. The finding is recorded in
// src117/h/SAI_Build.h for whoever takes it.
struct pool_list_pair_t
{
    /*0x00*/ DWORD_PTR dwPool;   // the pool base, read as `[this]`   in TSaveContainer<TBldg,1000>::Free (0x483B10)
    /*0x04*/ DWORD_PTR dwHead;   // the list head, read as `[this+4]` in TSaveContainer<TBldg,1000>::New  (0x483E40)
};
OC_X86_LAYOUT(sizeof(pool_list_pair_t) == 8, "TSaveContainer<T,N> sub-object stride");
static_assert(offsetof(pool_list_pair_t, dwPool) == 0x00, "pool_list_pair_t.dwPool = [this]");
OC_X86_LAYOUT(offsetof(pool_list_pair_t, dwHead) == 0x04, "pool_list_pair_t.dwHead = [this+4]");

// NewTown's inlined list push. Not an oracle row of its own: it exists because
// the redundant second `test esi,esi` at +0x2D needs two separate inlined
// callees.
static void town_list_prepend(TPlayerTowns *pC, TTown *p)
{
    if (p == 0)
        return;
    p->pPrev = 0;
    p->pNext = pC->pTowns;
    if (pC->pTowns != 0)
        pC->pTowns->pPrev = p;
    pC->pTowns = p;
}

// The free-list push is TSaveContainer<TTown,100>::Free inlined, after the
// TSaveContainer<TBldg,1000>::Free row at 0x483B10. Written flat against `this`
// the body misses by 10 register-allocation bytes; as a member of the container
// sub-object it is exact.
static void town_container_free(TPlayerTowns *pC, TTown *p)
{
    if (pC->pTowns == p)
        pC->pTowns = p->pNext;
    if (p->pPrev != 0)
        p->pPrev->pNext = p->pNext;
    if (p->pNext != 0)
        p->pNext->pPrev = p->pPrev;
    TTownPool *pPool = pC->pPool;
    p->pPrev = 0;
    p->pNext = pPool->pFreeHead;
    if (pPool->pFreeHead != 0)
        pPool->pFreeHead->pPrev = p;
    pPool->pFreeHead = p;
}
// Unlink a building record from its town's list and push it on the pool's free
// list. The oracle never inlines either Free; without noinline /Ob2 splices
// both into SAI_Build_RemoveUnit.
__declspec(noinline) void TSaveContainer<TBldg,1000>::Free(TBldg *pNode)
{
    if (pHead == pNode)
        pHead = pNode->pNext;
    if (pNode->pPrev != 0)
        pNode->pPrev->pNext = pNode->pNext;
    if (pNode->pNext != 0)
        pNode->pNext->pPrev = pNode->pPrev;
    TSaveNode<TBldg,1000> *pSave = pPool;
    pNode->pPrev = 0;
    pNode->pNext = pSave->pFree;
    if (pSave->pFree != 0)
        pSave->pFree->pPrev = pNode;
    pSave->pFree = pNode;
}

// The worker-record twin of the Free above.
__declspec(noinline) void TSaveContainer<TWorker,1000>::Free(TWorker *pNode)
{
    if (pHead == pNode)
        pHead = pNode->pNext;
    if (pNode->pPrev != 0)
        pNode->pPrev->pNext = pNode->pNext;
    if (pNode->pNext != 0)
        pNode->pNext->pPrev = pNode->pPrev;
    TSaveNode<TWorker,1000> *pSave = pPool;
    pNode->pPrev = 0;
    pNode->pNext = pSave->pFree;
    if (pSave->pFree != 0)
        pSave->pFree->pPrev = pNode;
    pSave->pFree = pNode;
}

// The per-record clears the pool Init and PreSave inline. The oracle clears
// TWorker +0x08 as one dword.
static void bldg_clear(TBldg *p)
{
    p->bType = 0;
    p->pUnit = 0;
    p->pTown = 0;
    memset(p->aSlotKind, 0, sizeof(p->aSlotKind));
    memset(p->aSlots, 0, sizeof(p->aSlots));
}

static void worker_clear(TWorker *p)
{
    *(DWORD *)&p->bType = 0;
    p->pUnit10 = 0;
    p->pTown = 0;
    p->dwUnknown0C = 0;
}

// Thread all 1000 records onto the free list and clear them. The loop body
// must set pPrev before pNext, or the pointer walk rebases.
void TSaveNode<TBldg,1000>::Init()
{
    aNodes[0].pPrev = 0;
    aNodes[0].pNext = &aNodes[1];
    bldg_clear(&aNodes[0]);
    for (int i = 1; i < g_aiPools.records - 1; ++i)  // Hyperion: 1000 in 1.17
    {
        aNodes[i].pPrev = &aNodes[i - 1];
        aNodes[i].pNext = &aNodes[i + 1];
        bldg_clear(&aNodes[i]);
    }
    aNodes[g_aiPools.records - 1].pNext = 0;
    aNodes[g_aiPools.records - 1].pPrev = &aNodes[g_aiPools.records - 2];
    bldg_clear(&aNodes[g_aiPools.records - 1]);
    pFree = aNodes;
}

// A town's two record lists are TSaveContainer sub-objects; reaching them
// through the container type is what orders the four stores.
static void town_init(TTown *p)
{
    memset(&p->bPlayer, 0, sizeof(TTown) - offsetof(TTown, bPlayer));  // Hyperion: the same fields, 0x1B4 bytes in 1.17
    TSaveContainer<TWorker,1000> *pW = (TSaveContainer<TWorker,1000> *)&p->pWorkerPool;
    pW->pPool = (TSaveNode<TWorker,1000> *)&gPeons_g;
    pW->pHead = 0;
    TSaveContainer<TBldg,1000> *pB = (TSaveContainer<TBldg,1000> *)&p->pBldgPool;
    pB->pPool = (TSaveNode<TBldg,1000> *)&gBldgs_g;
    pB->pHead = 0;
}

// Thread all 100 towns onto the free list and reset each one.
void TSaveNode<TTown,100>::Init()
{
    aNodes[0].pPrev = 0;
    aNodes[0].pNext = &aNodes[1];
    town_init(&aNodes[0]);
    for (int i = 1; i < g_aiPools.towns - 1; ++i)  // Hyperion: 100 in 1.17
    {
        aNodes[i].pPrev = &aNodes[i - 1];
        aNodes[i].pNext = &aNodes[i + 1];
        town_init(&aNodes[i]);
    }
    aNodes[g_aiPools.towns - 1].pNext = 0;
    aNodes[g_aiPools.towns - 1].pPrev = &aNodes[g_aiPools.towns - 2];
    town_init(&aNodes[g_aiPools.towns - 1]);
    pFree = aNodes;
}

// Thread all 1000 worker records onto the free list and clear them.
void TSaveNode<TWorker,1000>::Init()
{
    aNodes[0].pPrev = 0;
    aNodes[0].pNext = &aNodes[1];
    worker_clear(&aNodes[0]);
    for (int i = 1; i < g_aiPools.records - 1; ++i)  // Hyperion: 1000 in 1.17
    {
        aNodes[i].pPrev = &aNodes[i - 1];
        aNodes[i].pNext = &aNodes[i + 1];
        worker_clear(&aNodes[i]);
    }
    aNodes[g_aiPools.records - 1].pNext = 0;
    aNodes[g_aiPools.records - 1].pPrev = &aNodes[g_aiPools.records - 2];
    worker_clear(&aNodes[g_aiPools.records - 1]);
    pFree = aNodes;
}

// Pop a building record off the pool's free list and push it on this list.
// pNode must start at 0 and be bound inside the first guard, or MSVC folds
// the second null test into the first.
__declspec(noinline) TBldg *TSaveContainer<TBldg,1000>::New()
{
    TSaveNode<TBldg,1000> *pSave = pPool;
    TBldg *pNode = 0;
    if (pSave->pFree != 0)
    {
        pNode = pSave->pFree;
        pSave->pFree = pSave->pFree->pNext;
        if (pSave->pFree != 0)
            pSave->pFree->pPrev = 0;
    }
    if (pNode != 0)
    {
        pNode->pPrev = 0;
        pNode->pNext = pHead;
        if (pHead != 0)
            pHead->pPrev = pNode;
        pHead = pNode;
        return pNode;
    }
    return 0;
}

// Turn the saved 1-based ids of every building record back into pointers.
void TSaveNode<TBldg,1000>::PostLoad()
{
    for (int i = 0; i < g_aiPools.records; ++i)  // Hyperion: 1000 in 1.17
    {
        aNodes[i].PostLoad();
        aNodes[i].pNext = aNodes[i].pNext == 0 ? 0 : (TBldg *)((DWORD_PTR)aNodes[i].pNext * sizeof(TBldg) - sizeof(TBldg) + (DWORD_PTR)this);
        aNodes[i].pPrev = aNodes[i].pPrev == 0 ? 0 : (TBldg *)((DWORD_PTR)aNodes[i].pPrev * sizeof(TBldg) - sizeof(TBldg) + (DWORD_PTR)this);
    }
    pFree = pFree == 0 ? 0 : (TBldg *)((DWORD_PTR)pFree * sizeof(TBldg) - sizeof(TBldg) + (DWORD_PTR)this);
}

// Turn the saved 1-based ids of every town back into pointers.
void TSaveNode<TTown,100>::PostLoad()
{
    for (int i = 0; i < g_aiPools.towns; ++i)  // Hyperion: 100 in 1.17
    {
        aNodes[i].PostLoad();
        aNodes[i].pNext = aNodes[i].pNext == 0 ? 0 : (TTown *)((DWORD_PTR)aNodes[i].pNext * sizeof(TTown) - sizeof(TTown) + (DWORD_PTR)this);
        aNodes[i].pPrev = aNodes[i].pPrev == 0 ? 0 : (TTown *)((DWORD_PTR)aNodes[i].pPrev * sizeof(TTown) - sizeof(TTown) + (DWORD_PTR)this);
    }
    pFree = pFree == 0 ? 0 : (TTown *)((DWORD_PTR)pFree * sizeof(TTown) - sizeof(TTown) + (DWORD_PTR)this);
}

// Turn the saved ids of every worker record back into pointers.
void TSaveNode<TWorker,1000>::PostLoad()
{
    for (int i = 0; i < g_aiPools.records; ++i)  // Hyperion: 1000 in 1.17
    {
        aNodes[i].pUnit10 = CUnit::FromId((DWORD)(DWORD_PTR)aNodes[i].pUnit10);
        aNodes[i].pTown = town_ptr((int)(intptr_t)aNodes[i].pTown);
        aNodes[i].pNext = aNodes[i].pNext == 0 ? 0 : (TWorker *)((DWORD_PTR)aNodes[i].pNext * sizeof(TWorker) - sizeof(TWorker) + (DWORD_PTR)this);
        aNodes[i].pPrev = aNodes[i].pPrev == 0 ? 0 : (TWorker *)((DWORD_PTR)aNodes[i].pPrev * sizeof(TWorker) - sizeof(TWorker) + (DWORD_PTR)this);
    }
    pFree = pFree == 0 ? 0 : (TWorker *)((DWORD_PTR)pFree * sizeof(TWorker) - sizeof(TWorker) + (DWORD_PTR)this);
}

static void town_clear(TTown *p)
{
    memset(&p->bPlayer, 0, sizeof(TTown) - offsetof(TTown, bPlayer));  // Hyperion: the same fields, 0x1B4 bytes in 1.17
}

// Clear the free records, then turn every building record's pointers into
// 1-based ids for the save file.
void TSaveNode<TBldg,1000>::PreSave()
{
    for (TBldg *p = pFree; p != 0; p = p->pNext)
        bldg_clear(p);
    for (int i = 0; i < g_aiPools.records; ++i)  // Hyperion: 1000 in 1.17
    {
        aNodes[i].PreSave();
        aNodes[i].pNext = aNodes[i].pNext == 0 ? 0 : (TBldg *)(((DWORD_PTR)aNodes[i].pNext - (DWORD_PTR)this) / sizeof(TBldg) + 1);
        aNodes[i].pPrev = aNodes[i].pPrev == 0 ? 0 : (TBldg *)(((DWORD_PTR)aNodes[i].pPrev - (DWORD_PTR)this) / sizeof(TBldg) + 1);
    }
    pFree = pFree == 0 ? 0 : (TBldg *)(((DWORD_PTR)pFree - (DWORD_PTR)this) / sizeof(TBldg) + 1);
}

// Clear the free towns, then turn every town's pointers into ids.
void TSaveNode<TTown,100>::PreSave()
{
    for (TTown *p = pFree; p != 0; p = p->pNext)
        town_clear(p);
    for (int i = 0; i < g_aiPools.towns; ++i)  // Hyperion: 100 in 1.17
    {
        aNodes[i].PreSave();
        aNodes[i].pNext = aNodes[i].pNext == 0 ? 0 : (TTown *)(((DWORD_PTR)aNodes[i].pNext - (DWORD_PTR)this) / sizeof(TTown) + 1);
        aNodes[i].pPrev = aNodes[i].pPrev == 0 ? 0 : (TTown *)(((DWORD_PTR)aNodes[i].pPrev - (DWORD_PTR)this) / sizeof(TTown) + 1);
    }
    pFree = pFree == 0 ? 0 : (TTown *)(((DWORD_PTR)pFree - (DWORD_PTR)this) / sizeof(TTown) + 1);
}

// Clear the free worker records, then turn every record's pointers into ids.
void TSaveNode<TWorker,1000>::PreSave()
{
    for (TWorker *p = pFree; p != 0; p = p->pNext)
        worker_clear(p);
    for (int i = 0; i < g_aiPools.records; ++i)  // Hyperion: 1000 in 1.17
    {
        aNodes[i].pUnit10 = (CUnit *)(intptr_t)aNodes[i].pUnit10->GetUnitID();
        aNodes[i].pTown = (TTown *)(intptr_t)town_id(aNodes[i].pTown);
        aNodes[i].pNext = aNodes[i].pNext == 0 ? 0 : (TWorker *)(((DWORD_PTR)aNodes[i].pNext - (DWORD_PTR)this) / sizeof(TWorker) + 1);
        aNodes[i].pPrev = aNodes[i].pPrev == 0 ? 0 : (TWorker *)(((DWORD_PTR)aNodes[i].pPrev - (DWORD_PTR)this) / sizeof(TWorker) + 1);
    }
    pFree = pFree == 0 ? 0 : (TWorker *)(((DWORD_PTR)pFree - (DWORD_PTR)this) / sizeof(TWorker) + 1);
}

// The bodies, in oracle VA order.

// 0x484010  TBldg::PostLoad

// noinline: the oracle calls it from TSaveNode<TBldg,1000>::PostLoad.
__declspec(noinline) void TBldg::PostLoad()
{
    this->pUnit = CUnit::FromId((DWORD)(DWORD_PTR)this->pUnit);
    this->pTown = town_ptr((int)(intptr_t)this->pTown);

    for (unsigned int i = 0; i < 5; ++i)
    {
        switch (this->aSlotKind[i])
        {
        case 0:
            break;
        case 1:
            this->aSlots[i] = (DWORD_PTR)captain_id_to_ptr((int)this->aSlots[i]);
            break;
        case 2:
            this->aSlots[i] = (DWORD_PTR)guard_ptr((int)this->aSlots[i]);
            break;
        case 3:
            this->aSlots[i] = (DWORD_PTR)town_ptr((int)this->aSlots[i]);
            break;
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
            this->aSlots[i] = 0;
            break;
        default:
            this->aSlots[i] = 0;
            break;
        }
    }
}

// 0x4840C0  TTown::PostLoad
// 0x4840C0  TTown::PostLoad  124 B
// noinline: the oracle calls it from TSaveNode<TTown,100>::PostLoad.
__declspec(noinline) void TTown::PostLoad()
{
    pool_list_pair_t *pW = (pool_list_pair_t *)&pWorkerPool;
    pool_list_pair_t *pB = (pool_list_pair_t *)&pBldgPool;

    DWORD dwWorkerId = (DWORD)pW->dwHead;
    pW->dwPool = (DWORD_PTR)&gPeons_g;
    TWorker *pWorker;
    if (dwWorkerId == 0)
        pWorker = 0;
    else
        pWorker = &gPeons_g.aWorkers[dwWorkerId - 1];
    pW->dwHead = (DWORD_PTR)pWorker;

    DWORD dwBldgId = (DWORD)pB->dwHead;
    pB->dwPool = (DWORD_PTR)&gBldgs_g;
    TBldg *pBldg;
    if (dwBldgId == 0)
        pBldg = 0;
    else
        pBldg = &gBldgs_g.aBldgs[dwBldgId - 1];
    pB->dwHead = (DWORD_PTR)pBldg;

    pUnit2C = CUnit::FromId((DWORD)(DWORD_PTR)pUnit2C);
    pUnit30 = CUnit::FromId((DWORD)(DWORD_PTR)pUnit30);
    pUnit34 = CUnit::FromId((DWORD)(DWORD_PTR)pUnit34);
    pUnit38 = CUnit::FromId((DWORD)(DWORD_PTR)pUnit38);
    pUnit28 = CUnit::FromId((DWORD)(DWORD_PTR)pUnit28);
    pUnit24 = CUnit::FromId((DWORD)(DWORD_PTR)pUnit24);
}

// 0x484380  TBldg::PreSave
// 0x484380, 136 B.
void TBldg::PreSave()
{
    this->pUnit = (CUnit *)(intptr_t)this->pUnit->GetUnitID();
    this->pTown = (TTown *)(intptr_t)town_id(this->pTown);

    for (unsigned int i = 0; i < 5; ++i)
    {
        switch (this->aSlotKind[i])
        {
        case 1:
            this->aSlots[i] = (DWORD)captain_ptr_to_id((TAI_Captain *)this->aSlots[i]);
            break;
        case 2:
            this->aSlots[i] = (DWORD)guard_id((TGuard *)this->aSlots[i]);
            break;
        case 3:
            this->aSlots[i] = (DWORD)town_id((TTown *)this->aSlots[i]);
            break;
        default:
            break;
        }
    }
}

// 0x484410  TTown::PreSave
// 0x484410  TTown::PreSave  141 B
// noinline: the oracle calls it from TSaveNode<TTown,100>::PreSave.
__declspec(noinline) void TTown::PreSave()
{
    pool_list_pair_t *pW = (pool_list_pair_t *)&pWorkerPool;
    pool_list_pair_t *pB = (pool_list_pair_t *)&pBldgPool;

    DWORD_PTR dwWorkerId;
    if (pW->dwHead == 0)
        dwWorkerId = 0;
    else
        dwWorkerId = (pW->dwHead - pW->dwPool) / sizeof(TWorker) + 1;
    pW->dwHead = dwWorkerId;
    pW->dwPool = 0;

    DWORD_PTR dwBldgId;
    if (pB->dwHead == 0)
        dwBldgId = 0;
    else
        dwBldgId = (pB->dwHead - pB->dwPool) / sizeof(TBldg) + 1;
    pB->dwHead = dwBldgId;
    pB->dwPool = 0;

    pUnit2C = (CUnit *)(intptr_t)pUnit2C->GetUnitID();
    pUnit30 = (CUnit *)(intptr_t)pUnit30->GetUnitID();
    pUnit34 = (CUnit *)(intptr_t)pUnit34->GetUnitID();
    pUnit38 = (CUnit *)(intptr_t)pUnit38->GetUnitID();
    pUnit28 = (CUnit *)(intptr_t)pUnit28->GetUnitID();
    pUnit24 = (CUnit *)(intptr_t)pUnit24->GetUnitID();
}

// 0x4844A0  bldg_set_slot
// 0x4844A0, 27 B.
void bldg_set_slot(CUnit *pUnit, int nSlot, BYTE bKind, intptr_t nValue)
{
    TBldg *pBldg = (TBldg *)pUnit->dwUnion134;
    pBldg->aSlotKind[nSlot] = bKind;
    pBldg->aSlots[nSlot] = nValue;
}

// Load the three pools and the per-player town lists from a save, then relink
// them. Returns 0 if any block fails to decompress. Hyperion: each in 1.17's
// layout, and so in sai_write_build_data.
int sai_read_build_data(intptr_t nFile)
{
    // Hyperion: a grown pool is its records and its head
    TSaveNode<TWorker,1000> *pWorkers = (TSaveNode<TWorker,1000> *)&gPeons_g;
    TSaveNode<TBldg,1000> *pBldgs = (TSaveNode<TBldg,1000> *)&gBldgs_g;
    TSaveNode<TTown,100> *pTowns = (TSaveNode<TTown,100> *)&gTowns_g;
    if (!AiPoolLoad<TWorker, AI_RECORDS_117>(SAVE117_WorkerPool, SAVE117_Worker, pWorkers->aNodes, g_aiPools.records,
                                             &pWorkers->pFree, (void *)nFile))
        return 0;
    pWorkers->PostLoad();
    if (!AiPoolLoad<TBldg, AI_RECORDS_117>(SAVE117_BldgPool, SAVE117_Bldg, pBldgs->aNodes, g_aiPools.records,
                                           &pBldgs->pFree, (void *)nFile))
        return 0;
    pBldgs->PostLoad();
    if (!AiPoolLoad<TTown, AI_TOWNS_117>(SAVE117_TownPool, SAVE117_Town, pTowns->aNodes, g_aiPools.towns,
                                         &pTowns->pFree, (void *)nFile))
        return 0;
    pTowns->PostLoad();
    if (!PortSave117Decompress(SAVE117_PlayerTowns, gPlayerTowns_g.a, 8, (void *)nFile))
        return 0;
    // Hyperion: the nations past 7
    if (g_hxPlayers && !PortSave117Decompress(SAVE117_PlayerTowns, gPlayerTowns_g.Extra(), gPlayerTowns_g.kExtraCount, (void *)nFile))
        return 0;
    for (int i = 0; i < PlayerLobbySlots(); ++i)
    {
        gPlayerTowns_g[i].pPool = &gTowns_g;
        gPlayerTowns_g[i].pTowns = town_ptr((int)(intptr_t)gPlayerTowns_g[i].pTowns);
    }
    dword_685574 = 0;
    return 1;
}

// Save the three pools and the per-player town lists, relinking each block
// after it is written. Returns 0 if any block fails to compress.
int sai_write_build_data(intptr_t nFile)
{
    // Hyperion: a grown pool is its records and its head
    TSaveNode<TWorker,1000> *pWorkers = (TSaveNode<TWorker,1000> *)&gPeons_g;
    TSaveNode<TBldg,1000> *pBldgs = (TSaveNode<TBldg,1000> *)&gBldgs_g;
    TSaveNode<TTown,100> *pTowns = (TSaveNode<TTown,100> *)&gTowns_g;
    pWorkers->PreSave();
    int bOk = AiPoolSave<TWorker, AI_RECORDS_117>(SAVE117_WorkerPool, SAVE117_Worker, pWorkers->aNodes,
                                                  g_aiPools.records, pWorkers->pFree, (void *)nFile);
    pWorkers->PostLoad();
    pBldgs->PreSave();
    if (!AiPoolSave<TBldg, AI_RECORDS_117>(SAVE117_BldgPool, SAVE117_Bldg, pBldgs->aNodes, g_aiPools.records,
                                           pBldgs->pFree, (void *)nFile))
        bOk = 0;
    pBldgs->PostLoad();
    pTowns->PreSave();
    if (!AiPoolSave<TTown, AI_TOWNS_117>(SAVE117_TownPool, SAVE117_Town, pTowns->aNodes, g_aiPools.towns,
                                         pTowns->pFree, (void *)nFile))
        bOk = 0;
    pTowns->PostLoad();
    for (int i = 0; i < PlayerLobbySlots(); ++i)  // Hyperion: 8 in 1.17
    {
        gPlayerTowns_g[i].pTowns = gPlayerTowns_g[i].pTowns == 0 ? 0 : (TTown *)(((DWORD_PTR)gPlayerTowns_g[i].pTowns - (DWORD_PTR)gPlayerTowns_g[i].pPool) / sizeof(TTown) + 1);
        gPlayerTowns_g[i].pPool = 0;
    }
    if (!PortSave117Compress(SAVE117_PlayerTowns, gPlayerTowns_g.a, 8, (void *)nFile))
        bOk = 0;
    if (g_hxPlayers && !PortSave117Compress(SAVE117_PlayerTowns, gPlayerTowns_g.Extra(), gPlayerTowns_g.kExtraCount, (void *)nFile))
        bOk = 0;  // Hyperion: the nations past 7
    for (int i = 0; i < PlayerLobbySlots(); ++i)  // Hyperion: 8 in 1.17
    {
        gPlayerTowns_g[i].pPool = &gTowns_g;
        gPlayerTowns_g[i].pTowns = town_ptr((int)(intptr_t)gPlayerTowns_g[i].pTowns);
    }
    return bOk;
}

// Queue a unit or an upgrade for a building that is idle in its background
// order: a pending guard request wins, otherwise the building's type picks
// the first of its products that is not already under way.
int __fastcall town_queue_idle_building(int nPlayer, CUnit *pUnit, int nArg2)
{
    if (tai_unit_is_forming_or_incomplete(pUnit) || pUnit->bBackgroundOrderId != 0x17)
        return 0;
    int nId = -1;
    banker_take_pending_type(nPlayer, &nId);
    if (nId != -1)
    {
        if (guard_needs_type(nPlayer, nId))
        {
            guard_wake_posts_of_type(nPlayer, (WORD)nId);
            nId = -1;
        }
    }
    else if (gPlayerMinerals[nPlayer] < 600 || gPlayerGas[nPlayer] < 300 || !banker_default_build_on(nPlayer))
        return 0;
    int bFlag = 0;
    int nType;
    if (nId == -1)
    {
        switch (pUnit->wType)
        {
        case 0x6F:
            if (!banker_at_define_max(nPlayer, 0))
                nType = 0;
            else if (!bldg_cannot_build(pUnit, 1))
                nType = 1;
            else
                return 0;
            break;
        case 0x71:
            if (!bldg_cannot_build(pUnit, 3))
                nType = 3;
            else if (!bldg_cannot_build(pUnit, 5))
                nType = 5;
            else
                return 0;
            break;
        case 0x72:
            if (!banker_at_define_max(nPlayer, 8))
                nType = 8;
            else if (!bldg_cannot_build(pUnit, 0x0C))
                nType = 0x0C;
            else
                return 0;
            break;
        case 0x23:
            if (!bldg_cannot_build(pUnit, 0x26))
                nType = 0x26;
            else if (!bldg_cannot_build(pUnit, 0x2B))
                nType = 0x2B;
            else
                return 0;
            break;
        case 0xA0:
            if (!bldg_cannot_build(pUnit, 0x42))
                nType = 0x42;
            else if (!banker_at_define_max(nPlayer, 0x41))
                nType = 0x41;
            else
                return 0;
            break;
        case 0x9B:
            if (!bldg_cannot_build(pUnit, 0x53))
                nType = 0x53;
            else
                return 0;
            break;
        case 0xA7:
            if (!banker_at_define_max(nPlayer, 0x46))
                nType = 0x46;
            else if (!bldg_cannot_build(pUnit, 0x48))
                nType = 0x48;
            else
                return 0;
            break;
        default:
            return 0;
        }
    }
    else
    {
        bFlag = 1;
        if (nId == 0xE4)
            return 0;
        nType = nId;
    }
    banker_demand_unit_for_captain(nPlayer, (intptr_t)tai_captain_for_unit(pUnit), nType, nArg2, 1, bFlag);
    return sai_top_demand_type(nPlayer) != 0;
}

// Take a unit into a town: a worker gets a TWorker record, anything else a
// TBldg record, and the town's key-unit slots are filled from it. The worker
// tag is two byte stores the compiler merges into one immediate word store.
void town_add_unit(TTown *pTown, CUnit *pUnit)
{
    DWORD dwFlags = gulUnitTypeFlags[pUnit->wType];
    if ((dwFlags & 8) != 0)
    {
        TSaveContainer<TWorker,1000> *pList = (TSaveContainer<TWorker,1000> *)&pTown->pWorkerPool;
        TSaveNode<TWorker,1000> *pSave = pList->pPool;
        TWorker *pWorker = 0;
        if (pSave->pFree != 0)
        {
            pWorker = pSave->pFree;
            pSave->pFree = pSave->pFree->pNext;
            if (pSave->pFree != 0)
                pSave->pFree->pPrev = 0;
        }
        if (pWorker == 0)
            return;
        pWorker->pPrev = 0;
        pWorker->pNext = pList->pHead;
        if (pList->pHead != 0)
            pList->pHead->pPrev = pWorker;
        pList->pHead = pWorker;
        pWorker->pUnit10 = pUnit;
        pWorker->pTown = pTown;
        pWorker->bType = 2;
        pWorker->bUnknown09 = 1;
        pWorker->dwUnknown0C = g_dwElapsedTime;
        pWorker->bUnknown0A = 0;
        pWorker->bUnknown0B = 0;
        pUnit->pUnionC8_SavedOrderTarget = 0;
        pUnit->dwUnion134 = (DWORD_PTR)pWorker;
        ++pTown->bUnknown1A;
        return;
    }
    if (((dwFlags & 1) == 0 || pUnit->wType == 0xBC)
        && pUnit->wType != 0x23 && pUnit->wType != 0x24 && pUnit->wType != 0x2A)
        return;
    TBldg *pBldg = ((TSaveContainer<TBldg,1000> *)&pTown->pBldgPool)->New();
    if (pBldg == 0)
        return;
    pBldg->bType = 3;
    pBldg->pUnit = pUnit;
    pBldg->pTown = pTown;
    memset(pBldg->aSlots, 0, sizeof(pBldg->aSlots));
    memset(pBldg->aSlotKind, 0, sizeof(pBldg->aSlotKind));
    pUnit->dwUnion134 = (DWORD_PTR)pBldg;
    WORD wType = pUnit->wType;
    if ((wType == 0x83 || wType == 0x85 || wType == 0x84) && (pUnit->dwFlags & 1) != 0)
    {
        pUnit->SetOrder(0x9C);
        pUnit->SetBackgroundOrder(0x66);
    }
    // A failed aiscript_has_campaign calls the guard unconditionally (the oracle's `je`
    // lands on the call itself).
    if (!aiscript_has_campaign(pTown->bPlayer)
        || ((pUnit->dwFlags & 2) != 0
            && (wType = pUnit->wType) != 0x7C && wType != 0x7D && wType != 0x8F
            && wType != 0x90 && wType != 0x92 && wType != 0x9C && wType != 0xA2))
        TAI_Captain_aiscript_guard(pTown->bPlayer, pUnit);
    if (pTown->bUnknown19 == 0)
    {
        pTown->bUnknown19 = 1;
        pTown->pUnit2C = 0;
        pTown->pUnit30 = 0;
        pTown->pUnit34 = 0;
        pTown->pUnit38 = 0;
    }
    wType = pUnit->wType;
    if (wType != get_player_depot_type(pUnit->bPlayer) && !pUnit->IsResourceDest())
    {
        wType = pUnit->wType;
        if (wType == get_player_refinery_type(pUnit->bPlayer) || wType == 0xBC)
        {
            if (pTown->pUnit30 == 0)
                pTown->pUnit30 = pUnit;
            else if (pTown->pUnit34 == 0)
                pTown->pUnit34 = pUnit;
            else if (pTown->pUnit38 == 0)
                pTown->pUnit38 = pUnit;
            if (pTown->bTownPlan != 0)
                pTown->bUnknown1D = 1;
        }
    }
    else if (pTown->pUnit24 == 0)
        pTown->pUnit24 = pUnit;
}

// 0x484F10  town_clear_repair_worker
// 0x484F10, 33 B.
void town_clear_repair_worker(CUnit *pUnit)
{
    if (pUnit->wType != 7)
        return;
    TWorker *pRec = (TWorker *)pUnit->dwUnion134;
    if (pRec == 0)
        return;
    TTown *pTown = pRec->pTown;
    if (pTown->pUnit28 == pUnit)
        pTown->pUnit28 = 0;
}

// 0x484F40  town_add_unit_to_first_town
// 0x484F40  town_add_unit_to_first_town  41 B
void town_add_unit_to_first_town(intptr_t nArg0)
{
    CUnit *pUnit = (CUnit *)nArg0;
    if (gPlayerData[pUnit->bPlayer].nType == 1)
    {
        TTown *pTown = gPlayerTowns_g[pUnit->bPlayer].pTowns;
        if (pTown != 0)
            town_add_unit(pTown, pUnit);
    }
}

// 0x484F70  bldg_clear_slot
// 0x484F70, 30 B.
void bldg_clear_slot(CUnit *pUnit, int nSlot)
{
    TBldg *pBldg = (TBldg *)pUnit->dwUnion134;
    if (pBldg != 0 && pBldg->bType == 3)
    {
        pBldg->aSlotKind[nSlot] = 0;
        pBldg->aSlots[nSlot] = 0;
    }
}

// 0x484F90  bldg_move_slot
// 0x484F90, 44 B.
void bldg_move_slot(CUnit *pUnit, int nDstSlot, int nSrcSlot)
{
    TBldg *pBldg = (TBldg *)pUnit->dwUnion134;
    if (pBldg != 0 && pBldg->bType == 3)
    {
        pBldg->aSlotKind[nDstSlot] = pBldg->aSlotKind[nSrcSlot];
        pBldg->aSlots[nDstSlot]    = pBldg->aSlots[nSrcSlot];
    }
}

// 0x484FC0  town_needs_more_power
// 0x484FC0, 84 B.
BOOL town_needs_more_power(int nArg0)
{
    if (glCounts_g[0][156][nArg0] > glCounts_g[1][156][nArg0])
        return 0;

    for (CUnit *pUnit = CUnit::GetPlayerUnitList(nArg0); pUnit != 0; pUnit = pUnit->pPlayerUnitNext)
    {
        if (!pUnit->IsDeadOrDying() && (pUnit->dwFlags & 0x400) != 0 && tai_captain_unit_region_is_calm(pUnit))
            return 1;
    }
    return 0;
}

// 0x485020  town_tick   652 B
// Per-town tick: plan the town, refresh its captains, then share the workers
// between minerals and gas, rotating the kinds every four gas workers.
// The order is widened to int before the three compares (a BYTE local costs
// 8 B), and +0x0A/+0x0B must be BYTE fields, not a cast over the WORD.
void town_tick(TTown *pTown)
{
    if (pTown->bUnknown19 == 0)
        return;

    if (pTown->bTownPlan == 0)
    {
        pTown->bTownPlan = (BYTE)sai_nearest_town_plan(pTown->bPlayer, pTown->nX, pTown->nY);
        pTown->bUnknown1D = 1;
    }

    if (pTown->bUnknown1D != 0)
    {
        pTown->bUnknown1D = 0;
        sai_town_gas_slots(pTown->bTownPlan, (intptr_t)&pTown->pUnit30, &pTown->pUnit34, &pTown->pUnit38);
        if (pTown->pUnit30 != 0)
            TAI_Captain_aiscript_guard(pTown->bPlayer, pTown->pUnit30);
        if (pTown->pUnit34 != 0)
            TAI_Captain_aiscript_guard(pTown->bPlayer, pTown->pUnit34);
        if (pTown->pUnit38 != 0)
            TAI_Captain_aiscript_guard(pTown->bPlayer, pTown->pUnit38);
    }

    if (pTown->pUnit2C == 0)
    {
        pTown->pUnit2C = sai_town_first_mineral(pTown->bTownPlan);
        if (pTown->pUnit2C != 0)
            TAI_Captain_aiscript_guard(pTown->bPlayer, pTown->pUnit2C);
    }

    if (pTown->bTownPlan != 0)
        // Hyperion: a computer's level sets how many it keeps (port_ai.h)
        pTown->bUnknown1B = AiWorkerTarget(pTown->bPlayer, (BYTE)sai_town_worker_target((void *)pTown->bTownPlan, pTown->bPlayer));

    unsigned int nIdle = 1;
    unsigned int nBusy = 2;
    if (town_gas_slot(pTown, 3, 1) != 0 || town_gas_slot(pTown, 2, 1) != 0)
    {
        if (pTown->pUnit2C == 0)
            nIdle = 2;
    }
    else
    {
        nBusy = 1;
    }

    BYTE bOrePct;
    BYTE bGasPct;
    banker_resource_split(pTown->bPlayer, &bOrePct, &bGasPct);

    unsigned int nLimit = sai_town_vespene_geysers(pTown->bTownPlan) * 4;
    unsigned int nQuota = pTown->bUnknown1A * bGasPct / 100;

    int nWorkerType;
    BYTE bRace = gPlayerData[pTown->bPlayer].nRace;
    if (bRace == 1)
        nWorkerType = 7;
    else
        nWorkerType = bRace == 0 ? 0x29 : 0x40;
    unsigned int nWorkers = count_get_completion(nWorkerType, pTown->bPlayer);

    if (nQuota > nLimit)
    {
        nQuota = nLimit;
    }
    else if (gPlayerGas[pTown->bPlayer] < 800)
    {
        if (nWorkers >= 20)
        {
            if (nQuota < 4)
                nQuota = 4;
        }
        else if (nWorkers >= 15)
        {
            if (nQuota < 3)
                nQuota = 3;
        }
        else if (nWorkers >= 10)
        {
            if (nQuota < 2)
                nQuota = 2;
        }
        else if (nWorkers >= 5)
        {
            if (nQuota < 1)
                nQuota = 1;
        }
    }

    unsigned int nRun = 0;
    for (TWorker *pW = pTown->pWorkers; pW != 0; pW = pW->pNext)
    {
        int nOrder = pW->pUnit10->bOrderId;
        if (nOrder == 0x21 || nOrder == 0x60 || nOrder == 0x1A
            || (pW->pUnit10->dwFlags & 0x40) != 0)
        {
            pW->dwUnknown0C = g_dwElapsedTime;
        }
        else if (g_dwElapsedTime - pW->dwUnknown0C >= 60)
        {
            pW->pUnit10->SetOrder(0x9C);
            pW->bUnknown0B = 10;
            pW->dwUnknown0C = g_dwElapsedTime;
            pW->bUnknown0A++;
            SAI_Build_aiscript_get_old_peons(pW->pTown, 1);
        }

        unsigned int nKind;
        if (nQuota != 0)
        {
            --nQuota;
            nKind = nBusy;
        }
        else
        {
            nKind = nIdle;
        }
        pW->bUnknown09 = (BYTE)nKind;

        if (nKind != 1 && ++nRun >= 4)
        {
            nRun = 0;
            if (nBusy == nKind)
                ++nBusy;
            if (nIdle == nKind)
                ++nIdle;
            if (nBusy > 4)
                nBusy = 2;
            if (nIdle > 4)
                nIdle = 2;
        }
    }
}

// 0x4852B0  town_repair_order
// 0x4852B0, 222 B. Pick the town building whose unit has the largest missing
// hit-point margin and give the caller's unit a repair-class order on it. The
// second pUnion80 test is redundant on paper, but tai_captain_unit_region_is_calm may have written
// memory, so the oracle keeps the reload. AIFindRepairTarget takes one argument here
// (ECX set, EDX never); the cast spells that against CUnitSearches.h's seat.
BOOL town_repair_order(CUnit *pUnit, TTown *pTown)
{
    CUnit *pBest = 0;
    unsigned nBest = 0;

    for (TBldg *pBldg = pTown->pBldgs; pBldg != 0; pBldg = pBldg->pNext)
    {
        CUnit *pOwner = pBldg->pUnit;
        if (pOwner->pUnion80 != 0)
            continue;
        if (!tai_captain_unit_region_is_calm(pOwner))
            continue;
        if ((pOwner->dwFlags & 1) != 0)
        {
            int nMargin = HandicapLife(gxUnitLife[pOwner->wType], pOwner->bPlayer) >> 8;  // Hyperion: at the owner's handicap
            if (nMargin == 0)
            {
                int nLife = (pOwner->lHitPoints + 0xFF) >> 8;
                nMargin = (nLife != 0) ? nLife : 1;
            }
            nMargin -= (pOwner->lHitPoints + 0xFF) >> 8;
            if (nBest < nMargin)
            {
                pBest = pOwner;
                nBest = nMargin;
            }
        }
        else if (pOwner->pUnion80 == 0)
        {
            pBest = pOwner;
            break;
        }
    }

    if (pBest == 0)
    {
        if (pTown->pUnit28 == 0)
            pTown->pUnit28 = pUnit;
        if (pTown->pUnit28 != pUnit)
            goto fail;
        pBest = ((CUnit *(__fastcall *)(CUnit *))AIFindRepairTarget)(pUnit);
        if (pBest == 0)
            goto fail;
    }
    pUnit->SetOrder_CUnit(0x22, pBest);
    pBest->pUnion80 = pUnit;
    return 1;
fail:
    return 0;
}

// 0x485390  AIUpdateSilo
// AIUpdateSilo (0x485390, 109 B) -- the nuclear-silo AI tick. The unit
// argument is a `CUnit *`: the +0x4C reads, and it is TTAllowed's third
// argument.
void AIUpdateSilo(CUnit *pUnit)
{
    if (banker_at_define_max(pUnit->bPlayer, 0x0E) != 0)
        return;
    if (!unit_can_make_type(pUnit, 0x0E))
        return;
    if (!TTAllowed(0x0E, pUnit->bPlayer, pUnit))
        return;
    if (!ai_stage_unit_cost(pUnit->bPlayer, 0x0E))
        return;
    if (!pUnit->AIAddBuildQueue(0x0E, 8, 0))
        return;
    pUnit->SetOrder(0x7C);
    pUnit->SetBackgroundOrder(0x26);
}

// Send a worker to place the building the player's top demand asks for: find
// a site near the town (a geyser for a refinery), then order the build.
// Each start point is local to its arm, so the two share a slot and the default
// arm's site shares the player's; `fail` is last so its block lands after `ret`.
static __forceinline int worker_build_order(WORD wType)
{
    switch (wType)
    {
    case 0x07: return 0x1E;
    case 0x29: return 0x19;
    case 0x40: return 0x1F;
    }
    return 0;
}

int __fastcall worker_build_top_demand(TTown *pTown, CUnit *pUnit)
{
    if (sai_top_demand_type(pUnit->bPlayer) != 3)
        return 0;
    TWorker *pRec = (TWorker *)pUnit->dwUnion134;
    int nArg = sai_top_demand_arg(pUnit->bPlayer);
    if ((sai_top_demand_captain(pUnit->bPlayer) != (intptr_t)pRec->pTown && nArg != 0x6D)
        || (BYTE)pRec->wUnknown0A >= 3)
        goto fail;
    {
        int nType = sai_top_demand_arg(pUnit->bPlayer);
        if (!unit_can_make_type(pUnit, (WORD)nType))
            goto fail;
        if (TTAllowed((WORD)nType, pUnit->bPlayer, pUnit) != 1)
            goto fail;
        if (!ai_stage_unit_cost(pUnit->bPlayer, nType))
            goto fail;
        int nPlayer = pTown->bPlayer;
        mpoint_t  ptTarget;
        if (nType == get_player_refinery_type(nPlayer))
        {
            CUnit *pGeyser = town_gas_slot(pTown, 1, 1);
            if (pGeyser == 0)
                return 0;
            ptTarget.x = pGeyser->pSprite->wPosX;
            ptTarget.y = pGeyser->pSprite->wPosY;
        }
        else
        {
            mpoint_t  ptSite;
            if (nType == 0x7D)
            {
                mpoint_t  ptStart;
                if (!tai_captain_take_bunker_site(nPlayer, &ptStart))
                {
                    ptStart.x = pTown->nX;
                    ptStart.y = pTown->nY;
                }
                sai_mark_frontier_regions(pTown->bPlayer, GetRegion_xy(ptStart.x, ptStart.y), 1);
                if (!ai_get_build_location_near(pUnit, 0x7D, ptStart, &ptTarget, 1, 0x40))
                    return worker_build_failed((intptr_t)pUnit);
                tai_captain_claim_defence_region(pTown->bPlayer, GetRegion_xy(ptTarget.x, ptTarget.y), 1);
            }
            else if (nType == 0x7C)
            {
                mpoint_t  ptStart;
                if (!tai_captain_take_turret_site(nPlayer, &ptStart))
                {
                    ptStart.x = pTown->nX;
                    ptStart.y = pTown->nY;
                }
                if (!ai_get_build_location_near(pUnit, 0x7C, ptStart, &ptTarget, 1, 0x40))
                    return worker_build_failed((intptr_t)pUnit);
            }
            else if (nType == 0x83 && pTown->pUnit24 != 0)
            {
                sai_town_build_anchor(pTown->bTownPlan, &ptSite.x, &ptSite.y);
                if (!ai_get_build_location_near(pUnit, nType, ptSite, &ptTarget, 1, 0x40))
                    return worker_build_failed((intptr_t)pUnit);
            }
            else
            {
                ptSite.x = pTown->nX;
                ptSite.y = pTown->nY;
                if (nType == 0x8F || nType == 0xA2)
                    sai_mark_frontier_regions(pTown->bPlayer, GetRegion_xy(ptSite.x, ptSite.y), 4);
                if (!ai_get_build_location_near(pUnit, nType, ptSite, &ptTarget, 1, 0x40))
                    return worker_build_failed((intptr_t)pUnit);
            }
        }
        banker_spend_top_demand(pTown->bPlayer);
        int nOrder = worker_build_order(pUnit->wType);
        if (nOrder == 0)
            goto fail;
        pUnit->SetOrder_xy((BYTE)nOrder, ptTarget.x, ptTarget.y);
        pUnit->RemoveAllBuildQueue();
        pUnit->AddBuildQueue(nType);
        return 1;
    }
fail:
    return 0;
}

// 0x4856A0  town_delete_if_empty
// 0x4856A0  town_delete_if_empty  181 B
void town_delete_if_empty(intptr_t nArg0)
{
    TTown *pTown = (TTown *)nArg0;
    if (town_merge_into_nearest(pTown) == 0)
        return;

    sai_scripts_remove_town((intptr_t)pTown);
    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != 0; pUnit = pUnit->pNext)
    {
        if (!pUnit->IsDeadOrDying() && (gulUnitTypeFlags[pUnit->wType] & 0x2000))
            pUnit->bUnionD9 = 0;
    }

    BYTE bBase = pTown->bTownPlan;
    if (bBase != 0)
        sai_townbuilder_clear(bBase);

    town_container_free(&gPlayerTowns_g[pTown->bPlayer], pTown);
}

// 0x485760  unit_belongs_to_player
// 0x485760  unit_belongs_to_player  12 B
BOOL unit_belongs_to_player(CUnit *pUnit, int nPlayer)
{
    return pUnit->bPlayer == nPlayer;
}
// Pops a town off the player's free list, pushes it on the town list and
// initialises it. The zeroed byte pairs at +0x19 and +0x1C are separate byte
// stores that the compiler merges into immediate word stores; +0x1B goes last.
intptr_t NewTown(int nArg0, WORD wArg1, WORD wArg2)
{
    TTownPool *pPool = gPlayerTowns_g[nArg0].pPool;
    TTown *pTown = pPool->pFreeHead;
    if (pTown != 0)
    {
        TTown *pNext = pTown->pNext;
        pPool->pFreeHead = pNext;
        if (pNext != 0)
            pNext->pPrev = 0;
    }
    town_list_prepend(&gPlayerTowns_g[nArg0], pTown);
    if (pTown == 0)
        return 0;

    pTown->bPlayer = (BYTE)nArg0;
    pTown->bUnknown19 = 0;
    pTown->bUnknown1A = 0;
    pTown->nX = (short)wArg1;
    pTown->nY = (short)wArg2;
    pTown->pUnit2C = 0;
    pTown->pUnit30 = 0;
    pTown->pUnit34 = 0;
    pTown->pUnit38 = 0;
    pTown->pUnit28 = 0;
    pTown->pUnit24 = 0;
    pTown->bTownPlan = 0;
    pTown->bUnknown1D = 0;
    pTown->bUnknown1B = 0;
    memset(pTown->aQueue, 0, 0x190);
    return (intptr_t)pTown;
}

// 0x485840  town_merge_into_nearest
// Fold a town with nothing but overlords
// into the nearest town of the same player, under a re-entrancy guard. Each
// removal loop must be a do/while under its own `if` guard: that keeps the
// `!= 0` test at the top, where the back edge tests the town's list head
// instead.
BOOL town_merge_into_nearest(TTown *pTown)
{
    if (dword_69B558 != 0)
        return FALSE;

    if (pTown->pBldgs == 0 && pTown->pWorkers == 0)
        return TRUE;

    for (TBldg *pBldg = pTown->pBldgs; pBldg != 0; pBldg = pBldg->pNext)
    {
        if (pBldg->pUnit->wType != 0x2A)
            return FALSE;
    }

    TTown *pBest = 0;
    unsigned nBest = 99999999;
    for (TTown *pOther = gPlayerTowns_g[pTown->bPlayer].pTowns; pOther != 0;
         pOther = pOther->pNext)
    {
        if (pOther == pTown)
            continue;
        int nBY = pOther->nY;
        int nAY = pTown->nY;
        int nBX = pOther->nX;
        int nAX = pTown->nX;
        unsigned nDist = veclen(nAX, nBX, nAY, nBY);
        if (nDist < nBest)
        {
            nBest = nDist;
            pBest = pOther;
        }
    }
    if (pBest == 0)
        return FALSE;

    TBldg *pB = pTown->pBldgs;
    dword_69B558 = 1;
    if (pB != 0)
    {
        do
        {
            if (pB == 0)
                break;
            TBldg *pNextB = pB->pNext;
            CUnit *pUnit = pB->pUnit;
            SAI_RemoveUnit(pUnit, 0);
            town_add_unit(pBest, pUnit);
            pB = pNextB;
        } while (pTown->pBldgs != 0);
    }
    TWorker *pW = pTown->pWorkers;
    if (pW != 0)
    {
        do
        {
            if (pW == 0)
                break;
            TWorker *pNextW = pW->pNext;
            CUnit *pUnit = pW->pUnit10;
            SAI_RemoveUnit(pUnit, 0);
            town_add_unit(pBest, pUnit);
            pW = pNextW;
        } while (pTown->pWorkers != 0);
    }
    dword_69B558 = 0;
    return TRUE;
}

// 0x485970  town_queue_supply
// player_unit_count has no header seat yet, and a .cpp-local declaration needs an
// explicit calling convention.

// Queue another supply provider when the town is short. Race 1 is Terran, 0 is
// Zerg, anything else Protoss.
// The first race read goes through pTown and the second through bPlayer, so the
// second pick rescales the index. nNeed is set in both arms, which ranks it
// above nFood for EBX.
void town_queue_supply(TTown *pTown)
{
    if (pTown == 0)
        return;

    BYTE bPlayer = pTown->bPlayer;
    BYTE bRace1 = gPlayerData[pTown->bPlayer].nRace;
    int nSupplyType = bRace1 == 1 ? 0x6D : (bRace1 == 0 ? 0x2A : 0x9C);
    BYTE bRace2 = gPlayerData[bPlayer].nRace;
    int nWorkerType = bRace2 == 1 ? 7 : (bRace2 == 0 ? 0x29 : 0x40);
    int nFood = count_food_available(nWorkerType, bPlayer);
    unsigned nNeed;

    if (town_needs_more_power(pTown->bPlayer))
    {
        nNeed = player_unit_count(pTown->bPlayer, nSupplyType) + 1;
    }
    else
    {
        if (nFood >= 13)
            return;

        int nRace = count_race(nSupplyType);
        unsigned nUsed = count_get_supplies_used(nRace, pTown->bPlayer) + 13;
        int nDepotType = get_player_depot_type(pTown->bPlayer);
        // Hyperion: what the town hall and each supplier give with a mutator's boost
        const unsigned nDepotMade = MutatorFoodMade(gubUnitFoodMade[nDepotType]);
        if (nUsed <= nDepotMade)
            return;
        nNeed = (nUsed - nDepotMade) /
                MutatorFoodMade(gubUnitFoodMade[nSupplyType]) + 1;
    }

    if (nFood != 0 && banker_farms_timing(pTown->bPlayer))
    {
        if (!town_needs_more_power(pTown->bPlayer))
            return;
    }
    if (nNeed <= (unsigned)player_unit_count(pTown->bPlayer, nSupplyType))
        return;
    banker_demand_structure(pTown->bPlayer, (intptr_t)pTown, nSupplyType,
               (nFood <= 8) ? 0x8C : 0x78);
}

// 0x485AB0  town_queue_refinery
// town_queue_refinery (0x485AB0, 192 B) -- queue the next worker/refinery build for a
// town.
//
// The refinery-type expression is inlined again, as in town_gas_class's note.
// Both +0x1A/+0x1B compares are unsigned (`jae`/`jb`), so both fields are
// BYTEs; the gPlayerMinerals compare is signed (`jl`), which agrees with the
// registry's `int[12]`.
void town_queue_refinery(TTown *pTown)
{

    BYTE bOre;
    BYTE bGas;
    banker_resource_split(pTown->bPlayer, &bOre, &bGas);
    if (bGas == 0 && pTown->pUnit2C != 0)
        return;

    int nArg;
    if (town_gas_slot(pTown, 3, 1) != 0)
    {
        nArg = 0x50;
        if (town_gas_slot(pTown, 1, 1) == 0)
            return;
        if (pTown->bUnknown1A < pTown->bUnknown1B && pTown->bUnknown1A < 0x0F)
            return;
        if (gPlayerMinerals[pTown->bPlayer] < 0x1F4)
            return;
    }
    else
    {
        if (town_gas_slot(pTown, 1, 1) == 0)
            return;
        nArg = 0x5A;
    }

    BYTE bRace = gPlayerData[pTown->bPlayer].nRace;
    int nType;
    if (bRace == 1)
        nType = 0x6E;
    else
        nType = bRace == 0 ? 0x95 : 0x9D;
    banker_demand_structure(pTown->bPlayer, (intptr_t)pTown, nType, nArg);
}

// 0x485B70  sai_process_top_demand_unit
// 0x485B70, 296 B. Place the top unit demand: ask for a build location, fall
// back to a search around the builder's own sprite, then issue the build order.
// The bare get_allowed_err() on the failure paths throws its result away; the
// oracle calls it anyway. Both failure arms are written out in full so the
// shared get_allowed_err block sits at the first one.
void sai_process_top_demand_unit(int nArg0, CUnit *pUnit, TBldg *pBldg)
{
    mpoint_t  pos;  // Hyperion: ai_get_build_location_at_unit writes a map point
    mpoint_t  ptSprite;

    int nId = sai_top_demand_arg(nArg0);
    TTown *pTown = pBldg->pTown;
    if (sai_top_demand_captain(nArg0) != (intptr_t)pTown)
        return;

    if (unit_can_make_type(pUnit, nId) && TTAllowed(nId, nArg0, pUnit) == 1)
    {
        if (!ai_stage_unit_cost(nArg0, nId))
            return;

        if (!ai_get_build_location_at_unit((intptr_t)pUnit, nId, &pos, 0))
        {
            if (!aiscript_has_campaign(nArg0))
            {
                get_allowed_err();
                banker_drop_top_demand(nArg0);
                bldg_tick(pUnit);
                return;
            }
            ptSprite.x = pUnit->pSprite->wPosX;
            ptSprite.y = pUnit->pSprite->wPosY;
            if (!ai_get_build_location_near(pUnit, nId, ptSprite, &pos, 1, 0x40))
            {
                get_allowed_err();
                banker_drop_top_demand(nArg0);
                bldg_tick(pUnit);
                return;
            }
        }

        banker_spend_top_demand(pTown->bPlayer);
        pUnit->SetOrder_xy(0x24, pos.x, pos.y);
        pUnit->RemoveAllBuildQueue();
        *(WORD *)&pUnit->_unmapped_C4[0] = (WORD)nId;
        return;
    }

    {
        int nErr = get_allowed_err();
        if (nErr != 2 && nErr != 0x17 && nErr != 0x15 && nErr != 8 && nErr != 7 && nErr != 0x0D)
            return;
    }
    banker_drop_top_demand(nArg0);
    bldg_tick(pUnit);
}

// 0x485CA0  sai_process_top_demand_tech
// sai_process_top_demand_tech (0x485CA0, 129 B) and
// sai_process_top_demand_upgrade (0x485D30, 129 B) are exact twins. The second
// argument is a `CUnit *`, the unit the research or upgrade is started on.
//
// Both are in a call cycle with bldg_tick, so the tail `jmp` is certain and no
// inline directive is spent on it.
void sai_process_top_demand_tech(int nArg0, CUnit *pUnit)
{
    int nTech = sai_top_demand_arg(nArg0);
    if (ResearchAllowed(nTech, nArg0, pUnit) != 1)
    {
        int nErr = get_allowed_err();
        if (nErr == 2 || nErr == 0x17 || nErr == 0x15 || nErr == 8 || nErr == 7 || nErr == 0x0D)
        {
            banker_drop_top_demand(nArg0);
            bldg_tick(pUnit);
        }
        return;
    }
    if (ai_stage_tech_cost(nArg0, nTech) && pUnit->StartResearch((BYTE)nTech))
    {
        pUnit->SetOrder(0x4B);
        banker_spend_top_demand(nArg0);
    }
}

// 0x485D30  sai_process_top_demand_upgrade
void sai_process_top_demand_upgrade(int nArg0, CUnit *pUnit)
{
    int nUpgrade = sai_top_demand_arg(nArg0);
    if (UpgradeAllowed(nUpgrade, nArg0, pUnit) != 1)
    {
        int nErr = get_allowed_err();
        if (nErr == 2 || nErr == 0x17 || nErr == 0x15 || nErr == 8 || nErr == 7 || nErr == 0x0D)
        {
            banker_drop_top_demand(nArg0);
            bldg_tick(pUnit);
        }
        return;
    }
    if (ai_stage_upgrade_cost(nArg0, nUpgrade) && pUnit->StartUpgrade((BYTE)nUpgrade))
    {
        pUnit->SetOrder(0x4C);
        banker_spend_top_demand(nArg0);
    }
}

// 0x485DC0  worker_build_failed
// 0x485DC0  worker_build_failed  32 B
BOOL worker_build_failed(intptr_t nArg0)
{
    CUnit *pUnit = (CUnit *)nArg0;
    BYTE bPlayer = pUnit->bPlayer;
    get_allowed_err();
    banker_drop_top_demand(bPlayer);
    bldg_tick(pUnit);
    return 0;
}

// 0x485DE0  town_gas_slot
// town_gas_slot (0x485DE0, 148 B) -- return the nArg2'th of a town's three
// resource-depot slots (+0x30/+0x34/+0x38) whose town_gas_class class equals
// nArg1.
CUnit *town_gas_slot(TTown *pTown, int nArg1, int nArg2)
{
    int nFound = 0;

    if (pTown->pUnit30 != 0 && town_gas_class(pTown->bPlayer, pTown->pUnit30) == nArg1)
    {
        ++nFound;
        if (nFound == nArg2)
            return pTown->pUnit30;
    }
    if (pTown->pUnit34 != 0 && town_gas_class(pTown->bPlayer, pTown->pUnit34) == nArg1)
    {
        ++nFound;
        if (nFound == nArg2)
            return pTown->pUnit34;
    }
    if (pTown->pUnit38 != 0 && town_gas_class(pTown->bPlayer, pTown->pUnit38) == nArg1)
    {
        ++nFound;
        if (nFound == nArg2)
            return pTown->pUnit38;
    }
    return 0;
}

// 0x485E80  town_gas_class
// town_gas_class (0x485E80, 107 B) -- classify a resource unit for a player:
// 1 = a vespene geyser, 2 = that player's refinery with no gas left,
// 3 = that player's refinery with gas, 0 = anything else.
//
// The second argument is a `CUnit *`, since IsDeadOrDying is called on it as
// `this`.

// The refinery-type expression is inlined by the oracle: it is the body of
// get_player_refinery_type (0x486100, 36 B), whose oracle TU is SAI_Build.cpp
// while this tree keeps it in src117/lang/SAI_Banker.cpp, so a call would not
// inline. Open-coded here for the same bytes.

// `WORD wType` and not `int`: the oracle materialises 0xBC into ECX and
// compares `cmp ax, cx`, which is MSVC avoiding the length-changing 0x66
// prefix of `cmp ax, imm16` -- a 16-BIT compare, so both operands are 16 bits.
// Declared `int` the compare would be `cmp eax, 0BCh` and five bytes shorter.
// The 108-against-107 register rotation comes from a single-exit result
// variable scoped inside the guarded block: `int nResult = 0;` declared after
// the race select, assigned by the tail test and returned there, with a
// separate `return 0` for the guard's own failure path. That is what pins the
// oracle's hoisted `xor eax, eax` above `cmp edx, ecx` and rotates the race
// byte into AL and the refinery type into ECX. Declared at function top, or at
// the top of the guarded block, it stays at 108 B: the placement is the lever.
// The noinline is for town_expand_tick below, which calls this six times and inlines
// every one without it; town_gas_slot above already gets the call it needs because
// it sits ahead of this definition.
__declspec(noinline) int town_gas_class(int nArg0, CUnit *pUnit)
{
    if (pUnit != 0 && !pUnit->IsDeadOrDying())
    {
        // The field is read twice on purpose: the CSE temp lands in EAX and is
        // split to EDX (`mov edx, eax`) exactly as the oracle does. A single
        // `WORD wType` local lands in EDX from the start and loses it.
        if (pUnit->wType == 0xBC)
            return 1;
        int nType = pUnit->wType;

        // get_player_refinery_type (0x486100) inlined: its oracle TU is
        // SAI_Build.cpp but this tree keeps it in SAI_Banker.cpp, so a call
        // would not inline. Open-coded instead.
        BYTE bRace = gPlayerData[nArg0].nRace;
        int nWant;
        if (bRace == 1)
            nWant = 0x6E;
        else
            nWant = bRace == 0 ? 0x95 : 0x9D;

        int nResult = 0;
        if (nType == nWant)
            nResult = pUnit->wResourceAmount != 0 ? 3 : 2;
        return nResult;
    }
    return 0;
}

// 0x485EF0  get_player_depot_type
// The gPlayerData scaled read with a cmov pair.

// Two plain ifs, not a ternary: inlined into town_player_tick, a `== 0x83` test
// then folds to the race tests the oracle has.
int get_player_depot_type(int nPlayer)
{
    BYTE bRace = gPlayerData[nPlayer].nRace;
    if (bRace == 1)
        return 0x6A;
    if (bRace == 0)
        return 0x83;
    return 0x9A;
}

// 0x485F20  worker_harvest_order
// worker_harvest_order (0x485F20, 114 B) -- pick a harvest target for a worker: prefer
// nArg1, fall back to nArg2, and only if it is a live non-geyser resource.
// All three arguments are `CUnit *`: the first is `this` for SetOrder and
// SetOrder_CUnit, and the other two are both tested at +0x64 wType and +0xDC
// dwFlags, one of them becoming SetOrder_CUnit's target.
//
// The two early exits reuse the incoming stack slot as SetOrder's argument
// (`mov dword ptr [ebp+8h], 5Ah` then a tail `jmp`) -- both this body and
// CUnit::SetOrder are `ret 4`, so MSVC forwards the frame.
void worker_harvest_order(CUnit *pUnit, CUnit *pTarget1, CUnit *pTarget2)
{
    BYTE bCarry = pUnit->bCarryResource;
    if ((bCarry & 2) != 0)
    {
        pUnit->SetOrder(0x5A);
        return;
    }
    if ((bCarry & 1) != 0)
    {
        pUnit->SetOrder(0x54);
        return;
    }

    CUnit *pTarget = pTarget1;
    if (pTarget == 0 || pTarget->wType == 0xBC || (pTarget->dwFlags & 1) == 0)
        pTarget = pTarget2;
    if (pTarget != 0 && pTarget->wType != 0xBC && (pTarget->dwFlags & 1) != 0)
    {
        pUnit->SetOrder_CUnit(0x4F, pTarget);
        pTarget->bUnionD9 = 1;
    }
}

// 0x485FA0  add_request_to_town_queue
// 0x485FA0, 94 B.
void add_request_to_town_queue(TTown *pTown, int nKind, int nPrio, WORD wId, BYTE bArg4, int nFlag)
{
    if (gPlayerData[pTown->bPlayer].nType != 1)
        return;

    int i = 0;
    TTownRequest *pReq = pTown->aQueue;
    // Hyperion: the bound before the read. 1.17 reads the slot first, so with all 100
    // taken it reads aQueue[100], the first byte of the next town's pNext, and when that
    // byte's top five bits are clear it writes the request over that pNext; the player's
    // next walk of its towns then crashed (seen in a long game of Very Hard computers on
    // Lost Temple). Where 1.17 read a set byte there it returned, as this does.
    while (i < 100 && (pReq->bPacked & 0xF8) != 0)
    {
        ++pReq;
        ++i;
    }
    if (i == 100)
        return;

    // Two levers here, both measured.
    //   1. One expression, not a four-statement accumulate. The accumulate
    //      lands at 91 B: it keeps only two temps live, so `this` stays in ECX
    //      and the oracle's `push edi / mov edi, ecx`, exactly the missing
    //      3 B, never happens. The single expression keeps nPrio, nKind and
    //      nFlag live at once, which pushes `this` into a callee-saved
    //      register.
    //   2. The source store order is bPacked, wId, bUnknown01, which is not the
    //      oracle's emission order. All six permutations were swept: writing
    //      wId first lets MSVC hoist its load and store above the packing
    //      block, while writing it second schedules the packing first and then
    //      emits the wId store ahead of the bPacked store, which is the oracle.
    BYTE bLo = (BYTE)(((((nPrio << 2) | (nKind & 3)) << 1) | (nFlag & 1)));
    pTown->aQueue[i].bPacked    = bLo;
    pTown->aQueue[i].wId        = wId;
    pTown->aQueue[i].bUnknown01 = bArg4;
}

// 0x486000  town_forget_unit
// 0x486000  town_forget_unit  105 B
void town_forget_unit(CUnit *pUnit)
{
    if ((gulUnitTypeFlags[pUnit->wType] & 0x2000) == 0)
        return;
    // The byte-offset induction variable matters, and only the shipped form
    // shows it. The oracle walks a byte offset -- `add edx, 8h` /
    // `cmp edx, 40h` / `jb`, with the base folded into the displacement of
    // `mov eax, [edx+69B51Ch]`. Written `gPlayerTowns_g[i]` this body matches
    // in the plain form for free, because `gPlayerTowns_g` is a relocation
    // there, so MSVC cannot fold it and has to use the offset form. Under
    // /DOC_DLL_MODE the same source is a constant, MSVC promotes the induction
    // variable to a full pointer, and the loop test becomes
    // `cmp esi, 69B55Ch` (6 B) instead of `cmp edx, 40h` (3 B): the whole
    // 108-against-105 delta, and an address the oracle never forms. Spelling
    // the offset explicitly pins the oracle's choice under both spellings.
    // Eight shapes were tried; `int` i, `!= 8`, a pointer walk and a do/while
    // all keep the pointer promotion.
    // Hyperion: by index over every AI nation, Hyperion's slots too.
    for (int i = 0; i < PlayerLobbySlots(); i++)
    {
        TPlayerTowns *pC = &gPlayerTowns_g[i];
        for (TTown *pTown = pC->pTowns; pTown != 0; pTown = pTown->pNext)
        {
            if (pTown->pUnit2C == pUnit)
                pTown->pUnit2C = 0;
            if (pTown->pUnit30 == pUnit)
                pTown->pUnit30 = 0;
            if (pTown->pUnit34 == pUnit)
                pTown->pUnit34 = 0;
            if (pTown->pUnit38 == pUnit)
                pTown->pUnit38 = 0;
            if (pTown->bTownPlan != 0)
                pTown->bUnknown1D = 1;
        }
    }
}

// 0x4860A0  unit_can_make_type
// 0x4860A0  unit_can_make_type  83 B
BOOL unit_can_make_type(void *pArg0, WORD wArg1)
{
    CUnit *pUnit = (CUnit *)pArg0;
    BYTE bInfo = gubUnitCountInfo[wArg1];
    if (bInfo & 2)
    {
        if (gubUnitCountInfo[pUnit->wType] & 2)
            return 1;
    }
    else if (bInfo & 1)
    {
        if (gubUnitCountInfo[pUnit->wType] & 1)
            return 1;
    }
    else if (gubUnitCountInfo[pUnit->wType] & 4)
    {
        return 1;
    }
    set_allowed_err(0x19);
    return 0;
}

// 0x486130  player_mineral_fields
// 0x486130  player_mineral_fields  44 B
int player_mineral_fields(int nArg0)
{
    int nTotal = 0;
    for (TTown *pTown = gPlayerTowns_g[nArg0].pTowns; pTown != 0; pTown = pTown->pNext)
    {
        BYTE bBase = pTown->bTownPlan;
        if (bBase != 0)
            nTotal += sai_town_mineral_fields(bBase);
    }
    return nTotal;
}

// 0x486160  bldg_id
int bldg_id(TBldg *pBldg)
{
    if (pBldg == 0)
        return 0;
    return (int)((unsigned int)((BYTE *)pBldg - (BYTE *)gBldgs_g.aBldgs) / sizeof(TBldg)) + 1;
}

// 0x486180  bldg_ptr
TBldg *bldg_ptr(int nId)
{
    if (nId == 0)
        return 0;
    return &gBldgs_g.aBldgs[nId - 1];
}

// 0x4861A0  region_has_worker_or_building
// 0x4861A0  region_has_worker_or_building  87 B
int region_has_worker_or_building(int nPlayer, int nRegion)
{
    int bFound = 0;
    CUnit **pp = get_units_in_region(nRegion);
    CUnit **ppList = pp;
    for (CUnit *pUnit = *pp; pUnit != 0; pUnit = *++pp)
    {
        if (pUnit->bPlayer == nPlayer && (gulUnitTypeFlags[pUnit->wType] & 9))
        {
            bFound = 1;
            break;
        }
    }
    finish_unit_iteration(ppList);
    return bFound;
}

// A zerg building morphed from pMother: move its town record over, then ask
// the guard system for a spore or sunken colony guard where warranted.
void __fastcall town_move_morphed_building(CUnit *pMother, CUnit *pLarva)
{
    if (gPlayerData[pMother->bPlayer].nType != 1)
        return;
    TBldg *pRec = (TBldg *)pMother->dwUnion134;
    if (pRec == 0)
        return;
    // Merged arms under the 0x7C test the tail makes. Written once, pTown takes ESI
    // where the oracle keeps it in EDI.
    TTown *pTown;
    if (pLarva->wType == 0x7C)
        pTown = pRec->pTown;
    else
        pTown = pRec->pTown;
    if (pMother == pLarva)
    {
        town_forget_unit(pLarva);
        TBldg *pRec2 = (TBldg *)pLarva->dwUnion134;
        if (pRec2 != 0)
        {
            TTown *pTown2 = pRec2->pTown;
            ((TSaveContainer<TBldg,1000> *)&pTown2->pBldgPool)->Free(pRec2);
            if (pTown2 != 0)
            {
                if (pLarva == pTown2->pUnit28)
                    pTown2->pUnit28 = 0;
                if (pLarva == pTown2->pUnit24)
                    pTown2->pUnit24 = 0;
            }
            pLarva->dwUnion134 = 0;
        }
    }
    town_add_unit(pTown, pLarva);
    if (pLarva->wType == 0x7D && g_dwElapsedTime > 0x3C)
        guard_create_post(pLarva->bPlayer, 5, PosS(pLarva->pSprite->wPosX), pLarva->pSprite->wPosY);
    else if (pLarva->wType == 0x7C)
    {
        unsigned nCount = 0;
        for (TWorker *pW = pTown->pWorkers; pW != 0; pW = pW->pNext)
            nCount += pW->pUnit10->wType == 0x7C ? 1 : pW->pUnit10->CountInBuildQueue(0x7C);
        for (TBldg *pB = pTown->pBldgs; pB != 0; pB = pB->pNext)
            nCount += pB->pUnit->wType == 0x7C ? 1 : pB->pUnit->CountInBuildQueue(0x7C);
        if (nCount < 4)
            return;
    }
    else
        return;
    // The 0x7D arm falls through to this call as well.
    guard_create_post(pLarva->bPlayer, 1, PosS(pLarva->pSprite->wPosX), pLarva->pSprite->wPosY);
}

// 0x486340  town_move_record
// 0x486340, 103 B.
void town_move_record(CUnit *pUnit, CUnit *pOther)
{

    TBldg *pRec = (TBldg *)pUnit->dwUnion134;
    if (pRec == 0)
        return;

    TTown *pTown = pRec->pTown;
    if (pUnit == pOther)
    {
        town_forget_unit(pOther);

        TBldg *pRec2 = (TBldg *)pOther->dwUnion134;
        if (pRec2 != 0)
        {
            TTown *pTown2 = pRec2->pTown;
            ((TSaveContainer<TBldg, 1000> *)&pTown2->pBldgPool)->Free(pRec2);
            if (pTown2 != 0)
            {
                if (pOther == pTown2->pUnit28)
                    pTown2->pUnit28 = 0;
                if (pOther == pTown2->pUnit24)
                    pTown2->pUnit24 = 0;
            }
            pOther->dwUnion134 = 0;
        }
    }
    town_add_unit(pTown, pOther);
}

// 0x4863B0  town_is_researching
// 0x4863B0, 88 B.
BOOL town_is_researching(TTown *pTown, int nArg1)
{

    if (get_tech_researched(pTown->bPlayer, (WORD)nArg1))
        return 1;

    for (CUnit *pUnit = CUnit::GetPlayerUnitList(pTown->bPlayer); pUnit != 0; pUnit = pUnit->pPlayerUnitNext)
    {
        if (!pUnit->IsDeadOrDying() && pUnit->bOrderId == 0x4B && pUnit->bTechType == nArg1)
            return 1;
    }
    return 0;
}
// 0x486410  aiscript_is_groundmap
// aiscript_is_groundmap (0x486410, 112 B) -- is any reachable enemy unit on
// the ground path from (nArg1, nArg2)?
//
// IsEnemyUnit here is the free function @0x429110 in CUnitMisc.h, not the
// 48-byte member CUnit::IsEnemyUnit @0x4290E0: ECX is the player index, the
// spilled first argument, and EDX the unit. The two are different addresses
// with different signatures, and the names do not collide at namespace scope.
BOOL aiscript_is_groundmap(int nArg0, int nArg1, int nArg2)
{
    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != 0; pUnit = pUnit->pNext)
    {
        if (!pUnit->IsDeadOrDying()
            && IsEnemyUnit(nArg0, pUnit)
            && (pUnit->dwFlags & 2) != 0
            && sai_path_can_reach_target(pUnit, (WORD)nArg1, (WORD)nArg2))
            return TRUE;
    }
    return FALSE;
}

// 0x486480  SAI_Build_aiscript_get_old_peons
// Pull up to nCount idle workers out of the
// player's other towns into pTown, walking the town list from its tail
// backwards. The inner loop must be a do/while under its own `if` guard so the
// `pW != 0` test stays at the top, and pNext must be read before pUnit -- read
// the other way round MSVC folds pW and pNext into one register and drops the
// oracle's `mov eax, ebx`.
void SAI_Build_aiscript_get_old_peons(TTown *pTown, unsigned nCount)
{
    if (pTown->bUnknown1E != 0)
        return;

    if (pTown->bUnknown1A != 0)
    {
        if (pTown->bUnknown1A >= pTown->bUnknown1B)
            return;
        if (pTown->bUnknown1A + nCount > pTown->bUnknown1B)
            nCount = pTown->bUnknown1B - pTown->bUnknown1A;
    }
    if (nCount == 0)
        return;

    TTown *pOther = gPlayerTowns_g[pTown->bPlayer].pTowns;
    if (pOther == 0)
        return;
    while (pOther->pNext != 0)
        pOther = pOther->pNext;

    do
    {
        if (pOther != pTown && pOther->bUnknown1A > 1 && pOther->pUnit24 != 0 &&
            pOther->pUnit24->IsResourceDest() &&
            (pTown->bUnknown1A == 0 || pOther->bUnknown1A > pOther->bUnknown1B))
        {
            TWorker *pW = pOther->pWorkers;
            if (pW != 0)
            {
                do
                {
                    if (pW == 0)
                        break;
                    TWorker *pNext = pW->pNext;
                    CUnit *pUnit = pW->pUnit10;
                    int nOrder = pUnit->bOrderId;
                    if (nOrder != 0x53 && nOrder != 0x57 && nOrder != 0x58 &&
                        nOrder != 0x1E && nOrder != 0x21 && nOrder != 0x1F &&
                        nOrder != 0x46 && nOrder != 0x1A && nOrder != 0x19 &&
                        (pUnit->dwFlags & 1) != 0)
                    {
                        SAI_Build_RemoveUnit(pUnit, 0);
                        town_add_unit(pTown, pUnit);
                        if (--nCount == 0)
                            return;
                    }
                    pW = pNext;
                } while (pOther->pWorkers != 0);
            }
        }
        pOther = pOther->pPrev;
    } while (pOther != 0);
}

// 0x4865B0  town_player_tick
// The per-player town tick: refresh each town building's queue, move spare
// workers, re-issue every queued request still short, then run the last town's
// supply, captain and guard passes.
void town_player_tick(int nation)
{
    unsigned nPlanned = 0;
    BOOL bSpare = FALSE;
    TTown *pLast = 0;

    banker_set_has_upgrades(nation, 1);
    for (TTown *pTown = gPlayerTowns_g[nation].pTowns; pTown != 0; pTown = pTown->pNext)
    {
        for (TBldg *pB = pTown->pBldgs; pB != 0; pB = pB->pNext)
        {
            CUnit *pUnit = pB->pUnit;
            if (pTown->bPlayer == pUnit->bPlayer)
            {
                for (unsigned i = 1; i < 5; i++)
                    pUnit->RemoveBuildQueue(i);
            }
        }
        if (pTown->pWorkers == 0)
        {
            CUnit *pMain = pTown->pUnit24;
            if (pMain == 0 || (pMain->dwFlags & 1) || pMain->wType != 0x83)
                SAI_Build_aiscript_get_old_peons(pTown, 1);
        }
        else if (pTown->bUnknown1A > pTown->bUnknown1B)
            bSpare = TRUE;

        pLast = pTown;
        unsigned i;
        TTownRequest *pReq;
        for (i = 0, pReq = pTown->aQueue; i < 100; i++, pReq++)
        {
            if ((pReq->bPacked & 0xF8) == 0)
                break;
            unsigned nHave;
            BYTE nKind = (pReq->bPacked >> 1) & 3;
            if (nKind == 0)
            {
                if (pReq->bPacked & 1)
                    nHave = count_get_existence(pReq->wId, (BYTE)nation);
                else
                {
                    int nType = pReq->wId;
                    nHave = 0;
                    for (TWorker *pW = pTown->pWorkers; pW != 0; pW = pW->pNext)
                        nHave += pW->pUnit10->wType == nType ? 1 : pW->pUnit10->CountInBuildQueue(nType);
                    for (TBldg *pB = pTown->pBldgs; pB != 0; pB = pB->pNext)
                    {
                        nHave += pB->pUnit->wType == nType ? 1 : pB->pUnit->CountInBuildQueue(nType);
                        switch (nType)
                        {
                        case 0x83:
                            nHave += pB->pUnit->wType == 0x84 ? 1 : pB->pUnit->CountInBuildQueue(0x84);
                        case 0x84:
                            nHave += pB->pUnit->wType == 0x85 ? 1 : pB->pUnit->CountInBuildQueue(0x85);
                            break;
                        case 0x8F:
                            nHave += pB->pUnit->wType == 0x92 ? 1 : pB->pUnit->CountInBuildQueue(0x92);
                            nHave += pB->pUnit->wType == 0x90 ? 1 : pB->pUnit->CountInBuildQueue(0x90);
                            break;
                        case 0x8D:
                            nHave += pB->pUnit->wType == 0x89 ? 1 : pB->pUnit->CountInBuildQueue(0x89);
                            break;
                        }
                    }
                }
                if (pTown->bUnknown1E != 0)
                    continue;
            }
            else if (nKind == 1)
            {
                int nUpgrade = pReq->wId;
                nHave = get_upgrade_level(pTown->bPlayer, nUpgrade);
                for (CUnit *pU = CUnit::GetPlayerUnitList(pTown->bPlayer); pU != 0; pU = pU->pPlayerUnitNext)
                {
                    if (!pU->IsDeadOrDying() && pU->bOrderId == 0x4C && pU->bUpgradeType == nUpgrade)
                        nHave++;
                }
            }
            else
                nHave = town_is_researching(pTown, pReq->wId);

            unsigned nWant = pReq->bPacked >> 3;
            if (pReq->wId == get_player_worker_type(nation) && nWant > pTown->bUnknown1B)
                nWant = pTown->bUnknown1B;
            if (nHave >= nWant)
                continue;
            for (unsigned n = nWant - nHave; n != 0; n--)
            {
                unsigned nPrio = pReq->bUnknown01;
                BYTE nK = (pReq->bPacked >> 1) & 3;
                if ((nK == 1 || nK == 2) && aiscript_has_campaign(nation))
                {
                    if (nPlanned > nPrio)
                        nPrio = 0;
                    else
                    {
                        nPrio -= nPlanned;
                        nPlanned++;
                    }
                    banker_set_has_upgrades(nation, 0);
                }
                if (!aiscript_has_campaign(pTown->bPlayer) && (pReq->bPacked & 6) == 0
                    && pReq->wId == get_player_depot_type(pTown->bPlayer)
                    && gPlayerMinerals[pTown->bPlayer] < 600)
                    nPrio = 150;
                banker_demand_town_request(pTown->bPlayer, (intptr_t)pTown, (pReq->bPacked >> 1) & 3, pReq->wId, nPrio);
            }
        }
        if (pTown->bUnknown1A > 1
            || (pTown->bUnknown1A == 1 && pTown->pUnit24 != 0
                && get_player_depot_type(pTown->bPlayer) == 0x83))
            town_queue_refinery(pTown);
    }
    if (bSpare)
    {
        for (TTown *pTown = gPlayerTowns_g[nation].pTowns; pTown != 0; pTown = pTown->pNext)
        {
            CUnit *pMain = pTown->pUnit24;
            if (pMain != 0 && (pMain->dwFlags & 1) && pTown->bUnknown1A < pTown->bUnknown1B)
                SAI_Build_aiscript_get_old_peons(pTown, 1);
        }
    }
    if (pLast != 0)
    {
        town_queue_supply(pLast);
        tai_captain_crew_bunkers((intptr_t)pLast, nation);
        tai_captain_request_turret_build((intptr_t)pLast, nation);
    }
}

// 0x486A20  sai_build_init
void sai_build_init()
{
    ((TSaveNode<TWorker, 1000> *)&gPeons_g)->Init();
    ((TSaveNode<TBldg, 1000> *)&gBldgs_g)->Init();
    ((TSaveNode<TTown, 100> *)&gTowns_g)->Init();

    gPlayerTowns_g[0].pPool = &gTowns_g;
    gPlayerTowns_g[0].pTowns = 0;
    gPlayerTowns_g[1].pPool = &gTowns_g;
    gPlayerTowns_g[1].pTowns = 0;
    gPlayerTowns_g[2].pPool = &gTowns_g;
    gPlayerTowns_g[2].pTowns = 0;
    gPlayerTowns_g[3].pPool = &gTowns_g;
    gPlayerTowns_g[3].pTowns = 0;
    gPlayerTowns_g[4].pPool = &gTowns_g;
    gPlayerTowns_g[4].pTowns = 0;
    gPlayerTowns_g[5].pPool = &gTowns_g;
    gPlayerTowns_g[5].pTowns = 0;
    gPlayerTowns_g[6].pPool = &gTowns_g;
    gPlayerTowns_g[6].pTowns = 0;
    gPlayerTowns_g[7].pPool = &gTowns_g;
    gPlayerTowns_g[7].pTowns = 0;
    // Hyperion: and the nations of Hyperion's slots
    for (int i = 8; i < PlayerLobbySlots(); i++) {
        gPlayerTowns_g[i].pPool = &gTowns_g;
        gPlayerTowns_g[i].pTowns = 0;
    }

    dword_685574 = 0;
}
// 0x486AF0  SAI_Build_RemoveUnit
// 0x486AF0, 172 B.
BOOL SAI_Build_RemoveUnit(CUnit *pUnit, int nArg1)
{

    town_forget_unit(pUnit);

    DWORD_PTR dwRec = pUnit->dwUnion134;
    if (dwRec == 0)
        return 0;

    TTown *pTown;
    if (nArg1 == 0
        && ((gulUnitTypeFlags[pUnit->wType] & 1) == 0 || pUnit->wType == 0xBC)
        && pUnit->wType != 0x23
        && pUnit->wType != 0x24
        && pUnit->wType != 0x2A)
    {
        if ((gulUnitTypeFlags[pUnit->wType] & 8) != 0)
        {
            pTown = ((TWorker *)dwRec)->pTown;
            ((TSaveContainer<TWorker, 1000> *)&pTown->pWorkerPool)->Free((TWorker *)dwRec);
            --pTown->bUnknown1A;
        }
        else
        {
            return 0;
        }
    }
    else
    {
        pTown = ((TBldg *)dwRec)->pTown;
        ((TSaveContainer<TBldg, 1000> *)&pTown->pBldgPool)->Free((TBldg *)dwRec);
    }

    if (pTown != 0)
    {
        if (pUnit == pTown->pUnit28)
            pTown->pUnit28 = 0;
        if (pUnit == pTown->pUnit24)
            pTown->pUnit24 = 0;
    }
    if (nArg1 == 0 && pTown != 0)
        town_delete_if_empty((intptr_t)pTown);
    pUnit->dwUnion134 = 0;
    return 1;
}

// 0x486BA0  town_rejoin_worker
// 0x486BA0, 69 B.
void town_rejoin_worker(CUnit *pUnit)
{
    if (gPlayerData[pUnit->bPlayer].nType != 1)
        return;

    TWorker *pRec = (TWorker *)pUnit->dwUnion134;
    if (pRec == 0)
        return;

    TTown *pTown = pRec->pTown;
    pUnit->dwUnion134 = 0;
    ((TSaveContainer<TWorker, 1000> *)&pTown->pWorkerPool)->Free(pRec);
    --pTown->bUnknown1A;
    town_add_unit(pTown, pUnit);
}

// 0x486BF0  CUnit::AIUpdateOverlord
// 0x486BF0, 84 B.
void CUnit::AIUpdateOverlord()
{
    CSprite *pSprite = this->pSprite;
    if (this->wMoveTargetX != pSprite->wPosX)
        return;
    if (this->wMoveTargetY != PosU(pSprite->wPosY))
        return;

    if (!((this->dwFlags & 0x80000) ? 2 : 1))
        return;

    int x, y;
    patrol_pick_area(this, &x, &y);
    this->SetDestination_xy(x, y);
}

// 0x486C50  get_town_race
// 0x486C50  get_town_race  44 B
int get_town_race(void *pArg0)
{
    TTown *pTown = (TTown *)pArg0;
    CUnit *pUnit = (CUnit *)ai_nearest_enemy_building(pTown->bPlayer, pTown->nX, pTown->nY);
    if (pUnit != 0)
        return gPlayerData[pUnit->bPlayer].nRace;
    return 1;
}

// 0x486C80  town_expand_tick

// The three TTown captain slots, tried in order for one captain kind. Inlined
// at both call sites below.
static CUnit *sai_town_captain(TTown *pTown, int nKind)
{
    if (pTown->pUnit30 != 0 && town_gas_class(pTown->bPlayer, pTown->pUnit30) == nKind)
        return pTown->pUnit30;
    if (pTown->pUnit34 != 0 && town_gas_class(pTown->bPlayer, pTown->pUnit34) == nKind)
        return pTown->pUnit34;
    if (pTown->pUnit38 != 0 && town_gas_class(pTown->bPlayer, pTown->pUnit38) == nKind)
        return pTown->pUnit38;
    return 0;
}

// One player per call, round-robin: tick his
// towns, then decide whether he is settled enough to place another. Each money
// bound re-reads its global; share one local and MSVC folds the pair into
// `add eax,-LO / cmp eax,span / ja`. town_gas_class needs its __declspec(noinline)
// here, or all six calls inline and the body is 675 B.
void town_expand_tick()
{
    int nPlayer = dword_685574 + 1;
    if (nPlayer >= PlayerLobbySlots())  // Hyperion
        nPlayer = 0;
    dword_685574 = nPlayer;

    TTown *pTown = gPlayerTowns_g[nPlayer].pTowns;
    if (pTown == 0)
        return;

    unsigned nTowns = 0;
    unsigned nSum = 0;
    unsigned nCaptains = 0;
    int bBusy = 0;

    do
    {
        ++nTowns;
        CUnit *pCaptain = sai_town_captain(pTown, 3);
        if (pCaptain == 0)
            pCaptain = sai_town_captain(pTown, 1);
        if (pCaptain != 0)
            ++nCaptains;

        if (pTown->bUnknown19 == 0 || pTown->pUnit24 == 0 ||
            (pTown->pUnit24->dwFlags & 1) == 0)
            bBusy = 1;

        town_tick(pTown);

        if (pTown->bTownPlan != 0)
            nSum += sai_town_mineral_target((void *)pTown->bTownPlan, pTown->bPlayer);
        if (pTown->bUnknown1E != 0)
            pTown->bUnknown1E--;

        pTown = pTown->pNext;
    } while (pTown != 0);

    if (bBusy)
        return;

    if (nSum != 0 && (nSum < 0x10 || (nTowns > 1 && nSum < 0x14)))
        goto place;
    if (nTowns > 1 && nCaptains < 2)
        goto place;

    {
        if (gPlayerMinerals[dword_685574] < 0x7D0)
            return;
        if (gPlayerMinerals[dword_685574] >= 0x1388)
            return;
        if (gPlayerGas[dword_685574] < 0x3E8)
            return;
        if (gPlayerGas[dword_685574] >= 0xBB8)
            return;
        if (!banker_is_ahead(dword_685574))
            return;

        unsigned nPlans = 0;
        for (TTown *pNext = gPlayerTowns_g[dword_685574].pTowns; pNext != 0;
             pNext = pNext->pNext)
        {
            if (pNext->bTownPlan != 0)
                nPlans += sai_town_mineral_fields(pNext->bTownPlan);
        }
        if (nPlans >= 0x18)
            return;
    }

place:
    {
        TTown *pFirst = gPlayerTowns_g[dword_685574].pTowns;
        if (pFirst == 0)
            return;
        aiscript_run_panic(dword_685574, pFirst->nX, pFirst->nY, pFirst);
    }
}

// 0x486E80  town_from_area
// 0x486E80, 272 B. Build a town out of every unit of one player inside a
// rectangle, then anchor it on the first depot found, else the first worker,
// else the rectangle's centre.

TTown *town_from_area(int nPlayer, rect32_t *pRect, int nPlan)
{
    rect16_t area;
    TTown *pTown;

    area.top = (short)pRect->top;
    area.bottom = (short)pRect->bottom;
    area.left = (short)pRect->left;
    area.right = (short)pRect->right;

    CUnit **ppCur = get_units_in_rect_filtered(&area, unit_belongs_to_player, nPlayer);
    CUnit **ppList = ppCur;
    CUnit *pUnit = *ppCur;
    pTown = 0;
    if (pUnit != 0)
    {
        pTown = (TTown *)NewTown(nPlayer, 0, 0);
        if (pTown != 0)
        {
            CUnit *pAnchor;

            pTown->bTownPlan = (BYTE)nPlan;
            pTown->bUnknown1D = (nPlan != 0);
            pAnchor = 0;
            do
            {
                if (pUnit->dwUnion134 == 0)
                {
                    town_add_unit(pTown, pUnit);
                    switch (pUnit->wType)
                    {
                    case 7: case 41: case 64:          // the three workers
                        if (pAnchor == 0)
                            pAnchor = pUnit;
                        break;
                    case 106: case 131: case 132: case 133: case 154:
                        pAnchor = pUnit;               // the five depots
                        break;
                    }
                }
                ++ppCur;
                pUnit = *ppCur;
            } while (pUnit != 0);

            if (pAnchor != 0)
            {
                pTown->nX = pAnchor->pSprite->wPosX;
                pTown->nY = pAnchor->pSprite->wPosY;
            }
            else
            {
                pTown->nX = (short)((pRect->right - pRect->left) / 2 + pRect->left);
                pTown->nY = (short)((pRect->bottom - pRect->top) / 2 + pRect->top);
            }
        }
    }
    finish_unit_iteration(ppList);
    return pTown;
}

// 0x487030  town_from_point
// 0x487030  town_from_point  218 B
TTown *town_from_point(int nArg0, int nArg1, int nArg2, int nArg3)
{
    CUnit *pUnit = CUnit::GetPlayerUnitList(nArg0);
    if (pUnit == 0)
        return 0;

    int bNoTown = (gPlayerTowns_g[nArg0].pTowns == 0);
    if (bNoTown)
        guard_free_unmanned(nArg0);

    TTown *pTown = (TTown *)NewTown(nArg0, (WORD)nArg1, (WORD)nArg2);
    if (pTown == 0)
        return 0;

    pTown->bTownPlan = (BYTE)nArg3;
    pTown->bUnknown1D = (BYTE)(nArg3 != 0);

    do
    {
        if (!pUnit->IsDeadOrDying() && !tai_unit_is_forming_or_incomplete(pUnit) && pUnit->dwUnion134 == 0)
            town_add_unit(pTown, pUnit);
        pUnit = pUnit->pPlayerUnitNext;
    } while (pUnit != 0);

    if (aiscript_has_campaign(pTown->bPlayer) == 0)
    {
        sai_alert_frontier(pTown->bPlayer, pTown->nX, pTown->nY, bNoTown ? 5 : 2);
        tai_captain_defend_paths_to_enemies(pTown->bPlayer, pTown->nX, pTown->nY);
    }
    return pTown;
}

// 0x487110  bldg_tick
// 0x487110, 349 B. The per-unit AI tick for a unit that belongs to a town: run
// the type-specific update, then dispatch the town's top demand.
// bldg_build_addon is defined below in VA order; the header seat proposed for it
// carries this same spelling, so the two cannot disagree.

int bldg_tick(CUnit *pUnit)
{
    switch (pUnit->wType)
    {
    case 0x2A:
        pUnit->AIUpdateOverlord();
        break;
    case 0x6B:
        ai_update_unit_spells(pUnit, 0);
        break;
    case 0x6C:
        AIUpdateSilo(pUnit);
        break;
    }

    TBldg *pBldg = (TBldg *)pUnit->dwUnion134;
    if (pBldg == 0)
        return 1;
    if (pBldg->bType != 3)
        return 1;

    int nPlayer = pUnit->bPlayer;
    int nFlag = 0;
    if (gPlayerMinerals[nPlayer] > 0x5DC && gPlayerGas[nPlayer] > 0x5DC &&
        !pUnit->IsConstructing())
    {
        if (town_queue_idle_building(nPlayer, pUnit, 0xA0))
            nFlag = 1;
    }

    switch (sai_top_demand_type(nPlayer))
    {
    case 4:
        bldg_train_top_demand(nPlayer, pUnit, pBldg);
        break;
    case 5:
        sai_process_top_demand_upgrade(nPlayer, pUnit);
        break;
    case 6:
        sai_process_top_demand_tech(nPlayer, pUnit);
        break;
    case 7:
        sai_process_top_demand_unit(nPlayer, pUnit, pBldg);
        break;
    case 3:
        return bldg_build_addon(nPlayer, pUnit, pBldg);
    case 8:
        return bldg_arm_nuke(nPlayer, pUnit, (intptr_t)pBldg);
    case 0:
        if (!town_queue_idle_building(nPlayer, pUnit, 0x32))
            break;
        nFlag = 1;
        // fall through -- MSVC tail-duplicates the call below with nFlag in EAX
    case 1:
    case 2:
        return bldg_make_top_demand(nPlayer, pUnit, pBldg, nFlag);
    }
    return 1;
}

// 0x4872A0  town_first_town_xy
// 0x4872A0  town_first_town_xy  44 B
BOOL town_first_town_xy(int nArg0, void *pArg1, void *pArg2)
{
    TTown *pTown = gPlayerTowns_g[nArg0].pTowns;
    if (pTown != 0)
    {
        *(int *)pArg1 = pTown->nX;
        *(int *)pArg2 = pTown->nY;
        return 1;
    }
    return 0;
}

// 0x4872D0  town_count
// The player town-list walk: gPlayerTowns_g[p].pTowns, then
// TTown::pNext to the end.

int town_count(int nPlayer)
{
    int nCount = 0;
    for (TTown *pTown = gPlayerTowns_g[nPlayer].pTowns; pTown != 0; pTown = pTown->pNext)
        ++nCount;
    return nCount;
}

// 0x487510  town_id
int town_id(TTown *pTown)
{
    if (pTown == 0)
        return 0;
    return (int)((unsigned int)((BYTE *)pTown - (BYTE *)gTowns_g.aTowns) / sizeof(TTown)) + 1;
}

static int sai_town_unit_count(CUnit *pUnit, int nUnitType, int bCompleted)
{
    if (bCompleted != 0 && (pUnit->dwFlags & 1) == 0)
        return 0;
    if (pUnit->wType == nUnitType)
        return 1;
    if (bCompleted != 0)
        return 0;
    return pUnit->CountInBuildQueue(nUnitType);
}

// Count a town's units of one type, optionally only completed ones. A
// building also counts toward the type its add-ons and morphs derive from.
int __fastcall town_unit_count(TTown *pTown, int nUnitType, int bCompleted)
{
    int nCount = 0;
    for (TWorker *pW = pTown->pWorkers; pW != 0; pW = pW->pNext)
        nCount += sai_town_unit_count(pW->pUnit10, nUnitType, bCompleted);
    for (TBldg *pB = pTown->pBldgs; pB != 0; pB = pB->pNext)
    {
        nCount += sai_town_unit_count(pB->pUnit, nUnitType, bCompleted);
        switch (nUnitType)
        {
        case 0x83:
            nCount += sai_town_unit_count(pB->pUnit, 0x84, bCompleted);
        case 0x84:
            nCount += sai_town_unit_count(pB->pUnit, 0x85, bCompleted);
            break;
        case 0x8F:
            nCount += sai_town_unit_count(pB->pUnit, 0x92, bCompleted);
            nCount += sai_town_unit_count(pB->pUnit, 0x90, bCompleted);
            break;
        case 0x8D:
            nCount += sai_town_unit_count(pB->pUnit, 0x89, bCompleted);
            break;
        }
    }
    return nCount;
}

// 0x487530  town_repair_worker
// 0x487530, 18 B.
CUnit *__fastcall town_repair_worker(CUnit *pUnit)
{
    TWorker *pRec = (TWorker *)pUnit->dwUnion134;
    if (pRec == 0)
        return 0;
    return pRec->pTown->pUnit28;
}

// 0x487550  town_need
// A guarded forwarder. Both bounds are unsigned compares
// (`jnbe` / `jnb`), so the parameters must be compared as unsigned or MSVC
// emits the signed forms.

void town_need(TTown *pTown, int nKind, int nId, int nArg3, int nArg4)
{
    if ((unsigned int)nKind > 0x1E)
        return;
    if ((unsigned int)nId >= 0xE4)
        return;
    add_request_to_town_queue(pTown, 0, nKind, (WORD)nId, (BYTE)nArg3, nArg4);
}

// 0x487580  worker_tick
// CUnit::FindFreeMineral is seated in CUnit.h as the member the ledger names.

// One worker's AI tick: count the wait down,
// then hand the worker an order -- mine, gas or a return target -- from its
// town. pRes must be declared before bKind, which is what puts it in a stack
// slot rather than EBX, and the three-way slot pick must be three separate
// town_gas_slot calls so MSVC tail-merges them and pushes each constant in its own
// branch.
void worker_tick(CUnit *pUnit)
{
    TWorker *pW = (TWorker *)pUnit->dwUnion134;
    if (pW == 0)
        return;

    // +0x0B is a BYTE the oracle reads on its own; the header spells 0x0A..0x0B
    // as one WORD, so it is reached through that member here.
    BYTE bWait = ((BYTE *)&pW->wUnknown0A)[1];
    TTown *pTown = pW->pTown;
    if (bWait != 0)
    {
        ((BYTE *)&pW->wUnknown0A)[1] = bWait - 1;
        return;
    }

    pW->dwUnknown0C = g_dwElapsedTime;
    if (pUnit->GetRace() == 1)
    {
        if (town_repair_order(pUnit, pTown))
            return;
    }
    if (worker_build_top_demand(pTown, pUnit))
        return;
    if (pTown->pUnit24 == 0)
        return;
    if (!pTown->pUnit24->IsResourceDest())
        return;

    CUnit *pRes = pUnit->pUnionC8_SavedOrderTarget;
    BYTE bKind = pW->bUnknown09;
    if (bKind == 1)
    {
        if (pRes != 0 && pRes->IsMineralSrc())
        {
            worker_harvest_order(pUnit, pRes, 0);
            return;
        }

        pRes = pTown->pUnit2C;
        if (pRes == 0 || pRes->bUnionD9 != 0)
        {
            mpoint_t  pt;
            pt.x = pUnit->pSprite->wPosX;
            pt.y = pUnit->pSprite->wPosY;
            pRes = pUnit->FindFreeMineral(pt, 12 >= pTown->bUnknown1A);
            if (pRes == 0)
                pRes = pTown->pUnit2C;
        }

        CUnit *pAlt = town_gas_slot(pTown, 3, 1);
        if (pAlt == 0)
            pAlt = town_gas_slot(pTown, 2, 1);
        worker_harvest_order(pUnit, pRes, pAlt);
        return;
    }

    CUnit *pFound;
    if (bKind == 2)
        pFound = town_gas_slot(pTown, 3, 1);
    else if (bKind == 3)
        pFound = town_gas_slot(pTown, 3, 2);
    else
        pFound = town_gas_slot(pTown, 3, 3);
    if (pFound == 0) pFound = town_gas_slot(pTown, 3, 1);
    if (pFound == 0) pFound = town_gas_slot(pTown, 3, 2);
    if (pFound == 0) pFound = town_gas_slot(pTown, 3, 3);
    if (pFound == 0) pFound = town_gas_slot(pTown, 2, 1);
    if (pFound == 0) pFound = town_gas_slot(pTown, 2, 2);
    if (pFound == 0) pFound = town_gas_slot(pTown, 2, 3);
    worker_harvest_order(pUnit, pFound, pTown->pUnit2C);
}

// 0x487730  town_pending_costs
// town_pending_costs (0x487730, 152 B) -- subtract every in-progress town build from
// the caller's ore/gas running totals. Definition copied character for
// character from src117/h/SAI_Banker.h:571.
//
// TWorker +0x10 IS A `CUnit *`: `mov ecx, [esi+10h]` then
// `movzx eax, byte ptr [ecx+4Dh]` (CUnit::bOrderId), and ECX is then `this`
// for CUnit::GetBuildUnit. That corroborates SAI_Build.h's claim that TWorker
// and TBldg share their first 0x18 bytes (TBldg +0x10 is already `pUnit`).

// The three pushes before CUnit::GetBuildUnit are not its arguments: MSVC
// evaluates right to left, so bldg_get_unit_cost's two trailing BOOLs are
// pushed first and GetBuildUnit's own `0` last.
void town_pending_costs(int nation, DWORD *pOre, DWORD *pGas)
{
    for (TTown *pTown = gPlayerTowns_g[nation].pTowns; pTown != 0; pTown = pTown->pNext)
    {
        for (TWorker *pWorker = pTown->pWorkers; pWorker != 0; pWorker = pWorker->pNext)
        {
            CUnit *pUnit = pWorker->pUnit10;
            int nOrder = pUnit->bOrderId;
            if (nOrder == 0x1E || nOrder == 0x1F || nOrder == 0x46 || nOrder == 0x1A || nOrder == 0x19)
            {
                if (bldg_get_unit_cost((BYTE)nation, pUnit->GetBuildUnit(0), TRUE, TRUE))
                {
                    *pOre -= get_staged_ore_cost(nation);
                    *pGas -= get_staged_gas_cost(nation);
                }
            }
        }
    }
}

// 0x4877D0  town_ptr
TTown *town_ptr(int nId)
{
    if (nId == 0)
        return 0;
    return &gTowns_g.aTowns[nId - 1];
}

// 0x4877F0  aiscript_get_region_size
// aiscript_get_region_size (0x4877F0, 158 B) -- breadth-first sum of region
// sizes outward from a town until the running total reaches nArg1. The first
// argument is a `TTown *`: `movsx word ptr [ecx+20h]` and `[ecx+22h]` are
// TTown::nX and nY.
//
// The frame gives the queue's size: `sub esp, 9CCh` with two 4-byte locals
// below it leaves 0x9C4 == 625 * 4, and the wrap constant is
// `cmp reg, 271h` == 625.

// The region-record address is `epGlob + (region + 0x1128) * 64`, with the
// +0x1128 inside the index rather than a displacement: the oracle emits
// `add edx, 1128h` / `shl edx, 6h` / `movzx eax, word ptr [edx+eax*1]`. Had
// the source been `((rec *)((BYTE *)epGlob + 0x44A00))[region].wSize` MSVC
// would have folded 0x44A00 into the memory operand as a displacement, in one
// 8-byte instruction, which is what it does in GetRegionPaths
// (`shl ecx, 6h` / `add ecx, [epGlob]` / `mov eax, [ecx+44A08h]`). So the two
// bodies spell the same address two different ways, and this one scales an
// index biased by 0x1128. The record type is not declared here: only the WORD
// at its +0 is read.
int aiscript_get_region_size(TTown *pTown, int nArg1)
{
    int aQueue[625];

    // The statement order is the lever: the store has to precede `nHead = 0`
    // in source order, or MSVC schedules `xor edi, edi` before the call and
    // `lea esi, [edi+1]` before the store.
    aQueue[0] = GetRegion_xy(pTown->nX, pTown->nY);
    int nHead = 0;
    unsigned int nTotal = 0;
    int nTail = nHead + 1;

    do
    {
        int nRegion = aQueue[nHead];
        ++nHead;
        if (nHead == 625)
            nHead = 0;

        nTotal += ((ep_glob_t *)epGlob)->aRegions[(WORD)nRegion].wTiles;
        if (nTotal >= (unsigned int)nArg1)
            return nArg1;

        WORD *pPaths;
        int nCount = GetRegionPaths((WORD)nRegion, &pPaths);
        while (nCount != 0)
        {
            aQueue[nTail] = *pPaths;
            ++nTail;
            ++pPaths;
            if (nTail == 625)
                nTail = 0;
            --nCount;
        }
    } while (nHead != nTail);

    return (int)nTotal;
}

// 0x487890  aiscript_tech
// A `ret N` forwarder with a packed register and stack argument list.

void aiscript_tech(TTown *pTown, int nTech, int nArg2)
{
    add_request_to_town_queue(pTown, 2, 1, (WORD)nTech, (BYTE)nArg2, 0);
}

// 0x4878B0  aiscript_upgrade
void aiscript_upgrade(TTown *pTown, int nPrio, int nUpgrade, int nArg3)
{
    add_request_to_town_queue(pTown, 1, nPrio, (WORD)nUpgrade, (BYTE)nArg3, 0);
}

// 0x4878D0  bldg_cannot_build
// bldg_cannot_build (0x4878D0, 43 B) -- "is this unit type allowed and affordable".
// TTAllowed, then a tail jmp into banker_at_define_max. The first argument is a
// `CUnit *`: the +0x4C read, and it is pushed as TTAllowed's third argument,
// which is typed `CUnit *`.
BOOL bldg_cannot_build(CUnit *pUnit, int nArg1)
{
    int nPlayer = pUnit->bPlayer;
    WORD wType = (WORD)nArg1;

    if (TTAllowed(wType, nPlayer, pUnit) != 1)
        return TRUE;
    return banker_at_define_max(nPlayer, wType);
}
// 0x487900  town_rejoin_building
// 0x487900, 66 B.
void town_rejoin_building(CUnit *pUnit)
{
    if (gPlayerData[pUnit->bPlayer].nType != 1)
        return;

    TBldg *pRec = (TBldg *)pUnit->dwUnion134;
    if (pRec == 0)
        return;

    TTown *pTown = pRec->pTown;
    pUnit->dwUnion134 = 0;
    ((TSaveContainer<TBldg, 1000> *)&pTown->pBldgPool)->Free(pRec);
    town_add_unit(pTown, pUnit);
}

// Hand a newly produced unit to whatever its producer's building record
// serves: a guard post, a captain, or the unit's own captain.
void bldg_hand_over_product(CUnit *pUnit, CUnit *pParent)
{
    switch (pParent->wType)
    {
    case 0x2C:
    case 0x3E:
        return;
    }
    if ((pParent->dwFlags & 0x40000000) != 0)
    {
        tai_soldier_add_to_captain(tai_captain_for_unit(pParent), pParent, 1);
        return;
    }
    WORD wType = pParent->wType;
    if (wType == 0x44 || wType == 0x3F || wType == 0x67 || wType == 0x0E || wType == 0x54)
        return;
    TBldg *pRec = (TBldg *)pUnit->dwUnion134;
    if (pRec == 0)
        return;
    int nSlot;
    if (pUnit == pParent)
    {
        nSlot = 0;
        town_forget_unit(pParent);
        TBldg *pRec2 = (TBldg *)pParent->dwUnion134;
        if (pRec2 != 0)
        {
            TTown *pTown2 = pRec2->pTown;
            ((TSaveContainer<TBldg,1000> *)&pTown2->pBldgPool)->Free(pRec2);
            if (pTown2 != 0)
            {
                if (pParent == pTown2->pUnit28)
                    pTown2->pUnit28 = 0;
                if (pParent == pTown2->pUnit24)
                    pTown2->pUnit24 = 0;
            }
            pParent->dwUnion134 = 0;
        }
    }
    else if (pUnit->wType == pParent->wType)
        nSlot = 0;
    else
        nSlot = pUnit->bBuildQueueSlot;
    DWORD_PTR dwSlot = pRec->aSlots[nSlot];
    if (pRec->aSlotKind[nSlot] == 2)
    {
        if (*(CUnit **)((BYTE *)dwSlot + offsetof(TGuard, pUnit)) == 0)
        {
            guard_crew_post((TGuard *)dwSlot, pParent);
            return;
        }
        pRec->aSlotKind[nSlot] = 1;
        dwSlot = (DWORD_PTR)tai_captain_for_unit(pParent);
        pRec->aSlots[nSlot] = dwSlot;
    }
    if (pRec->aSlotKind[nSlot] != 1)
        pParent->MakeGuardPost();
    else
        tai_soldier_add_to_captain((TAI_Captain *)dwSlot, pParent, 0);
}

// 0x487950  worker_id
// The inverse: pointer difference by the stride, plus one. MSVC
// lowers the unsigned divide to the reciprocal magic; the signedness of the
// difference is the lever (an unsigned divide is `mul`/`shr`, a signed one adds
// a sign fixup), so the subtraction must be done on unsigned values.

int worker_id(TWorker *pWorker)
{
    if (pWorker == 0)
        return 0;
    return (int)((unsigned int)((BYTE *)pWorker - (BYTE *)gPeons_g.aWorkers) / sizeof(TWorker)) + 1;
}

// 0x487970  worker_ptr
// gPlayerData is already emitted by src117/gen/globals117.h as
// `PlayerData[8]` @0x6726C8 (net_data.cpp) -- do not re-spell it.

// The 1-based pool bijection, id to pointer.
// `imul`/`lea` by the stride against a base biased down one element. Writing
// `&gPeons_g[nId - 1]` is what folds the bias into the addend; writing
// `gPeons_g + nId - 1` schedules the subtract separately.

TWorker *worker_ptr(int nId)
{
    if (nId == 0)
        return 0;
    return &gPeons_g.aWorkers[nId - 1];
}

// 0x487AC0  town_place_point
// town_place_point (0x487AC0, 164 B) -- resolve a town "location kind" to a map
// position. The two out-params are `int *`, both stored to with a 4-byte `mov`
// from a sign-extended WORD.
//
// The `(short)` on pSprite->wPosX is needed: the oracle reads both coordinates
// with `movsx`, and CSprite.h declares +0x14 `WORD wPosX` but +0x16
// `short wPosY`. Without the cast the X read is a `movzx` and the body cannot
// match. One movsx site does not overturn the declared type, so the cast is
// where the evidence goes, and the finding stays with CSprite.h.
BOOL town_place_point(TTown *pTown, int nArg1, int *pX, int *pY)
{

    if (nArg1 == 0)
    {
        *pX = pTown->nX;
        *pY = pTown->nY;
        return TRUE;
    }
    if (nArg1 == 1)
    {
        if (pTown->pUnit2C == 0)
            return FALSE;
        *pX = PosS(pTown->pUnit2C->pSprite->wPosX);
        *pY = pTown->pUnit2C->pSprite->wPosY;
        return TRUE;
    }

    CUnit *pUnit = town_gas_slot(pTown, 3, 1);
    if (pUnit == 0)
    {
        pUnit = town_gas_slot(pTown, 2, 1);
        if (pUnit == 0)
            return FALSE;
    }
    *pX = PosS(pUnit->pSprite->wPosX);
    *pY = pUnit->pSprite->wPosY;
    return TRUE;
}

// 0x487B70  town_get_base_id
// A bare field accessor.

int town_get_base_id(TTown *pTown)
{
    return pTown->bTownPlan;
}

// 0x487B80  town_hold_workers
// The CUnit +0x134 chain: `[+0x134]` then `[+0x14]` reaches the
// owning town without testing the family tag.

void town_hold_workers(CUnit *pUnit)
{
    TBldg *pBldg = (TBldg *)pUnit->dwUnion134;
    if (pBldg != 0)
        pBldg->pTown->bUnknown1E = 1;
}

// 0x487BA0  player_unit_count

static int sai_count_unit(CUnit *pUnit, int nUnitType)
{
    if (pUnit->wType == nUnitType)
        return 1;
    return pUnit->CountInBuildQueue(nUnitType);
}

// Count how many of nUnitType the player
// has or is building
// across all his towns. Command centers also count their add-ons. The four case
// labels must stay in the order 0x83, 0x84, 0x8F, 0x8D -- source order sets the
// physical block order the jump table indexes -- and nTotal must be declared
// before pTown, which is what puts it in EDX rather than ECX.
int player_unit_count(int nPlayer, int nUnitType)
{
    int nTotal = 0;
    TTown *pTown = gPlayerTowns_g[nPlayer].pTowns;

    while (pTown != 0)
    {
        int nCount = 0;
        for (TWorker *pW = pTown->pWorkers; pW != 0; pW = pW->pNext)
            nCount += sai_count_unit(pW->pUnit10, nUnitType);

        for (TBldg *pB = pTown->pBldgs; pB != 0; pB = pB->pNext)
        {
            nCount += sai_count_unit(pB->pUnit, nUnitType);
            switch (nUnitType)
            {
            case 0x83:
                nCount += sai_count_unit(pB->pUnit, 0x84);
                // fall through
            case 0x84:
                nCount += sai_count_unit(pB->pUnit, 0x85);
                break;
            case 0x8F:
                nCount += sai_count_unit(pB->pUnit, 0x92);
                nCount += sai_count_unit(pB->pUnit, 0x90);
                break;
            case 0x8D:
                nCount += sai_count_unit(pB->pUnit, 0x89);
                break;
            }
        }
        nTotal += nCount;
        pTown = pTown->pNext;
    }
    return nTotal;
}

// 0x487D30  bldg_train_top_demand
// bldg_train_top_demand (0x487D30, 167 B) -- build the top demand's unit at a building.
// The second argument is a `CUnit *` and the third a `TBldg *`, from the one
// call site: bldg_tick loads `mov ebx, [esi+134h]`, tests
// `cmp byte ptr [ebx+8h], 3h` (TBldg::bType == 3) and then pushes EBX here, so
// `[esi+14h]` is TBldg::pTown.
void bldg_train_top_demand(int nArg0, CUnit *pUnit, TBldg *pBldg)
{

    if (sai_top_demand_captain(nArg0) != (intptr_t)pBldg->pTown)
        return;

    int nType = sai_top_demand_arg(nArg0);
    if (!unit_can_make_type(pUnit, (WORD)nType))
        return;
    if (TTAllowed((WORD)nType, nArg0, pUnit) != 1)
        return;
    if (!ai_stage_unit_cost(nArg0, nType))
        return;
    if (banker_at_define_max(nArg0, nType) != 0)
    {
        banker_drop_top_demand(nArg0);
        return;
    }
    if (!pUnit->AddBuildQueue(nType))
        return;

    if (pUnit->wType == 0x23)
        pUnit->SetOrder(0x2A);
    else
        pUnit->SetBackgroundOrder(0x26);
    banker_spend_top_demand(nArg0);
}

// Dispatch the player's top build demand to pUnit's town: pair up units that
// merge (archons, dark archons), take an idle unit for the job, or build it.
// When the cost cannot be staged the oracle reads the allowed error and
// ignores it, going straight to the failure handling.
int bldg_make_top_demand(int nPlayer, CUnit *pUnit, TBldg *pBldg, int nFlag)
{
    int nType = sai_top_demand_arg(nPlayer);
    CUnit *pSecond = 0;
    CUnit *pFirst = pUnit;
    int nErr;
    if (nType == 0x2C || nType == 0x3E)
    {
        pFirst = tai_captain_find_spare_mutalisk(nPlayer);
        if (pFirst == 0)
        {
            banker_drop_top_demand(nPlayer);
            return 0;
        }
    }
    else if (nType == 0x44)
    {
        if (!tai_captain_find_two_high_templar(nPlayer, &pFirst, &pSecond))
        {
            banker_drop_top_demand(nPlayer);
            return 0;
        }
        pFirst->SetOrder_CUnit(0x69, pSecond);
        pSecond->SetOrder_CUnit(0x69, pFirst);
        banker_spend_top_demand(nPlayer);
        return 0;
    }
    else if (nType == 0x3F)
    {
        if (!tai_captain_find_two_dark_templar(nPlayer, &pFirst, &pSecond))
        {
            banker_drop_top_demand(nPlayer);
            return 0;
        }
        pFirst->SetOrder_CUnit(0xB7, pSecond);
        pSecond->SetOrder_CUnit(0xB7, pFirst);
        banker_spend_top_demand(nPlayer);
        return 0;
    }
    else if (nType == 0x67)
    {
        pFirst = tai_captain_find_spare_hydralisk(nPlayer);
        if (pFirst == 0)
        {
            banker_drop_top_demand(nPlayer);
            return 0;
        }
    }
    if (unit_can_make_type(pFirst, (WORD)nType) && TTAllowed((WORD)nType, nPlayer, pFirst) == 1)
    {
        if (!ai_stage_unit_cost(nPlayer, nType))
        {
            if (aiscript_has_campaign(nPlayer))
                return 0;
            get_allowed_err();
            banker_drop_top_demand(nPlayer);
            if (nFlag == 0)
                bldg_tick(pUnit);
            return 0;
        }
        if (banker_at_define_max(nPlayer, nType) != 0)
        {
            banker_drop_top_demand(nPlayer);
            return 0;
        }
        if (pUnit == pFirst)
        {
            if (!pFirst->AIAddBuildQueue(nType, sai_top_demand_type(nPlayer), sai_top_demand_captain(nPlayer)))
                return 0;
        }
        else
        {
            if (!pFirst->AddBuildQueue(nType))
                return 0;
            SAI_RemoveUnit(pFirst, 0);
            if (sai_top_demand_type(nPlayer) == 2)
                guard_crew_post((TGuard *)sai_top_demand_captain(nPlayer), pFirst);
            else
                tai_soldier_add_to_captain((TAI_Captain *)sai_top_demand_captain(nPlayer), pFirst, 1);
        }
        banker_spend_top_demand(nPlayer);
        WORD wType = pFirst->wType;
        if (wType == 0x23 || wType == 0x2B || wType == 0x26)
            pFirst->SetOrder(0x2A);
        else
            pFirst->SetBackgroundOrder(0x26);
        return 1;
    }
    nErr = get_allowed_err();
    if (nErr == 2 || nErr == 0x17 || nErr == 0x15 || nErr == 8 || nErr == 7 || nErr == 0x0D)
    {
        banker_drop_top_demand(nPlayer);
        if (nFlag == 0)
            bldg_tick(pUnit);
    }
    return 0;
}

// 0x488030  bldg_arm_nuke
// bldg_arm_nuke (0x488030, 194 B) -- the nuclear-missile (0x54) build demand.
// The second argument is a `CUnit *`; the third is never read, and only its
// 4-byte footprint is known, from the `ret 4`.
BOOL bldg_arm_nuke(int nArg0, CUnit *pUnit, intptr_t nArg2)
{
    if (sai_top_demand_arg(nArg0) != 0x54)
        return FALSE;

    if (unit_can_make_type(pUnit, 0x54) && TTAllowed(0x54, nArg0, pUnit) == 1)
    {
        if (!ai_stage_unit_cost(nArg0, 0x54))
            return FALSE;
        if (banker_at_define_max(nArg0, 0x54) != 0)
        {
            banker_drop_top_demand(nArg0);
            return FALSE;
        }
        if (!pUnit->AIAddBuildQueue(0x54, 5, 0))
            return FALSE;
        pUnit->SetBackgroundOrder(0x26);
        banker_spend_top_demand(nArg0);
        return TRUE;
    }

    int nErr = get_allowed_err();
    if (nErr == 2 || nErr == 0x17 || nErr == 0x15 || nErr == 8 || nErr == 7 || nErr == 0x0D)
    {
        banker_drop_top_demand(nArg0);
        bldg_tick(pUnit);
    }
    return FALSE;
}

// 0x488100  bldg_build_addon
// bldg_build_addon (0x488100, 283 B) -- the addon/attached-building demand. The
// third parameter is the TBldg the caller found at CUnit +0x134, and its
// pTown is what sai_top_demand_captain's value is compared against, so that
// banker field holds a TTown * here.
BOOL bldg_build_addon(int nArg0, CUnit *pUnit, TBldg *pBldg)
{
    int nId = sai_top_demand_arg(nArg0);
    if (!ai_stage_unit_cost(nArg0, nId))
        return FALSE;

    if (nId == 0x2A)
    {
        if (pUnit->wType != 0x23)
            return FALSE;
        if (!pUnit->AIAddBuildQueue(nId, 3, (intptr_t)pBldg->pTown))
            return FALSE;
        pUnit->SetOrder(nId);
        banker_spend_top_demand(nArg0);
        return FALSE;
    }

    if (nId != 0x84 && nId != 0x85 && nId != 0x92 && nId != 0x90 && nId != 0x89)
        return FALSE;
    if (sai_top_demand_captain(nArg0) != (intptr_t)pBldg->pTown)
        return FALSE;

    if (unit_can_make_type(pUnit, nId) && TTAllowed(nId, nArg0, pUnit) == 1)
    {
        if (!pUnit->AddBuildQueue(nId))
            return FALSE;
        pUnit->SetOrder(0x2B);
        banker_spend_top_demand(nArg0);
        return TRUE;
    }

    int nErr = get_allowed_err();
    if (nErr == 2 || nErr == 0x17 || nErr == 0x15 || nErr == 8 || nErr == 7 || nErr == 0x0D)
    {
        banker_drop_top_demand(nArg0);
        bldg_tick(pUnit);
    }
    return FALSE;
}

// The silent-overload probe, compiled only under OC_W89_PROBE. A declaration
// that disagrees with its definition is not an error but an overload: a
// per-function byte comparison matches the definition's own COMDAT and says
// nothing, and only the payload link ever objects.

// `(void *)&name` supplies no target signature, so on an overload set MSVC
// 2013 cannot deduce which one is meant and raises C2440. A typed function
// pointer is useless here, being ordinary overload resolution, and compiles
// clean in exactly the failure case. Members need no probe: a member
// definition matching no declaration is C2511 at the definition itself.
//
// Run it by temporarily prepending `#define OC_W89_PROBE` at the top of this
// file and compiling: a clean compile is the proof. It covers every free
// function this TU defines and every callee declared for it.
#ifdef OC_W89_PROBE
static void *const w89_probe[] = {
    // -- defined here ------------------------------------------------------
    (void *)&bldg_set_slot,      (void *)&town_clear_repair_worker,      (void *)&town_add_unit_to_first_town,
    (void *)&bldg_clear_slot,      (void *)&bldg_move_slot,      (void *)&town_needs_more_power,
    (void *)&AIUpdateSilo,    (void *)&town_delete_if_empty,      (void *)&unit_belongs_to_player,
    (void *)&NewTown,         (void *)&town_queue_refinery,
    (void *)&sai_process_top_demand_tech,
    (void *)&sai_process_top_demand_upgrade,
    (void *)&worker_build_failed,      (void *)&town_gas_slot,      (void *)&town_gas_class,
    (void *)&get_player_depot_type,                     (void *)&worker_harvest_order,
    (void *)&add_request_to_town_queue,                  (void *)&town_forget_unit,
    (void *)&unit_can_make_type,      (void *)&player_mineral_fields,      (void *)&bldg_id,
    (void *)&bldg_ptr,        (void *)&region_has_worker_or_building,
    (void *)&town_move_record,      (void *)&town_is_researching,      (void *)&aiscript_is_groundmap,
    (void *)&sai_build_init,  (void *)&SAI_Build_RemoveUnit,
    (void *)&town_rejoin_worker,      (void *)&get_town_race,   (void *)&town_from_point,
    (void *)&town_first_town_xy,      (void *)&town_count,      (void *)&town_id,
    (void *)&town_repair_worker,      (void *)&town_need,       (void *)&town_pending_costs,
    (void *)&town_ptr,        (void *)&aiscript_get_region_size,
    (void *)&aiscript_tech,   (void *)&aiscript_upgrade,
    (void *)&bldg_cannot_build,      (void *)&town_rejoin_building,      (void *)&worker_id,
    (void *)&worker_ptr,      (void *)&town_place_point,      (void *)&town_get_base_id,
    (void *)&town_hold_workers,      (void *)&bldg_train_top_demand,      (void *)&bldg_arm_nuke,
    // -- the callees declared for this TU ---------------------------------
    (void *)&town_add_unit,      (void *)&bldg_tick,      (void *)&town_merge_into_nearest,
    (void *)&sai_town_mineral_fields,      (void *)&sai_townbuilder_clear,      (void *)&sai_scripts_remove_town,
    (void *)&ai_nearest_enemy_building,      (void *)&sai_alert_frontier,      (void *)&tai_captain_defend_paths_to_enemies,
    (void *)&guard_free_unmanned,      (void *)&set_allowed_err,
    (void *)&get_staged_ore_cost,                       (void *)&get_staged_gas_cost,
    (void *)&guard_ptr,       (void *)&guard_id,        (void *)&patrol_pick_area,
    (void *)&tai_unit_is_forming_or_incomplete,
};
#endif
