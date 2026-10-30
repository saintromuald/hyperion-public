// port_synchash.cpp -- a hash of the game's state a domain at a time (port_synchash.h;
// docs/modules/loop.md, "The state check"). It only reads.
//
// The records are the save's: each pool's records in use, walked field by field with the save's
// layouts (gen/save117.h), so padding is not hashed. A pointer is hashed as the 1.17 address it
// stands for (PortOriginalAddress), a unit or an order by its slot, and a pointer elsewhere (a
// GRP, a script, the heap) only as being set. A sprite or an image is hashed as what it shows
// (its type, owner and place), since the view takes some for itself and a game and its replay
// give the same sprite other slots; the sprites domain walks them from their owners. The player
// tables are 1.17's block the save's GAME section holds, then the slots past 11's entries.
//
// What a player's view sets is left out, since a replay is watched from another view than the
// game was played from: the camera and the lobby's bytes in GAME, the cell map and creep edges
// the screen draws, the square map's bit 30 (cellscrl.cpp), an image's map and screen place,
// redraw flag and colouring, a unit's under-attack notice and minimap blink, a sprite's slot,
// selection and flashing, the second thingy pool (the building copies each player's fog keeps),
// and the black X the local player's right-clicks move (show_target_x).
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <vector>

#include "CUnit.h"
#include "CSprite.h"
#include "CImage.h"
#include "CBullet.h"
#include "CThingy.h"
#include "COrder.h"
#include "CFlingy.h"
#include "oc_data.h"
#include "../../h/creep.h"           // getCreepMap
#include "../../h/saveload.h"        // get_tech_available and the other tech accessors
#include "../../h/point16.h"
#include "../../h/pos.h"           // g_posShift
#include "../../h/playertables.h"
#include "../../h/playervision.h"
#include "../../h/port_synchash.h"
#include "../../h/SAI_Banker.h"
#include "../../h/SAI_Build.h"
#include "../../h/SAI_Scripts.h"
#include "../../h/TAI_Captain.h"
#include "../../h/TAI_GuardPost.h"
#include "../../h/sai_TownBuilder.h"
#include "../../h/sai.h"             // NumAreas
#include "../../h/selectlimits.h"     // SELECTION_MAX
#include "../../h/statbtn.h"          // is_tech_in_progress
#include "../data/port_formats.h"  // PortSaveUsesPoint
#include "../platform/port_standalone.h"  // PortOriginalAddress, PortOriginalSpans
#include "port_workers.h"         // PortWorkersFor
#include "../gameplay/port_richbases.h"  // RichBasesTable
#include "../../gen/globals117.h"
#define SAVE117_TABLES
#include "../../gen/save117.h"
#include "../platform/port_globals117.h"

// A save117.h made before the types last changed builds and runs wrong: it is made from
// probes of the compiled types, tools/wide_globals.py and then tools/save_layouts.py.
#define OC_SAVE117_FRESH(layout, T)                                                   \
  static_assert(kSave117Layouts[layout].size == sizeof(T), "gen/save117.h is older than " #T \
                ": run tools/wide_globals.py, then tools/save_layouts.py")
OC_SAVE117_FRESH(SAVE117_CImage, CImage);
OC_SAVE117_FRESH(SAVE117_CSprite, CSprite);
OC_SAVE117_FRESH(SAVE117_CThingy, CThingy);
OC_SAVE117_FRESH(SAVE117_CFlingy, CFlingy);
OC_SAVE117_FRESH(SAVE117_CUnit, CUnit);
OC_SAVE117_FRESH(SAVE117_CBullet, CBullet);
OC_SAVE117_FRESH(SAVE117_COrder, COrder);
OC_SAVE117_FRESH(SAVE117_Captain, TAI_Captain);
OC_SAVE117_FRESH(SAVE117_PlayerTowns, TPlayerTowns);
OC_SAVE117_FRESH(SAVE117_PlayerGuards, TPlayerGuards);
OC_SAVE117_FRESH(SAVE117_Banker, AI_Banker);
OC_SAVE117_FRESH(SAVE117_ScriptMgr, TScriptMgr);
OC_SAVE117_FRESH(SAVE117_TownPlan, TTownPlan);
#if !defined(_M_IX86)  // a pool's layout is 1.17's shape's on 64 bits (h/aipools.h)
typedef TAiPool117<TSoldier, AI_RECORDS_117> TSoldierPool117;
typedef TAiPool117<TGuard, AI_RECORDS_117> TGuardPool117;
typedef TAiPool117<TWorker, AI_RECORDS_117> TWorkerPool117;
typedef TAiPool117<TBldg, AI_RECORDS_117> TBldgPool117;
typedef TAiPool117<TTown, AI_TOWNS_117> TTownPool117;
typedef TAiPool117<TScript, AI_TOWNS_117> TScriptPool117;
OC_SAVE117_FRESH(SAVE117_SoldierPool, TSoldierPool117);
OC_SAVE117_FRESH(SAVE117_GuardPool, TGuardPool117);
OC_SAVE117_FRESH(SAVE117_WorkerPool, TWorkerPool117);
OC_SAVE117_FRESH(SAVE117_BldgPool, TBldgPool117);
OC_SAVE117_FRESH(SAVE117_TownPool, TTownPool117);
OC_SAVE117_FRESH(SAVE117_ScriptPool, TScriptPool117);
#endif
#undef OC_SAVE117_FRESH
// An AI pool's nodes, which the AI domain walks one at a time with the layout of the pool's
// array (AiPool, below).
#define OC_SAVE117_FRESH_NODE(layout, T)                                                            static_assert(kSave117Layouts[kSave117Ops[kSave117Layouts[layout].firstOp].sub].size == sizeof(T),                 "gen/save117.h is older than " #T ": run tools/wide_globals.py, then tools/save_layouts.py")
#if !defined(_M_IX86)  // the layouts are the 64-bit build's; tools/wide_globals.py's x86 probe skips them
OC_SAVE117_FRESH_NODE(SAVE117_SoldierPool, TSoldier);
OC_SAVE117_FRESH_NODE(SAVE117_GuardPool, TGuard);
OC_SAVE117_FRESH_NODE(SAVE117_WorkerPool, TWorker);
OC_SAVE117_FRESH_NODE(SAVE117_BldgPool, TBldg);
OC_SAVE117_FRESH_NODE(SAVE117_TownPool, TTown);
OC_SAVE117_FRESH_NODE(SAVE117_ScriptPool, TScript);
#endif
#undef OC_SAVE117_FRESH_NODE

#if defined(__EMSCRIPTEN__)
#include <emscripten/heap.h>
extern "C" unsigned char __heap_base;  // the heap's start, after the module's own data (wasm-ld)
#endif

extern CImage sgCircleImages_g[80] OC_UNALIGNED;  // the selection circles' own images (CImage.cpp)
extern CImage sgHpbarImages[SELECTION_MAX];     // and the selection's health bars
extern CImage g_hxHpbarImages[SELECTION_WIDE];  // and both of Hyperion's selection (CImage.cpp)
extern CImage g_hxCircleImages[SELECTION_HUMANS * SELECTION_WIDE + 80];
extern CSprite sgSprites[POOL_SPRITES_STORE];
extern CUnit sgUnitsMem[POOL_UNITS_STORE];
extern CBullet sgBullets[POOL_BULLETS_STORE] OC_UNALIGNED;
extern COrder sgOrders[POOL_ORDERS_STORE] OC_UNALIGNED;
extern CThingy sgThingies1Arr_g[POOL_THINGIES_STORE];
extern CThingy sgThingies2Arr_g[POOL_THINGIES_STORE];
extern CThingy *gBlackX;  // the black X a right-click on the ground shows (CThingy.cpp)
extern CFlingy sgDummyFlingy_g;
extern WORD *gfpCellMap;

extern PlayerTable<unsigned char, PlayerTagVictoryCodes, 8> sgbVictoryCodes;

// The AI's state, as the save keeps it (sai_write_save_data).
extern PlayerTable<TPlayerTowns, PlayerTagTowns, 8> gPlayerTowns_g;
extern PlayerTable<TPlayerGuards, PlayerTagGuards, 8> gPlayerGuards_g;
extern TSoldierPool gSoldiers_g;
extern TGuardPool gGuards_g;
extern TWorkerPool gPeons_g;
extern TBldgPool gBldgs_g;
extern TTownPool gTowns_g;
extern TScriptPool gScripts_g;
extern TScriptMgr gScriptMgr_g;
extern DWORD dword_6A5D6C;

namespace {

// One step of a chain: the word in, multiplied and folded.
inline uint64_t Mix(uint64_t x, uint64_t v) {
  x ^= v;
  x *= 0xFF51AFD7ED558CCDull;
  return x ^ x >> 32;
}

// 64 bits at a time; a dump, when one is asked for, gets the bytes as they are hashed. The
// words go to four chains in turn, the k-th of a domain to chain k mod 4, and Digest mixes the
// four into the seed, so one thread runs four chains side by side where one chain made the
// map's words the check's longest wait. The turns follow the word's place in the domain, not how
// the calls split it, so Run, Word and a part's tape hash alike.
struct Hasher {
  uint64_t q;  // the seed
  FILE *pDump;
  // With a tape the words are kept, not mixed, to be mixed in order later (Ai): a part of a
  // domain gathered on a thread of its own hashes as it would have in its place.
  std::vector<uint64_t> *pTape;
  uint64_t nWords;  // the words mixed, which also says whose turn it is; for HYPERION_PERF's log
  uint64_t aqChain[4];
  void Word(uint64_t v) {
    if (pTape) {
      pTape->push_back(v);
      return;
    }
    uint64_t &x = aqChain[nWords & 3];
    x = Mix(x, v);
    ++nWords;
    if (pDump)
      fwrite(&v, sizeof v, 1, pDump);
  }
  // n words, the i-th word(i), mixed as n calls of Word would mix them, with the chains in
  // locals: through the struct, each word waited for the last one's store (a 1,024-tile
  // map's 6 million words were most of a check)
  template <class F>
  void Run(size_t n, F word) {
    if (pTape || pDump) {
      for (size_t i = 0; i < n; i++)
        Word(word(i));
      return;
    }
    size_t i = 0;
    for (; i < n && (nWords & 3) != 0; i++)
      Word(word(i));
    uint64_t a = aqChain[0], b = aqChain[1], c = aqChain[2], d = aqChain[3];
    const size_t nFours = (n - i) / 4;
    for (size_t k = 0; k < nFours; k++, i += 4) {
      a = Mix(a, word(i));
      b = Mix(b, word(i + 1));
      c = Mix(c, word(i + 2));
      d = Mix(d, word(i + 3));
    }
    aqChain[0] = a;
    aqChain[1] = b;
    aqChain[2] = c;
    aqChain[3] = d;
    nWords += nFours * 4;
    for (; i < n; i++)
      Word(word(i));
  }
  // The domain's hash: the seed, the four chains and the count of words.
  uint64_t Digest(void) const {
    uint64_t x = q;
    for (int k = 0; k < 4; k++)
      x = Mix(x, aqChain[k]);
    return Mix(x, nWords);
  }
  void Bytes(const void *p, size_t n) {
    const unsigned char *pb = static_cast<const unsigned char *>(p);
    Word(n);
    Run(n / 8, [pb](size_t i) {
      uint64_t v;
      memcpy(&v, pb + i * 8, 8);
      return v;
    });
    pb += n / 8 * 8;
    n %= 8;
    if (n) {
      uint64_t v = 0;
      memcpy(&v, pb, n);
      Word(v);
    }
  }
  void Pointer(const void *p);
};

// A record's fields, gathered in 1.17's form, a pointer as its key, and then hashed in one
// run, so that the hash follows the values and not how the layout splits the record into
// ops: a field that becomes a type of its own (a map position, stage E) hashes as it did
// inside the bytes around it.
struct Stream {
  // a buffer that only grows, appended to a few bytes at a time (a vector's insert was most
  // of the AI domain's time)
  struct Bytes {
    BYTE *p = NULL;
    size_t n = 0, cap = 0;
    void clear() { n = 0; }
    BYTE *data() { return p; }
    size_t size() const { return n; }
    ~Bytes() { free(p); }
  } b;
  void Put(const void *pv, size_t n) {
    if (b.n + n > b.cap) {
      size_t cap = b.cap ? b.cap * 2 : 1024;
      while (cap < b.n + n)
        cap *= 2;
      b.p = static_cast<BYTE *>(realloc(b.p, cap));
      b.cap = cap;
    }
    memcpy(b.p + b.n, pv, n);
    b.n += n;
  }
  void Word(uint64_t v) { Put(&v, sizeof v); }
  void Pointer(const void *p);
};

bool InPool(const void *p, const void *pPool, size_t nBytes) {
  return static_cast<const BYTE *>(p) >= static_cast<const BYTE *>(pPool) &&
         static_cast<const BYTE *>(p) < static_cast<const BYTE *>(pPool) + nBytes;
}

uint64_t SpriteKey(const CSprite *s) {
  return 0x5000000000000000ull ^ (s->wSpriteID | static_cast<uint64_t>(s->bCreator) << 16 |
                                  static_cast<uint64_t>(s->wPosX) << 24 |
                                  static_cast<uint64_t>(static_cast<WORD>(s->wPosY)) << 40);
}

// An image is its id and its sprite's key; its map position is the draw's
// (CImage::DrawUpdateRect), set only for what the view has drawn.
uint64_t ImageKey(const CImage *i) {
  const CSprite *s = i->pParentSprite;
  const uint64_t qSprite = s != NULL && InPool(s, sgSprites, sizeof sgSprites) ? SpriteKey(s) : 0;
  return 0x6000000000000000ull ^ i->wImageID ^ qSprite * 0x9E3779B97F4A7C15ull;
}

// A pointer into a table as the table's tag, the record's index and the offset in it.
uint64_t TableKey(uint64_t qTag, const void *p, const void *pTable, size_t nRecord) {
  const size_t n = static_cast<size_t>(static_cast<const BYTE *>(p) - static_cast<const BYTE *>(pTable));
  return qTag ^ static_cast<uint64_t>(n / nRecord) << 16 ^ n % nRecord;
}

// What a check asks of every pointer, taken once as it starts (SyncScope), while the game
// stands still: the areas and slots the captains' tables are walked by, and the span holding
// everything PointerKey names (the pools, the captains' tables, PortOriginalAddress's spans
// and in a browser the heap). A value outside the span is no pointer it names, which on x64,
// whose memory lies above 4 GB, settles every 32-bit value at once (IsInteger).
struct Scope {
  int nAreas, nSlots;
  UINT_PTR lo, hi;
  // the captains' tables, a slot's each: in the slots' order, and by their first address for
  // the search when none overlaps another (a slot's own heap block each)
  struct Captains {
    UINT_PTR lo, hi;
    int nSlot;
  };
  std::vector<Captains> bySlot, byAddress;
  UINT_PTR capLo, capHi;
  bool bOverlap;
} g_scope;

void SyncScope(void) {
  g_scope.nAreas = NumAreas();
  g_scope.nSlots = PlayerLobbySlots();
  UINT_PTR lo = ~static_cast<UINT_PTR>(0), hi = 0;
  auto span = [&lo, &hi](const void *p, size_t n) {
    const UINT_PTR a = reinterpret_cast<UINT_PTR>(p);
    lo = a < lo ? a : lo;
    hi = a + n > hi ? a + n : hi;
  };
  span(sgSprites, sizeof sgSprites);
  span(sgImages, sizeof sgImages);
  span(sgUnitsMem, g_pools.units * sizeof(CUnit));
  g_scope.bySlot.clear();
  for (int s = 0; s < g_scope.nSlots; s++)
    if (const TAI_Captain *pTable = sgCaptain[s]) {
      const UINT_PTR a = reinterpret_cast<UINT_PTR>(pTable);
      g_scope.bySlot.push_back({a, a + g_scope.nAreas * sizeof(TAI_Captain), s});
      span(pTable, g_scope.nAreas * sizeof(TAI_Captain));
    }
  g_scope.byAddress = g_scope.bySlot;
  std::sort(g_scope.byAddress.begin(), g_scope.byAddress.end(),
            [](const Scope::Captains &x, const Scope::Captains &y) { return x.lo < y.lo; });
  g_scope.bOverlap = false;
  g_scope.capLo = ~static_cast<UINT_PTR>(0);
  g_scope.capHi = 0;
  for (size_t i = 0; i < g_scope.byAddress.size(); i++) {
    const Scope::Captains &c = g_scope.byAddress[i];
    g_scope.bOverlap = g_scope.bOverlap || (i > 0 && c.lo < g_scope.byAddress[i - 1].hi);
    g_scope.capLo = c.lo < g_scope.capLo ? c.lo : g_scope.capLo;
    g_scope.capHi = c.hi > g_scope.capHi ? c.hi : g_scope.capHi;
  }
  UINT_PTR aSpans[2][2];
  PortOriginalSpans(aSpans);
  for (const UINT_PTR *s : aSpans)
    span(reinterpret_cast<const void *>(s[0]), s[1] - s[0]);
#if !defined(_M_IX86)
  const void *pPoolsLo, *pPoolsHi;
  AiPoolSpan(&pPoolsLo, &pPoolsHi);
  span(pPoolsLo, static_cast<const BYTE *>(pPoolsHi) - static_cast<const BYTE *>(pPoolsLo));
#endif
#if defined(__EMSCRIPTEN__)
  span(&__heap_base, emscripten_get_heap_size() - reinterpret_cast<UINT_PTR>(&__heap_base));
#endif
  g_scope.lo = lo;
  g_scope.hi = hi;
}

// The slot whose captains' table holds p, the first in the slots' order, or -1.
int CaptainSlot(const void *p) {
  const UINT_PTR v = reinterpret_cast<UINT_PTR>(p);
  if (v < g_scope.capLo || v >= g_scope.capHi)
    return -1;
  if (g_scope.bOverlap) {
    for (const Scope::Captains &c : g_scope.bySlot)
      if (v >= c.lo && v < c.hi)
        return c.nSlot;
    return -1;
  }
  const auto it = std::upper_bound(g_scope.byAddress.begin(), g_scope.byAddress.end(), v,
                                   [](UINT_PTR x, const Scope::Captains &c) { return x < c.lo; });
  if (it == g_scope.byAddress.begin())
    return -1;
  const Scope::Captains &c = *(it - 1);
  return v < c.hi ? c.nSlot : -1;
}

uint64_t PointerKey(const void *p) {
  if (p == NULL)
    return 0;
  if (InPool(p, sgSprites, sizeof sgSprites))
    return SpriteKey(static_cast<const CSprite *>(p));
  if (InPool(p, sgImages, sizeof sgImages))
    return ImageKey(static_cast<const CImage *>(p));
  if (InPool(p, sgUnitsMem, g_pools.units * sizeof(CUnit)))
    return TableKey(0x7000000000000000ull, p, sgUnitsMem, sizeof(CUnit));
  // a captain: the nations' tables are the heap's (captains_alloc)
  const int s = CaptainSlot(p);
  if (s >= 0)
    return TableKey(0x7100000000000000ull ^ static_cast<uint64_t>(s) << 40, p, sgCaptain[s], sizeof(TAI_Captain));
#if !defined(_M_IX86)
  // the AI's pools, Hyperion's own storage on 64 bits, by the 1.17 address
  unsigned long long qPool;
  if (AiPoolKey(p, &qPool))
    return qPool;
#endif
  const DWORD va = PortOriginalAddress(reinterpret_cast<UINT_PTR>(p));
  return va ? va : 1;
}

// Whether a value below 4 GB is a pointer to something PointerKey names: never on x64,
// where the program's memory lies above, and the rule for WebAssembly, whose memory is all
// below. 1.17's integers of these fields are far below the first pool.
bool LowPointer(UINT_PTR v) {
  if (v < 0x10000 || v < g_scope.lo || v >= g_scope.hi)
    return false;
  const void *p = reinterpret_cast<const void *>(v);
  if (InPool(p, sgSprites, sizeof sgSprites) || InPool(p, sgImages, sizeof sgImages) ||
      InPool(p, sgUnitsMem, g_pools.units * sizeof(CUnit)))
    return true;
  if (CaptainSlot(p) >= 0)
    return true;
#if !defined(_M_IX86)
  unsigned long long qPool;
  if (AiPoolKey(p, &qPool))
    return true;
#endif
#if defined(__EMSCRIPTEN__)
  // and the heap, whose blocks x64 has above 4 GB too
  if (v >= reinterpret_cast<UINT_PTR>(&__heap_base) && v < emscripten_get_heap_size())
    return true;
#endif
  return PortOriginalAddress(v) != 0;
}

// A field that holds a pointer or, in its union's other arm, an integer: a value of 32 bits
// is the integer, unless it points where a pointer would
bool IsInteger(UINT_PTR v) {
  return (v >> 32 == 0 || v >> 31 == 0x1FFFFFFFFull) && !LowPointer(v);
}

void Hasher::Pointer(const void *p) { Word(PointerKey(p)); }
void Stream::Pointer(const void *p) { Word(PointerKey(p)); }

// A 4-byte copy in a slot of 8 on x64 is most often a 4-byte field and 4 bytes of padding
// holding the high half of an old pointer. Only a few are unions whose other arm is a pointer,
// each a unit: the move-target unit at +0x14 (the save makes it a unit id first) and CUnit's
// +0xC4, a saved position or a hangar's links. Such a slot is a pointer when its 8 bytes are a
// unit of the pool, else its 4 bytes; a looser test, any pointer into a pool or 1.17's data,
// hashed differently by where the program loaded.
struct UnionSlot {
  unsigned int layout, off86;
};
const UnionSlot kUnionSlots[] = {
    {SAVE117_CFlingy, 0x14}, {SAVE117_CUnit, 0x14}, {SAVE117_CBullet, 0x14}, {SAVE117_CUnit, 0xC4}};

bool PointerSlot(unsigned int layout, const Save117Layout &l, unsigned int i, const BYTE *pRec) {
  const Save117Op &op = kSave117Ops[l.firstOp + i];
  const unsigned int nEnd = i + 1 < l.opCount ? kSave117Ops[l.firstOp + i + 1].off : l.size;
  if (op.n != 4 || op.off % 8 != 0 || nEnd - op.off < 8)
    return false;
  bool bUnion = false;
  for (const UnionSlot &u : kUnionSlots)
    bUnion |= u.layout == layout && u.off86 == op.off86;
  if (!bUnion)
    return false;
  const CUnit *p;
  memcpy(&p, pRec + op.off, sizeof p);
  return p >= sgUnitsMem && p < sgUnitsMem + g_pools.units &&
         (reinterpret_cast<const BYTE *>(p) - reinterpret_cast<const BYTE *>(sgUnitsMem)) % sizeof(CUnit) == 0;
}

void RecordFields(Stream &h, unsigned int layout, const BYTE *pRec) {
  const Save117Layout &l = kSave117Layouts[layout];
  for (unsigned int i = 0; i < l.opCount; i++) {
    const Save117Op &op = kSave117Ops[l.firstOp + i];
    if (op.kind == SAVE117_COPY && PointerSlot(layout, l, i, pRec)) {
      void *p;
      memcpy(&p, pRec + op.off, sizeof p);
      h.Pointer(p);
    } else if (op.kind == SAVE117_COPY) {
      h.Put(pRec + op.off, op.n);
    } else if (op.kind == SAVE117_ARRAY) {
      const Save117Layout &e = kSave117Layouts[op.sub];
      for (unsigned int k = 0; k < op.n; k++)
        RecordFields(h, op.sub, pRec + op.off + k * e.size);
    } else if (op.kind == SAVE117_PTRBYTES && !PortSaveUsesPointer(layout, op.off86, pRec)) {
      // bytes in a pointer's union (CUnit's +0xC8, a building's tech and upgrade): 1.17's four,
      // as a value of 32 bits has hashed, and in a Hyperion game their ids' high bytes
      BYTE ab[4];
      PortSave117Put(op.sub, pRec + op.off, ab);
      DWORD d;
      memcpy(&d, ab, 4);
      h.Word(d);
      BYTE abHigh[4];
      const unsigned int nHigh = PortSave117HighCount(op.sub);
      if (g_posShift != 16 && nHigh <= sizeof abHigh) {
        PortSave117HighGet(op.sub, pRec + op.off, 1, abHigh);
        h.Put(abHigh, nHigh);
      }
    } else if (op.kind == SAVE117_PTRPOINT && PortSaveUsesPoint(layout, op.off86, pRec)) {
      // a point in a pointer's union (CUnit's rally point, a power-up's pick-up position)
      h.Put(pRec + op.off, g_posShift == 16 ? 2 : 4);
      h.Put(pRec + op.off + 4, g_posShift == 16 ? 2 : 4);
    } else if (op.kind == SAVE117_POINTER && layout == SAVE117_CImage && (op.off86 == 0x34 || op.off86 == 0x38)) {
      // an image's draw and update functions, which every build names apart: whether it
      // has one, as a pointer outside the data has hashed
      void *p;
      memcpy(&p, pRec + op.off, sizeof p);
      h.Word(p != NULL);
    } else if (op.kind == SAVE117_POINTER || op.kind == SAVE117_PTRPOINT || op.kind == SAVE117_PTRBYTES) {
      // A pointer, or what its union holds instead: a value of 32 bits is that (a mineral
      // field's resources beside a pointer), any other a pointer (IsInteger).
      UINT_PTR v;
      memcpy(&v, pRec + op.off, sizeof v);
      if (IsInteger(v))
        h.Word(static_cast<DWORD>(v));
      else
        h.Pointer(reinterpret_cast<const void *>(v));
    } else if (op.kind == SAVE117_UNSIGNED16) {
      // A unit id the x64 build keeps in 32 bits (the cargo list): all of it.
      DWORD d;
      memcpy(&d, pRec + op.off, 4);
      h.Word(d);
    } else if (op.kind == SAVE117_OFFSET16) {
      // An image's offset into its scripts, which x64 keeps in 32 bits: its
      // 16 bits in a 1.17 game, as 1.17's record has it, and all of it in a Hyperion game.
      h.Put(pRec + op.off, g_posShift == 16 ? 2 : 4);
    } else if (op.kind == SAVE117_UNSIGNED8) {
      // An id of h/typelimits.h's the x64 build keeps in 16 bits: its byte
      // in a 1.17 game, as 1.17's record has it, and all of it in a Hyperion game.
      h.Put(pRec + op.off, g_posShift == 16 ? 1 : 2);
    } else if (op.kind == SAVE117_POS16S || op.kind == SAVE117_POS16U || op.kind == SAVE117_SIGNED16) {
      // A map position (h/pos.h, stage E), or a count that is a short in 1.17: its 16 bits
      // in a 1.17 game, as 1.17's record has it, and all of it in a Hyperion game.
      h.Put(pRec + op.off, g_posShift == 16 ? 2 : 4);
    } else {
      // An integer of pointer width (a union such as CUnit's dwUnionEC): 1.17's fit in 32 bits,
      // sign-extended or not, so a value past that is a pointer, hashed as one (IsInteger)
      UINT_PTR v;
      memcpy(&v, pRec + op.off, sizeof v);
      if (IsInteger(v))
        h.Word(static_cast<DWORD>(v));
      else
        h.Pointer(reinterpret_cast<const void *>(v));
    }
  }
}

void Record(Hasher &h, unsigned int layout, const BYTE *pRec) {
  thread_local Stream s;  // a worker thread's own (port_workers.h)
  s.b.clear();
  RecordFields(s, layout, pRec);
  h.Bytes(s.b.data(), s.b.size());
}

// A captain's record starts with its region, a WORD, and its stream with that WORD's bytes
// when the layout's first field is copied as it is (CaptainsAlike). Most of a 64-player
// game's 160,000 captains are of areas their slot has nothing in, alike but for the region,
// so a run of them is hashed from the first one's stream with each one's region put in its
// place: the same words as each walked field by field.
bool CaptainsAlike(void) {
  static const int s_nAlike = [] {
    const Save117Layout &l = kSave117Layouts[SAVE117_Captain];
    if (l.opCount == 0 || l.size < 2)
      return 0;
    const Save117Op &op = kSave117Ops[l.firstOp];
    const bool bBytes = op.kind == SAVE117_COPY || op.kind == SAVE117_SIGNED16 || op.kind == SAVE117_POS16S ||
                        op.kind == SAVE117_POS16U;
    return op.off == 0 && op.n >= 2 && bBytes ? 1 : 0;
  }();
  return s_nAlike == 1 && getenv("HYPERION_SYNC_CAPTAINS_WALK") == NULL;
}

void Captains(Hasher &h, const TAI_Captain *pCaptains, int nFrom, int nTo) {
  thread_local Stream s;
  if (!CaptainsAlike()) {
    for (int a = nFrom; a < nTo; a++)
      Record(h, SAVE117_Captain, reinterpret_cast<const BYTE *>(&pCaptains[a]));
    return;
  }
  const size_t nSize = kSave117Layouts[SAVE117_Captain].size;
  const BYTE *pLast = NULL;
  for (int a = nFrom; a < nTo; a++) {
    const BYTE *pRec = reinterpret_cast<const BYTE *>(&pCaptains[a]);
    if (pLast != NULL && memcmp(pRec + 2, pLast + 2, nSize - 2) == 0) {
      memcpy(s.b.data(), pRec, 2);
    } else {
      s.b.clear();
      RecordFields(s, SAVE117_Captain, pRec);
      pLast = pRec;
    }
    h.Bytes(s.b.data(), s.b.size());
  }
}

// HYPERION_SYNC_SERIAL=1: a check's domains and their parts one after another on the game's
// thread, so that HYPERION_PERF and HYPERION_PROFILE show each one's own cost
bool SyncSerial(void) {
  static const bool s_bSerial = getenv("HYPERION_SYNC_SERIAL") != NULL;
  return s_bSerial;
}

// A domain's work in parts, each gathered onto a tape of its own on the worker pool's threads
// (port_workers.h), and the tapes mixed in the parts' order: the same words in the same order
// as one part after another, so the values are those of the domain hashed whole. Serially, for
// a dump, or with one thread: straight into h. The tapes are a domain's own, kept from check
// to check, so that their megabytes are not allocated and faulted in again each time; every
// check is the game thread's, one at a time (the replay's, the harness's, the LAN's).
typedef std::vector<std::vector<uint64_t>> Tapes;

template <class F>
void Parts(Hasher &h, Tapes &tapes, int nParts, F part) {
  if (nParts <= 1 || SyncSerial() || h.pDump || h.pTape || PortWorkersCount() <= 1) {
    for (int k = 0; k < nParts; k++)
      part(h, k);
    return;
  }
  if (static_cast<int>(tapes.size()) < nParts)
    tapes.resize(nParts);
  // each tape mixed as soon as it and the ones before it are gathered, while the rest gather
  PortWorkersForInOrder(
      nParts,
      [&](int k) {
        tapes[k].clear();
        Hasher t = {0, NULL, &tapes[k]};
        part(t, k);
      },
      [&](int k) {
        const uint64_t *pWords = tapes[k].data();
        h.Run(tapes[k].size(), [pWords](size_t i) { return pWords[i]; });
      });
}

// item(h, i) for i from 0 to n - 1, in parts of nAPart.
template <class F>
void Each(Hasher &h, Tapes &tapes, int n, int nAPart, F item) {
  Parts(h, tapes, (n + nAPart - 1) / nAPart, [&](Hasher &t, int k) {
    const int nEnd = n < (k + 1) * nAPart ? n : (k + 1) * nAPart;
    for (int i = k * nAPart; i < nEnd; i++)
      item(t, i);
  });
}

const int kRecordsAPart = 512;

template <class T>
void Pool(Hasher &h, Tapes &tapes, unsigned int layout, const T *pPool, int nCount, bool (*pfnUsed)(const T &)) {
  Each(h, tapes, nCount, kRecordsAPart, [&](Hasher &t, int i) {
    if (pfnUsed(pPool[i])) {
      t.Word(i);
      Record(t, layout, reinterpret_cast<const BYTE *>(&pPool[i]));
    }
  });
}

// A player table: 1.17's entries, then in a game with slots past 11 the others, of the
// game's slots and laid out as a build of that many slots lays them, so that a game hashes
// the same in a build of more slots (stage D).
thread_local std::vector<BYTE> g_gather;

template <class T, class Tag, int N>
void Table(Hasher &h, const PlayerTable<T, Tag, N> &t) {
  h.Bytes(t.a, sizeof t.a);
  if (g_hxPlayers)
    h.Bytes(t.Extra(), sizeof(T) * (PlayerSlots() > N ? PlayerSlots() - N : 0));
}

// Rows of extra entries: the first n of each row, nStride a row in the build.
template <class T>
void Rows(Hasher &h, const T *pExtra, int nRows, int nStride, int n) {
  g_gather.resize(sizeof(T) * nRows * n + 1);
  for (int r = 0; r < nRows; r++)
    memcpy(&g_gather[sizeof(T) * r * n], pExtra + r * nStride, sizeof(T) * n);
  h.Bytes(&g_gather[0], sizeof(T) * nRows * n);
}

// The rows a mod's unit types have in a table of a row a type, in a game with them:
// the game's slots of each, in each of nPlanes planes, which follow one another at the
// table's whole count (the planes after the first were read from the first's).
template <class T, int R>
void TypeRows(Hasher &h, const T *pRows, int nPlanes) {
  const int n = PlayerTypeRows<R>::kCount != 0 ? UnitTypesMod() : 0;
  for (int i = 0; i < nPlanes && n != 0; i++)
    Rows(h, pRows + static_cast<size_t>(i) * PlayerTypeRows<R>::kCount * PLAYER_SLOTS_MAX, n, PLAYER_SLOTS_MAX,
         PlayerSlots());
}

template <class T, class Tag, int R>
void Table(Hasher &h, const PlayerGrid<T, Tag, R> &t) {
  h.Bytes(t.a, sizeof t.a);
  if (g_hxPlayers)
    Rows(h, t.Extra(), R, PLAYER_SLOTS_MAX - PLAYER_SLOTS_117, PlayerSlots() - PLAYER_SLOTS_117);
  TypeRows<T, R>(h, t.TypeRows(), 1);
}

template <class T, class Tag, int O, int R>
void Table(Hasher &h, const PlayerGrid3<T, Tag, O, R> &t) {
  h.Bytes(t.a, sizeof t.a);
  if (g_hxPlayers)
    Rows(h, t.Extra(), O * R, PLAYER_SLOTS_MAX - PLAYER_SLOTS_117, PlayerSlots() - PLAYER_SLOTS_117);
  TypeRows<T, R>(h, t.TypeRows(), O);
}

template <class T, class Tag>
void Table(Hasher &h, const PlayerMatrix<T, Tag> &t) {
  h.Bytes(t.a, sizeof t.a);
  if (g_hxPlayers)
    Rows(h, t.Extra(), PlayerSlots(), PLAYER_SLOTS_MAX, PlayerSlots());
}

// Sets of players in the words the game's slots take, as a build of that many slots holds
// them.
int SetWords() {
  return (PlayerSlots() + 63) / 64;
}

void Sets(Hasher &h, const PlayerSet *p, size_t n) {
  const int nWords = SetWords();
  if (nWords == PLAYER_SET_WORDS) {
    h.Bytes(p, n * sizeof(PlayerSet));
    return;
  }
  // the game's words of each set, as Bytes would take them gathered one set after another,
  // without the gathering: a 1,024-tile map's two sets a tile are 16 MB
  h.Word(n * nWords * 8);
  h.Run(n * nWords, [p, nWords](size_t i) { return static_cast<uint64_t>(p[i / nWords].w[i % nWords]); });
}

int UnitIndex(const CUnit *p) {
  return p ? static_cast<int>(p - sgUnitsMem) : -1;
}

// The units the save keeps (sub_431280): the four lists and the subunits that go with them.
// With a hasher, the order of each list too.
void UnitsUsed(BYTE *abUsed, Hasher *pHash = NULL) {
  memset(abUsed, 0, g_pools.units);
  CUnit *apLists[4] = {CUnit::GetUsedList(), CUnit::GetDeadList(), CUnit::GetTransList(),
                       CUnit::GetScannerList()};
  for (CUnit *pList : apLists) {
    if (pHash)
      pHash->Word(0xFFFFFFFFu);
    for (CUnit *p = pList; p; p = p->pNext) {
      const int n = UnitIndex(p);
      if (pHash)
        pHash->Word(static_cast<DWORD>(n));
      if (n < 0 || n >= g_pools.units)
        break;
      abUsed[n] = 1;
      if (p->subunit) {
        const int s = UnitIndex(p->subunit);
        if (s >= 0 && s < g_pools.units)
          abUsed[s] = 1;
      }
    }
  }
}

// A unit less what only a live game's local player sets (gamesnd_under_attack, never in a
// replay): the under-attack notice's timer and the minimap blink and its colour.
void Unit(Hasher &h, const CUnit *pUnit) {
  alignas(CUnit) BYTE ab[sizeof(CUnit)];
  memcpy(ab, pUnit, sizeof ab);
  CUnit *c = reinterpret_cast<CUnit *>(ab);
  c->_unmapped_86[1] = 0;
  c->_unmapped_86[4] = 0;
  c->_unmapped_86[5] = 0;
  Record(h, SAVE117_CUnit, ab);
}

void Units(Hasher &h) {
  std::vector<BYTE> abUsed(g_pools.units);
  UnitsUsed(abUsed.data(), &h);
  static Tapes s_tapes;
  Each(h, s_tapes, g_pools.units, kRecordsAPart, [&abUsed](Hasher &t, int i) {
    if (abUsed[i]) {
      t.Word(i);
      Unit(t, &sgUnitsMem[i]);
    }
  });
  for (int s = 0; s < PlayerSlots(); s++)
    h.Word(static_cast<DWORD>(UnitIndex(sgpPlayerLists_g[s])));
  // the rich resources and loads of a game with rich bases
  if (const unsigned char *pRich = RichBasesTable())
    h.Bytes(pRich, static_cast<size_t>(g_pools.units));
}

void Objects(Hasher &h) {
  static Tapes s_tapes;
  Pool<CBullet>(h, s_tapes, SAVE117_CBullet, sgBullets, g_pools.bullets, [](const CBullet &b) { return b.pSprite != NULL; });
  Pool<COrder>(h, s_tapes, SAVE117_COrder, sgOrders, g_pools.orders, [](const COrder &) { return true; });
  Pool<CThingy>(h, s_tapes, SAVE117_CThingy, sgThingies1Arr_g, g_pools.thingies,
                [](const CThingy &t) { return t.pSprite != NULL && &t != gBlackX; });
  Pool<CFlingy>(h, s_tapes, SAVE117_CFlingy, &sgDummyFlingy_g, 1, [](const CFlingy &f) { return f.pSprite != NULL; });
}

void Players(Hasher &h) {
  Table(h, gPlayerMinerals);
  Table(h, gPlayerGas);
  Table(h, gPlayerGasGathered_g);
  Table(h, gPlayerMineralsGathered_g);
  Table(h, glGameCounts_g);
  Table(h, glCounts_g);
  Table(h, sglCachedGameCounts_g);
  Table(h, sglCachedUnitCounts_g);
  Table(h, sglCachedCompletedCounts_g);
  Table(h, sgbAlliances_g);
  Table(h, SharedVision);
  Table(h, PlayerUnitsAvailable);
  Table(h, gTechInProgress);
  Table(h, gUpgradeInProgress);
  Table(h, gbVictoryState);
  Table(h, sgbVictoryCodes);
  Table(h, gStartLocations);
  // Tech and upgrades through the game's accessors, which reach the slots past 11.
  for (int s = 0; s < PlayerSlots(); s++) {
    for (WORD t = 0; t < PLAYER_TECHS; t++)
      h.Word(get_tech_available(s, t) | get_tech_researched(s, t) << 8);
    for (WORD u = 0; u < PLAYER_UPGRADES; u++)
      h.Word(get_upgrade_level(s, u) | get_max_upgrade_level(s, u) << 8);
    // and a mod's past 1.17's rows, with what is in progress
    for (WORD t = TECH_TYPE_MOD_FIRST; t < g_hxTypes.techs; t++)
      h.Word(get_tech_available(s, t) | get_tech_researched(s, t) << 8 | (is_tech_in_progress(t, s) != 0) << 16);
    for (WORD u = UPGRADE_TYPE_MOD_FIRST; u < g_hxTypes.upgrades; u++)
      h.Word(get_upgrade_level(s, u) | get_max_upgrade_level(s, u) << 8 | (is_upgrade_in_progress(u, s) != 0) << 16);
  }
}

void Map(Hasher &h) {
  const size_t nTiles = static_cast<size_t>(gwMapWidth) * gwMapHeight;
  const DWORD *pSquares = gpSquareMap;
  h.Run(nTiles, [pSquares](size_t t) { return static_cast<uint64_t>(pSquares[t] & ~0x40000000u); });
  h.Bytes(gfpMIMap, nTiles * 2);
  int nBytes = 0;
  const WORD *pCreep = getCreepMap(&nBytes);
  h.Bytes(pCreep, nBytes);
  if (g_hxPlayers) {
    if (const PlayerSet *p = VisionHxHidden())
      Sets(h, p, nTiles);
    if (const PlayerSet *p = VisionHxUnexplored())
      Sets(h, p, nTiles);
    // up to 32 slots, the parasites and blinders in 32 bits each, as before stage D; past that
    // their sets, with the detection's and each player's shared vision's
    if (PlayerSlots() <= 32) {
      for (int i = 0; i < g_pools.units; i++)
        h.Word(static_cast<uint64_t>(VisionHxParasite(&sgUnitsMem[i]).Low32()) |
               static_cast<uint64_t>(UnitMyopia(&sgUnitsMem[i]).Low32()) << 32);
    } else {
      for (int i = 0; i < g_pools.units; i++) {
        const CUnit *pUnit = &sgUnitsMem[i];
        const PlayerSet aq[3] = {VisionHxParasite(pUnit), UnitMyopia(pUnit), UnitDetection(pUnit)};
        Sets(h, aq, 3);
      }
      for (int s = 0; s < PlayerSlots(); s++) {
        const PlayerSet q = PlayerVision(s);
        Sets(h, &q, 1);
      }
    }
  }
}

// An image less what the view sets: its place on the map and the screen (both set as it is
// drawn), its redraw flag, its colouring.
// A selection circle's image or a health bar's, each from a store of its own and linked into
// the list of the sprite it marks: the view's, as the selection is the local player's.
bool Circle(const CImage *p) {
  return p != NULL && (InPool(p, sgCircleImages_g, sizeof sgCircleImages_g) ||
                       InPool(p, sgHpbarImages, sizeof sgHpbarImages) ||
                       InPool(p, g_hxCircleImages, sizeof g_hxCircleImages) ||
                       InPool(p, g_hxHpbarImages, sizeof g_hxHpbarImages));
}

void Image(Hasher &h, const CImage *pImage) {
  CImage c;
  memcpy(&c, pImage, sizeof c);
  // its neighbours as if no selection circle were linked between them
  while (Circle(c.pNext))
    c.pNext = c.pNext->pNext;
  while (Circle(c.pPrev))
    c.pPrev = c.pPrev->pPrev;
  c.wScreenX = c.wScreenY = c.wGrpX = c.wGrpY = 0;
  c.wMapX = c.wMapY = 0;
  c.wDrawWidth = c.wDrawHeight = 0;
  WORD wFlags;
  memcpy(&wFlags, &c.flags, sizeof wFlags);
  wFlags &= ~1;  // redraw
  memcpy(&c.flags, &wFlags, sizeof wFlags);
  memset(&c.coloringData, 0, sizeof c.coloringData);
  Record(h, SAVE117_CImage, reinterpret_cast<const BYTE *>(&c));
}

// A sprite less its slot, list links, selection and flashing, then its images in order.
void Sprite(Hasher &h, const CSprite *pSprite) {
  if (pSprite == NULL || !InPool(pSprite, sgSprites, sizeof sgSprites)) {
    h.Word(0);
    return;
  }
  h.Word(SpriteKey(pSprite));
  BYTE abFlags;
  memcpy(&abFlags, &pSprite->flags, 1);
  h.Word(pSprite->bVisibilityFlags | pSprite->bElevationLevel << 8 | (abFlags & ~0x09u) << 16 |
         static_cast<uint64_t>(pSprite->bGrpWidth) << 24 | static_cast<uint64_t>(pSprite->bGrpHeight) << 32);
  h.Pointer(pSprite->pMainImage);
  if (g_hxPlayers) {
    const PlayerSet q = VisionHxSprite(pSprite);
    for (int w = 0; w < SetWords(); w++)
      h.Word(q.w[w]);
  }
  int n = 0;
  for (const CImage *p = pSprite->pImageHead;
       p != NULL && (InPool(p, sgImages, sizeof sgImages) || Circle(p)) && n < g_pools.images; p = p->pNext, n++) {
    if (Circle(p) || (p->wImageID >= 561 && p->wImageID <= 580))
      continue;  // selection circles (images.dat 561 to 580) and health bars, which the view draws
    Image(h, p);
  }
}

// The sprites of everything that has one, in the order of their owners.
void Sprites(Hasher &h) {
  std::vector<BYTE> abUsed(g_pools.units);
  UnitsUsed(abUsed.data());
  static Tapes s_tapes;
  Each(h, s_tapes, g_pools.units, kRecordsAPart, [&abUsed](Hasher &t, int i) {
    if (abUsed[i])
      Sprite(t, sgUnitsMem[i].pSprite);
  });
  Each(h, s_tapes, g_pools.bullets, kRecordsAPart, [](Hasher &t, int i) {
    if (sgBullets[i].pSprite)
      Sprite(t, sgBullets[i].pSprite);
  });
  Each(h, s_tapes, g_pools.thingies, kRecordsAPart, [](Hasher &t, int i) {
    if (sgThingies1Arr_g[i].pSprite && &sgThingies1Arr_g[i] != gBlackX)
      Sprite(t, sgThingies1Arr_g[i].pSprite);
  });
}

// A pool of the AI's: the nodes not on its free list, in order, each with the layout of
// the pool's array. The save keeps the free nodes too, with their pointers turned into ids
// and back, which a loaded game need not give back as they were; nothing reads them.
template <class T>
void AiPool(Hasher &h, Tapes &tapes, unsigned int layout, const T *pNodes, int nCount, const T *pFree) {
  const unsigned int nodeLayout = kSave117Ops[kSave117Layouts[layout].firstOp].sub;
  std::vector<BYTE> free(nCount);
  for (const T *p = pFree; p != NULL && p >= pNodes && p < pNodes + nCount && !free[p - pNodes]; p = p->pNext)
    free[p - pNodes] = 1;
  Each(h, tapes, nCount, kRecordsAPart, [&](Hasher &t, int i) {
    if (!free[i]) {
      t.Word(i);
      Record(t, nodeLayout, reinterpret_cast<const BYTE *>(&pNodes[i]));
    }
  });
}

// A banker less the demands past its count: the heap's slots that a pop or a new turn
// (banker_start_plan) left behind, which nothing reads. Some point past the end of a nation's
// captains, which a loaded game allocates for its regions where a new game uses 2,500, so
// what such a pointer lands in depends on the heap.
void Banker(Hasher &h, const AI_Banker &bank) {
  AI_Banker b;
  memcpy(&b, &bank, sizeof b);
  const int nDemands = sizeof b.aDemands / sizeof b.aDemands[0];
  for (int i = b.bDemandCount; i < nDemands; i++)
    memset(&b.aDemands[i], 0, sizeof b.aDemands[i]);
  Record(h, SAVE117_Banker, reinterpret_cast<const BYTE *>(&b));
}

// What the save's AI sections hold (sai_write_save_data), every nation of the game.
void Ai(Hasher &h) {
  static Tapes s_tapes;
  const int nRecords = g_aiPools.records, nTowns = g_aiPools.towns;  // 1000 and 100 in 1.17
  AiPool(h, s_tapes, SAVE117_SoldierPool, gSoldiers_g.aSoldiers, nRecords, gSoldiers_g.pFreeHead);
  AiPool(h, s_tapes, SAVE117_GuardPool, gGuards_g.aNodes, nRecords, gGuards_g.pFree);
  AiPool(h, s_tapes, SAVE117_WorkerPool, gPeons_g.aWorkers, nRecords, gPeons_g.pFreeHead);
  AiPool(h, s_tapes, SAVE117_BldgPool, gBldgs_g.aBldgs, nRecords, gBldgs_g.pFreeHead);
  AiPool(h, s_tapes, SAVE117_TownPool, gTowns_g.aTowns, nTowns, gTowns_g.pFreeHead);
  AiPool(h, s_tapes, SAVE117_ScriptPool, gScripts_g.items, nTowns, gScripts_g.pFree);
  Record(h, SAVE117_ScriptMgr, reinterpret_cast<const BYTE *>(&gScriptMgr_g));
  const int nAreas = g_scope.nAreas;  // as the scope was taken (SyncScope)
  h.Word(nAreas);
  // the slots' records in pieces, the captains most of all (a 64-player game's 2,500 areas are
  // 160,000): a slot's towns, guards, banker and first captains, then its captains a run each
  struct Piece {
    int nSlot, nFrom, nTo;
  };
  const int kCaptainsAPiece = 256;
  static std::vector<Piece> s_pieces;
  s_pieces.clear();
  for (int s = 0; s < g_scope.nSlots; s++) {
    // the slot's table once: through the player table's accessor, twice an area was most of
    // the domain
    const int nCaptains = sgCaptain[s] != NULL ? nAreas : 0;
    int a = 0;
    do {
      const int nTo = nCaptains < a + kCaptainsAPiece ? nCaptains : a + kCaptainsAPiece;
      s_pieces.push_back({s, a, nTo});
      a = nTo;
    } while (a < nCaptains);
  }
  Parts(h, s_tapes, static_cast<int>(s_pieces.size()), [](Hasher &t, int k) {
    const Piece &q = s_pieces[k];
    if (q.nFrom == 0) {
      Record(t, SAVE117_PlayerTowns, reinterpret_cast<const BYTE *>(&gPlayerTowns_g[q.nSlot]));
      Record(t, SAVE117_PlayerGuards, reinterpret_cast<const BYTE *>(&gPlayerGuards_g[q.nSlot]));
      Banker(t, gAIBanker[q.nSlot]);
    }
    Captains(t, sgCaptain[q.nSlot], q.nFrom, q.nTo);
  });
  for (int i = 0; i < NUM_TOWN_PLANS; i++)
    Record(h, SAVE117_TownPlan, reinterpret_cast<const BYTE *>(&town_plan[i]));
  h.Word(dword_6A5D68 | static_cast<uint64_t>(dword_6A5D6C) << 32);
}

void (*const kDomains[SYNC_DOMAINS])(Hasher &) = {Units, Objects, Players, Map, Sprites, Ai};

}  // namespace

namespace {
// the checks' times since SyncHashTimes last took them, in the clock's ticks
std::atomic<int64_t> g_aqDomainTicks[SYNC_DOMAINS];
std::atomic<int64_t> g_aqDomainWords[SYNC_DOMAINS];
int64_t g_qWaitTicks;
int g_nChecks;

int64_t Ticks(void) {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

void SyncHashTimes(double adDomainMs[SYNC_DOMAINS], double adDomainWords[SYNC_DOMAINS], double *pdWaitMs,
                   int *pnChecks) {
  for (int d = 0; d < SYNC_DOMAINS; d++) {
    adDomainMs[d] = g_aqDomainTicks[d].exchange(0) / 1000.0;
    adDomainWords[d] = static_cast<double>(g_aqDomainWords[d].exchange(0));
  }
  *pdWaitMs = g_qWaitTicks / 1000.0;
  *pnChecks = g_nChecks;
  g_qWaitTicks = 0;
  g_nChecks = 0;
}

const char *SyncDomainName(int nDomain) {
  static const char *const kNames[] = {"units", "objects", "players", "map", "sprites", "ai"};
  static_assert(sizeof kNames / sizeof kNames[0] == SYNC_DOMAINS, "a name for each domain");
  return nDomain >= 0 && nDomain < SYNC_DOMAINS ? kNames[nDomain] : "?";
}

uint64_t SyncHashFold(void) {
  SyncHash h;
  SyncHashCompute(&h);
  uint64_t q = 0xCBF29CE484222325ull ^ h.dwRandState;
  for (int d = 0; d < SYNC_DOMAINS; ++d)
    q = (q ^ h.aqDomain[d]) * 0x100000001B3ull;
  return q;
}

void SyncHashCompute(SyncHash *pOut) {
  const int64_t qStart = Ticks();
  SyncScope();
  memset(pOut, 0, sizeof *pOut);
  pOut->dwFrame = oc_dwReplayFrame;
  pOut->dwVersion = SYNC_HASH_VERSION;
  pOut->dwRandState = sgNetRandState_g;
  pOut->dwRandCount = static_cast<uint32_t>(sgNetRandCount_g);
  // The domains only read, each with its own hasher, so they and their parts are hashed at once
  // on the worker pool while the game waits, to the same values; the map's, the longest, first.
  static const int kOrder[SYNC_DOMAINS] = {3, 5, 0, 4, 1, 2};
  auto hash = [pOut](int k) {
    const int d = kOrder[k];
    const int64_t q0 = Ticks();
    Hasher h = {0x9E3779B97F4A7C15ull + d, NULL};
    kDomains[d](h);
    pOut->aqDomain[d] = h.Digest();
    g_aqDomainTicks[d] += Ticks() - q0;
    g_aqDomainWords[d] += static_cast<int64_t>(h.nWords);
  };
  if (SyncSerial()) {
    for (int k = 0; k < SYNC_DOMAINS; k++)
      hash(k);
  } else {
    PortWorkersFor(SYNC_DOMAINS, hash);
  }
  g_qWaitTicks += Ticks() - qStart;
  ++g_nChecks;
}

void SyncSlotRead(int nSlot, SyncSlot *pOut) {
  memset(pOut, 0, sizeof *pOut);
  if (nSlot < 0 || nSlot >= PlayerSlots())
    return;
  pOut->nMinerals = gPlayerMinerals[nSlot];
  pOut->nGas = gPlayerGas[nSlot];
  pOut->nMineralsGathered = gPlayerMineralsGathered_g[nSlot];
  pOut->nGasGathered = gPlayerGasGathered_g[nSlot];
  for (int k = 0; k < 28; k++)
    pOut->adwScores[k] = glGameCounts_g[k][nSlot];
  // one chain, as the observer's logs have always had it
  uint64_t q = 0x9E3779B97F4A7C15ull;
  for (int i = 0; i < 4; i++)
    for (unsigned t = 0; UnitTypeValid(t); t = UnitTypeNext(t))
      q = Mix(q, glCounts_g[i][t][nSlot]);
  for (WORD t = 0; t < PLAYER_TECHS; t++)
    q = Mix(q, get_tech_available(nSlot, t) | get_tech_researched(nSlot, t) << 8);
  for (WORD u = 0; u < PLAYER_UPGRADES; u++)
    q = Mix(q, get_upgrade_level(nSlot, u) | get_max_upgrade_level(nSlot, u) << 8);
  pOut->dwTables = static_cast<uint32_t>(q ^ q >> 32);
}

bool SyncHashDump(const char *pszPath) {
  SyncScope();
  static const char kNames[SYNC_DOMAINS] = {'U', 'O', 'P', 'M', 'S', 'A'};
  for (int d = 0; d < SYNC_DOMAINS; d++) {
    char szPath[MAX_PATH];
    _snprintf(szPath, sizeof szPath, "%s.%c", pszPath, kNames[d]);
    szPath[sizeof szPath - 1] = '\0';
    FILE *f = fopen(szPath, "wb");
    if (!f)
      return false;
    Hasher h = {0, f};
    kDomains[d](h);
    fclose(f);
  }
  return true;
}
