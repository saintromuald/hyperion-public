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
#include "../../gen/globals117.h"      // gwMapWidth, gwMapHeight
#include "../data/port_moddata.h"      // ModDataId, g_hxModGame, ModTypeByName

// ---------------------------------------------------------------------------------------
// The gate and the name table.

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
}

const ModUnitState *ModUnitStates(unsigned *pnCount)
{
  *pnCount = s_nState;
  return s_pState;
}

int ModUnitStateSave(void *pFile)
{
  const DWORD adw[1] = {static_cast<DWORD>(s_nState)};
  if (pfwrite(adw, sizeof adw, pFile) == 0)
    return 0;
  if (s_nState != 0)
    return compress(s_pState, static_cast<int>(sizeof(ModUnitState) * s_nState), pFile) != 0;
  return 1;
}

int ModUnitStateLoad(void *pFile)
{
  DWORD dwCount;
  if (pfread(&dwCount, sizeof dwCount, pFile) == 0 ||
      dwCount > static_cast<DWORD>(g_pools.units))
    return 0;
  if (dwCount == 0)
    return 1;
  if (!s_pState || static_cast<unsigned>(g_pools.units) != s_nState) {
    delete[] s_pState;
    s_nState = static_cast<unsigned>(g_pools.units);
    s_pState = new ModUnitState[s_nState];
  }
  memset(s_pState, 0xFF, sizeof(ModUnitState) * s_nState);
  return decompress(s_pState, static_cast<int>(sizeof(ModUnitState) * dwCount), pFile) != 0;
}

// ---------------------------------------------------------------------------------------
// The vespene rework (gptp-fork hooks/harvest.cpp).

// The yields, as systems/triggers/trigger_system.h:170 gives them: { 5, 5, 3, 5, 7, 9 }.
static const BYTE s_yields[6] = {5, 5, 3, 5, 7, 9};

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
