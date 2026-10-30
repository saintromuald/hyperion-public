// float_test.cpp -- the game's floating-point arithmetic over every input it can take,
// for comparing the x86 build with the x64 one.
//
//   float_test.exe <out.txt>
//
// 1.17 is built with VS2013 for x86 and its C runtime; the x64 build is VS 2026's and
// its runtime, whose sqrt, pow and conversions are other code. The game computes in
// floating point in four places, copied here as they are written:
//   * TAI_AreaStrength.cpp, a unit's strength: sqrt of a DWORD, * 758.0 / 100.0, to
//     DWORD (the simulation, through the AI);
//   * select.cpp, select_form_calc_offset: sqrt of an int * 2.0, to int, to WORD (the
//     simulation, a group's formation spread);
//   * gds_vidinimo.cpp, apply_gamma_palette: pow of each palette byte / 256.0 by
//     (200 - gamma) / 100.0, * 256.0, to int, to BYTE, for the gamma set_gamma allows,
//     60 to 140 (the screen only);
//   * main.cpp's free disk space, which is compared with 20.0 once at start-up and not
//     tested here.
// The two sqrt columns are run for all 2^32 inputs and written as a digest per 2^24
// inputs; the gamma table is written whole. Built for x86 with the oracle's compiler
// (tools/build_porttest.bat) and for x64 with VS 2026 (tools/build_porttest_x64.bat),
// the two files must be the same.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>

// TAI_AreaStrength.cpp's last line.
static __declspec(noinline) DWORD AreaStrength(DWORD dwTotal)
{
  return static_cast<DWORD>(sqrt(static_cast<double>(dwTotal)) * 758.0 / 100.0);
}

// select_form_calc_offset's spread.
static __declspec(noinline) WORD FormSpread(int nArea)
{
  int nSpread = (WORD)(int)sqrt((double)nArea * 2.0);
  return (WORD)nSpread;
}

// apply_gamma_palette's curve for one byte.
static __declspec(noinline) BYTE Gamma(DWORD dwGamma, BYTE b)
{
  double dExp = (200 - dwGamma) / 100.0;
  return static_cast<BYTE>(static_cast<int>(pow(b / 256.0, dExp) * 256.0));
}

static uint64_t Fnv(uint64_t h, uint32_t v)
{
  for (int i = 0; i < 4; ++i) {
    h ^= (v >> (8 * i)) & 0xFF;
    h *= 0x100000001B3ull;
  }
  return h;
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: float_test.exe <out.txt>\n");
    return 2;
  }
  FILE *f = fopen(argv[1], "w");
  if (!f) {
    printf("cannot write %s\n", argv[1]);
    return 2;
  }
  DWORD t0 = GetTickCount();
  for (int col = 0; col < 2; ++col) {
    const char *name = col ? "form_spread" : "area_strength";
    for (uint32_t chunk = 0; chunk < 256; ++chunk) {
      uint64_t h = 0xCBF29CE484222325ull;
      for (uint32_t low = 0; low < (1u << 24); ++low) {
        uint32_t x = (chunk << 24) | low;
        h = Fnv(h, col ? FormSpread((int)x) : AreaStrength(x));
      }
      fprintf(f, "%s %02x %016llx\n", name, chunk, (unsigned long long)h);
    }
    printf("%s: done (%lu ms)\n", name, GetTickCount() - t0);
  }
  for (DWORD g = 60; g <= 140; ++g) {
    fprintf(f, "gamma %u", g);
    for (int b = 0; b < 256; ++b)
      fprintf(f, " %02x", Gamma(g, (BYTE)b));
    fprintf(f, "\n");
  }
  printf("gamma: done\n");
  fclose(f);
  return 0;
}
