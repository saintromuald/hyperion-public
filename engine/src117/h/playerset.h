// playerset.h -- a set of player slots, one bit a slot.
//
// 1.17 holds a set of players in a BYTE or a DWORD, each slot's bit at the slot. A
// Hyperion game's slots run to PLAYER_SLOTS_MAX - 1, past a DWORD's 32 bits with 32
// players and past 64 with 64, so a set of them is as many 64-bit words as
// PLAYER_SLOTS_MAX needs. Its first 32 bits are a 1.17 DWORD's, so a set and a DWORD of
// 1.17's agree on every slot the DWORD holds.
#ifndef PLAYERSET_H
#define PLAYERSET_H
#include "platform.h"  // LONG and ULONG

#include "playerlimits.h"

#define PLAYER_SET_WORDS ((PLAYER_SLOTS_MAX + 63) / 64)

// Whether a build has slots past 31, which 1.17's DWORDs of players do not hold. The
// port's records of those slots (detection, shared vision) are saved and hashed only in
// such a build, and the parasites and blinders as whole sets, so a build of fewer slots
// writes and hashes what it did before.
#define PLAYER_SETS_WIDE (PLAYER_SLOTS_MAX > 32)

// Plain data, so that arrays of it are cleared with memset and saved as bytes.
struct PlayerSet {
  unsigned long long w[PLAYER_SET_WORDS];

  static PlayerSet None() {
    PlayerSet s;
    for (int i = 0; i < PLAYER_SET_WORDS; i++)
      s.w[i] = 0;
    return s;
  }
  static PlayerSet All() {
    PlayerSet s;
    for (int i = 0; i < PLAYER_SET_WORDS; i++)
      s.w[i] = ~0ull;
    return s;
  }
  // A 1.17 mask of players, a BYTE or a DWORD.
  static PlayerSet FromBits(unsigned long long q) {
    PlayerSet s = None();
    s.w[0] = q;
    return s;
  }
  static PlayerSet Of(int nSlot) {
    PlayerSet s = None();
    s.Add(nSlot);
    return s;
  }

  bool Has(int nSlot) const { return (w[nSlot >> 6] >> (nSlot & 63) & 1) != 0; }
  void Add(int nSlot) { w[nSlot >> 6] |= 1ull << (nSlot & 63); }
  void Remove(int nSlot) { w[nSlot >> 6] &= ~(1ull << (nSlot & 63)); }
  bool Any() const {
    for (int i = 0; i < PLAYER_SET_WORDS; i++)
      if (w[i] != 0)
        return true;
    return false;
  }
  // The slots a 1.17 DWORD holds, and those its BYTE does.
  ULONG Low32() const { return static_cast<ULONG>(w[0]); }
  ULONG Low8() const { return static_cast<ULONG>(w[0] & 0xFF); }

  PlayerSet &operator|=(const PlayerSet &o) {
    for (int i = 0; i < PLAYER_SET_WORDS; i++)
      w[i] |= o.w[i];
    return *this;
  }
  PlayerSet &operator&=(const PlayerSet &o) {
    for (int i = 0; i < PLAYER_SET_WORDS; i++)
      w[i] &= o.w[i];
    return *this;
  }
  PlayerSet operator~() const {
    PlayerSet s;
    for (int i = 0; i < PLAYER_SET_WORDS; i++)
      s.w[i] = ~w[i];
    return s;
  }
  friend PlayerSet operator|(PlayerSet a, const PlayerSet &b) { return a |= b; }
  friend PlayerSet operator&(PlayerSet a, const PlayerSet &b) { return a &= b; }
  friend PlayerSet operator^(PlayerSet a, const PlayerSet &b) {
    for (int i = 0; i < PLAYER_SET_WORDS; i++)
      a.w[i] ^= b.w[i];
    return a;
  }
  friend bool operator==(const PlayerSet &a, const PlayerSet &b) {
    for (int i = 0; i < PLAYER_SET_WORDS; i++)
      if (a.w[i] != b.w[i])
        return false;
    return true;
  }
  friend bool operator!=(const PlayerSet &a, const PlayerSet &b) { return !(a == b); }
};

static inline PlayerSet PlayerBit(int nSlot) { return PlayerSet::Of(nSlot); }
// The slots 1.17's BYTEs of players hold (0 to 7), and the others.
static inline PlayerSet PlayerSetLow() { return PlayerSet::FromBits(0xFF); }
static inline PlayerSet PlayerSetHigh() { return ~PlayerSetLow(); }

#endif
