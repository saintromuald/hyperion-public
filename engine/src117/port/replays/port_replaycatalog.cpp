// port_replaycatalog.cpp -- incremental, cancellable local replay indexing.
// Workers own paths/bytes/cache DTOs. ReplayHxPeek and text conversion stay in
// Pump on the main thread, in measured slices, because they reach game data.
#include "../../h/port_replaycatalog.h"
#include "../../h/port_replaylibrary.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "../../h/hxsim.h"
#include "../../hyperion/version.h"
#include "../../gen/globals117.h"
#include "../platform/port_json.h"
#include "../platform/port_settings.h"
#include "../platform/port_util.h"
#include "../render/port_maptext.h"
#include "grid.h"

namespace ReplayCatalog {
namespace {

namespace fs = std::filesystem;
using ReplayKraken::UploadMetadata;
using ReplayKraken::UploadPlayer;

struct CachedScan {
  uint64_t generation = 0;
  std::vector<Entry> entries;
  bool bLimited = false;
};

struct Catalog {
  mutable std::mutex m;
  std::condition_variable cv;
  bool bStarted = false;
  bool bClose = false;
  bool bRefresh = false;
  uint64_t generation = 0;
  std::string root;
  std::string compatibilityKey;
  std::vector<Entry> entries;  // Main-thread only.
  Status status;               // Main-thread only.
  CachedScan completed;        // Worker to main, under m.
  bool bCompleted = false;
  bool bDirty = false;
  uint64_t lastSaveMs = 0;
  size_t limit = 10000;
  size_t depth = 16;
};
Catalog g_catalog;
std::atomic<bool> g_saveBusy{false};

std::string Slashes(std::string path)
{
  std::replace(path.begin(), path.end(), '\\', '/');
#if !defined(_WIN32)
  if (path.size() > 2 && path[1] == ':' && path[2] == '/')
    path.erase(0, 2);  // The POSIX compatibility layer presents the native drive as Z:\.
#endif
  while (path.size() > 1 && path.back() == '/')
    path.pop_back();
  return path;
}

fs::path PathOf(const std::string &text)
{
  return fs::path(std::u8string(text.begin(), text.end()));
}

std::string PathText(const fs::path &path)
{
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

std::string CachePath(const std::string &root)
{
  return root + "/../../replay-index-v1.json";
}

uint64_t FileTicks(const fs::file_time_type &time)
{
  const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
  const int64_t nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(systemTime.time_since_epoch()).count();
  return static_cast<uint64_t>(nanoseconds / 100 + 116444736000000000LL);  // 100 ns FILETIME units, not whole seconds.
}

std::string Field(const unsigned char *p, size_t n)
{
  const char *text = reinterpret_cast<const char *>(p);
  return MapTextUtf8(std::string(text, strnlen(text, n)).c_str());
}

std::string Lower(std::string s)
{
  for (char &c : s)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  return s;
}

std::string GameType(int type)
{
  switch (type) {
    case 0x02: return "Melee";
    case 0x03: return "Free for All";
    case 0x04: return "One on One";
    case 0x09: return "Use Map Settings";
    case 0x0A: return "Top vs Bottom";
    case 0x0B: return "Team Melee";
    case 0x0C: return "Team Free for All";
    case 0x0D: return "Team Capture the Flag";
    default: return "Unknown";
  }
}

void ReadPlayers(Entry &entry)
{
  entry.players.clear();
  const unsigned char *slots = entry.info.abHead + 0xA1;
  const size_t past = entry.info.past.size() / 36;
  for (size_t i = 0; i < 12 + past; ++i) {
    const unsigned char *slot = i < 12 ? slots + i * 36 : entry.info.past.data() + (i - 12) * 36;
    if (slot[8] != 1 && slot[8] != 2)
      continue;
    Player player;
    player.nType = slot[8];
    player.nRace = slot[9] < 3 ? slot[9] : -1;
    player.nTeam = slot[10];
    player.name = Field(slot + 11, 25);
    uint32_t id = 0;
    memcpy(&id, slot, 4);
    player.nColour = id < 8 ? static_cast<int>(id) : -1;
    entry.players.push_back(player);
  }
}

void ReadEntry(Entry &entry)
{
  entry.error.clear();
  entry.bOk = ReplayHxPeek(entry.path.c_str(), &entry.info, nullptr);
  if (!entry.bOk) {
    entry.error = "Unreadable or truncated replay; other library entries are unaffected.";
    return;
  }
  const unsigned char *header = entry.info.abHead;
  entry.bExpansion = header[0] != 0;
  memcpy(&entry.dwFrames, header + 1, 4);
  entry.nSpeed = header[0x3A];
  memcpy(&entry.nMission, header + 5, 2);
  uint16_t width = 0, height = 0, tileset = 0;
  memcpy(&width, header + 0x34, 2);
  memcpy(&height, header + 0x36, 2);
  memcpy(&tileset, header + 0x44, 2);
  entry.nWidth = width;
  entry.nHeight = height;
  entry.nGameType = header[0x3C];
  entry.nTileset = tileset & 7;
  entry.map = Field(header + 0x61, 0x20);
  entry.type = entry.nMission ? "Campaign mission" : GameType(entry.nGameType);
  ReadPlayers(entry);
  entry.matchup = MatchupOf(entry);
}

PortJson String(const std::string &text)
{
  PortJson v;
  v.kind = PortJson::STRING;
  v.s = text;
  return v;
}

PortJson Number(uint64_t number)
{
  PortJson v;
  v.kind = PortJson::NUMBER;
  v.d = static_cast<double>(number);
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

std::string Fixed(const char *text, size_t size)
{
  return std::string(text, strnlen(text, size));
}

PortJson EntryJson(const Entry &entry)
{
  PortJson out = Object();
  Add(&out, "rel", String(entry.rel));
  Add(&out, "name", String(entry.name));
  Add(&out, "contentId", String(entry.contentId));
  Add(&out, "when", Number(entry.when));
  Add(&out, "changed", Number(entry.changed));
  Add(&out, "bytes", Number(entry.bytes));
  Add(&out, "ok", Boolean(entry.bOk));
  Add(&out, "error", String(entry.error));
  Add(&out, "head", String(PortBase64(entry.info.abHead, sizeof entry.info.abHead)));
  Add(&out, "past", String(PortBase64(entry.info.past.data(), entry.info.past.size())));
  Add(&out, "races", String(PortBase64(entry.info.races.data(), entry.info.races.size())));
  Add(&out, "hx", Boolean(entry.info.bHx));
  Add(&out, "rule", Boolean(entry.info.bRule));
  Add(&out, "ruleName", String(Fixed(entry.info.szRule, sizeof entry.info.szRule)));
  Add(&out, "build", String(Fixed(entry.info.szBuild, sizeof entry.info.szBuild)));
  Add(&out, "mod", String(Fixed(entry.info.szMod, sizeof entry.info.szMod)));
  Add(&out, "depends", String(Fixed(entry.info.szDepends, sizeof entry.info.szDepends)));
  Add(&out, "sim", Number(entry.info.dwSimRevision));
  Add(&out, "options", Number(entry.info.dwOptions));
  Add(&out, "checks", Number(entry.info.dwChecks));
  Add(&out, "refusal", String(entry.info.refusal));
  Add(&out, "map", String(entry.map));
  Add(&out, "type", String(entry.type));
  Add(&out, "matchup", String(entry.matchup));
  Add(&out, "expansion", Boolean(entry.bExpansion));
  Add(&out, "frames", Number(entry.dwFrames));
  Add(&out, "speed", Number(entry.nSpeed));
  Add(&out, "width", Number(entry.nWidth));
  Add(&out, "height", Number(entry.nHeight));
  Add(&out, "tileset", Number(entry.nTileset));
  Add(&out, "gameType", Number(entry.nGameType));
  Add(&out, "mission", Number(entry.nMission));
  PortJson players = Array();
  for (const Player &player : entry.players) {
    PortJson row = Object();
    Add(&row, "name", String(player.name));
    Add(&row, "race", Number(static_cast<uint64_t>(player.nRace + 1)));
    Add(&row, "type", Number(player.nType));
    Add(&row, "team", Number(player.nTeam));
    Add(&row, "colour", Number(static_cast<uint64_t>(player.nColour + 1)));
    players.items.push_back(row);
  }
  Add(&out, "players", players);
  return out;
}

bool Decode(const PortJson &row, const char *name, void *pOut, size_t bytes)
{
  const std::vector<unsigned char> decoded = PortBase64Bytes(row[name].Str());
  if (decoded.size() != bytes)
    return false;
  if (bytes)
    memcpy(pOut, decoded.data(), bytes);
  return true;
}

bool EntryRead(const PortJson &row, Entry *pOut)
{
  if (row.kind != PortJson::OBJECT || row["players"].kind != PortJson::ARRAY || row["players"].items.size() > 64)
    return false;
  pOut->path.clear();
  pOut->rel = row["rel"].Str();
  pOut->name = row["name"].Str();
  pOut->contentId = row["contentId"].Str();
  pOut->when = static_cast<uint64_t>(row["when"].Num());
  pOut->changed = static_cast<uint64_t>(row["changed"].Num());
  pOut->bytes = static_cast<uint64_t>(row["bytes"].Num());
  pOut->bOk = row["ok"].b;
  pOut->error = row["error"].Str();
  pOut->bPending = false;
  pOut->bDeferred = false;
  if (!Decode(row, "head", pOut->info.abHead, sizeof pOut->info.abHead))
    return false;
  const std::vector<unsigned char> past = PortBase64Bytes(row["past"].Str());
  const std::vector<unsigned char> races = PortBase64Bytes(row["races"].Str());
  if (past.size() % 36 || past.size() / 36 > 53 || races.size() > 65)
    return false;
  pOut->info.past = past;
  pOut->info.races = races;
  pOut->info.bHx = row["hx"].b;
  pOut->info.bRule = row["rule"].b;
  auto copy = [](const PortJson &object, const char *name, char *out, size_t size) {
    const std::string text = object[name].Str();
    memset(out, 0, size);
    memcpy(out, text.data(), std::min(size - 1, text.size()));
  };
  copy(row, "ruleName", pOut->info.szRule, sizeof pOut->info.szRule);
  copy(row, "build", pOut->info.szBuild, sizeof pOut->info.szBuild);
  copy(row, "mod", pOut->info.szMod, sizeof pOut->info.szMod);
  copy(row, "depends", pOut->info.szDepends, sizeof pOut->info.szDepends);
  pOut->info.dwSimRevision = static_cast<unsigned>(row["sim"].Num());
  pOut->info.dwOptions = static_cast<unsigned>(row["options"].Num());
  pOut->info.dwChecks = static_cast<unsigned>(row["checks"].Num());
  pOut->info.refusal = row["refusal"].Str();
  pOut->map = row["map"].Str();
  pOut->type = row["type"].Str();
  pOut->matchup = row["matchup"].Str();
  pOut->bExpansion = row["expansion"].b;
  pOut->dwFrames = static_cast<unsigned>(row["frames"].Num());
  pOut->nSpeed = static_cast<int>(row["speed"].Num());
  pOut->nWidth = static_cast<int>(row["width"].Num());
  pOut->nHeight = static_cast<int>(row["height"].Num());
  pOut->nTileset = static_cast<int>(row["tileset"].Num());
  pOut->nGameType = static_cast<int>(row["gameType"].Num());
  pOut->nMission = static_cast<unsigned>(row["mission"].Num());
  for (const PortJson &item : row["players"].items) {
    Player player;
    player.name = item["name"].Str();
    player.nRace = static_cast<int>(item["race"].Num()) - 1;
    player.nType = static_cast<int>(item["type"].Num());
    player.nTeam = static_cast<int>(item["team"].Num());
    player.nColour = static_cast<int>(item["colour"].Num()) - 1;
    pOut->players.push_back(player);
  }
  return true;
}

std::map<std::string, Entry> LoadCache(const std::string &root, const std::string &key)
{
  std::map<std::string, Entry> cached;
  std::string text, error;
  if (!ReplayLibrary::ReadBounded(CachePath(root), 64 * 1024 * 1024, &text, &error))
    return cached;
  PortJson rootJson;
  const bool bParsed = PortJsonParse(text.c_str(), &rootJson, &error);
  const bool bShape = bParsed && rootJson["format"].Str() == "hyperion/replay-index" &&
                      rootJson["version"].Num() == 1 && rootJson["entries"].kind == PortJson::ARRAY &&
                      rootJson["entries"].items.size() <= 100000;
  if (!bShape) {
    // Keep the damaged or unknown file for diagnosis under a unique name; notes,
    // outbox snapshots and downloads are separate files and are never removed.
    std::error_code ec;
    fs::rename(PathOf(CachePath(root)), PathOf(CachePath(root) + ".corrupt-" + ReplayKraken::UuidNew()), ec);
    return cached;
  }
  if (rootJson["compatibility"].Str() != key)
    return cached;  // A valid cache from other data/mod rules simply needs revalidation.
  for (const PortJson &row : rootJson["entries"].items) {
    Entry entry;
    if (EntryRead(row, &entry) && !entry.rel.empty() && !entry.contentId.empty())
      cached[entry.rel] = entry;
  }
  return cached;
}

void SaveCache(const std::string &root, const std::string &key, const std::vector<Entry> &entries)
{
  PortJson rootJson = Object();
  Add(&rootJson, "format", String("hyperion/replay-index"));
  Add(&rootJson, "version", Number(1));
  Add(&rootJson, "compatibility", String(key));
  PortJson rows = Array();
  for (const Entry &entry : entries)
    if (!entry.bPending && !entry.bDeferred)
      rows.items.push_back(EntryJson(entry));
  Add(&rootJson, "entries", rows);
  std::string error;
  ReplayLibrary::AtomicWrite(CachePath(root), PortJsonWrite(rootJson), &error);
}

bool Current(Catalog &catalog, uint64_t generation)
{
  std::lock_guard<std::mutex> lock(catalog.m);
  return !catalog.bClose && generation == catalog.generation;
}

CachedScan Scan(Catalog &catalog, uint64_t generation, const std::string &root, const std::string &key)
{
  CachedScan result;
  result.generation = generation;
  const auto cached = LoadCache(root, key);
  size_t limit = 10000, depth = 16;
  {
    std::lock_guard<std::mutex> lock(catalog.m);
    limit = catalog.limit;
    depth = catalog.depth;
  }
  struct Folder {
    fs::path path;
    std::string rel;
    size_t depth;
  };
  std::vector<Folder> folders{{PathOf(root), "", 0}};
  std::error_code ec;
  while (!folders.empty() && Current(catalog, generation)) {
    Folder folder = folders.back();
    folders.pop_back();
    if (folder.depth > depth)
      continue;
    fs::directory_iterator it(folder.path, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end && Current(catalog, generation); it.increment(ec)) {
      const auto &item = *it;
      const std::string name = PathText(item.path().filename());
      if (item.is_directory(ec)) {
        if (!item.is_symlink(ec))
          folders.push_back({item.path(), folder.rel + name + "/", folder.depth + 1});
        continue;
      }
      if (!item.is_regular_file(ec) || Lower(item.path().extension().string()) != ".rep")
        continue;
      if (result.entries.size() >= limit) {
        result.bLimited = true;
        continue;
      }
      Entry entry;
      entry.path = PathText(item.path());
      entry.rel = folder.rel + name;
      entry.name = PathText(item.path().stem());
      entry.bytes = item.file_size(ec);
      const auto modified = item.last_write_time(ec);
      entry.changed = ec ? 0 : FileTicks(modified);
      entry.when = entry.changed;
      const auto age = fs::file_time_type::clock::now() - modified;
      if (!ec && age < std::chrono::seconds(1)) {
        entry.bDeferred = true;
        entry.bPending = true;
        entry.error = "Still being written; a later refresh will inspect it.";
        result.entries.push_back(entry);
        continue;
      }
      const auto old = cached.find(entry.rel);
      if (old != cached.end() && old->second.bytes == entry.bytes && old->second.changed == entry.changed) {
        entry = old->second;
        entry.path = PathText(item.path());
        result.entries.push_back(entry);
        continue;
      }
      std::string bytes, error;
      if (!ReplayLibrary::ReadBounded(entry.path, 64 * 1024 * 1024, &bytes, &error)) {
        entry.bPending = false;
        entry.error = error.empty() ? "The replay could not be read." : error;
        result.entries.push_back(entry);
        continue;
      }
      const std::vector<unsigned char> raw(bytes.begin(), bytes.end());
      entry.contentId = ReplayKraken::ContentId(raw);
      if (entry.contentId.empty())
        entry.error = "The replay content identity could not be computed.";
      result.entries.push_back(entry);
    }
    ec.clear();
  }
  if (!Current(catalog, generation))
    return result;
  std::stable_sort(result.entries.begin(), result.entries.end(), [](const Entry &a, const Entry &b) {
    const bool aLast = Lower(a.rel) == "lastreplay.rep";
    const bool bLast = Lower(b.rel) == "lastreplay.rep";
    if (aLast != bLast)
      return aLast;
    return a.when != b.when ? a.when > b.when : a.rel < b.rel;
  });
  return result;
}

void Worker(Catalog &catalog)
{
  std::unique_lock<std::mutex> lock(catalog.m);
  for (;;) {
    catalog.cv.wait(lock, [&] { return catalog.bRefresh || catalog.bClose; });
    if (catalog.bClose && !catalog.bRefresh)
      continue;  // Process lifetime: ReplaysOpen may use the catalog again.
    catalog.bRefresh = false;
    const uint64_t generation = catalog.generation;
    const std::string root = catalog.root;
    const std::string key = catalog.compatibilityKey;
    lock.unlock();
    CachedScan result = Scan(catalog, generation, root, key);
    lock.lock();
    if (!catalog.bClose && generation == catalog.generation) {
      catalog.completed = std::move(result);
      catalog.bCompleted = true;
    }
  }
}

void RequestSave(Catalog &catalog)
{
  if (!catalog.bDirty || g_saveBusy.load())
    return;
  const uint64_t now = PortNowMs();
  if (catalog.lastSaveMs && now - catalog.lastSaveMs < 2000 && catalog.status.pending)
    return;
  catalog.lastSaveMs = now;
  catalog.bDirty = false;
  const std::string root = catalog.root;
  const std::string key = catalog.compatibilityKey;
  const std::vector<Entry> entries = catalog.entries;
#if !defined(__EMSCRIPTEN__)
  if (!g_saveBusy.exchange(true)) {
    std::thread([root, key, entries] {
      SaveCache(root, key, entries);
      g_saveBusy.store(false);
    }).detach();
  }
#endif
}

}  // namespace

std::string MatchupOf(const Entry &entry)
{
  const bool bTeams = entry.nGameType == 0x0A || (entry.nGameType >= 0x0B && entry.nGameType <= 0x0F);
  std::vector<int> teams, sizes;
  for (const Player &player : entry.players) {
    const int team = bTeams ? player.nTeam : static_cast<int>(teams.size()) + 1000;
    const auto at = std::find(teams.begin(), teams.end(), team);
    if (at == teams.end()) {
      teams.push_back(team);
      sizes.push_back(1);
    } else {
      ++sizes[at - teams.begin()];
    }
  }
  if (entry.players.empty())
    return "Unknown";
  if (entry.players.size() == 2 && teams.size() == 2) {
    std::string out;
    for (const Player &player : entry.players) {
      if (!out.empty())
        out += "v";
      out += player.nRace == 0 ? 'Z' : player.nRace == 1 ? 'T' : player.nRace == 2 ? 'P' : 'R';
    }
    return out;
  }
  if (teams.size() == entry.players.size())
    return std::to_string(entry.players.size()) + " players, free for all";
  std::string out;
  for (int size : sizes)
    out += (out.empty() ? "" : "v") + std::to_string(size);
  return out;
}

void Open(const std::string &root)
{
  double limit = 10000, depth = 16;
  PortSettingNumber("hyperion", "replayIndexLimit", &limit);
  PortSettingNumber("hyperion", "replayIndexDepth", &depth);
  const unsigned ruleSize = ReplayHxRuleSize();
  std::vector<unsigned char> rule(ruleSize);
  ReplayHxRuleMake(rule.data());
  rule.push_back(gbIsExpansion);
  double checkMod = 1.0;
  PortSettingNumber("hyperion", "checkModFingerprint", &checkMod);
  rule.push_back(checkMod == 0.0 ? 0 : 1);
  const std::string compatibilityKey = ReplayKraken::Sha256Hex(rule.data(), rule.size());
  {
    std::lock_guard<std::mutex> lock(g_catalog.m);
    g_catalog.bClose = false;
    g_catalog.root = Slashes(root);
    g_catalog.entries.clear();
    g_catalog.status = Status();
    g_catalog.limit = static_cast<size_t>(std::max(1000.0, std::min(limit, 100000.0)));
    g_catalog.depth = static_cast<size_t>(std::max(1.0, std::min(depth, 64.0)));
    g_catalog.status.limit = g_catalog.limit;
    g_catalog.status.depth = g_catalog.depth;
    g_catalog.compatibilityKey = compatibilityKey;
#if !defined(__EMSCRIPTEN__)
    if (!g_catalog.bStarted) {
      g_catalog.bStarted = true;
      std::thread([] { Worker(g_catalog); }).detach();
    }
#else
    g_catalog.status.note = "Incremental indexing runs in bounded main-thread slices in the browser build.";
#endif
  }
  Refresh();
}

void Close()
{
  std::lock_guard<std::mutex> lock(g_catalog.m);
  g_catalog.bClose = true;
  ++g_catalog.generation;
  g_catalog.entries.clear();
  g_catalog.cv.notify_all();
}

void Refresh()
{
  double limit = 10000, depth = 16;
  PortSettingNumber("hyperion", "replayIndexLimit", &limit);
  PortSettingNumber("hyperion", "replayIndexDepth", &depth);
  std::lock_guard<std::mutex> lock(g_catalog.m);
  g_catalog.limit = static_cast<size_t>(std::max(1000.0, std::min(limit, 100000.0)));
  g_catalog.depth = static_cast<size_t>(std::max(1.0, std::min(depth, 64.0)));
  g_catalog.status.limit = g_catalog.limit;
  g_catalog.status.depth = g_catalog.depth;
  ++g_catalog.generation;
  g_catalog.bRefresh = true;
  g_catalog.bClose = false;
  g_catalog.status.bScanning = true;
  g_catalog.status.generation = g_catalog.generation;
  g_catalog.cv.notify_all();
}

bool Pump(double engineBudgetMs)
{
  bool changed = false;
#if defined(__EMSCRIPTEN__)
  {
    std::lock_guard<std::mutex> lock(g_catalog.m);
    if (g_catalog.bRefresh) {
      const uint64_t generation = g_catalog.generation;
      const std::string root = g_catalog.root;
      const std::string key = g_catalog.compatibilityKey;
      g_catalog.bRefresh = false;
      lock.unlock();
      CachedScan result = Scan(g_catalog, generation, root, key);
      lock.lock();
      g_catalog.completed = std::move(result);
      g_catalog.bCompleted = true;
    }
  }
#endif
  {
    std::lock_guard<std::mutex> lock(g_catalog.m);
    if (g_catalog.bCompleted) {
      g_catalog.entries = g_catalog.completed.entries;
      g_catalog.bCompleted = false;
      g_catalog.status.bScanning = false;
      g_catalog.status.bLimited = g_catalog.completed.bLimited;
      g_catalog.status.discovered = g_catalog.entries.size();
      g_catalog.status.pending = 0;
      g_catalog.status.deferred = 0;
      g_catalog.status.unreadable = 0;
      g_catalog.status.generation = g_catalog.completed.generation;
      g_catalog.bDirty = true;
      changed = true;
    }
  }
  const double start = static_cast<double>(PortNowMs());
  size_t pending = 0, deferred = 0, unreadable = 0;
  for (Entry &entry : g_catalog.entries) {
    if (entry.bDeferred) {
      ++deferred;
      continue;
    }
    if (!entry.bPending) {
      unreadable += !entry.bOk;
      continue;
    }
    ++pending;
  }
  g_catalog.status.pending = pending;
  g_catalog.status.deferred = deferred;
  g_catalog.status.unreadable = unreadable;
  for (Entry &entry : g_catalog.entries) {
    if (!entry.bPending || entry.bDeferred)
      continue;
    const double before = static_cast<double>(PortNowMs());
    ReadEntry(entry);
    const double cost = static_cast<double>(PortNowMs()) - before;
    g_catalog.status.worstEnginePeekMs = std::max(g_catalog.status.worstEnginePeekMs, cost);
    entry.bPending = false;
    g_catalog.bDirty = true;
    changed = true;
    --g_catalog.status.pending;
    g_catalog.status.unreadable += !entry.bOk;
    const double batch = static_cast<double>(PortNowMs()) - start;
    if (batch >= engineBudgetMs)
      break;
  }
  g_catalog.status.lastEngineBatchMs = static_cast<double>(PortNowMs()) - start;
  if (deferred && !g_catalog.status.bScanning) {
    static uint64_t lastDeferred = 0;
    const uint64_t now = PortNowMs();
    if (now - lastDeferred > 1000) {
      lastDeferred = now;
      Refresh();
    }
  }
  RequestSave(g_catalog);
  return changed;
}

const std::vector<Entry> &Entries()
{
  return g_catalog.entries;
}

Status GetStatus()
{
  return g_catalog.status;
}

void Renamed(const std::string &oldPath, const std::string &newPath)
{
  for (Entry &entry : g_catalog.entries)
    if (entry.path == oldPath) {
      entry.path = newPath;
      entry.rel = newPath.substr(std::min(newPath.size(), g_catalog.root.size() + 1));
      entry.name = PathText(PathOf(newPath).stem());
      g_catalog.bDirty = true;
      break;
    }
  Refresh();
}

void Deleted(const std::string &path)
{
  g_catalog.entries.erase(std::remove_if(g_catalog.entries.begin(), g_catalog.entries.end(), [&](const Entry &entry) {
    return entry.path == path;
  }), g_catalog.entries.end());
  g_catalog.bDirty = true;
  RequestSave(g_catalog);
}

bool ReadUploadMetadata(const Entry &entry, UploadMetadata *pOut)
{
  if (!entry.bOk)
    return false;
  pOut->gameVersion = entry.info.bHx ? Fixed(entry.info.szBuild, sizeof entry.info.szBuild) : "";
  pOut->map = entry.map;
  pOut->gameType = entry.type;
  pOut->durationFrames = entry.dwFrames;
  pOut->matchup = entry.matchup;
  size_t active = 0;
  const unsigned char *slots = entry.info.abHead + 0xA1;
  const size_t past = entry.info.past.size() / 36;
  for (size_t i = 0; i < 12 + past && active < entry.players.size(); ++i) {
    const unsigned char *slot = i < 12 ? slots + i * 36 : entry.info.past.data() + (i - 12) * 36;
    if (slot[8] != 1 && slot[8] != 2)
      continue;
    const Player &source = entry.players[active++];
    UploadPlayer player;
    player.name = source.name;
    player.race = source.nRace == 0 ? "Zerg" : source.nRace == 1 ? "Terran" : source.nRace == 2 ? "Protoss" : "unknown";
    player.slot = static_cast<int64_t>(i);  // The actual header slot, never a team/force.
    player.type = source.nType == 1 ? "computer" : source.nType == 2 ? "human" : "";
    pOut->players.push_back(player);
  }
  return active == entry.players.size();
}

std::vector<Entry> Synthetic(size_t count)
{
  count = std::min<size_t>(count, 100000);
  std::vector<Entry> entries(count);
  for (size_t i = 0; i < count; ++i) {
    Entry &entry = entries[i];
    entry.path = "synthetic/" + std::to_string(i) + ".rep";
    entry.rel = entry.path;
    entry.name = "Synthetic replay " + std::to_string(i);
    entry.contentId = "sha256:synthetic" + std::to_string(i);
    entry.when = 130000000000000000ULL + i * 10000000ULL;
    entry.bytes = 1000 + i;
    entry.bPending = false;
    entry.bOk = true;
    entry.map = i % 3 == 0 ? "Synthetic long map name " + std::to_string(i) : "Fixture Basin";
    entry.type = "Melee";
    entry.matchup = i % 2 ? "TvZ" : "PvP";
    entry.dwFrames = 24000 + static_cast<unsigned>(i);
    entry.nSpeed = 6;
    entry.nWidth = entry.nHeight = 128;
    entry.info.bHx = i % 2 == 0;
    entry.players = {{"fixture α", 0, 2, 0, 0}, {"fixture β", 1, 2, 1, 1}};
  }
  return entries;
}

}  // namespace ReplayCatalog
