// port_perf.cpp -- where a frame's time goes, by part, for HYPERION_PERF=1's log
// (port_present.cpp writes it once a second). The game loop marks each part's begin and end
// (gameloop.cpp); the marks only read the performance counter, so the game runs as it would.
//
// HYPERION_PROFILE=1 samples the game's thread instead: a thread of its own stops it about
// once a millisecond, walks its stack by the x64 unwind tables, and counts each function at
// its top (its own time) and anywhere on it (its time with what it calls). Every 10 seconds
// hyperion_profile.log gets the counts so far, named from the PDB beside the exe through
// dbghelp, loaded only then; the live channel's profile {reset?, write?} starts the counts
// again and writes them where it is asked, so a moment can be profiled by itself.
// It reads the thread and nothing else. The sampling is
// Windows' (its thread suspension and unwind tables); off Windows it is not there yet
//, and HYPERION_PROFILE does nothing.
#include <windows.h>
#include <stdio.h>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include "../tools/port_bench.h"  // PortBenchWanted, PortBenchPlaying, PortBenchSecond
#include "../tools/port_debugview.h"  // PortDebugViewShown
#include "../tools/port_framelog.h"  // PortFrameLogOn, a row a frame
#include "../tools/port_control.h"  // PortControlCommand
#include "../platform/port_json.h"
#include "../platform/port_paths.h"  // PortLogFile
#include "port_perf.h"
#include "../../h/port_synchash.h"  // SyncHashTimes
#include "CUnit.h"
#include "port_workers.h"  // PortCoresFit

namespace {

int g_nOn = -1;
LARGE_INTEGER g_aBegin[PERF_PARTS];
LONGLONG g_aTotal[PERF_PARTS];
LONGLONG g_aHeld[PERF_PARTS];  // PortPerfHold's, until PortPerfKeep says whether it counts
LONGLONG g_aFrame[PERF_PARTS];  // since the frame log's last row: a game frame's parts, a pass's
unsigned int g_nGameFrames;
unsigned int g_nUnitsMost;

const char *const kNames[PERF_PARTS] = {"events", "creep", "ai", "fog", "units", "movers", "triggers",
                                        "check", "minimap", "ui", "draw"};

// HYPERION_PROFILE's sampler
struct Count {
  unsigned int nSelf, nAll;
};
HANDLE g_hGame;  // the game's thread
std::unordered_map<DWORD64, Count> g_counts;  // by a function's start
unsigned int g_nSamples;
CRITICAL_SECTION g_csProfile;  // the counts, which the sampler takes only with the game running

typedef BOOL(WINAPI * SymFromAddrFn)(HANDLE, DWORD64, PDWORD64, void *);

// dbghelp's SymFromAddr, loaded and started when first asked for; NULL when it cannot be.
SymFromAddrFn SymbolsReady(void)
{
  typedef BOOL(WINAPI * SymInitializeFn)(HANDLE, PCSTR, BOOL);
  typedef DWORD(WINAPI * SymSetOptionsFn)(DWORD);
  static SymFromAddrFn s_pfnFromAddr;
  static bool s_bTried;
  if (!s_bTried) {
    s_bTried = true;
    if (HMODULE hHelp = LoadLibraryExA("dbghelp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
      SymSetOptionsFn pfnOptions = reinterpret_cast<SymSetOptionsFn>(GetProcAddress(hHelp, "SymSetOptions"));
      SymInitializeFn pfnInit = reinterpret_cast<SymInitializeFn>(GetProcAddress(hHelp, "SymInitialize"));
      s_pfnFromAddr = reinterpret_cast<SymFromAddrFn>(GetProcAddress(hHelp, "SymFromAddr"));
      if (pfnOptions != NULL)
        pfnOptions(0x00000002 | 0x00000004);  // SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS
      if (pfnInit == NULL || !pfnInit(GetCurrentProcess(), NULL, TRUE))
        s_pfnFromAddr = NULL;
    }
  }
  return s_pfnFromAddr;
}

void ProfileWrite(const char *pszPath)
{
  const SymFromAddrFn s_pfnFromAddr = SymbolsReady();
  std::vector<std::pair<DWORD64, Count>> rows(g_counts.begin(), g_counts.end());
  std::sort(rows.begin(), rows.end(), [](const std::pair<DWORD64, Count> &a, const std::pair<DWORD64, Count> &b) {
    return a.second.nAll > b.second.nAll;
  });
  FILE *pf = fopen(pszPath, "w");
  if (pf == NULL)
    return;
  fprintf(pf, "samples=%u (about 1 ms each); %%all is the function with what it calls, %%self alone\n", g_nSamples);
  const double f = g_nSamples > 0 ? 100.0 / g_nSamples : 0.0;
  for (size_t i = 0; i < rows.size() && i < 120; ++i) {
    char szName[200] = "?";
    if (s_pfnFromAddr != NULL) {
      // SYMBOL_INFO: 88 bytes before its name on x64, the name's room given in MaxNameLen
      alignas(8) BYTE ab[88 + 256] = {};
      *reinterpret_cast<ULONG *>(ab) = 88;              // SizeOfStruct
      *reinterpret_cast<ULONG *>(ab + 80) = 255;        // MaxNameLen
      DWORD64 qOff = 0;
      if (s_pfnFromAddr(GetCurrentProcess(), rows[i].first, &qOff, ab))
        snprintf(szName, sizeof szName, "%s", reinterpret_cast<const char *>(ab + 84));
    }
    fprintf(pf, "%6.2f %%all %6.2f %%self  %s\n", rows[i].second.nAll * f, rows[i].second.nSelf * f, szName);
  }
  fclose(pf);
}

#if defined(_WIN32)
DWORD WINAPI ProfileThread(void *)
{
  ULONGLONG qLastWrite = GetTickCount64();
  std::vector<DWORD64> seen;
  for (;;) {
    Sleep(1);
    if (SuspendThread(g_hGame) == static_cast<DWORD>(-1))
      return 0;
    CONTEXT ctx;
    ctx.ContextFlags = CONTEXT_FULL;
    seen.clear();
#if defined(_M_X64)  // the 32-bit build compiles this file only for tools/wide_globals.py's probes
    if (GetThreadContext(g_hGame, &ctx)) {
      // the stack, by the unwind tables, while the thread is stopped
      for (int nDepth = 0; nDepth < 48 && ctx.Rip != 0; ++nDepth) {
        DWORD64 qBase = 0;
        PRUNTIME_FUNCTION pFn = RtlLookupFunctionEntry(ctx.Rip, &qBase, NULL);
        if (pFn == NULL) {
          // a leaf function: its return address is at the stack's top
          if (nDepth == 0)
            seen.push_back(ctx.Rip);
          ctx.Rip = *reinterpret_cast<DWORD64 *>(ctx.Rsp);
          ctx.Rsp += 8;
          if (nDepth > 0)
            break;
          continue;
        }
        seen.push_back(qBase + pFn->BeginAddress);
        void *pHandler = NULL;
        DWORD64 qFrame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, qBase, ctx.Rip, pFn, &ctx, &pHandler, &qFrame, NULL);
      }
    }
#endif
    ResumeThread(g_hGame);
    EnterCriticalSection(&g_csProfile);
    if (!seen.empty()) {
      ++g_nSamples;
      ++g_counts[seen[0]].nSelf;
      std::sort(seen.begin(), seen.end());
      seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
      for (DWORD64 q : seen)
        ++g_counts[q].nAll;
    }
    if (GetTickCount64() - qLastWrite >= 10000) {
      qLastWrite = GetTickCount64();
      ProfileWrite(PortLogFile("hyperion_profile.log"));
    }
    LeaveCriticalSection(&g_csProfile);
  }
}

// profile {reset?, write?}: the counts written to the path write names, then, with reset
// true, started again.
bool CmdProfile(const PortJson &j, std::string *pAnswer)
{
  EnterCriticalSection(&g_csProfile);
  const unsigned int nSamples = g_nSamples;
  if (j["write"].kind == PortJson::STRING)
    ProfileWrite(j["write"].Str().c_str());
  if (j["reset"].kind == PortJson::BOOL && j["reset"].b) {
    g_counts.clear();
    g_nSamples = 0;
  }
  LeaveCriticalSection(&g_csProfile);
  *pAnswer = "\"samples\":" + std::to_string(nSamples);
  return true;
}

void ProfileStart(void)
{
  char sz[8];
  if (GetEnvironmentVariableA("HYPERION_PROFILE", sz, sizeof sz) == 0 || sz[0] != '1')
    return;
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_hGame,
                       THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0))
    return;
  InitializeCriticalSection(&g_csProfile);
  PortControlCommand("profile", CmdProfile);
  if (HANDLE h = CreateThread(NULL, 0, ProfileThread, NULL, 0, NULL))
    CloseHandle(h);
}
#else
void ProfileStart(void)
{
}
#endif

}  // namespace

bool PortSymbolName(unsigned long long qAddress, char *psz, size_t n)
{
  const SymFromAddrFn pfnFromAddr = SymbolsReady();
  if (pfnFromAddr == NULL || n == 0)
    return false;
  // SYMBOL_INFO: 88 bytes before its name on x64, the name's room given in MaxNameLen
  alignas(8) BYTE ab[88 + 256] = {};
  *reinterpret_cast<ULONG *>(ab) = 88;        // SizeOfStruct
  *reinterpret_cast<ULONG *>(ab + 80) = 255;  // MaxNameLen
  DWORD64 qOff = 0;
  if (!pfnFromAddr(GetCurrentProcess(), qAddress, &qOff, ab))
    return false;
  snprintf(psz, n, "%s", reinterpret_cast<const char *>(ab + 84));
  return true;
}

// Debug Tools' live numbers asked for (the live channel's "perf").
static bool g_bWantedLive;

const char *PortPerfPartName(int nPart)
{
  return nPart >= 0 && nPart < PERF_PARTS ? kNames[nPart] : "";
}

void PortPerfWant(bool bOn)
{
  g_bWantedLive = bOn;
}

bool PortPerfWanted(void)
{
  return g_bWantedLive;
}

// On with HYPERION_PERF=1, while a benchmark is wanted, which a run from the menus is only
// after the program has started, and while Debug Tools asks for the live numbers.
int PortPerfOn(void)
{
  if (g_nOn < 0) {
    char sz[8];
    g_nOn = GetEnvironmentVariableA("HYPERION_PERF", sz, sizeof sz) > 0 && sz[0] == '1' ? 1 : 0;
    ProfileStart();  // the first call is the game loop's, on the game's thread
  }
  return g_nOn > 0 || PortBenchWanted() || g_bWantedLive || PortDebugViewShown() || PortFrameLogOn() ? 1 : 0;
}

void PortPerfBegin(int nPart)
{
  if (PortPerfOn())
    QueryPerformanceCounter(&g_aBegin[nPart]);
}

void PortPerfEnd(int nPart)
{
  if (!PortPerfOn())
    return;
  // a part begun before the numbers were on has no beginning to count from
  if (g_aBegin[nPart].QuadPart == 0)
    return;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  g_aTotal[nPart] += now.QuadPart - g_aBegin[nPart].QuadPart;
  g_aFrame[nPart] += now.QuadPart - g_aBegin[nPart].QuadPart;
  g_aBegin[nPart].QuadPart = 0;
  // a pass's draw ends: the frame log's row for the frame it showed, with the pass's parts
  if (nPart == PERF_DRAW && PortFrameLogOn()) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    double adMs[PERF_PARTS] = {};
    for (int i : {PERF_EVENTS, PERF_UI, PERF_DRAW}) {
      adMs[i] = 1000.0 * static_cast<double>(g_aFrame[i]) / static_cast<double>(freq.QuadPart);
      g_aFrame[i] = 0;
    }
    PortFrameLogDrawEnd(adMs);
  }
}

// 1.17's CPU throttle (isCPUThrottleEnabled) sleeps a millisecond a pass of the game loop while it
// waits for a frame, inside the events' part: the sleep is left out of the part, and a benchmark
// run, which measures the game and not the wait, does not take it.
void PortPerfIdle(DWORD dwMs)
{
  if (PortBenchPlaying())
    return;
  const bool bOpen = PortPerfOn() && g_aBegin[PERF_EVENTS].QuadPart != 0;
  if (bOpen) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    g_aHeld[PERF_EVENTS] += now.QuadPart - g_aBegin[PERF_EVENTS].QuadPart;
  }
  Sleep(dwMs);
  if (bOpen)
    QueryPerformanceCounter(&g_aBegin[PERF_EVENTS]);
}

// The events' part of a pass of the game loop is held until the pass knows whether it runs a
// game frame: a pass that only waits for one (as many do at a fixed speed) is not a game frame's
// work, and counted it made the part the wait's length.
void PortPerfHold(int nPart)
{
  if (!PortPerfOn() || g_aBegin[nPart].QuadPart == 0)
    return;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  g_aHeld[nPart] += now.QuadPart - g_aBegin[nPart].QuadPart;
  g_aBegin[nPart].QuadPart = 0;
}

void PortPerfKeep(int nPart, bool bKeep)
{
  if (bKeep) {
    g_aTotal[nPart] += g_aHeld[nPart];
    g_aFrame[nPart] += g_aHeld[nPart];
  }
  g_aHeld[nPart] = 0;
}

void PortPerfGameFrame(void)
{
  // the units counted every 64 game frames for the game's cores (port_workers.h), and every
  // frame for HYPERION_PERF's log
  static unsigned int s_nFrame;
  const bool bCores = s_nFrame++ % 64 == 0;
  if (!bCores && !PortPerfOn())
    return;
  unsigned int nUnits = 0;
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
    ++nUnits;
  if (bCores)
    PortCoresFit(nUnits);
  if (!PortPerfOn())
    return;
  ++g_nGameFrames;
  if (nUnits > g_nUnitsMost)
    g_nUnitsMost = nUnits;
  // the frame log's row for this game frame: its own parts, the pass's left for the draw's end
  if (PortFrameLogOn()) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    double adMs[PERF_PARTS] = {};
    for (int i = 0; i < PERF_PARTS; ++i) {
      if (i == PERF_EVENTS || i == PERF_UI || i == PERF_DRAW)
        continue;
      adMs[i] = 1000.0 * static_cast<double>(g_aFrame[i]) / static_cast<double>(freq.QuadPart);
      g_aFrame[i] = 0;
    }
    PortFrameLogGame(adMs, nUnits);
  }
}

void PortPerfLine(char *psz, size_t n, double fSeconds, PortBenchSecond *pOut)
{
  LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);
  const double fFrames = g_nGameFrames > 0 ? g_nGameFrames : 1.0;
  int k = snprintf(psz, n, " game_frames/s=%.1f units=%u per_game_frame_ms:", g_nGameFrames / fSeconds,
                   g_nUnitsMost);
  if (pOut != NULL) {
    pOut->dGameFramesPerS = g_nGameFrames / fSeconds;
    pOut->nUnits = g_nUnitsMost;
  }
  for (int i = 0; i < PERF_PARTS && k > 0 && static_cast<size_t>(k) < n; ++i) {
    // the game's parts per game frame; the rest per second's share, as ms a game frame too
    const double fMs = 1000.0 * static_cast<double>(g_aTotal[i]) / static_cast<double>(freq.QuadPart) / fFrames;
    k += snprintf(psz + k, n - k, " %s=%.3f", kNames[i], fMs);
    if (pOut != NULL)
      pOut->adPartMs[i] = fMs;
    g_aTotal[i] = 0;
  }
  g_nGameFrames = 0;
  g_nUnitsMost = 0;
  // a Hyperion game's state checks: each domain's time a check, and the game's wait for them
  // (taken only when the line has room for them, so a full line leaves them to the next)
  double adDomainMs[SYNC_DOMAINS], adDomainWords[SYNC_DOMAINS], dWaitMs = 0.0;
  int nChecks = 0;
  if (k > 0 && static_cast<size_t>(k) < n)
    SyncHashTimes(adDomainMs, adDomainWords, &dWaitMs, &nChecks);
  if (nChecks > 0) {
    static const char kDomainNames[SYNC_DOMAINS] = {'U', 'O', 'P', 'M', 'S', 'A'};
    k += snprintf(psz + k, n - k, " checks=%d per_check_ms: wait=%.2f", nChecks, dWaitMs / nChecks);
    for (int d = 0; d < SYNC_DOMAINS && k > 0 && static_cast<size_t>(k) < n; ++d)
      k += snprintf(psz + k, n - k, " %c=%.2f/%.2fMw", kDomainNames[d], adDomainMs[d] / nChecks,
                    adDomainWords[d] / nChecks / 1e6);
  }
}
