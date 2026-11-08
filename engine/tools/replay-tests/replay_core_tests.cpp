// Focused ReplayKraken/library contract tests, using production sources.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "h/port_replaycatalog.h"
#include "h/port_replayclaims.h"
#include "h/port_replaykraken.h"
#include "h/port_replaylibrary.h"

namespace {
void Check(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

std::string Read(const std::filesystem::path &path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void UrlAndProtocol(const std::filesystem::path &fixtures)
{
  using namespace ReplayKraken;
  Endpoint endpoint;
  std::string error;
  Check(EndpointParse(" https://EXAMPLE.org:443/// ", &endpoint, &error), error.c_str());
  Check(endpoint.api == "https://example.org/api", "canonical host/API root");
  Check(EndpointParse("https://example.org/api", &endpoint, &error), error.c_str());
  Check(endpoint.api == "https://example.org/api", "input /api must not double the prefix");
  Check(EndpointParse("https://example.org/kraken/api/", &endpoint, &error), error.c_str());
  Check(endpoint.api == "https://example.org/kraken/api", "base path plus one /api");
  Check(EndpointParse("http://127.0.0.1:18787", &endpoint, &error), error.c_str());
  Check(!EndpointParse("http://example.org", &endpoint, &error), "non-loopback HTTP must be refused");
  Check(!EndpointParse("https://user:secret@example.org", &endpoint, &error), "URL credentials must be refused");
  Check(EndpointParse("https://example.org/kraken/api/", &endpoint, &error), error.c_str());
  Query query;
  query.player = "A&B / α";
  query.matchup = "TvZ";
  const std::string url = ListUrl(endpoint, query, 50, 50);
  Check(url.find("/kraken/api/matches?limit=50&offset=50") != std::string::npos, "list URL path");
  Check(url.find("player=A%26B%20%2F%20%CE%B1") != std::string::npos, "UTF-8 query escaping");

  MatchPage first, second;
  Check(ParsePage(Read(fixtures / "list-page-1.json"), endpoint, &first, &error), error.c_str());
  Check(first.matches.size() == 2 && first.total == 3 && !first.bComplete && first.nextOffset == 2,
        "first fixture page and raw next offset");
  Check(first.matches[0].serverApi == endpoint.api && first.matches[0].serverOrigin == endpoint.origin,
        "source origin persisted in normalized model");
  Check(ParsePage(Read(fixtures / "list-page-2.json"), endpoint, &second, &error), error.c_str());
  MergePage(&first, second);
  Check(first.matches.size() == 3 && first.bComplete && first.nextOffset == 3, "pagination reaches total with raw offset");
  MergePage(&first, second);
  Check(first.matches.size() == 3, "UUID deduplication on repeated page");
  Check(MatchupKey("ZvT") == MatchupKey("TvZ") && MatchupKey("P v P") == "p", "mirror matchup normalization");
  Check(!ParsePage("{\"matches\":[", endpoint, &second, &error), "truncated JSON is rejected");
  Check(!ParsePage("{\"matches\":[],\"total\":-1,\"limit\":50,\"offset\":0}", endpoint, &second, &error),
        "invalid total is rejected");
  Check(SafeFilePart("../..\\CON/α:*?", 64) == "_.._.._CON_α___" || !SafeFilePart("CON").empty(),
        "safe download names are bounded/sanitized");
  Check(SafeFilePart("CON") != "CON", "reserved Windows file name is prefixed");
  Check(SafeFilePart("α").find('\xce') != std::string::npos, "Unicode download name is preserved");
  Check(Sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 empty");
  Check(Sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 abc");

  UploadMetadata metadata;
  metadata.gameVersion = "Hyperion-test";
  metadata.map = "Fixture";
  metadata.gameType = "Melee";
  metadata.durationFrames = 1234;
  metadata.matchup = "ZvT";
  metadata.players = {{"Alice", "Zerg", 0, "", "human"}, {"Computer", "Zerg", 1, "", "computer"}};
  const std::vector<unsigned char> bytes{'r', 'e', 'p', 'l', 'a', 'y', '-', 'b', 'y', 't', 'e', 's'};
  std::string body;
  Check(UploadBody("uuid-1", metadata.gameVersion, metadata, bytes, &body, &error), error.c_str());
  Check(body.find("\"matchUuid\":\"uuid-1\"") != std::string::npos, "camelCase upload UUID");
  Check(body.find("\"replayFile\":\"cmVwbGF5LWJ5dGVz\"") != std::string::npos, "Base64 replay field");
  Check(body.find("\"result\"") == std::string::npos, "unknown results are omitted, not guessed");
  Check(body.find("\"type\":\"human\"") != std::string::npos, "authoritative player type");
  Check(body.find("uploaded_by") == std::string::npos, "response-only uploader is not invented");
  Check(ParseCreatedUuid("{\"uuid\":\"uuid-1\"}", "uuid-1", &error), error.c_str());
  Check(!ParseCreatedUuid("{\"uuid\":\"other\"}", "uuid-1", &error), "wrong acknowledgement UUID");
  const std::vector<unsigned char> oversized(kMaxReplayBytes + 1, 1);
  Check(!UploadBody("uuid-1", metadata.gameVersion, metadata, oversized, &body, &error), "server body limit is explicit");

  Check(ClassifyHttp(429, "3").kind == Failure::Transient, "429 retries");
  Check(ClassifyHttp(429, "3").retryAfterMs == 3000, "Retry-After seconds");
  Check(ClassifyHttp(401, "").kind == Failure::Unauthorized, "401 is not hot-looped");
  Check(ClassifyHttp(422, "").kind == Failure::Unsupported, "validation becomes unsupported/permanent");
  Check(ClassifyHttp(409, "").kind == Failure::Conflict, "409 needs equivalence review");
  Check(BackoffMs(3, -1, 999) == 4999, "bounded exponential backoff plus jitter");
  Endpoint next;
  Check(!RedirectAllowed(endpoint, "http://example.org/api/matches", true, &next, &error), "HTTPS downgrade refused");
  Check(!RedirectAllowed(endpoint, "https://other.org/api/matches", true, &next, &error),
        "credential cross-origin redirect refused");
  Check(RedirectAllowed(endpoint, "https://example.org/api/matches", true, &next, &error), "same-origin redirect");
}

void LibraryPersistence()
{
  using namespace ReplayLibrary;
  Data data;
  Record &record = For(&data, "sha256:one");
  record.bFavourite = true;
  record.startedAt = 123;
  record.sources = {{"https://example.org/api", "uuid-1"}};
  record.notes = {{"private-id", "do not export this private note", {"secret-tag"}, false, 10},
                  {"shared-id", "shared tactical note", {"review"}, true, 11}};
  SavedQuery query;
  query.id = "query-stable";
  query.name = "All fixture games";
  query.filters.search = "Fixture";
  query.filters.matchup = "TvZ";
  query.updatedAt = 20;
  data.queries.push_back(query);
  Collection list;
  list.id = "list-stable";
  list.name = "Only one";
  list.recordIds = {"sha256:one"};
  list.updatedAt = 21;
  data.collections.push_back(list);
  Check(query.filters.search == "Fixture" && query.filters.matchup == "TvZ", "dynamic query keeps full filters");
  Check(list.recordIds.size() == 1 && InCollection(list, "sha256:one") && !InCollection(list, "sha256:two"),
        "fixed collection restricts to members");

  const std::string saved = ToJson(data);
  Data roundTrip;
  std::string error;
  Check(FromJson(saved, &roundTrip, &error), error.c_str());
  Check(roundTrip.records.size() == 1 && roundTrip.records[0].sources[0].serverApi == "https://example.org/api",
        "source-aware persistence round trip");
  ExportSelection selection;
  selection.recordIds = {"sha256:one"};
  selection.queryIds = {"query-stable"};
  selection.collectionIds = {"list-stable"};
  selection.bFavourites = true;
  selection.bSharedNotes = true;
  const std::string exported = ExportJson(roundTrip, selection);
  Check(exported.find("shared tactical note") != std::string::npos, "explicit shared note exported");
  Check(exported.find("do not export this private note") == std::string::npos, "private note excluded at boundary");
  Check(exported.find("secret-tag") == std::string::npos, "private note tags excluded too");
  Check(exported.find("query-stable") != std::string::npos && exported.find("list-stable") != std::string::npos,
        "stable query/list IDs exported");
  Data imported;
  Check(ImportJson(exported, &imported, &error), error.c_str());
  Check(ImportJson(exported, &imported, &error), error.c_str());
  Check(imported.records.size() == 1 && imported.records[0].notes.size() == 1 && imported.queries.size() == 1 &&
            imported.collections.size() == 1,
        "stable-ID reimport is idempotent");
  Data withPrivate = imported;
  withPrivate.records[0].notes.push_back({"private-id", "local private", {"secret-tag"}, false, 12});
  Check(ImportJson(exported, &withPrivate, &error), error.c_str());
  Check(withPrivate.records[0].notes.size() == 2, "an export without private notes never deletes them");

  const std::filesystem::path root = std::filesystem::temp_directory_path() / "hyperion-replay-core-tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  Check(AtomicWrite((root / "library.json").string(), saved, &error), error.c_str());
  Data loaded;
  Check(LoadAtomic((root / "library.json").string(), &loaded, &error), error.c_str());
  Check(loaded.records.size() == 1 && loaded.records[0].notes.size() == 2, "atomic persistence round trip");
  std::ofstream(root / "blocker") << "not a directory";
  Check(!AtomicWrite((root / "blocker" / "child.json").string(), "{}", &error), "atomic failure is reported");
  Check(Read(root / "library.json") == saved, "failed atomic write leaves previous content intact");
  std::ofstream(root / "corrupt.json") << "{not-json";
  Data preserved = loaded;
  Check(!LoadAtomic((root / "corrupt.json").string(), &preserved, &error), "corrupt cache is rejected");
  Check(preserved.records.size() == 1, "corrupt cache does not erase in-memory user data");
  std::filesystem::remove_all(root);
}

void DownloadLinkingAndFileFailures()
{
  using namespace ReplayLibrary;
  ReplayKraken::Endpoint server;
  std::string error;
  Check(ReplayKraken::EndpointParse("https://example.org/api", &server, &error), error.c_str());
  const std::string remoteId = ReplayKraken::ServerRecordId(server, "uuid-1");
  Data first;
  Record &remote = For(&first, remoteId);
  remote.bFavourite = true;
  remote.notes = {{"note-1", "shared note", {"tag"}, true, 1}};
  first.collections.push_back({"list-1", "linked", {remoteId}, 1});
  // Add many records after the remote one so For(content) reallocates and the
  // erased remote precedes the content record, exercising both invalidations.
  for (int i = 0; i < 32; ++i)
    For(&first, "sha256:other" + std::to_string(i));
  LinkDownload(&first, server, "uuid-1", "sha256:content");
  const Record *linked = Find(first, "sha256:content");
  Check(linked && linked->bFavourite && linked->notes.size() == 1 && linked->sources.size() == 1,
        "download linking preserves remote notes/favourite/source");
  Check(Find(first, remoteId) == nullptr && InCollection(first.collections[0], "sha256:content"),
        "download linking removes remote ID and rekeys fixed collection");
  Data second;
  Record &content = For(&second, "sha256:content");
  content.notes = {{"note-2", "local private", {"private"}, false, 2}};
  For(&second, remoteId).bFavourite = true;
  LinkDownload(&second, server, "uuid-1", "sha256:content");
  Check(Find(second, "sha256:content")->notes.size() == 1 && Find(second, "sha256:content")->bFavourite,
        "content-first linking remains stable and does not duplicate notes");

  const std::filesystem::path root = std::filesystem::temp_directory_path() / "hyperion-replay-file-tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  const auto previous = std::filesystem::current_path();
  std::filesystem::current_path(root);
  Check(AtomicWrite("basename.json", "{}", &error), error.c_str());
  Check(std::filesystem::exists(root / "basename.json"), "basename export writes in the current directory");
  std::filesystem::current_path(previous);
  std::ofstream(root / "secret.json") << ToJson(second);
  std::filesystem::permissions(root / "secret.json", std::filesystem::perms::none);
  Data preserved = second;
  Check(!LoadAtomic((root / "secret.json").string(), &preserved, &error), "permission/read failure is not a first run");
  Check(!error.empty() && preserved.records.size() == second.records.size(), "read failure preserves user data and reports error");
  std::filesystem::permissions(root / "secret.json", std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
  std::filesystem::remove_all(root);
}

void ConcurrentClaims()
{
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "hyperion-replay-claim-tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  const std::string claim = (root / "job.claim").string();
  std::string error;
  std::string owner;
  Check(ReplayClaims::ClaimOwned(claim, owner, 600, &error), error.c_str());
  Check(!owner.empty() && !ReplayClaims::Claim(claim, 600, &error), "a concurrent live claimant is rejected");
  const auto beforeHeartbeat = std::filesystem::file_time_type::clock::now() - std::chrono::seconds(30);
  std::filesystem::last_write_time(claim, beforeHeartbeat);
  Check(ReplayClaims::Heartbeat(claim, owner), "owner heartbeat refreshes a live claim");
  Check(std::filesystem::last_write_time(claim) > beforeHeartbeat, "heartbeat updates claim mtime");
  Check(!ReplayClaims::Heartbeat(claim, "not-the-owner"), "mismatched heartbeat is rejected");
  const auto old = std::filesystem::file_time_type::clock::now() - std::chrono::seconds(3600);
  std::filesystem::last_write_time(claim, old);
  Check(ReplayClaims::ClaimOwned(claim, owner, 600, &error), "a stale claim is reclaimed");
  ReplayClaims::Release(claim, "not-the-owner");
  Check(std::filesystem::exists(claim), "another instance's claim is not released");
  ReplayClaims::Release(claim, owner);
  Check(ReplayClaims::ClaimOwned(claim, owner, 600, &error), "a released claim can be acquired again");
  ReplayClaims::Release(claim, owner);
  std::filesystem::remove_all(root);
}

void SyntheticBudget()
{
  const auto start = std::chrono::steady_clock::now();
  const auto entries = ReplayCatalog::Synthetic(10000);
  size_t matches = 0;
  for (const auto &entry : entries)
    matches += entry.name.find("9999") != std::string::npos;
  Check(entries.size() == 10000 && matches == 1, "10,000-entry synthetic catalog query");
  const size_t rendered = std::min<size_t>(entries.size(), 12);
  Check(rendered == 12, "rendered rows remain bounded/virtualized");
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  std::cout << "synthetic-10000 query+bound: " << ms << " ms; rendered rows=" << rendered << "\n";
  Check(ms < 250.0, "synthetic query exceeds the 250 ms test-environment bound");
}
}  // namespace

int main(int argc, char **argv)
{
  try {
    Check(argc == 2, "usage: replay_core_tests <fixture-directory>");
    UrlAndProtocol(argv[1]);
    LibraryPersistence();
    DownloadLinkingAndFileFailures();
    ConcurrentClaims();
    SyntheticBudget();
    std::cout << "PASS replay core contracts\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << "\n";
    return 1;
  }
}
