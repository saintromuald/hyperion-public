// port_instance.cpp -- the single-instance check, scoped to one data folder, which the
// engine's CheckForOtherInstances (main.cpp) forwards to. Presentation only.
//
// 1.17's CheckForOtherInstances (0x466F20) names one event for every copy on the machine, so
// a second StarCraft anywhere raises the first's window and quits. Hyperion refuses only a
// second copy using its own data folder (Data\ beside the exe, or --data's), which would
// share its replays, saves and logs; it runs beside other installs, and copies given a --data
// each run side by side (a LAN room's seats). The lock is a small shared-memory block,
// named after a hash of the folder since a kernel object's name cannot hold a path, so the
// first copy can leave its process id in it: the game's InitACL (0x467570) denies other
// processes the rights to read its exe path, and the id is how the second finds the window.
// A copy started by PortInstanceRestart (Settings' Restart now) names the first with --after
// and waits for it to end. Hyperion.exe makes the check before anything is asked or written
// (hyperion.cpp), and the game's own call finds it made.
#include "port_instance.h"
#include "../platform/port_paths.h"  // PortDataFolder, PortCommandOptions
#include "Storm.h"        // <windows.h>
#include "../tools/port_startup.h"  // PortStartupStep, PortStartupOtherCopy
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

// FNV-1a over the case-folded data folder, since Windows paths ignore case.
static DWORD PortHashInstallFolder()
{
  DWORD hash = 2166136261u;
  for (const char *p = PortDataFolder(); *p != '\0'; ++p) {
    char c = *p;
    if (c >= 'A' && c <= 'Z')
      c += 'a' - 'A';
    hash = (hash ^ (BYTE)c) * 16777619u;
  }
  return hash;
}

// The other copy's main window, polled for up to two seconds as the oracle
// does, but only among the windows of process `pid`.
static HWND PortFindOtherInstance(const char *pszTitle, DWORD pid)
{
  // the game's own class, and SDL's, which its window has: SDL_RegisterApp names the class only
  // when it comes before SDL's video starts
  const char *const apszClasses[] = {pszTitle, "SDL_app"};
  for (int i = 0; i < 8; ++i) {
    Sleep(250);
    for (const char *pszClass : apszClasses)
      for (HWND hWnd = FindWindowExA(NULL, NULL, pszClass, NULL); hWnd != NULL;
           hWnd = FindWindowExA(NULL, hWnd, pszClass, NULL)) {
        DWORD owner = 0;
        GetWindowThreadProcessId(hWnd, &owner);
        if (owner == pid)
          return hWnd;
      }
  }
  return NULL;
}

void PortCheckForOtherInstances(const char *pszTitle)
{
  static bool s_bChecked;
  if (s_bChecked)
    return;
  s_bChecked = true;
  // a mod's check plays no game and writes only its own report and logs, beside a Hyperion
  // that is running
  if (PortCommandOptions()->szModCheck[0] != '\0')
    return;
  char szName[64];
  sprintf(szName, "Hyperion Instance %08lX", PortHashInstallFolder());
  // The handle stays open for the life of the process: it is the lock.
  HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                   sizeof(DWORD), szName);
  if (hMap == NULL)
    return;
  bool bOther = GetLastError() == ERROR_ALREADY_EXISTS;
  volatile DWORD *pPid = static_cast<volatile DWORD *>(MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, sizeof(DWORD)));
  // The copy that made the block stores its id just after, so a copy that reads the block in
  // that moment waits for the id instead of looking for the windows of process 0.
  DWORD dwFirst = 0;
  for (int i = 0; bOther && pPid != NULL && i < 20 && (dwFirst = *pPid) == 0; ++i)
    Sleep(50);
  // A benchmark's second process (port_bench.cpp) is started by the first as it ends, and a
  // restarted copy by the one it follows (--after): it waits for that one to go, then holds the
  // lock; any other copy's it leaves, as they share a folder
  const PortOptions *pOptions = PortCommandOptions();
  const bool bFollows = pOptions->szAfter[0] != '\0' && strtoul(pOptions->szAfter, NULL, 10) == dwFirst;
  if (bOther && dwFirst != 0 && (pOptions->szBenchReplay[0] != '\0' || bFollows)) {
    if (HANDLE hFirst = OpenProcess(SYNCHRONIZE, FALSE, dwFirst)) {
      if (WaitForSingleObject(hFirst, 15000) == WAIT_OBJECT_0)
        bOther = false;
      CloseHandle(hFirst);
    } else {
      bOther = false;  // no such process: the id is of a copy that has gone
    }
  }
  if (!bOther) {
    if (pPid != NULL)
      *pPid = GetCurrentProcessId();
    PortStartupStep("one copy", dwFirst != 0 ? "the copy before has ended" : "this copy holds the data folder");
    return;
  }

  // From here on it is the oracle's tail: raise the other window and quit.
  HWND hWnd = dwFirst != 0 ? PortFindOtherInstance(pszTitle, dwFirst) : NULL;
  if (hWnd != NULL) {
    HWND hPopup = GetLastActivePopup(hWnd);
    if (hPopup != NULL)
      hWnd = hPopup;
    HWND hTop = GetTopWindow(hWnd);
    if (hTop == NULL)
      hTop = hWnd;
    SetForegroundWindow(hWnd);
    SetFocus(hTop);
    ShowWindow(hWnd, SW_SHOWNORMAL);
  }
  PortStartupStep("one copy", "another copy holds the data folder (process %lu); its window %s", dwFirst,
                  hWnd != NULL ? "was raised" : "was not found");
  PortStartupOtherCopy();
  // nothing of the game's is up yet, so the oracle's app_exit is the C library's exit
  exit(0);
}

// Erases " <name>=value" or " <name> value" from a command line; a value in quotes may hold
// spaces, and a longer option that starts with the name (--mod-check) stays.
template <class S>
static void DropOption(S &line, const char *pszName)
{
  S name(1, ' ');
  for (const char *p = pszName; *p != '\0'; ++p)
    name += static_cast<typename S::value_type>(*p);
  for (size_t at = line.find(name); at != S::npos; at = line.find(name, at)) {
    size_t end = at + name.size();
    if (end < line.size() && line[end] != '=' && line[end] != ' ') {
      at = end;
      continue;
    }
    bool bQuoted = false;
    for (++end; end < line.size() && (bQuoted || line[end] != ' '); ++end)
      if (line[end] == '"')
        bQuoted = !bQuoted;
    line.erase(at, end - at);
  }
}

bool PortInstanceRestart(const char *pszDrop)
{
  char szAfter[32];
  sprintf(szAfter, " --after=%lu", static_cast<unsigned long>(GetCurrentProcessId()));
  PROCESS_INFORMATION pi;
#if defined(_WIN32)
  // in UTF-16, so a folder named outside the code page starts again as it did
  std::wstring line = GetCommandLineW();
  const size_t at = line.find(L" --after=");  // a restart's own, always last
  if (at != std::wstring::npos)
    line.erase(at);
  if (pszDrop != NULL)
    DropOption(line, pszDrop);
  for (const char *p = szAfter; *p != '\0'; ++p)
    line += static_cast<wchar_t>(*p);
  STARTUPINFOW si;
  ZeroMemory(&si, sizeof si);
  si.cb = sizeof si;
  if (!CreateProcessW(NULL, &line[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    return false;
#else
  std::string line = GetCommandLineA();
  const size_t at = line.find(" --after=");
  if (at != std::string::npos)
    line.erase(at);
  if (pszDrop != NULL)
    DropOption(line, pszDrop);
  line += szAfter;
  STARTUPINFOA si;
  ZeroMemory(&si, sizeof si);
  si.cb = sizeof si;
  if (!CreateProcessA(NULL, &line[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    return false;
#endif
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}
