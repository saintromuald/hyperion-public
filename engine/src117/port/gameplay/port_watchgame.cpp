// A skirmish watched (port_watchgame.h): the made game, its replay and the watch.
#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "port_watchgame.h"
#include "port_launch.h"                 // PortLaunchWatchReplay
#include "port_teams.h"                  // TeamsSidesLeft
#include "CUnit.h"
#include "../hud/port_hudstats.h"        // PortHudStatsSet
#include "../platform/port_paths.h"      // PortDataFolder
#include "../tools/port_bench.h"         // PortBenchPatchLocal
#include "../front/port_frontscreens.h"  // FrontLog
#include "../../h/hxrules.h"             // HX_OPT_TEAMS
#include "../../h/gametype.h"            // GameHeader
#include "../../h/gds_file.h"            // pfopen, pfclose, pfdelete
#include "../../h/saveload.h"            // compress, decompress, pfwrite
#include "../../h/victory.h"             // do_victory_dlg
#include "../../h/port_hxreplay.h"       // PortReplayKeep
#include "../../h/map_load.h"            // GetCurrentMap
#include "../../h/net_cmds.h"            // gameloop_init, PlayerData
#include "../../h/playerlimits.h"        // PlayerSlots, PLAYER_NEUTRAL
#include "../../h/replay.h"              // replay_set_speed, order_replay_play, replay_dlg_elapsed_seconds
#include "../../gen/globals117.h"        // gPlayerData
#include "../platform/port_globals117.h"

extern GameHeader gGameHeader;  // 0x6728D0 (gametype.cpp)

#ifdef OC_DLL_MODE
#define sgbReplayPaused_g (*reinterpret_cast<int *>(OC_VA(0x67CC7Cu)))
#else
extern int sgbReplayPaused_g;  // the replay is paused (replay.cpp)
#endif

namespace {

enum { PHASE_NONE, PHASE_MAKING, PHASE_TO_WATCH, PHASE_WATCHING, PHASE_ENDED };
int g_nPhase = PHASE_NONE;
int g_nSpeed = 6, g_nMul = 1;
bool g_bEnding;          // the made game's end asked for
bool g_bStarted;         // the watch's first game frame passed, its speed set again
int g_nSidesAtStart;     // the sides when the watch began; with one, it never ends by itself
std::string g_replay;    // the made game's replay, which the watch plays
std::string g_map;       // the made game's map, which names the watch's replay in AutoSave
std::string g_result;    // who won and when, for the end's box
DWORD g_dwReached;       // the last frame the watch reached

// The replay's length while it is watched: as long as 1.17 counts, so that its end of a replay
// (its pause and dialog) never comes; the watch holds when one side is left.
const DWORD kOpenLength = 0x7FFFFFF0u;

// A replay's header, the second of its compressed blocks (REPLAY_HDR_BLOB, replay.cpp); its
// length in frames is at 1, after the expansion's byte.
const int kHeaderSize = 0x279;

// The end of the compressed block at `at` (compress.cpp): its CRC, its count of chunks, and each
// chunk's size and bytes; 0 when the file ends inside it.
size_t BlockEnd(const std::vector<BYTE> &bytes, size_t at)
{
  DWORD dwChunks = 0;
  if (at + 8 > bytes.size())
    return 0;
  memcpy(&dwChunks, &bytes[at + 4], 4);
  at += 8;
  for (DWORD i = 0; i < dwChunks; ++i) {
    DWORD dwSize = 0;
    if (at + 4 > bytes.size())
      return 0;
    memcpy(&dwSize, &bytes[at], 4);
    at += 4 + static_cast<size_t>(dwSize);
    if (at > bytes.size())
      return 0;
  }
  return at;
}

// The watched replay written to pszTo with dwFrames as its length, so it plays as any game's
// replay does: the first block's bytes, the header compressed again, and the rest as it was.
bool WriteWithLength(const char *pszFrom, const char *pszTo, DWORD dwFrames)
{
  std::vector<BYTE> bytes;
  if (FILE *pIn = fopen(pszFrom, "rb")) {
    BYTE ab[0x8000];
    size_t n;
    while ((n = fread(ab, 1, sizeof ab, pIn)) > 0)
      bytes.insert(bytes.end(), ab, ab + n);
    fclose(pIn);
  }
  const size_t nHeaderAt = BlockEnd(bytes, 0);
  const size_t nRestAt = nHeaderAt != 0 ? BlockEnd(bytes, nHeaderAt) : 0;
  if (nRestAt == 0)
    return false;
  void *pFile = pfopen(pszFrom, 1);
  if (pFile == NULL)
    return false;
  DWORD dwMagic = 0;
  BYTE abHeader[kHeaderSize];
  const bool bRead = decompress(&dwMagic, 4, pFile) && decompress(abHeader, kHeaderSize, pFile);
  pfclose(pFile);
  if (!bRead)
    return false;
  memcpy(abHeader + 1, &dwFrames, 4);
  pFile = pfopen(pszTo, 0x17);
  if (pFile == NULL)
    return false;
  const bool bOk = pfwrite(&bytes[0], static_cast<int>(nHeaderAt), pFile) &&
                   compress(abHeader, kHeaderSize, pFile) &&
                   (nRestAt == bytes.size() ||
                    pfwrite(&bytes[nRestAt], static_cast<int>(bytes.size() - nRestAt), pFile));
  pfclose(pFile);
  if (!bOk)
    pfdelete(const_cast<char *>(pszTo), FALSE);
  return bOk;
}

// The last side's players with units, by name: "Elite Guard", "A and B".
std::string Winners(int nLast)
{
  bool abHas[256] = {};
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
    abHas[pUnit->bPlayer] = true;
  const bool bTeams = (g_hxRuleOptions & HX_OPT_TEAMS) != 0 || gGameHeader.bNumTeams != 0;
  const int nTeam = bTeams && nLast < PlayerSlots() ? gPlayerData[nLast].nTeam : 0;
  std::string names;
  int nNames = 0;
  for (int i = 0; i < PlayerSlots(); ++i) {
    if (!abHas[i] || i == PLAYER_NEUTRAL || (i != nLast && (nTeam == 0 || gPlayerData[i].nTeam != nTeam)))
      continue;
    names += std::string(nNames == 0 ? "" : ", ") + gPlayerData[i].szName;
    ++nNames;
  }
  if (nNames > 1) {
    const size_t at = names.rfind(", ");
    names.replace(at, 2, " and ");
  }
  return names;
}

void End(int nLast)
{
  g_nPhase = PHASE_ENDED;
  if (sgbReplayPaused_g == 0)
    order_replay_play();  // the replay's own Pause
  // the time as the replay's readout counts it, at the recorded speed
  const DWORD dwSeconds = static_cast<DWORD>(replay_dlg_elapsed_seconds());
  char sz[160];
  if (nLast >= 0) {
    const std::string names = Winners(nLast);
    snprintf(sz, sizeof sz, "%s %s at %lu:%02lu.", names.c_str(), names.find(" and ") != std::string::npos ? "win" : "wins",
             static_cast<unsigned long>(dwSeconds / 60), static_cast<unsigned long>(dwSeconds % 60));
  } else {
    snprintf(sz, sizeof sz, "No one is left at %lu:%02lu.", static_cast<unsigned long>(dwSeconds / 60),
             static_cast<unsigned long>(dwSeconds % 60));
  }
  g_result = sz;
  // the standings at the side; over them the game menu's end box says who won, and asks to leave
  // or to watch on (PortGameEndKind, VictDlg.cpp), as a game's victory asks
  PortHudStatsSet(HUD_STATS_SIDE, true);
  do_victory_dlg();
  FrontKit::FrontLog("observe: the game ended at frame %lu: %s\n", static_cast<unsigned long>(oc_dwReplayFrame), sz);
}

}  // namespace

void PortWatchBegin(int nSpeed, int nMul)
{
  g_nPhase = PHASE_MAKING;
  g_nSpeed = nSpeed >= 0 && nSpeed <= 6 ? nSpeed : 6;
  g_nMul = nMul == 2 || nMul == 4 || nMul == 8 || nMul == 16 ? nMul : 1;
  g_bEnding = false;
  g_nSidesAtStart = 0;
  g_result.clear();
  g_dwReached = 0;
  FrontKit::FrontLog("observe: the game to watch is made, speed %d x%d\n", g_nSpeed, g_nMul);
}

bool PortWatchMaking(void)
{
  return g_nPhase == PHASE_MAKING;
}

void PortWatchPatchHeader(void *pHeader, unsigned cbHeader)
{
  PortBenchPatchLocal(pHeader, cbHeader, kOpenLength);
}

void PortWatchFrame(void)
{
  if (g_nPhase == PHASE_MAKING && !gPlayback && oc_dwReplayFrame >= 1) {
    if (!g_bEnding) {
      g_bEnding = true;
      gameloop_init(4);  // to the menus, as Quit Mission leaves; free_game writes the replay
    }
    return;
  }
  if ((g_nPhase != PHASE_WATCHING && g_nPhase != PHASE_ENDED) || !gPlayback || oc_dwReplayFrame == 0)
    return;
  g_dwReached = oc_dwReplayFrame;
  if (g_nPhase == PHASE_ENDED)
    return;
  // the speed again: the game's start sets 1.17's own after PortWatchReplayStarts, as a
  // benchmark's run finds (port_bench.cpp)
  if (!g_bStarted) {
    g_bStarted = true;
    PortWatchReplayStarts();
  }
  if (oc_dwReplayFrame % 24 != 0)
    return;
  int nLast = -1;
  const int nSides = TeamsSidesLeft(&nLast);
  if (g_nSidesAtStart == 0)
    g_nSidesAtStart = nSides;
  if (g_nSidesAtStart >= 2 && nSides <= 1)
    End(nLast);
}

void PortWatchAfterGame(void)
{
  char szLast[MAX_PATH];
  snprintf(szLast, sizeof szLast, "%smaps\\replays\\LastReplay.rep", PortDataFolder());
  if (g_nPhase == PHASE_MAKING) {
    // write_replay put it in LastReplay, its length open; the watch plays it from the cache, and
    // LastReplay is written again when the watch is over
    char szDir[MAX_PATH];
    snprintf(szDir, sizeof szDir, "%scache", PortDataFolder());
    CreateDirectoryA(szDir, NULL);
    g_replay = std::string(szDir) + "\\watch.rep";
    if (!MoveFileExA(szLast, g_replay.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
      FrontKit::FrontLog("observe: the made game's replay %s could not be moved (%lu)\n", szLast, GetLastError());
      g_nPhase = PHASE_NONE;
      return;
    }
    const char *pszMap = static_cast<const char *>(GetCurrentMap(1));
    g_map = pszMap != NULL ? pszMap : "";
    g_nPhase = PHASE_TO_WATCH;
    return;
  }
  if (g_nPhase == PHASE_WATCHING || g_nPhase == PHASE_ENDED) {
    // LastReplay: the match as far as it was watched, its length the last frame reached, kept
    // in AutoSave as any game's replay is
    if (g_dwReached > 0 && WriteWithLength(g_replay.c_str(), szLast, g_dwReached)) {
      PortReplayKeep(!g_map.empty() ? g_map.c_str() : NULL);
      FrontKit::FrontLog("observe: the watch is over at frame %lu, its replay in LastReplay\n",
                         static_cast<unsigned long>(g_dwReached));
    } else {
      FrontKit::FrontLog("observe: the watch is over at frame %lu, no replay written\n",
                         static_cast<unsigned long>(g_dwReached));
    }
    g_nPhase = PHASE_NONE;
  }
}

int PortWatchLaunch(void)
{
  if (g_nPhase != PHASE_TO_WATCH)
    return FALSE;
  g_nPhase = PHASE_WATCHING;
  g_bStarted = false;
  if (!PortLaunchWatchReplay(g_replay.c_str())) {
    FrontKit::FrontLog("observe: the replay %s did not start\n", g_replay.c_str());
    g_nPhase = PHASE_NONE;
    return FALSE;
  }
  FrontKit::FrontLog("observe: watching %s\n", g_replay.c_str());
  return TRUE;
}

void PortWatchReplayStarts(void)
{
  if (g_nPhase != PHASE_WATCHING || !gPlayback)
    return;
  sgbShowAll_g = 1;  // the replay panel's Show All
  replay_set_speed(static_cast<DWORD>(g_nSpeed), static_cast<DWORD>(g_nMul), 0);
}

bool PortWatchLive(void)
{
  return (g_nPhase == PHASE_WATCHING || g_nPhase == PHASE_ENDED) && gPlayback != 0;
}

const char *PortWatchResult(void)
{
  return g_result.c_str();
}
