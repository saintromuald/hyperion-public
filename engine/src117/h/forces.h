// forces.h -- declarations for src117/lang/forces.cpp: the four forces' slots,
// alliance mode, shared vision and names, as the lobby's chat receivers and
// net_slot.cpp reach them.
//
// A force index is 1..4 and is read as 8 bits. The force flag bytes are at
// 0x534897 and the name table at 0x53487E with a 30-byte stride; neither is a
// registered global, so the bodies dual-spell them in-TU.

#ifndef OC117_FORCES_H
#define OC117_FORCES_H

#include "platform.h"

// 0x4433D0 (34 B) -- the force in ECX. BYTE: its one consuming caller uses only
// AL.
BYTE GetForceOpenSlot(BYTE bForce);

// 0x443500 (84 B) -- ECX and EDX both read as 8 bits. BYTE, not void: the body
// materialises AL on both exits (`mov al, 8h` when the loop runs out, `mov al,
// bl` when it finds a slot), and recv_net_cmd_change_force consumes it as the
// same 0..8 slot index GetForceOpenSlot carries.
BYTE GetNextForceOpenSlot(BYTE bArg0, BYTE bArg1);

// 0x443600 (37 B) -- the force in ECX, 8 bits. int: two consuming callers use
// the full EAX.
int IsComputerForce(BYTE bForce);

// 0x443630 (24 B) -- the force in ECX as 8 bits, not a pointer: `lea eax,
// [ecx-1h] / cmp al, 3h` range-checks it to 1..4, then bit 0 of the flag byte
// at 0x534897 is the answer.
int IsForceRandomStartLocations(BYTE bForce);

// 0x443180 (153 B) -- the force in ECX and the mode in EDX, both 8 bits. void:
// EAX at the ret is inherited and no caller consumes it.
void SetForceAlliance(BYTE bArg0, BYTE bArg1);

// 0x443220 (167 B) -- the force in ECX and the enable flag in EDX, both 8 bits,
// the force range-checked to 1..4. For every player slot whose nTeam is this
// force and whose nType is a human type, it sets or clears that slot's bit for
// every other member of the force in SharedVision (0x5264CC, DWORD[12]): the
// force-wide shared-vision setter.
void SetForceVision(BYTE bForce, BYTE bEnable);

// 0x4432D0 (72 B) -- nullary.
void ApplyPlayerForces();

// 0x443320 (161 B) -- ECX is a genuine pointer: `mov eax, dword ptr [ecx]` and
// `[ecx+4h]` are stored into 0x534890 and 0x534894, then `[ecx+10h]` is walked
// backwards as four WORDs into get_map_string. A force-settings blob, `void *`
// until its struct is written.
void LoadForceSettings(void *pArg0);

// 0x443400 (34 B) -- the force in ECX as 8 bits, range-checked to 1..4; the
// answer is the alliance mode in the flag byte at 0x534897. BYTE: the
// out-of-range path is `or al, -1h`, which defines AL only.
BYTE GetForceAllianceMode(BYTE bForce);

// 0x443430 (40 B) -- ECX indexes the 0x1E-stride name table, EDX is SStrCopy's
// destination and the stack word its max length. void: EAX is written on no
// path to a ret.
void GetForceName(int nForce, char *pszDest, int nMaxLen);

// 0x443460 (24 B) -- the force in ECX as 8 bits, range-checked as above; the
// flag tested is bit 3 (`and eax, 8h`). BYTE for the same reason as
// GetForceAllianceMode.
BYTE IsForceVisionShared(BYTE bForce);

// 0x443480 (118 B) -- nullary. BYTE: its one consuming caller uses only AL.
BYTE GetEmptiestForce();

// 0x443580 (58 B) -- ECX is a player index, 0..7: `lea eax, [ecx+ecx*8]` and a
// scale of 4 make the 36-byte slot stride into the player array at 0x6726D2,
// and the byte read there is the force, which drives the same SStrCopy tail as
// GetForceName.
void GetPlayerForceName(int nPlayer, char *pszDest, int nMaxLen);

// 0x4435C0 (55 B) -- nullary. BYTE: one consuming caller uses only AL.
BYTE GetFreeSlotFromEmptiestForce();

// 0x443650 (33 B) -- the force index in ECX (`cmp ecx, 4h`) and the name in
// EDX, pushed to SStrCopy as the source with the table stride, 0x1E, as the max
// length. void: no frame and EAX is never written.
void SetForceName(int nForce, const char *pszName);

#endif  // OC117_FORCES_H
