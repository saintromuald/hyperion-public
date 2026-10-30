// intrin.h, off Windows: the compiler's x86 intrinsics. The engine uses SSE2 and
// __rdtsc; an ARM build takes them from emmintrin.h here and the counter below.
#pragma once
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#elif defined(__aarch64__)
#include "emmintrin.h"
// The time stamp: ARM's virtual counter, which ticks at a fixed rate as an x86's
// invariant TSC does. The game reads it only to time things (clock.cpp).
static inline unsigned long long __rdtsc(void)
{
  unsigned long long v;
  __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v));
  return v;
}
#elif defined(__EMSCRIPTEN__)
#include "emmintrin.h"
#include <emscripten/emscripten.h>
// The time stamp in a browser: its clock in microseconds, which ticks at a fixed
// rate as an invariant TSC does.
static inline unsigned long long __rdtsc(void)
{
  return static_cast<unsigned long long>(emscripten_get_now() * 1000.0);
}
#endif
