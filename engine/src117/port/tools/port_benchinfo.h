// port_benchinfo.h -- the benchmark overlay's read of the game (PRD 2026-10-17).
//
// A run's overlay shows the selected units' details in three views, and the players beside
// them. A run is a replay with 1.17's console hidden, so the selection is the overlay's own:
// a list of unit table indices, which a unit drops out of when it dies.
//
// Presentation only: every call here reads the game's state and writes none of it.
#pragma once
#include "../../h/selectlimits.h"  // SELECTION_MAX

// The units the overlay can hold selected at once, as 1.17's console holds twelve.
enum { PORT_BENCH_SELECT_MAX = SELECTION_MAX };

struct PortBenchUnit {
  int nIndex;                   // the unit's index in the unit table (pUnit - CUnit::Head())
  int nType, nPlayer;
  char szName[64];              // the unit type's name as the game shows it
  int nHp, nHpMax;              // whole points (the game stores hit points x256)
  int nShields, nShieldsMax;    // 0/0 for a type without shields
  int nEnergy, nEnergyMax;      // 0/0 for a type without energy
  int nKills;                   // the unit's kills
  bool bHasWeapon;              // it has a ground or an air weapon
  int nArmour, nArmourUp;       // the type's armour and the owner's armour upgrade level
  int nWeaponUp;                // the ground (else air) weapon's upgrade level, -1 with none
  char szWeapon[48];            // that weapon's name, as the console's tip gives it
  int nDamage, nDamageUp;       // its damage as the console shows it, and the upgrades' bonus
  bool bAir;                    // a flyer
  int nResources;               // a mineral field's or a geyser's what is left, -1 for any other
  bool bBuilding, bWorker, bTransport;
  int anQueue[5];               // the build queue from the current slot; 228 is an empty slot
  int nQueue;                   // the queued items at its head
  int nProgressPercent;         // what it is making, researching or becoming, 0..100; -1 none
  int nLarvae;                  // a hatchery's, lair's or hive's larvae, else 0
  int anCargo[8];               // the unit types a transport or a bunker is carrying
  int anCargoSpace[8];          // the room each takes (units.dat's volume)
  int nCargo;
  int nCargoUsed, nCargoHold;   // the room taken and the hold's
};

struct PortBenchPlayer {
  int nSlot, nRace, nTeam;
  int nMineralsGathered, nGasGathered;  // the figures summary.json records
  int nMinerals, nGas;                  // the bank
  int nSupplyUsed, nSupplyMax;          // its own race's, in whole supply
  int nUnits;                           // units it owns on the map
  bool bLeft;                           // still in the game
  unsigned nOutFrame;                   // the game frame it went out, 0 while it is in
};

// The selected units, in the selection's order; the count written. Units that have died
// drop out of the selection here.
int PortBenchSelected(PortBenchUnit *pOut, int nMax);
// The overlay's selection: up to PORT_BENCH_SELECT_MAX unit table indices. Unset (or cleared), it
// is the game's own, which a click, a box and Shift make in a replay as in 1.17.
void PortBenchSelectSet(const int *pnIndices, int nCount);
// The selection as it stands, the count written.
int PortBenchSelection(int *pnIndices, int nMax);
void PortBenchSelectionClear(void);
// How many are selected, cheaply, for seconds.csv: the game's selection, or the live channel's.
int PortBenchSelectedCount(void);
// One unit by its index in the table; false when it is not alive.
bool PortBenchUnitRead(int nIndex, PortBenchUnit *pOut);
// The players in the game, by slot: every seat this game, those gone included.
int PortBenchPlayersRead(PortBenchPlayer *pOut, int nMax);
// The seats noted, once a frame of a run (port_bench.cpp), so one that goes is still known.
void PortBenchPlayersNote(void);
bool PortBenchWasSeat(int nSlot);
