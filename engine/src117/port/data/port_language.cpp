// port_language.cpp -- the language the game's text and Hyperion's menus are in.
//
// One setting, Settings' Language, chooses it, as Remastered's does; by default it is the
// install's text language (PortCascTextLocale). The game's text then comes from that language's
// tables in the install (port_remastered.cpp), which a Battle.net install has for its own text
// language alone, so a language it lacks, or a classic install, leaves the game in English.
#include <SDL3/SDL.h>
#include <string.h>
#include "port_language.h"
#include "port_remastered.h"            // PortGameSource
#include "../front/port_ui.h"           // PortCascAvailable, PortCascRead, PortCascTextLocale
#include "../platform/port_settings.h"  // PortSettingString
#include "../tools/port_startup.h"      // PortStartupStep
#include "../tools/port_sysinfo.h"      // PortSysInfoSet

const char *const g_apszPortLanguages[] = {"enUS", "deDE", "esES", "esMX", "frFR", "itIT", "jaJP",
                                           "koKR", "plPL", "ptBR", "ruRU", "zhCN", "zhTW"};
const int g_nPortLanguages = static_cast<int>(SDL_arraysize(g_apszPortLanguages));

namespace {

char g_szSetting[16];
char g_szChosen[8];
char g_szGame[8];
char g_szSpeech[8];
bool g_bSetting, g_bGame;

// pszIn as Remastered writes a locale ("plPL" from "plpl", "pl-PL" or "pl_PL"), into psz[8];
// false when it is not one of Remastered's.
bool Normal(const char *pszIn, char *psz)
{
  char sz[5] = {};
  int n = 0;
  for (const char *p = pszIn; *p != 0 && n < 4; ++p)
    if (*p != '-' && *p != '_')
      sz[n++] = *p;
  if (n != 4)
    return false;
  for (int i = 0; i < g_nPortLanguages; ++i)
    if (SDL_strcasecmp(g_apszPortLanguages[i], sz) == 0) {
      SDL_strlcpy(psz, g_apszPortLanguages[i], 8);
      return true;
    }
  return false;
}

bool ReadsInstall(void)
{
  return PortGameSource() != PORT_SOURCE_MPQ && PortCascAvailable();
}

void DecideGame(void)
{
  if (g_bGame)
    return;
  g_bGame = true;
  SDL_strlcpy(g_szGame, "enUS", sizeof g_szGame);
  SDL_strlcpy(g_szSpeech, "enUS", sizeof g_szSpeech);
  const char *pszWant = PortLanguageChosen();
  const char *pszWhy = "";
  if (SDL_strcasecmp(pszWant, "enUS") != 0) {
    char szName[96];
    SDL_snprintf(szName, sizeof szName, "locales\\%s\\Assets\\rez\\stat_txt.json", pszWant);
    size_t cb = 0;
    void *p = ReadsInstall() ? PortCascRead(szName, &cb) : NULL;
    if (p != NULL)
      SDL_strlcpy(g_szGame, pszWant, sizeof g_szGame);
    else
      pszWhy = ReadsInstall() ? ": the install has no text in it" : ": the game does not read StarCraft: Remastered";
    SDL_free(p);
  }
  // the classic speech, translated in four languages, follows the install's speech language
  char szSpeech[8];
  if (ReadsInstall() && Normal(PortCascSpeechLocale(), szSpeech))
    for (const char *psz : {"deDE", "esES", "frFR", "itIT"})
      if (SDL_strcmp(psz, szSpeech) == 0)
        SDL_strlcpy(g_szSpeech, szSpeech, sizeof g_szSpeech);
  PortStartupStep("language", "chosen %s (%s), the game's text in %s%s, its speech in %s", PortLanguageNative(pszWant),
                  PortLanguageSetting(), PortLanguageNative(g_szGame), pszWhy, PortLanguageNative(g_szSpeech));
  PortSysInfoSet("run", "language", "%s; game text %s, speech %s", PortLanguageSetting(), g_szGame, g_szSpeech);
}

}  // namespace

const char *PortLanguageSetting(void)
{
  if (!g_bSetting) {
    g_bSetting = true;
    const char *pszEnv = SDL_getenv("HYPERION_LANGUAGE");
    if (pszEnv != NULL && pszEnv[0] != 0)
      SDL_strlcpy(g_szSetting, pszEnv, sizeof g_szSetting);
    else if (!PortSettingString("hyperion", "language", g_szSetting, sizeof g_szSetting))
      g_szSetting[0] = 0;
    char sz[8];
    if (Normal(g_szSetting, sz)) {
      SDL_strlcpy(g_szSetting, sz, sizeof g_szSetting);
      SDL_strlcpy(g_szChosen, sz, sizeof g_szChosen);
    } else {
      SDL_strlcpy(g_szSetting, "install", sizeof g_szSetting);
      if (!PortCascAvailable() || !Normal(PortCascTextLocale(), g_szChosen))
        SDL_strlcpy(g_szChosen, "enUS", sizeof g_szChosen);
    }
  }
  return g_szSetting;
}

const char *PortLanguageChosen(void)
{
  PortLanguageSetting();
  return g_szChosen;
}

const char *PortLanguageGame(void)
{
  DecideGame();
  return g_szGame;
}

const char *PortLanguageSpeech(void)
{
  DecideGame();
  return g_szSpeech;
}

const char *PortLanguageNative(const char *pszLocale)
{
  static const char *const kNames[][2] = {
      {"enUS", "English"},
      {"deDE", "Deutsch"},
      {"esES", "Espa\xC3\xB1ol (Espa\xC3\xB1" "a)"},
      {"esMX", "Espa\xC3\xB1ol (M\xC3\xA9xico)"},
      {"frFR", "Fran\xC3\xA7" "ais"},
      {"itIT", "Italiano"},
      {"jaJP", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"},
      {"koKR", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4"},
      {"plPL", "Polski"},
      {"ptBR", "Portugu\xC3\xAAs (Brasil)"},
      {"ruRU", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9"},
      {"zhCN", "\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87"},
      {"zhTW", "\xE7\xB9\x81\xE9\xAB\x94\xE4\xB8\xAD\xE6\x96\x87"},
  };
  for (const auto &name : kNames)
    if (SDL_strcasecmp(name[0], pszLocale) == 0)
      return name[1];
  return pszLocale;
}
