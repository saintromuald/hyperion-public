// port_score.h -- what the game tells the score screen (port_score.cpp; docs/modules/
// gameplay.md, "The score"). The game's own code calls these, so this header holds no more
// than the hooks; the score as the screen reads it is port_scoregame.h's.
//
// A game starts (game.cpp, after sub_461E00): the times players went out are forgotten. Each
// frame the game runs (gameloop.cpp): a slot whose victory state has just left 0 is noted
// with the game's clock, the "out at" 1.17 does not keep. The local player leaves (Quit
// Mission, opt_dlgs.cpp): noted. The game ends (main_game_run, before free_game): who played,
// the four tabs' numbers and, when the player left, the clock then are kept. The score screen
// (do_score_dlg) then hands over what 1.17 worked out at its end. Presentation only: these
// read the game's tables and write none of them.
#ifndef PORT_SCORE_H
#define PORT_SCORE_H

void PortScoreGameStart(void);
void PortScoreFrame(void);
void PortScoreLeft(void);
void PortScoreGameEnd(void);

// What do_score_dlg knows at the end: the local player's result as sub_477270 gives it (1 a
// victory, 2 a defeat, 3 a draw, 4 disconnected), whether the game was a replay, whether its
// replay may be saved (1.17's Save Replay greys itself for a replay, a loaded game, a
// campaign mission and a spawned copy), and the elapsed seconds 1.17 shows.
struct ScoreEnd {
  int nLocal;
  bool bReplay;
  bool bSaveable;
  int nSeconds;
};

#endif
