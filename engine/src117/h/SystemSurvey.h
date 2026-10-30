// SystemSurvey.h -- declarations for the machine-survey writer in
// SystemSurvey.cpp: the survey file it writes, the process timestamps it
// stamps into it, and the small file helpers around both. The free functions are
// __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_SYSTEMSURVEY_H
#define OC117_SYSTEMSURVEY_H

#include "platform.h"

// <windows.h> comes in here so its CreateDirectory macro is defined before the
// #undef below in every TU; otherwise this file's CreateDirectory becomes
// CreateDirectoryA, a legal overload with the wrong name.
#include <windows.h>
#undef CreateDirectory

// Microsoft's <time.h> layout: WriteTimestamp reads the first six fields, and
// GetEndTime and GetStartTime copy 0x24 bytes out of _gmtime64's result. The
// asserts sit outside the guard so they hold whichever definition the TU sees.
// The Universal CRT (`_UCRT`) defines struct tm without the _TM_DEFINED guard, so
// a compile against it takes the SDK's definition.
#if defined(_MSC_VER)  // Hyperion: elsewhere the C library's tm, whose first nine fields are these
#if defined(_UCRT)
#include <time.h>
#elif !defined(_TM_DEFINED)
struct tm {
        /*0x00*/ int tm_sec;
        /*0x04*/ int tm_min;
        /*0x08*/ int tm_hour;
        /*0x0C*/ int tm_mday;
        /*0x10*/ int tm_mon;
        /*0x14*/ int tm_year;
        /*0x18*/ int tm_wday;
        /*0x1C*/ int tm_yday;
        /*0x20*/ int tm_isdst;
        };
#define _TM_DEFINED
#endif

static_assert(offsetof(tm, tm_sec) == 0x00, "WriteTimestamp: push dword ptr [edx]");
static_assert(offsetof(tm, tm_min) == 0x04, "WriteTimestamp: push dword ptr [edx+4h]");
static_assert(offsetof(tm, tm_hour) == 0x08, "WriteTimestamp: push dword ptr [edx+8h]");
static_assert(offsetof(tm, tm_mday) == 0x0C, "WriteTimestamp: push dword ptr [edx+0Ch]");
static_assert(offsetof(tm, tm_mon) == 0x10, "WriteTimestamp: mov eax, [edx+10h] / inc eax");
static_assert(offsetof(tm, tm_year) == 0x14, "WriteTimestamp: mov eax, [edx+14h] / add eax, 76Ch");
static_assert(offsetof(tm, tm_wday) == 0x18, "SDK order; nothing in this TU reads it");
static_assert(offsetof(tm, tm_yday) == 0x1C, "SDK order; nothing in this TU reads it");
static_assert(offsetof(tm, tm_isdst) == 0x20, "GetEndTime: mov eax, [eax+20h] is the last word copied");
static_assert(sizeof(tm) == 0x24, "GetEndTime copies 0x10 + 0x10 + 4 bytes out of _gmtime64");

#endif

#if !defined(_TIME64_T_DEFINED) && defined(_MSC_VER)
typedef __int64 __time64_t;
#define _TIME64_T_DEFINED
#endif

#ifndef SEEK_SET
#define SEEK_CUR 1
#define SEEK_END 2
#define SEEK_SET 0
#endif

#if !defined(_FILETIME_) && defined(_MSC_VER)
#define _FILETIME_
typedef struct _FILETIME {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;
#endif

// The two imports GetStartTime calls through the import table, spelled exactly
// as <windows.h> spells them so a TU that sees both sees one declaration.
extern "C" {
__declspec(dllimport) void *__stdcall GetCurrentProcess(void);
__declspec(dllimport) int __stdcall GetProcessTimes(void *hProcess,
                                                    LPFILETIME pCreationTime,
                                                    LPFILETIME pExitTime,
                                                    LPFILETIME pKernelTime,
                                                    LPFILETIME pUserTime);
}

// The CRT entries SystemSurvey.cpp calls, spelled exactly as the CRT spells them.
// <stdio.h> is left out and FILE declared by hand: main.cpp includes this header
// and declares fopen returning void *, which cannot sit beside the CRT's.
#if defined(_MSC_VER)  // Hyperion: elsewhere compat/windows.h has them
#include <string.h>
#include <time.h>

typedef struct _iobuf FILE;

extern "C" {
int __cdecl fprintf(FILE *pFile, const char *pszFormat, ...);
size_t __cdecl fread(void *pDest, size_t nSize, size_t nCount, FILE *pFile);
int __cdecl fseek(FILE *pFile, long nOffset, int nOrigin);
long __cdecl ftell(FILE *pFile);
void __cdecl rewind(FILE *pFile);
size_t __cdecl strlen(const char *psz);
int __cdecl strncmp(const char *pszA, const char *pszB, size_t nLen);
struct tm *__cdecl _gmtime64(const __time64_t *pt);
__time64_t __cdecl _time64(__time64_t *pt);
int __cdecl sprintf_s(char *pszBuf, size_t cchBuf, const char *pszFormat, ...);
}
#else
#include <stdio.h>
#include <string.h>
#include <time.h>
#endif

// The shell import LocalAppDataDirectory calls, spelled as <shlobj.h> spells it.
extern "C" __declspec(dllimport) LONG __stdcall SHGetFolderPathA(HWND hwnd, int csidl,
                                                                 HANDLE hToken, DWORD dwFlags,
                                                                 LPSTR pszPath);


// 0x4BE990, zlib's crc32, defined in fd_user.cpp. ECX is the running CRC, EDX
// the buffer and the one pushed word the length.
unsigned int crc32(unsigned int nCrc, const void *pBuf, unsigned int nLen);

// 0x4A8320. ECX the destination, EDX the source and the stack word the
// destination's size. Copies the printable characters and drops the rest; with
// a size of 0 not even the terminator is written. Spelled as the local
// declaration in src117/lang/gluChatSlot.cpp spells it.
void __fastcall get_printable_string(char *pszDst, const char *pszSrc, int cch);

// 0x4A8390. ECX only; its one caller reads the full EAX. Checksums the whole
// file and leaves the stream position where it found it.
unsigned int CalcChecksum(FILE *pFile);

// 0x4A8430. ECX is a path; creates the directory holding its last component.
// True when the directory was created or already was one; the callers read AL.
bool CreateDirectory(const char *pszPath);

// 0x4A84D0. ECX only; both callers read AL. Fills *ptm with the current UTC
// time.
bool GetEndTime(struct tm *ptm);

// 0x4A8520. ECX only. Both exits clear EDX as well as setting EAX, so the return
// is 64 bits wide though the one caller reads only EAX. The name overloads
// <windows.h>'s GetFileSize, which takes a HANDLE.
unsigned __int64 GetFileSize(FILE *pFile);

// 0x4A8560. ECX only; both callers read AL. Fills *ptm with this process's
// creation time.
bool GetStartTime(struct tm *ptm);

// 0x4A85F0. Nullary; all three callers read the full EAX. Returns the
// four-character product code the survey is filed under.
char *GetSurveyProduct();

// 0x4A8600. Nullary; every caller reads the full EAX. Returns the survey's
// Appdata directory, creating it on first use, or NULL.
char *LocalAppDataDirectory();

// 0x4A8670. Nullary. Both exits set AL only, so the return is a bool;
// sub_4671B0 tail-jumps to it.
bool RunSystemSurvey();

// 0x4A8730. ECX, EDX and one pushed word, in fopen_s's argument order; three of
// the four callers read AL.
bool stat_open_file(FILE **ppFile, const char *pszName, const char *pszMode);

// 0x4A8750. ECX is the product code. Copies it into gszStatGameCode_g; the
// result is a constant true every caller discards.
bool SetSurveyProduct(const char *pszProduct);

// 0x4A8780. `ret 0Ch` with no register input, so __stdcall, strsafe.h's
// STRSAFEAPI. Returns an HRESULT every caller discards.
LONG __stdcall StringCchCopyA(char *pszDest, size_t cchDest, const char *pszSrc);

// 0x4A87F0. ECX the survey subdirectory, EDX the files in it and the stack word
// their count. Writes the .index file listing each file's size and checksum.
// Both callers discard AL.
bool WriteSurveyData(const char *pszDir, const char **apszFiles, unsigned int nFiles);

// 0x4A8AA0. ECX is a FILE ** (the body pushes [ecx], not ECX) and EDX the
// broken-down time. All four callers discard EAX.
void WriteTimestamp(FILE **ppFile, const struct tm *ptm);

// 0x4A8AD0. ECX and EDX; the one caller reads the full EAX. Returns the index of
// the named connection provider, and 0 both for no match and for a match on
// entry 0.
int get_provider_index(const char *pszName, size_t nLen);

#endif  // OC117_SYSTEMSURVEY_H
