// playertables.h -- 1.17's per-player arrays as player tables.
//
// Each type here is the one a TU declares a game global with in place of 1.17's T[12]: the
// same 12 entries at the same address, and through operator[] the entries of Hyperion's
// slots past 11 (playerlimits.h). Names that 1.17's data has twice, at one address, share
// a tag and so share the extra entries. Element types are spelled without windows.h, since
// some TUs that include this declare their own Win32 prototypes in its place.
#ifndef PLAYERTABLES_H
#define PLAYERTABLES_H
#include "platform.h"  // LONG and ULONG

#include "playerlimits.h"
#include "poollimits.h"

struct PlayerTagMinerals;
struct PlayerTagGas;
struct PlayerTagGasGathered;       // 0x526430: dword_526430, gPlayerGasGathered_g
struct PlayerTagMineralsGathered;  // 0x526460: dword_526460, gPlayerMineralsGathered_g

typedef PlayerTable<int, PlayerTagMinerals> PlayerMineralsTable;
typedef PlayerTable<int, PlayerTagGas> PlayerGasTable;
typedef PlayerTable<int, PlayerTagGasGathered> PlayerGasGatheredTable;
typedef PlayerTable<int, PlayerTagMineralsGathered> PlayerMineralsGatheredTable;

// Counts, the player the inner index: the game counts' 28 rows (supply and the scores),
// the unit counts' 4 x 228 (all, completed, killed, lost) and the triggers' caches of them.
struct PlayerTagGameCounts;
struct PlayerTagUnitCounts;
struct PlayerTagCachedGameCounts;
struct PlayerTagCachedUnitCounts;
struct PlayerTagCachedCompletedCounts;
typedef PlayerGrid<ULONG, PlayerTagGameCounts, 28> PlayerGameCountsGrid;
typedef PlayerGrid3<ULONG, PlayerTagUnitCounts, 4, 228> PlayerUnitCountsGrid;
typedef PlayerGrid<ULONG, PlayerTagCachedGameCounts, 28> PlayerCachedGameCountsGrid;
typedef PlayerGrid<ULONG, PlayerTagCachedUnitCounts, 228> PlayerCachedUnitCountsGrid;
typedef PlayerGrid<ULONG, PlayerTagCachedCompletedCounts, 228> PlayerCachedCompletedCountsGrid;

// Alliances, shared vision, the units a player may build.
struct PlayerTagAlliances;
struct PlayerTagSharedVision;
struct PlayerTagUnitsAvailable;
typedef PlayerMatrix<unsigned char, PlayerTagAlliances> PlayerAlliancesMatrix;
typedef PlayerTable<ULONG, PlayerTagSharedVision> PlayerSharedVisionTable;
typedef PlayerTable<unsigned char[228], PlayerTagUnitsAvailable> PlayerUnitsAvailableTable;

// Start locations, 8 in 1.17 (gStartLocations and gStartLoc_534A00 name one object), and
// the head of each player's unit list.
struct PlayerTagStartLocations;
struct PlayerTagUnitLists;

// The AI's tables, a nation each, 8 in 1.17: the bankers, the captains (2,500 areas a
// nation, sgRegions their states), the towns and the guard posts.
struct PlayerTagAIBanker;
struct PlayerTagCaptains;
struct PlayerTagTowns;
struct PlayerTagGuards;
struct PlayerTagRegions;
struct PlayerTag6B68C0;
struct PlayerTag6B68E0;
typedef PlayerTable<unsigned char[AI_AREAS_117], PlayerTagRegions, 8> PlayerRegionsTable;
// The costs the banker stages for what a nation buys next (banker.cpp): 12 entries in 1.17,
// written by player, so a nation past 11 wrote over the next array's first four.
struct PlayerTagBankerOre;
struct PlayerTagBankerGas;
struct PlayerTagBankerSupply;
typedef PlayerTable<int, PlayerTagBankerOre> PlayerBankerOreTable;
typedef PlayerTable<int, PlayerTagBankerGas> PlayerBankerGasTable;
typedef PlayerTable<int, PlayerTagBankerSupply> PlayerBankerSupplyTable;

// The player records: the game's, the lobby's, the slots'. Their element type is
// PlayerData (net_cmds.h), named where they are declared.
struct PlayerTagData;
struct PlayerTagNetData;
struct PlayerTagSlots;

// Colours.
struct PlayerTagMinimapColor;
struct PlayerTagSelCircleColor;
struct PlayerTagPlayerSelCircleColor;
struct PlayerTagTeamColor;
typedef PlayerTable<unsigned char, PlayerTagMinimapColor> PlayerMinimapColorTable;
typedef PlayerTable<unsigned char, PlayerTagSelCircleColor> PlayerSelCircleColorTable;
typedef PlayerTable<unsigned char, PlayerTagPlayerSelCircleColor> PlayerPlayerSelCircleColorTable;
typedef PlayerTable<ULONG[2], PlayerTagTeamColor> PlayerTeamColorTable;

// Tech and upgrades. 1.17 keeps them in flat arrays split at the Brood War ids (24 techs
// and 46 upgrades in one, the rest in another), reached through the accessors in
// saveload.cpp; a slot past 11 has one record over all the ids, and the in-progress bit
// sets are a row a slot.
enum { PLAYER_TECHS = 0x2C, PLAYER_UPGRADES = 0x3D };
struct PlayerTechRecord {
  unsigned char abTechAvailable[PLAYER_TECHS];
  unsigned char abTechResearched[PLAYER_TECHS];
  unsigned char abUpgradeLevel[PLAYER_UPGRADES];
  unsigned char abMaxUpgradeLevel[PLAYER_UPGRADES];
};
PlayerTechRecord *PlayerTech(int nSlot);
// A new game's tech for every slot past 11: techs available, none researched, upgrades at
// 0 of the maximum levels 1.17 gives every player (pbMaxLevels, 61 of them).
void PlayerTechInit(const unsigned char *pbMaxLevels);
// A mod's techs and upgrades past 1.17's rows, from TECH_TYPE_MOD_FIRST and
// UPGRADE_TYPE_MOD_FIRST (h/typelimits.h): each slot's state of one in three bytes, a
// tech's available, researched and in progress, an upgrade's level, most level and in
// progress. The accessors (saveload.cpp, statbtn.cpp) reach them for those ids; an id or a slot
// out of range reaches a byte of its own that nothing reads. PlayerTechInit sets them as 1.17
// sets its own: every tech available and none researched, every upgrade at level 0 of 0
// (ModTechsForGame gives a mod's its most), nothing in progress.
enum { PLAYER_MOD_AVAILABLE, PLAYER_MOD_RESEARCHED, PLAYER_MOD_IN_PROGRESS };
enum { PLAYER_MOD_LEVEL, PLAYER_MOD_MAX_LEVEL };
unsigned char &PlayerModTech(int nSlot, unsigned nTech, int nField);
unsigned char &PlayerModUpgrade(int nSlot, unsigned nUpgrade, int nField);
// The game's rows of them (g_hxTypes) in its save, after their counts (port_hxsave.cpp's
// HXMT); 0 on an error.
int PlayerModTechSave(void *pFile);
int PlayerModTechLoad(void *pFile);

struct PlayerTagTechInProgress;
struct PlayerTagUpgradeInProgress;
typedef PlayerTable<unsigned char[6], PlayerTagTechInProgress> PlayerTechInProgressTable;
typedef PlayerTable<unsigned char[8], PlayerTagUpgradeInProgress> PlayerUpgradeInProgressTable;

// A Hyperion map's players past 7, from its HXPL section (port_players.cpp): each slot's
// owner (OWNR), race (SIDE), force (FORC) and colour (COLR), as 1.17's sections give them.
struct PlayerMapSlot {
  unsigned char bOwner, bRace, bForce, bColor;
};
const PlayerMapSlot *PlayersMapSlot(int nSlot);
// Before a pass over a map's sections: a map is 1.17's until its HXPL says otherwise.
void PlayersMapBegin();
// The slots a Hyperion map gives players: one past the highest that has an owner, at
// least 8 (the pools' sizes, port_pools.cpp).
int PlayersMapSlotCount();
int __stdcall PlayersMapHxpl(void *pFile, int nSize, void *pDest);
// HXRG: the full RGB colours of the slots from 8 on (port_players.cpp, stage G).
int __stdcall PlayersMapHxrg(void *pFile, int nSize, void *pDest);

// The computers a launch asks for past the 7 that 1.17's Play Custom table holds, and their
// races, which the session keeps (port_session.h); with none asked for, one in each slot past
// 7 the map opens.
void PlayersSetExtraComputers(int nCount, unsigned char bRace);
// The i-th extra computer's own race, past the one race above (a benchmark's seats).
void PlayersSetExtraComputerRace(int i, unsigned char bRace);
int PlayersExtraComputers();
unsigned char PlayersExtraComputerRace(int i);

// The trigger and victory tables, 8 in 1.17: each nation's victory state and code, its
// delayed action and timer, whether it took part, and its trigger list.
struct PlayerTagVictoryState;
struct PlayerTagVictoryCodes;
struct PlayerTagDelayedActions;
struct PlayerTagDelayedTimers;
struct PlayerTagParticipants;
struct PlayerTagTriggers;
typedef PlayerTable<unsigned char, PlayerTagVictoryState, 8> PlayerVictoryStateTable;
// Each nation's mission objectives, as a map string id (dword_5349A4).
struct PlayerTagObjectives;
typedef PlayerTable<ULONG, PlayerTagObjectives> PlayerObjectivesTable;
// The triggers a nation's list holds (victory.cpp), for the control channel.
int TriggersCount(int nSlot);
// A Hyperion map's triggers: HXTE, the slots past 11 each of TRIG's triggers runs
// for, read before TRIG; the player codes of a trigger record made the game's; and the
// record added to the lists of HXTE's slots. victory.cpp.
void TriggersHxMapBegin();
int __stdcall TriggersMapHxte(void *pFile, int nSize, void *);
void TriggerHxCodes(void *pTrigger);
void TryAddTriggerHx(void *pTrigger, int nIndex);

// The colours of the slots past 7 from HXPL, after the game has given 1.17's slots
// theirs (port_players.cpp).
void PlayersColorsApply();

// Which of a single-player game's set-up each player came from: the local
// player is row 0, and the k-th computer 1.17's set-up seats (seat_computer_player, noting the lobby
// slot it takes) row 1 + k, wherever the shuffle of the starts puts its record (ShuffleStartSlots,
// noting where each record came from). PlayersSeatedComputer gives a player's k, or -1, as
// it does in a multiplayer team game, which seats by team (PrepareTeamSlots).
void PlayersSeatsBegin();
void PlayersSeatNote(int nSlot);
void PlayersShuffleNote(int nSlot, int nFrom);
int PlayersSeatedComputer(int nPlayer);
// The lobby slot the k-th computer was seated in, before the shuffle; -1 past the last.
int PlayersSeatSlot(int k);
// The lobby slot a player's record was in before the shuffle of the starts, in any game the
// shuffle seats (a room's match too); its own slot where the shuffle did not move it.
int PlayersCameFrom(int nPlayer);
// The skirmish screen's colours: a row's choice among the 65 (1.17's twelve in tunit.pcx's
// order, then Remastered's and Hyperion's, PLAYER_COLOURS), or -1 for the slot's own, which the
// session keeps (port_session.h). At a game's start each player takes the colour of its row.
// PlayersColourRamp gives one of the 65's ramp of eight palette entries and its minimap entry,
// as the game takes them; tunit.pcx and tminimap.pcx are read the first time. Presentation only.
enum { PLAYER_COLOURS = 65 };
void PlayersChooseColour(int nRow, int nColour);
// The colour Hyperion gives the i-th player past 1.17's eight when the map names none, one of the
// 65 and none of 1.17's first eight; the skirmish set-up's rows past 8 start with it.
int PlayersDefaultColour(int i);
bool PlayersColourRamp(int nColour, unsigned char *pRamp, unsigned char *pMinimap);
// Or any colour in full RGB, fitted to a ramp as --colour's are (port_teamcolor.cpp), which a
// Hyperion replay keeps (RGBS).
void PlayersChooseColourRgb(int nRow, unsigned char r, unsigned char g, unsigned char b);
void PlayersChosenColoursApply();

// A save of a Hyperion game (port_players.cpp, port_hxsave.cpp): its set-up (the map's
// slots and the player records past 12), read before the game starts and turning the
// extra entries on, or for a 1.17 save turning them off; then the extra entries of the
// tables the simulation keeps, and the tech of the slots past 11. 0 on an error.
int PlayersSaveSetUp(void *pFile);
int PlayersLoadSetUp(void *pFile);
void PlayersLoadNone();
int PlayerTablesSave(void *pFile);
int PlayerTablesLoad(void *pFile);

#endif
