// port_hotkeys.cpp -- the profile's profiles\<name>\hotkeys.json: the command card's keys (K1),
// the accelerator commands (K2) and the keys 1.17 compares in its code (K3)
// (docs/modules/gameplay.md). Presentation only: a key chooses a button or a command, and the
// command is what a replay records. A game reads the file as it starts, Settings' Layout
// writes it, and without a profile it is Classic with no keys of its own.
//
// K1: a button's key is the profile's own for its stat_txt string ("card"); else, with the
// Grid layout, its place's key, Q W E / A S D / Z X C, outside a replay (whose card keeps
// 1.17's P, U, D) and only for a button whose own key is a letter or a digit (Cancel keeps
// Escape); else 1.17's first character (statcmd_hotkey_cmp_fn, statcmd.cpp). K2: "accel" binds
// a command by name to a key or a list ("Ctrl+1", "Shift+F2", "Alt+C", "+"), an empty list
// unbinding it. K3: "keys", so far "groupSelect1" to "groupSelect0"; a digit keeps its group
// unless the profile gives that group another key, and the command card sees a key first.
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include "port_hotkeys.h"
#include "../platform/port_log.h"  // PortLogger
#include "../platform/port_json.h"
#include "../front/port_frontscreens.h"  // ProfileFolder
#include "../../h/selectlimits.h"  // SelectionWide, the letters' game
#include "../../gen/globals117.h"

namespace {

const char kGrid[10] = "qweasdzxc";

bool g_bLoaded;
int g_nLayout;                      // PORT_CARD_CLASSIC or PORT_CARD_GRID
std::map<unsigned, char> g_card;    // a string id's own key
std::map<unsigned, std::vector<ACCEL>> g_accel;  // a command's own keys
int g_anGroupKey[19];               // a group's key, lower case; 0 for its own

// The nine extra groups' keys, the letters the user picked (2026-11-02): groups 11 to 19
// select on R, F, V, T, G, B, Y, H and N, with Ctrl to make a group of the selection,
// Shift to add to it and Alt to select and centre, as the digit groups' Ctrl, Shift and
// Alt work. A profile may give any group any letter or digit in hotkeys.json's "keys".
// The letters exist in a game of Hyperion's selection, whose group rows hold them
// (selectlimits.h); a classic game keeps 1.17's rows and letters.
const char kGroupLetters[10] = "rfvtgbyhn";

// The accelerators' commands by name (dialog_accel, game_evt.cpp); the groups' and the
// locations' with their digit after.
struct Command {
  const char *pszName;
  unsigned nFirst, nCount;
};
const Command kCommands[] = {
    {"groupAdd", 39891, 10},     // Shift+1 .. Shift+9, Shift+0: add the selection to a group
    {"groupSet", 39901, 10},     // Ctrl+1 .. Ctrl+0: make a group of the selection
    {"groupCentre", 39921, 10},  // Alt+1 .. Alt+0: select a group and centre on it
    {"centreSelected", 40036, 1},
    {"music", 40038, 1},
    {"sound", 40039, 1},
    {"quit", 40043, 1},
    {"quitMission", 40045, 1},
    {"options", 40047, 1},
    {"help", 40052, 1},
    {"messageLog", 40053, 1},
    {"save", 40054, 1},
    {"load", 40055, 1},
    {"speedUp", 40056, 1},
    {"speedDown", 40058, 1},
    {"gameMenu", 40059, 1},
    // 1.17's F2 to F4 screen locations are gone (their keys came off the tables in
    // event_Win.cpp); SC2's eight camera locations are Hyperion's own hotkeys
    // (port_keys.h, PORT_HOTKEY_CAM1 to CAM8, one key each, Ctrl to save), set in
    // Settings rather than here.
};

// A command's id from its name: "speedUp", or "groupSet1" (the groups' 1 to 9 then 0, the
// locations' 1 to 3); 0 when it is none.
unsigned CommandId(const std::string &name)
{
  for (const Command &c : kCommands) {
    const size_t n = strlen(c.pszName);
    if (name.compare(0, n, c.pszName) != 0)
      continue;
    if (c.nCount == 1)
      return name.size() == n ? c.nFirst : 0;
    if (name.size() != n + 1 || !isdigit(static_cast<unsigned char>(name[n])))
      continue;
    const unsigned d = static_cast<unsigned>(name[n] - '0');
    if (c.nCount == 10)
      return c.nFirst + (d == 0 ? 9 : d - 1);
    return d >= 1 && d <= c.nCount ? c.nFirst + d - 1 : 0;
  }
  return 0;
}

// A key as written, "Ctrl+Shift+F2", "Alt+C", "+": false when it is none.
bool ParseKey(const std::string &text, ACCEL *pOut)
{
  BYTE fVirt = 0;
  std::string key = text;
  for (;;) {
    const size_t plus = key.find('+');
    if (plus == std::string::npos || plus == 0 || plus + 1 == key.size())
      break;
    const std::string mod = key.substr(0, plus);
    if (_stricmp(mod.c_str(), "Ctrl") == 0)
      fVirt |= FCONTROL;
    else if (_stricmp(mod.c_str(), "Shift") == 0)
      fVirt |= FSHIFT;
    else if (_stricmp(mod.c_str(), "Alt") == 0)
      fVirt |= FALT;
    else
      return false;
    key = key.substr(plus + 1);
  }
  WORD wKey = 0;
  if (key.size() == 1 && isalnum(static_cast<unsigned char>(key[0]))) {
    wKey = static_cast<WORD>(toupper(static_cast<unsigned char>(key[0])));
    fVirt |= FVIRTKEY;
  } else if ((key[0] == 'F' || key[0] == 'f') && key.size() <= 3 && atoi(key.c_str() + 1) >= 1 &&
             atoi(key.c_str() + 1) <= 12) {
    wKey = static_cast<WORD>(VK_F1 + atoi(key.c_str() + 1) - 1);
    fVirt |= FVIRTKEY;
  } else if (key.size() == 1 && fVirt == 0) {
    wKey = static_cast<WORD>(static_cast<unsigned char>(key[0]));  // a character, as 1.17's "+"
  } else {
    return false;
  }
  pOut->fVirt = fVirt;
  pOut->key = wKey;
  return true;
}

std::string FilePath(void)
{
  const std::string folder = FrontKit::ProfileFolder();
  return folder.empty() ? std::string() : folder + "\\hotkeys.json";
}

void Load(void)
{
  g_bLoaded = true;
  g_nLayout = PORT_CARD_CLASSIC;
  g_card.clear();
  g_accel.clear();
  memset(g_anGroupKey, 0, sizeof g_anGroupKey);
  // the nine extra groups' own letters, which a profile's keys may take off or move
  for (int g = 10; g < 19; ++g)
    g_anGroupKey[g] = kGroupLetters[g - 10];
  const std::string path = FilePath();
  FILE *f = NULL;
  if (path.empty() || fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
    return;
  std::string text;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0)
    text.append(buf, n);
  fclose(f);
  PortJson j;
  if (!PortJsonParse(text.c_str(), &j))
    return;
  g_nLayout = j["layout"].Str() == "grid" ? PORT_CARD_GRID : PORT_CARD_CLASSIC;
  for (const auto &m : j["accel"].members) {
    const unsigned nCmd = CommandId(m.first);
    if (nCmd == 0)
      continue;
    std::vector<ACCEL> &keys = g_accel[nCmd];
    keys.clear();
    std::vector<PortJson> items;
    if (m.second.kind == PortJson::ARRAY)
      items = m.second.items;
    else if (m.second.kind == PortJson::STRING)
      items.push_back(m.second);
    for (const PortJson &item : items) {
      ACCEL a = {};
      if (ParseKey(item.Str(), &a)) {
        a.cmd = static_cast<WORD>(nCmd);
        keys.push_back(a);
      }
    }
  }
  for (const auto &m : j["keys"].members) {
    const std::string value = m.second.Str();
    if (m.first.compare(0, 11, "groupSelect") != 0 || value.size() != 1)
      continue;
    const std::string num = m.first.substr(11);
    int g = 0;
    bool bDigits = !num.empty();
    for (const char c : num) {
      if (!isdigit(static_cast<unsigned char>(c))) {
        bDigits = false;
        break;
      }
      g = g * 10 + c - '0';
    }
    if (bDigits && g >= 0 && g < 19)
      g_anGroupKey[g] = tolower(static_cast<unsigned char>(value[0]));
  }
  for (const auto &m : j["card"].members) {
    const std::string &key = m.first;
    const std::string value = m.second.Str();
    if (!key.empty() && value.size() == 1)
      g_card[static_cast<unsigned>(atoi(key.c_str()))] = static_cast<char>(tolower(static_cast<unsigned char>(value[0])));
  }
}

void Save(void)
{
  const std::string path = FilePath();
  if (path.empty())
    return;
  FILE *f = NULL;
  if (fopen_s(&f, path.c_str(), "wb") != 0 || f == NULL)
    return;
  std::string t = std::string("{\r\n  \"layout\": \"") + (g_nLayout == PORT_CARD_GRID ? "grid" : "classic") + "\"";
  if (!g_card.empty()) {
    t += ",\r\n  \"card\": {";
    bool bFirst = true;
    for (const auto &kv : g_card) {
      t += (bFirst ? "\r\n    \"" : ",\r\n    \"") + std::to_string(kv.first) + "\": \"" + kv.second + "\"";
      bFirst = false;
    }
    t += "\r\n  }";
  }
  // the groups' keys other than their own: a digit's stays 1.17's digit, the letters' the
  // letter (kGroupLetters)
  std::string keys;
  for (int g = 0; g < 19; ++g) {
    const char c = static_cast<char>(g_anGroupKey[g]);
    if (c == 0 || (g >= 10 && c == kGroupLetters[g - 10]))
      continue;
    keys += (keys.empty() ? "\r\n    \"" : ",\r\n    \"") + std::string("groupSelect") + std::to_string(g) + "\": \"";
    keys += c;
    keys += "\"";
  }
  if (!keys.empty())
    t += ",\r\n  \"keys\": {" + keys + "\r\n  }";
  t += "\r\n}\r\n";
  fwrite(t.data(), 1, t.size(), f);
  fclose(f);
}

}  // namespace

void PortHotkeysLoad(void)
{
  Load();
}

int PortKeyGroup(unsigned nChar)
{
  if (!g_bLoaded)
    Load();
  const int c = tolower(static_cast<int>(nChar & 0xFF));
  for (int g = 0; g < 19; g++)
    if (g_anGroupKey[g] != 0 && g_anGroupKey[g] == c) {
      // the letters' rows exist only in a game of Hyperion's selection; a digit a profile
      // gave a letter group keeps its own group's row there
      if (g < 10 || SelectionWide())
        return g;
      if (c >= '0' && c <= '9')
        break;
      return -1;
    }
  if (c >= '0' && c <= '9' && g_anGroupKey[c - '0'] == 0)
    return c - '0';
  return -1;
}

// The key a group selects on, lower case, 0 for its own: 1.17's digit for the first ten,
// the letter for the six extra (kGroupLetters). Presentation only, for Settings' rows.
int PortKeyGroupGet(int nGroup)
{
  if (!g_bLoaded)
    Load();
  return nGroup >= 0 && nGroup < 19 ? g_anGroupKey[nGroup] : 0;
}

// The key a group shows and selects on, its own when the player gave it none: the digit for
// the first ten (1 to 9, then 0), the letter for the six extra. Presentation only.
char PortKeyGroupShown(int nGroup)
{
  if (!g_bLoaded)
    Load();
  if (nGroup < 0 || nGroup >= 19)
    return '0';
  if (g_anGroupKey[nGroup] != 0)
    return static_cast<char>(g_anGroupKey[nGroup]);
  return nGroup < 10 ? static_cast<char>('0' + nGroup) : kGroupLetters[nGroup - 10];
}

// The key a group selects on, lower case, 0 for its own. Saved to the profile's
// hotkeys.json; any letter or digit may be a group's key, and the key it leaves is free.
bool PortKeyGroupSet(int nGroup, int nChar)
{
  if (!g_bLoaded)
    Load();
  if (nGroup < 0 || nGroup >= 19)
    return false;
  if (nChar != 0 && !PortKeyGroupFree(nChar))
    return false;
  g_anGroupKey[nGroup] = nChar == 0 ? static_cast<char>(0) : static_cast<char>(tolower(nChar));
  Save();
  return true;
}

// Whether a key may select a group: a letter or a digit.
bool PortKeyGroupFree(int nChar)
{
  return (nChar >= 'a' && nChar <= 'z') || (nChar >= 'A' && nChar <= 'Z') ||
         (nChar >= '0' && nChar <= '9');
}

int PortAccelRemap(const void *pIn, int nIn, void *pOut, int nCap)
{
  if (!g_bLoaded)
    Load();
  const ACCEL *pFrom = static_cast<const ACCEL *>(pIn);
  ACCEL *pTo = static_cast<ACCEL *>(pOut);
  int n = 0;
  // 1.17's, less the commands the profile binds itself
  for (int i = 0; i < nIn && n < nCap; i++)
    if (g_accel.find(pFrom[i].cmd) == g_accel.end())
      pTo[n++] = pFrom[i];
  const int nKept = n;
  // then the profile's, for the commands this table has
  for (const auto &kv : g_accel) {
    bool bHere = false;
    for (int i = 0; i < nIn && !bHere; i++)
      bHere = pFrom[i].cmd == kv.first;
    for (const ACCEL &a : kv.second)
      if (bHere && n < nCap)
        pTo[n++] = a;
  }
  static const PortLogger s_log("hyperion_keys.log", PortLogger::SHARED);
  s_log("accelerators: %d of 1.17's kept, %d the profile's", nKept, n - nKept);
  return n;
}

bool PortAccelProfiled(void)
{
  if (!g_bLoaded)
    Load();
  return !g_accel.empty();
}

int PortCardLayout(void)
{
  if (!g_bLoaded)
    Load();
  return g_nLayout;
}

void PortCardLayoutSet(int nLayout)
{
  if (!g_bLoaded)
    Load();
  g_nLayout = nLayout == PORT_CARD_GRID ? PORT_CARD_GRID : PORT_CARD_CLASSIC;
  Save();
}

char PortCardKey(unsigned wStrId, int nSlot, const char *pszText)
{
  if (!g_bLoaded)
    Load();
  const char cOwn = pszText != NULL ? pszText[0] : 0;
  const auto it = g_card.find(wStrId);
  if (it != g_card.end())
    return it->second;
  if (g_nLayout == PORT_CARD_GRID && !gPlayback && nSlot >= 1 && nSlot <= 9 &&
      isalnum(static_cast<unsigned char>(cOwn))) {
    // Hyperion: Attack and Patrol are swapped (the user, 2026-11-02) before the grid's
    // by-place keys, so Attack stays A and Patrol is E in either layout. The string's
    // name follows its key and its cost's kind (a costless button's is 0).
    if (pszText != NULL && pszText[0] != '\0' && pszText[2] != '\0') {
      const char *pszName = pszText + 2;
      if (_strnicmp(pszName, "patrol", 6) == 0)
        return 'e';
      if (_strnicmp(pszName, "attack", 6) == 0)
        return 'a';
    }
    return kGrid[nSlot - 1];
  }
  // the swap for the classic layout's keys too
  if (pszText != NULL && pszText[0] != '\0' && pszText[2] != '\0') {
    const char *pszName = pszText + 2;
    if (_strnicmp(pszName, "patrol", 6) == 0)
      return 'e';
    if (_strnicmp(pszName, "attack", 6) == 0)
      return 'a';
  }
  return cOwn;
}

const char *PortCardTooltip(unsigned wStrId, int nSlot, const char *pszText)
{
  // the text after the key and the cost's kind; when the key is the string's own, as it is
  const char *pszShown = pszText + 2;
  const char cKey = PortCardKey(wStrId, nSlot, pszText);
  if (cKey == pszText[0])
    return pszShown;
  // else the own key's highlight taken off, and the key the button has now after the first
  // line, in the highlight colour
  static char s_sz[512];
  size_t n = 0;
  bool bMarked = false;
  for (const char *p = pszShown; *p != '\0' && n + 8 < sizeof s_sz; ++p) {
    if (*p == '\n' && !bMarked) {
      n += snprintf(s_sz + n, sizeof s_sz - n, " (\x03%c\x01)", toupper(static_cast<unsigned char>(cKey)));
      bMarked = true;
    }
    if (*p == '\x03' && p[1] != '\0' && p[2] == '\x01') {
      s_sz[n++] = p[1];
      p += 2;
      continue;
    }
    s_sz[n++] = *p;
  }
  if (!bMarked && n + 8 < sizeof s_sz)
    n += snprintf(s_sz + n, sizeof s_sz - n, " (\x03%c\x01)", toupper(static_cast<unsigned char>(cKey)));
  s_sz[n] = '\0';
  return s_sz;
}
