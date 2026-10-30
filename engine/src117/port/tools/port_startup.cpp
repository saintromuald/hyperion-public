// port_startup.cpp -- the start-up log (port_startup.h): hyperion_startup.log in the data folder,
// a line for each step of the start from the program's first line to the first frame shown,
// under a header of what the build and the system are (port_sysinfo.cpp). Each line is written
// as it comes, so a start that hangs or is ended from outside still says how far it got.
//
// A start that cannot go on says so in the error window (port_errorwin.cpp), which names the log
// and has what a player copies to ask for help, made from the log (port_share.cpp). The log stays
// open for the session, which ends in it ("ended", or "crashed" with the report's folder), so
// the next start can tell a session or a start that stopped with no word, and says so once. The
// previous start's log moves to logs\ as the new one opens, and the last five are kept. A
// watchdog notes a start with no step for 30 seconds, with the main thread's stack. SDL's own
// messages during the start go in too.
//
// HYPERION_TEST_STARTUP=<stage>:fail or <stage>:stall makes that step fail, or wait 40 seconds,
// for the tests of what a player is shown.
#include "port_startup.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_crash.h"    // PortCrashThreadStack
#include "port_errorwin.h"
#include "port_share.h"    // PortShareStartup
#include "port_sysinfo.h"
#include "../platform/port_paths.h"  // PortLogFile, PortDataFolder

namespace {

const int KEEP = 5;            // the earlier start-up logs logs\ keeps
const int STALL_SECONDS = 30;  // a start with no step for this long is noted

std::chrono::steady_clock::time_point g_t0;
std::mutex g_lock;
std::string g_pending;  // the lines before the log is open
std::string g_written;  // what the log holds, for the error window's text
HANDLE g_hLog = INVALID_HANDLE_VALUE;
char g_szPath[MAX_PATH];
char g_szWhy[512];  // why the log is not in the data folder, when it is not
bool g_bOpen, g_bPlayer, g_bSdl = true;
std::atomic<bool> g_bReady, g_bDone, g_bEnded;
std::atomic<int> g_nSteps, g_nWaits;
std::atomic<long long> g_qwLastStep;  // milliseconds since the start
char g_szStage[64] = "entry";
#if defined(_WIN32)
HANDLE g_hMain;
#endif

long long Elapsed(void)
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_t0).count();
}

bool Quiet(void)
{
  return GetFileAttributesA("hyperion_quiet.flag") != INVALID_FILE_ATTRIBUTES ||
         GetEnvironmentVariableA("HYPERION_QUIET", NULL, 0) != 0;
}

void WriteRaw(const std::string &s)
{
  // the header and the last of the steps kept when a start writes a great deal
  g_written += s;
  if (g_written.size() > 256 * 1024)
    g_written.erase(16 * 1024, g_written.size() - 128 * 1024);
  if (g_hLog == INVALID_HANDLE_VALUE)
    return;
  DWORD dwWrote = 0;
  WriteFile(g_hLog, s.data(), static_cast<DWORD>(s.size()), &dwWrote, NULL);
}

void Put(const char *pszStage, const std::string &text)
{
  char szHead[96];
  const long long ms = Elapsed();
  snprintf(szHead, sizeof szHead, "[%6lld.%03lld] %s: ", ms / 1000, ms % 1000, pszStage);
  std::string line = szHead + text;
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
    line.pop_back();
  line += "\r\n";
  std::lock_guard<std::mutex> hold(g_lock);
  if (g_bOpen)
    WriteRaw(line);
  else
    g_pending += line;
}

std::string Format(const char *pszFormat, va_list args)
{
  char sz[2048];
  const int n = vsnprintf(sz, sizeof sz, pszFormat, args);
  return n < 0 ? std::string() : std::string(sz);
}

std::string Stamp(void)
{
  SYSTEMTIME st;
  GetLocalTime(&st);
  char sz[64];
  snprintf(sz, sizeof sz, "%04u%02u%02u_%02u%02u%02u_%lu", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
           st.wSecond, GetCurrentProcessId());
  return sz;
}

std::string Folder(const char *pszPath)
{
  std::string s = pszPath;
  const size_t at = s.find_last_of("\\/");
  return at == std::string::npos ? std::string() : s.substr(0, at + 1);
}

std::string ReadAll(const char *pszPath)
{
  std::string s;
  if (FILE *f = fopen(pszPath, "rb")) {
    char buf[8192];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;)
      s.append(buf, n);
    fclose(f);
  }
  return s;
}

// What an earlier log says: its stamp from the header, and how its start and session ended.
struct Earlier {
  std::string stamp;
  bool bPlayer = false, bComplete = false, bFailed = false, bCrashed = false, bEnded = false;
};

Earlier Read(const std::string &text)
{
  Earlier e;
  unsigned y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
  unsigned long pid = 0;
  const size_t at = text.find("log, ");
  if (at != std::string::npos &&
      sscanf(text.c_str() + at, "log, %u-%u-%u %u:%u:%u, process %lu", &y, &mo, &d, &h, &mi, &s, &pid) == 7) {
    char sz[64];
    snprintf(sz, sizeof sz, "%04u%02u%02u_%02u%02u%02u_%lu", y, mo, d, h, mi, s, pid);
    e.stamp = sz;
  }
  e.bPlayer = text.find("a player's start") != std::string::npos;
  e.bComplete = text.find("] complete: ") != std::string::npos;
  e.bFailed = text.find("] failed: ") != std::string::npos;
  e.bCrashed = text.find("] crashed: ") != std::string::npos;
  e.bEnded = text.find("] ended: ") != std::string::npos;
  return e;
}

// The logs\ folder's start-up logs, oldest first, down to the newest KEEP.
void Prune(const std::string &folder)
{
  std::vector<std::string> names;
  WIN32_FIND_DATAA fd;
  HANDLE hFind = FindFirstFileA((folder + "startup_*.log").c_str(), &fd);
  if (hFind == INVALID_HANDLE_VALUE)
    return;
  do
    names.push_back(fd.cFileName);
  while (FindNextFileA(hFind, &fd));
  FindClose(hFind);
  std::sort(names.begin(), names.end());
  for (size_t i = 0; i + KEEP < names.size(); i++)
    DeleteFileA((folder + names[i]).c_str());
}

// The session's own logs, copied beside the start-up log of a session that stopped with no
// word, before this one's start writes over them.
void KeepSessionLogs(const std::string &to)
{
  const std::string from = PortDataFolder();
  WIN32_FIND_DATAA fd;
  HANDLE hFind = FindFirstFileA((from + "hyperion_*.log").c_str(), &fd);
  if (hFind == INVALID_HANDLE_VALUE)
    return;
  do {
    // the large logs only a developer turns on
    if (strcmp(fd.cFileName, "hyperion_gpu_debug.log") == 0 || strcmp(fd.cFileName, "hyperion_startup.log") == 0)
      continue;
    CopyFileA((from + fd.cFileName).c_str(), (to + fd.cFileName).c_str(), FALSE);
  } while (FindNextFileA(hFind, &fd));
  FindClose(hFind);
}

bool OpenAt(const char *pszPath)
{
  g_hLog = CreateFileA(pszPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (g_hLog == INVALID_HANDLE_VALUE)
    return false;
  lstrcpynA(g_szPath, pszPath, MAX_PATH);
  return true;
}

// A folder the log can be written in when the data folder cannot be: the system's own for
// temporary files.
bool OpenInTemp(void)
{
  char szTemp[MAX_PATH];
  const DWORD n = GetTempPathA(sizeof szTemp, szTemp);
  if (n == 0 || n > MAX_PATH - 40)
    return false;
  lstrcatA(szTemp, "Hyperion\\");
  CreateDirectoryA(szTemp, NULL);
  lstrcatA(szTemp, ("startup_" + Stamp() + ".log").c_str());
  return OpenAt(szTemp);
}

void Header(const char *pszWhat)
{
  SYSTEMTIME st;
  GetLocalTime(&st);
  char sz[256];
  snprintf(sz, sizeof sz, "Hyperion start-up log, %04u-%02u-%02u %02u:%02u:%02u, process %lu\r\n%s\r\n", st.wYear,
           st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId(), pszWhat);
  std::string head = sz;
  if (g_szWhy[0] != 0)
    head += std::string(g_szWhy) + "\r\n";
  PortSysInfoGather(g_bSdl);
  head += "\r\n" + PortSysInfoText() + "\r\n";
  WriteRaw(head);
}

// The error window over how an earlier start or session ended, a wait on the player.
void Say(const char *pszTitle, const std::string &what, const std::string &log, const char *pszOpen)
{
  if (Quiet())
    return;
  g_nWaits++;
  PortErrorWindow(pszTitle, what, PortShareStartup(pszTitle, log), pszOpen);
  g_nWaits--;
  g_qwLastStep = Elapsed();
}

// The watchdog: a start with no step for STALL_SECONDS, waits on the player aside, is noted once
// for that step, with the main thread's stack.
DWORD WINAPI Watch(LPVOID)
{
  int nNoted = -1;
  while (!g_bDone && !g_bEnded) {
    Sleep(1000);
    if (g_nWaits > 0) {
      g_qwLastStep = Elapsed();
      continue;
    }
    const int nStep = g_nSteps;
    if (nStep == nNoted || Elapsed() - g_qwLastStep < STALL_SECONDS * 1000LL || g_bDone)
      continue;
    nNoted = nStep;
    char szStage[64];
    {
      std::lock_guard<std::mutex> hold(g_lock);
      lstrcpynA(szStage, g_szStage, sizeof szStage);
    }
    std::string text = "no step for " + std::to_string(STALL_SECONDS) + " s since \"" + szStage + "\"";
#if defined(_WIN32)
    static char s_szStack[8192];
    if (PortCrashThreadStack(g_hMain, s_szStack, sizeof s_szStack) > 0)
      text += "; the main thread is in:\r\n" + std::string(s_szStack);
#endif
    Put("stalled", text);
  }
  return 0;
}

void AtExit(void) { PortStartupEnded(0); }

// SDL's messages, put in the log while the start lasts, then passed on as before
SDL_LogOutputFunction g_pfnSdlLog;
void *g_pSdlLogData;

void SDLCALL SdlLog(void *pData, int nCategory, SDL_LogPriority priority, const char *pszMessage)
{
  (void)pData;
  if (!g_bDone && !g_bEnded && pszMessage != NULL) {
    static const char *const s_apszLevels[] = {"", "trace", "verbose", "debug", "info", "warning", "error", "critical"};
    const int nLevel = priority >= 0 && priority < 8 ? priority : 0;
    char sz[64];
    snprintf(sz, sizeof sz, "SDL's %s (category %d)", s_apszLevels[nLevel], nCategory);
    Put("sdl", std::string(sz) + ": " + PortSysInfoMask(pszMessage));
  }
  if (g_pfnSdlLog != NULL)
    g_pfnSdlLog(g_pSdlLogData, nCategory, priority, pszMessage);
}

// The test hook (above): this step's stage named with :fail or :stall
void TestHook(const char *pszStage)
{
  static char s_szHook[96] = "?";
  if (s_szHook[0] == '?' && GetEnvironmentVariableA("HYPERION_TEST_STARTUP", s_szHook, sizeof s_szHook) == 0)
    s_szHook[0] = 0;
  const char *pszColon = strrchr(s_szHook, ':');
  if (pszColon == NULL || static_cast<size_t>(pszColon - s_szHook) != strlen(pszStage) ||
      strncmp(s_szHook, pszStage, pszColon - s_szHook) != 0)
    return;
  s_szHook[0] = 0;  // once
  if (strcmp(pszColon + 1, "stall") == 0) {
    Put("test", std::string("HYPERION_TEST_STARTUP stalls the start at ") + pszStage);
    Sleep(40000);
  } else {
    PortStartupFail(pszStage, "HYPERION_TEST_STARTUP failed this step, for a test.");
  }
}

}  // namespace

void PortStartupBegin(void)
{
  g_t0 = std::chrono::steady_clock::now();
  g_qwLastStep = 0;
#if defined(_WIN32)
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_hMain, 0, FALSE,
                  DUPLICATE_SAME_ACCESS);
  // SDL3.dll is loaded on its first call (the link's /DELAYLOAD), so its absence is said here
  // rather than by Windows' loader, which names no folder: an exe run from inside its zip has
  // been copied out alone
  if (LoadLibraryA("SDL3.dll") == NULL) {
    g_bSdl = false;
    const DWORD dwError = GetLastError();
    char szExe[MAX_PATH] = "";
    GetModuleFileNameA(NULL, szExe, sizeof szExe);
    PortStartupFail("entry",
                    "SDL3.dll, which Hyperion.exe needs beside it, is missing or could not be loaded (error %lu) "
                    "in %s. Extract the whole zip into a folder of its own and start Hyperion.exe from there; "
                    "if SDL3.dll is there, an antivirus may have taken it.",
                    dwError, PortSysInfoMask(Folder(szExe).c_str()).c_str());
  }
#endif
  atexit(AtExit);
  SDL_GetLogOutputFunction(&g_pfnSdlLog, &g_pSdlLogData);
  SDL_SetLogOutputFunction(SdlLog, NULL);
  Put("entry", std::string("started: ") + PortSysInfoMask(GetCommandLineA()));
  TestHook("entry");
}

// bSay: whether how the previous start or session ended is said, which a start that is failing
// leaves to its log, and which is all a start that fails before its own open says
static void Open(bool bPlayer, bool bSay, const char *pszWhat = NULL)
{
  if (g_bOpen)
    return;
  g_bPlayer = bPlayer;
  const std::string path = PortLogFile("hyperion_startup.log");
  const std::string data = Folder(path.c_str());
  const std::string logs = data + "logs\\";
  // the previous start's log, kept under logs\ with the stamp of its start
  const std::string text = ReadAll(path.c_str());
  Earlier earlier;
  std::string kept;
  if (!text.empty()) {
    earlier = Read(text);
    CreateDirectoryA(logs.c_str(), NULL);
    kept = logs + "startup_" + (earlier.stamp.empty() ? Stamp() + "_earlier" : earlier.stamp) + ".log";
    if (!MoveFileExA(path.c_str(), kept.c_str(), MOVEFILE_REPLACE_EXISTING))
      kept.clear();
    Prune(logs);
  }
  if (!OpenAt(path.c_str())) {
    const DWORD dwError = GetLastError();
    snprintf(g_szWhy, sizeof g_szWhy, "The data folder %s cannot be written (error %lu); this log is in the "
             "system's temporary folder instead.", PortSysInfoMask(data.c_str()).c_str(), dwError);
    OpenInTemp();
  }
  {
    std::lock_guard<std::mutex> hold(g_lock);
    g_bOpen = true;
    Header(pszWhat != NULL ? pszWhat : bPlayer ? "a player's start" : "a start for a script, a test or a tool");
    WriteRaw(g_pending);
    g_pending.clear();
  }
  if (g_szWhy[0] != 0)
    Put("data folder", g_szWhy);
  else
    Put("data folder", PortSysInfoMask(data.c_str()) + ", written");
#if defined(_WIN32)
  CloseHandle(CreateThread(NULL, 0, Watch, NULL, 0, NULL));
#else
  CreateThread(NULL, 0, Watch, NULL, 0, NULL);
#endif
  if (kept.empty() || !earlier.bPlayer || !bPlayer || !bSay)
    return;
  // how the previous start or session ended, said once
  if (!earlier.bComplete && !earlier.bFailed && !earlier.bCrashed) {
    Put("earlier", "the last start did not finish; its log is " + PortSysInfoMask(kept.c_str()));
    Say("The last start of Hyperion did not finish",
        "It stopped, or was stopped, before its menus came up. Its log is kept in\n" + kept, text, kept.c_str());
  } else if (earlier.bComplete && !earlier.bEnded && !earlier.bCrashed) {
    const std::string folder = data + "crashes\\" + (earlier.stamp.empty() ? Stamp() : earlier.stamp) + "_unfinished\\";
    CreateDirectoryA((data + "crashes").c_str(), NULL);
    CreateDirectoryA(folder.c_str(), NULL);
    CopyFileA(kept.c_str(), (folder + "hyperion_startup.log").c_str(), FALSE);
    KeepSessionLogs(folder);
    Put("earlier", "the last session ended with no word; its logs are kept in " + PortSysInfoMask(folder.c_str()));
    Say("Hyperion stopped unexpectedly the last time it ran",
        "It ended with no crash report, as when it is ended from outside or the system stops it. Its logs are kept "
        "in\n" + folder, text, (folder + "hyperion_startup.log").c_str());
  }
}

void PortStartupOpen(bool bPlayer) { Open(bPlayer, true); }

void PortStartupStep(const char *pszStage, const char *pszFormat, ...)
{
  va_list args;
  va_start(args, pszFormat);
  const std::string text = Format(pszFormat, args);
  va_end(args);
  {
    std::lock_guard<std::mutex> hold(g_lock);
    lstrcpynA(g_szStage, pszStage, sizeof g_szStage);
  }
  g_qwLastStep = Elapsed();
  g_nSteps++;
  Put(pszStage, text);
  TestHook(pszStage);
}

void PortStartupWait(bool bOn)
{
  g_nWaits += bOn ? 1 : -1;
  g_qwLastStep = Elapsed();
}

void PortStartupReady(void)
{
  if (!g_bReady.exchange(true))
    PortStartupStep("ready", "the menus or the game come next");
}

void PortStartupFrame(void)
{
  if (!g_bReady || g_bDone.exchange(true))
    return;
  char sz[64];
  snprintf(sz, sizeof sz, "the first frame shown, %lld ms after the start", Elapsed());
  Put("complete", sz);
  PortCrashTest("menus");
}

bool PortStartupDone(void) { return g_bDone; }

bool PortStartupPending(void) { return !g_bDone && !g_bEnded; }

void PortStartupFail(const char *pszStage, const char *pszFormat, ...)
{
  va_list args;
  va_start(args, pszFormat);
  const std::string text = Format(pszFormat, args);
  va_end(args);
  Put("failed", std::string(pszStage) + ": " + PortSysInfoMask(text.c_str()));
  g_bEnded = true;
  if (!g_bOpen)
    Open(false, false, "a start that ended before it read its settings");
  if (!Quiet()) {
    const bool bLog = g_hLog != INVALID_HANDLE_VALUE;
    std::string written;
    {
      std::lock_guard<std::mutex> hold(g_lock);
      written = g_written;
    }
    PortErrorWindow("Hyperion could not start",
                    text + (bLog ? std::string("\n\nThe start-up log has the details:\n") + g_szPath
                                 : std::string("\n\nNo log could be written.")),
                    PortShareStartup("Hyperion could not start", written), bLog ? g_szPath : NULL);
  }
  ExitProcess(1);
}

void PortStartupOtherCopy(void)
{
  Put("ended", "another Hyperion is running on this data folder, so this copy ends");
  std::lock_guard<std::mutex> hold(g_lock);
  g_bEnded = true;
  if (g_bOpen)
    return;
  // under logs\, so the running copy's own log stays where it is
  const std::string logs = Folder(PortLogFile("hyperion_startup.log")) + "logs\\";
  CreateDirectoryA(logs.c_str(), NULL);
  if (!OpenAt((logs + "startup_" + Stamp() + ".log").c_str()))
    return;
  g_bOpen = true;
  Header("a start that found another copy running");
  WriteRaw(g_pending);
  g_pending.clear();
}

void PortStartupCrashed(const char *pszFolder)
{
  if (g_bEnded.exchange(true))
    return;
  Put("crashed", std::string("the report is in ") + PortSysInfoMask(pszFolder));
}

void PortStartupEnded(int nCode)
{
  if (g_bEnded.exchange(true))
    return;
  char sz[64];
  snprintf(sz, sizeof sz, "the program ended, code %d", nCode);
  Put("ended", sz);
}

const char *PortStartupLogPath(void) { return g_szPath; }
