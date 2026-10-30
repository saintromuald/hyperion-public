// port_snap.cpp -- the trainer's snapshots: the game's state copied in memory at a
// game frame's end and put back in place in the same game, so it can go back to any moment it
// kept and play on from there (docs/modules/replays.md, "Snapshots for the trainer").
//
// A snapshot holds the game's parts of 1.17's two global blocks (gen/snapranges.h, from
// tools/snapranges.py), the heap blocks a game allocates as it starts and changes as it plays
// (kBlocks, noted by the allocator while they live), the port's statics that hold the game
// (PortSnapStatic), the replay's recorder or reader (gpNetRecord_g) by its place in its
// buffer, the replay feed's grown buffers (port_replay.cpp) and the single-player loopback's
// turns and messages (port_stormnet.cpp). The rest, from windows and sound to the camera, the
// speed and the pause, is left as it is at a restore. A snapshot is a list of 4 KB pages that
// shares each page equal to the last snapshot's; a restore copies every page back. The
// self-test is HYPERION_SNAPTEST=<period>,<back>[,<diff frame>], 1200 and 600 by default.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include "../platform/port_paths.h"  // PortLogFile
#include "port_snap.h"
#include "port_replay.h"  // PortReplayTurnsSave, PortReplayTurnsRestore
#include "../lan/port_stormnet.h"  // PortNetSnapSave, PortNetSnapRestore
#include "../render/port_scene.h"  // PortSceneStateRestored
#include "../../h/CUnitBounds.h"  // FinderSideRebuild
#include "../../h/pos.h"          // mpoint_t, for TAI_Captain.h
#include "../../h/TAI_Captain.h"  // CaptainSlotsForget
#include "../../h/oc_data.h"
#include "../../h/CNetRecord.h"
#include "CImage.h"
#include "CSprite.h"
#include "Priority.h"
#include "../../h/sai_Contours.h"
#include "../../h/poollimits.h"
#include "../../h/maplimits.h"
#include "../../h/port_synchash.h"
#include "../../h/port_hxreplay.h"  // ReplayHxRewound
#include "../../gen/snapranges.h"
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"
#ifdef _WIN32
#include <windows.h>
#endif

extern "C" unsigned char g_wide117[];  // the pointer-bearing globals (port_standalone.cpp)
// the draw order's record, as Priority.cpp declares it
struct spr_priority_t {
  sprite_key_t dwPriority;
  CSprite *pSprite;
};
extern spr_priority_t sgSprPriorities_g[POOL_SPRITES_STORE + 1] OC_UNALIGNED;
extern BYTE g_spritePriorityVisHash[MAP_SIDE_MAX];  // CSprite.cpp
extern int g_find_flag;
extern contour_workspace_t sgLocalPathContours_g;  // sai_PathFindLocal.cpp

namespace {

const size_t kPage = 4096;

// The allocation sites whose blocks hold the game (1.17's file and line, as SMemAlloc is
// told); a line of 0 is every line of the file.
const struct {
  const char *pszFile;
  int nLine;
} kBlocks[] = {
  {"Gamemap.cpp", 195},    // the map's doodads and sprites list
  {"Gamemap.cpp", 609},    // the MI map
  {"Gamemap.cpp", 610},    // the cell map
  {"Gamemap.cpp", 611},    // the scroll buffer's flags
  {"Gamemap.cpp", 612},    // the square map: tile flags, vision, creep
  {"Sai_path.cpp", 769},   // the path pool
  {"TAI_Captain.cpp", 1945},
  {"creep.cpp", 420},      // the creep map
  {"creep.cpp", 424},      // the creep edge map
  {"repulse.cpp", 323},    // the repulse map
  {"sai_PathCreate.cpp", 210},  // the regions, their neighbours and splits
  {"sai_ContoursCreate.cpp", 0},
  {".?AUTRIGGERNODE@@", -2},
};

struct Region {
  unsigned char *p;
  size_t cb;
  size_t nFirstPage;
};

struct Page {
  unsigned char ab[kPage];
};
typedef std::shared_ptr<const Page> PageRef;

// The recorder or reader with its pointers as places in its buffer: a recording grows the
// buffer, which moves it, and what it holds up to the place kept does not change.
struct RecordAt {
  bool bThere;
  int bRecording, bReading, nLen;
  DWORD dwFrame;
  intptr_t nCount, nRead;  // -1 for none
};

struct Snap {
  unsigned nFrame;
  std::vector<PageRef> pages;            // every region's pages, in g_regions' order
  RecordAt record;
  std::vector<unsigned char> abTurns;    // the feed's grown buffers
  std::vector<unsigned char> abNet;      // the loopback's turns in transit
};

std::map<const void *, size_t> *g_pBlocks;  // the game's heap blocks alive
// A static the snapshots hold: all of it, or as much of it as the game's pool uses
struct Static {
  void *pv;
  size_t cb;
  size_t cbEach;        // the bytes a pool slot takes, with pnCount
  const int *pnCount;   // the game's count (g_pools), or NULL for all of it
};
std::vector<Static> *g_pStatics;
std::vector<Region> g_regions;
size_t g_nPages;                // the pages g_regions covers
size_t g_nRegionStatics;        // the statics g_regions holds
bool g_bBlocksChanged = true;   // g_regions is to be made again from the blocks
PoolSizes g_regionPools;        // the pools g_regions was made for
typedef std::shared_ptr<const Snap> SnapRef;
std::map<int, std::map<unsigned, SnapRef>> g_lines;  // by line, by frame
unsigned g_nEvery;      // the self-test's snapshots, every so many frames
const int kTestLine = 0;
double g_msTake, g_msRestore;

// the self-test
int g_nTest = -1;  // -1 not read, 0 off, 1 on
unsigned g_nTestPeriod = 1200, g_nTestBack = 600;
unsigned g_nHighest;  // the furthest frame played
bool g_bAny;          // a frame has been played since the store began
std::vector<SyncHash> g_hashes;  // by frame, the first time each was played
unsigned g_nRewinds, g_nReplayed, g_nHashBad, g_nStateChecked, g_nStateBad;
unsigned g_nFirstBad = ~0u, g_nFirstBadDomains, g_nFirstStateBad = ~0u;
size_t g_nFirstStatePage;

size_t RegionPageBytes(const Region &r, size_t nPage);

// The bits of the state that drawing writes, which the self-test's whole compare leaves to the
// state hash: an image's dirty bit and its place on the map and screen as last drawn, the draw
// order's lists, and the unit finder's results and marks, which the console's searches also
// make. A snapshot holds them, and the next drawing or search makes them again. Also a local path
// search's blocker box: a search with no blocker copies its caller's unset box, the stack's
// leftovers, and reads it only when there is one (sai_PathFindLocal.cpp, as 1.17).
unsigned char ViewBits(const unsigned char *p)
{
  const unsigned char *pImages = reinterpret_cast<const unsigned char *>(sgImages);
  if (p >= pImages && p < pImages + sizeof sgImages) {
    const size_t n = static_cast<size_t>(p - pImages) % sizeof(CImage);
    if (n == offsetof(CImage, flags))
      return 0x01;  // Dirty
    if (n >= offsetof(CImage, wMapX) && n < offsetof(CImage, pGroup))
      return 0xFF;
    return 0;
  }
  const unsigned char *pOrder = reinterpret_cast<const unsigned char *>(sgSprPriorities_g);
  const unsigned char *pIter = reinterpret_cast<const unsigned char *>(sgSprPriorityIterator);
  const unsigned char *pRows = g_spritePriorityVisHash;
  const unsigned char *pFound = reinterpret_cast<const unsigned char *>(g_find_arr_unit);
  const unsigned char *pMarks = reinterpret_cast<const unsigned char *>(g_find_arr_flag);
  const unsigned char *pMark = reinterpret_cast<const unsigned char *>(&g_find_flag);
  const unsigned char *pSpan = reinterpret_cast<const unsigned char *>(&sgLocalPathContours_g.rcSpan);
  if (p >= pSpan && p < pSpan + sizeof sgLocalPathContours_g.rcSpan)
    return 0xFF;
  if ((p >= pFound && p < pFound + sizeof g_find_arr_unit) || (p >= pMarks && p < pMarks + sizeof g_find_arr_flag) ||
      (p >= pMark && p < pMark + sizeof g_find_flag))
    return 0xFF;
  if ((p >= pOrder && p < pOrder + sizeof sgSprPriorities_g) || (p >= pIter && p < pIter + sizeof sgSprPriorityIterator) ||
      (p >= pRows && p < pRows + MAP_SIDE_MAX))
    return 0xFF;
  return 0;
}

bool SameByte(const unsigned char *pNow, unsigned char was)
{
  return ((*pNow ^ was) & ~ViewBits(pNow)) == 0;
}

#ifdef _WIN32
// The self-test's whole-program diff: every writable section of the program at the first
// snapshot frame after the first rewind's target, the first time and again, so snapdiff.txt
// lists all that a restore left otherwise (tools/snapdiff.py names it by the linker map).
unsigned g_nDiffFrame = ~0u;
std::vector<std::pair<unsigned, std::vector<unsigned char>>> g_image;  // section RVA, bytes

template <class F> void Sections(F f)
{
  unsigned char *pBase = reinterpret_cast<unsigned char *>(GetModuleHandleA(NULL));
  const IMAGE_NT_HEADERS *pNt =
      reinterpret_cast<const IMAGE_NT_HEADERS *>(pBase + reinterpret_cast<const IMAGE_DOS_HEADER *>(pBase)->e_lfanew);
  const IMAGE_SECTION_HEADER *pSec = IMAGE_FIRST_SECTION(pNt);
  for (unsigned i = 0; i < pNt->FileHeader.NumberOfSections; i++)
    if ((pSec[i].Characteristics & IMAGE_SCN_MEM_WRITE) && pSec[i].Misc.VirtualSize > 0)
      f(pSec[i].VirtualAddress, pBase + pSec[i].VirtualAddress, static_cast<size_t>(pSec[i].Misc.VirtualSize),
        reinterpret_cast<const char *>(pSec[i].Name));
}

void ImageTake(void)
{
  g_image.clear();
  Sections([](unsigned rva, const unsigned char *p, size_t cb, const char *) {
    g_image.push_back({rva, std::vector<unsigned char>(p, p + cb)});
  });
}

// A snapshot's pages against the state now, every span that differs, by section (snapstate.txt;
// tools/snapdiff.py names it). A heap block's spans are under "heap", by region and offset.
void StateDiff(const Snap &s, unsigned nFrame)
{
  FILE *f = fopen(PortLogFile("snapstate.txt"), "w");
  if (f == NULL)
    return;
  fprintf(f, "snapstate: frame %u, its snapshot against the pass after a rewind (rva, bytes)\n", nFrame);
  unsigned char *pBase = reinterpret_cast<unsigned char *>(GetModuleHandleA(NULL));
  unsigned nLastRva = ~0u;
  for (size_t ri = 0; ri < g_regions.size(); ri++) {
    const Region &r = g_regions[ri];
    for (size_t n = r.nFirstPage; n < r.nFirstPage + (r.cb + kPage - 1) / kPage && n < s.pages.size(); n++) {
      const unsigned char *pNow = r.p + (n - r.nFirstPage) * kPage;
      const unsigned char *pWas = s.pages[n]->ab;
      const size_t cb = RegionPageBytes(r, n);
      for (size_t i = 0; i < cb;) {
        if (SameByte(pNow + i, pWas[i])) {
          ++i;
          continue;
        }
        size_t j = i;
        while (j < cb && !SameByte(pNow + j, pWas[j]))
          ++j;
        const unsigned char *pAt = pNow + i;
        unsigned nSecRva = ~0u;
        const char *pszSec = "heap";
        Sections([&](unsigned rva, const unsigned char *p, size_t cbSec, const char *pszName) {
          if (pAt >= p && pAt < p + cbSec) {
            nSecRva = rva;
            pszSec = pszName;
          }
        });
        if (nSecRva != nLastRva) {
          fprintf(f, "section %.8s 0x%X\n", pszSec, nSecRva == ~0u ? 0u : nSecRva);
          nLastRva = nSecRva;
        }
        if (nSecRva == ~0u)
          fprintf(f, "  region %zu +0x%zX %zu\n", ri, (n - r.nFirstPage) * kPage + i, j - i);
        else
          fprintf(f, "  0x%08zX %zu", static_cast<size_t>(pAt - pBase), j - i);
        // the bytes then and now, the first 16
        fprintf(f, " was");
        for (size_t k = i; k < j && k < i + 16; k++)
          fprintf(f, " %02X", pWas[k]);
        fprintf(f, " now");
        for (size_t k = i; k < j && k < i + 16; k++)
          fprintf(f, " %02X", pNow[k]);
        fprintf(f, "\n");
        i = j;
      }
    }
  }
  fclose(f);
}

void ImageDiff(void)
{
  FILE *f = fopen(PortLogFile("snapdiff.txt"), "w");
  if (f == NULL)
    return;
  fprintf(f, "snapdiff: frame %u, the first pass against the pass after a rewind (rva, bytes)\n", g_nDiffFrame);
  Sections([f](unsigned rva, const unsigned char *p, size_t cb, const char *pszName) {
    for (const auto &img : g_image) {
      if (img.first != rva)
        continue;
      fprintf(f, "section %.8s 0x%X\n", pszName, rva);
      const unsigned char *pWas = img.second.data();
      for (size_t i = 0; i < cb && i < img.second.size();) {
        if (p[i] == pWas[i]) {
          ++i;
          continue;
        }
        size_t j = i;
        while (j < cb && (p[j] != pWas[j] || (j + 8 < cb && memcmp(p + j + 1, pWas + j + 1, 8) != 0)))
          ++j;
        fprintf(f, "  0x%08zX %zu\n", rva + i, j - i);
        i = j;
      }
    }
  });
  fclose(f);
}
#endif

std::vector<Static> &Statics(void)
{
  if (g_pStatics == NULL)
    g_pStatics = new std::vector<Static>;
  return *g_pStatics;
}

size_t StaticBytes(const Static &s)
{
  if (s.pnCount == NULL)
    return s.cb;
  const size_t cb = s.cbEach * static_cast<size_t>(*s.pnCount);
  return cb < s.cb ? cb : s.cb;
}

// A pool's slots in use in this game, for a grown global's part of the wide block:
// the storage holds the most any game takes, 330 MB at 262,144 units, and a game walks only
// its own count.
size_t GrownCount(const SnapGrown &g)
{
  int n = 0;
  switch (g.pool) {
  case SNAP_POOL_UNITS: n = g_pools.units; break;
  case SNAP_POOL_FINDER: n = g_pools.finder; break;
  case SNAP_POOL_SPRITES: n = g_pools.sprites; break;
  case SNAP_POOL_IMAGES: n = g_pools.images; break;
  case SNAP_POOL_BULLETS: n = g_pools.bullets; break;
  case SNAP_POOL_THINGIES: n = g_pools.thingies; break;
  case SNAP_POOL_ORDERS: n = g_pools.orders; break;
  }
  return static_cast<size_t>(n) + g.extra;
}

// The list's lock: a player table's first use may be a worker thread's, two at once, while
// the state check reads the game (port_workers.h). Kept for the program's life, as the list is.
std::mutex &StaticsLock(void)
{
  static std::mutex *s_pLock = new std::mutex;
  return *s_pLock;
}

bool SameFile(const char *pszTag, const char *pszFile)
{
  for (; *pszTag && *pszFile; ++pszTag, ++pszFile) {
    char a = *pszTag, b = *pszFile;
    if (a >= 'A' && a <= 'Z')
      a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z')
      b = static_cast<char>(b - 'A' + 'a');
    if (a != b)
      return false;
  }
  return *pszTag == 0 && *pszFile == 0;
}

bool GameBlock(const char *pszTag, int nLine)
{
  if (pszTag == NULL)
    return false;
  for (const auto &b : kBlocks)
    if ((b.nLine == 0 || b.nLine == nLine) && SameFile(pszTag, b.pszFile))
      return true;
  return false;
}

double MsSince(std::chrono::steady_clock::time_point t0)
{
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void AddRegion(unsigned char *p, size_t cb)
{
  g_regions.push_back({p, cb, g_nPages});
  g_nPages += (cb + kPage - 1) / kPage;
}

// The regions again: the blocks are a game's, so every snapshot made before is another game's.
void MakeRegions(void)
{
  g_regions.clear();
  g_nPages = 0;
  unsigned char *pData = reinterpret_cast<unsigned char *>(OC_VA(0x4EB000u));
  for (const SnapRange &r : kSnapData)
    AddRegion(pData + r.off, r.size);
#if defined(_WIN64) || defined(__LP64__)
  for (const SnapRange &r : kSnapWide)
    AddRegion(g_wide117 + r.off, r.size);
  for (const SnapGrown &g : kSnapGrown) {
    const size_t cb = GrownCount(g) * g.stride;
    AddRegion(g_wide117 + g.off, cb < g.size ? cb : g.size);
  }
#endif
  g_regionPools = g_pools;
  if (g_pBlocks != NULL)
    for (const auto &b : *g_pBlocks)
      AddRegion(static_cast<unsigned char *>(const_cast<void *>(b.first)), b.second);
  g_nRegionStatics = 0;
  g_bBlocksChanged = false;
}

// The statics registered since the regions were made, after the blocks.
void AddStatics(void)
{
  std::lock_guard<std::mutex> lock(StaticsLock());
  auto &statics = Statics();
  for (; g_nRegionStatics < statics.size(); g_nRegionStatics++)
    AddRegion(static_cast<unsigned char *>(statics[g_nRegionStatics].pv), StaticBytes(statics[g_nRegionStatics]));
}

bool SamePools(const PoolSizes &a, const PoolSizes &b)
{
  return a.units == b.units && a.finder == b.finder && a.sprites == b.sprites && a.images == b.images &&
         a.bullets == b.bullets && a.thingies == b.thingies && a.orders == b.orders;
}

void Regions(void)
{
  if (g_bBlocksChanged || !SamePools(g_regionPools, g_pools)) {
    g_lines.clear();
    MakeRegions();
  }
  AddStatics();
}

size_t RegionPageBytes(const Region &r, size_t nPage)
{
  const size_t nAt = (nPage - r.nFirstPage) * kPage;
  return r.cb - nAt < kPage ? r.cb - nAt : kPage;
}

void Take(int nLine, unsigned nFrame)
{
  const auto t0 = std::chrono::steady_clock::now();
  Regions();
  // the line's last snapshot before this frame, whose pages may be shared
  auto &snaps = g_lines[nLine];
  const Snap *pPrev = NULL;
  auto it = snaps.lower_bound(nFrame);
  if (it != snaps.begin())
    pPrev = std::prev(it)->second.get();
  auto pSnap = std::make_shared<Snap>();
  Snap &s = *pSnap;
  s.nFrame = nFrame;
  s.pages.resize(g_nPages);
  for (const Region &r : g_regions)
    for (size_t n = r.nFirstPage; n < r.nFirstPage + (r.cb + kPage - 1) / kPage; n++) {
      const unsigned char *pAt = r.p + (n - r.nFirstPage) * kPage;
      const size_t cb = RegionPageBytes(r, n);
      if (pPrev != NULL && n < pPrev->pages.size() && memcmp(pPrev->pages[n]->ab, pAt, cb) == 0) {
        s.pages[n] = pPrev->pages[n];
        continue;
      }
      auto page = std::make_shared<Page>();
      memcpy(page->ab, pAt, cb);
      s.pages[n] = page;
    }
  s.record = {};
  if (const CNetRecord *p = gpNetRecord_g) {
    s.record = {true, p->bRecording, p->bReading, p->nLen, p->dwFrame,
                p->pCount != NULL ? p->pCount - p->pBuf : -1, p->pRead != NULL ? p->pRead - p->pBuf : -1};
  }
  PortReplayTurnsSave(&s.abTurns);
  PortNetSnapSave(&s.abNet);
  snaps[nFrame] = pSnap;
  g_msTake = MsSince(t0);
}

// The local commands queued and not yet sent, less all but the syncs, which every turn holds.
void QueueKeepSyncs(void)
{
  sgdwBytesInCmdQueue = static_cast<DWORD>(PortNetKeepSyncs(sgbCmdQueue_g, sgdwBytesInCmdQueue));
}

void Put(const Snap &s, bool bFeed)
{
  const auto t0 = std::chrono::steady_clock::now();
  Regions();
  for (const Region &r : g_regions)
    for (size_t n = r.nFirstPage; n < r.nFirstPage + (r.cb + kPage - 1) / kPage; n++) {
      unsigned char *pAt = r.p + (n - r.nFirstPage) * kPage;
      const size_t cb = RegionPageBytes(r, n);
      if (n < s.pages.size())
        memcpy(pAt, s.pages[n]->ab, cb);
      else
        memset(pAt, 0, cb);  // a static registered after the snapshot: untouched then
    }
  if (CNetRecord *p = gpNetRecord_g) {
    if (s.record.bThere && s.record.nLen <= p->nCapacity) {
      p->bRecording = s.record.bRecording;
      p->bReading = s.record.bReading;
      p->nLen = s.record.nLen;
      p->dwFrame = s.record.dwFrame;
      p->pCount = s.record.nCount >= 0 ? p->pBuf + s.record.nCount : NULL;
      p->pRead = s.record.nRead >= 0 ? p->pBuf + s.record.nRead : NULL;
    }
  }
  PortReplayTurnsRestore(s.abTurns);
  PortNetSnapRestore(s.abNet, bFeed);
  if (bFeed)
    QueueKeepSyncs();
  oc_dwReplayFrame = s.nFrame;
  ReplayHxRewound(s.nFrame);
  PortSceneStateRestored();
  FinderSideRebuild();  // the orderings' sides
  CaptainSlotsForget();  // the captains' unit slots
  g_msRestore = MsSince(t0);
}

// The first page that differs from a snapshot, or ~0.
size_t Differs(const Snap &s)
{
  for (const Region &r : g_regions)
    for (size_t n = r.nFirstPage; n < r.nFirstPage + (r.cb + kPage - 1) / kPage && n < s.pages.size(); n++)
      if (memcmp(s.pages[n]->ab, r.p + (n - r.nFirstPage) * kPage, RegionPageBytes(r, n)) != 0) {
        const unsigned char *pNow = r.p + (n - r.nFirstPage) * kPage;
        for (size_t i = 0; i < RegionPageBytes(r, n); i++)
          if (!SameByte(pNow + i, s.pages[n]->ab[i]))
            return n;
      }
  return ~static_cast<size_t>(0);
}

// The region and offset of a page, for the report.
void PageWhere(size_t nPage, char *psz, size_t cch)
{
  for (size_t i = 0; i < g_regions.size(); i++) {
    const Region &r = g_regions[i];
    if (nPage >= r.nFirstPage && nPage < r.nFirstPage + (r.cb + kPage - 1) / kPage) {
      snprintf(psz, cch, "region %zu (%zu bytes) +0x%zX", i, r.cb, (nPage - r.nFirstPage) * kPage);
      return;
    }
  }
  snprintf(psz, cch, "page %zu", nPage);
}

bool TestOn(void)
{
  if (g_nTest < 0) {
    g_nTest = 0;
    const char *psz = getenv("HYPERION_SNAPTEST");
    if (psz != NULL && *psz) {
      g_nTest = 1;
      unsigned a, b, c;
      const int n = sscanf(psz, "%u,%u,%u", &a, &b, &c);
      if (n >= 2 && a > 0 && b > 0 && b < a) {
        g_nTestPeriod = a;
        g_nTestBack = b;
      }
#ifdef _WIN32
      if (n == 3)
        g_nDiffFrame = c;  // the frame of the whole-program diff, played twice
#endif
      g_nEvery = 120;
    }
  }
  return g_nTest == 1;
}

void TestReport(void)
{
  FILE *f = fopen(PortLogFile("snaptest.txt"), "w");
  if (f == NULL)
    return;
  PortSnapStats st;
  PortSnapStatsGet(&st);
  fprintf(f, "snaptest: back %u frames every %u, snapshots every %u\n", g_nTestBack, g_nTestPeriod, g_nEvery);
  fprintf(f, "furthest frame %u, rewinds %u, frames played again %u\n", g_nHighest, g_nRewinds, g_nReplayed);
  fprintf(f, "frames whose hash differed %u", g_nHashBad);
  if (g_nFirstBad != ~0u)
    fprintf(f, " (first at %u, domains %#x)", g_nFirstBad, g_nFirstBadDomains);
  fprintf(f, "\nsnapshot frames compared whole %u, differed %u", g_nStateChecked, g_nStateBad);
  if (g_nFirstStateBad != ~0u) {
    char sz[96];
    PageWhere(g_nFirstStatePage, sz, sizeof sz);
    fprintf(f, " (first at %u, %s)", g_nFirstStateBad, sz);
  }
  fprintf(f, "\nsnapshots %u (frames %u to %u), state %zu bytes in %zu regions, store %zu bytes\n", st.nSnaps,
          st.nFirst, st.nLast, st.cbState, g_regions.size(), st.cbStored);
  fprintf(f, "last take %.2f ms, last restore %.2f ms\n", st.msTake, st.msRestore);
  fprintf(f, "%s\n", g_nHashBad == 0 && g_nStateBad == 0 && g_nRewinds > 0 ? "exact" : g_nRewinds == 0 ? "no rewind" : "DIFFERS");
  fclose(f);
}

void TestFrame(unsigned nFrame)
{
  SyncHash h;
  SyncHashCompute(&h);
  if (!g_bAny || nFrame > g_nHighest) {
    g_bAny = true;
    g_nHighest = nFrame;
    if (g_hashes.size() <= nFrame)
      g_hashes.resize(nFrame + 1);
    g_hashes[nFrame] = h;
#ifdef _WIN32
    if (g_nDiffFrame == ~0u)
      g_nDiffFrame = g_nTestPeriod - g_nTestBack + 120;
    if (nFrame == g_nDiffFrame)
      ImageTake();
#endif
  } else {
    g_nReplayed++;
    const SyncHash &want = g_hashes[nFrame];
    unsigned nDomains = 0;
    for (int d = 0; d < SYNC_DOMAINS; d++)
      if (h.aqDomain[d] != want.aqDomain[d])
        nDomains |= 1u << d;
    if (h.dwRandState != want.dwRandState || h.dwRandCount != want.dwRandCount)
      nDomains |= 0x100;
    if (nDomains) {
      if (g_nHashBad++ == 0) {
        g_nFirstBad = nFrame;
        g_nFirstBadDomains = nDomains;
        TestReport();
      }
    }
#ifdef _WIN32
    if (nFrame == g_nDiffFrame && !g_image.empty()) {
      ImageDiff();
      g_image.clear();
    }
#endif
    auto &snaps = g_lines[kTestLine];
    auto it = snaps.find(nFrame);
    if (it != snaps.end()) {
      if (g_nStateChecked++ % 20 == 0)
        TestReport();
      const size_t n = Differs(*it->second);
      if (n != ~static_cast<size_t>(0) && g_nStateBad++ == 0) {
        g_nFirstStateBad = nFrame;
        g_nFirstStatePage = n;
#ifdef _WIN32
        StateDiff(*it->second, nFrame);
#endif
        TestReport();
      }
    }
  }
}

}  // namespace

void PortSnapAlloc(const void *pv, size_t cb, const char *pszTag, int nLine)
{
  if (pv == NULL || !GameBlock(pszTag, nLine))
    return;
  if (g_pBlocks == NULL)
    g_pBlocks = new std::map<const void *, size_t>;
  (*g_pBlocks)[pv] = cb;
  g_bBlocksChanged = true;
}

void PortSnapFree(const void *pv)
{
  if (g_pBlocks == NULL || g_pBlocks->erase(pv) == 0)
    return;
  g_bBlocksChanged = true;
  g_lines.clear();  // the game whose blocks they were is over, or its blocks moved
}

bool PortSnapStatic(void *pv, size_t cb)
{
  std::lock_guard<std::mutex> lock(StaticsLock());
  if (pv != NULL && cb > 0)
    Statics().push_back({pv, cb, 0, NULL});
  return true;
}

bool PortSnapStaticPool(void *pv, size_t cb, size_t cbEach, const int *pnCount)
{
  std::lock_guard<std::mutex> lock(StaticsLock());
  if (pv != NULL && cb > 0)
    Statics().push_back({pv, cb, cbEach, pnCount});
  return true;
}

void PortSnapFrame(void)
{
  const bool bTest = TestOn();
  if (g_nEvery == 0)
    return;
  const unsigned nFrame = oc_dwReplayFrame;
  if (bTest)
    TestFrame(nFrame);
  if (nFrame % g_nEvery == 0 && !PortSnapHas(kTestLine, nFrame))
    Take(kTestLine, nFrame);
  if (bTest && nFrame == g_nHighest && nFrame >= g_nTestPeriod && nFrame % g_nTestPeriod == 0) {
    // once at each period's frame: the frames played again run up to it, then it is passed
    static unsigned s_nLastRewind = ~0u;
    if (s_nLastRewind != nFrame) {
      s_nLastRewind = nFrame;
      if (PortSnapRestore(kTestLine, nFrame - g_nTestBack, false) != ~0u)
        g_nRewinds++;
      TestReport();
    }
  }
}

void PortSnapTake(int nLine, unsigned nFrame)
{
  Take(nLine, nFrame);
}

bool PortSnapHas(int nLine, unsigned nFrame)
{
  auto it = g_lines.find(nLine);
  return it != g_lines.end() && it->second.count(nFrame) != 0;
}

unsigned PortSnapRestore(int nLine, unsigned nFrame, bool bFeed)
{
  if (g_bBlocksChanged)
    return ~0u;
  auto line = g_lines.find(nLine);
  if (line == g_lines.end())
    return ~0u;
  auto it = line->second.upper_bound(nFrame);
  if (it == line->second.begin())
    return ~0u;
  --it;
  Put(*it->second, bFeed);
  return it->first;
}

unsigned PortSnapLatest(int nLine, unsigned nFrame)
{
  auto line = g_lines.find(nLine);
  if (line == g_lines.end())
    return ~0u;
  auto it = line->second.upper_bound(nFrame);
  return it == line->second.begin() ? ~0u : (--it)->first;
}

void PortSnapThin(int nLine, unsigned nEvery)
{
  auto line = g_lines.find(nLine);
  if (line == g_lines.end() || nEvery == 0)
    return;
  // the first kept, the earliest a seek can go back to
  for (auto it = line->second.begin(); it != line->second.end();)
    it = it != line->second.begin() && it->first % nEvery != 0 ? line->second.erase(it) : std::next(it);
}

void PortSnapShare(int nFrom, int nTo, unsigned nUpTo)
{
  auto from = g_lines.find(nFrom);
  if (from == g_lines.end())
    return;
  auto &to = g_lines[nTo];
  for (auto it = from->second.begin(); it != from->second.end() && it->first <= nUpTo; ++it)
    to[it->first] = it->second;
}

void PortSnapDropAfter(int nLine, unsigned nFrame)
{
  auto line = g_lines.find(nLine);
  if (line != g_lines.end())
    line->second.erase(line->second.upper_bound(nFrame), line->second.end());
}

void PortSnapDropLine(int nLine)
{
  g_lines.erase(nLine);
}

void PortSnapClear(void)
{
  g_lines.clear();
}

size_t PortSnapStateBytes(void)
{
  size_t cb = 0;
  for (const Region &r : g_regions)
    cb += r.cb;
  return cb;
}

size_t PortSnapStoreBytes(void)
{
  std::vector<const void *> seen;
  for (const auto &line : g_lines)
    for (const auto &e : line.second)
      for (const PageRef &p : e.second->pages)
        seen.push_back(p.get());
  std::sort(seen.begin(), seen.end());
  return (std::unique(seen.begin(), seen.end()) - seen.begin()) * kPage;
}

void PortSnapStatsGet(PortSnapStats *pOut)
{
  PortSnapStats st = {};
  for (const auto &line : g_lines) {
    st.nSnaps += static_cast<unsigned>(line.second.size());
    if (!line.second.empty()) {
      st.nFirst = st.nSnaps == line.second.size() ? line.second.begin()->first : (std::min)(st.nFirst, line.second.begin()->first);
      st.nLast = (std::max)(st.nLast, line.second.rbegin()->first);
    }
  }
  st.cbState = PortSnapStateBytes();
  st.cbStored = PortSnapStoreBytes();
  st.msTake = g_msTake;
  st.msRestore = g_msRestore;
  *pOut = st;
}
