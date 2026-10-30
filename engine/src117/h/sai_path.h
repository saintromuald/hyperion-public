// sai_path.h -- the AI path object: its allocation, the per-unit path timer and
// the step/follow entry points CUnit uses. Bodies are in sai_path.cpp.
//
// Class members belong in the class header; this file owns the free functions,
// and every free function the .cpp defines is declared here.
//
// Free functions are spelled without an explicit __fastcall: /Gr already makes
// it the default.

#ifndef OC117_SAI_PATH_H
#define OC117_SAI_PATH_H

#include "platform.h"
#include "pos.h"  // Hyperion: spos_t
#include "point16.h"

class CUnit;
struct path_search_t;   // both pointed at only

// The path object itself. The 0x80 stride comes from this TU's own free list:
// sai_path_new turns a node index into an address with `shl edx, 7` and
// sai_path_recycle turns an address back into an index with `sar eax, 7`, so
// the record is exactly 128 bytes however little of it is named. The tail past
// the named fields is unexplored.
struct sai_path {
  union {                      // the free-list link, a one-based node index with 0 for
    /*0x00*/ int       nNextFree;   // the end, overlaid on the start position the node
    /*0x00*/ point16_t ptStart;     // carries once it is in use
  };
  union {                      // the packed 16-bit pair PathMoveToLegal copies into the
    /*0x04*/ DWORD     dwWaypoint;  // unit's own +0x18, compared whole and by halves. The
    /*0x04*/ point16_t ptWaypoint;  // pool parks the node's own index here while the node
    /*0x04*/ WORD      awWaypoint[2];  // is free.
  };
  union {                      // the path's own target pair, stored and compared
    /*0x08*/ DWORD     dw08;    // both whole and by halves
    /*0x08*/ point16_t pt08;
  };
  /*0x0C*/ DWORD dwFrame;
  /*0x10*/ DWORD dwUnitId;
  /*0x14*/ int   nSpeed;
  /*0x18*/ BYTE  bFlags;
  /*0x19*/ BYTE  b19;         
  /*0x1A*/ BYTE  bDirection;
  /*0x1B*/ BYTE  b1B;
  /*0x1C*/ char  cStepCount;   // entries in the WORD run that follows adwRegions
  /*0x1D*/ char  cStepIndex;
  /*0x1E*/ char  cRegionCount; // entries in adwRegions
  /*0x1F*/ char  c1F;
  /*0x20*/ DWORD adwRegions[(0x80 - 0x20) / 4];  // the WORD run starts at
                                                 // adwRegions[cRegionCount]
};

static_assert(sizeof(sai_path) == 0x80, "sai_path: shl edx,7h in sai_path_new");
static_assert(offsetof(sai_path, nNextFree)    == 0x00, "sai_path::nNextFree    mov dword ptr [ecx], eax in sai_path_recycle");
static_assert(offsetof(sai_path, ptStart)      == 0x00, "sai_path::ptStart      mov word ptr [edx], ax in CUnit::PathOneStep");
static_assert(offsetof(sai_path, ptWaypoint)   == 0x04, "sai_path::ptWaypoint   mov word ptr [edi+8h] pair in CUnit::PathOneStep");
static_assert(offsetof(sai_path, awWaypoint)   == 0x04, "sai_path::awWaypoint   movzx eax, word ptr [edi+4h] in CUnit::sub_48EE40");
static_assert(offsetof(sai_path, dwWaypoint)   == 0x04, "sai_path::dwWaypoint   mov dword ptr [ecx+4h], eax in sai_path_recycle");
static_assert(offsetof(sai_path, pt08)         == 0x08, "sai_path::pt08         mov word ptr [edi+8h], ax in sai_path_waypoint");
static_assert(offsetof(sai_path, dw08)         == 0x08, "sai_path::dw08         mov dword ptr [edx+8h], eax in CUnit::PathOneStep");
static_assert(offsetof(sai_path, dwFrame)      == 0x0C, "sai_path::dwFrame      mov dword ptr [esi+0Ch], eax in sai_unit_path_from_info");
static_assert(offsetof(sai_path, dwUnitId)     == 0x10, "sai_path::dwUnitId     mov dword ptr [eax+10h] in CUnit::PathFixCollision");
static_assert(offsetof(sai_path, nSpeed)       == 0x14, "sai_path::nSpeed       mov dword ptr [eax+14h] in CUnit::PathSlidePrep");
static_assert(offsetof(sai_path, bFlags)       == 0x18, "sai_path::bFlags       or byte ptr [eax+18h], 1h in CUnit::PathStartPath");
static_assert(offsetof(sai_path, b19)          == 0x19, "sai_path::b19          mov byte ptr [eax+19h], dl in sai_path_set_timer");
static_assert(offsetof(sai_path, bDirection)   == 0x1A, "sai_path::bDirection   mov byte ptr [eax+1Ah] in CUnit::PathTurnAndStart");
static_assert(offsetof(sai_path, b1B)          == 0x1B, "sai_path::b1B          mov byte ptr [esi+1Bh], al in sai_unit_path_from_info");
static_assert(offsetof(sai_path, cStepCount)   == 0x1C, "sai_path::cStepCount   movsx eax, byte ptr [esi+1Ch] in sai_unit_path_from_info");
static_assert(offsetof(sai_path, cStepIndex)   == 0x1D, "sai_path::cStepIndex   mov byte ptr [esi+1Dh], al in sai_unit_path_from_info");
static_assert(offsetof(sai_path, cRegionCount) == 0x1E, "sai_path::cRegionCount movsx eax, byte ptr [esi+1Eh] in sai_unit_path_from_info");
static_assert(offsetof(sai_path, c1F)          == 0x1F, "sai_path::c1F          mov byte ptr [esi+1Fh], 0h in sai_unit_path_from_info");
static_assert(offsetof(sai_path, adwRegions)   == 0x20, "sai_path::adwRegions   lea eax, [esi+20h] into memcpy in sai_unit_path_from_info");

void sai_path_kill(CUnit *pUnit);

// 0x48E9B0 (470 B, unported, `ret 4h`). The unit in ECX, then two signed shorts
// (`movsx edx, bx` for the first, `mov ax, word ptr [ebp+8h] / cwde` for the
// stack slot); both callers consume 32-bit EAX. The (x, y) order is inferred
// from the two call sites pairing a sprite's +0x14 into EDX with its +0x16 on
// the stack, not proven.
int sai_path_compute_distance(CUnit *pUnit, spos_t x, spos_t y);  // Hyperion: positions

// The path-node free list and the per-unit path timer.

// 0x48E8D0 (54 B). Pops the free list at 0x69B570 and hands the node back. The
// argument is a byte size: sub_42C840 computes `(2*(steps + 2*regions) + 23h)
// & 0FFFCh` and tail-jumps here, and the guard rejects anything over one node.
sai_path *sai_path_new(int nBytes);

// 0x48E910 (52 B). Pushes a node back onto the same free list; ECX is
// dereferenced and nothing is returned.
void sai_path_recycle(sai_path *pPath);

// 0x48E950 (31 B) and 0x48E970 (14 B). ECX is dereferenced at +0x100, the
// unit's path pointer, so it is a CUnit *. The timer itself is the byte at
// +0x19 of the path. set_timer's tick count is an int: its one caller,
// CUnit::PathFollowPath, loads it with `mov edx, 1Eh`, and the byte store reads
// DL under either width.
BOOL sai_path_progress_timer(CUnit *pUnit);
void sai_path_set_timer(CUnit *pUnit, int nTicks);

// 0x48E600 (141 B). Reports every pool node that is not on the free list. ECX
// only, EDX never; the body ignores it and no caller reads EAX.
void sub_48E600(int nMode);

// The node pool itself. Both are nullary and both tag their Storm call with
// this TU's own name, `Sai_path.cpp`.
void sai_pathpool_init();
void sai_pathpool_free();
// Hyperion: the pool made again for the game's paths, after PoolsForGame (revision 15)
void SaiPathPoolForGame();

// 0x48EC50 (489 B), `ret 4h`. ECX is the unit, EDX the out point its one caller
// takes from a local, and the stack dword is the same target pair
// CUnit::PathFollow was handed. It is point16_t by value, not DWORD: both bodies
// read the halves at [ebp+8h] and [ebp+0Ah]. A shift does not rule the struct
// out: CUnit::CreatePath takes point16_t too and shifts its from-pair, because
// it copies the pair whole first.
BOOL sai_path_waypoint(CUnit *pUnit, point16_t *pOut, point16_t ptTarget);

// 0x48EC20 (35 B). Both register arguments are pointers; EDX's pointee is read
// as two shorts at +8 and +0Ah, the request block's destination pair.
int sai_path_distance_to_dest(CUnit *pUnit, path_search_t *pReq);

// 0x48E730 (161 B). Takes a filled request block in ECX and hands back a path
// node holding the same two runs, shortened to what one node can carry.
sai_path *sai_unit_path_from_info(path_search_t *pReq);

// 0x48E7E0 (153 B), `ret 4h`. The inverse: fills a request block from a unit
// and the node it is already walking.
void sai_path_from_unit_path(CUnit *pUnit, sai_path *pPath, path_search_t *pReq);

#endif  // OC117_SAI_PATH_H
