// port_cosmonarchy.h -- the Cosmonarchy mod's rules, in a game that has its data.
//
// The port of Cosmonarchy BW (the-no-frauds-club's 1.16.1 total conversion, as its Plus
// build plays it) brings the mod's data through the ordinary mod framework, but the mod's
// gameplay lives in GPTP plugins that change the engine itself. Those changes are ported
// here, one system at a time, gated on PortCosmonarchy() so any other game plays as it
// always has. Every number is the mod's own (gptp-fork's hooks/), with the anchor in a
// comment: the port is a transcription, not a redesign.
#ifndef PORT_COSMONARCHY_H
#define PORT_COSMONARCHY_H

#include "../../h/platform.h"  // BYTE, WORD, DWORD

class CUnit;

// The game being played has the Cosmonarchy mod's data. The mod id is checked as well as
// the mod game flag, so another mod with data plays unchanged.
bool PortCosmonarchy(void);

// The game's per-unit mod state: the counters the mod's hooks keep on every unit
// (gptp-fork's UnitExt timers). It lives beside the unit table, indexed by the unit's
// slot, because CUnit's layout is 1.17's. Classic games leave it zeroed and unread.
struct ModUnitState {
  DWORD nEnergyRegen;    // frames since energy was spent; regen needs > 2secs (48)
  DWORD nShieldRegen;    // frames since shields took damage; flash shielding needs > 2secs
  DWORD nDamageTaken;    // frames since any damage; hp and fixed shield regen need > 2secs
  DWORD nSpawnLarvaGate; // the mod's TimerSpawnLarva: runs the larva logic every 8th frame
  DWORD _dwSpare;        // kept zero
};
ModUnitState &ModUnitStateOf(CUnit *pUnit);

// Zeroes and sizes the table, once a game (with PlayerTechInit, maphdr.cpp).
void ModUnitStateInit(void);
// The state's rows, for the sync hash's units domain; null when none yet.
const ModUnitState *ModUnitStates(unsigned *pnCount);
// The save's section (port_hxsave.cpp, "HXMU"), only in a game that has the mod.
int ModUnitStateSave(void *pFile);
int ModUnitStateLoad(void *pFile);

// The mod's unit types by their data name ("zerg_hachirosk"); 0xFFFF when the game has no
// such row. Resolved once, since a given data set always assigns the same ids.
unsigned CosmoUnit(const char *pszName);

// -- vespene rework (gptp-fork hooks/harvest.cpp) --------------------------------
// The mod's yields, per trip: 0 mineral-depleted, 1 mineral, 2 gas ridge, 3 gas geyser,
// 4 gas tier 2, 5 gas tier 3. The defaults of trigger_system.h; the mod's trigger actions
// can change them (not yet ported).
int CosmoYield(int nKind);
// get_gas_tier: 0 none, 1 a ridge, 2 a geyser or tier-1 cap, 3 a tier-2 cap, 4 a tier-3 cap.
int CosmoGasTier(unsigned wType);
// cm_ids: the neutral nodes never need a structure; the capped nodes are the mod's
// refineries (unit_is_geyser_ex + can_harvest_gas: self or mutual ally, completed).
bool CosmoUncappedGasNode(unsigned wType);
bool CosmoCappedGasNode(unsigned wType);
// unit_is_geyser_ex: can this worker mine gas at this node right now.
bool CosmoGeyserMineable(const CUnit *pNode, const CUnit *pWorker);
// get_harvest_amount: what one trip carries away from this node.
int CosmoHarvestAmount(const CUnit *pNode);

// -- energy (update_unit_energy, max_energy) ------------------------------------
// get_unit_max_energy: 200 for everyone, the named overrides for the rest, x256.
int CosmoMaxEnergy(const CUnit *pUnit);
// The energy the unit regains a frame once the 2-second post-spend delay passes: 10hp/1secs.
int CosmoEnergyRegen(void);
// The mod's cloaking costs no energy (get_cloaking_tech -> none).
bool CosmoCloakFree(void);

// -- shields (update_unit_state: flash shielding and shield regeneration) --------
// unit_id_has_flash_shielding: every shielded unit but the shades and the goldens.
bool CosmoFlashShielding(unsigned wType);
// unit_id_has_shield_regeneration: the five golden units.
bool CosmoShieldRegen(unsigned wType);
// The shield points a flash-shielding unit gains this frame, from its timer: the
// 10hp/sec to 2hp/sec ramp over the 2nd to 7th second since damage, forerunners doubled.
int CosmoShieldRegenAmount(const CUnit *pUnit);
// The fixed regen the golden units gain a frame: 21 (about 2 hp/sec).
int CosmoShieldRegenFixed(void);
// Passive hp regeneration: 1hp/sec, doubled while burrowed, after the 2-second window
// (get_regeneration_factor's base). The infested and spell factors are not yet ported.
int CosmoHpRegenAmount(const CUnit *pUnit);

// -- cagra and larva (larva_creep_spawn.cpp) ------------------------------------
// spreads_creep: the units that spread cagra, the always-list and the completed-list.
bool CosmoCreepSpreader(unsigned wType);      // Caphrolosk, Gathtelosk, the Cesiant, ...
bool CosmoCreepSpreaderDone(unsigned wType);  // ... and the completed-only five
// unit_id_spawns_larva_by_order: the buildings whose Spread Creep also births larva.
bool CosmoSpawnsLarva(unsigned wType);
// unit_id_get_larva_type: Ocsal buildings birth Ocsal larva, the rest the Zerg larva.
unsigned CosmoLarvaType(unsigned wType);
// A larva or Ocsal larva.
bool CosmoIsLarva(unsigned wType);
// The mod's creep region around a spreader: 320 px out either side, 200 up and down, as
// the ellipse x*x*100 + y*y*256 <= 10240000.
int CosmoCreepBox(int nX, int nY, int aBox[4]);

#endif  // PORT_COSMONARCHY_H
