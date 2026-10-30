// port_labwatch.cpp -- a game of the AI lab's farm watched in the lab (port_labwatch.h).
// A farm game has no commands: its replay is the map, the players and the seed, so played again
// it is the same game, from its start whether the worker has finished it or is still playing it.
// The lab copies it to <data>lab\watching.rep and plays it as the lab mod's game: SD art (the
// farm's pictures are the 8-bit game's), Fastest, Show All so every player's units are seen, and
// Replay TV pointing the camera at the fighting. A panel over the game's left side names the
// players with their bank, supply and units. At the frame the worker's game ended the lab folds
// the state (SyncHashFold) and compares it with the worker's, then holds the game there.
#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "port_labwatch.h"
#include "port_labfarm.h"
#include "../front/port_frontlobby.h"  // kSlotColours
#include "../front/port_ui.h"
#include "../gameplay/port_ai.h"
#include "../gameplay/port_launch.h"
#include "../platform/port_json.h"
#include "../platform/port_log.h"
#include "../platform/port_paths.h"
#include "../platform/port_settings.h"
#include "../platform/port_util.h"
#include "../render/port_gpu_hd.h"
#include "../render/port_overlay.h"
#include "../replays/port_tv.h"
#include "../tools/port_benchinfo.h"
#include "../../h/port_synchash.h"
#include "../../h/replay.h"  // replay_set_speed
#include "TrigAct.h"         // pause_game, unpause_game
#include "net_cmds.h"        // gameloop_init
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

namespace {

PortLogger g_log("hyperion_lab.log");

enum Check { CHECK_WAITING, CHECK_SAME, CHECK_DIFFERS, CHECK_NONE };

struct Watch {
  bool bAsked = false, bOn = false, bSetUp = false, bOver = false, bCameBack = false;
  bool bToEnd = false;         // to its end with nothing drawn (lab_watch's to_end)
  bool bLive = false;          // joined where its worker is, which keeps kLead frames ahead
  bool bOnFarm = false;        // its worker still plays it
  bool bCatching = false;      // live, and catching up with its worker
  unsigned dwWorkerFrame = 0;  // where its worker last said it is
  int nSame = 0, nDiffer = 0;  // the worker's states compared with the watcher's (HASH)
  unsigned dwDifferAt = 0;
  std::string replay, folder, title;
  uint32_t dwGame = 0;
  unsigned dwCheckFrame = 0;   // the frame its worker's game ended, 0 while not known
  std::string expected, got;   // the states, as 16 hex digits
  std::string ended;           // finished, last, or how it did not end
  std::vector<int> winners;
  Check check = CHECK_WAITING;
  uint64_t qLookedAt = 0;
  DWORD dwSpeedBefore = 6;
  bool bTvOurs = false;        // Replay TV was off, and the lab turned it on
};
Watch g_watch;
uint64_t g_qScreenAt;  // when the lab's screen was last drawn (PortLabWatchAsked)
uint64_t g_qPumpedAt;  // the farm pumped while catching up
const unsigned kLead = 48;    // a live game's worker plays up to so many frames ahead of the watcher
const unsigned kCatchFrom = 240;  // a watcher further behind its worker catches up, to kLead

const char *const kRaceNames[] = {"Zerg", "Terran", "Protoss"};
const char *const kColourNames[8] = {"Red", "Blue", "Teal", "Purple", "Orange", "Brown", "White", "Yellow"};

std::string Clock(unsigned dwFrame)
{
  const unsigned n = static_cast<unsigned>(dwFrame * 42ull / 1000);
  char sz[24];
  if (n >= 3600)
    snprintf(sz, sizeof sz, "%u:%02u:%02u", n / 3600, n / 60 % 60, n % 60);
  else
    snprintf(sz, sizeof sz, "%u:%02u", n / 60, n % 60);
  return sz;
}

std::string MapTitle(const std::string &map)
{
  std::string name = map.substr(map.find_last_of("\\/") + 1);
  if (name.size() > 3 && name[0] == '(' && name[2] == ')')
    name = name.substr(3);
  return name.substr(0, name.rfind('.'));
}

std::string Thousands(long long n)
{
  std::string s = std::to_string(n < 0 ? -n : n);
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
    s.insert(static_cast<size_t>(i), ",");
  return n < 0 ? "-" + s : s;
}

// The watched game's line in the project's games.jsonl, once its worker has ended it.
void LookUp(void)
{
  const std::string text = PortReadText(PortLabFarmFolder() + "games.jsonl");
  const std::string key = "{\"game\":" + std::to_string(g_watch.dwGame) + ",";
  for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + 1)) {
    if (at != 0 && text[at - 1] != '\n')
      continue;
    const size_t end = text.find('\n', at);
    PortJson j;
    if (!PortJsonParse(text.substr(at, end == std::string::npos ? std::string::npos : end - at).c_str(), &j))
      return;
    if (g_watch.title.empty())
      g_watch.title = MapTitle(j["map"].Str());
    g_watch.ended = j["ended"].Str();
    if (g_watch.ended != "finished" && g_watch.ended != "last") {
      g_watch.check = CHECK_NONE;
      return;
    }
    g_watch.dwCheckFrame = static_cast<unsigned>(j["end_frame"].Num());
    g_watch.expected = j["state"].Str();
    g_watch.winners.clear();
    for (size_t i = 0; i < j["winners"].size(); ++i)
      g_watch.winners.push_back(static_cast<int>(j["winners"].At(i).Num()));
    return;
  }
}

// Where the watched game's worker is, and whether it has ended it: its end is read from
// games.jsonl as soon as the farm has its GAME_END.
void Follow(void)
{
  bool bOnFarm = false;
  for (int i = 0; i < PortLabFarmSlots(); ++i) {
    const LabSlot &s = PortLabFarmSlot(i);
    if (s.dwShown != g_watch.dwGame)
      continue;
    if (s.dwFrame > g_watch.dwWorkerFrame)
      g_watch.dwWorkerFrame = s.dwFrame;
    bOnFarm = s.endReason.empty() && s.state != LAB_IDLE;
    if (!s.endReason.empty() && g_watch.check == CHECK_WAITING && g_watch.dwCheckFrame == 0)
      LookUp();
  }
  g_watch.bOnFarm = bOnFarm;
}

void Restore(void)
{
  if (g_watch.bLive)
    PortLabFarmWatch(g_watch.dwGame, false, 0);  // back to the farm's pace
  double dArt = 0.0;
  PortSettingNumber("hyperion", "art", &dArt);
  PortGpuHdSet(dArt != 0.0);
  gOptions = g_watch.dwSpeedBefore;
  if (g_watch.bTvOurs && PortTvOn())
    PortTvToggle();
}

void Leave(void)
{
  unpause_game(0);
  gameloop_init(4);
}

// ---- the panel over the game ----------------------------------------------------------------

bool LayerShown(void)
{
  return g_watch.bOn && gwGameMode == 3;
}

bool LayerDraw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  (void)nWidth;
  (void)nHeight;
  (void)fW;
  if (!LayerShown())
    return false;
  const float k = fH / 480.0f, fSize = 7.5f * k, fSmall = 6.5f * k, fPad = 5.0f * k, fLine = 10.0f * k;
  const UiColor ink = UiRgb(0xffffff), dim = UiRgb(0x9eccf3), shade = UiRgb(0x000000, 0.6f);
  // the game's sides: the bench's own seat, which made the replay and plays no side, has no race
  PortBenchPlayer read[12], players[12];
  const int nRead = PortBenchPlayersRead(read, 12);
  int nPlayers = 0;
  for (int i = 0; i < nRead; ++i)
    if (read[i].nRace >= 0 && read[i].nRace < 3)
      players[nPlayers++] = read[i];
  std::string check;
  UiColor checkInk = dim;
  switch (g_watch.check) {
    case CHECK_SAME:
      check = "The same game as the farm's";
      checkInk = UiRgb(0x7bd88f);
      break;
    case CHECK_DIFFERS:
      check = "Not the farm's game: its state differs";
      checkInk = UiRgb(0xff8a65);
      break;
    case CHECK_NONE:
      check = "Its worker did not finish it: no check";
      break;
      default:
      if (g_watch.bLive && g_watch.nDiffer > 0) {
        check = "Differs from the farm's at " + Clock(g_watch.dwDifferAt);
        checkInk = UiRgb(0xff8a65);
      } else if (g_watch.bLive && g_watch.nSame > 0) {
        check = "Live, the same as the farm's: " + std::to_string(g_watch.nSame) +
                (g_watch.nSame == 1 ? " check" : " checks");
        checkInk = UiRgb(0x7bd88f);
      } else if (g_watch.bLive) {
        check = "Live with the farm's game";
      } else {
        check = g_watch.dwCheckFrame ? "Checked against the farm's at " + Clock(g_watch.dwCheckFrame)
                                     : std::string("Still playing on the farm");
      }
  }
  const float w = 170.0f * k;
  const float h = fPad * 2 + fLine * 3.2f + nPlayers * fLine * 2.1f + fLine * (g_watch.bOver ? 3.2f : 2.2f);
  const float x0 = fX + 6.0f * k, y0 = fY + 24.0f * k;
  UiFill(x0, y0, w, h, shade, 3.0f * k);
  float y = y0 + fPad + fSize;
  const float x = x0 + fPad;
  UiText(x, y, UI_FACE_BOLD, fSize, 0.0f, ink, ("Game " + std::to_string(g_watch.dwGame)).c_str());
  UiText(x + 48.0f * k, y, UI_FACE_TEXT, fSize, 0.0f, dim, (g_watch.title + " \xc2\xb7 " + Clock(oc_dwReplayFrame)).c_str());
  if (g_watch.bLive && !g_watch.bOver)
    UiText(x0 + w - fPad - 22.0f * k, y, UI_FACE_BOLD, fSmall, 0.0f, UiRgb(0xff5a4e), "LIVE");
  y += fLine * 1.4f;
  for (int i = 0; i < nPlayers; ++i) {
    const PortBenchPlayer &p = players[i];
    const unsigned rgb = p.nSlot < 8 ? FrontKit::kSlotColours[p.nSlot] : 0xb0b0b0;
    UiFill(x, y - fSize * 0.75f, fSize * 0.8f, fSize * 0.8f, UiRgb(rgb), 1.0f * k);
    const std::string who = std::string(p.nSlot < 8 ? kColourNames[p.nSlot] : "Player") + " \xc2\xb7 " +
                            kRaceNames[p.nRace] + " \xc2\xb7 " +
                            AiLevelName(AiProfileOf(p.nSlot).bLevel) + (p.bLeft ? "" : " \xc2\xb7 out");
    UiText(x + fSize * 1.2f, y, UI_FACE_TEXT, fSize, 0.0f, p.bLeft ? ink : dim, who.c_str());
    y += fLine;
    const std::string bank = Thousands(p.nMinerals) + " minerals \xc2\xb7 " + Thousands(p.nGas) + " gas \xc2\xb7 " +
                             std::to_string(p.nSupplyUsed) + "/" + std::to_string(p.nSupplyMax) + " supply \xc2\xb7 " +
                             std::to_string(p.nUnits) + " units";
    UiText(x + fSize * 1.2f, y, UI_FACE_TEXT, fSmall, 0.0f, dim, bank.c_str());
    y += fLine * 1.1f;
  }
  y += fLine * 0.3f;
  UiText(x, y, UI_FACE_TEXT, fSmall, 0.0f, checkInk, check.c_str());
  if (g_watch.bOver) {
    y += fLine * 1.3f;
    const std::string result =
        g_watch.ended == "last"
            ? (g_watch.winners.empty() ? std::string("Nobody left")
                                       : std::string(g_watch.winners[0] < 8 ? kColourNames[g_watch.winners[0]] : "A player") +
                                             " wins")
            : std::string("The length limit: a draw");
    UiText(x, y, UI_FACE_BOLD, fSize, 0.0f, ink, result.c_str());
    y += fLine;
    UiText(x, y, UI_FACE_TEXT, fSmall, 0.0f, dim, "Esc goes back to the lab");
  } else {
    y += fLine;
    UiText(x, y, UI_FACE_TEXT, fSmall, 0.0f, dim, "B goes back to the lab");
  }
  return true;
}

bool LayerKey(unsigned vk, bool bDown, bool bRepeat)
{
  (void)bRepeat;
  if (!LayerShown() || !bDown)
    return false;
  if (vk == 'B' || (vk == VK_ESCAPE && g_watch.bOver)) {
    Leave();
    return true;
  }
  return false;
}

const PortOverlayLayer kLayer = {"labwatch", PORT_OVERLAY_LAB, PORT_OVERLAY_OWN_COUNTERS, LayerShown, NULL,
                                 LayerDraw, NULL, NULL, LayerKey, NULL};
PORT_OVERLAY_LAYER(kLayer);

}  // namespace

bool PortLabWatchAsk(uint32_t dwGame, const std::string &map, std::string *pWhy, bool bLive)
{
  char szGame[16];
  snprintf(szGame, sizeof szGame, "%06u", dwGame);
  const std::string folder = PortLabFarmFolder() + "games\\" + szGame;
  const std::string lab = std::string(PortDataFolder()) + "lab";
  CreateDirectoryA(lab.c_str(), NULL);
  const std::string replay = lab + "\\watching.rep";
  if (!CopyFileA((folder + "\\run.rep").c_str(), replay.c_str(), FALSE)) {
    *pWhy = "The game's replay could not be copied.";
    return false;
  }
  g_watch = Watch();
  g_watch.bAsked = true;
  g_watch.replay = replay;
  g_watch.folder = folder;
  g_watch.dwGame = dwGame;
  g_watch.title = map.empty() ? std::string() : MapTitle(map);
  LookUp();
  Follow();
  // live: its worker held from now, where it is, while the game starts here
  g_watch.bLive = bLive && g_watch.bOnFarm;
  if (g_watch.bLive)
    PortLabFarmWatch(dwGame, true, g_watch.dwWorkerFrame + kLead);
  g_log("watching game %u%s: %s", dwGame, g_watch.bLive ? " live" : "", folder);
  return true;
}

bool PortLabWatchAsked(void)
{
  g_qScreenAt = PortNowMs();
  return g_watch.bAsked;
}

bool PortLabWatchCommand(const PortJson &j, std::string *pOut)
{
  if (j["game"].kind == PortJson::NUMBER) {
    if (g_watch.bOn || g_watch.bAsked) {
      *pOut = "a game is watched already";
      return false;
    }
    if (PortNowMs() - g_qScreenAt > 500) {
      *pOut = "the lab's screen is not shown";
      return false;
    }
    const uint32_t dwGame = static_cast<uint32_t>(j["game"].d);
    std::string map;
    for (int i = 0; i < PortLabFarmSlots(); ++i)
      if (PortLabFarmSlot(i).dwShown == dwGame)
        map = PortLabFarmSlot(i).shownMap;
    const bool bToEnd = j["to_end"].kind == PortJson::BOOL && j["to_end"].b;
    if (!PortLabWatchAsk(dwGame, map, pOut, !bToEnd && j["live"].kind == PortJson::BOOL && j["live"].b))
      return false;
    g_watch.bToEnd = bToEnd;
  } else if (j["to_end"].kind == PortJson::BOOL && !g_watch.bLive) {
    g_watch.bToEnd = j["to_end"].b;  // the game watched, the rest of it at once
  } else if (j["leave"].kind == PortJson::BOOL && j["leave"].b && g_watch.bOn && gwGameMode == 3) {
    Leave();
  }
  static const char *const kChecks[] = {"waiting", "same", "differs", "none"};
  *pOut = std::string("\"watching\":") + (g_watch.bOn || g_watch.bAsked ? "true" : "false") +
          ",\"game\":" + std::to_string(g_watch.dwGame) + ",\"title\":" + PortJsonQuote(g_watch.title) +
          ",\"frame\":" + std::to_string(g_watch.bOn ? oc_dwReplayFrame : 0) + ",\"check_frame\":" +
          std::to_string(g_watch.dwCheckFrame) + ",\"check\":\"" + kChecks[g_watch.check] + "\",\"got\":" +
          PortJsonQuote(g_watch.got) + ",\"expected\":" + PortJsonQuote(g_watch.expected) + ",\"over\":" +
          (g_watch.bOver ? "true" : "false") + ",\"catching\":" + (PortLabWatchCatching() ? "true" : "false") +
          ",\"live\":" + (g_watch.bLive ? "true" : "false") + ",\"worker_frame\":" + std::to_string(g_watch.dwWorkerFrame) +
          ",\"same\":" + std::to_string(g_watch.nSame) + ",\"differ\":" + std::to_string(g_watch.nDiffer) +
          ",\"differ_at\":" + std::to_string(g_watch.dwDifferAt);
  return true;
}

int PortLabWatchLaunch(void)
{
  if (!g_watch.bAsked)
    return FALSE;
  g_watch.bAsked = false;
  g_watch.dwSpeedBefore = gOptions;
  PortGpuHdSet(0);  // SD art, as the farm's pictures are
  const int nStarted = PortLaunchWatchReplay(g_watch.replay.c_str());
  if (!nStarted) {
    g_log("game %u: its replay did not start", g_watch.dwGame);
    Restore();
    return FALSE;
  }
  g_watch.bOn = true;
  return nStarted;
}

void PortLabWatchFrame(void)
{
  if (!g_watch.bOn || g_watch.bOver)
    return;
  if (!g_watch.bSetUp) {
    g_watch.bSetUp = true;
    replay_set_speed(6, 1, 0);  // Fastest
    sgbShowAll_g = 1;           // the replay panel's Show All
    if (!PortTvOn()) {
      PortTvToggle();
      g_watch.bTvOurs = true;
    }
  }
  PortBenchPlayersNote();
  // nothing is drawn while catching up, and so the frame does not pump the farm: its workers'
  // messages read here
  if (PortLabWatchCatching() && PortNowMs() - g_qPumpedAt >= 16) {
    g_qPumpedAt = PortNowMs();
    PortLabFarmPump();
  }
  Follow();
  if (g_watch.bLive) {
    // its worker plays a little ahead, so its states come before the watcher's frame does
    PortLabFarmWatch(g_watch.dwGame, g_watch.bOnFarm, oc_dwReplayFrame + kLead);
    uint64_t qWorker;
    if (PortLabFarmHash(g_watch.dwGame, oc_dwReplayFrame, &qWorker)) {
      if (SyncHashFold() == qWorker) {
        ++g_watch.nSame;
      } else if (g_watch.nDiffer++ == 0) {
        g_watch.dwDifferAt = oc_dwReplayFrame;
        g_log("game %u: differs from its worker at frame %u", g_watch.dwGame, oc_dwReplayFrame);
      }
    }
  }
  // the worker's end, when it comes
  if (g_watch.check == CHECK_WAITING && g_watch.dwCheckFrame == 0 && PortNowMs() - g_watch.qLookedAt > 1000) {
    g_watch.qLookedAt = PortNowMs();
    LookUp();
  }
  if (g_watch.check != CHECK_WAITING || g_watch.dwCheckFrame == 0 || oc_dwReplayFrame < g_watch.dwCheckFrame)
    return;
  const uint64_t q = SyncHashFold();
  char sz[24];
  snprintf(sz, sizeof sz, "%016llX", static_cast<unsigned long long>(q));
  g_watch.got = sz;
  g_watch.check = oc_dwReplayFrame == g_watch.dwCheckFrame && g_watch.got == g_watch.expected ? CHECK_SAME : CHECK_DIFFERS;
  g_watch.bOver = true;
  pause_game();
  g_log("game %u at frame %u: %s, the farm's %s: %s", g_watch.dwGame, oc_dwReplayFrame, g_watch.got, g_watch.expected,
        g_watch.check == CHECK_SAME ? "the same" : "differs");
}

bool PortLabWatchCatching(void)
{
  if (!g_watch.bOn || g_watch.bOver || gwGameMode != 3)
    return false;
  if (g_watch.bToEnd)
    return g_watch.dwCheckFrame != 0;
  if (!g_watch.bLive)
    return false;
  const unsigned dwBehind = g_watch.dwWorkerFrame > oc_dwReplayFrame ? g_watch.dwWorkerFrame - oc_dwReplayFrame : 0;
  if (!g_watch.bCatching && dwBehind > kCatchFrom)
    g_watch.bCatching = true;
  else if (g_watch.bCatching && dwBehind <= kLead)
    g_watch.bCatching = false;
  return g_watch.bCatching;
}

bool PortLabWatchHold(void)
{
  if (!g_watch.bOn || g_watch.bOver || gwGameMode != 3)
    return false;
  Follow();
  return g_watch.bOnFarm && g_watch.check == CHECK_WAITING && g_watch.dwCheckFrame == 0 &&
         oc_dwReplayFrame >= g_watch.dwWorkerFrame;
}

bool PortLabWatchAfterGame(void)
{
  if (!g_watch.bOn)
    return false;
  g_watch.bOn = false;
  g_watch.bCameBack = true;
  Restore();
  g_log("game %u: no longer watched", g_watch.dwGame);
  return true;
}

bool PortLabWatchCameBack(void)
{
  // a replay left by Esc or its menu goes to the main menu, not by way of PortFrontCampaign
  PortLabWatchAfterGame();
  const bool b = g_watch.bCameBack;
  g_watch.bCameBack = false;
  return b;
}

bool PortLabWatchShown(void)
{
  return LayerShown();
}

bool PortLabWatchLive(void)
{
  return LayerShown() && g_watch.bLive && gPlayback != 0;
}
