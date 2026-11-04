// port_newbie.h -- the newbie helpers (the user, 2026-11-04).
#ifndef PORT_NEWBIE_H
#define PORT_NEWBIE_H

// The newbie helpers, once a game frame (gameloop.cpp): with the newbie mode on, workers
// are trained by themselves, mining is balanced by itself, and the upgrades the player
// chose research by themselves. Every action is the same net command the player's own
// click would make, so a replay records them and plays them back; nothing runs in a
// replay (gPlayback).
void PortNewbieFrame(void);

#endif
