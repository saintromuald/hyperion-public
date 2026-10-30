// emmintrin.h, off Windows. On x86 the compiler's own, and in a browser
// Emscripten's, over WebAssembly's SIMD; on ARM the SSE2 operations the port
// uses (port_mask.cpp, port_present.cpp), in NEON, with SSE2's results.
#pragma once
#if defined(__x86_64__) || defined(__i386__) || defined(__EMSCRIPTEN__)
#include_next <emmintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>

typedef int8x16_t __m128i;

static inline __m128i _mm_setzero_si128(void) { return vdupq_n_s8(0); }
static inline __m128i _mm_set1_epi8(char b) { return vdupq_n_s8(static_cast<int8_t>(b)); }
static inline __m128i _mm_loadu_si128(const __m128i *p) { return vld1q_s8(reinterpret_cast<const int8_t *>(p)); }
static inline void _mm_storeu_si128(__m128i *p, __m128i a) { vst1q_s8(reinterpret_cast<int8_t *>(p), a); }
static inline __m128i _mm_and_si128(__m128i a, __m128i b) { return vandq_s8(a, b); }
static inline __m128i _mm_or_si128(__m128i a, __m128i b) { return vorrq_s8(a, b); }
// SSE2's andnot is (~a) & b; NEON's bic is a & ~b
static inline __m128i _mm_andnot_si128(__m128i a, __m128i b) { return vbicq_s8(b, a); }
static inline __m128i _mm_cmpeq_epi8(__m128i a, __m128i b) { return vreinterpretq_s8_u8(vceqq_s8(a, b)); }

// The top bit of each byte, byte 0 in bit 0.
static inline int _mm_movemask_epi8(__m128i a)
{
  static const int8_t kShift[16] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7};
  const uint8x16_t top = vshrq_n_u8(vreinterpretq_u8_s8(a), 7);
  const uint8x16_t bits = vshlq_u8(top, vld1q_s8(kShift));
  return static_cast<int>(vaddv_u8(vget_low_u8(bits))) | (static_cast<int>(vaddv_u8(vget_high_u8(bits))) << 8);
}
#endif
