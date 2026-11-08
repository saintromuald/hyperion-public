// port_replaylibrary.h -- persistent, source-aware replay organization. The
// data model is independent of Clay and of live game state.
#ifndef PORT_REPLAYLIBRARY_H
#define PORT_REPLAYLIBRARY_H

#include <cstdint>
#include <string>
#include <vector>

#include "port_replaykraken.h"

namespace ReplayLibrary {

struct Note {
  std::string id;
  std::string text;
  std::vector<std::string> tags;
  bool bShared = false;  // Every new note is private unless the user explicitly changes this.
  int64_t updatedAt = 0;
};

struct Source {
  std::string serverApi;
  std::string matchUuid;
};

struct Record {
  std::string id;  // sha256:<content> or server:<origin-hash>:<uuid>
  bool bFavourite = false;
  int64_t startedAt = 0;  // Playback accepted by Hyperion, not a claimed full watch.
  std::vector<Source> sources;  // Preserved for source-safe download after restart/export.
  std::vector<Note> notes;
};

struct Filters {
  std::string search;
  std::string who;
  std::string matchup;
  std::string race;
  std::string result;
  std::string from;
  std::string to;
  std::string followedBy;
  std::string serverApi;
  int format = 0;
  int game = 0;
  bool bFavourites = false;
  bool bStarted = false;
  bool bNotes = false;
};

struct SavedQuery {
  std::string id;
  std::string name;
  Filters filters;  // Dynamic: evaluated against the whole catalog, never rendered-row IDs.
  int64_t updatedAt = 0;
};

struct Collection {
  std::string id;
  std::string name;
  std::vector<std::string> recordIds;  // Fixed: applying it restricts results to these members.
  int64_t updatedAt = 0;
};

struct Data {
  std::vector<Record> records;
  std::vector<SavedQuery> queries;
  std::vector<Collection> collections;
};

Record *Find(Data *data, const std::string &id);
const Record *Find(const Data &data, const std::string &id);
Record &For(Data *data, const std::string &id);
void MarkStarted(Data *data, const std::string &id, int64_t at);
// Links a downloaded server row to its content record without losing notes,
// favourites or Started history from either identity.
void LinkDownload(Data *data, const ReplayKraken::Endpoint &server, const std::string &uuid,
                  const std::string &contentId);
bool HasNotes(const Data &data, const std::string &id);
bool InCollection(const Collection &list, const std::string &id);

std::string ToJson(const Data &data);
bool FromJson(const std::string &json, Data *pOut, std::string *pError);
// Atomic same-directory write. Call off the game/UI thread. On failure the old
// file remains and the unique temporary is removed where possible.
bool AtomicWrite(const std::string &path, const std::string &text, std::string *pError);
bool ReadBounded(const std::string &path, size_t maxBytes, std::string *pOut, std::string *pError);
bool LoadAtomic(const std::string &path, Data *pOut, std::string *pError);

struct ExportSelection {
  std::vector<std::string> recordIds;     // Explicit records chosen by the user.
  std::vector<std::string> queryIds;      // Explicit dynamic queries.
  std::vector<std::string> collectionIds; // Explicit fixed lists.
  bool bFavourites = false;
  bool bSharedNotes = false;
};
// Hyperion JSON schema hyperion/replay-library-export, version 1. Private notes
// and their tags are excluded inside this service boundary, not left to callers.
std::string ExportJson(const Data &data, const ExportSelection &selection);
// Stable-ID, deterministic merge. Reimporting the same document is idempotent;
// private local notes are never deleted by an import that lacks them.
bool ImportJson(const std::string &json, Data *pInto, std::string *pError);

int64_t NowSeconds();

}  // namespace ReplayLibrary

#endif
