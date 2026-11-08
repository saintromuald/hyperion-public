// pluto_infer wrapper: keep the engine's stdout/stderr out of the bridge's
// reply pipe (they interleave with HXRP replies in the real game).
#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  freopen("pluto_infer_stdout.log", "ab", stdout);
  freopen("pluto_infer_stderr.log", "ab", stderr);
  char module[MAX_PATH];
  GetModuleFileNameA(NULL, module, sizeof module);
  std::string dir(module);
  const size_t slash = dir.find_last_of("\\/");
  if (slash != std::string::npos)
    dir.resize(slash + 1);
  std::string cmd = "\"" + dir + "pluto_infer_real.exe\"";
  for (int i = 1; i < argc; ++i) {
    cmd += " \"";
    cmd += argv[i];
    cmd += "\"";
  }
  fprintf(stderr, "[wrapper] %s\n", cmd.c_str());
  STARTUPINFOA si;
  memset(&si, 0, sizeof si);
  si.cb = sizeof si;
  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof pi);
  std::vector<char> buf(cmd.begin(), cmd.end());
  buf.push_back('\0');
  if (!CreateProcessA(NULL, buf.data(), NULL, NULL, TRUE, 0, NULL, dir.c_str(), &si, &pi)) {
    fprintf(stderr, "[wrapper] CreateProcess failed: %lu\n", GetLastError());
    return 2;
  }
  CloseHandle(pi.hThread);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  return static_cast<int>(code);
}
