// hyperion.cpp -- Hyperion.exe's start: StarCraft 1.17 played by Hyperion's engine,
// which is linked into this exe, from the player's own game files. No code of 1.17's
// runs, and no exe of 1.17's is read: the
// game's starting values are the engine's own source (src117/gen/data117.cpp).
//
// The C runtime starts at wWinMainCRTStartup (the link's /ENTRY), so it calls
// wWinMain below, not the game's WinMain (main.cpp), which this calls last; off Windows
// the start is main. Both go to HyperionMain:
//
//   1. find the game's folder, this one's unless HYPERION_GAME or Settings names another,
//      and on a start a player made with no game's files found, ask which install they
//      have (src117/port/data/port_install.cpp);
//   2. install the crash report (src117/port/tools/port_crash.cpp);
//   3. see that the game's files are there, and fill the engine's block with 1.17's
//      starting values, sending each address of 1.17's code they hold to the engine's
//      function (port_standalone.cpp, src117/gen/code_map.cpp);
//   4. load the development DLLs HYPERION_DLLS names (the replay observer), calling
//      each one's init;
//   5. run the game.
//
// Each step goes in the start-up log (src117/port/tools/port_startup.cpp), begun on the
// program's first line, and a start that cannot go on says so in the error window, which names
// it (src117/port/tools/port_errorwin.cpp).
//
// Environment, for development and the replay harness:
//   HYPERION_GAME  the game's folder instead of this one's; a file named there (1.17's
//                  StarCraft.exe) stands for the folder it is in
//   HYPERION_INSTALL_WINDOW  drawn: the first start's window Linux and a Mac have, on Windows
//   HYPERION_ERROR_WINDOW    drawn: the error window Linux and a Mac have, on Windows
//   HYPERION_DLLS  DLLs to load, "path[@export];...": the export (default oc_init) is
//                  called with the main thread id, rt_launcher's contract
// This sets HYPERION_GAME to the folder, with its separator, for the LAN room's files.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "../src117/port/tools/port_crash.h"       // PortInstallCrashReporter
#include "../src117/port/tools/port_errorwin.h"    // PortErrorCrashReport
#include "../src117/port/data/port_hxm.h"         // PortHxmCheckCommand, PortHxmCompileCommand
#include "../src117/port/hud/port_hudcheck.h"        // PortHudCheckCommand
#include "../src117/port/data/port_install.h"     // PortInstallFound, PortInstallFirstStart
#include "../src117/port/data/port_remastered.h"  // PortGameSource
#include "../src117/port/front/port_ui.h"         // PortCascFolder
#include "../src117/port/lan/port_lanroom.h"     // PortLanRoomMain
#include "../src117/port/platform/port_paths.h"       // PortCommandOptions, PortLogFile
#include "../src117/port/platform/port_posix.h"       // PortPosixSetCommandLine
#include "../src117/port/platform/port_standalone.h"  // PortGameDataInit, PortRunGame
#include "../src117/port/lab/port_labworker.h"        // PortLabWorkerStart
#include "../src117/port/lan/port_instance.h"         // PortCheckForOtherInstances
#include "../src117/port/tools/port_startup.h"        // PortStartupBegin, PortStartupStep, PortStartupFail
#include "../src117/port/tools/port_sysinfo.h"        // PortSysInfoMask, PortSysInfoSet
#include "../src117/port/platform/port_settings.h"    // PortSettingsReadNote

[[noreturn]] static void Fail(const char *stage, const char *fmt, ...)
{
  char msg[1024];
  va_list ap;
  va_start(ap, fmt);
  _vsnprintf_s(msg, sizeof msg, _TRUNCATE, fmt, ap);
  va_end(ap);
  PortStartupFail(stage, "%s", msg);
}

static void ExeDir(char *out, DWORD cap)
{
  strcpy_s(out, cap, PortProgramFolder());  // on a Mac, the folder Hyperion.app is in
}

// The game's folder, with its separator: a classic StarCraft folder --install-folder names,
// else HYPERION_GAME's, or the folder of the file it names, else the classic StarCraft folder
// Settings keeps, unless StarCraft from Battle.net is this run's install, else this exe's.
static void GameFolder(const char *exeDir, char *out, DWORD cap)
{
  if (PortInstallCommandFolder(out, cap))
    return;
  DWORD n = GetEnvironmentVariableA("HYPERION_GAME", out, cap);
  if (n == 0 || n >= cap) {
    if (!PortInstallClassicFolder(out, cap))
      strcpy_s(out, cap, exeDir);
    return;
  }
  const DWORD attr = GetFileAttributesA(out);
  if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
    while (n > 0 && out[n - 1] != '\\' && out[n - 1] != '/')
      n--;
    out[n] = 0;
  } else if (out[n - 1] != '\\' && out[n - 1] != '/' && n + 1 < cap) {
    out[n] = '\\';
    out[n + 1] = 0;
  }
}

// Whether the game's files are where the engine opens them (init.cpp, open_archives):
// StarDat.mpq, which 1.17 cannot start without, in the game's folder, or a StarCraft:
// Remastered install to read in its place (port_remastered.cpp).
static bool HaveGameFiles(const char *game)
{
  char path[MAX_PATH];
  sprintf_s(path, "%sStarDat.mpq", game);
  return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES || PortGameSource() != PORT_SOURCE_MPQ;
}

typedef DWORD(__stdcall *InitFn)(DWORD);

static void LoadDll(const char *spec)
{
  char path[MAX_PATH], exportName[64] = "oc_init";
  strncpy_s(path, spec, _TRUNCATE);
  char *at = strrchr(path, '@');
  if (at) {
    strncpy_s(exportName, at + 1, _TRUNCATE);
    *at = 0;
  }
  HMODULE h = LoadLibraryA(path);
  if (!h)
    Fail("development DLLs", "Cannot load %s (%lu).", path, GetLastError());
  InitFn init = reinterpret_cast<InitFn>(GetProcAddress(h, exportName));
  if (!init)
    Fail("development DLLs", "%s has no %s.", path, exportName);
  PortStartupStep("development DLLs", "%s, %s", PortSysInfoMask(path).c_str(), exportName);
  init(GetCurrentThreadId());
}

static void LoadDevelopmentDlls(void)
{
  char list[4096];
  DWORD n = GetEnvironmentVariableA("HYPERION_DLLS", list, sizeof list);
  if (n == 0 || n >= sizeof list)
    return;
#if defined(_WIN32)
  for (char *ctx = NULL, *s = strtok_s(list, ";", &ctx); s; s = strtok_s(NULL, ";", &ctx))
#else
  for (char *ctx = NULL, *s = strtok_r(list, ";", &ctx); s; s = strtok_r(NULL, ";", &ctx))
#endif
    if (*s)
      LoadDll(s);
}

// The start-up's record, in the folder the game runs in, which the replay harness
// keeps with a run.
static void WriteInitLog(const char *game, unsigned long ours, unsigned long trapped)
{
  char buf[1024], data[MAX_PATH + 64];
  if (PortGameSource() == PORT_SOURCE_MPQ)
    sprintf_s(data, "1.17's MPQs");
  else
    sprintf_s(data, "%s StarCraft: Remastered at %s",
              PortGameSource() == PORT_SOURCE_BOTH ? "1.17's MPQs under" : "", PortCascFolder());
  int n = sprintf_s(buf, "Hyperion started; the game's folder %s; the game's files from %s; 1.17's "
                         "starting values from Hyperion's source, %lu pointers to the engine's code, "
                         "%lu to PortOriginalCodeReached\r\n", game, data, ours, trapped);
  PortStartupStep("game files", "from %s; the game's folder %s", PortSysInfoMask(data + (data[0] == ' ')).c_str(),
                  PortSysInfoMask(game).c_str());
  PortSysInfoSet("install", "files", "%s", PortSysInfoMask(data + (data[0] == ' ')).c_str());
  PortStartupStep("data", "1.17's starting values from Hyperion's source, %lu pointers to the engine's code, %lu to "
                  "PortOriginalCodeReached", ours, trapped);
  HANDLE h = CreateFileA(PortLogFile("hyperion_init.log"), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
  if (h != INVALID_HANDLE_VALUE) {
    DWORD wrote;
    WriteFile(h, buf, n, &wrote, NULL);
    CloseHandle(h);
  }
}

// The program's arguments, in the game's code page.
static int HyperionMain(const std::vector<std::string> &argv)
{
  const size_t argc = argv.size();
  // --crash-report <folder>: the window for a crash, in a copy the one that crashed started for
  // it (src117/port/tools/port_crash.cpp), which reads the report and nothing else
  if (argc == 3 && lstrcmpiA(argv[1].c_str(), "--crash-report") == 0)
    return PortErrorCrashReport(argv[2].c_str());
  // --hxm-check <in.hxm> [<report.txt>]: an editor's check of a map it wrote
  // (port_hxm.cpp), which needs no game, so it comes before the game is read.
  if ((argc == 3 || argc == 4) && lstrcmpiA(argv[1].c_str(), "--hxm-check") == 0)
    return PortHxmCheckCommand(argv[2].c_str(), argc == 4 ? argv[3].c_str() : NULL);
  // --hud-check <layout.json> [--style ...]: a HUD design's check, and its boxes at a shape
  // (src117/port/hud/port_hudcheck.cpp), which needs no game either
  if (argc >= 3 && lstrcmpiA(argv[1].c_str(), "--hud-check") == 0)
    return PortHudCheckCommand(std::vector<std::string>(argv.begin() + 2, argv.end()));

  char exeDir[MAX_PATH], game[MAX_PATH];
  ExeDir(exeDir, sizeof exeDir);
  const bool bMayAsk = PortInstallMayAsk();  // before HYPERION_GAME is set below
  GameFolder(exeDir, game, sizeof game);
  SetEnvironmentVariableA("HYPERION_GAME", game);
  PortStartupStep("game folder", "%s", PortSysInfoMask(game).c_str());

  // --hxm-compile <in.hxm> <out.chk>: compile a Hyperion map and stop (port_hxm.cpp).
  if (argc == 4 && lstrcmpiA(argv[1].c_str(), "--hxm-compile") == 0)
    return PortHxmCompileCommand(argv[2].c_str(), argv[3].c_str());

  // --lab-worker: a worker of the AI lab's farm, hidden, its environment set before SDL starts
  // (src117/port/lab/port_labworker.cpp)
  PortLabWorkerStart();
  PortInstallCrashReporter();
  PortStartupStep("crash report", "installed");
  // --room: a LAN room, which needs no game (src117/port/lan/port_lanroom.cpp)
  if (PortCommandOptions()->bRoom)
    return PortLanRoomMain();
  // one copy to a data folder, before anything is asked or written: a second raises the first's
  // window and ends, its own log beside the first's (port_instance.cpp)
  PortCheckForOtherInstances("SWarClass");
  PortStartupOpen(bMayAsk);
  PortStartupStep("settings", "%s", PortSettingsReadNote());
  const PortInstallKind kept = PortInstallChosen();
  PortStartupStep("install", "kept: %s; this run: %s; found: %s",
                  kept == PORT_INSTALL_CLASSIC ? "classic" : kept == PORT_INSTALL_BATTLENET ? "StarCraft from Battle.net"
                                                                                           : "none",
                  PortInstallCommandLine() == PORT_INSTALL_UNSET ? "as kept" : PortCommandOptions()->szInstall,
                  PortInstallFound(game) ? "yes" : "no");
  // a first start, with no base kept, or the base's files gone: the player says which they have,
  // where, and which the game reads, before PortGameSource decides from it (HaveGameFiles)
  // (with no window to ask in, as with no display, the message below says what is missing)
  if (bMayAsk && (kept == PORT_INSTALL_UNSET || !PortInstallFound(game))) {
    PortStartupStep("first start", "the window asks which install");
    PortStartupWait(true);
    const PortInstallAnswer answer = PortInstallFirstStart();
    PortStartupWait(false);
    PortStartupStep("first start", "%s", answer == PORT_INSTALL_ANSWER_QUIT ? "Quit" :
                    answer == PORT_INSTALL_ANSWER_CHOSEN ? "chosen" : "no window could be shown");
    if (answer == PORT_INSTALL_ANSWER_QUIT) {
      PortStartupEnded(0);
      return 0;
    }
    if (answer == PORT_INSTALL_ANSWER_CHOSEN) {
      SetEnvironmentVariableA("HYPERION_GAME", NULL);  // the choice's folder, not the one set above
      GameFolder(exeDir, game, sizeof game);
      SetEnvironmentVariableA("HYPERION_GAME", game);
      PortStartupStep("game folder", "%s", PortSysInfoMask(game).c_str());
    }
  }
  // --install and --install-folder name this run's files (port_install.cpp): a word it does not
  // know, or files that are not there, end the run with what was asked
  const PortOptions *pOptions = PortCommandOptions();
  if (pOptions->szInstall[0] != 0 && PortInstallCommandLine() == PORT_INSTALL_UNSET)
    Fail("install", "--install takes classic or remastered, not %s.", pOptions->szInstall);
  if ((pOptions->szInstall[0] != 0 || pOptions->szInstallFolder[0] != 0) && !HaveGameFiles(game))
    Fail("install", "Hyperion found no StarCraft files for%s%s%s%s%s. A classic StarCraft folder holds StarDat.mpq "
         "and BrooDat.mpq; StarCraft from Battle.net's own folder holds .build.info.",
         pOptions->szInstall[0] != 0 ? " --install " : "", pOptions->szInstall,
         pOptions->szInstallFolder[0] != 0 ? " --install-folder \"" : "", pOptions->szInstallFolder,
         pOptions->szInstallFolder[0] != 0 ? "\"" : "");
  if (!HaveGameFiles(game))
    Fail("game files", "Hyperion needs StarCraft's files: install StarCraft from Battle.net (free), which Hyperion "
         "finds by itself, or start Hyperion by itself and choose a StarCraft folder that has StarDat.mpq "
         "and BrooDat.mpq (StarCraft 1.16 or 1.17, or the CDs' install). No StarCraft.exe is needed.");
  ULONG ours = 0, trapped = 0;
  PortGameDataInit(&ours, &trapped);
  WriteInitLog(game, ours, trapped);
  LoadDevelopmentDlls();
  PortStartupStep("game", "the game's own start");
  return PortRunGame();
}

#if defined(_WIN32)
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif

// Windows 11 may slow a process it takes for background work (power throttling, EcoQoS),
// running it on slower clocks, and stops keeping a fine timer resolution for one whose window
// is minimized or hidden behind others. A game at its own speed rests between frames
// (port_clock.cpp), and a LAN room's copies and streamed seats run with no window in front,
// so Hyperion asks to keep its speed and its timers. Windows 10 knows only the
// speed, and refuses both together; before its 1709 it refuses the call, and nothing changes.
static void KeepFullSpeed(void)
{
  PROCESS_POWER_THROTTLING_STATE s = {};
  s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
  s.StateMask = 0;
  if (!SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof s)) {
    s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof s);
  }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
  PortStartupBegin();
  KeepFullSpeed();
  int argc = 0;
  LPWSTR *argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::vector<std::string> argv;
  for (int i = 0; argvW != NULL && i < argc; i++) {
    char arg[MAX_PATH];
    if (WideCharToMultiByte(CP_ACP, 0, argvW[i], -1, arg, MAX_PATH, NULL, NULL) == 0)
      arg[0] = 0;
    argv.push_back(arg);
  }
  if (argvW != NULL)
    LocalFree(argvW);
  return HyperionMain(argv);
}
#else
int main(int argc, char **argv)
{
  // a send to a closed socket fails as on Windows instead of ending the process
  signal(SIGPIPE, SIG_IGN);
  PortPosixSetCommandLine(argc, argv);
  PortStartupBegin();
#if defined(__APPLE__)
  PortPosixKeepRunning();
#endif
  return HyperionMain(std::vector<std::string>(argv, argv + argc));
}
#endif
