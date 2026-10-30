// Teams.h -- declarations for src117/lang/Teams.cpp: the team slot queries the
// lobby's chat receivers and net_slot.cpp reach, and the slot preparation pass.

#ifndef OC117_TEAMS_H
#define OC117_TEAMS_H

#include "platform.h"

// 0x4B2AD0 (55 B) -- the team in ECX, read as 8 bits. int: both consuming
// callers use the full EAX.
int GetTeamFreeSlot(BYTE bTeam);

// 0x4B2BC0 (20 B) -- nullary. BYTE: all ten consuming callers use only AL, and
// with an 8-bit return a 32-bit ALU op inside the callee means nothing.
BYTE GetNumTeams();

// 0x4B2CF0 (43 B) -- the team in ECX, read as 8 bits. int: twelve consuming
// callers use the full EAX.
int IsComputerTeam(BYTE bTeam);

// 0x4B2D90 (27 B) -- nullary. BOOL: every ret materialises 0 or 1 and the
// consuming callers use the full EAX.
BOOL IsTeamGame();

// 0x4B2C00 (225 B) -- the team in ECX and the race in EDX, both 8 bits; BYTE,
// which count_init reads with `movzx ecx, al`. Its neighbour
// team_get_slot_captain @0x4B2BE0 is a Teams.cpp body too, but is still
// declared in src117/h/gametype.h; moving it would need gametype.cpp to include
// Teams.h.
BYTE __fastcall team_num_slots(BYTE bTeam, BYTE bRace);

// 0x4B2930 (82 B) -- the team in ECX, read as 8 bits. BYTE: its one consuming
// caller uses only AL.
BYTE FindTeamCaptain(BYTE bTeam);

// 0x4B2990 (316 B) -- nullary. BYTE: both consuming callers use only AL.
BYTE GetEmptiestTeam();

// 0x4B2D20 (34 B) -- the team in ECX, read as 8 bits. int: four consuming
// callers use the full EAX.
int IsFullPlayerTeam(BYTE bTeam);

// 0x4B2DB0 (932 B) -- nullary, and void: no caller reads a result.
void PrepareTeamSlots();


// 0x4B2B90 (47 B) -- the team in ECX, read as 8 bits. BYTE: its one consuming
// caller uses only AL.
BYTE GetNumTeamOccupiedSlots(BYTE bTeam);

// 0x4B2BF0 (12 B) -- the team in ECX, read as 8 bits. It has no ret of its own:
// the tail jmp to error_get_str pins the return type, `const char *`
// (net_cmds.h:460).
const char *GetTeamName(BYTE bTeam);

// 0x4B2B50 (56 B) -- the team in ECX, read as 8 bits. BYTE: both consuming
// callers use only AL.
BYTE GetNumTeamActivePlayers(BYTE bTeam);

// 0x4B2D50 (53 B) -- the team in ECX, read as 8 bits. int: two consuming
// callers use the full EAX.
int IsNonPlayerTeam(BYTE bTeam);

// nation_find_first 0x4B3160 and nation_find_next 0x4B3170 are declared in
// src117/h/TrigEnum.h, not here, so that each name has one declaration.

#endif  // OC117_TEAMS_H
