// mutators.h -- the mutators a game plays with (docs/modules/gameplay.md, "Mutators").
//
// The skirmish set-up's and a room's mutators change values 1.17 fixes for every game: what each
// mineral field and geyser holds, or that it never runs dry; the minerals, gas and workers each
// player begins with; the supply cap; the supply each depot, pylon, overlord and town hall
// gives; the fog of war; how long things take to build; and the tech and upgrades. The game
// asks for each where 1.17 has its own: a resource made or placed (CUnitInit.cpp, maphdr.cpp), a
// harvest (CUnit::GetHarvest), the start resources (gametype_mutator_resources), the start units
// (placebox.cpp), the caps (count_init), the supply given (count.cpp, the computer's planner in
// SAI_Build.cpp, the console in statdraw.cpp), and the research and upgrade gates (Allowed.cpp).
// The fog, the times and the tech researched are set in the game's own tables as the map is
// loaded and after each sight refresh (MutatorsMapLoaded, MutatorsVisionRefresh). In a game
// without mutators, every game by classic rules among them, each gives 1.17's value back, so
// the game plays as 1.17 does to the frame. Mutators play by Hyperion's rules (HX_OPT_MUTATORS,
// hxrules.h), and the replay and the save hold them (port_mutators.h).
#ifndef MUTATORS_H
#define MUTATORS_H

// Whether the game being played has a mutator other than 1.17's, set as it starts
// (MutatorsForGame, port_mutators.h).
extern bool g_hxMutatorsOn;

// What a mineral field (bGas false) or a geyser or refinery (bGas true) made or placed holds:
// wAmount, the map's or 1.17's, at the mutator's share, at least 1 and at most 65,535.
unsigned short MutatorResourceOf(unsigned short wAmount, bool bGas);
// Whether that kind of resource never runs dry: a harvest takes from it and leaves it as it was.
bool MutatorEndlessOf(bool bGas);
// The supply a unit type gives, nMade halves (units.dat's, gubUnitFoodMade), with the boost:
// rounded down to a whole supply, and never less than without it. A game recorded before 0.6.10
// rounds down to a half, as it was played (port_mutators.h, the record's revision).
int MutatorFoodMadeOf(int nMade);
// A player's supply cap in halves (1.17's 400), and the workers each player starts with (4).
int MutatorSupplyCapOf(void);
int MutatorWorkersOf(void);
// The minerals and gas each player begins with, when a mutator sets them; false for the game
// type's own (1.17's 50 and 0 in melee, none by a map's settings).
bool MutatorStartResources(int *pnMinerals, int *pnGas);

// The fog mutator's choices, and the tech mutator's: all to research, all researched from the
// start, no upgrades, or neither upgrades nor the abilities that need research.
enum { MUTATOR_FOG_CLASSIC, MUTATOR_FOG_EXPLORED, MUTATOR_FOG_NONE };
enum { MUTATOR_TECH_CLASSIC, MUTATOR_TECH_ALL, MUTATOR_TECH_NO_UPGRADES, MUTATOR_TECH_NO_RESEARCH };
int MutatorTechOf(void);

// Rich bases (port_richbases.h): which bases may turn rich, past the naturals, outside a radius
// round every start (which was the middle ground until 0.6.21), the farthest only, or every
// mineral field and geyser; the rules of the roll, one roll a set of bases that mirror each
// other, at least one base rich, and the same bases in every game on a map; and the trip a rich
// resource gives, StarCraft II's.
enum { MUTATOR_RICH_OFF, MUTATOR_RICH_NATURALS, MUTATOR_RICH_RADIUS, MUTATOR_RICH_FAR, MUTATOR_RICH_EVERY };
// MUTATOR_RICH_QUIET: the game's start does not say which bases turned rich (0.6.21; a rule of
// its own so the sets before it, which said, still do)
enum { MUTATOR_RICH_FAIR = 1, MUTATOR_RICH_AT_LEAST_ONE = 2, MUTATOR_RICH_SAME_MAP = 4, MUTATOR_RICH_QUIET = 8 };
enum { MUTATOR_RICH_MINERAL_TRIP = 11, MUTATOR_RICH_GAS_TRIP = 16 };
// The load a trip takes from the resource with unit index nUnit (a mineral field, or a geyser
// and the refinery on it): 1.17's 8, or a rich one's.
int MutatorTripOf(int nUnit, bool bGas);
// A worker took a load from the resource nSource: whether it is a rich one's, for its look.
void MutatorCarryOf(int nWorker, int nSource);
// A unit made in the slot nUnit (CUnit::sub_425710): neither rich nor carrying a rich load.
void MutatorUnitMade(int nUnit);
// A rich mineral field's or geyser's name, "Rich Mineral Field", or 0 for any other unit.
const char *MutatorRichNameOf(int nUnit, int nType);

static inline unsigned short MutatorResource(unsigned short wAmount, bool bGas) {
  return g_hxMutatorsOn ? MutatorResourceOf(wAmount, bGas) : wAmount;
}
static inline bool MutatorEndless(bool bGas) {
  return g_hxMutatorsOn && MutatorEndlessOf(bGas);
}
static inline int MutatorTrip(int nUnit, bool bGas) {
  return g_hxMutatorsOn ? MutatorTripOf(nUnit, bGas) : 8;
}
static inline void MutatorCarry(int nWorker, int nSource) {
  if (g_hxMutatorsOn)
    MutatorCarryOf(nWorker, nSource);
}
static inline const char *MutatorRichName(int nUnit, int nType) {
  return g_hxMutatorsOn ? MutatorRichNameOf(nUnit, nType) : 0;
}
static inline int MutatorFoodMade(int nMade) {
  return g_hxMutatorsOn ? MutatorFoodMadeOf(nMade) : nMade;
}
static inline int MutatorSupplyCap(void) {
  return g_hxMutatorsOn ? MutatorSupplyCapOf() : 400;
}
static inline int MutatorWorkers(void) {
  return g_hxMutatorsOn ? MutatorWorkersOf() : 4;
}
// Whether the tech mutator forbids researching upgrades (UpgradeAllowed), or abilities
// (ResearchAllowed); an ability that needs no research is used as in 1.17.
static inline bool MutatorUpgradesForbidden(void) {
  return g_hxMutatorsOn && MutatorTechOf() >= MUTATOR_TECH_NO_UPGRADES;
}
static inline bool MutatorResearchForbidden(void) {
  return g_hxMutatorsOn && MutatorTechOf() == MUTATOR_TECH_NO_RESEARCH;
}

#endif
