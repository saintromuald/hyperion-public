// port_language.h -- the language the game's text and Hyperion's menus are in: the Language
// setting, by default the install's text language (port_language.cpp).
#ifndef PORT_LANGUAGE_H
#define PORT_LANGUAGE_H

// The Language setting ("hyperion"/"language"): "install", the default, for the install's text
// language, or a locale such as "plPL". HYPERION_LANGUAGE stands for it in a test.
const char *PortLanguageSetting(void);

// The language chosen: the setting's, or the install's text language for "install".
const char *PortLanguageChosen(void);

// The game's text: the language chosen when the game reads a Remastered install that has its
// text, else "enUS". Decided once, at the first call.
const char *PortLanguageGame(void);

// The classic speech: the install's speech language when it is one of those with the classic
// speech translated (German, Spanish, French, Italian) and the game reads that install, else
// "enUS".
const char *PortLanguageSpeech(void);

// A locale's name in its own language ("Polski"); the locale itself when it is not one of
// Remastered's.
const char *PortLanguageNative(const char *pszLocale);

// Remastered's locales, English first.
extern const char *const g_apszPortLanguages[];
extern const int g_nPortLanguages;

#endif
