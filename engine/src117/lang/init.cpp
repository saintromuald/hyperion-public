// init.cpp -- the program's start-up: command-line tokenising, locale
// selection, the MPQ archive set (game, patch, CD, no-CD), the CD search, the
// font tables, the video bring-up and the matching exit hooks. Bodies are in
// oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/init.h"
#include "../h/gds_local.h"
#include "../h/gds_appcrit.h"

#include "../h/glues.h"
#include "../h/Storm.h"
#include "../h/dlg_dlg.h"
#include "../h/snd.h"
#include "../h/spd_dlg.h"
#include "../h/videodlg.h"
#include "../h/net_cmds.h"
#include "../h/CSprite.h"
#include "../h/CtxtHelp.h"
#include "../h/cursor.h"
#include "../h/dlg_dlgdraw.h"
#include "../h/event_Win.h"
#include "../h/gamedata.h"
#include "../h/gds_appfat.h"
#include "../h/gametype.h"
#include "../h/gds_image.h"
#include "../h/grid.h"
#include "../port/platform/port_paths.h"  // Hyperion: PortCdOptional, PortGameCommandLine
#include "../port/data/port_remastered.h"  // Hyperion: PortGameSource
#include "../port/data/port_local.h"  // Hyperion: PortLocalUseOwn
#include "../port/tools/port_startup.h"  // Hyperion: PortStartupStep, PortStartupFail
#include "../port/tools/port_sysinfo.h"  // Hyperion: PortSysInfoMask
#include "../h/net_player.h"
#include "../h/error.h"
#include "../h/gds_inifile.h"
#include "../h/gds_appexi.h"
#include "../h/gds_sys_Win.h"
#include "../h/gds_vidinimo.h"
#include "../gen/globals117.h"

#include <signal.h>
#include <string.h>
#include <stdlib.h>

// KERNEL32's GetCommandLineA, `__declspec(dllimport)` for 1.17's indirect call.
extern "C" __declspec(dllimport) char * __stdcall GetCommandLineA(void);

// Storm's archive imports, spelled character for character as maphdr.cpp
// spells them, extern "C" included.
extern "C" BOOL __stdcall SFileOpenArchive(const char *pszName, DWORD dwPriority,
                                           DWORD dwFlags, HANDLE *phArchive);
extern "C" BOOL __stdcall SFileCloseArchive(HANDLE hArchive);
extern "C" BOOL __stdcall SFileOpenFileEx(HANDLE hMpq, const char *pszFileName,
                                          DWORD dwSearchScope, HANDLE *phFile);
extern "C" BOOL __stdcall SFileCloseFile(HANDLE hFile);

// Storm ordinal 272, __stdcall.
extern "C" int __stdcall SFileSetLocale(int nLocale);

// VERSION.DLL, without __declspec(dllimport): 1.17 calls these through the
// exe's own jump thunks.
extern "C" DWORD __stdcall GetFileVersionInfoSizeA(const char *pszFile,
                                                   DWORD *pdwHandle);
extern "C" BOOL __stdcall GetFileVersionInfoA(const char *pszFile,
                                              DWORD dwHandle, DWORD cbLen,
                                              void *pvData);
extern "C" BOOL __stdcall VerQueryValueA(const void *pBlock,
                                         const char *pszSubBlock,
                                         void **ppBuffer, UINT *puLen);

// Callees in other TUs that no header here declares.

// 0x4CDD10 (font.cpp), 22 B. ECX only, the font it frees.
void __fastcall font_free(void *pFont);

// 0x4CDEC0 (font.cpp), 95 B. ECX is the font's base name ("font16x"), EDX the
// loader callback (LoadFontFile), then an owner file and line it never reads.
// Returns the font, which the callers store in sgpFonts_g.
void *__fastcall font_load(const char *pszName, void *pfnLoadFile,
                const char *pszFile, int nLine);
#if !defined(_MSC_VER)
#include "../h/gds_font.h"  // Hyperion: its overload taking the loader as a function
#endif

// 0x40DEE0 (cheat.cpp), 171 B. ECX is the token, EDX an in/out DWORD of cheat
// bits.
BOOL __fastcall parse_command(const char *pszToken, DWORD *pdwCheatBits);

// 0x4635D0 / 0x463820 (grid.cpp), 306 B / 91 B. ECX and EDX; LoadFontFile
// passes its key slot as an out-parameter and 0. Spelled exactly as netdesc.h
// spells them.
void RetrieveKey(DWORD_PTR *pdwKeyLo, DWORD_PTR *pdwKeyHi);
void cleanup_key(DWORD_PTR dwKeyLo, DWORD_PTR dwKeyHi);

// 0x444990 (gamedata.cpp), 197 B. `ret 14h`: ECX, EDX and five stack words,
// the last three an owner tag; returns the loaded block. Spelled exactly as
// gamedata.h spells it: a local declaration that differs is another overload.
void *__fastcall MemFileLoad(const char *pszFile, DWORD_PTR dwArg2, DWORD *pdwSize,
                             int nFlags, int nScope, const char *pszOwnerFile,
                             int nOwnerLine);


// 0x4CC190 (app.cpp), 14 B. __cdecl varargs, a jump to vappcritf.
void __cdecl appcritf(const char *pszFmt, ...);

// The three CRT callees, at C linkage. They restate what <windows.h> brings in;
// under /MT `_CRTIMP` is empty, so the two agree.
extern "C" {
unsigned long __cdecl strtoul(const char *pszStr, char **ppszEnd, int nBase);
char *__cdecl strtok(char *pszStr, const char *pszDelim);
int __cdecl _strnicmp(const char *psz1, const char *psz2, size_t cch);
}

// The globals this TU reaches, dual-spelled, one #ifndef per name.
// sgpFonts_g, sgGamePalette_g and glCheatBits come from gen/globals117.h.

// 0x66FD34 ghInst, spelled as src117/lang/gds_local.cpp spells it.
#ifndef OC_GHINST
#define OC_GHINST
#ifdef OC_DLL_MODE
#define ghInst (*reinterpret_cast<HMODULE *>(OC_VA(0x66FD34u)))
#else
extern HMODULE ghInst;
#endif
#endif

// 0x66CDE0 -- the patch archive's path, 0x104 bytes.
#ifndef OC_SGSZPATCHARCHIVENAME
#define OC_SGSZPATCHARCHIVENAME
#ifdef OC_DLL_MODE
#define sgszPatchArchiveName (reinterpret_cast<char *>(OC_VA(0x66CDE0u)))
#else
extern char sgszPatchArchiveName[0x104];
#endif
#endif

// 0x66CEE8 -- the semicolon-separated archive list Storm searches, 0x208
// bytes.
#ifndef OC_SGSZMAINARCHIVENAME
#define OC_SGSZMAINARCHIVENAME
#ifdef OC_DLL_MODE
#define sgszMainArchiveName (reinterpret_cast<char *>(OC_VA(0x66CEE8u)))
#else
extern char sgszMainArchiveName[0x208] OC_UNALIGNED;
#endif
#endif

// gVersion @0x66CCA8 and its VersionRecord are in src117/h/init.h.

// 0x51ABB8 gszMapFileTbl, spelled as src117/lang/TrigAct.cpp spells it.
#ifndef OC_GSZMAPFILETBL
#define OC_GSZMAPFILETBL
#ifdef OC_DLL_MODE
#define gszMapFileTbl (reinterpret_cast<char **>(OC_VA(0x51ABB8u)))
#else
extern char *gszMapFileTbl[65];  // Hyperion: mapdata.dat's 65 rows
#endif
#endif

// 0x6D3148 dword_6D3148, spelled as src117/lang/gameloop.cpp spells it.
#ifndef dword_6D3148
#ifdef OC_DLL_MODE
#define dword_6D3148 (*reinterpret_cast<DWORD *>(OC_VA(0x6D3148u)))
#else
extern DWORD dword_6D3148;
#endif
#endif

// 0x66CCD8 -- the module path GetModuleFileNameA fills, 0x104 bytes.
#ifndef OC_SGSZPROGRAMNAME
#define OC_SGSZPROGRAMNAME
#ifdef OC_DLL_MODE
#define sgszProgramName (reinterpret_cast<char *>(OC_VA(0x66CCD8u)))
#else
extern char sgszProgramName[0x104] OC_UNALIGNED;
#endif
#endif

// 0x508620 -- the formatted version string, 0x104 bytes.
#ifndef OC_GSZVERSIONNUMBER
#define OC_GSZVERSIONNUMBER
#ifdef OC_DLL_MODE
#define gszVersionNumber (reinterpret_cast<char *>(OC_VA(0x508620u)))
#else
extern char gszVersionNumber[0x104];
#endif
#endif

// 0x66CCBC -- the -nosound command-line flag, a BYTE.
#ifndef OC_NOSOUND
#define OC_NOSOUND
#ifdef OC_DLL_MODE
#define nosound (*reinterpret_cast<BYTE *>(OC_VA(0x66CCBCu)))
#else
extern BYTE nosound;
#endif
#endif

// 0x66CCC0 -- which font set is loaded: 0 none, 1 glues, 2 game.
#ifndef OC_SGDWCURRENTFONTSET_G
#define OC_SGDWCURRENTFONTSET_G
#ifdef OC_DLL_MODE
#define sgdwCurrentFontSet_g (*reinterpret_cast<DWORD *>(OC_VA(0x66CCC0u)))
#else
extern DWORD sgdwCurrentFontSet_g;
#endif
#endif

// 0x66CCC4 -- the map-data string table, freed by free_gamedata.
#ifndef OC_SGPMAPDATASTRS_G
#define OC_SGPMAPDATASTRS_G
#ifdef OC_DLL_MODE
#define sgpMapDataStrs_g (*reinterpret_cast<void **>(OC_VA(0x66CCC4u)))
#else
extern void *sgpMapDataStrs_g;
#endif
#endif

// 0x66CCC8 / 0x66CCCC / 0x66CCD0 / 0x66CCD4 -- the four open MPQ archives:
// patch, CD, main and expansion. Integers, as glues.h's get_patch_archive and
// get_cd_archive return them.
#ifndef OC_SGHSPATCHARCHIVE
#define OC_SGHSPATCHARCHIVE
#ifdef OC_DLL_MODE
#define sghsPatchArchive (*reinterpret_cast<intptr_t *>(OC_VA(0x66CCC8u)))
#else
extern intptr_t sghsPatchArchive;
#endif
#endif

#ifndef OC_SGHSCDARCHIVE_G
#define OC_SGHSCDARCHIVE_G
#ifdef OC_DLL_MODE
#define sghsCDArchive_g (*reinterpret_cast<intptr_t *>(OC_VA(0x66CCCCu)))
#else
extern intptr_t sghsCDArchive_g;
#endif
#endif

#ifndef OC_SGHSMAINARCHIVE
#define OC_SGHSMAINARCHIVE
#ifdef OC_DLL_MODE
#define sghsMainArchive (*reinterpret_cast<intptr_t *>(OC_VA(0x66CCD0u)))
#else
extern intptr_t sghsMainArchive;
#endif
#endif

#ifndef OC_SGHSEXPANSIONARCHIVE_G
#define OC_SGHSEXPANSIONARCHIVE_G
#ifdef OC_DLL_MODE
#define sghsExpansionArchive_g (*reinterpret_cast<intptr_t *>(OC_VA(0x66CCD4u)))
#else
extern intptr_t sghsExpansionArchive_g;
#endif
#endif

// 0x66D0F0 / 0x66D0F4 -- cmdtok's cursor into the command line and the number
// of characters left, a signed count.
#ifndef OC_COMMANDLINE
#define OC_COMMANDLINE
#ifdef OC_DLL_MODE
#define commandLine (*reinterpret_cast<char **>(OC_VA(0x66D0F0u)))
#else
extern char *commandLine;
#endif
#endif

#ifndef OC_COMMANDLINELENGTH
#define OC_COMMANDLINELENGTH
#ifdef OC_DLL_MODE
#define commandLineLength (*reinterpret_cast<int *>(OC_VA(0x66D0F4u)))
#else
extern int commandLineLength;
#endif
#endif

// 0x6D3184 -- the -ddemulate command-line flag, a BYTE.
#ifndef OC_DDEMULATE
#define OC_DDEMULATE
#ifdef OC_DLL_MODE
#define ddemulate (*reinterpret_cast<BYTE *>(OC_VA(0x6D3184u)))
#else
extern BYTE ddemulate;
#endif
#endif

// 0x66FD28 -- the main window.
#ifndef OC_GHWND
#define OC_GHWND
#ifdef OC_DLL_MODE
#define ghWnd (*reinterpret_cast<void **>(OC_VA(0x66FD28u)))
#else
extern void *ghWnd;
#endif
#endif

// The five .dat descriptor tables free_gamedata releases.
#ifndef OC_GMAPDATAARRAYDATATBL_G
#define OC_GMAPDATAARRAYDATATBL_G
#ifdef OC_DLL_MODE
#define gMAPDATAArrayDataTbl_g (reinterpret_cast<array_data_t*>(OC_VA(0x50125Cu)))
#else
extern array_data_t gMAPDATAArrayDataTbl_g[2] OC_UNALIGNED;
#endif
#endif

#ifndef OC_GPORTDATAARRAYDATATBL_G
#define OC_GPORTDATAARRAYDATATBL_G
#ifdef OC_DLL_MODE
#define gPORTDATAArrayDataTbl_g (reinterpret_cast<array_data_t*>(OC_VA(0x501368u)))
#else
extern array_data_t gPORTDATAArrayDataTbl_g[7] OC_UNALIGNED;
#endif
#endif

#ifndef OC_GTECHDATAARRAYDATATBL_G
#define OC_GTECHDATAARRAYDATATBL_G
#ifdef OC_DLL_MODE
#define gTECHDATAArrayDataTbl_g (reinterpret_cast<array_data_t*>(OC_VA(0x501460u)))
#else
extern array_data_t gTECHDATAArrayDataTbl_g[12] OC_UNALIGNED;
#endif
#endif

#ifndef OC_GUPGRADESARRAYDATATBL
#define OC_GUPGRADESARRAYDATATBL
#ifdef OC_DLL_MODE
#define gUPGRADESArrayDataTbl (reinterpret_cast<array_data_t*>(OC_VA(0x501788u)))
#else
extern array_data_t gUPGRADESArrayDataTbl[13] OC_UNALIGNED;
#endif
#endif

#ifndef OC_GWEAPONSARRAYDATATBL_G
#define OC_GWEAPONSARRAYDATATBL_G
#ifdef OC_DLL_MODE
#define gWEAPONSArrayDataTbl_g (reinterpret_cast<array_data_t*>(OC_VA(0x501828u)))
#else
extern array_data_t gWEAPONSArrayDataTbl_g[25] OC_UNALIGNED;
#endif
#endif

// Restated from init.h.
BOOL __stdcall DialogFunc(intptr_t hDlg, int uMsg, int wParam, intptr_t lParam);
intptr_t LoadFontFile(intptr_t nArg0, int nArg1, intptr_t nArg2, int nArg3, int nArg4,
                 intptr_t nArg5, int nArg6);
void font_close(BYTE bUnused);

// 0x4CEBF0 (24 B). Nullary; FindCDArchive consumes the full 32-bit EAX.
BOOL __fastcall InsertCDDlg();

intptr_t FindCDArchiveFile(intptr_t nArg0, intptr_t nArg1, int nArg2, intptr_t nArg3);

// The bodies, in oracle VA order.

// 0x465470 (42 B). Whether a file is in an archive: ECX is the file name, EDX
// the archive.
int FileExists(intptr_t nArg0, intptr_t nArg1)
{
  HANDLE hFile;

  if (!SFileOpenFileEx(reinterpret_cast<HANDLE>(nArg1),
                       reinterpret_cast<const char *>(nArg0), 0, &hFile))
    return 0;
  SFileCloseFile(hFile);
  return 1;
}

// 0x4654A0 (228 B), `ret 8h`. Walks the logical drives and, for each CD-ROM
// drive, builds "<drive><name>" into the caller's 0x104 buffer and tries to
// open it as an archive. Returns the archive, which OpenCDArchive returns in
// turn.
intptr_t FindCDArchiveFile(intptr_t nArg0, intptr_t nArg1, int nArg2, intptr_t nArg3)
{
  char szDrives[0x104];
  HANDLE hFile;
  char *pszOut;
  HANDLE hArchive;
  const char *pszName;
  char *psz;
  char *pszDrive;
  DWORD dwLen;

  pszOut = reinterpret_cast<char *>(nArg0);
  pszName = reinterpret_cast<const char *>(nArg1);
  dwLen = GetLogicalDriveStringsA(0x104, szDrives);
  if (dwLen == 0)
    return 0;
  if (dwLen > 0x104)
    return 0;
  while (*pszName == '\\')
    pszName++;
  for (psz = szDrives; *psz != 0;)
  {
    pszDrive = psz;
    while (*psz++ != 0)
      ;
    if (GetDriveTypeA(pszDrive) == DRIVE_CDROM)
    {
      SStrCopy(pszOut, pszDrive, 0x104);
      SStrPack(pszOut, pszName, 0x104);
      if (SFileOpenArchive(pszOut, nArg2, 3, &hArchive))
      {
        if (nArg3 != 0)
        {
          if (SFileOpenFileEx(hArchive, reinterpret_cast<const char *>(nArg3),
                              0, &hFile))
          {
            SFileCloseFile(hFile);
          }
          else
          {
            SFileCloseArchive(hArchive);
            continue;
          }
        }
        if (hArchive != 0)
          return reinterpret_cast<intptr_t>(hArchive);
      }
    }
  }
  return 0;
}

// 0x465590 (107 B). The locale's four-character code from the LCID's low WORD,
// 0 for a locale the game has no data for. The codes are MSVC multi-character
// constants: 'deDE' is 0x64654445.
int GetLocaleFourCC()
{
  switch (static_cast<WORD>(GetLocale()))
  {
  case 0x07:
  case 0x407:
    return 'deDE';
  case 0x09:
  case 0x409:
    return 'enUS';
  case 0x0A:
  case 0x40A:
    return 'esES';
  case 0x0C:
  case 0x40C:
    return 'frFR';
  case 0x10:
  case 0x410:
    return 'itIT';
  case 0x16:
  case 0x416:
    return 'ptBR';
  }
  return 0;
}

// 0x465640 (124 B). The DLGPROC InitLocal hands DialogBoxParamA. The
// WM_COMMAND arm returns 1 even for a wParam that is neither 1 nor 2, as 1.17
// does.
BOOL __stdcall DialogFunc(intptr_t hDlg, int uMsg, int wParam, intptr_t lParam)
{
  switch (uMsg)
  {
  case WM_COMMAND:
    if (1 == static_cast<WORD>(wParam))
    {
      EndDialog(reinterpret_cast<HWND>(hDlg), 1);
      ShowCursor(0);
    }
    else if (2 == static_cast<WORD>(wParam))
    {
      EndDialog(reinterpret_cast<HWND>(hDlg), 0);
      ShowCursor(0);
    }
    return 1;
  case WM_INITDIALOG:
    {
      HWND hWnd = reinterpret_cast<HWND>(hDlg);
      ShowCursor(1);
      center_window(hWnd);
      if (lParam != 0)
        SetDlgItemTextA(hWnd, 1000, reinterpret_cast<const char *>(lParam));
    }
    return 1;
  }
  return 0;
}

// 0x4656C0 (159 B). Reads the version file, cuts it at the first line
// terminator and demands that it parses to 0x11; anything else is fatal.
void validate_data_version(intptr_t nArg0)
{
  DWORD dwLeft;
  char *pszEnd;
  char *pszData;
  DWORD dwVer = 0;

  pszData = static_cast<char *>(
      MemFileLoad(reinterpret_cast<const char *>(nArg0), 0, &dwLeft, 1, 0,
                  "init.cpp", 0x5C3));
  if (pszData != 0)
  {
    for (pszEnd = pszData; dwLeft != 0; dwLeft--, pszEnd++)
    {
      if (*pszEnd == '\n' || *pszEnd == '\r' || *pszEnd == 0x1A)
      {
        *pszEnd = 0;
        break;
      }
    }
    if (dwLeft != 0)
    {
      dwVer = strtoul(pszData, &pszEnd, 0);
      if (dwVer == 0x11)
      {
        mem_free(pszData, "init.cpp", 0x5D7);
        return;
      }
    }
  }
  appcritf("Invalid data file version number %s\nProgram version:  %d\n"
           "Datafile version: %d\n",
           nArg0, 0x11, dwVer);
}

// 0x465760 (101 B). Four identical close-and-clear blocks, in 1.17's order,
// which is not the address order: CD, main, patch, expansion.
void close_archives()
{
  if (sghsCDArchive_g)
  {
    SFileCloseArchive(reinterpret_cast<HANDLE>(sghsCDArchive_g));
    sghsCDArchive_g = 0;
  }
  if (sghsMainArchive)
  {
    SFileCloseArchive(reinterpret_cast<HANDLE>(sghsMainArchive));
    sghsMainArchive = 0;
  }
  if (sghsPatchArchive)
  {
    SFileCloseArchive(reinterpret_cast<HANDLE>(sghsPatchArchive));
    sghsPatchArchive = 0;
  }
  if (sghsExpansionArchive_g)
  {
    SFileCloseArchive(reinterpret_cast<HANDLE>(sghsExpansionArchive_g));
    sghsExpansionArchive_g = 0;
  }
}

// 0x4657D0 (26 B). close_archives' first block, on its own.
void close_cd_archive()
{
  if (sghsCDArchive_g)
  {
    SFileCloseArchive(reinterpret_cast<HANDLE>(sghsCDArchive_g));
    sghsCDArchive_g = 0;
  }
}

// 0x4657F0 (90 B). The font-table loader callback font_load is given: loads
// and decodes a font file and returns the block.
intptr_t LoadFontFile(intptr_t nArg0, int nArg1, intptr_t nArg2, int nArg3, int nArg4,
                 intptr_t nArg5, int nArg6)
{
  DWORD_PTR nKey;
  DWORD dwSize;
  void *pvData;

  RetrieveKey(&nKey, 0);
  pvData = MemFileLoad(reinterpret_cast<const char *>(nArg0),
                       static_cast<DWORD>(nArg1), &dwSize, 0, nArg4,
                       reinterpret_cast<const char *>(nArg5), nArg6);
  if (nArg2 != 0)
    *reinterpret_cast<DWORD *>(nArg2) = dwSize;
  DecodeFile(pvData, dwSize, nKey);
  cleanup_key(nKey, 0);
  return reinterpret_cast<intptr_t>(pvData);
}

// 0x465850 (46 B). Frees the four fonts. The BYTE it takes is never read; both
// callers clear CL before the call.
void font_close(BYTE bUnused)
{
  int i;

  for (i = 0; i < 4; i++)
  {
    if (sgpFonts_g[i] != 0)
    {
      font_free(sgpFonts_g[i]);
      sgpFonts_g[i] = 0;
    }
  }
  sgdwCurrentFontSet_g = 0;
}

// 0x465880 (5 B). Tail call -> `jmp FreeLocaleModule`.
void FreeLocal()
{
  FreeLocaleModule();
}

// 0x465890 (5 B). Tail call -> `jmp snd_close`.
void exit_snd()
{
  snd_close();
}

// 0x4658A0 (5 B). Tail call -> `jmp deinit_video`.
void exit_video()
{
  deinit_video();
}

// 0x4658B0 (6 B). Declared in glues.h.
intptr_t get_cd_archive()
{
  return sghsCDArchive_g;
}

// 0x4658C0 (159 B). A strtok wrapper over the command line. A non-zero argument
// re-arms the cursor; zero continues. The separators depend on whether the
// cursor sits on a quote.
intptr_t cmdtok(intptr_t nArg0)
{
  char *pszTok;
  int nSkip;

  if (nArg0 != 0)
  {
    commandLine = reinterpret_cast<char *>(nArg0);
    commandLineLength = (int)strlen(reinterpret_cast<char *>(nArg0));
  }
  if (commandLineLength <= 0)
    return 0;
  while (*commandLine == ' ' || *commandLine == '\t')
  {
    commandLine++;
    commandLineLength--;
  }
  pszTok = strtok(commandLine, *commandLine == '"' ? "\"" : " \t");
  if (pszTok == 0)
    return 0;
  nSkip = (int)(strlen(commandLine) + 1);
  commandLineLength -= nSkip;
  commandLine += nSkip;
  return reinterpret_cast<intptr_t>(pszTok);
}

// 0x465960 (190 B). Formats the executable's own product version into
// gszVersionNumber. The version block is freed on every path that allocated
// it, and on none that did not.
void get_program_version_info()
{
  UINT uLen;
  DWORD dwHandle;
  VS_FIXEDFILEINFO *pInfo;
  DWORD cbInfo;
  DWORD dwVersionMS;
  void *pBlock;

  if (!GetModuleFileNameA(ghInst, sgszProgramName, 0x104))
  {
    sgszProgramName[0] = '\0';
    return;
  }
  cbInfo = GetFileVersionInfoSizeA(sgszProgramName, &dwHandle);
  if (cbInfo == 0)
    return;
  pBlock = SMemAlloc(cbInfo, "init.cpp", 0x159, 0);
  if (GetFileVersionInfoA(sgszProgramName, 0, cbInfo, pBlock)
      && VerQueryValueA(pBlock, "\\", reinterpret_cast<void **>(&pInfo),
                        &uLen))
  {
    dwVersionMS = pInfo->dwProductVersionMS;
    psnprintf(gszVersionNumber, 0x104, "Version %d.%d.%d", HIWORD(dwVersionMS),
              LOWORD(dwVersionMS), HIWORD(pInfo->dwProductVersionLS));
  }
  SMemFree(pBlock, "init.cpp", 0x198, 0);
}

// 0x465A20 (128 B). Builds "<install path>local.dll", loads it, and on failure
// puts up dialog template 106 before app_exit. The path buffer is 0x10E bytes,
// the length 1.17 gives SFileGetInstallPath.
void InitLocal(intptr_t nArg0)
{
  char szPath[0x10E];

  SFileGetInstallPath(szPath, 0x10E, 1);
  SStrPack(szPath, "local.dll", 0x10E);
  // Hyperion: with none in the game's folder, a Remastered install's
  PortRemasteredLocalDll(szPath, sizeof szPath);
  if (!InitLocaleModule(szPath))
  {
    // Hyperion: the classic game's dialog 106 here said it could not go on. A Battle.net
    // install in a language with no Local.dll, or a Mac's, has none, so Hyperion stands in for it
    // (port_local.cpp).
    const DWORD dwError = GetLastError();
    (void)nArg0;
    PortLocalUseOwn();
    PortStartupStep("Local.dll", "none loaded from %s (error %lu): Hyperion's own stands in",
                    PortSysInfoMask(szPath).c_str(), dwError);
  }
  else
    PortStartupStep("Local.dll", "%s", PortSysInfoMask(szPath).c_str());  // Hyperion
  SFileSetLocale(GetLocale());
  app_add_exit(FreeLocal);
}

// 0x465AA0 (277 B). The start-up sequence: every subsystem is brought up and
// its teardown pushed onto the exit list in the same statement pair.
void init_program()
{
  InitFileErrorMode();
  app_add_exit(net_exit);
  if (signal(SIGINT, SIG_IGN) != SIG_ERR)
    signal(SIGBREAK, SIG_IGN);
  parse_args();
  app_add_exit(close_archives);
  open_archives();
  validate_data_version(reinterpret_cast<intptr_t>("rez\\DataVersion.txt"));
  if (sghsCDArchive_g != 0)
    validate_data_version(reinterpret_cast<intptr_t>("rez\\CDversion.txt"));
  dword_6D3148 |= 7;
  ini_file_init();
  app_add_exit(reinterpret_cast<void(__fastcall *)(void)>(ini_file_close));
  error_init();
  accel_init();
  app_add_exit(accel_close);
  font_init_glues();
  app_add_exit(reinterpret_cast<void(__fastcall *)(void)>(font_close));
  img_init();
  app_add_exit(img_free);
  cursor_init();
  dlg_mgr_init();
  set_dlg_target_bitmap(&_img_draw_buf);
  ctxhelp_init();
  app_add_exit(ctxhelp_close);
  load_array_data("arr\\mapdata.dat", gMAPDATAArrayDataTbl_g);
  sgpMapDataStrs_g = str_load_table_ex("arr\\mapdata.tbl", gszMapFileTbl, 0x41,
                                       load_game_data, "init.cpp", 0x649);
  app_add_exit(free_gamedata);
  AllocGametypes();
  app_add_exit(reinterpret_cast<void(__fastcall *)(void)>(gametype_exit));
}

// 0x465BC0 (104 B). The video and sound bring-up. snd_init's out-parameter is
// the only local, 12 bytes in 1.17.
// Hyperion: snd.cpp's snd_err_t holds a pointer, so at 64 bits it is 16 bytes and
// snd_init's failure arms wrote past 1.17's 12; three pointer-sized words hold it
// on either width.
void init_video()
{
  ULONG_PTR aSndOut[3];

  init_video_opts();
  vid_init_mode();
  app_add_exit(exit_video);
  vid_restore_palette(0, 0x100, sgGamePalette_g);
  img_update();
  sfxdata_init();
  app_add_exit(sfxdata_exit);
  if (!nosound)
  {
    if (snd_init(ghWnd, aSndOut))
      app_add_exit(exit_snd);
  }
}

// 0x465C30 (5 B). Tail call -> `jmp init_spd_opts`.
void init_speed_options()
{
  init_spd_opts();
}

// 0x465C40 (151 B). The four loads go into sgpFonts_g descending, [3] first,
// and the line numbers 0x141..0x144 are 1.17's.
void font_init_game()
{
  if (sgdwCurrentFontSet_g != 2)
  {
    font_close(0);
    sgdwCurrentFontSet_g = 2;
    sgpFonts_g[3] = font_load("font16x", LoadFontFile, "init.cpp", 0x141);
    sgpFonts_g[2] = font_load("font16", LoadFontFile, "init.cpp", 0x142);
    sgpFonts_g[1] = font_load("font10", LoadFontFile, "init.cpp", 0x143);
    sgpFonts_g[0] = font_load("font8", LoadFontFile, "init.cpp", 0x144);
  }
}

// 0x465CE0 (151 B). font_init_game's twin; set id 1, lines 0x131..0x134, and
// "font14" where the game set takes "font8".
void font_init_glues()
{
  if (sgdwCurrentFontSet_g != 1)
  {
    font_close(0);
    sgdwCurrentFontSet_g = 1;
    sgpFonts_g[3] = font_load("font16x", LoadFontFile, "init.cpp", 0x131);
    sgpFonts_g[2] = font_load("font16", LoadFontFile, "init.cpp", 0x132);
    sgpFonts_g[1] = font_load("font14", LoadFontFile, "init.cpp", 0x133);
    sgpFonts_g[0] = font_load("font10", LoadFontFile, "init.cpp", 0x134);
  }
}

// 0x465D80 (81 B). Frees the map-data strings and the five .dat tables. The
// owner tag, "init.cpp" line 0x516, is 1.17's.
void free_gamedata()
{
  mem_free(sgpMapDataStrs_g, "init.cpp", 0x516);
  sgpMapDataStrs_g = 0;
  gamedata_free_data(gMAPDATAArrayDataTbl_g);
  gamedata_free_data(gUPGRADESArrayDataTbl);
  gamedata_free_data(gWEAPONSArrayDataTbl_g);
  gamedata_free_data(gTECHDATAArrayDataTbl_g);
  gamedata_free_data(gPORTDATAArrayDataTbl_g);
}

// 0x465DE0 (473 B). Opens Stardat and patch_rt from the install directory,
// then Broodat unless this is a spawned copy, and builds the archive list
// Storm searches.
void open_archives()
{
  char szPath[0x104];
  char szBroodPath[0x104];
  HANDLE hArchive;
  intptr_t hs;

  memset(&gVersion, 0, sizeof(gVersion));
  gVersion.pszPatchArchive = sgszPatchArchiveName;
  gVersion.cb = sizeof(gVersion);
  gVersion.pszVersion = gszVersionNumber;
  gVersion.pszProgramName = sgszProgramName;
  gVersion.pszMainArchive = sgszMainArchiveName;
  get_program_version_info();

  SFileGetInstallPath(szPath, 0x104, 0);
  SStrPack(szPath, "\\Stardat.mpq", 0x104);
  // Hyperion: a StarCraft: Remastered install as the main archive, in Stardat's place
  // (port_remastered.cpp)
  if (PortGameSource() == PORT_SOURCE_REMASTERED)
    SStrCopy(szPath, PORT_REMASTERED_ARCHIVE, 0x104);
  hs = (SFileOpenArchive(szPath, 0x7D0, 2, &hArchive) && hArchive != NULL)
           ? reinterpret_cast<intptr_t>(hArchive)
           : 0;
  sghsMainArchive = hs;
  if (hs == 0)
    FileErrorDlg("Stardat.mpq", GetLastError());

  // Hyperion: with the install alone no MPQ is opened, so one left in the game's folder
  // (patch_rt.mpq, Broodat.mpq, a CD archive) cannot stand in front of the install's files
  bool bMpqs = PortGameSource() != PORT_SOURCE_REMASTERED;
  SFileGetInstallPath(sgszPatchArchiveName, 0x104, 0);
  SStrPack(sgszPatchArchiveName, "\\patch_rt.mpq", 0x104);
  hs = (bMpqs && SFileOpenArchive(sgszPatchArchiveName, 0x1B58, 2, &hArchive)
        && hArchive != NULL)
           ? reinterpret_cast<intptr_t>(hArchive)
           : 0;
  sghsPatchArchive = hs;

  init_cdkey();
  app_add_exit(free_cdkey);
  if (!IsSpawnedCopy() && bMpqs)
    FindCDArchive(1, 0);

  sgszMainArchiveName[0] = '\0';
  if (!IsSpawnedCopy() && bMpqs)
  {
    SFileGetInstallPath(szBroodPath, 0x104, 0);
    SStrPack(szBroodPath, "\\Broodat.mpq", 0x104);
    hs = (SFileOpenArchive(szBroodPath, 0x9C4, 2, &hArchive)
          && hArchive != NULL)
             ? reinterpret_cast<intptr_t>(hArchive)
             : 0;
    sghsExpansionArchive_g = hs;
    if (hs != 0)
    {
      SStrCopy(sgszMainArchiveName, szBroodPath, 0x208);
      SStrPack(sgszMainArchiveName, ";", 0x208);
    }
  }
  // Hyperion: the development source, Remastered's archive searched before every MPQ,
  // which serve what it lacks
  if (PortGameSource() == PORT_SOURCE_BOTH)
    SFileOpenArchive(PORT_REMASTERED_ARCHIVE, 0x1F40, 2, &hArchive);
  CheckIsBroodwar();
  SStrPack(sgszMainArchiveName, szPath, 0x208);
  // Hyperion: the start-up log
  PortStartupStep("archives", "%s; patch_rt.mpq %s; Brood War %s; CD archive %s", PortSysInfoMask(szPath).c_str(),
                  sghsPatchArchive != 0 ? "open" : "none", sghsExpansionArchive_g != 0 ? "open" : "none",
                  sghsCDArchive_g != 0 ? "open" : "none");
}

// 0x465FC0 (165 B), `ret 4h`. Reads the CD's install path from the registry
// ("Starcraft"/"StarCD"), appends the archive name and opens it; on any failure
// it falls back to the drive sweep. nArg2's slot is reused for the archive
// handle, as 1.17's frame does.
intptr_t OpenCDArchive(intptr_t nArg0, int nArg1, intptr_t nArg2)
{
  char szPath[0x104];
  HANDLE hFile;
  intptr_t nFile;
  BOOL bHavePath;

  bHavePath = SRegLoadString("Starcraft", "StarCD", 0, szPath, 0x104);
  nFile = nArg2;
  if (bHavePath)
  {
    SStrPack(szPath, reinterpret_cast<const char *>(nArg0), 0x104);
    if (SFileOpenArchive(szPath, nArg1, 3, reinterpret_cast<HANDLE *>(&nArg2)))
    {
      if (nFile != 0)
      {
        if (SFileOpenFileEx(reinterpret_cast<HANDLE>(nArg2),
                            reinterpret_cast<const char *>(nFile), 0, &hFile))
        {
          SFileCloseFile(hFile);
        }
        else
        {
          SFileCloseArchive(reinterpret_cast<HANDLE>(nArg2));
          goto fallback;
        }
      }
      if (nArg2 != 0)
        return nArg2;
    }
  }
fallback:
  return FindCDArchiveFile(reinterpret_cast<intptr_t>(szPath), nArg0, nArg1, nFile);
}

// 0x466070 (121 B). Opens an archive in the install directory, optionally
// checking that it holds a given file; nArg2's slot is reused for that file's
// handle.
intptr_t OpenNoCDArchive(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, intptr_t nArg3)
{
  char *pszPath = reinterpret_cast<char *>(nArg0);
  HANDLE hArchive;

  SFileGetInstallPath(pszPath, 0x104, 0);
  SStrPack(pszPath, reinterpret_cast<const char *>(nArg1), 0x104);
  if (SFileOpenArchive(pszPath, (DWORD)nArg2, 2, &hArchive))
  {
    if (nArg3 != 0)
    {
      if (SFileOpenFileEx(hArchive, reinterpret_cast<const char *>(nArg3), 0,
                          reinterpret_cast<HANDLE *>(&nArg2)))
      {
        SFileCloseFile(reinterpret_cast<HANDLE>(nArg2));
      }
      else
      {
        SFileCloseArchive(hArchive);
        return 0;
      }
    }
    if (hArchive != NULL)
      return reinterpret_cast<intptr_t>(hArchive);
  }
  return 0;
}

// 0x4660F0 (179 B). The CD search proper: tries the installed BroodWar.mpq,
// then the installed StarCraft.mpq, then sweeps the CD drives for Install.exe,
// and keeps prompting until the CD turns up or the user declines; if
// InsertCDDlg itself fails, the program exits.
BOOL FindCDArchive(int nArg0, intptr_t nArg1)
{
  char szPath[0x104];

  if (sghsCDArchive_g == 0)
  {
    sghsCDArchive_g = OpenNoCDArchive(reinterpret_cast<intptr_t>(szPath),
                                      reinterpret_cast<intptr_t>("\\BroodWar.mpq"),
                                      1000, nArg1);
    if (sghsCDArchive_g == 0)
    {
      sghsCDArchive_g = OpenNoCDArchive(reinterpret_cast<intptr_t>(szPath),
                                        reinterpret_cast<intptr_t>("\\StarCraft.mpq"),
                                        1000, nArg1);
      if (sghsCDArchive_g == 0)
      {
        sghsCDArchive_g = OpenCDArchive(reinterpret_cast<intptr_t>("\\Install.exe"),
                                        1000, nArg1);
        // Hyperion: no CD archive where one may be missing (PortCdOptional): the
        // start goes on, and a file only the CD has is not there
        if (sghsCDArchive_g == 0 && PortCdOptional())
          return nArg0 != 0;
        while (sghsCDArchive_g == 0)
        {
          if (nArg0 == 0)
            return 0;
          if (!InsertCDDlg())
            app_exit(0);
          sghsCDArchive_g = OpenCDArchive(reinterpret_cast<intptr_t>("\\Install.exe"),
                                          1000, nArg1);
        }
      }
    }
  }
  return 1;
}

// 0x4661B0 (99 B).
void parse_args()
{
  char *pszCmdLine;
  DWORD dwCheatBits;
  char *pszTok;

  // Hyperion: without Hyperion's own options (port_paths.cpp), which this
  // parser would match against its switches by prefix.
  pszCmdLine = PortGameCommandLine();
  if (pszCmdLine != 0)
  {
    cmdtok(reinterpret_cast<intptr_t>(pszCmdLine));
    dwCheatBits = glCheatBits;
    for (pszTok = reinterpret_cast<char *>(cmdtok(0)); pszTok != 0;
         pszTok = reinterpret_cast<char *>(cmdtok(0)))
    {
      if (parse_command(pszTok, &dwCheatBits))
        cheat_set_bits(dwCheatBits, 0);
      else
        parse_argument(reinterpret_cast<intptr_t>(pszTok));
    }
  }
}

// 0x466220 (130 B). One strlen, reused as the length of all three compares, so
// a prefix of a switch matches it.
BOOL parse_argument(intptr_t nArg0)
{
  const char *pszArg = reinterpret_cast<const char *>(nArg0);
  size_t cchArg = strlen(pszArg);

  if (_strnicmp(pszArg, "nosound", cchArg) == 0)
  {
    nosound = 1;
    return 1;
  }
  if (_strnicmp(pszArg, "crash", cchArg) == 0)
  {
    crash();
    return 1;
  }
  if (_strnicmp(pszArg, "ddemulate", cchArg) == 0)
  {
    ddemulate = 1;
    return 1;
  }
  return 0;
}

// 0x4662B0 (6 B). Declared in glues.h.
intptr_t get_patch_archive()
{
  return sghsPatchArchive;
}
