// port_session.h -- the choices a single-player game is launched with, kept until the game is
// left (port_session.cpp; docs/modules/gameplay.md, "The session").
//
// The owners' Choose functions write them by row, 0 the local player and 1 + k the k-th
// computer seated: a team and a start (port_teams.h), a computer's level (port_ai.h), a colour
// (playertables.h), a mod's race (port_moddata.h) and a handicap (port_handicap.h); then the
// computers past 1.17's seven, the rules (hxrules.h) and the mutators (port_mutators.h). The
// game reads them as it starts (TeamsSeat, AiSeat, RulesForGame, ModRacesForGame, AiForGame,
// HandicapForGame, MutatorsForGame, PlayersChosenColoursApply) and never clears them, so a restart
// plays the same set-up; SessionAfterGame, when the game ends any other way, and a failed
// launch do, so the menus hold an empty session. This includes playerlimits.h only, because
// main.cpp includes it and declares 1.17's fopen itself; hence a row's level and style are two
// bytes, not an AiProfile.
#ifndef PORT_SESSION_H
#define PORT_SESSION_H

#include "../../h/playerlimits.h"  // PLAYER_SLOTS_MAX

struct SessionRow {
  int nTeam = 0;      // 0 no team, a player for itself; 1 and up a team
  int nStart = -1;    // the map's start of this slot, -1 a random one
  // a computer's level and style (port_ai.h's AiProfile), 0 Classic and its race's own script
  unsigned char bAiLevel = 0;
  unsigned char bAiStyle = 0;
  int nColour = -1;   // one of 1.17's twelve, -1 the slot's own
  bool bRgb = false;  // or a colour in full RGB
  unsigned char abRgb[3] = {0, 0, 0};
  int nModRace = 0;   // a mod's race, 0 none; a computer's only for the first eight
  unsigned char bHandicap = 100;  // the share of its units' life and shields, 50 to 100
};

// A game's mutators, one set for every player (port_mutators.h, h/mutators.h), 1.17's values
// by default. Here because the session holds one and main.cpp includes this.
struct MutatorSet {
  int nMinerals = 100;     // what each mineral field holds, a percent of the map's, 10 to 800; 0 endless
  int nGas = 100;          // and each geyser
  int nStartMinerals = 50; // what each player begins with, 0 to 1,000,000
  int nStartGas = 0;
  int nSupplyCap = 200;    // the most supply a player holds, 1 to 100,000; 0 no cap
  int nWorkers = 4;        // the workers each player begins with, 1 to 12
  int nSupplyBoost = 0;    // a percent more supply from each supplier, 0 to 100 by 25
  int nFog = 0;            // MUTATOR_FOG_*: the fog of war, the map explored from the start, or none
  int nBuildTime = 100;    // build, train, research and upgrade times, a percent of 1.17's, 10 to 400
  int nTech = 0;           // MUTATOR_TECH_*: all to research, all researched, or some forbidden
  int nRich = 0;           // MUTATOR_RICH_*: no rich bases, the bases that may turn rich, or every one
  int nRichChance = 50;    // the chance one that may turn rich does, a percent, 25 to 100 by 25
  int nRichRadius = 48;    // outside a radius: how far from every start, in tiles, 16 to 256, or
                           // when below 0 a share of the map's size, -10 to -75 (-25 a quarter)
  int nRichRules = 3;      // MUTATOR_RICH_FAIR, _AT_LEAST_ONE, _SAME_MAP and _QUIET
  // the unit limit, the most units the game holds at once, every player's and the map's: 0
  // automatic (PoolsForGame), else at least that many, 1,700 to 262,144. Chosen in
  // Game Options, not on the Mutators tab, but held, sent and recorded with the mutators.
  int nUnits = 0;
};

struct PortSession {
  // whether any row's team, start, level or colour was chosen
  bool bTeams = false;
  bool bStarts = false;
  bool bAi = false;
  bool bColours = false;
  bool bHandicaps = false;
  SessionRow aRow[1 + PLAYER_SLOTS_MAX];
  // the computers past 1.17's seven: how many, -1 for one in each open slot past 7 of a
  // Hyperion map; the race of each, 0xFF for bExtraRace
  int nExtraComputers = -1;
  unsigned char bExtraRace = 0;
  unsigned char abExtraRace[PLAYER_SLOTS_MAX];
  int nRules = -1;  // 1 Hyperion's, 0 classic, -1 as --rules or the "rules" setting says
  bool bMutators = false;  // whether the mutators were chosen
  MutatorSet mut;

  PortSession();
};

// The session, for the owners to read and write.
PortSession &Session(void);
// Every choice gone: after a launch that failed (port_launch.cpp, the benchmark's and the
// trainer's), and through SessionAfterGame.
void SessionClear(void);
// main_game_run, a game over: the choices kept for a restart (game mode 5), else cleared.
void SessionAfterGame(int nMode);

#endif
