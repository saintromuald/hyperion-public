// pos.h -- map positions of 32 bits.
//
// 1.17 keeps a map position in 16 bits, which stop at 32,767 pixels signed or 65,535
// unsigned, so a map of 1,024 tiles a side (32,768 pixels) does not fit. On x64 a
// position is one of the two 4-byte types below, read as an int as 1.17's short or WORD
// is promoted to one. A store wraps to 16 bits as 1.17's does while g_posShift is 16, so
// a 1.17 game reads back what 1.17 read; a Hyperion game sets it to 1.
//
// The x86 build keeps 1.17's types, so the save's layouts (tools/save_layouts.py) see a
// 2-byte field on x86 and a 4-byte one on x64, narrow it to 16 bits in 1.17's record
// (SAVE117_POS16S, SAVE117_POS16U) and read it back sign-extended, the stored form below.
#ifndef POS_H
#define POS_H
#include "platform.h"  // LONG and ULONG

#include <stddef.h>  // point16.h and rect16.h check their layouts with offsetof
#include "point16.h"
#include "rect16.h"

// 16: a store wraps to 16 bits, as 1.17's; 1: to 31 bits (a Hyperion game, PoolsForGame).
// The mask is what upos_t reads of the stored value: its low 16 bits, or its low 31. A
// Hyperion game's positions are then 1.17's at twice the width less one bit: a negative
// one read unsigned is a large one (`(WORD)x >= width` rejects x < 0), it stays positive
// in an int, and a sum that leaves the range and comes back, `(WORD)(y - 30) + 30`, comes
// back to y as 1.17's does modulo 65,536. Keeping the whole value instead let such a sum
// overflow, and a flyer by the top edge was clipped to the bottom one.
extern int g_posShift;
extern int g_posMaskU;

#if defined(_WIN64) || defined(__LP64__)

// The stored form is the same whichever type stores it: wrapped and sign-extended. The
// same bytes are read signed by one view and unsigned by another (CFlingy's short and
// CUnit's WORD at +0x28), so the sign is the read's: spos_t reads the stored value,
// upos_t its low 16 bits, or its low 31 in a Hyperion game.
static inline int PosStore(int v) {
  return static_cast<int>(static_cast<unsigned>(v) << g_posShift) >> g_posShift;
}
static inline int PosReadU(int v) { return v & g_posMaskU; }
static inline int PosReadS(int v) { return v; }

// The constructor from an int is implicit, as a short's is, so a short local or parameter
// becomes a position by its type alone and wraps where 1.17's did. A ternary
// of a position and an int then has two meanings and does not compile; it takes the
// position as an int.
#define OC_POS_OPS(T, READ)                                                    \
  T() = default;                                                               \
  T(int n) : v(PosStore(n)) {}                                                 \
  operator int() const { return READ(v); }                                     \
  T &operator=(int n) { v = PosStore(n); return *this; }                       \
  T &operator+=(int n) { v = PosStore(READ(v) + n); return *this; }            \
  T &operator-=(int n) { v = PosStore(READ(v) - n); return *this; }            \
  T &operator*=(int n) { v = PosStore(READ(v) * n); return *this; }            \
  T &operator/=(int n) { v = PosStore(READ(v) / n); return *this; }            \
  T &operator%=(int n) { v = PosStore(READ(v) % n); return *this; }            \
  T &operator&=(int n) { v = PosStore(READ(v) & n); return *this; }            \
  T &operator|=(int n) { v = PosStore(READ(v) | n); return *this; }            \
  T &operator^=(int n) { v = PosStore(READ(v) ^ n); return *this; }            \
  T &operator<<=(int n) { v = PosStore(READ(v) << n); return *this; }          \
  T &operator>>=(int n) { v = PosStore(READ(v) >> n); return *this; }          \
  T &operator++() { v = PosStore(READ(v) + 1); return *this; }                 \
  T &operator--() { v = PosStore(READ(v) - 1); return *this; }                 \
  int operator++(int) { int n = READ(v); v = PosStore(n + 1); return n; }      \
  int operator--(int) { int n = READ(v); v = PosStore(n - 1); return n; }

struct upos_t;

// A position 1.17 keeps signed (short).
struct spos_t {
  int v;
  OC_POS_OPS(spos_t, PosReadS)
  inline spos_t(const upos_t &u);
  inline spos_t &operator=(const upos_t &u);
};

// A position 1.17 keeps unsigned (WORD).
struct upos_t {
  int v;
  OC_POS_OPS(upos_t, PosReadU)
  upos_t(const spos_t &s) : v(s.v) {}
  upos_t &operator=(const spos_t &s) { v = s.v; return *this; }
};

// One type to the other keeps the stored form, as 1.17 reads a short's bytes as a WORD's
// and back.
inline spos_t::spos_t(const upos_t &u) : v(u.v) {}
inline spos_t &spos_t::operator=(const upos_t &u) { v = u.v; return *this; }

#undef OC_POS_OPS

// A point and a rectangle on the map, of positions above. Each converts to and from
// 1.17's point16_t and rect16_t, which the dialogs keep, so a function that still takes
// 1.17's type takes these as 1.17 did.
struct mpoint_t {
  spos_t x;
  spos_t y;
  mpoint_t() = default;
  mpoint_t(int nx, int ny) { x = nx; y = ny; }
  mpoint_t(const point16_t &p) { x = p.x; y = p.y; }
  mpoint_t &operator=(const point16_t &p) { x = p.x; y = p.y; return *this; }
  operator point16_t() const {
    point16_t p;
    p.x = static_cast<short>(x);
    p.y = static_cast<short>(y);
    return p;
  }
};

struct mrect_t {
  spos_t left;
  spos_t top;
  spos_t right;
  spos_t bottom;
  mrect_t() = default;
  mrect_t(const rect16_t &r) { left = r.left; top = r.top; right = r.right; bottom = r.bottom; }
  mrect_t &operator=(const rect16_t &r) {
    left = r.left; top = r.top; right = r.right; bottom = r.bottom;
    return *this;
  }
  operator rect16_t() const {
    rect16_t r;
    r.left = static_cast<short>(left);
    r.top = static_cast<short>(top);
    r.right = static_cast<short>(right);
    r.bottom = static_cast<short>(bottom);
    return r;
  }
};

static_assert(sizeof(spos_t) == 4 && sizeof(upos_t) == 4, "a position is 4 bytes on x64");
static_assert(sizeof(mpoint_t) == 8 && sizeof(mrect_t) == 16, "map points and rectangles of them");

#else

typedef short spos_t;
typedef unsigned short upos_t;
typedef point16_t mpoint_t;
typedef rect16_t mrect_t;

#endif

// 1.17's cast of a position to short or WORD: the same value in a 1.17 game, the value
// wrapped to 31 bits in a Hyperion game.
#if defined(_WIN64) || defined(__LP64__)
static inline int PosS(int v) { return PosStore(v); }
static inline int PosU(int v) { return PosReadU(PosStore(v)); }
#else
static inline int PosS(int v) { return static_cast<short>(v); }
static inline int PosU(int v) { return static_cast<unsigned short>(v); }
#endif

// A map point from its two positions, on either width.
static inline mpoint_t PosPoint(int x, int y) {
  mpoint_t p;
  p.x = static_cast<spos_t>(x);
  p.y = static_cast<spos_t>(y);
  return p;
}

// A point packed into one DWORD, x in the low half, as 1.17 moves one: a position's low 16
// bits each (stage E keeps the DWORDs that hold one until positions widen, E4).
static inline ULONG PosPack(int x, int y) {
  return (static_cast<ULONG>(y) << 16) | (static_cast<ULONG>(x) & 0xFFFF);
}
static inline int PosPackedX(ULONG d) { return static_cast<short>(d & 0xFFFF); }
static inline int PosPackedY(ULONG d) { return static_cast<short>(d >> 16); }

#endif
