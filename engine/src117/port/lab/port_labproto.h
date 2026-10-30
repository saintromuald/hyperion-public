// port_labproto.h -- what the AI lab and its workers say to each other. A worker is a
// hidden Hyperion the lab starts, playing a game of computers (port_labworker.cpp); it connects
// back to the lab on this machine over the LAN host's framed connection (port_lannet.cpp), and
// each message is one frame: its kind, then its fields, written with LanProto::Writer (B one
// byte, W two, D four, S a string ending in a zero; little-endian).
#ifndef PORT_LABPROTO_H
#define PORT_LABPROTO_H

#include <stdint.h>

namespace LabProto {

const uint16_t kVersion = 1;

enum Kind : uint8_t {
  // a worker to the lab
  HELLO = 1,       // W version, D worker, S token, D pid, S build
  GAME_START = 2,  // D game, S map, D seed, D frame limit, B speed, B multiple (0 the most),
                   // W map width, W map height (tiles), B players, then each: B slot, B race,
                   // B team, B level (port_ai.h), S name
  SAMPLE = 3,      // D game, D frame, B players, then each: B slot, D minerals, D gas,
                   // W supply used, W supply most, W units, B out; then W units, then each:
                   // D unit id (CUnit::GetUnitID), W x, W y (map pixels), B player, B kind
  STATUS = 4,      // D game, D frame, D game frames a second x10, D units
  HASH = 5,        // D game, D frame, D state low, D state high (SyncHashFold), while watched
  GAME_END = 6,    // D game, S reason, D end frame, D state low, D state high, D seconds x10,
                   // D game frames a second x10, B winners, then each B slot; B players, then
                   // each: B slot, D units, D minerals gathered, D gas gathered
  LOG = 7,         // S text
  // the lab to a worker
  WELCOME = 0x20,   // D worker
  JOB = 0x21,       // D game, S map, B players, S levels (port_ai.h), S races, S type (melee or
                    // ffa), D seed, D frame limit, S the game's folder: a worker started without
                    // --bench plays it, and after its GAME_END waits for the next JOB or STOP 1
  PACE = 0x22,      // B multiple of Fastest (0 the most), B paused; the worker keeps it by
                    // the clock, between its frames (port_labworker.cpp, Pace)
  SAMPLING = 0x23,  // W samples a second (0 none), W most units a sample
  WATCHED = 0x24,   // B on, D the frame it may play to: while on, the worker plays as fast as
                    // it can up to that frame and waits there, whatever the pace; it sends
                    // STATUS every 50 ms and HASH every 240 frames (port_labwatch.cpp)
  STOP = 0x25,      // B 0 to end the game, 1 to quit
};

// A unit's kind, in a sample (units.dat's flags)
enum : uint8_t { UNIT_BUILDING = 1, UNIT_AIR = 2, UNIT_WORKER = 4, UNIT_RESOURCE = 8 };

}  // namespace LabProto

#endif
