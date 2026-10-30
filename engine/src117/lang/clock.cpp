// clock.cpp -- the time-stamp counter reads Blizzard kept in asm\clock.asm.
// Bodies are in oracle VA order.

#include <intrin.h>
#include "../h/clock.h"
#include "../gen/globals117.h"

// Blizzard wrote these three in assembly; the compiler intrinsics give the same
// bytes. cli and sti fault outside ring 0, and the oracle executes them anyway.

void sub_405C70(unsigned __int64 *pStamp)
{
  *pStamp = __rdtsc();
}

void sub_405C78(unsigned __int64 *pStamp)
{
  _disable();
  *pStamp = __rdtsc();
}

// The stamp is read before sti and stored after it.
void sub_405C81(unsigned __int64 *pStamp)
{
  unsigned __int64 qwStamp = __rdtsc();

  _enable();
  *pStamp = qwStamp;
}
