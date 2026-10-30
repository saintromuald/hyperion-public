// storm_test.cpp -- checks Storm's memory, string, registry, image, file and
// video functions as the port has them (src117/port/data/port_stormbase.cpp,
// port_stormbmp.cpp, port_stormfile.cpp, port_stormvid.cpp) against the
// player's storm.dll.
//
//   storm_test.exe <path to storm.dll> [rounds] [map folders]
//
// Each case calls Storm's function, found by ordinal, and ours on the same
// input, into buffers with guard bytes on both sides, and compares the answer,
// every byte written and the last error Storm keeps (SErrGetLastError, reset
// before each call). The inputs are random, from the shapes 1.17 passes and
// past them: strings with both cases, both slashes, high bytes and shared
// prefixes; limits of 0, 1, the length, one either side of it and Storm's
// 0x7FFFFFFF; overlapping copies both ways; a format for each one 1.17 uses,
// cut short at every length. Allocation is compared by what can be: both
// answer a block, flag 8 zeroes it, a block keeps its bytes when it grows, and
// a block of Storm's handed to our SMemFree is freed by Storm. The registry
// functions load the player's own StarCraft settings with every flag, and
// load, save and delete values of every type under a test key of their own,
// HKCU\Software\HyperionStormTest, which the test deletes at the end.
// The image functions decode random PCX files of every depth and plane count,
// with data cut short or running on and buffers short of the image or past
// it, and load the game's own images from the install's archives.
// The file functions open the install's archives in both, and compare every
// name the game's tables give, through the search and in each archive, and
// misspelt; each file's size, name, archive, reads of random lengths past its
// end and a read by request; loading; files on disk in every scope; the
// install and data folders; and each map in the folders given, every file its
// list names.
// The Dda functions stream the game's music and sounds, one or two at a time,
// onto a DirectSound of the test's own whose play cursor the test moves; each
// buffer, the calls made on it and the positions reported are compared after
// every move, once storm.dll's worker has caught up.
// The video functions play the game's videos, the cinematics, the menus'
// animations and the portraits, with the flags the game passes and at random,
// into a bitmap or onto a screen of the test's own, and compare each frame,
// every palette handed to the screen and every lock, unlock and clear.
// Exit code 0 means no difference was found.
#include <windows.h>
#include <dsound.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

extern "C" {
LPVOID __stdcall SMemAlloc(DWORD size, LPCSTR ownerTag, int ownerLine, DWORD flags);
BOOL __stdcall SMemFree(LPCVOID ptr, LPCSTR ownerTag, int ownerLine, DWORD flags);
void *__stdcall SMemReAlloc(void *pMem, DWORD cbSize, const char *pszTag, int nLine, DWORD dwFlags);
void __stdcall SMemCopy(void *pDest, const void *pSrc, size_t cbLen);
void __stdcall SMemFill(void *pvDest, DWORD cbSize, BYTE bValue);
void __stdcall SMemZero(void *pvBuffer, DWORD cbBuffer);
int __stdcall SMemCmp(const void *pv1, const void *pv2, DWORD cbSize);
char *__stdcall SStrCopy(char *pszDest, const char *pszSrc, size_t cchDest);
int __stdcall SStrHash(const char *pszStr, DWORD dwFlags, DWORD dwSeed);
char *__stdcall SStrPack(char *pszDest, const char *pszSrc, size_t cchDest);
DWORD __stdcall SStrLen(const char *pszStr);
int __stdcall SStrCmp(const char *psz1, const char *psz2, size_t cchMax);
int __stdcall SStrCmpI(const char *pszA, const char *pszB, size_t cchMax);
void __stdcall SStrUpper(char *pszStr);
char *__stdcall SStrChrF(const char *pszStr, char chFind);
char *__stdcall SStrChrR(const char *pszStr, char chFind);
int __cdecl SStrPrintf(char *pszDest, size_t cchDest, const char *pszFormat, ...);
char *__stdcall SStrStr(const char *pszStr, const char *pszFind);
}

typedef void(__stdcall *PFN_SETERR)(DWORD);
typedef DWORD(__stdcall *PFN_GETERR)(void);
static PFN_SETERR s_pfnSetErr;
static PFN_GETERR s_pfnGetErr;

// Ours sets its errors through Storm's, as the payload does through its import.
extern "C" void __stdcall SErrSetLastError(DWORD dwErrCode) { s_pfnSetErr(dwErrCode); }

static HMODULE s_hStorm;
template <typename T> static T Ord(WORD w)
{
  T p = reinterpret_cast<T>(GetProcAddress(s_hStorm, MAKEINTRESOURCEA(w)));
  if (!p) {
    printf("storm.dll has no ordinal %u\n", w);
    exit(2);
  }
  return p;
}

static uint32_t g_rng = 0x2545F491u;
static uint32_t Rand32()
{
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static int RandBelow(int n) { return static_cast<int>(Rand32() % static_cast<uint32_t>(n)); }

static int g_nCases = 0;
static int g_nDiffs = 0;

static void Diff(const char *pszWhat, const std::string &detail)
{
  if (g_nDiffs++ < 30)
    printf("DIFF %s: %s\n", pszWhat, detail.c_str());
}

// What a call left: its answer, Storm's last error, and the destination with
// its guards.
struct Outcome {
  long long nRet;
  DWORD dwErr;
  std::vector<unsigned char> dest;
  bool operator==(const Outcome &o) const { return nRet == o.nRet && dwErr == o.dwErr && dest == o.dest; }
};

static const size_t GUARD = 16;

static std::string Describe(const Outcome &a, const Outcome &b)
{
  char sz[160];
  size_t i = 0;
  while (i < a.dest.size() && i < b.dest.size() && a.dest[i] == b.dest[i])
    ++i;
  sprintf(sz, "Storm %lld err %u, ours %lld err %u, buffers differ at %d", a.nRet, a.dwErr, b.nRet, b.dwErr,
          i < a.dest.size() ? static_cast<int>(i) - static_cast<int>(GUARD) : -1);
  return sz;
}

static void Compare(const char *pszWhat, const Outcome &a, const Outcome &b, const std::string &input)
{
  ++g_nCases;
  if (!(a == b))
    Diff(pszWhat, input + ": " + Describe(a, b));
}

// A random string: the letters 1.17's names and paths use, both slashes, and
// now and then any byte.
static std::string RandString(int nMax)
{
  static const char s_szChars[] = "aAbBcCmMsSzZ09 ._-\\/:%";
  std::string s;
  int n = RandBelow(nMax + 1);
  for (int i = 0; i < n; ++i) {
    char c = RandBelow(8) == 0 ? static_cast<char>(1 + RandBelow(255)) : s_szChars[RandBelow(sizeof s_szChars - 1)];
    s += c;
  }
  return s;
}

// A copy of s with some letters' case changed.
static std::string Recase(std::string s)
{
  for (size_t i = 0; i < s.size(); ++i)
    if (RandBelow(3) == 0 && isalpha(static_cast<unsigned char>(s[i])))
      s[i] ^= 0x20;
  return s;
}

static size_t RandLimit(size_t cch)
{
  switch (RandBelow(8)) {
  case 0: return 0;
  case 1: return 1;
  case 2: return cch;
  case 3: return cch + 1;
  case 4: return cch ? cch - 1 : 0;
  default: return 1 + RandBelow(static_cast<int>(cch) + 8);
  }
}

// ---------------------------------------------------------------------------

typedef char *(__stdcall *PFN_STRCOPY)(char *, const char *, size_t);
typedef int(__stdcall *PFN_STRHASH)(const char *, DWORD, DWORD);
typedef DWORD(__stdcall *PFN_STRLEN)(const char *);
typedef int(__stdcall *PFN_STRCMP)(const char *, const char *, size_t);
typedef void(__stdcall *PFN_STRUPPER)(char *);
typedef char *(__stdcall *PFN_STRCHR)(const char *, char);
typedef int(__cdecl *PFN_STRPRINTF)(char *, size_t, const char *, ...);
typedef char *(__stdcall *PFN_STRSTR)(const char *, const char *);
typedef void(__stdcall *PFN_MEMCOPY)(void *, const void *, size_t);
typedef void(__stdcall *PFN_MEMFILL)(void *, DWORD, BYTE);
typedef void(__stdcall *PFN_MEMZERO)(void *, DWORD);
typedef int(__stdcall *PFN_MEMCMP)(const void *, const void *, DWORD);
typedef LPVOID(__stdcall *PFN_MEMALLOC)(DWORD, LPCSTR, int, DWORD);
typedef BOOL(__stdcall *PFN_MEMFREE)(LPCVOID, LPCSTR, int, DWORD);
typedef void *(__stdcall *PFN_MEMREALLOC)(void *, DWORD, const char *, int, DWORD);

// A destination of cb bytes, filled with fill, guards around it.
static std::vector<unsigned char> Buffer(size_t cb, unsigned char fill)
{
  std::vector<unsigned char> v(cb + 2 * GUARD, 0xA5);
  memset(&v[GUARD], fill, cb);
  return v;
}

// SStrCopy and SStrPack: the destination starts with a string of its own.
static void StrCopyPack(int nRound)
{
  const bool bPack = nRound & 1;
  PFN_STRCOPY pfnStorm = Ord<PFN_STRCOPY>(bPack ? 503 : 501);
  PFN_STRCOPY pfnOurs = bPack ? SStrPack : SStrCopy;
  const std::string src = RandString(40), start = RandString(30);
  const size_t cb = 64;
  size_t cch = RandLimit(bPack ? start.size() + src.size() : src.size());
  if (bPack && RandBelow(16) == 0)
    cch = 0x7FFFFFFF;
  if (cch > cb && cch != 0x7FFFFFFF)
    cch = cb;
  const bool bNull = RandBelow(32) == 0;
  Outcome o[2];
  for (int k = 0; k < 2; ++k) {
    std::vector<unsigned char> d = Buffer(cb, 0x5A);
    memcpy(&d[GUARD], start.c_str(), start.size() + 1);
    s_pfnSetErr(0);
    char *p = (k ? pfnOurs : pfnStorm)(reinterpret_cast<char *>(&d[GUARD]), bNull ? NULL : src.c_str(), cch);
    o[k].nRet = reinterpret_cast<long long>(p);
    o[k].dwErr = s_pfnGetErr();
    o[k].dest.swap(d);
  }
  Compare(bPack ? "SStrPack" : "SStrCopy", o[0], o[1], "\"" + start + "\" + \"" + src + "\" cch " + std::to_string(cch));
}

static void StrHash(void)
{
  PFN_STRHASH pfnStorm = Ord<PFN_STRHASH>(502);
  const std::string s = RandString(48);
  const DWORD dwFlags = RandBelow(4) ? RandBelow(2) : Rand32();
  const DWORD dwSeed = RandBelow(3) ? 0 : Rand32();
  Outcome o[2];
  for (int k = 0; k < 2; ++k) {
    s_pfnSetErr(0);
    o[k].nRet = (k ? SStrHash : pfnStorm)(s.c_str(), dwFlags, dwSeed);
    o[k].dwErr = s_pfnGetErr();
  }
  Compare("SStrHash", o[0], o[1], "\"" + s + "\" flags " + std::to_string(dwFlags) + " seed " + std::to_string(dwSeed));
}

static void StrLenCmp(void)
{
  const std::string a = RandString(24);
  std::string b = RandBelow(2) ? Recase(a) : a;
  if (RandBelow(2)) {
    b.resize(RandBelow(static_cast<int>(b.size()) + 1));
    b += RandString(6);
  }
  const size_t cch = RandLimit(a.size() > b.size() ? a.size() : b.size());
  for (int f = 0; f < 3; ++f) {
    Outcome o[2];
    for (int k = 0; k < 2; ++k) {
      s_pfnSetErr(0);
      if (f == 0)
        o[k].nRet = (k ? SStrLen : Ord<PFN_STRLEN>(506))(a.c_str());
      else
        o[k].nRet = (k ? (f == 1 ? SStrCmp : SStrCmpI) : Ord<PFN_STRCMP>(f == 1 ? 508 : 509))(a.c_str(), b.c_str(), cch);
      o[k].dwErr = s_pfnGetErr();
    }
    static const char *s_aszName[] = {"SStrLen", "SStrCmp", "SStrCmpI"};
    Compare(s_aszName[f], o[0], o[1], "\"" + a + "\" \"" + b + "\" cch " + std::to_string(cch));
  }
}

static void StrUpperChr(void)
{
  const std::string s = RandString(32);
  const char ch = RandBelow(4) == 0 ? static_cast<char>(Rand32()) : (s.empty() ? 'a' : s[RandBelow(static_cast<int>(s.size()))]);
  {
    Outcome o[2];
    for (int k = 0; k < 2; ++k) {
      std::vector<unsigned char> d = Buffer(40, 0x5A);
      memcpy(&d[GUARD], s.c_str(), s.size() + 1);
      s_pfnSetErr(0);
      (k ? SStrUpper : Ord<PFN_STRUPPER>(510))(reinterpret_cast<char *>(&d[GUARD]));
      o[k].nRet = 0;
      o[k].dwErr = s_pfnGetErr();
      o[k].dest.swap(d);
    }
    Compare("SStrUpper", o[0], o[1], "\"" + s + "\"");
  }
  for (int r = 0; r < 2; ++r) {
    Outcome o[2];
    for (int k = 0; k < 2; ++k) {
      s_pfnSetErr(0);
      const char *p = (k ? (r ? SStrChrR : SStrChrF) : Ord<PFN_STRCHR>(r ? 572 : 571))(s.c_str(), ch);
      o[k].nRet = p ? p - s.c_str() : -1;
      o[k].dwErr = s_pfnGetErr();
    }
    Compare(r ? "SStrChrR" : "SStrChrF", o[0], o[1], "\"" + s + "\" '" + std::string(1, ch) + "'");
  }
}

static void StrStr(void)
{
  const std::string s = RandString(40);
  std::string find;
  if (RandBelow(3) && !s.empty()) {
    const int i = RandBelow(static_cast<int>(s.size()));
    find = Recase(s.substr(i, RandBelow(static_cast<int>(s.size()) - i + 1)));
  } else {
    find = RandString(4);
  }
  Outcome o[2];
  for (int k = 0; k < 2; ++k) {
    s_pfnSetErr(0);
    const char *p = (k ? SStrStr : Ord<PFN_STRSTR>(587))(s.c_str(), find.c_str());
    o[k].nRet = p ? p - s.c_str() : -1;
    o[k].dwErr = s_pfnGetErr();
  }
  Compare("SStrStr", o[0], o[1], "\"" + s + "\" \"" + find + "\"");
}

// Every format 1.17 passes, with arguments of their kind.
static void StrPrintf(void)
{
  PFN_STRPRINTF pfnStorm = Ord<PFN_STRPRINTF>(578);
  const std::string a = RandString(20), b = RandString(20), c = RandString(8);
  const int n = RandBelow(2) ? RandBelow(100) : static_cast<int>(Rand32());
  const int nFormat = RandBelow(10);
  const size_t cch = RandLimit(40);
  Outcome o[2];
  for (int k = 0; k < 2; ++k) {
    PFN_STRPRINTF pfn = k ? SStrPrintf : pfnStorm;
    std::vector<unsigned char> d = Buffer(64, 0x5A);
    char *pd = reinterpret_cast<char *>(&d[GUARD]);
    const size_t lim = cch > 64 ? 64 : cch;
    s_pfnSetErr(0);
    switch (nFormat) {
    case 0: o[k].nRet = pfn(pd, lim, "%s\\maps\\replays\\%s.%s", a.c_str(), b.c_str(), c.c_str()); break;
    case 1: o[k].nRet = pfn(pd, lim, "League_%d.cnd", n); break;
    case 2: o[k].nRet = pfn(pd, lim, "/stats %s", a.c_str()); break;
    case 3: o[k].nRet = pfn(pd, lim, "%s\\*", a.c_str()); break;
    case 4: o[k].nRet = pfn(pd, lim, "%s\\%s%s\\%s_%s.%s", a.c_str(), b.c_str(), c.c_str(), a.c_str(), c.c_str(), b.c_str()); break;
    case 5: o[k].nRet = pfn(pd, lim, "%s\\", a.c_str()); break;
    case 6: o[k].nRet = pfn(pd, lim, "%s%s\\%s.mpq", a.c_str(), b.c_str(), c.c_str()); break;
    case 7: o[k].nRet = pfn(pd, lim, "%s%s%s", a.c_str(), b.c_str(), c.c_str()); break;
    case 8: o[k].nRet = pfn(pd, lim, " %d", n); break;
    default: o[k].nRet = pfn(pd, lim, " %s", a.c_str()); break;
    }
    o[k].dwErr = s_pfnGetErr();
    o[k].dest.swap(d);
  }
  Compare("SStrPrintf", o[0], o[1], "format " + std::to_string(nFormat) + " cch " + std::to_string(cch));
}

static void MemOps(void)
{
  const size_t cb = 1 + RandBelow(300);
  std::vector<unsigned char> src(cb);
  for (size_t i = 0; i < cb; ++i)
    src[i] = static_cast<unsigned char>(Rand32());
  // copies, overlapping both ways inside one buffer
  {
    const size_t cbBuf = cb + 64;
    const size_t nFrom = RandBelow(64), nTo = RandBelow(64);
    const size_t n = RandBelow(static_cast<int>(cb) + 1);
    Outcome o[2];
    for (int k = 0; k < 2; ++k) {
      std::vector<unsigned char> d = Buffer(cbBuf, 0);
      memcpy(&d[GUARD], &src[0], cb);
      (k ? SMemCopy : Ord<PFN_MEMCOPY>(491))(&d[GUARD + nTo], &d[GUARD + nFrom], n);
      o[k].nRet = 0;
      o[k].dwErr = 0;
      o[k].dest.swap(d);
    }
    Compare("SMemCopy", o[0], o[1], std::to_string(n) + " bytes from " + std::to_string(nFrom) + " to " + std::to_string(nTo));
  }
  // fill and zero
  {
    const size_t n = RandBelow(static_cast<int>(cb) + 1);
    const BYTE b = static_cast<BYTE>(Rand32());
    Outcome o[2];
    for (int k = 0; k < 2; ++k) {
      std::vector<unsigned char> d = Buffer(cb, 0x77);
      (k ? SMemFill : Ord<PFN_MEMFILL>(492))(&d[GUARD], static_cast<DWORD>(n), b);
      (k ? SMemZero : Ord<PFN_MEMZERO>(494))(&d[GUARD + n / 2], static_cast<DWORD>(n / 3));
      o[k].nRet = 0;
      o[k].dwErr = 0;
      o[k].dest.swap(d);
    }
    Compare("SMemFill, SMemZero", o[0], o[1], std::to_string(n) + " bytes");
  }
  // compare: equal up to a random point
  {
    std::vector<unsigned char> other(src);
    const size_t n = RandBelow(static_cast<int>(cb) + 1);
    if (RandBelow(4) && cb)
      other[RandBelow(static_cast<int>(cb))] = static_cast<unsigned char>(Rand32());
    Outcome o[2];
    for (int k = 0; k < 2; ++k) {
      o[k].nRet = (k ? SMemCmp : Ord<PFN_MEMCMP>(495))(&src[0], &other[0], static_cast<DWORD>(n));
      o[k].dwErr = 0;
    }
    Compare("SMemCmp", o[0], o[1], std::to_string(n) + " bytes");
  }
}

// What can be compared of the allocator.
static void MemAlloc(void)
{
  PFN_MEMALLOC pfnAlloc = Ord<PFN_MEMALLOC>(401);
  PFN_MEMFREE pfnFree = Ord<PFN_MEMFREE>(403);
  const DWORD cb = RandBelow(4) ? RandBelow(256) : RandBelow(0x40000);

  // a block, zeroed under flag 8 by both and by ours always
  unsigned char *pStorm = static_cast<unsigned char *>(pfnAlloc(cb, "storm_test", 1, 8));
  unsigned char *pOurs = static_cast<unsigned char *>(SMemAlloc(cb, "storm_test", 1, RandBelow(2) ? 8 : 0));
  ++g_nCases;
  bool bZero = pStorm && pOurs;
  for (DWORD i = 0; bZero && i < cb; ++i)
    bZero = pStorm[i] == 0 && pOurs[i] == 0;
  if (!bZero)
    Diff("SMemAlloc", std::to_string(cb) + " bytes: a block missing or not zeroed");

  // growing keeps the bytes, and the new ones are zero
  for (DWORD i = 0; i < cb; ++i)
    pOurs[i] = static_cast<unsigned char>(i * 7 + 1);
  const DWORD cbNew = cb + 1 + RandBelow(512);
  unsigned char *pGrown = static_cast<unsigned char *>(SMemReAlloc(pOurs, cbNew, "storm_test", 2, 0));
  ++g_nCases;
  bool bKept = pGrown != NULL;
  for (DWORD i = 0; bKept && i < cbNew; ++i)
    bKept = pGrown[i] == (i < cb ? static_cast<unsigned char>(i * 7 + 1) : 0);
  if (!bKept)
    Diff("SMemReAlloc", std::to_string(cb) + " to " + std::to_string(cbNew) + " bytes: the bytes were not kept");
  // in place or not at all
  ++g_nCases;
  if (SMemReAlloc(pGrown, cbNew + 64, "storm_test", 3, 0x10) != NULL ||
      SMemReAlloc(pGrown, cbNew / 2, "storm_test", 3, 0x10) != pGrown)
    Diff("SMemReAlloc", "flag 0x10 moved a block or refused to shrink one");

  // our SMemFree takes back Storm's block through Storm, and ours itself
  ++g_nCases;
  s_pfnSetErr(0);
  if (!SMemFree(pStorm, "storm_test", 4, 0) || !SMemFree(pGrown, "storm_test", 4, 0))
    Diff("SMemFree", "a block was not freed");
  // a null pointer: FALSE, and the same error from both
  Outcome o[2];
  for (int k = 0; k < 2; ++k) {
    s_pfnSetErr(0);
    o[k].nRet = (k ? SMemFree : pfnFree)(NULL, "storm_test", 5, 0);
    o[k].dwErr = s_pfnGetErr();
  }
  Compare("SMemFree(NULL)", o[0], o[1], "");
}

// ---------------------------------------------------------------------------
// The registry. Writing happens only under HKCU\Software\HyperionStormTest,
// through flag 0x10, and the key is deleted at the end; the player's own
// StarCraft settings are only read, by both.

extern "C" {
BOOL __stdcall SRegLoadData(const char *, const char *, DWORD, void *, DWORD, DWORD *);
BOOL __stdcall SRegLoadString(const char *, const char *, DWORD, char *, DWORD);
BOOL __stdcall SRegLoadValue(const char *, const char *, DWORD, DWORD *);
BOOL __stdcall SRegSaveData(const char *, const char *, DWORD, const void *, DWORD);
BOOL __stdcall SRegSaveString(const char *, const char *, DWORD, const char *);
BOOL __stdcall SRegSaveValue(const char *, const char *, DWORD, DWORD);
BOOL __stdcall SRegDeleteValue(const char *, const char *, DWORD);
}
typedef BOOL(__stdcall *PFN_REGLOADDATA)(const char *, const char *, DWORD, void *, DWORD, DWORD *);
typedef BOOL(__stdcall *PFN_REGLOADSTRING)(const char *, const char *, DWORD, char *, DWORD);
typedef BOOL(__stdcall *PFN_REGLOADVALUE)(const char *, const char *, DWORD, DWORD *);
typedef BOOL(__stdcall *PFN_REGSAVEDATA)(const char *, const char *, DWORD, const void *, DWORD);
typedef BOOL(__stdcall *PFN_REGSAVESTRING)(const char *, const char *, DWORD, const char *);
typedef BOOL(__stdcall *PFN_REGSAVEVALUE)(const char *, const char *, DWORD, DWORD);
typedef BOOL(__stdcall *PFN_REGDELETE)(const char *, const char *, DWORD);

static const char TEST_KEY[] = "Software\\HyperionStormTest";

// A value of any type, set straight through Win32.
static void RegPut(const char *pszValue, DWORD dwType, const void *pv, DWORD cb)
{
  HKEY hKey;
  if (RegCreateKeyExA(HKEY_CURRENT_USER, TEST_KEY, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
    RegSetValueExA(hKey, pszValue, 0, dwType, static_cast<const BYTE *>(pv), cb);
    RegCloseKey(hKey);
  }
}

// What a value holds now, as its type and bytes.
static std::vector<unsigned char> RegGet(const char *pszValue)
{
  std::vector<unsigned char> v(4 + 512, 0);
  HKEY hKey;
  DWORD dwType = 0, cb = 512;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, TEST_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
    if (RegQueryValueExA(hKey, pszValue, NULL, &dwType, &v[4], &cb) != ERROR_SUCCESS)
      dwType = 0xFFFFFFFF, cb = 0;
    RegCloseKey(hKey);
  } else {
    dwType = 0xFFFFFFFE, cb = 0;
  }
  memcpy(&v[0], &dwType, 4);
  v.resize(4 + cb);
  return v;
}

// The load functions on one value, which the test has just set.
static void RegLoads(const char *pszKey, const char *pszValue, DWORD dwFlags, const std::string &what)
{
  const DWORD cch = RandLimit(40) + (RandBelow(4) ? 0 : 1);
  for (int f = 0; f < 3; ++f) {
    Outcome o[2];
    for (int k = 0; k < 2; ++k) {
      std::vector<unsigned char> d = Buffer(64, 0x5A);
      DWORD cb = 0xCCCCCCCC;
      s_pfnSetErr(0);
      SetLastError(0);
      const DWORD lim = cch > 64 ? 64 : cch;
      if (f == 0)
        o[k].nRet = (k ? SRegLoadData : Ord<PFN_REGLOADDATA>(421))(pszKey, pszValue, dwFlags, &d[GUARD], lim, &cb);
      else if (f == 1)
        o[k].nRet = (k ? SRegLoadString : Ord<PFN_REGLOADSTRING>(422))(pszKey, pszValue, dwFlags,
                                                                       reinterpret_cast<char *>(&d[GUARD]), lim);
      else
        o[k].nRet = (k ? SRegLoadValue : Ord<PFN_REGLOADVALUE>(423))(pszKey, pszValue, dwFlags,
                                                                     reinterpret_cast<DWORD *>(&d[GUARD]));
      o[k].dwErr = s_pfnGetErr() ^ (GetLastError() << 8);
      d.push_back(static_cast<unsigned char>(cb));
      o[k].dest.swap(d);
    }
    static const char *s_aszName[] = {"SRegLoadData", "SRegLoadString", "SRegLoadValue"};
    Compare(s_aszName[f], o[0], o[1], what + " cch " + std::to_string(cch));
  }
}

static void RegCases(int nRounds)
{
  // loads of every value the player's own settings hold, and of one they do not
  static const char *s_aszStarcraft[] = {"Gamma", "ColorCycle", "UnitPortraits", "speed", "music", "Recent Maps",
                                         "Custom Type", "Game Speed", "StarCD", "no such value"};
  for (int i = 0; i < 10; ++i)
    for (DWORD dwFlags = 0; dwFlags < 8; ++dwFlags)
      RegLoads("Starcraft", s_aszStarcraft[i], dwFlags, std::string("Starcraft ") + s_aszStarcraft[i]);

  static const char *s_aszNumbers[] = {"12", "0x1F", "  7", "-3", "017", "abc", "", "4294967295", "99999999999"};
  for (int r = 0; r < nRounds; ++r) {
    // a value of a random type, loaded by both
    const int nType = RandBelow(5);
    std::string s = RandString(60);
    DWORD dw = Rand32();
    switch (nType) {
    case 0: RegPut("v", REG_SZ, s.c_str(), static_cast<DWORD>(s.size() + 1)); break;
    case 1: {
      const char *psz = s_aszNumbers[RandBelow(9)];
      RegPut("v", REG_SZ, psz, static_cast<DWORD>(strlen(psz) + 1));
      s = psz;
      break;
    }
    case 2: RegPut("v", REG_DWORD, &dw, 4); break;
    case 3: RegPut("v", REG_BINARY, s.c_str(), static_cast<DWORD>(s.size())); break;
    default: RegPut("v", REG_MULTI_SZ, s.c_str(), static_cast<DWORD>(s.size() + 1)); break;
    }
    const DWORD dwLoadFlags = 0x10 | (RandBelow(4) == 0 ? 1 : 0);
    RegLoads(TEST_KEY, RandBelow(8) ? "v" : "missing", dwLoadFlags,
             "type " + std::to_string(nType) + " \"" + s + "\"");

    // each save by Storm and by ours, compared as the registry then holds it
    const int nSave = RandBelow(3);
    const DWORD dwFlags = 0x10 | (RandBelow(2) ? 0x80 : 0) | (RandBelow(2) ? 0x08 : 0);
    // no data only with no length: past it, both would write what follows their own empty string
    const char *pszData = RandBelow(8) ? s.c_str() : NULL;
    Outcome o[2];
    for (int k = 0; k < 2; ++k) {
      s_pfnSetErr(0);
      SetLastError(0);
      const char *pszName = k ? "saved_ours" : "saved_storm";
      if (nSave == 0)
        o[k].nRet = (k ? SRegSaveData : Ord<PFN_REGSAVEDATA>(424))(TEST_KEY, pszName, dwFlags,
                                                                   pszData,
                                                                   pszData ? static_cast<DWORD>(s.size()) : 0);
      else if (nSave == 1)
        o[k].nRet = (k ? SRegSaveString : Ord<PFN_REGSAVESTRING>(425))(TEST_KEY, pszName, dwFlags, s.c_str());
      else
        o[k].nRet = (k ? SRegSaveValue : Ord<PFN_REGSAVEVALUE>(426))(TEST_KEY, pszName, dwFlags, dw);
      o[k].dwErr = s_pfnGetErr() ^ (GetLastError() << 8);
      o[k].dest = RegGet(pszName);
    }
    Compare("SRegSave", o[0], o[1], "save " + std::to_string(nSave) + " flags " + std::to_string(dwFlags));

    // deletion, with every flag the function looks at
    const DWORD dwDelFlags = 0x10 | RandBelow(8);
    for (int k = 0; k < 2; ++k) {
      RegPut("gone", REG_DWORD, &dw, 4);
      s_pfnSetErr(0);
      SetLastError(0);
      o[k].nRet = (k ? SRegDeleteValue : Ord<PFN_REGDELETE>(428))(TEST_KEY, "gone", dwDelFlags);
      o[k].dwErr = s_pfnGetErr() ^ (GetLastError() << 8);
      o[k].dest = RegGet("gone");
    }
    Compare("SRegDeleteValue", o[0], o[1], "flags " + std::to_string(dwDelFlags));
  }

  // empty names
  for (int k = 0; k < 2; ++k) {
    Outcome o[2];
    for (int j = 0; j < 2; ++j) {
      s_pfnSetErr(0);
      o[j].nRet = (j ? SRegSaveValue : Ord<PFN_REGSAVEVALUE>(426))(k ? "" : TEST_KEY, k ? "x" : "", 0x10, 1);
      o[j].dwErr = s_pfnGetErr();
    }
    Compare("SRegSaveValue, empty name", o[0], o[1], "");
  }
  RegDeleteTreeA(HKEY_CURRENT_USER, TEST_KEY);
  RegDeleteKeyA(HKEY_CURRENT_USER, TEST_KEY);
}

// Null arguments, where Storm sets its error.
static void NullCases(void)
{
  for (int f = 0; f < 6; ++f) {
    Outcome o[2];
    for (int k = 0; k < 2; ++k) {
      s_pfnSetErr(0);
      long long n = 0;
      switch (f) {
      case 0: n = (k ? SStrHash : Ord<PFN_STRHASH>(502))(NULL, 0, 0); break;
      case 1: n = (k ? SStrLen : Ord<PFN_STRLEN>(506))(NULL); break;
      case 2: n = (k ? SStrCmp : Ord<PFN_STRCMP>(508))(NULL, "a", 4); break;
      case 3: n = (k ? SStrCmpI : Ord<PFN_STRCMP>(509))("b", NULL, 4); break;
      case 4: n = reinterpret_cast<long long>((k ? SStrChrF : Ord<PFN_STRCHR>(571))(NULL, 'a')); break;
      default: n = reinterpret_cast<long long>((k ? SStrStr : Ord<PFN_STRSTR>(587))("a", NULL)); break;
      }
      o[k].nRet = n;
      o[k].dwErr = s_pfnGetErr();
    }
    Compare("null argument", o[0], o[1], "case " + std::to_string(f));
  }
}

// ---------------------------------------------------------------------------
// Files. Ours reads the archives through StormLib and opens each one in
// storm.dll too (port_stormfile.cpp); the test opens its own copies in
// storm.dll to compare the two by handle.

extern "C" {
DWORD __stdcall SErrGetLastError(void) { return s_pfnGetErr(); }
BOOL __stdcall SFileOpenArchive(const char *, DWORD, DWORD, HANDLE *);
BOOL __stdcall SFileCloseArchive(HANDLE);
BOOL __stdcall SFileOpenFileEx(HANDLE, const char *, DWORD, HANDLE *);
BOOL __stdcall SFileOpenFile(const char *, HANDLE *);
BOOL __stdcall SFileCloseFile(HANDLE);
BOOL __stdcall SFileReadFile(HANDLE, void *, DWORD, DWORD *, void *);
DWORD __stdcall SFileGetFileSize(HANDLE, DWORD *);
BOOL __stdcall SFileGetFileArchive(HANDLE, HANDLE *);
BOOL __stdcall SFileGetFileName(HANDLE, char *, DWORD);
BOOL __stdcall SFileGetArchiveName(HANDLE, char *, DWORD);
BOOL __stdcall SFileLoadFile(const char *, void **, DWORD *, DWORD, void *);
BOOL __stdcall SFileLoadFileEx(HANDLE, const char *, void **, DWORD *, DWORD, DWORD, void *);
BOOL __stdcall SFileUnloadFile(void *);
DWORD __stdcall SFileGetInstallPath(char *, DWORD, DWORD);
DWORD __stdcall SFileGetUserDataPath(char *, DWORD, DWORD);
DWORD __stdcall SFileSetAppDataDir(const char *);
DWORD __stdcall SFileSetLocale(DWORD);
BOOL __stdcall SFileSetIoErrorMode(DWORD, void *);
}
typedef BOOL(__stdcall *PFN_FOPENARCHIVE)(const char *, DWORD, DWORD, HANDLE *);
typedef BOOL(__stdcall *PFN_FCLOSE)(HANDLE);
typedef BOOL(__stdcall *PFN_FOPENFILEEX)(HANDLE, const char *, DWORD, HANDLE *);
typedef BOOL(__stdcall *PFN_FOPENFILE)(const char *, HANDLE *);
typedef BOOL(__stdcall *PFN_FREAD)(HANDLE, void *, DWORD, DWORD *, void *);
typedef DWORD(__stdcall *PFN_FSIZE)(HANDLE, DWORD *);
typedef BOOL(__stdcall *PFN_FARCHIVE)(HANDLE, HANDLE *);
typedef BOOL(__stdcall *PFN_FNAME)(HANDLE, char *, DWORD);
typedef BOOL(__stdcall *PFN_FLOAD)(const char *, void **, DWORD *, DWORD, void *);
typedef BOOL(__stdcall *PFN_FLOADEX)(HANDLE, const char *, void **, DWORD *, DWORD, DWORD, void *);
typedef BOOL(__stdcall *PFN_FUNLOAD)(void *);
typedef DWORD(__stdcall *PFN_FPATH)(char *, DWORD, DWORD);
typedef DWORD(__stdcall *PFN_FSETDIR)(const char *);
typedef DWORD(__stdcall *PFN_FSETLOCALE)(DWORD);
typedef BOOL(__stdcall *PFN_FSETIOMODE)(DWORD, void *);

// Storm's error and Win32's, as one value to compare.
static DWORD BothErrors(void) { return s_pfnGetErr() ^ (GetLastError() << 8); }
static void ClearErrors(void)
{
  s_pfnSetErr(0);
  SetLastError(0);
}

struct TestArchive {
  const char *pszFile;
  DWORD dwPriority;
  HANDLE hStorm, hOurs;
};
static TestArchive s_aArchives[] = {{"StarDat.mpq", 0x7D0, NULL, NULL}, {"BrooDat.mpq", 0x9C4, NULL, NULL}};
static std::vector<std::string> s_names;

// A name table of the game's (arr\*.tbl), read through Storm.
static void AddTblNames(const char *pszTbl, const char *pszPrefix)
{
  void *pv = NULL;
  DWORD cb = 0;
  if (!Ord<PFN_FLOAD>(279)(pszTbl, &pv, &cb, 0, NULL) || cb < 2)
    return;
  const unsigned char *p = static_cast<const unsigned char *>(pv);
  const WORD n = static_cast<WORD>(p[0] | p[1] << 8);
  for (WORD i = 0; i < n && 2u + 2u * i + 1 < cb; ++i) {
    const WORD off = static_cast<WORD>(p[2 + 2 * i] | p[3 + 2 * i] << 8);
    if (off < cb && p[off])
      s_names.push_back(std::string(pszPrefix) + reinterpret_cast<const char *>(p + off));
  }
  Ord<PFN_FUNLOAD>(280)(pv);
}

// Opens the install's archives in both, at the game's priorities, and builds the
// names to look up.
static void ArchivesOpen(const char *pszInstall)
{
  // storm.dll's DllMain, when LoadLibrary loads it, builds the table that
  // decrypts archives from another seed than the one it uses when the loader
  // loads it with the game (0x10000100 against 0x100001), and no archive then
  // reads. Clearing its pointer (0x150696A4) makes the first file call build
  // the table the game has.
  *reinterpret_cast<void **>(reinterpret_cast<BYTE *>(s_hStorm) + 0x696A4) = NULL;
  Outcome o[2];
  for (int k = 0; k < 2; ++k) {
    o[k].nRet = (k ? SFileSetLocale : Ord<PFN_FSETLOCALE>(272))(0x409);
    o[k].dwErr = 0;
  }
  Compare("SFileSetLocale", o[0], o[1], "0x409");
  for (int k = 0; k < 2; ++k) {
    o[k].nRet = (k ? SFileSetIoErrorMode : Ord<PFN_FSETIOMODE>(274))(1, NULL);
    o[k].dwErr = 0;
  }
  Compare("SFileSetIoErrorMode", o[0], o[1], "1");
  for (size_t i = 0; i < sizeof s_aArchives / sizeof *s_aArchives; ++i) {
    TestArchive &a = s_aArchives[i];
    const std::string path = std::string(pszInstall) + "\\" + a.pszFile;
    for (int k = 0; k < 2; ++k) {
      ClearErrors();
      HANDLE h = NULL;
      o[k].nRet = (k ? SFileOpenArchive : Ord<PFN_FOPENARCHIVE>(266))(path.c_str(), a.dwPriority, 2, &h);
      o[k].dwErr = BothErrors();
      (k ? a.hOurs : a.hStorm) = h;
    }
    Compare("SFileOpenArchive", o[0], o[1], path);
    if (!a.hOurs || !a.hStorm)
      printf("cannot open %s\n", path.c_str());
  }
  static const char *s_aszLiterals[] = {
      "arr\\flingy.dat", "arr\\images.dat", "arr\\images.tbl", "arr\\mapdata.dat", "arr\\mapdata.tbl",
      "arr\\orders.dat", "arr\\portdata.dat", "arr\\portdata.tbl", "arr\\sfxdata.dat", "arr\\sfxdata.tbl",
      "arr\\sprites.dat", "arr\\techdata.dat", "arr\\units.dat", "arr\\upgrades.dat", "arr\\weapons.dat",
      "rez\\stat_txt.tbl", "rez\\gluexpcmpgn.bin", "rez\\gateways.txt", "rez\\DataVersion.txt",
      "game\\tunit.pcx", "game\\tselect.pcx", "game\\icons.grp", "game\\blink.grp", "game\\tfontgam.pcx",
      "glue\\title\\title.pcx", "glue\\palmm\\backgnd.pcx", "unit\\cmdbtns\\cmdicons.grp",
      "tileset\\badlands.vx4", "tileset\\badlands.cv5", "tileset\\jungle.wpe", "parallax\\star.spk",
      "music\\title.wav", "music\\terran1.wav", "sound\\misc\\buzz.wav", "font\\font16.fnt",
      "font\\font10.fnt", "(listfile)", "no\\such\\file.dat", "", "\\", "arr", "arr\\",
  };
  for (size_t i = 0; i < sizeof s_aszLiterals / sizeof *s_aszLiterals; ++i)
    s_names.push_back(s_aszLiterals[i]);
  AddTblNames("arr\\images.tbl", "unit\\");
  AddTblNames("arr\\sfxdata.tbl", "sound\\");
  AddTblNames("arr\\portdata.tbl", "portrait\\");
  AddTblNames("arr\\mapdata.tbl", "maps\\");
}

// A name as the game might pass it: its own, another case, '/' for '\', a
// drive or a leading backslash, cut short.
static std::string VaryName(const std::string &s)
{
  switch (RandBelow(8)) {
  case 0: return Recase(s);
  case 1: {
    std::string t(s);
    for (size_t i = 0; i < t.size(); ++i)
      if (t[i] == '\\' && RandBelow(2))
        t[i] = '/';
    return t;
  }
  case 2: return "\\" + s;
  case 3: return s.substr(0, RandBelow(static_cast<int>(s.size()) + 1));
  default: return s;
  }
}

// Everything that can be asked of one open file, of both: size, name, archive,
// reads of random lengths to past the end, a request read at a position.
static void CompareOpenFiles(HANDLE hStorm, HANDLE hOurs, const std::string &what)
{
  Outcome o[2];
  DWORD cbFile = 0;
  for (int k = 0; k < 2; ++k) {
    DWORD dwHigh = 7;
    ClearErrors();
    o[k].nRet = (k ? SFileGetFileSize : Ord<PFN_FSIZE>(265))(k ? hOurs : hStorm, &dwHigh);
    o[k].dwErr = BothErrors() ^ dwHigh;
    if (!k)
      cbFile = static_cast<DWORD>(o[k].nRet);
  }
  Compare("SFileGetFileSize", o[0], o[1], what);
  const DWORD cch = static_cast<DWORD>(RandLimit(40));
  for (int f = 0; f < 2; ++f) {
    for (int k = 0; k < 2; ++k) {
      std::vector<unsigned char> d = Buffer(300, 0x5A);
      ClearErrors();
      HANDLE h = k ? hOurs : hStorm;
      if (f == 1) {
        HANDLE hMpq = NULL;
        const BOOL bOk = (k ? SFileGetFileArchive : Ord<PFN_FARCHIVE>(264))(h, &hMpq);
        h = hMpq;
        d[0] = static_cast<unsigned char>((hMpq ? 1 : 0) + 2 * bOk);
      }
      o[k].nRet = h ? (k ? (f ? SFileGetArchiveName : SFileGetFileName) : Ord<PFN_FNAME>(f ? 275 : 276))(
                          h, reinterpret_cast<char *>(&d[GUARD]), f ? 300 : cch)
                    : -1;
      o[k].dwErr = BothErrors();
      o[k].dest.swap(d);
    }
    Compare(f ? "SFileGetArchiveName" : "SFileGetFileName", o[0], o[1], what + " cch " + std::to_string(cch));
  }
  // reads from the start, random lengths, until one comes up short
  for (int r = 0; r < 8; ++r) {
    const DWORD cb = RandBelow(3) ? 1 + RandBelow(cbFile / 3 + 16) : 1 + RandBelow(64);
    for (int k = 0; k < 2; ++k) {
      std::vector<unsigned char> d = Buffer(cb, 0x5A);
      DWORD cbRead = 0xCCCC;
      ClearErrors();
      o[k].nRet = (k ? SFileReadFile : Ord<PFN_FREAD>(269))(k ? hOurs : hStorm, &d[GUARD], cb, &cbRead, NULL);
      o[k].dwErr = BothErrors();
      // storm.dll reads in pieces of 0x20000 bytes and, when the file ends inside a whole piece,
      // counts all of it; the port counts what it read. 1.17 never reads the count after a short
      // read, so a short one past 0x20000 bytes leaves it out
      if (o[k].nRet || cb <= 0x20000)
        d.insert(d.end(), reinterpret_cast<unsigned char *>(&cbRead), reinterpret_cast<unsigned char *>(&cbRead) + 4);
      o[k].dest.swap(d);
    }
    Compare("SFileReadFile", o[0], o[1], what + " read " + std::to_string(r) + " of " + std::to_string(cb));
    if (!o[0].nRet)
      break;
  }
  // a request read: the position at +8, the event at +0x10
  {
    const DWORD dwAt = RandBelow(static_cast<int>(cbFile) + 4);
    const DWORD cb = 1 + RandBelow(4096);
    for (int k = 0; k < 2; ++k) {
      std::vector<unsigned char> d = Buffer(cb, 0x5A);
      DWORD adwRequest[5] = {0, 0, dwAt, 0, 0};
      HANDLE hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
      adwRequest[4] = reinterpret_cast<DWORD>(hEvent);
      DWORD cbRead = 0xCCCC;
      ClearErrors();
      o[k].nRet = (k ? SFileReadFile : Ord<PFN_FREAD>(269))(k ? hOurs : hStorm, &d[GUARD], cb, &cbRead, adwRequest);
      const DWORD dwErr = BothErrors();
      const DWORD dwWait = WaitForSingleObject(hEvent, 5000);
      CloseHandle(hEvent);
      o[k].dwErr = dwErr ^ (dwWait << 28);
      d.insert(d.end(), reinterpret_cast<unsigned char *>(&cbRead), reinterpret_cast<unsigned char *>(&cbRead) + 4);
      o[k].dest.swap(d);
    }
    Compare("SFileReadFile, a request", o[0], o[1], what + " at " + std::to_string(dwAt) + " for " + std::to_string(cb));
  }
}

// One name looked up in both, by the archive given or through the search.
static void CompareFile(const std::string &name, HANDLE hMpqStorm, HANDLE hMpqOurs, DWORD dwScope, bool bByOpenFile)
{
  Outcome o[2];
  HANDLE ah[2] = {NULL, NULL};
  for (int k = 0; k < 2; ++k) {
    ClearErrors();
    if (bByOpenFile)
      o[k].nRet = (k ? SFileOpenFile : Ord<PFN_FOPENFILE>(267))(name.c_str(), &ah[k]);
    else
      o[k].nRet = (k ? SFileOpenFileEx : Ord<PFN_FOPENFILEEX>(268))(k ? hMpqOurs : hMpqStorm, name.c_str(), dwScope,
                                                                   &ah[k]);
    o[k].dwErr = BothErrors();
    o[k].dest.assign(1, ah[k] ? 1 : 0);
  }
  const std::string what = "\"" + name + "\" scope " + std::to_string(dwScope) + (hMpqStorm ? " in an archive" : "");
  Compare(bByOpenFile ? "SFileOpenFile" : "SFileOpenFileEx", o[0], o[1], what);
  if (ah[0] && ah[1] && !(dwScope & 4))
    CompareOpenFiles(ah[0], ah[1], what);
  if (dwScope & 4) {
    for (int k = 0; k < 2; ++k)
      if (ah[k])
        CloseHandle(ah[k]);
    return;
  }
  if (ah[0])
    Ord<PFN_FCLOSE>(253)(ah[0]);
  if (ah[1])
    SFileCloseFile(ah[1]);
}

static void CompareLoad(const std::string &name, HANDLE hMpqStorm, HANDLE hMpqOurs)
{
  const DWORD cbExtra = RandBelow(3) ? 0 : RandBelow(40);
  const bool bEx = RandBelow(2) != 0;
  Outcome o[2];
  for (int k = 0; k < 2; ++k) {
    void *pv = NULL;
    DWORD cb = 0xCCCC;
    ClearErrors();
    if (bEx)
      o[k].nRet = (k ? SFileLoadFileEx : Ord<PFN_FLOADEX>(281))(k ? hMpqOurs : hMpqStorm, name.c_str(), &pv, &cb,
                                                               cbExtra, 0, NULL);
    else
      o[k].nRet = (k ? SFileLoadFile : Ord<PFN_FLOAD>(279))(name.c_str(), &pv, &cb, cbExtra, NULL);
    o[k].dwErr = BothErrors();
    if (pv)
      o[k].dest.assign(static_cast<unsigned char *>(pv), static_cast<unsigned char *>(pv) + cb + cbExtra);
    o[k].dest.insert(o[k].dest.end(), reinterpret_cast<unsigned char *>(&cb), reinterpret_cast<unsigned char *>(&cb) + 4);
    if (pv)
      (k ? SFileUnloadFile : Ord<PFN_FUNLOAD>(280))(pv);
  }
  Compare(bEx ? "SFileLoadFileEx" : "SFileLoadFile", o[0], o[1], "\"" + name + "\" extra " + std::to_string(cbExtra));
}

// A map's archive: every file its list names, and its scenario.
static void CompareMap(const std::string &path)
{
  Outcome o[2];
  HANDLE ah[2] = {NULL, NULL};
  for (int k = 0; k < 2; ++k) {
    ClearErrors();
    o[k].nRet = (k ? SFileOpenArchive : Ord<PFN_FOPENARCHIVE>(266))(path.c_str(), 0, 0, &ah[k]);
    o[k].dwErr = BothErrors();
  }
  Compare("SFileOpenArchive, a map", o[0], o[1], path);
  if (ah[0] && ah[1]) {
    std::vector<std::string> names;
    names.push_back("staredit\\scenario.chk");
    names.push_back("(listfile)");
    void *pv = NULL;
    DWORD cb = 0;
    if (Ord<PFN_FLOADEX>(281)(ah[0], "(listfile)", &pv, &cb, 1, 0, NULL)) {
      for (char *p = static_cast<char *>(pv); *p;) {
        char *pEnd = p + strcspn(p, "\r\n;");
        if (pEnd > p)
          names.push_back(std::string(p, pEnd));
        p = *pEnd ? pEnd + 1 : pEnd;
      }
      Ord<PFN_FUNLOAD>(280)(pv);
    }
    for (size_t i = 0; i < names.size(); ++i) {
      CompareFile(names[i], ah[0], ah[1], 0, false);
      CompareLoad(names[i], ah[0], ah[1]);
    }
    // the scenario through the search, which reaches the map last
    CompareFile("staredit\\scenario.chk", NULL, NULL, 0, false);
  }
  if (ah[0])
    Ord<PFN_FCLOSE>(252)(ah[0]);
  if (ah[1])
    SFileCloseArchive(ah[1]);
}

// Files on disk, in the test's own folder, which is Storm's base path here.
static void DiskCases(void)
{
  char szExe[MAX_PATH];
  GetModuleFileNameA(NULL, szExe, MAX_PATH);
  std::string base(szExe);
  base.resize(base.find_last_of('\\') + 1);
  const std::string sub = base + "storm_test_dir";
  CreateDirectoryA(sub.c_str(), NULL);
  std::vector<unsigned char> data(5000 + RandBelow(20000));
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = static_cast<unsigned char>(Rand32());
  const std::string files[2] = {base + "storm_test_disk.bin", sub + "\\storm_test_sub.bin"};
  for (int i = 0; i < 2; ++i) {
    FILE *f = fopen(files[i].c_str(), "wb");
    fwrite(&data[0], 1, data.size() - i * 100, f);
    fclose(f);
  }
  static const char *s_aszNames[] = {"storm_test_disk.bin", "storm_test_dir\\storm_test_sub.bin",
                                     "any\\folder\\storm_test_disk.bin", "storm_test_sub.bin",
                                     "storm_test_dir", "arr\\units.dat", "no_such_file.bin"};
  for (size_t i = 0; i < sizeof s_aszNames / sizeof *s_aszNames; ++i)
    for (DWORD dwScope = 0; dwScope < 8; ++dwScope) {
      CompareFile(s_aszNames[i], NULL, NULL, dwScope, false);
      if (dwScope == 4 || dwScope == 5 || dwScope == 7) {
        // a Win32 handle: what it reads from where it stands
        HANDLE ah[2] = {NULL, NULL};
        Outcome o[2];
        for (int k = 0; k < 2; ++k) {
          (k ? SFileOpenFileEx : Ord<PFN_FOPENFILEEX>(268))(NULL, s_aszNames[i], dwScope, &ah[k]);
          std::vector<unsigned char> d(64, 0);
          DWORD cbRead = 0;
          o[k].nRet = ah[k] ? ReadFile(ah[k], &d[0], 64, &cbRead, NULL) : -1;
          o[k].dwErr = cbRead;
          o[k].dest.swap(d);
          if (ah[k])
            CloseHandle(ah[k]);
        }
        Compare("SFileOpenFileEx, a Win32 handle", o[0], o[1], s_aszNames[i]);
      }
    }
  CompareFile(files[0], NULL, NULL, 1, false);
  CompareFile(files[1], NULL, NULL, 1, false);
  CompareFile(files[0], NULL, NULL, 0, false);
  DeleteFileA(files[0].c_str());
  DeleteFileA(files[1].c_str());
  RemoveDirectoryA(sub.c_str());
}

// The install and data folders.
static void PathCases(void)
{
  static const char *s_aszDirs[] = {"C:\\Games\\Hyperion\\", "C:\\Games\\Hyperion", "", "\\", "D:\\a\\b\\c\\d\\e\\"};
  for (int f = 0; f < 2; ++f)
    for (size_t i = 0; i < (f ? sizeof s_aszDirs / sizeof *s_aszDirs : 1); ++i)
      for (DWORD bSlash = 0; bSlash < 2; ++bSlash)
        for (int c = 0; c < 6; ++c) {
          static const DWORD s_acch[] = {0, 1, 4, 12, 20, MAX_PATH};
          const DWORD cch = s_acch[c];
          Outcome o[2];
          for (int k = 0; k < 2; ++k) {
            if (f) {
              ClearErrors();
              const DWORD n = (k ? SFileSetAppDataDir : Ord<PFN_FSETDIR>(297))(s_aszDirs[i]);
              o[k].dest.assign(reinterpret_cast<const unsigned char *>(&n), reinterpret_cast<const unsigned char *>(&n) + 4);
            }
            std::vector<unsigned char> d = Buffer(MAX_PATH, 0x5A);
            ClearErrors();
            o[k].nRet = (k ? (f ? SFileGetUserDataPath : SFileGetInstallPath) : Ord<PFN_FPATH>(f ? 296 : 295))(
                reinterpret_cast<char *>(&d[GUARD]), cch, bSlash);
            o[k].dwErr = BothErrors();
            o[k].dest.insert(o[k].dest.end(), d.begin(), d.end());
          }
          Compare(f ? "SFileGetUserDataPath" : "SFileGetInstallPath", o[0], o[1],
                  std::string(f ? s_aszDirs[i] : "") + " slash " + std::to_string(bSlash) + " cch " + std::to_string(cch));
        }
}

// Arguments the file functions refuse, and archives that are not there or are
// not archives.
static void FileBadArgs(const char *pszInstall)
{
  const std::string storm = std::string(pszInstall) + "\\storm.dll";
  const std::string missing = std::string(pszInstall) + "\\no_such_archive.mpq";
  const std::string cd = std::string(pszInstall) + "\\BroodWar.mpq";
  for (int f = 0; f < 10; ++f) {
    Outcome o[2];
    for (int k = 0; k < 2; ++k) {
      HANDLE h = reinterpret_cast<HANDLE>(1);
      ClearErrors();
      switch (f) {
      case 0: o[k].nRet = (k ? SFileOpenArchive : Ord<PFN_FOPENARCHIVE>(266))("", 0, 0, &h); break;
      case 1: o[k].nRet = (k ? SFileOpenArchive : Ord<PFN_FOPENARCHIVE>(266))(missing.c_str(), 0, 0, &h); break;
      case 2: o[k].nRet = (k ? SFileOpenArchive : Ord<PFN_FOPENARCHIVE>(266))(storm.c_str(), 0, 0, &h); break;
      case 3: o[k].nRet = (k ? SFileOpenArchive : Ord<PFN_FOPENARCHIVE>(266))(cd.c_str(), 0, 3, &h); break;
      case 4: o[k].nRet = (k ? SFileOpenArchive : Ord<PFN_FOPENARCHIVE>(266))("x.mpq", 0, 0, NULL); break;
      case 5: o[k].nRet = (k ? SFileOpenFileEx : Ord<PFN_FOPENFILEEX>(268))(NULL, "", 0, &h); break;
      case 6: o[k].nRet = (k ? SFileOpenFileEx : Ord<PFN_FOPENFILEEX>(268))(NULL, "arr\\units.dat", 0, NULL); break;
      case 7: o[k].nRet = (k ? SFileLoadFileEx : Ord<PFN_FLOADEX>(281))(NULL, "arr\\units.dat", NULL, NULL, 0, 0, NULL); break;
      case 8: o[k].nRet = (k ? SFileUnloadFile : Ord<PFN_FUNLOAD>(280))(NULL); break;
      default: {
        // an open file given no buffer
        HANDLE hFile = NULL;
        (k ? SFileOpenFile : Ord<PFN_FOPENFILE>(267))("arr\\units.dat", &hFile);
        ClearErrors();
        o[k].nRet = (k ? SFileReadFile : Ord<PFN_FREAD>(269))(hFile, NULL, 4, NULL, NULL);
        o[k].nRet += 2 * (k ? SFileGetFileArchive : Ord<PFN_FARCHIVE>(264))(hFile, NULL);
        o[k].nRet += 4 * (k ? SFileGetFileName : Ord<PFN_FNAME>(276))(hFile, NULL, 5);
        o[k].dwErr = BothErrors();
        (k ? SFileCloseFile : Ord<PFN_FCLOSE>(253))(hFile);
        h = NULL;
        break;
      }
      }
      if (f < 9)
        o[k].dwErr = BothErrors();
      o[k].dest.push_back(h == NULL ? 0 : h == reinterpret_cast<HANDLE>(1) ? 1 : 2);
      if (h && h != reinterpret_cast<HANDLE>(1))
        (k ? SFileCloseArchive : Ord<PFN_FCLOSE>(252))(h);
    }
    Compare("file, bad arguments", o[0], o[1], "case " + std::to_string(f));
  }
}

// An archive closed while one of its files is open: the file still reads.
static void CloseOrderCase(const char *pszInstall)
{
  const std::string path = std::string(pszInstall) + "\\StarDat.mpq";
  Outcome o[2];
  for (int k = 0; k < 2; ++k) {
    HANDLE hMpq = NULL, hFile = NULL;
    (k ? SFileOpenArchive : Ord<PFN_FOPENARCHIVE>(266))(path.c_str(), 5, 0, &hMpq);
    (k ? SFileOpenFileEx : Ord<PFN_FOPENFILEEX>(268))(hMpq, "arr\\units.dat", 0, &hFile);
    const BOOL bClosed = (k ? SFileCloseArchive : Ord<PFN_FCLOSE>(252))(hMpq);
    std::vector<unsigned char> d = Buffer(4096, 0x5A);
    DWORD cbRead = 0;
    ClearErrors();
    o[k].nRet = (k ? SFileReadFile : Ord<PFN_FREAD>(269))(hFile, &d[GUARD], 4096, &cbRead, NULL) + 2 * bClosed;
    o[k].dwErr = BothErrors() ^ cbRead;
    o[k].dest.swap(d);
    (k ? SFileCloseFile : Ord<PFN_FCLOSE>(253))(hFile);
  }
  Compare("SFileCloseArchive with a file open", o[0], o[1], "");
}

// Two archives at one priority, above the game's: the one opened last is
// searched first, so a name both hold comes from it.
static void PriorityCase(const char *pszInstall)
{
  static const char *s_aszShared[] = {"arr\\units.dat", "arr\\images.dat", "rez\\stat_txt.tbl", "arr\\sfxdata.tbl",
                                      "game\\icons.grp", "unit\\cmdbtns\\cmdicons.grp"};
  for (int nOrder = 0; nOrder < 2; ++nOrder) {
    HANDLE ah[2][2] = {{NULL, NULL}, {NULL, NULL}};
    for (int k = 0; k < 2; ++k)
      for (int i = 0; i < 2; ++i) {
        const std::string path = std::string(pszInstall) + "\\" + s_aArchives[i ^ nOrder].pszFile;
        (k ? SFileOpenArchive : Ord<PFN_FOPENARCHIVE>(266))(path.c_str(), 0x3000, 0, &ah[k][i]);
      }
    for (size_t n = 0; n < sizeof s_aszShared / sizeof *s_aszShared; ++n) {
      Outcome o[2];
      for (int k = 0; k < 2; ++k) {
        HANDLE hFile = NULL, hMpq = NULL;
        std::vector<unsigned char> d = Buffer(MAX_PATH, 0);
        if ((k ? SFileOpenFile : Ord<PFN_FOPENFILE>(267))(s_aszShared[n], &hFile)) {
          (k ? SFileGetFileArchive : Ord<PFN_FARCHIVE>(264))(hFile, &hMpq);
          (k ? SFileGetArchiveName : Ord<PFN_FNAME>(275))(hMpq, reinterpret_cast<char *>(&d[GUARD]), MAX_PATH);
          o[k].nRet = (k ? SFileGetFileSize : Ord<PFN_FSIZE>(265))(hFile, NULL);
          (k ? SFileCloseFile : Ord<PFN_FCLOSE>(253))(hFile);
        }
        o[k].dwErr = 0;
        o[k].dest.swap(d);
      }
      Compare("archives of one priority", o[0], o[1], std::string(s_aszShared[n]) + " order " + std::to_string(nOrder));
    }
    for (int k = 0; k < 2; ++k)
      for (int i = 0; i < 2; ++i)
        (k ? SFileCloseArchive : Ord<PFN_FCLOSE>(252))(ah[k][i]);
  }
}

// Every map in a folder and the folders in it; how many.
static int CompareMapsIn(const std::string &folder)
{
  int nMaps = 0;
  WIN32_FIND_DATAA fd;
  HANDLE hFind = FindFirstFileA((folder + "\\*").c_str(), &fd);
  if (hFind == INVALID_HANDLE_VALUE)
    return 0;
  do {
    const std::string name = fd.cFileName;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (name != "." && name != "..")
        nMaps += CompareMapsIn(folder + "\\" + name);
    } else if (name.size() > 4 && (_stricmp(name.c_str() + name.size() - 4, ".scm") == 0 ||
                                   _stricmp(name.c_str() + name.size() - 4, ".scx") == 0)) {
      CompareMap(folder + "\\" + name);
      ++nMaps;
    }
  } while (FindNextFileA(hFind, &fd));
  FindClose(hFind);
  return nMaps;
}

static void FileCases(const char *pszInstall, int nRounds, const std::vector<std::string> &mapFolders)
{
  PathCases();
  FileBadArgs(pszInstall);
  CloseOrderCase(pszInstall);
  PriorityCase(pszInstall);
  // every name, through the search and in each archive
  int nFound = 0;
  for (size_t i = 0; i < s_names.size(); ++i) {
    HANDLE h;
    if (Ord<PFN_FOPENFILE>(267)(s_names[i].c_str(), &h)) {
      ++nFound;
      Ord<PFN_FCLOSE>(253)(h);
    }
    CompareFile(s_names[i], NULL, NULL, 0, i % 2 == 0);
    if (i % 10 == 0 && s_names[i].find('\\') != std::string::npos) {
      // with '/', which Storm's hash does not read as '\'
      std::string slashed(s_names[i]);
      for (size_t c = 0; c < slashed.size(); ++c)
        if (slashed[c] == '\\')
          slashed[c] = '/';
      CompareFile(slashed, NULL, NULL, 0, false);
    }
    const TestArchive &a = s_aArchives[i % 2];
    CompareFile(s_names[i], a.hStorm, a.hOurs, 0, false);
    if (i % 4 == 0)
      CompareLoad(s_names[i], a.hStorm, a.hOurs);
  }
  printf("files: %d of %u names found in the archives\n", nFound, static_cast<unsigned>(s_names.size()));
  // the names as the game might misspell them
  for (int r = 0; r < nRounds / 20 + 1; ++r) {
    const std::string name = VaryName(s_names[RandBelow(static_cast<int>(s_names.size()))]);
    CompareFile(name, NULL, NULL, 0, RandBelow(2) != 0);
  }
  DiskCases();
  int nMaps = 0;
  for (size_t f = 0; f < mapFolders.size(); ++f)
    nMaps += CompareMapsIn(mapFolders[f]);
  printf("maps: %d\n", nMaps);
}

// ---------------------------------------------------------------------------
// Dda. Each side streams onto a DirectSound of the test's own, whose buffers are
// memory and whose play cursor the test moves. After each move both sides are
// brought up to the cursor, and their buffers, the calls they made on them and
// the positions they report are compared.

extern "C" {
BOOL __stdcall SFileDdaInitialize(IDirectSound *);
BOOL __stdcall SFileDdaDestroy(void);
BOOL __stdcall SFileDdaBeginEx(HANDLE, DWORD, DWORD, DWORD, LONG, LONG, DWORD);
BOOL __stdcall SFileDdaEnd(HANDLE);
BOOL __stdcall SFileDdaGetPos(HANDLE, DWORD *, DWORD *);
BOOL __stdcall SFileDdaSetVolume(HANDLE, LONG, LONG);
void __cdecl FileDdaService(void);
}

typedef BOOL(__stdcall *PFN_DDAINIT)(IDirectSound *);
typedef BOOL(__stdcall *PFN_DDADESTROY)(void);
typedef BOOL(__stdcall *PFN_DDABEGIN)(HANDLE, DWORD, DWORD, DWORD, LONG, LONG, DWORD);
typedef BOOL(__stdcall *PFN_DDAEND)(HANDLE);
typedef BOOL(__stdcall *PFN_DDAGETPOS)(HANDLE, DWORD *, DWORD *);
typedef BOOL(__stdcall *PFN_DDASETVOLUME)(HANDLE, LONG, LONG);

// A sound buffer in memory, starting as 0xCD, with the play cursor the test
// sets. It notes Play, SetVolume, SetPan, Stop and its last Release in order.
struct FakeBuffer : IDirectSoundBuffer {
  volatile LONG nRefs;
  volatile LONG nUnlocks;
  volatile DWORD dwPlay;
  std::vector<unsigned char> bytes;
  std::string calls;
  CRITICAL_SECTION cs;

  explicit FakeBuffer(DWORD cb) : nRefs(1), nUnlocks(0), dwPlay(0), bytes(cb, 0xCD)
  {
    InitializeCriticalSection(&cs);
  }
  void Note(const char *pszFormat, long n)
  {
    char sz[32];
    sprintf(sz, pszFormat, n);
    EnterCriticalSection(&cs);
    calls += sz;
    LeaveCriticalSection(&cs);
  }
  std::string Calls()
  {
    EnterCriticalSection(&cs);
    std::string s(calls);
    LeaveCriticalSection(&cs);
    return s;
  }
  STDMETHODIMP QueryInterface(REFIID, LPVOID *ppv)
  {
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&nRefs); }
  STDMETHODIMP_(ULONG) Release()
  {
    const LONG n = InterlockedDecrement(&nRefs);
    if (n == 0)
      Note("R;", 0);
    return n;
  }
  STDMETHODIMP GetCaps(LPDSBCAPS) { return DSERR_UNSUPPORTED; }
  STDMETHODIMP GetCurrentPosition(LPDWORD pdwPlay, LPDWORD pdwWrite)
  {
    if (pdwPlay)
      *pdwPlay = dwPlay;
    if (pdwWrite)
      *pdwWrite = (dwPlay + 0x1000) % bytes.size();
    return DS_OK;
  }
  STDMETHODIMP GetFormat(LPWAVEFORMATEX, DWORD, LPDWORD) { return DSERR_UNSUPPORTED; }
  STDMETHODIMP GetVolume(LPLONG pl)
  {
    *pl = 0;
    return DS_OK;
  }
  STDMETHODIMP GetPan(LPLONG pl)
  {
    *pl = 0;
    return DS_OK;
  }
  STDMETHODIMP GetFrequency(LPDWORD pdw)
  {
    *pdw = 22050;
    return DS_OK;
  }
  STDMETHODIMP GetStatus(LPDWORD pdw)
  {
    *pdw = 0;
    return DS_OK;
  }
  STDMETHODIMP Initialize(LPDIRECTSOUND, LPCDSBUFFERDESC) { return DSERR_ALREADYINITIALIZED; }
  STDMETHODIMP Lock(DWORD dwOffset, DWORD cb, LPVOID *ppv1, LPDWORD pcb1, LPVOID *ppv2, LPDWORD pcb2, DWORD)
  {
    const DWORD cbAll = static_cast<DWORD>(bytes.size());
    if (!cb || cb > cbAll || dwOffset >= cbAll || !ppv1 || !pcb1)
      return DSERR_INVALIDPARAM;
    const DWORD cbFirst = cb < cbAll - dwOffset ? cb : cbAll - dwOffset;
    if (cb > cbFirst && !ppv2)
      return DSERR_INVALIDPARAM;
    *ppv1 = &bytes[dwOffset];
    *pcb1 = cbFirst;
    if (ppv2)
      *ppv2 = cb > cbFirst ? &bytes[0] : NULL;
    if (pcb2)
      *pcb2 = cb - cbFirst;
    return DS_OK;
  }
  STDMETHODIMP Play(DWORD, DWORD, DWORD dwFlags)
  {
    Note("P%ld;", static_cast<long>(dwFlags));
    return DS_OK;
  }
  STDMETHODIMP SetCurrentPosition(DWORD dw)
  {
    dwPlay = dw;
    return DS_OK;
  }
  STDMETHODIMP SetFormat(LPCWAVEFORMATEX) { return DSERR_UNSUPPORTED; }
  STDMETHODIMP SetVolume(LONG l)
  {
    Note("V%ld;", l);
    return DS_OK;
  }
  STDMETHODIMP SetPan(LONG l)
  {
    Note("N%ld;", l);
    return DS_OK;
  }
  STDMETHODIMP SetFrequency(DWORD) { return DS_OK; }
  STDMETHODIMP Stop()
  {
    Note("S;", 0);
    return DS_OK;
  }
  STDMETHODIMP Unlock(LPVOID, DWORD, LPVOID, DWORD)
  {
    InterlockedIncrement(&nUnlocks);
    return DS_OK;
  }
  STDMETHODIMP Restore() { return DS_OK; }
};

// The DirectSound the buffers come from; it keeps what each was asked for.
struct FakeSound : IDirectSound {
  std::vector<FakeBuffer *> made;
  std::vector<unsigned char> asked;  // the last description, and the format it points to
  bool bFail;

  FakeSound() : bFail(false) {}
  STDMETHODIMP QueryInterface(REFIID, LPVOID *ppv)
  {
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() { return 2; }
  STDMETHODIMP_(ULONG) Release() { return 1; }
  STDMETHODIMP CreateSoundBuffer(LPCDSBUFFERDESC pDesc, LPDIRECTSOUNDBUFFER *pp, LPUNKNOWN)
  {
    // storm.dll's description is the first version's, 0x14 bytes
    const unsigned char *p = reinterpret_cast<const unsigned char *>(pDesc);
    asked.assign(p, p + 0x10);
    if (pDesc->lpwfxFormat) {
      const unsigned char *f = reinterpret_cast<const unsigned char *>(pDesc->lpwfxFormat);
      asked.insert(asked.end(), f, f + sizeof(WAVEFORMATEX));
    }
    *pp = NULL;
    if (bFail)
      return DSERR_OUTOFMEMORY;
    FakeBuffer *pBuffer = new FakeBuffer(pDesc->dwBufferBytes);
    made.push_back(pBuffer);
    *pp = pBuffer;
    return DS_OK;
  }
  STDMETHODIMP GetCaps(LPDSCAPS) { return DSERR_UNSUPPORTED; }
  STDMETHODIMP DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER, LPDIRECTSOUNDBUFFER *) { return DSERR_UNSUPPORTED; }
  STDMETHODIMP SetCooperativeLevel(HWND, DWORD) { return DS_OK; }
  STDMETHODIMP Compact() { return DS_OK; }
  STDMETHODIMP GetSpeakerConfig(LPDWORD pdw)
  {
    *pdw = 0;
    return DS_OK;
  }
  STDMETHODIMP SetSpeakerConfig(DWORD) { return DS_OK; }
  STDMETHODIMP Initialize(LPCGUID) { return DSERR_ALREADYINITIALIZED; }
};

static FakeSound s_aSound[2];
static HANDLE s_hPoke;  // a file of storm.dll's in an archive its worker reads

// storm.dll's stream for a file of its own: its list's first node at
// 0x150665CC, a node's next at +4 and its file at +8; the reference count, one
// and one more for each piece waiting to be read, at +0x3C.
static const BYTE *StormStream(HANDLE hFile)
{
  const BYTE *p = *reinterpret_cast<BYTE **>(reinterpret_cast<BYTE *>(s_hStorm) + 0x665CC);
  while (reinterpret_cast<intptr_t>(p) > 0) {
    if (*reinterpret_cast<const HANDLE *>(p + 8) == hFile)
      return p;
    p = *reinterpret_cast<BYTE *const *>(p + 4);
  }
  return NULL;
}

// A read by request, which storm.dll's worker takes after a pass over its
// streams.
static void StormPoke(void)
{
  BYTE ab[16];
  DWORD adwRequest[5] = {0, 0, 0, 0, 0};
  HANDLE hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
  adwRequest[4] = reinterpret_cast<DWORD>(hEvent);
  DWORD cbRead;
  Ord<PFN_FREAD>(269)(s_hPoke, ab, sizeof ab, &cbRead, adwRequest);
  WaitForSingleObject(hEvent, 20000);
  CloseHandle(hEvent);
}

// Waits for storm.dll's worker to catch up with the cursors: no piece waiting,
// and two passes in a row that wrote nothing.
static bool StormSettle(const std::vector<HANDLE> &files, const std::vector<FakeBuffer *> &buffers)
{
  const DWORD dwStart = GetTickCount();
  for (int nQuiet = 0; nQuiet < 2;) {
    if (GetTickCount() - dwStart > 60000)
      return false;
    LONG nBefore = 0;
    for (size_t i = 0; i < buffers.size(); ++i)
      nBefore += buffers[i]->nUnlocks;
    StormPoke();
    for (;;) {
      bool bWaiting = false;
      for (size_t i = 0; i < files.size(); ++i) {
        const BYTE *p = StormStream(files[i]);
        if (p && *reinterpret_cast<const LONG *>(p + 0x3C) > 1)
          bWaiting = true;
      }
      if (!bWaiting)
        break;
      if (GetTickCount() - dwStart > 60000)
        return false;
      Sleep(1);
    }
    LONG nAfter = 0;
    for (size_t i = 0; i < buffers.size(); ++i)
      nAfter += buffers[i]->nUnlocks;
    nQuiet = nAfter == nBefore ? nQuiet + 1 : 0;
  }
  return true;
}

static LONG RandLevel(void)
{
  switch (RandBelow(6)) {
  case 0: return 0;
  case 1: return 0x7FFFFFFF;
  case 2: return static_cast<LONG>(Rand32());
  default: return RandBelow(10001) - (RandBelow(2) ? 10000 : 0);
  }
}

static DWORD s_dwSettleMax;

// One or two sounds streamed at once, as the music and a voice are, and played
// through in steps of the cursor.
static void DdaCase(const std::vector<std::string> &names, int nSteps)
{
  struct Side {
    HANDLE hFile;
    FakeBuffer *pBuffer;
  };
  std::vector<Side> side[2];
  std::vector<HANDLE> stormFiles;
  std::vector<FakeBuffer *> stormBuffers;
  std::string what;
  for (size_t n = 0; n < names.size(); ++n) {
    HANDLE h[2] = {NULL, NULL};
    Ord<PFN_FOPENFILE>(267)(names[n].c_str(), &h[0]);
    SFileOpenFile(names[n].c_str(), &h[1]);
    if (!h[0] || !h[1]) {
      if (h[0])
        Ord<PFN_FCLOSE>(253)(h[0]);
      if (h[1])
        SFileCloseFile(h[1]);
      continue;
    }
    const DWORD cbFile = SFileGetFileSize(h[1], NULL);
    DWORD cbBuffer = 0x40000;
    switch (RandBelow(8)) {
    case 0: cbBuffer = RandBelow(0x9000); break;
    case 1: cbBuffer = 1 + RandBelow(0x50000); break;
    }
    DWORD dwFlags = (RandBelow(2) ? 0x40000 : 0) | (RandBelow(4) == 0 ? 0x20 : 0) | (RandBelow(4) == 0 ? 0x40 : 0);
    if (RandBelow(10) == 0)
      dwFlags |= Rand32() & ~0x40000u;
    // A stream that loops from the end of its data or past it has storm.dll's
    // worker queue empty reads for ever; the game starts every stream at 0.
    DWORD dwStart = 0;
    if (RandBelow(4) == 0) {
      if (dwFlags & 0x40000)
        dwStart = RandBelow(cbFile > 400 ? static_cast<int>(cbFile - 200) / 2 : 1);
      else if (RandBelow(2))
        dwStart = cbFile - RandBelow(200) + RandBelow(400);
      else
        dwStart = RandBelow(static_cast<int>(cbFile) + 0x1000);
    }
    const LONG lVolume = RandLevel(), lPan = RandLevel();
    const bool bFail = RandBelow(25) == 0;
    char sz[160];
    sprintf(sz, "%s buffer %u flags %x start %u volume %ld pan %ld%s", names[n].c_str(), cbBuffer, dwFlags, dwStart,
            lVolume, lPan, bFail ? " no buffer" : "");
    what += std::string(what.empty() ? "" : ", ") + sz;
    Outcome o[2];
    FakeBuffer *pMade[2] = {NULL, NULL};
    for (int k = 0; k < 2; ++k) {
      s_aSound[k].bFail = bFail;
      s_aSound[k].asked.clear();
      const size_t nMade = s_aSound[k].made.size();
      ClearErrors();
      o[k].nRet = (k ? SFileDdaBeginEx : Ord<PFN_DDABEGIN>(255))(h[k], cbBuffer, dwFlags, dwStart, lVolume, lPan, 0);
      o[k].dwErr = BothErrors();
      o[k].dest = s_aSound[k].asked;
      if (s_aSound[k].made.size() > nMade)
        pMade[k] = s_aSound[k].made.back();
    }
    s_aSound[0].bFail = s_aSound[1].bFail = false;
    Compare("SFileDdaBeginEx", o[0], o[1], sz);
    // A stream that loops on no data (its start, +0x20, at or past its end, +0x24,
    // with the loop flag, +0x14, set) has storm.dll's worker write nothing for
    // ever, and is only begun and ended.
    bool bStep = o[0].nRet && o[1].nRet && pMade[0] && pMade[1];
    if (bStep) {
      const BYTE *p = StormStream(h[0]);
      bStep = p && !(*reinterpret_cast<const DWORD *>(p + 0x14) &&
                     *reinterpret_cast<const DWORD *>(p + 0x20) >= *reinterpret_cast<const DWORD *>(p + 0x24));
    }
    for (int k = 0; k < 2; ++k) {
      Side s = {h[k], bStep ? pMade[k] : NULL};
      side[k].push_back(s);
    }
    if (bStep) {
      stormFiles.push_back(h[0]);
      stormBuffers.push_back(pMade[0]);
    }
  }

  for (int nStep = 0; nStep <= nSteps && !stormFiles.empty(); ++nStep) {
    FileDdaService();
    const DWORD dwSettle = GetTickCount();
    if (!StormSettle(stormFiles, stormBuffers)) {
      Diff("SFileDda", what + ": storm.dll's worker did not catch up");
      break;
    }
    if (GetTickCount() - dwSettle > s_dwSettleMax)
      s_dwSettleMax = GetTickCount() - dwSettle;
    for (size_t n = 0; n < side[0].size(); ++n) {
      if (!side[0][n].pBuffer)
        continue;
      Outcome o[2];
      for (int k = 0; k < 2; ++k) {
        FakeBuffer *pBuffer = side[k][n].pBuffer;
        DWORD dwPos = 0xCCCC, dwEnd = 0xCCCC;
        ClearErrors();
        o[k].nRet = (k ? SFileDdaGetPos : Ord<PFN_DDAGETPOS>(258))(side[k][n].hFile, &dwPos, &dwEnd);
        o[k].dwErr = BothErrors();
        o[k].dest = pBuffer->bytes;
        const std::string calls = pBuffer->Calls();
        o[k].dest.insert(o[k].dest.end(), calls.begin(), calls.end());
        o[k].dest.insert(o[k].dest.end(), reinterpret_cast<unsigned char *>(&dwPos),
                         reinterpret_cast<unsigned char *>(&dwPos) + 4);
        o[k].dest.insert(o[k].dest.end(), reinterpret_cast<unsigned char *>(&dwEnd),
                         reinterpret_cast<unsigned char *>(&dwEnd) + 4);
      }
      Compare("SFileDda stream", o[0], o[1], what + " step " + std::to_string(nStep));
      if (!(o[0] == o[1]))
        nStep = nSteps;
    }
    // the cursors move on, mostly by less than a piece and now and then far
    for (size_t n = 0; n < side[0].size(); ++n) {
      if (!side[0][n].pBuffer)
        continue;
      const DWORD cb = static_cast<DWORD>(side[0][n].pBuffer->bytes.size());
      const DWORD dwBy = RandBelow(4) == 0 ? RandBelow(static_cast<int>(cb)) : RandBelow(0x6000);
      const DWORD dwPlay = (side[0][n].pBuffer->dwPlay + dwBy) % cb;
      side[0][n].pBuffer->dwPlay = side[1][n].pBuffer->dwPlay = dwPlay;
      if (RandBelow(5) == 0) {
        const LONG lVolume = RandLevel(), lPan = RandLevel();
        Outcome o[2];
        for (int k = 0; k < 2; ++k) {
          ClearErrors();
          o[k].nRet = (k ? SFileDdaSetVolume : Ord<PFN_DDASETVOLUME>(261))(side[k][n].hFile, lVolume, lPan);
          o[k].dwErr = BothErrors();
        }
        Compare("SFileDdaSetVolume", o[0], o[1], what);
      }
    }
  }

  // ended: the buffer let go, and the file has no stream
  for (size_t n = 0; n < side[0].size(); ++n) {
    Outcome o[2];
    for (int k = 0; k < 2; ++k) {
      ClearErrors();
      o[k].nRet = (k ? SFileDdaEnd : Ord<PFN_DDAEND>(257))(side[k][n].hFile);
      DWORD dwPos = 0xCCCC, dwEnd = 0xCCCC;
      o[k].nRet += 2 * (k ? SFileDdaGetPos : Ord<PFN_DDAGETPOS>(258))(side[k][n].hFile, &dwPos, &dwEnd);
      o[k].nRet += 4 * (k ? SFileDdaSetVolume : Ord<PFN_DDASETVOLUME>(261))(side[k][n].hFile, -100, 0);
      o[k].dwErr = BothErrors() ^ dwPos ^ dwEnd;
      if (side[k][n].pBuffer) {
        const std::string calls = side[k][n].pBuffer->Calls();
        o[k].dest.assign(calls.begin(), calls.end());
      }
      (k ? SFileCloseFile : Ord<PFN_FCLOSE>(253))(side[k][n].hFile);
    }
    Compare("SFileDdaEnd", o[0], o[1], what);
  }
}

// The Dda functions' refusals: no DirectSound yet, no buffer size, a file on
// disk, a file that is not a WAV; and what a stream that did not start leaves
// its archive doing with reads by request.
static void DdaBadArgs(const char *pszInstall, bool bInitialized)
{
  const std::string storm = std::string(pszInstall) + "\\storm.dll";
  const std::string stardat = std::string(pszInstall) + "\\StarDat.mpq";
  for (int f = 0; f < 6; ++f) {
    Outcome o[2];
    for (int k = 0; k < 2; ++k) {
      PFN_DDABEGIN pfnBegin = k ? SFileDdaBeginEx : Ord<PFN_DDABEGIN>(255);
      HANDLE hMpq = NULL, hFile = NULL;
      if (f == 3 || f == 4 || f == 5) {
        // an archive of its own, opened with flag 0, whose reads by request are
        // answered at once until a stream begins on one of its files
        (k ? SFileOpenArchive : Ord<PFN_FOPENARCHIVE>(266))(stardat.c_str(), 0, 0, &hMpq);
        (k ? SFileOpenFileEx : Ord<PFN_FOPENFILEEX>(268))(hMpq, f == 5 ? "sound\\misc\\buzz.wav" : "arr\\units.dat", 0,
                                                          &hFile);
      } else if (f == 2) {
        (k ? SFileOpenFileEx : Ord<PFN_FOPENFILEEX>(268))(NULL, storm.c_str(), 1, &hFile);
      } else {
        (k ? SFileOpenFile : Ord<PFN_FOPENFILE>(267))("music\\title.wav", &hFile);
      }
      // storm.dll's reads the file it is given without a test
      ClearErrors();
      o[k].nRet = hFile ? pfnBegin(hFile, f == 1 || f == 4 ? 0 : 0x40000, 0, 0, 0, 0, 0) : -1;
      o[k].dwErr = BothErrors();
      if (hMpq && hFile) {
        BYTE ab[64];
        DWORD adwRequest[5] = {0, 0, 16, 0, 0};
        HANDLE hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        adwRequest[4] = reinterpret_cast<DWORD>(hEvent);
        DWORD cbRead = 0xCCCC;
        const BOOL bOk = (k ? SFileReadFile : Ord<PFN_FREAD>(269))(hFile, ab, sizeof ab, &cbRead, adwRequest);
        WaitForSingleObject(hEvent, 5000);
        CloseHandle(hEvent);
        o[k].dest.push_back(static_cast<unsigned char>(bOk));
        o[k].dest.insert(o[k].dest.end(), reinterpret_cast<unsigned char *>(&cbRead),
                         reinterpret_cast<unsigned char *>(&cbRead) + 4);
      }
      if (hFile) {
        (k ? SFileDdaEnd : Ord<PFN_DDAEND>(257))(hFile);
        (k ? SFileCloseFile : Ord<PFN_FCLOSE>(253))(hFile);
      }
      if (hMpq)
        (k ? SFileCloseArchive : Ord<PFN_FCLOSE>(252))(hMpq);
    }
    Compare("SFileDdaBeginEx, refused", o[0], o[1],
            "case " + std::to_string(f) + (bInitialized ? "" : ", no DirectSound"));
  }
}

// StormLib's writing, from stormlib.lib, where its names have an Sl prefix
// (src117/port/data/port_stormlib_names.h).
extern "C" {
bool __stdcall SlFileCreateArchive(const char *, DWORD, DWORD, HANDLE *);
bool __stdcall SlFileCreateFile(HANDLE, const char *, ULONGLONG, DWORD, LCID, DWORD, HANDLE *);
bool __stdcall SlFileWriteFile(HANDLE, const void *, DWORD, DWORD);
bool __stdcall SlFileFinishFile(HANDLE);
bool __stdcall SlFileCloseArchive(HANDLE);
}

static void PutBytes(std::vector<unsigned char> &v, const void *pv, size_t cb)
{
  v.insert(v.end(), static_cast<const unsigned char *>(pv), static_cast<const unsigned char *>(pv) + cb);
}

static void PutChunk(std::vector<unsigned char> &v, const char *pszId, DWORD cbField, DWORD cb)
{
  PutBytes(v, pszId, 4);
  PutBytes(v, &cbField, 4);
  for (DWORD i = 0; i < cb; ++i)
    v.push_back(static_cast<unsigned char>(Rand32()));
}

// A WAV file of the shapes the game's own do not have: 8-bit samples, a fmt
// chunk longer than 16 bytes or too short, chunks before and after the data,
// a data chunk claiming more or less than there is or none at all, a RIFF that
// is not WAVE, from a few bytes of data to more than a buffer's worth.
static std::vector<unsigned char> MakeWave(int n)
{
  std::vector<unsigned char> v;
  PutBytes(v, "RIFF\0\0\0\0", 8);
  PutBytes(v, n % 7 == 3 ? "WAVX" : "WAVE", 4);
  if (RandBelow(3) == 0) {
    const DWORD cb = 1 + RandBelow(40);
    PutChunk(v, "LIST", cb, cb);
  }
  DWORD cbFormat = n % 5 == 0 ? 18 : n % 5 == 1 ? 20 + RandBelow(8) : 16;
  if (n % 13 == 5)
    cbFormat = 14;
  const WORD wChannels = static_cast<WORD>(1 + RandBelow(2));
  const WORD wBits = n % 2 ? 8 : 16;
  static const DWORD s_adwRates[] = {11025, 22050, 44100};
  PCMWAVEFORMAT format;
  format.wf.wFormatTag = WAVE_FORMAT_PCM;
  format.wf.nChannels = wChannels;
  format.wf.nSamplesPerSec = s_adwRates[RandBelow(3)];
  format.wf.nBlockAlign = static_cast<WORD>(wChannels * wBits / 8);
  format.wf.nAvgBytesPerSec = format.wf.nSamplesPerSec * format.wf.nBlockAlign;
  format.wBitsPerSample = wBits;
  PutChunk(v, "fmt ", cbFormat, 0);
  PutBytes(v, &format, cbFormat < sizeof format ? cbFormat : sizeof format);
  for (DWORD i = sizeof format; i < cbFormat; ++i)
    v.push_back(static_cast<unsigned char>(Rand32()));
  if (RandBelow(3) == 0)
    PutChunk(v, "fact", 4, 4);
  if (n % 17 != 8) {
    DWORD cbData;
    switch (RandBelow(4)) {
    case 0: cbData = RandBelow(64); break;
    case 1: cbData = RandBelow(0x4000); break;
    case 2: cbData = RandBelow(0x20000); break;
    default: cbData = 0x40000 + RandBelow(0x30000); break;
    }
    DWORD cbField = cbData;
    if (n % 11 == 4)
      cbField = cbData + RandBelow(5000);
    else if (n % 11 == 6)
      cbField = cbData / 2;
    PutChunk(v, "data", cbField, cbData);
  }
  if (RandBelow(4) == 0)
    PutChunk(v, "LIST", 10, 10);
  const DWORD cbRiff = static_cast<DWORD>(v.size() - 8);
  memcpy(&v[4], &cbRiff, 4);
  return v;
}

static const int SYNTH_WAVES = 48;

// An archive of made-up WAV files, stored as they are, which storm.dll reads.
static bool MakeWaveArchive(const std::string &path)
{
  DeleteFileA(path.c_str());
  HANDLE hMpq = NULL;
  if (!SlFileCreateArchive(path.c_str(), 0, SYNTH_WAVES + 8, &hMpq))
    return false;
  for (int n = 0; n < SYNTH_WAVES; ++n) {
    const std::vector<unsigned char> wave = MakeWave(n);
    char szName[40];
    sprintf(szName, "synth\\wave%02d.wav", n);
    HANDLE hFile = NULL;
    if (SlFileCreateFile(hMpq, szName, 0, static_cast<DWORD>(wave.size()), 0, 0x80000000, &hFile)) {
      SlFileWriteFile(hFile, &wave[0], static_cast<DWORD>(wave.size()), 0);
      SlFileFinishFile(hFile);
    }
  }
  return SlFileCloseArchive(hMpq);
}

static void DdaCases(const char *pszInstall, int nRounds)
{
  DdaBadArgs(pszInstall, false);
  Outcome o[2];
  for (int k = 0; k < 2; ++k) {
    ClearErrors();
    o[k].nRet = (k ? SFileDdaInitialize : Ord<PFN_DDAINIT>(260))(NULL);
    o[k].dwErr = BothErrors();
    o[k].nRet += 2 * (k ? SFileDdaInitialize : Ord<PFN_DDAINIT>(260))(&s_aSound[k]);
  }
  Compare("SFileDdaInitialize", o[0], o[1], "NULL, then a DirectSound");
  DdaBadArgs(pszInstall, true);
  Ord<PFN_FOPENFILEEX>(268)(s_aArchives[0].hStorm, "arr\\units.dat", 0, &s_hPoke);

  // the sounds: the game's music and every sound its table names
  std::vector<std::string> waves;
  for (size_t i = 0; i < s_names.size(); ++i) {
    std::string lower(s_names[i]);
    for (size_t c = 0; c < lower.size(); ++c)
      lower[c] = static_cast<char>(tolower(static_cast<unsigned char>(lower[c])));
    if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".wav") == 0)
      waves.push_back(s_names[i]);
  }
  static const char *s_aszMusic[] = {"music\\title.wav", "music\\terran1.wav", "music\\terran2.wav",
                                     "music\\terran3.wav", "music\\zerg1.wav", "music\\protoss1.wav",
                                     "music\\tdefeat.wav", "music\\tvictory.wav", "music\\radiofreezerg.wav"};
  // and the made-up ones, in an archive of their own in the temporary folder
  char szTemp[MAX_PATH];
  GetTempPathA(MAX_PATH, szTemp);
  const std::string synth = std::string(szTemp) + "hyperion_storm_test.mpq";
  HANDLE hSynth[2] = {NULL, NULL};
  if (MakeWaveArchive(synth)) {
    for (int k = 0; k < 2; ++k)
      (k ? SFileOpenArchive : Ord<PFN_FOPENARCHIVE>(266))(synth.c_str(), 0x5000, 2, &hSynth[k]);
  }
  if (!hSynth[0] || !hSynth[1])
    printf("cannot make %s\n", synth.c_str());
  const int nCases = nRounds / 250 + 8;
  for (int c = 0; c < nCases; ++c) {
    std::vector<std::string> names;
    for (int n = RandBelow(3) == 0 ? 2 : 1; n > 0; --n) {
      switch (RandBelow(3)) {
      case 0: names.push_back(s_aszMusic[RandBelow(sizeof s_aszMusic / sizeof *s_aszMusic)]); break;
      case 1: names.push_back(waves[RandBelow(static_cast<int>(waves.size()))]); break;
      default: {
        char szName[40];
        sprintf(szName, "synth\\wave%02d.wav", RandBelow(SYNTH_WAVES));
        names.push_back(szName);
        break;
      }
      }
    }
    if (RandBelow(12) == 0)
      names.push_back(s_names[RandBelow(static_cast<int>(s_names.size()))]);
    DdaCase(names, 12 + RandBelow(40));
  }
  printf("dda: %d cases, storm.dll's worker caught up within %u ms\n", nCases, s_dwSettleMax);
  Ord<PFN_FCLOSE>(253)(s_hPoke);
  for (int k = 0; k < 2; ++k)
    if (hSynth[k])
      (k ? SFileCloseArchive : Ord<PFN_FCLOSE>(252))(hSynth[k]);

  for (int k = 0; k < 2; ++k) {
    ClearErrors();
    o[k].nRet = (k ? SFileDdaDestroy : Ord<PFN_DDADESTROY>(256))();
    o[k].dwErr = BothErrors();
    o[k].dest.clear();
  }
  Compare("SFileDdaDestroy", o[0], o[1], "");
  DdaBadArgs(pszInstall, false);
  DeleteFileA(synth.c_str());
}

// ---------------------------------------------------------------------------
// Images. Ours reads its files through our SFile functions, storm.dll's through
// its own; ArchivesOpen, above, opens the install's archives in both.

extern "C" {
BOOL __stdcall SBmpDecodeImage(DWORD, const void *, DWORD, void *, void *, DWORD, DWORD *, DWORD *, DWORD *);
BOOL __stdcall SBmpLoadImage(const char *, void *, void *, DWORD, DWORD *, DWORD *, DWORD *);
BOOL __stdcall SBmpAllocLoadImage(const char *, void *, void **, DWORD *, DWORD *, DWORD *, DWORD, void *);
BOOL __stdcall SBmpPadImage(void *, DWORD, int, int, int, DWORD *);
int __stdcall SBmpGetPitchForAlignment(int, int);
}
typedef BOOL(__stdcall *PFN_BMPDECODE)(DWORD, const void *, DWORD, void *, void *, DWORD, DWORD *, DWORD *, DWORD *);
typedef BOOL(__stdcall *PFN_BMPLOAD)(const char *, void *, void *, DWORD, DWORD *, DWORD *, DWORD *);
typedef BOOL(__stdcall *PFN_BMPALLOCLOAD)(const char *, void *, void **, DWORD *, DWORD *, DWORD *, DWORD, void *);
typedef BOOL(__stdcall *PFN_BMPPAD)(void *, DWORD, int, int, int, DWORD *);
typedef int(__stdcall *PFN_BMPPITCH)(int, int);

// A PCX of random size, depth, planes and line length, its data random runs and
// literals, cut short or run on now and then.
static std::vector<unsigned char> RandPcx(void)
{
  static const int s_anShape[][2] = {{8, 1}, {8, 1}, {8, 1}, {8, 3}, {1, 4}, {4, 1}, {8, 2}, {1, 1}};
  const int *pShape = s_anShape[RandBelow(8)];
  const int nBits = pShape[0], nPlanes = pShape[1];
  const int nWidth = 1 + RandBelow(RandBelow(4) ? 40 : 300), nHeight = 1 + RandBelow(40);
  int nBpl = (nWidth * nBits + 7) / 8;
  nBpl += RandBelow(4) == 0 ? RandBelow(4) : 0;
  std::vector<unsigned char> v(0x80, 0);
  v[0] = 0x0A;
  v[1] = 5;
  v[2] = 1;
  v[3] = static_cast<unsigned char>(nBits);
  const int x0 = RandBelow(3), y0 = RandBelow(3);
  v[4] = static_cast<unsigned char>(x0);
  v[6] = static_cast<unsigned char>(y0);
  v[8] = static_cast<unsigned char>(x0 + nWidth - 1);
  v[9] = static_cast<unsigned char>((x0 + nWidth - 1) >> 8);
  v[10] = static_cast<unsigned char>(y0 + nHeight - 1);
  v[11] = static_cast<unsigned char>((y0 + nHeight - 1) >> 8);
  v[0x41] = static_cast<unsigned char>(nPlanes);
  v[0x42] = static_cast<unsigned char>(nBpl);
  v[0x43] = static_cast<unsigned char>(nBpl >> 8);
  int nBytes = nBpl * nPlanes * nHeight;
  if (RandBelow(6) == 0)
    nBytes += RandBelow(2) ? RandBelow(64) : -RandBelow(nBytes / 2 + 1);
  while (nBytes > 0) {
    if (RandBelow(3) == 0) {
      const int n = 1 + RandBelow(63);
      v.push_back(static_cast<unsigned char>(0xC0 | n));
      v.push_back(static_cast<unsigned char>(Rand32()));
      nBytes -= n;
    } else {
      v.push_back(static_cast<unsigned char>(RandBelow(0xC0)));
      --nBytes;
    }
  }
  if (nBits * nPlanes == 8) {
    v.push_back(0x0C);
    for (int i = 0; i < 0x300; ++i)
      v.push_back(static_cast<unsigned char>(Rand32()));
  }
  return v;
}

static void BmpDecode(void)
{
  const std::vector<unsigned char> pcx = RandPcx();
  const DWORD nWidth = (pcx[8] | pcx[9] << 8) - (pcx[4] | pcx[5] << 8) + 1;
  const DWORD nHeight = (pcx[10] | pcx[11] << 8) - (pcx[6] | pcx[7] << 8) + 1;
  const DWORD nBpp = pcx[3] * pcx[0x41];
  const DWORD cbImage = (nBpp + 7) / 8 * nWidth * nHeight;
  const int nSize = RandBelow(4);
  const DWORD cbBuf = nSize == 0 ? cbImage - RandBelow(cbImage) : nSize == 1 ? cbImage + RandBelow(64) : cbImage;
  const bool bPalette = RandBelow(2) != 0, bBuf = RandBelow(8) != 0;
  const DWORD dwType = RandBelow(2) ? 2 : 0;
  const std::string input = std::to_string(nWidth) + "x" + std::to_string(nHeight) + " bpp " + std::to_string(nBpp) +
                            " planes " + std::to_string(pcx[0x41]) + " line " +
                            std::to_string(pcx[0x42] | pcx[0x43] << 8) + " buffer " + std::to_string(cbBuf) + " of " +
                            std::to_string(cbImage) + " file " + std::to_string(pcx.size());
  // A run that crosses the end of the output is cut there by ours; Storm's fill
  // goes on past it, up to a row's length. The output is the buffer, or the
  // image when the buffer is larger. It is compared up to its end, with room
  // behind it for Storm, and ours must leave that room alone.
  const size_t cbOut = cbBuf < cbImage ? cbBuf : cbImage;
  const size_t cbSlack = 0x10000;
  Outcome o[2];
  for (int k = 0; k < 2; ++k) {
    std::vector<unsigned char> d = Buffer(cbBuf + cbSlack, 0x5A), pal = Buffer(1024, 0x5A);
    DWORD adw[3] = {9, 9, 9};
    s_pfnSetErr(0);
    o[k].nRet = (k ? SBmpDecodeImage : Ord<PFN_BMPDECODE>(321))(dwType, &pcx[0], static_cast<DWORD>(pcx.size()),
                                                                 bPalette ? &pal[GUARD] : NULL,
                                                                 bBuf ? &d[GUARD] : NULL, bBuf ? cbBuf : 0,
                                                                 &adw[0], &adw[1], &adw[2]);
    o[k].dwErr = s_pfnGetErr();
    if (k) {
      ++g_nCases;
      for (size_t i = GUARD + cbOut; i < GUARD + cbBuf + cbSlack; ++i)
        if (d[i] != 0x5A) {
          Diff("SBmpDecodeImage", input + ": ours wrote past the output at " + std::to_string(i - GUARD));
          break;
        }
    }
    d.resize(GUARD + cbOut);
    d.insert(d.end(), pal.begin(), pal.end());
    d.insert(d.end(), reinterpret_cast<unsigned char *>(adw), reinterpret_cast<unsigned char *>(adw) + 12);
    o[k].dest.swap(d);
  }
  Compare("SBmpDecodeImage", o[0], o[1], input);
}

static void BmpPad(void)
{
  const int nWidth = 1 + RandBelow(50), nHeight = RandBelow(20), nAlign = 1 + RandBelow(8);
  const int nPitch = (nWidth + nAlign - 1) / nAlign * nAlign;
  // the padded size, one short of it now and then, or a little past it
  DWORD cb = static_cast<DWORD>(nPitch * nHeight);
  cb = cb && RandBelow(6) == 0 ? cb - 1 : cb + RandBelow(8);
  Outcome o[2];
  for (int k = 0; k < 2; ++k) {
    std::vector<unsigned char> d = Buffer(cb + 8, 0);
    for (DWORD i = 0; i < cb; ++i)
      d[GUARD + i] = static_cast<unsigned char>(i * 13 + 1);
    DWORD dwNew = 77;
    o[k].nRet = (k ? SBmpPadImage : Ord<PFN_BMPPAD>(327))(&d[GUARD], cb, nWidth, nHeight, nAlign, &dwNew);
    o[k].dwErr = 0;
    d.push_back(static_cast<unsigned char>(dwNew));
    o[k].dest.swap(d);
  }
  Compare("SBmpPadImage", o[0], o[1], std::to_string(nWidth) + "x" + std::to_string(nHeight) + " align " +
                                          std::to_string(nAlign) + " in " + std::to_string(cb));
  const int nAny = static_cast<int>(Rand32() % 5000) - 100;
  Outcome p[2];
  for (int k = 0; k < 2; ++k) {
    p[k].nRet = (k ? SBmpGetPitchForAlignment : Ord<PFN_BMPPITCH>(328))(nAny, nAlign);
    p[k].dwErr = 0;
  }
  Compare("SBmpGetPitchForAlignment", p[0], p[1], std::to_string(nAny) + " align " + std::to_string(nAlign));
}

static void *__stdcall TestAlloc(DWORD cb) { return malloc(cb ? cb : 1); }

// Arguments the image functions refuse: no data, no length, a buffer of no
// size, data of no format Storm knows, an empty name. (A null name crashes
// storm.dll's SBmpAllocLoadImage; the game never passes one.)
static void BmpBadArgs(void)
{
  const std::vector<unsigned char> pcx = RandPcx();
  const unsigned char abOther[] = "XYZW and no format at all";
  for (int f = 0; f < 9; ++f) {
    Outcome o[2];
    for (int k = 0; k < 2; ++k) {
      std::vector<unsigned char> d = Buffer(1024, 0x5A);
      DWORD adw[3] = {9, 9, 9};
      void *p = NULL;
      s_pfnSetErr(0);
      PFN_BMPDECODE pfnDecode = k ? SBmpDecodeImage : Ord<PFN_BMPDECODE>(321);
      PFN_BMPLOAD pfnLoad = k ? SBmpLoadImage : Ord<PFN_BMPLOAD>(323);
      PFN_BMPALLOCLOAD pfnAlloc = k ? SBmpAllocLoadImage : Ord<PFN_BMPALLOCLOAD>(325);
      switch (f) {
      case 0: o[k].nRet = pfnDecode(2, NULL, 100, NULL, &d[GUARD], 1024, &adw[0], &adw[1], &adw[2]); break;
      case 1: o[k].nRet = pfnDecode(2, &pcx[0], 0, NULL, &d[GUARD], 1024, &adw[0], &adw[1], &adw[2]); break;
      case 2: o[k].nRet = pfnDecode(2, &pcx[0], static_cast<DWORD>(pcx.size()), NULL, &d[GUARD], 0, &adw[0], &adw[1], &adw[2]); break;
      case 3: o[k].nRet = pfnDecode(0, abOther, sizeof abOther, NULL, &d[GUARD], 1024, &adw[0], &adw[1], &adw[2]); break;
      case 4: o[k].nRet = pfnDecode(3, &pcx[0], static_cast<DWORD>(pcx.size()), NULL, &d[GUARD], 1024, &adw[0], &adw[1], &adw[2]); break;
      case 5: o[k].nRet = pfnLoad("", NULL, &d[GUARD], 1024, &adw[0], &adw[1], &adw[2]); break;
      case 6: o[k].nRet = pfnLoad("game\\tunit.pcx", NULL, &d[GUARD], 0, &adw[0], &adw[1], &adw[2]); break;
      case 7: o[k].nRet = pfnAlloc("", NULL, &p, &adw[0], &adw[1], &adw[2], 0, reinterpret_cast<void *>(TestAlloc)); break;
      default: o[k].nRet = pfnAlloc("game\\tunit.pcx", NULL, NULL, &adw[0], &adw[1], &adw[2], 0, reinterpret_cast<void *>(TestAlloc)); break;
      }
      free(p);
      o[k].dwErr = s_pfnGetErr();
      d.insert(d.end(), reinterpret_cast<unsigned char *>(adw), reinterpret_cast<unsigned char *>(adw) + 12);
      o[k].dest.swap(d);
    }
    Compare("image, bad arguments", o[0], o[1], "case " + std::to_string(f));
  }
}

// The game's own images, from the install's archives.
static void BmpFiles(void)
{
  static const char *s_aszFile[] = {
      "game\\tunit.pcx",       "game\\tselect.pcx",          "game\\tminimap.pcx",   "game\\thpbar.pcx",
      "game\\tblink.pcx",      "game\\twire.pcx",            "game\\tfontgam.pcx",   "game\\tconsole.pcx",
      "glue\\palmm\\backgnd.pcx", "glue\\palmm\\tfont.pcx",  "glue\\campaign\\phist.pcx", "glue\\readyp\\pframe1.pcx",
      "glue\\title\\title.pcx", "unit\\cmdbtns\\ticon.pcx",  "tileset\\badlands\\dark.pcx", "tileset\\jungle\\dark.pcx",
      "game\\pconsole.pcx",    "game\\zconsole.pcx",         "glue\\palnl\\backgnd.pcx", "no\\such\\image.pcx",
      "rez\\gluexpcmpgn.bin",
  };
  int nFound = 0;
  for (size_t i = 0; i < sizeof s_aszFile / sizeof *s_aszFile; ++i) {
    // the image's size, as the game asks for it before it loads into a buffer of that size
    DWORD w = 0, h = 0, bpp = 0;
    Ord<PFN_BMPLOAD>(323)(s_aszFile[i], NULL, NULL, 0, &w, &h, &bpp);
    const DWORD cbImage = w * h * ((bpp + 7) / 8);
    for (int f = 0; f < 5; ++f) {
      Outcome o[2];
      for (int k = 0; k < 2; ++k) {
        std::vector<unsigned char> d = Buffer(640 * 480 * 3 + 1024, 0x5A);
        DWORD adw[3] = {9, 9, 9};
        s_pfnSetErr(0);
        if (f == 0 || f == 3) {
          o[k].nRet = (k ? SBmpLoadImage : Ord<PFN_BMPLOAD>(323))(s_aszFile[i], &d[GUARD + 640 * 480 * 3], &d[GUARD],
                                                                  f ? cbImage : 640 * 480 * 3, &adw[0], &adw[1],
                                                                  &adw[2]);
        } else if (f == 1) {
          o[k].nRet = (k ? SBmpLoadImage : Ord<PFN_BMPLOAD>(323))(s_aszFile[i], NULL, NULL, 0, &adw[0], &adw[1],
                                                                  &adw[2]);
        } else {
          // the game's call: no depth asked or given, its own allocator; then a depth of 8 or 24 asked for
          void *p = NULL;
          o[k].nRet = (k ? SBmpAllocLoadImage : Ord<PFN_BMPALLOCLOAD>(325))(
              s_aszFile[i], &d[GUARD + 640 * 480 * 3], &p, &adw[0], &adw[1], f == 2 ? NULL : &adw[2],
              f == 2 ? 0 : i % 2 ? 8 : 24, reinterpret_cast<void *>(TestAlloc));
          if (p) {
            memcpy(&d[GUARD], p, cbImage);
            free(p);
          }
        }
        o[k].dwErr = s_pfnGetErr();
        d.insert(d.end(), reinterpret_cast<unsigned char *>(adw), reinterpret_cast<unsigned char *>(adw) + 12);
        o[k].dest.swap(d);
      }
      if (f == 0 && o[0].nRet)
        ++nFound;
      static const char *s_aszName[] = {"SBmpLoadImage", "SBmpLoadImage, size only", "SBmpAllocLoadImage",
                                        "SBmpLoadImage, buffer the image's size", "SBmpAllocLoadImage, a depth"};
      Compare(s_aszName[f], o[0], o[1], s_aszFile[i]);
    }
  }
  BmpBadArgs();
  printf("images: %d of the game's files loaded\n", nFound);
}

// ---------------------------------------------------------------------------
// Videos. storm.dll's SVid draws on the screen through storm.dll's SDraw, whose
// entry points the test sends to a screen of its own; ours draws through
// PortVid*, which the test has draw on a second. Both decode with the
// install's smackw32.dll. Every video is opened without its sound and each
// frame asked for at once, which is when smackw32 decodes the same frames the
// same way each time: with no sound it drops none, and with no wait it reads
// no clock.

extern "C" {
BOOL __stdcall SVidInitialize(void *pDirectSound);
BOOL __stdcall SVidDestroy(void);
BOOL __stdcall SVidGetSize(HANDLE hVid, DWORD *pdwWidth, DWORD *pdwHeight, DWORD *pdwBpp);
BOOL __stdcall SVidPlayBegin(const char *pszFile, void *pvDest, const void *prcSource, const DWORD *pdwSize,
                             const DWORD *pPal, DWORD dwFlags, HANDLE *phVid);
BOOL __stdcall SVidPlayContinueSingle(HANDLE hVid, DWORD dwNoWait, DWORD *pdwChanged);
BOOL __stdcall SVidPlayEnd(HANDLE hVid);
}
typedef BOOL(__stdcall *PFN_VIDINIT)(void *);
typedef BOOL(__stdcall *PFN_VIDDESTROY)(void);
typedef BOOL(__stdcall *PFN_VIDSIZE)(HANDLE, DWORD *, DWORD *, DWORD *);
typedef BOOL(__stdcall *PFN_VIDBEGIN)(const char *, void *, const void *, const DWORD *, const DWORD *, DWORD, HANDLE *);
typedef BOOL(__stdcall *PFN_VIDCONTINUE)(HANDLE, DWORD, DWORD *);
typedef BOOL(__stdcall *PFN_VIDEND)(HANDLE);

// A screen: its pixels with guards either side, and what was done to it, in
// order.
struct VidScreen {
  std::vector<unsigned char> bits;
  std::string log;
  int nLocks;
  int nFailLock;  // the lock that fails, or -1
};
static VidScreen s_aScreen[2];  // storm.dll's, ours
static DWORD s_dwScreenWidth = 640, s_dwScreenHeight = 480;
static const DWORD VID_SCREEN_MAX = 1280 * 600;

static void ScreenReset(VidScreen &s)
{
  s.bits.assign(VID_SCREEN_MAX + 2 * GUARD, 0x5A);
  s.log.clear();
  s.nLocks = 0;
  s.nFailLock = -1;
}

static BOOL ScreenLock(VidScreen &s, void **ppBits, int *pnPitch)
{
  *ppBits = &s.bits[GUARD];
  if (pnPitch)
    *pnPitch = static_cast<int>(s_dwScreenWidth);
  const bool bFail = s.nLocks++ == s.nFailLock;
  s.log += bFail ? "L!" : "L";
  return !bFail;
}

static void ScreenPalette(VidScreen &s, const void *pEntries, unsigned nFirst, unsigned nCount)
{
  char sz[32];
  sprintf(sz, " P%u,%u:", nFirst, nCount);
  s.log += sz;
  const BYTE *p = static_cast<const BYTE *>(pEntries);
  for (unsigned i = 0; i < 4 * nCount && i < 1024; ++i) {
    sprintf(sz, "%02X", p[i]);
    s.log += sz;
  }
  s.log += " ";
}

static void ScreenClear(VidScreen &s)
{
  memset(&s.bits[GUARD], 0, s_dwScreenWidth * s_dwScreenHeight);
  s.log += "C";
}

// storm.dll's SDraw, with the exports' own arities.
static BOOL __stdcall FakeSDrawClearSurface(int) { ScreenClear(s_aScreen[0]); return TRUE; }
static BOOL __stdcall FakeSDrawGetScreenSize(DWORD *pdwWidth, DWORD *pdwHeight, DWORD *pdwBpp)
{
  if (pdwWidth)
    *pdwWidth = s_dwScreenWidth;
  if (pdwHeight)
    *pdwHeight = s_dwScreenHeight;
  if (pdwBpp)
    *pdwBpp = 8;
  return TRUE;
}
static BOOL __stdcall FakeSDrawLockSurface(int, RECT *, void **ppBits, int *pnPitch, int)
{
  return ScreenLock(s_aScreen[0], ppBits, pnPitch);
}
static BOOL __stdcall FakeSDrawUnlockSurface(int, void *, int, RECT *)
{
  s_aScreen[0].log += "U";
  return TRUE;
}
static BOOL __stdcall FakeSDrawUpdatePalette(unsigned nFirst, unsigned nCount, PALETTEENTRY *pEntries, BOOL)
{
  ScreenPalette(s_aScreen[0], pEntries, nFirst, nCount);
  return TRUE;
}

// Ours, port_present.cpp's in the game.
void PortVidScreenSize(unsigned long *pdwWidth, unsigned long *pdwHeight)
{
  *pdwWidth = s_dwScreenWidth;
  *pdwHeight = s_dwScreenHeight;
}
int PortVidLock(void **ppBits, int *pnPitch) { return ScreenLock(s_aScreen[1], ppBits, pnPitch); }
void PortVidUnlock(void) { s_aScreen[1].log += "U"; }
void PortVidPalette(const void *pEntries, int nFirst, int nCount)
{
  ScreenPalette(s_aScreen[1], pEntries, static_cast<unsigned>(nFirst), static_cast<unsigned>(nCount));
}
void PortVidClear(void) { ScreenClear(s_aScreen[1]); }

// A JMP to pfn over the start of storm.dll's export nOrdinal.
static void VidRedirect(int nOrdinal, void *pfn)
{
  BYTE *pEntry = reinterpret_cast<BYTE *>(GetProcAddress(s_hStorm, MAKEINTRESOURCEA(nOrdinal)));
  DWORD dwOld;
  VirtualProtect(pEntry, 5, PAGE_EXECUTE_READWRITE, &dwOld);
  pEntry[0] = 0xE9;
  *reinterpret_cast<LONG *>(pEntry + 1) = static_cast<LONG>(static_cast<BYTE *>(pfn) - (pEntry + 5));
  VirtualProtect(pEntry, 5, dwOld, &dwOld);
  FlushInstructionCache(GetCurrentProcess(), pEntry, 5);
}

// The videos 1.17 plays: the cinematics, the menus' animations, and the
// portraits, whose names portdata.tbl gives without their number.
static const char *const s_aszVideos[] = {
    "smk\\blizzard.smk", "smk\\starintr.smk", "smk\\PreTerran_1.smk", "smk\\PreTerran_2.smk", "smk\\PreTerran_3.smk",
    "smk\\PreTerran_4.smk", "smk\\Terran1.smk", "smk\\Terran2.smk", "smk\\Terran3.smk", "smk\\Terran4.smk",
    "smk\\PreZerg_2.smk", "smk\\PreZerg_3.smk", "smk\\PreZerg_4.smk", "smk\\Zerg1.smk", "smk\\Zerg2.smk",
    "smk\\Zerg3.smk", "smk\\Zerg4.smk", "smk\\PreProtoss_1.smk", "smk\\PreProtoss_2.smk", "smk\\PreProtoss_3.smk",
    "smk\\Protoss1.smk", "smk\\Protoss2.smk", "smk\\Protoss3.smk", "smk\\Protoss4.smk", "smk\\starXintr.smk",
    "smk\\xProtoss.smk", "smk\\xTerran.smk", "smk\\xZerg.smk", "glue\\campaign\\disk.smk", "glue\\campaign\\Prot.smk",
    "glue\\campaign\\ProtOn.smk", "glue\\campaign\\Terr.smk", "glue\\campaign\\TerrOn.smk", "glue\\campaign\\Zerg.smk",
    "glue\\campaign\\ZergOn.smk", "glue\\Expcampaign\\disk.smk", "glue\\Expcampaign\\XZerg.smk",
    "glue\\Expcampaign\\XZergOn.smk", "glue\\Expcampaign\\XProt.smk", "glue\\Expcampaign\\XProtOn.smk",
    "glue\\Expcampaign\\XTerr.smk", "glue\\Expcampaign\\XTerrOn.smk", "glue\\mainmenu\\Exit.smk",
    "glue\\mainmenu\\ExitOn.smk", "glue\\mainmenu\\SingleOn.smk", "glue\\mainmenu\\Single.smk",
    "glue\\mainmenu\\Multi.smk", "glue\\mainmenu\\MultiOn.smk", "glue\\mainmenu\\Editor.smk",
    "glue\\mainmenu\\EditorOn.smk", "glue\\ReadyP\\Ring.smk", "glue\\ReadyP\\Glow.smk", "glue\\ReadyT\\proloop.smk",
    "glue\\ReadyT\\proj.smk", "glue\\ReadyT\\start.smk", "glue\\ReadyZ\\start.smk", "glue\\ReadyZ\\starton.smk",
    "glue\\ReadyZ\\Drip.smk"};

struct VidName {
  std::string name;
  DWORD dwWidth, dwHeight;
};
static std::vector<VidName> s_videos;

// The flags the game's four callers pass.
static const DWORD s_adwCallerFlags[] = {0x10280808, 0x10A80808, 0x21100200, 0x21100000, 0x21520100, 0x21560100, 0};
// Every flag SVidPlayBegin reads but two, 0x2000000 and 0x4000000, which only
// pass bits on to SmackOpen that no caller sets and that crash smackw32.
static const DWORD s_adwVidBits[] = {0x1,     0x2,     0x4,      0x8,      0x100,    0x200,    0x800,     0x20000,
                                     0x40000, 0x80000, 0x100000, 0x200000, 0x400000, 0x800000, 0x10000000, 0x20000000};

// What one side of a case left after a call.
struct VidState {
  long long nRet;
  DWORD dwErr, dwChanged;
  bool bHandle;
};

static std::string VidDescribe(const std::string &name, DWORD dwFlags, int nFrame)
{
  char sz[64];
  sprintf(sz, " flags %08X frame %d", dwFlags, nFrame);
  return name + sz;
}

// The two screens, logs and bitmaps against each other.
static void VidCompareOutput(const char *pszWhat, const std::vector<unsigned char> *aDest, const std::string &input)
{
  ++g_nCases;
  if (s_aScreen[0].log != s_aScreen[1].log) {
    const std::string &a = s_aScreen[0].log, &b = s_aScreen[1].log;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i])
      ++i;
    const size_t nFrom = i > 40 ? i - 40 : 0;
    Diff(pszWhat, input + ": screen calls differ, Storm ..." + a.substr(nFrom, 80) + " ours ..." + b.substr(nFrom, 80));
  }
  if (s_aScreen[0].bits != s_aScreen[1].bits) {
    size_t i = 0, nLast = 0, nCount = 0;
    while (s_aScreen[0].bits[i] == s_aScreen[1].bits[i])
      ++i;
    for (size_t j = i; j < s_aScreen[0].bits.size(); ++j)
      if (s_aScreen[0].bits[j] != s_aScreen[1].bits[j]) {
        nLast = j;
        ++nCount;
      }
    char sz[128];
    sprintf(sz, ": screens differ at %d (Storm %02X, ours %02X) to %d, %d bytes",
            static_cast<int>(i) - static_cast<int>(GUARD), s_aScreen[0].bits[i], s_aScreen[1].bits[i],
            static_cast<int>(nLast) - static_cast<int>(GUARD), static_cast<int>(nCount));
    Diff(pszWhat, input + sz);
  }
  if (aDest[0] != aDest[1]) {
    size_t i = 0;
    while (aDest[0][i] == aDest[1][i])
      ++i;
    char sz[96];
    sprintf(sz, ": bitmaps differ at %d (Storm %02X, ours %02X)", static_cast<int>(i) - static_cast<int>(GUARD),
            aDest[0][i], aDest[1][i]);
    Diff(pszWhat, input + sz);
  }
}

static void VidCompareState(const char *pszWhat, const VidState *s, const std::string &input)
{
  ++g_nCases;
  if (s[0].nRet != s[1].nRet || s[0].dwErr != s[1].dwErr || s[0].dwChanged != s[1].dwChanged ||
      s[0].bHandle != s[1].bHandle) {
    char sz[160];
    sprintf(sz, ": Storm %lld err %u changed %u handle %d, ours %lld err %u changed %u handle %d", s[0].nRet,
            s[0].dwErr, s[0].dwChanged, s[0].bHandle, s[1].nRet, s[1].dwErr, s[1].dwChanged, s[1].bHandle);
    Diff(pszWhat, input + sz);
  }
}

// Plays one video in both, up to nSteps frames, comparing after each.
static void VidCase(const VidName &video, int nSteps)
{
  DWORD dwFlags;
  if (RandBelow(3) == 0) {
    dwFlags = s_adwCallerFlags[RandBelow(sizeof s_adwCallerFlags / sizeof *s_adwCallerFlags)];
  } else {
    dwFlags = 0;
    for (size_t i = 0; i < sizeof s_adwVidBits / sizeof *s_adwVidBits; ++i)
      if (RandBelow(3) == 0)
        dwFlags |= s_adwVidBits[i];
  }
  dwFlags |= 0x1000000;  // no sound
  // a video that keeps no palette of its own and blends seeds its table from
  // the system palette in storm.dll, which no 1.17 call asks for; the
  // registry's mode, when there is none, blends
  if (dwFlags & 0x100000)
    dwFlags &= ~0xAu;
  const bool bByHandle = RandBelow(6) == 0;
  if (bByHandle)
    dwFlags |= 0x10000;

  // the screen, one of the port's views. storm.dll compiles its doubling for
  // rows of up to 320 pixels, and a screen as wide as twice a wider video would
  // have it double one; 1.17's 640 never does
  static const DWORD s_adwViews[][2] = {{640, 480}, {640, 480}, {800, 600}, {1136, 480}, {1280, 480}};
  const int nView = RandBelow(video.dwWidth > 320 ? 2 : 5);
  s_dwScreenWidth = s_adwViews[nView][0];
  s_dwScreenHeight = s_adwViews[nView][1];
  const int nFailLock = RandBelow(4) == 0 ? RandBelow(nSteps + 1) : -1;
  for (int k = 0; k < 2; ++k) {
    ScreenReset(s_aScreen[k]);
    s_aScreen[k].nFailLock = nFailLock;
  }

  // the bitmap, room for the video twice its size where the rect puts it
  LONG rc[4] = {RandBelow(16), RandBelow(16), RandBelow(700), RandBelow(500)};
  const DWORD dwPitch = rc[0] + 2 * video.dwWidth + RandBelow(24);
  const DWORD dwHeight = rc[1] + 2 * video.dwHeight + 2 + RandBelow(8);
  DWORD adwSize[2] = {dwPitch, dwHeight};
  std::vector<unsigned char> aDest[2];
  aDest[0].resize(dwPitch * dwHeight + 2 * GUARD);
  for (size_t i = 0; i < aDest[0].size(); ++i)
    aDest[0][i] = static_cast<unsigned char>(Rand32());
  aDest[1] = aDest[0];
  const bool bDest = (dwFlags & 0x20000000) || RandBelow(4) == 0;
  const bool bRect = (dwFlags & 0x20000000) ? RandBelow(4) != 0 : RandBelow(4) == 0;
  DWORD adwPal[3] = {0x0C, 1, 0xFE};
  const bool bPal = RandBelow(2) == 0;
  if (bPal) {
    adwPal[1] = RandBelow(3) == 0 ? 0 : RandBelow(256);
    adwPal[2] = RandBelow(3) == 0 ? 256 - adwPal[1] : RandBelow(257 - adwPal[1]);
  }

  static const PFN_VIDBEGIN s_pfnBegin = Ord<PFN_VIDBEGIN>(454);
  static const PFN_VIDCONTINUE s_pfnContinue = Ord<PFN_VIDCONTINUE>(457);
  static const PFN_VIDEND s_pfnEnd = Ord<PFN_VIDEND>(458);
  static const PFN_VIDSIZE s_pfnSize = Ord<PFN_VIDSIZE>(452);
  static const PFN_FOPENFILEEX s_pfnOpenEx = Ord<PFN_FOPENFILEEX>(268);

  HANDLE ahVid[2] = {NULL, NULL}, ahFile[2] = {NULL, NULL};
  VidState s[2];
  std::string input = VidDescribe(video.name, dwFlags, -1);
  for (int k = 0; k < 2; ++k) {
    const char *pszFile = video.name.c_str();
    if (bByHandle) {
      (k ? SFileOpenFileEx : s_pfnOpenEx)(NULL, pszFile, 5, &ahFile[k]);
      pszFile = static_cast<const char *>(ahFile[k]);
    }
    s_pfnSetErr(0);
    s[k].nRet = (k ? SVidPlayBegin : s_pfnBegin)(pszFile, bDest ? &aDest[k][GUARD] : NULL, bRect ? rc : NULL,
                                                 bDest ? adwSize : NULL, bPal ? adwPal : NULL, dwFlags, &ahVid[k]);
    s[k].dwErr = s_pfnGetErr();
    s[k].dwChanged = 0;
    s[k].bHandle = ahVid[k] != NULL;
  }
  VidCompareState("SVidPlayBegin", s, input);
  VidCompareOutput("SVidPlayBegin", aDest, input);
  if (ahVid[0] && ahVid[1]) {
    DWORD adw[2][3];
    for (int k = 0; k < 2; ++k) {
      memset(adw[k], 0xCD, sizeof adw[k]);
      s_pfnSetErr(0);
      s[k].nRet = (k ? SVidGetSize : s_pfnSize)(ahVid[k], &adw[k][0], &adw[k][1], &adw[k][2]);
      s[k].dwErr = s_pfnGetErr();
      s[k].dwChanged = adw[k][0] ^ adw[k][1] << 12 ^ adw[k][2] << 24;
    }
    VidCompareState("SVidGetSize", s, input);
  }
  for (int nFrame = 0; nFrame < nSteps && ahVid[0] && ahVid[1]; ++nFrame) {
    input = VidDescribe(video.name, dwFlags, nFrame);
    const bool bChanged = RandBelow(8) != 0;
    for (int k = 0; k < 2; ++k) {
      s_pfnSetErr(0);
      DWORD dwChanged = 0xCDCDCDCD;
      s[k].nRet = (k ? SVidPlayContinueSingle : s_pfnContinue)(ahVid[k], 1, bChanged ? &dwChanged : NULL);
      s[k].dwErr = s_pfnGetErr();
      s[k].dwChanged = dwChanged;
      if (!s[k].nRet)
        ahVid[k] = NULL;
      s[k].bHandle = ahVid[k] != NULL;
    }
    VidCompareState("SVidPlayContinueSingle", s, input);
    VidCompareOutput("SVidPlayContinueSingle", aDest, input);
  }
  for (int k = 0; k < 2; ++k) {
    s_pfnSetErr(0);
    s[k].nRet = ahVid[k] ? (k ? SVidPlayEnd : s_pfnEnd)(ahVid[k]) : -1;
    s[k].dwErr = s_pfnGetErr();
    s[k].dwChanged = 0;
    s[k].bHandle = false;
    if (ahFile[k])
      CloseHandle(ahFile[k]);
  }
  VidCompareState("SVidPlayEnd", s, input);
  VidCompareOutput("SVidPlayEnd", aDest, input);
}

// The arguments SVid refuses, handles that are not videos, and names that do
// not open as one.
static void VidBadArgs(void)
{
  static const PFN_VIDBEGIN s_pfnBegin = Ord<PFN_VIDBEGIN>(454);
  static const PFN_VIDCONTINUE s_pfnContinue = Ord<PFN_VIDCONTINUE>(457);
  static const PFN_VIDEND s_pfnEnd = Ord<PFN_VIDEND>(458);
  static const PFN_VIDSIZE s_pfnSize = Ord<PFN_VIDSIZE>(452);
  for (int k = 0; k < 2; ++k)
    ScreenReset(s_aScreen[k]);
  // room for any 1.17 video: ours refuses a bitmap smaller than the video, which storm.dll draws past
  std::vector<unsigned char> aDest[2];
  aDest[0].assign(640 * 480 + 2 * GUARD, 0x33);
  aDest[1] = aDest[0];
  const DWORD adwSize[2] = {640, 480};
  const DWORD adwBadPal[3] = {0x0B, 0, 256};
  const LONG rc[4] = {0, 0, 0, 0};
  struct Begin {
    const char *pszWhat, *pszFile;
    bool bDest, bSize, bPhVid;
    const DWORD *pPal;
    DWORD dwFlags;
  };
  const Begin aBegin[] = {
      {"no name", NULL, true, true, true, NULL, 0x21100000},
      {"no handle out", "smk\\blizzard.smk", true, true, false, NULL, 0x21100000},
      {"bitmap without one", "smk\\blizzard.smk", false, false, true, NULL, 0x21100000},
      {"bitmap without its size", "smk\\blizzard.smk", true, false, true, NULL, 0x21100000},
      {"palette range of the wrong size", "smk\\blizzard.smk", true, true, true, adwBadPal, 0x21100000},
      {"no such file", "smk\\nosuch.smk", true, true, true, NULL, 0x21100000},
      {"not a video", "music\\title.wav", true, true, true, NULL, 0x21100000},
      {"compressed", "arr\\units.dat", true, true, true, NULL, 0x21100000},
      {"empty name", "", true, true, true, NULL, 0x21100000},
      {"no such file, the screen", "smk\\nosuch.smk", false, false, true, NULL, 0x10280808},
  };
  VidState s[2];
  for (size_t i = 0; i < sizeof aBegin / sizeof *aBegin; ++i) {
    const Begin &b = aBegin[i];
    for (int k = 0; k < 2; ++k) {
      HANDLE hVid = reinterpret_cast<HANDLE>(1);
      s_pfnSetErr(0);
      s[k].nRet = (k ? SVidPlayBegin : s_pfnBegin)(b.pszFile, b.bDest ? &aDest[k][GUARD] : NULL, rc,
                                                   b.bSize ? adwSize : NULL, b.pPal, b.dwFlags,
                                                   b.bPhVid ? &hVid : NULL);
      s[k].dwErr = s_pfnGetErr();
      s[k].dwChanged = 0;
      s[k].bHandle = hVid != NULL;
    }
    VidCompareState("SVidPlayBegin", s, b.pszWhat);
    VidCompareOutput("SVidPlayBegin", aDest, b.pszWhat);
  }

  // a handle that is not a video, and NULL
  HANDLE ahNot[2] = {NULL, &ahNot};
  for (int h = 0; h < 2; ++h) {
    const std::string what = h ? "not a video" : "NULL";
    for (int k = 0; k < 2; ++k) {
      DWORD adw[3] = {0xCDCDCDCD, 0xCDCDCDCD, 0xCDCDCDCD};
      s_pfnSetErr(0);
      s[k].nRet = (k ? SVidGetSize : s_pfnSize)(ahNot[h], &adw[0], &adw[1], &adw[2]);
      s[k].dwErr = s_pfnGetErr();
      s[k].dwChanged = adw[0] ^ adw[1] << 8 ^ adw[2] << 16;
      s[k].bHandle = false;
    }
    VidCompareState("SVidGetSize", s, what);
    for (int k = 0; k < 2; ++k) {
      DWORD dwChanged = 0xCDCDCDCD;
      s_pfnSetErr(0);
      s[k].nRet = (k ? SVidPlayContinueSingle : s_pfnContinue)(ahNot[h], 1, &dwChanged);
      s[k].dwErr = s_pfnGetErr();
      s[k].dwChanged = dwChanged;
    }
    VidCompareState("SVidPlayContinueSingle", s, what);
    for (int k = 0; k < 2; ++k) {
      s_pfnSetErr(0);
      s[k].nRet = (k ? SVidPlayEnd : s_pfnEnd)(ahNot[h]);
      s[k].dwErr = s_pfnGetErr();
      s[k].dwChanged = 0;
    }
    VidCompareState("SVidPlayEnd", s, what);
  }

  // a video's size with each out-parameter left out, then the handle once
  // ended
  for (int nMask = 0; nMask < 8; ++nMask) {
    HANDLE ahVid[2];
    for (int k = 0; k < 2; ++k) {
      (k ? SVidPlayBegin : s_pfnBegin)("smk\\blizzard.smk", &aDest[k][GUARD], NULL, adwSize, NULL, 0x21100000,
                                       &ahVid[k]);
      DWORD adw[3] = {0xCDCDCDCD, 0xCDCDCDCD, 0xCDCDCDCD};
      s_pfnSetErr(0);
      s[k].nRet = (k ? SVidGetSize : s_pfnSize)(ahVid[k], nMask & 1 ? &adw[0] : NULL, nMask & 2 ? &adw[1] : NULL,
                                                nMask & 4 ? &adw[2] : NULL);
      s[k].dwErr = s_pfnGetErr();
      s[k].dwChanged = adw[0] ^ adw[1] << 8 ^ adw[2] << 16;
      s[k].bHandle = ahVid[k] != NULL;
      (k ? SVidPlayEnd : s_pfnEnd)(ahVid[k]);
    }
    VidCompareState("SVidGetSize", s, "out-parameters " + std::to_string(nMask));
    for (int k = 0; k < 2; ++k) {
      s_pfnSetErr(0);
      s[k].nRet = (k ? SVidPlayEnd : s_pfnEnd)(ahVid[k]);
      s[k].dwErr = s_pfnGetErr();
      s[k].dwChanged = 0;
    }
    VidCompareState("SVidPlayEnd", s, "a video already ended");
  }
  VidCompareOutput("SVidGetSize", aDest, "out-parameters");
}

// SVidDestroy with videos open: it ends them, and a video opened after it
// brings smackw32 back.
static void VidDestroyCase(void)
{
  static const PFN_VIDBEGIN s_pfnBegin = Ord<PFN_VIDBEGIN>(454);
  static const PFN_VIDCONTINUE s_pfnContinue = Ord<PFN_VIDCONTINUE>(457);
  static const PFN_VIDDESTROY s_pfnDestroy = Ord<PFN_VIDDESTROY>(451);
  static const PFN_VIDEND s_pfnEnd = Ord<PFN_VIDEND>(458);
  // room for any 1.17 video: ours refuses a bitmap smaller than the video, which storm.dll draws past
  std::vector<unsigned char> aDest[2];
  aDest[0].assign(640 * 480 + 2 * GUARD, 0x44);
  aDest[1] = aDest[0];
  const DWORD adwSize[2] = {640, 480};
  HANDLE ahVid[2][3];
  VidState s[2];
  for (int k = 0; k < 2; ++k) {
    ScreenReset(s_aScreen[k]);
    for (int i = 0; i < 3; ++i)
      (k ? SVidPlayBegin : s_pfnBegin)(s_videos[s_videos.size() - 1 - i].name.c_str(), &aDest[k][GUARD], NULL,
                                       adwSize, NULL, 0x21100000, &ahVid[k][i]);
    s_pfnSetErr(0);
    s[k].nRet = (k ? SVidDestroy : s_pfnDestroy)();
    s[k].dwErr = s_pfnGetErr();
    s[k].dwChanged = 0;
    s[k].bHandle = false;
  }
  VidCompareState("SVidDestroy", s, "three videos open");
  for (int i = 0; i < 3; ++i) {
    for (int k = 0; k < 2; ++k) {
      DWORD dwChanged = 0xCDCDCDCD;
      s_pfnSetErr(0);
      s[k].nRet = (k ? SVidPlayContinueSingle : s_pfnContinue)(ahVid[k][i], 1, &dwChanged);
      s[k].dwErr = s_pfnGetErr();
      s[k].dwChanged = dwChanged;
    }
    VidCompareState("SVidPlayContinueSingle", s, "after SVidDestroy");
  }
  for (int k = 0; k < 2; ++k) {
    HANDLE hVid = NULL;
    s_pfnSetErr(0);
    s[k].nRet = (k ? SVidPlayBegin : s_pfnBegin)(s_videos.back().name.c_str(), &aDest[k][GUARD], NULL, adwSize, NULL,
                                                 0x21100000, &hVid);
    s[k].dwErr = s_pfnGetErr();
    s[k].bHandle = hVid != NULL;
    DWORD dwChanged = 0;
    (k ? SVidPlayContinueSingle : s_pfnContinue)(hVid, 1, &dwChanged);
    s[k].dwChanged = dwChanged;
    (k ? SVidPlayEnd : s_pfnEnd)(hVid);
  }
  VidCompareState("SVidPlayBegin", s, "after SVidDestroy");
  VidCompareOutput("SVidPlayBegin", aDest, "after SVidDestroy");
}

static void VidCases(const char *pszInstall, int nRounds)
{
  // storm.dll loads smackw32.dll by its name alone, which finds the exe's
  // folder first; the game's is the install
  if (!LoadLibraryA((std::string(pszInstall) + "\\smackw32.dll").c_str())) {
    printf("videos: no smackw32.dll in %s\n", pszInstall);
    ++g_nDiffs;
    return;
  }
  VidRedirect(344, reinterpret_cast<void *>(FakeSDrawClearSurface));
  VidRedirect(348, reinterpret_cast<void *>(FakeSDrawGetScreenSize));
  VidRedirect(350, reinterpret_cast<void *>(FakeSDrawLockSurface));
  VidRedirect(356, reinterpret_cast<void *>(FakeSDrawUnlockSurface));
  VidRedirect(357, reinterpret_cast<void *>(FakeSDrawUpdatePalette));

  // the CD's archives, where the cinematics are
  static const char *const s_aszCd[] = {"Starcraft.mpq", "BroodWar.mpq"};
  HANDLE ahCd[2][2] = {};
  for (int i = 0; i < 2; ++i) {
    const std::string path = std::string(pszInstall) + "\\" + s_aszCd[i];
    Ord<PFN_FOPENARCHIVE>(266)(path.c_str(), 0x3E8 + i, 2, &ahCd[i][0]);
    SFileOpenArchive(path.c_str(), 0x3E8 + i, 2, &ahCd[i][1]);
  }

  VidState s[2];
  for (int k = 0; k < 2; ++k) {
    s_pfnSetErr(0);
    s[k].nRet = (k ? SVidInitialize : Ord<PFN_VIDINIT>(453))(NULL);
    s[k].dwErr = s_pfnGetErr();
    s[k].dwChanged = 0;
    s[k].bHandle = false;
  }
  VidCompareState("SVidInitialize", s, "NULL");

  // the videos there are, with their sizes, through our own; each is opened
  // as a file handle at its data first, as SVidPlayBegin opens it, and the two
  // handles must stand at the same place
  std::vector<std::string> names(s_aszVideos, s_aszVideos + sizeof s_aszVideos / sizeof *s_aszVideos);
  for (size_t i = 0; i < s_names.size(); ++i) {
    if (s_names[i].compare(0, 9, "portrait\\") != 0)
      continue;
    for (int d = 0; d < 10; ++d)
      names.push_back(s_names[i] + static_cast<char>('0' + d) + ".smk");
  }
  for (size_t i = 0; i < names.size(); ++i) {
    HANDLE ahFile[2] = {NULL, NULL};
    Outcome o[2];
    for (int k = 0; k < 2; ++k) {
      s_pfnSetErr(0);
      o[k].nRet = (k ? SFileOpenFileEx : Ord<PFN_FOPENFILEEX>(268))(NULL, names[i].c_str(), 5, &ahFile[k]);
      o[k].dwErr = s_pfnGetErr();
      if (ahFile[k]) {
        o[k].nRet ^= static_cast<long long>(SetFilePointer(ahFile[k], 0, NULL, FILE_CURRENT)) << 1;
        CloseHandle(ahFile[k]);
      }
    }
    Compare("SFileOpenFileEx", o[0], o[1], names[i] + ", scope 5");
    HANDLE hVid = NULL;
    SVidPlayBegin(names[i].c_str(), NULL, NULL, NULL, NULL, 0x100000, &hVid);
    if (!hVid)
      continue;
    VidName v = {names[i], 0, 0};
    SVidGetSize(hVid, &v.dwWidth, &v.dwHeight, NULL);
    SVidPlayEnd(hVid);
    s_videos.push_back(v);
  }
  printf("videos: %d found\n", static_cast<int>(s_videos.size()));
  if (s_videos.size() < 3) {
    ++g_nDiffs;
    return;
  }

  VidBadArgs();
  // each video once, then at random; the cinematics are long, so most cases
  // play a few dozen frames, and some a portrait to its end
  const int nCases = static_cast<int>(s_videos.size()) + nRounds / 40;
  for (int i = 0; i < nCases; ++i) {
    const VidName &v = s_videos[i < static_cast<int>(s_videos.size()) ? i : RandBelow(static_cast<int>(s_videos.size()))];
    const bool bSmall = v.dwWidth * v.dwHeight <= 0x10000;
    VidCase(v, bSmall && RandBelow(2) == 0 ? 400 : 1 + RandBelow(24));
  }
  VidDestroyCase();
  for (int k = 0; k < 2; ++k)
    s[k].nRet = (k ? SVidDestroy : Ord<PFN_VIDDESTROY>(451))();
  for (int i = 0; i < 2; ++i) {
    Ord<PFN_FCLOSE>(252)(ahCd[i][0]);
    SFileCloseArchive(ahCd[i][1]);
  }
  printf("videos: %d cases\n", nCases);
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: storm_test.exe <storm.dll> [rounds] [map folders]\n");
    return 2;
  }
  setvbuf(stdout, NULL, _IONBF, 0);
  s_hStorm = LoadLibraryA(argv[1]);
  if (!s_hStorm) {
    printf("cannot load %s\n", argv[1]);
    return 2;
  }
  s_pfnSetErr = Ord<PFN_SETERR>(465);
  s_pfnGetErr = Ord<PFN_GETERR>(463);
  const int nRounds = argc > 2 ? atoi(argv[2]) : 20000;

  NullCases();
  for (int i = 0; i < nRounds; ++i) {
    StrCopyPack(i);
    StrHash();
    StrLenCmp();
    StrUpperChr();
    StrStr();
    StrPrintf();
    MemOps();
    if (i % 8 == 0)
      MemAlloc();
  }
  // The registry functions keep to Hyperion's settings file now, not the registry
  // (port_settings.cpp, docs/front-end.md, F3), so Storm's and ours no longer write the same
  // place; RegCases stays for the conversions it checks until a settings test takes it over,
  // and runs with STORM_TEST_REGISTRY=1.
  if (getenv("STORM_TEST_REGISTRY") != NULL)
    RegCases(nRounds / 40 + 1);
  for (int i = 0; i < nRounds; ++i) {
    BmpDecode();
    BmpPad();
  }
  // the install is storm.dll's folder
  std::string install(argv[1]);
  const size_t nSlash = install.find_last_of("\\/");
  install = nSlash == std::string::npos ? "." : install.substr(0, nSlash);
  std::vector<std::string> mapFolders;
  for (int i = 3; i < argc; ++i)
    mapFolders.push_back(argv[i]);
  ArchivesOpen(install.c_str());
  BmpFiles();
  FileCases(install.c_str(), nRounds, mapFolders);
  DdaCases(install.c_str(), nRounds);
  VidCases(install.c_str(), nRounds);
  printf("%d cases, %d differences\n", g_nCases, g_nDiffs);
  return g_nDiffs ? 1 : 0;
}
