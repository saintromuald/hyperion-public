#pragma once
// A skirmish watched: every seat a computer, the player's own too, watched as a replay is, at up
// to 16 times Fastest, to see the computers play (the skirmish set-up's "Computer" in your seat).
// The game is made as Start Game makes it and stops at its first frame; its replay, the local
// slot made a computer's and its length left open, then plays with the whole map shown, and holds
// when one player or team is left, the game menu's end box asking to leave or to watch on. When
// the watch is over, LastReplay is the match as far as it was watched, with that length. A
// benchmark's run is made the same way (port_bench.cpp).

// Start Game, before the game is made: the speed (0 to 6) and the replay's multiple (1 to 16)
// the watch starts at.
void PortWatchBegin(int nSpeed, int nMul);
// While the game to be watched is made, up to its first frame.
bool PortWatchMaking(void);
// The made game's replay header (write_replay): the local slot a computer's, the length open.
void PortWatchPatchHeader(void *pHeader, unsigned cbHeader);
// Each game frame (gameloop.cpp): the made game left at its first frame; the watched game's end.
void PortWatchFrame(void);
// After a game (main.cpp): the made game's replay kept for the watch, or the watch over and its
// replay written as LastReplay.
void PortWatchAfterGame(void);
// The menus' start (glues.cpp): the watch's replay started, TRUE when it did.
int PortWatchLaunch(void);
// The replay's start (game.cpp): the whole map shown, and the speed chosen.
void PortWatchReplayStarts(void);
// While the watched game plays, or holds at its end (the HUD's game.observer and replay.live).
bool PortWatchLive(void);
// Who won and when, once the watched game has ended: "Elite Guard wins at 13:50." The game
// menu's end box shows it.
const char *PortWatchResult(void);
