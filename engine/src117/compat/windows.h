// windows.h, off Windows -- the part of Win32 the engine uses.
//
// The engine is Windows code: 1.17's C++ and the port's call Win32 for files, time,
// threads, events and the rest, and name its types. A Linux or Mac build puts this
// folder (src117/compat) ahead of the system's headers, so `#include <windows.h>` and the
// SDK's other headers land here and the same code compiles unchanged. The functions are
// port/platform/port_posix.cpp's, over POSIX and SDL. The Windows build never sees this folder.
//
// What is here is what the engine uses, with Windows' own values and layouts: a struct
// that is also a file format (the PE headers, WAVEFORMATEX) is laid out as on disk, and
// every 32-bit field is DWORD or LONG, 32 bits on every platform (h/platform.h). A
// service Linux does not have (the registry, code signatures, minidumps, ACLs) answers as
// Windows does when it fails, so the callers' failure paths run. The window and sound
// are not here: they are SDL's on every platform (port_winmsg.cpp, port_audio.cpp).
#pragma once
#if defined(_WIN32)
#error "src117/compat is for builds off Windows"
#endif

#include "../h/platform.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <wchar.h>
#include <ctype.h>
#include <signal.h>

// Ctrl+Break, Windows' second interrupt signal: Ctrl+\ off Windows
#ifndef SIGBREAK
#define SIGBREAK SIGQUIT
#endif

#ifdef __cplusplus
#define OC_EXTERN_C extern "C"
#else
#define OC_EXTERN_C
#endif

// ---------------------------------------------------------------------------
// types
// ---------------------------------------------------------------------------
typedef void VOID;
typedef char CHAR;
typedef short SHORT;
typedef int INT;
typedef unsigned int UINT;
typedef unsigned short USHORT;
typedef unsigned char UCHAR;
typedef unsigned char BOOLEAN;
typedef float FLOAT;
typedef wchar_t WCHAR_NATIVE;
typedef uint16_t WCHAR;  // UTF-16, as on Windows
typedef long long LONGLONG;  // __int64, as on Windows (int64_t is long on Linux)
typedef unsigned long long ULONGLONG;
typedef unsigned long long DWORD64;
typedef unsigned long long ULONG64;
typedef long long LONG64;
typedef long long INT64;
typedef unsigned long long UINT64;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef uint16_t UINT16;
typedef uint8_t UINT8;
typedef DWORD *PDWORD, *LPDWORD;
typedef DWORD64 *PDWORD64;
typedef WORD *PWORD, *LPWORD;
typedef BYTE *PBYTE, *LPBYTE;
typedef UCHAR *PUCHAR;
typedef LONG *PLONG, *LPLONG;
typedef ULONG *PULONG;
typedef BOOL *PBOOL, *LPBOOL;
typedef int *PINT, *LPINT;
typedef UINT *PUINT;
typedef void *PVOID, *LPVOID;
typedef const void *LPCVOID, *PCVOID;
typedef char *PSTR, *LPSTR;
typedef const char *PCSTR, *LPCSTR;
typedef WCHAR *PWSTR, *LPWSTR;
typedef const WCHAR *PCWSTR, *LPCWSTR;
typedef char TCHAR;
typedef LPSTR LPTSTR;
typedef LPCSTR LPCTSTR;
typedef LONG HRESULT;
typedef DWORD LCID;
typedef WORD LANGID;
typedef DWORD COLORREF;
typedef DWORD ACCESS_MASK;
typedef ULONG_PTR SIZE_T;
typedef LONG_PTR SSIZE_T;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;
typedef WORD ATOM;
typedef int errno_t;

// Handles, distinct types as Windows' STRICT makes them.
#define OC_HANDLE(name) struct name##__ { int unused; }; typedef struct name##__ *name
typedef void *HANDLE;
typedef HANDLE *PHANDLE, *LPHANDLE;
OC_HANDLE(HWND);
OC_HANDLE(HINSTANCE);
OC_HANDLE(HKEY);
OC_HANDLE(HDC);
OC_HANDLE(HICON);
OC_HANDLE(HMENU);
OC_HANDLE(HBITMAP);
OC_HANDLE(HFONT);
OC_HANDLE(HBRUSH);
OC_HANDLE(HPEN);
OC_HANDLE(HRGN);
OC_HANDLE(HPALETTE);
OC_HANDLE(HACCEL);
OC_HANDLE(HRSRC);
OC_HANDLE(HIMC);
OC_HANDLE(HMONITOR);
OC_HANDLE(HGLRC);
typedef HICON HCURSOR;
typedef HINSTANCE HMODULE;
typedef HANDLE HGLOBAL, HLOCAL, HGDIOBJ;
typedef HKEY *PHKEY;
typedef UINT_PTR SOCKET_HANDLE_;
#undef OC_HANDLE

typedef LONG_PTR (__stdcall *FARPROC)(void);
typedef LONG_PTR (__stdcall *NEARPROC)(void);
typedef LONG_PTR (__stdcall *PROC)(void);
typedef LRESULT (__stdcall *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef INT_PTR (__stdcall *DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef BOOL (__stdcall *WNDENUMPROC)(HWND, LPARAM);
typedef void (__stdcall *TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);
typedef DWORD (__stdcall *LPTHREAD_START_ROUTINE)(LPVOID);
typedef LPTHREAD_START_ROUTINE PTHREAD_START_ROUTINE;

typedef union _LARGE_INTEGER {
  struct {
    DWORD LowPart;
    LONG HighPart;
  };
  struct {
    DWORD LowPart;
    LONG HighPart;
  } u;
  LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
  struct {
    DWORD LowPart;
    DWORD HighPart;
  };
  struct {
    DWORD LowPart;
    DWORD HighPart;
  } u;
  ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

typedef struct _GUID {
  DWORD Data1;
  WORD Data2;
  WORD Data3;
  BYTE Data4[8];
} GUID, IID, CLSID;
typedef const GUID *LPCGUID;
typedef const GUID &REFGUID;
typedef const IID &REFIID;

typedef struct tagRECT {
  LONG left;
  LONG top;
  LONG right;
  LONG bottom;
} RECT, *PRECT, *LPRECT;
typedef const RECT *LPCRECT;

typedef struct tagPOINT {
  LONG x;
  LONG y;
} POINT, *PPOINT, *LPPOINT;

typedef struct tagSIZE {
  LONG cx;
  LONG cy;
} SIZE, *PSIZE, *LPSIZE;

typedef struct tagPOINTS {
  SHORT x;
  SHORT y;
} POINTS;

typedef struct tagPALETTEENTRY {
  BYTE peRed;
  BYTE peGreen;
  BYTE peBlue;
  BYTE peFlags;
} PALETTEENTRY, *PPALETTEENTRY, *LPPALETTEENTRY;

typedef struct tagRGBQUAD {
  BYTE rgbBlue;
  BYTE rgbGreen;
  BYTE rgbRed;
  BYTE rgbReserved;
} RGBQUAD;

typedef struct tagMSG {
  HWND hwnd;
  UINT message;
  WPARAM wParam;
  LPARAM lParam;
  DWORD time;
  POINT pt;
#if defined(__LP64__)
  DWORD lPrivate;
#endif
} MSG, *PMSG, *LPMSG;

typedef struct tagACCEL {
  BYTE fVirt;
  WORD key;
  WORD cmd;
} ACCEL, *LPACCEL;

typedef struct _FILETIME {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef struct _SYSTEMTIME {
  WORD wYear;
  WORD wMonth;
  WORD wDayOfWeek;
  WORD wDay;
  WORD wHour;
  WORD wMinute;
  WORD wSecond;
  WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

typedef struct _TIME_ZONE_INFORMATION {
  LONG Bias;
  WCHAR StandardName[32];
  SYSTEMTIME StandardDate;
  LONG StandardBias;
  WCHAR DaylightName[32];
  SYSTEMTIME DaylightDate;
  LONG DaylightBias;
} TIME_ZONE_INFORMATION, *PTIME_ZONE_INFORMATION, *LPTIME_ZONE_INFORMATION;
#define TIME_ZONE_ID_UNKNOWN 0
#define TIME_ZONE_ID_STANDARD 1
#define TIME_ZONE_ID_DAYLIGHT 2
#define TIME_ZONE_ID_INVALID ((DWORD)0xFFFFFFFF)

#define MAX_PATH 260

typedef struct _WIN32_FIND_DATAA {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwReserved0;
  DWORD dwReserved1;
  CHAR cFileName[MAX_PATH];
  CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, *PWIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;
typedef WIN32_FIND_DATAA WIN32_FIND_DATA;

typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA, *LPWIN32_FILE_ATTRIBUTE_DATA;
typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard, GetFileExMaxInfoLevel } GET_FILEEX_INFO_LEVELS;

typedef struct _OVERLAPPED {
  ULONG_PTR Internal;
  ULONG_PTR InternalHigh;
  union {
    struct {
      DWORD Offset;
      DWORD OffsetHigh;
    };
    PVOID Pointer;
  };
  HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

typedef struct _SECURITY_ATTRIBUTES {
  DWORD nLength;
  LPVOID lpSecurityDescriptor;
  BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct _STARTUPINFOA {
  DWORD cb;
  LPSTR lpReserved;
  LPSTR lpDesktop;
  LPSTR lpTitle;
  DWORD dwX;
  DWORD dwY;
  DWORD dwXSize;
  DWORD dwYSize;
  DWORD dwXCountChars;
  DWORD dwYCountChars;
  DWORD dwFillAttribute;
  DWORD dwFlags;
  WORD wShowWindow;
  WORD cbReserved2;
  LPBYTE lpReserved2;
  HANDLE hStdInput;
  HANDLE hStdOutput;
  HANDLE hStdError;
} STARTUPINFOA, *LPSTARTUPINFOA;
typedef STARTUPINFOA STARTUPINFO;

typedef struct _PROCESS_INFORMATION {
  HANDLE hProcess;
  HANDLE hThread;
  DWORD dwProcessId;
  DWORD dwThreadId;
} PROCESS_INFORMATION, *PPROCESS_INFORMATION, *LPPROCESS_INFORMATION;

typedef struct _SYSTEM_INFO {
  union {
    DWORD dwOemId;
    struct {
      WORD wProcessorArchitecture;
      WORD wReserved;
    };
  };
  DWORD dwPageSize;
  LPVOID lpMinimumApplicationAddress;
  LPVOID lpMaximumApplicationAddress;
  DWORD_PTR dwActiveProcessorMask;
  DWORD dwNumberOfProcessors;
  DWORD dwProcessorType;
  DWORD dwAllocationGranularity;
  WORD wProcessorLevel;
  WORD wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;
#define PROCESSOR_INTEL_386 386
#define PROCESSOR_INTEL_486 486
#define PROCESSOR_INTEL_PENTIUM 586
#define PROCESSOR_ARCHITECTURE_INTEL 0
#define PROCESSOR_ARCHITECTURE_AMD64 9
#define PROCESSOR_ARCHITECTURE_ARM64 12

typedef struct _MEMORYSTATUS {
  DWORD dwLength;
  DWORD dwMemoryLoad;
  SIZE_T dwTotalPhys;
  SIZE_T dwAvailPhys;
  SIZE_T dwTotalPageFile;
  SIZE_T dwAvailPageFile;
  SIZE_T dwTotalVirtual;
  SIZE_T dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;

typedef struct _MEMORY_BASIC_INFORMATION {
  PVOID BaseAddress;
  PVOID AllocationBase;
  DWORD AllocationProtect;
  WORD PartitionId;
  SIZE_T RegionSize;
  DWORD State;
  DWORD Protect;
  DWORD Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

typedef struct tagVS_FIXEDFILEINFO {
  DWORD dwSignature;
  DWORD dwStrucVersion;
  DWORD dwFileVersionMS;
  DWORD dwFileVersionLS;
  DWORD dwProductVersionMS;
  DWORD dwProductVersionLS;
  DWORD dwFileFlagsMask;
  DWORD dwFileFlags;
  DWORD dwFileOS;
  DWORD dwFileType;
  DWORD dwFileSubtype;
  DWORD dwFileDateMS;
  DWORD dwFileDateLS;
} VS_FIXEDFILEINFO;

// The critical section and the slim lock are the port's own (port_posix.cpp). A critical
// section is Windows x64's size, the room each of 1.17's own has in the wide storage
// (gen/wide117.json): its lock, and space the browser's lock is kept in.
typedef struct _CRITICAL_SECTION {
  void *pImpl;
  int64_t aSpace[4];
} CRITICAL_SECTION, *PCRITICAL_SECTION, *LPCRITICAL_SECTION;
typedef struct _RTL_SRWLOCK {
  PVOID Ptr;
} SRWLOCK, *PSRWLOCK;
#define SRWLOCK_INIT {0}

// ---------------------------------------------------------------------------
// the PE image, as on disk (the player's 1.17 exe and its resources)
// ---------------------------------------------------------------------------
#pragma pack(push, 4)
typedef struct _IMAGE_DOS_HEADER {
  WORD e_magic;
  WORD e_cblp;
  WORD e_cp;
  WORD e_crlc;
  WORD e_cparhdr;
  WORD e_minalloc;
  WORD e_maxalloc;
  WORD e_ss;
  WORD e_sp;
  WORD e_csum;
  WORD e_ip;
  WORD e_cs;
  WORD e_lfarlc;
  WORD e_ovno;
  WORD e_res[4];
  WORD e_oemid;
  WORD e_oeminfo;
  WORD e_res2[10];
  LONG e_lfanew;
} IMAGE_DOS_HEADER, *PIMAGE_DOS_HEADER;
#pragma pack(pop)
#define IMAGE_DOS_SIGNATURE 0x5A4D
#define IMAGE_NT_SIGNATURE 0x00004550

typedef struct _IMAGE_FILE_HEADER {
  WORD Machine;
  WORD NumberOfSections;
  DWORD TimeDateStamp;
  DWORD PointerToSymbolTable;
  DWORD NumberOfSymbols;
  WORD SizeOfOptionalHeader;
  WORD Characteristics;
} IMAGE_FILE_HEADER, *PIMAGE_FILE_HEADER;

typedef struct _IMAGE_DATA_DIRECTORY {
  DWORD VirtualAddress;
  DWORD Size;
} IMAGE_DATA_DIRECTORY, *PIMAGE_DATA_DIRECTORY;

typedef struct _IMAGE_DEBUG_DIRECTORY {
  DWORD Characteristics;
  DWORD TimeDateStamp;
  WORD MajorVersion;
  WORD MinorVersion;
  DWORD Type;
  DWORD SizeOfData;
  DWORD AddressOfRawData;
  DWORD PointerToRawData;
} IMAGE_DEBUG_DIRECTORY, *PIMAGE_DEBUG_DIRECTORY;
#define IMAGE_DEBUG_TYPE_CODEVIEW 2

typedef struct _LIST_ENTRY {
  struct _LIST_ENTRY *Flink;
  struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;
#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16
#define IMAGE_DIRECTORY_ENTRY_EXPORT 0
#define IMAGE_DIRECTORY_ENTRY_IMPORT 1
#define IMAGE_DIRECTORY_ENTRY_RESOURCE 2
#define IMAGE_DIRECTORY_ENTRY_BASERELOC 5

typedef struct _IMAGE_OPTIONAL_HEADER {
  WORD Magic;
  BYTE MajorLinkerVersion;
  BYTE MinorLinkerVersion;
  DWORD SizeOfCode;
  DWORD SizeOfInitializedData;
  DWORD SizeOfUninitializedData;
  DWORD AddressOfEntryPoint;
  DWORD BaseOfCode;
  DWORD BaseOfData;
  DWORD ImageBase;
  DWORD SectionAlignment;
  DWORD FileAlignment;
  WORD MajorOperatingSystemVersion;
  WORD MinorOperatingSystemVersion;
  WORD MajorImageVersion;
  WORD MinorImageVersion;
  WORD MajorSubsystemVersion;
  WORD MinorSubsystemVersion;
  DWORD Win32VersionValue;
  DWORD SizeOfImage;
  DWORD SizeOfHeaders;
  DWORD CheckSum;
  WORD Subsystem;
  WORD DllCharacteristics;
  DWORD SizeOfStackReserve;
  DWORD SizeOfStackCommit;
  DWORD SizeOfHeapReserve;
  DWORD SizeOfHeapCommit;
  DWORD LoaderFlags;
  DWORD NumberOfRvaAndSizes;
  IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} IMAGE_OPTIONAL_HEADER32, *PIMAGE_OPTIONAL_HEADER32;

typedef struct _IMAGE_NT_HEADERS {
  DWORD Signature;
  IMAGE_FILE_HEADER FileHeader;
  IMAGE_OPTIONAL_HEADER32 OptionalHeader;
} IMAGE_NT_HEADERS32, *PIMAGE_NT_HEADERS32;

#define IMAGE_SIZEOF_SHORT_NAME 8
typedef struct _IMAGE_SECTION_HEADER {
  BYTE Name[IMAGE_SIZEOF_SHORT_NAME];
  union {
    DWORD PhysicalAddress;
    DWORD VirtualSize;
  } Misc;
  DWORD VirtualAddress;
  DWORD SizeOfRawData;
  DWORD PointerToRawData;
  DWORD PointerToRelocations;
  DWORD PointerToLinenumbers;
  WORD NumberOfRelocations;
  WORD NumberOfLinenumbers;
  DWORD Characteristics;
} IMAGE_SECTION_HEADER, *PIMAGE_SECTION_HEADER;
#define IMAGE_FIRST_SECTION(nt)                                                                          \
  ((PIMAGE_SECTION_HEADER)((ULONG_PTR)(nt) + offsetof(IMAGE_NT_HEADERS32, OptionalHeader) +            \
                           ((const IMAGE_NT_HEADERS32 *)(nt))->FileHeader.SizeOfOptionalHeader))

typedef struct _IMAGE_BASE_RELOCATION {
  DWORD VirtualAddress;
  DWORD SizeOfBlock;
} IMAGE_BASE_RELOCATION, *PIMAGE_BASE_RELOCATION;
#define IMAGE_REL_BASED_ABSOLUTE 0
#define IMAGE_REL_BASED_HIGHLOW 3
#define IMAGE_REL_BASED_DIR64 10

// ---------------------------------------------------------------------------
// sound formats (the game's WAV headers)
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
typedef struct tWAVEFORMATEX {
  WORD wFormatTag;
  WORD nChannels;
  DWORD nSamplesPerSec;
  DWORD nAvgBytesPerSec;
  WORD nBlockAlign;
  WORD wBitsPerSample;
  WORD cbSize;
} WAVEFORMATEX, *PWAVEFORMATEX, *LPWAVEFORMATEX;
typedef const WAVEFORMATEX *LPCWAVEFORMATEX;
typedef struct waveformat_tag {
  WORD wFormatTag;
  WORD nChannels;
  DWORD nSamplesPerSec;
  DWORD nAvgBytesPerSec;
  WORD nBlockAlign;
} WAVEFORMAT, *PWAVEFORMAT, *LPWAVEFORMAT;
typedef struct pcmwaveformat_tag {
  WAVEFORMAT wf;
  WORD wBitsPerSample;
} PCMWAVEFORMAT, *PPCMWAVEFORMAT, *LPPCMWAVEFORMAT;
#pragma pack(pop)
#define WAVE_FORMAT_PCM 1

// ---------------------------------------------------------------------------
// values
// ---------------------------------------------------------------------------
#define CONST const
#define CALLBACK
#define IN
#define OUT
#define OPTIONAL
#define UNREFERENCED_PARAMETER(p) ((void)(p))
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define INFINITE 0xFFFFFFFF

#define MAKEWORD(a, b) ((WORD)(((BYTE)((DWORD_PTR)(a) & 0xff)) | ((WORD)((BYTE)((DWORD_PTR)(b) & 0xff))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)((DWORD_PTR)(a) & 0xffff)) | ((DWORD)((WORD)((DWORD_PTR)(b) & 0xffff))) << 16))
#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l) ((WORD)((DWORD_PTR)(l) >> 16))
#define LOBYTE(w) ((BYTE)((DWORD_PTR)(w) & 0xff))
#define HIBYTE(w) ((BYTE)((DWORD_PTR)(w) >> 8))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)MAKELONG(l, h))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define GET_WHEEL_DELTA_WPARAM(wParam) ((short)HIWORD(wParam))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCE MAKEINTRESOURCEA
#define IS_INTRESOURCE(r) ((((ULONG_PTR)(r)) >> 16) == 0)
#define MAKELANGID(p, s) ((((WORD)(s)) << 10) | (WORD)(p))
#define PRIMARYLANGID(lgid) ((WORD)(lgid) & 0x3ff)
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#define _countof ARRAYSIZE
#define ZeroMemory(d, n) memset((d), 0, (n))
#define SecureZeroMemory(d, n) memset((d), 0, (n))
#define CopyMemory(d, s, n) memcpy((d), (s), (n))
#define MoveMemory(d, s, n) memmove((d), (s), (n))
#define FillMemory(d, n, v) memset((d), (v), (n))
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_FAIL ((HRESULT)0x80004005)
#define E_NOTIMPL ((HRESULT)0x80004001)
#define E_NOINTERFACE ((HRESULT)0x80004002)
#define E_OUTOFMEMORY ((HRESULT)0x8007000E)
#define E_INVALIDARG ((HRESULT)0x80070057)

// resource types
#define RT_CURSOR MAKEINTRESOURCEA(1)
#define RT_BITMAP MAKEINTRESOURCEA(2)
#define RT_ICON MAKEINTRESOURCEA(3)
#define RT_MENU MAKEINTRESOURCEA(4)
#define RT_DIALOG MAKEINTRESOURCEA(5)
#define RT_STRING MAKEINTRESOURCEA(6)
#define RT_ACCELERATOR MAKEINTRESOURCEA(9)
#define RT_RCDATA MAKEINTRESOURCEA(10)
#define RT_GROUP_ICON MAKEINTRESOURCEA(14)
#define RT_VERSION MAKEINTRESOURCEA(16)

// errors
#define ERROR_SUCCESS 0L
#define NO_ERROR 0L
#define ERROR_INVALID_FUNCTION 1L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_TOO_MANY_OPEN_FILES 4L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_INVALID_DATA 13L
#define ERROR_OUTOFMEMORY 14L
#define ERROR_INVALID_DRIVE 15L
#define ERROR_NO_MORE_FILES 18L
#define ERROR_WRITE_PROTECT 19L
#define ERROR_NOT_READY 21L
#define ERROR_SHARING_VIOLATION 32L
#define ERROR_HANDLE_EOF 38L
#define ERROR_HANDLE_DISK_FULL 39L
#define ERROR_NOT_SUPPORTED 50L
#define ERROR_FILE_EXISTS 80L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_DISK_FULL 112L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_INVALID_NAME 123L
#define ERROR_DIR_NOT_EMPTY 145L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_MORE_DATA 234L
#define ERROR_NO_MORE_ITEMS 259L
#define ERROR_FILE_CORRUPT 1392L
#define ERROR_TIMEOUT 1460L
#define ERROR_RESOURCE_NAME_NOT_FOUND 1814L
#define ERROR_RESOURCE_TYPE_NOT_FOUND 1813L

// files
#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#define GENERIC_EXECUTE 0x20000000
#define GENERIC_ALL 0x10000000
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define FILE_SHARE_DELETE 0x00000004
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100
#define FILE_FLAG_WRITE_THROUGH 0x80000000
#define FILE_FLAG_OVERLAPPED 0x40000000
#define FILE_FLAG_NO_BUFFERING 0x20000000
#define FILE_FLAG_RANDOM_ACCESS 0x10000000
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define MOVEFILE_COPY_ALLOWED 0x00000002
#define MOVEFILE_WRITE_THROUGH 0x00000008
#define DRIVE_UNKNOWN 0
#define DRIVE_NO_ROOT_DIR 1
#define DRIVE_REMOVABLE 2
#define DRIVE_FIXED 3
#define DRIVE_REMOTE 4
#define DRIVE_CDROM 5
#define DRIVE_RAMDISK 6
#define SEM_FAILCRITICALERRORS 0x0001
#define SEM_NOGPFAULTERRORBOX 0x0002
#define SEM_NOOPENFILEERRORBOX 0x8000
#define FILE_MAP_WRITE 0x0002
#define FILE_MAP_READ 0x0004
#define FILE_MAP_ALL_ACCESS 0xF001F

// memory
#define MEM_COMMIT 0x00001000
#define MEM_RESERVE 0x00002000
#define MEM_DECOMMIT 0x00004000
#define MEM_RELEASE 0x00008000
#define MEM_FREE 0x00010000
#define MEM_PRIVATE 0x00020000
#define MEM_IMAGE 0x01000000
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_GUARD 0x100
#define LMEM_FIXED 0x0000
#define LMEM_ZEROINIT 0x0040
#define LPTR (LMEM_FIXED | LMEM_ZEROINIT)
#define GMEM_FIXED 0x0000
#define GMEM_ZEROINIT 0x0040

// waits, threads, processes
#define WAIT_OBJECT_0 0x00000000L
#define WAIT_ABANDONED 0x00000080L
#define WAIT_TIMEOUT 0x00000102L
#define WAIT_FAILED ((DWORD)0xFFFFFFFF)
#define STILL_ACTIVE 0x00000103L
#define CREATE_SUSPENDED 0x00000004
#define CREATE_NO_WINDOW 0x08000000  // no console; ignored outside Windows
#define SYNCHRONIZE 0x00100000L
#define PROCESS_QUERY_INFORMATION 0x0400
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define PROCESS_TERMINATE 0x0001
#define THREAD_SUSPEND_RESUME 0x0002
#define THREAD_GET_CONTEXT 0x0008
#define THREAD_QUERY_INFORMATION 0x0040
#define DUPLICATE_SAME_ACCESS 0x00000002
#define STARTF_USESHOWWINDOW 0x00000001
#define NORMAL_PRIORITY_CLASS 0x00000020
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_LOWEST (-2)

// code pages, strings, messages
#define CP_ACP 0
#define CP_OEMCP 1
#define CP_UTF8 65001
#define MB_ERR_INVALID_CHARS 0x00000008
#define WC_NO_BEST_FIT_CHARS 0x00000400
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define FORMAT_MESSAGE_FROM_STRING 0x00000400
#define FORMAT_MESSAGE_FROM_HMODULE 0x00000800
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000
#define LANG_NEUTRAL 0x00
#define LANG_KOREAN 0x12
#define SUBLANG_DEFAULT 0x01
#define LOAD_LIBRARY_AS_DATAFILE 0x00000002
#define LOAD_LIBRARY_AS_IMAGE_RESOURCE 0x00000020
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#define GET_MODULE_HANDLE_EX_FLAG_PIN 0x00000001
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004

// message boxes
#define MB_OK 0x00000000L
#define MB_OKCANCEL 0x00000001L
#define MB_ABORTRETRYIGNORE 0x00000002L
#define MB_YESNOCANCEL 0x00000003L
#define MB_YESNO 0x00000004L
#define MB_RETRYCANCEL 0x00000005L
#define MB_ICONHAND 0x00000010L
#define MB_ICONQUESTION 0x00000020L
#define MB_ICONEXCLAMATION 0x00000030L
#define MB_ICONASTERISK 0x00000040L
#define MB_ICONERROR MB_ICONHAND
#define MB_ICONSTOP MB_ICONHAND
#define MB_ICONWARNING MB_ICONEXCLAMATION
#define MB_ICONINFORMATION MB_ICONASTERISK
#define MB_SYSTEMMODAL 0x00001000L
#define MB_TASKMODAL 0x00002000L
#define MB_SETFOREGROUND 0x00010000L
#define MB_TOPMOST 0x00040000L
#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7

// windows and their messages
#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SHOWWINDOW 0x0018
#define WM_ACTIVATEAPP 0x001C
#define WM_SETCURSOR 0x0020
#define WM_GETMINMAXINFO 0x0024
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_SETICON 0x0080
#define WM_NCHITTEST 0x0084
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_IME_STARTCOMPOSITION 0x010D
#define WM_IME_ENDCOMPOSITION 0x010E
#define WM_IME_COMPOSITION 0x010F
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_MOUSEWHEEL 0x020A
#define WM_XBUTTONDOWN 0x020B
#define WM_XBUTTONUP 0x020C
#define WM_CAPTURECHANGED 0x0215
#define WM_IME_SETCONTEXT 0x0281
#define WM_IME_NOTIFY 0x0282
#define WM_USER 0x0400
#define WM_APP 0x8000
#define SC_CLOSE 0xF060
#define SC_KEYMENU 0xF100
#define SC_SCREENSAVE 0xF140
#define SC_MONITORPOWER 0xF170
#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_SHIFT 0x0004
#define MK_CONTROL 0x0008
#define MK_MBUTTON 0x0010
#define WHEEL_DELTA 120
#define FVIRTKEY TRUE
#define FNOINVERT 0x02
#define FSHIFT 0x04
#define FCONTROL 0x08
#define FALT 0x10
#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP 0x80000000L
#define WS_VISIBLE 0x10000000L
#define WS_CAPTION 0x00C00000L
#define WS_SYSMENU 0x00080000L
#define WS_THICKFRAME 0x00040000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_EX_TOPMOST 0x00000008L
#define WS_EX_APPWINDOW 0x00040000L
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWLP_WNDPROC (-4)
#define GWLP_USERDATA (-21)
#define GW_HWNDFIRST 0
#define GW_HWNDLAST 1
#define GW_HWNDNEXT 2
#define GW_HWNDPREV 3
#define GW_OWNER 4
#define GW_CHILD 5
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_NORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_SHOWNOACTIVATE 4
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_RESTORE 9
#define SW_SHOWDEFAULT 10
#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOACTIVATE 0x0010
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW 0x0040
#define HWND_TOP ((HWND)0)
#define HWND_TOPMOST ((HWND)-1)
#define HWND_NOTOPMOST ((HWND)-2)
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXICON 11
#define SM_CYICON 12
#define SM_CXSMICON 49
#define SM_CYSMICON 50
#define IDC_ARROW MAKEINTRESOURCEA(32512)
#define IMAGE_BITMAP 0
#define IMAGE_ICON 1
#define IMAGE_CURSOR 2
#define LR_DEFAULTCOLOR 0x0000
#define LR_SHARED 0x8000

// virtual keys
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_HANGUL 0x15
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SELECT 0x29
#define VK_PRINT 0x2A
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_F13 0x7C
#define VK_F14 0x7D
#define VK_F15 0x7E
#define VK_F16 0x7F
#define VK_F17 0x80
#define VK_F18 0x81
#define VK_F19 0x82
#define VK_F20 0x83
#define VK_F21 0x84
#define VK_F22 0x85
#define VK_F23 0x86
#define VK_F24 0x87
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_OEM_102 0xE2

// GDI (gds_font.cpp's measuring of the Korean font): no device off Windows
#define SRCCOPY ((DWORD)0x00CC0020)
#define TRANSPARENT 1
#define OPAQUE 2
#define DT_LEFT 0x0000
#define DT_CENTER 0x0001
#define DT_RIGHT 0x0002
#define DT_VCENTER 0x0004
#define DT_SINGLELINE 0x0020
#define DT_NOCLIP 0x0100
#define DT_CALCRECT 0x0400
#define DIB_RGB_COLORS 0
#define BI_RGB 0L
#define HORZRES 8
#define VERTRES 10
#define BITSPIXEL 12
#define PLANES 14
#define RASTERCAPS 38
#define LOGPIXELSY 90
#define NUMCOLORS 24
#define SIZEPALETTE 104
#define RC_PALETTE 0x0100
#define BLACK_BRUSH 4
#define WHITE_BRUSH 0
#define NULL_BRUSH 5
#define BLACK_PEN 7
#define WHITE_PEN 6
#define DEFAULT_GUI_FONT 17
#define SYSTEM_FONT 13
#define FW_NORMAL 400
#define FW_BOLD 700
#define DEFAULT_CHARSET 1
#define HANGEUL_CHARSET 129
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_QUALITY 0
#define NONANTIALIASED_QUALITY 3
#define DEFAULT_PITCH 0
#define FF_DONTCARE (0 << 4)
#define LF_FACESIZE 32
typedef struct tagLOGFONTA {
  LONG lfHeight;
  LONG lfWidth;
  LONG lfEscapement;
  LONG lfOrientation;
  LONG lfWeight;
  BYTE lfItalic;
  BYTE lfUnderline;
  BYTE lfStrikeOut;
  BYTE lfCharSet;
  BYTE lfOutPrecision;
  BYTE lfClipPrecision;
  BYTE lfQuality;
  BYTE lfPitchAndFamily;
  CHAR lfFaceName[LF_FACESIZE];
} LOGFONTA, *PLOGFONTA, *LPLOGFONTA;
typedef LOGFONTA LOGFONT;
#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER {
  WORD bfType;
  DWORD bfSize;
  WORD bfReserved1;
  WORD bfReserved2;
  DWORD bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER, *LPBITMAPFILEHEADER;
#pragma pack(pop)
typedef struct tagBITMAPINFOHEADER {
  DWORD biSize;
  LONG biWidth;
  LONG biHeight;
  WORD biPlanes;
  WORD biBitCount;
  DWORD biCompression;
  DWORD biSizeImage;
  LONG biXPelsPerMeter;
  LONG biYPelsPerMeter;
  DWORD biClrUsed;
  DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER, *LPBITMAPINFOHEADER;
typedef struct tagBITMAPINFO {
  BITMAPINFOHEADER bmiHeader;
  RGBQUAD bmiColors[1];
} BITMAPINFO, *PBITMAPINFO, *LPBITMAPINFO;
typedef struct tagBITMAP {
  LONG bmType;
  LONG bmWidth;
  LONG bmHeight;
  LONG bmWidthBytes;
  WORD bmPlanes;
  WORD bmBitsPixel;
  LPVOID bmBits;
} BITMAP, *PBITMAP, *LPBITMAP;
typedef struct tagTEXTMETRICA {
  LONG tmHeight;
  LONG tmAscent;
  LONG tmDescent;
  LONG tmInternalLeading;
  LONG tmExternalLeading;
  LONG tmAveCharWidth;
  LONG tmMaxCharWidth;
  LONG tmWeight;
  LONG tmOverhang;
  LONG tmDigitizedAspectX;
  LONG tmDigitizedAspectY;
  BYTE tmFirstChar;
  BYTE tmLastChar;
  BYTE tmDefaultChar;
  BYTE tmBreakChar;
  BYTE tmItalic;
  BYTE tmUnderlined;
  BYTE tmStruckOut;
  BYTE tmPitchAndFamily;
  BYTE tmCharSet;
} TEXTMETRICA, *LPTEXTMETRICA;

// the registry, which Linux has none of: every key is missing
#define HKEY_CLASSES_ROOT ((HKEY)(ULONG_PTR)((LONG)0x80000000))
#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)((LONG)0x80000001))
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)((LONG)0x80000002))
#define HKEY_USERS ((HKEY)(ULONG_PTR)((LONG)0x80000003))
#define KEY_QUERY_VALUE 0x0001
#define KEY_SET_VALUE 0x0002
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define KEY_ALL_ACCESS 0xF003F
#define KEY_WOW64_32KEY 0x0200
#define KEY_WOW64_64KEY 0x0100
#define REG_NONE 0
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_BINARY 3
#define REG_DWORD 4
typedef ACCESS_MASK REGSAM;

// shell folders (shlobj.h)
#define CSIDL_PERSONAL 0x0005
#define CSIDL_APPDATA 0x001A
#define CSIDL_LOCAL_APPDATA 0x001C
#define CSIDL_COMMON_APPDATA 0x0023
#define CSIDL_PROGRAM_FILES 0x0026
#define CSIDL_PROGRAM_FILESX86 0x002A
#define CSIDL_FLAG_CREATE 0x8000
#define SHGFP_TYPE_CURRENT 0

// structured exceptions: the codes, which the crash report names
#define EXCEPTION_ACCESS_VIOLATION ((DWORD)0xC0000005L)
#define EXCEPTION_DATATYPE_MISALIGNMENT ((DWORD)0x80000002L)
#define EXCEPTION_BREAKPOINT ((DWORD)0x80000003L)
#define EXCEPTION_SINGLE_STEP ((DWORD)0x80000004L)
#define EXCEPTION_ARRAY_BOUNDS_EXCEEDED ((DWORD)0xC000008CL)
#define EXCEPTION_FLT_DIVIDE_BY_ZERO ((DWORD)0xC000008EL)
#define EXCEPTION_INT_DIVIDE_BY_ZERO ((DWORD)0xC0000094L)
#define EXCEPTION_INT_OVERFLOW ((DWORD)0xC0000095L)
#define EXCEPTION_PRIV_INSTRUCTION ((DWORD)0xC0000096L)
#define EXCEPTION_ILLEGAL_INSTRUCTION ((DWORD)0xC000001DL)
#define EXCEPTION_STACK_OVERFLOW ((DWORD)0xC00000FDL)
#define EXCEPTION_IN_PAGE_ERROR ((DWORD)0xC0000006L)
#define EXCEPTION_NONCONTINUABLE 0x1
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_MAXIMUM_PARAMETERS 15
typedef struct _EXCEPTION_RECORD {
  DWORD ExceptionCode;
  DWORD ExceptionFlags;
  struct _EXCEPTION_RECORD *ExceptionRecord;
  PVOID ExceptionAddress;
  DWORD NumberParameters;
  ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;
// The registers at a fault, under the names the crash report prints; filled from the
// signal's context off Windows (port_crash.cpp).
typedef struct _CONTEXT {
  DWORD ContextFlags;
  DWORD64 Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi;
  DWORD64 R8, R9, R10, R11, R12, R13, R14, R15;
  DWORD64 Rip;
  DWORD EFlags;
} CONTEXT, *PCONTEXT;
#define CONTEXT_FULL 0x0010000B
#define CONTEXT_CONTROL 0x00100001
typedef struct _EXCEPTION_POINTERS {
  PEXCEPTION_RECORD ExceptionRecord;
  PCONTEXT ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS, *LPEXCEPTION_POINTERS;
typedef LONG(__stdcall *PTOP_LEVEL_EXCEPTION_FILTER)(EXCEPTION_POINTERS *);
typedef PTOP_LEVEL_EXCEPTION_FILTER LPTOP_LEVEL_EXCEPTION_FILTER;
typedef LONG(__stdcall *PVECTORED_EXCEPTION_HANDLER)(EXCEPTION_POINTERS *);
#define GetExceptionCode() ((DWORD)0)
#define GetExceptionInformation() ((EXCEPTION_POINTERS *)0)

// security (main.cpp's folder ACLs, which Linux leaves to the file mode)
typedef PVOID PSID;
typedef struct _ACL {
  BYTE AclRevision;
  BYTE Sbz1;
  WORD AclSize;
  WORD AceCount;
  WORD Sbz2;
} ACL, *PACL;
typedef struct _SID_IDENTIFIER_AUTHORITY {
  BYTE Value[6];
} SID_IDENTIFIER_AUTHORITY, *PSID_IDENTIFIER_AUTHORITY;
typedef struct _SID_AND_ATTRIBUTES {
  PSID Sid;
  DWORD Attributes;
} SID_AND_ATTRIBUTES;
typedef struct _TOKEN_USER {
  SID_AND_ATTRIBUTES User;
} TOKEN_USER, *PTOKEN_USER;
typedef enum _TOKEN_INFORMATION_CLASS { TokenUser = 1 } TOKEN_INFORMATION_CLASS;
#define SECURITY_WORLD_SID_AUTHORITY {{0, 0, 0, 0, 0, 1}}
#define SECURITY_WORLD_RID 0x00000000L
#define ACL_REVISION 2
#define TOKEN_QUERY 0x0008
#define GENERIC_ALL_ACCESS 0x10000000

// ---------------------------------------------------------------------------
// functions (port/platform/port_posix.cpp)
// ---------------------------------------------------------------------------
#define WINBASEAPI OC_EXTERN_C
#define WINUSERAPI OC_EXTERN_C

// errors and the process
WINBASEAPI DWORD __stdcall GetLastError(void);
WINBASEAPI void __stdcall SetLastError(DWORD dwErr);
WINBASEAPI DWORD __stdcall FormatMessageA(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId,
                                          LPSTR lpBuffer, DWORD nSize, va_list *Arguments);
WINBASEAPI __attribute__((noreturn)) void __stdcall ExitProcess(UINT uExitCode);
WINBASEAPI BOOL __stdcall TerminateProcess(HANDLE hProcess, UINT uExitCode);
WINBASEAPI HANDLE __stdcall GetCurrentProcess(void);
WINBASEAPI DWORD __stdcall GetCurrentProcessId(void);
WINBASEAPI HANDLE __stdcall GetCurrentThread(void);
WINBASEAPI DWORD __stdcall GetCurrentThreadId(void);
WINBASEAPI LPSTR __stdcall GetCommandLineA(void);
WINBASEAPI void __stdcall GetStartupInfoA(LPSTARTUPINFOA lpStartupInfo);
WINBASEAPI DWORD __stdcall GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize);
WINBASEAPI BOOL __stdcall SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue);
WINBASEAPI UINT __stdcall SetErrorMode(UINT uMode);
WINBASEAPI BOOL __stdcall CreateProcessA(LPCSTR lpApplicationName, LPSTR lpCommandLine,
                                         LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                         LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
                                         DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
                                         LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);
WINBASEAPI HANDLE __stdcall OpenProcess(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId);
WINBASEAPI BOOL __stdcall GetProcessTimes(HANDLE hProcess, LPFILETIME lpCreationTime, LPFILETIME lpExitTime,
                                          LPFILETIME lpKernelTime, LPFILETIME lpUserTime);
WINBASEAPI BOOL __stdcall GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode);
WINBASEAPI void __stdcall GetSystemInfo(LPSYSTEM_INFO lpSystemInfo);
WINBASEAPI void __stdcall GlobalMemoryStatus(LPMEMORYSTATUS lpBuffer);
WINBASEAPI BOOL __stdcall GetComputerNameA(LPSTR lpBuffer, LPDWORD nSize);
OC_EXTERN_C BOOL __stdcall GetUserNameA(LPSTR lpBuffer, LPDWORD pcbBuffer);
WINBASEAPI void __stdcall RaiseException(DWORD dwExceptionCode, DWORD dwExceptionFlags, DWORD nNumberOfArguments,
                                         const ULONG_PTR *lpArguments);
WINBASEAPI LPTOP_LEVEL_EXCEPTION_FILTER __stdcall SetUnhandledExceptionFilter(
    LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);
WINBASEAPI PVOID __stdcall AddVectoredExceptionHandler(ULONG First, PVECTORED_EXCEPTION_HANDLER Handler);
WINBASEAPI void __stdcall OutputDebugStringA(LPCSTR lpOutputString);
WINBASEAPI BOOL __stdcall IsDebuggerPresent(void);

// modules and resources (a module loaded as a data file is read by port_posix.cpp's PE
// reader; Hyperion's own resources are linked in, hyperion/resources.cpp)
WINBASEAPI HMODULE __stdcall GetModuleHandleA(LPCSTR lpModuleName);
WINBASEAPI HMODULE __stdcall GetModuleHandleW(LPCWSTR lpModuleName);
WINBASEAPI BOOL __stdcall GetModuleHandleExA(DWORD dwFlags, LPCSTR lpModuleName, HMODULE *phModule);
WINBASEAPI DWORD __stdcall GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize);
WINBASEAPI HMODULE __stdcall LoadLibraryA(LPCSTR lpLibFileName);
WINBASEAPI HMODULE __stdcall LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
WINBASEAPI BOOL __stdcall FreeLibrary(HMODULE hLibModule);
WINBASEAPI FARPROC __stdcall GetProcAddress(HMODULE hModule, LPCSTR lpProcName);
WINBASEAPI HRSRC __stdcall FindResourceA(HMODULE hModule, LPCSTR lpName, LPCSTR lpType);
WINBASEAPI HGLOBAL __stdcall LoadResource(HMODULE hModule, HRSRC hResInfo);
WINBASEAPI LPVOID __stdcall LockResource(HGLOBAL hResData);
WINBASEAPI DWORD __stdcall SizeofResource(HMODULE hModule, HRSRC hResInfo);
WINUSERAPI int __stdcall LoadStringA(HINSTANCE hInstance, UINT uID, LPSTR lpBuffer, int cchBufferMax);
OC_EXTERN_C DWORD __stdcall GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle);
OC_EXTERN_C BOOL __stdcall GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
OC_EXTERN_C BOOL __stdcall VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID *lplpBuffer, PUINT puLen);

// memory
WINBASEAPI LPVOID __stdcall VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect);
WINBASEAPI BOOL __stdcall VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType);
WINBASEAPI BOOL __stdcall VirtualProtect(LPVOID lpAddress, SIZE_T dwSize, DWORD flNewProtect, PDWORD lpflOldProtect);
WINBASEAPI SIZE_T __stdcall VirtualQuery(LPCVOID lpAddress, PMEMORY_BASIC_INFORMATION lpBuffer, SIZE_T dwLength);
WINBASEAPI HLOCAL __stdcall LocalAlloc(UINT uFlags, SIZE_T uBytes);
WINBASEAPI HLOCAL __stdcall LocalFree(HLOCAL hMem);
WINBASEAPI HGLOBAL __stdcall GlobalAlloc(UINT uFlags, SIZE_T dwBytes);
WINBASEAPI HGLOBAL __stdcall GlobalFree(HGLOBAL hMem);
WINBASEAPI HANDLE __stdcall CreateFileMappingA(HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttributes, DWORD flProtect,
                                               DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCSTR lpName);
WINBASEAPI LPVOID __stdcall MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh,
                                          DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap);
WINBASEAPI BOOL __stdcall UnmapViewOfFile(LPCVOID lpBaseAddress);

// handles, threads, events, locks
WINBASEAPI BOOL __stdcall CloseHandle(HANDLE hObject);
WINBASEAPI BOOL __stdcall DuplicateHandle(HANDLE hSourceProcessHandle, HANDLE hSourceHandle,
                                          HANDLE hTargetProcessHandle, LPHANDLE lpTargetHandle,
                                          DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwOptions);
WINBASEAPI HANDLE __stdcall CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize,
                                         LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter,
                                         DWORD dwCreationFlags, LPDWORD lpThreadId);
WINBASEAPI DWORD __stdcall SuspendThread(HANDLE hThread);
WINBASEAPI DWORD __stdcall ResumeThread(HANDLE hThread);
WINBASEAPI BOOL __stdcall GetThreadContext(HANDLE hThread, PCONTEXT lpContext);
WINBASEAPI BOOL __stdcall SetThreadPriority(HANDLE hThread, int nPriority);
WINBASEAPI BOOL __stdcall GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode);
WINBASEAPI HANDLE __stdcall CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset,
                                         BOOL bInitialState, LPCSTR lpName);
WINBASEAPI BOOL __stdcall SetEvent(HANDLE hEvent);
WINBASEAPI BOOL __stdcall ResetEvent(HANDLE hEvent);
WINBASEAPI DWORD __stdcall WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
WINBASEAPI DWORD __stdcall WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll,
                                                  DWORD dwMilliseconds);
WINBASEAPI void __stdcall InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
WINBASEAPI void __stdcall DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
WINBASEAPI void __stdcall EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
WINBASEAPI void __stdcall LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
WINBASEAPI BOOL __stdcall TryEnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
WINBASEAPI void __stdcall AcquireSRWLockExclusive(PSRWLOCK SRWLock);
WINBASEAPI void __stdcall ReleaseSRWLockExclusive(PSRWLOCK SRWLock);
WINBASEAPI LONG __stdcall InterlockedIncrement(LONG volatile *Addend);
WINBASEAPI LONG __stdcall InterlockedDecrement(LONG volatile *Addend);
WINBASEAPI LONG __stdcall InterlockedExchange(LONG volatile *Target, LONG Value);
WINBASEAPI LONG __stdcall InterlockedExchangeAdd(LONG volatile *Addend, LONG Value);
WINBASEAPI LONG __stdcall InterlockedCompareExchange(LONG volatile *Destination, LONG Exchange, LONG Comparand);
WINBASEAPI void __stdcall Sleep(DWORD dwMilliseconds);

// time
WINBASEAPI DWORD __stdcall GetTickCount(void);
WINBASEAPI ULONGLONG __stdcall GetTickCount64(void);
OC_EXTERN_C DWORD __stdcall timeGetTime(void);
WINBASEAPI BOOL __stdcall QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount);
WINBASEAPI BOOL __stdcall QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency);
WINBASEAPI void __stdcall GetLocalTime(LPSYSTEMTIME lpSystemTime);
WINBASEAPI void __stdcall GetSystemTime(LPSYSTEMTIME lpSystemTime);
WINBASEAPI void __stdcall GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime);
WINBASEAPI BOOL __stdcall FileTimeToLocalFileTime(const FILETIME *lpFileTime, LPFILETIME lpLocalFileTime);
WINBASEAPI BOOL __stdcall FileTimeToSystemTime(const FILETIME *lpFileTime, LPSYSTEMTIME lpSystemTime);
WINBASEAPI BOOL __stdcall SystemTimeToFileTime(const SYSTEMTIME *lpSystemTime, LPFILETIME lpFileTime);
WINBASEAPI LONG __stdcall CompareFileTime(const FILETIME *lpFileTime1, const FILETIME *lpFileTime2);
WINBASEAPI DWORD __stdcall GetTimeZoneInformation(LPTIME_ZONE_INFORMATION lpTimeZoneInformation);
// multimedia timers (port_smacker.cpp)
typedef void(__stdcall *LPTIMECALLBACK)(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2);
#define TIME_ONESHOT 0x0000
#define TIME_PERIODIC 0x0001
#define TIME_CALLBACK_FUNCTION 0x0000
#define TIMERR_NOERROR 0
OC_EXTERN_C UINT __stdcall timeSetEvent(UINT uDelay, UINT uResolution, LPTIMECALLBACK fptc, DWORD_PTR dwUser,
                                        UINT fuEvent);
OC_EXTERN_C UINT __stdcall timeKillEvent(UINT uTimerID);
OC_EXTERN_C UINT __stdcall timeBeginPeriod(UINT uPeriod);
OC_EXTERN_C UINT __stdcall timeEndPeriod(UINT uPeriod);

// files and folders
WINBASEAPI HANDLE __stdcall CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                        LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                        DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
WINBASEAPI BOOL __stdcall ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
                                   LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
WINBASEAPI BOOL __stdcall WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
                                    LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
WINBASEAPI BOOL __stdcall FlushFileBuffers(HANDLE hFile);
WINBASEAPI DWORD __stdcall GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
WINBASEAPI BOOL __stdcall GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize);
WINBASEAPI DWORD __stdcall SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh,
                                          DWORD dwMoveMethod);
WINBASEAPI BOOL __stdcall SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove,
                                           PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod);
WINBASEAPI BOOL __stdcall SetEndOfFile(HANDLE hFile);
WINBASEAPI BOOL __stdcall GetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime,
                                      LPFILETIME lpLastWriteTime);
WINBASEAPI BOOL __stdcall SetFileTime(HANDLE hFile, const FILETIME *lpCreationTime,
                                      const FILETIME *lpLastAccessTime, const FILETIME *lpLastWriteTime);
WINBASEAPI DWORD __stdcall GetFileAttributesA(LPCSTR lpFileName);
WINBASEAPI BOOL __stdcall GetFileAttributesExA(LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId,
                                               LPVOID lpFileInformation);
WINBASEAPI BOOL __stdcall SetFileAttributesA(LPCSTR lpFileName, DWORD dwFileAttributes);
WINBASEAPI BOOL __stdcall DeleteFileA(LPCSTR lpFileName);
WINBASEAPI BOOL __stdcall CopyFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, BOOL bFailIfExists);
WINBASEAPI BOOL __stdcall MoveFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName);
WINBASEAPI BOOL __stdcall MoveFileExA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, DWORD dwFlags);
WINBASEAPI BOOL __stdcall CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
WINBASEAPI BOOL __stdcall RemoveDirectoryA(LPCSTR lpPathName);
WINBASEAPI HANDLE __stdcall FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData);
WINBASEAPI BOOL __stdcall FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData);
WINBASEAPI BOOL __stdcall FindClose(HANDLE hFindFile);
WINBASEAPI DWORD __stdcall GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer,
                                            LPSTR *lpFilePart);
WINBASEAPI DWORD __stdcall GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer);
WINBASEAPI BOOL __stdcall SetCurrentDirectoryA(LPCSTR lpPathName);
WINBASEAPI DWORD __stdcall GetTempPathA(DWORD nBufferLength, LPSTR lpBuffer);
WINBASEAPI UINT __stdcall GetWindowsDirectoryA(LPSTR lpBuffer, UINT uSize);
WINBASEAPI UINT __stdcall GetDriveTypeA(LPCSTR lpRootPathName);
WINBASEAPI DWORD __stdcall GetLogicalDriveStringsA(DWORD nBufferLength, LPSTR lpBuffer);
WINBASEAPI BOOL __stdcall GetDiskFreeSpaceA(LPCSTR lpRootPathName, LPDWORD lpSectorsPerCluster,
                                            LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters,
                                            LPDWORD lpTotalNumberOfClusters);
WINBASEAPI BOOL __stdcall GetVolumeInformationA(LPCSTR lpRootPathName, LPSTR lpVolumeNameBuffer,
                                                DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber,
                                                LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags,
                                                LPSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize);
OC_EXTERN_C HRESULT __stdcall SHGetFolderPathA(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPSTR pszPath);
OC_EXTERN_C int __stdcall SHCreateDirectoryExA(HWND hwnd, LPCSTR pszPath, const SECURITY_ATTRIBUTES *psa);
OC_EXTERN_C HINSTANCE __stdcall ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters,
                                              LPCSTR lpDirectory, INT nShowCmd);

// strings
WINBASEAPI int __stdcall lstrlenA(LPCSTR lpString);
WINBASEAPI LPSTR __stdcall lstrcpyA(LPSTR lpString1, LPCSTR lpString2);
WINBASEAPI LPSTR __stdcall lstrcpynA(LPSTR lpString1, LPCSTR lpString2, int iMaxLength);
WINBASEAPI LPSTR __stdcall lstrcatA(LPSTR lpString1, LPCSTR lpString2);
WINBASEAPI int __stdcall lstrcmpA(LPCSTR lpString1, LPCSTR lpString2);
WINBASEAPI int __stdcall lstrcmpiA(LPCSTR lpString1, LPCSTR lpString2);
WINUSERAPI LPSTR __stdcall CharLowerA(LPSTR lpsz);
WINUSERAPI LPSTR __stdcall CharUpperA(LPSTR lpsz);
WINUSERAPI LPSTR __stdcall CharNextA(LPCSTR lpsz);
WINUSERAPI int __cdecl wsprintfA(LPSTR, LPCSTR, ...);
WINUSERAPI int __stdcall wvsprintfA(LPSTR, LPCSTR, va_list arglist);
WINBASEAPI BOOL __stdcall IsDBCSLeadByte(BYTE TestChar);
WINBASEAPI int __stdcall MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte,
                                             LPWSTR lpWideCharStr, int cchWideChar);
WINBASEAPI int __stdcall WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar,
                                             LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar,
                                             LPBOOL lpUsedDefaultChar);
WINBASEAPI LANGID __stdcall GetUserDefaultLangID(void);
WINBASEAPI LANGID __stdcall GetSystemDefaultLangID(void);
WINBASEAPI int __stdcall MulDiv(int nNumber, int nNumerator, int nDenominator);

// rectangles
WINUSERAPI BOOL __stdcall SetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom);
WINUSERAPI BOOL __stdcall SetRectEmpty(LPRECT lprc);
WINUSERAPI BOOL __stdcall PtInRect(const RECT *lprc, POINT pt);
WINUSERAPI BOOL __stdcall OffsetRect(LPRECT lprc, int dx, int dy);
WINUSERAPI BOOL __stdcall IntersectRect(LPRECT lprcDst, const RECT *lprcSrc1, const RECT *lprcSrc2);
WINUSERAPI BOOL __stdcall IsRectEmpty(const RECT *lprc);

// the user's windows, the game's and others' (port_posix.cpp answers for the game's,
// through SDL; other programs' windows are never found off Windows)
WINUSERAPI int __stdcall MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);
WINUSERAPI BOOL __stdcall PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
WINUSERAPI LRESULT __stdcall SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
WINUSERAPI HWND __stdcall FindWindowA(LPCSTR lpClassName, LPCSTR lpWindowName);
WINUSERAPI HWND __stdcall FindWindowExA(HWND hWndParent, HWND hWndChildAfter, LPCSTR lpszClass, LPCSTR lpszWindow);
WINUSERAPI BOOL __stdcall EnumWindows(WNDENUMPROC lpEnumFunc, LPARAM lParam);
WINUSERAPI HWND __stdcall GetWindow(HWND hWnd, UINT uCmd);
WINUSERAPI HWND __stdcall GetTopWindow(HWND hWnd);
WINUSERAPI HWND __stdcall GetParent(HWND hWnd);
WINUSERAPI HWND __stdcall GetDesktopWindow(void);
WINUSERAPI HWND __stdcall GetForegroundWindow(void);
WINUSERAPI HWND __stdcall GetActiveWindow(void);
WINUSERAPI HWND __stdcall GetLastActivePopup(HWND hWnd);
WINUSERAPI BOOL __stdcall SetForegroundWindow(HWND hWnd);
WINUSERAPI HWND __stdcall SetFocus(HWND hWnd);
WINUSERAPI BOOL __stdcall ShowWindow(HWND hWnd, int nCmdShow);
WINUSERAPI BOOL __stdcall IsIconic(HWND hWnd);
WINUSERAPI BOOL __stdcall IsWindow(HWND hWnd);
WINUSERAPI BOOL __stdcall IsWindowVisible(HWND hWnd);
WINUSERAPI DWORD __stdcall GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId);
WINUSERAPI int __stdcall GetClassNameA(HWND hWnd, LPSTR lpClassName, int nMaxCount);
WINUSERAPI BOOL __stdcall GetClientRect(HWND hWnd, LPRECT lpRect);
WINUSERAPI BOOL __stdcall GetWindowRect(HWND hWnd, LPRECT lpRect);
WINUSERAPI BOOL __stdcall SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
WINUSERAPI BOOL __stdcall AdjustWindowRect(LPRECT lpRect, DWORD dwStyle, BOOL bMenu);
WINUSERAPI LONG_PTR __stdcall GetWindowLongPtrW(HWND hWnd, int nIndex);
WINUSERAPI LONG_PTR __stdcall SetWindowLongPtrW(HWND hWnd, int nIndex, LONG_PTR dwNewLong);
WINUSERAPI BOOL __stdcall ClientToScreen(HWND hWnd, LPPOINT lpPoint);
WINUSERAPI BOOL __stdcall ScreenToClient(HWND hWnd, LPPOINT lpPoint);
WINUSERAPI BOOL __stdcall UpdateWindow(HWND hWnd);
WINUSERAPI BOOL __stdcall InvalidateRect(HWND hWnd, const RECT *lpRect, BOOL bErase);
WINUSERAPI int __stdcall GetSystemMetrics(int nIndex);
OC_EXTERN_C DWORD __stdcall WaitForInputIdle(HANDLE hProcess, DWORD dwMilliseconds);
// the cursor and the keyboard
WINUSERAPI HCURSOR __stdcall LoadCursorA(HINSTANCE hInstance, LPCSTR lpCursorName);
#define LoadCursor LoadCursorA
WINUSERAPI HCURSOR __stdcall SetCursor(HCURSOR hCursor);
WINUSERAPI int __stdcall ShowCursor(BOOL bShow);
WINUSERAPI BOOL __stdcall GetCursorPos(LPPOINT lpPoint);
WINUSERAPI BOOL __stdcall SetCursorPos(int X, int Y);
WINUSERAPI BOOL __stdcall ClipCursor(const RECT *lpRect);
WINUSERAPI SHORT __stdcall GetAsyncKeyState(int vKey);
WINUSERAPI SHORT __stdcall GetKeyState(int nVirtKey);
WINUSERAPI HANDLE __stdcall LoadImageA(HINSTANCE hInst, LPCSTR name, UINT type, int cx, int cy, UINT fuLoad);
WINUSERAPI HICON __stdcall LoadIconA(HINSTANCE hInstance, LPCSTR lpIconName);
// dialogs: 1.17's few (init.cpp's disk prompt, gds_appfat.cpp's error box) are message
// boxes off Windows
WINUSERAPI INT_PTR __stdcall DialogBoxParamA(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWndParent,
                                             DLGPROC lpDialogFunc, LPARAM dwInitParam);
WINUSERAPI BOOL __stdcall EndDialog(HWND hDlg, INT_PTR nResult);
WINUSERAPI BOOL __stdcall SetDlgItemTextA(HWND hDlg, int nIDDlgItem, LPCSTR lpString);

// GDI: no device off Windows; every call fails, as with no display. The game declares the
// drawing calls it makes itself (gds_font.cpp, gluBNProfile.h).
OC_EXTERN_C int __stdcall DrawTextA(void *hdc, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format);
OC_EXTERN_C HDC __stdcall GetDC(HWND hWnd);
OC_EXTERN_C int __stdcall GetDeviceCaps(void *hdc, int nIndex);  // as gds_font.cpp declares it
OC_EXTERN_C int __stdcall ReleaseDC(HWND hWnd, HDC hDC);

// the registry: no keys
OC_EXTERN_C LONG __stdcall RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired,
                                         PHKEY phkResult);
OC_EXTERN_C LONG __stdcall RegCloseKey(HKEY hKey);
OC_EXTERN_C LONG __stdcall RegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType,
                                            LPBYTE lpData, LPDWORD lpcbData);
OC_EXTERN_C LONG __stdcall RegDeleteValueA(HKEY hKey, LPCSTR lpValueName);
OC_EXTERN_C LONG __stdcall RegSetValueExA(HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType,
                                          const BYTE *lpData, DWORD cbData);
OC_EXTERN_C LONG __stdcall RegCreateKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD Reserved, LPSTR lpClass,
                                           DWORD dwOptions, REGSAM samDesired,
                                           LPSECURITY_ATTRIBUTES lpSecurityAttributes, PHKEY phkResult,
                                           LPDWORD lpdwDisposition);

// security: nothing to set off Windows
OC_EXTERN_C BOOL __stdcall OpenProcessToken(HANDLE ProcessHandle, DWORD DesiredAccess, PHANDLE TokenHandle);
OC_EXTERN_C BOOL __stdcall GetTokenInformation(HANDLE TokenHandle, TOKEN_INFORMATION_CLASS TokenInformationClass,
                                               LPVOID TokenInformation, DWORD TokenInformationLength,
                                               PDWORD ReturnLength);
OC_EXTERN_C BOOL __stdcall AllocateAndInitializeSid(PSID_IDENTIFIER_AUTHORITY pIdentifierAuthority,
                                                    BYTE nSubAuthorityCount, DWORD nSubAuthority0,
                                                    DWORD nSubAuthority1, DWORD nSubAuthority2,
                                                    DWORD nSubAuthority3, DWORD nSubAuthority4,
                                                    DWORD nSubAuthority5, DWORD nSubAuthority6,
                                                    DWORD nSubAuthority7, PSID *pSid);
OC_EXTERN_C PVOID __stdcall FreeSid(PSID pSid);
OC_EXTERN_C BOOL __stdcall InitializeAcl(PACL pAcl, DWORD nAclLength, DWORD dwAclRevision);
OC_EXTERN_C BOOL __stdcall AddAccessAllowedAce(PACL pAcl, DWORD dwAceRevision, DWORD AccessMask, PSID pSid);
OC_EXTERN_C BOOL __stdcall AddAccessDeniedAce(PACL pAcl, DWORD dwAceRevision, DWORD AccessMask, PSID pSid);
OC_EXTERN_C DWORD __stdcall GetLengthSid(PSID pSid);

// ---------------------------------------------------------------------------
// the C runtime's Microsoft names (port_posix.cpp). Real functions, not macros, where a
// TU of the game declares the name itself.
// ---------------------------------------------------------------------------
#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif
#define _MAX_PATH MAX_PATH
#define _MAX_FNAME 256
#define _MAX_EXT 256
#define _MAX_DIR 256
#define _MAX_DRIVE 3
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define _strdup strdup
#define _unlink unlink
#define _getcwd getcwd
#define _fileno fileno
#define _fseeki64 fseeko
#define _ftelli64 ftello
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
typedef long long __time64_t;  // __int64, as the game's own declarations spell it
OC_EXTERN_C __time64_t __cdecl _time64(__time64_t *pt);
OC_EXTERN_C struct tm *__cdecl _gmtime64(const __time64_t *pt);
OC_EXTERN_C char *__cdecl _ctime64(const __time64_t *pt);
OC_EXTERN_C errno_t __cdecl _localtime64_s(struct tm *ptm, const __time64_t *pt);
OC_EXTERN_C char *__cdecl _strupr(char *s);
OC_EXTERN_C char *__cdecl _strlwr(char *s);
OC_EXTERN_C int __cdecl _wcsicmp(const wchar_t *a, const wchar_t *b);
OC_EXTERN_C errno_t __cdecl fopen_s(FILE **pf, const char *name, const char *mode);
OC_EXTERN_C int __cdecl sscanf_s(const char *buf, const char *fmt, ...);
OC_EXTERN_C errno_t __cdecl strcpy_s(char *dst, size_t n, const char *src);
OC_EXTERN_C errno_t __cdecl strncpy_s(char *dst, size_t n, const char *src, size_t count);
OC_EXTERN_C errno_t __cdecl strcat_s(char *dst, size_t n, const char *src);
OC_EXTERN_C int __cdecl sprintf_s(char *buf, size_t n, const char *fmt, ...);
OC_EXTERN_C int __cdecl vsprintf_s(char *buf, size_t n, const char *fmt, va_list va);
OC_EXTERN_C int __cdecl _snprintf_s(char *buf, size_t n, size_t count, const char *fmt, ...);
OC_EXTERN_C int __cdecl _vsnprintf_s(char *buf, size_t n, size_t count, const char *fmt, va_list va);
#ifdef __cplusplus
// The forms that take the array's size from its type.
template <size_t N> inline errno_t strcpy_s(char (&dst)[N], const char *src)
{
  return strcpy_s(dst, N, src);
}
template <size_t N> inline errno_t strcat_s(char (&dst)[N], const char *src)
{
  return strcat_s(dst, N, src);
}
template <size_t N> inline errno_t strncpy_s(char (&dst)[N], const char *src, size_t count)
{
  return strncpy_s(dst, N, src, count);
}
template <size_t N> inline int sprintf_s(char (&buf)[N], const char *fmt, ...)
{
  va_list va;
  va_start(va, fmt);
  int n = vsprintf_s(buf, N, fmt, va);
  va_end(va);
  return n;
}
template <size_t N> inline int vsprintf_s(char (&buf)[N], const char *fmt, va_list va)
{
  return vsprintf_s(buf, N, fmt, va);
}
template <size_t N> inline int _snprintf_s(char (&buf)[N], size_t count, const char *fmt, ...)
{
  va_list va;
  va_start(va, fmt);
  int n = _vsnprintf_s(buf, N, count, fmt, va);
  va_end(va);
  return n;
}
template <size_t N> inline int _vsnprintf_s(char (&buf)[N], size_t count, const char *fmt, va_list va)
{
  return _vsnprintf_s(buf, N, count, fmt, va);
}
#endif

#ifdef __cplusplus
// The C library's FILE functions taking void *, as a few of the game's TUs pass the
// handle (gds_file.cpp, main.cpp), which with MSVC declare the functions so themselves.
static inline int fclose(void *pFile)
{
  return fclose(static_cast<FILE *>(pFile));
}
static inline int fgetc(void *pFile)
{
  return fgetc(static_cast<FILE *>(pFile));
}
static inline int fputc(int ch, void *pFile)
{
  return fputc(ch, static_cast<FILE *>(pFile));
}
static inline size_t fread(void *pBuf, size_t nSize, size_t nCount, void *pFile)
{
  return fread(pBuf, nSize, nCount, static_cast<FILE *>(pFile));
}
static inline size_t fwrite(const void *pBuf, size_t nSize, size_t nCount, void *pFile)
{
  return fwrite(pBuf, nSize, nCount, static_cast<FILE *>(pFile));
}
static inline int fseek(void *pFile, long nOffset, int nOrigin)
{
  return fseek(static_cast<FILE *>(pFile), nOffset, nOrigin);
}
static inline int setvbuf(void *pFile, char *pBuf, int nMode, size_t nSize)
{
  return setvbuf(static_cast<FILE *>(pFile), pBuf, nMode, nSize);
}
#endif

