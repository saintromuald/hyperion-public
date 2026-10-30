// port_front_load.cpp -- Load Game (docs/modules/front-end.md), the dialog over
// the campaign screen: the profile's saves,
// where 1.17 keeps them (save\<ID>\), all of them, the campaign's or the skirmishes', each with
// its map and when it was saved, read from the text a save starts with (write_save_header).
// Load starts one as 1.17's Load screen does (port_launch.cpp), and Delete deletes it after
// asking. Presentation only.
//
// The game menu's Load Game and Save Game are the same dialog over a game (port_front_gamemenu.cpp,
// the saves of the kind the game makes, Load sending the load as 1.17's Load dialog does,
// and Save, with a name to save under, as its Save dialog does (PortGameLoad, PortGameSave). What
// either chose waits for GameSavesStep, which the game menu's step calls out of the frame.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include "../gameplay/port_launch.h"  // PortLaunchLoadFile
#include "saveload.h"     // MakeSaveDir, GetSaveExtension, sub_498B30
#include "port_frontkit.h"
#include "port_frontlobby.h"
#include "port_frontscreens.h"
#include "port_gamemenu.h"  // PortGameLoad, PortGameSave
#include "port_ui.h"
#include "../../gen/globals117.h"

using namespace FrontKit;

namespace {

struct Save {
  std::string path, name, title, type;
  bool bCampaign = false;
  FILETIME when;
};

std::vector<Save> g_saves;
bool g_bLoadUp;
bool g_bLoaded;
int g_nLoadTab;     // 0 all, 1 the campaign's, 2 the skirmishes'
int g_nSave;        // in the tab's list
bool g_bDeleteSave;
std::string g_loadNote;

// Over a game: which of its dialogs this is, the name Save saves under (and the one saved under
// last, which the next Save starts with, as 1.17 remembers it), a Replace asked for, and what
// waits for GameSavesStep.
enum { SAVES_MENU, SAVES_GAME_LOAD, SAVES_GAME_SAVE };
int g_nMode;
std::string g_name, g_lastName;
bool g_bReplaceSave;
enum { ASKED_NONE, ASKED_LOAD, ASKED_SAVE, ASKED_REPLACE };
int g_nAsked;
std::string g_asked;

const size_t kNameMost = 27;  // the characters 1.17's edit box takes (edit_key_evt)

// What write_save_header writes before the save's data, a line each: the game type, the map's
// title and its path, which for a campaign's mission is in the game's archives (campaign\...).
void ReadSave(Save &s)
{
  FILE *f = fopen(s.path.c_str(), "rb");
  if (f == NULL)
    return;
  char line[512];
  for (int n = 0; n < 24 && fgets(line, sizeof line, f) != NULL; ++n) {
    if (strchr(line, 0x1A) != NULL)
      break;
    std::string l = line;
    while (!l.empty() && (l.back() == '\r' || l.back() == '\n' || l.back() == ' '))
      l.pop_back();
    const size_t c = l.find(":  ");
    if (c == std::string::npos)
      continue;
    std::string key = l.substr(0, c), value = l.substr(c + 3);
    key.erase(0, key.find_first_not_of(' '));
    if (key == "Game Type")
      s.type = value;
    else if (key == "SCM Title")
      s.title = value;
    else if (key == "SCM Path")
      s.bCampaign = _strnicmp(value.c_str(), "campaign\\", 9) == 0;
  }
  fclose(f);
}

// The saves of the kind GetSaveExtension names: a single-player game's in the menus and in one,
// a multiplayer game's in one.
void ListSaves(void)
{
  g_saves.clear();
  char szDir[MAX_PATH];
  if (!MakeSaveDir(gLocalPlayerName, szDir, sizeof szDir, 0))
    return;
  const std::string dir = szDir;
  WIN32_FIND_DATAA fd;
  const HANDLE h = FindFirstFileA((dir + "*" + GetSaveExtension()).c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      continue;
    Save s;
    s.path = dir + fd.cFileName;
    s.name = fd.cFileName;
    const size_t nDot = s.name.rfind('.');
    if (nDot != std::string::npos)
      s.name.erase(nDot);
    s.when = fd.ftLastWriteTime;
    ReadSave(s);
    g_saves.push_back(s);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  std::sort(g_saves.begin(), g_saves.end(),
            [](const Save &a, const Save &b) { return CompareFileTime(&a.when, &b.when) > 0; });
}

std::vector<int> TabSaves(void)
{
  std::vector<int> v;
  for (size_t i = 0; i < g_saves.size(); ++i)
    if (g_nLoadTab == 0 || (g_nLoadTab == 1) == g_saves[i].bCampaign)
      v.push_back(static_cast<int>(i));
  return v;
}

void LoadSave(void)
{
  const std::vector<int> v = TabSaves();
  if (g_nSave < 0 || g_nSave >= static_cast<int>(v.size()))
    return;
  const Save &s = g_saves[v[g_nSave]];
  if (g_nMode == SAVES_GAME_LOAD) {
    g_nAsked = ASKED_LOAD;
    g_asked = s.name;
    return;
  }
  if (PortLaunchLoadFile(s.path.c_str())) {
    FrontLog("load game: %s\n", s.path.c_str());
    g_bLoadUp = false;
    g_bLoaded = true;
  } else {
    g_loadNote = PortText("That save could not be loaded.");
    FrontLog("load game: %s did not load\n", s.path.c_str());
  }
}

// Save under the name given, its trailing spaces left off, asking first when a save of that name
// is there.
void SaveName(void)
{
  std::string name = g_name;
  while (!name.empty() && name.back() == ' ')
    name.pop_back();
  if (name.empty())
    return;
  g_asked = name;
  for (const Save &s : g_saves)
    if (_stricmp(s.name.c_str(), name.c_str()) == 0) {
      g_bReplaceSave = true;
      return;
    }
  g_nAsked = ASKED_SAVE;
}

void DeleteSave(void)
{
  g_bDeleteSave = false;
  const std::vector<int> v = TabSaves();
  if (g_nSave < 0 || g_nSave >= static_cast<int>(v.size()))
    return;
  const Save &s = g_saves[v[g_nSave]];
  if (!sub_498B30(s.name.c_str())) {
    g_loadNote = PortText("That save could not be deleted.");
    return;
  }
  FrontLog("load game: %s deleted\n", s.path.c_str());
  ListSaves();
  const int n = static_cast<int>(TabSaves().size());
  g_nSave = g_nSave >= n ? n - 1 : g_nSave;
}

// The row picked, and for Save its name, as 1.17's list puts the name in the box.
void Pick(int k, const std::vector<int> &v)
{
  g_nSave = k;
  g_loadNote.clear();
  if (g_nMode == SAVES_GAME_SAVE && k >= 0 && k < static_cast<int>(v.size()))
    g_name = g_saves[v[k]].name.substr(0, kNameMost);
}

void SaveTab(int i, const char *pszLabel)
{
  Decl t = Box("lgtab", i);
  const bool bOn = i == g_nLoadTab;
  const bool bHot = Over(t.d.id);
  if (Clicked(t.d.id)) {
    g_nLoadTab = i;
    g_nSave = g_nMode == SAVES_GAME_SAVE ? -1 : 0;
  }
  t.d.layout.sizing.width = Grow();
  t.d.layout.sizing.height = Fixed(Sc(46));
  t.d.layout.padding = Pad(Fs(22), Fs(22), 0, 0);
  t.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  t.d.custom.customData = Custom(DrawScRaised, bOn ? kScTabOn : bHot ? kScTabHi : kScTab);
  Open(t);
  Text(pszLabel, UI_FACE_WIDE, Fs(16), 0.03f, bOn || bHot ? kScInkHi : kScInkButton);
  Close();
}

// Save's name box: the typing a file's name can hold, as 1.17's edit_key_evt takes it, no space to
// start it; none while a confirm is up.
void NameBox(bool bFocus)
{
  // a file's name: ASCII, as 1.17 names its files with Windows' ANSI calls
  g_typed.erase(std::remove_if(g_typed.begin(), g_typed.end(),
                               [](char c) { return strchr("\\/:*?\"<>|", c) != NULL || (c & 0x80) != 0; }),
                g_typed.end());
  while (g_name.empty() && !g_typed.empty() && g_typed[0] == ' ')
    g_typed.erase(0, 1);
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.childGap = Gap(Fs(16));
  row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(row);
  Text(PortText("Save as"), UI_FACE_BOLD, Fs(18), 0.0f, kScInkLabel);
  Decl edit = Box("lgname");
  edit.d.layout.sizing.width = Grow();
  edit.d.layout.sizing.height = Fixed(Sc(52));
  ScEdit(edit, &g_name, kNameMost, PortText("A name for the save"), bFocus);
  Close();
}

}  // namespace

namespace FrontKit {

void LoadGameOpen(void)
{
  ListSaves();
  g_bLoadUp = true;
  g_bLoaded = false;
  g_nMode = SAVES_MENU;
  g_nLoadTab = 0;
  g_nSave = 0;
  g_bDeleteSave = g_bReplaceSave = false;
  g_nAsked = ASKED_NONE;
  g_loadNote.clear();
  FrontLog("load game: %u saves of %s\n", static_cast<unsigned>(g_saves.size()), gLocalPlayerName);
}

void GameSavesOpen(bool bSave)
{
  LoadGameOpen();
  g_nMode = bSave ? SAVES_GAME_SAVE : SAVES_GAME_LOAD;
  if (bSave) {
    g_nSave = -1;
    g_name = g_lastName;
  }
}

void GameSavesClose(void)
{
  g_bLoadUp = g_bDeleteSave = g_bReplaceSave = false;
  g_nAsked = ASKED_NONE;
}

bool GameSavesStep(void)
{
  const int nAsked = g_nAsked;
  g_nAsked = ASKED_NONE;
  if (nAsked == ASKED_LOAD) {
    if (PortGameLoad(g_asked.c_str())) {
      FrontLog("load game: %s, from the game menu\n", g_asked.c_str());
      g_bLoadUp = false;
      return true;
    }
    g_loadNote = PortText("That save could not be loaded.");
  } else if (nAsked == ASKED_SAVE || nAsked == ASKED_REPLACE) {
    switch (PortGameSave(g_asked.c_str(), nAsked == ASKED_REPLACE)) {
      case PORT_SAVE_DONE:
        FrontLog("save game: %s\n", g_asked.c_str());
        g_lastName = g_asked;
        g_bLoadUp = false;
        return true;
      case PORT_SAVE_THERE:
        g_bReplaceSave = true;
        break;
      case PORT_SAVE_FULL:
        g_loadNote = PortText("The profile has 128 saved games, the most it keeps. Delete one to save another.");
        break;
      case PORT_SAVE_BAD_NAME:
        g_loadNote = PortText("A file cannot be named that. Try another name.");
        break;
      case PORT_SAVE_NOT_REPLACED:
        g_loadNote = PortText("The save of that name could not be replaced.");
        break;
    }
  }
  return false;
}

bool LoadGameUp(void)
{
  return g_bLoadUp;
}

bool LoadGameStarted(void)
{
  return g_bLoaded;
}

void LoadGameDialog(void)
{
  if (!g_bLoadUp)
    return;
  const bool bSave = g_nMode == SAVES_GAME_SAVE;
  const float lw = fminf(1545, UiWidth() / Sc(1) - 160), lh = fminf(925, UiHeight() / Sc(1) - 220);
  const bool bOut = DialogOpen("lgdlg", lw, lh, 150);
  DialogTitle(bSave ? PortText("SAVE GAME") : PortText("LOAD GAME"));
  Decl body;
  body.d.layout.sizing.width = Grow();
  body.d.layout.sizing.height = Grow();
  body.d.layout.childGap = Gap(Fs(24));
  Open(body);
  Decl tabs;
  tabs.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  tabs.d.layout.sizing.width = Fixed(Sc(320));
  tabs.d.layout.childGap = Gap(Fs(12));
  Open(tabs);
  SaveTab(0, PortText("All Saves"));
  SaveTab(1, PortText("Campaign"));
  SaveTab(2, PortText("Skirmish"));
  Close();
  Decl well = Box("lgwell");
  well.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  well.d.layout.sizing.width = Grow();
  well.d.layout.sizing.height = Grow();
  well.d.layout.padding = Pad(Fs(8), Fs(8), Fs(8), Fs(8));
  well.d.layout.childGap = Gap(Fs(4));
  well.d.clip.vertical = true;
  InnerBox(well);
  Open(well);
  const std::vector<int> v = TabSaves();
  if (g_nSave >= static_cast<int>(v.size()))
    g_nSave = static_cast<int>(v.size()) - 1;
  if (v.empty()) {
    Decl em;
    em.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    em.d.layout.sizing.width = Grow();
    em.d.layout.sizing.height = Grow();
    em.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    em.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    em.d.layout.childGap = Gap(Fs(10));
    Open(em);
    // {0} the profile's name
    Text(Keep(!g_saves.empty() ? std::string(PortText("No saves of this kind."))
                               : PortTextF("{0} has no saved games yet.", {gLocalPlayerName})),
         UI_FACE_BOLD, Fs(21), 0.0f, kLbInkInfo, -1, 0.0f, true);
    if (!bSave) {
      Decl line;
      line.d.layout.sizing.width = Fixed(Sc(560));
      Open(line);
      Text(PortText("Save during a mission or a skirmish with F10, Save Game. Saves belong to the profile that made "
                    "them, as the original kept them in save\\<ID>\\."),
           UI_FACE_TEXT, Fs(17), 0.0f, kLbInkDesc, -1, 1.45f, true);
      Close();
    }
    Close();
  }
  // the rows from the one picked back, so it shows; Save's name box takes a row's room
  const int nFit = static_cast<int>((lh - 75 - 120 - 16 - (bSave ? 76 : 0)) / 82);
  const int nFirst = g_nSave >= nFit ? g_nSave - nFit + 1 : 0;
  for (int k = nFirst; k < static_cast<int>(v.size()); ++k) {
    const Save &s = g_saves[v[k]];
    Decl row = Box("lgrow", k);
    const bool bOn = k == g_nSave;
    const bool bHot = Over(row.d.id);
    if (Clicked(row.d.id)) {
      Pick(k, v);
      if (DoubleClicked(row.d.id)) {
        if (bSave)
          SaveName();
        else
          LoadSave();
      }
    }
    row.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    row.d.layout.sizing.width = Grow();
    row.d.layout.padding = Pad(Fs(16), Fs(16), Fs(12), Fs(12));
    row.d.layout.childGap = Gap(Fs(3));
    row.d.backgroundColor = Col(bOn || bHot ? kLbItemHi : kLbItem);
    row.d.border = Border(bOn ? kScButtonHi.rim : kNone, 1, 1, 1, 1);
    Radius(row, 3);
    Open(row);
    Text(Keep(s.name), UI_FACE_BOLD, Fs(19), 0.0f, bOn || bHot ? kScInkHi : kLbInkInfo);
    std::string what = s.bCampaign ? std::string(PortText("Campaign"))
                       : s.type.empty() ? std::string(PortText("Skirmish"))
                                        : PortText("Skirmish") + std::string(kDot) + s.type;
    if (!s.title.empty())
      what += kDot + s.title;
    Text(Keep(what), UI_FACE_TEXT, Fs(15), 0.0f, kLbInkDesc);
    // {0} when the game was saved
    Text(Keep(PortTextF("Saved {0}", {FileDate(s.when)})), UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff);
    Close();
  }
  Close();
  Close();
  if (bSave)
    NameBox(!g_bDeleteSave && !g_bReplaceSave);
  Decl foot;
  foot.d.layout.sizing.width = Grow();
  foot.d.layout.childGap = Gap(Fs(12));
  foot.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(foot);
  const bool bPicked = g_nSave >= 0 && g_nSave < static_cast<int>(v.size());
  Decl lb = Box("lgload");
  lb.d.layout.sizing.width = Fixed(Sc(275));
  lb.d.layout.sizing.height = Fixed(Sc(52));
  const bool bLoad = bSave ? ScButton(lb, PortText("Save"), 0, true, !g_name.empty())
                           : ScButton(lb, PortText("Load"), 'L', true, !v.empty());
  Decl db = Box("lgdelete");
  db.d.layout.sizing.width = Fixed(Sc(200));
  db.d.layout.sizing.height = Fixed(Sc(52));
  const bool bDelete = ScButton(db, PortText("Delete"), bSave ? 0 : 'D', false, bPicked);
  Decl cb = Box("lgclose");
  cb.d.layout.sizing.width = Fixed(Sc(200));
  cb.d.layout.sizing.height = Fixed(Sc(52));
  const bool bClose =
      ScButton(cb, g_nMode == SAVES_MENU ? PortText("Close") : PortText("Cancel"), bSave ? 0 : 'C', false);
  if (!g_loadNote.empty())
    Text(Keep(g_loadNote), UI_FACE_TEXT, Fs(15), 0.0f, kScInkAccept);
  Spacer();
  // {0} the profile's name
  Text(Keep(PortTextF("Saves of {0}", {gLocalPlayerName}) + kDot + (gbIsExpansion ? "Brood War" : "StarCraft")),
       UI_FACE_TEXT, Fs(15), 0.0f, kLbInkDesc);
  Close();
  Close();  // the dialog
  if (g_bDeleteSave && bPicked) {
    const char *const kButtons[] = {PortText("Delete"), PortText("Cancel")};
    // {0} the save's name
    const int n = FrontDialog(Keep(PortTextF("Delete {0}?", {g_saves[v[g_nSave]].name})),
                              PortText("The saved game is deleted. Your campaign progress stays."), kButtons, "DC", 2);
    if (n == 0)
      DeleteSave();
    else if (n == 1)
      g_bDeleteSave = false;
    return;
  }
  if (g_bReplaceSave) {
    const char *const kButtons[] = {PortText("Replace"), PortText("Cancel")};
    // {0} the name the game would be saved under
    const int n = FrontDialog(Keep(PortTextF("Replace {0}?", {g_asked})),
                              PortText("A saved game of that name is there already. Saving replaces it."), kButtons,
                              "RC", 2);
    if (n == 0)
      g_nAsked = ASKED_REPLACE;
    if (n >= 0)
      g_bReplaceSave = false;
    return;
  }
  if (bLoad && bSave)
    SaveName();
  else if (bLoad)
    LoadSave();
  else if (bDelete)
    g_bDeleteSave = true;
  else if (bClose || bOut)
    g_bLoadUp = false;
}

// The dialog's keys. Save's are the name box's, so it has no letters: Enter saves and Escape goes
// back, as in 1.17's.
bool LoadGameKeys(void)
{
  if (!g_bLoadUp)
    return false;
  if (g_bDeleteSave || g_bReplaceSave) {
    if (Key(VK_ESCAPE))
      g_bDeleteSave = g_bReplaceSave = false;
    return true;
  }
  const std::vector<int> v = TabSaves();
  const int n = static_cast<int>(v.size());
  if (Key(VK_DOWN) && g_nSave + 1 < n)
    Pick(g_nSave + 1, v);
  else if (Key(VK_UP) && g_nSave > 0)
    Pick(g_nSave - 1, v);
  else if (g_nMode == SAVES_GAME_SAVE) {
    if (Key(VK_RETURN))
      SaveName();
    else if (Key(VK_ESCAPE))
      g_bLoadUp = false;
  } else if (Key('L') || Key(VK_RETURN))
    LoadSave();
  else if (Key('D') && n > 0)
    g_bDeleteSave = true;
  else if (Key('C') || Key(VK_ESCAPE))
    g_bLoadUp = false;
  return true;
}

}  // namespace FrontKit
