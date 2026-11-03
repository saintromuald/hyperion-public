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
#include <vector>

#include "port_pluto.h"
#include "port_ai.h"  // AiProfileOf, AI_LEVEL_PLUTO
#include "../../h/CUnit.h"
#include "../../h/net_cmds.h"      // the net command structs, the receivers, sub_477E00
#include "../../h/playerlimits.h"  // PLAYER_SLOTS_MAX, PlayerLobbySlots
#include "../../h/playervision.h"  // SpriteSeenBy, VisionHxTileSeen, VisionHxTileExplored
#include "../../gen/globals117.h"  // gPlayerData, gPlayerMinerals, gPlayerGas, gwMapWidth, gStartLocations
#include "../platform/port_log.h"  // PortLogger
#include "../platform/port_paths.h"  // PortDataFolder

// select_insert lives in net_cmds.cpp's own prototype.
int __fastcall select_insert(CUnit *pUnit, DWORD dwHumanID, int nCount);

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

// The Pluto slot this game plays (one for v1).
int s_nPlutoSlot = -1;

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
  while (cb) {
    DWORD n = 0;
    if (!WriteFile(h, p, static_cast<DWORD>(cb > 0x7FFFFFFF ? 0x7FFFFFFF : cb), &n,
                   NULL) ||
        n == 0)
      return false;
    p += n;
    cb -= n;
  }
  return true;
}

bool ReadAll(HANDLE h, void *pv, size_t cb) {
  char *p = static_cast<char *>(pv);
  while (cb) {
    DWORD n = 0;
    if (!ReadFile(h, p, static_cast<DWORD>(cb > 0x7FFFFFFF ? 0x7FFFFFFF : cb), &n,
                  NULL) ||
        n == 0)
      return false;
    p += n;
    cb -= n;
  }
  return true;
}

// One frame over to the bridge: the header, the units, the players.
bool PlutoSendFrame() {
  struct Unit {
    unsigned id, type, player, x, y, hp, shields, energy, order;
    unsigned char visible, completed, pad[2];
  };
  struct Player {
    unsigned race, minerals, gas, supplyTotal, supplyUsed;
    unsigned startX, startY;
  };
  // The units: the used list, in its order (the bot's ids follow it).
  s_nFrame = 0;
  std::vector<Unit> units;
  units.reserve(256);
  for (CUnit *u = CUnit::GetUsedList(); u != NULL && s_nFrame < kFrameUnitsMax;
       u = u->pNext) {
    s_aFrame[s_nFrame] = u;
    ++s_nFrame;
    Unit x;
    memset(&x, 0, sizeof x);
    x.id = static_cast<unsigned>(s_nFrame - 1);
    x.type = u->wType;
    x.player = u->bPlayer;
    x.x = u->wPositionX;
    x.y = u->wPositionY;
    x.hp = static_cast<unsigned>(u->lHitPoints / 256);
    x.shields = u->lShields / 256;
    x.energy = u->wEnergy / 256;
    x.order = static_cast<unsigned>(u->bOrderId);
    // The bot plays with fog-of-war, as its training had: a unit is visible
    // when its sprite's visibility flag for the Pluto slot is set.
    x.visible = (u->pSprite != NULL && SpriteSeenBy(u->pSprite, s_nPlutoSlot)) ? 1 : 0;
    x.completed = 1;
    units.push_back(x);
  }
  // The players: the lobby slots, the Pluto slot first as the bridge's player 0.
  // (The bridge's own player is 0; the others follow in slot order.)
  const int nLobby = PlayerLobbySlots();
  std::vector<Player> players;
  players.reserve(nLobby + 1);
  auto addPlayer = [&](int s) {
    Player p;
    memset(&p, 0, sizeof p);
    p.race = gPlayerData[s].nRace;
    p.minerals = static_cast<unsigned>(gPlayerMinerals[s]);
    p.gas = static_cast<unsigned>(gPlayerGas[s]);
    p.supplyTotal = 200;  // v1: the supply lookup is not wired yet
    p.supplyUsed = 0;
    p.startX = gStartLocations[s].x;
    p.startY = gStartLocations[s].y;
    players.push_back(p);
  };
  addPlayer(s_nPlutoSlot);
  for (int s = 0; s < nLobby; ++s)
    if (s != s_nPlutoSlot)
      addPlayer(s);

  const unsigned nUnits = static_cast<unsigned>(units.size());
  const unsigned nPlayers = static_cast<unsigned>(players.size());
  unsigned hdr[3] = {0x50465848, s_dwFrame, nUnits};  // 'HXPF'
  unsigned mapWH[2] = {gwMapWidth, gwMapHeight};
  // The fog planes: one bit a tile, in the map's row order, for the Pluto slot
  // (seen = not hidden from it; explored = its explored fog).
  const unsigned nTiles = static_cast<unsigned>(gwMapWidth) * gwMapHeight;
  const unsigned nPlaneBytes = (nTiles + 7) / 8;
  std::vector<unsigned char> seen(nPlaneBytes, 0), explored(nPlaneBytes, 0);
  for (unsigned t = 0; t < nTiles; ++t) {
    if (VisionHxTileSeen(static_cast<int>(t), s_nPlutoSlot))
      seen[t >> 3] |= static_cast<unsigned char>(1u << (t & 7));
    if (VisionHxTileExplored(static_cast<int>(t), s_nPlutoSlot))
      explored[t >> 3] |= static_cast<unsigned char>(1u << (t & 7));
  }
  static FILE *s_dump = NULL;
  static int s_dumpCount = 0;
  if (getenv("HYPERION_PLUTO_DUMP") != NULL) {
    if (s_dump == NULL)
      s_dump = fopen((PlutoFolder() + "/frames.bin").c_str(), "wb");
    if (s_dump != NULL) {
      fwrite(hdr, sizeof hdr, 1, s_dump);
      fwrite(mapWH, sizeof mapWH, 1, s_dump);
      fwrite(&nPlayers, sizeof nPlayers, 1, s_dump);
      fwrite(units.data(), nUnits * sizeof(Unit), 1, s_dump);
      fwrite(players.data(), nPlayers * sizeof(Player), 1, s_dump);
      fwrite(seen.data(), nPlaneBytes, 1, s_dump);
      fwrite(explored.data(), nPlaneBytes, 1, s_dump);
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
      !WriteAll(s_hToBridge, players.data(), nPlayers * sizeof(Player)) ||
      !WriteAll(s_hToBridge, seen.data(), nPlaneBytes) ||
      !WriteAll(s_hToBridge, explored.data(), nPlaneBytes))
    return false;
  return true;
}

// The bridge's reply: the commands, applied to the Pluto slot.
bool PlutoReadAndApply() {
  unsigned out[4] = {0, 0, 0, 0};
  for (;;) {
    if (!ReadAll(s_hFromBridge, out, 12))
      return false;
    if (out[0] == 0x50525848)  // 'HXRP'
      break;
    // Wine's noise before the first reply: skip to the magic.
    unsigned char rest[12];
    memcpy(rest, out, 12);
    // resync: read a byte at a time until the magic's first byte
    unsigned char c = rest[11];
    for (;;) {
      // find 'H' = 0x48 from the tail; read more until the magic lines up
      unsigned char b = 0;
      if (!ReadAll(s_hFromBridge, &b, 1))
        return false;
      memmove(rest, rest + 1, 11);
      rest[11] = b;
      if (memcmp(rest, "\x48\x58\x52\x50", 4) == 0) {
        memcpy(out, rest, 4);
        if (!ReadAll(s_hFromBridge, out + 1, 8))
          return false;
        break;
      }
      (void)c;
    }
    break;
  }
  const unsigned nCmd = out[2] < 0x10000 ? out[2] : 0;
  std::vector<unsigned char> cmd(nCmd);
  unsigned win = 0;
  if (!ReadAll(s_hFromBridge, cmd.data(), nCmd) || !ReadAll(s_hFromBridge, &win, 4))
    return false;

  // The commands, through the receivers, as the Pluto slot.
  sub_477E00(static_cast<BYTE>(s_nPlutoSlot));
  const unsigned char *p = cmd.data();
  const unsigned char *pEnd = p + nCmd;
  while (pEnd - p >= 2) {
    const unsigned char op = p[0];
    if (op == 0x09 || op == 0x0A) {  // Select / SelectDeltaAdd
      if (pEnd - p < 2 + p[1] * 2)
        break;
      const int count = p[1];
      for (int i = 0; i < count; ++i) {
        const unsigned id = static_cast<unsigned>(p[2 + i * 2]) |
                            (static_cast<unsigned>(p[3 + i * 2]) << 8);
        const int idx = static_cast<int>(id) - kPlutoIdBase;
        if (idx < 0 || idx >= s_nFrame)
          continue;
        CUnit *pu = s_aFrame[idx];
        if (pu != NULL)
          select_insert(pu, g_ActiveHumanID, i);
      }
      p += 2 + count * 2;
    } else if (op == 0x14) {  // RightClick
      if (pEnd - p < 10)
        break;
      net_cmd_rclick_t c;
      memset(&c, 0, sizeof c);
      c.bOpcode = p[0];
      c.wX = static_cast<WORD>(p[1] | (p[2] << 8));
      c.wY = static_cast<WORD>(p[3] | (p[4] << 8));
      c.wTargetID = static_cast<WORD>(p[5] | (p[6] << 8));
      c.wTargetType = static_cast<WORD>(p[7] | (p[8] << 8));
      c.bQueued = p[9];
      recv_net_cmd_RightClick(&c);
      p += 10;
    } else if (op == 0x15) {  // Action
      if (pEnd - p < 12)
        break;
      net_cmd_action_t c;
      memset(&c, 0, sizeof c);
      c.bOpcode = p[0];
      c.wX = static_cast<WORD>(p[1] | (p[2] << 8));
      c.wY = static_cast<WORD>(p[3] | (p[4] << 8));
      c.wTargetID = static_cast<WORD>(p[5] | (p[6] << 8));
      c.wTargetType = static_cast<WORD>(p[7] | (p[8] << 8));
      c.bOrderID = p[9];
      c.bQueued = p[10];
      recv_net_cmd_Action(&c);
      p += 12;
    } else {
      break;  // unknown; stop rather than desync
    }
  }
  return true;
}

}  // namespace

void PlutoGameStart() {
  s_bUp = false;
  s_bFailed = false;
  s_dwFrame = 0;
  s_nFrame = 0;
  s_hProc = NULL;
  s_hToBridge = NULL;
  s_hFromBridge = NULL;
  s_nPlutoSlot = FindPlutoSlot();
  if (s_nPlutoSlot < 0)
    return;
  PlutoStart();
  if (s_bUp)
    Log("Pluto: slot %d plays Pluto", s_nPlutoSlot);
}

void PlutoFrame() {
  if (s_nPlutoSlot < 0 || !s_bUp)
    return;
  // The bridge's frame 0 is the fork's MatchStart: it runs the bot's onStart
  // then. The first frame must therefore be 0 (sending 1 first left the fork's
  // game unstarted -- it loaded and idled while the game played on).
  if (s_dwFrame < 3 || s_dwFrame % 600 == 0)
    Log("Pluto: sending frame %u", s_dwFrame);
  if (!PlutoSendFrame()) {  // sends s_dwFrame
    Log("Pluto: frame %u did not send", s_dwFrame);
    return;
  }
  ++s_dwFrame;
  if (!PlutoReadAndApply()) {
    Log("Pluto: the reply at frame %u did not arrive", s_dwFrame - 1);
    return;
  }
  if (s_dwFrame < 4)
    Log("Pluto: frame %u and back", s_dwFrame - 1);
}

void PlutoGameEnd() {
  if (!s_bUp)
    return;
  if (s_hToBridge != NULL) {
    CloseHandle(s_hToBridge);  // the bridge's stdin closes; it exits
    s_hToBridge = NULL;
  }
  if (s_hFromBridge != NULL) {
    CloseHandle(s_hFromBridge);
    s_hFromBridge = NULL;
  }
  if (s_hProc != NULL) {
    TerminateProcess(s_hProc, 0);
    CloseHandle(s_hProc);
    s_hProc = NULL;
  }
  s_bUp = false;
  Log("Pluto: the bridge closed");
}
