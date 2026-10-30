// NDebugHelp.h -- declarations for the dbghelp.dll glue in
// src117/lang/DebugHelp.cpp: loading the library, loading symbols, resolving an
// address back to a module, a symbol or a source line, and the minidump thread.
// NDebugHelp's members are declared here; DebugHelp.h holds the free functions.

#ifndef OC117_NDEBUGHELP_H
#define OC117_NDEBUGHELP_H

#include "platform.h"

// The Win32 entries DebugHelp.cpp calls through the import table take their
// dllimport declarations from <windows.h>; a second extern "C" declaration that
// differs in any way does not compile.
#include "Storm.h"

// <windows.h> defines LoadLibrary as LoadLibraryA, which would rename the member
// NDebugHelp::LoadLibrary. 1.17 calls LoadLibraryW directly, so nothing here
// wants the macro.
#undef LoadLibrary

// The CRT entries DebugHelp.cpp calls.
extern "C" {
// strrchr is declared in crashy.h, returning const char *.
int __cdecl strncpy_s(char *pszDest, size_t cchDest, const char *pszSrc, size_t nCount);
                                                                        // 0x4E30DA
}

namespace NDebugHelp {

// The request WriteMiniDump parks in s_miniDumpParam for the minidump thread.
struct MiniDumpParam {
  /*0x00*/ void *hFile;
  /*0x04*/ void *pExceptionPointers;
  /*0x08*/ DWORD dwThreadId;
  /*0x0C*/ const char *pszComment;
  /*0x10*/ bool bResult;
};

static_assert(offsetof(MiniDumpParam, hFile) == 0x00,
              "WriteMiniDump: mov dword ptr [ebp-18h], edi");
OC_X86_LAYOUT(offsetof(MiniDumpParam, pExceptionPointers) == 0x04,
              "WriteMiniDump: mov dword ptr [ebp-14h], eax  (arg at [ebp+8])");
OC_X86_LAYOUT(offsetof(MiniDumpParam, dwThreadId) == 0x08,
              "WriteMiniDump: mov dword ptr [ebp-10h], eax  (GetCurrentThreadId)");
OC_X86_LAYOUT(offsetof(MiniDumpParam, pszComment) == 0x0C,
              "MiniDumpThreadWrite: mov edx, dword ptr [esi+0Ch], then strlen");
OC_X86_LAYOUT(offsetof(MiniDumpParam, bResult) == 0x10,
              "MiniDumpThreadWrite: mov byte ptr [esi+10h], al");
OC_X86_LAYOUT(sizeof(MiniDumpParam) == 0x14,
              "the frame slot runs [ebp-18h]..[ebp-8h]; the trailing pad is inferred");

// The three dbghelp entries DebugHelp.cpp calls through; every dbghelp export
// is __stdcall.
typedef DWORD(__stdcall *PFN_SymGetOptions)(void);
typedef DWORD(__stdcall *PFN_SymSetOptions)(DWORD dwOptions);
typedef int(__stdcall *PFN_SymInitialize)(void *hProcess, const char *pszSearchPath,
                                          int bInvadeProcess);

// IMAGEHLP_MODULE64 as GetAddressModule and GetAddressSymbol fill it; only the
// fields they read are named.
struct ImageHlpModule64 {
  /*0x000*/ DWORD SizeOfStruct;
  /*0x004*/ DWORD dwPad;
  /*0x008*/ DWORD dwBaseOfImage;
  /*0x00C*/ DWORD dwBaseOfImageHigh;
  /*0x010*/ DWORD dwImageSize;
  /*0x014*/ DWORD dwTimeDateStamp;
  /*0x018*/ DWORD dwCheckSum;
  /*0x01C*/ DWORD dwNumSyms;
  /*0x020*/ DWORD dwSymType;
  /*0x024*/ char szModuleName[32];
  /*0x044*/ char szImageName[256];
  /*0x144*/ char szLoadedImageName[256];
  /*0x244*/ BYTE abRest[0x690 - 0x244];
};

static_assert(offsetof(ImageHlpModule64, dwBaseOfImage) == 0x008, "GetAddressModule: mov eax, dword ptr [ebp-698h]");
static_assert(offsetof(ImageHlpModule64, dwSymType) == 0x020, "GetAddressSymbol: the SymNone/SymExport/SymDeferred test");
static_assert(offsetof(ImageHlpModule64, szImageName) == 0x044, "GetAddressModule: lea eax, [ebp-65Ch] into strncpy_s");
static_assert(offsetof(ImageHlpModule64, szLoadedImageName) == 0x144, "GetAddressModule: lea eax, [ebp-55Ch] into GetModuleHandleA");
static_assert(sizeof(ImageHlpModule64) == 0x690, "push 690h into memset");

// IMAGEHLP_SYMBOL64 and the name buffer GetAddressSymbol gives it.
struct ImageHlpSymbol64 {
  /*0x00*/ DWORD SizeOfStruct;
  /*0x04*/ DWORD dwPad;
  /*0x08*/ DWORD64 Address;
  /*0x10*/ DWORD Size;
  /*0x14*/ DWORD Flags;
  /*0x18*/ DWORD MaxNameLength;
  /*0x1C*/ char Name[1];
};

static_assert(offsetof(ImageHlpSymbol64, Name) == 0x1C, "GetAddressSymbol: lea eax, [ebp-41Ch] into strncpy_s");
static_assert(sizeof(ImageHlpSymbol64) == 0x20, "GetAddressSymbol: mov dword ptr [ebp-438h], 20h");

struct SymbolBuffer {
  ImageHlpSymbol64 hdr;
  char szName[0x400];
};

static_assert(sizeof(SymbolBuffer) == 0x420, "GetAddressSymbol: push 420h into memset");

// IMAGEHLP_LINE64 as GetAddressFileAndLine fills it.
struct ImageHlpLine64 {
  /*0x00*/ DWORD SizeOfStruct;
  /*0x04*/ void *Key;
  /*0x08*/ DWORD LineNumber;
  /*0x0C*/ char *FileName;
  /*0x10*/ DWORD64 Address;
};

OC_X86_LAYOUT(sizeof(ImageHlpLine64) == 0x18, "GetAddressFileAndLine: mov dword ptr [ebp-30h], 18h");

// MINIDUMP_EXCEPTION_INFORMATION, MINIDUMP_USER_STREAM and
// MINIDUMP_USER_STREAM_INFORMATION, laid out in MiniDumpThreadWrite's frame.
struct MiniDumpExceptionInfo {
  /*0x00*/ DWORD dwThreadId;
  /*0x04*/ void *pExceptionPointers;
  /*0x08*/ BOOL bClientPointers;
};

OC_X86_LAYOUT(sizeof(MiniDumpExceptionInfo) == 0x0C, "MiniDumpThreadWrite: the stream record follows at [ebp-20h]");

struct MiniDumpUserStream {
  /*0x00*/ DWORD dwType;
  /*0x04*/ DWORD dwBufferSize;
  /*0x08*/ const void *pBuffer;
};

OC_X86_LAYOUT(sizeof(MiniDumpUserStream) == 0x0C, "MiniDumpThreadWrite: the stream information follows at [ebp-14h]");

struct MiniDumpUserStreamInfo {
  /*0x00*/ DWORD dwUserStreamCount;
  /*0x04*/ MiniDumpUserStream *pUserStreamArray;
};

OC_X86_LAYOUT(sizeof(MiniDumpUserStreamInfo) == 0x08, "MiniDumpThreadWrite: ends at the EH record at [ebp-0Ch]");

// The four dbghelp entries the EH-frame bodies call through. The 64-bit
// address travels as two dwords, low then high.
typedef int(__stdcall *PFN_SymGetModuleInfo64)(void *hProcess, DWORD dwAddrLow, DWORD dwAddrHigh,
                                               ImageHlpModule64 *pInfo);
typedef int(__stdcall *PFN_SymGetSymFromAddr64)(void *hProcess, DWORD dwAddrLow, DWORD dwAddrHigh,
                                                DWORD64 *pqwDisplacement, ImageHlpSymbol64 *pSymbol);
typedef int(__stdcall *PFN_SymGetLineFromAddr64)(void *hProcess, DWORD dwAddrLow, DWORD dwAddrHigh,
                                                 DWORD *pdwDisplacement, ImageHlpLine64 *pLine);
typedef int(__stdcall *PFN_MiniDumpWriteDump)(void *hProcess, DWORD dwProcessId, void *hFile,
                                              DWORD dwDumpType, MiniDumpExceptionInfo *pException,
                                              MiniDumpUserStreamInfo *pUserStreams, void *pCallback);

// The scoped lock on s_dbgHelpLock. 0x404110 is its destructor; ECX is never
// read. The constructor is inline in every body that takes the lock.
class CDbgHelpLock {
public:
  CDbgHelpLock();
  ~CDbgHelpLock();
};

// 0x4041B0. Nullary; all four callers read AL.
bool LoadLibrary();

// 0x404330. Nullary: no call site sets ECX and the body does not read it.
bool LoadSymbols();

// 0x404120. Nullary; its one caller discards EAX.
void DebuggerSetThreadName();

// 0x404420. ECX is the address, EDX the 0x100-byte name buffer and [ebp+0Ch]
// the out module base; [ebp+8] is passed and never read.
bool GetAddressModule(DWORD dwAddress, char *pszModule, int nUnused, DWORD *pdwBase);

// 0x404530. ECX is the address, EDX the 0x200-byte name buffer and [ebp+0Ch]
// the out displacement; [ebp+8] is passed and never read.
bool GetAddressSymbol(DWORD dwAddress, char *pszSymbol, int nUnused, DWORD *pdwDisplacement);

// 0x4046B0. ECX is the address, EDX the 0x100-byte file buffer and [ebp+0Ch]
// the out line number; [ebp+8] is passed and never read.
bool GetAddressFileAndLine(DWORD dwAddress, char *pszFile, int nUnused, DWORD *pdwLine);

// 0x404790. Nullary; its one caller discards EAX. Runs one queued request off
// s_miniDumpParam.
void MiniDumpThreadWrite();

// 0x404870. The minidump thread entry: __stdcall, one unread parameter.
DWORD __stdcall MiniDumpThreadProc(void *pParam);

// 0x4048D0. ECX is the UTF-8 dump path, EDX the comment stream, [ebp+8] the
// exception pointers and [ebp+0Ch] the thread id (0 meaning the caller's). The
// one caller reads AL.
bool WriteMiniDump(const char *pszPath, const char *pszComment, void *pExceptionPointers,
                   DWORD dwThreadId);

// 0x4049E0. Nullary; takes MiniDumpThreadProc's address as a thread entry.
void Initialize();

}  // namespace NDebugHelp

#endif  // OC117_NDEBUGHELP_H
