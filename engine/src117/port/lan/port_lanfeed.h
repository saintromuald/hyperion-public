// port_lanfeed.h -- a LAN match's replay as it is played: each seat sends the room the
// game's record as it grows, and a Hyperion watching plays it as a replay, the room's delay
// behind the players. Presentation and transport only: a watched match is a replay.
#ifndef PORT_LANFEED_H
#define PORT_LANFEED_H

#include <stdint.h>
#include <string>
#include <vector>

// A seat's side, from port_lanlobby.cpp: each frame of its game, and its end.
void LanFeedSeatFrame(int nMatch, uint32_t dwFrame);
void LanFeedSeatEnd(void);

// Watching, from the screens: the match asked for, and its replay to start once the room has
// sent its beginning (port_front.cpp starts it as a mode's game).
void LanWatchAsk(int nMatch, const std::string &name);
// The room's copy of a match (--lan-copy <match>:<token>): the room has made it a watcher, with
// no delay; it sends a picture of each player's army in turn, and goes when the replay ends.
void LanWatchAsCopy(int nMatch);
// A fight the room's copy saw at the live edge, about to come in the watched picture: where it
// is in map pixels and its heat (Replay TV's director, port_tv.cpp).
bool PortLanWatchAhead(int *pnX, int *pnY, float *pfHeat);
void LanWatchCancel(void);
bool LanWatchWaiting(void);  // asked, its beginning not yet come
bool LanWatchReady(void);    // its beginning here: the game can start
int LanWatchLaunch(void);    // starts it, as Replays' Watch does; FALSE when it cannot
// Reads what the room sent (the screens' frames and the game's).
void LanWatchPump(void);
// A browser's watching seat, each frame (port_lanstream.cpp): in a watched match's game, Replay
// TV's director has the camera, and a match the room says to show instead has the game left.
void LanWatchFollowPump(void);

// In the game (gameloop.cpp): whether the replay waits for the feed, and whether it runs at once
// to catch up (nothing drawn). The Live bug and L, Go Live, are a layer over the game
// (port_overlay.h); PortLanWatchShown is whether it shows.
bool PortLanWatchHold(void);
// A watched match's replay is playing: its record is read to what has come (net_record.cpp).
bool PortLanWatchLive(void);
bool PortLanWatchCatching(void);
bool PortLanWatchShown(void);
// A watched match's state hash at a frame (port_stormlan.cpp's HashRecord), to the room.
void PortLanWatchHash(const std::vector<uint8_t> &record);
// After the game: whether it was a watched match, once (the screens go back to the room).
bool PortLanWatchAfterGame(void);

#endif
