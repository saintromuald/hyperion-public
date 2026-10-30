// net_rand.cpp -- the networked PRNG: a linear congruential generator whose
// state and per-tag roll counters are shared across the game so every machine
// draws the same sequence. Bodies are in oracle VA order. net_randbetween
// carries net_rand's body inline, as 1.17 does, so the two stay in one TU.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/net_rand.h"
#include "../gen/globals117.h"

#include <string.h>   // memset, which net_srand clears the tag table with

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x673930 -- the generator's on/off gate, a full dword.
#ifdef OC_DLL_MODE
#define sgbNetRandEnabled_g (*reinterpret_cast<int *>(OC_VA(0x673930u)))
#else
extern int sgbNetRandEnabled_g;
#endif

// 0x673934 -- the fixed seed.
#ifdef OC_DLL_MODE
#define sgNetFixedSeed_g (*reinterpret_cast<int *>(OC_VA(0x673934u)))
#else
extern int sgNetFixedSeed_g;
#endif

// 0x673938 -- the total roll counter, incremented on every draw the gate lets
// through, and reset by net_srand.
#ifdef OC_DLL_MODE
#define sgNetRandCount_g (*reinterpret_cast<int *>(OC_VA(0x673938u)))
#else
extern int sgNetRandCount_g;
#endif

// 0x67393C -- the LCG state itself, unsigned as 1.17's shift is.
// net_randsequence returns it raw.
#ifdef OC_DLL_MODE
#define sgNetRandState_g (*reinterpret_cast<unsigned int *>(OC_VA(0x67393Cu)))
#else
extern unsigned int sgNetRandState_g;
#endif

// 0x673940 -- one counter per call-site tag, 256 of them.
#ifdef OC_DLL_MODE
#define sgNetRandTagCounts_g (reinterpret_cast<int *>(OC_VA(0x673940u)))
#else
extern int sgNetRandTagCounts_g[256];
#endif

// The bodies, in oracle VA order.

// 0x4781C0 (12 B). Sets the gate and hands back what it displaced.
int set_rand_enabled(int nEnabled)
{
  const int nWas = sgbNetRandEnabled_g;

  sgbNetRandEnabled_g = nEnabled;
  return nWas;
}

// 0x4781D0 (6 B). The raw state, not a draw: no roll, no counter.
int net_randsequence()
{
  return static_cast<int>(sgNetRandState_g);
}

// 0x4781E0 (6 B). Returns the fixed seed.
int net_get_fixed_seed()
{
  return sgNetFixedSeed_g;
}

// 0x4781F0 (7 B). Sets the fixed seed.
void net_set_fixed_seed(int nSeed)
{
  sgNetFixedSeed_g = nSeed;
}

// 0x478200 (50 B). The draw. 0x15A4E35 is 22695477, and the result is bits 16
// to 30 of the advanced state. With the gate off it answers 0 and counts
// nothing.
int net_rand(int nTag)
{
  if (sgbNetRandEnabled_g == 0)
    return 0;

  sgNetRandTagCounts_g[nTag]++;
  sgNetRandCount_g++;
  sgNetRandState_g = sgNetRandState_g * 0x15A4E35 + 1;
  return static_cast<int>((sgNetRandState_g >> 16) & 0x7FFF);
}

// 0x478240 (41 B). Reseeds: the roll counter is cleared before the tag table,
// and the state is written last.
void net_srand(int nSeed)
{
  sgNetRandCount_g = 0;
  memset(sgNetRandTagCounts_g, 0, 0x400);
  sgNetRandState_g = nSeed;
}

// 0x478270 (84 B). `ret 4h`: two register arguments and one stack word.
// net_rand is inlined here, both arms of it: with the gate off the 0 it answers
// is scaled like any draw, so the result is nMin.
int net_randbetween(int nMin, int nMax, int nTag)
{
  return static_cast<int>(
             static_cast<unsigned int>(net_rand(nTag) * (nMax - nMin + 1)) >> 15)
         + nMin;
}
