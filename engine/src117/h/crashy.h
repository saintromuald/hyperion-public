// crashy.h -- the crash reporter Blizzard links into 1.17: the exception filter,
// the log writers, the report file and the file helpers around it.

#ifndef OC117_CRASHY_H
#define OC117_CRASHY_H

#include "platform.h"
// Not <stdio.h>: gds_file.cpp and main.cpp declare fopen returning void *, which
// cannot sit beside the CRT's, so FILE is declared as <stdio.h> declares it.
// Hyperion: other compilers take the C library's own.
#if defined(_MSC_VER)
typedef struct _iobuf FILE;

extern "C" {
int __cdecl fclose(FILE *pFile);
int __cdecl fflush(FILE *pFile);
size_t __cdecl fwrite(const void *pSrc, size_t nSize, size_t nCount, FILE *pFile);
size_t __cdecl fread(void *pDest, size_t nSize, size_t nCount, FILE *pFile);
}
#else
#include <stdio.h>
#include <wchar.h>
#endif
#include <stdarg.h>
#include <string.h>
#include <time.h>

// The CRT entry points the crash reporter calls, at global scope so they name the
// functions <stdio.h> and <string.h> declare rather than siblings in crashy.
#if defined(_MSC_VER)
extern "C" {
int __cdecl vfprintf(FILE *pFile, const char *pszFormat, va_list args);
int __cdecl _vsnprintf(char *pszBuf, size_t cchBuf, const char *pszFormat, va_list args);
const char *__cdecl strrchr(const char *pszStr, int chFind);
const wchar_t *__cdecl wcsrchr(const wchar_t *pwszStr, wchar_t chFind);
int __cdecl _localtime64_s(struct tm *pTm, const __time64_t *pTime);
}
#endif

namespace crashy {

// A log sink. The writers below take the function and its context as a pair; the
// pointer is /Gr's default __fastcall, the context in ECX and the line in EDX.
typedef void (*LOGPROC)(void *pContext, const char *pszLine);

// One captured stack frame; LogStackTrace clears 128 of them. bHasParams gates
// the four parameter words FormatFunctionInfo prints. The frame, the stack
// pointer and the parameter words are pointer-sized (DWORD_PTR, a DWORD on x86).
struct StackFrameInfo {
  /*0x000*/ void *pAddress;
  /*0x004*/ DWORD_PTR dwFrame;
  /*0x008*/ DWORD_PTR dwStack;
  /*0x00C*/ DWORD dwUnk00C;
  /*0x010*/ BYTE bHasParams;
  /*0x014*/ DWORD_PTR adwParams[4];

  StackFrameInfo();
};

inline StackFrameInfo::StackFrameInfo()
{
  pAddress = nullptr;
  dwFrame = 0;
  dwStack = 0;
  dwUnk00C = 0;
  bHasParams = 0;
  memset(adwParams, 0, sizeof(adwParams));
}

static_assert(offsetof(StackFrameInfo, pAddress)   == 0x000, "GetSymbol call: mov ecx, dword ptr [edi]");
OC_X86_LAYOUT(offsetof(StackFrameInfo, dwFrame)    == 0x004, "LogStackTrace: mov dword ptr [eax-4h], 0h");
OC_X86_LAYOUT(offsetof(StackFrameInfo, dwStack)    == 0x008, "LogStackTrace: mov dword ptr [eax-24h], 0h");
OC_X86_LAYOUT(offsetof(StackFrameInfo, dwUnk00C)   == 0x00C, "LogStackTrace: mov dword ptr [eax-20h], 0h");
OC_X86_LAYOUT(offsetof(StackFrameInfo, bHasParams) == 0x010, "FormatFunctionInfo: cmp byte ptr [esi+10h], 0h");
OC_X86_LAYOUT(offsetof(StackFrameInfo, adwParams)  == 0x014, "FormatFunctionInfo: push dword ptr [esi+14h]");
OC_X86_LAYOUT(sizeof(StackFrameInfo) == 0x24, "LogStackFrames: add edi, 24h");

// What GetSymbol resolves an address into.
struct SymbolInfo {
  /*0x000*/ char szSymbol[0x200];
  /*0x200*/ DWORD dwDisplacement;
  /*0x204*/ char szModule[0x40];
  /*0x244*/ char szFile[0x100];
  /*0x344*/ DWORD dwLine;
};

static_assert(offsetof(SymbolInfo, szSymbol)       == 0x000, "FormatFunctionInfo: cmp byte ptr [edi], 0h / push edi");
static_assert(offsetof(SymbolInfo, dwDisplacement) == 0x200, "GetSymbol: mov dword ptr [edi+200h], 0h");
static_assert(offsetof(SymbolInfo, szModule)       == 0x204, "GetSymbol: strncpy_s(edi+204h, 40h, ...)");
static_assert(offsetof(SymbolInfo, szFile)         == 0x244, "GetSymbol: strncpy_s(edi+244h, 100h, ...)");
static_assert(offsetof(SymbolInfo, dwLine)         == 0x344, "GetSymbol: mov dword ptr [edi+344h], 0h");
static_assert(sizeof(SymbolInfo) == 0x348, "the four members above, no tail padding");

// The report handle. Polymorphic: the ctor at 0x403DA0 stores the vtable
// (0x4FDCA8) at +0, so the first member sits at +4.
struct Report {
  // +0x000 is the vptr; the one virtual function is the destructor.
  /*0x004*/ FILE *pFile;
  /*0x008*/ char szPath[0x400];
  /*0x408*/ char szName[0x400];
  // The attachment count, not flags: add_attachment compares it signed with 16.
  /*0x808*/ int dwFlags;
  /*0x80C*/ char aszAttachments[16][0x400];

  Report();

  // 0x403E10. The vftable at 0x4FDCA8 holds one slot, the scalar deleting
  // destructor at 0x403DD0.
  virtual ~Report();

  bool is_open();
  bool close();

  // 0x4040F0. Variadic, so __cdecl with `this` on the stack. Forwards to
  // vfprintf on pFile and drops the count.
  void printf(const char *pszFormat, ...);

  // 0x403FC0. ECX is `this`, then the file to attach and whether to rename it
  // under szName.
  bool add_attachment(const char *pszPath, bool bRename);

  // 0x403E40. ECX is `this` and the one stack word the leaf file name.
  bool open(const char *pszFileName);

  // 0x405990. open is the one caller, and the /GL link specialized the body to
  // its constant arguments, so 1.17's copy reads no parameter.
  static bool PathAppendBlizzardError(char *pszPath, size_t cchPath, const char *pszMore);

  // 0x6D40F0: the process-wide report directory, filled by open on first use.
  static char errors_dir[0x400];
};

OC_X86_LAYOUT(offsetof(Report, pFile)  == 0x004, "Report::pFile   ctor: mov dword ptr [ecx+4h], 0h");
OC_X86_LAYOUT(offsetof(Report, szPath) == 0x008, "Report::szPath  ctor: mov byte ptr [ecx+8h], 0h");
OC_X86_LAYOUT(offsetof(Report, szName) == 0x408, "Report::szName  ctor: mov byte ptr [ecx+408h], 0h");
OC_X86_LAYOUT(offsetof(Report, dwFlags)== 0x808, "Report::dwFlags ctor: mov dword ptr [ecx+808h], 0h");
OC_X86_LAYOUT(offsetof(Report, aszAttachments) == 0x80C, "add_attachment: lea edx, [edi+80Ch] / shl eax, 0Ah");
OC_X86_LAYOUT(sizeof(Report) == 0x480C, "add_attachment: the slot index is bounded by `cmp dword ptr [edi+808h], 10h`");

// The inspector's launch parameters. The caller (ErrorHandler::ExceptionHandler,
// 0x4CB910) clears all 0x54 bytes and fills at most 16 attachment slots.
struct ReportInfo {
  const char *pszProduct;           /*0x00*/
  const char *pszLocale;            /*0x04*/
  const char *pszFrom;              /*0x08*/
  DWORD dwFlags;                    /*0x0C*/
  const char *rgpszAttachments[16]; /*0x10*/
  DWORD cAttachments;               /*0x50*/
};

static_assert(offsetof(ReportInfo, pszProduct) == 0x00, "ReportInfo::pszProduct: strlcat after \" -product \\\"\"");
OC_X86_LAYOUT(offsetof(ReportInfo, pszLocale) == 0x04, "ReportInfo::pszLocale: cmp dword ptr [edi+4h], 0h");
OC_X86_LAYOUT(offsetof(ReportInfo, pszFrom) == 0x08, "ReportInfo::pszFrom: cmp dword ptr [edi+8h], 0h");
OC_X86_LAYOUT(offsetof(ReportInfo, dwFlags) == 0x0C, "ReportInfo::dwFlags: test byte ptr [edi+0Ch], 1h/2h/4h");
OC_X86_LAYOUT(offsetof(ReportInfo, rgpszAttachments) == 0x10, "ReportInfo::rgpszAttachments: lea esi, [edi+10h]; caller caps the index at 10h");
OC_X86_LAYOUT(offsetof(ReportInfo, cAttachments) == 0x50, "ReportInfo::cAttachments: cmp ebx, dword ptr [edi+50h]");
OC_X86_LAYOUT(sizeof(ReportInfo) == 0x54, "ReportInfo size: the caller's push 54h into memset");

// 0x402CF0. ECX is the report's path and EDX the launch parameters; both callers
// read AL.
bool SubmitReport(const char *pszReportPath, const ReportInfo *pInfo);

// _wfopen is 1.17's statically linked CRT copy at 0x4E8934, which the build's
// own CRT also provides.
extern "C" {
FILE *__cdecl _wfopen(const wchar_t *pszPath, const wchar_t *pszMode);
int __cdecl _snprintf_s(char *pszBuf, size_t cchBuf, size_t cchMax,
                        const char *pszFormat, ...);
}

// 0x403250. ECX is the sink and EDX its context.
void LogModules(LOGPROC pfnLog, void *pContext);

// 0x403610. ECX is the thread (compared with GetCurrentThreadId), EDX the sink
// and [ebp+8h] its context; both callers pass the report there.
void LogThreadRegisters(DWORD dwThreadId, LOGPROC pfnLog, void *pContext);

// 0x4033C0. ECX is the thread id; EDX is passed on to StackTrace unread.
void LogStackTrace(DWORD dwThreadId, void *pvUnused, LOGPROC pfnLog, void *pContext,
                   bool bWithSymbols, bool bWithHeader, int nSkip);

// 0x403450. The two trailing flags are read one byte wide: [ebp+14h] keeps the
// resolved symbol name and [ebp+18h] writes the thread header line.
void LogStackFrames(DWORD dwThreadId, const StackFrameInfo *pFrames, int nFrames,
                    LOGPROC pfnLog, void *pContext, bool bWithSymbols, bool bWithHeader);

// 0x402BB0, in BlizzardError.cpp. ECX the thread and EDX the used byte count,
// then the stack base and the captured esp; `ret 8h`.
bool GetThreadStackInfo(DWORD dwThreadId, DWORD_PTR *pdwUsed, DWORD_PTR *pdwBase,
                        DWORD_PTR *pdwEsp);

// 0x403D00. [ebp+8h] is the thread to skip: the body compares it with
// te.th32ThreadID and passes on a match.
void LogAllThreads(LOGPROC pfnLog, void *pContext, DWORD dwSkipThreadId);

// 0x405790. ECX is the address and EDX the record to fill.
bool GetSymbol(const void *pAddress, SymbolInfo *pSymbol);

// 0x4055E0. ECX and EDX arrive as LogStackTrace's own two register parameters,
// untouched. EDX is a CONTEXT to copy, or null to capture the thread's own;
// [ebp+0Ch] is never read.
int StackTrace(DWORD dwThreadId, void *pvContext, StackFrameInfo *pFrames,
               DWORD dwUnused, int nSkip);

// What CrashyExceptionFilter hands the installed handler. ErrorHandler::
// ExceptionHandler (0x4CB910) passes pFrames and nFrames to LogStackFrames and
// nSkip to LogStackTrace.
struct ExceptionInfo {
  /*0x00*/ const char *pszName;
  /*0x04*/ char *pszMessage;
  /*0x08*/ const StackFrameInfo *pFrames;
  /*0x0C*/ int nFrames;
  /*0x10*/ int nSkip;
  /*0x14*/ DWORD dwThreadId;
  /*0x18*/ EXCEPTION_POINTERS *pExceptionPointers;
};

static_assert(offsetof(ExceptionInfo, pszName) == 0x00, "ExceptionInfo::pszName: mov dword ptr [ebp-1Ch], eax");
OC_X86_LAYOUT(offsetof(ExceptionInfo, pszMessage) == 0x04, "ExceptionInfo::pszMessage: mov dword ptr [ebp-18h], eax");
OC_X86_LAYOUT(offsetof(ExceptionInfo, pFrames) == 0x08, "ExceptionInfo::pFrames: mov dword ptr [ebp-14h], 0h");
OC_X86_LAYOUT(offsetof(ExceptionInfo, nFrames) == 0x0C, "ExceptionInfo::nFrames: mov dword ptr [ebp-10h], 0h");
OC_X86_LAYOUT(offsetof(ExceptionInfo, nSkip) == 0x10, "ExceptionInfo::nSkip: mov dword ptr [ebp-0Ch], 0h");
OC_X86_LAYOUT(offsetof(ExceptionInfo, dwThreadId) == 0x14, "ExceptionInfo::dwThreadId: mov dword ptr [ebp-8h], eax after GetCurrentThreadId");
OC_X86_LAYOUT(offsetof(ExceptionInfo, pExceptionPointers) == 0x18, "ExceptionInfo::pExceptionPointers: mov dword ptr [ebp-4h], esi");
OC_X86_LAYOUT(sizeof(ExceptionInfo) == 0x1C, "ExceptionInfo size: lea ecx, [ebp-1Ch] over a 31Ch frame whose other two locals are 100h and 200h");

// ECX takes the info block and EDX the registered argument, which is the
// /Gr default for a two-argument free function.
typedef int (*PFN_CRASHY_HANDLER)(ExceptionInfo *pInfo, void *pvParam);

// The crash reporter's own entry points.

// 0x402AF0. Ends the process with the caller's exit code.
void TerminateProcess(UINT uExitCode);

// 0x402B00. Walks the TEB for the stack extent containing dwAddr.
bool GetStackAddresses(DWORD_PTR dwAddr, DWORD_PTR *pdwStackTop, DWORD_PTR *pdwStackBase);

// 0x402C80. A tail jump through the kernel32 import.
DWORD GetCurrentThreadId();

// 0x402C90. A tail jump to the real initialiser.
void Initialize();

// 0x402AB0. Installs the handler the exception filter calls.
void SetExceptionHandler(PFN_CRASHY_HANDLER pfnHandler, void *pvParam, DWORD dwUnused);

namespace internal {
FILE *FileOpen(const char *pszPath);

// 0x404B10. Starts the inspector with CreateProcessW.
bool LaunchApplication(const char *pszApp, const char *pszCmdLine);

// 0x405900. Copies the last path component of pszPath into the caller's buffer;
// declared again below.
bool FileBasename(const char *pszPath, char *pszOut, DWORD cchOut);

// 0x404AE0, __cdecl varargs: the buffer, its size, then the format. The
// terminator goes in at size-1 after the call, so the size counts it.
int __cdecl snprintf(char *pszBuf, size_t cchBuf, const char *pszFormat, ...);

// 0x404A50, in BlizzardError.cpp. ECX the destination, EDX the source, the size
// on the stack. It returns the length it wanted, so callers compare that against
// the size.
size_t strlcat(char *pszDst, const char *pszSrc, size_t cbDst);

// 0x404C60. ECX the source path, EDX the destination, both UTF-8 and both
// widened into 0x104-wide buffers before CopyFileW.
bool FileCopy(const char *pszSource, const char *pszDest);

// 0x404CE0. ECX a wide path, cut at its last separator so the parents can be
// created first, then put back.
bool MakeDirRecursive(wchar_t *pwszPath);

// 0x405900. ECX the path, EDX the output buffer, its size on the stack. False
// when the basename did not fit.
bool FileBasename(const char *pszPath, char *pszOut, DWORD cchOut);
}

}  // namespace crashy

#endif  // OC117_CRASHY_H
