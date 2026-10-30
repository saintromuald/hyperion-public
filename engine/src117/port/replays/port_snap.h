// port_snap.h -- the trainer's snapshots (port_snap.cpp): the game's state copied in
// memory at a game frame's end, and put back in place in the same game.
#ifndef PORT_SNAP_H
#define PORT_SNAP_H

#include <stddef.h>

// The allocator's notes (port_stormbase.cpp): a block from one of the sites that hold the game
// is part of every snapshot while it lives.
void PortSnapAlloc(const void *pv, size_t cb, const char *pszTag, int nLine);
void PortSnapFree(const void *pv);

// A static of the port's own that holds the game (vision past slot 7, the players past 11, the
// C runtime's random state): part of every snapshot from now on. Storage registered after a
// snapshot was taken was untouched (zero) at that snapshot, and a restore to it clears it.
// Returns true, for a static initialiser.
bool PortSnapStatic(void *pv, size_t cb);
// At file scope after a static's definition: the static is part of every snapshot.
#define PORT_SNAP_KEEP(v) static const bool s_bSnapKeep_##v = PortSnapStatic(&(v), sizeof(v))
// A static array of a slot each of a pool, sized for the most any game takes: only the slots
// of the game's count (*pnCount, g_pools) are part of a snapshot.
bool PortSnapStaticPool(void *pv, size_t cb, size_t cbEach, const int *pnCount);
#define PORT_SNAP_KEEP_POOL(v, count) \
  static const bool s_bSnapKeep_##v = PortSnapStaticPool(&(v), sizeof(v), sizeof((v)[0]), &(count))

// From the game loop, at a game frame's end: snapshots on their frames, the self-test.
void PortSnapFrame(void);

// The snapshots are kept by line, a line being one of the trainer's timelines (port_trainer.cpp);
// the self-test's are line 0. A line's snapshots are shared with the lines branched from it up
// to where they part, and pages are shared between all of them.
//
// The game's state now, as a line's snapshot at a frame (the frame just ended).
void PortSnapTake(int nLine, unsigned nFrame);
bool PortSnapHas(int nLine, unsigned nFrame);
// The latest of a line's snapshots at or before a frame, put back: the game is at that frame's
// end. The frame put back, or ~0u when there is none or the game's blocks are not the
// snapshot's. With bFeed the commands in transit at that moment are dropped, sync aside: the
// trainer gives the line's commands itself (PortTrainerFeed).
unsigned PortSnapRestore(int nLine, unsigned nFrame, bool bFeed);
// The frame of a line's latest snapshot at or before a frame, ~0u for none.
unsigned PortSnapLatest(int nLine, unsigned nFrame);
// A line's snapshots on frames that are not a multiple of nEvery dropped, but its first (a
// replay's seek, whose store grew past its most).
void PortSnapThin(int nLine, unsigned nEvery);
// A line's snapshots up to a frame, shared with another line from now on.
void PortSnapShare(int nFrom, int nTo, unsigned nUpTo);
// Drops a line's snapshots after a frame (a line rewritten from there), or all of them.
void PortSnapDropAfter(int nLine, unsigned nFrame);
void PortSnapDropLine(int nLine);
// Drops every line's (a game's end).
void PortSnapClear(void);
// The bytes of state one snapshot covers, and of pages the store holds, each once.
size_t PortSnapStateBytes(void);
size_t PortSnapStoreBytes(void);

struct PortSnapStats {
  unsigned nSnaps;
  unsigned nFirst, nLast;   // their frames
  size_t cbState;           // the game's state, the bytes one snapshot covers
  size_t cbStored;          // the pages the store holds, each once
  double msTake, msRestore; // the last take and restore
};
void PortSnapStatsGet(PortSnapStats *pOut);

#endif
