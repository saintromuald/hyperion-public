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

fs::path MakeFakeMetalSharpWineAt(const TestDirectory &dir, const fs::path &app,
                                  const std::string &version = "wine-11.20 (MetalSharp Wine 6.0)",
                                  bool complete = true, bool profile = true)
{
  const fs::path contents = app / "Contents";
  const fs::path executable = contents / "MacOS" / "wine";
  Write(executable, "#!/bin/sh\nprintf '%s\\n' \"$0\" > \"" + (dir.root / "invoked").string() +
                        "\"\nprintf '%s\\n' \"$ROSETTA_ADVERTISE_AVX\" > \"" +
                        (dir.root / "rosetta").string() + "\"\nprintf '%s\\n' '" + version + "'\n");
  fs::permissions(executable, fs::perms::all, fs::perm_options::replace);
  if (complete) {
    Write(contents / "MacOS" / "wineserver", "wineserver");
    Write(contents / "lib" / "wine" / "i386-windows" / "ntdll.dll", "i386");
    Write(contents / "lib" / "wine" / "aarch64-unix" / "wine", "loader");
    fs::permissions(contents / "MacOS" / "wineserver", fs::perms::all, fs::perm_options::replace);
    fs::permissions(contents / "lib" / "wine" / "aarch64-unix" / "wine", fs::perms::all,
                    fs::perm_options::replace);
  }
  if (profile)
    Write(contents / "embedded.provisionprofile", "synthetic provisioning profile");
  return executable;
}

fs::path MakeFakeMetalSharpWine(const TestDirectory &dir,
                                const std::string &version = "wine-11.20 (MetalSharp Wine 6.0)",
                                bool complete = true, bool profile = true)
{
  return MakeFakeMetalSharpWineAt(dir, dir.root / "MetalSharp-Wine.app", version, complete, profile);
}

struct FakeDependencyTools {
  explicit FakeDependencyTools(TestDirectory &directory) : dir(directory)
  {
    tools.ditto = "synthetic-ditto";
    tools.codesign = "synthetic-codesign";
    tools.run = [this](const std::string &program, const std::vector<std::string> &args,
                       const CancelFn &cancel, std::string *output) {
      calls.push_back(args);
      if (cancel && cancel())
        return false;
      if (program == tools.ditto) {
        if (cancelOnDitto && cancellation != NULL)
          cancellation->store(true);
        if (failDitto) {
          *output = "synthetic ditto failure";
          return false;
        }
        if (cancel && cancel())
          return false;
        if (createApp) {
          const fs::path app = fs::path(args.back()) / "MetalSharp-Wine.app";
          MakeFakeMetalSharpWineAt(dir, app, identity, complete, profile);
          std::error_code ec;
          fs::create_symlink("lib", app / "Contents" / "library-link", ec);
          if (ec)
            std::abort();
        }
        return true;
      }
      if (program == tools.codesign) {
        if (cancelOnCodesign && cancellation != NULL)
          cancellation->store(true);
        if (failCodesign) {
          *output = "synthetic signature mismatch";
          return false;
        }
        return !cancel || !cancel();
      }
      *output = "unexpected synthetic tool";
      return false;
    };
  }

  TestDirectory &dir;
  MetalSharpTools tools;
  std::vector<std::vector<std::string>> calls;
  std::string identity = "wine-11.20 (MetalSharp Wine 6.0)";
  std::atomic<bool> *cancellation = NULL;
  bool createApp = true, complete = true, profile = true;
  bool failDitto = false, failCodesign = false;
  bool cancelOnDitto = false, cancelOnCodesign = false;
};

bool InstallDependency(const PlutoRuntimeSpec &spec, const fs::path &target,
                       const std::vector<uint8_t> &archive, MetalSharpTools &tools, std::string *error,
                       CancelFn cancel = CancelFn(), ProgressFn progress = ProgressFn())
{
  return InstallMetalSharpRuntime(spec, target.string(), tools, Serve(archive), cancel, progress, error);
}

bool NoMetalSharpStages(const fs::path &parent)
{
  if (!fs::exists(parent))
    return true;
  for (const fs::directory_entry &entry : fs::directory_iterator(parent)) {
    if (entry.path().filename().string().find(".metalsharp-install-") == 0)
      return false;
  }
  return true;
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
               "a directory fails Wine preflight before any download") && ok;
    const char *oldPath = getenv("PATH");
    const std::string savedPath = oldPath != NULL ? oldPath : "";
    const std::string trueExecutable = ResolveExecutableOnPath("true");
    const char *directError = ProbeWine11Unified(trueExecutable);
    std::cerr << "discovered true: path='" << trueExecutable << "', direct probe returns '"
              << (directError != NULL ? directError : "<accepted>") << "'\n";
    if (!Check(!trueExecutable.empty(), "the host has a true executable for the rejection tests") ||
        !Check(directError != NULL, "the discovered true executable fails direct Wine preflight"))
      return false;
    setenv("PATH", ParentPath(trueExecutable).c_str(), 1);
    setenv("HYPERION_PLUTO_WINE", "true", 1);
    const std::string resolvedTrue = PortPlutoWineExecutable();
    const char *probeError = ProbeWine11Unified(resolvedTrue);
    std::cerr << "bare true: parent='" << ParentPath(trueExecutable) << "', override resolves to '" << resolvedTrue
              << "', probe returns '" << (probeError != NULL ? probeError : "<accepted>") << "'\n";
    ok = Check(resolvedTrue == trueExecutable, "a bare non-Wine name resolves to the discovered true executable") && ok;
    ok = Check(probeError != NULL, "the discovered true executable fails the meaningful Wine probe") && ok;
    setenv("PATH", savedPath.c_str(), 1);
    unsetenv("HYPERION_PLUTO_WINE");
  }
  return ok;
}

bool TestMetalSharpWinePreflight()
{
  bool ok = Check(MacOsVersionAtLeast("26.4\n", 26, 4), "macOS 26.4 meets the MetalSharp minimum") &&
            Check(MacOsVersionAtLeast("26.4.1", 26, 4), "a macOS patch release is accepted") &&
            Check(MacOsVersionAtLeast("26.10", 26, 4), "macOS minor versions compare numerically") &&
            Check(MacOsVersionAtLeast("27", 26, 4), "a later macOS major version is accepted") &&
            Check(!MacOsVersionAtLeast("26.3.9", 26, 4), "macOS 26.3 is refused") &&
            Check(!MacOsVersionAtLeast("25.9", 26, 4), "an earlier macOS major version is refused") &&
            Check(!MacOsVersionAtLeast("26.x", 26, 4), "a malformed macOS minor version is refused") &&
            Check(!MacOsVersionAtLeast("not-a-version", 26, 4), "a malformed macOS version is refused");

  {
    TestDirectory dir;
    const fs::path executable = MakeFakeMetalSharpWine(dir);
    unsetenv("ROSETTA_ADVERTISE_AVX");
    ok = Check(ProbeMetalSharpWine(executable.string()) == NULL,
               "Wine 11 MetalSharp with its complete app layout passes preflight") &&
         ok;
    ok = Check(Read(dir.root / "invoked") == Bytes(executable.string() + "\n"),
               "the MetalSharp version probe uses the selected absolute executable") &&
         ok;
    ok = Check(Read(dir.root / "rosetta") == Bytes("\n"),
               "the MetalSharp probe does not inject Rosetta environment variables") &&
         ok;
    const fs::path home = dir.root / "home";
    const fs::path appManaged = home / "Library" / "Application Support" / "Hyperion" /
                                "MetalSharp-Wine-6.0.app" / "Contents" / "MacOS" / "wine";
    const fs::path system = dir.root / "Applications" / "MetalSharp-Wine.app" / "Contents" / "MacOS" / "wine";
    Write(appManaged, "app-managed");
    Write(system, "existing system Wine");
    fs::permissions(appManaged, fs::perms::all, fs::perm_options::replace);
    fs::permissions(system, fs::perms::all, fs::perm_options::replace);
    ok = Check(AppManagedMetalSharpWinePath(home.c_str()) == appManaged.string(),
               "the app-managed runtime uses the versioned Hyperion Library path") &&
         ok;
    ok = Check(AppManagedMetalSharpWineAppPath(home.c_str()) ==
                   appManaged.parent_path().parent_path().parent_path().string(),
               "the dependency installer targets the app bundle, not its loader executable") &&
         ok;
    ok = Check(std::string(kMetalSharpWinePath) ==
                   "/Applications/MetalSharp-Wine.app/Contents/MacOS/wine",
               "the system fallback is the signed MetalSharp app's wine loader") &&
         ok;
    ok = Check(ResolveAppleSiliconWineExecutable(executable.c_str(), home.c_str(), system.c_str()) ==
                   executable.string(),
               "an explicit Apple Silicon Wine override wins over both installed locations") &&
         ok;
    ok = Check(ResolveAppleSiliconWineExecutable(NULL, home.c_str(), system.c_str()) == appManaged.string(),
               "the app-managed MetalSharp runtime is preferred over the system installation") &&
         ok;
    ok = Check(ResolveAppleSiliconWineExecutable(NULL, (dir.root / "missing-home").c_str(), system.c_str()) ==
                   system.string(),
               "the system MetalSharp installation remains the fallback") &&
         ok;
    ok = Check(Read(system) == Bytes("existing system Wine"),
               "runtime discovery does not replace an existing system Wine") &&
         ok;
    const char *oldPath = getenv("PATH");
    const std::string savedPath = oldPath != NULL ? oldPath : "";
    setenv("PATH", executable.parent_path().c_str(), 1);
    ok = Check(ResolveAppleSiliconWineExecutable("wine", home.c_str(), system.c_str()) == executable.string(),
               "an explicit bare override still resolves through PATH before installed locations") &&
         ok;
    ok = Check(ResolveAppleSiliconWineExecutable(NULL, (dir.root / "missing-home").c_str(), "/missing/default")
                   .empty(),
               "Apple Silicon does not fall back to a generic Wine on PATH") &&
         ok;
    setenv("PATH", savedPath.c_str(), 1);
  }
  {
    TestDirectory dir;
    const fs::path executable = MakeFakeWine11(dir, "wine-11.21");
    const char *error = ProbeMetalSharpWine(executable.string());
    ok = Check(error != NULL && std::string(error).find("Generic WineHQ") != std::string::npos,
               "generic WineHQ is explicitly incompatible with Apple Silicon preflight") &&
         ok;
  }
  {
    TestDirectory dir;
    const fs::path executable = MakeFakeMetalSharpWine(dir, "wine-10.0 (MetalSharp Wine 6.0)");
    ok = Check(ProbeMetalSharpWine(executable.string()) != NULL, "MetalSharp with the wrong Wine major is refused") &&
         ok;
  }
  {
    TestDirectory dir;
    const fs::path executable = MakeFakeWine11(dir, "wine-11.20 (MetalSharp Wine 6.0)");
    const char *error = ProbeMetalSharpWine(executable.string());
    ok = Check(error != NULL && std::string(error).find("Contents/MacOS/wine") != std::string::npos,
               "MetalSharp identity outside the signed app layout is refused") &&
         ok;
  }
  {
    TestDirectory dir;
    const fs::path executable = MakeFakeMetalSharpWine(dir);
    fs::remove(executable.parent_path().parent_path() / "lib" / "wine" / "i386-windows" / "ntdll.dll");
    ok = Check(ProbeMetalSharpWine(executable.string()) != NULL, "MetalSharp without its i386 runtime is refused") &&
         ok;
  }
  {
    TestDirectory dir;
    const fs::path executable = MakeFakeMetalSharpWine(dir);
    fs::remove(executable.parent_path().parent_path() / "lib" / "wine" / "aarch64-unix" / "wine");
    ok = Check(ProbeMetalSharpWine(executable.string()) != NULL, "MetalSharp without its aarch64 loader is refused") &&
         ok;
  }
  {
    TestDirectory dir;
    const fs::path executable = MakeFakeMetalSharpWine(dir);
    fs::remove(executable.parent_path() / "wineserver");
    ok = Check(ProbeMetalSharpWine(executable.string()) != NULL, "MetalSharp without wineserver is refused") && ok;
  }
  {
    TestDirectory dir;
    const fs::path executable = MakeFakeMetalSharpWine(dir, "wine-11.20 (MetalSharp Wine 6.0)", true, false);
    const char *error = ProbeMetalSharpWine(executable.string());
    ok = Check(error != NULL && std::string(error).find("embedded.provisionprofile") != std::string::npos,
               "MetalSharp without its cross-architecture provisioning profile is refused") &&
         ok;
  }
  unsetenv("ROSETTA_ADVERTISE_AVX");
  return ok;
}

bool TestMetalSharpInstallSuccess()
{
  TestDirectory dir;
  const std::vector<uint8_t> archive = Bytes("tiny synthetic MetalSharp archive");
  PlutoRuntimeSpec spec = SpecFor(archive, {});
  spec.url = "synthetic-wine";
  const fs::path home = dir.root / "home";
  const fs::path target = AppManagedMetalSharpWineAppPath(home.c_str());
  const fs::path system = dir.root / "Applications" / "MetalSharp-Wine.app" / "Contents" / "MacOS" / "wine";
  Write(system, "existing system Wine");
  FakeDependencyTools fake(dir);
  std::vector<std::pair<uint64_t, std::string>> progress;
  std::string error;
  bool ok = Check(InstallDependency(spec, target, archive, fake.tools, &error, CancelFn(),
                                    [&](uint64_t, uint64_t total, const char *stage) {
                                      progress.push_back({total, stage});
                                    }),
                  error.c_str());
  const fs::path activeWine = target / "Contents" / "MacOS" / "wine";
  ok = Check(ProbeMetalSharpWine(activeWine.string()) == NULL,
             "the activated app passes native identity, layout and profile preflight") &&
       ok;
  ok = Check(fs::is_symlink(target / "Contents" / "library-link"),
             "app activation preserves ditto-provided symlinks") &&
       ok;
  ok = Check(Read(system) == Bytes("existing system Wine"),
             "dependency installation never replaces another Wine installation") &&
       ok;
  ok = Check(fake.calls.size() == 2, "a new dependency invokes only ditto and codesign") && ok;
  if (fake.calls.size() == 2) {
    ok = Check(fake.calls[0].size() == 5 && fake.calls[0][0] == "synthetic-ditto" &&
                   fake.calls[0][1] == "-x" && fake.calls[0][2] == "-k",
               "extraction uses ditto -x -k to preserve the signed app layout") &&
         ok;
    ok = Check(fake.calls[1].size() == 5 && fake.calls[1][0] == "synthetic-codesign" &&
                   fake.calls[1][1] == "--verify" && fake.calls[1][2] == "--strict" &&
                   fake.calls[1][3] == "--deep",
               "activation requires codesign --verify --strict --deep") &&
         ok;
  }
  for (const char *stage : {"downloading-wine", "extracting-wine", "validating-wine", "activating-wine"}) {
    ok = Check(std::find_if(progress.begin(), progress.end(), [&](const auto &event) {
                 return event.second == stage && event.first == spec.cbArchive;
               }) != progress.end(),
               stage) &&
         ok;
  }
  ok = Check(NoMetalSharpStages(target.parent_path()), "successful activation cleans owned staging") && ok;
  const size_t calls = fake.calls.size();
  ok = Check(InstallDependency(spec, target, archive, fake.tools, &error),
             "an already valid app-managed dependency is not downloaded again") &&
       Check(fake.calls.size() == calls, "dependency reuse invokes no extraction or signature tools") && ok;
  return ok;
}

bool TestMetalSharpInstallValidationFailures()
{
  bool ok = true;
  {
    TestDirectory dir;
    const std::vector<uint8_t> archive = Bytes("hash mismatch");
    PlutoRuntimeSpec spec = SpecFor(archive, {});
    spec.sha256 = std::string(64, '0');
    const fs::path target = dir.root / "home" / "MetalSharp-Wine-6.0.app";
    FakeDependencyTools fake(dir);
    std::string error;
    ok = Check(!InstallDependency(spec, target, archive, fake.tools, &error) &&
                   error.find("SHA-256") != std::string::npos && fake.calls.empty() && !fs::exists(target),
               "a Wine archive hash mismatch is rejected before extraction or activation") &&
         ok;
    ok = Check(NoMetalSharpStages(target.parent_path()), "hash failure cleans owned staging") && ok;
    spec.sha256 = Hash(archive);
    ok = Check(InstallDependency(spec, target, archive, fake.tools, &error),
               "retry after a dependency hash failure starts cleanly") &&
         Check(fake.calls.size() == 2 && ProbeMetalSharpWine((target / "Contents" / "MacOS" / "wine").string()) == NULL,
               "retry extracts, verifies and activates a complete app") &&
         ok;
  }
  {
    TestDirectory dir;
    const std::vector<uint8_t> archive = Bytes("short archive");
    PlutoRuntimeSpec spec = SpecFor(archive, {});
    ++spec.cbArchive;
    const fs::path target = dir.root / "home" / "MetalSharp-Wine-6.0.app";
    FakeDependencyTools fake(dir);
    std::string error;
    ok = Check(!InstallDependency(spec, target, archive, fake.tools, &error) &&
                   error.find("wrong size") != std::string::npos && fake.calls.empty() && !fs::exists(target),
               "a Wine archive size mismatch is rejected before extraction or activation") &&
         ok;
  }
  {
    TestDirectory dir;
    const std::vector<uint8_t> archive = Bytes("ditto failure");
    const PlutoRuntimeSpec spec = SpecFor(archive, {});
    const fs::path target = dir.root / "home" / "MetalSharp-Wine-6.0.app";
    FakeDependencyTools fake(dir);
    fake.failDitto = true;
    std::string error;
    ok = Check(!InstallDependency(spec, target, archive, fake.tools, &error) &&
                   error.find("synthetic ditto failure") != std::string::npos && !fs::exists(target),
               "ditto failure leaves no active app") &&
         ok;
  }
  {
    TestDirectory dir;
    const std::vector<uint8_t> archive = Bytes("signature failure");
    const PlutoRuntimeSpec spec = SpecFor(archive, {});
    const fs::path target = dir.root / "home" / "MetalSharp-Wine-6.0.app";
    FakeDependencyTools fake(dir);
    fake.failCodesign = true;
    std::string error;
    ok = Check(!InstallDependency(spec, target, archive, fake.tools, &error) &&
                   error.find("synthetic signature mismatch") != std::string::npos && !fs::exists(target),
               "a strict signature failure leaves no active app") &&
         ok;
  }
  {
    TestDirectory dir;
    const std::vector<uint8_t> archive = Bytes("wrong identity");
    const PlutoRuntimeSpec spec = SpecFor(archive, {});
    const fs::path target = dir.root / "home" / "MetalSharp-Wine-6.0.app";
    FakeDependencyTools fake(dir);
    fake.identity = "wine-11.20";
    std::string error;
    ok = Check(!InstallDependency(spec, target, archive, fake.tools, &error) &&
                   error.find("pre-activation validation") != std::string::npos && !fs::exists(target),
               "Wine 11 without MetalSharp identity is not activated") &&
         ok;
  }
  {
    TestDirectory dir;
    const std::vector<uint8_t> archive = Bytes("missing profile");
    const PlutoRuntimeSpec spec = SpecFor(archive, {});
    const fs::path target = dir.root / "home" / "MetalSharp-Wine-6.0.app";
    FakeDependencyTools fake(dir);
    fake.profile = false;
    std::string error;
    ok = Check(!InstallDependency(spec, target, archive, fake.tools, &error) &&
                   error.find("embedded.provisionprofile") != std::string::npos && !fs::exists(target),
               "an app without its provisioning profile is not activated") &&
         ok;
  }
  {
    TestDirectory dir;
    const std::vector<uint8_t> archive = Bytes("cancel extraction");
    const PlutoRuntimeSpec spec = SpecFor(archive, {});
    const fs::path target = dir.root / "home" / "MetalSharp-Wine-6.0.app";
    FakeDependencyTools fake(dir);
    std::atomic<bool> cancelled{false};
    fake.cancellation = &cancelled;
    fake.cancelOnDitto = true;
    std::string error;
    ok = Check(!InstallDependency(spec, target, archive, fake.tools, &error,
                                  [&] { return cancelled.load(); }) &&
                   error == kCancelled && !fs::exists(target),
               "cancelling extraction leaves no partial active app") &&
         ok;
    ok = Check(NoMetalSharpStages(target.parent_path()), "tool cancellation cleans owned staging") && ok;
  }
  return ok;
}

bool TestMetalSharpActivationRollback()
{
  bool ok = true;
  {
    TestDirectory dir;
    const std::vector<uint8_t> archive = Bytes("activation failure");
    const PlutoRuntimeSpec spec = SpecFor(archive, {});
    const fs::path target = dir.root / "home" / "MetalSharp-Wine-6.0.app";
    Write(target / "Contents" / "old-marker", "previous complete app");
    FakeDependencyTools fake(dir);
    fake.tools.move = [&](const std::string &from, const std::string &to, std::string *error) {
      if (to == target.string() && from.find("/unpacked/MetalSharp-Wine.app") != std::string::npos) {
        *error = "synthetic activation failure";
        return false;
      }
      return MoveAtomic(from, to, error);
    };
    std::string error;
    ok = Check(!InstallDependency(spec, target, archive, fake.tools, &error) &&
                   error.find("synthetic activation failure") != std::string::npos,
               "an activation rename failure is surfaced") &&
         ok;
    ok = Check(Read(target / "Contents" / "old-marker") == Bytes("previous complete app") &&
                   !fs::exists(target / "Contents" / "MacOS" / "wine"),
               "activation failure restores the previous app-managed bundle") &&
         ok;
    ok = Check(NoMetalSharpStages(target.parent_path()), "successful rollback cleans staging and its backup") && ok;
  }
  {
    TestDirectory dir;
    const std::vector<uint8_t> archive = Bytes("cancel activation");
    const PlutoRuntimeSpec spec = SpecFor(archive, {});
    const fs::path target = dir.root / "home" / "MetalSharp-Wine-6.0.app";
    Write(target / "Contents" / "old-marker", "previous complete app");
    FakeDependencyTools fake(dir);
    std::atomic<bool> cancelled{false};
    fake.tools.move = [&](const std::string &from, const std::string &to, std::string *error) {
      if (from == target.string() && to.find("previous.app") != std::string::npos)
        cancelled.store(true);
      return MoveAtomic(from, to, error);
    };
    std::string error;
    ok = Check(!InstallDependency(spec, target, archive, fake.tools, &error,
                                  [&] { return cancelled.load(); }) &&
                   error == kCancelled,
               "cancellation after backup reports cancellation") &&
         ok;
    ok = Check(Read(target / "Contents" / "old-marker") == Bytes("previous complete app") &&
                   !fs::exists(target / "Contents" / "MacOS" / "wine"),
               "cancellation before activation restores the previous app-managed bundle") &&
         ok;
    ok = Check(NoMetalSharpStages(target.parent_path()), "cancellation rollback cleans owned staging") && ok;
  }
  {
    TestDirectory dir;
    const std::vector<uint8_t> archive = Bytes("rollback recovery");
    const PlutoRuntimeSpec spec = SpecFor(archive, {});
    const fs::path target = dir.root / "home" / "MetalSharp-Wine-6.0.app";
    Write(target / "Contents" / "old-marker", "recovery bytes");
    FakeDependencyTools fake(dir);
    fake.tools.move = [&](const std::string &from, const std::string &to, std::string *error) {
      if (to == target.string() && from.find("/unpacked/MetalSharp-Wine.app") != std::string::npos) {
        *error = "synthetic activation failure";
        return false;
      }
      if (to == target.string() && from.find("previous.app") != std::string::npos) {
        *error = "synthetic restore failure";
        return false;
      }
      return MoveAtomic(from, to, error);
    };
    std::string error;
    ok = Check(!InstallDependency(spec, target, archive, fake.tools, &error) &&
                   error.find("could not be restored") != std::string::npos && !fs::exists(target),
               "a failed restore leaves no partial active app and identifies recovery data") &&
         ok;
    bool recoveryPreserved = false;
    for (const fs::directory_entry &entry : fs::directory_iterator(target.parent_path())) {
      if (entry.path().filename().string().find(".metalsharp-install-") == 0)
        recoveryPreserved = Read(entry.path() / "previous.app" / "Contents" / "old-marker") == Bytes("recovery bytes");
    }
    ok = Check(recoveryPreserved, "rollback recovery bytes are never deleted as staging garbage") && ok;
  }
  {
    std::atomic<bool> cancelled{false};
    std::thread cancelWorker([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      cancelled.store(true);
    });
    const auto started = std::chrono::steady_clock::now();
    std::string output;
    const bool ran = RunCancellableProcess("/bin/sh", {"sh", "-c", "sleep 30"},
                                           [&] { return cancelled.load(); }, &output);
    cancelWorker.join();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    ok = Check(!ran && elapsed < std::chrono::seconds(5),
               "the production tool runner kills and reaps its owned process group on cancellation") &&
         ok;
  }
  return ok;
}

bool TestAppleSiliconCombinedInstaller()
{
  bool ok = true;
  {
    TestDirectory dir;
    const std::vector<SyntheticEntry> oldEntries = {{"pluto.dll", Bytes("working old DLL")},
                                                    {"pluto/pluto_infer.exe", Bytes("working old inference")},
                                                    {"pluto/pluto_weights.bin", Bytes("working old weights")}};
    const auto newEntries = GoodEntries();
    const auto plutoZip = TestZip(newEntries);
    PlutoRuntimeSpec plutoSpec = SpecFor(plutoZip, newEntries);
    plutoSpec.url = "synthetic-pluto";
    WritePayloads(dir, oldEntries);
    Write(dir.root / "Data" / "pluto" / "pluto-bridge.exe", "packaged bridge");
    Write(dir.root / "Data" / "pluto" / "bridge.log", "preserve log");
    Write(dir.root / "Data" / "pluto" / "pluto" / "pluto_config.json", "preserve config");
    Write(dir.root / "Data" / "pluto" / "pluto" / "bandit.json", "preserve bandit");
    const std::vector<uint8_t> wineArchive = Bytes("combined Wine archive");
    PlutoRuntimeSpec wineSpec = SpecFor(wineArchive, {});
    wineSpec.url = "synthetic-wine";
    const fs::path home = dir.root / "home";
    const fs::path target = AppManagedMetalSharpWineAppPath(home.c_str());
    const fs::path activeWine = target / "Contents" / "MacOS" / "wine";
    FakeDependencyTools fake(dir);
    std::vector<std::string> downloads;
    bool wineActiveBeforePluto = false, capabilitySawWine = false;
    TransportFn transport = [&](const PlutoRuntimeSpec &requested, ArchiveSink &sink, std::string *transportError) {
      downloads.push_back(requested.url);
      const bool pluto = requested.url == plutoSpec.url;
      if (pluto)
        wineActiveBeforePluto = fs::exists(activeWine);
      return Serve(pluto ? plutoZip : wineArchive)(requested, sink, transportError);
    };
    std::string error;
    ok = Check(InstallAppleSiliconRuntime(
                   plutoSpec, wineSpec, dir.runtime, home.c_str(), fake.tools, transport,
                   [&] { return activeWine.string(); },
                   [&]() -> const char * {
                     capabilitySawWine = fs::exists(activeWine);
                     return NULL;
                   },
                   CancelFn(), ProgressFn(), &error),
               error.c_str());
    ok = Check(downloads.size() == 2 && downloads[0] == wineSpec.url && downloads[1] == plutoSpec.url,
               "one action downloads Wine before Pluto") &&
         ok;
    ok = Check(wineActiveBeforePluto && capabilitySawWine,
               "Wine is active and inference capability is rechecked before Pluto replacement") &&
         ok;
    ok = Check(RuntimeReady(plutoSpec, dir.runtime, &error), "the combined action installs verified Pluto payloads") &&
         ok;
    ok = Check(MetalSharpWineError(activeWine.string()) == NULL,
               "dependency activation invalidates a cached missing-Wine probe") &&
         ok;
    ok = Check(Read(dir.root / "Data" / "pluto" / "bridge.log") == Bytes("preserve log") &&
                   Read(dir.root / "Data" / "pluto" / "pluto" / "pluto_config.json") == Bytes("preserve config") &&
                   Read(dir.root / "Data" / "pluto" / "pluto" / "bandit.json") == Bytes("preserve bandit"),
               "combined repair preserves logs, configuration and bandit state") &&
         ok;
  }
  {
    TestDirectory dir;
    const auto oldEntries = GoodEntries();
    WritePayloads(dir, oldEntries);
    Write(dir.root / "Data" / "pluto" / "pluto-bridge.exe", "packaged bridge");
    const auto plutoZip = TestZip(oldEntries);
    PlutoRuntimeSpec plutoSpec = SpecFor(plutoZip, oldEntries);
    plutoSpec.url = "synthetic-pluto";
    const std::vector<uint8_t> wineArchive = Bytes("bad Wine hash");
    PlutoRuntimeSpec wineSpec = SpecFor(wineArchive, {});
    wineSpec.url = "synthetic-wine";
    wineSpec.sha256 = std::string(64, '0');
    const fs::path home = dir.root / "home";
    const fs::path target = AppManagedMetalSharpWineAppPath(home.c_str());
    FakeDependencyTools fake(dir);
    int plutoDownloads = 0;
    TransportFn transport = [&](const PlutoRuntimeSpec &requested, ArchiveSink &sink, std::string *transportError) {
      if (requested.url == plutoSpec.url)
        ++plutoDownloads;
      return Serve(wineArchive)(requested, sink, transportError);
    };
    std::string error;
    ok = Check(!InstallAppleSiliconRuntime(
                   plutoSpec, wineSpec, dir.runtime, home.c_str(), fake.tools, transport,
                   [&] { return (target / "Contents" / "MacOS" / "wine").string(); },
                   []() -> const char * { return NULL; }, CancelFn(), ProgressFn(), &error),
               "a Wine hash failure refuses combined setup") &&
         ok;
    const PlutoRuntimeSpec oldSpec = SpecFor(TestZip(oldEntries), oldEntries);
    ok = Check(plutoDownloads == 0 && RuntimeReady(oldSpec, dir.runtime, &error),
               "Wine failure never downloads or replaces working Pluto payloads") &&
         ok;
  }
  {
    TestDirectory dir;
    const auto oldEntries = GoodEntries();
    WritePayloads(dir, oldEntries);
    Write(dir.root / "Data" / "pluto" / "pluto-bridge.exe", "packaged bridge");
    const auto plutoZip = TestZip(oldEntries);
    PlutoRuntimeSpec plutoSpec = SpecFor(plutoZip, oldEntries);
    plutoSpec.url = "synthetic-pluto";
    const std::vector<uint8_t> wineArchive = Bytes("post-install capability failure");
    PlutoRuntimeSpec wineSpec = SpecFor(wineArchive, {});
    wineSpec.url = "synthetic-wine";
    const fs::path home = dir.root / "home";
    const fs::path target = AppManagedMetalSharpWineAppPath(home.c_str());
    const fs::path activeWine = target / "Contents" / "MacOS" / "wine";
    FakeDependencyTools fake(dir);
    int plutoDownloads = 0;
    TransportFn transport = [&](const PlutoRuntimeSpec &requested, ArchiveSink &sink, std::string *transportError) {
      if (requested.url == plutoSpec.url)
        ++plutoDownloads;
      return Serve(wineArchive)(requested, sink, transportError);
    };
    std::string error;
    ok = Check(!InstallAppleSiliconRuntime(
                   plutoSpec, wineSpec, dir.runtime, home.c_str(), fake.tools, transport,
                   [&] { return activeWine.string(); },
                   []() -> const char * { return "synthetic inference rejection"; }, CancelFn(), ProgressFn(), &error) &&
                   error.find("no Pluto payloads were replaced") != std::string::npos,
               "post-install inference preflight remains fail-closed") &&
         ok;
    const PlutoRuntimeSpec oldSpec = SpecFor(TestZip(oldEntries), oldEntries);
    ok = Check(plutoDownloads == 0 && RuntimeReady(oldSpec, dir.runtime, &error) &&
                   ProbeMetalSharpWine(activeWine.string()) == NULL,
               "capability failure leaves old Pluto intact and only a complete verified Wine app active") &&
         ok;
  }
  {
    TestDirectory dir;
    const auto entries = GoodEntries();
    WritePayloads(dir, entries);
    Write(dir.root / "Data" / "pluto" / "pluto-bridge.exe", "packaged bridge");
    const auto plutoZip = TestZip(entries);
    PlutoRuntimeSpec plutoSpec = SpecFor(plutoZip, entries);
    plutoSpec.url = "synthetic-pluto";
    const std::vector<uint8_t> wineArchive = Bytes("dependency must be skipped");
    PlutoRuntimeSpec wineSpec = SpecFor(wineArchive, {});
    wineSpec.url = "synthetic-wine";
    const fs::path systemWine = MakeFakeMetalSharpWineAt(dir, dir.root / "elsewhere" / "MetalSharp-Wine.app");
    const fs::path home = dir.root / "home";
    const fs::path target = AppManagedMetalSharpWineAppPath(home.c_str());
    FakeDependencyTools fake(dir);
    int wineDownloads = 0;
    TransportFn transport = [&](const PlutoRuntimeSpec &requested, ArchiveSink &sink, std::string *transportError) {
      if (requested.url == wineSpec.url)
        ++wineDownloads;
      return Serve(plutoZip)(requested, sink, transportError);
    };
    std::string error;
    ok = Check(InstallAppleSiliconRuntime(
                   plutoSpec, wineSpec, dir.runtime, home.c_str(), fake.tools, transport,
                   [&] { return systemWine.string(); }, []() -> const char * { return NULL; },
                   CancelFn(), ProgressFn(), &error),
               "a valid selected MetalSharp runtime permits Pluto-only repair") &&
         ok;
    ok = Check(wineDownloads == 0 && fake.calls.empty() && !fs::exists(target),
               "a valid external runtime skips dependency download, extraction and app-managed activation") &&
         ok;
  }
  return ok;
}

bool TestMacWineCapability()
{
#if defined(__APPLE__)
  bool ok = true;
  const char *oldPath = getenv("PATH");
  const std::string savedPath = oldPath != NULL ? oldPath : "";
  const std::string trueExecutable = ResolveExecutableOnPath("true");
  std::cerr << "public true discovery: path='" << trueExecutable << "'\n";
  if (!Check(!trueExecutable.empty(), "the host has a true executable for public capability rejection"))
    return false;
  setenv("HYPERION_PLUTO_WINE", trueExecutable.c_str(), 1);
  const char *directError = PortPlutoInferenceCapabilityError();
  std::cerr << "public direct true capability: path='" << trueExecutable << "', error='"
            << (directError != NULL ? directError : "<accepted>") << "'\n";
  ok = Check(directError != NULL, "the public Mac capability rejects the discovered true executable") && ok;
  {
    setenv("PATH", ParentPath(trueExecutable).c_str(), 1);
    setenv("HYPERION_PLUTO_WINE", "true", 1);
    const std::string resolvedTrue = PortPlutoWineExecutable();
    const char *error = PortPlutoInferenceCapabilityError();
    std::cerr << "public bare true capability: path='" << resolvedTrue << "', error='"
              << (error != NULL ? error : "<accepted>") << "'\n";
    ok = Check(resolvedTrue == trueExecutable, "the public Mac capability resolves bare true to the discovered path") && ok;
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
#if defined(__aarch64__) || defined(__arm64__)
    const fs::path executable = MakeFakeMetalSharpWine(dir);
    setenv("HYPERION_PLUTO_WINE", executable.c_str(), 1);
    const char *error = PortPlutoInferenceCapabilityError();
    if (error != NULL)
      std::cerr << error << '\n';
    if (MetalSharpMacOsSupported())
      ok = Check(error == NULL, "the public Apple Silicon capability accepts a complete MetalSharp installation") && ok;
    else
      ok = Check(error != NULL && std::string(error).find("macOS 26.4") != std::string::npos,
                 "the public Apple Silicon capability enforces macOS 26.4 before Wine") &&
           ok;
    const fs::path generic = MakeFakeWine11(dir, "wine-11.21");
    setenv("HYPERION_PLUTO_WINE", generic.c_str(), 1);
    error = PortPlutoInferenceCapabilityError();
    if (MetalSharpMacOsSupported())
      ok = Check(error != NULL && std::string(error).find("Generic WineHQ") != std::string::npos,
                 "the public Apple Silicon capability rejects compatible-looking generic WineHQ") &&
           ok;
#else
    const fs::path executable = MakeFakeWine11(dir, "wine-11.21");
    setenv("HYPERION_PLUTO_WINE", executable.c_str(), 1);
    const char *error = PortPlutoInferenceCapabilityError();
    if (error != NULL)
      std::cerr << error << '\n';
    ok = Check(error == NULL, "the public Intel Mac capability accepts a valid Wine 11 unified installation") && ok;
#endif
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
  const PlutoRuntimeSpec &wine = OfficialMetalSharpSpec();
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
               "official weights SHA-256 and size") &&
         Check(wine.url == "https://github.com/metalsharp/MetalSharp-Wine/releases/download/v6.0/MetalSharp-Wine-6.0-arm64.zip",
               "official MetalSharp archive URL") &&
         Check(wine.cbArchive == 1356725682, "official MetalSharp exact byte count") &&
         Check(wine.sha256 == "25f7dff91000c03c835b800980878acb12c1f952a4b7853f77de7f21fb87046f" &&
                   wine.payloads.empty(),
               "official MetalSharp SHA-256 and archive-only specification");
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
  status.stage = "downloading-wine";
  status.cbTotal = OfficialMetalSharpSpec().cbArchive;
  const std::string wineProgress = PortPlutoInstallText(status);
  ok = Check(wineProgress.find("MetalSharp Wine 6.0") != std::string::npos &&
                 wineProgress.find("1293 MiB") != std::string::npos,
             "Wine download text names the dependency and its honest whole-MiB total") &&
       ok;
  status.stage = "extracting-wine";
  ok = Check(PortPlutoInstallText(status).find("ditto") != std::string::npos,
             "dependency extraction has an explicit non-percent stage") &&
       ok;
  status.stage = "validating-wine";
  ok = Check(PortPlutoInstallText(status).find("signature") != std::string::npos,
             "dependency validation text includes the code signature") &&
       ok;
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
  else if (scenario == "metalsharp-preflight")
    ok = TestMetalSharpWinePreflight();
  else if (scenario == "metalsharp-install-success")
    ok = TestMetalSharpInstallSuccess();
  else if (scenario == "metalsharp-install-validation")
    ok = TestMetalSharpInstallValidationFailures();
  else if (scenario == "metalsharp-install-rollback")
    ok = TestMetalSharpActivationRollback();
  else if (scenario == "metalsharp-combined")
    ok = TestAppleSiliconCombinedInstaller();
  else if (scenario == "mac-wine-capability")
    ok = TestMacWineCapability();
  else if (scenario == "no-sb-capability")
    ok = TestNoSbDownloadCapability();
  else
    return 2;
  return ok ? 0 : 1;
}
