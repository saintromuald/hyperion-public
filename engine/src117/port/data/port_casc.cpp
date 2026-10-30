// port_casc.cpp -- files out of a StarCraft: Remastered install (docs/front-end.md, F1).
//
// Remastered keeps its data in CASC storage, which CascLib reads (MIT, fetched by
// tools/fetch_casclib.py). The game's files come from it when Hyperion runs on an install
// (port_remastered.cpp), and so do the subtitles, the chat filter's words and the glyphs map
// text needs (port_maptext.cpp); its HD art is held (PortCascPaid). Nothing is required:
// without an install every caller falls back.
//
// The install is --install-folder's when the command line names StarCraft from Battle.net
// (port_install.cpp), else HYPERION_REMASTERED's folder when that is set, else the one Settings
// names (the "remastered" setting, chosen with Browse), else the one Battle.net's uninstall
// entry names, as tools/assets/scasc.py finds it; on a Mac, which has no such entry,
// Battle.net's own place for it, /Applications/StarCraft. The storage opens on the first read and
// stays open; one chosen while none is open opens at once (PortCascUse).
//
// The free StarCraft is the classic game, and its install holds Remastered's paid content too:
// the HD art at both scales, the Cartooned skin, the HD consoles, portraits and tilesets, and
// the sounds Remastered recorded again. Blizzard's game unlocks them after its purchase, and
// nothing in the install tells an owner's from a free one. Every read from the storage comes
// through PortCascRead, which refuses those files (kPaid) unless the player owns Remastered
// (PortRemasteredOwned); while the user holds HD (the PRD's decision of 2026-10-21) no build
// reads them, and the classic game's files alone are read. HYPERION_FILE_LOG=1 lists each
// name read or refused in hyperion_casc.log.
//
// Battle.net's app installs the files of one text language: English's game types and Local.dll,
// say, are only in an English install (port_local.cpp stands in for them). The install's text
// language is in its .build.info (PortCascTextLocale). HYPERION_TEST_TEXT_LOCALE=<locale> makes
// this install one in another language, for a test: the storage's tags say which files such an
// install would have downloaded, and the others are refused (TestLanguage). The files of that
// language, which this install does not have, come from HYPERION_TEST_LOCALE_FILES, a folder
// tools/assets/fetch_locale.py fills from Battle.net's CDN (TestFile).
#include <windows.h>
#include <mutex>
#include <SDL3/SDL.h>
#include "CascLib.h"
#include "../platform/port_posix.h"  // PortNativePath
#include "../platform/port_settings.h"
#include "../front/port_ui.h"
#include "../platform/port_paths.h"  // PortLogFile, PortCommandOptions
#include "port_install.h"            // PortInstallCommandLine
#include "../tools/port_startup.h"   // PortStartupStep
#include "../tools/port_sysinfo.h"   // PortSysInfoMask, PortSysInfoSet
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

static HANDLE g_hStorage;
static bool g_bTried;
static char g_szFolder[MAX_PATH];  // the install found, opened or not; empty with none
static char g_szText[16] = "enUS";  // its text language (PortCascTextLocale)
static char g_szSpeech[16] = "enUS";  // and its speech's (PortCascSpeechLocale)
static std::set<std::string> *g_pHidden;  // HYPERION_TEST_TEXT_LOCALE's: what such an install lacks

static bool CascInstall(char *pszOut, size_t nOut)
{
  const char *pszEnv = SDL_getenv("HYPERION_REMASTERED");
  const char *pszCommand = PortCommandOptions()->szInstallFolder;
  if (PortInstallCommandLine() == PORT_INSTALL_BATTLENET && pszCommand[0] != 0) {
    // --install-folder's, or the install it is inside (port_install.cpp)
    if (!PortCascCheck(pszCommand, pszOut, nOut))
      SDL_strlcpy(pszOut, pszCommand, nOut);
  } else if (pszEnv != NULL && pszEnv[0] != 0) {
    SDL_strlcpy(pszOut, pszEnv, nOut);
  } else if (PortSettingString("hyperion", "remastered", pszOut, nOut) && pszOut[0] != 0) {
    // the folder Settings was given
  } else {
#if defined(__APPLE__)
    SDL_strlcpy(pszOut, "Z:\\Applications\\StarCraft", nOut);
#else
    HKEY hKey;
    DWORD nType = 0;
    DWORD nSize = static_cast<DWORD>(nOut);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\StarCraft", 0,
                      KEY_READ, &hKey) != ERROR_SUCCESS)
      return false;
    const LONG nResult = RegQueryValueExA(hKey, "InstallLocation", NULL, &nType,
                                          reinterpret_cast<BYTE *>(pszOut), &nSize);
    RegCloseKey(hKey);
    if (nResult != ERROR_SUCCESS || nType != REG_SZ)
      return false;
    pszOut[nOut - 1] = 0;
#endif
  }
  char szInfo[MAX_PATH + 16];
  SDL_snprintf(szInfo, sizeof szInfo, "%s\\.build.info", pszOut);
  return GetFileAttributesA(szInfo) != INVALID_FILE_ATTRIBUTES;
}

// The storage, its folder and whether it was tried are the HD loader's thread's too
// (port_hd.cpp): every function below holds this while it uses them.
static std::mutex g_lock;

// A language a folder's install was set to in Battle.net's app: in .build.info, the active
// line's tags end a group with it, the text's with " text?" and the speech's with " speech?"
// ("... enUS speech?:... enUS text?"). A test's HYPERION_TEST_TEXT_LOCALE stands for both.
static void BuildInfoLocale(const char *pszFolder, const char *pszGroup, char *pszOut, size_t nOut)
{
  SDL_strlcpy(pszOut, "enUS", nOut);
  const char *pszTest = SDL_getenv("HYPERION_TEST_TEXT_LOCALE");
  if (pszTest != NULL && pszTest[0] != 0) {
    SDL_strlcpy(pszOut, pszTest, nOut);
    return;
  }
  char szInfo[MAX_PATH + 16];
  SDL_snprintf(szInfo, sizeof szInfo, "%s\\.build.info", pszFolder);
  size_t nSize = 0;
  char *pText = static_cast<char *>(SDL_LoadFile(PortNativePath(szInfo), &nSize));
  if (pText == NULL)
    return;
  std::vector<std::vector<std::string>> rows;
  std::string cell;
  rows.emplace_back();
  for (size_t i = 0; i <= nSize; ++i) {
    const char c = i < nSize ? pText[i] : '\n';
    if (c == '|' || c == '\n') {
      rows.back().push_back(cell);
      cell.clear();
      if (c == '\n')
        rows.emplace_back();
    } else if (c != '\r') {
      cell += c;
    }
  }
  SDL_free(pText);
  size_t nActive = SIZE_MAX, nTags = SIZE_MAX;
  for (size_t i = 0; i < rows[0].size(); ++i) {
    if (rows[0][i].compare(0, 7, "Active!") == 0)
      nActive = i;
    if (rows[0][i].compare(0, 5, "Tags!") == 0)
      nTags = i;
  }
  for (size_t r = 1; r < rows.size() && nTags != SIZE_MAX; ++r) {
    const std::vector<std::string> &row = rows[r];
    if (row.size() <= nTags || (nActive < row.size() && row[nActive] != "1"))
      continue;
    const std::string &tags = row[nTags];
    const size_t nAt = tags.find(pszGroup);
    if (nAt == std::string::npos || nAt == 0)
      continue;
    const size_t nFrom = tags.find_last_of(" :", nAt - 1);
    const std::string locale = tags.substr(nFrom == std::string::npos ? 0 : nFrom + 1,
                                           nAt - (nFrom == std::string::npos ? 0 : nFrom + 1));
    if (locale.size() == 4)
      SDL_strlcpy(pszOut, locale.c_str(), nOut);
    return;
  }
}

void PortCascTextLocaleOf(const char *pszFolder, char *pszOut, size_t nOut)
{
  BuildInfoLocale(pszFolder, " text?", pszOut, nOut);
}

const char *PortCascLanguageName(const char *pszLocale)
{
  static const char *const kNames[][2] = {
      {"enUS", "English"},          {"deDE", "German"},           {"esES", "Spanish (Spain)"},
      {"esMX", "Spanish (Mexico)"}, {"frFR", "French"},           {"itIT", "Italian"},
      {"jaJP", "Japanese"},         {"koKR", "Korean"},           {"plPL", "Polish"},
      {"ptBR", "Portuguese"},       {"ruRU", "Russian"},          {"zhCN", "Chinese (simplified)"},
      {"zhTW", "Chinese (traditional)"},
  };
  for (const auto &name : kNames)
    if (SDL_strcasecmp(name[0], pszLocale) == 0)
      return name[1];
  return pszLocale;
}

// HYPERION_TEST_TEXT_LOCALE's install in pszLocale: the files tagged with a language but not
// with this one, which Battle.net's app would not have downloaded. Called under g_lock.
static void TestLanguage(const char *pszLocale)
{
  size_t cb = 0;
  CascGetStorageInfo(g_hStorage, CascStorageTags, NULL, 0, &cb);
  std::vector<char> buf(cb > 0 ? cb : 1);
  if (cb == 0 || !CascGetStorageInfo(g_hStorage, CascStorageTags, buf.data(), buf.size(), &cb))
    return;
  const CASC_STORAGE_TAGS *pTags = reinterpret_cast<const CASC_STORAGE_TAGS *>(buf.data());
  ULONGLONG nLanguages = 0, nThis = 0;
  for (size_t i = 0; i < pTags->TagCount && i < 64; ++i) {
    if (pTags->Tags[i].TagValue != 1)  // the languages' group
      continue;
    nLanguages |= 1ull << i;
    if (SDL_strcasecmp(pTags->Tags[i].szTagName, pszLocale) == 0)
      nThis |= 1ull << i;
  }
  g_pHidden = new std::set<std::string>;
  CASC_FIND_DATA fd;
  HANDLE hFind = CascFindFirstFile(g_hStorage, "*", &fd, NULL);
  // CascLib goes on past the last file with an empty name
  for (bool bMore = hFind != NULL; bMore && fd.szFileName[0] != 0; bMore = CascFindNextFile(hFind, &fd)) {
    if ((fd.TagBitMask & nLanguages) == 0 || (fd.TagBitMask & nThis) != 0)
      continue;
    std::string name(fd.szFileName);
    for (char &c : name)
      c = c == '/' ? '\\' : static_cast<char>(SDL_tolower(static_cast<unsigned char>(c)));
    g_pHidden->insert(name);
  }
  if (hFind != NULL)
    CascFindClose(hFind);
  PortStartupStep("battle.net", "HYPERION_TEST_TEXT_LOCALE: as an install in %s, %u files left out", pszLocale,
                  static_cast<unsigned>(g_pHidden->size()));
}

// A file of the test's other language from HYPERION_TEST_LOCALE_FILES, under its name in the
// storage, or NULL. Called under g_lock.
static void *TestFile(const char *pszName, size_t *pnSize)
{
  const char *pszFolder = SDL_getenv("HYPERION_TEST_LOCALE_FILES");
  if (pszFolder == NULL || pszFolder[0] == 0 || SDL_strncasecmp(pszName, "locales", 7) != 0)
    return NULL;
  std::string path = std::string(pszFolder) + "\\" + pszName;
  for (char &c : path)
    c = c == '/' ? '\\' : c;
  return SDL_LoadFile(PortNativePath(path.c_str()), pnSize);
}

// Whether the test's install lacks the file. Called under g_lock.
static bool Hidden(const char *pszName)
{
  if (g_pHidden == NULL)
    return false;
  std::string name(pszName);
  for (char &c : name)
    c = c == '/' ? '\\' : static_cast<char>(SDL_tolower(static_cast<unsigned char>(c)));
  return g_pHidden->count(name) != 0;
}

static bool CascOpen(void)
{
  if (g_bTried)
    return g_hStorage != NULL;
  g_bTried = true;
  char szPath[MAX_PATH] = "";
  if (!CascInstall(szPath, sizeof szPath)) {
    PortStartupStep("battle.net", "no StarCraft from Battle.net%s%s", szPath[0] != 0 ? " in " : " found",
                    PortSysInfoMask(szPath).c_str());
    return false;
  }
  SDL_strlcpy(g_szFolder, szPath, sizeof g_szFolder);
  if (!CascOpenStorage(PortNativePath(szPath), 0, &g_hStorage)) {
    // CascLib's reason, which the first-start window's words do not give
    const DWORD dwError = GetCascError();
    g_hStorage = NULL;
    PortStartupStep("battle.net", "StarCraft from Battle.net in %s did not open (CascLib's error %lu)",
                    PortSysInfoMask(szPath).c_str(), dwError);
    PortSysInfoSet("install", "battle.net", "%s, did not open (error %lu)", PortSysInfoMask(szPath).c_str(), dwError);
    return false;
  }
  PortCascTextLocaleOf(szPath, g_szText, sizeof g_szText);
  BuildInfoLocale(szPath, " speech?", g_szSpeech, sizeof g_szSpeech);
  const char *pszTest = SDL_getenv("HYPERION_TEST_TEXT_LOCALE");
  PortStartupStep("battle.net", "StarCraft from Battle.net opened in %s, its text in %s (%s), its speech in %s",
                  PortSysInfoMask(szPath).c_str(), PortCascLanguageName(g_szText), g_szText,
                  PortCascLanguageName(g_szSpeech));
  PortSysInfoSet("install", "battle.net", "%s, opened, text %s, speech %s", PortSysInfoMask(szPath).c_str(), g_szText,
                 g_szSpeech);
  if (pszTest != NULL && pszTest[0] != 0)
    TestLanguage(g_szText);
  return true;
}

int PortCascAvailable(void)
{
  std::lock_guard<std::mutex> hold(g_lock);
  return CascOpen();
}

const char *PortCascFolder(void)
{
  std::lock_guard<std::mutex> hold(g_lock);
  CascOpen();
  return g_szFolder;
}

const char *PortCascTextLocale(void)
{
  std::lock_guard<std::mutex> hold(g_lock);
  CascOpen();
  return g_szText;
}

const char *PortCascSpeechLocale(void)
{
  std::lock_guard<std::mutex> hold(g_lock);
  CascOpen();
  return g_szSpeech;
}

bool PortCascHidden(const char *pszName)
{
  std::lock_guard<std::mutex> hold(g_lock);
  CascOpen();
  return Hidden(pszName);
}

// A Remastered install is a folder whose .build.info names StarCraft's CDN path
// (tpr/sc1); the folder picked, or the nearest above it that is one, so a folder inside
// the install (x86_64, Data) finds it too.
int PortCascCheck(const char *pszPicked, char *pszOut, size_t nOut)
{
  char szDir[MAX_PATH];
  SDL_strlcpy(szDir, pszPicked, sizeof szDir);
  for (;;) {
    size_t n = SDL_strlen(szDir);
    while (n > 3 && (szDir[n - 1] == '\\' || szDir[n - 1] == '/'))
      szDir[--n] = 0;
    char szInfo[MAX_PATH + 16];
    SDL_snprintf(szInfo, sizeof szInfo, "%s\\.build.info", szDir);
    size_t nSize = 0;
    char *pText = static_cast<char *>(SDL_LoadFile(PortNativePath(szInfo), &nSize));
    if (pText != NULL) {
      const bool bStarCraft = SDL_strstr(pText, "tpr/sc1") != NULL;
      SDL_free(pText);
      if (bStarCraft) {
        SDL_strlcpy(pszOut, szDir, nOut);
        return 1;
      }
    }
    char *pSlash = SDL_strrchr(szDir, '\\');
    char *pOther = SDL_strrchr(szDir, '/');
    if (pOther > pSlash)
      pSlash = pOther;
    if (pSlash == NULL || pSlash - szDir < 3)
      return 0;
    *pSlash = 0;
  }
}

// The install Settings was given, which the setting already holds: open at once when no
// storage is open (1, or 0 when it will not open), or 2 when another is open, which the
// next start replaces.
int PortCascUse(const char *pszFolder)
{
  std::lock_guard<std::mutex> hold(g_lock);
  if (g_hStorage != NULL)
    return SDL_strcasecmp(g_szFolder, pszFolder) == 0 ? 1 : 2;
  g_bTried = false;
  return CascOpen() ? 1 : 0;
}

// Remastered's paid content, by the start of its name or by its kind, matched without case
// and with either slash. The classic game's own files are under SD\ and the locales' SD
// folders, or are the classic formats beside them (a .grp, a .pcx, a .smk's .webm), and none
// of them matches.
static const char *const kPaidFolders[] = {
    "anim\\",      // the HD sprites, at 4x
    "hd2\\",       // everything HD at 2x
    "carbot\\",    // the Cartooned skin, a purchase of its own
    "effect\\",    // the HD effects
    "parallax\\",  // the HD space backdrop
    "portrait\\",  // the HD portraits (the classic ones are SD\portrait\)
    "sound\\",     // the sounds Remastered recorded again (the classic ones are SD\sound\)
};
// by kind too: the HD sprites, pictures, tilesets and tile masks, in the folders that also hold
// the classic game's .grp, .pcx, .wpe and .cv5
static const char *const kPaidKinds[] = {".anim", ".dds.grp", ".dds.vr4", ".tmsk"};

bool PortRemasteredOwned(void)
{
  return false;  // held: the user, 2026-10-21 (PRD)
}

bool PortCascPaid(const char *pszName)
{
  std::string name(pszName);
  for (char &c : name)
    c = c == '/' ? '\\' : c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  // a locale's files (locales\<locale>\Assets\...) are the same kinds, by the same rules
  if (name.compare(0, 8, "locales\\") == 0) {
    const size_t nAssets = name.find("\\assets\\", 8);
    if (nAssets != std::string::npos)
      name.erase(0, nAssets + 8);
  }
  if (name.compare(0, 3, "sd\\") == 0)
    return false;
  for (const char *psz : kPaidFolders)
    if (name.compare(0, SDL_strlen(psz), psz) == 0)
      return true;
  for (const char *psz : kPaidKinds) {
    const size_t n = SDL_strlen(psz);
    if (name.size() >= n && name.compare(name.size() - n, n, psz) == 0)
      return true;
  }
  // the HD cinematics (the classic ones are SD\smk\; the subtitles beside them are not HD)
  if (name.compare(0, 4, "smk\\") == 0 && name.size() > 5 && name.compare(name.size() - 5, 5, ".webm") == 0)
    return true;
  // a console's HD pictures (game\consoles\<skin>\*.dds); its classic .grp files are not
  return name.compare(0, 14, "game\\consoles\\") == 0 && name.size() > 4 &&
         name.compare(name.size() - 4, 4, ".dds") == 0;
}

// HYPERION_FILE_LOG=1: each name asked of the storage, the first time, read, missing or
// refused, a line each in hyperion_casc.log in the data folder. Called under g_lock.
static void CascLog(const char *pszName, const char *pszWhat)
{
  static int s_nOn = -1;
  static FILE *s_f = NULL;
  static std::set<std::string> *s_pSeen = NULL;
  if (s_nOn < 0) {
    const char *psz = getenv("HYPERION_FILE_LOG");
    s_nOn = psz && *psz && *psz != '0';
    if (s_nOn) {
      s_f = fopen(PortLogFile("hyperion_casc.log"), "a");
      s_pSeen = new std::set<std::string>;
      s_nOn = s_f != NULL;
    }
  }
  if (!s_nOn || !s_pSeen->insert(pszName).second)
    return;
  fprintf(s_f, "%s\t%s\n", pszName, pszWhat);
  fflush(s_f);
}

// The file's bytes, SDL_malloc'd, or NULL. CASC names use either slash, so the other is
// tried too. Remastered's paid content is refused while the player does not own it.
void *PortCascRead(const char *pszName, size_t *pnSize)
{
  std::lock_guard<std::mutex> hold(g_lock);
  if (!CascOpen())
    return NULL;
  if (PortCascPaid(pszName) && !PortRemasteredOwned()) {
    CascLog(pszName, "refused");
    return NULL;
  }
  if (Hidden(pszName)) {
    CascLog(pszName, "not in the test's install in another language");
    return NULL;
  }
  if (void *pTest = TestFile(pszName, pnSize)) {
    CascLog(pszName, "read from HYPERION_TEST_LOCALE_FILES");
    return pTest;
  }
  char szName[MAX_PATH];
  for (int nTry = 0; nTry < 2; ++nTry) {
    SDL_strlcpy(szName, pszName, sizeof szName);
    if (nTry == 1) {
      for (char *p = szName; *p; ++p)
        *p = *p == '\\' ? '/' : *p == '/' ? '\\' : *p;
    }
    HANDLE hFile;
    if (!CascOpenFile(g_hStorage, szName, 0, CASC_OPEN_BY_NAME, &hFile))
      continue;
    ULONGLONG nSize = 0;
    void *pData = NULL;
    DWORD nRead = 0;
    if (CascGetFileSize64(hFile, &nSize) && nSize > 0 && nSize < 0x40000000) {
      pData = SDL_malloc(static_cast<size_t>(nSize));
      if (pData != NULL &&
          (!CascReadFile(hFile, pData, static_cast<DWORD>(nSize), &nRead) || nRead != nSize)) {
        SDL_free(pData);
        pData = NULL;
      }
    }
    CascCloseFile(hFile);
    if (pData != NULL) {
      *pnSize = static_cast<size_t>(nSize);
      CascLog(pszName, "read");
      return pData;
    }
  }
  CascLog(pszName, "missing");
  return NULL;
}
