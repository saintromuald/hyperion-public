// port_labwatch.h -- a game of the AI lab's farm watched in the lab: its replay played
// in SD art at Fastest, Show All and Replay TV on, a panel with its players over it, and at the
// frame its worker's game ended the state compared with the worker's (port_labwatch.cpp).
#ifndef PORT_LABWATCH_H
#define PORT_LABWATCH_H

#include <stdint.h>
#include <string>

struct PortJson;

// A game of the project to watch: its number and its map's file ("" when not known, read from
// games.jsonl then); bLive, a game still playing, joined where its worker is, which then plays
// only as far ahead of the watcher as it allows. False, and why, when its replay cannot be
// copied.
bool PortLabWatchAsk(uint32_t dwGame, const std::string &map, std::string *pWhy, bool bLive = false);
// The lab's screen, each frame it is drawn: whether a game was asked for (its wall, its games or
// lab_watch), so it hands the front end FRONT_LAB_WATCH.
bool PortLabWatchAsked(void);
// The live channel's lab_watch {game?, live?, to_end?, leave?}: a game asked for while the lab's
// screen shows, live or from its start, with to_end played to its end with nothing drawn
// (to_end alone: the game watched); leave goes back to the lab; and what is watched, with its
// checks.
bool PortLabWatchCommand(const PortJson &j, std::string *pOut);
// PortFrontStartMode for the lab's mode: the game asked for started (TRUE), or FALSE.
int PortLabWatchLaunch(void);
// Each game frame in the lab's process (PortLabFrame's hook in gameloop.cpp): the watched game's
// set-up on its first frame, and at the worker's end frame the state folded and compared.
void PortLabWatchFrame(void);
// Whether the watched game runs as fast as it can with nothing drawn (gameloop.cpp, as a LAN
// match's catching up): asked for with lab_watch's to_end, until the frame it is checked at, and
// a game watched live while it is more than 240 frames behind its worker, until it is 48.
bool PortLabWatchCatching(void);
// Whether the watched game waits before its next turn (gameloop.cpp, beside a LAN match's): at
// its worker's last frame, while the farm still plays it and its end is not known.
bool PortLabWatchHold(void);
// After a game, once: true when it was one watched from the lab (PortFrontCampaign goes home, and
// the lab opens again, PortLabWatchCameBack, which a game left to the main menu reaches first).
bool PortLabWatchAfterGame(void);
bool PortLabWatchCameBack(void);
bool PortLabWatchShown(void);
// Whether a game of the farm is watched live, as its worker plays it (the HUD's game.observer).
bool PortLabWatchLive(void);

#endif
