// port_replayonline.h -- Hyperion's application-owned Replay Kraken service.
// Public functions except the Capture write tee are called on the main thread;
// their file/network work is handed to one bounded native worker.
#ifndef PORT_REPLAYONLINE_H
#define PORT_REPLAYONLINE_H

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#include "port_replaykraken.h"
#include "port_replaylibrary.h"

namespace ReplayOnline {

enum class UploadPhase { Preparing, Disabled, Queued, Uploading, RetryWaiting, Uploaded, Failed, Conflict, Unsupported };
enum class DownloadPhase { Queued, Downloading, Validating, Ready, Failed };

struct UploadView {
  std::string uuid;
  std::string contentId;
  std::string sourcePath;
  std::string serverApi;
  std::string account;
  UploadPhase phase = UploadPhase::Preparing;
  std::string error;
  unsigned attempts = 0;
  bool bAutomatic = false;
  bool bCommitted = false;
  int64_t nextRetryAt = 0;
};

struct DownloadView {
  std::string serverApi;
  std::string uuid;
  DownloadPhase phase = DownloadPhase::Queued;
  std::string error;
  std::string tempPath;
  std::string path;
  std::string contentId;
  std::vector<unsigned char> bytes;  // Immutable candidate for main-thread validation only.
};

struct ListView {
  std::string serverApi;
  ReplayKraken::Query query;
  ReplayKraken::MatchPage page;
  bool bLoading = false;
  bool bPaginating = false;
  bool bStale = false;
  bool bOffline = false;
  bool bUnauthorized = false;
  std::string error;
  int64_t cachedAt = 0;
};

struct State {
  bool bAvailable = false;
  bool bTokenPresent = false;
  bool bAutomatic = false;
  bool bWorkerBusy = false;
  std::string serverInput;
  std::string serverApi;
  std::string account;
  std::string lastError;
  ListView list;
  std::vector<std::string> following;
  std::vector<UploadView> uploads;
  std::vector<DownloadView> downloads;
};

// Start once paths exist. The state object is process-lifetime so a no-server or
// partially initialized shutdown is safe even when ExitProcess skips destructors.
void Start(const std::string &dataFolder, const std::string &programFolder);
void Stop();  // Cancels transport and returns; committed jobs resume next launch.
void Configure(const std::string &serverText, bool bAutomatic);
void SetToken(const std::string &token);  // Empty clears only this server's private token.
State Snapshot();

void Fetch(const ReplayKraken::Query &query, bool bReset);
void LoadMore();
void Follow(const std::string &player, bool bFollow);
void Download(const ReplayKraken::Match &match);
// Main-thread Hyperion validation has completed. The worker performs the atomic
// install only when bValid is true; an incompatible-but-valid replay is installed.
void DownloadValidated(const std::string &serverApi, const std::string &uuid, bool bValid,
                       const std::string &validationError);

std::string Submit(const std::string &sourcePath, const ReplayKraken::UploadMetadata &metadata,
                   const std::string &contentId, bool bAutomatic);
void Retry(const std::string &uuid);
void Discard(const std::string &uuid);  // Explicit terminal-job removal; never automatic for pending work.
void LocalRenamed(const std::string &oldPath, const std::string &newPath);
void LocalDeleted(const std::string &path);

ReplayLibrary::Data LibrarySnapshot();
void SaveLibrary(const ReplayLibrary::Data &data);  // Serialized on main, written on the worker.
std::string ExportLibrary(const ReplayLibrary::ExportSelection &selection, const std::string &path);
void ImportLibrary(const std::string &path);

// Game-writer capture. Begin/Header/Finish run at the existing end-of-game write,
// never during a simulation frame. Write is used by ReplayHxWrite's direct FILE
// writes; pfwrite calls CaptureWrite after its successful write.
bool CaptureAutomaticEligible();
void CaptureBegin(void *pFile);
void CaptureHeader(const void *pHeader, size_t bytes);
void CaptureWrite(const void *pData, size_t bytes, void *pFile);
void CaptureFinish(void *pFile, bool bSuccess);
size_t Write(const void *pData, size_t size, size_t count, FILE *pFile);

// Test/diagnostics hooks: no game state, bounded waits, and a monotonic count of
// engine-bound validation requests handed back to the main thread.
bool WorkerStoppedForTest(int64_t timeoutMs);
uint64_t MainValidationRequests();

}  // namespace ReplayOnline

#endif
