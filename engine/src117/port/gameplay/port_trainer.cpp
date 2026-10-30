// port_trainer.cpp -- the trainer (docs/modules/gameplay.md): a session of lines
// over one game, each the
// commands its game was given frame by frame, as the recorder takes them (net_record.cpp).
//
// The game is always at the playhead. Going back puts back the line's latest snapshot at or
// before the frame (port_snap.cpp), drops the commands then in transit, and plays the line's
// commands on to it with nothing drawn (a seek). A line played before its end is watched until
// the player's first order takes over; at its end the game is live. A branch copies its
// parent's commands up to the split and shares its snapshots to there. HYPERION_TRAINERTEST=
// <period>,<back> goes back <back> frames at each period'th frame first reached, and every
// frame played again must hash as before (trainertest.txt). A session is saved as its game
// ends as a .hxtrain (JSON) in Trainer\Sessions; Leave asks which lines go, other ends save all.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "../loop/port_clock.h"  // PortGameSpeedTimesSet
#include "port_launch.h"  // PortLaunchTrainerReplay, PortLaunchTrainer
#include "port_session.h"  // SessionClear
#include "../platform/port_paths.h"  // PortLogFile, PortDataFolder, PortCommandOptions
#include "../render/port_present.h"  // PortPresentRedraw
#include "../platform/port_log.h"  // PortLogger
#include "../platform/port_util.h"  // PortReadFile
#include "../platform/port_json.h"
#include "../replays/port_snap.h"
#include "port_trainer.h"
#include "../lan/port_stormnet.h"  // PortNetKeepSyncs
#include "../../h/hxrules.h"      // g_hxRulesGame, RulesChooseNext
#include "../../h/hxsim.h"        // HX_SIM_REVISION
#include "../../h/CNetRecord.h"   // a replay's commands, as the recorder holds them
#include "../../h/CUnit.h"        // CUnit::CenterView
#include "../../h/game.h"         // game_slots_init
#include "../../h/net_record.h"   // net_record_stop_playback
#include "../../h/placebox.h"     // PreparePlayerSlots
#include "../../h/playervision.h" // ViewHxReset
#include "../../h/port_hxreplay.h"  // ReplayHxGameStart
#include "../../h/replay.h"       // replay_get_total_frames
#include "../../h/status.h"       // status_free, status_init
#include "../../h/oc_data.h"
#include "../../h/net_cmds.h"     // IsGamePaused, PlayerData
#include "../../h/net_mgr.h"      // net_process_game_turn
#include "../../h/net_rand.h"     // set_rand_enabled, net_get_fixed_seed
#include "../../h/TrigAct.h"      // pause_game, unpause_game
#include "../../h/gamemap.h"      // gamemap_force_redraw
#include "../../h/cursor.h"       // cursor_forget_target
#include "../../h/opt_dlgs.h"     // PortQuitToMenu
#include "../../h/typelimits.h"   // UnitTypeValid
#include "../../h/playertables.h"
#include "../../h/port_synchash.h"
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

extern BYTE gbInPacketProcess;         // 0x672984 (net_mgr.cpp)

int TrainerSecond::*const kTrainerSecondFields[] = {
    &TrainerSecond::nMinerals,   &TrainerSecond::nGas,        &TrainerSecond::nMined,
    &TrainerSecond::nSupplyUsed, &TrainerSecond::nSupplyMax,  &TrainerSecond::nWorkers,
    &TrainerSecond::nArmy,       &TrainerSecond::nMade,       &TrainerSecond::nLost,
    &TrainerSecond::nKilled,     &TrainerSecond::nActions,    &TrainerSecond::nLostValue,
    &TrainerSecond::nKilledValue, &TrainerSecond::nTheirArmy, &TrainerSecond::nBlocked,
    &TrainerSecond::nBlocks,     &TrainerSecond::nEffective,  &TrainerSecond::nWorkersLost,
    &TrainerSecond::nNearBase,   &TrainerSecond::nHallsLost,  &TrainerSecond::nEnemiesOut,
    &TrainerSecond::nFighting};
const int kTrainerSecondCount = static_cast<int>(sizeof kTrainerSecondFields / sizeof kTrainerSecondFields[0]);

namespace {

std::vector<TrainerLine> g_lines;
std::vector<TrainerPoint> g_points;
int g_nCurrent;          // the line the game is on
int g_nState = TRAINER_LIVE;
int g_nNextId = 1;
unsigned g_nOrder;
int g_nAttempts, g_nTimelines, g_nPointsMade;
bool g_bSession;         // a session runs this game
bool g_bFeeding;         // a line's command is being given (the recorder's is not the player's)
unsigned g_nFed = ~0u;   // the frame whose commands were given last
bool g_bFirstSnap;       // the snapshot at frame 0

// the set-up: the running session's, and the next game's while armed
TrainerSetup g_setup;
TrainerSetup g_armed;
bool g_bArmed;
bool g_bFromMenus;       // this session was set up on the entry screen
bool g_bOpening;         // a saved session, its lines read: paused at its start once frame 0 is kept
bool g_bTapeWanted;
bool g_bEndedFromMenus;  // the game of a session set up on the menus has ended
bool g_bCameBack;        // the same, for the trainer's entry
std::string g_made;      // when a saved session was made, kept when it is saved again
unsigned g_nDiffered, g_nChecked;

// a seek: to a frame of the current line, and what then
enum { AFTER_PAUSE, AFTER_LIVE, AFTER_WATCH };
unsigned g_nSeekTo;
int g_nSeekAfter;
unsigned g_nLastSnap, g_nLastFrames;  // the last seek's snapshot and the frames it played on

// the self-test
int g_nTest = -1;
unsigned g_nTestPeriod = 1200, g_nTestBack = 600;
unsigned g_nTestHighest;  // the furthest frame played on any line
struct TestHashes {
  std::vector<SyncHash> h;  // by frame, as first played on the line or the line it came from
  std::vector<char> have;
};
std::map<int, TestHashes> g_testHashes;  // by line
unsigned g_nTestRewinds, g_nTestReplayed, g_nTestBad, g_nTestFirstBad = ~0u, g_nTestFirstDomains;
unsigned g_nTestRetries, g_nTestTakeOvers, g_nTestBranches, g_nTestScrubs;
bool g_bTestTakeOver;     // the orders go on while this watch plays: the first takes over
bool g_bTestGoOn;         // paused by a scrub: watch on from there at the next pass

TrainerLine *Line(int nId)
{
  for (TrainerLine &l : g_lines)
    if (l.nId == nId)
      return &l;
  return NULL;
}

TrainerLine &Current(void)
{
  return *Line(g_nCurrent);
}

std::string Num(int n)
{
  char sz[16];
  snprintf(sz, sizeof sz, "%d", n);
  return sz;
}

// The first command of a line at or after a frame.
size_t CommandAt(const TrainerLine &l, unsigned nFrame)
{
  return std::lower_bound(l.commands.begin(), l.commands.end(), nFrame,
                          [](const TrainerCommand &c, unsigned f) { return c.nFrame < f; }) -
         l.commands.begin();
}

int TestOn(void);
bool SeekTo(int nLine, unsigned nTo, int nAfter);

bool Worker(unsigned t)
{
  return t == 7 || t == 41 || t == 64;  // an SCV, a drone, a probe
}

bool Hall(unsigned t)
{
  return t == 106 || t == 131 || t == 132 || t == 133 || t == 154;  // a command center, a hatchery, lair, hive, nexus
}

// An army unit: a unit that is not a worker or an overlord, nor a turret, larva or egg.
bool ArmyType(unsigned t)
{
  return t < UNIT_TYPES_117 && (gubUnitCountInfo[t] & 8) && !Worker(t) && t != 42 && t != 35 && t != 36 &&
         t != 59 && t != 97 && !(gulUnitTypeFlags[t] & 0x10);
}

// minerals and gas a unit of the type is worth; units.dat prices zerglings and scourge a pair
int Cost(unsigned t)
{
  const int n = guwUnitMineralsCost[t] + guwUnitGasesCost[t];
  return t == 37 || t == 47 ? n / 2 : n;
}

bool Enemy(int nSlot, int nOther)
{
  return nOther != nSlot && PlayerIsLobby(static_cast<unsigned>(nOther)) && !sgbAlliances_g[nSlot][nOther];
}

// The player's actions up to now, and those EAPM keeps (screp's rules: a selection changed again
// within 8 frames, and an order repeated within 10, are dropped).
void Actions(const TrainerLine &l, int *pnActions, int *pnEffective)
{
  int nActions = 0, nEffective = 0;
  const TrainerCommand *pPrev = NULL;
  bool bPrevSelect = false;
  const size_t nNow = CommandAt(l, oc_dwReplayFrame + 1);
  for (size_t i = 0; i < nNow; i++) {
    const TrainerCommand &c = l.commands[i];
    if (c.bStorm != g_LocalStormID || c.ab.empty() || c.ab[0] == 0x05 || c.ab[0] == 0x37)
      continue;
    nActions++;
    const unsigned char op = c.ab[0];
    const bool bSelect = op == 0x09 || op == 0x0A || op == 0x0B || op == 0x13;
    bool bEffective = true;
    if (pPrev != NULL && bSelect && bPrevSelect && c.nFrame - pPrev->nFrame <= 8)
      bEffective = false;
    else if (pPrev != NULL && !bSelect && pPrev->ab == c.ab && c.nFrame - pPrev->nFrame <= 10)
      bEffective = false;
    nEffective += bEffective;
    pPrev = &c;
    bPrevSelect = bSelect;
  }
  *pnActions = nActions;
  *pnEffective = nEffective;
}

// The player's figures now; prev, the second before on this line, for the running counts.
void Sample(TrainerSecond *pOut, const TrainerLine &l)
{
  TrainerSecond s = {};
  const TrainerSecond prev = l.seconds.empty() ? TrainerSecond{} : l.seconds.back();
  const int nSlot = static_cast<int>(g_LocalNationID);
  SyncSlot slot;
  SyncSlotRead(nSlot, &slot);
  s.nMinerals = slot.nMinerals;
  s.nGas = slot.nGas;
  s.nMined = slot.nMineralsGathered + slot.nGasGathered;
  // the race whose supply the player has most of: provided, used, at 18, 21 and 24
  int nBest = 0;
  for (int r = 1; r < 3; r++)
    if (slot.adwScores[18 + 3 * r] > slot.adwScores[18 + 3 * nBest])
      nBest = r;
  s.nSupplyUsed = static_cast<int>(slot.adwScores[19 + 3 * nBest] / 2);
  s.nSupplyMax = static_cast<int>((std::min)(slot.adwScores[18 + 3 * nBest], 400u) / 2);
  s.nMade = static_cast<int>(slot.adwScores[2]);
  s.nLost = static_cast<int>(slot.adwScores[3]);
  s.nKilled = static_cast<int>(slot.adwScores[4]);
  if (nSlot >= 0 && nSlot < PlayerSlots()) {
    for (unsigned t = 0; UnitTypeValid(t); t = UnitTypeNext(t)) {
      const int n = static_cast<int>(glCounts_g[1][t][nSlot]);
      if (n <= 0)
        continue;
      if (Worker(t))
        s.nWorkers += n;
      else if (t < UNIT_TYPES_117 && (gubUnitCountInfo[t] & 8) && t != 42)  // a unit, not an overlord
        s.nArmy += n * Cost(t);
    }
    for (unsigned t = 0; t < UNIT_TYPES_117; t++) {
      if (gulUnitTypeFlags[t] & 0x10)
        continue;
      s.nLostValue += static_cast<int>(glCounts_g[3][t][nSlot]) * Cost(t);
      s.nKilledValue += static_cast<int>(glCounts_g[2][t][nSlot]) * Cost(t);
      if (Worker(t))
        s.nWorkersLost += static_cast<int>(glCounts_g[3][t][nSlot]);
      if (Hall(t))
        s.nHallsLost += static_cast<int>(glCounts_g[3][t][nSlot]);
    }
    for (int o = 0; o < PlayerSlots(); o++) {
      if (!Enemy(nSlot, o))
        continue;
      if (gbVictoryState[o] != 0)
        s.nEnemiesOut++;
      for (unsigned t = 0; t < UNIT_TYPES_117; t++)
        if (ArmyType(t) && glCounts_g[1][t][o] != 0)
          s.nTheirArmy += static_cast<int>(glCounts_g[1][t][o]) * Cost(t);
    }
    // the player's army units within 6 tiles of an enemy's: the enemies' by cells of 8 tiles
    std::map<long long, std::vector<CUnit *>> cells;
    auto CellOf = [](int x, int y) { return (static_cast<long long>(y >> 8) << 32) | static_cast<unsigned>(x >> 8); };
    for (CUnit *u = CUnit::GetUsedList(); u != NULL; u = u->pNext)
      if (ArmyType(u->wType) && Enemy(nSlot, u->bPlayer))
        cells[CellOf(u->wPositionX, u->wPositionY)].push_back(u);
    if (!cells.empty())
      for (CUnit *u = CUnit::GetUsedList(); u != NULL; u = u->pNext) {
        if (u->bPlayer != nSlot || !ArmyType(u->wType))
          continue;
        bool bNear = false;
        for (int dy = -1; dy <= 1 && !bNear; dy++)
          for (int dx = -1; dx <= 1 && !bNear; dx++) {
            const auto it = cells.find(CellOf(u->wPositionX + dx * 256, u->wPositionY + dy * 256));
            if (it == cells.end())
              continue;
            for (CUnit *e : it->second) {
              const int ex = static_cast<int>(e->wPositionX) - u->wPositionX, ey = static_cast<int>(e->wPositionY) - u->wPositionY;
              if (ex * ex + ey * ey <= 192 * 192) {
                bNear = true;
                break;
              }
            }
          }
        s.nFighting += bNear;
      }
    // the enemies' army units within 8 tiles of any of the player's town halls
    std::vector<CUnit *> halls;
    for (CUnit *u = CUnit::GetUsedList(); u != NULL; u = u->pNext)
      if (u->bPlayer == nSlot && Hall(u->wType))
        halls.push_back(u);
    if (!halls.empty())
      for (CUnit *u = CUnit::GetUsedList(); u != NULL; u = u->pNext) {
        if (!ArmyType(u->wType) || !Enemy(nSlot, u->bPlayer))
          continue;
        for (CUnit *h : halls) {
          const int dx = static_cast<int>(u->wPositionX) - h->wPositionX, dy = static_cast<int>(u->wPositionY) - h->wPositionY;
          if (dx * dx + dy * dy <= 256 * 256) {
            s.nNearBase++;
            break;
          }
        }
      }
  }
  // the supply full: the cap reached, short of the most there is
  const bool bFull = s.nSupplyMax > 0 && s.nSupplyUsed >= s.nSupplyMax && s.nSupplyMax < 200;
  const bool bWasFull = prev.nSupplyMax > 0 && prev.nSupplyUsed >= prev.nSupplyMax && prev.nSupplyMax < 200;
  s.nBlocked = prev.nBlocked + (bFull ? 1 : 0);
  s.nBlocks = prev.nBlocks + (bFull && !bWasFull ? 1 : 0);
  // the player's up to now: a line watched, or a replay's, holds later ones too
  Actions(l, &s.nActions, &s.nEffective);
  *pOut = s;
}

// What the player has made of each type, existing or lost, as the playhead last saw it: a frame
// looked at for the first time on a line adds what grew to the line's build list.
std::vector<int> g_madeWas;
bool g_bMadeOk;

int MadeOf(unsigned t, int nSlot)
{
  // a siege tank sieged is the same tank
  if (t == 5)
    return static_cast<int>(glCounts_g[0][5][nSlot] + glCounts_g[3][5][nSlot] + glCounts_g[0][30][nSlot] +
                            glCounts_g[3][30][nSlot]);
  if (t == 30 || t == 35 || t == 36 || t == 59 || t == 97 || (gulUnitTypeFlags[t] & 0x10))
    return 0;
  return static_cast<int>(glCounts_g[0][t][nSlot] + glCounts_g[3][t][nSlot]);
}

void MadeRebase(void)
{
  const int nSlot = static_cast<int>(g_LocalNationID);
  g_madeWas.assign(UNIT_TYPES_117, 0);
  g_bMadeOk = nSlot >= 0 && nSlot < PlayerSlots();
  if (g_bMadeOk)
    for (unsigned t = 0; t < UNIT_TYPES_117; t++)
      g_madeWas[t] = MadeOf(t, nSlot);
}

int SupplyNow(int nSlot)
{
  DWORD dwBest = 0;
  for (int r = 0; r < 3; r++)
    dwBest = (std::max)(dwBest, static_cast<DWORD>(glGameCounts_g[19 + 3 * r][nSlot]));
  return static_cast<int>(dwBest / 2);
}

void ScanBuilt(TrainerLine &l, unsigned nFrame)
{
  const int nSlot = static_cast<int>(g_LocalNationID);
  if (!g_bMadeOk) {
    MadeRebase();
    return;
  }
  const bool bNew = nFrame > l.nBuiltTo;
  for (unsigned t = 0; t < UNIT_TYPES_117; t++) {
    const int n = MadeOf(t, nSlot);
    if (bNew)
      for (int k = g_madeWas[t]; k < n; k++)
        l.built.push_back({nFrame, static_cast<unsigned short>(t), static_cast<unsigned short>(SupplyNow(nSlot))});
    g_madeWas[t] = n;
  }
  if (bNew) {
    l.nBuiltTo = nFrame;
    // the game's end for the player, the first time a line reaches it
    const unsigned char bState = gbVictoryState[nSlot];
    if (l.nResult == TRAINER_RESULT_NONE && bState != 0) {
      l.nResult = bState == 3 || bState == 5 ? TRAINER_RESULT_WON : TRAINER_RESULT_LOST;
      l.marks.push_back({nFrame, TRAINER_MARK_END, l.nResult});
    }
  }
}

// The marks a second makes, the first time a line plays it: an attack wave at the player's base
// (3 or more of the enemies' army there, none a second before, and none marked for 30 seconds),
// the player's town hall lost, an enemy out of the game, a fight (the player's army by an
// enemy's, none a second before, and none marked for 20 seconds).
void AutoMarks(TrainerLine &l, const TrainerSecond &prev, const TrainerSecond &s, unsigned nFrame)
{
  if (s.nNearBase >= 3 && prev.nNearBase < 3) {
    bool bRecent = false;
    for (const TrainerMark &m : l.marks)
      if (m.nKind == TRAINER_MARK_WAVE && m.nFrame + 30 * TRAINER_SECOND > nFrame)
        bRecent = true;
    if (!bRecent)
      l.marks.push_back({nFrame, TRAINER_MARK_WAVE, s.nNearBase});
  }
  if (s.nHallsLost > prev.nHallsLost)
    l.marks.push_back({nFrame, TRAINER_MARK_FELL, static_cast<int>(g_LocalNationID)});
  // (the game's first seconds put the empty slots out)
  if (s.nEnemiesOut > prev.nEnemiesOut && nFrame > 10 * TRAINER_SECOND)
    l.marks.push_back({nFrame, TRAINER_MARK_FELL, -1});
  if (s.nFighting > 0 && prev.nFighting == 0) {
    bool bRecent = false;
    for (const TrainerMark &m : l.marks)
      if (m.nKind == TRAINER_MARK_FIGHT && m.nFrame + 20 * TRAINER_SECOND > nFrame)
        bRecent = true;
    if (!bRecent)
      l.marks.push_back({nFrame, TRAINER_MARK_FIGHT, s.nFighting});
  }
}

// A new line, from a parent at a frame: the parent's commands before it (and at it, for an
// order that takes over within that frame), its snapshots, marks and seconds up to it.
TrainerLine &NewLine(int nKind, int nParent, unsigned nSplit, bool bWithFrame)
{
  TrainerLine l;
  l.nId = g_nNextId++;
  l.nParent = nParent;
  l.nSplit = nSplit;
  l.nEnd = nSplit;
  l.nKind = nKind;
  l.nPoint = -1;
  l.nResult = TRAINER_RESULT_NONE;
  l.nOrder = g_nOrder++;
  if (nKind == TRAINER_ATTEMPT)
    l.name = "Attempt " + Num(++g_nAttempts);
  else
    l.name = "Timeline " + Num(++g_nTimelines);
  if (const TrainerLine *p = Line(nParent)) {
    // the parent's up to the split, and past it the other players' (a replay's, which go on
    // as recorded; the player's own are the new line's)
    for (const TrainerCommand &c : p->commands)
      if (c.nFrame < nSplit || (bWithFrame && c.nFrame == nSplit) || c.bStorm != g_LocalStormID)
        l.commands.push_back(c);
    for (const TrainerMark &m : p->marks)
      if (m.nFrame <= nSplit)
        l.marks.push_back(m);
    for (size_t i = 0; i < p->seconds.size() && i * TRAINER_SECOND <= nSplit; i++)
      l.seconds.push_back(p->seconds[i]);
    for (const TrainerCheck &c : p->checks)
      if (c.nFrame <= nSplit)
        l.checks.push_back(c);
    for (const TrainerBuilt &b : p->built)
      if (b.nFrame <= nSplit)
        l.built.push_back(b);
    l.nBuiltTo = nSplit;
    PortSnapShare(nParent, l.nId, nSplit);
    // the self-test: the parent's frames up to the split are this line's too
    auto from = g_testHashes.find(nParent);
    if (from != g_testHashes.end()) {
      TestHashes &to = g_testHashes[l.nId];
      const size_t n = std::min<size_t>(from->second.h.size(), nSplit + 1);
      to.h.assign(from->second.h.begin(), from->second.h.begin() + n);
      to.have.assign(from->second.have.begin(), from->second.have.begin() + n);
    }
  }
  g_lines.push_back(l);
  return g_lines.back();
}

// Is a line in another's past: an ancestor, or itself, up to the frame where the chain leaves it.
bool InPast(int nLine, unsigned nFrame, int nOf, unsigned nNow)
{
  unsigned nLimit = nNow;
  for (const TrainerLine *p = Line(nOf); p != NULL; p = Line(p->nParent)) {
    if (p->nId == nLine)
      return nFrame <= nLimit;
    nLimit = p->nSplit;
  }
  return false;
}

// Whether a line is one a timeline or the current line comes from: never dropped.
bool Kept(const TrainerLine &l)
{
  for (const TrainerLine &k : g_lines) {
    if (k.nKind != TRAINER_TIMELINE && k.nId != g_nCurrent)
      continue;
    for (const TrainerLine *p = &k; p != NULL; p = Line(p->nParent))
      if (p->nId == l.nId)
        return true;
  }
  return false;
}

// The attempts past the last eight go, the oldest first; a child of one goes to its parent.
void DropOldAttempts(void)
{
  for (;;) {
    std::vector<const TrainerLine *> held;
    for (const TrainerLine &l : g_lines)
      if (l.nKind == TRAINER_ATTEMPT && !Kept(l))
        held.push_back(&l);
    if (g_setup.nAttemptsHeld <= 0 || static_cast<int>(held.size()) <= g_setup.nAttemptsHeld)
      return;
    const TrainerLine *pOld = *std::min_element(held.begin(), held.end(), [](const TrainerLine *a, const TrainerLine *b) {
      return a->nOrder < b->nOrder;
    });
    const int nGone = pOld->nId, nUp = pOld->nParent;
    for (TrainerLine &l : g_lines)
      if (l.nParent == nGone)
        l.nParent = nUp;
    PortSnapDropLine(nGone);
    g_lines.erase(std::remove_if(g_lines.begin(), g_lines.end(), [&](const TrainerLine &l) { return l.nId == nGone; }),
                  g_lines.end());
  }
}

// Gives this frame's commands of the current line, as a replay's are given (net_mgr_replay_turns):
// all of them, or live only the other players' (a replay's, past the take-over).
void GiveCommands(unsigned nFrame, bool bOthersOnly)
{
  const TrainerLine &l = Current();
  size_t i = CommandAt(l, nFrame);
  if (i >= l.commands.size() || l.commands[i].nFrame != nFrame)
    return;
  g_bFeeding = true;
  set_rand_enabled(1);
  gbInPacketProcess = 1;
  for (; i < l.commands.size() && l.commands[i].nFrame == nFrame; i++) {
    const TrainerCommand &c = l.commands[i];
    if (bOthersOnly && c.bStorm == g_LocalStormID)
      continue;
    for (int j = 0; j < PlayerLobbySlots(); j++)
      if (gPlayerData[j].dwStormID == c.bStorm) {
        g_ActiveHumanID = gPlayerData[j].dwPlayerID;
        g_ActiveNationID = gPlayerData[j].dwPlayerID;
        break;
      }
    g_ActiveStormID = c.bStorm;
    std::vector<unsigned char> ab = c.ab;  // the game may write what it is given
    net_process_game_turn(reinterpret_cast<char *>(ab.data()), static_cast<unsigned>(ab.size()), 1);
  }
  g_ActiveNationID = g_LocalNationID;
  g_ActiveHumanID = g_LocalHumanID;
  g_ActiveStormID = g_LocalStormID;
  gbInPacketProcess = 0;
  set_rand_enabled(0);
  g_bFeeding = false;
}

void SeekDone(void)
{
  g_nLastFrames = oc_dwReplayFrame - g_nLastSnap;
  gamemap_force_redraw();
  const TrainerLine &l = Current();
  // the replay a session started from is only ever watched: it never changes
  const bool bWatched = oc_dwReplayFrame < l.nEnd || l.nKind == TRAINER_REPLAY;
  if (g_nSeekAfter == AFTER_PAUSE) {
    pause_game();
    g_nState = bWatched ? TRAINER_WATCH : TRAINER_LIVE;
  } else if (l.nKind == TRAINER_REPLAY || (g_nSeekAfter == AFTER_WATCH && oc_dwReplayFrame < l.nEnd)) {
    g_nState = TRAINER_WATCH;
    unpause_game(0);
  } else {
    // live from here: what the line held after this frame is not this line's any more
    g_nState = TRAINER_LIVE;
    unpause_game(0);
  }
  PortPresentRedraw();
}

// The game to a frame of a line: from the line's snapshot at or before it, or on from here when
// the game is on that line before the frame already.
bool SeekTo(int nLine, unsigned nTo, int nAfter)
{
  TrainerLine *pLine = Line(nLine);
  if (pLine == NULL)
    return false;
  if (nTo > pLine->nEnd)
    nTo = pLine->nEnd;
  g_nSeekTo = nTo;
  g_nSeekAfter = nAfter;
  const bool bOnward = nLine == g_nCurrent && oc_dwReplayFrame <= nTo &&
                       nTo - oc_dwReplayFrame < TRAINER_SNAP_EVERY;
  g_nCurrent = nLine;
  if (bOnward) {
    g_nLastSnap = oc_dwReplayFrame;
  } else {
    const unsigned nAt = PortSnapRestore(nLine, nTo, true);
    if (nAt == ~0u)
      return false;
    // the selection came back with the game, but the console's copy and the unit under the
    // cursor are the program's and may name units gone now (a click selected one with no sprite)
    status_get_new_selection();
    status_set_update();
    cursor_forget_target();
    MadeRebase();
    g_nLastSnap = nAt;
    g_nFed = ~0u;
  }
  if (oc_dwReplayFrame >= nTo) {
    SeekDone();
    return true;
  }
  g_nState = TRAINER_SEEK;
  unpause_game(0);
  return true;
}

// A retry point's name as the views give it.
std::string PointName(int nPoint)
{
  return nPoint >= 0 && nPoint < static_cast<int>(g_points.size()) ? g_points[nPoint].name : "the start";
}

// A new attempt or timeline from a frame of a line, played on at once.
bool TryFrom(int nFrom, unsigned nFrame, int nKind, int nPoint, std::string *pSaid)
{
  TrainerLine &l = NewLine(nKind, nFrom, nFrame, false);
  l.nPoint = nPoint;
  const int nId = l.nId;
  const std::string name = l.name;
  if (nPoint >= 0)
    g_points[nPoint].nTries++;
  DropOldAttempts();
  if (!SeekTo(nId, nFrame, AFTER_LIVE))
    return false;
  if (pSaid != NULL)
    *pSaid = name + "\n" + (nPoint >= 0 ? "from " + PointName(nPoint) : "from " + PortTrainerTime(nFrame));
  return true;
}

void TestReport(void)
{
  FILE *f = fopen(PortLogFile("trainertest.txt"), "w");
  if (f == NULL)
    return;
  fprintf(f, "trainertest: back %u frames every %u: watched, retried, scrubbed and taken over, branched\n",
          g_nTestBack, g_nTestPeriod);
  fprintf(f, "furthest frame %u, rewinds %u (retries %u, scrubs %u, take-overs %u, branches %u), frames played again %u\n",
          g_nTestHighest, g_nTestRewinds, g_nTestRetries, g_nTestScrubs, g_nTestTakeOvers, g_nTestBranches,
          g_nTestReplayed);
  fprintf(f, "frames whose hash differed %u", g_nTestBad);
  if (g_nTestFirstBad != ~0u)
    fprintf(f, " (first at %u, domains %#x)", g_nTestFirstBad, g_nTestFirstDomains);
  size_t nCommands = 0;
  for (const TrainerLine &l : g_lines)
    nCommands += l.commands.size();
  fprintf(f, "\nlines %zu, commands %zu, store %zu bytes, state %zu bytes\n", g_lines.size(), nCommands,
          PortSnapStoreBytes(), PortSnapStateBytes());
  for (const TrainerLine &l : g_lines)
    fprintf(f, "  %d %s: from %d at %u, to %u, %zu commands\n", l.nId, l.name.c_str(), l.nParent, l.nSplit, l.nEnd,
            l.commands.size());
  fprintf(f, "%s\n", g_nTestBad == 0 && g_nTestRewinds > 0 ? "exact" : g_nTestRewinds == 0 ? "no rewind" : "DIFFERS");
  fclose(f);
}

int TestOn(void)
{
  if (g_nTest < 0) {
    g_nTest = 0;
    const char *psz = getenv("HYPERION_TRAINERTEST");
    if (psz != NULL && *psz) {
      g_nTest = 1;
      unsigned a, b;
      if (sscanf(psz, "%u,%u", &a, &b) == 2 && a > 0 && b > 0 && b < a) {
        g_nTestPeriod = a;
        g_nTestBack = b;
      }
    }
  }
  return g_nTest;
}

// The frame's hash, kept the first time the line (or the line it came from) played it and
// compared every time after. True when no line had played this far.
bool TestFrame(unsigned nFrame)
{
  SyncHash h;
  SyncHashCompute(&h);
  TestHashes &t = g_testHashes[g_nCurrent];
  if (t.have.size() <= nFrame) {
    t.h.resize(nFrame + 1);
    t.have.resize(nFrame + 1, 0);
  }
  const bool bNew = nFrame > g_nTestHighest;
  if (bNew)
    g_nTestHighest = nFrame;
  if (!t.have[nFrame]) {
    t.h[nFrame] = h;
    t.have[nFrame] = 1;
    return bNew;
  }
  g_nTestReplayed++;
  const SyncHash &want = t.h[nFrame];
  unsigned nDomains = 0;
  for (int d = 0; d < SYNC_DOMAINS; d++)
    if (h.aqDomain[d] != want.aqDomain[d])
      nDomains |= 1u << d;
  if (h.dwRandState != want.dwRandState || h.dwRandCount != want.dwRandCount)
    nDomains |= 0x100;
  if (nDomains && g_nTestBad++ == 0) {
    g_nTestFirstBad = nFrame;
    g_nTestFirstDomains = nDomains;
    TestReport();
  }
  return bNew;
}

// What the self-test does at a frame first reached: a retry point <back> before each period's
// end, and at the end one of four by turns: back on the line and watched; R to that point, an
// attempt; a scrub there, paused, and watched with the orders going on, so the first takes over;
// a branch, then back on the branch and watched.
void TestAct(unsigned nFrame)
{
  if (nFrame % g_nTestPeriod == g_nTestPeriod - g_nTestBack && g_nState == TRAINER_LIVE)
    PortTrainerSetPoint(NULL);
  if (nFrame < g_nTestPeriod || nFrame % g_nTestPeriod != 0 || g_nState != TRAINER_LIVE)
    return;
  const unsigned nBack = nFrame - g_nTestBack;
  switch ((nFrame / g_nTestPeriod) % 4) {
    case 1:
      if (SeekTo(g_nCurrent, nBack, AFTER_WATCH))
        g_nTestRewinds++;
      break;
    case 2:
      if (PortTrainerRetry(NULL)) {
        g_nTestRewinds++;
        g_nTestRetries++;
      }
      break;
    case 3:
      if (PortTrainerScrub(g_nCurrent, nBack)) {
        g_nTestRewinds++;
        g_nTestScrubs++;
        g_bTestGoOn = true;
        g_bTestTakeOver = true;
      }
      break;
    default:
      if (PortTrainerBranch(NULL)) {
        g_nTestBranches++;
        if (SeekTo(g_nCurrent, nBack, AFTER_WATCH))
          g_nTestRewinds++;
      }
      break;
  }
  TestReport();
}

const PortLogger TrainerLog("hyperion_trainer.log", PortLogger::SHARED);

// A state hash folded to 64 bits: the random state and every domain's.
unsigned long long Fold(const SyncHash &h)
{
  unsigned long long q = 1469598103934665603ULL;
  auto mix = [&q](unsigned long long v) {
    for (int i = 0; i < 8; i++) {
      q ^= (v >> (8 * i)) & 0xFF;
      q *= 1099511628211ULL;
    }
  };
  mix(h.dwRandState);
  mix(h.dwRandCount);
  for (int d = 0; d < SYNC_DOMAINS; d++)
    mix(h.aqDomain[d]);
  return q;
}

unsigned long long HashNow(void)
{
  SyncHash h;
  SyncHashCompute(&h);
  return Fold(h);
}

// A line's hash at a check frame: kept the first time the line plays it as its own, and held to
// it every time after, in this game or a saved session's.
void CheckFrame(TrainerLine &l, unsigned nFrame)
{
  if (nFrame == 0)
    return;
  // by frame: a line's are made in order, a replay's come in order
  const auto it = std::lower_bound(l.checks.begin(), l.checks.end(), nFrame,
                                   [](const TrainerCheck &c, unsigned f) { return c.nFrame < f; });
  if (it != l.checks.end() && it->nFrame == nFrame) {
    const unsigned long long q = HashNow();
    g_nChecked++;
    if (it->qHash != q && g_nDiffered++ == 0)
      TrainerLog("%s plays differently at %s (frame %u): %016llx, %016llx when it was made", l.name.c_str(),
                 PortTrainerTime(nFrame).c_str(), nFrame, q, it->qHash);
    return;
  }
  if (g_nState == TRAINER_LIVE && nFrame % TRAINER_CHECK_EVERY == 0)
    l.checks.push_back({nFrame, HashNow()});
}

// With the set-up's "every minute as well", a retry point at each game minute a line first
// reaches, unless one is there already on the way here.
void MinutePoint(unsigned nFrame)
{
  if (!g_setup.bPointsEachMinute || g_nState != TRAINER_LIVE || nFrame == 0 || nFrame % (60 * TRAINER_SECOND) != 0)
    return;
  for (const TrainerPoint &p : g_points)
    if (p.nFrame == nFrame && InPast(p.nLine, p.nFrame, g_nCurrent, nFrame))
      return;
  g_points.push_back({g_nCurrent, nFrame, "At " + PortTrainerTime(nFrame), 0});
  if (!PortSnapHas(g_nCurrent, nFrame))
    PortSnapTake(g_nCurrent, nFrame);
}

// ---- saved sessions --------------------------------------------------------------------------------

// The skirmish set-up's record (port_frontscreens.h), as a session keeps it.
std::string GameJson(const SkirmishGame &g)
{
  std::string s = "{\"map\": " + PortJsonQuote(g.map) + ", \"title\": " + PortJsonQuote(g.title) + ", \"type\": " + PortJsonQuote(g.type);
  char sz[160];
  snprintf(sz, sizeof sz, ", \"key\": %d, \"melee\": %s, \"colours\": %s, \"speed\": %d, \"hyperion\": %s, \"seats\": [",
           g.nKey, g.bMelee ? "true" : "false", g.bColours ? "true" : "false", g.nSpeed, g.bHyperion ? "true" : "false");
  s += sz;
  for (size_t i = 0; i < g.seats.size(); ++i) {
    const SkirmishSeat &e = g.seats[i];
    snprintf(sz, sizeof sz, "%s{\"race\": %d, \"colour\": %d, \"rgb\": %u, \"team\": %d, \"level\": %d, \"start\": %d}",
             i ? ", " : "", e.nRace, e.nColour, e.rgb, e.nTeam, e.nLevel, e.nStart);
    s += sz;
  }
  return s + "]}";
}

bool GameRead(const PortJson &j, SkirmishGame *g)
{
  if (j.kind != PortJson::OBJECT || j["seats"].size() == 0)
    return false;
  auto flag = [&](const char *psz) { return j[psz].kind == PortJson::BOOL && j[psz].b; };
  g->map = j["map"].Str();
  g->title = j["title"].Str();
  g->type = j["type"].Str();
  g->nKey = static_cast<int>(j["key"].Num(0));
  g->bMelee = flag("melee");
  g->bColours = flag("colours");
  g->nSpeed = static_cast<int>(j["speed"].Num(6));
  g->bHyperion = flag("hyperion");
  g->seats.clear();
  for (size_t i = 0; i < j["seats"].size(); ++i) {
    const PortJson &e = j["seats"].At(i);
    SkirmishSeat s;
    s.nRace = static_cast<int>(e["race"].Num(6));
    s.nColour = static_cast<int>(e["colour"].Num(-1));
    s.rgb = static_cast<unsigned>(e["rgb"].Num(0));
    s.nTeam = static_cast<int>(e["team"].Num(0));
    s.nLevel = static_cast<int>(e["level"].Num(-1));
    s.nStart = static_cast<int>(e["start"].Num(-1));
    g->seats.push_back(s);
  }
  return true;
}

std::string SetupJson(const TrainerSetup &s)
{
  char sz[256];
  snprintf(sz, sizeof sz,
           "\"computers\": %d, \"ffa\": %s, \"speed\": %d, \"rules\": %d, \"seed\": %u, \"pointsEachMinute\": %s, "
           "\"attemptsHeld\": %d, \"ask\": %s",
           s.nComputers, s.bFfa ? "true" : "false", s.nSpeed, s.nRules, s.nSeed, s.bPointsEachMinute ? "true" : "false",
           s.nAttemptsHeld, s.bAsk ? "true" : "false");
  return "{\"map\": " + PortJsonQuote(s.map) + ", \"mapName\": " + PortJsonQuote(s.mapName) + ", \"race\": " + PortJsonQuote(s.race) +
         ", \"races\": " + PortJsonQuote(s.races) + ", \"replay\": " + PortJsonQuote(s.replay) +
         ", \"take\": " + std::to_string(s.nTakeSlot) + ", " + sz + (s.bGame ? ", \"game\": " + GameJson(s.game) : "") + "}";
}

void SetupRead(const PortJson &j, TrainerSetup *s)
{
  s->map = j["map"].Str();
  s->mapName = j["mapName"].Str();
  s->race = j["race"].Str("terran");
  s->races = j["races"].Str();
  s->nComputers = static_cast<int>(j["computers"].Num(1));
  s->bFfa = j["ffa"].kind == PortJson::BOOL && j["ffa"].b;
  s->nSpeed = static_cast<int>(j["speed"].Num(6));
  s->nRules = static_cast<int>(j["rules"].Num(-1));
  s->nSeed = static_cast<unsigned>(j["seed"].Num(0));
  s->bPointsEachMinute = j["pointsEachMinute"].kind == PortJson::BOOL && j["pointsEachMinute"].b;
  s->nAttemptsHeld = 0;  // the first sessions held 8; every one now holds as many as it makes
  s->bAsk = !(j["ask"].kind == PortJson::BOOL && !j["ask"].b);
  s->replay = j["replay"].Str();
  s->nTakeSlot = static_cast<int>(j["take"].Num(-1));
  s->bGame = GameRead(j["game"], &s->game);
}

int g_nSaveMode = TRAINER_SAVE_ALL;  // how the session is saved as its game ends

// What a session keeps: every line, or a timeline and the lines one comes from.
bool SavedLine(const TrainerLine &l)
{
  if (g_nSaveMode == TRAINER_SAVE_ALL)
    return true;
  for (const TrainerLine &k : g_lines) {
    if (k.nKind == TRAINER_ATTEMPT)
      continue;
    for (const TrainerLine *p = &k; p != NULL; p = Line(p->nParent))
      if (p->nId == l.nId)
        return true;
  }
  return false;
}

// A line's lists as text: entries split by ';', fields by ' '.
std::string CommandsText(const TrainerLine &l)
{
  std::string s;
  char sz[32];
  for (const TrainerCommand &c : l.commands) {
    snprintf(sz, sizeof sz, "%s%u %u ", s.empty() ? "" : ";", c.nFrame, c.bStorm);
    s += sz;
    for (unsigned char b : c.ab) {
      snprintf(sz, sizeof sz, "%02x", b);
      s += sz;
    }
  }
  return s;
}

std::string SecondsText(const TrainerLine &l)
{
  std::string s;
  char sz[16];
  for (const TrainerSecond &c : l.seconds) {
    for (int i = 0; i < kTrainerSecondCount; i++) {
      snprintf(sz, sizeof sz, "%s%d", i ? " " : s.empty() ? "" : ";", c.*kTrainerSecondFields[i]);
      s += sz;
    }
  }
  return s;
}

std::string BuiltText(const TrainerLine &l)
{
  std::string s;
  char sz[40];
  for (const TrainerBuilt &b : l.built) {
    snprintf(sz, sizeof sz, "%s%u %u %u", s.empty() ? "" : ";", b.nFrame, b.wType, b.nSupply);
    s += sz;
  }
  return s;
}

std::vector<std::string> Split(const std::string &s, char c)
{
  std::vector<std::string> out;
  size_t i = 0;
  while (i <= s.size() && !s.empty()) {
    const size_t j = s.find(c, i);
    out.push_back(s.substr(i, j == std::string::npos ? std::string::npos : j - i));
    if (j == std::string::npos)
      break;
    i = j + 1;
  }
  return out;
}

// A line as the file has it.
bool LineRead(const PortJson &j, TrainerLine *l)
{
  l->nId = static_cast<int>(j["id"].Num(0));
  l->nParent = static_cast<int>(j["parent"].Num(0));
  l->nSplit = static_cast<unsigned>(j["split"].Num(0));
  l->nEnd = static_cast<unsigned>(j["end"].Num(0));
  const std::string kind = j["kind"].Str("timeline");
  l->nKind = kind == "attempt" ? TRAINER_ATTEMPT : kind == "replay" ? TRAINER_REPLAY : TRAINER_TIMELINE;
  l->name = j["name"].Str("Timeline");
  l->nPoint = static_cast<int>(j["point"].Num(-1));
  l->nResult = static_cast<int>(j["result"].Num(TRAINER_RESULT_NONE));
  l->nOrder = static_cast<unsigned>(j["order"].Num(0));
  for (const std::string &e : Split(j["commands"].Str(), ';')) {
    const std::vector<std::string> f = Split(e, ' ');
    if (f.size() != 3 || f[2].size() % 2 != 0 || f[2].empty())
      return false;
    TrainerCommand c;
    c.nFrame = static_cast<unsigned>(strtoul(f[0].c_str(), NULL, 10));
    c.bStorm = static_cast<unsigned char>(strtoul(f[1].c_str(), NULL, 10));
    for (size_t i = 0; i < f[2].size(); i += 2)
      c.ab.push_back(static_cast<unsigned char>(strtoul(f[2].substr(i, 2).c_str(), NULL, 16)));
    l->commands.push_back(c);
  }
  for (const std::string &e : Split(j["marks"].Str(), ';')) {
    unsigned f, k;
    int w = 0;
    if (sscanf(e.c_str(), "%u %u %d", &f, &k, &w) >= 2)
      l->marks.push_back({f, static_cast<int>(k), w});
  }
  for (const std::string &e : Split(j["seconds"].Str(), ';')) {
    TrainerSecond s = {};
    const std::vector<std::string> f = Split(e, ' ');
    if (f.size() < 11)
      continue;
    for (int i = 0; i < kTrainerSecondCount && i < static_cast<int>(f.size()); i++)
      s.*kTrainerSecondFields[i] = atoi(f[i].c_str());
    l->seconds.push_back(s);
  }
  for (const std::string &e : Split(j["built"].Str(), ';')) {
    unsigned f, t, n;
    if (sscanf(e.c_str(), "%u %u %u", &f, &t, &n) == 3)
      l->built.push_back({f, static_cast<unsigned short>(t), static_cast<unsigned short>(n)});
  }
  // a replay's line is played only as far as it was watched; its end is the replay's
  l->nBuiltTo = static_cast<unsigned>(j["builtTo"].Num(l->nEnd));
  for (const std::string &e : Split(j["checks"].Str(), ';')) {
    unsigned f;
    unsigned long long q;
    if (sscanf(e.c_str(), "%u %llx", &f, &q) == 2)
      l->checks.push_back({f, q});
  }
  return l->nId > 0;
}

std::string g_lastSaved;  // the file the last session was saved to
std::string g_openedAs;   // the name the session's file was opened or last saved under
std::string g_openedPrint;  // what an opened session held as it opened (Print)

// What a session holds, as text to compare: its lines, their commands and marks, its points.
std::string Print(void)
{
  std::string s;
  char sz[96];
  for (const TrainerLine &l : g_lines) {
    snprintf(sz, sizeof sz, "%d %d %u %u %d %zu|", l.nId, l.nParent, l.nSplit, l.nEnd, l.nKind, l.marks.size());
    s += sz + l.name + "|" + CommandsText(l) + "\n";
  }
  for (const TrainerPoint &p : g_points) {
    snprintf(sz, sizeof sz, "%d %u|", p.nLine, p.nFrame);
    s += sz + p.name + "\n";
  }
  return s;
}

// A file's name from a session's: the characters a file name cannot hold made '_'.
std::string FileName(const std::string &name)
{
  std::string s;
  for (char c : name.empty() ? std::string("Session") : name)
    s += strchr("<>:\"/\\|?*", c) != NULL || static_cast<unsigned char>(c) < 0x20 ? '_' : c;
  while (!s.empty() && (s.back() == ' ' || s.back() == '.'))
    s.pop_back();
  return s.empty() ? std::string("Session") : s;
}

void Save(void)
{
  SYSTEMTIME t;
  GetLocalTime(&t);
  char szNow[32];
  snprintf(szNow, sizeof szNow, "%04d-%02d-%02d %02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
  const std::string path = PortTrainerSavePath(g_setup.name);
  CreateDirectoryA((std::string(PortDataFolder()) + "Trainer").c_str(), NULL);
  CreateDirectoryA((std::string(PortDataFolder()) + "Trainer\\Sessions").c_str(), NULL);
  // the replay a session started from goes beside it, so the session opens when the replay
  // has gone (an AutoSave folder keeps only the last few)
  if (!g_setup.replay.empty()) {
    const size_t nDot = path.find_last_of('.');
    const std::string copy = path.substr(0, nDot) + ".rep";
    if (_stricmp(copy.c_str(), g_setup.replay.c_str()) != 0) {
      if (CopyFileA(g_setup.replay.c_str(), copy.c_str(), FALSE))
        g_setup.replay = copy;
      else
        TrainerLog("could not copy the replay %s beside the session", g_setup.replay.c_str());
    }
  }
  if (g_made.empty())
    g_made = szNow;
  // the points on lines kept, and each kept line's own point by its place among them
  std::vector<int> pointAt(g_points.size(), -1);
  std::vector<const TrainerPoint *> points;
  for (size_t i = 0; i < g_points.size(); i++) {
    const TrainerLine *l = Line(g_points[i].nLine);
    if (l != NULL && SavedLine(*l)) {
      pointAt[i] = static_cast<int>(points.size());
      points.push_back(&g_points[i]);
    }
  }
  std::string s = "{\n  \"version\": 1,\n  \"name\": " + PortJsonQuote(g_setup.name) + ",\n  \"made\": " + PortJsonQuote(g_made) +
                  ",\n  \"played\": " + PortJsonQuote(szNow) + ",\n";
  char sz[256];
  snprintf(sz, sizeof sz, "  \"simRevision\": %d,\n  \"hyperionRules\": %s,\n  \"counters\": [%d, %d, %d, %d, %u],\n",
           HX_SIM_REVISION, g_hxRulesGame ? "true" : "false", g_nAttempts, g_nTimelines, g_nPointsMade, g_nNextId, g_nOrder);
  s += sz;
  s += "  \"setup\": " + SetupJson(g_setup) + ",\n  \"lines\": [";
  static const char *const kKinds[] = {"timeline", "attempt", "replay"};
  bool bFirst = true;
  int nSaved = 0;
  for (const TrainerLine &l : g_lines) {
    if (!SavedLine(l))
      continue;
    nSaved++;
    snprintf(sz, sizeof sz,
             "\n    {\"id\": %d, \"parent\": %d, \"split\": %u, \"end\": %u, \"kind\": \"%s\", \"point\": %d, \"result\": %d, "
             "\"order\": %u,\n     \"name\": ",
             l.nId, l.nParent, l.nSplit, l.nEnd, kKinds[l.nKind],
             l.nPoint >= 0 && l.nPoint < static_cast<int>(pointAt.size()) ? pointAt[l.nPoint] : -1, l.nResult, l.nOrder);
    s += std::string(bFirst ? "" : ",") + sz + PortJsonQuote(l.name);
    bFirst = false;
    s += ",\n     \"commands\": \"" + CommandsText(l) + "\",\n     \"marks\": \"";
    for (size_t i = 0; i < l.marks.size(); i++) {
      snprintf(sz, sizeof sz, "%s%u %d %d", i ? ";" : "", l.marks[i].nFrame, l.marks[i].nKind, l.marks[i].nWho);
      s += sz;
    }
    s += "\",\n     \"checks\": \"";
    for (size_t i = 0; i < l.checks.size(); i++) {
      snprintf(sz, sizeof sz, "%s%u %016llx", i ? ";" : "", l.checks[i].nFrame, l.checks[i].qHash);
      s += sz;
    }
    snprintf(sz, sizeof sz, "\",\n     \"builtTo\": %u,\n     \"built\": \"", l.nBuiltTo);
    s += sz + BuiltText(l);
    s += "\",\n     \"seconds\": \"" + SecondsText(l) + "\"}";
  }
  s += "\n  ],\n  \"points\": [";
  for (size_t i = 0; i < points.size(); i++) {
    snprintf(sz, sizeof sz, "%s\n    {\"line\": %d, \"frame\": %u, \"tries\": %d, \"name\": ", i ? "," : "",
             points[i]->nLine, points[i]->nFrame, points[i]->nTries);
    s += sz + PortJsonQuote(points[i]->name) + "}";
  }
  s += "\n  ]\n}\n";
  FILE *f = fopen(path.c_str(), "wb");
  if (f == NULL) {
    TrainerLog("could not write %s", path.c_str());
    return;
  }
  fwrite(s.data(), 1, s.size(), f);
  fclose(f);
  g_lastSaved = path;
  g_setup.open = path;
  g_openedAs = g_setup.name;
  TrainerLog("saved %s: %d lines of %zu, %zu points%s", path.c_str(), nSaved, g_lines.size(), points.size(),
             g_nSaveMode == TRAINER_SAVE_ALL ? ", every line" : ", timelines only");
}

// A saved session read into the game's: its lines, points and counters. False when it does not read.
bool Load(const std::string &path)
{
  std::string text, error;
  PortJson j;
  if (!PortReadFile(path, &text) || !PortJsonParse(text.c_str(), &j, &error) || j.kind != PortJson::OBJECT ||
      j["version"].Num(0) != 1) {
    TrainerLog("could not read %s %s", path.c_str(), error.c_str());
    return false;
  }
  std::vector<TrainerLine> lines;
  for (size_t i = 0; i < j["lines"].size(); i++) {
    TrainerLine l;
    if (!LineRead(j["lines"].At(i), &l)) {
      TrainerLog("%s: line %zu does not read", path.c_str(), i);
      return false;
    }
    lines.push_back(l);
  }
  if (lines.empty() || lines.front().nParent != 0)
    return false;
  // a line's parent is a line read before it, as in a session, so walking the parents (a
  // line's past, a timeline's origin) ends; two lines naming each other would go round for ever
  std::vector<int> before;
  for (const TrainerLine &l : lines) {
    if (l.nParent != 0 && std::find(before.begin(), before.end(), l.nParent) == before.end()) {
      TrainerLog("%s: line %d names %d as its parent, which is no line before it", path.c_str(), l.nId, l.nParent);
      return false;
    }
    before.push_back(l.nId);
  }
  g_lines = lines;
  g_points.clear();
  for (size_t i = 0; i < j["points"].size(); i++) {
    const PortJson &p = j["points"].At(i);
    g_points.push_back({static_cast<int>(p["line"].Num(0)), static_cast<unsigned>(p["frame"].Num(0)), p["name"].Str(),
                        static_cast<int>(p["tries"].Num(0))});
  }
  const PortJson &c = j["counters"];
  g_nAttempts = static_cast<int>(c.At(0).Num(0));
  g_nTimelines = static_cast<int>(c.At(1).Num(1));
  g_nPointsMade = static_cast<int>(c.At(2).Num(0));
  g_nNextId = static_cast<int>(c.At(3).Num(0));
  g_nOrder = static_cast<unsigned>(c.At(4).Num(0));
  for (const TrainerLine &l : g_lines) {
    g_nNextId = (std::max)(g_nNextId, l.nId + 1);
    g_nOrder = (std::max)(g_nOrder, l.nOrder + 1);
  }
  g_made = j["made"].Str();
  return true;
}

// ---- a replay as a session's start ------------------------------------------------------------------

bool g_bReplaySession;  // this session's game was loaded from a replay and turned live

// The replay's commands, read from the recorder's buffer as playback reads them (net_record_read_turn):
// blocks of a frame (4 bytes) and a count of bytes, then the commands, each a player's storm id
// and the command, its size GetCommandByteSize's. A frame may span blocks. 1.17's rules drop the
// last frame's, as its playback does.
std::vector<TrainerCommand> ReplayCommands(void)
{
  std::vector<TrainerCommand> out;
  const CNetRecord *r = gpNetRecord_g;
  if (r == NULL || r->pBuf == NULL)
    return out;
  const BYTE *p = r->pBuf;
  const BYTE *pEnd = r->pBuf + r->nLen;
  while (p + 5 <= pEnd) {
    const unsigned nFrame = *reinterpret_cast<const DWORD *>(p);
    const unsigned nTotal = p[4];
    p += 5;
    unsigned nRun = 0;
    while (nRun < nTotal && p < pEnd) {
      const BYTE bStorm = *p++;
      if (p >= pEnd)
        break;
      // the size read from a copy of the command's first two bytes, its opcode and the count a
      // select's size is made of, so a buffer ending mid-command is not read past
      const net_cmd_select_t head = {*p, static_cast<BYTE>(p + 1 < pEnd ? p[1] : 0)};
      const int nSize = GetCommandByteSize(&head);
      if (nSize <= 0 || p + nSize > pEnd) {
        nRun += 1;
        continue;
      }
      out.push_back({nFrame, bStorm, std::vector<unsigned char>(p, p + nSize)});
      p += nSize;
      nRun += static_cast<unsigned>(nSize) + 1;
    }
  }
  if (!HxRules() && !out.empty()) {
    const unsigned nLast = out.back().nFrame;
    while (!out.empty() && out.back().nFrame == nLast)
      out.pop_back();
  }
  return out;
}

// The replay's game, loaded as a replay is, turned into a live one with the player in a recorded
// person's seat: that person's storm id becomes the local one (0) in the slots and the commands,
// the local ids and the vision are theirs, the replay's feed stops and the console is theirs.
// Done before the first frame, so the game is the one the replay recorded.
bool TakeSeat(std::vector<TrainerCommand> *pCommands, std::vector<TrainerCheck> *pChecks)
{
  *pCommands = ReplayCommands();
  // a Hyperion replay's checks, which the replay's line is held to as it plays
  pChecks->clear();
  for (const SyncHash &h : ReplayHxExpected())
    pChecks->push_back({h.dwFrame, Fold(h)});
  int nSeat = -1;
  for (int i = 0; i < 8; i++) {
    if (gPlayerData[i].nType != 2)
      continue;
    if (i == g_setup.nTakeSlot) {
      nSeat = i;
      break;
    }
    if (g_setup.nTakeSlot < 0 && (nSeat < 0 || lstrcmpiA(gPlayerData[i].szName, gLocalPlayerName) == 0))
      nSeat = i;
  }
  if (nSeat < 0) {
    TrainerLog("the replay has no person to take over");
    return false;
  }
  const DWORD dwStorm = gPlayerData[nSeat].dwStormID;
  if (dwStorm != 0 && dwStorm < 8) {
    for (int i = 0; i < 12; i++)
      if (gPlayerData[i].dwStormID == 0)
        gPlayerData[i].dwStormID = dwStorm;
    gPlayerData[nSeat].dwStormID = 0;
    for (TrainerCommand &c : *pCommands)
      c.bStorm = c.bStorm == dwStorm ? 0 : c.bStorm == 0 ? static_cast<unsigned char>(dwStorm) : c.bStorm;
  }
  const DWORD dwNation = gPlayerData[nSeat].dwPlayerID;
  g_LocalNationID = dwNation;
  g_LocalHumanID = dwNation;
  g_LocalStormID = 0;
  g_StormToNationID[0] = dwNation;
  g_StormToHumanID[0] = dwNation;
  gPlayback = 0;
  net_record_stop_playback();
  netmgr_replay_init();
  PreparePlayerSlots();
  game_slots_init();
  sgbShowAll_g = 0;
  ViewHxReset(false);
  ReplayHxGameStart();
  // the console as the seat's race; status_free lets go of its strings too
  status_free();
  status_init();
  status_load_strings();
  // the view on the seat's start: its first building that takes resources, else any of its units
  CUnit *pAt = NULL;
  for (CUnit *u = CUnit::GetUsedList(); u != NULL; u = u->pNext) {
    if (u->bPlayer != dwNation)
      continue;
    const unsigned t = u->wType;
    if (t == 106 || t == 131 || t == 132 || t == 133 || t == 154) {
      pAt = u;
      break;
    }
    if (pAt == NULL)
      pAt = u;
  }
  if (pAt != NULL)
    pAt->CenterView();
  TrainerLog("from the replay %s: seat %d (%s), %zu commands, %d frames", g_setup.replay.c_str(), nSeat,
             gPlayerData[nSeat].szName, pCommands->size(), replay_get_total_frames());
  return true;
}

}  // namespace

bool PortTrainerFromMenus(const TrainerSetup &s)
{
  g_armed = s;
  g_bArmed = true;
  if (!s.replay.empty()) {
    if (PortLaunchTrainerReplay(s.replay.c_str()))
      return true;
    g_bArmed = false;
    TrainerLog("the replay %s did not start", s.replay.c_str());
    return false;
  }
  if (s.bGame) {
    if (PortLaunchSkirmishGame(s.game))
      return true;
    g_bArmed = false;
    TrainerLog("the game on %s did not start", s.game.map.c_str());
    return false;
  }
  RulesChooseNext(s.nRules);
  const std::string seats = std::string(1, s.race.empty() ? 'r' : s.race[0]) + s.races;
  if (PortLaunchTrainer(s.map.c_str(), s.race.c_str(), seats.c_str(), s.nComputers, s.bFfa))
    return true;
  g_bArmed = false;
  SessionClear();  // the rules chosen above (port_session.h)
  TrainerLog("the game on %s did not start", s.map.c_str());
  return false;
}

const TrainerSetup &PortTrainerSetupNow(void)
{
  return g_setup;
}

bool PortTrainerSeed(int *pnSeed)
{
  if (!g_bArmed || g_armed.nSeed == 0)
    return false;
  *pnSeed = static_cast<int>(g_armed.nSeed);
  return true;
}

int PortTrainerSpeed(void)
{
  return g_bSession && g_bFromMenus ? g_setup.nSpeed : -1;
}

void PortTrainerGameEnd(void)
{
  PortGameSpeedTimesSet(1);
  if (!g_bSession)
    return;
  if (g_bFromMenus && TestOn() != 1 && !g_lines.empty() && g_nSaveMode != TRAINER_SAVE_NONE)
    Save();
  g_nSaveMode = TRAINER_SAVE_ALL;
  g_bEndedFromMenus = g_bFromMenus;
  g_bCameBack = g_bCameBack || g_bFromMenus;
  if (g_nChecked > 0)
    TrainerLog("checks held %u, differed %u", g_nChecked, g_nDiffered);
  // a replay's game is let go as a replay's: its map freed, no replay of it written
  if (g_bReplaySession)
    gPlayback = 1;
  g_bReplaySession = false;
  g_bSession = false;
}

bool PortTrainerCameBack(void)
{
  const bool b = g_bCameBack;
  g_bCameBack = false;
  return b;
}

bool PortTrainerAfterGame(void)
{
  const bool b = g_bEndedFromMenus;
  g_bEndedFromMenus = false;
  return b;
}

std::string PortTrainerSavePath(const std::string &name)
{
  // its own file, when it was opened or saved under this name
  if (!g_setup.open.empty() && name == g_openedAs)
    return g_setup.open;
  const std::string base = std::string(PortDataFolder()) + "Trainer\\Sessions\\" + FileName(name);
  std::string path = base + ".hxtrain";
  for (int n = 2; GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES && n < 1000; ++n)
    path = base + " " + std::to_string(n) + ".hxtrain";
  return path;
}

bool PortTrainerChanged(void)
{
  return g_setup.open.empty() || g_bOpening || Print() != g_openedPrint;
}

std::string PortTrainerLastSaved(void)
{
  return g_lastSaved;
}

std::vector<TrainerSaved> PortTrainerSessions(void)
{
  struct Found {
    std::string path;
    FILETIME t;
  };
  std::vector<Found> found;
  const std::string dir = std::string(PortDataFolder()) + "Trainer\\Sessions\\";
  // .hxtrain, and .json as the first sessions were saved
  for (const char *pszType : {"*.hxtrain", "*.json"}) {
    WIN32_FIND_DATAA fd;
    const HANDLE h = FindFirstFileA((dir + pszType).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
      continue;
    do {
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        found.push_back({dir + fd.cFileName, fd.ftLastWriteTime});
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) { return CompareFileTime(&a.t, &b.t) > 0; });
  std::vector<TrainerSaved> out;
  for (const Found &f : found) {
    std::string text;
    PortJson j;
    if (!PortReadFile(f.path, &text) || !PortJsonParse(text.c_str(), &j, NULL) || j["version"].Num(0) != 1)
      continue;
    TrainerSaved s;
    s.path = f.path;
    s.name = j["name"].Str("Session");
    s.when = j["played"].Str();
    SetupRead(j["setup"], &s.setup);
    s.setup.name = s.name;
    s.setup.open = f.path;
    s.bOtherBuild = j["hyperionRules"].kind == PortJson::BOOL && j["hyperionRules"].b &&
                    static_cast<int>(j["simRevision"].Num(0)) != HX_SIM_REVISION;
    for (size_t i = 0; i < j["lines"].size(); i++) {
      const PortJson &lj = j["lines"].At(i);
      TrainerLine l;
      l.nId = static_cast<int>(lj["id"].Num(0));
      l.nParent = static_cast<int>(lj["parent"].Num(0));
      l.nSplit = static_cast<unsigned>(lj["split"].Num(0));
      l.nEnd = static_cast<unsigned>(lj["end"].Num(0));
      l.nKind = lj["kind"].Str() == "attempt" ? TRAINER_ATTEMPT : lj["kind"].Str() == "replay" ? TRAINER_REPLAY : TRAINER_TIMELINE;
      l.name = lj["name"].Str();
      if (l.nKind != TRAINER_ATTEMPT)
        s.nTimelines++;
      else
        s.nAttempts++;
      s.nLongest = (std::max)(s.nLongest, l.nEnd);
      s.shape.push_back(l);
    }
    out.push_back(s);
  }
  return out;
}

bool PortTrainerTapeWanted(void)
{
  const bool b = g_bTapeWanted;
  g_bTapeWanted = false;
  return b;
}

unsigned PortTrainerDiffered(void)
{
  return g_nDiffered;
}

void PortTrainerGameStart(void)
{
  g_lines.clear();
  g_points.clear();
  g_nNextId = 1;
  g_nOrder = 0;
  g_nAttempts = g_nTimelines = g_nPointsMade = 0;
  g_nState = TRAINER_LIVE;
  g_nFed = ~0u;
  g_bFirstSnap = false;
  // a session set up on the menus, used up by this game; a replay's is loaded as a replay
  g_bFromMenus = g_bArmed && (!gPlayback || !g_armed.replay.empty());
  g_bReplaySession = false;
  g_setup = g_bFromMenus ? g_armed : TrainerSetup();
  g_openedAs = g_setup.open.empty() ? std::string() : g_setup.name;
  g_openedPrint.clear();
  g_bArmed = false;
  g_bSession = PortCommandOptions()->bTrainer || TestOn() == 1 || g_bFromMenus;
  g_bOpening = g_bTapeWanted = false;
  g_nLastSnap = g_nLastFrames = 0;
  g_bMadeOk = false;
  PortGameSpeedTimesSet(1);
  g_made.clear();
  g_nDiffered = g_nChecked = 0;
  g_testHashes.clear();
  g_nTestHighest = 0;
  g_bTestGoOn = g_bTestTakeOver = false;
  // --trainer with --replay: a session from that replay; a replay played otherwise is not one
  // (HYPERION_TRAINER_SEAT=<slot> takes that person's seat, for a test)
  if (g_bSession && !g_bFromMenus && gPlayback && PortCommandOptions()->bTrainer) {
    g_setup.replay = PortCommandOptions()->szReplay;
    if (const char *pszSeat = getenv("HYPERION_TRAINER_SEAT"))
      g_setup.nTakeSlot = atoi(pszSeat);
  }
  if (gPlayback && g_setup.replay.empty())
    g_bSession = false;
  if (!g_bSession)
    return;
  // a replay's game, turned live with the player in a recorded seat: the replay the first line
  std::vector<TrainerCommand> recorded;
  std::vector<TrainerCheck> recordedChecks;
  if (!g_setup.replay.empty() && gPlayback) {
    if (!TakeSeat(&recorded, &recordedChecks)) {
      g_bSession = false;
      return;
    }
    g_bReplaySession = true;
  }
  // the game as it was made, to make it again
  g_setup.nSeed = static_cast<unsigned>(net_get_fixed_seed());
  g_setup.nRules = g_hxRulesGame ? 1 : 0;
  if (!g_setup.open.empty()) {
    if (Load(g_setup.open)) {
      // the first line watched from its start; paused there once frame 0 is kept
      g_nCurrent = g_lines.front().nId;
      g_nState = g_lines.front().nEnd > 0 ? TRAINER_WATCH : TRAINER_LIVE;
      g_bOpening = true;
      TrainerLog("opened %s: %zu lines, %zu points, seed %u", g_setup.open.c_str(), g_lines.size(), g_points.size(),
                 g_setup.nSeed);
      return;
    }
    g_setup.open.clear();
    g_lines.clear();
    g_points.clear();
    g_nNextId = 1;
    g_nOrder = 0;
    g_nAttempts = g_nTimelines = g_nPointsMade = 0;
  }
  TrainerLine &l = NewLine(TRAINER_TIMELINE, 0, 0, false);
  g_nCurrent = l.nId;
  g_points.push_back({l.nId, 0, "The start", 0});
  if (g_bReplaySession) {
    // the replay, watched from its start: any order of the player's, or T, takes over
    g_nTimelines--;
    l.nKind = TRAINER_REPLAY;
    l.name = "The replay";
    l.commands = recorded;
    l.checks = recordedChecks;
    l.nEnd = static_cast<unsigned>(replay_get_total_frames());
    g_nState = TRAINER_WATCH;
  }
}

void PortTrainerFeed(void)
{
  if (!g_bSession || gwGameMode != 3)
    return;
  if (g_bTestGoOn && IsGamePaused() && g_nState != TRAINER_SEEK) {
    g_bTestGoOn = false;
    PortTrainerGoOn(TRAINER_GO_WATCH, NULL);
  }
  if (IsGamePaused())
    return;
  const unsigned nFrame = oc_dwReplayFrame;
  // the game's start, before frame 0's commands: the start's snapshot
  if (!g_bFirstSnap && nFrame == 0) {
    g_bFirstSnap = true;
    PortSnapTake(g_nCurrent, 0);
  }
  // what the player orders while a seek runs is not sent
  if (g_nState == TRAINER_SEEK)
    sgdwBytesInCmdQueue = static_cast<DWORD>(PortNetKeepSyncs(sgbCmdQueue_g, sgdwBytesInCmdQueue));
  if (nFrame == g_nFed)
    return;
  g_nFed = nFrame;
  GiveCommands(nFrame, g_nState == TRAINER_LIVE);
}

void PortTrainerCommand(int nStorm, const void *pCmd, unsigned cbCmd)
{
  if (!g_bSession || g_bFeeding || gwGameMode != 3 || cbCmd == 0)
    return;
  // what the recorder leaves out (net_record.cpp's s_abNoRecord), and chat
  static const unsigned char kNot[] = {0x37, 0x05, 0x06, 0x07, 0x08, 0x10, 0x11, 0x38, 0x39, 0x3A, 0x3B, 0x55, 0x5C};
  const unsigned char bOp = *static_cast<const unsigned char *>(pCmd);
  for (unsigned char b : kNot)
    if (bOp == b)
      return;
  const unsigned nFrame = oc_dwReplayFrame;
  if (g_nState == TRAINER_SEEK)
    return;  // nothing of the player's runs while a seek does
  if (g_nState == TRAINER_WATCH) {
    // the player's order while a line plays on: a new attempt from this frame, the line's
    // commands of this frame given already
    TrainerLine &l = NewLine(TRAINER_ATTEMPT, g_nCurrent, nFrame, true);
    g_nCurrent = l.nId;
    g_nState = TRAINER_LIVE;
    if (g_bTestTakeOver) {
      g_bTestTakeOver = false;
      g_nTestTakeOvers++;
    }
    DropOldAttempts();
  }
  TrainerLine &l = Current();
  const unsigned char *p = static_cast<const unsigned char *>(pCmd);
  // after this frame's: the other players' later ones may be there already
  const auto at = std::upper_bound(l.commands.begin(), l.commands.end(), nFrame,
                                   [](unsigned f, const TrainerCommand &c) { return f < c.nFrame; });
  l.commands.insert(at, {nFrame, static_cast<unsigned char>(nStorm), std::vector<unsigned char>(p, p + cbCmd)});
}

void PortTrainerFrameEnd(void)
{
  if (!g_bSession || gwGameMode != 3)
    return;
  const unsigned nFrame = oc_dwReplayFrame;
  TrainerLine &l = Current();
  if (g_nState == TRAINER_LIVE && nFrame > l.nEnd)
    l.nEnd = nFrame;
  if (nFrame % TRAINER_SNAP_EVERY == 0 && !PortSnapHas(l.nId, nFrame))
    PortSnapTake(l.nId, nFrame);
  ScanBuilt(l, nFrame);
  // the figures of each game second, the first at the first frame's end, and the marks they make
  if (l.seconds.size() <= nFrame / TRAINER_SECOND) {
    const TrainerSecond prev = l.seconds.empty() ? TrainerSecond{} : l.seconds.back();
    TrainerSecond s;
    Sample(&s, l);
    if (!l.seconds.empty())
      AutoMarks(l, prev, s, nFrame);
    while (l.seconds.size() <= nFrame / TRAINER_SECOND)
      l.seconds.push_back(s);
  }
  CheckFrame(l, nFrame);
  MinutePoint(nFrame);
  // a saved session's lines start from its snapshot at frame 0: paused there, the Tape up
  if (g_bOpening && PortSnapHas(g_nCurrent, 0)) {
    g_bOpening = false;
    g_openedPrint = Print();
    for (const TrainerLine &o : g_lines)
      if (o.nId != g_nCurrent)
        PortSnapShare(g_nCurrent, o.nId, 0);
    pause_game();
    g_bTapeWanted = true;
  }
  // at a watched line's end the game is live, but at the replay's it stops with the Tape up:
  // going on from there is a new attempt
  if (g_nState == TRAINER_WATCH && nFrame >= l.nEnd) {
    if (l.nKind != TRAINER_REPLAY) {
      g_nState = TRAINER_LIVE;
    } else if (!IsGamePaused()) {
      pause_game();
      g_bTapeWanted = true;
    }
  }
  if (g_nState == TRAINER_SEEK && nFrame >= g_nSeekTo)
    SeekDone();
  if (g_nState == TRAINER_LIVE)
    g_bTestTakeOver = false;
  if (TestOn() == 1) {
    if (TestFrame(nFrame))
      TestAct(nFrame);
    else if (nFrame % 2400 == 0)
      TestReport();
  }
}

bool PortTrainerSession(void)
{
  return g_bSession && gwGameMode == 3;
}

void PortTrainerPause(void)
{
  if (!g_bSession)
    return;
  if (g_nState == TRAINER_SEEK)
    g_nSeekAfter = AFTER_PAUSE;  // paused where the seek ends
  else
    pause_game();
}

void PortTrainerResume(void)
{
  if (!g_bSession || g_nState == TRAINER_SEEK)
    return;
  // on from the replay's end: an attempt of the player's from there
  if (Current().nKind == TRAINER_REPLAY && oc_dwReplayFrame >= Current().nEnd) {
    PortTrainerTakeOver(NULL);
    return;
  }
  unpause_game(0);
}

bool PortTrainerHeld(void)
{
  return g_bSession && (IsGamePaused() || (g_nState == TRAINER_SEEK && g_nSeekAfter == AFTER_PAUSE));
}

bool PortTrainerTakeOver(std::string *pSaid)
{
  if (!g_bSession || g_nState == TRAINER_SEEK)
    return false;
  const unsigned nFrame = oc_dwReplayFrame;
  if (nFrame >= Current().nEnd && g_nState == TRAINER_LIVE)
    return false;  // live at the line's end already
  TrainerLine &l = NewLine(TRAINER_ATTEMPT, g_nCurrent, nFrame, false);
  g_nCurrent = l.nId;
  g_nState = TRAINER_LIVE;
  if (pSaid != NULL)
    *pSaid = l.name + "\nTaken over at " + PortTrainerTime(nFrame) + ", yours from here";
  DropOldAttempts();
  if (IsGamePaused())
    unpause_game(0);
  return true;
}

void PortTrainerLeave(int nSave, const std::string &name)
{
  if (!g_bSession)
    return;
  g_nSaveMode = nSave;
  if (!name.empty())
    g_setup.name = name;
  unpause_game(0);
  PortQuitToMenu();
}

bool PortTrainerSeeking(void)
{
  return g_bSession && g_nState == TRAINER_SEEK && gwGameMode == 3;
}

bool PortTrainerWatching(void)
{
  return g_bSession && gwGameMode == 3 &&
         (g_nState == TRAINER_SEEK || (g_nState == TRAINER_WATCH && !g_bTestTakeOver));
}

const std::vector<TrainerLine> &PortTrainerLines(void)
{
  return g_lines;
}

const TrainerLine *PortTrainerLine(int nId)
{
  return Line(nId);
}

int PortTrainerCurrent(void)
{
  return g_nCurrent;
}

int PortTrainerState(void)
{
  return g_nState;
}

const std::vector<TrainerPoint> &PortTrainerPoints(void)
{
  return g_points;
}

int PortTrainerRetryPoint(void)
{
  const unsigned nNow = oc_dwReplayFrame;
  int nBest = -1;
  for (int i = 0; i < static_cast<int>(g_points.size()); i++) {
    const TrainerPoint &p = g_points[i];
    if (p.nFrame + 12 > nNow && !(p.nFrame == 0 && nNow < 12))
      continue;
    if (!InPast(p.nLine, p.nFrame, g_nCurrent, nNow))
      continue;
    if (nBest < 0 || p.nFrame > g_points[nBest].nFrame)
      nBest = i;
  }
  return nBest;
}

void PortTrainerLastSeek(unsigned *pnSnap, unsigned *pnFrames)
{
  *pnSnap = g_nLastSnap;
  *pnFrames = g_nLastFrames;
}

bool PortTrainerSecondAt(int nLine, unsigned nFrame, TrainerSecond *pOut)
{
  const TrainerLine *l = Line(nLine);
  if (l == NULL || l->seconds.empty())
    return false;
  size_t i = nFrame / TRAINER_SECOND;
  if (i >= l->seconds.size())
    i = l->seconds.size() - 1;
  *pOut = l->seconds[i];
  return true;
}

bool PortTrainerRetry(std::string *pSaid)
{
  if (!g_bSession)
    return false;
  const int nPoint = PortTrainerRetryPoint();
  const TrainerPoint p = nPoint >= 0 ? g_points[nPoint] : TrainerPoint{g_lines.front().nId, 0, "The start", 0};
  return TryFrom(InPast(p.nLine, p.nFrame, g_nCurrent, oc_dwReplayFrame) ? g_nCurrent : p.nLine, p.nFrame,
                 TRAINER_ATTEMPT, nPoint, pSaid);
}

bool PortTrainerSetPoint(std::string *pSaid)
{
  if (!g_bSession)
    return false;
  const unsigned nFrame = oc_dwReplayFrame;
  for (const TrainerPoint &p : g_points)
    if (p.nLine == g_nCurrent && p.nFrame == nFrame)
      return false;
  g_points.push_back({g_nCurrent, nFrame, "Point " + Num(++g_nPointsMade), 0});
  // a snapshot where R comes back to, so it comes back at once
  if (!PortSnapHas(g_nCurrent, nFrame))
    PortSnapTake(g_nCurrent, nFrame);
  if (pSaid != NULL)
    *pSaid = "Retry point at " + PortTrainerTime(nFrame) + "\nR comes back here";
  return true;
}

bool PortTrainerKeep(int nLine, std::string *pSaid)
{
  TrainerLine *l = Line(nLine);
  if (l == NULL || l->nKind != TRAINER_ATTEMPT)
    return false;
  l->nKind = TRAINER_TIMELINE;
  l->name = "Timeline " + Num(++g_nTimelines);
  if (pSaid != NULL)
    *pSaid = l->name + " kept\nSaved with the session, to come back to";
  return true;
}

bool PortTrainerBranch(std::string *pSaid)
{
  if (!g_bSession)
    return false;
  const unsigned nFrame = oc_dwReplayFrame;
  const std::string was = Current().name;
  PortTrainerKeep(g_nCurrent, NULL);
  const std::string kept = Current().name;
  if (!PortSnapHas(g_nCurrent, nFrame))
    PortSnapTake(g_nCurrent, nFrame);
  TrainerLine &l = NewLine(TRAINER_TIMELINE, g_nCurrent, nFrame, false);
  g_nCurrent = l.nId;
  g_points.push_back({l.nId, nFrame, "Branch at " + PortTrainerTime(nFrame), 0});
  g_nState = TRAINER_LIVE;
  if (pSaid != NULL)
    *pSaid = "Branched at " + PortTrainerTime(nFrame) + "\n" + kept + " kept as it was; you go on as " + l.name;
  (void)was;
  return true;
}

bool PortTrainerMarkHere(std::string *pSaid)
{
  if (!g_bSession)
    return false;
  TrainerLine &l = Current();
  l.marks.push_back({oc_dwReplayFrame, TRAINER_MARK_YOURS});
  if (pSaid != NULL)
    *pSaid = "Marked " + PortTrainerTime(oc_dwReplayFrame) + "\nOn " + l.name;
  return true;
}

bool PortTrainerScrub(int nLine, unsigned nFrame)
{
  if (!g_bSession)
    return false;
  if (nLine == g_nCurrent && nFrame == oc_dwReplayFrame && g_nState != TRAINER_SEEK)
    return true;
  return SeekTo(nLine, nFrame, AFTER_PAUSE);
}

bool PortTrainerGoOn(int nChoice, std::string *pSaid)
{
  if (!g_bSession)
    return false;
  const unsigned nFrame = oc_dwReplayFrame;
  TrainerLine &l = Current();
  switch (nChoice) {
    case TRAINER_GO_AGAIN:
    case TRAINER_GO_TIMELINE: {
      TrainerLine &n = NewLine(nChoice == TRAINER_GO_AGAIN ? TRAINER_ATTEMPT : TRAINER_TIMELINE, l.nId, nFrame, false);
      g_nCurrent = n.nId;
      if (pSaid != NULL)
        *pSaid = n.name + "\nfrom " + PortTrainerTime(nFrame) + " on " + Line(n.nParent)->name +
                 (nChoice == TRAINER_GO_TIMELINE ? ", saved" : "");
      DropOldAttempts();
      break;
    }
    case TRAINER_GO_REWRITE: {
      if (l.nKind == TRAINER_REPLAY)
        return false;  // the replay itself never changes
      const unsigned nDropped = l.nEnd > nFrame ? l.nEnd - nFrame : 0;
      // the player's from here; the other players' stay, as a replay recorded them
      l.commands.erase(std::remove_if(l.commands.begin() + CommandAt(l, nFrame), l.commands.end(),
                                      [](const TrainerCommand &c) { return c.bStorm == g_LocalStormID; }),
                       l.commands.end());
      l.marks.erase(std::remove_if(l.marks.begin(), l.marks.end(), [&](const TrainerMark &m) { return m.nFrame > nFrame; }),
                    l.marks.end());
      if (l.seconds.size() > nFrame / TRAINER_SECOND + 1)
        l.seconds.resize(nFrame / TRAINER_SECOND + 1);
      l.built.erase(std::remove_if(l.built.begin(), l.built.end(), [&](const TrainerBuilt &b) { return b.nFrame > nFrame; }),
                    l.built.end());
      l.nBuiltTo = nFrame;
      l.checks.erase(std::remove_if(l.checks.begin(), l.checks.end(), [&](const TrainerCheck &c) { return c.nFrame > nFrame; }),
                     l.checks.end());
      l.nEnd = nFrame;
      l.nResult = TRAINER_RESULT_NONE;
      PortSnapDropAfter(l.nId, nFrame);
      for (TrainerLine &c : g_lines)
        if (c.nParent == l.nId && c.nSplit > nFrame)
          c.nParent = l.nParent;
      g_points.erase(std::remove_if(g_points.begin(), g_points.end(),
                                    [&](const TrainerPoint &p) { return p.nLine == l.nId && p.nFrame > nFrame; }),
                     g_points.end());
      if (pSaid != NULL)
        *pSaid = l.name + " rewritten from " + PortTrainerTime(nFrame) + "\n" + PortTrainerTime(nDropped) +
                 " after it dropped; nothing new is saved";
      break;
    }
    case TRAINER_GO_WATCH:
      g_nState = TRAINER_WATCH;
      g_nFed = ~0u;
      unpause_game(0);
      return true;
    default:
      return false;
  }
  g_nState = TRAINER_LIVE;
  unpause_game(0);
  return true;
}

std::string PortTrainerJson(void)
{
  static const char *const kStates[] = {"live", "watch", "seek"};
  static const char *const kKinds[] = {"timeline", "attempt", "replay"};
  char sz[256];
  snprintf(sz, sizeof sz, "\"session\":%s,\"state\":\"%s\",\"current\":%d,\"frame\":%u,\"paused\":%s,\"retry\":%d,",
           g_bSession ? "true" : "false", kStates[g_nState], g_nCurrent, oc_dwReplayFrame,
           IsGamePaused() ? "true" : "false", PortTrainerRetryPoint());
  std::string s = sz;
  snprintf(sz, sizeof sz, "\"checked\":%u,\"differed\":%u,\"seed\":%u,\"opening\":%s,", g_nChecked, g_nDiffered,
           g_setup.nSeed, g_bOpening ? "true" : "false");
  s += sz;
  s += "\"name\":" + PortJsonQuote(g_setup.name) + ",\"saved\":" + PortJsonQuote(g_lastSaved) + ",";
  s += "\"lines\":[";
  for (size_t i = 0; i < g_lines.size(); i++) {
    const TrainerLine &l = g_lines[i];
    // a name a file or the player gave goes through PortJsonQuote, so a quote or a backslash
    // in it still leaves the answer JSON the client can read, however long the name is
    snprintf(sz, sizeof sz, "%s{\"id\":%d,\"parent\":%d,\"split\":%u,\"end\":%u,\"kind\":\"%s\",\"name\":", i ? "," : "",
             l.nId, l.nParent, l.nSplit, l.nEnd, kKinds[l.nKind]);
    s += sz + PortJsonQuote(l.name);
    snprintf(sz, sizeof sz, ",\"commands\":%zu,\"marks\":%zu,\"built\":%zu,\"result\":%d}", l.commands.size(),
             l.marks.size(), l.built.size(), l.nResult);
    s += sz;
  }
  s += "],\"points\":[";
  for (size_t i = 0; i < g_points.size(); i++) {
    snprintf(sz, sizeof sz, "%s{\"line\":%d,\"frame\":%u,\"name\":", i ? "," : "", g_points[i].nLine,
             g_points[i].nFrame);
    s += sz + PortJsonQuote(g_points[i].name);
    snprintf(sz, sizeof sz, ",\"tries\":%d}", g_points[i].nTries);
    s += sz;
  }
  return s + "]";
}

std::string PortTrainerTime(unsigned nFrame)
{
  const unsigned s = nFrame / TRAINER_SECOND;
  char sz[16];
  snprintf(sz, sizeof sz, "%u:%02u", s / 60, s % 60);
  return sz;
}
