// port_record.cpp -- --record=<frames>[,<path>]: a game played by itself to a frame, left as a
// player leaves it, and its replay kept (the replay harness's hxrecord,
// tools/replaytest/replaytest.py; docs/modules/replays.md, "Recording").
//
// At the frame, or sooner when the local player has lost, the game ends through gameloop_init
// as Quit Mission does, so free_game writes LastReplay; that is copied to the path and the
// program exits, or under the harness (HYPERION_DLLS set) goes back to the menus, where the
// observer ends the run. --record-orders=<n> has the local player give orders every n frames,
// so the replay holds commands that name units: up to 12 of its units, the table's
// last first, sent by 1.17's own calls to right-click or move to one of another player's last
// eight, and in a game with a mod's units its spells and research. In a game of Hyperion's
// selection (stage S) up to 255, sent in chunks, and rounds of its own: the buildings
// of a kind train together, and a control group is made of the selection and called back. The
// rounds follow the frame, so a game loaded from a save gives the orders the game saved went on
// to give (the harness's checkpoints).
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "../platform/port_paths.h"  // PortCommandOptions, PortDataFolder
#include "port_record.h"
#include "oc_data.h"
#include "CUnit.h"
#include "CSprite.h"
#include "../../h/net_cmds.h"     // gameloop_init, send_net_cmd_*
#include "../../h/rclick.h"       // show_target_x
#include "../../h/playertables.h"
#include "../../h/poollimits.h"
#include "../../h/selectlimits.h"
#include "../../h/typelimits.h"
#include "../../h/saveload.h"     // get_tech_researched, the upgrade levels
#include "../../h/statbtn.h"      // is_tech_in_progress, is_upgrade_in_progress
#include "../../h/game_evt.h"     // game_key_down_num
#include "../data/port_moddata.h"
#include "../gameplay/port_trainer.h"
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"
#include "../tools/port_startup.h"  // PortStartupEnded

extern CUnit sgUnitsMem[POOL_UNITS_STORE];

namespace {

DWORD g_dwFrames;      // 0: not recording
char g_szPath[MAX_PATH];
bool g_bEnding;
DWORD g_dwOrdersEvery;  // --record-orders, 0 for none
// --record-orders <frames>,queue: the moves and right-clicks are queued (shift), so a unit's
// queue fills to 8 under the order limit of HX_OPT_ORDER_QUEUE (hxrules.h)
BYTE g_bQueued;

const BYTE kOrderMove = 0x06;

// The buildings whose kind a round of Hyperion's selection trains from together, and what each
// trains: the Command Center and SCV, the Barracks and Marine, the Nexus and Probe, the Gateway
// and Zealot.
const WORD kTrains[][2] = {{0x6A, 0x07}, {0x6F, 0x00}, {0x9A, 0x40}, {0xA0, 0x41}};

bool OnMap(CUnit *pUnit);

// A round of Hyperion's selection only (SelectionWide), so the rounds of every game recorded
// before it, and of their saves, stay as they were. True when it sent its commands.
bool WideRound(DWORD dwRound, CUnit **apMine, int nMine, CUnit *pTarget) {
  if (!SelectionWide())
    return false;
  // the buildings of the kind the player has most of, each selected, told to train one
  if (dwRound % 5 == 3) {
    CUnit *apBuildings[SELECTION_WIDE];
    int nBest = 0;
    WORD wTrain = 0;
    for (const auto &t : kTrains) {
      CUnit *apOf[SELECTION_WIDE];
      int nOf = 0;
      for (int i = 0; i < g_pools.units && nOf < SelectionMax(); i++) {
        CUnit *pUnit = &sgUnitsMem[i];
        if (OnMap(pUnit) && pUnit->bPlayer == g_LocalNationID && pUnit->wType == t[0])
          apOf[nOf++] = pUnit;
      }
      if (nOf > nBest) {
        nBest = nOf;
        wTrain = t[1];
        memcpy(apBuildings, apOf, nOf * sizeof(CUnit *));
      }
    }
    if (nBest == 0)
      return false;
    send_net_cmd_Select(nBest, apBuildings);
    send_net_cmd_Build(wTrain);
    return true;
  }
  // the selection made control group 1, then that group called back and sent at the target
  if (dwRound % 7 == 6) {
    send_net_cmd_Select(nMine, apMine);
    send_net_cmd_GroupUnits(0, 1, NULL, 0);
    return true;
  }
  if (dwRound % 7 == 3 && GroupRow(g_LocalHumanID, 1)[0] != 0) {
    game_key_down_num(1);
    send_net_cmd_RightClick(pTarget->pSprite->wPosX, pTarget->pSprite->wPosY, pTarget, pTarget->wType, g_bQueued);
    return true;
  }
  return false;
}

void Parse() {
  static bool s_bDone;
  if (s_bDone)
    return;
  s_bDone = true;
  g_dwOrdersEvery = strtoul(PortCommandOptions()->szRecordOrders, NULL, 10);
  g_bQueued = strstr(PortCommandOptions()->szRecordOrders, ",queue") != NULL;
  const char *psz = PortCommandOptions()->szRecord;
  if (!psz[0])
    return;
  g_dwFrames = strtoul(psz, NULL, 10);
  const char *pComma = strchr(psz, ',');
  if (pComma)
    lstrcpynA(g_szPath, pComma + 1, MAX_PATH);
}

// A unit on the map, alive, and not inside another.
bool OnMap(CUnit *pUnit) {
  return pUnit->pSprite != NULL && !pUnit->IsDeadOrDying() && !(pUnit->dwFlags & CUnit::UF_InTransport);
}

void Orders() {
  if (!g_dwOrdersEvery || oc_dwReplayFrame == 0 || oc_dwReplayFrame % g_dwOrdersEvery != 0)
    return;
  if (PortTrainerWatching())
    return;  // a trainer line's commands are being played
  const DWORD dwRound = oc_dwReplayFrame / g_dwOrdersEvery;
  // a selection of 12, or of 255 in a game of Hyperion's selection (selectlimits.h)
  CUnit *apMine[SELECTION_WIDE];
  int nMine = 0;
  int nSkip = static_cast<int>(dwRound % 8);  // the target: another player's unit, this far from the end
  CUnit *pTarget = NULL;
  for (int i = g_pools.units - 1; i >= 0 && !(pTarget && nMine == SelectionMax()); i--) {
    CUnit *pUnit = &sgUnitsMem[i];
    if (!OnMap(pUnit))
      continue;
    if (pUnit->bPlayer == g_LocalNationID) {
      if (nMine < SelectionMax())
        apMine[nMine++] = pUnit;
    } else if (!pTarget && nSkip-- == 0) {
      pTarget = pUnit;
    }
  }
  if (!nMine || !pTarget)
    return;
  // The whole selection, then one fewer, then the whole again: a new selection, one taken
  // out of it, and one put back.
  if (dwRound % 3 == 1 && nMine > 2)
    nMine--;
  // The last selection sent is taken as the game's, which a save keeps: so a game loaded from
  // a save sends what the game saved sent, a change to the selection where it sent one.
  memcpy(NetSelected(), SelectedRow(g_LocalHumanID), SelectionMax() * sizeof(CUnit *));
  int nButtons = 0;
  const ModCardButton *pButtons = ModCardButtons(&nButtons);
  // Every fifth round, two after the casts, a building researches the first mod tech its card
  // offers not yet had, or the first upgrade not at its most, passing over those in progress.
  if (dwRound % 5 == 4)
    for (int i = 0; i < nMine; i++)
      for (int k = 0; k < nButtons; k++) {
        if (pButtons[k].wCard != apMine[i]->wType)
          continue;
        const WORD w = pButtons[k].wTech;
        if (pButtons[k].bResearch && !get_tech_researched(g_LocalNationID, w) &&
            !is_tech_in_progress(w, g_LocalNationID)) {
          send_net_cmd_Select(1, &apMine[i]);
          send_net_cmd_Research(w);
          return;
        }
        if (pButtons[k].bUpgrade && get_upgrade_level(g_LocalNationID, w) < get_max_upgrade_level(g_LocalNationID, w) &&
            !is_upgrade_in_progress(w, g_LocalNationID)) {
          send_net_cmd_Select(1, &apMine[i]);
          send_net_cmd_Upgrade(w);
          return;
        }
      }
  // Every fifth round a mod's unit casts, its card's casts taking turns: one with no target
  // where it stands, an aimed one at the round's target.
  if (dwRound % 5 == 2) {
    for (int i = 0; i < nMine; i++) {
      const ModCardButton *apCasts[9];
      int nCasts = 0;
      for (int k = 0; k < nButtons && UnitTypeMod(apMine[i]->wType) && nCasts < 9; k++)
        if (pButtons[k].wUnit == 0xFFFF && !pButtons[k].bResearch && !pButtons[k].bUpgrade &&
            pButtons[k].wCard == apMine[i]->wType)
          apCasts[nCasts++] = &pButtons[k];
      if (!nCasts)
        continue;
      const ModCardButton *pCast = apCasts[(dwRound / 5) % nCasts];
      send_net_cmd_Select(1, &apMine[i]);
      if (pCast->bTarget)
        send_net_cmd_Action(static_cast<WORD>(PosS(pTarget->wPositionX)),
                            static_cast<WORD>(PosS(pTarget->wPositionY)), pTarget, pTarget->wType,
                            static_cast<order_id_t>(pCast->wOrder), 0);
      else
        send_net_cmd_Action(static_cast<WORD>(PosS(apMine[i]->wPositionX)),
                            static_cast<WORD>(PosS(apMine[i]->wPositionY)), NULL, 0xE4,
                            static_cast<order_id_t>(pCast->wOrder), 0);
      return;
    }
  }
  if (WideRound(dwRound, apMine, nMine, pTarget))
    return;
  send_net_cmd_Select(nMine, apMine);
  const int nX = pTarget->pSprite->wPosX, nY = pTarget->pSprite->wPosY;
  // Every fourth round a move to the ground beside the target, shown as a player's click
  // shows it: the black X, which the view moves (gamemap_lclick_target).
  if (dwRound % 4 == 3) {
    const int nGroundX = nX > 64 ? nX - 48 : nX + 48;
    show_target_x(nGroundX, nY, NULL);
    send_net_cmd_RightClick(nGroundX, nY, NULL, 0xE4, g_bQueued);
    return;
  }
  if (dwRound % 2 == 0)
    send_net_cmd_RightClick(nX, nY, pTarget, pTarget->wType, g_bQueued);
  else
    send_net_cmd_Action(static_cast<WORD>(nX), static_cast<WORD>(nY), pTarget, pTarget->wType, kOrderMove,
                        g_bQueued);
}

}  // namespace

void PortRecordFrame() {
  Parse();
  if (gPlayback)
    return;
  Orders();
  if (!g_dwFrames || g_bEnding)
    return;
  const bool bLost = g_LocalNationID < PLAYER_LOBBY_117 && gbVictoryState[g_LocalNationID] != 0;
  if (oc_dwReplayFrame >= g_dwFrames || bLost) {
    g_bEnding = true;
    gameloop_init(4);  // to the menus, as Quit Mission leaves
  }
}

void PortRecordAfterGame() {
  if (!g_bEnding)
    return;
  char szReplay[MAX_PATH];
  lstrcpynA(szReplay, PortDataFolder(), MAX_PATH);  // where write_replay puts it
  if (g_szPath[0] && lstrlenA(szReplay) + 32 < MAX_PATH) {
    lstrcatA(szReplay, "maps\\replays\\LastReplay.rep");
    CopyFileA(szReplay, g_szPath, FALSE);
  }
  g_bEnding = false;
  g_dwFrames = 0;
  if (GetEnvironmentVariableA("HYPERION_DLLS", NULL, 0) == 0) {
    PortStartupEnded(0);
    ExitProcess(0);
  }
}
