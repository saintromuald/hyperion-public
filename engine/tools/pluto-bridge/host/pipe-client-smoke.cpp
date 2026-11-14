// Focused Win32 Client::update-order probe for the patched-DLL smoke harness.
// It uses the real shared GameData layout but does not load BWAPI or a bot.
#include <BWAPI/Client/GameData.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

static bool ReadCode(HANDLE pipe, DWORD expected) {
  DWORD code = 0, count = 0;
  return ReadFile(pipe, &code, sizeof code, &count, NULL) && count == sizeof code &&
         code == expected;
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3)
    return 64;
  const bool disconnect_after_frame_zero = argc == 3 && std::strcmp(argv[2], "early") == 0;
  const bool send_bad_update_code = argc == 3 && std::strcmp(argv[2], "bad-code") == 0;
  const bool drop_final_request = argc == 3 && std::strcmp(argv[2], "drop-final-request") == 0;
  char name[128];
  std::snprintf(name, sizeof name, "\\\\.\\pipe\\bwapi_pipe_%s", argv[1]);
  HANDLE pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                            OPEN_EXISTING, 0, NULL);
  if (pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY) {
    if (WaitNamedPipeA(name, 10000))
      pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                         OPEN_EXISTING, 0, NULL);
  }
  if (pipe == INVALID_HANDLE_VALUE) {
    std::fprintf(stderr, "mock client: CreateFile(%s) failed %lu\n", name, GetLastError());
    return 2;
  }

  char mapping_name[128];
  std::snprintf(mapping_name, sizeof mapping_name, "Local\\bwapi_shared_memory_%s", argv[1]);
  HANDLE mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, mapping_name);
  if (mapping == NULL) {
    std::fprintf(stderr, "mock client: OpenFileMapping failed %lu\n", GetLastError());
    CloseHandle(pipe);
    return 3;
  }
  BWAPI::GameData *data = static_cast<BWAPI::GameData *>(
      MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(BWAPI::GameData)));
  if (data == NULL) {
    std::fprintf(stderr, "mock client: MapViewOfFile failed %lu\n", GetLastError());
    CloseHandle(mapping);
    CloseHandle(pipe);
    return 4;
  }

  // Client::connect's code 2 carries no event generation.
  if (!ReadCode(pipe, 2)) {
    std::fprintf(stderr, "mock client: connect code 2 failed %lu\n", GetLastError());
    return 5;
  }
  std::fprintf(stderr, "mock client: connect released\n");

  if (send_bad_update_code) {
    // A complete but invalid pipe request is an unexpected host/protocol
    // failure, not a graceful post-publication client disconnect.
    DWORD code = 7, count = 0;
    const BOOL written = WriteFile(pipe, &code, sizeof code, &count, NULL);
    std::fprintf(stderr, "mock client: sending invalid update code\n");
    UnmapViewOfFile(data);
    CloseHandle(mapping);
    CloseHandle(pipe);
    return written && count == sizeof code ? 0 : 8;
  }

  for (int expected_frame = 0; expected_frame < 2; ++expected_frame) {
    if (drop_final_request && expected_frame == 1) {
      // The server holds this accepted final release briefly in the boundary
      // smoke.  Closing now makes its already-pending code-2 WriteFile fail
      // while finalization is waiting for exactly that release to settle.
      DWORD code = 1, count = 0;
      const BOOL written = WriteFile(pipe, &code, sizeof code, &count, NULL);
      std::fprintf(stderr, "mock client: dropping accepted final update request\n");
      UnmapViewOfFile(data);
      CloseHandle(mapping);
      CloseHandle(pipe);
      return written && count == sizeof code ? 0 : 8;
    }
    // This is Client::update's ordering: request/acknowledge first, then wait
    // for code 2, then consume shared events.  The next iteration's code 1 is
    // deliberately sent immediately, before the host can publish the next
    // frame, to prove that ServePipe waits for a new ready generation.
    DWORD code = 1, count = 0;
    if (!WriteFile(pipe, &code, sizeof code, &count, NULL) || count != sizeof code ||
        !ReadCode(pipe, 2)) {
      std::fprintf(stderr, "mock client: update handshake failed %lu\n", GetLastError());
      return 6;
    }
    const int observed_frame = data->frameCount;
    const int observed_event = data->eventCount > 0 ? data->events[0].type : -1;
    std::fprintf(stderr, "mock client: consumed frame %d event %d\n",
                 observed_frame, observed_event);
    if (observed_frame != expected_frame) {
      std::fprintf(stderr, "mock client: expected frame %d, got %d\n",
                   expected_frame, observed_frame);
      return 7;
    }
    if (disconnect_after_frame_zero) {
      std::fprintf(stderr, "mock client: intentional early disconnect\n");
      break;
    }
  }

  // Closing after consumption must release any host writer and permit clean
  // bridge shutdown, just as a disconnected BWAPI client does.
  UnmapViewOfFile(data);
  CloseHandle(mapping);
  CloseHandle(pipe);
  return 0;
}
