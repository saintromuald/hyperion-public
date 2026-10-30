// port_ai.cpp -- the computer player's boundary (port_ai.h).
//
// A level is a set of knobs on 1.17's own AI (ai-core.md, stage C). Hard is 1.17's AI as it
// is, by Hyperion's rules. The easier levels think and follow their script more slowly,
// keep fewer workers, attack with fewer units and not before a time; the harder ones think
// and follow their script faster. The Cheater levels play as Elite and cheat as StarCraft
// II's do (its MeleeAI.galaxy, AIMeleeCheatResources, and the vision flag): each sees the
// whole map; resources brings back 1.0 times a load for the first 2 minutes, then a share
// more growing to 1.5 times at 20 minutes; insane 1.2 times from the start, growing to 2.0
// times at 20 minutes. The resource gives no more than its load.
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "port_ai.h"
#include "port_session.h"  // Session
#include "../front/port_text.h"    // PT_: the names are logged and saved in English
#include "../platform/port_log.h"  // PortLogger
#include "../../h/hxrules.h"       // HX_OPT_AI_LEVELS, g_hxRuleOptions
#include "../../h/playerlimits.h"  // PlayerSlots, PLAYER_SLOTS_MAX, PLAYER_LOBBY_117
#include "../../h/playertables.h"  // PlayersSeatSlot
#include "../../h/playervision.h"  // VisionHxHidden, VisionHxUnexplored
#include "../../h/net_cmds.h"      // PlayerData
#include "../../h/CUnit.h"
#include "../../h/CSprite.h"
#include "../replays/port_snap.h"          // PORT_SNAP_KEEP
#include "../../gen/globals117.h"  // gPlayerData, g_dwElapsedTime
#include "../../h/SAI_Main.h"  // sai_update

namespace {

// g_dwElapsedTime counts a tick every 16 frames (victory.cpp), a frame 42 ms at Fastest.
const DWORD kTicksPerMinute = 60000 / (42 * 16);

struct Knobs {
  WORD wThink;        // a unit's wait before it thinks again, % of 1.17's
  WORD wWait;         // the script's waits, % of 1.17's
  BYTE bAttack;       // attack_add's counts, %
  BYTE bWorkers;      // a town's worker target, %
  BYTE bFirstAttack;  // minutes of game time at Fastest before the script's first attack
  bool bVision;       // sees the whole map
  BYTE bHarvest;      // 0 none, 1 StarCraft II's resources ramp, 2 its insane ramp
};

// Classic and Hard are 1.17's numbers; Classic alone plays by classic rules.
// The easier levels' first attacks and their sizes follow StarCraft II's scripts (its rush
// opening's first attack: 2, 5, 8 and 11 supply at Very Easy, Easy, Medium and Hard, at
// about six minutes).
const Knobs kKnobs[AI_LEVELS] = {
    {100, 100, 100, 100, 0, false, 0},   // Classic
    {300, 200, 20, 50, 7, false, 0},     // Very Easy
    {200, 160, 45, 70, 6, false, 0},     // Easy
    {150, 125, 70, 85, 5, false, 0},     // Medium
    {100, 100, 100, 100, 0, false, 0},   // Hard
    {75, 90, 100, 100, 0, false, 0},     // Harder
    {60, 80, 100, 100, 0, false, 0},     // Very Hard
    {50, 70, 100, 100, 0, false, 0},     // Elite
    {50, 70, 100, 100, 0, true, 0},      // Cheater: vision
    {50, 70, 100, 100, 0, true, 1},      // Cheater: resources
    {50, 70, 100, 100, 0, true, 2},      // Cheater: insane
};

const char *const kLevelNames[AI_LEVELS] = {
    PT_("Classic AI"), PT_("Very Easy"), PT_("Easy"), PT_("Medium"), PT_("Hard"), PT_("Harder"), PT_("Very Hard"),
    PT_("Elite"), PT_("Cheater: vision"), PT_("Cheater: resources"), PT_("Cheater: insane")};
const char kLevelLetters[AI_LEVELS + 1] = "c1234567vri";  // see AiLevelLetter
const char *const kStyleNames[AI_STYLES] = {PT_("Its own script")};

int g_nSeated;                            // the rows seated: the local player and its computers
bool g_bSeated;                           // AiSeat ran for the game starting
AiProfile g_aRecorded[PLAYER_SLOTS_MAX];  // the last replay's or save's record
AiProfile g_aSlot[PLAYER_SLOTS_MAX];      // each slot's profile in the game being played
bool g_bVision;                           // a slot of this game sees the whole map
bool g_bVisionFirst;                      // and the fog made with the map is not yet off
PORT_SNAP_KEEP(g_bVisionFirst);           // the trainer's snapshots hold it (port_snap.cpp)

const Knobs &KnobsOf(int nPlayer) {
  return kKnobs[nPlayer >= 0 && nPlayer < PLAYER_SLOTS_MAX && g_aSlot[nPlayer].bLevel < AI_LEVELS
                    ? g_aSlot[nPlayer].bLevel : AI_LEVEL_CLASSIC];
}

bool IsClassic(const AiProfile &p) {
  return p.bLevel == AI_LEVEL_CLASSIC && p.bStyle == AI_STYLE_OWN;
}

// A seated row's profile, as the session chose it (port_session.h).
AiProfile SeatedProfile(int nRow) {
  AiProfile p;
  if (Session().bAi) {
    p.bLevel = Session().aRow[nRow].bAiLevel;
    p.bStyle = Session().aRow[nRow].bAiStyle;
  }
  return p;
}

// hyperion_ai.log: the levels each game's computers play with
const PortLogger Log("hyperion_ai.log");

DWORD Percent(DWORD dw, unsigned nPct) {
  return static_cast<DWORD>((static_cast<unsigned long long>(dw) * nPct + 50) / 100);
}

}  // namespace

const char *AiLevelName(int nLevel) {
  return nLevel >= 0 && nLevel < AI_LEVELS ? kLevelNames[nLevel] : kLevelNames[0];
}

const char *AiStyleName(int nStyle) {
  return nStyle >= 0 && nStyle < AI_STYLES ? kStyleNames[nStyle] : kStyleNames[0];
}

// c Classic, 1 to 7 Very Easy to Elite, v r i the Cheater levels.
char AiLevelLetter(int nLevel) {
  return nLevel >= 0 && nLevel < AI_LEVELS ? kLevelLetters[nLevel] : 'c';
}

int AiLevelFromLetter(char c) {
  if (c >= 'A' && c <= 'Z')
    c = static_cast<char>(c - 'A' + 'a');
  const char *p = c ? strchr(kLevelLetters, c) : NULL;
  return p ? static_cast<int>(p - kLevelLetters) : -1;
}

std::string AiLevelsLabel(const char *pszLetters) {
  std::string s;
  bool bOther = false;
  for (const char *p = pszLetters; p != NULL && *p; ++p) {
    const int nLevel = AiLevelFromLetter(*p);
    if (nLevel < 0)
      continue;
    bOther = bOther || nLevel != AI_LEVEL_CLASSIC;
    const std::string name = AiLevelName(nLevel);
    if (("/ " + s + " /").find("/ " + name + " /") == std::string::npos)
      s += (s.empty() ? "" : " / ") + name;
  }
  return bOther ? s : std::string();
}

bool AiLevelReady(int nLevel) {
  return nLevel >= 0 && nLevel < AI_LEVELS;
}

void AiChoose(int nRow, int nLevel, int nStyle) {
  if (nRow < 0 || nRow > PLAYER_SLOTS_MAX)
    return;
  SessionRow &row = Session().aRow[nRow];
  Session().bAi = true;
  row.bAiLevel = static_cast<BYTE>(AiLevelReady(nLevel) ? nLevel : AI_LEVEL_CLASSIC);
  row.bAiStyle = static_cast<BYTE>(nStyle >= 0 && nStyle < AI_STYLES ? nStyle : AI_STYLE_OWN);
}

void AiChooseList(const char *pszList) {
  int nRow = 0;
  for (const char *p = pszList; p != NULL && *p != '\0' && nRow <= PLAYER_SLOTS_MAX; ++p) {
    if (*p == ',' || *p == ' ')
      continue;
    const int nLevel = AiLevelFromLetter(*p);
    AiChoose(nRow++, nLevel >= 0 ? nLevel : AI_LEVEL_CLASSIC, AI_STYLE_OWN);
  }
}

void AiSeat() {
  g_bSeated = true;
  // the local player (the benchmark's plays as a computer), then each computer seated
  // (PlayersSeatNote); the start shuffle moves them, so AiForGame finds a row's player by origin
  g_nSeated = 1;
  while (g_nSeated <= PLAYER_SLOTS_MAX && PlayersSeatSlot(g_nSeated - 1) >= 0)
    ++g_nSeated;
  Log("seating: %d rows, %s", g_nSeated, Session().bAi ? "levels chosen" : "none chosen");
}

bool AiChosenThisGame() {
  if (!g_bSeated)
    return false;
  for (int r = 0; r < g_nSeated; r++)
    if (!IsClassic(SeatedProfile(r)))
      return true;
  return false;
}

void AiForGame() {
  const bool bOld = gPlayback || gpLoadGameFile != NULL;
  for (int s = 0; s < PLAYER_SLOTS_MAX; s++) {
    g_aSlot[s] = AiProfile();
    if ((g_hxRuleOptions & HX_OPT_AI_LEVELS) == 0)
      continue;
    if (bOld) {
      g_aSlot[s] = g_aRecorded[s];
    } else if (g_bSeated) {
      // the row each player came from (PlayersSeatedComputer), wherever the starts put it
      const int k = PlayersSeatedComputer(s);
      if (s == static_cast<int>(g_LocalNationID))
        g_aSlot[s] = SeatedProfile(0);
      else if (k >= 0 && k + 1 < g_nSeated && gPlayerData[s].nType == 1)
        g_aSlot[s] = SeatedProfile(1 + k);
    }
  }
  Log("a game: %s, %s", bOld ? "played back or loaded" : g_bSeated ? "new, seated" : "new",
      (g_hxRuleOptions & HX_OPT_AI_LEVELS) ? "with levels" : "every computer Classic");
  g_bSeated = false;
  g_bVision = false;
  g_bVisionFirst = true;
  for (int s = 0; s < PLAYER_SLOTS_MAX; s++) {
    if (IsClassic(g_aSlot[s]))
      continue;
    g_bVision = g_bVision || KnobsOf(s).bVision;
    Log("slot %d: %s, %s", s, AiLevelName(g_aSlot[s].bLevel), AiStyleName(g_aSlot[s].bStyle));
  }
}

AiProfile AiProfileOf(int nSlot) {
  return nSlot >= 0 && nSlot < PLAYER_SLOTS_MAX ? g_aSlot[nSlot] : AiProfile();
}

void AiProfilesSave(BYTE *pbOut) {
  for (int s = 0; s < PLAYER_SLOTS_MAX; s++) {
    pbOut[s * AI_PROFILE_BYTES] = g_aSlot[s].bLevel;
    pbOut[s * AI_PROFILE_BYTES + 1] = g_aSlot[s].bStyle;
  }
}

void AiProfilesRecorded(const BYTE *pbIn, int nSlots) {
  for (int s = 0; s < PLAYER_SLOTS_MAX; s++) {
    g_aRecorded[s] = AiProfile();
    if (pbIn != NULL && s < nSlots) {
      const BYTE bLevel = pbIn[s * AI_PROFILE_BYTES], bStyle = pbIn[s * AI_PROFILE_BYTES + 1];
      g_aRecorded[s].bLevel = bLevel < AI_LEVELS ? bLevel : AI_LEVEL_CLASSIC;
      g_aRecorded[s].bStyle = bStyle < AI_STYLES ? bStyle : AI_STYLE_OWN;
    }
  }
}

void AiFrame() {
  // the first frame's: the fog was put over the map as it was made (mask_init)
  if (g_bVisionFirst) {
    g_bVisionFirst = false;
    AiVisionRefresh();
  }
  sai_update();
}

void AiVisionRefresh() {
  if (!g_bVision || gpSquareMap == NULL)
    return;
  DWORD dwOpen = 0;
  for (int s = 0; s < PLAYER_SLOTS_MAX && s < PLAYER_LOBBY_117; s++)
    if (KnobsOf(s).bVision)
      dwOpen |= (1u << s) | (1u << (8 + s));
  const int nTiles = static_cast<int>(gwMapWidth) * gwMapHeight;
  if (dwOpen != 0)
    for (int i = 0; i < nTiles; i++)
      gpSquareMap[i] &= ~dwOpen;
  PlayerSet *pHidden = VisionHxHidden();
  PlayerSet *pUnexplored = VisionHxUnexplored();
  if (pHidden == NULL || pUnexplored == NULL)
    return;
  for (int s = PLAYER_LOBBY_117; s < PLAYER_SLOTS_MAX; s++) {
    if (!KnobsOf(s).bVision)
      continue;
    for (int i = 0; i < nTiles; i++) {
      pHidden[i].Remove(s);
      pUnexplored[i].Remove(s);
    }
  }
}

DWORD AiMeleeScript(int nSlot, DWORD dwClassic) {
  (void)nSlot;
  return dwClassic;
}

BYTE AiThinkFrames(const CUnit *pUnit, BYTE bClassic) {
  const DWORD dw = Percent(bClassic, KnobsOf(pUnit->bPlayer).wThink);
  return static_cast<BYTE>(dw < 1 ? 1 : dw > 255 ? 255 : dw);
}

BYTE AiDifficulty(int nPlayer, BYTE bClassic) {
  (void)nPlayer;
  return bClassic;
}

DWORD AiScriptWait(int nPlayer, DWORD dwClassic) {
  return Percent(dwClassic, KnobsOf(nPlayer).wWait);
}

BYTE AiAttackCount(int nPlayer, BYTE bClassic) {
  if (bClassic == 0)
    return 0;
  const DWORD dw = Percent(bClassic, KnobsOf(nPlayer).bAttack);
  return static_cast<BYTE>(dw < 1 ? 1 : dw > 255 ? 255 : dw);
}

bool AiAttackAllowed(int nPlayer) {
  return g_dwElapsedTime >= KnobsOf(nPlayer).bFirstAttack * kTicksPerMinute;
}

BYTE AiWorkerTarget(int nPlayer, BYTE bClassic) {
  return static_cast<BYTE>(Percent(bClassic, KnobsOf(nPlayer).bWorkers));
}

BYTE AiHarvestExtra(const CUnit *pWorker, BYTE bLoad, BYTE bClassic) {
  const BYTE bRamp = KnobsOf(pWorker->bPlayer).bHarvest;
  if (bRamp == 0)
    return bClassic;
  // the multiplier in thousandths, by game time at Fastest (a tick is 16 frames of 42 ms)
  const unsigned long long qMs = static_cast<unsigned long long>(g_dwElapsedTime) * 16 * 42;
  unsigned nMilli;
  if (bRamp == 1)
    nMilli = qMs >= 1200000 ? 1500 : qMs <= 120000 ? 1000 : 1000 + static_cast<unsigned>((qMs - 120000) / 2160);
  else
    nMilli = qMs >= 1200000 ? 2000 : 1200 + static_cast<unsigned>(qMs / 1500);
  // the share of a unit left over is rounded by where the worker stands and when, so that
  // trips average the multiplier without a count to keep
  const unsigned nExtra = bLoad * (nMilli - 1000);
  const unsigned nRound = (pWorker->pSprite != NULL
                               ? pWorker->pSprite->wPosX * 31u + pWorker->pSprite->wPosY * 17u : 0u) +
                          g_dwElapsedTime * 97u;
  return static_cast<BYTE>((nExtra + nRound % 1000) / 1000);
}
