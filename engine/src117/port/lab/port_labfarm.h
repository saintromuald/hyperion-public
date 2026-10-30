// port_labfarm.h -- the AI lab's farm: a project's games played by workers the lab
// starts and owns (port_labworker.cpp, one game a process), what they say taken in, and each
// finished game's line in the project's index. The front end and the presenter pump it each
// frame they show, so the farm plays on while the lab shows a game.
//
// A project is <data>lab\projects\<name>\project.json, made with the defaults the first time:
// its map pool, players, levels and races (each game the next of each list), its first seed
// (each game the next) and its length limit. Its games go in games\NNNNNN\ beside it (the
// benchmark's run.rep, settings.json and summary.json), and each finished one is a line of
// games.jsonl. A worker's data folder is <data>lab\workers\wNN.
#ifndef PORT_LABFARM_H
#define PORT_LABFARM_H

#include <stdint.h>
#include <string>
#include <vector>

struct LabProject {
  std::string name;
  std::vector<std::string> maps;    // names or paths, found as the live channel's map_load finds them
  int nPlayers = 2;
  std::vector<std::string> levels;  // --ai-levels of a game, "7,5"; empty for Classic AI
  std::vector<std::string> races;   // --bench-races of a game; empty for random
  // ffa, every computer for itself, or melee, where a single-player game puts every computer on
  // one side (port_bench.h), so they never fight
  std::string type = "ffa";
  int nFirstSeed = 1;
  unsigned dwFrames = 86400;        // the length limit, an hour of game at Fastest; a draw
};

// A unit on the wall: its id, the same from sample to sample so it glides, its place in map
// pixels, its player and its kind (LabProto's UNIT_ bits).
struct LabDot {
  uint32_t dwId;
  uint16_t x, y;
  uint8_t bPlayer, bKind;
};

// A player of a game: who, from GAME_START, and its bank and army from the last sample.
struct LabSeat {
  int nSlot = 0, nRace = 0, nTeam = 0, nLevel = 0;
  std::string name;
  uint32_t dwMinerals = 0, dwGas = 0;
  int nSupply = 0, nSupplyMost = 0, nUnits = 0;
  bool bOut = false;
};

struct LabSample {
  unsigned dwFrame = 0;
  uint64_t qAt = 0;  // PortNowMs when it came
  std::vector<LabDot> dots;
};

enum LabSlotState { LAB_IDLE, LAB_STARTING, LAB_PLAYING, LAB_ENDING };

// A worker of the farm and the game it plays, as the screen shows it.
struct LabSlot {
  int nWorker = 0;  // 1 up
  LabSlotState state = LAB_IDLE;
  // the game it was given
  uint32_t dwGame = 0;
  std::string map;  // its path
  std::string levels, races;
  int nSeed = 0;
  // the game it shows, from its GAME_START, kept while the next one starts
  uint32_t dwShown = 0;
  std::string shownMap;
  int nMapW = 0, nMapH = 0;  // tiles
  unsigned dwFrame = 0;      // the game's, from its last message
  int nFps10 = 0;            // game frames a second x10, from STATUS
  int nUnits = 0;
  std::vector<LabSeat> seats;
  LabSample prev, last;  // the last two samples, their units by id, which the wall glides between
  unsigned nSamples = 0;
  uint64_t qEndAt = 0;   // PortNowMs of its GAME_END, 0 before
  std::string endReason;  // finished, last
  std::vector<int> winners;
};

// Starts the current project's games, nAtOnce at a time (0 for PortLabFarmAtOnceDefault) and
// nGames of them (0 until stopped). False, and why, when the project has no map this install
// finds or the lab cannot listen.
bool PortLabFarmStart(int nAtOnce, int nGames, std::string *pWhy);
// Every worker ended at once; their games go in the index as stopped.
void PortLabFarmStop(void);
bool PortLabFarmRunning(void);
int PortLabFarmAtOnce(void);
// Games at once from now: a worker past it finishes its game and plays no other.
void PortLabFarmSetAtOnce(int n);
// The machine's cores less two, at most 24, at least 1.
int PortLabFarmAtOnceDefault(void);
// Games ended (their GAME_END) since the farm started, and so many an hour, 0 for its first
// half minute.
int PortLabFarmPlayed(void);
int PortLabFarmGamesAnHour(void);
const LabProject &PortLabFarmProject(void);
// The project the farm plays, or will when started: its folder's name, and its project.json,
// made with the defaults when it is not there.
std::string PortLabFarmProjectName(void);
std::string PortLabFarmProjectFile(void);
// <data>lab\projects\<name>\, with its backslash.
std::string PortLabFarmFolder(void);
int PortLabFarmSlots(void);
const LabSlot &PortLabFarmSlot(int i);
// Samples a second each worker sends while the wall shows them, 0 for none.
void PortLabFarmSampling(int nRate);
// Each frame the wall is drawn, bSamples when it draws the games' units: the workers send
// samples while it does, and none from half a second after it stops.
void PortLabFarmWallShown(bool bSamples);
// The workers' pace: a multiple of Fastest, 0 for as fast as each goes; and held where they are.
void PortLabFarmPace(int nMultiple, bool bPaused);
int PortLabFarmPaceNow(void);
bool PortLabFarmPaused(void);
// A game watched live in the lab (port_labwatch.cpp): its worker plays as fast as it can up to
// dwAllowed and waits there, whatever the farm's pace, and sends its state every 240 frames;
// bOn false gives it back to the farm's pace.
void PortLabFarmWatch(uint32_t dwGame, bool bOn, unsigned dwAllowed);
// The state a watched game's worker sent for a frame, if it has (its last 256).
bool PortLabFarmHash(uint32_t dwGame, unsigned dwFrame, uint64_t *pqState);
// Each frame the lab's process shows (port_front.cpp, port_present.cpp): connections taken,
// messages read, ended workers' slots given their next game.
void PortLabFarmPump(void);

#endif
