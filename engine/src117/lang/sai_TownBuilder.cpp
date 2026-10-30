// sai_TownBuilder.cpp -- the AI town builder: the layer that decides where a
// computer player's next structure goes. It owns `town_plan`, the persistent
// per-town placement record, and drives the `TReserve` scratch grid the
// placement searches run over. Functions are in oracle VA order.

// Which bodies live in this TU matters in both directions, and both have to be
// re-checked after a change here:
//   * A callee defined here that a landed body calls with a real `call` can be
//     spliced into it by /Ob2. The oracle has WPO off, so a cross-TU call
//     cannot be inlined, and leaving such a callee as an oracle thunk
//     reproduces the oracle for free. If a callee has to live here anyway,
//     `__declspec(noinline)` on the declaration is the lever.
//   * A body whose size is exactly caller plus callee to the byte needs the
//     callee in this TU, and removing it breaks the caller. An unwanted inline
//     is easy to spot: it costs the callee's whole size.

// A held body stays in the TU behind `#ifndef OC_PAYLOAD`, so the plain compile
// keeps checking it while the payload calls the oracle's copy instead.

// Three things in this TU look like mistakes and are not.
//
// 1. `town_plan_at()` is written out rather than calling
//    sai_town_mineral_fields (0x496F20, 29 B, this TU), which the oracle
//    inlines into both sai_town_mineral_target and sai_town_worker_target with
//    no `call` to it in either. Turning it back into a call costs that callee's
//    whole 29 B.

// 2. sai_town_worker_target looks the town up twice on purpose. One lookup
//    compiles to 92 B: MSVC forms the pointer straight into ESI and reuses the
//    `add`'s ZF, emitting no `test`. Two lookups give 94 B, the oracle's size,
//    because MSVC folds them to one pointer but keeps the scaled index in EAX
//    from the first, forcing `lea esi,[eax+town_plan]`, which sets no flags and
//    so costs the explicit `test esi,esi`. Five spellings of "look it up twice"
//    all land on 94 and the single-lookup form is the only one that does not,
//    so deleting the redundant-looking second lookup changes the code silently.

// 3. 0x6A5D68 is reached two ways: `dword_6A5D68` for the entry reads, and
//    `*(DWORD *)&town_plan[NUM_TOWN_PLANS]` for the back edges of
//    sai_recount_town_resources and sai_townbuilder_init's unrolled iterations.
//    The oracle's alias analysis re-reads the count inside those loops because
//    it believes a store into town_plan[i] may hit it, where two separate
//    objects let MSVC prove independence and hoist. A change to the model has
//    to move both spellings.
//    Hyperion: the back edges read the count by name too. Since positions
//    are 4 bytes on x64 (stage E) town_plan is a widened global, and the element
//    past its end is no longer the count.

// The globals this TU reaches, dual-spelled with one guard per name. The guard
// is per name and never one over the whole block, because the generator emits
// only registered rows' globals, so the set grows and a single guard would
// undeclare every other name under it.

// Before coining a name, check re/globals117.json by key -- it is keyed by VA
// and the rows carry no `va` field, so a lookup on row['va'] reports every
// global unregistered -- and check sc-refs/sheets for an existing name.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include <string.h>                  // memset / memcpy
#include <vector>
#include "../h/sai_TownBuilder.h"
#include "../h/sai_PathFindArea.h"   // path_search_t: sub_491300's one seat    // TTownPlan, TReserve, this TU's free functions

// Hyperion: TAiPathReq is path_search_t under other names, on both widths
//.
static_assert(sizeof(TAiPathReq) == sizeof(path_search_t), "TAiPathReq is path_search_t");
static_assert(offsetof(TAiPathReq, nFromX) == offsetof(path_search_t, ptStart), "TAiPathReq.nFromX");
static_assert(offsetof(TAiPathReq, nToX) == offsetof(path_search_t, ptGoal), "TAiPathReq.nToX");
static_assert(offsetof(TAiPathReq, ptToCur) == offsetof(path_search_t, ptCur), "TAiPathReq.ptToCur");
static_assert(offsetof(TAiPathReq, dwUnknown01C) == offsetof(path_search_t, b01C), "TAiPathReq.dwUnknown01C");
static_assert(offsetof(TAiPathReq, aRegions) == offsetof(path_search_t, nRegions), "TAiPathReq.aRegions");
static_assert(offsetof(TAiPathReq, nRegionCount) == offsetof(path_search_t, nRegionCount), "TAiPathReq.nRegionCount");
static_assert(offsetof(TAiPathReq, wFromRegion) == offsetof(path_search_t, nStartRegion), "TAiPathReq.wFromRegion");
static_assert(offsetof(TAiPathReq, wToRegion) == offsetof(path_search_t, nGoalRegion), "TAiPathReq.wToRegion");
#include "../h/sai_PathCreate.h"      // ep_glob_t: sai_route_is_open reads the region groups through it
#include "../h/sai.h"                // sai_town_mineral_fields / sai_townbuilder_clear / sai_alert_frontier
#include "../h/SAI_Build.h"          // TTown / TBldg / TWorker + the three pool objects
#include "../h/SAI_Banker.h"         // banker_build_site_failed and the banker queries
#include "../h/saveload.h"           // compress / decompress -- the town-data serialisers
#include "../port/data/port_formats.h"  // Hyperion: PortSave117Get, PortSave117Put
#include "../gen/save117.h"
#include "../h/CUnit.h"
#include "../h/CUnitSearches.h"      // aiscript_has_campaign
#include "../h/CUnitCombat.h"        // UnitCanReachGroundTarget_xy, GetVectorComponents
#include "../h/CUnitProtoss.h"       // IsCellPowered
#include "../h/CUnitFind.h"
#include "../h/CUnitMisc.h"
#include "../h/CSprite.h"
#include "../h/CFlingy.h"            // AngleBetween
#include "../h/CTarget.h"            // @veclen@16
#include "../h/TAI_Captain.h"        // tai_captain_regions_connected_xy
#include "../h/TrigEnum.h"           // get_map_height
#include "../h/collide.h"            // mtx_has_flags
#include "../h/order.h"
#include "../h/point16.h"
#include "../h/net_cmds.h"           // PlayerData -- MUST precede gen/globals117.h
#include "../gen/globals117.h"

// TBuildTemplate is not declared in src117/h: it stays TU-local until a second
// consumer appears. Stride 16 and the -1 terminator at +0x00 come from
// sai_reserve_template_spacing, and the four members are full-dword loads. The
// signedness is not derived; the loads and the `shl 6` give the width only.
struct TBuildTemplate {
    /*0x00*/ int nX;      // `mov edx, dword ptr [eax]`, and the -1 terminator compare
    /*0x04*/ int nY;      // `mov eax, dword ptr [ecx+509044h]` then `shl eax, 6h`
    /*0x08*/ int nDX;     // `mov ebx, dword ptr [ecx+509048h]`
    /*0x0C*/ int nDY;     // `mov ebx, dword ptr [ecx+50904Ch]` then `shl ebx, 6h`
};

static_assert(sizeof(TBuildTemplate) == 0x10, "TBuildTemplate");
static_assert(offsetof(TBuildTemplate, nX)  == 0x00, "TBuildTemplate.nX");
static_assert(offsetof(TBuildTemplate, nY)  == 0x04, "TBuildTemplate.nY");
static_assert(offsetof(TBuildTemplate, nDX) == 0x08, "TBuildTemplate.nDX");
static_assert(offsetof(TBuildTemplate, nDY) == 0x0C, "TBuildTemplate.nDY");

// -- town_plan @0x6A2E88, 250 records of 48 B, 1-BASED ----------------------
#ifdef OC_DLL_MODE
#define town_plan       (reinterpret_cast<TTownPlan *>(OC_VA(0x6A2E88u)))
#else
extern TTownPlan town_plan[NUM_TOWN_PLANS] OC_UNALIGNED;
#endif

// -- the high-water town index, immediately after the array ----------------
// Reached two ways on purpose; see note 3 at the top of the file. Collapsing
// the two spellings lets MSVC hoist the count out of
// sai_recount_town_resources's and sai_townbuilder_init's loops.
#ifdef OC_DLL_MODE
#define dword_6A5D68    (*reinterpret_cast<DWORD *>(OC_VA(0x6A5D68u)))
#else
extern DWORD dword_6A5D68;
#endif

// -- the recount cooldown; sai_recount_town_resources reloads it with 0x1C2 ----------------
#ifdef OC_DLL_MODE
#define dword_6A5D6C    (*reinterpret_cast<DWORD *>(OC_VA(0x6A5D6Cu)))
#else
extern DWORD dword_6A5D6C;
#endif

#ifdef OC_DLL_MODE
#define dword_6A2E84    (*reinterpret_cast<DWORD *>(OC_VA(0x6A2E84u)))
#else
extern DWORD dword_6A2E84;
#endif

// The four stride-0x24 blocks. The `9` is the naming sheet's adjacency and is
// not derived, so the registry records these with no span rather than resolving
// an interior address against a count no instruction gives. The local spelling
// keeps the stride the bodies walk.
#ifdef OC_DLL_MODE
#define dword_6A5DA4    (reinterpret_cast<DWORD *>(OC_VA(0x6A5DA4u)))
#define dword_6A5DC8    (reinterpret_cast<DWORD *>(OC_VA(0x6A5DC8u)))
#define dword_6A5DEC    (reinterpret_cast<DWORD *>(OC_VA(0x6A5DECu)))
#define dword_6A5E10    (reinterpret_cast<DWORD *>(OC_VA(0x6A5E10u)))
#else
extern DWORD dword_6A5DA4[9] OC_UNALIGNED;
extern DWORD dword_6A5DC8[9] OC_UNALIGNED;
extern DWORD dword_6A5DEC[9] OC_UNALIGNED;
extern DWORD dword_6A5E10[9];
#endif

#ifdef OC_DLL_MODE
#define word_6A5D70     (reinterpret_cast<WORD *>(OC_VA(0x6A5D70u)))
#else
extern WORD word_6A5D70[24];
#endif

// -- the build templates: stride 16, -1 terminator at +0x00, 14 records ----
#ifdef OC_DLL_MODE
#define sgBuildTemplates_g  (reinterpret_cast<TBuildTemplate *>(OC_VA(0x509040u)))
#else
extern TBuildTemplate sgBuildTemplates_g[];
#endif

// -- four ASCII coverage bitmaps, N x N for N = 17/13/9/5 ------------------
#ifdef OC_DLL_MODE
#define unit_tilerange_1  (reinterpret_cast<BYTE *>(OC_VA(0x509120u)))
#define unit_tilerange_2  (reinterpret_cast<BYTE *>(OC_VA(0x509248u)))
#define unit_tilerange_3  (reinterpret_cast<BYTE *>(OC_VA(0x5092F8u)))
#define unit_tilerange_4  (reinterpret_cast<BYTE *>(OC_VA(0x50934Cu)))
#else
extern BYTE unit_tilerange_1[0x11 * 0x11];
extern BYTE unit_tilerange_2[0x0D * 0x0D] OC_UNALIGNED;
extern BYTE unit_tilerange_3[0x09 * 0x09] OC_UNALIGNED;
extern BYTE unit_tilerange_4[0x05 * 0x05] OC_UNALIGNED;
#endif

#ifdef OC_DLL_MODE
#define dword_508FC8    (reinterpret_cast<int (*)[10]>(OC_VA(0x508FC8u)))
#else
extern int dword_508FC8[3][10] OC_UNALIGNED;
#endif

#ifdef OC_DLL_MODE
#define gStartLocations (reinterpret_cast<point16_t *>(OC_VA(0x534A00u)))
#else
extern PlayerTable<point16_t, PlayerTagStartLocations, 8> gStartLocations;
#endif

// epGlob @0x69B584 -- the pathing and region block's base. Spelled the same way
// as SAI_Build.cpp's so the two TUs cannot drift: bare, with a dll-mode arm,
// which is the convention the tree already uses.
#ifdef OC_DLL_MODE
#define epGlob          (*reinterpret_cast<void **>(OC_VA(0x69B584u)))
#else
extern void *epGlob;
#endif

// TTownPlan +0x0C is a second DWORD, the same family as +0x08, from
// sai_recount_town_resources's `add dword ptr [esi+0Ch], eax` and its reset
// partner `mov dword ptr [eax-28h], 0h`, where eax has just been advanced by
// 0x30. The header carries it as `/*0x0C*/ DWORD dwUnknown0C;`.

// The range-checked town-plan lookup: an inline that is never emitted on its
// own, but whose shape is stamped on four bodies in this TU.
//
// It has to return a pointer, NULL on the reject path, rather than only guard
// an index. sai_town_mineral_target and sai_town_worker_target both emit a null
// test on the formed pointer (`add esi, 6A2E88h` / `jz`, and
// `lea esi, [eax+6A2E88h]` / `test esi,esi` / `jz`), and that compare is only
// reachable if NULL arrives from another edge, since MSVC folds
// `&town_plan[n] == 0` to false when the pointer is formed unconditionally. The
// two spellings are register-allocation variants, which is itself a sign they
// come from one piece of source.
static TTownPlan *town_plan_at(int nTown)
{
    if (nTown == 0 || (unsigned int)nTown >= NUM_TOWN_PLANS)
        return 0;
    return &town_plan[nTown];
}

// 0x400001  town_plan_capped
// 0x496690  sai_town_worker_target   94 B  -- sai_town_mineral_target's body plus 3 * +0x05
static unsigned int town_plan_capped(TTownPlan *pPlan, int nArg1)
{
    unsigned int nLimit = player_mineral_fields(nArg1);
    unsigned int nValue = pPlan->dwUnknown08 / 500;
    if (nValue < nLimit)
    {
        nValue = pPlan->bUnknown04;
    }
    else
    {
        unsigned int nMax = pPlan->bUnknown04 * 2;
        if (nValue > nMax)
            nValue = nMax;
    }
    return nValue;
}

// 0x494FC0  ai_get_build_location
#ifndef gUnitAddOnOffset
#ifdef OC_DLL_MODE
#define gUnitAddOnOffset (reinterpret_cast<point16_t *>(OC_VA(0x522AB8u)))
#else
extern point16_t gUnitAddOnOffset[96] OC_UNALIGNED;
#endif
#endif
#ifndef gTargetUnpoweredStructure_g
#ifdef OC_DLL_MODE
#define gTargetUnpoweredStructure_g (*reinterpret_cast<CUnit **>(OC_VA(0x6A5DA0u)))
#else
extern CUnit *gTargetUnpoweredStructure_g;
#endif
#endif
#ifndef dword_6A2E80
#ifdef OC_DLL_MODE
#define dword_6A2E80 (*reinterpret_cast<CUnit **>(OC_VA(0x6A2E80u)))
#else
extern CUnit *dword_6A2E80;
#endif
#endif

// Finds a buildable cell for pUnit's structure of type nType near pos; writes its pixel centre.
// ptNear is adjusted in place so x stays in the ptTile register and y is reloaded as a word.
// Both zero inits and the grid read as aGrid[r][i] set the register ranks: pUnit in EBX
// before the switch, bSpacing in EBX after it, and pRow as the cell test's base.
int ai_get_build_location(CUnit *pUnit, int nType, mpoint_t pos, mpoint_t *pOut,
                        unsigned nRadius)
{
    TReserve grid;
    int nPlayer = pUnit->bPlayer;
    point16_t ptTile;
    ptTile.x = pos.x / 32;
    ptTile.y = pos.y / 32;
    point16_t ptNear = {0, 0};

    if (gulUnitTypeFlags[(WORD)nType] & 2)
    {
        ptNear = ptTile;
        ptNear.x -= gUnitPlaceboxSize[pUnit->wType].x / 2 / 32;
        ptNear.y += -gUnitPlaceboxSize[pUnit->wType].y / 2 / 32;
        ptNear.x += gUnitAddOnOffset[(WORD)nType - 106].x / 32;
        ptNear.y += gUnitAddOnOffset[(WORD)nType - 106].y / 32;
        if (!mtx_query_place_bldg(pUnit, (BYTE)nPlayer, *(int *)&ptNear.x, *(int *)&ptNear.y,
                                  nType, 0, 0, 1, 0))
        {
            ptNear.x += -gUnitAddOnOffset[(WORD)nType - 106].x / 32;
            ptNear.y += -gUnitAddOnOffset[(WORD)nType - 106].y / 32;
            pOut->x = ptNear.x * 32 + gUnitPlaceboxSize[pUnit->wType].x / 2;
            pOut->y = ptNear.y * 32 + gUnitPlaceboxSize[pUnit->wType].y / 2;
            return 1;
        }
    }

    int nRegion = AI_AREAS_117;
    BOOL bSpacing = TRUE;
    switch ((WORD)nType)
    {
    case 0x6A:
    case 0x83:
    case 0x9A:
        bSpacing = FALSE;
        break;
    case 0x9C:
        gTargetUnpoweredStructure_g = AIFindUnpoweredBldg(nPlayer, pos);
        if (gTargetUnpoweredStructure_g != NULL)
        {
            pos.x = gTargetUnpoweredStructure_g->pSprite->wPosX;
            pos.y = gTargetUnpoweredStructure_g->pSprite->wPosY;
            ptTile.x = pos.x / 32;
            ptTile.y = pos.y / 32;
            bSpacing = FALSE;
        }
        break;
    case 0x8F:
        if (sai_is_creep(nPlayer))
            break;
    case 0x7D:
        if (GetBunkerPlacement(nPlayer, &nRegion))
            bSpacing = FALSE;
        break;
    case 0x75:
    case 0x76:
        nType = 0x74;
        break;
    case 0x78:
        nType = 0x71;
        break;
    case 0x73:
        nType = 0x72;
        break;
    case 0x6B:
    case 0x6C:
        nType = 0x6A;
        break;
    }

    dword_6A2E80 = pUnit;
    ReservePlacement((WORD)nType, (intptr_t)&grid, pUnit->bPlayer, pos, nRadius);
    dword_6A2E80 = NULL;
    if (bSpacing)
        sai_reserve_template_spacing(&grid, pos);

    point16_t ptCenter = ptTile;
    if (nRegion != AI_AREAS_117)
    {
        point16_t ptRegion;
        GetRegionPosition(nRegion, &ptRegion);
        ptCenter.x = ptRegion.x / 32;
        ptCenter.y = ptRegion.y / 32;
    }

    // Nearest cell within nRadius, else the nearest beyond it; a second pass around pos
    // runs only when a bunker region moved the centre.
    unsigned nBestNear = 99999999;
    unsigned nBestFar = 99999999;
    point16_t ptFar = {0, 0};
    for (unsigned nPass = 0; nPass < 2; ++nPass)
    {
        int nRow = ptTile.y - 32;
        for (int r = 0; r < RESERVE_DIM; ++r)
        {
            int nCol = ptTile.x - 32;
            for (unsigned i = 0; i < RESERVE_DIM; ++i, ++nCol)
            {
                if (grid.aGrid[r][i] == 0)
                    continue;
                unsigned nDist = sai_get_ground_distance(ptCenter.x * 32, ptCenter.y * 32,
                                                       nCol * 32, nRow * 32);
                if (nDist >= nBestNear)
                    continue;
                if (nDist > nRadius)
                {
                    if (nDist < nBestFar)
                    {
                        nBestFar = nDist;
                        ptFar.x = nCol;
                        ptFar.y = nRow;
                    }
                }
                else
                {
                    nBestNear = nDist;
                    ptNear.x = nCol;
                    ptNear.y = nRow;
                }
            }
            ++nRow;
        }

        if (nBestNear != 99999999)
            goto found;
        nBestNear = nBestFar;
        ptNear = ptFar;
        if (nBestNear != 99999999)
            goto found;
        if (nRegion == AI_AREAS_117)
            return 0;
        ptCenter.x = pos.x / 32;
        ptCenter.y = pos.y / 32;
    }
    return 0;

found:
    pOut->x = ptNear.x * 32 + gUnitPlaceboxSize[(WORD)nType].x / 2;
    pOut->y = ptNear.y * 32 + gUnitPlaceboxSize[(WORD)nType].y / 2;
    return 1;
}

// 0x495440  sai_read_town_data
// TTownPlan +0x08 is a DWORD, from the same `mul dword ptr [esi+8h]` in
// sai_town_mineral_target and sai_town_worker_target, in both cases against
// 10624DD3h with `shr edx,5h`, MSVC's reciprocal for an unsigned divide by
// 1000, so the field is unsigned as well as 4 bytes wide. It and +0x0C are in
// src117/h/sai_TownBuilder.h as `/*0x08*/ DWORD dwUnknown08;` and
// `dwUnknown0C`, and every site below names them.

// 0x495440  sai_read_town_data   17 B
// `edx = 2EE8h` is a literal, not sizeof(town_plan): the array is 250*48 ==
// 0x2EE0 and the serialised run is 8 bytes longer, covering dword_6A5D68 (the
// high-water town index) and the dword after it. Blizzard wrote the byte count
// out by hand and we reproduce it.
int sai_read_town_data(intptr_t nArg0)
{
#if defined(_WIN64) || defined(__LP64__)
    // Hyperion: since positions are 4 bytes (stage E) town_plan is a widened global
    // and the count no longer follows it, so the run is read as 1.17's bytes and each
    // record and the two dwords after them are taken from it.
    BYTE ab[0x2EE8];
    const int nOk = decompress(ab, sizeof ab, (void *)nArg0);
    if (nOk) {
        const BYTE *p = ab;
        for (int i = 0; i < NUM_TOWN_PLANS; i++)
            p = PortSave117Get(SAVE117_TownPlan, p, &town_plan[i]);
        memcpy(&dword_6A5D68, p, 4);
        memcpy(&dword_6A5D6C, p + 4, 4);
    }
    return nOk;
#else
    return decompress(town_plan, 0x2EE8, (void *)nArg0);
#endif
}

// 0x495460  sai_write_town_data
// 0x495460  sai_write_town_data   17 B
int sai_write_town_data(intptr_t nArg0)
{
#if defined(_WIN64) || defined(__LP64__)
    // Hyperion: as 1.17's bytes, the records and the two dwords after them (see
    // sai_read_town_data)
    BYTE ab[0x2EE8];
    BYTE *p = ab;
    for (int i = 0; i < NUM_TOWN_PLANS; i++)
        p = PortSave117Put(SAVE117_TownPlan, &town_plan[i], p);
    memcpy(p, &dword_6A5D68, 4);
    memcpy(p + 4, &dword_6A5D6C, 4);
    return compress(ab, sizeof ab, (void *)nArg0);
#else
    return compress(town_plan, 0x2EE8, (void *)nArg0);
#endif
}

// 0x495480  GetBunkerPlacement
// 0x495480  GetBunkerPlacement   297 B
// The frame gives word_6A5D70's capacity as 24, without the naming sheet:
// `sub esp, 0C4h` buys exactly two parallel arrays indexed by the same
// `dword_6A2E84`-bounded counter, `[ebp-0C4h]` walking in steps of 4 (a
// point16_t per region) and `[ebp-64h]` in steps of 4 (an int per region).
// 0xC4 - 0x64 == 0x60 == 24 * 4 and 0x64 - 4 == 0x60 == 24 * 4, so both were
// sized for 24 entries, which is also the sheet's storage bound
// (0x6A5DA0 - 0x6A5D70 == 0x30 == 24 WORDs).
//
// It picks the region whose minimum ground distance to any of the player's
// bunker-class units is largest -- `cmp edx, esi` / `jbe` keeps the running
// maximum, and the per-region seed is 99999999 (0x5F5E0FF).
int GetBunkerPlacement(int nPlayer, int *pnRegion)
{
    point16_t aPos[24];
    int aFar[24];
    CUnit *pUnit;
    unsigned int i;
    unsigned int k;
    unsigned int nBest;
    int nFar;

    if (dword_6A2E84 == 0)
        return 0;

    if (dword_6A2E84 == 1)
    {
        *pnRegion = word_6A5D70[0];
        return 1;
    }

    for (i = 0; i < dword_6A2E84; i++)
    {
        aFar[i] = 99999999;
        GetRegionPosition(word_6A5D70[i], &aPos[i]);
    }

    for (pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != 0;
         pUnit = pUnit->pPlayerUnitNext)
    {
        if (pUnit->IsDeadOrDying())
            continue;

        switch (pUnit->wType)
        {
        case 125:
        case 143:
        case 146:
            break;
        default:
            continue;
        }

        for (i = 0; i < dword_6A2E84; i++)
        {
            CSprite *pSprite = pUnit->pSprite;
            upos_t wY = pSprite->wPosY;
            upos_t wX = pSprite->wPosX;
            const int nDist = sai_get_ground_distance(PosS(wX), PosS(wY),
                                                    aPos[i].x, aPos[i].y);

            if ((unsigned int)nDist < (unsigned int)aFar[i])
                aFar[i] = nDist;
        }
    }

    nFar = aFar[0];
    nBest = 0;
    for (k = 1; k < dword_6A2E84; k++)
    {
        const int nOne = aFar[k];

        if ((unsigned int)nOne > (unsigned int)nFar)
        {
            nFar = nOne;
            nBest = k;
        }
    }

    *pnRegion = word_6A5D70[nBest];
    return 1;
}

// 0x4955B0  sai_town_plan_of_unit
// Returns the 1-based town-plan index of the first town this unit is near, or 0.
// Block 1 reads the sprite fields into locals before the town fields, which is
// what makes the oracle evict pUnit for wX. The town pointer is taken per
// iteration so its first load follows the loop guard.
int sai_town_plan_of_unit(CUnit *pUnit)
{
    TTownPlan *pTown;
    unsigned int nTown;

    for (nTown = 1; nTown <= dword_6A5D68; nTown++)
    {
        pTown = &town_plan[nTown];
        CSprite *pSprite = pUnit->pSprite;
        upos_t wY = pSprite->wPosY;
        upos_t wX = pSprite->wPosX;
        int nTY = pTown->nY;
        int nTX = pTown->nX;

        if ((unsigned int)veclen(nTX, PosS(wX), nTY, PosS(wY))
                <= (unsigned int)(pUnit->IsMineralSrc() ? 0x280 : 0x380)
            && UnitCanReachGroundTarget_xy(pUnit, pTown->nX, pTown->nY))
        {
            pSprite = pUnit->pSprite;
            wY = pSprite->wPosY;
            wX = pSprite->wPosX;
            if (get_map_height(PosS(wX), PosS(wY))
                    == get_map_height(pTown->nX, pTown->nY))
            {
                pSprite = pUnit->pSprite;
                wY = pSprite->wPosY;
                wX = pSprite->wPosX;
                if ((unsigned int)sai_get_ground_distance(pTown->nX, pTown->nY,
                                                        PosS(wX), PosS(wY))
                        <= (unsigned int)(pUnit->IsMineralSrc() ? 0x14 : 0x1C))
                {
                    return nTown;
                }
            }
        }
    }

    return 0;
}


// 0x4956C0  sai_alert_regions_outward
// 0x4956C0  sai_alert_regions_outward   135 B
// A depth-bounded recursive region flood. `pArg3` serves two purposes, which is
// why `void *` is the honest declaration rather than a placeholder:
//   * on entry it is a per-region dword marker array -- `cmp dword ptr
//     [ebx+esi*4], 0h` then `mov dword ptr [ebx+esi*4], 1h`;
//   * its stack slot is then handed to GetRegionPaths as the `WORD **`
//     out-param (`lea edx, dword ptr [ebp+0Ch]`) and walked as the path cursor
//     (`movzx edx, word ptr [ecx]` / `add ecx, 2h`).
// The recursion pushes EBX, the original value cached before the call, so the
// marker array and the advanced cursor have to be separate expressions.

// Three levers here, all measured:
//   * the first marker read goes through `pArg3` and the write through the
//     local. Reading both through one local lets MSVC fold
//     `&pVisited[nRegion]` into a third frame slot, which costs `pVisited` its
//     callee-saved register, nMaxDepth taking EBX instead: 151 B, +16.
//     `pArg3` is address-taken, so a load through it is a separate value and
//     the common subexpression never forms.
//   * `dword_6A2E84` is copied to a local before the marker store. The oracle
//     loads the count above the store (`mov edx,[6A2E84h]` at +0x2A, the store
//     at +0x32), and MSVC will not hoist an extern load past a store through
//     an `int *` on its own.
//   * `nDepth` is incremented once before the loop, not `nDepth + 1` at the
//     call: the oracle's `inc edi` is at +0x61, outside the back edge.

// The shipped form has one 2-byte delta at +0x42 and is otherwise identical,
// with both baked addresses equal to the oracle's. It is the SIB encoding of
// one effective address -- `[ecx*2+6A5D70h]` (04 4D) against
// `[ecx+ecx+6A5D70h]` (84 09), the same address at the same length. Eight
// spellings were measured and all eight behave the same way.
void sai_alert_regions_outward(int nArg0, int nRegion, int nDepth, void *pArg3, int nMaxDepth)
{
    int *pVisited;
    DWORD nNum;
    DWORD i;
    int nCount;

    if (nRegion == 0)
        return;

    if (((int *)pArg3)[nRegion] != 0)
        return;

    if ((unsigned int)nDepth > (unsigned int)nMaxDepth)
        return;

    pVisited = (int *)pArg3;
    tai_captain_claim_defence_region(nArg0, nRegion, 0);
    nNum = dword_6A2E84;
    pVisited[nRegion] = 1;

    for (i = 0; i < nNum; i++)
    {
        if (word_6A5D70[i] == nRegion)
            return;
    }

    nCount = GetRegionPaths((WORD)nRegion, (WORD **)&pArg3);
    if (nCount == 0)
        return;

    nDepth++;
    do
    {
        WORD wNext = *(WORD *)pArg3;

        pArg3 = (WORD *)pArg3 + 1;
        sai_alert_regions_outward(nArg0, wNext, nDepth, pVisited, nMaxDepth);
        nCount--;
    }
    while (nCount != 0);
}

// 0x495750  sai_resource_in_patch   307 B
// Whether a resource unit lies inside rc and close enough to (nX, nY).
// The rect is by value; Y bounds go through the whole dword to keep the `shr`.
// The deepest reject (after `push ebx`) must be an inline `return 0`; the two
// shallower groups are gotos to labels at the end. Other reject shapes measured
// 283, 292 and 311 B.
int sai_resource_in_patch(CUnit *pUnit, int nX, int nY, mrect_t rc)
{
    CSprite *pSprite;
    upos_t wY;
    upos_t wX;
    int nDist;

    if ((gulUnitTypeFlags[pUnit->wType] & 0x2000) == 0)
        goto reject_a;
    if (pUnit->_unmapped_D8 != 0)
        goto reject_a;

    pSprite = pUnit->pSprite;
    if (PosS(pSprite->wPosX) < rc.left)
        goto reject_b;
    if (PosS(pSprite->wPosX) > rc.right)
        goto reject_b;
    if (PosS(pSprite->wPosY) < rc.top)  // Hyperion: 1.17 reads top and bottom by dword
        goto reject_b;
    if (PosS(pSprite->wPosY) > rc.bottom)
        goto reject_b;

    if (!UnitCanReachGroundTarget_xy(pUnit, nX, nY))
        return 0;

    pSprite = pUnit->pSprite;
    wY = pSprite->wPosY;
    wX = pSprite->wPosX;
    if (get_map_height(PosS(wX), PosS(wY)) != get_map_height(nX, nY))
        return 0;

    pSprite = pUnit->pSprite;
    wY = pSprite->wPosY;
    wX = pSprite->wPosX;
    nDist = veclen(nX, PosS(wX), nY, PosS(wY));
    if ((unsigned int)nDist
            > (unsigned int)(pUnit->IsMineralSrc() ? 0x280 : 0x380))
    {
        return 0;
    }

    pSprite = pUnit->pSprite;
    wY = pSprite->wPosY;
    wX = pSprite->wPosX;
    nDist = sai_get_ground_distance(nX, nY, PosS(wX), PosS(wY));
    return (unsigned int)nDist
        <= (unsigned int)(pUnit->IsMineralSrc() ? 0x14 : 0x1C);

reject_b:
    return 0;
reject_a:
    return 0;
}

// 0x495890  sai_seed_town_plans   787 B
// One resource seed per unit of kind nKind, scored by sai_score_resource_patch; then, best
// seed first, open a town plan at the centre of the patch it covers and claim
// that patch's resources for it.
struct TResourceSeed {
    /*0x00*/ mrect_t  rc;       // pushed whole as sai_score_resource_patch's stack argument; Hyperion: map positions
    /*0x08*/ DWORD    dwScore;  // `mov dword ptr [esi+8h], eax` after sai_score_resource_patch
    /*0x0C*/ CUnit   *pUnit;    // `mov dword ptr [esi+0Ch], edi`
};
OC_X86_LAYOUT(sizeof(TResourceSeed) == 0x10, "TResourceSeed");
static_assert(offsetof(TResourceSeed, rc) == 0x00, "TResourceSeed.rc");
OC_X86_LAYOUT(offsetof(TResourceSeed, dwScore) == 0x08, "TResourceSeed.dwScore");
OC_X86_LAYOUT(offsetof(TResourceSeed, pUnit) == 0x0C, "TResourceSeed.pUnit");

// The second pass takes the list head into its own local and the seed through a pointer
// before the walk. Those two values are what place the index reload after the sprite loads.
void sai_seed_town_plans(int nKind, BYTE *pKinds)
{
    static TResourceSeed aSeeds[POOL_UNITS_STORE];  // Hyperion: on the stack in 1.17
    unsigned int nSeeds = 0;

    CUnit *pUnit = CUnit::GetUsedList();
    if (pUnit != 0)
    {
        BYTE *pKind = pKinds;
        TResourceSeed *pNext = aSeeds;
        do
        {
            if (*pKind == nKind)
            {
                TResourceSeed *pSeed = pNext;
                ++nSeeds;
                ++pNext;
                CSprite *pSprite = pUnit->pSprite;
                pSeed->pUnit = pUnit;
                pSeed->rc.top = pSprite->wPosY - 0x3C0;
                pSeed->rc.bottom = pSprite->wPosY + 0x3C0;
                pSeed->rc.left = pSprite->wPosX - 0x3C0;
                pSeed->rc.right = pSprite->wPosX + 0x3C0;
                pSeed->dwScore = sai_score_resource_patch(pUnit, pSeed->rc);
            }
            pUnit = pUnit->pNext;
            ++pKind;
        }
        while (pUnit != 0);
    }

    for (unsigned int i = 0; i < nSeeds; ++i)
    {
        unsigned int nBest = 0;
        DWORD dwBest = aSeeds[0].dwScore;
        for (unsigned int j = 1; j < nSeeds; ++j)
        {
            if (aSeeds[j].dwScore > dwBest)
            {
                dwBest = aSeeds[j].dwScore;
                nBest = j;
            }
        }

        aSeeds[nBest].dwScore = 0;

        int nSumX = 0;
        int nSumY = 0;
        unsigned int nFound = 0;
        for (CUnit *pCand = CUnit::GetUsedList(); pCand != 0; pCand = pCand->pNext)
        {
            CSprite *pS = aSeeds[nBest].pUnit->pSprite;
            upos_t wSY = pS->wPosY;
            upos_t wSX = pS->wPosX;
            if (sai_resource_in_patch(pCand, PosS(wSX), PosS(wSY), aSeeds[nBest].rc))
            {
                nSumX += PosS(pCand->pSprite->wPosX);
                nSumY += PosS(pCand->pSprite->wPosY);
                ++nFound;
            }
        }
        if (nFound == 0)
            continue;

        if (dword_6A5D68 >= NUM_TOWN_PLANS - 1)
            return;
        ++dword_6A5D68;
        TTownPlan *pPlan = &town_plan[dword_6A5D68];

        CUnit *pCentre = (CUnit *)sai_nearest_marked_resource((intptr_t)aSeeds[nBest].pUnit, nSumX / nFound,
                                             nSumY / nFound, pKinds);
        aSeeds[nBest].pUnit = pCentre;
        pPlan->nX = pCentre->pSprite->wPosX;
        upos_t wCY = pCentre->pSprite->wPosY;
        pPlan->nY = wCY;
        upos_t wPX = pPlan->nX;
        int nMinY = PosS(wCY);
        int nMaxY = nMinY;
        int nMinX = PosS(wPX);
        int nMaxX = nMinX;
        aSeeds[nBest].rc.top = wCY - 0x3C0;
        aSeeds[nBest].rc.bottom = wCY + 0x3C0;
        aSeeds[nBest].rc.left = pPlan->nX - 0x3C0;
        aSeeds[nBest].rc.right = pPlan->nX + 0x3C0;

        CUnit *pFirst = CUnit::GetUsedList();
        if (pFirst != 0)
        {
            TResourceSeed *pSeed = &aSeeds[nBest];
            CUnit *pCand = pFirst;
            BYTE *pKind = pKinds;
            do
            {
                CSprite *pS = pCentre->pSprite;
                upos_t wSY = pS->wPosY;
                upos_t wSX = pS->wPosX;
                if (!sai_resource_in_patch(pCand, PosS(wSX), PosS(wSY), pSeed->rc))
                    continue;

                if (*pKind == 1)
                {
                    *pKind = 3;
                    ++pPlan->bUnknown04;
                    pPlan->dwUnknown08 += pCand->wResourceAmount;
                }
                else if (*pKind == 2)
                {
                    *pKind = 4;
                    ++pPlan->bUnknown05;
                    pPlan->dwUnknown0C += pCand->wResourceAmount;
                }
                else
                    continue;

                CSprite *pSprite = pCand->pSprite;
                pCand->_unmapped_D8 = (BYTE)dword_6A5D68;
                if (PosS(pSprite->wPosX) < nMinX)
                    nMinX = PosS(pSprite->wPosX);
                if (PosS(pSprite->wPosX) > nMaxX)
                    nMaxX = PosS(pSprite->wPosX);
                if (PosS(pSprite->wPosY) < nMinY)
                    nMinY = PosS(pSprite->wPosY);
                if (PosS(pSprite->wPosY) > nMaxY)
                    nMaxY = PosS(pSprite->wPosY);
            }
            while (pCand = pCand->pNext, ++pKind, pCand != 0);
        }

        pPlan->nX = (nMinX + nMaxX) / 2;
        pPlan->nY = (nMinY + nMaxY) / 2;
    }
}

// 0x495BB0  ReservePlacement   325 B
// Zeros the caller's 64x64 grid, marks the buildable cells, then runs the per-type filters.
// nArg4 is scratch and address-taken (GetBunkerPlacement gets `[ebp+10h]`); arm order sets block order.
// pos must be a point16_t: as an int MSVC caches it in EBX, as a struct it is re-pushed
// from the slot at every call like the oracle.
void ReservePlacement(int nArg0, intptr_t nArg1, intptr_t nArg2, mpoint_t pos, int nArg4)
{
    TReserve *pReserve = (TReserve *)nArg1;

    memset(pReserve, 0, sizeof(TReserve));

    AssignBuildableLocations(nArg0, pReserve, (BYTE)nArg2, pos, nArg4);
    pReserve->RemoveDisconnected(pos);

    if (nArg0 != 0x9C)
        sai_reserve_safe_regions(nArg0, nArg1, nArg2, pos);

    sai_reserve_walk_range(pReserve, nArg4, pos);

    for (unsigned int nRow = 0; nRow < RESERVE_DIM; ++nRow)
    {
        for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol)
        {
            if (pReserve->aGrid[nRow][nCol] != 0)
                goto found;
        }
    }
    return;

found:
    pReserve->RemoveWrongHeight(pos);
    sai_reserve_buildable_tiles(nArg0, nArg1, pos);

    if (nArg0 == 0x6A || nArg0 == 0x83 || nArg0 == 0x9A)
    {
        nArg4 = 1;
    }
    else
    {
        nArg4 = 0;
        sai_reserve_buildings(nArg0, nArg1, nArg2, pos);
    }

    sai_reserve_addon_space(nArg0, nArg1, (void *)nArg2, pos);

    switch (nArg0)
    {
    case 0x9C:
        sai_reserve_pylon_placement(nArg1, (int)nArg2, pos);
        break;

    case 0x8F:
        if (sai_is_creep((int)nArg2) || !GetBunkerPlacement((int)nArg2, &nArg4))
            sai_reserve_turret_placement(nArg1, nArg2, pos, nArg0, 0);
        break;

    case 0x7D:
        if (!GetBunkerPlacement((int)nArg2, &nArg4))
            sai_reserve_turret_placement(nArg1, nArg2, pos, nArg0, 0);
        break;

    case 0x7C:
    case 0xA2:
        sai_reserve_turret_placement(nArg1, nArg2, pos, nArg0, 0);
        break;

    default:
        if (nArg4 == 0)
            ReserveDefault(nArg0, nArg1, nArg2, pos);
        break;
    }
}


// 0x495D40  AssignBuildableLocations
// Marks every cell of the 64x64 grid where type nArg0 can be placed, inside the centred
// nArg4 x nArg4 window. The oracle's switch tables flag 0x6A, 0x71, 0x72 and 0x74, the
// addon-capable types, which also need the addon probe (type 0x7C) at (x+4, y+1).
// The x start's merged pair reads nMax once more in the inner preheader, which keeps nMax
// in EAX and the row counter in EDX across the outer loop.
void AssignBuildableLocations(int nArg0, TReserve *pReserve, BYTE bArg2, mpoint_t pos, int nArg4)
{
    int bAddon;

    switch (nArg0)
    {
    case 0x6A:
    case 0x71:
    case 0x72:
    case 0x74:
        bAddon = 1;
        break;
    default:
        bAddon = 0;
        break;
    }

    unsigned int nMin = 0x20 - (unsigned)nArg4 / 2;
    unsigned int nMax = nMin + nArg4;
    int nY = pos.y / 32 - 0x20;
    unsigned int nRow;
    for (nRow = 0; nRow < RESERVE_DIM; ++nRow)
    {
        if (nRow >= nMax)
            break;

        if (nRow >= nMin)
        {
            unsigned int nCol = 0;
            int nX = pos.x / 32;
            // Identical arms; the test is the inner loop's own and leaves no code.
            if (nCol >= nMax) { nX -= 0x20; } else { nX -= 0x20; }

            for (nCol = 0; nCol < RESERVE_DIM; ++nCol)
            {
                if (nCol >= nMax)
                    break;

                if (nCol >= nMin)
                {
                    if (!(bAddon != 0
                          && mtx_query_place_bldg(NULL, bArg2, nX + 4, nY + 1,
                                                  0x7C, 0, 0, 1, 0))
                        && !mtx_query_place_bldg(NULL, bArg2, nX, nY,
                                                 nArg0, 0, 0, 1, 0))
                    {
                        pReserve->aGrid[nRow][nCol] = 1;
                    }
                }
                ++nX;
            }
        }
        ++nY;
    }
}


// 0x495E70  sai_nearest_marked_resource
#if !defined(OC_PAYLOAD) || defined(OC_WITH_HELD)
// The flagged unit nearest to (nArg1, nArg2) by ground distance, if it is
// within 20 tiles; nArg0 otherwise. pbFlags runs in step with the unit list.
intptr_t sai_nearest_marked_resource(intptr_t nArg0, int nArg1, int nArg2, BYTE *pbFlags)
{
    unsigned int nBest = 99999999;
    CUnit *pBest = NULL;

    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext, ++pbFlags)
    {
        if (*pbFlags == 1)
        {
            unsigned int nDist = sai_get_ground_distance(nArg1, nArg2,
                                                       PosS(pUnit->pSprite->wPosX),
                                                       PosS(pUnit->pSprite->wPosY));

            if (nDist <= nBest && nDist <= 20)
            {
                nBest = nDist;
                pBest = pUnit;
            }
        }
    }

    if (pBest != NULL)
        return (intptr_t)pBest;
    return nArg0;
}
#endif  // OC_PAYLOAD


// 0x4960B0  ReserveStructure
// Clears the grid cells under a structure's footprint grown by its placebox and nArg7 tiles.
// pos is a point16_t by value: held whole in ECX (`shr` for y, `movsx eax, cx` for x), it
// leaves ESI to nArg7 and spills nPbY as the oracle does.
// The column loop counts up; MSVC turns it into a down-counter reloaded on the exit edge.
// The halves are unsigned (`shr 1`); nArg5 is the grid base (`add esi, [ebp+14h]`).
void ReserveStructure(int nArg0, int nArg1, int nArg2, int nArg3, int nArg4,
                      intptr_t nArg5, mpoint_t pos, int nArg7)
{
    TReserve *pReserve = (TReserve *)nArg5;

    int nPbX = gUnitPlaceboxSize[(WORD)nArg4].x / 32;
    int nPbY = gUnitPlaceboxSize[(WORD)nArg4].y / 32;

    int nX = nArg2 - (unsigned)nArg0 / 2 - nPbX - nArg7 + 1;
    unsigned nCols = nArg0 - 1 + nPbX + nArg7 * 2;
    unsigned nRows = nArg1 - 1 + nPbY + nArg7 * 2;
    int nRow = 0x20 + nArg3 - pos.y / 32
               - (unsigned)nArg1 / 2 - nPbY - nArg7 + 1;

    for (unsigned i = 0; i < nRows; ++i)
    {
        int nCol = nX - pos.x / 32 + 0x20;
        for (unsigned j = 0; j < nCols; ++j)
        {
            if (nCol >= 0 && nCol < 0x40 && nRow >= 0 && nRow < 0x40)
                pReserve->aGrid[nRow][nCol] = 0;
            ++nCol;
        }
        ++nRow;
    }
}


// 0x4961A0  ReserveStructure_CUnit
// 0x4961A0  ReserveStructure_CUnit   100 B
// Forwards a CUnit to ReserveStructure in build cells.

// The first argument is a CUnit*, not the header's placeholder `void *`:
// `mov esi, dword ptr [ecx+0Ch]` reaches CUnit::pSprite and
// `movzx edi, word ptr [ecx+64h]` reaches CUnit::wType, the two offsets and
// widths CUnit.h already carries (/*0x0C*/ CSprite *pSprite, /*0x64*/ WORD
// wType).

// gUnitPlaceboxSize and the sprite position are both in pixels; `cdq / and
// edx,1Fh / add / sar 5h` is MSVC's signed divide by 32, rounding toward zero,
// which is the pixels-to-cells conversion collide.cpp spells `/ 32`.

// `movsx eax, word ptr [esi+14h]` reads CSprite::wPosX signed where the header
// declares it `WORD`. One movsx site falsifies "never read signed" rather than
// the declared type, so the cast is written here instead of changing the
// header.
void ReserveStructure_CUnit(void *pArg0, int nArg1, intptr_t nArg2, mpoint_t pos, int nArg4)
{
    CUnit *pUnit = (CUnit *)pArg0;

    ReserveStructure(gUnitPlaceboxSize[pUnit->wType].x / 32,
                     gUnitPlaceboxSize[pUnit->wType].y / 32,
                     PosS(pUnit->pSprite->wPosX) / 32,
                     pUnit->pSprite->wPosY / 32,
                     nArg1, nArg2, pos, nArg4);
}

// 0x496210  TReserve::HasValidSpace
// 0x496210  TReserve::HasValidSpace   34 B   -- shape: nested 0x40 x 0x40 scan
// The oracle walks the grid with a row pointer (`add ecx, 40h`) and a column
// index (`inc eax` / `cmp eax, 40h`), which is what indexing a
// `BYTE[64][64]` with two loop variables lowers to. Returns TRUE only if every
// one of the 4096 cells is zero.
BOOL TReserve::HasValidSpace()
{
    for (unsigned int nRow = 0; nRow < RESERVE_DIM; ++nRow)
    {
        for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol)
        {
            if (aGrid[nRow][nCol] != 0)
                return FALSE;
        }
    }
    return TRUE;
}

// 0x496240  ai_get_build_location_at_unit
// 0x496240  ai_get_build_location_at_unit   128 B  -- ai_get_build_location_near's twin, with the start point taken
// from the unit's own sprite instead of a caller argument.
int ai_get_build_location_at_unit(intptr_t nArg0, int nArg1, mpoint_t *pPos, int nArg3)
{
    CUnit *pUnit = (CUnit *)nArg0;
    int nRadius = 3;
    mpoint_t  ptStart;

    ptStart.x = pUnit->pSprite->wPosX;
    ptStart.y = pUnit->pSprite->wPosY;

    if (nArg3 != 0 && banker_build_site_failed(pUnit->bPlayer))
        return 0;

    if ((WORD)nArg1 == 0x7C)
        nRadius = 0x28;

    int nResult = ai_get_build_location(pUnit, nArg1, ptStart,
                                      pPos, nRadius);
    if (nResult == 0 && nArg3 != 0)
        banker_set_build_site_failed(pUnit->bPlayer);
    return nResult;
}

// 0x4962C0  ai_get_build_location_near
// 0x4962C0  ai_get_build_location_near   95 B
// The signature is src117/lang/gametype.cpp's, copied as it stands: that row is
// declared TU-local there, so a different spelling here would be a silent
// overload and only the payload link would object.
//
// ai_get_build_location is reached through sai_TownBuilder.h's declaration
// (`int, int, int, void *, int`), so the point16_t and the CUnit * are cast at
// the call. This body shows that declaration is wrong: its first argument is a
// CUnit * (`mov ecx, esi`, where esi came from `movzx ecx, byte ptr [esi+4Ch]`,
// CUnit::bPlayer), its third a point16_t by value and its fourth a point16_t *.
// Correcting it is a signature change against a landed caller, so it is only
// recorded here.
// Spelled without an explicit `__fastcall`, to agree with the declaration in
// sai_TownBuilder.h. Under this TU's `/Gr` both spellings mangle the same, and a
// redundant `__fastcall` is what makes a name look ambiguous when the
// declaration is resolved from the symbol.
int ai_get_build_location_near(CUnit *pUnit, int nUnitType, mpoint_t ptStart,
                          mpoint_t *pPos, int nArg5, int nArg6)
{
    if (nArg5 != 0 && banker_build_site_failed(pUnit->bPlayer))
        return 0;

    int nRadius = ((WORD)nUnitType == 0x7C) ? 0x28 : nArg6;
    int nResult = ai_get_build_location(pUnit, nUnitType, ptStart,
                                      pPos, nRadius);
    if (nResult == 0 && nArg5 != 0)
        banker_set_build_site_failed(pUnit->bPlayer);
    return nResult;
}

// 0x496320  sai_find_attack_route   394 B
// Scores every enemy unit of the town class against a region and returns a
// threat number, 2500 by default. The route-unknown case is its own arm with a
// second copy of the vector test, the take stores nBestDist after nRet and nOut,
// and the tail nests the count test and returns nRet on both of its paths.
int sai_find_attack_route(int nNation, int nRegion, int *pnOut)
{
    int nCenterX;
    int nCenterY;
    int nOutA;
    int nOutB;
    int nBestDist;
    int nBestVec;
    int nBestPlayer;
    int nRet;
    int nOut;
    int nCount;
    int nBestTown;
    CUnit *pUnit;

    nBestDist = 99999999;
    nBestVec = 99999999;
    nBestPlayer = -1;
    nRet = 0x9C4;
    nOut = 0x9C4;
    nCount = 0;
    nBestTown = 0;
    GetRegionCenter((WORD)nRegion, &nCenterX, &nCenterY);

    for (pUnit = CUnit::GetUsedList(); pUnit != 0; pUnit = pUnit->pNext)
    {
        int nTown;
        unsigned int nVec;
        unsigned int nDist;

        if ((gulUnitTypeFlags[pUnit->wType] & 0x1000) == 0)
            continue;
        if (!IsEnemyUnit(nNation, pUnit))
            continue;

        nTown = sai_town_plan_of_unit(pUnit);
        if (nTown == 0)
            continue;

        const int nTownX = town_plan[nTown].nX;
        const int nTownY = town_plan[nTown].nY;

        nCount++;
        nVec = veclen(nCenterX, nTownX, nCenterY, nTownY);
        nDist = sai_region_route_cost(nNation, nRegion, GetRegion_xy(nTownX, nTownY),
                           &nOutA, &nOutB);
        if (nDist == 100000000)
            continue;

        if (nDist == 99999999)
        {
            if (nVec >= (unsigned int)nBestVec)
                continue;
        }
        else if (nBestPlayer == pUnit->bPlayer)
        {
            if (nDist > (unsigned int)nBestDist)
                continue;
        }
        else
        {
            if (nVec >= (unsigned int)nBestVec)
                continue;
        }

        nBestPlayer = pUnit->bPlayer;
        nRet = nOutA;
        nOut = nOutB;
        nBestDist = nDist;
        nBestVec = nVec;
        nBestTown = nTown;
    }

    *pnOut = nOut;
    if ((unsigned int)nCount < 2)
    {
        if ((unsigned int)nBestDist > 1000
            && g_dwElapsedTime < 0x5DC
            && nBestTown != 0
            && town_plan[nBestTown].bUnknown06 != 0)
        {
            return 0x9C4;
        }
        return nRet;
    }
    return nRet;
}


// 0x4964B0  sai_town_first_mineral
// 0x4964B0  sai_town_first_mineral   66 B   -- shape: CUnit used-list walk with two calls
CUnit *sai_town_first_mineral(int nArg0)
{
    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != 0; pUnit = pUnit->pNext)
    {
        if (!pUnit->IsDeadOrDying() && pUnit->IsMineralSrc()
            && pUnit->_unmapped_D8 == nArg0)
        {
            return pUnit;
        }
    }
    return 0;
}

// 0x496500  sai_town_iter_begin
// 0x496500  sai_town_iter_begin   11 B   -- shape: forwarding tail jmp
BOOL sai_town_iter_begin(int *pnTown)
{
    *pnTown = 0;
    return sai_town_iter_next(pnTown);
}

// 0x496510  sai_town_gas_slots
// 0x496510  sai_town_gas_slots   374 B   -- collect a town's three nearest gas sources
// and hand them back sorted by distance.
// All three pointer parameters are `CUnit **` out-params: each is stored a 0 up
// front (`mov dword ptr [edi], 0h` three times), filled with a matching CUnit
// during the used-list walk, and read back for the three-element sort at the
// tail. The declaration spells the second argument `int` and the other two
// `void *`, so the casts are here rather than in the header.
//
// 5F5E0FFh (99,999,999) is the same "no candidate" sentinel as in
// sai_nearest_town_plan. The tail is a three-element bubble sort, (1,2), (2,3),
// (1,2), and the last two swaps do not write the distances back, because they
// are dead afterwards. Reproduced as written.
void sai_town_gas_slots(int nArg0, intptr_t nArg1, void *pArg2, void *pArg3)
{
    CUnit **ppFirst  = (CUnit **)nArg1;
    CUnit **ppSecond = (CUnit **)pArg2;
    CUnit **ppThird  = (CUnit **)pArg3;
    TTownPlan *pPlan = &town_plan[nArg0];

    *ppFirst  = 0;
    *ppSecond = 0;
    *ppThird  = 0;

    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != 0; pUnit = pUnit->pNext)
    {
        if (pUnit->IsDeadOrDying())
            continue;
        if ((gulUnitTypeFlags[pUnit->wType] & 0x2000) == 0)
            continue;
        if (pUnit->IsMineralSrc())
            continue;
        if (pUnit->_unmapped_D8 != nArg0)
            continue;

        if (*ppFirst == 0)
            *ppFirst = pUnit;
        else if (*ppSecond == 0)
            *ppSecond = pUnit;
        else
            *ppThird = pUnit;
    }

    unsigned int nLen1;
    if (*ppFirst != 0)
    {
        CSprite *pSprite = (*ppFirst)->pSprite;
        spos_t sUnitY = PosS(PosU(pSprite->wPosY));
        spos_t sUnitX = PosS(pSprite->wPosX);
        int nTownY = pPlan->nY;
        int nTownX = pPlan->nX;
        nLen1 = veclen(nTownX, sUnitX, nTownY, sUnitY);
    }
    else
    {
        nLen1 = 99999999;
    }

    unsigned int nLen2;
    if (*ppSecond != 0)
    {
        CSprite *pSprite = (*ppSecond)->pSprite;
        spos_t sUnitY = PosS(PosU(pSprite->wPosY));
        spos_t sUnitX = PosS(pSprite->wPosX);
        int nTownY = pPlan->nY;
        int nTownX = pPlan->nX;
        nLen2 = veclen(nTownX, sUnitX, nTownY, sUnitY);
    }
    else
    {
        nLen2 = 99999999;
    }

    unsigned int nLen3;
    if (*ppThird != 0)
    {
        CSprite *pSprite = (*ppThird)->pSprite;
        spos_t sUnitY = PosS(PosU(pSprite->wPosY));
        spos_t sUnitX = PosS(pSprite->wPosX);
        int nTownY = pPlan->nY;
        int nTownX = pPlan->nX;
        nLen3 = veclen(nTownX, sUnitX, nTownY, sUnitY);
    }
    else
    {
        nLen3 = 99999999;
    }

    if (nLen1 > nLen2)
    {
        CUnit *pSwap = *ppFirst;
        unsigned int nSwap = nLen1;
        nLen1 = nLen2;
        nLen2 = nSwap;
        *ppFirst = *ppSecond;
        *ppSecond = pSwap;
    }
    if (nLen2 > nLen3)
    {
        CUnit *pSwap = *ppSecond;
        nLen2 = nLen3;
        *ppSecond = *ppThird;
        *ppThird = pSwap;
    }
    if (nLen1 > nLen2)
    {
        CUnit *pSwap = *ppFirst;
        *ppFirst = *ppSecond;
        *ppSecond = pSwap;
    }
}

// 0x496690  sai_town_worker_target
// The lookup happens twice in the source, and that is the whole 2-byte delta.
// With one lookup this body is 92 B: MSVC forms the pointer straight into ESI
// (`lea esi,[ecx+ecx*2]` / `shl esi,4` / `add esi, town_plan`) and then reuses
// the `add`'s ZF for the null test, so no `test` is emitted. The oracle is 94 B
// because it stages the scaled index through EAX and materialises the pointer
// with a three-address `lea esi,[eax+town_plan]`, which sets no flags and so
// costs an explicit `test esi, esi`.

// The pointer's spelling does not reach that: sixteen combinations of four
// pointer formations and four return expressions produced only 92 B, through the
// shared helper, or 95 B for any inline guard, which sinks `push esi` below the
// range check and duplicates the exit block. The lever is the second lookup for
// the capped computation: MSVC folds the two lookups into one pointer but keeps
// the scaled index in EAX from the first, which is the `lea esi,[eax+...]`
// staging. Five spellings of "look it up twice" all land on 94 B.
int sai_town_worker_target(void *pArg0, int nArg1)
{
    TTownPlan *pPlan = town_plan_at((int)(intptr_t)pArg0);
    if (pPlan == 0)
        return 0;

    return town_plan_capped(town_plan_at((int)(intptr_t)pArg0), nArg1) + pPlan->bUnknown05 * 3;
}

// 0x4966F0  sai_town_mineral_target
// 0x4966F0  sai_town_mineral_target   78 B
// The first parameter is a town index, not a pointer: a scaled-index `lea` read
// as a dereference, the same way as in sai_town_mineral_fields and
// sai_townbuilder_clear. The header still spells it `void *`, so the cast is
// here rather than silently overloading that declaration.
int sai_town_mineral_target(void *pArg0, int nArg1)
{
    TTownPlan *pPlan = town_plan_at((int)(intptr_t)pArg0);
    if (pPlan == 0)
        return 0;

    unsigned int nLimit = player_mineral_fields(nArg1);
    unsigned int nValue = pPlan->dwUnknown08 / 500;
    if (nValue < nLimit)
    {
        nValue = pPlan->bUnknown04;
    }
    else
    {
        unsigned int nMax = pPlan->bUnknown04 * 2;
        if (nValue > nMax)
            nValue = nMax;
    }
    return nValue;
}

// 0x496740  sai_town_iter_next   54 B
// Advance the 1-based town cursor to the next entry whose two low flag bits
// are clear. The bits are tested separately: MSVC loads the byte into CL for
// the two tests and then merges them into `test cl, 3`, where one `& 3` tests
// memory directly and strength-reduces the index into a walking pointer.
// The shipped form holds 0x6A5D68 in ESI across the loop (67 B).
BOOL __declspec(noinline) sai_town_iter_next(int *pnTown)
{
    ++*pnTown;
    if ((unsigned int)*pnTown > dword_6A5D68)
        return FALSE;
    do
    {
        if (!(town_plan[*pnTown].bFlags & 1) && !(town_plan[*pnTown].bFlags & 2))
            return TRUE;
        ++*pnTown;
    }
    while ((unsigned int)*pnTown <= dword_6A5D68);
    return FALSE;
}


// 0x496780  sai_region_route_cost
// The walking cost from one region to another for one nation, plus two out-params
// that report where the walk got to.
// The region record is addressed as a bias then a shift, for the oracle's lea/shl pair.
int sai_region_route_cost(int nNation, int nFromRegion, int nToRegion,
               int *pArg3, int *pArg4)
{
    TAiPathReq req;
    int nFromX;
    int nFromY;
    int nToX;
    int nToY;
    unsigned int i;
    int nTotal;

    *pArg4 = nFromRegion;
    *pArg3 = nToRegion;

    if (nFromRegion == nToRegion)
        return tai_captain_region_enemy_strength(nNation, nFromRegion, 0);

    const ep_glob_t *pGlob = (const ep_glob_t *)epGlob;
    if (pGlob->aRegions[nFromRegion].wArea != pGlob->aRegions[nToRegion].wArea)
        return 99999999;

    GetRegionCenter((WORD)nFromRegion, &nFromX, &nFromY);
    GetRegionCenter((WORD)nToRegion, &nToX, &nToY);

    req.nFromX = PosS(nFromX);
    req.nFromY = PosS(nFromY);
    req.nToX = PosS(nToX);
    req.nToY = PosS(nToY);
    req.dwFlags = 0;
    req.ptToCur = PosPoint(req.nToX, req.nToY);  // Hyperion: the point, not its DWORD (stage E)
    req.dwUnknown01C = 0;
    req.wFromRegion = (WORD)nFromRegion;
    req.wToRegion = (WORD)nToRegion;

    if (sub_491300(reinterpret_cast<path_search_t *>(&req)) == 0)
        return 100000000;

    if (req.nRegionCount <= 2)
        return tai_captain_region_enemy_strength(nNation, nFromRegion, 0)
             + tai_captain_region_enemy_strength(nNation, nToRegion, 0);

    *pArg3 = nToRegion;
    nTotal = 0;

    for (i = 0; i < (unsigned int)req.nRegionCount; i++)
    {
        const int nCost = tai_captain_region_enemy_strength(nNation, (WORD)req.aRegions[i], 0);

        if (*pArg3 == nToRegion)
        {
            if (i >= 3)
            {
                // Reading the region twice leaves EDX for it and EDI for nCost.
                if (((const ep_glob_t *)epGlob)->aRegions[req.aRegions[i - 3]].wTiles >= 0x3C)
                    *pArg4 = req.aRegions[i - 3];
            }

            if (nCost != 0)
                *pArg3 = (WORD)req.aRegions[i];
        }

        nTotal += nCost;
    }

    return nTotal;
}


// 0x496930  sai_get_ground_distance  (399 B)
// Ground (walking) distance between two map points, in tiles: a straight line
// within one region, 9999 across pathing groups, else the sum of the legs
// along the region chain sub_491300 builds. The early exits divide signed and
// the summed exit unsigned, as the oracle does. Reading the new x into a local
// after GetRegionPosition gives EBX to nY2 and spills both region ids.
int sai_get_ground_distance(int nX1, int nY1, int nX2, int nY2)
{
    TAiPathReq req;
    point16_t pt;
    unsigned int nSum;
    unsigned int i;
    const int nR1 = GetRegion_xy(nX1, nY1);
    const int nR2 = GetRegion_xy(nX2, nY2);

    if (nR1 == nR2)
        return veclen(nX1, nX2, nY1, nY2) / 32;

    const ep_glob_t *pGlob = (const ep_glob_t *)epGlob;
    if (pGlob->aRegions[nR1].wArea != pGlob->aRegions[nR2].wArea)
        return 9999;

    // From-first is the order sai_nearest_marked_resource and sai_reserve_walk_range need for their inlined
    // copies; the standalone body emits the same either way.
    req.nFromX = (short)nX1;
    req.nFromY = (short)nY1;
    req.nToX = (short)nX2;
    req.nToY = (short)nY2;
    req.dwFlags = 0;
    req.ptToCur = PosPoint(req.nToX, req.nToY);  // Hyperion: the point, not its DWORD (stage E)
    req.dwUnknown01C = 0;
    req.wFromRegion = (WORD)nR1;
    req.wToRegion = (WORD)nR2;

    if (sub_491300(reinterpret_cast<path_search_t *>(&req)) == 0)
        return 9999;
    if (req.nRegionCount <= 2)
        return veclen(nX1, nX2, nY1, nY2) / 32;

    GetRegionPosition((WORD)req.aRegions[1], &pt);
    nSum = veclen(nX1, pt.x, nY1, pt.y);
    req.nRegionCount--;

    for (i = 2; i < (unsigned int)req.nRegionCount; i++)
    {
        const int nPrevX = pt.x;
        const int nPrevY = pt.y;

        GetRegionPosition((WORD)req.aRegions[i], &pt);
        const int nCurX = pt.x;

        nSum += veclen(nPrevX, nCurX, nPrevY, pt.y);
    }

    return (veclen(pt.x, nX2, pt.y, nY2) + nSum) >> 5;
}


// 0x496AC0  sai_route_is_open
// Returns 0 only when the area search between two regions of one group finds
// an intermediate region that tai_captain_region_is_calm rejects for the player; 1 otherwise.
// The group compare needs the `const ep_glob_t *` local: spelled through
// epGlob twice, the glob lands in ECX where the oracle has EDX.
int sai_route_is_open(int nPlayer, int nRegionA, int nRegionB)
{
    path_search_t req;
    int nAX, nAY, nBX, nBY;

    if (nRegionA == nRegionB)
        return 1;
    const ep_glob_t *pGlob = (const ep_glob_t *)epGlob;
    if (pGlob->aRegions[nRegionA].wArea != pGlob->aRegions[nRegionB].wArea)
        return 1;

    GetRegionCenter((WORD)nRegionA, &nAX, &nAY);
    GetRegionCenter((WORD)nRegionB, &nBX, &nBY);
    req.ptStart.x = (short)nAX;
    req.ptStart.y = (short)nAY;
    req.ptGoal.x = (short)nBX;
    req.ptGoal.y = (short)nBY;
    req.pUnit = NULL;
    req.ptCur = req.ptGoal;
    *reinterpret_cast<DWORD *>(&req.b01C) = 0;
    req.nStartRegion = (WORD)nRegionA;
    req.nGoalRegion = (WORD)nRegionB;
    if (!sub_491300(&req))
        return 1;
    if (req.nRegionCount <= 2)
        return 1;
    req.nRegionCount--;
    for (unsigned int i = 1; i < (unsigned int)req.nRegionCount; i++)
    {
        if (!tai_captain_region_is_calm(nPlayer, (WORD)req.nRegions[i]))
            return 0;
    }
    return 1;
}


// 0x496BF0  sai_nearest_town_plan
// 0x496BF0  sai_nearest_town_plan   134 B   -- nearest town plan to a reference point
// Both value parameters are reassigned on the else path: `mov dword ptr
// [ebp-4h], eax` and `mov dword ptr [ebp+8h], ecx` write the argument slots
// themselves, which is a scratch use of the parameters rather than a reference.
//
// The scan is 1-based and unsigned end to end: the entry guard is
// `cmp eax, esi` / `jb` against a literal 1 and the back edge is `jbe`.
// 5F5E0FFh == 99,999,999 is the "no candidate yet" sentinel.
int sai_nearest_town_plan(int nArg0, int nArg1, int nArg2)
{
    unsigned int nCount;

    if (aiscript_has_campaign(nArg0))
    {
        nCount = dword_6A5D68;
    }
    else
    {
        nArg1 = gStartLocations[nArg0].x;
        nArg2 = gStartLocations[nArg0].y;
        nCount = PlayerLobbySlots();  // Hyperion: the start locations' plans
    }

    int nBest = 0;
    unsigned int nBestLen = 99999999;

    for (unsigned int nTown = 1; nTown <= nCount; ++nTown)
    {
        unsigned int nLen = veclen(nArg1, town_plan[nTown].nX,
                                   nArg2, town_plan[nTown].nY);
        if (nLen <= nBestLen)
        {
            nBest = nTown;
            nBestLen = nLen;
        }
    }
    return nBest;
}

// 0x496C80  sai_recount_town_resources
// 0x496C80  sai_recount_town_resources   398 B   -- the periodic town resource recount
// Four TTownPlan fields are read here and nowhere else in this file:
//   +0x04 BYTE  `inc byte ptr [esi+4h]`   -- a mineral-source count
//   +0x05 BYTE  `inc byte ptr [esi+5h]`   -- the other kind's count
//   +0x08 DWORD `add dword ptr [esi+8h], eax`   with eax = CUnit +0xD0
//   +0x0C DWORD `add dword ptr [esi+0Ch], eax`  (the same eax)
// and bit 0 of +0x07 is a per-pass "already reachable" mark
// (`and byte ptr [eax+3h], 0FEh` in the reset, `or byte ptr [esi+7h], 1h` when
// a unit reaches the town).

// The loop bound is re-read from memory on the back edge while the entry test
// uses the value cached at function entry, and that is not a scheduling
// accident: town_plan + 250*48 is dword_6A5D68, so a store into town_plan[i]
// may alias the bound and MSVC has to reload it. Reproduced by writing the
// global directly in the condition.
void sai_recount_town_resources()
{
    // Three levers here, all measured.
    // (1) The bound is an interior dword of town_plan, not a separate object,
    //     which is what obliges the oracle's reload on the back edge (above).
    // (2) The entry test uses the value cached at function entry while the back
    //     edge re-reads: `cmp edx, ecx` against the `mov edx,[..]` at the top.
    //     A `for` loop cannot say that, since both of its tests take one bound,
    //     so it is a do/while with the entry test written out on the local.
    // (3) The reset's statement order picks the induction anchor. With the
    //     flags written first, MSVC anchors the walking pointer at +0x07 and
    //     emits the one-byte-shorter `and byte ptr [eax], 0FEh`, which shifts
    //     every later byte and every absolute fixup by one. With the flags last
    //     it anchors at +0x04 like the oracle and emits
    //     `and byte ptr [eax+3], 0FEh`.
    DWORD dwCount = dword_6A5D68;
    if (dwCount == 0)
        return;

    if (dword_6A5D6C != 0)
    {
        --dword_6A5D6C;
        return;
    }
    dword_6A5D6C = 0x1C2;

    unsigned int nTown = 1;
    TTownPlan *pReset = &town_plan[1];
    if (nTown <= dwCount)
    {
        do
        {
            pReset->dwUnknown08 = 0;
            pReset->bUnknown04 = 0;
            pReset->bUnknown05 = 0;
            pReset->dwUnknown0C = 0;
            pReset->bFlags &= ~1;
            ++nTown;
            ++pReset;
        } while (nTown <= dword_6A5D68);  // Hyperion: by name, see note 3 (stage E)
    }

    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != 0; pUnit = pUnit->pNext)
    {
        if (pUnit->IsDeadOrDying())
            continue;

        DWORD dwFlags = gulUnitTypeFlags[pUnit->wType];
        if (dwFlags & 0x2000)
        {
            BYTE bTown = pUnit->_unmapped_D8;
            if (bTown == 0)
                continue;

            TTownPlan *pPlan = &town_plan[bTown];
            if (pUnit->IsMineralSrc())
            {
                pPlan->dwUnknown08 += pUnit->wResourceAmount;
                ++pPlan->bUnknown04;
            }
            else
            {
                pPlan->dwUnknown0C += pUnit->wResourceAmount;
                ++pPlan->bUnknown05;
            }
        }
        else if (dwFlags & 1)
        {
            for (unsigned int nTown = 1; nTown <= dword_6A5D68; ++nTown)
            {
                TTownPlan *pPlan = &town_plan[nTown];
                if (pPlan->bFlags & 1)
                    continue;
                if (!UnitCanReachGroundTarget_xy(pUnit, pPlan->nX, pPlan->nY))
                    continue;

                CSprite *pSprite = pUnit->pSprite;
                spos_t sUnitY = PosS(PosU(pSprite->wPosY));
                spos_t sUnitX = PosS(pSprite->wPosX);
                if ((unsigned int)veclen(sUnitX, pPlan->nX,
                                         sUnitY, pPlan->nY) > 0x180)
                    continue;

                pSprite = pUnit->pSprite;
                sUnitY = PosS(PosU(pSprite->wPosY));
                sUnitX = PosS(pSprite->wPosX);
                if ((unsigned int)sai_get_ground_distance(sUnitX, sUnitY,
                                                        pPlan->nX, pPlan->nY) <= 0xC)
                {
                    pPlan->bFlags |= 1;
                    break;
                }
            }
        }
    }
}

// 0x496E10  sai_mark_frontier_regions
// 0x496E10  sai_mark_frontier_regions   266 B
// Rebuilds word_6A5D70, the set of regions between this nation's town and its
// nearest foe in each enemy slot. `dword_6A2E84` is zeroed at entry and written
// back as the count, so the pair is a buffer and a count with no bound check:
// the append at `mov word ptr [edx*2+6A5D70h], si` is guarded only by the
// linear "already present?" scan above it.

// The append set is written through the global rather than a local copy. With
// `DWORD nNum = dword_6A2E84;` the body is the right size but wrong in 12
// places: MSVC puts the load ahead of the zero-trip guard and the extra local
// permutes the whole frame. Naming the global in the loop lets MSVC promote it
// to EDX itself, which places the load in the loop preheader, after the guard,
// as the oracle has it. The shipped form costs 2 B for that: under
// /DOC_DLL_MODE the global is `*(DWORD *)literal`, MSVC declines the promotion
// and re-materialises the address (`mov ecx, 6A2E84h` / `inc dword ptr [eax]`).

// The alliance test selects foes: `cmp byte ptr [ecx+ebx*1], 0h` / `jnz` skips
// every slot whose sgbAlliances_g byte is non-zero, and 0 is foe.
void sai_mark_frontier_regions(int nNation, int nRegion, int nArg2)
{
    WORD aRegions[4];
    int nStatus;
    point16_t pos;
    int nX;
    int nY;
    unsigned int j;

    GetRegionPosition((WORD)nRegion, &pos);
    nX = pos.x;
    nY = pos.y;
    dword_6A2E84 = 0;

    for (j = 0; j < (unsigned)PlayerLobbySlots(); j++)  // Hyperion
    {
        CUnit *pUnit;
        int nUnitRegion;
        unsigned int nRadius;
        unsigned int nCount;

        if (sgbAlliances_g[nNation][j] != 0)
            continue;

        pUnit = ai_nearest_enemy_of_player(nNation, j, nX, nY);
        if (pUnit == 0)
            continue;

        nUnitRegion = GetRegion_CUnit(pUnit);
        for (nRadius = 2; nRadius <= 6; nRadius++)
        {
            nCount = find_choke_regions(nRegion, nUnitRegion, nRadius, aRegions,
                                &nStatus, 2);
            if (nCount != 0 && nCount <= (unsigned int)nArg2)
            {
                unsigned int k;

                for (k = 0; k < nCount; k++)
                {
                    const int nOne = aRegions[k];
                    DWORD m;

                    for (m = 0; m < dword_6A2E84; m++)
                    {
                        if (word_6A5D70[m] == nOne)
                            goto next;
                    }
                    word_6A5D70[dword_6A2E84] = (WORD)nOne;
                    dword_6A2E84++;
                next:
                    ;
                }
                break;
            }
        }
    }
}

// 0x496F20  sai_town_mineral_fields
// 0x496F20  sai_town_mineral_fields   29 B   -- shape: range-checked indexed byte read
// The parameter is a town-plan index, not a pointer, though a scaled-index
// `lea` can be read as a dereference. Both call sites settle it:
// player_mineral_fields and town_delete_if_empty each do `movzx ecx, al` off
// TTown +0x1Ch, a BYTE town-plan index, and this body's own `test ecx,ecx` /
// `cmp ecx, 0FAh` guards read it 32-bit.
int sai_town_mineral_fields(int nTown)
{
    if (nTown == 0 || (unsigned int)nTown >= NUM_TOWN_PLANS)
        return 0;
    return town_plan[nTown].bUnknown04;
}

// 0x496F40  sai_town_vespene_geysers
// 0x496F40  sai_town_vespene_geysers   14 B   -- shape: bare indexed byte read
int sai_town_vespene_geysers(int nTown)
{
    return town_plan[nTown].bUnknown05;
}

// 0x496F50  sai_town_build_anchor
// Picks a town's build anchor: the farthest matching resource, moved 0xC0 back
// toward the town centre when that brings it closer. The nudges are written
// before the raw coordinates (Y first), which keeps the oracle's two sign
// extensions of each coordinate instead of one shared extension.
void sai_town_build_anchor(int nTownPlan, spos_t *pOutX, spos_t *pOutY)
{
    TTownPlan *pPlan = &town_plan[nTownPlan];

    CUnit *pBest = 0;
    unsigned int nBestLen = 0;

    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != 0; pUnit = pUnit->pNext)
    {
        if (pUnit->IsDeadOrDying())
            continue;
        if ((gulUnitTypeFlags[pUnit->wType] & 0x2000) == 0)
            continue;
        if (pUnit->_unmapped_D8 != nTownPlan)
            continue;

        CSprite *pSprite = pUnit->pSprite;
        spos_t sUnitY = PosS(PosU(pSprite->wPosY));
        spos_t sUnitX = PosS(pSprite->wPosX);
        unsigned int nLen = veclen(sUnitX, pPlan->nX, sUnitY, pPlan->nY);
        if (nLen >= nBestLen)
        {
            nBestLen = nLen;
            pBest = pUnit;
        }
    }

    if (pBest != 0)
    {
        CSprite *pSprite = pBest->pSprite;
        spos_t sAngY = PosS(PosU(pSprite->wPosY));
        spos_t sAngX = PosS(pSprite->wPosX);
        int nAngle = AngleBetween(sAngX, sAngY, pPlan->nX, pPlan->nY);

        int nOffX, nOffY;
        reinterpret_cast<CFlingy *>(pBest)->GetVectorComponents(nAngle, 0xC0, &nOffX, &nOffY);

        CSprite *pSprite2 = pBest->pSprite;
        int nNewX = PosS(pSprite2->wPosX) + nOffX;
        int nNewY = pSprite2->wPosY + nOffY;
        int nRawY = PosU(pSprite2->wPosY);
        int nRawX = pSprite2->wPosX;

        unsigned int nOldLen = veclen((short)nRawX, pPlan->nX,
                                      (short)nRawY, pPlan->nY);

        CSprite *pSprite3 = pBest->pSprite;
        spos_t sCurY = PosS(PosU(pSprite3->wPosY));
        spos_t sCurX = PosS(pSprite3->wPosX);
        if (veclen(sCurX, nNewX, sCurY, nNewY) < nOldLen)
        {
            *pOutX = PosS(nNewX);
            *pOutY = PosS(nNewY);
            return;
        }
    }

    *pOutX = pPlan->nX;
    *pOutY = pPlan->nY;
}


// 0x4970C0  sai_town_x
// 0x4970C0  sai_town_x   14 B   -- shape: bare indexed signed word read
int sai_town_x(int nTown)
{
    return town_plan[nTown].nX;
}

// 0x4970D0  sai_town_y
// 0x4970D0  sai_town_y   14 B
int sai_town_y(int nTown)
{
    return town_plan[nTown].nY;
}

// 0x4970E0  sai_townbuilder_choose   631 B
// The AI's "what do I build next, and where" selector. It clears the two
// 9-entry candidate cost arrays, asks sai_townbuilder_score to fill them for every live
// town, picks one of three priority orders out of dword_508FC8 by comparing
// the nation's minerals against its gas, and returns the first town in that
// order that has a candidate -- marking it with `bFlags |= 2`.

// This body gives TTownPlan's +0x08 and +0x0C on its own:
//   +0x08 DWORD  `cmp eax, 7D0h` after `mov eax, dword ptr [eax]` (three sites)
//   +0x0C DWORD  `cmp dword ptr [eax+4h], 3E8h`
// which agrees with the header's dwUnknown08 and dwUnknown0C, derived there from
// an unsigned `mul` reciprocal and a paired accumulate. They are reached through
// one `DWORD *` here because the oracle's `[eax]` / `[eax+4h]` pair has one base
// register serving both offsets.

// The order walk indexes the table, so the first entry is read in place and the
// cursor is formed again from nMode. The four
// 0x6A5D** arrays are DWORD[9].
int sai_townbuilder_choose(int nNation, intptr_t nArg1, int nArg2)
{
    int i;
    unsigned int nTown;
    int nFlag;
    int nMinerals;
    int nGas;
    int nDiff;
    int nMode;
    int nIdx;
    int nRes;

    for (i = 0; i < 9; i++)
    {
        dword_6A5DA4[i] = 99999999;
        dword_6A5DC8[i] = 99999999;
    }

    if (count_get_completion(69, (BYTE)nNation) != 0
        || count_get_completion(11, (BYTE)nNation) != 0
        || (count_get_completion(42, (BYTE)nNation) != 0
            && get_upgrade_level(nNation, 24) != 0))
        nFlag = 1;
    else
        nFlag = 0;

    for (nTown = 1; nTown <= dword_6A5D68; nTown++)
    {
        // Through a BYTE array the flags are loaded into AL before the test, as in the oracle.
        BYTE abFlags[1];
        abFlags[0] = town_plan[nTown].bFlags;
        if ((abFlags[0] & 3) == 0)
        {
            DWORD *const pdw = &town_plan[nTown].dwUnknown08;
            TTownPlan *const pTown = &town_plan[nTown];

            sai_townbuilder_score(nNation, nTown, (void *)nArg1, nFlag, 8);
            if (pdw[1] > 1000)
            {
                if (pdw[0] > 2000)
                    sai_townbuilder_score(nNation, nTown, (void *)nArg1, nFlag, 0);
                else if (pdw[0] != 0)
                    sai_townbuilder_score(nNation, nTown, (void *)nArg1, nFlag, 3);
                else
                    sai_townbuilder_score(nNation, nTown, (void *)nArg1, nFlag, 5);
            }
            else if (pTown->bUnknown05 != 0)
            {
                if (pdw[0] > 2000)
                    sai_townbuilder_score(nNation, nTown, (void *)nArg1, nFlag, 1);
                else if (pdw[0] != 0)
                    sai_townbuilder_score(nNation, nTown, (void *)nArg1, nFlag, 4);
                else
                    sai_townbuilder_score(nNation, nTown, (void *)nArg1, nFlag, 7);
            }
            else
            {
                if (pdw[0] > 2000)
                    sai_townbuilder_score(nNation, nTown, (void *)nArg1, nFlag, 2);
                else if (pdw[0] != 0)
                    sai_townbuilder_score(nNation, nTown, (void *)nArg1, nFlag, 6);
            }
        }
    }

    nGas = gPlayerGas[nNation];
    nMinerals = gPlayerMinerals[nNation];
    nDiff = (nMinerals > nGas) ? nMinerals - nGas : nGas - nMinerals;

    if (nArg2 == 1)
        nMode = 0;
    else if ((unsigned int)nDiff > 500)
        nMode = (nMinerals <= nGas) ? 2 : 1;
    else
        nMode = 2;

    for (int j = 0; (nIdx = dword_508FC8[nMode][j]) != 9; j++)
    {
        if (dword_6A5DA4[nIdx] != 99999999)
        {
            nRes = dword_6A5DEC[nIdx];
            goto found;
        }
        if (dword_6A5DC8[nIdx] != 99999999)
        {
            nRes = dword_6A5E10[nIdx];
            goto found;
        }
    }
    return 0;

found:
    if (nRes == 0)
        return 0;
    {
        TTownPlan *const pMark =
            (TTownPlan *)((BYTE *)town_plan + nRes * sizeof(TTownPlan));

        pMark->bFlags |= 2;
    }
    return nRes;
}


// 0x497360  sai_townbuilder_claim
// Sets bit 1 of a town plan's flags when neither low bit is set. The dword
// test keeps both memory operands, and the result assigned before the `|=`
// in its own arm puts the address in ECX.
BOOL sai_townbuilder_claim(int nTown)
{
    BOOL bOk;
    if ((*(DWORD *)&town_plan[nTown].bFlags & 3) == 0)
    {
        bOk = TRUE;
        town_plan[nTown].bFlags |= 2;
    }
    else
        bOk = FALSE;
    return bOk;
}


// 0x497380  sai_townbuilder_clear
// 0x497380  sai_townbuilder_clear   14 B   -- shape: indexed read-modify-write
// Same parameter finding as sai_town_mineral_fields: sai.h spells it `void *`, the caller
// town_delete_if_empty passes `movzx ecx, byte ptr [edi+1Ch]`.
void sai_townbuilder_clear(int nTown)
{
    town_plan[nTown].bFlags &= ~2;
}

// 0x497390  sai_townbuilder_score
// 0x497390  sai_townbuilder_score   234 B   -- score one town plan against another and
// keep the best per slot, in either the straight-line or the ground metric.
// The third argument is a town index, not a pointer, against another
// scaled-index `lea` (`lea eax,[eax+eax*2]` / `shl eax,4h`). It is cast at the
// top rather than silently overloading the declaration.
//
// The two record blocks store in opposite orders, and that is the oracle's: the
// veclen block writes the index first (`mov [ecx*4+6A5E10h], esi` then
// `mov [ecx*4+6A5DC8h], eax`) and the ground block writes the distance first.
void sai_townbuilder_score(int nArg0, int nArg1, void *pArg2, int nArg3, int nArg4)
{
    int nTown = (int)(intptr_t)pArg2;

    int nAX = town_plan[nTown].nX;
    int nAY = town_plan[nTown].nY;
    int nBX = town_plan[nArg1].nX;
    int nBY = town_plan[nArg1].nY;

    if (tai_captain_regions_connected_xy(nArg0, nAX, nAY, nBX, nBY) == 0)
    {
        if (nArg3 == 0)
            return;
    }
    else if (sai_route_is_open(nArg0, GetRegion_xy(nAX, nAY), GetRegion_xy(nBX, nBY)) != 0
             || nArg3 == 0)
    {
        unsigned int nGround = sai_get_ground_distance(nAX, nAY, nBX, nBY);
        if (nGround <= dword_6A5DA4[nArg4])
        {
            dword_6A5DA4[nArg4] = nGround;
            dword_6A5DEC[nArg4] = nArg1;
        }
        return;
    }

    unsigned int nLen = veclen(nAX, nBX, nAY, nBY);
    if (nLen <= dword_6A5DC8[nArg4])
    {
        dword_6A5E10[nArg4] = nArg1;
        dword_6A5DC8[nArg4] = nLen;
    }
}

// 0x497480  sai_score_resource_patch   418 B
// Scores a resource patch: one plus min(amount / 500 + 1, 4) for every free,
// reachable, same-height resource inside the rect around pRefUnit.
// The rect is a by-value rect16_t (EDX carries nothing); X bounds read as
// members, Y bounds through the whole dword, which keeps both in registers.
int sai_score_resource_patch(CUnit *pRefUnit, mrect_t rc)
{
    CUnit **ppList;
    CUnit **ppCur;
    CUnit *pUnit;
    int nTotal;

    nTotal = 1;
    ppList = get_units_in_rect(&rc);
    ppCur = ppList;
    for (pUnit = *ppCur; pUnit != 0; pUnit = *++ppCur)
    {
        CSprite *const pRefSprite = pRefUnit->pSprite;
        const int nRefY = pRefSprite->wPosY;
        const int nRefX = PosS(pRefSprite->wPosX);
        CSprite *pSprite;
        int nDist;
        int nAdd;

        if ((gulUnitTypeFlags[pUnit->wType] & 0x2000) == 0)
            continue;
        if (pUnit->_unmapped_D8 != 0)
            continue;

        pSprite = pUnit->pSprite;
        if (PosS(pSprite->wPosX) < rc.left)
            continue;
        if (PosS(pSprite->wPosX) > rc.right)
            continue;
        if (PosS(pSprite->wPosY) < rc.top)  // Hyperion: 1.17 reads top and bottom by dword
            continue;
        if (PosS(pSprite->wPosY) > rc.bottom)
            continue;

        if (!UnitCanReachGroundTarget_xy(pUnit, nRefX, nRefY))
            continue;

        {
            CSprite *const pS = pUnit->pSprite;
            const upos_t wY = pS->wPosY;
            const upos_t wX = pS->wPosX;

            if (get_map_height(PosS(wX), PosS(wY))
                    != get_map_height(nRefX, nRefY))
            {
                continue;
            }
        }

        {
            CSprite *const pS = pUnit->pSprite;
            const upos_t wY = pS->wPosY;
            const upos_t wX = pS->wPosX;

            nDist = veclen(nRefX, PosS(wX), nRefY, PosS(wY));
        }
        if ((unsigned int)nDist
                > (unsigned int)(pUnit->IsMineralSrc() ? 0x280 : 0x380))
        {
            continue;
        }

        {
            CSprite *const pS = pUnit->pSprite;
            const upos_t wY = pS->wPosY;
            const upos_t wX = pS->wPosX;

            nDist = sai_get_ground_distance(nRefX, nRefY, PosS(wX), PosS(wY));
        }
        if ((unsigned int)nDist
                > (unsigned int)(pUnit->IsMineralSrc() ? 0x14 : 0x1C))
        {
            continue;
        }

        nAdd = (int)pUnit->wResourceAmount / 500 + 1;
        if ((unsigned int)nAdd > 4)
            nAdd = 4;
        nTotal += nAdd;
    }

    finish_unit_iteration(ppList);
    return nTotal;
}


// 0x497630  TReserve::RemoveDisconnected   149 B
// Clears every reserved cell that does not share a pathing area with the town centre.
// Cell (col,row) is at pixel (x - 0x400 + col*0x20, y - 0x400 + row*0x20).
// The anchor must be a point16_t: each use reloads its half from the slot, as in the oracle.
void TReserve::RemoveDisconnected(mpoint_t pos)
{
    int nArea = GetAreaIndex(GetRegion_xy(pos.x, pos.y));
    int nY = pos.y - 0x400;

    for (unsigned int nRow = 0; nRow < RESERVE_DIM; ++nRow)
    {
        int nX = pos.x - 0x400;

        for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol)
        {
            if (aGrid[nRow][nCol] != 0
                && nArea != GetAreaIndex(GetRegion_xy(nX, nY)))
            {
                aGrid[nRow][nCol] = 0;
            }
            nX += 0x20;
        }
        nY += 0x20;
    }
}


#if !defined(OC_PAYLOAD) || defined(OC_WITH_HELD)
// Clears every reserve cell whose ground distance from the build position,
// in tiles, exceeds nArg1. The position is a point16_t by value: its halves
// are reloaded from the argument slot at each use, as the oracle does.
void sai_reserve_walk_range(void *pArg0, int nArg1, mpoint_t pos)
{
    TReserve *pReserve = (TReserve *)pArg0;
    int nY = pos.y - 0x400;

    for (int nRow = 0; nRow < RESERVE_DIM; ++nRow, nY += 32)
    {
        int nX = pos.x - 0x400;

        for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol, nX += 32)
        {
            if (pReserve->aGrid[nRow][nCol] != 0
                && (unsigned int)sai_get_ground_distance(pos.x, pos.y, nX, nY) > (unsigned int)nArg1)
                pReserve->aGrid[nRow][nCol] = 0;
        }
    }
}
#endif  // OC_PAYLOAD

// 0x497910  sai_reserve_safe_regions   153 B
// The RemoveDisconnected sweep with tai_captain_neighbors_calm(nArg2, region) as the cell test; types
// 106, 131 and 154 skip it. EDX is the TReserve grid, indexed as `[ebx+esi]`.
// The cleared cell stores AL, tai_captain_neighbors_calm's own zero result; `= 0` costs a byte.
// The position is a point16_t, as in TReserve::RemoveDisconnected.
void sai_reserve_safe_regions(int nUnitType, intptr_t nArg1, intptr_t nArg2, mpoint_t pos)
{
    TReserve *pReserve = (TReserve *)nArg1;

    if (nUnitType == 0x6A || nUnitType == 0x83 || nUnitType == 0x9A)
        return;

    int nY = pos.y - 0x400;

    for (unsigned int nRow = 0; nRow < RESERVE_DIM; ++nRow)
    {
        int nX = pos.x - 0x400;

        for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol)
        {
            if (pReserve->aGrid[nRow][nCol] != 0)
            {
                BOOL bKeep = tai_captain_neighbors_calm((int)nArg2, GetRegion_xy(nX, nY));
                if (!bKeep)
                    pReserve->aGrid[nRow][nCol] = (BYTE)bKeep;
            }
            nX += 0x20;
        }
        nY += 0x20;
    }
}


// 0x4979B0  sai_alert_frontier
// 0x4979B0  sai_alert_frontier   108 B
// The 0x2710 frame buffer is 10,000 bytes and is not a `TReserve`, which is
// 0x1000. `mov eax, 2710h` / `call _chkstk` allocates it,
// `memset(.., 0, 2710h)` clears it, and it is handed to
// sai_alert_regions_outward as its fourth argument. Nothing here names its
// element type, so BYTE[] is the honest spelling.
//
// This body confirms sai.h's `void sai_alert_frontier(int,int,int,int)`: ECX is
// forwarded opaquely to sai_mark_frontier_regions and sai_alert_regions_outward,
// EDX and [ebp+8h] are GetRegion_xy's x and y, and [ebp+0Ch] is
// sai_alert_regions_outward's last argument.
void sai_alert_frontier(int nArg0, int nArg1, int nArg2, int nArg3)
{
    BYTE aScratch[0x2710];

    int nRegion = GetRegion_xy(nArg1, nArg2);
    sai_mark_frontier_regions(nArg0, nRegion, 4);
    memset(aScratch, 0, sizeof(aScratch));
    sai_alert_regions_outward(nArg0, nRegion, 1, aScratch, dword_6A2E84 != 0 ? nArg3 : 2);
}


// 0x497A20  sai_townbuilder_init
// Held in the shipped form only; the plain form matches. That form bakes
// 0x6A2E8A (town_plan+2) and 23 more folded element addresses the oracle never
// forms, since it does `add eax, 6A2E88h` and then writes through short
// displacements. Four spellings measure byte-identical, so there is no fix from
// source.
// 0x497A20  sai_townbuilder_init   879 B
// The opening `memset(town_plan, 0, 0x2EE8)` is the fourth thing that says
// town_plan, dword_6A5D68 and dword_6A5D6C are one 0x2EE8-byte object:
// sai_read_town_data and sai_write_town_data serialise exactly that run, the
// arithmetic 250*48 lands on 0x6A5D68, and this body zeroes the whole run in
// one call. The sai_recount_town_resources note says why that matters to the
// codegen.

// The first half is eight unrolled iterations over gStartLocations (stride 4)
// and gPlayerData (stride 0x24): the oracle bakes 0x534A00..0x534A1C and
// 0x6726D0..0x6727CC as sixteen absolute operands, which is what a constant
// trip count of 8 lowers to.
//
// `mov byte ptr [ebx], al` after `neg eax` / `sbb al, al` / `add al, 2` is the
// standard MSVC lowering of `x ? 1 : 2`, not a bit trick of Blizzard's.

void sai_townbuilder_init()
{
    std::vector<BYTE> vKinds(g_pools.units);  // Hyperion: on the stack in 1.17
    BYTE *aKinds = vKinds.data();

#if defined(_WIN64) || defined(__LP64__)
    // Hyperion: town_plan is widened on x64 and the two dwords no longer follow it
    // (note 3), so 1.17's one run is cleared as its parts. Clearing 0x2EE8 bytes left the
    // count and the last plans as the session's last game had them, and the next game's
    // towns were numbered on from there: a replay watched after its game, or a second game's
    // replay played anywhere else, went another way from the first frames.
    memset(town_plan, 0, NUM_TOWN_PLANS * sizeof(TTownPlan));
    dword_6A5D68 = 0;
    dword_6A5D6C = 0;
#else
    memset(town_plan, 0, 0x2EE8);
#endif

    for (int i = 0; i < PlayerLobbySlots(); ++i)  // Hyperion
    {
        // The fields are read directly rather than bound to `short` locals: a
        // local makes MSVC promote (`movzx eax, word ptr [..]`, 8 sites), while
        // the field read stays 16-bit end to end, which is the oracle's
        // `mov dx, word ptr [0x534A00]` / `test dx, dx`.
        if (gStartLocations[i].x != 0 || gStartLocations[i].y != 0)
        {
            // Same interior-dword spelling as sai_recount_town_resources: the oracle re-reads
            // the count at the head of all eight unrolled iterations, which it
            // is only obliged to do if the stores into town_plan[] may alias it.
            TTownPlan *pPlan = &town_plan[++dword_6A5D68];  // Hyperion: by name, see note 3 (stage E)
            pPlan->nX = gStartLocations[i].x;
            pPlan->nY = gStartLocations[i].y;
            BYTE nType = gPlayerData[i].nType;
            pPlan->bUnknown06 = 1;
            if (nType == 2 || nType == 1)
                pPlan->bFlags |= 1;
        }
    }

    memset(aKinds, 0, vKinds.size());

    // The null test is hoisted out and pKind declared inside it, because the
    // oracle pushes EBX and EDI after `test esi,esi / jz`: MSVC confines the two
    // callee-saved registers to the region that uses them only if the source
    // guards that region. Written as a plain `for` over a possibly-empty list,
    // both pushes move up above the memset and cost 10 bytes.
    CUnit *pUnit = CUnit::GetUsedList();
    if (pUnit != 0)
    {
    BYTE *pKind = aKinds;
    for (; pUnit != 0; pUnit = pUnit->pNext, ++pKind)
    {
        // One shared `*pKind = 0` block, not two. Written as two separate `if`
        // statements MSVC emits the store-and-continue twice, 5 B each, and the
        // whole rest of the loop shifts by 10 bytes; the oracle branches both
        // guards to a single block between the gas arm and the loop increment,
        // which is also what forces the gas arm's trailing `jmp`.
        if (pUnit->IsDeadOrDying() ||
            (gulUnitTypeFlags[pUnit->wType] & 0x2000) == 0)
        {
            *pKind = 0;
            continue;
        }

        // The order matters: the oracle calls IsMineralSrc first and lets MSVC
        // schedule the zero-store between `neg eax` and `sbb al, al`. Written
        // the other way round the store lands before the call, which is the
        // last two bytes of this body.
        BYTE bKind = pUnit->IsMineralSrc() ? 1 : 2;
        pUnit->_unmapped_D8 = 0;
        *pKind = bKind;

        int nTown = sai_town_plan_of_unit(pUnit);
        if (nTown == 0)
            continue;

        TTownPlan *pPlan = &town_plan[nTown];
        if (pUnit->IsMineralSrc())
        {
            pPlan->dwUnknown08 += pUnit->wResourceAmount;
            ++pPlan->bUnknown04;
            *pKind = 3;
            pUnit->_unmapped_D8 = (BYTE)nTown;
        }
        else
        {
            pPlan->dwUnknown0C += pUnit->wResourceAmount;
            ++pPlan->bUnknown05;
            *pKind = 4;
            pUnit->_unmapped_D8 = (BYTE)nTown;
        }
    }

    }

    sai_seed_town_plans(2, aKinds);
    sai_seed_town_plans(1, aKinds);
}


// 0x497D90  sai_reserve_addon_space
// Walks the player's units and, for each live building that can take an addon
// (0x6A, 0x71, 0x72, 0x74), clears the 2-wide strip of the reserve grid where the
// addon would sit, and restores the snapshot if that emptied the grid. The height
// comes from the nArg0 type, not the unit's. The column test sits in the height
// loop's condition, so only its first conjunct is copied as the rotation guard.
void sai_reserve_addon_space(int nArg0, intptr_t nArg1, void *pArg2, mpoint_t pos)
{
    TReserve *pReserve = (TReserve *)nArg1;
    TReserve saved;

    memcpy(&saved, pReserve, sizeof(TReserve));

    for (CUnit *pUnit = CUnit::GetPlayerUnitList((int)(intptr_t)pArg2);
         pUnit != NULL;
         pUnit = pUnit->pPlayerUnitNext)
    {
        if (pUnit->IsDeadOrDying())
            continue;

        switch (pUnit->wType)
        {
        case 0x6A:
        case 0x71:
        case 0x72:
        case 0x74:
            {
                CSprite *pSprite = pUnit->pSprite;

                int nDY = PosS(pSprite->wPosY) / 32 - pos.y / 32;
                unsigned int nHeight = gUnitPlaceboxSize[(WORD)nArg0].y / 32 + 1;
                int nDX = PosS(pSprite->wPosX) / 32 - pos.x / 32;

                int nCol = nDX + 0x22;
                for (int k = 0; k < 2; ++k)
                {
                    int nRow = nDY + 0x21;
                    for (unsigned int i = 0; i < nHeight && nCol >= 0 && nCol <= 0x3F; ++i)
                    {
                        if (nRow >= 0 && nRow <= 0x3F)
                            pReserve->aGrid[nRow][nCol] = 0;
                        --nRow;
                    }
                    ++nCol;
                }
            }
            break;

        default:
            break;
        }
    }

    for (unsigned int nRow = 0; nRow < RESERVE_DIM; ++nRow)
    {
        for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol)
        {
            if (pReserve->aGrid[nRow][nCol] != 0)
                return;
        }
    }

    memcpy(pReserve, &saved, sizeof(TReserve));
}


// True when a neighbour cell is off the grid or empty in the snapshot. The
// signed range checks keep the oracle's repeated row tests; the row pointer
// sets how the (c-1, r+1) address is formed.
static inline BOOL reserve_cell_open(const TReserve *pSaved, int nCol, int nRow)
{
    if (nCol < 0 || nCol > 0x3F
        || nRow < 0 || nRow > 0x3F)
        return TRUE;
    const BYTE *pRow = pSaved->aGrid[nRow];
    return pRow[nCol] == 0;
}

// Restores the snapshot when no cell is left. ReserveDefault expands it on both
// switch paths, which is the oracle's two copies of this loop.
static inline void reserve_restore_if_empty(TReserve *pReserve, const TReserve *pSaved)
{
    for (unsigned int nRow = 0; nRow < RESERVE_DIM; ++nRow)
    {
        for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol)
        {
            if (pReserve->aGrid[nRow][nCol] != 0)
                return;
        }
    }

    memcpy(pReserve, pSaved, sizeof(TReserve));
}

// Reserves the footprints of the player's defensive and support buildings,
// then, for the types that want room around them, clears every candidate cell
// that touches an open one. Restores the snapshot if nothing is left.
// nPos is an int because the scan's row pointer is spilled into its slot.
void ReserveDefault(int nArg0, intptr_t nArg1, intptr_t nArg2, mpoint_t pos)
{
    TReserve *pReserve = (TReserve *)nArg1;
    TReserve saved;

    memcpy(&saved, pReserve, sizeof(TReserve));

    for (CUnit *pUnit = CUnit::GetPlayerUnitList((int)nArg2);
         pUnit != NULL;
         pUnit = pUnit->pPlayerUnitNext)
    {
        if (pUnit->IsDeadOrDying())
            continue;

        switch (pUnit->wType)
        {
        case 0x71:
        case 0x7C:
        case 0x9B:
        case 0xA0:
        case 0xA2:
            ReserveStructure(gUnitPlaceboxSize[pUnit->wType].x / 32,
                             gUnitPlaceboxSize[pUnit->wType].y / 32,
                             PosS(pUnit->pSprite->wPosX) / 32,
                             PosS(pUnit->pSprite->wPosY) / 32,
                             nArg0, nArg1, pos, 2);
            break;

        case 0x6F:
        case 0x7D:
        case 0x8F:
            ReserveStructure_CUnit(pUnit, nArg0, nArg1, pos, 1);
            break;
        }
    }

    switch (nArg0)
    {
    case 0x6F:
    case 0x71:
    case 0x7C:
    case 0x7D:
    case 0x9B:
    case 0xA0:
    case 0xA2:
        for (unsigned int nRow = 0; nRow < RESERVE_DIM; ++nRow)
        {
            for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol)
            {
                if (pReserve->aGrid[nRow][nCol] != 0
                    && (reserve_cell_open(&saved, nCol - 1, nRow - 1)
                        || reserve_cell_open(&saved, nCol, nRow - 1)
                        || reserve_cell_open(&saved, nCol + 1, nRow - 1)
                        || reserve_cell_open(&saved, nCol - 1, nRow)
                        || reserve_cell_open(&saved, nCol + 1, nRow)
                        || reserve_cell_open(&saved, nCol - 1, nRow + 1)
                        || reserve_cell_open(&saved, nCol, nRow + 1)
                        || reserve_cell_open(&saved, nCol + 1, nRow + 1)))
                    pReserve->aGrid[nRow][nCol] = 0;
            }
        }
        reserve_restore_if_empty(pReserve, &saved);
        break;

    default:
        reserve_restore_if_empty(pReserve, &saved);
        break;
    }
}

// Reserves the footprint of every building the player has that another
// structure may not overlap, then the neutral player's, and restores the
// snapshot if that left no free cell.
void sai_reserve_buildings(int nArg0, intptr_t nArg1, intptr_t nArg2, mpoint_t pos)
{
    TReserve *pReserve = (TReserve *)nArg1;

    if (gulUnitTypeFlags[nArg0] & 2)
        return;

    TReserve saved;

    memcpy(&saved, pReserve, sizeof(TReserve));

    for (CUnit *pUnit = CUnit::GetPlayerUnitList((int)nArg2);
         pUnit != NULL;
         pUnit = pUnit->pPlayerUnitNext)
    {
        if (pUnit->IsDeadOrDying())
            continue;

        if (((pUnit->dwFlags & 1)
             || pUnit->wType == 0x85 || pUnit->wType == 0x84
             || (pUnit->wType == 0x83 && pUnit->IsZergBldgUpgrading()))
            && (pUnit->dwFlags & 2)
            && (gulUnitTypeFlags[pUnit->wType] & 0x1000)
            || (gulUnitTypeFlags[pUnit->wType] & 0x2000))
        {
            ReserveStructure(gUnitPlaceboxSize[pUnit->wType].x / 32,
                             gUnitPlaceboxSize[pUnit->wType].y / 32,
                             PosS(pUnit->pSprite->wPosX) / 32,
                             PosS(pUnit->pSprite->wPosY) / 32,
                             nArg0, nArg1, pos, 2);
        }
    }

    if (nArg0 != 0x7D)
    {
        for (CUnit *pUnit = CUnit::GetPlayerUnitList(11);
             pUnit != NULL;
             pUnit = pUnit->pPlayerUnitNext)
        {
            if (pUnit->IsDeadOrDying())
                continue;

            if (gulUnitTypeFlags[pUnit->wType] & 0x2000)
            {
                ReserveStructure(gUnitPlaceboxSize[pUnit->wType].x / 32,
                                 gUnitPlaceboxSize[pUnit->wType].y / 32,
                                 PosS(pUnit->pSprite->wPosX) / 32,
                                 PosS(pUnit->pSprite->wPosY) / 32,
                                 nArg0, nArg1, pos, 2);
            }
        }
    }

    for (unsigned int nRow = 0; nRow < RESERVE_DIM; ++nRow)
    {
        for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol)
        {
            if (pReserve->aGrid[nRow][nCol] != 0)
                return;
        }
    }

    memcpy(pReserve, &saved, sizeof(TReserve));
}

// 0x498430  TReserve::RemoveWrongHeight   229 B
// Clears every reserved cell whose map height differs from the anchor's, and
// restores the snapshot if that emptied the grid. HasValidSpace is written out.
// The anchor is a point16_t; the height call's Y is read through the whole
// dword, the sweep's halves as members. As an int, MSVC hoists the row X base.
void TReserve::RemoveWrongHeight(mpoint_t pos)
{
    TReserve saved;

    memcpy(&saved, this, sizeof(TReserve));

    int nHeight = get_map_height(pos.x, pos.y);
    int nY = pos.y - 0x400;

    for (unsigned int nRow = 0; nRow < RESERVE_DIM; ++nRow)
    {
        int nX = pos.x - 0x400;

        for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol)
        {
            if (aGrid[nRow][nCol] != 0 && nHeight != get_map_height(nX, nY))
                aGrid[nRow][nCol] = 0;
            nX += 0x20;
        }
        nY += 0x20;
    }

    for (unsigned int nRow = 0; nRow < RESERVE_DIM; ++nRow)
    {
        for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol)
        {
            if (aGrid[nRow][nCol] != 0)
                return;
        }
    }

    memcpy(this, &saved, sizeof(TReserve));
}


// 0x498520  sai_reserve_pylon_placement   217 B
// Clears every reserved cell already inside a pylon's power field (0xA2 is the
// Pylon), then undoes the pass if that emptied the grid.
// The anchor is a point16_t with Y read through the whole dword: that shares
// the first row's X load with Y, as the oracle does. pos.y costs 20 B.
void sai_reserve_pylon_placement(intptr_t nArg0, int nPlayer, mpoint_t pos)
{
    TReserve *pReserve = (TReserve *)nArg0;
    TReserve saved;

    memcpy(&saved, pReserve, sizeof(TReserve));

    int nY = pos.y - 0x400;

    for (unsigned int nRow = 0; nRow < RESERVE_DIM; ++nRow)
    {
        int nX = pos.x - 0x400;

        for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol)
        {
            if (pReserve->aGrid[nRow][nCol] != 0
                && IsCellPowered(0xA2, nX, nY, (BYTE)nPlayer))
            {
                pReserve->aGrid[nRow][nCol] = 0;
            }
            nX += 0x20;
        }
        nY += 0x20;
    }

    for (unsigned int nRow = 0; nRow < RESERVE_DIM; ++nRow)
    {
        for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol)
        {
            if (pReserve->aGrid[nRow][nCol] != 0)
                return;
        }
    }

    memcpy(pReserve, &saved, sizeof(TReserve));
}


// 0x498600  sai_reserve_turret_placement
// 0x498600 (461 B). Clear each live turret's coverage circle from the reserve
// grid; if that empties it, restore the snapshot and recurse one ring smaller.
// The anchor goes through pt, copied per unit and again after the loop, which
// gives it ESI instead of nArg3; nRow steps in the row loop's increment.
void sai_reserve_turret_placement(intptr_t nArg0, intptr_t nArg1, mpoint_t pos, int nArg3, int nArg4)
{
    TReserve *pReserve = (TReserve *)nArg0;
    TReserve saved;

    memcpy(&saved, pReserve, sizeof(TReserve));

    point16_t pt;
    for (CUnit *pUnit = CUnit::GetPlayerUnitList((int)nArg1);
         pUnit != NULL;
         pUnit = pUnit->pPlayerUnitNext)
    {
        if (pUnit->IsDeadOrDying())
            continue;
        pt = pos;

        switch (pUnit->wType)
        {
        case 0x7C:
        case 0x7D:
        case 0xA2:
            if ((WORD)nArg3 != pUnit->wType)
                break;
            // fall through -- the oracle's jump table enters the next group's
            // body directly for 0x8F/0x90/0x92, i.e. past this compare.

        case 0x8F:
        case 0x90:
        case 0x92:
            {
                BYTE *pMask;
                int nSpan;

                switch (nArg4)
                {
                case 0:  nSpan = 0x11; pMask = unit_tilerange_1; break;
                case 1:  nSpan = 0x0D; pMask = unit_tilerange_2; break;
                case 2:  nSpan = 0x09; pMask = unit_tilerange_3; break;
                case 3:  nSpan = 0x05; pMask = unit_tilerange_4; break;
                default: return;
                }

                int nHalf = (unsigned)nSpan / 2;
                int nRow = (PosS(pUnit->pSprite->wPosY)
                            - pos.y) / 32 - nHalf + 0x1F;

                for (unsigned int i = 0; i < (unsigned)nSpan; ++i, ++nRow)
                {
                    if (nRow >= 0x40)
                        break;

                    if (nRow < 0)
                    {
                        pMask += nSpan;
                    }
                    else
                    {
                        int nCol = (PosS(pUnit->pSprite->wPosX)
                                    - pt.x) / 32 - nHalf + 0x1F;

                        // The signed range check gives the oracle's second column
                        // register; `(unsigned)nCol <= 0x3F` merges the two.
                        int j = nSpan;
                        do
                        {
                            if (nCol >= 0 && nCol < 0x40 && *pMask == 0x78)
                                pReserve->aGrid[nRow][nCol] = 0;
                            ++nCol;
                            ++pMask;
                        } while (--j != 0);
                    }
                }
            }
            break;

        default:
            break;
        }
    }

    pt = pos;
    for (unsigned int nRow = 0; nRow < RESERVE_DIM; ++nRow)
    {
        for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol)
        {
            if (pReserve->aGrid[nRow][nCol] != 0)
                return;
        }
    }

    memcpy(pReserve, &saved, sizeof(TReserve));

    if ((unsigned)nArg4 < 3)
        sai_reserve_turret_placement(nArg0, nArg1, pt, nArg3, nArg4 + 1);
}


// 0x498820  sai_reserve_buildable_tiles   261 B
// Sweeps the 64x64 tile square around the anchor tile and reserves a 1x1
// footprint on every on-map tile without matrix flag 0x10000; restores the
// snapshot if that emptied the grid. nArg1 is the grid.
// The anchor is a point16_t with Y read through the whole dword; pos.y costs 16 B.
void sai_reserve_buildable_tiles(int nArg0, intptr_t nArg1, mpoint_t pos)
{
    TReserve *pReserve = (TReserve *)nArg1;
    TReserve saved;

    memcpy(&saved, pReserve, sizeof(TReserve));

    int nY = pos.y / 32 - 0x20;

    for (unsigned int nRow = 0; nRow < RESERVE_DIM; ++nRow)
    {
        if (nY >= gwMapHeight)
            break;

        if (nY >= 0)
        {
            int nX = pos.x / 32 - 0x20;

            for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol)
            {
                if (nX >= gwMapWidth)
                    break;

                if (nX >= 0 && !mtx_has_flags(PosS(nX), PosS(nY), 0x10000, 0))
                    ReserveStructure(1, 1, nX, nY, nArg0, nArg1, pos, 1);

                ++nX;
            }
        }
        ++nY;
    }

    for (unsigned int nRow = 0; nRow < RESERVE_DIM; ++nRow)
    {
        for (unsigned int nCol = 0; nCol < RESERVE_DIM; ++nCol)
        {
            if (pReserve->aGrid[nRow][nCol] != 0)
                return;
        }
    }

    memcpy(pReserve, &saved, sizeof(TReserve));
}


// 0x498930  sai_reserve_template_spacing
// 0x498930  sai_reserve_template_spacing   192 B
// Walks the build-template table, zeroing one 64-cell line of the reserve grid
// per entry, and undoes the whole thing the moment the grid runs empty.

// The saved copy is a whole `TReserve` on the stack: `mov eax, 1008h` /
// `call _chkstk` / `lea eax, [ebp-1008h]` -- 0x1000 for the object plus the two
// dwords the frame needs ([ebp-4] the table index, [ebp-8] the X step spill).

// `aGrid[nY][nX]` is what MSVC strength-reduces into the oracle's pair: the row
// term is kept pre-multiplied (`shl eax, 6h` once, then `add eax, ebx` with ebx
// the step also pre-multiplied) while the column term is added raw
// (`lea ecx, [eax+edx]`). Both `shl 6h` are the compiler's, not the source's.
void sai_reserve_template_spacing(TReserve *pReserve, mpoint_t pos)
{
    TReserve saved;
    int i;

    memcpy(&saved, pReserve, sizeof(TReserve));

    i = 0;
    while (sgBuildTemplates_g[i].nX != -1)
    {
        int nX  = sgBuildTemplates_g[i].nX;
        int nY  = sgBuildTemplates_g[i].nY;
        int nDX = sgBuildTemplates_g[i].nDX;
        int nDY = sgBuildTemplates_g[i].nDY;

        for (int k = 0; k < RESERVE_DIM; ++k)
        {
            pReserve->aGrid[nY][nX] = 0;
            nX += nDX;
            nY += nDY;
        }

        if (pReserve->HasValidSpace())
        {
            memcpy(pReserve, &saved, sizeof(TReserve));
            return;
        }
        ++i;
    }
}

