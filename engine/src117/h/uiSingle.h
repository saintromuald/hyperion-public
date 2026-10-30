// uiSingle.h -- declarations for src117/lang/uiSingle.cpp: the single-player
// campaign and custom-game setup screens.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_UISINGLE_H
#define OC117_UISINGLE_H

#include "platform.h"

#pragma pack(push, 1)

// A campaign-menu row, 8 bytes. A run ends on wMission == 0. Nothing in the TU
// reads the first two bytes.
struct campaign_t {
  /*0x0*/ WORD wEntry;
  /*0x2*/ WORD wMission;
  /*0x4*/ WORD wFlic;       // a flic to play in place of a mission, or 0
  /*0x6*/ BYTE nRace;
  /*0x7*/ BYTE bBranch;     // nonzero on a branch of the row before
};

#pragma pack(pop)

static_assert(offsetof(campaign_t, wEntry)   == 0x0, "campaign_t.wEntry");
static_assert(offsetof(campaign_t, wMission) == 0x2, "campaign_t.wMission");
static_assert(offsetof(campaign_t, wFlic)    == 0x4, "campaign_t.wFlic");
static_assert(offsetof(campaign_t, nRace)    == 0x6, "campaign_t.nRace");
static_assert(offsetof(campaign_t, bBranch)  == 0x7, "campaign_t.bBranch");
static_assert(sizeof(campaign_t) == 8, "campaign_t is 8 bytes");

// 0x4BB360 (348 B). Starts the currently selected mission, or parks glGluesMode
// on the screen that explains why it cannot. Nullary; returns 0 or 1, which
// sub_4BB790 discards.
BOOL sub_4BB360();

// 0x4BB4C0 (24 B). Nullary and void.
void reopen_computer_slots();

// 0x4BBAE0 (51 B). The external copy. uiSingle.cpp's callers use the static
// twin sub_4BBAE0_local, the same bytes: they keep word_526524_g in EDX across
// the call, which MSVC does only for an internal callee.
campaign_t *sub_4BBAE0(campaign_t **ppTbl, WORD wMission);

// 0x4BB4E0 (66 B). ECX is a BYTE, the race.
BOOL seat_computer_player(BYTE bRace);

// 0x4BB530 (21 B). ECX is a BYTE, the race. Returns 1, which
// SinglePlayerMeleeInitGame discards.
BOOL set_single_race(BYTE bRace);

// 0x4BB550 (93 B). Nullary; returns 0 or 1, and all three callers read the full
// EAX.
BOOL seat_local_player();

// 0x4BB5B0 sub_4BB5B0 (260 B) is declared in gluCmpgn.h.

// 0x4BB6C0 (203 B). ECX is a WORD, the mission.
BOOL sub_4BB6C0(WORD wMission);

// 0x4BB790 (399 B). ECX is a flag; four of the five callers pass 1.
int sub_4BB790(int bAdvance);

// 0x4BB920 (76 B). Nullary; four callers read the full EAX.
BOOL seat_custom_player();

// 0x4BBB40 (22 B). Nullary and void.
void sub_4BBB40();

// 0x4BB970 (93 B). Nullary and void; it ends in a tail jump to sub_4BBC20.
void sub_4BB970();

// 0x4BB9D0 (79 B). `ret 4h`: ECX and EDX are SStrCmpI's second string and
// length cap; the stack word is the walking index, compared unsigned.
BOOL sub_4BB9D0(const char *pszName, int cchName, DWORD *pnIndex);

// 0x4BBA20 (177 B). ECX is the map file name.
campaign_t *sub_4BBA20(const char *pszFile);

// 0x4BBB20 (22 B). Nullary and void.
void play_next_flic();

// 0x4BBB60 (186 B). `ret 0Ch`: ECX is the text and EDX the race, then the first
// mission, the expansion flag and the DWORD the mission is written to.
int find_campaign(const char *pszText, int nRace, int nFirst, int bExpansion,
                  DWORD *pdwMission);

// 0x4BBC20 (139 B). ECX is a campaign row.
void sub_4BBC20(campaign_t *pRow);

#endif  // OC117_UISINGLE_H
