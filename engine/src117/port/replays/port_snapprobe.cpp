// port_snapprobe.cpp -- a probe for the trainer's snapshots: what a game keeps in
// memory while it plays. HYPERION_SNAPPROBE=<a>,<b> (game frames) writes snapprobe.txt in the
// log folder: the game thread's heap blocks alive at frame a, by owner (SMemAlloc's file and
// line), the blocks it allocated after a and still holds at b, and the bytes that changed from
// a to b in 1.17's .data (g_data117, as 1.17 addresses) and in the wide block (g_wide117, as
// offsets); tools/snapprobe.py names them.
//
// A debugging aid: nothing here changes the game. Only the thread that runs the game is
// followed, so no lock is taken; with the variable unset it does nothing but one test.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include "../platform/port_paths.h"  // PortLogFile
#include "port_snapprobe.h"
#include "../../h/oc_data.h"
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

extern "C" unsigned char g_wide117[];  // the pointer-bearing globals (port_standalone.cpp)

namespace {

const unsigned kDataVa = 0x501000u;  // 1.17's .data, after .rdata
const size_t kWideSize = 0xE206E0u;   // gen/wide117.h's OC_WIDE_SIZE on x64

struct Block {
  size_t cb;
  const char *pszTag;
  int nLine;
  unsigned nFrame;  // the game frame it was allocated in, ~0 before a game
};

int g_nOn = -1;  // -1 not read, 0 off, 1 on
DWORD g_dwThread;
unsigned g_nA, g_nB;
bool g_bDone, g_bTaken;
std::map<const void *, Block> *g_pLive;
std::vector<unsigned char> g_dataAt, g_wideAt;
bool g_bInside;  // within the probe's own allocations

bool On(void)
{
  if (g_nOn < 0) {
    g_nOn = 0;
    const char *psz = getenv("HYPERION_SNAPPROBE");
    if (psz != NULL && sscanf(psz, "%u,%u", &g_nA, &g_nB) == 2 && g_nB > g_nA) {
      g_dwThread = GetCurrentThreadId();  // the first to allocate: the program's own thread
      g_pLive = new std::map<const void *, Block>;
      g_nOn = 1;
    }
  }
  return g_nOn == 1 && GetCurrentThreadId() == g_dwThread;
}

std::string Owner(const Block &b)
{
  char sz[160];
  snprintf(sz, sizeof sz, "%s:%d", b.pszTag != NULL ? b.pszTag : "?", b.nLine);
  return sz;
}

void Report(FILE *f, const char *pszHead, bool (*pfnWhich)(const Block &))
{
  std::map<std::string, std::pair<unsigned, size_t>> by;
  for (const auto &e : *g_pLive)
    if (pfnWhich(e.second)) {
      auto &v = by[Owner(e.second)];
      v.first++;
      v.second += e.second.cb;
    }
  fprintf(f, "%s\n", pszHead);
  for (const auto &e : by)
    fprintf(f, "  %-48s %6u blocks %12zu bytes\n", e.first.c_str(), e.second.first, e.second.second);
}

// the ranges that differ, a gap of one byte joined
void Diff(FILE *f, const char *pszHead, const unsigned char *pNow, const std::vector<unsigned char> &at, size_t nBase)
{
  fprintf(f, "\n%s\n", pszHead);
  size_t nRanges = 0, cbChanged = 0;
  for (size_t i = 0; i < at.size();) {
    if (pNow[i] == at[i]) {
      ++i;
      continue;
    }
    size_t j = i;
    while (j < at.size() && (pNow[j] != at[j] || (j + 1 < at.size() && pNow[j + 1] != at[j + 1])))
      ++j;
    fprintf(f, "  0x%06zX %zu\n", nBase + i, j - i);
    ++nRanges;
    cbChanged += j - i;
    i = j;
  }
  fprintf(f, "%zu ranges, %zu bytes\n", nRanges, cbChanged);
}

}  // namespace

void PortSnapProbeAlloc(const void *pv, size_t cb, const char *pszTag, int nLine)
{
  if (pv == NULL || g_bDone || g_bInside || !On())
    return;
  g_bInside = true;
  (*g_pLive)[pv] = {cb, pszTag, nLine, gwGameMode == 3 ? oc_dwReplayFrame : ~0u};
  g_bInside = false;
}

void PortSnapProbeFree(const void *pv)
{
  if (pv == NULL || g_bDone || g_bInside || !On())
    return;
  g_bInside = true;
  g_pLive->erase(pv);
  g_bInside = false;
}

// From the game loop, at a game frame's end.
void PortSnapProbeFrame(void)
{
  if (g_bDone || !On())
    return;
  const unsigned char *pData = reinterpret_cast<const unsigned char *>(OC_VA(kDataVa));
  const size_t cbData = OC_DATA_END - kDataVa;
  g_bInside = true;
  if (!g_bTaken && oc_dwReplayFrame >= g_nA) {
    g_bTaken = true;
    g_dataAt.assign(pData, pData + cbData);
    g_wideAt.assign(g_wide117, g_wide117 + kWideSize);
    // the blocks alive now, marked as at a
    for (auto &e : *g_pLive)
      if (e.second.nFrame != ~0u)
        e.second.nFrame = 0;
  } else if (g_bTaken && oc_dwReplayFrame >= g_nB) {
    g_bDone = true;
    FILE *f = fopen(PortLogFile("snapprobe.txt"), "w");
    if (f != NULL) {
      fprintf(f, "snapprobe: frames %u to %u\n\n", g_nA, g_nB);
      Report(f, "alive at a, allocated before the game's first frame:",
             [](const Block &b) { return b.nFrame == ~0u; });
      Report(f, "alive at a, allocated during play up to a:", [](const Block &b) { return b.nFrame == 0; });
      Report(f, "allocated after a and alive at b:", [](const Block &b) { return b.nFrame != ~0u && b.nFrame != 0; });
      Diff(f, "data: .data changed from a to b (1.17 address, bytes):", pData, g_dataAt, kDataVa);
      Diff(f, "wide: g_wide117 changed from a to b (offset, bytes):", g_wide117, g_wideAt, 0);
      fclose(f);
    }
  }
  g_bInside = false;
}
