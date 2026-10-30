// port_labworker.cpp -- a worker of the AI lab's farm (port_labworker.h). The worker
// is the benchmark's run (port_bench.cpp) with a connection to the lab: the game is made, its
// replay played with nothing drawn as fast as it goes, and it ends at its length or with one
// player left. Its folder is the one the lab names (--lab-out).
//
// Every call here reads the game and writes none of it, so a worker's game is the benchmark's
// and its replay plays it again exactly.
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#if defined(__linux__)
#include <signal.h>
#include <sys/prctl.h>
#elif defined(__APPLE__)
#include <errno.h>
#include <pthread.h>
#include <sys/event.h>
#include <unistd.h>
#endif
#include "port_labworker.h"
#include "port_labwatch.h"
#include "port_labproto.h"
#include "../lan/port_lannet.h"
#include "../lan/port_lanproto.h"
#include "../platform/port_paths.h"
#include "../platform/port_log.h"
#include "../tools/port_bench.h"
#include "../tools/port_benchinfo.h"
#include "../gameplay/port_ai.h"
#include "../../h/port_synchash.h"
#include "../../h/playerlimits.h"
#include "../../h/typelimits.h"
#include "CUnit.h"
#include "net_cmds.h"
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"
#include "../../../hyperion/version.h"
#include "../tools/port_startup.h"  // PortStartupEnded

// gOptions[3], the music device on, as music.cpp declares it: a worker's is off
extern int gOptions_music_on;

namespace {

PortLogger g_log("hyperion_lab.log");

bool g_bWorker;
bool g_bJobs;              // its games from the lab's JOBs, one after another, not --bench's one
bool g_bJob, g_bGiven;     // a JOB came and is not yet started; one was started, not yet ended
PortBenchSettings g_job;
std::string g_out;         // the game's folder, from its JOB
int g_nConn = -1;
uint32_t g_dwWorker;
uint32_t g_dwGame;
bool g_bStarted, g_bEnded;
int g_nRate = 30;          // samples a second of the clock (SAMPLING)
int g_nMostUnits = 6000;   // units a sample at the most
Uint64 g_nSampleAt, g_nStatusAt, g_nPumpAt;
unsigned g_dwStatusFrame;
double g_dSeconds;         // the run's seconds, kept each frame: its clock stops as it ends
int g_nPace;               // the lab's pace (PACE): a multiple of Fastest, 0 for the most
bool g_bPaused;
Uint64 g_qPaceAt;          // the clock (ns) and the game's frame the pace counts from
unsigned g_dwPaceFrame;
bool g_bWatched;           // watched live in the lab (WATCHED): played up to g_dwAllowed
unsigned g_dwAllowed;
const Uint64 kFastestNs = 42000000;  // a frame at Fastest, 1.17's 42 ms
const unsigned kHashEvery = 240;     // a watched game's state sent every so many frames
const size_t kBacklogMost = 1u << 20;  // a sample waits when this much is still to go

// units.dat's flags (gulUnitTypeFlags), as port_benchinfo.cpp reads them
enum { TYPE_BUILDING = 1, TYPE_FLYER = 4, TYPE_WORKER = 8, TYPE_RESOURCES = 0x2000 };

void Send(const LanProto::Writer &w, uint8_t bKind)
{
  LanNet::Send(g_nConn, bKind, w.v.data(), w.v.size());
}

// The lab closes the connection when it has read the end (port_labfarm.cpp): until then the
// program does not end, since a connection ended with its process may lose what it last sent.
void WaitClosed(int nMs)
{
  const Uint64 nUntil = SDL_GetTicks() + static_cast<Uint64>(nMs);
  while (LanNet::Alive(g_nConn) && SDL_GetTicks() < nUntil) {
    LanNet::Pump();
    LanNet::Wait(10);
  }
}

// What waits to go, sent, for at most nMs: the end, before the program does.
void Drain(int nMs)
{
  const Uint64 nUntil = SDL_GetTicks() + static_cast<Uint64>(nMs);
  for (;;) {
    LanNet::Pump();
    if (LanNet::Pending(g_nConn) == 0 || !LanNet::Alive(g_nConn) || SDL_GetTicks() >= nUntil)
      return;
    LanNet::Wait(10);
  }
}

bool InGame(int s)
{
  return gPlayerData[s].nType == 1 || gPlayerData[s].nType == 2 || PortBenchWasSeat(s);
}

void GameStart(void)
{
  const PortBenchSettings *pSet = PortBenchCurrent();
  // the pace counts from the game's first frame (a game before this one left it on its own)
  g_qPaceAt = SDL_GetTicksNS();
  g_dwPaceFrame = oc_dwReplayFrame;
  LanProto::Writer w;
  w.D(g_dwGame);
  w.S(pSet->szMap);
  w.D(static_cast<uint32_t>(pSet->nSeed));
  w.D(pSet->dwFrames);
  w.B(static_cast<uint8_t>(pSet->nSpeed));
  w.B(static_cast<uint8_t>(pSet->nSpeedMul));
  w.W(gwMapWidth);
  w.W(gwMapHeight);
  std::vector<int> slots;
  for (int s = 0; s < PlayerSlots(); ++s)
    if (InGame(s))
      slots.push_back(s);
  w.B(static_cast<uint8_t>(slots.size()));
  for (int s : slots) {
    w.B(static_cast<uint8_t>(s));
    w.B(gPlayerData[s].nRace);
    w.B(gPlayerData[s].nTeam);
    w.B(AiProfileOf(s).bLevel);
    char szName[sizeof gPlayerData[s].szName + 1] = {};
    memcpy(szName, gPlayerData[s].szName, sizeof gPlayerData[s].szName);
    w.S(szName);
  }
  Send(w, LabProto::GAME_START);
  g_log("game %u started: %s, seed %d, %u players", g_dwGame, pSet->szMap, pSet->nSeed,
             static_cast<unsigned>(slots.size()));
}

void Sample(void)
{
  LanProto::Writer w;
  w.D(g_dwGame);
  w.D(oc_dwReplayFrame);
  PortBenchPlayer aPlayers[64];
  const int nPlayers = PortBenchPlayersRead(aPlayers, 64);
  w.B(static_cast<uint8_t>(nPlayers));
  for (int i = 0; i < nPlayers; ++i) {
    const PortBenchPlayer &p = aPlayers[i];
    w.B(static_cast<uint8_t>(p.nSlot));
    w.D(static_cast<uint32_t>(p.nMinerals));
    w.D(static_cast<uint32_t>(p.nGas));
    w.W(static_cast<uint16_t>(p.nSupplyUsed));
    w.W(static_cast<uint16_t>(p.nSupplyMax));
    w.W(static_cast<uint16_t>(p.nUnits));
    w.B(p.bLeft ? 1 : 0);
  }
  // the units, counted in when written: the neutral player's (11) are the map's, which the
  // lab draws from the map itself
  const size_t nCountAt = w.v.size();
  w.W(0);
  int nUnits = 0;
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL && nUnits < g_nMostUnits; pUnit = pUnit->pNext) {
    if (pUnit->bPlayer == 11)
      continue;
    const unsigned nType = pUnit->wType;
    const unsigned long dwFlags = UnitTypeValid(nType) ? gulUnitTypeFlags[nType] : 0;
    uint8_t bKind = 0;
    if (dwFlags & TYPE_BUILDING)
      bKind |= LabProto::UNIT_BUILDING;
    if (dwFlags & TYPE_FLYER)
      bKind |= LabProto::UNIT_AIR;
    if (dwFlags & TYPE_WORKER)
      bKind |= LabProto::UNIT_WORKER;
    if (dwFlags & TYPE_RESOURCES)
      bKind |= LabProto::UNIT_RESOURCE;
    w.D(static_cast<uint32_t>(pUnit->GetUnitID()));
    w.W(pUnit->wPositionX);
    w.W(pUnit->wPositionY);
    w.B(pUnit->bPlayer);
    w.B(bKind);
    ++nUnits;
  }
  w.v[nCountAt] = static_cast<uint8_t>(nUnits);
  w.v[nCountAt + 1] = static_cast<uint8_t>(nUnits >> 8);
  Send(w, LabProto::SAMPLE);
}

void Status(Uint64 nNow)
{
  const unsigned dwFrames = oc_dwReplayFrame - g_dwStatusFrame;
  const Uint64 nMs = nNow - g_nStatusAt;
  int nUnits = 0;
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
    ++nUnits;
  LanProto::Writer w;
  w.D(g_dwGame);
  w.D(oc_dwReplayFrame);
  w.D(nMs > 0 ? static_cast<uint32_t>(dwFrames * 10000ull / nMs) : 0);
  w.D(static_cast<uint32_t>(nUnits));
  Send(w, LabProto::STATUS);
  g_dwStatusFrame = oc_dwReplayFrame;
  g_nStatusAt = nNow;
}

void GameEnd(void)
{
  const uint64_t qState = SyncHashFold();
  const double dSeconds = PortBenchRunning() ? PortBenchSeconds() : g_dSeconds;
  const unsigned dwFrames = PortBenchGameFrame();
  const std::string reason = PortBenchEndReason();
  int anUnits[PLAYER_SLOTS_MAX] = {};
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
    if (pUnit->bPlayer < PLAYER_SLOTS_MAX)
      ++anUnits[pUnit->bPlayer];
  std::vector<int> seats, winners;
  for (int s = 0; s < PlayerSlots() && s < PLAYER_SLOTS_MAX; ++s)
    if (InGame(s)) {
      seats.push_back(s);
      // with one player (or one team) left, those with units won; at the length, nobody did
      if (reason == "last" && anUnits[s] > 0)
        winners.push_back(s);
    }
  LanProto::Writer w;
  w.D(g_dwGame);
  w.S(reason);
  w.D(oc_dwReplayFrame);
  w.D(static_cast<uint32_t>(qState));
  w.D(static_cast<uint32_t>(qState >> 32));
  w.D(static_cast<uint32_t>(dSeconds * 10.0 + 0.5));
  w.D(dSeconds > 0.0 ? static_cast<uint32_t>(dwFrames * 10.0 / dSeconds + 0.5) : 0);
  w.B(static_cast<uint8_t>(winners.size()));
  for (int s : winners)
    w.B(static_cast<uint8_t>(s));
  w.B(static_cast<uint8_t>(seats.size()));
  for (int s : seats) {
    SyncSlot slot;
    SyncSlotRead(s, &slot);
    w.B(static_cast<uint8_t>(s));
    w.D(static_cast<uint32_t>(anUnits[s]));
    w.D(static_cast<uint32_t>(slot.nMineralsGathered));
    w.D(static_cast<uint32_t>(slot.nGasGathered));
  }
  Send(w, LabProto::GAME_END);
  g_log("game %u ended: %s at frame %u, %.1f s, state %016llX", g_dwGame, reason.c_str(),
             oc_dwReplayFrame, dSeconds, static_cast<unsigned long long>(qState));
  Drain(1000);
  // the lab answers with the next game or STOP; a --bench worker's one game is read when the
  // lab closes the connection
  if (!g_bJobs)
    WaitClosed(2000);
}

// What the lab sent.
void Read(void)
{
  uint8_t bKind;
  std::vector<uint8_t> data;
  while (LanNet::Receive(g_nConn, &bKind, &data)) {
    LanProto::Reader r(data);
    if (bKind == LabProto::SAMPLING) {
      g_nRate = r.W();
      const int nMost = r.W();
      g_nMostUnits = nMost > 0 ? nMost : g_nMostUnits;
    } else if (bKind == LabProto::PACE) {
      g_nPace = r.B();
      g_bPaused = r.B() != 0;
      g_qPaceAt = SDL_GetTicksNS();
      g_dwPaceFrame = oc_dwReplayFrame;
    } else if (bKind == LabProto::JOB) {
      PortBenchSettings s;
      const uint32_t dwGame = r.D();
      lstrcpynA(s.szMap, r.S().c_str(), sizeof s.szMap);
      s.nPlayers = r.B();
      lstrcpynA(s.szAiLevels, r.S().c_str(), sizeof s.szAiLevels);
      lstrcpynA(s.szRaces, r.S().c_str(), sizeof s.szRaces);
      s.nGameType = r.S() == "melee" ? 1 : 0;
      s.nSeed = static_cast<int>(r.D());
      s.dwFrames = r.D();
      const std::string out = r.S();
      if (r.bBad || !g_bJobs)
        continue;
      // as --bench-speed max --bench-shown none
      s.nSpeed = 6;
      s.nSpeedMul = 0;
      s.nShown = 2;
      g_job = s;
      g_out = out;
      g_dwGame = dwGame;
      g_bJob = true;
    } else if (bKind == LabProto::WATCHED) {
      const bool bWas = g_bWatched;
      g_bWatched = r.B() != 0;
      g_dwAllowed = r.D();
      // the farm's pace counts again from here
      g_qPaceAt = SDL_GetTicksNS();
      g_dwPaceFrame = oc_dwReplayFrame;
      if (g_bWatched != bWas) {
        g_log("game %u: %s at frame %u", g_dwGame, g_bWatched ? "watched" : "no longer watched", oc_dwReplayFrame);
        if (g_bStarted && !g_bEnded)
          Status(SDL_GetTicks());  // where it is, for the watcher to catch up to
      }
    } else if (bKind == LabProto::STOP) {
      if (r.B() == 1) {
        g_log("told to quit");
        PortStartupEnded(0);
        ExitProcess(0);
      }
      // the game ended now, as a run's Leave ends it, and the program with it
      if (g_bStarted && !g_bEnded) {
        PortBenchLeave(1);
        g_bEnded = true;
        GameEnd();
      }
    }
  }
}

// The lab's pace: the game held to so many times Fastest by the clock, or held where it is
// while the farm is paused; watched, held at the frame the watcher allows. Waiting here only
// puts off the next frame; nothing of the game changes. A worker that falls behind its pace
// counts from where it is, not racing to catch up. While it waits it still says where it is,
// so the lab does not take it for silent.
void Pace(void)
{
  while (!g_bEnded && (g_bWatched ? oc_dwReplayFrame >= g_dwAllowed : g_bPaused || g_nPace > 0)) {
    const Uint64 qNow = SDL_GetTicksNS();
    Uint64 qWait = 5000000;
    if (g_bWatched) {
      // as far as the watcher allows, then here until it allows more
    } else if (!g_bPaused) {
      const Uint64 qDue = g_qPaceAt + static_cast<Uint64>(oc_dwReplayFrame - g_dwPaceFrame) * kFastestNs /
                                          static_cast<Uint64>(g_nPace);
      if (qNow + 1000000 >= qDue) {
        if (qNow > qDue + 250000000) {
          g_qPaceAt = qNow;
          g_dwPaceFrame = oc_dwReplayFrame;
        }
        return;
      }
      qWait = qDue - qNow < qWait ? qDue - qNow : qWait;
    } else {
      g_qPaceAt = qNow;
      g_dwPaceFrame = oc_dwReplayFrame;
    }
    LanNet::Wait(static_cast<int>(qWait / 1000000));
    LanNet::Pump();
    if (!LanNet::Alive(g_nConn)) {
      g_log("the lab has gone");
      ExitProcess(3);
    }
    Read();
    if (SDL_GetTicks() - g_nStatusAt >= 500)
      Status(SDL_GetTicks());
  }
}

#if defined(__APPLE__)
// A Mac's worker ends with the lab as Linux's does through PR_SET_PDEATHSIG: a thread waits in
// kqueue for the lab's process to end.
void *WatchTheLabThread(void *pv)
{
  const int nQueue = static_cast<int>(reinterpret_cast<intptr_t>(pv));
  struct kevent ev;
  for (;;) {
    const int n = kevent(nQueue, NULL, 0, &ev, 1, NULL);
    if (n > 0)
      _exit(3);
    if (n < 0 && errno != EINTR)
      return NULL;
  }
}

void WatchTheLab(void)
{
  const pid_t lab = getppid();
  if (lab == 1)
    _exit(3);  // the lab is gone already, and launchd has the worker
  const int nQueue = kqueue();
  if (nQueue < 0)
    return;
  struct kevent ev;
  EV_SET(&ev, lab, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, NULL);
  if (kevent(nQueue, &ev, 1, NULL, 0, NULL) < 0) {
    if (errno == ESRCH)
      _exit(3);
    close(nQueue);
    return;
  }
  pthread_t thread;
  if (pthread_create(&thread, NULL, WatchTheLabThread, reinterpret_cast<void *>(static_cast<intptr_t>(nQueue))) == 0)
    pthread_detach(thread);
}
#endif

}  // namespace

bool PortLabWorker(void)
{
  return g_bWorker;
}

const char *PortLabWorkerOut(void)
{
  return g_bJobs ? g_out.c_str() : PortCommandOptions()->szLabOut;
}

bool PortLabWorkerNext(void)
{
  if (!g_bWorker || !g_bJobs)
    return false;
  // the game given came back here without its end: it did not start, and the lab starts another
  // worker in this one's place
  if (g_bGiven && !g_bEnded) {
    g_log("game %u: did not start", g_dwGame);
    LanProto::Writer w;
    w.S("game " + std::to_string(g_dwGame) + " did not start");
    Send(w, LabProto::LOG);
    Drain(1000);
    ExitProcess(4);
  }
  while (!g_bJob) {
    LanNet::Pump();
    if (!LanNet::Alive(g_nConn)) {
      g_log("the lab has gone");
      ExitProcess(3);
    }
    Read();  // STOP 1 ends the program here
    if (!g_bJob)
      LanNet::Wait(10);
  }
  g_bJob = false;
  g_bGiven = true;
  g_bStarted = g_bEnded = false;
  g_bWatched = false;
  g_dwAllowed = 0;
  g_dSeconds = 0.0;
  g_nSampleAt = 0;
  // no music: 1.17 fades it out for a second as each game ends (stop_music), twice a farm game
  // (the game made, then played), and a worker has no one to hear it
  gOptions_music_on = 0;
  if (!PortBenchLabGame(g_job)) {
    g_log("game %u: its game on %s was not made", g_dwGame, g_job.szMap);
    return false;  // back here, as a game that did not start
  }
  return true;
}

void PortLabWorkerStart(void)
{
  const PortOptions *o = PortCommandOptions();
  if (o->szLabWorker[0] == '\0')
    return;
  g_bWorker = true;
  // hidden, silent and drawing as little as there is: the farm's games are seen in the lab,
  // and many run at once, so each takes one thread and no graphics card
  SetEnvironmentVariableA("HYPERION_WINDOW", "hidden");
  SetEnvironmentVariableA("SDL_AUDIO_DRIVER", "dummy");
  SetEnvironmentVariableA("HYPERION_RENDERER", "sdl");
  SetEnvironmentVariableA("SDL_RENDER_DRIVER", "software");
  SetEnvironmentVariableA("HYPERION_WORKERS", "1");
  SetEnvironmentVariableA("HYPERION_CORES", "any");
  SetEnvironmentVariableA("HYPERION_QUIET", "1");  // a crash ends it without a message box (port_crash.cpp)
  // nothing of the lab's own tests and tools: its front end's steering, its DLLs
  static const char *const kNotTheWorkers[] = {
      "HYPERION_FRONT_OPEN", "HYPERION_FRONT_MAP", "HYPERION_FRONT_SNAP", "HYPERION_FRONT_SNAP_AT",
      "HYPERION_FRONT_SNAP_SCREEN", "HYPERION_FRONT_SETTINGS", "HYPERION_FRONT_ENTER", "HYPERION_LAN_KEY",
      "HYPERION_DLLS", "HYPERION_TRAINERTEST", "HYPERION_SNAPTEST", "HYPERION_SNAPPROBE"};
  for (const char *psz : kNotTheWorkers)
    SetEnvironmentVariableA(psz, NULL);
#if defined(__linux__)
  prctl(PR_SET_PDEATHSIG, SIGKILL);  // it ends with the lab, as a Job ends it on Windows
#elif defined(__APPLE__)
  WatchTheLab();  // macOS has no PR_SET_PDEATHSIG
#endif
  // <port>:<token>:<worker>
  const int nPort = atoi(o->szLabWorker);
  const char *pszToken = strchr(o->szLabWorker, ':');
  std::string token = pszToken != NULL ? pszToken + 1 : "";
  const size_t nColon = token.find(':');
  if (nColon != std::string::npos) {
    g_dwWorker = static_cast<uint32_t>(strtoul(token.c_str() + nColon + 1, NULL, 10));
    token.resize(nColon);
  }
  // without --bench, its games are the lab's (JOB), one after another
  g_bJobs = !o->bBench;
  g_dwGame = static_cast<uint32_t>(strtoul(o->szLabGame, NULL, 10));
  uint32_t dwLoopback = 0;
  LanNet::AddrParse("127.0.0.1", &dwLoopback);
  g_nConn = LanNet::Connect(dwLoopback, nPort, 3000);
  if (g_nConn < 0) {
    g_log("worker %u: no lab on port %d", g_dwWorker, nPort);
    ExitProcess(2);
  }
  LanProto::Writer w;
  w.W(LabProto::kVersion);
  w.D(g_dwWorker);
  w.S(token);
  w.D(GetCurrentProcessId());
  w.S(HYPERION_VERSION_TEXT);
  Send(w, LabProto::HELLO);
  LanNet::Pump();
  g_log("worker %u: connected to the lab on port %d, game %u", g_dwWorker, nPort, g_dwGame);
}

void PortLabFrame(void)
{
  if (!g_bWorker) {
    PortLabWatchFrame();  // the lab's own process: a farm game watched (port_labwatch.cpp)
    return;
  }
  const Uint64 nNow = SDL_GetTicks();
  if (nNow - g_nPumpAt >= 5) {
    g_nPumpAt = nNow;
    LanNet::Pump();
    if (!LanNet::Alive(g_nConn)) {
      if (g_bEnded)
        return;  // the lab has read the end and closed it
      g_log("the lab has gone");
      ExitProcess(3);
    }
    Read();
  }
  if (g_bEnded)
    return;
  if (!g_bStarted) {
    if (!PortBenchPlaying() || !PortBenchRunning())
      return;
    g_bStarted = true;
    g_nStatusAt = nNow;
    g_dwStatusFrame = oc_dwReplayFrame;
    GameStart();
  }
  if (PortBenchRunning())
    g_dSeconds = PortBenchSeconds();
  if (PortBenchEndReason()[0] != '\0') {
    g_bEnded = true;
    GameEnd();
    return;
  }
  if (g_nRate > 0 && nNow - g_nSampleAt >= static_cast<Uint64>(1000 / g_nRate) &&
      LanNet::Pending(g_nConn) < kBacklogMost) {
    g_nSampleAt = nNow;
    Sample();
  }
  // watched: the state every 240 frames, which the watcher compares with its own at that frame
  if (g_bWatched && oc_dwReplayFrame % kHashEvery == 0) {
    const uint64_t qState = SyncHashFold();
    LanProto::Writer w;
    w.D(g_dwGame);
    w.D(oc_dwReplayFrame);
    w.D(static_cast<uint32_t>(qState));
    w.D(static_cast<uint32_t>(qState >> 32));
    Send(w, LabProto::HASH);
  }
  if (nNow - g_nStatusAt >= (g_bWatched ? 50u : 500u))
    Status(nNow);
  Pace();
}
