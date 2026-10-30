// err_test.cpp -- the port's Storm errors (src117/port/data/port_stormerr.cpp) against
// storm.dll's.
//
//   err_test.exe <install folder> [random codes]
//
// Loads the install's storm.dll and asks both for the text of every Storm error
// code (facility 0x510), every DirectDraw and DirectSound code (0x876, 0x878),
// the Win32 codes below 0x3000 and their HRESULTs, and random codes, into
// buffers of every size around the text's length, of none, and no buffer at all,
// comparing what each returns and writes and the last error each leaves, Storm's
// and Win32's. It sets and reads the last error in turns, with SetLastError in
// between, and tries SErrSuppressErrors on a few values.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>

extern "C" {
void __stdcall SErrSetLastError(DWORD dwErrCode);
DWORD __stdcall SErrGetLastError();
BOOL __stdcall SErrSuppressErrors(BOOL bSuppress);
BOOL __stdcall SErrGetErrorStr(DWORD dwErrCode, char *pszBuffer, DWORD cchBuffer);
}

typedef void(__stdcall *PFN_SET)(DWORD);
typedef DWORD(__stdcall *PFN_GET)();
typedef BOOL(__stdcall *PFN_SUPPRESS)(BOOL);
typedef BOOL(__stdcall *PFN_STR)(DWORD, char *, DWORD);

struct Api {
  PFN_SET Set;
  PFN_GET Get;
  PFN_SUPPRESS Suppress;
  PFN_STR Str;
};

static Api s_storm, s_ours = {SErrSetLastError, SErrGetLastError, SErrSuppressErrors, SErrGetErrorStr};
static int s_nCases, s_nDiffs, s_nTexts;

static DWORD s_dwSeed = 0xE22C0DE5;
static DWORD Rand32()
{
  s_dwSeed = s_dwSeed * 1103515245 + 12345;
  DWORD hi = s_dwSeed >> 16;
  s_dwSeed = s_dwSeed * 1103515245 + 12345;
  return (hi << 16) | (s_dwSeed >> 16);
}

static void Check(const char *pszWhat, const std::string &a, const std::string &b)
{
  ++s_nCases;
  if (a == b)
    return;
  if (++s_nDiffs <= 20)
    printf("DIFF %s\n  storm %s\n  ours  %s\n", pszWhat, a.c_str(), b.c_str());
}

// One SErrGetErrorStr call: the result, the buffer's bytes up to two past its
// size, and the last errors after it.
static std::string Text(const Api &api, DWORD dwCode, DWORD cchBuffer, bool bNull)
{
  char buf[1100];
  memset(buf, 0xCC, sizeof buf);
  api.Set(0x3333);
  SetLastError(0x4444);
  const BOOL bRet = api.Str(dwCode, bNull ? NULL : buf, cchBuffer);
  const DWORD dwWin32 = GetLastError(), dwStorm = api.Get();
  char sz[64];
  sprintf(sz, "%d win32 %X storm %X:", bRet, dwWin32, dwStorm);
  std::string s = sz;
  if (!bNull) {
    const DWORD n = cchBuffer + 2 < sizeof buf ? cchBuffer + 2 : sizeof buf;
    for (DWORD i = 0; i < n; ++i) {
      sprintf(sz, "%02X", static_cast<BYTE>(buf[i]));
      s += sz;
    }
  }
  return s;
}

static void Code(DWORD dwCode)
{
  char full[1100];
  const BOOL bKnown = s_storm.Str(dwCode, full, sizeof full);
  const DWORD cch = bKnown ? static_cast<DWORD>(strlen(full)) : 0;
  if (bKnown)
    ++s_nTexts;
  // Around the text's length when there is one, and a few sizes regardless.
  DWORD sizes[12];
  int n = 0;
  sizes[n++] = sizeof full;
  sizes[n++] = 1;
  if (bKnown) {
    sizes[n++] = cch;
    sizes[n++] = cch + 1;
    sizes[n++] = cch + 2;
    if (cch > 1)
      sizes[n++] = cch - 1;
  }
  sizes[n++] = 1 + Rand32() % 300;
  for (int i = 0; i < n; ++i) {
    char what[64];
    sprintf(what, "SErrGetErrorStr %08X in %u", dwCode, sizes[i]);
    Check(what, Text(s_storm, dwCode, sizes[i], false), Text(s_ours, dwCode, sizes[i], false));
  }
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: err_test.exe <install folder> [random codes]\n");
    return 2;
  }
  HMODULE hStorm = LoadLibraryA((std::string(argv[1]) + "\\storm.dll").c_str());
  if (!hStorm) {
    printf("cannot load %s\\storm.dll\n", argv[1]);
    return 2;
  }
  s_storm.Set = reinterpret_cast<PFN_SET>(GetProcAddress(hStorm, MAKEINTRESOURCEA(465)));
  s_storm.Get = reinterpret_cast<PFN_GET>(GetProcAddress(hStorm, MAKEINTRESOURCEA(463)));
  s_storm.Suppress = reinterpret_cast<PFN_SUPPRESS>(GetProcAddress(hStorm, MAKEINTRESOURCEA(468)));
  s_storm.Str = reinterpret_cast<PFN_STR>(GetProcAddress(hStorm, MAKEINTRESOURCEA(462)));
  const int nRandom = argc > 2 ? atoi(argv[2]) : 20000;

  // The last error: set through each, then Win32's alone, read back.
  static const DWORD kValues[] = {0, 1, 0x57, 0x4B4, 0x85100070, 0xFFFFFFFF};
  for (int i = 0; i < ARRAYSIZE(kValues); ++i) {
    std::string r[2];
    const Api *apis[2] = {&s_storm, &s_ours};
    for (int k = 0; k < 2; ++k) {
      char sz[80];
      apis[k]->Set(kValues[i]);
      const DWORD dwWin32 = GetLastError();
      SetLastError(0x1234);
      sprintf(sz, "win32 %X storm %X", dwWin32, apis[k]->Get());
      r[k] = sz;
    }
    Check("SErrSetLastError", r[0], r[1]);
  }
  static const BOOL kSuppress[] = {TRUE, TRUE, FALSE, 5, 0, -1};
  for (int i = 0; i < ARRAYSIZE(kSuppress); ++i) {
    char a[16], b[16];
    sprintf(a, "%d", s_storm.Suppress(kSuppress[i]));
    sprintf(b, "%d", s_ours.Suppress(kSuppress[i]));
    Check("SErrSuppressErrors", a, b);
  }
  s_storm.Suppress(FALSE);
  s_ours.Suppress(FALSE);

  // No buffer, and a buffer of no size.
  static const DWORD kSome[] = {0, 5, 0x85100070, 0x8876017C, 0xE0000001};
  for (int i = 0; i < ARRAYSIZE(kSome); ++i) {
    Check("SErrGetErrorStr, no buffer", Text(s_storm, kSome[i], 10, true), Text(s_ours, kSome[i], 10, true));
    Check("SErrGetErrorStr, size 0", Text(s_storm, kSome[i], 0, false), Text(s_ours, kSome[i], 0, false));
  }

  for (DWORD c = 0; c < 0x200; ++c)
    Code(0x85100000 | c);
  for (DWORD c = 0; c < 0x10000; ++c) {
    Code(0x88760000 | c);
    Code(0x88780000 | c);
  }
  for (DWORD c = 0; c < 0x3000; ++c) {
    Code(c);
    Code(0x80070000 | c);
  }
  for (int i = 0; i < nRandom; ++i)
    Code(Rand32());
  printf("%d cases, %d codes with a text, %d differences\n", s_nCases, s_nTexts, s_nDiffs);
  return s_nDiffs ? 1 : 0;
}
