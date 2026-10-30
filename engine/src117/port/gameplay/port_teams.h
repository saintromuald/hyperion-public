// port_teams.h -- teams a person chooses for a single-player game, and the alliances a game
// with chosen teams starts with (port_teams.cpp).
//
// 1.17 allies by team only in Top vs Bottom, by a slot's nTeam, and its other team games
// make a team one nation. A game whose teams were chosen (the skirmish set-up, --teams)
// keeps 1.17's game type and plays by Hyperion's rules with HX_OPT_TEAMS: each player's
// nTeam is its chosen team, carried through the start's shuffle with its record, and at
// the start every two players on a team are allied with allied victory and share vision,
// and every two players on different teams, or without one, are enemies. A replay keeps
// nTeam in its slot records and the option in its rule record, and plays the alliances
// again from them.
#ifndef PORT_TEAMS_H
#define PORT_TEAMS_H

#include <string>
#include <vector>

// A launch's choice, which the session keeps (port_session.h), by its rows: 0 the local
// player, 1 + k the k-th computer seated. nTeam 0 is no team, a player for itself; 1 and up a
// team.
void TeamsChoose(int nRow, int nTeam);
// --teams <t,t,...>, a team a row in that order, for a game started from the command line.
void TeamsChooseList(const char *pszList);

// Start positions by slot: a row plays from the map's start location of slot nStart, where
// 1.17 gives each player a random one (ShuffleStartSlots); -1 leaves the row's random. A
// person (row 0) plays from one of the first eight, as 1.17 seats people; a start past them
// leaves the row's random. The start's slot is the player's, which the replay's slot records
// keep, so a replay plays from the same starts without the binding. Not a rule: a classic
// game may have it.
void TeamsChooseStart(int nRow, int nStart);
// --starts <s,s,...>, a start a row in that order, -1 for random.
void TeamsChooseStartList(const char *pszList);
// ShuffleStartSlots, for the record that came from lobby slot nFrom: the start it is bound
// to, or -1; and whether a start is bound to some player, for the others to leave it.
int TeamsStartFor(int nFrom, int nType);
bool TeamsStartTaken(int nSlot);

// SinglePlayerMeleeInitGame, its computers seated: each seated player's nTeam from the
// choice, which is then this game's.
void TeamsSeat();
// RulesForGame, for a new game: whether its teams were chosen (HX_OPT_TEAMS).
bool TeamsThisGame();
// game_init, after RulesForGame: a game with chosen teams, new or played back, allied by
// team. A loaded save keeps its own alliances.
void TeamsAlliances();
// The sides still in the game: the players with units, the neutral player left out, a team
// counted once (Hyperion's chosen teams, or 1.17's in a team game). *pnLast, when given, is a
// player of the last side when one is left, else -1. A benchmark's run and a watched skirmish
// end on it.
int TeamsSidesLeft(int *pnLast = nullptr);

// The ways to split a map's starts into teams, for the set-up (the design center's
// 11-front-end/teams.js, layouts): Free for all first; then halves along four lines (north and
// south, west and east, the two diagonals) and edge against centre; then balanced clusters of
// nearby starts for every team count that divides the starts evenly. Each is scored by how
// cleanly its teams sit apart (a silhouette, -1 to 1) and its teams named from where they lie;
// fewer teams first, the best first within a count, the ones that hardly split left out.
struct TeamPoint {
  double x, y;  // a start, across and down the map, 0 to 1
};
struct TeamLayout {
  std::string name;                // "North vs South"
  std::string vs;                  // "2 v 2"
  std::string line;                // what it does, a sentence
  std::vector<int> teams;          // a team a start, from 0
  std::vector<std::string> names;  // a name a team
  double fScore = 0.0;
  int nTeams = 1;
  bool bFfa = false;
};
std::vector<TeamLayout> TeamsLayouts(const std::vector<TeamPoint> &starts);
// A new team at the end of *pNames, named by its number ("Team 3"); its index.
int TeamsAdd(std::vector<std::string> *pNames);
// The teams' sizes, "4 v 4", or past two even teams "4 × 8"; and the score in words.
std::string TeamsVs(const std::vector<int> &teams);
const char *TeamsFit(double fScore);
// A team's colours: n shades of base (0xRRGGBB) in OKLab, lighter and darker by turns with the
// hue turning a little, lightest first. A team's own colour by default is kTeamBases[team].
std::vector<unsigned> TeamsShades(unsigned base, int n);
extern const unsigned kTeamBases[16];

#endif
