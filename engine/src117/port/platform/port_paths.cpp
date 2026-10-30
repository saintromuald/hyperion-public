// port_paths.cpp -- where Hyperion keeps the player's data (profiles, replays, saves, maps,
// logs, screenshots), the extra map folders, and the split of Hyperion's command line options
// out of 1.17's (docs/modules/platform.md, "Where the player's files live").
//
// Hyperion keeps its data apart from the player's other copies of the game (PRD 2026-09-27),
// in a folder Data\ it makes in the folder Hyperion.exe runs from, a folder of its own
// anywhere, with the game's files found where they are installed (PRD 2026-10-21); or in
// --data's folder, which the replay harness gives its game folders. Before 0.6.2's last build
// the folder was Hyperion\, and what one holds moves into Data\ at the start. --maps adds up to
// eight folders of maps, which Hyperion only reads, and StarCraft from Battle.net's Maps folder
// is one more when its files are the game's. The options are taken out of the line 1.17's
// parse_args (init.cpp) reads, since it matches each word as a prefix of its switches, so a
// word of a folder's name could set one: "c" is a prefix of "crash". Each takes --name=value
// too.
#include <windows.h>
#include <string.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif
#include <string>
#include <vector>
#include "port_paths.h"
#include "port_posix.h"  // PortNativePath
#include "../data/port_remastered.h"  // PortGameSource
#include "../front/port_ui.h"  // PortCascFolder
#include "../tools/port_canvas.h"  // PORT_CANVAS_TILESET

static char g_szDataFolder[MAX_PATH];
static PortOptions g_options;
static char g_szGameCommandLine[0x8000];

static char PathsLower(char c)
{
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// Whether psz starts with pszPrefix, ignoring ASCII case.
static bool PathsHasPrefix(const char *psz, const char *pszPrefix)
{
  for (; *pszPrefix != '\0'; ++psz, ++pszPrefix)
    if (PathsLower(*psz) != PathsLower(*pszPrefix))
      return false;
  return true;
}

// The next word of a command line from p: words are split at spaces and tabs
// outside quotes, and the quotes are dropped from the word's text. *ppStart is
// where the word starts (p itself when only spaces are left); the return is
// where it ends.
static const char *PathsNextWord(const char *p, const char **ppStart, char *pszWord,
                                 size_t cchWord)
{
  size_t n = 0;
  bool bQuoted = false;

  while (*p == ' ' || *p == '\t')
    ++p;
  *ppStart = p;
  while (*p != '\0' && (bQuoted || (*p != ' ' && *p != '\t'))) {
    if (*p == '"')
      bQuoted = !bQuoted;
    else if (n + 1 < cchWord)
      pszWord[n++] = *p;
    ++p;
  }
  pszWord[n] = '\0';
  return p;
}

// Whether pszWord is the option pszName: "--name=value", or "--name" with the
// value in the next word, which *ppEnd then moves past. The value goes to
// pszValue, MAX_PATH bytes.
static bool PathsOption(const char *pszWord, const char *pszName, const char **ppEnd,
                        char *pszValue)
{
  int cchName = lstrlenA(pszName);
  const char *pStart;

  if (!PathsHasPrefix(pszWord, pszName))
    return false;
  if (pszWord[cchName] == '=') {
    lstrcpynA(pszValue, pszWord + cchName + 1, MAX_PATH);
    return true;
  }
  if (pszWord[cchName] != '\0')
    return false;
  *ppEnd = PathsNextWord(*ppEnd, &pStart, pszValue, MAX_PATH);
  return true;
}

// Splits pszLine into the command line 1.17 is to see (pszGame, cchGame bytes)
// and Hyperion's options (pOptions, each empty when not given). The program's
// name, the first word, is never an option.
void PortPathsSplitCommandLine(const char *pszLine, char *pszGame, size_t cchGame,
                               PortOptions *pOptions)
{
  char szWord[MAX_PATH];
  char szMaps[MAX_PATH];
  const char *pStart;
  const char *pEnd;
  const char *p;
  size_t n = 0;

  ZeroMemory(pOptions, sizeof *pOptions);
  p = PathsNextWord(pszLine, &pStart, szWord, sizeof szWord);
  for (;;) {
    // Copy what came before, the separating spaces included.
    for (; pszLine < p && n + 1 < cchGame; ++pszLine)
      pszGame[n++] = *pszLine;
    pszLine = p;
    pEnd = PathsNextWord(p, &pStart, szWord, sizeof szWord);
    if (pStart == pEnd)
      break;
    if (PathsOption(szWord, "--mod-check", &pEnd, pOptions->szModCheck)) {
      // and the report's file, when a word that is no option follows; the mod's data is the
      // data that loads
      char szNext[MAX_PATH];
      const char *pNextStart;
      const char *pAfter = PathsNextWord(pEnd, &pNextStart, szNext, sizeof szNext);
      if (pNextStart != pAfter && szNext[0] != '-') {
        lstrcpynA(pOptions->szModCheckReport, szNext, MAX_PATH);
        pEnd = pAfter;
      }
      lstrcpynA(pOptions->szMod, pOptions->szModCheck, MAX_PATH);
      pszLine = pEnd;
    } else if (PathsOption(szWord, "--data", &pEnd, pOptions->szData) ||
        PathsOption(szWord, "--install", &pEnd, pOptions->szInstall) ||
        PathsOption(szWord, "--install-folder", &pEnd, pOptions->szInstallFolder) ||
        PathsOption(szWord, "--replay", &pEnd, pOptions->szReplay) ||
        PathsOption(szWord, "--map", &pEnd, pOptions->szMap) ||
        PathsOption(szWord, "--load", &pEnd, pOptions->szLoad) ||
        PathsOption(szWord, "--race", &pEnd, pOptions->szRace) ||
        PathsOption(szWord, "--computers", &pEnd, pOptions->szComputers) ||
        PathsOption(szWord, "--view", &pEnd, pOptions->szView) ||
        PathsOption(szWord, "--seed", &pEnd, pOptions->szSeed) ||
        PathsOption(szWord, "--record", &pEnd, pOptions->szRecord) ||
        PathsOption(szWord, "--record-orders", &pEnd, pOptions->szRecordOrders) ||
        PathsOption(szWord, "--colour", &pEnd, pOptions->szColour) ||
        PathsOption(szWord, "--teams", &pEnd, pOptions->szTeams) ||
        PathsOption(szWord, "--starts", &pEnd, pOptions->szStarts) ||
        PathsOption(szWord, "--ai-levels", &pEnd, pOptions->szAiLevels) ||
        PathsOption(szWord, "--handicaps", &pEnd, pOptions->szHandicaps) ||
        PathsOption(szWord, "--mutators", &pEnd, pOptions->szMutators) ||
        PathsOption(szWord, "--rules", &pEnd, pOptions->szRules) ||
        PathsOption(szWord, "--mod", &pEnd, pOptions->szMod) ||
        PathsOption(szWord, "--computer-race", &pEnd, pOptions->szComputerRace) ||
        PathsOption(szWord, "--players", &pEnd, pOptions->szPlayers) ||
        PathsOption(szWord, "--bench-frames", &pEnd, pOptions->szBenchFrames) ||
        PathsOption(szWord, "--bench-speed", &pEnd, pOptions->szBenchSpeed) ||
        PathsOption(szWord, "--bench-replay", &pEnd, pOptions->szBenchReplay) ||
        PathsOption(szWord, "--bench-races", &pEnd, pOptions->szBenchRaces) ||
        PathsOption(szWord, "--bench-shown", &pEnd, pOptions->szBenchShown) ||
        PathsOption(szWord, "--overlay", &pEnd, pOptions->szOverlay) ||
        PathsOption(szWord, "--overlay-at", &pEnd, pOptions->szOverlayAt) ||
        PathsOption(szWord, "--overlay-size", &pEnd, pOptions->szOverlaySize) ||
        PathsOption(szWord, "--overlay-density", &pEnd, pOptions->szOverlayDensity) ||
        PathsOption(szWord, "--overlay-show", &pEnd, pOptions->szOverlayShow) ||
        PathsOption(szWord, "--overlay-figures", &pEnd, pOptions->szOverlayFigures) ||
        PathsOption(szWord, "--overlay-see", &pEnd, pOptions->szOverlaySee) ||
        PathsOption(szWord, "--bench-frames-shown", &pEnd, pOptions->szBenchFramesShown) ||
        PathsOption(szWord, "--bench-picture", &pEnd, pOptions->szBenchPicture) ||
        PathsOption(szWord, "--bench-against", &pEnd, pOptions->szBenchAgainst) ||
        PathsOption(szWord, "--bench-type", &pEnd, pOptions->szBenchType) ||
        PathsOption(szWord, "--bench-army", &pEnd, pOptions->szBenchArmy) ||
        PathsOption(szWord, "--bench-pools", &pEnd, pOptions->szBenchPools) ||
        PathsOption(szWord, "--room-key", &pEnd, pOptions->szRoomKey) ||
        PathsOption(szWord, "--room-name", &pEnd, pOptions->szRoomName) ||
        PathsOption(szWord, "--room-password", &pEnd, pOptions->szRoomPassword) ||
        PathsOption(szWord, "--join", &pEnd, pOptions->szJoin) ||
        PathsOption(szWord, "--lan-name", &pEnd, pOptions->szLanName) ||
        PathsOption(szWord, "--lan-copy", &pEnd, pOptions->szLanCopy) ||
        PathsOption(szWord, "--lan-stream", &pEnd, pOptions->szLanStream) ||
        PathsOption(szWord, "--lan-tab", &pEnd, pOptions->szLanTab) ||
        PathsOption(szWord, "--lab-worker", &pEnd, pOptions->szLabWorker) ||
        PathsOption(szWord, "--lab-game", &pEnd, pOptions->szLabGame) ||
        PathsOption(szWord, "--lab-out", &pEnd, pOptions->szLabOut) ||
        PathsOption(szWord, "--after", &pEnd, pOptions->szAfter)) {
      pszLine = pEnd;
    } else if (lstrcmpiA(szWord, "--canvas") == 0) {
      lstrcpynA(pOptions->szCanvas, PORT_CANVAS_TILESET, MAX_PATH);
      pszLine = pEnd;
    } else if (PathsHasPrefix(szWord, "--canvas=")) {
      lstrcpynA(pOptions->szCanvas, szWord + 9, MAX_PATH);
      pszLine = pEnd;
    } else if (lstrcmpiA(szWord, "--bench") == 0) {
      pOptions->bBench = true;
      pszLine = pEnd;
    } else if (lstrcmpiA(szWord, "--trainer") == 0) {
      pOptions->bTrainer = true;
      pszLine = pEnd;
    } else if (lstrcmpiA(szWord, "--bench-frames-csv") == 0) {
      pOptions->bBenchFrames = true;
      pszLine = pEnd;
    } else if (lstrcmpiA(szWord, "--room") == 0) {
      pOptions->bRoom = true;
      pszLine = pEnd;
    } else if (lstrcmpiA(szWord, "--lan-ready") == 0) {
      pOptions->bLanReady = true;
      pszLine = pEnd;
    } else if (lstrcmpiA(szWord, "--lan-watch") == 0) {
      pOptions->bLanWatch = true;
      pszLine = pEnd;
    } else if (lstrcmpiA(szWord, "--ums") == 0) {
      pOptions->bUms = true;
      pszLine = pEnd;
    } else if (lstrcmpiA(szWord, "--control") == 0) {
      lstrcpynA(pOptions->szControl, "0", MAX_PATH);
      pszLine = pEnd;
    } else if (PathsHasPrefix(szWord, "--control=")) {
      lstrcpynA(pOptions->szControl, szWord + 10, MAX_PATH);
      pszLine = pEnd;
    } else if (PathsOption(szWord, "--maps", &pEnd, szMaps)) {
      // Past the eighth, a folder is dropped rather than left to parse_args.
      if (pOptions->nMaps < PORT_MAP_FOLDERS)
        lstrcpynA(pOptions->aszMaps[pOptions->nMaps++], szMaps, MAX_PATH);
      pszLine = pEnd;
    }
    p = pEnd;
  }
  pszGame[n] = '\0';
}

// Creates each folder along pszPath that does not exist yet.
static void PathsCreateFolders(char *pszPath)
{
  for (char *p = pszPath; *p != '\0'; ++p) {
    if ((*p == '\\' || *p == '/') && p > pszPath && p[-1] != ':') {
      char c = *p;
      *p = '\0';
      CreateDirectoryA(pszPath, NULL);
      *p = c;
    }
  }
}

// A file named on the command line, as a full path against the folder Hyperion
// was started in, which the game may leave later.
static void PathsFull(char *pszPath)
{
  char szFull[MAX_PATH];
  DWORD n;

  if (pszPath[0] == '\0')
    return;
  n = GetFullPathNameA(pszPath, MAX_PATH, szFull, NULL);
  if (n != 0 && n < MAX_PATH)
    lstrcpynA(pszPath, szFull, MAX_PATH);
}

// The folder beside the program that holds Hyperion's data and what its package brings: Data\,
// or Hyperion\ when the program is in StarCraft from Battle.net's own folder (a .build.info
// beside it), whose Data\ is Blizzard's storage and is left alone.
static const char *PathsHomeName(void)
{
  struct Name {
    const char *psz = "Data\\";
    Name()
    {
      std::string info = std::string(PortProgramFolder()) + ".build.info";
      if (GetFileAttributesA(info.c_str()) != INVALID_FILE_ATTRIBUTES)
        psz = "Hyperion\\";
    }
  };
  static const Name s_name;
  return s_name.psz;
}

// Whether two files hold the same bytes.
static bool PathsSameFile(const char *pszA, const char *pszB)
{
  HANDLE hA = CreateFileA(pszA, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  HANDLE hB = CreateFileA(pszB, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  bool bSame = hA != INVALID_HANDLE_VALUE && hB != INVALID_HANDLE_VALUE &&
               GetFileSize(hA, NULL) == GetFileSize(hB, NULL);
  std::vector<char> a(1 << 16), b(1 << 16);
  while (bSame) {
    DWORD nA = 0, nB = 0;
    if (!ReadFile(hA, a.data(), static_cast<DWORD>(a.size()), &nA, NULL) ||
        !ReadFile(hB, b.data(), static_cast<DWORD>(b.size()), &nB, NULL) || nA != nB) {
      bSame = false;
    } else if (nA == 0) {
      break;
    } else {
      bSame = memcmp(a.data(), b.data(), nA) == 0;
    }
  }
  if (hA != INVALID_HANDLE_VALUE)
    CloseHandle(hA);
  if (hB != INVALID_HANDLE_VALUE)
    CloseHandle(hB);
  return bSame;
}

// Moves what the folder pszFrom holds into pszTo, both ending in a backslash: a file or folder
// pszTo has nothing of that name for goes whole, a folder both have is merged the same way, and
// of a file both have the old copy goes when it is the same, else stays where it was, the
// package's newer copy kept. pszFrom goes once empty. pszLeave names an entry of pszFrom's left
// where it is.
static void PathsMoveInto(const char *pszFrom, const char *pszTo, const char *pszLeave = NULL)
{
  std::vector<std::string> names;
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA((std::string(pszFrom) + "*").c_str(), &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (lstrcmpA(fd.cFileName, ".") != 0 && lstrcmpA(fd.cFileName, "..") != 0)
        names.push_back(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  for (const std::string &name : names) {
    if (pszLeave != NULL && lstrcmpiA(name.c_str(), pszLeave) == 0)
      continue;
    const std::string from = pszFrom + name, to = pszTo + name;
    const DWORD dwTo = GetFileAttributesA(to.c_str());
    const DWORD dwFrom = GetFileAttributesA(from.c_str());
    if (dwTo == INVALID_FILE_ATTRIBUTES)
      MoveFileA(from.c_str(), to.c_str());
    else if ((dwTo & FILE_ATTRIBUTE_DIRECTORY) && (dwFrom & FILE_ATTRIBUTE_DIRECTORY))
      PathsMoveInto((from + "\\").c_str(), (to + "\\").c_str());
    else if (!(dwTo & FILE_ATTRIBUTE_DIRECTORY) && !(dwFrom & FILE_ATTRIBUTE_DIRECTORY) &&
             PathsSameFile(from.c_str(), to.c_str()))
      DeleteFileA(from.c_str());
  }
  std::string folder = pszFrom;
  folder.pop_back();
  RemoveDirectoryA(folder.c_str());
}

// The secrets a Hyperion that starts another passes in the environment, set for that start alone,
// not on the command line, which any program on the machine can read: the room's key and password, and
// the token of a copy, a seat or a lab worker, which its option names as "env" ("--lan-copy 3:env").
// Each is taken out of the environment once read, so what this one starts does not have it.
static void PathsSecrets(PortOptions *o)
{
  char sz[260];
  const auto Take = [&sz](const char *pszVar) {
    const DWORD n = GetEnvironmentVariableA(pszVar, sz, sizeof sz);
    SetEnvironmentVariableA(pszVar, NULL);
    return n > 0 && n < sizeof sz;
  };
  if (Take("HYPERION_ROOM_KEY") && o->szRoomKey[0] == '\0')
    lstrcpynA(o->szRoomKey, sz, sizeof o->szRoomKey);
  if (Take("HYPERION_ROOM_PASSWORD") && o->szRoomPassword[0] == '\0')
    lstrcpynA(o->szRoomPassword, sz, sizeof o->szRoomPassword);
  if (!Take("HYPERION_CHILD_TOKEN"))
    return;
  for (char *psz : {o->szLanCopy, o->szLanStream, o->szLabWorker}) {
    char *pAt = strstr(psz, ":env");
    if (pAt == NULL || (pAt[4] != '\0' && pAt[4] != ':'))
      continue;
    const std::string rest = pAt + 4;
    const std::string full = std::string(psz, pAt + 1) + sz + rest;
    lstrcpynA(psz, full.c_str(), 260);
  }
}

static bool PathsInitOnce(void)
{
  DWORD n;

  PortPathsSplitCommandLine(GetCommandLineA(), g_szGameCommandLine, sizeof g_szGameCommandLine,
                            &g_options);
  PathsSecrets(&g_options);
  // a benchmark run played again without --replay plays the run's own replay
  PathsFull(g_options.szBenchReplay);
  if (g_options.szBenchReplay[0] != '\0' && g_options.szReplay[0] == '\0') {
    lstrcpynA(g_options.szReplay, g_options.szBenchReplay, MAX_PATH);
    if (lstrlenA(g_options.szReplay) < MAX_PATH - 9)
      lstrcatA(g_options.szReplay, "\\run.rep");
  }
  PathsFull(g_options.szReplay);
  PathsFull(g_options.szMap);
  PathsFull(g_options.szLoad);
  PathsFull(g_options.szInstallFolder);
  for (int i = 0; i < g_options.nMaps; ++i) {
    char *pszFolder = g_options.aszMaps[i];

    PathsFull(pszFolder);
    n = lstrlenA(pszFolder);
    if (n > 0 && n < MAX_PATH - 1 && pszFolder[n - 1] != '\\' && pszFolder[n - 1] != '/')
      lstrcatA(pszFolder, "\\");
  }
  n = 0;
  if (g_options.szData[0] != '\0')
    n = GetFullPathNameA(g_options.szData, MAX_PATH, g_szDataFolder, NULL);
  bool bHome = false;
  if (n == 0 || n >= MAX_PATH - 1) {
    // Data\ in the program's folder (PathsHomeName)
    lstrcpynA(g_szDataFolder, PortProgramFolder(), MAX_PATH - 10);
    lstrcatA(g_szDataFolder, PathsHomeName());
    n = lstrlenA(g_szDataFolder);
    bHome = true;
  }
  if (n > 0 && g_szDataFolder[n - 1] != '\\' && g_szDataFolder[n - 1] != '/') {
    g_szDataFolder[n] = '\\';
    g_szDataFolder[n + 1] = '\0';
  }
  PathsCreateFolders(g_szDataFolder);
  // an earlier version's Hyperion\ beside the program moves into Data\ (platform.md)
  if (bHome && lstrcmpA(PathsHomeName(), "Data\\") == 0 &&
      lstrlenA(PortProgramFolder()) < MAX_PATH - 10) {
    char szOld[MAX_PATH];
    lstrcpyA(szOld, PortProgramFolder());
    lstrcatA(szOld, "Hyperion\\");
    const DWORD dwOld = GetFileAttributesA(szOld);
    if (dwOld != INVALID_FILE_ATTRIBUTES && (dwOld & FILE_ATTRIBUTE_DIRECTORY)) {
      // its docs\ was the package's, which is beside the program now: what differs stays
      const std::string docs = std::string(PortProgramFolder()) + "docs\\";
      const DWORD dwDocs = GetFileAttributesA(docs.c_str());
      const bool bDocs = dwDocs != INVALID_FILE_ATTRIBUTES && (dwDocs & FILE_ATTRIBUTE_DIRECTORY);
      if (bDocs && GetFileAttributesA((std::string(szOld) + "docs").c_str()) != INVALID_FILE_ATTRIBUTES)
        PathsMoveInto((std::string(szOld) + "docs\\").c_str(), docs.c_str());
      PathsMoveInto(szOld, g_szDataFolder, bDocs ? "docs" : NULL);
    }
  }
  return true;
}

// Every call below goes through this, and the first of them can come from any thread (the
// logs are written from the LAN's and the live channel's), so the first caller does the
// work and the rest wait on the guard a local static has.
static void PathsInit(void)
{
  static const bool s_bDone = PathsInitOnce();

  (void)s_bDone;
}

#if defined(_WIN32)
const char *PortProgramFolder(void)
{
  struct Folder {
    char sz[MAX_PATH];
    Folder()
    {
      const DWORD n = GetModuleFileNameA(NULL, sz, MAX_PATH);
      sz[n < MAX_PATH ? n : 0] = '\0';
      char *pszEnd = sz;
      for (char *p = sz; *p != '\0'; ++p)
        if (*p == '\\' || *p == '/')
          pszEnd = p + 1;
      *pszEnd = '\0';
    }
  };
  static const Folder s_folder;
  return s_folder.sz;
}
#endif

const char *PortDataFolder(void)
{
  PathsInit();
  return g_szDataFolder;
}

const char *PortPackagedFile(const char *pszRel)
{
  thread_local char s_szPath[MAX_PATH];
  const char *const pszProgram = PortProgramFolder();

  if (lstrlenA(pszProgram) + lstrlenA(PathsHomeName()) + lstrlenA(pszRel) >= MAX_PATH)
    return pszRel;
  lstrcpyA(s_szPath, pszProgram);
  lstrcatA(s_szPath, PathsHomeName());
  lstrcatA(s_szPath, pszRel);
  if (GetFileAttributesA(s_szPath) != INVALID_FILE_ATTRIBUTES)
    return s_szPath;
  lstrcpyA(s_szPath, pszProgram);
  lstrcatA(s_szPath, pszRel);
  return s_szPath;
}

bool PortCdOptional(void)
{
#if defined(__EMSCRIPTEN__)
  return true;
#else
  return PortGameSource() == PORT_SOURCE_REMASTERED;
#endif
}

const char *PortExplorer(void)
{
#if defined(_WIN32)
  static char s_sz[MAX_PATH + 16];
  if (s_sz[0] == '\0') {
    const UINT n = GetWindowsDirectoryA(s_sz, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
      strcpy(s_sz, "C:\\Windows");
    strcat(s_sz, "\\explorer.exe");
  }
  return s_sz;
#else
  return "explorer.exe";
#endif
}

bool PortWritePrivateFile(const char *pszPath, const char *pszText)
{
  const std::string text = pszText;
#if defined(_WIN32)
  const HANDLE h = CreateFileA(pszPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE)
    return false;
  DWORD cb = 0;
  const bool bOk = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &cb, NULL) && cb == text.size();
  CloseHandle(h);
  return bOk;
#else
  const std::string native = PortNativePath(pszPath);
  unlink(native.c_str());  // a file there before keeps its own permissions otherwise
  const int fd = open(native.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    return false;
  const bool bOk = write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
  close(fd);
  return bOk;
#endif
}

const char *PortLogFile(const char *pszName)
{
  thread_local char s_szPath[MAX_PATH];

  PathsInit();
  // --mod-check's in a folder of their own, so a Hyperion that is running keeps its own
  const char *pszSub = g_options.szModCheck[0] != '\0' ? "mod-check\\" : "";
  if (lstrlenA(g_szDataFolder) + lstrlenA(pszSub) + lstrlenA(pszName) + 1 > MAX_PATH)
    return pszName;
  lstrcpyA(s_szPath, g_szDataFolder);
  lstrcatA(s_szPath, pszSub);
  if (pszSub[0] != '\0')
    CreateDirectoryA(s_szPath, NULL);
  lstrcatA(s_szPath, pszName);
  return s_szPath;
}

const PortOptions *PortCommandOptions(void)
{
  PathsInit();
  return &g_options;
}

char *PortGameCommandLine(void)
{
  PathsInit();
  return g_szGameCommandLine;
}

// The map folders: --maps's, then the Maps folder of StarCraft from Battle.net when its files are
// the game's. 1.17's own two places, the data folder and the game's folder, which is then
// Hyperion's own, do not reach it; a classic StarCraft folder elsewhere is the game's folder and
// is one of them already. Decided on the first call, after the start has chosen the game's files.
static char g_aszMapFolders[PORT_MAP_FOLDERS][MAX_PATH];
static int g_nMapFolders;

static bool PathsMapFoldersOnce(void)
{
  PathsInit();
  for (int i = 0; i < g_options.nMaps; ++i)
    lstrcpynA(g_aszMapFolders[g_nMapFolders++], g_options.aszMaps[i], MAX_PATH);
  if (g_nMapFolders < PORT_MAP_FOLDERS && PortGameSource() == PORT_SOURCE_REMASTERED) {
    const std::string maps = std::string(PortCascFolder()) + "\\Maps\\";
    const DWORD dw = GetFileAttributesA(maps.c_str());
    if (maps.size() > 7 && maps.size() < MAX_PATH && dw != INVALID_FILE_ATTRIBUTES &&
        (dw & FILE_ATTRIBUTE_DIRECTORY))
      lstrcpyA(g_aszMapFolders[g_nMapFolders++], maps.c_str());
  }
  return true;
}

int PortMapFolderCount(void)
{
  static const bool s_bDone = PathsMapFoldersOnce();

  (void)s_bDone;
  return g_nMapFolders;
}

// What follows the first folder of pszName when that folder is "maps", else
// NULL: "" for "maps", "BroodWar\x.scx" for "maps\BroodWar\x.scx".
static const char *PathsUnderMaps(const char *pszName)
{
  if (!PathsHasPrefix(pszName, "maps"))
    return NULL;
  if (pszName[4] == '\0')
    return pszName + 4;
  if (pszName[4] == '\\' || pszName[4] == '/')
    return pszName + 5;
  return NULL;
}

int PortMapFolderPath(int i, const char *pszName, char *pszPath, int cchPath)
{
  const char *pszRest = PathsUnderMaps(pszName);
  const char *pszFolder;

  if (i < 0 || i >= PortMapFolderCount() || pszRest == NULL)
    return FALSE;
  pszFolder = g_aszMapFolders[i];
  if (lstrlenA(pszFolder) + lstrlenA(pszRest) >= cchPath)
    return FALSE;
  lstrcpyA(pszPath, pszFolder);
  lstrcatA(pszPath, pszRest);
  return TRUE;
}

int PortMapFindInFolders(const char *pszName, char *pszPath, int cchPath)
{
  char szPath[MAX_PATH];

  for (int i = 0; i < PortMapFolderCount(); ++i) {
    DWORD dwAttributes;

    if (!PortMapFolderPath(i, pszName, szPath, sizeof szPath))
      continue;
    dwAttributes = GetFileAttributesA(szPath);
    if (dwAttributes != INVALID_FILE_ATTRIBUTES && !(dwAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        lstrlenA(szPath) < cchPath) {
      lstrcpyA(pszPath, szPath);
      return TRUE;
    }
  }
  return FALSE;
}

int PortMapRelativePath(char *pszPath, int cchPath)
{
  for (int i = 0; i < PortMapFolderCount(); ++i) {
    const char *pszFolder = g_aszMapFolders[i];
    int cchRest = lstrlenA(pszPath) - lstrlenA(pszFolder);
    char szName[MAX_PATH];

    // "maps\" and the rest, which has to fit where the path was.
    if (!PathsHasPrefix(pszPath, pszFolder) || cchRest + 5 >= cchPath ||
        cchRest + 5 >= (int)sizeof szName)
      continue;
    lstrcpyA(szName, "maps\\");
    lstrcatA(szName, pszPath + lstrlenA(pszFolder));
    lstrcpyA(pszPath, szName);
    return TRUE;
  }
  return FALSE;
}
