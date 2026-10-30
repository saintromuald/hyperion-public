// port_sysinfo.cpp -- what the program is and what it runs on (port_sysinfo.h): the build, the
// system and the run, gathered once at the start, and the parts the start learns later. The
// start-up log writes them at its head and the crash report in its own, so a report from a
// player says which build it was and what it ran on.
#include "port_sysinfo.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <mutex>
#include <thread>
#include <vector>
#include <SDL3/SDL.h>
#include "../platform/port_paths.h"     // PortProgramFolder, PortDataFolder
#include "../platform/port_settings.h"  // PortSettingString, PortSettingNumber
#include "../../../hyperion/version.h"
#include "../../h/hxsim.h"  // HX_SIM_REVISION
#if __has_include("build_id.h")
#include "build_id.h"  // the commit and the build's number, written by the build
#endif
#ifndef HYPERION_BUILD_NUMBER
#define HYPERION_BUILD_NUMBER 0
#endif
#ifndef HYPERION_COMMIT
#define HYPERION_COMMIT ""
#endif
#ifndef HYPERION_BRANCH
#define HYPERION_BRANCH ""
#endif
#if defined(_WIN32)
#include <intrin.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#elif defined(__linux__)
#include <link.h>
#endif
#if defined(__x86_64__)
#include <cpuid.h>
#endif
extern char **environ;
#endif

namespace {

struct Fact {
  std::string part, name, value;
};

std::mutex g_lock;
std::vector<Fact> g_facts;
char g_szText[24576];
char g_szJson[32768];

std::string Format(const char *pszFormat, va_list args)
{
  char sz[1024];
  va_list again;
  va_copy(again, args);
  const int n = vsnprintf(sz, sizeof sz, pszFormat, args);
  if (n < 0) {
    va_end(again);
    return "";
  }
  if (static_cast<size_t>(n) < sizeof sz) {
    va_end(again);
    return sz;
  }
  std::string s(static_cast<size_t>(n) + 1, '\0');
  vsnprintf(&s[0], s.size(), pszFormat, again);
  va_end(again);
  s.resize(static_cast<size_t>(n));
  return s;
}

std::string TextOf(void)
{
  std::string s;
  for (const Fact &f : g_facts) {
    char szPart[16];
    snprintf(szPart, sizeof szPart, "%-10s", f.part.c_str());
    s += szPart;
    s += f.name + ": " + f.value + "\r\n";
  }
  return s;
}

std::string JsonOf(void)
{
  std::string s = "{";
  std::vector<std::string> parts;
  for (const Fact &f : g_facts) {
    bool bSeen = false;
    for (const std::string &p : parts)
      bSeen = bSeen || p == f.part;
    if (!bSeen)
      parts.push_back(f.part);
  }
  for (size_t i = 0; i < parts.size(); i++) {
    s += (i > 0 ? ", " : "") + PortSysInfoJsonString(parts[i].c_str()) + ": {";
    bool bFirst = true;
    for (const Fact &f : g_facts) {
      if (f.part != parts[i])
        continue;
      s += (bFirst ? "" : ", ") + PortSysInfoJsonString(f.name.c_str()) + ": " + PortSysInfoJsonString(f.value.c_str());
      bFirst = false;
    }
    s += "}";
  }
  return s + "}";
}

void Copy(char *pszTo, size_t cb, const std::string &s)
{
  const size_t n = s.size() < cb - 1 ? s.size() : cb - 1;
  memcpy(pszTo, s.data(), n);
  pszTo[n] = 0;
}

void SetLocked(const char *pszPart, const char *pszName, const std::string &value)
{
  bool bFound = false;
  for (Fact &f : g_facts)
    if (f.part == pszPart && f.name == pszName) {
      f.value = value;
      bFound = true;
    }
  if (!bFound)
    g_facts.push_back({pszPart, pszName, value});
  Copy(g_szText, sizeof g_szText, TextOf());
  Copy(g_szJson, sizeof g_szJson, JsonOf());
}

void Set(const char *pszPart, const char *pszName, const std::string &value)
{
  std::lock_guard<std::mutex> hold(g_lock);
  SetLocked(pszPart, pszName, value);
}

std::string Trim(std::string s)
{
  while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r' || s.back() == '\t'))
    s.pop_back();
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
    i++;
  return s.substr(i);
}

std::string Gb(unsigned long long cb)
{
  char sz[32];
  snprintf(sz, sizeof sz, "%.1f GB", static_cast<double>(cb) / (1024.0 * 1024.0 * 1024.0));
  return sz;
}

// The processor's own name, from its CPUID brand string where it has one.
std::string CpuName(void)
{
#if defined(_WIN32) && (defined(_M_X64) || defined(_M_IX86))
  int a[4];
  __cpuid(a, 0x80000000);
  if (static_cast<unsigned>(a[0]) >= 0x80000004u) {
    char sz[49] = {};
    for (int i = 0; i < 3; i++)
      __cpuid(reinterpret_cast<int *>(sz + 16 * i), 0x80000002 + i);
    return Trim(sz);
  }
#elif defined(__APPLE__)
  char sz[256] = "";
  size_t cb = sizeof sz;
  if (sysctlbyname("machdep.cpu.brand_string", sz, &cb, NULL, 0) == 0)
    return sz;
#elif defined(__x86_64__)
  unsigned a[12] = {};
  if (__get_cpuid_max(0x80000000u, NULL) >= 0x80000004u) {
    for (unsigned i = 0; i < 3; i++)
      __get_cpuid(0x80000002u + i, &a[4 * i], &a[4 * i + 1], &a[4 * i + 2], &a[4 * i + 3]);
    char sz[49] = {};
    memcpy(sz, a, 48);
    return Trim(sz);
  }
#else
  // an ARM board names itself in the device tree, Linux's /proc/cpuinfo seldom does
  if (FILE *f = fopen("/sys/firmware/devicetree/base/model", "rb")) {
    char sz[256] = "";
    const size_t n = fread(sz, 1, sizeof sz - 1, f);
    fclose(f);
    sz[n] = 0;
    if (sz[0] != 0)
      return sz;
  }
  if (FILE *f = fopen("/proc/cpuinfo", "rb")) {
    char sz[512];
    std::string name;
    while (fgets(sz, sizeof sz, f) != NULL && name.empty())
      if (strncmp(sz, "model name", 10) == 0 || strncmp(sz, "Hardware", 8) == 0)
        if (const char *p = strchr(sz, ':'))
          name = Trim(p + 1);
    fclose(f);
    return name;
  }
#endif
  return "";
}

#if defined(_WIN32)
std::string RegString(const char *pszName)
{
  char sz[256] = "";
  DWORD cb = sizeof sz;
  if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", pszName, RRF_RT_REG_SZ,
                   NULL, sz, &cb) != ERROR_SUCCESS)
    return "";
  return sz;
}

DWORD RegNumber(const char *pszName)
{
  DWORD d = 0, cb = sizeof d;
  if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", pszName, RRF_RT_REG_DWORD,
                   NULL, &d, &cb) != ERROR_SUCCESS)
    return 0;
  return d;
}

// The system's version as it is, which GetVersionEx gives only to a program whose manifest
// names it.
std::string OsName(void)
{
  typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW *);
  OSVERSIONINFOW v = {};
  v.dwOSVersionInfoSize = sizeof v;
  if (RtlGetVersionFn pfn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion")))
    pfn(&v);
  std::string name = RegString("ProductName");
  // Windows 11 still names itself Windows 10 there
  if (v.dwBuildNumber >= 22000 && name.compare(0, 10, "Windows 10") == 0)
    name.replace(0, 10, "Windows 11");
  char sz[160];
  snprintf(sz, sizeof sz, "%s%s%s (%lu.%lu.%lu.%lu)", name.c_str(), RegString("DisplayVersion").empty() ? "" : ", ",
           RegString("DisplayVersion").c_str(), v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber,
           RegNumber("UBR"));
  std::string s = sz;
  // Wine says so in ntdll, and a report from it is read differently
  typedef const char *(CDECL * WineVersionFn)(void);
  if (WineVersionFn pfn = reinterpret_cast<WineVersionFn>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version")))
    s += std::string(", under Wine ") + pfn();
  return s;
}

// The PDB the exe names, as a symbol store files it: name, GUID and age, so a report finds the
// symbols of its own build.
std::string Symbols(void)
{
  const BYTE *base = reinterpret_cast<const BYTE *>(GetModuleHandleA(NULL));
  if (base == NULL)
    return "";
  const IMAGE_NT_HEADERS *nt =
      reinterpret_cast<const IMAGE_NT_HEADERS *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
  const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
  const IMAGE_DEBUG_DIRECTORY *p = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY *>(base + dir.VirtualAddress);
  for (DWORD i = 0; dir.VirtualAddress != 0 && i < dir.Size / sizeof *p; i++) {
    if (p[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW || p[i].AddressOfRawData == 0)
      continue;
    struct Rsds {
      DWORD dwSig;
      GUID guid;
      DWORD dwAge;
      char szPath[1];
    };
    const Rsds *r = reinterpret_cast<const Rsds *>(base + p[i].AddressOfRawData);
    if (r->dwSig != 0x53445352)  // "RSDS"
      continue;
    const char *pszName = r->szPath;
    for (const char *q = r->szPath; *q; q++)
      if (*q == '\\' || *q == '/')
        pszName = q + 1;
    char sz[160];
    const GUID &g = r->guid;
    snprintf(sz, sizeof sz, "%s %08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%lX", pszName, g.Data1, g.Data2, g.Data3,
             g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7], r->dwAge);
    return sz;
  }
  return "";
}
#else
std::string OsName(void)
{
  struct utsname u;
  if (uname(&u) != 0)
    return "";
#if defined(__APPLE__)
  char szVersion[64] = "", szBuild[64] = "";
  size_t cb = sizeof szVersion;
  sysctlbyname("kern.osproductversion", szVersion, &cb, NULL, 0);
  cb = sizeof szBuild;
  sysctlbyname("kern.osversion", szBuild, &cb, NULL, 0);
  return std::string("macOS ") + szVersion + " (" + szBuild + "), " + u.machine;
#else
  std::string name;
  if (FILE *f = fopen("/etc/os-release", "rb")) {
    char sz[512];
    while (fgets(sz, sizeof sz, f) != NULL)
      if (strncmp(sz, "PRETTY_NAME=", 12) == 0) {
        name = Trim(sz + 12);
        if (name.size() >= 2 && name.front() == '"')
          name = name.substr(1, name.size() - 2);
      }
    fclose(f);
  }
  std::string s = (name.empty() ? std::string(u.sysname) : name) + ", kernel " + u.release + ", " + u.machine;
  // the desktop SDL's video meets
  const char *pszSession = getenv("XDG_SESSION_TYPE");
  s += std::string(", ") + (pszSession != NULL ? pszSession : "no session type");
  s += getenv("WAYLAND_DISPLAY") != NULL ? ", Wayland" : "";
  s += getenv("DISPLAY") != NULL ? ", X11" : "";
  return s;
#endif
}

#if defined(__linux__)
// The GNU build id in the program's notes, as a hex string.
int OnProgram(struct dl_phdr_info *pInfo, size_t, void *pData)
{
  std::string *pId = static_cast<std::string *>(pData);
  for (int i = 0; i < pInfo->dlpi_phnum; i++) {
    const ElfW(Phdr) &ph = pInfo->dlpi_phdr[i];
    if (ph.p_type != PT_NOTE)
      continue;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(pInfo->dlpi_addr + ph.p_vaddr);
    const unsigned char *pEnd = p + ph.p_memsz;
    while (p + sizeof(ElfW(Nhdr)) <= pEnd) {
      const ElfW(Nhdr) *n = reinterpret_cast<const ElfW(Nhdr) *>(p);
      const unsigned char *pName = p + sizeof *n;
      const unsigned char *pDesc = pName + ((n->n_namesz + 3) & ~3u);
      if (pDesc + n->n_descsz > pEnd)
        break;
      if (n->n_type == NT_GNU_BUILD_ID && n->n_namesz == 4 && memcmp(pName, "GNU", 4) == 0) {
        char sz[3];
        for (unsigned k = 0; k < n->n_descsz; k++) {
          snprintf(sz, sizeof sz, "%02x", pDesc[k]);
          *pId += sz;
        }
        return 1;
      }
      p = pDesc + ((n->n_descsz + 3) & ~3u);
    }
  }
  return 1;  // the first module is the program
}
#endif

// The program's file name and the id its kept copy has too: the GNU build id on Linux, the
// Mach-O UUID on a Mac. A report finds the symbols of its own build by it (tools/crash_symbols.py).
std::string Symbols(void)
{
#if defined(__linux__)
  std::string id;
  dl_iterate_phdr(OnProgram, &id);
  char szExe[1024];
  const ssize_t cb = readlink("/proc/self/exe", szExe, sizeof szExe - 1);
  szExe[cb > 0 ? cb : 0] = 0;
#elif defined(__APPLE__)
  std::string id;
  const mach_header_64 *h = reinterpret_cast<const mach_header_64 *>(_dyld_get_image_header(0));
  const char *p = reinterpret_cast<const char *>(h + 1);
  for (uint32_t i = 0; h != NULL && i < h->ncmds && id.empty(); i++) {
    const load_command *lc = reinterpret_cast<const load_command *>(p);
    if (lc->cmd == LC_UUID) {
      char sz[3];
      for (int k = 0; k < 16; k++) {
        snprintf(sz, sizeof sz, "%02X", reinterpret_cast<const uuid_command *>(lc)->uuid[k]);
        id += sz;
      }
    }
    p += lc->cmdsize;
  }
  char szExe[1024];
  uint32_t cbExe = sizeof szExe;
  if (_NSGetExecutablePath(szExe, &cbExe) != 0)
    szExe[0] = 0;
#else
  std::string id;
  const char szExe[] = "";
#endif
  if (id.empty())
    return "";
  const char *pszName = strrchr(szExe, '/');
  return std::string(pszName != NULL ? pszName + 1 : "Hyperion") + " " + id;
}
#endif

std::string Memory(void)
{
#if defined(_WIN32)
  MEMORYSTATUSEX m = {};
  m.dwLength = sizeof m;
  if (!GlobalMemoryStatusEx(&m))
    return "";
  return Gb(m.ullTotalPhys) + ", " + Gb(m.ullAvailPhys) + " free";
#elif defined(__APPLE__)
  unsigned long long cb = 0;
  size_t n = sizeof cb;
  if (sysctlbyname("hw.memsize", &cb, &n, NULL, 0) != 0)
    return "";
  return Gb(cb);
#else
  unsigned long long total = 0, available = 0;
  if (FILE *f = fopen("/proc/meminfo", "rb")) {
    char sz[256];
    while (fgets(sz, sizeof sz, f) != NULL) {
      unsigned long long kb = 0;
      if (sscanf(sz, "MemTotal: %llu", &kb) == 1)
        total = kb * 1024;
      else if (sscanf(sz, "MemAvailable: %llu", &kb) == 1)
        available = kb * 1024;
    }
    fclose(f);
  }
  return total == 0 ? "" : Gb(total) + ", " + Gb(available) + " free";
#endif
}

std::string Locale(void)
{
#if defined(_WIN32)
  wchar_t wsz[LOCALE_NAME_MAX_LENGTH] = L"";
  char sz[LOCALE_NAME_MAX_LENGTH] = "";
  GetUserDefaultLocaleName(wsz, LOCALE_NAME_MAX_LENGTH);
  WideCharToMultiByte(CP_UTF8, 0, wsz, -1, sz, sizeof sz, NULL, NULL);
  char szOut[96];
  snprintf(szOut, sizeof szOut, "%s, code page %u", sz, GetACP());
  return szOut;
#else
  const char *psz = getenv("LC_ALL");
  if (psz == NULL || psz[0] == 0)
    psz = getenv("LANG");
  return psz != NULL ? psz : "";
#endif
}

// The variables and options that carry a key, a password or a token, which a report leaves out:
// it is text a player pastes where others read it.
const char *const kSecretVars[] = {"HYPERION_LAN_KEY", "HYPERION_ROOM_KEY", "HYPERION_ROOM_PASSWORD",
                                   "HYPERION_CHILD_TOKEN", "HYPERION_CONTROL_KEY"};
const char *const kSecretOptions[] = {"--room-key", "--room-password", "--lan-copy", "--lan-stream", "--lan-tab",
                                      "--lab-worker"};

// A command line with those options' values as "(hidden)", whether given as "--opt value" or "--opt=value".
std::string HideSecrets(std::string s)
{
  for (const char *pszOpt : kSecretOptions) {
    const size_t n = strlen(pszOpt);
    for (size_t at = s.find(pszOpt); at != std::string::npos; at = s.find(pszOpt, at + n)) {
      size_t v = at + n;
      if (v >= s.size() || (s[v] != ' ' && s[v] != '='))
        continue;
      while (v < s.size() && (s[v] == ' ' || s[v] == '='))
        ++v;
      size_t end = v;
      if (end < s.size() && s[end] == '"')
        end = s.find('"', end + 1) == std::string::npos ? s.size() : s.find('"', end + 1) + 1;
      else
        while (end < s.size() && s[end] != ' ')
          ++end;
      s.replace(v, end - v, "(hidden)");
    }
  }
  return s;
}

// HYPERION_* and the few others that change how the program starts, as the process has them.
std::string Environment(void)
{
  std::string s;
  auto add = [&](const char *psz) {
    if (strncmp(psz, "HYPERION_", 9) != 0 && strncmp(psz, "SDL_", 4) != 0)
      return;
    for (const char *pszVar : kSecretVars) {
      const size_t n = strlen(pszVar);
      if (strncmp(psz, pszVar, n) == 0 && psz[n] == '=') {
        s += (s.empty() ? "" : "; ") + std::string(pszVar) + "=(hidden)";
        return;
      }
    }
    s += (s.empty() ? "" : "; ") + PortSysInfoMask(psz);
  };
#if defined(_WIN32)
  if (char *pBlock = GetEnvironmentStringsA()) {
    for (const char *p = pBlock; *p != 0; p += strlen(p) + 1)
      add(p);
    FreeEnvironmentStringsA(pBlock);
  }
#else
  for (char **pp = environ; pp != NULL && *pp != NULL; pp++)
    add(*pp);
#endif
  return s.empty() ? "(none)" : s;
}

// The settings that decide how the program starts and draws, by name; not the profile, which
// is the player's name.
std::string Settings(void)
{
  static const char *const kNames[] = {
      "data", "classic", "remastered", "renderer", "display", "monitor", "monitorIndex", "width", "height", "view",
      "viewWide", "vsync", "frameRate", "frameTiming", "fullColour", "art", "menus", "menuQuality", "mod", "modsFolder",
      "rules", "gameCores", "workerThreads", "hardwareCursor", "runInBackground", "hdLighting", "effects",
      "consoleLayout", "consoleSkin", "loopRest", "framesAhead", "zoomMax", "smoothCamera", "discord"};
  std::string s;
  for (const char *pszName : kNames) {
    char sz[512];
    double d = 0.0;
    std::string value;
    if (PortSettingString("hyperion", pszName, sz, sizeof sz))
      value = PortSysInfoMask(sz);
    else if (PortSettingNumber("hyperion", pszName, &d)) {
      snprintf(sz, sizeof sz, "%g", d);
      value = sz;
    } else
      continue;
    s += (s.empty() ? "" : "; ") + std::string(pszName) + "=" + value;
  }
  return s.empty() ? "(none kept)" : s;
}

}  // namespace

std::string PortSysInfoMask(const char *psz)
{
  std::string s = HideSecrets(psz != NULL ? psz : "");
#if defined(_WIN32)
  char szHome[MAX_PATH];
  const DWORD n = GetEnvironmentVariableA("USERPROFILE", szHome, sizeof szHome);
  const char *pszAs = "%USERPROFILE%";
  if (n == 0 || n >= sizeof szHome)
    return s;
  const std::string homes[] = {szHome};
#else
  const char *pszHome = getenv("HOME");
  const char *pszAs = "~";
  if (pszHome == NULL || pszHome[0] == 0 || strcmp(pszHome, "/") == 0)
    return s;
  // the folder as the system names it, and as the Win32 layer does (Z:\home\...)
  std::string win = std::string("Z:") + pszHome;
  for (char &c : win)
    c = c == '/' ? '\\' : c;
  const std::string homes[] = {pszHome, win};
#endif
  // no SDL here: a start that cannot load SDL3.dll writes paths too
  for (const std::string &home : homes) {
    auto same = [&](size_t at) {
      for (size_t i = 0; i < home.size(); i++) {
        char a = s[at + i], b = home[i];
        a = a >= 'A' && a <= 'Z' ? static_cast<char>(a - 'A' + 'a') : a;
        b = b >= 'A' && b <= 'Z' ? static_cast<char>(b - 'A' + 'a') : b;
        if (a != b)
          return false;
      }
      return true;
    };
    for (size_t at = 0; at + home.size() <= s.size();) {
      if (same(at)) {
        s.replace(at, home.size(), pszAs);
        at += strlen(pszAs);
      } else {
        at++;
      }
    }
  }
  return s;
}

std::string PortSysInfoJsonString(const char *psz)
{
  std::string s = "\"";
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(psz != NULL ? psz : ""); *p; p++) {
    if (*p == '"' || *p == '\\') {
      s += '\\';
      s += static_cast<char>(*p);
    } else if (*p == '\n') {
      s += "\\n";
    } else if (*p == '\r') {
      s += "\\r";
    } else if (*p == '\t') {
      s += "\\t";
    } else if (*p < 0x20) {
      char sz[8];
      snprintf(sz, sizeof sz, "\\u%04x", *p);
      s += sz;
    } else {
      s += static_cast<char>(*p);
    }
  }
  return s + "\"";
}

void PortSysInfoGather(bool bSdl)
{
  char sz[512];
#if defined(_WIN64) || defined(__x86_64__)
  const char *pszArch = "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  const char *pszArch = "arm64";
#elif defined(__EMSCRIPTEN__)
  const char *pszArch = "wasm";
#else
  const char *pszArch = "x86";
#endif
  snprintf(sz, sizeof sz, "%s, build %d, %s, simulation r%d", HYPERION_FILE_VERSION_STR, HYPERION_BUILD_NUMBER,
           pszArch, HX_SIM_REVISION);
  Set("build", "version", sz);
  if (HYPERION_COMMIT[0] != 0)
    Set("build", "commit", std::string(HYPERION_COMMIT) + " on " + HYPERION_BRANCH);
#if defined(_MSC_VER)
  snprintf(sz, sizeof sz, "MSVC %d, %s %s", _MSC_FULL_VER, __DATE__, __TIME__);
#else
  snprintf(sz, sizeof sz, "%s, %s %s", __VERSION__, __DATE__, __TIME__);
#endif
  Set("build", "compiled", sz);
  const std::string symbols = Symbols();
  if (!symbols.empty())
    Set("build", "symbols", symbols);

  Set("system", "os", OsName());
  const std::string cpu = CpuName();
  snprintf(sz, sizeof sz, "%s%s%u threads", cpu.c_str(), cpu.empty() ? "" : ", ", std::thread::hardware_concurrency());
  Set("system", "cpu", sz);
  Set("system", "memory", Memory());
  Set("system", "locale", Locale());
  if (bSdl) {
    const int nSdl = SDL_GetVersion();
    snprintf(sz, sizeof sz, "%d.%d.%d (built with %d.%d.%d)", SDL_VERSIONNUM_MAJOR(nSdl), SDL_VERSIONNUM_MINOR(nSdl),
             SDL_VERSIONNUM_MICRO(nSdl), SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
    Set("system", "sdl", sz);
  } else {
    Set("system", "sdl", "not loaded");
  }

  Set("run", "command line", PortSysInfoMask(GetCommandLineA()));
  Set("run", "program folder", PortSysInfoMask(PortProgramFolder()));
  Set("run", "data folder", PortSysInfoMask(PortDataFolder()));
  char szWork[MAX_PATH] = "";
  GetCurrentDirectoryA(sizeof szWork, szWork);
  Set("run", "working folder", PortSysInfoMask(szWork));
  Set("run", "environment", Environment());
  Set("run", "settings", Settings());
}

void PortSysInfoSet(const char *pszPart, const char *pszName, const char *pszFormat, ...)
{
  va_list args;
  va_start(args, pszFormat);
  const std::string value = Format(pszFormat, args);
  va_end(args);
  Set(pszPart, pszName, value);
}

std::string PortSysInfoText(void)
{
  std::lock_guard<std::mutex> hold(g_lock);
  return TextOf();
}

std::string PortSysInfoJson(void)
{
  std::lock_guard<std::mutex> hold(g_lock);
  return JsonOf();
}

const char *PortSysInfoTextNow(void) { return g_szText; }

const char *PortSysInfoJsonNow(void) { return g_szJson[0] != 0 ? g_szJson : "{}"; }
