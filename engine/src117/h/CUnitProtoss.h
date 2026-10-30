// CUnitProtoss.h -- the free functions of src117/lang/CUnitProtoss.cpp: the protoss psi
// (juice) power grid, the shield battery, and the Archon and Dark Archon merge. The member
// functions are declared in CUnit.h with the rest of the class. Free functions are spelled
// without an explicit __fastcall, which /Gr already makes the default; the exception is
// status_set_update, which four other headers already spell with the keyword, so this TU
// reaches it by including CUnitTerran.h instead of respelling it.
//
// The TU name comes off the oracle: free_juice_matrix and cunit_protoss_init each
// `push 4EC150h` into SMemFree and SMemAlloc, and .rdata at 0x4EC150 reads
// "CUnitProtoss.cpp", the __FILE__ Storm's allocator tags the block with.

#ifndef OC117_CUNITPROTOSS_H
#define OC117_CUNITPROTOSS_H

#include "platform.h"

class CUnit;

// 0x42FFC0 (11 B). `mov dword ptr [0x665B60], 1h / ret` -- raises the "the power grid is
// stale" flag. Nullary, so every calling convention encodes identically; all five callers
// discard EAX.
void RequestJuiceUpdate();

// 0x430320 (108 B). Walks the sgJuiceLink_g chain and makes each local player's pylon show
// its power template, then latches sgbShowJuice_g. Nullary; EAX is never defined at the
// ret and all four callers discard it.
void ShowPowerTemplates();

// 0x42FDA0 (191 B). The mirror of ShowPowerTemplates -- the same walk, but it creates the
// template where one is missing and clears sgbShowJuice_g at the end. The corpus name says
// it inits the juice matrix; the body does not touch the matrix at all, and the name here
// is the corpus's. The body is CUnit::CreatePowerTemplate inlined into the walk while
// ShowPowerTemplates emits a real call, so both shapes live in one TU: check both after a
// change to either.
void init_juice_matrix();

// 0x42FD70 (35 B). Frees sgJuiceMtx_g through SMemFree and nulls it. Nullary. The
// `push 4EC150h` is the "CUnitProtoss.cpp" owner tag; the line number is 0x66 = 102.
void free_juice_matrix();

// 0x42FE60 (340 B). Allocates the psi matrix from the juice frame's dimensions
// and stamps the frame's clickable mask into all four quadrant mirrors, then
// converts sgJuiceRangeHorz / sgJuiceRangeVert from cell counts to pixels
// (`shl .., 5h`). Nullary. Owner tag "CUnitProtoss.cpp", line 0x4A = 74.
void cunit_protoss_init();

// 0x430000 (213 B). Is the cell under (x, y) inside some friendly pylon's psi field?
// `ret 8h`, ECX and EDX live-in, two stack slots: the type is a WORD and the width
// matters (`movzx eax, cx` indexes gulUnitTypeFlags), x and y are ints subtracted from
// movsx-ed sprite coordinates, and the player is a BYTE, read a byte at a time and
// compared against a unit's +0x4C. UpdateProtossPowerGrid pushes the player first and the
// y second, which is right-to-left evaluation of this order. BOOL: two exits 1, one 0.
BOOL IsCellPowered(WORD wType, int x, int y, BYTE bPlayer);

// 0x430670 (258 B). The grid refresh: while sgbNeedsJuiceUpdate_g is set, walk the used
// list and turn each protoss building's unpowered flag (0x400) and its warp animation on
// or off. Nullary; it ends in a tail jmp to status_set_update, which is void, so void.
void UpdateProtossPowerGrid();

// 0x430780 (31 B). The area-search callback CUnit::DispatchRecharge hands to
// find_first_unit_near by address, so its convention cannot be read off a call site:
// __fastcall with both registers live-in and no stack slot, the shape TrigEnum.h's
// FindUnitCb documents for this family. ECX is the candidate, which becomes
// CanRechargeShieldsAt's `this`, and EDX the seeker. It returns int, always 0, which is a
// finder callback's "keep searching".
int do_dispatch_recharge_finder(CUnit *pCandidate, CUnit *pSeeker);

// 0x4302C0 (87 B). The recall search filter DispatchTeleport hands to
// find_first_unit_near: candidate in ECX, the caster in EDX, always returns 0.
int do_recall_single(CUnit *pUnit, CUnit *pCaster);

// 0x430150 / 0x4301F0, unported. Nullary; saveload_write_save_data brackets
// the unit save with them and discards EAX both times.
void DestroyPsiLinks();
void RestorePsiLinks();

#endif  // OC117_CUNITPROTOSS_H
