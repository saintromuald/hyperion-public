// debug.h -- declarations for src117/lang/debug.cpp, the dirty-tile
// invalidation. Free functions are __fastcall through /Gr and are declared
// without the keyword.

#ifndef OC117_DEBUG_H
#define OC117_DEBUG_H

#include "platform.h"

struct rect32_t;

// 0x43EB50 (164 B). ECX the pixel rectangle, rewritten in place as 16-pixel
// tiles; every caller discards EAX.
void invalidate_rect(rect32_t *prc);

#endif  // OC117_DEBUG_H
