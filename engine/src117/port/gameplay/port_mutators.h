// port_mutators.h -- the mutators a game is launched with and plays by
// (h/mutators.h for what each one does).
//
// A game's mutators are one set for every player (MutatorSet, port_session.h): the skirmish
// set-up's, --mutators, or a room's, the host's choice. Any one other than 1.17's plays by
// Hyperion's rules with HX_OPT_MUTATORS, and the replay (MUTS) and the save (HXMU) hold the
// set, so the game plays back and loads as it was played.
#pragma once
#include <string>

#include "port_session.h"  // MutatorSet

struct PortJson;

// Every value 1.17's: a game with this set has no mutator.
bool MutatorsStandard(const MutatorSet &m);
// A set made one Hyperion plays: each value inside its bounds (MutatorSet's comments).
MutatorSet MutatorsClamp(const MutatorSet &m);

// The choices the set-up lists for each mutator, a value each, and their names; *pnStandard
// the index of 1.17's. A value not among them shows as the nearest. Rich bases has two more
// choices on its card, the radius and the chance (MUT_RICH_RADIUS, MUT_RICH_CHANCE, past the
// cards' count).
enum { MUT_MINERALS, MUT_GAS, MUT_START, MUT_SUPPLY, MUT_WORKERS, MUT_BOOST, MUT_FOG, MUT_BUILD, MUT_TECH, MUT_RICH,
       MUT_COUNT, MUT_RICH_CHANCE = MUT_COUNT, MUT_RICH_RADIUS };
// Rich bases' radius in tiles on a map nMapW by nMapH tiles: m's tiles, or its share of the
// map's size, its width and height averaged.
int MutatorRichRadiusTiles(const MutatorSet &m, int nMapW, int nMapH);
// And in words: "48 tiles", "a quarter of the map".
std::string MutatorRichRadiusName(const MutatorSet &m);
int MutatorChoices(int nMut);
std::string MutatorChoiceName(int nMut, int nIndex);
int MutatorChoiceOf(const MutatorSet &m, int nMut);           // the index m has
void MutatorChoose(MutatorSet *pm, int nMut, int nIndex);     // and the one chosen
// The index m has when it is that choice exactly, else -1 (a value the lists left, Unlimited or
// x1.5, which still plays); and m's value in words, a choice's name or the value as it is.
int MutatorChoiceExact(const MutatorSet &m, int nMut);
std::string MutatorValueName(const MutatorSet &m, int nMut);
// Whether m's value of the mutator is other than 1.17's.
bool MutatorOn(const MutatorSet &m, int nMut);
// Whether m's value makes the game harder than 1.17's (the user, 2026-10-25, marked in a dark
// red): less in a field or a geyser, fewer workers, a cap under 200, upgrades or research taken
// away; and whether a choice would.
bool MutatorHard(const MutatorSet &m, int nMut);
bool MutatorChoiceHard(int nMut, int nIndex);
const char *MutatorName(int nMut);                            // "Mineral fields", ...
const char *MutatorLine(int nMut);                            // what it changes, 1.17's value
// The presets (SC2's lobby's, the design's): Standard, Plenty, Gold Rush, Infinite and Quick
// Start; MutatorPresetOf the one m is, or -1 for none (Custom).
int MutatorPresets(void);
const char *MutatorPresetName(int nIndex);
MutatorSet MutatorPreset(int nIndex);
int MutatorPresetOf(const MutatorSet &m);
// The set in words, for the logs and the rules' strip: "mineral fields x2, 8 start workers",
// or "" when it is 1.17's.
std::string MutatorsText(const MutatorSet &m, bool bShown = false);

// A room's word on them (port_lanroom*.cpp, port_front_lan.cpp, web/lan/join.js): the set as
// a JSON object, {"minerals":100,"gas":100,"startMinerals":50,"startGas":0,"supply":200,
// "workers":4,"boost":0,"fog":0,"build":100,"tech":0,"rich":0,"richChance":50,"richRules":3,
// "richRadius":48,"units":0},
// and back, a missing value 1.17's.
std::string MutatorsJson(const MutatorSet &m);
MutatorSet MutatorsFromJson(const PortJson &j);

// A launch's choice, which the session keeps.
void MutatorsChoose(const MutatorSet &m);
// --mutators: a preset's name, or values by name, e.g. "plenty", "minerals=200,gas=endless,
// start=1000/500,supply=400,workers=8,boost=50,fog=explored,build=x0.5,tech=all,rich=radius,
// radius=64,chance=75"; fog is normal, explored or none, tech normal, all, noupgrades or
// noresearch, rich off, naturals, radius (middle, its name before 0.6.21, too), far or every,
// radius in tiles or quarter, third or half (of the map), with fair=off, atleast=off and
// richseed=map for the roll's rules and announce=off for no word at the start; units=auto or a
// count for the unit limit. "rich" alone is Plenty, the preset's name before rich bases.
bool MutatorsChooseList(const char *pszList);
// And the other way: m as --mutators lists it, its values other than 1.17's ("supply=600,
// minerals=200%,tech=all"), "" for none; MutatorsChooseList reads it back to m.
std::string MutatorsList(const MutatorSet &m);
// A room's match (port_lanlobby.cpp): the launch's set, the same on every seat; forgotten by
// MutatorsForGame.
void MutatorsChooseNet(const MutatorSet &m);
// RulesForGame, for a new game: whether it is to have a mutator.
bool MutatorsChosenThisGame(void);
// As the game starts (game_init, after RulesForGame): the mutators it plays with, the chosen
// ones for a new game, the recorded ones for a replay or a save with HX_OPT_MUTATORS, else
// none.
void MutatorsForGame(void);
// The game's set, as it plays.
const MutatorSet &MutatorsOfGame(void);

// As the map is loaded (game_init, after LoadMapData, before the start units are placed): a new
// game's build, train, research and upgrade times at the build time mutator, its tech and
// upgrades all researched if the tech mutator says so, and the fog as the fog mutator has it;
// a loaded game's mod unit types' times, which come from the mod at each start and not from
// the save. The times are the game's own tables, which every reader and the save then have.
void MutatorsMapLoaded(void);
// After each sight refresh puts the fog back over the map (gameloop, sub_46C180): the fog
// mutator's tiles, explored, or seen and explored, for every player.
void MutatorsVisionRefresh(void);

// The unit limit's choices (MutatorSet::nUnits), Automatic first: the count of each, 0 for
// Automatic, and its name; MutatorUnitLimitOf the index of the one nearest nUnits.
int MutatorUnitLimits(void);
int MutatorUnitLimit(int nIndex);
std::string MutatorUnitLimitName(int nIndex);
int MutatorUnitLimitOf(int nUnits);
// The rules' revision the game's mutators play by (MUTATOR_REVISION, or a replay's or save's).
int MutatorsRevisionOf(void);

// The replay's and the save's record, MUTATOR_RECORD_BYTES: the ten values, the rules'
// revision, MUTATOR_REVISION, rich bases' four and the unit limit. 0.6.21 to 0.6.24 wrote
// MUTATOR_RECORD_BYTES_RADIUS, without the unit limit, read as Automatic; 0.6.16 to 0.6.20 wrote
// MUTATOR_RECORD_BYTES_RICH, without the radius, read with 48 tiles; 0.6.10 to 0.6.15 wrote
// MUTATOR_RECORD_BYTES_REVISION, without rich bases, read with none; 0.6.9
// MUTATOR_RECORD_BYTES_TEN, the ten without the revision, whose supply boost rounds down to a
// half; and its first builds MUTATOR_RECORD_BYTES_FIRST, the first seven, before fog, build time
// and tech, read with the three at 1.17's. MutatorsRecorded(NULL, 0) forgets the last one read;
// false for a record this build does not read.
#define MUTATOR_RECORD_BYTES 64
#define MUTATOR_RECORD_BYTES_RADIUS 60
#define MUTATOR_RECORD_BYTES_RICH 56
#define MUTATOR_RECORD_BYTES_REVISION 44
#define MUTATOR_RECORD_BYTES_TEN 40
#define MUTATOR_RECORD_BYTES_FIRST 28
// 1: the supply boost gives whole supply (0.6.10; the user, 2026-10-24).
// 2: the unit table grows with the supply cap past 200, and with the unit limit.
#define MUTATOR_REVISION 2
bool MutatorRecordBytesRead(int nBytes);
void MutatorsSave(unsigned char *pbOut);
bool MutatorsRecorded(const unsigned char *pbIn, int nBytes);
