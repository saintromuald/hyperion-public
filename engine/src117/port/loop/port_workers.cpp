// port_workers.cpp -- the pool of worker threads (port_workers.h; docs/modules/loop.md, "The
// worker pool").
//
// The threads start on the first call and wait on a condition variable between calls, so a
// check does not pay for starting threads, which come up on cold cores. A call posts a batch of
// n items; every free thread takes the newest batch's next item, so a batch posted from inside
// an item (a domain's parts) is worked on before the rest of its parent's. The batches' state
// is under one mutex, taken once an item: a check is a few hundred items.
#include "port_workers.h"

#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include "../platform/port_settings.h"
#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

struct Batch {
  const std::function<void(int)> *pFn;
  int n;
  int nNext;  // the next item to take
  int nDone;  // the items run
  std::vector<char> *pRun;  // PortWorkersForInOrder's: which items have run, each told as it does
};

// Kept for the program's life and never destroyed: the threads wait on it until the process
// ends, and a destructor at exit would pull it from under them.
// The workers wait on one condition and the threads waiting for a batch on another, so a batch
// posted wakes as many workers as it has items, not every thread waiting on anything: with a
// thread for every core, waking all of them for a few parts cost a 64-player game about 80 us
// a frame.
struct Pool {
  std::mutex m;
  std::condition_variable cv;    // a batch's item done, for the threads waiting on it
  std::condition_variable work;  // a batch posted, for the workers
  std::vector<Batch *> batches;  // the batches with items not yet taken, the newest last
  int nThreads = 1;  // the calling thread and the workers
  int nIdle = 0;     // the workers waiting on work
};

Pool *g_pool;
std::once_flag g_started;

// Wakes a worker for each of n items, as many as are waiting; the lock held.
void Wake(int n) {
  if (n >= g_pool->nIdle)
    g_pool->work.notify_all();
  else
    for (int i = 0; i < n; i++)
      g_pool->work.notify_one();
}

int Wanted(void) {
  if (const char *psz = getenv("HYPERION_WORKERS")) {
    const int n = atoi(psz);
    if (n >= 1)
      return n < 64 ? n : 64;
  }
#if defined(__EMSCRIPTEN__)
  return 1;
#else
  // Settings, "Processor threads": 0 Auto, else the number
  double d = 0.0;
  if (PortSettingNumber("hyperion", "workerThreads", &d) && d >= 1.0)
    return d < 64.0 ? static_cast<int>(d) : 64;
  const int nHardware = static_cast<int>(std::thread::hardware_concurrency());
  const int n = nHardware / 2;
  return n < 1 ? 1 : (n > 16 ? 16 : n);
#endif
}

// The newest batch's next item, the lock held; false when no batch has one.
bool Take(Batch **ppBatch, int *pnItem) {
  std::vector<Batch *> &batches = g_pool->batches;
  while (!batches.empty()) {
    Batch *b = batches.back();
    if (b->nNext < b->n) {
      *ppBatch = b;
      *pnItem = b->nNext++;
      if (b->nNext == b->n)
        batches.pop_back();
      return true;
    }
    batches.pop_back();
  }
  return false;
}

// Runs an item with the lock let go, and counts it done.
void Run(std::unique_lock<std::mutex> &lock, Batch *b, int nItem) {
  lock.unlock();
  (*b->pFn)(nItem);
  lock.lock();
  if (b->pRun != NULL) {
    (*b->pRun)[nItem] = 1;
    if (++b->nDone == b->n || nItem == 0 || (*b->pRun)[nItem - 1])
      g_pool->cv.notify_all();  // the caller may be waiting for this one
    return;
  }
  if (++b->nDone == b->n)
    g_pool->cv.notify_all();
}

void Worker(void) {
  std::unique_lock<std::mutex> lock(g_pool->m);
  for (;;) {
    Batch *b;
    int nItem;
    if (Take(&b, &nItem)) {
      Run(lock, b, nItem);
    } else {
      ++g_pool->nIdle;
      g_pool->work.wait(lock);
      --g_pool->nIdle;
    }
  }
}

void Start(void) {
  g_pool = new Pool;
#if !defined(__EMSCRIPTEN__)
  g_pool->nThreads = Wanted();
  for (int i = 1; i < g_pool->nThreads; i++)
    std::thread(Worker).detach();
#endif
}

}  // namespace

int PortWorkersCount(void) {
  std::call_once(g_started, Start);
  return g_pool->nThreads;
}

void PortWorkersFor(int n, const std::function<void(int)> &fn) {
  if (n <= 0)
    return;
  if (n == 1 || PortWorkersCount() <= 1) {
    for (int i = 0; i < n; i++)
      fn(i);
    return;
  }
  Batch batch = {&fn, n, 0, 0};
  std::unique_lock<std::mutex> lock(g_pool->m);
  g_pool->batches.push_back(&batch);
  Wake(n - 1);  // the calling thread takes one
  // the calling thread takes its own batch's items first, then any other's while it waits
  while (batch.nDone < batch.n) {
    Batch *b = &batch;
    int nItem;
    if (batch.nNext < batch.n) {
      nItem = batch.nNext++;
      if (batch.nNext == batch.n) {
        std::vector<Batch *> &batches = g_pool->batches;
        batches.erase(std::remove(batches.begin(), batches.end(), &batch), batches.end());
      }
    } else if (!Take(&b, &nItem)) {
      g_pool->cv.wait(lock);
      continue;
    }
    Run(lock, b, nItem);
  }
}

void PortWorkersForInOrder(int n, const std::function<void(int)> &fn, const std::function<void(int)> &done) {
  if (n <= 0)
    return;
  if (n == 1 || PortWorkersCount() <= 1) {
    for (int i = 0; i < n; i++) {
      fn(i);
      done(i);
    }
    return;
  }
  std::vector<char> run(n, 0);
  Batch batch = {&fn, n, 0, 0, &run};
  std::unique_lock<std::mutex> lock(g_pool->m);
  g_pool->batches.push_back(&batch);
  Wake(n - 1);
  // the caller tells each next item done as it runs (the order's own work, a chain mixed in
  // order), else runs its own batch's items, then waits: another batch's would hold that up
  int nTold = 0;
  while (nTold < n) {
    if (run[nTold]) {
      lock.unlock();
      done(nTold);
      lock.lock();
      ++nTold;
      continue;
    }
    if (batch.nNext >= batch.n) {
      g_pool->cv.wait(lock);
      continue;
    }
    const int nItem = batch.nNext++;
    if (batch.nNext == batch.n) {
      std::vector<Batch *> &batches = g_pool->batches;
      batches.erase(std::remove(batches.begin(), batches.end(), &batch), batches.end());
    }
    Run(lock, &batch, nItem);
  }
  // every item has run and been told; a worker may still be inside Run's count for the last,
  // which holds the lock, so the batch is left once nDone says so
  while (batch.nDone < batch.n)
    g_pool->cv.wait(lock);
}

struct PortWorkersBatch {
  Batch batch;
};

PortWorkersBatch *PortWorkersPost(int n, const std::function<void(int)> &fn) {
  if (n <= 0)
    return NULL;
  if (PortWorkersCount() <= 1) {
    for (int i = 0; i < n; i++)
      fn(i);
    return NULL;
  }
  PortWorkersBatch *p = new PortWorkersBatch{{&fn, n, 0, 0, NULL}};
  std::lock_guard<std::mutex> lock(g_pool->m);
  g_pool->batches.push_back(&p->batch);
  Wake(n);
  return p;
}

void PortWorkersJoin(PortWorkersBatch *p) {
  if (p == NULL)
    return;
  Batch &batch = p->batch;
  {
    std::unique_lock<std::mutex> lock(g_pool->m);
    while (batch.nDone < batch.n) {
      if (batch.nNext >= batch.n) {
        g_pool->cv.wait(lock);
        continue;
      }
      const int nItem = batch.nNext++;
      if (batch.nNext == batch.n) {
        std::vector<Batch *> &batches = g_pool->batches;
        batches.erase(std::remove(batches.begin(), batches.end(), &batch), batches.end());
      }
      Run(lock, &batch, nItem);
    }
  }
  delete p;
}

// The game's cores (port_workers.h). Measured on a 7950X3D, the game's thread alone held on
// each half, three runs each as fast as the replays go (tag t30-cores): on the
// large-cache half the 64-player 8x8 game 370 to 412 frames a second, the 4x4 577 to 616, the
// 32-player 3x3 1,150 to 1,177, and a 1v1 4,693 to 4,560, that half's clocks being lower;
// left to Windows it ran on the other half.
namespace {

int g_nCoresChoice = -1;  // 0 Auto, 1 Large cache, 2 Any, 3 the other cores (measuring)
int g_nCoresOn = -1;      // what the thread is held to: 0 nothing, 1 the large cache, 3 the others

int CoresChoice(void) {
  if (g_nCoresChoice < 0) {
    const char *psz = getenv("HYPERION_CORES");
    double d = 0.0;
    if (psz != NULL && strcmp(psz, "cache") == 0)
      g_nCoresChoice = 1;
    else if (psz != NULL && strcmp(psz, "any") == 0)
      g_nCoresChoice = 2;
    else if (psz != NULL && strcmp(psz, "other") == 0)
      g_nCoresChoice = 3;
    else
      g_nCoresChoice = PortSettingNumber("hyperion", "gameCores", &d) && d >= 0.0 && d <= 2.0 ? static_cast<int>(d) : 0;
  }
  return g_nCoresChoice;
}

#if defined(_WIN32)
// The cores of the largest L3 and the rest, read once; both 0 where every core shares one.
void CoresRead(KAFFINITY *pLargest, KAFFINITY *pOthers) {
  static KAFFINITY s_largest, s_others;
  static bool s_bRead;
  if (!s_bRead) {
    s_bRead = true;
    DWORD nBytes = 0;
    GetLogicalProcessorInformationEx(RelationCache, NULL, &nBytes);
    std::vector<BYTE> info(nBytes);
    if (nBytes != 0 && GetLogicalProcessorInformationEx(
                           RelationCache, reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(info.data()), &nBytes)) {
      KAFFINITY all = 0;
      DWORD nLargest = 0, nSmallest = 0;
      for (DWORD at = 0; at < nBytes;) {
        const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *p =
            reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(info.data() + at);
        if (p->Relationship == RelationCache && p->Cache.Level == 3 && p->Cache.GroupMask.Group == 0) {
          all |= p->Cache.GroupMask.Mask;
          if (p->Cache.CacheSize > nLargest) {
            nLargest = p->Cache.CacheSize;
            s_largest = p->Cache.GroupMask.Mask;
          }
          if (nSmallest == 0 || p->Cache.CacheSize < nSmallest)
            nSmallest = p->Cache.CacheSize;
        }
        at += p->Size;
      }
      if (nSmallest == nLargest)
        s_largest = 0;
      s_others = s_largest != 0 ? all & ~s_largest : 0;
    }
  }
  *pLargest = s_largest;
  *pOthers = s_others;
}
#endif

}  // namespace

void PortCoresFit(unsigned int nUnits) {
#if defined(_WIN32)
  KAFFINITY largest, others;
  CoresRead(&largest, &others);
  if (largest == 0)
    return;
  const int nChoice = CoresChoice();
  int nWant = nChoice == 1 ? 1 : nChoice == 3 ? 3 : 0;
  if (nChoice == 0)
    nWant = nUnits >= 2000 || (g_nCoresOn == 1 && nUnits >= 1500) ? 1 : 0;
  if (nWant == g_nCoresOn)
    return;
  DWORD_PTR process = 0, system = 0;
  GetProcessAffinityMask(GetCurrentProcess(), &process, &system);
  const KAFFINITY mask = nWant == 1 ? largest & process : nWant == 3 ? others & process : process;
  if (mask != 0 && SetThreadAffinityMask(GetCurrentThread(), mask) != 0)
    g_nCoresOn = nWant;
#else
  (void)nUnits;
#endif
}

void PortCoresSet(int nChoice) {
  g_nCoresChoice = nChoice >= 0 && nChoice <= 2 ? nChoice : 0;
}
