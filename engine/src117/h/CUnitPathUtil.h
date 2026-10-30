// CUnitPathUtil.h -- the free functions of src117/lang/CUnitPathUtil.cpp: the intersection
// and land tests the path code asks, and the map-bounds check over a unit type's box. The
// class members are declared in CUnit.h with the rest of the class.

#ifndef OC117_CUNITPATHUTIL_H
#define OC117_CUNITPATHUTIL_H

#include "platform.h"
#include "pos.h"        // Hyperion: spos_t

// 0x42ED50, 161 B. `ret 4h`, two register arguments plus one stack word. Seven consuming
// callers use the full EAX and none narrows it. The body bounds-checks a unit-type box
// against the map, indexing gUnitBoundBox with ECX against gwMapPixWidth and
// gwMapPixHeight. The two coordinates are 16-bit and only the caller proves it: sub_42C8C0
// calls it with `movzx eax, word ptr [esi+2h] / mov dx, word ptr [esi]`, which an int
// parameter cannot produce; the sign comes from this body's own `cwde` and `movsx eax, bx`.
int sub_42ED50(int nType, spos_t x, spos_t y);


// The rest of this TU's free functions. A pointee the oracle bytes do not name stays void *.

// 0x42E130 (133 B). Both register arguments are read as 32 bits; one consuming
// caller and every ret materialises 0 or 1.
BOOL sub_42E130(intptr_t nArg0, intptr_t nArg1);

// 0x42E1C0 (183 B), `ret 8h`. The second stack slot is loaded and then
// dereferenced.
intptr_t sub_42E1C0(intptr_t nArg0, int nArg1, int nArg2, void *pArg3);

// 0x42E280 (211 B). ECX is forwarded to a callee untouched, so this body shows
// no width for it; the callee's contract is the place to read it.
intptr_t sub_42E280(intptr_t nArg0, intptr_t nArg1);

// 0x42E490 (142 B). Five consuming callers use the full EAX.
BOOL IsIntersectable(intptr_t nArg0, intptr_t nArg1);

// 0x42E670 (71 B). Its address is taken once and it has no direct call site, so
// the arity here is a lower bound and the indirect call site is the authority
// on what rides ECX and EDX.
BOOL update_unit_to_unit_collision(intptr_t nArg0, void *pArg1);

// 0x42EA10 (154 B). Both coordinates are read with movsx, so both are signed
// 16-bit; three consuming callers use the full EAX.
BOOL is_land_pixel(spos_t x, spos_t y);

// 0x42EBF0 (119 B), six consuming callers use the full EAX.
int sub_42EBF0(intptr_t nArg0, intptr_t nArg1);

// 0x42EC70 (211 B), `ret 8h`. Both stack slots are scratch, not references.
// The first two are 16-bit on the same caller evidence as sub_42ED50, and
// signed: the body tests them `test ax, ax / js` and passes both to
// is_land_pixel, whose own coordinates are signed 16-bit.
int sub_42EC70(spos_t x, spos_t y, int nArg2, int nArg3);

#endif  // OC117_CUNITPATHUTIL_H
