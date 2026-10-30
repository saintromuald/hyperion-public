// port_ai.h -- the computer player's boundary (docs/modules/gameplay.md).
//
// The engine reaches 1.17's AI only through these. Each is given 1.17's own value and gives it
// back for a computer at Classic, so a game whose computers are all Classic plays as 1.17
// does, to the frame. A profile is a level and a style, chosen a row as teams are
// (port_teams.h): row 0 the person (the benchmark's local seat, which plays as a computer),
// row 1 + k the k-th computer seated; AiSeat gives each seated slot its row's. The levels are
// StarCraft II's beside Classic (PRD 2026-10-16). Any level but Classic plays by Hyperion's
// rules with HX_OPT_AI_LEVELS, and the replay and save hold each slot's profile
// (AiProfilesSave), so the game plays back and loads as it was played.
#pragma once
#include <windows.h>
#include <string>

class CUnit;

enum AiLevel {
  AI_LEVEL_CLASSIC,         // 1.17's own computer
  AI_LEVEL_VERY_EASY,
  AI_LEVEL_EASY,
  AI_LEVEL_MEDIUM,
  AI_LEVEL_HARD,
  AI_LEVEL_HARDER,
  AI_LEVEL_VERY_HARD,
  AI_LEVEL_ELITE,
  AI_LEVEL_CHEATER_VISION,  // Elite, and sees the whole map
  AI_LEVEL_CHEATER_MONEY,   // Elite, sees the whole map and brings back more, 1.5 times at 20 minutes
  AI_LEVEL_CHEATER_INSANE,  // Elite, sees the whole map and brings back 1.2 times, 2.0 at 20 minutes
  AI_LEVELS
};

enum AiStyle {
  AI_STYLE_OWN,  // its race's own script, as 1.17 starts it
  AI_STYLES
};

struct AiProfile {
  BYTE bLevel = AI_LEVEL_CLASSIC;
  BYTE bStyle = AI_STYLE_OWN;
};

// A level's or a style's name as the menus show it, its letter on the command line
// (--ai-levels), and whether it can be played yet.
const char *AiLevelName(int nLevel);
const char *AiStyleName(int nStyle);
char AiLevelLetter(int nLevel);
int AiLevelFromLetter(char c);  // -1 for none
bool AiLevelReady(int nLevel);
// --ai-levels' letters named: each level once, in the players' order ("Elite / Very Easy"),
// or "" when every one is Classic.
std::string AiLevelsLabel(const char *pszLetters);

// A launch's choice, a row at a time, which the session keeps (port_session.h).
void AiChoose(int nRow, int nLevel, int nStyle);
// --ai-levels: a letter a row, the local player's first (AiLevelLetter), e.g. "c,e,i".
void AiChooseList(const char *pszList);
// As SinglePlayerMeleeInitGame seats its players (beside TeamsSeat): each seated slot
// takes its row's profile.
void AiSeat();
// RulesForGame, for a new game: whether a seated slot plays other than Classic.
bool AiChosenThisGame();
// As the game starts (game_init, after RulesForGame): the profiles it plays with, the
// seated ones for a new game, the recorded ones for a replay or a save with
// HX_OPT_AI_LEVELS, else every slot Classic.
void AiForGame();
// A slot's profile in the game being played.
AiProfile AiProfileOf(int nSlot);

// The replay's and the save's record: a level and a style a slot, 2 bytes each, for
// PLAYER_SLOTS_MAX slots. AiProfilesRecorded(NULL) forgets the last one read.
enum { AI_PROFILE_BYTES = 2 };
void AiProfilesSave(BYTE *pbOut);  // PLAYER_SLOTS_MAX * AI_PROFILE_BYTES bytes
void AiProfilesRecorded(const BYTE *pbIn, int nSlots);

// The AI's part of a game frame (gameloop_update_game): 1.17's sai_update.
void AiFrame();
// After the frame's sight refresh puts the fog back (sub_46C180): a slot that sees the
// whole map has it taken off again.
void AiVisionRefresh();
// The script a melee computer in slot nSlot starts (gametype_run_aiscript), given the one
// 1.17 picks for its race; 0 to start none.
DWORD AiMeleeScript(int nSlot, DWORD dwClassic);
// The frames a computer's unit waits before it thinks again (CUnit::DispatchComputerAI),
// given 1.17's for what it just did.
BYTE AiThinkFrames(const CUnit *pUnit, BYTE bClassic);
// The difficulty a computer's scripts see (banker_start_nation), given 1.17's (always 1).
BYTE AiDifficulty(int nPlayer, BYTE bClassic);
// A script's wait opcode, in frames.
DWORD AiScriptWait(int nPlayer, DWORD dwClassic);
// attack_add's count of a unit type.
BYTE AiAttackCount(int nPlayer, BYTE bClassic);
// Whether the script's attacks (attack_prepare, attack_do, quick_attack) go ahead yet; an
// attack ordered before a level's first attack time is let go.
bool AiAttackAllowed(int nPlayer);
// A town's worker target (town_tick).
BYTE AiWorkerTarget(int nPlayer, BYTE bClassic);
// What a worker takes from a resource on top of the load bLoad (CUnit::SetHarvestInventory),
// given 1.17's (1 when banker_worker_bonus_allowed, else 0). The resource gives no more.
BYTE AiHarvestExtra(const CUnit *pWorker, BYTE bLoad, BYTE bClassic);
