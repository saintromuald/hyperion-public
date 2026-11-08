// port_pluto.cpp -- Pluto AI: the bridge client (docs/pluto-ai.md,
// docs/pluto-protocol.md).
//
// The bridge runs Pluto's real brain under Wine: it emulates the StarCraft
// 1.16.1 memory the bot reads, filled from one HxFrame a game frame that
// Hyperion sends, and answers one HxReply with the bot's commands -- the classic
// net commands (0x09 Select, 0x14 RightClick, 0x15 Action). Hyperion applies
// them through the same receivers a replay's commands go through, with the
// active nation switched to the Pluto slot; the selection rows are per-human,
// so the player's own selection is untouched.
//
// The bot's unit ids are its own counter plus the unit's place in the first
// frame's list (the rig measured the base, 9363). Hyperion sends the units in
// the used list's order every frame and keeps that list, so a command's id maps
// back by id - kPlutoIdBase.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>

#include "port_pluto.h"
#include "port_ai.h"  // AiProfileOf, AI_LEVEL_PLUTO
#include "../../h/CUnit.h"
#include "../../h/count.h"        // count_get_supplies_used/available
#include "../../h/net_cmds.h"      // the net command structs and receivers
#include "../../h/playerlimits.h"  // PLAYER_SLOTS_MAX, PlayerLobbySlots
#include "../../h/playervision.h"  // SpriteSeenBy, VisionHxTileSeen, VisionHxTileExplored
#include "../../h/playertables.h"  // sgbAlliances_g
#include "../../h/gametype.h"      // gGameHeader
#include <fcntl.h>
#include <sys/stat.h>
#if defined(_MSC_VER)
// Native Windows cannot create the POSIX FIFO used by the Wine bridge. Keep
// the client compilable there; mkfifo fails cleanly before any bridge starts.
#include <errno.h>
#include <io.h>
#define open _open
#define close _close
#define write _write
#define unlink _unlink
#ifndef O_RDWR
#define O_RDWR _O_RDWR
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
static int mkfifo(const char *, int) {
  errno = ENOSYS;
  return -1;
}
#else
#include <unistd.h>
#endif
#include "../../gen/globals117.h"  // gPlayerData, gPlayerMinerals, gPlayerGas, gwMapWidth, gStartLocations
#include "../platform/port_log.h"  // PortLogger
#include "../platform/port_paths.h"  // PortDataFolder
#include "../tools/port_bench.h"     // PortBenchPlaying

extern GameHeader gGameHeader;

namespace {

// The bot's id base (the rig's measurement) and the frame's most units.
enum { kPlutoIdBase = 9363, kFrameUnitsMax = 1700 };

const PortLogger Log("hyperion_pluto.log");

// The bridge's process and its two pipes.
HANDLE s_hProc = NULL;
HANDLE s_hToBridge = NULL;    // write: Hyperion -> the bridge's stdin
HANDLE s_hFromBridge = NULL;  // read: the bridge's stdout -> Hyperion
bool s_bUp = false;
bool s_bFailed = false;       // tried and could not start; do not try again

// The frame's units, in the order sent (the bot's ids are kPlutoIdBase + i).
CUnit *s_aFrame[kFrameUnitsMax];
int s_nFrame = 0;
DWORD s_dwFrame = 0;

struct PendingCommand {
  DWORD dwApplyAt;
  std::vector<unsigned char> bytes;
};
std::vector<PendingCommand> s_pendingCommands;

// The Pluto slot this game plays (one for v1).
int s_nPlutoSlot = -1;
bool s_bResultKnown = false;
bool s_bPlutoWon = false;

bool IsPlutoSlot(int nSlot) {
  return nSlot >= 0 && nSlot < PLAYER_SLOTS_MAX &&
         AiProfileOf(nSlot).bLevel == AI_LEVEL_PLUTO;
}

int FindPlutoSlot() {
  for (int s = 0; s < PlayerLobbySlots(); ++s)
    if (IsPlutoSlot(s) && gPlayerData[s].nType == 1)
      return s;
  return -1;
}

// The bridge's folder: HYPERION_PLUTO_FOLDER, else Data/pluto beside the exe.
std::string PlutoFolder() {
  if (const char *psz = getenv("HYPERION_PLUTO_FOLDER"))
    return psz;
  std::string s = PortDataFolder();
  if (!s.empty() && s.back() != '/' && s.back() != '\\')
    s += '/';
  return s + "pluto";
}

// The frame FIFO: opened late, once the fork's start-up is over. Opening it
// at launch derails onStart; the console detach later would cut the pipe.
static int s_fifoFrames = -1;

// The game's paths are wine-shaped (Z:\...); the raw POSIX calls want unix ones.
static std::string UnixPath(const std::string &s) {
  std::string r = s;
  for (char &c : r)
    if (c == '\\')
      c = '/';
  if (r.size() >= 2 && r[1] == ':')
    r.erase(0, 2);
  return r;
}

// The bridge's process, once the game starts.
bool PlutoStart() {
  if (s_bUp || s_bFailed)
    return s_bUp;
  const std::string folder = PlutoFolder();
  std::string exe = folder + "/pluto-bridge.exe";
  // The wine binary: HYPERION_PLUTO_WINE, else the PATH's.
  std::string wine = "/usr/bin/wine";
  if (const char *psz = getenv("HYPERION_PLUTO_WINE"))
    wine = psz;

  // The frame transport: stdin/stdout for the first three frames (the fork's
  // start-up is sensitive to that layout), then the named FIFO, which the
  // console detach cannot take away.
  const std::string ff = UnixPath(folder) + "/frames.fifo";
  unlink(ff.c_str());
  if (mkfifo(ff.c_str(), 0666) != 0) {
    Log("Pluto: the frame FIFO was not made (%s): %s", ff.c_str(), strerror(errno));
    return false;
  }
  SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};  // inheritable
  HANDLE hToRead = NULL, hToWrite = NULL;            // the bridge's stdin ends
  HANDLE hFromRead = NULL, hFromWrite = NULL;        // the bridge's stdout ends
  if (!CreatePipe(&hToRead, &hToWrite, &sa, 0))
    return false;
  if (!CreatePipe(&hFromRead, &hFromWrite, &sa, 0)) {
    CloseHandle(hToRead);
    CloseHandle(hToWrite);
    return false;
  }
  // Hyperion keeps the other ends.
  s_hToBridge = hToWrite;
  s_hFromBridge = hFromRead;

  // The bridge's stderr rides its stdout pipe no more: the client's reply reader
  // skips it, so the bridge's startup sequence was invisible. It now goes to a
  // log file beside the bridge.
  static int s_errFd = -1;
  if (s_errFd < 0) {
    const std::string errPath = folder + "/bridge.log";
    FILE *fErr = fopen(errPath.c_str(), "wb");
    if (fErr) {
      setvbuf(fErr, NULL, _IONBF, 0);
      s_errFd = fileno(fErr);
    }
  }
  STARTUPINFOA si;
  memset(&si, 0, sizeof si);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = hToRead;
  si.hStdOutput = hFromWrite;
  si.hStdError = reinterpret_cast<HANDLE>(static_cast<intptr_t>(s_errFd));
  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof pi);
  const std::string cmd = "\"" + wine + "\" \"" + exe + "\"";
  std::vector<char> buf(cmd.begin(), cmd.end());
  buf.push_back('\0');
  if (!CreateProcessA(NULL, buf.data(), NULL, NULL, TRUE, 0, NULL, folder.c_str(),
                      &si, &pi)) {
    Log("Pluto: the bridge did not start (%s)", cmd.c_str());
    s_bFailed = true;
    CloseHandle(hToRead);
    CloseHandle(s_hToBridge);
    CloseHandle(s_hFromBridge);
    CloseHandle(hFromWrite);
    return false;
  }
  // The child's copies close; Hyperion keeps only its ends.
  CloseHandle(hToRead);
  CloseHandle(hFromWrite);
  s_hProc = pi.hProcess;
  CloseHandle(pi.hThread);
  s_bUp = true;
  Log("Pluto: the bridge runs (%s)", cmd.c_str());
  return true;
}

bool WriteAll(HANDLE h, const void *pv, size_t cb) {
  const char *p = static_cast<const char *>(pv);
  // From frame 1 the frames go to the named FIFO, not the console pipe: the
  // fork's console detach would cut that pipe a few frames into the game.
  if (s_dwFrame >= 1) {
    if (s_fifoFrames < 0) {
      const std::string ff = UnixPath(PlutoFolder()) + "/frames.fifo";
      s_fifoFrames = open(ff.c_str(), O_RDWR | O_CLOEXEC);
      if (s_fifoFrames < 0) {
        Log("Pluto: the frame FIFO did not open (%s)", ff.c_str());
        return false;
      }
    }
    while (cb) {
      const auto n = write(s_fifoFrames, p, cb);
      if (n < 0) {
        if (errno == EINTR)
          continue;
        return false;
      }
      p += n;
      cb -= static_cast<size_t>(n);
    }
    return true;
  }
  while (cb) {
    DWORD n = 0;
    if (!WriteFile(h, p, static_cast<DWORD>(cb), &n, NULL) || n == 0)
      return false;
    p += n;
    cb -= n;
  }
  return true;
}

bool ReadAll(HANDLE h, void *pv, size_t cb) {
  char *p = static_cast<char *>(pv);
  static FILE *s_rawReplyLog = NULL;
  if (s_rawReplyLog == NULL && getenv("HYPERION_PLUTO_DUMP") != NULL)
    s_rawReplyLog = fopen((PlutoFolder() + "/replies-raw.bin").c_str(), "wb");
  while (cb) {
    DWORD n = 0;
    if (!ReadFile(h, p, static_cast<DWORD>(cb), &n, NULL) || n == 0)
      return false;
    if (s_rawReplyLog != NULL) {
      fwrite(p, 1, n, s_rawReplyLog);
      fflush(s_rawReplyLog);
    }
    p += n;
    cb -= n;
  }
  return true;
}

// One frame over to the bridge: the header, the units, the players.
bool PlutoSendFrame() {
  struct Unit {
    unsigned id, type, player, x, y, hp, shields, energy, order;
    unsigned char visible, completed, direction, pad;
  };
  struct UnitExtra {
    unsigned statusFlags, orderTargetId, moveTargetId, connectedId, currentBuildUnitId;
    int speedX, speedY;
    unsigned buildType, previousType, resourceCount, orderUnitType;
    unsigned short moveTargetX, moveTargetY, orderTargetX, orderTargetY;
    unsigned short remainingBuildTime;
    unsigned char buildQueueSlot, orderState, groundCooldown, airCooldown;
    unsigned char spellCooldown, movementFlags, currentDirection, orderFlags;
    unsigned char detected, mainOrderTimer;
  };
  static_assert(sizeof(UnitExtra) == 64, "HXP3 unit extension size");
  struct Player {
    unsigned race, minerals, gas, supplyTotal, supplyUsed;
    unsigned startX, startY;
  };
  // The players are reordered below: Pluto first, the other occupied slots,
  // then neutral. Empty lobby slots must not become phantom opponents. Unit
  // player ids use this same index space.
  std::vector<int> orderedPlayers;
  orderedPlayers.push_back(s_nPlutoSlot);
  for (int s = 0; s < PlayerLobbySlots(); ++s) {
    if (orderedPlayers.size() >= 11)  // reserve player 11 for neutral
      break;
    if (s == s_nPlutoSlot)
      continue;
    if (gPlayerData[s].nType == 1 || gPlayerData[s].nType == 2)
      orderedPlayers.push_back(s);
  }
  auto mapPlayer = [&](BYTE bPlayer) -> unsigned {
    for (unsigned i = 0; i < orderedPlayers.size(); ++i)
      if (orderedPlayers[i] == bPlayer)
        return i;
    return 11;  // BWAPI's standard neutral player index
  };
  auto unitSlot = [](CUnit *u) -> unsigned {
    if (u == NULL)
      return ~0u;
    const unsigned slot = static_cast<unsigned>((u->GetUnitID() & 0x7FF) - 1);
    return slot < kFrameUnitsMax ? slot : ~0u;
  };
  // The units: the used list, with the engine's STABLE table slot as the id
  // (the bot's ids are kPlutoIdBase + that slot). The used list reorders every
  // frame, so position-based ids churned -- the bot's model saw its units
  // vanish each step and its orders targeted stale positions.
  s_nFrame = 0;
  memset(s_aFrame, 0, sizeof s_aFrame);
  std::vector<Unit> units;
  std::vector<UnitExtra> extras;
  units.reserve(256);
  extras.reserve(256);
  for (CUnit *u = CUnit::GetUsedList(); u != NULL && s_nFrame < kFrameUnitsMax;
       u = u->pNext) {
    const unsigned slot = static_cast<unsigned>((u->GetUnitID() & 0x7FF) - 1);
    if (slot >= kFrameUnitsMax)
      continue;
    s_aFrame[slot] = u;
    ++s_nFrame;
    Unit x;
    memset(&x, 0, sizeof x);
    x.id = slot;
    x.type = u->wType;
    x.player = mapPlayer(u->bPlayer);
    x.x = u->wPositionX;
    x.y = u->wPositionY;
    x.hp = static_cast<unsigned>(u->lHitPoints / 256);
    x.shields = u->lShields / 256;
    x.energy = u->wEnergy / 256;
    x.order = static_cast<unsigned>(u->bOrderId);
    // The bot plays with fog-of-war, as its training had: a unit is visible
    // when its sprite's visibility flag for the Pluto slot is set.
    x.visible = (u->bPlayer == s_nPlutoSlot ||
                 (u->pSprite != NULL && SpriteSeenBy(u->pSprite, s_nPlutoSlot))) ? 1 : 0;
    x.completed = (u->dwFlags & 1) != 0 ? 1 : 0;
    x.direction = u->bDirection;
    units.push_back(x);
    UnitExtra e;
    memset(&e, 0, sizeof e);
    e.statusFlags = u->dwFlags;
    e.orderTargetId = unitSlot(u->pOrderTarget);
    e.moveTargetId = unitSlot(u->_unmapped_14_width);
    e.connectedId = unitSlot(u->pUnion80);
    e.currentBuildUnitId = unitSlot(reinterpret_cast<CUnit *>(u->dwUnionEC));
    e.speedX = static_cast<short>(u->_unmapped_38[8] | (u->_unmapped_38[9] << 8));
    e.speedY = static_cast<short>(u->_unmapped_38[10] | (u->_unmapped_38[11] << 8));
    e.buildType = u->bBuildQueueSlot < 5 ? u->wBuildQueue[u->bBuildQueueSlot] : 0xE4;
    e.previousType = u->wPrevUnitType;
    e.resourceCount = u->wResourceAmount;
    e.orderUnitType = u->_unmapped_50[0] | (u->_unmapped_50[1] << 8);
    e.moveTargetX = u->wMoveTargetX;
    e.moveTargetY = u->wMoveTargetY;
    e.orderTargetX = static_cast<unsigned short>(u->wOrderTargetX);
    e.orderTargetY = static_cast<unsigned short>(u->wOrderTargetY);
    e.remainingBuildTime = u->wRemainingBuildTime;
    e.buildQueueSlot = u->bBuildQueueSlot;
    e.orderState = u->bOrderState;
    e.groundCooldown = u->_unmapped_55[0];
    e.airCooldown = u->_unmapped_55[1];
    e.spellCooldown = u->bUnion57;
    e.movementFlags = u->bMoveFlags;
    e.currentDirection = u->bUnion21;
    e.orderFlags = u->bOrderFlags;
    e.detected = (u->bPlayer == s_nPlutoSlot || (u->dwFlags & 0x100) == 0 ||
                  UnitDetectedBy(u, s_nPlutoSlot)) ? 1 : 0;
    e.mainOrderTimer = u->bUnion54;
    extras.push_back(e);
  }
  // The players: the lobby slots, the Pluto slot first as the bridge's player 0.
  // (The bridge's own player is 0; the others follow in slot order.)
  std::vector<Player> players;
  players.reserve(orderedPlayers.size() + 1);
  auto addPlayer = [&](int s) {
    Player p;
    memset(&p, 0, sizeof p);
    p.race = gPlayerData[s].nRace;
    p.minerals = static_cast<unsigned>(gPlayerMinerals[s]);
    p.gas = static_cast<unsigned>(gPlayerGas[s]);
    // BWAPI uses StarCraft's doubled supply values (a zergling costs 1).
    p.supplyTotal = count_get_supplies_available(static_cast<BYTE>(p.race), static_cast<BYTE>(s));
    p.supplyUsed = count_get_supplies_used(static_cast<BYTE>(p.race), static_cast<BYTE>(s));
    p.startX = gStartLocations[s].x;
    p.startY = gStartLocations[s].y;
    players.push_back(p);
  };
  for (int s : orderedPlayers)
    addPlayer(s);
  // Keep BWAPI's standard neutral player at index 11, with the two lobby
  // players at 0 and 1 and the unused seats empty.
  players.resize(12);

  const unsigned nUnits = static_cast<unsigned>(units.size());
  const unsigned nPlayers = static_cast<unsigned>(players.size());
  unsigned hdr[3] = {0x33505848, s_dwFrame, nUnits};  // 'HXP3'
  unsigned mapWH[2] = {gwMapWidth, gwMapHeight};
  // HXP3 carries real four-byte tile flags and per-unit production/combat
  // state. HXP2 remains readable for older captures.
  const unsigned nTiles = static_cast<unsigned>(gwMapWidth) * gwMapHeight;
  // The classic tile masks use engine slot numbers, but BWAPI sees Pluto as
  // player 0 and its opponent as player 1. Remap visibility/explored bits into
  // that order while preserving the walkable/buildable/terrain bits.
  std::vector<DWORD> tiles(nTiles);
  for (unsigned t = 0; t < nTiles; ++t) {
    const DWORD raw = gpSquareMap[t];
    DWORD mapped = raw & ~0xFFFFu;
    for (unsigned i = 0; i < orderedPlayers.size() && i < 8; ++i) {
      const int s = orderedPlayers[i];
      // Classic masks use one for hidden/unexplored, opposite the VisionHx
      // predicates used for extended source slots. Only players 0-7 have bits.
      const bool hidden = s < 8 ? (raw & (1u << s)) != 0
                                : !VisionHxTileSeen(static_cast<int>(t), s);
      const bool unexplored = s < 8 ? (raw & (1u << (8 + s))) != 0
                                    : !VisionHxTileExplored(static_cast<int>(t), s);
      if (hidden)
        mapped |= 1u << i;
      if (unexplored)
        mapped |= 1u << (8 + i);
    }
    tiles[t] = mapped;
  }
  static FILE *s_dump = NULL;
  static int s_dumpCount = 0;
  if (getenv("HYPERION_PLUTO_DUMP") != NULL && s_dumpCount < 60) {
    if (s_dump == NULL)
      s_dump = fopen((PlutoFolder() + "/frames.bin").c_str(), "wb");
    if (s_dump != NULL) {
      fwrite(hdr, sizeof hdr, 1, s_dump);
      fwrite(mapWH, sizeof mapWH, 1, s_dump);
      fwrite(&nPlayers, sizeof nPlayers, 1, s_dump);
      fwrite(units.data(), nUnits * sizeof(Unit), 1, s_dump);
      fwrite(extras.data(), nUnits * sizeof(UnitExtra), 1, s_dump);
      fwrite(players.data(), nPlayers * sizeof(Player), 1, s_dump);
      fwrite(tiles.data(), nTiles * sizeof(DWORD), 1, s_dump);
      ++s_dumpCount;
      if (s_dumpCount == 60) {  // enough for the offline replay
        fclose(s_dump);
        s_dump = NULL;
      }
    }
  }
  if (!WriteAll(s_hToBridge, hdr, sizeof hdr) ||
      !WriteAll(s_hToBridge, mapWH, sizeof mapWH) ||
      !WriteAll(s_hToBridge, &nPlayers, sizeof nPlayers) ||
      !WriteAll(s_hToBridge, units.data(), nUnits * sizeof(Unit)) ||
      !WriteAll(s_hToBridge, extras.data(), nUnits * sizeof(UnitExtra)) ||
      !WriteAll(s_hToBridge, players.data(), nPlayers * sizeof(Player)) ||
      !WriteAll(s_hToBridge, tiles.data(), nTiles * sizeof(DWORD)))
    return false;
  return true;
}

// The bridge's reply: the commands, applied to the Pluto slot.
bool PlutoReadAndApply() {
  unsigned out[4] = {0, 0, 0, 0};
  // Resync on magic AND the expected frame. The bridge and the fork share
  // stdout, so foreign bytes can sit between replies: a bare magic match is
  // not enough, or a noise header with a nonsense frame traps the reader.
  const unsigned expected = s_dwFrame - 1;
  for (;;) {
    if (!ReadAll(s_hFromBridge, out, 12))
      return false;
    unsigned char rest[12];
    memcpy(rest, out, 12);
    for (;;) {
      if (memcmp(rest, "\x48\x58\x52\x50", 4) == 0) {
        const unsigned frame = static_cast<unsigned>(rest[4]) |
                               (static_cast<unsigned>(rest[5]) << 8) |
                               (static_cast<unsigned>(rest[6]) << 16) |
                               (static_cast<unsigned>(rest[7]) << 24);
        const unsigned n = static_cast<unsigned>(rest[8]) |
                           (static_cast<unsigned>(rest[9]) << 8) |
                           (static_cast<unsigned>(rest[10]) << 16) |
                           (static_cast<unsigned>(rest[11]) << 24);
        if (frame == expected && n < 0x10000) {
          memcpy(out, rest, 12);
          break;
        }
      }
      unsigned char b = 0;
      if (!ReadAll(s_hFromBridge, &b, 1))
        return false;
      memmove(rest, rest + 1, 11);
      rest[11] = b;
    }
    break;
  }
  unsigned nCmd = out[2] < 0x10000 ? out[2] : 0;
  std::vector<unsigned char> cmd(nCmd);
  unsigned win = 0;
  static int s_nCmdFrames = 0;
  if (nCmd > 0 && s_nCmdFrames++ < 12)
    Log("Pluto: frame %u has %u cmd bytes", out[1], nCmd);
  if (!ReadAll(s_hFromBridge, cmd.data(), nCmd) || !ReadAll(s_hFromBridge, &win, 4))
    return false;

  // Pluto trains with a four-frame action latency. Hyperion's immediate path
  // measured one, so hold each reply for three more game frames.
  if (nCmd != 0)
    s_pendingCommands.push_back(PendingCommand{s_dwFrame + 3, std::move(cmd)});
  if (s_pendingCommands.empty() || s_pendingCommands.front().dwApplyAt > s_dwFrame)
    return true;
  cmd = std::move(s_pendingCommands.front().bytes);
  s_pendingCommands.erase(s_pendingCommands.begin());
  nCmd = static_cast<unsigned>(cmd.size());
  // The command receivers read the active nation/human for selection and
  // orders. Set those directly: sub_477E00 is the player-leave handler, not an
  // active-player setter, and calling it here declares the current player quit.
  const DWORD dwSavedNation = g_ActiveNationID;
  const DWORD dwSavedHuman = g_ActiveHumanID;
  g_ActiveNationID = static_cast<DWORD>(s_nPlutoSlot);
  g_ActiveHumanID = static_cast<DWORD>(s_nPlutoSlot);
  const unsigned char *p = cmd.data();
  const unsigned char *pEnd = p + nCmd;
  auto liveId = [](unsigned id) -> WORD {
    const int idx = static_cast<int>(id) - kPlutoIdBase;
    if (idx >= 0 && idx < kFrameUnitsMax && s_aFrame[idx] != NULL)
      return static_cast<WORD>(s_aFrame[idx]->GetUnitID());
    return static_cast<WORD>(id);
  };
  while (pEnd - p >= 2) {
    const unsigned char op = p[0];
    if (op == 0x09 || op == 0x0A || op == 0x0B) {  // Select and its deltas
      if (pEnd - p < 2 + p[1] * 2)
        break;
      const int count = p[1];
      std::vector<unsigned char> translated(p, p + 2 + count * 2);
      for (int i = 0; i < count; ++i) {
        const unsigned id = static_cast<unsigned>(p[2 + i * 2]) |
                            (static_cast<unsigned>(p[3 + i * 2]) << 8);
        const WORD live = liveId(id);
        translated[2 + i * 2] = static_cast<unsigned char>(live);
        translated[3 + i * 2] = static_cast<unsigned char>(live >> 8);
      }
      if (op == 0x09)
        recv_net_cmd_Select(reinterpret_cast<const net_cmd_select_t *>(translated.data()));
      else if (op == 0x0A)
        recv_net_cmd_SelectDeltaAdd(reinterpret_cast<const net_cmd_selectdelta_t *>(translated.data()));
      else
        recv_net_cmd_SelectDeltaDel(reinterpret_cast<const net_cmd_selectdelta_t *>(translated.data()));
      p += 2 + count * 2;
    } else if (op == 0x0C) {  // Placebox
      if (pEnd - p < 8)
        break;
      recv_net_cmd_Placebox(reinterpret_cast<const net_cmd_placebox_t *>(p));
      p += 8;
    } else if (op == 0x1A) {  // Stop
      if (pEnd - p < 2)
        break;
      recv_net_cmd_Stop(reinterpret_cast<const net_cmd_byte_t *>(p));
      p += 2;
    } else if (op == 0x2B) {  // HoldPosition
      if (pEnd - p < 2)
        break;
      recv_net_cmd_HoldPosition(reinterpret_cast<const net_cmd_byte_t *>(p));
      p += 2;
    } else if (op == 0x1F || op == 0x23 || op == 0x35) {  // Build / Morph / UpgradeBldg
      if (pEnd - p < 3)
        break;
      if (op == 0x1F)
        recv_net_cmd_Build(reinterpret_cast<const net_cmd_word_t *>(p));
      else if (op == 0x23)
        recv_net_cmd_Morph(reinterpret_cast<const net_cmd_word_t *>(p));
      else
        recv_net_cmd_UpgradeBldg(reinterpret_cast<const net_cmd_word_t *>(p));
      p += 3;
    } else if (op == 0x30 || op == 0x32 || op == 0x25 || op == 0x26 ||
               op == 0x2C || op == 0x2D || op == 0x21 || op == 0x22 ||
               op == 0x1E || op == 0x28) {
      if (pEnd - p < 2)
        break;
      if (op == 0x30)
        recv_net_cmd_Research(reinterpret_cast<const net_cmd_byte_t *>(p));
      else if (op == 0x32)
        recv_net_cmd_Upgrade(reinterpret_cast<const net_cmd_byte_t *>(p));
      else if (op == 0x25)
        recv_net_cmd_SwitchTankMode(reinterpret_cast<const net_cmd_byte_t *>(p));
      else if (op == 0x26)
        recv_net_cmd_SiegeMode(reinterpret_cast<const net_cmd_byte_t *>(p));
      else if (op == 0x2C)
        recv_net_cmd_Burrow(reinterpret_cast<const net_cmd_byte_t *>(p));
      else if (op == 0x2D)
        recv_net_cmd_Deburrow(reinterpret_cast<const net_cmd_t *>(p));
      else if (op == 0x21)
        recv_net_cmd_Cloak(reinterpret_cast<const net_cmd_t *>(p));
      else if (op == 0x22)
        recv_net_cmd_Decloak(reinterpret_cast<const net_cmd_t *>(p));
      else if (op == 0x1E)
        recv_net_cmd_Return(reinterpret_cast<const net_cmd_byte_t *>(p));
      else
        recv_net_cmd_UnloadAll(reinterpret_cast<const net_cmd_byte_t *>(p));
      p += 2;
    } else if (op == 0x20 || op == 0x29) {  // StopBuild / ExitTransport
      if (pEnd - p < 3)
        break;
      if (op == 0x20)
        recv_net_cmd_StopBuild(reinterpret_cast<const net_cmd_word_t *>(p));
      else
        recv_net_cmd_ExitTransport(reinterpret_cast<const net_cmd_word_t *>(p));
      p += 3;
    } else if (op == 0x27 || op == 0x2A || op == 0x5A || op == 0x31 ||
               op == 0x33 || op == 0x34 || op == 0x36) {
      if (op == 0x27)
        recv_net_cmd_BuildFighter();
      else if (op == 0x2A)
        recv_net_cmd_SummonArchon();
      else if (op == 0x5A)
        recv_net_cmd_SummonDArchon();
      else if (op == 0x31)
        recv_net_cmd_StopResearch(reinterpret_cast<const net_cmd_t *>(p));
      else if (op == 0x33)
        recv_net_cmd_StopUpgrade(reinterpret_cast<const net_cmd_t *>(p));
      else if (op == 0x34)
        recv_net_cmd_StopAddon(reinterpret_cast<const net_cmd_t *>(p));
      else
        recv_net_cmd_Stim();
      ++p;
    } else if (op == 0x14) {  // RightClick
      if (pEnd - p < 10)
        break;
      net_cmd_rclick_t c;
      memset(&c, 0, sizeof c);
      c.bOpcode = p[0];
      c.wX = static_cast<WORD>(p[1] | (p[2] << 8));
      c.wY = static_cast<WORD>(p[3] | (p[4] << 8));
      c.wTargetID = liveId(static_cast<unsigned>(p[5] | (p[6] << 8)));
      c.wTargetType = static_cast<WORD>(p[7] | (p[8] << 8));
      c.bQueued = p[9];
      recv_net_cmd_RightClick(&c);
      p += 10;
    } else if (op == 0x15) {  // Action: 11 bytes through the queued flag
      if (pEnd - p < 11)
        break;
      net_cmd_action_t c;
      memset(&c, 0, sizeof c);
      c.bOpcode = p[0];
      c.wX = static_cast<WORD>(p[1] | (p[2] << 8));
      c.wY = static_cast<WORD>(p[3] | (p[4] << 8));
      c.wTargetID = liveId(static_cast<unsigned>(p[5] | (p[6] << 8)));
      c.wTargetType = static_cast<WORD>(p[7] | (p[8] << 8));
      c.bOrderID = p[9];
      c.bQueued = p[10];
      recv_net_cmd_Action(&c);
      p += 11;
    } else {
      break;  // unknown; stop rather than desync
    }
  }
  g_ActiveNationID = dwSavedNation;
  g_ActiveHumanID = dwSavedHuman;
  return true;
}

}  // namespace

void PlutoGameStart() {
  // Starting a new game or the benchmark's playback must never leave the old
  // bridge alive to share BWRL_SHM and the opponent's stats file.
  PlutoGameEnd();
  s_bUp = false;
  s_bFailed = false;
  s_dwFrame = 0;
  s_nFrame = 0;
  s_pendingCommands.clear();
  s_bResultKnown = false;
  s_bPlutoWon = false;
  s_hProc = NULL;
  s_hToBridge = NULL;
  s_hFromBridge = NULL;
  s_nPlutoSlot = FindPlutoSlot();
  s_fifoFrames = -1;
  if (s_nPlutoSlot < 0)
    return;
  // Keep the AI identifiable in the lobby, players panel, replay header, and
  // benchmark summary rather than only by its AI-level label.
  snprintf(gPlayerData[s_nPlutoSlot].szName, sizeof gPlayerData[s_nPlutoSlot].szName,
           "PLuto");
  // Benchmark playback marks the viewer allied with the recorded players for
  // vision. That is useful for watching, but wrong for an AI-vs-AI melee: the
  // combat receivers turn attacks into Follow orders. Restore melee hostility
  // in both directions for the live benchmark game.
  if (gPlayback && PortBenchPlaying() && gGameHeader.bNumTeams == 0) {
    for (int i = 0; i < PlayerLobbySlots(); ++i) {
      if (gPlayerData[i].nType != 1 && gPlayerData[i].nType != 2)
        continue;
      for (int j = 0; j < PlayerLobbySlots(); ++j) {
        if (i == j || (gPlayerData[j].nType != 1 && gPlayerData[j].nType != 2))
          continue;
        sgbAlliances_g[i][j] = 0;
      }
    }
  }
  PlutoStart();
  if (s_bUp)
    Log("Pluto: slot %d plays Pluto", s_nPlutoSlot);
}

void PlutoFrame() {
  static int s_guardLogs = 0;
  if (s_guardLogs++ < 24)
    Log("Pluto: tick %u up=%d slot=%d", s_dwFrame, s_bUp ? 1 : 0, s_nPlutoSlot);
  if (s_nPlutoSlot < 0 || !s_bUp)
    return;
  // The bridge's frame 0 is the fork's MatchStart: it runs the bot's onStart
  // then. The first frame must therefore be 0 (sending 1 first left the fork's
  // game unstarted -- it loaded and idled while the game played on).
  if (s_dwFrame < 3 || s_dwFrame % 600 == 0)
    Log("Pluto: sending frame %u", s_dwFrame);
  static int s_flowLogs = 0;
  const bool flow = s_flowLogs++ < 24;
  if (flow)
    Log("Pluto: send begin %u", s_dwFrame);
  if (!PlutoSendFrame()) {  // sends s_dwFrame
    static int s_fails = 0;
    if (s_fails++ < 3)
      Log("Pluto: frame %u did not send (err %lu)", s_dwFrame, GetLastError());
    return;
  }
  if (flow)
    Log("Pluto: send done %u", s_dwFrame);
  ++s_dwFrame;
  if (flow)
    Log("Pluto: read begin %u", s_dwFrame - 1);
  if (!PlutoReadAndApply()) {
    Log("Pluto: the reply at frame %u did not arrive", s_dwFrame - 1);
    return;
  }
  if (flow)
    Log("Pluto: read done %u", s_dwFrame - 1);
  if (s_dwFrame < 4)
    Log("Pluto: frame %u and back", s_dwFrame - 1);
  if (s_dwFrame % 60 == 0) {
    int own = 0, other = 0;
    for (CUnit *u = CUnit::GetUsedList(); u != NULL; u = u->pNext) {
      if (u->bPlayer == s_nPlutoSlot)
        ++own;
      else if (u->bPlayer < PLAYER_SLOTS_MAX)
        ++other;
    }
    Log("Pluto state: frame %u minerals=%d gas=%d own_units=%d other_units=%d supply=%u/%u",
        s_dwFrame, gPlayerMinerals[s_nPlutoSlot], gPlayerGas[s_nPlutoSlot], own, other,
        count_get_supplies_used(static_cast<BYTE>(gPlayerData[s_nPlutoSlot].nRace),
                               static_cast<BYTE>(s_nPlutoSlot)),
        count_get_supplies_available(static_cast<BYTE>(gPlayerData[s_nPlutoSlot].nRace),
                                    static_cast<BYTE>(s_nPlutoSlot)));
    int shown = 0;
    for (CUnit *u = CUnit::GetUsedList(); u != NULL && shown < 8; u = u->pNext) {
      if (u->bPlayer != s_nPlutoSlot)
        continue;
      Log("Pluto unit: id=%u type=%u order=%u x=%u y=%u", u->GetUnitID(), u->wType,
          static_cast<unsigned>(u->bOrderId), u->wPositionX, u->wPositionY);
      ++shown;
    }
  }
}

void PlutoNoteResult(int nLastPlayer) {
  if (s_nPlutoSlot < 0)
    return;
  s_bResultKnown = true;
  s_bPlutoWon = nLastPlayer == s_nPlutoSlot;
}

void PlutoGameEnd() {
  if (!s_bUp)
    return;
  // Give the DLL the actual game result before closing transport. An unknown
  // result is not reported as a victory and cannot reinforce a failed build.
  if (s_bResultKnown) {
    const unsigned end[3] = {0x45505848, s_bPlutoWon ? 1u : 0u, 0};  // 'HXPE'
    WriteAll(s_hToBridge, end, sizeof end);
  }
  // Close the bridge's stdin first so it sees EOF and runs its own clean
  // shutdown (the bot's onEnd sets the engine's quit flag, and pluto_infer.exe
  // exits). Killing the bridge hard left the engine lingering, and the next
  // game's bridge then handshook the stale engine.
  if (s_fifoFrames >= 0) {
    close(s_fifoFrames);
    s_fifoFrames = -1;
  }
  if (s_hToBridge != NULL) {
    CloseHandle(s_hToBridge);
    s_hToBridge = NULL;
  }
  if (s_hFromBridge != NULL) {
    CloseHandle(s_hFromBridge);
    s_hFromBridge = NULL;
  }
  if (s_hProc != NULL) {
    if (WaitForSingleObject(s_hProc, 3000) == WAIT_TIMEOUT) {
      TerminateProcess(s_hProc, 0);  // only when the clean exit does not come
      Log("Pluto: the bridge was forced closed");
    }
    CloseHandle(s_hProc);
    s_hProc = NULL;
  }
  s_bUp = false;
  Log("Pluto: the bridge closed");
}
