// port_handicap.h -- the handicaps a game is launched with and plays by
// (h/handicap.h for what one does).
//
// A handicap is chosen a row as a computer's level is (port_ai.h): row 0 the local player, row
// 1 + k the k-th computer seated, and the game finds each slot's row wherever the starts put
// it. A room's match names them by seat instead (HandicapChooseNet). Any handicap below 100%
// plays by Hyperion's rules with HX_OPT_HANDICAP, and the replay (HCAP) and the save (HXHC)
// hold each slot's, so the game plays back and loads as it was played.
#pragma once
#include <windows.h>
#include <string>
#include <vector>

// The choices a list offers, 100% first: 100, 90, 80, 70, 60 and 50.
int HandicapChoices();
int HandicapChoice(int nIndex);
// A percent made one of the choices: the nearest step from 50 to 100.
int HandicapClamp(int nPercent);

// A launch's choice, a row at a time, which the session keeps (port_session.h).
void HandicapChoose(int nRow, int nPercent);
// --handicaps: a percent a row, the local player's first, e.g. "100,50,70".
void HandicapChooseList(const char *pszList);
// A room's match (port_lanlobby.cpp): each person's by name and each computer's in the order
// the launch seats them, the same on every seat; forgotten by HandicapForGame.
void HandicapChooseNet(const std::vector<std::pair<std::string, int>> &people, const std::vector<int> &computers);
// RulesForGame, for a new game: whether a player is to have one below 100%.
bool HandicapChosenThisGame();
// As the game starts (game_init, after RulesForGame): the handicaps it plays with, the
// chosen ones for a new game, the recorded ones for a replay or a save with HX_OPT_HANDICAP,
// else 100% for every slot.
void HandicapForGame();

// The replay's and the save's record: a percent a slot, PLAYER_SLOTS_MAX bytes.
// HandicapsRecorded(NULL) forgets the last one read.
void HandicapsSave(BYTE *pbOut);
void HandicapsRecorded(const BYTE *pbIn, int nSlots);
