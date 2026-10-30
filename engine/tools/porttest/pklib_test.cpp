// pklib_test.cpp -- checks PKWARE's implode, explode and CRC-32 as the port has
// them (src117/port/data/port_pklib.cpp, StormLib's pklib) against the original's,
// run at their own addresses.
//
//   pklib_test.exe <path to the 1.17 StarCraft.exe> [calls] [replay or folder ...]
//
// It is laid out like draw_test (build_porttest.bat): the oracle's code and
// data are copied into the blank section over 1.17's range, so the library at
// 0x401000 finds its tables in .data where it expects them. Each case runs the
// original and ours on the same input with the same callbacks and compares the
// return value and every byte written:
//   * implode, for both literal modes and the three dictionary sizes, on
//     random bytes, on bytes from a few symbols, on runs, on text and on
//     zeros, from empty to several work buffers long; then explode, both on
//     the original's output, which must give the input back;
//   * the same with the callbacks handing over a few bytes at a time;
//   * explode on the original's output cut short and with bytes changed;
//   * crc32_pklib on random blocks from random seeds;
//   * every section of every replay named, as compress wrote it: each block
//     that was imploded is exploded by both, and each block's bytes imploded
//     by ours must be what the file holds, or no smaller when the file holds
//     them as they are; each section's CRC must be the file's. The library
//     reads part of its work buffer before it writes it, so the last bytes
//     of a block depend on what the buffer held, in the original and in ours
//     alike; a block that is not the file's is compared with the original's
//     from the same zeroed buffer instead, and noted.
// Exit code 0 means no difference was found.
#include "oracle_image.h"
#include <string>
#include <vector>
#include "../../src117/h/compress.h"

typedef unsigned int(__cdecl *ImplodeFn)(PFN_COMP_READ, PFN_COMP_WRITE, char *, void *, unsigned int *,
                                         unsigned int *);
typedef unsigned int(__cdecl *ExplodeFn)(PFN_COMP_READ, PFN_COMP_WRITE, char *, void *);
typedef unsigned int(__cdecl *Crc32Fn)(const void *, unsigned int *, unsigned int *);

static const ImplodeFn s_oracleImplode = reinterpret_cast<ImplodeFn>(0x401000u);
static const ExplodeFn s_oracleExplode = reinterpret_cast<ExplodeFn>(0x401AE0u);
static const Crc32Fn s_oracleCrc32 = reinterpret_cast<Crc32Fn>(0x402160u);

static const size_t IMPLODE_WORK = 0x8DD8;
static const size_t EXPLODE_WORK = 0x3134;

static uint32_t g_rng = 0x9E3779B9u;
static uint32_t Rand32()
{
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static int RandBelow(int n) { return static_cast<int>(Rand32() % static_cast<uint32_t>(n)); }

static bool RangeIsOurs(void)
{
  MEMORY_BASIC_INFORMATION mbi;
  if (VirtualQuery(reinterpret_cast<void *>(0x401000u), &mbi, sizeof mbi) == sizeof mbi &&
      mbi.State == MEM_COMMIT &&
      (mbi.Protect == PAGE_EXECUTE_READWRITE || mbi.Protect == PAGE_EXECUTE_WRITECOPY) &&
      reinterpret_cast<uint32_t>(mbi.BaseAddress) + mbi.RegionSize >= 0x700000u)
    return true;
  printf("0x401000..0x700000 is not this exe's placeholder; build it with build_porttest.bat\n");
  return false;
}

// Every section's file bytes to its VA: the library's code and its tables.
static bool CopyImage(const BYTE *image)
{
  const IMAGE_DOS_HEADER *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(image);
  const IMAGE_NT_HEADERS32 *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(image + dos->e_lfanew);
  const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
  for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
    uint32_t va = nt->OptionalHeader.ImageBase + sec->VirtualAddress;
    uint32_t cb = sec->SizeOfRawData < sec->Misc.VirtualSize ? sec->SizeOfRawData : sec->Misc.VirtualSize;
    if (va < 0x401000u || va + cb > 0x700000u) {
      if (va <= 0x401000u)
        return false;
      continue;  // the resources, which the library does not read
    }
    memcpy(reinterpret_cast<void *>(va), image + sec->PointerToRawData, cb);
  }
  return true;
}

// The callbacks' state: the input, how far it has been read, how much one read
// may take (0 for all it asks), and everything written.
struct Stream {
  const BYTE *pIn;
  unsigned int nIn;
  unsigned int nPos;
  unsigned int nChunk;
  std::vector<BYTE> out;
  unsigned int nReads;
  unsigned int nWrites;
};

static unsigned int __cdecl StreamRead(char *pBuf, unsigned int *pnSize, void *pParam)
{
  Stream *s = static_cast<Stream *>(pParam);
  unsigned int n = s->nIn - s->nPos;
  if (*pnSize < n)
    n = *pnSize;
  if (s->nChunk && s->nChunk < n)
    n = s->nChunk;
  memcpy(pBuf, s->pIn + s->nPos, n);
  s->nPos += n;
  s->nReads++;
  return n;
}

static void __cdecl StreamWrite(char *pBuf, unsigned int *pnSize, void *pParam)
{
  Stream *s = static_cast<Stream *>(pParam);
  s->out.insert(s->out.end(), reinterpret_cast<BYTE *>(pBuf), reinterpret_cast<BYTE *>(pBuf) + *pnSize);
  s->nWrites++;
}

struct Result {
  unsigned int nRet;
  std::vector<BYTE> out;
};

static std::vector<BYTE> s_work(IMPLODE_WORK);

// One implode or explode, the work buffer filled with bFill first.
static Result RunImplode(ImplodeFn fn, const BYTE *p, unsigned int n, unsigned int nChunk, unsigned int nType,
                         unsigned int nDict, BYTE bFill)
{
  Stream s = {p, n, 0, nChunk};
  memset(&s_work[0], bFill, s_work.size());
  Result r;
  r.nRet = fn(StreamRead, StreamWrite, reinterpret_cast<char *>(&s_work[0]), &s, &nType, &nDict);
  r.out.swap(s.out);
  return r;
}

static Result RunExplode(ExplodeFn fn, const BYTE *p, unsigned int n, unsigned int nChunk, BYTE bFill)
{
  Stream s = {p, n, 0, nChunk};
  memset(&s_work[0], bFill, s_work.size());
  Result r;
  r.nRet = fn(StreamRead, StreamWrite, reinterpret_cast<char *>(&s_work[0]), &s);
  r.out.swap(s.out);
  return r;
}

static ImplodeFn PortImplode() { return &implode; }
static ExplodeFn PortExplode() { return &explode; }

static int g_nCases = 0;
static int g_nDiffs = 0;

static bool Same(const Result &a, const Result &b, const char *pszWhat, const std::string &where)
{
  ++g_nCases;
  if (a.nRet == b.nRet && a.out == b.out)
    return true;
  if (g_nDiffs++ < 20) {
    size_t i = 0;
    while (i < a.out.size() && i < b.out.size() && a.out[i] == b.out[i])
      ++i;
    printf("DIFF %s %s: returned %u and %u, wrote %u and %u bytes, first difference at %u\n", pszWhat,
           where.c_str(), a.nRet, b.nRet, static_cast<unsigned>(a.out.size()),
           static_cast<unsigned>(b.out.size()), static_cast<unsigned>(i));
  }
  return false;
}

// Input of one of the kinds the header lists.
static void MakeInput(std::vector<BYTE> &v, unsigned int n, int nKind)
{
  static const char s_szText[] = "The quick brown fox jumps over the lazy dog. Zerg rush kekeke! gg wp\r\n";
  v.resize(n);
  int nSymbols = 2 + RandBelow(14);
  for (unsigned int i = 0; i < n; ++i) {
    switch (nKind) {
    case 0: v[i] = static_cast<BYTE>(Rand32() >> 24); break;
    case 1: v[i] = static_cast<BYTE>(RandBelow(nSymbols) * 17); break;
    case 2: v[i] = i && RandBelow(8) ? v[i - 1] : static_cast<BYTE>(Rand32() >> 24); break;
    case 3: v[i] = static_cast<BYTE>(s_szText[(i * 7 + RandBelow(3)) % (sizeof s_szText - 1)]); break;
    default: v[i] = 0; break;
    }
  }
}

static unsigned int RandLength(void)
{
  switch (RandBelow(6)) {
  case 0: return RandBelow(16);
  case 1: return 0x2000 - 8 + RandBelow(17);  // compress's blocks, and next to them
  case 2: return 0x1000 * (1 + RandBelow(4)) - 2 + RandBelow(5);
  case 3: return RandBelow(0x40000);  // several work buffers
  default: return RandBelow(0x2001);
  }
}

static void RandomCases(int nCalls)
{
  static const unsigned int s_anDict[] = {0x400, 0x800, 0x1000};
  std::vector<BYTE> in;
  for (int nCall = 0; nCall < nCalls; ++nCall) {
    const int nKind = RandBelow(5);
    MakeInput(in, RandLength(), nKind);
    const unsigned int nType = RandBelow(2);
    const unsigned int nDict = s_anDict[RandBelow(3)];
    const unsigned int nChunk = RandBelow(4) == 0 ? 1 + RandBelow(64) : 0;
    const BYTE *p = in.empty() ? NULL : &in[0];
    const unsigned int n = static_cast<unsigned int>(in.size());
    char sz[96];
    sprintf(sz, "#%d kind %d, %u bytes, type %u, dict 0x%X, chunk %u", nCall, nKind, n, nType, nDict, nChunk);

    Result a = RunImplode(s_oracleImplode, p, n, nChunk, nType, nDict, 0);
    Result b = RunImplode(PortImplode(), p, n, nChunk, nType, nDict, 0);
    Same(a, b, "implode", sz);
    // the work buffer as the heap leaves it: neither may depend on it
    if (nCall % 16 == 0) {
      const BYTE bFill = static_cast<BYTE>(Rand32());
      Result c = RunImplode(s_oracleImplode, p, n, nChunk, nType, nDict, bFill);
      Result d = RunImplode(PortImplode(), p, n, nChunk, nType, nDict, bFill);
      Same(c, d, "implode, work buffer not zeroed", sz);
      if (c.out != a.out)
        printf("note %s: the original's implode wrote other bytes with the work buffer filled with 0x%02X\n", sz, bFill);
    }

    const BYTE *pz = a.out.empty() ? NULL : &a.out[0];
    const unsigned int nz = static_cast<unsigned int>(a.out.size());
    Result x = RunExplode(s_oracleExplode, pz, nz, nChunk, 0);
    Result y = RunExplode(PortExplode(), pz, nz, nChunk, 0);
    Same(x, y, "explode", sz);
    ++g_nCases;
    if (x.nRet == 0 && x.out != in && g_nDiffs++ < 20)
      printf("DIFF round trip %s: the original's explode did not give the input back\n", sz);

    // cut short, or with a few bytes changed
    if (nz > 2 && nCall % 4 == 0) {
      std::vector<BYTE> bad(a.out.begin(), a.out.end());
      if (RandBelow(2))
        bad.resize(2 + RandBelow(nz - 2));
      else
        for (int k = 1 + RandBelow(4); k > 0; --k)
          bad[2 + RandBelow(nz - 2)] ^= static_cast<BYTE>(1 + RandBelow(255));
      const unsigned int nb = static_cast<unsigned int>(bad.size());
      Same(RunExplode(s_oracleExplode, &bad[0], nb, nChunk, 0), RunExplode(PortExplode(), &bad[0], nb, nChunk, 0),
           "explode, damaged", sz);
    }

    // crc32_pklib from a random seed
    unsigned int nCrcLen = n, nCrcLen2 = n;
    unsigned int dwSeed = Rand32(), dwSeed2 = dwSeed;
    Result ca, cb;
    ca.nRet = s_oracleCrc32(p, &nCrcLen, &dwSeed);
    cb.nRet = crc32_pklib(p, &nCrcLen2, &dwSeed2);
    Same(ca, cb, "crc32_pklib", sz);
  }
}

// A replay, section by section, as compress wrote it (tools/replaytest/repfile.py).
static void ReplayCases(const char *pszPath)
{
  FILE *f = fopen(pszPath, "rb");
  if (!f) {
    printf("cannot open %s\n", pszPath);
    ++g_nDiffs;
    return;
  }
  std::vector<BYTE> file;
  fseek(f, 0, SEEK_END);
  file.resize(ftell(f));
  fseek(f, 0, SEEK_SET);
  if (!file.empty())
    fread(&file[0], 1, file.size(), f);
  fclose(f);

  size_t pos = 0;
  int nSection = 0, nBlocks = 0;
  // Reads one section of nLen bytes into out; false at a malformed one.
  auto section = [&](unsigned int nLen, std::vector<BYTE> &out) -> bool {
    out.clear();
    if (nLen == 0)
      return true;
    if (pos + 8 > file.size())
      return false;
    unsigned int dwCrc, nCount;
    memcpy(&dwCrc, &file[pos], 4);
    memcpy(&nCount, &file[pos + 4], 4);
    pos += 8;
    for (unsigned int i = 0; i < nCount; ++i) {
      unsigned int nSize;
      if (pos + 4 > file.size())
        return false;
      memcpy(&nSize, &file[pos], 4);
      pos += 4;
      if (pos + nSize > file.size())
        return false;
      const BYTE *pBlock = &file[pos];
      pos += nSize;
      const unsigned int nWant = nLen - i * 0x2000 < 0x2000 ? nLen - i * 0x2000 : 0x2000;
      char sz[400];
      sprintf(sz, "%s section %d block %u", pszPath, nSection, i);
      std::vector<BYTE> plain;
      if (nSize == nWant) {
        plain.assign(pBlock, pBlock + nSize);
      } else {
        Result a = RunExplode(s_oracleExplode, pBlock, nSize, 0, 0);
        Result b = RunExplode(PortExplode(), pBlock, nSize, 0, 0);
        Same(a, b, "explode", sz);
        plain.swap(a.out);
      }
      // compress's implode: binary literals, a 1 KB dictionary
      const BYTE *pp = plain.empty() ? NULL : &plain[0];
      Result c = RunImplode(PortImplode(), pp, static_cast<unsigned int>(plain.size()), 0, 0, 0x400, 0);
      ++g_nCases;
      const bool bStored = nSize == nWant;
      if (bStored ? c.out.size() < nWant : (c.out.size() != nSize || memcmp(&c.out[0], pBlock, nSize) != 0)) {
        // The library reads some of its work buffer before writing it, and the
        // game's comes from the heap, so a block the game wrote can end in
        // other bytes than a zeroed buffer gives. The original on a zeroed
        // buffer is then the reference.
        Result o = RunImplode(s_oracleImplode, pp, static_cast<unsigned int>(plain.size()), 0, 0, 0x400, 0);
        if (Same(o, c, "implode", sz))
          printf("note %s: the file's %u bytes are not what a zeroed work buffer gives, the original's or ours\n",
                 sz, nSize);
      }
      out.insert(out.end(), plain.begin(), plain.end());
      ++nBlocks;
    }
    unsigned int nCrcLen = static_cast<unsigned int>(out.size()), dwSeed = 0xFFFFFFFFu;
    ++g_nCases;
    if (crc32_pklib(out.empty() ? NULL : &out[0], &nCrcLen, &dwSeed) != dwCrc && g_nDiffs++ < 20)
      printf("DIFF crc32_pklib %s section %d: not the file's CRC\n", pszPath, nSection);
    ++nSection;
    return true;
  };

  std::vector<BYTE> data;
  if (!section(4, data) || data.size() != 4 || memcmp(&data[0], "reRS", 4) != 0) {
    printf("%s is not a replay\n", pszPath);
    ++g_nDiffs;
    return;
  }
  if (!section(0x279, data)) {
    printf("%s: bad header section\n", pszPath);
    ++g_nDiffs;
    return;
  }
  // then pairs of a length and a section that long: the commands, the map.
  // Some replays carry more after the map, which compress did not write: it
  // starts with a CRC and a block count of 0.
  while (pos + 8 <= file.size()) {
    unsigned int adwHead[2];
    memcpy(adwHead, &file[pos], 8);
    if (adwHead[1] == 0) {
      printf("%s: %u bytes after the sections, not compress's\n", pszPath, static_cast<unsigned>(file.size() - pos));
      break;
    }
    if (!section(4, data) || data.size() != 4)
      break;
    unsigned int nLen;
    memcpy(&nLen, &data[0], 4);
    if (!section(nLen, data)) {
      printf("%s: section %d is cut short\n", pszPath, nSection);
      ++g_nDiffs;
      return;
    }
  }
  printf("%s: %d sections, %d blocks\n", pszPath, nSection, nBlocks);
}

static void ReplayPath(const std::string &path)
{
  DWORD attr = GetFileAttributesA(path.c_str());
  if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((path + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
      return;
    do {
      std::string name = fd.cFileName;
      if (name == "." || name == "..")
        continue;
      std::string sub = path + "\\" + name;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        ReplayPath(sub);
      else if (name.size() > 4 && _stricmp(name.c_str() + name.size() - 4, ".rep") == 0)
        ReplayCases(sub.c_str());
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return;
  }
  ReplayCases(path.c_str());
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: pklib_test.exe <StarCraft.exe 1.17> [calls] [replay or folder ...]\n");
    return 2;
  }
  setvbuf(stdout, NULL, _IONBF, 0);
  if (!RangeIsOurs())
    return 2;
  const int nCalls = argc > 2 ? atoi(argv[2]) : 4000;

  BYTE *image = LoadOracle(argv[1]);
  if (!image || !CopyImage(image))
    return 2;

  RandomCases(nCalls);
  printf("random: %d cases\n", g_nCases);
  for (int i = 3; i < argc; ++i)
    ReplayPath(argv[i]);

  printf("%d cases, %d differences\n", g_nCases, g_nDiffs);
  return g_nDiffs ? 1 : 0;
}
