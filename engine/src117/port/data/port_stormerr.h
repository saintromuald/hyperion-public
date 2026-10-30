// port_stormerr.h -- the error codes the port's Storm network sets (SErrSetLastError) and the LAN
// relay sends, each named for what 1.17 makes of it; port_stormerr.cpp gives every Storm code its
// text. Where Storm used one of Windows' own codes (ERROR_*), the value is that code's.
#ifndef PORT_STORMERR_H
#define PORT_STORMERR_H

#include <stdint.h>

const uint32_t NET_BAD_PROVIDER = 0x4B4;          // ERROR_BAD_PROVIDER: no provider started
const uint32_t NET_INVALID = 0x57;                // ERROR_INVALID_PARAMETER
const uint32_t NET_NOT_OWNER = 0x120;             // ERROR_NOT_OWNER: not the game's creator
const uint32_t NET_NO_PLAYER = 0x8510006A;        // no such player
const uint32_t NET_NOTHING_WAITING = 0x8510006B;  // no turn or message ready
const uint32_t NET_NOT_IN_GAME = 0x85100070;
const uint32_t NET_CANNOT_JOIN = 0x85100078;      // a join refused, for any reason but a full game
const uint32_t NET_GAME_FULL = 0x85100079;        // a join refused: 1.17 says the game is full

#endif
