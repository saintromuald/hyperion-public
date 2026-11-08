// port_replaycatalog.h -- incremental local replay discovery and its persistent
// metadata cache. Pump and every engine-bound call run on the main thread.
#ifndef PORT_REPLAYCATALOG_H
#define PORT_REPLAYCATALOG_H

#include <cstdint>
#include <string>
#include <vector>

#include "port_hxreplay.h"
#include "port_replaykraken.h"

namespace ReplayCatalog {

struct Player {
  std::string name;
  int nRace = -1;
  int nType = 0;
  int nTeam = 0;
  int nColour = -1;
};

struct Entry {
  std::string path;
  std::string rel;
  std::string name;
  std::string contentId;
  uint64_t when = 0;  // FILETIME ticks, for the existing display helpers.
  uint64_t bytes = 0;
  uint64_t changed = 0;
  bool bPending = true;
  bool bDeferred = false;
  bool bOk = false;
  std::string error;
  ReplayHxInfo info;
  std::vector<Player> players;
  std::string map;
  std::string type;
  std::string matchup;
  bool bExpansion = false;
  unsigned dwFrames = 0;
  int nSpeed = 6;
  int nWidth = 0, nHeight = 0, nTileset = 0, nGameType = 0;
  unsigned nMission = 0;
};

struct Status {
  size_t discovered = 0;
  size_t pending = 0;
  size_t deferred = 0;
  size_t unreadable = 0;
  size_t limit = 0;
  size_t depth = 0;
  bool bLimited = false;
  bool bScanning = false;
  std::string note;
  double worstEnginePeekMs = 0.0;
  double lastEngineBatchMs = 0.0;
  uint64_t generation = 0;
};

void Open(const std::string &root);
void Close();  // Cancels superseded work; process-lifetime worker state is not freed.
void Refresh();
// Applies completed scans and parses at most a small time slice of new/changed
// files. Returns true when Entries or Status changed.
bool Pump(double engineBudgetMs = 1.5);
const std::vector<Entry> &Entries();
Status GetStatus();
// Records a main-thread rename/delete without waiting for the next scan.
void Renamed(const std::string &oldPath, const std::string &newPath);
void Deleted(const std::string &path);
// Pure helpers shared with upload metadata construction and tests.
std::string MatchupOf(const Entry &entry);
bool ReadUploadMetadata(const Entry &entry, ReplayKraken::UploadMetadata *pOut);
// Synthetic summaries for the 10,000-row rendering/query budget check.
std::vector<Entry> Synthetic(size_t count);

}  // namespace ReplayCatalog

#endif
