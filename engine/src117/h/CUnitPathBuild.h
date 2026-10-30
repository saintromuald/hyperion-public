// CUnitPathBuild.h -- the free functions of src117/lang/CUnitPathBuild.cpp, which starts a
// unit's path: sizing a path node, the two path starts, and the destination resolve behind
// them. The class members are declared in CUnit.h with the rest of the class. Free
// functions are spelled without an explicit __fastcall, which /Gr already makes the
// default.

#ifndef OC117_CUNITPATHBUILD_H
#define OC117_CUNITPATHBUILD_H

#include "platform.h"

class CUnit;

// A pointee the oracle bytes do not name stays void *.

// 0x42C840 (96 B). It has no ret of its own: it ends `jmp sai_path_new`, so the tail call
// pins the return type, and sai_path_new hands back the node it popped. Its one caller
// consumes the full EAX. Both register arguments are pointers to a 16-bit value, not ints;
// EDX's pointee is read both zero- and sign-extended, so its sign is unsettled.
void *sub_42C840(short *pCount, short *pArg1);

// 0x42C930 (103 B), `ret 8h`. Its one caller discards EAX, so only the callee
// settles the return: the early exit materialises `xor eax, eax` into a
// duplicated epilogue, and the tail falls out of `call sub_42C9A0`.
BOOL sub_42C930(intptr_t nArg0, int nArg1, int nArg2, int nArg3);

// 0x42C9A0 (222 B), `ret 4h`. One consuming caller uses the full EAX and every
// ret materialises 0 or 1.
BOOL sub_42C9A0(intptr_t nArg0, intptr_t nArg1, int nArg2);

#endif  // OC117_CUNITPATHBUILD_H
