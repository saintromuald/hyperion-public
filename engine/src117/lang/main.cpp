// main.cpp -- the program's entry, the install-path set-up and the top-level
// game loop, 0x466F20 through 0x467E60. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/main.h"

#include "../h/Storm.h"
#include "../h/SystemSurvey.h"
#include "../h/statwrite.h"
#include "../h/CImage.h"
#include "../h/CodeSign.h"
#include "../h/credits.h"
#include "../h/cursor.h"
#include "../h/dlg_dlg.h"
#include "../h/event_Win.h"
#include "../h/game.h"
#include "../h/gameloop.h"
#include "../h/gametype.h"
#include "../h/gds_appcrit.h"
#include "../h/gds_appfat.h"
#include "../h/gds_appexi.h"
#include "../h/gluGameMode.h"
#include "../h/gluMain.h"
#include "../h/glues.h"
#include "../h/net_cmds.h"
#include "../h/net_rand.h"
#include "../h/replay.h"
#include "../h/saveload.h"
#include "../h/grid.h"
#include "../h/init.h"
#include "../h/spd_dlg.h"
#include "../h/title.h"
#ifdef OC_PAYLOAD
#include "../port/tools/port_bench.h"  // Hyperion: PortBenchAfterGame
#include "../port/gameplay/port_watchgame.h"  // Hyperion: PortWatchAfterGame
#include "../port/replays/port_replayseek.h"  // Hyperion: PortReplaySeekAfterGame
#include "../port/lan/port_instance.h"  // Hyperion: PortCheckForOtherInstances
#include "../port/lan/port_lanlobby.h"  // Hyperion: PortLanAfterGame
#include "../port/platform/port_paths.h"  // Hyperion: PortDataFolder, PortOptions, PortCommandOptions
#include "../port/replays/port_record.h"  // Hyperion: PortRecordAfterGame
#include "../port/lab/port_labworker.h"  // Hyperion: PortLabWorker
#include "../port/gameplay/port_session.h"  // Hyperion: SessionAfterGame
#include "../port/platform/port_standalone.h"  // Hyperion: PortStartUp
#include "../port/render/port_view.h"  // Hyperion: PortViewSet, PortViewGameWidth, g_portView
#include "../port/render/port_window.h"  // Hyperion: PortWindowCreated
#include "../port/platform/port_winmsg.h"  // Hyperion: the window and its messages
#include "../port/data/port_moddata.h"  // Hyperion: ModDataAfterGame
#include "../port/gameplay/port_score.h"  // Hyperion: PortScoreGameEnd
#include "../port/tools/port_startup.h"  // Hyperion: PortStartupStep, PortStartupWait, PortStartupReady
#include "../port/tools/port_crashnotes.h"  // Hyperion: PortCrashNotesGameEnd
#endif
#include "../h/TrigAct.h"
#include "../h/uiSingle.h"

#include "../gen/globals117.h"

#include <malloc.h>
#include <shlobj.h>

// SetSecurityInfo is reached through GetProcAddress, so InitACL needs its own
// spelling: seven __stdcall words, the object type and the security-information
// mask as plain integers.
typedef DWORD (__stdcall *PFN_SETSECURITYINFO)(HANDLE hHandle, int nObjectType,
                                               DWORD dwSecurityInfo,
                                               PSID pSidOwner, PSID pSidGroup,
                                               ACL *pDacl, ACL *pSacl);

// Storm imports Storm.h does not carry, each spelled as the TU that already
// uses it spells it; the trailing numbers are their ordinals.
extern "C" {
char * __stdcall SStrChrF(const char *pszStr, char chFind);                // 571
int __cdecl SStrPrintf(char *pszDest, size_t nDestSize, const char *pszFormat,
                       ...);                                              // 578
// The naming sheet calls ordinal 587 SStrStrI; the ledger names it SStrStr.
char * __stdcall SStrStr(const char *pszStr, const char *pszFind);         // 587
int __stdcall SFileSetAppDataDir(const char *pszPath);                     // 297
// Ordinal 147. Nullary; main_game_run reads the full EAX.
int __stdcall SNetGetCurrentProviderID();                                  // 147
}

// The CRT entry points InitLocale reaches, declared rather than taken from
// <stdio.h>; fclose and fopen are spelled as gds_file.cpp spells them.
// Hyperion: with MSVC only; elsewhere the C library's, which compat/windows.h
// gives forms taking void *.
#if defined(_MSC_VER)
extern "C" {
int    __cdecl fclose(void *pFile);
void * __cdecl fopen(const char *pszName, const char *pszMode);
int    __cdecl fputc(int ch, void *pFile);
}
#endif

// Hyperion: port_front.h's and port_modcheck.h's, which bring <stdio.h> against the
// declarations above
bool PortFrontCinematicWatched(void);
void PortModCheckRun(void);

// The Storm import RunGame installs the Authenticode check through. The ledger
// has its decorated name, which this declaration mangles to.
void __fastcall SNetSetCodeSignVerifcationFunction(
    bool (__fastcall *pfnVerify)(const char *pszPath));

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x6728D0 gGameHeader, declared in each TU that reaches it (gametype.h says
// why), spelled as gametype.cpp and uiMulti.cpp spell it.
#ifndef OC_OWN_gGameHeader
#define OC_OWN_gGameHeader
#ifdef OC_DLL_MODE
#define gGameHeader (*reinterpret_cast<GameHeader *>(OC_VA(0x6728D0u)))
#else
extern GameHeader gGameHeader;
#endif
#endif

// GameData -- Txt2GameData's output record, at gGameHeader+0x1C, declared as
// uiMulti.cpp declares it. main_game_run fills six of its fields from the
// loaded map and from GameHeader.
#pragma pack(push, 1)
struct GameData {
  /*0x00*/ DWORD dwSeed;
  /*0x04*/ WORD  wUnk04;
  /*0x06*/ WORD  wUnk06;
  /*0x08*/ BYTE  bUnk08;
  /*0x09*/ BYTE  bUnk09;
  /*0x0A*/ BYTE  bUnk0A;
  /*0x0B*/ BYTE  bMapAuthentication;
  /*0x0C*/ BYTE  bUnk0C;
  /*0x0D*/ BYTE  bUnk0D;
  /*0x0E*/ WORD  wUnk0E;
  /*0x10*/ DWORD dwUnk10;
  /*0x14*/ WORD  wUnk14;
  /*0x16*/ BYTE  bPlaybackKind;
  /*0x17*/ BYTE  _underived_17;
  /*0x18*/ char  szCreatorName[0x19];
  /*0x31*/ char  szMapName[0x20];
};
#pragma pack(pop)

static_assert(sizeof(GameData) == 0x51, "five movdqu (80) + one byte store");
static_assert(offsetof(GameData, dwSeed) == 0x00, "0x6728EC");
static_assert(offsetof(GameData, wUnk04) == 0x04, "0x6728F0");
static_assert(offsetof(GameData, wUnk06) == 0x06, "0x6728F2");
static_assert(offsetof(GameData, bUnk08) == 0x08, "Txt2GameData [edi+8]");
static_assert(offsetof(GameData, bUnk09) == 0x09, "Txt2GameData [edi+9]");
static_assert(offsetof(GameData, bUnk0A) == 0x0A, "Txt2GameData [edi+0Ah]");
static_assert(offsetof(GameData, bMapAuthentication) == 0x0B, "0x6728F7");
static_assert(offsetof(GameData, bUnk0C) == 0x0C, "0x6728F8");
static_assert(offsetof(GameData, bUnk0D) == 0x0D, "0x6728F9");
static_assert(offsetof(GameData, wUnk0E) == 0x0E, "0x6728FA");
static_assert(offsetof(GameData, dwUnk10) == 0x10, "0x6728FC");
static_assert(offsetof(GameData, wUnk14) == 0x14, "0x672900");
static_assert(offsetof(GameData, bPlaybackKind) == 0x16, "0x672902");
static_assert(offsetof(GameData, szCreatorName) == 0x18, "0x672904");
static_assert(offsetof(GameData, szMapName) == 0x31, "0x67291D");

#define gGameData \
  (*reinterpret_cast<GameData *>(reinterpret_cast<BYTE *>(&gGameHeader) + 0x1C))

// 0x66FD34 ghInst, spelled as src117/lang/gluBN.cpp spells it.
#ifndef OC_GHINST
#define OC_GHINST
#ifdef OC_DLL_MODE
#define ghInst (*reinterpret_cast<HINSTANCE *>(OC_VA(0x66FD34u)))
#else
extern HINSTANCE ghInst;
#endif
#endif

// 0x66FD28 ghWnd, spelled as src117/lang/gds_font.cpp spells it.
#ifndef OC_GHWND
#define OC_GHWND
#ifdef OC_DLL_MODE
#define ghWnd (*reinterpret_cast<void **>(OC_VA(0x66FD28u)))
#else
extern void *ghWnd;
#endif
#endif

// 0x4EDB94 / 0x4EDB9C, the two product codes, spelled as
// src117/lang/gluMain.cpp spells them.
#ifndef OC_SG_STAR_PRODUCT_CODE
#define OC_SG_STAR_PRODUCT_CODE
#ifdef OC_DLL_MODE
#define sgStarProductCode (*reinterpret_cast<char (*)[1]>(OC_VA(0x4EDB94u)))
#else
extern char sgStarProductCode[1];
#endif
#endif

#ifndef OC_SG_SEXP_PRODUCT_CODE
#define OC_SG_SEXP_PRODUCT_CODE
#ifdef OC_DLL_MODE
#define sgSexpProductCode (*reinterpret_cast<char (*)[1]>(OC_VA(0x4EDB9Cu)))
#else
extern char sgSexpProductCode[1];
#endif
#endif

// 0x508948 -- the one survey entry sub_4671B0 hands WriteSurveyData.
#ifndef OC_OFF_508948
#define OC_OFF_508948
#ifdef OC_DLL_MODE
#define off_508948 (*reinterpret_cast<const char **>(OC_VA(0x508948u)))
#else
extern const char *off_508948;
#endif
#endif
// 0x5264BC g_MapTileset, spelled as src117/lang/gamemap.cpp spells it.
#ifndef g_MapTileset
#ifdef OC_DLL_MODE
#define g_MapTileset (*reinterpret_cast<WORD *>(OC_VA(0x5264BCu)))
#else
extern WORD g_MapTileset;
#endif
#endif

// 0x5264FC gdwColorSlotMap_g, spelled as src117/lang/CUnitColor.cpp spells it.
#ifndef OC_GDWCOLORSLOTMAP_G
#define OC_GDWCOLORSLOTMAP_G
#ifdef OC_DLL_MODE
#define gdwColorSlotMap_g (reinterpret_cast<DWORD *>(OC_VA(0x5264FCu)))
#else
extern DWORD gdwColorSlotMap_g[8] OC_UNALIGNED;
#endif
#endif

// 0x6D3148 dword_6D3148, spelled as src117/lang/gameloop.cpp spells it. The
// two bits RunGame clears are the "intro already seen" flags.
#ifndef dword_6D3148
#ifdef OC_DLL_MODE
#define dword_6D3148 (*reinterpret_cast<DWORD *>(OC_VA(0x6D3148u)))
#else
extern DWORD dword_6D3148;
#endif
#endif

// 0x536720 gbIsExpansion, spelled as src117/lang/gametype.cpp spells it.
#ifndef OC_GBISEXPANSION
#define OC_GBISEXPANSION
#ifdef OC_DLL_MODE
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbIsExpansion;
#endif
#endif

// 0x66FD2C sgbRestartGame_g, spelled as src117/lang/game.cpp spells it.
#ifndef sgbRestartGame_g
#ifdef OC_DLL_MODE
#define sgbRestartGame_g (*reinterpret_cast<DWORD *>(OC_VA(0x66FD2Cu)))
#else
extern DWORD sgbRestartGame_g;
#endif
#endif

// 0x66FD30 sgbLowMem_g, spelled as title.cpp spells it.
#ifndef OC_SGBLOWMEM_G
#define OC_SGBLOWMEM_G
#ifdef OC_DLL_MODE
#define sgbLowMem_g (*reinterpret_cast<DWORD *>(OC_VA(0x66FD30u)))
#else
extern DWORD sgbLowMem_g;
#endif
#endif

// The bodies, in oracle VA order.

// The other instance's main window, polled for up to two seconds, or NULL.
static __forceinline HWND FindOtherInstance(const char *pszTitle)
{
  for (int i = 0; i < 8; ++i) {
    Sleep(250);
    HWND hWnd = FindWindowA(pszTitle, NULL);
    if (hWnd != NULL)
      return hWnd;
  }
  return NULL;
}

// 0x466F20, 140 B. Refuses a second instance: if the named event already
// exists, waits up to two seconds for the other window, raises it, and quits.
#ifdef OC_PAYLOAD
// Hyperion: Hyperion scopes the check to its install folder so it runs
// next to other copies of the game (src117/port/lan/port_instance.cpp). The body
// is replaced here rather than by a hook row because WinMain, its one caller,
// is compiled into the payload and calls this copy directly.
void CheckForOtherInstances(const char *pszTitle)
{
  PortCheckForOtherInstances(pszTitle);
}
#else
void CheckForOtherInstances(const char *pszTitle)
{
  CreateEventA(NULL, FALSE, FALSE, "Starcraft Check For Other Instances");
  if (GetLastError() != ERROR_ALREADY_EXISTS)
    return;

  HWND hWnd = FindOtherInstance(pszTitle);
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
  app_exit(0);
}
#endif  // OC_PAYLOAD

// 0x466FB0, 141 B. Copies one source into the destination: a plain file with
// CopyFileA, a directory by creating the destination tree and recursing.
int CopyFilesRecursive(const char *pszSrc, const char *pszDst)
{
  char szDst[MAX_PATH];
  DWORD cch;

  if ((GetFileAttributesA(pszSrc) & FILE_ATTRIBUTE_DIRECTORY) == 0)
    return (CopyFileA(pszSrc, pszDst, TRUE) != 0) + 1;

  SStrCopy(szDst, pszDst, MAX_PATH);
  cch = SStrLen(szDst);
  if (cch != 0 && szDst[cch - 1] != '\\')
    SStrPack(szDst, "\\", MAX_PATH);
  CreateDirs(szDst);
  return CopyDirRecursive(pszSrc, pszDst);
}

// 0x467040, 177 B. Creates every directory along a path, walking backslash to
// backslash. An existing entry is tolerated only when it is itself a directory.
int CreateDirs(const char *pszPath)
{
  char szDir[MAX_PATH];
  const char *p;
  int bOk;
  DWORD dwErr;

  if (pszPath == NULL || *pszPath == '\0')
    return 0;

  bOk = 1;
  p = SStrChrF(pszPath + 1, '\\');
  while (p != NULL)
  {
    if ((DWORD)(p - pszPath) >= MAX_PATH)
    {
      bOk = 0;
      break;
    }
    SStrCopy(szDir, pszPath, (p - pszPath) + 1);
    if (!CreateDirectoryA(szDir, NULL))
    {
      dwErr = GetLastError();
      if (dwErr == ERROR_ALREADY_EXISTS || dwErr == ERROR_ACCESS_DENIED)
      {
        if ((GetFileAttributesA(szDir) & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
          bOk = 0;
          break;
        }
      }
    }
    p = SStrChrF(p + 1, '\\');
  }
  return bOk;
}

// 0x467100, 163 B. Whether a path sits under either Program Files folder; the
// literal default is used when the shell cannot answer.
int IsProgramFilesDir(const char *pszPath)
{
  char szDir[MAX_PATH];

  if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILESX86, NULL, 0, szDir) < 0)
    SStrPrintf(szDir, MAX_PATH, "c:\\Program Files (x86)");
  if (SStrStr(pszPath, szDir) != NULL)
    return 1;
  if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, 0, szDir) < 0)
    SStrPrintf(szDir, MAX_PATH, "c:\\Program Files");
  return SStrStr(pszPath, szDir) != NULL;
}

// 0x4671B0, 83 B. Files the survey for whichever products are installed, then
// runs it. Both WriteSurveyData calls pass the same one entry.
void sub_4671B0()
{
  InitLocale();
  SetSurveyProduct(sgStarProductCode);
  write_game_creation_log();
  WriteSurveyData("Game", &off_508948, 1);
  if (IsBroodwar())
  {
    SetSurveyProduct(sgSexpProductCode);
    write_game_creation_log();
    WriteSurveyData("Game", &off_508948, 1);
  }
  RunSystemSurvey();
}

// 0x467210, 279 B. Walks one directory's entries and hands each to
// CopyFilesRecursive. FindClose runs even when FindFirstFileA failed, as in
// 1.17.
int CopyDirRecursive(const char *pszSrc, const char *pszDst)
{
  char szDst[MAX_PATH];
  char szSrc[MAX_PATH];
  WIN32_FIND_DATAA fd;
  int cchSrc;
  int cchDst;
  HANDLE hFind;
  int nResult;

  nResult = 0;
  cchSrc = SStrPrintf(szSrc, MAX_PATH, "%s\\*", pszSrc) - 1;
  cchDst = SStrPrintf(szDst, MAX_PATH, "%s\\", pszDst);
  hFind = FindFirstFileA(szSrc, &fd);
  if (hFind != INVALID_HANDLE_VALUE)
  {
    do
    {
      if (SStrCmp(fd.cFileName, ".", 0x7FFFFFFF) != 0
          && SStrCmp(fd.cFileName, "..", 0x7FFFFFFF) != 0)
      {
        SStrCopy(szSrc + cchSrc, fd.cFileName, MAX_PATH - cchSrc);
        SStrCopy(szDst + cchDst, fd.cFileName, MAX_PATH - cchDst);
        nResult |= CopyFilesRecursive(szSrc, szDst);
      }
    } while (FindNextFileA(hFind, &fd));
  }
  FindClose(hFind);
  return nResult;
}

// 0x467330, 295 B. Copies the seven pre-Appdata subdirectories from the old
// install root to the new one.
int TransferLegacyDirsToAppdata(const char *pszOld, const char *pszNew)
{
  char szSrc[MAX_PATH];
  char szDst[MAX_PATH];
  const char *apszDirs[7];
  const char *pszDir;
  const char *pszFrom;
  const char *pszTo;
  int nResult;
  unsigned int i;
  DWORD cch;

  pszFrom = pszOld;
  pszTo = pszNew;
  nResult = 0;
  apszDirs[0] = "characters\\";
  apszDirs[1] = "Errors\\";
  apszDirs[2] = "maps\\replays\\";
  apszDirs[3] = "maps\\download\\";
  apszDirs[4] = "maps\\save";
  apszDirs[5] = "save\\";
  apszDirs[6] = "leagues\\";

  for (i = 0; i < 7; ++i)
  {
    SStrCopy(szSrc, pszFrom, MAX_PATH);
    cch = SStrLen(szSrc);
    if (cch != 0 && szSrc[cch - 1] != '\\')
      SStrPack(szSrc, "\\", MAX_PATH);
    pszDir = apszDirs[i];
    SStrPack(szSrc, pszDir, MAX_PATH);

    SStrCopy(szDst, pszTo, MAX_PATH);
    cch = SStrLen(szDst);
    if (cch != 0 && szDst[cch - 1] != '\\')
      SStrPack(szDst, "\\", MAX_PATH);
    SStrPack(szDst, pszDir, MAX_PATH);

    nResult |= CopyFilesRecursive(szSrc, szDst);
  }
  return nResult;
}

// 0x467460, 264 B. Points Storm's app-data directory at the player's data
// folder.
void InitPaths()
{
  // Hyperion: the player's data goes in Hyperion's own folder, the exe's
  // or the one given with --data (PortDataFolder, port_paths.cpp), so Hyperion
  // leaves the profiles, replays and saved games of the player's other copies of
  // the game alone. The oracle chose Documents\Starcraft\, copying the old
  // folders there from the install folder on a first run, and fell back to the
  // install folder without a Documents folder.
  SFileSetAppDataDir(PortDataFolder());
}

// 0x467570, 365 B. Replaces the process DACL with one that denies Everyone and
// allows only the token's own user, through the SetSecurityInfo that has to be
// looked up at run time.
int InitACL()
{
  BYTE abAcl[0x200];
  HANDLE hProcess;
  TOKEN_USER *pTokenUser;
  PSID pSidEveryone;
  DWORD cbTokenUser;
  SID_IDENTIFIER_AUTHORITY sia = SECURITY_WORLD_SID_AUTHORITY;
  HANDLE hToken;
  HMODULE hAdvapi;
  PFN_SETSECURITYINFO pfnSetSecurityInfo;
  int nResult;

  hProcess = GetCurrentProcess();
  pSidEveryone = NULL;
  hToken = NULL;
  cbTokenUser = 0;
  nResult = 0;
  if (!AllocateAndInitializeSid(&sia, 1, 0, 0, 0, 0, 0, 0, 0, 0,
                                &pSidEveryone))
    goto cleanup;
  if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hToken))
    goto cleanup;
  GetTokenInformation(hToken, TokenUser, NULL, 0, &cbTokenUser);
  if (cbTokenUser > 0x400)
    goto cleanup;
  pTokenUser = static_cast<TOKEN_USER *>(_alloca(cbTokenUser));
  if (!GetTokenInformation(hToken, TokenUser, pTokenUser, cbTokenUser,
                           &cbTokenUser))
    goto cleanup;
  if (!InitializeAcl(reinterpret_cast<ACL *>(abAcl), 0x200, ACL_REVISION))
    goto cleanup;
  if (!AddAccessDeniedAce(reinterpret_cast<ACL *>(abAcl), ACL_REVISION, 0xFA,
                          pSidEveryone))
    goto cleanup;
  if (!AddAccessAllowedAce(reinterpret_cast<ACL *>(abAcl), ACL_REVISION,
                           0x100701, pTokenUser->User.Sid))
    goto cleanup;
  hAdvapi = GetModuleHandleA("advapi32.dll");
  if (hAdvapi == NULL)
    goto cleanup;
  pfnSetSecurityInfo = reinterpret_cast<PFN_SETSECURITYINFO>(
      GetProcAddress(hAdvapi, "SetSecurityInfo"));
  if (pfnSetSecurityInfo == NULL)
    goto cleanup;
  if (pfnSetSecurityInfo(hProcess, 6, 0x80000004, NULL, NULL,
                         reinterpret_cast<ACL *>(abAcl), NULL) == 0)
    nResult = 1;

cleanup:
  if (hToken != NULL)
    CloseHandle(hToken);
  if (pSidEveryone != NULL)
    FreeSid(pSidEveryone);
  return nResult;
}

// 0x4676E0, 332 B. Writes the four-character locale code, big end first, into
// the STAR and SEXP locale files under the app-data directory.
void InitLocale()
{
  char szPath[MAX_PATH];
  char *pszEnd;
  DWORD dwLocale;
  const char *pszAppData;
  intptr_t cchLeft;
  void *pf;

  dwLocale = GetLocaleFourCC();
  if (dwLocale == 0)
    return;
  pszAppData = LocalAppDataDirectory();
  if (pszAppData == NULL)
    return;

  SStrCopy(szPath, pszAppData, MAX_PATH);
  pszEnd = szPath + SStrLen(szPath);
  cchLeft = &szPath[MAX_PATH] - pszEnd;
  SStrCopy(pszEnd, "STAR\\locale", cchLeft);
  CreateDirs(szPath);
  pf = fopen(szPath, "w");
  if (pf != NULL)
  {
    fputc(dwLocale >> 24, pf);
    fputc((dwLocale >> 16) & 0xFF, pf);
    fputc((dwLocale >> 8) & 0xFF, pf);
    fputc(dwLocale & 0xFF, pf);
    fclose(pf);
  }

  if (IsBroodwar())
  {
    SStrCopy(pszEnd, "SEXP\\locale", cchLeft);
    CreateDirs(szPath);
    pf = fopen(szPath, "w");
    if (pf != NULL)
    {
      fputc(dwLocale >> 24, pf);
      fputc((dwLocale >> 16) & 0xFF, pf);
      fputc((dwLocale >> 8) & 0xFF, pf);
      fputc(dwLocale & 0xFF, pf);
      fclose(pf);
    }
  }
}

// 0x467830, 384 B. Refuses to start on a machine below the minimum processor,
// free disk space and memory, then picks the low-memory pool sizes.
void InitMemory()
{
  SYSTEM_INFO si;
  MEMORYSTATUS ms;
  DWORD dwTotalClusters;
  DWORD dwSectorsPerCluster;
  DWORD dwFreeClusters;
  DWORD dwBytesPerSector;
  DWORD dwForce;
  double dFreeMB;

  GetSystemInfo(&si);
  if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL
      && si.dwProcessorType < PROCESSOR_INTEL_486)
  {
    if (!ProcErrorDlg())
      app_exit(1);
  }

  if (GetDiskFreeSpaceA("C:\\", &dwSectorsPerCluster, &dwBytesPerSector,
                        &dwFreeClusters, &dwTotalClusters))
  {
    dFreeMB = dwBytesPerSector / 1024.0 * dwFreeClusters
              * (dwSectorsPerCluster / 1024.0);
    if (dFreeMB < 20.0)
    {
      if (!DiskFreeErrorDlg(static_cast<unsigned int>(dFreeMB)))
        app_exit(1);
    }
  }

  ms.dwLength = sizeof(ms);
  GlobalMemoryStatus(&ms);
  if (ms.dwTotalPhys < 0xD00000)
  {
    if (!MemErrorDlg())
      app_exit(1);
  }
  else if (ms.dwTotalPageFile < 0x1E00000)
  {
    if (!VRamErrorDlg())
      app_exit(1);
  }

  if (SRegLoadValue("Starcraft", "ForceLowMem", 0, &dwForce) && dwForce != 0)
  {
    sgbLowMem_g = 1;
    return;
  }
  if (SRegLoadValue("Starcraft", "ForceHighMem", 0, &dwForce) && dwForce != 0)
  {
    sgbLowMem_g = 0;
    return;
  }
  sgbLowMem_g = (ms.dwTotalPhys < 0x1600000);
}

// 0x467BA0, 310 B. Runs one game and reports the mode it ended in. The six
// carry-overs fill the lobby record's map and game-type fields from the loaded
// map wherever the record left them zero.
void main_game_run()
{
  BOOL bStarted;
  WORD wMode;

  // Hyperion: the game is played at the view's width, --view; the menus
  // are 640x480 (port_view.cpp).
  PortViewSet(PortViewGameWidth());
  set_rand_enabled(1);
  bStarted = new_game();
  set_rand_enabled(0);
  if (gPlayback == 0)
  {
    if (gGameData.wUnk04 == 0)
      gGameData.wUnk04 = gwMapWidth;
    if (gGameData.wUnk06 == 0)
      gGameData.wUnk06 = gwMapHeight;
    if (gGameData.wUnk14 == 0)
      gGameData.wUnk14 = g_MapTileset;
    if (gGameData.bUnk0C == 0)
      gGameData.bUnk0C = gGameHeader.bGameType;
    if (gGameData.bUnk0D == 0)
      gGameData.bUnk0D = gGameHeader.bGameTypeSub;
    if (gGameData.wUnk0E == 0)
      gGameData.wUnk0E = gGameHeader.wSubType;
    replay_set_gamedata(&gGameHeader, gPlayerData.a, gdwColorSlotMap_g);
  }
  close_savefile();
  if (gbMultiPlayerMode != 0)
    sub_4A8DD0(SNetGetCurrentProviderID());
  if (bStarted)
    wMode = run_game();
  else
    wMode = 4;
  if (bStarted) {
    PortScoreGameEnd();  // Hyperion: the score screen's game, kept before it is freed
    PortCrashNotesGameEnd();  // Hyperion: the games played, for a crash report
  }
  free_game(bStarted);
  PortViewSet(640);  // Hyperion: back to the menus' size
  SessionAfterGame(wMode);  // Hyperion: the set-up's choices kept for a restart (port_session.h)
  PortRecordAfterGame();  // Hyperion: --record's replay kept, and the end
  PortBenchAfterGame();   // Hyperion: a benchmark's next step
  PortWatchAfterGame();  // Hyperion: a watched skirmish's next step
  PortReplaySeekAfterGame();  // Hyperion: a replay's seek ended, its snapshots freed
  PortLanAfterGame();     // Hyperion: a LAN room's match, its end told to the room
  ModDataAfterGame();     // Hyperion: a mod's pushes put away with their game
  gwGameMode = wMode;
}

// 0x467CE0, 287 B. Creates the main window. appcritf does not return, though
// 1.17 does not mark it so.
void InitWindow()
{
  // Hyperion: the window is SDL's, on every platform, and its messages are
  // port_winmsg.cpp's, made from SDL's events and handed to WndProc.
  // The oracle registered the class SWarClass (CS_DBLCLKS, WndProc, its icon,
  // the arrow) and created a full-screen WS_POPUP window of it; SDL's window takes
  // the class's name and double clicks, and Hyperion's icon (port_window.cpp). It is
  // titled Hyperion, so it can be told from a classic game on the taskbar
  // (the original's was "Brood War" or "Starcraft"), and created hidden:
  // PortPresentInit, from init_directx, sizes it for full screen or a window and
  // shows it.
  ghWnd = WinMsgCreateWindow(reinterpret_cast<WinMsgProc>(WndProc), "Hyperion");
  if (ghWnd == NULL)
    appcritf("CreateWindowEx");
  PortWindowCreated(ghWnd);  // Hyperion: square corners and the icon (port_window.cpp)
  init_lang_imm(0);
}

// 0x467E00, 67 B. The program entry: brings the install tree, the locale and
// the memory pools up, then hands control to RunGame, which never returns.
extern "C" int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                                 LPSTR lpCmdLine, int nShowCmd)
{
  // Hyperion: what the original's start-up did for the game, in the engine's
  // block of 1.17's data (port_standalone.cpp).
  PortStartUp();
  ghInst = hInstance;
  InitCrashReporter();
  CheckForOtherInstances("SWarClass");
  InitPaths();
  InitLocal(reinterpret_cast<intptr_t>(hInstance));
  InitMemory();
  PortStartupStep("memory check", "passed");  // Hyperion: the start-up log
  kill_problem_apps();
  // Hyperion: InitACL is left out. It denied every other process the rights to read this
  // one, which kept Task Manager from saving a dump of a copy that hangs.
  RunGame();
  return 0;
}

// 0x4679B0, 440 B. Brings the program up, then runs the mode loop until a mode
// outside the table ends it in app_exit.
void RunGame()
{
  gwGameMode = 4;
  init_program();
  PortStartupStep("game data", "fonts, images, dialogs and the map data read");  // Hyperion
  // Hyperion: --mod-check checks a mod once the game's files are open, with no window, and
  // ends the process (port_modcheck.cpp)
  if (PortCommandOptions()->szModCheck[0] != '\0')
    PortModCheckRun();
  SNetSetCodeSignVerifcationFunction(
      reinterpret_cast<bool (__fastcall *)(const char *)>(
          VerifyBlizzardSignature));
  app_add_exit(sub_4671B0);
  init_events();
  InitWindow();
  PortStartupStep("window", "created");  // Hyperion
  init_video();
  PortStartupStep("video and sound", "started");  // Hyperion
  init_speed_options();
  mou_set_pos(g_portView.nWidth / 2, g_portView.nHeight / 2);  // Hyperion: 320, 240
  // Hyperion: a game started from the command line (--map, --replay, --load,
  // --canvas: an editor's test play, a tool's run), or a start an editor drives over the
  // live channel (--control), or a LAN room joined (--join), or an AI lab's worker
  // waiting for its games, goes straight in, and the intro waits, still unseen, for
  // the first start from the menus
  const PortOptions *pOptions = PortCommandOptions();
  // Hyperion: HYPERION_CINEMATIC=n plays the cinematic numbered n first (0 the logo, 1
  // StarCraft's opening), to see one without its campaign
  // Hyperion: a cinematic is the player's to watch, so the start-up log's watchdog waits
  PortStartupWait(true);
  if (const char *pszCinematic = getenv("HYPERION_CINEMATIC"))
    play_flic(atoi(pszCinematic));
  if (pOptions->szMap[0] != '\0' || pOptions->szReplay[0] != '\0' || pOptions->szLoad[0] != '\0' ||
      pOptions->szCanvas[0] != '\0' || pOptions->szControl[0] != '\0' || pOptions->szJoin[0] != '\0' ||
      PortLabWorker())
  {
  }
  else if (!HasExpansionCampaign())
  {
    if ((dword_6D3148 & 0x200) != 0)
    {
      dword_6D3148 &= ~0x200;
      PortStartupStep("cinematics", "the intro plays, the first start's");  // Hyperion
      play_flic(0);
      play_flic(1);
    }
  }
  else
  {
    if ((dword_6D3148 & 0x800) != 0)
    {
      dword_6D3148 &= ~0x800;
      PortStartupStep("cinematics", "the intro plays, the first start's");  // Hyperion
      play_flic(0);
      play_flic(0x18);
    }
  }
  PortStartupWait(false);  // Hyperion
  do_title_dlg();
  PortStartupStep("title", "shown");  // Hyperion
  LoadGraphics();
  PortStartupStep("graphics", "the units' images, scripts and groups loaded");  // Hyperion
  PortStartupReady();  // Hyperion: the next frame shown ends the start
  app_add_exit(group_free_all);
  if (gwGameMode != 4)
    close_title_dlg();

  for (;;)
  {
    cursor_set(10);
    img_update();
    if (!IsBroodwar())
      gbIsExpansion = 0;
    switch (static_cast<short>(gwGameMode))
    {
    case 3:
      main_game_run();
      break;
    case 10:
    {
      play_next_flic();
      // Hyperion: a cinematic watched again from Hyperion's campaign screen goes back to it
      const bool bWatched = PortFrontCinematicWatched();
      if (gwGameMode == 10 && bWatched)
        gwGameMode = 4;
      else if (gwGameMode == 10)
        sub_4BB790(1);
      break;
    }
    case 5:
      sgbRestartGame_g = 1;
      // fall through
    case 1:
      gwGameMode = 3;
      break;
    case 0:
      play_flic(0);
      if (gwGameMode != 0)
        break;
      if (HasExpansionCampaign())
        play_flic(0x18);
      else
        play_flic(1);
      if (gwGameMode != 0)
        break;
      gwGameMode = 4;
      break;
    case 4:
    case 6:
    case 7:
      RunGlues();
      break;
    case 8:
      GameState_Credits();
      if (gwGameMode == 8)
      {
        gwGameMode = 4;
        glGluesMode = 0;
      }
      break;
    case 9:
      GameState_Epilog();
      if (gwGameMode == 9)
      {
        gwGameMode = 4;
        glGluesMode = 0;
      }
      break;
    default:
      goto done;
    }
  }

done:
  app_exit(0);
}
