// port_aipools.cpp -- the computer player's pools on 64 bits (h/aipools.h).
//
// 1.17's six pools of AI records hold 1,000 soldiers, guards, workers and buildings and 100
// towns and script threads for every computer of a game. A game past 1.17's limits takes as
// many as its units (PoolsForGame), up to 262,144 records of each kind, so on 64 bits each
// pool is storage here, past 1.17's block, sized for the most; the 32-bit build keeps 1.17's.
// What 1.17's block did for them, this does: the snapshots hold them, a grown pool's save is
// its records and its head, and the state check hashes a pointer into one as the 1.17
// address it stands for, so that a classic game still checks against 1.17's own.
#include <windows.h>
#include <stddef.h>

#include "../../h/aipools.h"
#include "../../h/CUnit.h"
#include "../../h/TAI_Captain.h"
#include "../../h/TAI_GuardPost.h"
#include "../../h/SAI_Build.h"
#include "../../h/SAI_Scripts.h"
#include "../../h/saveload.h"          // compress, decompress
#include "../../gen/save117.h"         // SAVE117_Soldier and the others
#include "../data/port_formats.h"      // PortSave117Compress, PortSave117Offset86
#include "../replays/port_snap.h"      // PortSnapStaticPool

AiPoolSizes g_aiPools = {AI_RECORDS_117, AI_TOWNS_117};

#if !defined(_M_IX86)

TSoldierPool g_aiSoldiers;
TGuardPool g_aiGuards;
TWorkerPool g_aiPeons;
TBldgPool g_aiBldgs;
TTownPool g_aiTowns;
TScriptPool g_aiScripts;

namespace {

// A pool: its records here, its head and what follows, and 1.17's pool at its address.
struct Pool {
  const BYTE *pRecs;
  size_t cbRecs;          // the storage's records, all of them
  const BYTE *pTail;      // the free head and the dword some pools keep after it
  size_t cbTail;
  const int *pnCount;     // the game's count (g_aiPools)
  unsigned int layout;    // a record's (gen/save117.h)
  DWORD va86;             // 1.17's pool
  int n117;               // 1.17's count
};

#define POOL(g, recs, head, count, layout, va, n)                                                         \
  {reinterpret_cast<const BYTE *>(g.recs), sizeof g.recs, reinterpret_cast<const BYTE *>(&g.head),          \
   sizeof g - offsetof(decltype(g), head), &g_aiPools.count, layout, va, n}

const Pool kPools[] = {
    POOL(g_aiSoldiers, aSoldiers, pFreeHead, records, SAVE117_Soldier, 0x6ACC50u, AI_RECORDS_117),
    POOL(g_aiGuards, aNodes, pFree, records, SAVE117_Guard, 0x6B6908u, AI_RECORDS_117),
    POOL(g_aiPeons, aWorkers, pFreeHead, records, SAVE117_Worker, 0x67F7B0u, AI_RECORDS_117),
    POOL(g_aiBldgs, aBldgs, pFreeHead, records, SAVE117_Bldg, 0x685578u, AI_RECORDS_117),
    POOL(g_aiTowns, aTowns, pFreeHead, towns, SAVE117_Town, 0x690160u, AI_TOWNS_117),
    POOL(g_aiScripts, items, pFree, towns, SAVE117_Script, 0x6A1A18u, AI_TOWNS_117),
};

#undef POOL

// The snapshots hold each pool's records in the game's count, and its head.
const bool s_bSnap = [] {
  for (const Pool &p : kPools) {
    PortSnapStaticPool(const_cast<BYTE *>(p.pRecs), p.cbRecs, PortSave117SizeHere(p.layout), p.pnCount);
    PortSnapStatic(const_cast<BYTE *>(p.pTail), p.cbTail);
  }
  return true;
}();

}  // namespace

bool AiPoolKey(const void *pv, unsigned long long *pqKey) {
  const BYTE *pb = static_cast<const BYTE *>(pv);
  for (size_t k = 0; k < sizeof kPools / sizeof kPools[0]; k++) {
    const Pool &p = kPools[k];
    const size_t cbRec = PortSave117SizeHere(p.layout);
    const size_t cb86 = PortSave117Size(p.layout);
    if (pb >= p.pTail && pb < p.pTail + p.cbTail) {
      // the head, and the dword after it: past 1.17's records, as there
      *pqKey = p.va86 + p.n117 * cb86 + static_cast<DWORD>(pb - p.pTail);
      return true;
    }
    if (pb < p.pRecs || pb >= p.pRecs + p.cbRecs)
      continue;
    const size_t nIndex = static_cast<size_t>(pb - p.pRecs) / cbRec;
    const unsigned int off = static_cast<unsigned int>(static_cast<size_t>(pb - p.pRecs) % cbRec);
    const int off86 = PortSave117Offset86(p.layout, off);
    if (nIndex < static_cast<size_t>(p.n117) && off86 >= 0)
      *pqKey = p.va86 + nIndex * cb86 + static_cast<DWORD>(off86);
    else
      *pqKey = 0x7200000000000000ull ^ static_cast<unsigned long long>(k) << 40 ^
               static_cast<unsigned long long>(nIndex) << 16 ^ off;
    return true;
  }
  return false;
}

void AiPoolSpan(const void **ppLo, const void **ppHi) {
  const BYTE *lo = kPools[0].pRecs, *hi = kPools[0].pTail + kPools[0].cbTail;
  for (const Pool &p : kPools) {
    lo = p.pRecs < lo ? p.pRecs : lo;
    hi = p.pTail + p.cbTail > hi ? p.pTail + p.cbTail : hi;
  }
  *ppLo = lo;
  *ppHi = hi;
}

#endif  // !_M_IX86

int AiPoolCompress(unsigned int layout, const void *pRecs, int nCount, void *const *ppHead, void *pFile) {
  const DWORD dwHead = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(*ppHead));
  return PortSave117Compress(layout, pRecs, static_cast<unsigned int>(nCount), pFile) && compress(&dwHead, 4, pFile);
}

int AiPoolDecompress(unsigned int layout, void *pRecs, int nCount, void **ppHead, void *pFile) {
  DWORD dwHead = 0;
  if (!PortSave117Decompress(layout, pRecs, static_cast<unsigned int>(nCount), pFile) || !decompress(&dwHead, 4, pFile))
    return 0;
  *ppHead = reinterpret_cast<void *>(static_cast<DWORD_PTR>(dwHead));
  return 1;
}
