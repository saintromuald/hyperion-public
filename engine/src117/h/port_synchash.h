// port_synchash.h -- a hash of the game's state, for checking that a replay or a loaded
// game plays the game it recorded (port/loop/port_synchash.cpp).
//
// One hash a domain, so a difference says where it is. The hash reads the state and never
// changes it, and it folds a pointer in as the 1.17 address it stands for, so two runs of
// one game hash alike wherever the program was loaded.
#ifndef PORT_SYNCHASH_H
#define PORT_SYNCHASH_H

#include <stdint.h>

enum {
  SYNC_UNITS,     // the unit table's units in use, and the order of its lists
  SYNC_OBJECTS,   // bullets, orders, the doodad thingies, the dummy flingy
  SYNC_PLAYERS,   // the per-player tables, 1.17's block and the slots past 11
  SYNC_MAP,       // the tile maps, creep, and the vision of the slots past 7
  SYNC_SPRITES,   // the sprites and images of units, bullets and thingies, less the view's
  SYNC_AI,        // the computers' towns, workers, buildings, captains, soldiers, guard posts,
                  // scripts, bankers and town plans, the nodes of each pool in use
  SYNC_DOMAINS
};

// The version of what the hash reads and how; a replay's records of another are not
// compared, nor a save's. 3 (2026-10-11): a 4-byte field padded to 8 is its 4 bytes, but
// in the four slots that can hold a unit (port_synchash.cpp, PointerSlot). 4 (2026-10-11):
// the cargo list's ids are read as 32 bits, and a sprite's images are read past
// a selection circle or health bar, whose images come from stores of their own: the walk
// stopped there, so a sprite the local player had selected was read without its images.
// 5 (2026-10-12): the black X is left out, a thingy the local player's right-clicks on the
// ground move, which a playback does not.
// 6 (2026-10-12): a record is hashed as one run of its fields in 1.17's form, a pointer as
// its key, so that the hash follows the values and not how the layout splits the record:
// a map position, 16 bits while positions wrap, hashes the same inside a run of bytes or
// as a field of its own type (h/pos.h, stage E).
// 7 (2026-10-12): a pointer's slot that holds a value of 32 bits is hashed as that value, a
// mineral field's resources or a rally point, where it was hashed as an unknown pointer.
// 8 (2026-10-12): the AI is a domain of its own (SYNC_AI), as the save keeps it; and a
// pointer to a unit is its slot, and one to a captain its nation and area, where a unit past
// 1.17's table and every captain (the heap's) were only "set".
// 9 (2026-10-18): the same words, mixed over four chains in turn and folded at the end, which
// one thread runs side by side: the map's chain was the check's longest wait.
#define SYNC_HASH_VERSION 9

struct SyncHash {
  uint32_t dwFrame;
  uint32_t dwRandState;
  uint32_t dwRandCount;
  uint32_t dwVersion;  // SYNC_HASH_VERSION; 0 in a save made before version 3
  uint64_t aqDomain[SYNC_DOMAINS];
};

void SyncHashCompute(SyncHash *pOut);
// The state as one number: every domain's hash and the random state folded, as a benchmark's
// summary gives it (end_state) and the AI lab compares a game's two plays by.
uint64_t SyncHashFold(void);
// A domain's name, as the logs and the live channel give it: "units", "objects" and so on.
const char *SyncDomainName(int nDomain);
// What the checks since the last call took, for HYPERION_PERF's log: each domain's ms and the
// game's wait (the slowest domain, with the threads' start), summed, each domain's words
// mixed, and how many checks.
void SyncHashTimes(double adDomainMs[SYNC_DOMAINS], double adDomainWords[SYNC_DOMAINS], double *pdWaitMs,
                   int *pnChecks);
// Each domain's bytes, as the hash reads them, to a file for comparing two runs.
bool SyncHashDump(const char *pszPath);

// A slot's numbers through the player tables, for the replay harness's observer
// (port_observe.cpp), which logs every slot of a Hyperion game.
struct SyncSlot {
  int32_t nMinerals, nGas;
  int32_t nMineralsGathered, nGasGathered;
  uint32_t adwScores[28];
  uint32_t dwTables;         // the unit counts, tech and upgrades, folded
};
void SyncSlotRead(int nSlot, SyncSlot *pOut);

#endif
