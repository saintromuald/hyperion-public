// rt_launcher.cpp -- start the untouched 1.17 oracle with a list of DLLs injected,
// for replay test runs.
//
// It differs from tools/hookdll/oc117_launcher.cpp in three ways: it injects any
// number of DLLs in order (a payload, then the observer), it calls each DLL's
// named init export with the game's main thread id as the argument, and it can
// wait for the game to exit, killing it after a timeout.
//
// Usage:
//   rt_launcher.exe <starcraft.exe> [--dll <path>[@<export>]]... [--arg <word>]...
//                   [--cfg <observer.cfg>] [--timeout <secs>]
//
//   --dll      injected in the order given; the export defaults to oc_init
//   --arg      appended to the game's command line (e.g. nosound)
//   --cfg      exported to the game as OC_RT_CFG, which the observer reads
//   --timeout  wait for the game to exit; kill it after this many seconds
//
// Exit code: 0 the game exited, 1 a launch or injection failed, 3 timed out.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

struct Dll {
  std::string path;
  std::string fn;
};

static std::string parent_dir(const std::string &p) {
  size_t s = p.find_last_of("\\/");
  return s == std::string::npos ? std::string(".") : p.substr(0, s);
}

static uint32_t remote_call(HANDLE hProc, uint32_t addr, uint32_t arg, DWORD waitMs) {
  HANDLE h = CreateRemoteThread(hProc, NULL, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(addr),
                                reinterpret_cast<void *>(arg), 0, NULL);
  if (!h) return 0xFFFFFFFFu;
  DWORD r = WaitForSingleObject(h, waitMs);
  DWORD code = 0xFFFFFFFFu;
  if (r == WAIT_OBJECT_0) GetExitCodeThread(h, &code);
  CloseHandle(h);
  return code;
}

// LoadLibraryA in the target; returns the module base there, or 0.
static uint32_t inject(HANDLE hProc, const std::string &path) {
  char full[MAX_PATH];
  if (!GetFullPathNameA(path.c_str(), MAX_PATH, full, NULL)) return 0;
  size_t n = strlen(full) + 1;
  void *mem = VirtualAllocEx(hProc, NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!mem) return 0;
  WriteProcessMemory(hProc, mem, full, n, NULL);
  uint32_t ll = (uint32_t)(uintptr_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
  uint32_t base = remote_call(hProc, ll, (uint32_t)(uintptr_t)mem, 15000);
  VirtualFreeEx(hProc, mem, 0, MEM_RELEASE);
  return base == 0xFFFFFFFFu ? 0 : base;
}

// The export's RVA, read by mapping the DLL locally without running it.
static uint32_t export_rva(const std::string &path, const std::string &fn) {
  HMODULE h = LoadLibraryExA(path.c_str(), NULL, DONT_RESOLVE_DLL_REFERENCES);
  if (!h) return 0;
  FARPROC p = GetProcAddress(h, fn.c_str());
  uint32_t rva = p ? (uint32_t)((uintptr_t)p - (uintptr_t)h) : 0;
  FreeLibrary(h);
  return rva;
}

int main(int argc, char **argv) {
  std::string exe, cfg;
  std::vector<Dll> dlls;
  std::string args;
  DWORD timeout = 0;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--dll" && i + 1 < argc) {
      std::string v = argv[++i];
      Dll d;
      size_t at = v.rfind('@');
      d.path = at == std::string::npos ? v : v.substr(0, at);
      d.fn = at == std::string::npos ? "oc_init" : v.substr(at + 1);
      dlls.push_back(d);
    } else if (a == "--arg" && i + 1 < argc) {
      args += " ";
      args += argv[++i];
    } else if (a == "--cfg" && i + 1 < argc) {
      cfg = argv[++i];
    } else if (a == "--timeout" && i + 1 < argc) {
      timeout = strtoul(argv[++i], NULL, 10);
    } else if (exe.empty()) {
      exe = a;
    }
  }
  if (exe.empty()) {
    printf("usage: rt_launcher.exe <starcraft.exe> [--dll path[@export]]... [--arg w]... "
           "[--cfg file] [--timeout secs]\n");
    return 1;
  }
  if (!cfg.empty()) {
    char full[MAX_PATH];
    GetFullPathNameA(cfg.c_str(), MAX_PATH, full, NULL);
    SetEnvironmentVariableA("OC_RT_CFG", full);
  }

  std::string cmd = "\"" + exe + "\"" + args;
  std::vector<char> cmdBuf(cmd.begin(), cmd.end());
  cmdBuf.push_back(0);
  STARTUPINFOA si = {};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessA(exe.c_str(), cmdBuf.data(), NULL, NULL, FALSE, CREATE_SUSPENDED, NULL,
                      parent_dir(exe).c_str(), &si, &pi)) {
    printf("ERROR: CreateProcess failed (%lu)\n", GetLastError());
    return 1;
  }
  printf("PID=%lu TID=%lu\n", pi.dwProcessId, pi.dwThreadId);

  bool ok = true;
  for (size_t i = 0; i < dlls.size() && ok; ++i) {
    uint32_t base = inject(pi.hProcess, dlls[i].path);
    uint32_t rva = base ? export_rva(dlls[i].path, dlls[i].fn) : 0;
    if (!base || !rva) {
      printf("ERROR: inject %s (%s) failed: base=%08X rva=%08X\n", dlls[i].path.c_str(),
             dlls[i].fn.c_str(), base, rva);
      ok = false;
      break;
    }
    uint32_t r = remote_call(pi.hProcess, base + rva, pi.dwThreadId, 30000);
    printf("DLL %s @%08X %s -> %u\n", dlls[i].path.c_str(), base, dlls[i].fn.c_str(), r);
    if (r == 0xFFFFFFFFu) ok = false;
  }
  if (!ok) {
    TerminateProcess(pi.hProcess, 1);
    return 1;
  }
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);

  int rc = 0;
  if (timeout) {
    DWORD r = WaitForSingleObject(pi.hProcess, timeout * 1000);
    if (r == WAIT_TIMEOUT) {
      TerminateProcess(pi.hProcess, 3);
      printf("TIMEOUT after %lus, killed\n", timeout);
      rc = 3;
    } else {
      DWORD code = 0;
      GetExitCodeProcess(pi.hProcess, &code);
      printf("EXIT=%lu\n", code);
    }
  }
  CloseHandle(pi.hProcess);
  return rc;
}
