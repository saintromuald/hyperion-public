// port_lanproto.h -- what the LAN host's parts say to each other: the frames between a
// player's Hyperion and a match's relay (port_stormlan.cpp, port_lanrelay.cpp), the frames
// between a Hyperion and the room (port_lanclient.cpp, port_lanroom.cpp), the advert a query is
// answered with, and how a frame's fields are written. Every frame goes over port_lannet.cpp.
#ifndef PORT_LANPROTO_H
#define PORT_LANPROTO_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "../../h/port_synchash.h"  // SYNC_DOMAINS, SyncDomainName

namespace LanProto {

// Protocol 2: the relay apart from the players (H0's was 1, with the host a player).
const uint16_t kProtocol = 2;

enum Frame : uint8_t {
  HELLO = 1,  // to a relay: protocol, sim revision, version, name, description, password, game
  WELCOME,    // from a relay: the joiner's id, the game and the players in it
  REFUSE,     // from a relay: Storm's error and why
  JOINED,     // from a relay: a player let in
  TURN,       // to a relay: the sender's next turn
  STEP,       // from a relay: a step's players and turns, and who left before it
  MESSAGE,    // to a relay with whom it is for; from it with whom it is from
  LEAVE,      // to a relay: the sender leaves, and why
  STATUS,     // from a relay: the players a step waits for, and each player's latency
  PING,
  PONG,
  MODE,       // from a relay: the game's mode
  DROP,       // to a relay: drop a player
  HASH,       // to a relay: the sender's state hash at a frame
  CREATE,     // to a relay: HELLO's fields and the game the creator makes, who is player 0

  ROOM = 0x20,  // a room's message, JSON, either way
  ROOM_FILE,    // from the room: a file a launch needs, its name and its bytes
  ROOM_FEED,    // a match's replay as it is played (port_lanfeed.cpp): from its seats to the
                // room, and from the room to who watches; a FeedKind, the match, its fields
};

// A ROOM_FEED's kinds, each after its kind and the match's id. FEED_START: the replay's magic,
// header, map and Hyperion's sections. FEED_RECORD: where the bytes go in the record, the last
// frame whose commands they complete, the bytes. FEED_END: the last frame, the record's length,
// and the header and sections as the game ended. FEED_HASH, from a watcher: its state hash at a
// frame, as a player's HASH, held against the players'. FEED_PICTURE, from the room's copy to
// the console: the player whose army it shows and the name, width and height, three bytes a
// pixel. FEED_HEAT, from the copy to the watchers: the frame, x and y in map pixels and the heat
// in hundredths where the fighting was hottest, so a watcher's director reads ahead by its delay.
enum FeedKind : uint8_t { FEED_START = 1, FEED_RECORD, FEED_END, FEED_HASH, FEED_PICTURE, FEED_HEAT };
const int kPictureW = 192, kPictureH = 108;  // a copy's picture
const uint32_t kReplayHeader = 0x279;        // a FEED_START's replay header (REPLAY_HDR_BLOB, lang/replay.cpp)

// What a query's answer carries (port_lannet.cpp's UDP advert), after the protocol.
enum Advert : uint8_t {
  ADVERT_GAME = 1,  // a game made on 1.17's own screens, its relay in the creator's Hyperion
  ADVERT_ROOM = 2,  // a room (port_lanroom.cpp): its summary as JSON
};

// The provider's limits, as Storm's.
const int kPlayers = 8;
// A seat's turns the relay holds before it is out: a game runs a few steps ahead at most, so
// this many is a seat that is not playing it (port_lanrelay.cpp).
const size_t kTurnsQueuedMost = 256;
// A turn's most: a game's command queue is 512 bytes (classic sgbCmdQueue_g), so a turn far past
// it is no game's, and the relay drops its seat rather than pass it to the others.
const size_t kTurnMost = 1 << 14;
// How long a connection has to send its first frame before it is closed (LanNet::Greeted).
const int kGreetingMs = 10000;
// How long the steps wait on a seat that is connected but sends no turns before it is lost, as
// the host console's "A player drops" says (port_lanrelay.cpp).
const uint64_t kStalledMs = 120000;
const uint32_t kHashEvery = 120;  // frames between the players' state hashes

struct Writer {
  std::vector<uint8_t> v;
  void B(uint8_t b) { v.push_back(b); }
  void W(uint16_t w) { B(static_cast<uint8_t>(w)); B(static_cast<uint8_t>(w >> 8)); }
  void D(uint32_t d) { W(static_cast<uint16_t>(d)); W(static_cast<uint16_t>(d >> 16)); }
  void S(const char *psz) { v.insert(v.end(), psz, psz + strlen(psz) + 1); }
  void S(const std::string &s) { S(s.c_str()); }
  void Bytes(const void *pv, size_t cb)
  {
    D(static_cast<uint32_t>(cb));
    v.insert(v.end(), static_cast<const uint8_t *>(pv), static_cast<const uint8_t *>(pv) + cb);
  }
};

struct Reader {
  const std::vector<uint8_t> &v;
  size_t at = 0;
  bool bBad = false;
  explicit Reader(const std::vector<uint8_t> &data) : v(data) {}
  bool Has(size_t cb)
  {
    if (at + cb > v.size())
      bBad = true;
    return !bBad;
  }
  uint8_t B(void) { return Has(1) ? v[at++] : 0; }
  uint16_t W(void)
  {
    const uint16_t lo = B();
    return static_cast<uint16_t>(lo | (B() << 8));
  }
  uint32_t D(void)
  {
    const uint32_t lo = W();
    return lo | (static_cast<uint32_t>(W()) << 16);
  }
  std::string S(void)
  {
    std::string s;
    while (Has(1) && v[at] != 0)
      s += static_cast<char>(v[at++]);
    B();
    return s;
  }
  std::vector<uint8_t> Bytes(void)
  {
    const uint32_t cb = D();
    if (!Has(cb))
      return std::vector<uint8_t>();
    std::vector<uint8_t> out(v.begin() + at, v.begin() + at + cb);
    at += cb;
    return out;
  }
  std::string Rest(void)
  {
    std::string s(v.begin() + at, v.end());
    at = v.size();
    return s;
  }
};


// Where two players' hash records (port_stormlan.cpp's HashRecord: the frame, the random
// number's state and count, and each domain's 8 bytes) differ, for the logs: "random numbers,
// map" and so on; empty when they are not both whole.
inline std::string HashPartsDiffer(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
  std::string parts;
  if (a.size() != b.size() || a.size() < 12 + SYNC_DOMAINS * 8)
    return parts;
  if (memcmp(a.data() + 4, b.data() + 4, 8) != 0)
    parts = "random numbers";
  for (int d = 0; d < SYNC_DOMAINS; d++)
    if (memcmp(a.data() + 12 + d * 8, b.data() + 12 + d * 8, 8) != 0)
      parts += std::string(parts.empty() ? "" : ", ") + SyncDomainName(d);
  return parts;
}

}  // namespace LanProto

#endif
