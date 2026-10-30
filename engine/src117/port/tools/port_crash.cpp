// port_crash.cpp -- Hyperion's crash report, installed by PortInstallCrashReporter
// (docs/modules/tools.md; "Crashes and traces").
//
// A fault the game does not handle writes a folder of its own under crashes\ in the data folder,
// crashes\<stamp>_<process>\, and ends the process. The folder holds report.txt: what happened
// and where, the build, the system and the run (port_sysinfo.cpp), what the session was doing
// (port_crashnotes.cpp), the faulting thread's stack (function, file and line where the PDB the
// exe names is found, else a place in its module), the last lines of the logs, the registers, a
// scan of the stack for return addresses in case the unwind stops early, and the modules loaded.
// Each part is on the disk once made, so a fault in the reporter still leaves the parts before
// it. Beside it go report.json, the same for a program to read, a minidump Visual Studio opens,
// and copies of the session's logs; hyperion_crash.log in the data folder is a copy of the latest
// report.txt, and the last ten folders are kept. The window that says so, with what a player
// copies to ask for help, is shown by a copy of Hyperion this one starts for it (--crash-report,
// port_errorwin.cpp), since a process that has crashed cannot safely show one. hyperion_quiet.flag
// in the working folder, or HYPERION_QUIET, keeps the window away, for the replay harness and the
// AI lab.
//
// Besides faults: abort(), std::terminate, the C library's invalid-parameter and pure-call
// handlers, and 1.17's own fatal errors after the start each write one, named for what they are.
// Off Windows the fault is a signal, reported from its handler with the C library's backtrace and
// the symbols the program exports, and no minidump. In a browser the port's own exceptions are
// reported to the console.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <shellapi.h>  // ShellExecuteA
#include <tlhelp32.h>
#include <signal.h>
#include <typeinfo>
#else
#include <windows.h>  // compat/windows.h: the file calls
#if !defined(__EMSCRIPTEN__)
#include <signal.h>
#include <sys/wait.h>  // waitpid
#include <unistd.h>    // fork, execv
#endif
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <chrono>
#include <exception>
#include <stdexcept>
#include "port_crash.h"
#include "port_crashnotes.h"  // PortCrashNotesNow
#include "port_startup.h"     // PortStartupPending, PortStartupFail, PortStartupCrashed, PortStartupLogPath
#include "port_sysinfo.h"     // PortSysInfoTextNow, PortSysInfoJsonNow
#include "../platform/port_paths.h"  // PortLogFile

// The exceptions the port raises on purpose, with the caller's address.
static const DWORD ORIGINAL_CODE_REACHED = 0xE0520002;  // port_standalone.cpp
// What the reporter writes for an end that is no fault: abort, terminate, the C library's
static const DWORD HYPERION_STOPPED = 0xE0520010;
static const DWORD CPP_EXCEPTION = 0xE06D7363;  // MSVC's throw

// ---------------------------------------------------------------------------
// What the report copies: the last lines of the logs and the session's notes, kept as they come.

namespace {

const int NOTE_COUNT = 64;
const int NOTE_SIZE = 300;
char g_aszNotes[NOTE_COUNT][NOTE_SIZE];
std::atomic<int> g_nNotes;
const std::chrono::steady_clock::time_point g_t0 = std::chrono::steady_clock::now();

const int SESSION_COUNT = 8;
struct SessionNote {
  char szName[32];
  char szText[1600];
};
SessionNote g_aSession[SESSION_COUNT];

bool Quiet(void)
{
  return GetFileAttributesA("hyperion_quiet.flag") != INVALID_FILE_ATTRIBUTES ||
         GetEnvironmentVariableA("HYPERION_QUIET", NULL, 0) != 0;  // an AI lab worker's
}

const char *BaseName(const char *psz)
{
  const char *pszName = psz;
  for (const char *p = psz; *p; p++)
    if (*p == '\\' || *p == '/')
      pszName = p + 1;
  return pszName;
}

}  // namespace

void __cdecl PortCrashNote(const char *psz)
{
  const int n = g_nNotes.fetch_add(1);
  char *pszTo = g_aszNotes[n % NOTE_COUNT];
  const long long ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_t0).count();
  int i = snprintf(pszTo, NOTE_SIZE, "[%6lld.%03lld] ", ms / 1000, ms % 1000);
  if (i < 0)
    i = 0;
  for (int k = 0; psz[k] != 0 && psz[k] != '\r' && psz[k] != '\n' && i < NOTE_SIZE - 1; k++)
    pszTo[i++] = psz[k];
  pszTo[i] = 0;
}

void __cdecl PortCrashSession(const char *pszName, const char *pszText)
{
  SessionNote *pNote = NULL;
  for (SessionNote &s : g_aSession)
    if (s.szName[0] == 0 || strcmp(s.szName, pszName) == 0) {
      pNote = &s;
      break;
    }
  if (pNote == NULL)
    return;
  // the text emptied first, so a report written meanwhile reads a whole one or none
  pNote->szText[0] = 0;
  snprintf(pNote->szName, sizeof pNote->szName, "%s", pszName);
  snprintf(pNote->szText, sizeof pNote->szText, "%s", pszText);
}

#if !defined(__EMSCRIPTEN__)

// ---------------------------------------------------------------------------
// The report's writing, the same on every system: a file per part, each part written to the disk
// as it is made.

namespace {

struct Frame {
  uintptr_t a;
  char szModule[64];
  uintptr_t off;
  char szFunction[256];
  char szFile[96];
  unsigned nLine;
};

const int FRAME_COUNT = 64;
Frame g_aFrames[FRAME_COUNT];
int g_nFrames;

char g_szFolder[MAX_PATH];  // the report's folder, ending in a backslash
#if !defined(_WIN32)
char g_szExe[4096];  // the program's own file, found as the reporter is installed
#endif
char g_szWhat[1200];        // what happened, in a line
char g_szWhere[400];        // where: the module, the place in it, the function
char g_szAccess[96];        // an access violation's read, write or execute, and its address
char g_szThread[160];

// a file the report writes, a part at a time
struct Out {
  HANDLE h = INVALID_HANDLE_VALUE;
  char *buf;
  int cb;
  int n = 0, nWritten = 0;
  Out(char *b, int c) : buf(b), cb(c) {}
};

static char g_textBuf[98304], g_jsonBuf[98304];
Out g_text(g_textBuf, sizeof g_textBuf), g_json(g_jsonBuf, sizeof g_jsonBuf);

void Open(Out &o, const char *pszName)
{
  char szPath[MAX_PATH];
  snprintf(szPath, sizeof szPath, "%s%s", g_szFolder, pszName);
  o.h = CreateFileA(szPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

// A part added to the file, as much of it as there is room for. vsnprintf truncates where
// sprintf_s would call the invalid-parameter handler, which ends the process.
void Put(Out &o, const char *pszFormat, ...)
{
  if (o.n >= o.cb - 1)
    return;
  va_list args;
  va_start(args, pszFormat);
  const int n = vsnprintf(o.buf + o.n, static_cast<size_t>(o.cb - o.n), pszFormat, args);
  va_end(args);
  if (n > 0)
    o.n = n >= o.cb - o.n ? o.cb - 1 : o.n + n;
}

void Flush(Out &o)
{
  if (o.h != INVALID_HANDLE_VALUE && o.n > o.nWritten) {
    DWORD dwWrote = 0;
    WriteFile(o.h, o.buf + o.nWritten, static_cast<DWORD>(o.n - o.nWritten), &dwWrote, NULL);
  }
  o.nWritten = o.n;
}

void Close(Out &o)
{
  Flush(o);
  if (o.h != INVALID_HANDLE_VALUE)
    CloseHandle(o.h);
  o.h = INVALID_HANDLE_VALUE;
}

// A string as JSON, in its quotes.
void PutJson(Out &o, const char *psz)
{
  Put(o, "\"");
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(psz); *p && o.n < o.cb - 8; p++) {
    if (*p == '"' || *p == '\\')
      Put(o, "\\%c", *p);
    else if (*p == '\n')
      Put(o, "\\n");
    else if (*p == '\r')
      Put(o, "\\r");
    else if (*p == '\t')
      Put(o, "\\t");
    else if (*p < 0x20)
      Put(o, "\\u%04x", *p);
    else
      o.buf[o.n++] = static_cast<char>(*p);
  }
  o.buf[o.n] = 0;
  Put(o, "\"");
}

// The folder, made first.
bool Begin(const SYSTEMTIME &st)
{
  CreateDirectoryA(PortLogFile("crashes"), NULL);
  char szName[96];
  snprintf(szName, sizeof szName, "crashes\\%04u%02u%02u_%02u%02u%02u_%lu", st.wYear, st.wMonth, st.wDay, st.wHour,
           st.wMinute, st.wSecond, static_cast<unsigned long>(GetCurrentProcessId()));
  snprintf(g_szFolder, sizeof g_szFolder, "%s\\", PortLogFile(szName));
  CreateDirectoryA(g_szFolder, NULL);
  Open(g_text, "report.txt");
  return g_text.h != INVALID_HANDLE_VALUE;
}

void PutHead(const SYSTEMTIME &st)
{
  Put(g_text, "Hyperion crash report, %04u-%02u-%02u %02u:%02u:%02u, process %lu\r\n\r\n", st.wYear, st.wMonth,
      st.wDay, st.wHour, st.wMinute, st.wSecond, static_cast<unsigned long>(GetCurrentProcessId()));
  Put(g_text, "what:   %s\r\nwhere:  %s\r\n", g_szWhat, g_szWhere);
  if (g_szAccess[0] != 0)
    Put(g_text, "access: %s\r\n", g_szAccess);
  Put(g_text, "thread: %s\r\n", g_szThread);
  Flush(g_text);
  Put(g_text, "\r\n%s", PortSysInfoTextNow());
  Flush(g_text);
}

void PutSession(void)
{
  const long long ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_t0).count();
  Put(g_text, "\r\nsession\r\n  running for %lld.%03lld s; its start %s\r\n", ms / 1000, ms % 1000,
      PortStartupPending() ? "had not completed" : "completed");
  char sz[512];
  PortCrashNotesNow(sz, sizeof sz);
  Put(g_text, "  now: %s\r\n", sz);
  for (const SessionNote &s : g_aSession)
    if (s.szName[0] != 0)
      Put(g_text, "  %s: %s\r\n", s.szName, s.szText);
  Flush(g_text);
}

void PutFrames(const char *pszHow)
{
  Put(g_text, "\r\ncall stack (%s):\r\n", pszHow);
  for (int i = 0; i < g_nFrames; i++) {
    const Frame &f = g_aFrames[i];
    Put(g_text, "  #%-2d %s+0x%llX", i, f.szModule, static_cast<unsigned long long>(f.off));
    if (f.szFunction[0] != 0)
      Put(g_text, "  %s", f.szFunction);
    if (f.szFile[0] != 0)
      Put(g_text, " (%s:%u)", f.szFile, f.nLine);
    Put(g_text, "\r\n");
  }
  Flush(g_text);
}

void PutNotes(void)
{
  const int nNotes = g_nNotes;
  if (nNotes <= 0)
    return;
  Put(g_text, "\r\nlast notes (the logs' lines, oldest first):\r\n");
  for (int k = nNotes > NOTE_COUNT ? nNotes - NOTE_COUNT : 0; k < nNotes; k++)
    Put(g_text, "  %s\r\n", g_aszNotes[k % NOTE_COUNT]);
  Flush(g_text);
}

// report.json: what report.txt says, for a program to read (the online report's).
void WriteJson(const SYSTEMTIME &st, DWORD dwCode, const char *pszModules)
{
  Open(g_json, "report.json");
  if (g_json.h == INVALID_HANDLE_VALUE)
    return;
  Put(g_json, "{\n  \"format\": 1,\n  \"time\": \"%04u-%02u-%02uT%02u:%02u:%02u\",\n  \"process\": %lu,\n", st.wYear,
      st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, static_cast<unsigned long>(GetCurrentProcessId()));
  Put(g_json, "  \"code\": \"0x%08lX\",\n  \"what\": ", static_cast<unsigned long>(dwCode));
  PutJson(g_json, g_szWhat);
  Put(g_json, ",\n  \"where\": ");
  PutJson(g_json, g_szWhere);
  Put(g_json, ",\n  \"access\": ");
  PutJson(g_json, g_szAccess);
  Put(g_json, ",\n  \"thread\": ");
  PutJson(g_json, g_szThread);
  Put(g_json, ",\n  \"system\": %s,\n", PortSysInfoJsonNow());
  Put(g_json, "  \"session\": {\n    \"startComplete\": %s", PortStartupPending() ? "false" : "true");
  char sz[512];
  PortCrashNotesNow(sz, sizeof sz);
  Put(g_json, ",\n    \"now\": ");
  PutJson(g_json, sz);
  for (const SessionNote &s : g_aSession)
    if (s.szName[0] != 0) {
      Put(g_json, ",\n    ");
      PutJson(g_json, s.szName);
      Put(g_json, ": ");
      PutJson(g_json, s.szText);
    }
  Put(g_json, "\n  },\n  \"stack\": [");
  for (int i = 0; i < g_nFrames; i++) {
    const Frame &f = g_aFrames[i];
    Put(g_json, "%s\n    {\"module\": ", i == 0 ? "" : ",");
    PutJson(g_json, f.szModule);
    Put(g_json, ", \"offset\": \"0x%llX\", \"function\": ", static_cast<unsigned long long>(f.off));
    PutJson(g_json, f.szFunction);
    Put(g_json, ", \"file\": ");
    PutJson(g_json, f.szFile);
    Put(g_json, ", \"line\": %u}", f.nLine);
  }
  Put(g_json, "\n  ],\n  \"notes\": [");
  const int nNotes = g_nNotes;
  for (int k = nNotes > NOTE_COUNT ? nNotes - NOTE_COUNT : 0, first = 1; k < nNotes; k++, first = 0) {
    Put(g_json, "%s\n    ", first ? "" : ",");
    PutJson(g_json, g_aszNotes[k % NOTE_COUNT]);
  }
  Put(g_json, "\n  ],\n  \"modules\": ");
  PutJson(g_json, pszModules);
  Put(g_json, "\n}\n");
  Close(g_json);
}

// A log copied into the report's folder: whole, or a long one's last 256 KB.
void CopyLog(const char *pszFrom, const char *pszName)
{
  static char s_buf[65536];
  FILE *in = fopen(pszFrom, "rb");
  if (in == NULL)
    return;
  char szTo[MAX_PATH];
  snprintf(szTo, sizeof szTo, "%s%s", g_szFolder, pszName);
  FILE *out = fopen(szTo, "wb");
  if (out != NULL) {
    fseek(in, 0, SEEK_END);
    const long cb = ftell(in);
    const long TAIL = 256 * 1024;
    fseek(in, cb > 4 * TAIL ? cb - TAIL : 0, SEEK_SET);
    if (cb > 4 * TAIL)
      fputs("[the log's last 256 KB]\n", out);
    for (size_t n; (n = fread(s_buf, 1, sizeof s_buf, in)) > 0;)
      fwrite(s_buf, 1, n, out);
    fclose(out);
  }
  fclose(in);
}

// The session's logs, those written since the program started, the start-up log among them.
void CopyLogs(void)
{
  const char *pszStartup = PortStartupLogPath();
  if (pszStartup != NULL && pszStartup[0] != 0)
    CopyLog(pszStartup, "hyperion_startup.log");
  char szData[MAX_PATH];
  snprintf(szData, sizeof szData, "%s", PortLogFile(""));
  char szFind[MAX_PATH];
  snprintf(szFind, sizeof szFind, "%shyperion_*.log", szData);
  WIN32_FIND_DATAA fd;
  HANDLE hFind = FindFirstFileA(szFind, &fd);
  if (hFind == INVALID_HANDLE_VALUE)
    return;
#if defined(_WIN32)
  FILETIME ftStart, ftExit, ftKernel, ftUser;
  const bool bTimes = GetProcessTimes(GetCurrentProcess(), &ftStart, &ftExit, &ftKernel, &ftUser) != 0;
#endif
  do {
    // the large logs only a developer turns on, the start-up log copied above, and this report's
    if (strcmp(fd.cFileName, "hyperion_gpu_debug.log") == 0 || strcmp(fd.cFileName, "hyperion_startup.log") == 0 ||
        strcmp(fd.cFileName, "hyperion_crash.log") == 0)
      continue;
#if defined(_WIN32)
    if (bTimes && CompareFileTime(&fd.ftLastWriteTime, &ftStart) < 0)
      continue;  // an earlier session's
#endif
    char szFrom[MAX_PATH];
    snprintf(szFrom, sizeof szFrom, "%s%s", szData, fd.cFileName);
    CopyLog(szFrom, fd.cFileName);
  } while (FindNextFileA(hFind, &fd));
  FindClose(hFind);
}

// The crashes\ folder down to the newest ten reports' folders.
void Prune(void)
{
  const int KEEP = 10;
  static char s_aszNames[64][64];
  int nNames = 0;
  char szFind[MAX_PATH];
  snprintf(szFind, sizeof szFind, "%s\\*", PortLogFile("crashes"));
  WIN32_FIND_DATAA fd;
  HANDLE hFind = FindFirstFileA(szFind, &fd);
  if (hFind == INVALID_HANDLE_VALUE)
    return;
  do
    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && fd.cFileName[0] != '.' && nNames < 64)
      snprintf(s_aszNames[nNames++], sizeof s_aszNames[0], "%s", fd.cFileName);
  while (FindNextFileA(hFind, &fd));
  FindClose(hFind);
  // the names begin with their stamp, so the oldest sort first
  qsort(s_aszNames, static_cast<size_t>(nNames), sizeof s_aszNames[0],
        [](const void *a, const void *b) { return strcmp(static_cast<const char *>(a), static_cast<const char *>(b)); });
  for (int i = 0; i + KEEP < nNames; i++) {
    char szDir[MAX_PATH];
    snprintf(szDir, sizeof szDir, "%s\\%s\\", PortLogFile("crashes"), s_aszNames[i]);
    snprintf(szFind, sizeof szFind, "%s*", szDir);
    hFind = FindFirstFileA(szFind, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
      do {
        char szFile[MAX_PATH];
        snprintf(szFile, sizeof szFile, "%s%s", szDir, fd.cFileName);
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
          DeleteFileA(szFile);
      } while (FindNextFileA(hFind, &fd));
      FindClose(hFind);
    }
    RemoveDirectoryA(szDir);
  }
}

// The window that says so, in a copy of Hyperion started for it (hyperion.cpp's --crash-report).
// When this process goes on, as after 1.17's own error, whose dialog comes next, it waits for the
// window to be closed. False when no copy could be started.
bool StartReporter(bool bWait)
{
  char szFolder[MAX_PATH];
  snprintf(szFolder, sizeof szFolder, "%s", g_szFolder);
  // without its last separator, which would escape the quote after it
  const size_t n = strlen(szFolder);
  if (n > 0 && (szFolder[n - 1] == '\\' || szFolder[n - 1] == '/'))
    szFolder[n - 1] = 0;
#if defined(_WIN32)
  static wchar_t s_szExe[MAX_PATH], s_szFolder[MAX_PATH], s_szCommand[MAX_PATH * 2 + 32];
  if (GetModuleFileNameW(NULL, s_szExe, MAX_PATH) == 0 ||
      MultiByteToWideChar(CP_ACP, 0, szFolder, -1, s_szFolder, MAX_PATH) == 0)
    return false;
  _snwprintf_s(s_szCommand, _countof(s_szCommand), _TRUNCATE, L"\"%s\" --crash-report \"%s\"", s_szExe, s_szFolder);
  STARTUPINFOW si;
  memset(&si, 0, sizeof si);
  si.cb = sizeof si;
  PROCESS_INFORMATION pi;
  if (!CreateProcessW(s_szExe, s_szCommand, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    return false;
  AllowSetForegroundWindow(pi.dwProcessId);
  if (bWait)
    WaitForSingleObject(pi.hProcess, INFINITE);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
#else
  if (g_szExe[0] == 0)
    return false;
  char szFlag[] = "--crash-report";
  char *argv[] = {g_szExe, szFlag, szFolder, NULL};
  const pid_t pid = fork();
  if (pid < 0)
    return false;
  if (pid == 0) {
    // the new program without the handler's blocked signals, which it would keep
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
    execv(g_szExe, argv);
    _exit(127);
  }
  if (bWait)
    waitpid(pid, NULL, 0);
  return true;
#endif
}

// The end of every report: the latest as hyperion_crash.log, the logs, the start-up log's last
// line, the older folders gone, and the window. bGoesOn: this process goes on after the report.
void Finish(const SYSTEMTIME &st, DWORD dwCode, const char *pszModules, bool bGoesOn)
{
  Close(g_text);
  char szReport[MAX_PATH];
  snprintf(szReport, sizeof szReport, "%sreport.txt", g_szFolder);
  CopyFileA(szReport, PortLogFile("hyperion_crash.log"), FALSE);
  WriteJson(st, dwCode, pszModules);
  PortStartupCrashed(g_szFolder);
  CopyLogs();
  Prune();
  if (Quiet() || StartReporter(bGoesOn))
    return;
  // no copy to show the window: a box of this one's
  char sz[MAX_PATH + 200];
  snprintf(sz, sizeof sz, "Hyperion stopped on an error. A report was saved in\n%s\n\nOpen the folder?", g_szFolder);
  if (MessageBoxA(NULL, sz, "Hyperion", MB_YESNO | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST) != IDYES)
    return;
#if defined(_WIN32)
  char szArgs[MAX_PATH + 16];
  snprintf(szArgs, sizeof szArgs, "/select,\"%s\"", szReport);
  ShellExecuteA(NULL, "open", PortExplorer(), szArgs, NULL, SW_SHOWNORMAL);
#else
  ShellExecuteA(NULL, "open", g_szFolder, NULL, NULL, SW_SHOWNORMAL);
#endif
}

}  // namespace

#endif  // !__EMSCRIPTEN__

#if defined(_WIN32)

// ---------------------------------------------------------------------------
// Windows: the exception filter, the unwind, the symbols and the minidump.

static uintptr_t g_base, g_end;
static uintptr_t g_codeBegin, g_codeEnd;  // the image's code, for the stack's scan
static LONG volatile g_reported;
static DWORD g_dwMainThread;

static bool Ours(uintptr_t p) { return p >= g_base && p < g_end; }
static bool OurCode(uintptr_t p) { return p >= g_codeBegin && p < g_codeEnd; }

static bool Readable(const void *p, size_t cb)
{
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
      (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) == 0 ||
      (mbi.Protect & PAGE_GUARD) != 0)
    return false;
  return reinterpret_cast<uintptr_t>(p) + cb <=
         reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

// DbgHelp, loaded only when a report is written.
typedef BOOL(WINAPI *SymInitializeFn)(HANDLE, PCSTR, BOOL);
typedef DWORD(WINAPI *SymSetOptionsFn)(DWORD);
typedef BOOL(WINAPI *SymFromAddrFn)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
typedef BOOL(WINAPI *SymGetLineFromAddr64Fn)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);
typedef BOOL(WINAPI *MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
typedef HRESULT(WINAPI *GetThreadDescriptionFn)(HANDLE, PWSTR *);

static SymFromAddrFn g_pfnSymFromAddr;
static SymGetLineFromAddr64Fn g_pfnSymGetLine;
static MiniDumpWriteDumpFn g_pfnMiniDump;

static void SymbolsInit(void)
{
  static bool s_bDone;
  if (s_bDone)
    return;
  s_bDone = true;
  HMODULE h = LoadLibraryExA("dbghelp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (h == NULL)
    return;
  SymInitializeFn pfnInit = reinterpret_cast<SymInitializeFn>(GetProcAddress(h, "SymInitialize"));
  SymSetOptionsFn pfnOptions = reinterpret_cast<SymSetOptionsFn>(GetProcAddress(h, "SymSetOptions"));
  g_pfnMiniDump = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(h, "MiniDumpWriteDump"));
  if (pfnInit == NULL || pfnOptions == NULL)
    return;
  pfnOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
  if (!pfnInit(GetCurrentProcess(), NULL, TRUE))
    return;
  g_pfnSymFromAddr = reinterpret_cast<SymFromAddrFn>(GetProcAddress(h, "SymFromAddr"));
  g_pfnSymGetLine = reinterpret_cast<SymGetLineFromAddr64Fn>(GetProcAddress(h, "SymGetLineFromAddr64"));
}

// An address's module, its place there, and its function, file and line where there are symbols.
// A return address names the call before it, so callers pass bReturn for one.
static void Name(Frame *pf, uintptr_t a, bool bReturn)
{
  memset(pf, 0, sizeof *pf);
  pf->a = a;
  HMODULE h = NULL;
  char szPath[MAX_PATH] = "";
  if (Ours(a)) {
    snprintf(pf->szModule, sizeof pf->szModule, "Hyperion.exe");
    pf->off = a - g_base;
  } else if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCSTR>(a), &h) &&
             h != NULL && GetModuleFileNameA(h, szPath, sizeof szPath) != 0) {
    snprintf(pf->szModule, sizeof pf->szModule, "%s", BaseName(szPath));
    pf->off = a - reinterpret_cast<uintptr_t>(h);
  } else {
    snprintf(pf->szModule, sizeof pf->szModule, "?");
    pf->off = a;
  }
  const DWORD64 at = bReturn ? a - 1 : a;
  if (g_pfnSymFromAddr != NULL) {
    union {
      SYMBOL_INFO info;
      char raw[sizeof(SYMBOL_INFO) + 256];
    } sym;
    memset(&sym, 0, sizeof sym);
    sym.info.SizeOfStruct = sizeof(SYMBOL_INFO);
    sym.info.MaxNameLen = 255;
    DWORD64 dwOff = 0;
    // with no PDB, Hyperion's nearest export is not the function the frame is in
    // (tools/crash_symbols.py names it from the build's own)
    if (g_pfnSymFromAddr(GetCurrentProcess(), at, &dwOff, &sym.info) &&
        !(Ours(a) && (sym.info.Flags & SYMFLAG_EXPORT) != 0))
      snprintf(pf->szFunction, sizeof pf->szFunction, "%s+0x%llX", sym.info.Name,
               static_cast<unsigned long long>(dwOff + (bReturn ? 1 : 0)));
  }
  if (g_pfnSymGetLine != NULL) {
    IMAGEHLP_LINE64 line;
    memset(&line, 0, sizeof line);
    line.SizeOfStruct = sizeof line;
    DWORD dwDisp = 0;
    if (g_pfnSymGetLine(GetCurrentProcess(), at, &dwDisp, &line) && line.FileName != NULL) {
      snprintf(pf->szFile, sizeof pf->szFile, "%s", BaseName(line.FileName));
      pf->nLine = line.LineNumber;
    }
  }
}

// The stack walked from a context with the modules' unwind data, up to nMax frames.
static int Unwind(CONTEXT c, DWORD64 *pFrames, int nMax)
{
  int n = 0;
#ifdef _WIN64
  while (n < nMax && c.Rip != 0) {
    pFrames[n++] = c.Rip;
    DWORD64 imageBase = 0;
    PRUNTIME_FUNCTION pFn = RtlLookupFunctionEntry(c.Rip, &imageBase, NULL);
    if (pFn == NULL) {
      // a leaf function: its return address is at the top of the stack
      if (!Readable(reinterpret_cast<void *>(c.Rsp), 8))
        break;
      c.Rip = *reinterpret_cast<DWORD64 *>(c.Rsp);
      c.Rsp += 8;
    } else {
      void *pHandlerData = NULL;
      DWORD64 establisher = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, pFn, &c, &pHandlerData, &establisher, NULL);
    }
    if (!Readable(reinterpret_cast<void *>(c.Rsp), 8))
      break;
  }
#else
  (void)c;
  (void)pFrames;
  (void)nMax;
#endif
  return n;
}

// A C++ exception's type, from what MSVC's throw passes: ".?AVruntime_error@std@@" written
// std::runtime_error. Its what() too where one of its types is std::exception.
static void ThrownType(const EXCEPTION_RECORD *rec, char *psz, size_t cb)
{
  psz[0] = 0;
#ifdef _WIN64
  if (rec->NumberParameters < 4)
    return;
  const uintptr_t base = static_cast<uintptr_t>(rec->ExceptionInformation[3]);
  const int *pInfo = reinterpret_cast<const int *>(rec->ExceptionInformation[2]);
  if (pInfo == NULL || !Readable(pInfo, 16))
    return;
  const int *pTypes = reinterpret_cast<const int *>(base + static_cast<uintptr_t>(pInfo[3]));
  if (!Readable(pTypes, 8) || pTypes[0] < 1)
    return;
  for (int k = 0; k < pTypes[0] && k < 16; k++) {
    const int *pType = reinterpret_cast<const int *>(base + static_cast<uintptr_t>(pTypes[1 + k]));
    if (!Readable(pType, 28))
      return;
    const char *pszDecorated = reinterpret_cast<const char *>(base + static_cast<uintptr_t>(pType[1]) + 16);
    if (!Readable(pszDecorated, 8))
      return;
    if (k == 0) {
      // ".?AV" or ".?AU", then the names innermost first, '@'-separated, ending "@@"
      char szParts[8][64];
      int nParts = 0;
      const char *p = pszDecorated + 4;
      while (*p && *p != '@' && nParts < 8) {
        int i = 0;
        while (*p && *p != '@' && i < 63)
          szParts[nParts][i++] = *p++;
        szParts[nParts++][i] = 0;
        if (*p == '@')
          p++;
      }
      size_t n = 0;
      for (int i = nParts - 1; i >= 0 && n < cb; i--)
        n += static_cast<size_t>(snprintf(psz + n, cb - n, "%s%s", szParts[i], i > 0 ? "::" : ""));
      if (nParts == 0)
        snprintf(psz, cb, "%s", pszDecorated);
    }
    if (strcmp(pszDecorated, ".?AVexception@std@@") == 0) {
      const std::exception *pe = reinterpret_cast<const std::exception *>(
          static_cast<uintptr_t>(rec->ExceptionInformation[1]) + static_cast<uintptr_t>(pType[2]));
      const size_t n = strlen(psz);
      const char *pszWhat = NULL;
      __try {
        pszWhat = pe->what();
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        pszWhat = NULL;
      }
      if (pszWhat != NULL && Readable(pszWhat, 1))
        snprintf(psz + n, cb - n, ": %.400s", pszWhat);
      return;
    }
  }
#else
  (void)rec;
  (void)cb;
#endif
}

// A frame of the reporter's own or the C library's, between an end and the code that made it.
static bool Plumbing(const Frame &f)
{
  static const char *const s_apszFiles[] = {"signal.cpp", "abort.cpp", "terminate.cpp", "invalid_parameter.cpp",
                                            "purevirt.cpp", "throw.cpp", "strcpy_s.cpp", "tcscpy_s.inl"};
  if (strcmp(f.szModule, "Hyperion.exe") != 0)
    return true;  // the system's, as RaiseException
  if (strcmp(f.szFile, "port_crash.cpp") == 0)
    return strncmp(f.szFunction, "PortCrashTest", 13) != 0;
  for (const char *psz : s_apszFiles)
    if (strcmp(f.szFile, psz) == 0)
      return true;
  return false;
}

static const char *CodeName(DWORD dwCode)
{
  switch (dwCode) {
  case EXCEPTION_ACCESS_VIOLATION: return "an access violation";
  case EXCEPTION_STACK_OVERFLOW: return "a stack overflow";
  case EXCEPTION_ILLEGAL_INSTRUCTION: return "an illegal instruction";
  case EXCEPTION_PRIV_INSTRUCTION: return "a privileged instruction";
  case EXCEPTION_INT_DIVIDE_BY_ZERO: return "an integer divided by zero";
  case EXCEPTION_INT_OVERFLOW: return "an integer overflow";
  case EXCEPTION_IN_PAGE_ERROR: return "a page that could not be read in";
  case EXCEPTION_BREAKPOINT: return "a breakpoint";
  case EXCEPTION_DATATYPE_MISALIGNMENT: return "a misaligned access";
  case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "a float divided by zero";
  case EXCEPTION_FLT_INVALID_OPERATION: return "an invalid float operation";
  case CPP_EXCEPTION: return "a C++ exception nothing caught";
  default: return "an exception";
  }
}

static void ThreadName(DWORD dwThread)
{
  char szName[96] = "";
  static GetThreadDescriptionFn s_pfn = reinterpret_cast<GetThreadDescriptionFn>(
      GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadDescription"));
  PWSTR pwsz = NULL;
  if (s_pfn != NULL && SUCCEEDED(s_pfn(GetCurrentThread(), &pwsz)) && pwsz != NULL) {
    WideCharToMultiByte(CP_UTF8, 0, pwsz, -1, szName, sizeof szName, NULL, NULL);
    LocalFree(pwsz);
  }
  snprintf(g_szThread, sizeof g_szThread, "%lu%s%s%s%s", static_cast<unsigned long>(dwThread),
           dwThread == g_dwMainThread ? ", the main thread" : "", szName[0] ? ", \"" : "", szName,
           szName[0] ? "\"" : "");
}

// The modules loaded: name, where, size and file version, a line each.
static void PutModules(char *pszList, size_t cbList)
{
  size_t n = 0;
  pszList[0] = 0;
  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
  if (hSnap == INVALID_HANDLE_VALUE)
    return;
  MODULEENTRY32W me;
  me.dwSize = sizeof me;
  for (BOOL b = Module32FirstW(hSnap, &me); b && n < cbList - 200; b = Module32NextW(hSnap, &me)) {
    // the file version, from the image's version resource: VS_FIXEDFILEINFO follows its signature
    unsigned v[4] = {0, 0, 0, 0};
    HRSRC hRes = FindResourceA(me.hModule, MAKEINTRESOURCEA(1), MAKEINTRESOURCEA(16));
    const BYTE *p = hRes != NULL ? static_cast<const BYTE *>(LockResource(LoadResource(me.hModule, hRes))) : NULL;
    const DWORD cb = hRes != NULL ? SizeofResource(me.hModule, hRes) : 0;
    for (DWORD i = 0; p != NULL && i + 52 <= cb; i += 4)
      if (*reinterpret_cast<const DWORD *>(p + i) == 0xFEEF04BD) {
        const DWORD ms = *reinterpret_cast<const DWORD *>(p + i + 8), ls = *reinterpret_cast<const DWORD *>(p + i + 12);
        v[0] = HIWORD(ms), v[1] = LOWORD(ms), v[2] = HIWORD(ls), v[3] = LOWORD(ls);
        break;
      }
    char szName[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, szName, sizeof szName, NULL, NULL);
    const int k = snprintf(pszList + n, cbList - n, "  %-28s %p  %8lX  %u.%u.%u.%u\r\n", szName,
                           static_cast<void *>(me.modBaseAddr), static_cast<unsigned long>(me.modBaseSize), v[0], v[1],
                           v[2], v[3]);
    if (k > 0)
      n += static_cast<size_t>(k);
  }
  CloseHandle(hSnap);
}

static void Report(EXCEPTION_POINTERS *pInfo, const char *pszWhat, bool bGoesOn)
{
  if (InterlockedCompareExchange(&g_reported, 1, 0) != 0)
    return;
  const EXCEPTION_RECORD *rec = pInfo->ExceptionRecord;
  const CONTEXT *ctx = pInfo->ContextRecord;
#ifdef _WIN64
  const uintptr_t ip = ctx->Rip, sp = ctx->Rsp;
#else
  const uintptr_t ip = ctx->Eip, sp = ctx->Esp;
#endif
  SymbolsInit();
  SYSTEMTIME st;
  GetLocalTime(&st);
  Begin(st);

  // what happened, and where
  if (pszWhat != NULL) {
    snprintf(g_szWhat, sizeof g_szWhat, "%s", pszWhat);
  } else if (rec->ExceptionCode == ORIGINAL_CODE_REACHED && rec->NumberParameters >= 1) {
    Frame from;
    Name(&from, static_cast<uintptr_t>(rec->ExceptionInformation[0]), true);
    snprintf(g_szWhat, sizeof g_szWhat,
             "a pointer of 1.17's data to its code, which has no function of ours (tools/code_map.py), called from "
             "%s+0x%llX %s",
             from.szModule, static_cast<unsigned long long>(from.off), from.szFunction);
  } else if (rec->ExceptionCode == CPP_EXCEPTION) {
    char szType[600];
    ThrownType(rec, szType, sizeof szType);
    snprintf(g_szWhat, sizeof g_szWhat, "%s (0x%08lX)%s%s", CodeName(rec->ExceptionCode),
             static_cast<unsigned long>(rec->ExceptionCode), szType[0] ? ", of type " : "", szType);
  } else {
    snprintf(g_szWhat, sizeof g_szWhat, "%s (0x%08lX)", CodeName(rec->ExceptionCode),
             static_cast<unsigned long>(rec->ExceptionCode));
  }
  // the call stack, unwound from the fault
  DWORD64 aFrames[FRAME_COUNT];
  const int nFrames = Unwind(*ctx, aFrames, FRAME_COUNT);
  for (g_nFrames = 0; g_nFrames < nFrames; g_nFrames++)
    Name(&g_aFrames[g_nFrames], static_cast<uintptr_t>(aFrames[g_nFrames]), g_nFrames > 0);
  Frame at;
  Name(&at, ip, false);
  if (pszWhat != NULL || rec->ExceptionCode == CPP_EXCEPTION)
    for (int i = 0; i < g_nFrames; i++)
      if (!Plumbing(g_aFrames[i])) {
        at = g_aFrames[i];
        break;
      }
  snprintf(g_szWhere, sizeof g_szWhere, "%s+0x%llX%s%s%s%s", at.szModule, static_cast<unsigned long long>(at.off),
           at.szFunction[0] ? "  " : "", at.szFunction, at.szFile[0] ? " in " : "", at.szFile);
  if (at.szFile[0] != 0)
    snprintf(g_szWhere + strlen(g_szWhere), sizeof g_szWhere - strlen(g_szWhere), ":%u", at.nLine);
  if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
    snprintf(g_szAccess, sizeof g_szAccess, "a %s of 0x%p",
             rec->ExceptionInformation[0] == 1 ? "write" : rec->ExceptionInformation[0] == 8 ? "execute" : "read",
             reinterpret_cast<void *>(rec->ExceptionInformation[1]));
  ThreadName(GetCurrentThreadId());
  PutHead(st);
  PutSession();
  PutFrames(pszWhat != NULL ? "from the report's call" : "unwound from the fault");
  PutNotes();

#ifdef _WIN64
  Put(g_text,
      "\r\nrax %016llX rcx %016llX rdx %016llX rbx %016llX\r\n"
      "rsp %016llX rbp %016llX rsi %016llX rdi %016llX\r\n"
      "r8  %016llX r9  %016llX r10 %016llX r11 %016llX\r\n"
      "r12 %016llX r13 %016llX r14 %016llX r15 %016llX\r\n",
      ctx->Rax, ctx->Rcx, ctx->Rdx, ctx->Rbx, ctx->Rsp, ctx->Rbp, ctx->Rsi, ctx->Rdi, ctx->R8, ctx->R9, ctx->R10,
      ctx->R11, ctx->R12, ctx->R13, ctx->R14, ctx->R15);
#endif
  Put(g_text, "\r\nHyperion.exe 0x%p to 0x%p, its code from 0x%p\r\n"
      "return addresses on the stack (a scan, so an old one can show):\r\n",
      reinterpret_cast<void *>(g_base), reinterpret_cast<void *>(g_end), reinterpret_cast<void *>(g_codeBegin));
  const uintptr_t *pStack = reinterpret_cast<const uintptr_t *>(sp);
  for (int i = 0, shown = 0; i < 1024 && shown < 24; i++) {
    if (!Readable(pStack + i, sizeof *pStack))
      break;
    if (!OurCode(pStack[i]))
      continue;
    Frame f;
    Name(&f, pStack[i], true);
    Put(g_text, "  sp+%03X  Hyperion.exe+0x%llX  %s\r\n", i * static_cast<int>(sizeof *pStack),
        static_cast<unsigned long long>(f.off), f.szFunction);
    shown++;
  }
  Flush(g_text);

  static char s_szModules[24576];
  PutModules(s_szModules, sizeof s_szModules);
  Put(g_text, "\r\nmodules (name, where, size, file version):\r\n%s", s_szModules);
  Flush(g_text);

  // a minidump Visual Studio opens, with the stacks of every thread and what they point at
  if (g_pfnMiniDump != NULL) {
    char szPath[MAX_PATH];
    snprintf(szPath, sizeof szPath, "%sminidump.dmp", g_szFolder);
    HANDLE h = CreateFileA(szPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
      MINIDUMP_EXCEPTION_INFORMATION mei;
      mei.ThreadId = GetCurrentThreadId();
      mei.ExceptionPointers = pInfo;
      mei.ClientPointers = FALSE;
      g_pfnMiniDump(GetCurrentProcess(), GetCurrentProcessId(), h,
                    static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo), &mei,
                    NULL, NULL);
      CloseHandle(h);
    }
  }
  Finish(st, rec->ExceptionCode, s_szModules, bGoesOn);
}

// A report for an end that is no fault, from where it is called.
static void Stopped(const char *pszWhat, bool bEnd)
{
  CONTEXT ctx;
  RtlCaptureContext(&ctx);
  EXCEPTION_RECORD rec;
  memset(&rec, 0, sizeof rec);
  rec.ExceptionCode = HYPERION_STOPPED;
#ifdef _WIN64
  rec.ExceptionAddress = reinterpret_cast<void *>(ctx.Rip);
#endif
  EXCEPTION_POINTERS pointers = {&rec, &ctx};
  Report(&pointers, pszWhat, !bEnd);
  if (bEnd)
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(-1));
}

static void __cdecl OnInvalidParameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned, uintptr_t)
{
  Stopped("the C library was given a parameter it does not take", true);
}

static void __cdecl OnPureCall(void) { Stopped("a pure virtual function was called", true); }

static void __cdecl OnAbort(int) { Stopped("abort() was called", true); }

static void OnTerminate(void)
{
  char sz[700] = "std::terminate was called";
  try {
    if (std::exception_ptr p = std::current_exception())
      std::rethrow_exception(p);
  } catch (const std::exception &e) {
    snprintf(sz, sizeof sz, "std::terminate, with an exception of type %s nothing caught: %.400s", typeid(e).name(),
             e.what());
  } catch (...) {
    snprintf(sz, sizeof sz, "std::terminate, with an exception nothing caught");
  }
  Stopped(sz, true);
}

int __cdecl PortCrashThreadStack(void *hThread, char *buf, int cb)
{
  // the frames read while the thread is held, and named once it runs again, since naming them
  // can take a lock the thread may hold
  DWORD64 aFrames[48];
  int nFrames = 0;
  if (SuspendThread(hThread) == static_cast<DWORD>(-1))
    return 0;
  CONTEXT c;
  memset(&c, 0, sizeof c);
  c.ContextFlags = CONTEXT_FULL;
  if (GetThreadContext(hThread, &c))
    nFrames = Unwind(c, aFrames, 48);
  ResumeThread(hThread);
  SymbolsInit();
  int n = 0;
  for (int i = 0; i < nFrames && n < cb - 700; i++) {
    Frame f;
    Name(&f, static_cast<uintptr_t>(aFrames[i]), i > 0);
    const int k = _snprintf_s(buf + n, static_cast<size_t>(cb - n), _TRUNCATE, "  #%-2d %s+0x%llX%s%s", i, f.szModule,
                              static_cast<unsigned long long>(f.off), f.szFunction[0] ? "  " : "", f.szFunction);
    n += k > 0 ? k : 0;
    if (f.szFile[0] != 0) {
      const int k2 = _snprintf_s(buf + n, static_cast<size_t>(cb - n), _TRUNCATE, " (%s:%u)", f.szFile, f.nLine);
      n += k2 > 0 ? k2 : 0;
    }
    const int k3 = _snprintf_s(buf + n, static_cast<size_t>(cb - n), _TRUNCATE, "\r\n");
    n += k3 > 0 ? k3 : 0;
  }
  if (n < cb)
    buf[n] = 0;
  return n;
}

// The report is made only for a fault no handler took, not from a vectored handler, which sees
// every fault first: port_stormbase.cpp reads a block's header under __try, and that read
// faults in the ordinary way of things. What no handler took, the port's own exceptions among
// it, ends the process here, so a crash does not reach Windows Error Reporting.
static LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS *pInfo)
{
  Report(pInfo, NULL, false);
  TerminateProcess(GetCurrentProcess(), static_cast<UINT>(-1));
  return EXCEPTION_EXECUTE_HANDLER;
}

// InitCrashReporter (gds_appcrit.cpp) calls this where the game installed its own.
void __cdecl PortInstallUnhandledFilter(void) { SetUnhandledExceptionFilter(UnhandledFilter); }

void __cdecl PortInstallCrashReporter(void)
{
  HMODULE hSelf = NULL;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCSTR>(&PortInstallCrashReporter), &hSelf);
  if (hSelf) {
    const BYTE *base = reinterpret_cast<const BYTE *>(hSelf);
    const IMAGE_NT_HEADERS *nt =
        reinterpret_cast<const IMAGE_NT_HEADERS *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
    g_base = reinterpret_cast<uintptr_t>(base);
    g_end = g_base + nt->OptionalHeader.SizeOfImage;
    const IMAGE_SECTION_HEADER *pSection = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, pSection++)
      if ((pSection->Characteristics & IMAGE_SCN_CNT_CODE) != 0 && g_codeBegin == 0) {
        g_codeBegin = g_base + pSection->VirtualAddress;
        g_codeEnd = g_codeBegin + pSection->Misc.VirtualSize;
      }
  }
  g_dwMainThread = GetCurrentThreadId();
  // a slice of this thread's stack kept back for the filter, so a stack overflow has the room
  // to load dbghelp and write its report (the POSIX branch's sigaltstack, below)
  ULONG nGuarantee = 64 * 1024;
  SetThreadStackGuarantee(&nGuarantee);
  SetUnhandledExceptionFilter(UnhandledFilter);
  // the ends that are no fault, which would otherwise end the process with no report: the C
  // library's own checks, abort() (std::terminate's on a thread of its own) and terminate here
  _set_invalid_parameter_handler(OnInvalidParameter);
  _set_purecall_handler(OnPureCall);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  signal(SIGABRT, OnAbort);
  std::set_terminate(OnTerminate);
}

// 1.17's error after the start: the report, with the error's words, before its dialog.
static void ReportError(const char *pszText)
{
  char sz[900];
  snprintf(sz, sizeof sz, "1.17's error: %s", pszText);
  Stopped(sz, false);
}

#elif defined(__EMSCRIPTEN__)  // in a browser
// A trap in WebAssembly is no signal: it ends the module, and the page says so with the
// browser's own stack. Here the port's own exceptions are reported, with the last notes and the
// call stack Emscripten reads, to the console and hyperion_crash.log.
#include <emscripten/emscripten.h>

void __cdecl PortCrashRaise(DWORD dwCode, const ULONG_PTR *pArgs, DWORD nArgs)
{
  static char buf[32768];
  int n = snprintf(buf, sizeof buf, "Hyperion crash report\r\n\r\n");
  if (dwCode == ORIGINAL_CODE_REACHED && nArgs >= 1)
    n += snprintf(buf + n, sizeof buf - n,
                  "a pointer of 1.17's data to its code, which has no function of ours (tools/code_map.py), "
                  "called from %p\r\n",
                  reinterpret_cast<void *>(pArgs[0]));
  else
    n += snprintf(buf + n, sizeof buf - n, "exception 0x%08X\r\n", static_cast<unsigned>(dwCode));
  n += snprintf(buf + n, sizeof buf - n, "\r\n%s", PortSysInfoTextNow());
  n = static_cast<int>(strlen(buf));
  n += snprintf(buf + n, sizeof buf - n, "\r\ncall stack:\r\n");
  n += emscripten_get_callstack(EM_LOG_C_STACK, buf + n, static_cast<int>(sizeof buf) - n - 4096);
  n = static_cast<int>(strlen(buf));
  n += snprintf(buf + n, sizeof buf - n, "\r\nlast notes (the logs' lines, oldest first):\r\n");
  const int nNotes = g_nNotes;
  for (int k = nNotes > NOTE_COUNT ? nNotes - NOTE_COUNT : 0; k < nNotes && n < (int)sizeof buf - 512; k++)
    n += snprintf(buf + n, sizeof buf - n, "  %s\r\n", g_aszNotes[k % NOTE_COUNT]);
  emscripten_log(EM_LOG_ERROR, "%s", buf);
  if (FILE *f = fopen(PortLogFile("hyperion_crash.log"), "wb")) {
    fwrite(buf, 1, static_cast<size_t>(n), f);
    fclose(f);
  }
  abort();
}

void __cdecl PortInstallUnhandledFilter(void)
{
}

void __cdecl PortInstallCrashReporter(void)
{
}

int __cdecl PortCrashThreadStack(void *, char *, int) { return 0; }

static void ReportError(const char *pszText)
{
  if (FILE *f = fopen(PortLogFile("hyperion_crash.log"), "wb")) {
    fprintf(f, "Hyperion stopped on 1.17's error:\r\n%s\r\n", pszText);
    fclose(f);
  }
}

#else  // off Windows
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <time.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/ucontext.h>  // ucontext_t alone: <ucontext.h>'s routines are deprecated on macOS
#else
#include <link.h>
#include <ucontext.h>
#endif
#include <unistd.h>

// the program's code: Linux's linker marks where it starts and ends; on a Mac it is the image's
// __TEXT segment, from its header
#if defined(__APPLE__)
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#else
extern "C" char __executable_start;
extern "C" char etext;
#endif

static uintptr_t g_base, g_end;
static volatile int g_reported;

static bool Ours(uintptr_t p) { return p >= g_base && p < g_end; }

static void Name(Frame *pf, uintptr_t a)
{
  memset(pf, 0, sizeof *pf);
  pf->a = a;
  Dl_info info;
  if (dladdr(reinterpret_cast<void *>(a), &info)) {
    snprintf(pf->szModule, sizeof pf->szModule, "%s",
             Ours(a) ? "Hyperion" : info.dli_fname != NULL ? BaseName(info.dli_fname) : "?");
    pf->off = a - (Ours(a) ? g_base : reinterpret_cast<uintptr_t>(info.dli_fbase));
    // Hyperion's own names are only its few exports, not the function the frame is in
    if (info.dli_sname != NULL && !Ours(a))
      snprintf(pf->szFunction, sizeof pf->szFunction, "%s+0x%zX", info.dli_sname,
               static_cast<size_t>(a - reinterpret_cast<uintptr_t>(info.dli_saddr)));
  } else {
    snprintf(pf->szModule, sizeof pf->szModule, "%s", Ours(a) ? "Hyperion" : "?");
    pf->off = Ours(a) ? a - g_base : a;
  }
}

// The libraries loaded, a line each.
#if !defined(__APPLE__)
struct ModuleList {
  char *psz;
  size_t cb, n;
};

static int OnModule(struct dl_phdr_info *pInfo, size_t, void *pData)
{
  ModuleList *pList = static_cast<ModuleList *>(pData);
  if (pList->n >= pList->cb - 200)
    return 1;
  const char *pszName = pInfo->dlpi_name != NULL && pInfo->dlpi_name[0] != 0 ? BaseName(pInfo->dlpi_name) : "Hyperion";
  const int k = snprintf(pList->psz + pList->n, pList->cb - pList->n, "  %-28s %p\r\n", pszName,
                         reinterpret_cast<void *>(pInfo->dlpi_addr));
  if (k > 0)
    pList->n += static_cast<size_t>(k);
  return 0;
}
#endif

static void PutModules(char *psz, size_t cb)
{
  psz[0] = 0;
#if defined(__APPLE__)
  size_t n = 0;
  for (uint32_t i = 0; i < _dyld_image_count() && n < cb - 200; i++) {
    const char *pszName = _dyld_get_image_name(i);
    const int k = snprintf(psz + n, cb - n, "  %-28s %p\r\n", pszName != NULL ? BaseName(pszName) : "?",
                           static_cast<const void *>(_dyld_get_image_header(i)));
    if (k > 0)
      n += static_cast<size_t>(k);
  }
#else
  ModuleList list = {psz, cb, 0};
  dl_iterate_phdr(OnModule, &list);
#endif
}

// The report: what happened, where, the stack, the notes and the registers.
static void Report(const char *pszWhat, uintptr_t ip, uintptr_t fault, const ucontext_t *uc, bool bGoesOn)
{
  if (__sync_lock_test_and_set(&g_reported, 1) != 0)
    return;
  SYSTEMTIME st;
  GetLocalTime(&st);
  Begin(st);
  snprintf(g_szWhat, sizeof g_szWhat, "%s", pszWhat);
  Frame at;
  Name(&at, ip);
  snprintf(g_szWhere, sizeof g_szWhere, "%s+0x%llX%s%s", at.szModule, static_cast<unsigned long long>(at.off),
           at.szFunction[0] ? "  " : "", at.szFunction);
  if (fault != 0)
    snprintf(g_szAccess, sizeof g_szAccess, "address %p", reinterpret_cast<void *>(fault));
  snprintf(g_szThread, sizeof g_szThread, "%lu", static_cast<unsigned long>(GetCurrentThreadId()));
  PutHead(st);
  PutSession();

  void *apFrames[FRAME_COUNT];
  const int nFrames = backtrace(apFrames, FRAME_COUNT);
  for (g_nFrames = 0; g_nFrames < nFrames; g_nFrames++)
    Name(&g_aFrames[g_nFrames], reinterpret_cast<uintptr_t>(apFrames[g_nFrames]));
  PutFrames("the C library's backtrace, from the handler");
  PutNotes();

#if defined(__x86_64__)
  if (uc != NULL) {
    const greg_t *g = uc->uc_mcontext.gregs;
    Put(g_text,
        "\r\nrax %016llX rcx %016llX rdx %016llX rbx %016llX\r\n"
        "rsp %016llX rbp %016llX rsi %016llX rdi %016llX\r\n"
        "r8  %016llX r9  %016llX r10 %016llX r11 %016llX\r\n"
        "r12 %016llX r13 %016llX r14 %016llX r15 %016llX\r\n",
        (unsigned long long)g[REG_RAX], (unsigned long long)g[REG_RCX], (unsigned long long)g[REG_RDX],
        (unsigned long long)g[REG_RBX], (unsigned long long)g[REG_RSP], (unsigned long long)g[REG_RBP],
        (unsigned long long)g[REG_RSI], (unsigned long long)g[REG_RDI], (unsigned long long)g[REG_R8],
        (unsigned long long)g[REG_R9], (unsigned long long)g[REG_R10], (unsigned long long)g[REG_R11],
        (unsigned long long)g[REG_R12], (unsigned long long)g[REG_R13], (unsigned long long)g[REG_R14],
        (unsigned long long)g[REG_R15]);
  }
#elif defined(__APPLE__) && defined(__aarch64__)
  if (uc != NULL) {
    // macOS keeps the frame and link registers, x29 and x30, apart from the rest
    const __darwin_arm_thread_state64 &ss = uc->uc_mcontext->__ss;
    Put(g_text, "\r\n");
    for (int r = 0; r < 31; r++)
      Put(g_text, "x%-2d %016llX%s", r, (unsigned long long)(r < 29 ? ss.__x[r] : r == 29 ? ss.__fp : ss.__lr),
          r % 4 == 3 ? "\r\n" : " ");
    Put(g_text, "\r\nsp  %016llX pc  %016llX\r\n", (unsigned long long)ss.__sp, (unsigned long long)ss.__pc);
  }
#elif defined(__aarch64__)
  if (uc != NULL) {
    Put(g_text, "\r\n");
    for (int r = 0; r < 31; r++)
      Put(g_text, "x%-2d %016llX%s", r, (unsigned long long)uc->uc_mcontext.regs[r], r % 4 == 3 ? "\r\n" : " ");
    Put(g_text, "\r\nsp  %016llX pc  %016llX\r\n", (unsigned long long)uc->uc_mcontext.sp,
        (unsigned long long)uc->uc_mcontext.pc);
  }
#endif
  Put(g_text, "\r\nHyperion %p to %p\r\n", reinterpret_cast<void *>(g_base), reinterpret_cast<void *>(g_end));
  Flush(g_text);
  static char s_szModules[24576];
  PutModules(s_szModules, sizeof s_szModules);
  Put(g_text, "\r\nmodules (name, where):\r\n%s", s_szModules);
  Finish(st, 0, s_szModules, bGoesOn);
}

static void OnSignal(int sig, siginfo_t *si, void *pContext)
{
  const ucontext_t *uc = static_cast<const ucontext_t *>(pContext);
  uintptr_t ip = 0;
#if defined(__x86_64__)
  ip = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
#elif defined(__APPLE__) && defined(__aarch64__)
  ip = static_cast<uintptr_t>(uc->uc_mcontext->__ss.__pc);
#elif defined(__aarch64__)
  ip = static_cast<uintptr_t>(uc->uc_mcontext.pc);
#endif
  char szWhat[64];
  snprintf(szWhat, sizeof szWhat, "signal %d (%s)", sig, strsignal(sig));
  Report(szWhat, ip, reinterpret_cast<uintptr_t>(si->si_addr), uc, false);
  signal(sig, SIG_DFL);
  raise(sig);
}

// RaiseException off Windows (port_posix.cpp): the port's own exceptions end the
// process here, as the unhandled filter ends it on Windows.
void __cdecl PortCrashRaise(DWORD dwCode, const ULONG_PTR *pArgs, DWORD nArgs)
{
  char szWhat[200];
  if (dwCode == ORIGINAL_CODE_REACHED && nArgs >= 1)
    snprintf(szWhat, sizeof szWhat,
             "a pointer of 1.17's data to its code, which has no function of ours (tools/code_map.py), "
             "called from %p",
             reinterpret_cast<void *>(pArgs[0]));
  else
    snprintf(szWhat, sizeof szWhat, "exception 0x%08X", static_cast<unsigned>(dwCode));
  Report(szWhat, reinterpret_cast<uintptr_t>(__builtin_return_address(0)), 0, NULL, false);
  _exit(255);
}

static void ReportError(const char *pszText)
{
  char sz[900];
  snprintf(sz, sizeof sz, "1.17's error: %s", pszText);
  Report(sz, reinterpret_cast<uintptr_t>(__builtin_return_address(0)), 0, NULL, true);
}

void __cdecl PortInstallUnhandledFilter(void)
{
}

void __cdecl PortInstallCrashReporter(void)
{
#if defined(__APPLE__)
  unsigned long cbText = 0;
  g_base = reinterpret_cast<uintptr_t>(&_mh_execute_header);
  g_end = reinterpret_cast<uintptr_t>(getsegmentdata(&_mh_execute_header, "__TEXT", &cbText)) + cbText;
  uint32_t cbExe = sizeof g_szExe;
  if (_NSGetExecutablePath(g_szExe, &cbExe) != 0)
    g_szExe[0] = 0;
#else
  g_base = reinterpret_cast<uintptr_t>(&__executable_start);
  g_end = reinterpret_cast<uintptr_t>(&etext);
  const ssize_t cbExe = readlink("/proc/self/exe", g_szExe, sizeof g_szExe - 1);
  g_szExe[cbExe > 0 ? cbExe : 0] = 0;
#endif
  // a stack of its own, so an overflow of the game's can still be reported; a megabyte, since
  // the report copies files through 64 KB buffers on it
  static char s_altStack[1 << 20];
  stack_t ss;
  ss.ss_sp = s_altStack;
  ss.ss_size = sizeof s_altStack;
  ss.ss_flags = 0;
  sigaltstack(&ss, NULL);
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = OnSignal;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  const int aSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
  for (int sig : aSignals)
    sigaction(sig, &sa, NULL);
}

int __cdecl PortCrashThreadStack(void *, char *, int) { return 0; }
#endif

// 1.17's critical error (gds_appcrit.cpp's vappcritf), which ends the game with no dialog: the
// start's failure during the start, else a report.
void __cdecl PortCrashCritical(const char *pszText)
{
  if (PortStartupPending())
    PortStartupFail("1.17's error", "%s", pszText);
  ReportError(pszText);
}

// 1.17's own error (gds_appfat.cpp's ErrorDlg and FileErrorDlg): its dialog can be out of sight,
// under a room's match start, and the game then ends, so it is reported before the dialog. During
// the start it ends the start, in the start-up log and its box.
void __cdecl PortCrashFatal(const char *pszText)
{
  if (PortStartupPending())
    PortStartupFail("1.17's error", "%s", pszText);
  ReportError(pszText);
}

// HYPERION_TEST_CRASH=av|abort|terminate|invalid|purecall|throw: a crash of that kind made on
// purpose, for the tests of the report (PortCrashTest).
void __cdecl PortCrashTest(const char *pszWhere)
{
  static char s_szKind[32] = "?";
  if (s_szKind[0] == '?' && GetEnvironmentVariableA("HYPERION_TEST_CRASH", s_szKind, sizeof s_szKind) == 0)
    s_szKind[0] = 0;
  if (s_szKind[0] == 0)
    return;
  // "kind" crashes at the first call, "kind:where" at that place's
  const char *pszColon = strchr(s_szKind, ':');
  if (pszColon != NULL && strcmp(pszColon + 1, pszWhere) != 0)
    return;
  char szKind[32];
  snprintf(szKind, sizeof szKind, "%.*s", pszColon != NULL ? static_cast<int>(pszColon - s_szKind) : 31, s_szKind);
  s_szKind[0] = 0;
  PortCrashNote("HYPERION_TEST_CRASH makes a crash, for a test");
  if (strcmp(szKind, "abort") == 0)
    abort();
  if (strcmp(szKind, "terminate") == 0)
    std::terminate();
  if (strcmp(szKind, "throw") == 0)
    throw std::runtime_error("HYPERION_TEST_CRASH's exception");
#if defined(_WIN32)
  if (strcmp(szKind, "invalid") == 0) {
    char sz[4];
    strcpy_s(sz, sizeof sz, "longer than four");
  }
  if (strcmp(szKind, "purecall") == 0) {
    _purecall();
  }
#endif
  volatile int *p = NULL;
  *p = 1;
}
