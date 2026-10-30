// playerlimits.h -- player slots.
//
// 1.17 has 12 slots: 0 to 7 the lobby's, 8 to 10 players a map can give units to, 11
// neutral. Hyperion keeps them as they are and adds slots from 12 up, so a game within
// 1.17's slots plays, saves and replays as 1.17's. With PLAYERS_MAX players a game uses
// slots 0 to 10 and 12 to PLAYERS_MAX; PLAYER_SLOTS_MAX counts every slot, neutral too.
// The masks that hold one bit a player give each slot its own bit, as 1.17's do
// (playervision.h).
#ifndef PLAYERLIMITS_H
#define PLAYERLIMITS_H

#include <string.h>
#include <atomic>
#include "typelimits.h"

#define PLAYER_SLOTS_117 12   // 1.17's slots
#define PLAYER_LOBBY_117 8    // the slots 1.17's lobby seats
#define PLAYER_NEUTRAL 11

#define PLAYERS_MAX 64        // the players a Hyperion game holds
#define PLAYER_SLOTS_MAX (PLAYERS_MAX + 1)
#define PLAYER_EXTRA_FIRST PLAYER_SLOTS_117  // the first of Hyperion's slots
#define PLAYER_EXTRA_SLOTS (PLAYER_SLOTS_MAX - PLAYER_SLOTS_117)

// The game has slots past 1.17's: set from the map as a game starts.
// While it is clear every player table reads 1.17's entries only, as 1.17 did, out-of-
// range trigger reads included.
extern bool g_hxPlayers;
// The slots of a game with Hyperion's slots: its map's (HXPL), at least 1.17's 12 and at
// most PLAYER_SLOTS_MAX. A loop over the slots runs to the game's, not the build's, so a
// game plays and hashes the same in a build of more slots (stage D).
extern int g_hxSlots;

// The slots a loop over every slot runs to: 1.17's 12, or with Hyperion's slots the game's.
static inline int PlayerSlots() { return g_hxPlayers ? g_hxSlots : PLAYER_SLOTS_117; }

// The slots a loop over the lobby's players runs to: 1.17's 8, or on a Hyperion map every
// slot of the game, which the loop walks skipping neutral's (PlayerIsLobby).
static inline int PlayerLobbySlots() { return g_hxPlayers ? g_hxSlots : PLAYER_LOBBY_117; }
// Unsigned, as 1.17's own tests of a player against 8 mostly are: a trigger's player field
// is any DWORD a map holds.
// The nations the computer player's round-robins serve for each one 1.17 served (revision 17):
// 1 with 1.17's 8 lobby slots, the slots over 8 rounded up past them, so each computer keeps
// 1.17's pace. With one turn shared by 65 slots, a computer's army was seen to about every
// 20 seconds and its plan about every 22, and its attacks waited on timers counted in plans.
static inline int PlayerAiTurns() { return (PlayerLobbySlots() + PLAYER_LOBBY_117 - 1) / PLAYER_LOBBY_117; }
static inline bool PlayerIsLobby(unsigned nSlot) {
  return nSlot < static_cast<unsigned>(PlayerLobbySlots()) && nSlot != PLAYER_NEUTRAL;
}

// Whether index nSlot of a table whose 1.17 entries end at nFirst is one of the extra
// entries: only in a game with Hyperion's slots, and only up to the last of them. An index
// past that (a raw trigger code from 27 up, any DWORD a map holds) reads 1.17's block past
// its entries, as 1.17 does, and never past the extra entries' storage.
static inline bool PlayerExtraIndex(int nSlot, int nFirst) {
  return g_hxPlayers && nSlot >= nFirst && nSlot < PLAYER_SLOTS_MAX;
}

// The extra entries' storage, one block for each tag and size: two names 1.17 gives one
// object, with element types of one size, share it.
// Each is part of the trainer's snapshots from its first use (port/replays/port_snap.cpp), untouched
// before it.
bool PortSnapStatic(void *pv, size_t cb);
template <class Tag, unsigned Bytes>
struct PlayerExtraStore {
  alignas(16) static unsigned char s_ab[Bytes];
  static std::atomic<bool> s_bKept;
  static void *Get() {
    // registered at the first use, then a load and a branch: a static with an initialiser
    // had the thread-safe guard on every call, 3.5 % of a 64-player game's thread
    if (!s_bKept.load(std::memory_order_acquire))
      Keep();
    return s_ab;
  }
  static void Keep() {
    // the guard's once, then the flag, so a thread that sees it sees the table registered
    static const bool s_bOnce = PortSnapStatic(s_ab, Bytes);
    (void)s_bOnce;
    s_bKept.store(true, std::memory_order_release);
  }
};
template <class Tag, unsigned Bytes>
alignas(16) unsigned char PlayerExtraStore<Tag, Bytes>::s_ab[Bytes];
template <class Tag, unsigned Bytes>
std::atomic<bool> PlayerExtraStore<Tag, Bytes>::s_bKept{false};

// A table of one entry a slot, laid out as 1.17's: the 1.17 entries in its data block at
// the global's address, and the entries of Hyperion's slots in storage of their own, one
// set per Tag. A global keeps its name and address and is declared with this type in place
// of T[N]; an index past N reaches the extra entries only in a game with such slots, and
// in any other game reads past the 12 as 1.17 did. a is 1.17's array, for the code that
// takes the block's address: the clears, the save's GAME dump, the sync hash's walk.
template <class T, class Tag, int N = PLAYER_SLOTS_117>
struct PlayerTable {
  enum { kExtra = PLAYER_SLOTS_MAX - N, kExtraCount = kExtra, kTypeCount = 0 };
  T a[N];
  static T *Extra() { return static_cast<T *>(PlayerExtraStore<Tag, sizeof(T) * kExtra>::Get()); }
  static T *TypeRows() { return nullptr; }
  T &operator[](int nSlot) { return PlayerExtraIndex(nSlot, N) ? Extra()[nSlot - N] : a[nSlot]; }
  const T &operator[](int nSlot) const { return PlayerExtraIndex(nSlot, N) ? Extra()[nSlot - N] : a[nSlot]; }
};

// The rows past 1.17's that a mod's types bring to a table with a row a type:
// a table of 228 rows, one a unit type, has rows for the mod's unit types too,
// from UNIT_TYPE_MOD_FIRST, each with every slot. Other tables have none.
template <int Rows>
struct PlayerTypeRows {
  enum { kFirst = Rows, kCount = 0 };
};
template <>
struct PlayerTypeRows<UNIT_TYPES_117> {
  enum { kFirst = UNIT_TYPE_MOD_FIRST, kCount = UNIT_TYPES_MAX - UNIT_TYPE_MOD_FIRST };
};

// Whether nRow of such a table is one of a mod's rows: only in a game with the mod's types,
// so that any other game reads past 1.17's rows as 1.17 did.
template <int Rows>
inline bool PlayerTypeRow(int nRow) {
  return PlayerTypeRows<Rows>::kCount != 0 && UnitTypeMod(static_cast<unsigned>(nRow));
}

// The mod's type rows of a table, in a store of their own beside the extra slots' entries.
template <class Tag>
struct PlayerTypeTag;

// Rows of one entry a slot, the slot the inner index as in 1.17's T[Rows][12]:
// grid[nRow][nSlot]. A row is a proxy that knows its number, so the code reads as before;
// a.b is 1.17's array. Extra holds the extra slots' entries, kExtraCount of them; TypeRows
// the mod's type rows, PLAYER_SLOTS_MAX entries each, kTypeCount in all.
template <class T, class Tag, int Rows>
struct PlayerGrid {
  typedef PlayerTypeRows<Rows> Types;
  enum {
    kExtra = PLAYER_SLOTS_MAX - PLAYER_SLOTS_117, kExtraCount = Rows * kExtra,
    kTypeCount = Types::kCount * PLAYER_SLOTS_MAX
  };
  T a[Rows][PLAYER_SLOTS_117];
  static T *Extra() { return static_cast<T *>(PlayerExtraStore<Tag, sizeof(T) * kExtraCount>::Get()); }
  static T *TypeRows() {
    return static_cast<T *>(PlayerExtraStore<PlayerTypeTag<Tag>, sizeof(T) * (kTypeCount ? kTypeCount : 1)>::Get());
  }
  struct Row {
    T *p;
    int nRow;
    T *pAll;  // a mod's type row, every slot in it; else null
    T &operator[](int nSlot) const {
      if (pAll != nullptr)
        return pAll[static_cast<unsigned>(nSlot) < PLAYER_SLOTS_MAX ? nSlot : PLAYER_NEUTRAL];
      return PlayerExtraIndex(nSlot, PLAYER_SLOTS_117)
                 ? Extra()[nRow * kExtra + nSlot - PLAYER_SLOTS_117] : p[nSlot];
    }
  };
  static Row TypeRow(int nRow) {
    Row r = {nullptr, nRow, TypeRows() + (nRow - Types::kFirst) * PLAYER_SLOTS_MAX};
    return r;
  }
  Row operator[](int nRow) {
    if (PlayerTypeRow<Rows>(nRow))
      return TypeRow(nRow);
    Row r = {a[nRow], nRow, nullptr};
    return r;
  }
  Row operator[](int nRow) const { return const_cast<PlayerGrid *>(this)->operator[](nRow); }
};

// The same over T[Outer][Rows][12]: grid[i][nRow][nSlot]. The mod's type rows of each plane
// follow one another in TypeRows.
template <class T, class Tag, int Outer, int Rows>
struct PlayerGrid3 {
  typedef PlayerGrid<T, Tag, Outer * Rows> Flat;
  typedef PlayerTypeRows<Rows> Types;
  enum { kExtraCount = Flat::kExtraCount, kTypeCount = Outer * Types::kCount * PLAYER_SLOTS_MAX };
  T a[Outer][Rows][PLAYER_SLOTS_117];
  static T *Extra() { return Flat::Extra(); }
  static T *TypeRows() {
    return static_cast<T *>(PlayerExtraStore<PlayerTypeTag<Tag>, sizeof(T) * (kTypeCount ? kTypeCount : 1)>::Get());
  }
  struct Plane {
    T (*p)[PLAYER_SLOTS_117];
    int nFirst;
    int i;
    typename Flat::Row operator[](int nRow) const {
      if (PlayerTypeRow<Rows>(nRow)) {
        typename Flat::Row r = {nullptr, nRow,
                                TypeRows() + (i * Types::kCount + nRow - Types::kFirst) * PLAYER_SLOTS_MAX};
        return r;
      }
      typename Flat::Row r = {p[nRow], nFirst + nRow, nullptr};
      return r;
    }
  };
  Plane operator[](int i) { Plane pl = {a[i], i * Rows, i}; return pl; }
  Plane operator[](int i) const { Plane pl = {const_cast<T (*)[PLAYER_SLOTS_117]>(a[i]), i * Rows, i}; return pl; }
};

// One entry for each pair of slots, as 1.17's T[12][12]: matrix[nSlot][nOther]. The pairs
// past 1.17's live in a full matrix of their own.
template <class T, class Tag>
struct PlayerMatrix {
  enum { kExtraCount = PLAYER_SLOTS_MAX * PLAYER_SLOTS_MAX, kTypeCount = 0 };
  T a[PLAYER_SLOTS_117][PLAYER_SLOTS_117];
  static T *Extra() { return static_cast<T *>(PlayerExtraStore<Tag, sizeof(T) * kExtraCount>::Get()); }
  static T *TypeRows() { return nullptr; }
  struct Row {
    T *p;
    int nSlot;
    T &operator[](int nOther) const {
      return Extra2(nSlot, nOther) ? Extra()[nSlot * PLAYER_SLOTS_MAX + nOther] : p[nOther];
    }
  };
  // A pair goes to the full matrix when either is one of Hyperion's slots and neither is
  // past the last; any other pair reads 1.17's block, as 1.17 does.
  static bool Extra2(int nSlot, int nOther) {
    return (PlayerExtraIndex(nSlot, PLAYER_SLOTS_117) && nOther >= 0 && nOther < PLAYER_SLOTS_MAX) ||
           (PlayerExtraIndex(nOther, PLAYER_SLOTS_117) && nSlot >= 0 && nSlot < PLAYER_SLOTS_MAX);
  }
  Row operator[](int nSlot) { Row r = {a[nSlot], nSlot}; return r; }
  Row operator[](int nSlot) const { Row r = {const_cast<T *>(a[nSlot]), nSlot}; return r; }
};

// The whole-table clear and copy 1.17 does with memset and memcpy over its array, over the
// extra entries and the mod's type rows too.
template <class P>
inline void PlayerFill(P &t, int nByte) {
  memset(t.a, nByte, sizeof t.a);
  memset(P::Extra(), nByte, sizeof(*P::Extra()) * P::kExtraCount);
  if (P::kTypeCount != 0)
    memset(P::TypeRows(), nByte, sizeof(*P::Extra()) * P::kTypeCount);
}

template <class P>
inline void PlayerClear(P &t) { PlayerFill(t, 0); }

template <class P, class Q>
inline void PlayerCopy(P &d, const Q &s) {
  static_assert(sizeof d.a == sizeof s.a && (int)P::kExtraCount == (int)Q::kExtraCount,
                "PlayerCopy: tables of one shape");
  memcpy(d.a, s.a, sizeof d.a);
  memcpy(P::Extra(), Q::Extra(), sizeof(*P::Extra()) * P::kExtraCount);
  if (P::kTypeCount != 0)
    memcpy(P::TypeRows(), Q::TypeRows(), sizeof(*P::Extra()) * P::kTypeCount);
}

#endif
