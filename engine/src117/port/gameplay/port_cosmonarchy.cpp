// port_cosmonarchy.cpp -- the Cosmonarchy mod's rules (port_cosmonarchy.h).
//
// The mod's gameplay changes are ported from gptp-fork one system at a time; every number
// and every list carries its anchor. Anything not yet ported is noted where it belongs.
#include <string.h>

#include "port_cosmonarchy.h"
#include "../../h/platform.h"
#include "../../h/CUnit.h"
#include "../../h/poollimits.h"        // PoolSizes, g_pools
#include "../../h/saveload.h"          // compress, decompress, pfread, pfwrite
#include "../../h/playertables.h"      // PLAYER_SLOTS_MAX
#include "../../h/gametype.h"          // GameHeader, gGameHeader
#include "../../h/creep.h"             // cover_creep_at, tile_can_have_creep
#include "../../h/tile2.h"             // the tiles' helpers
#include "../../h/net_rand.h"          // net_randbetween
#include "../../h/CUnitTSpells.h"      // net_randbetween's declaration
#include "../../h/CUnitInit.h"         // CreateUnitAt
#include "../../h/CUnitZerg.h"         // FindCountLarvae
#include "../../h/CUnitFind.h"         // find_first_unit_near
#include "../../h/CSprite.h"           // the sprite fields the hooks read
#include "../../h/net_cmds.h"          // PlayerData

// The larva-count walk CUnitZerg.cpp declares for itself, so a game's egg cancel can
// ask it too.
extern "C" CUnit *__fastcall find_first_unit_near(int x, int y, int nRadius,
                                                  int (__fastcall *pfnFilter)(void *, intptr_t),
                                                  intptr_t nContext);
#include "../../gen/globals117.h"      // gwMapWidth, gwMapHeight
#include "../data/port_moddata.h"      // ModDataId, g_hxModGame, ModTypeByName

// ---------------------------------------------------------------------------------------
// The gate and the name table.

extern GameHeader gGameHeader;  // gametype.cpp; the game's type byte decides its mode

bool PortCosmonarchy(void)
{
  return g_hxModGame && strcmp(ModDataId(), "cosmonarchy") == 0;
}

unsigned CosmoUnit(const char *pszName)
{
  const int n = ModTypeByName("units", pszName);
  return n < 0 ? 0xFFFFu : static_cast<unsigned>(n);
}

// ---------------------------------------------------------------------------------------
// The per-unit mod state (gptp-fork's UnitExt timers that the sim reads).

static ModUnitState *s_pState = nullptr;
static unsigned s_nState = 0;
static ModPlayerState s_aPlayers[PLAYER_SLOTS_MAX];

static ModUnitState s_none;

ModUnitState &ModUnitStateOf(CUnit *pUnit)
{
  const unsigned nSlot = static_cast<unsigned>(pUnit - CUnit::Head());
  if (!s_pState || static_cast<unsigned>(g_pools.units) != s_nState) {
    delete[] s_pState;
    s_nState = static_cast<unsigned>(g_pools.units);
    s_pState = new ModUnitState[s_nState];
    memset(s_pState, 0xFF, sizeof(ModUnitState) * s_nState);  // out of combat, as the mod's
  }
  return nSlot < s_nState ? s_pState[nSlot] : s_none;
}

void ModUnitStateInit(void)
{
  if (s_pState && s_nState)
    memset(s_pState, 0xFF, sizeof(ModUnitState) * s_nState);
  memset(s_aPlayers, 0, sizeof s_aPlayers);
  CosmoForgetGame();
}

const ModUnitState *ModUnitStates(unsigned *pnCount)
{
  *pnCount = s_nState;
  return s_pState;
}

const ModPlayerState *ModPlayerStates(unsigned *pnCount)
{
  *pnCount = PLAYER_SLOTS_MAX;
  return s_aPlayers;
}

ModPlayerState &ModPlayerStateOf(int nSlot)
{
  static ModPlayerState s_none;
  return nSlot >= 0 && nSlot < PLAYER_SLOTS_MAX ? s_aPlayers[nSlot] : s_none;
}

int ModUnitStateSave(void *pFile)
{
  const DWORD adw[1] = {static_cast<DWORD>(s_nState)};
  if (pfwrite(adw, sizeof adw, pFile) == 0)
    return 0;
  if (s_nState != 0 &&
      compress(s_pState, static_cast<int>(sizeof(ModUnitState) * s_nState), pFile) == 0)
    return 0;
  return compress(s_aPlayers, static_cast<int>(sizeof s_aPlayers), pFile) != 0;
}

int ModUnitStateLoad(void *pFile)
{
  DWORD dwCount;
  if (pfread(&dwCount, sizeof dwCount, pFile) == 0 ||
      dwCount > static_cast<DWORD>(g_pools.units))
    return 0;
  if (dwCount == 0) {
    memset(s_aPlayers, 0, sizeof s_aPlayers);
    return 1;
  }
  if (!s_pState || static_cast<unsigned>(g_pools.units) != s_nState) {
    delete[] s_pState;
    s_nState = static_cast<unsigned>(g_pools.units);
    s_pState = new ModUnitState[s_nState];
  }
  memset(s_pState, 0xFF, sizeof(ModUnitState) * s_nState);
  if (decompress(s_pState, static_cast<int>(sizeof(ModUnitState) * dwCount), pFile) == 0)
    return 0;
  return decompress(s_aPlayers, static_cast<int>(sizeof s_aPlayers), pFile) != 0;
}

// ---------------------------------------------------------------------------------------
// The vespene rework (gptp-fork hooks/harvest.cpp).

// Mineral fields never die: a full trip carries 5, then the field floors at its depleted
// amount and keeps yielding 2 (classic's reduced final trip) at the slower 90-frame timer.
// Gas stays { 3, 5, 7, 9 } by tier, as systems/triggers/trigger_system.h:170 gives it.
static const BYTE s_yields[6] = {2, 5, 3, 5, 7, 9};

int CosmoYield(int nKind)
{
  return nKind >= 0 && nKind < 6 ? s_yields[nKind] : 0;
}

// get_gas_tier, hooks/harvest.cpp:22-47.
static unsigned s_nRidge;        // ResourceVespeneRidge -> tier 1
static unsigned s_nGeyser;       // ResourceVespeneGeyser -> tier 2
static unsigned s_nT2a, s_nT2b, s_nT2c, s_nT2d;  // Sieve, Cedent, Eskigant, Arch -> 2
static unsigned s_nT3a, s_nT3b, s_nT3c, s_nT3d;  // Reservoir, Aquifer, Excisant, Abutment -> 3
static unsigned s_nT4a, s_nT4b;  // Wellbore, Ezigrisant -> 4
static bool s_bResolved = false;

static void CosmoResolve(void)
{
  if (s_bResolved)
    return;
  s_bResolved = true;
  s_nRidge = CosmoUnit("vespene_ridge");
  s_nGeyser = CosmoUnit("vespene_geyser");
  s_nT2a = CosmoUnit("terran_sieve");
  s_nT2b = CosmoUnit("protoss_cedent");
  s_nT2c = CosmoUnit("zerg_eskigant");
  s_nT2d = CosmoUnit("askosi_arch");
  s_nT3a = CosmoUnit("terran_reservoir");
  s_nT3b = CosmoUnit("protoss_aquifer");
  s_nT3c = CosmoUnit("zerg_excisant");
  s_nT3d = CosmoUnit("askosi_abutment");
  s_nT4a = CosmoUnit("terran_wellbore");
  s_nT4b = CosmoUnit("zerg_ezigrisant");
}

int CosmoGasTier(unsigned wType)
{
  CosmoResolve();
  if (wType == s_nRidge)
    return 1;
  if (wType == s_nGeyser || wType == s_nT2a || wType == s_nT2b ||
      wType == s_nT2c || wType == s_nT2d)
    return 2;
  if (wType == s_nT3a || wType == s_nT3b || wType == s_nT3c || wType == s_nT3d)
    return 3;
  if (wType == s_nT4a || wType == s_nT4b)
    return 4;
  return 0;
}

// cm_ids.cpp: the neutral nodes (uncapped) and the refineries (capped).
bool CosmoUncappedGasNode(unsigned wType)
{
  CosmoResolve();
  return wType == s_nRidge || wType == s_nGeyser;
}

bool CosmoCappedGasNode(unsigned wType)
{
  CosmoResolve();
  return wType == s_nT2a || wType == s_nT2b || wType == s_nT2c || wType == s_nT2d ||
         wType == s_nT3a || wType == s_nT3b || wType == s_nT3c || wType == s_nT3d ||
         wType == s_nT4a || wType == s_nT4b;
}

// unit_is_geyser_ex, hooks/harvest.cpp:172-194: the neutral nodes always, a refinery when
// it is completed and the worker's own or a mutual ally's.
bool CosmoGeyserMineable(const CUnit *pNode, const CUnit *pWorker)
{
  if (CosmoUncappedGasNode(pNode->wType))
    return true;
  if (!CosmoCappedGasNode(pNode->wType))
    return false;
  if (!(pNode->dwFlags & 1))
    return false;
  const BYTE a = pNode->bPlayer, b = pWorker->bPlayer;
  return a == b || (sgbAlliances_g[a][b] != 0 && sgbAlliances_g[b][a] != 0);
}

// get_harvest_amount, hooks/harvest.cpp:49-70.
int CosmoHarvestAmount(const CUnit *pNode)
{
  switch (pNode->wType) {
    case 0xB0: case 0xB1: case 0xB2:
      return pNode->wResourceAmount > CosmoYield(0) ? CosmoYield(1) : CosmoYield(0);
  }
  switch (CosmoGasTier(pNode->wType)) {
    case 1: return CosmoYield(2);
    case 2: return CosmoYield(3);
    case 3: return CosmoYield(4);
    case 4: return CosmoYield(5);
  }
  return 0;
}

// ---------------------------------------------------------------------------------------
// Energy (gptp-fork hooks/unit_stats/max_energy.cpp and update_unit_state.cpp).

static unsigned s_nE50a, s_nE50b, s_nE75, s_nE100[9], s_nE125[3], s_nE150[6],
    s_nE175, s_nE250[5];
static unsigned s_nVagrantShade, s_nPlasmaShell, s_nGolden[5], s_nForerunner[7];
static unsigned s_nSpreader[11], s_nSpreaderDone[5], s_nLarvaSpawn[9];
static unsigned s_nLarva, s_nOcsalLarva;

static void CosmoResolveUnits(void)
{
  static bool s_bResolvedUnits = false;
  if (s_bResolvedUnits)
    return;
  s_bResolvedUnits = true;

  // max_energy.cpp: 50, 75, 100, 125, 150, 175, 250; the rest (200) need no row.
  s_nE50a = CosmoUnit("askosi_eyelet");      s_nE50b = CosmoUnit("askosi_vicar");
  s_nE75 = CosmoUnit("askosi_zonguard");
  s_nE100[0] = CosmoUnit("terran_piety_ward");  s_nE100[1] = CosmoUnit("terran_autocrat");
  s_nE100[2] = CosmoUnit("terran_phobos");      s_nE100[3] = CosmoUnit("terran_facade");
  s_nE100[4] = CosmoUnit("protoss_demiurge");   s_nE100[5] = CosmoUnit("askosi_cardinal");
  s_nE100[6] = CosmoUnit("askosi_rove");        s_nE100[7] = CosmoUnit("askosi_elgird");
  s_nE100[8] = CosmoUnit("askosi_histrion");
  s_nE125[0] = CosmoUnit("terran_ancile");      s_nE125[1] = CosmoUnit("protoss_xenolith");
  s_nE125[2] = CosmoUnit("askosi_paraclete");
  s_nE150[0] = CosmoUnit("terran_apostle");     s_nE150[1] = CosmoUnit("terran_hypnagogue");
  s_nE150[2] = CosmoUnit("terran_seraph");      s_nE150[3] = CosmoUnit("terran_luminary_golden");
  s_nE150[4] = CosmoUnit("protoss_analogue");   s_nE150[5] = CosmoUnit("askosi_proselyte");
  s_nE175 = CosmoUnit("terran_triton");
  s_nE250[0] = CosmoUnit("terran_archbishop");  s_nE250[1] = CosmoUnit("protoss_cantavis");
  s_nE250[2] = CosmoUnit("protoss_saladius");   s_nE250[3] = CosmoUnit("tassadar");
  s_nE250[4] = CosmoUnit("francisco_goska");

  // update_unit_state.cpp: the shades and the golden units, and the plasma shell.
  s_nVagrantShade = CosmoUnit("protoss_vagrant_shade");
  s_nPlasmaShell = CosmoUnit("protoss_plasma_shell_deprecated");
  s_nGolden[0] = CosmoUnit("terran_maverick_golden");
  s_nGolden[1] = CosmoUnit("terran_harakan_golden");
  s_nGolden[2] = CosmoUnit("terran_aspirant");
  s_nGolden[3] = CosmoUnit("terran_blackjack_golden");
  s_nGolden[4] = CosmoUnit("terran_luminary_golden");
  // cm_ids.cpp:2011-2019, the forerunners whose flash shielding heals twice as fast.
  s_nForerunner[0] = CosmoUnit("forerunner_athla");
  s_nForerunner[1] = CosmoUnit("forerunner_ferrum");
  s_nForerunner[2] = CosmoUnit("forerunner_chalyb");
  s_nForerunner[3] = CosmoUnit("forerunner_circitor");
  s_nForerunner[4] = CosmoUnit("forerunner_exigo");
  s_nForerunner[5] = CosmoUnit("forerunner_vicere");
  s_nForerunner[6] = CosmoUnit("forerunner_machinamentum");

  // larva_creep_spawn.cpp:216-240, the cagra spreaders, the always-list first.
  s_nSpreader[0] = CosmoUnit("zerg_caphrolosk");
  s_nSpreader[1] = CosmoUnit("zerg_gathtelosk");
  s_nSpreader[2] = CosmoUnit("zerg_spraith_cesiant");
  s_nSpreader[3] = CosmoUnit("zerg_surkith_cesiant");
  s_nSpreader[4] = CosmoUnit("zerg_larvosk_cesiant");
  s_nSpreader[5] = CosmoUnit("zerg_skortrith_cesiant");
  s_nSpreader[6] = CosmoUnit("zerg_spirtith_cesiant");
  s_nSpreader[7] = CosmoUnit("zerg_nydus_cesiant");
  s_nSpreader[8] = CosmoUnit("zerg_kazitrith_cesiant");
  s_nSpreader[9] = CosmoUnit("zerg_ocsal_cesiant");
  s_nSpreader[10] = CosmoUnit("zerg_overmind_with_shell_the_fall");
  s_nSpreaderDone[0] = CosmoUnit("zerg_hachirosk");
  s_nSpreaderDone[1] = CosmoUnit("zerg_cagrant_cesiant");
  s_nSpreaderDone[2] = CosmoUnit("zerg_cerebrate_the_fall");
  s_nSpreaderDone[3] = CosmoUnit("zerg_overmind_the_fall");
  s_nSpreaderDone[4] = CosmoUnit("zerg_zytryleth_zorthos");

  // unit_id_spawns_larva_by_order, cm_ids.cpp:1206-1216.
  s_nLarvaSpawn[0] = CosmoUnit("zerg_hachirosk");
  s_nLarvaSpawn[1] = CosmoUnit("zerg_caphrolosk");
  s_nLarvaSpawn[2] = CosmoUnit("zerg_gathtelosk");
  s_nLarvaSpawn[3] = CosmoUnit("zerg_larvosk_cesiant");
  s_nLarvaSpawn[4] = CosmoUnit("zerg_ocsal_cesiant");
  s_nLarvaSpawn[5] = CosmoUnit("zerg_cerebrate_the_fall");
  s_nLarvaSpawn[6] = CosmoUnit("zerg_overmind_with_shell_the_fall");
  s_nLarvaSpawn[7] = CosmoUnit("zerg_overmind_the_fall");
  s_nLarvaSpawn[8] = CosmoUnit("zerg_zytryleth_zorthos");

  s_nLarva = CosmoUnit("zerg_larva");
  s_nOcsalLarva = CosmoUnit("zerg_ocsal_larva");
}

static bool CosmoOneOf(unsigned wType, const unsigned *p, int n)
{
  for (int i = 0; i < n; i++)
    if (wType == p[i])
      return true;
  return false;
}

// get_unit_max_energy, hooks/unit_stats/max_energy.cpp:7-59, x256.
int CosmoMaxEnergy(const CUnit *pUnit)
{
  CosmoResolveUnits();
  const unsigned t = pUnit->wType;
  if (t == s_nE50a || t == s_nE50b)
    return 50 << 8;
  if (t == s_nE75)
    return 75 << 8;
  if (CosmoOneOf(t, s_nE100, 9))
    return 100 << 8;
  if (CosmoOneOf(t, s_nE125, 3))
    return 125 << 8;
  if (CosmoOneOf(t, s_nE150, 6))
    return 150 << 8;
  if (t == s_nE175)
    return 175 << 8;
  if (CosmoOneOf(t, s_nE250, 5))
    return 250 << 8;
  return 200 << 8;
}

// update_unit_energy, hooks/update_unit_state.cpp:17-51: 10hp per second, after 2 seconds
// since the last spend, for completed casters that are not frozen.
int CosmoEnergyRegen(void)
{
  return (10 * 256) / 24;  // 106
}

// cloak_tech.cpp: get_cloaking_tech returns none, so cloaking is free.
bool CosmoCloakFree(void)
{
  return true;
}

bool CosmoFlashShielding(unsigned wType)
{
  CosmoResolveUnits();
  if (wType == s_nVagrantShade || wType == s_nPlasmaShell || CosmoOneOf(wType, s_nGolden, 5))
    return false;
  return true;
}

bool CosmoShieldRegen(unsigned wType)
{
  CosmoResolveUnits();
  return CosmoOneOf(wType, s_nGolden, 5);
}

// The flash-shielding ramp: map(timer, 2s, 7s, 10hp/1s, 2hp/1s, clamp), forerunners x2.
int CosmoShieldRegenAmount(const CUnit *pUnit)
{
  CosmoResolveUnits();
  const DWORD t = ModUnitStateOf(const_cast<CUnit *>(pUnit)).nShieldRegen;
  int nHeal = (10 * 256) / 24;              // 106 at the 2-second mark
  const int nEnd = (2 * 256) / 24;          // 21 at the 7-second mark and after
  if (t >= 48 + 120)
    nHeal = nEnd;
  else if (t > 48)
    nHeal = nHeal + ((nEnd - nHeal) * static_cast<int>(t - 48)) / 120;
  if (CosmoOneOf(pUnit->wType, s_nForerunner, 7))
    nHeal *= 2;
  return nHeal;
}

int CosmoShieldRegenFixed(void)
{
  return 21;  // update_unit_state.cpp: heal_shields(21, ...)
}

// get_regeneration_factor's base: 1hp per second, doubled while burrowed (api.cpp:4317-4322).
int CosmoHpRegenAmount(const CUnit *pUnit)
{
  const int n = 256 / 24;
  return (pUnit->dwFlags & CUnit::UF_Burrowed) ? n * 2 : n;
}

// ---------------------------------------------------------------------------------------
// Cagra and larva (gptp-fork hooks/orders/larva_creep_spawn.cpp).

bool CosmoCreepSpreader(unsigned wType)
{
  CosmoResolveUnits();
  return CosmoOneOf(wType, s_nSpreader, 11);
}

bool CosmoCreepSpreaderDone(unsigned wType)
{
  CosmoResolveUnits();
  return CosmoOneOf(wType, s_nSpreaderDone, 5);
}

bool CosmoSpawnsLarva(unsigned wType)
{
  CosmoResolveUnits();
  return CosmoOneOf(wType, s_nLarvaSpawn, 9);
}

// unit_id_get_larva_type, cm_ids.cpp:279-288.
unsigned CosmoLarvaType(unsigned wType)
{
  CosmoResolveUnits();
  if (wType == s_nLarvaSpawn[1] || wType == s_nLarvaSpawn[4])  // Caphrolosk, Ocsal Cesiant
    return s_nOcsalLarva;
  return s_nLarva;
}

bool CosmoIsLarva(unsigned wType)
{
  CosmoResolveUnits();
  return wType == s_nLarva || wType == s_nOcsalLarva;
}

// The mod's creep region: 320 px either side, 200 up and down (larva_creep_spawn.cpp:172-212),
// as tiles, clamped to the map.
int CosmoCreepBox(int nX, int nY, int aBox[4])
{
  int nLeft = (nX - 320) >> 5;
  int nRight = (nX + 320) >> 5;
  int nTop = (nY - 200) >> 5;
  int nBottom = (nY + 200) >> 5;
  if (nLeft < 0)
    nLeft = 0;
  if (nTop < 0)
    nTop = 0;
  if (nRight > static_cast<int>(gwMapWidth) - 1)
    nRight = static_cast<int>(gwMapWidth) - 1;
  if (nBottom > static_cast<int>(gwMapHeight) - 1)
    nBottom = static_cast<int>(gwMapHeight) - 1;
  aBox[0] = nLeft;
  aBox[1] = nTop;
  aBox[2] = nRight;
  aBox[3] = nBottom;
  return 1;  // the caller tests the ellipse itself
}

// ---------------------------------------------------------------------------------------
// The game's kind, from the type byte (gptp-fork GameType 30-44, enumerations.h:601-635).

CosmoKind CosmoGameKind(void)
{
  switch (gGameHeader.bGameType) {
    case 33: return COSMO_KIND_FACTIONWARS;
    case 34: return COSMO_KIND_MASSACRE;
    case 37: return COSMO_KIND_DUOPOLY;
    case 41: return COSMO_KIND_DEATHMARCH;
  }
  return COSMO_KIND_NONE;
}

// ---------------------------------------------------------------------------------------
// The Zerg remainder: regression, egg cancels, broodspawn and the burrowed spreaders.

// The regression tables' ids (api.cpp:2106-2220, cm_utils.cpp:601-637), resolved once.
struct CosmoRegressions {
  // structure morphs -> the previous tier
  unsigned nHachirosk, nExcisant, nCagrant, nSurkith, nSpraith, nLarvosk, nKazitrith,
      nNydus, nIrolIris,
      nQuazethPool, nVornathPond, nMutathSpire, nHydrithDen, nSkibactScab, nAxtothAxis,
      nGeszkathGrotto, nAlkagIteth, nZorkizShroud, nUlgorgCavern, nMatravilNest,
      nOvermindTheFall;
  unsigned nCaphrolosk, nGathtelosk, nEzigrisant, nSkortrith, nSpirtith, nOcsalCesiant,
      nOthstolOviform, nKalkiirLake, nTosvrolHaunt, nTethvithSwamp, nBegrozithBarrow,
      nKethrajKeep, nChokralChazsal, nZozarcGorge, nAlmakisAntre, nOvermindWithShell;
  // unit morphs -> the morph source
  unsigned nDroleth, nGosvileth, nZethrokor, nSovrokor, nCikralisk, nProtathalor,
      nMutalisk, nSkithrokor, nKagralisk, nEvigrilisk, nAlmaksalisk, nKeskathalor,
      nGeszithalor, nZoryusthaleth, nInzorthrokor, nLakizilisk, nKonvilisk, nVilgorokor,
      nMatraleth, nYblyleth;
  unsigned nVorvrokor, nZethalor, nGarvrokor, nGhitorokor, nSovroleth, nTosgrilisk,
      nVithrilisk, nCherbrathalor, nOkrimnilisk, nAlgoztalisk, nKethrostalor, nAlkajelisk,
      nUltrokor, nAlzurokor, nZobriolisk, nTetcaerokor, nIsthrathaleth, nAkistrokor,
      nScrylyleth;
  // factional cases
  unsigned nQuazilisk, nQuazrokor, nNathrokor, nNathrelisk, nHydralisk, nHydrokor,
      nHydraliskOrkoti, nIzirokor, nRozirokor, nIziralisk, nGorgrokor, nAzanthrokor;
  unsigned nIroleth, nLiiralisk, nKalkalisk, nBactalisk, nTethzorokor, nZarcavrokor;
  // broodspawn and misc
  unsigned nRilirokor, nRililisk, nSkryrokor, nSovrokor2, nGathtelosk2, nZytryleth,
      nEgg, nCocoon, nSteward, nOcsalLarva, nMapRevealer;
  unsigned nBurrower[4];
  unsigned nMason, nScribe, nOmnivale, nHachirosk2, nCaphrolosk2;
  // factional unit substitutions (cm_utils.cpp:432-558), base -> per-faction 1..5
  unsigned nHecaton[10], nFuturist[7], nGolden[6], nNydusT[4], nUpstart[8],
      nKhalendric[4], nSacredSaber[5], nStarbound[4], nImicren[4], nGoszirith[3],
      nOrkoti[3], nZorthos[3];
};
static CosmoRegressions g_rg;
static bool s_bRegs = false;

#define C(id, name) g_rg.id = CosmoUnit(name)

static void CosmoResolveRegs(void)
{
  if (s_bRegs)
    return;
  s_bRegs = true;
  C(nHachirosk, "zerg_hachirosk");       C(nExcisant, "zerg_excisant");
  C(nCagrant, "zerg_cagrant_cesiant");   C(nSurkith, "zerg_surkith_cesiant");
  C(nSpraith, "zerg_spraith_cesiant");   C(nLarvosk, "zerg_larvosk_cesiant");
  C(nKazitrith, "zerg_kazitrith_cesiant"); C(nNydus, "zerg_nydus_cesiant");
  C(nIrolIris, "zerg_irol_iris");        C(nQuazethPool, "zerg_quazeth_pool");
  C(nVornathPond, "zerg_vornath_pond");  C(nMutathSpire, "zerg_mutath_spire");
  C(nHydrithDen, "zerg_hydrith_den");    C(nSkibactScab, "zerg_skibact_scab");
  C(nAxtothAxis, "zerg_axtoth_axis");    C(nGeszkathGrotto, "zerg_geszkath_grotto");
  C(nAlkagIteth, "zerg_alkag_iteth");    C(nZorkizShroud, "zerg_zorkiz_shroud");
  C(nUlgorgCavern, "zerg_ulgorg_cavern"); C(nMatravilNest, "zerg_matravil_nest");
  C(nOvermindTheFall, "zerg_overmind_the_fall");
  C(nCaphrolosk, "zerg_caphrolosk");     C(nGathtelosk, "zerg_gathtelosk");
  C(nEzigrisant, "zerg_ezigrisant");     C(nSkortrith, "zerg_skortrith_cesiant");
  C(nSpirtith, "zerg_spirtith_cesiant"); C(nOcsalCesiant, "zerg_ocsal_cesiant");
  C(nOthstolOviform, "zerg_othstol_oviform"); C(nKalkiirLake, "zerg_kalkiir_lake");
  C(nTosvrolHaunt, "zerg_tosvrol_haunt"); C(nTethvithSwamp, "zerg_tethvith_swamp");
  C(nBegrozithBarrow, "zerg_begrozith_barrow"); C(nKethrajKeep, "zerg_kethraj_keep");
  C(nChokralChazsal, "zerg_chokral_chazsal"); C(nZozarcGorge, "zerg_zozarc_gorge");
  C(nAlmakisAntre, "zerg_almakis_antre"); C(nOvermindWithShell, "zerg_overmind_with_shell_the_fall");
  C(nDroleth, "zerg_droleth");           C(nGosvileth, "zerg_gosvileth");
  C(nZethrokor, "zerg_zethrokor");       C(nSovrokor, "zerg_sovrokor");
  C(nCikralisk, "zerg_cikralisk");       C(nProtathalor, "zerg_protathalor");
  C(nMutalisk, "zerg_mutalisk");         C(nSkithrokor, "zerg_skithrokor");
  C(nKagralisk, "zerg_kagralisk");       C(nEvigrilisk, "zerg_evigrilisk");
  C(nAlmaksalisk, "zerg_almaksalisk");   C(nKeskathalor, "zerg_keskathalor");
  C(nGeszithalor, "zerg_geszithalor");   C(nZoryusthaleth, "zerg_zoryusthaleth");
  C(nInzorthrokor, "zerg_inzorthrokor"); C(nLakizilisk, "zerg_lakizilisk");
  C(nKonvilisk, "zerg_konvilisk");       C(nVilgorokor, "zerg_vilgorokor");
  C(nMatraleth, "zerg_matraleth");       C(nYblyleth, "zerg_yblyleth");
  C(nVorvrokor, "zerg_vorvrokor");       C(nZethalor, "zerg_zethalor");
  C(nGarvrokor, "zerg_garvrokor");       C(nGhitorokor, "zerg_ghitorokor");
  C(nSovroleth, "zerg_sovroleth");       C(nTosgrilisk, "zerg_tosgrilisk");
  C(nVithrilisk, "zerg_vithrilisk");     C(nCherbrathalor, "zerg_cherbrathalor");
  C(nOkrimnilisk, "zerg_okrimnilisk");   C(nAlgoztalisk, "zerg_algoztalisk");
  C(nKethrostalor, "zerg_kethrostalor"); C(nAlkajelisk, "zerg_alkajelisk");
  C(nUltrokor, "zerg_ultrokor");         C(nAlzurokor, "zerg_alzurokor");
  C(nZobriolisk, "zerg_zobriolisk");     C(nTetcaerokor, "zerg_tetcaerokor");
  C(nIsthrathaleth, "zerg_isthrathaleth"); C(nAkistrokor, "zerg_akistrokor");
  C(nScrylyleth, "zerg_scrylyleth");
  C(nQuazilisk, "zerg_quazilisk");       C(nQuazrokor, "zerg_quazrokor");
  C(nNathrokor, "zerg_nathrokor");       C(nNathrelisk, "zerg_nathrelisk");
  C(nHydralisk, "zerg_hydralisk");       C(nHydrokor, "zerg_hydrokor");
  C(nHydraliskOrkoti, "zerg_hydralisk_orkoti");
  C(nIzirokor, "zerg_izirokor");         C(nRozirokor, "zerg_rozirokor");
  C(nIziralisk, "zerg_iziralisk");       C(nGorgrokor, "zerg_gorgrokor");
  C(nAzanthrokor, "zerg_azanthrokor");
  C(nIroleth, "zerg_iroleth");           C(nLiiralisk, "zerg_liiralisk");
  C(nKalkalisk, "zerg_kalkalisk");       C(nBactalisk, "zerg_bactalisk");
  C(nTethzorokor, "zerg_tethzorokor");   C(nZarcavrokor, "zerg_zarcavrokor");
  C(nRilirokor, "zerg_rilirokor");       C(nRililisk, "zerg_rililisk");
  C(nSkryrokor, "zerg_skryrokor");       C(nGathtelosk2, "zerg_gathtelosk");
  C(nSovrokor2, "zerg_sovrokor");        C(nZytryleth, "zerg_zytryleth_zorthos");
  C(nEgg, "zerg_egg");                   C(nCocoon, "zerg_cocoon");
  C(nSteward, "protoss_steward");        C(nOcsalLarva, "zerg_ocsal_larva");
  C(nMapRevealer, "map_revealer");
  C(nBurrower[0], "zerg_agrileth");      C(nBurrower[1], "zerg_yblyleth");
  C(nBurrower[2], "zerg_scrylyleth");    C(nBurrower[3], "zerg_malkothrokor");
  C(nMason, "terran_mason");             C(nScribe, "protoss_scribe");
  C(nOmnivale, "omnivale");              C(nHachirosk2, "zerg_hachirosk");
  C(nCaphrolosk2, "zerg_caphrolosk");
  // the factional substitutions, base then 1..n per faction (cm_utils.cpp:432-558)
  C(nHecaton[0], "terran_maverick"); C(nHecaton[1], "terran_hotshot");
  C(nHecaton[2], "terran_cyprian"); C(nHecaton[3], "terran_cyprian_hecaton");
  C(nHecaton[4], "terran_apostle"); C(nHecaton[5], "terran_auriel");
  C(nHecaton[6], "terran_vulture"); C(nHecaton[7], "terran_vulture_hecaton");
  C(nHecaton[8], "terran_goliath"); C(nHecaton[9], "terran_arhaun");
  C(nFuturist[0], "terran_maverick"); C(nFuturist[1], "terran_maverick_futurist");
  C(nFuturist[2], "terran_cleric"); C(nFuturist[3], "terran_cleric_futurist");
  C(nFuturist[4], "terran_cyclops"); C(nFuturist[5], "terran_cyclops_futurist");
  C(nFuturist[6], "terran_wraith"); C(nGolden[0], "terran_maverick");
  C(nGolden[1], "terran_maverick_golden"); C(nGolden[2], "terran_harakan");
  C(nGolden[3], "terran_harakan_golden"); C(nGolden[4], "terran_goliath");
  C(nGolden[5], "terran_aspirant");
  C(nNydusT[0], "terran_harakan"); C(nNydusT[1], "terran_camazotz");
  C(nNydusT[2], "terran_cyprian"); C(nNydusT[3], "terran_cohort");
  C(nUpstart[0], "terran_maverick"); C(nUpstart[1], "terran_maverick_upstart");
  C(nUpstart[2], "terran_cyprian"); C(nUpstart[3], "terran_cannibal");
  C(nUpstart[4], "terran_vulture"); C(nUpstart[5], "terran_vulture_upstart");
  C(nUpstart[6], "terran_goliath"); C(nUpstart[7], "terran_southpaw");
  C(nKhalendric[0], "protoss_ecclesiast"); C(nKhalendric[1], "protoss_ecclesiast_khalendric");
  C(nKhalendric[2], "protoss_hierophant"); C(nKhalendric[3], "protoss_ashguard");
  C(nSacredSaber[0], "protoss_dracadin"); C(nSacredSaber[1], "protoss_dracadin_order");
  C(nSacredSaber[2], "protoss_legionnaire"); C(nSacredSaber[3], "protoss_adherent");
  C(nSacredSaber[4], "protoss_vassal");
  C(nStarbound[0], "protoss_zealot"); C(nStarbound[1], "protoss_zealot_starbound");
  C(nStarbound[2], "protoss_legionnaire"); C(nStarbound[3], "protoss_cicatus");
  C(nImicren[0], "zerg_quazilisk"); C(nImicren[1], "zerg_quazrokor");
  C(nImicren[2], "zerg_izirokor"); C(nImicren[3], "zerg_rozirokor");
  C(nGoszirith[0], "zerg_zethrokor"); C(nGoszirith[1], "zerg_sovrokor");
  C(nGoszirith[2], "zerg_hydralisk");
  C(nOrkoti[0], "zerg_vorvrokor"); C(nOrkoti[1], "zerg_zethalor");
  C(nOrkoti[2], "zerg_hydralisk");
  C(nZorthos[0], "zerg_agrileth"); C(nZorthos[1], "zerg_yblyleth");
  C(nZorthos[2], "zerg_nathrokor");
}

#undef C

// get_zerg_regression: the chains above, the worker for completed base buildings, and
// the factional cases; 0xFFFF for none (api.cpp:2106-2220).
unsigned CosmoZergRegression(CUnit *pUnit)
{
  CosmoResolveRegs();
  const unsigned t = pUnit->wType;
  ModUnitState &st = ModUnitStateOf(pUnit);
  if ((st.nFlags & 1) || CosmoIsLarva(t))
    return 0xFFFF;
  // structure morphs -> the previous tier
  if (t == g_rg.nCaphrolosk || t == g_rg.nGathtelosk) return g_rg.nHachirosk;
  if (t == g_rg.nEzigrisant) return g_rg.nExcisant;
  if (t == g_rg.nSurkith || t == g_rg.nSpraith || t == g_rg.nLarvosk ||
      t == g_rg.nKazitrith || t == g_rg.nNydus)
    return g_rg.nCagrant;
  if (t == g_rg.nSkortrith) return g_rg.nSurkith;
  if (t == g_rg.nSpirtith) return g_rg.nSpraith;
  if (t == g_rg.nOcsalCesiant) return g_rg.nLarvosk;
  if (t == g_rg.nOthstolOviform) return g_rg.nIrolIris;
  if (t == g_rg.nVornathPond) return g_rg.nQuazethPool;
  if (t == g_rg.nKalkiirLake) return g_rg.nVornathPond;
  if (t == g_rg.nTosvrolHaunt) return g_rg.nMutathSpire;
  if (t == g_rg.nSkibactScab) return g_rg.nHydrithDen;
  if (t == g_rg.nTethvithSwamp) return g_rg.nSkibactScab;
  if (t == g_rg.nBegrozithBarrow) return g_rg.nAxtothAxis;
  if (t == g_rg.nKethrajKeep) return g_rg.nGeszkathGrotto;
  if (t == g_rg.nChokralChazsal) return g_rg.nAlkagIteth;
  if (t == g_rg.nUlgorgCavern) return g_rg.nZorkizShroud;
  if (t == g_rg.nZozarcGorge) return g_rg.nUlgorgCavern;
  if (t == g_rg.nAlmakisAntre) return g_rg.nMatravilNest;
  if (t == g_rg.nOvermindWithShell) return g_rg.nOvermindTheFall;
  // unit morphs -> the morph source
  if (t == g_rg.nGosvileth) return g_rg.nDroleth;
  if (t == g_rg.nVorvrokor) return g_rg.nZethrokor;
  if (t == g_rg.nZethalor) return g_rg.nZethrokor;
  if (t == g_rg.nGarvrokor) return g_rg.nSovrokor;
  if (t == g_rg.nGhitorokor) return g_rg.nCikralisk;
  if (t == g_rg.nSovroleth) return g_rg.nProtathalor;
  if (t == g_rg.nTosgrilisk) return g_rg.nMutalisk;
  if (t == g_rg.nVithrilisk) return g_rg.nSkithrokor;
  if (t == g_rg.nCherbrathalor) return g_rg.nKagralisk;
  if (t == g_rg.nOkrimnilisk) return g_rg.nEvigrilisk;
  if (t == g_rg.nAlgoztalisk) return g_rg.nAlmaksalisk;
  if (t == g_rg.nKethrostalor) return g_rg.nKeskathalor;
  if (t == g_rg.nAlkajelisk) return g_rg.nGeszithalor;
  if (t == g_rg.nUltrokor) return g_rg.nZoryusthaleth;
  if (t == g_rg.nAlzurokor) return g_rg.nInzorthrokor;
  if (t == g_rg.nZobriolisk) return g_rg.nLakizilisk;
  if (t == g_rg.nTetcaerokor) return g_rg.nKonvilisk;
  if (t == g_rg.nIsthrathaleth) return g_rg.nVilgorokor;
  if (t == g_rg.nAkistrokor) return g_rg.nMatraleth;
  if (t == g_rg.nScrylyleth) return g_rg.nYblyleth;
  // the factional cases (cm_utils.cpp:601-637; 2 Imicren, 3 Goszirith, 4 Orkoti, 5 Zorthos)
  const int nFaction = ModPlayerStateOf(pUnit->bPlayer).bFaction;
  if (t == g_rg.nLiiralisk) return nFaction == 2 ? g_rg.nQuazrokor : g_rg.nQuazilisk;
  if (t == g_rg.nKalkalisk) return nFaction == 5 ? g_rg.nNathrelisk : g_rg.nNathrokor;
  if (t == g_rg.nBactalisk)
    return nFaction == 3 ? g_rg.nHydrokor : nFaction == 4 ? g_rg.nHydraliskOrkoti
                                                          : g_rg.nHydralisk;
  if (t == g_rg.nTethzorokor)
    return nFaction == 2 ? g_rg.nRozirokor : nFaction == 5 ? g_rg.nIziralisk
                                                           : g_rg.nIzirokor;
  if (t == g_rg.nZarcavrokor) return nFaction == 4 ? g_rg.nAzanthrokor : g_rg.nGorgrokor;
  if (t == g_rg.nZytryleth) return g_rg.nScrylyleth;
  // a completed Zerg building becomes its worker; an incomplete one stays dead
  if ((gulUnitTypeFlags[t] & 1) && pUnit->GetRace() == 1) {
    if (!(pUnit->dwFlags & 1))
      return 0xFFFF;
    return g_rg.nDroleth;
  }
  return 0xFFFF;
}

bool CosmoRegressionSpawns(unsigned wRegressTo)
{
  CosmoResolveRegs();
  return wRegressTo == g_rg.nDroleth || wRegressTo == g_rg.nGosvileth ||
         CosmoIsLarva(wRegressTo);
}

// get_cancel_morph_revert_id: the egg's larva when fewer than three remain by its
// building, else its morph source; 0xFFFF when it should die (unit_morph.cpp:586-609).
unsigned CosmoCancelMorphRevert(CUnit *pEgg)
{
  CosmoResolveRegs();
  const unsigned wSource = pEgg->wPrevUnitType;
  if (CosmoIsLarva(wSource)) {
    if (pEgg->pUnion80 != NULL) {
      extern BYTE gLarvaNearCount;
      gLarvaNearCount = 0;
      find_first_unit_near(pEgg->pUnion80->pSprite->wPosX,
                           PosU(pEgg->pUnion80->pSprite->wPosY), 0x100, FindCountLarvae,
                           (intptr_t)pEgg->pUnion80);
      if (gLarvaNearCount < 3 && !(pEgg->dwFlags & 4))
        return wSource == g_rg.nOcsalLarva ? g_rg.nOcsalLarva : wSource;
    }
    return 0xFFFF;
  }
  return wSource != 0xFFFF ? wSource : 0xFFFF;
}

bool CosmoBroodspawn(unsigned wType)
{
  CosmoResolveRegs();
  return wType == g_rg.nRilirokor || wType == g_rg.nRililisk || wType == g_rg.nSkryrokor;
}

bool CosmoSwarmholdBuilding(unsigned wType)
{
  CosmoResolveRegs();
  return wType == g_rg.nGathtelosk2;
}

bool CosmoTripleBirth(unsigned wType)
{
  CosmoResolveRegs();
  return wType == g_rg.nSovrokor2;
}

bool CosmoBurrowedSpreader(unsigned wType)
{
  CosmoResolveRegs();
  for (int i = 0; i < 4; i++)
    if (wType == g_rg.nBurrower[i])
      return true;
  return false;
}

// spawn_custom_creep_at: the unit's footprint, and a two-tile ring for a factory
// (api.cpp:1952-1967).
void CosmoCoverCreep(const CUnit *pUnit)
{
  const point16_t box = gUnitPlaceboxSize[pUnit->wType];
  const int nW = box.x / 32, nH = box.y / 32;
  const int x0 = pUnit->pSprite->wPosX / 32 - nW / 2;
  const int y0 = PosU(pUnit->pSprite->wPosY) / 32 - nH / 2;
  const bool bFactory = (gulUnitTypeFlags[pUnit->wType] & 1) != 0;
  const int dL = bFactory ? -2 : 0, dT = bFactory ? -1 : 0;
  const int dR = bFactory ? 2 : 0, dB = bFactory ? 2 : 0;
  for (int y = y0 + dT; y < y0 + nH + dB; y++) {
    if (y < 0 || y >= static_cast<int>(gwMapHeight))
      continue;
    for (int x = x0 + dL; x < x0 + nW + dR; x++) {
      if (x < 0 || x >= static_cast<int>(gwMapWidth))
        continue;
      if (!tile_can_have_creep(static_cast<WORD>(x), static_cast<WORD>(y)))
        cover_creep_at(x, y);
    }
  }
}

// ---------------------------------------------------------------------------------------
// The game modes.

bool CosmoMassacreGasUnit(unsigned wType)
{
  CosmoResolveRegs();
  if (CosmoIsLarva(wType) || wType == g_rg.nEgg || wType == g_rg.nCocoon ||
      wType == g_rg.nSteward || wType == g_rg.nMapRevealer)
    return false;
  if (gulUnitTypeFlags[wType] & 0x10)  // a subunit pays nothing
    return false;
  if (wType >= 0xB0 && wType <= 0xB2)  // the mineral fields
    return false;
  return !CosmoUncappedGasNode(wType) && !CosmoCappedGasNode(wType);
}

unsigned CosmoRacialWorker(int nRace)
{
  CosmoResolveRegs();
  switch (nRace) {
    case 0: return g_rg.nMason;
    case 1: return g_rg.nScribe;
    case 2: return g_rg.nDroleth;
  }
  return 0xFFFF;
}

int CosmoDuopolyOtherRace(int nRace)
{
  switch (nRace) {
    case 0: return 1;  // TerranProtoss
    case 1: return 2;  // ProtossZerg
    case 2: return 0;  // ZergTerran
  }
  return nRace;
}

// get_random_faction (cm_utils.cpp:751-759): the race's first to last, 1-based.
int CosmoRollFaction(int nRace)
{
  switch (nRace) {
    case 0: case 1: case 2: return net_randbetween(1, 5, 0x14);
    case 3: return net_randbetween(1, 2, 0x14);
  }
  return 0;
}

// get_factional_unit_id (cm_utils.cpp:432-558): the pair of ids in order base, faction.
static unsigned CosmoFactionSwap(const unsigned *p, int nFaction, unsigned t)
{
  for (int k = 0; p[k * 2] != 0xFFFFu && p[k * 2] != 0; k += 2)
    if (t == p[k * 2])
      return p[k * 2 + 1];
  return t;
}

unsigned CosmoFactionUnit(unsigned wType, int nRace, int nFaction)
{
  CosmoResolveRegs();
  static unsigned aT[10 * 2], aP[6 * 2], aZ[8 * 2];
  if (nRace == 0) {
    switch (nFaction) {
      case 1:  // Orion Imperium: no replacements
        break;
      case 2: {  // Hecaton Coalition
        static const unsigned k[] = {g_rg.nHecaton[0], g_rg.nHecaton[1], g_rg.nHecaton[2],
                                     g_rg.nHecaton[3], g_rg.nHecaton[4], g_rg.nHecaton[5],
                                     g_rg.nHecaton[6], g_rg.nHecaton[7], g_rg.nHecaton[8],
                                     g_rg.nHecaton[9], 0xFFFFu, 0xFFFFu};
        return CosmoFactionSwap(k, 0, wType);
      }
      case 3: {  // Futurist Federation
        static const unsigned k[] = {g_rg.nFuturist[0], g_rg.nFuturist[1], g_rg.nFuturist[2],
                                     g_rg.nFuturist[3], g_rg.nFuturist[4], g_rg.nFuturist[5],
                                     g_rg.nFuturist[6], g_rg.nFuturist[2], 0xFFFFu, 0xFFFFu};
        return CosmoFactionSwap(k, 0, wType);
      }
      case 4: {  // Golden Republic
        static const unsigned k[] = {g_rg.nGolden[0], g_rg.nGolden[1], g_rg.nGolden[2],
                                     g_rg.nGolden[3], g_rg.nGolden[4], g_rg.nGolden[5],
                                     0xFFFFu, 0xFFFFu};
        return CosmoFactionSwap(k, 0, wType);
      }
      case 5: {  // Nydus Trinity
        static const unsigned k[] = {g_rg.nNydusT[0], g_rg.nNydusT[1], g_rg.nNydusT[2],
                                     g_rg.nNydusT[3], 0xFFFFu, 0xFFFFu};
        return CosmoFactionSwap(k, 0, wType);
      }
    }
    // the Upstart faction (6) folds into 5's slot for v1
    return wType;
  }
  if (nRace == 1) {
    switch (nFaction) {
      case 2: {  // Order of the Sacred Saber
        static const unsigned k[] = {g_rg.nSacredSaber[0], g_rg.nSacredSaber[1],
                                     g_rg.nSacredSaber[2], g_rg.nSacredSaber[3],
                                     g_rg.nSacredSaber[4], g_rg.nSacredSaber[4], 0xFFFFu,
                                     0xFFFFu};
        return CosmoFactionSwap(k, 0, wType);
      }
      case 3: {  // Khalendric Knights
        static const unsigned k[] = {g_rg.nKhalendric[0], g_rg.nKhalendric[1],
                                     g_rg.nKhalendric[2], g_rg.nKhalendric[3], 0xFFFFu,
                                     0xFFFFu};
        return CosmoFactionSwap(k, 0, wType);
      }
      case 4: {  // Starbound
        static const unsigned k[] = {g_rg.nStarbound[0], g_rg.nStarbound[1],
                                     g_rg.nStarbound[2], g_rg.nStarbound[3], 0xFFFFu,
                                     0xFFFFu};
        return CosmoFactionSwap(k, 0, wType);
      }
    }
    return wType;
  }
  if (nRace == 2) {
    switch (nFaction) {
      case 2: {  // Imicren Tendril
        static const unsigned k[] = {g_rg.nImicren[0], g_rg.nImicren[1], g_rg.nImicren[2],
                                     g_rg.nImicren[3], 0xFFFFu, 0xFFFFu};
        return CosmoFactionSwap(k, 0, wType);
      }
      case 3: {  // Goszirith Tendril
        static const unsigned k[] = {g_rg.nGoszirith[0], g_rg.nGoszirith[1],
                                     g_rg.nGoszirith[2], g_rg.nHydrokor, 0xFFFFu, 0xFFFFu};
        return CosmoFactionSwap(k, 0, wType);
      }
      case 4: {  // Orkoti Tendril
        static const unsigned k[] = {g_rg.nOrkoti[0], g_rg.nOrkoti[1], g_rg.nOrkoti[2],
                                     g_rg.nHydraliskOrkoti, 0xFFFFu, 0xFFFFu};
        return CosmoFactionSwap(k, 0, wType);
      }
      case 5: {  // Zorthos Tendril
        static const unsigned k[] = {g_rg.nZorthos[0], g_rg.nZorthos[1], g_rg.nZorthos[2],
                                     g_rg.nNathrelisk, 0xFFFFu, 0xFFFFu};
        return CosmoFactionSwap(k, 0, wType);
      }
    }
    return wType;
  }
  return wType;
}

// ---------------------------------------------------------------------------------------
// The per-frame mode work: the once-at-start init of the three modes, at the game's
// first frame (game_hooks.cpp:701-761), and nothing after.

static bool s_bModeInit = false;

void CosmoForgetGame(void)
{
  s_bModeInit = false;
}

void CosmoPerFrame(void)
{
  if (!PortCosmonarchy() || s_bModeInit)
    return;
  s_bModeInit = true;
  switch (CosmoGameKind()) {
    case COSMO_KIND_MASSACRE:
    case COSMO_KIND_DEATHMARCH: {
      // the nodes all go: gas comes only from kills and completions
      for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
        if (CosmoUncappedGasNode(pUnit->wType) || CosmoCappedGasNode(pUnit->wType))
          pUnit->Suicide();
      }
      break;
    }
    case COSMO_KIND_FACTIONWARS: {
      for (int i = 0; i < PLAYER_SLOTS_MAX; i++) {
        if (gPlayerData[i].nType == 1 && ModPlayerStateOf(i).bFaction == 0)
          ModPlayerStateOf(i).bFaction = static_cast<BYTE>(CosmoRollFaction(gPlayerData[i].nRace));
      }
      break;
    }
    case COSMO_KIND_DUOPOLY: {
      CosmoResolveRegs();
      for (int i = 0; i < PLAYER_SLOTS_MAX; i++) {
        if (gPlayerData[i].nType != 2)
          continue;
        ModPlayerState &ps = ModPlayerStateOf(i);
        if (ps.bGameModeRace != 0)
          continue;
        switch (gPlayerData[i].nRace) {
          case 0: ps.bGameModeRace = 1; break;  // TerranProtoss
          case 1: ps.bGameModeRace = 2; break;  // ProtossZerg
          case 2: ps.bGameModeRace = 3; break;  // ZergTerran
        }
        // the town hall becomes the Omnivale, and the start gains the second
        // race's three workers in place of one of the four (create_init_units)
        const unsigned wWorker = CosmoRacialWorker(gPlayerData[i].nRace);
        const unsigned wOther = CosmoRacialWorker(CosmoDuopolyOtherRace(gPlayerData[i].nRace));
        const int nSx = gStartLocations[i].x, nSy = gStartLocations[i].y;
        int nSeen = 0;
        CUnit *pTown = NULL;
        for (CUnit *pUnit = CUnit::GetPlayerUnitList(i); pUnit; pUnit = pUnit->pPlayerUnitNext) {
          if (wWorker != 0xFFFF && pUnit->wType == wWorker) {
            if (++nSeen == 4)
              pUnit->Suicide();  // the fourth worker leaves room for the pair
          }
          if (pTown == NULL && (pUnit->dwFlags & CUnit::UF_Building) &&
              pUnit->pSprite->wPosX / 32 == nSx / 32 &&
              PosU(pUnit->pSprite->wPosY) / 32 == nSy / 32)
            pTown = pUnit;
        }
        if (pTown != NULL && g_rg.nOmnivale != 0xFFFF)
          pTown->TransformType(static_cast<WORD>(g_rg.nOmnivale));
        if (wOther != 0xFFFF && pTown != NULL) {
          for (int k = 0; k < 3; k++) {
            CUnit *pNew = CreateUnitAt(wOther, pTown->pSprite->wPosX,
                                       PosU(pTown->pSprite->wPosY), i);
            if (pNew != NULL)
              pNew->FinishedMaking();
          }
        }
      }
      break;
    }
    default:
      break;
  }
}
