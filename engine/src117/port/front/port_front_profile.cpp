// port_front_profile.cpp -- the player card, Create a Profile, the profiles dialog and Import
// StarCraft IDs (docs/modules/front-end.md). Presentation only.
//
// A profile is 1.17's single-player ID: its character file in characters\ (.spc), which keeps
// the campaign's progress, and its saved games under save\. "hyperion"/"profile" names the
// one chosen; Campaign, Skirmish and Replays ask for one first (ProfileGate), as 1.17's screen
// did. Hyperion's own part, profiles\<name>\profile.json, holds the race last played, the
// games started and the 1.17 ID it came from. The progress stays in the character file, which
// 1.17's campaign and score screens use, not in profile.json as the design has it (campaign.js
// PATHS). Imported IDs, from the install and Documents\StarCraft\characters\ (1.17's and
// Remastered's), are read with 1.17's own reader, never written.

#include <windows.h>
#include <shlobj.h>      // SHGetFolderPathA
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "clay.h"
#include "port_frontkit.h"
#include "port_frontlobby.h"  // DialogOpen, Pulldown, the boxes
#include "port_frontscreens.h"
#include "../platform/port_json.h"
#include "../platform/port_paths.h"  // PortDataFolder
#include "../platform/port_settings.h"
#include "port_ui.h"
#include "oc_data.h"
#include "charactr.h"    // EnumCharacterFiles, CreateNewCharacterFile, WriteCharacterFile, DeleteCharacterFile
#include "gluCmpgn.h"    // ReadCharacterFile
#include "net_glue.h"    // SetLocalPlayerName
#include "net_cmds.h"    // is_bad_filename
#include "saveload.h"    // MakeSaveDir, DeleteSave, decompress
#include "fd_user.h"     // pfopen, pfclose
#include "gds_file.h"    // pfseek

namespace FrontKit {

namespace {

// What the card is doing.
enum Form { FORM_NONE, FORM_NEW, FORM_RENAME, FORM_FIRST };

// What profile.json keeps.
struct Extra {
  std::string lastRace;  // "zerg", "terran", "protoss", "random", a mod race's name; "" none yet
  int nGames = 0;        // the games it started from Hyperion's screens
  std::string from;      // the 1.17 ID it was imported from, "" for none
};

struct Profile {
  std::string name;
  DWORD dwStamp;  // when it was made: 1.17 lists IDs in that order
  BYTE rec[0x70];  // its character file
  Extra x;
};

bool g_bOpen;                      // the card is down
int g_nForm = FORM_NONE;
std::string g_edit;                // the name being typed
std::string g_msg;                 // why it cannot be that name
int g_nDelete = -1;                // the profile whose deletion is being asked
std::vector<Profile> g_list;
const char *g_pszThen;             // what Create a Profile goes on to (ProfileGate)
const char *g_pszThenDone;         // made: ProfileThen hands it over once
bool g_bChanged;                   // the profile changed: ProfileChanged says so once
std::string g_formFor;             // the profile Rename renames
bool g_bManage;                    // the profiles dialog is up
int g_nSel;                        // the profile it has picked
bool g_bImport;                    // Import StarCraft IDs is up
Uint64 g_nLastClick;
int g_nLastClickRow = -1;

const UiColor kDoneInk = KitRgb(0x7bd88f);  // a finished mission (campaign.css --cp-done)

// The 0x70-byte character record (charactr.cpp's charrec_t), as far as this file reads it.
struct CharRec {
  DWORD dwMagic;
  BYTE bMultiPlayer;
  BYTE abPad[3];
  DWORD dwStamp;
  char szName[0x19];
  BYTE abRest[0x70 - 0x25];
};
static_assert(sizeof(CharRec) == 0x70, "CharRec");

void __stdcall Found(void *pRec)
{
  const CharRec *p = static_cast<const CharRec *>(pRec);
  if (p->bMultiPlayer != 0)
    return;
  Profile pr;
  pr.name.assign(p->szName, strnlen(p->szName, sizeof p->szName));
  pr.dwStamp = p->dwStamp;
  memcpy(pr.rec, pRec, sizeof pr.rec);
  g_list.push_back(pr);
}

// ---------------------------------------------------------------------------
// profile.json, in the data folder's profiles\<name>\.

std::string ExtraFolder(const std::string &name)
{
  return std::string(PortDataFolder()) + "profiles\\" + name;
}

Extra ExtraRead(const std::string &name)
{
  Extra x;
  FILE *f = NULL;
  if (name.empty() || fopen_s(&f, (ExtraFolder(name) + "\\profile.json").c_str(), "rb") != 0 || f == NULL)
    return x;
  std::string text;
  char buf[1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0)
    text.append(buf, n);
  fclose(f);
  PortJson j;
  if (!PortJsonParse(text.c_str(), &j))
    return x;
  x.lastRace = j["lastRace"].Str();
  x.nGames = static_cast<int>(j["games"].Num());
  x.from = j["importedFrom"].Str();
  return x;
}

void ExtraWrite(const std::string &name, const Extra &x)
{
  CreateDirectoryA((std::string(PortDataFolder()) + "profiles").c_str(), NULL);
  CreateDirectoryA(ExtraFolder(name).c_str(), NULL);
  const std::string path = ExtraFolder(name) + "\\profile.json";
  FILE *f = NULL;
  if (fopen_s(&f, path.c_str(), "wb") != 0 || f == NULL) {
    FrontLog("profile: %s could not be written\n", path.c_str());
    return;
  }
  std::string t = "{\r\n  \"name\": " + PortJsonQuote(name) + ",\r\n  \"lastRace\": " + PortJsonQuote(x.lastRace) +
                  ",\r\n  \"games\": " + std::to_string(x.nGames);
  if (!x.from.empty())
    t += ",\r\n  \"importedFrom\": " + PortJsonQuote(x.from);
  t += "\r\n}\r\n";
  fwrite(t.data(), 1, t.size(), f);
  fclose(f);
}

void Refresh(void)
{
  g_list.clear();
  EnumCharacterFiles(Found);
  std::stable_sort(g_list.begin(), g_list.end(),
                   [](const Profile &a, const Profile &b) { return a.dwStamp < b.dwStamp; });
  for (Profile &pr : g_list)
    pr.x = ExtraRead(pr.name);
}

bool SameName(const std::string &a, const std::string &b)
{
  return a.size() == b.size() && _stricmp(a.c_str(), b.c_str()) == 0;
}

int IndexOf(const std::string &name)
{
  for (size_t i = 0; i < g_list.size(); ++i)
    if (SameName(g_list[i].name, name))
      return static_cast<int>(i);
  return -1;
}

std::string Trim(const std::string &s)
{
  size_t a = 0, b = s.size();
  while (a < b && s[a] == ' ')
    ++a;
  while (b > a && s[b - 1] == ' ')
    --b;
  return s.substr(a, b - a);
}

// Why a name cannot be a profile's, or "" when it can (front.js dNameError, and 1.17's own
// rules, since the name is a file's: gluLogin_new_id, is_bad_filename).
std::string NameError(const std::string &raw, const std::string &except)
{
  const std::string name = Trim(raw);
  if (name.empty())
    return PortText("Give the profile a name.");
  if (name.size() > 24)
    return PortText("At most 24 letters.");
  if (name.find_first_of("*\\/:><|&+^?\"-") != std::string::npos)
    return PortText("A name cannot hold * \\ / : > < | & + ^ ? \" or -.");
  static const char *const kDevices[] = {"con", "prn", "aux", "nul"};
  // {0} the name that was typed
  for (const char *d : kDevices)
    if (_stricmp(name.c_str(), d) == 0)
      return PortTextF("{0} is a name Windows keeps for itself.", {name});
  if (name.size() == 4 && (_strnicmp(name.c_str(), "com", 3) == 0 || _strnicmp(name.c_str(), "lpt", 3) == 0) &&
      name[3] >= '0' && name[3] <= '9')
    return PortTextF("{0} is a name Windows keeps for itself.", {name});
  if (is_bad_filename(name.c_str(), 0x19, 0))
    return PortText("That name cannot be a file's.");
  if (except.empty() && g_list.size() >= 250)
    return PortText("There are 250 profiles, the most there can be.");
  for (const Profile &p : g_list)
    if (SameName(p.name, name) && !SameName(p.name, except))
      return PortTextF("There is already a profile called {0}.", {name});
  return "";
}

void Choose(const std::string &name)
{
  PortSettingSetString("hyperion", "profile", name.c_str());
  SetLocalPlayerName(name.c_str());
  g_bChanged = true;
  FrontLog("profile: %s chosen\n", name.c_str());
}

bool Make(const std::string &raw)
{
  const std::string name = Trim(raw);
  g_msg = NameError(name, "");
  if (!g_msg.empty())
    return false;
  CharRec rec;
  if (!CreateNewCharacterFile(name.c_str(), &rec)) {
    g_msg = PortText("The profile could not be saved.");
    return false;
  }
  FrontLog("profile: %s made\n", name.c_str());
  Refresh();
  Choose(name);
  g_nSel = IndexOf(name);
  return true;
}

// A new name for a profile: its character file written under the new name, its saved games'
// folder moved with it, then the old file deleted.
bool Rename(const std::string &from, const std::string &raw)
{
  const std::string name = Trim(raw);
  if (name == from)
    return true;
  g_msg = NameError(name, from);
  if (!g_msg.empty())
    return false;
  const bool bCurrent = SameName(from, ProfileCurrent());
  CharRec rec;
  char szFrom[0x19];
  SDL_strlcpy(szFrom, from.c_str(), sizeof szFrom);
  if (!ReadCharacterFile(szFrom, &rec)) {
    g_msg = PortText("The profile could not be read.");
    return false;
  }
  SDL_zero(rec.szName);
  SDL_strlcpy(rec.szName, name.c_str(), sizeof rec.szName);
  if (!WriteCharacterFile(&rec)) {
    g_msg = PortText("The profile could not be saved.");
    return false;
  }
  char szOld[MAX_PATH], szNew[MAX_PATH];
  if (MakeSaveDir(from.c_str(), szOld, sizeof szOld, FALSE) && MakeSaveDir(name.c_str(), szNew, sizeof szNew, FALSE)) {
    szOld[strlen(szOld) - 1] = '\0';  // MakeSaveDir ends them with a backslash
    szNew[strlen(szNew) - 1] = '\0';
    if (GetFileAttributesA(szOld) != INVALID_FILE_ATTRIBUTES && GetFileAttributesA(szNew) == INVALID_FILE_ATTRIBUTES)
      MoveFileA(szOld, szNew);
  }
  // only a case change leaves one file, which the write already renamed
  if (!SameName(from, name))
    DeleteCharacterFile(from.c_str());
  // profile.json goes with it, its name in it the new one
  const Extra x = ExtraRead(from);
  MoveFileA(ExtraFolder(from).c_str(), ExtraFolder(name).c_str());
  ExtraWrite(name, x);
  FrontLog("profile: %s renamed %s\n", from.c_str(), name.c_str());
  Refresh();
  if (bCurrent)
    Choose(name);
  g_nSel = IndexOf(name);
  return true;
}

void Delete(int n)
{
  if (n < 0 || n >= static_cast<int>(g_list.size()))
    return;
  const std::string name = g_list[n].name;
  const bool bCurrent = SameName(name, ProfileCurrent());
  DeleteSave(name.c_str());
  DeleteCharacterFile(name.c_str());
  // its folder goes only when nothing else is in it
  DeleteFileA((ExtraFolder(name) + "\\profile.json").c_str());
  RemoveDirectoryA(ExtraFolder(name).c_str());
  FrontLog("profile: %s deleted\n", name.c_str());
  Refresh();
  if (bCurrent) {
    if (!g_list.empty()) {
      Choose(g_list[0].name);
    } else {
      PortSettingSetString("hyperion", "profile", "");
      g_bChanged = true;
    }
  }
}

void FormOpen(int nForm, const std::string &start)
{
  g_nForm = nForm;
  g_typed.clear();  // the letter of the key that opened it is not the name's first
  g_edit = start;
  g_formFor = start;
  g_msg.clear();
}

// ---------------------------------------------------------------------------
// The pieces.

// The face: the profile's initial in the avatar's rings, 64 units (front.css .d-pf-face).
void DrawFace(float x, float y, float w, float h, void *)
{
  const float u = Sc(1.0f);
  UiGradient(x + 6 * u, y + 6 * u, w - 12 * u, h - 12 * u, kScAvaFaceTop, kScAvaFaceBot, false, 3 * u);
  UiRing(x, y, w, h, kScAvaOuter, 6 * u, 2 * u);
  UiRing(x + 3 * u, y + 3 * u, w - 6 * u, h - 6 * u, kScAvaInner, 4 * u, 2 * u);
}

// The form under the head or the list: the box, then Create or Save and Cancel, and why the
// name will not do.
void NameForm(const char *pszOk)
{
  Decl f;
  f.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  f.d.layout.sizing.width = Grow();
  f.d.layout.childGap = Gap(Fs(10));
  Open(f);
  Decl box;
  box.d.id = Id("pfedit");
  box.d.layout.sizing.width = Grow();
  box.d.layout.sizing.height = Fixed(Sc(54));
  ScEdit(box, &g_edit, 24, PortText("Name"));
  Decl row;
  row.d.layout.childGap = Gap(Fs(10));
  Open(row);
  Decl ok;
  ok.d.id = Id("pfok");
  ok.d.layout.sizing.width = Fixed(Sc(150));
  ok.d.layout.sizing.height = Fixed(Sc(48));
  const bool bOk = ScButton(ok, pszOk, 0, true) || Key(VK_RETURN);
  Decl no;
  no.d.id = Id("pfno");
  no.d.layout.sizing.width = Fixed(Sc(150));
  no.d.layout.sizing.height = Fixed(Sc(48));
  const bool bNo = ScButton(no, PortText("Cancel"), 0, false);
  Close();
  if (!g_msg.empty())
    Text(Keep(g_msg), UI_FACE_TEXT, Fs(16), 0.0f, kScInkAccept, -1, 1.3f);
  Close();
  if (bNo) {
    FormOpen(FORM_NONE, "");
  } else if (bOk) {
    const bool bDone = g_nForm == FORM_NEW ? Make(g_edit) : Rename(g_formFor, g_edit);
    if (bDone)
      FormOpen(FORM_NONE, "");
  }
}

// The player card (front.js profileCard, front.css .d-profile): SC2's party panel under the
// card tab, 422 units wide. The profile with Rename, then every profile on this computer
// (choose one to switch, x to delete it), New Profile, and Manage Profiles.
void Card(void)
{
  const std::string me = ProfileCurrent();
  Decl p;
  p.d.id = Id("pfcard");
  p.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  p.d.layout.sizing.width = Fixed(Sc(422));
  p.d.layout.padding = Pad(Fs(22), Fs(22), Fs(22), Fs(24));
  p.d.layout.childGap = Gap(Fs(18));
  p.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  p.d.floating.attachPoints.element = CLAY_ATTACH_POINT_RIGHT_TOP;
  p.d.floating.attachPoints.parent = CLAY_ATTACH_POINT_RIGHT_TOP;
  p.d.floating.offset.x = -Sc(11);
  p.d.floating.offset.y = Sc(130);
  p.d.floating.zIndex = 70;
  static UiCustom frame = {0.0f, DrawScFrame, const_cast<UiColor *>(&kScFillSolid)};
  p.d.custom.customData = &frame;
  Open(p);

  if (me.empty()) {
    Decl t;
    t.d.layout.sizing.width = Grow();
    Open(t);
    Text(PortText("No profile yet. Make one to play: it keeps your campaign and saved games."), UI_FACE_TEXT, Fs(18),
         0.0f, kScInkMsg, -1, 1.35f);
    Close();
  } else if (g_nForm == FORM_RENAME) {
    NameForm(PortText("Save"));
  } else {
    Decl head;
    head.d.layout.sizing.width = Grow();
    head.d.layout.childGap = Gap(Fs(16));
    head.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(head);
    Decl face;
    face.d.layout.sizing.width = Fixed(Sc(64));
    face.d.layout.sizing.height = Fixed(Sc(64));
    face.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    face.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    static UiCustom faceDraw = {0.0f, DrawFace, NULL};
    face.d.custom.customData = &faceDraw;
    Open(face);
    char szInitial[2] = {me[0] >= 'a' && me[0] <= 'z' ? static_cast<char>(me[0] - 32) : me[0], 0};
    Text(Keep(szInitial), UI_FACE_WIDE, Fs(30), 0.0f, kScInkNav);
    Close();
    Decl who;
    who.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    who.d.layout.sizing.width = Grow();
    who.d.layout.childGap = Gap(Fs(4));
    Open(who);
    Text(Keep(me), UI_FACE_WIDE, Fs(22), 0.0f, kScInkHead);
    Text(PortText("Local profile"), UI_FACE_TEXT, Fs(16), 0.0f, kScInkLabel);
    Close();
    Decl ren;
    ren.d.id = Id("pfrename");
    ren.d.layout.sizing.height = Fixed(Sc(36));
    if (ScMini(ren, PortText("Rename"), 'N'))
      FormOpen(FORM_RENAME, me);
    Close();
  }

  // every profile
  Decl sec;
  sec.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  sec.d.layout.sizing.width = Grow();
  sec.d.layout.childGap = Gap(Fs(10));
  sec.d.layout.padding = Pad(0, 0, Fs(16), 0);
  sec.d.border = Border(kScWellRim, 0, 0, 1, 0);
  Open(sec);
  Text(PortText("Profiles on this computer"), UI_FACE_BOLD, Fs(18), 0.0f, kScInkLabel);
  for (size_t i = 0; i < g_list.size(); ++i) {
    const bool bMe = SameName(g_list[i].name, me);
    const Clay_ElementId rowId = Id("pfrow", static_cast<int>(i));
    const Clay_ElementId xId = Id("pfx", static_cast<int>(i));
    const bool bHot = Over(rowId) && !Over(xId);
    if (Clicked(rowId) && !Over(xId) && !bMe && g_nForm == FORM_NONE)
      Choose(g_list[i].name);
    Decl r;
    r.d.id = rowId;
    r.d.layout.sizing.width = Grow();
    r.d.layout.sizing.height = Fixed(Sc(46));
    r.d.layout.padding = Pad(Fs(14), Fs(52), 0, 0);
    r.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    r.d.backgroundColor = Col(bMe || bHot ? kScMenuSel : kNone);
    r.d.border = Border(bMe ? kScTabOn.rim : kNone, 1, 1, 1, 1);
    Open(r);
    Text(Keep(g_list[i].name), UI_FACE_TEXT, Fs(18), 0.0f, bMe ? kScInkHi : kScInkButton);
    Decl grow;
    grow.d.layout.sizing.width = Grow();
    Open(grow);
    Close();
    if (bMe)
      Text(PortText("Playing"), UI_FACE_TEXT, Fs(16), 0.0f, kScInkOff);
    // the x: delete, after asking
    Decl x = Floating("pfx", static_cast<int>(i), -Sc(6), 0.0f, 71, CLAY_ATTACH_POINT_RIGHT_CENTER,
                      CLAY_ATTACH_POINT_RIGHT_CENTER);
    x.d.id = xId;
    x.d.layout.sizing.width = Fixed(Sc(34));
    x.d.layout.sizing.height = Fixed(Sc(34));
    x.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    x.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    const bool bXHot = Over(xId);
    x.d.custom.customData = Custom(DrawScRaised, bXHot ? kScTabHi : kScTab);
    if (Clicked(xId) && g_nForm == FORM_NONE)
      g_nDelete = static_cast<int>(i);
    Open(x);
    Text("\xC3\x97", UI_FACE_TEXT, Fs(24), 0.0f, bXHot ? kScInkHi : kScInkButton);
    Close();
    Close();
  }
  if (g_nForm == FORM_NEW) {
    NameForm(PortText("Create"));
  } else {
    Decl nb;
    nb.d.id = Id("pfnew");
    nb.d.layout.sizing.width = Fixed(Sc(250));
    nb.d.layout.sizing.height = Fixed(Sc(48));
    if (ScButton(nb, PortText("New Profile"), 'W', false))
      FormOpen(FORM_NEW, "");
  }
  // the whole of every profile (campaign.js's profiles dialog)
  Decl mg;
  mg.d.id = Id("pfmanage");
  mg.d.layout.sizing.width = Fixed(Sc(320));
  mg.d.layout.sizing.height = Fixed(Sc(48));
  if (ScButton(mg, PortText("Manage Profiles"), 'G', false))
    ProfilesOpen();
  Close();

  Close();
}

// ---------------------------------------------------------------------------
// 1.17's IDs on this computer (campaign.js importIds).

struct FoundId {
  std::string name;    // its file's name less .spc, which 1.17 takes for the ID's
  std::string file;    // the file
  std::string folder;  // where it was found
  DWORD cb;
  bool bRead;          // a 1.17 character record, read
  CharRec rec;
  bool bOn;            // to be imported
  int nHow;            // when a profile has its name: 0 a new profile, 1 its progress added, 2 skipped
  bool bSame;          // that profile has the same progress already
};
std::vector<FoundId> g_found;
std::string g_importNote;

// A character file as 1.17 writes one (WriteCharacterFile): a single compressed block, its
// check word, a count of 1, then its length and bytes; read as ReadCharacterFile reads one,
// an older record of 0x34 bytes too.
bool ReadId(const std::string &path, DWORD cb, CharRec *p)
{
  if (cb < 12 || cb > 0x400)
    return false;
  DWORD head[3] = {};
  DWORD nRead = 0;
  HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE)
    return false;
  const BOOL bHead = ReadFile(h, head, sizeof head, &nRead, NULL);
  CloseHandle(h);
  if (!bHead || nRead != sizeof head || head[1] != 1 || head[2] + 12 != cb)
    return false;
  void *pFile = pfopen(path.c_str(), 1);
  if (pFile == NULL)
    return false;
  memset(p, 0, sizeof *p);
  int nOk = decompress(p, 0x70, pFile);
  if (nOk == 0 && pfseek(pFile, 0, 0) == 0) {
    nOk = decompress(p, 0x34, pFile);
    if (nOk != 0)
      memset(reinterpret_cast<BYTE *>(p) + 0x34, 0, 0x3C);
  }
  pfclose(pFile);
  return nOk != 0 && p->dwMagic == 0x112233 && p->bMultiPlayer == 0;
}

bool SameProgress(const BYTE *a, const BYTE *b)
{
  return memcmp(a + 0x28, b + 0x28, 0x18) == 0;
}

void ScanFolder(const std::string &folder)
{
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA((folder + "*.spc").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;
  do {
    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
      continue;
    FoundId f;
    f.file = folder + fd.cFileName;
    f.folder = folder;
    f.name = fd.cFileName;
    f.name.resize(f.name.size() - 4);
    f.cb = fd.nFileSizeLow;
    f.bRead = f.name.size() < 0x19 && ReadId(f.file, f.cb, &f.rec);
    // the same ID in both places is listed once
    bool bTwice = false;
    for (const FoundId &g : g_found)
      bTwice = bTwice || (f.bRead && g.bRead && SameName(f.name, g.name) &&
                          SameProgress(reinterpret_cast<const BYTE *>(&f.rec), reinterpret_cast<const BYTE *>(&g.rec)));
    if (!bTwice)
      g_found.push_back(f);
  } while (FindNextFileA(h, &fd) != 0);
  FindClose(h);
}

// Where 1.17's IDs are: characters\ where StarCraft is installed (the folder Hyperion.exe
// runs from) and Documents\StarCraft\characters\, unless that is the data folder's own.
void Scan(void)
{
  g_found.clear();
  const std::string mine = std::string(PortDataFolder()) + "characters\\";
  char sz[MAX_PATH];
  const std::string install = std::string(PortProgramFolder()) + "characters\\";
  if (_stricmp(install.c_str(), mine.c_str()) != 0)
    ScanFolder(install);
  if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, sz))) {
    const std::string docs = std::string(sz) + "\\StarCraft\\characters\\";
    if (_stricmp(docs.c_str(), mine.c_str()) != 0)
      ScanFolder(docs);
  }
  for (FoundId &f : g_found) {
    const int nClash = f.bRead ? IndexOf(f.name) : -1;
    f.bSame = nClash >= 0 && SameProgress(g_list[nClash].rec, reinterpret_cast<const BYTE *>(&f.rec));
    f.nHow = f.bSame ? 2 : 0;
    f.bOn = f.bRead && !f.bSame;
  }
}

int FoundCount(void)
{
  int n = 0;
  for (const FoundId &f : g_found)
    n += f.bRead;
  return n;
}

// "2 StarCraft IDs found on this computer (Tassadar, Duran), with the missions each reached."
std::string FoundLine(void)
{
  std::string names;
  for (const FoundId &f : g_found)
    if (f.bRead)
      names += (names.empty() ? "" : ", ") + f.name;
  const int n = FoundCount();
  // {0} how many were found, {1} their names
  return PortTextNF("{0} StarCraft ID found on this computer ({1}), with the missions it reached.",
                    "{0} StarCraft IDs found on this computer ({1}), with the missions each reached.", n,
                    {std::to_string(n), names});
}

std::string Made(DWORD dwStamp)
{
  const __time64_t t = dwStamp;
  struct tm tm;
  if (dwStamp == 0 || _localtime64_s(&tm, &t) != 0)
    return "";
  char sz[16];
  strftime(sz, sizeof sz, "%Y-%m-%d", &tm);
  return sz;
}

// "I done, II 3/10" (with a middle dot between), or "No campaign played" (campaign.js progressLine).
std::string ProgressLine(const BYTE *pRec)
{
  std::string s;
  for (int i = 0; i < CampaignEpisodes(); ++i) {
    int nTotal = 0;
    const int nDone = CampaignDone(pRec, i, &nTotal);
    if (nDone == 0)
      continue;
    std::string num = CampaignEpisodeNumber(i);
    if (num.compare(0, 8, "Episode ") == 0)
      num.erase(0, 8);
    // {0} the episode's number
    s += (s.empty() ? "" : kDot) +
         (nDone >= nTotal ? PortTextF("{0} done", {num})
                          : num + " " + std::to_string(nDone) + "/" + std::to_string(nTotal));
  }
  return s.empty() ? PortText("No campaign played") : s;
}

void ImportOpen(void)
{
  Refresh();
  Scan();
  g_importNote.clear();
  g_lbOpen.clear();
  g_bImport = true;
}

void ImportClose(void)
{
  g_bImport = false;
  g_lbOpen.clear();
}

// Each ID picked becomes a profile, its character file written into the data folder with
// the stamp it was made with; one whose name a profile has comes in as "<name> (Classic)", or
// adds its progress to that profile (each race's furthest mission the further of the two).
void ImportRun(void)
{
  int nMade = 0;
  std::string first;
  for (const FoundId &f : g_found) {
    if (!f.bRead || !f.bOn)
      continue;
    const int nClash = IndexOf(f.name);
    if (nClash >= 0 && f.nHow == 2)
      continue;
    if (nClash >= 0 && f.nHow == 1) {
      CharRec rec;
      memcpy(&rec, g_list[nClash].rec, sizeof rec);
      BYTE *pMine = reinterpret_cast<BYTE *>(&rec) + 0x28;
      const BYTE *pTheirs = reinterpret_cast<const BYTE *>(&f.rec) + 0x28;
      for (int k = 0; k < 6; ++k) {
        DWORD a, b;
        memcpy(&a, pMine + 4 * k, 4);
        memcpy(&b, pTheirs + 4 * k, 4);
        if (b > a)
          memcpy(pMine + 4 * k, &b, 4);
      }
      if (WriteCharacterFile(&rec))
        FrontLog("profile: %s's progress added to %s\n", f.file.c_str(), g_list[nClash].name.c_str());
      Refresh();
      continue;
    }
    std::string name = f.name;
    if (nClash >= 0)
      name = f.name.substr(0, 17) + " (Classic)";
    const std::string why = NameError(name, "");
    if (!why.empty()) {
      FrontLog("profile: %s not imported: %s\n", f.file.c_str(), why.c_str());
      continue;
    }
    CharRec rec = f.rec;
    SDL_zero(rec.szName);
    SDL_strlcpy(rec.szName, name.c_str(), sizeof rec.szName);
    if (!WriteCharacterFile(&rec)) {
      FrontLog("profile: %s could not be written as %s\n", f.file.c_str(), name.c_str());
      continue;
    }
    Extra x;
    x.from = f.file;
    ExtraWrite(name, x);
    FrontLog("profile: %s imported as %s\n", f.file.c_str(), name.c_str());
    Refresh();
    ++nMade;
    if (first.empty())
      first = name;
  }
  Refresh();
  if (ProfileCurrent().empty() && !g_list.empty())
    Choose(first.empty() ? g_list[0].name : first);
  g_nSel = IndexOf(first.empty() ? ProfileCurrent() : first);
  if (g_nSel < 0)
    g_nSel = 0;
  ImportClose();
  // from the first run: a profile now, so on to where the player was going
  if (g_nForm == FORM_FIRST && !ProfileCurrent().empty()) {
    FormOpen(FORM_NONE, "");
    g_pszThenDone = g_pszThen;
    g_pszThen = NULL;
  }
}

int ImportCount(void)
{
  int n = 0;
  for (const FoundId &f : g_found)
    n += f.bRead && f.bOn && !(IndexOf(f.name) >= 0 && f.nHow == 2);
  return n;
}

// Import StarCraft IDs: each ID found, with what becomes of it, and the files found that are
// not IDs, listed and left alone. 1180 by 760 units, from 180 down; over the first run's
// dialog when it came from there.
void ImportDialog(void)
{
  const float dw = fminf(1180, UiWidth() / Sc(1) - 200), dh = fminf(760, UiHeight() / Sc(1) - 260);
  const bool bOut = DialogOpen("pfimport", dw, dh, 180, g_nForm == FORM_FIRST ? 82 : 74);
  DialogTitle(PortText("IMPORT STARCRAFT IDS"));
  std::string folders;
  for (const FoundId &f : g_found)
    if (f.bRead && folders.find(f.folder) == std::string::npos)
      folders = folders.empty() ? f.folder : PortTextF("{0} and {1}", {folders, f.folder});
  const int nRead = FoundCount();
  bool bAllHere = nRead > 0;
  for (const FoundId &f : g_found)
    bAllHere = bAllHere && (!f.bRead || f.bSame);
  Decl intro;
  intro.d.layout.sizing.width = Grow();
  Open(intro);
  // {0} the folders they were found in
  Text(Keep(bAllHere ? PortTextF("Every ID found in {0} is a profile here already, with the same progress. Tick one "
                                 "to bring it in again under another name.",
                                 {folders})
            : nRead > 0
                ? PortTextF("Hyperion found these IDs in {0}. Each becomes a profile with the missions it reached. "
                            "The .spc files are read, never changed: the original and Remastered keep playing with "
                            "them as before.",
                            {folders})
            : !g_found.empty()
                ? std::string(PortText("No StarCraft ID here can be imported. The files below were found and are "
                                       "left as they are."))
                : std::string(PortText("No StarCraft IDs were found. Hyperion looks in characters\\ where StarCraft "
                                       "is installed, and in Documents\\StarCraft\\characters\\."))),
       UI_FACE_TEXT, Fs(17), 0.0f, kScInkMsg, -1, 1.45f);
  Close();
  Decl well = Box("pfiwell");
  well.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  well.d.layout.sizing.width = Grow();
  well.d.layout.sizing.height = Grow();
  well.d.layout.padding = Pad(Fs(8), Fs(8), Fs(8), Fs(8));
  well.d.layout.childGap = Gap(Fs(4));
  InnerBox(well);
  Open(well);
  const int nFit = static_cast<int>((dh - 150 - 120 - 16) / 68);
  int nShown = 0;
  for (size_t k = 0; k < g_found.size() && nShown < nFit; ++k, ++nShown) {
    FoundId &f = g_found[k];
    const int i = static_cast<int>(k);
    Decl r = Box("pfirow", i);
    r.d.layout.sizing.width = Grow();
    r.d.layout.sizing.height = Fixed(Sc(64));
    r.d.layout.padding = Pad(Fs(16), Fs(16), 0, 0);
    r.d.layout.childGap = Gap(Fs(16));
    r.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    if (f.bRead)
      r.d.backgroundColor = Col(kLbItem);
    else
      r.d.border = Border(kLbDivider, 1, 1, 1, 1);
    Open(r);
    const bool bClash = f.bRead && IndexOf(f.name) >= 0;
    const bool bOn = f.bRead && f.bOn;
    Decl cb = Box("pficheck", i);
    cb.d.layout.sizing.width = Fixed(Sc(26));
    cb.d.layout.sizing.height = Fixed(Sc(26));
    if (f.bRead) {
      const bool bHot = Over(cb.d.id);
      cb.d.custom.customData = Custom(DrawScCheck, kScButton, 0.0f, (f.bOn ? 1 : 0) | (bHot ? 2 : 0));
      if (Clicked(cb.d.id)) {
        f.bOn = !f.bOn;
        if (f.bOn && f.nHow == 2)
          f.nHow = 0;  // ticked, it comes in: under another name
      }
    }
    Open(cb);
    Close();
    Decl t;
    t.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    t.d.layout.sizing.width = Grow();
    t.d.layout.childGap = Gap(Fs(3));
    Open(t);
    Text(Keep(f.name + ".spc"), UI_FACE_BOLD, Fs(19), 0.0f, !f.bRead ? kLbInkDesc : bOn ? kLbInkInfo : kScInkOff);
    std::string line;
    if (f.bRead) {
      const std::string made = Made(f.rec.dwStamp);
      // {0} the day it was made
      line = (made.empty() ? std::string() : PortTextF("Made {0}", {made}) + kDot) +
             ProgressLine(reinterpret_cast<const BYTE *>(&f.rec));
    } else {
      // {0} the folder it is in, {1} how large the file is
      line = PortTextF("In {0}", {f.folder}) + kDot +
             PortTextF("{0} bytes, not a StarCraft ID it can read", {std::to_string(f.cb)});
    }
    Text(Keep(line), UI_FACE_TEXT, Fs(15), 0.0f, bOn || !f.bRead ? kLbInkDesc : kScInkOff);
    Close();
    if (bClash) {
      // {0} the name it comes in under, {0} the profile it is added to
      const std::vector<std::string> how = {PortTextF("Import as {0}", {f.name.substr(0, 17) + " (Classic)"}),
                                            PortTextF("Add its progress to {0}", {f.name}), PortText("Skip it")};
      const int n = Pulldown("pfhow", i, 330, how, f.nHow, f.bOn ? PD_ON : PD_OFF);
      if (n >= 0)
        f.nHow = n;
    }
    // what becomes of it, in a column of its own so the pulldowns line up
    Decl why;
    why.d.layout.sizing.width = Fixed(Sc(f.bRead && !bClash ? 200 : 300));
    why.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
    Open(why);
    // {0} the ID's name
    if (!f.bRead)
      Text(PortText("Unreadable, left out"), UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff);
    else if (bClash)
      Text(Keep(f.bSame ? PortTextF("{0} has this progress already", {f.name})
                        : PortTextF("A profile is called {0}", {f.name})),
           UI_FACE_TEXT, Fs(14), 0.0f, f.bSame ? kScInkOff : kLbWarn);
    else
      Text(PortText("New profile"), UI_FACE_TEXT, Fs(14), 0.0f, bOn ? kDoneInk : kScInkOff);
    Close();
    Close();
  }
  if (nShown < static_cast<int>(g_found.size()))
    Text(Keep(PortTextF("and {0} more", {std::to_string(g_found.size() - nShown)})), UI_FACE_TEXT, Fs(15), 0.0f,
         kLbInkDesc);
  Close();
  Decl foot;
  foot.d.layout.sizing.width = Grow();
  foot.d.layout.childGap = Gap(Fs(12));
  foot.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(foot);
  const int nGo = ImportCount();
  Decl go = Box("pfigo");
  go.d.layout.sizing.width = Fixed(Sc(275));
  go.d.layout.sizing.height = Fixed(Sc(52));
  // {0} how many IDs come in
  const bool bGo = ScButton(
      go, Keep(nGo > 0 ? PortTextF("Import {0}", {std::to_string(nGo)}) : std::string(PortText("Import"))), 'I', true,
      nGo > 0);
  Decl no = Box("pfino");
  no.d.layout.sizing.width = Fixed(Sc(200));
  no.d.layout.sizing.height = Fixed(Sc(52));
  const bool bNo = ScButton(no, PortText("Cancel"), 'C', false);
  Spacer();
  Text(PortText(
           "Settings stay Hyperion\xe2\x80\x99s own; the original\xe2\x80\x99s are in its registry key and are not "
           "brought over."),
       UI_FACE_TEXT, Fs(15), 0.0f, kLbInkDesc);
  Close();
  Close();  // the dialog
  if (bGo && nGo > 0)
    ImportRun();
  else if (bNo || (bOut && g_lbOpen.empty()))
    ImportClose();
}

void ImportKeys(void)
{
  if (PulldownKeys())
    return;
  if ((Key('I') || Key(VK_RETURN)) && ImportCount() > 0)
    ImportRun();
  else if (Key('C') || Key(VK_ESCAPE))
    ImportClose();
}

// ---------------------------------------------------------------------------
// The profiles dialog (campaign.js profiles): every profile with its progress, the one picked
// in full and where its things are kept, and the IDs found. 1400 by 900 units, from 150 down.

void FaceBox(const std::string &name, float fSide, float fText)
{
  Decl face;
  face.d.layout.sizing.width = Fixed(Sc(fSide));
  face.d.layout.sizing.height = Fixed(Sc(fSide));
  face.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  face.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  static UiCustom faceDraw = {0.0f, DrawFace, NULL};
  face.d.custom.customData = &faceDraw;
  Open(face);
  char szInitial[2] = {name.empty() ? '?' : name[0] >= 'a' && name[0] <= 'z' ? static_cast<char>(name[0] - 32) : name[0],
                       0};
  Text(Keep(szInitial), UI_FACE_WIDE, Fs(fText), 0.0f, kScInkNav);
  Close();
}

void ProfileChoose(void)
{
  if (g_nSel < 0 || g_nSel >= static_cast<int>(g_list.size()))
    return;
  if (!SameName(g_list[g_nSel].name, ProfileCurrent()))
    Choose(g_list[g_nSel].name);
  g_bManage = false;
}

void Section(const char *psz)
{
  Decl s;
  s.d.layout.padding = Pad(0, 0, Fs(10), 0);
  Open(s);
  Text(psz, UI_FACE_WIDE, Fs(18), 0.04f, kScInkHead);
  Close();
}

// An episode's progress: its number, a bar, done of all (campaign.css .cp-pp).
void EpisodeBar(const BYTE *pRec, int nEpisode)
{
  int nTotal = 0;
  const int nDone = CampaignDone(pRec, nEpisode, &nTotal);
  Decl r;
  r.d.layout.sizing.width = Grow();
  r.d.layout.childGap = Gap(Fs(10));
  r.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(r);
  Decl n;
  n.d.layout.sizing.width = Fixed(Sc(120));
  Open(n);
  Text(PortText(CampaignEpisodeNumber(nEpisode)), UI_FACE_TEXT, Fs(16), 0.0f, kLbInkInfo);
  Close();
  Decl bar;
  bar.d.layout.sizing.width = Grow();
  bar.d.layout.sizing.height = Fixed(Sc(8));
  bar.d.backgroundColor = Col({1.0f, 1.0f, 1.0f, 0.06f});
  Radius(bar, 4);
  Open(bar);
  if (nDone > 0 && nTotal > 0) {
    Decl fill;
    fill.d.layout.sizing.width.type = CLAY__SIZING_TYPE_PERCENT;
    fill.d.layout.sizing.width.size.percent = static_cast<float>(nDone) / nTotal;
    fill.d.layout.sizing.height = Grow();
    fill.d.backgroundColor = Col(kDoneInk);
    Radius(fill, 4);
    Open(fill);
    Close();
  }
  Close();
  Decl c;
  c.d.layout.sizing.width = Fixed(Sc(56));
  c.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
  Open(c);
  Text(Keep(nDone >= nTotal && nTotal > 0 ? std::string(PortText("Done"))
                                          : std::to_string(nDone) + " / " + std::to_string(nTotal)),
       UI_FACE_TEXT, Fs(16), 0.0f, kLbInkInfo);
  Close();
  Close();
}

void Kept(const std::string &path, const char *pszWhat)
{
  Decl r;
  r.d.layout.sizing.width = Grow();
  r.d.layout.childGap = Gap(Fs(18));
  Open(r);
  Decl dt;
  dt.d.layout.sizing.width = Fixed(Sc(330));
  Open(dt);
  Text(Keep(path), UI_FACE_TEXT, Fs(15), 0.0f, kScInkLabel);
  Close();
  Decl dd;
  dd.d.layout.sizing.width = Grow();
  Open(dd);
  Text(pszWhat, UI_FACE_TEXT, Fs(15), 0.0f, kLbInkDesc, -1, 1.35f);
  Close();
  Close();
}

void Details(void)
{
  const Profile &p = g_list[g_nSel];
  Decl hd;
  hd.d.layout.sizing.width = Grow();
  hd.d.layout.childGap = Gap(Fs(16));
  hd.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(hd);
  FaceBox(p.name, 72, 32);
  Decl nm;
  nm.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  nm.d.layout.sizing.width = Grow();
  nm.d.layout.childGap = Gap(Fs(4));
  Open(nm);
  Text(Keep(p.name), UI_FACE_BOLD, Fs(26), 0.0f, kScInkHi);
  std::string sub = PortTextNF("{0} game", "{0} games", p.x.nGames, {std::to_string(p.x.nGames)});
  if (!p.x.from.empty())
    sub += std::string(kDot) + PortText("imported from a classic ID");
  Text(Keep(sub), UI_FACE_TEXT, Fs(17), 0.0f, kLbInkDesc);
  Close();
  Decl ren = Box("pmrename");
  ren.d.layout.sizing.height = Fixed(Sc(36));
  if (ScMini(ren, PortText("Rename"), 'N'))
    FormOpen(FORM_RENAME, p.name);
  Close();

  Section(PortText("CAMPAIGN"));
  const int nEp = CampaignEpisodes();
  for (int i = 0; i < nEp; i += 2) {
    Decl row;
    row.d.layout.sizing.width = Grow();
    row.d.layout.childGap = Gap(Fs(40));
    Open(row);
    for (int k = i; k < i + 2; ++k) {
      Decl cell;
      cell.d.layout.sizing.width = Grow();
      Open(cell);
      if (k < nEp)
        EpisodeBar(p.rec, k);
      Close();
    }
    Close();
  }

  Section(PortText("KEPT IN HYPERION\xe2\x80\x99S DATA FOLDER"));
  Kept("characters\\" + p.name + ".spc",
       PortText("Its campaign progress, in the original\xe2\x80\x99s own ID format"));
  Kept("profiles\\" + p.name + "\\profile.json",
       PortText("The race it last played and its games; later its hotkeys and its Game, Interface and Accessibility "
                "settings"));
  Kept("save\\" + p.name + "\\", PortText("Its saved games, as the original keeps them per ID"));
  Kept("settings.json",
       PortText("This computer, for every profile: the display and graphics, sound, and the folders"));
  Kept("maps\\replays\\", PortText("Replays, shared by every profile"));
  Decl note;
  note.d.layout.sizing.width = Grow();
  note.d.layout.padding = Pad(0, 0, Fs(6), 0);
  Open(note);
  Text(PortText("Hyperion never writes to the original\xe2\x80\x99s folders. Deleting a profile deletes its progress "
                "and saves; its replays stay."),
       UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff, -1, 1.35f);
  Close();
}

void Manager(void)
{
  if (g_nSel >= static_cast<int>(g_list.size()))
    g_nSel = static_cast<int>(g_list.size()) - 1;
  if (g_nSel < 0)
    g_nSel = 0;
  const bool bTop = !g_bImport && g_nForm == FORM_NONE && g_nDelete < 0;
  const float dw = fminf(1400, UiWidth() / Sc(1) - 160), dh = fminf(900, UiHeight() / Sc(1) - 220);
  const bool bOut = DialogOpen("pfmgr", dw, dh, 150);
  DialogTitle(PortText("PROFILES"));
  Decl body;
  body.d.layout.sizing.width = Grow();
  body.d.layout.sizing.height = Grow();
  body.d.layout.childGap = Gap(Fs(20));
  Open(body);
  // the list, and the IDs found under it
  Decl left;
  left.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  left.d.layout.sizing.width = Fixed(Sc(420));
  left.d.layout.sizing.height = Grow();
  left.d.layout.childGap = Gap(Fs(12));
  Open(left);
  Decl list = Box("pmlist");
  list.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  list.d.layout.sizing.width = Grow();
  list.d.layout.sizing.height = Grow();
  list.d.layout.padding = Pad(Fs(8), Fs(8), Fs(8), Fs(8));
  list.d.layout.childGap = Gap(Fs(4));
  InnerBox(list);
  Open(list);
  if (g_list.empty())
    Text(PortText("No profiles yet."), UI_FACE_TEXT, Fs(17), 0.0f, kLbInkDesc);
  const int nFit = static_cast<int>((dh - 75 - 120 - (FoundCount() > 0 ? 104 : 0) - 16) / 72);
  const int nFirst = g_nSel >= nFit ? g_nSel - nFit + 1 : 0;
  const std::string me = ProfileCurrent();
  for (int i = nFirst; i < static_cast<int>(g_list.size()) && i < nFirst + nFit; ++i) {
    const Profile &p = g_list[i];
    Decl r = Box("pmrow", i);
    const bool bOn = i == g_nSel;
    const bool bHot = Over(r.d.id);
    if (bTop && Clicked(r.d.id)) {
      const Uint64 nNow = SDL_GetTicks();
      const bool bDouble = g_nLastClickRow == i && nNow - g_nLastClick < 450;
      g_nLastClick = nNow;
      g_nLastClickRow = i;
      g_nSel = i;
      if (bDouble) {
        g_nLastClickRow = -1;
        ProfileChoose();
      }
    }
    r.d.layout.sizing.width = Grow();
    r.d.layout.sizing.height = Fixed(Sc(68));
    r.d.layout.padding = Pad(Fs(12), Fs(14), 0, 0);
    r.d.layout.childGap = Gap(Fs(14));
    r.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    r.d.backgroundColor = Col(bOn || bHot ? kLbItemHi : kLbItem);
    r.d.border = Border(bOn ? kScButtonHi.rim : kNone, 1, 1, 1, 1);
    Radius(r, 3);
    Open(r);
    FaceBox(p.name, 44, 20);
    Decl t;
    t.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    t.d.layout.sizing.width = Grow();
    t.d.layout.childGap = Gap(Fs(3));
    Open(t);
    Text(Keep(p.name), UI_FACE_BOLD, Fs(19), 0.0f, bOn || bHot ? kScInkHi : kLbInkInfo);
    Text(Keep(ProgressLine(p.rec)), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
    Close();
    if (SameName(p.name, me))
      Text(PortText("PLAYING"), UI_FACE_BOLD, Fs(12), 0.05f, kScInkAccept);
    Close();
  }
  Close();
  if (FoundCount() > 0) {
    Decl fd = Box("pmfound");
    fd.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    fd.d.layout.sizing.width = Grow();
    fd.d.layout.padding = Pad(Fs(16), Fs(16), Fs(12), Fs(12));
    fd.d.layout.childGap = Gap(Fs(4));
    fd.d.backgroundColor = Col(kLbInner);
    fd.d.border = Border(kLbHyperionRim, 1, 1, 1, 1);
    Open(fd);
    const int n = FoundCount();
    Text(Keep(PortTextNF("{0} StarCraft ID found", "{0} StarCraft IDs found", n, {std::to_string(n)})), UI_FACE_BOLD,
         Fs(17), 0.0f, kScInkAccept);
    std::string where;
    for (const FoundId &f : g_found)
      if (f.bRead && where.find(f.folder) == std::string::npos)
        // {0} a folder they were found in
        where = where.empty() ? PortTextF("in {0}", {f.folder}) : PortTextF("{0} and {1}", {where, f.folder});
    Text(Keep(where), UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
    Decl ib = Box("pmimport");
    ib.d.layout.sizing.height = Fixed(Sc(32));
    if (ScMini(ib, PortText("Import\xe2\x80\xa6"), 'I') && bTop)
      ImportOpen();
    Close();
  }
  Close();
  // the one picked
  Decl right = Box("pmdetail");
  right.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  right.d.layout.sizing.width = Grow();
  right.d.layout.sizing.height = Grow();
  right.d.layout.padding = Pad(Fs(24), Fs(24), Fs(20), Fs(20));
  right.d.layout.childGap = Gap(Fs(10));
  InnerBox(right);
  Open(right);
  if (!g_list.empty())
    Details();
  else
    Text(PortText("Make a profile to play. It keeps your campaign and your saves."), UI_FACE_BOLD, Fs(19), 0.0f,
         kLbInkInfo);
  Close();
  Close();
  // the foot
  Decl foot;
  foot.d.layout.sizing.width = Grow();
  foot.d.layout.childGap = Gap(Fs(12));
  foot.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(foot);
  const bool bAny = !g_list.empty();
  Decl ch = Box("pmchoose");
  ch.d.layout.sizing.width = Fixed(Sc(275));
  ch.d.layout.sizing.height = Fixed(Sc(52));
  const bool bChoose = ScButton(ch, PortText("Choose"), 'E', true, bAny);
  Decl nw = Box("pmnew");
  nw.d.layout.sizing.width = Fixed(Sc(225));
  nw.d.layout.sizing.height = Fixed(Sc(52));
  const bool bNew = ScButton(nw, PortText("New Profile"), 'W', false);
  Decl de = Box("pmdelete");
  de.d.layout.sizing.width = Fixed(Sc(200));
  de.d.layout.sizing.height = Fixed(Sc(52));
  const bool bDelete = ScButton(de, PortText("Delete"), 'D', false, bAny);
  Decl cl = Box("pmclose");
  cl.d.layout.sizing.width = Fixed(Sc(200));
  cl.d.layout.sizing.height = Fixed(Sc(52));
  const bool bClose = ScButton(cl, PortText("Close"), 'C', false);
  Close();
  Close();  // the dialog
  if (!bTop)
    return;
  if (bChoose && bAny)
    ProfileChoose();
  else if (bNew)
    FormOpen(FORM_NEW, "");
  else if (bDelete && bAny)
    g_nDelete = g_nSel;
  else if (bClose || bOut)
    g_bManage = false;
}

// New Profile and Rename over the profiles dialog.
void NameDialog(void)
{
  const bool bNew = g_nForm == FORM_NEW;
  DialogOpen("pfname", 760, 300, 260, 74);
  // {0} the profile being renamed
  DialogTitle(bNew ? PortText("NEW PROFILE")
                   : Keep(PortTextF("RENAME {0}", {Upper(g_formFor.c_str())})));
  Decl msg;
  msg.d.layout.sizing.width = Grow();
  msg.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  Open(msg);
  Text(bNew ? PortText("A profile keeps your campaign progress and saved games.")
            : PortText("The profile keeps its campaign and saves under its new name."),
       UI_FACE_TEXT, Fs(17), 0.0f, kScInkMsg);
  Close();
  NameForm(bNew ? PortText("Create") : PortText("Save"));
  Close();
}

void ManageKeys(void)
{
  if (g_nDelete >= 0)
    return;  // the dialog reads its own
  if (g_nForm != FORM_NONE) {
    if (Key(VK_ESCAPE))
      FormOpen(FORM_NONE, "");
    return;  // the box has the keyboard
  }
  const int n = static_cast<int>(g_list.size());
  if (Key(VK_DOWN) && g_nSel + 1 < n)
    ++g_nSel;
  else if (Key(VK_UP) && g_nSel > 0)
    --g_nSel;
  else if (Key('E') || Key(VK_RETURN))
    ProfileChoose();
  else if (Key('W'))
    FormOpen(FORM_NEW, "");
  else if (Key('N') && g_nSel >= 0 && g_nSel < n)
    FormOpen(FORM_RENAME, g_list[g_nSel].name);
  else if (Key('D') && g_nSel >= 0 && g_nSel < n)
    g_nDelete = g_nSel;
  else if (Key('I') && FoundCount() > 0)
    ImportOpen();
  else if (Key('C') || Key(VK_ESCAPE))
    g_bManage = false;
}

// Create a Profile (front.js profileFlow): with no profile, what starts a game asks for one,
// then goes on to it. 860 units wide at 260 down, over a blackout. With 1.17 IDs on the
// computer and no profile yet, it offers to import them instead.
void FirstRun(void)
{
  Decl scrim;
  scrim.d.id = Id("pfscrim");
  scrim.d.layout.sizing.width = Fixed(static_cast<float>(UiWidth()));
  scrim.d.layout.sizing.height = Fixed(static_cast<float>(UiHeight()));
  scrim.d.backgroundColor = Col(kScBlackout);
  scrim.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  scrim.d.floating.zIndex = 78;
  Open(scrim);
  Close();
  Decl d;
  d.d.id = Id("pfflow");
  d.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  d.d.layout.sizing.width = Fixed(Sc(860));
  d.d.layout.padding = Pad(Fs(60), Fs(60), Fs(40), Fs(38));
  d.d.layout.childGap = Gap(Fs(12));
  d.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  d.d.floating.attachPoints.element = CLAY_ATTACH_POINT_CENTER_TOP;
  d.d.floating.attachPoints.parent = CLAY_ATTACH_POINT_CENTER_TOP;
  d.d.floating.offset.y = Sc(260);
  d.d.floating.zIndex = 79;
  static UiCustom frame = {0.0f, DrawScFrame, const_cast<UiColor *>(&kScFillSolid)};
  d.d.custom.customData = &frame;
  Open(d);
  Decl title;
  title.d.layout.sizing.width = Grow();
  title.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  Open(title);
  Text(PortText("CREATE A PROFILE"), UI_FACE_WIDE, Fs(24), 0.04f, kScInkHi);
  Close();
  Decl msg;
  msg.d.layout.sizing.width = Grow();
  msg.d.layout.padding = Pad(0, 0, 0, Fs(6));
  Open(msg);
  Text(PortText("Hyperion keeps your campaign progress and saved games in a profile on this computer, as the original "
                "kept them in its IDs. Make one to play."),
       UI_FACE_TEXT, Fs(18), 0.0f, kScInkMsg, -1, 1.4f, true);
  Close();
  Text(PortText("Name"), UI_FACE_BOLD, Fs(18), 0.0f, kScInkLabel);
  Decl box;
  box.d.id = Id("pfedit");
  box.d.layout.sizing.width = Grow();
  box.d.layout.sizing.height = Fixed(Sc(54));
  ScEdit(box, &g_edit, 24, PortText("Name"), !g_bImport);
  Decl m;
  m.d.layout.sizing.height = Fixed(Sc(22));
  Open(m);
  if (!g_msg.empty())
    Text(Keep(g_msg), UI_FACE_TEXT, Fs(16), 0.0f, kScInkAccept);
  Close();
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.childGap = Gap(Fs(12));
  row.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  Open(row);
  Decl ok;
  ok.d.id = Id("pfflowok");
  ok.d.layout.sizing.width = Fixed(Sc(250));
  ok.d.layout.sizing.height = Fixed(Sc(52));
  const bool bOk = ScButton(ok, PortText("Create"), 0, true) || (!g_bImport && Key(VK_RETURN));
  Decl no;
  no.d.id = Id("pfflowno");
  no.d.layout.sizing.width = Fixed(Sc(250));
  no.d.layout.sizing.height = Fixed(Sc(52));
  const bool bNo = ScButton(no, PortText("Cancel"), 0, false);
  Close();
  // 1.17's IDs found on this computer: bring them over instead of starting afresh
  if (g_list.empty() && FoundCount() > 0) {
    Decl fd;
    fd.d.id = Id("pfflowfound");
    fd.d.layout.sizing.width = Grow();
    fd.d.layout.padding = Pad(Fs(16), Fs(16), Fs(12), Fs(12));
    fd.d.layout.childGap = Gap(Fs(16));
    fd.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    fd.d.backgroundColor = Col(kLbInner);
    fd.d.border = Border(kLbHyperionRim, 1, 1, 1, 1);
    Open(fd);
    Decl t;
    t.d.layout.sizing.width = Grow();
    Open(t);
    Text(Keep(FoundLine()), UI_FACE_TEXT, Fs(16), 0.0f, kScInkMsg, -1, 1.35f);
    Close();
    Decl ib;
    ib.d.id = Id("pfflowimport");
    ib.d.layout.sizing.height = Fixed(Sc(36));
    if (ScMini(ib, PortText("Import Them"), 0) && !g_bImport)
      ImportOpen();
    Close();
  }
  Close();
  if (g_bImport) {
  } else if (bNo) {
    FormOpen(FORM_NONE, "");
    g_pszThen = NULL;
  } else if (bOk && Make(g_edit)) {
    FormOpen(FORM_NONE, "");
    g_pszThenDone = g_pszThen;
    g_pszThen = NULL;
  }
}

}  // namespace

std::string ProfileCurrent(void)
{
  char sz[32] = "";
  if (!PortSettingString("hyperion", "profile", sz, sizeof sz) || sz[0] == '\0')
    return std::string();
  unsigned char rec[0x70];
  if (!ReadCharacterFile(sz, rec))
    return std::string();
  return sz;
}

std::string ProfileFolder(void)
{
  const std::string name = ProfileCurrent();
  if (name.empty())
    return std::string();
  CreateDirectoryA((std::string(PortDataFolder()) + "profiles").c_str(), NULL);
  CreateDirectoryA(ExtraFolder(name).c_str(), NULL);
  return ExtraFolder(name);
}

void ProfilesOpen(void)
{
  Refresh();
  Scan();
  g_bOpen = false;
  g_bManage = true;
  g_bImport = false;
  g_nDelete = -1;
  FormOpen(FORM_NONE, "");
  g_nSel = IndexOf(ProfileCurrent());
  if (g_nSel < 0)
    g_nSel = 0;
}

std::string ProfileLastRace(void)
{
  return ExtraRead(ProfileCurrent()).lastRace;
}

void ProfileSetLastRace(const char *pszRace)
{
  const std::string name = ProfileCurrent();
  if (name.empty())
    return;
  Extra x = ExtraRead(name);
  if (x.lastRace == pszRace)
    return;
  x.lastRace = pszRace;
  ExtraWrite(name, x);
}

void ProfileCountGame(void)
{
  const std::string name = ProfileCurrent();
  if (name.empty())
    return;
  Extra x = ExtraRead(name);
  ++x.nGames;
  ExtraWrite(name, x);
}

void ProfileToggle(void)
{
  g_bOpen = !g_bOpen;
  FormOpen(FORM_NONE, "");
  g_nDelete = -1;
  if (g_bOpen)
    Refresh();
}

void ProfileClose(void)
{
  g_bOpen = false;
  g_nDelete = -1;
  if (g_nForm != FORM_FIRST)
    FormOpen(FORM_NONE, "");
}

bool ProfileGate(const char *pszThen)
{
  if (!ProfileCurrent().empty())
    return true;
  Refresh();
  Scan();
  g_bOpen = false;
  g_bManage = false;
  g_pszThen = pszThen;
  FormOpen(FORM_FIRST, "");
  return false;
}

const char *ProfileThen(void)
{
  const char *p = g_pszThenDone;
  g_pszThenDone = NULL;
  return p;
}

bool ProfileChanged(void)
{
  const bool b = g_bChanged;
  g_bChanged = false;
  return b;
}

// Delete X?, from the card or the profiles dialog.
static void DeleteAsk(void)
{
  const Profile &p = g_list[g_nDelete];
  // {0} the profile's name
  const std::string title = PortTextF("Delete {0}?", {p.name});
  const char *const kButtons[] = {PortText("Delete"), PortText("Cancel")};
  const int n =
      FrontDialog(Keep(title),
                  p.x.from.empty()
                      ? PortText("Its campaign progress and saved games are deleted with it. Its replays stay where "
                                 "they are.")
                      : PortText("Its campaign progress and saved games are deleted with it. Its replays stay where "
                                 "they are. The classic ID it came from is not touched."),
                  kButtons, "DC", 2);
  if (n == 0) {
    Delete(g_nDelete);
    g_nDelete = -1;
  } else if (n == 1 || Key(VK_ESCAPE)) {
    g_nDelete = -1;
  }
}

void ProfileScreen(void)
{
  if (g_nForm == FORM_FIRST) {
    FirstRun();
    if (g_bImport)
      ImportDialog();
    return;
  }
  if (g_bManage) {
    Manager();
    if (g_bImport)
      ImportDialog();
    else if (g_nForm == FORM_NEW || g_nForm == FORM_RENAME)
      NameDialog();
    else if (g_nDelete >= 0 && g_nDelete < static_cast<int>(g_list.size()))
      DeleteAsk();
    return;
  }
  if (!g_bOpen)
    return;
  Card();
  if (g_nDelete >= 0 && g_nDelete < static_cast<int>(g_list.size())) {
    DeleteAsk();
    return;
  }
  // a click outside the card, the tab and the avatar closes it
  if (g_bReleased && g_nForm == FORM_NONE && !Over(Id("pfcard")) && !Over(Id("cardtab")) && !Over(Id("avatar")))
    ProfileClose();
}

bool ProfileKeys(void)
{
  if (g_bImport) {
    ImportKeys();
    return true;
  }
  if (g_bManage) {
    ManageKeys();
    return true;
  }
  if (g_nForm == FORM_FIRST) {
    if (Key(VK_ESCAPE)) {
      FormOpen(FORM_NONE, "");
      g_pszThen = NULL;
    }
    return true;
  }
  if (!g_bOpen)
    return false;
  if (g_nDelete >= 0)
    return true;  // the dialog reads its own
  if (g_nForm != FORM_NONE) {
    if (Key(VK_ESCAPE))
      FormOpen(FORM_NONE, "");
    return true;  // the box has the keyboard
  }
  if (Key(VK_ESCAPE) || Key('Y'))
    ProfileClose();
  else if (Key('W'))
    FormOpen(FORM_NEW, "");
  else if (Key('N') && !ProfileCurrent().empty())
    FormOpen(FORM_RENAME, ProfileCurrent());
  else if (Key('G'))
    ProfilesOpen();
  return true;
}

}  // namespace FrontKit
