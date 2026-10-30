// port_lanroom_host.cpp -- the room's work on the host machine: the password and
// tokens, the console's settings (lanroom.json), the files and fonts it serves, the host's
// load, the room's own Hyperions (a match's copy, a browser player's seat, a page's tab) and
// the note that says a room is running. Presentation and transport only.
#include "port_lanroom_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__APPLE__)
#include <mach/mach.h>  // host_statistics
#endif
#include "port_lannet.h"
#include "port_lanweb.h"
#include "../platform/port_paths.h"
#include "../platform/port_posix.h"  // PortNativePath
#include "../platform/port_util.h"  // PortNowMs

namespace LanRoom {

std::mt19937 &Rng(void)
{
  static std::mt19937 s_rng{std::random_device{}()};
  return s_rng;
}

// Three words and a number, easy to read out across a room.
std::string NewPassword(void)
{
  static const char *const kWords[] = {"ghost", "lurker", "zealot", "marine", "carrier", "hydra", "probe",
                                       "vulture", "goliath", "scout", "archon", "wraith", "medic", "drone",
                                       "overlord", "arbiter", "reaver", "valkyrie", "defiler", "dragoon"};
  const size_t n = sizeof kWords / sizeof kWords[0];
  std::uniform_int_distribution<size_t> w(0, n - 1);
  std::uniform_int_distribution<int> d(10, 99);
  std::string a = kWords[w(Rng())], b;
  do
    b = kWords[w(Rng())];
  while (b == a);
  return a + "-" + b + "-" + std::to_string(d(Rng()));
}

// 128 bits from the system's random source, not Rng's: a token is a password another program
// must not work out from the ones it has seen
std::string Token(void)
{
  std::random_device rd;
  char sz[40];
  snprintf(sz, sizeof sz, "%08x%08x%08x%08x", static_cast<unsigned>(rd()), static_cast<unsigned>(rd()),
           static_cast<unsigned>(rd()), static_cast<unsigned>(rd()));
  return sz;
}

std::string Setting(const char *pszName, const char *pszDefault)
{
  const PortJson &v = g_settings[pszName];
  return v.kind == PortJson::STRING ? v.s : pszDefault;
}

bool PasswordOn(void)
{
  return g_settings["password"].kind != PortJson::BOOL || g_settings["password"].b;
}

// The host's own game files, served at /files/ to the browsers that play in their tab while
// Settings offer them: what the page fetches when its player has not given it a
// folder. Only this network reaches the room's page; each player owns StarCraft.
bool BrowserFilesOn(void)
{
  return Setting("browserFiles", "Offer") != "Never";
}

void ServeGameFiles(void)
{
  static const char *const kFiles[] = {"Local.dll", "StarDat.mpq", "BrooDat.mpq", "patch_rt.mpq"};
  std::string folder;
  if (const char *psz = getenv("HYPERION_GAME")) {
    folder = psz;
    const size_t n = folder.find_last_of("\\/");
    folder = n == std::string::npos ? std::string() : folder.substr(0, n + 1);
  }
  const bool bOn = BrowserFilesOn() && !folder.empty();
  for (const char *pszName : kFiles)
    LanWeb::ServeFile(std::string("/files/") + pszName, "application/octet-stream",
                      bOn ? std::string(PortNativePath((folder + pszName).c_str())) : std::string());
  // the more the page fetches past those: none, the CD's music and movies being left to a
  // player's own folder
  static const char kList[] = "[]";
  LanWeb::Serve("/files/files.json", "application/json",
                bOn ? std::vector<uint8_t>(kList, kList + sizeof kList - 1) : std::vector<uint8_t>());
}

void SettingsSave(void)
{
  FILE *f = fopen(PortLogFile("lanroom.json"), "w");
  if (f == NULL)
    return;
  fputs(g_settingsText.c_str(), f);
  fclose(f);
}

void SettingsLoad(void)
{
  FILE *f = fopen(PortLogFile("lanroom.json"), "r");
  std::string text;
  if (f != NULL) {
    char ab[4096];
    size_t n;
    while ((n = fread(ab, 1, sizeof ab, f)) > 0)
      text.append(ab, n);
    fclose(f);
  }
  if (text.empty() || !PortJsonParse(text.c_str(), &g_settings))
    text = "{}";
  PortJsonParse(text.c_str(), &g_settings);
  g_settingsText = text;
}

void ReadLoad(void)
{
#if defined(_WIN32)
  FILETIME idle, kernel, user;
  if (GetSystemTimes(&idle, &kernel, &user)) {
    auto q = [](const FILETIME &f) { return static_cast<uint64_t>(f.dwHighDateTime) << 32 | f.dwLowDateTime; };
    const uint64_t qwIdle = q(idle), qwAll = q(kernel) + q(user);  // the kernel's time holds the idle time
    if (g_load.qwAll != 0 && qwAll > g_load.qwAll)
      g_load.fCpu = 100.0 * (1.0 - static_cast<double>(qwIdle - g_load.qwIdle) / static_cast<double>(qwAll - g_load.qwAll));
    g_load.qwIdle = qwIdle;
    g_load.qwAll = qwAll;
  }
  MEMORYSTATUSEX ms;
  ms.dwLength = sizeof ms;
  if (GlobalMemoryStatusEx(&ms)) {
    g_load.fMemTotal = static_cast<double>(ms.ullTotalPhys) / (1 << 20);
    g_load.fMemUsed = static_cast<double>(ms.ullTotalPhys - ms.ullAvailPhys) / (1 << 20);
  }
#elif defined(__APPLE__)
  // a Mac's: the processors' ticks from the Mach host, the memory as the game reads it
  host_cpu_load_info_data_t cpu;
  mach_msg_type_number_t nCount = HOST_CPU_LOAD_INFO_COUNT;
  if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&cpu), &nCount) ==
      KERN_SUCCESS) {
    const uint64_t qwIdle = cpu.cpu_ticks[CPU_STATE_IDLE];
    const uint64_t qwAll = qwIdle + cpu.cpu_ticks[CPU_STATE_USER] + cpu.cpu_ticks[CPU_STATE_SYSTEM] +
                           cpu.cpu_ticks[CPU_STATE_NICE];
    if (g_load.qwAll != 0 && qwAll > g_load.qwAll)
      g_load.fCpu = 100.0 * (1.0 - static_cast<double>(qwIdle - g_load.qwIdle) / static_cast<double>(qwAll - g_load.qwAll));
    g_load.qwIdle = qwIdle;
    g_load.qwAll = qwAll;
  }
  MEMORYSTATUS ms;
  ms.dwLength = sizeof ms;
  GlobalMemoryStatus(&ms);
  g_load.fMemTotal = static_cast<double>(ms.dwTotalPhys) / (1 << 20);
  g_load.fMemUsed = static_cast<double>(ms.dwTotalPhys - ms.dwAvailPhys) / (1 << 20);
#else
  if (FILE *f = fopen("/proc/stat", "r")) {
    unsigned long long a[8] = {0};
    if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7]) >= 4) {
      const uint64_t qwIdle = a[3] + a[4];
      uint64_t qwAll = 0;
      for (unsigned long long v : a)
        qwAll += v;
      if (g_load.qwAll != 0 && qwAll > g_load.qwAll)
        g_load.fCpu = 100.0 * (1.0 - static_cast<double>(qwIdle - g_load.qwIdle) / static_cast<double>(qwAll - g_load.qwAll));
      g_load.qwIdle = qwIdle;
      g_load.qwAll = qwAll;
    }
    fclose(f);
  }
  if (FILE *f = fopen("/proc/meminfo", "r")) {
    char line[256];
    double fTotal = 0, fAvail = 0;
    while (fgets(line, sizeof line, f)) {
      unsigned long long v;
      if (sscanf(line, "MemTotal: %llu kB", &v) == 1)
        fTotal = v / 1024.0;
      else if (sscanf(line, "MemAvailable: %llu kB", &v) == 1)
        fAvail = v / 1024.0;
    }
    fclose(f);
    g_load.fMemTotal = fTotal;
    g_load.fMemUsed = fTotal - fAvail;
  }
#endif
}

// The page's typefaces are the front end's (Saira and Michroma, OFL), which the exe carries as
// resources (port_ui.cpp): served from them, so the page needs nothing from the internet.
void ServeFonts(void)
{
  static const char *const kFonts[][2] = {{"HYPERION_FONT_TEXT", "/fonts/saira.ttf"},
                                          {"HYPERION_FONT_BOLD", "/fonts/saira-semibold.ttf"},
                                          {"HYPERION_FONT_WIDE", "/fonts/michroma.ttf"}};
  for (const auto &f : kFonts) {
    HRSRC hRes = FindResourceA(NULL, f[0], MAKEINTRESOURCEA(10));  // RT_RCDATA
    HGLOBAL hData = hRes != NULL ? LoadResource(NULL, hRes) : NULL;
    const uint8_t *p = hData != NULL ? static_cast<const uint8_t *>(LockResource(hData)) : NULL;
    const DWORD cb = hRes != NULL ? SizeofResource(NULL, hRes) : 0;
    if (p != NULL && cb != 0)
      LanWeb::Serve(f[1], "font/ttf", std::vector<uint8_t>(p, p + cb));
  }
}

// What the room's own Hyperions start with: no window, sound to nowhere, and none of a check's
// steering of the console's front end (which would open the console and try to start a room).
void ForChild(void)
{
  SetEnvironmentVariableA("HYPERION_WINDOW", "hidden");
  SetEnvironmentVariableA("SDL_AUDIO_DRIVER", "dummy");
  static const char *const kNotTheChilds[] = {"HYPERION_FRONT_OPEN", "HYPERION_FRONT_MAP", "HYPERION_FRONT_SNAP",
                                              "HYPERION_FRONT_SNAP_AT", "HYPERION_FRONT_SNAP_SCREEN",
                                              "HYPERION_FRONT_SETTINGS", "HYPERION_LAN_KEY"};
  for (const char *psz : kNotTheChilds)
    SetEnvironmentVariableA(psz, NULL);
}

// A child Hyperion whose command line names its token as "env": the token in its environment for
// this start alone, which only this user's programs can read (port_paths.cpp, PathsSecrets).
bool StartWithToken(std::string &cmd, const std::string &token, STARTUPINFOA *pSi, PROCESS_INFORMATION *pPi)
{
  SetEnvironmentVariableA("HYPERION_CHILD_TOKEN", token.c_str());
  const BOOL bOk = CreateProcessA(NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, pSi, pPi);
  SetEnvironmentVariableA("HYPERION_CHILD_TOKEN", NULL);
  return bOk != FALSE;
}

// The room's copy of a match: this exe again, with no window and no sound, in a data folder of
// its own, into the room by a token, as a watcher with no delay (port_lanfeed.cpp's
// LanWatchAsCopy). It goes when its replay ends; the room ends it when the room stops.
void StartCopy(Match &m)
{
  if (m.hCopy != NULL || Setting("copies", "On") == "Off")
    return;
  char szExe[MAX_PATH] = "";
  GetModuleFileNameA(NULL, szExe, MAX_PATH);
  std::string data = std::string(PortDataFolder()) + "lan";
  CreateDirectoryA(data.c_str(), NULL);
  data += "\\copy" + std::to_string(m.nId);
  CreateDirectoryA(data.c_str(), NULL);
  m.copyToken = Token();
  std::string cmd = "\"" + std::string(szExe) + "\" --data \"" + data + "\" --join 127.0.0.1:" +
                    std::to_string(LanNet::GamePort()) + " --lan-name \"Host copy\" --lan-copy " + std::to_string(m.nId) +
                    ":env";
  ForChild();
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si);
  si.cb = sizeof si;
  memset(&pi, 0, sizeof pi);
  if (!StartWithToken(cmd, m.copyToken, &si, &pi)) {
    Log("match %d: its copy could not be started", m.nId);
    return;
  }
  CloseHandle(pi.hThread);
  m.hCopy = pi.hProcess;
  m.qwCopyStarted = PortNowMs();
  Log("match %d: its copy started", m.nId);
}

void StopCopy(Match &m)
{
  if (m.hCopy == NULL)
    return;
  if (WaitForSingleObject(m.hCopy, 0) != WAIT_OBJECT_0)
    TerminateProcess(m.hCopy, 0);
  CloseHandle(m.hCopy);
  m.hCopy = NULL;
}

// A browser's player's seat: this exe again with no window, in a data folder of its own, under
// the player's name, into the room by a token (port_lanstream.cpp). Its sound is mixed and sent
// to the browser, not played here (SDL's dummy device).
void StartStream(Device &w, int nWatch)
{
  // a page's seat that watches is not the one that plays, nor one watching something else; a page
  // that plays in its tab has its seat there
  if (StreamSeat *t = StreamFor(w.nId)) {
    if (t->bTab)
      return;
    if (t->nWatch == nWatch && nWatch == 0)
      return;
    StopStream(*t);
  }
  char szExe[MAX_PATH] = "";
  GetModuleFileNameA(NULL, szExe, MAX_PATH);
  std::string data = std::string(PortDataFolder()) + "lan";
  CreateDirectoryA(data.c_str(), NULL);
  data += "\\seat" + std::to_string(w.nId);
  CreateDirectoryA(data.c_str(), NULL);
  StreamSeat t;
  t.token = Token();
  t.name = w.name;
  t.nFor = w.nId;
  t.nWatch = nWatch;
  std::string name = w.name;
  for (char &c : name)
    if (c == '"')
      c = '\'';
  std::string cmd = "\"" + std::string(szExe) + "\" --data \"" + data + "\" --join 127.0.0.1:" +
                    std::to_string(LanNet::GamePort()) + " --lan-name \"" + name + "\" --lan-stream " +
                    std::to_string(w.nId) + ":env";
  ForChild();
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si);
  si.cb = sizeof si;
  memset(&pi, 0, sizeof pi);
  if (!StartWithToken(cmd, t.token, &si, &pi)) {
    Log("%s's seat on this machine could not be started", w.name.c_str());
    return;
  }
  CloseHandle(pi.hThread);
  t.hProcess = pi.hProcess;
  t.qwStarted = PortNowMs();
  g_streams.push_back(t);
  Log("%s's seat on this machine started%s", w.name.c_str(),
      nWatch == kBigScreen ? ", for the Big Screen" : nWatch ? ", to watch" : "");
}

// A page's Watch: a live match, or the Big Screen, which needs none yet. Its seat on this machine
// watches as an installed Hyperion does, and streams it.
void WatchInBrowser(Device &d, const PortJson &j)
{
  const bool bBig = j["big"].b;
  Match *m = bBig ? NULL : MatchOf(static_cast<int>(j["match"].Num()));
  if (d.state == "playing") {
    SendJson(d, "{\"t\":\"error\",\"text\":\"You are playing in a match.\"}");
    return;
  }
  if (!bBig && (m == NULL || (m->state != "live" && m->state != "starting"))) {
    SendJson(d, "{\"t\":\"error\",\"text\":\"That match is not being played now.\"}");
    return;
  }
  if (!WatchingOn()) {
    SendJson(d, "{\"t\":\"error\",\"text\":\"The host has turned watching off.\"}");
    return;
  }
  Unseat(d);
  d.state = "watching";
  d.nMatch = bBig ? 0 : m->nId;
  if (d.route != "tab") {
    StartStream(d, bBig ? kBigScreen : m->nId);
    return;
  }
  // a page that plays in its tab watches there: its seat is told what to show (PumpWatch)
  StartTab(d);
  if (StreamSeat *t = StreamFor(d.nId)) {
    t->nWatch = bBig ? kBigScreen : m->nId;
    t->nFollowing = 0;
    t->qwFollowed = 0;
  }
}

// Tells a page's watching seat which match to show, and the page which it is.
void Follow(StreamSeat &t, Device &seat, int nMatch)
{
  t.nFollowing = nMatch;
  t.qwFollowed = PortNowMs();
  SendJson(seat, "{\"t\":\"follow\",\"match\":" + std::to_string(nMatch) + "}");
  if (Device *p = Dev(t.nFor)) {
    p->nMatch = nMatch;
    g_bDirty = true;
  }
}

// A watching seat's match: the one it asked for, told again while it is not watching yet; when it
// has been shown to its end, the page goes back to the matches and the seat goes. The Big Screen
// goes to a live match, and between them: to another match with a fight when its own has had none
// for 20 s, no sooner than 30 s after it last went; to the next once its own has been shown.
void PumpWatch(StreamSeat &t)
{
  Device *seat = Dev(t.nDev);
  if (seat == NULL || seat->bGone)
    return;
  const bool bWatching = seat->state == "watching";
  const uint64_t qwNow = PortNowMs();
  auto live = [](const Match *m) { return m != NULL && (m->state == "live" || m->state == "starting"); };
  if (t.nWatch > 0) {
    Match *m = MatchOf(t.nWatch);
    if (!bWatching && t.qwFollowed != 0 && !live(m)) {
      Log("%s has watched match %d to its end", t.name.c_str(), t.nWatch);
      if (Device *p = Dev(t.nFor))
        if (p->state == "watching") {
          p->state = "joined";
          p->nMatch = 0;
        }
      EndWatch(t);
      g_bDirty = true;
    } else if (!bWatching && live(m) && qwNow - t.qwFollowed > 10000) {
      Follow(t, *seat, t.nWatch);
    }
    return;
  }
  Match *cur = MatchOf(t.nFollowing);
  Match *hot = NULL, *newest = NULL;
  for (auto &kv : g_matches) {
    Match &m = *kv.second;
    if (m.state != "live")
      continue;
    if (newest == NULL || m.nId > newest->nId)
      newest = &m;
    if (qwNow - m.qwHeat < 5000 && (hot == NULL || m.fHeat > hot->fHeat))
      hot = &m;
  }
  if (!bWatching) {
    Match *go = live(cur) ? cur : hot != NULL ? hot : newest;
    if (go != NULL && (go != cur || qwNow - t.qwFollowed > 10000)) {
      Follow(t, *seat, go->nId);
    } else if (go == NULL && t.nFollowing != 0) {
      // nothing being played: the page waits for the next match
      t.nFollowing = 0;
      if (Device *p = Dev(t.nFor)) {
        p->nMatch = 0;
        g_bDirty = true;
      }
    }
  } else if (live(cur) && hot != NULL && hot != cur && qwNow - cur->qwHeat > 20000 && qwNow - t.qwFollowed > 30000) {
    Log("the Big Screen goes from match %d to match %d, where the fighting is", cur->nId, hot->nId);
    Follow(t, *seat, hot->nId);
  }
}

// A page that plays in its tab: the token it starts its Hyperion with (--lan-tab
// <device>:<token>), given when the page says so and again while that Hyperion is not in.
void StartTab(Device &w)
{
  StreamSeat *t = StreamFor(w.nId);
  if (t != NULL && !t->bTab) {
    StopStream(*t);
    t = NULL;
  }
  if (t == NULL) {
    StreamSeat s;
    s.bTab = s.bTabOn = true;
    s.token = Token();
    s.name = w.name;
    s.nFor = w.nId;
    s.qwStarted = PortNowMs();
    g_streams.push_back(s);
    t = &g_streams.back();
    Log("%s plays in the browser", w.name.c_str());
  }
  const Device *seat = Dev(t->nDev);
  if ((seat == NULL || seat->bGone) && (t->qwTabSaid == 0 || PortNowMs() - t->qwTabSaid > 10000)) {
    t->qwTabSaid = PortNowMs();
    SendJson(w, "{\"t\":\"tab\",\"token\":" + PortJsonQuote(t->token) + ",\"port\":" + std::to_string(LanNet::GamePort()) + "}");
  }
}

// A page's watching ended: a seat on this machine goes; one in the page's tab stays, for the
// page's next match.
void EndWatch(StreamSeat &t)
{
  if (!t.bTab) {
    StopStream(t);
    return;
  }
  t.nWatch = 0;
  t.nFollowing = 0;
  t.qwFollowed = 0;
}

void StopStream(StreamSeat &t)
{
  t.bTabOn = false;
  if (t.hProcess == NULL)
    return;
  if (WaitForSingleObject(t.hProcess, 0) != WAIT_OBJECT_0)
    TerminateProcess(t.hProcess, 0);
  CloseHandle(t.hProcess);
  t.hProcess = NULL;
}

// The seats' Hyperions that have ended, or were stopped, are forgotten; so is a browser's device
// gone with its seat (Gone keeps it while its seat plays).
void PumpStreams(void)
{
  for (size_t i = 0; i < g_streams.size();) {
    StreamSeat &t = g_streams[i];
    if (t.hProcess != NULL && WaitForSingleObject(t.hProcess, 0) == WAIT_OBJECT_0) {
      Log("%s's seat on this machine has ended", t.name.c_str());
      StopStream(t);
    }
    // a tab's seat: its token said again while its Hyperion is not in, and gone with its page
    if (t.bTabOn) {
      Device *p = Dev(t.nFor), *seat = Dev(t.nDev);
      if (p != NULL && !p->bGone)
        StartTab(*p);
      else if (seat == NULL || seat->bGone)
        StopStream(t);
    }
    if (SeatOn(t) && t.nWatch != 0)
      PumpWatch(t);
    if (!SeatOn(t)) {
      if (Device *p = Dev(t.nDev))
        Gone(*p, "");
      g_streams.erase(g_streams.begin() + static_cast<std::ptrdiff_t>(i));
      g_bDirty = true;
    } else {
      ++i;
    }
  }
}

// The room's note in the data folder, lan_room.json: its process and its console's key, so that
// a console started again in this folder (Hyperion restarted while its room waited a minute for
// it) takes the room back instead of starting a second, which could not have the port
// (port_front_lan.cpp, TakeBackRoom). A room with no console writes none.
void RoomNoteWrite(void)
{
  if (g_key.empty())
    return;
  // the key lets in the room's admin, so the note is this user's alone to read
  char sz[64];
  snprintf(sz, sizeof sz, "{\"pid\":%lu,\"key\":\"", static_cast<unsigned long>(GetCurrentProcessId()));
  PortWritePrivateFile(PortLogFile("lan_room.json"), (sz + g_key + "\"}\n").c_str());
}

// Removes the note while it is still this room's.
void RoomNoteRemove(void)
{
  if (g_key.empty())
    return;
  const std::string path = PortLogFile("lan_room.json");
  std::string text;
  if (FILE *f = fopen(path.c_str(), "r")) {
    char sz[256];
    const size_t n = fread(sz, 1, sizeof sz - 1, f);
    fclose(f);
    text.assign(sz, n);
  }
  PortJson j;
  if (PortJsonParse(text.c_str(), &j) && static_cast<DWORD>(j["pid"].Num()) == GetCurrentProcessId())
    DeleteFileA(path.c_str());
}

std::string MachineName(void)
{
  const std::string name = LanNet::HostName();
  return name.empty() ? "this machine" : name;
}

}  // namespace LanRoom
