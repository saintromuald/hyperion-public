// sai_PathFindLocal.h -- the local leg of AI pathfinding: the A* over the tile
// grid and its priority queue. Bodies are in sai_PathFindLocal.cpp.
//
// Class members belong in the class header; this file owns the free functions,
// and every free function the .cpp defines is declared here.
//
// Free functions are spelled without an explicit __fastcall: /Gr already makes
// it the default.

#ifndef OC117_SAI_PATHFINDLOCAL_H
#define OC117_SAI_PATHFINDLOCAL_H

#include "platform.h"

#include "PriorityQueue.h"
#include "sai_Contours.h"        // contour_workspace_t, sub_491BA0's first argument
#include "sai_PathFindArea.h"   // path_search_t, the request both halves of the search share

// The queue's element, one tile the search has reached. The 0x20 stride is
// sub_491F50's own two records, 0x69B788 and 0x69B7A8, and that body seeds the
// first one field by field, which is where every offset below comes from.
// Every comparison in the three PriorityQueue<pathlocaluser_t,150> members is
// `mov eax, [p+1Ch]` against another element's +0x1Ch, compared whole and
// signed, so the key is the last dword.
struct pathlocaluser_t {
  /*0x00*/ WORD nX;          // written with +0x02 as one dword, a packed point
  /*0x02*/ WORD nY;
  /*0x04*/ WORD nRegion;
  // The six flag bytes are seeded 0,0 and 0,0,FF,0 by two merged constant stores.
  /*0x06*/ BYTE bMarked;     // the tile's region was one of the target's neighbours
  /*0x07*/ BYTE bTarget;     // the tile lies in the target region
  /*0x08*/ BYTE bGoal;       // the contour mark that made it was a goal hit
  /*0x09*/ BYTE bClosed;
  /*0x0A*/ BYTE b0A;         // sub_491BA0 hands it to sub_488320 as the scan mask
  /*0x0B*/ BYTE nDepth;      // the parent's plus one
  /*0x0C*/ BYTE _0C[0x04];
  /*0x10*/ pathlocaluser_t *pParent;
  /*0x14*/ int  nH;          // veclen to the goal, x256
  /*0x18*/ int  nG;          // the cost so far
  /*0x1C*/ int  nKey;
};

OC_X86_LAYOUT(sizeof(pathlocaluser_t) == 0x20,
              "pathlocaluser_t stride  sub_491F50 seeds 0x69B788 and walks to 0x69B7A8");
static_assert(offsetof(pathlocaluser_t, nX) == 0x00,
              "pathlocaluser_t::nX     sub_491BA0 `movzx eax, word ptr [esi]`");
static_assert(offsetof(pathlocaluser_t, nY) == 0x02,
              "pathlocaluser_t::nY     sub_491BA0 `movzx eax, word ptr [esi+2h]`");
static_assert(offsetof(pathlocaluser_t, nRegion) == 0x04,
              "pathlocaluser_t::nRegion  sub_491F50 `mov word ptr [69B78Ch], bx`");
static_assert(offsetof(pathlocaluser_t, bMarked) == 0x06,
              "pathlocaluser_t::bMarked  sub_491F50 `mov byte ptr [edi+6h], ch`");
static_assert(offsetof(pathlocaluser_t, bTarget) == 0x07,
              "pathlocaluser_t::bTarget  sub_491F50 `mov byte ptr [edi+7h], dl`");
static_assert(offsetof(pathlocaluser_t, bGoal) == 0x08,
              "pathlocaluser_t::bGoal  sub_491F50 `cmp byte ptr [ebx+8h], 0h`");
static_assert(offsetof(pathlocaluser_t, bClosed) == 0x09,
              "pathlocaluser_t::bClosed  sub_491F50 `mov byte ptr [ebx+9h], 1h`");
static_assert(offsetof(pathlocaluser_t, b0A) == 0x0A,
              "pathlocaluser_t::b0A    sub_491F50 `cmp byte ptr [ebx+0Ah], 0h`");
static_assert(offsetof(pathlocaluser_t, nDepth) == 0x0B,
              "pathlocaluser_t::nDepth  sub_491F50 `mov al, byte ptr [esi+0Bh] / inc al`");
static_assert(offsetof(pathlocaluser_t, pParent) == 0x10,
              "pathlocaluser_t::pParent  sub_491F50 `mov ebx, dword ptr [ebx+10h]`");
OC_X86_LAYOUT(offsetof(pathlocaluser_t, nH) == 0x14,
              "pathlocaluser_t::nH     sub_491F50 `mov dword ptr [69B79Ch], esi` after veclen");
OC_X86_LAYOUT(offsetof(pathlocaluser_t, nG) == 0x18,
              "pathlocaluser_t::nG     sub_491F50 `mov ecx, dword ptr [esi+18h] / add ecx, eax`");
OC_X86_LAYOUT(offsetof(pathlocaluser_t, nKey) == 0x1C,
              "pathlocaluser_t::nKey   sub_491F50 seeds it 400000h; the heap compares it whole and signed");

// The heap this TU instantiates. From the three member bodies: the elements are
// indexed `[esi+i*4]` from 1, the count sits at +0x25Ch (151*4, just past them)
// and the sentinel at +0x260h is parked in slot 0, so the sift-up needs no index
// test. Hyperion: the
// 64-bit build's holds a larger map's budget, g_epLocalQueue being the limit a search
// keeps to.
#if defined(_M_IX86)
#define LOCAL_QUEUE_CAP 150
#else
#define LOCAL_QUEUE_CAP EP_LOCAL_QUEUE_MAX
#endif
template <> struct PriorityQueue<pathlocaluser_t,150> {
  /*0x000*/ pathlocaluser_t *m_elems[LOCAL_QUEUE_CAP + 1];
  /*0x25C*/ int              m_count;
  /*0x260*/ pathlocaluser_t *m_pSentinel;

  void heapifyDown(int i);
  void heapUpdate(pathlocaluser_t *pElem, int nKey);
  void heapInsert(pathlocaluser_t *pElem);
};

// 0x491BA0 (109 B), plain `ret`, two register arguments and no stack. ECX is a
// contour_workspace_t because the body calls four of its members through it;
// EDX is the queue element sub_491F50 is standing on, whose +0x00/+0x02/+0x0A
// this body forwards to sub_488320. The one caller discards EAX.
void sub_491BA0(contour_workspace_t *pWs, pathlocaluser_t *pUser);

// 0x492860 (141 B), `ret 4h`. ECX is the search context, EDX the step count and
// the stack slot the packed-point array to copy back. Walks it backwards into
// adwPath, dropping the last step, and leaves the count in nPathCount. The one
// caller discards EAX.
void sub_492860(path_search_t *pFind, int nCount, const DWORD *pPath);

// 0x491C10 (829 B), plain `ret`, ECX only. Advance the region cursor to the
// unit's own region, pick the next waypoint, work out what is blocking it and
// hand the step to sub_491F50. Both area-search entry points tail-jmp here, so
// this row decides their return as well.
BOOL sub_491C10(path_search_t *pSearch);

// 0x491F50 (2305 B), `ret 4h`. The local tile search itself. The third argument
// is a point16_t by value: the single stack slot is read at both [ebp+08h] and
// [ebp+0Ah], and sub_491C10 pushes a packed dword into it.
BOOL sub_491F50(path_search_t *pSearch, int nRegion, point16_t pt);

// 0x4928F0 (5 B), `ret 8h`. A stub: `xor eax, eax / ret 8h`. None of the four
// arguments is read, but all five callers set ECX and EDX and push two words,
// and all five consume the full EAX. The names come from FindPlaceForUnit,
// which passes a unit type widened with movzx and a point widened with movsx.
int sub_4928F0(int nUnitType, int nX, int nY, int nArg3);


#endif  // OC117_SAI_PATHFINDLOCAL_H
