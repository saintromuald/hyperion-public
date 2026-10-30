// League.h -- declarations for src117/lang/League.cpp, ladder replay and
// game-result recording (oracle 0x4662C0 through 0x4666B0). Free functions are
// __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_LEAGUE_H
#define OC117_LEAGUE_H

#include "platform.h"

// League.cpp's other four functions are declared elsewhere: league_write_replay
// (0x4662C0) and league_get_game_result (0x466470) in game.h, league_get_name
// (0x466450) and league_load_profile_img (0x466490) in gluBNProfile.h.

// Installs the ladder name: __stdcall, one stack word, which SStrCopy copies
// into the 0x1E-byte name buffer at 0x66D0F8. It returns 1; its one consumer
// is SNETUIDATA::pfnSetLeague, an untyped field.
BOOL __stdcall UiSetLeagueCallback(const char *pszName);

// Writes the ladder game result. ECX is eight scores and EDX eight name
// pointers, and the two stack words are the %s pair the document ends with.
// No caller reads EAX.
void league_write_gameresult(const int *pnResults, const char **ppszNames,
                             const char *pszArg2, const char *pszArg3);

// Allocates and formats the timestamped replay name; the one caller passes it
// to SStrPrintf as a %s and then frees it.
char *get_replay_filename();


#endif  // OC117_LEAGUE_H
