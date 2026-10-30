// port_richbases.h -- rich bases, the tenth mutator (docs/modules/gameplay.md,
// "Rich resources").
//
// Some of a map's bases turn rich as the game starts: a trip to a rich mineral field gives 11
// minerals and to a rich geyser, or the refinery on it, 16 gas, StarCraft II's for 1.17's 8
// (MutatorTrip, h/mutators.h). Which bases is read from the map, so the mutator works on any
// melee map: its resources joined into bases, each start's main and natural, and the walking
// distance from every start to every other base. A base no start's main or natural may turn rich
// by the mutator's choice: any (past the naturals), one at least a radius from every start in a
// straight line, or the farthest only; at the chance chosen, rolled once for each set of bases
// that mirror one another, so every player gets the same. Every base turns every mineral field
// and geyser rich (Gold Rush), mains and naturals too.
//
// The game's simulation reads one byte a unit (the resource, and whether a worker's load came
// from one), kept with the trainer's snapshots, saved in a Hyperion save and part of the sync
// hash. The replay records which resources turned rich (RICH), so it plays them back as they
// were rolled whatever a later build's read would say.
#pragma once
#include <string>
#include <vector>

// As the map is loaded (MutatorsMapLoaded, a new game or a replay played back, not a save): the
// read and the roll, or the replay's record.
void RichBasesMapLoaded(void);
// A game frame's end (gameloop): the game's first says which bases turned rich, once.
void RichBasesFrame(void);

// The set-up's picture (port_front_mutators.cpp), before a game: the same read of a map from its
// file, its minitiles walkable or not (nMapW * 4 by nMapH * 4), its starts and resources in map
// pixels. Each base's middle, its kind (0 a start's main, 1 a natural, 2 any other) and its chance
// to turn rich in thousandths, 0 for none; the reason none may, or "". The roll itself waits for
// the game's seed.
struct RichBaseResource {
  int x, y;
  bool bGas;
  int nAmount;  // what it holds as the map places it; a mineral field of under 50 is a wall
};
struct RichBasePreview {
  int x, y, nKind, nChance;
};
struct MutatorSet;
std::string RichBasesPreview(int nMapW, int nMapH, const std::vector<unsigned char> &walk,
                             const std::vector<std::pair<int, int>> &starts, const std::vector<RichBaseResource> &res,
                             const MutatorSet &m, std::vector<RichBasePreview> *pOut);

// Whether the resource with unit index nUnit is rich, and whether a worker's load is from one.
bool RichBasesResource(int nUnit);
bool RichBasesCarry(int nUnit);
// The game's rich units as they began, unit indices, for the looks and the minimap's fog.
const std::vector<unsigned int> &RichBasesChosen(void);

// The replay's record (port_hxreplay.cpp): the indices of the resources that turned rich; a
// replay's read with NULL forgets the last one.
void RichBasesRecord(std::vector<unsigned int> *pOut);
void RichBasesRecorded(const std::vector<unsigned int> *pList);
// The Hyperion save's (HXST, port_hxsave.cpp), when the game has rich bases: the record and the
// units' bytes.
bool RichBasesInGame(void);
int RichBasesSave(void *pFile);
int RichBasesLoad(void *pFile);
// The units' bytes for the sync hash, NULL when the game has no rich bases.
const unsigned char *RichBasesTable(void);
