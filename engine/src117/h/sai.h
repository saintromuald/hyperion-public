// sai.h -- the AI-module entry points src117 code calls: the region-table
// readers, the pathing glob's init and free, and the sai.cpp free functions
// other TUs reach.
//
// Each name is declared in a header of ours, not only in the .cpp that defines
// it, and in exactly one header: a second, disagreeing spelling is a C2371 in a
// TU this file never touches, and two spellings at one include root emit no
// thunk at all. A name another of our headers already seats stays there.
#pragma once

#include "platform.h"
#include "point16.h"   // GetRegionPosition's out-param

// sai_map_init -- 0x481D50, 26 B. Nullary: no register input, no stack
// parameters and a plain `ret`, so every convention is byte-identical for it and
// the /Gr default __fastcall is left implicit. The return is its tail call's --
// the body leaves EAX as sub_490D80 left it -- and its one caller (maphdr_MTXM,
// 0x468650) uses the full EAX, so `int`; `void` would be a C4716 on a
// `return f();` tail. Not `BOOL`: nothing here proves a truth value, and `BOOL`
// is `typedef int`, so the two are codegen-identical anyway.
int sai_map_init(void);

// IsInaccessibleRegion -- 0x481BB0, 33 B. One argument in ECX, read sixteen bits
// wide and never wider (`movzx edx, cx`), and for a parameter the callee is the
// authority, so WORD: the call sites agree, passing GetRegion_xy's WORD result.
// BOOL because `xor eax, eax` before the compare and `setz al` after it define
// all 32 bits as exactly 0 or 1. Bare `ret`, no stack slots.
BOOL IsInaccessibleRegion(WORD wRegion);

// The region-table readers, 0x481A30..0x481B00, all in sai.cpp, share one
// prologue, and it is the whole width argument: `movzx ecx, cx` (the only read
// of the region id), `shl ecx, 6h` for the 64-byte records, then
// `add ecx, dword ptr [0x69B584]`. The id is read sixteen bits wide and never
// wider, so WORD on callee authority; GetRegion_CUnit and GetRegion_xy hand
// over WORD as well.

// NumAreas -- 0x481B00, 9 B. `mov eax, [0x69B584] / movsx eax, word ptr [eax] /
// ret`. Nullary, so every convention is byte-identical and the /Gr default is
// left implicit. `int`, not WORD: the load is movsx, so the callee itself
// defines all 32 bits, and all 32 consuming callers use the full EAX. The count
// is signed in the image, whatever TAI_Captain.cpp's loops compare it with.
int NumAreas();

// GetRegionPosition -- 0x481A60, 38 B. Writes the region's centre into a
// point16_t out-parameter carried in EDX: two 16-bit stores at +0 and +2 off the
// one pointer, each `sar eax, 8h` of the x256 fixed-point field behind it, which
// is a point16_t by layout. void.
void GetRegionPosition(WORD wRegion, point16_t *pPos);
#if defined(_WIN64) || defined(__LP64__)
// Hyperion: into a map point of 4-byte positions (h/pos.h), until GetRegionPosition
// takes it.
#include "pos.h"
inline void GetRegionPosition(WORD wRegion, mpoint_t *pPos) {
  point16_t pt;
  GetRegionPosition(wRegion, &pt);
  *pPos = pt;
}
#endif

// GetRegionCenter -- 0x481A30, 44 B. The same two fields as GetRegionPosition,
// written 32-bit through two separate out-params instead of one point16_t.
// `ret 4h`, so the third argument is the one stack word.
void GetRegionCenter(WORD wRegion, int *pX, int *pY);

// GetDefPriority -- 0x481A90, 20 B. ECX only, `movzx ecx, cx`, so a WORD
// region id. The return is `movzx eax, byte ptr [..+44A1Ch]`, the byte at +0x20
// of the 64-byte record, and sub_40E4E0 compares the full EAX with `jb`.
DWORD GetDefPriority(WORD wRegion);

// GetRegionPaths -- 0x481AB0, 28 B. Stores the region's path-set pointer through
// the EDX out-param (`mov eax, [ecx+44A08h] / mov [edx], eax`) and returns a
// zero-extended byte, so all 32 bits are defined by the callee and `int` is
// honest; 10 of 10 consuming callers use the full EAX. The stored dword points
// at a WORD array: every consumer reads the slot `movzx eax, word ptr [ecx]`.
int GetRegionPaths(WORD wRegion, WORD **pPaths);

// GetRegionNeighbors -- 0x481AD0, 39 B. GetRegionPaths' sibling: the same
// out-param store of a `WORD *`, then `movzx eax, byte ptr [esi+44A03h]` minus
// `movsx ecx, byte ptr [esi+44A1Dh]` -- an unsigned count less a signed
// adjustment, so the result is `int` and not a byte.
int GetRegionNeighbors(WORD wRegion, WORD **pPaths);

// sai_town_mineral_fields -- 0x496F20, 29 B (oracle TU sai_TownBuilder.cpp),
// called by player_mineral_fields. One register argument in ECX and it is a
// town-plan index, not a pointer: both call sites do
// `movzx ecx, byte ptr [<town>+1Ch]` and the body's own `test ecx,ecx` /
// `cmp ecx, 0FAh` guards read it 32-bit. Both callers take the full EAX.
int sai_town_mineral_fields(int nTown);

// sai_townbuilder_clear -- 0x497380, oracle TU sai_TownBuilder.cpp. A town-plan
// index in ECX, as sai_town_mineral_fields takes one; EAX is written on no path
// to a `ret`, so the return really is `void`.
void sai_townbuilder_clear(int nTown);

// sai_scripts_remove_town -- 0x493860, oracle TU SAI_Scripts.cpp. One 32-bit
// register argument in ECX; `void` return.
void sai_scripts_remove_town(intptr_t nArg0);

// ai_nearest_enemy_building -- 0x48E0C0, 145 B (oracle TU SAI_Main.cpp). Two
// register arguments (EDX is read before written), both 32-bit, plus one stack
// word read twice; `ret 4h`. Two consuming callers take the full EAX.
intptr_t ai_nearest_enemy_building(int nArg0, int nArg1, int nArg2);

// sai_alert_frontier -- 0x4979B0, oracle TU sai_TownBuilder.cpp.
void sai_alert_frontier(int nArg0, int nArg1, int nArg2, int nArg3);

// guard_free_unmanned -- 0x4B21F0, oracle TU TAI_GuardPost.cpp.
void guard_free_unmanned(int nArg0);

// GetRegionBounds -- 0x481A00, 34 B. The region-table prologue above, so the
// region id is WORD on callee authority. EDX is an out-parameter whose pointee
// the bytes do not name; `void *` stands until a body reads it, and it is 4
// bytes either way. void: EAX at the ret is a leftover.
void GetRegionBounds(WORD wRegion, void *pBounds);

// IsInaccessibleRegion_xy -- 0x481BE0, 38 B. IsInaccessibleRegion's (x,y) front
// end. Both register arguments are forwarded to a callee untouched, so this body
// shows no width for either; `int` is what agrees with GetRegion_xy's own
// contract, `WORD __fastcall (int x, int y)` in SAI_Banker.h. BOOL: every ret
// materialises 0/1.
BOOL IsInaccessibleRegion_xy(int x, int y);

// IsValidInaccessibleRegion -- 0x481C10, 46 B. Same region-table prologue, so
// WORD on callee authority. Its one consuming caller takes the full EAX.
int IsValidInaccessibleRegion(WORD wRegion);

// GetRegionFromPx -- 0x481C40, 65 B. Two 32-bit register arguments, pixel
// coordinates read 32 bits wide by the callee itself. The WORD return is the
// callers' call: 2 of the 3 consuming sites narrow EAX 16-bit right after the
// call, and `movzx eax, ax` is not emittable for an `int` return. A 16-bit
// result passed onward in a register needs no narrowing, so the third site's
// wide use is not counter-evidence.
WORD GetRegionFromPx(int x, int y);

// sai_path_free -- 0x481D70, 15 B. Nullary, and void because it has no `ret` of
// its own: it tail-jmps voice_squelch, which pins the return type. The payload
// link refuses a body that tail-calls voice_squelch (0x4BE950, a 1-byte
// ICF-folded `ret`) with LNK2019: one thunk is emitted for that address, under
// net_cmds.h's `void __fastcall voice_squelch(DWORD, BOOL)`, and no spelling
// both links and matches, so such a body goes under `#ifndef OC_PAYLOAD`.
void sai_path_free();

// sai_path_init -- 0x481D80, 10 B. Nullary; no `ret` of its own -- it tail-jmps
// sai_debug_init, which itself tail-jmps app_add_exit, and every path there
// materialises an explicit 0/1 into EAX that the body never consumes. A void
// body does not emit a dead immediate, so the return is BOOL; all 25 callers
// discard it, so the callee is the whole evidence.
BOOL sai_path_init();

// The callees sai.cpp reaches that no header of ours declared.

// sai_debug_init -- 0x48C6C0, 10 B (oracle TU sai_debug.cpp). Nullary; no `ret`
// of its own, it tail-jmps app_add_exit -- see sai_path_init above for why that
// pins BOOL. All 25 callers discard the result.
BOOL sai_debug_init();

// sai_pathpool_free -- 0x48EFD0, 38 B (oracle TU sai_path.cpp). Nullary; EAX at
// the ret is inherited and no caller reads it -> void.
void sai_pathpool_free();

// sai_pathpool_init -- 0x48F000, 96 B (oracle TU sai_path.cpp). Nullary; EAX at
// the ret is an incidental 32-bit write and no caller reads it -> void.
void sai_pathpool_init();

// sai_glob_free -- 0x491060, 95 B (oracle TU sai_PathCreate.cpp). Nullary; EAX
// at the ret is inherited and no caller reads it -> void.
void sai_glob_free();

// sub_490D80 is seated in sai_PathCreate.h, its own TU's header.
