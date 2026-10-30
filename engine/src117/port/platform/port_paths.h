// port_paths.h -- the player's data folder and its log files, Hyperion's command line options and
// the map folders --maps adds (port_paths.cpp).
#ifndef PORT_PATHS_H
#define PORT_PATHS_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// The options Hyperion's command line adds (port_paths.cpp), each empty when not
// given. The files and folders are full paths.
#define PORT_MAP_FOLDERS 8
struct PortOptions {
  char szData[260];         // --data <folder>
  char szInstall[260];      // --install classic|remastered: this run's game files (port_install.cpp)
  char szInstallFolder[260];  // --install-folder <folder>: that install's folder, for this run
  char szReplay[260];       // --replay <file>
  char szMap[260];          // --map <file>
  char szLoad[260];         // --load <file>, a saved game
  char szRace[260];         // --race zerg|terran|protoss|random
  char szComputers[260];    // --computers <count>
  char szView[260];         // --view <width> or <w>:<h>, the view's size in a game
  char szCanvas[260];       // --canvas[=<tileset>], Debug Tools' canvas; "badlands" when bare
  char szControl[260];      // --control[=<port>], the live channel (port_control.h); "0" when bare
  char szSeed[260];         // --seed <n>, new_game's seed in place of the clock's
  char szRecord[260];       // --record <frames>[,<path>], port_record.cpp
  char szRecordOrders[260]; // --record-orders <frames>, the local player's orders while recording
  char szColour[260];       // --colour <slot>:<#rrggbb>[,...], slots' colours in full RGB (port_teamcolor.cpp)
  char szTeams[260];        // --teams <t,t,...>: a team a player, the local player first (port_teams.h)
  char szStarts[260];       // --starts <s,s,...>: a start location a player, -1 for random (port_teams.h)
  char szAiLevels[260];     // --ai-levels <l,l,...>: a computer level a player, the local player first (port_ai.h)
  char szHandicaps[260];    // --handicaps <n,n,...>: a handicap in percent a player, the local player first (port_handicap.h)
  char szMutators[260];     // --mutators <preset|name=value,...>: the game's mutators (port_mutators.h)
  char aszMaps[PORT_MAP_FOLDERS][260];  // --maps <folder>, once per folder, each ending in a backslash
  int nMaps;
  bool bUms;                // --ums: --map's game is Use Map Settings, not melee
  char szRules[260];        // --rules classic|hyperion, a new game's rules (h/hxrules.h)
  char szMod[260];          // --mod <id>, the mod whose data loads, in place of the "mod" setting
  char szModCheck[260];     // --mod-check <id> [<report.txt>]: that mod's check, and no game (port_modcheck.cpp)
  char szModCheckReport[260];
  char szComputerRace[260]; // --computer-race <race>, the computers' race, a mod's too
  bool bBench;              // --bench: --map's game as a benchmark run (port_bench.cpp)
  bool bTrainer;            // --trainer: the game a trainer session (port_trainer.cpp)
  char szPlayers[260];      // --players <n>, a benchmark's players, every one a computer
  char szBenchFrames[260];  // --bench-frames <n>, how long a benchmark runs, in game frames
  char szBenchSpeed[260];   // --bench-speed fastest|x2|x4|x8|x16|max
  char szBenchReplay[260];  // --bench-replay <run folder>, a run's replay played again as a run
  char szBenchRaces[260];   // --bench-races <letters>, a race for each seat: t, z, p or r
  bool bBenchFrames;        // --bench-frames-csv: every shown frame's time to frames.csv
  char szBenchShown[260];   // --bench-shown window|full|none: the run in a window, full screen or no picture
  // the run's overlay (port_benchoverlay.cpp): --overlay panels|readout|strip|off, --overlay-at a
  // readout's corner (tl|tr|bl|br) or a strip's edge (top|bottom), --overlay-size large, --overlay-density
  // compact|figures, --overlay-show the boxes (players,selection,settings,graph,parts,minimap or
  // none), --overlay-figures a readout's (fps,low,ms,gfps,units,clock,vs), --overlay-see a percent
  char szOverlay[260];
  char szOverlayAt[260];
  char szOverlaySize[260];
  char szOverlayDensity[260];
  char szOverlayShow[260];
  char szOverlayFigures[260];
  char szOverlaySee[260];
  char szBenchFramesShown[260];  // --bench-frames-shown free|display: as many as it can, or held to the display
  char szBenchPicture[260]; // --bench-picture gpu|256: the GPU's drawing, or 1.17's 256 colours
  char szBenchAgainst[260]; // --bench-against <run folder>: the earlier run this one is held against
  char szBenchType[260];    // --bench-type ffa|melee: every computer for itself (the default), or 1.17's melee
  char szBenchArmy[260];    // --bench-army <n>: a stress run's army, n units a computer at the first frame
  char szBenchPools[260];   // --bench-pools <k>: a stress run's pools, 1.17's times k, up to POOL_SCALE_MAX
  // the LAN host: --room runs a room and no game (port_lanroom.cpp), with --room-key
  // the key its console holds, --room-name and --room-password; --join <address> joins the room
  // there at the start, as --lan-name, and --lan-ready says Ready in its lobby (for a test)
  bool bRoom;
  char szRoomKey[260];
  char szRoomName[260];
  char szRoomPassword[260];
  char szJoin[260];
  char szLanName[260];
  bool bLanReady;
  bool bLanWatch;  // --lan-watch: watches the match being played, once in a room
  char szLanCopy[260];  // --lan-copy <match>:<token>: the room's copy of a match
  char szLanStream[260];  // --lan-stream <device>:<token>: a browser's player's seat on the host
  char szLanTab[260];     // --lan-tab <device>:<token>: a browser's player's seat in the browser
  // the AI lab: --lab-worker <port>:<token>:<worker> makes this a worker of its farm
  // (port_labworker.cpp), playing the game --lab-game numbers into the folder --lab-out names
  char szLabWorker[260];
  char szLabGame[260];
  char szLabOut[260];
  char szAfter[260];  // --after <process id>: started by that copy's Restart now, it waits for it to end (port_instance.cpp)
};

// The player's data folder (port_paths.cpp): Hyperion's own, Data\ in the folder the
// exe runs from (Hyperion\ there in StarCraft from Battle.net's own folder, and before
// 0.6.2's last build) or the one given as --data <folder>, ending in a backslash. LogFile: a log file's path there, in a buffer of the calling thread's
// that the next call reuses. The command line's options. And the command line with
// them taken out, for 1.17's parse_args; the split itself is PortPathsSplitCommandLine.
const char *PortDataFolder(void);
// The folder the program runs from as the player sees it, ending in a backslash: the exe's, and
// on a Mac the folder Hyperion.app is in when the program is an app's (port_posix.cpp), where
// Data\ is, as beside the program elsewhere. Starting another copy of the program takes
// GetModuleFileNameA's path, the program itself.
const char *PortProgramFolder(void);
// Where a file or folder Hyperion's package brings is, by its path in the package's folder
// Data\ ("web\lan", "notes\media\a.png"): in the program folder's Data\ when it is there, as a
// package puts it, else beside the program, as the builds and tools/play.py put it, as packages
// before 0.6.2 did, and as a package puts docs\. In a buffer of the calling thread's that the
// next call reuses.
const char *PortPackagedFile(const char *pszRel);
// Whether 1.17's CD archives (BroodWar.mpq, StarCraft.mpq: the music and the movies) may be
// missing: in a browser, whose page holds only what it needs to play (init.cpp's
// FindCDArchive), and on a StarCraft: Remastered install, which has its own
// music and movies. Without them 1.17 plays without its music and movies.
bool PortCdOptional(void);
const char *PortLogFile(const char *pszName);
// A file only this user may read, written whole (a key, a room's note): elsewhere than Windows
// made 0600 afresh; on Windows the folder's own permissions, which a user's folder keeps to them.
bool PortWritePrivateFile(const char *pszPath, const char *pszText);
// Explorer by its path in the Windows folder, so an explorer.exe beside the program runs in its
// place nowhere; the name alone elsewhere, which the POSIX layer opens with the desktop's opener.
const char *PortExplorer(void);
const PortOptions *PortCommandOptions(void);
char *PortGameCommandLine(void);
void PortPathsSplitCommandLine(const char *pszLine, char *pszGame, size_t cchGame,
                               PortOptions *pOptions);

// The folders of maps given with --maps (port_paths.cpp), and StarCraft from Battle.net's Maps
// folder when its files are the game's, which the map and replay lists show, each standing for
// a "maps" folder: "maps\BroodWar\x.scx" is BroodWar\x.scx in it.
// FindInFolders looks for pszName in each in order, TRUE with its path in pszPath when found;
// its callers look in the game's own two places first. Path gives folder i's path for pszName,
// FALSE when pszName is not under "maps" or the path does not fit; Relative turns a path in one
// of the folders back into its name, in place.
int PortMapFolderCount(void);
int PortMapFolderPath(int i, const char *pszName, char *pszPath, int cchPath);
int PortMapFindInFolders(const char *pszName, char *pszPath, int cchPath);
int PortMapRelativePath(char *pszPath, int cchPath);

#endif
