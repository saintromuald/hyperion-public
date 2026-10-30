// port_moddata.h -- a mod's data: new unit types, weapons, flingy, sprites, images,
// orders and techs, appended past 1.17's rows (docs/mod-data.md), and
// edits of rows already there. The active mod's data loads at start-up, after the
// data of the mods it depends on, and a game that has the mod fills its rows and writes its
// edits as the game's tables load; any other game never reaches them (h/typelimits.h).
#ifndef PORT_MODDATA_H
#define PORT_MODDATA_H

#include "../../h/platform.h"  // BYTE, WORD, DWORD

struct GROUP;

// At the end of LoadGraphics: the active mod's data when it has some, its images, their
// frames and their scripts after 1.17's. A mod whose data does not load is left out, and
// hyperion_mods.log says why.
void ModDataStartUp();

// The mod whose data loaded at start-up: its id, "" for none, its name as its mod.json gives
// it, and its data's fingerprint (SHA-1 of its data files and those of the mods it depends
// on, in load order, 20 bytes); and the mods with data it depends on, "id version" in load
// order and separated by ", ", "" for none.
const char *ModDataId();
const char *ModDataName();
const unsigned char *ModDataFingerprint();
const char *ModDataDepends();

// The game being played has the mod's types: set as a game starts (RulesForGame), from a
// new game's mod or the replay's or save's.
extern bool g_hxModGame;

// In CUnit::InitUnits, after units.dat: the rows of the mod's types, its edits and g_hxTypes
// in a game that has them; 1.17's types, and the rows the edits change as the original has
// them, in any other. Every table but images.dat is read again for each game before this,
// and the map's settings apply after it (LoadMapData), so they keep the last word.
void ModDataForGame();
// As a game ends (main.cpp, after free_game): what pushes brought put away, so the front end
// and the next game have the data loaded at start-up.
void ModDataAfterGame();

// A mod's unit type's submission (0 for none), the newest type of its name, and the last
// submission's number.
int ModUnitSubmission(unsigned nType);
int ModUnitNewest(unsigned nType);
int ModDataLastSubmission();

// A mod's unit's name, for get_unit_name_from_type; "" for none. And the name an edit gives
// one of the original's units, in a game that has the mod; null for none, and the original's
// name stands. A map's own name for the unit comes first.
const char *ModUnitName(unsigned nType);
const char *ModUnitEditName(unsigned nType);

// A mod's unit's command card: a 1.17 unit's, its record's "buttons" or else the unit its
// looks are (ModUnitLooks, h/typelimits.h).
unsigned ModUnitButtonset(unsigned nType);

// A mod's unit's requirements in 1.17's form (TechTree.cpp's sgTree): the unit, then its
// conditions, then 0xFFFF, so TTAllowed starts at [1]; null for a unit with none, which
// nothing trains. From its record's "requires" (docs/mod-data.md).
const unsigned short *ModUnitTree(unsigned nType);

// A mod's tech's use requirements in the same form (Allowed.cpp's sgTechUseTree), from its
// record's "use_requires"; null for a tech that is not the mod's, or in a game without it.
const unsigned short *ModTechUseTree(unsigned nTech);
// A mod's tech's research requirements, in 1.17's tree form, when the game has the mod and
// the tech is researched ("requires" in its record); else null.
const unsigned short *ModTechResearchTree(unsigned nTech);
// A mod's upgrade's requirements, in 1.17's tree form, when the game has the mod; else null.
const unsigned short *ModUpgradeTree(unsigned nUpgrade);
// The requirements an edit gives one of the original's units, techs or upgrades ("requires",
// "use_requires"), in the same form, in a game that has the mod; null where it gives none, and
// the original's tree applies. Only the tree is the edit's: the original's other checks
// (available, researched, in progress) stay, so a map's settings keep the last word.
enum ModTreeKind { MOD_TREE_UNIT, MOD_TREE_TECH_USE, MOD_TREE_TECH_RESEARCH, MOD_TREE_UPGRADE, MOD_TREE_KINDS };
const unsigned short *ModTreeEdit(int nKind, unsigned n);
// A new game's start for a mod's techs that are researched: available to every player, and
// researched by none; and for its upgrades: level 0 of their max_level for every player;
// whatever the map says of the rows they took.
void ModTechsForGame();

// A mod's unit has a command card of its own: its looks' card with the mod's buttons
// ("cards" naming the unit).
bool ModUnitHasCard(unsigned nType);

// A mod's unit goes in a build queue: it is no building (1.17 trains its units up to 105).
bool ModUnitTrainable(unsigned nType);

// The mod's strings (a button's text), with ids from MOD_STRING_FIRST past stat_txt's;
// status_get_str gives them. Null for an id the mod has none of.
enum { MOD_STRING_FIRST = 0xF000 };
const char *ModString(unsigned nId);

// A button a mod adds to a command card ("cards"), for a game that has the mod: a 1.17
// unit's card, or a card of a mod's unit's own (its looks' card with the mod's buttons).
struct ModCardButton {
  unsigned short wCard;  // the unit whose card it is
  unsigned short wPos;   // the card's position, 1 to 9
  unsigned short wIcon;  // the command icon
  unsigned short wUnit;  // the unit it trains, or 0xFFFF for a cast
  unsigned short wOrder; // the order a cast gives: at the caster's own place, or aimed
  unsigned short wTech;  // the cast's tech, whose use requirements show the button
  unsigned short wStr;   // its text, and the text when its requirements are not met
  unsigned short wDisabledStr;
  bool bTarget;          // the cast is aimed at a unit or a place, as 1.17's spells are
  bool bResearch;        // the button researches wTech
  bool bUpgrade;         // the button upgrades wTech, an upgrade
  bool bUnitIcon;        // it gives no icon: wUnit's own (ModUnitArtFrame), set for each game
  unsigned char bAi;     // a computer casts it: 1 + the finder (kModAiFinders), or 0, it does not
};
const ModCardButton *ModCardButtons(int *pnCount);

// The targets a computer's caster looks for, by the names a cast button's "ai" gives: 1.17's
// own finders, each of the spell it chooses targets for (ai_update_unit_spells), and last
// "enemy_cluster", Psionic Storm's without its care for the caster's side, for a spell whose
// splash spares it (AIFindModClusterTarget).
enum { MOD_AI_FINDERS = 20 };
extern const char *const kModAiFinders[MOD_AI_FINDERS];

// A type of the game by name: a mod's by its own name, or one of the original's rows by its
// name (port_typenames.h: "terran_marine", "gauss_rifle"); -1 for none. pszTable is "units",
// "weapons", "flingy", "sprites", "images", "orders", "techs", "upgrades", "sounds" or
// "portraits".
int ModTypeByName(const char *pszTable, const char *pszName);

// A mod's races ("races" in types.json): each plays one of the three races'
// ways, its base, whose number a player of it has in the game's slots, and has its own
// name, start units and units. A player's race is 0 for one of the three, else 1 and up,
// the mod's races in the order of their names.
int ModRaceCount();
const char *ModRaceName(int nRace);   // the name a player sees; "" for none
int ModRaceBase(int nRace);           // 0 zerg, 1 terran, 2 protoss
int ModRaceByName(const char *psz);   // by its record's name or its name; 0 for none

// The race the local player chose (--race), 0 for none; the session keeps it (port_session.h).
void ModRaceChoose(int nRace);

// The race of a single-player game's nIndex'th computer (the order of the races Play
// Custom keeps, byte_526547_g), 0 for one of the three; the session keeps it. The computer
// is seated as its base, and the start positions move the players, so the game gives each
// such race to a computer of its base, in slot order.
void ModRaceChooseComputer(int nIndex, int nRace);

// In game_init, after RulesForGame: the slots' races for the game, from the replay's or
// save's, or for a new game the local player's choice and the computers'.
void ModRacesForGame();

// The slots' races of the replay or save read last, for the game it starts; null for none.
void ModRacesRecorded(const unsigned char *pbRaces, int nSlots);

// A slot's race in the game being played, 0 for one of the three.
int ModRaceOf(int nSlot);

// The unit a player's AI makes and counts where 1.17's AI scripts name nType: a mod's race
// says what stands in for a unit it goes without ("ai" in its record); else nType.
int ModRaceAiUnit(int nPlayer, int nType);
// And the tech or upgrade it researches where they ask for one of 1.17's.
int ModRaceAiTech(int nPlayer, int nTech);
int ModRaceAiUpgrade(int nPlayer, int nUpgrade);
bool ModRacesAny();

// A new game's start units (PlaceStartingUnits): whether the slot's race has its own, in
// place of its base's, and its i-th, a unit and how many; false past the last.
bool ModRaceOwnStart(int nSlot);
bool ModRaceStartUnit(int nSlot, int i, int *pnType, int *pnCount);

// At a new game's start (PlaceGameStart): the units each slot's race goes without are taken
// out of its available units.
void ModRacesWithout();

// A mod's unit that a race names is for that race's players only; any other is for every
// player (TTAllowed).
bool ModUnitForPlayer(unsigned nType, int nPlayer);

// The hooks the game's files keep for the mod data: the image scripts' end and a block of a
// mod's appended (CImage.cpp), and the command cards' buttons (statcmd.cpp).
DWORD ImageScriptsEnd();
bool ImageScriptsAppend(const BYTE *pBlock, DWORD cbBlock);
// The header's offset of a mod's script by its id, from 1000, for CImage::ForceScript; false
// for an id no mod's script has.
bool ModScriptHeader(int id, DWORD *pdwHeader);
void StatcmdModCards(bool bApply);

// A mod's sounds and portraits, what is heard and seen only: in no
// game's state, so there from start-up, their files in the mod's folder. A sound or a
// portrait's videos are named to the game as hxmod\<mod>\<path in its data folder>, under
// 1.17's sound\ and portrait\; ModDataFilePath gives the file on disk for such a name, which
// the game's file reads take (port_stormfile.cpp). False for any other name.
bool ModDataFilePath(const char *pszName, char *pszPath, size_t cchPath);
// A cinematic of the active mod's own in the game's one's place: for 1.17's name
// (smk\terran1.smk), the file on disk with that path in the data folder of the active mod or of
// one it depends on, the active mod's first, by any of the videos' extensions or .smk. False
// when none has one.
bool ModDataVideoPath(const char *pszName, char *pszPath, size_t cchPath);
// The chance in 100 of a portrait's idle video nVariant (0 to 3; 1.17's four portdata.dat
// columns), for statport.cpp and gluBrfPort.cpp: the original's, or a mod's past them.
unsigned char ModPortraitChance(int nVariant, unsigned nPortrait, const unsigned char *const *ppColumns);
// After statport_init and briefing_port_init turn the original's portrait strings into names:
// the names a mod's edits give them, in a game that has the mod.
void ModPortraitsResolved();

// A unit's own command icon and wireframes, from a mod's PNG or GRP files,
// in the console's four groups: cmdicons, wirefram, tranwire and grpwire.
enum ModArtKind { MOD_ART_ICON, MOD_ART_WIRE, MOD_ART_TRANWIRE, MOD_ART_GRPWIRE, MOD_ART_KINDS };
// The frame of nKind's group that shows unit type nType: in a game of a mod with art of its
// own, the mod's frame, added after the group's own or in place of an original unit's; else
// the frame of the unit it looks like. A grpwire frame past that group's own is a wireframe,
// drawn at half its size (PortConsoleSmallWire).
unsigned ModUnitArtFrame(int nKind, unsigned nType);
// Whether frame nFrame of nKind's group is a mod's, which Remastered's HD sets do not have.
bool ModArtOwnFrame(int nKind, unsigned nFrame);
// pLoaded, the group of nKind the console has just read (statcmd_init, statdata_init,
// statwire_init), or in a game of a mod with art of its own a group with the mod's frames,
// pLoaded freed. The group returned is freed as pLoaded would have been.
GROUP *ModConsoleGroup(int nKind, GROUP *pLoaded);

#endif
