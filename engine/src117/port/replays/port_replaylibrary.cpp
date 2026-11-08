// port_replaylibrary.cpp -- versioned replay organization, deterministic
// import/export and crash-safe whole-file persistence. No engine state is read.
#include "../../h/port_replaylibrary.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <set>

#include "../platform/port_json.h"

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#elif !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ReplayLibrary {
namespace {

namespace fs = std::filesystem;

PortJson String(const std::string &s)
{
  PortJson v;
  v.kind = PortJson::STRING;
  v.s = s;
  return v;
}

PortJson Number(int64_t n)
{
  PortJson v;
  v.kind = PortJson::NUMBER;
  v.d = static_cast<double>(n);
  return v;
}

PortJson Boolean(bool b)
{
  PortJson v;
  v.kind = PortJson::BOOL;
  v.b = b;
  return v;
}

PortJson Array()
{
  PortJson v;
  v.kind = PortJson::ARRAY;
  return v;
}

PortJson Object()
{
  PortJson v;
  v.kind = PortJson::OBJECT;
  return v;
}

void Add(PortJson *object, const std::string &name, const PortJson &value)
{
  object->members.push_back({name, value});
}

PortJson Strings(const std::vector<std::string> &strings)
{
  PortJson out = Array();
  for (const std::string &s : strings)
    out.items.push_back(String(s));
  return out;
}

bool Fail(std::string *pError, const std::string &text)
{
  if (pError)
    *pError = text;
  return false;
}

bool Text(const PortJson &object, const char *name, std::string *pOut, size_t maxBytes, std::string *pError,
          bool bRequired = false)
{
  const PortJson &v = object[name];
  if (v.kind == PortJson::NONE || v.kind == PortJson::NULLV) {
    if (bRequired)
      return Fail(pError, std::string("missing text: ") + name);
    pOut->clear();
    return true;
  }
  if (v.kind != PortJson::STRING || v.s.size() > maxBytes)
    return Fail(pError, std::string("invalid text: ") + name);
  *pOut = v.s;
  return true;
}

bool Integer(const PortJson &object, const char *name, int64_t *pOut, std::string *pError)
{
  const PortJson &v = object[name];
  if (v.kind == PortJson::NONE || v.kind == PortJson::NULLV) {
    *pOut = 0;
    return true;
  }
  if (v.kind != PortJson::NUMBER || v.d < 0 || v.d > 9007199254740991.0)
    return Fail(pError, std::string("invalid integer: ") + name);
  *pOut = static_cast<int64_t>(v.d);
  return true;
}

bool Flag(const PortJson &object, const char *name, bool *pOut, std::string *pError)
{
  const PortJson &v = object[name];
  if (v.kind == PortJson::NONE || v.kind == PortJson::NULLV) {
    *pOut = false;
    return true;
  }
  if (v.kind != PortJson::BOOL)
    return Fail(pError, std::string("invalid Boolean: ") + name);
  *pOut = v.b;
  return true;
}

bool StringList(const PortJson &object, const char *name, size_t maxItems, size_t maxBytes,
                std::vector<std::string> *pOut, std::string *pError)
{
  pOut->clear();
  const PortJson &v = object[name];
  if (v.kind == PortJson::NONE || v.kind == PortJson::NULLV)
    return true;
  if (v.kind != PortJson::ARRAY || v.items.size() > maxItems)
    return Fail(pError, std::string("invalid string array: ") + name);
  for (const PortJson &item : v.items) {
    if (item.kind != PortJson::STRING || item.s.size() > maxBytes)
      return Fail(pError, std::string("invalid string array item: ") + name);
    pOut->push_back(item.s);
  }
  return true;
}

PortJson NoteJson(const Note &note)
{
  PortJson out = Object();
  Add(&out, "id", String(note.id));
  Add(&out, "text", String(note.text));
  Add(&out, "tags", Strings(note.tags));
  Add(&out, "shared", Boolean(note.bShared));
  Add(&out, "updatedAt", Number(note.updatedAt));
  return out;
}

PortJson SourceJson(const Source &source)
{
  PortJson out = Object();
  Add(&out, "serverApi", String(source.serverApi));
  Add(&out, "matchUuid", String(source.matchUuid));
  return out;
}

PortJson RecordJson(const Record &record, bool bFavourite, bool bNotes, bool bSharedOnly)
{
  PortJson out = Object();
  Add(&out, "id", String(record.id));
  if (bFavourite)
    Add(&out, "favourite", Boolean(record.bFavourite));
  Add(&out, "startedAt", Number(record.startedAt));
  PortJson sources = Array();
  for (const Source &source : record.sources)
    sources.items.push_back(SourceJson(source));
  Add(&out, "sources", sources);
  PortJson notes = Array();
  if (bNotes)
    for (const Note &note : record.notes)
      if (!bSharedOnly || note.bShared)
        notes.items.push_back(NoteJson(note));
  Add(&out, "notes", notes);
  return out;
}

PortJson FiltersJson(const Filters &filters)
{
  PortJson out = Object();
  Add(&out, "search", String(filters.search));
  Add(&out, "who", String(filters.who));
  Add(&out, "matchup", String(filters.matchup));
  Add(&out, "race", String(filters.race));
  Add(&out, "result", String(filters.result));
  Add(&out, "from", String(filters.from));
  Add(&out, "to", String(filters.to));
  Add(&out, "followedBy", String(filters.followedBy));
  Add(&out, "serverApi", String(filters.serverApi));
  Add(&out, "format", Number(filters.format));
  Add(&out, "game", Number(filters.game));
  Add(&out, "favourites", Boolean(filters.bFavourites));
  Add(&out, "started", Boolean(filters.bStarted));
  Add(&out, "notes", Boolean(filters.bNotes));
  return out;
}

PortJson QueryJson(const SavedQuery &query)
{
  PortJson out = Object();
  Add(&out, "id", String(query.id));
  Add(&out, "name", String(query.name));
  Add(&out, "filters", FiltersJson(query.filters));
  Add(&out, "updatedAt", Number(query.updatedAt));
  return out;
}

PortJson CollectionJson(const Collection &list)
{
  PortJson out = Object();
  Add(&out, "id", String(list.id));
  Add(&out, "name", String(list.name));
  Add(&out, "members", Strings(list.recordIds));
  Add(&out, "updatedAt", Number(list.updatedAt));
  return out;
}

PortJson Document(const Data &data, const char *format)
{
  PortJson root = Object();
  Add(&root, "format", String(format));
  Add(&root, "version", Number(1));
  PortJson records = Array();
  for (const Record &record : data.records)
    records.items.push_back(RecordJson(record, true, true, false));
  Add(&root, "records", records);
  PortJson queries = Array();
  for (const SavedQuery &query : data.queries)
    queries.items.push_back(QueryJson(query));
  Add(&root, "queries", queries);
  PortJson lists = Array();
  for (const Collection &list : data.collections)
    lists.items.push_back(CollectionJson(list));
  Add(&root, "collections", lists);
  return root;
}

bool ParseFilters(const PortJson &v, Filters *pOut, std::string *pError)
{
  if (v.kind != PortJson::OBJECT)
    return Fail(pError, "saved query filters are not an object");
  const std::pair<const char *, std::string Filters::*> fields[] = {
      {"search", &Filters::search}, {"who", &Filters::who},       {"matchup", &Filters::matchup},
      {"race", &Filters::race},     {"result", &Filters::result}, {"from", &Filters::from},
      {"to", &Filters::to},         {"followedBy", &Filters::followedBy}, {"serverApi", &Filters::serverApi}};
  for (const auto &field : fields)
    if (!Text(v, field.first, &(pOut->*(field.second)), 4096, pError))
      return false;
  int64_t number = 0;
  if (!Integer(v, "format", &number, pError) || number > 16)
    return false;
  pOut->format = static_cast<int>(number);
  if (!Integer(v, "game", &number, pError) || number > 16)
    return false;
  pOut->game = static_cast<int>(number);
  return Flag(v, "favourites", &pOut->bFavourites, pError) && Flag(v, "started", &pOut->bStarted, pError) &&
         Flag(v, "notes", &pOut->bNotes, pError);
}

bool ParseDocument(const std::string &json, Data *pOut, std::string *pError, bool bExport)
{
  PortJson root;
  if (!PortJsonParse(json.c_str(), &root, pError))
    return false;
  if (root.kind != PortJson::OBJECT)
    return Fail(pError, "The replay library document is not an object.");
  const std::string expected = bExport ? "hyperion/replay-library-export" : "hyperion/replay-library";
  if (root["format"].Str() != expected || root["version"].Num() != 1)
    return Fail(pError, "Unsupported replay library schema or version.");
  if (root["records"].kind != PortJson::ARRAY || root["records"].items.size() > 100000 ||
      root["queries"].kind != PortJson::ARRAY || root["queries"].items.size() > 2000 ||
      root["collections"].kind != PortJson::ARRAY || root["collections"].items.size() > 2000)
    return Fail(pError, "Replay library arrays are missing or exceed their bounds.");
  Data data;
  for (const PortJson &row : root["records"].items) {
    if (row.kind != PortJson::OBJECT)
      return Fail(pError, "A replay record is not an object.");
    Record record;
    if (!Text(row, "id", &record.id, 512, pError, true) || record.id.empty() ||
        !Flag(row, "favourite", &record.bFavourite, pError) || !Integer(row, "startedAt", &record.startedAt, pError))
      return false;
    if (row["sources"].kind != PortJson::ARRAY || row["sources"].items.size() > 128)
      return Fail(pError, "A replay record has invalid sources.");
    for (const PortJson &item : row["sources"].items) {
      Source source;
      if (item.kind != PortJson::OBJECT || !Text(item, "serverApi", &source.serverApi, 2048, pError, true) ||
          !Text(item, "matchUuid", &source.matchUuid, 128, pError, true))
        return false;
      ReplayKraken::Endpoint endpoint;
      if (!ReplayKraken::EndpointParse(source.serverApi, &endpoint, pError) || endpoint.api != source.serverApi ||
          !ReplayKraken::MatchUuidValid(source.matchUuid))
        return Fail(pError, "A replay source has an invalid canonical server or UUID.");
      record.sources.push_back(source);
    }
    if (row["notes"].kind != PortJson::ARRAY || row["notes"].items.size() > 64)
      return Fail(pError, "A replay record has invalid notes.");
    for (const PortJson &item : row["notes"].items) {
      Note note;
      if (item.kind != PortJson::OBJECT || !Text(item, "id", &note.id, 128, pError, true) || note.id.empty() ||
          !Text(item, "text", &note.text, 65536, pError) ||
          !StringList(item, "tags", 32, 128, &note.tags, pError) || !Flag(item, "shared", &note.bShared, pError) ||
          !Integer(item, "updatedAt", &note.updatedAt, pError))
        return false;
      record.notes.push_back(note);
    }
    data.records.push_back(record);
  }
  for (const PortJson &row : root["queries"].items) {
    SavedQuery query;
    if (row.kind != PortJson::OBJECT || !Text(row, "id", &query.id, 128, pError, true) || query.id.empty() ||
        !Text(row, "name", &query.name, 256, pError, true) ||
        !ParseFilters(row["filters"], &query.filters, pError) || !Integer(row, "updatedAt", &query.updatedAt, pError))
      return false;
    data.queries.push_back(query);
  }
  for (const PortJson &row : root["collections"].items) {
    Collection list;
    if (row.kind != PortJson::OBJECT ||
        !Text(row, "id", &list.id, 128, pError, true) || list.id.empty() ||
        !Text(row, "name", &list.name, 256, pError, true) ||
        !StringList(row, "members", 100000, 512, &list.recordIds, pError) ||
        !Integer(row, "updatedAt", &list.updatedAt, pError))
      return false;
    data.collections.push_back(list);
  }
  *pOut = std::move(data);
  return true;
}

void MergeRecord(Record *into, const Record &from)
{
  into->bFavourite = into->bFavourite || from.bFavourite;
  into->startedAt = std::max(into->startedAt, from.startedAt);
  for (const Source &source : from.sources)
    if (std::find_if(into->sources.begin(), into->sources.end(), [&](const Source &old) {
          return old.serverApi == source.serverApi && old.matchUuid == source.matchUuid;
        }) == into->sources.end())
      into->sources.push_back(source);
  for (const Note &note : from.notes) {
    auto at = std::find_if(into->notes.begin(), into->notes.end(), [&](const Note &old) { return old.id == note.id; });
    if (at == into->notes.end())
      into->notes.push_back(note);
    else if (note.updatedAt > at->updatedAt || (note.updatedAt == at->updatedAt && PortJsonWrite(NoteJson(note)) >
                                               PortJsonWrite(NoteJson(*at))))
      *at = note;
  }
  std::sort(into->sources.begin(), into->sources.end(), [](const Source &a, const Source &b) {
    return a.serverApi != b.serverApi ? a.serverApi < b.serverApi : a.matchUuid < b.matchUuid;
  });
  std::sort(into->notes.begin(), into->notes.end(), [](const Note &a, const Note &b) { return a.id < b.id; });
}

void Merge(Data *into, const Data &from)
{
  for (const Record &record : from.records) {
    Record *old = Find(into, record.id);
    if (!old) {
      into->records.push_back(record);
      old = &into->records.back();
    }
    MergeRecord(old, record);
  }
  for (const SavedQuery &query : from.queries) {
    auto at = std::find_if(into->queries.begin(), into->queries.end(), [&](const SavedQuery &old) {
      return old.id == query.id;
    });
    if (at == into->queries.end())
      into->queries.push_back(query);
    else if (query.updatedAt > at->updatedAt || (query.updatedAt == at->updatedAt && PortJsonWrite(QueryJson(query)) >
                                                PortJsonWrite(QueryJson(*at))))
      *at = query;
  }
  for (const Collection &list : from.collections) {
    auto at = std::find_if(into->collections.begin(), into->collections.end(), [&](const Collection &old) {
      return old.id == list.id;
    });
    if (at == into->collections.end()) {
      into->collections.push_back(list);
      continue;
    }
    for (const std::string &id : list.recordIds)
      if (!InCollection(*at, id))
        at->recordIds.push_back(id);
    std::sort(at->recordIds.begin(), at->recordIds.end());
    if (list.updatedAt > at->updatedAt || (list.updatedAt == at->updatedAt && list.name > at->name))
      at->name = list.name;
    at->updatedAt = std::max(at->updatedAt, list.updatedAt);
  }
  std::sort(into->records.begin(), into->records.end(), [](const Record &a, const Record &b) { return a.id < b.id; });
  std::sort(into->queries.begin(), into->queries.end(), [](const SavedQuery &a, const SavedQuery &b) {
    return a.id < b.id;
  });
  std::sort(into->collections.begin(), into->collections.end(), [](const Collection &a, const Collection &b) {
    return a.id < b.id;
  });
}

fs::path PathOf(const std::string &text)
{
  std::string native = text;
  std::replace(native.begin(), native.end(), '\\', '/');
#if !defined(_WIN32)
  if (native.size() > 2 && native[1] == ':' && native[2] == '/')
    native.erase(0, 2);
#endif
  const std::u8string utf8(native.begin(), native.end());
  return fs::path(utf8);
}

FILE *Open(const fs::path &path, const char *mode)
{
#if defined(_WIN32)
  const std::wstring wideMode(mode, mode + strlen(mode));
  return _wfopen(path.c_str(), wideMode.c_str());
#else
  return fopen(path.c_str(), mode);
#endif
}

void SyncDirectory(const fs::path &path)
{
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
  const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd >= 0) {
    fsync(fd);
    close(fd);
  }
#else
  (void)path;
#endif
}

}  // namespace

Record *Find(Data *data, const std::string &id)
{
  const auto at = std::find_if(data->records.begin(), data->records.end(), [&](const Record &r) { return r.id == id; });
  return at == data->records.end() ? nullptr : &*at;
}

const Record *Find(const Data &data, const std::string &id)
{
  const auto at = std::find_if(data.records.begin(), data.records.end(), [&](const Record &r) { return r.id == id; });
  return at == data.records.end() ? nullptr : &*at;
}

Record &For(Data *data, const std::string &id)
{
  Record *record = Find(data, id);
  if (!record) {
    data->records.push_back(Record());
    data->records.back().id = id;
    record = &data->records.back();
  }
  return *record;
}

void MarkStarted(Data *data, const std::string &id, int64_t at)
{
  Record &record = For(data, id);
  record.startedAt = std::max(record.startedAt, at);
}

void LinkDownload(Data *data, const ReplayKraken::Endpoint &server, const std::string &uuid,
                  const std::string &contentId)
{
  const std::string serverId = ReplayKraken::ServerRecordId(server, uuid);
  const Record *found = Find(data, serverId);
  const Record remote = found ? *found : Record();  // For may reallocate records; no borrowed record survives it.
  Record &local = For(data, contentId);
  if (found)
    MergeRecord(&local, remote);
  const Source source{server.api, uuid};
  if (std::find_if(local.sources.begin(), local.sources.end(), [&](const Source &old) {
        return old.serverApi == source.serverApi && old.matchUuid == source.matchUuid;
      }) == local.sources.end())
    local.sources.push_back(source);
  if (found)
    data->records.erase(std::remove_if(data->records.begin(), data->records.end(), [&](const Record &record) {
      return record.id == serverId;
    }), data->records.end());
  for (Collection &list : data->collections) {
    for (std::string &id : list.recordIds)
      if (id == serverId)
        id = contentId;
    std::sort(list.recordIds.begin(), list.recordIds.end());
    list.recordIds.erase(std::unique(list.recordIds.begin(), list.recordIds.end()), list.recordIds.end());
  }
}

bool HasNotes(const Data &data, const std::string &id)
{
  const Record *record = Find(data, id);
  return record && std::any_of(record->notes.begin(), record->notes.end(), [](const Note &note) {
    return !note.text.empty() || !note.tags.empty();
  });
}

bool InCollection(const Collection &list, const std::string &id)
{
  return std::find(list.recordIds.begin(), list.recordIds.end(), id) != list.recordIds.end();
}

std::string ToJson(const Data &data)
{
  return PortJsonWrite(Document(data, "hyperion/replay-library"));
}

bool FromJson(const std::string &json, Data *pOut, std::string *pError)
{
  Data data;
  if (!ParseDocument(json, &data, pError, false))
    return false;
  Merge(pOut, data);
  return true;
}

bool AtomicWrite(const std::string &path, const std::string &text, std::string *pError)
{
  std::error_code ec;
  const fs::path target = PathOf(path);
  if (!target.parent_path().empty())
    fs::create_directories(target.parent_path(), ec);
  if (ec)
    return Fail(pError, "Cannot create the replay data folder: " + ec.message());
  const fs::path temp = PathOf(path + ".tmp-" + ReplayKraken::UuidNew());
  FILE *file = Open(temp, "wb");
  if (!file)
    return Fail(pError, "Cannot create the temporary replay data file.");
  const bool bWritten = text.empty() || fwrite(text.data(), 1, text.size(), file) == text.size();
  const bool bFlushed = fflush(file) == 0;
#if defined(_WIN32)
  const bool bSynced = _commit(_fileno(file)) == 0;
#elif !defined(__EMSCRIPTEN__)
  const bool bSynced = fsync(fileno(file)) == 0;
#else
  const bool bSynced = true;
#endif
  const bool bClosed = fclose(file) == 0;
  if (!bWritten || !bFlushed || !bSynced || !bClosed) {
    fs::remove(temp, ec);
    return Fail(pError, "Cannot write or flush the replay data file; the previous file is unchanged.");
  }
#if defined(_WIN32)
  if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    fs::remove(temp, ec);
    return Fail(pError, "Cannot replace the replay data file atomically.");
  }
#else
  fs::rename(temp, target, ec);
  if (ec) {
    fs::remove(temp, ec);
    return Fail(pError, "Cannot replace the replay data file atomically.");
  }
  SyncDirectory(target.parent_path());
#endif
  return true;
}

bool ReadBounded(const std::string &path, size_t maxBytes, std::string *pOut, std::string *pError)
{
  pOut->clear();
  std::error_code ec;
  if (!fs::exists(PathOf(path), ec)) {
    if (pError)
      pError->clear();
    return false;
  }
  FILE *file = Open(PathOf(path), "rb");
  if (!file)
    return Fail(pError, "The replay data file exists but cannot be opened.");
  char buffer[65536];
  bool bOk = true;
  while (FILE *current = file) {
    const size_t got = fread(buffer, 1, sizeof buffer, current);
    if (got > maxBytes - std::min(maxBytes, pOut->size())) {
      bOk = false;
      break;
    }
    pOut->append(buffer, got);
    if (got < sizeof buffer) {
      if (ferror(file))
        bOk = false;
      break;
    }
  }
  fclose(file);
  if (!bOk) {
    pOut->clear();
    return Fail(pError, "The replay data file is unreadable or exceeds its size limit.");
  }
  return true;
}

bool LoadAtomic(const std::string &path, Data *pOut, std::string *pError)
{
  std::string json;
  if (!ReadBounded(path, 32 * 1024 * 1024, &json, pError)) {
    if (pError && pError->empty()) {
      *pOut = Data();
      return true;  // A first run has no library file.
    }
    return false;
  }
  Data loaded;
  if (!FromJson(json, &loaded, pError))
    return false;
  *pOut = std::move(loaded);
  return true;
}

std::string ExportJson(const Data &data, const ExportSelection &selection)
{
  PortJson root = Object();
  Add(&root, "format", String("hyperion/replay-library-export"));
  Add(&root, "version", Number(1));
  const std::set<std::string> records(selection.recordIds.begin(), selection.recordIds.end());
  const std::set<std::string> queries(selection.queryIds.begin(), selection.queryIds.end());
  const std::set<std::string> lists(selection.collectionIds.begin(), selection.collectionIds.end());
  PortJson exportedRecords = Array();
  for (const Record &record : data.records)
    if (records.count(record.id))
      exportedRecords.items.push_back(RecordJson(record, selection.bFavourites, selection.bSharedNotes, true));
  Add(&root, "records", exportedRecords);
  PortJson exportedQueries = Array();
  for (const SavedQuery &query : data.queries)
    if (queries.count(query.id))
      exportedQueries.items.push_back(QueryJson(query));
  Add(&root, "queries", exportedQueries);
  PortJson exportedLists = Array();
  for (const Collection &list : data.collections)
    if (lists.count(list.id))
      exportedLists.items.push_back(CollectionJson(list));
  Add(&root, "collections", exportedLists);
  return PortJsonWrite(root);
}

bool ImportJson(const std::string &json, Data *pInto, std::string *pError)
{
  Data imported;
  if (!ParseDocument(json, &imported, pError, true))
    return false;
  Merge(pInto, imported);
  return true;
}

int64_t NowSeconds()
{
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace ReplayLibrary
