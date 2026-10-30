// port_discord.cpp -- Hyperion in Discord's "Playing": what the player is doing, told
// to the Discord app on this computer as its Rich Presence, so a friend sees "Playing Hyperion"
// with "Skirmish · Lost Temple" and "1 vs 3 computers" under it and the time since it began.
//
// Discord's app listens on a local pipe, \\.\pipe\discord-ipc-0 to -9 on Windows and a Unix
// socket of the same names under $XDG_RUNTIME_DIR (or the temporary folder) elsewhere, Flatpak's
// and Snap's folders too. A frame is an opcode and a length, little-endian, then JSON: a
// handshake naming the application (Discord's Developer Portal, "Hyperion", the user's, its id
// below), then SET_ACTIVITY each time what the player does changes. Discord answers each, and
// pings; a close frame or a broken pipe ends the connection, tried again every 15 seconds. No
// library: Discord's own SDKs are larger than this file and do the same over the same pipe.
//
// What it says comes from what is loaded, never from a list of mods: the game's own modes
// (skirmish, campaign, a replay, a LAN room and its games, watching), and for a mod whose home
// screen starts a mode of its own, the mod's name from its mod.json; the picture's tip names
// the mod or game loaded (the user, 2026-10-26: "Have it read the mod based on whats loaded").
//
// A thread of its own talks to the pipe, so a Discord that is slow or gone never holds a frame.
// The main thread describes the activity once a second (Describe) and the thread sends it when
// it changes, at most one in 4 seconds (Discord allows 5 in 20). Presentation only: nothing
// here is read by the game. Settings' "Show in Discord" (`discord`, on by default)
// turns it off, and the line is cleared at once.
#include "port_discord.h"

#if defined(__EMSCRIPTEN__)

// in a browser there is no Discord app to tell
void PortDiscordPump(void) {}
void PortDiscordStop(void) {}
std::string PortDiscordGameText(void) { return std::string(); }

#else

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "port_json.h"      // PortJsonQuote
#include "port_log.h"       // PortLogger
#include "port_paths.h"     // PortCommandOptions
#include "port_settings.h"  // PortSettingNumber
#include "port_util.h"      // PortNowMs
#include "oc_data.h"        // OC_VA, where 1.17's data is
#include "../../gen/globals117.h"     // gwGameMode, gPlayback, gbMultiPlayerMode, gPlayerData
#include "../../h/hxrules.h"          // g_hxRuleOptions, HX_OPT_TEAMS, RulesCampaignMission
#include "../../h/net_cmds.h"         // PlayerData
#include "../../h/playerlimits.h"     // PlayerLobbySlots, PlayerIsLobby
#include "../../h/Teams.h"            // IsTeamGame
#include "../data/port_mods.h"        // PortMods, PortModActive
#include "../front/port_front.h"      // PortFrontActive, PortFrontScreen, PortFrontSkirmishGame
#include "../front/port_text.h"
#include "../front/port_frontscreens.h"  // FrontKit::SkirmishPlayed
#include "../gameplay/port_watchgame.h"  // PortWatchLive
#include "../lab/port_labworker.h"    // PortLabWorker
#include "../lan/port_lanclient.h"    // LanClientIn, LanClientRoom, LanClientYou
#include "../lan/port_lanfeed.h"      // PortLanWatchLive
#include "../lan/port_lanlobby.h"     // PortLanLobbyActive
#include "../render/port_maptext.h"   // MapTextUtf8

namespace {

const PortLogger Log("hyperion_discord.log");

// The Discord application "Hyperion" (the user's, made 2026-10-26), whose name Discord shows after
// "Playing", and the key of its art in the Developer Portal (Rich Presence, Art Assets).
const char kClientId[] = "1564423796356685854";
const char kLargeImage[] = "hyperion";

enum { OP_HANDSHAKE = 0, OP_FRAME = 1, OP_CLOSE = 2, OP_PING = 3, OP_PONG = 4 };

const uint64_t kRetryMs = 15000;  // between tries while Discord is not running
const uint64_t kSendGapMs = 4000; // between two SET_ACTIVITY

// What the main thread wants shown, as SET_ACTIVITY's activity ("" for none), and the thread's
// state; the pipe is the thread's alone. Never freed and the thread detached, so a process that
// ends without PortDiscordStop (1.17's exits, ExitProcess) ends as it would without them.
struct Shared {
  std::mutex lock;
  std::condition_variable wake;
  std::string wanted;
  bool bStarted = false;
  bool bStop = false;
  bool bDone = false;
};

Shared &S(void)
{
  static Shared *p = new Shared;
  return *p;
}

// ---------------------------------------------------------------------------
// The pipe, Windows' or a Unix socket.

#if defined(_WIN32)

HANDLE g_hPipe = INVALID_HANDLE_VALUE;

bool PipeOpen(void)
{
  for (int i = 0; i < 10; ++i) {
    char szName[32];
    snprintf(szName, sizeof szName, "\\\\.\\pipe\\discord-ipc-%d", i);
    g_hPipe = CreateFileA(szName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hPipe != INVALID_HANDLE_VALUE)
      return true;
  }
  return false;
}

void PipeClose(void)
{
  if (g_hPipe != INVALID_HANDLE_VALUE)
    CloseHandle(g_hPipe);
  g_hPipe = INVALID_HANDLE_VALUE;
}

bool PipeWrite(const void *p, size_t cb)
{
  const char *pc = static_cast<const char *>(p);
  while (cb > 0) {
    DWORD dwDone = 0;
    if (!WriteFile(g_hPipe, pc, static_cast<DWORD>(cb), &dwDone, NULL) || dwDone == 0)
      return false;
    pc += dwDone;
    cb -= dwDone;
  }
  return true;
}

// The bytes waiting, -1 when the pipe is broken.
int PipeWaiting(void)
{
  DWORD dwAvail = 0;
  if (!PeekNamedPipe(g_hPipe, NULL, 0, NULL, &dwAvail, NULL))
    return -1;
  return static_cast<int>(dwAvail);
}

bool PipeRead(void *p, size_t cb)
{
  char *pc = static_cast<char *>(p);
  while (cb > 0) {
    DWORD dwDone = 0;
    if (!ReadFile(g_hPipe, pc, static_cast<DWORD>(cb), &dwDone, NULL) || dwDone == 0)
      return false;
    pc += dwDone;
    cb -= dwDone;
  }
  return true;
}

#else

int g_fd = -1;

bool PipeOpen(void)
{
  const char *const aVars[] = {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"};
  std::string folders[5];
  int nFolders = 0;
  for (const char *pszVar : aVars)
    if (const char *psz = getenv(pszVar))
      if (psz[0] != 0)
        folders[nFolders++] = psz;
  folders[nFolders++] = "/tmp";
  // Discord as installed, as a Flatpak, as a Snap
  const char *const aWithin[] = {"", "/app/com.discordapp.Discord", "/snap.discord"};
  for (int f = 0; f < nFolders; ++f)
    for (const char *pszWithin : aWithin)
      for (int i = 0; i < 10; ++i) {
        sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        const int n = snprintf(addr.sun_path, sizeof addr.sun_path, "%s%s/discord-ipc-%d", folders[f].c_str(),
                               pszWithin, i);
        if (n <= 0 || n >= static_cast<int>(sizeof addr.sun_path))
          continue;
        const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
          return false;
#if defined(SO_NOSIGPIPE)
        int nOne = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nOne, sizeof nOne);  // the Mac's, for a Discord gone
#endif
        if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == 0) {
          g_fd = fd;
          return true;
        }
        close(fd);
      }
  return false;
}

void PipeClose(void)
{
  if (g_fd >= 0)
    close(g_fd);
  g_fd = -1;
}

bool PipeWrite(const void *p, size_t cb)
{
  const char *pc = static_cast<const char *>(p);
  while (cb > 0) {
#if defined(MSG_NOSIGNAL)
    const ssize_t n = send(g_fd, pc, cb, MSG_NOSIGNAL);
#else
    const ssize_t n = send(g_fd, pc, cb, 0);
#endif
    if (n <= 0) {
      if (n < 0 && errno == EINTR)
        continue;
      return false;
    }
    pc += n;
    cb -= static_cast<size_t>(n);
  }
  return true;
}

int PipeWaiting(void)
{
  pollfd pfd = {g_fd, POLLIN, 0};
  const int n = poll(&pfd, 1, 0);
  if (n < 0)
    return errno == EINTR ? 0 : -1;
  if (n == 0)
    return 0;
  if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
    return -1;
  return 8;  // something waits: a frame's header at least, or the end, which the read finds
}

bool PipeRead(void *p, size_t cb)
{
  char *pc = static_cast<char *>(p);
  while (cb > 0) {
    const ssize_t n = recv(g_fd, pc, cb, 0);
    if (n <= 0) {
      if (n < 0 && errno == EINTR)
        continue;
      return false;
    }
    pc += n;
    cb -= static_cast<size_t>(n);
  }
  return true;
}

#endif

bool PipeOpened(void)
{
#if defined(_WIN32)
  return g_hPipe != INVALID_HANDLE_VALUE;
#else
  return g_fd >= 0;
#endif
}

// ---------------------------------------------------------------------------
// Frames.

bool Send(uint32_t dwOp, const std::string &json)
{
  unsigned char head[8];
  const uint32_t cb = static_cast<uint32_t>(json.size());
  for (int i = 0; i < 4; ++i) {
    head[i] = static_cast<unsigned char>(dwOp >> (8 * i));
    head[4 + i] = static_cast<unsigned char>(cb >> (8 * i));
  }
  return PipeWrite(head, sizeof head) && PipeWrite(json.data(), json.size());
}

// The next frame, waiting up to nWaitMs for one; false on none, or a broken pipe (*pbBroken).
bool Receive(uint32_t *pdwOp, std::string *pJson, int nWaitMs, bool *pbBroken)
{
  *pbBroken = false;
  const uint64_t qEnd = PortNowMs() + static_cast<uint64_t>(nWaitMs);
  for (;;) {
    const int n = PipeWaiting();
    if (n < 0) {
      *pbBroken = true;
      return false;
    }
    if (n >= 8 || (n > 0 && nWaitMs > 0))
      break;
    if (PortNowMs() >= qEnd)
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  unsigned char head[8];
  if (!PipeRead(head, sizeof head)) {
    *pbBroken = true;
    return false;
  }
  uint32_t dwOp = 0, cb = 0;
  for (int i = 0; i < 4; ++i) {
    dwOp |= static_cast<uint32_t>(head[i]) << (8 * i);
    cb |= static_cast<uint32_t>(head[4 + i]) << (8 * i);
  }
  if (cb > (1u << 20)) {  // not Discord's
    *pbBroken = true;
    return false;
  }
  pJson->assign(cb, '\0');
  if (cb > 0 && !PipeRead(&(*pJson)[0], cb)) {
    *pbBroken = true;
    return false;
  }
  *pdwOp = dwOp;
  return true;
}

void Disconnect(const char *pszWhy)
{
  if (!PipeOpened())
    return;
  PipeClose();
  Log("Discord: %s", pszWhy);
}

// The handshake, and Discord's READY to it.
bool Connect(void)
{
  if (!PipeOpen())
    return false;
  if (!Send(OP_HANDSHAKE, std::string("{\"v\":1,\"client_id\":\"") + kClientId + "\"}")) {
    Disconnect("the handshake could not be sent");
    return false;
  }
  uint32_t dwOp = 0;
  std::string json;
  bool bBroken = false;
  if (!Receive(&dwOp, &json, 5000, &bBroken)) {
    Disconnect(bBroken ? "the pipe closed at the handshake" : "no answer to the handshake");
    return false;
  }
  if (dwOp != OP_FRAME || json.find("\"READY\"") == std::string::npos) {
    Log("Discord refused the handshake: %s", json.substr(0, 300));
    Disconnect("closed");
    return false;
  }
  Log("Discord: connected");
  return true;
}

bool SetActivity(const std::string &activity)
{
  static unsigned s_nNonce;
#if defined(_WIN32)
  const unsigned long nPid = GetCurrentProcessId();
#else
  const unsigned long nPid = static_cast<unsigned long>(getpid());
#endif
  char szHead[96];
  snprintf(szHead, sizeof szHead, "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%u\",\"args\":{\"pid\":%lu", ++s_nNonce, nPid);
  std::string json = szHead;
  if (!activity.empty())
    json += ",\"activity\":" + activity;
  json += "}}";
  return Send(OP_FRAME, json);
}

// Discord's answers and pings, read so the pipe never fills; false when it has closed.
bool Drain(void)
{
  for (;;) {
    uint32_t dwOp = 0;
    std::string json;
    bool bBroken = false;
    if (!Receive(&dwOp, &json, 0, &bBroken))
      return !bBroken;
    if (dwOp == OP_PING) {
      if (!Send(OP_PONG, json))
        return false;
    } else if (dwOp == OP_CLOSE) {
      Log("Discord closed the connection: %s", json.substr(0, 300));
      return false;
    } else if (json.find("\"evt\":\"ERROR\"") != std::string::npos) {
      Log("Discord: %s", json.substr(0, 300));
    }
  }
}

void Thread(void)
{
  std::string sent;
  bool bSentAny = false;
  uint64_t qTried = 0, qSent = 0;
  bool bSaidAbsent = false;
  Shared &s = S();
  std::unique_lock<std::mutex> lock(s.lock);
  while (!s.bStop) {
    const std::string wanted = s.wanted;
    lock.unlock();
    const uint64_t qNow = PortNowMs();
    if (!PipeOpened() && !wanted.empty() && (qTried == 0 || qNow - qTried >= kRetryMs)) {
      qTried = qNow;
      if (Connect()) {
        bSentAny = false;
        bSaidAbsent = false;
      } else if (!bSaidAbsent) {
        Log("Discord is not running, or takes no connection; trying every %d seconds", static_cast<int>(kRetryMs / 1000));
        bSaidAbsent = true;
      }
    }
    if (PipeOpened()) {
      if (!Drain()) {
        Disconnect("the connection ended");
      } else if ((!bSentAny || wanted != sent) && (!bSentAny || qNow - qSent >= kSendGapMs)) {
        if (SetActivity(wanted)) {
          if (!bSentAny || wanted != sent)
            Log("shown: %s", wanted.empty() ? "nothing" : wanted);
          sent = wanted;
          bSentAny = true;
          qSent = qNow;
        } else {
          Disconnect("the activity could not be sent");
        }
      }
    }
    lock.lock();
    s.wake.wait_for(lock, std::chrono::milliseconds(1000));
  }
  lock.unlock();
  if (PipeOpened()) {
    SetActivity("");
    Drain();
    PipeClose();
  }
  lock.lock();
  s.bDone = true;
  s.wake.notify_all();
}

// ---------------------------------------------------------------------------
// What the player is doing.

struct Activity {
  std::string details;  // the first line, "Skirmish · Lost Temple"
  std::string state;    // the second, "1 vs 3 computers"
  std::string picture;  // the picture's tip: "Hyperion · " and the game or mod loaded
  std::string key;      // what it is, so its clock starts again only when that changes
  int nPartySize = 0;   // a LAN room's players and its seats, 0 for none
  int nPartyMax = 0;
};

Activity Describe(void);  // below the thread's part, where the game's headers are

std::string ActivityJson(const Activity &a, long long nStart)
{
  std::string s = "{\"details\":" + PortJsonQuote(a.details);
  if (!a.state.empty())
    s += ",\"state\":" + PortJsonQuote(a.state);
  if (nStart > 0)
    s += ",\"timestamps\":{\"start\":" + std::to_string(nStart) + "}";
  s += std::string(",\"assets\":{\"large_image\":\"") + kLargeImage +
       "\",\"large_text\":" + PortJsonQuote(a.picture.empty() ? "Hyperion" : a.picture) + "}";
  if (a.nPartyMax > 0)
    s += ",\"party\":{\"id\":\"hyperion-room\",\"size\":[" + std::to_string(a.nPartySize) + "," +
         std::to_string(a.nPartyMax) + "]}";
  s += ",\"instance\":false}";
  return s;
}

// A copy no player sees, or a test's: the LAN room's own process and copy, a browser player's
// seat on the host, an AI lab worker, a run under the replay harness or a test's window
// (HYPERION_WINDOW, hyperion_quiet.flag), or HYPERION_DISCORD=0; HYPERION_DISCORD=1 shows a
// test's window all the same, to check the line. Read once.
bool Hidden(void)
{
  static int s_n = -1;
  if (s_n < 0) {
    const PortOptions *o = PortCommandOptions();
    const char *pszSay = getenv("HYPERION_DISCORD");
    const char *pszWindow = getenv("HYPERION_WINDOW");
    FILE *f = fopen("hyperion_quiet.flag", "rb");
    const bool bTest = (pszWindow && pszWindow[0]) || f != NULL;
    s_n = o->bRoom || o->szLanCopy[0] || o->szLanStream[0] || o->szLanTab[0] || PortLabWorker() ||
          (pszSay && pszSay[0] == '0') || (bTest && !(pszSay && pszSay[0] == '1'));
    if (f)
      fclose(f);
  }
  return s_n != 0;
}

bool Wanted(void)
{
  if (Hidden())
    return false;
  double d = 1.0;
  PortSettingNumber("hyperion", "discord", &d);
  return d != 0.0;
}

}  // namespace

void PortDiscordPump(void)
{
  static uint64_t s_qLast;
  static std::string s_key;
  static long long s_nStart;
  const uint64_t qNow = PortNowMs();
  if (s_qLast != 0 && qNow - s_qLast < 1000)
    return;
  s_qLast = qNow;
  std::string json;
  if (Wanted()) {
    const Activity a = Describe();
    if (a.key != s_key || s_nStart == 0) {
      s_key = a.key;
      s_nStart = static_cast<long long>(time(NULL));
    }
    json = ActivityJson(a, s_nStart);
  }
  Shared &s = S();
  std::lock_guard<std::mutex> guard(s.lock);
  if (s.bStop)
    return;
  if (!s.bStarted) {
    if (json.empty())
      return;  // nothing to show: no thread, no pipe
    s.bStarted = true;
    std::thread(Thread).detach();
  }
  if (json != s.wanted) {
    s.wanted = json;
    s.wake.notify_one();
  }
}

void PortDiscordStop(void)
{
  Shared &s = S();
  std::unique_lock<std::mutex> lock(s.lock);
  if (!s.bStarted || s.bStop)
    return;
  s.bStop = true;
  s.wanted.clear();
  s.wake.notify_all();
  // the line cleared, a second at the most: a Discord that does not answer keeps no one waiting
  s.wake.wait_for(lock, std::chrono::milliseconds(1000), [&] { return s.bDone; });
}

// ---------------------------------------------------------------------------
// Describe: the game's state, read on the main thread.

namespace {

// The map's title as the game has it, 31 bytes at the most and without its colour codes.
std::string MapTitle(void)
{
  char sz[0x21];
  memcpy(sz, reinterpret_cast<const char *>(OC_VA(0x527120u)), 0x20);
  sz[0x20] = 0;
  return MapTextUtf8(sz);
}

// The players of the game running: "1 vs 3 computers", "2 vs 2", "4 players, 2 computers". With
// bLocal the local player is one of them (a skirmish, a LAN game), else the game is watched.
std::string PlayersLine(bool bLocal)
{
  int nHumans = 0, nComputers = 0;
  int aTeam[64] = {};
  int nTeams = 0;
  const bool bTeams = (g_hxRuleOptions & HX_OPT_TEAMS) != 0 || IsTeamGame();
  for (int s = 0; s < PlayerLobbySlots(); ++s) {
    if (!PlayerIsLobby(static_cast<unsigned>(s)))
      continue;
    const int nType = gPlayerData[s].nType;
    const bool bComputer = nType == 1 || nType == 11;
    const bool bHuman = nType == 2 || nType == 10;
    if (!bComputer && !bHuman)
      continue;
    (bComputer ? nComputers : nHumans)++;
    const int t = gPlayerData[s].nTeam;
    if (bTeams && t >= 0 && t < 64) {
      if (aTeam[t]++ == 0)
        ++nTeams;
    }
  }
  if (bTeams && nTeams >= 2) {
    std::string s;
    for (int t = 0; t < 64; ++t)
      if (aTeam[t] > 0)
        s += (s.empty() ? "" : " vs ") + std::to_string(aTeam[t]);
    return s;
  }
  if (bLocal && nHumans == 1 && nComputers > 0)
    return PortTextNF("1 vs {0} computer", "1 vs {0} computers", nComputers, {std::to_string(nComputers)});
  if (nComputers == 0)
    return nHumans == 2 ? std::string("1 vs 1")
                        : PortTextNF("{0} player", "{0} players", nHumans, {std::to_string(nHumans)});
  if (nHumans == 0)
    return PortTextNF("{0} computer", "{0} computers", nComputers, {std::to_string(nComputers)});
  // {0} the people playing, {1} the computers
  return PortTextF("{0}, {1}", {PortTextNF("{0} player", "{0} players", nHumans, {std::to_string(nHumans)}),
                                PortTextNF("{0} computer", "{0} computers", nComputers, {std::to_string(nComputers)})});
}

std::string Dot(const std::string &a, const std::string &b)
{
  return b.empty() ? a : a + " \xC2\xB7 " + b;
}

// A LAN room this copy is in: its players, and the seats of the match the player is in.
void RoomParty(Activity *pa)
{
  const PortJson &room = LanClientRoom();
  const int nYou = LanClientYou();
  int nMatch = -1;
  const PortJson &devices = room["devices"];
  for (size_t i = 0; i < devices.items.size(); ++i)
    if (static_cast<int>(devices.items[i]["id"].Num(-1)) == nYou)
      nMatch = static_cast<int>(devices.items[i]["match"].Num(-1));
  const PortJson &matches = room["matches"];
  for (size_t i = 0; i < matches.items.size(); ++i) {
    const PortJson &m = matches.items[i];
    if (nMatch < 0 || static_cast<int>(m["id"].Num(-2)) != nMatch)
      continue;
    int nTaken = 0;
    const PortJson &seats = m["seats"];
    for (size_t k = 0; k < seats.items.size(); ++k) {
      const std::string owner = seats.items[k]["owner"].Str();
      if (owner == "player" || owner == "computer")
        ++nTaken;
    }
    const int nMax = static_cast<int>(m["mapPlayers"].Num(0));
    if (nMax > 0 && nTaken > 0 && nTaken <= nMax) {
      pa->nPartySize = nTaken;
      pa->nPartyMax = nMax;
    }
    pa->state = m["mapName"].Str();
    return;
  }
  const int nPeople = static_cast<int>(room["people"].Num(0));
  if (nPeople > 0)
    pa->state = PortTextNF("{0} person", "{0} people", nPeople, {std::to_string(nPeople)});
}

// The mod loaded, as its mod.json names it, and whether its home screen starts a mode of its
// own (a tool's: anything but the campaign the built-in games start). No mod is named here: a
// player has the mods they installed, and each says what it is.
const PortMod &Loaded(void)
{
  return PortMods()[PortModActive()];
}

bool OwnMode(const PortMod &m)
{
  return !m.bBuiltIn && !m.mode.empty() && m.mode != "campaign";
}

Activity InGame(void)
{
  Activity a;
  const PortMod &mod = Loaded();
  if (OwnMode(mod)) {
    // a game a mod's own mode started: the mod's name and the map
    a.details = Dot(mod.name, MapTitle());
    a.state = PlayersLine(!gPlayback);
    a.key = "mod:" + mod.id;
  } else if (PortLanWatchLive()) {
    a.details = Dot(PortText("Watching a LAN game"), MapTitle());
    a.state = PlayersLine(false);
    a.key = "lan-watch";
  } else if (PortWatchLive()) {
    a.details = Dot(PortText("Watching a skirmish"), MapTitle());
    a.state = PlayersLine(false);
    a.key = "watch";
  } else if (gPlayback) {
    a.details = Dot(PortText("Watching a replay"), MapTitle());
    a.state = PlayersLine(false);
    a.key = "replay";
  } else if (gbMultiPlayerMode) {
    a.details = Dot(PortText("LAN game"), MapTitle());
    a.state = PlayersLine(true);
    a.key = "lan";
  } else if (RulesCampaignMission() != 0) {
    a.details = Dot(PortText("Campaign"), MapTitle());
    a.key = "campaign";
  } else {
    std::string title;
    if (PortFrontSkirmishGame()) {
      std::string type;
      std::vector<std::string> teams;
      FrontKit::SkirmishPlayed(&title, &type, &teams);
    }
    a.details = Dot(PortText("Skirmish"), title.empty() ? MapTitle() : title);
    a.state = PlayersLine(true);
    a.key = "skirmish";
  }
  a.key += "|" + a.details;
  return a;
}

Activity InMenus(void)
{
  Activity a;
  const PortMod &mod = Loaded();
  a.details = PortText("In the menus");
  if (LanClientIn()) {
    a.details = PortText("In a LAN room");
    RoomParty(&a);
  } else if (PortLanLobbyActive()) {
    a.details = PortText("Starting a LAN game");
  } else if (PortFrontActive()) {
    // the game's own screens; any other is the loaded mod's, named as the mod names itself
    const std::string screen = PortFrontScreen();
    if (screen == "skirmish")
      a.details = PortText("Setting up a skirmish");
    else if (screen == "campaign")
      a.details = PortText("Choosing a campaign");
    else if (screen == "replays")
      a.details = PortText("Choosing a replay");
    else if (screen == "multiplayer")
      a.details = PortText("Looking for a LAN room");
    else if (screen == "score")
      a.details = PortText("Looking at the score");
    else if (screen != "menu" && !mod.bBuiltIn)
      a.details = mod.name;
  }
  // the clock runs from entering the menus, not from each screen
  a.key = LanClientIn() ? "room" : "menus";
  return a;
}

Activity Describe(void)
{
  Activity a = gwGameMode == 3 ? InGame() : InMenus();
  a.picture = "Hyperion \xC2\xB7 " + Loaded().name;  // "Brood War", or the mod loaded
  return a;
}

}  // namespace

std::string PortDiscordGameText(void)
{
  // for the crash report, in English
  const PortTextEnglish english;
  const Activity a = InGame();
  return a.state.empty() ? a.details : a.details + ", " + a.state;
}

#endif
