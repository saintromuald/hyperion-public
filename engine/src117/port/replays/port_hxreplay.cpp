// port_hxreplay.cpp -- Hyperion's replays: the sections, the rule record, the state
// check and the AutoSave copy (docs/modules/replays.md has the format).
//
// A game by classic rules keeps 1.17's replay as it is. A game by Hyperion's rules
// (h/hxrules.h) writes "hxRS" in place of "reRS", so 1.17 and the tools that read its replays
// never take one for theirs, then the same three blocks, the marker "HYPERION", a format
// version and the sections, each a 20-byte header in the save's layout (port_formats.cpp) and
// its records, uncompressed; a reader skips sections it does not know. A replay whose rules,
// revision or data are not this build's is refused, since it would play another game; one
// that plays is checked against SYNC at each frame it holds (SYNC's version is the hash's),
// and hyperion_replay.log, the live channel and the harness's observer report a difference.
#include <windows.h>
#include <bcrypt.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "../../h/playertables.h"
#include "../platform/port_log.h"  // PortLogger
#include "../../h/maplimits.h"
#include "../../h/net_cmds.h"   // PlayerData
#include "../../h/port_hxreplay.h"
#include "../../h/port_synchash.h"
#include "../../h/hxsim.h"
#include "../../h/hxrules.h"
#include "oc_data.h"
#include "../../h/Storm.h"   // SFileLoadFile
#include "../data/port_stormapi.h"  // SFileUnloadFile, SFileGetUserDataPath

#include "../../../hyperion/version.h"
#include "../../h/port_hxsave.h"  // HxSaveReplay
#include "../tools/port_bench.h"  // PortBenchMaking, PortBenchStressGame
#include "../gameplay/port_watchgame.h"  // PortWatchMaking
#include "../../h/poollimits.h"     // POOL_SCALE_MAX
#include "../render/port_teamcolor.h"  // PortTeamColorGet, PortTeamColorSet
#include "../data/port_moddata.h"        // g_hxModGame, ModDataId, ModDataFingerprint
#include "../gameplay/port_ai.h"             // AiProfilesSave, AiProfilesRecorded
#include "../gameplay/port_handicap.h"       // HandicapsSave, HandicapsRecorded
#include "../gameplay/port_mutators.h"       // MutatorsSave, MutatorsRecorded
#include "../gameplay/port_richbases.h"      // RichBasesRecord, RichBasesRecorded
#include "../platform/port_settings.h"       // "keepReplays", "checkModFingerprint"
#include "port_observe.h"  // ObserveFrameDone
#include "../lan/port_stormlan.h"  // LanGameFrame
#include "../../h/map_load.h"       // GetCurrentMap
#include "../../h/fd_user.h"        // pfopen, pfclose
#include "../../h/saveload.h"  // decompress
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

namespace {

const char kMarker[8] = {'H', 'Y', 'P', 'E', 'R', 'I', 'O', 'N'};
const DWORD kFormatVersion = 2;
const DWORD kRule = 'ELUR', kSlot = 'TOLS', kColr = 'RLOC', kSync = 'CNYS';  // "RULE", "SLOT", "COLR", "SYNC"
const DWORD kSave = 'EVAS';  // "SAVE": the frames the game was saved at, after each frame's check
const DWORD kRgbs = 'SBGR';  // "RGBS": the slots' colours in full RGB
const DWORD kRace = 'ECAR';  // "RACE": the slots' races, a mod's, when a slot has one
const DWORD kAiLv = 'VLIA';  // "AILV": the computers' levels and styles
const DWORD kHcap = 'PACH';  // "HCAP": the slots' handicaps
const DWORD kMuts = 'STUM';  // "MUTS": the game's mutators
const DWORD kRich = 'HCIR';  // "RICH": the resources that turned rich, unit indices (rich bases)
const DWORD kStrs = 'SRTS';  // "STRS": a benchmark's stress run, its armies and pools
const DWORD kUnts = 'STNU';  // "UNTS": the unit table, when it is not the slots'
const DWORD kSyncEvery = 24;   // frames, a second of game time at fastest

#pragma pack(push, 1)
struct Section {
  DWORD dwId;
  WORD wVersion;
  WORD wFlags;
  DWORD dwCount;
  DWORD dwRecordSize;
  DWORD dwBytes;
};
struct RuleRecord {
  char szRule[16];     // the rule version, HX_RULE_CLASSIC or HX_RULE_HYPERION
  BYTE abData[20];     // SHA-1 of the game data
  char szBuild[32];    // the build that wrote it
  DWORD dwSimRevision; // HX_SIM_REVISION; format 1 had none
  // revision 12 on; a shorter record reads them as 0
  DWORD dwOptions;     // HX_OPT_*
  char szMod[32];      // the mod whose data the game played with, empty for none
  BYTE abMod[20];      // SHA-1 of that mod's data, and of the data of the mods it depends on
  // a shorter record reads it as empty
  char szDepends[96];  // the mods with data it depends on, "id version, ..." (ModDataDepends)
};
const unsigned kRuleSize11 = offsetof(RuleRecord, dwOptions);  // the record up to revision 11
const unsigned kRuleSizeNoDepends = offsetof(RuleRecord, szDepends);  // and before the mods it depends on
struct ColorRecord {
  BYTE abRamp[8];
  BYTE bMinimap;
};
struct RgbRecord {
  BYTE bSet;
  BYTE abRgb[3];
};
struct StressRecord {
  DWORD dwArmy;   // units a computer, given at the first frame
  DWORD dwPools;  // the pools' scale, 0 for the players' own
};
#pragma pack(pop)
static_assert(sizeof(Section) == 20, "the save's section header");

// The files whose contents decide what the simulation does.
const char *const kDataFiles[] = {
    "arr\\units.dat", "arr\\weapons.dat", "arr\\flingy.dat", "arr\\sprites.dat", "arr\\images.dat",
    "arr\\upgrades.dat", "arr\\techdata.dat", "arr\\orders.dat", "scripts\\iscript.bin",
    "scripts\\aiscript.bin", "scripts\\bwscript.bin",
};

bool g_noted;
bool g_haveSlots, g_haveColors, g_haveRgbs;
PlayerData g_slots[PLAYER_SLOTS_MAX];
ColorRecord g_colors[PLAYER_SLOTS_MAX];
RgbRecord g_rgbs[PLAYER_SLOTS_MAX];

std::vector<SyncHash> g_recorded;   // the game being played
std::vector<SyncHash> g_expected;   // the replay being played, from its SYNC
size_t g_nextExpected;
// A save changes the game it is taken from (the pylons' power fields are put up again, the
// units' collision boxes sorted again: saveload_write_save_data), so a replay notes the
// frames its game was saved at, and its playback saves there too, to a scratch file
// (HxSaveReplay), so it goes on as the game did.
std::vector<DWORD> g_saves;          // the game being played
std::vector<DWORD> g_expectedSaves;  // the replay being played, from its SAVE
size_t g_nextSave;
ReplayHxSyncStatus g_status;

void Reason(const char *psz) {
  strncpy(g_status.szReason, psz, sizeof g_status.szReason - 1);
  g_status.szReason[sizeof g_status.szReason - 1] = 0;
}

const PortLogger Log("hyperion_replay.log");

// SHA-1 over each file's name, size and bytes, as the archives give them now; a file
// missing counts as its name alone.
void DataFingerprint(BYTE abOut[20]) {
  BCRYPT_HASH_HANDLE hHash = NULL;
  memset(abOut, 0, 20);
  if (BCryptCreateHash(BCRYPT_SHA1_ALG_HANDLE, &hHash, NULL, 0, NULL, 0, 0) != 0)
    return;
  for (const char *pszName : kDataFiles) {
    void *pData = NULL;
    DWORD cb = 0;
    BCryptHashData(hHash, reinterpret_cast<PUCHAR>(const_cast<char *>(pszName)), static_cast<ULONG>(strlen(pszName)), 0);
    if (SFileLoadFile(pszName, &pData, &cb, 0, NULL) && pData) {
      BCryptHashData(hHash, reinterpret_cast<PUCHAR>(&cb), sizeof cb, 0);
      BCryptHashData(hHash, static_cast<PUCHAR>(pData), cb, 0);
      SFileUnloadFile(pData);
    }
  }
  BCryptFinishHash(hHash, abOut, 20, 0);
  BCryptDestroyHash(hHash);
}

bool PutVersioned(FILE *f, DWORD dwId, WORD wVersion, const void *pData, DWORD dwCount, DWORD dwRecordSize) {
  Section s = {dwId, wVersion, 0, dwCount, dwRecordSize, dwCount * dwRecordSize};
  return fwrite(&s, sizeof s, 1, f) == 1 && (s.dwBytes == 0 || fwrite(pData, s.dwBytes, 1, f) == 1);
}

bool Put(FILE *f, DWORD dwId, const void *pData, DWORD dwCount, DWORD dwRecordSize) {
  return PutVersioned(f, dwId, 1, pData, dwCount, dwRecordSize);
}

}  // namespace

bool ReplayHxGame() {
  return HxRules();
}

void ReplayHxNoteGame() {
  g_noted = ReplayHxGame();
}

bool ReplayHxNoted() {
  return g_noted;
}

int ReplayHxWrite(void *pFile) {
  FILE *f = static_cast<FILE *>(pFile);
  RuleRecord rule;
  ReplayHxRuleMake(&rule);

  PlayerData aSlots[PLAYER_SLOTS_MAX];
  int nSlots = 0;
  for (int s = PLAYER_SLOTS_117; g_hxPlayers && s < PlayerSlots(); s++)
    aSlots[nSlots++] = gPlayerData[s];

  ColorRecord aColors[PLAYER_SLOTS_MAX];
  const int nColors = PlayerSlots();
  for (int s = 0; s < nColors; s++) {
    memcpy(aColors[s].abRamp, gdwTeamColorTbl_g[s], 8);
    aColors[s].bMinimap = gbMinimapColor_g[s];
  }
  RgbRecord aRgbs[PLAYER_SLOTS_MAX];
  bool bRgbs = false;
  for (int s = 0; s < nColors; s++) {
    aRgbs[s].bSet = static_cast<BYTE>(PortTeamColorGet(s, aRgbs[s].abRgb));
    if (!aRgbs[s].bSet)
      memset(aRgbs[s].abRgb, 0, 3);
    bRgbs = bRgbs || aRgbs[s].bSet;
  }

  BYTE abRaces[PLAYER_SLOTS_MAX];
  for (int s = 0; s < PLAYER_SLOTS_MAX; s++)
    abRaces[s] = static_cast<BYTE>(ModRaceOf(s));
  BYTE abLevels[PLAYER_SLOTS_MAX * AI_PROFILE_BYTES];
  AiProfilesSave(abLevels);
  BYTE abHandicaps[PLAYER_SLOTS_MAX];
  HandicapsSave(abHandicaps);
  BYTE abMutators[MUTATOR_RECORD_BYTES];
  MutatorsSave(abMutators);
  std::vector<unsigned int> rich;
  RichBasesRecord(&rich);
  const PortBenchStress stress = PortBenchStressGame();
  const StressRecord rStress = {static_cast<DWORD>(stress.nArmy), static_cast<DWORD>(stress.nPools)};
  const DWORD dwUnits = static_cast<DWORD>(PoolsUnitsToRecord());

  return fwrite(kMarker, sizeof kMarker, 1, f) == 1 && fwrite(&kFormatVersion, 4, 1, f) == 1
      && Put(f, kRule, &rule, 1, sizeof rule)
      && (!ModRacesAny() || Put(f, kRace, abRaces, PlayerSlots(), 1))
      && ((g_hxRuleOptions & HX_OPT_AI_LEVELS) == 0 || Put(f, kAiLv, abLevels, PLAYER_SLOTS_MAX, AI_PROFILE_BYTES))
      && ((g_hxRuleOptions & HX_OPT_HANDICAP) == 0 || Put(f, kHcap, abHandicaps, PLAYER_SLOTS_MAX, 1))
      && ((g_hxRuleOptions & HX_OPT_MUTATORS) == 0 || Put(f, kMuts, abMutators, 1, MUTATOR_RECORD_BYTES))
      && (!RichBasesInGame() || Put(f, kRich, rich.empty() ? NULL : &rich[0], static_cast<DWORD>(rich.size()), 4))
      && ((g_hxRuleOptions & HX_OPT_STRESS) == 0 || Put(f, kStrs, &rStress, 1, sizeof rStress))
      && (dwUnits == 0 || Put(f, kUnts, &dwUnits, 1, 4))
      && Put(f, kSlot, aSlots, nSlots, sizeof(PlayerData))
      && Put(f, kColr, aColors, nColors, sizeof(ColorRecord))
      && (!bRgbs || Put(f, kRgbs, aRgbs, nColors, sizeof(RgbRecord)))
      // a benchmark's replay has no checks: its local slot plays as a computer, as
      // a watched skirmish's does
      && (PortBenchMaking() || PortWatchMaking() ||
          PutVersioned(f, kSync, SYNC_HASH_VERSION, g_recorded.empty() ? NULL : &g_recorded[0],
                       static_cast<DWORD>(g_recorded.size()), sizeof(SyncHash)))
      && (g_saves.empty() || PortBenchMaking() || PortWatchMaking() ||
          Put(f, kSave, &g_saves[0], static_cast<DWORD>(g_saves.size()), sizeof(DWORD)));
}

int ReplayHxRead(void *pFile) {
  FILE *f = static_cast<FILE *>(pFile);
  char marker[sizeof kMarker];
  DWORD dwVersion;
  g_haveSlots = g_haveColors = g_haveRgbs = false;
  ModRacesRecorded(NULL, 0);
  AiProfilesRecorded(NULL, 0);
  HandicapsRecorded(NULL, 0);
  MutatorsRecorded(NULL, 0);
  RichBasesRecorded(NULL);
  PortBenchStressRecorded(NULL);
  PoolsUnitsRecorded(0);
  g_expected.clear();
  g_expectedSaves.clear();
  memset(&g_status, 0, sizeof g_status);
  if (fread(marker, sizeof marker, 1, f) != 1 || memcmp(marker, kMarker, sizeof marker) != 0
      || fread(&dwVersion, 4, 1, f) != 1 || dwVersion > kFormatVersion) {
    g_status.nState = REPLAY_SYNC_REFUSED;
    Reason("not a Hyperion replay this build reads");
    Log("refused: %s", g_status.szReason);
    return 0;
  }
  bool bRule = false;
  Section s;
  while (fread(&s, sizeof s, 1, f) == 1) {
    const long next = ftell(f) + static_cast<long>(s.dwBytes);
    if (s.dwId == kRule && s.dwBytes >= offsetof(RuleRecord, dwSimRevision)) {
      RuleRecord rule = {};
      if (fread(&rule, s.dwBytes < sizeof rule ? s.dwBytes : sizeof rule, 1, f) != 1)
        return 0;
      rule.szRule[sizeof rule.szRule - 1] = 0;
      rule.szBuild[sizeof rule.szBuild - 1] = 0;
      const char *pszWhy = ReplayHxRuleRefusal(&rule, s.dwBytes);
      if (pszWhy) {
        // other rules, other data, or another simulation: it would not play the game it recorded
        g_status.nState = REPLAY_SYNC_REFUSED;
        Reason(pszWhy);
        Log("refused: %s (rule %s, revision %u, build %s; this build has revision %d)", pszWhy, rule.szRule,
            static_cast<unsigned>(rule.dwSimRevision), rule.szBuild, HX_SIM_REVISION);
        return 0;
      }
      ReplayHxRuleApply(&rule, s.dwBytes);
      bRule = true;
    } else if (s.dwId == kSlot && s.dwRecordSize == sizeof(PlayerData)
               && s.dwCount <= PLAYER_SLOTS_MAX - PLAYER_SLOTS_117) {
      memset(g_slots, 0, sizeof g_slots);
      if (s.dwCount && fread(&g_slots[PLAYER_SLOTS_117], sizeof(PlayerData), s.dwCount, f) != s.dwCount)
        return 0;
      g_haveSlots = true;
    } else if (s.dwId == kColr && s.dwRecordSize == sizeof(ColorRecord) && s.dwCount <= PLAYER_SLOTS_MAX) {
      memset(g_colors, 0, sizeof g_colors);
      if (s.dwCount && fread(g_colors, sizeof(ColorRecord), s.dwCount, f) != s.dwCount)
        return 0;
      g_haveColors = true;
    } else if (s.dwId == kRace && s.dwRecordSize == 1 && s.dwCount <= PLAYER_SLOTS_MAX) {
      BYTE abRaces[PLAYER_SLOTS_MAX];
      if (s.dwCount && fread(abRaces, 1, s.dwCount, f) != s.dwCount)
        return 0;
      ModRacesRecorded(abRaces, static_cast<int>(s.dwCount));
    } else if (s.dwId == kAiLv && s.dwRecordSize == AI_PROFILE_BYTES && s.dwCount <= PLAYER_SLOTS_MAX) {
      BYTE abLevels[PLAYER_SLOTS_MAX * AI_PROFILE_BYTES];
      if (s.dwCount && fread(abLevels, AI_PROFILE_BYTES, s.dwCount, f) != s.dwCount)
        return 0;
      AiProfilesRecorded(abLevels, static_cast<int>(s.dwCount));
    } else if (s.dwId == kHcap && s.dwRecordSize == 1 && s.dwCount <= PLAYER_SLOTS_MAX) {
      BYTE abHandicaps[PLAYER_SLOTS_MAX];
      if (s.dwCount && fread(abHandicaps, 1, s.dwCount, f) != s.dwCount)
        return 0;
      HandicapsRecorded(abHandicaps, static_cast<int>(s.dwCount));
    } else if (s.dwId == kMuts && s.dwCount == 1 && MutatorRecordBytesRead(static_cast<int>(s.dwRecordSize))) {
      BYTE abMutators[MUTATOR_RECORD_BYTES];
      if (fread(abMutators, s.dwRecordSize, 1, f) != 1)
        return 0;
      MutatorsRecorded(abMutators, static_cast<int>(s.dwRecordSize));
    } else if (s.dwId == kRich && s.dwRecordSize == 4 && s.dwCount <= POOL_UNITS_STORE) {
      std::vector<unsigned int> rich(s.dwCount);
      if (s.dwCount && fread(&rich[0], 4, s.dwCount, f) != s.dwCount)
        return 0;
      RichBasesRecorded(&rich);
    } else if (s.dwId == kStrs && s.dwRecordSize == sizeof(StressRecord) && s.dwCount == 1) {
      StressRecord r;
      if (fread(&r, sizeof r, 1, f) != 1)
        return 0;
      PortBenchStress stress;
      stress.nArmy = static_cast<int>(r.dwArmy < 0x10000 ? r.dwArmy : 0x10000);
      stress.nPools = static_cast<int>(r.dwPools < POOL_SCALE_MAX ? r.dwPools : POOL_SCALE_MAX);
      PortBenchStressRecorded(&stress);
    } else if (s.dwId == kUnts && s.dwRecordSize == 4 && s.dwCount == 1) {
      DWORD dwUnits;
      if (fread(&dwUnits, 4, 1, f) != 1)
        return 0;
      PoolsUnitsRecorded(dwUnits > POOL_UNITS_MAX ? POOL_UNITS_MAX : static_cast<int>(dwUnits));
    } else if (s.dwId == kRgbs && s.dwRecordSize == sizeof(RgbRecord) && s.dwCount <= PLAYER_SLOTS_MAX) {
      memset(g_rgbs, 0, sizeof g_rgbs);
      if (s.dwCount && fread(g_rgbs, sizeof(RgbRecord), s.dwCount, f) != s.dwCount)
        return 0;
      g_haveRgbs = true;
    } else if (s.dwId == kSync && s.wVersion == SYNC_HASH_VERSION && s.dwRecordSize == sizeof(SyncHash)) {
      g_expected.resize(s.dwCount);
      if (s.dwCount && fread(&g_expected[0], sizeof(SyncHash), s.dwCount, f) != s.dwCount)
        return 0;
    } else if (s.dwId == kSave && s.dwRecordSize == sizeof(DWORD) && s.dwCount <= 0x10000) {
      g_expectedSaves.resize(s.dwCount);
      if (s.dwCount && fread(&g_expectedSaves[0], sizeof(DWORD), s.dwCount, f) != s.dwCount)
        return 0;
    }
    if (fseek(f, next, SEEK_SET) != 0)
      return 0;
  }
  return bRule;
}

const std::vector<SyncHash> &ReplayHxExpected() {
  return g_expected;
}

void ReplayHxGameStart() {
  g_recorded.clear();
  g_saves.clear();
  g_nextExpected = 0;
  g_nextSave = 0;
  if (!gPlayback) {
    g_expected.clear();
    g_expectedSaves.clear();
    memset(&g_status, 0, sizeof g_status);
    if (ReplayHxGame())
      g_status.nState = REPLAY_SYNC_RECORDING;
    return;
  }
  g_status.nState = g_expected.empty() ? REPLAY_SYNC_NONE : REPLAY_SYNC_OK;
  g_status.dwRecorded = static_cast<uint32_t>(g_expected.size());
  if (!g_expected.empty())
    Log("playing a replay with %u checks, the last at frame %u", g_status.dwRecorded, g_expected.back().dwFrame);
}

// HYPERION_SYNC_DUMP=<frame>,<folder>: at that frame, each domain's bytes as hashed go to
// <folder>/live.* in a game and <folder>/replay.* in its replay, to find what a check that
// differs differs in (SyncHashDump; engine/docs/replay-test.md).
static void DumpAt(DWORD dwFrame) {
  static int s_nFrame = -2;
  static char s_szDir[MAX_PATH];
  if (s_nFrame == -2) {
    char szVal[MAX_PATH + 16];
    s_nFrame = -1;
    if (GetEnvironmentVariableA("HYPERION_SYNC_DUMP", szVal, sizeof szVal) && strchr(szVal, ',')) {
      s_nFrame = atoi(szVal);
      lstrcpynA(s_szDir, strchr(szVal, ',') + 1, sizeof s_szDir);
    }
  }
  if (s_nFrame < 0 || dwFrame != static_cast<DWORD>(s_nFrame))
    return;
  char szPath[MAX_PATH + 16];
  wsprintfA(szPath, "%s\\%s", s_szDir, gPlayback ? "replay" : "live");
  SyncHashDump(szPath);
}

static void Check(DWORD dwFrame);

void ReplayHxFrame() {
  const DWORD dwFrame = oc_dwReplayFrame;
  Check(dwFrame);
  LanGameFrame(dwFrame);
  if (g_status.nState != REPLAY_SYNC_NONE)
    ObserveFrameDone();
  if (!gPlayback)
    return;
  while (g_nextSave < g_expectedSaves.size() && g_expectedSaves[g_nextSave] < dwFrame)
    g_nextSave++;
  for (; g_nextSave < g_expectedSaves.size() && g_expectedSaves[g_nextSave] == dwFrame; g_nextSave++)
    if (!HxSaveReplay())
      Log("the save at frame %u could not be made again", static_cast<unsigned>(dwFrame));
}

void ReplayHxRewound(unsigned long dwFrame) {
  while (g_nextExpected > 0 && g_expected[g_nextExpected - 1].dwFrame > dwFrame)
    g_nextExpected--;
  while (g_nextSave > 0 && g_expectedSaves[g_nextSave - 1] > dwFrame)
    g_nextSave--;
  while (!g_recorded.empty() && g_recorded.back().dwFrame > dwFrame)
    g_recorded.pop_back();
  while (!g_saves.empty() && g_saves.back() > dwFrame)
    g_saves.pop_back();
  g_status.dwRecorded = static_cast<uint32_t>(g_recorded.size());
}

void ReplayHxSaved() {
  if (!gPlayback && g_status.nState == REPLAY_SYNC_RECORDING)
    g_saves.push_back(oc_dwReplayFrame);
}

static void Check(DWORD dwFrame) {
  DumpAt(dwFrame);
  if (!gPlayback) {
    if (g_status.nState == REPLAY_SYNC_RECORDING && dwFrame % kSyncEvery == 0) {
      SyncHash h;
      SyncHashCompute(&h);
      g_recorded.push_back(h);
      g_status.dwRecorded = static_cast<uint32_t>(g_recorded.size());
    }
    return;
  }
  while (g_nextExpected < g_expected.size() && g_expected[g_nextExpected].dwFrame < dwFrame)
    g_nextExpected++;  // a frame not reached: playback skipped it, which leaves its check undone
  if (g_nextExpected >= g_expected.size() || g_expected[g_nextExpected].dwFrame != dwFrame)
    return;
  const SyncHash &want = g_expected[g_nextExpected++];
  SyncHash h;
  SyncHashCompute(&h);
  DWORD dwDomains = 0;
  for (int d = 0; d < SYNC_DOMAINS; d++)
    if (h.aqDomain[d] != want.aqDomain[d])
      dwDomains |= 1u << d;
  if (h.dwRandState != want.dwRandState || h.dwRandCount != want.dwRandCount)
    dwDomains |= REPLAY_SYNC_RAND;
  g_status.dwChecked++;
  if (dwDomains && g_status.nState != REPLAY_SYNC_MISMATCH) {
    g_status.nState = REPLAY_SYNC_MISMATCH;
    g_status.dwFirstBad = dwFrame;
    g_status.dwBadDomains = dwDomains;
    Log("differs from the game it recorded at frame %u (domains %#x: units 1, objects 2, players 4, "
        "map 8, sprites 16, ai 32, random state 0x100)", static_cast<unsigned>(dwFrame),
        static_cast<unsigned>(dwDomains));
  }
  if (g_nextExpected == g_expected.size())
    Log("checked %u of %u: %s", g_status.dwChecked, g_status.dwRecorded,
        g_status.nState == REPLAY_SYNC_MISMATCH ? "differs" : "the game it recorded");
}

const ReplayHxSyncStatus *ReplayHxSync() {
  return &g_status;
}

void ReplayHxAssignSlots() {
  if (!gPlayback || !g_haveSlots || !g_hxPlayers)
    return;
  for (int s = PLAYER_SLOTS_117; s < PLAYER_SLOTS_MAX; s++)
    gPlayerData[s] = g_slots[s];
}

void ReplayHxColors() {
  for (int s = 0; gPlayback && g_haveRgbs && s < PlayerSlots(); s++)
    if (g_rgbs[s].bSet)
      PortTeamColorSet(s, g_rgbs[s].abRgb[0], g_rgbs[s].abRgb[1], g_rgbs[s].abRgb[2], FALSE);
  if (!gPlayback || !g_haveColors)
    return;
  // every slot's, the first eight's too: a colour chosen in the set-up is the game's, past
  // the one 1.17's colour map gives the slot (PlayersChosenColoursApply)
  for (int s = 0; s < PlayerSlots(); s++) {
    if (s == PLAYER_NEUTRAL)
      continue;
    memcpy(gdwTeamColorTbl_g[s], g_colors[s].abRamp, 8);
    gbMinimapColor_g[s] = g_colors[s].bMinimap;
  }
}

void ReplayHxForget() {
  g_haveSlots = g_haveColors = g_haveRgbs = false;
  RulesRecordedClassic();
}

// The rule record a save of a Hyperion game carries as well (port_hxsave.cpp).
unsigned ReplayHxRuleSize() {
  return sizeof(RuleRecord);
}

bool ReplayHxRuleSizeKnown(unsigned cb) {
  return cb == sizeof(RuleRecord) || cb == kRuleSizeNoDepends || cb == kRuleSize11;
}

void ReplayHxRuleMake(void *pOut) {
  RuleRecord rule = {};
  strncpy(rule.szRule, HxRules() ? HX_RULE_HYPERION : HX_RULE_CLASSIC, sizeof rule.szRule - 1);
  DataFingerprint(rule.abData);
  strncpy(rule.szBuild, HYPERION_VERSION_TEXT, sizeof rule.szBuild - 1);
  rule.dwSimRevision = HX_SIM_REVISION;
  rule.dwOptions = g_hxRuleOptions;
  if (g_hxModGame) {
    strncpy(rule.szMod, ModDataId(), sizeof rule.szMod - 1);
    memcpy(rule.abMod, ModDataFingerprint(), sizeof rule.abMod);
    strncpy(rule.szDepends, ModDataDepends(), sizeof rule.szDepends - 1);
  }
  memcpy(pOut, &rule, sizeof rule);
}

static RuleRecord RuleRead(const void *pRule, unsigned cbRule) {
  RuleRecord rule = {};
  memcpy(&rule, pRule, cbRule < sizeof rule ? cbRule : sizeof rule);
  rule.szRule[sizeof rule.szRule - 1] = 0;
  rule.szBuild[sizeof rule.szBuild - 1] = 0;
  rule.szMod[sizeof rule.szMod - 1] = 0;
  rule.szDepends[sizeof rule.szDepends - 1] = 0;
  return rule;
}

const char *ReplayHxRuleRefusal(const void *pRule, unsigned cbRule, const unsigned char *pabData) {
  const RuleRecord rule = RuleRead(pRule, cbRule);
  BYTE abData[20];
  if (pabData != NULL)
    memcpy(abData, pabData, sizeof abData);
  else
    DataFingerprint(abData);
  if (strcmp(rule.szRule, HX_RULE_CLASSIC) != 0 && strcmp(rule.szRule, HX_RULE_HYPERION) != 0)
    return "rules this build does not have";
  if (memcmp(rule.abData, abData, sizeof abData) != 0)
    return "game data other than this build's";
  if (rule.dwSimRevision != HX_SIM_REVISION)
    return "another revision of Hyperion's simulation";
  if ((rule.dwOptions & ~HX_OPT_KNOWN) != 0)
    return "options this build does not have";
  // A game with a mod's types plays back or loads only with that mod's data, as it was
  //: the mod is chosen as Hyperion starts, with --mod or the "mod" setting.
  static char s_szWhy[256];
  if (rule.szMod[0] != 0 && strcmp(rule.szMod, ModDataId()) != 0) {
    snprintf(s_szWhy, sizeof s_szWhy, "the mod %s's data, which this run did not load (--mod %s)",
             rule.szMod, rule.szMod);
    return s_szWhy;
  }
  if (rule.szMod[0] != 0 && memcmp(rule.abMod, ModDataFingerprint(), sizeof rule.abMod) != 0) {
    // "checkModFingerprint" off: played all the same, for a mod's author trying a change
    // against an old game, which may go its own way from the frame the changed data matters
    double dCheck = 1.0;
    if (PortSettingNumber("hyperion", "checkModFingerprint", &dCheck) && dCheck == 0.0) {
      Log("played with another version of the mod %s's data: the fingerprint check is off", rule.szMod);
      return NULL;
    }
    // the mods under it, when they differ, name what to install (an older record
    // names none)
    if (rule.szDepends[0] != 0 && strcmp(rule.szDepends, ModDataDepends()) != 0)
      snprintf(s_szWhy, sizeof s_szWhy, "the mod %s with %s, where this run has %s", rule.szMod, rule.szDepends,
               ModDataDepends()[0] != 0 ? ModDataDepends() : "none of them");
    else
      snprintf(s_szWhy, sizeof s_szWhy, "another version of the mod %s's data", rule.szMod);
    return s_szWhy;
  }
  return NULL;
}

// A block compress wrote, passed over: its check, its count of pieces, and each piece's
// length and bytes (compress.cpp).
static bool SkipBlock(FILE *f) {
  DWORD adw[2];
  if (fread(adw, 4, 2, f) != 2)
    return false;
  for (DWORD i = 0; i < adw[1]; ++i) {
    DWORD cb;
    if (fread(&cb, 4, 1, f) != 1 || fseek(f, static_cast<long>(cb), SEEK_CUR) != 0)
      return false;
  }
  return true;
}

// The file as LoadReplayData reads it: the magic, the header, the command record's length and
// its commands, the map's length and the map, then a Hyperion replay's sections, of which
// this keeps what a list shows. The game data's fingerprint is taken once for each of the
// two games.
bool ReplayHxPeek(const char *pszPath, ReplayHxInfo *pOut, std::vector<unsigned char> *pChk) {
  *pOut = ReplayHxInfo();
  if (pChk != NULL)
    pChk->clear();
  void *pFile = pfopen(pszPath, 1);
  if (pFile == NULL)
    return false;
  FILE *f = static_cast<FILE *>(pFile);
  DWORD dwMagic = 0, dwCommands = 0, dwMap = 0;
  bool bOk = decompress(&dwMagic, 4, pFile) && (dwMagic == 0x53526572 || dwMagic == REPLAY_MAGIC_HX)
             && decompress(pOut->abHead, sizeof pOut->abHead, pFile) && pOut->abHead[0x46] != 0;
  pOut->bHx = dwMagic == REPLAY_MAGIC_HX;
  if (bOk && (pOut->bHx || pChk != NULL)) {
    bOk = decompress(&dwCommands, 4, pFile) && (dwCommands == 0 || SkipBlock(f)) && decompress(&dwMap, 4, pFile);
    if (bOk && pChk != NULL && dwMap > 0 && dwMap < 0x4000000) {
      pChk->resize(dwMap);
      bOk = decompress(pChk->data(), static_cast<int>(dwMap), pFile) != 0;
      if (!bOk)
        pChk->clear();
    } else if (bOk) {
      bOk = SkipBlock(f);
    }
  }
  char marker[sizeof kMarker];
  DWORD dwVersion = 0;
  if (bOk && pOut->bHx) {
    if (fread(marker, sizeof marker, 1, f) != 1 || memcmp(marker, kMarker, sizeof marker) != 0
        || fread(&dwVersion, 4, 1, f) != 1 || dwVersion > kFormatVersion) {
      pOut->refusal = "not a Hyperion replay this build reads";
    } else {
      Section s;
      while (fread(&s, sizeof s, 1, f) == 1) {
        const long next = ftell(f) + static_cast<long>(s.dwBytes);
        if (s.dwId == kRule && s.dwBytes >= offsetof(RuleRecord, dwSimRevision)) {
          BYTE ab[sizeof(RuleRecord)] = {};
          const unsigned cb = s.dwBytes < sizeof ab ? s.dwBytes : static_cast<unsigned>(sizeof ab);
          if (fread(ab, cb, 1, f) != 1)
            break;
          const RuleRecord rule = RuleRead(ab, cb);
          pOut->bRule = true;
          memcpy(pOut->szRule, rule.szRule, sizeof pOut->szRule);
          memcpy(pOut->szBuild, rule.szBuild, sizeof pOut->szBuild);
          memcpy(pOut->szMod, rule.szMod, sizeof pOut->szMod);
          memcpy(pOut->szDepends, rule.szDepends, sizeof pOut->szDepends);
          pOut->dwSimRevision = rule.dwSimRevision;
          pOut->dwOptions = rule.dwOptions;
          static BYTE s_abData[2][20];
          static bool s_bData[2];
          const int nGame = (pOut->abHead[0] != 0) == (gbIsExpansion != 0) ? 0 : 1;
          if (nGame == 0 && !s_bData[gbIsExpansion != 0]) {
            DataFingerprint(s_abData[gbIsExpansion != 0]);
            s_bData[gbIsExpansion != 0] = true;
          }
          // a replay of the other game is checked against its data when it plays
          const char *pszWhy = nGame == 0 ? ReplayHxRuleRefusal(ab, cb, s_abData[gbIsExpansion != 0]) : NULL;
          if (nGame != 0 && strcmp(rule.szRule, HX_RULE_CLASSIC) != 0 && strcmp(rule.szRule, HX_RULE_HYPERION) != 0)
            pszWhy = "rules this build does not have";
          else if (nGame != 0 && rule.dwSimRevision != HX_SIM_REVISION)
            pszWhy = "another revision of Hyperion's simulation";
          pOut->refusal = pszWhy != NULL ? pszWhy : "";
        } else if (s.dwId == kSlot && s.dwRecordSize == sizeof(PlayerData)
                   && s.dwCount <= PLAYER_SLOTS_MAX - PLAYER_SLOTS_117) {
          pOut->past.resize(s.dwCount * sizeof(PlayerData));
          if (s.dwCount && fread(pOut->past.data(), sizeof(PlayerData), s.dwCount, f) != s.dwCount)
            pOut->past.clear();
        } else if (s.dwId == kRace && s.dwRecordSize == 1 && s.dwCount <= PLAYER_SLOTS_MAX) {
          pOut->races.resize(s.dwCount);
          if (s.dwCount && fread(pOut->races.data(), 1, s.dwCount, f) != s.dwCount)
            pOut->races.clear();
        } else if (s.dwId == kSync) {
          pOut->dwChecks = s.dwCount;
        }
        if (fseek(f, next, SEEK_SET) != 0)
          break;
      }
      if (!pOut->bRule && pOut->refusal.empty())
        pOut->refusal = "no rule record";
    }
  }
  pfclose(pFile);
  return bOk;
}

void ReplayHxRuleApply(const void *pRule, unsigned cbRule) {
  const RuleRecord rule = RuleRead(pRule, cbRule);
  RulesRecorded(strcmp(rule.szRule, HX_RULE_HYPERION) == 0, rule.dwOptions, rule.szMod[0] != 0);
}

// Settings, "Save a replay of every game" ("keepReplays", on unless set off): 1.17 writes
// each game's replay as LastReplay, which the next game writes over; this keeps a copy of
// it in maps\replays\AutoSave, named by when the game ended and its map.
void PortReplayKeep(const char *pszGameMap) {
  if (PortBenchMaking() || PortWatchMaking())
    return;  // a benchmark's is kept in its run's folder (port_bench.cpp), a watch's when it is over
  double dKeep = 1.0;
  if (PortSettingNumber("hyperion", "keepReplays", &dKeep) && dKeep == 0.0)
    return;
  char szDir[MAX_PATH];
  szDir[0] = 0;
  SFileGetUserDataPath(szDir, sizeof szDir, 1);
  char szFrom[MAX_PATH];
  snprintf(szFrom, sizeof szFrom, "%smaps\\replays\\LastReplay.rep", szDir);
  if (GetFileAttributesA(szFrom) == INVALID_FILE_ATTRIBUTES)
    return;
  strncat(szDir, "maps\\replays\\AutoSave\\", sizeof szDir - strlen(szDir) - 1);
  CreateDirectoryA(szDir, NULL);
  // the map's file name without its folder and extension, and nothing a file name cannot hold
  char szMap[64] = "game";
  if (const char *pszMap = pszGameMap != NULL ? pszGameMap : static_cast<const char *>(GetCurrentMap(1))) {
    const char *pszName = strrchr(pszMap, '\\');
    pszName = pszName != NULL ? pszName + 1 : pszMap;
    size_t n = 0;
    for (; pszName[n] != 0 && pszName[n] != '.' && n + 1 < sizeof szMap; ++n)
      szMap[n] = strchr("<>:\"/\\|?*", pszName[n]) != NULL ? '_' : pszName[n];
    if (n > 0)
      szMap[n] = 0;
  }
  SYSTEMTIME now;
  GetLocalTime(&now);
  char szTo[MAX_PATH];
  snprintf(szTo, sizeof szTo, "%s%04u-%02u-%02u %02u.%02u.%02u %s.rep", szDir, now.wYear, now.wMonth, now.wDay,
           now.wHour, now.wMinute, now.wSecond, szMap);
  if (!CopyFileA(szFrom, szTo, TRUE))
    Log("keeping the replay as %s failed (error %u)", szTo, static_cast<unsigned>(GetLastError()));
}
