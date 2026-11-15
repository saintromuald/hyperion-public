// Synthetic, small-archive tests for the production Pluto runtime installer. No network access,
// official payloads, user runtime folder, Python runtime, or inference process is involved.
#include "../../src117/port/data/port_pluto_runtime.cpp"
#include "../../src117/port/data/port_zip.cpp"

#undef fopen
#include <cstdarg>
#include <fstream>
#include <iostream>

namespace {
std::string g_testData;
}

const char *PortDataFolder() { return g_testData.c_str(); }
const char *PortNativePath(const char *path) { return path; }
void PortLogger::Line(const char *) const {}
void PortLogger::Write(const char *, ...) const {}
extern "C" FILE *OcFopen(const char *path, const char *mode) { return fopen(path, mode); }

namespace {

struct TestDirectory {
  fs::path root;
  std::string runtime;
  TestDirectory() {
    const fs::path parent = fs::is_directory("/tmp/opencode") ? fs::path("/tmp/opencode")
                                                               : fs::temp_directory_path();
    root = parent / ("pluto-runtime-regression-" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                     std::to_string(::getpid()));
    runtime = (root / "Data" / "pluto").string();
    fs::create_directories(root);
  }
  ~TestDirectory() { fs::remove_all(root); }
};

bool Check(bool condition, const char *message)
{
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

void Put16(std::vector<uint8_t> *out, uint16_t value)
{
  out->push_back(static_cast<uint8_t>(value));
  out->push_back(static_cast<uint8_t>(value >> 8));
}

void Put32(std::vector<uint8_t> *out, uint32_t value)
{
  for (unsigned i = 0; i < 4; ++i)
    out->push_back(static_cast<uint8_t>(value >> (i * 8)));
}

std::vector<uint8_t> Bytes(const std::string &text)
{
  return std::vector<uint8_t>(text.begin(), text.end());
}

std::string Hash(const std::vector<uint8_t> &bytes)
{
  return Sha256(bytes.data(), bytes.size());
}

struct SyntheticEntry {
  std::string name;
  std::vector<uint8_t> bytes;
};

std::vector<uint8_t> Deflate(const std::vector<uint8_t> &input)
{
  z_stream stream = {};
  if (deflateInit2(&stream, Z_BEST_SPEED, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    std::abort();
  std::vector<uint8_t> output(deflateBound(&stream, static_cast<uLong>(input.size())));
  stream.next_in = const_cast<Bytef *>(input.data());
  stream.avail_in = static_cast<uInt>(input.size());
  stream.next_out = output.data();
  stream.avail_out = static_cast<uInt>(output.size());
  const int result = deflate(&stream, Z_FINISH);
  if (result != Z_STREAM_END)
    std::abort();
  output.resize(stream.total_out);
  deflateEnd(&stream);
  return output;
}

std::vector<uint8_t> TestZip(const std::vector<SyntheticEntry> &entries, bool compress = false)
{
  std::vector<uint8_t> zip, central;
  for (const SyntheticEntry &entry : entries) {
    const std::vector<uint8_t> packed = compress ? Deflate(entry.bytes) : entry.bytes;
    const uint32_t local = static_cast<uint32_t>(zip.size());
    Put32(&zip, 0x04034b50);
    Put16(&zip, 20);
    Put16(&zip, 0);
    Put16(&zip, compress ? 8 : 0);
    Put16(&zip, 0);
    Put16(&zip, 0);
    Put32(&zip, 0);  // the reader authenticates content with SHA-256, not ZIP's CRC
    Put32(&zip, static_cast<uint32_t>(packed.size()));
    Put32(&zip, static_cast<uint32_t>(entry.bytes.size()));
    Put16(&zip, static_cast<uint16_t>(entry.name.size()));
    Put16(&zip, 0);
    zip.insert(zip.end(), entry.name.begin(), entry.name.end());
    zip.insert(zip.end(), packed.begin(), packed.end());

    Put32(&central, 0x02014b50);
    Put16(&central, 20);
    Put16(&central, 20);
    Put16(&central, 0);
    Put16(&central, compress ? 8 : 0);
    Put16(&central, 0);
    Put16(&central, 0);
    Put32(&central, 0);
    Put32(&central, static_cast<uint32_t>(packed.size()));
    Put32(&central, static_cast<uint32_t>(entry.bytes.size()));
    Put16(&central, static_cast<uint16_t>(entry.name.size()));
    Put16(&central, 0);
    Put16(&central, 0);
    Put16(&central, 0);
    Put16(&central, 0);
    Put32(&central, 0);
    Put32(&central, local);
    central.insert(central.end(), entry.name.begin(), entry.name.end());
  }
  const uint32_t offset = static_cast<uint32_t>(zip.size());
  zip.insert(zip.end(), central.begin(), central.end());
  Put32(&zip, 0x06054b50);
  Put16(&zip, 0);
  Put16(&zip, 0);
  Put16(&zip, static_cast<uint16_t>(entries.size()));
  Put16(&zip, static_cast<uint16_t>(entries.size()));
  Put32(&zip, static_cast<uint32_t>(central.size()));
  Put32(&zip, offset);
  Put16(&zip, 0);
  return zip;
}

std::vector<SyntheticEntry> GoodEntries()
{
  return {{"pluto.dll", Bytes("synthetic DLL")},
          {"pluto/pluto_infer.exe", Bytes("synthetic inference")},
          {"pluto/pluto_weights.bin", Bytes("small synthetic weights")}};
}

PlutoRuntimeSpec SpecFor(const std::vector<uint8_t> &zip, const std::vector<SyntheticEntry> &entries)
{
  PlutoRuntimeSpec spec;
  spec.url = "https://github.invalid/synthetic.zip";
  spec.cbArchive = zip.size();
  spec.sha256 = Hash(zip);
  for (const SyntheticEntry &entry : entries)
    spec.payloads.push_back({entry.name, Hash(entry.bytes), static_cast<uint32_t>(entry.bytes.size()), 1024});
  return spec;
}

TransportFn Serve(const std::vector<uint8_t> &zip, size_t chunk = 7)
{
  return [zip, chunk](const PlutoRuntimeSpec &, ArchiveSink &sink, std::string *error) {
    size_t at = 0;
    while (at < zip.size()) {
      const size_t take = std::min(chunk, zip.size() - at);
      if (!sink.Write(zip.data() + at, take)) {
        *error = sink.IsCancelled() ? kCancelled : "synthetic stream write failed";
        return false;
      }
      at += take;
    }
    return true;
  };
}

bool Install(const PlutoRuntimeSpec &spec, const std::string &runtime, const TransportFn &transport,
             std::string *error, CancelFn cancel = CancelFn())
{
  return InstallRuntime(spec, runtime, transport, cancel, ProgressFn(), error);
}

std::vector<uint8_t> Read(const fs::path &path)
{
  std::ifstream input(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void Write(const fs::path &path, const std::string &text)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

void WritePayloads(const TestDirectory &dir, const std::vector<SyntheticEntry> &entries)
{
  for (const SyntheticEntry &entry : entries)
    Write(dir.root / "Data" / "pluto" / entry.name, std::string(entry.bytes.begin(), entry.bytes.end()));
  fs::permissions(dir.root / "Data" / "pluto" / "pluto" / "pluto_infer.exe",
                  fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                  fs::perm_options::add);
}

bool TestSuccessAndPreservation()
{
  TestDirectory dir;
  const auto entries = GoodEntries();
  const auto zip = TestZip(entries);
  const auto spec = SpecFor(zip, entries);
  Write(dir.root / "Data" / "pluto" / "pluto-bridge.exe", "ours");
  Write(dir.root / "Data" / "pluto" / "bridge.log", "log");
  Write(dir.root / "Data" / "pluto" / "pluto" / "pluto_config.json", "config");
  Write(dir.root / "Data" / "pluto" / "pluto" / "bandit.json", "bandit");
  Write(dir.root / "Data" / "pluto" / "pluto.dll", "old");
  std::string error;
  bool ok = Check(Install(spec, dir.runtime, Serve(zip), &error), error.c_str());
  for (const auto &entry : entries)
    ok = Check(Read(dir.root / "Data" / "pluto" / entry.name) == entry.bytes, "installed payload content") && ok;
  for (const char *path : {"pluto-bridge.exe", "bridge.log", "pluto/pluto_config.json", "pluto/bandit.json"})
    ok = Check(fs::exists(dir.root / "Data" / "pluto" / path), "unrelated runtime data is preserved") && ok;
  ok = Check(fs::exists(dir.root / "Data" / "pluto" / kReadyMarker), "ready marker is installed last") && ok;
  std::string readyError;
  ok = Check(RuntimeReady(spec, dir.runtime, &readyError), readyError.c_str()) && ok;
  return ok;
}

bool TestDeflatedInstall()
{
  TestDirectory dir;
  const auto entries = GoodEntries();
  const auto zip = TestZip(entries, true);
  const auto spec = SpecFor(zip, entries);
  Write(dir.root / "Data" / "pluto" / "pluto-bridge.exe", "synthetic bridge");
  std::vector<PortZipEntry> directory;
  bool ok = Check(PortZipDirectory(zip, &directory), "deflated central directory reads");
  for (const PortZipEntry &entry : directory)
    ok = Check(entry.wMethod == 8, "every synthetic entry uses raw DEFLATE") && ok;
  std::string error;
  return Check(Install(spec, dir.runtime, Serve(zip, 5), &error), error.c_str()) &&
         Check(RuntimeReady(spec, dir.runtime, &error), "deflated payloads verify after installation") && ok;
}

bool TestOuterHashMismatch()
{
  TestDirectory dir;
  const auto entries = GoodEntries();
  auto zip = TestZip(entries);
  const auto spec = SpecFor(zip, entries);
  zip[0] ^= 1;
  Write(dir.root / "Data" / "pluto" / "pluto.dll", "old DLL");
  std::string error;
  const bool installed = Install(spec, dir.runtime, Serve(zip), &error);
  return Check(!installed, "outer hash mismatch fails") &&
         Check(error.find("ZIP failed") != std::string::npos, "outer hash error is explicit") &&
         Check(Read(dir.root / "Data" / "pluto" / "pluto.dll") == Bytes("old DLL"),
               "outer mismatch never replaces an existing payload") &&
         Check(!fs::exists(dir.root / "Data" / "pluto" / kReadyMarker), "outer mismatch has no ready marker");
}

bool TestInnerHashMismatch()
{
  TestDirectory dir;
  const auto entries = GoodEntries();
  const auto zip = TestZip(entries);
  auto spec = SpecFor(zip, entries);
  spec.payloads[1].sha256.assign(64, '0');
  std::string error;
  return Check(!Install(spec, dir.runtime, Serve(zip), &error), "inner hash mismatch fails") &&
         Check(error.find("pluto_infer.exe") != std::string::npos && error.find("SHA-256") != std::string::npos,
               "inner hash error names the payload") &&
         Check(!fs::exists(dir.root / "Data" / "pluto" / "pluto.dll"), "no payload is committed before all verify") &&
         Check(!fs::exists(dir.root / "Data" / "pluto" / kReadyMarker), "inner mismatch has no ready marker");
}

bool TestMissingDuplicateAndTraversal()
{
  for (int kind = 0; kind < 3; ++kind) {
    TestDirectory dir;
    auto entries = GoodEntries();
    if (kind == 0)
      entries.erase(entries.begin() + 1);
    else if (kind == 1)
      entries.insert(entries.begin(), entries.front());
    else
      entries.push_back({"../escape", Bytes("never extracted")});
    const auto required = kind == 0 ? GoodEntries() : entries;
    const auto zip = TestZip(entries);
    const auto spec = SpecFor(zip, required);
    std::string error;
    const bool installed = Install(spec, dir.runtime, Serve(zip), &error);
    if (!Check(!installed, "malformed archive fails") ||
        !Check(!fs::exists(dir.root / "escape"), "traversal entry cannot escape staging") ||
        !Check(!fs::exists(dir.root / "Data" / "pluto" / kReadyMarker), "malformed archive has no ready marker"))
      return false;
    const char *expected = kind == 0 ? "missing" : kind == 1 ? "duplicate" : "unsafe path";
    if (!Check(error.find(expected) != std::string::npos, expected))
      return false;
  }
  return true;
}

bool TestSizeLimit()
{
  TestDirectory dir;
  const auto entries = GoodEntries();
  const auto zip = TestZip(entries);
  auto spec = SpecFor(zip, entries);
  spec.payloads[2].cbMax = static_cast<uint32_t>(entries[2].bytes.size() - 1);
  std::string error;
  bool ok = Check(!Install(spec, dir.runtime, Serve(zip), &error), "per-payload size limit fails closed") &&
            Check(error.find("permitted size") != std::string::npos, "size error is explicit");

  std::vector<PortZipEntry> directory;
  ok = Check(PortZipDirectory(zip, &directory), "synthetic central directory reads") && ok;
  PortZipEntry oversized = directory.front();
  oversized.cbSize = PORT_ZIP_ENTRY_MAX + 1;
  std::vector<uint8_t> out;
  ok = Check(!PortZipRead(zip, oversized, &out), "the default 256 MiB reader limit remains intact") && ok;
  PortZipEntry small = directory.front();
  ok = Check(!PortZipRead(zip, small, &out, static_cast<uint32_t>(small.cbSize - 1)),
             "a per-call smaller limit is enforced before allocation") &&
       Check(PortZipRead(zip, small, &out, small.cbSize), "a per-call sufficient limit reads the real entry") &&
       Check(out == entries.front().bytes, "per-call read returns the exact entry") && ok;
  return ok;
}

bool TestInterruption()
{
  TestDirectory dir;
  const auto entries = GoodEntries();
  const auto zip = TestZip(entries);
  const auto spec = SpecFor(zip, entries);
  Write(dir.root / "Data" / "pluto" / "pluto.dll", "old DLL");
  TransportFn interrupted = [zip](const PlutoRuntimeSpec &, ArchiveSink &sink, std::string *error) {
    if (!sink.Write(zip.data(), zip.size() / 2))
      return false;
    *error = "synthetic connection interrupted";
    return false;
  };
  std::string error;
  return Check(!Install(spec, dir.runtime, interrupted, &error), "interrupted transport fails") &&
         Check(error == "synthetic connection interrupted", "transport interruption reaches the UI error") &&
         Check(Read(dir.root / "Data" / "pluto" / "pluto.dll") == Bytes("old DLL"),
               "interruption leaves the previous payload intact") &&
         Check(!fs::exists(dir.root / "Data" / "pluto" / kReadyMarker), "interruption has no ready marker");
}

bool TestCancelRetry()
{
  TestDirectory dir;
  const auto entries = GoodEntries();
  const auto zip = TestZip(entries);
  const auto spec = SpecFor(zip, entries);
  Write(dir.root / "Data" / "pluto" / "pluto-bridge.exe", "synthetic bridge");
  bool cancelled = false;
  TransportFn cancellable = [zip, &cancelled](const PlutoRuntimeSpec &, ArchiveSink &sink, std::string *error) {
    for (uint8_t byte : zip) {
      cancelled = true;
      if (!sink.Write(&byte, 1)) {
        *error = kCancelled;
        return false;
      }
    }
    return true;
  };
  std::string error;
  bool ok = Check(!Install(spec, dir.runtime, cancellable, &error, [&cancelled] { return cancelled; }),
                  "active cancellation aborts the transport") &&
            Check(error == kCancelled, "cancellation is not reported as a hash or network error") &&
            Check(!fs::exists(dir.root / "Data" / "pluto" / kReadyMarker), "cancelled install is not ready");
  error.clear();
  ok = Check(Install(spec, dir.runtime, Serve(zip), &error), "retry after cancellation succeeds") && ok;
  ok = Check(error.empty() && RuntimeReady(spec, dir.runtime, &error), "retry produces a verified runtime") && ok;
  return ok;
}

bool TestCommitRollback()
{
  TestDirectory dir;
  const auto entries = GoodEntries();
  const auto zip = TestZip(entries);
  const auto spec = SpecFor(zip, entries);
  Write(dir.root / "Data" / "pluto" / "pluto.dll", "old DLL");
  Write(dir.root / "Data" / "pluto" / kReadyMarker, "old marker");
  // The second target's parent is a regular file. The DLL commits first, then this failure must
  // restore it and remove the ready marker rather than leave a half-new runtime marked ready.
  Write(dir.root / "Data" / "pluto" / "pluto", "blocks the inference directory");
  std::string error;
  return Check(!Install(spec, dir.runtime, Serve(zip), &error), "injected commit failure fails") &&
         Check(Read(dir.root / "Data" / "pluto" / "pluto.dll") == Bytes("old DLL"),
               "commit failure rolls the first replaced file back") &&
         Check(Read(dir.root / "Data" / "pluto" / "pluto") == Bytes("blocks the inference directory"),
               "commit failure does not remove an unrelated conflicting path") &&
         Check(!fs::exists(dir.root / "Data" / "pluto" / kReadyMarker), "commit failure removes ready state");
}

bool TestCancelAndWaitJoins()
{
  Installer &installer = GlobalInstaller();
  if (installer.worker.joinable())
    installer.worker.join();
  installer.cancel.store(false);
  installer.status = {PORT_PLUTO_INSTALL_RUNNING, 0, 1, "downloading", ""};
  installer.worker = std::thread([] {
    while (!GlobalCancelled())
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    SetPhase(PORT_PLUTO_INSTALL_CANCELLED, kCancelled);
  });
  PortPlutoInstallCancelAndWait();
  return Check(!installer.worker.joinable(), "cancel-and-wait joins the installer worker") &&
         Check(installer.status.phase == PORT_PLUTO_INSTALL_CANCELLED,
               "cancel-and-wait returns only after the worker publishes its terminal state");
}

bool TestRollbackPreservesExecutableMode()
{
  TestDirectory dir;
  const auto entries = GoodEntries();
  const auto zip = TestZip(entries);
  const auto spec = SpecFor(zip, entries);
  WritePayloads(dir, entries);
  Write(dir.root / "Data" / "pluto" / "pluto-bridge.exe", "synthetic bridge");
  Write(dir.root / "Data" / "pluto" / kReadyMarker, "old marker");
  const fs::path inference = dir.root / "Data" / "pluto" / "pluto" / "pluto_infer.exe";
  Write(inference, "old executable inference");
  fs::permissions(inference, fs::perms::all, fs::perm_options::replace);
  const fs::path weights = dir.root / "Data" / "pluto" / "pluto" / "pluto_weights.bin";
  fs::remove(weights);
  fs::create_directory(weights);  // the third payload's backup fails after inference was replaced
  std::string error;
  return Check(!Install(spec, dir.runtime, Serve(zip), &error), "late payload failure triggers rollback") &&
         Check(Read(inference) == Bytes("old executable inference"), "rollback restores inference bytes") &&
         Check(access(inference.c_str(), X_OK) == 0,
               "rollback preserves inference's executable mode") &&
         Check(fs::is_directory(weights), "the conflicting weights path is preserved") &&
         Check(Read(dir.root / "Data" / "pluto" / "pluto-bridge.exe") == Bytes("synthetic bridge"),
               "rollback preserves the bridge") &&
         Check(!fs::exists(dir.root / "Data" / "pluto" / kReadyMarker),
               "rollback leaves no ready marker");
}

bool TestWineResolution()
{
  TestDirectory dir;
  const fs::path executable = dir.root / "bin" / "wine-test";
  Write(executable, "#!/bin/sh\nexit 0\n");
  fs::permissions(executable, fs::perms::all, fs::perm_options::replace);
  const char *oldPath = getenv("PATH");
  const std::string savedPath = oldPath != NULL ? oldPath : "";
  setenv("PATH", (dir.root / "bin").c_str(), 1);
  setenv("HYPERION_PLUTO_WINE", "wine-test", 1);
  bool ok = Check(PortPlutoWineExecutable() == executable.string(),
                  "a bare Wine override resolves to the same absolute executable used for launch");
  setenv("HYPERION_PLUTO_WINE", executable.c_str(), 1);
  ok = Check(PortPlutoWineExecutable() == executable.string(),
             "an absolute Wine override remains the same executable") && ok;
  setenv("HYPERION_PLUTO_WINE", "missing-wine-test", 1);
  ok = Check(PortPlutoWineExecutable().empty(), "a missing bare executable fails capability resolution") && ok;
  setenv("PATH", savedPath.c_str(), 1);
  unsetenv("HYPERION_PLUTO_WINE");
  return ok;
}

fs::path MakeFakeWine11(const TestDirectory &dir, const std::string &version, bool dualArchitecture = true)
{
  const fs::path root = dir.root / "wine";
  const fs::path executable = root / "bin" / "wine-test";
  Write(executable, "#!/bin/sh\nprintf '%s\\n' \"$0\" > \"" + (dir.root / "invoked").string() +
                        "\"\nprintf '%s\\n' \"$ROSETTA_ADVERTISE_AVX\" > \"" +
                        (dir.root / "rosetta").string() + "\"\nprintf '%s\\n' '" + version + "'\n");
  fs::permissions(executable, fs::perms::all, fs::perm_options::replace);
  if (dualArchitecture) {
    Write(root / "lib" / "wine" / "i386-windows" / "ntdll.dll", "i386");
    Write(root / "lib" / "wine" / "x86_64-windows" / "ntdll.dll", "x64");
    Write(root / "lib" / "wine" / "x86_64-unix" / "wine", "loader");
    fs::permissions(root / "lib" / "wine" / "x86_64-unix" / "wine", fs::perms::all,
                    fs::perm_options::replace);
  }
  return executable;
}

bool TestWine11UnifiedPreflight()
{
  bool ok = true;
  {
    TestDirectory dir;
    const fs::path executable = MakeFakeWine11(dir, "wine-11.21");
    const char *oldPath = getenv("PATH");
    const std::string savedPath = oldPath != NULL ? oldPath : "";
    setenv("PATH", (dir.root / "wine" / "bin").c_str(), 1);
    setenv("HYPERION_PLUTO_WINE", "wine-test", 1);
    const std::string resolved = PortPlutoWineExecutable();
    ok = Check(resolved == executable.string(), "a bare Wine 11 name resolves to its absolute executable") && ok;
    ok = Check(ProbeWine11Unified(resolved) == NULL, "Wine 11 with unified i386/x86-64 files passes preflight") && ok;
    ok = Check(Read(dir.root / "invoked") == Bytes(executable.string() + "\n"),
               "the version probe uses the same absolute path selected for launch") &&
         Check(Read(dir.root / "rosetta") == Bytes("1\n"), "the Wine consumer receives ROSETTA_ADVERTISE_AVX=1") && ok;
    setenv("PATH", savedPath.c_str(), 1);
    unsetenv("HYPERION_PLUTO_WINE");
  }
  {
    TestDirectory dir;
    const fs::path executable = MakeFakeWine11(dir, "wine-10.0");
    ok = Check(ProbeWine11Unified(executable.string()) != NULL, "a wrong Wine major version is refused") && ok;
  }
  {
    TestDirectory dir;
    const fs::path executable = MakeFakeWine11(dir, "wine-11.21", false);
    ok = Check(ProbeWine11Unified(executable.string()) != NULL, "Wine 11 without unified runtime files is refused") && ok;
  }
  {
    TestDirectory dir;
    fs::create_directories(dir.root / "bin" / "wine-directory");
    fs::permissions(dir.root / "bin" / "wine-directory", fs::perms::all, fs::perm_options::replace);
    ok = Check(AbsoluteExecutable((dir.root / "bin" / "wine-directory").string()).empty(),
               "an executable directory is not a Wine executable") &&
         Check(ProbeWine11Unified((dir.root / "bin" / "wine-directory").string()) != NULL,
               "a directory fails Wine preflight before any download") &&
         Check(ProbeWine11Unified("/bin/true") != NULL, "/bin/true is not accepted as Wine") && ok;
    const char *oldPath = getenv("PATH");
    const std::string savedPath = oldPath != NULL ? oldPath : "";
    setenv("PATH", "/bin", 1);
    setenv("HYPERION_PLUTO_WINE", "true", 1);
    const std::string expectedTrue = ResolveExecutableOnPath("true");
    const std::string resolvedTrue = PortPlutoWineExecutable();
    const char *probeError = ProbeWine11Unified(resolvedTrue);
    std::cerr << "bare true: PATH resolves to '" << expectedTrue << "', override resolves to '" << resolvedTrue
              << "', probe returns '" << (probeError != NULL ? probeError : "<accepted>") << "'\n";
    ok = Check(!expectedTrue.empty(), "the host has a true executable for the bare-name rejection test") && ok;
    ok = Check(resolvedTrue == expectedTrue, "a bare non-Wine name resolves to the host's true executable") && ok;
    ok = Check(probeError != NULL, "the host's true executable fails the meaningful Wine probe") && ok;
    setenv("PATH", savedPath.c_str(), 1);
    unsetenv("HYPERION_PLUTO_WINE");
  }
  return ok;
}

bool TestMacWineCapability()
{
#if defined(__APPLE__)
  bool ok = true;
  setenv("HYPERION_PLUTO_WINE", "/bin/true", 1);
  ok = Check(PortPlutoInferenceCapabilityError() != NULL, "the public Mac capability rejects /bin/true") && ok;
  {
    const char *oldPath = getenv("PATH");
    const std::string savedPath = oldPath != NULL ? oldPath : "";
    setenv("PATH", "/bin", 1);
    setenv("HYPERION_PLUTO_WINE", "true", 1);
    const std::string resolvedTrue = PortPlutoWineExecutable();
    const char *error = PortPlutoInferenceCapabilityError();
    std::cerr << "public bare true capability: path='" << resolvedTrue << "', error='"
              << (error != NULL ? error : "<accepted>") << "'\n";
    ok = Check(error != NULL, "the public Mac capability rejects bare true") && ok;
    setenv("PATH", savedPath.c_str(), 1);
  }
  {
    TestDirectory dir;
    fs::create_directories(dir.root / "wine-directory");
    setenv("HYPERION_PLUTO_WINE", (dir.root / "wine-directory").c_str(), 1);
    ok = Check(PortPlutoInferenceCapabilityError() != NULL, "the public Mac capability rejects a directory") && ok;
  }
  {
    TestDirectory dir;
    const fs::path executable = MakeFakeWine11(dir, "wine-10.0");
    const char *oldPath = getenv("PATH");
    const std::string savedPath = oldPath != NULL ? oldPath : "";
    setenv("PATH", (dir.root / "wine" / "bin").c_str(), 1);
    setenv("HYPERION_PLUTO_WINE", "wine-test", 1);
    ok = Check(PortPlutoInferenceCapabilityError() != NULL,
               "the public Mac capability rejects a wrong-version bare Wine name") &&
         ok;
    setenv("PATH", savedPath.c_str(), 1);
  }
  {
    TestDirectory dir;
    const fs::path executable = MakeFakeWine11(dir, "wine-11.21");
    setenv("HYPERION_PLUTO_WINE", executable.c_str(), 1);
    const char *error = PortPlutoInferenceCapabilityError();
    if (error != NULL)
      std::cerr << error << '\n';
    ok = Check(error == NULL, "the public Mac capability accepts a valid Wine 11 unified installation") && ok;
  }
  unsetenv("HYPERION_PLUTO_WINE");
  return ok;
#else
  return true;
#endif
}

bool TestCurrentCapability()
{
  const std::string wine = PortPlutoWineExecutable();
  const char *capability = PortPlutoInferenceCapabilityError();
  if (capability != NULL)
    std::cerr << capability << '\n';
  return Check(!wine.empty() && fs::path(wine).is_absolute(),
               "the current runtime resolves an absolute Wine executable") &&
         Check(capability == NULL, "the current host must pass the production inference capability gate") &&
         Check(fs::path(wine) == fs::path(PortPlutoWineExecutable()),
               "capability and launch resolve the same Wine pathname");
}

bool TestNoSbDownloadCapability()
{
  TestDirectory dir;
  Write(dir.root / "Data" / "pluto" / "pluto-bridge.exe", "synthetic bridge");
  setenv("HYPERION_PLUTO_FOLDER", dir.runtime.c_str(), 1);
  Installer &installer = GlobalInstaller();
  installer.status.phase = PORT_PLUTO_INSTALL_IDLE;
  const char *capability = PortPlutoDownloadCapabilityError();
  const std::string text = PortPlutoInstallText(installer.status);
  const bool started = PortPlutoInstallStartOrCancel(false);
  unsetenv("HYPERION_PLUTO_FOLDER");
  return Check(capability != NULL && std::string(capability).find("--no-sb") != std::string::npos,
               "--no-sb exposes an upfront download capability error") &&
         Check(text.find("--no-sb") != std::string::npos, "the unstarted UI text explains --no-sb") &&
         Check(!started && !installer.worker.joinable(), "--no-sb never starts a download worker") &&
         Check(installer.status.error.find("--no-sb") != std::string::npos,
               "a programmatic start receives the same capability error");
}

bool TestMissingBridge()
{
  TestDirectory dir;
  const auto entries = GoodEntries();
  const auto zip = TestZip(entries);
  const auto spec = SpecFor(zip, entries);
  WritePayloads(dir, entries);  // a complete official payload set, but no Hyperion bridge
  std::string error;
  bool ok = Check(!RuntimeReady(spec, dir.runtime, &error), "official payloads alone are not ready") &&
            Check(error.find("Repair or reinstall Hyperion") != std::string::npos,
                  "missing bridge readiness error says to repair Hyperion");

  {
    Installer &installer = GlobalInstaller();
    std::lock_guard<std::mutex> hold(installer.lock);
    installer.status.phase = PORT_PLUTO_INSTALL_SUCCEEDED;
    installer.status.error.clear();
  }
  setenv("HYPERION_PLUTO_FOLDER", dir.runtime.c_str(), 1);
  std::string publicError;
  ok = Check(!PortPlutoRuntimeReady(&publicError) && publicError.find("Repair or reinstall Hyperion") != std::string::npos,
             "the public game-start readiness API rejects payloads without a bridge") && ok;
  PortPlutoInstallRefresh();
  ok = Check(PortPlutoInstallGetStatus().phase == PORT_PLUTO_INSTALL_IDLE,
             "refresh clears stale success when the bridge is absent") && ok;
  const bool started = PortPlutoInstallStartOrCancel(false);
  unsetenv("HYPERION_PLUTO_FOLDER");
  const PortPlutoInstallStatus status = PortPlutoInstallGetStatus();
  Installer &installer = GlobalInstaller();
  ok = Check(!started, "start refuses a missing packaged bridge") &&
       Check(status.phase == PORT_PLUTO_INSTALL_FAILED &&
                 status.error.find("Repair or reinstall Hyperion") != std::string::npos,
             "start error directs the player to repair Hyperion") &&
       Check(!installer.worker.joinable(), "missing-bridge refusal never starts a download worker") && ok;
  return ok;
}

bool TestNonExecutableInference()
{
  TestDirectory dir;
  const auto entries = GoodEntries();
  const auto zip = TestZip(entries);
  const auto spec = SpecFor(zip, entries);
  WritePayloads(dir, entries);
  Write(dir.root / "Data" / "pluto" / "pluto-bridge.exe", "synthetic bridge");
  Write(dir.root / "Data" / "pluto" / "pluto" / "bandit.json", "preserve me");
  const fs::path inference = dir.root / "Data" / "pluto" / "pluto" / "pluto_infer.exe";
  fs::permissions(inference,
                  fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                  fs::perm_options::remove);
  std::string error;
  bool ok = Check(access(inference.c_str(), X_OK) != 0, "fixture begins without execute access") &&
            Check(!RuntimeReady(spec, dir.runtime, &error), "non-executable inference is not ready") &&
            Check(error.find("not executable") != std::string::npos,
                  "non-executable readiness error is explicit");
  error.clear();
  ok = Check(Install(spec, dir.runtime, Serve(zip), &error), error.c_str()) &&
       Check(access(inference.c_str(), X_OK) == 0, "repair gives staged inference execute access") &&
       Check(RuntimeReady(spec, dir.runtime, &error), "repaired runtime passes the complete readiness gate") &&
       Check(Read(dir.root / "Data" / "pluto" / "pluto-bridge.exe") == Bytes("synthetic bridge"),
             "repair preserves Hyperion's bridge") &&
       Check(Read(dir.root / "Data" / "pluto" / "pluto" / "bandit.json") == Bytes("preserve me"),
             "repair preserves bandit data") && ok;
  return ok;
}

bool TestInGameLock()
{
  Installer &installer = GlobalInstaller();
  {
    std::lock_guard<std::mutex> hold(installer.lock);
    installer.status.phase = PORT_PLUTO_INSTALL_IDLE;
    installer.status.error.clear();
  }
  const bool started = PortPlutoInstallStartOrCancel(true);
  const PortPlutoInstallStatus status = PortPlutoInstallGetStatus();
  return Check(!started, "an in-game install action is refused") &&
         Check(status.phase == PORT_PLUTO_INSTALL_FAILED &&
                   status.error.find("Leave the game") != std::string::npos,
               "the in-game lock gives an accurate repair error") &&
         Check(!installer.worker.joinable(), "the in-game refusal never starts a worker");
}

bool TestOfficialPins()
{
  const PlutoRuntimeSpec &spec = OfficialSpec();
  return Check(spec.url == "https://github.com/tscmoo/pluto/releases/download/v2.0/pluto-v2.0.zip",
               "official release URL") &&
         Check(spec.cbArchive == 309686477, "official outer byte count") &&
         Check(spec.sha256 == "17544fc3ab32e3e2823646887e021cf998362348d73740817e94d1a78aa6209d",
               "official outer SHA-256") &&
         Check(spec.payloads.size() == 3, "three official payloads") &&
         Check(spec.payloads[0].sha256 == "603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc",
               "official DLL SHA-256") &&
         Check(spec.payloads[1].sha256 == "3f2814a51034b9817b396e52b38d30341ede506a2baedc7cbc11cb6ebcece2cc",
               "official inference SHA-256") &&
         Check(spec.payloads[2].sha256 == "c53a559b4910777dc0e1c561c0d89d243091f465d8e9fc0f5f72e56597067d86" &&
                   spec.payloads[2].cbSize == 347935248,
               "official weights SHA-256 and size");
}

bool TestResolutionAndQuantizedText()
{
  bool ok = Check(RuntimeFolderFor(NULL, "Data") == "Data/pluto", "default runtime folder") &&
            Check(RuntimeFolderFor(NULL, "Data\\") == "Data\\pluto", "existing trailing separator") &&
            Check(RuntimeFolderFor("/tmp/custom", "Data") == "/tmp/custom", "runtime override wins") &&
            Check(RuntimeFolderFor("", "Data/") == "Data/pluto", "empty override is unset");
  g_testData = "/tmp/synthetic-data/";
  ok = Check(PortPlutoRuntimeFolder() == "/tmp/synthetic-data/pluto", "public resolver shares the same rule") && ok;
  setenv("HYPERION_PLUTO_FOLDER", "/tmp/synthetic-override", 1);
  ok = Check(PortPlutoRuntimeFolder() == "/tmp/synthetic-override", "public resolver reads the override") && ok;
  unsetenv("HYPERION_PLUTO_FOLDER");
  PortPlutoInstallStatus status{PORT_PLUTO_INSTALL_RUNNING, 100 * 1048576 + 100, 309686477, "downloading", ""};
  const std::string first = PortPlutoInstallText(status);
  status.cbReceived += 100;
  const std::string second = PortPlutoInstallText(status);
  ok = Check(first == second, "progress text is quantized instead of changing for every chunk") &&
       Check(first.find("33%") != std::string::npos && first.find("100 / 295 MiB") != std::string::npos,
             "progress contains whole percent and MiB") &&
       Check(PortPlutoInstallButtonText(status) == "Cancel Pluto AI download", "running action is cancellable") && ok;
  status.phase = PORT_PLUTO_INSTALL_FAILED;
  ok = Check(PortPlutoInstallButtonText(status) == "Retry Pluto AI download", "failed action exposes retry") && ok;
  return ok;
}

}  // namespace

int main(int argc, char **argv)
{
  if (argc != 2)
    return 2;
  const std::string scenario = argv[1];
  bool ok = false;
  if (scenario == "success-preserve")
    ok = TestSuccessAndPreservation();
  else if (scenario == "deflated")
    ok = TestDeflatedInstall();
  else if (scenario == "outer-hash")
    ok = TestOuterHashMismatch();
  else if (scenario == "inner-hash")
    ok = TestInnerHashMismatch();
  else if (scenario == "entries")
    ok = TestMissingDuplicateAndTraversal();
  else if (scenario == "size-limit")
    ok = TestSizeLimit();
  else if (scenario == "interruption")
    ok = TestInterruption();
  else if (scenario == "cancel-retry")
    ok = TestCancelRetry();
  else if (scenario == "rollback")
    ok = TestCommitRollback();
  else if (scenario == "resolution-progress")
    ok = TestResolutionAndQuantizedText();
  else if (scenario == "official-pins")
    ok = TestOfficialPins();
  else if (scenario == "in-game-lock")
    ok = TestInGameLock();
  else if (scenario == "missing-bridge")
    ok = TestMissingBridge();
  else if (scenario == "inference-executable")
    ok = TestNonExecutableInference();
  else if (scenario == "cancel-wait")
    ok = TestCancelAndWaitJoins();
  else if (scenario == "rollback-mode")
    ok = TestRollbackPreservesExecutableMode();
  else if (scenario == "wine-resolution")
    ok = TestWineResolution();
  else if (scenario == "current-capability")
    ok = TestCurrentCapability();
  else if (scenario == "wine-11-preflight")
    ok = TestWine11UnifiedPreflight();
  else if (scenario == "mac-wine-capability")
    ok = TestMacWineCapability();
  else if (scenario == "no-sb-capability")
    ok = TestNoSbDownloadCapability();
  else
    return 2;
  return ok ? 0 : 1;
}
