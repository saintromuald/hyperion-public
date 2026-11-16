// port_pluto_runtime.cpp -- official Pluto runtime resolution, verification and repair.
//
// The three closed-source payloads are fetched directly from tscmoo/pluto's pinned v2.0 release.
// Apple Silicon first installs the pinned signed MetalSharp Wine dependency when needed. Nothing is
// mirrored or bundled here. Downloads land in owned staging, their pinned digests are checked, and
// only the app-managed Wine bundle and three known payload paths are replaced. The bridge, logs,
// configuration, bandit data, and every other Wine installation are left alone.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <new>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "port_pluto_runtime.h"
#include "port_zip.h"
#include "../platform/port_log.h"
#include "../platform/port_paths.h"
#include "../platform/port_posix.h"
#include "../../../tools/pluto-bridge/official-dll.h"
#ifndef SB_NETCODE_OFF
#include "../../../tools/rallypoint2/bridge/pluto_download.h"
#endif
#if !defined(_WIN32)
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace {

namespace fs = std::filesystem;

const PortLogger Log("hyperion_pluto_install.log", PortLogger::SHARED);
const char kCancelled[] = "Download cancelled.";
const char kReadyMarker[] = ".pluto-runtime-v2.ready";
const uint32_t kPayloadMax = 512u << 20;

struct PayloadSpec {
  std::string name, sha256;
  uint32_t cbSize;  // zero when only the digest pins it
  uint32_t cbMax;
};

struct PlutoRuntimeSpec {
  std::string url, sha256;
  uint64_t cbArchive;
  std::vector<PayloadSpec> payloads;
};

const PlutoRuntimeSpec &OfficialSpec()
{
  static const PlutoRuntimeSpec spec = {
      "https://github.com/tscmoo/pluto/releases/download/v2.0/pluto-v2.0.zip",
      "17544fc3ab32e3e2823646887e021cf998362348d73740817e94d1a78aa6209d",
      309686477,
      {{"pluto.dll", pluto_bridge::OfficialPlutoSha256(), 0, kPayloadMax},
       {"pluto/pluto_infer.exe", "3f2814a51034b9817b396e52b38d30341ede506a2baedc7cbc11cb6ebcece2cc", 0,
        kPayloadMax},
       {"pluto/pluto_weights.bin", "c53a559b4910777dc0e1c561c0d89d243091f465d8e9fc0f5f72e56597067d86",
        347935248, kPayloadMax}}};
  return spec;
}

const PlutoRuntimeSpec &OfficialMetalSharpSpec()
{
  static const PlutoRuntimeSpec spec = {
      "https://github.com/metalsharp/MetalSharp-Wine/releases/download/v6.0/MetalSharp-Wine-6.0-arm64.zip",
      "25f7dff91000c03c835b800980878acb12c1f952a4b7853f77de7f21fb87046f", 1356725682, {}};
  return spec;
}

std::string RuntimeFolderFor(const char *pszOverride, const char *pszData)
{
  if (pszOverride != NULL && pszOverride[0] != 0)
    return pszOverride;
  std::string folder = pszData != NULL ? pszData : "";
  if (!folder.empty() && folder.back() != '/' && folder.back() != '\\')
    folder += '/';
  return folder + "pluto";
}

std::string JoinPath(const std::string &folder, const std::string &name)
{
  if (folder.empty() || folder.back() == '/' || folder.back() == '\\')
    return folder + name;
  return folder + '/' + name;
}

std::string ParentPath(const std::string &path)
{
  std::string trimmed = path;
  while (trimmed.size() > 1 && (trimmed.back() == '/' || trimmed.back() == '\\'))
    trimmed.pop_back();
  const size_t at = trimmed.find_last_of("/\\");
  if (at == std::string::npos)
    return ".";
  if (at == 2 && trimmed.size() >= 3 && trimmed[1] == ':')
    return trimmed.substr(0, 3);
  if (at == 0)
    return trimmed.substr(0, 1);
  return trimmed.substr(0, at);
}

fs::path NativePath(const std::string &path)
{
  // PortNativePath's ring buffer is copied before any subsequent path conversion.
  return fs::path(PortNativePath(path.c_str()));
}

bool PathExists(const std::string &path, bool *pExists, std::string *pError)
{
  std::error_code ec;
  *pExists = fs::exists(NativePath(path), ec);
  if (ec) {
    *pError = "Could not inspect " + path + ": " + ec.message();
    return false;
  }
  return true;
}

bool MakeDirectories(const std::string &path, std::string *pError)
{
  std::error_code ec;
  fs::create_directories(NativePath(path), ec);
  if (ec) {
    *pError = "Could not create " + path + ": " + ec.message();
    return false;
  }
  return true;
}

bool RemovePath(const std::string &path, std::string *pError = NULL)
{
  std::error_code ec;
  fs::remove(NativePath(path), ec);
  if (ec) {
    if (pError != NULL)
      *pError = "Could not remove " + path + ": " + ec.message();
    return false;
  }
  return true;
}

bool MoveAtomic(const std::string &from, const std::string &to, std::string *pError)
{
  // Used when the target is absent (notably the final ready marker).
  std::error_code ec;
  fs::rename(NativePath(from), NativePath(to), ec);
  if (ec) {
    *pError = "Could not move " + from + " into place: " + ec.message();
    return false;
  }
  return true;
}

bool ReplaceAtomic(const std::string &from, const std::string &to, std::string *pError)
{
#if defined(_MSC_VER)
  if (MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return true;
  *pError = "Could not atomically replace " + to + ".";
  return false;
#else
  // POSIX rename replaces a file atomically when staging and target share a filesystem.
  std::error_code ec;
  fs::rename(NativePath(from), NativePath(to), ec);
  if (!ec)
    return true;
  *pError = "Could not atomically replace " + to + ": " + ec.message();
  return false;
#endif
}

bool CopyWholeFile(const std::string &from, const std::string &to, std::string *pError)
{
  pError->clear();
  FILE *input = fopen(from.c_str(), "rb");
  if (input == NULL) {
    *pError = "Could not back up " + from + ".";
    return false;
  }
  FILE *output = fopen(to.c_str(), "wb");
  if (output == NULL) {
    fclose(input);
    *pError = "Could not create a rollback backup for " + from + ".";
    return false;
  }
  bool ok = true;
  uint8_t block[65536];
  for (;;) {
    const size_t got = fread(block, 1, sizeof block, input);
    if (got != 0 && fwrite(block, 1, got, output) != got) {
      ok = false;
      break;
    }
    if (got != sizeof block) {
      ok = ferror(input) == 0;
      break;
    }
  }
  ok = fclose(input) == 0 && ok;
  ok = fclose(output) == 0 && ok;
#if !defined(_WIN32)
  if (ok) {
    // Content-only copies lose the execute bit. Preserve the source mode so a rollback restores
    // both the old bytes and its launch/readiness permissions.
    const fs::perms mode = fs::status(NativePath(from)).permissions();
    std::error_code ec;
    fs::permissions(NativePath(to), mode, fs::perm_options::replace, ec);
    ok = !ec;
    if (ec)
      *pError = "Could not preserve the rollback backup's execute mode: " + ec.message();
  }
#endif
  if (!ok) {
    RemovePath(to);
    if (pError->empty())
      *pError = "Could not finish the rollback backup for " + from + ".";
  }
  return ok;
}

std::string Sha256(const uint8_t *pData, size_t cb)
{
  pluto_bridge::Sha256 hash;
  if (cb != 0)
    hash.Update(pData, cb);
  char hex[65];
  hash.Finish(hex);
  return hex;
}

bool HashFile(const std::string &path, std::string *pHex, std::string *pError)
{
  char hex[65];
  if (!pluto_bridge::HashFileSha256(path.c_str(), hex)) {
    *pError = "Could not read " + path + " to verify its SHA-256.";
    return false;
  }
  *pHex = hex;
  return true;
}

bool BridgeReady(const std::string &folder, std::string *pError)
{
  const std::string path = JoinPath(folder, "pluto-bridge.exe");
  std::error_code ec;
  const bool regular = fs::is_regular_file(NativePath(path), ec);
  if (ec || !regular || fs::file_size(NativePath(path), ec) == 0 || ec) {
    *pError = "Hyperion's Pluto bridge is missing or invalid. Repair or reinstall Hyperion to restore "
              "pluto-bridge.exe; the official Pluto download does not include it.";
    return false;
  }
  return true;
}

bool RuntimeReady(const PlutoRuntimeSpec &spec, const std::string &folder, std::string *pError)
{
  if (!BridgeReady(folder, pError))
    return false;
  for (const PayloadSpec &payload : spec.payloads) {
    const std::string path = JoinPath(folder, payload.name);
    if (payload.cbSize != 0) {
      std::error_code ec;
      const uint64_t size = fs::file_size(NativePath(path), ec);
      if (ec || size != payload.cbSize) {
        *pError = "The installed " + payload.name + " is missing or has the wrong size.";
        return false;
      }
    }
    std::string actual;
    if (!HashFile(path, &actual, pError))
      return false;
    if (actual != payload.sha256) {
      *pError = "The installed " + payload.name + " failed its SHA-256 check.";
      return false;
    }
#if !defined(_WIN32)
    if (payload.name == "pluto/pluto_infer.exe" && access(NativePath(path).c_str(), X_OK) != 0) {
      *pError = "Pluto inference is not executable. Repair the runtime to restore execute access to "
                "pluto/pluto_infer.exe.";
      return false;
    }
#endif
  }
  return true;
}

bool WriteWholeFile(const std::string &path, const std::vector<uint8_t> &bytes, std::string *pError)
{
  FILE *file = fopen(path.c_str(), "wb");
  if (file == NULL) {
    *pError = "Could not create " + path + ".";
    return false;
  }
  const bool wrote = bytes.empty() || fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
  const bool closed = fclose(file) == 0;
  if (!wrote || !closed) {
    *pError = "Could not finish writing " + path + ".";
    RemovePath(path);
    return false;
  }
  return true;
}

bool SetStagedPayloadAccess(const std::string &name, const std::string &path, std::string *pError)
{
#if !defined(_WIN32)
  if (name == "pluto/pluto_infer.exe") {
    std::error_code ec;
    fs::permissions(NativePath(path),
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
    if (ec) {
      *pError = "Could not make the staged Pluto inference engine executable: " + ec.message();
      return false;
    }
  }
#else
  (void)name;
  (void)path;
#endif
  return true;
}

bool ReadWholeFile(const std::string &path, uint64_t cbExpected, std::vector<uint8_t> *pBytes,
                   std::string *pError)
{
  FILE *file = fopen(path.c_str(), "rb");
  if (file == NULL) {
    *pError = "Could not reopen the downloaded archive.";
    return false;
  }
  bool ok = fseek(file, 0, SEEK_END) == 0;
  const long size = ok ? ftell(file) : -1;
  ok = ok && size >= 0 && static_cast<uint64_t>(size) == cbExpected && fseek(file, 0, SEEK_SET) == 0;
  if (ok) {
    try {
      pBytes->resize(static_cast<size_t>(size));
    } catch (const std::bad_alloc &) {
      ok = false;
    }
  }
  if (ok && !pBytes->empty())
    ok = fread(pBytes->data(), 1, pBytes->size(), file) == pBytes->size();
  ok = fclose(file) == 0 && ok;
  if (!ok) {
    pBytes->clear();
    *pError = "The downloaded archive was truncated or could not be read.";
  }
  return ok;
}

using CancelFn = std::function<bool()>;
using ProgressFn = std::function<void(uint64_t, uint64_t, const char *)>;

bool Cancelled(const CancelFn &cancel, std::string *pError)
{
  if (!cancel || !cancel())
    return false;
  *pError = kCancelled;
  return true;
}

class ArchiveSink {
public:
  ArchiveSink(const std::string &path, uint64_t cbExpected, CancelFn cancel, ProgressFn progress,
              const char *stage = "downloading")
      : m_expected(cbExpected), m_cancel(std::move(cancel)), m_progress(std::move(progress)), m_stage(stage)
  {
    m_file = fopen(path.c_str(), "wb");
  }

  ~ArchiveSink()
  {
    if (m_file != NULL)
      fclose(m_file);
  }

  bool Open(std::string *pError) const
  {
    if (m_file != NULL)
      return true;
    *pError = "Could not create the temporary download file.";
    return false;
  }

  bool Write(const uint8_t *pData, size_t cb)
  {
    if (m_file == NULL || m_failed || (m_cancel && m_cancel()))
      return false;
    if (cb > m_expected || m_received + cb > m_expected) {
      m_failed = true;
      return false;
    }
    if (cb != 0 && fwrite(pData, 1, cb, m_file) != cb) {
      m_failed = true;
      return false;
    }
    if (cb != 0)
      m_hash.Update(pData, cb);
    m_received += cb;
    if (m_progress)
      m_progress(m_received, m_expected, m_stage.c_str());
    return true;
  }

  bool Finish(std::string *pError)
  {
    if (m_file == NULL) {
      *pError = "The temporary download file is not open.";
      return false;
    }
    const bool closed = fclose(m_file) == 0;
    m_file = NULL;
    if (m_failed || !closed || m_received != m_expected) {
      *pError = m_failed ? "The downloaded archive could not be written completely."
                         : "The downloaded archive ended at the wrong size.";
      return false;
    }
    char hex[65];
    m_hash.Finish(hex);
    m_sha256 = hex;
    return true;
  }

  bool IsCancelled() const { return m_cancel && m_cancel(); }
  const std::string &sha256() const { return m_sha256; }

private:
  uint64_t m_expected, m_received = 0;
  CancelFn m_cancel;
  ProgressFn m_progress;
  std::string m_stage;
  FILE *m_file = NULL;
  bool m_failed = false;
  pluto_bridge::Sha256 m_hash;
  std::string m_sha256;
};

#ifndef SB_NETCODE_OFF
bool RustWrite(void *context, const uint8_t *data, size_t size)
{
  return static_cast<ArchiveSink *>(context)->Write(data, size);
}

bool RustCancel(void *context)
{
  return static_cast<ArchiveSink *>(context)->IsCancelled();
}
#endif

using TransportFn = std::function<bool(const PlutoRuntimeSpec &, ArchiveSink &, std::string *)>;

bool RustTransport(const PlutoRuntimeSpec &spec, ArchiveSink &sink, std::string *pError)
{
#ifdef SB_NETCODE_OFF
  (void)spec;
  (void)sink;
  *pError = "This build has no HTTPS downloader (built with --no-sb).";
  return false;
#else
  char error[512] = "";
  const int code = rp_https_download(spec.url.c_str(), spec.cbArchive, &sink, RustWrite, NULL,
                                     RustCancel, error, sizeof error);
  if (code == RP_DOWNLOAD_OK)
    return true;
  *pError = code == RP_DOWNLOAD_ERR_CANCELLED ? kCancelled : error[0] != 0 ? error : "HTTPS download failed.";
  return false;
#endif
}

bool SafeEntryName(const std::string &name)
{
  if (name.empty() || name.size() > 1024 || name[0] == '/' || name[0] == '\\' ||
      (name.size() >= 2 && name[1] == ':'))
    return false;
  if (name.find('\0') != std::string::npos || name.find('\\') != std::string::npos)
    return false;
  size_t at = 0;
  while (at < name.size()) {
    const size_t slash = name.find('/', at);
    const std::string part = name.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
    if (part == "." || part == "..")
      return false;
    if (slash == std::string::npos)
      break;
    at = slash + 1;
  }
  return true;
}

bool SelectPayloads(const std::vector<PortZipEntry> &entries, const PlutoRuntimeSpec &spec,
                    std::vector<PortZipEntry> *pSelected, std::string *pError)
{
  std::set<std::string> seen;
  for (const PortZipEntry &entry : entries) {
    if (!SafeEntryName(entry.name)) {
      *pError = "The archive contains an unsafe path: " + entry.name;
      return false;
    }
    if (!seen.insert(entry.name).second) {
      *pError = "The archive contains a duplicate entry: " + entry.name;
      return false;
    }
  }
  for (const PayloadSpec &payload : spec.payloads) {
    auto found = std::find_if(entries.begin(), entries.end(),
                              [&](const PortZipEntry &entry) { return entry.name == payload.name; });
    if (found == entries.end()) {
      *pError = "The archive is missing " + payload.name + ".";
      return false;
    }
    if ((payload.cbSize != 0 && found->cbSize != payload.cbSize) || found->cbSize > payload.cbMax) {
      *pError = "The archive's " + payload.name + " exceeds its permitted size.";
      return false;
    }
    pSelected->push_back(*found);
  }
  return true;
}

struct CommitRecord {
  std::string staged, target, backup;
  bool hadOld = false, installed = false;
};

void Rollback(std::vector<CommitRecord> &records)
{
  for (auto it = records.rbegin(); it != records.rend(); ++it) {
    std::string ignored;
    if (it->installed && !it->hadOld)
      RemovePath(it->target, &ignored);
    if (it->installed && it->hadOld) {
      if (ReplaceAtomic(it->backup, it->target, &ignored)) {
        RemovePath(it->backup, &ignored);
      } else {
        Log("Pluto install: rollback could not restore %s; its previous file remains at %s", it->target,
            it->backup);
      }
    }
    it->installed = false;
    it->hadOld = false;
  }
}

bool CommitPayloads(const std::string &stage, const std::string &folder, const PlutoRuntimeSpec &spec,
                    std::vector<CommitRecord> *pRecords, std::string *pError)
{
  RemovePath(JoinPath(folder, kReadyMarker));
  for (size_t i = 0; i < spec.payloads.size(); ++i) {
    const std::string &name = spec.payloads[i].name;
    CommitRecord record = {JoinPath(JoinPath(stage, "files"), name), JoinPath(folder, name),
                           JoinPath(JoinPath(stage, "backup"), name)};
    if (!MakeDirectories(ParentPath(record.target), pError) ||
        !MakeDirectories(ParentPath(record.backup), pError)) {
      Rollback(*pRecords);
      return false;
    }
    bool exists = false;
    if (!PathExists(record.target, &exists, pError)) {
      Rollback(*pRecords);
      return false;
    }
    record.hadOld = exists;
    // Copy, rather than move, the old payload: the live target remains present until the staged
    // file atomically replaces it, and the backup permits rollback after later files fail.
    if (exists && !CopyWholeFile(record.target, record.backup, pError)) {
      Rollback(*pRecords);
      return false;
    }
    if (!ReplaceAtomic(record.staged, record.target, pError)) {
      Rollback(*pRecords);
      return false;
    }
    record.installed = true;
    pRecords->push_back(record);
  }
  return true;
}

void CleanupStage(const std::string &stage)
{
  std::error_code ec;
  fs::remove_all(NativePath(stage), ec);
  if (ec)
    Log("Pluto install: staging cleanup failed: %s", ec.message());
}

bool InstallRuntime(const PlutoRuntimeSpec &spec, const std::string &folder, const TransportFn &transport,
                    const CancelFn &cancel, const ProgressFn &progress, std::string *pError)
{
  if (folder.empty()) {
    *pError = "The Pluto runtime folder is empty.";
    Log("Pluto install: %s", *pError);
    return false;
  }
  Log("Pluto install: official download begins: %s (%llu bytes)", spec.url,
      static_cast<unsigned long long>(spec.cbArchive));
  const std::string parent = ParentPath(folder);
  if (!MakeDirectories(parent, pError))
    return false;
  static std::atomic<unsigned> s_serial{0};
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::string stage = JoinPath(parent, ".pluto-install-" + std::to_string(ticks) + "-" +
                                                std::to_string(++s_serial));
  if (!MakeDirectories(JoinPath(stage, "files"), pError))
    return false;
  bool ok = false;
  // Every failure before commit leaves the existing runtime wholly untouched. CommitPayloads and
  // the checks below roll back the three known payloads if any part of a replacement fails.
  do {
    ArchiveSink sink(JoinPath(stage, "official.zip"), spec.cbArchive, cancel, progress);
    if (!sink.Open(pError))
      break;
    if (Cancelled(cancel, pError) || !transport(spec, sink, pError) || !sink.Finish(pError))
      break;
    if (sink.sha256() != spec.sha256) {
      *pError = "The downloaded ZIP failed its pinned SHA-256 check.";
      break;
    }
    if (Cancelled(cancel, pError))
      break;
    if (progress)
      progress(spec.cbArchive, spec.cbArchive, "verifying");
    std::vector<uint8_t> zip;
    if (!ReadWholeFile(JoinPath(stage, "official.zip"), spec.cbArchive, &zip, pError))
      break;
    std::vector<PortZipEntry> entries, selected;
    if (!PortZipDirectory(zip, &entries)) {
      *pError = "The official download is not a readable ZIP archive.";
      break;
    }
    if (!SelectPayloads(entries, spec, &selected, pError))
      break;
    for (size_t i = 0; i < selected.size(); ++i) {
      if (Cancelled(cancel, pError))
        break;
      std::vector<uint8_t> bytes;
      try {
        if (!PortZipRead(zip, selected[i], &bytes, spec.payloads[i].cbMax)) {
          *pError = "Could not unpack " + spec.payloads[i].name + " within its size limit.";
          break;
        }
      } catch (const std::bad_alloc &) {
        *pError = "Not enough memory to unpack " + spec.payloads[i].name + ".";
        break;
      }
      if (Sha256(bytes.data(), bytes.size()) != spec.payloads[i].sha256) {
        *pError = "The archive's " + spec.payloads[i].name + " failed its pinned SHA-256 check.";
        break;
      }
      const std::string staged = JoinPath(JoinPath(stage, "files"), spec.payloads[i].name);
      if (!MakeDirectories(ParentPath(staged), pError) || !WriteWholeFile(staged, bytes, pError) ||
          !SetStagedPayloadAccess(spec.payloads[i].name, staged, pError))
        break;
      if (i + 1 == selected.size()) {
        ok = true;
        break;
      }
    }
  } while (false);
  if (!ok) {
    Log("Pluto install: %s", *pError);
    CleanupStage(stage);
    return false;
  }
  if (Cancelled(cancel, pError)) {
    CleanupStage(stage);
    return false;
  }
  if (!MakeDirectories(folder, pError)) {
    CleanupStage(stage);
    return false;
  }
  if (progress)
    progress(spec.cbArchive, spec.cbArchive, "installing");
  std::vector<CommitRecord> committed;
  ok = CommitPayloads(stage, folder, spec, &committed, pError);
  if (ok && !RuntimeReady(spec, folder, pError)) {
    *pError = "Installed runtime verification failed: " + *pError;
    ok = false;
  }
  if (ok && Cancelled(cancel, pError))
    ok = false;
  if (ok) {
    const std::string markerText = spec.sha256 + "\n";
    const std::vector<uint8_t> bytes(markerText.begin(), markerText.end());
    const std::string stagedMarker = JoinPath(stage, kReadyMarker);
    ok = WriteWholeFile(stagedMarker, bytes, pError) &&
         MoveAtomic(stagedMarker, JoinPath(folder, kReadyMarker), pError);
  }
  if (!ok) {
    Rollback(committed);
    RemovePath(JoinPath(folder, kReadyMarker));
    Log("Pluto install: replacement failed; previous payloads restored where present: %s", *pError);
    bool backupRemain = false;
    for (const CommitRecord &record : committed) {
      std::error_code ec;
      backupRemain = backupRemain || fs::exists(NativePath(record.backup), ec);
    }
    // A backup that could not be restored is recovery data, never cleanup garbage.
    if (!backupRemain)
      CleanupStage(stage);
    else
      Log("Pluto install: staging retained for recovery at %s", stage);
    return false;
  }
  CleanupStage(stage);  // also discards the superseded official payloads, and no other user data
  Log("Pluto install: verified official runtime installed at %s", folder);
  return true;
}

struct Installer {
  std::mutex lock;
  std::thread worker;
  std::atomic<bool> cancel{false};
  PortPlutoInstallStatus status{PORT_PLUTO_INSTALL_IDLE, 0, OfficialSpec().cbArchive, "", ""};
};

Installer &GlobalInstaller()
{
  // Process-lifetime ownership: a setup window may close while its worker finishes.
  static Installer *installer = new Installer;
  return *installer;
}

void SetPhase(PortPlutoInstallPhase phase, const std::string &error = "")
{
  Installer &installer = GlobalInstaller();
  std::lock_guard<std::mutex> hold(installer.lock);
  installer.status.phase = phase;
  installer.status.error = error;
  if (phase != PORT_PLUTO_INSTALL_RUNNING)
    installer.status.stage.clear();
}

void Progress(uint64_t received, uint64_t total, const char *stage)
{
  Installer &installer = GlobalInstaller();
  std::lock_guard<std::mutex> hold(installer.lock);
  installer.status.cbReceived = received;
  installer.status.cbTotal = total;
  installer.status.stage = stage;
}

bool GlobalCancelled()
{
  return GlobalInstaller().cancel.load();
}

#if !defined(_WIN32)
std::string Trimmed(const std::string &text)
{
  const size_t begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos)
    return "";
  const size_t end = text.find_last_not_of(" \t\r\n");
  return text.substr(begin, end - begin + 1);
}

bool RunCapturedProcess(const std::string &program, const std::vector<std::string> &args,
                        bool advertiseRosettaAvx, std::string *pOutput)
{
  int output[2] = {-1, -1};
  if (pipe(output) != 0)
    return false;
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    close(output[0]);
    close(output[1]);
    return false;
  }
  const bool actionsOk = posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO) == 0 &&
                         posix_spawn_file_actions_adddup2(&actions, output[1], STDERR_FILENO) == 0 &&
                         posix_spawn_file_actions_addclose(&actions, output[0]) == 0 &&
                         posix_spawn_file_actions_addclose(&actions, output[1]) == 0;
  std::vector<char *> argv;
  for (const std::string &arg : args)
    argv.push_back(const_cast<char *>(arg.c_str()));
  argv.push_back(NULL);
  std::vector<std::string> environment;
  std::vector<char *> envp;
  char **childEnvironment = environ;
  if (advertiseRosettaAvx) {
    for (char **current = environ; current != NULL && *current != NULL; ++current) {
      if (strncmp(*current, "ROSETTA_ADVERTISE_AVX=", 22) != 0)
        environment.push_back(*current);
    }
    environment.push_back("ROSETTA_ADVERTISE_AVX=1");
    for (std::string &entry : environment)
      envp.push_back(&entry[0]);
    envp.push_back(NULL);
    childEnvironment = envp.data();
  }
  pid_t pid = -1;
  const int spawned = actionsOk ? posix_spawn(&pid, program.c_str(), &actions, NULL, argv.data(), childEnvironment)
                                : EINVAL;
  posix_spawn_file_actions_destroy(&actions);
  close(output[1]);
  if (spawned != 0) {
    close(output[0]);
    return false;
  }
  std::string text;
  for (;;) {
    char block[256];
    const ssize_t got = read(output[0], block, sizeof block);
    if (got > 0) {
      if (text.size() < 4096)
        text.append(block, std::min(static_cast<size_t>(got), 4096 - text.size()));
    } else if (got < 0 && errno == EINTR) {
      continue;
    } else {
      break;
    }
  }
  close(output[0]);
  int status = 0;
  pid_t waited;
  do {
    waited = waitpid(pid, &status, 0);
  } while (waited < 0 && errno == EINTR);
  *pOutput = text;
  return waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Acceptance can use pInterrupted to distinguish an actual SIGKILL interruption from a tool that
// exited normally before cancellation. Normal installer callers use the wrapper below.
bool RunCancellableProcessObserved(const std::string &program, const std::vector<std::string> &args,
                                   const CancelFn &cancel, std::string *pOutput, bool *pInterrupted)
{
  if (pInterrupted != NULL)
    *pInterrupted = false;
  int output[2] = {-1, -1};
  if (pipe(output) != 0)
    return false;
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    close(output[0]);
    close(output[1]);
    return false;
  }
  const bool actionsOk = posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO) == 0 &&
                         posix_spawn_file_actions_adddup2(&actions, output[1], STDERR_FILENO) == 0 &&
                         posix_spawn_file_actions_addclose(&actions, output[0]) == 0 &&
                         posix_spawn_file_actions_addclose(&actions, output[1]) == 0;
  posix_spawnattr_t attributes;
  const bool attributesInitialized = posix_spawnattr_init(&attributes) == 0;
  const bool attributesOk = attributesInitialized &&
                            posix_spawnattr_setpgroup(&attributes, 0) == 0 &&
                            posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP) == 0;
  std::vector<char *> argv;
  for (const std::string &arg : args)
    argv.push_back(const_cast<char *>(arg.c_str()));
  argv.push_back(NULL);
  pid_t pid = -1;
  const int spawned = actionsOk && attributesOk
                          ? posix_spawn(&pid, program.c_str(), &actions, &attributes, argv.data(), environ)
                          : EINVAL;
  if (attributesInitialized)
    posix_spawnattr_destroy(&attributes);
  posix_spawn_file_actions_destroy(&actions);
  close(output[1]);
  if (spawned != 0) {
    close(output[0]);
    return false;
  }

  const bool nonblocking = fcntl(output[0], F_SETFL, fcntl(output[0], F_GETFL, 0) | O_NONBLOCK) >= 0;
  if (!nonblocking) {
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    int ignored = 0;
    while (waitpid(pid, &ignored, 0) < 0 && errno == EINTR) {
    }
    close(output[0]);
    return false;
  }

  std::string text;
  int status = 0;
  pid_t waited = 0;
  bool killed = false;
  while (waited == 0) {
    for (;;) {
      char block[256];
      const ssize_t got = read(output[0], block, sizeof block);
      if (got > 0) {
        if (text.size() < 4096)
          text.append(block, std::min(static_cast<size_t>(got), 4096 - text.size()));
      } else if (got < 0 && errno == EINTR) {
        continue;
      } else {
        break;
      }
    }
    pid_t result;
    do {
      result = waitpid(pid, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result != 0) {
      waited = result;
      break;
    }
    if (!killed && cancel && cancel()) {
      // The tool and any children it spawned share this owned process group. They only touch the
      // dependency's staging directory, never the active app or Pluto runtime.
      kill(-pid, SIGKILL);
      kill(pid, SIGKILL);
      killed = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  close(output[0]);
  *pOutput = text;
  if (pInterrupted != NULL)
    *pInterrupted = killed && waited == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL;
  return !killed && waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool RunCancellableProcess(const std::string &program, const std::vector<std::string> &args,
                           const CancelFn &cancel, std::string *pOutput)
{
  return RunCancellableProcessObserved(program, args, cancel, pOutput, NULL);
}

bool MacOsVersionAtLeast(const std::string &text, long minimumMajor, long minimumMinor)
{
  const std::string version = Trimmed(text);
  char *end = NULL;
  const long major = strtol(version.c_str(), &end, 10);
  if (end == version.c_str())
    return false;
  long minor = 0;
  if (*end == '.') {
    const char *minorStart = end + 1;
    minor = strtol(minorStart, &end, 10);
    if (end == minorStart)
      return false;
  }
  if (*end != 0 && *end != '.')
    return false;
  return major > minimumMajor || (major == minimumMajor && minor >= minimumMinor);
}

#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
bool MetalSharpMacOsSupported()
{
  static const bool available = [] {
    std::string output;
    return RunCapturedProcess("/usr/sbin/sysctl", {"sysctl", "-n", "kern.osproductversion"}, false,
                              &output) &&
           MacOsVersionAtLeast(output, 26, 4);
  }();
  return available;
}
#endif

std::string AbsoluteExecutable(const std::string &candidate)
{
  std::error_code ec;
  fs::path path(candidate);
  if (!fs::is_regular_file(path, ec) || ec || access(candidate.c_str(), X_OK) != 0)
    return "";
  if (!path.is_absolute())
    path = fs::absolute(path, ec);
  return ec ? "" : path.lexically_normal().string();
}

const char kMetalSharpWinePath[] = "/Applications/MetalSharp-Wine.app/Contents/MacOS/wine";
const char kAppManagedMetalSharpWineAppRelativePath[] =
    "Library/Application Support/Hyperion/MetalSharp-Wine-6.0.app";
const char kAppManagedMetalSharpWineRelativePath[] =
    "Library/Application Support/Hyperion/MetalSharp-Wine-6.0.app/Contents/MacOS/wine";

const char *ProbeMetalSharpWine(const std::string &wine)
{
  if (AbsoluteExecutable(wine).empty())
    return "The selected MetalSharp Wine path is not a regular executable file.";
  std::string version;
  const bool wine11 = RunCapturedProcess(wine, {wine, "--version"}, false, &version);
  const std::string identity = Trimmed(version);
  if (!wine11 || identity.rfind("wine-11.", 0) != 0 || identity.find("MetalSharp Wine") == std::string::npos)
    return "Pluto inference on Apple Silicon needs Wine 11 with MetalSharp Wine identity. Generic WineHQ and Rosetta are not compatible; select the official MetalSharp Wine app's Contents/MacOS/wine executable.";

  const fs::path executable(wine);
  const fs::path contents = executable.parent_path().parent_path();
  if (executable.filename() != "wine" || executable.parent_path().filename() != "MacOS" ||
      contents.filename() != "Contents" || contents.parent_path().extension() != ".app")
    return "HYPERION_PLUTO_WINE must name Contents/MacOS/wine inside a MetalSharp Wine .app bundle.";

  const fs::path wineFiles = contents / "lib" / "wine";
  std::error_code ec;
  const bool i386 = fs::is_regular_file(wineFiles / "i386-windows" / "ntdll.dll", ec) && !ec;
  const bool runtimeFiles = i386 &&
                            !AbsoluteExecutable((contents / "MacOS" / "wineserver").string()).empty() &&
                            !AbsoluteExecutable((wineFiles / "aarch64-unix" / "wine").string()).empty();
  if (!runtimeFiles)
    return "The selected MetalSharp Wine app lacks its required wineserver, i386 Windows runtime, or aarch64 Unix loader.";
  if (!fs::is_regular_file(contents / "embedded.provisionprofile", ec) || ec)
    return "The selected MetalSharp Wine app lacks Contents/embedded.provisionprofile, which is required for cross-architecture execution.";
  return NULL;
}

const char *ProbeWine11Unified(const std::string &wine)
{
  if (AbsoluteExecutable(wine).empty())
    return "The selected Wine path is not a regular executable file.";
  std::string version;
  if (!RunCapturedProcess(wine, {wine, "--version"}, true, &version) ||
      Trimmed(version).rfind("wine-11.", 0) != 0)
    return "Pluto inference on macOS needs Wine 11; the resolved executable did not report 'wine-11.* --version'.";
  const fs::path root = fs::path(wine).parent_path().parent_path();
  const fs::path wineFiles = root / "lib" / "wine";
  std::error_code ec;
  const bool dualArchitecture =
      fs::is_regular_file(wineFiles / "i386-windows" / "ntdll.dll", ec) && !ec &&
      fs::is_regular_file(wineFiles / "x86_64-windows" / "ntdll.dll", ec) && !ec &&
      !AbsoluteExecutable((wineFiles / "x86_64-unix" / "wine").string()).empty();
  if (!dualArchitecture)
    return "The selected Wine 11 installation lacks its unified i386/x86-64 runtime files.";
  return NULL;
}

const char *Wine11UnifiedError(const std::string &wine)
{
  // Setup surfaces call this repeatedly. Cache per resolved pathname rather than starting Wine for
  // every redraw; changing HYPERION_PLUTO_WINE selects a new probe. Restart after repairing Wine in
  // place.
  static std::mutex lock;
  static std::string cachedPath;
  static const char *cachedError = NULL;
  static bool cached = false;
  std::lock_guard<std::mutex> hold(lock);
  if (cached && cachedPath == wine)
    return cachedError;
  cachedError = ProbeWine11Unified(wine);
  cachedPath = wine;
  cached = true;
  return cachedError;
}

std::string ResolveExecutableOnPath(const std::string &program)
{
  if (program.empty())
    return "";
  if (program.find('/') != std::string::npos)
    return AbsoluteExecutable(program);
  const char *path = getenv("PATH");
  if (path == NULL)
    return "";
  std::string paths(path);
  size_t at = 0;
  while (at <= paths.size()) {
    const size_t colon = paths.find(':', at);
    const std::string folder = paths.substr(at, colon == std::string::npos ? std::string::npos : colon - at);
    const std::string found = AbsoluteExecutable(JoinPath(folder.empty() ? "." : folder, program));
    if (!found.empty())
      return found;
    if (colon == std::string::npos)
      break;
    at = colon + 1;
  }
  return "";
}

std::string AppManagedMetalSharpWineAppPath(const char *home)
{
  if (home == NULL || home[0] == 0)
    return "";
  return JoinPath(home, kAppManagedMetalSharpWineAppRelativePath);
}

std::string AppManagedMetalSharpWinePath(const char *home)
{
  if (home == NULL || home[0] == 0)
    return "";
  return JoinPath(home, kAppManagedMetalSharpWineRelativePath);
}

std::string ResolveAppleSiliconWineExecutable(const char *overridePath, const char *home,
                                              const char *systemPath)
{
  if (overridePath != NULL && overridePath[0] != 0)
    return ResolveExecutableOnPath(overridePath);
  const std::string appManaged = AppManagedMetalSharpWinePath(home);
  if (!appManaged.empty()) {
    const std::string resolved = ResolveExecutableOnPath(appManaged);
    if (!resolved.empty())
      return resolved;
  }
  return ResolveExecutableOnPath(systemPath);
}

struct MetalSharpProbeCache {
  std::mutex lock;
  std::string path;
  const char *error = NULL;
  bool valid = false;
};

MetalSharpProbeCache &GlobalMetalSharpProbeCache()
{
  static MetalSharpProbeCache cache;
  return cache;
}

const char *MetalSharpWineError(const std::string &wine)
{
  // Setup surfaces call this repeatedly, so only a resolved-path change or dependency activation
  // starts another Wine process.
  MetalSharpProbeCache &cache = GlobalMetalSharpProbeCache();
  std::lock_guard<std::mutex> hold(cache.lock);
  if (cache.valid && cache.path == wine)
    return cache.error;
  cache.error = ProbeMetalSharpWine(wine);
  cache.path = wine;
  cache.valid = true;
  return cache.error;
}

void InvalidateMetalSharpWineProbeCache()
{
  MetalSharpProbeCache &cache = GlobalMetalSharpProbeCache();
  std::lock_guard<std::mutex> hold(cache.lock);
  cache.valid = false;
  cache.path.clear();
  cache.error = NULL;
}

using MetalSharpToolFn = std::function<bool(const std::string &, const std::vector<std::string> &,
                                            const CancelFn &, std::string *)>;
using MetalSharpMoveFn = std::function<bool(const std::string &, const std::string &, std::string *)>;

struct MetalSharpTools {
  std::string ditto = "/usr/bin/ditto";
  std::string codesign = "/usr/bin/codesign";
  MetalSharpToolFn run = RunCancellableProcess;
  MetalSharpMoveFn move = MoveAtomic;
};

bool PathEntryExists(const std::string &path, bool *pExists, std::string *pError)
{
  std::error_code ec;
  const fs::file_status status = fs::symlink_status(NativePath(path), ec);
  if (ec == std::errc::no_such_file_or_directory) {
    *pExists = false;
    return true;
  }
  if (ec) {
    *pError = "Could not inspect " + path + ": " + ec.message();
    return false;
  }
  *pExists = fs::exists(status);
  return true;
}

bool MakeFreshDirectory(const std::string &path, std::string *pError)
{
  std::error_code ec;
  const bool created = fs::create_directory(NativePath(path), ec);
  if (ec || !created) {
    *pError = "Could not create the owned fresh staging directory " + path +
              (ec ? ": " + ec.message() : " because it already exists.");
    return false;
  }
  fs::permissions(NativePath(path), fs::perms::owner_all, fs::perm_options::replace, ec);
  if (ec) {
    *pError = "Could not restrict the dependency staging directory: " + ec.message();
    CleanupStage(path);
    return false;
  }
  return true;
}

bool RunMetalSharpTool(const MetalSharpTools &tools, const std::string &program,
                       const std::vector<std::string> &args, const CancelFn &cancel,
                       const char *failure, std::string *pError)
{
  std::string output;
  if (!tools.run || !tools.run(program, args, cancel, &output)) {
    if (Cancelled(cancel, pError))
      return false;
    const std::string detail = Trimmed(output);
    *pError = failure + (detail.empty() ? std::string(".") : ": " + detail);
    return false;
  }
  return !Cancelled(cancel, pError);
}

bool InstallMetalSharpRuntime(const PlutoRuntimeSpec &spec, const std::string &target,
                              const MetalSharpTools &tools, const TransportFn &transport,
                              const CancelFn &cancel, const ProgressFn &progress, std::string *pError,
                              bool forceReinstall = false)
{
  if (target.empty()) {
    *pError = "The app-managed MetalSharp Wine destination is empty.";
    return false;
  }
  if (Cancelled(cancel, pError))
    return false;
  const std::string activeWine = JoinPath(JoinPath(target, "Contents"), "MacOS/wine");
  // Real installer acceptance forces one repair so cancellation can be observed with a valid previous
  // app. Normal production setup always reuses a valid dependency.
  if (!forceReinstall && ProbeMetalSharpWine(activeWine) == NULL)
    return true;

  const std::string parent = ParentPath(target);
  if (!MakeDirectories(parent, pError))
    return false;
  static std::atomic<unsigned> serial{0};
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::string stage = JoinPath(parent, ".metalsharp-install-" + std::to_string(ticks) + "-" +
                                               std::to_string(++serial));
  if (!MakeFreshDirectory(stage, pError))
    return false;
  bool preserveStage = false;
  auto fail = [&] {
    Log("Pluto install: MetalSharp dependency failed: %s", *pError);
    if (!preserveStage)
      CleanupStage(stage);
    else
      Log("Pluto install: dependency staging retained for recovery at %s", stage);
    return false;
  };

  Log("Pluto install: MetalSharp download begins: %s (%llu bytes)", spec.url,
      static_cast<unsigned long long>(spec.cbArchive));
  const std::string archive = JoinPath(stage, "MetalSharp-Wine-6.0-arm64.zip");
  if (progress)
    progress(0, spec.cbArchive, "downloading-wine");
  {
    ArchiveSink sink(archive, spec.cbArchive, cancel, progress, "downloading-wine");
    if (!sink.Open(pError))
      return fail();
    if (Cancelled(cancel, pError) || !transport(spec, sink, pError) || !sink.Finish(pError))
      return fail();
    if (sink.sha256() != spec.sha256) {
      *pError = "The downloaded MetalSharp Wine ZIP failed its pinned SHA-256 check.";
      return fail();
    }
  }
  std::error_code ec;
  const uint64_t archiveSize = fs::file_size(NativePath(archive), ec);
  if (ec || archiveSize != spec.cbArchive) {
    *pError = "The downloaded MetalSharp Wine ZIP does not have its pinned exact byte size.";
    return fail();
  }
  if (Cancelled(cancel, pError))
    return fail();

  const std::string unpacked = JoinPath(stage, "unpacked");
  if (!MakeDirectories(unpacked, pError))
    return fail();
  if (progress)
    progress(spec.cbArchive, spec.cbArchive, "extracting-wine");
  if (!RunMetalSharpTool(tools, tools.ditto,
                         {tools.ditto, "-x", "-k", archive, unpacked}, cancel,
                         "macOS ditto could not extract the signed MetalSharp Wine archive", pError))
    return fail();

  const std::string stagedApp = JoinPath(unpacked, "MetalSharp-Wine.app");
  const fs::file_status appStatus = fs::symlink_status(NativePath(stagedApp), ec);
  if (ec || !fs::is_directory(appStatus) || fs::is_symlink(appStatus)) {
    *pError = "The MetalSharp archive did not produce a real MetalSharp-Wine.app directory in owned staging.";
    return fail();
  }
  if (progress)
    progress(spec.cbArchive, spec.cbArchive, "validating-wine");
  if (!RunMetalSharpTool(tools, tools.codesign,
                         {tools.codesign, "--verify", "--strict", "--deep", stagedApp}, cancel,
                         "MetalSharp Wine failed codesign --verify --strict --deep", pError))
    return fail();
  if (const char *wineError = ProbeMetalSharpWine(JoinPath(JoinPath(stagedApp, "Contents"), "MacOS/wine"))) {
    *pError = std::string("The downloaded MetalSharp Wine app failed pre-activation validation: ") + wineError;
    return fail();
  }
  if (Cancelled(cancel, pError))
    return fail();

  if (progress)
    progress(spec.cbArchive, spec.cbArchive, "activating-wine");
  bool hadOld = false;
  if (!PathEntryExists(target, &hadOld, pError))
    return fail();
  const std::string backup = JoinPath(stage, "previous.app");
  bool oldMoved = false;
  auto restorePrevious = [&] {
    if (!oldMoved)
      return true;
    std::string restoreError;
    if (tools.move && tools.move(backup, target, &restoreError)) {
      oldMoved = false;
      return true;
    }
    preserveStage = true;
    *pError += " The previous app-managed bundle could not be restored and remains at " + backup;
    if (!restoreError.empty())
      *pError += ": " + restoreError;
    return false;
  };

  if (hadOld) {
    if (!tools.move || !tools.move(target, backup, pError))
      return fail();
    oldMoved = true;
  }
  if (Cancelled(cancel, pError)) {
    restorePrevious();
    return fail();
  }
  if (!tools.move || !tools.move(stagedApp, target, pError)) {
    restorePrevious();
    return fail();
  }
  CleanupStage(stage);  // the active app is complete; this also discards the superseded app bundle
  Log("Pluto install: verified MetalSharp Wine installed at %s", target);
  return true;
}

using WineExecutableFn = std::function<std::string()>;
using InferenceCapabilityFn = std::function<const char *()>;

bool InstallAppleSiliconRuntime(const PlutoRuntimeSpec &spec, const PlutoRuntimeSpec &wineSpec,
                                const std::string &folder, const char *home,
                                const MetalSharpTools &tools, const TransportFn &transport,
                                const WineExecutableFn &wineExecutable,
                                const InferenceCapabilityFn &capability, const CancelFn &cancel,
                                const ProgressFn &progress, std::string *pError)
{
  if (Cancelled(cancel, pError))
    return false;
  if (progress)
    progress(0, wineSpec.cbArchive, "checking-wine");
  const std::string selected = wineExecutable ? wineExecutable() : "";
  const bool wineReady = !selected.empty() && MetalSharpWineError(selected) == NULL;
  if (!wineReady) {
    const std::string target = AppManagedMetalSharpWineAppPath(home);
    if (target.empty()) {
      *pError = "Could not determine HOME for the admin-free MetalSharp Wine installation.";
      return false;
    }
    if (!InstallMetalSharpRuntime(wineSpec, target, tools, transport, cancel, progress, pError))
      return false;
    InvalidateMetalSharpWineProbeCache();
  }
  if (Cancelled(cancel, pError))
    return false;
  if (capability) {
    if (const char *capabilityError = capability()) {
      *pError = std::string("Pluto inference preflight still fails after MetalSharp Wine setup; no Pluto "
                            "payloads were replaced: ") +
                capabilityError;
      return false;
    }
  }
  if (progress)
    progress(0, spec.cbArchive, "downloading");
  return InstallRuntime(spec, folder, transport, cancel, progress, pError);
}
#endif

}  // namespace

std::string PortPlutoRuntimeFolder()
{
  return RuntimeFolderFor(getenv("HYPERION_PLUTO_FOLDER"), PortDataFolder());
}

bool PortPlutoRuntimeReady(std::string *pError)
{
  std::string ignored;
  return RuntimeReady(OfficialSpec(), PortPlutoRuntimeFolder(), pError != NULL ? pError : &ignored);
}

std::string PortPlutoWineExecutable()
{
#if defined(_WIN32)
  return "";
#else
  const char *wine = getenv("HYPERION_PLUTO_WINE");
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
  return ResolveAppleSiliconWineExecutable(wine, getenv("HOME"), kMetalSharpWinePath);
#else
  if (wine != NULL && wine[0] != 0)
    return ResolveExecutableOnPath(wine);
#if defined(__APPLE__)
  // Native Intel Macs retain the official WineHQ app-bundle and PATH search. A unified Wine 11
  // loader is required: wine64-only installations cannot run Hyperion's 32-bit bridge.
  static const char *const installed[] = {
      "/Applications/Wine Stable.app/Contents/Resources/wine/bin/wine",
      "/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine",
      "/Applications/Wine Staging.app/Contents/Resources/wine/bin/wine",
  };
  for (const char *candidate : installed) {
    const std::string resolved = ResolveExecutableOnPath(candidate);
    if (!resolved.empty())
      return resolved;
  }
  return ResolveExecutableOnPath("wine");
#else
  return ResolveExecutableOnPath("/usr/bin/wine");
#endif
#endif
#endif
}

const char *PortPlutoInferenceCapabilityError()
{
#if defined(__EMSCRIPTEN__)
  return "Pluto inference is not available in the browser build.";
#elif defined(__APPLE__)
#if defined(__x86_64__) || defined(__amd64__)
  if (!__builtin_cpu_supports("avx2"))
    return "Pluto inference needs an x86-64 CPU with AVX2 support.";
  const std::string wine = PortPlutoWineExecutable();
  if (wine.empty())
    return "Pluto inference needs an official Wine 11 macOS package with unified 32-bit and 64-bit Windows support. Install Wine in /Applications or on PATH, or set HYPERION_PLUTO_WINE to its absolute wine executable, then retry.";
  if (const char *wineError = Wine11UnifiedError(wine))
    return wineError;
  return NULL;
#elif defined(__aarch64__) || defined(__arm64__)
  if (!MetalSharpMacOsSupported())
    return "Pluto inference on Apple Silicon needs macOS 26.4 or later. Update macOS, then restart Hyperion.";
  const std::string wine = PortPlutoWineExecutable();
  if (wine.empty())
    return "Pluto inference on Apple Silicon needs MetalSharp Wine 6.0 (Wine 11). Install MetalSharp-Wine-6.0.app in '$HOME/Library/Application Support/Hyperion' (no admin access) or MetalSharp-Wine.app in /Applications, or set HYPERION_PLUTO_WINE to its Contents/MacOS/wine executable, then retry.";
  if (const char *wineError = MetalSharpWineError(wine))
    return wineError;
  return NULL;
#else
  return "Pluto inference needs an Apple Silicon or x86-64 Mac with AVX2 support.";
#endif
#elif defined(_MSC_VER)
  if (IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE))
    return NULL;
  return "Pluto inference needs a CPU with AVX2 support.";
#elif defined(__x86_64__) || defined(__amd64__)
  if (!__builtin_cpu_supports("avx2"))
    return "Pluto inference needs a CPU with AVX2 support.";
  if (PortPlutoWineExecutable().empty())
    return "Pluto inference needs Wine for the official x86-64 engine. Install Wine or set HYPERION_PLUTO_WINE, then retry.";
  return NULL;
#else
  return "Pluto inference needs an x86-64 CPU with AVX2 and a proven Wine path for its official Windows engine.";
#endif
}

const char *PortPlutoDownloadCapabilityError()
{
#ifdef SB_NETCODE_OFF
  return "This build cannot download Pluto AI because its HTTPS downloader is not included (built with --no-sb).";
#elif defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
  if (!MetalSharpMacOsSupported())
    return "Pluto setup on Apple Silicon needs macOS 26.4 or later. Update macOS, then restart Hyperion.";
  // A missing or invalid app-managed Wine is the dependency this action installs. Inference keeps
  // its stricter gate and is checked again after dependency activation, before Pluto replacement.
  return NULL;
#else
  return PortPlutoInferenceCapabilityError();
#endif
}

void PortPlutoInstallRefresh()
{
  Installer &installer = GlobalInstaller();
  bool wasReady = false;
  {
    std::lock_guard<std::mutex> hold(installer.lock);
    if (installer.status.phase == PORT_PLUTO_INSTALL_RUNNING)
      return;
    wasReady = installer.status.phase == PORT_PLUTO_INSTALL_READY ||
               installer.status.phase == PORT_PLUTO_INSTALL_SUCCEEDED;
  }
  std::string error;
  if (RuntimeReady(OfficialSpec(), PortPlutoRuntimeFolder(), &error))
    SetPhase(PORT_PLUTO_INSTALL_READY);
  else if (wasReady)
    SetPhase(PORT_PLUTO_INSTALL_IDLE);
}

PortPlutoInstallStatus PortPlutoInstallGetStatus()
{
  Installer &installer = GlobalInstaller();
  std::lock_guard<std::mutex> hold(installer.lock);
  return installer.status;
}

bool PortPlutoInstallStartOrCancel(bool bInGame)
{
  Installer &installer = GlobalInstaller();
  std::lock_guard<std::mutex> hold(installer.lock);
  if (installer.status.phase == PORT_PLUTO_INSTALL_RUNNING) {
    installer.cancel.store(true);
    return false;
  }
  if (bInGame) {
    installer.status.phase = PORT_PLUTO_INSTALL_FAILED;
    installer.status.error = "Leave the game before installing or repairing Pluto AI.";
    return false;
  }
  std::string bridgeError;
  if (!BridgeReady(PortPlutoRuntimeFolder(), &bridgeError)) {
    installer.status.phase = PORT_PLUTO_INSTALL_FAILED;
    installer.status.error = bridgeError;
    Log("Pluto install: bridge preflight refused: %s", bridgeError);
    return false;
  }
  if (const char *capability = PortPlutoDownloadCapabilityError()) {
    installer.status.phase = PORT_PLUTO_INSTALL_FAILED;
    installer.status.error = capability;
    Log("Pluto install: capability refused: %s", capability);
    return false;
  }
  if (installer.worker.joinable())
    installer.worker.join();  // the previous worker published its terminal state while unlocked
  installer.cancel.store(false);
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
  installer.status = {PORT_PLUTO_INSTALL_RUNNING, 0, OfficialMetalSharpSpec().cbArchive, "checking-wine", ""};
#else
  installer.status = {PORT_PLUTO_INSTALL_RUNNING, 0, OfficialSpec().cbArchive, "downloading", ""};
#endif
  try {
    installer.worker = std::thread([] {
      std::string error;
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
      const bool installed = InstallAppleSiliconRuntime(
          OfficialSpec(), OfficialMetalSharpSpec(), PortPlutoRuntimeFolder(), getenv("HOME"),
          MetalSharpTools{}, RustTransport, PortPlutoWineExecutable, PortPlutoInferenceCapabilityError,
          GlobalCancelled, Progress, &error);
#else
      const bool installed = InstallRuntime(OfficialSpec(), PortPlutoRuntimeFolder(), RustTransport,
                                            GlobalCancelled, Progress, &error);
#endif
      if (installed)
        SetPhase(PORT_PLUTO_INSTALL_SUCCEEDED);
      else
        SetPhase(GlobalCancelled() && error == kCancelled ? PORT_PLUTO_INSTALL_CANCELLED : PORT_PLUTO_INSTALL_FAILED,
                 error);
    });
  } catch (const std::system_error &error) {
    installer.status.phase = PORT_PLUTO_INSTALL_FAILED;
    installer.status.stage.clear();
    installer.status.error = std::string("Could not start the installer worker: ") + error.what();
    return false;
  }
  return true;
}

void PortPlutoInstallCancel()
{
  Installer &installer = GlobalInstaller();
  std::lock_guard<std::mutex> hold(installer.lock);
  if (installer.status.phase == PORT_PLUTO_INSTALL_RUNNING)
    installer.cancel.store(true);
}

void PortPlutoInstallCancelAndWait()
{
  Installer &installer = GlobalInstaller();
  PortPlutoInstallCancel();
  // Never hold the status lock while joining: the worker takes it to publish its terminal state.
  if (installer.worker.joinable())
    installer.worker.join();
}

std::string PortPlutoInstallText(const PortPlutoInstallStatus &status)
{
  char text[256];
  switch (status.phase) {
  case PORT_PLUTO_INSTALL_READY:
  case PORT_PLUTO_INSTALL_SUCCEEDED:
    return "Pluto AI v2.0 runtime is installed and verified. Repair downloads the official files again.";
  case PORT_PLUTO_INSTALL_RUNNING:
    if (GlobalCancelled())
      return "Cancelling Pluto AI setup; no partial app or runtime will be activated...";
    if (status.stage == "checking-wine")
      return "Checking whether the signed MetalSharp Wine 6.0 dependency is needed...";
    if (status.stage == "extracting-wine")
      return "Extracting the hash-verified MetalSharp Wine archive with macOS ditto...";
    if (status.stage == "validating-wine")
      return "Verifying MetalSharp Wine's signature, provisioning profile, runtime layout and identity...";
    if (status.stage == "activating-wine")
      return "Activating verified MetalSharp Wine; the previous app-managed bundle can be restored...";
    if (status.stage == "verifying")
      return "Verifying the official Pluto ZIP and every runtime file's SHA-256...";
    if (status.stage == "installing")
      return "Installing verified Pluto files; the bridge, logs, configuration and bandit data are preserved...";
    {
      const unsigned percent = status.cbTotal != 0
                                   ? static_cast<unsigned>(std::min<uint64_t>(100, status.cbReceived * 100 /
                                                                                         status.cbTotal))
                                   : 0;
      if (status.stage == "downloading-wine") {
        snprintf(text, sizeof text, "Downloading signed MetalSharp Wine 6.0: %u%% (%llu / %llu MiB).", percent,
                 static_cast<unsigned long long>(status.cbReceived / 1048576),
                 static_cast<unsigned long long>(status.cbTotal / 1048576));
      } else {
        snprintf(text, sizeof text, "Downloading official Pluto AI: %u%% (%llu / %llu MiB).", percent,
                 static_cast<unsigned long long>(status.cbReceived / 1048576),
                 static_cast<unsigned long long>(status.cbTotal / 1048576));
      }
      return text;
    }
  case PORT_PLUTO_INSTALL_FAILED:
    return "Pluto AI setup failed: " + status.error + " Use Retry to start cleanly.";
  case PORT_PLUTO_INSTALL_CANCELLED:
    return "Pluto AI setup cancelled. No partial app was activated and no partial runtime was marked ready; retry when ready.";
  default:
    if (const char *capability = PortPlutoDownloadCapabilityError())
      return capability;
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    {
      const std::string wine = PortPlutoWineExecutable();
      if (wine.empty() || MetalSharpWineError(wine) != NULL) {
        snprintf(text, sizeof text,
                 "Download signed MetalSharp Wine 6.0 (%llu MiB), then official Pluto AI v2.0 (%llu MiB); both are hash-verified.",
                 static_cast<unsigned long long>(OfficialMetalSharpSpec().cbArchive / 1048576),
                 static_cast<unsigned long long>(OfficialSpec().cbArchive / 1048576));
        return text;
      }
    }
#endif
    return "Download Pluto AI v2.0 directly from the official tscmoo/pluto release: 295 MiB, SHA-256 verified.";
  }
}

std::string PortPlutoInstallButtonText(const PortPlutoInstallStatus &status)
{
  switch (status.phase) {
  case PORT_PLUTO_INSTALL_RUNNING:
    return "Cancel Pluto AI download";
  case PORT_PLUTO_INSTALL_READY:
  case PORT_PLUTO_INSTALL_SUCCEEDED:
    return "Repair Pluto AI";
  case PORT_PLUTO_INSTALL_FAILED:
  case PORT_PLUTO_INSTALL_CANCELLED:
    return "Retry Pluto AI download";
  default:
    return "Download Pluto AI";
  }
}
