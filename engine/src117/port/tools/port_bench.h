// port_bench.h -- the benchmark's runs (port_bench.cpp): each second's readings, the
// settings, the overlay's choices and the run's game.
#ifndef PORT_BENCH_H
#define PORT_BENCH_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types
#include "../loop/port_perf.h"

// A benchmark run's log (port_bench.cpp): PortBenchWant asks for one before the
// game starts (it turns on HYPERION_PERF's numbers); PortBenchStart makes the run's folder
// under benchmarks\ and writes its settings (a JSON object); each shown frame and each
// second's numbers are added; PortBenchEnd writes the summary. PortBenchHistory gives the
// seconds so far, oldest first.
struct PortBenchSecond {
  double dFramesPerS;
  double dGameFramesPerS;
  unsigned int nUnits;
  double adPartMs[PERF_PARTS];
};
void PortBenchWant(void);
bool PortBenchWanted(void);
bool PortBenchRunning(void);
const char *PortBenchFolder(void);
bool PortBenchStart(const char *pszName, const char *pszSettingsJson);
void PortBenchShown(void);
void PortBenchAddSecond(const PortBenchSecond &s, double fSeconds);
int PortBenchHistory(const PortBenchSecond **ppSeconds);
void PortBenchEnd(const char *pszReason);
// The run's overlay (port_benchview.cpp): the shown frames a quarter second at a time,
// each with its game frame since the run's start (PortBenchBuckets); the worst 1 % of shown
// frames so far, in ms; the run's seconds less its paused ones, and its game frames; the last
// lines of seconds.csv. PortBenchPauseSet holds the clock and the numbers while the game is
// paused, so paused time is left out of every one.
struct PortBenchBucket {
  float fSeconds;
  unsigned int nFrame;
  float fMeanMs, fMaxMs;
};
int PortBenchBuckets(const PortBenchBucket **ppBuckets);
double PortBenchWorstMs(void);
double PortBenchSeconds(void);
unsigned int PortBenchGameFrame(void);
int PortBenchTail(const char *const **ppLines);
bool PortBenchPaused(void);
void PortBenchPauseSet(bool bPaused);
// A benchmark run's overlay (port_benchoverlay.cpp): the layout, a readout's corner and
// size, a strip's edge, how much shows, a readout's figures, and the panels' see-through.
enum {
  PORT_OV_PLAYERS = 1, PORT_OV_SELECTION = 2, PORT_OV_SETTINGS = 4, PORT_OV_GRAPH = 8, PORT_OV_PARTS = 16,
  PORT_OV_MINIMAP = 32, PORT_OV_ALL = 63
};
enum {
  PORT_FIG_FPS = 1, PORT_FIG_LOW = 2, PORT_FIG_MS = 4, PORT_FIG_GFPS = 8, PORT_FIG_UNITS = 16, PORT_FIG_CLOCK = 32,
  PORT_FIG_VS = 64, PORT_FIG_ALL = 127
};
enum { PORT_BENCH_STYLES = 7 };
struct PortBenchOverlay {
  int nLayout = 0;               // 0 the panels, 1 a readout, 2 a strip, 3 nothing
  bool bLarge = false;           // a readout's large size (Large readings)
  int nCorner = 1;               // a readout's: 0 top left, 1 top right, 2 bottom left, 3 bottom right
  int nEdge = 1;                 // a strip's: 0 the top, 1 the foot
  int nDensity = 0;              // 0 full, 1 compact, 2 the figures only, 3 the boxes ticked
  unsigned nShow = PORT_OV_ALL;  // the boxes, PORT_OV_*
  unsigned nFigures = PORT_FIG_ALL;
  int nSee = -1;                 // the panels' see-through in percent; -1 the picture's own
};
struct PortBenchOverlayChange {
  unsigned nFrame;
  PortBenchOverlay overlay;
  char szHow[16];  // "key" for O, "dialog" for the Overlay dialog
};
// The functions over it are in port_benchoverlay.h.

// The run itself: the game made as the options ask, the local player one of the computers,
// stopped at its first frame, its replay written with the local slot a computer, the run's
// length and no commands (PortBenchPatchHeader, from write_replay), then played by the menus
// (PortBenchLaunch). --bench ends the program after; a set-up screen's run (PortBenchFromMenus)
// goes back to the menus, PortBenchLastRun its folder. nSpeed is 1.17's 0 to 6, nSpeedMul
// Fastest's multiple, 1 to 16 or 0 for flat out; szRaces a race a seat ('r' random), not szRace.
struct PortBenchSettings {
  char szMap[260];
  int nPlayers = 8;
  char szRace[32] = "random";  // the computers' race
  char szRaces[72] = "";
  int nSeed = 1;
  ULONG dwFrames = 14400;
  int nSpeed = 6;
  int nSpeedMul = 1;
  bool bFramesCsv = false;  // every shown frame's time written to frames.csv
  // shown in a window with the overlay round it (0), full screen with it over the game (1),
  // or no picture, the overlay alone once a second (2); the frames shown as many as it can (0)
  // or held to the display (1); the GPU's drawing (0) or 1.17's 256 colours (1)
  int nShown = 0;
  int nFramesShown = 0;
  int nPicture = 0;
  PortBenchOverlay overlay;  // what the overlay shows and where (port_benchoverlay.cpp)
  char szAgainst[260] = "";  // the earlier run's folder this one is held against, "" for none
  // the game type: Free For All (0), every computer for itself, or 1.17's Melee (1), where a
  // single-player game puts every computer on one side, so a run's computers are all allies
  int nGameType = 0;
  // teams (port_teams.h): a team a player, the local slot first ("1,1,2,2"), "" for none; the
  // map's start each plays from, -1 random; the layout's name; each player's colour, #rrggbb
  char szTeams[260] = "";
  char szStarts[260] = "";
  // the computers' levels (port_ai.h), a letter a player, the local slot first ("c,7,v"), ""
  // for every one Classic
  char szAiLevels[260] = "";
  char szLayout[64] = "";
  char szColours[600] = "";  // 64 players' #rrggbb
  // a stress run: each computer given nArmy units at the first frame, the pools
  // scaled by nPools when that is more than the players take; 0 for neither
  int nArmy = 0;
  int nPools = 0;
  // the game's mutators as --mutators lists them (port_mutators.h), "" for none: the set-up's
  // supply cap and unit limit, or the command line's
  char szMutators[260] = "";
  PortBenchSettings() { szMap[0] = '\0'; }
};
// A stress run's armies and pools: the run's own while it is made, the replay's STRS
// while one plays (port_hxreplay.cpp), none in any other game. The pools follow it as the game
// starts (port_pools.cpp), and PortBenchFrame gives the armies at the first frame: nArmy units
// a computer by its start location, as Create Unit makes them, a mix of the three races'
// fighters so a computer's own supply is not used up by them.
struct PortBenchStress {
  int nArmy = 0;
  int nPools = 0;
};
PortBenchStress PortBenchStressGame(void);
// Whether a new game is a stress run, which makes it a game by Hyperion's rules (port_rules.cpp).
bool PortBenchStressChosen(void);
// The replay's STRS, as it is read; NULL before a replay is read, for none.
void PortBenchStressRecorded(const PortBenchStress *pStress);
// The speed as settings.json names it: slowest to fastest, x2 to x16, or max.
const char *PortBenchSpeedId(const PortBenchSettings &s);
// The shown mode as settings.json and --bench-shown name it: window, full or none.
const char *PortBenchShownId(const PortBenchSettings &s);
// port_perf.cpp's parts by their names in the log.
const char *PortBenchPartName(int i);
bool PortBenchFromMenus(const PortBenchSettings &s);
// A game of an AI lab's worker that takes its games from the lab: made and played as a
// set-up screen's run is, but left at its end rather than held under its summary, and the
// program goes on to the lab's next game (PortLabWorkerNext) rather than ending.
bool PortBenchLabGame(const PortBenchSettings &s);
const PortBenchSettings *PortBenchCurrent(void);
const char *PortBenchLastRun(void);
int PortBenchLaunch(void);
bool PortBenchMaking(void);
bool PortBenchPlaying(void);
void PortBenchPatchHeader(void *pHeader, unsigned cbHeader);
// A made game's replay header (write_replay): its local slot a computer's, with no network id,
// and its length in game frames. A benchmark's run and a watched skirmish both play one.
void PortBenchPatchLocal(void *pHeader, unsigned cbHeader, DWORD dwLength);
void PortBenchFrame(void);
void PortBenchAfterGame(void);
void PortBenchReplayStarts(void);
bool PortBenchFast(void);
// The run's speed now (1.17's 0 to 6 and Fastest's multiple, 0 flat out), the set-up's until the
// replay's Slower and Faster step it (order_replay_slowdown, order_replay_speedup: the HUD's
// buttons, D and U) through Slowest to Fastest, x2 to x16 and as fast as it goes. Each step is
// kept with its game frame, and summary.json lists them (speed_changes); a restart plays at the
// set-up's speed again.
void PortBenchSpeedNow(int *pnSpeed, int *pnMul);
void PortBenchSpeedStep(int nDelta);
int PortBenchSeed(void);
// Why the run ended ("" while it runs: "finished" or "stopped"); Stop ends it where it is
// and holds the game under its summary, as the run's length does from the menus; Restart ends
// it and plays the same game again in a folder of its own; Leave ends it and goes back to the
// menus, to the set-up (0) or its runs (1), which PortBenchBackTo then says.
const char *PortBenchEndReason(void);
const char *PortBenchBuild(void);  // the build's commit, as settings.json has it
void PortBenchStop(void);
void PortBenchRestart(void);
void PortBenchLeave(int nBackTo);
int PortBenchBackTo(void);
bool PortBenchFromMenusRun(void);
// No picture (nShown 2): the game is not drawn, and PortBenchPictureDue is true once a second
// for the overlay to be shown.
bool PortBenchNoPicture(void);
bool PortBenchPictureDue(void);

#endif
