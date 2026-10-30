// Logging.h -- declarations for src117/lang/Logging.cpp: the crash log's module
// roster, stack trace, stack frames and register dump. Class members are in the
// class headers. The free functions are __fastcall through /Gr and are declared
// without the keyword.

#ifndef OC117_LOGGING_H
#define OC117_LOGGING_H

#include "platform.h"
#include "EnumModuleData.h"
#include "Module.h"       // MODULE_DEBUG_INFO
#include "Storm.h"        // the one <windows.h> in the src117 closure
#include "crashy.h"       // crashy::StackFrameInfo, crashy::SymbolInfo

// The toolhelp entry points, declared here rather than by <tlhelp32.h>, which
// cannot give both call shapes: 1.17 calls CreateToolhelp32Snapshot through a
// thunk, so it is not dllimport, and the other four through the import table.
struct THREADENTRY32 {
  /*0x00*/ DWORD dwSize;
  /*0x04*/ DWORD cntUsage;
  /*0x08*/ DWORD th32ThreadID;
  /*0x0C*/ DWORD th32OwnerProcessID;
  /*0x10*/ LONG tpBasePri;
  /*0x14*/ LONG tpDeltaPri;
  /*0x18*/ DWORD dwFlags;
};

static_assert(offsetof(THREADENTRY32, dwSize)             == 0x00, "LogAllThreads: mov dword ptr [ebp-24h], 1Ch");
static_assert(offsetof(THREADENTRY32, cntUsage)           == 0x04, "Win32 layout, not oracle-witnessed");
static_assert(offsetof(THREADENTRY32, tpBasePri)          == 0x10, "Win32 layout, not oracle-witnessed");
static_assert(offsetof(THREADENTRY32, tpDeltaPri)         == 0x14, "Win32 layout, not oracle-witnessed");
static_assert(offsetof(THREADENTRY32, dwFlags)            == 0x18, "Win32 layout, not oracle-witnessed");
static_assert(offsetof(THREADENTRY32, th32ThreadID)       == 0x08, "LogAllThreads: mov ecx, dword ptr [ebp-1Ch]");
static_assert(offsetof(THREADENTRY32, th32OwnerProcessID) == 0x0C, "LogAllThreads: cmp dword ptr [ebp-18h], edi");
static_assert(sizeof(THREADENTRY32) == 0x1C, "LogAllThreads zeroes 16+8+4 bytes then writes dwSize = 1Ch");

struct MODULEENTRY32W {
  /*0x000*/ DWORD dwSize;
  /*0x004*/ DWORD th32ModuleID;
  /*0x008*/ DWORD th32ProcessID;
  /*0x00C*/ DWORD GlblcntUsage;
  /*0x010*/ DWORD ProccntUsage;
  /*0x014*/ BYTE *modBaseAddr;
  /*0x018*/ DWORD modBaseSize;
  /*0x01C*/ void *hModule;
  /*0x020*/ wchar_t szModule[256];
  /*0x220*/ wchar_t szExePath[260];
};

static_assert(offsetof(MODULEENTRY32W, dwSize)        == 0x000, "EnumModuleDebugInfo_TH32: mov dword ptr [esp+240h], 428h");
static_assert(offsetof(MODULEENTRY32W, th32ModuleID)  == 0x004, "Win32 layout, not oracle-witnessed");
static_assert(offsetof(MODULEENTRY32W, th32ProcessID) == 0x008, "Win32 layout, not oracle-witnessed");
static_assert(offsetof(MODULEENTRY32W, GlblcntUsage)  == 0x00C, "Win32 layout, not oracle-witnessed");
static_assert(offsetof(MODULEENTRY32W, ProccntUsage)  == 0x010, "Win32 layout, not oracle-witnessed");
OC_X86_LAYOUT(offsetof(MODULEENTRY32W, modBaseAddr)   == 0x014, "EnumModuleDebugInfo_TH32: mov eax, dword ptr [esp+254h]");
OC_X86_LAYOUT(offsetof(MODULEENTRY32W, modBaseSize)   == 0x018, "Win32 layout, not oracle-witnessed");
OC_X86_LAYOUT(offsetof(MODULEENTRY32W, hModule)       == 0x01C, "Win32 layout, not oracle-witnessed");
OC_X86_LAYOUT(offsetof(MODULEENTRY32W, szModule)      == 0x020, "EnumModuleDebugInfo_TH32: lea eax, [esp+274h] into WideCharToMultiByte");
OC_X86_LAYOUT(offsetof(MODULEENTRY32W, szExePath)     == 0x220, "Win32 layout, not oracle-witnessed");
OC_X86_LAYOUT(sizeof(MODULEENTRY32W) == 0x428, "EnumModuleDebugInfo_TH32: mov dword ptr [esp+240h], 428h");

extern "C" {
void *__stdcall CreateToolhelp32Snapshot(DWORD dwFlags, DWORD th32ProcessID);
__declspec(dllimport) BOOL __stdcall Thread32First(void *hSnapshot, THREADENTRY32 *pte);
__declspec(dllimport) BOOL __stdcall Thread32Next(void *hSnapshot, THREADENTRY32 *pte);
__declspec(dllimport) BOOL __stdcall Module32FirstW(void *hSnapshot, MODULEENTRY32W *pme);
__declspec(dllimport) BOOL __stdcall Module32NextW(void *hSnapshot, MODULEENTRY32W *pme);
}

// The loader's in-memory module list, walked by EnumModuleDebugInfo_Undocumented
// through fs:[18h] -> +30h -> +0Ch -> +14h. Only the fields it reads matter; the
// rest hold the offsets apart.
struct LDR_UNICODE_STRING {
  /*0x00*/ WORD Length;
  /*0x02*/ WORD MaximumLength;
  /*0x04*/ wchar_t *Buffer;
};

struct LDR_MODULE_ENTRY {
  /*0x000*/ LIST_ENTRY InLoadOrderLinks;
  /*0x008*/ LIST_ENTRY InMemoryOrderLinks;
  /*0x010*/ LIST_ENTRY InInitializationOrderLinks;
  /*0x018*/ void *DllBase;
  /*0x01C*/ void *EntryPoint;
  /*0x020*/ DWORD SizeOfImage;
  /*0x024*/ LDR_UNICODE_STRING FullDllName;
  /*0x02C*/ LDR_UNICODE_STRING BaseDllName;
};

struct LDR_DATA_MIN {
  /*0x00*/ DWORD Length;
  /*0x04*/ DWORD Initialized;
  /*0x08*/ void *SsHandle;
  /*0x0C*/ LIST_ENTRY InLoadOrderModuleList;
  /*0x14*/ LIST_ENTRY InMemoryOrderModuleList;
};

struct LDR_PEB_MIN {
  /*0x00*/ BYTE Reserved[0x0C];
  /*0x0C*/ LDR_DATA_MIN *Ldr;
};

struct LDR_TEB_MIN {
  /*0x00*/ BYTE Reserved[0x30];
  /*0x30*/ LDR_PEB_MIN *ProcessEnvironmentBlock;
};

static_assert(offsetof(LDR_UNICODE_STRING, Length)        == 0x00, "_Undocumented: movzx eax, word ptr [esi+1Ch]");
static_assert(offsetof(LDR_UNICODE_STRING, MaximumLength) == 0x02, "Win32 layout, not oracle-witnessed");
OC_X86_LAYOUT(offsetof(LDR_UNICODE_STRING, Buffer)        == 0x04, "_Undocumented: push dword ptr [esi+20h]");
OC_X86_LAYOUT(sizeof(LDR_UNICODE_STRING) == 0x08, "FullDllName and BaseDllName sit 8 apart");
static_assert(offsetof(LDR_MODULE_ENTRY, InLoadOrderLinks) == 0x000, "Win32 layout, not oracle-witnessed");
OC_X86_LAYOUT(offsetof(LDR_MODULE_ENTRY, InInitializationOrderLinks) == 0x010, "Win32 layout, not oracle-witnessed");
OC_X86_LAYOUT(offsetof(LDR_MODULE_ENTRY, InMemoryOrderLinks) == 0x008, "_Undocumented walks the list at Ldr+14h");
OC_X86_LAYOUT(offsetof(LDR_MODULE_ENTRY, DllBase)    == 0x018, "_Undocumented: push dword ptr [esi+10h], esi = entry+8");
OC_X86_LAYOUT(offsetof(LDR_MODULE_ENTRY, EntryPoint) == 0x01C, "Win32 layout, not oracle-witnessed");
OC_X86_LAYOUT(offsetof(LDR_MODULE_ENTRY, SizeOfImage)== 0x020, "Win32 layout, not oracle-witnessed");
OC_X86_LAYOUT(offsetof(LDR_MODULE_ENTRY, FullDllName)== 0x024, "_Undocumented: movzx eax, word ptr [esi+1Ch]");
OC_X86_LAYOUT(offsetof(LDR_MODULE_ENTRY, BaseDllName)== 0x02C, "Win32 layout, not oracle-witnessed");
OC_X86_LAYOUT(sizeof(LDR_MODULE_ENTRY) == 0x34, "the eight members above, no tail padding");
static_assert(offsetof(LDR_DATA_MIN, Length)      == 0x00, "Win32 layout, not oracle-witnessed");
static_assert(offsetof(LDR_DATA_MIN, Initialized) == 0x04, "Win32 layout, not oracle-witnessed");
static_assert(offsetof(LDR_DATA_MIN, SsHandle)    == 0x08, "Win32 layout, not oracle-witnessed");
OC_X86_LAYOUT(offsetof(LDR_DATA_MIN, InLoadOrderModuleList)   == 0x0C, "Win32 layout, not oracle-witnessed");
OC_X86_LAYOUT(offsetof(LDR_DATA_MIN, InMemoryOrderModuleList) == 0x14, "_Undocumented: add edi, 14h");
OC_X86_LAYOUT(sizeof(LDR_DATA_MIN) == 0x1C, "the five members above; the loader's record runs on past it");
static_assert(offsetof(LDR_PEB_MIN, Reserved) == 0x00, "only the Ldr pointer is read");
OC_X86_LAYOUT(offsetof(LDR_PEB_MIN, Ldr) == 0x0C, "_Undocumented: mov eax, dword ptr [eax+0Ch]");
OC_X86_LAYOUT(sizeof(LDR_PEB_MIN) == 0x10, "the two members above; the real PEB runs on past it");
static_assert(offsetof(LDR_TEB_MIN, Reserved) == 0x00, "only the PEB pointer is read");
static_assert(offsetof(LDR_TEB_MIN, ProcessEnvironmentBlock) == 0x30, "_Undocumented: mov eax, dword ptr [eax+30h]");
OC_X86_LAYOUT(sizeof(LDR_TEB_MIN) == 0x34, "the two members above; the real TEB runs on past it");

// 0x402FB0. The qsort comparison for the module roster.
int __cdecl sModuleCompareProc(const void *pv1, const void *pv2);

// 0x402FD0. ECX is the entry to append and EDX the roster; the caller
// (EnumModuleDebugInfo_TH32) tests AL.
bool CollectModulesCallback(const MODULE_DEBUG_INFO *pEntry, EnumModuleData *pData);

// 0x403000. ECX is the entry and EDX the destination; the third slot is never
// read, and LogModules leaves it undefined.
void FormatModuleDebugInfo(const MODULE_DEBUG_INFO *pEntry, char *pszBuf, size_t cchBuf);

// 0x4030F0. ECX is the frame and EDX the resolved symbol; of the stack slots
// only the first is read.
void FormatFunctionInfo(const crashy::StackFrameInfo *pFrame,
                        const crashy::SymbolInfo *pSymbol, char *pszBuf,
                        size_t cbUnused1, size_t cbUnused2);

// 0x405170 / 0x4052A0 / 0x405450, three routes to the same roster. Each reads
// EDX and never ECX: the first parameter is dead, and no caller sets it.
bool EnumModuleDebugInfo_PSAPI(int nUnused, EnumModuleData *pData);
bool EnumModuleDebugInfo_TH32(int nUnused, EnumModuleData *pData);
bool EnumModuleDebugInfo_Undocumented(int nUnused, EnumModuleData *pData);

// The three psapi entry points the PSAPI route binds by name, and the record
// GetModuleInformation fills; only the first field is read, as the base handed
// to ExecuteEnumCallback.
struct PSAPI_MODULEINFO {
  /*0x00*/ void *lpBaseOfDll;
  /*0x04*/ DWORD SizeOfImage;
  /*0x08*/ void *EntryPoint;
};

static_assert(offsetof(PSAPI_MODULEINFO, lpBaseOfDll) == 0x00, "EnumModuleDebugInfo_PSAPI: push dword ptr [esp+34h] off the record's head");
OC_X86_LAYOUT(offsetof(PSAPI_MODULEINFO, SizeOfImage) == 0x04, "Win32 layout, not oracle-witnessed");
OC_X86_LAYOUT(offsetof(PSAPI_MODULEINFO, EntryPoint)  == 0x08, "Win32 layout, not oracle-witnessed");
OC_X86_LAYOUT(sizeof(PSAPI_MODULEINFO) == 0x0C, "EnumModuleDebugInfo_PSAPI: push 0Ch");

typedef BOOL(__stdcall *PFN_ENUMPROCESSMODULES)(void *hProcess, HMODULE *phModules,
                                                DWORD cb, DWORD *pcbNeeded);
typedef BOOL(__stdcall *PFN_GETMODULEINFORMATION)(void *hProcess, HMODULE hModule,
                                                  PSAPI_MODULEINFO *pmi, DWORD cb);
typedef DWORD(__stdcall *PFN_GETMODULEBASENAMEW)(void *hProcess, HMODULE hModule,
                                                 wchar_t *pwszName, DWORD cchName);

extern "C" {
void __cdecl qsort(void *pBase, size_t nElems, size_t cbElem,
                   int (__cdecl *pfnCompare)(const void *, const void *));
}

#endif  // OC117_LOGGING_H
