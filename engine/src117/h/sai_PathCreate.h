// sai_PathCreate.h -- the AI pathing glob and its region table, with the
// sai_PathCreate.cpp bodies that build them.
//
// Class members belong in the class header; this file owns the free functions,
// and every free function the .cpp defines is declared here.
//
// Free functions are spelled without an explicit __fastcall: /Gr already makes
// it the default.

#ifndef OC117_SAI_PATHCREATE_H
#define OC117_SAI_PATHCREATE_H

#include "platform.h"
#include "point16.h"
#include "rect16.h"
#include "pos.h"           // Hyperion: spos_t, mpoint_t (stage E)
#include "sai_Contours.h"   // contours_t, the pContours pointee
#include "maplimits.h"      // Hyperion: the tables past 256 tiles

// The pathing glob is what epGlob @0x69B584 points at. Its extent is
// sai_glob_alloc's own `push 97A20h` into SMemAlloc, and the two interior
// pointers close the layout: the builder re-seats +0x04 to `glob + 0x92BFC` and
// +0x08 to `glob + 0x2000C`, each the end of the array that precedes it.

// One region record, 0x40 bytes: sub_4902E0 walks the table with `add esi, 40h`
// and sai.cpp's IsInaccessibleRegion does
// `shl edx, 6h / cmp word ptr [edx+ecx*1+449FCh], si`.
//
// +0x18..+0x1E are a bounding box in map pixels: sub_490330 seeds the minima
// 0x7FFF and the maxima -1, then keeps them against `col << 5` / `row << 5`.
struct ep_region_t {
  /*0x00*/ WORD   wGroup;        // 0x1FFD / 0x1FFF sentinels
  /*0x02*/ WORD   wArea;         // 0x4000 sentinel; sub_4901F0 numbers it from 1
  /*0x04*/ WORD   wTiles;        // unsigned: sub_4901F0 tests it with `jbe`
  /*0x06*/ BYTE   bPathFirst;
  /*0x07*/ BYTE   bPathCount;
  // Hyperion: pointer-wide, `int` in 1.17. The area search keeps its node's
  // pointer here (region_user, sai_PathFindArea.cpp) and the local search counts in
  // it; with an int the x64 build clears only half of a pointer.
  /*0x08*/ intptr_t nMark;       // sub_491F50: `cmp dword ptr [..+44A04h], 0` / `cmp eax, 2 / jl`
  /*0x0C*/ WORD  *pPaths;        // sub_4910C0 walks it `movzx .., word ptr [edi] / add edi, 2h`
  /*0x10*/ int    nX;            // x256; sai.cpp GetRegionPosition does `sar eax, 8h`
  /*0x14*/ int    nY;
  // Hyperion: 4 bytes each on x64 (h/pos.h, stage E), and the four as the box
  // 1.17 read them as through a cast
  union {
    struct {
      /*0x18*/ spos_t nMinX;
      /*0x1A*/ spos_t nMinY;
      /*0x1C*/ spos_t nMaxX;
      /*0x1E*/ spos_t nMaxY;
    };
    /*0x18*/ mrect_t rcBounds;
  };
  /*0x20*/ BYTE   bAreaLinks;   // sub_48F0E0 stores its capped neighbour-area count here
  /*0x21*/ char   nExtraPaths;   // sai.cpp GetRegionNeighbors reads it `movsx .., byte ptr [esi+44A1Dh]`
  /*0x22*/ BYTE   bUnk22[0x0A];
  /*0x2C*/ WORD   wInlinePaths[10];  // sub_490500: `lea ecx, [edi+2Ch]` / `mov [edi+0Ch], ecx`,
                                     //   then fills to ten with 0x1FFF; the save converters
                                     //   decode a -1 pPaths sentinel to record+0x2C
};

OC_X86_LAYOUT(sizeof(ep_region_t)                == 0x40, "ep_region_t: sub_4902E0 `add esi, 40h`");
static_assert(offsetof(ep_region_t, wGroup)      == 0x00, "IsInaccessibleRegion: cmp word ptr [edx+ecx*1+449FCh], si");
static_assert(offsetof(ep_region_t, wArea)       == 0x02, "sub_4901F0: mov word ptr [ebx+2h], dx");
static_assert(offsetof(ep_region_t, wTiles)      == 0x04, "sub_4902E0: cmp word ptr [esi+4h], 0h");
static_assert(offsetof(ep_region_t, bPathFirst)  == 0x06, "sub_490E20: movzx edi, byte ptr [edx+6h]");
static_assert(offsetof(ep_region_t, bPathCount)  == 0x07, "sub_4910C0: movzx esi, byte ptr [edx+7h]");
static_assert(offsetof(ep_region_t, nMark)       == 0x08, "sub_491F50: mov dword ptr [eax+edi*1+44A04h], 1h");
OC_X86_LAYOUT(offsetof(ep_region_t, pPaths)      == 0x0C, "sub_490E20: mov eax, dword ptr [edx+0Ch]");
OC_X86_LAYOUT(offsetof(ep_region_t, nX)          == 0x10, "GetRegionPosition: mov eax, dword ptr [ecx+44A0Ch]");
OC_X86_LAYOUT(offsetof(ep_region_t, nY)          == 0x14, "GetRegionPosition: mov eax, dword ptr [ecx+44A10h]");
OC_X86_LAYOUT(offsetof(ep_region_t, nMinX)       == 0x18, "sub_490330: movsx eax, word ptr [ecx+edx*1+44A14h]");
OC_X86_LAYOUT(offsetof(ep_region_t, nMinY)       == 0x1A, "sub_490330: movsx eax, word ptr [ecx+eax*1+44A16h]");
OC_X86_LAYOUT(offsetof(ep_region_t, nMaxX)       == 0x1C, "sub_490330: movsx eax, word ptr [ecx+eax*1+44A18h]");
OC_X86_LAYOUT(offsetof(ep_region_t, nMaxY)       == 0x1E, "sub_490330: movsx eax, word ptr [ecx+eax*1+44A1Ah]");
OC_X86_LAYOUT(offsetof(ep_region_t, bAreaLinks)   == 0x20, "sub_48F0E0: mov byte ptr [eax+1Ah], bl at base +44A02h");
OC_X86_LAYOUT(offsetof(ep_region_t, nExtraPaths) == 0x21, "sub_4910C0: mov byte ptr [esi+21h], cl");
OC_X86_LAYOUT(offsetof(ep_region_t, bUnk22)      == 0x22, "no site reads it");
OC_X86_LAYOUT(offsetof(ep_region_t, wInlinePaths) == 0x2C, "sub_490500: lea ecx, [edi+2Ch] / mov dword ptr [edi+0Ch], ecx");

// The whole allocation, 0x97A20 bytes. wMap is flat, not [256][256]: every
// two-index site forms `(y << 8) + x` and scales the sum by 2; wMap's end
// 0x2000C is stored into pScratch and aRegions' end 0x92BFC into pPathPool.
// Hyperion: the 64-bit build sizes the tables for the largest map
// (h/maplimits.h); the 32-bit layout is 1.17's, which tools/save_layouts.py reads as
// the save's, so a save holds each table's 1.17 part.
#if defined(_M_IX86)
#define EP_MAP_CELLS   0x10000
#define EP_SPLIT_RECS  EP_SPLITS_117
#define EP_REGION_RECS EP_REGIONS_117
#define EP_POOL_WORDS  EP_POOL_117
#else
// The region table reaches 0x2000 so that a group code read as a region while a large
// map's carve is under way (sai_PathCreate.cpp, PathCarveWide) indexes an empty record.
#define EP_MAP_CELLS   MAP_TILES_MAX
#define EP_SPLIT_RECS  EP_SPLITS_MAX
#define EP_REGION_RECS 0x2000
#define EP_POOL_WORDS  EP_POOL_MAX
#endif

struct ep_glob_t {
  /*0x00000*/ short        nRegions;
  /*0x00002*/ WORD         wUnk02;
  /*0x00004*/ WORD        *pPathPool;
  /*0x00008*/ void        *pScratch;
  /*0x0000C*/ WORD         wMap[EP_MAP_CELLS];  // [y * EP_STRIDE + x]
  /*0x2000C*/ BYTE         bScratch[EP_SPLIT_RECS * 6];  // 25000 records of 6 in 1.17
  /*0x449FC*/ ep_region_t  aRegions[EP_REGION_RECS];
  /*0x92BFC*/ WORD         wPathPool[EP_POOL_WORDS];
  /*0x97A1C*/ contours_t  *pContours;   // sai_glob_free SMemFrees [eax+97A1Ch]
};

OC_X86_LAYOUT(sizeof(ep_glob_t)               == 0x97A20, "sai_glob_alloc: push 97A20h / SMemAlloc");
static_assert(offsetof(ep_glob_t, nRegions)   == 0x00000, "NumAreas: movsx eax, word ptr [eax]");
static_assert(offsetof(ep_glob_t, wUnk02)     == 0x00002, "unread here; holds the WORD between nRegions and pPathPool");
OC_X86_LAYOUT(offsetof(ep_glob_t, pPathPool)  == 0x00004, "sub_4902E0: lea eax, [ebx+92BFCh] / mov [ebx+4h], eax");
OC_X86_LAYOUT(offsetof(ep_glob_t, pScratch)   == 0x00008, "sub_48F060: lea eax, [esi+2000Ch] / mov [esi+8h], eax");
OC_X86_LAYOUT(offsetof(ep_glob_t, wMap)       == 0x0000C, "sub_490180: mov edx, glob / add edx, 0Ch / lea ecx, [edx+ecx*2]");
OC_X86_LAYOUT(offsetof(ep_glob_t, bScratch)   == 0x2000C, "sub_48F060 stores this address into pScratch");
OC_X86_LAYOUT(offsetof(ep_glob_t, aRegions)   == 0x449FC, "sub_4902E0: lea esi, [ebx+449FCh]");
OC_X86_LAYOUT(offsetof(ep_glob_t, wPathPool)  == 0x92BFC, "sub_4902E0: lea eax, [ebx+92BFCh]");
OC_X86_LAYOUT(offsetof(ep_glob_t, pContours)  == 0x97A1C, "sai_glob_free: mov esi, dword ptr [eax+97A1Ch]");

// The area sub_48F060 is built over. Its two halves go to sub_48F8E0 by value
// as two dwords (`push dword ptr [edi+4h]` / `push dword ptr [edi]`), and that
// callee reads each one's high half back with `movsx .., word ptr [ebp+0Ah]`,
// so each half is a pair of shorts rather than one int.
struct ep_area_t {
  /*0x00*/ mpoint_t ptMin;  // Hyperion: points of 4-byte positions on x64 (stage E)
  /*0x04*/ mpoint_t ptMax;
};

OC_X86_LAYOUT(sizeof(ep_area_t) == 0x08, "sub_48F060 pushes [edi] and [edi+4h]");
static_assert(offsetof(ep_area_t, ptMin) == 0x00, "sub_48F060: push dword ptr [edi]");
OC_X86_LAYOUT(offsetof(ep_area_t, ptMax) == 0x04, "sub_48F060: push dword ptr [edi+4h]");

// The bodies, in oracle VA order.

// 0x48F060, 123 B. The build spine. Returns 0 when sub_48F8E0 fails -- the
// oracle emits no `xor eax, eax` there because EAX is already the zero the
// `test` branched on -- and 1 otherwise; its one caller reads EAX 32-bit.
int sub_48F060(ep_glob_t *pGlob, const ep_area_t *pArea);

// 0x490D80, 155 B. `ret 4h` with one stack word, the tile flags handed to
// sub_490E90. ECX and EDX are set by the one caller (sai_map_init) and never
// read, so their `int` is a placeholder. The return is `int` because
// sai_map_init forwards it and maphdr_MTXM tests it.
int sub_490D80(int nArg0, int nArg1, DWORD *pTiles);

// 0x48F0E0, 193 B. ECX only, no stack parameters, EAX at the ret inherited.
void sub_48F0E0(ep_glob_t *pGlob);

// 0x48F870, 108 B. EDX is the region count the caller already sign-extended;
// the body compares it against `movsx .. word ptr [edi]` with `jnl`, so signed.
// No `ret` of its own -- it tail-jmps sub_4901F0, which pins the return `void`.
void sub_48F870(ep_glob_t *pGlob, int nRegions);

// 0x48FC80, 554 B. EDX is set by the one caller and never read here, so the
// second parameter exists but this body shows no type for it; it is the same
// pointer sub_48F060 hands sub_48F8E0.
void EPCalc_JoinAreas(ep_glob_t *pGlob, const ep_area_t *pArea);

// 0x48FEB0, 313 B. Renumbers the regions and compacts the table; EAX at the ret
// is inherited and the one caller discards it.
void sub_48FEB0(ep_glob_t *pGlob);

// 0x48FFF0, 392 B. `ret 8h`. nFill is read 32-bit and compared against
// zero-extended map words; wMatch is only ever compared 16-bit.
void sub_48FFF0(ep_glob_t *pGlob, const rect16_t *pBox, int nFill, WORD wMatch);

// 0x490180, 111 B. `ret 4h`. The slot is loaded `mov edi, dword ptr [ebp+8h]`
// and stored 16-bit, so the parameter is 32-bit and the store truncates.
void sub_490180(ep_glob_t *pGlob, const rect16_t *pBox, int nValue);

// 0x4901F0, 225 B. ECX only. No direct callers -- sub_48F870 reaches it by
// tail jmp -- so the return is the callee's own: EAX at the ret is inherited.
void sub_4901F0(ep_glob_t *pGlob);

// 0x4902E0, 73 B. ECX only.
void sub_4902E0(ep_glob_t *pGlob);

// 0x490330, 455 B. ECX only; the caller leaves the glob in ECX rather than
// re-loading it.
void sub_490330(ep_glob_t *pGlob);

// 0x490A60, 278 B. `ret 8h`. Fills eight map words -- the 8-neighbourhood of
// (x, y) -- through the out-parameter, substituting 0x1FFF off the map edge.
void sub_490A60(ep_glob_t *pGlob, int x, int y, WORD *pOut);

// 0x490B80, 504 B. `ret 0Ch`. wMatch is the only slot read 16-bit.
void sub_490B80(ep_glob_t *pGlob, int x, int y, rect16_t *pBox, WORD wMatch);

// 0x490E20, 111 B. `ret 4h`, recursive. nArea is loaded 32-bit and stored into
// the region's WORD at +0x02.
void sub_490E20(ep_glob_t *pGlob, ep_region_t *pRegion, int nArea);

// 0x490E90, 180 B. ECX is not the glob: this body loads epGlob itself and walks
// the ECX argument as a dword array, `mov ecx, dword ptr [esi]` / `add esi, 4h`,
// testing it against 0x410000. EAX at the ret is inherited.
void sub_490E90(const DWORD *pTiles);

// 0x490F50, 259 B. ECX only; appends one region and seeds its bounds.
void sub_490F50(ep_glob_t *pGlob);

// 0x4910C0, 280 B. Splits a region's path list in two over 0x400 bytes of
// stack, then writes the halves back and records their counts at +0x06/+0x21.
// noinline because its one caller sub_4902E0 lives in this TU and the oracle
// calls it there: without the attribute /Ob2 splices it in and sub_4902E0 goes
// from 73 B to 392 B.
__declspec(noinline) void sub_4910C0(ep_glob_t *pGlob, ep_region_t *pRegion);

// The three bodies of this TU that are not written yet, declared here so the
// callers above can be.

// 0x48F8E0, 920 B. `ret 8h`. ECX is the glob and the caller leaves it in place.
// The two stack words are the area by value: ptMin and ptMax, each read whole
// and as halves (`movsx .., word ptr [ebp+0Ah]` / `[ebp+0Eh]`). Its one caller
// pushes [edi] and [edi+4h] and reads EAX 32-bit.
int sub_48F8E0(ep_glob_t *pGlob, ep_area_t area);

// 0x48F1B0, 1671 B. No argument is read at all -- the body loads epGlob itself
// -- but the one caller sets ECX, so the parameter exists and is ignored.
void sub_48F1B0(ep_glob_t *pGlob);

// 0x490500, 1351 B. `ret 4h`. Called once, from sub_4902E0, as
// (glob, region index, &glob->aRegions[index]); EAX at the ret is discarded.
void sub_490500(ep_glob_t *pGlob, int nRegion, ep_region_t *pRegion);

// 0x4911E0, 44 B. Nullary; allocates the glob and zeroes it. EAX at the ret is
// memset's and the one caller discards it. sai_glob_free (0x491060) is this
// file's too but is declared in sai.h, where sai_path_free already needed it.
void sai_glob_alloc();

#endif  // OC117_SAI_PATHCREATE_H
