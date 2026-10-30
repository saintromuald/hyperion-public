// port_front_lan.h -- what the LAN screens share: Multiplayer and a room's lobby
// (port_front_lan.cpp), the host's console (port_front_lanhost.cpp) and its Devices tab
// (port_front_landevices.cpp). The room's state is the room's own JSON (port_lanroom.cpp's
// RoomJson), as the client last had it. Presentation only.
#ifndef PORT_FRONT_LAN_H
#define PORT_FRONT_LAN_H

#include <string>
#include <vector>
#include "../front/port_frontkit.h"
#include "../front/port_frontlobby.h"
#include "../platform/port_json.h"

namespace FrontKit {
namespace Lan {

// The design's colours for what a thing is, kept in every theme: how a device plays, and how
// far it is from the host.
extern const UiColor kStream, kInstall, kGood, kFair, kPoor, kOn, kOff, kBad;

// The room, as last sent; a device and a match in it by id, NULL when gone.
const PortJson &Room(void);
const PortJson *DeviceOf(int nId);
const PortJson *MatchOf(int nId);
int You(void);

// Words for a device: its kind ("Windows", "Linux ARM", "Phone"), how it plays ("Streamed",
// "Installed"), what it is doing as a line and the detail under it, and how long it has been in.
std::string KindName(const PortJson &d);
const char *RouteName(const PortJson &d);
void Doing(const PortJson &d, std::string *pLine, std::string *pDetail);
std::string Ago(double fSeconds);
std::string Clock(double fSeconds);
std::string Count(int n, const char *pszOne, const char *pszMany = NULL);
// A version as a player reads it: "Hyperion 0.3.4-beta" for "Hyperion-v0.3.4-beta".
std::string VersionName(const std::string &v);
// A match's game time, from its seats' frames (1.17's fastest is 24 frames a second).
double GameSeconds(const PortJson &m);
// The room's people as the Devices tab counts them: every device but the console's own unless
// it plays, the refused included.
std::vector<const PortJson *> People(void);

// Drawn pieces: a device's glyph (its kind's simple shape), the route's pill, a latency in its
// band's colour, a bar filled to f, a spark of the last values.
void Glyph(const PortJson &d, float fSide = 22);
void RouteChip(const PortJson &d, bool bSmall);
void Rtt(const PortJson &d, float fSize = 13);
void Bar(float fWidth, float f, bool bHot = false);
void Spark(const std::vector<float> &values, float fMax, float w, float h, UiColor c);
// A device's round trips, the last 30 seconds, sampled each second by the console.
const std::vector<float> &RttHistory(int nId);
void RttSample(void);

// A box in the panel, at x, y, w by h in SC2's units, with its title (lobby.css .lb-box).
void BoxAt(const char *pszId, float x, float y, float w, float h, const char *pszTitle);
void Title(const char *psz);
void Fine(const char *psz, UiColor c = kLbInkDesc, float fSize = 14);

// The design's ask (lan.js ask): a title, a line, and choices each with its key and a line
// under it; the first is the main one. Ask shows it until one is chosen; AskShown draws the one
// up, and gives the chosen index once, -1 while none, -2 when cancelled by Escape or a click
// outside.
struct AskChoice {
  char cKey;
  std::string label, line;
};
void Ask(const std::string &id, const std::string &title, const std::string &body, const std::vector<AskChoice> &choices);
bool AskUp(void);
int AskShown(std::string *pId);
bool AskKeys(void);

// A note at the top right for a few seconds (lan.js K.toast): a title and a line.
void Toast(const std::string &title, const std::string &line);
void ToastsDraw(void);

// Where the screens go.
void ToMultiplayer(void);
void ToConsole(void);
void ToLobby(int nMatch);
// The room's server, started by the console: running here, and its password to keep across a
// stop and a start.
bool ServerRunning(void);
void ServerStart(void);
void ServerStop(bool bAfterMatches);
// The console asks the room for a new match, which the lobby opens on.
void NewMatch(void);
// A live match watched from this machine, the room's delay behind (port_lanfeed.cpp): asked of
// the room, and started as a replay once its beginning is here. WatchOff says why it cannot
// be, NULL when it can.
void WatchMatch(int nMatch);
const char *WatchOff(const PortJson &m);

// The console (port_front_lanhost.cpp) and its Devices tab (port_front_landevices.cpp).
void ConsoleOpen(void);
void ConsoleScreen(float fW, float fH);
bool ConsoleKeys(void);
void DevicesTab(float x, float y, float w, float h, float px, float pw);
bool DevicesKeys(void);
void DeviceCard(const PortJson &d, bool bPane);
bool DeviceCardKeys(const PortJson &d);
extern int g_nDeviceOpen;  // the device whose card is open over the console, 0 none
void OpenDevice(int nId);

}  // namespace Lan
}  // namespace FrontKit

#endif
