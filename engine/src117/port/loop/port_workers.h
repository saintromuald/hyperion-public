// port_workers.h -- a pool of worker threads for work that only reads the game while it stands
// still between frames, such as the state check's domains and their parts
// (port_synchash.cpp). The game itself stays on
// one thread: its order of updates and its one random generator are what make a replay play
// the same game.
#pragma once

#include <functional>

// Runs fn(i) for every i from 0 to n - 1 on the pool's threads and the calling thread, and
// returns once every one has run. Each thread takes the next i as it comes free, so a thread
// on a slow or busy core does fewer. fn may call PortWorkersFor itself (a part of a part):
// the calling thread runs the pool's other items while it waits for its own. In a browser,
// which has no threads, and with HYPERION_WORKERS=1, the calling thread runs them in order.
void PortWorkersFor(int n, const std::function<void(int)> &fn);

// As PortWorkersFor, and done(i) on the calling thread for each i in order, as soon as fn(i)
// has run: the caller's work on the results in order (a hash chain mixed) goes on while later
// items still run, where after PortWorkersFor it waited for the last.
void PortWorkersForInOrder(int n, const std::function<void(int)> &fn, const std::function<void(int)> &done);

// Posts fn(i) for every i from 0 to n - 1 to the pool's threads and returns at once, so the
// calling thread goes on with its own work beside them; PortWorkersJoin then runs any item
// not yet taken and waits for those still running. fn must stay alive until the join. With
// one thread the items run in PortWorkersPost, which returns NULL, and the join does nothing.
struct PortWorkersBatch;
PortWorkersBatch *PortWorkersPost(int n, const std::function<void(int)> &fn);
void PortWorkersJoin(PortWorkersBatch *pBatch);

// The game's thread on a processor whose cores do not all share one L3 cache (a Ryzen X3D's two
// halves): Settings' "Game's cores" ("gameCores": 0 Auto, 1 Large cache, 2 Any;
// HYPERION_CORES=cache, other or any goes before it). Auto holds it on the larger cache's cores
// from 2,000 units and lets go below 1,500. Called from the game's thread every 64 game frames
// (port_perf.cpp); PortCoresSet takes a new choice at the next. Nothing changes on a uniform
// processor or off Windows.
void PortCoresFit(unsigned int nUnits);
void PortCoresSet(int nChoice);

// The threads PortWorkersFor runs on, the calling one included: Settings' "Processor
// threads" ("workerThreads", 0 Auto), Auto being half the processor's threads (its cores,
// where each has two) up to 16; HYPERION_WORKERS goes before the setting. Read once, at the
// first call.
int PortWorkersCount(void);
