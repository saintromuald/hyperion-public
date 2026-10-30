// net_slot.h -- declarations for src117/lang/net_slot.cpp, the lobby's slot table:
// the per-player slot records, the legality predicates over slot values, the
// race-selection mask, the counting family, and the swap and sync the chat-room
// command receivers drive.
//
// Two arrays of 8 records at a 36-byte stride, which the bodies address by their
// end: gPlayerData, the game slots, at 0x6726C8..0x6727F0, and the chat-room slots
// at 0x673D50..0x673E70. A body's array is the `cmp ecx, <base>` that ends its walk.
//
// IsNamedComputerPlayerType (0x479580) is declared in TrigAct.h.

#ifndef OC117_NET_SLOT_H
#define OC117_NET_SLOT_H

#include "platform.h"

// 0x4793B0 (122 B) -- ECX read as 32 bits, EDX never; no stack parameters.
// EAX is written on no path to a `ret`, so the return is void.
void net_slot_init(DWORD dwLocalId);

// 0x4795A0 (9 B) -- ECX read as 8 bits, EDX never. Seven callers use the full
// EAX, and every `ret` materialises 0 or 1.
BOOL IsHumanPlayerType(BYTE bType);

// 0x479630 (210 B) -- ECX read as 8 bits, EDX read before written and also 8
// bits, so two register arguments. EAX at the `ret` is call:net_slot_init's
// leftover and no caller consumes it -> void.
void swap_slots(BYTE bSlotA, BYTE bSlotB);

// The counting family. All return BYTE: every caller uses only AL, and
// CountHumanSlots builds its result with `xor al, al` / `inc al`, never touching
// EAX's upper half.

// 0x478E50 (26 B) -- nullary. Counts gPlayerData[0..7].nType == 6, the same walk
// as CountDefaultHumanSlots (0x479550) with a different return width.
BYTE CountHumanSlots();

// 0x479550 (25 B) -- nullary, the same walk with `xor eax,eax` / `inc eax`.
BYTE CountDefaultHumanSlots();

// 0x478E70 (46 B) -- nullary. Walks the CHATROOM array, counting nType in
// {3,4,5,6,7}.
BYTE CountValidSlots();

// 0x478EA0 (34 B) -- nullary. The chatroom array, nType in {1,5}.
BYTE CountComputerSlots();

// 0x479520 (33 B) -- nullary. gPlayerData, nType in {1,5}.
BYTE CountDefaultComputerSlots();

// The legality and selection predicates.

// 0x4795B0 (34 B) -- ECX 8 bits. Three consuming callers use the full EAX and
// every `ret` materialises 0 or 1 -> BOOL.
BOOL IsLegalGameSlotVal(BYTE bVal);

// 0x4795E0 (34 B) -- ECX 8 bits, eleven consuming callers, same 0/1 shape.
BOOL IsLegalChatroomSlotVal(BYTE bVal);

// 0x479610 (19 B) -- ECX 8 bits. Six consuming callers use the full EAX.
int is_race_selectable(BYTE bRace);

// 0x478A20 (16 B) -- ECX 8 bits; EAX written on no path to a `ret` -> void.
void set_selectable_race(BYTE bRace);

// 0x479570 (16 B) -- the whole body is memcpy(ECX, 0x673F08, EDX), so ECX is
// memcpy's destination and EDX its count. EAX at the `ret` is memcpy's leftover.
void get_selectable_races(void *pDst, size_t nLen);

// 0x479C80 (12 B) -- nullary, void.
void clear_selectable_races();

// 0x479C90 (37 B) -- ECX 8 bits. The body tail-jumps to set_rand_enabled, and
// its three consuming callers use the full EAX -> int.
int slot_set_race(BYTE bRace);

// The slot machinery.

// 0x4794E0 (57 B) -- nullary; three consuming callers use the full EAX -> int.
int FindLocalSeatSlot();

// 0x479430 (171 B) -- nullary; one consuming caller uses the full EAX -> int.
int ShuffleForceStartSlots();

// 0x479340 (105 B) -- nullary, void.
void AssignGameSlots();

// 0x479710 (69 B) -- nullary; is_race_selectable(FindLocalSeatSlot()) with both
// inlined. It returns int: its exits are `xor eax, eax / ret` and a `movzx` pair,
// where a BYTE return would be `mov al` and `xor al, al`, though its one caller,
// gluCreate_computerSlots_update, discards EAX.
int IsLocalSeatRaceSelectable();

// 0x479BF0 (65 B) -- nullary, void.
void SyncSlots();

// 0x479C40 (57 B) -- ECX 8 bits, void.
void ResetSlotFromLobby(BYTE bArg0);

// 0x47A000 (46 B) -- nullary, void.
void ResetLobbySlotsOpen();

// 0x479CC0 (29 B) -- ECX is dereferenced, so a pointer; EDX is set by callers and
// never read, so its int is a placeholder.
void set_selectable_races(void *pArg0, int nArg1);

// 0x478C50 (218 B) -- ECX 8 bits, EDX 32 bits.
void sub_478C50(BYTE bArg0, int nArg1);

// 0x478D30 (280 B) -- ECX 8 bits, EDX 32 bits.
void sub_478D30(BYTE bArg0, int nArg1);

// 0x479760 (218 B) -- ECX 8 bits, EDX 32 bits.
void sub_479760(BYTE bArg0, int nArg1);

// 0x47A030 (259 B) -- ECX and EDX both 32 bits. Returns int: its one caller
// stores only AL, but a BYTE return keeps nPick in a byte register (EBX).
int sub_47A030(int nArg0, int nArg1);

// 0x479A00 (485 B) -- ECX is an 8-element BYTE mask: the body dereferences it and,
// when it is NULL, uses a local array of eight 1s. EAX at the `ret` is
// net_slot_init's leftover and no caller consumes it.
void ShuffleStartSlots(BYTE *pMask);

// 0x478A30 (544 B) -- nullary, void.
void AssignTeamsBySlot();

// 0x478EE0 (455 B) -- nullary, void.
void AssignRandomRaces();

// 0x4790B0 (649 B) -- nullary, void.
void SeatLoadedGameSlots();

// 0x479840 (435 B) -- nullary, void.
void sub_479840();

// 0x479CE0 (789 B) -- nullary, void.
void sub_479CE0();

#endif  // OC117_NET_SLOT_H
