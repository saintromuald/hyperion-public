// port_hudstate.cpp -- the HUD's data layer (port_hudstate.h).
//
// The values are read when asked for, from the globals 1.17's console reads (statres,
// statdraw, statcmd, the minimap's buttons), so a HUD shows what the console would. The
// events come from hooks in the game's code, each placed where 1.17 itself tells the player:
// the under-attack warning, the error lines, the "research complete" and "unit ready"
// speech, the control groups; and from PortHudFrame, which compares the selection and the
// game's end with the frame before. All of it reads only, apart from the presses, which end
// in the handlers a click or a key on 1.17's console reaches, so the commands sent are those.
//
// No clock but the game's frames: the replay suite's observer turns every GetTickCount into
// 50 ms of its virtual clock, and a call here once moved four replays (2026-10-09).
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "CUnit.h"
#include "CSprite.h"
#include "oc_data.h"
#include "port_hudstate.h"
#include "port_hudlive.h"
#include "../../h/count.h"      // count_get_supplies_used, count_get_supplies_available
#include "../../h/dialog.h"     // ctrl_t
#include "../../h/dlg_ctrl.h"   // ctrl_find_ndx, ctrl_send_msg
#include "../../h/filters.h"    // do_ally_filter_dlg, do_msg_filter_dlg (through minimap_btn_command)
#include "../../h/game_evt.h"   // game_key_down_num
#include "../../h/hxrules.h"    // HxRules
#include "../../h/minimap.h"    // minimap_btn_command
#include "../../h/select.h"     // select_units
#include "../../h/selectlimits.h"  // the selection's arrays, 12 units or 255
#include "../../h/usel.h"       // sub_4BC5B0, a drag box's gather
#include "../render/port_view.h"  // PortViewCameraCentre
#include "../../h/net_cmds.h"   // PlayerData
#include "../../h/opt_dlgs.h"   // do_gamemenu_dlg
#include "../../h/playertables.h"
#include "../../h/saveload.h"   // get_upgrade_level
#include "../../h/order.h"      // status_get_str
#include "../../h/statcmd.h"    // statcmd_btn_t
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"
#include "../lab/port_labwatch.h"  // PortLabWatchLive
#include "../gameplay/port_watchgame.h"  // PortWatchLive
#include "../lan/port_lanfeed.h"  // PortLanWatchLive
#include "../platform/port_json.h"  // PortJsonQuote
#include "../../h/handicap.h"  // HandicapShields
#include "../../h/glues.h"     // glues_get_str
#include "../../h/net_cmds.h"  // IsGameHost
#include "../../h/playerlimits.h"  // PlayerLobbySlots, PlayerIsLobby
#include "../../h/replay.h"    // the replay's speed, its orders and their buttons' tests
#include "../front/port_gamemenu.h"  // PortReplayVisionToggle
#include "../replays/port_replayseek.h"  // PortReplaySeekTarget
#include "../render/port_maptext.h"  // MapTextUtf8
#include "../replays/port_tv.h"      // PortTvOn, PortFollowOn and their toggles
#include "port_hudstats.h"           // the players' stats
#include "../gameplay/port_ai.h"     // AiProfileOf, AiLevelName
#include "../front/port_text.h"       // PortText

#ifdef OC_DLL_MODE
#define sgbReplayPaused_g (*reinterpret_cast<int *>(OC_VA(0x67CC7Cu)))
#define sgReplayTicks_g (reinterpret_cast<DWORD *>(OC_VA(0x4EF660u)))
#else
extern int sgbReplayPaused_g;      // the replay is paused (replay.cpp)
extern DWORD sgReplayTicks_g[10];  // a turn's milliseconds at each game speed (replay.cpp)
#endif

namespace {

const int EVENTS_KEPT = 256;
PortHudEvent g_aEvents[EVENTS_KEPT];
unsigned int g_nSerial;            // the last event's
bool g_bStarted;                   // game.start is out for this game
BYTE g_bEnded;                     // the local player's victory state when it was last seen
CUnit *g_apSelected[SELECTION_WIDE];  // the selection at the last frame
int g_nSelected;
bool g_bAttacked;                  // in a replay, an alert.attack has been given
unsigned int g_nAttackFrame;       // and the frame of the last
int g_nGroupLast = -1;
int g_nSubgroupLast, g_nPageLast;  // the console's subgroup and page at the last frame
// the HUD's selection grid: the page it shows, kept as the first unit on it, its units a page
// (0 with no grid), and the unit the console reported on and its subgroup when it last looked
int g_nGridFirst, g_nGridPer;
const CUnit *g_pGridStat;
int g_nGridSub = -1;
const unsigned int ATTACK_FRAMES = 238;  // ten seconds at Fastest, 42 ms a frame

// The commands of the last minute of game time, a ring of frame numbers a player, for the
// console's player and the players' board; as many as a bot of SSCAIT's makes in a minute and more
// (1024 held the count there)
const int APM_KEPT = 4096;
unsigned int g_aanCommandFrames[PORT_HUD_PLAYERS][APM_KEPT];
int g_anCommands[PORT_HUD_PLAYERS];
bool g_bSeekDrag;  // the viewer drags the replay's position (PortHudSeekDrag)
const unsigned int APM_FRAMES = 1429;  // a minute at Fastest, 42 ms a frame
const unsigned int APM_FIRST = 240;    // ten seconds, before which a rate means little
unsigned int g_nGameFrom;             // the frame the game's commands are counted from

// The game's seats so far, so a player who leaves stays on the board.
bool g_abSeat[PORT_HUD_PLAYERS];

// A replay as it was at the last look (PortHudReplayWatch), its events given on a change.
bool g_bReplayLooked;
bool g_bReplayPaused;
int g_nReplayStep;
unsigned int g_dwReplayVision;

// The minimap's buttons by their control index (minimap_btn_activate).
const short MINIMAP_TERRAIN = 2, MINIMAP_MESSAGING = 3, MINIMAP_DIPLOMACY = 4;

void Emit(int nKind, const CUnit *pUnit, int nGroup = -1) {
  PortHudEvent &e = g_aEvents[g_nSerial % EVENTS_KEPT];
  e.nSerial = ++g_nSerial;
  e.nKind = nKind;
  e.nFrame = oc_dwReplayFrame;
  e.nPlayer = -1;
  e.nStep = -1;
  e.nView = -1;
  e.nUnitType = pUnit != NULL ? pUnit->wType : -1;
  e.x = pUnit != NULL ? static_cast<int>(pUnit->wPositionX) : -1;
  e.y = pUnit != NULL ? static_cast<int>(pUnit->wPositionY) : -1;
  e.bBuilding = pUnit != NULL && (gulUnitTypeFlags[pUnit->wType] & 1) != 0;
  e.nGroup = nGroup;
}

bool InGame(void) { return gwGameMode == 3; }

// The player the console is for: in a replay, 1.17's console follows the selected unit's
// owner (statres_animate), and so does this.
int HudPlayer(void) {
  if (gPlayback && byte_6AB4C0 != 0 && StatUnits()[0] != NULL)
    return StatUnits()[0]->bPlayer;
  return static_cast<int>(g_LocalNationID);
}

bool IsWorker(int nType) { return nType == 0x07 || nType == 0x29 || nType == 0x40; }

// What SC2 counts as the army: a unit of the player's that a group could hold, shown, and
// neither a worker nor an Overlord; Interceptors and Scarabs are their carriers'.
bool IsArmy(CUnit *p, int nPlayer) {
  if (p->bPlayer != nPlayer || p->pSprite == NULL || (p->pSprite->flags.b & 0x20) != 0)
    return false;
  const int nType = p->wType;
  if (IsWorker(nType) || nType == 0x2A || nType == 0x55 || nType == 0x56)
    return false;
  return p->IsMultiSelectable() && !p->IsDead();
}

ctrl_t *MinimapButton(short nIndex) {
  return sgpMinimapDlg != NULL ? ctrl_find_ndx(sgpMinimapDlg, nIndex) : NULL;
}

bool Shown(const ctrl_t *c) { return c != NULL && (c->dwFlags & 8) != 0; }

// A player's actions over the last minute of game time, as the overlay counts them; 0 for the
// first ten seconds.
int Apm(int nPlayer, unsigned int nFrame) {
  if (nPlayer < 0 || nPlayer >= PORT_HUD_PLAYERS)
    return 0;
  // from the newest back to the first a minute old
  int nRecent = 0;
  const int nKept = g_anCommands[nPlayer] < APM_KEPT ? g_anCommands[nPlayer] : APM_KEPT;
  for (int i = 0; i < nKept; ++i) {
    if (nFrame - g_aanCommandFrames[nPlayer][(g_anCommands[nPlayer] - 1 - i) % APM_KEPT] >= APM_FRAMES)
      break;
    ++nRecent;
  }
  // over the game so far until a minute of it has gone, as the overlay scales it
  const unsigned int nAge = nFrame - g_nGameFrom;
  const unsigned int nSpan = nAge < APM_FRAMES ? nAge : APM_FRAMES;
  return nSpan < APM_FIRST ? 0 : static_cast<int>(nRecent * APM_FRAMES / nSpan);
}

// The replay's speed as a step of eleven: 1.17's seven game speeds (gOptions), then Fastest
// doubled up to four times (sgdwSpeedMul_g), as order_replay_speedup steps them.
int ReplayStep(void) {
  int nStep = gOptions <= 6 ? static_cast<int>(gOptions) : 6;
  for (int nMul = replay_dlg_get_mult(); nMul > 1 && nStep < 10; nMul >>= 1)
    ++nStep;
  return nStep;
}

void NoteSeats(void) {
  for (int s = 0; s < PlayerLobbySlots() && s < PORT_HUD_PLAYERS; ++s)
    if (PlayerIsLobby(s) && (gPlayerData[s].nType == 1 || gPlayerData[s].nType == 2))
      g_abSeat[s] = true;
}

void ReadCard(PortHudState *s) {
  if (sgpStatCmdDlg == NULL)
    return;
  for (ctrl_t *c = ctrl_find_ndx(sgpStatCmdDlg, 1); c != NULL; c = c->pNext) {
    if (c->nIndex < 1)
      break;
    if (c->nIndex > 9)
      continue;
    PortHudButton &b = s->aCard[c->nIndex - 1];
    const statcmd_btn_t *pBtn = ctrl_p26(c);
    if ((c->dwFlags & 8) == 0 || pBtn == NULL)
      continue;
    b.bShown = true;
    b.bEnabled = (c->dwFlags & 2) == 0;
    b.nIcon = pBtn->wIcon;
    // as statcmd_show_btn_tooltip reads it: the hotkey, the kind, then the text; a grey
    // button shows its reason instead, the whole string, by the upgrade's level for one
    const char *psz = status_get_str(pBtn->wStatusStrId);
    if (psz == NULL || psz[0] == '\0')
      continue;
    b.cHotkey = static_cast<char>(tolower(static_cast<unsigned char>(psz[0])));
    b.nCostKind = static_cast<unsigned char>(psz[1]);
    const char *pszText = psz + 2;
    if (!b.bEnabled && pBtn->wDisabledStrId != 0) {
      WORD wStr = pBtn->wDisabledStrId;
      if (b.nCostKind == 2 && gpStatUnit != NULL)
        wStr = static_cast<WORD>(wStr + get_upgrade_level(gpStatUnit->bPlayer, pBtn->w0E));
      pszText = status_get_str(wStr);
    }
    if (pszText != NULL)
      lstrcpynA(b.szText, pszText, sizeof b.szText);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// The hooks.

void PortHudGameReset(void) {
  g_bStarted = false;
  g_bEnded = 0;
  g_nSelected = 0;
  memset(g_anCommands, 0, sizeof g_anCommands);
  memset(g_abSeat, 0, sizeof g_abSeat);
  g_bReplayLooked = false;
  g_nGameFrom = oc_dwReplayFrame;
  g_bAttacked = false;
  g_nAttackFrame = 0;
  g_nGroupLast = -1;
  g_nGridFirst = g_nGridPer = 0;
  g_pGridStat = NULL;
  g_nGridSub = -1;
  PortHudStatsReset();
}

bool PortHudGameStarted(void) { return g_bStarted; }

int PortHudGridPage(int nPer, int nUnits) {
  // a new selection or subgroup: the page that holds the unit the console reports on, the
  // subgroup's first; else the page shown, the last one if the selection is now shorter
  if (gpStatUnit != g_pGridStat || SelectionSubgroup() != g_nGridSub) {
    g_pGridStat = gpStatUnit;
    g_nGridSub = SelectionSubgroup();
    g_nGridFirst = 0;
    for (int i = 0; i < nUnits && i < SelectionMax(); ++i)
      if (StatUnits()[i] == gpStatUnit)
        g_nGridFirst = i;
  }
  g_nGridPer = nPer > 0 ? nPer : 0;
  if (nPer <= 0)
    return 0;
  const int nPages = nUnits > nPer ? (nUnits + nPer - 1) / nPer : 1;
  const int nPage = g_nGridFirst / nPer;
  return nPage < nPages ? nPage : nPages - 1;
}

void PortHudGridTurn(int nPage) {
  if (g_nGridPer <= 0 || nPage < 0 || nPage * g_nGridPer == g_nGridFirst - g_nGridFirst % g_nGridPer)
    return;
  g_nGridFirst = nPage * g_nGridPer;
  Emit(PORT_HUD_SELECTION_PAGE, NULL);
}

void PortHudArrive(void) {
  if (g_bStarted)
    return;
  g_bStarted = true;
  Emit(PORT_HUD_GAME_START, NULL);
  Emit(PORT_HUD_CONSOLE_IN, NULL);
}

void PortHudFrame(void) {
  PortHudArrive();
  NoteSeats();
  PortHudReplayWatch();
  // the selection the console shows, against the last frame's
  const int nCount = byte_6AB4C0 <= SelectionMax() ? byte_6AB4C0 : SelectionMax();
  if (nCount != g_nSelected || memcmp(g_apSelected, StatUnits(), nCount * sizeof(CUnit *)) != 0) {
    const bool bFirst = g_nSelected == 0 && nCount == 0;
    g_nSelected = nCount;
    memcpy(g_apSelected, StatUnits(), nCount * sizeof(CUnit *));
    if (!bFirst)
      Emit(PORT_HUD_SELECT_CHANGE, nCount != 0 ? StatUnits()[0] : NULL);
  } else if (SelectionSubgroup() != g_nSubgroupLast) {
    Emit(PORT_HUD_SELECTION_SUBGROUP, gpStatUnit);
  } else if (SelectionPageFirst() != g_nPageLast) {
    Emit(PORT_HUD_SELECTION_PAGE, NULL);
  }
  g_nSubgroupLast = SelectionSubgroup();
  g_nPageLast = SelectionPageFirst();
  // the local player's victory or defeat; a replay's viewer, or a skirmish's watcher, has none,
  // though the recorded player's state changes under them
  const int nPlayer = static_cast<int>(g_LocalNationID);
  const BYTE bState = nPlayer < 8 && gPlayback == 0 ? gbVictoryState[nPlayer] : 0;
  if (bState != g_bEnded) {
    g_bEnded = bState;
    if (bState == 2)
      Emit(PORT_HUD_GAME_DEFEAT, NULL);
    else if (bState == 3 || bState == 5)
      Emit(PORT_HUD_GAME_VICTORY, NULL);
  }
  PortHudStatsFrame();
}

void PortHudAttack(CUnit *pUnit) {
  if (pUnit->bPlayer != HudPlayer())
    return;
  if (!gPlayback) {
    // 1.17's own tests, read before gamesnd_under_attack makes them: the warning waits
    // 100 ticks of its 100 ms timer (game_timer), so ten seconds whatever the speed, is
    // for a unit off the screen, and is quiet for a while around one already warned of
    if (sgnHelpSndTimer != 0 || !pUnit->IsOffScreen() || pUnit->_unmapped_86[1] != 0)
      return;
  } else {
    // a replay has no warning of its own (1.17 leaves it out): ten seconds between them
    // here, and only for a unit off the screen, as in a game, counted in game frames
    const unsigned int nNow = oc_dwReplayFrame;
    if ((g_bAttacked && nNow - g_nAttackFrame < ATTACK_FRAMES) || !pUnit->IsOffScreen())
      return;
    g_bAttacked = true;
    g_nAttackFrame = nNow;
  }
  Emit(PORT_HUD_ALERT_ATTACK, pUnit);
}

void PortHudError(int nPlayer, int nStringId) {
  if (nPlayer != static_cast<int>(g_LocalNationID) || gPlayback)
    return;
  // banker.cpp's and CUnitBuild.cpp's: 0x34C to 0x34E not enough supply (by race), 0x34F
  // to 0x351 the supply at its cap, 0x352 minerals, 0x353 gas
  if (nStringId == 0x352)
    Emit(PORT_HUD_ALERT_MINERALS, NULL);
  else if (nStringId == 0x353)
    Emit(PORT_HUD_ALERT_GAS, NULL);
  else if (nStringId >= 0x34C && nStringId <= 0x351)
    Emit(PORT_HUD_ALERT_SUPPLY, NULL);
}

void PortHudDone(CUnit *pUnit, int nWhat) {
  if (pUnit->bPlayer != HudPlayer())
    return;
  Emit(nWhat == 0 ? PORT_HUD_RESEARCH_DONE : PORT_HUD_UNIT_DONE, pUnit);
}

void PortHudCommand(const unsigned char *pCmd) {
  // what the overlay counts as an action (port_overlay.cpp, IsAction): not the turn's
  // keep-alive, saving, loading, restarting, pausing and resuming, the sync check, chat or the
  // lobby's commands; nor a replay's own (0x55, 0x56), nor a selection's chunk after its first
  const unsigned int nCmd = pCmd[0];
  if (nCmd == NET_CMD_SELECT_CHUNK && (pCmd[2] & SELECT_CHUNK_NEXT) != 0)
    return;
  switch (nCmd) {
    case 0x05: case 0x06: case 0x07: case 0x08: case 0x10: case 0x11: case 0x37: case 0x5C:
    case 0x55: case 0x56:
      return;
  }
  if (nCmd >= 0x3C && nCmd <= 0x4F)
    return;
  const int nPlayer = static_cast<int>(g_ActiveNationID);
  if (nPlayer < 0 || nPlayer >= PORT_HUD_PLAYERS)
    return;
  g_aanCommandFrames[nPlayer][g_anCommands[nPlayer]++ % APM_KEPT] = oc_dwReplayFrame;
}

void PortHudReplayWatch(void) {
  if (!InGame() || !gPlayback)
    return;
  const bool bPaused = sgbReplayPaused_g != 0;
  const int nStep = ReplayStep();
  const unsigned int dwVision = gdwReplayVisionMask & 0xFF;
  if (!g_bReplayLooked) {
    g_bReplayLooked = true;
  } else {
    if (bPaused != g_bReplayPaused)
      Emit(bPaused ? PORT_HUD_REPLAY_PAUSE : PORT_HUD_REPLAY_RESUME, NULL);
    if (nStep != g_nReplayStep) {
      Emit(PORT_HUD_REPLAY_SPEED, NULL);
      g_aEvents[(g_nSerial - 1) % EVENTS_KEPT].nStep = nStep;
    }
    if (dwVision != g_dwReplayVision) {
      const unsigned int dwTurned = dwVision ^ g_dwReplayVision;
      Emit(PORT_HUD_REPLAY_VISION, NULL);
      int nPlayer = -1;
      for (int s = 0; s < 8; ++s)
        if (dwTurned == (1u << s))
          nPlayer = s;
      g_aEvents[(g_nSerial - 1) % EVENTS_KEPT].nPlayer = nPlayer;
    }
  }
  g_bReplayPaused = bPaused;
  g_nReplayStep = nStep;
  g_dwReplayVision = dwVision;
}

void PortHudGroup(int nGroup, bool bSet) {
  // the ten hotkey groups only; 10 to 17 are 1.17's own recent selections
  if (nGroup < 0 || nGroup > 9 || g_ActiveNationID != g_LocalNationID)
    return;
  if (!bSet)
    g_nGroupLast = nGroup;
  Emit(bSet ? PORT_HUD_GROUP_SET : PORT_HUD_GROUP_RECALL, NULL, nGroup);
}

// ---------------------------------------------------------------------------
// The values.

void PortHudGet(PortHudState *s) {
  memset(s, 0, sizeof *s);
  s->nGroupLast = -1;
  if (!InGame())
    return;
  s->bInGame = true;
  s->bReplay = gPlayback != 0;
  s->bMultiplayer = gbMultiPlayerMode != 0 && get_num_active_players() > 1;
  // a game watched as it is played, which Hyperion plays as a replay; 1.17 has no observer seat
  // (IsTVBGame is Top vs Bottom)
  s->bObserver = PortLanWatchLive() || PortLabWatchLive() || PortWatchLive();
  s->nFrame = oc_dwReplayFrame;
  s->nSeconds = static_cast<int>(g_dwElapsedTime);
  const int nPlayer = HudPlayer();
  s->nPlayer = nPlayer;
  s->nRace = nPlayer == static_cast<int>(g_LocalNationID) ? byte_5264C2 : nPlayer < 8 ? gPlayerData[nPlayer].nRace : 1;
  if (s->nRace > 2)
    s->nRace = 1;
  const BYTE *pc = sgGamePalette_g + gbMinimapColor_g[nPlayer] * 4;
  s->nColour = static_cast<unsigned int>(pc[0]) << 16 | static_cast<unsigned int>(pc[1]) << 8 | pc[2];
  s->nMinerals = gPlayerMinerals[nPlayer];
  s->nGas = gPlayerGas[nPlayer];
  // as statres_draw_supplies shows them: 1.17 counts half supplies
  for (int r = 0; r < 3; ++r) {
    s->anSupplyUsed[r] = (count_get_supplies_used(static_cast<BYTE>(r), static_cast<BYTE>(nPlayer)) + 1) / 2;
    s->anSupplyMax[r] = (count_get_supplies_available(static_cast<BYTE>(r), static_cast<BYTE>(nPlayer)) + 1) / 2;
  }
  s->nSupplyUsed = s->anSupplyUsed[s->nRace];
  s->nSupplyMax = s->anSupplyMax[s->nRace];
  s->nApm = Apm(nPlayer, s->nFrame);

  // the selection, of 12 in 1.17 and 255 in Hyperion's (selectlimits.h): its count, the page
  // of 12 the console shows and its types, and the types over the whole selection
  s->nSelected = byte_6AB4C0 <= SelectionMax() ? byte_6AB4C0 : SelectionMax();
  const int nFirst = SelectionPageFirst();
  s->nPages = s->nSelected > SELECTION_PAGE ? (s->nSelected + SELECTION_PAGE - 1) / SELECTION_PAGE : 1;
  s->nPage = nFirst / SELECTION_PAGE;
  // a HUD's grid shows pages of its own size (PortHudGridPage)
  if (g_nGridPer > 0) {
    s->nPages = s->nSelected > g_nGridPer ? (s->nSelected + g_nGridPer - 1) / g_nGridPer : 1;
    s->nPage = g_nGridFirst / g_nGridPer < s->nPages ? g_nGridFirst / g_nGridPer : s->nPages - 1;
  }
  for (int i = 0; i < SELECTION_PAGE; ++i) {
    CUnit *p = nFirst + i < SelectionMax() ? StatUnits()[nFirst + i] : NULL;
    s->anSelectedTypes[i] = p != NULL ? p->wType : -1;
  }
  for (int i = 0; i < s->nSelected; ++i) {
    CUnit *p = StatUnits()[i];
    bool bSeen = p == NULL;
    for (int k = 0; k < i && !bSeen; ++k)
      bSeen = StatUnits()[k] != NULL && StatUnits()[k]->wType == p->wType;
    if (!bSeen)
      ++s->nSubgroups;
  }
  s->nSubgroup = SelectionSubgroup();
  CUnit *u = gpStatUnit;
  if (s->nSelected != 0 && u != NULL) {
    // as statdraw_draw_hp, statdraw_draw_mana and statdraw_draw_kills show them
    const WORD wType = u->wType;
    const bool bSees = gPlayback || u->bPlayer == g_LocalNationID;
    s->bUnit = true;
    s->nUnitType = wType;
    s->nUnitOrder = u->bOrderId;
    const char *pszName = u->GetName();
    if (pszName != NULL)
      lstrcpynA(s->szUnitName, pszName, sizeof s->szUnitName);
    s->nHp = (u->lHitPoints + 0xFF) >> 8;
    s->nHpMax = u->GetLife();
    s->bShields = gbUnitHasShields[wType] != 0;
    if (s->bShields) {
      s->nShields = static_cast<long>(u->lShields) >> 8;
      s->nShieldsMax = HandicapShields(guwUnitEnergy[u->GetTypeEx()], u->bPlayer);  // at its handicap
    }
    s->bEnergy = (gulUnitTypeFlags[wType] & 0x200000) != 0 && (u->dwFlags & 0x40000000) == 0 && bSees;
    if (s->bEnergy) {
      s->nEnergy = u->wEnergy >> 8;
      s->nEnergyMax = u->GetMaxEnergyReal();
    }
    s->bKills = wType != 0x2F && wType != 0x32 &&
                !((u->dwFlags & 0x40000000) && !gPlayback && u->bPlayer == g_LocalNationID) &&
                (u->CanAttack() || u->bKillCount != 0);
    s->nKills = u->bKillCount;
  }

  int anWorkers[PORT_HUD_PLAYERS] = {};
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
    if (p->bPlayer < PORT_HUD_PLAYERS && IsWorker(p->wType) && !p->IsDead())
      ++anWorkers[p->bPlayer];
    if (p->bPlayer != nPlayer)
      continue;
    if (IsWorker(p->wType) && (p->bOrderId == 0x02 || p->bOrderId == 0x03))
      ++s->nIdleWorkers;
    else if (IsArmy(p, nPlayer))
      ++s->nArmy;
  }
  if (g_LocalHumanID < 8)
    for (int g = 0; g < 10; ++g)
      for (int i = 0; i < SelectionMax(); ++i) {
        const DWORD dwId = GroupRow(g_LocalHumanID, g)[i];
        if (dwId == 0)
          break;
        CUnit *p = CUnit::FromIdAlive(dwId);
        if (p != NULL && !p->IsDead())
          ++s->anGroups[g];
      }
  s->nGroupLast = g_nGroupLast;
  s->bTerrain = gbMinimapMtxMode_g == 0;  // minimap_btn_command flips it; 0 at the start

  // the buttons as 1.17's console shows them: the minimap's as minimap_btn_activate and its
  // timer leave them, the menu always; Hyperion's two in a game the player plays
  s->abButton[PORT_HUD_BTN_MENU] = true;
  s->abButton[PORT_HUD_BTN_TERRAIN] = Shown(MinimapButton(MINIMAP_TERRAIN));
  s->abButton[PORT_HUD_BTN_DIPLOMACY] = Shown(MinimapButton(MINIMAP_DIPLOMACY));
  s->abButton[PORT_HUD_BTN_MESSAGING] = Shown(MinimapButton(MINIMAP_MESSAGING));
  s->abButton[PORT_HUD_BTN_IDLE] = !s->bReplay;
  // the army button is Hyperion's: a game by classic rules keeps 1.17's selections and has none
  s->abButton[PORT_HUD_BTN_ARMY] = !s->bReplay && HxRules();
  ReadCard(s);

  // a replay's buttons as its card shows and greys them (bf_replay_play and the rest: 1 shown, -1
  // greyed, 0 not there), and Hyperion's camera, Replay TV in a replay and Follow in any game
  const int anTest[3] = {bf_replay_play(0), bf_replay_slowdown(0), bf_replay_speedup(0)};
  for (int i = 0; i < 3; ++i) {
    s->abButton[PORT_HUD_BTN_PLAY + i] = anTest[i] != 0;
    s->abButtonOff[PORT_HUD_BTN_PLAY + i] = anTest[i] < 0;
  }
  s->abButton[PORT_HUD_BTN_TV] = s->bReplay;
  s->abButton[PORT_HUD_BTN_FOLLOW] = true;
  s->bTv = PortTvOn() != 0;
  s->bFollow = PortFollowOn() != 0;
  s->abButtonLit[PORT_HUD_BTN_TV] = s->bTv;
  s->abButtonLit[PORT_HUD_BTN_FOLLOW] = s->bFollow;
  if (s->bReplay) {
    // as statdraw_replay_progress shows them: the speed's name, the time played, the share
    s->bPaused = sgbReplayPaused_g != 0;
    s->nSpeedStep = ReplayStep();
    const char *pszSpeed = gOptions <= 6 ? glues_get_str(static_cast<int>(gOptions) + 0x55) : NULL;
    snprintf(s->szSpeed, sizeof s->szSpeed, "%s", pszSpeed != NULL ? pszSpeed : "");
    if (replay_dlg_get_mult() > 1)
      snprintf(s->szSpeed + strlen(s->szSpeed), sizeof s->szSpeed - strlen(s->szSpeed), " \xC3\x97%d", replay_dlg_get_mult());
    // where the replay is, or while it is moved where it goes, so a dragged handle follows the
    // pointer
    const unsigned int nFrame = PortReplaySeekTarget();
    const unsigned int nTotal = static_cast<unsigned int>(replay_get_total_frames());
    const int nInitial = replay_get_initial_game_speed();
    const unsigned int nTicks = nInitial >= 0 && nInitial < 10 ? sgReplayTicks_g[nInitial] : 0;
    s->nReplaySeconds = static_cast<int>(nTicks * nFrame / 1000u);
    s->nReplayLength = static_cast<int>(nTicks * nTotal / 1000u);
    s->fProgress = nTotal != 0 ? (nFrame < nTotal ? static_cast<double>(nFrame) / nTotal : 1.0) : 0.0;
    s->bSeeking = g_bSeekDrag;
    // a game watched as it is played has no length yet: replay.progress is 0 and replay.length
    // empty (the live contract), and a layout reads replay.live
    s->bLive = s->bObserver;
    if (s->bLive) {
      s->fProgress = 0.0;
      s->nReplayLength = -1;
    }
  }

  // the players' board: every seat of the game, in slot order
  NoteSeats();
  for (int slot = 0; slot < PORT_HUD_PLAYERS && s->nPlayers < PORT_HUD_PLAYERS; ++slot) {
    if (!g_abSeat[slot])
      continue;
    PortHudPlayer &p = s->aPlayers[s->nPlayers++];
    const PlayerData &d = gPlayerData[slot];
    p.nSlot = slot;
    p.nRace = d.nRace <= 2 ? d.nRace : 1;
    p.nTeam = d.nTeam;
    const BYTE *pcp = sgGamePalette_g + gbMinimapColor_g[slot] * 4;
    p.nColour = static_cast<unsigned int>(pcp[0]) << 16 | static_cast<unsigned int>(pcp[1]) << 8 | pcp[2];
    char szName[sizeof d.szName + 1];
    memcpy(szName, d.szName, sizeof d.szName);
    szName[sizeof d.szName] = '\0';
    lstrcpynA(p.szName, MapTextUtf8(szName).c_str(), sizeof p.szName);
    // a computer's level, when its game chose them (Settings' or a skirmish's AI levels); in a
    // watched skirmish every seat's, a computer out of the game too, whose type 1.17 changes
    if ((d.nType == 1 && ((g_hxRuleOptions & HX_OPT_AI_LEVELS) != 0 || s->bObserver)) || PortWatchLive())
      lstrcpynA(p.szLevel, PortText(AiLevelName(AiProfileOf(slot).bLevel)), sizeof p.szLevel);
    p.nMinerals = gPlayerMinerals[slot];
    p.nGas = gPlayerGas[slot];
    p.nSupplyUsed = (count_get_supplies_used(static_cast<BYTE>(p.nRace), static_cast<BYTE>(slot)) + 1) / 2;
    p.nSupplyMax = (count_get_supplies_available(static_cast<BYTE>(p.nRace), static_cast<BYTE>(slot)) + 1) / 2;
    p.nWorkers = anWorkers[slot];
    p.nApm = Apm(slot, s->nFrame);
    p.bEye = s->bReplay && slot < 8;
    p.bSeen = !p.bEye || (gdwReplayVisionMask & (1u << slot)) != 0;
  }
  // the teams, as the design center counts them: a player without a team is one alone
  bool abTeam[256] = {};
  for (int i = 0; i < s->nPlayers; ++i) {
    const int nTeam = s->aPlayers[i].nTeam;
    if (nTeam <= 0 || nTeam > 255 || !abTeam[nTeam])
      ++s->nTeams;
    if (nTeam > 0 && nTeam <= 255)
      abTeam[nTeam] = true;
  }
  // the players' stats: their army, income and losses, and whether they show
  PortHudStatsFigures(s);
}

unsigned int PortHudSerial(void) { return g_nSerial; }

void PortHudStatsEvent(bool bShow, int nView) {
  Emit(bShow ? PORT_HUD_STATS_SHOW : PORT_HUD_STATS_HIDE, NULL);
  g_aEvents[(g_nSerial - 1) % EVENTS_KEPT].nView = nView;
}

void PortHudSeekDrag(bool bOn) { g_bSeekDrag = bOn; }

void PortHudSeekEvent(void) {
  if (InGame() && gPlayback)
    Emit(PORT_HUD_REPLAY_SEEK, NULL);
}

void PortHudRewound(void) {
  // the players' commands past the frame, for their APM
  const unsigned int nNow = oc_dwReplayFrame;
  for (int p = 0; p < PORT_HUD_PLAYERS; ++p)
    while (g_anCommands[p] > 0 && g_aanCommandFrames[p][(g_anCommands[p] - 1) % APM_KEPT] > nNow)
      --g_anCommands[p];
}

int PortHudEventsSince(unsigned int nSerial, PortHudEvent *pEvents, int nMax) {
  const unsigned int nFirst = g_nSerial > EVENTS_KEPT ? g_nSerial - EVENTS_KEPT : 0;
  int n = 0;
  for (unsigned int k = (nSerial > nFirst ? nSerial : nFirst); k < g_nSerial && n < nMax; ++k)
    pEvents[n++] = g_aEvents[k % EVENTS_KEPT];
  return n;
}

const char *PortHudEventName(int nKind) {
  static const char *const s_apsz[PORT_HUD_EVENT_KINDS] = {
      "game.start",    "game.victory",  "game.defeat",  "select.change", "alert.attack",
      "alert.minerals", "alert.gas",    "alert.supply", "research.done", "unit.done",
      "group.recall",  "group.set",     "console.in",   "console.out",   "selection.page",
      "selection.subgroup", "replay.pause", "replay.resume", "replay.speed", "replay.vision",
      "stats.show",    "stats.hide",    "replay.seek"};
  return nKind >= 0 && nKind < PORT_HUD_EVENT_KINDS ? s_apsz[nKind] : "";
}

const char *PortHudButtonName(int nKind) {
  static const char *const s_apsz[PORT_HUD_BTN_KINDS] = {"menu", "terrain", "diplomacy", "messaging", "idle",
                                                         "army", "play",    "slower",    "faster",    "tv",
                                                         "follow"};
  return nKind >= 0 && nKind < PORT_HUD_BTN_KINDS ? s_apsz[nKind] : "";
}

int PortHudButtonByName(const char *pszName) {
  for (int k = 0; k < PORT_HUD_BTN_KINDS; ++k)
    if (lstrcmpiA(pszName, PortHudButtonName(k)) == 0)
      return k;
  return -1;
}

// ---------------------------------------------------------------------------
// The presses.

bool PortHudPress(int nPlace, const char **ppszWhy) {
  if (!InGame() || sgpStatCmdDlg == NULL) {
    *ppszWhy = "no game";
    return false;
  }
  if (nPlace < 0 || nPlace > 8) {
    *ppszWhy = "a place is 0 to 8";
    return false;
  }
  ctrl_t *c = ctrl_find_ndx(sgpStatCmdDlg, static_cast<short>(nPlace + 1));
  if (c == NULL || (c->dwFlags & 8) == 0 || ctrl_p26(c) == NULL) {
    *ppszWhy = "no button there";
    return false;
  }
  if ((c->dwFlags & 2) != 0) {
    *ppszWhy = "the button is grey";
    return false;
  }
  // as statcmd_dlg_char_evn sends a hotkey's press to its button
  ctrl_send_msg(c, 3, 0);
  return true;
}

namespace {

// The idle worker button (Hyperion's): the player's idle worker after the one it chose last, in
// the order of the game's list of units, selected as a click on it selects it, and the view
// taken to it. The one chosen last is only compared, never read: it may be gone.
bool SelectIdle(const char **ppszWhy) {
  static const CUnit *s_pLast;
  const int nPlayer = static_cast<int>(g_LocalNationID);
  CUnit *pFirst = NULL, *pNext = NULL;
  bool bPast = false;
  for (CUnit *p = CUnit::GetUsedList(); p != NULL && pNext == NULL; p = p->pNext) {
    if (p == s_pLast)
      bPast = true;
    if (p->bPlayer != nPlayer || !IsWorker(p->wType) || (p->bOrderId != 0x02 && p->bOrderId != 0x03))
      continue;
    if (pFirst == NULL)
      pFirst = p;
    if (bPast && p != s_pLast)
      pNext = p;
  }
  CUnit *pUnit = pNext != NULL ? pNext : pFirst;
  if (pUnit == NULL) {
    *ppszWhy = "no idle worker";
    return false;
  }
  s_pLast = pUnit;
  select_units(1, &pUnit, 1, 1);
  PortViewCameraCentre(pUnit->wPositionX, pUnit->wPositionY);
  return true;
}

// The army button (Hyperion's): the player's army, as many as a selection holds, selected as a
// drag box over them selects them.
bool SelectArmy(const char **ppszWhy) {
  const int nPlayer = static_cast<int>(g_LocalNationID);
  std::vector<CUnit *> army;
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext)
    if (IsArmy(p, nPlayer))
      army.push_back(p);
  if (army.empty()) {
    *ppszWhy = "no army";
    return false;
  }
  army.push_back(NULL);
  CUnit *apUnits[USEL_SELECT_CAPACITY] = {};
  const int nCount = sub_4BC5B0(army.data(), apUnits, NULL);
  if (nCount == 0) {
    *ppszWhy = "no army to select";
    return false;
  }
  select_units(nCount, apUnits, 1, 1);
  return true;
}

}  // namespace

bool PortHudPressButton(int nKind, const char **ppszWhy) {
  if (!InGame()) {
    *ppszWhy = "no game";
    return false;
  }
  PortHudState s;
  PortHudGet(&s);
  if (nKind < 0 || nKind >= PORT_HUD_BTN_KINDS || !s.abButton[nKind]) {
    *ppszWhy = "the button is not shown in this game";
    return false;
  }
  switch (nKind) {
    case PORT_HUD_BTN_MENU:
      // as statF10_btn_ctrl_fcn opens it
      do_gamemenu_dlg();
      return true;
    case PORT_HUD_BTN_TERRAIN:
      minimap_btn_command(MinimapButton(MINIMAP_TERRAIN));
      return true;
    case PORT_HUD_BTN_DIPLOMACY:
      minimap_btn_command(MinimapButton(MINIMAP_DIPLOMACY));
      return true;
    case PORT_HUD_BTN_MESSAGING:
      minimap_btn_command(MinimapButton(MINIMAP_MESSAGING));
      return true;
    case PORT_HUD_BTN_IDLE:
      return SelectIdle(ppszWhy);
    case PORT_HUD_BTN_ARMY:
      return SelectArmy(ppszWhy);
    case PORT_HUD_BTN_PLAY:
    case PORT_HUD_BTN_SLOWER:
    case PORT_HUD_BTN_FASTER:
      if (s.abButtonOff[nKind]) {
        *ppszWhy = "the button is grey";
        return false;
      }
      // the orders the replay card's buttons and their keys (P, D, U) give
      if (nKind == PORT_HUD_BTN_PLAY)
        order_replay_play();
      else if (nKind == PORT_HUD_BTN_SLOWER)
        order_replay_slowdown();
      else
        order_replay_speedup();
      return true;
    case PORT_HUD_BTN_TV:
      // as F12 turns it (event_Win.cpp)
      PortTvToggle();
      return true;
    case PORT_HUD_BTN_FOLLOW:
      // as F11 turns it: on, the selected unit; off, whichever
      if (!s.bFollow && gpStatUnit == NULL) {
        *ppszWhy = "no unit selected";
        return false;
      }
      PortFollowToggle();
      return true;
  }
  *ppszWhy = "not built yet";
  return false;
}

bool PortHudPressEye(int nSlot, const char **ppszWhy) {
  if (!InGame() || !gPlayback) {
    *ppszWhy = "not a replay";
    return false;
  }
  if (nSlot < 0 || nSlot >= 8 || !g_abSeat[nSlot]) {
    *ppszWhy = "no player of the lobby's eight there";
    return false;
  }
  PortReplayVisionToggle(nSlot);
  return true;
}

bool PortHudPressGroup(int nGroup, const char **ppszWhy, int nHow) {
  if (!InGame() || gPlayback) {
    *ppszWhy = "no game the player plays";
    return false;
  }
  if (nGroup < 0 || nGroup > 9) {
    *ppszWhy = "a group is 0 to 9";
    return false;
  }
  // as the group's key recalls it (game_keychar_hook), and as Ctrl and Shift with it make it of
  // the selection or add the selection to it (dialog_accel)
  if (nHow == PORT_HUD_USE_SET || nHow == PORT_HUD_USE_ADD)
    send_net_cmd_GroupUnits(nHow == PORT_HUD_USE_SET ? 0 : 2, static_cast<BYTE>(nGroup), NULL, 0);
  else
    game_key_down_num(static_cast<BYTE>(nGroup));
  return true;
}

// ---------------------------------------------------------------------------
// The live channel's answers (port_hudlive.h).

namespace {

std::string Int(int n) { return std::to_string(n); }
std::string Bool(bool b) { return b ? "true" : "false"; }

}  // namespace

std::string PortHudJson(void) {
  PortHudState s;
  PortHudGet(&s);
  static const char *const s_apszRace[3] = {"zerg", "terran", "protoss"};
  char szColour[8];
  snprintf(szColour, sizeof szColour, "#%06x", s.nColour & 0xFFFFFFu);
  std::string j = "\"inGame\":" + Bool(s.bInGame) + ",\"replay\":" + Bool(s.bReplay) + ",\"frame\":" +
                  std::to_string(s.nFrame) + ",\"serial\":" + std::to_string(g_nSerial);
  if (!s.bInGame)
    return j;
  // the format's names (docs/hud-format.md, "Bindings"); time is the overlay's, not a skin's
  j += ",\"values\":{\"minerals\":" + Int(s.nMinerals) + ",\"gas\":" + Int(s.nGas) +
       ",\"supply.used\":" + Int(s.nSupplyUsed) + ",\"supply.max\":" + Int(s.nSupplyMax) +
       ",\"apm\":" + Int(s.nApm) + ",\"selection.count\":" + Int(s.nSelected) +
       ",\"selection.page\":" + Int(s.nPage + 1) + ",\"selection.pages\":" + Int(s.nPages) +
       ",\"selection.subgroups\":" + Int(s.nSubgroups) + ",\"unit.name\":" + PortJsonQuote(s.szUnitName) +
       ",\"unit.hp\":" + Int(s.nHp) + ",\"unit.hpMax\":" + Int(s.nHpMax) + ",\"unit.shields\":" +
       Int(s.nShields) + ",\"unit.shieldsMax\":" + Int(s.nShieldsMax) + ",\"unit.energy\":" + Int(s.nEnergy) +
       ",\"unit.energyMax\":" + Int(s.nEnergyMax) + ",\"unit.kills\":" + Int(s.nKills) + ",\"idle.count\":" +
       Int(s.nIdleWorkers) + ",\"army.count\":" + Int(s.nArmy) + ",\"game.multiplayer\":" +
       Bool(s.bMultiplayer) + ",\"game.observer\":" + Bool(s.bObserver) + ",\"game.replay\":" +
       Bool(s.bReplay) + ",\"minimap.terrain\":" + Bool(s.bTerrain) + ",\"race\":" +
       PortJsonQuote(s_apszRace[s.nRace]) + ",\"player.colour\":" + PortJsonQuote(szColour) +
       ",\"group.last\":" + Int(s.nGroupLast) + ",\"players.count\":" + Int(s.nPlayers);
  if (s.bReplay) {
    char szClock[2][16];
    snprintf(szClock[0], sizeof szClock[0], "%d:%02d", s.nReplaySeconds / 60, s.nReplaySeconds % 60);
    if (s.nReplayLength >= 0)
      snprintf(szClock[1], sizeof szClock[1], "%d:%02d", s.nReplayLength / 60, s.nReplayLength % 60);
    else
      szClock[1][0] = '\0';  // live: no length
    char szProgress[16];
    snprintf(szProgress, sizeof szProgress, "%.4f", s.fProgress);
    j += ",\"replay.paused\":" + Bool(s.bPaused) + ",\"replay.speed\":" + PortJsonQuote(s.szSpeed) +
         ",\"replay.step\":" + Int(s.nSpeedStep) + ",\"replay.progress\":" + szProgress +
         ",\"replay.elapsed\":" + PortJsonQuote(szClock[0]) + ",\"replay.length\":" + PortJsonQuote(szClock[1]) +
         ",\"replay.tv\":" + Bool(s.bTv) + ",\"replay.follow\":" + Bool(s.bFollow) + ",\"replay.live\":" + Bool(s.bLive) + ",\"replay.seeking\":" + Bool(s.bSeeking);
  }
  j += ",\"stats.side\":" + Bool(s.bStatsSide) + ",\"stats.popup\":" + Bool(s.bStatsPopup) +
       ",\"teams.count\":" + Int(s.nTeams);
  for (int g = 0; g < 10; ++g)
    j += ",\"group." + Int(g) + ".count\":" + Int(s.anGroups[g]);
  j += "}";
  j += ",\"player\":" + Int(s.nPlayer) + ",\"unit\":{\"shown\":" + Bool(s.bUnit) + ",\"type\":" +
       Int(s.bUnit ? s.nUnitType : -1) + ",\"shields\":" + Bool(s.bShields) + ",\"energy\":" + Bool(s.bEnergy) +
       ",\"kills\":" + Bool(s.bKills) + ",\"order\":" + Int(s.bUnit ? s.nUnitOrder : -1) + "}";
  j += ",\"supplyByRace\":[";
  for (int r = 0; r < 3; ++r)
    j += std::string(r ? "," : "") + "[" + Int(s.anSupplyUsed[r]) + "," + Int(s.anSupplyMax[r]) + "]";
  j += "],\"selection\":[";
  for (int i = 0; i < s.nSelected; ++i)
    j += std::string(i ? "," : "") + Int(s.anSelectedTypes[i]);
  j += "],\"buttons\":{";
  for (int k = 0; k < PORT_HUD_BTN_KINDS; ++k)
    j += std::string(k ? "," : "") + PortJsonQuote(PortHudButtonName(k)) + ":" + Bool(s.abButton[k]);
  j += "},\"players\":[";
  for (int i = 0; i < s.nPlayers; ++i) {
    const PortHudPlayer &p = s.aPlayers[i];
    char szPc[8];
    snprintf(szPc, sizeof szPc, "#%06x", p.nColour & 0xFFFFFFu);
    j += std::string(i ? "," : "") + "{\"slot\":" + Int(p.nSlot) + ",\"name\":" + PortJsonQuote(p.szName) +
         ",\"race\":" + PortJsonQuote(s_apszRace[p.nRace]) + ",\"team\":" + Int(p.nTeam) + ",\"colour\":" +
         PortJsonQuote(szPc) + ",\"minerals\":" + Int(p.nMinerals) + ",\"gas\":" + Int(p.nGas) +
         ",\"supply\":[" + Int(p.nSupplyUsed) + "," + Int(p.nSupplyMax) + "],\"workers\":" + Int(p.nWorkers) +
         ",\"apm\":" + Int(p.nApm) + ",\"seen\":" + Bool(p.bSeen) + ",\"eye\":" + Bool(p.bEye) + "}";
  }
  j += "],\"card\":[";
  for (int i = 0; i < 9; ++i) {
    const PortHudButton &b = s.aCard[i];
    j += i ? "," : "";
    if (!b.bShown) {
      j += "null";
      continue;
    }
    const char szKey[2] = {b.cHotkey, 0};
    j += "{\"icon\":" + Int(b.nIcon) + ",\"enabled\":" + Bool(b.bEnabled) + ",\"hotkey\":" +
         PortJsonQuote(szKey) + ",\"costKind\":" + Int(b.nCostKind) + ",\"text\":" + PortJsonQuote(b.szText) +
         "}";
  }
  return j + "]";
}

std::string PortHudEventsJson(unsigned int nSerial) {
  static PortHudEvent s_a[EVENTS_KEPT];
  const int n = PortHudEventsSince(nSerial, s_a, EVENTS_KEPT);
  std::string j = "\"serial\":" + std::to_string(g_nSerial) + ",\"events\":[";
  for (int i = 0; i < n; ++i) {
    const PortHudEvent &e = s_a[i];
    j += std::string(i ? "," : "") + "{\"serial\":" + std::to_string(e.nSerial) + ",\"event\":" +
         PortJsonQuote(PortHudEventName(e.nKind)) + ",\"frame\":" + std::to_string(e.nFrame);
    if (e.nUnitType >= 0)
      j += ",\"unit\":" + Int(e.nUnitType) + ",\"x\":" + Int(e.x) + ",\"y\":" + Int(e.y) + ",\"building\":" +
           Bool(e.bBuilding);
    if (e.nGroup >= 0)
      j += ",\"group\":" + Int(e.nGroup);
    if (e.nKind == PORT_HUD_REPLAY_VISION)
      j += ",\"player\":" + Int(e.nPlayer);
    if (e.nStep >= 0)
      j += ",\"step\":" + Int(e.nStep);
    j += "}";
  }
  return j + "]";
}
