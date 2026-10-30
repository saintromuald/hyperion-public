// port_scoregame.h -- the score screen's game: who played, how each ended, and the numbers of
// 1.17's four tabs, for every lobby slot of the game (port_score.cpp). The screen is
// port_front_score.cpp's; this is the data and 1.17's rules for it, with no drawing.
#ifndef PORT_SCOREGAME_H
#define PORT_SCOREGAME_H

#include <string>
#include <vector>
#include "port_score.h"

// 1.17's tabs, in its order, and a tab's three columns and its score.
enum { SCORE_OVERVIEW, SCORE_UNITS, SCORE_STRUCTURES, SCORE_RESOURCES, SCORE_TABS };
enum { SCORE_COLUMNS = 3, SCORE_CELLS = 4 };

// How a player's game ended: still playing (or not known), or as gbVictoryState says.
enum { SCORE_PLAYING, SCORE_WON, SCORE_LOST, SCORE_DRAW, SCORE_DROPPED };

struct ScorePlayer {
  int nSlot = 0;
  std::string name;       // UTF-8
  int nRace = 0;          // 0 Zerg, 1 Terran, 2 Protoss
  unsigned rgb = 0;       // the colour the game drew it in, 0xRRGGBB
  bool bComputer = false;
  int nLevel = 0;         // a computer's level (port_ai.h's AiLevel)
  int nTeam = -1;         // in ScoreGame::teams, -1 none
  int nResult = SCORE_PLAYING;
  int nOutAt = -1;        // the game's second its victory state was set, -1 never
  bool bYou = false;
  int anValue[SCORE_TABS][SCORE_CELLS];  // a tab's three columns, then its score
};

struct ScoreTeam {
  std::string name;
  int nIndex = 0;         // its number in the game, from 0: its colour is kTeamBases[nIndex]
};

struct ScoreGame {
  bool bValid = false;
  std::vector<ScorePlayer> players;
  std::vector<ScoreTeam> teams;
  // 1.17's team games make a team one nation, played by its captain: every player of a team
  // shows the captain's numbers, and the team's are the captain's, not a sum
  bool bShared = false;
  std::string map;        // the map's file name, without its folder or extension
  int nGameType = 0;      // the game header's type (0x02 Melee, 0x0F Top vs Bottom, ...)
  std::string type;       // the set-up's name for it, when the front end set the game up
  bool bExpansion = false;
  bool bHyperion = false; // Hyperion's rules
  bool bLeft = false;     // the local player left (Quit Mission)
  int nLeftAt = -1;
  ScoreEnd end = {0, false, false, 0};
};

// The game as it ended, with do_score_dlg's end; bValid false before any game has.
ScoreGame ScoreRead(const ScoreEnd &end);

// The colour the game draws a slot in, as RGB: its full colour, else its minimap dot's; the game
// menu's Alliances shows it too.
unsigned ScoreColour(int nSlot);

// A game made up for a check of the screen's layouts (HYPERION_FRONT_OPEN=score-<preset>), as
// the design's presets have it: "1v1", "ffa8", "4v4", "ffa16", "4x8" or "ffa32"; the numbers
// are drawn from a seed, the first player the local one and its side the winner.
ScoreGame ScoreStandIn(const char *pszPreset);

// 1.17's order (sub_4630C0, by score_row_rank): winners, then the rest, then losers; within
// each the higher score first, then the lower slot. True when a comes before b.
bool ScoreBefore(int nResultA, long long nScoreA, int nSlotA, int nResultB, long long nScoreB, int nSlotB);

// The count-up, 1.17's (sub_4629F0, sub_462F70): a value counts in steps of 1 / ScoreScaleOf
// (16 up to 28, 8 up to 128, else 1, from 256 / value); each 20 ms tick it moves an eighth of what is left,
// never less than 4 steps, to its target, value * scale. The value shown is anim / scale.
enum { SCORE_TICK_MS = 20 };
int ScoreScaleOf(int nValue);
long long ScoreStep(long long nAnim, long long nTarget);

#endif
