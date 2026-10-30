// port_replay.cpp -- replay turns that outlive the call that read them
// (docs/modules/replays.md, "Playback").
//
// net_mgr_replay_turns (net_mgr.cpp, 0x4769B0) builds its turn buffer, a CNetUnkn, over the
// static 0x200-byte arrays at 0x672DE8 (the command bytes) and 0x672FE8 (their sizes) on every
// call. A frame ahead of the game is read one call early; if its commands do not fit,
// net_record_expand grows the buffer onto the heap and the early return frees it with the
// CNetUnkn, so the next call dispatches stale bytes, which the original plays
// (engine/docs/replay-test.md, "The restart in M1G2-Leta"). Hyperion keeps the grown buffers
// until the next growth: PortReplayTurnsUse points each call's CNetUnkn at them, and
// PortReplayTurnsKeep takes over what a read grew, so the destructor leaves it. Playback only;
// a replay that never grows the buffer past 0x200 plays as 1.17 plays it.
#include <string.h>
#include <vector>
#include "port_replay.h"
#include "Storm.h"        // SMemFree
#include "CNetUnkn.h"

static BYTE *g_pTurnBytes;   // the buffers of the last growth, or null
static int *g_pnTurnSizes;
static DWORD g_nTurnCap;

void PortReplayTurnsUse(CNetUnkn *pTurns)
{
  if (g_pTurnBytes == NULL)
    return;
  pTurns->pBytes = g_pTurnBytes;
  pTurns->pnSizes = g_pnTurnSizes;
  pTurns->nCount = g_nTurnCap;
  pTurns->bOwned = 0;
}

void PortReplayTurnsKeep(CNetUnkn *pTurns)
{
  if (!pTurns->bOwned)
    return;   // the read did not grow the buffer
  // The first growth of this read started from the kept buffers with bOwned clear, so
  // net_record_expand copied them and left them allocated; a second growth freed the first's.
  if (g_pTurnBytes != NULL) {
    SMemFree(g_pTurnBytes, "port_replay.cpp", __LINE__, 0);
    SMemFree(g_pnTurnSizes, "port_replay.cpp", __LINE__, 0);
  }
  g_pTurnBytes = pTurns->pBytes;
  g_pnTurnSizes = pTurns->pnSizes;
  g_nTurnCap = pTurns->nCount;
  pTurns->bOwned = 0;
}

// The trainer's snapshots (port_snap.cpp): a frame read one call early may wait in the grown
// buffers, which a snapshot keeps. Buffers only grow, and a grown buffer is used from then on,
// so a restore copies the kept ones into the buffers of now, or, for a snapshot taken before
// any growth, 1.17's static arrays, which the snapshot restored.
extern BYTE byte_672DE8[];  // 0x672DE8, the turns' bytes (net_mgr.cpp)
extern int dword_672FE8[];  // 0x672FE8, their sizes

void PortReplayTurnsSave(std::vector<unsigned char> *pOut)
{
  pOut->clear();
  if (g_pTurnBytes == NULL)
    return;
  const size_t cb = g_nTurnCap + g_nTurnCap * sizeof(int);
  pOut->resize(sizeof(DWORD) + cb);
  memcpy(pOut->data(), &g_nTurnCap, sizeof(DWORD));
  memcpy(pOut->data() + sizeof(DWORD), g_pTurnBytes, g_nTurnCap);
  memcpy(pOut->data() + sizeof(DWORD) + g_nTurnCap, g_pnTurnSizes, g_nTurnCap * sizeof(int));
}

void PortReplayTurnsRestore(const std::vector<unsigned char> &saved)
{
  if (g_pTurnBytes == NULL)
    return;
  if (saved.empty()) {
    memcpy(g_pTurnBytes, byte_672DE8, 0x200);
    memcpy(g_pnTurnSizes, dword_672FE8, 0x200 * sizeof(int));
    return;
  }
  DWORD nCap;
  memcpy(&nCap, saved.data(), sizeof(DWORD));
  if (nCap > g_nTurnCap)
    return;  // cannot be: the buffers only grow
  memcpy(g_pTurnBytes, saved.data() + sizeof(DWORD), nCap);
  memcpy(g_pnTurnSizes, saved.data() + sizeof(DWORD) + nCap, nCap * sizeof(int));
}
