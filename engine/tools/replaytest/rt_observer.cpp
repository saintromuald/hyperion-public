// rt_observer.cpp -- the replay-test observer.
//
// This DLL contains no game code. rt_launcher injects it into the untouched 1.17
// oracle for the reference run (the 32-bit build), and Hyperion.exe loads it for a
// build under test (the x64 build, tools/build_engine_x64.py), so every run carries
// the same instrument. It changes nothing in the game's code bytes. It replaces the
// exe's GetTickCount (and optionally Sleep) import, and its CreateEventA so the
// game's single-instance check never sees another copy of StarCraft, whether the
// oracle's copy of the check runs or the engine's (see hk_CreateEventA).
// From inside the GetTickCount hook it:
//
//   * runs a virtual clock, so the frame pump never waits and the simulation
//     runs as fast as the CPU allows;
//   * writes one record per simulated frame: the frame, the game's random state
//     and call count, and two hashes of the live unit list, plus per-player
//     totals every few frames;
//   * can press keys in the menus from a script in its config, to reach a
//     replay without a person at the keyboard;
//   * ends the process when the replay ends, when a frame limit is reached, or
//     when nothing moves for too long.
//
// A fault handler logs where a crashing game was (the frame, the code and the
// faulting address) and flushes the log before the game's own handler runs.
//
// Two data writes are made while a game runs, and both are optional:
// sgbWindowActive_g is held at 1, because a single-player game only advances
// while its window is active, and sgdwUpdatesPerFrame_g is set to the configured
// count, the knob the game itself raises to 10 at replay speed x8 and above, so
// the screen is repainted once per N frames. The per-frame hashes are what show
// whether either changes the simulation: run the oracle with and without them.
// With snapshots on, a third write holds the cursor's animation on its first
// frame (sgdwCurFrame_g, sgdwCurNextTick_g), so the cursor looks the same in
// every build.
//
// Addresses are the 1.17 oracle's, from re/globals117.json and src117/h.
//
// The x64 build runs only with an engine, and takes from
// oc_observe whatever 1.17 kept as a pointer or in a struct that holds one; the rest,
// plain values, it still reads at their places in the engine's block. The raw hash
// and the unit dump read 1.17's layout, so they are off there.
//
// On Linux it is a shared library that Hyperion loads the same way. The Windows
// calls it makes are Hyperion's own (compat/, port_posix.cpp), exported to it; the clock,
// Sleep and CreateEventA are swapped through Hyperion's oc_posix_hook, as there is no
// import table; a fault is a signal.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#if !defined(_WIN32)
#include <dlfcn.h>
#include <setjmp.h>
#include <signal.h>
#if defined(__APPLE__)
#include <sys/ucontext.h>  // ucontext_t alone: <ucontext.h>'s routines are deprecated on macOS
#else
#include <ucontext.h>
#endif
#endif
#include "../../src117/port/replays/port_observe.h"

#if defined(_MSC_VER)
#pragma intrinsic(_ReturnAddress)
#endif

// A 64-bit engine build, Windows' or another system's: 1.17's own code is not there.
#if defined(_WIN64) || defined(__LP64__)
#define RT_WIDE 1
#endif

// rt_init, exported: by the .def file on Windows.
#if defined(_WIN32)
#define OC_RT_EXPORT
#else
#define OC_RT_EXPORT __attribute__((visibility("default")))
#endif

// A pointer-sized value in hex, for printf.
#if defined(_MSC_VER)
#define RT_PX "IX"
#else
#define RT_PX "lX"
#endif

// The real clock and Sleep, which the hooks below pass on to. Off Windows the hooks replace
// Hyperion's GetTickCount and Sleep for every caller, this library included, so its own
// timing (the watchdog, the settle times) asks these instead.
typedef DWORD(WINAPI *PFN_GTC)(void);
typedef void(WINAPI *PFN_SLEEP)(DWORD);
static PFN_GTC g_realGTC;
static PFN_SLEEP g_realSleep;
#if !defined(_WIN32)
#define GetTickCount() (g_realGTC ? g_realGTC() : ::GetTickCount())
#define Sleep(ms) (g_realSleep ? g_realSleep(ms) : ::Sleep(ms))
#endif

// A fault inside these is skipped (a snapshot's bitmap freed under it, a dialog's text).
#if defined(_WIN32)
#define RT_TRY __try {
#define RT_EXCEPT } __except (EXCEPTION_EXECUTE_HANDLER) {
#define RT_END }
#else
static thread_local sigjmp_buf *t_pGuard;
struct RtGuard {
  explicit RtGuard(sigjmp_buf *p) { t_pGuard = p; }
  ~RtGuard() { t_pGuard = NULL; }
};
#define RT_TRY { sigjmp_buf rtJump; if (sigsetjmp(rtJump, 1) == 0) { RtGuard rtGuard(&rtJump);
#define RT_EXCEPT } else { t_pGuard = NULL;
#define RT_END } }
#endif

// ---------------------------------------------------------------------------
// Oracle addresses.

#define VA_GAME_MODE        0x6662B8u  // gwGameMode, WORD: 3 in game, 4 in menus
#define VA_GLUE_SCREEN      0x66A274u  // sgnGlueScreen (glGluesMode), int
#define VA_GLUE_DLG         0x66A318u  // sgpGlueDlg, the active glue dialog
#define VA_HWND             0x66FD28u  // ghWnd
#define VA_WINDOW_ACTIVE    0x66FD24u  // sgbWindowActive_g, DWORD
#define VA_UPDATES_PER_FRM  0x50335Cu  // sgdwUpdatesPerFrame_g, DWORD
#define VA_FRAME            0x52651Cu  // the frame counter, DWORD
#define VA_GAME_RUNNING     0x6663EDu  // sgbGameRunning_g, BYTE
#define VA_PLAYBACK         0x67CC90u  // gPlayback, DWORD
#define VA_REPLAY_FRAMES    0x67CCA1u  // the replay's total frames, unaligned DWORD
#define VA_REPLAY_SEED      0x67CCA8u  // sgdwReplaySeed_g
#define VA_GAME_SPEED       0x6D3130u  // gOptions, the game speed index 0..6
#define VA_TICKS_PER_FRAME  0x503360u  // sgTicksPerFrame_g, DWORD[7]
#define VA_RAND_STATE       0x67393Cu  // sgNetRandState_g
#define VA_RAND_COUNT       0x673938u  // sgNetRandCount_g
// new_game's `call _time64`, the clock a single-player game seeds its random numbers
// from; with pin_seed it calls hk_time64 instead.
#define VA_TIME64_CALL      0x443FE9u
static const DWORD kPinnedSeed = 0x7E5A2031u;
#define VA_MINERALS         0x5263D0u  // gPlayerMinerals, int[12]
#define VA_GAS              0x526400u  // gPlayerGas, int[12]
// The stats the score screen and the triggers read, all plain data that every build
// keeps in 1.17's layout, so they are read at their addresses everywhere.
#define VA_MINERALS_MINED   0x526460u  // gPlayerMineralsGathered_g, int[12], the starting minerals included
#define VA_GAS_MINED        0x526430u  // gPlayerGasGathered_g, int[12]
#define VA_GAME_COUNTS      0x5290C4u  // glGameCounts_g, DWORD[28][12]: the score counters
#define VA_UNIT_COUNTS      0x529604u  // glCounts_g, DWORD[4][228][12]: the counts per unit type
#define VA_TECH_SC          0x534224u  // gbTechResearchedSC, BYTE[12][24]
#define VA_TECH_BW          0x536408u  // gbTechResearchedBW, BYTE[12][20]
#define VA_UPGRADES_SC      0x534590u  // gbUpgradeLevelSC, BYTE[12][46]
#define VA_UPGRADES_BW      0x5365DEu  // gbUpgradeLevelBW, BYTE[12][15]
#define VA_UNIT_TABLE       0x5DA3A0u  // CUnit::Head(), the first of 1,700 units
#define VA_UNIT_USED_HEAD   0x665AE0u  // CUnit::GetUsedList()
#define VA_DRAW_BUF         0x6D0B88u  // _img_draw_buf, bmp_t
#define VA_GAME_PALETTE     0x666568u  // sgGamePalette_g, 256 PALETTEENTRY
#define VA_DLG_TARGET       0x6CE778u  // sgpDlgTargetBuf_g, bmp_t *
#define VA_DLG_RETURN       0x6CE6DCu  // gnDlgReturnVal, long
#define VA_CTRL_EVT         0x4C2080u  // ctrl_evt, __fastcall(ctrl, event)
#define VA_CUR_FRAME        0x665D58u  // sgdwCurFrame_g, the cursor's free-running frame counter
#define VA_CUR_NEXT_TICK    0x665D5Cu  // sgdwCurNextTick_g, its next deadline in GetTickCount
#define VA_SAVE_GAME        0x498D20u  // saveload_write_save_data, __fastcall(timestamp, name, flags)
#define VA_MULTIPLAYER      0x672664u  // gbMultiPlayerMode, BYTE
#define VA_OPEN_SAVE        0x498CD0u  // sub_498CD0, __fastcall(path): the Load screen's open, game mode 1
#define VA_CENTER_ON_MTX    0x4457F0u  // gamemap_center_on_mtx, __fastcall(tile x, tile y)

#define UNIT_SIZE           0x150u

// CUnit fields read here (src117/h/CUnit.h).
#define U_NEXT        0x04
#define U_HP          0x08
#define U_X           0x28
#define U_Y           0x2A
#define U_PLAYER      0x4C
#define U_ORDER       0x4D
#define U_ORDER_STATE 0x4E
#define U_ORDER_TGT   0x5C
#define U_SHIELDS     0x60
#define U_TYPE        0x64
#define U_PATH        0x100  // sai_path *pPath, a heap pointer
#define U_ENERGY      0xA2
#define U_FLAGS       0xDC

// The exe is linked /DYNAMICBASE. The runner clears that bit on its run copy,
// as 1.17's absolute addresses need the exe at 0x400000; g_delta
// keeps the observer correct either way, and the start of the log records it. With an
// engine loaded it is where the engine keeps 1.17's data instead (observe_find), and
// the addresses of 1.17's code are not used. An engine keeps the globals with a
// pointer in them outside that block, so with one each
// address is the engine's answer (g_address).
static uintptr_t g_delta;
static uintptr_t(__cdecl *g_address)(uint32_t va);
static inline uintptr_t A(DWORD va) { return g_address ? g_address(va) : va + g_delta; }
template <class T> static inline T rd(DWORD va) { return *reinterpret_cast<volatile T *>(A(va)); }
template <class T> static inline void wr(DWORD va, T v) { *reinterpret_cast<volatile T *>(A(va)) = v; }

// ---------------------------------------------------------------------------
// Configuration.

struct MenuStep {
  int nGameMode;      // -1 matches any
  int nScreen;        // -1 matches any
  int nSettleMs;      // real milliseconds the state must hold before the keys go
  char szKeys[512];   // e.g. "S", "{ENTER}", "{DOWN*40}{ENTER}", "{WAIT500}"
};

struct Config {
  char szOut[MAX_PATH];
  char szSnapDir[MAX_PATH];
  bool bVirtualClock;
  DWORD dwClockStep;
  bool bClockStorm;
  DWORD dwSleepCap;       // 0 leaves Sleep alone
  DWORD dwUpdates;        // 0 leaves sgdwUpdatesPerFrame_g alone
  bool bKeepActive;
  DWORD dwMaxFrames;
  DWORD dwStatsEvery;
  DWORD dwPerfEvery;
  DWORD dwSnapEvery;
  bool bSnapScreens;
  bool bSnapScreen;       // snapshots are the whole screen, not the draw buffer
  bool bSnapMove;         // halfway to each snapshot the camera moves to a unit
  DWORD dwViewEvery;      // an engine build switches its view every N frames (oc_view_toggle)
  bool bExitAtEnd;
  DWORD dwStallSecs;
  DWORD dwMenuTimeoutSecs;
  bool bRawHash;
  DWORD dwDumpFrame;     // write every used unit's raw bytes at this frame
  DWORD dwSaveFrame;     // save the game at this frame, under szSaveName
  char szSaveName[32];
  char szLoadFile[MAX_PATH];  // the saved game the {LOAD} menu key opens
  bool bPinSeed;         // a single-player game seeded with kPinnedSeed, not the clock
  bool bCheckObserve;    // an engine build: read 1.17's memory too, and compare
  DWORD dwSyncEvery;     // an engine build: the state's hash (port_synchash.h) every N frames, as H lines
  MenuStep aSteps[32];
  int nSteps;
};

static Config g_cfg;

static void cfg_defaults() {
  memset(&g_cfg, 0, sizeof g_cfg);
  strcpy(g_cfg.szOut, "rt_observer.csv");
  g_cfg.bVirtualClock = true;
  g_cfg.dwClockStep = 50;
  g_cfg.dwSleepCap = 10;
  g_cfg.dwUpdates = 0;
  g_cfg.bKeepActive = true;
  g_cfg.dwStatsEvery = 24;
  g_cfg.dwPerfEvery = 240;
  g_cfg.bExitAtEnd = true;
  g_cfg.dwStallSecs = 60;
  g_cfg.dwMenuTimeoutSecs = 120;
  g_cfg.bRawHash = true;
}

static char *trim(char *s) {
  while (*s == ' ' || *s == '\t') ++s;
  char *e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
  return s;
}

static bool truthy(const char *v) { return v[0] == '1' || v[0] == 'y' || v[0] == 't'; }

// menu=<gamemode>,<screen>,<settle_ms>,<keys>
static void parse_menu(const char *v) {
  if (g_cfg.nSteps >= 32) return;
  MenuStep &st = g_cfg.aSteps[g_cfg.nSteps];
  char buf[600];
  strncpy(buf, v, sizeof buf - 1);
  buf[sizeof buf - 1] = 0;
  char *p = buf;
  char *f[4] = {0, 0, 0, 0};
  for (int i = 0; i < 4 && p; ++i) {
    f[i] = p;
    char *c = (i < 3) ? strchr(p, ',') : NULL;
    if (c) { *c = 0; p = c + 1; } else p = NULL;
  }
  if (!f[3]) return;
  st.nGameMode = (trim(f[0])[0] == '*') ? -1 : atoi(f[0]);
  st.nScreen = (trim(f[1])[0] == '*') ? -1 : atoi(f[1]);
  st.nSettleMs = atoi(f[2]);
  strncpy(st.szKeys, trim(f[3]), sizeof st.szKeys - 1);
  ++g_cfg.nSteps;
}

static void cfg_load() {
  cfg_defaults();
  char path[MAX_PATH];
  if (!GetEnvironmentVariableA("OC_RT_CFG", path, MAX_PATH)) strcpy(path, "rt_observer.cfg");
  FILE *f = fopen(path, "r");
  if (!f) return;
  char line[1024];
  while (fgets(line, sizeof line, f)) {
    char *s = trim(line);
    if (!*s || *s == '#') continue;
    char *eq = strchr(s, '=');
    if (!eq) continue;
    *eq = 0;
    char *k = trim(s), *v = trim(eq + 1);
    if (!strcmp(k, "out")) strncpy(g_cfg.szOut, v, MAX_PATH - 1);
    else if (!strcmp(k, "snap_dir")) strncpy(g_cfg.szSnapDir, v, MAX_PATH - 1);
    else if (!strcmp(k, "clock")) g_cfg.bVirtualClock = !strcmp(v, "virtual");
    else if (!strcmp(k, "clock_step")) g_cfg.dwClockStep = strtoul(v, 0, 0);
    else if (!strcmp(k, "clock_storm")) g_cfg.bClockStorm = truthy(v);
    else if (!strcmp(k, "sleep_cap")) g_cfg.dwSleepCap = strtoul(v, 0, 0);
    else if (!strcmp(k, "updates")) g_cfg.dwUpdates = strtoul(v, 0, 0);
    else if (!strcmp(k, "keep_active")) g_cfg.bKeepActive = truthy(v);
    else if (!strcmp(k, "max_frames")) g_cfg.dwMaxFrames = strtoul(v, 0, 0);
    else if (!strcmp(k, "stats_every")) g_cfg.dwStatsEvery = strtoul(v, 0, 0);
    else if (!strcmp(k, "sync_every")) g_cfg.dwSyncEvery = strtoul(v, 0, 0);
    else if (!strcmp(k, "perf_every")) g_cfg.dwPerfEvery = strtoul(v, 0, 0);
    else if (!strcmp(k, "snap_every")) g_cfg.dwSnapEvery = strtoul(v, 0, 0);
    else if (!strcmp(k, "snap_screens")) g_cfg.bSnapScreens = truthy(v);
    else if (!strcmp(k, "snap_screen")) g_cfg.bSnapScreen = truthy(v);
    else if (!strcmp(k, "snap_move")) g_cfg.bSnapMove = truthy(v);
    else if (!strcmp(k, "view_every")) g_cfg.dwViewEvery = strtoul(v, 0, 0);
    else if (!strcmp(k, "exit_at_end")) g_cfg.bExitAtEnd = truthy(v);
    else if (!strcmp(k, "save_frame")) g_cfg.dwSaveFrame = strtoul(v, 0, 0);
    else if (!strcmp(k, "save_name")) strncpy(g_cfg.szSaveName, v, sizeof g_cfg.szSaveName - 1);
    else if (!strcmp(k, "load_file")) strncpy(g_cfg.szLoadFile, v, sizeof g_cfg.szLoadFile - 1);
    else if (!strcmp(k, "pin_seed")) g_cfg.bPinSeed = truthy(v);
    else if (!strcmp(k, "stall_secs")) g_cfg.dwStallSecs = strtoul(v, 0, 0);
    else if (!strcmp(k, "menu_timeout_secs")) g_cfg.dwMenuTimeoutSecs = strtoul(v, 0, 0);
    else if (!strcmp(k, "raw_hash")) g_cfg.bRawHash = truthy(v);
    else if (!strcmp(k, "dump_frame")) g_cfg.dwDumpFrame = strtoul(v, 0, 0);
    else if (!strcmp(k, "check_observe")) g_cfg.bCheckObserve = truthy(v);
    else if (!strcmp(k, "menu")) parse_menu(v);
  }
  fclose(f);
}

// ---------------------------------------------------------------------------
// The log. One writer at a time; the watchdog thread only writes when the game
// thread has stopped making progress.

static CRITICAL_SECTION g_logLock;
static HANDLE g_hLog = INVALID_HANDLE_VALUE;
static char g_logBuf[1 << 16];
static DWORD g_logLen;
static LARGE_INTEGER g_qpcFreq, g_qpcStart;

static double wall_ms() {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return (double)(now.QuadPart - g_qpcStart.QuadPart) * 1000.0 / (double)g_qpcFreq.QuadPart;
}

static void log_flush_locked() {
  if (g_hLog != INVALID_HANDLE_VALUE && g_logLen) {
    DWORD w;
    WriteFile(g_hLog, g_logBuf, g_logLen, &w, NULL);
  }
  g_logLen = 0;
}

static void rt_log(const char *fmt, ...) {
  char line[1024];
  va_list ap;
  va_start(ap, fmt);
  int n = _vsnprintf(line, sizeof line - 2, fmt, ap);
  va_end(ap);
  if (n < 0) n = (int)sizeof line - 2;
  line[n++] = '\n';
  EnterCriticalSection(&g_logLock);
  if (g_logLen + (DWORD)n > sizeof g_logBuf) log_flush_locked();
  memcpy(g_logBuf + g_logLen, line, n);
  g_logLen += n;
  LeaveCriticalSection(&g_logLock);
}

static void log_flush() {
  EnterCriticalSection(&g_logLock);
  log_flush_locked();
  if (g_hLog != INVALID_HANDLE_VALUE) FlushFileBuffers(g_hLog);
  LeaveCriticalSection(&g_logLock);
}

// The same without waiting, for a fault or process exit, which can come while
// a thread that will never run again holds the lock.
static void log_flush_try() {
  if (!TryEnterCriticalSection(&g_logLock)) return;
  log_flush_locked();
  if (g_hLog != INVALID_HANDLE_VALUE) FlushFileBuffers(g_hLog);
  LeaveCriticalSection(&g_logLock);
}

// ---------------------------------------------------------------------------
// Snapshots: the game's 8-bit draw buffer with the game palette, as a BMP, or
// with snap_screen the whole 640x480 screen: the map, the console, the dialogs
// and the cursor as the player sees them. The oracle's screen is Storm's SDraw
// surface, locked here as its blitters lock it (ordinals 350 and 356). An
// engine build draws into its own buffer instead (src117/port/render/port_present.cpp)
// and exports it as oc_screen, which is used when the exe has it, with its
// size as oc_screen_size (the view's, wider than 640 in a wide view).

static void snap_unguarded(const char *tag);

typedef int(WINAPI *PFN_OC_SCREEN)(void **, int *);
typedef int(WINAPI *PFN_OC_SCREEN_SIZE)(int *, int *);
typedef void(WINAPI *PFN_OC_VIEW_TOGGLE)(void);
typedef int(WINAPI *PFN_SDRAW_LOCK)(int, RECT *, void **, int *, int);
typedef int(WINAPI *PFN_SDRAW_UNLOCK)(int, void *, int, RECT *);
static PFN_OC_SCREEN g_ocScreen;
static PFN_OC_SCREEN_SIZE g_ocScreenSize;
static PFN_OC_VIEW_TOGGLE g_ocViewToggle;  // the engine's F5 (src117/port/render/port_view.cpp)
static PFN_SDRAW_LOCK g_sdrawLock;
static PFN_SDRAW_UNLOCK g_sdrawUnlock;

static void module_name(HMODULE h, char *out, DWORD cap);

// The module with the engine's exports: Hyperion.exe itself, which has the engine
// linked in; NULL for the oracle.
static HMODULE engine_module() {
  HMODULE hExe = GetModuleHandleA(NULL);
  return GetProcAddress(hExe, "oc_observe") ? hExe : NULL;
}

// Finds oc_screen in the engine, else Storm's lock pair.
static void screen_find() {
  static bool bLooked;
  if (bLooked) return;
  bLooked = true;
  if (HMODULE hEngine = engine_module()) {
    g_ocScreen = reinterpret_cast<PFN_OC_SCREEN>(GetProcAddress(hEngine, "oc_screen"));
    g_ocScreenSize = reinterpret_cast<PFN_OC_SCREEN_SIZE>(GetProcAddress(hEngine, "oc_screen_size"));
    g_ocViewToggle = reinterpret_cast<PFN_OC_VIEW_TOGGLE>(GetProcAddress(hEngine, "oc_view_toggle"));
  }
  HMODULE storm = GetModuleHandleA("storm.dll");
  if (!g_ocScreen && storm) {
    g_sdrawLock = reinterpret_cast<PFN_SDRAW_LOCK>(GetProcAddress(storm, MAKEINTRESOURCEA(350)));
    g_sdrawUnlock = reinterpret_cast<PFN_SDRAW_UNLOCK>(GetProcAddress(storm, MAKEINTRESOURCEA(356)));
  }
  rt_log("V,screen=%s", g_ocScreen ? "oc_screen" : (g_sdrawLock && g_sdrawUnlock ? "sdraw" : "none"));
}

static void write_bmp(const char *tag, const BYTE *px, int w, int h, int pitch);
static const OcObserveApi *g_obs;

// The source bitmap can be swapped or freed while a screen changes, so a fault
// while reading it skips the snapshot instead of ending the run.
static void snap(const char *tag) {
  if (!g_cfg.szSnapDir[0]) return;
  RT_TRY
    snap_unguarded(tag);
  RT_EXCEPT
    rt_log("X,snap_fault,%s", tag);
  RT_END
}

static void snap_unguarded(const char *tag) {
  if (g_cfg.bSnapScreen) {
    screen_find();
    void *pBits = NULL;
    int nPitch = 0;
    if (g_ocScreen) {
      int w = 640, h = 480;
      if (g_ocScreenSize) g_ocScreenSize(&w, &h);
      if (g_ocScreen(&pBits, &nPitch) && pBits) write_bmp(tag, static_cast<BYTE *>(pBits), w, h, nPitch);
    } else if (g_sdrawLock && g_sdrawUnlock && g_sdrawLock(0, NULL, &pBits, &nPitch, 0)) {
      if (pBits) write_bmp(tag, static_cast<BYTE *>(pBits), 640, 480, nPitch);
      g_sdrawUnlock(0, pBits, 0, NULL);
    }
    return;
  }
#ifdef RT_WIDE
  rt_log("X,snap_needs_screen,%s", tag);
#else
  // In the menus the dialogs draw into the bitmap sgpDlgTargetBuf_g names; in a
  // game the screen is _img_draw_buf.
  DWORD bmp = A(VA_DRAW_BUF);
  if (rd<WORD>(VA_GAME_MODE) != 3 && rd<DWORD>(VA_DLG_TARGET)) bmp = rd<DWORD>(VA_DLG_TARGET);
  short w = *reinterpret_cast<short *>(bmp), h = *reinterpret_cast<short *>(bmp + 2);
  const BYTE *px = *reinterpret_cast<const BYTE **>(bmp + 4);
  if (!px || w <= 0 || h <= 0 || w > 4096 || h > 4096) return;
  write_bmp(tag, px, w, h, w);
#endif
}

static void write_bmp(const char *tag, const BYTE *px, int w, int h, int pitch) {
  char path[MAX_PATH];
  _snprintf(path, sizeof path, "%s\\%s.bmp", g_cfg.szSnapDir, tag);
  HANDLE hf = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
  if (hf == INVALID_HANDLE_VALUE) return;
  DWORD stride = ((DWORD)w + 3) & ~3u;
  BITMAPFILEHEADER bf = {};
  BITMAPINFOHEADER bi = {};
  bi.biSize = sizeof bi;
  bi.biWidth = w;
  bi.biHeight = h;
  bi.biPlanes = 1;
  bi.biBitCount = 8;
  bi.biClrUsed = 256;
  bi.biSizeImage = stride * h;
  bf.bfType = 0x4D42;
  bf.bfOffBits = sizeof bf + sizeof bi + 1024;
  bf.bfSize = bf.bfOffBits + bi.biSizeImage;
  DWORD wr_;
  WriteFile(hf, &bf, sizeof bf, &wr_, NULL);
  WriteFile(hf, &bi, sizeof bi, &wr_, NULL);
  BYTE pal[1024];
  const BYTE *gp = g_obs ? g_obs->pfnPalette() : reinterpret_cast<const BYTE *>(A(VA_GAME_PALETTE));
  for (int i = 0; i < 256; ++i) {
    pal[i * 4 + 0] = gp[i * 4 + 2];
    pal[i * 4 + 1] = gp[i * 4 + 1];
    pal[i * 4 + 2] = gp[i * 4 + 0];
    pal[i * 4 + 3] = 0;
  }
  WriteFile(hf, pal, sizeof pal, &wr_, NULL);
  static BYTE row[4096];
  for (int y = h - 1; y >= 0; --y) {
    memcpy(row, px + (DWORD)y * (DWORD)pitch, w);
    WriteFile(hf, row, stride, &wr_, NULL);
  }
  CloseHandle(hf);
}

// ---------------------------------------------------------------------------
// Hashing. A word-at-a-time mix: fast, and any single changed value changes it.

static inline DWORD mix(DWORD h, DWORD v) {
  h ^= v;
  h *= 0x01000193u;
  return (h << 13) | (h >> 19);
}

// The end of the unit table: where the next global starts in 1.17, which an engine
// may keep elsewhere.
static inline uintptr_t unit_table_end() { return A(VA_UNIT_TABLE) + 1700 * UNIT_SIZE; }

static inline DWORD unit_index(uintptr_t p) {
  if (p < A(VA_UNIT_TABLE) || p >= unit_table_end()) return 0;
  return (DWORD)((p - A(VA_UNIT_TABLE)) / UNIT_SIZE + 1);
}

// ---------------------------------------------------------------------------
// The game state, from the engine or from 1.17's memory. An engine build hands
// it over through oc_observe (src117/port/replays/port_observe.h); the oracle's, and
// hx's, is read at 1.17's addresses into the same records, so one path logs
// both, and a log does not depend on where the engine keeps things.

static DWORD g_obsFrames, g_obsDiffs;  // check_observe's counts

static void __cdecl on_frame(void);
static bool g_frameHook;  // the engine took the frame hook (on_frame)
static void observe_find() {
  static bool bLooked;
  if (bLooked) return;
  bLooked = true;
  if (HMODULE hEngine = engine_module()) {
    PFN_OC_OBSERVE pfn = reinterpret_cast<PFN_OC_OBSERVE>(GetProcAddress(hEngine, "oc_observe"));
    const OcObserveApi *pApi = pfn ? pfn() : NULL;
    if (pApi && pApi->dwVersion == OC_OBSERVE_VERSION) g_obs = pApi;
  }
  rt_log("V,observe=%s%s", g_obs ? "oc_observe" : "memory", g_obs && g_cfg.bCheckObserve ? ",checked" : "");
  // An engine keeps 1.17's data in a block of its own (src117/h/oc_data.h), so every
  // address this reads moves with it.
  if (g_obs) {
    g_obs->pfnSetFrameHook(on_frame);
    g_frameHook = true;
    g_delta = g_obs->pfnDataBase() - 0x4EB000u;
    g_address = g_obs->pfnAddress;
    rt_log("B,data_base=%08" RT_PX ",delta=%08" RT_PX, 0x4EB000u + g_delta, g_delta);
  }
#ifdef RT_WIDE
  if (!g_obs) rt_log("X,no_oc_observe");
#endif
}

// A single-player game seeds its random numbers from the clock (new_game), which
// also picks the race of a slot left random, so two runs of a loaded game agree
// only with the seed pinned. An engine takes it through oc_observe; the
// oracle's call to _time64 is pointed at hk_time64.
static __int64 __cdecl hk_time64(__int64 *) { return kPinnedSeed; }

static void pin_seed() {
  if (g_obs) {
    g_obs->pfnPinSeed(kPinnedSeed);
    rt_log("V,pin_seed,%08X,oc_observe", kPinnedSeed);
    return;
  }
#ifndef RT_WIDE
  BYTE *pCall = reinterpret_cast<BYTE *>(A(VA_TIME64_CALL));
  static const BYTE kCall[5] = {0xE8, 0xF3, 0xD1, 0x08, 0x00};  // call 0x4D11E1, _time64
  DWORD old;
  if (memcmp(pCall, kCall, 5) || !VirtualProtect(pCall, 5, PAGE_EXECUTE_READWRITE, &old)) {
    rt_log("X,pin_seed_no_call");
    return;
  }
  *reinterpret_cast<int32_t *>(pCall + 1) =
      static_cast<int32_t>(reinterpret_cast<uintptr_t>(&hk_time64) - reinterpret_cast<uintptr_t>(pCall + 5));
  VirtualProtect(pCall, 5, old, &old);
  FlushInstructionCache(GetCurrentProcess(), pCall, 5);
  rt_log("V,pin_seed,%08X,patched", kPinnedSeed);
#endif
}

static void state_from_memory(OcObserveState *s) {
  memset(s, 0, sizeof *s);
  s->dwGameMode = rd<WORD>(VA_GAME_MODE);
  s->nGlueScreen = rd<int>(VA_GLUE_SCREEN);
  s->dwFrame = rd<DWORD>(VA_FRAME);
  s->dwPlayback = rd<DWORD>(VA_PLAYBACK);
  s->dwReplayFrames = rd<DWORD>(VA_REPLAY_FRAMES);
  s->dwReplaySeed = rd<DWORD>(VA_REPLAY_SEED);
  s->dwGameSpeed = rd<DWORD>(VA_GAME_SPEED);
  s->dwMsPerFrame = s->dwGameSpeed < 7 ? rd<DWORD>(VA_TICKS_PER_FRAME + 4 * s->dwGameSpeed) : 0;
  s->dwRandState = rd<DWORD>(VA_RAND_STATE);
  s->dwRandCount = rd<DWORD>(VA_RAND_COUNT);
  for (int i = 0; i < 12; ++i) {
    s->anMinerals[i] = rd<int>(VA_MINERALS + 4 * i);
    s->anGas[i] = rd<int>(VA_GAS + 4 * i);
  }
}

// The used list in order, at most nMax units; a link out of the table stops it.
// CUnit is 1.17's layout here, so the x64 build has none.
static uint32_t units_from_memory(OcObserveUnit *pUnits, uint32_t nMax, uint32_t *pbBroken) {
  uint32_t n = 0;
  *pbBroken = 0;
#ifdef RT_WIDE
  (void)pUnits, (void)nMax;
  return n;
#else
  for (DWORD p = rd<DWORD>(VA_UNIT_USED_HEAD); p && n < nMax; p = *reinterpret_cast<const DWORD *>(p + U_NEXT)) {
    if (p < A(VA_UNIT_TABLE) || p >= unit_table_end()) {
      *pbBroken = 1;
      break;
    }
    const BYTE *u = reinterpret_cast<const BYTE *>(p);
    OcObserveUnit &r = pUnits[n++];
    r.dwSlot = unit_index(p);
    r.dwTypePlayer = *reinterpret_cast<const WORD *>(u + U_TYPE) | ((DWORD)u[U_PLAYER] << 16);
    r.dwHitPoints = *reinterpret_cast<const DWORD *>(u + U_HP);
    r.dwShields = *reinterpret_cast<const DWORD *>(u + U_SHIELDS);
    r.dwEnergy = *reinterpret_cast<const WORD *>(u + U_ENERGY);
    r.dwPosition = *reinterpret_cast<const DWORD *>(u + U_X);
    r.dwOrder = u[U_ORDER] | ((DWORD)u[U_ORDER_STATE] << 8);
    r.dwOrderTarget = unit_index(*reinterpret_cast<const DWORD *>(u + U_ORDER_TGT));
    r.dwFlags = *reinterpret_cast<const DWORD *>(u + U_FLAGS);
  }
  return n;
#endif
}

static void read_state(OcObserveState *s) {
  if (g_obs) g_obs->pfnState(s);
  else state_from_memory(s);
}

static uint32_t read_units(OcObserveUnit *pUnits, uint32_t nMax, uint32_t *pbBroken) {
  return g_obs ? g_obs->pfnUnits(pUnits, nMax, pbBroken) : units_from_memory(pUnits, nMax, pbBroken);
}

// check_observe: what the engine handed over against 1.17's memory, which is
// the same memory while the engine runs in 1.17's image. The first differences
// are logged as X lines; the count goes in the V line at the end.
static OcObserveUnit g_unitsMem[1700];
static void check_observe(const OcObserveState &st, const OcObserveUnit *pUnits, uint32_t nUnits, uint32_t bBroken) {
  OcObserveState mem;
  state_from_memory(&mem);
  uint32_t bMemBroken = 0;
  uint32_t nMem = units_from_memory(g_unitsMem, 1700, &bMemBroken);
  ++g_obsFrames;
  const char *what = NULL;
  DWORD nAt = 0;
  if (memcmp(&st, &mem, sizeof st) != 0) {
    what = "state";
  }
#ifndef RT_WIDE
  else if (nUnits != nMem || bBroken != bMemBroken) {
    what = "unit_count";
  } else {
    for (uint32_t i = 0; i < nUnits; ++i) {
      if (memcmp(&pUnits[i], &g_unitsMem[i], sizeof pUnits[i]) != 0) {
        what = "unit";
        nAt = i;
        break;
      }
    }
  }
#else
  (void)pUnits, (void)nUnits, (void)bBroken, (void)nMem, (void)bMemBroken;
#endif
  if (what && g_obsDiffs++ < 20)
    rt_log("X,observe_diff,%u,%s,%u", st.dwFrame, what, nAt);
}

// ---------------------------------------------------------------------------
// Per-frame state record.

static DWORD g_lastFrame = 0xFFFFFFFFu;
static DWORD g_framesLogged;
static DWORD g_hookFrames;  // a Hyperion game's frames, read at the engine's frame hook
static double g_gameStartMs;
static bool g_inGame, g_sawGame, g_sawStart, g_finished;
static volatile LONG g_progressTick;  // real GetTickCount of the last progress

static void note_progress() { InterlockedExchange(&g_progressTick, (LONG)GetTickCount()); }

static LONG main_calls();
static LONG clock_now();

// Centres the view on a player's unit, the nPick-th of the used list's units of
// players 0 to 7 counted round, as the game's own centre-view trigger does.
// Neutral units are passed over: most sit in ground nobody has explored, which
// draws black. Called between two game updates, where triggers run.
typedef void(__fastcall *PFN_CENTER_ON_MTX)(int, int);
static void center_on_unit(DWORD nPick, const OcObserveUnit *pUnits, uint32_t nUnits, uint32_t bBroken) {
  if (bBroken) return;
  DWORD nPlayers = 0;
  for (uint32_t i = 0; i < nUnits; ++i)
    if ((BYTE)(pUnits[i].dwTypePlayer >> 16) < 8) ++nPlayers;
  if (nPlayers == 0) return;
  DWORD nIndex = nPick % nPlayers;
  for (uint32_t i = 0; i < nUnits; ++i) {
    if ((BYTE)(pUnits[i].dwTypePlayer >> 16) >= 8 || nIndex-- != 0) continue;
    int x = (short)(pUnits[i].dwPosition & 0xFFFF), y = (short)(pUnits[i].dwPosition >> 16);
    if (g_obs) g_obs->pfnCentre(x / 32, y / 32);
#ifndef RT_WIDE
    else reinterpret_cast<PFN_CENTER_ON_MTX>(A(VA_CENTER_ON_MTX))(x / 32, y / 32);
#endif
    return;
  }
}

// The hash of every used unit's raw 1.17 bytes, and the Y dump. It reads 1.17's
// memory, so it only means something while the engine runs in 1.17's image; the
// 64-bit build turns it off.
static DWORD raw_hash(DWORD frame) {
  DWORD hRaw = 0x811C9DC5u, n = 0;
#ifdef RT_WIDE
  (void)frame, (void)n;
  return hRaw;
#else
  if (!g_cfg.bRawHash && g_cfg.dwDumpFrame != frame) return hRaw;
  for (DWORD p = rd<DWORD>(VA_UNIT_USED_HEAD); p && n < 1700; p = *reinterpret_cast<const DWORD *>(p + U_NEXT), ++n) {
    if (p < A(VA_UNIT_TABLE) || p >= unit_table_end()) break;
    const BYTE *u = reinterpret_cast<const BYTE *>(p);
    if (g_cfg.bRawHash) {
      // Every dword of the unit except pPath (+0x100), which points into a heap
      // block whose place differs between builds: the same path record at a
      // different address.
      // A pointer field that points at 1.17's data where an engine keeps it
      // (observe_find) is hashed as the 1.17 address it stands for, so the hash does
      // not depend on where the engine keeps it. Only CUnit's pointer fields and the
      // pointer-sized integers that hold one (src117/h/CUnit.h): another field can
      // hold a value in that range by chance.
      static const WORD kPointerFields[] = {0x00, 0x04, 0x0C, 0x14, 0x5C, 0x68, 0x6C, 0x70,
                                            0x74, 0x78, 0x7C, 0x80, 0xC0, 0xC4, 0xC8, 0xD0,
                                            0xD4, 0xD8, 0xEC, 0xF0, 0xF4, 0xF8, 0xFC, 0x11C,
                                            0x134};
      const DWORD *w = reinterpret_cast<const DWORD *>(u);
      for (DWORD i = 0; i < UNIT_SIZE / 4; ++i) {
        if (i == U_PATH / 4) continue;
        DWORD v = w[i];
        bool bPointer = false;
        for (DWORD k = 0; k < sizeof kPointerFields / sizeof kPointerFields[0] && !bPointer; ++k)
          bPointer = kPointerFields[k] == i * 4;
        if (bPointer && g_obs) {
          if (DWORD o = g_obs->pfnOriginal(v)) v = o;
        } else if (bPointer && g_delta && v - A(0x4EB000u) < 0x6DF000u - 0x4EB000u) {
          v -= g_delta;
        }
        hRaw = mix(hRaw, v);
      }
    }
    if (g_cfg.dwDumpFrame == frame) {
      char hex[UNIT_SIZE * 2 + 1];
      for (DWORD i = 0; i < UNIT_SIZE; ++i) _snprintf(hex + i * 2, 3, "%02X", u[i]);
      rt_log("Y,%u,%u,%s", frame, unit_index(p), hex);
    }
  }
  return hRaw;
#endif
}

// The slots the P and Q lines cover: 1.17's 8 lobby slots, or with an engine whose game has
// Hyperion's slots past 11 (port_observe.h, version 9), every slot but neutral's.
static int log_slots() {
  const uint32_t n = g_obs ? g_obs->pfnSlots() : 12;
  return n > 12 ? (int)n : 8;
}

// A slot's numbers past 1.17's 12, which only the engine can give.
static OcObserveSlot slot_numbers(int i) {
  OcObserveSlot s;
  memset(&s, 0, sizeof s);
  if (g_obs) g_obs->pfnSlot((uint32_t)i, &s);
  return s;
}

// The hash of the stats: resources on hand and gathered, the score counters, the
// counts per unit type, and what each player has researched and upgraded; then, in a
// game with Hyperion's slots, the same for each slot past 11.
static DWORD stat_hash() {
  DWORD h = 0x811C9DC5u;
  const DWORD aDwords[][2] = {{VA_MINERALS, 12}, {VA_GAS, 12}, {VA_MINERALS_MINED, 12}, {VA_GAS_MINED, 12},
                              {VA_GAME_COUNTS, 28 * 12}, {VA_UNIT_COUNTS, 4 * 228 * 12}};
  for (const auto &r : aDwords)
    for (DWORD i = 0; i < r[1]; ++i) h = mix(h, rd<DWORD>(r[0] + 4 * i));
  const DWORD aBytes[][2] = {{VA_TECH_SC, 12 * 24}, {VA_TECH_BW, 12 * 20}, {VA_UPGRADES_SC, 12 * 46},
                             {VA_UPGRADES_BW, 12 * 15}};
  for (const auto &r : aBytes)
    for (DWORD i = 0; i < r[1]; ++i) h = mix(h, rd<BYTE>(r[0] + i));
  for (int i = 12; i < log_slots(); ++i) {
    const OcObserveSlot s = slot_numbers(i);
    const DWORD *p = reinterpret_cast<const DWORD *>(&s);
    for (size_t k = 0; k < sizeof s / 4; ++k) h = mix(h, p[k]);
  }
  return h;
}

// The score screen's numbers for each slot log_slots covers: minerals and gas gathered,
// then the 28 score counters.
static void log_scores(DWORD frame) {
  for (int i = 0; i < log_slots(); ++i) {
    if (i == 11 && log_slots() > 8) continue;  // neutral
    char line[512];
    int n;
    if (i < 12) {
      n = _snprintf(line, sizeof line, "Q,%u,%d,%d,%d", frame, i, rd<int>(VA_MINERALS_MINED + 4 * i),
                    rd<int>(VA_GAS_MINED + 4 * i));
      for (int k = 0; k < 28 && n > 0 && n < (int)sizeof line; ++k)
        n += _snprintf(line + n, sizeof line - n, ",%u", rd<DWORD>(VA_GAME_COUNTS + 4 * (k * 12 + i)));
    } else {
      const OcObserveSlot s = slot_numbers(i);
      n = _snprintf(line, sizeof line, "Q,%u,%d,%d,%d", frame, i, s.nMineralsGathered, s.nGasGathered);
      for (int k = 0; k < 28 && n > 0 && n < (int)sizeof line; ++k)
        n += _snprintf(line + n, sizeof line - n, ",%u", s.adwScores[k]);
    }
    rt_log("%s", line);
  }
}

// The state's check of a Hyperion game or replay, as the engine keeps it.
static const char *const kSyncStates[] = {"none", "recording", "ok", "mismatch", "refused"};
static bool g_syncMismatchLogged;
static void log_sync_mismatch(DWORD frame) {
  if (!g_obs || g_syncMismatchLogged) return;
  OcObserveSync sync;
  g_obs->pfnSync(&sync);
  if (sync.dwState != 3) return;
  g_syncMismatchLogged = true;
  rt_log("E,%u,sync_mismatch,%u,%X", frame, sync.dwFirstBad, sync.dwBadDomains);
}

// The used units, as many as the unit table holds: 1.17's 1,700, or an engine's own size.
static OcObserveUnit *g_units;
static uint32_t g_nUnitCap;
// The buffer holds the unit table of the game being played, grown when a later game's is
// larger (a game's table follows its slots).
static void units_alloc() {
  uint32_t nCap = g_obs ? g_obs->pfnUnitCap() : 1700;
  if (nCap < 1700) nCap = 1700;
  if (g_units && nCap <= g_nUnitCap) return;
  delete[] g_units;
  g_nUnitCap = nCap;
  g_units = new OcObserveUnit[g_nUnitCap];
}

static void log_frame(const OcObserveState &st, void *pCaller) {
  const DWORD frame = st.dwFrame;
  uint32_t bBroken = 0;
  units_alloc();
  const uint32_t nUnits = read_units(g_units, g_nUnitCap, &bBroken);
  if (g_obs && g_cfg.bCheckObserve) check_observe(st, g_units, nUnits, bBroken);
  DWORD hSem = 0x811C9DC5u;
  DWORD aUnits[256] = {0}, aHp[256] = {0};
  for (uint32_t i = 0; i < nUnits; ++i) {
    const OcObserveUnit &u = g_units[i];
    hSem = mix(hSem, u.dwSlot);
    hSem = mix(hSem, u.dwTypePlayer);
    hSem = mix(hSem, u.dwHitPoints);
    hSem = mix(hSem, u.dwShields);
    hSem = mix(hSem, u.dwEnergy);
    hSem = mix(hSem, u.dwPosition);
    hSem = mix(hSem, u.dwOrder);
    hSem = mix(hSem, u.dwOrderTarget);
    hSem = mix(hSem, u.dwFlags);
    if (g_cfg.dwDumpFrame == frame)  // the fields h_sem hashes, which every build has
      rt_log("Z,%u,%u,%08X,%u,%u,%u,%08X,%08X,%08X,%08X", frame, u.dwSlot, u.dwTypePlayer, u.dwHitPoints,
             u.dwShields, u.dwEnergy, u.dwPosition, u.dwOrder, u.dwOrderTarget, u.dwFlags);
    const BYTE bPlayer = (BYTE)(u.dwTypePlayer >> 16);
    ++aUnits[bPlayer];
    aHp[bPlayer] += u.dwHitPoints >> 8;
  }
  if (bBroken) hSem = mix(hSem, 0xBADu);
  const DWORD hRaw = raw_hash(frame);

  rt_log("F,%u,%08X,%u,%u,%08X,%08X,%08X", frame, st.dwRandState, st.dwRandCount, nUnits, hSem, hRaw,
         stat_hash());

  if (g_framesLogged < 3)
    rt_log("R,%u,%p", frame, pCaller);  // which call site sampled the frame

  // A Hyperion game's state hash, and its replay's check against the game it recorded
  // (src117/port/loop/port_synchash.h; port_hxreplay.h).
  if (g_obs && g_cfg.dwSyncEvery && frame % g_cfg.dwSyncEvery == 0) {
    OcObserveHash hh;
    g_obs->pfnHash(&hh);
    rt_log("H,%u,%08X,%u,%016llX,%016llX,%016llX,%016llX,%016llX,%016llX", frame, hh.dwRandState,
           hh.dwRandCount, hh.aqDomain[0], hh.aqDomain[1], hh.aqDomain[2], hh.aqDomain[3], hh.aqDomain[4],
           hh.aqDomain[5]);
  }
  log_sync_mismatch(frame);

  // The save the checks compare, as the save menu writes it, with a fixed time in
  // place of the clock's so that every build writes the same header. Between two
  // game updates, where the save menu's command runs too.
  if (g_cfg.dwSaveFrame && frame == g_cfg.dwSaveFrame) {
    const char *pszName = g_cfg.szSaveName[0] ? g_cfg.szSaveName : "rt_save";
    const DWORD dwStamp = 0x5A5A0000u | (frame & 0xFFFFu);
    // A single-player save wants exactly one human (write_save_header), and a
    // replay has one per player, as a game loaded from a replay's save does, so
    // every save here is written as a multiplayer game's.
    const BYTE bMulti = rd<BYTE>(VA_MULTIPLAYER);
    wr<BYTE>(VA_MULTIPLAYER, 1);
    int nOk;
    if (g_obs) {
      nOk = g_obs->pfnSave(pszName, dwStamp);
    } else {
#ifdef RT_WIDE
      nOk = -1;
#else
      typedef BOOL(__fastcall * PFN_SAVE)(DWORD, const char *, int);
      nOk = reinterpret_cast<PFN_SAVE>(A(VA_SAVE_GAME))(dwStamp, pszName, 0);
#endif
    }
    wr<BYTE>(VA_MULTIPLAYER, bMulti);
    rt_log("V,save,%u,%s,%d", frame, pszName, nOk);
  }

  // A replay's view stays where it starts, so without this the snapshots see
  // one corner of the map. The unit is the same in every build, their units
  // being the same; a stride through the list spreads the picks over players.
  if (g_cfg.bSnapMove && g_cfg.dwSnapEvery && frame % g_cfg.dwSnapEvery == g_cfg.dwSnapEvery / 2)
    center_on_unit(frame / g_cfg.dwSnapEvery * 37u, g_units, nUnits, bBroken);

  // An engine build's view switches between 640x480 and its wide view, as F5
  // switches it: a request the engine takes between frames. A quarter of the way
  // into each period, so a snapshot and the camera's move at the half both see
  // the view the switch left.
  if (g_cfg.dwViewEvery && frame % g_cfg.dwViewEvery == g_cfg.dwViewEvery / 4) {
    screen_find();
    if (g_ocViewToggle) {
      g_ocViewToggle();
      rt_log("V,view_toggle,%u", frame);
    }
  }

  if (g_cfg.dwStatsEvery && frame % g_cfg.dwStatsEvery == 0) {
    for (int i = 0; i < log_slots(); ++i) {
      if (i == 11 && log_slots() > 8) continue;  // neutral
      int m = i < 12 ? st.anMinerals[i] : 0, g = i < 12 ? st.anGas[i] : 0;
      if (i >= 12) {
        const OcObserveSlot s = slot_numbers(i);
        m = s.nMinerals;
        g = s.nGas;
      }
      if (m || g || aUnits[i])
        rt_log("P,%u,%d,%d,%d,%u,%u", frame, i, m, g, aUnits[i], aHp[i]);
    }
    log_scores(frame);
  }
  if (g_cfg.dwPerfEvery && frame % g_cfg.dwPerfEvery == 0)
    rt_log("T,%u,%.1f", frame, wall_ms() - g_gameStartMs);
  if (g_cfg.dwSnapEvery && frame % g_cfg.dwSnapEvery == 0) {
    char tag[32];
    _snprintf(tag, sizeof tag, "f%06u", frame);
    snap(tag);
    rt_log("N,%u,%ld", frame, main_calls());
  }
  ++g_framesLogged;
  note_progress();
}

static void finish(const char *reason) {
  if (g_finished) return;
  g_finished = true;
  double ms = g_inGame || g_sawGame ? wall_ms() - g_gameStartMs : 0.0;
  OcObserveState st;
  read_state(&st);
  DWORD frame = st.dwFrame;
  if (g_obs && g_cfg.bCheckObserve) rt_log("V,observe_check,%u,%u", g_obsFrames, g_obsDiffs);
  if (g_obs) {
    // The presenter, and the GPU presenter's read-back check (docs/renderer.md, R4a).
    char szName[64];
    uint32_t nChecked = 0, nMismatched = 0;
    g_obs->pfnPresenter(szName, sizeof szName, &nChecked, &nMismatched);
    for (char *p = szName; *p; ++p)
      if (*p == ',') *p = ' ';
    rt_log("V,presenter,%s,%u,%u", szName, nChecked, nMismatched);
    // A Hyperion replay's check against the game it recorded: its state, the checks in
    // the replay and those made, and the first frame that differed with its domains.
    OcObserveSync sync;
    g_obs->pfnSync(&sync);
    for (char *p = sync.szReason; *p; ++p)
      if (*p == ',') *p = ';';
    rt_log("V,sync,%s,%u,%u,%u,%X,%s", sync.dwState < 5 ? kSyncStates[sync.dwState] : "?", sync.dwRecorded,
           sync.dwChecked, sync.dwFirstBad, sync.dwBadDomains, sync.szReason);
    // A Hyperion game's frames read at the engine's frame hook.
    if (g_hookFrames) rt_log("V,frame_hook,%u", g_hookFrames);
  }
  rt_log("S,%s,%u,%u,%.1f,%.1f", reason, frame, g_framesLogged, ms,
       ms > 0 ? g_framesLogged * 1000.0 / ms : 0.0);
  log_flush();
  if (g_cfg.bExitAtEnd) TerminateProcess(GetCurrentProcess(), 0);
}

// ---------------------------------------------------------------------------
// Menu driver: posts key presses to the game window from the script in the
// config, one step at a time, each once its (game mode, screen) state has held
// for its settle time.

// Logs every control of the active glue dialog: index, kind, flags, rectangle
// and text. It is how a menu script finds its buttons.
static void dump_dialog_unguarded() {
#ifdef RT_WIDE
  rt_log("U,unsupported");  // ctrl_t is 1.17's layout here
  return;
#else
  DWORD dlg = rd<DWORD>(VA_GLUE_DLG);
  if (!dlg) { rt_log("U,none"); return; }
  int n = 0;
  for (DWORD c = *reinterpret_cast<DWORD *>(dlg + 0x42); c && n < 64; c = *reinterpret_cast<DWORD *>(c), ++n) {
    const short *rc = reinterpret_cast<const short *>(c + 4);
    const char *text = *reinterpret_cast<const char **>(c + 0x14);
    char buf[48] = {0};
    if (text && !IsBadStringPtrA(text, 40)) {
      strncpy(buf, text, sizeof buf - 1);
      for (char *q = buf; *q; ++q) if (*q == ',' || (unsigned char)*q < 32) *q = ' ';
    }
    rt_log("U,%d,type=%u,flags=%08X,rect=%d:%d:%d:%d,%s", *reinterpret_cast<short *>(c + 0x20),
           *reinterpret_cast<WORD *>(c + 0x22), *reinterpret_cast<DWORD *>(c + 0x18), rc[0], rc[1], rc[2], rc[3],
           buf);
  }
#endif
}

static void dump_dialog() {
  RT_TRY
    dump_dialog_unguarded();
  RT_EXCEPT
    rt_log("X,dump_fault");
  RT_END
}

// Ends the active glue dialog with result n, as a push button with index n
// does (ctrl_flc_evn): publish the index as gnDlgReturnVal and send the dialog
// event 2 through ctrl_evt. It stands in for a button a screen does not show.
// An engine build does it itself, as none of 1.17's code may be in the process.
typedef int(__fastcall *PFN_CTRL_EVT)(void *, void *);
static void end_dialog(int n) {
  if (g_obs) {
    if (!g_obs->pfnGlueDialog()) rt_log("X,end_dialog_no_dlg");
    g_obs->pfnEndDialog(n);
    return;
  }
#ifndef RT_WIDE
  DWORD dlg = rd<DWORD>(VA_GLUE_DLG);
  if (!dlg) { rt_log("X,end_dialog_no_dlg"); return; }
  BYTE evt[0x14] = {0};
  *reinterpret_cast<DWORD *>(evt) = 2;
  *reinterpret_cast<WORD *>(evt + 0x0C) = 0x0E;
  wr<long>(VA_DLG_RETURN, n);
  reinterpret_cast<PFN_CTRL_EVT>(A(VA_CTRL_EVT))(reinterpret_cast<void *>(dlg), evt);
#endif
}

static int g_step;
static DWORD g_stateSince;  // real GetTickCount when the current state began
static int g_lastMode = -2, g_lastScreen = -2;
static const char *g_keyCursor;  // mid-step position in the key string
static DWORD g_keyWaitUntil;

#if !defined(_WIN32)
// What the menus' keys need of user32's: a character's key (letters and digits are their
// own), and no scan code.
static SHORT VkKeyScanA(char ch) {
  return (ch >= 'a' && ch <= 'z') ? (SHORT)(ch - 'a' + 'A') : (SHORT)(unsigned char)ch;
}
static UINT MapVirtualKeyA(UINT, UINT) { return 0; }
#endif

static void post_key(HWND hWnd, UINT vk, char ch) {
  UINT sc = MapVirtualKeyA(vk, 0);
  LPARAM down = 1 | (sc << 16);
  LPARAM up = down | (1u << 30) | (1u << 31);
  PostMessageA(hWnd, WM_KEYDOWN, vk, down);
  if (ch) PostMessageA(hWnd, WM_CHAR, (BYTE)ch, down);
  PostMessageA(hWnd, WM_KEYUP, vk, up);
}

// Sends keys from the cursor until the string ends or a {WAITn} is reached.
// Returns true when the string is done.
static bool send_keys(HWND hWnd) {
  while (*g_keyCursor) {
    const char *k = g_keyCursor;
    if (*k == '{') {
      const char *e = strchr(k, '}');
      if (!e) { g_keyCursor += strlen(k); break; }
      char name[32] = {0};
      size_t n = (size_t)(e - k - 1);
      if (n >= sizeof name) n = sizeof name - 1;
      memcpy(name, k + 1, n);
      g_keyCursor = e + 1;
      // {NAME*n} repeats a key n times.
      int nRepeat = 1;
      char *star = strchr(name, '*');
      if (star) { *star = 0; nRepeat = atoi(star + 1); if (nRepeat < 1) nRepeat = 1; }
      for (int r = 1; r < nRepeat; ++r) {
        if (!strcmp(name, "DOWN")) post_key(hWnd, VK_DOWN, 0);
        else if (!strcmp(name, "UP")) post_key(hWnd, VK_UP, 0);
      }
      if (!strncmp(name, "WAIT", 4)) {
        g_keyWaitUntil = GetTickCount() + (DWORD)atoi(name + 4);
        return false;
      }
      if (!strcmp(name, "ENTER")) post_key(hWnd, VK_RETURN, '\r');
      else if (!strcmp(name, "ESC")) post_key(hWnd, VK_ESCAPE, 27);
      else if (!strcmp(name, "DOWN")) post_key(hWnd, VK_DOWN, 0);
      else if (!strcmp(name, "UP")) post_key(hWnd, VK_UP, 0);
      else if (!strcmp(name, "LEFT")) post_key(hWnd, VK_LEFT, 0);
      else if (!strcmp(name, "RIGHT")) post_key(hWnd, VK_RIGHT, 0);
      else if (!strcmp(name, "TAB")) post_key(hWnd, VK_TAB, '\t');
      else if (!strcmp(name, "HOME")) post_key(hWnd, VK_HOME, 0);
      else if (!strcmp(name, "END")) post_key(hWnd, VK_END, 0);
      else if (!strcmp(name, "SPACE")) post_key(hWnd, VK_SPACE, ' ');
      else if (!strcmp(name, "BACK")) post_key(hWnd, VK_BACK, 8);
      else if (!strncmp(name, "SNAP", 4)) snap(name[4] ? name + 4 : "snap");
      else if (!strcmp(name, "DUMP")) dump_dialog();
      else if (!strncmp(name, "RETURN", 6)) end_dialog(atoi(name + 6));
      else if (!strcmp(name, "LOAD")) {
        // The Load screen's Ok for load_file (load_game): the save opened and game
        // mode 1 set, which ends the menus once the dialog is ended too.
#ifndef RT_WIDE
        typedef BOOL(__fastcall * PFN_OPEN_SAVE)(const char *);
        const BOOL bOk = reinterpret_cast<PFN_OPEN_SAVE>(A(VA_OPEN_SAVE))(g_cfg.szLoadFile);
        rt_log("V,load,menu,%s,%d", g_cfg.szLoadFile, bOk);
#endif
      }
      else if (!strncmp(name, "CLICK", 5)) {
        // {CLICKx:y} in the 640x480 client coordinates.
        int x = atoi(name + 5), y = 0;
        const char *c = strchr(name + 5, ':');
        if (c) y = atoi(c + 1);
        LPARAM xy = MAKELPARAM(x, y);
        PostMessageA(hWnd, WM_MOUSEMOVE, 0, xy);
        PostMessageA(hWnd, WM_LBUTTONDOWN, MK_LBUTTON, xy);
        PostMessageA(hWnd, WM_LBUTTONUP, 0, xy);
      }
      rt_log("K,%.1f,%d,{%s}", wall_ms(), g_step, name);
    } else {
      char ch = *k;
      SHORT vks = VkKeyScanA(ch);
      post_key(hWnd, (UINT)(vks & 0xFF), ch);
      rt_log("K,%.1f,%d,%c", wall_ms(), g_step, ch);
      ++g_keyCursor;
    }
  }
  return true;
}

static void menu_tick(int nMode, int nScreen) {
  if (g_step >= g_cfg.nSteps) return;
  HWND hWnd = g_obs ? static_cast<HWND>(g_obs->pfnWindow()) : rd<HWND>(VA_HWND);
  if (!hWnd) return;
  DWORD now = GetTickCount();
  if (g_keyCursor) {
    if ((int)(now - g_keyWaitUntil) < 0) return;
    if (send_keys(hWnd)) { g_keyCursor = NULL; ++g_step; g_stateSince = now; }
    return;
  }
  const MenuStep &st = g_cfg.aSteps[g_step];
  if (st.nGameMode != -1 && st.nGameMode != nMode) return;
  if (st.nScreen != -1 && st.nScreen != nScreen) return;
  const bool bDialog = g_obs ? g_obs->pfnGlueDialog() != NULL : rd<DWORD>(VA_GLUE_DLG) != 0;
  if (st.nScreen != -1 && !bDialog && nMode == 4) return;
  if (now - g_stateSince < (DWORD)st.nSettleMs) return;
  g_keyCursor = st.szKeys;
  g_keyWaitUntil = 0;
  if (send_keys(hWnd)) { g_keyCursor = NULL; ++g_step; g_stateSince = now; }
}

// ---------------------------------------------------------------------------
// The game-thread tick, run from every GetTickCount the game thread makes.

static DWORD g_mainTid;

// A frame is read at the first clock call after it advanced, as the oracle's runs read
// 1.17's games. A Hyperion game's frames are read at the engine's frame hook instead
// (pfnSetFrameHook, on_frame): a clock call can come from inside a frame, such as a
// message's timer, and would read the frame half run, and a replay makes such calls at
// other frames than the game it recorded (its local nation is another player, so the
// "cannot create more units" lines come to another player's units).
static bool hx_frames() {
  if (!g_obs || !g_frameHook) return false;
  OcObserveSync sync;
  g_obs->pfnSync(&sync);
  return sync.dwState != 0;  // none: a 1.17 game
}

// A frame's lines, once a frame; at the last frame the run ends, and this says so.
static bool frame_read(const OcObserveState &st, void *pCaller) {
  DWORD frame = st.dwFrame;
  if (frame == g_lastFrame) return false;
  g_lastFrame = frame;
  if (frame) log_frame(st, pCaller);
  DWORD total = st.dwReplayFrames;
  // The last frame's scores are the score screen's.
  const bool bEnd = (g_cfg.dwMaxFrames && frame >= g_cfg.dwMaxFrames) || (st.dwPlayback && total && frame >= total);
  if (bEnd && frame && !(g_cfg.dwStatsEvery && frame % g_cfg.dwStatsEvery == 0)) log_scores(frame);
  if (g_cfg.dwMaxFrames && frame >= g_cfg.dwMaxFrames) { finish("max_frames"); return true; }
  if (st.dwPlayback && total && frame >= total) { finish("replay_end"); return true; }
  return false;
}

static void on_tick(void *pCaller) {
  if (g_finished) return;
  observe_find();
  OcObserveState st;
  read_state(&st);
  int nMode = (int)st.dwGameMode;
  int nScreen = st.nGlueScreen;

  // The cursor's animation advances on clock calls, and how many the menus make
  // follows wall time and the window's state, so two runs show the same cursor
  // frame only by chance. With snapshots on, the cursor stays on its first
  // frame from the start: the counter at 0, the next deadline never reached.
  if (g_cfg.dwSnapEvery) {
    if (g_obs) {
      g_obs->pfnHoldCursor((DWORD)clock_now() + 0x40000000u);
    } else {
      wr<DWORD>(VA_CUR_FRAME, 0);
      wr<DWORD>(VA_CUR_NEXT_TICK, (DWORD)clock_now() + 0x40000000u);
    }
  }

  if (nMode != g_lastMode || nScreen != g_lastScreen) {
    rt_log("E,%.1f,mode,%d,%d,%u", wall_ms(), nMode, nScreen, st.dwPlayback);
    g_lastMode = nMode;
    g_lastScreen = nScreen;
    g_stateSince = GetTickCount();
    note_progress();
    if (g_cfg.bSnapScreens) {
      char tag[32];
      _snprintf(tag, sizeof tag, "screen_m%d_s%d", nMode, nScreen);
      snap(tag);
    }
  }

  // Mode 1 is a game starting. Back in the menus before mode 3 means the game
  // refused the replay (the "error loading scenario file" popup).
  if (nMode == 1) g_sawStart = true;
  if (g_sawStart && !g_sawGame && nMode == 4) {
    finish("load_failed");
    return;
  }

  if (nMode == 3) {
    if (!g_inGame) {
      g_inGame = g_sawGame = true;
      g_gameStartMs = wall_ms();
      g_lastFrame = 0xFFFFFFFFu;
      rt_log("G,%.1f,start,playback=%u,total_frames=%u,seed=%08X,speed=%u,ms_per_frame=%u",
           wall_ms(), st.dwPlayback, st.dwReplayFrames, st.dwReplaySeed, st.dwGameSpeed, st.dwMsPerFrame);
    }
    if (g_obs) {
      g_obs->pfnControl(g_cfg.bKeepActive, g_cfg.dwUpdates);
    } else {
      if (g_cfg.bKeepActive) wr<DWORD>(VA_WINDOW_ACTIVE, 1);
      if (g_cfg.dwUpdates) wr<DWORD>(VA_UPDATES_PER_FRM, g_cfg.dwUpdates);
    }

    if (!hx_frames() && frame_read(st, pCaller)) return;
  } else if (g_inGame) {
    g_inGame = false;
#ifdef RT_WIDE
    // A live game (Hyperion's --record, replaytest.py hxrecord) ends by being left; its last
    // frame's scores are logged as a replay's end logs them, so the two logs compare.
    if (g_lastFrame && g_lastFrame != 0xFFFFFFFFu && !(g_cfg.dwStatsEvery && g_lastFrame % g_cfg.dwStatsEvery == 0))
      log_scores(g_lastFrame);
#endif
    finish("left_game");
    return;
  }

  menu_tick(nMode, nScreen);
}

// ---------------------------------------------------------------------------
// Import hooks.

static volatile LONG g_vclock;
static volatile LONG g_inTick;

static volatile LONG g_callsAll, g_callsMain;
static LONG main_calls() { return g_callsMain; }
static LONG clock_now() { return g_cfg.bVirtualClock ? g_vclock : (LONG)g_realGTC(); }
static volatile DWORD g_lastOtherTid;
static void *volatile g_lastOtherCaller;

static DWORD WINAPI hk_GetTickCount(void) {
  InterlockedIncrement(&g_callsAll);
  if (GetCurrentThreadId() == g_mainTid) InterlockedIncrement(&g_callsMain);
  else { g_lastOtherTid = GetCurrentThreadId(); g_lastOtherCaller = _ReturnAddress(); }
  DWORD v = g_cfg.bVirtualClock ? (DWORD)InterlockedExchangeAdd(&g_vclock, (LONG)g_cfg.dwClockStep) +
                                      g_cfg.dwClockStep
                                : g_realGTC();
  if (GetCurrentThreadId() == g_mainTid && !g_inTick) {
    g_inTick = 1;
    on_tick(_ReturnAddress());
    g_inTick = 0;
  }
  return v;
}

// The engine's frame hook (pfnSetFrameHook): a Hyperion game's frame, read whole. A clock
// call from the frame's own lines, such as the save at --save-frame, reads nothing.
static void __cdecl on_frame(void) {
  if (g_finished || !g_inGame || !hx_frames()) return;
  OcObserveState st;
  read_state(&st);
  ++g_hookFrames;
  const LONG bWas = g_inTick;
  g_inTick = 1;
  frame_read(st, _ReturnAddress());
  g_inTick = bWas;
}

static void WINAPI hk_Sleep(DWORD ms) {
  if (g_cfg.dwSleepCap && ms > g_cfg.dwSleepCap) ms = g_cfg.dwSleepCap;
  g_realSleep(ms);
}

// CheckForOtherInstances (0x466F20) creates this named event and quits when it
// already exists, which it does whenever any other StarCraft is running. An
// unnamed event never exists before, so a run starts next to another harness's
// runs or a game someone is playing, and it never blocks one of those either.
static const char kInstanceEvent[] = "Starcraft Check For Other Instances";
typedef HANDLE(WINAPI *PFN_CREATEEVENTA)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR);
static PFN_CREATEEVENTA g_realCreateEventA;

static HANDLE WINAPI hk_CreateEventA(LPSECURITY_ATTRIBUTES pAttrs, BOOL bManual, BOOL bInitial, LPCSTR pszName) {
  if (pszName && !strcmp(pszName, kInstanceEvent)) {
    HANDLE h = g_realCreateEventA(pAttrs, bManual, bInitial, NULL);
    SetLastError(ERROR_SUCCESS);
    return h;
  }
  return g_realCreateEventA(pAttrs, bManual, bInitial, pszName);
}

#if defined(_WIN32)
// Replaces every import slot in hMod whose current target is one of pOld[].
static int patch_iat(HMODULE hMod, void *const *pOld, int nOld, void *pNew) {
  BYTE *base = reinterpret_cast<BYTE *>(hMod);
  IMAGE_DOS_HEADER *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
  IMAGE_NT_HEADERS *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
  IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress) return 0;
  int n = 0;
  for (IMAGE_IMPORT_DESCRIPTOR *d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress);
       d->Name; ++d) {
    for (void **slot = reinterpret_cast<void **>(base + d->FirstThunk); *slot; ++slot) {
      for (int i = 0; i < nOld; ++i) {
        if (*slot == pOld[i]) {
          DWORD old;
          VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old);
          *slot = pNew;
          VirtualProtect(slot, sizeof(void *), old, &old);
          ++n;
        }
      }
    }
  }
  return n;
}
#endif

static void module_name(HMODULE h, char *out, DWORD cap) {
  char full[MAX_PATH];
  GetModuleFileNameA(h, full, MAX_PATH);
  const char *s = strrchr(full, '\\');
  strncpy(out, s ? s + 1 : full, cap - 1);
  out[cap - 1] = 0;
  _strlwr(out);
}

#if !defined(_WIN32)
// Off Windows there is no import table: Hyperion's GetTickCount, Sleep and CreateEventA ask
// for a hook of their own (oc_posix_hook, port_posix.cpp), which answers with the real one.
static void install_hooks(HMODULE) {
  typedef void *(*PFN_HOOK)(const char *, void *);
  PFN_HOOK pfnHook = reinterpret_cast<PFN_HOOK>(dlsym(RTLD_DEFAULT, "oc_posix_hook"));
  if (!pfnHook) {
    rt_log("X,no_oc_posix_hook");
    return;
  }
  g_realGTC = reinterpret_cast<PFN_GTC>(pfnHook("GetTickCount", NULL));
  g_realSleep = reinterpret_cast<PFN_SLEEP>(pfnHook("Sleep", NULL));
  g_vclock = (LONG)g_realGTC();
  pfnHook("GetTickCount", reinterpret_cast<void *>(hk_GetTickCount));
  if (g_cfg.dwSleepCap) pfnHook("Sleep", reinterpret_cast<void *>(hk_Sleep));
  g_realCreateEventA = reinterpret_cast<PFN_CREATEEVENTA>(
      pfnHook("CreateEventA", reinterpret_cast<void *>(hk_CreateEventA)));
  rt_log("H,hyperion,GetTickCount=1,Sleep=%d,CreateEventA=1", g_cfg.dwSleepCap ? 1 : 0);
}
#else
static void install_hooks(HMODULE hSelf) {
  HMODULE k32 = GetModuleHandleA("kernel32.dll");
  HMODULE kb = GetModuleHandleA("kernelbase.dll");
  g_realGTC = reinterpret_cast<PFN_GTC>(GetProcAddress(k32, "GetTickCount"));
  g_realSleep = reinterpret_cast<PFN_SLEEP>(GetProcAddress(k32, "Sleep"));
  void *gtc[2] = {reinterpret_cast<void *>(g_realGTC), kb ? GetProcAddress(kb, "GetTickCount") : NULL};
  void *slp[2] = {reinterpret_cast<void *>(g_realSleep), kb ? GetProcAddress(kb, "Sleep") : NULL};
  g_realCreateEventA = reinterpret_cast<PFN_CREATEEVENTA>(GetProcAddress(k32, "CreateEventA"));
  void *cev[2] = {reinterpret_cast<void *>(g_realCreateEventA), kb ? GetProcAddress(kb, "CreateEventA") : NULL};
  g_vclock = (LONG)g_realGTC();

  // The exe, and storm.dll on request.
  HMODULE mods[1024];
  DWORD cb = 0;
  HANDLE hProc = GetCurrentProcess();
  typedef BOOL(WINAPI * PFN_EPM)(HANDLE, HMODULE *, DWORD, LPDWORD);
  PFN_EPM epm = reinterpret_cast<PFN_EPM>(GetProcAddress(k32, "K32EnumProcessModules"));
  if (!epm || !epm(hProc, mods, sizeof mods, &cb)) cb = 0;
  for (DWORD i = 0; i < cb / sizeof(HMODULE); ++i) {
    HMODULE h = mods[i];
    if (h == hSelf) continue;
    char name[MAX_PATH];
    module_name(h, name, sizeof name);
    bool isExe = (h == GetModuleHandleA(NULL));
    bool isStorm = !strcmp(name, "storm.dll");
    if (!(isExe || (isStorm && g_cfg.bClockStorm))) continue;
    int nG = patch_iat(h, gtc, 2, reinterpret_cast<void *>(hk_GetTickCount));
    int nS = g_cfg.dwSleepCap ? patch_iat(h, slp, 2, reinterpret_cast<void *>(hk_Sleep)) : 0;
    int nE = isStorm ? 0 : patch_iat(h, cev, 2, reinterpret_cast<void *>(hk_CreateEventA));
    rt_log("H,%s,GetTickCount=%d,Sleep=%d,CreateEventA=%d", name, nG, nS, nE);
  }
}
#endif

// ---------------------------------------------------------------------------
// Faults. A run whose game crashes used to lose the unflushed tail of its log,
// and an oracle run left no record of where it died. The first handler in the
// chain logs the frame, code and addresses of an error-class exception and
// flushes, then passes the fault on unchanged: to the engine's crash reporter,
// then the game's own handler. A fault the game handles itself costs a line.

static volatile LONG g_faults;

#if !defined(_WIN32)
// Off Windows: a fault is a signal. A guarded read (RT_TRY) goes back to its guard; any other
// is logged as on Windows (an access violation's code, the faulting instruction and address,
// the stack's code addresses), and the handler before this one then has it.
static struct sigaction g_aOldActions[4];
static const int kFaultSignals[4] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL};

static bool is_code(uintptr_t v) {
  Dl_info info;
  return v >= 0x10000u && dladdr(reinterpret_cast<void *>(v), &info) && info.dli_sname;
}

static void on_signal(int nSignal, siginfo_t *pInfo, void *pContext) {
  if (t_pGuard && (nSignal == SIGSEGV || nSignal == SIGBUS)) siglongjmp(*t_pGuard, 1);
  int nIndex = 0;
  while (nIndex < 3 && kFaultSignals[nIndex] != nSignal) ++nIndex;
  if (InterlockedIncrement(&g_faults) <= 8) {
    const ucontext_t *uc = static_cast<const ucontext_t *>(pContext);
#if defined(__x86_64__)
    const uintptr_t pc = uc->uc_mcontext.gregs[REG_RIP], csp = uc->uc_mcontext.gregs[REG_RSP],
                    cecx = uc->uc_mcontext.gregs[REG_RCX];
#elif defined(__APPLE__) && defined(__aarch64__)
    const uintptr_t pc = uc->uc_mcontext->__ss.__pc, csp = uc->uc_mcontext->__ss.__sp,
                    cecx = uc->uc_mcontext->__ss.__x[0];
#elif defined(__aarch64__)
    const uintptr_t pc = uc->uc_mcontext.pc, csp = uc->uc_mcontext.sp, cecx = uc->uc_mcontext.regs[0];
#else
    const uintptr_t pc = 0, csp = 0, cecx = 0;
#endif
    char stack[8 * 17 + 1];
    int len = 0;
    stack[0] = 0;
    const uintptr_t *sp = reinterpret_cast<const uintptr_t *>(csp);
    for (int i = 0, n = 0; sp && i < 1024 && n < 8; ++i) {
      if (is_code(sp[i])) {
        len += snprintf(stack + len, sizeof stack - len, n ? "|%08" RT_PX : "%08" RT_PX, sp[i]);
        n++;
      }
    }
    static const DWORD kCodes[4] = {0xC0000005u, 0xC0000005u, 0xC0000094u, 0xC000001Du};
    rt_log("X,%u,%08X,%p,%p,ecx=%08" RT_PX ",stack=%s", rd<DWORD>(VA_FRAME), kCodes[nIndex],
           reinterpret_cast<void *>(pc), pInfo->si_addr, cecx, stack);
    log_flush_try();
  }
  // the handler before this one, or the default, which ends the process
  sigaction(nSignal, &g_aOldActions[nIndex], NULL);
}

static void install_fault_handler() {
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = on_signal;
  sa.sa_flags = SA_SIGINFO | SA_NODEFER;
  sigemptyset(&sa.sa_mask);
  for (int i = 0; i < 4; ++i) sigaction(kFaultSignals[i], &sa, &g_aOldActions[i]);
}
#else
// Whether v points into executable image memory: the exe or a DLL's code.
static bool is_code(uintptr_t v) {
  MEMORY_BASIC_INFORMATION mbi;
  if (v < 0x10000u || !VirtualQuery(reinterpret_cast<void *>(v), &mbi, sizeof mbi)) return false;
  return mbi.Type == MEM_IMAGE &&
         (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}

static LONG CALLBACK on_fault(EXCEPTION_POINTERS *ep) {
  const EXCEPTION_RECORD *er = ep->ExceptionRecord;
  if ((er->ExceptionCode & 0xF0000000u) != 0xC0000000u || InterlockedIncrement(&g_faults) > 8)
    return EXCEPTION_CONTINUE_SEARCH;
  ULONG_PTR addr = er->NumberParameters >= 2 ? er->ExceptionInformation[1] : 0;
  // A sketch of the call stack: the first 8 dwords above ESP that point into
  // code. Most are return addresses; a saved function pointer can pass too. The
  // walk stays below the thread's stack base, so it cannot fault itself.
  const CONTEXT *cx = ep->ContextRecord;
#ifdef RT_WIDE
  const uintptr_t csp = cx->Rsp, cecx = cx->Rcx;
#else
  const uintptr_t csp = cx->Esp, cecx = cx->Ecx;
#endif
  const uintptr_t *sp = reinterpret_cast<const uintptr_t *>(csp);
  const uintptr_t *top = reinterpret_cast<const uintptr_t *>(reinterpret_cast<NT_TIB *>(NtCurrentTeb())->StackBase);
  char stack[8 * 17 + 1];
  int len = 0;
  stack[0] = 0;
  for (int n = 0; sp < top && sp < reinterpret_cast<const uintptr_t *>(csp) + 1024 && n < 8; sp++) {
    if (is_code(*sp)) {
      len += _snprintf(stack + len, sizeof stack - len, n ? "|%08" RT_PX : "%08" RT_PX, *sp);
      n++;
    }
  }
  rt_log("X,%u,%08X,%p,%p,ecx=%08" RT_PX ",stack=%s", rd<DWORD>(VA_FRAME), er->ExceptionCode, er->ExceptionAddress,
         reinterpret_cast<void *>(addr), cecx, stack);
  log_flush_try();
  return EXCEPTION_CONTINUE_SEARCH;
}

static void install_fault_handler() { AddVectoredExceptionHandler(1, on_fault); }
#endif

// ---------------------------------------------------------------------------
// Watchdog: ends a run that stops making progress.

// Where a game thread that has stopped calling GetTickCount is: its EIP and
// the first 12 code addresses on its stack, each as module+offset. Logged once,
// when the thread has made no call for 10 seconds.
#if !defined(_WIN32)
// Off Windows a thread cannot be stopped and read from another; gdb shows where it is.
static void dump_stuck_thread() { rt_log("W,%.1f,stuck,no_context", wall_ms()); }
#else
static void dump_stuck_thread() {
  HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                              g_mainTid);
  if (!hThread) return;
  if (SuspendThread(hThread) == (DWORD)-1) { CloseHandle(hThread); return; }
  CONTEXT cx;
  cx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
  char line[900];
  int len = 0;
  if (GetThreadContext(hThread, &cx)) {
    uintptr_t addrs[13];
    int n = 0;
#ifdef RT_WIDE
    addrs[n++] = cx.Rip;
    const uintptr_t csp = cx.Rsp;
#else
    addrs[n++] = cx.Eip;
    const uintptr_t csp = cx.Esp;
#endif
    RT_TRY
      const uintptr_t *sp = reinterpret_cast<const uintptr_t *>(csp);
      for (int i = 0; i < 4096 && n < 13; ++i)
        if (is_code(sp[i])) addrs[n++] = sp[i];
    RT_EXCEPT
    RT_END
    for (int i = 0; i < n; ++i) {
      HMODULE h = NULL;
      char name[MAX_PATH] = "?";
      if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCSTR>(addrs[i]), &h))
        module_name(h, name, sizeof name);
      len += _snprintf(line + len, sizeof line - len, i ? "|%s+%X" : "%s+%X", name,
                       (unsigned)(addrs[i] - reinterpret_cast<uintptr_t>(h)));
      if (len >= (int)sizeof line - 1) break;
    }
  }
  ResumeThread(hThread);
  CloseHandle(hThread);
  rt_log("W,%.1f,stuck,%s", wall_ms(), len ? line : "no_context");
}
#endif

static DWORD WINAPI watchdog(void *) {
  LONG lastMain = -1;
  int nStill = 0;
  for (;;) {
    Sleep(1000);
    if (g_finished) return 0;
    if (g_callsMain != lastMain) { lastMain = g_callsMain; nStill = 0; }
    else if (++nStill == 10) dump_stuck_thread();
    static int nBeat;
    if (++nBeat % 5 == 0)
      rt_log("D,%.1f,calls=%ld,main=%ld,other_tid=%lu,other_caller=%p,mode=%d,screen=%d", wall_ms(), g_callsAll,
             g_callsMain, g_lastOtherTid, g_lastOtherCaller, (int)rd<WORD>(VA_GAME_MODE), rd<int>(VA_GLUE_SCREEN));
    DWORD idle = (GetTickCount() - (DWORD)g_progressTick) / 1000;
    if (g_inGame && g_cfg.dwStallSecs && idle >= g_cfg.dwStallSecs) {
      rt_log("W,%.1f,stall_in_game,frame=%u", wall_ms(), rd<DWORD>(VA_FRAME));
      finish("stall_in_game");
    } else if (!g_sawGame && g_cfg.dwMenuTimeoutSecs && wall_ms() / 1000 >= g_cfg.dwMenuTimeoutSecs) {
      rt_log("W,%.1f,menu_timeout,mode=%d,screen=%d,step=%d", wall_ms(), g_lastMode, g_lastScreen, g_step);
      finish("menu_timeout");
    }
    log_flush();
  }
}

// ---------------------------------------------------------------------------
// Entry. rt_launcher calls this on a remote thread before the game's main
// thread first runs, passing that thread's id.

extern "C" OC_RT_EXPORT DWORD __stdcall rt_init(void *pMainTid) {
  InitializeCriticalSection(&g_logLock);
  QueryPerformanceFrequency(&g_qpcFreq);
  QueryPerformanceCounter(&g_qpcStart);
  cfg_load();
  g_mainTid = (DWORD)(UINT_PTR)pMainTid;
  g_delta = (uintptr_t)GetModuleHandleA(NULL) - 0x400000u;
  g_hLog = CreateFileA(g_cfg.szOut, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
  if (g_cfg.szSnapDir[0]) CreateDirectoryA(g_cfg.szSnapDir, NULL);
  rt_log("# rt_observer 1; F=frame,rand_state,rand_count,units,h_sem,h_raw,h_stat P=frame,player,minerals,gas,units,hp "
       "Q=frame,player,minerals_gathered,gas_gathered,score_counters[28] "
       "T=frame,wall_ms E=wall_ms,mode,gamemode,screen,playback "
       "Z=frame,slot,type_player,hp,shields,energy,position,order,order_target,flags");
  rt_log("B,exe_base=%08" RT_PX ",delta=%08" RT_PX, 0x400000u + g_delta, g_delta);
  rt_log("C,clock=%s,step=%u,updates=%u,keep_active=%d,sleep_cap=%u,max_frames=%u,steps=%d",
       g_cfg.bVirtualClock ? "virtual" : "real", g_cfg.dwClockStep, g_cfg.dwUpdates, g_cfg.bKeepActive,
       g_cfg.dwSleepCap, g_cfg.dwMaxFrames, g_cfg.nSteps);
  // An engine has loaded 1.17's data before this (hyperion.cpp).
  observe_find();
  if (g_cfg.bPinSeed) pin_seed();
  HMODULE hSelf = NULL;
#if defined(_WIN32)
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCSTR>(&rt_init), &hSelf);
#endif
  install_hooks(hSelf);
  install_fault_handler();
  note_progress();
  CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
  log_flush();
  return 1;
}

// A game that exits through ExitProcess still gets its last frames written.
#if defined(_WIN32)
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_DETACH && g_hLog != INVALID_HANDLE_VALUE) log_flush_try();
  return TRUE;
}
#else
__attribute__((destructor)) static void rt_fini() {
  if (g_hLog != INVALID_HANDLE_VALUE) log_flush_try();
}
#endif
