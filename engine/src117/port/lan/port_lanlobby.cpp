// port_lanlobby.cpp -- a LAN room's match made and joined (docs/modules/lan.md): the
// room's launch (port_lanroom.cpp) played through 1.17's own steps, so its lobby, map download
// and start run as on its LAN screens and the game is 1.17's multiplayer game. Presentation
// only: the lobby's commands are 1.17's own, as its players would send them.
//
// The maker does what Create Game's Ok does with the map the room sent (PortCreateNetGame,
// gluMap.cpp), the others what Join's Ok does with the one game the provider lists. In 1.17's
// chat room PortLanLobbyTick, from the chat room's poll, does what the players would: each sets
// its race, and the maker, once every person is in, makes the computers and starts the game.
// None of it is seen: until the game's first frame the window stays black under the motion's
// cover, the map and players over it (StartDraw) and the input held, and a popup the chat room
// would show goes to PortLanChatWhy. A browser's seat, with no GPU presenter, shows 1.17's.
#include "../platform/port_log.h"  // PortLogger
#include "../platform/port_util.h"  // PortNowMs
#include <stdio.h>
#include <string.h>
#include <string>
#include "../platform/port_json.h"
#include "port_lanclient.h"
#include "port_lanfeed.h"
#include "port_lanlobby.h"
#include "port_lanstream.h"
#include "port_stormlan.h"
#include "Storm.h"            // SStrCopy
#include "fd_sessn.h"         // IsSessionComplete
#include "gametype.h"         // IsMeleeTemplate
#include "gluChatSlot.h"      // CountSides
#include "gluMap.h"           // PortCreateNetGame
#include "grid.h"             // IsBroodwar
#include "net_chat_cmds.h"    // create_net_cmd_change_race, create_net_cmd_change_slot
#include "net_glue.h"         // NetInit, ListGames, LookupGameHdr, net_glue_OnInitData
#include "net_cmds.h"         // PlayerData
#include "opt_dlgs.h"         // PortQuitToMenu
#include "uiMulti.h"          // StartNetGame
#include "playertables.h"     // PlayerVictoryStateTable
#include "../../gen/globals117.h"  // gPlayerData, g_LocalNationID, gbMultiPlayerMode, gLocalPlayerName
#include "pause.h"  // get_lobby_state
#include "../platform/port_globals117.h"
#include "../front/port_text.h"
#include "../front/port_ui.h"            // UiFill, UiText
#include "../render/port_motion.h"       // PortMotionCover, PortMotionCovered, PortMotionIsAction
#include "../render/port_overlay.h"      // PORT_OVERLAY_LAYER
#include "../render/port_present.h"      // PortPresentGpu
#include "../gameplay/port_handicap.h"   // HandicapChooseNet
#include "../gameplay/port_mutators.h"   // MutatorsChooseNet
#include "../../h/hxrules.h"             // RulesChooseNext
#include "../../h/playertables.h"        // PlayersSeatsBegin

#ifndef OC_DLL_MODE
extern GameHeader gGameHeader;
#endif

#ifndef OC_DLL_MODE
extern TFD_SESSION *dword_672960;  // the map download's session, while one runs
#endif

namespace {

enum { TYPE_HUMAN = 2, TYPE_COMPUTER = 5, TYPE_OPEN = 6, SLOT_COMPUTER = 0, MELEE_KEY = 2 | 1 << 16, FFA_KEY = 3 | 1 << 16 };
enum { RACE_ZERG = 0, RACE_TERRAN = 1, RACE_PROTOSS = 2, RACE_RANDOM = 6, RACE_TRIES = 12 };

// The match being made: the launch, and how far the lobby has got.
struct Plan {
  bool bOn = false;
  bool bCreator = false;
  int nRace = 6;
  int nHumans = 1;
  std::vector<int> computers;  // their races, in the room's seat order
  std::vector<std::pair<std::string, int>> people;  // each person's name and race
  bool bStarted = false, bLeaving = false;
  uint64_t qwLastAct = 0, qwRaceSent = 0;
  int nRaceTries = 0;  // this player's race asked for; a lobby that will not take it is let be
  int nMatch = 0;
  std::string mapName, who;  // what the start shows over the cover
} g_plan;
bool g_bBackToFront;  // a room's match has ended: home after its score screen
std::string g_why;    // why the chat room closed before the game, for the home screen

const PortLogger Log("hyperion_lan.log", PortLogger::SHARED, "lobby: ");

// A race the room sent. Only the four a slot's list offers are asked for: another number is one
// no lobby ever sets, and a seat would ask for it until the match was given up on.
int LobbyRace(int nRace)
{
  if (nRace == RACE_ZERG || nRace == RACE_TERRAN || nRace == RACE_PROTOSS)
    return nRace;
  return RACE_RANDOM;
}

// The lobby is unseen: a room's match under the cover, which the GPU presenter draws.
bool Unseen(void)
{
  return g_plan.bOn && PortMotionCovered();
}

// "Alpha, Bravo and a computer": the people by name, then the computers.
std::string Who(void)
{
  std::vector<std::string> names;
  for (const auto &p : g_plan.people)
    names.push_back(p.first);
  const size_t nComputers = g_plan.computers.size();
  if (nComputers > 0)
    names.push_back(PortTextNF("a computer", "{0} computers", static_cast<long long>(nComputers),
                               {std::to_string(nComputers)}));
  std::string s;
  for (size_t i = 0; i < names.size(); ++i)
    s = i == 0 ? names[i]
        : i + 1 == names.size() ? PortTextF("{0} and {1}", {s, names[i]})
                                : PortTextF("{0}, {1}", {s, names[i]});
  return s;
}

int CountType(BYTE bType)
{
  int n = 0;
  for (int i = 0; i < 8; ++i)
    n += gPlayerData[i].nType == bType;
  return n;
}

// Enumeration's per-game callback: the list is gGameHeaders_g; nothing to show.
void __stdcall NoList(intptr_t, intptr_t)
{
}

}  // namespace

bool PortLaunchNetGame(const PortJson &launch)
{
  g_plan = Plan();
  const bool bCreate = launch["role"].Str() == "create";
  const std::string token = launch["token"].Str();
  const unsigned long dwGame = static_cast<unsigned long>(launch["game"].Num());
  LanSetLaunch(LanClientAddress(), static_cast<int>(launch["port"].Num()), token.c_str(), dwGame,
               launch["name"].Str().c_str(), launch["stats"].Str().c_str());
  SStrCopy(gLocalPlayerName, LanPlayerName().c_str(), 25);
  gPlayback = 0;
  gbIsExpansion = static_cast<BYTE>(IsBroodwar() != 0);
  // as the main menu's Multiplayer leaves it (do_main_dlg)
  gbMultiPlayerMode = 1;
  TGAMEPARAMS *pParams = gametype_load_ums();
  if (pParams == NULL) {
    // the Use Map Settings template, which a damaged install or a mod's MPQ may not have
    Log("the game's templates were not read");
    return false;
  }
  *reinterpret_cast<TGAMEPARAMS *>(&gGameHeader.bGameType) = *pParams;
  SMemFree(pParams, "port_lanlobby.cpp", __LINE__, 0);
  if (!NetInit(LAN_PROVIDER)) {
    Log("the network did not start");
    return false;
  }
  g_plan.bCreator = bCreate;
  g_plan.nRace = LobbyRace(static_cast<int>(launch["race"].Num(6)));
  g_plan.nHumans = static_cast<int>(launch["humans"].Num(1));
  g_plan.nMatch = static_cast<int>(launch["match"].Num());
  const PortJson &seats = launch["seats"];
  // each seat's handicap: a person's by name, a computer's in the order the lobby seats them,
  // the same on every seat (port_handicap.h)
  std::vector<std::pair<std::string, int>> handicapPeople;
  std::vector<int> handicapComputers;
  for (size_t i = 0; i < seats.size(); ++i) {
    const int nHandicap = static_cast<int>(seats.At(i)["handicap"].Num(100));
    if (seats.At(i)["kind"].Str() == "computer") {
      g_plan.computers.push_back(LobbyRace(static_cast<int>(seats.At(i)["race"].Num(6))));
      handicapComputers.push_back(nHandicap);
    } else {
      g_plan.people.push_back(
          std::make_pair(seats.At(i)["name"].Str(), LobbyRace(static_cast<int>(seats.At(i)["race"].Num(6)))));
      handicapPeople.push_back(std::make_pair(seats.At(i)["name"].Str(), nHandicap));
    }
  }
  // the rules the host chose, the same on every seat (hxrules.h): a guest is told them here
  RulesChooseNext(launch["rules"].Str() == "hyperion" ? 1 : 0);
  HandicapChooseNet(handicapPeople, handicapComputers);
  // and the mutators (port_mutators.h)
  MutatorsChooseNet(MutatorsFromJson(launch["mutators"]));
  PlayersSeatsBegin();  // where the shuffle of the starts puts each lobby slot's record
  g_plan.mapName = launch["mapName"].Str(PortText("A match"));
  g_plan.who = Who();
  if (bCreate) {
    // the game type's template key, as the room's set-up chose it; a room before it said one
    // names Melee or Free For All
    const bool bFfa = launch["type"].Str() == "ffa";
    const int nKey = static_cast<int>(launch["key"].Num(bFfa ? FFA_KEY : MELEE_KEY));
    if (!IsMeleeTemplate(static_cast<BYTE>(nKey), static_cast<BYTE>(nKey >> 8), static_cast<WORD>(nKey >> 16))) {
      Log("no melee template for the key 0x%X", nKey);
      return false;
    }
    char szPath[MAX_PATH];
    SStrCopy(szPath, LanClientMapPath().c_str(), MAX_PATH);
    char szName[0x20];
    SStrCopy(szName, launch["mapName"].Str("A match").c_str(), sizeof szName);
    int nSpeed = static_cast<int>(launch["speedN"].Num(launch["speed"].Str() == "fastest" ? 6 : 4));
    if (nSpeed < 0 || nSpeed > 6)
      nSpeed = 6;
    const int nRes = PortCreateNetGame(szPath, szName, nKey, static_cast<BYTE>(nSpeed));
    if (nRes != 0) {
      Log("the game was not made from %s (0x%X)", szPath, nRes);
      return false;
    }
    Log("made the room's match %s", szName);
  } else {
    // the provider lists the room's match alone; it is joined as Join's Ok joins one
    ClearGameHeaders();
    ListGames(NoList, 0);
    const intptr_t hdr = LookupGameHdr(static_cast<int>(dwGame));
    if (hdr == 0 || !net_glue_OnInitData(hdr)) {
      Log("the room's match was not joined");
      return false;
    }
    Log("joined the room's match");
  }
  g_plan.bOn = true;
  g_why.clear();
  // the chat room and the loading under black from here, as a skirmish's start has them
  if (PortPresentGpu())
    PortMotionCover();
  return true;
}

bool PortLanLobbyActive(void)
{
  return g_plan.bOn;
}

// After the game (main.cpp's main_game_run): how it ended for this player, to the room.
void PortLanAfterGame(void)
{
  if (!g_plan.bOn)
    return;
  const BYTE bState = g_LocalNationID < 8 ? gbVictoryState[g_LocalNationID] : 0;
  const char *pszHow = bState == 3 ? "won" : bState == 2 ? "lost" : bState == 5 ? "drew" : bState == 1 ? "left" : "ended";
  Log("the game ended: %s", pszHow);
  LanFeedSeatEnd();
  LanStreamGameEnded();
  LanClientResult((LanPlayerName() + " " + pszHow).c_str());
  LanClientPump();
  PortLanLobbyEnd();
  g_bBackToFront = true;
}

bool PortLanBackToFront(void)
{
  LanStreamScoreDone();
  const bool b = g_bBackToFront;
  g_bBackToFront = false;
  return b;
}

// Each frame of a LAN game (port_stormlan.cpp's LanGameFrame): the room told how far it has got,
// and, when the host ends the match, the game left as Quit Mission leaves it.
void PortLanGameFrame(uint32_t dwFrame)
{
  if (!g_plan.bOn)
    return;
  LanClientFrame(dwFrame);
  LanFeedSeatFrame(g_plan.nMatch, dwFrame);
  LanClientPump();
  if (LanClientTakeEnd() && !g_plan.bLeaving) {
    g_plan.bLeaving = true;
    Log("the room ended the match for this seat (the host's End, or its connection to the match lost); leaving it");
    PortQuitToMenu();
  }
}

void PortLanLobbyEnd(void)
{
  g_plan.bOn = false;
  LanSetLaunch(NULL, 0, NULL, 0, NULL, NULL);
}

// From the chat room's poll (gluChat.cpp), every pass of its loop.
void PortLanLobbyTick(void)
{
  if (!g_plan.bOn)
    return;
  LanClientPump();
  // the room ended the match before it started, in the lobby or its countdown: the chat room's
  // Cancel, which takes the poll off the chat room (gluChat_poll goes no further)
  if (LanClientTakeEnd()) {
    Log("the room ended the match before it started; leaving its lobby");
    PortChatPress(8);
    return;
  }
  if (g_plan.bStarted)
    return;
  const uint64_t qwNow = PortNowMs();
  if (qwNow - g_plan.qwLastAct < 250)
    return;
  g_plan.qwLastAct = qwNow;
  if (get_lobby_state() < 4 || g_LocalNationID >= 8)
    return;
  // this player's race, as a player picks it from the slot's list; a lobby that does not take it
  // after a few tries is left with the race it has, rather than asked for ever
  if (gPlayerData[g_LocalNationID].nRace != g_plan.nRace && qwNow - g_plan.qwRaceSent > 1500 &&
      g_plan.nRaceTries <= RACE_TRIES) {
    g_plan.qwRaceSent = qwNow;
    if (++g_plan.nRaceTries > RACE_TRIES)
      Log("the lobby did not take this seat's race; going on with the one it has");
    else
      create_net_cmd_change_race(static_cast<BYTE>(g_LocalNationID), static_cast<BYTE>(g_plan.nRace));
    return;
  }
  if (!g_plan.bCreator)
    return;
  // every person in: the computers into open slots
  if (CountType(TYPE_HUMAN) < g_plan.nHumans)
    return;
  // one lobby command a pass: 1.17 keeps one buffer for each kind, and a second sent before
  // the turn goes would take the first one's place
  if (CountType(TYPE_COMPUTER) < static_cast<int>(g_plan.computers.size())) {
    for (int i = 0; i < 8; ++i)
      if (gPlayerData[i].nType == TYPE_OPEN) {
        create_net_cmd_change_slot(static_cast<BYTE>(i), SLOT_COMPUTER);
        return;
      }
    return;
  }
  // the computers' races, once 1.17 has made them
  size_t k = 0;
  for (int i = 0; i < 8 && k < g_plan.computers.size(); ++i)
    if (gPlayerData[i].nType == TYPE_COMPUTER) {
      const int nWant = g_plan.computers[k++];
      if (gPlayerData[i].nRace != nWant) {
        create_net_cmd_change_race(static_cast<BYTE>(i), static_cast<BYTE>(nWant));
        return;
      }
    }
  // each person's race theirs, as they set it themselves
  for (const auto &p : g_plan.people)
    for (int i = 0; i < 8; ++i)
      if (gPlayerData[i].nType == TYPE_HUMAN && p.first.substr(0, 24) == gPlayerData[i].szName &&
          gPlayerData[i].nRace != p.second)
        return;
  // every person's race as theirs is, the map downloaded: Start Game
  if (dword_672960 != NULL && !IsSessionComplete(dword_672960))
    return;
  if (CountSides() < 2)
    return;
  g_plan.bStarted = true;
  Log("started the room's match");
  PortChatPress(7);
}

bool PortLanChatLeft(void)
{
  if (!g_plan.bOn)
    return false;
  Log("left the room's match's lobby");
  PortMotionArriveCancel();  // the cover off: the menus come next
  PortLanLobbyEnd();
  return true;
}

bool PortLanLobbyUnseen(void)
{
  return Unseen();
}

bool PortLanChatWhy(const char *pszWhy)
{
  if (!Unseen() || pszWhy == NULL)
    return false;
  // 1.17's line, its colour codes and line breaks as spaces
  g_why.clear();
  for (const char *p = pszWhy; *p; ++p)
    g_why += static_cast<unsigned char>(*p) < 0x20 ? ' ' : *p;
  Log("the chat room closed: %s", g_why.c_str());
  return true;
}

const char *PortLanTakeWhy(void)
{
  static std::string s_taken;
  if (g_why.empty())
    return NULL;
  s_taken.swap(g_why);
  g_why.clear();
  return s_taken.c_str();
}

// ---------------------------------------------------------------------------
// The match's start over the cover: the map's name, who plays, and that it is starting, on black.
// Over the cover a layer has the whole window to draw (port_overlay.cpp blacks only the game's
// box), so it paints its own black first.

namespace {

bool StartShown(void)
{
  return Unseen();
}

bool StartDraw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  (void)fX;
  (void)fY;
  (void)fW;
  (void)fH;
  if (!StartShown())  // asked whether it shows or not (port_overlay.h)
    return false;
  const float W = static_cast<float>(nWidth), H = static_cast<float>(nHeight);
  UiFill(0.0f, 0.0f, W, H, UiRgb(0x000000));
  const float k = H / 1080.0f;  // the front end's stage, 1080 units high
  const float fTitle = 40.0f * k, fLine = 22.0f * k, fSmall = 17.0f * k;
  const float fTitleSpacing = 2.0f * k;
  const char *pszStatus = PortText("Starting the match");
  const float yTitle = H * 0.46f;
  UiText((W - UiTextWidth(UI_FACE_WIDE, fTitle, fTitleSpacing, g_plan.mapName.c_str())) * 0.5f, yTitle, UI_FACE_WIDE,
         fTitle, fTitleSpacing, UiRgb(0xffffff), g_plan.mapName.c_str());
  UiText((W - UiTextWidth(UI_FACE_TEXT, fLine, 0.0f, g_plan.who.c_str())) * 0.5f, yTitle + 52.0f * k, UI_FACE_TEXT,
         fLine, 0.0f, UiRgb(0x9eccf3), g_plan.who.c_str());
  UiText((W - UiTextWidth(UI_FACE_TEXT, fSmall, 1.0f * k, pszStatus)) * 0.5f, yTitle + 96.0f * k, UI_FACE_TEXT, fSmall,
         1.0f * k, UiRgb(0x8a96a8), pszStatus);
  return true;
}

// The player's clicks and keys wait while the start shows: the chat room under it would take them.
bool StartHold(unsigned uMsg, unsigned long long wParam, long long lParam)
{
  (void)wParam;
  return StartShown() && PortMotionIsAction(uMsg, lParam);
}

const PortOverlayLayer kStartLayer = {
    "lanstart", PORT_OVERLAY_LANSTART, PORT_OVERLAY_OVER_COVER,
    StartShown, NULL, StartDraw, StartHold, NULL, NULL, NULL};
PORT_OVERLAY_LAYER(kStartLayer);

}  // namespace
