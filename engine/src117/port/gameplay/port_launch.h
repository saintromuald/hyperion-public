// port_launch.h -- a game started without the menus, or from Hyperion's own screens: a replay, a
// map, a save, the canvas, a benchmark's, a trainer's or a LAN match's (port_launch.cpp).
#ifndef PORT_LAUNCH_H
#define PORT_LAUNCH_H

// Starts the replay given with --replay, the game given with --map or --canvas, or the
// save given with --load, in place of the menus the first time they come up
// (port_launch.cpp, from RunGlues). TRUE when the game is set to start; FALSE without
// any of them, or when it cannot be played and the menus are to come up as usual.
int PortLaunch(void);

int PortLaunchCanvas(const char *pszTileset);
// A map file played from the menus (the live channel's map_load): Use Map Settings when bUms,
// else a melee game as --map makes one; TRUE when it will start.
int PortLaunchMapFile(const char *pszPath, bool bUms);
// A benchmark's (port_bench.cpp): its game made as --bench makes it, every player a
// computer, and then its replay played; pszMutators is --mutators' list, "" for none.
int PortLaunchBench(const char *pszMap, const char *pszPlayers, const char *pszRace, const char *pszSeatRaces,
                    const char *pszTeams, const char *pszStarts, const char *pszAiLevels,
                    const char *pszType, const char *pszMutators);
int PortLaunchBenchReplay(const char *pszPath);
// A replay watched: a LAN match's as it is so far (port_lanfeed.cpp), a game of the AI
// lab's farm (port_labwatch.cpp).
int PortLaunchWatchReplay(const char *pszPath);
// A trainer session's game (port_trainer.cpp's PortTrainerFromMenus): a melee game on
// pszMap, the player's race, a race a seat (the player's first, then each computer's: t, z, p
// or r), the computers' count, and each for itself or the computers allied.
int PortLaunchTrainer(const char *pszMap, const char *pszRace, const char *pszSeatRaces, int nComputers, bool bFfa);
// A trainer session's replay, loaded as a replay is (the session turns it live as it starts).
int PortLaunchTrainerReplay(const char *pszPath);
// The replays screen's Watch and Load Game's Load (port_front_replays.cpp): the replay or the
// save at pszPath made ready as --replay and --load make one; false when it cannot be, the
// menus left as they were. A save loads in the game the menus are in. The game mode stays the
// menus' until PortLaunchStart, so the screen can fade out first.
bool PortLaunchReplayFile(const char *pszPath);
bool PortLaunchLoadFile(const char *pszPath);
void PortLaunchStart(void);
// run_game tells each game's start, and skips 1.17's tips when it is the canvas or a map the
// live channel loaded. IsCanvas:
// whether the game started last was (the live channel's sandbox commands need it).
bool PortLaunchGameStarts(void);
bool PortLaunchIsCanvas(void);

#endif
