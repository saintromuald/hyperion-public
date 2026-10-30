// port_lanroom_internal.h -- what the room's four files share (port_lanroom.cpp, and its
// _proto, _host and _replay parts): the room's parts, its state, and the functions one file
// calls in another. Only those files include it.
#ifndef PORT_LANROOM_INTERNAL_H
#define PORT_LANROOM_INTERNAL_H

#include <stdint.h>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <windows.h>  // HANDLE
#include "../platform/port_json.h"
#include "port_lanproto.h"
#include "port_lanrelay.h"
#include "../platform/port_log.h"  // PortLogger
#include "../gameplay/port_session.h"  // MutatorSet

namespace LanRoom {

using namespace LanProto;

// ---------------------------------------------------------------------------
// The room's parts.

struct Device {
  int nId = 0;
  int nConn = -1;      // a Hyperion's framed connection, or the page's WebSocket (bWeb)
  bool bWeb = false;
  bool bAdmin = false;  // the console
  std::string name, machine, os, arch, kind, route, version, fp, mod, rules, ip, browser, page;
  int nSim = 0;
  // Empty until its hello: nobody, told only PublicJson, and nothing it says counts (Said).
  // Then joined, lobby, playing, watching, installing, refused, or a copy's or a seat's.
  std::string state;
  int nMatch = 0;
  std::string why, fix;
  std::string filesToken;  // a page's, once it is in: the host's game files at /files/<token>/ (LanWeb::AllowFiles)
  std::string backToken;   // a page's, once it is in: shown again after a reload, it takes the seat back
  uint64_t qwJoined = 0;
  uint32_t dwRtt = 0;
  uint64_t qwPingSent = 0;
  bool bReady = false;
  std::string race = "random";
  int nHandicap = 100;  // the share of its units' life and shields it plays at (port_handicap.h)
  int nColour = -1;
  uint32_t dwFrame = 0;  // its game's frame, as it last said
  bool bGone = false;
  bool bCopy = false;   // the room's own copy of a match (Hyperion --lan-copy), listed nowhere
  // a browser's player's seat (route "stream"): a Hyperion the room starts on the host with no
  // window (--lan-stream), which plays the seat and streams it to the browser
  bool bStream = false;  // this is that Hyperion, listed nowhere (g_streams keeps it)
  std::string mapSent;  // the lobby's map, sent to it so its set-up shows the map
};

struct Seat {
  std::string owner = "open";  // open, closed, computer, player
  int nDev = 0;
  std::string race = "random";
  int nHandicap = 100;  // a computer's, the host's choice
  int nColour = -1;
  int nTeam = 0;
};

// A seat's feed of its match (port_lanfeed.cpp): its beginning (the replay's magic, header, map
// and sections), the record as it grows, and its end (the last frame, the header and sections).
struct Feed {
  std::vector<uint8_t> start, record, end;
  uint32_t dwFed = 0;  // the commands are complete to this frame
  bool bEnded = false;
  uint32_t dwEnd = 0;
  uint64_t qwHeard = 0;
};

struct Match {
  int nId = 0;
  std::string name;
  std::string state = "lobby";  // lobby, starting, live, ended
  std::string map, mapName, type = "melee", speed = "fastest";
  // the map's picture, which the host draws (mappic) and the room serves to its pages: its
  // address, and the starts on it as JSON, [[the map's player, across, down], ...]
  std::string pic, picStarts;
  int nPic = 0;
  int nKey = 2 | 1 << 16, nSpeed = 6;  // the game type's template key, and 1.17's speed 0 to 6
  bool bHyperion = false;  // it plays by Hyperion's rules, the host's choice (hxrules.h)
  MutatorSet mut;          // and its mutators, the host's (port_mutators.h)
  int nMapPlayers = 8;
  Seat seats[kPlayers];
  std::vector<int> watchers;
  std::vector<std::pair<std::string, std::string>> chat;  // who (empty for the room), what
  std::unique_ptr<LanRelay> relay;
  uint32_t dwGameId = 0;
  std::string token;
  int nCreator = 0;    // the device that makes the game
  bool bJoinsSent = false;
  uint64_t qwLaunched = 0, qwStarted = 0, qwEnded = 0;
  uint64_t qwLeaveAsked = 0;  // its seats told to leave the game; the relay goes once they have
  std::string result;
  std::map<int, Feed> feeds;  // each seat's, by its device
  int nFeed = 0;              // the device whose feed the watchers get
  std::map<int, std::pair<uint32_t, uint32_t>> sent;  // a watcher's: the record's bytes and frame sent it
  bool bFeedDone = false;     // every feed ended: the replay written, the watchers told
  bool bFeedsDiffer = false;
  std::string replay;         // where the room wrote it
  // the players' hashes, a frame's, and those watching held against them (their FEED_HASH)
  std::map<uint32_t, std::vector<uint8_t>> hashes;
  std::map<uint32_t, std::map<int, std::vector<uint8_t>>> watchHashes;
  uint32_t dwWatchAgreed = 0, dwWatchDiffered = 0;
  std::vector<int> outOfStep;  // watchers already said to differ
  // the room's copy of the match: a Hyperion with no window following the feed with no delay,
  // whose pictures fill the console's Wall and whose hashes are held against the players'
  std::string copyToken;
  HANDLE hCopy = NULL;
  int nCopyDev = 0;
  uint32_t dwCopyAgreed = 0, dwCopyDiffered = 0;
  uint64_t qwCopyStarted = 0;
  // Start Match waiting for its browsers' seats to come in (StartStream), and who asked
  uint64_t qwSeating = 0;
  bool bSeatingForce = false;
  int nStarter = 0;
  // the last fight its copy saw (FEED_HEAT), which the Big Screen goes to
  uint64_t qwHeat = 0;
  float fHeat = 0.0f;
};

// A browser's player's seat on this machine (port_lanstream.cpp): the Hyperion the room started
// for them, with no window, which plays their seat and streams it to the page. A page that goes
// while its game is under way leaves its device in the room, gone, so the seat plays on and a
// page opened again by the same player takes it back. Or, for a page that plays in its tab
// (route "tab"), the Hyperion the page runs itself, which comes in by the token the
// room gave the page (StartTab); it is on until the page goes.
struct StreamSeat {
  HANDLE hProcess = NULL;
  bool bTab = false, bTabOn = false;
  uint64_t qwTabSaid = 0;  // when the page was last given the token
  std::string token, name;
  int nFor = 0;  // the browser's device
  int nDev = 0;  // the seat's own, once it is in
  uint64_t qwStarted = 0;
  std::string info;  // its readings, as it last said them (a JSON object)
  // a page watching: the match it asked for, or kBigScreen, which goes where the fighting is; the
  // match it was last told to follow, and when (PumpStreams)
  int nWatch = 0;
  int nFollowing = 0;
  uint64_t qwFollowed = 0;
};

const int kBigScreen = -1;

struct LogLine {
  uint64_t qwAt;
  std::string kind, text;
  std::vector<int> devs;
};

// The host's load, as the console's This Machine shows it: the processor's share busy, and the
// memory in use, read each second.
struct Load {
  double fCpu = 0, fMemUsed = 0, fMemTotal = 0;
  uint64_t qwIdle = 0, qwAll = 0;
};

// The room's state (port_lanroom.cpp).
extern std::map<int, Device> g_devices;
extern std::map<int, std::unique_ptr<Match>> g_matches;
extern std::vector<StreamSeat> g_streams;
extern std::vector<LogLine> g_log;
extern std::vector<int> g_pending;  // connections whose first frame has not come
extern int g_nNextDevice, g_nNextMatch;
extern std::string g_name, g_machine, g_key, g_password, g_address;
extern PortJson g_settings;  // the console's Settings, kept in lanroom.json
extern std::string g_settingsText;
extern bool g_bRunning, g_bStopAfter, g_bQuit;
extern bool g_bDirty;
extern uint64_t g_qwStarted, g_qwLastSent, g_qwLastAdvert, g_qwLastPing, g_qwAdminGone;
extern bool g_bHadAdmin;
extern Load g_load;
extern const PortLogger Log;  // hyperion_lan.log, "room: "

// The room (port_lanroom.cpp).
void Tonight(const char *pszKind, const std::string &text, std::vector<int> devs = {});
Device *Dev(int nId);
Match *MatchOf(int nId);
bool Nobody(const Device &d);
bool SeatOn(const StreamSeat &t);
StreamSeat *StreamFor(int nBrowser);
StreamSeat *StreamOn(int nDev);
void Unseat(Device &d);
bool Sit(Device &d, Match &m, int nSeat);
void Gone(Device &d, const char *pszWhy);
Match &NewMatch(const PortJson &j);
void Back(Match &m);
std::string Launch(Match &m, bool bForce);
void EndMatch(Match &m, const std::string &why);

// What devices are told and say (port_lanroom_proto.cpp).
void SendJson(Device &d, const std::string &json);
std::string RoomJson(const Device *pFor);
void Advertise(void);
void Heard(Device &d, const std::string &text);

// This machine's work (port_lanroom_host.cpp).
std::mt19937 &Rng(void);
std::string NewPassword(void);
std::string Token(void);
std::string Setting(const char *pszName, const char *pszDefault);
bool PasswordOn(void);
bool BrowserFilesOn(void);
void ServeGameFiles(void);
void SettingsSave(void);
void SettingsLoad(void);
void ReadLoad(void);
void ServeFonts(void);
void StartCopy(Match &m);
void StopCopy(Match &m);
void StartStream(Device &w, int nWatch = 0);
void WatchInBrowser(Device &d, const PortJson &j);
void StartTab(Device &w);
void EndWatch(StreamSeat &t);
void StopStream(StreamSeat &t);
void PumpStreams(void);
void RoomNoteWrite(void);
void RoomNoteRemove(void);
std::string MachineName(void);

// The feeds and the replay (port_lanroom_replay.cpp).
int DelaySeconds(void);
bool WatchingOn(void);
void FeedHeard(Device &d, const std::vector<uint8_t> &data);
void PumpFeed(Match &m);

}  // namespace LanRoom

#endif
