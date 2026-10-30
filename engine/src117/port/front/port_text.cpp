// port_text.cpp -- Hyperion's own text in the menu language (port_text.h, docs/translating.md).
//
// The catalogs come from engine/locale/<locale>.po, compiled in for each build
// (locale_catalogs.h, tools/locale_catalog.py). The menu language is the Language setting's,
// as the game's text is (port_language.cpp), and English when Hyperion has no catalog for it.
// It is decided at the first call and holds until the next start.
#include <SDL3/SDL.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include "port_text.h"
#include "../data/port_language.h"  // PortLanguageChosen

namespace {

struct PortTextEntry {
  const char *pszKey;  // the English, or its context, 0x04 and the English
  const char *apszForms[3];
};

struct PortTextCatalog {
  const char *pszLocale;
  int nPlural;  // the plural rule, as tools/locale_catalog.py numbers them
  const PortTextEntry *pEntries;
  int nEntries;
};

#if __has_include("locale_catalogs.h")
#include "locale_catalogs.h"  // kPortTextCatalogs, written by tools/locale_catalog.py
#else
const PortTextCatalog kPortTextCatalogs[] = {{NULL, 0, NULL, 0}};
const int kPortTextCatalogCount = 0;
#endif

const PortTextCatalog *g_pCatalog;
std::unordered_map<std::string, const PortTextEntry *> *g_pIndex;
bool g_bDecided;

void Decide(void)
{
  if (g_bDecided)
    return;
  g_bDecided = true;
  const char *pszWant = PortLanguageChosen();
  for (int i = 0; i < kPortTextCatalogCount; ++i)
    if (SDL_strcmp(kPortTextCatalogs[i].pszLocale, pszWant) == 0)
      g_pCatalog = &kPortTextCatalogs[i];
  if (g_pCatalog == NULL)
    return;
  // kept for the process's life, as the text it hands out is
  g_pIndex = new std::unordered_map<std::string, const PortTextEntry *>();
  g_pIndex->reserve(static_cast<size_t>(g_pCatalog->nEntries));
  for (int i = 0; i < g_pCatalog->nEntries; ++i)
    g_pIndex->emplace(g_pCatalog->pEntries[i].pszKey, &g_pCatalog->pEntries[i]);
}

// PortTextEnglish's count on this thread
thread_local int t_nEnglish;

const PortTextEntry *Find(const char *pszKey)
{
  Decide();
  if (g_pIndex == NULL || t_nEnglish > 0 || pszKey == NULL || pszKey[0] == 0)
    return NULL;
  const auto it = g_pIndex->find(pszKey);
  return it == g_pIndex->end() ? NULL : it->second;
}

// which of a language's forms n takes, gettext's Plural-Forms for each rule
int PluralIndex(int nRule, long long n)
{
  if (n < 0)
    n = -n;
  const long long n10 = n % 10, n100 = n % 100;
  switch (nRule) {
  case 0:
    return 0;
  case 2:
    return n > 1 ? 1 : 0;
  case 3:
    return n10 == 1 && n100 != 11 ? 0 : n10 >= 2 && n10 <= 4 && (n100 < 10 || n100 >= 20) ? 1 : 2;
  case 4:
    return n == 1 ? 0 : n10 >= 2 && n10 <= 4 && (n100 < 10 || n100 >= 20) ? 1 : 2;
  default:
    return n != 1 ? 1 : 0;
  }
}

}  // namespace

const char *PortText(const char *pszEnglish)
{
  const PortTextEntry *e = Find(pszEnglish);
  return e != NULL ? e->apszForms[0] : pszEnglish;
}

const char *PortTextC(const char *pszContext, const char *pszEnglish)
{
  Decide();
  if (g_pIndex == NULL || t_nEnglish > 0)
    return pszEnglish;
  const PortTextEntry *e = Find((std::string(pszContext) + '\x04' + pszEnglish).c_str());
  return e != NULL ? e->apszForms[0] : pszEnglish;
}

const char *PortTextN(const char *pszOne, const char *pszOther, long long n)
{
  const PortTextEntry *e = Find(pszOne);
  if (e == NULL)
    return n == 1 ? pszOne : pszOther;
  const char *psz = e->apszForms[PluralIndex(g_pCatalog->nPlural, n)];
  return psz != NULL ? psz : e->apszForms[0];
}

std::string PortTextFill(const char *pszText, std::initializer_list<std::string> args)
{
  std::string out;
  for (const char *p = pszText; *p != 0; ++p) {
    if (*p == '{' && p[1] >= '0' && p[1] <= '9') {
      const char *q = p + 1;
      size_t k = 0;
      while (*q >= '0' && *q <= '9')
        k = k * 10 + static_cast<size_t>(*q++ - '0');
      if (*q == '}' && k < args.size()) {
        out += *(args.begin() + k);
        p = q;
        continue;
      }
    }
    out += *p;
  }
  return out;
}

std::string PortTextF(const char *pszEnglish, std::initializer_list<std::string> args)
{
  return PortTextFill(PortText(pszEnglish), args);
}

std::string PortTextNF(const char *pszOne, const char *pszOther, long long n,
                       std::initializer_list<std::string> args)
{
  return PortTextFill(PortTextN(pszOne, pszOther, n), args);
}

PortTextEnglish::PortTextEnglish()
{
  ++t_nEnglish;
}

PortTextEnglish::~PortTextEnglish()
{
  --t_nEnglish;
}

const char *PortTextLocale(void)
{
  Decide();
  return g_pCatalog != NULL ? g_pCatalog->pszLocale : "enUS";
}

int PortTextLanguages(const char *apszOut[], int nMax)
{
  int n = 0;
  if (n < nMax)
    apszOut[n++] = "enUS";
  for (int i = 0; i < kPortTextCatalogCount && n < nMax; ++i)
    apszOut[n++] = kPortTextCatalogs[i].pszLocale;
  return n;
}
