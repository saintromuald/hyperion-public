// port_control.cpp -- the live channel port_control.h describes: its thread, the WebSocket
// (RFC 6455) and small HTTP server on 127.0.0.1, and the commands (docs/modules/tools.md).
//
// The channel's thread only moves bytes; each command runs on the game's thread in
// PortControlPoll. A WebSocket upgrade from a page Hyperion did not serve (any other Origin)
// is refused, so a page the player has open cannot reach the game, and so is a request naming
// any host but 127.0.0.1 or localhost, so a name rebound to this machine cannot either. An
// upgrade without the channel's key (PortControlKey) is refused too: being on this machine is
// not enough, since another user's programs and sandboxed ones are as well.
//
// kCommands names every command, what it needs and its handler, its arguments beside it. The
// sandbox commands change the game and mark it as changed; the rest change only what is
// shown, so a replay still plays the same. The map messages and the cutscenes are in full in
// docs/live-channel-for-editors.md.
//
// winsock2.h's GROUP is a name the game's headers have too (h/grp.h); winsock's is renamed here, and not used.
#define GROUP WinsockGroup
#include <winsock2.h>
#include <ws2tcpip.h>
#undef GROUP
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>  // SHGetFolderPathA, the key file's folder
#if !defined(_WIN32)
#include <sys/stat.h>  // mkdir
#endif
#include <random>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include "../gameplay/port_ai.h"         // bench_info's levels
#include "../gameplay/port_aitables.h"   // PortAiTablesStart
#include "../gameplay/port_richbases.h"  // RichBasesResource, RichBasesCarry
#include "port_benchinfo.h"  // bench_info
#include "port_bench.h"  // PortBenchPartName, PortBenchWanted
#include "../render/port_camera.h"  // PortCameraZoom, PortCameraZoomTo
#include "../loop/port_clock.h"  // PortClockStart, PortClockStop
#include "gluRdy.h"  // PortMusicPlaying
#include "port_debugview.h"  // PortDebugViewSet, PortDebugViewModeName
#include "../front/port_front.h"  // PortFrontLaunchNext, PortFrontActive
#include "../data/port_hxm.h"  // PortHxmIsMap, PortHxmCompiled
#include "../gameplay/port_launch.h"  // PortLaunchIsCanvas
#include "../platform/port_paths.h"  // PortDataFolder, PortCommandOptions, PortLogFile, PortPackagedFile, PortMapFolderPath
#include "../loop/port_perf.h"  // PortPerfWant, PERF_PARTS, PortPerfWanted
#include "../platform/port_posix.h"  // PortNativePath
#include "../render/port_present.h"  // PortPresentRedraw, PortPresentGpu, PortScreenshot
#include "../render/port_teamcolor.h"
#include "port_trace.h"  // PortTraceCameraStart, PortTraceCameraOn, PortTracePoll
#include "../render/port_view.h"  // PortViewCameraMiddle, PortViewCameraCentre, PortViewDialogs
#include "port_control.h"
#include "../hud/port_hudlive.h"  // hud, hud_events
#include "../hud/port_hudstate.h"  // hud_press
#include "../hud/port_hudinput.h"  // hud's hits
#include "../hud/port_hudstats.h"  // hud_stats
#include "../hud/port_hud.h"       // PortHudStatsPlaced
#include "../render/port_gpu.h"
#include "../platform/port_json.h"
#include "../render/port_lights.h"
#include "../data/port_mods.h"
#include "../data/port_moddata.h"   // ModTypeByName
#include "../gameplay/port_trigrun.h"    // spawn, order, clear as the trigger actions do them
#include "../platform/port_winmsg.h"     // press, click
#include "../gameplay/port_cutscene.h"   // cutscene_play
#include "port_canvas.h"     // map_push
#include "../../../hyperion/version.h"  // HYPERION_VERSION_TEXT, in the AI context
#include "../render/port_motion.h"     // map_push's way out and in
#include "net_cmds.h"        // gameloop_init
#include "oc_data.h"
#include "gamemap.h"        // gamemap_center_on_mtx
#include "gds_palette.h"    // vid_palette_to_monitor
#include "gds_vidinimo.h"   // get_gamma, set_gamma
#include "TrigAct.h"        // TrigAction, pause_game, unpause_game
#include "net_cmds.h"       // IsGamePaused
#include "TrigEnum.h"       // MapRegion
#include "CUnit.h"          // CUnit::GetUsedList
#include "CSprite.h"        // a unit's place, for units
#include "select.h"         // select_units
#include "CUnitFind.h"      // find_units_in_rect, finish_unit_iteration
#include "usel.h"           // sub_4BC5B0, a drag box's gather
#include "cursor.h"         // cursor_get, for hud
#include "selectlimits.h"   // SELECTION_MAX
#include "playertables.h"   // the player tables, PlayerSlots
#include "playervision.h"   // SpriteSeenBy, VisionHxTileSeen
#include "saveload.h"       // get_tech_researched, get_upgrade_level, for players
#include "typelimits.h"     // TECH_TYPES_117, UPGRADE_TYPES_117
#include "port_synchash.h"   // SyncHashCompute
#include "port_hxreplay.h"   // ReplayHxSync
#include "port_pathstats.h"  // g_pathStats
#include "sai_PathCreate.h"  // ep_glob_t, the regions
#include "sai_Contours.h"    // contours_t
#include "SAI_Banker.h"      // GetRegion_xy
#include "maphdr.h"          // get_map_chk, map_push's from
#include "glues.h"           // mem_free
#include "../front/port_frontlobby.h"  // MinimapRgba, map_info's picture
#include <array>
#include <set>
#include "../data/port_stormapi.h"  // SFileGetUserDataPath, SFileGetInstallPath
#include "../../gen/globals117.h"

#include "gluMap.h"          // FindMapFile, a map's name looked for as the game looks

// 0x66FD4C ghScenArchive, spelled as src117/lang/maphdr.cpp spells it: the scenario archive,
// which a running game keeps open as its map's.
extern HANDLE ghScenArchive;
#include "sai_TownBuilder.h" // GetAreaIndex
#include "../gameplay/port_trainer.h"    // the trainer's session and keys
#include "../data/port_moddata_push.h"  // ModDataPush, ModDataSubmissionsJson

#include <algorithm>
#include "mask.h"  // mask_disable_fog
#include "mapmsg.h"  // MapMsgLines
#include "scroll.h"  // ScrollAutoTarget, ScrollWindowActive
#include "count.h"  // count_get_supplies_used, count_get_supplies_available
#include "../platform/port_globals117.h"

// Game globals (re/globals117.json), bound to their storage by the build.
struct point16_t;
extern DWORD gbColorCycle_g;           // 0x50E138

namespace {

// ---------------------------------------------------------------------------
// Messages between the two threads.

struct Message {
  int nClient;
  std::string text;
  bool bStream;  // published (PortControlPublish): left out for a client too far behind
};

std::mutex g_lock;
std::deque<Message> g_inbox;   // to the game's thread
std::deque<Message> g_outbox;  // to the channel's
std::vector<int> g_gone;       // clients the channel has closed, to the game's thread
// A client this far behind misses published messages until it catches up.
const size_t kStreamBehind = 8u << 20;
bool g_bRunning;
int g_nPort;
std::string g_key;  // written before the channel's thread starts, read only after
// A message's most, its frames together: a map's tiles in base64 fit many times over.
const unsigned long long kMessageMost = 16ull << 20;

// A WebSocket's text is UTF-8, and a client closes on any other (PortJsonUtf8).
void Send(int nClient, const std::string &text) {
  std::string utf8 = PortJsonUtf8(text);
  std::lock_guard<std::mutex> hold(g_lock);
  g_outbox.push_back({nClient, std::move(utf8), false});
}

// ---------------------------------------------------------------------------
// The channel's thread.

struct Client {
  int nId;
  SOCKET s;
  bool bSocket;          // the handshake is done
  std::string in;        // bytes not yet taken
  std::string message;   // a fragmented message so far
  std::string out;       // bytes not yet sent
  bool bClose;           // close once out is sent
};

std::string Base64(const unsigned char *p, size_t n) {
  static const char k[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < n; i += 3) {
    const unsigned v = p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
    out += k[v >> 18 & 63];
    out += k[v >> 12 & 63];
    out += i + 1 < n ? k[v >> 6 & 63] : '=';
    out += i + 2 < n ? k[v & 63] : '=';
  }
  return out;
}

// The handshake's answer: SHA-1 of the key and RFC 6455's GUID, in base64.
std::string Accept(const std::string &key) {
  const std::string text = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  unsigned char hash[20] = {};
  BCryptHash(BCRYPT_SHA1_ALG_HANDLE, NULL, 0, reinterpret_cast<PUCHAR>(const_cast<char *>(text.data())),
             static_cast<ULONG>(text.size()), hash, sizeof hash);
  return Base64(hash, sizeof hash);
}

// A header's value from a request, its name in any case, or empty.
std::string Header(const std::string &request, const char *pszName) {
  const size_t n = strlen(pszName);
  size_t at = request.find("\r\n");
  while (at != std::string::npos) {
    const size_t next = request.find("\r\n", at + 2);
    const std::string line = request.substr(at + 2, next == std::string::npos ? std::string::npos : next - at - 2);
    if (line.size() > n && _strnicmp(line.c_str(), pszName, n) == 0 && line[n] == ':') {
      size_t v = n + 1;
      while (v < line.size() && line[v] == ' ')
        ++v;
      return line.substr(v);
    }
    at = next;
  }
  return std::string();
}

std::string Frame(int nOpcode, const std::string &payload) {
  std::string f;
  f += static_cast<char>(0x80 | nOpcode);
  const size_t n = payload.size();
  if (n < 126) {
    f += static_cast<char>(n);
  } else if (n < 65536) {
    f += static_cast<char>(126);
    f += static_cast<char>(n >> 8);
    f += static_cast<char>(n);
  } else {
    f += static_cast<char>(127);
    for (int i = 7; i >= 0; --i)
      f += static_cast<char>(static_cast<unsigned long long>(n) >> (8 * i));
  }
  return f + payload;
}

void Http(Client &c, const char *pszStatus, const char *pszType, const std::string &body) {
  c.out += std::string("HTTP/1.1 ") + pszStatus + "\r\nContent-Type: " + pszType +
           "\r\nCache-Control: no-store\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\nConnection: close\r\n\r\n" + body;
  c.bClose = true;
}

bool OriginAllowed(const std::string &origin) {
  if (origin.empty())
    return true;  // not a browser; the key decides
  const std::string a = "http://127.0.0.1:" + std::to_string(g_nPort);
  const std::string b = "http://localhost:" + std::to_string(g_nPort);
  return origin == a || origin == b;
}

// A GET's path without its query ("/guide" of "GET /guide?t=... HTTP/1.1"), or empty for any
// other method.
std::string GetPath(const std::string &request) {
  if (request.compare(0, 4, "GET ") != 0)
    return std::string();
  const size_t end = request.find_first_of(" ?\r", 4);
  return request.substr(4, end == std::string::npos ? std::string::npos : end - 4);
}

// Whether the request's query holds t=<the key>, compared in full whatever differs first.
bool KeyGiven(const std::string &request) {
  const size_t line = request.find("\r\n");
  const size_t q = request.find('?');
  if (g_key.empty() || q == std::string::npos || q > line)
    return false;
  size_t at = q;
  while (at < line && at != std::string::npos) {
    if (request.compare(at + 1, 2, "t=") == 0) {
      const size_t end = request.find_first_of("& ", at + 3);
      const std::string given = request.substr(at + 3, end == std::string::npos ? std::string::npos : end - at - 3);
      unsigned char diff = given.size() == g_key.size() ? 0 : 1;
      for (size_t i = 0; i < g_key.size(); ++i)
        diff |= static_cast<unsigned char>(g_key[i] ^ (i < given.size() ? given[i] : 0));
      return diff == 0;
    }
    at = request.find('&', at + 1);
  }
  return false;
}

void Request(Client &c) {
  const size_t end = c.in.find("\r\n\r\n");
  if (end == std::string::npos) {
    if (c.in.size() > 16384)
      Http(c, "431 Request Header Fields Too Large", "text/plain", "too long\n");
    return;
  }
  const std::string request = c.in.substr(0, end + 2);
  c.in.erase(0, end + 4);
  // a name that only resolves here through DNS rebinding is refused, as is any other
  const std::string host = Header(request, "Host");
  if (!host.empty() && host != "127.0.0.1:" + std::to_string(g_nPort) && host != "localhost:" + std::to_string(g_nPort)) {
    Http(c, "403 Forbidden", "text/plain", "Hyperion's channel answers to 127.0.0.1 and localhost only.\n");
    return;
  }
  const std::string key = Header(request, "Sec-WebSocket-Key");
  if (!key.empty()) {
    if (!OriginAllowed(Header(request, "Origin"))) {
      Http(c, "403 Forbidden", "text/plain", "Hyperion's channel takes pages it served itself, and tools.\n");
      return;
    }
    if (!KeyGiven(request)) {
      Http(c, "401 Unauthorized", "text/plain",
           "Hyperion's channel needs its key as ?t=: tools read it from " + PortControlKeyFile(g_nPort) +
               ", and Debug Tools' Copy link gives a page's link with it.\n");
      return;
    }
    c.out += "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
             "Sec-WebSocket-Accept: " + Accept(key) + "\r\n\r\n";
    c.bSocket = true;
    return;
  }
  // the panel, the editors' guide, the sample scenes and the snapshots the panel shows (each a
  // plain name in its folder); the pages hold no state, a snapshot shows the game and takes the key
  const std::string get = GetPath(request);
  std::string path, type;
  if (get == "/" || get == "/index.html") {
    path = PortModsFolder() + "debug-tools\\panel.html";
    type = "text/html; charset=utf-8";
  } else if (get == "/guide" || get == "/guide.html") {
    path = PortModsFolder() + "debug-tools\\guide.html";
    type = "text/html; charset=utf-8";
  } else if (get == "/cutscenes/") {
    // the sample scenes there are, as a JSON list of their file names
    std::string list;
    WIN32_FIND_DATAA fd;
    const HANDLE hFind = FindFirstFileA((PortModsFolder() + "debug-tools\\cutscenes\\*.json").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
      do
        list += std::string(list.empty() ? "" : ",") + PortJsonQuote(fd.cFileName);
      while (FindNextFileA(hFind, &fd));
      FindClose(hFind);
    }
    Http(c, "200 OK", "application/json; charset=utf-8", "[" + list + "]");
    return;
  } else if (request.compare(0, 15, "GET /cutscenes/") == 0) {
    const std::string name = request.substr(15, request.find_first_of(" ?", 15) - 15);
    bool bPlain = name.size() > 5 && _stricmp(name.c_str() + name.size() - 5, ".json") == 0 && name[0] != '.';
    for (char ch : name)
      bPlain = bPlain && (isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.');
    if (bPlain) {
      path = PortModsFolder() + "debug-tools\\cutscenes\\" + name;
      type = "application/json; charset=utf-8";
    }
  } else if (get == "/ai-context") {
    Http(c, "200 OK", "text/markdown; charset=utf-8", PortControlAiContext(false));
    return;
  } else if (request.compare(0, 15, "GET /snapshots/") == 0 && KeyGiven(request)) {
    const std::string name = request.substr(15, request.find_first_of(" ?", 15) - 15);
    bool bPlain = name.size() > 4 && _stricmp(name.c_str() + name.size() - 4, ".png") == 0 && name[0] != '.';
    for (char ch : name)
      bPlain = bPlain && (isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.');
    if (bPlain) {
      path = std::string(PortDataFolder()) + "snapshots\\" + name;
      type = "image/png";
    }
  }
  if (!path.empty()) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
      std::string body(GetFileSize(h, NULL), '\0');
      DWORD cb = 0;
      ReadFile(h, &body[0], static_cast<DWORD>(body.size()), &cb, NULL);
      CloseHandle(h);
      Http(c, "200 OK", type.c_str(), body);
      return;
    }
  }
  Http(c, "404 Not Found", "text/plain",
       "Hyperion's live channel: connect a WebSocket here. Debug Tools' panel is not installed.\n");
}

// The frames in c.in, as far as they are whole.
void Frames(Client &c) {
  for (;;) {
    const unsigned char *p = reinterpret_cast<const unsigned char *>(c.in.data());
    const size_t n = c.in.size();
    if (n < 2)
      return;
    const bool bFin = (p[0] & 0x80) != 0;
    const int nOpcode = p[0] & 0x0F;
    const bool bMasked = (p[1] & 0x80) != 0;
    unsigned long long len = p[1] & 0x7F;
    size_t at = 2;
    if (len == 126) {
      if (n < 4)
        return;
      len = static_cast<unsigned long long>(p[2]) << 8 | p[3];
      at = 4;
    } else if (len == 127) {
      if (n < 10)
        return;
      len = 0;
      for (int i = 0; i < 8; ++i)
        len = len << 8 | p[2 + i];
      at = 10;
    }
    if (!bMasked || len > kMessageMost) {  // a client masks every frame
      c.bClose = true;
      return;
    }
    if (n < at + 4 + len)
      return;
    const unsigned char *mask = p + at;
    std::string payload(static_cast<size_t>(len), '\0');
    for (size_t i = 0; i < len; ++i)
      payload[i] = static_cast<char>(p[at + 4 + i] ^ mask[i & 3]);
    c.in.erase(0, at + 4 + static_cast<size_t>(len));
    if (nOpcode == 8) {  // close
      c.out += Frame(8, payload.substr(0, 2));
      c.bClose = true;
      return;
    }
    if (nOpcode == 9) {  // ping
      c.out += Frame(10, payload);
      continue;
    }
    if (nOpcode == 1 || nOpcode == 0) {
      if (c.message.size() + payload.size() > kMessageMost) {  // the whole message, its fragments together
        c.bClose = true;
        return;
      }
      c.message += payload;
      if (bFin) {
        std::lock_guard<std::mutex> hold(g_lock);
        g_inbox.push_back({c.nId, c.message, false});
        c.message.clear();
      }
    }
  }
}

DWORD WINAPI Thread(void *pv) {
  SOCKET listener = reinterpret_cast<SOCKET>(pv);
  std::vector<Client> clients;
  int nNext = 1;
  for (;;) {
    // what the game's thread wrote
    {
      std::lock_guard<std::mutex> hold(g_lock);
      for (const Message &m : g_outbox)
        for (Client &c : clients)
          if (c.nId == m.nClient && c.bSocket && !(m.bStream && c.out.size() > kStreamBehind))
            c.out += Frame(1, m.text);
      g_outbox.clear();
    }
    fd_set reads, writes;
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    FD_SET(listener, &reads);
    for (const Client &c : clients) {
      FD_SET(c.s, &reads);
      if (!c.out.empty())
        FD_SET(c.s, &writes);
    }
    timeval wait = {0, 15000};
    if (select(0, &reads, &writes, NULL, &wait) == SOCKET_ERROR) {
      Sleep(15);
      continue;
    }
    if (FD_ISSET(listener, &reads)) {
      SOCKET s = accept(listener, NULL, NULL);
      if (s != INVALID_SOCKET && clients.size() < 16) {
        u_long nonBlocking = 1;
        ioctlsocket(s, FIONBIO, &nonBlocking);
        clients.push_back({nNext++, s, false, "", "", "", false});
      } else if (s != INVALID_SOCKET) {
        closesocket(s);
      }
    }
    for (Client &c : clients) {
      if (FD_ISSET(c.s, &reads)) {
        char buf[65536];
        const int n = recv(c.s, buf, sizeof buf, 0);
        if (n <= 0) {
          c.bClose = true;
          c.out.clear();
        } else {
          c.in.append(buf, n);
          if (!c.bSocket)
            Request(c);
          if (c.bSocket)
            Frames(c);
        }
      }
      if (!c.out.empty() && FD_ISSET(c.s, &writes)) {
        const int n = send(c.s, c.out.data(), static_cast<int>(c.out.size() > 1u << 20 ? 1u << 20 : c.out.size()), 0);
        if (n > 0)
          c.out.erase(0, n);
        else if (WSAGetLastError() != WSAEWOULDBLOCK) {
          c.bClose = true;
          c.out.clear();
        }
      }
    }
    for (size_t i = 0; i < clients.size();) {
      if (clients[i].bClose && clients[i].out.empty()) {
        {
          std::lock_guard<std::mutex> hold(g_lock);
          g_gone.push_back(clients[i].nId);
        }
        closesocket(clients[i].s);
        clients.erase(clients.begin() + i);
      } else {
        ++i;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// The values.

struct Value {
  std::string name;
  std::string help;
  float *pf;
  PFN_CONTROL_GET pfnGet;
  PFN_CONTROL_SET pfnSet;
  float fMin, fMax, fDefault;
  float fStep;  // 1 for a switch, 0 for a value that slides freely
};

std::vector<Value> &Values(void) {
  static std::vector<Value> s_values;
  return s_values;
}

void Register(const Value &v) {
  for (Value &o : Values())
    if (o.name == v.name) {
      o = v;
      return;
    }
  Values().push_back(v);
}

float Get(const Value &v) {
  return v.pf != NULL ? *v.pf : v.pfnGet != NULL ? v.pfnGet() : 0.0f;
}

void Set(const Value &v, float f) {
  f = f < v.fMin ? v.fMin : f > v.fMax ? v.fMax : f;
  if (v.pf != NULL)
    *v.pf = f;
  else if (v.pfnSet != NULL)
    v.pfnSet(f);
  PortPresentRedraw();  // seen at once, paused or not
}

float GammaGet(void) { return static_cast<float>(get_gamma()); }
void GammaSet(float f) {
  set_gamma(static_cast<DWORD>(lroundf(f)));
  vid_palette_to_monitor();  // as the video options dialog applies it
}
float CycleGet(void) { return gbColorCycle_g != 0 ? 1.0f : 0.0f; }
void CycleSet(float f) { gbColorCycle_g = f >= 0.5f; }

void BuiltInValues(void) {
  static bool s_bDone;
  if (s_bDone)
    return;
  s_bDone = true;
  PortControlValue("gamma", GammaGet, GammaSet, 60.0f, 140.0f, 100.0f,
                   "the original's gamma, 60 to 140; 100 leaves the palette as it is", 1.0f);
  PortControlValue("colour_cycling", CycleGet, CycleSet, 0.0f, 1.0f, 1.0f,
                   "whether the palette's water and lava colours turn (1) or hold still (0)", 1.0f);
}

// ---------------------------------------------------------------------------
// JSON answers.

std::string Num(double d) {
  char sz[32];
  sprintf_s(sz, "%.6g", d);
  return sz;
}

// The request's id, written back as it came (a number, whole numbers to the last digit, or a
// string), or null.
std::string IdOf(const PortJson &j) {
  const PortJson &id = j["id"];
  if (id.kind == PortJson::NUMBER) {
    char sz[40];
    snprintf(sz, sizeof sz, id.d == floor(id.d) && fabs(id.d) < 9007199254740992.0 ? "%.0f" : "%.17g", id.d);
    return sz;
  }
  return id.kind == PortJson::STRING ? PortJsonQuote(id.s) : "null";
}

std::string Answer(const std::string &id, const std::string &body) {
  return "{\"id\":" + id + ",\"ok\":true" + (body.empty() ? "" : "," + body) + "}";
}

std::string Error(const std::string &id, const std::string &why) {
  return "{\"id\":" + id + ",\"ok\":false,\"error\":" + PortJsonQuote(why) + "}";
}

// ---------------------------------------------------------------------------
// The game's thread: state the commands need across frames.

bool g_bChanged;           // a sandbox command ran in this game
bool g_bStepping;
bool g_bStepFast;          // the step runs its frames back to back (pathbench)
bool g_bPausedHere;        // the channel paused the game, and the player has not taken it back
unsigned g_nStepTo;
int g_nStepClient;
std::string g_stepId;

// A snapshot asked for: its path, and who asked.
std::string g_snapPath;
int g_nSnapClient;
std::string g_snapId;
bool g_bSnapGpu;           // asked of the GPU presenter's next frame
bool g_bSnapFront;         // asked of the front end's next frame

std::string g_canvasTileset;
bool g_bCanvasAsked;

bool InGame(void) {
  return gwGameMode == 3 && gbMultiPlayerMode == 0;
}

// A map pushed or loaded (map_push, map_load): the picture goes out (SWAP_OUT), the game is
// left as Quit Mission leaves it and the front end starts the map (SWAP_LEFT), and when the new
// game runs its scene is played. The arrival is the motion's (PortMotionArriveNext).
enum { SWAP_NONE, SWAP_OUT, SWAP_LEFT };
int g_nSwap;
double g_fSwapAt;
std::string g_swapCanvas, g_swapMap;
bool g_bSwapUms, g_bSwapScene, g_bSwapSawMenus;
bool g_bSwapReveal;  // the fog lifted as the map comes in, as reveal does
// the frame count the last game left, seen from the menus: until the new game's count starts
// again below it, a game mode of 3 is the new game still being set up, its fog not yet made
unsigned int g_nSwapOldFrame;
bool g_bSwapNewGame;
PortJson g_swapScene;
std::string g_swapError;  // why the last one's scene did not play

void SwapLeave(void) {
  PortFrontLaunchNext(g_swapCanvas.empty() ? NULL : g_swapCanvas.c_str(), g_swapMap.empty() ? NULL : g_swapMap.c_str(),
                      g_bSwapUms);
  PortMotionArriveNext();
  if (gwGameMode == 3)
    PortMotionCover();
  g_bSwapSawMenus = gwGameMode != 3;
  g_nSwapOldFrame = oc_dwReplayFrame;
  g_bSwapNewGame = false;
  if (gwGameMode == 3)
    gameloop_init(4);  // to the menus, as Quit Mission leaves
  g_nSwap = SWAP_LEFT;
}

void SwapStep(void) {
  if (g_nSwap == SWAP_OUT && (gwGameMode != 3 || PortMotionClock() >= g_fSwapAt)) {
    SwapLeave();
  } else if (g_nSwap == SWAP_LEFT) {
    if (gwGameMode != 3) {
      g_bSwapSawMenus = true;
      g_nSwapOldFrame = oc_dwReplayFrame;
    } else if (g_bSwapSawMenus && !g_bSwapNewGame) {
      g_bSwapNewGame = oc_dwReplayFrame < g_nSwapOldFrame || g_nSwapOldFrame <= 1;
    }
    if (g_bSwapSawMenus && g_bSwapNewGame && gwGameMode == 3 && oc_dwReplayFrame >= 1 && gpSquareMap != NULL) {
      g_nSwap = SWAP_NONE;
      g_swapError.clear();
      if (g_bSwapReveal && gbMultiPlayerMode == 0) {
        glCheatBits |= 1;
        mask_disable_fog();
      }
      g_bSwapReveal = false;
      if (g_bSwapScene && !PortCutscenePlay(g_swapScene, &g_swapError))
        g_swapError = "the scene did not play: " + g_swapError;
      g_bSwapScene = false;
    }
  }
}

const char *SwapState(void) {
  return g_nSwap == SWAP_OUT ? "going out" : g_nSwap == SWAP_LEFT ? "loading" : "none";
}

// The tiles of a map_push: tiles, tiles_b64 or fill; false with *pWhy when they cannot be read.
bool PushTiles(const PortJson &j, int nCells, std::vector<unsigned short> *pTiles, std::string *pWhy) {
  if (j["tiles"].kind == PortJson::ARRAY) {
    for (const PortJson &v : j["tiles"].items) {
      if (v.kind != PortJson::NUMBER || v.d < 0 || v.d > 0xFFFF) {
        *pWhy = "tiles holds numbers 0-65535, MTXM's values";
        return false;
      }
      pTiles->push_back(static_cast<unsigned short>(v.d));
    }
  } else if (j["tiles_b64"].kind == PortJson::STRING) {
    static const std::string kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<unsigned char> bytes;
    unsigned int nBits = 0, nValue = 0;
    for (const char c : j["tiles_b64"].s) {
      if (c == '=' || c == '\r' || c == '\n')
        continue;
      const size_t k = kAlphabet.find(c);
      if (k == std::string::npos) {
        *pWhy = "tiles_b64 is not base64";
        return false;
      }
      nValue = (nValue << 6) | static_cast<unsigned int>(k);
      nBits += 6;
      if (nBits >= 8) {
        nBits -= 8;
        bytes.push_back(static_cast<unsigned char>(nValue >> nBits));
      }
    }
    for (size_t i = 0; i + 1 < bytes.size(); i += 2)
      pTiles->push_back(static_cast<unsigned short>(bytes[i] | (bytes[i + 1] << 8)));
  } else if (j["fill"].kind == PortJson::NUMBER) {
    pTiles->assign(static_cast<size_t>(nCells), static_cast<unsigned short>(j["fill"].d));
  }
  return true;
}

// The maps Hyperion finds (maps, and a name looked for by its file name): every .scm, .scx and
// .hxm under the data folder's maps\, the install's and each map folder (PortMapFolderPath), in
// their folders too, as the skirmish list walks them, leaving out replays and saves. Read again
// when asked, or when ten seconds old.
struct MapEntry {
  std::string name;    // the file's name
  std::string path;    // its full path
  std::string folder;  // its folder under the root: "" at the top, "ladder\" and so on
  std::string root;    // "data", "install", or the map folder (--maps, StarCraft from Battle.net's Maps)
};
std::vector<MapEntry> g_maps;
Uint64 g_nMapsAt;

void WalkMapFolder(const std::string &dir, const std::string &rel, const std::string &root, int nDepth,
                   std::set<std::string> *pSeen) {
  if (nDepth > 6 || g_maps.size() >= 5000)
    return;
  WIN32_FIND_DATAA fd;
  const HANDLE h = FindFirstFileA((dir + "*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;
  std::vector<std::string> folders;
  do {
    const std::string name = fd.cFileName;
    if (name == "." || name == "..")
      continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (_stricmp(name.c_str(), "replays") != 0 && _stricmp(name.c_str(), "save") != 0)
        folders.push_back(name);
      continue;
    }
    const size_t nDot = name.rfind('.');
    const char *pszExt = nDot == std::string::npos ? "" : name.c_str() + nDot + 1;
    if (_stricmp(pszExt, "scm") != 0 && _stricmp(pszExt, "scx") != 0 && _stricmp(pszExt, "hxm") != 0)
      continue;
    std::string key = dir + name;
    CharLowerA(&key[0]);
    if (pSeen->insert(key).second)
      g_maps.push_back({name, dir + name, rel, root});
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  for (const std::string &f : folders)
    WalkMapFolder(dir + f + "\\", rel + f + "\\", root, nDepth + 1, pSeen);
}

const std::vector<MapEntry> &Maps(bool bFresh) {
  if (!bFresh && g_nMapsAt != 0 && SDL_GetTicks() - g_nMapsAt < 10000)
    return g_maps;
  g_maps.clear();
  std::set<std::string> seen;
  char sz[MAX_PATH] = "";
  std::string data;
  if (SFileGetUserDataPath(sz, sizeof sz, 1)) {
    data = std::string(sz) + "maps\\";
    WalkMapFolder(data, "", "data", 0, &seen);
  }
  if (SFileGetInstallPath(sz, sizeof sz, 1) && _stricmp((std::string(sz) + "maps\\").c_str(), data.c_str()) != 0)
    WalkMapFolder(std::string(sz) + "maps\\", "", "install", 0, &seen);
  for (int i = 0; i < PortMapFolderCount(); ++i)
    if (PortMapFolderPath(i, "maps\\", sz, sizeof sz))
      WalkMapFolder(sz, "", sz, 0, &seen);
  g_nMapsAt = SDL_GetTicks();
  return g_maps;
}

// A map file's full path: a full path as it is, or a name looked for as the game looks for a
// map (FindMapFile: the data folder, the install folder, then each --maps folder), so
// "maps\(2)Volcanis.scm" finds the install's; failing that, its file name, with or without the
// extension, in any folder of the maps Hyperion finds, so "(4)Lost Temple.scm" finds the one in
// a ladder\ folder.
bool MapFilePath(const std::string &name, std::string *pPath) {
  char szPath[MAX_PATH] = "";
  if (name.empty())
    return false;
  if (FindMapFile(name.c_str(), szPath, sizeof szPath)) {
    *pPath = szPath;
    return true;
  }
  const std::string file = name.substr(name.find_last_of("\\/") + 1);
  const bool bExt = file.find('.') != std::string::npos;
  for (int nPass = 0; nPass < 2; ++nPass)
    for (const MapEntry &m : Maps(nPass == 1)) {
      const std::string stem = m.name.substr(0, m.name.rfind('.'));
      if (_stricmp(m.name.c_str(), file.c_str()) == 0 || (!bExt && _stricmp(stem.c_str(), file.c_str()) == 0)) {
        *pPath = m.path;
        return true;
      }
    }
  return false;
}

// A map file's scenario.chk, read as 1.17 reads a map (get_map_chk; an .hxm through the map
// Hyperion compiles from it). get_map_chk opens the map as the scenario archive and closes it
// after; a running game's own stays open beside it, and is its again after.
bool ReadMapChk(const std::string &name, std::string *pPath, std::vector<BYTE> *pChk, std::string *pWhy) {
  if (!MapFilePath(name, pPath)) {
    *pWhy = "names no map file: " + name;
    return false;
  }
  std::string path = *pPath;
  char szCompiled[MAX_PATH] = "";
  if (PortHxmIsMap(path.c_str())) {
    if (!PortHxmCompiled(path.c_str(), szCompiled, sizeof szCompiled)) {
      *pWhy = "the .hxm could not be read (hyperion_hxm.log says why): " + path;
      return false;
    }
    path = szCompiled;
  }
  const HANDLE hGameArchive = ghScenArchive;
  ghScenArchive = NULL;
  DWORD dwSize = 0;
  BYTE *p = static_cast<BYTE *>(get_map_chk(path.c_str(), &dwSize));
  ghScenArchive = hGameArchive;
  if (p == NULL) {
    *pWhy = "the map's scenario could not be read: " + *pPath;
    return false;
  }
  pChk->assign(p, p + dwSize);
  mem_free(p, __FILE__, __LINE__);
  return true;
}

// Each section of a CHK in turn, its name and data; the last of a name counts, as in 1.17.
template <typename F> void ChkSections(const std::vector<BYTE> &chk, F f) {
  const DWORD dwSize = static_cast<DWORD>(chk.size());
  for (DWORD dwAt = 0; dwAt + 8 <= dwSize;) {
    const BYTE *p = chk.data() + dwAt;
    LONG nLen = 0;
    memcpy(&nLen, p + 4, 4);
    const DWORD dwData = dwAt + 8;
    if (nLen < 0)
      break;
    f(p, p + 8, (std::min)(static_cast<DWORD>(nLen), dwSize - dwData));
    if (static_cast<DWORD>(nLen) > dwSize - dwData)
      break;
    dwAt = dwData + static_cast<DWORD>(nLen);
  }
}

const char *const kTilesetNames[] = {"badlands", "platform", "install", "ashworld",
                                     "jungle", "desert", "ice", "twilight"};

// The maps command's list: the maps whose folder and name hold filter (any case), nMax of them.
std::string MapsJson(const std::string &filter, size_t nMax, bool bFresh) {
  std::string want = filter;
  for (char &ch : want)
    ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
  std::string list;
  size_t nShown = 0, nAll = 0;
  for (const MapEntry &m : Maps(bFresh)) {
    if (!want.empty()) {
      std::string hay = m.folder + m.name;
      for (char &ch : hay)
        ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
      if (hay.find(want) == std::string::npos)
        continue;
    }
    ++nAll;
    if (nShown >= nMax)
      continue;
    list += std::string(nShown++ ? "," : "") + "{\"name\":" + PortJsonQuote(m.name) + ",\"folder\":" + PortJsonQuote(m.folder) +
            ",\"root\":" + PortJsonQuote(m.root) + ",\"path\":" + PortJsonQuote(m.path) + "}";
  }
  return "\"count\":" + std::to_string(nAll) + ",\"maps\":[" + list + "]";
}

// One string of a CHK's STR section (1-based), or "" (a Remastered map's STRx too).
std::string ChkString(const BYTE *pStr, DWORD cbStr, bool bWide, unsigned n) {
  if (pStr == NULL || n == 0)
    return "";
  const DWORD dwCountSize = bWide ? 4 : 2;
  if (cbStr < dwCountSize)
    return "";
  DWORD dwCount = 0;
  memcpy(&dwCount, pStr, dwCountSize);
  if (n > dwCount || dwCountSize * (n + 1) > cbStr)
    return "";
  DWORD dwOffset = 0;
  memcpy(&dwOffset, pStr + dwCountSize * n, dwCountSize);
  if (dwOffset >= cbStr)
    return "";
  std::string text;
  for (DWORD i = dwOffset; i < cbStr && pStr[i] != 0; ++i)
    if (pStr[i] >= 0x20 || pStr[i] == '\n')  // the colour codes left out
      text += static_cast<char>(pStr[i]);
  return text;
}

// map_info's answer: what the map is, its starts, and its minimap written as a picture in the data
// folder's snapshots\ (served at /snapshots/), its starts marked.
bool MapInfoJson(const std::string &name, std::string *pBody, std::string *pWhy) {
  std::string path;
  std::vector<BYTE> chk;
  if (!ReadMapChk(name, &path, &chk, pWhy))
    return false;
  int nEra = -1, nWidth = 0, nHeight = 0, nPlayers = 0;
  const BYTE *pMtxm = NULL, *pStr = NULL;
  DWORD cbMtxm = 0, cbStr = 0;
  bool bWide = false;
  unsigned nTitle = 0, nAbout = 0;
  std::vector<std::array<int, 3>> starts;
  ChkSections(chk, [&](const BYTE *pName, const BYTE *p, DWORD cb) {
    if (memcmp(pName, "ERA ", 4) == 0 && cb >= 2) {
      nEra = (p[0] | (p[1] << 8)) & 7;
    } else if (memcmp(pName, "DIM ", 4) == 0 && cb >= 4) {
      nWidth = p[0] | (p[1] << 8);
      nHeight = p[2] | (p[3] << 8);
    } else if (memcmp(pName, "MTXM", 4) == 0) {
      pMtxm = p;
      cbMtxm = cb;
    } else if (memcmp(pName, "OWNR", 4) == 0) {
      nPlayers = 0;
      for (DWORD i = 0; i < cb && i < 12; ++i)
        nPlayers += p[i] == 5 || p[i] == 6;  // a computer's slot or a human's
    } else if (memcmp(pName, "SPRP", 4) == 0 && cb >= 4) {
      nTitle = p[0] | (p[1] << 8);
      nAbout = p[2] | (p[3] << 8);
    } else if (memcmp(pName, "STR ", 4) == 0 && !bWide) {
      pStr = p;
      cbStr = cb;
    } else if (memcmp(pName, "STRx", 4) == 0) {
      pStr = p;
      cbStr = cb;
      bWide = true;
    } else if (memcmp(pName, "UNIT", 4) == 0) {
      starts.clear();
      for (DWORD i = 0; i + 36 <= cb; i += 36)
        if ((p[i + 8] | (p[i + 9] << 8)) == 214)  // a start location
          starts.push_back({p[i + 4] | (p[i + 5] << 8), p[i + 6] | (p[i + 7] << 8), p[i + 16]});
    }
  });
  if (nEra < 0 || nWidth <= 0 || nHeight <= 0) {
    *pWhy = "the map has no tileset or size: " + path;
    return false;
  }
  // the minimap, the starts marked, as a picture
  std::string preview;
  std::vector<WORD> tiles(static_cast<size_t>(nWidth) * nHeight, 0);
  for (size_t i = 0; i < tiles.size() && i * 2 + 1 < cbMtxm; ++i)
    tiles[i] = static_cast<WORD>(pMtxm[i * 2] | (pMtxm[i * 2 + 1] << 8));
  std::vector<unsigned char> rgba;
  int nW = 0, nH = 0;
  if (FrontKit::MinimapRgba(nEra, nWidth, nHeight, tiles.data(), tiles.size(), 256, &rgba, &nW, &nH)) {
    for (const auto &st : starts) {
      const int cx = st[0] * nW / (nWidth * 32), cy = st[1] * nH / (nHeight * 32);
      for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx) {
          const int x = cx + dx, y = cy + dy;
          if (x < 0 || y < 0 || x >= nW || y >= nH || dx * dx + dy * dy > 10)
            continue;
          unsigned char *o = &rgba[(static_cast<size_t>(y) * nW + x) * 4];
          const bool bRim = dx * dx + dy * dy > 4;
          o[0] = bRim ? 0 : 255;
          o[1] = bRim ? 0 : 255;
          o[2] = bRim ? 0 : 255;
        }
    }
    unsigned int nHash = 2166136261u;
    for (char ch : path)
      nHash = (nHash ^ static_cast<unsigned char>(tolower(static_cast<unsigned char>(ch)))) * 16777619u;
    char szName[32];
    snprintf(szName, sizeof szName, "map-%08x.png", nHash);
    const std::string folder = std::string(PortDataFolder()) + "snapshots\\";
    CreateDirectoryA(folder.c_str(), NULL);
    SDL_Surface *s = SDL_CreateSurfaceFrom(nW, nH, SDL_PIXELFORMAT_RGBA32, rgba.data(), nW * 4);
    if (s != NULL) {
      if (SDL_SavePNG(s, PortNativePath((folder + szName).c_str())))
        preview = std::string("/snapshots/") + szName;
      SDL_DestroySurface(s);
    }
  }
  std::string startList;
  for (const auto &st : starts)
    startList += std::string(startList.empty() ? "" : ",") + "[" + std::to_string(st[0]) + "," +
                 std::to_string(st[1]) + "," + std::to_string(st[2]) + "]";
  const size_t nSlash = path.find_last_of("\\/");
  *pBody = "\"name\":" + PortJsonQuote(path.substr(nSlash + 1)) + ",\"path\":" + PortJsonQuote(path) +
           ",\"width\":" + std::to_string(nWidth) + ",\"height\":" + std::to_string(nHeight) +
           ",\"tileset\":" + PortJsonQuote(kTilesetNames[nEra]) + ",\"players\":" + std::to_string(nPlayers) +
           ",\"title\":" + PortJsonQuote(ChkString(pStr, cbStr, bWide, nTitle)) +
           ",\"description\":" + PortJsonQuote(ChkString(pStr, cbStr, bWide, nAbout)) + ",\"starts\":[" + startList + "]" +
           ",\"pushable\":" + std::string((nWidth == 64 || nWidth == 96 || nWidth == 128 || nWidth == 192 ||
                                           nWidth == 256) && (nHeight == 64 || nHeight == 96 || nHeight == 128 ||
                                                              nHeight == 192 || nHeight == 256) ? "true" : "false") +
           ",\"preview\":" + PortJsonQuote(preview);
  return true;
}

// map_push's "from": a map file's terrain, its tileset, size and MTXM, read as 1.17 reads a map
// (get_map_chk; an .hxm through the map Hyperion compiles from it). The last of each section
// counts, as in 1.17; a short MTXM leaves the rest as tile 0.
bool PushFromMap(const std::string &from, PortCanvasMap *pMap, std::string *pWhy) {
  std::string path;
  std::vector<BYTE> chk;
  if (!ReadMapChk(from, &path, &chk, pWhy)) {
    if (pWhy->compare(0, 6, "names ") == 0)
      *pWhy = "from " + *pWhy;
    return false;
  }
  int nEra = -1;
  const BYTE *pMtxm = NULL;
  DWORD cbMtxm = 0;
  ChkSections(chk, [&](const BYTE *pName, const BYTE *p, DWORD cb) {
    if (memcmp(pName, "ERA ", 4) == 0 && cb >= 2) {
      nEra = (p[0] | (p[1] << 8)) & 7;
    } else if (memcmp(pName, "DIM ", 4) == 0 && cb >= 4) {
      pMap->nWidth = p[0] | (p[1] << 8);
      pMap->nHeight = p[2] | (p[3] << 8);
    } else if (memcmp(pName, "MTXM", 4) == 0) {
      pMtxm = p;
      cbMtxm = cb;
    }
  });
  const auto pushable = [](int n) { return n == 64 || n == 96 || n == 128 || n == 192 || n == 256; };
  // the size is tested before the cells are made: DIM holds two words, so a map saying it is
  // 65535 tiles a side would ask for the cells of a map that size first
  bool bOk = nEra >= 0 && pMap->nWidth > 0 && pMap->nHeight > 0 && pMtxm != NULL;
  if (!bOk) {
    *pWhy = "the map has no tileset, size or tiles: " + path;
  } else if (!pushable(pMap->nWidth) || !pushable(pMap->nHeight)) {
    *pWhy = from + " is " + std::to_string(pMap->nWidth) + " x " + std::to_string(pMap->nHeight) +
            " tiles; map_push makes maps 64, 96, 128, 192 or 256 tiles a side, and map_load plays any";
    bOk = false;
  } else {
    pMap->tileset = kTilesetNames[nEra];
    const size_t nCells = static_cast<size_t>(pMap->nWidth) * pMap->nHeight;
    pMap->tiles.assign(nCells, 0);
    for (size_t i = 0; i < nCells && i * 2 + 1 < cbMtxm; ++i)
      pMap->tiles[i] = static_cast<unsigned short>(pMtxm[i * 2] | (pMtxm[i * 2 + 1] << 8));
  }
  return bOk;
}

std::string Status(void) {
  const bool bGame = gwGameMode == 3;
  std::string s = "\"state\":" + PortJsonQuote(PortFrontActive() ? "front end" : bGame ? "game" : "menus");
  s += ",\"frame\":" + std::to_string(bGame ? oc_dwReplayFrame : 0);
  s += ",\"paused\":" + std::string(bGame && gbPauseGame != 0 ? "true" : "false");
  s += ",\"canvas\":" + std::string(bGame && PortLaunchIsCanvas() ? "true" : "false");
  s += ",\"changed\":" + std::string(g_bChanged ? "true" : "false");
  if (bGame) {
    s += ",\"map\":{\"width\":" + std::to_string(gwMapWidth * 32) + ",\"height\":" + std::to_string(gwMapHeight * 32) + "}";
    // where the camera looks, as a cutscene's camera step names it
    double fX = 0.0;
    double fY = 0.0;
    PortViewCameraMiddle(&fX, &fY);
    char szView[96];
    sprintf_s(szView, ",\"view\":{\"x\":%d,\"y\":%d,\"zoom\":%.2f}", static_cast<int>(lround(fX)),
              static_cast<int>(lround(fY)), PortCameraZoom());
    s += szView;
    // the game's own view, its top left, which the camera may be away from; a centre-view
    // trigger's scroll toward its target, which holds the trigger until it arrives; and whether
    // the window counts as active, which that scroll needs to move
    int nToX, nToY;
    const bool bAuto = ScrollAutoTarget(&nToX, &nToY);
    s += ",\"scroll\":{\"x\":" + std::to_string(glScrollPixelX) + ",\"y\":" + std::to_string(glScrollPixelY) +
         ",\"to\":" + (bAuto ? "[" + std::to_string(nToX) + "," + std::to_string(nToY) + "]" : std::string("null")) +
         ",\"active\":" + (ScrollWindowActive() ? "true" : "false") + "}";
  }
  s += ",\"presenter\":" + PortJsonQuote(PortPresentGpu() ? PortGpuDriver() : "sdl");
  s += ",\"mod\":" + PortJsonQuote(PortMods()[PortModActive()].id);
  s += ",\"cutscene\":{" + PortCutsceneStatusJson() + "}";
  s += ",\"music\":" + std::to_string(PortMusicPlaying());  // the track playing, or -1
  s += ",\"map_swap\":" + PortJsonQuote(SwapState());
  if (!g_swapError.empty())
    s += ",\"map_swap_error\":" + PortJsonQuote(g_swapError);
  // The state's check of a Hyperion game or replay (port_hxreplay.h).
  static const char *const kSyncStates[] = {"none", "recording", "ok", "mismatch", "refused"};
  const ReplayHxSyncStatus *pSync = ReplayHxSync();
  s += ",\"sync\":{\"state\":" + PortJsonQuote(kSyncStates[pSync->nState]) + ",\"recorded\":" + std::to_string(pSync->dwRecorded) +
       ",\"checked\":" + std::to_string(pSync->dwChecked) + ",\"first_bad\":" + std::to_string(pSync->dwFirstBad) +
       ",\"domains\":" + std::to_string(pSync->dwBadDomains) + ",\"reason\":" + PortJsonQuote(pSync->szReason) + "}";
  return s;
}

std::string SnapPath(const PortJson &j) {
  std::string name = j["name"].Str();
  for (char &c : name)
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.'))
      c = '_';
  if (name.empty() || name[0] == '.')
    name = "frame-" + std::to_string(gwGameMode == 3 ? oc_dwReplayFrame : 0);
  if (name.size() < 4 || _stricmp(name.c_str() + name.size() - 4, ".png") != 0)
    name += ".png";
  const std::string folder = std::string(PortDataFolder()) + "snapshots\\";
  CreateDirectoryA(folder.c_str(), NULL);
  return folder + name;
}

// mod_push: a mod's data sent again in the canvas game, as a new submission; then, when
// asked, the units of earlier submissions turned into the newest of their type
// (CUnit::TransformType, as a morph turns them).
void ModPush(int nClient, const std::string &id, const PortJson &j) {
  if (gwGameMode != 3 || !PortLaunchIsCanvas()) {
    Send(nClient, Error(id, "mod_push needs Debug Tools' canvas game"));
    return;
  }
  const std::string name = j["mod"].Str();
  const PortMod *pMod = nullptr;
  for (const PortMod &mod : PortMods())
    if (mod.id == name && !mod.data.empty())
      pMod = &mod;
  std::string folder = j["folder"].Str();
  if (pMod == nullptr && folder.empty()) {
    Send(nClient, Error(id, "mod_push needs mod, one of the mods that bring data (mods lists them), or a folder"));
    return;
  }
  if (folder.empty())
    folder = pMod->folder + pMod->data;
  std::string types, script;
  const PortJson &t = j["types"];
  if (t.kind == PortJson::OBJECT)
    types = PortJsonWrite(t);
  else if (t.kind == PortJson::STRING)
    types = t.s;
  if (j["iscript"].kind == PortJson::STRING)
    script = j["iscript"].s;
  std::string answer;
  if (!ModDataPush(folder.c_str(), name.empty() ? "pushed" : name.c_str(), types.empty() ? nullptr : &types,
                   j["iscript"].kind == PortJson::STRING ? &script : nullptr, &answer)) {
    Send(nClient, Error(id, answer));
    return;
  }
  g_bChanged = true;
  // which units take the newest: none, the submission before, all earlier, or one by number
  const int nLast = ModDataLastSubmission();
  const PortJson &rep = j["replace"];
  int nFrom = -1;  // -1 none, 0 every earlier one, else that one
  if (rep.kind == PortJson::STRING && rep.s == "last")
    nFrom = nLast - 1;
  else if (rep.kind == PortJson::STRING && rep.s == "all")
    nFrom = 0;
  else if (rep.kind == PortJson::NUMBER)
    nFrom = static_cast<int>(rep.d);
  int nReplaced = 0;
  if (nFrom >= 0)
    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
      const int nSub = ModUnitSubmission(pUnit->wType);
      if (nSub == 0 || nSub == nLast || (nFrom > 0 && nSub != nFrom))
        continue;
      const int nNewest = ModUnitNewest(pUnit->wType);
      if (nNewest >= 0 && nNewest != pUnit->wType) {
        pUnit->TransformType(static_cast<WORD>(nNewest));
        ++nReplaced;
      }
    }
  Send(nClient, Answer(id, answer + ",\"replaced\":" + std::to_string(nReplaced)));
}

// The game's lines on screen now, without their colour codes.
std::vector<std::string> ScreenLines() {
  const char *apsz[16];
  const int n = MapMsgLines(apsz, 16);
  std::vector<std::string> lines;
  for (int i = 0; i < n; ++i) {
    std::string line;
    for (const char *p = apsz[i]; *p != '\0'; ++p)
      if (static_cast<unsigned char>(*p) >= 0x20)
        line += *p;
    lines.push_back(line);
  }
  return lines;
}

void Spawn(int nClient, const std::string &id, const PortJson &j) {
  // a number, or a name: one of 1.17's units as .hxm maps name them, or a mod's
  const int nUnit = j["unit"].kind == PortJson::STRING ? ModTypeByName("units", j["unit"].s.c_str())
                                                       : static_cast<int>(j["unit"].Num(-1));
  const int x = static_cast<int>(j["x"].Num(-1)), y = static_cast<int>(j["y"].Num(-1));
  const int nPlayer = static_cast<int>(j["player"].Num(0));
  const int nCount = static_cast<int>(j["count"].Num(1));
  if (!PortTrigCreateFits(nUnit, x, y, nPlayer, nCount)) {
    Send(nClient, Error(id, "spawn needs unit (0-227, a mod's from 256, or a name), x and y on the map, a player slot of the game, count 1-64"));
    return;
  }
  // made as Create Unit makes them (port_trigrun.cpp), then attack-moved or moved
  // (pathbench) as Order does; the lines the game puts on screen meanwhile are what it said,
  // such as a building it could not place
  const std::vector<std::string> before = ScreenLines();
  std::vector<int> made;
  PortTrigCreate(nUnit, x, y, nPlayer, nCount, &made);
  std::string said;
  for (const std::string &line : ScreenLines())
    if (std::find(before.begin(), before.end(), line) == before.end())
      said += (said.empty() ? "" : ",") + PortJsonQuote(line.c_str());
  const PortJson &to = j["attack"].size() == 2 ? j["attack"] : j["move"];
  if (to.size() == 2)
    PortTrigOrderNear(nPlayer, x, y, 64, static_cast<int>(to.At(0).Num()), static_cast<int>(to.At(1).Num()),
                      j["attack"].size() == 2 ? PORT_TRIG_ATTACK : PORT_TRIG_MOVE);
  std::string ids;
  for (const int n : made)
    ids += (ids.empty() ? "" : ",") + std::to_string(n);
  Send(nClient, Answer(id, "\"frame\":" + std::to_string(oc_dwReplayFrame) + ",\"ids\":[" + ids + "]" +
                               (said.empty() ? "" : ",\"said\":[" + said + "]")));
}

// Units by their index in the table, for pathbench: where each is, its order
// and its move target, or that it is gone.
std::string ModUnitJson(const CUnit *pUnit);

std::string Units(const PortJson &j) {
  const PortJson &ids = j["ids"];
  std::vector<CUnit *> used;
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
    used.push_back(pUnit);
  std::sort(used.begin(), used.end());
  std::string s = "\"frame\":" + std::to_string(oc_dwReplayFrame) + ",\"units\":[";
  for (int i = 0; i < ids.size(); i++) {
    const int n = static_cast<int>(ids.At(i).Num(-1));
    CUnit *pUnit = n < 0 ? NULL : CUnit::Head() + n;
    s += i ? "," : "";
    if (pUnit == NULL || !std::binary_search(used.begin(), used.end(), pUnit)) {
      s += "{\"id\":" + std::to_string(n) + ",\"alive\":false}";
      continue;
    }
    s += "{\"id\":" + std::to_string(n) + ",\"alive\":true,\"type\":" + std::to_string(pUnit->wType) +
         ",\"player\":" + std::to_string(pUnit->bPlayer) + ",\"x\":" + std::to_string(pUnit->wPositionX) +
         ",\"y\":" + std::to_string(pUnit->wPositionY) + ",\"order\":" + std::to_string(pUnit->bOrderId) +
         ",\"to\":[" + std::to_string(pUnit->wMoveTargetX) + "," + std::to_string(pUnit->wMoveTargetY) + "]" +
         ",\"hp\":" + std::to_string(pUnit->lHitPoints >> 8) + ",\"shields\":" + std::to_string(pUnit->lShields >> 8) +
         ",\"energy\":" + std::to_string(pUnit->wEnergy >> 8) + ",\"paralyzed\":" + std::to_string(pUnit->bParalyzeTimer) +
         ModUnitJson(pUnit) + "}";
  }
  return s + "]";
}

// A mod's unit's name and submission, as JSON members, for a pushed unit; "" else.
std::string ModUnitJson(const CUnit *pUnit) {
  const int nSub = ModUnitSubmission(pUnit->wType);
  return nSub > 0 ? ",\"name\":" + PortJsonQuote(ModUnitName(pUnit->wType)) + ",\"submission\":" + std::to_string(nSub) : "";
}

// units without ids: the units on the map, a player's or a type's, max of them listed.
std::string UnitList(const PortJson &j) {
  const int nPlayer = static_cast<int>(j["player"].Num(-1));
  const int nType = j["type"].kind == PortJson::STRING ? ModTypeByName("units", j["type"].s.c_str())
                                                       : static_cast<int>(j["type"].Num(-1));
  const int nMax = static_cast<int>(j["max"].Num(500));
  std::string s;
  int nCount = 0, nListed = 0;
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
    if ((nPlayer >= 0 && pUnit->bPlayer != nPlayer) || (nType >= 0 && pUnit->wType != nType))
      continue;
    if (++nCount > nMax)
      continue;
    ++nListed;
    s += (s.empty() ? "{\"id\":" : ",{\"id\":") + std::to_string(pUnit - CUnit::Head()) + ",\"type\":" +
         std::to_string(pUnit->wType) + ",\"player\":" + std::to_string(pUnit->bPlayer) + ",\"x\":" +
         std::to_string(pUnit->wPositionX) + ",\"y\":" + std::to_string(pUnit->wPositionY) + ",\"order\":" +
         std::to_string(pUnit->bOrderId) + ",\"hp\":" + std::to_string(pUnit->lHitPoints >> 8) + ",\"hpMax\":" +
         std::to_string(pUnit->GetLife()) + ",\"shields\":" + std::to_string(pUnit->lShields >> 8) +
         ",\"energy\":" + std::to_string(pUnit->wEnergy >> 8) + ",\"speed\":" + std::to_string(pUnit->lTopSpeed) +
         (UnitTypeValid(pUnit->wType) ? ",\"flingy\":" + std::to_string(gubUnitFlingy[pUnit->wType]) +
                                            ",\"weapons\":[" + std::to_string(pUnit->GetWeaponGnd()) + "," +
                                            std::to_string(pUnit->GetWeaponAir()) + "]"
                                      : std::string()) +
         ModUnitJson(pUnit) +
         // and what a mineral field, geyser or refinery holds (a mutator's share)
         (pUnit->IsMineralSrc() || pUnit->wType == 0xBC || pUnit->IsRefinery()
              ? ",\"resources\":" + std::to_string(pUnit->wResourceAmount)
              : std::string()) +
         // and a rich one, or a worker's rich load (rich bases)
         (RichBasesInGame() && RichBasesResource(static_cast<int>(pUnit - CUnit::Head())) ? ",\"rich\":true" : "") +
         (RichBasesInGame() && pUnit->bCarryResource != 0 && RichBasesCarry(static_cast<int>(pUnit - CUnit::Head()))
              ? ",\"richLoad\":true"
              : "") +
         // and the time left to make one not yet made, in frames
         ((pUnit->dwFlags & 1) == 0 ? ",\"left\":" + std::to_string(pUnit->wRemainingBuildTime) : std::string()) +
         "}";
  }
  return "\"frame\":" + std::to_string(oc_dwReplayFrame) + ",\"count\":" + std::to_string(nCount) + ",\"units\":[" +
         s + "]";
}

// The benchmark overlay's read (port_benchinfo.h): with "ids" (unit indices) or "player" (its
// first units, "max" of them) the selection set first; then the selected units and the players.
std::string BenchInfo(const PortJson &j) {
  if (j["ids"].kind == PortJson::ARRAY) {
    int an[PORT_BENCH_SELECT_MAX];
    int n = 0;
    for (int i = 0; i < j["ids"].size() && n < PORT_BENCH_SELECT_MAX; i++)
      an[n++] = static_cast<int>(j["ids"].At(i).Num(-1));
    PortBenchSelectSet(an, n);
  } else if (j["player"].kind == PortJson::NUMBER) {
    const int nPlayer = static_cast<int>(j["player"].Num(-1));
    const int nMax = static_cast<int>(j["max"].Num(PORT_BENCH_SELECT_MAX));
    int an[PORT_BENCH_SELECT_MAX];
    int n = 0;
    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL && n < nMax && n < PORT_BENCH_SELECT_MAX;
         pUnit = pUnit->pNext)
      if (pUnit->bPlayer == nPlayer)
        an[n++] = static_cast<int>(pUnit - CUnit::Head());
    PortBenchSelectSet(an, n);
  }
  PortBenchUnit au[PORT_BENCH_SELECT_MAX];
  const int nUnits = PortBenchSelected(au, PORT_BENCH_SELECT_MAX);
  std::string s = "\"frame\":" + std::to_string(oc_dwReplayFrame) + ",\"selected\":[";
  for (int i = 0; i < nUnits; i++) {
    const PortBenchUnit &u = au[i];
    std::string queue, cargo;
    for (int q = 0; q < u.nQueue; q++)
      queue += (q ? "," : "") + std::to_string(u.anQueue[q]);
    for (int c = 0; c < u.nCargo; c++)
      cargo += (c ? "," : "") + std::to_string(u.anCargo[c]);
    s += std::string(i ? "," : "") + "{\"id\":" + std::to_string(u.nIndex) + ",\"type\":" + std::to_string(u.nType) +
         ",\"player\":" + std::to_string(u.nPlayer) + ",\"name\":" + PortJsonQuote(u.szName) + ",\"hp\":[" +
         std::to_string(u.nHp) + "," + std::to_string(u.nHpMax) + "],\"shields\":[" + std::to_string(u.nShields) +
         "," + std::to_string(u.nShieldsMax) + "],\"energy\":[" + std::to_string(u.nEnergy) + "," +
         std::to_string(u.nEnergyMax) + "],\"kills\":" + std::to_string(u.nKills) + ",\"weapon\":" +
         (u.bHasWeapon ? "true" : "false") + ",\"armour\":[" + std::to_string(u.nArmour) + "," +
         std::to_string(u.nArmourUp) + "],\"weapon_up\":" + std::to_string(u.nWeaponUp) + ",\"building\":" +
         (u.bBuilding ? "true" : "false") + ",\"worker\":" + (u.bWorker ? "true" : "false") + ",\"transport\":" +
         (u.bTransport ? "true" : "false") + ",\"queue\":[" + queue + "],\"progress\":" +
         std::to_string(u.nProgressPercent) + ",\"larvae\":" + std::to_string(u.nLarvae) + ",\"cargo\":[" + cargo +
         "]}";
  }
  PortBenchPlayer ap[PLAYER_SLOTS_MAX];
  const int nPlayers = PortBenchPlayersRead(ap, PLAYER_SLOTS_MAX);
  s += "],\"players\":[";
  for (int i = 0; i < nPlayers; i++) {
    const PortBenchPlayer &p = ap[i];
    s += std::string(i ? "," : "") + "{\"slot\":" + std::to_string(p.nSlot) + ",\"race\":" + std::to_string(p.nRace) +
         ",\"team\":" + std::to_string(p.nTeam) + ",\"level\":" + PortJsonQuote(AiLevelName(AiProfileOf(p.nSlot).bLevel)) +
         ",\"gathered\":[" + std::to_string(p.nMineralsGathered) + "," + std::to_string(p.nGasGathered) +
         "],\"bank\":[" + std::to_string(p.nMinerals) + "," + std::to_string(p.nGas) + "],\"supply\":[" +
         std::to_string(p.nSupplyUsed) + "," + std::to_string(p.nSupplyMax) + "],\"units\":" +
         std::to_string(p.nUnits) + ",\"in\":" + (p.bLeft ? "true" : "false") + ",\"out_frame\":" +
         std::to_string(p.nOutFrame) + "}";
  }
  return s + "]";
}

// The pathing's shape and its counters (h/port_pathstats.h).
std::string PathStatsJson() {
  const ep_glob_t *pGlob = static_cast<const ep_glob_t *>(epGlob);
  int nRegions = 0, nAreas = 0, nMaxPaths = 0;
  std::string contours = "[]";
  if (pGlob != NULL) {
    nRegions = pGlob->nRegions;
    for (int i = 0; i < nRegions; i++) {
      const ep_region_t &r = pGlob->aRegions[i];
      if (r.wArea < 0x4000 && r.wArea > nAreas)
        nAreas = r.wArea;
      if (r.bPathCount > nMaxPaths)
        nMaxPaths = r.bPathCount;
    }
    if (pGlob->pContours != NULL) {
      const contours_t *c = pGlob->pContours;
      contours = "[" + std::to_string(c->nCounts[0]) + "," + std::to_string(c->nCounts[1]) + "," +
                 std::to_string(c->nCounts[2]) + "," + std::to_string(c->nCounts[3]) + "]";
    }
  }
  const PathStats &p = g_pathStats;
  char szMs[32];
  sprintf_s(szMs, "%.1f", p.fBuildMs);
  return "\"frame\":" + std::to_string(oc_dwReplayFrame) + ",\"map\":[" + std::to_string(gwMapWidth) + "," +
         std::to_string(gwMapHeight) + "],\"regions\":" + std::to_string(nRegions) + ",\"areas\":" +
         std::to_string(nAreas) + ",\"max_neighbours\":" + std::to_string(nMaxPaths) + ",\"contours\":" + contours +
         ",\"build_ms\":" + szMs + ",\"area_searches\":" + std::to_string(p.nArea) + ",\"area_capped\":" +
         std::to_string(p.nAreaCapped) + ",\"area_short\":" + std::to_string(p.nAreaShort) +
         ",\"local_searches\":" + std::to_string(p.nLocal) + ",\"local_capped\":" + std::to_string(p.nLocalCapped) +
         ",\"local_short\":" + std::to_string(p.nLocalShort) + ",\"local_nodes\":" + std::to_string(p.nLocalNodes);
}

// select: the slot's units in the square round (x, y), up to 12, as a drag box over them
// selects them (usel_drag_select's gather, sub_4BC5B0): its own units, its buildings left out
// when units are there; else, or for another slot, one unit. A test of what a selection draws;
// with "sound" true, the selection's voice plays as a click's does (gamesnd_select).
void Select(int nClient, const std::string &id, const PortJson &j) {
  const int x = static_cast<int>(j["x"].Num(-1)), y = static_cast<int>(j["y"].Num(-1));
  const bool bSound = j["sound"].kind == PortJson::BOOL && j["sound"].b;
  const int nRadius = static_cast<int>(j["radius"].Num(96));
  const int nPlayer = static_cast<int>(j["player"].Num(0));
  mrect_t rc;
  rc.left = static_cast<spos_t>(x > nRadius ? x - nRadius : 0);
  rc.top = static_cast<spos_t>(y > nRadius ? y - nRadius : 0);
  rc.right = static_cast<spos_t>(x + nRadius > 0 ? x + nRadius : 0);
  rc.bottom = static_cast<spos_t>(y + nRadius > 0 ? y + nRadius : 0);
  std::vector<CUnit *> owned;
  CUnit **ppIter = find_units_in_rect(&rc);
  for (CUnit **pp = ppIter; *pp != NULL; ++pp)
    if ((*pp)->bPlayer == nPlayer)
      owned.push_back(*pp);
  finish_unit_iteration(ppIter);
  owned.push_back(NULL);
  CUnit *apUnits[USEL_SELECT_CAPACITY] = {};
  const int nCount = sub_4BC5B0(owned.data(), apUnits, NULL);
  if (nCount != 0)
    select_units(nCount, apUnits, bSound ? 1 : 0, 1);
  Send(nClient, Answer(id, "\"selected\":" + std::to_string(nCount)));
}

// The player's alliance row with the slots in "with" allied, sent as 1.17's diplomacy
// dialog sends it (a test of what follows alliances, such as Settings' ally colours).
void Allies(int nClient, const std::string &id, const PortJson &j) {
  const int nLocal = static_cast<int>(g_LocalNationID);
  DWORD dwAllies = 0;
  for (int i = 0; i < 12; i++) {
    DWORD dw = i == nLocal ? 2 : 0;
    const PortJson &w = j["with"];
    for (size_t k = 0; k < w.items.size(); k++)
      if (static_cast<int>(w.items[k].Num(-1)) == i)
        dw = 2;
    dwAllies |= dw << (2 * i);
  }
  send_net_cmd_SetAllies(dwAllies);
  Send(nClient, Answer(id, "\"local\":" + std::to_string(nLocal) + ",\"allies\":" + std::to_string(dwAllies)));
}

// Each slot in use, from the game's own tables, for a test of Hyperion's slots.
std::string Players() {
  std::string s = "\"players\":[";
  bool bFirst = true;
  for (int i = 0; i < PlayerSlots(); i++) {
    const PlayerData &p = gPlayerData[i];
    int nUnits = 0, nOthersSeen = 0;
    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
      nUnits += pUnit->bPlayer == i;
      nOthersSeen += pUnit->bPlayer != i && pUnit->bPlayer != PLAYER_NEUTRAL && SpriteSeenBy(pUnit->pSprite, i);
    }
    if (p.nType == 0 && nUnits == 0)
      continue;
    int nSeen = 0, nExplored = 0;
    if (i != PLAYER_NEUTRAL) {
      for (int t = gwMapWidth * gwMapHeight; t-- != 0;) {
        nSeen += VisionHxTileSeen(t, i);
        nExplored += VisionHxTileExplored(t, i);
      }
    }
    char szName[26];
    memcpy(szName, p.szName, 25);
    szName[25] = 0;
    const WORD *pStart = i < PLAYER_SLOTS_MAX && i != PLAYER_NEUTRAL
                             ? reinterpret_cast<const WORD *>(&gStartLocations[i]) : NULL;
    // the abilities researched and the upgrades' levels added up, a mod's too
    int nResearched = 0, nLevels = 0;
    for (int t = 0; t < g_hxTypes.techs; t++)
      nResearched += TechTypeValid(t) && get_tech_researched(i, static_cast<WORD>(t)) != 0;
    for (int u = 0; u < g_hxTypes.upgrades; u++)
      nLevels += UpgradeTypeValid(u) ? get_upgrade_level(i, static_cast<WORD>(u)) : 0;
    // the race's supply in halves: used, given up to the cap, given, and the cap
    const BYTE bRace = p.nRace <= 2 ? p.nRace : 0;
    const std::string supply = "[" + std::to_string(count_get_supplies_used(bRace, static_cast<BYTE>(i))) + "," +
                               std::to_string(count_get_supplies_available(bRace, static_cast<BYTE>(i))) + "," +
                               std::to_string(glGameCounts_g[18 + 3 * bRace][i]) + "," +
                               std::to_string(glGameCounts_g[20 + 3 * bRace][i]) + "]";
    s += std::string(bFirst ? "" : ",") + "{\"slot\":" + std::to_string(i) + ",\"type\":" +
         std::to_string(p.nType) + ",\"race\":" + std::to_string(p.nRace) + ",\"name\":" + PortJsonQuote(szName) +
         ",\"minerals\":" + std::to_string(gPlayerMinerals[i]) + ",\"gas\":" + std::to_string(gPlayerGas[i]) +
         ",\"supply\":" + supply + ",\"tech\":[" + std::to_string(nResearched) + "," + std::to_string(nLevels) +
         "],\"start\":[" + std::to_string(pStart ? pStart[0] : 0) + "," + std::to_string(pStart ? pStart[1] : 0) +
         "],\"units\":" + std::to_string(nUnits) + ",\"seen\":" + std::to_string(nSeen) +
         ",\"explored\":" + std::to_string(nExplored) + ",\"others_seen\":" + std::to_string(nOthersSeen) +
         ",\"triggers\":" + std::to_string(TriggersCount(i)) +
         ",\"victory\":" + std::to_string(PlayerIsLobby(i) ? gbVictoryState[i] : 0) + "}";
    bFirst = false;
  }
  return s + "]";
}

// A command's call as Handle reads it: the client that sent it, its id for the answer, the name it
// came by, and the message.
struct Call {
  int nClient;
  std::string id;
  std::string cmd;
  const PortJson &j;
  void Ok(const std::string &body) const { Send(nClient, Answer(id, body)); }
  void Fail(const std::string &why) const { Send(nClient, Error(id, why)); }
};

void CmdStatus(const Call &r) {
  r.Ok(Status());
}

void CmdPlayers(const Call &r) {
  r.Ok(Players());
}

void CmdHash(const Call &r) {
  // The state's hash a domain at a time (port_synchash.h), and with "dump" a path, each
  // domain's bytes as hashed, to compare two runs.
  SyncHash h;
  SyncHashCompute(&h);
  std::string s = "\"frame\":" + std::to_string(h.dwFrame) + ",\"rand\":" + std::to_string(h.dwRandState) +
                  ",\"rand_count\":" + std::to_string(h.dwRandCount) + ",\"domains\":{";
  for (int d = 0; d < SYNC_DOMAINS; d++) {
    char sz[24];
    _snprintf(sz, sizeof sz, "%016llX", static_cast<unsigned long long>(h.aqDomain[d]));
    s += std::string(d ? "," : "") + "\"" + SyncDomainName(d) + "\":\"" + sz + "\"";
  }
  s += "}";
  const std::string dump = r.j["dump"].Str();
  if (!dump.empty())
    s += ",\"dumped\":" + std::string(SyncHashDump(dump.c_str()) ? "true" : "false");
  r.Ok(s);
}

void CmdList(const Call &r) {
  std::string s = "\"values\":[";
  for (size_t i = 0; i < Values().size(); ++i) {
    const Value &v = Values()[i];
    s += std::string(i ? "," : "") + "{\"name\":" + PortJsonQuote(v.name) + ",\"value\":" + Num(Get(v)) + ",\"min\":" +
         Num(v.fMin) + ",\"max\":" + Num(v.fMax) + ",\"default\":" + Num(v.fDefault) + ",\"step\":" + Num(v.fStep) +
         ",\"help\":" + PortJsonQuote(v.help) + "}";
  }
  r.Ok(s + "]");
}

void CmdValue(const Call &r) {
  const std::string name = r.j["name"].Str();
  for (const Value &v : Values())
    if (v.name == name) {
      if (r.cmd == "set") {
        if (r.j["value"].kind != PortJson::NUMBER) {
          r.Fail("set needs a number value");
          return;
        }
        Set(v, static_cast<float>(r.j["value"].d));
      }
      r.Ok("\"name\":" + PortJsonQuote(name) + ",\"value\":" + Num(Get(v)));
      return;
    }
  r.Fail("no value named " + name + "; list names them");
}

void CmdSnapshot(const Call &r) {
  if (!g_snapPath.empty()) {
    r.Fail("a snapshot is already being taken");
    return;
  }
  g_snapPath = SnapPath(r.j);
  g_nSnapClient = r.nClient;
  g_snapId = r.id;
  if (PortFrontActive()) {
    g_bSnapFront = true;
  } else if (PortPresentGpu()) {
    g_bSnapGpu = true;
    PortGpuSnapAsk(g_snapPath.c_str());
  } else {
    // the SDL presenter: 1.17's own screen, at the game's size
    if (PortScreenshot(g_snapPath.c_str()) != 0)
      r.Ok("\"path\":" + PortJsonQuote(g_snapPath));
    else
      r.Fail("the screen could not be saved");
    g_snapPath.clear();
  }
}

void CmdPauseStep(const Call &r) {
  if (r.cmd == "pause") {
    pause_game();
    g_bStepping = false;
    g_bPausedHere = true;
    r.Ok("\"frame\":" + std::to_string(oc_dwReplayFrame));
  } else if (r.cmd == "resume") {
    g_bStepping = false;
    g_bPausedHere = false;
    unpause_game(0);
    r.Ok("\"frame\":" + std::to_string(oc_dwReplayFrame));
  } else {
    const int n = static_cast<int>(r.j["frames"].Num(1));
    g_nStepTo = oc_dwReplayFrame + static_cast<unsigned>(n < 1 ? 1 : n > 10000 ? 10000 : n);
    g_bStepping = true;
    g_bStepFast = r.j["fast"].kind == PortJson::BOOL && r.j["fast"].b;
    g_nStepClient = r.nClient;
    g_stepId = r.id;
    unpause_game(0);
  }
}

void CmdReveal(const Call &r) {
  // the map's fog off for every player, as the cheat black sheep wall: a test's view
  glCheatBits |= 1;
  mask_disable_fog();
  r.Ok("");
}

void CmdColour(const Call &r) {
  // a slot's colour in full RGB (port_teamcolor.cpp): "rgb" "#rrggbb" sets it, and its
  // ramp for the 8-bit drawing; "" clears it, the ramp staying; none gives it. Presentation only.
  const int nSlot = static_cast<int>(r.j["slot"].Num(-1));
  if (nSlot < 0 || nSlot >= PLAYER_SLOTS_MAX) {
    r.Fail("colour needs a slot, 0 to " + std::to_string(PLAYER_SLOTS_MAX - 1));
    return;
  }
  if (r.j["rgb"].kind == PortJson::STRING) {
    const std::string sRgb = r.j["rgb"].Str();
    unsigned char rgb[3];
    if (sRgb.empty())
      PortTeamColorClear(nSlot);
    else if (PortTeamColorParse(sRgb.c_str(), rgb))
      PortTeamColorSet(nSlot, rgb[0], rgb[1], rgb[2], 1);
    else {
      r.Fail("colour takes rgb as #rrggbb");
      return;
    }
  }
  unsigned char rgb[3];
  char sz[16] = "";
  if (PortTeamColorGet(nSlot, rgb))
    snprintf(sz, sizeof sz, "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
  r.Ok("\"slot\":" + std::to_string(nSlot) + ",\"rgb\":" + PortJsonQuote(sz));
}

void CmdCamera(const Call &r) {
  const int x = static_cast<int>(r.j["x"].Num(0)), y = static_cast<int>(r.j["y"].Num(0));
  // a zoom too, when given; in a replay the camera goes there, not the game's view
  const double fZoom = r.j["zoom"].Num(0.0);
  if (fZoom > 0.0)
    PortCameraZoomTo(fZoom);
  if (gPlayback)
    PortViewCameraCentre(x, y);
  else
    gamemap_center_on_mtx(x / 32, y / 32);
  r.Ok("");
}

void CmdCutscenePlay(const Call &r) {
  std::string why;
  if (!PortCutscenePlay(r.j, &why)) {
    r.Fail(why);
    return;
  }
  r.Ok(PortCutsceneStatusJson());
}

void CmdCutsceneStop(const Call &r) {
  PortCutsceneStop();
  r.Ok(PortCutsceneStatusJson());
}

void CmdCutsceneStatus(const Call &r) {
  r.Ok(PortCutsceneStatusJson());
}

void CmdMapSwitch(const Call &r) {
  if (g_nSwap != SWAP_NONE) {
    r.Fail(std::string("a map is on its way already (") + SwapState() + ")");
    return;
  }
  if (gbMultiPlayerMode != 0 || (gwGameMode != 3 && !PortFrontActive())) {
    r.Fail(r.cmd + " needs a single-player game, a replay or Hyperion's front end");
    return;
  }
  std::string answer;
  if (r.cmd == "map_push") {
    PortCanvasMap map;
    std::string why;
    const std::string from = r.j["from"].Str("");
    if (!from.empty()) {
      if (!PushFromMap(from, &map, &why)) {
        r.Fail(why);
        return;
      }
    } else {
      map.tileset = r.j["tileset"].Str("badlands");
      map.nWidth = static_cast<int>(r.j["width"].Num(64));
      map.nHeight = static_cast<int>(r.j["height"].Num(64));
      // the size before the tiles: a fill asks for width x height cells, and a size past the
      // largest map the canvas plays overflows that count (PortCanvasCheck says which sizes)
      if (map.nWidth < 1 || map.nWidth > 256 || map.nHeight < 1 || map.nHeight > 256) {
        r.Fail("width and height are each 64, 96, 128, 192 or 256 tiles");
        return;
      }
      if (!PushTiles(r.j, map.nWidth * map.nHeight, &map.tiles, &why)) {
        r.Fail(why);
        return;
      }
    }
    for (const PortJson &rect : r.j["rects"].items)
      map.rects.push_back({static_cast<int>(rect["x"].Num()), static_cast<int>(rect["y"].Num()),
                           static_cast<int>(rect["w"].Num(1)), static_cast<int>(rect["h"].Num(1)),
                           static_cast<unsigned short>(rect["tile"].Num())});
    if (!PortCanvasCheck(map, &why)) {
      r.Fail(why);
      return;
    }
    PortCanvasPushNext(map);
    g_swapCanvas = map.tileset;
    g_swapMap.clear();
    answer = "\"tileset\":" + PortJsonQuote(map.tileset) + ",\"size\":[" + std::to_string(map.nWidth) + "," +
             std::to_string(map.nHeight) + "]";
    if (!from.empty())
      answer += ",\"from\":" + PortJsonQuote(from);
  } else {
    std::string path;
    if (!MapFilePath(r.j["path"].Str(""), &path)) {
      r.Fail("map_load needs path, a map file (.scm, .scx, .hxm) that is there: " + r.j["path"].Str(""));
      return;
    }
    g_swapCanvas.clear();
    g_swapMap = path;
    g_bSwapUms = r.j["ums"].kind == PortJson::BOOL && r.j["ums"].b;
    answer = "\"path\":" + PortJsonQuote(path);
  }
  // a new switch: the last one's error is no longer the news
  g_swapError.clear();
  g_bSwapReveal = r.j["reveal"].kind == PortJson::BOOL && r.j["reveal"].b;
  g_bSwapScene = r.j["scene"].kind == PortJson::OBJECT;
  if (g_bSwapScene)
    g_swapScene = r.j["scene"];
  if (gwGameMode == 3) {
    // out as a skirmish comes in, backwards: the console down, the map shrinking into black
    const double s = r.j["ms"].Num(1000.0) / 1000.0;
    PortCutsceneStop();
    PortMotionDepart(s > 0.0 ? s : 0.0);
    g_fSwapAt = PortMotionClock() + s;
    g_nSwap = SWAP_OUT;
  } else {
    SwapLeave();
  }
  r.Ok(answer + ",\"state\":" + PortJsonQuote(SwapState()));
}

void CmdCanvas(const Call &r) {
  if (!PortFrontActive()) {
    r.Fail("canvas starts from the front end's home screen");
    return;
  }
  g_canvasTileset = r.j["tileset"].Str(PORT_CANVAS_TILESET);
  const bool bBattle = r.j["battle"].kind == PortJson::BOOL && r.j["battle"].b;
  PortCanvasBattle(bBattle);
  g_bCanvasAsked = true;
  r.Ok("\"tileset\":" + PortJsonQuote(g_canvasTileset) + ",\"battle\":" + (bBattle ? "true" : "false"));
}

void CmdSpawn(const Call &r) {
  Spawn(r.nClient, r.id, r.j);
}

void CmdClear(const Call &r) {
  const int nPlayer = static_cast<int>(r.j["player"].Num(-1));
  if (gwGameMode != 3 || nPlayer < 0 || nPlayer >= PlayerSlots() || nPlayer == PLAYER_NEUTRAL) {
    r.Fail("clear needs a game and a player slot of it");
    return;
  }
  // every unit of the player removed, as Remove Unit does, for pathbench: a Use
  // Map Settings game's local player then has none but the next ones made
  r.Ok("\"removed\":" + std::to_string(PortTrigRemoveAll(nPlayer)));
}

void CmdUnits(const Call &r) {
  r.Ok(r.j["ids"].kind == PortJson::ARRAY ? Units(r.j) : UnitList(r.j));
}

void CmdBenchInfo(const Call &r) {
  r.Ok(BenchInfo(r.j));
}

void CmdRegion(const Call &r) {
  // the region and area of a point, for pathbench: a ground unit reaches only the points
  // of its own area, and a region of group 0x1FFD is not walkable
  const int x = static_cast<int>(r.j["x"].Num(-1)), y = static_cast<int>(r.j["y"].Num(-1));
  // the pathing's own map, which GetRegion_xy reads: a game mode of 3 is already set while a
  // pushed map is being made, and it is not there yet (SwapStep waits for the same pointer)
  const ep_glob_t *pGlob = static_cast<const ep_glob_t *>(epGlob);
  if (gwGameMode != 3 || pGlob == NULL || gpSquareMap == NULL || x < 0 || y < 0 || x >= gwMapWidth * 32 ||
      y >= gwMapHeight * 32) {
    r.Fail("region needs a game with its map ready and x and y on the map");
    return;
  }
  const WORD wRegion = GetRegion_xy(x, y);
  r.Ok("\"region\":" + std::to_string(wRegion) + ",\"group\":" + std::to_string(pGlob->aRegions[wRegion].wGroup) +
       ",\"area\":" + std::to_string(GetAreaIndex(wRegion)));
}

void CmdPathstats(const Call &r) {
  r.Ok(PathStatsJson());
}

void CmdTrace(const Call &r) {
  // Debug Tools' traces: {"what":"camera","on":true|false}; off writes the trace and
  // answers with its file and an account of it
  if (r.j["what"].Str("camera") != "camera") {
    r.Fail("the traces are: camera");
    return;
  }
  if (r.j["on"].kind == PortJson::BOOL && r.j["on"].b) {
    PortTraceCameraStart();
    r.Ok("\"tracing\":\"camera\"");
  } else {
    r.Ok(PortTraceCameraStop());
  }
}

void CmdTraceRows(const Call &r) {
  // the camera trace's rows from "since" on, the last 500 at most, while it runs
  const size_t nSince = static_cast<size_t>(r.j["since"].Num(0));
  r.Ok("\"count\":" + std::to_string(PortTraceCameraCount()) + ",\"on\":" + (PortTraceCameraOn() ? "true" : "false") +
       ",\"rows\":" + PortTraceCameraRows(nSince, 500));
}

void CmdMinimap(const Call &r) {
  // press the minimap at {"u","v"}, 0 to 1 across and down, or drag along
  // {"path":[[u,v],...]}, a point every "step_ms" (33 unless given)
  if (!InGame()) {
    r.Fail("minimap needs a game");
    return;
  }
  std::vector<std::pair<double, double>> path;
  const PortJson &p = r.j["path"];
  for (size_t i = 0; i < p.items.size(); ++i)
    path.push_back({p.items[i].At(0).Num(0.5), p.items[i].At(1).Num(0.5)});
  if (path.empty())
    path.push_back({r.j["u"].Num(0.5), r.j["v"].Num(0.5)});
  PortTraceMinimapPath(path, r.j["step_ms"].Num(33.0));
  r.Ok("\"points\":" + std::to_string(path.size()));
}

void CmdPerf(const Call &r) {
  // Debug Tools' live numbers, "on" to turn them on or off; each second from "since" on: frames
  // and game frames, units, the parts, the present's and scene's ms, speed, focus, replay or not
  if (r.j["on"].kind == PortJson::BOOL)
    PortPerfWant(r.j["on"].b);
  size_t nCount = 0;
  const std::string rows = PortPerfSecondsJson(static_cast<size_t>(r.j["since"].Num(0)), &nCount);
  std::string parts = "[";
  for (int i = 0; i < PERF_PARTS; ++i)
    parts += std::string(i ? "," : "") + "\"" + PortBenchPartName(i) + "\"";
  r.Ok(std::string("\"on\":") + (PortPerfWanted() ? "true" : "false") + ",\"count\":" + std::to_string(nCount) +
       ",\"names\":" + parts + "],\"seconds\":" + rows);
}

void CmdSettings(const Call &r) {
  // Settings' rows that take a value, as the dialog has them (port_front.cpp)
  r.Ok(PortSettingsLiveJson());
}

void CmdSetting(const Call &r) {
  // one of them changed now, applied and saved as the dialog's Accept does: {name, value,
  // on?}, a choice or steps by index, a slider 0 to 1, a toggle 0 or 1
  if (r.j["value"].kind != PortJson::NUMBER) {
    r.Fail("setting needs name and a number value; settings lists them");
    return;
  }
  std::string answer;
  const int nOn = r.j["on"].kind == PortJson::BOOL ? (r.j["on"].b ? 1 : 0) : -1;
  if (PortSettingLiveSet(r.j["name"].Str(), r.j["value"].d, nOn, &answer))
    r.Ok(answer);
  else
    r.Fail(answer);
}

void CmdMods(const Call &r) {
  std::string s;
  for (const PortMod &mod : PortMods())
    if (!mod.data.empty())
      s += (s.empty() ? "{" : ",{") + std::string("\"id\":") + PortJsonQuote(mod.id) + ",\"name\":" + PortJsonQuote(mod.name) +
           ",\"folder\":" + PortJsonQuote(mod.folder + mod.data) + "}";
  r.Ok("\"mods\":[" + s + "],\"loaded\":" + PortJsonQuote(ModDataId()));
}

void CmdModPush(const Call &r) {
  ModPush(r.nClient, r.id, r.j);
}

void CmdModSubmissions(const Call &r) {
  r.Ok(ModDataSubmissionsJson());
}

void CmdTrainer(const Call &r) {
  // the trainer's session, and a key of the trainer's pressed with the modifiers
  // given, which a posted key cannot carry: {"key":"R","ctrl":true}, or a virtual key's number
  if (r.j["key"].kind != PortJson::NONE) {
    const std::string key = r.j["key"].Str();
    unsigned vk = key.size() == 1 ? static_cast<unsigned>(toupper(static_cast<unsigned char>(key[0])))
                                  : static_cast<unsigned>(r.j["key"].Num(0));
    if (key == "space")
      vk = VK_SPACE;
    else if (key == "enter")
      vk = VK_RETURN;
    else if (key == "left")
      vk = VK_LEFT;
    else if (key == "right")
      vk = VK_RIGHT;
    else if (key == "up")
      vk = VK_UP;
    else if (key == "down")
      vk = VK_DOWN;
    else if (key == "escape")
      vk = VK_ESCAPE;
    else if (key == "[")
      vk = VK_OEM_4;
    else if (key == "]")
      vk = VK_OEM_6;
    else if (key == "+")
      vk = VK_OEM_PLUS;
    else if (key == "-")
      vk = VK_OEM_MINUS;
    const bool bTaken = PortTrainerViewKeyAs(vk, r.j["ctrl"].kind == PortJson::BOOL && r.j["ctrl"].b,
                                             r.j["shift"].kind == PortJson::BOOL && r.j["shift"].b);
    r.Ok(std::string("\"taken\":") + (bTaken ? "true" : "false") + ",\"tape\":" +
         (PortTrainerViewTape() ? "true" : "false") + "," + PortTrainerJson());
    return;
  }
  r.Ok(std::string("\"tape\":") + (PortTrainerViewTape() ? "true" : "false") + "," + PortTrainerJson());
}

void CmdOverlay(const Call &r) {
  const std::string mode = r.j["mode"].Str();
  static const char *const kModes[] = {"off", "window", "full", "counter"};
  for (int i = 0; i < 4; ++i)
    if (mode == kModes[i])
      PortDebugViewSet(i);
  if (!mode.empty() && mode != PortDebugViewModeName()) {
    r.Fail("overlay's mode is off, window, full or counter");
    return;
  }
  r.Ok("\"mode\":" + PortJsonQuote(PortDebugViewModeName()));
}

void CmdPress(const Call &r) {
  std::string key = r.j["key"].Str();
  unsigned vk = static_cast<unsigned>(r.j["vk"].Num(0));
  if (key.size() == 1)
    vk = static_cast<unsigned>(toupper(static_cast<unsigned char>(key[0])));
  else if (key == "enter")
    vk = VK_RETURN;
  else if (key == "escape")
    vk = VK_ESCAPE;
  else if (key == "tab")
    vk = VK_TAB;
  else if (key == "up" || key == "down" || key == "left" || key == "right")
    vk = key == "up" ? VK_UP : key == "down" ? VK_DOWN : key == "left" ? VK_LEFT : VK_RIGHT;
  if (vk == 0) {
    r.Fail("press needs a key or a vk");
  } else {
    void *hWnd = WinMsgWindowHandle();
    WinMsgPost(hWnd, WM_KEYDOWN, vk, 1);
    const std::string ch = r.j["char"].Str();
    if (!ch.empty())
      WinMsgPost(hWnd, WM_CHAR, static_cast<unsigned char>(ch[0]), 1);
    WinMsgPost(hWnd, WM_KEYUP, vk, static_cast<LONG_PTR>(0xC0000001u));
    r.Ok("\"vk\":" + std::to_string(vk));
  }
}

void CmdClick(const Call &r) {
  const int x = static_cast<int>(r.j["x"].Num()), y = static_cast<int>(r.j["y"].Num());
  const bool bRight = r.j["right"].kind == PortJson::BOOL && r.j["right"].b;
  const bool bMove = r.j["move"].kind == PortJson::BOOL && r.j["move"].b;  // the pointer there, no press
  const bool bDouble = r.j["double"].kind == PortJson::BOOL && r.j["double"].b;  // a second press, a double click
  const LONG_PTR lp = static_cast<LONG_PTR>((y & 0xFFFF) << 16 | (x & 0xFFFF));
  void *hWnd = WinMsgWindowHandle();
  // marked as a test's, so a background window run with HYPERION_TEST_MOUSE=1 takes it
  WinMsgPost(hWnd, WM_MOUSEMOVE, PORT_TEST_MOUSE, lp);
  // the wheel there, 120 a notch away from the player
  const int nWheel = static_cast<int>(r.j["wheel"].Num());
  if (nWheel != 0)
    WinMsgPost(hWnd, WM_MOUSEWHEEL, (static_cast<UINT_PTR>(static_cast<WORD>(nWheel)) << 16) | PORT_TEST_MOUSE, lp);
  if (bMove || nWheel != 0) {
    r.Ok("\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y));
    return;
  }
  // half a press, for a drag: "phase" down, moves with "move", then up
  const std::string phase = r.j["phase"].Str();
  if (phase == "down" || phase == "up") {
    if (phase == "down")
      WinMsgPost(hWnd, bRight ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, (bRight ? MK_RBUTTON : MK_LBUTTON) | PORT_TEST_MOUSE, lp);
    else
      WinMsgPost(hWnd, bRight ? WM_RBUTTONUP : WM_LBUTTONUP, PORT_TEST_MOUSE, lp);
    r.Ok("\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y));
    return;
  }
  WinMsgPost(hWnd, bRight ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, (bRight ? MK_RBUTTON : MK_LBUTTON) | PORT_TEST_MOUSE, lp);
  WinMsgPost(hWnd, bRight ? WM_RBUTTONUP : WM_LBUTTONUP, PORT_TEST_MOUSE, lp);
  if (bDouble) {
    // as Windows sends a window with CS_DBLCLKS the second press of a pair
    WinMsgPost(hWnd, bRight ? WM_RBUTTONDBLCLK : WM_LBUTTONDBLCLK, (bRight ? MK_RBUTTON : MK_LBUTTON) | PORT_TEST_MOUSE,
               lp);
    WinMsgPost(hWnd, bRight ? WM_RBUTTONUP : WM_LBUTTONUP, PORT_TEST_MOUSE, lp);
  }
  r.Ok("\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y));
}

void CmdClock(const Call &r) {
  // Debug Tools' game clock: {"on":true} starts it afresh, {"on":false} stops it; the
  // readings either way (port_clock.cpp)
  if (r.j["on"].kind == PortJson::BOOL) {
    if (r.j["on"].b)
      PortClockStart();
    else
      PortClockStop();
  }
  r.Ok(PortClockJson());
}

void CmdScroll(const Call &r) {
  // hold a scroll key: {"dir":"left|right|up|down|up-left|up-right|down-left|down-right",
  // "ms":1000}; the camera trace starts if it was not, and every pass is a row while held
  if (!InGame()) {
    r.Fail("scroll needs a game");
    return;
  }
  static const struct {
    const char *pszDir;
    int nKey;
  } kDirs[] = {{"left", VK_LEFT},           {"right", VK_RIGHT},         {"up", VK_UP},
               {"down", VK_DOWN},           {"up-left", VK_NUMPAD7},     {"up-right", VK_NUMPAD9},
               {"down-left", VK_NUMPAD1},   {"down-right", VK_NUMPAD3}};
  const std::string dir = r.j["dir"].Str("right");
  int nKey = 0;
  for (const auto &d : kDirs)
    if (dir == d.pszDir)
      nKey = d.nKey;
  if (nKey == 0) {
    r.Fail("dir is left, right, up, down or a diagonal such as up-left");
    return;
  }
  const double dMs = r.j["ms"].Num(1000.0);
  PortTraceScroll(nKey, dMs);
  r.Ok("\"scrolling\":\"" + dir + "\",\"ms\":" + std::to_string(static_cast<int>(dMs)));
}

void CmdSelect(const Call &r) {
  Select(r.nClient, r.id, r.j);
}

void CmdAllies(const Call &r) {
  Allies(r.nClient, r.id, r.j);
}

void CmdDialogs(const Call &r) {
  static char s_sz[16384];
  PortViewDialogs(s_sz, sizeof s_sz);
  r.Ok(std::string("\"dialogs\":[") + s_sz + "]");
}

void CmdMaps(const Call &r) {
  r.Ok(MapsJson(r.j["filter"].Str(""), static_cast<size_t>(r.j["max"].Num(2000)),
                r.j["fresh"].kind == PortJson::BOOL && r.j["fresh"].b));
}

void CmdMapInfo(const Call &r) {
  std::string body, why;
  if (MapInfoJson(r.j["path"].Str(""), &body, &why))
    r.Ok(body);
  else
    r.Fail("path " + why);
}

void CmdLights(const Call &r) {
  r.Ok(PortLightsJson());
}

void CmdLight(const Call &r) {
  std::string why;
  if (PortLightSet(r.j, &why)) {
    PortPresentRedraw();
    r.Ok("");
  } else {
    r.Fail(why);
  }
}

void CmdLightsSave(const Call &r) {
  std::string path;
  if (PortLightsSave(&path))
    r.Ok("\"path\":" + PortJsonQuote(path));
  else
    r.Fail("could not write " + path);
}

// What a command needs before its handler runs; the error names the command.
enum Need {
  NEED_NOTHING,
  NEED_GAME,    // gwGameMode 3
  NEED_SINGLE,  // a single-player game (InGame)
};

struct Command {
  const char *pszName;
  Need need;
  void (*pfnRun)(const Call &r);
};

// The cursor too: its id (cursor.cpp's table, 6 to 8 the magnifier over a unit) and where it is in
// the game's pixels, so a test can tell what the pointer shows over the map and the panels.
void CmdHud(const Call &r) {
  char sz[96];
  SDL_snprintf(sz, sizeof sz, ",\"cursor\":{\"id\":%d,\"x\":%ld,\"y\":%ld}", cursor_get(),
               static_cast<long>(gCursorPos_g[0]), static_cast<long>(gCursorPos_g[1]));
  r.Ok(PortHudJson() + "," + PortHudHitsJson() + sz);
}

void CmdHudEvents(const Call &r) {
  r.Ok(PortHudEventsJson(static_cast<unsigned int>(r.j["since"].Num(0))));
}

// A press the HUD would make: the card's place 0 to 8, a button by its name (menu, terrain,
// diplomacy, messaging, idle, army, and a replay's play, slower, faster, tv and follow), a control
// group 0 to 9, or a replay's player's eye by slot, each as 1.17's own would.
void CmdHudPress(const Call &r) {
  const char *pszWhy = "hud_press needs a place, a button, a group or an eye";
  bool bDone = false;
  if (r.j["place"].kind == PortJson::NUMBER)
    bDone = PortHudPress(static_cast<int>(r.j["place"].Num(-1)), &pszWhy);
  else if (r.j["button"].kind == PortJson::STRING)
    bDone = PortHudPressButton(PortHudButtonByName(r.j["button"].Str().c_str()), &pszWhy);
  else if (r.j["group"].kind == PortJson::NUMBER)
    bDone = PortHudPressGroup(static_cast<int>(r.j["group"].Num(-1)), &pszWhy);
  else if (r.j["eye"].kind == PortJson::NUMBER)
    bDone = PortHudPressEye(static_cast<int>(r.j["eye"].Num(-1)), &pszWhy);
  if (bDone)
    r.Ok("\"frame\":" + std::to_string(oc_dwReplayFrame));
  else
    r.Fail(pszWhy);
}

// {side?, popup?, page?}: the players' stats' views on or off and the popup's page, as F1, Shift+F1
// and Tab change them; a posted key carries no Shift
void CmdHudStats(const Call &r) {
  if (r.j["side"].kind == PortJson::BOOL)
    PortHudStatsSet(HUD_STATS_SIDE, r.j["side"].b);
  if (r.j["popup"].kind == PortJson::BOOL)
    PortHudStatsSet(HUD_STATS_POPUP, r.j["popup"].b);
  if (r.j["page"].kind == PortJson::NUMBER)
    PortHudStatsTurn(static_cast<int>(r.j["page"].d));
  PortHudState st;
  PortHudGet(&st);
  r.Ok(std::string("\"allowed\":") + (PortHudStatsAllowed() ? "true" : "false") + ",\"side\":" +
       (st.bStatsSide ? "true" : "false") + ",\"popup\":" + (st.bStatsPopup ? "true" : "false") +
       ",\"page\":" + std::to_string(st.nStatsPage) + ",\"sidePlaced\":" +
       (PortHudStatsPlaced(HUD_STATS_SIDE) ? "true" : "false") + ",\"popupPlaced\":" +
       (PortHudStatsPlaced(HUD_STATS_POPUP) ? "true" : "false"));
}

// {place}: the card button's tooltip at that place (0 to 8) as a resting pointer shows it, the
// HUD style's or 1.17's bubble; -1 hides it (statcmd.cpp, StatCmdTipAt)
void CmdHudTip(const Call &r) {
  if (StatCmdTipAt(static_cast<int>(r.j["place"].Num(-1))))
    r.Ok("\"frame\":" + std::to_string(oc_dwReplayFrame));
  else
    r.Fail("no button at that place");
}

// {topics}: the topics whose published messages the client is sent from now, in place of any
// before; [] for none
void CmdSubscribe(const Call &r);

const Command kCommands[] = {
    {"status", NEED_NOTHING, CmdStatus},             // menus or a game, its frame, paused, the map, the view
    {"hello", NEED_NOTHING, CmdStatus},
    {"players", NEED_GAME, CmdPlayers},
    {"hash", NEED_GAME, CmdHash},                    // {dump?}
    {"list", NEED_NOTHING, CmdList},
    {"get", NEED_NOTHING, CmdValue},                 // {name}
    {"set", NEED_NOTHING, CmdValue},                 // {name, value}
    {"snapshot", NEED_NOTHING, CmdSnapshot},         // {name?}: a PNG in snapshots\, answered after that frame
    {"pause", NEED_SINGLE, CmdPauseStep},            // 1.17's own pause
    {"resume", NEED_SINGLE, CmdPauseStep},
    {"step", NEED_SINGLE, CmdPauseStep},             // {frames?, fast?}
    {"reveal", NEED_SINGLE, CmdReveal},              // sandbox
    {"colour", NEED_GAME, CmdColour},                // {slot, rgb?}
    {"camera", NEED_GAME, CmdCamera},                // {x, y, zoom?}, in map pixels
    {"cutscene_play", NEED_NOTHING, CmdCutscenePlay},  // {steps, hold?, skip?, rest?}
    {"cutscene_stop", NEED_NOTHING, CmdCutsceneStop},
    {"cutscene_status", NEED_NOTHING, CmdCutsceneStatus},
    {"map_push", NEED_NOTHING, CmdMapSwitch},        // sandbox: {tileset?, width?, height?, tiles?, ...} or {from, ...}
    {"map_load", NEED_NOTHING, CmdMapSwitch},        // sandbox: {path, ums?, ms?, scene?, reveal?}
    {"canvas", NEED_NOTHING, CmdCanvas},             // sandbox, from the menus: {tileset?, battle?}
    {"spawn", NEED_SINGLE, CmdSpawn},                // sandbox: {unit, x, y, player?, count?, attack? | move?: [x, y]}
    {"clear", NEED_SINGLE, CmdClear},                // sandbox: {player}
    {"units", NEED_GAME, CmdUnits},                  // {ids} or {player?, type?, max?}
    {"bench_info", NEED_GAME, CmdBenchInfo},         // {ids? | player?, max?}
    {"region", NEED_NOTHING, CmdRegion},             // {x, y}
    {"pathstats", NEED_GAME, CmdPathstats},
    {"trace", NEED_NOTHING, CmdTrace},               // {what, on}
    {"trace_rows", NEED_NOTHING, CmdTraceRows},      // {since?}
    {"minimap", NEED_NOTHING, CmdMinimap},           // {u, v} or {path, step_ms?}
    {"perf", NEED_NOTHING, CmdPerf},                 // {on?, since?}
    {"settings", NEED_NOTHING, CmdSettings},
    {"setting", NEED_NOTHING, CmdSetting},           // {name, value, on?}
    {"mods", NEED_NOTHING, CmdMods},
    {"mod_push", NEED_NOTHING, CmdModPush},          // sandbox, in the canvas: {mod, types?, iscript?, folder?, replace?}
    {"mod_submissions", NEED_NOTHING, CmdModSubmissions},
    {"trainer", NEED_NOTHING, CmdTrainer},           // {key?, ctrl?, shift?}
    {"overlay", NEED_NOTHING, CmdOverlay},           // {mode?}: off, window, full or counter
    {"press", NEED_NOTHING, CmdPress},               // {key | vk, char?}, posted as Windows posts it
    {"click", NEED_NOTHING, CmdClick},               // {x, y, right?, move?, double?, wheel?, phase?}, at a client position
    {"clock", NEED_NOTHING, CmdClock},               // {on?}
    {"scroll", NEED_NOTHING, CmdScroll},             // {dir, ms}
    {"select", NEED_SINGLE, CmdSelect},              // sandbox: {x, y, player?, radius?, sound?}
    {"allies", NEED_NOTHING, CmdAllies},             // sandbox: {with}
    {"dialogs", NEED_NOTHING, CmdDialogs},
    {"maps", NEED_NOTHING, CmdMaps},                 // {filter?, max?, fresh?}
    {"map_info", NEED_NOTHING, CmdMapInfo},          // {path}
    {"lights", NEED_NOTHING, CmdLights},
    {"light", NEED_NOTHING, CmdLight},               // {image, name?, color?, radius?, intensity?, flicker?, remove?}
    {"lights_save", NEED_NOTHING, CmdLightsSave},    // to lights.json in the data folder
    {"subscribe", NEED_NOTHING, CmdSubscribe},       // {topics}: published messages, {topic, data} each
    {"hud", NEED_NOTHING, CmdHud},                   // the HUD's values under the format's names, the card, the buttons
    {"hud_events", NEED_NOTHING, CmdHudEvents},      // {since?}: the HUD's events after that serial
    {"hud_press", NEED_GAME, CmdHudPress},           // {place | button | group | eye}: a HUD press, through 1.17's handler
    {"hud_stats", NEED_GAME, CmdHudStats},           // {side?, popup?, page?}: the players' stats' views
    {"hud_tip", NEED_GAME, CmdHudTip},               // {place}: the card button's tooltip there, -1 none
};

// the commands other parts added (PortControlCommand)
std::vector<std::pair<std::string, PFN_CONTROL_COMMAND>> g_added;

// each subscribed client's topics (subscribe), on the game's thread
std::vector<std::pair<int, std::vector<std::string>>> g_subs;

void CmdSubscribe(const Call &r) {
  std::vector<std::string> topics;
  for (size_t i = 0; i < r.j["topics"].size(); ++i)
    if (r.j["topics"].At(i).kind == PortJson::STRING)
      topics.push_back(r.j["topics"].At(i).s);
  g_subs.erase(std::remove_if(g_subs.begin(), g_subs.end(),
                              [&](const std::pair<int, std::vector<std::string>> &s) { return s.first == r.nClient; }),
               g_subs.end());
  std::string list;
  for (const std::string &t : topics)
    list += (list.empty() ? "" : ",") + PortJsonQuote(t);
  if (!topics.empty())
    g_subs.push_back({r.nClient, topics});
  r.Ok("\"topics\":[" + list + "]");
}

void Handle(const Message &m) {
  PortJson j;
  std::string error;
  if (!PortJsonParse(m.text.c_str(), &j, &error) || j.kind != PortJson::OBJECT) {
    Send(m.nClient, Error("null", "not a JSON object: " + error));
    return;
  }
  const Call r{m.nClient, IdOf(j), j["cmd"].Str(), j};
  for (const Command &c : kCommands) {
    if (r.cmd != c.pszName)
      continue;
    if (c.need == NEED_GAME && gwGameMode != 3)
      r.Fail(r.cmd + " needs a game");
    else if (c.need == NEED_SINGLE && !InGame())
      r.Fail(r.cmd + " needs a single-player game");
    else
      c.pfnRun(r);
    return;
  }
  for (const auto &a : g_added) {
    if (r.cmd != a.first)
      continue;
    std::string answer;
    if (a.second(j, &answer))
      r.Ok(answer);
    else
      r.Fail(answer);
    return;
  }
  std::string names;
  for (const Command &c : kCommands)
    names += std::string(names.empty() ? "" : ", ") + c.pszName;
  for (const auto &a : g_added)
    names += ", " + a.first;
  r.Fail("no command " + r.cmd + "; " + names);
}

}  // namespace

void PortControlCommand(const char *pszName, PFN_CONTROL_COMMAND pfn)
{
  for (auto &a : g_added)
    if (a.first == pszName) {
      a.second = pfn;
      return;
    }
  g_added.emplace_back(pszName, pfn);
}

bool PortControlMapPath(const std::string &name, std::string *pPath)
{
  return MapFilePath(name, pPath);
}

void PortControlChanged(void)
{
  g_bChanged = true;
}

void PortControlLaunchFailed(const char *pszWhy)
{
  if (g_nSwap == SWAP_NONE)
    return;
  g_nSwap = SWAP_NONE;
  g_bSwapScene = false;
  g_bSwapReveal = false;
  // a map_push that did not reach the canvas's writing is not the next canvas's map either
  PortCanvasPushCancel();
  g_swapError = pszWhy;
  PortMotionArriveCancel();
}

int PortControlPort(void)
{
  return g_bRunning ? g_nPort : 0;
}

std::string PortControlAiContext(bool bState)
{
  const std::string exe = PortProgramFolder();  // on a Mac, the folder Hyperion.app is in
  const std::string mod = PortModsFolder() + "debug-tools\\";
  const int nPort = g_nPort > 0 ? g_nPort : PORT_CONTROL_PORT;
  const std::string base = "http://127.0.0.1:" + std::to_string(nPort);
  auto Exists = [](const std::string &path) { return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; };

  std::string text;
  HANDLE h = CreateFileA((mod + "ai-context.md").c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (h != INVALID_HANDLE_VALUE) {
    text.assign(GetFileSize(h, NULL), '\0');
    DWORD cb = 0;
    ReadFile(h, &text[0], static_cast<DWORD>(text.size()), &cb, NULL);
    text.resize(cb);
    CloseHandle(h);
  } else {
    text = "Hyperion's live channel listens on 127.0.0.1:{{port}}: a WebSocket at ws://127.0.0.1:{{port}}/?t=<key>, "
           "the key read from {{key_file}}, one JSON message at a time, "
           "{\"id\": 1, \"cmd\": \"status\"}. The live guide at {{base}}/guide explains every command.\n\n"
           "{{files}}\n\n{{state}}\n";
  }

  // the files there are to read, those this install has
  static const char *const kFiles[][2] = {
      {"docs\\live-channel-for-editors.md", "the live channel in full: every message, map switches, scene steps"},
      {"docs\\live-guide.html", "the live guide, to read offline"},
      {"docs\\hxm-for-editors.md", "Hyperion's map format, for editors"},
      {"docs\\hxm-format.md", "the map format field by field"},
      {"docs\\release-notes.md", "what each version of Hyperion changed"},
      {"tools\\hxlive.py", "a client in Python's standard library, for the command line and as a reference"},
      {"tools\\hxlive_cpp\\hxlive.h", "a client for C++ in one header over WinHTTP"},
      {"tools\\hxlive_cpp\\example.cpp", "a whole program using the C++ client"}};
  std::string files = "- `" + base + "/guide`: the live guide, every feature explained with code samples\n";
  for (const auto &f : kFiles) {
    const std::string path = PortPackagedFile(f[0]);  // in the package's Hyperion folder, or beside the program
    if (Exists(path))
      files += "- `" + path + "`: " + f[1] + "\n";
  }
  if (Exists(mod + "guide.html"))
    files += "- `" + mod + "guide.html`: the live guide's own file (open it from " + base +
             "/guide to use its buttons)\n";
  if (Exists(mod + "panel.html"))
    files += "- `" + mod + "panel.html`: Debug Tools' panel, served at " + base + "/\n";

  // the sample scenes in the mod's cutscenes folder
  std::string scenes;
  WIN32_FIND_DATAA fd;
  HANDLE hFind = FindFirstFileA((mod + "cutscenes\\*.json").c_str(), &fd);
  if (hFind != INVALID_HANDLE_VALUE) {
    do
      scenes += std::string(scenes.empty() ? "" : ", ") + "`" + base + "/cutscenes/" + fd.cFileName + "`";
    while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
  }
  if (scenes.empty())
    scenes = "none in this install.";
  else
    scenes += " (each made for the map it is named after; the folder is `" + mod + "cutscenes\\`).";

  std::string state = "Not included here: send `status` to see it.";
  if (bState) {
    state = g_bRunning ? "From `status` when this was copied:\n\n    {" + Status() + "}"
                       : "The channel is not open: start Hyperion with --control, or make Debug Tools the active mod.";
  }

  const std::pair<const char *, std::string> kFill[] = {
      {"{{version}}", HYPERION_VERSION_TEXT}, {"{{exe_folder}}", exe}, {"{{data_folder}}", PortDataFolder()},
      {"{{mod_folder}}", mod}, {"{{port}}", std::to_string(nPort)}, {"{{base}}", base},
      {"{{key_file}}", PortControlKeyFile(nPort)},
      {"{{files}}", files}, {"{{cutscenes}}", scenes}, {"{{state}}", state}};
  for (const auto &f : kFill)
    for (size_t at = text.find(f.first); at != std::string::npos; at = text.find(f.first, at + f.second.size()))
      text.replace(at, strlen(f.first), f.second);
  return text;
}

// ---------------------------------------------------------------------------

const std::string &PortControlKey(void) {
  return g_key;
}

// In the user's own local folder (Windows' Local AppData, XDG's data home elsewhere), which other
// users cannot read; by port, so two Hyperions on two ports keep two keys.
std::string PortControlKeyFile(int nPort) {
  char sz[MAX_PATH] = "";
  if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, sz) != S_OK || sz[0] == '\0')
    return std::string();
  return std::string(sz) + "\\Hyperion\\control-" + std::to_string(nPort) + ".key";
}

namespace {

// 128 bits from the system's random source, in hex; or HYPERION_CONTROL_KEY's, for a channel
// reached through a tunnel from another machine.
std::string MakeKey(void) {
  const char *pszFixed = getenv("HYPERION_CONTROL_KEY");
  if (pszFixed != NULL && strlen(pszFixed) >= 16 && strspn(pszFixed, "0123456789abcdefABCDEF") == strlen(pszFixed))
    return pszFixed;
  std::random_device rd;
  char sz[33];
  snprintf(sz, sizeof sz, "%08x%08x%08x%08x", static_cast<unsigned>(rd()), static_cast<unsigned>(rd()),
           static_cast<unsigned>(rd()), static_cast<unsigned>(rd()));
  return sz;
}

// The key to its file, readable by this user only, in a folder of Hyperion's (0700 elsewhere
// than Windows, where the user's local folder keeps it to them)
bool WriteKey(const std::string &file) {
  if (file.empty())
    return false;
  const std::string folder = file.substr(0, file.rfind('\\'));
#if defined(_WIN32)
  CreateDirectoryA(folder.c_str(), NULL);
#else
  mkdir(std::string(PortNativePath(folder.c_str())).c_str(), 0700);
#endif
  return PortWritePrivateFile(file.c_str(), g_key.c_str());
}

}  // namespace

void PortControlStart(int nPort) {
  static bool s_bTried;
  if (g_bRunning || s_bTried)
    return;
  s_bTried = true;  // one try: a port another program holds stays held
  BuiltInValues();
  PortAiTablesStart();  // ai_tables, the computer player's shared tables
  const int nAsked = atoi(PortCommandOptions()->szControl);
  g_nPort = nPort > 0 ? nPort : nAsked > 0 && nAsked < 65536 ? nAsked : PORT_CONTROL_PORT;
  FILE *pLog = fopen(PortLogFile("hyperion_control.log"), "w");
  WSADATA wsa;
  SOCKET s = INVALID_SOCKET;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0)
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u_short>(g_nPort));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#if !defined(_WIN32)
  // the port again at once after a run that ended, its connections still in TIME_WAIT
  // (Windows binds then without it, and there it would let two programs share the port)
  if (s != INVALID_SOCKET) {
    int nOne = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&nOne), sizeof nOne);
  }
#endif
  HANDLE h = NULL;
  if (s != INVALID_SOCKET && bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == 0 && listen(s, 4) == 0) {
    // the key once the port is ours, so a channel another Hyperion holds keeps its file
    g_key = MakeKey();
    if (!WriteKey(PortControlKeyFile(g_nPort)) && pLog != NULL)
      fprintf(pLog, "control: the key could not be written to %s\n", PortControlKeyFile(g_nPort).c_str());
    h = CreateThread(NULL, 0, Thread, reinterpret_cast<void *>(s), 0, NULL);
  }
  if (h == NULL) {
    if (pLog != NULL)
      fprintf(pLog, "control: cannot listen on 127.0.0.1:%d (error %d)\n", g_nPort, WSAGetLastError());
    if (s != INVALID_SOCKET)
      closesocket(s);
  } else {
    CloseHandle(h);
    g_bRunning = true;
    if (pLog != NULL)
      fprintf(pLog, "control: listening on 127.0.0.1:%d\n", g_nPort);
  }
  if (pLog != NULL)
    fclose(pLog);
}

bool PortControlRunning(void) {
  return g_bRunning;
}

bool PortControlFast(void) {
  return g_bStepping && g_bStepFast && gwGameMode == 3;
}

void PortControlFrameRan(void) {
  if (g_bStepping && gwGameMode == 3 && oc_dwReplayFrame >= g_nStepTo) {
    pause_game();
    g_bPausedHere = true;
  }
}

bool PortControlKeepsRunning(void) {
  return g_bRunning || PortCommandOptions()->szRecord[0] != 0  // --record plays on unwatched (port_record.cpp)
         || PortBenchWanted()                                    // a benchmark (port_bench.cpp)
         || PortClockBackgroundOn();                             // and the player's setting (port_clock.cpp)
}

void PortControlPlayerInput(void) {
  if (!g_bPausedHere)
    return;
  g_bPausedHere = false;
  g_bStepping = false;
  if (gwGameMode == 3 && IsGamePaused())
    unpause_game(0);
}

void PortControlPoll(void) {
  PortTracePoll();
  if (!g_bRunning) {
    // started by --control, by a tool mod while it is active and the player allowed it, or by
    // the canvas --canvas started (a mod's canvas is its mod's); the mod is looked at now and
    // then, as the Mods screen can change it
    static unsigned s_nPolls;
    if (s_nPolls++ % 60 == 0 &&
        (PortCommandOptions()->szControl[0] != '\0' || PortModChannelAllowed(PortModActive()) ||
         (gwGameMode == 3 && PortLaunchIsCanvas() && PortCommandOptions()->szCanvas[0] != '\0')))
      PortControlStart(0);
    if (!g_bRunning)
      return;
  }
  if (gwGameMode != 3)
    g_bChanged = false;
  SwapStep();
  // a step ends at its frame, or where the game stopped itself (a trainer session at the replay's end)
  if (g_bStepping && (gwGameMode != 3 || oc_dwReplayFrame >= g_nStepTo || IsGamePaused())) {
    g_bStepping = false;
    if (gwGameMode == 3) {
      pause_game();
      g_bPausedHere = true;
    }
    Send(g_nStepClient, Answer(g_stepId, "\"frame\":" + std::to_string(gwGameMode == 3 ? oc_dwReplayFrame : 0)));
  }
  if (g_bSnapGpu) {
    bool bOk = false;
    int w = 0, h = 0;
    if (PortGpuSnapTake(&bOk, &w, &h)) {
      g_bSnapGpu = false;
      Send(g_nSnapClient, bOk ? Answer(g_snapId, "\"path\":" + PortJsonQuote(g_snapPath) + ",\"width\":" + std::to_string(w) +
                                                     ",\"height\":" + std::to_string(h))
                              : Error(g_snapId, "the frame could not be saved"));
      g_snapPath.clear();
    }
  }
  // a snapshot the front end was asked for and left before it drew: nothing will take it, so
  // the asker hears now and the next snapshot is not turned away as one still being taken
  if (g_bSnapFront && !PortFrontActive()) {
    g_bSnapFront = false;
    Send(g_nSnapClient, Error(g_snapId, "the front end was left before the frame was saved"));
    g_snapPath.clear();
  }
  std::deque<Message> inbox;
  std::vector<int> gone;
  {
    std::lock_guard<std::mutex> hold(g_lock);
    inbox.swap(g_inbox);
    gone.swap(g_gone);
  }
  // a closed client's subscriptions go with it
  for (int nClient : gone)
    g_subs.erase(std::remove_if(g_subs.begin(), g_subs.end(),
                                [&](const std::pair<int, std::vector<std::string>> &s) { return s.first == nClient; }),
                 g_subs.end());
  for (const Message &m : inbox)
    Handle(m);
}

bool PortControlSubscribed(const char *pszTopic) {
  for (const auto &s : g_subs)
    if (std::find(s.second.begin(), s.second.end(), pszTopic) != s.second.end())
      return true;
  return false;
}

void PortControlPublish(const char *pszTopic, const std::string &json) {
  std::string text;
  for (const auto &s : g_subs) {
    if (std::find(s.second.begin(), s.second.end(), pszTopic) == s.second.end())
      continue;
    if (text.empty())
      text = PortJsonUtf8("{\"topic\":" + PortJsonQuote(pszTopic) + ",\"data\":" + json + "}");
    std::lock_guard<std::mutex> hold(g_lock);
    g_outbox.push_back({s.first, text, true});
  }
}

void PortControlFloat(const char *pszName, float *pf, float fMin, float fMax, const char *pszHelp, float fStep) {
  Register({pszName, pszHelp, pf, NULL, NULL, fMin, fMax, *pf, fStep});
}

void PortControlValue(const char *pszName, PFN_CONTROL_GET pfnGet, PFN_CONTROL_SET pfnSet, float fMin, float fMax,
                      float fDefault, const char *pszHelp, float fStep) {
  Register({pszName, pszHelp, NULL, pfnGet, pfnSet, fMin, fMax, fDefault, fStep});
}

bool PortControlFrontSnap(std::string *pPath) {
  if (!g_bSnapFront)
    return false;
  *pPath = g_snapPath;
  return true;
}

void PortControlFrontSnapped(bool bOk, int nWidth, int nHeight) {
  if (!g_bSnapFront)
    return;
  g_bSnapFront = false;
  Send(g_nSnapClient, bOk ? Answer(g_snapId, "\"path\":" + PortJsonQuote(g_snapPath) + ",\"width\":" + std::to_string(nWidth) +
                                                 ",\"height\":" + std::to_string(nHeight))
                          : Error(g_snapId, "the frame could not be saved"));
  g_snapPath.clear();
}

bool PortControlFrontCanvas(std::string *pTileset) {
  if (!g_bCanvasAsked)
    return false;
  g_bCanvasAsked = false;
  *pTileset = g_canvasTileset;
  return true;
}
