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
  DWORD nStun;           // the mod's TimerStun: set to 1secs on regression; > 1 blocks orders
  DWORD nFlags;          // bit 0: ValueRegressionUnstable -- the unit cannot regress again
};
ModUnitState &ModUnitStateOf(CUnit *pUnit);

// The game's per-player mod state: the mode race and the faction, in a game that has the
// mod. Lives beside the unit state; saved and hashed with it.
struct ModPlayerState {
  BYTE bFaction;      // the player's faction, 1..5 by race (Faction Wars); 0 none
  BYTE bGameModeRace; // GameModeRace: 1 TerranProtoss, 2 ProtossZerg, 3 ZergTerran; 0 none
  BYTE _abSpare[2];
};
ModPlayerState &ModPlayerStateOf(int nSlot);

// Zeroes and sizes the tables, once a game (with PlayerTechInit, maphdr.cpp).
void ModUnitStateInit(void);
// The state's rows, for the sync hash's units domain; null when none yet.
const ModUnitState *ModUnitStates(unsigned *pnCount);
const ModPlayerState *ModPlayerStates(unsigned *pnCount);

// The mod's game kinds, from the game's type byte (gptp-fork GameType 30-44), for the
// modes ported so far; anything else is none.
enum CosmoKind {
  COSMO_KIND_NONE = 0,
  COSMO_KIND_FACTIONWARS,
  COSMO_KIND_MASSACRE,
  COSMO_KIND_DUOPOLY,
  COSMO_KIND_DEATHMARCH,
};
CosmoKind CosmoGameKind(void);

// The mod's per-frame work a game needs: the once-at-start mode init (Massacre's node
// removal, Faction Wars' rolls, Duopoly's town hall and workers) on the first frame.
// Called from gameloop_update_game; the tables' reset hooks ModUnitStateInit.
void CosmoPerFrame(void);
// A new game: forget the mode init so its first frame runs it again.
void CosmoForgetGame(void);
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

// -- the Zerg remainder ---------------------------------------------------------
// get_zerg_regression (api.cpp:2106-2220): the type a dying Zerg unit regresses to,
// 0xFFFF for none. The structure chains, the worker default for completed base
// buildings, the unit morph sources and the factional cases.
unsigned CosmoZergRegression(CUnit *pUnit);
// A regression destination that is made as a new unit while the old one dies for real:
// a worker (Droleth, Gosvileth) or a larva (the mod's regress spawns these beside).
bool CosmoRegressionSpawns(unsigned wRegressTo);
// get_cancel_morph_revert_id (unit_morph.cpp:586-609): what a cancelled egg becomes --
// the larva it came from when fewer than three remain, else the morph source; 0xFFFF
// when the egg should simply die.
unsigned CosmoCancelMorphRevert(CUnit *pEgg);
// The mod's broodspawn (Rilirokor, Rililisk, Skryrokor), which live 119 frames off cagra.
bool CosmoBroodspawn(unsigned wType);
// unit_id_swarmhold_value: the Gathtelosk hatches one extra unit per egg.
bool CosmoSwarmholdBuilding(unsigned wType);
// UnitId::is_triple_birth: the Sovrokor hatches one more.
bool CosmoTripleBirth(unsigned wType);
// spreads_cagra_while_burrowed: Agrileth, Yblyleth, Scrylyleth, Malkothrokor.
bool CosmoBurrowedSpreader(unsigned wType);
// Covers the unit's footprint with cagra and, for a factory, a two-tile ring
// (spawn_custom_creep_at, api.cpp:1952-1967).
void CosmoCoverCreep(const CUnit *pUnit);

// -- the game modes -------------------------------------------------------------
// unit_id_should_give_gas_in_massacre_mode (cm_ids.cpp:1532-1542): every unit pays the
// Massacre completion reward but the larva, the eggs, the steward, the revealer and the
// resource nodes.
bool CosmoMassacreGasUnit(unsigned wType);
// The mod's racial workers for Duopoly's mixed starts (Terran Mason, Protoss Scribe,
// Zerg Droleth); 0xFFFF for a race it has none of.
unsigned CosmoRacialWorker(int nRace);
// Duopoly's pairs: Terran <-> Protoss, Protoss <-> Zerg, Zerg <-> Terran.
int CosmoDuopolyOtherRace(int nRace);
// The faction a race's computer rolls in Faction Wars, 1..5 (1..2 for the Askosi), from
// the game's RNG (get_random_faction, cm_utils.cpp:751-759); 0 for a race with none.
int CosmoRollFaction(int nRace);
// get_factional_unit_id (cm_utils.cpp:432-558): the unit a faction's player builds in
// place of the base one; the unit itself when the faction has no replacement.
unsigned CosmoFactionUnit(unsigned wType, int nRace, int nFaction);

#endif  // PORT_COSMONARCHY_H
