// port_hxsave.cpp -- a save of a Hyperion game:
// the sections a game by Hyperion's rules (HxRules) adds to 1.17's save, HXGM after GAME, then
// HXRC, HXRA, HXAI and HXHC when a slot needs them (each a DWORD count of slots, then a record a
// slot), HXST after VIEW, HXWI after it when a mod's ids pass 255 and HXMT when it has techs or
// upgrades past 1.17's (docs/modules/replays.md,
// "Hyperion's save").
//
// HXGM, read before the game starts, turns on the player tables' extra entries, which the
// sections between it and HXST read through; a save without it is 1.17's and turns them off.
// The parts that keep pointers or lists save their slots past 7 after 1.17's eight: the AI's
// bankers, captains, towns and guards, the trigger lists and the unit lists (SAI_Banker.cpp,
// TAI_Captain.cpp, SAI_Build.cpp, TAI_GuardPost.cpp, victory.cpp, CUnitSave.cpp).
//
// HXST keeps what a 1.17 load loses (the random state, the lists' order, the counters between
// the game's passes), so a loaded game goes on frame for frame as the game saved does, and the
// load compares the state's hash with the one saved (hyperion_save.log). Saving changes the
// game, so a Hyperion replay makes the same saves on playback (port_hxreplay.cpp).
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "../data/port_formats.h"  // PORT_SAVE_ID, PortSaveSection, PortLoadSectionIf
#include "../../gen/save117.h"     // SAVE117_CUnit, SAVE117_CBullet, SAVE117_COrder
#include "../platform/port_standalone.h"  // PortCrtRandState
#include "../render/port_teamcolor.h"  // PortTeamColorGet, PortTeamColorSet
#include "../platform/port_log.h"  // PortLogger
#include "CUnit.h"
#include "CSprite.h"
#include "CImage.h"
#include "CThingy.h"
#include "CBullet.h"
#include "COrder.h"
#include "CLink.h"
#include "../../h/SAI_Banker.h"
#include "../../h/SAI_Build.h"
#include "../../h/SAI_Scripts.h"
#include "../../h/TAI_Captain.h"
#include "../../h/TAI_GuardPost.h"
#include "oc_data.h"
#include "../../h/playertables.h"
#include "../../h/playervision.h"
#include "../../h/maplimits.h"
#include "../../h/poollimits.h"     // PoolsUnitsToRecord, PoolsUnitsRecorded
#include "../../h/saveload.h"       // pfread, pfwrite, compress, decompress
#include "../../h/port_hxreplay.h"  // the rule record
#include "../../h/port_synchash.h"
#include "../../h/port_hxsave.h"
#include "../../h/hxrules.h"
#include "../../h/selectlimits.h"  // SelectionSave, SelectionLoad: Hyperion's selection
#include "../data/port_moddata.h"  // g_hxModGame
#include "../gameplay/port_ai.h"        // AiProfilesSave, AiProfilesRecorded
#include "../gameplay/port_handicap.h"  // HandicapsSave, HandicapsRecorded
#include "../gameplay/port_mutators.h"  // MutatorsSave, MutatorsRecorded
#include "../gameplay/port_richbases.h"  // RichBasesSave, RichBasesLoad
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

extern CUnit sgUnitsMem[POOL_UNITS_STORE];
extern CSprite sgSprites[POOL_SPRITES_STORE];
extern CThingy sgThingies1Arr_g[POOL_THINGIES_STORE];
extern CThingy *thingies1_sgpFreeHead;
extern CThingy *thingies1_sgpFreeTail;
extern CThingy *thingies1_sgpUsedHead;
extern CThingy *thingies1_sgpUsedTail;
extern CThingy sgThingies2Arr_g[POOL_THINGIES_STORE];
extern CThingy *thingies2_sgpFreeHead;
extern CThingy *thingies2_sgpFreeTail;
extern CThingy *thingies2_sgpUsedHead;
extern CThingy *thingies2_sgpUsedTail;
extern CBullet sgBullets[POOL_BULLETS_STORE] OC_UNALIGNED;
extern COrder sgOrders[POOL_ORDERS_STORE] OC_UNALIGNED;
extern CLink *sgBulletsUsedTail_g;
extern CLink *sgBulletsFreeHead_g;
extern CLink *sgBulletsFreeTail_g;
extern BYTE sgbCachedregion_g;
extern WORD sgwCheckDelay;
extern TWorkerPool gPeons_g;
extern TBldgPool gBldgs_g;
extern TTownPool gTowns_g;
extern TSoldierPool gSoldiers_g;
extern TGuardPool gGuards_g;
extern TScriptPool gScripts_g;
// The counters between the game's passes, which 1.17's load starts again: the AI's
// (sai_update, ai_periodic_timer_tick, the banker's staging, the town builder's regions, the
// captains'), the creep's decay and the trigger clock's.
extern DWORD dword_69B560;
extern DWORD dword_69B564;
extern DWORD dword_69B568;
extern DWORD dword_69B56C;
extern PlayerBankerSupplyTable banker_staging_supplycost_g;
extern DWORD dword_685574;
extern DWORD dword_6A2E84;
extern WORD word_6A5D70[24];
extern DWORD dword_6B1A94;
extern DWORD dword_6B1A98;
extern WORD sgwTickDelay;    // the trigger clock's frames to the next game second,
extern WORD sgwLBDelay;      // to the next leaderboard update,
extern WORD sgwVerifyDelay;  // and to the next victory check (victory.cpp)
extern PlayerRegionsTable sgRegions;
extern PlayerTable<DWORD, PlayerTag6B68C0, 8> dword_6B68C0;
extern PlayerTable<DWORD, PlayerTag6B68E0, 8> dword_6B68E0;
extern DWORD sgdwOrderSeqDelay_g;            // the frames to the next order slots
extern DWORD sgdwBackgroundOrderSeqDelay_g;  // (CUnit::EntityUpdate)

namespace {

const ULONG kSetUp = PORT_SAVE_ID('H', 'X', 'G', 'M');
const ULONG kState = PORT_SAVE_ID('H', 'X', 'S', 'T');
const ULONG kColours = PORT_SAVE_ID('H', 'X', 'R', 'C');
const ULONG kRaces = PORT_SAVE_ID('H', 'X', 'R', 'A');
const ULONG kAiLevels = PORT_SAVE_ID('H', 'X', 'A', 'I');
const ULONG kHandicaps = PORT_SAVE_ID('H', 'X', 'H', 'C');  // the slots' handicaps
const ULONG kMutators = PORT_SAVE_ID('H', 'X', 'M', 'U');  // the game's mutators
const ULONG kUnits = PORT_SAVE_ID('H', 'X', 'U', 'N');  // the unit table, when not the slots'
const ULONG kWideIds = PORT_SAVE_ID('H', 'X', 'W', 'I');  // ids past 255, after HXST
const ULONG kModTech = PORT_SAVE_ID('H', 'X', 'M', 'T');  // a mod's techs and upgrades, after

// HXRC's records, as the load read them, until the loaded game starts.
BYTE g_abLoadRgb[PLAYER_SLOTS_MAX][4];
bool g_bLoadRgb;

// HXST's head. After it: the player tables' extra entries and the tech of the slots past
// 11, the vision masks, the trigger counts, the lists' order, the AI's demands and the
// counters (below), the cargo of a game of wide unit ids, and every human's selection and
// groups in a game of Hyperion's selection (port_selwide.cpp).
struct StateHead {
  DWORD dwVision;        // whether the vision masks follow
  DWORD dwRandState;     // the game's random numbers, which a load reseeds
  DWORD dwRandCount;
  DWORD dwCheckDelay;    // the frames to the next trigger pass
  DWORD dwCacheKind;     // the trigger counts' kind: sgbCachedEnum_g | sgbCachedregion_g << 8
  DWORD dwFogCountdown;  // dword_6663E4, which a game's start sets to 1
  DWORD adwOrderSeq[2];  // sgdwOrderSeqDelay_g and sgdwBackgroundOrderSeqDelay_g, set to 10, 150
  DWORD dwCrtRand;       // the C runtime's rand (PortCrtRandState), which a game's start seeds
  SyncHash hash;         // the state's hash as it was saved
};

// A loaded game's state as it was saved, until the game has started and it is compared;
// then whether the game is a Hyperion save going on.
bool g_pending;
StateHead g_saved;
bool g_goesOn;
// The early lists of a Hyperion save being loaded, until the units are read.
bool g_earlyPending;
std::vector<DWORD> g_early;
// Whether the save being loaded is a Hyperion save, from its set-up on.
bool g_loading;
// Where HxSaveReplay makes a save again, while it does.
const char *g_pszTarget;

// With HYPERION_SYNC_DUMP=<frame>,<folder>, each domain's bytes as hashed go to
// <folder>/<name>.*: the game as saved, and as loaded, beside the game's own at that frame
// (port_hxreplay.cpp).
void Dump(const char *pszName) {
  char szDump[MAX_PATH + 16];
  if (!GetEnvironmentVariableA("HYPERION_SYNC_DUMP", szDump, MAX_PATH) || !strchr(szDump, ','))
    return;
  char szPath[MAX_PATH + 32];
  wsprintfA(szPath, "%s\\%s", strchr(szDump, ',') + 1, pszName);
  SyncHashDump(szPath);
}

// The lists a load rebuilds in slot order, where a new object comes from the head of the
// free list: so the next unit, sprite, image, thingy or bullet the game makes would take
// another slot than in the game saved. Their order is saved as slots, head first, and the
// same nodes linked again in it after the load. Orders and the AI's pools save their links.
struct PoolList {
  void *pBase;
  size_t cbItem;
  const int *pnCount;  // the game's count (g_pools), read when the list is
  CLink **ppHead;
  CLink **ppTail;
};

template <class T, class P>
PoolList List(T *pBase, const int *pnCount, P **ppHead, P **ppTail) {
  PoolList l = {pBase, sizeof(T), pnCount, reinterpret_cast<CLink **>(ppHead), reinterpret_cast<CLink **>(ppTail)};
  return l;
}

// Two sets. The sprites' and images' free lists as the save writes them, after the
// pylons' power fields are taken down (DestroyPsiLinks): a load puts them up again
// (RestorePsiLinks) as the save did, from these lists, before the late section is read.
// The rest as the game goes on.
const PoolList *Lists(bool bEarly, int *pnLists) {
  static const PoolList s_early[] = {
      List(sgSprites, &g_pools.sprites, &sprites_sgpFreeHead, &sprites_sgpFreeTail),
      List(sgImages, &g_pools.images, &images_sgpFreeHead_g, &images_sgpFreeTail),
  };
  static const PoolList s_late[] = {
      List(sgUnitsMem, &g_pools.units, &sgpFreeHead, &sgpFreeTail),
      List(sgThingies1Arr_g, &g_pools.thingies, &thingies1_sgpFreeHead, &thingies1_sgpFreeTail),
      List(sgThingies1Arr_g, &g_pools.thingies, &thingies1_sgpUsedHead, &thingies1_sgpUsedTail),
      List(sgThingies2Arr_g, &g_pools.thingies, &thingies2_sgpFreeHead, &thingies2_sgpFreeTail),
      List(sgThingies2Arr_g, &g_pools.thingies, &thingies2_sgpUsedHead, &thingies2_sgpUsedTail),
      List(sgBullets, &g_pools.bullets, &sgBulletsFreeHead_g, &sgBulletsFreeTail_g),
      List(sgBullets, &g_pools.bullets, &sgBulletsUsedHead_g, &sgBulletsUsedTail_g),
  };
  *pnLists = bEarly ? sizeof s_early / sizeof s_early[0] : sizeof s_late / sizeof s_late[0];
  return bEarly ? s_early : s_late;
}

CLink *Node(const PoolList &l, DWORD i) {
  return reinterpret_cast<CLink *>(static_cast<BYTE *>(l.pBase) + i * l.cbItem);
}

// Each list's count, then its slots.
int ListsSave(void *pFile, bool bEarly) {
  int nLists;
  const PoolList *pLists = Lists(bEarly, &nLists);
  std::vector<DWORD> v;
  for (int k = 0; k < nLists; k++) {
    const PoolList &l = pLists[k];
    const size_t nAt = v.size();
    v.push_back(0);
    for (CLink *p = *l.ppHead; p != NULL && v.size() - nAt <= static_cast<size_t>(*l.pnCount); p = p->pNext)
      v.push_back(static_cast<DWORD>((reinterpret_cast<BYTE *>(p) - static_cast<BYTE *>(l.pBase)) / l.cbItem));
    v[nAt] = static_cast<DWORD>(v.size() - nAt - 1);
  }
  const DWORD nBytes = static_cast<DWORD>(v.size() * sizeof(DWORD));
  return pfwrite(&nBytes, 4, pFile) && compress(v.data(), nBytes, pFile);
}

// The most a save's lists hold: each pool's slots once and each list's count; 1 MB at least,
// the limit before the pools passed 65,535.
DWORD ListsMostBytes() {
  const PoolSizes &p = g_pools;
  const size_t n = (static_cast<size_t>(p.units) + p.sprites + p.images + 2 * p.bullets + 4 * p.thingies + p.orders + 16) *
                   sizeof(DWORD);
  return static_cast<DWORD>(n > 0x100000 ? n : 0x100000);
}

int ListsRead(void *pFile, std::vector<DWORD> *pv) {
  DWORD nBytes = 0;
  if (!pfread(&nBytes, 4, pFile) || nBytes % 4 != 0 || nBytes > ListsMostBytes())
    return 0;
  pv->resize(nBytes / 4);
  return decompress(pv->data(), nBytes, pFile);
}

int ListsApply(const std::vector<DWORD> &v, bool bEarly) {
  int nLists;
  const PoolList *pLists = Lists(bEarly, &nLists);
  size_t at = 0;
  for (int k = 0; k < nLists; k++) {
    const PoolList &l = pLists[k];
    const DWORD nCount = static_cast<DWORD>(*l.pnCount);
    if (at >= v.size() || v[at] > nCount || at + 1 + v[at] > v.size())
      return 0;
    const DWORD nNodes = v[at++];
    for (DWORD j = 0; j < nNodes; j++)
      if (v[at + j] >= nCount)
        return 0;
    CLink *pPrev = NULL;
    *l.ppHead = NULL;
    for (DWORD j = 0; j < nNodes; j++) {
      CLink *p = Node(l, v[at + j]);
      p->pPrev = pPrev;
      p->pNext = NULL;
      if (pPrev)
        pPrev->pNext = p;
      else
        *l.ppHead = p;
      pPrev = p;
    }
    *l.ppTail = pPrev;
    at += nNodes;
  }
  return at == v.size();
}

// The AI's demands, which 1.17's load clears and a Hyperion load keeps
// (sai_read_banker_data). Each names what it was made for by a pointer of one of several
// types (a captain, a town, a request), whose low half alone the save keeps; here each is
// the region it points into (one of the AI's pools, or a nation's captains) and the offset.
struct DemandRef {
  DWORD dwRegion;  // 0 for none, 1 to 6 the pools below, 16 + n nation n's captains
  DWORD dwOffset;
};

const DWORD kCaptainRegion = 16;
const DWORD kCaptainsMax = AI_AREAS_117;  // captains_alloc's count

bool Region(DWORD dwRegion, BYTE **ppBase, size_t *pcb) {
  switch (dwRegion) {
    case 1: *ppBase = reinterpret_cast<BYTE *>(&gPeons_g); *pcb = sizeof gPeons_g; return true;
    case 2: *ppBase = reinterpret_cast<BYTE *>(&gBldgs_g); *pcb = sizeof gBldgs_g; return true;
    case 3: *ppBase = reinterpret_cast<BYTE *>(&gTowns_g); *pcb = sizeof gTowns_g; return true;
    case 4: *ppBase = reinterpret_cast<BYTE *>(&gSoldiers_g); *pcb = sizeof gSoldiers_g; return true;
    case 5: *ppBase = reinterpret_cast<BYTE *>(&gGuards_g); *pcb = sizeof gGuards_g; return true;
    case 6: *ppBase = reinterpret_cast<BYTE *>(&gScripts_g); *pcb = sizeof gScripts_g; return true;
  }
  if (dwRegion >= kCaptainRegion && dwRegion < kCaptainRegion + static_cast<DWORD>(PlayerLobbySlots())
      && sgCaptain[dwRegion - kCaptainRegion] != NULL) {
    *ppBase = reinterpret_cast<BYTE *>(sgCaptain[dwRegion - kCaptainRegion]);
    *pcb = kCaptainsMax * sizeof(TAI_Captain);
    return true;
  }
  return false;
}

DemandRef DemandEncode(DWORD_PTR p) {
  DemandRef r = {0, 0};
  if (p == 0)
    return r;
  for (DWORD dwRegion = 1; dwRegion < kCaptainRegion + static_cast<DWORD>(PlayerLobbySlots()); dwRegion++) {
    BYTE *pBase;
    size_t cb;
    if (Region(dwRegion, &pBase, &cb) && p >= reinterpret_cast<DWORD_PTR>(pBase)
        && p < reinterpret_cast<DWORD_PTR>(pBase) + cb) {
      r.dwRegion = dwRegion;
      r.dwOffset = static_cast<DWORD>(p - reinterpret_cast<DWORD_PTR>(pBase));
      return r;
    }
  }
  r.dwRegion = 0xFFFFFFFF;  // nowhere known: loaded as none
  return r;
}

DWORD_PTR DemandDecode(const DemandRef &r) {
  BYTE *pBase;
  size_t cb;
  if (r.dwRegion == 0 || !Region(r.dwRegion, &pBase, &cb) || r.dwOffset >= cb)
    return 0;
  return reinterpret_cast<DWORD_PTR>(pBase + r.dwOffset);
}

const int kDemands = sizeof(reinterpret_cast<AI_Banker *>(0)->aDemands) / sizeof(SAI_Demand);

int DemandsSave(void *pFile) {
  std::vector<DemandRef> v;
  for (int n = 0; n < PlayerLobbySlots(); n++)
    for (int i = 0; i < kDemands; i++)
      v.push_back(DemandEncode(gAIBanker[n].aDemands[i].dwCaptain));
  const DWORD nBytes = static_cast<DWORD>(v.size() * sizeof(DemandRef));
  return pfwrite(&nBytes, 4, pFile) && compress(v.data(), nBytes, pFile);
}

int DemandsLoad(void *pFile) {
  DWORD nBytes = 0;
  if (!pfread(&nBytes, 4, pFile) || nBytes != PlayerLobbySlots() * kDemands * sizeof(DemandRef))
    return 0;
  std::vector<DemandRef> v(nBytes / sizeof(DemandRef));
  if (!decompress(v.data(), nBytes, pFile))
    return 0;
  for (int n = 0; n < PlayerLobbySlots(); n++)
    for (int i = 0; i < kDemands; i++)
      gAIBanker[n].aDemands[i].dwCaptain = DemandDecode(v[n * kDemands + i]);
  return 1;
}

#define HX_COUNTERS(X)                                                                            \
  X(dword_69B560) X(dword_69B564) X(dword_69B568) X(dword_69B56C) X(gActiveNation) X(dword_685574) \
  X(dword_6A2E84) X(word_6A5D70) X(dword_6A5DA4) X(dword_6A5DC8) X(dword_6A5DEC) X(dword_6B1A94)     \
  X(dword_6B1A98) X(dword_6C46D4) X(sgwTickDelay) X(sgwLBDelay) X(sgwVerifyDelay)
#define HX_COUNTER_SAVE(g) && compress(&g, sizeof g, pFile)
#define HX_COUNTER_LOAD(g) && decompress(&g, sizeof g, pFile)

// A table's rows of a mod's types (h/playerlimits.h), in a game with them: their
// count, then each plane's rows of the game's types, as the table holds 3,840.
const int kTypeRows = PlayerTypeRows<UNIT_TYPES_117>::kCount;

template <class P>
int TypeRowsSave(void *pFile) {
  if (!g_hxModGame || P::kTypeCount == 0)
    return 1;
  const size_t cbRow = sizeof(*P::Extra()) * PLAYER_SLOTS_MAX;
  const int nPlanes = P::kTypeCount / (kTypeRows * PLAYER_SLOTS_MAX);
  const DWORD dwRows = static_cast<DWORD>(UnitTypesMod());
  if (!pfwrite(&dwRows, sizeof dwRows, pFile))
    return 0;
  for (int i = 0; i < nPlanes && dwRows != 0; i++)
    if (!compress(reinterpret_cast<BYTE *>(P::TypeRows()) + cbRow * kTypeRows * i,
                  static_cast<int>(cbRow * dwRows), pFile))
      return 0;
  return 1;
}

template <class P>
int TypeRowsLoad(void *pFile) {
  if (!g_hxModGame || P::kTypeCount == 0)
    return 1;
  const size_t cbRow = sizeof(*P::Extra()) * PLAYER_SLOTS_MAX;
  const int nPlanes = P::kTypeCount / (kTypeRows * PLAYER_SLOTS_MAX);
  DWORD dwRows;
  if (!pfread(&dwRows, sizeof dwRows, pFile) || dwRows > static_cast<DWORD>(kTypeRows))
    return 0;
  for (int i = 0; i < nPlanes && dwRows != 0; i++)
    if (!decompress(reinterpret_cast<BYTE *>(P::TypeRows()) + cbRow * kTypeRows * i,
                    static_cast<int>(cbRow * dwRows), pFile))
      return 0;
  return 1;
}

// A player table 1.17's save leaves out, in a game with slots past 11 with its extra
// entries, and with a mod's types with their rows: the trigger counts, the banker's staged
// costs, the AI's regions and two of the captains' tables.
template <class P>
int TableSave(const P &t, void *pFile) {
  return compress(t.a, sizeof t.a, pFile)
      && (!g_hxPlayers || compress(P::Extra(), static_cast<int>(sizeof(*P::Extra()) * P::kExtraCount), pFile))
      && TypeRowsSave<P>(pFile);
}

template <class P>
int TableLoad(P &t, void *pFile) {
  return decompress(t.a, sizeof t.a, pFile)
      && (!g_hxPlayers || decompress(P::Extra(), static_cast<int>(sizeof(*P::Extra()) * P::kExtraCount), pFile))
      && TypeRowsLoad<P>(pFile);
}

int CountersSave(void *pFile) {
  return 1 HX_COUNTERS(HX_COUNTER_SAVE) && TableSave(banker_staging_orecost_g, pFile)
      && TableSave(banker_staging_gascost_g, pFile) && TableSave(banker_staging_supplycost_g, pFile)
      && TableSave(sgRegions, pFile) && TableSave(dword_6B68C0, pFile) && TableSave(dword_6B68E0, pFile);
}

int CountersLoad(void *pFile) {
  return 1 HX_COUNTERS(HX_COUNTER_LOAD) && TableLoad(banker_staging_orecost_g, pFile)
      && TableLoad(banker_staging_gascost_g, pFile) && TableLoad(banker_staging_supplycost_g, pFile)
      && TableLoad(sgRegions, pFile) && TableLoad(dword_6B68C0, pFile) && TableLoad(dword_6B68E0, pFile);
}

// The cargo lists of a game of wide unit ids, whose 1.17 records keep each id's low half
// (port_formats.cpp): each slot's eight ids, as the table has them.
int CargoSave(void *pFile) {
  std::vector<DWORD> v(static_cast<size_t>(g_pools.units) * 8);
  for (int i = 0; i < g_pools.units; i++)
    for (int k = 0; k < 8; k++)
      v[static_cast<size_t>(i) * 8 + k] = sgUnitsMem[i].wCargoUnitIds[k];
  return compress(v.data(), static_cast<int>(v.size() * sizeof(DWORD)), pFile);
}

int CargoLoad(void *pFile) {
  std::vector<DWORD> v(static_cast<size_t>(g_pools.units) * 8);
  if (!decompress(v.data(), static_cast<int>(v.size() * sizeof(DWORD)), pFile))
    return 0;
  for (int i = 0; i < g_pools.units; i++)
    for (int k = 0; k < 8; k++)
      sgUnitsMem[i].wCargoUnitIds[k] = v[static_cast<size_t>(i) * 8 + k];
  return 1;
}

// The high bytes of the ids the x64 build keeps in 16 bits where 1.17's records have a byte
// (h/typelimits.h: a mod's orders, weapons, techs and upgrades past 255), and the high halves
// of the images' offsets into their scripts (a mod's past 64 KiB), which the
// records' sections drop (port_formats.cpp): the units', the bullets', the orders' and the
// images', each run's count and then its bytes, in a section of their own when any is not 0
//.
struct WideRun {
  unsigned int layout;
  void *pRecs;
  DWORD dwCount;
};

const int kWideRuns = 4;

void WideRuns(WideRun (&runs)[kWideRuns]) {
  runs[0] = {SAVE117_CUnit, sgUnitsMem, static_cast<DWORD>(g_pools.units)};
  runs[1] = {SAVE117_CBullet, sgBullets, static_cast<DWORD>(g_pools.bullets)};
  runs[2] = {SAVE117_COrder, sgOrders, static_cast<DWORD>(g_pools.orders)};
  runs[3] = {SAVE117_CImage, sgImages, static_cast<DWORD>(g_pools.images)};
}

int WideIdsSave(void *pFile) {
  WideRun runs[kWideRuns];
  WideRuns(runs);
  std::vector<BYTE> v;
  for (const WideRun &r : runs) {
    const size_t at = v.size();
    v.resize(at + static_cast<size_t>(PortSave117HighCount(r.layout)) * r.dwCount);
    PortSave117HighGet(r.layout, r.pRecs, r.dwCount, v.data() + at);
  }
  bool bAny = false;
  for (BYTE b : v)
    bAny = bAny || b != 0;
  const DWORD adwCounts[kWideRuns] = {runs[0].dwCount, runs[1].dwCount, runs[2].dwCount, runs[3].dwCount};
  return !bAny || (PortSaveSection(pFile, kWideIds) && pfwrite(adwCounts, sizeof adwCounts, pFile)
                   && compress(v.data(), static_cast<int>(v.size()), pFile));
}

int WideIdsLoad(void *pFile) {
  const int nHave = PortLoadSectionIf(pFile, kWideIds);
  if (nHave <= 0)
    return nHave == 0;
  WideRun runs[kWideRuns];
  WideRuns(runs);
  DWORD adwCounts[kWideRuns];
  size_t cb = 0;
  if (!pfread(adwCounts, sizeof adwCounts, pFile))
    return 0;
  for (int i = 0; i < kWideRuns; i++) {
    if (adwCounts[i] != runs[i].dwCount)
      return 0;
    cb += static_cast<size_t>(PortSave117HighCount(runs[i].layout)) * runs[i].dwCount;
  }
  std::vector<BYTE> v(cb);
  if (cb == 0 || !decompress(v.data(), static_cast<int>(cb), pFile))
    return 0;
  size_t at = 0;
  for (const WideRun &r : runs) {
    PortSave117HighSet(r.layout, r.pRecs, r.dwCount, v.data() + at);
    at += static_cast<size_t>(PortSave117HighCount(r.layout)) * r.dwCount;
  }
  return 1;
}

// Each slot's state of a mod's techs and upgrades past 1.17's rows (playertables.h), in a game
// that has them.
int ModTechSave(void *pFile) {
  if (g_hxTypes.techs <= TECH_TYPES_117 && g_hxTypes.upgrades <= UPGRADE_TYPES_117)
    return 1;
  return PortSaveSection(pFile, kModTech) && PlayerModTechSave(pFile);
}

int ModTechLoad(void *pFile) {
  const int nHave = PortLoadSectionIf(pFile, kModTech);
  return nHave <= 0 ? nHave == 0 : PlayerModTechLoad(pFile);
}

const PortLogger Log("hyperion_save.log");

}  // namespace

const char *HxSaveTarget() {
  return g_pszTarget;
}

int HxSaveReplay() {
  char szPath[MAX_PATH];
  const DWORD n = GetTempPathA(MAX_PATH - 32, szPath);
  if (n == 0 || n > MAX_PATH - 32)
    return 0;
  lstrcatA(szPath, "hyperion-replayed-save.tmp");
  g_pszTarget = szPath;
  const BOOL bOk = saveload_write_save_data(0, "replayed", 0);
  g_pszTarget = NULL;
  return bOk;
}

bool HxSaveNeeded() {
  return HxRules();
}

int HxSaveSetUp(void *pFile) {
  if (!HxSaveNeeded())
    return 1;
  BYTE abRule[256];
  if (ReplayHxRuleSize() > sizeof abRule)
    return 0;
  ReplayHxRuleMake(abRule);
  const DWORD cbRule = ReplayHxRuleSize();
  const DWORD dwPlayers = g_hxPlayers ? 1 : 0;
  if (!PortSaveSection(pFile, kSetUp) || !pfwrite(&cbRule, 4, pFile) || !pfwrite(abRule, cbRule, pFile)
      || !pfwrite(&dwPlayers, 4, pFile) || (dwPlayers && !PlayersSaveSetUp(pFile)) || !ListsSave(pFile, true))
    return 0;
  BYTE abRgb[PLAYER_SLOTS_MAX][4] = {};
  bool bAny = false;
  for (int s = 0; s < PLAYER_SLOTS_MAX; s++) {
    abRgb[s][0] = static_cast<BYTE>(PortTeamColorGet(s, &abRgb[s][1]));
    bAny = bAny || abRgb[s][0] != 0;
  }
  const DWORD dwSlots = PLAYER_SLOTS_MAX;
  if (bAny && !(PortSaveSection(pFile, kColours) && pfwrite(&dwSlots, 4, pFile) && pfwrite(abRgb, sizeof abRgb, pFile)))
    return 0;
  BYTE abRaces[PLAYER_SLOTS_MAX];
  for (int s = 0; s < PLAYER_SLOTS_MAX; s++)
    abRaces[s] = static_cast<BYTE>(ModRaceOf(s));
  if (ModRacesAny() && !(PortSaveSection(pFile, kRaces) && pfwrite(&dwSlots, 4, pFile) && pfwrite(abRaces, sizeof abRaces, pFile)))
    return 0;
  BYTE abLevels[PLAYER_SLOTS_MAX * AI_PROFILE_BYTES];
  AiProfilesSave(abLevels);
  if ((g_hxRuleOptions & HX_OPT_AI_LEVELS) != 0 &&
      !(PortSaveSection(pFile, kAiLevels) && pfwrite(&dwSlots, 4, pFile) && pfwrite(abLevels, sizeof abLevels, pFile)))
    return 0;
  BYTE abHandicaps[PLAYER_SLOTS_MAX];
  HandicapsSave(abHandicaps);
  if ((g_hxRuleOptions & HX_OPT_HANDICAP) != 0 &&
      !(PortSaveSection(pFile, kHandicaps) && pfwrite(&dwSlots, 4, pFile) &&
        pfwrite(abHandicaps, sizeof abHandicaps, pFile)))
    return 0;
  BYTE abMutators[MUTATOR_RECORD_BYTES];
  MutatorsSave(abMutators);
  if ((g_hxRuleOptions & HX_OPT_MUTATORS) != 0 &&
      !(PortSaveSection(pFile, kMutators) && pfwrite(abMutators, sizeof abMutators, pFile)))
    return 0;
  const DWORD dwUnits = static_cast<DWORD>(PoolsUnitsToRecord());
  return dwUnits == 0 || (PortSaveSection(pFile, kUnits) && pfwrite(&dwUnits, 4, pFile));
}

void HxSaveColorsApply() {
  for (int s = 0; g_bLoadRgb && s < PLAYER_SLOTS_MAX; s++)
    if (g_abLoadRgb[s][0] != 0)
      PortTeamColorSet(s, g_abLoadRgb[s][1], g_abLoadRgb[s][2], g_abLoadRgb[s][3], FALSE);
  g_bLoadRgb = false;
}

int HxLoadSetUp(void *pFile) {
  g_pending = false;
  g_earlyPending = false;
  g_loading = false;
  g_bLoadRgb = false;
  ModRacesRecorded(NULL, 0);
  AiProfilesRecorded(NULL, 0);
  HandicapsRecorded(NULL, 0);
  MutatorsRecorded(NULL, 0);
  PoolsUnitsRecorded(0);
  const int nHave = PortLoadSectionIf(pFile, kSetUp);
  if (nHave <= 0) {
    PlayersLoadNone();  // a 1.17 save: 1.17's slots, whatever the last map had,
    RulesRecordedClassic();  // and classic rules
    return nHave == 0;
  }
  BYTE abRule[256] = {};
  DWORD cbRule = 0, dwPlayers = 0;
  if (!pfread(&cbRule, 4, pFile) || !ReplayHxRuleSizeKnown(cbRule) || cbRule > sizeof abRule
      || !pfread(abRule, cbRule, pFile))
    return 0;
  if (const char *pszWhy = ReplayHxRuleRefusal(abRule, cbRule)) {
    Log("refused: %s", pszWhy);
    return 0;
  }
  ReplayHxRuleApply(abRule, cbRule);
  if (!pfread(&dwPlayers, 4, pFile))
    return 0;
  if (!dwPlayers)
    PlayersLoadNone();
  else if (!PlayersLoadSetUp(pFile))
    return 0;
  if (!ListsRead(pFile, &g_early))
    return 0;
  const int nRgb = PortLoadSectionIf(pFile, kColours);
  DWORD dwSlots = 0;
  if (nRgb < 0 || (nRgb > 0 && (!pfread(&dwSlots, 4, pFile) || dwSlots != PLAYER_SLOTS_MAX
                                 || !pfread(g_abLoadRgb, sizeof g_abLoadRgb, pFile))))
    return 0;
  g_bLoadRgb = nRgb > 0;
  const int nRaces = PortLoadSectionIf(pFile, kRaces);
  BYTE abRaces[PLAYER_SLOTS_MAX];
  if (nRaces < 0 || (nRaces > 0 && (!pfread(&dwSlots, 4, pFile) || dwSlots != PLAYER_SLOTS_MAX
                                     || !pfread(abRaces, sizeof abRaces, pFile))))
    return 0;
  if (nRaces > 0)
    ModRacesRecorded(abRaces, PLAYER_SLOTS_MAX);
  const int nLevels = PortLoadSectionIf(pFile, kAiLevels);
  BYTE abLevels[PLAYER_SLOTS_MAX * AI_PROFILE_BYTES];
  if (nLevels < 0 || (nLevels > 0 && (!pfread(&dwSlots, 4, pFile) || dwSlots != PLAYER_SLOTS_MAX
                                       || !pfread(abLevels, sizeof abLevels, pFile))))
    return 0;
  if (nLevels > 0)
    AiProfilesRecorded(abLevels, PLAYER_SLOTS_MAX);
  const int nHandicaps = PortLoadSectionIf(pFile, kHandicaps);
  BYTE abHandicaps[PLAYER_SLOTS_MAX];
  if (nHandicaps < 0 || (nHandicaps > 0 && (!pfread(&dwSlots, 4, pFile) || dwSlots != PLAYER_SLOTS_MAX
                                             || !pfread(abHandicaps, sizeof abHandicaps, pFile))))
    return 0;
  if (nHandicaps > 0)
    HandicapsRecorded(abHandicaps, PLAYER_SLOTS_MAX);
  const int nMutators = PortLoadSectionIf(pFile, kMutators);
  BYTE abMutators[MUTATOR_RECORD_BYTES];
  // 0.6.9 and its first builds wrote shorter records
  const int nMutatorBytes = nMutators > 0 ? PortLoadSectionBytes() : 0;
  if (nMutators < 0 ||
      (nMutators > 0 && (!MutatorRecordBytesRead(nMutatorBytes) || !pfread(abMutators, nMutatorBytes, pFile))))
    return 0;
  if (nMutators > 0)
    MutatorsRecorded(abMutators, nMutatorBytes);
  const int nUnits = PortLoadSectionIf(pFile, kUnits);
  DWORD dwUnits = 0;
  if (nUnits < 0 || (nUnits > 0 && !pfread(&dwUnits, 4, pFile)))
    return 0;
  PoolsUnitsRecorded(dwUnits > POOL_UNITS_MAX ? POOL_UNITS_MAX : static_cast<int>(dwUnits));
  g_earlyPending = true;
  g_loading = true;
  return 1;
}

bool HxSaveLoading() {
  return g_loading;
}

int HxSaveBeforePsiLinks() {
  if (!g_earlyPending)
    return 1;
  g_earlyPending = false;
  return ListsApply(g_early, true);
}

int HxSaveState(void *pFile) {
  if (!HxSaveNeeded())
    return 1;
  StateHead head = {};
  head.dwVision = g_hxPlayers ? 1 : 0;
  head.dwRandState = sgNetRandState_g;
  head.dwRandCount = static_cast<DWORD>(sgNetRandCount_g);
  head.dwCheckDelay = sgwCheckDelay;
  head.dwCacheKind = sgbCachedEnum_g | sgbCachedregion_g << 8;
  head.dwFogCountdown = dword_6663E4;
  head.adwOrderSeq[0] = sgdwOrderSeqDelay_g;
  head.adwOrderSeq[1] = sgdwBackgroundOrderSeqDelay_g;
  head.dwCrtRand = *PortCrtRandState();
  SyncHashCompute(&head.hash);
  Dump("saved");
  return PortSaveSection(pFile, kState) && pfwrite(&head, sizeof head, pFile)
      && (!g_hxPlayers || PlayerTablesSave(pFile)) && TypeRowsSave<PlayerUnitCountsGrid>(pFile)
      && (!head.dwVision || VisionHxSave(pFile))
      && TableSave(sglCachedGameCounts_g, pFile) && TableSave(sglCachedUnitCounts_g, pFile)
      && TableSave(sglCachedCompletedCounts_g, pFile) && ListsSave(pFile, false) && DemandsSave(pFile)
      && CountersSave(pFile) && (!PoolsWideIds() || CargoSave(pFile)) && (!SelectionWide() || SelectionSave(pFile))
      && (!RichBasesInGame() || RichBasesSave(pFile)) && WideIdsSave(pFile) && ModTechSave(pFile);
}

int HxLoadState(void *pFile) {
  const int nHave = PortLoadSectionIf(pFile, kState);
  if (nHave <= 0)
    return nHave == 0;
  StateHead head;
  std::vector<DWORD> late;
  if (!pfread(&head, sizeof head, pFile) || (g_hxPlayers && !PlayerTablesLoad(pFile))
      || !TypeRowsLoad<PlayerUnitCountsGrid>(pFile) || (head.dwVision && !VisionHxLoad(pFile)) || !TableLoad(sglCachedGameCounts_g, pFile)
      || !TableLoad(sglCachedUnitCounts_g, pFile) || !TableLoad(sglCachedCompletedCounts_g, pFile)
      || !ListsRead(pFile, &late) || !ListsApply(late, false) || !DemandsLoad(pFile) || !CountersLoad(pFile)
      || (PoolsWideIds() && !CargoLoad(pFile)) || (SelectionWide() && !SelectionLoad(pFile))
      || (RichBasesInGame() && !RichBasesLoad(pFile)) || !WideIdsLoad(pFile) || !ModTechLoad(pFile))
    return 0;
  sgwCheckDelay = static_cast<WORD>(head.dwCheckDelay);
  sgdwOrderSeqDelay_g = head.adwOrderSeq[0];
  sgdwBackgroundOrderSeqDelay_g = head.adwOrderSeq[1];
  sgbCachedEnum_g = static_cast<BYTE>(head.dwCacheKind);
  sgbCachedregion_g = static_cast<BYTE>(head.dwCacheKind >> 8);
  g_saved = head;
  g_pending = true;
  return 1;
}

bool HxSaveGoesOn() {
  return g_goesOn;
}

void HxSaveRunGame() {
  if (g_goesOn)
    dword_6663E4 = g_saved.dwFogCountdown;
}

void HxSaveLoaded() {
  g_loading = false;
  g_goesOn = g_pending;
  if (!g_pending)
    return;
  g_pending = false;
  sgNetRandState_g = g_saved.dwRandState;
  sgNetRandCount_g = static_cast<int>(g_saved.dwRandCount);
  *PortCrtRandState() = g_saved.dwCrtRand;
  Dump("loaded");
  if (g_saved.hash.dwVersion != SYNC_HASH_VERSION) {
    Log("loaded at frame %lu: saved with hash version %lu, not compared", static_cast<ULONG>(g_saved.hash.dwFrame),
        static_cast<ULONG>(g_saved.hash.dwVersion));
    return;
  }
  SyncHash now;
  SyncHashCompute(&now);
  DWORD dwDomains = 0;
  for (int d = 0; d < SYNC_DOMAINS; d++)
    if (now.aqDomain[d] != g_saved.hash.aqDomain[d])
      dwDomains |= 1u << d;
  if (dwDomains)
    Log("loaded at frame %lu: the state differs from the game saved (domains %#lx: units 1, objects 2, "
        "players 4, map 8, sprites 16, ai 32)", static_cast<ULONG>(g_saved.hash.dwFrame),
        static_cast<ULONG>(dwDomains));
  else
    Log("loaded at frame %lu: the state is the game saved", static_cast<ULONG>(g_saved.hash.dwFrame));
}
