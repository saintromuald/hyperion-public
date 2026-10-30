// port_veclen.cpp -- veclen and veclen_precise, the game's integer distance.
//
// Both approximate the length of (ax - bx, ay - by) from the larger and smaller absolute
// difference with shifts and adds, an octagon fitted to the circle, so every machine computes
// the same number. They feed the simulation: range and distance checks, movement toward
// waypoints, path costs, and (the precise one, through CFlingy::InterceptTarget) leading a
// moving target. Blizzard wrote them in assembly (asm\distance.asm, "approx_distance"), so no
// C++ compiles to their bytes; these match their results for every 32-bit input, which
// tools/porttest/veclen_test.cpp checks against the oracle's own code. The arithmetic is
// uint32_t on purpose: the oracle's differences wrap, its abs leaves INT_MIN as INT_MIN, its
// shifts are logical and its two comparisons signed, which shows only beyond map-sized inputs.
#include "port_veclen.h"
#include <stdint.h>

// |a - b| the way the oracle forms it: 32-bit wrap, then sar/xor/sub.
static inline uint32_t PortAbsDiff(int a, int b)
{
  uint32_t d = (uint32_t)a - (uint32_t)b;
  uint32_t s = (uint32_t)((int32_t)d >> 31);
  return (d ^ s) - s;
}

// Orders the two differences, larger first, by a signed compare.
static inline void PortMaxMin(int ax, int bx, int ay, int by, uint32_t *pMax, uint32_t *pMin)
{
  uint32_t dx = PortAbsDiff(ax, bx);
  uint32_t dy = PortAbsDiff(ay, by);
  if ((int32_t)dx >= (int32_t)dy) {
    *pMax = dx;
    *pMin = dy;
  } else {
    *pMax = dy;
    *pMin = dx;
  }
}

// 0x405CF0, 77 B in the oracle. Near an axis (min at most max/4) the answer is
// max; otherwise max*(1 - 1/16 - 1/64) + 3*min/8 + 3*min/256.
int __fastcall PortVeclen(int ax, int bx, int ay, int by)
{
  uint32_t max, min;
  PortMaxMin(ax, bx, ay, by, &max, &min);
  if ((int32_t)(max >> 2) >= (int32_t)min)
    return (int)max;
  uint32_t t = (min * 3) >> 3;
  return (int)(max - (max >> 4) - (max >> 6) + t + (t >> 5));
}

// 0x405C90, 93 B in the oracle. The same shape with finer terms: near an axis
// max*(1 + 1/32) + (3*min/2)/128, otherwise
// max*(1 - 1/128) + (3*min/2)/128 + (3*min/2)/4 + (3*min/2)/32.
int __fastcall PortVeclenPrecise(int ax, int bx, int ay, int by)
{
  uint32_t max, min;
  PortMaxMin(ax, bx, ay, by, &max, &min);
  uint32_t h = (min * 3) >> 1;
  uint32_t r = max + (max >> 5) + (h >> 7);
  if ((int32_t)(max >> 2) >= (int32_t)min)
    return (int)r;
  uint32_t k = h >> 2;
  return (int)(r - (max >> 5) - (max >> 7) + k + (k >> 3));
}
