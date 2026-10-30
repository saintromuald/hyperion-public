// big_test.cpp -- the port's big numbers (src117/port/data/port_stormbig.cpp) against
// storm.dll's.
//
//   big_test.exe <install folder> <StarCraft.exe> [rounds] [map folders]
//
// Loads the install's storm.dll and runs its SBig functions and ours on the same
// numbers: bytes in and out at every length, with zero bytes at the top and
// buffers short of the number and past it, and powers modulo numbers of every
// length, the result in place of the base now and then, and the game's own
// shape, 256-byte keys and a 4-byte exponent. Then, for each map in the folders
// given that ends in a signature block, it raises the signature to the public
// exponent modulo the public key, both read from the player's StarCraft.exe, in
// each, as signature.cpp does, compares the two, and counts the maps whose result
// has the padding a Blizzard signature decodes to.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

extern "C" {
void __stdcall SBigNew(void **ppBig);
void __stdcall SBigDel(void *pBig);
void __stdcall SBigFromBinary(void *pBig, const void *pvData, DWORD cbData);
void __stdcall SBigToBinaryBuffer(void *pBig, void *pvBuffer, DWORD cbBuffer, DWORD *pcbWritten);
void __stdcall SBigPowMod(void *pResult, void *pBase, void *pExp, void *pMod);
}

typedef void(__stdcall *PFN_NEW)(void **);
typedef void(__stdcall *PFN_DEL)(void *);
typedef void(__stdcall *PFN_FROM)(void *, const void *, DWORD);
typedef void(__stdcall *PFN_TO)(void *, void *, DWORD, DWORD *);
typedef void(__stdcall *PFN_POWMOD)(void *, void *, void *, void *);

struct Api {
  PFN_NEW New;
  PFN_DEL Del;
  PFN_FROM From;
  PFN_TO To;
  PFN_POWMOD PowMod;
};

static Api s_storm, s_ours = {SBigNew, SBigDel, SBigFromBinary, SBigToBinaryBuffer, SBigPowMod};
static int s_nCases, s_nDiffs;

static DWORD s_dwSeed = 0x5EED1234;
static DWORD Rand32()
{
  s_dwSeed = s_dwSeed * 1103515245 + 12345;
  DWORD hi = s_dwSeed >> 16;
  s_dwSeed = s_dwSeed * 1103515245 + 12345;
  return (hi << 16) | (s_dwSeed >> 16);
}
static DWORD RandBelow(DWORD n) { return n ? Rand32() % n : 0; }

typedef std::vector<BYTE> Bytes;

// Random bytes, sometimes with zero bytes at the top, sometimes all zero.
static Bytes RandNumber(DWORD cb)
{
  Bytes b(cb);
  for (DWORD i = 0; i < cb; ++i)
    b[i] = static_cast<BYTE>(Rand32());
  if (cb && RandBelow(4) == 0) {
    DWORD nZero = 1 + RandBelow(RandBelow(2) ? 4 : cb);
    for (DWORD i = 0; i < nZero && i < cb; ++i)
      b[cb - 1 - i] = 0;
  }
  if (cb && RandBelow(40) == 0)
    memset(&b[0], 0, cb);
  return b;
}

static std::string Hex(const BYTE *pb, DWORD cb)
{
  std::string s;
  char sz[4];
  for (DWORD i = 0; i < cb; ++i) {
    sprintf(sz, "%02X", pb[i]);
    s += sz;
  }
  return s;
}

// A number's bytes as ToBinaryBuffer gives them, into a buffer of cbBuffer.
static std::string Out(const Api &api, void *h, DWORD cbBuffer)
{
  Bytes buf(cbBuffer + 8, 0xCC);
  DWORD cb = 0xDEADBEEF;
  api.To(h, &buf[0], cbBuffer, &cb);
  char sz[32];
  sprintf(sz, "%08X:", cb);
  return sz + Hex(&buf[0], static_cast<DWORD>(buf.size()));
}

static void *Make(const Api &api, const Bytes &b)
{
  void *h = NULL;
  api.New(&h);
  api.From(h, b.empty() ? NULL : &b[0], static_cast<DWORD>(b.size()));
  return h;
}

static void Check(const char *pszWhat, const std::string &a, const std::string &b, const std::string &args)
{
  ++s_nCases;
  if (a == b)
    return;
  if (++s_nDiffs <= 20)
    printf("DIFF %s\n  args %s\n  storm %s\n  ours  %s\n", pszWhat, args.c_str(), a.substr(0, 700).c_str(),
           b.substr(0, 700).c_str());
}

static void BytesCase()
{
  const Bytes b = RandNumber(RandBelow(48));
  const DWORD cbBuffer = RandBelow(56);
  std::string out[2];
  const Api *apis[2] = {&s_storm, &s_ours};
  for (int k = 0; k < 2; ++k) {
    void *h = Make(*apis[k], b);
    out[k] = Out(*apis[k], h, cbBuffer);
    apis[k]->Del(h);
  }
  char sz[32];
  sprintf(sz, " into %u", cbBuffer);
  Check("bytes", out[0], out[1], Hex(b.empty() ? NULL : &b[0], static_cast<DWORD>(b.size())) + sz);
}

static void PowModCase(const Bytes &base, const Bytes &exp, const Bytes &mod, bool bInPlace)
{
  std::string out[2];
  const Api *apis[2] = {&s_storm, &s_ours};
  for (int k = 0; k < 2; ++k) {
    const Api &api = *apis[k];
    void *hb = Make(api, base), *he = Make(api, exp), *hm = Make(api, mod), *hr = hb;
    if (!bInPlace)
      api.New(&hr);
    api.PowMod(hr, hb, he, hm);
    out[k] = Out(api, hr, 320);
    if (!bInPlace)
      api.Del(hr);
    api.Del(hb), api.Del(he), api.Del(hm);
  }
  const std::string args = Hex(base.empty() ? NULL : &base[0], static_cast<DWORD>(base.size())) + " ^ " +
                           Hex(exp.empty() ? NULL : &exp[0], static_cast<DWORD>(exp.size())) + " mod " +
                           Hex(&mod[0], static_cast<DWORD>(mod.size())) + (bInPlace ? " in place" : "");
  Check("powmod", out[0], out[1], args);
}

static bool IsZero(const Bytes &b)
{
  for (size_t i = 0; i < b.size(); ++i)
    if (b[i])
      return false;
  return true;
}

static void RandomPowMod()
{
  const bool bGame = RandBelow(4) == 0;
  Bytes mod, exp, base;
  if (bGame) {
    // 256-byte keys and signatures and a 4-byte exponent, as the game's.
    mod = RandNumber(256);
    mod[255] |= 0x80;
    exp = RandBelow(2) ? Bytes{1, 0, 1, 0} : RandNumber(4);
    base = RandNumber(256);
  } else {
    const DWORD cbMod = RandBelow(3) ? 1 + RandBelow(12) : 1 + RandBelow(300);
    do
      mod = RandNumber(cbMod);
    while (IsZero(mod));
    exp = RandNumber(RandBelow(5) ? RandBelow(9) : RandBelow(65));
    base = RandNumber(RandBelow(cbMod + 9));
  }
  PowModCase(base, exp, mod, RandBelow(6) == 0);
}

// The file's bytes at a virtual address, read from its section table.
static bool ReadAtVa(const Bytes &exe, DWORD dwVa, void *pv, DWORD cb)
{
  const IMAGE_DOS_HEADER *pDos = reinterpret_cast<const IMAGE_DOS_HEADER *>(&exe[0]);
  const IMAGE_NT_HEADERS32 *pNt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(&exe[pDos->e_lfanew]);
  const IMAGE_SECTION_HEADER *pSec = IMAGE_FIRST_SECTION(pNt);
  const DWORD dwRva = dwVa - pNt->OptionalHeader.ImageBase;
  for (int i = 0; i < pNt->FileHeader.NumberOfSections; ++i, ++pSec) {
    if (dwRva >= pSec->VirtualAddress && dwRva + cb <= pSec->VirtualAddress + pSec->SizeOfRawData) {
      memcpy(pv, &exe[pSec->PointerToRawData + dwRva - pSec->VirtualAddress], cb);
      return true;
    }
  }
  return false;
}

static bool ReadFileBytes(const std::string &path, Bytes &out)
{
  HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE)
    return false;
  const DWORD cb = GetFileSize(h, NULL);
  out.resize(cb);
  DWORD cbRead = 0;
  const BOOL bOk = !cb || ReadFile(h, &out[0], cb, &cbRead, NULL);
  CloseHandle(h);
  return bOk && cbRead == cb;
}

static BYTE s_abKey[0x100], s_abExp[4];
static int s_nMaps, s_nSigned, s_nPadded;

// signature.cpp's sig_powmod on a map's signature, in one implementation.
static std::string Decode(const Api &api, const BYTE *pbSig)
{
  void *hMod = Make(api, Bytes(s_abKey, s_abKey + sizeof s_abKey));
  void *hExp = Make(api, Bytes(s_abExp, s_abExp + sizeof s_abExp));
  void *hBase = Make(api, Bytes(pbSig, pbSig + sizeof s_abKey)), *hResult = NULL;
  api.New(&hResult);
  api.PowMod(hResult, hBase, hExp, hMod);
  BYTE ab[sizeof s_abKey];
  DWORD cb = 0;
  api.To(hResult, ab, sizeof ab, &cb);
  if (cb < sizeof ab)
    memset(ab + cb, 0, sizeof ab - cb);
  api.Del(hResult), api.Del(hBase), api.Del(hExp), api.Del(hMod);
  return Hex(ab, sizeof ab);
}

static void MapsIn(const std::string &folder)
{
  WIN32_FIND_DATAA fd;
  HANDLE hFind = FindFirstFileA((folder + "\\*").c_str(), &fd);
  if (hFind == INVALID_HANDLE_VALUE)
    return;
  do {
    const std::string name = fd.cFileName;
    if (name == "." || name == "..")
      continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      MapsIn(folder + "\\" + name);
      continue;
    }
    Bytes data;
    if (!ReadFileBytes(folder + "\\" + name, data))
      continue;
    ++s_nMaps;
    const DWORD cbTail = sizeof s_abKey + 4;
    if (data.size() < cbTail || memcmp(&data[data.size() - cbTail], "NGIS", 4) != 0)
      continue;
    ++s_nSigned;
    const BYTE *pbSig = &data[data.size() - sizeof s_abKey];
    const std::string a = Decode(s_storm, pbSig), b = Decode(s_ours, pbSig);
    Check("map signature", a, b, folder + "\\" + name);
    // The hash's padding: 0xBB after the 20 bytes of SHA-1, 0x0B last.
    if (a.size() == 2 * sizeof s_abKey && a.substr(a.size() - 2) == "0B" && a.substr(40, 8) == "BBBBBBBB")
      ++s_nPadded;
  } while (FindNextFileA(hFind, &fd));
  FindClose(hFind);
}

int main(int argc, char **argv)
{
  if (argc < 3) {
    printf("usage: big_test.exe <install folder> <StarCraft.exe> [rounds] [map folders]\n");
    return 2;
  }
  const std::string install = argv[1];
  HMODULE hStorm = LoadLibraryA((install + "\\storm.dll").c_str());
  if (!hStorm) {
    printf("cannot load %s\\storm.dll\n", install.c_str());
    return 2;
  }
  s_storm.New = reinterpret_cast<PFN_NEW>(GetProcAddress(hStorm, MAKEINTRESOURCEA(624)));
  s_storm.Del = reinterpret_cast<PFN_DEL>(GetProcAddress(hStorm, MAKEINTRESOURCEA(606)));
  s_storm.From = reinterpret_cast<PFN_FROM>(GetProcAddress(hStorm, MAKEINTRESOURCEA(609)));
  s_storm.To = reinterpret_cast<PFN_TO>(GetProcAddress(hStorm, MAKEINTRESOURCEA(638)));
  s_storm.PowMod = reinterpret_cast<PFN_POWMOD>(GetProcAddress(hStorm, MAKEINTRESOURCEA(628)));
  Bytes exe;
  // The public key and exponent map_load.cpp passes (sig_pubkey_g, sig_exponent_g).
  if (!ReadFileBytes(argv[2], exe) || !ReadAtVa(exe, 0x4EEC90, s_abKey, sizeof s_abKey) ||
      !ReadAtVa(exe, 0x4EED90, s_abExp, sizeof s_abExp)) {
    printf("cannot read the key from %s\n", argv[2]);
    return 2;
  }
  const int nRounds = argc > 3 ? atoi(argv[3]) : 20000;

  // Fixed cases first: zero, one, powers of 0, modulo 1, results of zero.
  static const Bytes kZero, kOne(1, 1), kSeven(1, 7), kThree(1, 3);
  PowModCase(kThree, kZero, kSeven, false);
  PowModCase(kThree, kZero, kOne, false);
  PowModCase(kZero, kZero, kSeven, false);
  PowModCase(kThree, kThree, kOne, false);
  PowModCase(kSeven, kThree, kSeven, true);
  // A division whose first guess at a digit is one too many and must be added
  // back, which random numbers almost never meet: 0x7FFFFFFF_80000000_0_0 modulo
  // 0x80000000_0_1, as a power of 1.
  static const BYTE kAddBackU[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0xFF, 0xFF, 0xFF, 0x7F};
  static const BYTE kAddBackV[] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80};
  PowModCase(Bytes(kAddBackU, kAddBackU + sizeof kAddBackU), kOne,
             Bytes(kAddBackV, kAddBackV + sizeof kAddBackV), false);
  for (int i = 0; i < nRounds; ++i) {
    BytesCase();
    RandomPowMod();
  }
  const int nRandom = s_nCases;
  for (int i = 4; i < argc; ++i)
    MapsIn(argv[i]);
  printf("%d cases at random, %d maps, %d with a signature, %d of them decoding to its padding, "
         "%d differences\n",
         nRandom, s_nMaps, s_nSigned, s_nPadded, s_nDiffs);
  return s_nDiffs ? 1 : 0;
}
