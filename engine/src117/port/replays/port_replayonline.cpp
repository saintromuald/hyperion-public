// port_replayonline.cpp -- one application-owned worker for Replay Kraken
// archive traffic, validated downloads, immutable upload snapshots and retries.
// The only engine-state access is the explicitly main-thread capture at the end.
#include "../../h/port_replayonline.h"
#include "../../h/port_replayclaims.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#include "../../h/maplimits.h"
#include "../../h/gds_appaddex.h"
#include "../../h/net_cmds.h"
#include "../../h/playerlimits.h"
#include "../../h/replay.h"
#include "../../hyperion/version.h"
#include "../../gen/globals117.h"
#include "../data/port_stormapi.h"
#include "../gameplay/port_watchgame.h"
#include "../../h/port_http.h"
#include "../platform/port_json.h"
#include "../platform/port_log.h"
#include "../platform/port_paths.h"
#include "../platform/port_globals117.h"
#include "../platform/port_settings.h"
#include "../platform/port_util.h"
#include "../render/port_maptext.h"
#include "../tools/port_bench.h"
#include "oc_data.h"

#if defined(_WIN32)
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ReplayOnline {
namespace {

namespace fs = std::filesystem;
using ReplayKraken::Endpoint;
using ReplayKraken::Failure;
using ReplayKraken::Match;
using ReplayKraken::MatchPage;
using ReplayKraken::Query;
using ReplayKraken::UploadMetadata;
using ReplayKraken::UploadPlayer;
using ReplayLibrary::AtomicWrite;
using ReplayLibrary::ReadBounded;

constexpr size_t kMaxJobs = 64;
constexpr size_t kMaxOutboxBytes = 256 * 1024 * 1024;
constexpr unsigned kMaxAttempts = 8;
constexpr int64_t kClaimStaleSeconds = 600;

struct Job {
  std::string uuid;
  std::string contentId;
  std::string sourcePath;
  std::string serverApi;
  std::string account;
  std::string tokenHash;
  UploadMetadata metadata;
  bool bAutomatic = false;
  bool bManualOverride = false;
  UploadPhase phase = UploadPhase::Queued;
  std::string error;
  unsigned attempts = 0;
  int64_t createdAt = 0;
  int64_t nextRetryAt = 0;
  size_t replayBytes = 0;
};

struct DownloadRecord {
  std::string serverApi;
  std::string uuid;
  std::string path;
  std::string contentId;
};

struct Service {
  mutable std::mutex m;
  std::condition_variable cv;
  std::deque<std::function<void()>> tasks;
  bool bStop = false;
  bool bWorkerDone = false;
  State state;
  ReplayLibrary::Data library;
  std::string root;
  std::string replayRoot;
  std::string caFile;
  Endpoint endpoint;
  bool bEndpointValid = false;
  std::atomic<bool> bAutomatic{false};
  std::atomic<bool> bCancelAll{false};
  std::shared_ptr<std::atomic<bool>> activeCancel;
  bool bActiveAutomatic = false;
  uint64_t generation = 0;
  std::vector<Job> jobs;
  std::vector<DownloadRecord> downloaded;
  MatchPage cache;
  int64_t cacheAt = 0;
};

Service *g_service = nullptr;
std::mutex g_start;
std::atomic<uint64_t> g_validationRequests{0};
PortLogger g_log("hyperion_replay_kraken.log");

std::string Slashes(std::string path)
{
  std::replace(path.begin(), path.end(), '\\', '/');
#if !defined(_WIN32)
  if (path.size() > 2 && path[1] == ':' && path[2] == '/')
    path.erase(0, 2);
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

std::string Join(const std::string &a, const std::string &b)
{
  return a + "/" + b;
}

std::string TokenPath(Service &s, const std::string &api)
{
  return Join(s.root, "token-" + ReplayKraken::Sha256Hex(api.data(), api.size()) + ".secret");
}

std::string CachePath(Service &s, const std::string &api)
{
  return Join(s.root, "cache-" + ReplayKraken::Sha256Hex(api.data(), api.size()) + ".json");
}

std::string JobDir(Service &s, const std::string &uuid)
{
  return Join(Join(s.root, "jobs"), uuid);
}

void SetError(Service &s, const std::string &error)
{
  std::lock_guard<std::mutex> lock(s.m);
  s.state.lastError = error;
  if (!error.empty())
    g_log("%s", error.c_str());
}

void FailPreparing(Service &s, const std::string &uuid, const std::string &error,
                   UploadPhase phase = UploadPhase::Failed)
{
  SetError(s, error);
  std::lock_guard<std::mutex> lock(s.m);
  for (UploadView &view : s.state.uploads)
    if (view.uuid == uuid) {
      view.phase = phase;
      view.error = error;
      view.bCommitted = false;
    }
}

void Post(Service &s, std::function<void()> task)
{
#if defined(__EMSCRIPTEN__)
  task();
  std::lock_guard<std::mutex> lock(s.m);
  s.state.bWorkerBusy = false;
  return;
#endif
  {
    std::lock_guard<std::mutex> lock(s.m);
    if (s.bStop)
      return;
    const bool bBusy = !s.tasks.empty() || s.state.bWorkerBusy;
    s.tasks.push_back(std::move(task));
    s.state.bWorkerBusy = true;
    (void)bBusy;
  }
  s.cv.notify_one();
}

PortJson String(const std::string &text)
{
  PortJson v;
  v.kind = PortJson::STRING;
  v.s = text;
  return v;
}

PortJson Number(int64_t number)
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

std::string Text(const PortJson &object, const char *name)
{
  return object[name].Str();
}

int64_t Integer(const PortJson &object, const char *name)
{
  const double value = object[name].Num();
  return value > 0 && value < 9007199254740991.0 ? static_cast<int64_t>(value) : 0;
}

bool ParseFailure(std::string *pError, const std::string &text)
{
  if (pError)
    *pError = text;
  return false;
}

PortJson MetadataJson(const UploadMetadata &meta)
{
  PortJson out = Object();
  Add(&out, "gameVersion", String(meta.gameVersion));
  Add(&out, "map", String(meta.map));
  Add(&out, "gameType", String(meta.gameType));
  Add(&out, "durationFrames", Number(meta.durationFrames));
  Add(&out, "matchup", String(meta.matchup));
  PortJson players = Array();
  for (const UploadPlayer &player : meta.players) {
    PortJson row = Object();
    Add(&row, "name", String(player.name));
    Add(&row, "race", String(player.race));
    Add(&row, "slot", Number(player.slot));
    Add(&row, "result", String(player.result));
    Add(&row, "type", String(player.type));
    players.items.push_back(row);
  }
  Add(&out, "players", players);
  return out;
}

bool MetadataRead(const PortJson &row, UploadMetadata *pOut, std::string *pError)
{
  if (row.kind != PortJson::OBJECT || row["players"].kind != PortJson::ARRAY || row["players"].items.size() > 64)
    return ParseFailure(pError, "Invalid immutable upload metadata.");
  pOut->gameVersion = Text(row, "gameVersion");
  pOut->map = Text(row, "map");
  pOut->gameType = Text(row, "gameType");
  pOut->durationFrames = Integer(row, "durationFrames");
  pOut->matchup = Text(row, "matchup");
  for (const PortJson &item : row["players"].items) {
    if (item.kind != PortJson::OBJECT)
      return ParseFailure(pError, "Invalid immutable upload player.");
    UploadPlayer player;
    player.name = Text(item, "name");
    player.race = Text(item, "race");
    player.slot = Integer(item, "slot");
    player.result = Text(item, "result");
    player.type = Text(item, "type");
    pOut->players.push_back(player);
  }
  return true;
}

PortJson JobJson(const Job &job)
{
  PortJson out = Object();
  Add(&out, "format", String("hyperion/replay-kraken-outbox"));
  Add(&out, "version", Number(1));
  Add(&out, "uuid", String(job.uuid));
  Add(&out, "contentId", String(job.contentId));
  Add(&out, "sourcePath", String(job.sourcePath));
  Add(&out, "serverApi", String(job.serverApi));
  Add(&out, "account", String(job.account));
  Add(&out, "tokenHash", String(job.tokenHash));
  Add(&out, "automatic", Boolean(job.bAutomatic));
  Add(&out, "createdAt", Number(job.createdAt));
  Add(&out, "replayBytes", Number(static_cast<int64_t>(job.replayBytes)));
  Add(&out, "metadata", MetadataJson(job.metadata));
  return out;
}

PortJson JobStateJson(const Job &job)
{
  PortJson out = Object();
  Add(&out, "phase", Number(static_cast<int>(job.phase)));
  Add(&out, "error", String(job.error));
  Add(&out, "attempts", Number(job.attempts));
  Add(&out, "nextRetryAt", Number(job.nextRetryAt));
  Add(&out, "account", String(job.account));
  Add(&out, "tokenHash", String(job.tokenHash));
  Add(&out, "manualOverride", Boolean(job.bManualOverride));
  return out;
}

bool ParseJson(const std::string &text, PortJson *pOut, std::string *pError)
{
  return PortJsonParse(text.c_str(), pOut, pError);
}

bool SaveJobState(Service &s, const Job &job)
{
  std::string error;
  const bool bOk = AtomicWrite(Join(JobDir(s, job.uuid), "state.json"), PortJsonWrite(JobStateJson(job)), &error);
  if (!bOk)
    SetError(s, error + " The job remains committed; retry after storage recovers.");
  return bOk;
}

void PublishJobs(Service &s)
{
  std::lock_guard<std::mutex> lock(s.m);
  s.state.uploads.clear();
  for (const Job &job : s.jobs) {
    UploadView view;
    view.uuid = job.uuid;
    view.contentId = job.contentId;
    view.sourcePath = job.sourcePath;
    view.serverApi = job.serverApi;
    view.account = job.account;
    view.phase = job.phase;
    if (job.bAutomatic && !job.bManualOverride && !s.bAutomatic.load() &&
        (job.phase == UploadPhase::Queued || job.phase == UploadPhase::RetryWaiting))
      view.phase = UploadPhase::Disabled;
    view.error = job.error;
    view.attempts = job.attempts;
    view.bAutomatic = job.bAutomatic;
    view.bCommitted = true;
    view.nextRetryAt = job.nextRetryAt;
    s.state.uploads.push_back(view);
  }
}

Job *FindJob(Service &s, const std::string &uuid)
{
  const auto at = std::find_if(s.jobs.begin(), s.jobs.end(), [&](const Job &job) { return job.uuid == uuid; });
  return at == s.jobs.end() ? nullptr : &*at;
}

bool ReadBytes(const std::string &path, size_t maxBytes, std::vector<unsigned char> *pOut, std::string *pError)
{
  std::string text;
  if (!ReadBounded(path, maxBytes, &text, pError)) {
    if (pError && pError->empty())
      *pError = "The replay file could not be read.";
    return false;
  }
  pOut->assign(text.begin(), text.end());
  return true;
}

bool WriteBytes(const std::string &path, const std::vector<unsigned char> &bytes, std::string *pError)
{
  return AtomicWrite(path, std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size()), pError);
}

std::string ReadToken(Service &s, const std::string &api)
{
  std::string token, error;
  if (!ReadBounded(TokenPath(s, api), 4096, &token, &error))
    return "";
  const size_t first = token.find_first_not_of(" \t\r\n");
  const size_t last = token.find_last_not_of(" \t\r\n");
  return first == std::string::npos ? "" : token.substr(first, last - first + 1);
}

void RefreshTokenState(Service &s, const Endpoint &endpoint)
{
  const std::string token = ReadToken(s, endpoint.api);
  std::lock_guard<std::mutex> lock(s.m);
  if (s.state.serverApi == endpoint.api)
    s.state.bTokenPresent = !token.empty();
}

bool Claim(Service &s, const Job &job, std::string &owner)
{
  return ReplayClaims::ClaimOwned(Join(JobDir(s, job.uuid), "claim"), owner, kClaimStaleSeconds);
}

void Unclaim(Service &s, const Job &job, const std::string &owner)
{
  ReplayClaims::Release(Join(JobDir(s, job.uuid), "claim"), owner);
}

PortHttp::Request RequestFor(Service &s, const std::string &url, const std::shared_ptr<std::atomic<bool>> &cancel)
{
  PortHttp::Request request;
  request.url = url;
  request.caFile = s.caFile;
  request.pCancel = cancel.get();
  request.maxBytes = ReplayKraken::kMaxResponseBytes;
  return request;
}

std::shared_ptr<std::atomic<bool>> BeginNetwork(Service &s, bool bAutomatic)
{
  auto cancel = std::make_shared<std::atomic<bool>>(false);
  {
    std::lock_guard<std::mutex> lock(s.m);
    s.activeCancel = cancel;
    s.bActiveAutomatic = bAutomatic;
  }
  if (s.bCancelAll.load() || (bAutomatic && !s.bAutomatic.load()))
    cancel->store(true);
  return cancel;
}

void EndNetwork(Service &s)
{
  std::lock_guard<std::mutex> lock(s.m);
  s.activeCancel.reset();
  s.bActiveAutomatic = false;
}

bool AccountFor(Service &s, Job &job, const std::string &token, const std::shared_ptr<std::atomic<bool>> &cancel,
                std::string *pError, Failure *pFailure)
{
  *pFailure = Failure::None;
  if (!job.account.empty())
    return true;
  PortHttp::Request request = RequestFor(s, job.serverApi + "/users/@me", cancel);
  request.bearer = token;
  request.maxBytes = 64 * 1024;
  const PortHttp::Response response = PortHttp::Send(request);
  if (response.status == 401 || response.status == 403) {
    *pFailure = Failure::Unauthorized;
    *pError = "Replay Kraken rejected the token. Replace it in Settings, then submit as a new upload.";
    return false;
  }
  if (!response.error.empty() || response.status != 200) {
    *pFailure = response.error.empty() ? ReplayKraken::ClassifyHttp(response.status, response.Header("retry-after")).kind
                                       : Failure::Transient;
    *pError = response.error.empty() ? "Identity validation failed with HTTP " + std::to_string(response.status) :
                                      response.error;
    return false;
  }
  PortJson root;
  std::string parseError;
  if (!ParseJson(response.Text(), &root, &parseError)) {
    *pFailure = Failure::Permanent;
    *pError = "Identity validation returned malformed JSON: " + parseError;
    return false;
  }
  job.account = Text(root, "username");
  if (job.account.empty() || job.account.size() > 256) {
    *pFailure = Failure::Permanent;
    *pError = "Identity validation returned no usable account name.";
    return false;
  }
  return true;
}

void RetryLater(Service &s, Job &job, const std::string &error, int64_t retryAfter)
{
  job.error = error;
  if (job.attempts >= kMaxAttempts) {
    job.phase = UploadPhase::Failed;
    job.nextRetryAt = 0;
    job.error = error + " Retry limit reached; use Retry to start a fresh attempt sequence.";
  } else {
    const uint32_t jitter = static_cast<uint32_t>(ReplayKraken::Sha256Hex(job.uuid.data(), job.uuid.size())[0]) * 37;
    job.nextRetryAt = ReplayLibrary::NowSeconds() * 1000 + ReplayKraken::BackoffMs(job.attempts, retryAfter, jitter);
    job.phase = UploadPhase::RetryWaiting;
  }
  SaveJobState(s, job);
  PublishJobs(s);
}

bool SameMetadata(const Job &job, const Match &remote)
{
  if (remote.gameVersion != job.metadata.gameVersion || remote.map != job.metadata.map ||
      remote.gameType != job.metadata.gameType || remote.durationFrames != job.metadata.durationFrames ||
      ReplayKraken::MatchupKey(remote.matchup) != ReplayKraken::MatchupKey(job.metadata.matchup) ||
      remote.players.size() != job.metadata.players.size())
    return false;
  for (size_t i = 0; i < remote.players.size(); ++i) {
    const auto &a = remote.players[i];
    const auto &b = job.metadata.players[i];
    const std::string remoteResult = a.result.empty() || a.result == "unknown" ? "" : a.result;
    if (a.name != b.name || a.race != b.race || a.slot != b.slot || remoteResult != b.result || a.type != b.type)
      return false;
  }
  return true;
}

enum class ExistingMatch { Missing, Equivalent, Conflict, Transient };

ExistingMatch ResolveConflict(Service &s, Job &job, const Endpoint &endpoint, const std::vector<unsigned char> &bytes,
                              const std::shared_ptr<std::atomic<bool>> &cancel, const std::string &claimOwner)
{
  ReplayClaims::Heartbeat(Join(JobDir(s, job.uuid), "claim"), claimOwner);
  PortHttp::Request download = RequestFor(s, ReplayKraken::MatchPath(endpoint, job.uuid), cancel);
  download.maxBytes = 64 * 1024 * 1024;
  const PortHttp::Response existing = PortHttp::Send(download);
  if (existing.status == 404 && existing.error.empty())
    return ExistingMatch::Missing;
  if (!existing.error.empty() || existing.status != 200)
    return ExistingMatch::Transient;
  if (existing.bytes != bytes)
    return ExistingMatch::Conflict;
  size_t offset = 0;
  for (unsigned pageNumber = 0; pageNumber < 50; ++pageNumber) {
    ReplayClaims::Heartbeat(Join(JobDir(s, job.uuid), "claim"), claimOwner);
    PortHttp::Request list = RequestFor(s, job.serverApi + "/matches?limit=200&offset=" + std::to_string(offset), cancel);
    list.maxBytes = ReplayKraken::kMaxResponseBytes;
    const PortHttp::Response page = PortHttp::Send(list);
    if (!page.error.empty() || page.status != 200)
      return ExistingMatch::Transient;
    MatchPage parsed;
    std::string error;
    if (!ReplayKraken::ParsePage(page.Text(), endpoint, &parsed, &error))
      return ExistingMatch::Conflict;
    const auto at = std::find_if(parsed.matches.begin(), parsed.matches.end(), [&](const Match &match) {
      return match.uuid == job.uuid;
    });
    if (at != parsed.matches.end())
      return SameMetadata(job, *at) ? ExistingMatch::Equivalent : ExistingMatch::Conflict;
    if (parsed.bComplete || parsed.matches.empty())
      return ExistingMatch::Conflict;  // No authoritative metadata was available to compare.
    offset = static_cast<size_t>(std::max<int64_t>(0, parsed.nextOffset));
  }
  return ExistingMatch::Conflict;  // A 10,000-row search is the explicit bounded limit.
}

void UploadOne(Service &s, Job &job)
{
  std::string claimOwner;
  if (!Claim(s, job, claimOwner))
    return;
  struct Release {
    Service &s;
    Job &job;
    std::string owner;
    ~Release() { Unclaim(s, job, owner); }
  } release{s, job, claimOwner};
  if (job.bAutomatic && !job.bManualOverride && !s.bAutomatic.load())
    return;
  Endpoint endpoint;
  std::string error;
  if (!ReplayKraken::EndpointParse(job.serverApi, &endpoint, &error) || endpoint.api != job.serverApi) {
    job.phase = UploadPhase::Failed;
    job.error = "The immutable destination is invalid: " + error;
    SaveJobState(s, job);
    PublishJobs(s);
    return;
  }
  const std::string token = ReadToken(s, job.serverApi);
  const std::string tokenHash = token.empty() ? "" : ReplayKraken::Sha256Hex(token.data(), token.size());
  if (token.empty()) {
    job.phase = UploadPhase::Failed;
    job.error = "No token for this job's original server. Restore its token or submit the replay to another server as new.";
    SaveJobState(s, job);
    PublishJobs(s);
    return;
  }
  if (!job.tokenHash.empty() && job.tokenHash != tokenHash) {
    job.phase = UploadPhase::Failed;
    job.error = "The account token changed after this job was committed. It was not sent; use Submit to create a new job.";
    SaveJobState(s, job);
    PublishJobs(s);
    return;
  }
  job.tokenHash = tokenHash;
  job.phase = UploadPhase::Uploading;
  job.error.clear();
  ++job.attempts;
  SaveJobState(s, job);
  PublishJobs(s);
  auto cancel = BeginNetwork(s, job.bAutomatic && !job.bManualOverride);
  Failure accountFailure = Failure::None;
  if (!AccountFor(s, job, token, cancel, &error, &accountFailure)) {
    EndNetwork(s);
    if (cancel->load()) {
      job.phase = UploadPhase::Queued;
      job.error = "Cancelled; the committed job will resume later.";
      SaveJobState(s, job);
      PublishJobs(s);
    } else if (accountFailure == Failure::Transient) {
      RetryLater(s, job, error, -1);
    } else {
      job.phase = UploadPhase::Failed;
      job.error = error;
      SaveJobState(s, job);
      PublishJobs(s);
    }
    return;
  }
  SaveJobState(s, job);
  ReplayClaims::Heartbeat(Join(JobDir(s, job.uuid), "claim"), claimOwner);
  std::vector<unsigned char> bytes;
  if (!ReadBytes(Join(JobDir(s, job.uuid), "replay.rep"), 64 * 1024 * 1024, &bytes, &error)) {
    EndNetwork(s);
    job.phase = UploadPhase::Failed;
    job.error = error;
    SaveJobState(s, job);
    PublishJobs(s);
    return;
  }
  std::string body;
  if (!ReplayKraken::UploadBody(job.uuid, job.metadata.gameVersion, job.metadata, bytes, &body, &error)) {
    EndNetwork(s);
    job.phase = UploadPhase::Unsupported;
    job.error = error;
    SaveJobState(s, job);
    PublishJobs(s);
    return;
  }
  const ExistingMatch existing = ResolveConflict(s, job, endpoint, bytes, cancel, claimOwner);
  if (cancel->load()) {
    EndNetwork(s);
    job.phase = UploadPhase::Queued;
    job.error = "Cancelled during the existing-match check; the same UUID will resume later.";
    SaveJobState(s, job);
    PublishJobs(s);
    return;
  }
  if (existing == ExistingMatch::Equivalent || existing == ExistingMatch::Conflict) {
    EndNetwork(s);
    job.phase = existing == ExistingMatch::Equivalent ? UploadPhase::Uploaded : UploadPhase::Conflict;
    job.error = existing == ExistingMatch::Equivalent ? "The server already held equivalent replay bytes and metadata." :
                "Conflict/Needs review: the existing UUID does not have equivalent replay bytes and metadata.";
    SaveJobState(s, job);
    PublishJobs(s);
    return;
  }
  if (existing == ExistingMatch::Transient) {
    EndNetwork(s);
    RetryLater(s, job, "The existing-match check failed; no new payload was sent.", -1);
    return;
  }
  ReplayClaims::Heartbeat(Join(JobDir(s, job.uuid), "claim"), claimOwner);
  PortHttp::Request request = RequestFor(s, job.serverApi + "/matches", cancel);
  request.method = "POST";
  request.bearer = token;
  request.contentType = "application/json";
  request.body = body;
  request.maxBytes = 64 * 1024;
  request.timeoutMs = 60000;
  const PortHttp::Response response = PortHttp::Send(request);
  EndNetwork(s);
  if (cancel->load() || s.bCancelAll.load()) {
    job.phase = UploadPhase::Queued;
    job.error = "Cancelled; the same UUID will resume later.";
    SaveJobState(s, job);
    PublishJobs(s);
    return;
  }
  if (response.status == 201) {
    std::string responseError;
    if (ReplayKraken::ParseCreatedUuid(response.Text(), job.uuid, &responseError)) {
      job.phase = UploadPhase::Uploaded;
      job.error.clear();
      job.nextRetryAt = 0;
      g_log("uploaded %s to %s", job.uuid.c_str(), job.serverApi.c_str());
    } else {
      job.phase = UploadPhase::Failed;
      job.error = responseError;
    }
    SaveJobState(s, job);
    PublishJobs(s);
    return;
  }
  if (response.status == 409) {
    // Another writer won the race. Recheck once: two instances committing the
    // same immutable snapshot can now be proved equivalent; anything else is Conflict.
    const ExistingMatch after = ResolveConflict(s, job, endpoint, bytes, cancel, claimOwner);
    EndNetwork(s);
    job.phase = after == ExistingMatch::Equivalent ? UploadPhase::Uploaded : UploadPhase::Conflict;
    job.error = after == ExistingMatch::Equivalent ? "The server already held equivalent replay bytes and metadata." :
                "Conflict/Needs review: another writer claimed this UUID during upload.";
    SaveJobState(s, job);
    PublishJobs(s);
    return;
  }
  if (!response.error.empty()) {
    const bool bPolicy = response.error.find("Refused") != std::string::npos ||
                         response.error.find("redirect") != std::string::npos ||
                         response.error.find("CA bundle") != std::string::npos;
    if (bPolicy) {
      job.phase = UploadPhase::Failed;
      job.error = response.error;
      SaveJobState(s, job);
      PublishJobs(s);
    } else {
      RetryLater(s, job, response.error + " The server may have accepted the request; the same UUID will be reused.", -1);
    }
    return;
  }
  const ReplayKraken::RetryDecision decision = ReplayKraken::ClassifyHttp(response.status, response.Header("retry-after"));
  if (decision.kind == Failure::Transient) {
    RetryLater(s, job, "Temporary upload failure (HTTP " + std::to_string(response.status) + ").", decision.retryAfterMs);
  } else {
    job.phase = decision.kind == Failure::Unsupported ? UploadPhase::Unsupported : UploadPhase::Failed;
    job.error = decision.kind == Failure::Unauthorized
                    ? "Replay Kraken rejected the token (HTTP 401/403). Replace it and submit as a new upload."
                    : "Upload failed permanently with HTTP " + std::to_string(response.status) + ": " + response.Text().substr(0, 512);
    SaveJobState(s, job);
    PublishJobs(s);
  }
}

void ProcessDue(Service &s)
{
  const int64_t now = ReplayLibrary::NowSeconds() * 1000;
  for (Job &job : s.jobs) {
    if (job.phase != UploadPhase::Queued && job.phase != UploadPhase::RetryWaiting)
      continue;
    if (job.nextRetryAt > now || (job.bAutomatic && !job.bManualOverride && !s.bAutomatic.load()))
      continue;
    UploadOne(s, job);
    break;
  }
}

void LoadJobs(Service &s)
{
  std::error_code ec;
  const fs::path root = PathOf(Join(s.root, "jobs"));
  fs::create_directories(root, ec);
  if (ec)
    return;
  std::vector<std::string> ids;
  for (const auto &entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
    if (entry.is_directory() && ReplayKraken::MatchUuidValid(PathText(entry.path().filename())))
      ids.push_back(PathText(entry.path().filename()));
  }
  std::sort(ids.begin(), ids.end());
  for (const std::string &uuid : ids) {
    std::string jobText, stateText, error;
    if (!ReadBounded(Join(JobDir(s, uuid), "job.json"), 1024 * 1024, &jobText, &error) ||
        !ReadBounded(Join(JobDir(s, uuid), "state.json"), 1024 * 1024, &stateText, &error))
      continue;
    PortJson jobJson, stateJson;
    if (!ParseJson(jobText, &jobJson, &error) || !ParseJson(stateText, &stateJson, &error))
      continue;
    Job job;
    job.uuid = Text(jobJson, "uuid");
    if (job.uuid != uuid || !ReplayKraken::MatchUuidValid(uuid))
      continue;
    job.contentId = Text(jobJson, "contentId");
    job.sourcePath = Text(jobJson, "sourcePath");
    job.serverApi = Text(jobJson, "serverApi");
    job.account = Text(stateJson, "account").empty() ? Text(jobJson, "account") : Text(stateJson, "account");
    job.tokenHash = Text(stateJson, "tokenHash").empty() ? Text(jobJson, "tokenHash") : Text(stateJson, "tokenHash");
    job.bAutomatic = jobJson["automatic"].b;
    job.bManualOverride = stateJson["manualOverride"].b;
    job.createdAt = Integer(jobJson, "createdAt");
    job.replayBytes = static_cast<size_t>(Integer(jobJson, "replayBytes"));
    if (!MetadataRead(jobJson["metadata"], &job.metadata, &error))
      continue;
    const int phase = static_cast<int>(Integer(stateJson, "phase"));
    job.phase = phase >= static_cast<int>(UploadPhase::Preparing) && phase <= static_cast<int>(UploadPhase::Unsupported)
                    ? static_cast<UploadPhase>(phase)
                    : UploadPhase::Failed;
    if (job.phase == UploadPhase::Uploading || job.phase == UploadPhase::Preparing)
      job.phase = UploadPhase::Queued;
    job.error = Text(stateJson, "error");
    job.attempts = static_cast<unsigned>(Integer(stateJson, "attempts"));
    job.nextRetryAt = Integer(stateJson, "nextRetryAt");
    s.jobs.push_back(job);
  }
  PublishJobs(s);
}

bool ActiveOutboxJob(const Job &job)
{
  return job.phase != UploadPhase::Uploaded;
}

void ReapJobs(Service &s)
{
  const int64_t cutoff = ReplayLibrary::NowSeconds() - 7 * 24 * 60 * 60;
  size_t uploadedCount = 0, uploadedBytes = 0;
  for (auto at = s.jobs.begin(); at != s.jobs.end();) {
    bool bRemove = at->phase == UploadPhase::Uploaded && at->createdAt < cutoff;
    if (at->phase == UploadPhase::Uploaded) {
      ++uploadedCount;
      uploadedBytes += at->replayBytes;
      // Keep successful history bounded separately from active work. Oldest
      // successful snapshots expire first; pending/failed jobs are untouched.
      bRemove = bRemove || uploadedCount > 32 || uploadedBytes > 64 * 1024 * 1024;
    }
    if (bRemove) {
      g_log("retention: removing uploaded snapshot %s", at->uuid.c_str());
      std::error_code ec;
      fs::remove_all(PathOf(JobDir(s, at->uuid)), ec);
      at = s.jobs.erase(at);
    } else {
      ++at;
    }
  }
  PublishJobs(s);
}

void CommitUpload(Service &s, const Endpoint &endpoint, bool bEndpointValid, const std::string &uuid,
                  const std::string &sourcePath, const UploadMetadata &metadata, const std::string &contentId,
                  bool bAutomatic, const std::vector<unsigned char> &captured)
{
  std::vector<unsigned char> bytes = captured;
  std::string error;
  if (bytes.empty() && !ReadBytes(sourcePath, 64 * 1024 * 1024, &bytes, &error)) {
    FailPreparing(s, uuid, error);
    return;
  }
  if (bytes.empty()) {
    FailPreparing(s, uuid, "An empty or incomplete replay cannot be submitted.");
    return;
  }
  const size_t activeJobs = static_cast<size_t>(std::count_if(s.jobs.begin(), s.jobs.end(), ActiveOutboxJob));
  if (activeJobs >= kMaxJobs) {
    FailPreparing(s, uuid, "The active upload outbox is full (64 jobs). Existing committed jobs are unchanged; retry after some finish or discard terminal jobs.");
    return;
  }
  size_t total = 0;
  for (const Job &job : s.jobs)
    if (ActiveOutboxJob(job))
      total += job.replayBytes;
  if (total > kMaxOutboxBytes || bytes.size() > kMaxOutboxBytes - total) {
    FailPreparing(s, uuid, "The upload outbox would exceed 256 MiB. Existing committed jobs are unchanged.");
    return;
  }
  if (!bEndpointValid) {
    FailPreparing(s, uuid, "Configure a valid Replay Kraken server before Submit.");
    return;
  }
  const std::string actualContent = ReplayKraken::ContentId(bytes);
  if (actualContent.empty() || (!contentId.empty() && contentId != actualContent)) {
    FailPreparing(s, uuid, "The replay changed before its immutable upload snapshot could be made; nothing was queued.");
    return;
  }
  Job job;
  job.uuid = uuid;
  job.contentId = actualContent;
  job.sourcePath = sourcePath;
  job.serverApi = endpoint.api;
  job.metadata = metadata;
  job.bAutomatic = bAutomatic;
  job.createdAt = ReplayLibrary::NowSeconds();
  job.replayBytes = bytes.size();
  {
    std::lock_guard<std::mutex> lock(s.m);
    if (s.state.serverApi == endpoint.api)
      job.account = s.state.account;
  }
  const std::string token = ReadToken(s, endpoint.api);
  job.tokenHash = token.empty() ? "" : ReplayKraken::Sha256Hex(token.data(), token.size());
  const std::string temp = Join(Join(s.root, "incoming"), uuid + ".tmp");
  std::error_code ec;
  fs::remove_all(PathOf(temp), ec);
  const std::string replayPath = Join(temp, "replay.rep");
  if (!WriteBytes(replayPath, bytes, &error) ||
      !AtomicWrite(Join(temp, "job.json"), PortJsonWrite(JobJson(job)), &error) ||
      !AtomicWrite(Join(temp, "state.json"), PortJsonWrite(JobStateJson(job)), &error)) {
    fs::remove_all(PathOf(temp), ec);
    FailPreparing(s, uuid, error + " The new job was not queued; every previous job is unchanged.");
    return;
  }
  const std::string finalDir = JobDir(s, uuid);
  fs::create_directories(PathOf(Join(s.root, "jobs")), ec);
  fs::rename(PathOf(temp), PathOf(finalDir), ec);
  if (ec) {
    fs::remove_all(PathOf(temp), ec);
    FailPreparing(s, uuid, "Cannot atomically commit the upload snapshot; nothing was queued: " + ec.message());
    return;
  }
  s.jobs.push_back(job);
  PublishJobs(s);
  ProcessDue(s);
}

PortJson MatchJson(const Match &match)
{
  PortJson out = Object();
  Add(&out, "uuid", String(match.uuid));
  Add(&out, "serverOrigin", String(match.serverOrigin));
  Add(&out, "serverApi", String(match.serverApi));
  Add(&out, "game_version", String(match.gameVersion));
  Add(&out, "map", String(match.map));
  Add(&out, "game_type", String(match.gameType));
  Add(&out, "duration_frames", Number(match.durationFrames));
  Add(&out, "matchup", String(match.matchup));
  Add(&out, "uploaded_by", String(match.uploadedBy));
  Add(&out, "uploaded_at", String(match.uploadedAt));
  PortJson players = Array();
  for (const ReplayKraken::Player &player : match.players) {
    PortJson row = Object();
    Add(&row, "name", String(player.name));
    Add(&row, "race", String(player.race));
    Add(&row, "slot", Number(player.slot));
    Add(&row, "result", String(player.result));
    Add(&row, "type", String(player.type));
    Add(&row, "discord_id", String(player.discordId));
    players.items.push_back(row);
  }
  Add(&out, "players", players);
  return out;
}

PortJson CacheJson(const Service &s)
{
  PortJson out = Object();
  Add(&out, "format", String("hyperion/replay-kraken-cache"));
  Add(&out, "version", Number(1));
  Add(&out, "cachedAt", Number(s.cacheAt));
  PortJson rows = Array();
  for (const Match &match : s.cache.matches)
    rows.items.push_back(MatchJson(match));
  Add(&out, "matches", rows);
  return out;
}

void SaveCache(Service &s)
{
  std::string error;
  if (!s.cache.matches.empty() && !AtomicWrite(CachePath(s, s.cache.matches.front().serverApi), PortJsonWrite(CacheJson(s)), &error))
    SetError(s, error);
}

void LoadCache(Service &s, const Endpoint &endpoint)
{
  std::string text, error;
  if (!ReadBounded(CachePath(s, endpoint.api), ReplayKraken::kMaxResponseBytes, &text, &error))
    return;
  PortJson root;
  if (!ParseJson(text, &root, &error) || root["format"].Str() != "hyperion/replay-kraken-cache" ||
      root["version"].Num() != 1 || root["matches"].kind != PortJson::ARRAY || root["matches"].items.size() > 10000) {
    const std::string path = CachePath(s, endpoint.api);
    std::error_code ec;
    fs::rename(PathOf(path), PathOf(path + ".corrupt-" + ReplayKraken::UuidNew()), ec);
    SetError(s, "The online cache was corrupt or unsupported and was preserved for diagnosis: " + error);
    return;
  }
  MatchPage page;
  for (const PortJson &row : root["matches"].items) {
    // Reuse the protocol parser's bounds and required fields by wrapping one row.
    PortJson envelope = Object();
    PortJson matches = Array();
    matches.items.push_back(row);
    Add(&envelope, "matches", matches);
    Add(&envelope, "total", Number(1));
    Add(&envelope, "limit", Number(1));
    Add(&envelope, "offset", Number(0));
    MatchPage one;
    if (!ReplayKraken::ParsePage(PortJsonWrite(envelope), endpoint, &one, &error))
      continue;
    if (one.matches[0].serverApi != endpoint.api || Text(row, "serverApi") != endpoint.api ||
        Text(row, "serverOrigin") != endpoint.origin)
      continue;
    page.matches.push_back(one.matches[0]);
  }
  s.cache = page;
  s.cacheAt = Integer(root, "cachedAt");
}

void SaveDownloads(Service &s)
{
  PortJson root = Object();
  Add(&root, "format", String("hyperion/replay-kraken-downloads"));
  Add(&root, "version", Number(1));
  PortJson rows = Array();
  for (const DownloadRecord &record : s.downloaded) {
    PortJson row = Object();
    Add(&row, "serverApi", String(record.serverApi));
    Add(&row, "uuid", String(record.uuid));
    Add(&row, "path", String(record.path));
    Add(&row, "contentId", String(record.contentId));
    rows.items.push_back(row);
  }
  Add(&root, "matches", rows);
  std::string error;
  if (!AtomicWrite(Join(s.root, "downloads.json"), PortJsonWrite(root), &error))
    SetError(s, error);
}

void LoadDownloads(Service &s)
{
  std::string text, error;
  if (!ReadBounded(Join(s.root, "downloads.json"), 16 * 1024 * 1024, &text, &error))
    return;
  PortJson root;
  if (!ParseJson(text, &root, &error) || root["matches"].kind != PortJson::ARRAY || root["matches"].items.size() > 10000)
    return;
  for (const PortJson &row : root["matches"].items) {
    DownloadRecord record{Text(row, "serverApi"), Text(row, "uuid"), Text(row, "path"), Text(row, "contentId")};
    Endpoint endpoint;
    if (ReplayKraken::EndpointParse(record.serverApi, &endpoint, &error) && endpoint.api == record.serverApi &&
        ReplayKraken::MatchUuidValid(record.uuid)) {
      s.downloaded.push_back(record);
      std::error_code ec;
      if (fs::is_regular_file(PathOf(record.path), ec)) {
        DownloadView view;
        view.serverApi = record.serverApi;
        view.uuid = record.uuid;
        view.phase = DownloadPhase::Ready;
        view.path = record.path;
        view.contentId = record.contentId;
        s.state.downloads.push_back(view);
      }
    }
  }
}

void Worker(Service &s)
{
  {
    ReplayLibrary::Data library;
    std::string error;
    if (ReplayLibrary::LoadAtomic(Join(s.root, "library.json"), &library, &error)) {
      std::lock_guard<std::mutex> lock(s.m);
      s.library = library;
    } else if (!error.empty()) {
      std::error_code ec;
      const std::string path = Join(s.root, "library.json");
      fs::rename(PathOf(path), PathOf(path + ".corrupt-" + ReplayKraken::UuidNew()), ec);
      SetError(s, "The replay library cache was corrupt and was preserved for diagnosis: " + error);
    }
  }
  Endpoint endpoint;
  bool bEndpointValid;
  {
    std::lock_guard<std::mutex> lock(s.m);
    endpoint = s.endpoint;
    bEndpointValid = s.bEndpointValid;
  }
  if (bEndpointValid) {
    LoadCache(s, endpoint);
    RefreshTokenState(s, endpoint);
  }
  LoadDownloads(s);
  LoadJobs(s);
  ReapJobs(s);
  {
    std::lock_guard<std::mutex> lock(s.m);
    s.state.bWorkerBusy = false;
  }
  ProcessDue(s);
  std::unique_lock<std::mutex> lock(s.m);
  for (;;) {
    s.cv.wait_for(lock, std::chrono::milliseconds(500), [&] { return s.bStop || !s.tasks.empty(); });
    if (s.bStop && s.tasks.empty())
      break;
    if (!s.tasks.empty()) {
      auto task = std::move(s.tasks.front());
      s.tasks.pop_front();
      s.state.bWorkerBusy = true;
      lock.unlock();
      task();
      lock.lock();
      if (s.tasks.empty())
        s.state.bWorkerBusy = false;
      continue;
    }
    lock.unlock();
    ProcessDue(s);
    ReapJobs(s);
    lock.lock();
  }
  lock.unlock();
  EndNetwork(s);
  {
    std::lock_guard<std::mutex> done(s.m);
    s.bWorkerDone = true;
    s.state.bWorkerBusy = false;
  }
  s.cv.notify_all();
}

Service &RequireService()
{
  {
    std::lock_guard<std::mutex> lock(g_start);
    if (g_service)
      return *g_service;
  }
  Start(PortDataFolder(), PortProgramFolder());
  std::lock_guard<std::mutex> lock(g_start);
  if (!g_service)
    throw std::runtime_error("Replay online service is unavailable");
  return *g_service;
}

bool QueryEqual(const Query &a, const Query &b)
{
  return a.player == b.player && a.race == b.race && a.result == b.result && a.matchup == b.matchup &&
         a.from == b.from && a.to == b.to && a.followedBy == b.followedBy && a.search == b.search &&
         a.bFavourites == b.bFavourites && a.bStarted == b.bStarted && a.bNotes == b.bNotes;
}

void ShowCachedRows(Service &s)
{
  std::lock_guard<std::mutex> lock(s.m);
  if (s.cache.matches.empty())
    return;
  s.state.list.page = s.cache;
  s.state.list.page.total = static_cast<int64_t>(s.cache.matches.size());
  s.state.list.page.bComplete = true;
  s.state.list.bStale = true;
  s.state.list.cachedAt = s.cacheAt;
}

void RefreshFollowing(Service &s, const Endpoint &endpoint)
{
  const std::string token = ReadToken(s, endpoint.api);
  if (token.empty()) {
    std::lock_guard<std::mutex> lock(s.m);
    s.state.following.clear();
    return;
  }
  auto cancel = BeginNetwork(s, false);
  PortHttp::Request request = RequestFor(s, endpoint.api + "/me/following", cancel);
  request.bearer = token;
  request.maxBytes = 256 * 1024;
  const PortHttp::Response response = PortHttp::Send(request);
  EndNetwork(s);
  if (!response.error.empty() || response.status != 200)
    return;
  PortJson root;
  std::string error;
  if (!ParseJson(response.Text(), &root, &error) || root["players"].kind != PortJson::ARRAY ||
      root["players"].items.size() > 1024)
    return;
  std::vector<std::string> players;
  for (const PortJson &player : root["players"].items)
    if (player.kind == PortJson::STRING && player.s.size() <= 256)
      players.push_back(player.s);
  std::lock_guard<std::mutex> lock(s.m);
  s.state.following = std::move(players);
}

bool CurrentGeneration(Service &s, uint64_t generation)
{
  std::lock_guard<std::mutex> lock(s.m);
  return generation == s.generation;
}

void FetchTask(Service &s, const Endpoint &endpoint, Query query, size_t offset, bool bReset, uint64_t generation)
{
  if (!CurrentGeneration(s, generation))
    return;
  const std::string url = ReplayKraken::ListUrl(endpoint, query, offset);
  auto cancel = BeginNetwork(s, false);
  PortHttp::Request request = RequestFor(s, url, cancel);
  if (!query.followedBy.empty())
    request.bearer = ReadToken(s, endpoint.api);
  const PortHttp::Response response = PortHttp::Send(request);
  EndNetwork(s);
  if (!CurrentGeneration(s, generation) || cancel->load())
    return;
  std::unique_lock<std::mutex> lock(s.m);
  s.state.list.bLoading = false;
  s.state.list.bPaginating = false;
  if (!response.error.empty() || response.status != 200) {
    s.state.list.bOffline = true;
    s.state.list.bStale = !s.cache.matches.empty();
    if (s.state.list.page.matches.empty() && !s.cache.matches.empty()) {
      s.state.list.page = s.cache;
      s.state.list.page.total = static_cast<int64_t>(s.cache.matches.size());
      s.state.list.page.bComplete = true;
      s.state.list.cachedAt = s.cacheAt;
    }
    s.state.list.bUnauthorized = response.status == 401 || response.status == 403;
    s.state.list.error = response.error.empty() ? "Archive request failed with HTTP " + std::to_string(response.status) :
                                                 response.error;
    return;
  }
  MatchPage page;
  std::string error;
  if (!ReplayKraken::ParsePage(response.Text(), endpoint, &page, &error)) {
    s.state.list.bOffline = true;
    s.state.list.error = "Malformed archive response: " + error;
    return;
  }
  if (bReset)
    s.state.list.page = ReplayKraken::MatchPage();
  ReplayKraken::MergePage(&s.state.list.page, page);
  s.state.list.query = query;
  s.state.list.serverApi = endpoint.api;
  s.state.list.bOffline = false;
  s.state.list.bStale = false;
  s.state.list.bUnauthorized = false;
  s.state.list.error.clear();
  lock.unlock();
  if (bReset)
    s.cache = ReplayKraken::MatchPage();
  ReplayKraken::MergePage(&s.cache, page);
  s.cacheAt = ReplayLibrary::NowSeconds();
  SaveCache(s);
}

// The writer capture is main-thread only and never owns a FILE or calls a worker.
struct Capture {
  void *pFile = nullptr;
  std::vector<unsigned char> bytes;
  UploadMetadata metadata;
  bool bActive = false;
};
Capture g_capture;

std::string RaceName(int race)
{
  return race == 0 ? "Zerg" : race == 1 ? "Terran" : race == 2 ? "Protoss" : "unknown";
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
    case 0x0F: return "Team Melee (custom)";
    default: return "Unknown";
  }
}

}  // namespace

void Start(const std::string &dataFolder, const std::string &programFolder)
{
  std::lock_guard<std::mutex> lock(g_start);
  if (g_service)
    return;
  Service *s = new Service();
  s->root = Slashes(dataFolder) + "/replay-kraken";
  s->replayRoot = Slashes(dataFolder) + "/maps/replays";
  s->caFile = Slashes(programFolder) + "/cacert.pem";
  s->state.bAvailable = PortHttp::Available();
  g_service = s;
  app_add_exit(ReplayOnline::Stop);
#if !defined(__EMSCRIPTEN__)
  std::thread([s] { Worker(*s); }).detach();
#else
  s->state.bAvailable = false;
  s->state.lastError = "Online replay services need a native Hyperion build.";
  s->bWorkerDone = true;
#endif
}

void Stop()
{
  Service *service;
  {
    std::lock_guard<std::mutex> lock(g_start);
    if (!g_service)
      return;
    service = g_service;
  }
  Service &s = *service;
  s.bCancelAll.store(true);
  {
    std::lock_guard<std::mutex> state(s.m);
    s.bStop = true;
    if (s.activeCancel)
      s.activeCancel->store(true);
  }
  s.cv.notify_all();
  // Give immutable commit tasks a bounded grace period. Network is cancelled
  // immediately; a stalled worker is never joined indefinitely.
  std::unique_lock<std::mutex> wait(s.m);
  s.cv.wait_for(wait, std::chrono::milliseconds(3000), [&] { return s.bWorkerDone; });
}

void Configure(const std::string &serverText, bool bAutomatic)
{
  Service &s = RequireService();
  Endpoint endpoint;
  std::string error;
  const bool bValid = ReplayKraken::EndpointParse(serverText, &endpoint, &error);
  const bool bWasAutomatic = s.bAutomatic.exchange(bAutomatic);
  {
    std::lock_guard<std::mutex> lock(s.m);
    s.state.serverInput = serverText;
    s.state.serverApi = bValid ? endpoint.api : "";
    s.state.account.clear();
    s.state.bTokenPresent = false;
    s.state.bAutomatic = bAutomatic;
    s.state.list = ListView();
    s.state.list.serverApi = s.state.serverApi;
    s.endpoint = endpoint;
    s.bEndpointValid = bValid;
    s.state.lastError = bValid ? "" : error;
    ++s.generation;
    if (!bAutomatic && s.bActiveAutomatic && s.activeCancel)
      s.activeCancel->store(true);
  }
  if (bWasAutomatic && !bAutomatic)
    PublishJobs(s);
  Post(s, [&s, endpoint, bValid] {
    if (!bValid)
      return;
    LoadCache(s, endpoint);
    RefreshTokenState(s, endpoint);
    RefreshFollowing(s, endpoint);
    PublishJobs(s);
    ShowCachedRows(s);
  });
}

void SetToken(const std::string &token)
{
  Service &s = RequireService();
  Endpoint endpoint;
  bool bValid;
  {
    std::lock_guard<std::mutex> lock(s.m);
    endpoint = s.endpoint;
    bValid = s.bEndpointValid;
  }
  Post(s, [&s, token, endpoint, bValid] {
    if (!bValid)
      return;
    std::string error;
    const std::string path = TokenPath(s, endpoint.api);
    if (token.empty()) {
      std::error_code ec;
      fs::remove(PathOf(path), ec);
    } else if (token.size() > 4096 || !PortWritePrivateFile(path.c_str(), (token + "\n").c_str())) {
      SetError(s, "The private Replay Kraken token file could not be written.");
    }
    RefreshTokenState(s, endpoint);
  });
}

State Snapshot()
{
  Service &s = RequireService();
  std::lock_guard<std::mutex> lock(s.m);
  return s.state;
}

void Fetch(const Query &query, bool bReset)
{
  Service &s = RequireService();
  uint64_t generation;
  Endpoint endpoint;
  {
    std::lock_guard<std::mutex> lock(s.m);
    if (!s.bEndpointValid)
      return;
    if (!bReset && s.state.list.bLoading && QueryEqual(s.state.list.query, query))
      return;
    endpoint = s.endpoint;
    generation = bReset ? ++s.generation : s.generation;
    s.state.list.query = query;
    s.state.list.bLoading = true;
    s.state.list.bPaginating = !bReset && !s.state.list.page.matches.empty();
    s.state.list.error.clear();
    if (bReset)
      s.state.list.page = MatchPage();
  }
  Post(s, [&s, endpoint, query, bReset, generation] { FetchTask(s, endpoint, query, 0, bReset, generation); });
}

void LoadMore()
{
  Service &s = RequireService();
  Query query;
  size_t offset = 0;
  uint64_t generation;
  Endpoint endpoint;
  {
    std::lock_guard<std::mutex> lock(s.m);
    if (!s.bEndpointValid || s.state.list.bLoading || s.state.list.page.bComplete || s.state.list.bOffline)
      return;
    endpoint = s.endpoint;
    query = s.state.list.query;
    offset = static_cast<size_t>(std::max<int64_t>(0, s.state.list.page.nextOffset));
    generation = s.generation;
    s.state.list.bLoading = true;
    s.state.list.bPaginating = true;
  }
  Post(s, [&s, endpoint, query, offset, generation] { FetchTask(s, endpoint, query, offset, false, generation); });
}

void Follow(const std::string &player, bool bFollow)
{
  Service &s = RequireService();
  if (player.empty() || player.size() > 256)
    return;
  Endpoint endpoint;
  bool bValid;
  {
    std::lock_guard<std::mutex> lock(s.m);
    endpoint = s.endpoint;
    bValid = s.bEndpointValid;
  }
  Post(s, [&s, player, bFollow, endpoint, bValid] {
    if (!bValid)
      return;
    const std::string token = ReadToken(s, endpoint.api);
    if (token.empty()) {
      SetError(s, "Follow requires this server's bearer token in Settings.");
      return;
    }
    auto cancel = BeginNetwork(s, false);
    PortHttp::Request request = RequestFor(s, endpoint.api + "/players/" + ReplayKraken::UrlEncode(player) + "/follow", cancel);
    request.method = bFollow ? "PUT" : "DELETE";
    request.bearer = token;
    request.maxBytes = 64 * 1024;
    const PortHttp::Response response = PortHttp::Send(request);
    EndNetwork(s);
    if (!response.error.empty() || response.status != 200) {
      SetError(s, response.error.empty() ? "Follow request failed with HTTP " + std::to_string(response.status) : response.error);
      return;
    }
    RefreshFollowing(s, endpoint);
  });
}

void FailDownloadView(Service &s, const std::string &serverApi, const std::string &uuid, const std::string &error)
{
  SetError(s, error);
  std::lock_guard<std::mutex> lock(s.m);
  for (DownloadView &view : s.state.downloads)
    if (view.serverApi == serverApi && view.uuid == uuid) {
      view.phase = DownloadPhase::Failed;
      view.error = error;
      view.bytes.clear();
    }
}

void Download(const Match &match)
{
  Service &s = RequireService();
  {
    std::lock_guard<std::mutex> lock(s.m);
    const auto old = std::find_if(s.state.downloads.begin(), s.state.downloads.end(), [&](const DownloadView &view) {
      return view.serverApi == match.serverApi && view.uuid == match.uuid && view.phase != DownloadPhase::Failed;
    });
    if (old != s.state.downloads.end())
      return;
    DownloadView view;
    view.serverApi = match.serverApi;
    view.uuid = match.uuid;
    s.state.downloads.push_back(view);
  }
  Post(s, [&s, match] {
    Endpoint endpoint;
    std::string error;
    if (!ReplayKraken::EndpointParse(match.serverApi, &endpoint, &error) || endpoint.api != match.serverApi ||
        match.serverOrigin != endpoint.origin) {
      SetError(s, "The cached match has an unsafe or inconsistent server origin.");
      return;
    }
    {
      std::lock_guard<std::mutex> lock(s.m);
      for (DownloadView &view : s.state.downloads)
        if (view.serverApi == match.serverApi && view.uuid == match.uuid)
          view.phase = DownloadPhase::Downloading;
    }
    auto cancel = BeginNetwork(s, false);
    PortHttp::Request request = RequestFor(s, ReplayKraken::MatchPath(endpoint, match.uuid), cancel);
    request.maxBytes = 64 * 1024 * 1024;
    request.timeoutMs = 60000;
    const PortHttp::Response response = PortHttp::Send(request);
    EndNetwork(s);
    if (!response.error.empty() || response.status != 200 || response.bytes.empty()) {
      std::lock_guard<std::mutex> lock(s.m);
      for (DownloadView &view : s.state.downloads)
        if (view.serverApi == match.serverApi && view.uuid == match.uuid) {
          view.phase = DownloadPhase::Failed;
          view.error = response.error.empty() ? "Download failed with HTTP " + std::to_string(response.status) : response.error;
        }
      return;
    }
    const std::string temp = Join(Join(s.root, "downloads"), match.uuid + "-" + ReplayKraken::UuidNew() + ".part");
    if (!WriteBytes(temp, response.bytes, &error)) {
      FailDownloadView(s, match.serverApi, match.uuid, error);
      return;
    }
    g_validationRequests.fetch_add(1);
    std::lock_guard<std::mutex> lock(s.m);
    for (DownloadView &view : s.state.downloads)
      if (view.serverApi == match.serverApi && view.uuid == match.uuid) {
        view.phase = DownloadPhase::Validating;
        view.tempPath = temp;
        view.contentId = ReplayKraken::ContentId(response.bytes);
        view.bytes = response.bytes;
      }
  });
}

void DownloadValidated(const std::string &serverApi, const std::string &uuid, bool bValid,
                       const std::string &validationError)
{
  Service &s = RequireService();
  Post(s, [&s, serverApi, uuid, bValid, validationError] {
    DownloadView current;
    Match match;
    {
      std::lock_guard<std::mutex> lock(s.m);
      for (const DownloadView &view : s.state.downloads)
        if (view.serverApi == serverApi && view.uuid == uuid)
          current = view;
      for (const Match &cached : s.cache.matches)
        if (cached.serverApi == serverApi && cached.uuid == uuid)
          match = cached;
    }
    if (current.tempPath.empty())
      return;
    std::error_code ec;
    if (!bValid) {
      fs::remove(PathOf(current.tempPath), ec);
      std::lock_guard<std::mutex> lock(s.m);
      for (DownloadView &view : s.state.downloads)
        if (view.serverApi == serverApi && view.uuid == uuid) {
          view.phase = DownloadPhase::Failed;
          view.error = validationError;
          view.bytes.clear();
        }
      return;
    }
    Endpoint endpoint;
    std::string error;
    if (!ReplayKraken::EndpointParse(serverApi, &endpoint, &error)) {
      FailDownloadView(s, serverApi, uuid, error);
      return;
    }
    std::string base = match.map.empty() ? "replay" : ReplayKraken::SafeFilePart(match.map, 56);
    const std::string folder = Join(s.replayRoot, "Replay Kraken");
    std::string destination = Join(folder, base + "-" + uuid.substr(0, 8) + ".rep");
    std::vector<unsigned char> existing;
    if (ReadBytes(destination, 64 * 1024 * 1024, &existing, &error) && ReplayKraken::ContentId(existing) == current.contentId) {
      fs::remove(PathOf(current.tempPath), ec);
    } else {
      destination = Join(folder, base + "-" + uuid + "-" + current.contentId.substr(7, 8) + ".rep");
      fs::create_directories(PathOf(folder), ec);
      fs::rename(PathOf(current.tempPath), PathOf(destination), ec);
      if (ec) {
        FailDownloadView(s, serverApi, uuid, "Cannot install the validated replay without overwriting another file: " + ec.message());
        return;
      }
    }
    DownloadRecord record{serverApi, uuid, destination, current.contentId};
    s.downloaded.erase(std::remove_if(s.downloaded.begin(), s.downloaded.end(), [&](const DownloadRecord &old) {
      return old.serverApi == serverApi && old.uuid == uuid;
    }), s.downloaded.end());
    s.downloaded.push_back(record);
    SaveDownloads(s);
    {
      std::lock_guard<std::mutex> lock(s.m);
      ReplayLibrary::LinkDownload(&s.library, endpoint, uuid, current.contentId);
      for (DownloadView &view : s.state.downloads)
        if (view.serverApi == serverApi && view.uuid == uuid) {
          view.phase = DownloadPhase::Ready;
          view.path = destination;
          view.error.clear();
          view.bytes.clear();
        }
    }
    Post(s, [&s] {
      const std::string json = ReplayLibrary::ToJson(s.library);
      std::string writeError;
      if (!AtomicWrite(Join(s.root, "library.json"), json, &writeError))
        SetError(s, writeError);
    });
  });
}

std::string Submit(const std::string &sourcePath, const UploadMetadata &metadata,
                   const std::string &contentId, bool bAutomatic)
{
  Service &s = RequireService();
  const std::string uuid = ReplayKraken::UuidNew();
  if (uuid.empty())
    return "";
  Endpoint endpoint;
  bool bValid;
  {
    std::lock_guard<std::mutex> lock(s.m);
    endpoint = s.endpoint;
    bValid = s.bEndpointValid;
    UploadView preparing;
    preparing.uuid = uuid;
    preparing.contentId = contentId;
    preparing.sourcePath = sourcePath;
    preparing.serverApi = endpoint.api;
    preparing.bAutomatic = bAutomatic;
    s.state.uploads.push_back(preparing);
  }
  Post(s, [&s, endpoint, bValid, uuid, sourcePath, metadata, contentId, bAutomatic] {
    CommitUpload(s, endpoint, bValid, uuid, sourcePath, metadata, contentId, bAutomatic, {});
  });
  return uuid;
}

void Retry(const std::string &uuid)
{
  Service &s = RequireService();
  Post(s, [&s, uuid] {
    Job *job = FindJob(s, uuid);
    if (!job || job->phase == UploadPhase::Uploading)
      return;
    job->phase = UploadPhase::Queued;
    job->attempts = 0;
    job->nextRetryAt = 0;
    job->bManualOverride = true;
    job->error.clear();
    SaveJobState(s, *job);
    PublishJobs(s);
    ProcessDue(s);
  });
}

void Discard(const std::string &uuid)
{
  Service &s = RequireService();
  Post(s, [&s, uuid] {
    const auto at = std::find_if(s.jobs.begin(), s.jobs.end(), [&](const Job &job) { return job.uuid == uuid; });
    if (at == s.jobs.end())
      return;
    if (at->phase == UploadPhase::Preparing || at->phase == UploadPhase::Queued || at->phase == UploadPhase::Uploading ||
        at->phase == UploadPhase::RetryWaiting)
      return;
    std::error_code ec;
    fs::remove_all(PathOf(JobDir(s, uuid)), ec);
    s.jobs.erase(at);
    PublishJobs(s);
  });
}

void LocalRenamed(const std::string &oldPath, const std::string &newPath)
{
  Service &s = RequireService();
  Post(s, [&s, oldPath, newPath] {
    for (DownloadRecord &record : s.downloaded)
      if (record.path == oldPath)
        record.path = newPath;
    for (DownloadView &view : s.state.downloads)
      if (view.path == oldPath)
        view.path = newPath;
    SaveDownloads(s);
  });
}

void LocalDeleted(const std::string &path)
{
  Service &s = RequireService();
  Post(s, [&s, path] {
    s.downloaded.erase(std::remove_if(s.downloaded.begin(), s.downloaded.end(), [&](const DownloadRecord &record) {
      return record.path == path;
    }), s.downloaded.end());
    for (DownloadView &view : s.state.downloads)
      if (view.path == path) {
        view.phase = DownloadPhase::Failed;
        view.error = "The downloaded file was deleted; download it again to restore it.";
        view.path.clear();
      }
    SaveDownloads(s);
  });
}

ReplayLibrary::Data LibrarySnapshot()
{
  Service &s = RequireService();
  std::lock_guard<std::mutex> lock(s.m);
  return s.library;
}

void SaveLibrary(const ReplayLibrary::Data &data)
{
  Service &s = RequireService();
  const std::string json = ReplayLibrary::ToJson(data);
  {
    std::lock_guard<std::mutex> lock(s.m);
    s.library = data;
  }
  Post(s, [&s, json] {
    std::string error;
    if (!AtomicWrite(Join(s.root, "library.json"), json, &error))
      SetError(s, error);
  });
}

std::string ExportLibrary(const ReplayLibrary::ExportSelection &selection, const std::string &path)
{
  Service &s = RequireService();
  const std::string json = ReplayLibrary::ExportJson(LibrarySnapshot(), selection);
  Post(s, [&s, json, path] {
    std::string error;
    if (!AtomicWrite(path, json, &error))
      SetError(s, error);
  });
  return path;
}

void ImportLibrary(const std::string &path)
{
  Service &s = RequireService();
  Post(s, [&s, path] {
    std::string json, error;
    if (!ReadBounded(path, 32 * 1024 * 1024, &json, &error)) {
      SetError(s, error);
      return;
    }
    ReplayLibrary::Data merged;
    {
      std::lock_guard<std::mutex> lock(s.m);
      merged = s.library;
    }
    if (!ReplayLibrary::ImportJson(json, &merged, &error)) {
      SetError(s, error);
      return;
    }
    {
      std::lock_guard<std::mutex> lock(s.m);
      s.library = merged;
    }
    SaveLibrary(merged);
  });
}

bool CaptureAutomaticEligible()
{
  double enabled = 0.0;
  if (!PortSettingNumber("hyperion", "replayKrakenAuto", &enabled) || enabled == 0.0 || gPlayback ||
      PortBenchMaking() || PortWatchMaking())
    return false;
  const PortOptions *options = PortCommandOptions();
  if (options->szRecord[0] || options->bBench || options->bTrainer)
    return false;
  char server[2048];
  Endpoint endpoint;
  std::string error;
  if (!PortSettingString("hyperion", "replayKrakenServer", server, sizeof server) ||
      !ReplayKraken::EndpointParse(server, &endpoint, &error))
    return false;
  // One automatic submission per LAN room: every seat can evaluate the same
  // recorded human slots; only the lowest occupied human slot uploads.
  int firstHuman = -1;
  for (int slot = 0; slot < PlayerSlots(); ++slot)
    if (gPlayerData[slot].nType == 2) {
      firstHuman = slot;
      break;
    }
  if (firstHuman < 0)
    return false;
  const int local = static_cast<int>(g_LocalHumanID);
  return local == firstHuman;
}

void CaptureBegin(void *pFile)
{
  g_capture = Capture();
  if (!CaptureAutomaticEligible())
    return;
  g_capture.pFile = pFile;
  g_capture.bytes.reserve(256 * 1024);
  g_capture.metadata.gameVersion = HYPERION_VERSION_TEXT;
  g_capture.bActive = true;
}

void CaptureHeader(const void *pHeader, size_t bytes)
{
  if (!g_capture.bActive || bytes < 0x279)
    return;
  const unsigned char *header = static_cast<const unsigned char *>(pHeader);
  uint32_t frames = 0;
  memcpy(&frames, header + 1, 4);
  if (frames == 0 && oc_dwReplayFrame != 0)
    frames = oc_dwReplayFrame;  // A very early leave has not filled the header's total yet.
  g_capture.metadata.durationFrames = frames;
  g_capture.metadata.map = MapTextUtf8(std::string(reinterpret_cast<const char *>(header + 0x61),
      strnlen(reinterpret_cast<const char *>(header + 0x61), 0x20)).c_str());
  g_capture.metadata.gameType = GameType(header[0x3C]);
  for (int slot = 0; slot < PlayerSlots(); ++slot) {
    const PlayerData &data = gPlayerData[slot];
    if (data.nType != 1 && data.nType != 2)
      continue;
    UploadPlayer player;
    player.name = MapTextUtf8(std::string(data.szName, strnlen(data.szName, sizeof data.szName)).c_str());
    player.race = RaceName(data.nRace);
    player.slot = slot;
    player.type = data.nType == 1 ? "computer" : "human";
    g_capture.metadata.players.push_back(player);
  }
  const bool bTeams = header[0x3C] == 0x0A || (header[0x3C] >= 0x0B && header[0x3C] <= 0x0F);
  if (g_capture.metadata.players.size() == 2) {
    for (const UploadPlayer &player : g_capture.metadata.players) {
      const char letter = player.race == "Zerg" ? 'Z' : player.race == "Terran" ? 'T' : player.race == "Protoss" ? 'P' : 'R';
      g_capture.metadata.matchup += (g_capture.metadata.matchup.empty() ? "" : "v") + std::string(1, letter);
    }
  } else if (bTeams) {
    std::vector<int> teams;
    for (const UploadPlayer &player : g_capture.metadata.players) {
      const int team = gPlayerData[player.slot].nTeam;
      if (std::find(teams.begin(), teams.end(), team) == teams.end())
        teams.push_back(team);
    }
    std::vector<size_t> sizes(teams.size(), 0);
    for (const UploadPlayer &player : g_capture.metadata.players)
      ++sizes[std::find(teams.begin(), teams.end(), gPlayerData[player.slot].nTeam) - teams.begin()];
    for (size_t size : sizes)
      g_capture.metadata.matchup += (g_capture.metadata.matchup.empty() ? "" : "v") + std::to_string(size);
  } else {
    g_capture.metadata.matchup = std::to_string(g_capture.metadata.players.size()) + " players, free for all";
  }
}

void CaptureWrite(const void *pData, size_t bytes, void *pFile)
{
  if (!g_capture.bActive || pFile != g_capture.pFile || bytes > 64 * 1024 * 1024 - g_capture.bytes.size()) {
    if (g_capture.bActive && pFile == g_capture.pFile && bytes > 64 * 1024 * 1024 - g_capture.bytes.size())
      g_capture.bActive = false;
    return;
  }
  const unsigned char *at = static_cast<const unsigned char *>(pData);
  g_capture.bytes.insert(g_capture.bytes.end(), at, at + bytes);
}

void CaptureFinish(void *pFile, bool bSuccess)
{
  if (!g_capture.bActive || pFile != g_capture.pFile) {
    g_capture = Capture();
    return;
  }
  if (bSuccess && !g_capture.bytes.empty()) {
    try {
      Service &s = RequireService();
      char server[2048] = {};
      Endpoint endpoint;
      std::string endpointError;
      const bool bValid = PortSettingString("hyperion", "replayKrakenServer", server, sizeof server) &&
                          ReplayKraken::EndpointParse(server, &endpoint, &endpointError);
      const std::string uuid = ReplayKraken::UuidNew();
      const UploadMetadata metadata = g_capture.metadata;
      const std::vector<unsigned char> bytes = g_capture.bytes;
      {
        std::lock_guard<std::mutex> lock(s.m);
        UploadView preparing;
        preparing.uuid = uuid;
        preparing.sourcePath = "maps\\replays\\LastReplay.rep";
        preparing.serverApi = endpoint.api;
        preparing.bAutomatic = true;
        s.state.uploads.push_back(preparing);
      }
      if (bValid)
        Post(s, [&s, endpoint, uuid, metadata, bytes] {
          CommitUpload(s, endpoint, true, uuid, "maps\\replays\\LastReplay.rep", metadata, "", true, bytes);
        });
      if (server[0])
        Configure(server, true);  // Identity/follow work follows the immutable commit, never precedes it.
    } catch (...) {
      // A disabled or partially initialized service never changes game teardown.
    }
  }
  g_capture = Capture();
}

size_t Write(const void *pData, size_t size, size_t count, FILE *pFile)
{
  const size_t written = fwrite(pData, size, count, pFile);
  if (written == count)
    CaptureWrite(pData, size * count, pFile);
  return written;
}

bool WorkerStoppedForTest(int64_t timeoutMs)
{
  Service &s = RequireService();
  std::unique_lock<std::mutex> lock(s.m);
  return s.cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return s.bWorkerDone; });
}

uint64_t MainValidationRequests()
{
  return g_validationRequests.load();
}

}  // namespace ReplayOnline
