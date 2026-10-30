// port_front_campaign.cpp -- Hyperion's campaign screen
// (docs/modules/front-end.md). Presentation only: a mission starts as 1.17's
// campaign screen starts it (sub_4BB5B0, without its history list), and the briefing, the
// game and the score screen after it are 1.17's.
//
// The episodes: one card each, StarCraft's three and, with Brood War installed, Brood War's
// three, each with the profile's progress (its character file, as 1.17 keeps it: the furthest
// mission per race and game) and, for an episode whose story comes later, 1.17's warning,
// which asks and never locks. An episode: its missions as 1.17's history list names them
// (rez\gluHist.tbl), done, next or not reached, and Play Mission for any reached; between them
// its cinematics, and Watch Again for any seen, which comes back here after it.
#include <windows.h>
#include <string.h>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "../data/port_smacker.h"
#include "oc_data.h"         // OC_VA, where 1.17's data is
#include "charactr.h"        // WriteCharacterFile
#include "dlg_str.h"         // str_load_table, str_get_string
#include "gluCmpgn.h"        // ReadCharacterFile
#include "glues.h"           // mem_free
#include "gluMain.h"         // glumain_choose_product
#include "grid.h"            // IsBroodwar
#include "snd.h"             // load_game_data
#include "uiSingle.h"        // campaign_t, sub_4BB6C0
#include "port_frontkit.h"
#include "port_frontscreens.h"
#include "port_ui.h"
#include "port_world.h"
#include "../data/port_stormapi.h"  // SFileOpenFileEx
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

using namespace FrontKit;

namespace {

enum { VIEW_EPISODES, VIEW_EPISODE };
enum { ROW_DONE, ROW_NEXT, ROW_LOCKED };

struct Episode {
  const char *pszNumber;
  const char *pszName;
  const char *pszRace;
  int nRace;  // 0 zerg, 1 terran, 2 protoss: the tables' and the character file's order
  bool bExpansion;
  const char *pszWorld;
  const char *pszWorldName;
};

const Episode kEpisodes[] = {
    {PT_("Episode I"), PT_("Rebel Yell"), PT_("Terran"), 1, false, "marsara", "Mar Sara"},
    {PT_("Episode II"), PT_("Overmind"), PT_("Zerg"), 0, false, "char", "Char"},
    {PT_("Episode III"), PT_("The Fall"), PT_("Protoss"), 2, false, "aiur", "Aiur"},
    {PT_("Episode IV"), PT_("The Stand"), PT_("Protoss"), 2, true, "shakuras", "Shakuras"},
    {PT_("Episode V"), PT_("The Iron Fist"), PT_("Terran"), 1, true, "braxis", "Braxis"},
    {PT_("Episode VI"), PT_("The Queen of Blades"), PT_("Zerg"), 0, true, "char", "Char"},
};

struct Mission {
  campaign_t *pRow;  // in 1.17's table
  std::string name;
  std::string number;  // as the episode numbers it ("4", "5a"); "" for the tutorial
  bool bTutorial;
  bool bCinematic;  // a cinematic's row (wFlic), not a mission
};

int g_nView;
int g_nEpisode;
int g_nRow;
int g_nResult;
int g_nGlues;  // the glue screen to go on to, when done
int g_nAsk = -1;  // the episode whose warning is asked, or -1
std::string g_note;
BYTE g_rec[0x70];  // the profile's character file
void *g_pHist;     // rez\gluHist.tbl
std::vector<Mission> g_missions[6];
campaign_t *g_pWatch;  // the cinematic chosen to watch again, played once the screen is black
bool g_bWatched;       // one was, so the game comes back here after it

// The episodes' art: 1.17's own clips of each campaign's figures (glue\campaign\*.smk and
// glue\Expcampaign\*.smk, which its campaign screens play), decoded by the port's Smacker
// player into the picture atlas, a slot a clip, with the black ground keyed out as 1.17
// keys it, and drawn at a whole scale while one fills most of the space. The second six are
// the clips 1.17 plays over a figure while its episode is pointed at, from their start,
// centred on the figure at its scale.
const char *const kClips[12] = {
    "glue\\campaign\\terr.smk",      "glue\\campaign\\zerg.smk",       "glue\\campaign\\prot.smk",
    "glue\\Expcampaign\\XProt.smk",  "glue\\Expcampaign\\XTerr.smk",   "glue\\Expcampaign\\XZerg.smk",
    "glue\\campaign\\terron.smk",    "glue\\campaign\\zergon.smk",     "glue\\campaign\\proton.smk",
    "glue\\Expcampaign\\XProtOn.smk", "glue\\Expcampaign\\XTerrOn.smk", "glue\\Expcampaign\\XZergOn.smk"};
const int kOnClip = 6;    // the first of the clips played on pointing
const int kClipSlot = 1;  // the picture atlas's slots 1 to 12 (the skirmish screen's minimap has 0)
// The part of each figure's clip ever drawn, over all its frames (left, top, right, bottom):
// the figure is fitted by it, not by the clip's frame, which is mostly empty.
const short kClipBox[6][4] = {{3, 3, 228, 195},  {9, 3, 187, 220}, {9, 9, 174, 201},
                              {36, 0, 139, 236}, {31, 1, 136, 241}, {19, 11, 170, 277}};

struct Clip {
  HANDLE hFile = INVALID_HANDLE_VALUE;
  SmkFile *pSmk = nullptr;
  bool bTried = false;
  bool bShown = false;  // a frame is in its slot
  int nW = 0, nH = 0;
  std::vector<BYTE> frame;
};
Clip g_clips[12];
UiCustom g_clipDraw[6];
const int kEpisodeIndex[6] = {0, 1, 2, 3, 4, 5};  // DrawEpisodeArt's contexts
float g_fOn[6];          // how far each episode's pointing clip has faded in, 0 to 1
bool g_bWasHot[6];       // whether the episode was pointed at last frame
float g_fCardText;       // the tallest card's text block last frame, 0 before it is known
float g_fOnStep;         // this frame's share of the fade, 120 ms in all
Uint64 g_nOnTicks;       // when the fades last moved

// A clip as a StarCraft: Remastered install has it, the same name with .webm, put in
// a palette of its own whose entry 0 is where it is clear.
SmkFile *ClipOpenWebm(const char *pszClip)
{
  std::string name(pszClip);
  name.replace(name.size() - 4, 4, ".webm");
  HANDLE hFile;
  if (!SFileOpenFileEx(NULL, name.c_str(), 0, &hFile))
    return nullptr;
  const DWORD cb = SFileGetFileSize(hFile, NULL);
  std::vector<BYTE> data(cb != 0xFFFFFFFF ? cb : 0);
  const bool bRead = !data.empty() && SFileReadFile(hFile, data.data(), cb, NULL, NULL);
  SFileCloseFile(hFile);
  return bRead ? SmkOpenVideo(data.data(), cb, 0x1000, nullptr, 1, 254) : nullptr;
}

// Opens the clip the first time, then puts its next frame in its slot when the frame is due.
void ClipStep(int i)
{
  Clip &c = g_clips[i];
  if (!c.bTried) {
    c.bTried = true;
    if (SFileOpenFileEx(NULL, kClips[i], 5, &c.hFile)) {  // scope 5: a file handle at the data
      c.pSmk = SmkOpen(c.hFile, 0x1000);                  // from the handle, with no sound
    } else {
      c.hFile = INVALID_HANDLE_VALUE;
      c.pSmk = ClipOpenWebm(kClips[i]);
      if (c.pSmk == nullptr)
        FrontLog("campaign: no %s\n", kClips[i]);
    }
    if (c.pSmk == nullptr)
      return;
    c.nW = static_cast<int>(SmkWidth(c.pSmk));
    c.nH = static_cast<int>(SmkHeight(c.pSmk));
    c.frame.assign(static_cast<size_t>(c.nW) * c.nH, 0);
    SmkToBuffer(c.pSmk, 0, 0, static_cast<ULONG>(c.nW), c.frame.data());
  }
  if (c.pSmk == nullptr || (c.bShown && SmkWait(c.pSmk)))
    return;
  SmkDoFrame(c.pSmk);
  const BYTE *pPal = SmkPaletteRGB(c.pSmk);
  std::vector<unsigned char> rgba(c.frame.size() * 4);
  for (size_t k = 0; k < c.frame.size(); ++k) {
    const BYTE *e = pPal + c.frame[k] * 3;
    rgba[k * 4] = e[0];
    rgba[k * 4 + 1] = e[1];
    rgba[k * 4 + 2] = e[2];
    rgba[k * 4 + 3] = e[0] | e[1] | e[2] ? 255 : 0;
  }
  UiPictureSet(kClipSlot + i, rgba.data(), c.nW, c.nH);
  c.bShown = true;
  SmkNextFrame(c.pSmk);
}

// Closes a clip; its next step opens it again from its first frame.
void ClipClose(Clip &c)
{
  if (c.pSmk != nullptr)
    SmkClose(c.pSmk);
  if (c.hFile != INVALID_HANDLE_VALUE)
    CloseHandle(c.hFile);
  c = Clip();
}

void ClipsClose(void)
{
  for (Clip &c : g_clips)
    ClipClose(c);
  for (int i = 0; i < 6; ++i) {
    g_fOn[i] = 0.0f;
    g_bWasHot[i] = false;
  }
  g_nOnTicks = 0;
}

// An episode's figure in its card's space: its drawn part at the bottom, centred, at a whole
// scale when that fills most of the space, else at the scale that fits; and over it, while
// the episode is pointed at, its pointing clip fading in.
void DrawEpisodeArt(float x, float y, float w, float h, void *pCtx)
{
  const int i = *static_cast<const int *>(pCtx);
  const Clip &c = g_clips[i];
  if (!c.bShown || c.nW <= 0 || c.nH <= 0)
    return;
  const short *b = kClipBox[i];
  const float bw = static_cast<float>(b[2] - b[0]), bh = static_cast<float>(b[3] - b[1]);
  const float fFit = fminf(w / bw, h / bh), fWhole = floorf(fFit);
  const float s = fWhole >= 1.0f && fWhole / fFit >= 0.8f ? fWhole : fFit;
  const float cx = roundf(x + (w - bw * s) / 2 - b[0] * s), cy = roundf(y + h - bh * s - b[1] * s);
  UiPicture(cx, cy, c.nW * s, c.nH * s, kClipSlot + i, UiRgb(0xffffff), 0.0f, true);
  const Clip &o = g_clips[kOnClip + i];
  if (g_fOn[i] > 0.0f && o.bShown)
    UiPicture(roundf(cx + (c.nW - o.nW) * s / 2), roundf(cy + (c.nH - o.nH) * s / 2), o.nW * s, o.nH * s,
              kClipSlot + kOnClip + i, UiColor{1.0f, 1.0f, 1.0f, g_fOn[i]}, 0.0f, true);
}

int EpisodeCount(void)
{
  return IsBroodwar() ? 6 : 3;
}

DWORD *Best(int nEpisode)
{
  const Episode &e = kEpisodes[nEpisode];
  return reinterpret_cast<DWORD *>(g_rec + (e.bExpansion ? 0x34 : 0x28)) + e.nRace;
}

campaign_t *Table(int nEpisode)
{
  const Episode &e = kEpisodes[nEpisode];
  return e.bExpansion ? sExpCampaigns_g[e.nRace] : sCampaigns_g[e.nRace];
}

std::string Clean(const char *psz)
{
  std::string s;
  for (; psz != NULL && *psz; ++psz)
    if (static_cast<unsigned char>(*psz) >= 0x20)
      s += *psz;
  return s;
}

// The episode's missions and cinematics: the rows 1.17's history list shows.
void ListMissions(void)
{
  for (int i = 0; i < 6; ++i) {
    g_missions[i].clear();
    int nNumber = 0;
    for (campaign_t *p = Table(i); p->wMission != 0; ++p) {
      if (p->wEntry == 0)
        continue;
      Mission m;
      m.pRow = p;
      m.name = g_pHist != NULL ? Clean(reinterpret_cast<const char *>(str_get_string(g_pHist, p->wEntry))) : "";
      m.bCinematic = p->wFlic != 0;
      m.bTutorial = !m.bCinematic && i == 0 && p->wMission == 1;  // Boot Camp
      if (m.bCinematic) {
        // the list names one "Cinematic: Wasteland Patrol"
        if (m.name.compare(0, 11, "Cinematic: ") == 0)
          m.name.erase(0, 11);
        if (m.name.empty())
          m.name = PortText("Cinematic");
        g_missions[i].push_back(m);
        continue;
      }
      if (!m.bTutorial)
        m.number = std::to_string(++nNumber);
      // 1.17's list names a mission with its number first ("4. The Jacobs Installation", "5a. ")
      size_t k = 0;
      while (k < m.name.size() && m.name[k] >= '0' && m.name[k] <= '9')
        ++k;
      if (k > 0 && k + 1 < m.name.size() && m.name[k] >= 'a' && m.name[k] <= 'z' && m.name[k + 1] == '.')
        ++k;
      if (k > 0 && k + 1 < m.name.size() && m.name[k] == '.') {
        m.number = m.name.substr(0, k);
        m.name = m.name.substr(k + 1);
        while (!m.name.empty() && m.name[0] == ' ')
          m.name.erase(0, 1);
      }
      if (m.bTutorial && m.name.compare(0, 10, "Tutorial: ") == 0)
        m.name.erase(0, 10);
      if (m.name.empty())
        // {0} the mission's number in its episode
        m.name = PortTextF("Mission {0}", {m.number});
      g_missions[i].push_back(m);
    }
  }
}

DWORD Furthest(int nEpisode)
{
  const DWORD dwBest = *Best(nEpisode);
  const DWORD dwFirst = g_missions[nEpisode].empty() ? 0 : g_missions[nEpisode][0].pRow->wMission;
  return dwBest > dwFirst ? dwBest : dwFirst;
}

int RowState(int nEpisode, const Mission &m)
{
  const DWORD dwFurthest = Furthest(nEpisode);
  // a cinematic plays on the way into the mission after it, which has its number: seen once
  // that mission is reached
  if (m.bCinematic)
    return m.pRow->wMission <= dwFurthest ? ROW_DONE : ROW_LOCKED;
  if (m.pRow->wMission < dwFurthest)
    return ROW_DONE;
  return m.pRow->wMission == dwFurthest ? ROW_NEXT : ROW_LOCKED;
}

int DoneCount(int nEpisode)
{
  int n = 0;
  for (const Mission &m : g_missions[nEpisode])
    n += !m.bCinematic && RowState(nEpisode, m) == ROW_DONE;
  return n;
}

int MissionCount(int nEpisode)
{
  int n = 0;
  for (const Mission &m : g_missions[nEpisode])
    n += !m.bCinematic;
  return n;
}

// The episode's last mission's number, 0 when it has none.
DWORD LastMission(int nEpisode)
{
  DWORD dw = 0;
  for (const Mission &m : g_missions[nEpisode])
    if (!m.bCinematic)
      dw = m.pRow->wMission;
  return dw;
}

// The row to show first: the next one, or the last when all are done.
int NextRow(int nEpisode)
{
  const std::vector<Mission> &v = g_missions[nEpisode];
  int nLast = 0;
  for (size_t i = 0; i < v.size(); ++i) {
    if (RowState(nEpisode, v[i]) == ROW_NEXT)
      return static_cast<int>(i);
    if (!v[i].bCinematic)
      nLast = static_cast<int>(i);
  }
  return nLast;  // the last mission
}

// The episodes whose story comes first and is not finished, as 1.17's lock gates read the
// character file (cmpgn_preinit, xcampaign_preinit); "" when none. *pnCount is how many they
// are, for the sentence that names them.
std::string StoryBefore(int nEpisode, int *pnCount = NULL)
{
  const DWORD *o = reinterpret_cast<const DWORD *>(g_rec + 0x28);  // zerg, terran, protoss
  const DWORD *x = reinterpret_cast<const DWORD *>(g_rec + 0x34);
  std::vector<int> need;
  switch (nEpisode) {
  case 1:
    if (o[0] == 0 && o[1] <= 0x0B)
      need.push_back(0);
    break;
  case 2:
    if (o[2] == 0) {
      if (o[1] <= 0x0B)
        need.push_back(0);
      if (o[0] <= 0x15)
        need.push_back(1);
    }
    break;
  case 4:
    if (x[1] == 0 && x[2] <= 0x27)
      need.push_back(3);
    break;
  case 5:
    if (x[0] == 0) {
      if (x[2] <= 0x27)
        need.push_back(3);
      if (x[1] <= 0x30)
        need.push_back(4);
    }
    break;
  }
  if (pnCount != NULL)
    *pnCount = static_cast<int>(need.size());
  std::string s;
  for (size_t i = 0; i < need.size(); ++i) {
    const char *psz = PortText(kEpisodes[need[i]].pszNumber);
    s = i == 0 ? psz : PortTextF("{0} and {1}", {s, psz});
  }
  return s;
}

void ReadProfile(void)
{
  memset(g_rec, 0, sizeof g_rec);
  ReadCharacterFile(gLocalPlayerName, g_rec);
}

void OpenEpisode(int nEpisode)
{
  g_nEpisode = nEpisode;
  g_nRow = NextRow(nEpisode);
  g_nView = VIEW_EPISODE;
  g_nAsk = -1;
  g_note.clear();
  WorldSetPlanet(kEpisodes[nEpisode].pszWorld, false);
}

void ChooseEpisode(int nEpisode)
{
  if (nEpisode >= EpisodeCount())
    return;
  if (!StoryBefore(nEpisode).empty())
    g_nAsk = nEpisode;
  else
    OpenEpisode(nEpisode);
}

// Watch Again: the cinematic played as the history list plays one (sub_4BB5B0: game mode 10,
// with its pretext), once the screen is black (CampaignWatchStart), and this screen next.
void Watch(const Mission &m)
{
  if (RowState(g_nEpisode, m) == ROW_LOCKED) {
    g_note = PortText("Not seen yet: it plays on the way into the mission after it.");
    return;
  }
  const Episode &e = kEpisodes[g_nEpisode];
  if ((gbIsExpansion != 0) != e.bExpansion && !glumain_choose_product(3, e.bExpansion)) {
    g_note = e.bExpansion ? PortText("Brood War's campaign could not be found.")
                          : PortText("StarCraft's campaign could not be found.");
    return;
  }
  g_pWatch = m.pRow;
  FrontLog("campaign: %s, cinematic %s\n", e.pszNumber, m.name.c_str());
  g_nGlues = 6;  // do_cmpgn_dlg, which is this screen
  g_nResult = FRONT_CAMPAIGN_DONE;
}

// Play Mission: the mission made as sub_4BB5B0 makes the row its history list returns, and
// 1.17's briefing next (do_cmpgn_dlg: 8 zerg, 7 terran, 9 protoss).
void Play(void)
{
  const Mission &m = g_missions[g_nEpisode][g_nRow];
  if (m.bCinematic) {
    Watch(m);
    return;
  }
  if (RowState(g_nEpisode, m) == ROW_LOCKED) {
    g_note = PortText("Not reached yet: the missions before it come first.");
    return;
  }
  const Episode &e = kEpisodes[g_nEpisode];
  // the episode's game, as choosing it in 1.17 sets it: its campaign archive and its flag
  if ((gbIsExpansion != 0) != e.bExpansion && !glumain_choose_product(3, e.bExpansion)) {
    g_note = e.bExpansion ? PortText("Brood War's campaign could not be found.")
                          : PortText("StarCraft's campaign could not be found.");
    return;
  }
  byte_5264C3_g = 0;
  dword_6CA71C = 0;
  ReadProfile();
  sgpSelectedCmpgn_g = m.pRow;
  DWORD *pnBest = Best(g_nEpisode);
  if (*pnBest < m.pRow->wMission) {
    *pnBest = m.pRow->wMission;
    WriteCharacterFile(g_rec);
  }
  if (!sub_4BB6C0(m.pRow->wMission)) {
    g_note = PortText("The mission's map could not be read.");
    FrontLog("campaign: mission %u did not start\n", m.pRow->wMission);
    return;
  }
  FrontLog("campaign: %s, %s (mission %u)\n", e.pszNumber, m.name.c_str(), m.pRow->wMission);
  g_nGlues = e.nRace == 0 ? 8 : e.nRace == 1 ? 7 : 9;
  g_nResult = FRONT_CAMPAIGN_DONE;
}

void Leave(int nGlues)
{
  g_nGlues = nGlues;
  g_nResult = FRONT_CAMPAIGN_DONE;
}

// ---------------------------------------------------------------------------
// Pieces.

void Well(Decl &w)
{
  w.d.backgroundColor = Col(kScWell);
  w.d.border = Border(kScWellRim, Fs(2), Fs(2), Fs(2), Fs(2));
}

Decl Button(const char *pszId, float fWidth)
{
  Decl b = Box(pszId);
  b.d.layout.sizing.width = Fixed(Sc(fWidth));
  b.d.layout.sizing.height = Fixed(Sc(52));
  return b;
}

// a mission's state as a pip: done lit, next ringed, the rest dark
void Pips(int nEpisode)
{
  Decl row;
  row.d.layout.childGap = Gap(Fs(5));
  Open(row);
  for (const Mission &m : g_missions[nEpisode]) {
    if (m.bCinematic)
      continue;
    const int s = RowState(nEpisode, m);
    Decl pip;
    pip.d.layout.sizing.width = Fixed(Sc(14));
    pip.d.layout.sizing.height = Fixed(Sc(14));
    if (s == ROW_DONE)
      pip.d.backgroundColor = Col(kScInkLabel);
    else if (s == ROW_NEXT)
      pip.d.border = Border(kScInkAccept, 2, 2, 2, 2);
    else
      pip.d.backgroundColor = Col({1.0f, 1.0f, 1.0f, 0.1f});
    Open(pip);
    Close();
  }
  Close();
}

// ---------------------------------------------------------------------------
// The episodes.

void EpisodeCard(int i, float fWidth, float fHeight)
{
  const Episode &e = kEpisodes[i];
  Decl card = Box("cpcard", i);
  const bool bHot = Over(card.d.id) && g_nAsk < 0;
  if (bHot)
    WorldSetPlanet(e.pszWorld, false);
  // the pointing clip: from its start each time the episode is pointed at, fading in and out
  if (bHot && !g_bWasHot[i])
    ClipClose(g_clips[kOnClip + i]);
  g_bWasHot[i] = bHot;
  g_fOn[i] = fminf(1.0f, fmaxf(0.0f, g_fOn[i] + (bHot ? g_fOnStep : -g_fOnStep)));
  if (bHot || g_fOn[i] > 0.0f)
    ClipStep(kOnClip + i);
  if (Clicked(card.d.id) && g_nAsk < 0)
    ChooseEpisode(i);
  card.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  card.d.layout.sizing.width = Fixed(fWidth);
  card.d.layout.sizing.height = Fixed(fHeight);
  card.d.layout.padding = Pad(Fs(24), Fs(24), Fs(22), Fs(24));
  card.d.layout.childGap = Gap(Fs(8));
  card.d.backgroundColor = Col(bHot ? UiColor{0.0f, 0.12f, 0.25f, 0.55f} : UiColor{0.0f, 0.03f, 0.08f, 0.6f});
  card.d.border = Border(bHot ? kScInkLabel : kScWellRim, Fs(2), Fs(2), Fs(2), Fs(2));
  Open(card);

  Text(Keep(std::to_string(i + 1)), UI_FACE_WIDE, Fs(20), 0.0f, bHot ? kScInkHi : kScInkMenu);
  Decl art = Box("cpart", i);
  art.d.layout.sizing.width = Grow();
  art.d.layout.sizing.height = Grow();
  g_clipDraw[i] = UiCustom{0.0f, DrawEpisodeArt, const_cast<int *>(&kEpisodeIndex[i])};
  art.d.custom.customData = &g_clipDraw[i];
  Open(art);
  Close();
  // the text under the art, as tall on every card (the tallest's, measured last frame), so
  // that the episodes' titles line up whatever wraps
  Decl text;
  text.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  text.d.layout.sizing.width = Grow();
  if (g_fCardText > 0.0f)
    text.d.layout.sizing.height = Fixed(g_fCardText);
  Open(text);
  Decl in = Box("cptext", i);
  in.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  in.d.layout.sizing.width = Grow();
  in.d.layout.childGap = Gap(Fs(8));
  Open(in);
  Text(Keep(Upper(PortText(e.pszNumber))), UI_FACE_WIDE, Fs(17), 0.08f, kScInkLabel);
  Text(PortText(e.pszName), UI_FACE_BOLD, Fs(30), 0.0f, kScInkHi);
  Text(Keep(std::string(PortText(e.pszRace)) + " \xc2\xb7 " + e.pszWorldName), UI_FACE_TEXT, Fs(17), 0.0f,
       kScInkButton);
  Decl rule;
  rule.d.layout.sizing.width = Grow();
  rule.d.layout.sizing.height = Fixed(UiPx(1));
  rule.d.backgroundColor = Col(kScWellRim);
  Open(rule);
  Close();
  Pips(i);
  const int nDone = DoneCount(i);
  const int nTotal = MissionCount(i);
  std::string prog;
  if (nTotal > 0 && nDone == nTotal && Furthest(i) > LastMission(i))
    prog = PortText("Complete \xc2\xb7 every mission replays");
  else if (nDone == 0)
    prog = PortTextF("{0} missions \xc2\xb7 not started", {std::to_string(nTotal)});
  else
    // {0} the missions done, {1} how many there are, {2} the next one's name
    prog = PortTextF("{0} of {1} \xc2\xb7 next: {2}",
                     {std::to_string(nDone), std::to_string(nTotal), g_missions[i][NextRow(i)].name});
  Text(Keep(prog), UI_FACE_TEXT, Fs(15), 0.0f, kScInkMsg, -1, 1.3f);
  const std::string before = StoryBefore(i);
  if (!before.empty())
    // {0} the episodes whose story comes first
    Text(Keep(PortTextF("Story follows {0}", {before})), UI_FACE_TEXT, Fs(15), 0.0f, kScInkAccept);
  Close();
  Close();
  Close();
}

void EpisodesView(void)
{
  const float fLeft = UiPx(58);
  Decl head = Floating(NULL, 0, fLeft, UiPx(92 + 40), 20, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  head.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  head.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  head.d.layout.childGap = Gap(Fs(8));
  Open(head);
  Text(IsBroodwar() ? PortText("BROOD WAR: CAMPAIGN") : PortText("STARCRAFT: CAMPAIGN"), UI_FACE_WIDE, Fs(34), 0.04f,
       kScInkHi);
  Decl line;
  line.d.layout.childGap = Gap(Fs(14));
  line.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(line);
  Text(PortText("Playing as"), UI_FACE_TEXT, Fs(17), 0.0f, kScInkButton);
  Text(Keep(std::string(gLocalPlayerName)), UI_FACE_BOLD, Fs(17), 0.0f, kScInkLabel);
  Text(PortText("\xc2\xb7 progress is kept in the profile, as the original kept it in the ID"), UI_FACE_TEXT, Fs(17),
       0.0f, kScInkButton);
  Decl chg = Box("cpprofile");
  chg.d.layout.sizing.width = Fixed(Sc(220));
  chg.d.layout.sizing.height = Fixed(Sc(40));
  if (ScMini(chg, PortText("Change Profile"), 'Y'))
    ProfilesOpen();
  Close();
  Close();

  const int n = EpisodeCount();
  for (int i = 0; i < n; ++i)
    ClipStep(i);
  const Uint64 nNow = SDL_GetTicks();
  g_fOnStep = g_nOnTicks != 0 ? static_cast<float>(nNow - g_nOnTicks) / 120.0f : 0.0f;
  g_nOnTicks = nNow;
  const float fGap = UiPx(18);
  const float fTop = UiPx(92 + 150);
  const float fAll = UiWidth() - 2 * fLeft;
  float fCard = (fAll - (n - 1) * fGap) / n;
  if (fCard > UiPx(400))
    fCard = UiPx(400);
  const float fHeight = UiHeight() - fTop - UiPx(150);
  Decl cards = Floating(NULL, 0, fLeft + (fAll - (fCard * n + fGap * (n - 1))) / 2, fTop, 20,
                        CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  cards.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  cards.d.layout.childGap = static_cast<uint16_t>(fGap + 0.5f);
  Open(cards);
  for (int i = 0; i < n; ++i)
    EpisodeCard(i, fCard, fHeight);
  Close();
  // the tallest card text, for the next frame
  float fText = 0.0f;
  for (int i = 0; i < n; ++i) {
    const Clay_ElementData d = Clay_GetElementData(Id("cptext", i));
    if (d.found)
      fText = fmaxf(fText, d.boundingBox.height);
  }
  g_fCardText = fText;

  Decl foot = Floating(NULL, 0, fLeft, UiHeight() - UiPx(112), 20, CLAY_ATTACH_POINT_LEFT_TOP,
                       CLAY_ATTACH_POINT_LEFT_TOP);
  foot.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  foot.d.layout.childGap = Gap(Fs(12));
  foot.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(foot);
  if (ScButton(Button("cpload", 240), PortText("Load Game"), 'L', false))
    LoadGameOpen();
  if (ScButton(Button("cpback", 240), PortText("Back"), 'B', false))
    Leave(0);
  Text(PortText(
           "Play Custom and Load Replay, which the original kept on this screen, are Skirmish and Replays in the bar."),
       UI_FACE_TEXT, Fs(15), 0.0f, kScInkMsg);
  Close();
}

// 1.17's question before an episode whose story comes later: asked, never a lock.
void Ask(void)
{
  const Episode &e = kEpisodes[g_nAsk];
  Decl black;
  black.d.layout.sizing.width = Fixed(static_cast<float>(UiWidth()));
  black.d.layout.sizing.height = Fixed(static_cast<float>(UiHeight()));
  black.d.backgroundColor = Col({0.0f, 0.0f, 0.0f, 0.55f});
  black.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  black.d.floating.zIndex = 60;
  Open(black);
  Close();
  Decl dlg = Box("cpask");
  dlg.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  dlg.d.layout.sizing.width = Fixed(Sc(820));
  dlg.d.layout.padding = Pad(Fs(48), Fs(48), Fs(40), Fs(40));
  dlg.d.layout.childGap = Gap(Fs(18));
  dlg.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  dlg.d.floating.attachPoints.element = CLAY_ATTACH_POINT_CENTER_CENTER;
  dlg.d.floating.attachPoints.parent = CLAY_ATTACH_POINT_CENTER_CENTER;
  dlg.d.floating.zIndex = 61;
  static UiCustom frame = {0.0f, DrawScFrame, const_cast<UiColor *>(&kScFillSolid)};
  dlg.d.custom.customData = &frame;
  Open(dlg);
  // {0} the episode's number
  Text(Keep(Upper(PortTextF("{0} comes later in the story", {PortText(e.pszNumber)}).c_str())), UI_FACE_WIDE, Fs(22),
       0.04f, kScInkHi);
  int nBefore = 0;
  const std::string before = StoryBefore(g_nAsk, &nBefore);
  // {0} the episodes whose story comes first, {1} the profile
  Text(Keep(PortTextNF("Its story follows {0}, which is not finished in {1}'s campaign. You can play it now anyway, "
                       "as the original lets you.",
                       "Its story follows {0}, which are not finished in {1}'s campaign. You can play it now anyway, "
                       "as the original lets you.",
                       nBefore, {before, gLocalPlayerName})),
       UI_FACE_TEXT, Fs(18), 0.0f, kScInkButton, -1, 1.4f);
  Decl row;
  row.d.layout.childGap = Gap(Fs(12));
  Open(row);
  const bool bPlay = ScButton(Button("cpaskplay", 240), PortText("Play It Now"), 'P', true);
  const bool bCancel = ScButton(Button("cpaskcancel", 240), PortText("Cancel"), 'C', false);
  Close();
  Close();
  if (bPlay)
    OpenEpisode(g_nAsk);
  else if (bCancel)
    g_nAsk = -1;
}

// ---------------------------------------------------------------------------
// An episode.

void MissionList(void)
{
  Decl well = Box("cplist");
  well.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  well.d.layout.sizing.width = Fixed(Sc(700));
  well.d.layout.sizing.height = Grow();
  Well(well);
  Open(well);
  Decl head;
  head.d.layout.sizing.width = Grow();
  head.d.layout.sizing.height = Fixed(Sc(48));
  head.d.layout.padding = Pad(Fs(18), Fs(18), 0, 0);
  head.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  head.d.border = Border(kScWellRim, 0, 0, 0, 1);
  Open(head);
  Text(PortText("MISSIONS"), UI_FACE_WIDE, Fs(18), 0.05f, kScInkHead);
  Spacer();
  // {0} the missions done, {1} how many there are
  Text(Keep(PortTextF("{0} of {1} done",
                      {std::to_string(DoneCount(g_nEpisode)), std::to_string(MissionCount(g_nEpisode))})),
       UI_FACE_TEXT, Fs(15), 0.0f, kScInkMenu);
  Close();
  const std::vector<Mission> &v = g_missions[g_nEpisode];
  for (size_t k = 0; k < v.size(); ++k) {
    const Mission &m = v[k];
    const int s = RowState(g_nEpisode, m);
    Decl row = Box("cprow", static_cast<int>(k));
    const bool bOn = static_cast<int>(k) == g_nRow;
    const bool bHot = Over(row.d.id);
    if (Clicked(row.d.id)) {
      g_nRow = static_cast<int>(k);
      g_note.clear();
    }
    row.d.layout.sizing.width = Grow();
    row.d.layout.sizing.height = Fixed(Sc(50));
    row.d.layout.padding = Pad(Fs(18), Fs(18), 0, 0);
    row.d.layout.childGap = Gap(Fs(14));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    row.d.border = Border(bOn ? kScInkLabel : kScWellRim, bOn ? Fs(3) : 0, 0, 0, 1);
    if (bOn)
      row.d.backgroundColor = Col({0.0f, 0.27f, 0.55f, 0.35f});
    else if (bHot)
      row.d.backgroundColor = Col({0.0f, 0.2f, 0.4f, 0.2f});
    Open(row);
    Decl num;
    num.d.layout.sizing.width = Fixed(Sc(34));
    Open(num);
    Text(m.bTutorial || m.bCinematic ? "\xc2\xb7" : Keep(m.number), UI_FACE_BOLD, Fs(17), 0.0f, kScInkMenu);
    Close();
    const UiColor ink = s == ROW_LOCKED ? kScInkOff : bOn || bHot ? kScInkHi : kScInkButton;
    Text(Keep(m.name), m.bCinematic ? UI_FACE_TEXT : UI_FACE_BOLD, Fs(17), 0.0f, ink);
    if (m.bTutorial)
      Text(PortText("Tutorial"), UI_FACE_TEXT, Fs(14), 0.0f, kScInkMenu);
    if (m.bCinematic)
      Text(PortText("Cinematic"), UI_FACE_TEXT, Fs(14), 0.0f, kScInkMenu);
    Spacer();
    Text(s == ROW_DONE     ? (m.bCinematic ? PortText("Seen") : PortText("Done"))
         : s == ROW_NEXT   ? PortText("Next")
                           : "",
         UI_FACE_TEXT, Fs(15), 0.0f, s == ROW_NEXT ? kScInkAccept : kScInkLabel);
    Close();
  }
  Close();
}

void MissionDetail(void)
{
  const Episode &e = kEpisodes[g_nEpisode];
  const Mission &m = g_missions[g_nEpisode][g_nRow];
  const int s = RowState(g_nEpisode, m);
  Decl det = Box("cpdetail");
  det.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  det.d.layout.sizing.width = Grow();
  det.d.layout.sizing.height = Grow();
  det.d.layout.padding = Pad(Fs(30), Fs(30), Fs(28), Fs(28));
  det.d.layout.childGap = Gap(Fs(12));
  Well(det);
  Open(det);
  // {0} the mission's number in its episode
  Text(m.bCinematic  ? PortText("CINEMATIC")
       : m.bTutorial ? PortText("TUTORIAL")
                     : Keep(PortTextF("MISSION {0}", {m.number})),
       UI_FACE_WIDE, Fs(17), 0.08f, kScInkLabel);
  Text(Keep(m.name), UI_FACE_BOLD, Fs(34), 0.0f, kScInkHi);
  Text(Keep(std::string(PortText(e.pszRace)) + " \xc2\xb7 " + e.pszWorldName), UI_FACE_TEXT, Fs(17), 0.0f,
       kScInkButton);
  if (m.bCinematic) {
    Text(s == ROW_DONE
             ? PortText("Seen on the way into the mission after it. Watch it again, then come back here.")
             : PortText("Not seen yet: it plays on the way into the mission after it."),
         UI_FACE_TEXT, Fs(17), 0.0f, kScInkMsg, -1, 1.4f);
  } else {
    // {0} the profile
    Text(s == ROW_DONE   ? PortText("Done. Playing it again keeps your place in the campaign.")
         : s == ROW_NEXT ? Keep(PortTextF("Next in {0}'s campaign.", {gLocalPlayerName}))
                         : PortText("Not reached yet. The original lists only the missions up to the furthest "
                                    "reached."),
         UI_FACE_TEXT, Fs(17), 0.0f, s == ROW_NEXT ? kScInkAccept : kScInkMsg, -1, 1.4f);
  }
  Decl gap;
  gap.d.layout.sizing.height = Grow();
  Open(gap);
  Close();
  Text(m.bCinematic
           ? PortText("Escape ends it.")
           : PortText("The briefing, the mission and the score screen after it are the original's."),
       UI_FACE_TEXT, Fs(15), 0.0f, kScInkMenu);
  Close();
}

void EpisodeView(void)
{
  const Episode &e = kEpisodes[g_nEpisode];
  Decl panel = Box("cppanel");
  panel.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  panel.d.layout.sizing.width = Fixed(UiWidth() - 2 * UiPx(58));
  panel.d.layout.sizing.height = Fixed(UiHeight() - UiPx(92 + 40) - Sc(78));
  panel.d.layout.padding = Pad(Fs(40), Fs(40), Fs(26), Fs(30));
  panel.d.layout.childGap = Gap(Fs(18));
  panel.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  panel.d.floating.offset.x = UiPx(58);
  panel.d.floating.offset.y = UiPx(92 + 40);
  panel.d.floating.zIndex = 20;
  static UiCustom frame = {0.0f, DrawScFrame, NULL};
  panel.d.custom.customData = &frame;
  Open(panel);

  Decl head;
  head.d.layout.sizing.width = Grow();
  head.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(head);
  Decl names;
  names.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  names.d.layout.childGap = Gap(Fs(4));
  Open(names);
  Text(Keep(Upper((std::string(PortText(e.pszNumber)) + " \xc2\xb7 " + PortText(e.pszRace)).c_str())), UI_FACE_WIDE,
       Fs(16), 0.08f, kScInkLabel);
  Text(PortText(e.pszName), UI_FACE_BOLD, Fs(32), 0.0f, kScInkHi);
  Close();
  Spacer();
  if (g_nEpisode > 0) {
    Decl prev = Box("cpprev");
    prev.d.layout.sizing.width = Fixed(Sc(200));
    prev.d.layout.sizing.height = Fixed(Sc(40));
    if (ScMini(prev, Keep(std::string("\xe2\x80\xb9 ") + PortText(kEpisodes[g_nEpisode - 1].pszNumber)), 0))
      ChooseEpisode(g_nEpisode - 1);
  }
  if (g_nEpisode + 1 < EpisodeCount()) {
    Decl next = Box("cpnext");
    next.d.layout.sizing.width = Fixed(Sc(200));
    next.d.layout.sizing.height = Fixed(Sc(40));
    if (ScMini(next, Keep(std::string(PortText(kEpisodes[g_nEpisode + 1].pszNumber)) + " \xe2\x80\xba"), 0))
      ChooseEpisode(g_nEpisode + 1);
  }
  Close();

  Decl body;
  body.d.layout.sizing.width = Grow();
  body.d.layout.sizing.height = Grow();
  body.d.layout.childGap = Gap(Fs(24));
  Open(body);
  MissionList();
  MissionDetail();
  Close();

  Decl foot;
  foot.d.layout.sizing.width = Grow();
  foot.d.layout.childGap = Gap(Fs(12));
  foot.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(foot);
  const Mission &m = g_missions[g_nEpisode][g_nRow];
  const int s = RowState(g_nEpisode, m);
  const char *pszPlay = m.bCinematic ? (s == ROW_DONE ? PortText("Watch Again") : PortText("Watch"))
                        : s == ROW_DONE ? PortText("Play Again")
                                        : PortText("Play Mission");
  const bool bPlay = ScButton(Button("cpplay", 260), pszPlay, 'P', true, s != ROW_LOCKED);
  const bool bLoad = ScButton(Button("cpload2", 240), PortText("Load Game"), 'L', false);
  const bool bBack = ScButton(Button("cpback2", 240), PortText("Back"), 'B', false);
  if (!g_note.empty())
    Text(Keep(g_note), UI_FACE_TEXT, Fs(15), 0.0f, kScInkAccept);
  Close();
  Close();

  if (bPlay)
    Play();
  else if (bLoad)
    LoadGameOpen();
  else if (bBack) {
    g_nView = VIEW_EPISODES;
    g_note.clear();
  }
}

}  // namespace

namespace FrontKit {

void CampaignReread(void)
{
  ReadProfile();
}

int CampaignEpisodes(void)
{
  return EpisodeCount();
}

const char *CampaignEpisodeNumber(int nEpisode)
{
  return kEpisodes[nEpisode].pszNumber;
}

// As the campaign screen counts them: the rows of 1.17's history list less its cinematics,
// done when before the furthest mission reached (RowState).
int CampaignDone(const unsigned char *pRec, int nEpisode, int *pnTotal)
{
  const Episode &e = kEpisodes[nEpisode];
  DWORD dwBest;
  memcpy(&dwBest, pRec + (e.bExpansion ? 0x34 : 0x28) + 4 * e.nRace, sizeof dwBest);
  DWORD dwFirst = 0;
  int nTotal = 0;
  for (campaign_t *p = Table(nEpisode); p->wMission != 0; ++p)
    if (p->wEntry != 0 && p->wFlic == 0 && nTotal++ == 0)
      dwFirst = p->wMission;
  const DWORD dwFurthest = dwBest > dwFirst ? dwBest : dwFirst;
  int nDone = 0;
  for (campaign_t *p = Table(nEpisode); p->wMission != 0; ++p)
    nDone += p->wEntry != 0 && p->wFlic == 0 && p->wMission < dwFurthest;
  *pnTotal = nTotal;
  return nDone;
}

void CampaignOpen(void)
{
  g_nResult = 0;
  g_nGlues = 0;
  g_pWatch = NULL;
  g_nAsk = -1;
  g_note.clear();
  if (g_pHist == NULL)
    g_pHist = reinterpret_cast<void *>(str_load_table(reinterpret_cast<intptr_t>("rez\\gluHist.tbl"),
                                                      reinterpret_cast<intptr_t>(load_game_data),
                                                      reinterpret_cast<intptr_t>(__FILE__), __LINE__));
  ListMissions();
  ReadProfile();
  // back from a mission or a load, the episode it was in, at the next mission; back from a
  // cinematic, at it; else the episodes
  g_nView = VIEW_EPISODES;
  int nCinematic = -1;
  if (sgpSelectedCmpgn_g != NULL)
    for (int i = 0; i < EpisodeCount(); ++i)
      for (size_t k = 0; k < g_missions[i].size(); ++k)
        if (g_missions[i][k].pRow == sgpSelectedCmpgn_g) {
          g_nEpisode = i;
          g_nView = VIEW_EPISODE;
          nCinematic = g_missions[i][k].bCinematic ? static_cast<int>(k) : -1;
        }
  if (g_nView == VIEW_EPISODE)
    g_nRow = nCinematic >= 0 ? nCinematic : NextRow(g_nEpisode);
  FrontLog("campaign: %d episodes for %s; furthest %u %u %u, %u %u %u\n", EpisodeCount(), gLocalPlayerName,
           reinterpret_cast<DWORD *>(g_rec + 0x28)[0], reinterpret_cast<DWORD *>(g_rec + 0x28)[1],
           reinterpret_cast<DWORD *>(g_rec + 0x28)[2], reinterpret_cast<DWORD *>(g_rec + 0x34)[0],
           reinterpret_cast<DWORD *>(g_rec + 0x34)[1], reinterpret_cast<DWORD *>(g_rec + 0x34)[2]);
}

void CampaignClose(void)
{
  ClipsClose();
  if (g_pHist != NULL) {
    mem_free(g_pHist, __FILE__, __LINE__);
    g_pHist = NULL;
  }
  for (std::vector<Mission> &v : g_missions)
    v.clear();
}

int CampaignResult(void)
{
  return LoadGameStarted() ? FRONT_LOAD_STARTED : g_nResult;
}

int CampaignGluesMode(void)
{
  return g_nGlues;
}

bool CampaignWatching(void)
{
  return g_pWatch != NULL;
}

void CampaignWatchStart(void)
{
  sgpSelectedCmpgn_g = g_pWatch;
  sgSavedFlic = g_pWatch->wFlic;
  gwGameMode = 10;
  g_pWatch = NULL;
  g_bWatched = true;
}

bool CampaignWatchedTake(void)
{
  const bool b = g_bWatched;
  g_bWatched = false;
  return b;
}

void CampaignScreen(void)
{
  if (g_nView == VIEW_EPISODES)
    EpisodesView();
  else
    EpisodeView();
  if (g_nAsk >= 0)
    Ask();
  LoadGameDialog();
}

bool CampaignKeys(void)
{
  // chosen: nothing more while the screen fades out
  if (g_nResult != 0)
    return true;
  if (LoadGameKeys())
    return true;
  if (g_nAsk >= 0) {
    if (Key('P') || Key(VK_RETURN))
      OpenEpisode(g_nAsk);
    else if (Key('C') || Key(VK_ESCAPE))
      g_nAsk = -1;
    return true;
  }
  if (g_nView == VIEW_EPISODES) {
    for (int i = 0; i < EpisodeCount(); ++i)
      if (Key('1' + i))
        ChooseEpisode(i);
    if (Key('L'))
      LoadGameOpen();
    else if (Key('Y'))
      ProfilesOpen();
    else if (Key('B') || Key(VK_ESCAPE))
      Leave(0);
    return true;
  }
  const int nRows = static_cast<int>(g_missions[g_nEpisode].size());
  if (Key(VK_DOWN) && g_nRow + 1 < nRows)
    ++g_nRow;
  else if (Key(VK_UP) && g_nRow > 0)
    --g_nRow;
  else if (Key('P') || Key(VK_RETURN))
    Play();
  else if (Key('L'))
    LoadGameOpen();
  else if (Key('B') || Key(VK_ESCAPE))
    g_nView = VIEW_EPISODES;
  return true;
}

}  // namespace FrontKit
