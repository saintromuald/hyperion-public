// port_labfarm.cpp -- the AI lab's farm (port_labfarm.h). The lab listens on this
// machine at a port the system gives, starts each worker with a token, and takes the
// connection that says HELLO with it as that worker's. A worker plays one game and ends; its
// slot then starts the next game, until the games asked for are played or the farm is stopped.
//
// On Windows every worker is in one Job object that ends them when the lab's process ends,
// however it ends; elsewhere a worker ends when its connection to the lab goes.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <deque>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include "port_labfarm.h"
#include "port_labwatch.h"
#include "port_labproto.h"
#include "../lan/port_lannet.h"
#include "../lan/port_lanproto.h"
#include "../platform/port_json.h"
#include "../platform/port_log.h"
#include "../platform/port_paths.h"
#include "../platform/port_util.h"
#include "../tools/port_control.h"

namespace {

PortLogger g_log("hyperion_lab.log");

const int kAtOnceMost = 64;
const uint64_t kSilentMs = 30000;   // a worker that says nothing this long is ended
const uint64_t kHelloMs = 10000;    // a connection that has not said who it is by then is dropped

struct Worker : LabSlot {
  HANDLE hProcess = NULL;
  DWORD dwPid = 0;
  int nConn = -1;
  std::string token, folder;
  uint64_t qStarted = 0, qHeard = 0;
  bool bEnded = false;      // its GAME_END came
  bool bGiven = false;      // a game given (JOB) whose end has not come
  std::string lostWhy;      // why the lab ended it
  bool bWatched = false;    // watched live: WATCHED sent, up to dwAllowed
  unsigned dwAllowed = 0;
  uint64_t qPublishedAt = 0;  // its last sample on the stream
};

// A watched game's state at a frame (HASH)
struct LabHash {
  uint32_t dwGame;
  unsigned dwFrame;
  uint64_t qState;
};

struct Unclaimed {
  int nConn;
  uint64_t qAt;
};

LabProject g_project;
std::string g_projectName = "First Project";
std::vector<std::string> g_mapPaths;
std::vector<Worker> g_workers;
std::vector<Unclaimed> g_unclaimed;
std::deque<LabHash> g_hashes;
int g_nListener = -1, g_nPort = 0;
bool g_bRunning;
int g_nAtOnce, g_nGamesWanted, g_nGamesGiven, g_nPlayed;
uint32_t g_dwNextGame = 1;
uint64_t g_qStartedAt;
int g_nRate = 30;          // samples a second while the wall shows them
int g_nRateSent = -1;      // what the workers were told
uint64_t g_qWallAt;        // when the wall last drew the games' units
int g_nPace;               // 0, as fast as each goes (the user's choice, PRD 2026-10-19)
bool g_bPaused;
#if defined(_WIN32)
HANDLE g_hJob;
#endif

std::string LabFolder(void)
{
  const std::string lab = std::string(PortDataFolder()) + "lab";
  CreateDirectoryA(lab.c_str(), NULL);
  return lab + "\\";
}

std::string ProjectFolder(void)
{
  const std::string projects = LabFolder() + "projects";
  CreateDirectoryA(projects.c_str(), NULL);
  const std::string folder = projects + "\\" + g_projectName;
  CreateDirectoryA(folder.c_str(), NULL);
  return folder + "\\";
}

std::vector<std::string> Strings(const PortJson &j)
{
  std::vector<std::string> out;
  for (size_t i = 0; i < j.size(); ++i)
    out.push_back(j.At(i).Str());
  return out;
}

std::string JsonList(const std::vector<std::string> &v)
{
  std::string s = "[";
  for (size_t i = 0; i < v.size(); ++i)
    s += (i ? ", " : "") + PortJsonQuote(v[i]);
  return s + "]";
}

// project.json, written with the defaults when it is not there
bool LoadProject(std::string *pWhy)
{
  const std::string path = ProjectFolder() + "project.json";
  const std::string text = PortReadText(path);
  LabProject p;
  p.name = g_projectName;
  if (text.empty()) {
    p.maps = {"(4)Lost Temple.scm"};
    p.levels = {"7,7", "7,6", "6,6", "7,5"};
    const std::string json = "{\r\n  \"name\": " + PortJsonQuote(p.name) + ",\r\n  \"maps\": " + JsonList(p.maps) +
                             ",\r\n  \"players\": " + std::to_string(p.nPlayers) + ",\r\n  \"levels\": " +
                             JsonList(p.levels) + ",\r\n  \"races\": [],\r\n  \"type\": " + PortJsonQuote(p.type) +
                             ",\r\n  \"first_seed\": " + std::to_string(p.nFirstSeed) + ",\r\n  \"frames\": " +
                             std::to_string(p.dwFrames) + "\r\n}\r\n";
    if (FILE *f = fopen(path.c_str(), "wb")) {
      fwrite(json.data(), 1, json.size(), f);
      fclose(f);
    }
    g_log("project %s: made with the defaults", p.name);
  } else {
    PortJson j;
    std::string error;
    if (!PortJsonParse(text.c_str(), &j, &error) || j.kind != PortJson::OBJECT) {
      *pWhy = path + " is not a JSON object: " + error;
      return false;
    }
    p.maps = Strings(j["maps"]);
    p.nPlayers = static_cast<int>(j["players"].Num(2));
    p.levels = Strings(j["levels"]);
    p.races = Strings(j["races"]);
    p.type = j["type"].Str("ffa");
    p.nFirstSeed = static_cast<int>(j["first_seed"].Num(1));
    p.dwFrames = static_cast<unsigned>(j["frames"].Num(86400));
  }
  if (p.nPlayers < 2 || p.nPlayers > 8) {
    *pWhy = "a project's game has 2 to 8 players";
    return false;
  }
  g_project = p;
  return true;
}

// The next game's number: one past the highest folder in games\.
uint32_t NextGame(void)
{
  uint32_t dwMost = 0;
  WIN32_FIND_DATAA fd;
  const HANDLE h = FindFirstFileA((ProjectFolder() + "games\\*").c_str(), &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      const std::string name = fd.cFileName;
      const uint32_t dw = static_cast<uint32_t>(strtoul(name.c_str(), NULL, 10));
      if (name.size() == 6 && name.find_first_not_of("0123456789") == std::string::npos && dw > dwMost)
        dwMost = dw;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  return dwMost + 1;
}

std::string Token(void)
{
  static std::random_device s_rd;
  char sz[40];
  snprintf(sz, sizeof sz, "%08x%08x%08x%08x", static_cast<unsigned>(s_rd()), static_cast<unsigned>(s_rd()),
           static_cast<unsigned>(s_rd()), static_cast<unsigned>(s_rd()));
  return sz;
}

std::string Stamp(void)
{
  SYSTEMTIME t;
  GetLocalTime(&t);
  char sz[32];
  snprintf(sz, sizeof sz, "%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  return sz;
}

std::string FileName(const std::string &path)
{
  return path.substr(path.find_last_of("\\/") + 1);
}

// A game's line in games.jsonl: what was asked of it, then fields of how it ended.
void IndexLine(const Worker &w, const std::string &ended)
{
  std::string line = "{\"game\":" + std::to_string(w.dwGame) + ",\"map\":" + PortJsonQuote(FileName(w.map)) +
                     ",\"map_path\":" + PortJsonQuote(w.map) + ",\"players\":" + std::to_string(g_project.nPlayers) +
                     ",\"levels\":" + PortJsonQuote(w.levels) + ",\"races\":" + PortJsonQuote(w.races) +
                     ",\"seed\":" + std::to_string(w.nSeed) + ",\"worker\":" + std::to_string(w.nWorker) +
                     ",\"at\":" + PortJsonQuote(Stamp()) + "," + ended + "}";
  // the stream's lab.games: each game's end, its line as games.jsonl has it
  if (PortControlSubscribed("lab.games"))
    PortControlPublish("lab.games", "{\"event\":\"end\",\"project\":" + PortJsonQuote(g_projectName) +
                                        ",\"game\":" + line + "}");
  line += "\n";
  FILE *f = fopen((ProjectFolder() + "games.jsonl").c_str(), "ab");
  if (f == NULL) {
    g_log("game %u: games.jsonl could not be written", w.dwGame);
    return;
  }
  fwrite(line.data(), 1, line.size(), f);
  fclose(f);
}

void Lost(Worker &w, const std::string &why)
{
  IndexLine(w, "\"ended\":\"lost\",\"why\":" + PortJsonQuote(why));
  g_log("game %u on worker %d: lost, %s", w.dwGame, w.nWorker, why);
}

#if defined(_WIN32)
// One Job for every worker, which ends them as the lab's process ends.
HANDLE Job(void)
{
  if (g_hJob == NULL) {
    g_hJob = CreateJobObjectA(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
    memset(&li, 0, sizeof li);
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (g_hJob != NULL)
      SetInformationJobObject(g_hJob, JobObjectExtendedLimitInformation, &li, sizeof li);
  }
  return g_hJob;
}
#endif

void Reset(Worker &w)
{
  const int n = w.nWorker;
  w = Worker();
  w.nWorker = n;
}

// The process's part of a slot, for its next game: what it shows stays until that game starts.
void ResetProcess(Worker &w)
{
  w.hProcess = NULL;
  w.dwPid = 0;
  w.nConn = -1;
  w.token.clear();
  w.folder.clear();
  w.qStarted = w.qHeard = 0;
  w.bEnded = false;
  w.bGiven = false;
  w.lostWhy.clear();
  w.bWatched = false;
  w.dwAllowed = 0;
}

bool WantMore(void);

// The project's next game for a worker that has said HELLO, or has ended its last: its folder
// made, and JOB sent.
void GiveJob(Worker &w)
{
  const uint32_t dwGame = g_dwNextGame++;
  ++g_nGamesGiven;
  const size_t n = dwGame - 1;
  w.dwGame = dwGame;
  w.map = g_mapPaths[n % g_mapPaths.size()];
  w.levels = g_project.levels.empty() ? "" : g_project.levels[n % g_project.levels.size()];
  w.races = g_project.races.empty() ? "" : g_project.races[n % g_project.races.size()];
  w.nSeed = g_project.nFirstSeed + static_cast<int>(n);
  const std::string games = ProjectFolder() + "games";
  CreateDirectoryA(games.c_str(), NULL);
  char szGame[16];
  snprintf(szGame, sizeof szGame, "%06u", dwGame);
  w.folder = games + "\\" + szGame;
  CreateDirectoryA(w.folder.c_str(), NULL);
  w.bEnded = false;
  w.bGiven = true;
  w.bWatched = false;
  w.dwAllowed = 0;
  w.state = LAB_STARTING;
  LanProto::Writer s;
  s.D(dwGame);
  s.S(w.map);
  s.B(static_cast<uint8_t>(g_project.nPlayers));
  s.S(w.levels);
  s.S(w.races);
  s.S(g_project.type);
  s.D(static_cast<uint32_t>(w.nSeed));
  s.D(g_project.dwFrames);
  s.S(w.folder);
  LanNet::Send(w.nConn, LabProto::JOB, s.v.data(), s.v.size());
  g_log("game %u: worker %d on %s, seed %d, levels %s", dwGame, w.nWorker, FileName(w.map), w.nSeed,
        w.levels.empty() ? "classic" : w.levels);
}

// After HELLO or a game's end: the next game, or STOP when the farm wants no more of this worker.
void Next(Worker &w)
{
  if (WantMore() && w.nWorker <= g_nAtOnce) {
    GiveJob(w);
    return;
  }
  LanProto::Writer s;
  s.B(1);
  LanNet::Send(w.nConn, LabProto::STOP, s.v.data(), s.v.size());
}

void StartWorker(Worker &w)
{
  ResetProcess(w);
  w.dwGame = 0;  // given at its HELLO
  const std::string workers = LabFolder() + "workers";
  CreateDirectoryA(workers.c_str(), NULL);
  char szWorker[16];
  snprintf(szWorker, sizeof szWorker, "w%02d", w.nWorker);
  const std::string data = workers + "\\" + szWorker;
  CreateDirectoryA(data.c_str(), NULL);
  w.token = Token();
  char szExe[MAX_PATH] = "";
  GetModuleFileNameA(NULL, szExe, MAX_PATH);
  // no --bench: its games come from the lab (JOB), game after game; no --seed, which would
  // outrank each game's (PortObserveSeed); 640 wide, as nobody sees its frames and a wider one
  // costs drawing (the View shape's default is the screen's)
  // the token in the worker's environment, not its command line, which any program can read
  // (port_paths.cpp, PathsSecrets)
  const std::string cmd = "\"" + std::string(szExe) + "\" --data \"" + data + "\" --view 640 --lab-worker " +
                          std::to_string(g_nPort) + ":env:" + std::to_string(w.nWorker);
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si);
  si.cb = sizeof si;
  memset(&pi, 0, sizeof pi);
  DWORD dwFlags = CREATE_NO_WINDOW;
#if defined(_WIN32)
  dwFlags |= CREATE_SUSPENDED;  // in the Job before it runs a line
#endif
  w.qStarted = w.qHeard = PortNowMs();
  std::vector<char> line(cmd.begin(), cmd.end());
  line.push_back('\0');
  SetEnvironmentVariableA("HYPERION_CHILD_TOKEN", w.token.c_str());
  const BOOL bStarted = CreateProcessA(NULL, line.data(), NULL, NULL, FALSE, dwFlags, NULL, NULL, &si, &pi);
  SetEnvironmentVariableA("HYPERION_CHILD_TOKEN", NULL);
  if (!bStarted) {
    g_log("worker %d: could not be started", w.nWorker);
    w.state = LAB_IDLE;
    return;
  }
#if defined(_WIN32)
  if (HANDLE hJob = Job())
    AssignProcessToJobObject(hJob, pi.hProcess);
  ResumeThread(pi.hThread);
#endif
  if (pi.hThread != NULL)
    CloseHandle(pi.hThread);
  w.hProcess = pi.hProcess;
  w.dwPid = pi.dwProcessId;
  w.state = LAB_STARTING;
  g_log("worker %d: process %lu", w.nWorker, static_cast<unsigned long>(w.dwPid));
}

void SendSampling(int nConn)
{
  LanProto::Writer s;
  s.W(static_cast<uint16_t>(g_nRateSent < 0 ? 0 : g_nRateSent));
  s.W(6000);
  LanNet::Send(nConn, LabProto::SAMPLING, s.v.data(), s.v.size());
}

void SendPace(int nConn)
{
  LanProto::Writer s;
  s.B(static_cast<uint8_t>(g_nPace));
  s.B(g_bPaused ? 1 : 0);
  LanNet::Send(nConn, LabProto::PACE, s.v.data(), s.v.size());
}

// A new connection's HELLO, which names its worker and carries its token.
void Claim(void)
{
  for (int c; (c = LanNet::AcceptOn(g_nListener)) >= 0;)
    g_unclaimed.push_back({c, PortNowMs()});
  for (size_t i = 0; i < g_unclaimed.size();) {
    const int c = g_unclaimed[i].nConn;
    uint8_t bKind = 0;
    std::vector<uint8_t> data;
    bool bDone = false;
    if (LanNet::Receive(c, &bKind, &data)) {
      bDone = true;
      LanProto::Reader r(data);
      const uint16_t wVersion = r.W();
      const uint32_t dwWorker = r.D();
      const std::string token = r.S();
      Worker *pFor = NULL;
      for (Worker &w : g_workers)
        if (static_cast<uint32_t>(w.nWorker) == dwWorker && w.nConn < 0 && w.hProcess != NULL && w.token == token)
          pFor = &w;
      if (bKind != LabProto::HELLO || wVersion != LabProto::kVersion || pFor == NULL) {
        g_log("a connection that is no worker's dropped");
        LanNet::Drop(c);
      } else {
        pFor->nConn = c;
        pFor->qHeard = PortNowMs();
        LanNet::Trust(c);  // its token passed: a game's records may follow
        LanProto::Writer w;
        w.D(dwWorker);
        LanNet::Send(c, LabProto::WELCOME, w.v.data(), w.v.size());
        SendSampling(c);
        SendPace(c);
        Next(*pFor);
      }
    } else if (!LanNet::Alive(c) || PortNowMs() - g_unclaimed[i].qAt > kHelloMs) {
      bDone = true;
      LanNet::Drop(c);
    }
    if (bDone)
      g_unclaimed.erase(g_unclaimed.begin() + static_cast<std::ptrdiff_t>(i));
    else
      ++i;
  }
}

LabSeat *SeatOf(Worker &w, int nSlot)
{
  for (LabSeat &s : w.seats)
    if (s.nSlot == nSlot)
      return &s;
  return NULL;
}

void GameEnd(Worker &w, LanProto::Reader &r)
{
  r.D();  // the game
  const std::string reason = r.S();
  const uint32_t dwFrame = r.D();
  const uint32_t dwLo = r.D(), dwHi = r.D();
  const uint32_t dwSeconds10 = r.D(), dwFps10 = r.D();
  char szState[24];
  snprintf(szState, sizeof szState, "%08X%08X", dwHi, dwLo);
  std::vector<int> win;
  std::string winners = "[";
  for (int i = 0, n = r.B(); i < n; ++i) {
    win.push_back(r.B());
    winners += (i ? "," : "") + std::to_string(win.back());
  }
  winners += "]";
  std::string seats = "[";
  for (int i = 0, n = r.B(); i < n; ++i) {
    const int nSlot = r.B();
    const uint32_t dwUnits = r.D(), dwMinerals = r.D(), dwGas = r.D();
    const LabSeat *p = SeatOf(w, nSlot);
    seats += std::string(i ? "," : "") + "{\"slot\":" + std::to_string(nSlot) +
             ",\"race\":" + std::to_string(p ? p->nRace : -1) + ",\"team\":" + std::to_string(p ? p->nTeam : 0) +
             ",\"level\":" + std::to_string(p ? p->nLevel : 0) + ",\"name\":" + PortJsonQuote(p ? p->name : "") +
             ",\"units\":" + std::to_string(dwUnits) + ",\"minerals\":" + std::to_string(dwMinerals) +
             ",\"gas\":" + std::to_string(dwGas) + "}";
  }
  seats += "]";
  char szNums[96];
  snprintf(szNums, sizeof szNums, ",\"seconds\":%.1f,\"game_fps\":%.0f", dwSeconds10 / 10.0, dwFps10 / 10.0);
  IndexLine(w, "\"ended\":" + PortJsonQuote(reason) + ",\"end_frame\":" + std::to_string(dwFrame) +
                   ",\"winners\":" + winners + szNums + ",\"state\":\"" + szState + "\",\"seats\":" + seats);
  w.bEnded = true;
  w.bGiven = false;
  w.state = LAB_ENDING;
  w.dwFrame = dwFrame;
  w.qEndAt = PortNowMs();
  w.endReason = reason;
  w.winners = win;
  ++g_nPlayed;
  g_log("game %u: %s at frame %u in %.1f s", w.dwGame, reason, dwFrame, dwSeconds10 / 10.0);
}

// The stream's lab.samples: a worker's last sample, its players' banks and its units, each 10
// bytes little-endian in base64 (D id, W x, W y in map pixels, B player, B kind).
void PublishSample(const Worker &w)
{
  std::string seats;
  for (const LabSeat &s : w.seats)
    seats += std::string(seats.empty() ? "" : ",") + "{\"slot\":" + std::to_string(s.nSlot) +
             ",\"minerals\":" + std::to_string(s.dwMinerals) + ",\"gas\":" + std::to_string(s.dwGas) +
             ",\"supply\":" + std::to_string(s.nSupply) + ",\"supply_most\":" + std::to_string(s.nSupplyMost) +
             ",\"units\":" + std::to_string(s.nUnits) + ",\"out\":" + (s.bOut ? "true" : "false") + "}";
  std::vector<uint8_t> units;
  units.reserve(w.last.dots.size() * 10);
  for (const LabDot &d : w.last.dots) {
    const uint8_t b[10] = {static_cast<uint8_t>(d.dwId), static_cast<uint8_t>(d.dwId >> 8),
                           static_cast<uint8_t>(d.dwId >> 16), static_cast<uint8_t>(d.dwId >> 24),
                           static_cast<uint8_t>(d.x), static_cast<uint8_t>(d.x >> 8),
                           static_cast<uint8_t>(d.y), static_cast<uint8_t>(d.y >> 8), d.bPlayer, d.bKind};
    units.insert(units.end(), b, b + 10);
  }
  PortControlPublish("lab.samples", "{\"game\":" + std::to_string(w.dwGame) + ",\"worker\":" +
                                        std::to_string(w.nWorker) + ",\"frame\":" + std::to_string(w.last.dwFrame) +
                                        ",\"seats\":[" + seats + "],\"count\":" +
                                        std::to_string(w.last.dots.size()) + ",\"units\":\"" +
                                        PortBase64(units.data(), units.size()) + "\"}");
}

void Take(Worker &w, uint8_t bKind, const std::vector<uint8_t> &data)
{
  LanProto::Reader r(data);
  if (bKind == LabProto::GAME_START) {
    r.D();  // the game
    r.S();  // the map, as the worker found it
    r.D();  // the seed
    r.D();  // the length
    r.B();  // the speed
    r.B();  // its multiple
    w.nMapW = r.W();
    w.nMapH = r.W();
    w.dwShown = w.dwGame;
    w.shownMap = w.map;
    w.prev = LabSample();
    w.last = LabSample();
    w.nSamples = 0;
    w.dwFrame = 0;
    w.nFps10 = 0;
    w.qEndAt = 0;
    w.endReason.clear();
    w.winners.clear();
    w.seats.clear();
    for (int i = 0, n = r.B(); i < n && !r.bBad; ++i) {
      LabSeat s;
      s.nSlot = r.B();
      s.nRace = r.B();
      s.nTeam = r.B();
      s.nLevel = r.B();
      s.name = r.S();
      w.seats.push_back(s);
    }
    w.state = LAB_PLAYING;
    // the stream's lab.games: each game's start, with its seats
    if (PortControlSubscribed("lab.games")) {
      std::string seats;
      for (const LabSeat &s : w.seats)
        seats += std::string(seats.empty() ? "" : ",") + "{\"slot\":" + std::to_string(s.nSlot) +
                 ",\"race\":" + std::to_string(s.nRace) + ",\"team\":" + std::to_string(s.nTeam) +
                 ",\"level\":" + std::to_string(s.nLevel) + ",\"name\":" + PortJsonQuote(s.name) + "}";
      PortControlPublish("lab.games", "{\"event\":\"start\",\"project\":" + PortJsonQuote(g_projectName) +
                                          ",\"game\":" + std::to_string(w.dwGame) + ",\"worker\":" +
                                          std::to_string(w.nWorker) + ",\"map\":" + PortJsonQuote(FileName(w.map)) +
                                          ",\"map_width\":" + std::to_string(w.nMapW) + ",\"map_height\":" +
                                          std::to_string(w.nMapH) + ",\"seed\":" + std::to_string(w.nSeed) +
                                          ",\"levels\":" + PortJsonQuote(w.levels) + ",\"seats\":[" + seats + "]}");
    }
  } else if (bKind == LabProto::SAMPLE) {
    LabSample s;
    r.D();  // the game
    s.dwFrame = r.D();
    s.qAt = PortNowMs();
    for (int i = 0, n = r.B(); i < n && !r.bBad; ++i) {
      const int nSlot = r.B();
      LabSeat dummy, *p = SeatOf(w, nSlot);
      if (p == NULL)
        p = &dummy;
      p->dwMinerals = r.D();
      p->dwGas = r.D();
      p->nSupply = r.W();
      p->nSupplyMost = r.W();
      p->nUnits = r.W();
      p->bOut = r.B() != 0;
    }
    const int nDots = r.W();
    s.dots.reserve(static_cast<size_t>(nDots));
    for (int i = 0; i < nDots && !r.bBad; ++i) {
      LabDot d;
      d.dwId = r.D();
      d.x = r.W();
      d.y = r.W();
      d.bPlayer = r.B();
      d.bKind = r.B();
      s.dots.push_back(d);
    }
    if (r.bBad)
      return;
    std::sort(s.dots.begin(), s.dots.end(), [](const LabDot &a, const LabDot &b) { return a.dwId < b.dwId; });
    w.prev = std::move(w.last);
    w.last = std::move(s);
    w.dwFrame = w.last.dwFrame;
    ++w.nSamples;
    if (PortNowMs() - w.qPublishedAt >= 100 && PortControlSubscribed("lab.samples")) {
      w.qPublishedAt = PortNowMs();
      PublishSample(w);
    }
  } else if (bKind == LabProto::STATUS) {
    r.D();  // the game
    w.dwFrame = r.D();
    w.nFps10 = static_cast<int>(r.D());
    w.nUnits = static_cast<int>(r.D());
  } else if (bKind == LabProto::HASH) {
    LabHash h;
    h.dwGame = r.D();
    h.dwFrame = r.D();
    h.qState = r.D();
    h.qState |= static_cast<uint64_t>(r.D()) << 32;
    if (r.bBad)
      return;
    g_hashes.push_back(h);
    if (g_hashes.size() > 256)
      g_hashes.pop_front();
  } else if (bKind == LabProto::GAME_END) {
    GameEnd(w, r);
    Next(w);
  } else if (bKind == LabProto::LOG) {
    g_log("worker %d: %s", w.nWorker, r.S());
  }
}

void Serve(Worker &w)
{
  if (w.hProcess == NULL)
    return;
  if (w.nConn >= 0) {
    uint8_t bKind = 0;
    std::vector<uint8_t> data;
    while (LanNet::Receive(w.nConn, &bKind, &data)) {
      w.qHeard = PortNowMs();
      Take(w, bKind, data);
    }
  }
  if (WaitForSingleObject(w.hProcess, 0) == WAIT_OBJECT_0) {
    DWORD dwCode = 0;
    GetExitCodeProcess(w.hProcess, &dwCode);
    if (w.bGiven)
      Lost(w, !w.lostWhy.empty() ? w.lostWhy : "the worker ended with code " + std::to_string(dwCode));
    CloseHandle(w.hProcess);
    if (w.nConn >= 0)
      LanNet::Drop(w.nConn);
    w.hProcess = NULL;
    w.nConn = -1;
    w.state = LAB_IDLE;
    return;
  }
  if (w.lostWhy.empty() && PortNowMs() - w.qHeard > kSilentMs) {
    w.lostWhy = "silent for " + std::to_string(kSilentMs / 1000) + " s";
    TerminateProcess(w.hProcess, 1);
  }
}

bool WantMore(void)
{
  return g_bRunning && (g_nGamesWanted == 0 || g_nGamesGiven < g_nGamesWanted);
}

// the live channel's lab_ commands (port_control.h)
bool CmdStatus(const PortJson &, std::string *pOut)
{
  std::string workers;
  for (const Worker &w : g_workers) {
    if (w.state == LAB_IDLE)
      continue;
    static const char *const kStates[] = {"idle", "starting", "playing", "ending"};
    workers += std::string(workers.empty() ? "" : ",") + "{\"worker\":" + std::to_string(w.nWorker) +
               ",\"state\":\"" + kStates[w.state] + "\",\"pid\":" + std::to_string(w.dwPid) +
               ",\"game\":" + std::to_string(w.dwGame) + ",\"map\":" + PortJsonQuote(FileName(w.map)) +
               ",\"frame\":" + std::to_string(w.dwFrame) + ",\"game_fps\":" + std::to_string(w.nFps10 / 10) +
               ",\"units\":" + std::to_string(w.last.dots.size()) + ",\"samples\":" + std::to_string(w.nSamples) +
               ",\"watched\":" + (w.bWatched ? "true" : "false") + "}";
  }
  *pOut = std::string("\"running\":") + (g_bRunning ? "true" : "false") + ",\"project\":" + PortJsonQuote(g_projectName) +
          ",\"folder\":" + PortJsonQuote(g_nListener >= 0 ? ProjectFolder() : "") + ",\"at_once\":" +
          std::to_string(g_nAtOnce) + ",\"at_once_default\":" + std::to_string(PortLabFarmAtOnceDefault()) +
          ",\"given\":" + std::to_string(g_nGamesGiven) + ",\"played\":" + std::to_string(g_nPlayed) +
          ",\"port\":" + std::to_string(g_nPort) + ",\"pace\":" + std::to_string(g_nPace) +
          ",\"paused\":" + (g_bPaused ? "true" : "false") + ",\"workers\":[" + workers + "]";
  return true;
}

bool CmdStart(const PortJson &j, std::string *pOut)
{
  if (j["project"].kind == PortJson::STRING && !g_bRunning)
    g_projectName = j["project"].s;
  std::string why;
  if (!PortLabFarmStart(static_cast<int>(j["at_once"].Num(0)), static_cast<int>(j["games"].Num(0)), &why)) {
    *pOut = why;
    return false;
  }
  return CmdStatus(j, pOut);
}

// {multiple?, paused?}: the workers' pace, a multiple of Fastest or 0 for the most
bool CmdPace(const PortJson &j, std::string *pOut)
{
  PortLabFarmPace(static_cast<int>(j["multiple"].Num(PortLabFarmPaceNow())),
                  j["paused"].kind == PortJson::BOOL ? j["paused"].b : PortLabFarmPaused());
  return CmdStatus(j, pOut);
}

bool CmdStop(const PortJson &j, std::string *pOut)
{
  PortLabFarmStop();
  return CmdStatus(j, pOut);
}

// games.jsonl's lines from the since'th (0 the first), at most max of them
bool CmdGames(const PortJson &j, std::string *pOut)
{
  const size_t nSince = static_cast<size_t>(j["since"].Num(0));
  const size_t nMost = static_cast<size_t>(j["max"].Num(500));
  const std::string text = PortReadText(ProjectFolder() + "games.jsonl");
  std::string games;
  size_t nLine = 0, nShown = 0;
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos)
      end = text.size();
    std::string line = text.substr(at, end - at);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    at = end + 1;
    if (line.empty())
      continue;
    if (nLine++ < nSince || nShown >= nMost)
      continue;
    games += (nShown++ ? "," : "") + line;
  }
  *pOut = "\"count\":" + std::to_string(nLine) + ",\"games\":[" + games + "]";
  return true;
}

}  // namespace

bool PortLabFarmStart(int nAtOnce, int nGames, std::string *pWhy)
{
#if defined(__EMSCRIPTEN__)
  // a browser starts no processes: the farm is a desktop's
  (void)nAtOnce;
  (void)nGames;
  *pWhy = "the lab's farm needs Hyperion on a desktop";
  return false;
#else
  if (g_bRunning) {
    *pWhy = "the farm is playing";
    return false;
  }
  if (!LoadProject(pWhy))
    return false;
  g_mapPaths.clear();
  for (const std::string &m : g_project.maps) {
    std::string path;
    if (PortControlMapPath(m, &path))
      g_mapPaths.push_back(path);
    else
      g_log("project %s: no map %s", g_projectName, m);
  }
  if (g_mapPaths.empty()) {
    *pWhy = "none of the project's maps is in this install";
    return false;
  }
  if (g_nListener < 0) {
    g_nListener = LanNet::ListenLocal(&g_nPort);
    if (g_nListener < 0) {
      *pWhy = "the lab could not listen on this machine";
      return false;
    }
  }
  PortLabFarmSetAtOnce(nAtOnce > 0 ? nAtOnce : PortLabFarmAtOnceDefault());
  g_nGamesWanted = nGames > 0 ? nGames : 0;
  g_nGamesGiven = g_nPlayed = 0;
  g_dwNextGame = NextGame();
  g_qStartedAt = PortNowMs();
  g_bRunning = true;
  g_log("farm: %s, %d at once, %s, from game %u, port %d", g_projectName, g_nAtOnce,
        g_nGamesWanted ? (std::to_string(g_nGamesWanted) + " games").c_str() : "until stopped", g_dwNextGame, g_nPort);
  return true;
#endif
}

void PortLabFarmStop(void)
{
  g_bRunning = false;
  for (Worker &w : g_workers)
    if (w.hProcess != NULL)
      TerminateProcess(w.hProcess, 1);
  for (Worker &w : g_workers) {
    if (w.hProcess == NULL)
      continue;
    WaitForSingleObject(w.hProcess, 2000);
    if (w.bGiven)
      IndexLine(w, "\"ended\":\"stopped\"");
    CloseHandle(w.hProcess);
    if (w.nConn >= 0)
      LanNet::Drop(w.nConn);
    Reset(w);
  }
  g_log("farm: stopped, %d games played", g_nPlayed);
}

bool PortLabFarmRunning(void)
{
  return g_bRunning;
}

int PortLabFarmAtOnce(void)
{
  return g_nAtOnce;
}

void PortLabFarmSetAtOnce(int n)
{
  g_nAtOnce = n < 1 ? 1 : n > kAtOnceMost ? kAtOnceMost : n;
  while (static_cast<int>(g_workers.size()) < g_nAtOnce) {
    g_workers.emplace_back();
    g_workers.back().nWorker = static_cast<int>(g_workers.size());
  }
}

int PortLabFarmAtOnceDefault(void)
{
  const int n = static_cast<int>(std::thread::hardware_concurrency()) - 2;
  return n < 1 ? 1 : n > 24 ? 24 : n;
}

int PortLabFarmPlayed(void)
{
  return g_nPlayed;
}

int PortLabFarmGamesAnHour(void)
{
  const uint64_t qMs = PortNowMs() - g_qStartedAt;
  return g_bRunning && qMs >= 30000 ? static_cast<int>(g_nPlayed * 3600000ull / qMs) : 0;
}

const LabProject &PortLabFarmProject(void)
{
  return g_project;
}

std::string PortLabFarmProjectName(void)
{
  return g_projectName;
}

std::string PortLabFarmProjectFile(void)
{
  const std::string path = ProjectFolder() + "project.json";
  std::string why;
  if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES && !g_bRunning)
    LoadProject(&why);
  return path;
}

std::string PortLabFarmFolder(void)
{
  return ProjectFolder();
}

int PortLabFarmSlots(void)
{
  return static_cast<int>(g_workers.size());
}

const LabSlot &PortLabFarmSlot(int i)
{
  return g_workers[static_cast<size_t>(i)];
}

void PortLabFarmSampling(int nRate)
{
  g_nRate = nRate < 0 ? 0 : nRate;
}

void PortLabFarmWallShown(bool bSamples)
{
  if (bSamples)
    g_qWallAt = PortNowMs();
}

void PortLabFarmPace(int nMultiple, bool bPaused)
{
  if (nMultiple == g_nPace && bPaused == g_bPaused)
    return;
  g_nPace = nMultiple < 0 ? 0 : nMultiple > 255 ? 255 : nMultiple;
  g_bPaused = bPaused;
  for (const Worker &w : g_workers)
    if (w.nConn >= 0)
      SendPace(w.nConn);
  g_log("farm: pace %s%s", g_nPace ? ("x" + std::to_string(g_nPace)).c_str() : "the most", g_bPaused ? ", paused" : "");
}

int PortLabFarmPaceNow(void)
{
  return g_nPace;
}

bool PortLabFarmPaused(void)
{
  return g_bPaused;
}

void PortLabFarmWatch(uint32_t dwGame, bool bOn, unsigned dwAllowed)
{
  for (Worker &w : g_workers) {
    if (w.nConn < 0 || w.bEnded)
      continue;
    const bool bThis = bOn && w.dwGame == dwGame && w.state != LAB_IDLE;
    const unsigned dwTo = bThis ? dwAllowed : 0;
    if (bThis == w.bWatched && dwTo == w.dwAllowed)
      continue;
    if (bThis != w.bWatched)
      g_log("game %u: %s", w.dwGame, bThis ? "watched live" : "back to the farm's pace");
    w.bWatched = bThis;
    w.dwAllowed = dwTo;
    LanProto::Writer s;
    s.B(bThis ? 1 : 0);
    s.D(dwTo);
    LanNet::Send(w.nConn, LabProto::WATCHED, s.v.data(), s.v.size());
  }
}

bool PortLabFarmHash(uint32_t dwGame, unsigned dwFrame, uint64_t *pqState)
{
  for (auto it = g_hashes.rbegin(); it != g_hashes.rend(); ++it)
    if (it->dwGame == dwGame && it->dwFrame == dwFrame) {
      *pqState = it->qState;
      return true;
    }
  return false;
}

void PortLabFarmPump(void)
{
  static bool s_bCommands;
  if (!s_bCommands) {
    s_bCommands = true;
    PortControlCommand("lab_status", CmdStatus);  // the farm and each worker's game
    PortControlCommand("lab_start", CmdStart);    // {at_once?, games?, project?}
    PortControlCommand("lab_stop", CmdStop);
    PortControlCommand("lab_pace", CmdPace);      // {multiple?, paused?}
    PortControlCommand("lab_games", CmdGames);    // {since?, max?}: games.jsonl's lines
    PortControlCommand("lab_watch", PortLabWatchCommand);  // {game?, live?, to_end?, leave?}: a game watched
  }
  if (g_nListener < 0)
    return;
  // samples while the wall shows them, and 10 a second at least while the stream has them
  int nRate = g_qWallAt != 0 && PortNowMs() - g_qWallAt < 500 ? g_nRate : 0;
  if (nRate < 10 && PortControlSubscribed("lab.samples"))
    nRate = 10;
  if (nRate != g_nRateSent) {
    g_nRateSent = nRate;
    for (const Worker &w : g_workers)
      if (w.nConn >= 0)
        SendSampling(w.nConn);
  }
  LanNet::Pump();
  Claim();
  for (Worker &w : g_workers)
    Serve(w);
  // the stream's lab.workers: the farm as lab_status has it, once a second
  static uint64_t s_qWorkersAt;
  if (PortNowMs() - s_qWorkersAt >= 1000 && PortControlSubscribed("lab.workers")) {
    s_qWorkersAt = PortNowMs();
    std::string status;
    CmdStatus(PortJson(), &status);
    PortControlPublish("lab.workers", "{" + status + "}");
  }
  // a process a slot, while there are games to give that the processes starting will not take
  int nStarting = 0;
  for (const Worker &w : g_workers)
    nStarting += w.hProcess != NULL && w.nConn < 0 ? 1 : 0;
  for (int i = 0; i < g_nAtOnce && i < static_cast<int>(g_workers.size()); ++i)
    if (g_workers[static_cast<size_t>(i)].state == LAB_IDLE && g_workers[static_cast<size_t>(i)].hProcess == NULL &&
        WantMore() && (g_nGamesWanted == 0 || g_nGamesGiven + nStarting < g_nGamesWanted)) {
      StartWorker(g_workers[static_cast<size_t>(i)]);
      ++nStarting;
    }
  if (g_bRunning && !WantMore()) {
    bool bAny = false;
    for (const Worker &w : g_workers)
      bAny = bAny || w.hProcess != NULL;
    if (!bAny) {
      g_bRunning = false;
      g_log("farm: the %d games asked for are played", g_nGamesWanted);
    }
  }
}
