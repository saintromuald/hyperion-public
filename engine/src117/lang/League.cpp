// League.cpp -- ladder replay and game-result recording, oracle 0x4662C0 through
// 0x4666B0. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/League.h"
#include "../gen/globals117.h"
#include "../h/game.h"           // league_write_replay, league_get_game_result
#include "../h/gluBNProfile.h"   // league_get_name, league_load_profile_img
#include "../h/replay.h"         // sub_480BD0
#include "../h/gds_local.h"      // psnprintf
#include "../h/Storm.h"          // SStrCopy / SStrLen / SMemAlloc / SMemFree

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// 0x66D0F8 and 0x66D118, this TU's .data. gszLeagueName_g is spelled and typed as
// src117/lang/game.cpp spells it; league_write_gameresult's SStrPrintf gives
// gszLeagueGameResult_g its capacity, 0xC00.
#ifdef OC_DLL_MODE
  #ifndef gszLeagueName_g
  #define gszLeagueName_g (reinterpret_cast<char *>(OC_VA(0x66D0F8u)))
  #endif
  #ifndef gszLeagueGameResult_g
  #define gszLeagueGameResult_g (reinterpret_cast<char *>(OC_VA(0x66D118u)))
  #endif
#else
extern char  gszLeagueName_g[1];        // 0x66D0F8
extern char  gszLeagueGameResult_g[1];  // 0x66D118
#endif

// Callees no header of ours declares. The Storm imports are spelled exactly as
// src117/lang/maphdr.cpp spells them; a second spelling that differs even in a
// width is an overload.
extern "C" BOOL __stdcall SFileOpenArchive(const char *pszName, DWORD dwPriority,
                                           DWORD dwFlags, HANDLE *phArchive);
extern "C" BOOL __stdcall SFileCloseArchive(HANDLE hArchive);
extern "C" int __cdecl SStrPrintf(char *pszDest, size_t cchDest,
                                  const char *pszFmt, ...);

// Storm ordinal 323 through thunk 0x4D00AC, spelled as src117/h/gluBNProfile.h
// spells it so the two cannot disagree.
extern "C" BOOL __stdcall SBmpLoadImage(const char *pszFileName, void *pPalette,
                                        void *pBuffer, DWORD dwBufferSize,
                                        DWORD *pdwWidth, DWORD *pdwHeight,
                                        DWORD *pdwBpp);

// CreateFileA @0x4EB194, ReadFile @0x4EB1EC, WriteFile @0x4EB1F0 and
// CloseHandle @0x4EB228 come from <windows.h>, which platform.h's includers
// already pull in; redeclaring them is a C2733 against the SDK.

// The 16-byte record at [ebp-10h] is a SYSTEMTIME: get_replay_filename reads
// WORDs at +0, +2, +6, +8, +0Ah and +0Ch, which are wYear, wMonth, wDay,
// wHour, wMinute and wSecond.

// The bodies, in oracle VA order.

// Copies the finished game's LastReplay.rep into the ladder folder under a
// timestamped name, which it also writes back through pszPath.
BOOL __stdcall league_write_replay(char *pszPath)
{
  char szSrc[0x104];
  char szUserPath[0x104];
  DWORD dwWritten;
  DWORD dwRead;

  sub_480BD0(szUserPath, 0x104);
  SStrPrintf(szSrc, 0x104, "%s\\maps\\replays\\%s.%s", szUserPath, "LastReplay",
             "rep");

  char *pszStamp = get_replay_filename();
  SStrPrintf(pszPath, 0x104, "%s\\%s%s\\%s_%s.%s", szUserPath, "leagues\\",
             gszLeagueName_g, gszLeagueName_g, pszStamp, "rep");
  SMemFree(pszStamp, "League.cpp", 0x9C, 0);

  HANDLE hSrc = CreateFileA(szSrc, 0x80000000u, 1, NULL, 3, 0x80, NULL);
  if (hSrc == INVALID_HANDLE_VALUE)
    return FALSE;

  HANDLE hDst = CreateFileA(pszPath, 0x40000000u, 0, NULL, 2, 0x80, NULL);
  if (hDst == INVALID_HANDLE_VALUE)
    return FALSE;

  char *pBuf = static_cast<char *>(SMemAlloc(0x4000, "League.cpp", 0xBA, 0));
  ReadFile(hSrc, pBuf, 0x4000, &dwRead, NULL);
  while (dwRead != 0) {
    WriteFile(hDst, pBuf, dwRead, &dwWritten, NULL);
    ReadFile(hSrc, pBuf, 0x4000, &dwRead, NULL);
  }

  if (pBuf != NULL)
    SMemFree(pBuf, "delete", -1, 0);

  CloseHandle(hSrc);
  CloseHandle(hDst);
  return TRUE;
}

// Installs the ladder name. Reached only by address, as SNETUIDATA's league
// callback.
BOOL __stdcall UiSetLeagueCallback(const char *pszName)
{
  SStrCopy(gszLeagueName_g, pszName, 0x1E);
  return TRUE;
}

// The ladder name, or NULL while it is empty.
intptr_t league_get_name()
{
  return gszLeagueName_g[0] != 0 ? (intptr_t)gszLeagueName_g : 0;
}

// The last game result document, or NULL while it is empty.
intptr_t league_get_game_result()
{
  return gszLeagueGameResult_g[0] != 0 ? (intptr_t)gszLeagueGameResult_g : 0;
}

// Loads profile.pcx out of the ladder's own .mpq. A failed decode frees the
// buffer and returns it anyway, which is the oracle's behaviour.
void *league_load_profile_img()
{
  BYTE bPalette[0x400];
  char szMpq[0x104];
  DWORD dwBpp;
  DWORD dwHeight;
  DWORD dwWidth;
  HANDLE hMpq;

  const char *pszName = (const char *)league_get_name();
  SStrPrintf(szMpq, 0x104, "%s%s\\%s.mpq", "leagues\\", pszName, pszName);
  if (SFileOpenArchive(szMpq, 0, 0, &hMpq)) {
    void *pImg = SMemAlloc(0xD2F0, "League.cpp", 0x5B, 0);
    if (pImg != NULL &&
        !SBmpLoadImage("profile.pcx", bPalette, pImg, 0xD2F0, &dwWidth,
                       &dwHeight, &dwBpp))
      SMemFree(pImg, "League.cpp", 0x65, 0);

    SFileCloseArchive(hMpq);
    return pImg;
  }
  return NULL;
}

// Formats the ladder result document. Each of the eight scores and names is
// appended with a leading space except the first.
void league_write_gameresult(const int *pnResults, const char **ppszNames,
                             const char *pszArg2, const char *pszArg3)
{
  char szNames[0xC8];
  char szResults[0x20];

  int nOff = 0;
  for (int i = 0; i < 8; ++i)
    nOff += SStrPrintf(szResults + nOff, 0x20, i == 0 ? "%d" : " %d",
                       pnResults[i]);

  nOff = 0;
  for (int i = 0; i < 8; ++i)
    nOff += SStrPrintf(szNames + nOff, 0xC8, i == 0 ? "%s" : " %s",
                       ppszNames[i] != NULL ? ppszNames[i] : "(nonplayer)");

  SStrPrintf(gszLeagueGameResult_g, 0xC00,
             "<gameresult>%s</gameresult><playernames>%s</playernames>\r\n%s\r\n%s\r\n",
             szResults, szNames, pszArg2, pszArg3);
}

// Allocates and formats an MMDDYY_HHMMSS replay name. The buffer is sized by
// the format string's own length, which is a function-local static.
char *get_replay_filename()
{
  SYSTEMTIME st;

  static const DWORD cchName = SStrLen("%02d%02d%02d_%02d%02d%02d");

  char *pszName = static_cast<char *>(SMemAlloc(cchName, "League.cpp", 0xDD, 0));
  if (pszName != NULL) {
    GetLocalTime(&st);
    psnprintf(pszName, cchName, "%02d%02d%02d_%02d%02d%02d", st.wMonth, st.wDay,
              st.wYear % 100, st.wHour, st.wMinute, st.wSecond);
  }
  return pszName;
}
