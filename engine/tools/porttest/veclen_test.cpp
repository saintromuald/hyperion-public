// veclen_test.cpp -- checks PortVeclen and PortVeclenPrecise against the oracle.
//
//   veclen_test.exe <path to the 1.17 StarCraft.exe> [range]
//
// Reads the two routines' machine code out of the exe at run time (nothing of
// Blizzard's is in the repo), runs it from executable memory, and compares its
// result with ours for:
//   * every difference in [-range, range) on both axes (default 16384, which
//     covers any two points on a 256x256-tile map with room to spare), with the
//     points themselves moved around so argument order is exercised too;
//   * random full-range 32-bit arguments;
//   * edge values (0, powers of two and their neighbours, INT_MIN, INT_MAX, ...)
//     crossed with each other in all four arguments.
// Both routines are position-independent (short jumps only, no absolute
// addresses), so a copy runs anywhere. The exe is checked against the oracle's
// SHA-1 first. Exit code 0 means no difference was found.
//
// Each routine's results are also written as a digest. The x64 build
// (tools/build_porttest_x64.bat) cannot run the oracle's 32-bit code, so it runs
// ours alone and prints the digests, which must be the x86 build's.
#include "oracle_image.h"
#include "../../src117/port/gameplay/port_veclen.h"

typedef int(__fastcall *VecFn)(int ax, int bx, int ay, int by);

struct Routine {
  const char *name;
  uint32_t va;
  uint32_t size;
  VecFn port;
  VecFn oracle;
  uint64_t checked;
  uint64_t diffs;
  uint64_t digest;
};

static void Check(Routine &r, int ax, int bx, int ay, int by)
{
  int got = r.port(ax, bx, ay, by);
  ++r.checked;
  for (int i = 0; i < 4; ++i) {
    r.digest ^= ((uint32_t)got >> (8 * i)) & 0xFF;
    r.digest *= 0x100000001B3ull;
  }
  if (!r.oracle)
    return;
  int want = r.oracle(ax, bx, ay, by);
  if (want != got && r.diffs++ < 10)
    printf("  DIFF %s(%d, %d, %d, %d): oracle %d, port %d\n", r.name, ax, bx, ay, by, want, got);
}

static uint32_t g_rng = 0x2545F491u;
static uint32_t Rand32()
{
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: veclen_test.exe <StarCraft.exe 1.17> [range]\n");
    return 2;
  }
  int range = argc > 2 ? atoi(argv[2]) : 16384;

  Routine routines[] = {
    {"veclen", 0x405CF0u, 77u, &PortVeclen, NULL, 0, 0, 0xCBF29CE484222325ull},
    {"veclen_precise", 0x405C90u, 93u, &PortVeclenPrecise, NULL, 0, 0, 0xCBF29CE484222325ull},
  };
  const int nRoutines = sizeof routines / sizeof routines[0];
#ifndef _WIN64
  BYTE *image = LoadOracle(argv[1]);
  if (!image)
    return 2;
  for (int i = 0; i < nRoutines; ++i) {
    long off = FileOffset(image, routines[i].va);
    void *code = VirtualAlloc(NULL, routines[i].size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (off < 0 || !code) {
      printf("cannot place %s\n", routines[i].name);
      return 2;
    }
    memcpy(code, image + off, routines[i].size);
    routines[i].oracle = reinterpret_cast<VecFn>(code);
  }
#endif

  DWORD t0 = GetTickCount();

  // Every difference in the range, from points that move with each row.
  for (int dx = -range; dx < range; ++dx) {
    int bx = (int)Rand32(), by = (int)Rand32();
    for (int dy = -range; dy < range; ++dy) {
      for (int i = 0; i < nRoutines; ++i)
        Check(routines[i], bx + dx, bx, by + dy, by);
    }
  }
  printf("differences in [%d, %d) on both axes: done (%lu ms)\n", -range, range, GetTickCount() - t0);

  // Random full-range arguments.
  for (int n = 0; n < 100000000; ++n) {
    int ax = (int)Rand32(), bx = (int)Rand32(), ay = (int)Rand32(), by = (int)Rand32();
    for (int i = 0; i < nRoutines; ++i)
      Check(routines[i], ax, bx, ay, by);
  }
  printf("random 32-bit arguments: done (%lu ms)\n", GetTickCount() - t0);

  // Edge values in all four arguments.
  static const uint32_t base[] = {0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256,
                                  8191, 8192, 32767, 32768, 65535, 65536, 0x2AAAAAAAu, 0x3FFFFFFFu,
                                  0x40000000u, 0x55555555u, 0x7FFFFFFEu, 0x7FFFFFFFu};
  int edges[2 * sizeof base / sizeof base[0] + 1];
  int nEdges = 0;
  for (size_t i = 0; i < sizeof base / sizeof base[0]; ++i) {
    edges[nEdges++] = (int)base[i];
    edges[nEdges++] = (int)(0u - base[i]);
  }
  edges[nEdges++] = (int)0x80000000u;  // INT_MIN, which no negation above reaches
  for (int a = 0; a < nEdges; ++a)
    for (int b = 0; b < nEdges; ++b)
      for (int c = 0; c < nEdges; ++c)
        for (int d = 0; d < nEdges; ++d)
          for (int i = 0; i < nRoutines; ++i)
            Check(routines[i], edges[a], edges[b], edges[c], edges[d]);
  printf("edge values (%d) in all four arguments: done (%lu ms)\n", nEdges, GetTickCount() - t0);

  int bad = 0;
  for (int i = 0; i < nRoutines; ++i) {
    printf("%-15s %llu inputs, %llu differences, digest %016llx\n", routines[i].name,
           routines[i].checked, routines[i].diffs, routines[i].digest);
    bad += routines[i].diffs != 0;
  }
#ifdef _WIN64
  printf("x64: no oracle to run; the digests must be the x86 build's\n");
  return 0;
#else
  printf(bad ? "FAIL\n" : "OK: identical to the oracle on every input\n");
  return bad ? 1 : 0;
#endif
}
