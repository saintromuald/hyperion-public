// sai_Contours.h -- the AI pathing contour types and the sai_Contours.cpp rows.
//
// Two classes live here because three TUs share them: sai_Contours.cpp holds
// the workspace methods, sai_ContoursCreate.cpp builds and frees the heap form,
// and sai_ContourScan.cpp runs the four directional scans.
//
// Class members belong in the class header; this file owns the free functions
// too, and every free function those TUs define is declared here. They are
// spelled without an explicit __fastcall: /Gr already makes it the default.

#ifndef OC117_SAI_CONTOURS_H
#define OC117_SAI_CONTOURS_H

#include "platform.h"
#include "point16.h"
#include "rect16.h"
#include "pos.h"

// A single contour edge, 8 bytes: find_contour indexes `[edi+eax*8]` and
// sub_488600 recovers an index with `sub eax, 2D8h / sar eax, 3h`. The two
// leading fields are signed -- contour_sort_cb compares them 16-bit with
// jnl/jle/jl/setnle, where an unsigned compare would use jae/jbe.
//
// Hyperion: on x64 the point and the end are positions of 4 bytes (h/pos.h), 16
// bytes in all. A map of 1,024 tiles a side has the contours of its right and bottom
// edges at 32,768, which a short made -32,768; the list then sorted them first and the
// local search's stores overran. A 1.17 game's contours stay within 8,192
// either way.
struct contour_t {
  /*0x00*/ mpoint_t  pt;
  /*0x04*/ spos_t    nEnd;
  /*0x06*/ BYTE      bC;
  /*0x07*/ BYTE      bFlags;    // sub_488600 masks it with 3
};

// The +0x74 mark record, also 8 bytes: a whole point and two flag bytes.
struct contour_mark_t {
  /*0x00*/ point16_t pt;
  /*0x04*/ BYTE      bA;
  /*0x05*/ BYTE      bB;
  /*0x06*/ BYTE      bC;
  /*0x07*/ BYTE      bFlags;
};

// A list's count or capacity. Hyperion: an int on x64, where a map of 1,024 tiles
// a side has more than 32,767 contours in a list and 1.17's short wrapped, and Grow then
// asked for a negative size. A count within a short is the same either way;
// the x86 probe builds keep 1.17's short.
#if defined(_M_IX86)
typedef short contour_count_t;
#else
typedef int contour_count_t;
#endif

// The contour table: four directional lists with their counts, plus the search
// rectangle the scans were run over. sai_contours_free SMemFrees exactly [esi],
// [esi+4], [esi+8] and [esi+0Ch], and sub_488320 stores the four counts at
// +0x2D0..+0x2D6 into +0x10..+0x16. nCounts and nCapacity are signed: every
// arithmetic read of either is movsx, and a WORD spelling emits movzx alone.
struct contours_t {
  /*0x00*/ contour_t *pLists[4];
  /*0x10*/ contour_count_t nCounts[4];
  /*0x18*/ contour_count_t nCapacity[4];
  /*0x20*/ short      nOrigin[4];    // indexed by the contour flags, 2 bits each
  /*0x28*/ short      nBound0;       // Reset writes 1, -1, -1, 1 across these
  /*0x2A*/ short      nBound1;
  /*0x2C*/ short      nBound2;
  /*0x2E*/ short      nBound3;
  /*0x30*/ short      nLimits[4];    // each origin plus or minus one

  // All four are thiscall with ECX only and no stack parameters, and all four
  // leave EAX as their last callee did with no caller reading it, so void.
  void Reset();    // 0x488DF0
  void Init();     // 0x48A7F0
  void Grow();     // 0x48B040
  void Shrink();   // 0x48B600
  void SetSearchParameters(WORD nA, WORD nB, WORD nC, WORD nD);
  void SetSearchUnit(int nId);
  void SetSearch(const rect16_t *pRect);
  contour_t *sub_48C300(short nA, short nB, short nC, short nD);
  contour_t *sub_48C400(short nA, short nB, short nC, short nD);
  contour_t *sub_48C4E0(short nA, short nB, short nC, short nD);
  contour_t *sub_48C5E0(short nA, short nB, short nC, short nD);

  // 0x489320, `ret 4h`, one stack argument written through at +0, +2 and +4.
  // Every one of the eight callers discards EAX, so void.
  void sub_489320(contour_t *pEntry);

  // 0x48B780, `ret 8h`. The body reads both slots as dwords and keeps only the
  // low word, so it cannot decide its own parameter width; widening them to int
  // cost the landed caller sub_42ED50 a byte, so the callers arbitrate and both
  // coordinates are short.
  int sub_48B780(short nA, short nB);

  // 0x48B9E0, `ret 0Ch`. Two short coordinates and the rectangle it shrinks in
  // place. EAX at the return is the rectangle's own second dword, left there by
  // the copy-back, and the one caller discards it, so void.
  void sub_48B9E0(short nA, short nB, rect16_t *pRect);

  // 0x48BEC0, `ret 10h`. Four short coordinates: every stack read is 2-byte,
  // and the two 4-byte reads are of slots MSVC recycled for its own locals
  // after the parameter died. The one caller, CUnit::sub_42DA50, uses EAX
  // 32-bit.
  int sub_48BEC0(short nX0, short nY0, short nX1, short nY1);

};

// The scan workspace is itself a contours_t: sub_488320 points the inherited
// four pointers at its own arrays (`lea eax, [esi+2D8h] / mov [esi], eax`) and
// copies its own counts up, so the heap form (contours_t::Init) and this stack
// form present the same face to every consumer. The +0x74 array is 32 records of
// 8 bytes (sub_488220 caps the +0x70 count at 0x20 and indexes
// `[ecx+eax*8+74h]`), and 0x74 + 32*8 == 0x174, where the saved header begins.
struct contour_workspace_t : contours_t {
  /*0x38*/ short      nTargetX;
  /*0x3A*/ short      nTargetY;
  /*0x3C*/ intptr_t   nReserved3C;
  /*0x40*/ point16_t  ptCur;
  /*0x44*/ intptr_t   nReserved44;
  /*0x48*/ rect16_t   rcSpan;
  /*0x50*/ contour_t *pFound[4];
  /*0x60*/ short      nEdge[4];
  /*0x68*/ int        nReserved68;
  /*0x6C*/ int        nReserved6C;
  /*0x70*/ short      nMarks;
  /*0x72*/ BYTE       bHit;
  /*0x73*/ BYTE       bScanMask;      // sub_48A750 tests bits 1, 2, 4, 8
  /*0x74*/ contour_mark_t marks[32];
  /*0x174*/ contour_t *pSavedLists[4];
  /*0x184*/ contour_count_t nSavedCounts[4];
  /*0x18C*/ short     nStack;
  /*0x18E*/ short     nPad18E;
  // Hyperion: rectangles, which sub_491BA0 hands on to sub_492DE0; 1.17 types the
  // slots contour_t on their stride and casts both ways.
  /*0x190*/ rect16_t  stack[40];
  /*0x2D0*/ short     nStore[4];     // sub_488600 reads it with movsx
  /*0x2D8*/ contour_t store0[128];
  /*0x6D8*/ contour_t store1[128];
  /*0xAD8*/ contour_t store2[128];
  /*0xED8*/ contour_t store3[128];

  // The methods, in oracle VA order. Every one is thiscall -- ECX read as an
  // input, EDX never -- which a member declaration already is.
  int  sub_489070(const rect16_t *pRect);
  void sub_489110();
  void sub_488220(short nX, short nY, BYTE bMask, int nDir);
  void sub_488320(int nX, int nY, BYTE bMask);
  void sub_488600();
  void sub_488910();
  void sub_488F00(const contour_t *pNew, contour_t **ppPos, contour_t **ppEnd);
  void sub_488E10(const contours_t *pSrc);
  void sub_488EE0();
  void sub_4890E0(const rect16_t *pEntry);
  void sub_4892D0(intptr_t nA, const point16_t *pPt, intptr_t nB, const rect16_t *pRect);

  // The four directional sweeps and the dispatcher that runs them, all in
  // sai_ContourScan.cpp. Each sweep returns int: sub_48A750 tests EAX after
  // every call and stops on the first nonzero.
  int  sub_489360();
  int  sub_489880();
  int  sub_489D50();
  int  sub_48A240();
  void sub_48A750();
  // 0x48A7A0 (72 B), `ret 14h`: the rectangle normaliser the four sweeps share.
  // Every caller sets ECX to the workspace first, and the body reads none of it.
  void NormalizeRect(rect16_t *pRect, short x0, short y0, short x1, short y1);

};

OC_X86_LAYOUT(sizeof(contour_t) == 8,
              "contour_t stride       find_contour `[edi+eax*8]`, sub_488600 `sar eax, 3h`");
#if !defined(_M_IX86)
static_assert(sizeof(contour_t) == 16, "contour_t on x64: a point, an end, two bytes and two of padding");
#endif
static_assert(sizeof(contour_mark_t) == 8,
              "contour_mark_t stride  sub_488220 and sub_489070 index marks with `[reg+idx*8+74h]`");
static_assert(offsetof(contour_t, pt) == 0x00,
              "contour_t::pt          sub_489070 copies [edx+40h] into [edx+ecx*8+74h] as ONE dword");
OC_X86_LAYOUT(offsetof(contour_t, bFlags) == 0x07,
              "contour_t::bFlags      sub_488600 `movzx edx, byte ptr [edi+7h] / and eax, 3h`");
OC_X86_LAYOUT(sizeof(contours_t) == 0x38,
              "contours_t size        its own last field is +0x36 and the workspace's first is +0x38");
static_assert(offsetof(contours_t, pLists) == 0x00,
              "contours_t::pLists     sai_contours_free SMemFrees [esi], [esi+4h], [esi+8h], [esi+0Ch]");
OC_X86_LAYOUT(offsetof(contours_t, nCounts) == 0x10,
              "contours_t::nCounts    sub_488320 copies +0x2D0..+0x2D6 into +0x10..+0x16");
OC_X86_LAYOUT(offsetof(contours_t, nCapacity) == 0x18,
              "contours_t::nCapacity  contours_t::Init seeds +0x18/+0x1C with 13881388h, 5000 per list");
OC_X86_LAYOUT(offsetof(contours_t, nOrigin) == 0x20,
              "contours_t::nOrigin    sub_48C5E0 `movzx eax, word ptr [ebx+eax*2+20h]`");
OC_X86_LAYOUT(offsetof(contours_t, nBound0) == 0x28,
              "contours_t::nBound0    contours_t::Reset writes +0x28..+0x2E");
OC_X86_LAYOUT(offsetof(contours_t, nLimits) == 0x30,
              "contours_t::nLimits    sub_489320 `movzx eax, word ptr [ecx+eax*2+30h]` with a 2-bit index");
OC_X86_LAYOUT(offsetof(contour_workspace_t, nTargetX) == 0x38,
              "workspace nTargetX     sub_488220 `cmp si, word ptr [ecx+38h]`");
OC_X86_LAYOUT(offsetof(contour_workspace_t, ptCur) == 0x40,
              "workspace ptCur        sub_489070 `mov eax, dword ptr [edx+40h]`, one dword");
OC_X86_LAYOUT(offsetof(contour_workspace_t, nMarks) == 0x70,
              "workspace nMarks       sub_488220 `inc word ptr [ecx+70h]`, capped at 0x20");
OC_X86_LAYOUT(offsetof(contour_workspace_t, bScanMask) == 0x73,
              "workspace bScanMask    sub_48A750 tests [esi+73h] bits 1, 2, 4, 8");
OC_X86_LAYOUT(offsetof(contour_workspace_t, marks) == 0x74,
              "workspace marks        sub_488220 `mov word ptr [ecx+eax*8+74h], si`");
OC_X86_LAYOUT(offsetof(contour_workspace_t, pSavedLists) == 0x174,
              "workspace pSavedLists  sub_488E10 saves +0x00..+0x16 into +0x174..+0x18A");
OC_X86_LAYOUT(offsetof(contour_workspace_t, nStack) == 0x18C,
              "workspace nStack       sub_4890E0 `inc word ptr [ecx+18Ch]`");
OC_X86_LAYOUT(offsetof(contour_workspace_t, stack) == 0x190,
              "workspace stack        sub_4890E0 `mov [ecx+esi*8+190h], eax`");
OC_X86_LAYOUT(offsetof(contour_workspace_t, nStore) == 0x2D0,
              "workspace nStore       sub_488600 `mov word ptr [esi+2D0h], ax` after sar 3");
OC_X86_LAYOUT(offsetof(contour_workspace_t, store0) == 0x2D8,
              "workspace store0       sub_488320 `lea eax, [esi+2D8h] / mov [esi], eax`");
OC_X86_LAYOUT(offsetof(contour_workspace_t, store1) == 0x6D8,
              "workspace store1       sub_488320 `lea eax, [esi+6D8h] / mov [esi+4h], eax`");
OC_X86_LAYOUT(offsetof(contour_workspace_t, store2) == 0xAD8,
              "workspace store2       sub_488320 `lea eax, [esi+0AD8h] / mov [esi+8h], eax`");
OC_X86_LAYOUT(offsetof(contour_workspace_t, store3) == 0xED8,
              "workspace store3       sub_488320 `lea eax, [esi+0ED8h] / mov [esi+0Ch], eax`");
OC_X86_LAYOUT(sizeof(contour_workspace_t) == 0x12D8,
              "workspace size         0x2D8 + 4 * 128 * 8, the four stores back to back");

OC_X86_LAYOUT(offsetof(contour_t, nEnd) == 0x04,
              "contou::nEnd           sub_488F00 `movsx ecx, word ptr [eax+4h]` and the `mov word ptr [eax+4h], cx` store");
OC_X86_LAYOUT(offsetof(contour_t, bC) == 0x06,
              "contou::bC             sub_489320 and sub_48B780 use it raw as a 2-bit table index");
static_assert(offsetof(contour_mark_t, pt) == 0x00,
              "contou::pt             sub_489070 `mov dword ptr [edx+ecx*8+74h], eax` from ptCur, one dword");
static_assert(offsetof(contour_mark_t, bA) == 0x04,
              "contou::bA             sub_488220 `and byte ptr [eax+78h], dl` -- a BYTE-wide read-modify-write");
static_assert(offsetof(contour_mark_t, bB) == 0x05,
              "contou::bB             sub_489070 `mov byte ptr [edx+eax*8+79h], 1h`");
static_assert(offsetof(contour_mark_t, bC) == 0x06,
              "contou::bC             width by subtraction; no site reads it");
static_assert(offsetof(contour_mark_t, bFlags) == 0x07,
              "contou::bFlags         the mark record's last byte, paired with contour_t's");
OC_X86_LAYOUT(offsetof(contours_t, nBound1) == 0x2A,
              "contou::nBound1        contours_t::Reset `mov word ptr [ecx+2Ah], ax` with eax = -1");
OC_X86_LAYOUT(offsetof(contours_t, nBound2) == 0x2C,
              "contou::nBound2        contours_t::Reset `mov word ptr [ecx+2Ch], ax`");
OC_X86_LAYOUT(offsetof(contours_t, nBound3) == 0x2E,
              "contou::nBound3        contours_t::Reset `mov word ptr [ecx+2Eh], dx` with edx = 1");
OC_X86_LAYOUT(offsetof(contour_workspace_t, nTargetY) == 0x3A,
              "contou::nTargetY       sub_488220 `cmp dx, word ptr [ecx+3Ah]`");
OC_X86_LAYOUT(offsetof(contour_workspace_t, nReserved3C) == 0x3C,
              "contou::nReserved3C    sub_4892D0 `mov dword ptr [ecx+3Ch], eax`");
OC_X86_LAYOUT(offsetof(contour_workspace_t, nReserved44) == 0x44,
              "contou::nReserved44    sub_488220 `cmp dword ptr [ecx+44h], ebx`");
OC_X86_LAYOUT(offsetof(contour_workspace_t, rcSpan) == 0x48,
              "contou::rcSpan         sub_4892D0 copies it as two dword moves; sub_488220 compares all four members");
OC_X86_LAYOUT(offsetof(contour_workspace_t, pFound) == 0x50,
              "contou::pFound         sub_488320 `mov dword ptr [esi+50h], eax` from contours_t::sub_48C5E0");
OC_X86_LAYOUT(offsetof(contour_workspace_t, nEdge) == 0x60,
              "contou::nEdge          sub_488320 `mov word ptr [esi+60h], dx` .. `[esi+66h], di`");
OC_X86_LAYOUT(offsetof(contour_workspace_t, nReserved68) == 0x68,
              "contou::nReserved68    sub_488EE0 zeroes it as a dword");
OC_X86_LAYOUT(offsetof(contour_workspace_t, nReserved6C) == 0x6C,
              "contou::nReserved6C    sub_488EE0 zeroes it as a dword");
OC_X86_LAYOUT(offsetof(contour_workspace_t, bHit) == 0x72,
              "contou::bHit           sub_488220 `mov byte ptr [ecx+72h], al`");
OC_X86_LAYOUT(offsetof(contour_workspace_t, nSavedCounts) == 0x184,
              "contou::nSavedCounts   sub_488E10 `mov word ptr [ecx+184h], ax` from the source's +0x10");
OC_X86_LAYOUT(offsetof(contour_workspace_t, nPad18E) == 0x18E,
              "contou::nPad18E        alignment between nStack and the 8-byte-aligned stack array");


#endif  // OC117_SAI_CONTOURS_H
