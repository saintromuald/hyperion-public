// port_front_lab.cpp -- the AI lab's screen (the design center's 32-ai-lab). The lab is
// a mod (mods\lab\mod.json, mode "lab") whose home screen button opens this screen. Its farm
// plays games of computers in hidden worker processes, and the screen shows them as a wall of
// small maps (port_labwall.cpp), lists the games played and opens any one to watch.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string>
#include <SDL3/SDL.h>
#include "../front/port_frontkit.h"
#include "../front/port_frontlobby.h"
#include "../front/port_frontscreens.h"
#include "../front/port_ui.h"
#include "port_labfarm.h"
#include "port_labgames.h"
#include "port_labwall.h"
#include "port_labwatch.h"

using namespace FrontKit;

namespace {

const UiColor kInkWhite = KitRgb(0xffffff);
const UiColor kGoldRim = KitRgb(0xdaa308, 0.7f);

// the screen's views, in its header
enum { VIEW_FARM, VIEW_GAMES, VIEWS };
const char *const kViewNames[VIEWS] = {"The Farm", "Games"};
const char kViewKeys[VIEWS] = {'F', 'G'};

// the farm's pace: as fast as each game goes, or a multiple of Fastest (the user's choice, PRD
// 2026-10-19)
const int kPaces[] = {0, 64, 32, 16};
const char *const kPaceNames[] = {"Max", "\xc3\x97" "64", "\xc3\x97" "32", "\xc3\x97" "16"};
const int kPaceCount = 4;

int g_nView;
int g_nResult;
bool g_bHidden;      // the wall shows how far each game has gone, not the game
std::string g_why;   // why the farm did not start
bool g_bBack;        // back from a game watched: the view it was watched from

void HeadButtons(void)
{
  for (int i = 0; i < VIEWS; ++i) {
    Decl b = Box("labview", i);
    b.d.layout.sizing.width = Fit();
    b.d.layout.sizing.width.size.minMax.min = Sc(150);
    b.d.layout.sizing.height = Fixed(Sc(40));
    if (i == g_nView)
      b.d.border = Border(kGoldRim, Fs(2), Fs(2), Fs(2), Fs(2));
    if (ScMini(b, kViewNames[i], kViewKeys[i]))
      g_nView = i;
  }
}

std::string Thousands(long long n)
{
  std::string s = std::to_string(n < 0 ? -n : n);
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
    s.insert(static_cast<size_t>(i), ",");
  return n < 0 ? "-" + s : s;
}

void StartStop(void)
{
  g_why.clear();
  if (PortLabFarmRunning())
    PortLabFarmStop();
  else if (!PortLabFarmStart(PortLabFarmAtOnce(), 0, &g_why))
    FrontLog("lab: the farm did not start: %s\n", g_why.c_str());
}

void Pause(void)
{
  PortLabFarmPace(PortLabFarmPaceNow(), !PortLabFarmPaused());
}

// games at once, the pace, the wall hidden or shown, paused, and Start or Stop
void FarmControls(float fW)
{
  const bool bRunning = PortLabFarmRunning();
  if (PortLabFarmAtOnce() == 0)
    PortLabFarmSetAtOnce(PortLabFarmAtOnceDefault());
  const int nAtOnce = PortLabFarmAtOnce();
  Decl row = At("labfarmrow", 40, 96, fW - 80, 44);
  row.d.layout.childGap = Gap(Fs(12));
  row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(row);
  Text("Games at once", UI_FACE_TEXT, Fs(17), 0.0f, kLbInkInfo);
  Decl less = Box("labless");
  less.d.layout.sizing.height = Fixed(Sc(36));
  if (ScMini(less, "\xe2\x88\x92", 0, nAtOnce > 1))
    PortLabFarmSetAtOnce(nAtOnce - 1);
  Text(Keep(std::to_string(nAtOnce)), UI_FACE_BOLD, Fs(19), 0.0f, kInkWhite);
  Decl more = Box("labmore");
  more.d.layout.sizing.height = Fixed(Sc(36));
  if (ScMini(more, "+", 0, nAtOnce < 64))
    PortLabFarmSetAtOnce(nAtOnce + 1);
  Decl gap = Box("labgap1");
  gap.d.layout.sizing.width = Fixed(Sc(10));
  Open(gap);
  Close();
  Text("Pace", UI_FACE_TEXT, Fs(17), 0.0f, kLbInkInfo);
  int nPace = 0;
  for (int i = 0; i < kPaceCount; ++i)
    if (kPaces[i] == PortLabFarmPaceNow())
      nPace = i;
  const int nChosen = Segments("labpace", kPaceNames, kPaceCount, nPace, 0, NULL, 36);
  if (nChosen >= 0)
    PortLabFarmPace(kPaces[nChosen], PortLabFarmPaused());
  Decl hide = Box("labhide");
  hide.d.layout.sizing.height = Fixed(Sc(36));
  if (ScMini(hide, g_bHidden ? "Show the Games" : "Hide the Games", 'H'))
    g_bHidden = !g_bHidden;
  Decl pause = Box("labpause");
  pause.d.layout.sizing.height = Fixed(Sc(36));
  if (ScMini(pause, PortLabFarmPaused() ? "Resume" : "Pause", PortLabFarmPaused() ? 'R' : 'P', bRunning))
    Pause();
  Spacer();
  int nPlaying = 0;
  for (int i = 0; i < PortLabFarmSlots(); ++i)
    nPlaying += PortLabFarmSlot(i).state != LAB_IDLE;
  std::string state;
  if (!g_why.empty())
    state = g_why;
  else if (bRunning)
    state = Thousands(PortLabFarmPlayed()) + " played" + kDot + std::to_string(nPlaying) + " playing" +
            (PortLabFarmGamesAnHour() > 0 ? kDot + Thousands(PortLabFarmGamesAnHour()) + " an hour" : std::string());
  else
    state = PortLabFarmPlayed() > 0 ? Thousands(PortLabFarmPlayed()) + " played, stopped" : std::string("Stopped");
  Text(Keep(state), UI_FACE_TEXT, Fs(17), 0.0f, kLbInkDesc);
  Decl go = Box("labgo");
  go.d.layout.sizing.width = Fixed(Sc(160));
  go.d.layout.sizing.height = Fixed(Sc(44));
  if (ScButton(go, bRunning ? "Stop" : "Start", 'S', !bRunning))
    StartStop();
  Close();
}

void Foot(float fW, float fH)
{
  Decl foot = At("labfoot", 40, fH - 33 - 52, fW - 80, 52);
  foot.d.layout.childGap = Gap(Fs(16));
  foot.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(foot);
  Text(Keep(PortLabFarmProjectName()), UI_FACE_BOLD, Fs(17), 0.0f, kInkWhite);
  Decl edit = Box("labedit");
  edit.d.layout.sizing.height = Fixed(Sc(38));
  if (ScMini(edit, "Edit Project", 0))
    ShellExecuteA(NULL, "open", PortLabFarmProjectFile().c_str(), NULL, NULL, SW_SHOWNORMAL);
  Text(g_nView == VIEW_FARM ? "Each game is a Hyperion of its own with nothing drawn; a tile shows its units as it sends them."
                            : "Every game the project's farm has played, newest first.",
       UI_FACE_TEXT, Fs(15), 0.0f, kLbInkDesc);
  Spacer();
  Decl b = Box("labback");
  b.d.layout.sizing.width = Fixed(Sc(180));
  b.d.layout.sizing.height = Fixed(Sc(52));
  if (ScButton(b, "Back", 'B', false))
    g_nResult = FRONT_LAB_HOME;
  Close();
}

}  // namespace

namespace FrontKit {

void LabOpen(bool bGames)
{
  g_nResult = 0;
  if (!g_bBack)
    g_nView = bGames ? VIEW_GAMES : VIEW_FARM;
  g_bBack = false;
  FrontLog("lab: open\n");
}

bool LabCameBack(void)
{
  g_bBack = PortLabWatchCameBack();
  return g_bBack;
}

int LabResult(void)
{
  const int n = g_nResult;
  g_nResult = 0;
  return n;
}

const char *LabPanelId(void)
{
  return "labpanel";
}

void LabScreen(void)
{
  const float fW = UiWidth() / Sc(1) - 116;
  const float fH = fminf(990.0f, UiHeight() / Sc(1) - 132 - 74);
  Decl panel = Box("labpanel");
  panel.d.layout.sizing.width = Fixed(Sc(fW));
  panel.d.layout.sizing.height = Fixed(Sc(fH));
  panel.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  panel.d.floating.offset.x = Sc(58);
  panel.d.floating.offset.y = Sc(132);
  panel.d.floating.zIndex = 20;
  static UiCustom frame = {0.0f, DrawScFrame, NULL};
  panel.d.custom.customData = &frame;
  Open(panel);
  Decl head = At("labhead", 40, 22, fW - 40 - 400, 0);
  head.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  Open(head);
  Text("AI Lab", UI_FACE_BOLD, Fs(27), 0.0f, kInkWhite);
  Text(Keep(std::string("Make computer players") + kDot + "play thousands of games in the background" + kDot +
            "watch any one of them"),
       UI_FACE_TEXT, Fs(17), 0.0f, kLbInkInfo);
  Close();
  Decl tabs = Floating("labtabs", 0, -Sc(34), Sc(32), 22, CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP);
  tabs.d.layout.childGap = Gap(Fs(8));
  Open(tabs);
  HeadButtons();
  Close();
  if (g_nView == VIEW_FARM) {
    FarmControls(fW);
    LabWall(40, 156, fW - 80, fH - 156 - 33 - 52 - 16, g_bHidden);
    // a tile clicked: its game, joined where it is while it plays, else from its start
    const int nSlot = LabWallClicked();
    if (nSlot >= 0 && PortLabFarmSlot(nSlot).dwShown != 0) {
      const LabSlot &s = PortLabFarmSlot(nSlot);
      PortLabWatchAsk(s.dwShown, s.shownMap, &g_why, s.endReason.empty());
    }
  } else {
    LabGames(40, 100, fW - 80, fH - 100 - 33 - 52 - 16);
  }
  // a game asked for, here or by lab_watch: the front end starts it
  if (PortLabWatchAsked())
    g_nResult = FRONT_LAB_WATCH;
  Foot(fW, fH);
  Close();
  DrawTip();
}

bool LabKeys(void)
{
  for (int i = 0; i < VIEWS; ++i)
    if (Key(static_cast<unsigned>(kViewKeys[i]))) {
      g_nView = i;
      return true;
    }
  if (g_nView == VIEW_FARM) {
    if (Key('S'))
      StartStop();
    if (Key('H'))
      g_bHidden = !g_bHidden;
    if (PortLabFarmRunning() && (Key('P') || Key('R')))
      Pause();
  }
  if (Key(VK_ESCAPE) || Key('B'))
    g_nResult = FRONT_LAB_HOME;
  return true;
}

}  // namespace FrontKit
