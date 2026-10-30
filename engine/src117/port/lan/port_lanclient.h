// port_lanclient.h -- a Hyperion in a LAN room: its connection to the room
// (port_lanroom.cpp), the room's state as the room last sent it, the rooms its network has, and
// the launch of a match it plays in. The screens (port_front_lan*.cpp) read and drive it; the
// provider (port_stormlan.cpp) and the game loop keep it pumped while a game runs.
#ifndef PORT_LANCLIENT_H
#define PORT_LANCLIENT_H

#include <stdint.h>
#include <string>
#include <vector>
#include "../platform/port_json.h"

// A room found on the network: its summary, and whether this copy can play there and why not.
struct LanRoomFound {
  std::string addr;  // "a.b.c.d"
  int nPort = 0;
  PortJson info;     // the room's summary (port_lanroom.cpp's Advertise)
  bool bOk = false;
  std::string why, fix, mark;  // mark: "Plays here", "Needs a mod", "Another version"...
};

// Queries the network (twice a second while called) and gives the rooms heard from lately.
std::vector<LanRoomFound> LanClientRooms(void);

// Into the room at pszAddr:nPort, saying hello: false when nothing answers there. The room's
// answer, welcome or refused, comes with the pump. pszKey is the console's key, for the host.
bool LanClientJoin(const char *pszAddr, int nPort, const char *pszPassword, const char *pszKey);
void LanClientLeave(void);
bool LanClientIn(void);       // connected, and let in
bool LanClientAdmin(void);    // let in as the console
bool LanClientWaiting(void);  // connected, its hello not yet answered
// The room's refusal, and its fix, when it refused.
const std::string &LanClientRefused(std::string *pFix);
// The room closed, or the connection was lost, since it was let in.
bool LanClientLost(void);
const char *LanClientAddress(void);

// Reads what the room sent; answers its pings; tells it this player's frame.
void LanClientPump(void);
// The room's state, as it last sent it (port_lanroom.cpp's RoomJson), and this device's id.
const PortJson &LanClientRoom(void);
int LanClientYou(void);
// A message to the room, JSON; a frame of another kind (LanProto::Frame) with its bytes.
void LanClientSay(const std::string &json);
void LanClientSend(uint8_t bKind, const std::vector<uint8_t> &data);
// A match's feed the room sent this copy to watch (LanProto::ROOM_FEED), oldest first.
bool LanClientTakeFeed(std::vector<uint8_t> *pOut);
// The pictures of the live matches, from the room's copies (the console's Wall): the latest of
// each player's army, three bytes a pixel, nSeq counting up as each is replaced.
struct LanPicture {
  int nMatch = 0, nPlayer = 0;
  std::string name;
  int w = 0, h = 0;
  std::vector<uint8_t> rgb;
  uint32_t nSeq = 0;
};
const std::vector<LanPicture> &LanClientPictures(void);
// The room's own lines for the player since the last call: an error, a removal.
std::vector<std::string> LanClientNotes(void);

// A launch the room sent, once: the match to make or join.
bool LanClientTakeLaunch(PortJson *pOut);
// A browser's watching seat (--lan-stream): the match the room says to show, until it is watched
// (LanClientFollowed); 0 none.
int LanClientFollow(void);
void LanClientFollowed(void);
// The map the room sent with the launch, saved in the data folder.
const std::string &LanClientMapPath(void);
// This player's game, as the room is told: its frame, and how it ended.
void LanClientFrame(uint32_t dwFrame);
void LanClientResult(const char *pszText);
// The host ended the match this player is in, once: its game is left.
bool LanClientTakeEnd(void);
// The match the room made at the console's New Match, once; 0 none.
int LanClientTakeMade(void);

// This copy's game data, as a fingerprint the room compares ("4F2A91C0"): the game's rules
// (its DATs and scripts) as Storm reads them, so a copy on 1.17's MPQs and one on a
// Remastered install agree.
const std::string &LanFingerprint(void);
// The name this copy plays under: --lan-name, the profile's, or the game's.
std::string LanPlayerName(void);

#endif
