// uiMulti.h -- declarations for src117/lang/uiMulti.cpp, the multiplayer lobby
// screen's actions. No explicit __fastcall: /Gr already makes it the default.

#ifndef OC117_UIMULTI_H
#define OC117_UIMULTI_H

#include "platform.h"

// 0x4BB310 (5 B). A tail jump to 0x4523A0: ECX is the slot and EDX a BYTE, the
// progress. Void; recv_net_cmd_download_progress tail-jumps here.
void sub_4BB310(int nSlot, BYTE bProgress);

// The rows below that take an int take a player or slot index, not a dialog
// control, and their callers read the full EAX.

// 0x4BAD10 (356 B). Nullary.
BOOL uiMulti_OnInitData();

// 0x4BAE80 (30 B). ECX is a BYTE, the force, which both callees take as a BYTE.
BOOL sub_4BAE80(BYTE bForce);

// 0x4BAEA0 (11 B). ECX and EDX are BYTEs, the current and the new race, passed
// on untouched through sub_474EF0 to create_net_cmd_change_race; returns 1.
BOOL sub_4BAEA0(BYTE bRaceCur, BYTE bRaceNew);

// 0x4BAEB0 (30 B). ECX is a BYTE, the team, which both callees take as a BYTE.
BOOL sub_4BAEB0(BYTE bTeam);

// 0x4BAED0 (137 B). Nullary.
BOOL StartNetGame();

// 0x4BAF60 (12 B). Nullary; it ends in a tail jump to end_countdown.
BOOL sub_4BAF60();

// 0x4BAF70 (16 B). ECX is passed on; the body ends in a tail jump to
// DeleteCharacterFile, whose result gluLogin_delete_id reads whole.
int remove_character(intptr_t nArg0);

// 0x4BAF80 (5 B). A tail jump that forwards ECX.
BOOL sub_4BAF80(intptr_t nArg0);

// 0x4BAF90 (11 B). Nullary.
BOOL sub_4BAF90();

// 0x4BAFA0 (56 B). ECX is an int.
BOOL Begin_BNET(int nArg0);

// 0x4BB000 (119 B). ECX is the player.
int BanPlayer(int nArg0);

// 0x4BB080 (88 B). ECX is an int.
int sub_4BB080(int nArg0);

// 0x4BB0E0 (421 B). ECX is an int.
BOOL sub_4BB0E0(int nArg0);

// 0x4BB290 (88 B). ECX is an int.
int sub_4BB290(int nArg0);

// 0x4BB2F0 (28 B). ECX and EDX are 32-bit values, not pointers.
BOOL sub_4BB2F0(int nArg0, intptr_t nArg1);

// 0x4BB320 (24 B). Nullary.
void sub_4BB320();

// 0x4BB340 (24 B). Takes the seconds byte in ECX and passes it on untouched to
// updateChrmCountdown, which reads CL.
void update_countdown(BYTE bSecs);

#endif  // OC117_UIMULTI_H
