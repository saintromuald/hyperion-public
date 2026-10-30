// port_text.h -- Hyperion's own text in the menu language (docs/translating.md). The English
// is the key: each call returns the language's text from engine/locale/<locale>.po, or the
// English when the language has none. tools/locale_catalog.py reads the literals given here.
#pragma once
#include <initializer_list>
#include <string>

// the English's translation
const char *PortText(const char *pszEnglish);
// the same, for an English that translates two ways (a short word in two places)
const char *PortTextC(const char *pszContext, const char *pszEnglish);
// the form for n, by the language's plural rule; English's "one" is for 1 alone
const char *PortTextN(const char *pszOne, const char *pszOther, long long n);
// the translation with its slots {0}, {1}... filled in order of the arguments, so that a
// language may put them in its own order
std::string PortTextF(const char *pszEnglish, std::initializer_list<std::string> args);
std::string PortTextNF(const char *pszOne, const char *pszOther, long long n,
                       std::initializer_list<std::string> args);
// slots filled in text already translated
std::string PortTextFill(const char *pszText, std::initializer_list<std::string> args);

// a table's English, for the catalog; the code translates it where it is shown
#define PT_(s) s
#define PTC_(c, s) s

// While one is in scope, this thread's PortText calls return the English: for text that goes
// into a report a player copies to us.
struct PortTextEnglish {
  PortTextEnglish();
  ~PortTextEnglish();
};

// the menu language's locale ("enUS" when Hyperion has no catalog for it)
const char *PortTextLocale(void);
// the locales Hyperion has a catalog for, English first
int PortTextLanguages(const char *apszOut[], int nMax);
