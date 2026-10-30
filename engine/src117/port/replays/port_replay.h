// port_replay.h -- the replay turn buffer net_mgr_replay_turns keeps across calls
// (port_replay.cpp).
#ifndef PORT_REPLAY_H
#define PORT_REPLAY_H

#include <vector>

// net_mgr_replay_turns's replay turn buffer, kept across calls once it has grown
// (port_replay.cpp). Use points a fresh CNetUnkn at it; Keep takes over what a
// read grew.
class CNetUnkn;
void PortReplayTurnsUse(CNetUnkn *pTurns);
void PortReplayTurnsKeep(CNetUnkn *pTurns);

// The trainer's snapshots (port_snap.cpp): a frame read one call early may wait in the grown
// buffers, which a snapshot keeps. Buffers only grow, and a grown buffer is used from then on,
// so a restore copies the kept ones into the buffers of now, or, for a snapshot taken before
// any growth, 1.17's static arrays, which the snapshot restored.
void PortReplayTurnsSave(std::vector<unsigned char> *pOut);
void PortReplayTurnsRestore(const std::vector<unsigned char> &saved);

#endif
