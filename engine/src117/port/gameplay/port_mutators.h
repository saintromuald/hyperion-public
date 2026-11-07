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
       MUT_NOCAP, MUT_ADDON, MUT_NOARMYKEY, MUT_ENERGY, MUT_NEWBIE, MUT_AUTOWORKERS, MUT_AUTOMINE, MUT_VICTORY,
       MUT_ANTIMETA, MUT_COUNT, MUT_RICH_CHANCE = MUT_COUNT, MUT_RICH_RADIUS };
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
// "richRadius":48,"units":0,"victory":0,"antiMeta":0},
// and back, a missing value 1.17's.
std::string MutatorsJson(const MutatorSet &m);
MutatorSet MutatorsFromJson(const PortJson &j);

// A launch's choice, which the session keeps.
void MutatorsChoose(const MutatorSet &m);
// --mutators: a preset's name, or values by name, e.g. "plenty", "minerals=200,gas=endless,
// start=1000/500,supply=400,workers=8,boost=50,fog=explored,build=x0.5,tech=all,rich=radius,
// radius=64,chance=75,victory=production,antimeta=on"; fog is normal, explored, none or resources (the
// resources and doodads remembered, never opponents revealed), tech normal, all, noupgrades or
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
// --mutators for a network game (the SB relay launch): the list parsed, stored in the net set.
bool MutatorsChooseNetList(const char *pszList);
// RulesForGame, for a new game: whether it is to have a mutator.
bool MutatorsChosenThisGame(void);
// As the game starts (game_init, after RulesForGame): the mutators it plays with, the chosen
// ones for a new game, the recorded ones for a replay or a save with HX_OPT_MUTATORS, else
// none.
void MutatorsForGame(void);
// The game's set, as it plays.
const MutatorSet &MutatorsOfGame(void);
// Whether production victory is on: triggers_loop checks it even on a map with no triggers.
bool MutatorProductionVictory(void);

// As the map is loaded (game_init, after LoadMapData, before the start units are placed): a new
// game's build, train, research and upgrade times at the build time mutator, its tech and
// upgrades all researched if the tech mutator says so, and the fog as the fog mutator has it.
// Resource fog also seeds its static explored tiles and fogged memories here, once per new map.
// A loaded game's mod unit types' times come from the mod at each start and not from the save;
// the times are the game's own tables, which every reader and the save then have.
void MutatorsMapLoaded(void);
// During a revision-4 new map's initial CircleUpdate pass, suppress a resource's generic fog
// copy: MutatorsResourcesRemember creates exactly one neutral copy a moment later. This is false
// at every other time, and for player-owned resources whose initial copy is intentionally absent.
bool MutatorFogSuppressInitialCopy(WORD wType);
// After each sight refresh puts the fog back over the map (gameloop, sub_46C180): the fog
// mutator's tiles, explored, or seen and explored, for every player. Resource fog does no new
// reveal here at revision 4; older records keep their original repeated reveal.
void MutatorsVisionRefresh(void);
// Each game frame (gameloop, next to RichBasesFrame): the anti-meta start's word on the roll,
// said once as the game begins.
void MutatorsFrame(void);

// The unit limit's choices (MutatorSet::nUnits), Automatic first: the count of each, 0 for
// Automatic, and its name; MutatorUnitLimitOf the index of the one nearest nUnits.
int MutatorUnitLimits(void);
int MutatorUnitLimit(int nIndex);
std::string MutatorUnitLimitName(int nIndex);
int MutatorUnitLimitOf(int nUnits);
// The rules' revision the game's mutators play by (MUTATOR_REVISION, or a replay's or save's).
int MutatorsRevisionOf(void);

// The replay's and the save's record, MUTATOR_RECORD_BYTES_VICTORY: the ten values, the rules'
// revision, MUTATOR_REVISION, rich bases' four, the unit limit, the later mutators, and finally
// production victory. Anti-meta deliberately has no record field: its rolled values are recorded
// instead, so playback does not roll again. Every old size stays readable; a field a record
// predates takes Classic's value. 100 (NEWBIE) ends at the automatic-upgrade masks, 72 through one add-on, 64 (UNITS)
// at the unit limit, 60 (RADIUS) at rich bases' radius, 56 (RICH) before it, 44 (REVISION) before
// rich bases, 40 (TEN) before the revision, and 28 (FIRST) after the first seven values.
// MutatorsRecorded(NULL, 0) forgets the last one read; false for a record this build does not read.
#define MUTATOR_RECORD_BYTES_VICTORY 104
#define MUTATOR_RECORD_BYTES_NEWBIE 100
#define MUTATOR_RECORD_BYTES 72
#define MUTATOR_RECORD_BYTES_UNITS 64
#define MUTATOR_RECORD_BYTES_RADIUS 60
#define MUTATOR_RECORD_BYTES_RICH 56
#define MUTATOR_RECORD_BYTES_REVISION 44
#define MUTATOR_RECORD_BYTES_TEN 40
#define MUTATOR_RECORD_BYTES_FIRST 28
// 1: the supply boost gives whole supply (0.6.10; the user, 2026-10-24).
// 2: the unit table grows with the supply cap past 200, and with the unit limit.
// 3: the no-cap mutator and the one add-on's (2026-11-02), the record's two new words.
// 4: fog=resources seeds static memory once and never opens live sight; older records replay the
// old, repeatedly revealed resource tiles as they were played.
#define MUTATOR_REVISION 4
bool MutatorRecordBytesRead(int nBytes);
void MutatorsSave(unsigned char *pbOut);
bool MutatorsRecorded(const unsigned char *pbIn, int nBytes);
