// port_stormbase.cpp -- Storm's memory, string and registry functions, the port's own
//. They keep Storm's names, ordinals' shapes and
// __stdcall, so the game's calls reach them unchanged in place of storm.dll's.
// Each does what storm.dll's does, read from its code,
// and tools/porttest/storm_test.cpp runs the player's storm.dll and these on the
// same inputs and compares them. The registry functions are the exception: they keep
// to Hyperion's settings file in place of the registry (port_settings.cpp).
//
// An error here is set through SErrSetLastError, port_stormerr.cpp's, as Storm's
// functions set theirs.
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#include <malloc/malloc.h>  // malloc_size
#endif
#include "../platform/port_settings.h"

#include "../replays/port_snapprobe.h"  // the trainer's memory probe
#include "port_stormbase.h"
#include "../replays/port_snap.h"  // PortSnapAlloc, PortSnapFree
#include "port_stormapi.h"

// ---------------------------------------------------------------------------
// Memory. A block is the CRT heap's, with a header that marks it as the port's and
// keeps its size. Storm's allocator zeroes a block only for flag 8; this one
// zeroes every block and every grown tail, so nothing the game reads depends
// on what the heap held.
//
// The header comes before the block, except on a Mac: there the C++ library is the
// system's, and its own code frees with free() blocks the game's operator new gave (Storm.cpp),
// so a block is malloc's own pointer and its header is in the last bytes malloc gave it.

static const DWORD SMEM_ZERO = 0x08;     // SMemAlloc, SMemReAlloc: zero the new bytes
static const DWORD SMEM_NO_MOVE = 0x10;  // SMemReAlloc: resize in place or fail
static const DWORD MEM_MAGIC = 0x4D454D48;  // "HMEM"

// 16 bytes on x86 and 32 on x64, so a block keeps malloc's 16-byte alignment: GCC and Clang
// take operator new's blocks to be 16-byte aligned and store to them with aligned SSE moves.
struct alignas(16) MemHeader {
  DWORD dwMagic;
  DWORD dwCheck;  // dwMagic ^ the block's address, so a stray match is not taken for a header
  size_t cbSize;
  size_t cbPad;
};

static bool MemGuard(void);

// Where a block's header is, whoever's the block is.
static MemHeader *MemHeaderAt(const void *pv)
{
#if defined(__APPLE__)
  if (!MemGuard())
    return reinterpret_cast<MemHeader *>(static_cast<BYTE *>(const_cast<void *>(pv)) + malloc_size(pv) -
                                         sizeof(MemHeader));
#endif
  return reinterpret_cast<MemHeader *>(const_cast<void *>(pv)) - 1;
}

// A block's header, or NULL for a block that is not the port's. A block of Storm's may
// start near the beginning of its heap's pages, so the read is guarded.
static MemHeader *MemHeaderOf(const void *pv)
{
#if defined(__APPLE__)
  if (!MemGuard() && malloc_size(pv) < sizeof(MemHeader))
    return NULL;  // not the start of one of malloc's blocks
#endif
  MemHeader *pHeader = MemHeaderAt(pv);
  bool bOurs = false;
#if defined(_WIN32)
  __try {
    bOurs = pHeader->dwMagic == MEM_MAGIC &&
            pHeader->dwCheck == (MEM_MAGIC ^ static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(pv)));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    bOurs = false;
  }
#else
  // no Storm off Windows: every block is the port's
  bOurs = pHeader->dwMagic == MEM_MAGIC &&
          pHeader->dwCheck == (MEM_MAGIC ^ static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(pv)));
#endif
  return bOurs ? pHeader : NULL;
}

// A block Storm allocated and the game hands back: the one kind this file
// cannot free itself, so it goes to Storm's own SMemFree and SMemReAlloc
// (ordinals 403 and 405) while any Storm function still allocates for the game.
typedef BOOL(__stdcall *PFN_SMEMFREE)(LPCVOID, LPCSTR, int, DWORD);
typedef void *(__stdcall *PFN_SMEMREALLOC)(void *, DWORD, LPCSTR, int, DWORD);

static FARPROC StormOrdinal(WORD wOrdinal)
{
  HMODULE hStorm = GetModuleHandleA("storm.dll");
  return hStorm ? GetProcAddress(hStorm, MAKEINTRESOURCEA(wOrdinal)) : NULL;
}

// Storm ends the process when it cannot allocate, and no caller checks.
static void MemFail(void)
{
  MessageBoxA(NULL, "Out of memory.", "Hyperion", MB_OK | MB_ICONERROR);
  ExitProcess(1);
}

// HYPERION_GUARD_HEAP=1, for finding a write past a block: each block gets pages
// of its own and ends where a page no one may touch begins, and a freed block's
// pages stay reserved and untouchable. Slow and wasteful, for debugging only.
static const size_t MEM_GUARDED = 0x47554152;  // cbPad of a guarded block

static bool MemGuard(void)
{
  static int s_nGuard = -1;
  if (s_nGuard < 0) {
    char v[8];
    DWORD n = GetEnvironmentVariableA("HYPERION_GUARD_HEAP", v, sizeof v);
    s_nGuard = n > 0 && n < sizeof v && v[0] == '1';
  }
  return s_nGuard == 1;
}

// A new block of cbSize bytes, its header's place reserved.
static void *MemRaw(size_t cbSize)
{
  if (!MemGuard()) {
#if defined(__APPLE__)
    void *pv = malloc(cbSize + sizeof(MemHeader));
    if (pv)
      MemHeaderAt(pv)->cbPad = 0;
    return pv;
#else
    MemHeader *pHeader = static_cast<MemHeader *>(malloc(sizeof(MemHeader) + cbSize));
    if (pHeader)
      pHeader->cbPad = 0;
    return pHeader ? pHeader + 1 : NULL;
#endif
  }
  // to the header's alignment, so a block ends up to 15 bytes short of the guard page
  const size_t cbAll = (sizeof(MemHeader) + cbSize + alignof(MemHeader) - 1) & ~(alignof(MemHeader) - 1);
  const size_t cbPages = (cbAll + 0xFFF) & ~static_cast<size_t>(0xFFF);
  BYTE *p = static_cast<BYTE *>(VirtualAlloc(NULL, cbPages + 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  DWORD dwOld;
  if (!p || !VirtualProtect(p + cbPages, 0x1000, PAGE_NOACCESS, &dwOld))
    return NULL;
  MemHeader *pHeader = reinterpret_cast<MemHeader *>(p + cbPages - cbAll);
  pHeader->cbPad = MEM_GUARDED;
  return pHeader + 1;
}

static void MemRelease(void *pv)
{
  MemHeader *pHeader = MemHeaderAt(pv);
  pHeader->dwMagic = 0;
  if (pHeader->cbPad != MEM_GUARDED) {
#if defined(__APPLE__)
    free(pv);
#else
    free(pHeader);
#endif
    return;
  }
  BYTE *p = reinterpret_cast<BYTE *>(reinterpret_cast<ULONG_PTR>(pHeader) & ~static_cast<ULONG_PTR>(0xFFF));
  VirtualFree(p, 0, MEM_DECOMMIT);  // the whole allocation, the guard page with it
}

static void *MemBlock(size_t cbSize)
{
  if (cbSize > 0x7FFFFFFF)
    MemFail();
  void *pv = MemRaw(cbSize);
  if (!pv)
    MemFail();
  MemHeader *pHeader = MemHeaderAt(pv);
  pHeader->dwMagic = MEM_MAGIC;
  pHeader->dwCheck = MEM_MAGIC ^ static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(pv));
  pHeader->cbSize = cbSize;
  memset(pv, 0, cbSize);
  return pv;
}

// The size a block of the port's was allocated with, 0 for one that is not the port's
// (PortGroupNative, which is handed a loaded file as a block and not its size).
size_t __cdecl PortMemSize(const void *pMem)
{
  const MemHeader *pHeader = pMem ? MemHeaderOf(pMem) : NULL;
  return pHeader ? pHeader->cbSize : 0;
}

extern "C" {

// Ordinal 401. The tag and line name the owner in Storm's heap; this one has none.
LPVOID __stdcall SMemAlloc(DWORD size, LPCSTR ownerTag, int ownerLine, DWORD flags)
{
  (void)flags;
  void *pv = MemBlock(size);
  PortSnapProbeAlloc(pv, size, ownerTag, ownerLine);  // the trainer's probe (port_snapprobe.cpp)
  PortSnapAlloc(pv, size, ownerTag, ownerLine);       // and its snapshots (port_snap.cpp)
  return pv;
}

// Ordinal 403. TRUE once the block is freed. A null pointer, which comp_free
// passes when compress failed early, is FALSE and Storm's error 0x85100081.
BOOL __stdcall SMemFree(LPCVOID ptr, LPCSTR ownerTag, int ownerLine, DWORD flags)
{
  if (!ptr) {
    SErrSetLastError(0x85100081);
    return FALSE;
  }
  MemHeader *pHeader = MemHeaderOf(ptr);
  if (pHeader) {
    PortSnapProbeFree(ptr);
    PortSnapFree(ptr);
  }
  if (!pHeader) {
#if defined(__APPLE__)
    // a block of the system's C++ library, which the game's operator delete is handed
    if (malloc_size(ptr) != 0) {
      free(const_cast<void *>(ptr));
      return TRUE;
    }
#endif
    PFN_SMEMFREE pfnFree = reinterpret_cast<PFN_SMEMFREE>(StormOrdinal(403));
    return pfnFree ? pfnFree(ptr, ownerTag, ownerLine, flags) : FALSE;
  }
  MemRelease(const_cast<void *>(ptr));
  return TRUE;
}

// Ordinal 405. A null pointer is a new block. With SMEM_NO_MOVE the block keeps
// its place or the answer is NULL and the block is left as it was, which is
// how the map list's table asks (gluMap.cpp); it then moves its entries itself.
void *__stdcall SMemReAlloc(void *pMem, DWORD cbSize, const char *pszTag, int nLine, DWORD dwFlags)
{
  if (!pMem)
    return MemBlock(cbSize);
  MemHeader *pHeader = MemHeaderOf(pMem);
  if (!pHeader) {
    PFN_SMEMREALLOC pfnReAlloc = reinterpret_cast<PFN_SMEMREALLOC>(StormOrdinal(405));
    return pfnReAlloc ? pfnReAlloc(pMem, cbSize, pszTag, nLine, dwFlags) : NULL;
  }
  if (cbSize <= pHeader->cbSize) {
    pHeader->cbSize = cbSize;
    PortSnapFree(pMem);
    PortSnapAlloc(pMem, cbSize, pszTag, nLine);
    return pMem;
  }
  if (dwFlags & SMEM_NO_MOVE)
    return NULL;
  void *pNew = MemBlock(cbSize);
  memcpy(pNew, pMem, pHeader->cbSize);
  PortSnapProbeFree(pMem);
  PortSnapProbeAlloc(pNew, cbSize, pszTag, nLine);
  PortSnapFree(pMem);
  PortSnapAlloc(pNew, cbSize, pszTag, nLine);
  MemRelease(pMem);
  return pNew;
}

// Ordinal 491: the CRT's memcpy in Storm, which moves overlapping bytes as
// memmove does.
void __stdcall SMemCopy(void *pDest, const void *pSrc, size_t cbLen) { memmove(pDest, pSrc, cbLen); }

// Ordinal 492.
void __stdcall SMemFill(void *pvDest, DWORD cbSize, BYTE bValue) { memset(pvDest, bValue, cbSize); }

// Ordinal 494.
void __stdcall SMemZero(void *pvBuffer, DWORD cbBuffer) { memset(pvBuffer, 0, cbBuffer); }

// Ordinal 495: -1, 0 or 1, from the first byte that differs, as unsigned.
int __stdcall SMemCmp(const void *pv1, const void *pv2, DWORD cbSize)
{
  const int n = memcmp(pv1, pv2, cbSize);
  return n < 0 ? -1 : n > 0 ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Strings. A null string is error 0x57 (ERROR_INVALID_PARAMETER) in each.

static const DWORD SERR_INVALID = 0x57;

// Ordinal 501. At most cchDest - 1 characters and always a terminator; the
// answer is the length copied, which no caller in 1.17 reads. A cut that would
// split a UTF-8 character (a translated name in the classic's 25 bytes) leaves
// the whole character out.
char *__stdcall SStrCopy(char *pszDest, const char *pszSrc, size_t cchDest)
{
  if (!pszDest || !pszSrc) {
    SErrSetLastError(SERR_INVALID);
    return NULL;
  }
  if (cchDest == 0)
    return NULL;
  size_t n = 0;
  while (n + 1 < cchDest && pszSrc[n]) {
    pszDest[n] = pszSrc[n];
    ++n;
  }
  if (pszSrc[n] != '\0' && (static_cast<unsigned char>(pszSrc[n]) & 0xC0) == 0x80) {
    size_t nLead = n;
    while (nLead > 0 && n - nLead < 3 && (static_cast<unsigned char>(pszDest[nLead - 1]) & 0xC0) == 0x80)
      --nLead;
    if (nLead > 0) {
      const unsigned char b = static_cast<unsigned char>(pszDest[nLead - 1]);
      const size_t cb = b >= 0xF0 && b <= 0xF4 ? 4 : b >= 0xE0 && b < 0xF0 ? 3 : b >= 0xC2 && b < 0xE0 ? 2 : 0;
      if (cb > n - nLead + 1)
        n = nLead - 1;
    }
  }
  pszDest[n] = '\0';
  return reinterpret_cast<char *>(n);
}

// Ordinal 502. Storm's hash of a name. Without flag 1 it folds case and reads
// '/' as '\\', so paths that differ only in those hash alike; a seed of 0 is
// Storm's own. It never answers 0.
static const DWORD s_adwHashTable[16] = {
    0x486E26EE, 0xDCAA16B3, 0xE1918EEF, 0x202DAFDB, 0x341C7DC7, 0x1C365303, 0x40EF2D37, 0x65FD5E49,
    0xD6057177, 0x904ECE93, 0x1C38024F, 0x98FD323B, 0xE3061AE7, 0xA39B0FA1, 0x9797F25F, 0xE4444563,
};

int __stdcall SStrHash(const char *pszStr, DWORD dwFlags, DWORD dwSeed)
{
  if (!pszStr) {
    SErrSetLastError(SERR_INVALID);
    return 0;
  }
  DWORD dwHash = dwSeed ? dwSeed : 0x7FED7FED;
  DWORD dwShift = 0xEEEEEEEE;
  for (const BYTE *p = reinterpret_cast<const BYTE *>(pszStr); *p; ++p) {
    DWORD c = *p;
    if (!(dwFlags & 1)) {
      if (c - 'a' <= 'z' - 'a')
        c -= 'a' - 'A';
      if (c == '/')
        c = '\\';
    }
    dwHash = (dwHash + dwShift) ^ (s_adwHashTable[c >> 4] - s_adwHashTable[c & 15]);
    dwShift = c + dwShift + (dwShift << 5) + dwHash + 3;
  }
  return static_cast<int>(dwHash ? dwHash : 1);
}

// Ordinal 503. Appends pszSrc to pszDest within cchDest bytes; the byte at
// cchDest - 1 is made a terminator first, unless cchDest is 0x7FFFFFFF, Storm's
// "no limit". The answer is the new length, which no caller in 1.17 reads.
char *__stdcall SStrPack(char *pszDest, const char *pszSrc, size_t cchDest)
{
  if (!pszDest || !pszSrc) {
    SErrSetLastError(SERR_INVALID);
    return NULL;
  }
  if (cchDest == 0)
    return NULL;
  if (cchDest != 0x7FFFFFFF)
    pszDest[cchDest - 1] = '\0';
  size_t n = strlen(pszDest);
  while (n + 1 < cchDest && *pszSrc)
    pszDest[n++] = *pszSrc++;
  pszDest[n] = '\0';
  return reinterpret_cast<char *>(n);
}

// Ordinal 506.
DWORD __stdcall SStrLen(const char *pszStr)
{
  if (!pszStr) {
    SErrSetLastError(SERR_INVALID);
    return 0;
  }
  return static_cast<DWORD>(strlen(pszStr));
}

// Ordinals 508 and 509: the CRT's strncmp and _strnicmp. A null string
// answers the two pointers' difference, as Storm's does. Storm's 509 answers -1,
// 0 or 1, where the x64 runtime's _strnicmp answers the bytes' difference.
int __stdcall SStrCmp(const char *psz1, const char *psz2, size_t cchMax)
{
  if (!psz1 || !psz2) {
    SErrSetLastError(SERR_INVALID);
    return static_cast<int>(psz1 - psz2);
  }
  return strncmp(psz1, psz2, cchMax);
}

int __stdcall SStrCmpI(const char *pszA, const char *pszB, size_t cchMax)
{
  if (!pszA || !pszB) {
    SErrSetLastError(SERR_INVALID);
    return static_cast<int>(pszA - pszB);
  }
  const int n = _strnicmp(pszA, pszB, cchMax);
  return n < 0 ? -1 : n > 0 ? 1 : 0;
}

// Ordinal 510: the CRT's _strupr, in place.
void __stdcall SStrUpper(char *pszStr) { _strupr(pszStr); }

// Ordinals 571 and 572: the first and the last chFind in pszStr. Unlike strchr,
// the terminator is never found.
char *__stdcall SStrChrF(const char *pszStr, char chFind)
{
  if (!pszStr) {
    SErrSetLastError(SERR_INVALID);
    return NULL;
  }
  for (; *pszStr; ++pszStr)
    if (*pszStr == chFind)
      return const_cast<char *>(pszStr);
  return NULL;
}

char *__stdcall SStrChrR(const char *pszStr, char chFind)
{
  if (!pszStr) {
    SErrSetLastError(SERR_INVALID);
    return NULL;
  }
  const char *pLast = NULL;
  for (; *pszStr; ++pszStr)
    if (*pszStr == chFind)
      pLast = pszStr;
  return const_cast<char *>(pLast);
}

// Ordinal 578. _vsnprintf, and a result that did not fit is cut at cchDest - 1
// and terminated there; a result of exactly cchDest characters is not
// terminated, as the CRT leaves it.
int __cdecl SStrPrintf(char *pszDest, size_t cchDest, const char *pszFormat, ...)
{
  if (!pszDest || !pszFormat) {
    SErrSetLastError(SERR_INVALID);
    return 0;
  }
  if (cchDest == 0)
    return 0;
  va_list args;
  va_start(args, pszFormat);
  int n = _vsnprintf(pszDest, cchDest, pszFormat, args);
  va_end(args);
  if (n < 0) {
    n = static_cast<int>(cchDest) - 1;
    pszDest[n] = '\0';
  }
  return n;
}

// Ordinal 587: the first place pszFind is in pszStr, ignoring case. An empty
// pszStr has no match, even for an empty pszFind.
char *__stdcall SStrStr(const char *pszStr, const char *pszFind)
{
  if (!pszStr || !pszFind) {
    SErrSetLastError(SERR_INVALID);
    return NULL;
  }
  const size_t cchFind = strlen(pszFind);
  for (; *pszStr; ++pszStr)
    if (_strnicmp(pszStr, pszFind, cchFind) == 0)
      return const_cast<char *>(pszStr);
  return NULL;
}

// ---------------------------------------------------------------------------
// The registry. 1.17 keeps its settings under Storm's registry key, which vanilla shares;
// Hyperion keeps them apart (PRD 2026-09-27), so these read and write the settings file
// (port_settings.cpp), a section a key, and answer as Storm does over the registry. A key is
// the section, or "Battle.net\<key>" with flag 2, which Storm puts under "Software\Battle.net\"
// rather than "Software\Blizzard Entertainment\". A missing value fails with
// ERROR_FILE_NOT_FOUND, as the registry's does, and a missing name with Storm's error 0x57.

static const DWORD SREG_BATTLENET = 0x02;
static const DWORD SREG_HKLM = 0x04;        // deleting: from HKEY_LOCAL_MACHINE
static const DWORD SREG_MULTI_SZ = 0x80;    // SRegSaveData: REG_MULTI_SZ, not REG_BINARY

static void RegSection(char *pszSection, const char *pszKey, DWORD dwFlags)
{
  pszSection[0] = '\0';
  if (dwFlags & SREG_BATTLENET)
    SStrCopy(pszSection, "Battle.net\\", MAX_PATH);
  SStrPack(pszSection, pszKey, MAX_PATH);
}

// Storm checks the names' first characters, so an empty name is refused too.
static bool RegNamed(const char *pszKey, const char *pszValue)
{
  if (*pszKey && *pszValue)
    return true;
  SErrSetLastError(SERR_INVALID);
  return false;
}

static BOOL RegMissing(void)
{
  SetLastError(ERROR_FILE_NOT_FOUND);
  return FALSE;
}

// Ordinal 421. The value's bytes, however it is kept: a number as its four bytes, a
// string with its terminator.
BOOL __stdcall SRegLoadData(const char *pszKey, const char *pszValue, DWORD dwFlags, void *pvData, DWORD cbData,
                            DWORD *pcbData)
{
  if (!RegNamed(pszKey, pszValue))
    return FALSE;
  char szSection[MAX_PATH];
  RegSection(szSection, pszKey, dwFlags);
  size_t cb = 0;
  const bool bOk = PortSettingData(szSection, pszValue, pvData, cbData, &cb, NULL);
  if (pcbData)
    *pcbData = static_cast<DWORD>(cb);
  if (bOk)
    return TRUE;
  if (cb > cbData) {
    SetLastError(ERROR_MORE_DATA);
    return FALSE;
  }
  return RegMissing();
}

// Ordinal 422. A string is ended within cchBuf; a number is written out in
// decimal; bytes are given as they are.
BOOL __stdcall SRegLoadString(const char *pszKey, const char *pszValue, DWORD dwFlags, char *pszBuf, DWORD cchBuf)
{
  if (!*pszKey || !*pszValue || !pszBuf || !cchBuf) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  char szSection[MAX_PATH];
  RegSection(szSection, pszKey, dwFlags);
  const PortSettingKind kind = PortSettingKindOf(szSection, pszValue);
  if (kind == PORT_SETTING_NONE)
    return RegMissing();
  if (kind == PORT_SETTING_DATA) {
    size_t cb = 0;
    if (!PortSettingData(szSection, pszValue, pszBuf, cchBuf, &cb, NULL)) {
      SetLastError(ERROR_MORE_DATA);
      return FALSE;
    }
    return TRUE;
  }
  return PortSettingString(szSection, pszValue, pszBuf, cchBuf) ? TRUE : RegMissing();
}

// Ordinal 423. A number, or a string read as one (strtoul, any base); bytes answer
// TRUE and leave *pdwValue alone.
BOOL __stdcall SRegLoadValue(const char *pszKey, const char *pszValue, DWORD dwFlags, DWORD *pdwValue)
{
  if (!*pszKey || !*pszValue || !pdwValue) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  char szSection[MAX_PATH];
  RegSection(szSection, pszKey, dwFlags);
  const PortSettingKind kind = PortSettingKindOf(szSection, pszValue);
  if (kind == PORT_SETTING_NONE)
    return RegMissing();
  double d = 0.0;
  if (kind != PORT_SETTING_DATA && PortSettingNumber(szSection, pszValue, &d))
    *pdwValue = static_cast<DWORD>(d);
  return TRUE;
}

// Ordinal 424. Bytes, or REG_MULTI_SZ's strings with flag 0x80; no data is an empty value.
BOOL __stdcall SRegSaveData(const char *pszKey, const char *pszValue, DWORD dwFlags, const void *pvData, DWORD cbData)
{
  if (!RegNamed(pszKey, pszValue))
    return FALSE;
  char szSection[MAX_PATH];
  RegSection(szSection, pszKey, dwFlags);
  PortSettingSetData(szSection, pszValue, pvData ? pvData : "", pvData ? cbData : 0, (dwFlags & SREG_MULTI_SZ) != 0);
  return TRUE;
}

// Ordinal 425.
BOOL __stdcall SRegSaveString(const char *pszKey, const char *pszValue, DWORD dwFlags, const char *pszString)
{
  if (!*pszKey || !*pszValue || !pszString) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  char szSection[MAX_PATH];
  RegSection(szSection, pszKey, dwFlags);
  PortSettingSetString(szSection, pszValue, pszString);
  return TRUE;
}

// Ordinal 426.
BOOL __stdcall SRegSaveValue(const char *pszKey, const char *pszValue, DWORD dwFlags, DWORD dwValue)
{
  if (!RegNamed(pszKey, pszValue))
    return FALSE;
  char szSection[MAX_PATH];
  RegSection(szSection, pszKey, dwFlags);
  PortSettingSetNumber(szSection, pszValue, static_cast<double>(dwValue));
  return TRUE;
}

// Ordinal 428. Storm opens the key for reading only, which Windows refuses a
// deletion through, so with flag 1 or 4 nothing is deleted and the answer is
// ERROR_ACCESS_DENIED; with neither, as the game calls it, nothing is deleted
// and the answer is FALSE. This keeps that: the value stays in the file.
BOOL __stdcall SRegDeleteValue(const char *pszKey, const char *pszValue, DWORD dwFlags)
{
  if (!RegNamed(pszKey, pszValue))
    return FALSE;
  SetLastError(dwFlags & (0x01 | SREG_HKLM) ? ERROR_ACCESS_DENIED : ERROR_SUCCESS);
  return FALSE;
}

}  // extern "C"
