// port_bench.cpp -- a benchmark run: the game it plays
// and its log, a folder under benchmarks\ in the data folder named for its start, its map and
// its players, with settings.json, seconds.csv, summary.json, frames.csv when asked for, and
// run.rep (docs/modules/tools.md, "The benchmark").
//
// seconds.csv has, once a second, frames shown and game frames a second, the units and each
// part's ms (port_perf.cpp's parts); summary.json's end state is the replay check's hash, which
// says whether two runs played the same game. The numbers are HYPERION_PERF's, which a run
// turns on. The run keeps the last minute of seconds and every shown frame's time for the
// overlay and the summary.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_bench.h"
#include "../gameplay/port_launch.h"  // PortLaunchBench, PortLaunchBenchReplay
#include "../gameplay/port_mutators.h"  // MutatorsOfGame, MutatorsJson
#include "../gameplay/port_session.h"  // SessionClear
#include "../gameplay/port_teams.h"    // TeamsSidesLeft
#include "../platform/port_paths.h"  // PortCommandOptions, PortDataFolder, PortOptions
#include "../loop/port_perf.h"  // PERF_PARTS
#include "../render/port_present.h"  // PortPresentGpu, PortPresentPerfFlush, PortFrameRateSet
#include "../render/port_scene.h"  // PortSceneSoftware
#include "../platform/port_log.h"  // PortLogger
#include "../platform/port_json.h"  // PortJsonQuote
#include "../render/port_gpu.h"        // PortGpuDriver
#include "../../h/net_cmds.h"   // gameloop_init
#include "../../h/replay.h"     // replay_set_speed
#include "../../h/map_load.h"   // GetCurrentMap
#include "../../h/port_synchash.h"  // the end state
#include "CUnit.h"
#include "TrigAct.h"            // pause_game, unpause_game
#include "../../h/hxrules.h"       // HX_OPT_TEAMS
#include "../../h/playertables.h"  // PlayersChooseColourRgb
#include "port_benchinfo.h"  // PortBenchPlayersNote, PortBenchWasSeat, PortBenchSelectedCount
#include "port_benchoverlay.h"
#include "../lab/port_labworker.h"  // PortLabWorkerOut
#include "../gameplay/port_ai.h"             // AiLevelName, AiLevelsLabel
#include "../../gen/globals117.h"  // gPlayerData
#include "../platform/port_settings.h"       // the frame rate and vertical sync put back after a run
#include "../../../hyperion/version.h"
#include "../platform/port_globals117.h"
#include "../gameplay/port_trigrun.h"  // PortTrigCreate, a stress run's armies
#include "SAI_Banker.h"                  // GetRegion_xy
#include "SAI_Main.h"                    // SAI_RemoveUnit
#include "sai_TownBuilder.h"             // GetAreaIndex
#include "CSprite.h"
#include "pos.h"                         // PosS
#include "../../h/poollimits.h"        // POOL_SCALE_MAX, g_pools
#if __has_include("build_id.h")
#include "build_id.h"  // the commit and branch, written by tools/build_engine_x64.py
#include "port_startup.h"  // PortStartupEnded
#endif
#ifndef HYPERION_COMMIT
#define HYPERION_COMMIT ""
#define HYPERION_BRANCH ""
#endif

namespace {

bool g_bWanted;
bool g_bRunning;
std::string g_folder;
std::string g_endReason;
LARGE_INTEGER g_start, g_lastShown;
std::vector<float> g_frameMs;          // each shown frame's time since the one before
std::vector<unsigned int> g_frameAt;   // and the game frame it showed
std::vector<PortBenchSecond> g_seconds;
double g_aPartTotal[PERF_PARTS];  // ms a game frame, summed over the seconds, weighted by game frames
double g_dGameFrames;
unsigned int g_dwStartFrame;  // the frame counter when the run's clock started
// the run's speed now, and its steps (PortBenchSpeedStep)
int g_nLiveSpeed = 6, g_nLiveMul = 1;
struct SpeedChange {
  unsigned nFrame;
  int nSpeed, nMul;
};
std::vector<SpeedChange> g_speedChanges;
int g_nPlayersAtStart;  // the players with units at the run's first frame
unsigned int g_nUnitsMost;
unsigned int g_dwUnitsMostAt;  // the game frame of the most units
bool g_bFramesCsv;
// the overlay's (port_benchview.cpp): the shown frames a quarter second at a time, the paused
// time left out of every number, and seconds.csv's last lines
std::vector<PortBenchBucket> g_buckets;
PortBenchBucket g_bucket;
int g_nInBucket;
int g_nSelectedSeconds;  // the seconds logged with units selected
LARGE_INTEGER g_bucketFrom;
bool g_bPaused;
LARGE_INTEGER g_pausedAt;
double g_dPaused;  // seconds paused since the run's start
std::string g_tail[4];
int g_nTail;

double Seconds(const LARGE_INTEGER &from)
{
  LARGE_INTEGER now, freq;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&freq);
  return static_cast<double>(now.QuadPart - from.QuadPart) / static_cast<double>(freq.QuadPart);
}

// the run's own seconds: since its start, less the time it was paused
double RunSeconds(void)
{
  return Seconds(g_start) - g_dPaused - (g_bPaused ? Seconds(g_pausedAt) : 0.0);
}

void ClearRun(void)
{
  g_frameMs.clear();
  g_frameAt.clear();
  g_seconds.clear();
  g_buckets.clear();
  g_nInBucket = 0;
  g_bPaused = false;
  g_dPaused = 0.0;
  g_nTail = 0;
}

void WriteFile(const char *pszName, const std::string &text)
{
  FILE *f = NULL;
  if (fopen_s(&f, (g_folder + "\\" + pszName).c_str(), "wb") != 0 || f == NULL)
    return;
  fwrite(text.data(), 1, text.size(), f);
  fclose(f);
}

// hyperion_bench.log: a run's steps
const PortLogger BenchLog("hyperion_bench.log", PortLogger::SHARED);

const char *const kParts[PERF_PARTS] = {"events", "creep", "ai", "fog", "units", "movers", "triggers",
                                        "check", "minimap", "ui", "draw"};

}  // namespace

const char *PortBenchBuild(void)
{
  return HYPERION_COMMIT[0] ? HYPERION_COMMIT : "this build";
}

const char *PortBenchPartName(int i)
{
  return i >= 0 && i < PERF_PARTS ? kParts[i] : "";
}

void PortBenchWant(void)
{
  g_bWanted = true;
}

bool PortBenchWanted(void)
{
  return g_bWanted || PortCommandOptions()->bBench || PortCommandOptions()->szBenchReplay[0] != '\0';
}

bool PortBenchRunning(void)
{
  return g_bRunning;
}

const char *PortBenchFolder(void)
{
  return g_folder.c_str();
}

bool PortBenchStart(const char *pszName, const char *pszSettingsJson)
{
  SYSTEMTIME t;
  GetLocalTime(&t);
  char szStamp[32];
  snprintf(szStamp, sizeof szStamp, "%04u-%02u-%02u %02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
  const std::string root = std::string(PortDataFolder()) + "benchmarks";
  CreateDirectoryA(root.c_str(), NULL);
  g_folder = root + "\\" + szStamp + " " + pszName;
  // the AI lab's worker: the folder the lab named, made by the lab
  if (PortLabWorkerOut()[0] != '\0') {
    g_folder = PortLabWorkerOut();
    while (!g_folder.empty() && (g_folder.back() == '\\' || g_folder.back() == '/'))
      g_folder.pop_back();
    CreateDirectoryA(g_folder.c_str(), NULL);
  }
  // a second run in the same minute gets a folder of its own
  for (int n = 2; PortLabWorkerOut()[0] == '\0' && !CreateDirectoryA(g_folder.c_str(), NULL); ++n) {
    if (GetLastError() != ERROR_ALREADY_EXISTS || n > 99)
      return false;
    g_folder = root + "\\" + szStamp + " " + pszName + " (" + std::to_string(n) + ")";
  }
  std::string settings = pszSettingsJson;
  // when, and the build, as the object's last members
  size_t close = settings.rfind('}');
  while (close != std::string::npos && close > 0 && (settings[close - 1] == '\r' || settings[close - 1] == '\n'))
    --close;
  char szWhen[40];
  snprintf(szWhen, sizeof szWhen, "%04u-%02u-%02u %02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
  if (close != std::string::npos)
    settings.insert(close, std::string(",\r\n  \"when\": \"") + szWhen + "\",\r\n  \"build\": \"" +
                               HYPERION_VERSION_TEXT + "\",\r\n  \"commit\": \"" + HYPERION_COMMIT +
                               "\",\r\n  \"branch\": \"" + HYPERION_BRANCH + "\",\r\n  \"renderer\": \"" +
                               (PortPresentGpu() ? PortGpuDriver() : "sdl") + "\"");
  WriteFile("settings.json", settings);
  std::string head = "second,frames_per_s,game_frames_per_s,units";
  for (const char *p : kParts)
    head += std::string(",") + p + "_ms";
  // the overlay that second and the units selected, last so a reader of the columns before
  // them reads older runs and newer alike
  head += ",overlay,selected";
  WriteFile("seconds.csv", head + "\r\n");
  ClearRun();
  memset(g_aPartTotal, 0, sizeof g_aPartTotal);
  g_dGameFrames = 0.0;
  g_nUnitsMost = 0;
  g_dwUnitsMostAt = 0;
  g_endReason.clear();
  g_nSelectedSeconds = 0;
  PortPresentPerfFlush(false);  // the seconds count from the run's start
  QueryPerformanceCounter(&g_start);
  g_lastShown = g_start;
  g_bRunning = true;
  return true;
}

void PortBenchShown(void)
{
  if (!g_bRunning || g_bPaused || PortBenchNoPicture())
    return;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);
  const float fMs = static_cast<float>(1000.0 * (now.QuadPart - g_lastShown.QuadPart) / freq.QuadPart);
  g_frameMs.push_back(fMs);
  g_frameAt.push_back(oc_dwReplayFrame);
  g_lastShown = now;
  // the quarter second's mean and worst
  if (g_nInBucket == 0) {
    g_bucket.fMeanMs = 0.0f;
    g_bucket.fMaxMs = 0.0f;
    g_bucketFrom = now;
  }
  ++g_nInBucket;
  g_bucket.fMeanMs += fMs;
  g_bucket.fMaxMs = fMs > g_bucket.fMaxMs ? fMs : g_bucket.fMaxMs;
  if (static_cast<double>(now.QuadPart - g_bucketFrom.QuadPart) / freq.QuadPart >= 0.25) {
    g_bucket.fMeanMs /= g_nInBucket;
    g_bucket.fSeconds = static_cast<float>(RunSeconds());
    g_bucket.nFrame = oc_dwReplayFrame - g_dwStartFrame;
    g_buckets.push_back(g_bucket);
    g_nInBucket = 0;
  }
}

int PortBenchBuckets(const PortBenchBucket **ppBuckets)
{
  *ppBuckets = g_buckets.empty() ? NULL : g_buckets.data();
  return static_cast<int>(g_buckets.size());
}

double PortBenchWorstMs(void)
{
  if (g_frameMs.empty())
    return 0.0;
  std::vector<float> v = g_frameMs;
  const size_t nAt = v.size() * 99 / 100 < v.size() ? v.size() * 99 / 100 : v.size() - 1;
  std::nth_element(v.begin(), v.begin() + nAt, v.end());
  return v[nAt];
}

double PortBenchSeconds(void)
{
  return g_bRunning ? RunSeconds() : 0.0;
}

unsigned int PortBenchGameFrame(void)
{
  return oc_dwReplayFrame - g_dwStartFrame;
}

int PortBenchTail(const char *const **ppLines)
{
  static const char *s_apsz[4];
  for (int i = 0; i < g_nTail; ++i)
    s_apsz[i] = g_tail[i].c_str();
  *ppLines = s_apsz;
  return g_nTail;
}

bool PortBenchPaused(void)
{
  return g_bPaused;
}

void PortBenchPauseSet(bool bPaused)
{
  if (!g_bRunning || bPaused == g_bPaused)
    return;
  if (bPaused) {
    PortPresentPerfFlush(true);  // the part of a second before the pause
    g_nInBucket = 0;
    QueryPerformanceCounter(&g_pausedAt);
    g_bPaused = true;
  } else {
    g_dPaused += Seconds(g_pausedAt);
    g_bPaused = false;
    PortPresentPerfFlush(false);  // what the sampler gathered while paused
    QueryPerformanceCounter(&g_lastShown);
  }
}

void PortBenchAddSecond(const PortBenchSecond &s, double fSeconds)
{
  if (!g_bRunning || g_bPaused)
    return;
  g_seconds.push_back(s);
  const double dFrames = s.dGameFramesPerS * fSeconds;
  for (int i = 0; i < PERF_PARTS; ++i)
    g_aPartTotal[i] += s.adPartMs[i] * dFrames;
  g_dGameFrames += dFrames;
  if (s.nUnits > g_nUnitsMost) {
    g_nUnitsMost = s.nUnits;
    g_dwUnitsMostAt = oc_dwReplayFrame;
  }
  char sz[512];
  int k = snprintf(sz, sizeof sz, "%.1f,%.1f,%.1f,%u", RunSeconds(), s.dFramesPerS, s.dGameFramesPerS, s.nUnits);
  for (int i = 0; i < PERF_PARTS && k > 0 && static_cast<size_t>(k) < sizeof sz; ++i)
    k += snprintf(sz + k, sizeof sz - k, ",%.3f", s.adPartMs[i]);
  const PortBenchOverlay &ov = PortBenchOverlayLive();
  const int nSelected = PortBenchNoPicture() ? 0 : PortBenchSelectedCount();
  g_nSelectedSeconds += nSelected > 0;
  if (k > 0 && static_cast<size_t>(k) < sizeof sz)
    k += snprintf(sz + k, sizeof sz - k, ",%s%s,%d", PortBenchOverlayLayoutId(ov),
                  ov.nLayout == 1 && ov.bLarge ? "-large" : "", nSelected);
  FILE *f = NULL;
  if (fopen_s(&f, (g_folder + "\\seconds.csv").c_str(), "ab") == 0 && f != NULL) {
    fprintf(f, "%s\r\n", sz);
    fclose(f);
  }
  if (g_nTail == 4) {
    for (int i = 0; i < 3; ++i)
      g_tail[i] = g_tail[i + 1];
    --g_nTail;
  }
  g_tail[g_nTail++] = sz;
}

int PortBenchHistory(const PortBenchSecond **ppSeconds)
{
  *ppSeconds = g_seconds.empty() ? NULL : g_seconds.data();
  return static_cast<int>(g_seconds.size());
}

static const char *SpeedId(int nSpeed, int nMul);

void PortBenchEnd(const char *pszReason)
{
  if (!g_bRunning)
    return;
  BenchLog("ended: %s, %s", pszReason, g_folder.c_str());
  PortBenchPauseSet(false);
  PortPresentPerfFlush(true);  // the last part of a second, so a short run has its parts too
  const double dSeconds = RunSeconds();
  g_bRunning = false;
  g_endReason = pszReason;
  // the shown frames' times: the mean, and the worst 1 % as the frame time below which 99 % fall
  double dMeanMs = 0.0, dWorstMs = 0.0;
  if (!g_frameMs.empty()) {
    std::vector<float> sorted = g_frameMs;
    std::sort(sorted.begin(), sorted.end());
    for (float f : sorted)
      dMeanMs += f;
    dMeanMs /= sorted.size();
    const size_t nAt = sorted.size() * 99 / 100;
    dWorstMs = sorted[nAt < sorted.size() ? nAt : sorted.size() - 1];
  }
  // the state the game ended in, as the replay check hashes it: its domains folded into one
  const unsigned long long qEnd = SyncHashFold();
  char sz[256];
  std::string t = "{\r\n";
  snprintf(sz, sizeof sz, "  \"ended\": \"%s\",\r\n  \"seconds\": %.1f,\r\n", pszReason, dSeconds);
  t += sz;
  snprintf(sz, sizeof sz, "  \"frames_shown\": %u,\r\n  \"frames_per_s\": %.1f,\r\n  \"frame_ms_mean\": %.2f,\r\n",
           static_cast<unsigned>(g_frameMs.size()), dSeconds > 0.0 ? g_frameMs.size() / dSeconds : 0.0, dMeanMs);
  t += sz;
  snprintf(sz, sizeof sz, "  \"frame_ms_worst_1pct\": %.2f,\r\n  \"frames_per_s_1pct_low\": %.1f,\r\n", dWorstMs,
           dWorstMs > 0.0 ? 1000.0 / dWorstMs : 0.0);
  t += sz;
  // the game frames by the frame counter; the parts' ms are over the whole seconds logged
  const unsigned nGameFrames = oc_dwReplayFrame - g_dwStartFrame;
  snprintf(sz, sizeof sz, "  \"game_frames\": %u,\r\n  \"game_frames_per_s\": %.1f,\r\n  \"units_most\": %u,\r\n",
           nGameFrames, dSeconds > 0.0 ? nGameFrames / dSeconds : 0.0, g_nUnitsMost);
  t += sz;
  snprintf(sz, sizeof sz, "  \"units_most_at\": %u,\r\n  \"end_frame\": %u,\r\n  \"end_state\": \"%016llX\",\r\n",
           g_dwUnitsMostAt, oc_dwReplayFrame, qEnd);
  t += sz;
  t += "  \"ms_per_game_frame\": {";
  for (int i = 0; i < PERF_PARTS; ++i) {
    snprintf(sz, sizeof sz, "%s\"%s\": %.3f", i ? ", " : "", kParts[i],
             g_dGameFrames > 0.0 ? g_aPartTotal[i] / g_dGameFrames : 0.0);
    t += sz;
  }
  t += "},\r\n";
  // the overlay's changes during the run, O's or the dialog's, each from its game frame; the
  // run compares as the overlay it began with. The seconds with units selected.
  {
    const PortBenchOverlayChange *pChanges = NULL;
    const int nChanges = PortBenchOverlayChanges(&pChanges);
    t += "  \"overlay_changes\": [";
    for (int i = 0; i < nChanges; ++i) {
      const PortBenchOverlay &ov = pChanges[i].overlay;
      snprintf(sz, sizeof sz, "%s{\"game_frame\": %u, \"layout\": \"%s\", \"size\": \"%s\", \"by\": \"%s\"}",
               i ? ", " : "", pChanges[i].nFrame - g_dwStartFrame, PortBenchOverlayLayoutId(ov),
               ov.bLarge ? "large" : "small", pChanges[i].szHow);
      t += sz;
    }
    snprintf(sz, sizeof sz, "],\r\n  \"selected_seconds\": %d,\r\n", g_nSelectedSeconds);
    t += sz;
  }
  // the speed's steps during the run, each from its game frame; the run began at settings.json's
  t += "  \"speed_changes\": [";
  for (size_t i = 0; i < g_speedChanges.size(); ++i) {
    snprintf(sz, sizeof sz, "%s{\"game_frame\": %u, \"speed\": \"%s\"}", i ? ", " : "",
             g_speedChanges[i].nFrame - g_dwStartFrame, SpeedId(g_speedChanges[i].nSpeed, g_speedChanges[i].nMul));
    t += sz;
  }
  t += "],\r\n";
  // each player as the run ended: its computer level (port_ai.h), what it gathered and its units
  int anUnits[PLAYER_SLOTS_MAX] = {};
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
    if (pUnit->bPlayer < PLAYER_SLOTS_MAX)
      ++anUnits[pUnit->bPlayer];
  t += "  \"players\": [";
  bool bFirst = true;
  for (int s = 0; s < PlayerSlots(); ++s) {
    // a person's seat or a computer's, or one gone from the game (port_benchinfo.cpp)
    if (gPlayerData[s].nType != 1 && gPlayerData[s].nType != 2 && !PortBenchWasSeat(s))
      continue;
    SyncSlot slot;
    SyncSlotRead(s, &slot);
    snprintf(sz, sizeof sz,
             "%s\r\n    {\"slot\": %d, \"race\": %d, \"team\": %d, \"level\": \"%s\", \"minerals_gathered\": %d, "
             "\"gas_gathered\": %d, \"units\": %d}",
             bFirst ? "" : ",", s, gPlayerData[s].nRace, gPlayerData[s].nTeam, AiLevelName(AiProfileOf(s).bLevel),
             static_cast<int>(slot.nMineralsGathered), static_cast<int>(slot.nGasGathered), anUnits[s]);
    t += sz;
    bFirst = false;
  }
  t += "\r\n  ]\r\n}\r\n";
  WriteFile("summary.json", t);
  if (g_bFramesCsv) {
    std::string f = "frame_ms,game_frame\r\n";
    for (size_t i = 0; i < g_frameMs.size(); ++i) {
      snprintf(sz, sizeof sz, "%.3f,%u\r\n", g_frameMs[i], i < g_frameAt.size() ? g_frameAt[i] : 0u);
      f += sz;
    }
    WriteFile("frames.csv", f);
  }
}

// The run itself. Its game is made as a skirmish would be, every slot a computer, the local
// player's too, and stops at its first frame; the replay written then is patched (the local
// slot a computer, the run's length, no commands, no checks) and the menus play it
// (PortBenchLaunch), so the viewer is no player, sees the whole map and cannot lose, and
// run.rep plays the same game again. --bench-replay with --replay plays a run's replay again
// as a run of its own; port_bench.h says where a run ends.
namespace {

enum { PHASE_NONE, PHASE_MAKING, PHASE_TO_PLAY, PHASE_PLAYING, PHASE_DONE };
int g_nPhase = PHASE_NONE;
PortBenchSettings g_set;
bool g_bFromMenus;
bool g_bLabGames;  // an AI lab worker's games, one after another (PortBenchLabGame)
DWORD g_dwSpeedBefore;  // the player's game speed (gOptions), which a run plays at Fastest (B40)
bool g_bEnding;  // the made game's end asked for
std::string g_replay;
std::string g_lastRun;  // the last run's folder, which the set-up shows
// A run's name and settings, for Restart's new folder; Restart asked for; where Set-up and
// Runs go back to in the menus (0 the set-up, 1 its runs)
std::string g_runName, g_runSettings;
bool g_bRestart;
int g_nBackTo = 1;

// a run from the menus gives the player's game speed back once it is over: PortLaunchBench set
// Fastest, which a restart's replay plays at again
void SpeedBack()
{
  if (g_bFromMenus)
    gOptions = g_dwSpeedBefore;
}

const DWORD kSlotsAt = 0x14 + 0x8D;  // the header's 12 player records, after the game's header
const DWORD kSlotSize = 36;

const char *const kSpeedIds[7] = {"slowest", "slower", "slow", "normal", "fast", "faster", "fastest"};

// --bench-speed: one of 1.17's seven speeds, a multiple of Fastest (x2 to x16), or max
void ParseSpeed(const char *pszSpeed, int *pnSpeed, int *pnMul)
{
  *pnSpeed = 6;
  *pnMul = 1;
  if (pszSpeed[0] == '\0')
    return;
  for (int i = 0; i < 7; ++i)
    if (_stricmp(pszSpeed, kSpeedIds[i]) == 0) {
      *pnSpeed = i;
      return;
    }
  if (_stricmp(pszSpeed, "max") == 0) {
    *pnMul = 0;
    return;
  }
  const int n = pszSpeed[0] == 'x' ? atoi(pszSpeed + 1) : 1;
  *pnMul = n == 2 || n == 4 || n == 8 || n == 16 ? n : 1;
}

// The command line's run, once, the first time a game asks.
void Begin(void)
{
  static bool s_bDone;
  if (s_bDone)
    return;
  s_bDone = true;
  const PortOptions *o = PortCommandOptions();
  if (!o->bBench && o->szBenchReplay[0] == '\0')
    return;
  g_bWanted = true;
  lstrcpynA(g_set.szMap, o->szMap, sizeof g_set.szMap);
  g_set.nPlayers = o->szPlayers[0] ? atoi(o->szPlayers) : 8;
  lstrcpynA(g_set.szRace, o->szComputerRace[0] ? o->szComputerRace : "random", sizeof g_set.szRace);
  lstrcpynA(g_set.szRaces, o->szBenchRaces, sizeof g_set.szRaces);
  g_set.nSeed = o->szSeed[0] ? static_cast<int>(strtoul(o->szSeed, NULL, 0)) : 1;
  g_set.dwFrames = o->szBenchFrames[0] ? strtoul(o->szBenchFrames, NULL, 10) : 14400;
  if (g_set.dwFrames == 0)
    g_set.dwFrames = 14400;
  ParseSpeed(o->szBenchSpeed, &g_set.nSpeed, &g_set.nSpeedMul);
  g_set.bFramesCsv = o->bBenchFrames;
  g_set.nShown = _stricmp(o->szBenchShown, "full") == 0 ? 1 : _stricmp(o->szBenchShown, "none") == 0 ? 2 : 0;
  PortBenchOverlayFromOptions(o, &g_set.overlay);
  g_set.nFramesShown = _stricmp(o->szBenchFramesShown, "display") == 0 ? 1 : 0;
  g_set.nPicture = strcmp(o->szBenchPicture, "256") == 0 ? 1 : 0;
  lstrcpynA(g_set.szAgainst, o->szBenchAgainst, sizeof g_set.szAgainst);
  g_set.nGameType = _stricmp(o->szBenchType, "melee") == 0 ? 1 : 0;
  lstrcpynA(g_set.szTeams, o->szTeams, sizeof g_set.szTeams);
  lstrcpynA(g_set.szStarts, o->szStarts, sizeof g_set.szStarts);
  lstrcpynA(g_set.szAiLevels, o->szAiLevels, sizeof g_set.szAiLevels);
  g_set.nArmy = o->szBenchArmy[0] ? atoi(o->szBenchArmy) : 0;
  g_set.nPools = o->szBenchPools[0] ? atoi(o->szBenchPools) : 0;
  lstrcpynA(g_set.szMutators, o->szMutators, sizeof g_set.szMutators);
  g_bFramesCsv = g_set.bFramesCsv;
  g_nPhase = o->bBench ? PHASE_MAKING : PHASE_PLAYING;
  if (g_nPhase == PHASE_PLAYING) {
    g_folder = o->szBenchReplay;
    g_replay = o->szReplay;
    BenchLog("playing %s again", g_replay.c_str());
  } else {
    BenchLog("making the game on %s", g_set.szMap);
  }
}

}  // namespace

const char *PortBenchShownId(const PortBenchSettings &s)
{
  return s.nShown == 1 ? "full" : s.nShown == 2 ? "none" : "window";
}

bool PortBenchNoPicture(void)
{
  return g_nPhase == PHASE_PLAYING && g_set.nShown == 2;
}

bool PortBenchPictureDue(void)
{
  static Uint64 s_nAt;
  const Uint64 nNow = SDL_GetTicks();
  if (nNow - s_nAt < 1000)
    return false;
  s_nAt = nNow;
  return true;
}

static const char *SpeedId(int nSpeed, int nMul)
{
  return nMul == 0    ? "max"
         : nMul == 1  ? kSpeedIds[nSpeed >= 0 && nSpeed < 7 ? nSpeed : 6]
         : nMul == 2  ? "x2"
         : nMul == 4  ? "x4"
         : nMul == 8  ? "x8"
                      : "x16";
}

const char *PortBenchSpeedId(const PortBenchSettings &s)
{
  return SpeedId(s.nSpeed, s.nSpeedMul);
}

static bool MakeRun(const PortBenchSettings &s, bool bLab)
{
  Begin();
  if (g_nPhase == PHASE_MAKING || g_nPhase == PHASE_TO_PLAY || g_nPhase == PHASE_PLAYING)
    return false;
  g_set = s;
  if (g_set.dwFrames == 0)
    g_set.dwFrames = 14400;
  g_bFramesCsv = g_set.bFramesCsv;
  g_bFromMenus = !bLab;
  g_bLabGames = bLab;
  g_bWanted = true;
  g_bEnding = false;
  g_endReason.clear();
  g_nPhase = PHASE_MAKING;
  BenchLog("making the game on %s, %s", g_set.szMap, bLab ? "the AI lab's" : "from the menus");
  g_dwSpeedBefore = gOptions;
  char szPlayers[16];
  snprintf(szPlayers, sizeof szPlayers, "%d", g_set.nPlayers);
  // each player's colour (the teams' shades), a row each as the set-up counts them
  int nRow = 0;
  for (const char *p = g_set.szColours; *p == '#' && nRow < 64; ++nRow) {
    const unsigned long n = strtoul(p + 1, NULL, 16);
    PlayersChooseColourRgb(nRow, static_cast<unsigned char>(n >> 16), static_cast<unsigned char>(n >> 8),
                           static_cast<unsigned char>(n));
    p = strchr(p, ',');
    if (p == NULL)
      break;
    ++p;
  }
  if (!PortLaunchBench(g_set.szMap, szPlayers, g_set.szRace, g_set.szRaces, g_set.szTeams, g_set.szStarts,
                       g_set.szAiLevels, g_set.nGameType == 1 ? "melee" : "ffa", g_set.szMutators)) {
    BenchLog("the game on %s did not start", g_set.szMap);
    SpeedBack();
    SessionClear();  // the colours chosen above (port_session.h)
    g_nPhase = PHASE_DONE;
    g_bWanted = false;
    return false;
  }
  return true;
}

bool PortBenchFromMenus(const PortBenchSettings &s)
{
  return MakeRun(s, false);
}

bool PortBenchLabGame(const PortBenchSettings &s)
{
  return MakeRun(s, true);
}

const PortBenchSettings *PortBenchCurrent(void)
{
  Begin();
  return &g_set;
}

const char *PortBenchLastRun(void)
{
  return g_lastRun.c_str();
}

bool PortBenchMaking(void)
{
  Begin();
  return g_nPhase == PHASE_MAKING;
}

bool PortBenchPlaying(void)
{
  Begin();
  return g_nPhase == PHASE_PLAYING && gPlayback;
}

int PortBenchSeed(void)
{
  Begin();
  return g_set.nSeed;
}

bool PortBenchFast(void)
{
  return PortBenchPlaying() && g_nLiveMul == 0;
}

void PortBenchSpeedNow(int *pnSpeed, int *pnMul)
{
  *pnSpeed = g_nLiveSpeed;
  *pnMul = g_nLiveMul;
}

// the speeds a step goes through: 1.17's seven, then Fastest's multiples, then flat out
static int SpeedRung(int nSpeed, int nMul)
{
  return nMul == 0 ? 11 : nMul == 1 ? SDL_clamp(nSpeed, 0, 6) : nMul == 2 ? 7 : nMul == 4 ? 8 : nMul == 8 ? 9 : 10;
}

static void BenchPresent(bool bRun);

void PortBenchSpeedStep(int nDelta)
{
  if (!PortBenchPlaying())
    return;
  const int nFrom = SpeedRung(g_nLiveSpeed, g_nLiveMul), nTo = SDL_clamp(nFrom + nDelta, 0, 11);
  if (nTo == nFrom)
    return;
  g_nLiveSpeed = nTo < 7 ? nTo : 6;
  g_nLiveMul = nTo < 7 ? 1 : nTo == 11 ? 0 : 1 << (nTo - 6);
  PortBenchReplayStarts();
  BenchPresent(true);  // flat out draws less often
  if (g_bRunning) {
    const SpeedChange c = {oc_dwReplayFrame, g_nLiveSpeed, g_nLiveMul};
    g_speedChanges.push_back(c);
  }
  BenchLog("speed %s at game frame %u", SpeedId(g_nLiveSpeed, g_nLiveMul), oc_dwReplayFrame - g_dwStartFrame);
}

// --- a stress run ---

static PortBenchStress g_stressRecorded;
// the armies' units as they were made, for the line a minute in (StressReport)
struct StressMade {
  CUnit *pUnit;
  int nId;
  int x, y;    // where its army was made
  int ux, uy;  // where the unit was made
  int nArmy;   // its army's place in g_stressSlots
};
static std::vector<StressMade> g_stressMade;
static std::vector<int> g_stressSlots;  // the computers given an army, in slot order

PortBenchStress PortBenchStressGame(void)
{
  if (gPlayback)
    return (g_hxRuleOptions & HX_OPT_STRESS) != 0 ? g_stressRecorded : PortBenchStress();
  PortBenchStress s;
  if (PortBenchMaking()) {
    s.nArmy = g_set.nArmy > 0 ? g_set.nArmy : 0;
    s.nPools = g_set.nPools > 0 ? (g_set.nPools < POOL_SCALE_MAX ? g_set.nPools : POOL_SCALE_MAX) : 0;
  }
  return s;
}

bool PortBenchStressChosen(void)
{
  const PortBenchStress s = PortBenchStressGame();
  return !gPlayback && (s.nArmy > 0 || s.nPools > 0);
}

void PortBenchStressRecorded(const PortBenchStress *pStress)
{
  g_stressRecorded = pStress != NULL ? *pStress : PortBenchStress();
}

// Where a computer's army is made: on the ground between its start and the start it attacks,
// at the middle or back toward its own start a step at a time until the point is in its
// start's area, so the army can walk to its target; its own start when none is. Made at the
// start itself, Create Unit's search filled the mineral line and the gaps round the town hall,
// and away from the minerals it was a pocket behind the base on Big Game Hunters; 125 units in
// one blob there were boxed in.
static void StressArmyAt(int x, int y, int tx, int ty, int *pnX, int *pnY)
{
  const WORD wArea = GetAreaIndex(GetRegion_xy(x, y));
  *pnX = x;
  *pnY = y;
  for (int k = 8; k >= 2; --k) {
    const int px = x + (tx - x) * k / 16, py = y + (ty - y) * k / 16;
    if (px < 16 || py < 16 || px >= gwMapWidth * 32 - 16 || py >= gwMapHeight * 32 - 16)
      continue;
    if (GetAreaIndex(GetRegion_xy(px, py)) == wArea) {
      *pnX = px;
      *pnY = py;
      return;
    }
  }
}

// The armies, at the first frame of a stress run's replay, when the frame's triggers have made
// the start units: each computer's round a point between its base and its target
// (StressArmyAt), a unit to a point of a grid a tile and a half apart, going round the point
// outward, on the points in the start's area, the types in turn in the table's shares; so
// every playing of the replay makes the same units in the same places. Made all at one point,
// Create Unit packed them into a block with no room between them, and a unit inside it found
// no path out, gave its order up and stood there. Each unit is then taken from its computer's
// AI, as the rescue switch takes a nation's (SAI_RemoveUnit): the AI kept them at home, and
// refused a far target to them, wanting a transport, where its areas did not join on a map of
// 1,024 tiles (ComputerTransport). Each army then attacks toward the next computer's start, as
// the trigger action Order does, so the armies meet.
static void StressArmies(void)
{
  struct Part {
    WORD wType;
    int nShare;
  };
  static const Part kMix[] = {
      {0, 3},   // marines
      {37, 4},  // zerglings
      {65, 2},  // zealots
      {38, 2},  // hydralisks
      {66, 1},  // dragoons
      {3, 1},   // goliaths
      {43, 1},  // mutalisks
      {8, 1},   // wraiths
  };
  const int nArmy = PortBenchStressGame().nArmy;
  std::vector<WORD> types;  // the army's units in the order they are made
  while (static_cast<int>(types.size()) < nArmy)
    for (const Part &part : kMix)
      for (int k = 0; k < part.nShare && static_cast<int>(types.size()) < nArmy; ++k)
        types.push_back(part.wType);
  // the grid's points, ring by ring from the middle
  enum { STEP = 48, RINGS = 20 };
  std::vector<std::pair<int, int>> points;
  points.push_back({0, 0});
  for (int r = 1; r <= RINGS; ++r) {
    for (int k = -r; k < r; ++k)
      points.push_back({k, -r});
    for (int k = -r; k < r; ++k)
      points.push_back({r, k});
    for (int k = r; k > -r; --k)
      points.push_back({k, r});
    for (int k = r; k > -r; --k)
      points.push_back({-r, k});
  }
  g_stressMade.clear();
  g_stressSlots.clear();
  int nBefore = 0;
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
    ++nBefore;
  std::vector<char> abOld(g_pools.units);  // the units there before an army
  for (int i = 0; i < PlayerLobbySlots(); ++i) {
    if (gPlayerData[i].nType != 1)
      continue;
    int nNext = i;
    do
      nNext = (nNext + 1) % PlayerLobbySlots();
    while (gPlayerData[nNext].nType != 1 && nNext != i);
    std::fill(abOld.begin(), abOld.end(), 0);
    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
      abOld[pUnit - CUnit::Head()] = 1;
    int x, y;
    StressArmyAt(gStartLocations[i].x, gStartLocations[i].y, gStartLocations[nNext].x, gStartLocations[nNext].y, &x,
                 &y);
    g_stressSlots.push_back(i);
    const WORD wArea = GetAreaIndex(GetRegion_xy(gStartLocations[i].x, gStartLocations[i].y));
    size_t nMade = 0;
    for (size_t k = 0; k < points.size() && nMade < types.size(); ++k) {
      const int px = x + points[k].first * STEP, py = y + points[k].second * STEP;
      if (px < 16 || py < 16 || px >= gwMapWidth * 32 - 16 || py >= gwMapHeight * 32 - 16)
        continue;
      if (GetAreaIndex(GetRegion_xy(px, py)) != wArea)
        continue;
      PortTrigCreate(types[nMade++], px, py, i, 1, NULL, false);
    }
    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
      if (!abOld[pUnit - CUnit::Head()]) {
        SAI_RemoveUnit(pUnit, 0);  // the computer's still, but not its AI's
        PortTrigOrderOne(pUnit, gStartLocations[nNext].x, gStartLocations[nNext].y, PORT_TRIG_ATTACK);
        g_stressMade.push_back({pUnit, pUnit->GetUnitID(), x, y, PosS(pUnit->pSprite->wPosX),
                                static_cast<int>(pUnit->pSprite->wPosY),
                                static_cast<int>(g_stressSlots.size()) - 1});
      }
  }
  int nAfter = 0;
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
    ++nAfter;
  BenchLog("stress: %d units asked of each computer, %d made, %d units in all (pools %d)", nArmy,
           nAfter - nBefore, nAfter, g_pools.units);
}

// A second into the run and every ten seconds after: each army's units taken from the AI again,
// and those that stand idle sent to attack toward another computer's start, the next one on
// at each wave, so the armies keep moving and meeting. A unit already fighting or on its way
// is left to it.
static void StressWave(int nWave)
{
  const int nArmies = static_cast<int>(g_stressSlots.size());
  if (nArmies < 2)
    return;
  for (const StressMade &m : g_stressMade) {
    if (m.pUnit->IsDeadOrDying() || m.pUnit->GetUnitID() != m.nId)
      continue;
    // the AI takes a unit in again once it counts it as made; out of it, an attack its AI
    // refused goes on at the unit's next step
    if (m.pUnit->dwUnion134 != 0)
      SAI_RemoveUnit(m.pUnit, 0);
    const order_id_t bOrder = m.pUnit->bOrderId;
    if (bOrder != 0x01 && bOrder != 0x02 && bOrder != 0x03 && bOrder != 0x9C && bOrder != 0xA0)
      continue;  // Stop, Guard, PlayerGuard, ComputerAI, GuardPost: idle
    int nTarget = (m.nArmy + 1 + nWave % (nArmies - 1)) % nArmies;
    const int nSlot = g_stressSlots[nTarget];
    PortTrigOrderOne(m.pUnit, gStartLocations[nSlot].x, gStartLocations[nSlot].y, PORT_TRIG_ATTACK);
  }
}

// A minute in and five: of the armies' units still alive, how many stand within twelve tiles
// of where their army was made, and how many within a tile of where each was made, which a
// unit boxed in would.
static void StressReport(void)
{
  int nAlive = 0, nNear = 0, nUnmoved = 0;
  for (const StressMade &m : g_stressMade) {
    if (m.pUnit->IsDeadOrDying() || m.pUnit->GetUnitID() != m.nId)
      continue;
    ++nAlive;
    const int ux = PosS(m.pUnit->pSprite->wPosX), uy = static_cast<int>(m.pUnit->pSprite->wPosY);
    nNear += ux - m.x >= -384 && ux - m.x <= 384 && uy - m.y >= -384 && uy - m.y <= 384;
    nUnmoved += ux - m.ux >= -32 && ux - m.ux <= 32 && uy - m.uy >= -32 && uy - m.uy <= 32;
  }
  BenchLog("stress: at frame %lu, %d of the %d units made alive, %d within 12 tiles of where their army was made, "
           "%d within a tile of where each was made",
           static_cast<ULONG>(oc_dwReplayFrame), nAlive, static_cast<int>(g_stressMade.size()), nNear, nUnmoved);
}

void PortBenchPatchLocal(void *pHeader, unsigned cbHeader, DWORD dwLength)
{
  BYTE *p = static_cast<BYTE *>(pHeader);
  if (cbHeader < kSlotsAt + 12 * kSlotSize || g_LocalNationID >= 12)
    return;
  memcpy(p + 1, &dwLength, 4);
  BYTE *pSlot = p + kSlotsAt + g_LocalNationID * kSlotSize;
  const DWORD dwNoStorm = 0xFFFFFFFFu;
  memcpy(pSlot + 4, &dwNoStorm, 4);  // a computer's: no network id
  pSlot[8] = 1;                      // a computer
}

void PortBenchPatchHeader(void *pHeader, unsigned cbHeader)
{
  // the replay's length: past the run's end, so that 1.17's end of a replay (its pause and
  // dialog) never comes before it
  PortBenchPatchLocal(pHeader, cbHeader, g_set.dwFrames + 48);
}

// The sides left (port_teams.h), counted every 24 game frames of a run so every build ends a
// run on the same frame.
static int PlayersLeft(void)
{
  return TeamsSidesLeft();
}

// A run's end, at its length or with one player left: from the menus the game holds at its last
// frame under the run's summary (port_benchview.cpp); from the command line the program ends.
static void RunEnds(const char *pszReason)
{
  PortBenchEnd(pszReason);
  if (g_bFromMenus)
    pause_game();
  else
    gameloop_init(4);
}

void PortBenchFrame(void)
{
  // a stress run's armies, in the replay that is the run: the game made first stops here, and
  // its local slot is still a person's
  if (gPlayback && oc_dwReplayFrame == 1 && PortBenchStressGame().nArmy > 0)
    StressArmies();
  if (gPlayback && PortBenchStressGame().nArmy > 0 && (oc_dwReplayFrame == 24 || oc_dwReplayFrame % 240 == 0))
    StressWave(static_cast<int>(oc_dwReplayFrame / 240));
  if (gPlayback && (oc_dwReplayFrame == 1440 || oc_dwReplayFrame == 7200) && PortBenchStressGame().nArmy > 0)
    StressReport();
  if (PortBenchMaking() && !gPlayback && oc_dwReplayFrame >= 1) {
    if (!g_bEnding) {
      g_bEnding = true;
      gameloop_init(4);  // to the menus, as Quit Mission leaves; free_game writes the replay
    }
    return;
  }
  // the run's clock starts with its first game frame, past the loading
  if (PortBenchPlaying() && !PortBenchRunning() && g_endReason.empty()) {
    QueryPerformanceCounter(&g_start);
    g_lastShown = g_start;
    ClearRun();
    g_dwStartFrame = oc_dwReplayFrame;
    g_bRunning = true;
    g_nPlayersAtStart = PlayersLeft();
    PortBenchOverlayBegin(g_set.overlay);  // the overlay the run begins with, which O changes
    // the run's speed again: the game's start set 1.17's Fast after PortBenchReplayStarts; a
    // restart's is the set-up's, whatever the last run was stepped to
    g_nLiveSpeed = g_set.nSpeed;
    g_nLiveMul = g_set.nSpeedMul;
    g_speedChanges.clear();
    PortBenchReplayStarts();
    BenchPresent(true);
  }
  if (!PortBenchPlaying() || !PortBenchRunning())
    return;
  PortBenchPlayersNote();  // the seats, so one gone is still listed (port_benchinfo.cpp)
  if (oc_dwReplayFrame >= g_set.dwFrames)
    RunEnds("finished");
  else if (g_nPlayersAtStart >= 2 && (oc_dwReplayFrame - g_dwStartFrame) % 24 == 0 && PlayersLeft() <= 1)
    RunEnds("last");
}

const char *PortBenchEndReason(void)
{
  return g_endReason.c_str();
}

void PortBenchStop(void)
{
  if (!PortBenchRunning())
    return;
  PortBenchEnd("stopped");
  pause_game();
}

void PortBenchRestart(void)
{
  if (!PortBenchPlaying() || g_runSettings.empty())
    return;
  PortBenchEnd("stopped");  // nothing if it had ended
  g_bRestart = true;
  unpause_game(0);
  gameloop_init(4);
}

void PortBenchLeave(int nBackTo)
{
  if (!PortBenchPlaying())
    return;
  g_nBackTo = nBackTo;
  PortBenchEnd("stopped");
  unpause_game(0);
  gameloop_init(4);
}

int PortBenchBackTo(void)
{
  return g_nBackTo;
}

bool PortBenchFromMenusRun(void)
{
  return g_bFromMenus;
}

static void BenchPresent(bool bRun);

void PortBenchAfterGame(void)
{
  if (g_nPhase == PHASE_MAKING) {
    // the run's name: the map's, from its file, and the players
    const char *pszMap = strrchr(g_set.szMap, '\\');
    pszMap = pszMap != NULL ? pszMap + 1 : g_set.szMap;
    std::string title;
    for (const char *p = pszMap; *p && *p != '.'; ++p)
      title += strchr("<>:\"/\\|?*", *p) != NULL ? '_' : *p;
    const std::string name = title + ", " + std::to_string(g_set.nPlayers) + " players";
    char sz[200];
    std::string settings = "{\r\n  \"map\": " + PortJsonQuote(g_set.szMap) + ",\r\n  \"title\": " + PortJsonQuote(title.c_str()) +
                           ",\r\n";
    snprintf(sz, sizeof sz, "  \"players\": %d,\r\n", g_set.nPlayers);
    settings += sz;
    settings += "  \"race\": " + PortJsonQuote(g_set.szRace[0] ? g_set.szRace : "random") + ",\r\n";
    if (g_set.szRaces[0])
      settings += "  \"races\": " + PortJsonQuote(g_set.szRaces) + ",\r\n";
    // the computers' levels (port_ai.h): "Classic" when every one is
    settings += "  \"level\": " + PortJsonQuote(AiLevelsLabel(g_set.szAiLevels).empty() ? "Classic" : AiLevelsLabel(g_set.szAiLevels).c_str()) + ",\r\n";
    if (g_set.szAiLevels[0])
      settings += "  \"ai_levels\": " + PortJsonQuote(g_set.szAiLevels) + ",\r\n";
    settings += g_set.nGameType == 1 ? "  \"game_type\": \"melee\",\r\n" : "  \"game_type\": \"ffa\",\r\n";
    if (g_set.szTeams[0])
      settings += "  \"teams\": " + PortJsonQuote(g_set.szTeams) + ",\r\n";
    if (g_set.szStarts[0])
      settings += "  \"starts\": " + PortJsonQuote(g_set.szStarts) + ",\r\n";
    if (g_set.szLayout[0])
      settings += "  \"layout\": " + PortJsonQuote(g_set.szLayout) + ",\r\n";
    if (g_set.szColours[0])
      settings += "  \"colours\": " + PortJsonQuote(g_set.szColours) + ",\r\n";
    if (PortBenchStressChosen()) {
      snprintf(sz, sizeof sz, "  \"army\": %d,\r\n  \"pools\": %d,\r\n", PortBenchStressGame().nArmy,
               PortBenchStressGame().nPools);
      settings += sz;
    }
    // the game's mutators, as a room says them (port_mutators.h), and the unit table it had
    if (!MutatorsStandard(MutatorsOfGame()))
      settings += "  \"mutators\": " + MutatorsJson(MutatorsOfGame()) + ",\r\n";
    snprintf(sz, sizeof sz, "  \"unit_table\": %d,\r\n", g_pools.units);
    settings += sz;
    snprintf(sz, sizeof sz, "  \"seed\": %d,\r\n  \"frames\": %lu,\r\n  \"speed\": \"%s\",\r\n", g_set.nSeed,
             static_cast<ULONG>(g_set.dwFrames), PortBenchSpeedId(g_set));
    settings += sz;
    // the screen and its overlay (port_benchoverlay.cpp), the one the run begins with
    {
      const int nStyle = PortBenchStyleOf(g_set.nShown, g_set.overlay);
      settings += std::string("  \"screen\": \"") + (nStyle >= 0 ? PortBenchStyleId(nStyle) : "own") + "\",\r\n";
      settings += "  \"overlay\": " + PortBenchOverlayJson(g_set.nShown, g_set.overlay) + ",\r\n";
    }
    snprintf(sz, sizeof sz,
             "  \"shown\": \"%s\",\r\n  \"frames_shown\": \"%s\",\r\n  \"picture\": \"%s\",\r\n"
             "  \"frames_csv\": %s\r\n}\r\n",
             PortBenchShownId(g_set), g_set.nFramesShown == 1 ? "display" : "free", g_set.nPicture == 1 ? "256" : "gpu",
             g_set.bFramesCsv ? "true" : "false");
    settings += sz;
    g_nPhase = PHASE_DONE;
    g_runName = name;
    g_runSettings = settings;
    g_nBackTo = 1;
    if (!PortBenchStart(name.c_str(), settings.c_str())) {
      SpeedBack();
      return;
    }
    g_bRunning = false;  // the clock starts with the replay's game
    char szReplay[MAX_PATH];
    snprintf(szReplay, sizeof szReplay, "%smaps\\replays\\LastReplay.rep", PortDataFolder());  // where write_replay puts it
    g_replay = g_folder + "\\run.rep";
    if (!CopyFileA(szReplay, g_replay.c_str(), FALSE)) {
      BenchLog("could not copy the replay to %s", g_replay.c_str());
      SpeedBack();
      return;
    }
    BenchLog("replay written to %s", g_replay.c_str());
    g_nPhase = PHASE_TO_PLAY;
    return;
  }
  if (g_nPhase == PHASE_PLAYING) {
    PortBenchEnd("stopped");  // nothing if it finished
    g_nPhase = PHASE_DONE;
    g_lastRun = g_folder;
    BenchPresent(false);
    // Restart: the same game again in a folder of its own, its replay copied there
    if (g_bRestart) {
      g_bRestart = false;
      const std::string from = g_replay;
      if (PortBenchStart(g_runName.c_str(), g_runSettings.c_str())) {
        g_bRunning = false;  // the clock starts with the replay's game
        g_replay = g_folder + "\\run.rep";
        if (CopyFileA(from.c_str(), g_replay.c_str(), FALSE)) {
          BenchLog("restarted in %s", g_folder.c_str());
          g_nPhase = PHASE_TO_PLAY;
          return;
        }
        BenchLog("could not copy the replay to %s", g_replay.c_str());
      }
    }
    SpeedBack();
    // from the menus, back to them; an AI lab's worker, on to its next game (PortLabWorkerNext)
    if (g_bFromMenus || g_bLabGames) {
      g_bWanted = false;
      return;
    }
    if (GetEnvironmentVariableA("HYPERION_DLLS", NULL, 0) == 0) {
      PortStartupEnded(0);
      ExitProcess(0);
    }
  }
}

// The run's presentation, as it starts to play: the frames shown (no limit and no vertical sync,
// or the display's refresh with it) and the picture (the GPU's or 1.17's 256 colours), neither
// saved; put back from Settings when it ends.
static void BenchPresent(bool bRun)
{
  if (!PortPresentGpu())
    return;
  double dRate = 2.0, dVsync = 1.0;
  PortSettingNumber("hyperion", "frameRate", &dRate);
  PortSettingNumber("hyperion", "vsync", &dVsync);
  if (bRun) {
    // as many as it can, but at the most a game can go 60 a second, so the frames shown do not
    // take the time the game frames are measured by (the set-up says so)
    PortFrameRateSet(g_set.nFramesShown == 1 ? 2 : g_nLiveMul == 0 ? 1 : 5);
    PortGpuVsyncSet(g_set.nFramesShown == 1);
    PortSceneSoftware(g_set.nPicture == 1);
  } else {
    PortFrameRateSet(static_cast<int>(dRate));
    PortGpuVsyncSet(dVsync != 0.0);
    PortSceneSoftware(false);
  }
}

int PortBenchLaunch(void)
{
  Begin();
  if (g_nPhase != PHASE_TO_PLAY)
    return FALSE;
  g_nPhase = PHASE_PLAYING;
  g_nLiveSpeed = g_set.nSpeed;
  g_nLiveMul = g_set.nSpeedMul;
  BenchPresent(true);
  g_endReason.clear();
  if (!PortLaunchBenchReplay(g_replay.c_str())) {
    BenchLog("the replay %s did not start", g_replay.c_str());
    SpeedBack();
    g_nPhase = PHASE_DONE;
    g_bWanted = !g_bFromMenus;
    return FALSE;
  }
  BenchLog("playing %s", g_replay.c_str());
  return TRUE;
}

void PortBenchReplayStarts(void)
{
  if (!PortBenchPlaying())
    return;
  sgbShowAll_g = 1;  // the replay panel's Show All
  // the replay's speed and multiple, the run's now; its game was made at Fastest
  replay_set_speed(g_nLiveMul == 1 ? g_nLiveSpeed : 6, g_nLiveMul == 0 ? 16 : g_nLiveMul, 0);
}
