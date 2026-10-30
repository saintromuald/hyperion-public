// port_settings.cpp -- Hyperion's settings file, settings.json in the player data folder
// (docs/modules/platform.md, "Settings file"). Presentation only.
//
// Hyperion keeps its settings apart from the player's vanilla StarCraft (PRD 2026-09-27).
// 1.17's own come through Storm's registry calls (port_stormbase.cpp) under the key and value
// names 1.17 gives them, so vanilla's key is never read or written; Hyperion's own are the
// "hyperion" section. A REG_DWORD is a JSON number, a REG_SZ a string, and REG_BINARY (or
// REG_MULTI_SZ, with "multi") an object holding the bytes as hex. Names are matched without
// regard to case, as the registry matches them. The file is written whole, through a temporary
// file, each time a value changes, unless PortSettingsHold holds the writes.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "port_settings.h"
#include "port_json.h"  // PortJsonQuote

#include "port_paths.h"  // PortDataFolder; the port's Storm tests, which build this alone, give their own

namespace {

// KIND_NONE is a new entry's until its setter, under the same lock, gives it a value
enum Kind { KIND_NONE, KIND_NUMBER, KIND_STRING, KIND_DATA };

struct Entry {
  std::string name;
  Kind kind;
  double d;
  std::string s;
  std::vector<unsigned char> data;
  bool bMulti;
};

struct Section {
  std::string name;
  std::vector<Entry> entries;
};

std::vector<Section> g_sections;
bool g_bLoaded;
char g_szNote[160] = "no settings.json; the defaults";
int g_nHold;
bool g_bDirty;
char g_szPath[MAX_PATH + 32];
SRWLOCK g_lock = SRWLOCK_INIT;

// ---------------------------------------------------------------------------
// Reading the file: a small JSON reader for what the file holds, objects of strings,
// numbers and hex objects. Anything else is skipped.

struct Reader {
  const char *p;
  const char *e;
  bool bBad;

  void Space()
  {
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
      ++p;
  }
  bool Take(char c)
  {
    Space();
    if (p < e && *p == c) {
      ++p;
      return true;
    }
    return false;
  }
  bool String(std::string *ps)
  {
    Space();
    if (p >= e || *p != '"') {
      bBad = true;
      return false;
    }
    ++p;
    ps->clear();
    while (p < e && *p != '"') {
      char c = *p++;
      if (c == '\\' && p < e) {
        c = *p++;
        switch (c) {
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'r': c = '\r'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'u': {
          unsigned int u = 0;
          for (int i = 0; i < 4 && p < e; ++i, ++p) {
            const char h = *p;
            u = u * 16 + (h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
          }
          // as UTF-8; the file never holds surrogate pairs of its own making
          if (u < 0x80) {
            ps->push_back(static_cast<char>(u));
          } else if (u < 0x800) {
            ps->push_back(static_cast<char>(0xC0 | (u >> 6)));
            ps->push_back(static_cast<char>(0x80 | (u & 0x3F)));
          } else {
            ps->push_back(static_cast<char>(0xE0 | (u >> 12)));
            ps->push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
            ps->push_back(static_cast<char>(0x80 | (u & 0x3F)));
          }
          continue;
        }
        default: break;  // \" \\ \/
        }
      }
      ps->push_back(c);
    }
    if (p >= e) {
      bBad = true;
      return false;
    }
    ++p;
    return true;
  }
  // a value of any kind, passed over; one nested past 64 deep is a broken file, as port_json's
  void Skip(int nDepth = 0)
  {
    Space();
    if (p >= e)
      return;
    if (nDepth > 64) {
      bBad = true;
      return;
    }
    if (*p == '"') {
      std::string s;
      String(&s);
    } else if (*p == '{' || *p == '[') {
      const char cOpen = *p, cClose = cOpen == '{' ? '}' : ']';
      ++p;
      while (!bBad && p < e) {
        Space();
        if (Take(cClose))
          return;
        if (cOpen == '{') {
          std::string k;
          if (!String(&k) || !Take(':')) {
            bBad = true;
            return;
          }
        }
        Skip(nDepth + 1);
        Take(',');
      }
    } else {
      while (p < e && *p != ',' && *p != '}' && *p != ']')
        ++p;
    }
  }
};

int HexDigit(char c)
{
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// one entry's value
bool ReadValue(Reader &r, Entry *pEntry)
{
  r.Space();
  if (r.p >= r.e)
    return false;
  const char c = *r.p;
  if (c == '"') {
    pEntry->kind = KIND_STRING;
    return r.String(&pEntry->s);
  }
  if (c == '-' || (c >= '0' && c <= '9')) {
    char *pEnd = NULL;
    // the number's own characters, however many: cut short, the digits left in the stream
    // would fail the entry after it and lose every section from there
    const char *pNum = r.p;
    while (pNum < r.e && (*pNum == '-' || *pNum == '+' || *pNum == '.' || *pNum == 'e' ||
                          *pNum == 'E' || (*pNum >= '0' && *pNum <= '9')))
      ++pNum;
    std::string num(r.p, pNum - r.p);
    pEntry->d = strtod(num.c_str(), &pEnd);
    r.p += pEnd - num.c_str();
    pEntry->kind = KIND_NUMBER;
    return true;
  }
  if (c == '{') {
    ++r.p;
    pEntry->kind = KIND_DATA;
    pEntry->bMulti = false;
    while (!r.bBad && r.p < r.e) {
      if (r.Take('}'))
        return true;
      std::string k;
      if (!r.String(&k) || !r.Take(':'))
        return false;
      if (k == "hex") {
        std::string h;
        if (!r.String(&h))
          return false;
        pEntry->data.clear();
        for (size_t i = 0; i + 1 < h.size(); i += 2) {
          const int hi = HexDigit(h[i]), lo = HexDigit(h[i + 1]);
          if (hi < 0 || lo < 0)
            break;
          pEntry->data.push_back(static_cast<unsigned char>(hi * 16 + lo));
        }
      } else if (k == "multi") {
        r.Space();
        pEntry->bMulti = r.p < r.e && *r.p == 't';
        r.Skip();
      } else {
        r.Skip();
      }
      r.Take(',');
    }
    return false;
  }
  r.Skip();
  return false;
}

void Load(void)
{
  if (g_bLoaded)
    return;
  g_bLoaded = true;
  sprintf_s(g_szPath, sizeof g_szPath, "%ssettings.json", PortDataFolder());
  FILE *f = fopen(g_szPath, "rb");
  if (f == NULL)
    return;
  std::vector<char> text;
  char buf[4096];
  for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;)
    text.insert(text.end(), buf, buf + n);
  fclose(f);
  sprintf_s(g_szNote, sizeof g_szNote, "settings.json, %u bytes, empty", static_cast<unsigned>(text.size()));
  if (text.empty())
    return;
  char *pText = text.data();
  const size_t nSize = text.size();
  Reader r = {pText, pText + nSize, false};
  if (nSize >= 3 && static_cast<unsigned char>(pText[0]) == 0xEF)
    r.p += 3;  // a byte-order mark an editor may have added
  if (r.Take('{')) {
    while (!r.bBad && r.p < r.e) {
      if (r.Take('}'))
        break;
      Section sec;
      if (!r.String(&sec.name) || !r.Take(':'))
        break;
      if (!r.Take('{')) {
        r.Skip();
        r.Take(',');
        continue;
      }
      while (!r.bBad && r.p < r.e) {
        if (r.Take('}'))
          break;
        Entry en;
        en.d = 0.0;
        en.bMulti = false;
        if (!r.String(&en.name) || !r.Take(':'))
          break;
        if (ReadValue(r, &en))
          sec.entries.push_back(en);
        r.Take(',');
      }
      g_sections.push_back(sec);
      r.Take(',');
    }
  }
  // where a file an editor broke stopped being read: what came after is the defaults
  size_t nEntries = 0;
  for (const Section &sec : g_sections)
    nEntries += sec.entries.size();
  r.Space();
  if (r.bBad || r.p < r.e) {
    unsigned nLine = 1;
    for (const char *p = pText; p < r.p && p < r.e; ++p)
      nLine += *p == '\n';
    sprintf_s(g_szNote, sizeof g_szNote, "settings.json, %u bytes, read as far as line %u; %u values kept, the rest "
              "the defaults", static_cast<unsigned>(nSize), nLine, static_cast<unsigned>(nEntries));
  } else {
    sprintf_s(g_szNote, sizeof g_szNote, "settings.json, %u bytes, %u values", static_cast<unsigned>(nSize),
              static_cast<unsigned>(nEntries));
  }
}

// ---------------------------------------------------------------------------
// Writing it.

void Write(void)
{
  if (g_nHold > 0) {
    g_bDirty = true;
    return;
  }
  g_bDirty = false;
  std::string out = "{\n";
  for (size_t i = 0; i < g_sections.size(); ++i) {
    const Section &sec = g_sections[i];
    out += "  ";
    out += PortJsonQuote(sec.name);
    out += ": {";
    for (size_t j = 0; j < sec.entries.size(); ++j) {
      const Entry &en = sec.entries[j];
      out += j == 0 ? "\n    " : ",\n    ";
      out += PortJsonQuote(en.name);
      out += ": ";
      char sz[48];
      if (en.kind == KIND_NUMBER) {
        if (en.d == floor(en.d) && fabs(en.d) < 1e15)
          sprintf_s(sz, sizeof sz, "%.0f", en.d);
        else
          sprintf_s(sz, sizeof sz, "%.17g", en.d);
        out += sz;
      } else if (en.kind == KIND_STRING) {
        out += PortJsonQuote(en.s);
      } else {
        out += "{\"hex\": \"";
        for (const unsigned char b : en.data) {
          sprintf_s(sz, sizeof sz, "%02x", b);
          out += sz;
        }
        out += en.bMulti ? "\", \"multi\": true}" : "\"}";
      }
    }
    out += sec.entries.empty() ? "}" : "\n  }";
    out += i + 1 < g_sections.size() ? ",\n" : "\n";
  }
  out += "}\n";
  char szTemp[MAX_PATH + 40];
  sprintf_s(szTemp, sizeof szTemp, "%s.new", g_szPath);
  FILE *f = fopen(szTemp, "wb");
  if (f == NULL)
    return;
  const bool bOk = fwrite(out.data(), 1, out.size(), f) == out.size();
  if (fclose(f) != 0 || !bOk) {
    DeleteFileA(szTemp);
    return;
  }
  MoveFileExA(szTemp, g_szPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

Section *SectionOf(const char *pszSection, bool bMake)
{
  for (Section &s : g_sections)
    if (_stricmp(s.name.c_str(), pszSection) == 0)
      return &s;
  if (!bMake)
    return NULL;
  Section s;
  s.name = pszSection;
  g_sections.push_back(s);
  return &g_sections.back();
}

Entry *EntryOf(const char *pszSection, const char *pszName, bool bMake)
{
  Load();
  Section *pSec = SectionOf(pszSection, bMake);
  if (pSec == NULL)
    return NULL;
  for (Entry &en : pSec->entries)
    if (_stricmp(en.name.c_str(), pszName) == 0)
      return &en;
  if (!bMake)
    return NULL;
  Entry en;
  en.name = pszName;
  en.kind = KIND_NONE;
  en.d = 0.0;
  en.bMulti = false;
  pSec->entries.push_back(en);
  return &pSec->entries.back();
}

struct Lock {
  Lock() { AcquireSRWLockExclusive(&g_lock); }
  ~Lock() { ReleaseSRWLockExclusive(&g_lock); }
};

}  // namespace

// ---------------------------------------------------------------------------

PortSettingKind PortSettingKindOf(const char *pszSection, const char *pszName)
{
  Lock lock;
  const Entry *en = EntryOf(pszSection, pszName, false);
  if (en == NULL)
    return PORT_SETTING_NONE;
  return en->kind == KIND_NUMBER ? PORT_SETTING_NUMBER : en->kind == KIND_STRING ? PORT_SETTING_STRING : PORT_SETTING_DATA;
}

bool PortSettingNumber(const char *pszSection, const char *pszName, double *pdValue)
{
  Lock lock;
  const Entry *en = EntryOf(pszSection, pszName, false);
  if (en == NULL)
    return false;
  if (en->kind == KIND_NUMBER)
    *pdValue = en->d;
  else if (en->kind == KIND_STRING)
    *pdValue = static_cast<double>(strtoul(en->s.c_str(), NULL, 0));  // as SRegLoadValue reads a string
  else
    return false;
  return true;
}

bool PortSettingString(const char *pszSection, const char *pszName, char *pszBuf, size_t cchBuf)
{
  Lock lock;
  const Entry *en = EntryOf(pszSection, pszName, false);
  if (en == NULL || cchBuf == 0)
    return false;
  if (en->kind == KIND_STRING)
    strncpy_s(pszBuf, cchBuf, en->s.c_str(), _TRUNCATE);
  else if (en->kind == KIND_NUMBER)
    _snprintf_s(pszBuf, cchBuf, _TRUNCATE, "%u", static_cast<unsigned int>(en->d));  // as SRegLoadString writes a number
  else
    return false;
  return true;
}

bool PortSettingData(const char *pszSection, const char *pszName, void *pvBuf, size_t cbBuf, size_t *pcbData,
                     bool *pbMulti)
{
  Lock lock;
  const Entry *en = EntryOf(pszSection, pszName, false);
  if (en == NULL)
    return false;
  std::vector<unsigned char> bytes;
  if (en->kind == KIND_DATA) {
    bytes = en->data;
  } else if (en->kind == KIND_STRING) {
    bytes.assign(en->s.begin(), en->s.end());
    bytes.push_back(0);
  } else {
    const DWORD dw = static_cast<DWORD>(en->d);
    bytes.assign(reinterpret_cast<const unsigned char *>(&dw), reinterpret_cast<const unsigned char *>(&dw) + 4);
  }
  if (pbMulti != NULL)
    *pbMulti = en->kind == KIND_DATA && en->bMulti;
  *pcbData = bytes.size();
  if (bytes.size() > cbBuf)
    return false;  // as the registry answers ERROR_MORE_DATA
  if (!bytes.empty())
    memcpy(pvBuf, bytes.data(), bytes.size());
  return true;
}

void PortSettingSetNumber(const char *pszSection, const char *pszName, double dValue)
{
  Lock lock;
  Entry *en = EntryOf(pszSection, pszName, true);
  if (en->kind == KIND_NUMBER && en->d == dValue)
    return;
  en->kind = KIND_NUMBER;
  en->d = dValue;
  Write();
}

void PortSettingSetString(const char *pszSection, const char *pszName, const char *pszValue)
{
  Lock lock;
  Entry *en = EntryOf(pszSection, pszName, true);
  if (en->kind == KIND_STRING && en->s == pszValue)
    return;
  en->kind = KIND_STRING;
  en->s = pszValue;
  Write();
}

void PortSettingSetData(const char *pszSection, const char *pszName, const void *pvData, size_t cbData, bool bMulti)
{
  Lock lock;
  Entry *en = EntryOf(pszSection, pszName, true);
  const unsigned char *p = static_cast<const unsigned char *>(pvData);
  std::vector<unsigned char> bytes(p, p + cbData);
  if (en->kind == KIND_DATA && en->data == bytes && en->bMulti == bMulti)
    return;
  en->kind = KIND_DATA;
  en->data = bytes;
  en->bMulti = bMulti;
  Write();
}

bool PortSettingRemove(const char *pszSection, const char *pszName)
{
  Lock lock;
  Load();
  Section *pSec = SectionOf(pszSection, false);
  if (pSec == NULL)
    return false;
  for (size_t i = 0; i < pSec->entries.size(); ++i) {
    if (_stricmp(pSec->entries[i].name.c_str(), pszName) == 0) {
      pSec->entries.erase(pSec->entries.begin() + i);
      Write();
      return true;
    }
  }
  return false;
}

void PortSettingsHold(void)
{
  Lock lock;
  ++g_nHold;
}

void PortSettingsRelease(void)
{
  Lock lock;
  if (g_nHold > 0 && --g_nHold == 0 && g_bDirty)
    Write();
}

const char *PortSettingsPath(void)
{
  Lock lock;
  Load();
  return g_szPath;
}

const char *PortSettingsReadNote(void)
{
  Lock lock;
  Load();
  return g_szNote;
}
