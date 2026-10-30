// port_posix.cpp -- the Win32 functions of src117/compat/windows.h, off Windows:
// files, time, threads, events, memory and the rest from POSIX and SDL under Win32's names, so
// 1.17's code and the port's run unchanged (docs/modules/platform.md, "What port_posix.cpp
// stands in for"). On Windows it is empty.
//
// A path is made the file system's by OcPath (a drive letter the root, '/' for separators, a
// name not there in its case searched for in any case), and one given to the engine is in
// Wine's form, the root as drive Z: (WinPath), since 1.17's code looks for the backslash to find
// a file's folder; the port's own calls into SDL and the archive libraries take PortNativePath.
// A HANDLE is an object of this file, and every waitable object is guarded by one lock and one
// condition, which a change to any of them wakes. LoadLibraryA is dlopen (HYPERION_DLLS), and
// LOAD_LIBRARY_AS_DATAFILE this file's own reader of PE resources.
#if !defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>  // PATH_MAX
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#if defined(__linux__)
#include <sys/sysinfo.h>
#include <sys/syscall.h>
#endif
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <emscripten/fiber.h>
#include <emscripten/heap.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>  // _NSGetExecutablePath
#include <mach/mach.h>    // host_statistics64
#include <sys/sysctl.h>
#endif
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>
#include <SDL3/SDL.h>
#include "port_posix.h"
#include "port_paths.h"  // PortProgramFolder
#include "port_winmsg.h"
#include "../tools/port_crash.h"  // PortCrashRaise

extern char **environ;

// ---------------------------------------------------------------------------
// errors
// ---------------------------------------------------------------------------
static thread_local DWORD t_dwLastError;

DWORD __stdcall GetLastError(void) { return t_dwLastError; }
void __stdcall SetLastError(DWORD dwErr) { t_dwLastError = dwErr; }

static DWORD ErrorFromErrno(int e)
{
  switch (e) {
  case 0: return ERROR_SUCCESS;
  case ENOENT: return ERROR_FILE_NOT_FOUND;
  case ENOTDIR: return ERROR_PATH_NOT_FOUND;
  case EACCES:
  case EPERM:
  case EROFS: return ERROR_ACCESS_DENIED;
  case EEXIST: return ERROR_ALREADY_EXISTS;
  case ENOTEMPTY: return ERROR_DIR_NOT_EMPTY;
  case ENOSPC: return ERROR_DISK_FULL;
  case EMFILE:
  case ENFILE: return ERROR_TOO_MANY_OPEN_FILES;
  case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
  case EINVAL: return ERROR_INVALID_PARAMETER;
  case EBADF: return ERROR_INVALID_HANDLE;
  case ENAMETOOLONG: return ERROR_INVALID_NAME;
  case EBUSY: return ERROR_SHARING_VIOLATION;
  default: return ERROR_INVALID_FUNCTION;
  }
}

static void FailErrno(void) { t_dwLastError = ErrorFromErrno(errno); }

DWORD __stdcall FormatMessageA(DWORD dwFlags, LPCVOID, DWORD dwMessageId, DWORD, LPSTR lpBuffer, DWORD nSize,
                               va_list *)
{
  if ((dwFlags & FORMAT_MESSAGE_FROM_SYSTEM) == 0 || lpBuffer == NULL || nSize == 0 ||
      (dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER) != 0)
    return 0;
  const char *psz = NULL;
  switch (dwMessageId) {
  case ERROR_FILE_NOT_FOUND: psz = "The system cannot find the file specified."; break;
  case ERROR_PATH_NOT_FOUND: psz = "The system cannot find the path specified."; break;
  case ERROR_ACCESS_DENIED: psz = "Access is denied."; break;
  case ERROR_ALREADY_EXISTS: psz = "Cannot create a file when that file already exists."; break;
  case ERROR_DISK_FULL: psz = "There is not enough space on the disk."; break;
  default: break;
  }
  const int n = psz ? snprintf(lpBuffer, nSize, "%s\r\n", psz) : snprintf(lpBuffer, nSize, "Error %u.\r\n", dwMessageId);
  return n > 0 && static_cast<DWORD>(n) < nSize ? static_cast<DWORD>(n) : 0;
}

// ---------------------------------------------------------------------------
// paths
// ---------------------------------------------------------------------------
// A name in pszDir in any case, or "" when there is none.
static std::string FindAnyCase(const std::string &dir, const std::string &name)
{
  DIR *d = opendir(dir.empty() ? "." : dir.c_str());
  if (d == NULL)
    return std::string();
  std::string found;
  while (struct dirent *e = readdir(d))
    if (strcasecmp(e->d_name, name.c_str()) == 0) {
      found = e->d_name;
      break;
    }
  closedir(d);
  return found;
}

// The file system's path for a Windows one (see the top of the file).
static std::string OcPath(const char *psz)
{
  std::string s(psz ? psz : "");
  for (char &c : s)
    if (c == '\\')
      c = '/';
  // a drive letter names the root
  if (s.size() >= 2 && isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':')
    s.erase(0, 2);
  struct stat st;
  if (s.empty() || stat(s.c_str(), &st) == 0)
    return s;
  // the longest part that is there as given, then each further name in any case
  std::string out;
  size_t i = 0;
  if (s[0] == '/') {
    out = "/";
    i = 1;
  }
  while (i <= s.size()) {
    size_t j = s.find('/', i);
    if (j == std::string::npos)
      j = s.size();
    std::string name = s.substr(i, j - i);
    if (!name.empty()) {
      std::string candidate = out + name;
      if (name != "." && name != ".." && lstat(candidate.c_str(), &st) != 0) {
        std::string dir = out.empty() ? "." : out.substr(0, out.size() - (out.size() > 1 ? 1 : 0));
        std::string any = FindAnyCase(dir, name);
        if (!any.empty())
          candidate = out + any;
      }
      out = candidate;
      if (j < s.size())
        out += '/';
    } else if (j < s.size() && !out.empty() && out.back() != '/') {
      out += '/';
    }
    i = j + 1;
  }
  return out;
}

// The engine's form of a path of the file system's: an absolute one on drive Z:,
// backslashes for slashes.
static std::string WinPath(const std::string &native)
{
  std::string s = native;
  for (char &c : s)
    if (c == '/')
      c = '\\';
  return !s.empty() && s[0] == '\\' ? "Z:" + s : s;
}

const char *PortNativePath(const char *pszPath)
{
  // a few at once, so two can be in one call
  static thread_local std::string s_aOut[4];
  static thread_local int s_nNext;
  std::string &out = s_aOut[s_nNext++ & 3];
  out = OcPath(pszPath);
  return out.c_str();
}

// The C library's fopen and remove, for the port's own files: on Linux the link sends every call
// to these (-Wl,--wrap, tools/build_engine_linux.py), since <cstdio> takes h/compiler.h's
// fopen macro away again. Apple's linker has no --wrap: no file there calls the two but this
// one, the port's files deleting through DeleteFileA, and the real calls are the C library's.
#undef fopen
#if defined(__APPLE__)
extern "C" FILE *__real_fopen(const char *pszName, const char *pszMode) { return fopen(pszName, pszMode); }
extern "C" int __real_remove(const char *pszName) { return remove(pszName); }
#else
extern "C" FILE *__real_fopen(const char *pszName, const char *pszMode);
extern "C" int __real_remove(const char *pszName);
#endif

extern "C" FILE *__wrap_fopen(const char *pszName, const char *pszMode)
{
  return __real_fopen(OcPath(pszName).c_str(), pszMode);
}

extern "C" int __wrap_remove(const char *pszName) { return __real_remove(OcPath(pszName).c_str()); }

extern "C" FILE *OcFopen(const char *pszName, const char *pszMode) { return __wrap_fopen(pszName, pszMode); }

// ---------------------------------------------------------------------------
// handles
// ---------------------------------------------------------------------------
namespace {

enum Kind { KIND_FILE = 1, KIND_FIND, KIND_EVENT, KIND_THREAD, KIND_PROCESS, KIND_MAPPING, KIND_MODULE, KIND_SHARED };

// Every object alive, so a handle that is not one fails as it does on Windows: the game
// frees what it took for dsound.dll's handle, for one.
std::mutex &LiveLock()
{
  static std::mutex *s_p = new std::mutex;
  return *s_p;
}

std::unordered_set<const void *> &Live()
{
  static std::unordered_set<const void *> *s_p = new std::unordered_set<const void *>;
  return *s_p;
}

struct Object {
  Kind kind;
  explicit Object(Kind k) : kind(k)
  {
    std::lock_guard<std::mutex> lock(LiveLock());
    Live().insert(this);
  }
  virtual ~Object()
  {
    std::lock_guard<std::mutex> lock(LiveLock());
    Live().erase(this);
  }
};

struct FileObject : Object {
  int fd;
  explicit FileObject(int f) : Object(KIND_FILE), fd(f) {}
};

struct FindObject : Object {
  std::string dir;
  std::string pattern;
  std::vector<std::string> names;
  size_t next = 0;
  FindObject() : Object(KIND_FIND) {}
};

// The waitable objects, under one lock. Never destroyed: exit() would destroy the condition
// while a thread still waits on it (DdaThread), and glibc's pthread_cond_destroy then waits
// for that thread for ever, so the game hung on quitting.
std::mutex &g_wait = *new std::mutex;
std::condition_variable &g_waitChanged = *new std::condition_variable;

struct EventObject : Object {
  bool bManual;
  bool bSet;
  EventObject(bool m, bool s) : Object(KIND_EVENT), bManual(m), bSet(s) {}
};

struct ThreadObject : Object {
  pthread_t thread;
  LPTHREAD_START_ROUTINE pfn;
  void *pArg;
  bool bDone = false;
  bool bSuspended = false;
  DWORD dwExit = STILL_ACTIVE;
  DWORD dwId = 0;
  std::atomic<int> nRefs{2};  // the handle and the running thread
  ThreadObject() : Object(KIND_THREAD) {}
};

struct ProcessObject : Object {
  pid_t pid;
  bool bDone = false;
  DWORD dwExit = STILL_ACTIVE;
  explicit ProcessObject(pid_t p) : Object(KIND_PROCESS), pid(p) {}
};

struct MappingObject : Object {
  int fd;
  size_t cb;
  MappingObject(int f, size_t c) : Object(KIND_MAPPING), fd(f), cb(c) {}
};

}  // namespace

static HANDLE const PSEUDO_PROCESS = reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1));
static HANDLE const PSEUDO_THREAD = reinterpret_cast<HANDLE>(static_cast<intptr_t>(-2));

static Object *ObjectOf(const void *h)
{
  if (h == NULL || h == INVALID_HANDLE_VALUE || h == PSEUDO_THREAD)
    return NULL;
  std::lock_guard<std::mutex> lock(LiveLock());
  return Live().count(h) ? static_cast<Object *>(const_cast<void *>(h)) : NULL;
}

static FileObject *FileOf(HANDLE h)
{
  Object *p = ObjectOf(h);
  return p && p->kind == KIND_FILE ? static_cast<FileObject *>(p) : NULL;
}

static void ThreadRelease(ThreadObject *t)
{
  if (--t->nRefs == 0) {
#if !defined(__EMSCRIPTEN__)
    pthread_detach(t->thread);
#endif
    delete t;
  }
}

#if defined(__EMSCRIPTEN__)
// ---------------------------------------------------------------------------
// threads in a browser
// ---------------------------------------------------------------------------
// A page on a LAN's plain http has no threads, as shared memory needs a secure context, so
// Hyperion's threads (1.17's sound loader, the music's streamer, Smacker's timers) are
// Emscripten's fibers on Asyncify, taking turns where one waits, sleeps or meets a critical
// section another holds. When the program's own fiber waits, the others run until they wait,
// then the browser has its turn, to draw and to take the player's input.
static ULONGLONG MonotonicNs(void);

namespace {

struct Fiber {
  emscripten_fiber_t ctx;
  std::vector<char> cstack, astack;
  ThreadObject *t = NULL;
  void (*pfn)(void *) = NULL;  // a thread of the port's own (WebFiberStart), or t's
  void *pArg = NULL;
  DWORD dwId = 1;
  bool bDone = false;
};

const size_t WEB_ASYNC_STACK = 1 << 20;  // Asyncify's record of a fiber's calls, as it is put aside
Fiber g_webMain;
std::vector<Fiber *> g_webFibers;  // the main fiber first
size_t g_webAt;                    // the one running
DWORD g_webNextId = 2;
ULONGLONG g_webLastTurn;

void WebInit(void)
{
  if (!g_webFibers.empty())
    return;
  g_webMain.astack.resize(WEB_ASYNC_STACK);
  emscripten_fiber_init_from_current_context(&g_webMain.ctx, g_webMain.astack.data(), g_webMain.astack.size());
  g_webFibers.push_back(&g_webMain);
}

// To the next fiber that has not ended; around the ring and back to the caller's.
void WebYield(void)
{
  WebInit();
  const size_t n = g_webFibers.size();
  if (n < 2)
    return;
  Fiber *pFrom = g_webFibers[g_webAt];
  size_t k = g_webAt;
  do
    k = (k + 1) % n;
  while (g_webFibers[k]->bDone && k != g_webAt);
  if (k == g_webAt)
    return;
  g_webAt = k;
  emscripten_fiber_swap(&pFrom->ctx, &g_webFibers[k]->ctx);
  // back here: the fibers that ended, their stacks let go, from the main fiber's
  if (g_webAt == 0)
    for (size_t i = 1; i < g_webFibers.size();)
      if (g_webFibers[i]->bDone) {
        delete g_webFibers[i];
        g_webFibers.erase(g_webFibers.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        ++i;
      }
}

// A wait's turn: the other fibers run; on the main fiber the browser has its turn too, for
// up to nMs (0: only the turn).
void WebWait(DWORD nMs)
{
  WebYield();
  if (g_webAt == 0) {
    g_webLastTurn = MonotonicNs();
    emscripten_sleep(nMs);
  }
}

void WebFiberEntry(void *pv)
{
  Fiber *f = static_cast<Fiber *>(pv);
  f->pfn(f->pArg);
  f->bDone = true;
  for (;;)
    WebYield();
}

// A fiber of nStack bytes of stack running pfn(pArg), its turn the next time one waits.
Fiber *WebFiberStart(void (*pfn)(void *), void *pArg, size_t nStack)
{
  WebInit();
  Fiber *f = new Fiber;
  f->pfn = pfn;
  f->pArg = pArg;
  f->dwId = g_webNextId++;
  f->cstack.resize(nStack);
  f->astack.resize(WEB_ASYNC_STACK);
  emscripten_fiber_init(&f->ctx, WebFiberEntry, f, f->cstack.data(), f->cstack.size(), f->astack.data(),
                        f->astack.size());
  g_webFibers.push_back(f);
  return f;
}

}  // namespace

// The browser's turn from the game's own frames (port_present.cpp), when no wait has given
// it one for a frame's time: a game that never sleeps would hold the page still.
void PortWebTurn(void)
{
  if (MonotonicNs() - g_webLastTurn < 16000000ULL)
    return;
  WebWait(0);
}
#endif

BOOL __stdcall CloseHandle(HANDLE hObject)
{
  Object *p = ObjectOf(hObject);
  if (p == NULL) {
    t_dwLastError = ERROR_INVALID_HANDLE;
    return FALSE;
  }
  switch (p->kind) {
  case KIND_FILE:
    close(static_cast<FileObject *>(p)->fd);
    delete p;
    break;
  case KIND_THREAD:
    ThreadRelease(static_cast<ThreadObject *>(p));
    break;
  case KIND_MAPPING:
    close(static_cast<MappingObject *>(p)->fd);
    delete p;
    break;
  default:
    delete p;
    break;
  }
  return TRUE;
}

BOOL __stdcall DuplicateHandle(HANDLE, HANDLE, HANDLE, LPHANDLE lpTargetHandle, DWORD, BOOL, DWORD)
{
  if (lpTargetHandle)
    *lpTargetHandle = NULL;
  t_dwLastError = ERROR_NOT_SUPPORTED;
  return FALSE;
}

// ---------------------------------------------------------------------------
// files
// ---------------------------------------------------------------------------
HANDLE __stdcall CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD, LPSECURITY_ATTRIBUTES,
                             DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE)
{
  const std::string path = OcPath(lpFileName);
  int flags = 0;
  const bool bRead = (dwDesiredAccess & (GENERIC_READ | GENERIC_ALL)) != 0;
  const bool bWrite = (dwDesiredAccess & (GENERIC_WRITE | GENERIC_ALL)) != 0;
  flags = bRead && bWrite ? O_RDWR : bWrite ? O_WRONLY : O_RDONLY;
  switch (dwCreationDisposition) {
  case CREATE_NEW: flags |= O_CREAT | O_EXCL; break;
  case CREATE_ALWAYS: flags |= O_CREAT | O_TRUNC; break;
  case OPEN_ALWAYS: flags |= O_CREAT; break;
  case TRUNCATE_EXISTING: flags |= O_TRUNC; break;
  default: break;
  }
  struct stat st;
  const bool bExisted = stat(path.c_str(), &st) == 0;
  if (bExisted && S_ISDIR(st.st_mode)) {
    t_dwLastError = ERROR_ACCESS_DENIED;
    return INVALID_HANDLE_VALUE;
  }
  const mode_t mode = (dwFlagsAndAttributes & FILE_ATTRIBUTE_READONLY) ? 0444 : 0644;
  const int fd = open(path.c_str(), flags | O_CLOEXEC, mode);
  if (fd < 0) {
    FailErrno();
    return INVALID_HANDLE_VALUE;
  }
  // as Windows reports it: a file that was there for CREATE_ALWAYS and OPEN_ALWAYS
  t_dwLastError = bExisted && (dwCreationDisposition == CREATE_ALWAYS || dwCreationDisposition == OPEN_ALWAYS)
                      ? ERROR_ALREADY_EXISTS
                      : ERROR_SUCCESS;
  return new FileObject(fd);
}

BOOL __stdcall ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead,
                        LPOVERLAPPED lpOverlapped)
{
  FileObject *f = FileOf(hFile);
  if (lpNumberOfBytesRead)
    *lpNumberOfBytesRead = 0;
  if (f == NULL) {
    t_dwLastError = ERROR_INVALID_HANDLE;
    return FALSE;
  }
  if (lpOverlapped)
    lseek(f->fd, static_cast<off_t>(lpOverlapped->Offset) | (static_cast<off_t>(lpOverlapped->OffsetHigh) << 32),
          SEEK_SET);
  size_t done = 0;
  while (done < nNumberOfBytesToRead) {
    const ssize_t n = read(f->fd, static_cast<char *>(lpBuffer) + done, nNumberOfBytesToRead - done);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      FailErrno();
      if (lpNumberOfBytesRead)
        *lpNumberOfBytesRead = static_cast<DWORD>(done);
      return FALSE;
    }
    if (n == 0)
      break;
    done += static_cast<size_t>(n);
  }
  if (lpNumberOfBytesRead)
    *lpNumberOfBytesRead = static_cast<DWORD>(done);
  return TRUE;
}

BOOL __stdcall WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten,
                         LPOVERLAPPED lpOverlapped)
{
  FileObject *f = FileOf(hFile);
  if (lpNumberOfBytesWritten)
    *lpNumberOfBytesWritten = 0;
  if (f == NULL) {
    t_dwLastError = ERROR_INVALID_HANDLE;
    return FALSE;
  }
  if (lpOverlapped)
    lseek(f->fd, static_cast<off_t>(lpOverlapped->Offset) | (static_cast<off_t>(lpOverlapped->OffsetHigh) << 32),
          SEEK_SET);
  size_t done = 0;
  while (done < nNumberOfBytesToWrite) {
    const ssize_t n = write(f->fd, static_cast<const char *>(lpBuffer) + done, nNumberOfBytesToWrite - done);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      FailErrno();
      if (lpNumberOfBytesWritten)
        *lpNumberOfBytesWritten = static_cast<DWORD>(done);
      return FALSE;
    }
    done += static_cast<size_t>(n);
  }
  if (lpNumberOfBytesWritten)
    *lpNumberOfBytesWritten = static_cast<DWORD>(done);
  return TRUE;
}

BOOL __stdcall FlushFileBuffers(HANDLE hFile)
{
  FileObject *f = FileOf(hFile);
  return f && fsync(f->fd) == 0;
}

BOOL __stdcall GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize)
{
  FileObject *f = FileOf(hFile);
  struct stat st;
  if (f == NULL || fstat(f->fd, &st) != 0) {
    t_dwLastError = ERROR_INVALID_HANDLE;
    return FALSE;
  }
  lpFileSize->QuadPart = st.st_size;
  return TRUE;
}

DWORD __stdcall GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)
{
  LARGE_INTEGER li;
  if (!GetFileSizeEx(hFile, &li))
    return INVALID_FILE_SIZE;
  if (lpFileSizeHigh)
    *lpFileSizeHigh = static_cast<DWORD>(li.QuadPart >> 32);
  t_dwLastError = ERROR_SUCCESS;
  return static_cast<DWORD>(li.QuadPart);
}

BOOL __stdcall SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer,
                                DWORD dwMoveMethod)
{
  FileObject *f = FileOf(hFile);
  if (f == NULL) {
    t_dwLastError = ERROR_INVALID_HANDLE;
    return FALSE;
  }
  const int whence = dwMoveMethod == FILE_CURRENT ? SEEK_CUR : dwMoveMethod == FILE_END ? SEEK_END : SEEK_SET;
  const off_t pos = lseek(f->fd, static_cast<off_t>(liDistanceToMove.QuadPart), whence);
  if (pos < 0) {
    t_dwLastError = ERROR_INVALID_PARAMETER;  // NEGATIVE_SEEK, as Windows reports a move before the start
    return FALSE;
  }
  if (lpNewFilePointer)
    lpNewFilePointer->QuadPart = pos;
  return TRUE;
}

DWORD __stdcall SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod)
{
  LARGE_INTEGER li, out;
  li.QuadPart = lpDistanceToMoveHigh
                    ? static_cast<LONGLONG>((static_cast<ULONGLONG>(static_cast<DWORD>(*lpDistanceToMoveHigh)) << 32) |
                                            static_cast<DWORD>(lDistanceToMove))
                    : static_cast<LONGLONG>(lDistanceToMove);
  if (!SetFilePointerEx(hFile, li, &out, dwMoveMethod))
    return INVALID_SET_FILE_POINTER;
  if (lpDistanceToMoveHigh)
    *lpDistanceToMoveHigh = static_cast<LONG>(out.QuadPart >> 32);
  t_dwLastError = ERROR_SUCCESS;
  return static_cast<DWORD>(out.QuadPart);
}

BOOL __stdcall SetEndOfFile(HANDLE hFile)
{
  FileObject *f = FileOf(hFile);
  if (f == NULL)
    return FALSE;
  const off_t pos = lseek(f->fd, 0, SEEK_CUR);
  if (pos < 0 || ftruncate(f->fd, pos) != 0) {
    FailErrno();
    return FALSE;
  }
  return TRUE;
}

// FILETIME is 100-nanosecond ticks from 1601; Unix time is seconds from 1970.
static const ULONGLONG FILETIME_UNIX_EPOCH = 116444736000000000ULL;

static FILETIME FileTimeFrom(const struct timespec &ts)
{
  const ULONGLONG q = FILETIME_UNIX_EPOCH + static_cast<ULONGLONG>(ts.tv_sec) * 10000000ULL +
                      static_cast<ULONGLONG>(ts.tv_nsec) / 100;
  FILETIME ft;
  ft.dwLowDateTime = static_cast<DWORD>(q);
  ft.dwHighDateTime = static_cast<DWORD>(q >> 32);
  return ft;
}

static ULONGLONG Ticks(const FILETIME *p)
{
  return (static_cast<ULONGLONG>(p->dwHighDateTime) << 32) | p->dwLowDateTime;
}

// stat's times, which macOS names st_mtimespec and st_atimespec
#if defined(__APPLE__)
#define st_mtim st_mtimespec
#define st_atim st_atimespec
#endif

BOOL __stdcall GetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime,
                           LPFILETIME lpLastWriteTime)
{
  FileObject *f = FileOf(hFile);
  struct stat st;
  if (f == NULL || fstat(f->fd, &st) != 0)
    return FALSE;
  if (lpCreationTime)
    *lpCreationTime = FileTimeFrom(st.st_mtim);
  if (lpLastAccessTime)
    *lpLastAccessTime = FileTimeFrom(st.st_atim);
  if (lpLastWriteTime)
    *lpLastWriteTime = FileTimeFrom(st.st_mtim);
  return TRUE;
}

BOOL __stdcall SetFileTime(HANDLE hFile, const FILETIME *, const FILETIME *lpLastAccessTime,
                           const FILETIME *lpLastWriteTime)
{
  FileObject *f = FileOf(hFile);
  if (f == NULL)
    return FALSE;
  struct timespec ts[2];
  for (int i = 0; i < 2; i++) {
    const FILETIME *p = i == 0 ? lpLastAccessTime : lpLastWriteTime;
    if (p == NULL) {
      ts[i].tv_sec = 0;
      ts[i].tv_nsec = UTIME_OMIT;
    } else {
      const ULONGLONG q = Ticks(p) - FILETIME_UNIX_EPOCH;
      ts[i].tv_sec = static_cast<time_t>(q / 10000000ULL);
      ts[i].tv_nsec = static_cast<long>((q % 10000000ULL) * 100);
    }
  }
  return futimens(f->fd, ts) == 0;
}

static DWORD AttributesOf(const std::string &path, const struct stat &st)
{
  DWORD a = 0;
  if (S_ISDIR(st.st_mode))
    a |= FILE_ATTRIBUTE_DIRECTORY;
  if ((st.st_mode & S_IWUSR) == 0)
    a |= FILE_ATTRIBUTE_READONLY;
  const size_t slash = path.find_last_of('/');
  const char *name = path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
  if (name[0] == '.' && strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
    a |= FILE_ATTRIBUTE_HIDDEN;
  return a ? a : FILE_ATTRIBUTE_NORMAL;
}

DWORD __stdcall GetFileAttributesA(LPCSTR lpFileName)
{
  const std::string path = OcPath(lpFileName);
  struct stat st;
  if (path.empty() || stat(path.c_str(), &st) != 0) {
    t_dwLastError = path.empty() ? ERROR_PATH_NOT_FOUND : ErrorFromErrno(errno);
    return INVALID_FILE_ATTRIBUTES;
  }
  return AttributesOf(path, st);
}

BOOL __stdcall GetFileAttributesExA(LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS, LPVOID lpFileInformation)
{
  const std::string path = OcPath(lpFileName);
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    FailErrno();
    return FALSE;
  }
  WIN32_FILE_ATTRIBUTE_DATA *p = static_cast<WIN32_FILE_ATTRIBUTE_DATA *>(lpFileInformation);
  p->dwFileAttributes = AttributesOf(path, st);
  p->ftCreationTime = FileTimeFrom(st.st_mtim);
  p->ftLastAccessTime = FileTimeFrom(st.st_atim);
  p->ftLastWriteTime = FileTimeFrom(st.st_mtim);
  p->nFileSizeHigh = static_cast<DWORD>(static_cast<ULONGLONG>(st.st_size) >> 32);
  p->nFileSizeLow = static_cast<DWORD>(st.st_size);
  return TRUE;
}

BOOL __stdcall SetFileAttributesA(LPCSTR lpFileName, DWORD dwFileAttributes)
{
  const std::string path = OcPath(lpFileName);
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    FailErrno();
    return FALSE;
  }
  mode_t mode = st.st_mode & 07777;
  if (dwFileAttributes & FILE_ATTRIBUTE_READONLY)
    mode &= ~static_cast<mode_t>(0222);
  else
    mode |= S_IWUSR;
  return chmod(path.c_str(), mode) == 0;
}

BOOL __stdcall DeleteFileA(LPCSTR lpFileName)
{
  const std::string path = OcPath(lpFileName);
  if (unlink(path.c_str()) != 0) {
    FailErrno();
    return FALSE;
  }
  return TRUE;
}

BOOL __stdcall CopyFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, BOOL bFailIfExists)
{
  const std::string from = OcPath(lpExistingFileName), to = OcPath(lpNewFileName);
  const int in = open(from.c_str(), O_RDONLY | O_CLOEXEC);
  if (in < 0) {
    FailErrno();
    return FALSE;
  }
  struct stat st;
  fstat(in, &st);
  const int out = open(to.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC | (bFailIfExists ? O_EXCL : O_TRUNC), st.st_mode & 0777);
  if (out < 0) {
    FailErrno();
    close(in);
    return FALSE;
  }
  char buf[65536];
  BOOL ok = TRUE;
  for (;;) {
    const ssize_t n = read(in, buf, sizeof buf);
    if (n == 0)
      break;
    if (n < 0 || write(out, buf, static_cast<size_t>(n)) != n) {
      FailErrno();
      ok = FALSE;
      break;
    }
  }
  close(in);
  close(out);
  return ok;
}

BOOL __stdcall MoveFileExA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, DWORD dwFlags)
{
  const std::string from = OcPath(lpExistingFileName), to = OcPath(lpNewFileName);
  struct stat st;
  if ((dwFlags & MOVEFILE_REPLACE_EXISTING) == 0 && stat(to.c_str(), &st) == 0) {
    t_dwLastError = ERROR_ALREADY_EXISTS;
    return FALSE;
  }
  if (rename(from.c_str(), to.c_str()) != 0) {
    if (errno == EXDEV && (dwFlags & MOVEFILE_COPY_ALLOWED) != 0 && CopyFileA(lpExistingFileName, lpNewFileName, FALSE))
      return DeleteFileA(lpExistingFileName);
    FailErrno();
    return FALSE;
  }
  return TRUE;
}

BOOL __stdcall MoveFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName)
{
  return MoveFileExA(lpExistingFileName, lpNewFileName, MOVEFILE_COPY_ALLOWED);
}

BOOL __stdcall CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES)
{
  std::string path = OcPath(lpPathName);
  while (path.size() > 1 && path.back() == '/')
    path.pop_back();
  if (mkdir(path.c_str(), 0755) != 0) {
    FailErrno();
    return FALSE;
  }
  return TRUE;
}

BOOL __stdcall RemoveDirectoryA(LPCSTR lpPathName)
{
  const std::string path = OcPath(lpPathName);
  if (rmdir(path.c_str()) != 0) {
    FailErrno();
    return FALSE;
  }
  return TRUE;
}

// Windows' wildcard match: * and ?, in any case. "*.*" matches every name, with or
// without a dot, as on Windows.
static bool WildMatch(const char *pat, const char *name)
{
  if (strcmp(pat, "*.*") == 0 || strcmp(pat, "*") == 0)
    return true;
  return fnmatch(pat, name, FNM_CASEFOLD | FNM_NOESCAPE) == 0;
}

static bool FindFill(FindObject *f, LPWIN32_FIND_DATAA p)
{
  while (f->next < f->names.size()) {
    const std::string &name = f->names[f->next++];
    if (!WildMatch(f->pattern.c_str(), name.c_str()))
      continue;
    const std::string full = (f->dir.empty() ? std::string(".") : f->dir) + "/" + name;
    struct stat st;
    if (stat(full.c_str(), &st) != 0)
      continue;
    memset(p, 0, sizeof *p);
    p->dwFileAttributes = AttributesOf(name, st);
    p->ftCreationTime = FileTimeFrom(st.st_mtim);
    p->ftLastAccessTime = FileTimeFrom(st.st_atim);
    p->ftLastWriteTime = FileTimeFrom(st.st_mtim);
    p->nFileSizeHigh = static_cast<DWORD>(static_cast<ULONGLONG>(st.st_size) >> 32);
    p->nFileSizeLow = static_cast<DWORD>(st.st_size);
    snprintf(p->cFileName, sizeof p->cFileName, "%s", name.c_str());
    return true;
  }
  return false;
}

HANDLE __stdcall FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData)
{
  std::string spec(lpFileName ? lpFileName : "");
  for (char &c : spec)
    if (c == '\\')
      c = '/';
  const size_t slash = spec.find_last_of('/');
  FindObject *f = new FindObject;
  f->pattern = slash == std::string::npos ? spec : spec.substr(slash + 1);
  f->dir = slash == std::string::npos ? std::string() : OcPath(spec.substr(0, slash + 1).c_str());
  while (f->dir.size() > 1 && f->dir.back() == '/')
    f->dir.pop_back();
  if (f->pattern.empty())
    f->pattern = "*";
  DIR *d = opendir(f->dir.empty() ? "." : f->dir.c_str());
  if (d == NULL) {
    t_dwLastError = ERROR_PATH_NOT_FOUND;
    delete f;
    return INVALID_HANDLE_VALUE;
  }
  while (struct dirent *e = readdir(d))
    f->names.push_back(e->d_name);
  closedir(d);
  if (!FindFill(f, lpFindFileData)) {
    t_dwLastError = ERROR_FILE_NOT_FOUND;
    delete f;
    return INVALID_HANDLE_VALUE;
  }
  return f;
}

BOOL __stdcall FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData)
{
  Object *p = ObjectOf(hFindFile);
  if (p == NULL || p->kind != KIND_FIND) {
    t_dwLastError = ERROR_INVALID_HANDLE;
    return FALSE;
  }
  if (!FindFill(static_cast<FindObject *>(p), lpFindFileData)) {
    t_dwLastError = ERROR_NO_MORE_FILES;
    return FALSE;
  }
  return TRUE;
}

BOOL __stdcall FindClose(HANDLE hFindFile)
{
  Object *p = ObjectOf(hFindFile);
  if (p == NULL || p->kind != KIND_FIND)
    return FALSE;
  delete p;
  return TRUE;
}

DWORD __stdcall GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR *lpFilePart)
{
  std::string path = OcPath(lpFileName);
  if (path.empty() || path[0] != '/') {
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd) == NULL)
      return 0;
    path = std::string(cwd) + "/" + path;
  }
  // "." and ".." taken out, as Windows does
  std::vector<std::string> parts;
  size_t i = 1;
  while (i <= path.size()) {
    size_t j = path.find('/', i);
    if (j == std::string::npos)
      j = path.size();
    const std::string name = path.substr(i, j - i);
    if (name == "..") {
      if (!parts.empty())
        parts.pop_back();
    } else if (!name.empty() && name != ".") {
      parts.push_back(name);
    }
    i = j + 1;
  }
  std::string out;
  for (const std::string &s : parts)
    out += "/" + s;
  if (out.empty() || (!path.empty() && path.back() == '/'))
    out += "/";
  out = WinPath(out);
  if (out.size() + 1 > nBufferLength)
    return static_cast<DWORD>(out.size() + 1);
  memcpy(lpBuffer, out.c_str(), out.size() + 1);
  if (lpFilePart) {
    char *p = strrchr(lpBuffer, '\\');
    *lpFilePart = p && p[1] ? p + 1 : NULL;
  }
  return static_cast<DWORD>(out.size());
}

DWORD __stdcall GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer)
{
  char cwd[4096];
  if (getcwd(cwd, sizeof cwd) == NULL)
    return 0;
  const std::string s = WinPath(cwd);
  const DWORD n = static_cast<DWORD>(s.size());
  if (n + 1 > nBufferLength)
    return n + 1;
  memcpy(lpBuffer, s.c_str(), n + 1);
  return n;
}

BOOL __stdcall SetCurrentDirectoryA(LPCSTR lpPathName)
{
  const std::string path = OcPath(lpPathName);
  if (chdir(path.c_str()) != 0) {
    FailErrno();
    return FALSE;
  }
  return TRUE;
}

static DWORD CopyOut(const std::string &s, LPSTR lpBuffer, DWORD nBufferLength)
{
  if (s.size() + 1 > nBufferLength)
    return static_cast<DWORD>(s.size() + 1);
  memcpy(lpBuffer, s.c_str(), s.size() + 1);
  return static_cast<DWORD>(s.size());
}

DWORD __stdcall GetTempPathA(DWORD nBufferLength, LPSTR lpBuffer)
{
  const char *psz = getenv("TMPDIR");
  std::string s = psz && *psz ? psz : "/tmp";
  if (s.back() != '/')
    s += '/';
  return CopyOut(WinPath(s), lpBuffer, nBufferLength);
}

// No Windows folder off Windows: the callers look for fonts there, and do without.
UINT __stdcall GetWindowsDirectoryA(LPSTR lpBuffer, UINT uSize)
{
  if (lpBuffer && uSize)
    lpBuffer[0] = 0;
  return 0;
}

UINT __stdcall GetDriveTypeA(LPCSTR) { return DRIVE_FIXED; }

// No drive letters, and so no CD drives to look on.
DWORD __stdcall GetLogicalDriveStringsA(DWORD nBufferLength, LPSTR lpBuffer)
{
  if (lpBuffer && nBufferLength >= 2)
    lpBuffer[0] = lpBuffer[1] = 0;
  return 0;
}

BOOL __stdcall GetDiskFreeSpaceA(LPCSTR lpRootPathName, LPDWORD lpSectorsPerCluster, LPDWORD lpBytesPerSector,
                                 LPDWORD lpNumberOfFreeClusters, LPDWORD lpTotalNumberOfClusters)
{
  const std::string path = OcPath(lpRootPathName && *lpRootPathName ? lpRootPathName : ".");
  struct statvfs sv;
  if (statvfs(path.empty() ? "." : path.c_str(), &sv) != 0) {
    FailErrno();
    return FALSE;
  }
  // in clusters of 64 KiB, so a large disk still fits a DWORD's count
  const ULONGLONG cbCluster = 65536;
  const ULONGLONG qFree = static_cast<ULONGLONG>(sv.f_bavail) * sv.f_frsize / cbCluster;
  const ULONGLONG qTotal = static_cast<ULONGLONG>(sv.f_blocks) * sv.f_frsize / cbCluster;
  if (lpSectorsPerCluster)
    *lpSectorsPerCluster = 128;
  if (lpBytesPerSector)
    *lpBytesPerSector = 512;
  if (lpNumberOfFreeClusters)
    *lpNumberOfFreeClusters = qFree > 0xFFFFFFFFULL ? 0xFFFFFFFFu : static_cast<DWORD>(qFree);
  if (lpTotalNumberOfClusters)
    *lpTotalNumberOfClusters = qTotal > 0xFFFFFFFFULL ? 0xFFFFFFFFu : static_cast<DWORD>(qTotal);
  return TRUE;
}

BOOL __stdcall GetVolumeInformationA(LPCSTR, LPSTR lpVolumeNameBuffer, DWORD nVolumeNameSize,
                                     LPDWORD lpVolumeSerialNumber, LPDWORD lpMaximumComponentLength,
                                     LPDWORD lpFileSystemFlags, LPSTR lpFileSystemNameBuffer,
                                     DWORD nFileSystemNameSize)
{
  if (lpVolumeNameBuffer && nVolumeNameSize)
    lpVolumeNameBuffer[0] = 0;
  if (lpVolumeSerialNumber)
    *lpVolumeSerialNumber = 0;
  if (lpMaximumComponentLength)
    *lpMaximumComponentLength = 255;
  if (lpFileSystemFlags)
    *lpFileSystemFlags = 0;
  if (lpFileSystemNameBuffer && nFileSystemNameSize)
    snprintf(lpFileSystemNameBuffer, nFileSystemNameSize, "%s", "posix");
  return TRUE;
}

// The shell's folders, by the XDG base directories.
static std::string Home(void)
{
  const char *psz = getenv("HOME");
  if (psz && *psz)
    return psz;
  struct passwd *pw = getpwuid(getuid());
  return pw && pw->pw_dir ? pw->pw_dir : "/tmp";
}

static int MakeDirs(const std::string &path)
{
  std::string s;
  size_t i = 0;
  while (i <= path.size()) {
    size_t j = path.find('/', i);
    if (j == std::string::npos)
      j = path.size();
    s = path.substr(0, j);
    if (!s.empty())
      mkdir(s.c_str(), 0755);
    i = j + 1;
  }
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode) ? 0 : -1;
}

HRESULT __stdcall SHGetFolderPathA(HWND, int csidl, HANDLE, DWORD, LPSTR pszPath)
{
  std::string s;
  const char *pszXdg;
  switch (csidl & 0xFF) {
  case CSIDL_PERSONAL:
    s = Home();
    break;
  case CSIDL_APPDATA:
    pszXdg = getenv("XDG_CONFIG_HOME");
    s = pszXdg && *pszXdg ? pszXdg : Home() + "/.config";
    break;
  case CSIDL_LOCAL_APPDATA:
  case CSIDL_COMMON_APPDATA:
    pszXdg = getenv("XDG_DATA_HOME");
    s = pszXdg && *pszXdg ? pszXdg : Home() + "/.local/share";
    break;
  default:
    pszPath[0] = 0;
    return E_FAIL;
  }
  if (csidl & CSIDL_FLAG_CREATE)
    MakeDirs(s);
  snprintf(pszPath, MAX_PATH, "%s", WinPath(s).c_str());
  return S_OK;
}

int __stdcall SHCreateDirectoryExA(HWND, LPCSTR pszPath, const SECURITY_ATTRIBUTES *)
{
  const std::string path = OcPath(pszPath);
  struct stat st;
  if (stat(path.c_str(), &st) == 0)
    return ERROR_ALREADY_EXISTS;
  return MakeDirs(path) == 0 ? ERROR_SUCCESS : ERROR_PATH_NOT_FOUND;
}

// ShellExecute's "open": the desktop's opener, xdg-open (open on a Mac).
HINSTANCE __stdcall ShellExecuteA(HWND, LPCSTR, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR, INT)
{
  if (lpFile == NULL)
    return reinterpret_cast<HINSTANCE>(static_cast<intptr_t>(2));
  std::string target = lpFile;
#if defined(__EMSCRIPTEN__)
  // in a browser: a link opens in a new tab; a file has nothing to open it
  if (target.find("://") == std::string::npos)
    return reinterpret_cast<HINSTANCE>(static_cast<intptr_t>(2));
  std::string js = "window.open('";
  for (char c : target)
    if (c == '\\' || c == '\'')
      js += std::string("\\") + c;
    else if (static_cast<unsigned char>(c) >= 0x20)
      js += c;
  js += "', '_blank')";
  emscripten_run_script(js.c_str());
  return reinterpret_cast<HINSTANCE>(static_cast<intptr_t>(33));
#else
  if (target.find("://") == std::string::npos)
    target = OcPath(lpFile);
#if defined(__APPLE__)
  const char *pszOpener = "open";
#else
  const char *pszOpener = "xdg-open";
#endif
  std::vector<std::string> args = {pszOpener, target};
  if (lpParameters && *lpParameters)
    args.push_back(lpParameters);
  std::vector<char *> argv;
  for (std::string &a : args)
    argv.push_back(&a[0]);
  argv.push_back(NULL);
  pid_t pid;
  if (posix_spawnp(&pid, pszOpener, NULL, NULL, argv.data(), environ) != 0)
    return reinterpret_cast<HINSTANCE>(static_cast<intptr_t>(2));
  return reinterpret_cast<HINSTANCE>(static_cast<intptr_t>(33));
#endif
}

// ---------------------------------------------------------------------------
// the process and its environment
// ---------------------------------------------------------------------------
static std::string g_commandLine;

// The command line, as Windows gives it (hyperion/hyperion.cpp's main): the program,
// then each argument, quoted where it has a space.
void PortPosixSetCommandLine(int argc, char **argv)
{
  g_commandLine.clear();
  for (int i = 0; i < argc; i++) {
    if (i)
      g_commandLine += ' ';
    const bool bQuote = strchr(argv[i], ' ') != NULL || argv[i][0] == 0;
    if (bQuote)
      g_commandLine += '"';
    g_commandLine += argv[i];
    if (bQuote)
      g_commandLine += '"';
  }
}

LPSTR __stdcall GetCommandLineA(void) { return &g_commandLine[0]; }

void __stdcall GetStartupInfoA(LPSTARTUPINFOA lpStartupInfo)
{
  memset(lpStartupInfo, 0, sizeof *lpStartupInfo);
  lpStartupInfo->cb = sizeof *lpStartupInfo;
}

DWORD __stdcall GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize)
{
  const char *psz = getenv(lpName);
  if (psz == NULL) {
    t_dwLastError = 203;  // ERROR_ENVVAR_NOT_FOUND
    return 0;
  }
  return CopyOut(psz, lpBuffer, nSize);
}

BOOL __stdcall SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue)
{
  return (lpValue ? setenv(lpName, lpValue, 1) : unsetenv(lpName)) == 0;
}

UINT __stdcall SetErrorMode(UINT uMode)
{
  static UINT s_uMode;
  const UINT uOld = s_uMode;
  s_uMode = uMode;
  return uOld;
}

void __stdcall ExitProcess(UINT uExitCode)
{
#if defined(__EMSCRIPTEN__)
  PortWebExit(static_cast<int>(uExitCode));
#endif
  exit(static_cast<int>(uExitCode));
}

#if defined(__EMSCRIPTEN__)
void PortWebExit(int nCode)
{
  EM_ASM({ if (Module['hyExit']) Module['hyExit']($0); }, nCode);
}
#endif

HANDLE __stdcall GetCurrentProcess(void) { return PSEUDO_PROCESS; }
DWORD __stdcall GetCurrentProcessId(void) { return static_cast<DWORD>(getpid()); }
HANDLE __stdcall GetCurrentThread(void) { return PSEUDO_THREAD; }

DWORD __stdcall GetCurrentThreadId(void)
{
#if defined(__linux__)
  return static_cast<DWORD>(syscall(SYS_gettid));
#elif defined(__EMSCRIPTEN__)
  WebInit();
  return g_webFibers[g_webAt]->dwId;
#else
  uint64_t id = 0;
  pthread_threadid_np(NULL, &id);
  return static_cast<DWORD>(id);
#endif
}

BOOL __stdcall TerminateProcess(HANDLE hProcess, UINT uExitCode)
{
  if (hProcess == PSEUDO_PROCESS)
    _exit(static_cast<int>(uExitCode));
  Object *p = ObjectOf(hProcess);
  if (p == NULL || p->kind != KIND_PROCESS)
    return FALSE;
  return kill(static_cast<ProcessObject *>(p)->pid, SIGTERM) == 0;
}

// The command line split as Windows' C runtime splits it: spaces, and quotes around a
// part that has them.
static std::vector<std::string> SplitCommandLine(const char *psz)
{
  std::vector<std::string> out;
  std::string cur;
  bool bIn = false, bAny = false;
  for (; psz && *psz; psz++) {
    if (*psz == '"') {
      bIn = !bIn;
      bAny = true;
    } else if ((*psz == ' ' || *psz == '\t') && !bIn) {
      if (bAny || !cur.empty())
        out.push_back(cur);
      cur.clear();
      bAny = false;
    } else {
      cur += *psz;
    }
  }
  if (bAny || !cur.empty())
    out.push_back(cur);
  return out;
}

// glibc 2.29 and later can give a spawned child a folder of its own; where the C library
// cannot, the child keeps this process's folder, since changing that over a spawn would
// reach every thread's relative file I/O. The one caller that asks for a folder is
// gluMain.cpp's StarEdit button.
#if defined(__GLIBC__) && defined(_GNU_SOURCE)
#if __GLIBC_PREREQ(2, 29)
#define OC_SPAWN_CHDIR 1
#endif
#endif

BOOL __stdcall CreateProcessA(LPCSTR lpApplicationName, LPSTR lpCommandLine, LPSECURITY_ATTRIBUTES,
                              LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCSTR lpCurrentDirectory, LPSTARTUPINFOA,
                              LPPROCESS_INFORMATION lpProcessInformation)
{
  std::vector<std::string> args = SplitCommandLine(lpCommandLine);
  std::string program = lpApplicationName ? OcPath(lpApplicationName) : args.empty() ? std::string() : OcPath(args[0].c_str());
  if (program.empty()) {
    t_dwLastError = ERROR_FILE_NOT_FOUND;
    return FALSE;
  }
  if (args.empty())
    args.push_back(program);
  std::vector<char *> argv;
  for (std::string &a : args)
    argv.push_back(&a[0]);
  argv.push_back(NULL);
  pid_t pid;
  posix_spawn_file_actions_t actions;
  bool bActions = false;
#if defined(OC_SPAWN_CHDIR)
  if (lpCurrentDirectory != NULL && posix_spawn_file_actions_init(&actions) == 0) {
    bActions = posix_spawn_file_actions_addchdir_np(&actions, OcPath(lpCurrentDirectory).c_str()) == 0;
    if (!bActions)
      posix_spawn_file_actions_destroy(&actions);
  }
#endif
  const int r = posix_spawn(&pid, program.c_str(), bActions ? &actions : NULL, NULL, argv.data(), environ);
  if (bActions)
    posix_spawn_file_actions_destroy(&actions);
  if (r != 0) {
    t_dwLastError = ErrorFromErrno(r);
    return FALSE;
  }
  if (lpProcessInformation) {
    lpProcessInformation->hProcess = new ProcessObject(pid);
    lpProcessInformation->hThread = NULL;
    lpProcessInformation->dwProcessId = static_cast<DWORD>(pid);
    lpProcessInformation->dwThreadId = 0;
  }
  return TRUE;
}

HANDLE __stdcall OpenProcess(DWORD, BOOL, DWORD dwProcessId)
{
  if (kill(static_cast<pid_t>(dwProcessId), 0) != 0) {
    t_dwLastError = ERROR_INVALID_PARAMETER;
    return NULL;
  }
  return new ProcessObject(static_cast<pid_t>(dwProcessId));
}

static void ProcessPoll(ProcessObject *p)
{
  if (p->bDone)
    return;
  int status = 0;
  const pid_t r = waitpid(p->pid, &status, WNOHANG);
  if (r == p->pid) {
    p->bDone = true;
    p->dwExit = WIFEXITED(status) ? static_cast<DWORD>(WEXITSTATUS(status)) : 1;
  } else if (r < 0 && errno == ECHILD) {
    // not this process's child: it has ended when it is gone
    if (kill(p->pid, 0) != 0) {
      p->bDone = true;
      p->dwExit = 0;
    }
  }
}

BOOL __stdcall GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode)
{
  Object *p = ObjectOf(hProcess);
  if (p == NULL || p->kind != KIND_PROCESS)
    return FALSE;
  ProcessPoll(static_cast<ProcessObject *>(p));
  *lpExitCode = static_cast<ProcessObject *>(p)->dwExit;
  return TRUE;
}

DWORD __stdcall WaitForInputIdle(HANDLE, DWORD) { return 0; }

BOOL __stdcall GetProcessTimes(HANDLE, LPFILETIME lpCreationTime, LPFILETIME lpExitTime, LPFILETIME lpKernelTime,
                               LPFILETIME lpUserTime)
{
  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  const ULONGLONG qUser = static_cast<ULONGLONG>(ru.ru_utime.tv_sec) * 10000000ULL + ru.ru_utime.tv_usec * 10ULL;
  const ULONGLONG qSys = static_cast<ULONGLONG>(ru.ru_stime.tv_sec) * 10000000ULL + ru.ru_stime.tv_usec * 10ULL;
  if (lpCreationTime)
    memset(lpCreationTime, 0, sizeof *lpCreationTime);
  if (lpExitTime)
    memset(lpExitTime, 0, sizeof *lpExitTime);
  if (lpKernelTime) {
    lpKernelTime->dwLowDateTime = static_cast<DWORD>(qSys);
    lpKernelTime->dwHighDateTime = static_cast<DWORD>(qSys >> 32);
  }
  if (lpUserTime) {
    lpUserTime->dwLowDateTime = static_cast<DWORD>(qUser);
    lpUserTime->dwHighDateTime = static_cast<DWORD>(qUser >> 32);
  }
  return TRUE;
}

void __stdcall GetSystemInfo(LPSYSTEM_INFO p)
{
  memset(p, 0, sizeof *p);
#if defined(__aarch64__)
  p->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM64;
#else
  p->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
#endif
  p->dwPageSize = static_cast<DWORD>(sysconf(_SC_PAGESIZE));
  p->dwNumberOfProcessors = static_cast<DWORD>(sysconf(_SC_NPROCESSORS_ONLN));
  p->dwProcessorType = 8664;  // PROCESSOR_AMD_X8664, which every 64-bit Windows reports
  p->dwAllocationGranularity = 65536;
  p->lpMinimumApplicationAddress = reinterpret_cast<LPVOID>(0x10000);
  p->lpMaximumApplicationAddress = reinterpret_cast<LPVOID>(0x7FFFFFFEFFFFULL);
  p->dwActiveProcessorMask = 1;
}

void __stdcall GlobalMemoryStatus(LPMEMORYSTATUS p)
{
  memset(p, 0, sizeof *p);
  p->dwLength = sizeof *p;
#if defined(__linux__)
  struct sysinfo si;
  if (sysinfo(&si) == 0) {
    p->dwTotalPhys = static_cast<SIZE_T>(si.totalram) * si.mem_unit;
    p->dwAvailPhys = static_cast<SIZE_T>(si.freeram + si.bufferram) * si.mem_unit;
    p->dwTotalPageFile = p->dwTotalPhys + static_cast<SIZE_T>(si.totalswap) * si.mem_unit;
    p->dwAvailPageFile = p->dwAvailPhys + static_cast<SIZE_T>(si.freeswap) * si.mem_unit;
    p->dwMemoryLoad = p->dwTotalPhys ? static_cast<DWORD>(100 - p->dwAvailPhys * 100 / p->dwTotalPhys) : 0;
  }
#elif defined(__EMSCRIPTEN__)
  // in a browser: the memory the module may grow to, and what of it is not
  // yet taken; 1.17's start-up checks there is enough (gds_appfat.cpp's MemErrorDlg)
  p->dwTotalPhys = static_cast<SIZE_T>(emscripten_get_heap_max());
  p->dwAvailPhys = p->dwTotalPhys - static_cast<SIZE_T>(emscripten_get_heap_size());
  p->dwTotalPageFile = p->dwTotalPhys;
  p->dwAvailPageFile = p->dwAvailPhys;
  p->dwMemoryLoad = p->dwTotalPhys ? static_cast<DWORD>(100 - p->dwAvailPhys * 100 / p->dwTotalPhys) : 0;
#elif defined(__APPLE__)
  // a Mac's: the machine's memory, what is free or can be had back at once, and the swap
  uint64_t cbTotal = 0;
  size_t cb = sizeof cbTotal;
  if (sysctlbyname("hw.memsize", &cbTotal, &cb, NULL, 0) == 0) {
    vm_statistics64_data_t vm;
    mach_msg_type_number_t nCount = HOST_VM_INFO64_COUNT;
    uint64_t cbAvail = 0;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &nCount) ==
        KERN_SUCCESS)
      cbAvail = (static_cast<uint64_t>(vm.free_count) + vm.inactive_count) * vm_kernel_page_size;
    struct xsw_usage swap;
    memset(&swap, 0, sizeof swap);
    cb = sizeof swap;
    sysctlbyname("vm.swapusage", &swap, &cb, NULL, 0);
    p->dwTotalPhys = static_cast<SIZE_T>(cbTotal);
    p->dwAvailPhys = static_cast<SIZE_T>(cbAvail);
    p->dwTotalPageFile = p->dwTotalPhys + static_cast<SIZE_T>(swap.xsu_total);
    p->dwAvailPageFile = p->dwAvailPhys + static_cast<SIZE_T>(swap.xsu_avail);
    p->dwMemoryLoad = p->dwTotalPhys ? static_cast<DWORD>(100 - p->dwAvailPhys * 100 / p->dwTotalPhys) : 0;
  }
#endif
  p->dwTotalVirtual = static_cast<SIZE_T>(1) << 47;
  p->dwAvailVirtual = p->dwTotalVirtual;
}

BOOL __stdcall GetComputerNameA(LPSTR lpBuffer, LPDWORD nSize)
{
  char sz[256];
  if (gethostname(sz, sizeof sz) != 0)
    return FALSE;
  sz[sizeof sz - 1] = 0;
  const DWORD n = static_cast<DWORD>(strlen(sz));
  if (n + 1 > *nSize) {
    *nSize = n + 1;
    t_dwLastError = ERROR_INSUFFICIENT_BUFFER;
    return FALSE;
  }
  memcpy(lpBuffer, sz, n + 1);
  *nSize = n;
  return TRUE;
}

BOOL __stdcall GetUserNameA(LPSTR lpBuffer, LPDWORD pcbBuffer)
{
  struct passwd *pw = getpwuid(getuid());
  const char *psz = pw && pw->pw_name ? pw->pw_name : getenv("USER");
  if (psz == NULL)
    psz = "player";
  const DWORD n = static_cast<DWORD>(strlen(psz));
  if (n + 1 > *pcbBuffer) {
    *pcbBuffer = n + 1;
    t_dwLastError = ERROR_INSUFFICIENT_BUFFER;
    return FALSE;
  }
  memcpy(lpBuffer, psz, n + 1);
  *pcbBuffer = n + 1;
  return TRUE;
}

void __stdcall RaiseException(DWORD dwExceptionCode, DWORD, DWORD nNumberOfArguments, const ULONG_PTR *lpArguments)
{
  PortCrashRaise(dwExceptionCode, lpArguments, nNumberOfArguments);
}

// port_crash.cpp installs its own signal handlers off Windows.
LPTOP_LEVEL_EXCEPTION_FILTER __stdcall SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER) { return NULL; }
PVOID __stdcall AddVectoredExceptionHandler(ULONG, PVECTORED_EXCEPTION_HANDLER) { return NULL; }

void __stdcall OutputDebugStringA(LPCSTR lpOutputString) { fputs(lpOutputString, stderr); }
BOOL __stdcall IsDebuggerPresent(void) { return FALSE; }

// ---------------------------------------------------------------------------
// time
// ---------------------------------------------------------------------------
static ULONGLONG MonotonicNs(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<ULONGLONG>(ts.tv_sec) * 1000000000ULL + static_cast<ULONGLONG>(ts.tv_nsec);
}

// The replay observer's hooks (tools/replaytest/rt_observer.cpp). On Windows it replaces
// the exe's imports of GetTickCount, Sleep and CreateEventA; here it asks for each by name,
// and the engine's calls go to its function from then on.
typedef DWORD(__stdcall *PFN_TICKS)(void);
typedef void(__stdcall *PFN_SLEEP)(DWORD);
typedef HANDLE(__stdcall *PFN_CREATE_EVENT)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR);
static PFN_TICKS g_pfnTicksHook;
static PFN_SLEEP g_pfnSleepHook;
static PFN_CREATE_EVENT g_pfnCreateEventHook;

// Milliseconds as Windows' GetTickCount gives them: in steps of the system timer's tick, a
// 64th of a second, 15 or 16 ms. 1.17 times things with it at that grain: the palette's
// cycle runs "at most once every ten milliseconds", which on Windows is 64 times a second,
// and with exact milliseconds the water flowed half again as fast.
static ULONGLONG WindowsTicksMs(void) { return MonotonicNs() / 15625000ULL * 125ULL / 8ULL; }

static DWORD __stdcall RealGetTickCount(void) { return static_cast<DWORD>(WindowsTicksMs()); }
static void __stdcall RealSleep(DWORD dwMilliseconds);
static HANDLE __stdcall RealCreateEventA(LPSECURITY_ATTRIBUTES, BOOL bManualReset, BOOL bInitialState, LPCSTR);

// Installs pfnNew in place of the function named, and returns the function itself, for the
// hook to call; NULL for a name that has no hook.
extern "C" __attribute__((visibility("default"))) void *oc_posix_hook(const char *pszName, void *pfnNew)
{
  if (strcmp(pszName, "GetTickCount") == 0) {
    g_pfnTicksHook = reinterpret_cast<PFN_TICKS>(pfnNew);
    return reinterpret_cast<void *>(&RealGetTickCount);
  }
  if (strcmp(pszName, "Sleep") == 0) {
    g_pfnSleepHook = reinterpret_cast<PFN_SLEEP>(pfnNew);
    return reinterpret_cast<void *>(&RealSleep);
  }
  if (strcmp(pszName, "CreateEventA") == 0) {
    g_pfnCreateEventHook = reinterpret_cast<PFN_CREATE_EVENT>(pfnNew);
    return reinterpret_cast<void *>(&RealCreateEventA);
  }
  return NULL;
}

ULONGLONG __stdcall GetTickCount64(void) { return WindowsTicksMs(); }
DWORD __stdcall GetTickCount(void) { return g_pfnTicksHook ? g_pfnTicksHook() : RealGetTickCount(); }
// timeGetTime is to the millisecond, as on Windows with timeBeginPeriod(1)
DWORD __stdcall timeGetTime(void) { return static_cast<DWORD>(MonotonicNs() / 1000000ULL); }

BOOL __stdcall QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount)
{
  lpPerformanceCount->QuadPart = static_cast<LONGLONG>(MonotonicNs());
  return TRUE;
}

BOOL __stdcall QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency)
{
  lpFrequency->QuadPart = 1000000000LL;
  return TRUE;
}

static void SystemTimeFrom(const struct tm &t, long nMs, LPSYSTEMTIME p)
{
  p->wYear = static_cast<WORD>(t.tm_year + 1900);
  p->wMonth = static_cast<WORD>(t.tm_mon + 1);
  p->wDayOfWeek = static_cast<WORD>(t.tm_wday);
  p->wDay = static_cast<WORD>(t.tm_mday);
  p->wHour = static_cast<WORD>(t.tm_hour);
  p->wMinute = static_cast<WORD>(t.tm_min);
  p->wSecond = static_cast<WORD>(t.tm_sec);
  p->wMilliseconds = static_cast<WORD>(nMs);
}

void __stdcall GetLocalTime(LPSYSTEMTIME lpSystemTime)
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm t;
  localtime_r(&ts.tv_sec, &t);
  SystemTimeFrom(t, ts.tv_nsec / 1000000, lpSystemTime);
}

void __stdcall GetSystemTime(LPSYSTEMTIME lpSystemTime)
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm t;
  gmtime_r(&ts.tv_sec, &t);
  SystemTimeFrom(t, ts.tv_nsec / 1000000, lpSystemTime);
}

void __stdcall GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  *lpSystemTimeAsFileTime = FileTimeFrom(ts);
}

BOOL __stdcall FileTimeToLocalFileTime(const FILETIME *lpFileTime, LPFILETIME lpLocalFileTime)
{
  const ULONGLONG q = Ticks(lpFileTime);
  const time_t t = static_cast<time_t>((q - FILETIME_UNIX_EPOCH) / 10000000ULL);
  struct tm lt;
  localtime_r(&t, &lt);
  const LONGLONG qLocal = static_cast<LONGLONG>(q) + static_cast<LONGLONG>(lt.tm_gmtoff) * 10000000LL;
  lpLocalFileTime->dwLowDateTime = static_cast<DWORD>(qLocal);
  lpLocalFileTime->dwHighDateTime = static_cast<DWORD>(static_cast<ULONGLONG>(qLocal) >> 32);
  return TRUE;
}

BOOL __stdcall FileTimeToSystemTime(const FILETIME *lpFileTime, LPSYSTEMTIME lpSystemTime)
{
  const ULONGLONG q = Ticks(lpFileTime);
  if (q < FILETIME_UNIX_EPOCH) {
    memset(lpSystemTime, 0, sizeof *lpSystemTime);
    lpSystemTime->wYear = 1601;
    lpSystemTime->wMonth = lpSystemTime->wDay = 1;
    return TRUE;
  }
  const time_t t = static_cast<time_t>((q - FILETIME_UNIX_EPOCH) / 10000000ULL);
  struct tm g;
  gmtime_r(&t, &g);
  SystemTimeFrom(g, static_cast<long>((q / 10000ULL) % 1000ULL), lpSystemTime);
  return TRUE;
}

BOOL __stdcall SystemTimeToFileTime(const SYSTEMTIME *p, LPFILETIME lpFileTime)
{
  struct tm g = {};
  g.tm_year = p->wYear - 1900;
  g.tm_mon = p->wMonth - 1;
  g.tm_mday = p->wDay;
  g.tm_hour = p->wHour;
  g.tm_min = p->wMinute;
  g.tm_sec = p->wSecond;
  struct timespec ts;
  ts.tv_sec = timegm(&g);
  ts.tv_nsec = static_cast<long>(p->wMilliseconds) * 1000000L;
  *lpFileTime = FileTimeFrom(ts);
  return TRUE;
}

LONG __stdcall CompareFileTime(const FILETIME *lpFileTime1, const FILETIME *lpFileTime2)
{
  const ULONGLONG a = Ticks(lpFileTime1), b = Ticks(lpFileTime2);
  return a < b ? -1 : a > b ? 1 : 0;
}

DWORD __stdcall GetTimeZoneInformation(LPTIME_ZONE_INFORMATION p)
{
  memset(p, 0, sizeof *p);
  const time_t now = time(NULL);
  struct tm lt;
  localtime_r(&now, &lt);
  // Windows' bias is UTC less local time, in minutes
  p->Bias = static_cast<LONG>(-lt.tm_gmtoff / 60);
  return lt.tm_isdst > 0 ? TIME_ZONE_ID_DAYLIGHT : TIME_ZONE_ID_STANDARD;
}

void __stdcall Sleep(DWORD dwMilliseconds)
{
  if (g_pfnSleepHook)
    g_pfnSleepHook(dwMilliseconds);
  else
    RealSleep(dwMilliseconds);
}

static void __stdcall RealSleep(DWORD dwMilliseconds)
{
#if defined(__EMSCRIPTEN__)
  // a turn for the other fibers and the browser, as long as the sleep
  const ULONGLONG qEnd = MonotonicNs() + dwMilliseconds * 1000000ULL;
  do {
    const ULONGLONG qNow = MonotonicNs();
    WebWait(qNow < qEnd ? static_cast<DWORD>((qEnd - qNow + 999999ULL) / 1000000ULL) : 0);
  } while (MonotonicNs() < qEnd);
#else
  struct timespec ts;
  ts.tv_sec = dwMilliseconds / 1000;
  ts.tv_nsec = static_cast<long>(dwMilliseconds % 1000) * 1000000L;
  while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
  }
#endif
}

// The multimedia timers (port_smacker.cpp): a thread per timer, calling back on time.
namespace {
struct MmTimer {
  UINT uId;
  UINT uDelay;
  LPTIMECALLBACK pfn;
  DWORD_PTR dwUser;
  bool bPeriodic;
  std::atomic<bool> bStop{false};
  pthread_t thread;
  bool bFiberDone = false;  // in a browser: its fiber has ended
};
std::mutex g_mmLock;
std::map<UINT, MmTimer *> g_mmTimers;
UINT g_mmNext = 1;

void *MmTimerThread(void *pv)
{
  MmTimer *t = static_cast<MmTimer *>(pv);
  ULONGLONG qNext = MonotonicNs() + static_cast<ULONGLONG>(t->uDelay) * 1000000ULL;
  while (!t->bStop.load()) {
    const ULONGLONG qNow = MonotonicNs();
    if (qNow < qNext) {
      const ULONGLONG qWait = qNext - qNow;
      struct timespec ts;
      ts.tv_sec = static_cast<time_t>(qWait / 1000000000ULL);
      ts.tv_nsec = static_cast<long>(qWait % 1000000000ULL);
      nanosleep(&ts, NULL);
      continue;
    }
    t->pfn(t->uId, 0, t->dwUser, 0, 0);
    if (!t->bPeriodic)
      break;
    qNext += static_cast<ULONGLONG>(t->uDelay ? t->uDelay : 1) * 1000000ULL;
  }
  return NULL;
}

#if defined(__EMSCRIPTEN__)
// A timer's fiber, in a browser: its turns until it is due, and its end once it is killed,
// which has no thread to wait for.
void MmTimerFiber(void *pv)
{
  MmTimer *t = static_cast<MmTimer *>(pv);
  ULONGLONG qNext = MonotonicNs() + static_cast<ULONGLONG>(t->uDelay) * 1000000ULL;
  while (!t->bStop.load()) {
    if (MonotonicNs() < qNext) {
      WebWait(0);
      continue;
    }
    t->pfn(t->uId, 0, t->dwUser, 0, 0);
    if (!t->bPeriodic)
      break;
    qNext += static_cast<ULONGLONG>(t->uDelay ? t->uDelay : 1) * 1000000ULL;
  }
  // the last of the fiber and timeKillEvent lets it go
  t->bFiberDone = true;
  if (t->bStop.load())
    delete t;
}
#endif
}  // namespace

UINT __stdcall timeSetEvent(UINT uDelay, UINT, LPTIMECALLBACK fptc, DWORD_PTR dwUser, UINT fuEvent)
{
  MmTimer *t = new MmTimer;
  t->uDelay = uDelay;
  t->pfn = fptc;
  t->dwUser = dwUser;
  t->bPeriodic = (fuEvent & TIME_PERIODIC) != 0;
  {
    std::lock_guard<std::mutex> lock(g_mmLock);
    t->uId = g_mmNext++;
    g_mmTimers[t->uId] = t;
  }
#if defined(__EMSCRIPTEN__)
  WebFiberStart(MmTimerFiber, t, 1 << 18);
  return t->uId;
#endif
  if (pthread_create(&t->thread, NULL, MmTimerThread, t) != 0) {
    std::lock_guard<std::mutex> lock(g_mmLock);
    g_mmTimers.erase(t->uId);
    delete t;
    return 0;
  }
  return t->uId;
}

UINT __stdcall timeKillEvent(UINT uTimerID)
{
  MmTimer *t = NULL;
  {
    std::lock_guard<std::mutex> lock(g_mmLock);
    auto it = g_mmTimers.find(uTimerID);
    if (it == g_mmTimers.end())
      return 97;  // MMSYSERR_INVALPARAM
    t = it->second;
    g_mmTimers.erase(it);
  }
  t->bStop.store(true);
#if defined(__EMSCRIPTEN__)
  if (t->bFiberDone)
    delete t;  // else its fiber lets it go (MmTimerFiber)
  return TIMERR_NOERROR;
#endif
  if (pthread_equal(t->thread, pthread_self()))
    pthread_detach(t->thread);  // a callback killing its own timer: the thread ends after it
  else
    pthread_join(t->thread, NULL);
  delete t;
  return TIMERR_NOERROR;
}

UINT __stdcall timeBeginPeriod(UINT) { return TIMERR_NOERROR; }
UINT __stdcall timeEndPeriod(UINT) { return TIMERR_NOERROR; }

// ---------------------------------------------------------------------------
// threads, events and waits
// ---------------------------------------------------------------------------
static void *ThreadStart(void *pv)
{
  ThreadObject *t = static_cast<ThreadObject *>(pv);
  {
    std::unique_lock<std::mutex> lock(g_wait);
    t->dwId = GetCurrentThreadId();
    g_waitChanged.wait(lock, [t] { return !t->bSuspended; });
  }
  const DWORD dwExit = t->pfn(t->pArg);
  {
    std::lock_guard<std::mutex> lock(g_wait);
    t->bDone = true;
    t->dwExit = dwExit;
  }
  g_waitChanged.notify_all();
  ThreadRelease(t);
  return NULL;
}

#if defined(__EMSCRIPTEN__)
// A thread in a browser: a fiber (WebFiberStart), which runs once it is resumed.
static void WebThreadStart(void *pv)
{
  ThreadObject *t = static_cast<ThreadObject *>(pv);
  while (t->bSuspended)
    WebWait(0);
  const DWORD dwExit = t->pfn(t->pArg);
  t->bDone = true;
  t->dwExit = dwExit;
  ThreadRelease(t);
}
#endif

HANDLE __stdcall CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress,
                              LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId)
{
  ThreadObject *t = new ThreadObject;
  t->pfn = lpStartAddress;
  t->pArg = lpParameter;
  t->bSuspended = (dwCreationFlags & CREATE_SUSPENDED) != 0;
#if defined(__EMSCRIPTEN__)
  t->dwId = WebFiberStart(WebThreadStart, t, dwStackSize > 0x100000 ? dwStackSize : 0x100000)->dwId;
  if (lpThreadId)
    *lpThreadId = t->dwId;
  return t;
#endif
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  // Windows' default stack is 1 MiB; the game's threads were made for it
  pthread_attr_setstacksize(&attr, dwStackSize > 0x100000 ? dwStackSize : 0x100000 * 4);
  const int r = pthread_create(&t->thread, &attr, ThreadStart, t);
  pthread_attr_destroy(&attr);
  if (r != 0) {
    delete t;
    t_dwLastError = ERROR_NOT_ENOUGH_MEMORY;
    return NULL;
  }
  if (lpThreadId) {
    // the id is the thread's own, once it has started
    std::unique_lock<std::mutex> lock(g_wait);
    while (t->dwId == 0 && !t->bSuspended) {
      lock.unlock();
      sched_yield();
      lock.lock();
    }
    *lpThreadId = t->dwId ? t->dwId : static_cast<DWORD>(reinterpret_cast<uintptr_t>(t));
  }
  return t;
}

// Only a thread made suspended can be resumed; no other thread can be stopped.
DWORD __stdcall SuspendThread(HANDLE) { return static_cast<DWORD>(-1); }

DWORD __stdcall ResumeThread(HANDLE hThread)
{
  Object *p = ObjectOf(hThread);
  if (p == NULL || p->kind != KIND_THREAD)
    return static_cast<DWORD>(-1);
  ThreadObject *t = static_cast<ThreadObject *>(p);
  DWORD dwWas;
  {
    std::lock_guard<std::mutex> lock(g_wait);
    dwWas = t->bSuspended ? 1 : 0;
    t->bSuspended = false;
  }
  g_waitChanged.notify_all();
  return dwWas;
}

BOOL __stdcall GetThreadContext(HANDLE, PCONTEXT) { return FALSE; }
BOOL __stdcall SetThreadPriority(HANDLE, int) { return TRUE; }

BOOL __stdcall GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode)
{
  Object *p = ObjectOf(hThread);
  if (p == NULL || p->kind != KIND_THREAD)
    return FALSE;
  std::lock_guard<std::mutex> lock(g_wait);
  *lpExitCode = static_cast<ThreadObject *>(p)->dwExit;
  return TRUE;
}

// Events have no names off Windows, so a named one is always new.
static HANDLE __stdcall RealCreateEventA(LPSECURITY_ATTRIBUTES, BOOL bManualReset, BOOL bInitialState, LPCSTR)
{
  t_dwLastError = ERROR_SUCCESS;
  return new EventObject(bManualReset != FALSE, bInitialState != FALSE);
}

HANDLE __stdcall CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState,
                              LPCSTR lpName)
{
  return g_pfnCreateEventHook ? g_pfnCreateEventHook(lpEventAttributes, bManualReset, bInitialState, lpName)
                              : RealCreateEventA(lpEventAttributes, bManualReset, bInitialState, lpName);
}

BOOL __stdcall SetEvent(HANDLE hEvent)
{
  Object *p = ObjectOf(hEvent);
  if (p == NULL || p->kind != KIND_EVENT)
    return FALSE;
  {
    std::lock_guard<std::mutex> lock(g_wait);
    static_cast<EventObject *>(p)->bSet = true;
  }
  g_waitChanged.notify_all();
  return TRUE;
}

BOOL __stdcall ResetEvent(HANDLE hEvent)
{
  Object *p = ObjectOf(hEvent);
  if (p == NULL || p->kind != KIND_EVENT)
    return FALSE;
  std::lock_guard<std::mutex> lock(g_wait);
  static_cast<EventObject *>(p)->bSet = false;
  return TRUE;
}

// Whether an object is signalled, under g_wait; an auto-reset event is taken.
static bool Signalled(Object *p, bool bTake)
{
  switch (p->kind) {
  case KIND_EVENT: {
    EventObject *e = static_cast<EventObject *>(p);
    if (!e->bSet)
      return false;
    if (bTake && !e->bManual)
      e->bSet = false;
    return true;
  }
  case KIND_THREAD:
    return static_cast<ThreadObject *>(p)->bDone;
  case KIND_PROCESS:
    ProcessPoll(static_cast<ProcessObject *>(p));
    return static_cast<ProcessObject *>(p)->bDone;
  default:
    return true;
  }
}

DWORD __stdcall WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll, DWORD dwMilliseconds)
{
  if (nCount == 0 || lpHandles == NULL)
    return WAIT_FAILED;
  std::vector<Object *> objs;
  bool bProcess = false;
  for (DWORD i = 0; i < nCount; i++) {
    Object *p = ObjectOf(lpHandles[i]);
    if (p == NULL) {
      t_dwLastError = ERROR_INVALID_HANDLE;
      return WAIT_FAILED;
    }
    bProcess |= p->kind == KIND_PROCESS;
    objs.push_back(p);
  }
  const ULONGLONG qEnd = dwMilliseconds == INFINITE ? 0 : MonotonicNs() + dwMilliseconds * 1000000ULL;
  std::unique_lock<std::mutex> lock(g_wait);
  for (;;) {
    if (bWaitAll) {
      bool bAll = true;
      for (Object *p : objs)
        bAll = bAll && Signalled(p, false);
      if (bAll) {
        for (Object *p : objs)
          Signalled(p, true);
        return WAIT_OBJECT_0;
      }
    } else {
      for (DWORD i = 0; i < nCount; i++)
        if (Signalled(objs[i], true))
          return WAIT_OBJECT_0 + i;
    }
    if (dwMilliseconds == 0)
      return WAIT_TIMEOUT;
#if defined(__EMSCRIPTEN__)
    // in a browser: the other fibers' turns, which are what can signal it
    if (dwMilliseconds != INFINITE && MonotonicNs() >= qEnd)
      return WAIT_TIMEOUT;
    lock.unlock();
    WebWait(0);
    lock.lock();
    continue;
#endif
    // a process ends without telling the lock, so its waits look again every 10 ms
    if (dwMilliseconds == INFINITE && !bProcess) {
      g_waitChanged.wait(lock);
    } else {
      const ULONGLONG qNow = MonotonicNs();
      if (dwMilliseconds != INFINITE && qNow >= qEnd)
        return WAIT_TIMEOUT;
      ULONGLONG qLeft = dwMilliseconds == INFINITE ? 10000000ULL : qEnd - qNow;
      if (bProcess && qLeft > 10000000ULL)
        qLeft = 10000000ULL;
      g_waitChanged.wait_for(lock, std::chrono::nanoseconds(qLeft));
    }
  }
}

DWORD __stdcall WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds)
{
  return WaitForMultipleObjects(1, &hHandle, FALSE, dwMilliseconds);
}

#if defined(__EMSCRIPTEN__)
// In a browser the fiber that holds it and how often, in the same space; another waits its
// turns until it is let go.
struct WebSection {
  DWORD dwOwner;
  int nCount;
};
static_assert(sizeof(WebSection) <= sizeof(((CRITICAL_SECTION *)0)->aSpace), "a section fits");

void __stdcall InitializeCriticalSection(LPCRITICAL_SECTION p)
{
  WebSection *s = reinterpret_cast<WebSection *>(p->aSpace);
  s->dwOwner = 0;
  s->nCount = 0;
  p->pImpl = s;
}

void __stdcall DeleteCriticalSection(LPCRITICAL_SECTION p) { p->pImpl = NULL; }

BOOL __stdcall TryEnterCriticalSection(LPCRITICAL_SECTION p)
{
  if (p->pImpl == NULL)
    InitializeCriticalSection(p);
  WebSection *s = static_cast<WebSection *>(p->pImpl);
  const DWORD dwMe = GetCurrentThreadId();
  if (s->nCount != 0 && s->dwOwner != dwMe)
    return FALSE;
  s->dwOwner = dwMe;
  s->nCount++;
  return TRUE;
}

void __stdcall EnterCriticalSection(LPCRITICAL_SECTION p)
{
  while (!TryEnterCriticalSection(p))
    WebWait(0);
}

void __stdcall LeaveCriticalSection(LPCRITICAL_SECTION p)
{
  WebSection *s = static_cast<WebSection *>(p->pImpl);
  if (s != NULL && s->nCount > 0 && --s->nCount == 0)
    s->dwOwner = 0;
}
#else
// The critical section's recursive pthread mutex is kept apart: it is bigger than a section on
// Windows x64 (40 bytes on Linux with the pointer before it, 64 on a Mac), and 1.17's sections
// have only that room.
void __stdcall InitializeCriticalSection(LPCRITICAL_SECTION p)
{
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_t *m = static_cast<pthread_mutex_t *>(malloc(sizeof(pthread_mutex_t)));
  pthread_mutex_init(m, &attr);
  pthread_mutexattr_destroy(&attr);
  p->pImpl = m;
}

void __stdcall DeleteCriticalSection(LPCRITICAL_SECTION p)
{
  if (p->pImpl) {
    pthread_mutex_destroy(static_cast<pthread_mutex_t *>(p->pImpl));
    free(p->pImpl);
  }
  p->pImpl = NULL;
}

void __stdcall EnterCriticalSection(LPCRITICAL_SECTION p)
{
  if (p->pImpl == NULL)
    InitializeCriticalSection(p);
  pthread_mutex_lock(static_cast<pthread_mutex_t *>(p->pImpl));
}

void __stdcall LeaveCriticalSection(LPCRITICAL_SECTION p) { pthread_mutex_unlock(static_cast<pthread_mutex_t *>(p->pImpl)); }

BOOL __stdcall TryEnterCriticalSection(LPCRITICAL_SECTION p)
{
  if (p->pImpl == NULL)
    InitializeCriticalSection(p);
  return pthread_mutex_trylock(static_cast<pthread_mutex_t *>(p->pImpl)) == 0;
}
#endif

// A slim lock is a pointer's room, all zero to begin with: it is one of a table of
// mutexes, chosen by its address.
static std::mutex g_srw[64];

static std::mutex &SrwOf(PSRWLOCK p) { return g_srw[(reinterpret_cast<uintptr_t>(p) >> 3) % 64]; }

void __stdcall AcquireSRWLockExclusive(PSRWLOCK SRWLock) { SrwOf(SRWLock).lock(); }
void __stdcall ReleaseSRWLockExclusive(PSRWLOCK SRWLock) { SrwOf(SRWLock).unlock(); }

LONG __stdcall InterlockedIncrement(LONG volatile *Addend) { return __sync_add_and_fetch(Addend, 1); }
LONG __stdcall InterlockedDecrement(LONG volatile *Addend) { return __sync_sub_and_fetch(Addend, 1); }
LONG __stdcall InterlockedExchange(LONG volatile *Target, LONG Value) { return __sync_lock_test_and_set(Target, Value); }

LONG __stdcall InterlockedExchangeAdd(LONG volatile *Addend, LONG Value) { return __sync_fetch_and_add(Addend, Value); }

LONG __stdcall InterlockedCompareExchange(LONG volatile *Destination, LONG Exchange, LONG Comparand)
{
  return __sync_val_compare_and_swap(Destination, Comparand, Exchange);
}

// ---------------------------------------------------------------------------
// memory
// ---------------------------------------------------------------------------
static int ProtFrom(DWORD flProtect)
{
  switch (flProtect & 0xFF) {
  case PAGE_NOACCESS: return PROT_NONE;
  case PAGE_READONLY: return PROT_READ;
  case PAGE_EXECUTE: return PROT_EXEC;
  case PAGE_EXECUTE_READ: return PROT_READ | PROT_EXEC;
  case PAGE_EXECUTE_READWRITE: return PROT_READ | PROT_WRITE | PROT_EXEC;
  default: return PROT_READ | PROT_WRITE;
  }
}

// Each reservation's size, for VirtualFree's MEM_RELEASE, which names only its start.
static std::mutex g_vmLock;
static std::map<uintptr_t, size_t> g_vmSizes;

static size_t PageRound(size_t cb)
{
  const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  return (cb + page - 1) & ~(page - 1);
}

LPVOID __stdcall VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect)
{
  if (dwSize == 0) {
    t_dwLastError = ERROR_INVALID_PARAMETER;
    return NULL;
  }
  if (lpAddress != NULL && (flAllocationType & MEM_COMMIT) && !(flAllocationType & MEM_RESERVE)) {
    // committing inside a reservation
    const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    const uintptr_t a = reinterpret_cast<uintptr_t>(lpAddress) & ~(page - 1);
    const size_t cb = PageRound(reinterpret_cast<uintptr_t>(lpAddress) + dwSize - a);
    if (mprotect(reinterpret_cast<void *>(a), cb, ProtFrom(flProtect)) != 0) {
      FailErrno();
      return NULL;
    }
    return lpAddress;
  }
  const size_t cb = PageRound(dwSize);
  const int prot = (flAllocationType & MEM_COMMIT) ? ProtFrom(flProtect) : PROT_NONE;
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(MAP_FIXED_NOREPLACE)
  if (lpAddress != NULL)
    flags |= MAP_FIXED_NOREPLACE;
#endif
  void *p = mmap(lpAddress, cb, prot, flags, -1, 0);
  // an address the caller asked for is kept or the call fails, as Windows does it: mmap
  // would hand back another address, and a kernel without the flag above still does
  if (p != MAP_FAILED && lpAddress != NULL && p != lpAddress) {
    munmap(p, cb);
    p = MAP_FAILED;
  }
  if (p == MAP_FAILED) {
    t_dwLastError = ERROR_NOT_ENOUGH_MEMORY;
    return NULL;
  }
  std::lock_guard<std::mutex> lock(g_vmLock);
  g_vmSizes[reinterpret_cast<uintptr_t>(p)] = cb;
  return p;
}

BOOL __stdcall VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType)
{
  if (dwFreeType & MEM_RELEASE) {
    size_t cb = 0;
    {
      std::lock_guard<std::mutex> lock(g_vmLock);
      auto it = g_vmSizes.find(reinterpret_cast<uintptr_t>(lpAddress));
      if (it == g_vmSizes.end()) {
        t_dwLastError = ERROR_INVALID_PARAMETER;
        return FALSE;
      }
      cb = it->second;
      g_vmSizes.erase(it);
    }
    return munmap(lpAddress, cb) == 0;
  }
  if (dwFreeType & MEM_DECOMMIT) {
    const size_t cb = PageRound(dwSize);
    madvise(lpAddress, cb, MADV_DONTNEED);
    return mprotect(lpAddress, cb, PROT_NONE) == 0;
  }
  return FALSE;
}

BOOL __stdcall VirtualProtect(LPVOID lpAddress, SIZE_T dwSize, DWORD flNewProtect, PDWORD lpflOldProtect)
{
  const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  const uintptr_t a = reinterpret_cast<uintptr_t>(lpAddress) & ~(page - 1);
  const size_t cb = PageRound(reinterpret_cast<uintptr_t>(lpAddress) + dwSize - a);
  if (lpflOldProtect)
    *lpflOldProtect = PAGE_READWRITE;  // not known off Windows; the callers put back what they read
  if (mprotect(reinterpret_cast<void *>(a), cb, ProtFrom(flNewProtect)) != 0) {
    FailErrno();
    return FALSE;
  }
  return TRUE;
}

SIZE_T __stdcall VirtualQuery(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T) { return 0; }

HLOCAL __stdcall LocalAlloc(UINT uFlags, SIZE_T uBytes)
{
  return (uFlags & LMEM_ZEROINIT) ? calloc(1, uBytes ? uBytes : 1) : malloc(uBytes ? uBytes : 1);
}

HLOCAL __stdcall LocalFree(HLOCAL hMem)
{
  free(hMem);
  return NULL;
}

HGLOBAL __stdcall GlobalAlloc(UINT uFlags, SIZE_T dwBytes) { return LocalAlloc(uFlags, dwBytes); }
HGLOBAL __stdcall GlobalFree(HGLOBAL hMem) { return LocalFree(hMem); }

// A named mapping of memory alone (port_instance.cpp's): a file in the temporary folder.
// On Windows the mapping is gone once no process holds a handle to it; here each holder
// keeps a shared lock on the file, so a file nobody locks is one a process left behind,
// and it is made new.
HANDLE __stdcall CreateFileMappingA(HANDLE hFile, LPSECURITY_ATTRIBUTES, DWORD, DWORD dwMaximumSizeHigh,
                                    DWORD dwMaximumSizeLow, LPCSTR lpName)
{
  const size_t cb = static_cast<size_t>((static_cast<ULONGLONG>(dwMaximumSizeHigh) << 32) | dwMaximumSizeLow);
  if (hFile != INVALID_HANDLE_VALUE || cb == 0) {
    t_dwLastError = ERROR_NOT_SUPPORTED;
    return NULL;
  }
  int fd;
  bool bExisted = false;
  if (lpName && *lpName) {
    const char *pszTmp = getenv("TMPDIR");
    std::string path = std::string(pszTmp && *pszTmp ? pszTmp : "/tmp") + "/hyperion-map-";
    for (const char *p = lpName; *p; p++)
      path += isalnum(static_cast<unsigned char>(*p)) ? *p : '_';
    fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd >= 0) {
      if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        if (ftruncate(fd, 0) != 0)
          FailErrno();
        flock(fd, LOCK_SH);
      } else {
        bExisted = true;
        flock(fd, LOCK_SH);
      }
    }
  } else {
    char tmpl[] = "/tmp/hyperion_mapXXXXXX";
    fd = mkstemp(tmpl);
    if (fd >= 0)
      unlink(tmpl);
  }
  if (fd < 0) {
    FailErrno();
    return NULL;
  }
  if (!bExisted && ftruncate(fd, static_cast<off_t>(cb)) != 0) {
    FailErrno();
    close(fd);
    return NULL;
  }
  t_dwLastError = bExisted ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS;
  return new MappingObject(fd, cb);
}

LPVOID __stdcall MapViewOfFile(HANDLE hFileMappingObject, DWORD, DWORD, DWORD, SIZE_T dwNumberOfBytesToMap)
{
  Object *p = ObjectOf(hFileMappingObject);
  if (p == NULL || p->kind != KIND_MAPPING)
    return NULL;
  MappingObject *m = static_cast<MappingObject *>(p);
  const size_t cb = dwNumberOfBytesToMap ? dwNumberOfBytesToMap : m->cb;
  void *pv = mmap(NULL, cb, PROT_READ | PROT_WRITE, MAP_SHARED, m->fd, 0);
  if (pv == MAP_FAILED) {
    FailErrno();
    return NULL;
  }
  std::lock_guard<std::mutex> lock(g_vmLock);
  g_vmSizes[reinterpret_cast<uintptr_t>(pv)] = cb;
  return pv;
}

BOOL __stdcall UnmapViewOfFile(LPCVOID lpBaseAddress)
{
  return VirtualFree(const_cast<LPVOID>(lpBaseAddress), 0, MEM_RELEASE);
}

// ---------------------------------------------------------------------------
// modules and their resources
// ---------------------------------------------------------------------------
// Hyperion's own resources, which the build links in (resources_posix.cpp).
struct PortPosixResource {
  int nType;            // a resource type's number (10 for RCDATA)
  const char *pszName;  // a name, or NULL for a number
  int nName;
  const unsigned char *pData;
  unsigned cb;
};
extern const PortPosixResource g_aPortPosixResources[];
extern const int g_nPortPosixResources;

namespace {

struct Resource {
  const BYTE *pData;
  DWORD cb;
};

struct ModuleObject : Object {
  std::string path;
  std::vector<BYTE> file;
  std::vector<std::unique_ptr<Resource>> found;  // what FindResource has handed out
  bool bMain = false;
  ModuleObject() : Object(KIND_MODULE) {}
};

ModuleObject g_mainModule;
std::mutex g_moduleLock;

ModuleObject *ModuleOf(HMODULE h)
{
  if (h == NULL)
    return &g_mainModule;
  Object *p = ObjectOf(h);
  return p && p->kind == KIND_MODULE ? static_cast<ModuleObject *>(p) : NULL;
}

std::string ExePath(void)
{
  char sz[4096];
#if defined(__linux__)
  const ssize_t n = readlink("/proc/self/exe", sz, sizeof sz - 1);
  if (n <= 0)
    return "Hyperion";
  sz[n] = 0;
  return sz;
#elif defined(__EMSCRIPTEN__)
  // in a browser, the program's folder is the one the page fills with the game's files
  // (engine/web/play), with Data\ beside it for its data
  (void)sz;
  return "/hyperion/Hyperion.wasm";
#else
  uint32_t cb = sizeof sz;
  return _NSGetExecutablePath(sz, &cb) == 0 ? sz : "Hyperion";
#endif
}

#if defined(__APPLE__)
// A Mac app's folder, the one Hyperion.app is in, when exe is an app's program (its
// Contents/MacOS/); else exe's own. A downloaded app still in quarantine is run by macOS from a
// copy of its own in a folder of the system's (App Translocation), so the folder is the
// original's, which the Security framework's SecTranslocateCreateOriginalPathForURL gives.
static std::string MacAppFolder(const std::string &exe)
{
  const std::string own = exe.substr(0, exe.find_last_of('/') + 1);
  const std::string tail = ".app/Contents/MacOS/";
  if (own.size() <= tail.size() || own.compare(own.size() - tail.size(), tail.size(), tail) != 0)
    return own;
  std::string app = own.substr(0, own.size() - tail.size() + 4);  // .../Hyperion.app
  typedef void *(*UrlFromPath)(void *, const unsigned char *, long, unsigned char);
  typedef unsigned char (*PathFromUrl)(void *, unsigned char, unsigned char *, long);
  typedef void (*Release)(void *);
  typedef unsigned char (*IsTranslocated)(void *, bool *, void **);
  typedef void *(*Original)(void *, void **);
  void *pCf = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", RTLD_LAZY);
  void *pSec = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY);
  UrlFromPath pfnUrl = pCf ? reinterpret_cast<UrlFromPath>(dlsym(pCf, "CFURLCreateFromFileSystemRepresentation")) : NULL;
  PathFromUrl pfnPath = pCf ? reinterpret_cast<PathFromUrl>(dlsym(pCf, "CFURLGetFileSystemRepresentation")) : NULL;
  Release pfnRelease = pCf ? reinterpret_cast<Release>(dlsym(pCf, "CFRelease")) : NULL;
  IsTranslocated pfnIs = pSec ? reinterpret_cast<IsTranslocated>(dlsym(pSec, "SecTranslocateIsTranslocatedURL")) : NULL;
  Original pfnOriginal = pSec ? reinterpret_cast<Original>(dlsym(pSec, "SecTranslocateCreateOriginalPathForURL")) : NULL;
  if (pfnUrl && pfnPath && pfnRelease && pfnIs && pfnOriginal) {
    void *pUrl = pfnUrl(NULL, reinterpret_cast<const unsigned char *>(app.c_str()), static_cast<long>(app.size()), 1);
    bool bMoved = false;
    if (pUrl != NULL && pfnIs(pUrl, &bMoved, NULL) && bMoved) {
      void *pFrom = pfnOriginal(pUrl, NULL);
      unsigned char sz[4096];
      if (pFrom != NULL && pfnPath(pFrom, 1, sz, sizeof sz))
        app = reinterpret_cast<const char *>(sz);
      if (pFrom != NULL)
        pfnRelease(pFrom);
    }
    if (pUrl != NULL)
      pfnRelease(pUrl);
  }
  while (!app.empty() && app.back() == '/')
    app.pop_back();
  return app.substr(0, app.find_last_of('/') + 1);
}
#endif

// A PE file's resource directory: the data of a type, name and the first language.
struct PeResources {
  const BYTE *pFile;
  size_t cb;
  const BYTE *pRoot = NULL;  // the directory, in the file
  DWORD dwRootRva = 0;
  std::vector<std::pair<DWORD, DWORD>> sections;  // (rva, file offset) pairs, with sizes below
  std::vector<DWORD> sectionSizes;

  bool Open(const BYTE *p, size_t n)
  {
    pFile = p;
    cb = n;
    if (n < sizeof(IMAGE_DOS_HEADER))
      return false;
    const IMAGE_DOS_HEADER *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(p);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > n)
      return false;
    const IMAGE_NT_HEADERS32 *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(p + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != 0x10B)
      return false;
    const IMAGE_SECTION_HEADER *sh = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
      sections.push_back(std::make_pair(sh[i].VirtualAddress, sh[i].PointerToRawData));
      sectionSizes.push_back(sh[i].SizeOfRawData > sh[i].Misc.VirtualSize ? sh[i].SizeOfRawData : sh[i].Misc.VirtualSize);
    }
    dwRootRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress;
    pRoot = At(dwRootRva, 16);
    return pRoot != NULL;
  }

  const BYTE *At(DWORD rva, DWORD n) const
  {
    for (size_t i = 0; i < sections.size(); i++)
      if (rva >= sections[i].first && rva + n <= sections[i].first + sectionSizes[i]) {
        const size_t off = sections[i].second + (rva - sections[i].first);
        return off + n <= cb ? pFile + off : NULL;
      }
    return NULL;
  }

  // The entry of directory pDir for an id or a name (upper case, as Windows keeps them),
  // or the first entry when both are absent.
  const BYTE *Entry(const BYTE *pDir, LPCSTR pszKey, bool bFirst) const
  {
    const WORD nNamed = *reinterpret_cast<const WORD *>(pDir + 12);
    const WORD nIds = *reinterpret_cast<const WORD *>(pDir + 14);
    // the counts are the file's, and At held only the directory's own 16 bytes: its
    // entries have to be in the file too
    const size_t nEntries = static_cast<size_t>(nNamed) + nIds;
    if (static_cast<size_t>(pDir - pFile) + 16 + 8 * nEntries > cb)
      return NULL;
    const DWORD *pEntries = reinterpret_cast<const DWORD *>(pDir + 16);
    for (size_t i = 0; i < nEntries; i++) {
      const DWORD dwName = pEntries[2 * i], dwOffset = pEntries[2 * i + 1];
      bool bMatch = bFirst;
      if (!bFirst) {
        if (IS_INTRESOURCE(pszKey)) {
          bMatch = (dwName & 0x80000000u) == 0 && dwName == static_cast<DWORD>(reinterpret_cast<uintptr_t>(pszKey));
        } else if (dwName & 0x80000000u) {
          const BYTE *pStr = At(dwRootRva + (dwName & 0x7FFFFFFFu), 2);
          const WORD nLen = pStr ? *reinterpret_cast<const WORD *>(pStr) : 0;
          pStr = pStr ? At(dwRootRva + (dwName & 0x7FFFFFFFu) + 2, nLen * 2u) : NULL;
          if (pStr && strlen(pszKey) == nLen) {
            bMatch = true;
            for (WORD k = 0; k < nLen && bMatch; k++)
              bMatch = toupper(static_cast<unsigned char>(pszKey[k])) == (pStr[2 * k] | (pStr[2 * k + 1] << 8));
          }
        }
      }
      if (bMatch)
        return At(dwRootRva + (dwOffset & 0x7FFFFFFFu), (dwOffset & 0x80000000u) ? 16 : 16);
    }
    return NULL;
  }

  bool Find(LPCSTR pszType, LPCSTR pszName, Resource *pOut) const
  {
    const BYTE *pTypes = pRoot ? Entry(pRoot, pszType, false) : NULL;
    const BYTE *pNames = pTypes ? Entry(pTypes, pszName, false) : NULL;
    const BYTE *pData = pNames ? Entry(pNames, NULL, true) : NULL;
    if (pData == NULL)
      return false;
    const DWORD dwRva = *reinterpret_cast<const DWORD *>(pData), dwSize = *reinterpret_cast<const DWORD *>(pData + 4);
    pOut->pData = At(dwRva, dwSize);
    pOut->cb = dwSize;
    return pOut->pData != NULL;
  }
};

// "#12" names resource 12, as in Windows' resource calls.
LPCSTR ResourceKey(LPCSTR psz)
{
  if (!IS_INTRESOURCE(psz) && psz[0] == '#')
    return MAKEINTRESOURCEA(atoi(psz + 1));
  return psz;
}

bool FindIn(ModuleObject *m, LPCSTR pszType, LPCSTR pszName, Resource *pOut)
{
  pszType = ResourceKey(pszType);
  pszName = ResourceKey(pszName);
  if (m->bMain || m == &g_mainModule) {
    if (!IS_INTRESOURCE(pszType))
      return false;
    const int nType = static_cast<int>(reinterpret_cast<uintptr_t>(pszType));
    for (int i = 0; i < g_nPortPosixResources; i++) {
      const PortPosixResource &r = g_aPortPosixResources[i];
      if (r.nType != nType)
        continue;
      const bool bMatch = IS_INTRESOURCE(pszName)
                              ? r.pszName == NULL && r.nName == static_cast<int>(reinterpret_cast<uintptr_t>(pszName))
                              : r.pszName != NULL && strcasecmp(r.pszName, pszName) == 0;
      if (bMatch) {
        pOut->pData = r.pData;
        pOut->cb = r.cb;
        return true;
      }
    }
    return false;
  }
  PeResources pe;
  return pe.Open(m->file.data(), m->file.size()) && pe.Find(pszType, pszName, pOut);
}

}  // namespace

HMODULE __stdcall GetModuleHandleA(LPCSTR lpModuleName)
{
  if (lpModuleName != NULL) {
    t_dwLastError = 126;  // ERROR_MOD_NOT_FOUND
    return NULL;
  }
  g_mainModule.bMain = true;
  return reinterpret_cast<HMODULE>(static_cast<Object *>(&g_mainModule));
}

HMODULE __stdcall GetModuleHandleW(LPCWSTR lpModuleName)
{
  return lpModuleName ? NULL : GetModuleHandleA(NULL);
}

BOOL __stdcall GetModuleHandleExA(DWORD, LPCSTR, HMODULE *phModule)
{
  *phModule = GetModuleHandleA(NULL);
  return TRUE;
}

const char *PortPosixProgramFolder(void)
{
  static const std::string s_folder = [] {
    std::string exe = ExePath();
#if defined(__APPLE__)
    char sz[PATH_MAX];
    if (realpath(exe.c_str(), sz) != NULL)
      exe = sz;
    return MacAppFolder(exe);
#else
    return exe.substr(0, exe.find_last_of('/') + 1);
#endif
  }();
  return s_folder.c_str();
}

const char *PortProgramFolder(void)
{
  static const std::string s_folder = WinPath(PortPosixProgramFolder());
  return s_folder.c_str();
}

DWORD __stdcall GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize)
{
  ModuleObject *m = ModuleOf(hModule);
  const std::string path =
      m == NULL ? std::string() : WinPath((m->bMain || m == &g_mainModule) ? ExePath() : m->path);
  if (nSize == 0)
    return 0;
  const DWORD n = static_cast<DWORD>(path.size() < nSize - 1 ? path.size() : nSize - 1);
  memcpy(lpFilename, path.c_str(), n);
  lpFilename[n] = 0;
  t_dwLastError = path.size() >= nSize ? ERROR_INSUFFICIENT_BUFFER : ERROR_SUCCESS;
  return n;
}

namespace {
struct SharedObject : Object {
  void *pHandle;
  explicit SharedObject(void *p) : Object(KIND_SHARED), pHandle(p) {}
};

SharedObject *SharedOf(HMODULE h)
{
  Object *p = ObjectOf(h);
  return p && p->kind == KIND_SHARED ? static_cast<SharedObject *>(p) : NULL;
}
}  // namespace

HMODULE __stdcall LoadLibraryA(LPCSTR lpLibFileName)
{
  const std::string path = OcPath(lpLibFileName);
  void *p = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (p == NULL) {
    fprintf(stderr, "LoadLibrary %s: %s\n", path.c_str(), dlerror());
    t_dwLastError = 126;  // ERROR_MOD_NOT_FOUND
    return NULL;
  }
  return reinterpret_cast<HMODULE>(static_cast<Object *>(new SharedObject(p)));
}

HMODULE __stdcall LoadLibraryExA(LPCSTR lpLibFileName, HANDLE, DWORD dwFlags)
{
  if ((dwFlags & (LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)) == 0)
    return LoadLibraryA(lpLibFileName);
  const std::string path = OcPath(lpLibFileName);
  FILE *f = fopen(path.c_str(), "rb");
  if (f == NULL) {
    t_dwLastError = ERROR_FILE_NOT_FOUND;
    return NULL;
  }
  ModuleObject *m = new ModuleObject;
  m->path = path;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  m->file.resize(n > 0 ? static_cast<size_t>(n) : 0);
  const bool bRead = n > 0 && fread(m->file.data(), 1, m->file.size(), f) == m->file.size();
  fclose(f);
  PeResources pe;
  if (!bRead || !pe.Open(m->file.data(), m->file.size())) {
    delete m;
    t_dwLastError = 193;  // ERROR_BAD_EXE_FORMAT
    return NULL;
  }
  return reinterpret_cast<HMODULE>(static_cast<Object *>(m));
}

BOOL __stdcall FreeLibrary(HMODULE hLibModule)
{
  if (SharedObject *so = SharedOf(hLibModule)) {
    dlclose(so->pHandle);
    delete so;
    return TRUE;
  }
  ModuleObject *m = ModuleOf(hLibModule);
  if (m == NULL)
    return FALSE;
  if (!m->bMain && m != &g_mainModule)
    delete m;
  return TRUE;
}

// The program's own exports (hyperion.def's names, which the link makes dynamic) and a
// shared object's; a data file has none.
FARPROC __stdcall GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
  void *p = NULL;
  if (!IS_INTRESOURCE(lpProcName)) {
    if (SharedObject *so = SharedOf(hModule))
      p = dlsym(so->pHandle, lpProcName);
    else if (ModuleOf(hModule) == &g_mainModule)
      p = dlsym(RTLD_DEFAULT, lpProcName);
  }
  if (p == NULL)
    t_dwLastError = 127;  // ERROR_PROC_NOT_FOUND
  return reinterpret_cast<FARPROC>(p);
}

HRSRC __stdcall FindResourceA(HMODULE hModule, LPCSTR lpName, LPCSTR lpType)
{
  ModuleObject *m = ModuleOf(hModule);
  Resource r;
  if (m == NULL || !FindIn(m, lpType, lpName, &r)) {
    t_dwLastError = ERROR_RESOURCE_NAME_NOT_FOUND;
    return NULL;
  }
  std::lock_guard<std::mutex> lock(g_moduleLock);
  // the same resource asked for again gets the handle handed out before: the module keeps each
  // until freed, and the game looks its dialogs and maps up over and over (gds_local, maphdr)
  for (const std::unique_ptr<Resource> &p : m->found)
    if (p->pData == r.pData && p->cb == r.cb)
      return reinterpret_cast<HRSRC>(p.get());
  m->found.push_back(std::unique_ptr<Resource>(new Resource(r)));
  return reinterpret_cast<HRSRC>(m->found.back().get());
}

HGLOBAL __stdcall LoadResource(HMODULE, HRSRC hResInfo)
{
  return hResInfo ? const_cast<BYTE *>(reinterpret_cast<Resource *>(hResInfo)->pData) : NULL;
}

LPVOID __stdcall LockResource(HGLOBAL hResData) { return hResData; }

DWORD __stdcall SizeofResource(HMODULE, HRSRC hResInfo)
{
  return hResInfo ? reinterpret_cast<Resource *>(hResInfo)->cb : 0;
}

// Windows-1252, the game's code page: 0x80 to 0x9F, and the rest is Latin-1.
static const WORD k1252[32] = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                               0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                               0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

static WORD From1252(BYTE b) { return b >= 0x80 && b < 0xA0 ? k1252[b - 0x80] : b; }

static int To1252(unsigned u)
{
  if (u < 0x80 || (u >= 0xA0 && u < 0x100))
    return static_cast<int>(u);
  for (int i = 0; i < 32; i++)
    if (k1252[i] == u)
      return 0x80 + i;
  return -1;
}

int __stdcall LoadStringA(HINSTANCE hInstance, UINT uID, LPSTR lpBuffer, int cchBufferMax)
{
  ModuleObject *m = ModuleOf(hInstance);
  Resource r;
  if (m == NULL || !FindIn(m, RT_STRING, MAKEINTRESOURCEA(uID / 16 + 1), &r)) {
    if (lpBuffer && cchBufferMax > 0)
      lpBuffer[0] = 0;
    return 0;
  }
  // a block of 16 counted UTF-16 strings
  const BYTE *p = r.pData, *pEnd = r.pData + r.cb;
  for (UINT i = 0; i < uID % 16 && p + 2 <= pEnd; i++)
    p += 2 + 2 * (p[0] | (p[1] << 8));
  if (p + 2 > pEnd || lpBuffer == NULL || cchBufferMax <= 0)
    return 0;
  const int nLen = p[0] | (p[1] << 8);
  p += 2;
  int n = 0;
  for (; n < nLen && n < cchBufferMax - 1 && p + 2 * n + 2 <= pEnd; n++) {
    const int c = To1252(p[2 * n] | (p[2 * n + 1] << 8));
    lpBuffer[n] = static_cast<char>(c < 0 ? '?' : c);
  }
  lpBuffer[n] = 0;
  return n;
}

// The version block of a file's RT_VERSION resource; for the program itself, which is
// not a PE file, the build's table's.
static bool VersionOf(LPCSTR lptstrFilename, Resource *pOut, HMODULE *phModule)
{
  *phModule = NULL;
  if (lptstrFilename != NULL && OcPath(lptstrFilename) == ExePath())
    return FindIn(&g_mainModule, RT_VERSION, MAKEINTRESOURCEA(1), pOut);
  *phModule = LoadLibraryExA(lptstrFilename, NULL, LOAD_LIBRARY_AS_DATAFILE);
  return *phModule != NULL && FindIn(ModuleOf(*phModule), RT_VERSION, MAKEINTRESOURCEA(1), pOut);
}

DWORD __stdcall GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle)
{
  if (lpdwHandle)
    *lpdwHandle = 0;
  Resource r;
  HMODULE h;
  const DWORD cb = VersionOf(lptstrFilename, &r, &h) ? r.cb : 0;
  if (h)
    FreeLibrary(h);
  return cb;
}

BOOL __stdcall GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD, DWORD dwLen, LPVOID lpData)
{
  Resource r;
  HMODULE h;
  const bool bFound = VersionOf(lptstrFilename, &r, &h);
  if (bFound)
    memcpy(lpData, r.pData, r.cb < dwLen ? r.cb : dwLen);
  if (h)
    FreeLibrary(h);
  return bFound;
}

// Only the root block, "\\": the VS_FIXEDFILEINFO after the key "VS_VERSION_INFO".
BOOL __stdcall VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID *lplpBuffer, PUINT puLen)
{
  if (pBlock == NULL || lpSubBlock == NULL || strcmp(lpSubBlock, "\\") != 0)
    return FALSE;
  const BYTE *p = static_cast<const BYTE *>(pBlock) + 40;  // 6 bytes of header, 32 of key, aligned to 4
  if (*reinterpret_cast<const DWORD *>(p) != 0xFEEF04BDu)
    return FALSE;
  *lplpBuffer = const_cast<BYTE *>(p);
  if (puLen)
    *puLen = sizeof(VS_FIXEDFILEINFO);
  return TRUE;
}

// ---------------------------------------------------------------------------
// strings and code pages
// ---------------------------------------------------------------------------
int __stdcall lstrlenA(LPCSTR lpString) { return lpString ? static_cast<int>(strlen(lpString)) : 0; }

LPSTR __stdcall lstrcpyA(LPSTR lpString1, LPCSTR lpString2) { return strcpy(lpString1, lpString2); }

LPSTR __stdcall lstrcpynA(LPSTR lpString1, LPCSTR lpString2, int iMaxLength)
{
  if (iMaxLength <= 0)
    return lpString1;
  int i = 0;
  for (; i < iMaxLength - 1 && lpString2[i]; i++)
    lpString1[i] = lpString2[i];
  lpString1[i] = 0;
  return lpString1;
}

LPSTR __stdcall lstrcatA(LPSTR lpString1, LPCSTR lpString2) { return strcat(lpString1, lpString2); }
int __stdcall lstrcmpA(LPCSTR lpString1, LPCSTR lpString2) { return strcmp(lpString1, lpString2); }
int __stdcall lstrcmpiA(LPCSTR lpString1, LPCSTR lpString2) { return strcasecmp(lpString1, lpString2); }

// Case in Windows-1252: ASCII, and the Latin-1 letters.
static int Lower1252(int c)
{
  if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
    return c + 0x20;
  if (c == 0x8A || c == 0x8C || c == 0x8E)
    return c + 0x10;
  if (c == 0x9F)
    return 0xFF;
  return c;
}

static int Upper1252(int c)
{
  if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7))
    return c - 0x20;
  if (c == 0x9A || c == 0x9C || c == 0x9E)
    return c - 0x10;
  if (c == 0xFF)
    return 0x9F;
  return c;
}

LPSTR __stdcall CharLowerA(LPSTR lpsz)
{
  if (IS_INTRESOURCE(lpsz))
    return reinterpret_cast<LPSTR>(static_cast<uintptr_t>(Lower1252(static_cast<int>(reinterpret_cast<uintptr_t>(lpsz) & 0xFF))));
  for (unsigned char *p = reinterpret_cast<unsigned char *>(lpsz); *p; p++)
    *p = static_cast<unsigned char>(Lower1252(*p));
  return lpsz;
}

LPSTR __stdcall CharUpperA(LPSTR lpsz)
{
  if (IS_INTRESOURCE(lpsz))
    return reinterpret_cast<LPSTR>(static_cast<uintptr_t>(Upper1252(static_cast<int>(reinterpret_cast<uintptr_t>(lpsz) & 0xFF))));
  for (unsigned char *p = reinterpret_cast<unsigned char *>(lpsz); *p; p++)
    *p = static_cast<unsigned char>(Upper1252(*p));
  return lpsz;
}

LPSTR __stdcall CharNextA(LPCSTR lpsz) { return const_cast<LPSTR>(*lpsz ? lpsz + 1 : lpsz); }

// wsprintf's buffer is 1024 characters at most.
int __stdcall wvsprintfA(LPSTR lpOut, LPCSTR lpFmt, va_list arglist)
{
  const int n = vsnprintf(lpOut, 1024, lpFmt, arglist);
  return n < 0 ? 0 : n > 1023 ? 1023 : n;
}

int __cdecl wsprintfA(LPSTR lpOut, LPCSTR lpFmt, ...)
{
  va_list va;
  va_start(va, lpFmt);
  const int n = wvsprintfA(lpOut, lpFmt, va);
  va_end(va);
  return n;
}

BOOL __stdcall IsDBCSLeadByte(BYTE) { return FALSE; }
LANGID __stdcall GetUserDefaultLangID(void) { return 0x0409; }
LANGID __stdcall GetSystemDefaultLangID(void) { return 0x0409; }

int __stdcall MulDiv(int nNumber, int nNumerator, int nDenominator)
{
  if (nDenominator == 0)
    return -1;
  long long q = static_cast<long long>(nNumber) * nNumerator;
  // rounded half away from zero, as Windows rounds
  const long long d = nDenominator;
  q = ((q < 0) != (d < 0)) ? (q - d / 2) / d : (q + d / 2) / d;
  return q > 0x7FFFFFFFLL || q < -0x7FFFFFFFLL - 1 ? -1 : static_cast<int>(q);
}

int __stdcall MultiByteToWideChar(UINT CodePage, DWORD, LPCSTR lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr,
                                  int cchWideChar)
{
  const unsigned char *p = reinterpret_cast<const unsigned char *>(lpMultiByteStr);
  const size_t n = cbMultiByte < 0 ? strlen(lpMultiByteStr) + 1 : static_cast<size_t>(cbMultiByte);
  std::vector<WORD> out;
  if (CodePage == CP_UTF8) {
    for (size_t i = 0; i < n;) {
      unsigned u = p[i++];
      int nMore = u >= 0xF0 ? 3 : u >= 0xE0 ? 2 : u >= 0xC0 ? 1 : 0;
      if (nMore)
        u &= 0x3F >> nMore;
      for (; nMore > 0 && i < n; nMore--)
        u = (u << 6) | (p[i++] & 0x3F);
      if (u >= 0x10000) {
        u -= 0x10000;
        out.push_back(static_cast<WORD>(0xD800 | (u >> 10)));
        out.push_back(static_cast<WORD>(0xDC00 | (u & 0x3FF)));
      } else {
        out.push_back(static_cast<WORD>(u));
      }
    }
  } else {
    for (size_t i = 0; i < n; i++)
      out.push_back(From1252(p[i]));
  }
  if (cchWideChar == 0 || lpWideCharStr == NULL)
    return static_cast<int>(out.size());
  if (out.size() > static_cast<size_t>(cchWideChar)) {
    t_dwLastError = ERROR_INSUFFICIENT_BUFFER;
    return 0;
  }
  memcpy(lpWideCharStr, out.data(), out.size() * sizeof(WORD));
  return static_cast<int>(out.size());
}

int __stdcall WideCharToMultiByte(UINT CodePage, DWORD, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr,
                                  int cbMultiByte, LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar)
{
  size_t n = 0;
  if (cchWideChar < 0) {
    while (lpWideCharStr[n])
      n++;
    n++;
  } else {
    n = static_cast<size_t>(cchWideChar);
  }
  std::string out;
  if (lpUsedDefaultChar)
    *lpUsedDefaultChar = FALSE;
  for (size_t i = 0; i < n; i++) {
    unsigned u = lpWideCharStr[i];
    if (u >= 0xD800 && u < 0xDC00 && i + 1 < n) {
      u = 0x10000 + ((u - 0xD800) << 10) + (lpWideCharStr[i + 1] - 0xDC00);
      i++;
    }
    if (CodePage == CP_UTF8) {
      if (u < 0x80) {
        out += static_cast<char>(u);
      } else if (u < 0x800) {
        out += static_cast<char>(0xC0 | (u >> 6));
        out += static_cast<char>(0x80 | (u & 0x3F));
      } else if (u < 0x10000) {
        out += static_cast<char>(0xE0 | (u >> 12));
        out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (u & 0x3F));
      } else {
        out += static_cast<char>(0xF0 | (u >> 18));
        out += static_cast<char>(0x80 | ((u >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (u & 0x3F));
      }
    } else {
      const int c = To1252(u);
      if (c < 0) {
        out += lpDefaultChar ? *lpDefaultChar : '?';
        if (lpUsedDefaultChar)
          *lpUsedDefaultChar = TRUE;
      } else {
        out += static_cast<char>(c);
      }
    }
  }
  if (cbMultiByte == 0 || lpMultiByteStr == NULL)
    return static_cast<int>(out.size());
  if (out.size() > static_cast<size_t>(cbMultiByte)) {
    t_dwLastError = ERROR_INSUFFICIENT_BUFFER;
    return 0;
  }
  memcpy(lpMultiByteStr, out.data(), out.size());
  return static_cast<int>(out.size());
}

// ---------------------------------------------------------------------------
// rectangles
// ---------------------------------------------------------------------------
BOOL __stdcall SetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom)
{
  if (lprc == NULL)
    return FALSE;
  lprc->left = xLeft;
  lprc->top = yTop;
  lprc->right = xRight;
  lprc->bottom = yBottom;
  return TRUE;
}

BOOL __stdcall SetRectEmpty(LPRECT lprc) { return SetRect(lprc, 0, 0, 0, 0); }

BOOL __stdcall PtInRect(const RECT *lprc, POINT pt)
{
  return lprc && pt.x >= lprc->left && pt.x < lprc->right && pt.y >= lprc->top && pt.y < lprc->bottom;
}

BOOL __stdcall OffsetRect(LPRECT lprc, int dx, int dy)
{
  if (lprc == NULL)
    return FALSE;
  lprc->left += dx;
  lprc->right += dx;
  lprc->top += dy;
  lprc->bottom += dy;
  return TRUE;
}

BOOL __stdcall IsRectEmpty(const RECT *lprc) { return lprc == NULL || lprc->right <= lprc->left || lprc->bottom <= lprc->top; }

BOOL __stdcall IntersectRect(LPRECT lprcDst, const RECT *a, const RECT *b)
{
  RECT r;
  r.left = a->left > b->left ? a->left : b->left;
  r.top = a->top > b->top ? a->top : b->top;
  r.right = a->right < b->right ? a->right : b->right;
  r.bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
  if (IsRectEmpty(&r)) {
    SetRectEmpty(lprcDst);
    return FALSE;
  }
  *lprcDst = r;
  return TRUE;
}

// ---------------------------------------------------------------------------
// windows: the game's, which is SDL's (port_winmsg.cpp); no other program's
// ---------------------------------------------------------------------------
static HWND GameWindow(void) { return static_cast<HWND>(WinMsgWindowHandle()); }

static SDL_Window *SdlOf(HWND hWnd)
{
  return hWnd != NULL && hWnd == GameWindow() ? WinMsgSdlWindow() : NULL;
}

int __stdcall MessageBoxA(HWND, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
{
  fprintf(stderr, "%s: %s\n", lpCaption ? lpCaption : "Hyperion", lpText ? lpText : "");
  SDL_MessageBoxButtonData aButtons[3];
  int nButtons = 0;
  auto add = [&](int id, const char *psz, bool bReturn, bool bEscape) {
    aButtons[nButtons].flags = (bReturn ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0) |
                               (bEscape ? SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0);
    aButtons[nButtons].buttonID = id;
    aButtons[nButtons].text = psz;
    nButtons++;
  };
  switch (uType & 0x0F) {
  case MB_OKCANCEL: add(IDOK, "OK", true, false); add(IDCANCEL, "Cancel", false, true); break;
  case MB_ABORTRETRYIGNORE: add(IDABORT, "Abort", false, true); add(IDRETRY, "Retry", true, false); add(IDIGNORE, "Ignore", false, false); break;
  case MB_YESNOCANCEL: add(IDYES, "Yes", true, false); add(IDNO, "No", false, false); add(IDCANCEL, "Cancel", false, true); break;
  case MB_YESNO: add(IDYES, "Yes", true, false); add(IDNO, "No", false, true); break;
  case MB_RETRYCANCEL: add(IDRETRY, "Retry", true, false); add(IDCANCEL, "Cancel", false, true); break;
  default: add(IDOK, "OK", true, true); break;
  }
  SDL_MessageBoxData data = {};
  const UINT uIcon = uType & 0xF0;
  data.flags = uIcon == MB_ICONERROR ? SDL_MESSAGEBOX_ERROR : uIcon == MB_ICONWARNING ? SDL_MESSAGEBOX_WARNING
                                                                                      : SDL_MESSAGEBOX_INFORMATION;
  data.window = WinMsgSdlWindow();
  data.title = lpCaption ? lpCaption : "Hyperion";
  data.message = lpText ? lpText : "";
  data.numbuttons = nButtons;
  data.buttons = aButtons;
  int nChosen = aButtons[0].buttonID;
  if (!SDL_ShowMessageBox(&data, &nChosen))
    nChosen = aButtons[0].buttonID;
  return nChosen;
}

BOOL __stdcall PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
  if (hWnd != NULL && hWnd != GameWindow())
    return FALSE;
  return WinMsgPost(hWnd, Msg, wParam, lParam);
}

LRESULT __stdcall SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
  if (hWnd == NULL || hWnd != GameWindow())
    return 0;
  return WinMsgSend(hWnd, Msg, wParam, lParam);
}

HWND __stdcall FindWindowA(LPCSTR, LPCSTR) { return NULL; }
HWND __stdcall FindWindowExA(HWND, HWND, LPCSTR, LPCSTR) { return NULL; }

BOOL __stdcall EnumWindows(WNDENUMPROC lpEnumFunc, LPARAM lParam)
{
  if (GameWindow() != NULL)
    lpEnumFunc(GameWindow(), lParam);
  return TRUE;
}

HWND __stdcall GetWindow(HWND, UINT) { return NULL; }
HWND __stdcall GetTopWindow(HWND hWnd) { return hWnd == NULL ? GameWindow() : NULL; }
HWND __stdcall GetParent(HWND) { return NULL; }

HWND __stdcall GetDesktopWindow(void)
{
  static int s_desktop;
  return reinterpret_cast<HWND>(&s_desktop);
}

HWND __stdcall GetForegroundWindow(void)
{
  SDL_Window *w = WinMsgSdlWindow();
  return w != NULL && SDL_GetKeyboardFocus() == w ? GameWindow() : NULL;
}

HWND __stdcall GetActiveWindow(void) { return GameWindow(); }
HWND __stdcall GetLastActivePopup(HWND hWnd) { return hWnd; }

BOOL __stdcall SetForegroundWindow(HWND hWnd)
{
  SDL_Window *w = SdlOf(hWnd);
  return w != NULL && SDL_RaiseWindow(w);
}

HWND __stdcall SetFocus(HWND hWnd)
{
  const HWND hOld = GetForegroundWindow();
  SetForegroundWindow(hWnd);
  return hOld;
}

BOOL __stdcall ShowWindow(HWND hWnd, int nCmdShow)
{
  SDL_Window *w = SdlOf(hWnd);
  if (w == NULL)
    return FALSE;
  const bool bWasVisible = (SDL_GetWindowFlags(w) & SDL_WINDOW_HIDDEN) == 0;
  switch (nCmdShow) {
  case SW_HIDE: SDL_HideWindow(w); break;
  case SW_MINIMIZE:
  case SW_SHOWMINIMIZED: SDL_MinimizeWindow(w); break;
  case SW_SHOWMAXIMIZED: SDL_ShowWindow(w); SDL_MaximizeWindow(w); break;
  case SW_RESTORE: SDL_RestoreWindow(w); SDL_ShowWindow(w); break;
  default: SDL_ShowWindow(w); break;
  }
  return bWasVisible;
}

BOOL __stdcall IsIconic(HWND hWnd)
{
  SDL_Window *w = SdlOf(hWnd);
  return w != NULL && (SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED) != 0;
}

BOOL __stdcall IsWindow(HWND hWnd) { return SdlOf(hWnd) != NULL; }

BOOL __stdcall IsWindowVisible(HWND hWnd)
{
  SDL_Window *w = SdlOf(hWnd);
  return w != NULL && (SDL_GetWindowFlags(w) & SDL_WINDOW_HIDDEN) == 0;
}

DWORD __stdcall GetWindowThreadProcessId(HWND, LPDWORD lpdwProcessId)
{
  if (lpdwProcessId)
    *lpdwProcessId = GetCurrentProcessId();
  return 1;
}

int __stdcall GetClassNameA(HWND hWnd, LPSTR lpClassName, int nMaxCount)
{
  if (SdlOf(hWnd) == NULL || nMaxCount <= 0)
    return 0;
  return snprintf(lpClassName, static_cast<size_t>(nMaxCount), "%s", "SWarClass");
}

// The pixels to a point of the game's window: 2 for a Mac's on a Retina screen
// (port_winmsg.cpp), else 1. SDL's window and mouse positions are points; the game's screen and
// client positions are pixels, as on Windows, so its screen is SDL's desktop scaled by this.
static float Density(SDL_Window *w)
{
  const float fDensity = w != NULL ? SDL_GetWindowPixelDensity(w) : 1.0f;
  return fDensity > 0.0f ? fDensity : 1.0f;
}

static int Pixels(float fPoints, float fDensity) { return static_cast<int>(SDL_lroundf(fPoints * fDensity)); }
static int Points(int nPixels, float fDensity) { return static_cast<int>(SDL_lroundf(nPixels / fDensity)); }

BOOL __stdcall GetClientRect(HWND hWnd, LPRECT lpRect)
{
  SDL_Window *w = SdlOf(hWnd);
  int cx = 0, cy = 0;
  if (w == NULL || !SDL_GetWindowSizeInPixels(w, &cx, &cy))
    return FALSE;
  return SetRect(lpRect, 0, 0, cx, cy);
}

BOOL __stdcall GetWindowRect(HWND hWnd, LPRECT lpRect)
{
  SDL_Window *w = SdlOf(hWnd);
  int x = 0, y = 0, cx = 0, cy = 0;
  if (w == NULL)
    return FALSE;
  const float fDensity = Density(w);
  SDL_GetWindowPosition(w, &x, &y);
  SDL_GetWindowSizeInPixels(w, &cx, &cy);
  return SetRect(lpRect, Pixels(x, fDensity), Pixels(y, fDensity), Pixels(x, fDensity) + cx, Pixels(y, fDensity) + cy);
}

BOOL __stdcall SetWindowPos(HWND hWnd, HWND, int X, int Y, int cx, int cy, UINT uFlags)
{
  SDL_Window *w = SdlOf(hWnd);
  if (w == NULL)
    return FALSE;
  const float fDensity = Density(w);
  if ((uFlags & SWP_NOSIZE) == 0)
    SDL_SetWindowSize(w, Points(cx, fDensity), Points(cy, fDensity));
  if ((uFlags & SWP_NOMOVE) == 0)
    SDL_SetWindowPosition(w, Points(X, fDensity), Points(Y, fDensity));
  if (uFlags & SWP_SHOWWINDOW)
    SDL_ShowWindow(w);
  return TRUE;
}

BOOL __stdcall AdjustWindowRect(LPRECT, DWORD, BOOL) { return TRUE; }

static std::map<int, LONG_PTR> g_windowLongs;

LONG_PTR __stdcall GetWindowLongPtrW(HWND, int nIndex)
{
  auto it = g_windowLongs.find(nIndex);
  return it == g_windowLongs.end() ? 0 : it->second;
}

LONG_PTR __stdcall SetWindowLongPtrW(HWND, int nIndex, LONG_PTR dwNewLong)
{
  const LONG_PTR old = GetWindowLongPtrW(NULL, nIndex);
  g_windowLongs[nIndex] = dwNewLong;
  return old;
}

BOOL __stdcall ClientToScreen(HWND hWnd, LPPOINT lpPoint)
{
  SDL_Window *w = SdlOf(hWnd);
  int x = 0, y = 0;
  if (w == NULL)
    return FALSE;
  const float fDensity = Density(w);
  SDL_GetWindowPosition(w, &x, &y);
  lpPoint->x += Pixels(x, fDensity);
  lpPoint->y += Pixels(y, fDensity);
  return TRUE;
}

BOOL __stdcall ScreenToClient(HWND hWnd, LPPOINT lpPoint)
{
  SDL_Window *w = SdlOf(hWnd);
  int x = 0, y = 0;
  if (w == NULL)
    return FALSE;
  const float fDensity = Density(w);
  SDL_GetWindowPosition(w, &x, &y);
  lpPoint->x -= Pixels(x, fDensity);
  lpPoint->y -= Pixels(y, fDensity);
  return TRUE;
}

BOOL __stdcall UpdateWindow(HWND) { return TRUE; }
BOOL __stdcall InvalidateRect(HWND, const RECT *, BOOL) { return TRUE; }

int __stdcall GetSystemMetrics(int nIndex)
{
  switch (nIndex) {
  case SM_CXSCREEN:
  case SM_CYSCREEN: {
    SDL_Rect r = {0, 0, 1920, 1080};
    const SDL_DisplayID id = SDL_WasInit(SDL_INIT_VIDEO) ? SDL_GetPrimaryDisplay() : 0;
    if (id != 0)
      SDL_GetDisplayBounds(id, &r);
    const float fDensity = id != 0 ? Density(WinMsgSdlWindow()) : 1.0f;
    return Pixels(static_cast<float>(nIndex == SM_CXSCREEN ? r.w : r.h), fDensity);
  }
  case SM_CXICON:
  case SM_CYICON: return 32;
  case SM_CXSMICON:
  case SM_CYSMICON: return 16;
  default: return 0;
  }
}

// the cursor and the keyboard
HCURSOR __stdcall LoadCursorA(HINSTANCE, LPCSTR)
{
  static int s_arrow;
  return reinterpret_cast<HCURSOR>(&s_arrow);
}

HCURSOR __stdcall SetCursor(HCURSOR) { return NULL; }

int __stdcall ShowCursor(BOOL bShow)
{
  static int s_nCount;
  return bShow ? ++s_nCount : --s_nCount;
}

BOOL __stdcall GetCursorPos(LPPOINT lpPoint)
{
  float x = 0, y = 0;
  const float fDensity = Density(WinMsgSdlWindow());
#if defined(__EMSCRIPTEN__)
  // A browser's global position is the page's, in CSS pixels from the page's corner, where the
  // page has placed and scaled the canvas (web/play), so the game took the pointer for off its
  // picture and held it; the window's own, which SDL scales from the canvas's events, is placed
  // on the screen by the window's position.
  int nWinX = 0, nWinY = 0;
  if (SDL_Window *w = WinMsgSdlWindow())
    SDL_GetWindowPosition(w, &nWinX, &nWinY);
  SDL_GetMouseState(&x, &y);
  WinMsgWebHeld(&x, &y);
  x += static_cast<float>(nWinX);
  y += static_cast<float>(nWinY);
#else
  SDL_GetGlobalMouseState(&x, &y);
#endif
  lpPoint->x = static_cast<LONG>(x * fDensity);
  lpPoint->y = static_cast<LONG>(y * fDensity);
  return TRUE;
}

BOOL __stdcall SetCursorPos(int X, int Y)
{
  const float fDensity = Density(WinMsgSdlWindow());
  return SDL_WarpMouseGlobal(X / fDensity, Y / fDensity);
}

BOOL __stdcall ClipCursor(const RECT *lpRect)
{
  SDL_Window *w = WinMsgSdlWindow();
  if (w == NULL)
    return FALSE;
  if (lpRect == NULL)
    return SDL_SetWindowMouseRect(w, NULL);
  int x = 0, y = 0;
  const float fDensity = Density(w);
  SDL_GetWindowPosition(w, &x, &y);
  const int nLeft = Points(lpRect->left, fDensity) - x, nTop = Points(lpRect->top, fDensity) - y;
  SDL_Rect r = {nLeft, nTop, Points(lpRect->right, fDensity) - x - nLeft, Points(lpRect->bottom, fDensity) - y - nTop};
  return SDL_SetWindowMouseRect(w, &r);
}

SHORT __stdcall GetAsyncKeyState(int vKey) { return WinMsgAsyncKeyState(vKey); }
SHORT __stdcall GetKeyState(int nVirtKey) { return WinMsgKeyState(nVirtKey); }
HANDLE __stdcall LoadImageA(HINSTANCE, LPCSTR, UINT, int, int, UINT) { return NULL; }
HICON __stdcall LoadIconA(HINSTANCE, LPCSTR) { return NULL; }

// Dialogs: 1.17's few (a missing disc, a fatal error) run their procedure as Windows
// would, and what it sets in the dialog's text is shown in a message box with OK and
// Cancel, whose answer goes to the procedure as the button's WM_COMMAND.
static int g_dialog;
static std::vector<std::string> g_dialogTexts;
static bool g_bDialogEnded;
static INT_PTR g_nDialogResult;

INT_PTR __stdcall DialogBoxParamA(HINSTANCE, LPCSTR lpTemplateName, HWND, DLGPROC lpDialogFunc, LPARAM dwInitParam)
{
  HWND hDlg = reinterpret_cast<HWND>(&g_dialog);
  g_dialogTexts.clear();
  g_bDialogEnded = false;
  g_nDialogResult = 0;
  lpDialogFunc(hDlg, WM_INITDIALOG, 0, dwInitParam);
  if (g_bDialogEnded)
    return g_nDialogResult;
  std::string text;
  for (const std::string &s : g_dialogTexts)
    text += (text.empty() ? "" : "\n\n") + s;
  if (text.empty()) {
    // which of 1.17's dialogs (gds_appfat.cpp's templates: 0x6B a CD, 0x6C the processor,
    // 0x6D memory, 0x6F video memory), for the log
    fprintf(stderr, "Hyperion: 1.17's dialog %u, which has no text\n",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(lpTemplateName)));
    text = "StarCraft needs your attention.";
  }
  const int nAnswer = MessageBoxA(NULL, text.c_str(), "Hyperion", MB_OKCANCEL | MB_ICONWARNING);
  lpDialogFunc(hDlg, WM_COMMAND, static_cast<WPARAM>(nAnswer), 0);
  return g_bDialogEnded ? g_nDialogResult : nAnswer;
}

BOOL __stdcall EndDialog(HWND, INT_PTR nResult)
{
  g_bDialogEnded = true;
  g_nDialogResult = nResult;
  return TRUE;
}

BOOL __stdcall SetDlgItemTextA(HWND, int, LPCSTR lpString)
{
  if (lpString && *lpString)
    g_dialogTexts.push_back(lpString);
  return TRUE;
}

// GDI: no device off Windows. The game declares the drawing calls it makes itself, with
// its own types (gds_font.cpp, gluBNProfile.h); they are C functions, so these serve.
extern "C" {
HDC __stdcall GetDC(HWND) { return NULL; }
int __stdcall ReleaseDC(HWND, HDC) { return 0; }
int __stdcall DrawTextA(void *, LPCSTR, int, LPRECT, UINT) { return 0; }
int __stdcall GetDeviceCaps(void *, int) { return 0; }
void *__stdcall CreateFontIndirectA(const void *) { return NULL; }
void *__stdcall SelectObject(void *, void *) { return NULL; }
void *__stdcall CreateCompatibleDC(void *) { return NULL; }
BOOL __stdcall DeleteDC(void *) { return TRUE; }
int __stdcall SetBkMode(void *, int) { return 0; }
DWORD __stdcall SetTextColor(void *, DWORD) { return 0; }
DWORD __stdcall SetBkColor(void *, DWORD) { return 0; }
void *__stdcall GetStockObject(int) { return NULL; }
void *__stdcall CreateDIBSection(void *, const void *, unsigned int, void **ppvBits, void *, DWORD)
{
  if (ppvBits)
    *ppvBits = NULL;
  return NULL;
}
BOOL __stdcall Rectangle(void *, int, int, int, int) { return FALSE; }
int __stdcall GetObjectA(void *, int, void *) { return 0; }
LONG __stdcall GetBitmapBits(void *, LONG, void *) { return 0; }
BOOL __stdcall DeleteObject(void *) { return TRUE; }
}

// The IME (event_Win.cpp, which declares these itself): no input context off Windows.
extern "C" {
void *__stdcall ImmGetContext(void *) { return NULL; }
BOOL __stdcall ImmReleaseContext(void *, void *) { return TRUE; }
LONG __stdcall ImmGetCompositionStringA(void *, DWORD, void *, DWORD) { return 0; }
BOOL __stdcall ImmSetCompositionStringA(void *, DWORD, void *, DWORD, void *, DWORD) { return FALSE; }
}

// ---------------------------------------------------------------------------
// the registry, which has no keys, and security, which has nothing to set
// ---------------------------------------------------------------------------
LONG __stdcall RegOpenKeyExA(HKEY, LPCSTR, DWORD, REGSAM, PHKEY phkResult)
{
  if (phkResult)
    *phkResult = NULL;
  return ERROR_FILE_NOT_FOUND;
}
LONG __stdcall RegCloseKey(HKEY) { return ERROR_SUCCESS; }
LONG __stdcall RegQueryValueExA(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD) { return ERROR_FILE_NOT_FOUND; }
LONG __stdcall RegDeleteValueA(HKEY, LPCSTR) { return ERROR_FILE_NOT_FOUND; }
LONG __stdcall RegSetValueExA(HKEY, LPCSTR, DWORD, DWORD, const BYTE *, DWORD) { return ERROR_ACCESS_DENIED; }
LONG __stdcall RegCreateKeyExA(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY phkResult,
                               LPDWORD)
{
  if (phkResult)
    *phkResult = NULL;
  return ERROR_ACCESS_DENIED;
}

BOOL __stdcall OpenProcessToken(HANDLE, DWORD, PHANDLE TokenHandle)
{
  if (TokenHandle)
    *TokenHandle = NULL;
  t_dwLastError = ERROR_NOT_SUPPORTED;
  return FALSE;
}
BOOL __stdcall GetTokenInformation(HANDLE, TOKEN_INFORMATION_CLASS, LPVOID, DWORD, PDWORD) { return FALSE; }
BOOL __stdcall AllocateAndInitializeSid(PSID_IDENTIFIER_AUTHORITY, BYTE, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                        DWORD, DWORD, PSID *pSid)
{
  if (pSid)
    *pSid = NULL;
  return FALSE;
}
PVOID __stdcall FreeSid(PSID) { return NULL; }
BOOL __stdcall InitializeAcl(PACL, DWORD, DWORD) { return FALSE; }
BOOL __stdcall AddAccessAllowedAce(PACL, DWORD, DWORD, PSID) { return FALSE; }
BOOL __stdcall AddAccessDeniedAce(PACL, DWORD, DWORD, PSID) { return FALSE; }
DWORD __stdcall GetLengthSid(PSID) { return 0; }

// ---------------------------------------------------------------------------
// the C runtime's Microsoft names
// ---------------------------------------------------------------------------
__time64_t __cdecl _time64(__time64_t *pt)
{
  const __time64_t t = static_cast<__time64_t>(time(NULL));
  if (pt)
    *pt = t;
  return t;
}

struct tm *__cdecl _gmtime64(const __time64_t *pt)
{
  static thread_local struct tm s_tm;
  const time_t t = static_cast<time_t>(*pt);
  return gmtime_r(&t, &s_tm);
}

char *__cdecl _ctime64(const __time64_t *pt)
{
  static thread_local char s_sz[32];
  const time_t t = static_cast<time_t>(*pt);
  return ctime_r(&t, s_sz);
}

errno_t __cdecl _localtime64_s(struct tm *ptm, const __time64_t *pt)
{
  const time_t t = static_cast<time_t>(*pt);
  return localtime_r(&t, ptm) ? 0 : EINVAL;
}

// ASCII alone, as the runtime's own do in the C locale the game runs in, and whatever
// locale the C library is left in: an account name upper-cased into a signature's hash
// (signature.cpp's sig_sha) has to come out the same on every platform.
char *__cdecl _strupr(char *s)
{
  for (char *p = s; *p; p++)
    if (*p >= 'a' && *p <= 'z')
      *p = static_cast<char>(*p - 'a' + 'A');
  return s;
}

char *__cdecl _strlwr(char *s)
{
  for (char *p = s; *p; p++)
    if (*p >= 'A' && *p <= 'Z')
      *p = static_cast<char>(*p - 'A' + 'a');
  return s;
}

int __cdecl _wcsicmp(const wchar_t *a, const wchar_t *b) { return wcscasecmp(a, b); }

extern "C" int __cdecl _mbsicmp(const char *a, const char *b) { return strcasecmp(a, b); }

// The game's code page has one byte to a character, so these are the C library's; the
// game declares them itself (dlg_edit.cpp, gds_log.cpp, gluBNCreate.cpp).
extern "C" size_t __cdecl _mbslen(const unsigned char *psz) { return strlen(reinterpret_cast<const char *>(psz)); }

extern "C" unsigned char *__cdecl _mbsrchr(const unsigned char *pszStr, unsigned int ch)
{
  return reinterpret_cast<unsigned char *>(const_cast<char *>(strrchr(reinterpret_cast<const char *>(pszStr),
                                                                      static_cast<int>(ch))));
}

// The C runtime's thread start (snd.cpp, gluModem.cpp), on CreateThread.
extern "C" ULONG_PTR __cdecl _beginthreadex(void *pSecurity, unsigned nStack, ULONG(__stdcall *pfnStart)(void *),
                                            void *pvArg, unsigned nFlags, unsigned *pnThreadId)
{
  DWORD dwId = 0;
  HANDLE h = CreateThread(static_cast<LPSECURITY_ATTRIBUTES>(pSecurity), nStack,
                          reinterpret_cast<LPTHREAD_START_ROUTINE>(pfnStart), pvArg, nFlags, &dwId);
  if (pnThreadId)
    *pnThreadId = dwId;
  return reinterpret_cast<ULONG_PTR>(h);
}

errno_t __cdecl fopen_s(FILE **pf, const char *name, const char *mode)
{
  *pf = OcFopen(name, mode);
  return *pf ? 0 : errno;
}

// MSVC's takes a buffer size after each %s and %c, which vsscanf would read as the next
// conversion's pointer. Every format the engine passes reads numbers (statwrite.cpp's
// suppressed %*s takes no size either), so one holding %s or %c has to be written out
// here before it is used.
int __cdecl sscanf_s(const char *buf, const char *fmt, ...)
{
  va_list va;
  va_start(va, fmt);
  const int n = vsscanf(buf, fmt, va);
  va_end(va);
  return n;
}

errno_t __cdecl strcpy_s(char *dst, size_t n, const char *src)
{
  const size_t len = strlen(src);
  if (dst == NULL || n == 0)
    return EINVAL;
  if (len >= n) {
    dst[0] = 0;
    return ERANGE;
  }
  memcpy(dst, src, len + 1);
  return 0;
}

errno_t __cdecl strncpy_s(char *dst, size_t n, const char *src, size_t count)
{
  if (dst == NULL || n == 0)
    return EINVAL;
  size_t len = 0;
  while (len < count && src[len])
    len++;
  if (len >= n) {
    if (count == _TRUNCATE) {
      memcpy(dst, src, n - 1);
      dst[n - 1] = 0;
      return 80;  // STRUNCATE
    }
    dst[0] = 0;
    return ERANGE;
  }
  memcpy(dst, src, len);
  dst[len] = 0;
  return 0;
}

errno_t __cdecl strcat_s(char *dst, size_t n, const char *src)
{
  const size_t have = strnlen(dst, n);
  if (have == n)
    return EINVAL;
  return strcpy_s(dst + have, n - have, src);
}

int __cdecl vsprintf_s(char *buf, size_t n, const char *fmt, va_list va)
{
  const int r = vsnprintf(buf, n, fmt, va);
  return r < 0 || static_cast<size_t>(r) >= n ? -1 : r;
}

int __cdecl sprintf_s(char *buf, size_t n, const char *fmt, ...)
{
  va_list va;
  va_start(va, fmt);
  const int r = vsprintf_s(buf, n, fmt, va);
  va_end(va);
  return r;
}

int __cdecl _vsnprintf_s(char *buf, size_t n, size_t count, const char *fmt, va_list va)
{
  if (buf == NULL || n == 0)
    return -1;
  const size_t cap = count == _TRUNCATE || count + 1 > n ? n : count + 1;
  const int r = vsnprintf(buf, cap, fmt, va);
  return r < 0 || static_cast<size_t>(r) >= cap ? -1 : r;
}

int __cdecl _snprintf_s(char *buf, size_t n, size_t count, const char *fmt, ...)
{
  va_list va;
  va_start(va, fmt);
  const int r = _vsnprintf_s(buf, n, count, fmt, va);
  va_end(va);
  return r;
}

// ---------------------------------------------------------------------------
// BCrypt's SHA-1 (FIPS 180-4)
// ---------------------------------------------------------------------------
namespace {
struct Sha1 {
  uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  uint64_t cb = 0;
  BYTE block[64];
  size_t nBlock = 0;

  static uint32_t Rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

  void Compress(const BYTE *p)
  {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
      w[i] = (static_cast<uint32_t>(p[4 * i]) << 24) | (static_cast<uint32_t>(p[4 * i + 1]) << 16) |
             (static_cast<uint32_t>(p[4 * i + 2]) << 8) | p[4 * i + 3];
    for (int i = 16; i < 80; i++)
      w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999u;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1u;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCu;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6u;
      }
      const uint32_t t = Rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = Rol(b, 30);
      b = a;
      a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }

  void Add(const BYTE *p, size_t n)
  {
    cb += n;
    while (n > 0) {
      const size_t take = 64 - nBlock < n ? 64 - nBlock : n;
      memcpy(block + nBlock, p, take);
      nBlock += take;
      p += take;
      n -= take;
      if (nBlock == 64) {
        Compress(block);
        nBlock = 0;
      }
    }
  }

  void Finish(BYTE out[20])
  {
    const uint64_t bits = cb * 8;
    const BYTE one = 0x80, zero = 0;
    Add(&one, 1);
    while (nBlock != 56)
      Add(&zero, 1);
    BYTE len[8];
    for (int i = 0; i < 8; i++)
      len[i] = static_cast<BYTE>(bits >> (56 - 8 * i));
    Add(len, 8);
    for (int i = 0; i < 20; i++)
      out[i] = static_cast<BYTE>(h[i / 4] >> (24 - 8 * (i % 4)));
  }
};
}  // namespace

NTSTATUS __stdcall BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE *phAlgorithm, const wchar_t *, const wchar_t *, ULONG)
{
  *phAlgorithm = BCRYPT_SHA1_ALG_HANDLE;
  return 0;
}

NTSTATUS __stdcall BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE, ULONG) { return 0; }

NTSTATUS __stdcall BCryptCreateHash(BCRYPT_ALG_HANDLE, BCRYPT_HASH_HANDLE *phHash, PUCHAR, ULONG, PUCHAR, ULONG, ULONG)
{
  *phHash = new Sha1;
  return 0;
}

NTSTATUS __stdcall BCryptHashData(BCRYPT_HASH_HANDLE hHash, PUCHAR pbInput, ULONG cbInput, ULONG)
{
  static_cast<Sha1 *>(hHash)->Add(pbInput, cbInput);
  return 0;
}

NTSTATUS __stdcall BCryptFinishHash(BCRYPT_HASH_HANDLE hHash, PUCHAR pbOutput, ULONG cbOutput, ULONG)
{
  if (cbOutput < 20)
    return static_cast<NTSTATUS>(0xC0000023);  // STATUS_BUFFER_TOO_SMALL
  static_cast<Sha1 *>(hHash)->Finish(pbOutput);
  return 0;
}

NTSTATUS __stdcall BCryptDestroyHash(BCRYPT_HASH_HANDLE hHash)
{
  delete static_cast<Sha1 *>(hHash);
  return 0;
}

NTSTATUS __stdcall BCryptHash(BCRYPT_ALG_HANDLE, PUCHAR, ULONG, PUCHAR pbInput, ULONG cbInput, PUCHAR pbOutput,
                              ULONG cbOutput)
{
  if (cbOutput < 20)
    return static_cast<NTSTATUS>(0xC0000023);
  Sha1 s;
  s.Add(pbInput, cbInput);
  s.Finish(pbOutput);
  return 0;
}

#if defined(__APPLE__)
// App Nap slows a program whose windows are not seen and that plays no sound: its timers wait
// and its threads lose the processor. A game in the background must not slow, a LAN game's
// or a lab worker's (one suite run on the Mac played m1 at 51 frames a second where it plays
// at 7,000, which App Nap would explain). So the run is one of NSProcessInfo's
// activities, begun through the Objective-C runtime: user initiated and latency critical, the
// system still free to sleep when idle (NSActivityUserInitiatedAllowingIdleSystemSleep |
// NSActivityLatencyCritical). The activity lasts while its object does, so it is kept for the
// run. The runtime's calls are declared here with plain pointers: <objc/objc.h> makes BOOL a
// bool, which Win32's is not.
extern "C" void *objc_getClass(const char *pszName);
extern "C" void *sel_registerName(const char *pszName);
extern "C" void objc_msgSend(void);
extern "C" void *objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void *pPool);

void PortPosixKeepRunning(void)
{
  static void *s_activity;
  const unsigned long long kUserInitiated = 0x00FFFFFFULL, kIdleSystemSleepDisabled = 1ULL << 20,
                           kLatencyCritical = 0xFF00000000ULL;
  typedef void *(*Send)(void *, void *);
  typedef void *(*SendText)(void *, void *, const char *);
  typedef void *(*SendActivity)(void *, void *, unsigned long long, void *);
  void *pInfo = objc_getClass("NSProcessInfo");
  void *pString = objc_getClass("NSString");
  if (s_activity != NULL || pInfo == NULL || pString == NULL)
    return;
  void *pPool = objc_autoreleasePoolPush();
  void *pProcess = reinterpret_cast<Send>(objc_msgSend)(pInfo, sel_registerName("processInfo"));
  void *pReason = reinterpret_cast<SendText>(objc_msgSend)(pString, sel_registerName("stringWithUTF8String:"),
                                                           "Hyperion plays a game");
  void *pActivity = reinterpret_cast<SendActivity>(objc_msgSend)(
      pProcess, sel_registerName("beginActivityWithOptions:reason:"),
      (kUserInitiated & ~kIdleSystemSleepDisabled) | kLatencyCritical, pReason);
  if (pActivity != NULL)
    s_activity = reinterpret_cast<Send>(objc_msgSend)(pActivity, sel_registerName("retain"));
  objc_autoreleasePoolPop(pPool);
}
#endif

#endif  // !_WIN32
