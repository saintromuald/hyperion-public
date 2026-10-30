// pause.h -- declarations for src117/lang/pause.cpp: the game's pause flag, the
// per-player pause budget that multiplayer spends, and the pause and unpause
// transitions.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_PAUSE_H
#define OC117_PAUSE_H

#include "platform.h"

// IsGamePaused (net_cmds.h), IsPausedSingleplayer (minimap.h), pause_game and
// unpause_game (TrigAct.h) are declared where their callers live, and not again
// here.

// 0x47CB90 (21 B). Nullary and void; game_ui_init calls it.
void pause_init();

// 0x47CC00 (7 B) and 0x47CC60 (28 B). ECX is the player, an int index. Both
// callers of RemainingPauses read AL only, so it returns a BYTE.
BYTE RemainingPauses(int nPlayer);
void SubtractPauseCount(int nPlayer);

// 0x470710 (6 B). Nullary; five of its six callers read AL only, so it returns a
// BYTE. Its TU is another; it is declared here for player_can_pause.
BYTE get_lobby_state();

// 0x47CB20 (54 B). ECX is the player.
BOOL player_can_pause(int nPlayer);

// 0x47A620 (16 B). ECX is a paused span in seconds, taken out of the banked game
// time while the clock runs. net_time.cpp defines it; unpause_game calls it.
void sub_47A620(int nSeconds);

#endif  // OC117_PAUSE_H
