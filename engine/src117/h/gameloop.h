// gameloop.h -- declarations for src117/lang/gameloop.cpp: the per-frame turn,
// the update order the frame runs in, and the outer loop that drives them.
//
// Class members belong in the class header; this file owns the free functions,
// spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_GAMELOOP_H
#define OC117_GAMELOOP_H

#include "platform.h"

// Writes 0 or 1 through pbRedrew; gameloop_update uses the int result.
int DoNetworkTurn(int *pbRedrew);

void update_circle_flashes();

// Returns the running flag, a byte at 0x6663ED, widened to int: the callers
// test the whole register.
int IsGameRunning();

// It tail-jumps map_msg_update.
__declspec(noinline) void gameloop_update_ui();

void sub_445460();

// Copies seven turn rates from pTurnRates into sgTicksPerFrame_g; replay.cpp is
// its one caller.
void sub_445110(DWORD *pTurnRates);

void sub_445470();

// bFlag in ECX, 0 from its one caller. RunGameLoop stores the int result.
int gameloop_update(int bFlag);

// One simulation step, between two set_rand_enabled calls.
__declspec(noinline) void gameloop_update_game();

// Two simulation steps and two UI updates.
void gameloop_advance2();

// The pump that runs until the running flag clears.
void RunGameLoop();

// Returns the return-mode word at 0x666420, which main_game_run reads as 16
// bits.
WORD run_game();

// Callees of other TUs that no other header declares. SaveMinimapCounts is
// net_sync.cpp's, at 0x47A2F0.
void SaveMinimapCounts();
void scroll_update();               // 0x49BF10  scroll.cpp
void sub_470150();                  // 0x470150  net_bnet_msg.cpp
void triggers_loop(int nTicks);     // 0x4BE500  victory.cpp, one ECX argument
void net_mgr_replay_turns();                  // 0x4769B0  net_mgr.cpp
void sub_4B4010();                  // 0x4B4010  tile2.cpp
void cell_fog_update();             // 0x40AF30  cellscrl.cpp

#endif  // OC117_GAMELOOP_H
