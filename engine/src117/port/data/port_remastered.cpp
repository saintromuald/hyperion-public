// port_remastered.cpp -- a StarCraft: Remastered install as one of Storm's
// archives. The files 1.17 asks for are read out of the install's CASC storage
// (port_casc.cpp), where most are byte for byte 1.17's, and the few Remastered keeps in
// another form are made from what it has. Storm opens this as an archive by its name
// (PORT_REMASTERED_ARCHIVE) and keeps each file it serves in memory (port_stormfile.cpp).
//
// A name is looked for under SD\ first, where Remastered keeps 1.17's own files (the root
// has its HD sounds and fonts of the same names), then as it is, then in the locales' folders,
// English's, then the install's text language's (the campaign, the game types). Some files moved
// (kMoved). In another language (PortLanguageGame, port_language.cpp) its locale's folder comes
// first (Find), and the text is that language's, from Remastered's own text files (LangEntries).
// These are made:
//   tileset\<name>.vx4   from <name>.vx4ex, whose entries are 32 bits where 1.17's are 16;
//                        the entries 1.17 has are the same numbers, and the rest (the newer
//                        doodads') are cut to 16 bits; the engine itself reads the .vx4ex
//                        (PortMegatilesLoad, B5), so this serves a reader asking for the .vx4
//   rez\stat_txt.tbl     Remastered's, which is 1.17's without its last 8 strings, the
//                        replay screen's, with those (kStatTxtTail); in another language from
//                        its rez\stat_txt.json (LangStatTxt)
//   rez\gluAll.tbl       from the locale's rez\<table>.xml, in 1.17's order by the keys
//   rez\network.tbl      port_remastered_keys.h gives (tools/remastered_keys.py)
//   rez\tips.tbl
//   rez\gluHist.tbl
//   rez\help_txt.tbl     (in another language its rez\<table>.json over English's)
//   rez\DataVersion.txt  1.17's data version, which validate_data_version checks
//   game\<r>console.pcx, <r>conover.pcx, <r>pbrempt.pcx, <r>pbrfull.pcx
//                        with a Remastered install alone, from SD\game\'s DDS, put in the
//                        colours the eight tilesets' palettes share (DdsPicture)
//   rez\<name>.bin      1.17's dialog layouts, from Hyperion's source (port_layouts.cpp),
//                        in another language with its text (LangLayout)
//   glue\<folder>\<name>.pcx
//                        with a Remastered install alone, from SD\glue\'s DDS, which hold 1.17's
//                        pictures in every pixel, in the palette of the screen that draws
//                        them: each screen's glue\Pal<xx>\tFont.pcx, which Remastered keeps, ends
//                        with that screen's whole palette (GluePicture, B10)
//   glue\chatroom\iChat.grp
//                        with a Remastered install alone, which has none: 1.17's frames,
//                        every pixel clear, for a room's unseen chat room (ChatIcons)
//   Templates\<name>.got
//                        the classic game types, which an install has in English alone
//                        (port_local.cpp), in another language with its names (LangTemplate)
//   glue\Pal<xx>\Dlg.grp, tile.grp, arrow.grp
//                        the dialog pieces, tiles and cursor, which Remastered has only
//                        redrawn: PalNl's Dlg.grp (kept as 1.17's), the game's tile.grp and
//                        arrow.grp, each put in the screen's colours (GlueGrp)
//
// Should one of those not be made, the glue screen's cursor, dialog frame and tiles have
// stand-ins (kStandIn), the game's own in the game's colours.
//
// The fonts are 1.17's, unencoded: with no key files (font\font.gid), LoadFontFile's
// DecodeFile has no key and leaves them as they are. So in the development source, where the
// MPQs' key files are there, the fonts come from the MPQs.
#include <windows.h>
#include <SDL3/SDL.h>
#include <string.h>
#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "port_remastered.h"
#include "port_remastered_keys.h"
#include "port_language.h"           // PortLanguageGame, PortLanguageSpeech
#include "port_layouts.h"            // PortLayoutFile, PortLayoutFind, PortLayoutMake
#include "../platform/port_json.h"   // PortJsonParse
#include "port_local.h"              // PortLocalTemplate
#include "port_stormapi.h"            // SFileGetInstallPath
#include "port_install.h"             // PortInstallCommandLine
#include "../platform/port_settings.h"  // PortSettingString
#include "../front/port_ui.h"         // PortCascAvailable, PortCascRead, PortCascFolder

const char PORT_REMASTERED_ARCHIVE[] = "<StarCraft Remastered>";

// The locales whose folders a name is looked for in: English, which the game's text is in
// (gds_local.cpp's GetLocale), then the install's text language, the only one a Battle.net
// install in another language has (PortCascTextLocale).
static int Locales(const char *apszLocale[2])
{
  apszLocale[0] = "enUS";
  apszLocale[1] = PortCascTextLocale();
  return SDL_strcasecmp(apszLocale[0], apszLocale[1]) == 0 ? 1 : 2;
}

static int SourceDecide(void)
{
  char szWant[32] = "";
  const char *pszEnv = SDL_getenv("HYPERION_DATA_SOURCE");
  // the command line's --install first (port_install.cpp)
  const PortInstallKind kind = PortInstallCommandLine();
  if (kind != PORT_INSTALL_UNSET)
    SDL_strlcpy(szWant, kind == PORT_INSTALL_CLASSIC ? "mpq" : "remastered", sizeof szWant);
  else if (pszEnv != NULL && pszEnv[0] != 0)
    SDL_strlcpy(szWant, pszEnv, sizeof szWant);
  else
    PortSettingString("hyperion", "data", szWant, sizeof szWant);
  if (SDL_strcasecmp(szWant, "mpq") == 0)
    return PORT_SOURCE_MPQ;
  if (SDL_strcasecmp(szWant, "remastered") == 0)
    return PortCascAvailable() ? PORT_SOURCE_REMASTERED : PORT_SOURCE_MPQ;
  if (SDL_strcasecmp(szWant, "both") == 0)
    return PortCascAvailable() ? PORT_SOURCE_BOTH : PORT_SOURCE_MPQ;
  char szPath[MAX_PATH];
  SFileGetInstallPath(szPath, MAX_PATH, 1);
  SDL_strlcat(szPath, "StarDat.mpq", sizeof szPath);
  if (GetFileAttributesA(szPath) != INVALID_FILE_ATTRIBUTES)
    return PORT_SOURCE_MPQ;
  return PortCascAvailable() ? PORT_SOURCE_REMASTERED : PORT_SOURCE_MPQ;
}

int PortGameSource(void)
{
  static const int s_nSource = SourceDecide();
  return s_nSource;
}

bool PortRemasteredLocalDll(char *pszPath, size_t cchPath)
{
  if (GetFileAttributesA(pszPath) != INVALID_FILE_ATTRIBUTES || !PortCascAvailable())
    return false;
  const char *apszLocale[2];
  const int nLocales = Locales(apszLocale);
  for (int i = 0; i < nLocales; ++i) {
    char szDll[MAX_PATH], szName[64];
    SDL_snprintf(szDll, sizeof szDll, "%s\\locales\\%s\\Local.dll", PortCascFolder(), apszLocale[i]);
    SDL_snprintf(szName, sizeof szName, "locales\\%s\\Local.dll", apszLocale[i]);
    // a test's install in another language has no other language's folder (PortCascHidden)
    if (GetFileAttributesA(szDll) == INVALID_FILE_ATTRIBUTES || PortCascHidden(szName))
      continue;
    SDL_strlcpy(pszPath, szDll, cchPath);
    return true;
  }
  return false;
}

// The files that are 1.17's bytes under another name.
struct RemasteredMoved {
  const char *psz117;
  const char *pszRemastered;
};

static const RemasteredMoved kMoved[] = {
    {"unit\\cmdbtns\\cmdicons.grp", "unit\\cmdicons\\cmdicons.grp"},
    {"unit\\cmdbtns\\ticon.pcx", "game\\ticon.pcx"},
    {"unit\\cmdbtns\\pcmdbtns.grp", "game\\consoles\\protoss\\cmdbtns.grp"},
    {"unit\\cmdbtns\\tcmdbtns.grp", "game\\consoles\\terran\\cmdbtns.grp"},
    {"unit\\cmdbtns\\zcmdbtns.grp", "game\\consoles\\zerg\\cmdbtns.grp"},
    {"dlgs\\protoss.grp", "game\\consoles\\protoss\\dlg.grp"},
    {"dlgs\\terran.grp", "game\\consoles\\terran\\dlg.grp"},
    {"dlgs\\zerg.grp", "game\\consoles\\zerg\\dlg.grp"},
    {"parallax\\star.spk", "SD\\TileSet\\platform.spk"},
};

// The stand-ins for glue\Pal<xx>\<name>, with a Remastered install alone, should GlueGrp not
// make it.
static const RemasteredMoved kStandIn[] = {
    {"arrow.grp", "cursor\\arrow.grp"},
    {"Dlg.grp", "game\\consoles\\terran\\dlg.grp"},
    {"tile.grp", "dlgs\\tile.grp"},
};

// stat_txt.tbl's strings past Remastered's 1,539: the replay screen's, as 1.17's bytes. A
// button's runs on past a NUL: its key, 0 for a tooltip with no cost line, then the text.
struct StatTxtString {
  const char *p;
  size_t n;
};
#define STAT_TXT_STRING(s) {s, sizeof s - 1}
static const StatTxtString kStatTxtTail[] = {
    STAT_TXT_STRING("View Players"),
    STAT_TXT_STRING("u\0Speed \x03" "U\x01" "p"),
    STAT_TXT_STRING("p\0\x03" "P\x01" "lay"),
    STAT_TXT_STRING("p\0\x03" "P\x01" "ause"),
    STAT_TXT_STRING("d\0Slow \x03" "D\x01" "own"),
    STAT_TXT_STRING("Replay Progress"),
    STAT_TXT_STRING("Paused"),
    STAT_TXT_STRING("Unlimited"),
};
#undef STAT_TXT_STRING
static const unsigned kStatTxtCount = 1539;

static bool NameIs(const char *pszName, const char *pszWant)
{
  for (; *pszName && *pszWant; ++pszName, ++pszWant) {
    const char a = *pszName == '/' ? '\\' : *pszName, b = *pszWant;
    if (SDL_tolower(static_cast<unsigned char>(a)) != SDL_tolower(static_cast<unsigned char>(b)))
      return false;
  }
  return *pszName == *pszWant;
}

static bool NameEndsWith(const char *pszName, const char *pszEnd)
{
  const size_t n = SDL_strlen(pszName), m = SDL_strlen(pszEnd);
  return n >= m && SDL_strcasecmp(pszName + n - m, pszEnd) == 0;
}

// The language a file is read in: a sound, a briefing's too, in the classic speech's
// (PortLanguageSpeech), the rest in the game text's (PortLanguageGame).
static const char *LanguageOf(const char *pszName)
{
  if (SDL_strncasecmp(pszName, "sound\\", 6) == 0 || NameEndsWith(pszName, ".wav") || NameEndsWith(pszName, ".ogg"))
    return PortLanguageSpeech();
  return PortLanguageGame();
}

// The name in English, as the classic game has it: under SD\, as it is, then in the locales'
// folders. The game types are English's alone, else the classic ones (PortLocalTemplate), so
// that they agree with the list that names them, which every install has.
static void *FindEnglish(const char *pszName, size_t *pnSize)
{
  char szName[MAX_PATH];
  static const char *const kForms[] = {"SD\\%s", "%s", "locales\\%s\\Assets\\SD\\%s", "locales\\%s\\Assets\\%s"};
  const char *apszLocale[2];
  const int nLocales = SDL_strncasecmp(pszName, "Templates\\", 10) == 0 ? 1 : Locales(apszLocale);
  apszLocale[0] = "enUS";
  for (size_t i = 0; i < SDL_arraysize(kForms); ++i)
    for (int k = 0; k < (i < 2 ? 1 : nLocales); ++k) {
      if (i < 2)
        SDL_snprintf(szName, sizeof szName, kForms[i], pszName);
      else
        SDL_snprintf(szName, sizeof szName, kForms[i], apszLocale[k], pszName);
      void *p = PortCascRead(szName, pnSize);
      if (p != NULL)
        return p;
    }
  return NULL;
}

// The name in the language it is read in, when that is not English: its locale's copy under SD\,
// where the four languages with the classic campaign and speech translated keep those, and for
// rez\ (the credit pages) its locale's own; else English's. Only SD\ has the classic campaign:
// a locale's own campaign is Remastered's, which differs. The game types are English's
// (LangTemplate names them).
static void *Find(const char *pszName, size_t *pnSize)
{
  const char *pszLocale = LanguageOf(pszName);
  if (SDL_strncasecmp(pszName, "Templates\\", 10) != 0 && SDL_strcmp(pszLocale, "enUS") != 0) {
    static const char *const kForms[] = {"locales\\%s\\Assets\\SD\\%s", "locales\\%s\\Assets\\%s"};
    const int nForms = SDL_strncasecmp(pszName, "rez\\", 4) == 0 ? 2 : 1;
    for (int i = 0; i < nForms; ++i) {
      char szName[MAX_PATH];
      SDL_snprintf(szName, sizeof szName, kForms[i], pszLocale, pszName);
      if (void *p = PortCascRead(szName, pnSize))
        return p;
    }
  }
  return FindEnglish(pszName, pnSize);
}

static void *Copy(const std::string &s, unsigned long *pcbSize)
{
  void *p = SDL_malloc(s.size() ? s.size() : 1);
  if (p != NULL) {
    memcpy(p, s.data(), s.size());
    *pcbSize = static_cast<unsigned long>(s.size());
  }
  return p;
}

// stat_txt.tbl with the replay screen's strings after Remastered's: its bytes as they are, each
// offset moved past the longer index, since a string read up to its first NUL would lose a
// button's text (kStatTxtTail).
static void *StatTxt(unsigned char *p, size_t cb, unsigned long *pcbSize)
{
  const unsigned n = cb >= 2 ? p[0] | p[1] << 8 : 0;
  const size_t nAll = kStatTxtCount + SDL_arraysize(kStatTxtTail);
  *pcbSize = static_cast<unsigned long>(cb);
  if (n < kStatTxtCount || n >= nAll || cb < 2 + 2 * n)
    return p;
  const size_t nShift = 2 * (nAll - n);
  std::string out(2 + 2 * nAll, '\0');
  const auto put = [&out](size_t at, size_t v) {
    out[at] = static_cast<char>(v & 0xFF);
    out[at + 1] = static_cast<char>(v >> 8 & 0xFF);
  };
  put(0, nAll);
  for (unsigned i = 0; i < n; ++i)
    put(2 + 2 * i, (p[2 + 2 * i] | p[3 + 2 * i] << 8) + nShift);
  out.append(reinterpret_cast<const char *>(p) + 2 + 2 * n, cb - (2 + 2 * n));
  for (size_t i = n; i < nAll; ++i) {
    put(2 + 2 * i, out.size());
    out.append(kStatTxtTail[i - kStatTxtCount].p, kStatTxtTail[i - kStatTxtCount].n);
    out += '\0';
  }
  if (out.size() > 0xFFFF)
    return p;
  SDL_free(p);
  return Copy(out, pcbSize);
}

// A .tbl of the strings: a 16-bit count and offsets, as the classic's, or for a table past
// 64 KB (a translation's) the wide form str_get_string reads, 0xFFFF, then a 32-bit count and
// 32-bit offsets.
static std::string TblMake(const std::vector<std::string> &strings)
{
  size_t cb = 2 + 2 * strings.size();
  for (const std::string &s : strings)
    cb += s.size() + 1;
  const bool bWide = cb > 0xFFFF;
  const size_t nHead = bWide ? 6 : 2, nOffset = bWide ? 4 : 2;
  std::string out(nHead + nOffset * strings.size(), '\0');
  const auto put = [&out](size_t at, size_t v, size_t n) {
    for (size_t k = 0; k < n; ++k)
      out[at + k] = static_cast<char>(v >> (8 * k) & 0xFF);
  };
  if (bWide) {
    put(0, 0xFFFF, 2);
    put(2, strings.size(), 4);
  } else {
    put(0, strings.size(), 2);
  }
  for (size_t i = 0; i < strings.size(); ++i) {
    put(nHead + nOffset * i, out.size(), nOffset);
    out += strings[i];
    out += '\0';
  }
  return out;
}

// A classic .tbl's strings, each up to the start of the one after it (a button's runs on past a
// NUL), less its own NUL.
static std::vector<std::string> TblStrings(const unsigned char *p, size_t cb)
{
  std::vector<std::string> out;
  const size_t n = cb >= 2 ? (p[0] | p[1] << 8) : 0;
  if (cb < 2 + 2 * n)
    return out;
  std::vector<size_t> starts, sorted;
  for (size_t i = 0; i < n; ++i)
    starts.push_back(p[2 + 2 * i] | p[3 + 2 * i] << 8);
  sorted = starts;
  std::sort(sorted.begin(), sorted.end());
  for (size_t i = 0; i < n; ++i) {
    const size_t at = starts[i] < cb ? starts[i] : cb;
    const auto next = std::upper_bound(sorted.begin(), sorted.end(), at);
    size_t end = next != sorted.end() && *next <= cb ? *next : cb;
    if (end > at && p[end - 1] == 0)
      --end;
    out.emplace_back(reinterpret_cast<const char *>(p) + at, end - at);
  }
  return out;
}

// XML text as 1.17's tables have it: entities resolved, CR LF as LF, and UTF-8 as the game's
// single bytes (a character past them as '?').
static std::string XmlText(const char *p, size_t n)
{
  std::string s;
  for (size_t i = 0; i < n;) {
    unsigned c = static_cast<unsigned char>(p[i]);
    if (c == '&') {
      const char *pEnd = static_cast<const char *>(memchr(p + i, ';', n - i));
      if (pEnd != NULL) {
        const std::string e(p + i + 1, pEnd - (p + i + 1));
        unsigned v = 0;
        if (e.size() > 1 && e[0] == '#')
          v = static_cast<unsigned>(e[1] == 'x' ? SDL_strtoul(e.c_str() + 2, NULL, 16) : SDL_strtoul(e.c_str() + 1, NULL, 10));
        else
          v = e == "lt" ? '<' : e == "gt" ? '>' : e == "amp" ? '&' : e == "quot" ? '"' : e == "apos" ? '\'' : 0;
        if (v != 0) {
          s += static_cast<char>(v < 256 ? v : '?');
          i = pEnd - p + 1;
          continue;
        }
      }
    }
    if (c == '\r' && i + 1 < n && p[i + 1] == '\n') {
      ++i;
      continue;
    }
    if (c >= 0xC0) {
      const int nMore = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
      c &= 0x3F >> nMore;
      for (int k = 0; k < nMore && i + 1 < n; ++k)
        c = c << 6 | (static_cast<unsigned char>(p[++i]) & 0x3F);
      s += static_cast<char>(c < 256 ? c : '?');
      ++i;
      continue;
    }
    s += static_cast<char>(c);
    ++i;
  }
  return s;
}

// ---------------------------------------------------------------------------
// The game's text in another language (PortLanguageGame). Remastered keeps each language's
// text in its locale's rez\<table>.json, an array of {"id", "Key", "Value"}, which the tables,
// the dialogs' text and the game types' names are made from; English's stands in for what a
// language lacks.

// A value of those files as the game's bytes. Remastered escapes the text a second time inside
// the JSON string ("\\u0003", "\\n"); this undoes that, each character past ASCII as UTF-8,
// which the game's text drawing reads (MapTextChar), or with bLatin1 as one byte ('?' past 255).
static std::string JsonText(const std::string &v, bool bLatin1 = false)
{
  static const char kFrom[] = "nrt\\\"/", kTo[] = "\n\r\t\\\"/";
  std::string s;
  for (size_t i = 0; i < v.size();) {
    unsigned c = static_cast<unsigned char>(v[i]);
    size_t n = 1;
    if (c == '\\' && i + 1 < v.size()) {
      const char *pEscape = v[i + 1] != 0 ? SDL_strchr(kFrom, v[i + 1]) : NULL;
      if (v[i + 1] == 'u' && i + 6 <= v.size()) {
        c = static_cast<unsigned>(SDL_strtoul(v.substr(i + 2, 4).c_str(), NULL, 16));
        n = 6;
      } else if (pEscape != NULL) {
        c = static_cast<unsigned char>(kTo[pEscape - kFrom]);
        n = 2;
      }
    } else if (c >= 0xC0) {
      const size_t nMore = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
      c &= 0x3F >> nMore;
      for (n = 1; n <= nMore && i + n < v.size(); ++n)
        c = c << 6 | (static_cast<unsigned char>(v[i + n]) & 0x3F);
    }
    i += n;
    if (bLatin1 || c < 0x80) {
      s += static_cast<char>(c < 256 ? c : '?');
    } else if (c < 0x800) {
      s += static_cast<char>(0xC0 | c >> 6);
      s += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      s += static_cast<char>(0xE0 | c >> 12);
      s += static_cast<char>(0x80 | (c >> 6 & 0x3F));
      s += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      s += static_cast<char>(0xF0 | c >> 18);
      s += static_cast<char>(0x80 | (c >> 12 & 0x3F));
      s += static_cast<char>(0x80 | (c >> 6 & 0x3F));
      s += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  return s;
}

// The entries of a locale's rez\<pszFile>.json in their order, as key and text (JsonText), the
// ones Remastered left as a placeholder empty; none when the locale has no such file.
static std::vector<std::pair<std::string, std::string>> LangEntries(const char *pszLocale, const char *pszFile,
                                                                    bool bLatin1 = false)
{
  std::vector<std::pair<std::string, std::string>> out;
  char szName[MAX_PATH];
  SDL_snprintf(szName, sizeof szName, "locales\\%s\\Assets\\rez\\%s.json", pszLocale, pszFile);
  size_t cb = 0;
  char *p = static_cast<char *>(PortCascRead(szName, &cb));
  if (p == NULL)
    return out;
  std::string text(p, cb);
  SDL_free(p);
  if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
    text.erase(0, 3);
  PortJson json;
  if (!PortJsonParse(text.c_str(), &json))
    return out;
  for (size_t i = 0; i < json.size(); ++i) {
    const std::string value = json.At(i)["Value"].Str();
    out.emplace_back(json.At(i)["Key"].Str(), value == "<<PLACEHOLDER>>" ? std::string() : JsonText(value, bLatin1));
  }
  return out;
}

static bool LangOther(void)
{
  return SDL_strcmp(PortLanguageGame(), "enUS") != 0;
}

// stat_txt.tbl in the language from its stat_txt.json, string for string with English's
// (pEnglish, StatTxt's): both have the classic's 1,539 strings, then the replay screen's 8. A
// string the language leaves empty stays English's. A button keeps the classic's form, its key
// and then its tooltip's kind, which Remastered's text writes as 7 where the classic has 0:
// English's kind, and English's whole string when the translation is not in that form or its key
// is not English's or an ASCII letter or digit.
static void *LangStatTxt(void *pEnglish, unsigned long cbEnglish, unsigned long *pcbSize)
{
  const auto entries = LangEntries(PortLanguageGame(), "stat_txt");
  std::vector<std::string> strings = TblStrings(static_cast<const unsigned char *>(pEnglish), cbEnglish);
  if (entries.empty() || strings.empty())
    return pEnglish;
  for (size_t i = 0; i < strings.size() && i < entries.size(); ++i) {
    std::string s = entries[i].second;
    const std::string &en = strings[i];
    if (s.empty())
      continue;
    if (en.size() >= 2 && static_cast<unsigned char>(en[1]) < 8) {
      const unsigned char k = static_cast<unsigned char>(s[0]);
      if (s.size() < 2 || static_cast<unsigned char>(s[1]) >= 8 || (s[0] != en[0] && (k >= 0x80 || !SDL_isalnum(k))))
        continue;
      s[1] = en[1];
    }
    // Consume's requirement indents its line as [5], the classic's seven spaces
    for (size_t at; (at = s.find("[5]")) != std::string::npos;)
      s.replace(at, 3, "       ");
    strings[i] = s;
  }
  SDL_free(pEnglish);
  return Copy(TblMake(strings), pcbSize);
}

static std::mutex g_langLock;  // the dialogs' and game types' text, made once

// A dialog's layout with its controls' text in the language, from rc3.json, which keys each
// dialog's text "<dialog>-<n>". Remastered numbers its dialogs' controls its own way, so a control
// takes the text whose English (English's rc3.json) is its own, in its dialog, else in any; the
// others keep English's.
static void *LangLayout(const char *pszName, unsigned long *pcbSize)
{
  const PortLayout *pLayout = PortLayoutFind(pszName);
  if (pLayout == NULL)
    return NULL;
  std::lock_guard<std::mutex> hold(g_langLock);
  // by dialog, its entries' English and the language's text; then the language's by English's
  static std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> s_dialogs;
  static std::unordered_map<std::string, std::string> s_any;
  static bool s_bMade;
  if (!s_bMade) {
    s_bMade = true;
    std::unordered_map<std::string, std::string> byKey;
    for (auto &e : LangEntries(PortLanguageGame(), "rc3"))
      if (!e.second.empty())
        byKey[e.first] = e.second;
    for (auto &e : LangEntries("enUS", "rc3", true)) {
      const auto it = byKey.find(e.first);
      if (it == byKey.end() || e.second.empty())
        continue;
      std::string dialog = e.first.substr(0, e.first.rfind('-'));
      for (char &c : dialog)
        c = static_cast<char>(SDL_tolower(static_cast<unsigned char>(c)));
      s_dialogs[dialog].emplace_back(e.second, it->second);
      s_any.emplace(e.second, it->second);
    }
  }
  std::string dialog = pszName + 4;
  dialog.erase(dialog.size() > 4 ? dialog.size() - 4 : 0);
  for (char &c : dialog)
    c = static_cast<char>(SDL_tolower(static_cast<unsigned char>(c)));
  const auto itDialog = s_dialogs.find(dialog);
  std::vector<PortLayoutCtrl> ctrls(pLayout->pCtrls, pLayout->pCtrls + pLayout->nCtrls);
  for (PortLayoutCtrl &c : ctrls) {
    if (c.pszText == NULL)
      continue;
    const std::string *pText = NULL;
    if (itDialog != s_dialogs.end())
      for (const auto &e : itDialog->second)
        if (e.first == c.pszText) {
          pText = &e.second;
          break;
        }
    const auto itAny = pText == NULL ? s_any.find(c.pszText) : s_any.end();
    if (itAny != s_any.end())
      pText = &itAny->second;
    if (pText != NULL)
      c.pszText = pText->c_str();
  }
  PortLayout layout = *pLayout;
  layout.pCtrls = ctrls.data();
  return PortLayoutMake(&layout, pcbSize);
}

// A game type's names, a .got's own and its setting's (32 bytes each from +1), in the language
// by English's gametype.json, cut at a character to fit.
static void LangTemplate(void *p, unsigned long cb)
{
  if (cb < 0x41)
    return;
  std::lock_guard<std::mutex> hold(g_langLock);
  static std::unordered_map<std::string, std::string> s_names;
  static bool s_bMade;
  if (!s_bMade) {
    s_bMade = true;
    std::unordered_map<std::string, std::string> byKey;
    for (auto &e : LangEntries(PortLanguageGame(), "gametype"))
      if (!e.second.empty())
        byKey[e.first] = e.second;
    for (auto &e : LangEntries("enUS", "gametype", true)) {
      const auto it = byKey.find(e.first);
      if (it != byKey.end() && !e.second.empty())
        s_names.emplace(e.second, it->second);
    }
  }
  char *psz = static_cast<char *>(p);
  for (const size_t at : {static_cast<size_t>(1), static_cast<size_t>(0x21)}) {
    const auto it = s_names.find(std::string(psz + at, SDL_strnlen(psz + at, 0x20)));
    if (it == s_names.end())
      continue;
    const std::string &s = it->second;
    size_t n = s.size() < 0x1F ? s.size() : 0x1F;
    while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80)
      --n;
    memset(psz + at, 0, 0x20);
    memcpy(psz + at, s.data(), n);
  }
}

// One of 1.17's tables of keyed strings (gluAll, network, tips, gluHist, help_txt) from English's
// XML, and in another language from its pszJson's text by the same keys over English's (a help
// page's key, and a few of network's, has a '-' where the XML's has its last '_').
static void *KeyedTable(const char *pszXml, const char *pszJson, const PortRemasteredKey *pKeys, size_t nKeys,
                        unsigned long *pcbSize)
{
  std::unordered_map<std::string, std::string> byLang;
  if (LangOther())
    for (auto &e : LangEntries(PortLanguageGame(), pszJson))
      if (!e.second.empty())
        byLang[e.first] = e.second;
  size_t cb = 0;
  char *pText = static_cast<char *>(FindEnglish(pszXml, &cb));
  if (pText == NULL)
    return NULL;
  std::unordered_map<std::string, std::string> byKey;
  const char *p = pText, *pEnd = pText + cb;
  for (;;) {
    const char *pId = SDL_strnstr(p, "<id>", pEnd - p);
    if (pId == NULL)
      break;
    pId += 4;
    const char *pIdEnd = SDL_strnstr(pId, "</id>", pEnd - pId);
    const char *pValue = pIdEnd ? SDL_strnstr(pIdEnd, "<value>", pEnd - pIdEnd) : NULL;
    const char *pValueEnd = pValue ? SDL_strnstr(pValue, "</value>", pEnd - pValue) : NULL;
    if (pValueEnd == NULL)
      break;
    pValue += 7;
    byKey[std::string(pId, pIdEnd - pId)] = XmlText(pValue, pValueEnd - pValue);
    p = pValueEnd + 8;
  }
  SDL_free(pText);
  std::vector<std::string> strings;
  for (size_t i = 0; i < nKeys; ++i) {
    std::string s;
    if (pKeys[i].pszKey == NULL) {
      s = pKeys[i].pszOwn;
    } else {
      std::string key = pKeys[i].pszKey;
      auto itLang = byLang.find(key);
      const size_t nAt = key.rfind('_');
      if (itLang == byLang.end() && nAt != std::string::npos) {
        key[nAt] = '-';
        itLang = byLang.find(key);
      }
      const auto it = byKey.find(pKeys[i].pszKey);
      if (itLang != byLang.end())
        s = itLang->second;
      else if (it != byKey.end())
        s = it->second;
      if (pKeys[i].pszTail != NULL)
        s += pKeys[i].pszTail;
    }
    strings.push_back(s);
  }
  return Copy(TblMake(strings), pcbSize);
}

// ---------------------------------------------------------------------------
// 8-bit pictures from Remastered's DDS.

static unsigned U16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned U32(const unsigned char *p) { return U16(p) | U16(p + 2) << 16; }

// A colour of 5, 6 and 5 bits as RGB.
static void Rgb565(unsigned c, unsigned char *pRgb)
{
  pRgb[0] = static_cast<unsigned char>((c >> 11 & 31) * 255 / 31);
  pRgb[1] = static_cast<unsigned char>((c >> 5 & 63) * 255 / 63);
  pRgb[2] = static_cast<unsigned char>((c & 31) * 255 / 31);
}

// One 4 x 4 block of DXT1 colour (bDxt1: with its three-colour mode) into pOut, a pitch of
// nPitch pixels, its alpha left alone unless the block makes a pixel clear.
static void DxtColourBlock(const unsigned char *p, bool bDxt1, unsigned char *pOut, int nPitch)
{
  const unsigned c0 = U16(p), c1 = U16(p + 2);
  unsigned char aColour[4][4];
  Rgb565(c0, aColour[0]);
  Rgb565(c1, aColour[1]);
  aColour[0][3] = aColour[1][3] = aColour[2][3] = aColour[3][3] = 255;
  for (int k = 0; k < 3; ++k) {
    if (c0 > c1 || !bDxt1) {
      aColour[2][k] = static_cast<unsigned char>((2 * aColour[0][k] + aColour[1][k]) / 3);
      aColour[3][k] = static_cast<unsigned char>((aColour[0][k] + 2 * aColour[1][k]) / 3);
    } else {
      aColour[2][k] = static_cast<unsigned char>((aColour[0][k] + aColour[1][k]) / 2);
      aColour[3][k] = 0;
    }
  }
  if (bDxt1 && c0 <= c1)
    aColour[3][3] = 0;
  const unsigned nIndices = U32(p + 4);
  for (int i = 0; i < 16; ++i) {
    unsigned char *o = pOut + 4 * ((i / 4) * nPitch + i % 4);
    const unsigned char *c = aColour[nIndices >> (2 * i) & 3];
    o[0] = c[0];
    o[1] = c[1];
    o[2] = c[2];
    if (bDxt1)
      o[3] = c[3];
  }
}

// One 4 x 4 block of DXT5 alpha.
static void DxtAlphaBlock(const unsigned char *p, unsigned char *pOut, int nPitch)
{
  int aAlpha[8] = {p[0], p[1]};
  for (int i = 2; i < 8; ++i)
    aAlpha[i] = aAlpha[0] > aAlpha[1] ? ((8 - i) * aAlpha[0] + (i - 1) * aAlpha[1]) / 7
                : i < 6               ? ((6 - i) * aAlpha[0] + (i - 1) * aAlpha[1]) / 5
                : i == 6              ? 0
                                      : 255;
  unsigned long long nBits = 0;
  for (int i = 0; i < 6; ++i)
    nBits |= static_cast<unsigned long long>(p[2 + i]) << (8 * i);
  for (int i = 0; i < 16; ++i)
    pOut[4 * ((i / 4) * nPitch + i % 4) + 3] = static_cast<unsigned char>(aAlpha[nBits >> (3 * i) & 7]);
}

// A DDS's pixels as RGBA, for its three kinds in Remastered's SD folder: 32 bits a pixel with
// channel masks, DXT1 and DXT5.
static bool DdsToRgba(const unsigned char *p, size_t cb, int *pnWidth, int *pnHeight, std::vector<unsigned char> &rgba)
{
  if (cb < 128 || memcmp(p, "DDS ", 4) != 0)
    return false;
  const int nHeight = static_cast<int>(U32(p + 12)), nWidth = static_cast<int>(U32(p + 16));
  if (nWidth <= 0 || nHeight <= 0 || nWidth > 4096 || nHeight > 4096)
    return false;
  const unsigned nFlags = U32(p + 80);
  const unsigned char *pData = p + 128;
  const size_t cbData = cb - 128;
  rgba.assign(static_cast<size_t>(nWidth) * nHeight * 4, 255);
  if (nFlags & 4) {
    const bool bDxt1 = memcmp(p + 84, "DXT1", 4) == 0, bDxt5 = memcmp(p + 84, "DXT5", 4) == 0;
    if (!bDxt1 && !bDxt5)
      return false;
    const int nBlocksX = (nWidth + 3) / 4, nBlocksY = (nHeight + 3) / 4, cbBlock = bDxt1 ? 8 : 16;
    if (cbData < static_cast<size_t>(nBlocksX) * nBlocksY * cbBlock)
      return false;
    // decoded into a canvas of whole blocks, then cut to the picture
    const int nPitch = nBlocksX * 4;
    std::vector<unsigned char> blocks(static_cast<size_t>(nPitch) * nBlocksY * 4 * 4, 255);
    for (int by = 0; by < nBlocksY; ++by)
      for (int bx = 0; bx < nBlocksX; ++bx) {
        const unsigned char *b = pData + (static_cast<size_t>(by) * nBlocksX + bx) * cbBlock;
        unsigned char *o = &blocks[(static_cast<size_t>(by) * 4 * nPitch + bx * 4) * 4];
        if (bDxt5)
          DxtAlphaBlock(b, o, nPitch);
        DxtColourBlock(bDxt5 ? b + 8 : b, bDxt1, o, nPitch);
      }
    for (int y = 0; y < nHeight; ++y)
      memcpy(&rgba[static_cast<size_t>(y) * nWidth * 4], &blocks[static_cast<size_t>(y) * nPitch * 4],
             static_cast<size_t>(nWidth) * 4);
  } else {
    if (U32(p + 88) != 32 || cbData < static_cast<size_t>(nWidth) * nHeight * 4)
      return false;
    unsigned aMask[4] = {U32(p + 92), U32(p + 96), U32(p + 100), (nFlags & 1) ? U32(p + 104) : 0};
    for (size_t i = 0; i < static_cast<size_t>(nWidth) * nHeight; ++i) {
      const unsigned v = U32(pData + 4 * i);
      for (int k = 0; k < 4; ++k) {
        if (aMask[k] == 0)
          continue;
        unsigned m = aMask[k], s = 0;
        while (!(m & 1)) {
          m >>= 1;
          ++s;
        }
        rgba[4 * i + k] = static_cast<unsigned char>((v >> s & m) * 255 / m);
      }
    }
  }
  *pnWidth = nWidth;
  *pnHeight = nHeight;
  return true;
}

// The game's palette as a picture drawn without a tileset sees it: the entries whose colour
// is the same in all eight tilesets (188 of them), the rest marked unusable.
struct RemasteredPalette {
  unsigned char abRgb[768];
  bool abShared[256];
};

static bool SharedPalette(RemasteredPalette *pPal)
{
  static const char *const kTilesets[] = {"badlands", "platform", "install", "AshWorld",
                                          "Jungle",   "Desert",   "Ice",     "Twilight"};
  for (int i = 0; i < 256; ++i)
    pPal->abShared[i] = true;
  for (size_t t = 0; t < SDL_arraysize(kTilesets); ++t) {
    char szName[64];
    SDL_snprintf(szName, sizeof szName, "tileset\\%s.wpe", kTilesets[t]);
    size_t cb = 0;
    unsigned char *p = static_cast<unsigned char *>(Find(szName, &cb));
    if (p == NULL || cb < 1024) {
      SDL_free(p);
      return false;
    }
    for (int i = 0; i < 256; ++i) {
      if (t == 0)
        memcpy(pPal->abRgb + 3 * i, p + 4 * i, 3);
      else if (memcmp(pPal->abRgb + 3 * i, p + 4 * i, 3) != 0)
        pPal->abShared[i] = false;
    }
    SDL_free(p);
  }
  return true;
}

// The shared entry nearest a colour. Remastered paints in (35, 35, 255) the few console
// pixels 1.17 draws in entry 200, whose colour changes with the tileset; they get 200 back.
static unsigned char SharedNearest(const RemasteredPalette &pal, const unsigned char *pRgb)
{
  if (pRgb[0] == 35 && pRgb[1] == 35 && pRgb[2] == 255)
    return 200;
  int nBest = 0, nBestD = 0x7FFFFFFF;
  for (int i = 1; i < 256; ++i) {
    if (!pal.abShared[i])
      continue;
    const int dr = pRgb[0] - pal.abRgb[3 * i], dg = pRgb[1] - pal.abRgb[3 * i + 1], db = pRgb[2] - pal.abRgb[3 * i + 2];
    const int d = dr * dr + dg * dg + db * db;
    if (d < nBestD) {
      nBestD = d;
      nBest = i;
    }
  }
  return static_cast<unsigned char>(nBest);
}

// An 8-bit PCX of nWidth x nHeight with the palette after it.
static std::string PcxMake(const unsigned char *pIndices, int nWidth, int nHeight, const unsigned char *pRgb)
{
  std::string s(128, '\0');
  s[0] = 10;
  s[1] = 5;
  s[2] = 1;
  s[3] = 8;
  const unsigned aWord[] = {0, 0, static_cast<unsigned>(nWidth - 1), static_cast<unsigned>(nHeight - 1), 72, 72};
  for (int i = 0; i < 6; ++i) {
    s[4 + 2 * i] = static_cast<char>(aWord[i] & 0xFF);
    s[5 + 2 * i] = static_cast<char>(aWord[i] >> 8);
  }
  s[65] = 1;
  s[66] = static_cast<char>(nWidth & 0xFF);
  s[67] = static_cast<char>(nWidth >> 8);
  s[68] = 1;
  for (int y = 0; y < nHeight; ++y) {
    const unsigned char *r = pIndices + static_cast<size_t>(y) * nWidth;
    for (int x = 0; x < nWidth;) {
      int n = 1;
      while (x + n < nWidth && n < 63 && r[x + n] == r[x])
        ++n;
      if (n > 1 || r[x] >= 0xC0)
        s += static_cast<char>(0xC0 | n);
      s += static_cast<char>(r[x]);
      x += n;
    }
  }
  s += static_cast<char>(12);
  s.append(reinterpret_cast<const char *>(pRgb), 768);
  return s;
}

// game\<r>console.pcx and its pieces, with a Remastered install alone: SD\game\'s DDS of the
// same name in the shared colours, clear where its alpha is under half. The console is
// Remastered's strip at the foot of a 640 x 480 picture, which gives 1.17's back in every
// pixel; the portrait's frame and the empty bar are DXT and come near it, scaled to 1.17's size.
// The full bar: 1.17's is the empty bar with the green segments in rows 3 to 5, and 1.17 draws
// it over the empty one up to the progress; Remastered's holds the segments alone, in the same
// rows of a taller picture. So it is the empty bar with those rows laid over it, not scaled (the
// user, 2026-10-26: scaled, the filled part was a thin dotted line with no frame round it).
static void *DdsPicture(const char *pszName, unsigned long *pcbSize)
{
  enum { FIT_SCALE, FIT_FOOT, FIT_TOP };
  static const struct {
    const char *pszKind;
    int nWidth, nHeight;
    int nFit;
    const char *pszUnder;  // the picture drawn first, showing where this one is clear
  } kPictures[] = {
      {"console", 640, 480, FIT_FOOT, NULL},
      {"conover", 60, 56, FIT_SCALE, NULL},
      {"pbrempt", 108, 9, FIT_SCALE, NULL},
      {"pbrfull", 108, 9, FIT_TOP, "pbrempt"},
  };
  if (SDL_strlen(pszName) < 10 || SDL_strncasecmp(pszName, "game\\", 5) != 0 || !SDL_strchr("tpzn", pszName[5]) ||
      !NameEndsWith(pszName, ".pcx"))
    return NULL;
  for (size_t k = 0; k < SDL_arraysize(kPictures); ++k) {
    char szWant[32];
    SDL_snprintf(szWant, sizeof szWant, "game\\%c%s.pcx", pszName[5], kPictures[k].pszKind);
    if (!NameIs(pszName, szWant))
      continue;
    RemasteredPalette pal;
    if (!SharedPalette(&pal))
      return NULL;
    const int nW = kPictures[k].nWidth, nH = kPictures[k].nHeight;
    std::vector<unsigned char> indices(static_cast<size_t>(nW) * nH, 0);
    // a DDS's pixels into the picture, fitted as nFit says; false when it cannot be read
    const auto Lay = [&](const char *pszKind, int nFit) {
      char szDds[64];
      SDL_snprintf(szDds, sizeof szDds, "game\\%c%s.DDS", pszName[5], pszKind);
      size_t cb = 0;
      unsigned char *p = static_cast<unsigned char *>(Find(szDds, &cb));
      std::vector<unsigned char> rgba;
      int w = 0, h = 0;
      const bool bRead = p && DdsToRgba(p, cb, &w, &h, rgba);
      SDL_free(p);
      if (!bRead)
        return false;
      for (int y = 0; y < nH; ++y)
        for (int x = 0; x < nW; ++x) {
          int sx = x, sy = y;
          if (nFit == FIT_FOOT) {
            sy = y - (nH - h);
          } else if (nFit == FIT_SCALE) {
            sx = x * w / nW;
            sy = y * h / nH;
          }
          if (sx < 0 || sy < 0 || sx >= w || sy >= h)
            continue;
          const unsigned char *c = &rgba[(static_cast<size_t>(sy) * w + sx) * 4];
          if (c[3] >= 128)
            indices[static_cast<size_t>(y) * nW + x] = SharedNearest(pal, c);
        }
      return true;
    };
    if (kPictures[k].pszUnder != NULL && !Lay(kPictures[k].pszUnder, FIT_SCALE))
      return NULL;
    if (!Lay(kPictures[k].pszKind, kPictures[k].nFit))
      return NULL;
    return Copy(PcxMake(indices.data(), nW, nH, pal.abRgb), pcbSize);
  }
  return NULL;
}

// The palette of the glue screen that draws glue\<folder>\<file>, from the end of that screen's
// tFont.pcx (Remastered keeps 1.17's, whose palette is the screen's, the same 256 entries as
// its BackGnd.pcx). Each glue\Pal<xx> is a screen; glue\Score<xx> is drawn in Pal<xx>'s
// colours, glue\Ready<r> in PalR<r>'s, the campaign's in PalCs's, the main menu's in PalMm's, the
// title in its own, and the rest (Battle.net's and the multiplayer screens) in PalNl's: checked
// against 1.17's pictures, each one's colours at its entries.
static bool GluePalette(const char *pszName, unsigned char *pRgb)
{
  if (SDL_strncasecmp(pszName, "glue\\", 5) != 0)
    return false;
  const char *pszFolder = pszName + 5, *pszEnd = SDL_strchr(pszFolder, '\\');
  if (pszEnd == NULL)
    return false;
  const std::string folder(pszFolder, pszEnd);
  std::string screen = "PalNl";
  if (folder.size() > 3 && SDL_strncasecmp(folder.c_str(), "pal", 3) == 0)
    screen = folder;
  else if (folder.size() == 7 && SDL_strncasecmp(folder.c_str(), "score", 5) == 0)
    screen = "Pal" + folder.substr(5);
  else if (folder.size() == 6 && SDL_strncasecmp(folder.c_str(), "ready", 5) == 0)
    screen = std::string("PalR") + folder[5];
  else if (SDL_strcasecmp(folder.c_str(), "campaign") == 0)
    screen = "PalCs";
  else if (SDL_strcasecmp(folder.c_str(), "mainmenu") == 0)
    screen = "PalMm";
  else if (SDL_strcasecmp(folder.c_str(), "title") == 0)
    screen = "title";
  char szFont[MAX_PATH];
  SDL_snprintf(szFont, sizeof szFont, "glue\\%s\\tFont.pcx", screen.c_str());
  size_t cb = 0;
  unsigned char *p = static_cast<unsigned char *>(Find(szFont, &cb));
  const bool bOk = p != NULL && cb >= 128 + 769 && p[cb - 769] == 12;
  if (bOk)
    memcpy(pRgb, p + cb - 768, 768);
  SDL_free(p);
  return bOk;
}

// The entry of a screen's palette for a colour: the last entry of that colour, else the
// nearest; never 0, which a masked picture leaves clear.
static unsigned char GlueNearest(const unsigned char *pPal, const unsigned char *pRgb)
{
  int nBest = 1, nBestD = 0x7FFFFFFF;
  for (int i = 255; i >= 1 && nBestD != 0; --i) {
    const int dr = pRgb[0] - pPal[3 * i], dg = pRgb[1] - pPal[3 * i + 1], db = pRgb[2] - pPal[3 * i + 2];
    const int d = dr * dr + dg * dg + db * db;
    if (d < nBestD) {
      nBestD = d;
      nBest = i;
    }
  }
  return static_cast<unsigned char>(nBest);
}

// glue\<folder>\<name>.pcx from SD\glue\<folder>\<name>.DDS, in its screen's palette (GluePalette).
// A clear pixel is entry 0. 1.17's pictures with clear pixels have no other pixel at 0, and
// the ones without (the backgrounds) have some of entry 0's colour at 0, which they keep.
// Checked against 1.17's 235 such pictures: the same colour in every pixel but those of the
// two main menu buttons Hyperion's front end does not show and one pixel of PalCs's arrhot.
static void *GluePicture(const char *pszName, unsigned long *pcbSize)
{
  const size_t n = SDL_strlen(pszName);
  if (n < 10 || n >= MAX_PATH - 8 || SDL_strncasecmp(pszName, "glue\\", 5) != 0 || !NameEndsWith(pszName, ".pcx"))
    return NULL;
  char szDds[MAX_PATH];
  SDL_snprintf(szDds, sizeof szDds, "SD\\%.*s.DDS", static_cast<int>(n - 4), pszName);
  size_t cb = 0;
  unsigned char *p = static_cast<unsigned char *>(PortCascRead(szDds, &cb));
  std::vector<unsigned char> rgba;
  int w = 0, h = 0;
  const bool bRead = p != NULL && DdsToRgba(p, cb, &w, &h, rgba);
  SDL_free(p);
  unsigned char abPal[768];
  if (!bRead || !GluePalette(pszName, abPal))
    return NULL;
  const size_t nPixels = static_cast<size_t>(w) * h;
  bool bClear = false;
  for (size_t i = 0; i < nPixels && !bClear; ++i)
    bClear = rgba[4 * i + 3] == 0;
  std::vector<unsigned char> indices(nPixels, 0);
  std::unordered_map<unsigned, unsigned char> seen;
  for (size_t i = 0; i < nPixels; ++i) {
    const unsigned char *c = &rgba[4 * i];
    if (c[3] == 0 || (!bClear && memcmp(c, abPal, 3) == 0))
      continue;
    const unsigned key = c[0] | c[1] << 8 | c[2] << 16;
    auto it = seen.find(key);
    indices[i] = it != seen.end() ? it->second : (seen[key] = GlueNearest(abPal, c));
  }
  return Copy(PcxMake(indices.data(), w, h, abPal), pcbSize);
}

// glue\chatroom\iChat.grp, the chat room's icons, which Remastered does not have: 1.17's eleven
// frames at their places and sizes, every pixel clear. The chat room reads it as it opens and
// stops the game on a file it cannot read, so a room's match that a host on Remastered's files
// made ended as another player joined (the user, 2026-10-26: "The game host has left"). A
// room's chat room is never seen; the icons are its players' latency bars.
static void *ChatIcons(unsigned long *pcbSize)
{
  static const unsigned char kFrames[11][4] = {{5, 1, 5, 10},  {0, 0, 16, 12}, {0, 0, 16, 12}, {0, 0, 16, 12},
                                               {0, 0, 16, 12}, {0, 0, 4, 10},  {0, 0, 4, 10},  {0, 0, 4, 10},
                                               {0, 0, 184, 18}, {0, 0, 184, 18}, {0, 0, 164, 16}};
  const int nFrames = static_cast<int>(SDL_arraysize(kFrames));
  std::string s;
  auto u16 = [&](int v) { s += static_cast<char>(v & 0xFF); s += static_cast<char>((v >> 8) & 0xFF); };
  u16(nFrames);
  u16(256);
  u16(100);
  size_t at = 6 + 8 * nFrames;
  std::string rows;
  for (const auto &f : kFrames) {
    s.append(reinterpret_cast<const char *>(f), 4);
    const size_t table = at + rows.size();
    for (int i = 0; i < 4; ++i)
      s += static_cast<char>((table >> (8 * i)) & 0xFF);
    // each row's offset from the table, then the rows: runs of clear pixels, 127 at most
    std::string line;
    for (int w = f[2]; w > 0; w -= 127)
      line += static_cast<char>(0x80 | (w < 127 ? w : 127));
    const size_t first = 2 * f[3];
    for (int y = 0; y < f[3]; ++y) {
      const size_t off = first + y * line.size();
      rows += static_cast<char>(off & 0xFF);
      rows += static_cast<char>((off >> 8) & 0xFF);
    }
    for (int y = 0; y < f[3]; ++y)
      rows += line;
  }
  return Copy(s + rows, pcbSize);
}

// glue\Pal<xx>\Dlg.grp, tile.grp and arrow.grp, which Remastered has only redrawn, from 1.17's
// of the same shape it keeps, put entry by entry into the screen's colours: the dialog pieces
// from PalNl's (whose 139 first frames every screen's are, in its own colours; 77 to 84% of
// the pixels exactly 1.17's colour, the rest near it), the tiles and the cursor from the
// game's. Dlg.grp's frames are stored whole, the others as 1.17's GRP rows.
static void *GlueGrp(const char *pszName, unsigned long *pcbSize)
{
  const char *pszLast = SDL_strrchr(pszName, '\\');
  if (SDL_strncasecmp(pszName, "glue\\Pal", 8) != 0 || pszLast == NULL || SDL_strchr(pszName + 5, '\\') != pszLast)
    return NULL;
  const bool bDlg = NameIs(pszLast + 1, "Dlg.grp");
  const char *pszFrom = bDlg ? "glue\\PalNl\\Dlg.grp"
                        : NameIs(pszLast + 1, "tile.grp")  ? "dlgs\\tile.grp"
                        : NameIs(pszLast + 1, "arrow.grp") ? "cursor\\arrow.grp"
                                                           : NULL;
  if (pszFrom == NULL || NameIs(pszName, pszFrom))
    return NULL;
  unsigned char abTo[768], abFrom[768];
  RemasteredPalette game;
  if (!GluePalette(pszName, abTo))
    return NULL;
  if (bDlg) {
    if (!GluePalette(pszFrom, abFrom))
      return NULL;
  } else {
    if (!SharedPalette(&game))
      return NULL;
    memcpy(abFrom, game.abRgb, sizeof abFrom);
  }
  size_t cb = 0;
  unsigned char *p = static_cast<unsigned char *>(Find(pszFrom, &cb));
  if (p == NULL || cb < 6) {
    SDL_free(p);
    return NULL;
  }
  unsigned char abMap[256];
  abMap[0] = 0;
  for (int i = 1; i < 256; ++i)
    abMap[i] = GlueNearest(abTo, abFrom + 3 * i);
  // a frame may share another's bytes (Dlg.grp's first two do), so each byte is put once
  std::vector<bool> done(cb, false);
  auto put = [&](size_t at) {
    if (at < cb && !done[at]) {
      p[at] = abMap[p[at]];
      done[at] = true;
    }
  };
  const unsigned nFrames = U16(p);
  for (unsigned f = 0; f < nFrames && 6 + 8 * (f + 1) <= cb; ++f) {
    const unsigned char *pHead = p + 6 + 8 * f;
    const unsigned nW = pHead[2], nH = pHead[3];
    const size_t nOff = U32(pHead + 4);
    if (bDlg) {
      for (size_t k = 0; k < static_cast<size_t>(nW) * nH; ++k)
        put(nOff + k);
      continue;
    }
    for (unsigned r = 0; r < nH && nOff + 2 * r + 2 <= cb; ++r) {
      size_t at = nOff + U16(p + nOff + 2 * r);
      for (unsigned x = 0; x < nW && at < cb;) {
        const unsigned char k = p[at++];
        if (k & 0x80) {
          x += k & 0x7F;
        } else if (k & 0x40) {
          put(at++);
          x += k & 0x3F;
        } else {
          for (unsigned j = 0; j < k; ++j)
            put(at + j);
          at += k;
          x += k;
        }
      }
    }
  }
  *pcbSize = static_cast<unsigned long>(cb);
  return p;
}

static void *Made(const char *pszName, unsigned long *pcbSize, bool *pbMade)
{
  *pbMade = true;
  if (NameEndsWith(pszName, ".vx4") && SDL_strncasecmp(pszName, "tileset", 7) == 0) {
    char szEx[MAX_PATH];
    SDL_snprintf(szEx, sizeof szEx, "%sex", pszName);
    size_t cb = 0;
    unsigned char *p = static_cast<unsigned char *>(Find(szEx, &cb));
    if (p == NULL)
      return NULL;
    const size_t n = cb / 4;
    for (size_t i = 0; i < n; ++i) {
      p[2 * i] = p[4 * i];
      p[2 * i + 1] = p[4 * i + 1];
    }
    *pcbSize = static_cast<unsigned long>(2 * n);
    return p;
  }
  if (NameIs(pszName, "rez\\stat_txt.tbl")) {
    size_t cb = 0;
    unsigned char *p = static_cast<unsigned char *>(FindEnglish(pszName, &cb));
    if (p == NULL)
      return NULL;
    void *pTable = StatTxt(p, cb, pcbSize);
    return LangOther() ? LangStatTxt(pTable, *pcbSize, pcbSize) : pTable;
  }
  if (NameIs(pszName, "rez\\gluAll.tbl"))
    return KeyedTable("rez\\gluAll.xml", "gluall", kRemasteredGluAll, SDL_arraysize(kRemasteredGluAll), pcbSize);
  if (NameIs(pszName, "rez\\network.tbl"))
    return KeyedTable("rez\\network.xml", "network", kRemasteredNetwork, SDL_arraysize(kRemasteredNetwork), pcbSize);
  if (NameIs(pszName, "rez\\tips.tbl"))
    return KeyedTable("rez\\tips.xml", "tips_txt", kRemasteredTips, SDL_arraysize(kRemasteredTips), pcbSize);
  if (NameIs(pszName, "rez\\gluHist.tbl"))
    return KeyedTable("rez\\gluHist.xml", "gluhist", kRemasteredGluHist, SDL_arraysize(kRemasteredGluHist), pcbSize);
  if (NameIs(pszName, "rez\\help_txt.tbl"))
    return KeyedTable("rez\\help_txt.xml", "help_txt", kRemasteredHelpTxt, SDL_arraysize(kRemasteredHelpTxt), pcbSize);
  if (NameIs(pszName, "rez\\DataVersion.txt"))
    return Copy("0x11\r\n", pcbSize);
  if (SDL_strncasecmp(pszName, "rez\\", 4) == 0 && NameEndsWith(pszName, ".bin")) {
    void *p = LangOther() ? LangLayout(pszName, pcbSize) : PortLayoutFile(pszName, pcbSize);
    if (p != NULL)
      return p;
  }
  if (PortGameSource() == PORT_SOURCE_REMASTERED) {
    if (NameIs(pszName, "glue\\chatroom\\iChat.grp"))
      return ChatIcons(pcbSize);
    void *p = DdsPicture(pszName, pcbSize);
    if (p == NULL)
      p = GluePicture(pszName, pcbSize);
    if (p == NULL)
      p = GlueGrp(pszName, pcbSize);
    if (p != NULL)
      return p;
  }
  *pbMade = false;
  return NULL;
}

// HYPERION_REMASTERED_DUMP=<folder>: each file served, written there under its name with its
// separators as '#', for comparing with 1.17's files.
static void Dump(const char *pszName, const void *p, unsigned long cb)
{
  const char *pszFolder = SDL_getenv("HYPERION_REMASTERED_DUMP");
  if (pszFolder == NULL || pszFolder[0] == 0)
    return;
  char szPath[MAX_PATH];
  SDL_snprintf(szPath, sizeof szPath, "%s\\%s", pszFolder, pszName);
  for (char *q = szPath + SDL_strlen(pszFolder) + 1; *q; ++q)
    if (*q == '\\' || *q == '/')
      *q = '#';
  SDL_IOStream *f = SDL_IOFromFile(szPath, "wb");
  if (f != NULL) {
    SDL_WriteIO(f, p, cb);
    SDL_CloseIO(f);
  }
}

void *PortRemasteredVideoHd(const char *pszName, int nRows, unsigned long *pcbSize)
{
  *pcbSize = 0;
  if (PortGameSource() == PORT_SOURCE_MPQ)
    return NULL;
  // dubbed in the classic speech's language, which German, Spanish, French and Italian have under
  // their locale's SD\, in the classic's size
  if (SDL_strcmp(PortLanguageSpeech(), "enUS") != 0) {
    char szDubbed[MAX_PATH];
    SDL_snprintf(szDubbed, sizeof szDubbed, "locales\\%s\\Assets\\SD\\%s", PortLanguageSpeech(), pszName);
    size_t cb = 0;
    if (void *p = PortCascRead(szDubbed, &cb)) {
      *pcbSize = static_cast<unsigned long>(cb);
      return p;
    }
  }
  char szHalf[MAX_PATH];
  SDL_snprintf(szHalf, sizeof szHalf, "HD2\\%s", pszName);
  const char *apszTry[2] = {nRows <= 540 ? szHalf : pszName, nRows <= 540 ? pszName : szHalf};
  for (const char *pszTry : apszTry) {
    size_t cb = 0;
    void *p = PortCascRead(pszTry, &cb);
    if (p != NULL) {
      *pcbSize = static_cast<unsigned long>(cb);
      return p;
    }
  }
  return NULL;
}

void *PortRemasteredStatTxtEnglish(unsigned long *pcbSize)
{
  *pcbSize = 0;
  if (PortGameSource() == PORT_SOURCE_MPQ)
    return NULL;
  size_t cb = 0;
  unsigned char *p = static_cast<unsigned char *>(FindEnglish("rez\\stat_txt.tbl", &cb));
  return p != NULL ? StatTxt(p, cb, pcbSize) : NULL;
}

void *PortRemasteredLoad(const char *pszName, unsigned long *pcbSize)
{
  *pcbSize = 0;
  if (PortGameSource() == PORT_SOURCE_BOTH && SDL_strncasecmp(pszName, "font\\", 5) == 0)
    return NULL;
  bool bMade;
  void *p = Made(pszName, pcbSize, &bMade);
  if (!bMade) {
    const char *pszLook = pszName;
    for (size_t i = 0; i < SDL_arraysize(kMoved); ++i)
      if (NameIs(pszName, kMoved[i].psz117))
        pszLook = kMoved[i].pszRemastered;
    const char *pszLast = SDL_strrchr(pszName, '\\');
    if (PortGameSource() == PORT_SOURCE_REMASTERED && SDL_strncasecmp(pszName, "glue\\Pal", 8) == 0 && pszLast &&
        pszLast - pszName == 10)
      for (size_t i = 0; i < SDL_arraysize(kStandIn); ++i)
        if (NameIs(pszLast + 1, kStandIn[i].psz117))
          pszLook = kStandIn[i].pszRemastered;
    size_t cb = 0;
    p = pszLook != pszName ? PortCascRead(pszLook, &cb) : Find(pszName, &cb);
    *pcbSize = static_cast<unsigned long>(cb);
    if (p == NULL)
      p = PortLocalTemplate(pszName, pcbSize);
    if (p != NULL && LangOther() && SDL_strncasecmp(pszName, "Templates\\", 10) == 0)
      LangTemplate(p, *pcbSize);
  }
  if (p != NULL)
    Dump(pszName, p, *pcbSize);
  return p;
}
