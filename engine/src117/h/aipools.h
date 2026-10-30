// aipools.h -- the computer player's pools.
//
// 1.17's AI keeps a record for each unit it commands in pools every computer shares: 1,000
// soldiers (gSoldiers_g), guards (gGuards_g), workers (gPeons_g) and buildings (gBldgs_g),
// and 100 towns (gTowns_g) and script threads (gScripts_g). A unit made while its pool is
// empty gets no record, and the AI never orders it again: with 32 to 64 computers the pools
// fill within minutes. A game on a 1.17 map keeps 1.17's sizes; a game past 1.17's limits
// takes a record of each kind for every unit its table holds, and a town and a script thread
// for every 17 units (revision 17, port_pools.cpp).
//
// On 64 bits each pool is storage of Hyperion's own (port_aipools.cpp), sized for the most
// any game takes, and 1.17's names stand for it; the 32-bit build keeps 1.17's pools in its
// block. A game's pools are known before its save is read, as its other pools are.
#ifndef AIPOOLS_H
#define AIPOOLS_H

#include <string.h>
#include "poollimits.h"
#include "../port/data/port_formats.h"  // PortSave117Compress, PortSave117Decompress

#define AI_RECORDS_117 1000  // soldiers, guards, workers and buildings, each
#define AI_TOWNS_117 100     // towns and script threads, each

#if defined(_M_IX86)
#define AI_RECORDS_STORE AI_RECORDS_117
#define AI_TOWNS_STORE AI_TOWNS_117
#else
#define AI_RECORDS_STORE POOL_UNITS_MAX
#define AI_TOWNS_STORE ((POOL_UNITS_MAX * AI_TOWNS_117 + POOL_UNITS_117 - 1) / POOL_UNITS_117)
#if !defined(OC_DLL_MODE)
#define gSoldiers_g g_aiSoldiers
#define gGuards_g g_aiGuards
#define gPeons_g g_aiPeons
#define gBldgs_g g_aiBldgs
#define gTowns_g g_aiTowns
#define gScripts_g g_aiScripts
#endif
#endif

// The sizes of the game being played (PoolsForGame).
struct AiPoolSizes {
  int records;  // soldiers, guards, workers and buildings, each
  int towns;    // towns and script threads, each
};
extern AiPoolSizes g_aiPools;

// Whether the game's pools are past 1.17's: its save holds each pool as its records and then
// its free head, where 1.17's holds the pool whole, and a unit's link to its record keeps 24
// bits of the record's id past 65,535 records (CUnitIO.cpp).
inline bool AiPoolsGrown() {
  return g_aiPools.records != AI_RECORDS_117 || g_aiPools.towns != AI_TOWNS_117;
}
inline int AiLinkShift() {
  return g_aiPools.records > 0xFFFF ? 24 : 16;
}

// A grown pool's save: its nCount records in layout (gen/save117.h), in their save form, and
// then its free head, an id, in 4 bytes (port_aipools.cpp).
int AiPoolCompress(unsigned int layout, const void *pRecs, int nCount, void *const *ppHead, void *pFile);
int AiPoolDecompress(unsigned int layout, void *pRecs, int nCount, void **ppHead, void *pFile);

// A pool in 1.17's shape, N records and then the free head, as a save of 1.17's sizes holds it
// whole. The storage on 64 bits holds more records than 1.17's, so such a save goes
// through a copy of this shape, and gen/save117.h's pool layouts are made from it
// (save_layouts.py), not from the storage.
template <class T, int N> struct TAiPool117 {
  T aNodes[N];
  T *pFree;
};

// A pool's save, its records and free head already in their save form (PreSave): 1.17's pool
// whole in layoutPool, or a grown one's nCount records in layoutRec and its head.
template <class T, int N>
int AiPoolSave(unsigned int layoutPool, unsigned int layoutRec, const T *pRecs, int nCount, T *pHead, void *pFile) {
  if (AiPoolsGrown())
    return AiPoolCompress(layoutRec, pRecs, nCount, reinterpret_cast<void *const *>(&pHead), pFile);
  static TAiPool117<T, N> s;
  memcpy(static_cast<void *>(s.aNodes), pRecs, sizeof s.aNodes);
  s.pFree = pHead;
  return PortSave117Compress(layoutPool, &s, 1, pFile);
}
template <class T, int N>
int AiPoolLoad(unsigned int layoutPool, unsigned int layoutRec, T *pRecs, int nCount, T **ppHead, void *pFile) {
  if (AiPoolsGrown())
    return AiPoolDecompress(layoutRec, pRecs, nCount, reinterpret_cast<void **>(ppHead), pFile);
  static TAiPool117<T, N> s;
  if (!PortSave117Decompress(layoutPool, &s, 1, pFile))
    return 0;
  memcpy(static_cast<void *>(pRecs), s.aNodes, sizeof s.aNodes);
  *ppHead = s.pFree;
  return 1;
}

#if !defined(_M_IX86)
// For the state check (port_synchash.cpp): whether p points into one of the pools, and if so
// the key it hashes as, the 1.17 address it stands for in a record of 1.17's count; and the
// span of memory the pools take.
bool AiPoolKey(const void *p, unsigned long long *pqKey);
void AiPoolSpan(const void **ppLo, const void **ppHi);
#endif

#endif
