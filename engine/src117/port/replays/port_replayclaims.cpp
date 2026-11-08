// port_replayclaims.cpp -- create-exclusive claim files with bounded stale recovery.
#include "../../h/port_replayclaims.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#include "port_replaykraken.h"

#if defined(_WIN32)
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ReplayClaims {
namespace {
std::filesystem::path PathOf(const std::string &text)
{
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

FILE *OpenFile(const std::filesystem::path &path)
{
#if defined(_WIN32)
  return _wfopen(path.c_str(), L"rb");
#else
  return fopen(path.c_str(), "rb");
#endif
}
}

bool ClaimOwned(const std::string &path, std::string &owner, int64_t staleAfterSeconds, std::string *pError)
{
  owner.clear();
  const auto file = PathOf(path);
  std::error_code ec;
  std::string stale;
  if (std::filesystem::exists(file, ec)) {
    const auto age = std::filesystem::file_time_type::clock::now() - std::filesystem::last_write_time(file, ec);
    if (!ec && age < std::chrono::seconds(staleAfterSeconds)) {
      if (pError)
        *pError = "another live application instance holds this job";
      return false;
    }
    // Move the stale claim aside rather than deleting a path another process may
    // have just reclaimed. Only the process whose rename succeeds may continue.
    stale = path + ".stale-" + ReplayKraken::UuidNew();
    std::filesystem::rename(file, PathOf(stale), ec);
    if (ec) {
      if (pError)
        *pError = "another application instance reclaimed this job first";
      return false;
    }
  }
  owner = ReplayKraken::UuidNew();
#if defined(_WIN32)
  HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_HIDDEN, NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    if (pError)
      *pError = "another application instance claimed this job first";
    return false;
  }
  DWORD wrote = 0;
  const bool bOk = WriteFile(handle, owner.data(), static_cast<DWORD>(owner.size()), &wrote, NULL) &&
                   wrote == owner.size();
  CloseHandle(handle);
  if (bOk && !stale.empty())
    std::filesystem::remove(PathOf(stale), ec);
  return bOk;
#elif !defined(__EMSCRIPTEN__)
  const int fd = open(file.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) {
    if (pError)
      *pError = "another application instance claimed this job first";
    return false;
  }
  const ssize_t wrote = write(fd, owner.data(), owner.size());
  close(fd);
  const bool bOk = wrote == static_cast<ssize_t>(owner.size());
  if (bOk && !stale.empty())
    std::filesystem::remove(PathOf(stale), ec);
  return bOk;
#else
  return false;
#endif
}

bool Claim(const std::string &path, int64_t staleAfterSeconds, std::string *pError)
{
  std::string owner;
  return ClaimOwned(path, owner, staleAfterSeconds, pError);
}

bool OwnerMatches(const std::filesystem::path &file, const std::string &owner)
{
  if (owner.empty())
    return false;
  char current[128] = {};
  FILE *input = OpenFile(file);
  if (!input)
    return false;
  const size_t got = fread(current, 1, sizeof current - 1, input);
  fclose(input);
  return std::string(current, got) == owner;
}

bool Heartbeat(const std::string &path, const std::string &owner)
{
  const auto file = PathOf(path);
  if (!OwnerMatches(file, owner))
    return false;
  std::error_code ec;
  std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now(), ec);
  return !ec;
}

void Release(const std::string &path, const std::string &owner)
{
  if (!OwnerMatches(PathOf(path), owner))
    return;
  std::error_code ec;
  std::filesystem::remove(PathOf(path), ec);
}

}  // namespace ReplayClaims
